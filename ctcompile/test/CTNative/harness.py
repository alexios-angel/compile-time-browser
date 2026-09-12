"""What the CTNative python drivers share: a checked subprocess run and the two
compilers (the same pair Target/Cpp/harness.py finds).

lit puts ctcompile/test on PYTHONPATH (lit.cfg.py), which is also what lets a
driver import a sibling from another directory as, say,
`from CTNative.HostContract import contract`. A hand run needs the same
PYTHONPATH. Node and the interpreter reference are not discovered here: every
RUN line passes `--node %node --reference %native_reference`, both configured
by CMake (lit.site.cfg.py.in).
"""

import subprocess

from Target.Cpp.harness import find_compilers  # noqa: F401  (re-exported for the drivers)


def run(command, *, success=True, environment=None, input_text=None):
    result = subprocess.run(
        command, capture_output=True, text=True, timeout=120, env=environment, input=input_text
    )
    if (result.returncode == 0) != success:
        raise RuntimeError(
            f"unexpected tool status {result.returncode}: {command!r}\n{result.stdout}{result.stderr}"
        )
    return result
