#!/usr/bin/env python3
"""Does a table's `file.cpp:NNN` citation point at code that exists?

aot_helpers.def cites the runtime that owns each row's semantics by file and
line, and FrameEnds.def cites nine exit paths the same way. A LINE NUMBER IN A
COMMENT IS A FACT WITH AN EXPIRY DATE: Phases 3, 4 and 5 moved several hundred
lines and six citations ended up PAST THE END of a file that had shrunk -
which is the one class of rot a machine can see. The rest it cannot: a
citation that still lands inside the file now names a different handler, and
only a reader can notice. So this checks the class it can, and the fix for a
bad row is to cite by NAME - `run_loop.cpp's VM_CASE(cell_get)` - rather than
by a fresh number that will rot again.

The rows cite bare basenames, so the engine tree is searched for them; a name
that matches more than one file passes if any is long enough, because the row
does not say which it meant and guessing would invent a failure. A basename
not in the tree is skipped (a standard header, say).
"""

import re
import sys
from pathlib import Path


def table_text(path):
    text = path.read_text()
    return re.sub(
        r'^#include "([^\"]+)"$',
        lambda match: table_text(path.parent / match[1]),
        text,
        flags=re.M,
    )


definition, root = Path(sys.argv[1]), Path(sys.argv[2]) / "ctbrowser"
citations = sorted(
    set(re.findall(r"[A-Za-z_0-9]+\.(?:cpp|hpp|def|h):[0-9]+", table_text(definition)))
)
wanted = {citation.rsplit(":", 1)[0] for citation in citations}
files = {}  # one walk of the tree, indexed by basename
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
