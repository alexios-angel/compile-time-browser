#!/usr/bin/env python3
"""The loop around every corpus ratchet and API surface: build, measure, record.

ctbrowser/test/corpus/<dir>/<corpus>_ratchet.cpp measures how FAR a corpus gets
(a LEVEL and a BLOCKER); <corpus>_api.cpp measures how WIDE the working surface
is (which probes pass). The .txt next to each records it, and --advance is the
only thing that writes the record. Deliberately: a test that edits its own
expectations cannot fail.

    tools/corpus/ratchet.py p5 ratchet                 build, measure, show the blocker in context
    tools/corpus/ratchet.py p5 ratchet --advance       record what was just measured
    tools/corpus/ratchet.py p5 ratchet --bisect color  carve one rollup module out and measure THAT
    tools/corpus/ratchet.py p5 ratchet --survey        every module, ranked by what is blocking it
    tools/corpus/ratchet.py p5 api                     build, run, show the failures
    tools/corpus/ratchet.py p5 api --advance           record what is passing now
    tools/corpus/ratchet.py p5 api --coverage          what no probe mentions - the work queue
    tools/corpus/ratchet.py p5 api --only shape        run and report one module

tools/corpus/<corpus>-ratchet.py and <corpus>-api.py are shims onto this, so
the names the tests print in their ADVANCE lines keep working.

--bisect and --survey are p5-only: that bundle failed at the LANGUAGE rungs and
a 4.5 MB IIFE makes a parse error a needle in a haystack, so a rollup module
(`function NAME(p5, fn) { ... }` at a known indent) is carved out and padded
with blank lines so every reported line number is still the line in p5.js. The
other corpora clear the language rungs, so every failure they have had is a
runtime one a fragment cannot reproduce.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
# The test binary resolves its inputs against the ctbrowser PROJECT directory,
# which is where ctest runs it from too.
ENGINE = ROOT / "ctbrowser"

# corpus -> (test/corpus subdirectory, vendored bundle or None, rung names,
#            coverage kind, the doc the `unscoped` probe module points at)
CORPORA = {
    "p5": ("p5", "ctbrowser/vendor/p5/p5.js",
           ["unread", "read", "lexed", "parsed", "compiled", "fits the bytecode",
            "top level ran", "defines p5", "loads as a page", "constructs", "setup ran",
            "draw ran", "paints what the sketch drew"],
           "functions", None),
    "phaser": ("phaser", "ctbrowser/vendor/phaser/phaser.js",
               ["unread", "read", "lexed", "parsed", "compiled", "runs as a page",
                "defines Phaser", "constructs a Game", "runs create()", "runs update()",
                "paints what the scene drew"],
               "namespaces", None),
    "babylon": ("babylon", "ctbrowser/vendor/babylon/babylon.js",
                ["nothing", "a scene renders", "a texture samples",
                 "two meshes with two materials", "an animation moves the picture",
                 "a directional light shades", "specular highlights", "alpha blending",
                 "a post-process runs", "a shadow lands", "a PBR material renders",
                 "glTF import", "the GUI draws"],
                "modules", "ctbrowser/docs/plans/babylon.md"),
    "webgl2": ("webgl2", None,
               ["nothing", "makes a webgl2 context", "has the WebGL 2 constants",
                "compiles #version 300 es", "vertex array objects work",
                "accepts instanced drawing", "an instanced draw reaches the pixels",
                "the WebGL 1 extensions expose the same thing",
                "Phaser's WebGL renderer paints on it"],
               "modules", "ctbrowser/docs/history/webgl2.md"),
    "module": ("modules", None,
               ["nothing", "import/export parse", "one module runs in its own scope",
                "an importer sees an export", "imported bindings are live", "a cycle resolves",
                "<script type=module> runs on a page", "relative specifiers resolve",
                "dynamic import() works", "Babylon's ES build boots"],
               None, None),
}

# `  function color$1(p5, fn, lifecycles){` - rollup's module wrappers, at the
# one indent the p5 bundle uses for them.
MODULE = re.compile(r"^  function ([A-Za-z_$][\w$]*)\(p5, fn[^)]*\)\s*\{", re.M)
# `    fn.background = function (...)` - p5's public surface as the bundle itself
# declares it, so the denominator tracks the library instead of drifting behind it.
PUBLIC = re.compile(r"^\s+fn\.([a-zA-Z][A-Za-z0-9_$]*)\s*=", re.M)
# Phaser's root export. ANCHORED TO `var Phaser = {`: the same line shape appears
# in every one of the 1763 webpack modules' export objects, and unanchored it
# counted 1131 "namespaces".
ROOT_EXPORT = re.compile(r"^var Phaser = \{(.*?)^\};", re.M | re.S)
NAMESPACE = re.compile(r"^    ([A-Z][A-Za-z0-9_$]*): __webpack_require__\(\d+\),?$", re.M)


class Corpus:
    def __init__(self, name, mode):
        subdir, bundle, self.rungs, self.coverage, self.unscoped_doc = CORPORA[name]
        self.name, self.mode = name, mode
        self.tag = f"{name}-{mode}"
        self.bundle = ROOT / bundle if bundle else None
        self.record = ROOT / "ctbrowser/test/corpus" / subdir / f"{name}-{mode}.txt"
        self.probes = ROOT / "ctbrowser/test/corpus" / subdir / f"{name}-api-probe.js"
        self.target = f"ctbrowser-test-{name}_{mode}"
        self.test = ROOT / "build/test" / self.target

    def build(self):
        """Build just the one test, so the inner loop is seconds rather than a minute."""
        r = subprocess.run(["cmake", "--build", "--preset", "default", "--target", self.target],
                           cwd=ENGINE, capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(r.stdout + r.stderr)
            sys.exit(f"{self.tag}: build failed")

    def run(self, args=()):
        """Run the measurement. Its exit code is the pawl's verdict, not an error here."""
        r = subprocess.run([str(self.test), *args], cwd=ENGINE, capture_output=True, text=True)
        return r.stdout + r.stderr, r.returncode

    def rung(self, level):
        return self.rungs[level] if level < len(self.rungs) else "?"


# --- ratchet: how FAR ---------------------------------------------------------

def parse_ratchet(out):
    """(level, blocker, full_level, full_blocker) from the test's own report.

    The `full` pair is p5 only - the same ladder with IS_MINIFIED left undefined
    - and None for every other corpus and for a carved fragment.
    """
    level, blocker, full, full_blocker = None, None, None, None
    for line in out.splitlines():
        line = line.strip()
        if hit := re.match(r"(FULL |BABYLON )?LEVEL (\d+)/", line):
            if hit.group(1) == "FULL ":
                full = int(hit.group(2))
            else:
                level = int(hit.group(2))
        elif line.startswith("FULL BLOCKER "):
            full_blocker = line[len("FULL BLOCKER "):]
        elif hit := re.match(r"(?:BLOCKER|blocked by:)\s*(.*)$", line):
            blocker = hit.group(1)
    return level, blocker, full, full_blocker


def show_context(blocker, path, span=10):
    """Print the source around a `file:LINE:COL` mentioned in the blocker.

    A blocker that names a position but makes you go and find it is half a
    diagnostic.
    """
    hit = re.search(r"([\w.$-]+):(\d+):(\d+)", blocker or "")
    if not hit or path is None:
        return
    line_no, col = int(hit.group(2)), int(hit.group(3))
    try:
        lines = path.read_text(errors="replace").splitlines()
    except OSError:
        return
    if line_no > len(lines):
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
        # module in this bundle.
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


def carve(c, text, name, where):
    """Write one module to build/ as a padded fragment, so a reported line is still the bundle's."""
    start, end, start_line = where
    fragment = ROOT / "build" / f"{c.name}-module-{name}.js"
    fragment.parent.mkdir(parents=True, exist_ok=True)
    fragment.write_text("\n" * (start_line - 1) + text[start:end] + "\n")
    return fragment


def do_bisect(c, name):
    text = c.bundle.read_text(errors="replace")
    found = modules(text)
    if name not in found:
        print(f"{c.tag}: no module {name!r}. The bundle has {len(found)}:\n")
        for chunk in sorted(found):
            print(f"    {chunk}")
        sys.exit(1)
    fragment = carve(c, text, name, found[name])
    start, end, start_line = found[name]
    body = text[start:end]
    print(f"{c.tag}: {name} is {c.bundle.name}:{start_line}, {len(body)} bytes, "
          f"{body.count(chr(10)) + 1} lines -> {fragment.relative_to(ROOT)}\n")
    c.build()
    out, _ = c.run([str(fragment)])
    print(out)
    show_context(parse_ratchet(out)[1], c.bundle)


def do_survey(c):
    """Measure every module, and rank the blockers by how many modules they stop.

    The bundle fails at its first blocker, but the modules fail INDEPENDENTLY,
    so counting them turns a single stop into a ranked work list.
    """
    text = c.bundle.read_text(errors="replace")
    found = modules(text)
    c.build()
    results = []
    for name, where in sorted(found.items(), key=lambda kv: kv[1][2]):
        out, _ = c.run([str(carve(c, text, name, where))])
        level, blocker, _, _ = parse_ratchet(out)
        results.append((name, where[2], level, blocker or ""))

    ceiling = max((lvl for _, _, lvl, _ in results if lvl is not None), default=0)
    blocked = [r for r in results if r[2] is not None and r[2] < ceiling]
    print(f"\n  {len(found)} modules, {len(blocked)} of them below level {ceiling}\n")

    # Group by the SHAPE of the blocker: a position makes every one unique, and
    # only the parenthetical that quotes SOURCE varies between modules. Blanking
    # every parenthetical would turn "spread in a call, `f(...args)`" into
    # "`f(...)`", which reads as a different construct entirely.
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
            print(f"        level {level}  {name:<22} {c.bundle.name}:{line}"
                  f"{'  -> ' + position.group(0) if position else ''}")
        print()


def advance_ratchet(c):
    out, _ = c.run()
    level, blocker, full, full_blocker = parse_ratchet(out)
    if level is None:
        sys.exit(f"{c.tag}: could not read a level from the test:\n" + out)
    old = c.record.read_text() if c.record.exists() else "level=0\nblocker=\n"

    def forward(key, measured):
        """Refuse to move a recorded number backwards. The pawl turns one way."""
        was = re.search(rf"^{key}=(\d+)$", old, re.M)
        if was and measured is not None and int(was.group(1)) > measured:
            sys.exit(f"{c.tag}: refusing to advance {key} BACKWARDS, "
                     f"{was.group(1)} -> {measured}.\nThe pawl only turns one way. "
                     f"Fix the regression.")

    forward("level", level)
    forward("full-level", full)
    text = re.sub(r"^level=.*$", f"level={level}", old, flags=re.M)
    text = re.sub(r"^blocker=.*$", f"blocker={blocker or ''}", text, flags=re.M)
    if full is not None:
        # Added rather than replaced when the file predates the second mode.
        if re.search(r"^full-level=", text, re.M):
            text = re.sub(r"^full-level=.*$", f"full-level={full}", text, flags=re.M)
            text = re.sub(r"^full-blocker=.*$", f"full-blocker={full_blocker or ''}", text,
                          flags=re.M)
        else:
            text = text.rstrip() + f"\nfull-level={full}\nfull-blocker={full_blocker or ''}\n"
    c.record.write_text(text)
    pad = " " * (len(c.tag) + 2)
    print(f"{c.tag}: recorded level={level} ({c.rung(level)})")
    if blocker:
        print(f"{pad}blocker={blocker}")
    if full is not None:
        print(f"{pad}full-level={full} ({c.rung(full)})")
        if full_blocker:
            print(f"{pad}full-blocker={full_blocker}")


# --- api: how WIDE ------------------------------------------------------------

def advance_api(c):
    out, _ = c.run()
    if "probes -" not in out:
        sys.exit(f"{c.tag}: the probes did not report:\n" + out)
    # The test prints what is newly passing; the record is the union of what it
    # had and that. A name that stopped passing stays - the test keeps failing
    # until it is fixed, which is what "the pawl only turns one way" means.
    # `(.+)`, NOT `(\S+)`: a probe name has spaces in it.
    gained = re.findall(r"^\s+\+ (.+)$", out, re.M)
    failing = re.findall(r"^\s+!! (.+)$", out, re.M)
    if not gained:
        print(f"{c.tag}: nothing new is passing.")
        if failing:
            print(f"{' ' * (len(c.tag) + 2)}{len(failing)} still failing:")
            for f in failing:
                print(f"{' ' * (len(c.tag) + 4)}{f}")
        return
    header, names = [], set()
    if c.record.exists():
        for line in c.record.read_text().splitlines():
            if line.startswith("#") or not line.strip():
                header.append(line)
            else:
                names.add(line.strip())
    names.update(gained)
    c.record.write_text("\n".join(header + sorted(names)) + "\n")
    print(f"{c.tag}: recorded {len(gained)} newly passing, {len(names)} total")
    for name in gained:
        print(f"{' ' * (len(c.tag) + 2)}+ {name}")


def do_coverage(c):
    """What no probe mentions - the work queue. A function nobody has written a
    probe for is not passing and not failing - it is UNMEASURED, which is the
    state every bug found so far was hiding in."""
    probes = c.probes.read_text(errors="replace")
    if c.coverage == "functions":
        declared = sorted(set(PUBLIC.findall(c.bundle.read_text(errors="replace"))))
        public = [n for n in declared if not n.startswith("_")]
        covered = [n for n in public if re.search(rf"\b{re.escape(n)}\b", probes)]
        missing = [n for n in public if n not in covered]
        print(f"\n  {c.name} declares {len(public)} public functions; "
              f"probes mention {len(covered)}.\n")
        print(f"  {len(missing)} with no probe:\n")
        for i in range(0, len(missing), 6):
            print("    " + "  ".join(f"{n:<22}" for n in missing[i:i + 6]).rstrip())
        print()
    elif c.coverage == "namespaces":
        root = ROOT_EXPORT.search(c.bundle.read_text(errors="replace"))
        namespaces = sorted(set(NAMESPACE.findall(root.group(1)))) if root else []
        if not namespaces:
            sys.exit(f"{c.tag}: no namespaces under `var Phaser = {{` - has the bundle changed?")
        # Where a probe's module tag does not match the namespace name. Written
        # out rather than matched fuzzily: a coverage tool that overstates
        # itself is worse than one that understates.
        aliases = {"Animations": ["anims"], "GameObjects": ["add", "gameobject", "displaylist"],
                   "Scenes": ["scene"]}
        missing = [n for n in namespaces
                   if f"Phaser.{n}" not in probes
                   and not any(f"['{t}'," in probes for t in aliases.get(n, [n.lower()]))]
        print(f"\n  {len(namespaces) - len(missing)}/{len(namespaces)} Phaser namespaces "
              f"have at least one probe\n")
        for name in missing:
            print(f"    no probe mentions  Phaser.{name}")
        print("\n  A namespace with one probe is not a namespace that works - this is a\n"
              "  list of what has NOTHING pointed at it, not a coverage percentage.\n")
    else:
        # No bundle to count against: the shape of the probe set itself, and in
        # particular how much of it is `unscoped` - what the doc has
        # deliberately NOT committed to.
        counts = {}
        for tag in re.findall(r"^  \['([a-z0-9]+)',", probes, re.M):
            counts[tag] = counts.get(tag, 0) + 1
        print(f"\n  {sum(counts.values())} probes across {len(counts)} modules\n")
        for tag in sorted(counts, key=lambda t: -counts[t]):
            note = f"  <- deliberately not implemented; see {c.unscoped_doc}" \
                if tag == "unscoped" else ""
            print(f"    {counts[tag]:>3}  {tag}{note}")
        print()


# --- main ---------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("corpus", choices=sorted(CORPORA))
    ap.add_argument("mode", choices=["ratchet", "api"])
    ap.add_argument("--advance", action="store_true", help="record what was just measured")
    ap.add_argument("--bisect", metavar="MODULE",
                    help="ratchet, p5 only: measure one rollup module instead of the bundle")
    ap.add_argument("--survey", action="store_true",
                    help="ratchet, p5 only: measure every module and rank the blockers")
    ap.add_argument("--coverage", action="store_true",
                    help="api: list what no probe mentions - the work queue")
    ap.add_argument("--only", metavar="MODULE", help="api: show only one module's results")
    args = ap.parse_args(argv)
    c = Corpus(args.corpus, args.mode)

    if c.bundle and not c.bundle.exists():
        sys.exit(f"{c.tag}: {c.bundle.relative_to(ROOT)} is missing")
    if args.mode == "ratchet":
        if args.bisect or args.survey:
            if args.corpus != "p5":
                sys.exit(f"{c.tag}: --bisect and --survey carve rollup modules, and only p5 is one")
            do_bisect(c, args.bisect) if args.bisect else do_survey(c)
            return 0
        c.build()
        if args.advance:
            advance_ratchet(c)
            return 0
        out, code = c.run()
        print(out)
        show_context(parse_ratchet(out)[1], c.bundle)
        if code != 0:
            print("The ratchet is not satisfied. If this is progress, "
                  f"run tools/corpus/{c.tag}.py --advance")
        return code

    if args.coverage:
        do_coverage(c)
        return 0
    c.build()
    if args.advance:
        advance_api(c)
        return 0
    out, code = c.run()
    if args.only:
        for line in out.splitlines():
            if args.only in line or "probes -" in line:
                print(line)
    else:
        print(out)
    if code != 0:
        print("The recorded surface is not satisfied. If this is progress, "
              f"run tools/corpus/{c.tag}.py --advance")
    return code


if __name__ == "__main__":
    sys.exit(main())
