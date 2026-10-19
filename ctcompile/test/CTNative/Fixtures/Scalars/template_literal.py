"""Build template-literal.test's native program with GCC and Clang, linked with
ctbrowser's Core library (Number-to-String formatting), run it, and require
its stdout to equal the VM's console.log lines (--expected)."""

import argparse
from pathlib import Path

from CTNative.Browser.dom.dom import link_options
from CTNative.harness import FLAGS, build_and_run, find_compilers


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("cpp", "expected", "build", "include", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    expected = args.expected.read_text()
    _, libraries = link_options(args, core_only=True)
    for index, compiler in enumerate(find_compilers()):
        binary = args.work / f"template-literal.{index}"
        result = build_and_run(compiler, args.cpp, binary, flags=FLAGS, libraries=libraries)
        if result.stdout != expected:
            raise SystemExit(
                f"{Path(compiler).name}: native template-literal lines differ from the VM\n"
                f"--- VM\n{expected}--- native\n{result.stdout}"
            )
    print(f"template-literal: {len(expected.splitlines())} lines match the VM with GCC and Clang")


if __name__ == "__main__":
    main()
