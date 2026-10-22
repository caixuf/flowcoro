#!/usr/bin/env python3
"""App-facing Channel pipeline scaffold for flowcoro_py.

Shows producer → Channel → consumer with CoroutineThreadPool.
Does not touch C++ core scheduler sources.

Build first:
    cmake -B build-py -DFLOWCORO_BUILD_PYTHON=ON
    cmake --build build-py --target flowcoro_py
    python3 examples/python/channel_pipeline_demo.py
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
        "flowcoro_py not found. Build with:\n"
        "  cmake -B build-py -DFLOWCORO_BUILD_PYTHON=ON\n"
        "  cmake --build build-py --target flowcoro_py"
    )


def main() -> int:
    fc = _import_flowcoro_py()
    ch = fc.Channel(capacity=8)
    print(f"flowcoro_py {fc.__version__}  Channel capacity={ch.capacity}")

    produced = []
    consumed = []

    def producer():
        for i in range(12):
            payload = f"msg-{i}".encode()
            # Busy-wait when full — matches try_push non-blocking contract.
            while not ch.try_push(payload):
                time.sleep(0.001)
            produced.append(payload)
        ch.close()
        return len(produced)

    def consumer():
        while True:
            item = ch.try_pop()
            if item is None:
                if ch.closed:
                    break
                time.sleep(0.001)
                continue
            consumed.append(item)
        return len(consumed)

    with fc.CoroutineThreadPool(threads=2) as pool:
        f_prod = pool.submit(producer)
        f_cons = pool.submit(consumer)
        n_prod, n_cons = fc.when_all([f_prod, f_cons])

    print(f"produced={n_prod} consumed={n_cons} closed={ch.closed}")
    assert n_prod == 12 and n_cons == 12
    assert produced == consumed
    print("channel pipeline scaffold OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
