#!/usr/bin/env python3
"""Check callable-body inlining, retained functions, forwarding and metadata."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess


MAIN = """
int main() {
    const auto first = ctn_bind_inline(40);
    const auto alias = first;
    if (first(1) != 42 || alias(1) != 42 || first(9) != 50 || direct_caller(40) != 42) { return 1; }
    const auto changing = ctn_bind_mutable(5);
    const auto separate = ctn_bind_mutable(100);
    if (changing() != 6 || changing() != 6 || separate() != 101 || mutable_target(5) != 6) { return 2; }
    const auto unknown = ctn_bind_unknown(40);
    if (unknown() != 42 || unknown() != 42) { return 3; }
    if (address_pointer()(20, 22) != 42 || ctn_bind_address(20)(22) != 42) { return 4; }
    if (literal_pointer()(42) != 42 || ctn_bind_literal(42)() != 42) { return 5; }
    return 0;
}
"""


def run(command):
    result = subprocess.run(command, text=True, capture_output=True, timeout=120)
    if result.returncode:
        raise RuntimeError(f"{command!r}\n{result.stdout}{result.stderr}")
    return result.stdout


def body(text, name):
    match = re.search(r"\b" + re.escape(name) + r"\([^;{}]*\)\s*\{", text)
    if not match:
        raise RuntimeError(f"missing definition of {name}\n{text}")
    start, depth = match.end(), 1
    for at in range(start, len(text)):
        depth += (text[at] == "{") - (text[at] == "}")
        if depth == 0:
            return text[start:at]
    raise RuntimeError(f"unterminated definition of {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--translate", required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    compilers = []
    for choices in [("g++-13", "g++"), ("clang++-18", "clang++")]:
        compiler = next((shutil.which(name) for name in choices if shutil.which(name)), None)
        if not compiler:
            raise RuntimeError("callable-body regression requires " + " or ".join(choices))
        compilers.append(compiler)
    for label in ["callables", "hoisted"]:
        cpp = (args.fixtures / f"{label}.cpp").read_text()
        inlined = body(cpp, "ctn_bind_inline")
        assert "increment(argument_delta)" in inlined, inlined
        assert "inline_target(" not in inlined, inlined
        assert "](int32_t argument_delta) -> int32_t" in inlined, inlined
        assert "capture_seed = std::move(capture_seed)" in inlined, inlined
        assert "increment(" in body(cpp, "inline_target"), cpp
        assert "inline_target(" in body(cpp, "direct_caller"), cpp
        for name in ["mutable", "unknown"]:
            forwarded = body(cpp, f"ctn_bind_{name}")
            assert f"return {name}_target(capture_seed);" in forwarded, forwarded
            assert "mutable {" not in forwarded and "[&" not in forwarded, forwarded
            assert body(cpp, f"{name}_target"), cpp
        assert "&address_target" in body(cpp, "address_pointer"), cpp
        assert "&literal_target" in body(cpp, "literal_pointer"), cpp
        assert body(cpp, "address_target") and body(cpp, "literal_target"), cpp
        source = args.work / f"{label}.cpp"
        source.write_text(cpp + MAIN)
        for index, compiler in enumerate(compilers):
            binary = (args.work / f"{label}-{index}").resolve()
            run([compiler, "-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-Wconversion", "-pedantic", "-I", str(args.fixtures), str(source), "-o", str(binary)])
            run([str(binary)])
    rejected = subprocess.run([args.translate, "--mlir-to-cpp", str(args.fixtures / "invalid.mlir")],
                              text=True, capture_output=True, timeout=120)
    assert rejected.returncode != 0, rejected.stdout
    assert "invalid native callable body description" in rejected.stderr, rejected.stderr
    print("callable bodies: inline/forwarded behavior agrees under GCC/Clang, hoisted output and invalid metadata checked")


if __name__ == "__main__":
    main()
