"""Check appended classic-script boundaries through the real page reference."""

import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    work = args.work
    before = "before 17 undefined\n"
    after = "after 17 19 36 1 1 kept 1\nsloppy 23\n"
    cases = [
        (
            "appended",
            "listener.html",
            ["a.js", "b.js"],
            before + "onerror 1 true\nerror 1 true\n" + after,
            [("a.js", "first-script")],
        ),
        (
            "inline",
            "inline-error.html",
            ["b.js"],
            before + "onerror 1 false\nerror 1 false\ninline-next 17\n" + after,
            [("inline-error.html", "inline-script")],
        ),
        (
            "syntax",
            "listener.html",
            ["bad.js", "good.js", "b.js"],
            "onerror 1 false\nerror 1 false\n" + before + after,
            [("bad.js", "parse error: expression - at 1:14")],
        ),
        (
            "multiple-errors",
            "listener.html",
            ["bad.js", "a.js", "b.js"],
            "onerror 1 false\nerror 1 false\n"
            + before
            + "onerror 2 true\nerror 2 true\n"
            + after.replace("36 1 1 kept 1", "36 2 2 kept 2"),
            [("bad.js", "parse error: expression - at 1:14"), ("a.js", "first-script")],
        ),
        (
            "success",
            "listener.html",
            ["good.js", "b.js"],
            before + after.replace("36 1 1 kept 1", "36 0 0 kept null"),
            [],
        ),
    ]
    for name, page, scripts, expected, errors in cases:
        command = [args.reference, "--page", str(work / page)]
        for script in scripts:
            command += ["--script", str(work / script)]
        result = subprocess.run(command, text=True, capture_output=True, timeout=30, check=False)
        expected_status = 1 if errors else 0
        assert result.returncode == expected_status, (name, result)
        assert result.stdout == expected, (name, expected, result)
        diagnostics = [
            line for line in result.stderr.splitlines() if line.startswith("native reference:")
        ]
        assert len(diagnostics) == len(errors), (name, errors, result)
        for diagnostic, (filename, message) in zip(diagnostics, errors):
            assert str(work / filename) in diagnostic and message in diagnostic, (name, result)
        if not errors:
            assert not result.stderr, (name, result)
        print(f"{name}: status={expected_status}, complete ordered console and error reporting")


if __name__ == "__main__":
    main()
