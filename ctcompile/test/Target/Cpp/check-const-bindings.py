#!/usr/bin/env python3
"""Execute binding/alias cases and require exact const pins under both compilers."""

import argparse
from pathlib import Path
import shutil
import subprocess


MAIN = """
int main() {
    if (scalar(5.0) != 7.0 || scalar(-2.0) != 0.0) { return 1; }
    if (safe_call(3) != 7) { return 2; }
    if (unknown_overload(5) != 15 || unknown_overload(-5) != 5) { return 3; }
    if (unknown_reference() != 10) { return 4; }
    if (unknown_verbatim(4) != 9) { return 5; }
    if (mixed_contract(6) != 10) { return 6; }
    int32_t value = 7;
    if (pointer_mutation(&value) != 8 || value != 8) { return 7; }
    if (local_storage() != 10) { return 8; }
    int32_t left = 1, right = 2;
    if (pointer_choice(false, &left, &right) != 42 || left != 1 || right != 42) { return 9; }
    if (pointer_choice(true, &left, &right) != 42 || left != 42 || right != 42) { return 10; }
    if (!empty_initializer().empty()) { return 11; }
    if (loop_total(0) != 0 || loop_total(4) != 6 || loop_total(7) != 21) { return 12; }
    if (tuple_values() != 42) { return 13; }
    if (string_value("owned") != "owned") { return 14; }
    if (external_value(ExternalCounter{5}) != 6) { return 15; }
    if (inline_math(3.0) != 8.0 || inline_math(-1.0) != 0.0) { return 16; }
    if (captured_overload(5, 2) != 17 || captured_overload(-5, 4) != 9) { return 17; }
    return 0;
}
"""

ISOLATION_MAIN = """
int main() {
    return ordinary_before(1) + marked(2) + ordinary_nested(3) + marked_after(4)
        + wrong_marker(5) + names_only(6) + ordinary_after(7) == 28 ? 0 : 1;
}
"""


def run(command, *, failure_site=None):
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    diagnostic = result.stdout + result.stderr
    if failure_site is not None:
        if result.returncode == 0 or failure_site not in diagnostic:
            raise RuntimeError(f"expected a pin failure at {failure_site}: {command!r}\n{diagnostic}")
    elif result.returncode:
        raise RuntimeError(f"const binding test failed: {command!r}\n{diagnostic}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    compilers = []
    for candidates in [("g++-13", "g++"), ("clang++-18", "clang++")]:
        compiler = next((shutil.which(name) for name in candidates if shutil.which(name)), None)
        if not compiler:
            raise RuntimeError("const binding test requires " + " or ".join(candidates))
        compilers.append(compiler)
    flags = ["-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
             "-Wconversion", "-ffp-contract=off", "-I", str(args.fixtures.resolve())]
    emitted = {}
    for label, harness in [("bindings", MAIN), ("hoisted", MAIN), ("isolation", ISOLATION_MAIN)]:
        text = (args.fixtures / f"{label}.cpp").read_text()
        if not text.strip():
            raise RuntimeError(f"empty emitted {label}.cpp")
        emitted[label] = text
        source = args.work / f"{label}.cpp"
        source.write_text(text + harness)
        for index, compiler in enumerate(compilers):
            binary = (args.work / f"{label}-{index}").resolve()
            run([compiler, *flags, str(source), "-o", str(binary)])
            run([str(binary)])
            print(f"const bindings: {Path(compiler).name} preserved {label} behavior")

    # Mutation witnesses keep the declared value and change only the pin.
    # One rejects dropping binding const; the other rejects moving const from
    # the pointer binding to its pointee. Disabling pins restores the same run.
    mutations = [
        ("scalar-pin", '"const-bindings.js:2:1", double const);',
         '"const-bindings.js:2:1", double);', "const-bindings.js:2:1"),
        ("pointer-pin", '"const-bindings.js:3:1", int32_t* const);',
         '"const-bindings.js:3:1", int32_t const*);', "const-bindings.js:3:1"),
    ]
    for label, before, after, site in mutations:
        if emitted["bindings"].count(before) != 1:
            raise RuntimeError(f"expected exactly one {label} mutation anchor: {before}")
        source = args.work / f"{label}.cpp"
        source.write_text(emitted["bindings"].replace(before, after, 1) + MAIN)
        for index, compiler in enumerate(compilers):
            run([compiler, *flags, "-fsyntax-only", str(source)], failure_site=site)
            binary = (args.work / f"{label}-disabled-{index}").resolve()
            run([compiler, *flags, "-DCTCOMPILE_NO_TYPE_PINS", str(source), "-o", str(binary)])
            run([str(binary)])
            print(f"const bindings: {Path(compiler).name} rejected {label}, no-pin run agreed")


if __name__ == "__main__":
    main()
