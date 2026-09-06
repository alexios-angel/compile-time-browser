#!/usr/bin/env python3
"""Compile emitted shortest literals and compare their original IEEE bits."""

import argparse
from pathlib import Path
import random
import shutil
import subprocess


def run(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise SystemExit(
            "failed: " + " ".join(map(str, command)) + "\n" + result.stdout + result.stderr
        )
    return result.stdout


def patterns(width):
    # Zero signs, subnormal/normal boundaries, decimal rounding boundaries,
    # adjacent values around one, the integer precision boundary and maxima.
    anchors = {
        64: [
            0x0000000000000000, 0x8000000000000000,
            0x0000000000000001, 0x0000000000000002,
            0x000FFFFFFFFFFFFF, 0x0010000000000000, 0x0010000000000001,
            0x3FB9999999999999, 0x3FB999999999999A, 0x3FB999999999999B,
            0xBFB999999999999A, 0x3FD5555555555555,
            0x3FEFFFFFFFFFFFFF, 0x3FF0000000000000, 0x3FF0000000000001,
            0x4059000000000000, 0x4340000000000000, 0x4340000000000001,
            0x7FEFFFFFFFFFFFFF, 0xFFEFFFFFFFFFFFFF,
        ],
        32: [
            0x00000000, 0x80000000, 0x00000001, 0x00000002,
            0x007FFFFF, 0x00800000, 0x00800001,
            0x3DCCCCCC, 0x3DCCCCCD, 0x3DCCCCCE, 0xBDCCCCCD, 0x3EAAAAAB,
            0x3F7FFFFF, 0x3F800000, 0x3F800001,
            0x42C80000, 0x4B800000, 0x4B800001, 0x7F7FFFFF, 0xFF7FFFFF,
        ],
    }[width]
    values = set(anchors)
    generator = random.Random(0xC7C0 + width)
    exponent = {64: 0x7FF0000000000000, 32: 0x7F800000}[width]
    while len(values) < len(anchors) + 64:
        bits = generator.getrandbits(width)
        if bits & exponent != exponent:
            values.add(bits)
    return sorted(values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    compilers = []
    for candidates in [("g++-13", "g++"), ("clang++-18", "clang++")]:
        compiler = next((shutil.which(name) for name in candidates if shutil.which(name)), None)
        if not compiler:
            raise SystemExit("readable float test requires " + " or ".join(candidates))
        compilers.append(compiler)

    args.work.mkdir(parents=True, exist_ok=True)
    source = args.work / "floats.mlir"
    cpp = args.work / "floats.cpp"
    cases = [(width, bits) for width in (64, 32) for bits in patterns(width)]
    ir = ["module attributes {ctnative.readable_literals} {"]
    for width, bits in cases:
        name = f"value_{width}_{bits:0{width // 4}X}"
        ir.extend([
            f"  emitc.func @{name}() -> f{width} {{",
            f'    %value = "emitc.constant"() {{value = 0x{bits:0{width // 4}X} : '
            f"f{width}, ctnative.deduced}} : () -> f{width}",
            f"    emitc.return %value : f{width}",
            "  }",
        ])
    ir.append("}")
    source.write_text("\n".join(ir) + "\n")
    emitted = run([args.translate, "--mlir-to-cpp", str(source)])

    # The C++ compiler independently parses the emitted decimals. The source
    # bits are integer expectations, and the existing deduction pins also
    # ensure 100.0 remains double and 100.0f remains float under auto.
    prefix = """#include <bit>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#define CTCOMPILE_PIN(name, site, ...) static_assert(std::is_same_v<decltype(name), __VA_ARGS__>, site)
template<class Float, class UInt> bool same_bits(Float value, UInt expected) {
    const UInt actual = std::bit_cast<UInt>(value);
    if (actual == expected) { return true; }
    std::fprintf(stderr, "expected bits %llx, got %llx\\n",
                 static_cast<unsigned long long>(expected),
                 static_cast<unsigned long long>(actual));
    return false;
}
"""
    checks = ["int main() {"]
    for width, bits in cases:
        name = f"value_{width}_{bits:0{width // 4}X}"
        checks.append(
            f"    if (!same_bits({name}(), UINT{width}_C(0x{bits:X}))) {{ return 1; }}"
        )
    checks.append("    return 0;\n}")
    cpp.write_text(prefix + emitted + "\n" + "\n".join(checks) + "\n")
    for index, compiler in enumerate(compilers):
        executable = args.work / f"floats-{index}"
        run([compiler, "-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
             "-Wconversion", "-ffp-contract=off", str(cpp), "-o", str(executable)])
        run([str(executable)])
        print(f"readable float literals: {Path(compiler).name} preserved {len(cases)} bit patterns")


if __name__ == "__main__":
    main()
