#!/usr/bin/env python3
"""App-facing Python IO demo (flowcoro_py).

Uses CoroutineThreadPool + Channel + when_all on GIL-releasing waits
(time.sleep / trivial IO). See docs/PYTHON_BINDING.md — this is NOT a
Python async/await bridge, and we make no speed claim vs ThreadPoolExecutor.

Build:
    cmake -B build-py -DFLOWCORO_BUILD_PYTHON=ON
    cmake --build build-py --target flowcoro_py
"""

from __future__ import annotations

import pathlib
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
        "flowcoro_py not found. See docs/PYTHON_BINDING.md\n"
        "  cmake -B build-py -DFLOWCORO_BUILD_PYTHON=ON && "
        "cmake --build build-py --target flowcoro_py"
    )


def main() -> int:
    fc = _import_flowcoro_py()
    ch = fc.Channel(capacity=8)
    t0 = time.perf_counter()

    def producer():
        for i in range(10):
            payload = f"msg-{i}".encode()
            while not ch.try_push(payload):
                time.sleep(0.001)  # GIL-releasing wait
            time.sleep(0.001)
        ch.close()
        return 10

    def consumer():
        n = 0
        while True:
            item = ch.try_pop()
            if item is None:
                if ch.closed:
                    break
                time.sleep(0.001)
                continue
            n += 1
        return n

    with fc.CoroutineThreadPool(threads=2) as pool:
        n_prod, n_cons = fc.when_all([pool.submit(producer), pool.submit(consumer)])

    wall_ms = (time.perf_counter() - t0) * 1000.0
    print(
        f"app_io_demo.py flowcoro_py={fc.__version__} "
        f"produced={n_prod} consumed={n_cons} wall_ms={wall_ms:.1f}"
    )
    print("Note: timings are wall clock only; see docs/PYTHON_BINDING.md / docs/APP_FACING.md")
    assert n_prod == 10 and n_cons == 10
    print("app_io_demo.py OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
