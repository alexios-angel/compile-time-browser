"""A table split must preserve every byte and remove obsolete generated parts."""

from pathlib import Path
import re
import tempfile
import unittest

from cpp_table import write_table


class TableTest(unittest.TestCase):
    def test_round_trip_and_smaller_regeneration(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "table.inc"
            for count in (1801, 1):
                source = "// table\nconstexpr int values[] = {\n"
                source += "".join(f"    {i},\n" for i in range(count)) + "};\n"
                write_table(source, output)
                expanded = re.sub(
                    r'^#include "([^\"]+)"\n',
                    lambda match: (output.parent / match[1]).read_text(),
                    output.read_text(),
                    flags=re.M,
                )
                self.assertEqual(expanded, source)
                parts = list(output.with_suffix("").glob("*.inc"))
                self.assertEqual(len(parts), (count + 899) // 900)
                self.assertTrue(all(len(part.read_text().splitlines()) <= 900 for part in parts))


if __name__ == "__main__":
    unittest.main()
