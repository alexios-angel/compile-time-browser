#!/usr/bin/env python3
"""Shim: tools/corpus/ratchet.py p5 api."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ratchet import main

sys.exit(main(["p5", "api", *sys.argv[1:]]))
