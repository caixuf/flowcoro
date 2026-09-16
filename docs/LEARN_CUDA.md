# CUDA 协程与有界通道（给 flowtrain / flowserve 用）

FlowCoro 主体是通用 C++20 协程库。LLM 两仓只用到其中几件，按这条学最快。

完整 API、实时 `RtExecutor`、Python 绑定仍看仓库根 README 和 `docs/`。

## 1. 有界通道：满了必须失败

读 `include/flowcoro/bounded_channel.h`。

- MPMC、运行时容量（向上取 2 的幂）、`try_push` / `try_pop` **非阻塞**。
- 满或已 close：`try_push` 返回 false。签名是 `try_push(T value)`：失败时入参已被移进函数，调用方要用拷贝重试，不能 `std::move` 进循环。
- `size()` 是近似上界，可当「不超过 capacity」的证据，不能当精确计数器。

flowtrain 用容量 2 做 1F1B 反压。若最后一级把 M 条前向结果也塞进容量 2 的队列，GPipe 会死锁——那是使用问题，不是通道实现错。

## 2. yield：挂起后必须能被工作池叫醒

读 `include/flowcoro/yield.h`。

`YieldAwaiter::await_suspend` 必须 `schedule_coroutine_enhanced(h)`。  
若只 `CoroutineManager::schedule_resume`，而外层 `Task::get()` 在 CV 上死等、不再 `drive()` 全局 manager，协程会永久挂起（flowtrain 异步流水线踩过）。

`BatchYieldAwaiter` 与 `yield()` 必须同一条唤醒路径。

## 3. CUDA Stream 等待

读 `include/flowcoro/cuda.h`。

- `PinnedHostBuffer` / `DeviceBuffer`：RAII，`cuMemAllocHost` / `cuMemAlloc`。
- `async_copy_h2d/d2h`：`cuMemcpy*Async` + `CudaStreamAwaiter`。
- Awaiter：`cuLaunchHostFunc` 在回调里 `schedule_coroutine_enhanced`，**不要在 CUDA 回调线程里直接 resume 业务协程**。
- 无 `cuda.h` 时头文件里有 stub（malloc+memcpy）。stub 上 `cuModuleLoadData` 成功但 module 为空，flowtrain `require_gpu=true` 应失败，这是故意的。

PTX GEMM 内核在 **flowtrain** 的 `async_pipeline.hpp`，不在本库。本库只提供流、拷贝、等待。

## 4. 建议阅读顺序

`bounded_channel.h` → `yield.h` → `cuda.h`（Stream / Buffer / Awaiter）→ `scheduler_api.h` 里的 `schedule_coroutine_enhanced` → 再到 flowtrain `async_pipeline.hpp` 看怎么拼起来。
