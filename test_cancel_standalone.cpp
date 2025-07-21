#include <coroutine>
#include <memory>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>
#include <thread>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace flowcoro {

// 简化的日志宏
#define LOG_INFO(msg) std::cout << "[INFO] " << msg << std::endl

// 取消状态
class cancellation_state {
private:
    std::atomic<bool> cancelled_{false};
    std::mutex callbacks_mutex_;
    std::vector<std::function<void()>> callbacks_;
    
public:
    void request_cancellation() noexcept {
        bool expected = false;
        if (cancelled_.compare_exchange_strong(expected, true)) {
            std::lock_guard<std::mutex> lock(callbacks_mutex_);
            for (auto& callback : callbacks_) {
                try { callback(); } catch (...) {}
            }
        }
    }
    
    bool is_cancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }
    
    template<typename Callback>
    void register_callback(Callback&& cb) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callbacks_.emplace_back(std::forward<Callback>(cb));
        
        if (is_cancelled()) {
            try { cb(); } catch (...) {}
        }
    }
    
    void clear_callbacks() noexcept {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callbacks_.clear();
    }
};

// 操作取消异常
class operation_cancelled_exception : public std::exception {
private:
    std::string message_;
    
public:
    explicit operation_cancelled_exception(const std::string& msg = "Operation was cancelled")
        : message_(msg) {}
    
    const char* what() const noexcept override {
        return message_.c_str();
    }
};

// 取消注册句柄
class cancellation_registration {
private:
    std::weak_ptr<cancellation_state> state_;
    std::function<void()> cleanup_;
    
public:
    cancellation_registration() = default;
    
    cancellation_registration(std::weak_ptr<cancellation_state> state, std::function<void()> cleanup)
        : state_(std::move(state)), cleanup_(std::move(cleanup)) {}
    
    cancellation_registration(cancellation_registration&& other) noexcept
        : state_(std::move(other.state_)), cleanup_(std::move(other.cleanup_)) {}
    
    cancellation_registration& operator=(cancellation_registration&& other) noexcept {
        if (this != &other) {
            unregister();
            state_ = std::move(other.state_);
            cleanup_ = std::move(other.cleanup_);
        }
        return *this;
    }
    
    cancellation_registration(const cancellation_registration&) = delete;
    cancellation_registration& operator=(const cancellation_registration&) = delete;
    
    ~cancellation_registration() {
        unregister();
    }
    
    void unregister() noexcept {
        if (cleanup_) {
            try {
                cleanup_();
            } catch (...) {}
            cleanup_ = nullptr;
        }
        state_.reset();
    }
    
    bool is_valid() const noexcept {
        return !state_.expired();
    }
};

// 取消令牌
class cancellation_token {
private:
    std::shared_ptr<cancellation_state> state_;
    
public:
    cancellation_token() = default;
    
    explicit cancellation_token(std::shared_ptr<cancellation_state> state)
        : state_(std::move(state)) {}
    
    cancellation_token(const cancellation_token&) = default;
    cancellation_token& operator=(const cancellation_token&) = default;
    
    cancellation_token(cancellation_token&&) noexcept = default;
    cancellation_token& operator=(cancellation_token&&) noexcept = default;
    
    bool is_cancelled() const noexcept {
        return state_ && state_->is_cancelled();
    }
    
    void throw_if_cancelled() const {
        if (is_cancelled()) {
            throw operation_cancelled_exception{};
        }
    }
    
    template<typename Callback>
    cancellation_registration register_callback(Callback&& cb) const {
        if (!state_) {
            return {};
        }
        
        auto cleanup_fn = [state_weak = std::weak_ptr<cancellation_state>(state_)]() {
            if (auto state = state_weak.lock()) {
                state->clear_callbacks();
            }
        };
        
        state_->register_callback(std::forward<Callback>(cb));
        return cancellation_registration{std::weak_ptr<cancellation_state>(state_), std::move(cleanup_fn)};
    }
    
    bool is_valid() const noexcept {
        return static_cast<bool>(state_);
    }
    
    static cancellation_token none() {
        return cancellation_token{};
    }
    
    static cancellation_token cancelled() {
        auto state = std::make_shared<cancellation_state>();
        state->request_cancellation();
        return cancellation_token{std::move(state)};
    }
    
    static cancellation_token create_timeout(std::chrono::milliseconds timeout) {
        auto state = std::make_shared<cancellation_state>();
        auto token = cancellation_token{state};
        
        std::thread([state, timeout]() {
            std::this_thread::sleep_for(timeout);
            state->request_cancellation();
        }).detach();
        
        return token;
    }
};

// 取消源
class cancellation_source {
private:
    std::shared_ptr<cancellation_state> state_;
    
public:
    cancellation_source() 
        : state_(std::make_shared<cancellation_state>()) {}
    
    cancellation_source(cancellation_source&&) noexcept = default;
    cancellation_source& operator=(cancellation_source&&) noexcept = default;
    
    cancellation_source(const cancellation_source&) = delete;
    cancellation_source& operator=(const cancellation_source&) = delete;
    
    cancellation_token get_token() const {
        return cancellation_token{state_};
    }
    
    void cancel() noexcept {
        if (state_) {
            state_->request_cancellation();
        }
    }
    
    bool is_cancelled() const noexcept {
        return state_ && state_->is_cancelled();
    }
    
    bool is_valid() const noexcept {
        return static_cast<bool>(state_);
    }
};

// 组合取消令牌
class combined_cancellation_token {
private:
    std::vector<cancellation_token> tokens_;
    std::shared_ptr<cancellation_state> combined_state_;
    std::vector<cancellation_registration> registrations_;
    
public:
    template<typename... Tokens>
    explicit combined_cancellation_token(Tokens&&... tokens)
        : tokens_{std::forward<Tokens>(tokens)...}
        , combined_state_(std::make_shared<cancellation_state>()) {
        
        for (const auto& token : tokens_) {
            if (token.is_valid()) {
                registrations_.emplace_back(
                    token.register_callback([state = combined_state_]() {
                        state->request_cancellation();
                    })
                );
                
                if (token.is_cancelled()) {
                    combined_state_->request_cancellation();
                    break;
                }
            }
        }
    }
    
    cancellation_token get_token() const {
        return cancellation_token{combined_state_};
    }
    
    bool is_cancelled() const noexcept {
        return combined_state_ && combined_state_->is_cancelled();
    }
};

template<typename... Tokens>
auto combine_tokens(Tokens&&... tokens) {
    return combined_cancellation_token{std::forward<Tokens>(tokens)...};
}

} // namespace flowcoro

using namespace flowcoro;

int main() {
    std::cout << "\n=== FlowCoro 取消功能独立测试 ===\n" << std::endl;
    
    try {
        // 测试1：基础取消功能
        std::cout << "1. 测试基础取消功能..." << std::endl;
        
        cancellation_source source;
        auto token = source.get_token();
        
        // 立即取消
        source.cancel();
        
        try {
            token.throw_if_cancelled();
            std::cout << "   ❌ 取消检测失败" << std::endl;
        } catch (const operation_cancelled_exception& e) {
            std::cout << "   ✅ 取消检测成功: " << e.what() << std::endl;
        }
        
        // 测试2：超时令牌
        std::cout << "\n2. 测试超时令牌..." << std::endl;
        
        auto timeout_token = cancellation_token::create_timeout(std::chrono::milliseconds(100));
        
        // 等待超时
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        
        if (timeout_token.is_cancelled()) {
            std::cout << "   ✅ 超时令牌测试成功" << std::endl;
        } else {
            std::cout << "   ❌ 超时令牌测试失败" << std::endl;
        }
        
        // 测试3：组合令牌
        std::cout << "\n3. 测试组合令牌..." << std::endl;
        
        cancellation_source source1, source2;
        auto combined = combine_tokens(source1.get_token(), source2.get_token());
        
        // 取消其中一个
        source1.cancel();
        
        if (combined.is_cancelled()) {
            std::cout << "   ✅ 组合令牌测试成功" << std::endl;
        } else {
            std::cout << "   ❌ 组合令牌测试失败" << std::endl;
        }
        
        // 测试4：回调注册
        std::cout << "\n4. 测试回调注册..." << std::endl;
        
        cancellation_source callback_source;
        auto callback_token = callback_source.get_token();
        
        bool callback_called = false;
        auto registration = callback_token.register_callback([&callback_called]() {
            callback_called = true;
            LOG_INFO("取消回调被调用");
        });
        
        // 触发取消
        callback_source.cancel();
        
        // 短暂等待回调执行
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        if (callback_called) {
            std::cout << "   ✅ 回调注册测试成功" << std::endl;
        } else {
            std::cout << "   ❌ 回调注册测试失败" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 测试失败: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n🎉 取消功能测试完成!" << std::endl;
    return 0;
}
