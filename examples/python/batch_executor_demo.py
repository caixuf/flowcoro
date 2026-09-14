#!/usr/bin/env python3
"""需求场景 1 的可运行版本：批量任务并发调度器。

跑法（先在 build-py 里构建出模块）：

    cmake -B build-py -DFLOWCORO_BUILD_PYTHON=ON
    cmake --build build-py --target flowcoro_py
    python3 examples/python/batch_executor_demo.py

脚本会自动在仓库根下找 ``build*/python/flowcoro_py*.so``，也可以直接设
``PYTHONPATH``。

注意读一下最后打印的「结果」一节：这里的收益来自并发度和绑核，不来自
调度器本身 —— 见 docs/PYTHON_BINDING.md 里的基准表。
"""

from __future__ import annotations

import pathlib
import random
import sys
import time

_ROOT = pathlib.Path(__file__).resolve().parents[2]


def _import_flowcoro_py():
    try:
        import flowcoro_py  # noqa: PLC0415

        return flowcoro_py
    except ImportError:
        for candidate in sorted(_ROOT.glob("build*/python")):
            if list(candidate.glob("flowcoro_py*.so")):
                sys.path.insert(0, str(candidate))
                import flowcoro_py  # noqa: PLC0415

                return flowcoro_py
    raise SystemExit(
        "flowcoro_py 未找到。先构建：\n"
        "  cmake -B build-py -DFLOWCORO_BUILD_PYTHON=ON\n"
        "  cmake --build build-py --target flowcoro_py"
    )


def main() -> int:
    fc = _import_flowcoro_py()

    cores = fc.physical_core_cpus()
    print(f"flowcoro_py {fc.__version__}  物理核={cores}")
    if not cores:
        raise SystemExit("未能枚举物理核")

    # 真实场景里这里换成「拉起一个分区容器并等它结束」；用 subprocess 之外的
    # time.sleep 也能说明问题——关键是被等待期间 GIL 被放开。
    cases = [f"case_{i:02d}" for i in range(12)]
    workers = min(len(cores), len(cases))
    print(f"用例 {len(cases)} 个，并发 {workers}（绑核）\n")

    def run_case(path: str) -> dict:
        # 模拟：部署 + 跑场景 + 收集结果
        time.sleep(random.uniform(0.05, 0.15))
        return {"case": path, "ok": random.random() > 0.1}

    with fc.CoroutineThreadPool(threads=workers, pin_to_cores=True) as pool:
        print(f"pool={pool}")
        futures = [pool.submit(run_case, path) for path in cases]

        started = time.monotonic()
        # 一次放 GIL 等完全部；结果严格按 cases 顺序
        results = fc.when_all(futures)
        elapsed = time.monotonic() - started

    # 对照：串行跑同样的任务
    serial_start = time.monotonic()
    [run_case(path) for path in cases]
    serial_elapsed = time.monotonic() - serial_start

    passed = sum(1 for r in results if r["ok"])
    print(f"\n结果（保序）：{len(results)} 个")
    for r in results:
        print(f"  {r['case']}  {'PASS' if r['ok'] else 'FAIL'}")
    print(f"\n并行耗时 {elapsed:.2f}s（串行 {serial_elapsed:.2f}s）")
    print(f"通过 {passed}/{len(results)}")

    # 场景 2：把同进程 C++ 侧的高频消息搬给 Python
    print("\nChannel（同进程 C++ → Python，载荷 bytes）：")
    channel = fc.Channel(capacity=4096)
    for i in range(5):
        channel.try_push(f"pdu-{i}".encode())
    drained = []
    while (msg := channel.try_pop()) is not None:
        drained.append(msg)
    print(f"  capacity={channel.capacity} drained={drained} empty再取={channel.try_pop()}")

    print(
        "\n注：并行加速来自「并发度 = 物理核数 + GIL 在等待期被放开」，"
        "不是 flowcoro 独有的 —— 与 concurrent.futures 实测持平（见基准表）。"
    )
    fc.shutdown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
