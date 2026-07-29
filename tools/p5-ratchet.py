#!/usr/bin/env python3
"""How far p5.js gets through the engine, and what is stopping it.

tests/p5_ratchet.cpp measures a LEVEL and a BLOCKER; tests/p5-ratchet.txt
records them; this drives the loop around both.

    tools/p5-ratchet.py                 build, measure, show the blocker in context
    tools/p5-ratchet.py --advance       record what was just measured
    tools/p5-ratchet.py --bisect color  carve one rollup module out and measure THAT
    tools/p5-ratchet.py --survey        every module, ranked by what is blocking it

The bundle is 4.5 MB in one IIFE, so a failure reported against it is a needle
in a haystack. --bisect extracts a single module - p5 is a rollup bundle, so
its modules are still `function NAME(p5, fn) { ... }` at a known indent - and
pads the fragment with blank lines so every reported line number is still the
line number in p5.js. A 3,000-line reproducer that points at the right place.

--advance is the only thing that writes the recorded file. Deliberately: a test
that edits its own expectations cannot fail.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUNDLE = ROOT / "examples/assets/p5.js"
RECORD = ROOT / "tests/p5-ratchet.txt"
TEST = ROOT / "build/src/tests/ctbrowser-test-p5_ratchet"

# `  function color$1(p5, fn, lifecycles){` - rollup's module wrappers, at the
# one indent the bundle uses for them.
MODULE = re.compile(r"^  function ([A-Za-z_$][\w$]*)\(p5, fn[^)]*\)\s*\{", re.M)

LEVELS = ["unread", "read", "lexed", "parsed", "compiled",
          "fits the bytecode", "top level ran", "defines p5", "loads as a page",
          "constructs", "setup ran", "draw ran", "paints what the sketch drew"]


def build():
    """Build just the ratchet, so the inner loop is seconds rather than a minute."""
    r = subprocess.run(
        ["cmake", "--build", "--preset", "default",
         "--target", "ctbrowser-test-p5_ratchet"],
        cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit("p5-ratchet: build failed")


def run(args=()):
    """Run the measurement. Its exit code is the pawl's verdict, not an error here."""
    r = subprocess.run([str(TEST), *args], cwd=ROOT, capture_output=True, text=True)
    return r.stdout + r.stderr, r.returncode


def parse_output(out):
    """The measured level and blocker, for each of the two modes.

    p5-min is the ladder the bundle is normally measured on; p5-full runs the
    same ladder with IS_MINIFIED left undefined, so the Friendly Error System
    and i18next's setup are in play. Returns (level, blocker, full, full_blocker)
    with the full pair None when only one mode was run - --bisect and --survey
    measure a fragment, which has no modes.
    """
    level, blocker, full, full_blocker = None, None, None, None
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("FULL LEVEL "):
            full = int(line.split()[2].split("/")[0])
        elif line.startswith("FULL BLOCKER "):
            full_blocker = line[len("FULL BLOCKER "):]
        elif line.startswith("LEVEL "):
            level = int(line.split()[1].split("/")[0])
        elif line.startswith("BLOCKER "):
            blocker = line[len("BLOCKER "):]
    return level, blocker, full, full_blocker


def show_context(blocker, path=BUNDLE, span=10):
    """Print the source around a `file:LINE:COL` mentioned in the blocker.

    The whole point of the offset work in the parser: a blocker that names a
    position but makes you go and find it is half a diagnostic.
    """
    hit = re.search(r"([\w.$-]+):(\d+):(\d+)", blocker or "")
    if not hit:
        return
    line_no, col = int(hit.group(2)), int(hit.group(3))
    try:
        lines = path.read_text(errors="replace").splitlines()
    except OSError:
        return
    lo, hi = max(1, line_no - span), min(len(lines), line_no + span)
    print(f"\n  {path.name}:{line_no}:{col}")
    for n in range(lo, hi + 1):
        mark = "->" if n == line_no else "  "
        print(f"  {mark} {n:>6}  {lines[n - 1]}")
        if n == line_no:
            print(f"       {'':>6}  {' ' * (col - 1)}^")
    print()


def modules(text):
    """Every rollup module in the bundle: name -> (start_offset, end_offset, start_line)."""
    found = {}
    for hit in MODULE.finditer(text):
        open_brace = text.index("{", hit.end() - 1)
        depth, i = 0, open_brace
        # Brace matching, not a parser. It is wrong inside a string or a regex
        # literal containing an unbalanced brace; it has been right on every
        # module in this bundle, and a reproducer that is occasionally short is
        # still worth having.
        while i < len(text):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        found[hit.group(1)] = (hit.start(), i + 1, text.count("\n", 0, hit.start()) + 1)
    return found


def do_bisect(name):
    text = BUNDLE.read_text(errors="replace")
    found = modules(text)
    if name not in found:
        print(f"p5-ratchet: no module {name!r}. The bundle has {len(found)}:\n")
        for chunk in sorted(found):
            print(f"    {chunk}")
        sys.exit(1)

    start, end, start_line = found[name]
    fragment = ROOT / "build" / f"p5-module-{name}.js"
    fragment.parent.mkdir(parents=True, exist_ok=True)
    # Padded, so a reported line is still p5.js's line.
    fragment.write_text("\n" * (start_line - 1) + text[start:end] + "\n")

    body = text[start:end]
    print(f"p5-ratchet: {name} is p5.js:{start_line}, {len(body)} bytes, "
          f"{body.count(chr(10)) + 1} lines -> {fragment.relative_to(ROOT)}\n")
    build()
    out, _ = run([str(fragment)])
    print(out)
    _, blocker, _, _ = parse_output(out)
    show_context(blocker)


def carve(text, name, where):
    """Write one module to build/ as a padded fragment. Returns the path."""
    start, end, start_line = where
    fragment = ROOT / "build" / f"p5-module-{name}.js"
    fragment.parent.mkdir(parents=True, exist_ok=True)
    fragment.write_text("\n" * (start_line - 1) + text[start:end] + "\n")
    return fragment


def do_survey():
    """Measure every module, and rank the blockers by how many modules they stop.

    One number says how far the bundle gets. This says what to work on next:
    the bundle fails at its first blocker, but the modules fail INDEPENDENTLY,
    so counting them turns a single stop into a ranked work list.
    """
    text = BUNDLE.read_text(errors="replace")
    found = modules(text)
    build()

    results = []
    for name, where in sorted(found.items(), key=lambda kv: kv[1][2]):
        out, _ = run([str(carve(text, name, where))])
        level, blocker, _, _ = parse_output(out)
        results.append((name, where[2], level, blocker or ""))

    ceiling = max((lvl for _, _, lvl, _ in results if lvl is not None), default=0)
    blocked = [r for r in results if r[2] is not None and r[2] < ceiling]
    print(f"\n  {len(found)} modules, {len(blocked)} of them below level {ceiling}\n")

    # Group by the SHAPE of the blocker, not its text: a position makes every
    # one unique, and the count is the whole point. Only the parenthetical that
    # quotes SOURCE varies between modules - `(/void\s+main/)` and the like - so
    # that is the only one collapsed. Blanking every parenthetical would turn
    # "spread in a call, `f(...args)`" into "`f(...)`", which reads as a
    # different construct entirely.
    def shape(blocker):
        s = re.sub(r"[\w.$-]+:\d+:\d+", "<position>", blocker)
        return re.sub(r"\((/|['\"])[^)]*\)\s*$", "(<source>)", s).strip()

    tally = {}
    for name, line, level, blocker in blocked:
        tally.setdefault(shape(blocker), []).append((name, line, level, blocker))

    for kind, hits in sorted(tally.items(), key=lambda kv: -len(kv[1])):
        print(f"  {len(hits):>3} modules  {kind}")
        for name, line, level, blocker in hits:
            position = re.search(r"[\w.$-]+:\d+:\d+", blocker)
            print(f"        level {level}  {name:<22} p5.js:{line}"
                  f"{'  -> ' + position.group(0) if position else ''}")
        print()


def do_advance():
    out, _ = run()
    level, blocker, full, full_blocker = parse_output(out)
    if level is None:
        sys.exit("p5-ratchet: could not read a level from the test:\n" + out)

    old = RECORD.read_text() if RECORD.exists() else ""

    def forward(key, measured):
        """Refuse to move a recorded number backwards. The pawl turns one way."""
        was = re.search(rf"^{key}=(\d+)$", old, re.M)
        if was and measured is not None and int(was.group(1)) > measured:
            sys.exit(f"p5-ratchet: refusing to advance {key} BACKWARDS, "
                     f"{was.group(1)} -> {measured}.\nThe pawl only turns one way. "
                     f"Fix the regression.")

    forward("level", level)
    forward("full-level", full)

    text = re.sub(r"^level=.*$", f"level={level}", old, flags=re.M)
    text = re.sub(r"^blocker=.*$", f"blocker={blocker or ''}", text, flags=re.M)
    if full is not None:
        # Added rather than replaced when the file predates the second mode, so
        # an older record is upgraded instead of silently losing the number.
        if re.search(r"^full-level=", text, re.M):
            text = re.sub(r"^full-level=.*$", f"full-level={full}", text, flags=re.M)
            text = re.sub(r"^full-blocker=.*$", f"full-blocker={full_blocker or ''}", text,
                          flags=re.M)
        else:
            text = text.rstrip() + f"\nfull-level={full}\nfull-blocker={full_blocker or ''}\n"
    RECORD.write_text(text)
    name = LEVELS[level] if level < len(LEVELS) else "?"
    print(f"p5-ratchet: recorded level={level} ({name})")
    if blocker:
        print(f"            blocker={blocker}")
    if full is not None:
        full_name = LEVELS[full] if full < len(LEVELS) else "?"
        print(f"            full-level={full} ({full_name})")
        if full_blocker:
            print(f"            full-blocker={full_blocker}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--advance", action="store_true",
                    help="record the measured level and blocker in tests/p5-ratchet.txt")
    ap.add_argument("--bisect", metavar="MODULE",
                    help="measure one rollup module instead of the whole bundle")
    ap.add_argument("--survey", action="store_true",
                    help="measure every module and rank the blockers by module count")
    args = ap.parse_args()

    if not BUNDLE.exists():
        sys.exit(f"p5-ratchet: {BUNDLE.relative_to(ROOT)} is missing")

    if args.bisect:
        do_bisect(args.bisect)
        return
    if args.survey:
        do_survey()
        return
    build()
    if args.advance:
        do_advance()
        return

    out, code = run()
    print(out)
    _, blocker, _, _ = parse_output(out)
    show_context(blocker)
    if code != 0:
        print("The ratchet is not satisfied. If this is progress, "
              "run tools/p5-ratchet.py --advance")
    sys.exit(code)


if __name__ == "__main__":
    main()
