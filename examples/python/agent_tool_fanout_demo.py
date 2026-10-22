#!/usr/bin/env python3
"""Agent-like N-tool fanout on FlowCoro — not a full LLM agent.

Shows: submit_tool × N → gather / wait_first → cooperative cancel.
"""
from __future__ import annotations

import json
import pathlib
import sys
import time
import threading

_ROOT = pathlib.Path(__file__).resolve().parents[2]
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

def _import_fc():
    try:
        import flowcoro_py as fc  # noqa: F401
        return
    except ImportError:
        for candidate in sorted(_ROOT.glob("build*/python")):
            if list(candidate.glob("flowcoro_py*.so")):
                sys.path.insert(0, str(candidate))
                return
    raise SystemExit("flowcoro_py missing — build with FLOWCORO_BUILD_PYTHON=ON")


_import_fc()
from flowcoro_agent import AgentScheduler  # noqa: E402
import flowcoro_py as fc  # noqa: E402


def stub_tool(tool_id: int, work_ms: int, cancel_event: threading.Event) -> dict:
    step = 0.02
    waited = 0.0
    while waited < work_ms / 1000.0:
        if cancel_event.is_set():
            return {"id": tool_id, "status": "cancelled", "waited_ms": int(waited * 1000)}
        time.sleep(step)
        waited += step
    return {"id": tool_id, "status": "ok", "waited_ms": work_ms}


def main() -> int:
    durations = [80, 120, 200, 300, 450, 600]
    bus = fc.Channel(capacity=16)

    with AgentScheduler(threads=4) as sched:
        handles = [
            sched.submit_tool(f"tool-{i}", stub_tool, i, ms)
            for i, ms in enumerate(durations)
        ]

        # Cancel two long tools cooperatively.
        sched.cancel(handles[4])
        sched.cancel(handles[5])

        idx = sched.wait_first(handles, timeout=1.0)
        print(f"wait_first index={idx}")

        results = sched.gather(handles)
        for r in results:
            payload = json.dumps(r).encode()
            while not bus.try_push(payload):
                time.sleep(0.001)
        bus.close()

    drained = []
    while True:
        item = bus.try_pop()
        if item is None:
            if bus.closed:
                break
            time.sleep(0.001)
            continue
        drained.append(json.loads(item))

    ok = sum(1 for r in drained if r["status"] == "ok")
    cancelled = sum(1 for r in drained if r["status"] == "cancelled")
    print(f"ok={ok} cancelled={cancelled} total={len(drained)}")
    assert ok >= 3 and cancelled >= 1
    print("agent_tool_fanout_demo OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
