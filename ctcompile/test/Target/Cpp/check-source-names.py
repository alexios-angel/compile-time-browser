#!/usr/bin/env python3
"""Compile named EmitC output and exercise bindings with both native compilers.

The paired FileCheck assertions pin intended source-family names. These runtime
checks cover spelling choices whose exact sanitized suffix is not the contract:
function/global/type/macro shadowing, fallback collisions and nested scopes.
"""

import argparse
from pathlib import Path
import shutil
import subprocess


def run(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise SystemExit(
            "source names failed: " + " ".join(map(str, command)) + "\n"
            + result.stdout + result.stderr
        )
    return result.stdout


NAMED_MAIN = """
int main() {
    if (naming(10.0) != 23.0 || naming(-1.0) != 1.0) { return 1; }
    if (fresh(42.0) != 42.0 || pair_sum() != 42) { return 2; }
    if (fallback_collision() != 41) { return 3; }
    if (collisions(10) != 131 || collisions(0) != 91) { return 4; }
    if (deferred_values() != 25) { return 5; }
    if (statement_only() != 0 || g_touches != 1) { return 6; }
    return 0;
}
"""

LOOP_MAIN = """
int main() {
    if (loop_names(4) != 18) { return 1; }
    if (loop_names(0) != 0) { return 2; }
    if (loop_names(2) != 7) { return 3; }
    return 0;
}
"""

HEADER_MAIN = """
int main() {
    if (header_argument(5) != 23 || header_argument(-18) != 0) { return 1; }
    if (header_local() != 42) { return 2; }
    if (header_index(0) != 3 || header_index(39) != 42) { return 3; }
    return 0;
}
"""

OPAQUE_MAIN = """
int main() {
    if (opaque_argument(17) != 17 || opaque_argument(-4) != -4) { return 1; }
    if (opaque_local() != 42) { return 2; }
    return 0;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    compilers = []
    for candidates in [("g++-13", "g++"), ("clang++-18", "clang++")]:
        compiler = next((shutil.which(name) for name in candidates if shutil.which(name)), None)
        if not compiler:
            raise SystemExit("source name test requires " + " or ".join(candidates))
        compilers.append(compiler)
    args.work.mkdir(parents=True, exist_ok=True)
    flags = ["-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
             "-Wconversion", "-ffp-contract=off"]
    for name, harness in [("named", NAMED_MAIN), ("loops", LOOP_MAIN),
                          ("loops-top", LOOP_MAIN), ("standard-headers", HEADER_MAIN),
                          ("opaque-types", OPAQUE_MAIN)]:
        emitted = (args.fixtures / f"{name}.cpp").read_text()
        if not emitted.strip():
            raise SystemExit(f"source name test received empty {name}.cpp")
        source = args.work / f"{name}.cpp"
        source.write_text(emitted + harness)
        for index, compiler in enumerate(compilers):
            executable = (args.work / f"{name}-{index}").resolve()
            run([compiler, *flags, "-I", str(args.fixtures.resolve()),
                 str(source), "-o", str(executable)])
            run([str(executable)])
            print(f"source names: {Path(compiler).name} preserved {name} bindings")


if __name__ == "__main__":
    main()
