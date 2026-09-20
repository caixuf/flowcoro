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
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#if __has_include(<cuda.h>)
#include <cuda.h>
#else
typedef struct CUstream_st* CUstream;
typedef struct CUevent_st* CUevent;
typedef struct CUctx_st* CUcontext;
typedef int CUdevice;
typedef unsigned long long CUdeviceptr;
typedef int CUresult;
#define CUDA_SUCCESS 0
#define CU_STREAM_NON_BLOCKING 1
#define CU_EVENT_DISABLE_TIMING 2
#define CUDA_CB
inline CUresult cuInit(unsigned int) { return CUDA_SUCCESS; }
inline CUresult cuDeviceGetCount(int* count) { *count = 1; return CUDA_SUCCESS; }
inline CUresult cuDeviceGet(CUdevice* dev, int) { *dev = 0; return CUDA_SUCCESS; }
inline CUresult cuDevicePrimaryCtxRetain(CUcontext* ctx, CUdevice) { *ctx = nullptr; return CUDA_SUCCESS; }
inline CUresult cuCtxSetCurrent(CUcontext) { return CUDA_SUCCESS; }
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
inline CUresult cuMemAlloc(CUdeviceptr* dptr, size_t bytes) { *dptr = reinterpret_cast<CUdeviceptr>(std::malloc(bytes)); return CUDA_SUCCESS; }
inline CUresult cuMemFree(CUdeviceptr dptr) { std::free(reinterpret_cast<void*>(dptr)); return CUDA_SUCCESS; }
inline CUresult cuMemAllocHost(void** pp, size_t bytes) { *pp = std::malloc(bytes); return CUDA_SUCCESS; }
inline CUresult cuMemFreeHost(void* p) { std::free(p); return CUDA_SUCCESS; }
inline CUresult cuMemcpyHtoDAsync(CUdeviceptr dst, const void* src, size_t bytes, CUstream) { std::memcpy(reinterpret_cast<void*>(dst), src, bytes); return CUDA_SUCCESS; }
inline CUresult cuMemcpyDtoHAsync(void* dst, CUdeviceptr src, size_t bytes, CUstream) { std::memcpy(dst, reinterpret_cast<const void*>(src), bytes); return CUDA_SUCCESS; }
typedef struct CUmod_st* CUmodule;
typedef struct CUfunc_st* CUfunction;
inline CUresult cuModuleLoadData(CUmodule* mod, const void*) { *mod = nullptr; return CUDA_SUCCESS; }
inline CUresult cuModuleUnload(CUmodule) { return CUDA_SUCCESS; }
inline CUresult cuModuleGetFunction(CUfunction* func, CUmodule, const char*) { *func = nullptr; return CUDA_SUCCESS; }
inline CUresult cuLaunchKernel(CUfunction, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, CUstream, void**, void**) { return CUDA_SUCCESS; }
#endif

#include "flowcoro/scheduler_api.h"

namespace flowcoro::cuda {

#define FLOWCORO_CUDA_CHECK(call) do { \
    CUresult _err = (call); \
    if (_err != CUDA_SUCCESS) { \
        throw std::runtime_error("FlowCoro CUDA error code " + std::to_string(_err) + \
                                 " at " + std::string(__FILE__) + ":" + std::to_string(__LINE__)); \
    } \
} while(0)

inline void ensure_cuda_initialized() {
    static bool driver_inited = []() {
        if (cuInit(0) != CUDA_SUCCESS) return false;
        return true;
    }();
    if (driver_inited) {
        thread_local bool thread_bound = []() {
            int count = 0;
            if (cuDeviceGetCount(&count) == CUDA_SUCCESS && count > 0) {
                CUdevice dev = 0;
                if (cuDeviceGet(&dev, 0) == CUDA_SUCCESS) {
                    CUcontext ctx = nullptr;
                    if (cuDevicePrimaryCtxRetain(&ctx, dev) == CUDA_SUCCESS && ctx) {
                        cuCtxSetCurrent(ctx);
                    }
                }
            }
            return true;
        }();
        (void)thread_bound;
    }
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

    void await_resume() const noexcept {
        ensure_cuda_initialized();
    }

private:
    static void CUDA_CB resume_coroutine(void* userData) {
        if (userData) {
            auto handle = std::coroutine_handle<>::from_address(userData);
            // 将唤醒事件派发至 flowcoro 工作线程池，脱离 CUDA 驱动通知线程
            schedule_coroutine_enhanced(handle);
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

/**
 * @brief RAII 封装的 CUDA 显存缓冲区 (Device Buffer)
 */
template <typename T>
class DeviceBuffer {
public:
    DeviceBuffer() = default;

    explicit DeviceBuffer(size_t count) : count_(count) {
        if (count_ > 0) {
            ensure_cuda_initialized();
            FLOWCORO_CUDA_CHECK(cuMemAlloc(&dptr_, count_ * sizeof(T)));
        }
    }

    ~DeviceBuffer() {
        if (dptr_) {
            cuMemFree(dptr_);
            dptr_ = 0;
        }
    }

    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;

    DeviceBuffer(DeviceBuffer&& other) noexcept : dptr_(other.dptr_), count_(other.count_) {
        other.dptr_ = 0;
        other.count_ = 0;
    }

    DeviceBuffer& operator=(DeviceBuffer&& other) noexcept {
        if (this != &other) {
            if (dptr_) cuMemFree(dptr_);
            dptr_ = other.dptr_;
            count_ = other.count_;
            other.dptr_ = 0;
            other.count_ = 0;
        }
        return *this;
    }

    CUdeviceptr get() const noexcept { return dptr_; }
    operator CUdeviceptr() const noexcept { return dptr_; }
    size_t size() const noexcept { return count_; }
    size_t bytes() const noexcept { return count_ * sizeof(T); }

private:
    CUdeviceptr dptr_{0};
    size_t count_{0};
};

/**
 * @brief RAII 封装的 CUDA 锁页内存 (Pinned Host Buffer)，支持 CPU 协程直接读写与 GPU 零拷贝 DMA
 */
template <typename T>
class PinnedHostBuffer {
public:
    PinnedHostBuffer() = default;

    explicit PinnedHostBuffer(size_t count) : count_(count) {
        if (count_ > 0) {
            ensure_cuda_initialized();
            void* ptr = nullptr;
            FLOWCORO_CUDA_CHECK(cuMemAllocHost(&ptr, count_ * sizeof(T)));
            data_ = static_cast<T*>(ptr);
        }
    }

    ~PinnedHostBuffer() {
        if (data_) {
            cuMemFreeHost(data_);
            data_ = nullptr;
        }
    }

    PinnedHostBuffer(const PinnedHostBuffer&) = delete;
    PinnedHostBuffer& operator=(const PinnedHostBuffer&) = delete;

    PinnedHostBuffer(PinnedHostBuffer&& other) noexcept : data_(other.data_), count_(other.count_) {
        other.data_ = nullptr;
        other.count_ = 0;
    }

    PinnedHostBuffer& operator=(PinnedHostBuffer&& other) noexcept {
        if (this != &other) {
            if (data_) cuMemFreeHost(data_);
            data_ = other.data_;
            count_ = other.count_;
            other.data_ = nullptr;
            other.count_ = 0;
        }
        return *this;
    }

    T* data() noexcept { return data_; }
    const T* data() const noexcept { return data_; }
    T& operator[](size_t idx) { return data_[idx]; }
    const T& operator[](size_t idx) const { return data_[idx]; }
    size_t size() const noexcept { return count_; }
    size_t bytes() const noexcept { return count_ * sizeof(T); }

private:
    T* data_{nullptr};
    size_t count_{0};
};

/**
 * @brief 异步 Host-to-Device 锁页内存 DMA 拷贝，返回 Awaiter 支持 co_await 零阻塞挂起
 */
template <typename T>
inline CudaStreamAwaiter async_copy_h2d(CUstream stream, CUdeviceptr dst_device, const T* src_host, size_t count) {
    FLOWCORO_CUDA_CHECK(cuMemcpyHtoDAsync(dst_device, src_host, count * sizeof(T), stream));
    return CudaStreamAwaiter(stream);
}

/**
 * @brief 异步 Device-to-Host 锁页内存 DMA 拷贝，返回 Awaiter 支持 co_await 零阻塞挂起
 */
template <typename T>
inline CudaStreamAwaiter async_copy_d2h(CUstream stream, T* dst_host, CUdeviceptr src_device, size_t count) {
    FLOWCORO_CUDA_CHECK(cuMemcpyDtoHAsync(dst_host, src_device, count * sizeof(T), stream));
    return CudaStreamAwaiter(stream);
}

} // namespace flowcoro::cuda
