#!/usr/bin/env python3
"""Validate the SPIR-V this engine emits, with a real validator.

WHY THIS EXISTS. `tests/spirv_basics.cpp` checks the bytes structurally - the
header, the word counts, that every id used is defined - and that catches a lot.
`tests/gpu_basics.cpp` hands them to SDL_CreateGPUShader, and MEASURES what that
is worth: on this machine the driver accepts deliberate garbage, so acceptance
proves the bytes reached it and nothing more. Neither is validation.

spirv-val is. It is the reference implementation's checker, and it knows the
rules a structural pass cannot: that an OpFAdd's operands have the same type,
that an entry point lists the variables it uses, that a decoration is legal
where it appears.

    tools/check-spirv.py build/spirv/*.spv

OPTIONAL, like plutosvg and SDL3_image: without spirv-val everything still
builds and passes, and this says plainly that it did not check rather than
implying it did. Install it with:

    brew install spirv-tools        # or: apt install spirv-tools
"""

import pathlib
import shutil
import struct
import subprocess
import sys

MAGIC = 0x07230203


def structure(path: pathlib.Path) -> str | None:
    """The checks that need no tool. Returns a complaint, or None."""
    data = path.read_bytes()
    if len(data) < 20:
        return "shorter than the five-word header"
    if len(data) % 4:
        return f"{len(data)} bytes is not a whole number of words"
    words = struct.unpack(f"<{len(data) // 4}I", data)
    if words[0] != MAGIC:
        return f"magic is {words[0]:#010x}, expected {MAGIC:#010x}"
    bound = words[3]
    at = 5
    count = 0
    while at < len(words):
        length = words[at] >> 16
        if length == 0:
            return f"a zero word count at word {at}"
        if at + length > len(words):
            return f"the instruction at word {at} runs off the end"
        at += length
        count += 1
    return None


def main() -> int:
    paths = [pathlib.Path(p) for p in sys.argv[1:]]
    if not paths:
        sys.exit("check-spirv: give it some .spv files")

    validator = shutil.which("spirv-val")
    if validator is None:
        print("check-spirv: spirv-val is NOT INSTALLED, so nothing was validated.")
        print("             Structure is still checked below; the rules that need a")
        print("             real validator - operand types, entry-point interfaces,")
        print("             decoration placement - are NOT.")
        print("             brew install spirv-tools\n")

    failures = 0
    for path in paths:
        complaint = structure(path)
        if complaint is not None:
            print(f"  FAIL {path.name}: {complaint}")
            failures += 1
            continue
        if validator is None:
            print(f"  ok?  {path.name}: structure only, {path.stat().st_size} bytes")
            continue
        result = subprocess.run([validator, str(path)], capture_output=True, text=True)
        if result.returncode != 0:
            print(f"  FAIL {path.name}:\n{result.stdout}{result.stderr}")
            failures += 1
        else:
            print(f"  ok   {path.name}: valid SPIR-V, {path.stat().st_size} bytes")

    if failures:
        sys.exit(f"\ncheck-spirv: {failures} file(s) failed")
    print(f"\ncheck-spirv: {len(paths)} file(s) checked"
          f"{'' if validator else ' - STRUCTURE ONLY, see above'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
