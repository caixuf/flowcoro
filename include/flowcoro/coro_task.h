#pragma once
#include <coroutine>
#include <memory>
#include <string>
#include "thread_pool_wrapper.h"
#include "network_request.h"
#include "logger.h"

// HttpRequest
class HttpRequest;

namespace flowcoro {

struct CoroTask {
    struct promise_type {
        CoroTask get_return_object() {
            return CoroTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() {
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
            if (handle) handle.destroy();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
    ~CoroTask() { if (handle) handle.destroy(); }

    void resume() {
        if (handle && !handle.done()) {
            LOG_DEBUG("Resuming coroutine handle: %p", handle.address());
            // 
            GlobalThreadPool::get().enqueue_void([h = handle]() {
                if (h && !h.done()) {
                    LOG_TRACE("Executing coroutine in thread pool");
                    h.resume();
                    LOG_TRACE("Coroutine execution completed");
                }
            });
        }
    }
    bool done() const { return !handle || handle.done(); }

    // CoroTask
    bool await_ready() const {
        return done();
    }

    void await_suspend(std::coroutine_handle<> waiting_handle) {
        if (handle && !handle.done()) [[likely]] {
            // 
            GlobalThreadPool::get().enqueue_void([h = handle, waiting_handle]() {
                // 
                if (h) [[likely]] {
                    h.resume();
                }
                // 
                if (waiting_handle) [[likely]] {
                    waiting_handle.resume();
                }
            });
        } else {
            // 
            GlobalThreadPool::get().enqueue_void([waiting_handle]() {
                if (waiting_handle) [[likely]] {
                    waiting_handle.resume();
                }
            });
        }
    }

    void await_resume() const {
        // 
    }

    // Task
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
                // 
                request_ = std::make_unique<NetworkRequestT>();
                request_->request(url_, [h, this](const T& response) {
                    LOG_DEBUG("Network request completed, response size: %zu", response.size());
                    // 
                    response_ = response;
                    // 
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
