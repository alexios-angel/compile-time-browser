#!/usr/bin/env python3
"""Shim: tools/corpus/ratchet.py module ratchet."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ratchet import main

sys.exit(main(["module", "ratchet", *sys.argv[1:]]))
