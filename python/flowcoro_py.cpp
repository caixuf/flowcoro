// =====================================================================
// python/flowcoro_py.cpp
//
// flowcoro 的 Python 绑定（pybind11）。
//
// 提供四样东西：
//   · CoroutineThreadPool —— 把 lockfree::ThreadPool 暴露给 Python，
//     可选用 physical_core_cpus() 把每个 worker 绑到独立物理核；
//     submit() 返回 Future。
//   · when_all / wait_any —— 对 N 个 Future 的汇聚。库里的 when_all 是
//     编译期变参的 C++ 协程组合子（when_any.h），Python 侧用不了；这里在
//     绑定层用「任务已并发在跑 + 顺序 join」实现，保序、语义等价。
//   · Channel —— 有界 MPMC 无锁环（bounded_channel.h），载荷 bytes，
//     非阻塞 try_push / try_pop。
//   · shutdown() —— drain hazard retired 节点（线程池内部的
//     lockfree::Queue 走 hazard pointer SMR）。
//
// 三条必须守住的实现约束（改动前先读）：
//
//   1) 不用 ThreadPool::enqueue/std::future。~ThreadPool 会给排队任务
//      broken_promise，与真实异常无法区分；且 enqueue 有 stop_ TOCTOU。
//      这里用 enqueue_void + 自己的 FutureState{Pending/Done/Raised/Dropped}
//      配 cleanup-once 守卫（TaskContext::~TaskContext）：任务被丢弃时会明确
//      标成 Dropped，而不是静默丢结果。
//
//   2) 池任务闭包只捕获 shared_ptr；所有 Python 对象装在 gil_owned<T> 里
//      （见 gil_holder.h），由自定义 deleter 持 GIL 析构。
//
//   3) 关池走「显式 close() → ThreadPool::shutdown()（stop + 无条件 join，
//      会把队列里剩余任务跑完）」，因此永不进入 ~ThreadPool 的 timeout+detach
//      分支（长任务必触发，detach 后 this 悬垂 = UAF）。close() 幂等，
//      __del__ 与 atexit 兜底都会调。join 期间必须放开 GIL（见 close()）。
//
// 边界（不要对外承诺更多）：
//   · submit 是把 Python 可调用对象放到 C++ 池线程上跑，不是「Python 协程跑在
//     C++ 协程帧上」。GIL 只在任务执行期间被持有，所以只有会释放 GIL 的工作
//     （subprocess / IO / C 扩展）才能拿到真并行；纯 Python 计算没有收益。
//   · Channel 只解决**同进程** C++ → Python 的搬运。跨进程（另一个容器里的
//     MFF/CAN 生产者）需要共享内存或 IPC，不经由这里。
// =====================================================================

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "flowcoro/bounded_channel.h"
#include "flowcoro/cpu_affinity.h"
#include "flowcoro/hazard_pointer.h"
#include "flowcoro/thread_pool.h"

#include "gil_holder.h"

namespace py = pybind11;
using flowcoro_py::make_gil_owned;

namespace {

// ---------------------------------------------------------------------------
// 任务入参 / 产出：所有 Python 对象都只能经 gil_owned 持有
// ---------------------------------------------------------------------------

struct PyCallable {
    py::object fn;
    py::tuple args;
    py::dict kwargs;
};

// 成功则 value 有效；失败则 exc_* 有效。
// exc_trace 允许是空对象（C++ 异常没有 traceback），PyErr_Restore 需要 NULL 而非 None。
struct PyOutcome {
    bool ok = false;
    py::object value;
    py::object exc_type;
    py::object exc_value;
    py::object exc_trace;
};

enum class TaskState { Pending, Done, Raised, Dropped };

struct PoolState;

// 池级完成事件：让 wait_any 无轮询等待。seq 单调递增，等待方察觉变化后重查
// 各 future 的 done 位即可（不会漏唤醒：先查 done 再读 seq，读到的 seq 若已变
// 则谓词立即为真）。
struct CompletionBus {
    std::mutex mtx;
    std::condition_variable cv;
    uint64_t seq = 0;

    void notify() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            ++seq;
        }
        cv.notify_all();
    }

    uint64_t current() {
        std::lock_guard<std::mutex> lock(mtx);
        return seq;
    }
};

struct FutureState {
    std::mutex mtx;
    std::condition_variable cv;
    TaskState state = TaskState::Pending;
    std::shared_ptr<PyOutcome> outcome;
    std::atomic<bool> done{false};
    std::weak_ptr<PoolState> pool;  // 仅用于 wait_any 的同池校验
};

struct PoolState {
    std::unique_ptr<lockfree::ThreadPool> pool;
    std::shared_ptr<CompletionBus> bus = std::make_shared<CompletionBus>();
    std::atomic<bool> closed{false};
};

// ---------------------------------------------------------------------------
// 活跃池注册表：Py_AtExit 兜底关池
// ---------------------------------------------------------------------------

std::mutex g_registry_mutex;
std::vector<std::weak_ptr<PoolState>> g_registry;

void close_pool(const std::shared_ptr<PoolState>& state) {
    if (!state) return;
    if (state->closed.exchange(true)) return;  // 幂等
    if (state->pool) {
        // stop + 无条件 join；队列里剩余任务会被 worker 跑完，不丢结果
        state->pool->shutdown();
    }
}

void register_pool(const std::shared_ptr<PoolState>& state) {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    g_registry.push_back(state);
}

void close_all_registered_pools() {
    std::vector<std::shared_ptr<PoolState>> alive;
    {
        std::lock_guard<std::mutex> lock(g_registry_mutex);
        for (auto& weak : g_registry) {
            if (auto state = weak.lock()) alive.push_back(state);
        }
        g_registry.clear();
    }
    if (alive.empty()) return;
    // 同 PyPool::close()：join 期间放开 GIL，否则与在跑任务互等
    py::gil_scoped_release release;
    for (auto& state : alive) close_pool(state);
}

// ---------------------------------------------------------------------------
// 任务执行
// ---------------------------------------------------------------------------

void complete(const std::shared_ptr<FutureState>& future, TaskState state,
              std::shared_ptr<PyOutcome> outcome) {
    {
        std::lock_guard<std::mutex> lock(future->mtx);
        future->state = state;
        future->outcome = std::move(outcome);
    }
    future->done.store(true, std::memory_order_release);
    future->cv.notify_all();
}

// 把 C++ 异常翻译成 Python 异常对象（需持有 GIL）
void fill_cpp_error(PyOutcome& out, PyObject* exc_type, const std::string& message) {
    PyObject* instance = PyObject_CallFunction(exc_type, "s", message.c_str());
    out.exc_type = py::reinterpret_borrow<py::object>(exc_type);
    if (instance != nullptr) {
        out.exc_value = py::reinterpret_steal<py::object>(instance);
    } else {
        PyErr_Clear();
        out.exc_value = py::str(message);
    }
    out.exc_trace = py::object();  // 无 traceback
}

// 任务闭包。析构即「入队后未被执行」的兜底路径：必须把 future 明确标成
// Dropped 并唤醒等待者，绝不能静默丢任务（cleanup-once 守卫）。
struct TaskContext {
    std::shared_ptr<PyCallable> payload;
    std::shared_ptr<FutureState> future;
    std::shared_ptr<CompletionBus> bus;
    std::atomic<bool> finished{false};

    ~TaskContext() {
        bool expected = false;
        if (finished.compare_exchange_strong(expected, true)) {
            complete(future, TaskState::Dropped, nullptr);
            if (bus) bus->notify();
        }
    }
};

// 任务体。**调用方必须已持有 GIL**（见 submit 里的闭包），因为整段都要碰
// Python 对象，且入参对象的释放也被刻意安排在 GIL 作用域内。
//
// 为什么释放点不能放到 worker_loop 里：闭包（std::function）的析构发生在
// `task()` 返回之后，那时 GIL 已释放，而 TaskContext 持有 py::object 入参
// ——析构会去 PyGILState_Ensure。若此时主线程正持 GIL 调 close()→join，
// 双方互等即死锁。把 `ctx.reset()` 挪进这里，闭包捕获的 shared_ptr 已置空，
// std::function 析构不再触碰 Python 对象。
void run_task_locked(const std::shared_ptr<TaskContext>& ctx) {
    std::shared_ptr<PyOutcome> outcome = make_gil_owned<PyOutcome>();
    try {
        py::object value =
            ctx->payload->fn(*ctx->payload->args, **ctx->payload->kwargs);
        outcome->ok = true;
        outcome->value = std::move(value);
    } catch (py::error_already_set& e) {
        outcome->ok = false;
        outcome->exc_type = e.type();
        outcome->exc_value = e.value();
        outcome->exc_trace = e.trace();
        // e 已把错误取走；清掉可能残留的错误指示器，避免污染后续 Python 调用
        PyErr_Clear();
    } catch (const std::bad_alloc&) {
        outcome->ok = false;
        fill_cpp_error(*outcome, PyExc_MemoryError,
                       "flowcoro_py task: std::bad_alloc");
    } catch (const std::exception& e) {
        outcome->ok = false;
        fill_cpp_error(*outcome, PyExc_RuntimeError, e.what());
    } catch (...) {
        outcome->ok = false;
        fill_cpp_error(*outcome, PyExc_RuntimeError,
                       "flowcoro_py task: unknown C++ exception");
    }

    ctx->finished.store(true, std::memory_order_release);
    complete(ctx->future, outcome->ok ? TaskState::Done : TaskState::Raised, outcome);
    if (ctx->bus) ctx->bus->notify();
}

// 用 PyErr_Restore 还原（保留 type/value/traceback）。调用方必须持有 GIL。
[[noreturn]] void raise_stored_error(const std::shared_ptr<PyOutcome>& outcome) {
    PyObject* type = outcome ? outcome->exc_type.ptr() : nullptr;
    PyObject* value = outcome ? outcome->exc_value.ptr() : nullptr;
    PyObject* trace = outcome ? outcome->exc_trace.ptr() : nullptr;
    if (type == nullptr) type = PyExc_RuntimeError;
    Py_INCREF(type);
    Py_XINCREF(value);
    Py_XINCREF(trace);
    PyErr_Restore(type, value, trace);
    throw py::error_already_set();
}

// 取结果。调用方必须持有 GIL，且已确认任务不再 Pending。
py::object take_result(const std::shared_ptr<FutureState>& future) {
    std::lock_guard<std::mutex> lock(future->mtx);
    if (future->state == TaskState::Dropped) {
        throw std::runtime_error(
            "flowcoro_py: task dropped before execution (pool shut down)");
    }
    if (future->state == TaskState::Pending) {
        throw std::runtime_error("flowcoro_py: task is still pending");
    }
    if (!future->outcome) {
        throw std::runtime_error("flowcoro_py: task finished without a result");
    }
    if (!future->outcome->ok) raise_stored_error(future->outcome);
    return future->outcome->value;
}

// ---------------------------------------------------------------------------
// 等待工具
// ---------------------------------------------------------------------------

// None → 无限等待；int/float 秒 → 毫秒。-1 表示无限。
long parse_timeout_ms(const py::object& timeout_obj) {
    if (timeout_obj.is_none()) return -1;
    const double seconds = timeout_obj.cast<double>();
    if (seconds < 0) return -1;
    return static_cast<long>(seconds * 1000.0);
}

[[noreturn]] void throw_timeout() {
    PyErr_SetString(PyExc_TimeoutError,
                    "flowcoro_py: task did not complete within the timeout");
    throw py::error_already_set();
}

// 等待单个 future。true = 已完成，false = 超时。
//
// GIL 顺序（关键）：先放 GIL，再抢 future 锁，最后 cv.wait。逆序会在
// 「worker 持 GIL 想抢 future 锁」与「本线程持 future 锁等 worker 发布」之间死锁。
// 退出作用域时 unique_lock 先析构、gil_scoped_release 后析构 → 先放锁再拿 GIL。
bool wait_state(const std::shared_ptr<FutureState>& future, long timeout_ms) {
    py::gil_scoped_release release;
    std::unique_lock<std::mutex> lock(future->mtx);
    if (future->state != TaskState::Pending) return true;
    if (timeout_ms < 0) {
        future->cv.wait(lock, [&] { return future->state != TaskState::Pending; });
        return true;
    }
    return future->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                              [&] { return future->state != TaskState::Pending; });
}

// ---------------------------------------------------------------------------
// Future
// ---------------------------------------------------------------------------

class PyFuture {
public:
    explicit PyFuture(std::shared_ptr<FutureState> state) : state_(std::move(state)) {}

    const std::shared_ptr<FutureState>& state() const { return state_; }

    bool done() const { return state_->done.load(std::memory_order_acquire); }

    void wait(const py::object& timeout) {
        if (!wait_state(state_, parse_timeout_ms(timeout))) throw_timeout();
    }

    py::object result(const py::object& timeout) {
        if (!wait_state(state_, parse_timeout_ms(timeout))) throw_timeout();
        return take_result(state_);
    }

private:
    std::shared_ptr<FutureState> state_;
};

// ---------------------------------------------------------------------------
// CoroutineThreadPool
// ---------------------------------------------------------------------------

class PyPool {
public:
    PyPool(const py::object& threads_obj, bool pin_to_cores,
           const py::object& cpus_obj) {
        std::vector<int> cpus;
        if (!cpus_obj.is_none()) {
            cpus = cpus_obj.cast<std::vector<int>>();
            if (cpus.empty()) {
                throw py::value_error("cpus must not be empty when provided");
            }
        } else if (pin_to_cores) {
            cpus = flowcoro::physical_core_cpus();
            if (cpus.empty()) {
                throw std::runtime_error(
                    "flowcoro_py: unable to detect physical cores for pin_to_cores");
            }
        }

        size_t threads;
        if (threads_obj.is_none()) {
            threads = cpus.empty() ? std::max(1u, std::thread::hardware_concurrency())
                                   : cpus.size();
        } else {
            const long requested = threads_obj.cast<long>();
            if (requested <= 0) throw py::value_error("threads must be > 0");
            threads = static_cast<size_t>(requested);
        }

        state_ = std::make_shared<PoolState>();
        state_->pool = std::make_unique<lockfree::ThreadPool>(threads, cpus);
        cpus_ = std::move(cpus);
        threads_ = threads;
        register_pool(state_);
    }

    ~PyPool() { close(); }

    PyPool(const PyPool&) = delete;
    PyPool& operator=(const PyPool&) = delete;

    // 关池：stop + 无条件 join（队列里剩余任务会先被跑完）。幂等。
    //
    // join 期间必须放开 GIL：仍在跑的任务需要 GIL 才能继续推进（例如
    // time.sleep 醒来后要重新获取），持 GIL join 会与之互等死锁。
    // 注意：不要从池内任务里 close 自己所在的池（会 join 到自己）。
    void close() {
        if (!state_) return;
        py::gil_scoped_release release;
        close_pool(state_);
    }

    bool closed() const { return !state_ || state_->closed.load(); }

    size_t threads() const { return threads_; }

    std::vector<int> cpus() const { return cpus_; }

    PyFuture submit(py::object fn, py::args args, py::kwargs kwargs) {
        if (!state_ || state_->closed.load()) {
            throw std::runtime_error("flowcoro_py: CoroutineThreadPool is closed");
        }

        // 入参只能在 GIL 下构造，之后交给 gil_owned 保证析构也在 GIL 下
        std::shared_ptr<PyCallable> payload = make_gil_owned<PyCallable>();
        payload->fn = std::move(fn);
        payload->args = py::tuple(args);
        payload->kwargs = py::dict(kwargs);

        auto future = std::make_shared<FutureState>();
        future->pool = state_;

        auto ctx = std::make_shared<TaskContext>();
        ctx->payload = payload;
        ctx->future = future;
        ctx->bus = state_->bus;

        try {
            // 整个 body 在 GIL 作用域内跑，并在这里（持 GIL 时）把闭包捕获的
            // ctx 置空 —— 入参对象的释放绝不能留到 std::function 析构点。
            state_->pool->enqueue_void([ctx]() mutable {
                py::gil_scoped_acquire gil;
                run_task_locked(ctx);
                ctx.reset();
            });
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("flowcoro_py: submit rejected: ") +
                                     e.what());
        }
        return PyFuture(std::move(future));
    }

    std::string repr() const {
        std::string text = "<flowcoro_py.CoroutineThreadPool threads=" +
                           std::to_string(threads_);
        if (!cpus_.empty()) text += " cpus=" + join(cpus_);
        text += closed() ? " closed>" : ">";
        return text;
    }

private:
    static std::string join(const std::vector<int>& values) {
        std::string text = "[";
        for (size_t i = 0; i < values.size(); ++i) {
            if (i) text += ", ";
            text += std::to_string(values[i]);
        }
        return text + "]";
    }

    std::shared_ptr<PoolState> state_;
    std::vector<int> cpus_;
    size_t threads_ = 0;
};

// ---------------------------------------------------------------------------
// 汇聚：when_all / wait_any
// ---------------------------------------------------------------------------

std::vector<std::shared_ptr<FutureState>> extract_states(const py::object& futures) {
    std::vector<py::object> items;
    for (py::handle handle : futures) {
        items.push_back(py::reinterpret_borrow<py::object>(handle));  // 保活到取完状态
    }

    std::vector<std::shared_ptr<FutureState>> states;
    states.reserve(items.size());
    for (const auto& item : items) {
        if (!py::isinstance<PyFuture>(item)) {
            throw py::type_error("flowcoro_py: expected a Future or an iterable of Futures");
        }
        states.push_back(item.cast<PyFuture*>()->state());
    }
    return states;
}

// 顺序等待全部完成（不在中途抛），然后按输入顺序取结果；有事则抛索引最小的那个。
py::list when_all(const py::object& futures) {
    const std::vector<std::shared_ptr<FutureState>> states = extract_states(futures);

    {
        // 一次放 GIL 等完全部；任务早已在池里并发执行，这里只是 join
        py::gil_scoped_release release;
        for (const auto& state : states) {
            std::unique_lock<std::mutex> lock(state->mtx);
            state->cv.wait(lock, [&] { return state->state != TaskState::Pending; });
        }
    }

    py::list results;
    for (const auto& state : states) {
        results.append(take_result(state));  // 抛索引最小的失败
    }
    return results;
}

// 返回最先完成的索引；超时返回 None。不做轮询（走池级 CompletionBus）。
py::object wait_any(const py::object& futures, const py::object& timeout) {
    const std::vector<std::shared_ptr<FutureState>> states = extract_states(futures);
    if (states.empty()) {
        throw py::value_error("flowcoro_py: wait_any requires at least one future");
    }

    std::shared_ptr<PoolState> pool = states.front()->pool.lock();
    if (!pool) {
        throw std::runtime_error("flowcoro_py: the owning pool no longer exists");
    }
    for (const auto& state : states) {
        if (state->pool.lock() != pool) {
            throw py::value_error(
                "flowcoro_py: wait_any requires futures from the same pool");
        }
    }

    const long timeout_ms = parse_timeout_ms(timeout);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms);
    const std::shared_ptr<CompletionBus> bus = pool->bus;

    for (;;) {
        for (size_t i = 0; i < states.size(); ++i) {
            if (states[i]->done.load(std::memory_order_acquire)) {
                return py::int_(i);
            }
        }

        long remaining_ms = -1;
        if (timeout_ms >= 0) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) return py::none();
            remaining_ms = static_cast<long>(
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
                    .count());
        }

        {
            py::gil_scoped_release release;
            std::unique_lock<std::mutex> lock(bus->mtx);
            const uint64_t seen = bus->seq;
            const auto changed = [&] { return bus->seq != seen; };
            if (remaining_ms < 0) {
                bus->cv.wait(lock, changed);
            } else {
                bus->cv.wait_for(lock, std::chrono::milliseconds(remaining_ms), changed);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Channel（同进程 C++ → Python，载荷 bytes）
// ---------------------------------------------------------------------------

std::string to_bytes(const py::object& data) {
    if (py::isinstance<py::str>(data)) {
        throw py::type_error(
            "flowcoro_py.Channel requires a bytes-like object, not str "
            "(encode it first, e.g. data.encode())");
    }
    Py_buffer view{};
    if (PyObject_GetBuffer(data.ptr(), &view, PyBUF_SIMPLE) != 0) {
        throw py::error_already_set();
    }
    std::string out;
    if (view.len > 0) {
        out.assign(static_cast<const char*>(view.buf),
                   static_cast<size_t>(view.len));
    }
    PyBuffer_Release(&view);
    return out;
}

class PyChannel {
public:
    explicit PyChannel(size_t capacity) {
        if (capacity == 0) throw py::value_error("capacity must be >= 1");
        channel_ = std::make_unique<flowcoro::BoundedChannel<std::string>>(capacity);
    }

    bool try_push(const py::object& data) {
        std::string bytes = to_bytes(data);
        return channel_->try_push(std::move(bytes));
    }

    py::object try_pop() {
        std::string out;
        if (!channel_->try_pop(out)) return py::none();
        return py::bytes(out);
    }

    void close() { channel_->close(); }
    bool closed() const { return channel_->is_closed(); }
    size_t size() const { return channel_->size(); }
    size_t capacity() const { return channel_->capacity(); }
    bool empty() const { return channel_->empty(); }

    std::string repr() const {
        return "<flowcoro_py.Channel size=" + std::to_string(size()) + "/" +
               std::to_string(capacity()) +
               (closed() ? " closed>" : ">");
    }

private:
    std::unique_ptr<flowcoro::BoundedChannel<std::string>> channel_;
};

}  // namespace

// ---------------------------------------------------------------------------
// 模块
// ---------------------------------------------------------------------------

PYBIND11_MODULE(flowcoro_py, m) {
    m.doc() = R"(flowcoro 的 Python 绑定。

典型用法（批量任务并发调度）：

    import flowcoro_py as fc

    pool = fc.CoroutineThreadPool(threads=8, pin_to_cores=True)
    futures = [pool.submit(run_case, path) for path in cases]
    results = fc.when_all(futures)      # 保序；失败抛索引最小的那个
    pool.close()

注意：submit 是把 Python 可调用对象放到 C++ 池线程上跑，只有会释放 GIL 的
工作（subprocess / IO / C 扩展）才能拿到真并行。
)";

    m.attr("__version__") = FLOWCORO_PY_VERSION;

    py::class_<PyFuture>(m, "Future",
                         "提交任务的句柄。result() 会阻塞并放 GIL；重复取值是允许的。")
        .def("done", &PyFuture::done,
             "任务是否已完成（非阻塞）。")
        .def("wait", &PyFuture::wait, py::arg("timeout") = py::none(),
             "阻塞等待完成；超时抛 TimeoutError。")
        .def("result", &PyFuture::result, py::arg("timeout") = py::none(),
             "阻塞等待并返回结果；任务失败则原样重抛（保留 traceback）。")
        .def("__repr__", [](const PyFuture& self) {
            return std::string("<flowcoro_py.Future ") +
                   (self.done() ? "done>" : "pending>");
        });

    py::class_<PyPool>(m, "CoroutineThreadPool",
                       "基于 flowcoro 无锁线程池的协程/任务池（C++ 线程池 + "
                       "Python 可调用对象）。")
        .def(py::init<const py::object&, bool, const py::object&>(),
             py::arg("threads") = py::none(), py::arg("pin_to_cores") = false,
             py::arg("cpus") = py::none(),
             "threads: 工作线程数（None = pin_to_cores 时取物理核数，否则取 CPU 数）。\n"
             "pin_to_cores: True 时逐个绑到不同物理核（SMT 兄弟不会重合）。\n"
             "cpus: 显式指定绑核列表，优先于 pin_to_cores。")
        .def("submit", &PyPool::submit, py::arg("fn"),
             "提交可调用对象；返回 Future。多余位置/关键字参数原样传给 fn。")
        .def("close", &PyPool::close,
             "关池：停止接单并 join（队列里剩余任务会先跑完）。幂等。")
        .def("__enter__", [](PyPool& self) -> PyPool& { return self; })
        .def("__exit__", [](PyPool& self, py::object, py::object, py::object) {
            self.close();
            return false;
        })
        .def_property_readonly("closed", &PyPool::closed)
        .def_property_readonly("threads", &PyPool::threads)
        .def_property_readonly("cpus", &PyPool::cpus,
                               "实际绑核列表（空 = 未绑核）。")
        .def("__repr__", &PyPool::repr);

    m.def("when_all", &when_all, py::arg("futures"),
          "顺序等待全部完成并返回结果列表（保序）。任何一个失败则抛索引最小的那个，"
          "但所有任务都会被等到完成。");

    m.def("wait_any", &wait_any, py::arg("futures"), py::arg("timeout") = py::none(),
          "返回最先完成的 Future 的索引；超时返回 None。仅支持同一个池的 Future。");

    py::class_<PyChannel>(m, "Channel",
                          "有界 MPMC 无锁通道，载荷 bytes（非阻塞 try_push/try_pop）。")
        .def(py::init<size_t>(), py::arg("capacity") = 10240,
             "capacity: 容量下界，内部向上取 2 的幂。")
        .def("try_push", &PyChannel::try_push, py::arg("data"),
             "非阻塞入队，队满或已关闭返回 False。")
        .def("try_pop", &PyChannel::try_pop,
             "非阻塞出队，返回 bytes 或 None（空）。")
        .def("close", &PyChannel::close, "关闭：try_push 立即失败，已有元素仍可取完。")
        .def_property_readonly("closed", &PyChannel::closed)
        .def_property_readonly("size", &PyChannel::size, "近似元素数（恒 <= capacity）。")
        .def_property_readonly("capacity", &PyChannel::capacity)
        .def("__len__", &PyChannel::size)
        .def("__repr__", &PyChannel::repr);

    m.def("physical_core_cpus", &flowcoro::physical_core_cpus,
          "本机物理核代表 CPU 列表（与 cpu_topology.sh 的 thread_siblings_list 同口径）。");

    m.def("shutdown", [] {
        flowcoro::smr::HazardManager::instance().drain_all();
    }, "回收 hazard retired 节点。进程退出前可调用（多次安全）。");

    // 解释器退出前的兜底：把所有还活着的池 join 掉（正常路径应显式 pool.close()）。
    //
    // 必须注册到**标准库 atexit 模块**，不能用 C 的 Py_AtExit：
    // Py_AtExit 回调在 Py_FinalizeEx 里触发得太晚（解释器已在 finalizing），
    // 此时 worker 线程再碰 Python 会直接
    // 「Fatal Python error: PyEval_SaveThread: ... finalizing」并 abort。
    // atexit 模块的回调由 call_py_exitfuncs 触发，CPython 明确保证
    // 「解释器在此处仍完全完好（the interpreter is still entirely intact）,
    //  退出函数可以依赖这一点」，因此线程能正常拿 GIL 把在跑的任务跑完。
    py::module_::import("atexit").attr("register")(
        py::cpp_function([]() { close_all_registered_pools(); }));
}
