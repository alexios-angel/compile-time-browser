"""The negative proofs for run-wpt.py, run by pytest and by `run-wpt.py --selftest`.

Each check runs ctdrive on one page in tools/wpt/selftest/. `--selftest` passes
its own driver and corpus; under pytest the checks use WPT_DRIVER (default
build/tools/ctdrive) and WPT_DIR, and skip when either is missing.
"""

import importlib.util
import os
from pathlib import Path
import shutil
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
SPEC = importlib.util.spec_from_file_location("run_wpt", Path(__file__).with_name("run-wpt.py"))
run_wpt = importlib.util.module_from_spec(SPEC)
sys.modules["run_wpt"] = run_wpt
SPEC.loader.exec_module(run_wpt)

# The instrument is not evidence until it has been seen to fail. Every fixture
# in tools/wpt/selftest/ has one required outcome, asserted here rather than
# read off a table by eye. A run of these that passes is what makes a run of the
# real corpus mean anything - and each of the four failure modes below has, at
# some point in this file's life, been reported as a pass by something.
SELFTESTS = [
    ("selftest/must-pass.html", run_wpt.Outcome.PASS, 2, {"PASS": 2}),
    ("selftest/must-fail.html", run_wpt.Outcome.FAIL, 2, {"PASS": 1, "FAIL": 1}),
    ("selftest/never-done.html", run_wpt.Outcome.TIMEOUT, None, None),
    ("selftest/throws-on-load.html", run_wpt.Outcome.HARNESS_ERROR, None, None),
    ("selftest/no-harness.html", run_wpt.Outcome.HARNESS_ERROR, 0, {}),
    ("selftest/testdriver.html", run_wpt.Outcome.PASS, 2, {"PASS": 2}),
]


def default_args():
    """Driver, corpus and memory cap under pytest, set up as main() sets them up.

    Skips the check when ctdrive or the corpus is absent.
    """
    driver = Path(os.environ.get("WPT_DRIVER", run_wpt.ROOT / "build" / "tools" / "ctdrive"))
    wpt = run_wpt.DEFAULT_WPT.expanduser()
    if not driver.exists() or not (wpt / "resources" / "testharness.js").exists():
        import pytest

        pytest.skip(f"needs ctdrive at {driver} (WPT_DRIVER) and a corpus at {wpt} (WPT_DIR)")
    run_wpt.install_testdriver_vendor(wpt)
    shutil.copyfile(
        run_wpt.HERE / "testharnessreport.js", wpt / "resources" / "testharnessreport.js"
    )
    return driver, wpt, 4096


def make_test(rel, want, want_subs, want_status):
    def test(driver=None, wpt=None, memory_mb=None):
        if driver is None:
            driver, wpt, memory_mb = default_args()
        page = run_wpt.SELFTEST_DIR / Path(rel).name
        plan = run_wpt.Plan(rel, page=page, timeout=run_wpt.TIMEOUT_NORMAL)
        got = run_wpt.run_one(plan, driver, wpt, memory_mb)
        seen = {}
        for one in got.subtests:
            seen[one.get("status")] = seen.get(one.get("status"), 0) + 1
        why = f"{rel}: got {got.status} {seen}; message {got.message[:200]}; log {got.log.strip()[-400:]}"
        assert got.status == want, why
        if want_subs is not None:
            assert len(got.subtests) == want_subs, why
        if want_status is not None:
            assert seen == want_status, why

    return test


for case in SELFTESTS:
    globals()[f"test_{Path(case[0]).stem.replace('-', '_')}"] = make_test(*case)
