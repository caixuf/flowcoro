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
| Cancel | Knife A: `threading.Event` checked by tool; Knife B: bind `CancellationToken` |

See `PYTHON_BINDING.md` for GIL rules (waits release GIL; CPU-bound Python still holds GIL).

## Façade

`examples/python/flowcoro_agent.py` → `AgentScheduler` / `ToolHandle`  
Demo: `examples/python/agent_tool_fanout_demo.py`

## Limits (honest)

- Knife A cancel is **cooperative**, not preempt.
- Channel payloads are `bytes` only.
- Same-process only; no cross-container Channel.
- Do not claim speedups vs `concurrent.futures.ThreadPoolExecutor`.

## Out

Hermes / MCP / OpenAI tool schemas · LLM clients · sched core changes.
