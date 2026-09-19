"""Storage changes must retain failures, unexpected passes and untouched suites."""

from pathlib import Path
import tempfile
import unittest

from expectations import parse_expectations, write_expectations


class ExpectationsTest(unittest.TestCase):
    def test_flat_and_split_round_trip(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "expectations.txt"
            rows = [f'dom/nodes/a.html\tSUBTEST\tFAIL\t"case {i}"' for i in range(1801)]
            rows += ["css/cssom/b.html\tFAIL"]
            write_expectations(path, ["# historical measurement"], rows)
            self.assertEqual(parse_expectations(path), set(rows))
            write_expectations(path, ["# historical measurement"], rows, split=True)
            self.assertEqual(parse_expectations(path), set(rows))
            self.assertTrue(path.read_text().startswith("# historical measurement\n"))
            parts = list(path.with_suffix("").rglob("*.txt"))
            self.assertEqual(len(parts), 4)
            self.assertTrue(all(len(p.read_text().splitlines()) <= 900 for p in parts))
            # A scoped update carries the other suite; removed failures remain removed.
            remaining = [rows[-1], "dom/nodes/c.html\tTIMEOUT"]
            write_expectations(path, [], remaining, split=True)
            self.assertEqual(parse_expectations(path), set(remaining))
            self.assertEqual(len(list(path.with_suffix("").rglob("*.txt"))), 2)

    def test_missing_part_is_an_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "expectations.txt"
            path.write_text("# include missing.txt\n")
            with self.assertRaises(FileNotFoundError):
                parse_expectations(path)


if __name__ == "__main__":
    unittest.main()
