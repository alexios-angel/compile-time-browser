#!/usr/bin/env python3
"""The official ECMAScript conformance suite, run against ctbrowser's engine.

    tools/fetch-test262.sh                              # once, per machine
    tools/check/test262.py --dir test/language/types     # one directory, a table
    tools/check/test262.py --gate                        # the ctest subset
    tools/check/test262.py --dir test/language --json /tmp/language.json

One directory at a time: a conformance run that only prints one number cannot
be acted on. The answer to "is `for-of` implemented" is a table for
`test/language/statements/for-of`, not the whole corpus and a percentage.
Every invocation takes a `--dir` under the corpus root and prints a
per-subdirectory table, the top failure causes with counts, and optionally the
machine-readable rows.

It decides nothing about JavaScript. The host decisions (`$262`, `print`, the
strict transformation, modules, what a thrown error is called) all live in
`ctbrowser/tools/ct262/ct262.cpp`, which this spawns once per test per mode.
This file is process management, YAML frontmatter, and arithmetic.

The skip list is short on purpose. A test is skipped only when this host cannot
present the test's preconditions at all (no second realm, no SharedArrayBuffer,
no [[IsHTMLDDA]] object) or when the suite itself says to skip (intl402 without
ECMA-402, `_FIXTURE` files, which are data). A language feature the engine has
not implemented is a failure, not a skip, so the pass rate stays honest.
`--list-skips` prints the list and its reasons.
"""

import argparse
import concurrent.futures
import json
import os
import re
import resource
import subprocess
import sys
import time
from collections import Counter, defaultdict
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_CORPUS = Path(os.environ.get("TEST262_DIR", Path.home() / ".cache/ctbrowser/test262"))
# Default ct262 location. CMake builds the target `ctbrowser-tool-ct262`;
# pass --binary when the build tree puts it elsewhere (ctest always does).
DEFAULT_BINARY = ROOT / "build" / "tools" / "ct262"
EXPECTATIONS = ROOT / "ctbrowser" / "test" / "test262" / "expectations.txt"

# The ctest gate's fixed subset. Chosen to be fast (a couple of hundred tests),
# broad (the type system, operators, statements, and the four built-ins the
# engine leans on hardest) and stable: no directory here is one the engine is
# expected to churn in. The full corpus is a documented command
# (ctbrowser/docs/test262.md), not a gate: tens of thousands of process spawns
# (one per test per mode) should not run from a plain `ctest`.
GATE_DIRS = [
    "test/language/types/boolean",
    "test/language/types/null",
    "test/language/types/undefined",
    "test/language/types/number",
    "test/language/expressions/addition",
    "test/language/expressions/typeof",
    "test/language/statements/if",
    "test/language/statements/do-while",
    "test/built-ins/Boolean",
    "test/built-ins/Math/abs",
    "test/built-ins/Number/isInteger",
    "test/built-ins/JSON/stringify",
]

# --- what this host cannot present, by name ------------------------------
#
# Each entry is a test262 feature (the `features:` frontmatter key) that the
# host, not the language, cannot supply, with the reason. These are the only
# feature-based skips; everything else runs and is counted.
SKIP_FEATURES = {
    "cross-realm": "$262.createRealm: a script::context owns its heap, so no value can cross realms",
    "SharedArrayBuffer": "$262.agent: one agent, one thread, no SharedArrayBuffer",
    "Atomics": "$262.agent: one agent, one thread, no SharedArrayBuffer",
    "Atomics.pause": "$262.agent: one agent, one thread, no SharedArrayBuffer",
    "Atomics.waitAsync": "$262.agent: one agent, one thread, no SharedArrayBuffer",
    "IsHTMLDDA": "$262.IsHTMLDDA: no [[IsHTMLDDA]] object exists to hand out",
}
# Harness include files the host cannot support, with the reason. Empty today;
# kept so a future one can be listed here.
SKIP_INCLUDES: dict[str, str] = {}
# Flags the host cannot honour. This agent can block (it is one synchronous
# thread), so a test that requires [[CanBlock]] false is not for it.
SKIP_FLAGS = {
    "CanBlockIsFalse": "this agent blocks: it is one synchronous thread",
}
# INTERPRETING.md: "When testing an implementation lacking the capabilities in
# ECMA-402, the tests in those folders should be skipped."
SKIP_PATHS = ("test/intl402/", "test/staging/intl402/")

PASS, FAIL, TIMEOUT, CRASH, SKIP, HOST = "PASS", "FAIL", "TIMEOUT", "CRASH", "SKIP", "HOST"
STATUSES = (PASS, FAIL, TIMEOUT, CRASH, HOST, SKIP)

FRONTMATTER = re.compile(r"/\*---(.*?)---\*/", re.S)


def frontmatter(source):
    """The test's YAML metadata, as much of it as the suite actually uses.

    No real YAML parser: PyYAML is not a dependency of this repository, and
    the subset here is fixed by the suite's own linter. What this must get
    right is the `info: |` block scalar, whose body is arbitrary prose
    containing lines that look exactly like keys, so the rule is indentation,
    not pattern matching: a top-level key starts at column zero and everything
    indented under it belongs to it.
    """
    found = FRONTMATTER.search(source)
    if not found:
        return {}
    out, key, block = {}, None, []
    for line in found.group(1).splitlines():
        if not line.strip():
            if key is not None:
                block.append(line)
            continue
        if line[0] not in " \t-":  # a top-level key
            if key is not None:
                out[key] = block
            head, _, tail = line.partition(":")
            key, block = head.strip(), ([tail.strip()] if tail.strip() else [])
        elif key is not None:
            block.append(line.rstrip())
    if key is not None:
        out[key] = block
    return out


def as_list(block):
    """`[a, b]` on the key's own line, or `- a` lines under it. Both are used."""
    if not block:
        return []
    first = block[0]
    if first.startswith("["):
        inside = " ".join(block)
        inside = inside[inside.find("[") + 1 : inside.rfind("]")]
        return [item.strip() for item in inside.split(",") if item.strip()]
    items = []
    for line in block:
        stripped = line.strip()
        if stripped.startswith("- "):
            items.append(stripped[2:].strip())
    return items


def as_map(block):
    """`key: value` lines under a key, as a dict (used for `negative:`)."""
    out = {}
    for line in block:
        head, _, tail = line.strip().partition(":")
        if tail.strip():
            out[head.strip()] = tail.strip()
    return out


class Test:
    """One test262 file: its path, the frontmatter fields the runner uses, and
    why it is skipped (None when it runs)."""

    # __slots__ and no stored `meta`: the parsed frontmatter (including long
    # `info:` blocks) for every file of a large run is hundreds of MB and is read
    # once, so only the five fields below are kept.
    __slots__ = ("path", "rel", "flags", "features", "includes", "negative", "skip")

    def __init__(self, path, corpus):
        self.path = path
        self.rel = path.relative_to(corpus).as_posix()
        meta = frontmatter(path.read_text(encoding="utf-8", errors="replace"))
        self.flags = set(as_list(meta.get("flags", [])))
        self.features = set(as_list(meta.get("features", [])))
        self.includes = as_list(meta.get("includes", []))
        self.negative = as_map(meta.get("negative", []))
        self.skip = self._skip_reason()

    def _skip_reason(self):
        for prefix in SKIP_PATHS:
            if self.rel.startswith(prefix):
                return "intl402: this engine has no ECMA-402"
        for feature in sorted(self.features):
            if feature in SKIP_FEATURES:
                return f"{feature}: {SKIP_FEATURES[feature]}"
        for include in self.includes:
            if include in SKIP_INCLUDES:
                return f"{include}: {SKIP_INCLUDES[include]}"
        for flag in sorted(self.flags):
            if flag in SKIP_FLAGS:
                return f"{flag}: {SKIP_FLAGS[flag]}"
        return None

    def modes(self):
        """The suite's rule: both ways unless the metadata says otherwise."""
        if "raw" in self.flags:
            return ["raw"]
        if "module" in self.flags:
            return ["module"]
        if "onlyStrict" in self.flags:
            return ["strict"]
        if "noStrict" in self.flags:
            return ["sloppy"]
        return ["sloppy", "strict"]

    def prelude(self, corpus):
        """The harness files ct262 loads before the test, in order."""
        if "raw" in self.flags:
            return []
        harness = corpus / "harness"
        files = [harness / "assert.js", harness / "sta.js"]
        if "async" in self.flags:
            files.append(harness / "doneprintHandle.js")
        files += [harness / name for name in self.includes]
        return files


def cap_address_space(megabytes):
    """The memory cap, set once on this process so every child inherits it.

    Not a `preexec_fn`: that runs between fork and exec in a process with four
    worker threads, where the child may deadlock on a lock another thread held
    at the moment of the fork (Python's own documentation says so). RLIMIT_AS is
    per-process and inherited, so setting it here gives each `ct262` its own cap
    with no fork-time code at all. The runner lives under the same cap, which it
    has no trouble with: the largest thing it holds is one Test per file.
    """
    cap = megabytes * 1024 * 1024
    _current, hard = resource.getrlimit(resource.RLIMIT_AS)
    if hard != resource.RLIM_INFINITY:
        cap = min(cap, hard)
    resource.setrlimit(resource.RLIMIT_AS, (cap, hard))


def run_mode(test, mode, args):
    """One process: one test, one mode, one realm. Returns (status, cause)."""
    command = [str(args.binary)]
    for prelude in test.prelude(args.corpus):
        command += ["--prelude", str(prelude)]
    if mode == "strict":
        command.append("--strict")
    if mode == "module":
        command.append("--module")
    command.append(str(test.path))
    try:
        # errors="replace", not the default. The engine's messages carry the
        # offending source in them, and a test whose source is a lone surrogate
        # or a truncated multi-byte sequence (several test262 tests are about
        # exactly that) comes back as bytes that are not UTF-8. The default
        # strict decode would raise inside subprocess and end the whole run.
        done = subprocess.run(
            command, capture_output=True, text=True, errors="replace", timeout=args.timeout
        )
    except subprocess.TimeoutExpired:
        return TIMEOUT, f"timeout after {args.timeout}s"
    except OSError as problem:
        return HOST, f"spawn failed: {problem}"

    error = None
    for line in done.stderr.splitlines():
        parts = line.split("\t")
        if len(parts) >= 6 and parts[0] == "ct262" and parts[1] == "error":
            error = {"phase": parts[2], "ctor": parts[3], "name": parts[4], "message": parts[5]}
        elif len(parts) >= 3 and parts[0] == "ct262" and parts[1] == "host":
            return HOST, parts[2]

    if done.returncode == 3:
        return HOST, (done.stderr.strip().splitlines() or ["host error"])[-1]
    if done.returncode not in (0, 1):
        return CRASH, f"exit {done.returncode}"

    if test.negative:
        want_phase = test.negative.get("phase", "")
        want_type = test.negative.get("type", "")
        if error is None:
            return FAIL, f"negative {want_phase}/{want_type}: nothing was thrown"
        # Accept the constructor or the name. They differ in this engine: every
        # error `context::throw_error` builds sits on the one Error prototype, so
        # an engine-raised TypeError has constructor Error and name TypeError.
        # Accepting either is a documented leniency (ctbrowser/docs/test262.md),
        # so runtime negatives do not fail on a prototype-wiring detail
        # unrelated to what they test.
        if error["phase"] != want_phase:
            return (
                FAIL,
                f"negative {want_phase}/{want_type}: got {error['phase']} "
                f"{error['ctor'] or error['name']}",
            )
        if want_type not in (error["ctor"], error["name"]):
            return (
                FAIL,
                f"negative {want_phase}/{want_type}: got {error['ctor'] or error['name'] or '?'}",
            )
        return PASS, None

    if error is not None:
        return FAIL, f"{error['phase']}: {error['message']}"
    if done.returncode != 0:
        return FAIL, "non-zero exit with no report"

    if "async" in test.flags:
        # The suite's own protocol: the test is not complete until $DONE has
        # printed. Silence is a failure, not a pass: an engine whose promises
        # never settle exits 0 having done nothing.
        out = done.stdout
        if "Test262:AsyncTestComplete" in out:
            return PASS, None
        failure = [ln for ln in out.splitlines() if ln.startswith("Test262:AsyncTestFailure")]
        if failure:
            return FAIL, "async: " + failure[0][len("Test262:AsyncTestFailure:") :].strip()
        return FAIL, "async: $DONE was never called"
    return PASS, None


def run_test(test, args):
    """One test, every mode its metadata asks for, worst answer wins.

    Never raises. A large run that dies on one unexpected exception has
    measured nothing, and such an exception is more likely to be the runner's
    than the engine's, so a failure to classify is reported as a HOST row
    carrying it, which lands in the table and in the causes instead of ending
    the run.
    """
    try:
        return classify(test, args)
    except Exception as problem:  # deliberately broad; see the docstring
        return {"test": test.rel, "status": HOST, "cause": f"runner: {problem!r}", "mode": None}


def classify(test, args):
    """Run every mode of `test`; the worst status wins (TIMEOUT > CRASH > HOST > FAIL)."""
    if test.skip:
        return {"test": test.rel, "status": SKIP, "cause": test.skip, "mode": None}
    worst = (PASS, None, None)
    order = {PASS: 0, FAIL: 1, HOST: 2, CRASH: 3, TIMEOUT: 4}
    for mode in test.modes():
        status, cause = run_mode(test, mode, args)
        if order[status] > order[worst[0]]:
            worst = (status, cause, mode)
    return {"test": test.rel, "status": worst[0], "cause": worst[1], "mode": worst[2]}


def collect(corpus, where):
    """Every test under `where` (a file or directory relative to the corpus)."""
    root = (corpus / where).resolve()
    if not root.exists():
        sys.exit(f"test262.py: no such directory in the corpus: {where}")
    if root.is_file():
        return [Test(root, corpus)]
    found = []
    for path in sorted(root.rglob("*.js")):
        # _FIXTURE files are module dependencies, not tests. INTERPRETING.md:
        # they "MUST NOT be interpreted as standalone tests".
        if "_FIXTURE" in path.name:
            continue
        found.append(Test(path, corpus))
    return found


def normalise_cause(cause):
    """A cause with the specifics rubbed off, so causes can be counted.

    Two failures reading "parse error: expected `)` - at 12:7" and "... at 40:3"
    are one cause. Line numbers, quoted names and offsets are what differ
    between instances of the same gap, and leaving them in produces a histogram
    with a count of 1 in every row, which is not a diagnosis.
    """
    if cause is None:
        return "-"
    text = re.sub(r"\d+", "N", cause)
    text = re.sub(r"[`'\"][^`'\"]*[`'\"]", "X", text)
    return text[:110]


def report(rows, args):
    """Print the per-directory status table, the top failure causes and the skips."""
    counts = Counter(row["status"] for row in rows)
    by_dir = defaultdict(Counter)
    # One row per subdirectory of what was asked for, whatever was asked for.
    # A fixed depth answers the wrong question at both ends: `--dir test/language`
    # wants its six areas and `--dir test/language/types/boolean` wants one row,
    # not five files. The key is the test's own directory, truncated to one level
    # below the query.
    depth = args.group_depth
    for row in rows:
        parts = row["test"].split("/")[:-1]
        by_dir["/".join(parts[:depth])][row["status"]] += 1

    width = max([len(name) for name in by_dir] + [9])
    header = f"{'directory'.ljust(width)}  {'total':>6} " + " ".join(s.rjust(7) for s in STATUSES)
    print(header)
    print("-" * len(header))
    for name in sorted(by_dir):
        line = by_dir[name]
        total = sum(line.values())
        cells = " ".join(str(line[s]).rjust(7) for s in STATUSES)
        print(f"{name.ljust(width)}  {total:>6} {cells}")
    print("-" * len(header))
    total = sum(counts.values())
    cells = " ".join(str(counts[s]).rjust(7) for s in STATUSES)
    print(f"{'TOTAL'.ljust(width)}  {total:>6} {cells}")
    ran = total - counts[SKIP]
    rate = (100.0 * counts[PASS] / ran) if ran else 0.0
    print(
        f"\n{counts[PASS]}/{ran} of the tests that RAN passed ({rate:.1f}%); "
        f"{counts[SKIP]} skipped, {total} in the directory. {date.today().isoformat()}"
    )

    causes = Counter(normalise_cause(row["cause"]) for row in rows if row["status"] == FAIL)
    if causes:
        print(f"\ntop failure causes ({len(causes)} distinct):")
        for cause, count in causes.most_common(args.top):
            print(f"  {count:>6}  {cause}")
    skips = Counter(row["cause"] for row in rows if row["status"] == SKIP)
    if skips:
        print("\nskipped, by reason:")
        for cause, count in skips.most_common():
            print(f"  {count:>6}  {cause}")


def gate(rows, args):
    """No regressions and no unexpected passes.

    An unexpected pass fails the gate too: an expectations list that only
    ratchets one way rots into a list of things nobody has re-measured, and
    when a fix makes some of them pass, nothing says so. Both directions are a
    diff to look at and one command to record.
    """
    expected = {}
    if EXPECTATIONS.exists():
        for line in EXPECTATIONS.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            status, _, name = line.partition(" ")
            expected[name.strip()] = status.strip()
    regressions, unexpected = [], []
    for row in rows:
        was = expected.get(row["test"], PASS)
        now = row["status"]
        if was == now:
            continue
        if now == PASS:
            unexpected.append((row["test"], was))
        else:
            regressions.append((row["test"], was, now, row["cause"]))
    for name, was, now, cause in regressions:
        print(f"REGRESSED  {name}: expected {was}, got {now} - {cause}")
    for name, was in unexpected:
        print(
            f"NOW PASSES {name}: recorded as {was}. Re-record with --update-expectations "
            f"- an expectations file nobody prunes is a list of excuses."
        )
    if regressions or unexpected:
        print(
            f"\ntest262 gate FAILED: {len(regressions)} regressed, {len(unexpected)} newly passing"
        )
        return 1
    print(f"test262 gate ok: {len(rows)} tests match ctbrowser/test/test262/expectations.txt")
    return 0


def write_expectations(rows):
    """Rewrite expectations.txt with every row that does not pass."""
    EXPECTATIONS.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# test262 expectations - the gate's fixed subset, and what it does today.",
        "#",
        "# WRITTEN BY `tools/check/test262.py --gate --update-expectations`, never by hand.",
        "# Every line is a test that does NOT pass; anything absent is expected to pass, so",
        "# a fix that makes one of these pass FAILS the gate until the line is removed. That",
        "# is deliberate - see the note on gate() in the runner.",
        f"# Recorded {date.today().isoformat()} against the pinned corpus in tools/fetch-test262.sh.",
        "",
    ]
    for row in sorted(rows, key=lambda r: r["test"]):
        if row["status"] != PASS:
            lines.append(f"{row['status']} {row['test']}")
    EXPECTATIONS.write_text("\n".join(lines) + "\n")
    kept = sum(1 for row in rows if row["status"] != PASS)
    print(f"wrote {EXPECTATIONS.relative_to(ROOT)}: {kept} of {len(rows)} tests are not passing")


# --- the harness's own test ----------------------------------------------
#
# Each entry is a way this runner could misreport, written down as a test it
# must classify correctly. A harness that reports PASS for everything looks like
# a conforming engine from its output alone, so each fixture below is a planted
# answer with the classification it must get, and `--self-test` asserts them.
#
# The ones that matter most are `must-fail` (a false assertion must be reported
# as FAIL, not CRASH or PASS) and the `negative-*` fixtures, the only proof that
# `negative:` is scored on both the phase and the error type rather than on
# "something was thrown".
SELF_TEST = [
    ("must-pass", PASS, "", "assert.sameValue(1, 1);\n"),
    ("must-fail", FAIL, "", "assert.sameValue(1, 2);\n"),
    (
        "negative-parse-correct",
        PASS,
        "negative:\n  phase: parse\n  type: SyntaxError\n",
        "$DONOTEVALUATE();\nvar a = ;\n",
    ),
    (
        "negative-parse-wrong-type",
        FAIL,
        "negative:\n  phase: parse\n  type: TypeError\n",
        "$DONOTEVALUATE();\nvar a = ;\n",
    ),
    (
        "negative-parse-nothing-thrown",
        FAIL,
        "negative:\n  phase: parse\n  type: SyntaxError\n",
        "var a = 1;\n",
    ),
    (
        "negative-runtime-correct",
        PASS,
        "negative:\n  phase: runtime\n  type: Test262Error\n",
        "throw new Test262Error('planted');\n",
    ),
    (
        "negative-runtime-wrong-phase",
        FAIL,
        "negative:\n  phase: runtime\n  type: SyntaxError\n",
        "var a = ;\n",
    ),
    ("async-complete", PASS, "flags: [async]\n", "$DONE();\n"),
    ("async-silent", FAIL, "flags: [async]\n", "var never = 1;\n"),
    ("skipped-feature", SKIP, "features: [cross-realm]\n", "$262.createRealm();\n"),
    ("timeout", TIMEOUT, "", "while (true) { }\n"),
]


def self_test(args):
    """Run the planted SELF_TEST fixtures; return 1 if any is misclassified."""
    import tempfile

    with tempfile.TemporaryDirectory(prefix="ct262-selftest-") as scratch:
        root = Path(scratch)
        planted = []
        for name, want, meta, body in SELF_TEST:
            path = root / f"{name}.js"
            path.write_text(f"/*---\ndescription: planted\n{meta}---*/\n{body}")
            planted.append((Test(path, root), want))
        # A 3-second cap: one fixture is an infinite loop, and the point is only
        # to prove that the timeout fires. Waiting the default 10 seconds would
        # waste time.
        args.timeout = min(args.timeout, 3.0)
        wrong = 0
        for test, want in planted:
            got = run_test(test, args)
            mark = "ok  " if got["status"] == want else "WRONG"
            if got["status"] != want:
                wrong += 1
            print(
                f"{mark} {test.rel:34s} want {want:7s} got {got['status']:7s} "
                f"{(got['cause'] or '')[:60]}"
            )
        print(f"\n{len(planted) - wrong}/{len(planted)} planted answers classified correctly")
        if wrong:
            print("test262 self-test FAILED: the runner does not classify what it is told to")
        return 1 if wrong else 0


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--dir", default=None, help="a directory under the corpus root, e.g. test/language/types"
    )
    parser.add_argument("--gate", action="store_true", help="run the ctest subset")
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="plant answers this runner must get right, and assert it does",
    )
    parser.add_argument("--update-expectations", action="store_true")
    parser.add_argument("--list-skips", action="store_true")
    parser.add_argument("--corpus", type=Path, default=DEFAULT_CORPUS)
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    parser.add_argument(
        "--jobs", type=int, default=4, help="capped at 4: the devbox is shared and builds run on it"
    )
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--memory-mb", type=int, default=2048)
    parser.add_argument(
        "--group-depth",
        type=int,
        default=0,
        help="path components per table row; 0 = one level below --dir",
    )
    parser.add_argument("--top", type=int, default=15)
    parser.add_argument("--json", type=Path, default=None)
    parser.add_argument("--tsv", type=Path, default=None)
    args = parser.parse_args()

    if args.list_skips:
        print("features this HOST cannot present (skipped, and why):")
        for name, why in sorted(SKIP_FEATURES.items()):
            print(f"  {name:24s} {why}")
        for name, why in sorted(SKIP_INCLUDES.items()):
            print(f"  {name:24s} {why}")
        for name, why in sorted(SKIP_FLAGS.items()):
            print(f"  {name:24s} {why}")
        for path in SKIP_PATHS:
            print(f"  {path:24s} INTERPRETING.md: skip without ECMA-402")
        return 0

    args.jobs = max(1, min(args.jobs, 4))
    cap_address_space(args.memory_mb)
    if not args.binary.exists():
        sys.exit(
            f"test262.py: no ct262 at {args.binary} - build it on the devbox "
            f"(`DEVBOX_DIR=... tools/remote-build.sh ctbrowser-tool-ct262`)"
        )
    if not (args.corpus / "harness" / "assert.js").exists():
        sys.exit(f"test262.py: no corpus at {args.corpus} - run tools/fetch-test262.sh")

    if args.self_test:
        return self_test(args)

    if args.gate:
        tests = []
        for where in GATE_DIRS:
            tests += collect(args.corpus, where)
    elif args.dir:
        tests = collect(args.corpus, args.dir)
    else:
        sys.exit("test262.py: pass --dir <corpus subdirectory> or --gate")

    if args.group_depth <= 0:
        args.group_depth = len(Path(args.dir).parts) + 1 if args.dir else 3

    started = time.monotonic()
    rows = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(run_test, test, args): test for test in tests}
        for done in concurrent.futures.as_completed(futures):
            rows.append(done.result())
    rows.sort(key=lambda row: row["test"])

    if args.json:
        args.json.write_text(json.dumps(rows, indent=1))
    if args.tsv:
        args.tsv.write_text(
            "".join(f"{row['status']}\t{row['test']}\t{row['cause'] or ''}\n" for row in rows)
        )

    if args.update_expectations:
        # Only from the gate's own subset. `--dir test/built-ins/Array
        # --update-expectations` would replace the gate's expectations with one
        # directory's, and the gate would then pass over a file that describes
        # something it does not run.
        if not args.gate:
            sys.exit(
                "test262.py: --update-expectations records the GATE's subset, so it needs --gate"
            )
        write_expectations(rows)
        return 0
    if args.gate:
        return gate(rows, args)
    report(rows, args)
    print(
        f"{time.monotonic() - started:.1f}s on {args.jobs} workers, "
        f"{args.timeout:g}s timeout, {args.memory_mb} MB address-space cap"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
