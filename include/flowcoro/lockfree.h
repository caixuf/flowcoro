#pragma once
#include <atomic>
#include <memory>
#include <type_traits>
#include "memory_pool.h"
#include "hazard_pointer.h"

namespace lockfree {

// 使用flowcoro内存池的便利函数
using flowcoro::pool_malloc;
using flowcoro::pool_free;

// =====================================================================
// 无锁队列 (Michael-Scott Queue) — 修复版
//
// 原实现存在两个严重bug：
//   1) ABA / use-after-free: dequeue_unsafe 在 head.compare_exchange
//      成功后立即 pool_free(head_node)，但其他败者消费者可能仍在读
//      head_node->next 或 head_node->data。内存被 pool_malloc 立即复用
//      后即触发 UAF。
//   2) 数据丢失竞态: 多个消费者同时执行 next->data.exchange(nullptr)
//      取走数据，但只有一个能 CAS 成功。败者丢失了数据但没推进 head。
//
// 修复策略：
//   - 引入 hazard pointer (flowcoro::smr::HazardManager) 延迟回收节点，
//     节点只有在没有任何线程持有 hazard 时才会真正释放，根治 UAF。
//   - 调整 dequeue 顺序为 "CAS 先行 + 胜者独占读取数据"，败者只重试而
//     不会消费数据，消除数据丢失竞态。
// =====================================================================

template<typename T>
class Queue {
private:
    struct Node {
        std::atomic<T*> data{nullptr};
        std::atomic<Node*> next{nullptr};

        Node() = default;
        ~Node() {
            // 兜底：若析构时仍有 data，销毁之（不应发生在正常路径）
            T* data_ptr = data.load(std::memory_order_acquire);
            if (data_ptr) {
                data_ptr->~T();
                // 不在此处 pool_free —— data 嵌在 NodeWithData 块中
                // 由 reclaim_node 统一释放
            }
        }
    };

    // 单块分配 Node + T 数据，提高缓存局部性
    struct NodeWithData {
        Node node;
        alignas(T) char data_storage[sizeof(T)];
    };

    alignas(64) std::atomic<Node*> head;
    alignas(64) std::atomic<Node*> tail;
    alignas(64) std::atomic<bool> destroyed{false};

    // 计算 Node* 对应的 NodeWithData* 块地址
    static NodeWithData* block_of(Node* n) noexcept {
        return reinterpret_cast<NodeWithData*>(
            reinterpret_cast<char*>(n) - offsetof(NodeWithData, node));
    }

    // hazard pointer 用的回收函数：销毁 Node 并归还整块内存
    static void reclaim_node(void* p) noexcept {
        if (!p) return;
        Node* n = static_cast<Node*>(p);
        n->~Node();
        pool_free(block_of(n));
    }

    // 不带 SMR 的内部 drain，仅供析构使用（单线程上下文）
    bool drain_one(T& result) {
        Node* head_node = head.load(std::memory_order_acquire);
        if (!head_node) return false;
        Node* next = head_node->next.load(std::memory_order_acquire);
        if (!next) return false;
        T* data_ptr = next->data.exchange(nullptr, std::memory_order_acquire);
        if (!data_ptr) return false;
        result = std::move(*data_ptr);
        data_ptr->~T();
        Node* expected = head_node;
        if (head.compare_exchange_strong(expected, next,
                std::memory_order_acquire, std::memory_order_relaxed)) {
            head_node->~Node();
            pool_free(block_of(head_node));
        }
        return true;
    }

public:
    Queue() {
        NodeWithData* dummy_block = static_cast<NodeWithData*>(
            pool_malloc(sizeof(NodeWithData)));
        new(&dummy_block->node) Node();
        head.store(&dummy_block->node);
        tail.store(&dummy_block->node);
        destroyed.store(false, std::memory_order_release);
    }

    ~Queue() {
        // 标记析构，阻止新的 enqueue/dequeue
        destroyed.store(true, std::memory_order_release);

        // 单线程 drain 剩余节点（不需要 SMR）
        T unused{};
        while (drain_one(unused)) {}

        // 释放 dummy
        Node* h = head.load(std::memory_order_acquire);
        if (h) {
            h->~Node();
            pool_free(block_of(h));
            head.store(nullptr, std::memory_order_release);
        }

        // 尽力回收所有线程的 retired 节点
        flowcoro::smr::HazardManager::instance().drain_all();
    }

    void enqueue(T item) {
        if (destroyed.load(std::memory_order_acquire)) {
            return; // 队列已析构，丢弃
        }

        NodeWithData* block = static_cast<NodeWithData*>(
            pool_malloc(sizeof(NodeWithData)));
        new(&block->node) Node();

        T* data_ptr = reinterpret_cast<T*>(block->data_storage);
        new(data_ptr) T(std::move(item));
        // 在链接到链表前完成 data 的写入，确保消费者看到 next 时 data 已就绪
        block->node.data.store(data_ptr, std::memory_order_release);

        // MS enqueue: tail.exchange 后立刻设置 prev_tail->next
        // 期间消费者可能看到 head != tail 但 next == nullptr 的瞬态，
        // dequeue 会通过 retry + 帮助推进 tail 的方式正确处理
        Node* prev_tail = tail.exchange(&block->node, std::memory_order_acq_rel);
        if (prev_tail) {
            prev_tail->next.store(&block->node, std::memory_order_release);
        }
    }

    bool dequeue(T& result) {
        if (destroyed.load(std::memory_order_acquire)) {
            return false;
        }

        // 使用两个 hazard slot：
        //   slot 0 -> head_node (旧 dummy，将回收)
        //   slot 1 -> next      (新 dummy，将读取其 data)
        flowcoro::smr::HazardGuard h0(0), h1(1);

        while (true) {
            Node* head_node = head.load(std::memory_order_acquire);
            if (!head_node) return false;
            h0.protect(head_node);
            // 防止 TOCTOU：protect 后必须重新确认 head 未变
            if (head.load(std::memory_order_acquire) != head_node) continue;

            Node* tail_node = tail.load(std::memory_order_acquire);
            Node* next = head_node->next.load(std::memory_order_acquire);

            if (head_node == tail_node) {
                // head == tail
                if (!next) {
                    // 真正的空队列
                    return false;
                }
                // tail 落后，帮助推进
                tail.compare_exchange_strong(tail_node, next,
                    std::memory_order_release, std::memory_order_relaxed);
                continue;
            }

            // head != tail：next 必然非空（除 producer 瞬态外）
            if (!next) {
                // producer 正在执行 tail.exchange 但 prev_tail->next.store
                // 尚未完成。retry。
                continue;
            }
            h1.protect(next);
            // 再次确认 head 未被其他消费者改掉
            if (head.load(std::memory_order_acquire) != head_node) continue;

            // 关键修复：先 CAS 推进 head，胜者独占读取 next->data。
            // 败者不消费任何数据，避免原实现的丢数据竞态。
            //
            // Bug#2 修复：CAS 成功 order 从 acquire 改为 acq_rel。
            // CAS 成功是一次 RMW（读-改-写），写（head 前进）需要 release
            // 出去，让败者消费者在 acquire 重读 head 时能看到我们对 head
            // 的推进，从而与本线程「读走 data / retire()」之间形成
            // release/acquire 配对。原 acquire 只读不写，缺乏 release 语义。
            Node* expected = head_node;
            if (!head.compare_exchange_weak(expected, next,
                    std::memory_order_acq_rel,    // 成功：acq_rel（读 data 写 + 写 head 前进）
                    std::memory_order_relaxed)) { // 失败：relaxed（仅更新 expected）
                continue; // CAS 失败，重试
            }

            // CAS 成功：现在 next 是新 dummy，由我们独占。
            // 无人能再 next->data.exchange(nullptr)，因为其他消费者都
            // CAS 失败并在重试（它们读到的是新的 head）。
            T* data_ptr = next->data.exchange(nullptr, std::memory_order_acquire);
            if (data_ptr) {
                result = std::move(*data_ptr);
                data_ptr->~T();
            }
            // 安全回收旧 dummy：必须延迟到无任何线程持有 hazard
            flowcoro::smr::HazardManager::instance().retire(
                head_node, &Queue::reclaim_node);

            return data_ptr != nullptr;
        }
    }

    // Bug#3 修复：empty() 不再解引用 head_node。
    //
    // 原实现读 head_node->next，但 head_node 可能已被并发 dequeue+
    // retire+free → UAF。MS 队列的 empty 在并发下本就不可靠，
    // 这里用 head == tail 近似判断（不解引用任何节点）。
    // 注意：enqueue 后 tail.exchange 但 next 未 store 的瞬态下
    // head==tail 但非空，这是 MS 队列固有的瞬态，调用方不应依赖精确性。
    bool empty() const {
        Node* h = head.load(std::memory_order_acquire);
        Node* t = tail.load(std::memory_order_acquire);
        return h == t;
    }

    // 估算队列大小（仅近似）。
    //
    // ⚠️ Bug#3 警告：本方法沿 next 链遍历，无 hazard 保护。
    // 并发 dequeue 会 retire+free 节点，遍历中可能命中已释放内存 → UAF。
    // 仅可在单线程/quiesce（无并发 dequeue）时调用，禁止用于并发监控轮询。
    size_t size_estimate() const {
        if (destroyed.load(std::memory_order_acquire)) return 0;
        size_t count = 0;
        Node* current = head.load(std::memory_order_acquire);
        if (current) {
            current = current->next.load(std::memory_order_acquire);
            while (current && count < 10000) {
                current = current->next.load(std::memory_order_acquire);
                ++count;
            }
        }
        return count;
    }
};

// =====================================================================
// 无锁栈 (Treiber Stack) — 修复版
//
// 原实现：pop 成功后立即 pool_free，但其他 pop 中途读取了该节点
// 会触发 UAF；ABA 也存在（节点 push 回来后 cmpxchg 误判）。
// 修复：使用 hazard pointer 保护 head，并在 pop 成功后 retire 节点
// 而非立即释放。
// =====================================================================

template<typename T>
class Stack {
private:
    struct Node {
        T data;
        Node* next;
        explicit Node(T&& item) : data(std::move(item)), next(nullptr) {}
    };

    alignas(64) std::atomic<Node*> head{nullptr};

    static void reclaim_node(void* p) noexcept {
        if (!p) return;
        Node* n = static_cast<Node*>(p);
        n->~Node();
        pool_free(n);
    }

public:
    ~Stack() {
        // 单线程 drain
        Node* h = head.load(std::memory_order_acquire);
        while (h) {
            Node* next = h->next;
            h->~Node();
            pool_free(h);
            h = next;
        }
        head.store(nullptr, std::memory_order_release);
        // 尽力回收 retired
        flowcoro::smr::HazardManager::instance().drain_all();
    }

    void push(T item) {
        Node* new_node = static_cast<Node*>(pool_malloc(sizeof(Node)));
        new(new_node) Node(std::move(item));
        Node* old_head = head.load(std::memory_order_acquire);
        do {
            new_node->next = old_head;
        } while (!head.compare_exchange_weak(
            old_head, new_node,
            std::memory_order_release, std::memory_order_acquire));
    }

    bool pop(T& result) {
        flowcoro::smr::HazardGuard h0(0);
        while (true) {
            Node* old_head = head.load(std::memory_order_acquire);
            if (!old_head) return false;
            h0.protect(old_head);
            // 重新确认 head 未变（防止读到已被释放的节点）
            if (head.load(std::memory_order_acquire) != old_head) continue;

            // 读 next 必须在 hazard 保护下完成
            Node* next = old_head->next;
            //
            // Bug#2 修复：CAS 成功 order 从 acquire 改为 acq_rel。
            // 成功 CAS 是 RMW，写（head 前进）需 release 出去，让败者
            // acquire 重读 head 时能看到推进，与本线程 retire() 形成配对。
            if (head.compare_exchange_weak(old_head, next,
                    std::memory_order_acq_rel,    // 成功：acq_rel
                    std::memory_order_relaxed)) { // 失败：relaxed
                // CAS 成功，独占 old_head
                result = std::move(old_head->data);
                old_head->~Node();
                // 延迟回收：可能还有并发 pop 正在读 old_head
                flowcoro::smr::HazardManager::instance().retire(
                    old_head, &Stack::reclaim_node);
                return true;
            }
            // CAS 失败，重试
        }
    }

    bool empty() const {
        return head.load(std::memory_order_acquire) == nullptr;
    }
};

// 无锁环形缓冲区 (SPSC Ring Buffer) — 未改动，原实现正确
template<typename T, size_t Size>
class RingBuffer {
private:
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
    static constexpr size_t MASK = Size - 1;

    struct alignas(64) Slot {
        std::atomic<T> data{};
        std::atomic<bool> valid{false};
    };

    alignas(64) std::atomic<size_t> head{0};
    alignas(64) std::atomic<size_t> tail{0};
    alignas(64) Slot slots[Size];

public:
    bool push(T item) {
        size_t current_tail = tail.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) & MASK;

        if (next_tail == head.load(std::memory_order_acquire)) {
            return false; // 缓冲区满
        }

        slots[current_tail].data.store(std::move(item), std::memory_order_relaxed);
        slots[current_tail].valid.store(true, std::memory_order_release);
        tail.store(next_tail, std::memory_order_release);

        return true;
    }

    bool pop(T& result) {
        size_t current_head = head.load(std::memory_order_relaxed);

        if (current_head == tail.load(std::memory_order_acquire)) {
            return false; // 缓冲区空
        }

        if (!slots[current_head].valid.load(std::memory_order_acquire)) {
            return false;
        }

        result = std::move(slots[current_head].data.load(std::memory_order_relaxed));
        slots[current_head].valid.store(false, std::memory_order_release);
        head.store((current_head + 1) & MASK, std::memory_order_release);

        return true;
    }

    bool empty() const {
        return head.load(std::memory_order_acquire) == tail.load(std::memory_order_acquire);
    }

    bool full() const {
        size_t current_tail = tail.load(std::memory_order_acquire);
        size_t next_tail = (current_tail + 1) & MASK;
        return next_tail == head.load(std::memory_order_acquire);
    }
};

// 高性能原子计数器 — 未改动
class AtomicCounter {
private:
    alignas(64) std::atomic<size_t> count_{0};

public:
    size_t increment() {
        return count_.fetch_add(1, std::memory_order_relaxed);
    }

    size_t decrement() {
        return count_.fetch_sub(1, std::memory_order_relaxed);
    }

    size_t get() const {
        return count_.load(std::memory_order_acquire);
    }

    void set(size_t value) {
        count_.store(value, std::memory_order_release);
    }
};

} // namespace lockfree
