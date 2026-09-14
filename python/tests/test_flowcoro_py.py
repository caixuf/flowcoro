"""flowcoro_py 冒烟/契约测试。

覆盖点（每条都对应绑定里一个容易写错的约束，改动绑定前先看这里）：
  · when_all 保序，且所有任务都会被等到完成（不 fail-fast 半途丢结果）；
  · 任务异常原样重抛并保留 traceback（能指回任务内部的源码行）；
  · GIL 真的被放开了（否则 time.sleep 批次不可能并行）；
  · close() 会先把队列里的任务跑完，之后拒绝新提交（不静默丢任务）；
  · pin_to_cores 让每个 worker 独占一个物理核（SMT 兄弟不重合）；
  · Channel 的容量硬上界 / FIFO / 空与 close 语义；
  · 进程不显式 close 也要能干净退出（Py_AtExit 兜底）。
"""

import os
import subprocess
import sys
import textwrap
import threading
import time

import pytest

import flowcoro_py as fc


# ---------------------------------------------------------------------------
# 基础
# ---------------------------------------------------------------------------


def test_module_surface():
    assert isinstance(fc.__version__, str) and fc.__version__
    assert callable(fc.when_all)
    assert callable(fc.wait_any)
    assert callable(fc.shutdown)
    assert callable(fc.physical_core_cpus)
    assert fc.Channel is not None
    assert fc.CoroutineThreadPool is not None


def test_physical_core_cpus_looks_sane():
    cores = fc.physical_core_cpus()
    assert cores, "should detect at least one physical core"
    assert cores == sorted(cores)
    assert len(set(cores)) == len(cores), "SMT siblings must not repeat"
    assert all(c >= 0 for c in cores)


# ---------------------------------------------------------------------------
# submit / when_all
# ---------------------------------------------------------------------------


def test_submit_returns_result_and_forwards_args():
    with fc.CoroutineThreadPool(threads=2) as pool:
        assert pool.submit(lambda: 7).result() == 7
        assert pool.submit(lambda a, b, c=0: a + b + c, 1, 2, c=3).result() == 6
        assert pool.submit(lambda: None).result() is None
        assert pool.submit(lambda: "x").result() == "x"


def test_when_all_preserves_input_order():
    """乱序完成的 64 个任务，结果必须严格按输入顺序返回。"""
    with fc.CoroutineThreadPool(threads=8) as pool:
        futures = [
            pool.submit(lambda i=i: (time.sleep((64 - i) * 0.002), i)[1])
            for i in range(64)
        ]
        assert fc.when_all(futures) == list(range(64))


def test_when_all_accepts_any_iterable():
    with fc.CoroutineThreadPool(threads=2) as pool:
        assert fc.when_all(pool.submit(lambda i=i: i) for i in range(4)) == [0, 1, 2, 3]
        assert fc.when_all([]) == []


def test_when_all_waits_for_every_task_even_if_one_fails():
    """失败不 fail-fast：其余任务必须都已经跑完（这里用副作用计数验证）。"""
    done = []
    lock = threading.Lock()

    def record(i):
        time.sleep(0.05)
        with lock:
            done.append(i)
        if i == 1:
            raise ValueError("boom")
        return i

    with fc.CoroutineThreadPool(threads=4) as pool:
        futures = [pool.submit(record, i) for i in range(4)]
        with pytest.raises(ValueError):
            fc.when_all(futures)
        assert sorted(done) == [0, 1, 2, 3]


def test_exception_traceback_points_into_task_body():
    line_holder = {}

    def failing_task():
        line_holder["line"] = failing_task.__code__.co_firstlineno  # 函数首行
        raise KeyError("missing-key")

    with fc.CoroutineThreadPool(threads=1) as pool:
        future = pool.submit(failing_task)
        with pytest.raises(KeyError) as excinfo:
            future.result()

    traceback = excinfo.value.__traceback__
    assert traceback is not None, "traceback must be preserved"
    frames = []
    while traceback is not None:
        frames.append((traceback.tb_frame.f_code.co_filename, traceback.tb_lineno))
        traceback = traceback.tb_next
    assert any(
        "test_flowcoro_py" in filename for filename, _ in frames
    ), f"traceback should include the task body frame, got {frames}"


def test_future_done_result_and_repeatable_result():
    with fc.CoroutineThreadPool(threads=1) as pool:
        future = pool.submit(lambda: 41 + 1)
        assert future.result(timeout=5) == 42
        # 重复取值是允许的（与 concurrent.futures 一致）
        assert future.result() == 42
        assert future.done() is True
        assert "Future" in repr(future)


def test_future_timeout_raises_timeout_error():
    with fc.CoroutineThreadPool(threads=1) as pool:
        future = pool.submit(lambda: time.sleep(1.5))
        with pytest.raises(TimeoutError):
            future.result(timeout=0.05)
        assert future.done() is False
        future.result(timeout=10)  # 之后仍可正常拿到
        assert future.done() is True


def test_wait_any_returns_first_index_then_none_on_timeout():
    with fc.CoroutineThreadPool(threads=4) as pool:
        slow = [pool.submit(lambda: time.sleep(0.5)) for _ in range(3)]
        assert fc.wait_any(slow, timeout=0.05) is None
        fast = pool.submit(lambda: time.sleep(0.02))
        assert fc.wait_any(slow + [fast], timeout=5) == 3
        fc.when_all(slow)


def test_wait_any_requires_same_pool_and_non_empty():
    with fc.CoroutineThreadPool(threads=1) as pool_a, fc.CoroutineThreadPool(
        threads=1
    ) as pool_b:
        with pytest.raises(ValueError):
            fc.wait_any([])
        with pytest.raises(ValueError):
            fc.wait_any([pool_a.submit(lambda: 1), pool_b.submit(lambda: 2)], timeout=1)


def test_gil_is_released_so_tasks_run_in_parallel():
    """8 个 0.3s 的 sleep 任务：串行要 2.4s，并行应远小于它。"""
    with fc.CoroutineThreadPool(threads=8) as pool:
        start = time.monotonic()
        fc.when_all([pool.submit(time.sleep, 0.3) for _ in range(8)])
        elapsed = time.monotonic() - start
    assert elapsed < 1.2, f"tasks did not run in parallel (elapsed={elapsed:.2f}s)"


# ---------------------------------------------------------------------------
# 关池语义
# ---------------------------------------------------------------------------


def test_close_drains_pending_tasks_and_rejects_new_submit():
    """close() 不会丢任务：队列里剩余任务会被跑完，之后 submit 报错。"""
    pool = fc.CoroutineThreadPool(threads=2)
    futures = [pool.submit(lambda i=i: (time.sleep(0.05), i)[1]) for i in range(6)]
    assert pool.closed is False
    pool.close()
    assert pool.closed is True
    assert fc.when_all(futures) == list(range(6)), "close() must not drop pending tasks"

    with pytest.raises(RuntimeError):
        pool.submit(lambda: 1)
    pool.close()  # 幂等
    assert pool.closed is True


def test_context_manager_closes_pool():
    with fc.CoroutineThreadPool(threads=1) as pool:
        future = pool.submit(lambda: 1)
    assert pool.closed is True
    assert future.result() == 1


def test_close_does_not_deadlock_while_task_still_running():
    """任务还在 sleep，立刻 close()：必须能 join 完成，不能和 GIL 互等。"""
    pool = fc.CoroutineThreadPool(threads=2)
    pool.submit(lambda: time.sleep(0.4))
    start = time.monotonic()
    pool.close()
    assert time.monotonic() - start < 5


# ---------------------------------------------------------------------------
# 绑核
# ---------------------------------------------------------------------------


def test_pin_to_cores_gives_each_worker_a_distinct_physical_core():
    cores = fc.physical_core_cpus()
    if len(cores) < 2:
        pytest.skip("needs at least 2 physical cores")
    count = min(4, len(cores))

    # 池是单条 MPMC 队列 + N 个 worker，任务落到哪个 worker 不确定。
    # 用屏障让 N 个任务同时被 N 个 worker 各持一个，采样才覆盖全部 worker。
    barrier = threading.Barrier(count, timeout=20)

    def record_affinity():
        affinity = sorted(os.sched_getaffinity(0))
        barrier.wait()
        return affinity

    with fc.CoroutineThreadPool(threads=count, pin_to_cores=True) as pool:
        assert pool.cpus == cores
        assert pool.threads == count
        observed = fc.when_all([pool.submit(record_affinity) for _ in range(count)])

    picked = [entry[0] for entry in observed]
    assert all(len(entry) == 1 for entry in observed), observed
    assert all(cpu in cores for cpu in picked), picked
    assert len(set(picked)) == count, f"workers must not share a core: {picked}"


def test_explicit_cpus_and_no_pinning():
    cores = fc.physical_core_cpus()
    with fc.CoroutineThreadPool(threads=1, cpus=cores[:1]) as pool:
        assert pool.cpus == cores[:1]
        observed = pool.submit(lambda: sorted(os.sched_getaffinity(0))).result()
    assert observed == [cores[0]]

    with fc.CoroutineThreadPool(threads=2) as pool:
        assert pool.cpus == []
        multi = pool.submit(lambda: len(os.sched_getaffinity(0))).result()
    assert multi > 1, "without pin_to_cores the worker should keep the full mask"


def test_default_threads_and_validation():
    with fc.CoroutineThreadPool(threads=3) as pool:
        assert pool.threads == 3
    with pytest.raises(ValueError):
        fc.CoroutineThreadPool(threads=0)
    with pytest.raises(ValueError):
        fc.CoroutineThreadPool(cpus=[])
    assert "CoroutineThreadPool" in repr(fc.CoroutineThreadPool(threads=1))


# ---------------------------------------------------------------------------
# Channel
# ---------------------------------------------------------------------------


def test_channel_capacity_is_rounded_up_and_bounded():
    channel = fc.Channel(capacity=3)
    assert channel.capacity == 4  # 向上取 2 的幂
    assert channel.size == 0
    assert len(channel) == 0
    with pytest.raises(ValueError):
        fc.Channel(capacity=0)

    for i in range(4):
        assert channel.try_push(bytes([i])) is True
    assert channel.size <= channel.capacity
    assert channel.try_push(b"overflow") is False, "must be a hard bound"
    assert channel.size == 4


def test_channel_bytes_roundtrip_fifo_and_empty():
    channel = fc.Channel(capacity=8)
    assert channel.try_pop() is None, "empty pop must return None"

    payloads = [b"alpha", b"beta\x00gamma", b"", bytes(range(256))]
    for payload in payloads:
        assert channel.try_push(payload) is True
    for payload in payloads:
        got = channel.try_pop()
        assert got == payload, f"FIFO violated: {payload!r} != {got!r}"
    assert channel.try_pop() is None


def test_channel_accepts_bytes_like_and_rejects_str():
    channel = fc.Channel(capacity=4)
    assert channel.try_push(bytearray(b"ba")) is True
    assert channel.try_push(memoryview(b"mv")) is True
    assert channel.try_pop() == b"ba"
    assert channel.try_pop() == b"mv"
    with pytest.raises(TypeError):
        channel.try_push("text")
    with pytest.raises(TypeError):
        channel.try_push(123)


def test_channel_close_semantics():
    channel = fc.Channel(capacity=4)
    assert channel.try_push(b"a") is True
    channel.close()
    assert channel.closed is True
    assert channel.try_push(b"b") is False
    assert channel.try_pop() == b"a", "already-queued items stay drainable"
    assert channel.try_pop() is None
    channel.close()  # 幂等
    assert "Channel" in repr(channel)


def test_channel_no_loss_or_duplication_under_python_threads():
    channel = fc.Channel(capacity=64)
    producers, consumers, per_producer = 4, 4, 5000
    total = producers * per_producer

    def produce(base):
        for k in range(per_producer):
            value = base * per_producer + k + 1
            while not channel.try_push(value.to_bytes(4, "big")):
                time.sleep(0)
        return None

    got = []
    got_lock = threading.Lock()
    stop = threading.Event()

    def consume():
        while not stop.is_set() or channel.size > 0:
            item = channel.try_pop()
            if item is None:
                time.sleep(0)
                continue
            with got_lock:
                got.append(int.from_bytes(item, "big"))

    consumers_threads = [threading.Thread(target=consume) for _ in range(consumers)]
    for thread in consumers_threads:
        thread.start()
    producer_threads = [
        threading.Thread(target=produce, args=(p,)) for p in range(producers)
    ]
    for thread in producer_threads:
        thread.start()
    for thread in producer_threads:
        thread.join()
    deadline = time.monotonic() + 30
    while len(got) < total and time.monotonic() < deadline:
        time.sleep(0.01)
    stop.set()
    for thread in consumers_threads:
        thread.join()

    assert sorted(got) == list(range(1, total + 1)), "items lost or duplicated"
    assert channel.size == 0


# ---------------------------------------------------------------------------
# 退出与全局
# ---------------------------------------------------------------------------


def test_process_exits_cleanly_without_explicit_close():
    """建池 + 提交长任务 + 不 close 直接退出：必须退出码 0，不能挂住/崩。"""
    script = textwrap.dedent(
        """
        import time
        import flowcoro_py as fc

        pool = fc.CoroutineThreadPool(threads=2, pin_to_cores=False)
        pool.submit(lambda: time.sleep(0.3))
        pool.submit(lambda: time.sleep(0.3))
        # 故意不 close，也不 wait
        """
    )
    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join(
        [str(_built_module_dir()), env.get("PYTHONPATH", "")]
    ).strip(os.pathsep)
    result = subprocess.run(
        [sys.executable, "-c", script], capture_output=True, text=True, env=env, timeout=60
    )
    assert result.returncode == 0, f"stderr={result.stderr}\nstdout={result.stdout}"
    assert "traceback" not in result.stderr.lower()


def test_shutdown_is_idempotent():
    fc.shutdown()
    fc.shutdown()


def _built_module_dir():
    import pathlib

    root = pathlib.Path(__file__).resolve().parents[2]
    for candidate in sorted(root.glob("build*/python")):
        if list(candidate.glob("flowcoro_py*.so")) or list(
            candidate.glob("flowcoro_py*.pyd")
        ):
            return candidate
    raise AssertionError("flowcoro_py module not found; build with FLOWCORO_BUILD_PYTHON=ON")
