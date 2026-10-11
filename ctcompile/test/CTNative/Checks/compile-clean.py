#!/usr/bin/env python3
"""Check that generated C++ compiles with no output on every compiler.

One EmitC module is printed by the forked emitter and compiled (not just
parsed: -O2 code generation is where -Wmaybe-uninitialized and similar warnings
fire) by every compiler in --compilers with the harness FLAGS (-std=c++23 -O2
-pedantic -Wall -Wextra -Werror -Wconversion). The compile must succeed with no
output at all: a warning on generated code is a ctcompile bug, because
ctcompile, not the C++ compiler, is meant to report problems in the program.
Every definition in the file (each function, class and global) must also sit
under a provenance comment naming its JavaScript site, so a later diagnostic
can be mapped back to the source.

--mutate inserts one unused variable at the top of main; every compiler must
then refuse the file naming it, which proves -Wall -Werror reaches the compile.
That run exits 0 only when every compiler refused; its report line is what the
RUN line checks.
"""

import argparse
import re
import sys
from pathlib import Path

from CTNative.harness import FLAGS, run, slotted_run

DEFINITION = re.compile(
    r"^[A-Za-z_][A-Za-z_0-9:<>]* [A-Za-z_][A-Za-z_0-9]*\(.*\) \{$"
    r"|^(class|struct) [A-Za-z_][A-Za-z_0-9]* \{$"
    r"|^static [A-Za-z_][A-Za-z_0-9:<>]* [A-Za-z_][A-Za-z_0-9]* = "
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--module", required=True)
    parser.add_argument("--compilers", required=True, help="comma-separated; all must pass")
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--mutate", action="store_true")
    args = parser.parse_args()

    def fail(message):
        sys.exit(f"compile-clean ({args.name}): {message}")

    work = args.work / f"compile-clean-{args.name}"
    work.mkdir(parents=True, exist_ok=True)
    text = run([args.translate, "--mlir-to-cpp", args.module]).stdout
    if args.mutate:
        mutated = re.sub(r"(int32_t main\(\) \{\n)", r"\1  double ctcompile_mutant = 1.0;\n", text)
        if mutated == text:
            fail("the mutation found no main to land in")
        text = mutated
    unit = work / "unit.cpp"
    unit.write_text(text)

    # Every definition must sit directly under a provenance comment. Look back
    # two lines when the previous line is `template <...>` (the emitter makes
    # one class template per object-literal shape), and no further: a
    # definition that has drifted further from its comment is what this check
    # exists to catch.
    definitions = 0
    previous = before_previous = ""
    for line in text.split("\n"):
        if DEFINITION.match(line):
            comment = before_previous if previous.startswith("template <") else previous
            if not comment.startswith("// ctcompile: "):
                fail(f"a definition without a provenance comment above it:\n  {comment}\n  {line}")
            definitions += 1
        before_previous, previous = previous, line
    if definitions == 0:
        fail("no definition found in the emitted file; the gate would be vacuous")

    # Every compiler does a real compile, and none may print anything.
    compilers = args.compilers.split(",")
    if len(compilers) < 2:
        fail(
            f"part 24 Phase 63 Step 7 wants two toolchains; got {len(compilers)} ({args.compilers})"
        )
    report = ""
    for i, cxx in enumerate(compilers):
        version = run([cxx, "--version"]).stdout.split("\n", 1)[0]
        result = slotted_run(
            [cxx, *FLAGS, "-c", "-o", str(work / f"unit.{i}.o"), str(unit)],
            capture_output=True,
            text=True,
        )
        said = result.stdout + result.stderr
        if args.mutate:
            if result.returncode == 0:
                fail(f"{version} accepted the generated file with an unused mutant")
            if "ctcompile_mutant" not in said:
                fail(f"{version} refused the mutant for an unrelated reason:\n{said}")
        elif result.returncode != 0:
            fail(f"{version} refused the generated file:\n{said}")
        elif said:
            fail(
                f"{version} compiled the file but said something, and a warning on generated "
                f"code is a ctcompile bug:\n{said}"
            )
        report += f"{version}; "
    if args.mutate:
        print(
            f"compile-clean ({args.name}): refused the generated file on all {len(compilers)} "
            f"compilers: {report}"
        )
        return
    print(
        f"compile-clean ({args.name}): {definitions} definitions, each under a provenance "
        f"comment; clean at -O2 -Wall -Wextra -Wconversion -Werror on {report}"
    )


if __name__ == "__main__":
    main()
