"""Run a script's pytest checks without pytest.

A tool with a self-test keeps its checks in test_<script>.py beside it, as plain
`test_*` functions that pytest collects (`uv run --with pytest pytest <file>`).
The tool's self-test flag calls run(), which calls the same functions on the
standard library alone, so the flag still works on a Python without pytest,
such as the interpreter CTest finds.
"""

import importlib.util
from pathlib import Path
import sys
import traceback


def load(path, name=None):
    """Import the Python file at `path` as a module; works for hyphenated names."""
    path = Path(path)
    name = name or path.stem.replace("-", "_")
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def run(script, *args):
    """Call every test_* function in test_<script>.py with `args`.

    Prints one line per check and a total; returns 0 if every check passed and
    1 otherwise, for use as an exit status.
    """
    script = Path(script)
    tests = load(script.with_name(f"test_{script.stem.replace('-', '_')}.py"))
    cases = [
        (name, f) for name, f in vars(tests).items() if name.startswith("test_") and callable(f)
    ]
    failed = 0
    for name, case in cases:
        try:
            case(*args)
        except Exception:
            failed += 1
            print(f"FAIL {name}")
            traceback.print_exc(file=sys.stdout)
        else:
            print(f"ok   {name}")
    print(f"{script.name} self-test: {len(cases) - failed}/{len(cases)} checks passed")
    return 1 if failed else 0
