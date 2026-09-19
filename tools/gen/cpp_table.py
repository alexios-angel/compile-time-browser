"""Keep generated C++ array rows in bounded include files beside their declarations."""

import argparse
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
import re


def write_table(text, output):
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
