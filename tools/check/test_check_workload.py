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


@stage("probe-binary", "a native binary that prints a=1", "write it", needs="discover")
def probe_binary(ctx, name):
    def body(item):
        ctx.artifacts[item] = {"binaries": [ctx.root / "native.sh"]}
        ctx.add(name, item, "PASS", "probe")
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
    "globals": {"scripts": ["a.js"], "stages": ["discover", "probe-binary", "script-run"]},
    "vacuous": {"scripts": ["b.js"], "stages": ["discover", "probe-binary", "script-run"]},
    "unnamed-skip": {
        "scripts": [{"pattern": "b.js", "script-run": {"skip": "no globals"}}],
        "stages": ["discover", "probe-binary", "script-run"],
    },
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
    # script-run's reference prints a=1 for a.js and nothing for b.js; the
    # native binary prints a=1.
    reference = root / "ctcompile/test/ctcompile-test-native-reference"
    reference.parent.mkdir(parents=True)
    reference.write_text('#!/bin/sh\ncase "$1" in */a.js) echo a=1;; esac\n')
    (root / "native.sh").write_text("#!/bin/sh\necho a=1\n")
    for program in (reference, root / "native.sh"):
        program.chmod(0o755)
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


def test_globals_match_reference():
    globals_ = planted().results["globals"]
    assert globals_["verdict"] == "DONE", globals_


def test_vacuous_comparison_fails():
    results = planted().results["vacuous"]["results"]
    vacuous = [r for r in results if r["stage"] == "script-run"]
    assert vacuous[0]["status"] == "FAIL" and "no globals" in vacuous[0]["summary"], vacuous


def test_opt_out_without_ctest_is_refused():
    # An opt-out that names no CTest of the workload is refused.
    results = planted().results["unnamed-skip"]["results"]
    skip = [r for r in results if r["stage"] == "script-run"]
    assert skip[0]["status"] == "FAIL" and "opt-out" in skip[0]["summary"], skip


def test_join_refuses_hoisted_probe_declaration():
    # The probe declares a top-level function the library mentions: joined, it
    # hoists above the library, so the join is refused.
    library = "later();\nvar untouched = 1;\n"
    probe = "function later() { return 1; }\nlater();\n"
    problem = workload.join_problem(library, probe)
    assert problem and "later" in problem and "hoist" in problem


def test_join_refuses_comma_list_declaration():
    library = "later();\n"
    probe = "var a = 1, later = 2;\n"
    problem = workload.join_problem(library, probe)
    assert problem and "later" in problem


def test_join_refuses_class_declaration():
    library = "later();\n"
    probe = "class later {}\n"
    problem = workload.join_problem(library, probe)
    assert problem and "later" in problem


def test_join_refuses_generator_declaration():
    library = "later();\n"
    probe = "function* later() { yield 1; }\n"
    problem = workload.join_problem(library, probe)
    assert problem and "later" in problem


def test_join_refuses_destructuring_declaration():
    library = "later();\n"
    probe = "let {later} = o;\n"
    problem = workload.join_problem(library, probe)
    assert problem and "later" in problem


def test_join_refuses_dollar_named_declaration():
    # `$` is an identifier character: `\b` never matches next to it.
    library = "$later();\n"
    probe = "var $later = 1;\n"
    problem = workload.join_problem(library, probe)
    assert problem and "later" in problem


def test_join_refuses_probe_with_regex_literal():
    # A quote inside a regex literal would blank the rest of the probe as a
    # string and hide the declaration, so the scan refuses instead.
    library = "later();\n"
    probe = "var r = /[\"']/; var later = 1;\n"
    problem = workload.join_problem(library, probe)
    assert problem and "regular expression" in problem


def test_join_refuses_asi_before_a_declaration():
    library = "console.log(typeof later);\n"
    probe = "var a = 1\nfunction later() { return 1; }\n"
    problem = workload.join_problem(library, probe)
    assert problem and "later" in problem


def test_join_refuses_renamed_destructuring():
    library = "console.log(typeof later);\n"
    probe = "let {source: later} = {source: 1};\n"
    assert workload.join_problem(library, probe)


def test_join_refuses_nested_destructuring():
    library = "console.log(typeof later);\n"
    probe = "let {outer: {later}} = {outer: {later: 1}};\n"
    assert workload.join_problem(library, probe)


def test_join_refuses_block_var():
    library = "console.log(later);\n"
    probe = "if (false) { var later = 1; }\n"
    problem = workload.join_problem(library, probe)
    assert problem and "later" in problem


def test_join_refuses_library_template_reference():
    library = "console.log(`${later}`);\n"
    probe = "let later = 1;\n"
    problem = workload.join_problem(library, probe)
    assert problem and "later" in problem


def test_join_refuses_library_return_regex_hiding_code():
    library = 'function f() { return /"/.test(""); }\nconsole.log(typeof later);\n'
    probe = "let later = 1;\n"
    problem = workload.join_problem(library, probe)
    assert problem and "later" in problem


def test_join_refuses_probe_templates():
    assert workload.join_problem("var a = 1;", "var b = `${a}`;")


def test_join_refuses_escaped_identifiers():
    library = "console.log(lat" + chr(92) + "u0065r);"
    probe = "let lat" + chr(92) + "u0065r = 1;"
    assert workload.join_problem(library, "let later = 1;")
    assert workload.join_problem("console.log(later);", probe)


def test_join_refuses_default_parameter_binding():
    library = "console.log(typeof later);"
    probe = "function unrelated(arg = function later() {}) {}"
    problem = workload.join_problem(library, probe)
    assert problem and "later" in problem


def test_join_refuses_ambiguous_directives():
    library = '"use strict" + ""; console.log(this);'
    probe = '"use strict"; console.log(this);'
    assert workload.join_problem(library, probe)
    assert workload.join_problem('"use strict"\nvar a = 1;', '"use strict"; var b = 1;')


def test_join_refuses_computed_global_properties():
    for global_object in ("globalThis", "window", "self"):
        library = f'console.log({global_object}["later"]);'
        problem = workload.join_problem(library, "var later = 1;")
        assert problem and "computed global-property" in problem
    assert workload.join_problem("window[key];", "function later() {}")


def test_join_refuses_global_object_keys():
    problem = workload.join_problem("console.log(Object.keys(globalThis));", "var later = 1;")
    assert problem and "global reflection" in problem


def test_join_refuses_global_own_property_names():
    problem = workload.join_problem("Object.getOwnPropertyNames(window);", "var later = 1;")
    assert problem and "global reflection" in problem


def test_join_refuses_global_reflect_own_keys():
    problem = workload.join_problem("Reflect.ownKeys(self);", "var later = 1;")
    assert problem and "global reflection" in problem


def test_join_refuses_global_for_in():
    library = "for (var key in globalThis) console.log(key);"
    problem = workload.join_problem(library, "var later = 1;")
    assert problem and "global reflection" in problem


def test_join_refuses_unbalanced_probe():
    assert workload.join_problem("var a = 1;", "function b() {")


def test_join_refuses_strictness_mismatch():
    # Only the library says "use strict": one joined script would keep the
    # library's prologue and make the probe strict too.
    library = '"use strict";\nvar a = 1;\n'
    probe = "var b = 2;\n"
    problem = workload.join_problem(library, probe)
    assert problem and "directive" in problem and "use strict" in problem


def test_join_allows_matching_strictness():
    library = '"use strict";\nvar a = 1;\n'
    probe = '"use strict";\nvar b = 2;\n'
    assert workload.join_problem(library, probe) is None


def test_join_allows_the_bootstrap_probe():
    # The Bootstrap bundle has no directive prologue and never mentions the
    # probe's bootstrap_probe_* functions, so its join stays allowed.
    root = Path(__file__).resolve().parents[2]
    library = (root / "ctbrowser/vendor/bootstrap/bootstrap.bundle.js").read_text()
    probe = (root / "tools/check/workloads/bootstrap-probe.js").read_text()
    assert workload.join_problem(library, probe) is None


def test_join_allows_the_fixture_with_the_bootstrap_probe():
    root = Path(__file__).resolve().parents[2]
    library = (root / "ctcompile/test/CTNative/Fixtures/ControlFlow/functions.js").read_text()
    probe = (root / "tools/check/workloads/bootstrap-probe.js").read_text()
    assert workload.join_problem(library, probe) is None


def test_join_refuses_sloppy_block_function():
    # Annex B gives a sloppy block function a script-level var binding.
    library = "later();\n"
    probe = "if (true) { function later() { return 1; } }\n"
    problem = workload.join_problem(library, probe)
    assert problem and "later" in problem


def test_join_ignores_strict_block_declarations():
    library = '"use strict"; console.log(typeof later);\n'
    for declaration in ("let later = 1;", "function later() { return 1; }"):
        probe = f'"use strict"; if (true) {{ {declaration} }}\n'
        assert workload.join_problem(library, probe) is None


def test_join_ignores_function_local_declarations():
    library = "later();\n"
    probe = "function unrelated() { var later = 1; function nested() {} }\n"
    assert workload.join_problem(library, probe) is None


def test_join_ignores_names_only_in_strings_or_comments():
    # A mention inside a string or a comment is not a use of the name.
    library = 'var s = "later"; // later();\n'
    probe = "function later() { return 1; }\n"
    assert workload.join_problem(library, probe) is None


def test_throwing_library_reference_cannot_report_done():
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        library, probe, page = (root / name for name in ("library.js", "probe.js", "page.html"))
        library.write_text('throw new Error("stop");\n')
        probe.write_text('console.log("probe ran");\n')
        page.write_text("<p>test</p>\n")
        reference = root / "ctcompile/test/ctcompile-test-native-reference"
        reference.parent.mkdir(parents=True)
        # The real reference exits nonzero when an explicit --script throws,
        # even if earlier page scripts already printed matching output.
        reference.write_text("#!/bin/sh\necho 'page ran'\necho 'library threw' >&2\nexit 1\n")
        reference.chmod(0o755)
        config = {
            "name": "throwing-library",
            "stages": ["script-run"],
            "timeouts": {"command": 5, "item": 10, "total": 60},
        }
        ctx = workload.Context(config, root, time.monotonic() + 60, root)
        ctx.pages = [page]
        ctx.scripts = {"script:library.js": {"path": library, "script_run": {"probe": probe.name}}}
        workload.script_run(ctx, "script-run")
        result = workload.summarize(ctx)
        assert result["verdict"] == "NOT DONE", result
        assert ctx.results[0]["status"] == "FAIL", ctx.results
        assert "VM reference failed probing" in ctx.results[0]["summary"], ctx.results
        assert "library threw" in ctx.results[0]["details"], ctx.results


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
