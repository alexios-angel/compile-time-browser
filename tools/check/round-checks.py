#!/usr/bin/env python3
"""Print the focused checks that cover a change, for a validator to paste.

  tools/check/round-checks.py <base> <tip>     # the checks for git diff base..tip
  tools/check/round-checks.py --self-test       # checks the selection; no git, no build

For each file changed between <base> and <tip> it selects lit paths (under
ctcompile/test), CTests, build targets and commands:

- a file under a path in tools/check/round-checks.json's `prefixes` selects
  that entry (the longest matching prefix wins);
- a changed lit test (.mlir, .td or .test under ctcompile/test) with no entry
  selects itself, and a changed test helper selects its directory;
- `always` is added whenever anything was selected.

It prints a lit --filter, a ctest -R regex, the build targets, the commands
and an estimate of how many lit tests the filter selects at <tip>. The
selection stays focused: it exits 2 instead of printing a filter that would
select every lit test (a whole-tree run needs an explicit user request).
Exit 0 with "nothing to check" when no change maps to a check (only docs).
"""

import argparse
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
MAPPING = Path(__file__).resolve().with_name("round-checks.json")
TESTS = "ctcompile/test/"
LIT_SUFFIXES = (".mlir", ".td", ".test")  # lit.cfg.py's config.suffixes
LIT_EXCLUDED = "Inputs"  # lit.cfg.py's config.excludes


def is_lit_test(path):
    """Whether `path` (relative to ctcompile/test) is a lit test: a lit suffix,
    and no directory named Inputs on the way."""
    parts = PurePosixPath(path).parts
    return path.endswith(LIT_SUFFIXES) and LIT_EXCLUDED not in parts[:-1]


def select(changed, mapping):
    """The selection for the changed paths: a dict of sets (lit, ctests,
    targets, commands, notes) and the paths that selected nothing."""
    chosen = {key: set() for key in ("lit", "ctests", "targets", "commands", "notes")}
    unmapped, matched = [], False
    entries = sorted(mapping["prefixes"], key=lambda entry: len(entry["path"]), reverse=True)
    for path in changed:
        entry = next((entry for entry in entries if path.startswith(entry["path"])), None)
        if entry is not None:
            matched = True
            for key in chosen:
                chosen[key].update(entry.get(key, []))
            continue
        if path.startswith(TESTS):
            relative = path[len(TESTS) :]
            directory = str(PurePosixPath(relative).parent)
            if is_lit_test(relative) or directory != ".":
                matched = True
                chosen["lit"].add(relative if is_lit_test(relative) else directory + "/")
                continue
        unmapped.append(path)
    if matched:
        for key in chosen:
            chosen[key].update(mapping["always"].get(key, []))
    # A path inside a selected directory adds nothing.
    directories = sorted(p for p in chosen["lit"] if p.endswith("/"))
    chosen["lit"] = {
        p for p in chosen["lit"] if not any(p != d and p.startswith(d) for d in directories)
    }
    return chosen, unmapped


def lit_filter(paths):
    """lit's --filter regex for the selected paths."""
    parts = [re.escape(p) if p.endswith("/") else re.escape(p) + "$" for p in sorted(paths)]
    return "^ctcompile :: (" + "|".join(parts) + ")"


def covered(paths, tests):
    """The lit tests (paths relative to ctcompile/test) the selection runs."""
    return [
        test
        for test in tests
        if any(test == p or (p.endswith("/") and test.startswith(p)) for p in paths)
    ]


def report(chosen, tests):
    """The printed selection, or (None, why) when it must be refused."""
    lit = sorted(chosen["lit"])
    if any(p in ("", "/", "./") for p in lit):
        return None, "a selection names the whole lit tree"
    count = len(covered(lit, tests))
    if tests and count == len(tests):
        return None, f"the selection covers all {count} lit tests; narrow round-checks.json"
    out = []
    if lit:
        out += [f"lit ({count} of {len(tests)} tests):", f"  --filter='{lit_filter(lit)}'"]
    if chosen["ctests"]:
        out += ["ctest:", "  -R '^(" + "|".join(sorted(chosen["ctests"])) + ")$'"]
    if chosen["targets"]:
        out += ["build:", "  tools/remote-build.sh " + " ".join(sorted(chosen["targets"]))]
    if chosen["commands"]:
        out += ["commands:"] + [f"  {command}" for command in sorted(chosen["commands"])]
    if chosen["notes"]:
        out += ["notes:"] + [f"  {note}" for note in sorted(chosen["notes"])]
    return "\n".join(out), None


def git(*args):
    return subprocess.run(
        ["git", "-C", str(ROOT), *args], capture_output=True, text=True, check=True
    ).stdout


def self_test():
    mapping = {
        "always": {"lit": ["A/"], "targets": ["t"], "notes": ["n"]},
        "prefixes": [
            {"path": "ctcompile/lib/X/", "lit": ["X/"]},
            {"path": "ctcompile/lib/X/Narrow/", "lit": ["X/narrow.mlir"], "ctests": ["c"]},
            {"path": "ctcompile/test/shared.py", "lit": []},
        ],
    }
    tests = ["A/a.mlir", "X/x.mlir", "X/narrow.mlir", "X/Inputs/in.mlir", "Y/y.test", "Z/z.test"]
    tests = [t for t in tests if is_lit_test(t)]
    assert tests == ["A/a.mlir", "X/x.mlir", "X/narrow.mlir", "Y/y.test", "Z/z.test"], tests
    # A library file selects its entry, plus always.
    chosen, unmapped = select(["ctcompile/lib/X/a.cpp"], mapping)
    assert chosen["lit"] == {"X/", "A/"} and chosen["targets"] == {"t"} and not unmapped, chosen
    # The longest prefix wins.
    chosen, _ = select(["ctcompile/lib/X/Narrow/b.cpp"], mapping)
    assert chosen["lit"] == {"X/narrow.mlir", "A/"} and chosen["ctests"] == {"c"}, chosen
    # A changed lit test selects itself; a helper its directory; inside a
    # selected directory it adds nothing.
    chosen, _ = select(["ctcompile/test/Y/y.test", "ctcompile/test/Z/helper.py"], mapping)
    assert chosen["lit"] == {"Y/y.test", "Z/", "A/"}, chosen
    chosen, _ = select(["ctcompile/test/A/a.mlir"], mapping)
    assert chosen["lit"] == {"A/"}, chosen
    # A shared helper with an empty entry selects only always, not its tree.
    chosen, _ = select(["ctcompile/test/shared.py"], mapping)
    assert chosen["lit"] == {"A/"}, chosen
    # Docs select nothing, and always is not added.
    chosen, unmapped = select(["ctcompile/docs/x.md"], mapping)
    assert not any(chosen.values()) and unmapped == ["ctcompile/docs/x.md"], chosen
    # The printed filter selects exactly the covered tests.
    chosen, _ = select(["ctcompile/test/Y/y.test", "ctcompile/lib/X/Narrow/b.cpp"], mapping)
    text, why = report(chosen, tests)
    assert why is None and "lit (3 of 5 tests)" in text, text
    pattern = re.compile(lit_filter(chosen["lit"]))
    hits = [t for t in tests if pattern.search("ctcompile :: " + t)]
    assert hits == covered(chosen["lit"], tests) == ["A/a.mlir", "X/narrow.mlir", "Y/y.test"], hits
    # A selection covering every test, or naming the root, is refused.
    whole = {"lit": {"A/", "X/", "Y/", "Z/"}, "ctests": set(), "targets": set()}
    whole.update(commands=set(), notes=set())
    assert report(whole, tests)[0] is None
    assert report(dict(whole, lit={""}), tests)[0] is None
    print("self-test passed")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("base", nargs="?")
    parser.add_argument("tip", nargs="?")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not (args.base and args.tip):
        parser.error("give <base> <tip>, or --self-test")
    changed = git("diff", "--name-only", f"{args.base}..{args.tip}").split()
    mapping = json.loads(MAPPING.read_text())
    tests = [
        path[len(TESTS) :]
        for path in git("ls-tree", "-r", "--name-only", args.tip, TESTS).split()
        if is_lit_test(path[len(TESTS) :])
    ]
    chosen, unmapped = select(changed, mapping)
    print(f"round-checks: {len(changed)} changed file(s) in {args.base}..{args.tip}")
    if not any(chosen.values()):
        print("nothing to check: no changed file maps to a check")
        return 0
    text, why = report(chosen, tests)
    if why:
        print(f"round-checks: refused: {why}", file=sys.stderr)
        return 2
    print(text)
    if unmapped:
        print(f"unmapped ({len(unmapped)}; add them to round-checks.json if they need checks):")
        print("".join(f"  {path}\n" for path in unmapped), end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
