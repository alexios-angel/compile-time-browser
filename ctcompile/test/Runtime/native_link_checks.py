"""Check native compiler links without starting a compiler."""

import argparse
from pathlib import Path

from Target.Cpp.harness import native_link_command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--inline-runtime", type=Path)
    parser.add_argument("--output", type=Path)
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
    if args.inline_runtime:
        from CTNative.harness import RUNTIME_INCLUDE_LINE, inline_runtime

        source = RUNTIME_INCLUDE_LINE + "\n" + args.inline_runtime.read_text()
        args.output.write_text(inline_runtime(source))
    print("native links: Core default, explicit libraries, Core-free and compile-only preserved")


if __name__ == "__main__":
    main()
