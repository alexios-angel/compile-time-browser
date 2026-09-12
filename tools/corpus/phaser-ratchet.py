#!/usr/bin/env python3
"""Shim: tools/corpus/ratchet.py phaser ratchet."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ratchet import main

sys.exit(main(["phaser", "ratchet", *sys.argv[1:]]))
