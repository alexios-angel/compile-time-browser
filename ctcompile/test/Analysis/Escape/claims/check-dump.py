#!/usr/bin/env python3
"""Compares the escape fixture's dump with its snapshot and tests the dump itself.

usage: check-dump.py CHECKER DUMP EXPECTED_DIR RECORDING

First the dump that CHECKER (tools/check/escape-oracle.py) wrote for the fixture
must equal the concatenated EXPECTED_DIR/*.txt parts, ignoring blank and `#`
lines. Then CHECKER is run on a small hand-written recording: the dump must not
depend on input order, every evidence mutation (a dropped or extra allocation,
observation or claim) must change the dump, and every duplicate or malformed
input must be rejected by name.
"""

import difflib
from pathlib import Path
import subprocess
import sys
import tempfile


def rows(text):
    """Returns the non-blank, non-`#` lines of `text`, each ending in a newline."""
    return [line + "\n" for line in text.splitlines() if line and not line.startswith("#")]


checker, dump, expected, source_recording = map(Path, sys.argv[1:])
with source_recording.open() as source:
    census = next(line.rstrip() for line in source if line.startswith("opcodes "))
want, got = (
    rows("".join(p.read_text() for p in sorted(expected.glob("*.txt")))),
    rows(dump.read_text()),
)
if want != got:
    sys.stderr.writelines(difflib.unified_diff(want, got, str(expected), str(dump)))
    raise SystemExit("escape fixture allocation/observation/claim snapshot changed")

# A hand-written recording independent of the fixture: one observed literal, one
# implicit Error (site 2, which has no claim), and two literals that never run.
# Reordering its rows must not change the dump; losing any row must.
recording = f"""ctbrowser-type-recording 2
{census}
defs recorded 0 dropped 0 orphan-frames 0
escape budget unlimited pops 1 unwinds 0 checks 1 unframed 0 unresolved 0
programs 1
program abc size 0 functions 2 label -
fn 0 entries 1 params 0 frame 0 name live
alloc 1 kind obj
alloc 5 kind arr
site 1 kind obj made 3 confined 0 escaped 3 unresolved 0 unchecked 0 routes globals:1,temporaries:2
site 2 kind obj made 1 confined 0 escaped 1 unresolved 0 unchecked 0 routes thrown:1
fn 1 entries 0 params 0 frame 0 name unreached
alloc 7 kind obj
"""
claims = """escape abc 0 1 obj escapes:stored
escape abc 0 5 arr confined
escape abc 1 7 obj confined
"""
baseline = """program abc
fn 0 live
alloc 1 obj 3 0 3 0 0 globals:1,temporaries:2 escapes:stored
site 2 obj 1 0 1 0 0 thrown:1 unclaimed
alloc 5 arr unobserved confined
fn 1 unreached
alloc 7 obj unobserved confined
"""
with tempfile.TemporaryDirectory(prefix="escape-dump-") as directory:
    work = Path(directory)

    def run(rec, claim):
        """Runs the checker on `rec` and `claim`; returns its result and dump."""
        (work / "input.rec").write_text(rec)
        (work / "input.claims").write_text(claim)
        result = subprocess.run(
            [
                sys.executable,
                str(checker),
                "--recording",
                str(work / "input.rec"),
                "--claims",
                str(work / "input.claims"),
                "--dump",
                str(work / "dump"),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        return result, (work / "dump").read_text() if result.returncode == 0 else ""

    result, observed = run(recording, claims)
    assert result.returncode == 0 and observed == baseline, result.stderr + observed
    result, observed = run(
        recording.replace("globals:1,temporaries:2", "temporaries:2,globals:1"),
        "\n".join(reversed(claims.splitlines())) + "\n",
    )
    assert result.returncode == 0 and observed == baseline, "dump depends on input order"

    source_site = next(line + "\n" for line in recording.splitlines() if line.startswith("site 1 "))
    implicit_site = next(
        line + "\n" for line in recording.splitlines() if line.startswith("site 2 ")
    )
    # Each mutation is valid input whose dump must differ from the baseline.
    mutations = [
        ("missing allocation", recording.replace("alloc 1 kind obj\n", ""), claims),
        (
            "extra allocation",
            recording.replace("alloc 1 kind obj", "alloc 2 kind obj\nalloc 1 kind obj"),
            claims,
        ),
        ("missing observation", recording.replace(source_site, ""), claims),
        (
            "extra observation",
            recording.replace(source_site, source_site + implicit_site.replace("site 2", "site 3")),
            claims,
        ),
        (
            "changed route",
            recording.replace("globals:1,temporaries:2", "globals:2,temporaries:1"),
            claims,
        ),
        (
            "changed count",
            recording.replace("made 3 confined 0", "made 4 confined 1"),
            claims,
        ),
        (
            "missing claim",
            recording,
            claims.replace("escape abc 0 1 obj escapes:stored\n", ""),
        ),
        (
            "missing unobserved claim",
            recording,
            claims.replace("escape abc 0 5 arr confined\n", ""),
        ),
        (
            "implicit Error claimed",
            recording,
            claims + "escape abc 0 2 obj escapes:thrown\n",
        ),
        ("extra unobserved claim", recording, claims + "escape abc 0 9 obj confined\n"),
        (
            "unrecorded function claim",
            recording,
            claims + "escape abc 2 1 obj confined\n",
        ),
        (
            "unrecorded program claim",
            recording,
            claims + "escape def 0 1 obj confined\n",
        ),
        (
            "changed claim",
            recording,
            claims.replace("escapes:stored", "escapes:returned"),
        ),
    ]
    for name, rec, claim in mutations:
        result, observed = run(rec, claim)
        assert result.returncode == 0, name + ": " + result.stderr
        assert observed != baseline, name + " disappeared from the dump"

    # Each of these inputs must make the checker fail with the case name in stderr.
    rejected = [
        (
            "duplicate allocation",
            recording.replace("alloc 1 kind obj", "alloc 1 kind obj\nalloc 1 kind obj"),
            claims,
        ),
        ("duplicate site", recording.replace(source_site, source_site * 2), claims),
        ("duplicate claim", recording, claims + "escape abc 0 1 obj escapes:stored\n"),
        (
            "duplicate function",
            recording + "fn 1 entries 0 params 0 frame 0 name duplicate\n",
            claims,
        ),
        (
            "duplicate program",
            recording + "program abc size 0 functions 0 label -\n",
            claims,
        ),
        (
            "duplicate root route",
            recording.replace("globals:1,temporaries:2", "globals:1,globals:2"),
            claims,
        ),
        ("malformed site", recording.replace("made 3", "lost 3"), claims),
        (
            "malformed allocation",
            recording.replace("alloc 1 kind obj", "alloc 1 lost obj"),
            claims,
        ),
    ]
    for name, rec, claim in rejected:
        result, _ = run(rec, claim)
        assert result.returncode != 0 and name in result.stderr, name + ": " + result.stderr

print(
    f"escape dump: fixture matches; {len(mutations)} evidence mutations and {len(rejected)} malformed/duplicate controls pass"
)
