#pragma once
#include <coroutine>
#include <memory>
#include <string>
#include "thread_pool_wrapper.h"
#include "network_request.h"
#include "logger.h"

// 前向声明HttpRequest类
class HttpRequest;

namespace flowcoro {

struct CoroTask {
    struct promise_type {
        // Bug 修复3: 存储异常，以便在 await_resume() 中重新抛出
        std::exception_ptr exception_;
        // 用于续体链式唤醒的 continuation（修复 Bug 1 & 2）
        std::coroutine_handle<> continuation;

        CoroTask get_return_object() {
            return CoroTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }

        // Bug 修复2: final_suspend 通过续体恢复等待者，避免在 await_suspend 里手动双重 resume
        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                auto cont = h.promise().continuation;
                return cont ? cont : std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };
        FinalAwaiter final_suspend() noexcept { return {}; }

        void return_void() noexcept {}

        // Bug 修复3: 捕获异常而非静默丢弃
        void unhandled_exception() noexcept {
            exception_ = std::current_exception();
            LOG_ERROR("Unhandled exception in CoroTask");
        }
    };
    std::coroutine_handle<promise_type> handle;
    CoroTask(std::coroutine_handle<promise_type> h) : handle(h) {}
    CoroTask(const CoroTask&) = delete;
    CoroTask& operator=(const CoroTask&) = delete;
    CoroTask(CoroTask&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
    CoroTask& operator=(CoroTask&& other) noexcept {
        if (this != &other) {
            // 延迟销毁：避免在协程帧仍可能被其他线程引用时直接 destroy。
            // schedule_destroy 入队后由 CoroutineManager::process_pending_tasks
            // 在安全线程上下文统一回收（与 Task<T> 的销毁路径一致）。
            if (handle) {
                CoroutineManager::get_instance().schedule_destroy(handle);
            }
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
    ~CoroTask() {
        // 同上：延迟销毁。原 handle.destroy() 在协程池模式下可能销毁
        // 仍被引用的帧（跨线程 done()/destroy() 是 GCC 已知竞态）。
        if (handle) {
            CoroutineManager::get_instance().schedule_destroy(handle);
        }
    }

    void resume() {
        // Bug 修复1: 使用 CoroutineManager::schedule_resume，它内部通过无锁队列保证
        // 每个 handle 只被调度一次，避免多线程下 check-then-enqueue 竞态导致双重 resume。
        if (handle && !handle.done()) {
            LOG_DEBUG("Resuming coroutine handle: %p", handle.address());
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(handle);
        }
    }
    bool done() const { return !handle || handle.done(); }

    // 使CoroTask可等待
    bool await_ready() const {
        return done();
    }

    void await_suspend(std::coroutine_handle<> waiting_handle) {
        // Bug 修复2: 使用续体模式（continuation）替代手动在 lambda 里双重 resume。
        // 原实现在线程池 lambda 里先 h.resume() 再 waiting_handle.resume()，
        // 若 h 内部也触发了 waiting_handle 的恢复，则导致双重 resume -> UB/崩溃。
        // 正确做法：把 waiting_handle 存入 h 的 promise.continuation，
        // 由 final_suspend 在 h 完成时通过对称转移（symmetric transfer）恢复它，
        // 保证 waiting_handle 只被恢复一次。
        if (handle && !handle.done()) [[likely]] {
            handle.promise().continuation = waiting_handle;
            // 调度 h 运行（而非直接 resume，保持线程安全）
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(handle);
        } else {
            // h 已完成或无效，直接恢复等待者
            auto& manager = CoroutineManager::get_instance();
            manager.schedule_resume(waiting_handle);
        }
    }

    void await_resume() const {
        // Bug 修复3: 重新抛出协程内捕获的异常
        if (handle && handle.promise().exception_) {
            std::rethrow_exception(handle.promise().exception_);
        }
    }

    // 执行网络请求（返回可等待的Task）
    template<typename NetworkRequestT = HttpRequest, typename T = std::string>
    static auto execute_network_request(const std::string& url) {
        class RequestTask {
        public:
            RequestTask(std::string url) : url_(std::move(url)) {}

            bool await_ready() const noexcept {
                return false;
            }

            void await_suspend(std::coroutine_handle<> h) {
                LOG_INFO("Starting network request to: %s", url_.c_str());
                // 创建网络请求实例并发起请求
                request_ = std::make_unique<NetworkRequestT>();
                request_->request(url_, [h, this](const T& response) {
                    LOG_DEBUG("Network request completed, response size: %zu", response.size());
                    // 保存响应结果
                    response_ = response;
                    // 使用无锁线程池继续执行协程
                    GlobalThreadPool::get().enqueue_void([h]() {
                        if (h && !h.done()) {
                            LOG_TRACE("Resuming coroutine after network response");
                            h.resume();
                        }
                    });
                });
            }

            const T& await_resume() const {
                return response_;
            }

        private:
            std::string url_;
            T response_;
            std::unique_ptr<NetworkRequestT> request_;
        };

        return RequestTask(url);
    }
};

} // namespace flowcoro
