"""pytest 目录引导：让 ``pytest python/tests`` 直接可用（不必先设 PYTHONPATH）。

优先使用已安装/已在 sys.path 上的 flowcoro_py；否则在仓库根下找
``build*/python/flowcoro_py*.so``（CMake 的 LIBRARY_OUTPUT_DIRECTORY）。
"""

import pathlib
import sys

try:  # pragma: no cover - 环境差异分支
    import flowcoro_py  # noqa: F401
except ImportError:
    _root = pathlib.Path(__file__).resolve().parents[2]
    for _candidate in sorted(_root.glob("build*/python")):
        if list(_candidate.glob("flowcoro_py*.so")) or list(
            _candidate.glob("flowcoro_py*.pyd")
        ):
            sys.path.insert(0, str(_candidate))
            break
