"""Checks for check-emitter-merge.py, run by pytest and by its --self-test.

A tiny four-line merge: upstream changes line 2, the baseline fork changes
line 4, and the candidate also changes line 2 and opens a function above it.
"""

from pathlib import Path

from selftest import load

merge = load(Path(__file__).with_name("check-emitter-merge.py"))

BASE = "int a;\nint b;\nint c;\nint d;\n"
UPSTREAM = "int a;\nint B;\nint c;\nint d;\n"
BASELINE = "int a;\nint b;\nint c;\nint d2;\n"
CANDIDATE = "Emitter::Emitter(int x) {\nint a;\nint b2;\nint c;\nint d2;\n"


def candidate_report():
    return merge.analyse(CANDIDATE, BASELINE, BASE, UPSTREAM, 12)


def test_counts_candidate_conflict():
    report = candidate_report()
    assert report["candidate_conflicts"] == 1, report


def test_counts_no_baseline_conflict():
    report = candidate_report()
    assert report["baseline_conflicts"] == 0, report


def test_marks_conflict_new():
    report = candidate_report()
    conflict = report["conflicts"][0]
    assert conflict["status"] == "new" and report["new"] == [1], conflict


def test_locates_conflict_in_function():
    conflict = candidate_report()["conflicts"][0]
    assert conflict["fork_line"] == 3 and conflict["function"] == "Emitter::Emitter", conflict


def test_detects_resolution():
    assert merge.analyse(BASELINE, CANDIDATE, BASE, UPSTREAM, 12)["resolved"], "resolution missed"
