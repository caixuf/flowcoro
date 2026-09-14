// =====================================================================
// python/gil_holder.h
//
// GIL 安全的 Python 对象持有者。
//
// 背景：flowcoro 池任务的闭包会被 worker 线程或 hazard reclaimer 线程析构，
// 这些线程不持有 GIL。任何 py::object 成员的析构都会做 refcount decref ——
// 没有 GIL 就是未定义行为（崩溃 / 堆损坏）。
//
// 约定（flowcoro_py 里必须遵守）：
//   · 池任务闭包只捕获 shared_ptr，绝不含裸 py::object / exception_ptr；
//   · 所有需要活过 worker 调用边界的 Python 对象（入参、返回值、异常
//     type/value/traceback）都装在 gil_owned<T> 里；
//   · 解释器终结阶段不能再 PyGILState_Ensure（会 abort），此时只做裸 delete，
//     把 Python 对象的引用交给进程退出回收。
// =====================================================================

#ifndef FLOWCORO_PY_GIL_HOLDER_H
#define FLOWCORO_PY_GIL_HOLDER_H

#include <memory>
#include <utility>

#include <Python.h>

namespace flowcoro_py {

template <typename T>
struct GilDeleter {
    void operator()(T* ptr) const noexcept {
        if (ptr == nullptr) return;
        if (Py_IsInitialized() != 0) {
            const PyGILState_STATE state = PyGILState_Ensure();
            delete ptr;
            PyGILState_Release(state);
        } else {
            // 解释器已终结：不能 Ensure，直接还内存（跳过析构 → 引用交给进程退出）
            ::operator delete(static_cast<void*>(ptr));
        }
    }
};

// 用共享所有权持有 T，且保证析构时持有 GIL。
template <typename T, typename... Args>
std::shared_ptr<T> make_gil_owned(Args&&... args) {
    return std::shared_ptr<T>(new T(std::forward<Args>(args)...), GilDeleter<T>{});
}

}  // namespace flowcoro_py

#endif  // FLOWCORO_PY_GIL_HOLDER_H
