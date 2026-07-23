#pragma once

// =====================================================================
// flowcoro/cancellation.h
//
// 结构化取消支持：CancellationToken + 取消点 + CancellationTokenSource。
//
// 解决原 Task 取消机制的两个缺陷：
//   1) 取消标志嵌在 promise 里，无法跨多个子任务共享；
//   2) 没有协作式取消检查点，co_await 一个长任务时无法被取消。
//
// 设计要点：
//   - CancellationToken 是值类型，可拷贝、可传递（共享同一份状态）
//   - CancellationTokenSource 持有真正的状态，通过 token() 派发多个 token
//   - cancel() 是线程安全的（原子 release store）
//   - 取消点 awaiter 把取消检查嵌入到 co_await 路径上
// =====================================================================

#include <atomic>
#include <memory>
#include <chrono>
#include <coroutine>
#include <stdexcept>
#include <utility>
#include <mutex>
#include <vector>

namespace flowcoro {

// 取消时抛出的异常
class CancellationTokenException : public std::runtime_error {
public:
    CancellationTokenException()
        : std::runtime_error("operation was cancelled") {}
    explicit CancellationTokenException(const char* msg)
        : std::runtime_error(msg) {}
};

namespace detail {

struct CancellationState {
    std::atomic<bool> cancelled{false};

    // 注册的回调，用于级联取消子 token / 唤醒等待者。
    // 这里使用简单的 spinlock-protected 链表，性能不是关键路径。
    // 若取消已发生，新注册的回调会立即同步执行。
    //
    // FC-3 修复：加 unregistered/fired 原子标志 + unregister_callback。
    // 回调注册者（如 Task::promise_type）在析构前调 unregister，标记
    // unregistered=true。fire 跳过 unregistered 的 node，不执行 fn，
    // 避免解引用已释放的 promise。fire 不 delete node（unregister 可能
    // 并发访问），node 由 ~CancellationState 统一清理。
    struct CallbackNode {
        void (*fn)(void*);
        void* ctx;
        std::atomic<bool> unregistered{false};
        std::atomic<bool> fired{false};
        CallbackNode* next{nullptr};
    };

    CallbackNode* callbacks_head{nullptr};
    // 自旋锁保护 callbacks 链表
    std::atomic<bool> lock{false};

    // 返回 handle（node 指针）用于后续 unregister。
    // 若已取消，同步执行后返回 nullptr（无需 unregister）。
    void* register_callback(void (*fn)(void*), void* ctx) {
        auto* node = new CallbackNode{fn, ctx, false, false, nullptr};

        bool expected = false;
        while (!lock.compare_exchange_weak(
                expected, true, std::memory_order_acquire)) {
            expected = false;
        }

        // 在锁内检查 cancelled（避免漏掉已发生的取消）
        if (cancelled.load(std::memory_order_acquire)) {
            lock.store(false, std::memory_order_release);
            // 已取消：立即同步执行回调，不入链
            fn(ctx);
            delete node;
            return nullptr;
        }

        node->next = callbacks_head;
        callbacks_head = node;
        lock.store(false, std::memory_order_release);
        return node;
    }

    // FC-3: 标记回调为已注销。fire 会跳过 unregistered 的 node。
    // 不 delete node（fire 可能并发遍历），node 由 ~CancellationState 清理。
    // handle 为 nullptr（已同步执行）时是 no-op。
    void unregister_callback(void* handle) {
        if (!handle) return;
        auto* node = static_cast<CallbackNode*>(handle);
        node->unregistered.store(true, std::memory_order_release);
    }

    void fire_callbacks() {
        bool expected = false;
        while (!lock.compare_exchange_weak(
                expected, true, std::memory_order_acquire)) {
            expected = false;
        }

        // 不清空链表——node 留着让 ~CancellationState 清理。
        // 在锁内标记所有未 fired 且未 unregistered 的 node。
        CallbackNode* cur = callbacks_head;
        while (cur) {
            if (!cur->fired.load(std::memory_order_acquire) &&
                !cur->unregistered.load(std::memory_order_acquire)) {
                cur->fired.store(true, std::memory_order_release);
            }
            cur = cur->next;
        }
        lock.store(false, std::memory_order_release);

        // 锁外执行回调（避免死锁——fn 可能调 cancel 相关操作）
        // 此时不可能有新 register（cancelled==true 时 register 同步执行不入链）
        cur = callbacks_head;
        while (cur) {
            if (cur->fired.load(std::memory_order_acquire) &&
                !cur->unregistered.load(std::memory_order_acquire)) {
                cur->fn(cur->ctx);
            }
            cur = cur->next;
        }
    }

    ~CancellationState() {
        // 清理所有未 delete 的 node
        bool expected = false;
        while (!lock.compare_exchange_weak(
                expected, true, std::memory_order_acquire)) {
            expected = false;
        }
        CallbackNode* cur = callbacks_head;
        callbacks_head = nullptr;
        lock.store(false, std::memory_order_release);
        while (cur) {
            CallbackNode* next = cur->next;
            delete cur;
            cur = next;
        }
    }
};

} // namespace detail

// =====================================================================
// CancellationToken — 值类型，可拷贝共享同一状态
// =====================================================================
class CancellationTokenSource; // 前向声明

class CancellationToken {
public:
    CancellationToken() = default;  // 默认构造的 token 永不取消

    explicit CancellationToken(std::shared_ptr<detail::CancellationState> state)
        : state_(std::move(state)) {}

    bool is_cancelled() const noexcept {
        return state_ && state_->cancelled.load(std::memory_order_acquire);
    }

    // 显式协作式取消检查点
    void throw_if_cancellation_requested() const {
        if (is_cancelled()) {
            throw CancellationTokenException();
        }
    }

    // 注册一个回调，在取消发生时被调用一次（若已取消则立即同步调用）
    // 返回 handle 用于后续 unregister（nullptr 表示已同步执行，无需 unregister）
    void* register_callback(void (*fn)(void*), void* ctx) const {
        if (state_) {
            return state_->register_callback(fn, ctx);
        }
        return nullptr;
    }

    // FC-3: 注销回调。在 promise 析构前调用，避免 fire 访问已释放的 promise。
    void unregister_callback(void* handle) const {
        if (state_ && handle) {
            state_->unregister_callback(handle);
        }
    }

    bool operator==(const CancellationToken& other) const noexcept {
        return state_ == other.state_;
    }

    bool operator!=(const CancellationToken& other) const noexcept {
        return !(*this == other);
    }

private:
    std::shared_ptr<detail::CancellationState> state_;
};

// =====================================================================
// CancellationTokenSource — 取消的源头，可创建多个共享同一状态的 token
// =====================================================================
class CancellationTokenSource {
public:
    CancellationTokenSource()
        : state_(std::make_shared<detail::CancellationState>()) {}

    CancellationToken token() const noexcept {
        return CancellationToken(state_);
    }

    // 取消：原子地设置标志并触发所有已注册回调
    void cancel() const {
        if (!state_->cancelled.exchange(true, std::memory_order_acq_rel)) {
            state_->fire_callbacks();
        }
    }

    bool is_cancelled() const noexcept {
        return state_->cancelled.load(std::memory_order_acquire);
    }

private:
    std::shared_ptr<detail::CancellationState> state_;
};

// =====================================================================
// 取消点 Awaiter：把 CancellationToken 检查嵌入到 co_await 路径
//
// 用法：
//   co_await cancellation_point(token);
//
// 行为：
//   - 已取消 -> 立即抛 CancellationTokenException
//   - 未取消 -> 立即返回，不挂起
// =====================================================================
class CancellationTokenAwaiter {
public:
    explicit CancellationTokenAwaiter(CancellationToken token)
        : token_(std::move(token)) {}

    bool await_ready() const {
        // 已取消或无效 token 直接 ready（resume 时抛出）
        // 未取消也 ready（resume 直接返回）
        return true;
    }

    void await_suspend(std::coroutine_handle<>) const noexcept {
        // 永不挂起
    }

    void await_resume() const {
        token_.throw_if_cancellation_requested();
    }

private:
    CancellationToken token_;
};

inline CancellationTokenAwaiter cancellation_point(CancellationToken token) {
    return CancellationTokenAwaiter(std::move(token));
}

// =====================================================================
// TaskGroup（nursery 风格）— 简化版结构化并发
//
// 用法：
//   TaskGroup group;
//   group.spawn(task_a());
//   group.spawn(task_b());
//   co_await group.wait_all();  // 等待全部完成
//   // 或：
//   co_await group.cancel_all(); // 主动取消全部
//
// 当前是头文件友好实现，不依赖 channel，仅做基础组合。
// =====================================================================
class TaskGroup {
public:
    explicit TaskGroup(CancellationToken token = CancellationToken{})
        : token_(std::move(token)) {}

    ~TaskGroup() {
        // 析构时若仍未 join，主动取消所有未完成任务
        if (!joined_.load(std::memory_order_acquire)) {
            cancel_all();
        }
    }

    CancellationToken token() const noexcept { return token_; }

    // 注册一个 handle 进组（不取得所有权，调用者负责其生命周期）
    // 简化版：仅记录，等 join 时统一等待
    void track(std::coroutine_handle<> h) {
        if (!h) return;
        std::lock_guard<std::mutex> lock(mu_);
        handles_.push_back(h);
    }

    // 取消本组所有任务（基于 token 触发取消）
    void cancel_all() {
        cancelled_.store(true, std::memory_order_release);
        // 若 token 关联到 source，取消它
        // （这里只能取消 source 创建的 token；默认 token 无法取消）
        // 通过 token 的回调机制可以接入更细粒度控制，此处简化
    }

    bool is_cancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire) || token_.is_cancelled();
    }

private:
    CancellationToken token_;
    std::atomic<bool> joined_{false};
    std::atomic<bool> cancelled_{false};
    std::mutex mu_;
    std::vector<std::coroutine_handle<>> handles_;
};

} // namespace flowcoro
