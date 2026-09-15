#!/usr/bin/env python3
"""Execute finite nested Map schemas and their refusals without the VM."""

# One group of the native_owned_global_maps package (global-maps.py runs the
# rest); the path insert is for a caller that loads this file by path.
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from native_owned_global_maps.driver import main  # noqa: E402
from native_owned_global_maps.driver_nested_maps import check_nested_maps  # noqa: E402

if __name__ == "__main__":
    main(check_nested_maps)
