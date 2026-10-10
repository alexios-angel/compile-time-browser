#!/usr/bin/env python3
"""Check that CMake files are owned locally and never use parent-relative paths.

Every directory under ctbrowser/, ctcompile/, cmake/ and tools/ that holds C++
sources must have its own CMakeLists.txt, and no CMakeLists.txt or .cmake file
may reference `..`. Runs a self-test on a temporary tree first.
"""

import os
from pathlib import Path
import re
import sys
import tempfile

# Files whose presence requires a CMakeLists.txt in the same directory.
CPP_SUFFIXES = {".cpp", ".cc", ".cxx", ".h", ".hpp", ".hxx"}
SKIP_DIRECTORIES = {
    ".git",
    ".claude",
    ".venv",
    "__pycache__",
    "vendor",
    "clang-std-embed",
    "llvm-mingw",
}
# A `..` path component not preceded by a word character or dot.
PARENT_PATH = re.compile(r"(?<![\w.])\.\.(?:[/\\]|[\"'\s)]|$)")


def check(root):
    """Return one error string per violation found under `root`."""
    errors = []
    for area in ("ctbrowser", "ctcompile", "cmake", "tools"):
        for directory, children, files in os.walk(root / area):
            children[:] = [
                child
                for child in children
                if child not in SKIP_DIRECTORIES and not child.startswith("build")
            ]
            folder = Path(directory)
            if any(Path(name).suffix in CPP_SUFFIXES for name in files):
                if "CMakeLists.txt" not in files:
                    errors.append(f"{folder.relative_to(root)}: missing CMakeLists.txt")
            for name in files:
                if name != "CMakeLists.txt" and not name.endswith(".cmake"):
                    continue
                path = folder / name
                for number, line in enumerate(path.read_text().splitlines(), 1):
                    if not line.lstrip().startswith("#") and PARENT_PATH.search(line):
                        errors.append(f"{path.relative_to(root)}:{number}: parent-relative path")
    return errors


def self_test():
    """Assert check() reports a missing CMakeLists.txt and a `..` path, and
    skips vendor directories."""
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        folder = root / "ctcompile/lib/Example"
        folder.mkdir(parents=True)
        (folder / "example.cpp").touch()
        assert check(root) == ["ctcompile/lib/Example: missing CMakeLists.txt"]
        cmake = folder / "CMakeLists.txt"
        cmake.write_text("target_sources(example PRIVATE ../example.cpp)\n")
        assert check(root) == ["ctcompile/lib/Example/CMakeLists.txt:1: parent-relative path"]
        cmake.write_text("target_sources(example PRIVATE example.cpp)\n")
        vendor = root / "ctbrowser/vendor/example"
        vendor.mkdir(parents=True)
        (vendor / "external.hpp").touch()
        assert check(root) == []


if __name__ == "__main__":
    self_test()
    problems = check(Path(__file__).resolve().parents[2])
    if problems:
        print("\n".join(problems), file=sys.stderr)
        sys.exit(1)
    print("CMake ownership and path checks passed.")
