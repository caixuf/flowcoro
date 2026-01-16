#pragma once
#include "result.h"
#include <coroutine>
#include <string>
#include <exception>
#include <type_traits>
#include <functional>

namespace flowcoro {

// 
enum class FlowCoroError {
    NetworkTimeout,
    DatabaseConnectionFailed,
    CoroutineDestroyed,
    ResourceExhausted,
    InvalidOperation,
    TaskCancelled,
    UnknownError
};

// 
struct ErrorInfo {
    FlowCoroError code;
    std::string message;
    std::string file;
    int line;

    ErrorInfo(FlowCoroError c, std::string msg, std::string f = "", int l = 0)
        : code(c), message(std::move(msg)), file(std::move(f)), line(l) {}

    std::string to_string() const {
        std::string result = "FlowCoroError::" + error_code_to_string(code) + ": " + message;
        if (!file.empty()) {
            result += " at " + file + ":" + std::to_string(line);
        }
        return result;
    }

private:
    std::string error_code_to_string(FlowCoroError code) const {
        switch (code) {
            case FlowCoroError::NetworkTimeout: return "NetworkTimeout";
            case FlowCoroError::DatabaseConnectionFailed: return "DatabaseConnectionFailed";
            case FlowCoroError::CoroutineDestroyed: return "CoroutineDestroyed";
            case FlowCoroError::ResourceExhausted: return "ResourceExhausted";
            case FlowCoroError::InvalidOperation: return "InvalidOperation";
            case FlowCoroError::TaskCancelled: return "TaskCancelled";
            case FlowCoroError::UnknownError: return "UnknownError";
            default: return "Unknown";
        }
    }
};

// void ResultOk
template<>
struct Ok<void> {
    Ok() = default;
};

// void
inline Ok<void> ok() {
    return Ok<void>{};
}

// 
#define CO_TRY(expr) \
    ({ \
        auto __result = (expr); \
        if (__result.is_err()) { \
            co_return err(std::move(__result).error()); \
        } \
        std::move(__result).unwrap(); \
    })

//  - void
#define CO_TRY_VOID(expr) \
    do { \
        auto __result = (expr); \
        if (__result.is_err()) { \
            co_return err(std::move(__result).error()); \
        } \
        __result.unwrap(); \
    } while(0)

// 
#define FLOWCORO_ERROR(code, msg) \
    flowcoro::err(flowcoro::ErrorInfo(code, msg, __FILE__, __LINE__))

// Try-catch Result
template<typename F>
auto try_catch_to_result(F&& func) noexcept -> Result<std::invoke_result_t<F>, ErrorInfo> {
    try {
        if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
            func();
            return ok();
        } else {
            return ::flowcoro::ok(std::invoke(std::forward<F>(func)));
        }
    } catch (const std::exception& e) {
        return err(ErrorInfo(FlowCoroError::UnknownError, e.what()));
    } catch (...) {
        return err(ErrorInfo(FlowCoroError::UnknownError, "Unknown exception"));
    }
}

} // namespace flowcoro
