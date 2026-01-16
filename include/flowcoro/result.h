#pragma once
#include <variant>
#include <type_traits>
#include <exception>
#include <functional>
#include <string>

namespace flowcoro {

// 
template<typename T, typename E = std::exception_ptr>
class Result;

// 
template<typename T>
struct Ok {
    T value;

    template<typename U>
    explicit Ok(U&& v) : value(std::forward<U>(v)) {}
};

// 
template<typename E>
struct Err {
    E error;

    template<typename U>
    explicit Err(U&& e) : error(std::forward<U>(e)) {}
};

// 
template<typename T>
Ok<std::decay_t<T>> ok(T&& value) {
    return Ok<std::decay_t<T>>(std::forward<T>(value));
}

template<typename E>
Err<std::decay_t<E>> err(E&& error) {
    return Err<std::decay_t<E>>(std::forward<E>(error));
}

// Result  - Rust
template<typename T, typename E>
class Result {
private:
    std::variant<T, E> value_;

public:
    using value_type = T;
    using error_type = E;

    // 
    template<typename U>
    Result(Ok<U>&& ok) : value_(std::move(ok.value)) {}

    template<typename U>
    Result(Err<U>&& error) : value_(std::move(error.error)) {}

    // 
    Result(const Result&) = default;
    Result(Result&&) = default;
    Result& operator=(const Result&) = default;
    Result& operator=(Result&&) = default;

    // 
    bool is_ok() const noexcept {
        return std::holds_alternative<T>(value_);
    }

    bool is_err() const noexcept {
        return std::holds_alternative<E>(value_);
    }

    // bool
    explicit operator bool() const noexcept {
        return is_ok();
    }

    //  - 
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

    // 
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

    // 
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

    //  - map
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

    //  - and_then (flatMap)
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

    // 
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

// void
template<typename E>
class Result<void, E> {
private:
    std::optional<E> error_;

public:
    using value_type = void;
    using error_type = E;

    // 
    Result() : error_(std::nullopt) {} // 

    template<typename U>
    Result(Err<U>&& error) : error_(std::move(error.error)) {}

    // 
    bool is_ok() const noexcept {
        return !error_.has_value();
    }

    bool is_err() const noexcept {
        return error_.has_value();
    }

    explicit operator bool() const noexcept {
        return is_ok();
    }

    // voidunwrap
    void unwrap() const {
        if (is_err()) {
            if constexpr (std::is_same_v<E, std::exception_ptr>) {
                std::rethrow_exception(*error_);
            } else {
                throw std::runtime_error("Result contains error");
            }
        }
    }

    // 
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
