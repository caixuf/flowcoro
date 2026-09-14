# flowcoro_py — Python 绑定

`flowcoro_py` 把 flowcoro 的**无锁线程池**和有**界 MPMC 无锁通道**暴露给 Python。
默认构建不含它，需要显式打开：

```bash
pip install pybind11            # 或让 CMake 自动 FetchContent 拉取
cmake -B build-py -DCMAKE_BUILD_TYPE=Release -DFLOWCORO_BUILD_PYTHON=ON
cmake --build build-py --parallel $(nproc) --target flowcoro_py
export PYTHONPATH=$PWD/build-py/python
python3 -c "import flowcoro_py as fc; print(fc.__version__)"
```

## 先明确两件事（不然会用错）

**1. 这不是「让 Python 协程跑在 C++ 协程帧上」。**

C++20 协程帧和 Python `async`/`await` 是两套互不兼容的调度模型，不存在互操作。
`submit(fn, *args)` 的真实语义是：**把 Python 可调用对象放到 C++ 无锁池的 worker
线程上执行**，执行期间持有 GIL。因此

- 只有**会释放 GIL 的工作**（`subprocess`、socket IO、`time.sleep`、C 扩展）能拿到
  真并行；
- 纯 Python 计算换任何调度器都不会变快。

**2. `Channel` 只解决同进程 C++ → Python 的搬运。**

跨进程（例如另一个容器里的 MFF / CAN PDU 生产者）需要共享内存或 IPC，不经由这里。

## API

```python
import flowcoro_py as fc

pool = fc.CoroutineThreadPool(threads=8, pin_to_cores=True)
```

### `CoroutineThreadPool(threads=None, pin_to_cores=False, cpus=None)`

| 参数 | 含义 |
|---|---|
| `threads` | worker 数。`None` → `pin_to_cores` 时取物理核数，否则取 `hardware_concurrency()` |
| `pin_to_cores` | `True` 时把第 i 个 worker 绑到第 i 个**物理核**（SMT 兄弟不会重合） |
| `cpus` | 显式绑核列表，优先于 `pin_to_cores`。`threads > len(cpus)` 时按 `i % len(cpus)` 复用 |

属性：`threads`、`cpus`（实际绑核列表，空 = 未绑核）、`closed`。
方法：`submit(fn, *args, **kwargs) -> Future`、`close()`；支持
`with` 上下文管理。`close()` **幂等**，且会先等队列里的任务跑完（不是丢弃）。

### `Future`

`done() -> bool`、`wait(timeout=None)`、`result(timeout=None)`。
`result()` 可重复调用（与 `concurrent.futures` 一致）。任务抛 Python 异常时**原样重抛并保留
traceback**；超时抛 `TimeoutError`。`timeout` 单位是秒，`None` = 无限等待。

### `when_all(futures) -> list`

顺序等待**全部**完成，然后按输入顺序返回结果列表。任何一个失败就抛**索引最小**的那个，
但不会 fail-fast 半途丢结果 —— 其余任务仍然被等到完成。这是批量跑测需要的语义。

### `wait_any(futures, timeout=None) -> int | None`

返回最先完成的 Future 的索引；超时返回 `None`。走池级完成事件（`CompletionBus`），
不做轮询。仅支持同一池的 Future，混池会 `ValueError`。

### `Channel(capacity=10240)`

有界 MPMC 无锁环，载荷是 **`bytes`**（只接受 buffer 协议对象：`bytes` / `bytearray` /
`memoryview`；传 `str` 会 `TypeError`）。

```python
channel = fc.Channel(capacity=10240)
channel.try_push(b"...")   # -> bool；队满或已 close 返回 False
msg = channel.try_pop()    # -> Optional[bytes]
```

`capacity` 是**下界**，内部向上取 2 的幂（`Channel(capacity=3).capacity == 4`）。
属性：`size`（近似值，恒 `<= capacity`）、`capacity`、`closed`；`close()` 幂等，
关闭后 `try_push` 立即失败但已入队元素仍可 `try_pop` 取完。
`len(channel)` 等于 `size`。

### 其它

`fc.physical_core_cpus() -> list[int]`、`fc.shutdown()`（回收 hazard retired 节点，可重复调用）。

## GIL 模型（用之前值得知道）

| 时机 | GIL |
|---|---|
| `submit()` 入队 | 持有（需要构造 `fn/args/kwargs`） |
| 任务体执行 | 持有 |
| worker 等待调度 / 队列里排队 | 释放 |
| `when_all` / `wait_any` / `wait` / `result` 阻塞等待 | **释放**（否则与在跑任务互等） |
| `close()` 的 join | **释放**（同上） |

实现上有三条不能破的约束，改动 `python/flowcoro_py.cpp` 前请先读那里的文件头注释：

1. **池任务闭包只捕获 `shared_ptr`**，所有 Python 对象（入参、返回值、异常的
   `type/value/traceback`）装在 `gil_owned<T>` 里，由自定义 deleter 持 GIL 析构。
   否则 worker 或 hazard reclaimer 线程会在无 GIL 下 decref → 堆损坏。
2. **不能用 `ThreadPool::enqueue` / `std::future`** —— `~ThreadPool` 会给排队任务
   `broken_promise`，与真实异常无法区分。改用 `enqueue_void` + 自己的
   `FutureState{Pending/Done/Raised/Dropped}` 配 cleanup-once 守卫。
3. **解释器退出钩子注册在标准库 `atexit` 模块上**，不是 C 的 `Py_AtExit`：
   后者回调太晚（解释器已在 finalizing），worker 线程再碰 Python 会
   `Fatal Python error: PyEval_SaveThread ... finalizing` 并 abort。

正常用法是显式 `pool.close()`（或用 `with`）。不 close 也能干净退出（`atexit` 兜底会
join），但那是兜底，不是推荐路径。

## 并行粒度：什么时候用得上

**并行上限由物理核数决定，不由调度器决定。** 用 `physical_core_cpus()` 而不是
`os.cpu_count()` 定并发度 —— 后者是逻辑核，在带 SMT 的机器上会把并发超卖一倍。

几个已验证的形状（本机 6 物理核 / 12 逻辑核，`--workers 6 --tasks 24`）：

| 形状 | 串行 | `ThreadPoolExecutor` | `flowcoro_py` | flowcoro / ThreadPool |
|---|---|---|---|---|
| 子进程等待（等 `subprocess` 结束） | 1.46s | 0.246s (×5.94) | 0.247s (×5.92) | **1.00x** |
| 纯 Python 计算（全程持 GIL） | 1.27s | 1.26s (×1.00) | 1.27s (×0.99) | 0.99x |
| 微任务派发（任务体 ~0） | — | 200,903 tps | 48,007 tps | **0.24x** |

复现：`python3 benchmarks/python_batch_benchmark.py`。

结论直说：

- **子进程/IO 等待这种形状上，flowcoro 相对 `concurrent.futures.ThreadPoolExecutor`
  没有净收益（1.00x）。** 加速比来自「GIL 被放开 + 并发度 = 物理核数」，这两点
  Python 标准库同样做得到。
- 纯 Python 计算上两者一样慢，符合预期。
- 任务粒度极细时 flowcoro 明显更慢：每个任务多一层 pybind11 FFI。真实跑测的任务
  粒度是秒级（拉起容器 + 跑场景），不会落在这里。

因此：**不要指望「把批量执行核心换成 flowcoro」本身带来加速比。** 它值得用的场合是
需要「进程内、有界、非阻塞、低抖动且能精确绑核」的搬运通道，以及需要一个
C++/Python 共用的调度原语时。

## 已知限制

- 不提供「Python 协程并发调度」。要用 `async` 请用 `asyncio`。
- `Channel` 载荷限 `bytes`；要传结构化对象请自行序列化（或在 C++ 侧定义类型）。
- 不要在池内任务里 `close()` 自己所在的池（会 join 到自己）。
- 不要在任务里长期持有 GIL 做纯计算然后指望并行 —— 见上面的形状 B。
- Release 构建的全局 `-march=native -flto=auto` 会传进模块，产出的 `.so`
  **不可跨机器分发**。要做 wheel 请另开构建目录并覆盖这两个 flag。
