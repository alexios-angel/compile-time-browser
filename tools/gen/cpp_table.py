"""Keep generated C++ array rows in bounded include files beside their declarations.

Used by the table generators in this directory (idna_table.py, nfc_table.py,
unicode_properties.py): each prints one C++ header to stdout, and generate()
splits every large array in it into `<header stem>/<array>-NN.inc` files that
the header #includes.
"""

import argparse
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
import re


def write_table(text, output):
    """Write C++ header `text` to `output`, moving the rows of each
    `constexpr ... name[...] = {` array into `<output stem>/<name>-NN.inc`
    include files, and delete any stale .inc files left in that directory."""
    directory = output.with_suffix("")
    directory.mkdir(parents=True, exist_ok=True)
    lines = text.splitlines(keepends=True)
    result = []
    written = set()
    at = 0
    while at < len(lines):
        line = lines[at]
        result.append(line)
        at += 1
        match = re.match(r"^(?:inline )?constexpr .+ (\w+)\[.*\] = \{\s*$", line)
        if not match:
            continue
        end = next(i for i in range(at, len(lines)) if lines[i].strip() == "};")
        # 900 rows per include file keeps every generated file small enough to
        # read and diff; the include order rebuilds the array unchanged.
        for start in range(at, end, 900):
            part = directory / f"{match[1]}-{(start - at) // 900 + 1:02d}.inc"
            part.write_text("".join(lines[start : min(start + 900, end)]))
            written.add(part)
            result.append(f'#include "{directory.name}/{part.name}"\n')
        at = end
    output.write_text("".join(result))
    for stale in set(directory.glob("*.inc")) - written:
        stale.unlink()


def generate(main, relative_output):
    """Run `main` (which prints a C++ table to stdout), split the result with
    write_table, and write it to --output (default: `relative_output` under the
    repository root). Returns main's nonzero result unchanged."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[2] / relative_output,
    )
    args = parser.parse_args()
    generated = StringIO()
    with redirect_stdout(generated):
        result = main()
    if result:
        return result
    write_table(generated.getvalue(), args.output)
    return 0
