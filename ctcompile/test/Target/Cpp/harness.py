"""What every Target/Cpp python driver needs: the two compilers, the gate's flags, a
checked subprocess run and a brace-matched function body extractor.

Both compilers are required, not whichever is found: a gate that silently drops
one passes vacuously (test/cmake/Native.cmake says the same for the CMake half).
"""

from pathlib import Path
import re
import shutil
import subprocess

# THE RUNTIME HEADER'S INCLUDE DIRECTORY. Every generated program includes
# ctcompile/CTNative/Runtime/ctnative.hpp, so every compile of one needs
# ctcompile/include on its path - and nothing else of the compiler's.
RUNTIME_INCLUDE = "-I" + str(Path(__file__).resolve().parents[3] / "include")

FLAGS = [
    "-std=c++23",
    RUNTIME_INCLUDE,
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-pedantic",
    "-Wconversion",
    "-ffp-contract=off",
]


def find_compilers():
    found = []
    for candidates in [("g++-13", "g++"), ("clang++-18", "clang++")]:
        compiler = next((shutil.which(name) for name in candidates if shutil.which(name)), None)
        if not compiler:
            raise RuntimeError("Target/Cpp drivers require " + " or ".join(candidates))
        found.append(compiler)
    return found


def run(command, *, failure_site=None):
    """Run and return stdout; with failure_site, require a failure naming that site."""
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    diagnostic = result.stdout + result.stderr
    if failure_site is not None:
        if result.returncode == 0 or failure_site not in diagnostic:
            raise RuntimeError(f"expected a failure at {failure_site}: {command!r}\n{diagnostic}")
    elif result.returncode:
        raise RuntimeError(f"failed: {command!r}\n{diagnostic}")
    return result.stdout


def function_body(text, name):
    match = re.search(r"\b" + re.escape(name) + r"\([^;{}]*\)\s*\{", text)
    if not match:
        raise RuntimeError(f"missing definition of {name}\n{text}")
    start, depth = match.end(), 1
    for at in range(start, len(text)):
        depth += (text[at] == "{") - (text[at] == "}")
        if depth == 0:
            return text[start:at]
    raise RuntimeError(f"unterminated definition of {name}")
