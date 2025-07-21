#pragma once

#include "core.h"
#include <memory>
#include <stdexcept>

namespace flowcoro {

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
    
    // 移动构造
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
    
    // 禁止拷贝
    cancellation_registration(const cancellation_registration&) = delete;
    cancellation_registration& operator=(const cancellation_registration&) = delete;
    
    ~cancellation_registration() {
        unregister();
    }
    
    // 手动取消注册
    void unregister() noexcept {
        if (cleanup_) {
            try {
                cleanup_();
            } catch (...) {
                // 忽略清理异常
            }
            cleanup_ = nullptr;
        }
        state_.reset();
    }
    
    // 检查是否仍有效
    bool is_valid() const noexcept {
        return !state_.expired();
    }
};

// 取消令牌
class cancellation_token {
private:
    std::shared_ptr<cancellation_state> state_;
    
public:
    // 默认构造：永不取消的令牌
    cancellation_token() = default;
    
    // 从状态构造
    explicit cancellation_token(std::shared_ptr<cancellation_state> state)
        : state_(std::move(state)) {}
    
    // 拷贝构造（共享状态）
    cancellation_token(const cancellation_token&) = default;
    cancellation_token& operator=(const cancellation_token&) = default;
    
    // 移动构造
    cancellation_token(cancellation_token&&) noexcept = default;
    cancellation_token& operator=(cancellation_token&&) noexcept = default;
    
    // 检查是否已取消
    bool is_cancelled() const noexcept {
        return state_ && state_->is_cancelled();
    }
    
    // 如果已取消则抛出异常
    void throw_if_cancelled() const {
        if (is_cancelled()) {
            throw operation_cancelled_exception{};
        }
    }
    
    // 注册取消回调
    template<typename Callback>
    cancellation_registration register_callback(Callback&& cb) const {
        if (!state_) {
            return {}; // 返回空的注册句柄
        }
        
        auto cleanup_fn = [state_weak = std::weak_ptr<cancellation_state>(state_)]() {
            if (auto state = state_weak.lock()) {
                state->clear_callbacks();
            }
        };
        
        state_->register_callback(std::forward<Callback>(cb));
        return cancellation_registration{std::weak_ptr<cancellation_state>(state_), std::move(cleanup_fn)};
    }
    
    // 检查是否为有效令牌
    bool is_valid() const noexcept {
        return static_cast<bool>(state_);
    }
    
    // 创建一个永不取消的令牌
    static cancellation_token none() {
        return cancellation_token{};
    }
    
    // 创建一个已经取消的令牌
    static cancellation_token cancelled() {
        auto state = std::make_shared<cancellation_state>();
        state->request_cancellation();
        return cancellation_token{std::move(state)};
    }
    
    // 创建超时取消令牌
    static cancellation_token create_timeout(std::chrono::milliseconds timeout) {
        auto state = std::make_shared<cancellation_state>();
        auto token = cancellation_token{state};
        
        // 在后台线程中设置超时
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
    // 构造新的取消源
    cancellation_source() 
        : state_(std::make_shared<cancellation_state>()) {}
    
    // 移动构造
    cancellation_source(cancellation_source&&) noexcept = default;
    cancellation_source& operator=(cancellation_source&&) noexcept = default;
    
    // 禁止拷贝（避免意外的多重取消）
    cancellation_source(const cancellation_source&) = delete;
    cancellation_source& operator=(const cancellation_source&) = delete;
    
    // 获取对应的取消令牌
    cancellation_token get_token() const {
        return cancellation_token{state_};
    }
    
    // 请求取消
    void cancel() noexcept {
        if (state_) {
            state_->request_cancellation();
        }
    }
    
    // 检查是否已取消
    bool is_cancelled() const noexcept {
        return state_ && state_->is_cancelled();
    }
    
    // 检查是否有效
    bool is_valid() const noexcept {
        return static_cast<bool>(state_);
    }
};

// 组合取消令牌：当任意一个取消时触发
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
        
        // 为每个令牌注册回调
        for (const auto& token : tokens_) {
            if (token.is_valid()) {
                registrations_.emplace_back(
                    token.register_callback([state = combined_state_]() {
                        state->request_cancellation();
                    })
                );
                
                // 如果任何令牌已经取消，立即取消组合令牌
                if (token.is_cancelled()) {
                    combined_state_->request_cancellation();
                    break;
                }
            }
        }
    }
    
    // 获取组合后的令牌
    cancellation_token get_token() const {
        return cancellation_token{combined_state_};
    }
    
    // 检查是否已取消
    bool is_cancelled() const noexcept {
        return combined_state_ && combined_state_->is_cancelled();
    }
};

// 工厂函数：组合多个取消令牌
template<typename... Tokens>
auto combine_tokens(Tokens&&... tokens) {
    return combined_cancellation_token{std::forward<Tokens>(tokens)...};
}

} // namespace flowcoro
