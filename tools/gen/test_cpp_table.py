"""A table split must preserve every byte and remove obsolete generated parts."""

import re

from cpp_table import write_table


def test_round_trip_and_smaller_regeneration(tmp_path):
    output = tmp_path / "table.inc"
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
        assert expanded == source
        # write_table splits a table into parts of at most 900 lines.
        parts = list(output.with_suffix("").glob("*.inc"))
        assert len(parts) == (count + 899) // 900
        assert all(len(part.read_text().splitlines()) <= 900 for part in parts)
