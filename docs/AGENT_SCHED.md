# Agent scheduler surface (Hermes-like underlying)

> **GO** (北极星) · additive Pool/Channel façade · one concurrent-tool demo · no Hermes wire

FlowCoro as the **tool-fanout scheduler** under an AI agent loop — not an agent itself.

Prefer the Python binding (`flowcoro_py`). C++ remains available via umbrella + optional `app.hpp`.

## Loop shape

```text
agent turn
  ├─ decide tools (caller / LLM — out of FlowCoro)
  ├─ AgentScheduler.submit_tool × N
  ├─ gather | wait_first
  ├─ cancel stragglers (cooperative)
  └─ feed results back to agent (caller)
```

## Map → primitives

| Agent need | Use |
|---|---|
| Run tool on worker | `CoroutineThreadPool.submit` |
| Wait all tools | `when_all(futures)` |
| First done | `wait_any(futures, timeout)` |
| Stream results | `Channel` + serialize to `bytes` |
| Cancel | `CancellationTokenSource.cancel()`; tool polls `cancel_token` |
| Bind | `flowcoro_py.CancellationTokenSource` / `CancellationToken` / `CancelledError` |

See `PYTHON_BINDING.md` for GIL rules (waits release GIL; CPU-bound Python still holds GIL).

## Façade

`examples/python/flowcoro_agent.py` → `AgentScheduler` / `ToolHandle`  
Demo: `examples/python/agent_tool_fanout_demo.py`

## Cancel

`AgentScheduler` Prefer one Source per tool; `cancel_all` cancels each. Knife A `cancel_event` remains wide-in one release.

### Risk (honest)

**Pool ≠ Task.** Cancel is cooperative poll only — not `Future` preempt, not thread kill. Tools that ignore the token may still finish successfully.

## Limits (honest)

- Cancel is **cooperative**, not preempt (see above).
- Channel payloads are `bytes` only.
- Same-process only; no cross-container Channel.
- Do not claim speedups vs `concurrent.futures.ThreadPoolExecutor`.

## Out

Hermes / MCP / OpenAI tool schemas · LLM clients · sched core changes.
