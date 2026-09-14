#!/usr/bin/env python3
"""flowcoro_py 批量调度基准：对比 concurrent.futures.ThreadPoolExecutor。

为什么只比这两种形状，以及结论为什么可以外推到真实跑测：

  形状 A（子进程/IO 等待，GIL 释放）
      每个任务等一个子进程跑完 —— 这就是 IMFL 跑测里「拉起一个分区容器
      然后等它结束」的形状。GIL 在等待期被放开，是**唯一**能拿到真并行的
      形状。两个后端的差别只剩调度器本身。
  形状 B（纯 Python 计算，全程持 GIL）
      两个后端都退化成串行。放进来是为了把「没有 free lunch」写进结果里，
      避免把 flowcoro 当成本地加速器。
  形状 C（微任务派发开销）
      任务体几乎不耗时，纯粹量调度器自己的成本。任务粒度越细，C++ 池 +
      pybind11 那层 FFI 相对 Python 原生 ThreadPoolExecutor 越可能落后。

用法：
  python3 benchmarks/python_batch_benchmark.py                # 自动找 build*/python/flowcoro_py*.so
  python3 benchmarks/python_batch_benchmark.py --json out.json
  PYTHONPATH=build-py/python python3 benchmarks/python_batch_benchmark.py
"""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

_ROOTS = pathlib.Path(__file__).resolve().parents[1]


def _import_flowcoro_py():
    try:
        import flowcoro_py  # noqa: PLC0415

        return flowcoro_py
    except ImportError:
        for candidate in sorted(_ROOTS.glob("build*/python")):
            if list(candidate.glob("flowcoro_py*.so")):
                sys.path.insert(0, str(candidate))
                import flowcoro_py  # noqa: PLC0415

                return flowcoro_py
    raise SystemExit(
        "flowcoro_py 未找到。先构建：\n"
        "  cmake -B build-py -DFLOWCORO_BUILD_PYTHON=ON && cmake --build build-py --target flowcoro_py"
    )


def _subprocess_task(_index: int) -> None:
    """形状 A：等一个子进程结束（GIL 在 wait 期间释放）。"""
    subprocess.run(
        [sys.executable, "-c", "import time; time.sleep(0.05)"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )


def _sleep_task(_index: int) -> None:
    time.sleep(0.05)


def _compute_task(index: int) -> int:
    """形状 B：纯 Python 计算，全程持 GIL。"""
    return sum(i * i for i in range(index % 7 + 2_000_000))


def _trivial_task(_index: int) -> int:
    return 1


# (名称, 任务体, 任务数倍率) —— 倍率用于把极细粒度的形状放大到可测量
SHAPES = [
    ("A 子进程等待(0.05s)", _subprocess_task, 1),
    ("B 纯Python计算(~0.1s)", _compute_task, 1),
    ("C 微任务派发", _trivial_task, 500),
]


def _time_it(fn) -> float:
    start = time.perf_counter()
    fn()
    return time.perf_counter() - start


def run_shape(name: str, task, tasks: int, workers: int, fc) -> dict:
    results = {"shape": name, "tasks": tasks, "workers": workers}

    results["serial_s"] = _time_it(lambda: [task(i) for i in range(tasks)])

    def run_native():
        with ThreadPoolExecutor(max_workers=workers) as executor:
            list(executor.map(task, range(tasks)))

    results["threadpool_s"] = _time_it(run_native)

    def run_flowcoro():
        pool = fc.CoroutineThreadPool(threads=workers, pin_to_cores=True)
        try:
            fc.when_all([pool.submit(task, i) for i in range(tasks)])
        finally:
            pool.close()

    results["flowcoro_s"] = _time_it(run_flowcoro)

    serial = results["serial_s"]
    results["threadpool_speedup"] = serial / results["threadpool_s"] if results["threadpool_s"] else float("nan")
    results["flowcoro_speedup"] = serial / results["flowcoro_s"] if results["flowcoro_s"] else float("nan")
    results["flowcoro_vs_threadpool"] = (
        results["threadpool_s"] / results["flowcoro_s"] if results["flowcoro_s"] else float("nan")
    )
    # 微任务形状里绝对耗时接近 0，加速比没意义，吞吐量才可读
    for key in ("serial", "threadpool", "flowcoro"):
        seconds = results[f"{key}_s"]
        results[f"{key}_tps"] = tasks / seconds if seconds > 0 else float("inf")
    return results


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tasks", type=int, default=0,
                        help="每个形状的任务数（0 = 按 workers 推导）")
    parser.add_argument("--workers", type=int, default=0,
                        help="并发数（0 = min(物理核, 8)）")
    parser.add_argument("--json", type=pathlib.Path, help="把结果写到 JSON")
    args = parser.parse_args()

    fc = _import_flowcoro_py()
    cores = fc.physical_core_cpus()
    workers = args.workers or max(1, min(8, len(cores) or 1))
    tasks = args.tasks or workers * 4

    print(f"# flowcoro_py {fc.__version__} 批量调度基准")
    print(f"# 物理核 {cores}")
    print(f"# 并发数 {workers}   基础任务数 {tasks}")
    print("# flowcoro 侧 pin_to_cores=True\n")

    rows = []
    for name, task, multiplier in SHAPES:
        shape_tasks = tasks * multiplier
        row = run_shape(name, task, shape_tasks, workers, fc)
        rows.append(row)
        print(
            f"{row['shape']:<22} n={row['tasks']:<6} "
            f"串行 {row['serial_s']:7.3f}s/{row['serial_tps']:>10,.0f}tps | "
            f"ThreadPool {row['threadpool_s']:7.3f}s/{row['threadpool_tps']:>10,.0f}tps "
            f"(x{row['threadpool_speedup']:5.2f}) | "
            f"flowcoro {row['flowcoro_s']:7.3f}s/{row['flowcoro_tps']:>10,.0f}tps "
            f"(x{row['flowcoro_speedup']:5.2f}) | "
            f"flowcoro/ThreadPool {row['flowcoro_vs_threadpool']:5.2f}x"
        )

    print(
        "\n读法：\n"
        "  · 形状 A 才是有意义的那一栏 —— 任务在等外部进程时 GIL 被释放，两个后端都能并行；\n"
        "    两者之差就是「调度器 + 绑核」的净收益。\n"
        "  · 形状 B 的加速比应当接近 1.0 —— 全程持 GIL 的工作换任何调度器都不会变快。\n"
        "  · 形状 C 若 flowcoro 落后是正常的：每任务多一层 pybind11 FFI，任务越细越吃亏。\n"
        "    真实跑测的任务粒度是秒级（拉起容器 + 跑场景），不会落在这一栏。"
    )

    if args.json:
        args.json.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
        print(f"\n结果已写入 {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
