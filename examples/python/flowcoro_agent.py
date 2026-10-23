"""Thin AgentScheduler façade over flowcoro_py (Knife B).

Prefer CancellationTokenSource per tool; inject cancel_token= when accepted.
Knife A cancel_event= Event bridge remains wide-in for one release.
Cooperative poll only — Pool ≠ Task; no Future preempt. See docs/AGENT_SCHED.md.
"""
from __future__ import annotations

import inspect
import threading
from dataclasses import dataclass, field
from typing import Any, Callable, Optional

import flowcoro_py as fc


def _accepts_kw(fn: Callable[..., Any], name: str) -> bool:
    try:
        inspect.signature(fn).bind_partial(**{name: None})
        return True
    except TypeError:
        return False


@dataclass
class ToolHandle:
    name: str
    future: Any
    source: Any  # CancellationTokenSource
    cancel_event: threading.Event
    _cancelled: bool = field(default=False, repr=False)

    def done(self) -> bool:
        return bool(self.future.done())

    def cancelled(self) -> bool:
        return self._cancelled or self.source.is_cancelled() or self.cancel_event.is_set()

    def result(self, timeout: Optional[float] = None) -> Any:
        return self.future.result(timeout=timeout)


class AgentScheduler:
    """Hermes-like underlying scheduler: submit / gather / wait_first / cancel."""

    def __init__(self, threads: Optional[int] = None, pin_to_cores: bool = False):
        self._pool = fc.CoroutineThreadPool(threads=threads, pin_to_cores=pin_to_cores)
        self._handles: list[ToolHandle] = []
        self._closed = False

    def submit_tool(
        self,
        name: str,
        fn: Callable[..., Any],
        *args: Any,
        **kwargs: Any,
    ) -> ToolHandle:
        if self._closed:
            raise RuntimeError("AgentScheduler is closed")
        source = fc.CancellationTokenSource()
        ev = threading.Event()
        inject = dict(kwargs)
        if _accepts_kw(fn, "cancel_token") and "cancel_token" not in inject:
            inject["cancel_token"] = source.token()
        if _accepts_kw(fn, "cancel_event") and "cancel_event" not in inject:
            inject["cancel_event"] = ev
        fut = self._pool.submit(fn, *args, **inject)
        h = ToolHandle(name=name, future=fut, source=source, cancel_event=ev)
        self._handles.append(h)
        return h

    def gather(self, handles: list[ToolHandle]) -> list[Any]:
        return list(fc.when_all([h.future for h in handles]))

    def wait_first(
        self, handles: list[ToolHandle], timeout: Optional[float] = None
    ) -> Optional[int]:
        return fc.wait_any([h.future for h in handles], timeout=timeout)

    def cancel(self, handle: ToolHandle) -> None:
        handle._cancelled = True
        handle.source.cancel()
        handle.cancel_event.set()

    def cancel_all(self) -> None:
        for h in self._handles:
            self.cancel(h)

    def close(self) -> None:
        self._closed = True
        self._pool.close()

    def __enter__(self) -> "AgentScheduler":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()
