// =====================================================================
// flowcoro/bounded_channel.h
//
// 有界 MPMC 无锁队列（Vyukov 逐 slot sequence 算法）。
//
// 为什么不是复用现成的两个容器：
//
//   · lockfree::RingBuffer<T, Size>（lockfree.h）是 SPSC，不是 MPMC。
//     pop() 里 head 走 load→check→store（非 CAS），两个消费者会读到同一个
//     head 并把同一元素投递两次；更糟的是其中一方可能用陈旧的 current_head
//     把 head 写回去，造成 head 回退 + 该位置的元素永久滞留。它的 Size 还是
//     编译期模板参数，无法运行时指定容量。
//
//   · lockfree::Queue<T> 本身是正确的 Michael-Scott MPMC，但它是无界的，
//     且每条消息都要 pool_malloc 一个节点、dequeue 要占 2 个 hazard slot、
//     retire() 走自旋锁（hazard_pointer.h），而 hazard 线程表上限 128
//     （hazard_pointer.h 中 MAX_HAZARD_THREADS，超限共享 slot 的注释自己
//     承认不安全）。高频路径上这是全局争用点，Python 侧多线程调用还会
//     撞上线程数上限。
//
// Vyukov 环：无分配、无 SMR、容量运行时可定（向上取 2 的幂）、push/pop 均非
// 阻塞（满/空立即返回 false）。每个 slot 自带 sequence，用它同时表达
// 「该 slot 属于第几轮」和「空/满」两件事，因此不需要 head/tail 哨兵，
// 满容量 N 个元素全部可用。
//
// 类型要求：T 需 nothrow 可移动构造/赋值且可析构 —— 位置一旦 CAS 占用就无法
// 回滚，若移动构造抛异常会让队列状态与 sequence 不一致。std::string /
// std::vector / std::shared_ptr 等均满足。
//
// 容量上界语义：size() 是近似值（采样跨两个原子读），但恒有
// size() <= capacity()。
// =====================================================================

#ifndef FLOWCORO_BOUNDED_CHANNEL_H
#define FLOWCORO_BOUNDED_CHANNEL_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace flowcoro {

template <typename T>
class BoundedChannel {
    static_assert(std::is_nothrow_move_constructible_v<T>,
                  "BoundedChannel<T>: T 必须 nothrow 可移动构造");
    static_assert(std::is_nothrow_move_assignable_v<T>,
                  "BoundedChannel<T>: T 必须 nothrow 可移动赋值");
    static_assert(std::is_destructible_v<T>,
                  "BoundedChannel<T>: T 必须可析构");

public:
    // capacity 需 >= 1；内部向上取到 2 的幂。0 非法。
    explicit BoundedChannel(size_t capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("BoundedChannel: capacity must be >= 1");
        }
        size_t pow2 = 1;
        while (pow2 < capacity) pow2 <<= 1;
        capacity_ = pow2;
        mask_ = pow2 - 1;

        slots_ = new Slot[capacity_];
        for (size_t i = 0; i < capacity_; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~BoundedChannel() { delete[] slots_; }

    BoundedChannel(const BoundedChannel&) = delete;
    BoundedChannel& operator=(const BoundedChannel&) = delete;

    // 非阻塞入队。队列已满或已 close 时返回 false，value 不被消费。
    bool try_push(T value) {
        if (closed_.load(std::memory_order_acquire)) return false;

        Slot* slot = nullptr;
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            slot = &slots_[pos & mask_];
            const size_t seq = slot->sequence.load(std::memory_order_acquire);
            const std::intptr_t dif =
                static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

            if (dif == 0) {
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    break;  // 占位成功，pos 归本线程所有
                }
                // CAS 失败：pos 已被更新为当前值，重试
            } else if (dif < 0) {
                return false;  // slot 还在上一轮 → 队列满
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }

        ::new (static_cast<void*>(slot->storage)) T(std::move(value));
        slot->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    // 非阻塞出队。队列为空时返回 false 且不修改 out。
    bool try_pop(T& out) {
        Slot* slot = nullptr;
        size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            slot = &slots_[pos & mask_];
            const size_t seq = slot->sequence.load(std::memory_order_acquire);
            const std::intptr_t dif = static_cast<std::intptr_t>(seq) -
                                      static_cast<std::intptr_t>(pos + 1);

            if (dif == 0) {
                if (dequeue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    break;  // 占位成功；此时元素必然已被生产者发布
                }
            } else if (dif < 0) {
                return false;  // 生产者尚未发布该位置 → 空
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }

        T* data = reinterpret_cast<T*>(static_cast<void*>(slot->storage));
        out = std::move(*data);
        data->~T();
        // release：交还 slot 给生产者；sequence 前进一整轮
        slot->sequence.store(pos + capacity_, std::memory_order_release);
        return true;
    }

    // 近似元素数。用 enqueue_pos_ - dequeue_pos_ 表达：Vyukov 环的不变量保证
    // 任何时刻该差值 ∈ [0, capacity]（第 pos 个位置只有在 pos - capacity 被
    // 出队后才可再次入队），因此**不会**超过 capacity()。
    // 采样跨两个原子读，可能短暂低估（claim 了位置但还没发布的入队会被算进去），
    // 但绝不会高估，故可作为有界性的硬上界依据。
    size_t size() const noexcept {
        const size_t e = enqueue_pos_.load(std::memory_order_acquire);
        const size_t d = dequeue_pos_.load(std::memory_order_acquire);
        return e > d ? e - d : 0;
    }

    size_t capacity() const noexcept { return capacity_; }

    bool empty() const noexcept { return size() == 0; }

    // 关闭通道：try_push 立即开始返回 false；try_pop 仍可把已有元素取完。
    void close() noexcept { closed_.store(true, std::memory_order_release); }

    bool is_closed() const noexcept {
        return closed_.load(std::memory_order_acquire);
    }

private:
    struct Slot {
        std::atomic<size_t> sequence;
        alignas(T) unsigned char storage[sizeof(T)];
    };

    Slot* slots_ = nullptr;
    size_t capacity_ = 0;
    size_t mask_ = 0;

    alignas(64) std::atomic<size_t> enqueue_pos_{0};
    alignas(64) std::atomic<size_t> dequeue_pos_{0};
    std::atomic<bool> closed_{false};
};

}  // namespace flowcoro

#endif  // FLOWCORO_BOUNDED_CHANNEL_H
