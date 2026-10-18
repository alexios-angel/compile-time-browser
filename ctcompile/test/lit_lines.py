"""Run a lit case's RUN lines under pytest, in order, as lit runs them.

The cases.py modules (in Analysis, CTJS, CTNative, Packaging and Target)
hold lit cases whose Python step needs what earlier RUN lines make (split-file
sections, a recording, a concatenated corpus, emitted C++), or whose later RUN
lines check what that step wrote. So a test there copies all of its case's
RUN lines and runs them through run() below.
"""

import os
from pathlib import Path
import re
import subprocess

# A Python driver's RUN line: `python3 SCRIPT ARGS`, with no shell syntax.
DRIVER = re.compile(r"(?:python3|%python)\s+(\S+\.py)\s+([^|<>;&]*)")
# The substitutions lit.cfg.py inserts ahead of its tool substitutions. Their
# text names tools (ctjs-translate, ctcompile-test-type-oracle), which lit then
# replaces with their paths too.
FIRST = ("%dom_driver", "%print_deduced", "%compile_clean", "%compilation_unit")


def run_lines(path):
    """The RUN lines of the lit case file `path`, with backslash continuations
    joined as lit joins them."""
    lines = []
    for line in re.findall(r"RUN:(.*)", path.read_text()):
        if lines and lines[-1].endswith("\\"):
            lines[-1] = lines[-1][:-1] + line.rstrip()
        else:
            lines.append(line.rstrip())
    return [line.strip() for line in lines]


def run(lit, case, lines, mains):
    """Run `lines`, the RUN lines of the lit case file `case` copied verbatim,
    in order, and stop at the first that fails. While `case` still has RUN
    lines of its own, they must equal `lines`, so that lit and pytest run the
    same lines.

    %s and %S name `case` and its folder. A driver line whose script is a key
    of `mains` (resolved paths) calls that main with the line's arguments in
    this process. Every other line runs under bash with pipefail, as lit's
    external shell runs it, with lit.cfg.py's substitutions in lit's order:
    first FIRST; then each tool name replaced by its path where lit's tool
    substitutions (lit.llvm.subst.ToolSubst) would replace it, `count` only
    after "| " as in lit's default substitution; then the other
    %-substitutions.
    """
    if run_lines(case) not in ([], list(lines)):
        raise AssertionError(f"{case}: its RUN lines differ from the test's copy; edit both")
    lit.substitutions.update({"%s": str(case), "%S": str(case.parent)})
    names = "|".join(
        re.escape(name) for name in sorted(lit.tools, key=len, reverse=True) if name != "count"
    )
    tool = re.compile(rf"(?<![.\-^/\\<])\b({names})\b(?![-.])|(?<=\| )\bcount\b")
    for line in lines:
        driver = DRIVER.fullmatch(line)
        main = driver and mains.get(Path(lit.expand(driver[1])).resolve())
        if main:
            main(lit.args(driver[2]))
            continue
        for key in FIRST:
            line = line.replace(key, lit.substitutions[key])
        command = lit.expand(tool.sub(lambda match: lit.tools[match[0]], line))
        # lit runs its commands without LANG or LC_*, in the C locale, where
        # GCC quotes names in ASCII; under a UTF-8 locale it uses typographic
        # quotes, and a diagnostic would differ between the runners.
        done = subprocess.run(
            ["bash", "-c", "set -o pipefail; " + command],
            cwd=Path(lit.substitutions["%t"]).parent,
            env=dict(os.environ, LC_ALL="C"),
            capture_output=True,
            text=True,
        )
        if done.returncode:
            raise AssertionError(
                f"RUN line failed with exit status {done.returncode}: {line}\n"
                f"$ {command}\n{done.stdout}{done.stderr}"
            )
