"""Checks for check-workload.py, run by pytest and by `check-workload.py --self-test`.

Every check reads one planted tree: two scripts and a page, a plugin with three
probe stages, and one workload per verdict. It needs no build.
"""

import functools
import json
from pathlib import Path
import tempfile
import time
import types

from selftest import load

workload = load(Path(__file__).with_name("check-workload.py"))

# A plugin the checks load: one stage that fails on b*.js, one that hangs
# in a grandchild process, and one that reports MISSING.
PLUGIN = """
@stage("probe-check", "fails on scripts named b*.js", "rename it", needs="discover")
def probe_check(ctx, name):
    def body(item):
        bad = ctx.scripts[item]["path"].name.startswith("b")
        ctx.add(name, item, "FAIL" if bad else "PASS", "probe")
    ctx.each(name, list(ctx.scripts), body)


@stage("probe-sleep", "sleeps past its limit", "wake it", needs="discover")
def probe_sleep(ctx, name):
    # the grandchild holds the output pipe: only killing the process GROUP ends the read
    nap = "import subprocess, sys, time; "
    nap += "subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)']); "
    nap += "time.sleep(30)"
    def body(item):
        import sys
        ctx.run([sys.executable, "-c", nap], name, item)
        ctx.add(name, item, "PASS", "slept")
    ctx.each(name, list(ctx.scripts), body)


@stage("probe-missing", "a capability that does not exist yet", "build it", needs="discover")
def probe_missing(ctx, name):
    ctx.each(name, list(ctx.scripts), lambda item: ctx.add(name, item, "MISSING", "absent"))
"""


WORKLOADS = {
    "good": {"scripts": ["a.js"]},
    "bad": {"scripts": ["*.js"], "stages": ["discover", "probe-check", "native-run"]},
    "page": {"pages": ["page.html"], "stages": ["discover", "page-scripts", "probe-check"]},
    "slow": {
        "scripts": ["a.js"],
        "stages": ["discover", "probe-sleep", "probe-check"],
        "options": {"probe-sleep": {"timeout": 0.5}},
    },
    "unknown": {"scripts": ["a.js"], "stages": ["discover", "no-such-stage"]},
    "stale": {"scripts": ["missing.js"]},
    "gap": {"scripts": ["a.js"], "stages": ["discover", "probe-missing"]},
    "entry": {"scripts": [{"pattern": "a.js", "string_entries": ["f", "g"]}]},
}


@functools.cache
def planted():
    """Plant the tree once and check every workload in it; return what the checks read."""
    temp = tempfile.TemporaryDirectory()
    root = Path(temp.name)
    (root / "a.js").write_text("var a = 1;\n")
    (root / "b.js").write_text("var b = 2;\n")
    (root / "page.html").write_text('<p><script src="a.js"></script><script>var c;</script>')
    defs = root / "workloads"
    defs.mkdir()
    (defs / "probe.py").write_text(PLUGIN)
    defaults = {"schema": 1, "done_means": "test", "stages": ["discover", "probe-check"]}
    defaults["timeouts"] = {"command": 5, "item": 10, "total": 60}
    (defs / "defaults.json").write_text(json.dumps(defaults))
    for name, body in WORKLOADS.items():
        (defs / f"{name}.json").write_text(json.dumps(body))
    loaded = workload.load(defs)
    later = time.monotonic() + 60
    results = {name: workload.check(loaded[name], root, later, root) for name in WORKLOADS}
    # The directory lives as long as the cache: `temp` is kept in the result.
    return types.SimpleNamespace(temp=temp, root=root, loaded=loaded, results=results)


@functools.cache
def spent():
    """Check the slow workload with 0.3 s of budget left."""
    fixture = planted()
    return workload.check(
        fixture.loaded["slow"], fixture.root, time.monotonic() + 0.3, fixture.root
    )


def test_good_is_done():
    good = planted().results["good"]
    assert good["verdict"] == "DONE", good


def test_string_entries_workload_is_done():
    entry = planted().results["entry"]
    assert entry["verdict"] == "DONE", entry


def test_string_entries_joins_names():
    assert workload.string_entries({"string_entries": ["f", "g"]}) == "f,g"


def test_string_entries_defaults_to_empty():
    assert workload.string_entries({"path": planted().root / "a.js"}) == ""


def test_bad_first_gap_is_probe_check():
    bad = planted().results["bad"]
    assert bad["verdict"] == "NOT DONE" and bad["first_gap"]["stage"] == "probe-check", bad


def test_bad_stage_statuses():
    bad = planted().results["bad"]
    assert [s["status"] for s in bad["stages"]] == ["PASS", "FAIL", "PASS"], bad["stages"]


def test_page_inventories_its_scripts():
    page = planted().results["page"]
    assert page["inventory"]["scripts"] == 2 and page["verdict"] == "DONE", page


def slow_run():
    fixture = planted()
    started = time.monotonic()
    slow = workload.check(fixture.loaded["slow"], fixture.root, time.monotonic() + 60, fixture.root)
    return slow, time.monotonic() - started


def test_slow_times_out_promptly():
    slow, seconds = slow_run()
    assert slow["verdict"] == "TIMED OUT" and seconds < 5, slow


def test_slow_blocks_later_stage():
    slow, _ = slow_run()
    blocked = [r for r in slow["results"] if r["stage"] == "probe-check"]
    assert blocked and blocked[0]["status"] == "BLOCKED", slow["results"]


def test_unknown_stage_cannot_check():
    unknown = planted().results["unknown"]
    assert unknown["verdict"] == "CANNOT CHECK", unknown


def test_stale_pattern_cannot_check():
    stale = planted().results["stale"]
    assert stale["verdict"] == "CANNOT CHECK", stale


def test_missing_capability_is_first_gap():
    gap = planted().results["gap"]
    assert gap["verdict"] == "NOT DONE" and gap["first_gap"]["status"] == "MISSING", gap


def test_spent_budget_times_out():
    assert spent()["verdict"] == "TIMED OUT", spent()


def test_spent_budget_summary_counts_timeout():
    assert spent()["stages"][-1]["summary"].startswith("1 TIMEOUT"), spent()["stages"]


def report():
    results = planted().results
    return workload.finish([results["good"], results["bad"]], 60, time.monotonic())


def test_report_not_done_exit_and_text():
    assert report()["exit"] == 1 and "VERDICT: NOT DONE" in workload.render_text(report(), 5)


def test_report_timed_out_exit():
    assert workload.finish([spent()], 60, time.monotonic())["exit"] == 3


def test_report_round_trips_json():
    assert json.loads(json.dumps(report()))["schema"] == workload.SCHEMA
