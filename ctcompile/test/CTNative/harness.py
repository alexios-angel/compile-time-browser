"""What the CTNative python drivers share: a checked subprocess run and the two
compilers (the same pair Target/Cpp/harness.py finds).

lit puts ctcompile/test on PYTHONPATH (lit.cfg.py), which is also what lets a
driver import a sibling from another directory as, say,
`from CTNative.HostContract import contract`. A hand run needs the same
PYTHONPATH. Node and the interpreter reference are not discovered here: every
RUN line passes `--node %node --reference %native_reference`, both configured
by CMake (lit.site.cfg.py.in).
"""

from pathlib import Path
import subprocess

from Target.Cpp.harness import (
    CORE_INCLUDE,
    RUNTIME_INCLUDE,
    find_compilers,
)  # noqa: F401  (re-exported)

RUNTIME_HEADER = (
    Path(__file__).resolve().parents[2] / "include/ctcompile/CTNative/Runtime/ctnative.hpp"
)
RUNTIME_INCLUDE_LINE = '#include "ctcompile/CTNative/Runtime/ctnative.hpp"'


def inline_runtime(cpp):
    """The emitted C++ with the runtime header's text in place of its include.

    A driver that instruments a helper (the lifetime harnesses rewrite make_map
    to record a weak_ptr of every Map) edits text, and the text has to be in the
    file it edits. #pragma once goes, since the text is now the main file.
    """
    if cpp.count(RUNTIME_INCLUDE_LINE) != 1:
        raise RuntimeError("the emitted C++ does not include the runtime header exactly once")
    return cpp.replace(
        RUNTIME_INCLUDE_LINE, RUNTIME_HEADER.read_text().replace("#pragma once\n", "")
    )


def run(command, *, success=True, environment=None, input_text=None):
    result = subprocess.run(
        command, capture_output=True, text=True, timeout=120, env=environment, input=input_text
    )
    if (result.returncode == 0) != success:
        raise RuntimeError(
            f"unexpected tool status {result.returncode}: {command!r}\n{result.stdout}{result.stderr}"
        )
    return result
