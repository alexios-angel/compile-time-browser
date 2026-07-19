#!/usr/bin/env python3
"""Wrap an HTML file as a C++ raw-string literal, so a real .html page
can be #include'd as the NTTP of ctbrowser::page<...>. Writes to the
output path when given, else stdout. The raw-string delimiter grows
underscores until it cannot collide with the content."""
import sys

src = open(sys.argv[1], encoding="utf-8").read()
delim = "ctb"
while f"){delim}\"" in src:
    delim += "_"
lit = f'R"{delim}(' + src + f'){delim}"\n'
if len(sys.argv) > 2:
    with open(sys.argv[2], "w", encoding="utf-8") as out:
        out.write(lit)
else:
    sys.stdout.write(lit)
