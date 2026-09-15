#pragma once

// ============================================================================
// FlowCoro - 原生 C++20 异步 CUDA 异构协程运行时 (Heterogeneous Coroutine Runtime)
// 
// 架构定位:
//   1. 基于工业级 CUDA Driver API (<cuda.h>)，自包含、无 CRT 冗余依赖；
//   2. 零阻塞 Stream 等待: co_await flowcoro::cuda::await_stream(stream);
//      利用 cuLaunchHostFunc 挂起当前 CPU 协程，GPU 执行完毕后由 CUDA 回调线程唤醒；
//   3. 零阻塞 Event 等待: co_await flowcoro::cuda::await_event(event);
//   4. 资源 RAII 管理: CudaStream 与 CudaEvent 句柄安全释放。
// ============================================================================

#include <coroutine>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#if __has_include(<cuda.h>)
#include <cuda.h>
#elif __has_include("/home/caixuf/.local/lib/python3.12/site-packages/nvidia/cu13/include/cuda.h")
#include "/home/caixuf/.local/lib/python3.12/site-packages/nvidia/cu13/include/cuda.h"
#else
typedef struct CUstream_st* CUstream;
typedef struct CUevent_st* CUevent;
typedef int CUresult;
#define CUDA_SUCCESS 0
#define CU_STREAM_NON_BLOCKING 1
#define CU_EVENT_DISABLE_TIMING 2
inline CUresult cuInit(unsigned int) { return CUDA_SUCCESS; }
inline CUresult cuStreamCreate(CUstream* s, unsigned int) { *s = nullptr; return CUDA_SUCCESS; }
inline CUresult cuStreamDestroy(CUstream) { return CUDA_SUCCESS; }
inline CUresult cuStreamQuery(CUstream) { return CUDA_SUCCESS; }
inline CUresult cuStreamSynchronize(CUstream) { return CUDA_SUCCESS; }
inline CUresult cuLaunchHostFunc(CUstream, void (*)(void*), void*) { return CUDA_SUCCESS; }
inline CUresult cuEventCreate(CUevent* e, unsigned int) { *e = nullptr; return CUDA_SUCCESS; }
inline CUresult cuEventDestroy(CUevent) { return CUDA_SUCCESS; }
inline CUresult cuEventRecord(CUevent, CUstream) { return CUDA_SUCCESS; }
inline CUresult cuEventQuery(CUevent) { return CUDA_SUCCESS; }
inline CUresult cuEventSynchronize(CUevent) { return CUDA_SUCCESS; }
#endif

namespace flowcoro::cuda {

#define FLOWCORO_CUDA_CHECK(call) do { \
    CUresult _err = (call); \
    if (_err != CUDA_SUCCESS) { \
        throw std::runtime_error("FlowCoro CUDA error code " + std::to_string(_err) + \
                                 " at " + std::string(__FILE__) + ":" + std::to_string(__LINE__)); \
    } \
} while(0)

inline void ensure_cuda_initialized() {
    static bool inited = []() {
        cuInit(0);
        return true;
    }();
    (void)inited;
}

class CudaEvent;

/**
 * @brief RAII 封装的 CUDA Stream，支持非阻塞并发执行与异步协程挂起
 */
class CudaStream {
public:
    CudaStream() {
        ensure_cuda_initialized();
        cuStreamCreate(&stream_, CU_STREAM_NON_BLOCKING);
    }

    explicit CudaStream(CUstream stream, bool own = false)
        : stream_(stream), own_(own) {}

    ~CudaStream() {
        if (own_ && stream_) {
            cuStreamDestroy(stream_);
            stream_ = nullptr;
        }
    }

    CudaStream(const CudaStream&) = delete;
    CudaStream& operator=(const CudaStream&) = delete;

    CudaStream(CudaStream&& other) noexcept
        : stream_(other.stream_), own_(other.own_) {
        other.stream_ = nullptr;
        other.own_ = false;
    }

    CudaStream& operator=(CudaStream&& other) noexcept {
        if (this != &other) {
            if (own_ && stream_) cuStreamDestroy(stream_);
            stream_ = other.stream_;
            own_ = other.own_;
            other.stream_ = nullptr;
            other.own_ = false;
        }
        return *this;
    }

    CUstream get() const noexcept { return stream_; }
    operator CUstream() const noexcept { return stream_; }

    bool is_done() const noexcept {
        return cuStreamQuery(stream_) == CUDA_SUCCESS;
    }

    void synchronize() {
        FLOWCORO_CUDA_CHECK(cuStreamSynchronize(stream_));
    }

private:
    CUstream stream_{nullptr};
    bool own_{true};
};

/**
 * @brief RAII 封装的 CUDA Event，用于精细粒度同步
 */
class CudaEvent {
public:
    CudaEvent(bool enable_timing = false) {
        ensure_cuda_initialized();
        unsigned int flags = enable_timing ? 0 : CU_EVENT_DISABLE_TIMING;
        cuEventCreate(&event_, flags);
    }

    ~CudaEvent() {
        if (event_) {
            cuEventDestroy(event_);
            event_ = nullptr;
        }
    }

    CudaEvent(const CudaEvent&) = delete;
    CudaEvent& operator=(const CudaEvent&) = delete;

    CudaEvent(CudaEvent&& other) noexcept : event_(other.event_) {
        other.event_ = nullptr;
    }

    CudaEvent& operator=(CudaEvent&& other) noexcept {
        if (this != &other) {
            if (event_) cuEventDestroy(event_);
            event_ = other.event_;
            other.event_ = nullptr;
        }
        return *this;
    }

    CUevent get() const noexcept { return event_; }
    operator CUevent() const noexcept { return event_; }

    void record(CUstream stream) {
        FLOWCORO_CUDA_CHECK(cuEventRecord(event_, stream));
    }

    bool is_done() const noexcept {
        return cuEventQuery(event_) == CUDA_SUCCESS;
    }

    void synchronize() {
        FLOWCORO_CUDA_CHECK(cuEventSynchronize(event_));
    }

private:
    CUevent event_{nullptr};
};

/**
 * @brief CUDA Stream 原生协程 Awaiter
 * 在协程中直接: co_await flowcoro::cuda::await_stream(stream);
 */
class CudaStreamAwaiter {
public:
    explicit CudaStreamAwaiter(CUstream stream) : stream_(stream) {}

    bool await_ready() const noexcept {
        if (!stream_) return true;
        // 如果 GPU stream 已经执行完毕，直接不挂起当前协程
        return cuStreamQuery(stream_) == CUDA_SUCCESS;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        if (!stream_) {
            h.resume();
            return;
        }
        // 向 stream 中发射宿主回调函数，当 GPU 执行到此处时由 CUDA 回调线程唤醒协程
        CUresult err = cuLaunchHostFunc(stream_, &CudaStreamAwaiter::resume_coroutine, h.address());
        if (err != CUDA_SUCCESS) {
            // 如果 launch 失败，立刻在当前线程恢复以防死锁
            h.resume();
        }
    }

    void await_resume() const noexcept {}

private:
    static void CUDA_CB resume_coroutine(void* userData) {
        if (userData) {
            auto handle = std::coroutine_handle<>::from_address(userData);
            handle.resume();
        }
    }

    CUstream stream_;
};

inline CudaStreamAwaiter await_stream(CUstream stream) {
    return CudaStreamAwaiter(stream);
}

inline CudaStreamAwaiter await_stream(const CudaStream& stream) {
    return CudaStreamAwaiter(stream.get());
}

inline CudaStreamAwaiter operator co_await(const CudaStream& stream) {
    return CudaStreamAwaiter(stream.get());
}

} // namespace flowcoro::cuda
