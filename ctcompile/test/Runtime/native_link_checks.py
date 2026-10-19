"""Check native compiler links without starting a compiler."""

import argparse
from pathlib import Path

from Target.Cpp.harness import native_link_command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    command = ["clang++-18", "-O2", "input.cpp", "-o", "program"]
    linked = native_link_command(command, args.build)
    assert linked[: len(command)] == command
    assert str(args.build / "lib/Core/libctbrowser-core.a") in linked
    assert native_link_command(command + ["--core-free"], args.build) == command
    for flag in ("-c", "-S", "-E", "-fsyntax-only", "--version"):
        unchanged = command + [flag]
        assert native_link_command(unchanged, args.build) == unchanged
    explicit = command + ["/explicit/libctbrowser-core.a"]
    assert native_link_command(explicit, args.build) == explicit
    assert native_link_command(["nm", "-C", "program"], args.build) == ["nm", "-C", "program"]
    print("native links: Core default, explicit libraries, Core-free and compile-only preserved")


if __name__ == "__main__":
    main()
