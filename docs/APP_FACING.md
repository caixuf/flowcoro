# App-facing façade map

One-page map from demos → existing primitives. **No core sched rewrite.**

Prefer `#include <flowcoro.hpp>` (umbrella). Optional thin map: `flowcoro/app.hpp`.

| Façade / demo | Existing primitive | Notes |
|---|---|---|
| `flowcoro::app::HttpClient` | `flowcoro::net::HttpClient` (`http_client.h`) | GET/POST awaitable + `request_sync` |
| `flowcoro::app::HttpServer` | `flowcoro::net::HttpServer` (`http_server.h`) | Thin route table over `TcpServer` (echo/chat patterns) |
| `make_pipeline_channel` | `make_channel` / `Channel` (`channel.h`) | Bounded MPMC; also see `bounded_channel.h` |
| `fan_in` | `when_all` | Named recipe for demos |
| `tool_fan_in` / `ToolFanIn` | `when_all` + `CancellationToken` | Agent tool fan-in; see [`AGENT_SCHED.md`](AGENT_SCHED.md) |
| `Stopwatch` | `std::chrono` | Honest wall timings only — not vs ThreadPoolExecutor |
| Python `CoroutineThreadPool` + `Channel` | `docs/PYTHON_BINDING.md` | Separate surface; GIL-releasing waits only |

## Examples

| Binary / script | Shows |
|---|---|
| `examples/app_io_demo/main.cpp` | HttpServer + Channel + `when_all` + timings |
| `examples/http_client_demo.cpp` | Local-stub HTTP client (no external net) |
| `examples/http_channel_demo.cpp` | HttpServer + Channel lifecycle |
| `examples/python/app_io_demo.py` | Pool + Channel bytes + `when_all` |
| `examples/python/channel_pipeline_demo.py` | Minimal Channel pipeline |
| `examples/python/agent_tool_fanout_demo.py` | Agent-like N-tool fanout + coop cancel |
| `examples/agent_tool_fanout_demo.cpp` | C++ ToolFanIn + CancellationToken mirror |
| `examples/python/flowcoro_agent.py` | `AgentScheduler` facade (Knife B: per-tool `CancellationTokenSource`) |

## Out of scope

- Scheduler / `thread_pool` / `RtExecutor` / `BoundedChannel` core changes
- New Python async/await bridge
- Speedup claims vs `concurrent.futures.ThreadPoolExecutor`
- Invented HTTP/wire schemas (use existing `HttpResponse` / request structs)

## Contract

Strict-out umbrella: see team contract `flowcoro-public-app-surface` (尺规).
Advanced includes (`http_*.h`, `channel.h`) are wide-in; umbrella remains the stable app include.

## Agent scheduler

Tool-fanout under an AI agent loop (not an agent itself): [`AGENT_SCHED.md`](AGENT_SCHED.md).
