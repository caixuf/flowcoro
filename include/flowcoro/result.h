#pragma once
#include <variant>
#include <type_traits>
#include <exception>
#include <functional>
#include <string>

namespace flowcoro {

// 前向声明
template<typename T, typename E = std::exception_ptr>
class Result;

// 成功值包装器
template<typename T>
struct Ok {
    T value;

    template<typename U>
    explicit Ok(U&& v) : value(std::forward<U>(v)) {}
};

// 错误值包装器
template<typename E>
struct Err {
    E error;

    template<typename U>
    explicit Err(U&& e) : error(std::forward<U>(e)) {}
};

// 便利函数
template<typename T>
Ok<std::decay_t<T>> ok(T&& value) {
    return Ok<std::decay_t<T>>(std::forward<T>(value));
}

template<typename E>
Err<std::decay_t<E>> err(E&& error) {
    return Err<std::decay_t<E>>(std::forward<E>(error));
}

// Result 类型 - Rust风格的错误处理
template<typename T, typename E>
class Result {
private:
    std::variant<T, E> value_;

public:
    using value_type = T;
    using error_type = E;

    // 构造函数
    template<typename U>
    Result(Ok<U>&& ok) : value_(std::move(ok.value)) {}

    template<typename U>
    Result(Err<U>&& error) : value_(std::move(error.error)) {}

    // 拷贝和移动构造
    Result(const Result&) = default;
    Result(Result&&) = default;
    Result& operator=(const Result&) = default;
    Result& operator=(Result&&) = default;

    // 状态查询
    bool is_ok() const noexcept {
        return std::holds_alternative<T>(value_);
    }

    bool is_err() const noexcept {
        return std::holds_alternative<E>(value_);
    }

    // 显式转换为bool
    explicit operator bool() const noexcept {
        return is_ok();
    }

    // 值访问 - 不安全版本
    T& unwrap() & {
        if (is_err()) {
            if constexpr (std::is_same_v<E, std::exception_ptr>) {
                std::rethrow_exception(std::get<E>(value_));
            } else {
                throw std::runtime_error("Result contains error");
            }
        }
        return std::get<T>(value_);
    }

    const T& unwrap() const & {
        if (is_err()) {
            if constexpr (std::is_same_v<E, std::exception_ptr>) {
                std::rethrow_exception(std::get<E>(value_));
            } else {
                throw std::runtime_error("Result contains error");
            }
        }
        return std::get<T>(value_);
    }

    T&& unwrap() && {
        if (is_err()) {
            if constexpr (std::is_same_v<E, std::exception_ptr>) {
                std::rethrow_exception(std::get<E>(value_));
            } else {
                throw std::runtime_error("Result contains error");
            }
        }
        return std::get<T>(std::move(value_));
    }

    // 带默认值的访问
    template<typename U>
    T unwrap_or(U&& default_value) const & {
        if (is_ok()) {
            return std::get<T>(value_);
        }
        return static_cast<T>(std::forward<U>(default_value));
    }

    template<typename U>
    T unwrap_or(U&& default_value) && {
        if (is_ok()) {
            return std::get<T>(std::move(value_));
        }
        return static_cast<T>(std::forward<U>(default_value));
    }

    // 错误访问
    const E& error() const & {
        if (is_ok()) {
            throw std::runtime_error("Result contains value, not error");
        }
        return std::get<E>(value_);
    }

    E&& error() && {
        if (is_ok()) {
            throw std::runtime_error("Result contains value, not error");
        }
        return std::get<E>(std::move(value_));
    }

    // 链式操作 - map
    template<typename F>
    auto map(F&& f) & -> Result<std::invoke_result_t<F, T&>, E> {
        using NewT = std::invoke_result_t<F, T&>;
        if (is_ok()) {
            return ok(std::invoke(std::forward<F>(f), std::get<T>(value_)));
        } else {
            return err(std::get<E>(value_));
        }
    }

    template<typename F>
    auto map(F&& f) const & -> Result<std::invoke_result_t<F, const T&>, E> {
        using NewT = std::invoke_result_t<F, const T&>;
        if (is_ok()) {
            return ok(std::invoke(std::forward<F>(f), std::get<T>(value_)));
        } else {
            return err(std::get<E>(value_));
        }
    }

    template<typename F>
    auto map(F&& f) && -> Result<std::invoke_result_t<F, T&&>, E> {
        using NewT = std::invoke_result_t<F, T&&>;
        if (is_ok()) {
            return ok(std::invoke(std::forward<F>(f), std::get<T>(std::move(value_))));
        } else {
            return err(std::get<E>(std::move(value_)));
        }
    }

    // 链式操作 - and_then (flatMap)
    template<typename F>
    auto and_then(F&& f) & -> std::invoke_result_t<F, T&> {
        static_assert(std::is_same_v<typename std::invoke_result_t<F, T&>::error_type, E>,
                      "Error types must match");
        if (is_ok()) {
            return std::invoke(std::forward<F>(f), std::get<T>(value_));
        } else {
            return err(std::get<E>(value_));
        }
    }

    template<typename F>
    auto and_then(F&& f) const & -> std::invoke_result_t<F, const T&> {
        static_assert(std::is_same_v<typename std::invoke_result_t<F, const T&>::error_type, E>,
                      "Error types must match");
        if (is_ok()) {
            return std::invoke(std::forward<F>(f), std::get<T>(value_));
        } else {
            return err(std::get<E>(value_));
        }
    }

    template<typename F>
    auto and_then(F&& f) && -> std::invoke_result_t<F, T&&> {
        static_assert(std::is_same_v<typename std::invoke_result_t<F, T&&>::error_type, E>,
                      "Error types must match");
        if (is_ok()) {
            return std::invoke(std::forward<F>(f), std::get<T>(std::move(value_)));
        } else {
            return err(std::get<E>(std::move(value_)));
        }
    }

    // 错误映射
    template<typename F>
    auto map_err(F&& f) -> Result<T, std::invoke_result_t<F, E>> {
        using NewE = std::invoke_result_t<F, E>;
        if (is_err()) {
            return err(std::invoke(std::forward<F>(f), std::get<E>(value_)));
        } else {
            return ok(std::get<T>(value_));
        }
    }
};

// void的特化版本
template<typename E>
class Result<void, E> {
private:
    std::optional<E> error_;

public:
    using value_type = void;
    using error_type = E;

    // 构造函数
    Result() : error_(std::nullopt) {} // 成功

    template<typename U>
    Result(Err<U>&& error) : error_(std::move(error.error)) {}

    // 状态查询
    bool is_ok() const noexcept {
        return !error_.has_value();
    }

    bool is_err() const noexcept {
        return error_.has_value();
    }

    explicit operator bool() const noexcept {
        return is_ok();
    }

    // void版本的unwrap
    void unwrap() const {
        if (is_err()) {
            if constexpr (std::is_same_v<E, std::exception_ptr>) {
                std::rethrow_exception(*error_);
            } else {
                throw std::runtime_error("Result contains error");
            }
        }
    }

    // 错误访问
    const E& error() const & {
        if (is_ok()) {
            throw std::runtime_error("Result contains success, not error");
        }
        return *error_;
    }

    E&& error() && {
        if (is_ok()) {
            throw std::runtime_error("Result contains success, not error");
        }
        return std::move(*error_);
    }
};

} // namespace flowcoro
