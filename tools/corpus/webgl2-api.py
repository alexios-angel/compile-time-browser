#!/usr/bin/env python3
"""Shim: tools/corpus/ratchet.py webgl2 api."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ratchet import main

sys.exit(main(["webgl2", "api", *sys.argv[1:]]))
