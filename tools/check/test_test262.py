"""Checks for test262.py, run by pytest and by `test262.py --self-test`.

Each check runs ct262 on one planted file. `--self-test` passes its own
arguments (CTest names the binary and the corpus); under pytest the checks use
CT262_BINARY (default build/tools/ct262) and TEST262_DIR, and skip when either
is missing.
"""

import argparse
import os
from pathlib import Path
import tempfile

from selftest import load

test262 = load(Path(__file__).with_name("test262.py"))

# Each entry is a way this runner could misreport, written down as a test it
# must classify correctly. A harness that reports PASS for everything looks like
# a conforming engine from its output alone, so each fixture below is a planted
# answer with the classification it must get, and these checks assert them.
#
# The ones that matter most are `must-fail` (a false assertion must be reported
# as FAIL, not CRASH or PASS) and the `negative-*` fixtures, the only proof
# that `negative:` is scored on both the phase and the error type rather than on
# "something was thrown".
PLANTED = [
    ("must-pass", test262.PASS, "", "assert.sameValue(1, 1);\n"),
    ("must-fail", test262.FAIL, "", "assert.sameValue(1, 2);\n"),
    (
        "negative-parse-correct",
        test262.PASS,
        "negative:\n  phase: parse\n  type: SyntaxError\n",
        "$DONOTEVALUATE();\nvar a = ;\n",
    ),
    (
        "negative-parse-wrong-type",
        test262.FAIL,
        "negative:\n  phase: parse\n  type: TypeError\n",
        "$DONOTEVALUATE();\nvar a = ;\n",
    ),
    (
        "negative-parse-nothing-thrown",
        test262.FAIL,
        "negative:\n  phase: parse\n  type: SyntaxError\n",
        "var a = 1;\n",
    ),
    (
        "negative-runtime-correct",
        test262.PASS,
        "negative:\n  phase: runtime\n  type: Test262Error\n",
        "throw new Test262Error('planted');\n",
    ),
    (
        "negative-runtime-wrong-phase",
        test262.FAIL,
        "negative:\n  phase: runtime\n  type: SyntaxError\n",
        "var a = ;\n",
    ),
    ("async-complete", test262.PASS, "flags: [async]\n", "$DONE();\n"),
    ("async-silent", test262.FAIL, "flags: [async]\n", "var never = 1;\n"),
    ("skipped-feature", test262.SKIP, "features: [cross-realm]\n", "$262.createRealm();\n"),
    ("timeout", test262.TIMEOUT, "", "while (true) { }\n"),
]


def default_args():
    """The arguments under pytest; skips the check when ct262 or the corpus is absent."""
    binary = Path(os.environ.get("CT262_BINARY", test262.DEFAULT_BINARY))
    corpus = test262.DEFAULT_CORPUS
    if not binary.exists() or not (corpus / "harness" / "assert.js").exists():
        import pytest

        pytest.skip(f"needs ct262 at {binary} (CT262_BINARY) and a corpus at {corpus}")
    return argparse.Namespace(binary=binary, corpus=corpus, timeout=3.0)


def make_test(name, want, meta, body):
    def test(args=None):
        args = args or default_args()
        with tempfile.TemporaryDirectory(prefix="ct262-selftest-") as scratch:
            root = Path(scratch)
            path = root / f"{name}.js"
            path.write_text(f"/*---\ndescription: planted\n{meta}---*/\n{body}")
            got = test262.run_test(test262.Test(path, root), args)
        assert got["status"] == want, f"{name}: want {want}, got {got['status']} {got['cause']}"

    return test


for case in PLANTED:
    globals()[f"test_{case[0].replace('-', '_')}"] = make_test(*case)
