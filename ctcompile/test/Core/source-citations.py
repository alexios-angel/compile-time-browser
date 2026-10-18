#!/usr/bin/env python3
"""Checks that a table's `file.cpp:NNN` citations point at lines that exist.

Usage: source-citations.py TABLE.def MONOREPO_ROOT

aot_helpers.def cites, by file and line, the runtime code that owns each row's
semantics, and FrameEnds.def cites its exit paths the same way. Line numbers in
comments rot as files change. The one kind of rot a machine can see is a
citation past the end of a file that has shrunk, so that is what this checks; a
citation that still lands inside the file but now names a different handler
needs a human. The durable fix for a bad row is to cite by name
(`run_loop.cpp's VM_CASE(cell_get)`) rather than by a fresh line number.

The rows cite bare basenames, so the engine tree is searched for them. A name
that matches more than one file passes if any of them is long enough, because
the row does not say which it meant and guessing would invent a failure. A
basename not in the tree (a standard header, say) is skipped.
"""

import re
import sys
from pathlib import Path


def table_text(path):
    """Returns the table's text with each `#include "x"` line replaced, recursively."""
    text = path.read_text()
    return re.sub(
        r'^#include "([^\"]+)"$',
        lambda match: table_text(path.parent / match[1]),
        text,
        flags=re.M,
    )


def main(argv=None):
    definition, root = (Path(a) for a in (sys.argv[1:] if argv is None else argv))
    root /= "ctbrowser"
    citations = sorted(
        set(re.findall(r"[A-Za-z_0-9]+\.(?:cpp|hpp|def|h):[0-9]+", table_text(definition)))
    )
    wanted = {citation.rsplit(":", 1)[0] for citation in citations}
    files = {}  # One walk of the tree, indexed by basename.
    for path in root.rglob("*"):
        if path.name in wanted:
            files.setdefault(path.name, []).append(path)
    checked, bad = 0, []
    for citation in citations:
        cited_file, cited_line = citation.rsplit(":", 1)
        found = files.get(cited_file, [])
        if not found:
            continue
        checked += 1
        if not any(path.read_text().count("\n") >= int(cited_line) for path in found):
            bad.append(citation)
    if bad:
        sys.exit(
            "the table cites lines past the end of the files it names:\n  "
            + "\n  ".join(bad)
            + '\nRepair by citing the handler BY NAME - "run_loop.cpp\'s VM_CASE(cell_get)" - rather '
            "than by a fresh line number, which will rot again the next time that file moves."
        )
    print(f"ok citations ({checked} file:line citations, all inside their files)")


def test_def_citations(lit):
    """def-citations.test: both tables' RUN lines."""
    main(lit.args("%{monorepo}/ctbrowser/include/ctbrowser/aot/aot_helpers.def %{monorepo}"))
    main(lit.args("%S/../../include/ctcompile/JavaScript/FrameEnds.def %{monorepo}"))


if __name__ == "__main__":
    main()
