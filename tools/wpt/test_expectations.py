"""Storage changes must retain failures, unexpected passes and untouched suites."""

import pytest

from expectations import parse_expectations, write_expectations


def test_flat_and_split_round_trip(tmp_path):
    path = tmp_path / "expectations.txt"
    rows = [f'dom/nodes/a.html\tSUBTEST\tFAIL\t"case {i}"' for i in range(1801)]
    rows += ["css/cssom/b.html\tFAIL"]
    write_expectations(path, ["# historical measurement"], rows)
    assert parse_expectations(path) == set(rows)
    write_expectations(path, ["# historical measurement"], rows, split=True)
    assert parse_expectations(path) == set(rows)
    assert path.read_text().startswith("# historical measurement\n")
    parts = list(path.with_suffix("").rglob("*.txt"))
    assert len(parts) == 4
    assert all(len(p.read_text().splitlines()) <= 900 for p in parts)
    # A scoped update carries the other suite; removed failures remain removed.
    remaining = [rows[-1], "dom/nodes/c.html\tTIMEOUT"]
    write_expectations(path, [], remaining, split=True)
    assert parse_expectations(path) == set(remaining)
    assert len(list(path.with_suffix("").rglob("*.txt"))) == 2


def test_missing_part_is_an_error(tmp_path):
    path = tmp_path / "expectations.txt"
    path.write_text("# include missing.txt\n")
    with pytest.raises(FileNotFoundError):
        parse_expectations(path)
