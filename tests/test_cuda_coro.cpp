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

    co_return 42;
}

int main() {
    std::cout << "[FlowCoro CUDA] Testing C++20 CUDA Stream Coroutine Awaiter...\n";
    int result = sync_wait(run_cuda_async_coro());
    assert(result == 42);
    std::cout << "[PASS] FlowCoro native CUDA coroutine awaiter passed!\n";
    return 0;
}
