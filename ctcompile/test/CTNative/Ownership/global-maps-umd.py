#!/usr/bin/env python3
"""Execute the pinned Bootstrap UMD prefix and its published methods without the VM."""

# One group of the native_owned_global_maps package (global-maps.py runs the
# rest); the path insert is for a caller that loads this file by path.
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from native_owned_global_maps.driver import main  # noqa: E402
from native_owned_global_maps.driver_umd import check_umd  # noqa: E402

if __name__ == "__main__":
    main(check_umd)
