#!/usr/bin/env python3
"""Shim: tools/corpus/ratchet.py p5 ratchet."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ratchet import main

sys.exit(main(["p5", "ratchet", *sys.argv[1:]]))
