"""Read flat expectation files or manifests of bounded, suite-grouped parts."""

from collections import defaultdict
from pathlib import Path

INCLUDE = "# include "


def parse_expectations(path: Path):
    if not path.exists():
        return set()
    lines = path.read_text(encoding="utf-8").splitlines()
    parts = [
        path.parent / line.removeprefix(INCLUDE)
        for line in lines
        if line.startswith(INCLUDE)
    ]
    for part in parts:
        lines.extend(part.read_text(encoding="utf-8").splitlines())
    return {line for line in lines if line and not line.startswith("#")}


def write_expectations(path: Path, header, lines, *, split=False):
    if not split:
        path.write_text("\n".join(header + lines) + "\n", encoding="utf-8")
        return
    groups = defaultdict(list)
    for line in lines:
        suite = "/".join(line.split("\t", 1)[0].split("/")[:2])
        groups[suite].append(line)
    directory = path.with_suffix("")
    previous = set(directory.rglob("*.txt")) if directory.exists() else set()
    written = set()
    includes = []
    for suite, rows in sorted(groups.items()):
        # WPT suite names are relative paths beneath the corpus.
        relative = Path(suite)
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError(f"invalid WPT suite: {suite}")
        for offset in range(0, len(rows), 900):
            part = directory / relative / f"{offset // 900 + 1:03d}.txt"
            part.parent.mkdir(parents=True, exist_ok=True)
            part.write_text(
                "\n".join(rows[offset : offset + 900]) + "\n", encoding="utf-8"
            )
            written.add(part)
            includes.append(INCLUDE + part.relative_to(path.parent).as_posix())
    path.write_text("\n".join(header + includes) + "\n", encoding="utf-8")
    for stale in previous - written:
        stale.unlink()
