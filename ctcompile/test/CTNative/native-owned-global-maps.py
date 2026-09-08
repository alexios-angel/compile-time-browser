#!/usr/bin/env python3
"""Execute checked published Map methods and live primitive results without the VM."""

# Since 2026-09-08 the driver is the package beside this file - it had grown to
# 1,013 lines. This entry keeps the command line and the lit RUN line exactly
# as they were: `python3 native-owned-global-maps.py --translate ... --opt ...
# --work ...`. The path insert is for a caller that loads this file by path.
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from native_owned_global_maps.driver import main  # noqa: E402


if __name__ == "__main__":
    main()
