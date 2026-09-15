# Autonomous driving examples

Two demos, two schedulers. They are not interchangeable.

## `rt_control_loop_demo` — `flowcoro::rt` contract

Single-thread `RtExecutor`, `sleep_until` aligned 50 Hz control, 30 Hz camera /
10 Hz LiDAR as latest-sample atomics. No DDS, no `Task<>` pool, no ROS.

This is the shape a FlowEngine-style control loop should use: host calls `run()`,
waits on `next_timer_deadline()`, sensors never `resume()` on another thread.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target rt_control_loop_demo
./build/examples/autonomous_driving/rt_control_loop_demo 2      # seconds
./build/examples/autonomous_driving/rt_control_loop_demo 2 2    # pin_cpu=2 (Linux)
```

It prints p50 / p99 / max tick interval and deadline tardiness for **this run**.
That is a measurement, not a certified hard-realtime claim.

CI-integrated budgets live in `tests/test_rt_latency.cpp`:

```bash
ctest --test-dir build -R test_rt_latency --output-on-failure
FLOWCORO_RT_SLO_STRICT=1 ctest --test-dir build -R test_rt_latency --output-on-failure
```

## `ad_pipeline_demo` — Task + in-process DDS

Multi-thread `Task<>` / `sync_wait` pipeline with `flowcoro::dds` pub/sub and a
mock map RPC timeout path. Useful for middleware-shaped code; it does **not**
run on `RtExecutor` and does not assert tick jitter.

```bash
cmake --build build --target ad_pipeline_demo
./build/examples/autonomous_driving/ad_pipeline_demo 3
```
