"""The escape checker derives its census from the recorder's opcode rows."""

import re
import tempfile
from pathlib import Path

from selftest import load

oracle = load(Path(__file__).with_name("escape-oracle.py"))
DEFINITIONS = (
    Path(__file__).resolve().parents[2] / "ctbrowser/include/ctbrowser/script/bytecode_opcodes.def"
)


def recording(path, opcodes, writers):
    path.write_text(
        f"ctbrowser-type-recording 2\nopcodes {opcodes} writers {writers}\n"
        "escape budget unlimited pops 0 unwinds 0 checks 0 unframed 0 unresolved 0\n"
        "programs 0\n"
    )


def test_current_census():
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "recording.txt"
        count, writers = oracle.opcode_census(DEFINITIONS)
        recording(path, count, writers)
        parsed = oracle.read_recording(str(path), DEFINITIONS)
        assert (parsed.opcodes, parsed.writers) == (count, writers)


def test_removed_opcode_rejects_recording():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        definitions = root / "opcodes.def"
        text = DEFINITIONS.read_text()
        changed, removed = re.subn(
            r"^CT_OPCODE\([^\n]*\n(?:.*\n)*?.*\)\n", "", text, count=1, flags=re.MULTILINE
        )
        assert removed == 1 and changed != text
        definitions.write_text(changed)
        count, writers = oracle.opcode_census(DEFINITIONS)
        assert oracle.opcode_census(definitions) == (count - 1, writers - 1)
        path = root / "recording.txt"
        recording(path, count, writers)
        try:
            oracle.read_recording(str(path), definitions)
        except SystemExit as error:
            assert "opcode definitions" in str(error)
        else:
            raise AssertionError("a recording from a different opcode census was accepted")


def test_writer_classification():
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "opcodes.def"
        text = (
            "/* CT_OPCODE(comment, reg, unused, unused, 1, 0) */\n"
            "CT_OPCODE(load, reg, unused, unused, 1, 0)\n"
            "CT_OPCODE(store, reg, unused, unused, 0, 0)\n"
        )
        path.write_text(text)
        assert oracle.opcode_census(path) == (2, 1)
        path.write_text(
            text.replace("store, reg, unused, unused, 0", "store, reg, unused, unused, 1")
        )
        assert oracle.opcode_census(path) == (2, 2)
