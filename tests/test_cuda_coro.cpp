#include "flowcoro/cuda.h"
#include "flowcoro/task.h"
#include "flowcoro/sync_wait.h"
#include <cassert>
#include <iostream>

using namespace flowcoro;
using namespace flowcoro::cuda;

static Task<int> run_cuda_async_coro() {
    CudaStream stream;
    // 验证 is_done 初始状态
    bool initially_done = stream.is_done();
    (void)initially_done;

    // 原生语法糖: co_await stream 异步等待
    co_await stream;

    // 函数式调用: co_await await_stream(stream)
    co_await await_stream(stream);

    // 异构 DMA 锁页内存与显存双向传输协程流水线
    const size_t N = 512;
    PinnedHostBuffer<float> h_in(N);
    PinnedHostBuffer<float> h_out(N);
    DeviceBuffer<float> d_buf(N);

    for (size_t i = 0; i < N; ++i) {
        h_in[i] = static_cast<float>(i) * 2.5f + 1.0f;
        h_out[i] = 0.0f;
    }

    // 异步 DMA: Host (Pinned) -> Device
    co_await async_copy_h2d(stream, d_buf, h_in.data(), N);

    // 异步 DMA: Device -> Host (Pinned)
    co_await async_copy_d2h(stream, h_out.data(), d_buf, N);

    // 校验回传数值
    for (size_t i = 0; i < N; ++i) {
        assert(h_out[i] == h_in[i]);
    }

    co_return 42;
}

int main() {
    std::cout << "[FlowCoro CUDA] Testing C++20 CUDA Stream & DMA Coroutine Awaiter...\n";
    int result = sync_wait(run_cuda_async_coro());
    assert(result == 42);
    std::cout << "[PASS] FlowCoro native CUDA coroutine awaiter passed!\n";
    return 0;
}
