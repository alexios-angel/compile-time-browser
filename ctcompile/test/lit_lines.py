"""Run a lit case's RUN lines under pytest, in order, as lit runs them.

The cases.py modules (Analysis/Escape, Analysis/Types, Target/Cpp) hold lit
cases whose Python step needs what earlier RUN lines make (split-file
sections, a recording, a concatenated corpus, emitted C++), or whose later RUN
lines check what that step wrote. So a test there copies all of its case's
RUN lines and runs them through run() below.
"""

from pathlib import Path
import re
import subprocess

# A Python driver's RUN line: `python3 SCRIPT ARGS`, with no shell syntax.
DRIVER = re.compile(r"(?:python3|%python)\s+(\S+\.py)\s+([^|<>;&]*)")


def run(lit, case, lines, mains):
    """Run `lines`, the RUN lines of the lit case file `case` copied verbatim,
    in order, and stop at the first that fails.

    %s and %S name `case` and its folder. A driver line whose script is a key
    of `mains` (resolved paths) calls that main with the line's arguments in
    this process. Every other line runs under bash with pipefail, as lit's
    external shell runs it: each tool name replaced by its path where lit's
    tool substitutions (lit.llvm.subst.ToolSubst) would replace it, then the
    %-substitutions expanded.
    """
    lit.substitutions.update({"%s": str(case), "%S": str(case.parent)})
    names = "|".join(re.escape(name) for name in sorted(lit.tools, key=len, reverse=True))
    tool = re.compile(rf"(?<![.\-^/\\<])\b({names})\b(?![-.])")
    for line in lines:
        driver = DRIVER.fullmatch(line)
        main = driver and mains.get(Path(lit.expand(driver[1])).resolve())
        if main:
            main(lit.args(driver[2]))
            continue
        command = lit.expand(tool.sub(lambda match: lit.tools[match[1]], line))
        done = subprocess.run(
            ["bash", "-c", "set -o pipefail; " + command],
            cwd=Path(lit.substitutions["%t"]).parent,
            capture_output=True,
            text=True,
        )
        if done.returncode:
            raise AssertionError(
                f"RUN line failed with exit status {done.returncode}: {line}\n"
                f"$ {command}\n{done.stdout}{done.stderr}"
            )
