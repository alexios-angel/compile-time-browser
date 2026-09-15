#!/usr/bin/env python3
"""Check member-call arguments, receiver precedence and move-only extraction."""

import argparse
from pathlib import Path

from harness import FLAGS, find_compilers, run

PREFIX = """#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
struct Probe {
    int32_t total = 0;
    template<class T, int32_t N> T mix(T left, T middle, T right) {
        return N * left + 10 * middle + 100 * right;
    }
    void touch() { ++total; }
    int32_t bump(int32_t value) { total += value; return total; }
};
"""

MAIN = """
int main() {
    std::optional<std::unique_ptr<int>> value;
    if (present(value)) { return 1; }
    value.emplace(std::make_unique<int>(42));
    if (!present(value)) { return 2; }
    auto owned = take(value);
    if (!owned || *owned != 42 || !value || *value) { return 3; }
    value.reset();
    if (*owned != 42) { return 4; }
    Probe left, right;
    if (ordered(left, 3, 5) != 380 || left.total != 1) { return 5; }
    if (pointer_call(&left, 4) != 5 || left.total != 5) { return 6; }
    if (selected(false, left, right, 7) != 7 || right.total != 7 || left.total != 5) {
        return 7;
    }
    if (selected(true, left, right, 2) != 7 || left.total != 7 || right.total != 7) {
        return 8;
    }
    return 0;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    emitted = run([args.translate, "--mlir-to-cpp", str(args.source)])
    source = args.work / "member-call.cpp"
    source.write_text(PREFIX + emitted + MAIN)
    for index, compiler in enumerate(find_compilers()):
        binary = (args.work / f"member-call-{index}").resolve()
        run([compiler, *FLAGS, str(source), "-o", str(binary)])
        run([str(binary)])
        print(f"member calls: {Path(compiler).name} preserved arguments, receivers and ownership")


if __name__ == "__main__":
    main()
