"""Shared helpers for the Target/Cpp Python drivers.

Provides the two C++ compilers, the compile flags for generated programs, a
checked subprocess runner and a brace-matched function body extractor.

Both compilers are required, not whichever is found: a check that silently
skipped one would pass without testing it (test/cmake/Native.cmake does the
same for the CMake-driven tests).
"""

import contextlib
import fcntl
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
from types import SimpleNamespace

# Include directories for generated programs: ctcompile/include for
# ctcompile/CTNative/Runtime/ctnative.hpp, and ctbrowser/include for the core
# headers that runtime includes.
CORE_INCLUDE = "-I" + str(Path(__file__).resolve().parents[4] / "ctbrowser/include")
RUNTIME_INCLUDE = "-I" + str(Path(__file__).resolve().parents[3] / "include")

FLAGS = [
    "-std=c++23",
    RUNTIME_INCLUDE,
    CORE_INCLUDE,
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-pedantic",
    "-Wconversion",
    "-ffp-contract=off",
]


# The drivers run their cases on os.cpu_count() threads, and lit runs one
# driver per CPU too, so on its own a lit run on a 32-core box started about
# 32 x 32 tools at once and compiles hit the 120 s timeout. A tool runs only
# while it holds one of os.cpu_count() slot files here, locked with flock, so
# the limit holds across every lit worker and driver on the machine, and a
# killed process gives its slot back.
#
# The directory is /tmp itself, not tempfile.gettempdir(): lit points TMPDIR at
# a directory of its own for each run, which would give every lit run (each
# checkout, each agent) a separate set of slots.
JOB_SLOTS = Path("/tmp/ctcompile-job-slots")


@contextlib.contextmanager
def job_slot():
    """Hold one machine-wide tool slot (JOB_SLOTS) for the body: the first free
    one from a random start, else wait for that start slot."""
    slots = os.cpu_count() or 1
    JOB_SLOTS.mkdir(exist_ok=True)
    start = random.randrange(slots)
    for attempt in range(slots + 1):
        fd = os.open(JOB_SLOTS / str((start + attempt) % slots), os.O_RDWR | os.O_CREAT, 0o666)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | (0 if attempt == slots else fcntl.LOCK_NB))
        except BlockingIOError:
            os.close(fd)
            continue
        try:
            yield
        finally:
            os.close(fd)
        return


def native_link_command(command, build=None):
    """Link native test programs with public Core unless explicitly Core-free."""
    command = [os.fspath(arg) for arg in command]
    if not command or not re.fullmatch(
        r"(?:g\+\+|clang\+\+|c\+\+)(?:-\d+)?", Path(command[0]).name
    ):
        return command
    core_free = "--core-free" in command
    command = [arg for arg in command if arg != "--core-free"]
    if core_free or any(arg in command for arg in ("-c", "-S", "-E", "-fsyntax-only", "--version")):
        return command
    if any("ctbrowser-core" in arg or arg == "-nostdlib" for arg in command[1:]):
        return command
    build = build or os.environ.get("CTCOMPILE_BUILD")
    if not build or not any(arg.endswith((".cpp", ".cc", ".cxx", ".o")) for arg in command[1:]):
        return command
    from CTNative.Browser.dom.dom import link_options

    _, libraries = link_options(
        SimpleNamespace(build=Path(build), include=Path(CORE_INCLUDE[2:])), core_only=True
    )
    return [*command, *libraries]


def slotted_run(command, *args, **kwargs):
    """subprocess.run inside a job_slot(), with the native test link contract."""
    with job_slot():
        return subprocess.run(native_link_command(command), *args, **kwargs)


def find_compilers():
    """Return paths to a GCC and a Clang C++ compiler, or raise if either is missing."""
    found = []
    for candidates in [("g++-13", "g++"), ("clang++-18", "clang++")]:
        compiler = next((shutil.which(name) for name in candidates if shutil.which(name)), None)
        if not compiler:
            raise RuntimeError("Target/Cpp drivers require " + " or ".join(candidates))
        found.append(compiler)
    return found


def run(command, *, failure_site=None):
    """Run and return stdout; with failure_site, require a failure naming that site."""
    result = slotted_run(command, capture_output=True, text=True, timeout=120)
    diagnostic = result.stdout + result.stderr
    if failure_site is not None:
        if result.returncode == 0 or failure_site not in diagnostic:
            raise RuntimeError(f"expected a failure at {failure_site}: {command!r}\n{diagnostic}")
    elif result.returncode:
        raise RuntimeError(f"failed: {command!r}\n{diagnostic}")
    return result.stdout


def function_body(text, name):
    """Return the text between the braces of C++ function `name`'s definition."""
    match = re.search(r"\b" + re.escape(name) + r"\([^;{}]*\)\s*\{", text)
    if not match:
        raise RuntimeError(f"missing definition of {name}\n{text}")
    start, depth = match.end(), 1
    for at in range(start, len(text)):
        depth += (text[at] == "{") - (text[at] == "}")
        if depth == 0:
            return text[start:at]
    raise RuntimeError(f"unterminated definition of {name}")
