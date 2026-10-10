#!/usr/bin/env python3
"""Lower string-arrays-calls.js through the native pipeline into one EmitC module.

The fixture is not registered in NativeFixtures.cmake, so its .test builds
the module here, with Checks/pipeline.cmake as the build does, and then runs
the usual checks over it.
"""

import argparse
from pathlib import Path

from CTNative.harness import native_pipeline


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--js", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    native_pipeline(args, args.js, args.out)


if __name__ == "__main__":
    main()
