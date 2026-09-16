#!/usr/bin/env python3
"""THE COMPILE-CLEAN GATE - ctcompile Phase 63 Step 7.

"The generated file compiles clean, and that is a test." One EmitC module goes
through the forked emitter, and the C++ is compiled - not just parsed: -O2
codegen is where -Wmaybe-uninitialized and friends fire - by EVERY compiler in
--compilers with -std=c++23 -O2 -pedantic -Wall -Wextra -Werror -Wconversion,
and the compile must succeed with NO output at all: a warning that is not an
error is still a ctcompile bug, because part 24 §2 makes ctcompile the
diagnostician. And every definition in the file - each function, class and
global - must sit under a provenance comment naming its JavaScript site, so the
day a diagnostic appears the mapping back already exists.

NEGATIVE PROOF, so the gate's teeth stay in the suite: --mutate inserts one
unused variable at the top of main, and every compiler must then refuse the
file naming it, or -Wall -Werror is not reaching the compile. The run exits 0
only when every compiler refused; its report line is what the RUN line checks.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

DEFINITION = re.compile(
    r"^[A-Za-z_][A-Za-z_0-9:<>]* [A-Za-z_][A-Za-z_0-9]*\(.*\) \{$"
    r"|^(class|struct) [A-Za-z_][A-Za-z_0-9]* \{$"
    r"|^static [A-Za-z_][A-Za-z_0-9:<>]* [A-Za-z_][A-Za-z_0-9]* = "
)
# The runtime header every native program includes lives in ctcompile/include.
CORE_INCLUDE = "-I" + str(Path(__file__).resolve().parents[4] / "ctbrowser/include")
RUNTIME_INCLUDE = "-I" + str(Path(__file__).resolve().parents[3] / "include")
FLAGS = [
    "-std=c++23",
    RUNTIME_INCLUDE,
    CORE_INCLUDE,
    "-O2",
    "-pedantic",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wconversion",
]


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
    emitted = subprocess.run(
        [args.translate, "--mlir-to-cpp", args.module], capture_output=True, text=True
    )
    if emitted.returncode != 0:
        fail(f"the emitter refused {args.module}:\n{emitted.stderr}")
    text = emitted.stdout
    if args.mutate:
        mutated = re.sub(r"(int32_t main\(\) \{\n)", r"\1  double ctcompile_mutant = 1.0;\n", text)
        if mutated == text:
            fail("the mutation found no main to land in")
        text = mutated
    unit = work / "unit.cpp"
    unit.write_text(text)

    # EVERY DEFINITION UNDER A PROVENANCE COMMENT: the line before each must be
    # the comment. TWO LINES OF LOOKBACK, BECAUSE A TEMPLATE PUTS ONE IN
    # BETWEEN (Phase 56C emits one class template per object-literal family),
    # and no more: a definition that has drifted further from its comment is
    # exactly what this gate is here to catch.
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

    # EVERY COMPILER, A REAL COMPILE, NO OUTPUT.
    compilers = args.compilers.split(",")
    if len(compilers) < 2:
        fail(
            f"part 24 Phase 63 Step 7 wants two toolchains; got {len(compilers)} ({args.compilers})"
        )
    report = ""
    for i, cxx in enumerate(compilers):
        version = subprocess.run([cxx, "--version"], capture_output=True, text=True).stdout
        version = version.split("\n", 1)[0]
        result = subprocess.run(
            [cxx, *FLAGS, "-ffp-contract=off", "-c", "-o", str(work / f"unit.{i}.o"), str(unit)],
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
