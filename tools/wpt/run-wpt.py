#!/usr/bin/env python3
"""Run web-platform-tests against ctbrowser, one directory at a time.

    tools/wpt/run-wpt.py --dir dom/nodes
    tools/wpt/run-wpt.py --dir dom/events --jobs 4 --json /tmp/events.json
    tools/wpt/run-wpt.py --dir dom/nodes --filter Node-appendChild
    tools/wpt/run-wpt.py --gate                 the fixed subset, against expectations
    tools/wpt/run-wpt.py --dir dom/nodes --update-expectations
    tools/wpt/run-wpt.py --selftest             prove the harness reports failure

ONE DIRECTORY AT A TIME IS THE INTERFACE, not a batch that hides everything.
Every run prints a table and says which corpus commit it was measured against;
`--json` and `--tsv` write the same numbers for something else to read.

HOW A TEST IS RUN. `ctdrive` - the engine's existing driver - opens the page and
takes JSON commands on a loopback socket. One process per test, which is what
makes a crash a CRASH rather than a lost run: the process dies, the runner sees
the signal, and the next test is unaffected. Results come back through
tools/wpt/testharnessreport.js, the per-vendor hook WPT leaves for exactly this,
which publishes a JSON string the runner reads with one `eval`.

WHAT IS DELIBERATELY NOT MEASURED is in ctbrowser/docs/wpt.md, and every skip
names a feature rather than a number.
"""

import argparse
import concurrent.futures
import json
import os
import re
import shutil
import signal as signal_module
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
DEFAULT_WPT = Path(os.environ.get("WPT_DIR", Path.home() / ".cache" / "wpt"))
EXPECTATIONS = HERE / "expectations.txt"
FETCH_SCRIPT = HERE / "fetch-wpt.sh"
SELFTEST_DIR = HERE / "selftest"

# The fixed subset the ctest gate runs. Small on purpose - the gate has a
# two-minute budget and the full run is a documented command, not a build step.
# A DIRECTORY plus a substring, because a whole suite does not fit in the budget
# and "the first N files" would silently change meaning whenever WPT adds one.
GATE_SELECTION = [("dom/events", "Event-")]

# Per-test wall clock. WPT's own metadata is the source: `<meta name="timeout"
# content="long">` means 60 s, everything else 10 s. The driver gets a margin on
# top because the harness must be given the chance to report its OWN timeout -
# a test killed at exactly its deadline is reported as a runner timeout and
# loses the subtest detail the harness was about to publish.
TIMEOUT_NORMAL = 10.0
TIMEOUT_LONG = 60.0
DRIVER_MARGIN = 5.0


class Outcome:
    PASS = "PASS"
    FAIL = "FAIL"
    TIMEOUT = "TIMEOUT"
    CRASH = "CRASH"
    HARNESS_ERROR = "HARNESS_ERROR"
    SKIP = "SKIP"


ORDER = [Outcome.PASS, Outcome.FAIL, Outcome.TIMEOUT, Outcome.CRASH,
         Outcome.HARNESS_ERROR, Outcome.SKIP]


# --- choosing what to run ---------------------------------------------------

# Not tests, whatever their extension: references, manual tests, and the
# directories a suite keeps its fixtures in.
SKIP_DIR_PARTS = {"support", "resources", "reference", "tools"}
META_RE = re.compile(rb"^//\s*META:\s*(\w+)=(.*)$", re.MULTILINE)

# WPT ENCODES ITS SERVER REQUIREMENTS IN THE FILENAME, and there is no server
# here. A `.https.` test is served over TLS from a second origin, `.h2.` needs
# HTTP/2, and the worker spellings need a Worker this engine does not have. Each
# names the missing thing rather than a number - a skip that says "0 subtests"
# is how a suite gets tuned to look good.
SERVER_SUFFIXES = {
    ".https.": "https: needs a TLS origin, and there is no server here",
    ".h2.": "h2: needs an HTTP/2 server",
    ".serviceworker.": "ServiceWorker: not implemented",
    ".sharedworker.": "SharedWorker: not implemented",
    ".worker.": "Worker: not implemented",
}
# The wrapper this runner generates for a .any.js test, named so that a run
# killed halfway leaves something a later run can recognise and delete.
WRAPPER_SUFFIX = ".ctwpt.html"


def is_candidate(path: Path, wpt: Path) -> bool:
    rel = path.relative_to(wpt)
    if any(part in SKIP_DIR_PARTS for part in rel.parts[:-1]):
        return False
    name = path.name
    if name.startswith("."):
        return False
    if name.endswith(WRAPPER_SUFFIX):  # our own leftovers, never a test
        return False
    if "-manual." in name or name.endswith("-ref.html") or name.endswith("-ref.xhtml"):
        return False
    if name.endswith((".any.js", ".window.js")):
        return True
    return name.endswith((".html", ".xhtml", ".htm"))


def collect(wpt: Path, where: str, filter_text):
    base = wpt / where
    if not base.is_dir():
        sys.exit(f"no such directory in the WPT checkout: {base}")
    found = [p for p in sorted(base.rglob("*")) if p.is_file() and is_candidate(p, wpt)]
    if filter_text:
        found = [p for p in found if filter_text in str(p.relative_to(wpt))]
    return found


def sweep_wrappers(wpt: Path, where: str) -> int:
    """Delete wrappers a killed run left behind, before planning a new one.

    They are generated INTO the corpus - a .any.js test's `<script src>` is
    relative to itself, so the page has to sit beside it - and a run that is
    interrupted cannot clean up after itself. Left alone they are picked up as
    candidates by the next run, which is how a corpus grows tests nobody wrote.
    """
    base = wpt / where
    stale = list(base.rglob("*" + WRAPPER_SUFFIX)) if base.is_dir() else []
    for leftover in stale:
        leftover.unlink(missing_ok=True)
    return len(stale)


def read_head(path: Path, limit: int = 8192) -> bytes:
    with path.open("rb") as handle:
        return handle.read(limit)


class Plan:
    """What one candidate turns into: a page to open, or a reason to skip."""

    def __init__(self, rel, page=None, timeout=TIMEOUT_NORMAL, skip=None, wrapper=None):
        self.rel = rel            # the test's WPT path, and its key in expectations
        self.page = page          # the file ctdrive actually opens
        self.timeout = timeout
        self.skip = skip          # a reason, naming a feature - never a number
        self.wrapper = wrapper    # a generated .any.html to delete afterwards


def plan_for(path: Path, wpt: Path) -> Plan:
    rel = str(path.relative_to(wpt))
    for marker, why in SERVER_SUFFIXES.items():
        if marker in path.name:
            return Plan(rel, skip=why)
    head = read_head(path)

    if path.name.endswith((".any.js", ".window.js")):
        meta = {}
        for key, value in META_RE.findall(head):
            meta.setdefault(key.decode(), []).append(value.decode().strip())
        scopes = meta.get("global", ["window"])[0]
        # `global=worker` and friends: this engine has no Worker, so the wrapper
        # WPT would generate for it is not one we can open. Named as the missing
        # feature, which is what a skip has to be.
        if scopes and "window" not in scopes and "default" not in scopes:
            return Plan(rel, skip=f"global={scopes}: no Worker/ServiceWorker in this engine")
        if meta.get("variant"):
            return Plan(rel, skip="variant: the driver opens a file and has no query string")
        # THE WRAPPER WPT'S MANIFEST WOULD HAVE GENERATED. A .any.js test has no
        # HTML on disk at all - wptrunner synthesises `<test>.any.html` at
        # request time - so the runner has to build the same page, beside the
        # script so that its relative <script src> resolves.
        long_timeout = "long" in meta.get("timeout", [""])[0]
        extra = "".join(
            f'<script src="{src}"></script>\n' for src in meta.get("script", []))
        wrapper_name = path.name.rsplit(".js", 1)[0] + WRAPPER_SUFFIX
        wrapper = path.parent / wrapper_name
        wrapper.write_text(
            "<!doctype html>\n<meta charset=utf-8>\n"
            + ('<meta name="timeout" content="long">\n' if long_timeout else "")
            + '<script src="/resources/testharness.js"></script>\n'
            '<script src="/resources/testharnessreport.js"></script>\n'
            "<div id=log></div>\n"
            + extra
            + f'<script src="{path.name}"></script>\n',
            encoding="utf-8")
        return Plan(rel, page=wrapper,
                    timeout=TIMEOUT_LONG if long_timeout else TIMEOUT_NORMAL,
                    wrapper=wrapper)

    # A REFERENCE TEST IS NOT A TESTHARNESS TEST. It is a render comparison, and
    # this runner has no reference rendering to compare against - the render
    # goldens are a different instrument (tools/check/check-render.cmake).
    if b"rel=match" in head or b'rel="match"' in head or b"rel=mismatch" in head:
        return Plan(rel, skip="reftest: needs a reference render, not a harness result")
    if b"testharness.js" not in head:
        return Plan(rel, skip="not a testharness test")
    if b'name="variant"' in head or b"name=variant" in head:
        return Plan(rel, skip="variant: the driver opens a file and has no query string")
    if b"testdriver.js" in head:
        return Plan(rel, skip="testdriver: needs WebDriver input injection")
    long_timeout = (b'name="timeout" content="long"' in head
                    or b"name=timeout content=long" in head)
    return Plan(rel, page=path, timeout=TIMEOUT_LONG if long_timeout else TIMEOUT_NORMAL)


# --- driving one page -------------------------------------------------------

PORT_RE = re.compile(r"listening on 127\.0\.0\.1:(\d+)")

# The probe. `D` and the payload once the harness has published; otherwise `W`
# and whether the harness is even there - which is the whole difference between
# "never finished" (TIMEOUT) and "never loaded" (HARNESS_ERROR).
PROBE = ('console.log(window.__wpt_done ? ("D" + window.__wpt_state) '
         ': ("W" + (typeof add_completion_callback)))')

# RLIMIT_AS, APPLIED BY THE SHELL RATHER THAN BY preexec_fn.
#
# `ulimit -v` is the same rlimit, set between fork and exec by a process that is
# single-threaded because it has just been forked from one. Python's own
# preexec_fn runs in the same place but from a process forked out of a THREAD
# POOL, which the standard library documents as unsafe: it may deadlock in the
# child on a lock another thread held at fork. This runner has four workers, so
# that is not a theoretical objection.
#
# `ulimit -c 0` goes with it. A page that crashes here is one of hundreds, and a
# box with 8 vCPUs writing hundreds of multi-GB cores fills the disk long before
# the run finishes - which is a build machine down, not a finding.
LIMIT_WRAPPER = 'ulimit -v "$1" || exit 71; ulimit -c 0; shift; exec "$@"'


def signal_name(code: int) -> str:
    """`-11` as `SIGSEGV`, because the number is the least useful part."""
    if code >= 0:
        return f"exit {code}"
    try:
        return signal_module.Signals(-code).name
    except ValueError:
        return f"signal {-code}"


class DriverResult:
    def __init__(self, status, message="", subtests=None, signal=None, log="", seconds=0.0):
        self.status = status
        self.message = message
        self.subtests = subtests or []
        self.signal = signal
        self.log = log
        self.seconds = seconds


def send_command(sock, payload, deadline):
    sock.sendall((json.dumps(payload) + "\n").encode())
    buffer = b""
    while b"\n" not in buffer:
        left = deadline - time.monotonic()
        if left <= 0:
            raise TimeoutError("no reply")
        sock.settimeout(min(left, 1.0))
        chunk = sock.recv(65536)
        if not chunk:
            raise ConnectionError("driver closed the socket")
        buffer += chunk
    return json.loads(buffer.split(b"\n", 1)[0])


def run_one(plan: Plan, driver: Path, wpt: Path, memory_mb: int) -> DriverResult:
    started = time.monotonic()
    deadline = started + plan.timeout + DRIVER_MARGIN
    env = dict(os.environ)
    env.update({
        # WHAT A LEADING `/` MEANS. Every WPT test asks for
        # /resources/testharness.js, which without this is read off the root of
        # the disk and missed - see shell::asset_registry::set_document_root.
        "CTBROWSER_DOC_ROOT": str(wpt),
        # DETERMINISTIC AND HEADLESS. The box has no GPU: a Linux binary here
        # sees lavapipe and SwiftShader only, so the driver is pinned the same
        # way tools/check/check-render.cmake pins it rather than left to
        # whatever the Vulkan loader picks.
        "CTBROWSER_GL_DRIVER": "deterministic",
        "SDL_VIDEODRIVER": "offscreen",
        "SDL_AUDIODRIVER": "dummy",
        # A test that reaches the network fails for reasons that are not about
        # this engine, and hangs for as long as a DNS timeout while it does.
        "CTBROWSER_NETWORK": "0",
        "CTBROWSER_FONTS": "font8x8",
    })
    log = tempfile.TemporaryFile(mode="w+b")
    process = subprocess.Popen(
        ["/bin/sh", "-c", LIMIT_WRAPPER, "ctwpt", str(memory_mb * 1024),
         str(driver), str(plan.page), "--port", "0"],
        cwd=str(plan.page.parent), env=env, stdout=log, stderr=subprocess.STDOUT,
        stdin=subprocess.DEVNULL)

    def output():
        log.seek(0)
        return log.read().decode("utf-8", "replace")

    def gone():
        """The driver ended on its own, which it is never supposed to do.

        The ONLY clean exit is the `quit` command, which this runner does not
        send - it kills the driver instead, because a page that has published
        its results has nothing left to say. So any exit is a finding, and the
        two kinds are told apart: a signal or a non-zero code is a CRASH, and a
        tidy exit 0 before anything was published is the driver refusing the
        page (a file it could not open, an SDL backend it could not start),
        which is a harness error rather than an engine crash.
        """
        return process.poll() is not None

    def ended(where: str) -> DriverResult:
        code = process.returncode
        if code == 0:
            return DriverResult(Outcome.HARNESS_ERROR,
                                f"driver exited 0 {where} without publishing a result",
                                log=output(), seconds=time.monotonic() - started)
        return DriverResult(Outcome.CRASH, f"driver died {where}: {signal_name(code)}",
                            signal=code, log=output(), seconds=time.monotonic() - started)

    try:
        port = None
        while port is None:
            match = PORT_RE.search(output())
            if match:
                port = int(match.group(1))
                break
            if gone():
                return ended("before listening")
            if time.monotonic() > deadline:
                return DriverResult(Outcome.HARNESS_ERROR, "driver never printed a port",
                                    log=output(), seconds=time.monotonic() - started)
            time.sleep(0.01)

        sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        last_probe = ""
        # DID THE DRIVER EVER ANSWER? A page whose script never returns - an
        # infinite loop at the top level - prints its port and then runs no
        # frames at all, so `eval` is never even read off the socket. That is a
        # HANG, and reporting it as "testharness.js never defined
        # add_completion_callback" blames the corpus for an engine that stopped.
        answered = False
        try:
            while True:
                if gone():
                    return ended("while running")
                if time.monotonic() > deadline:
                    break
                try:
                    reply = send_command(sock, {"cmd": "eval", "script": PROBE}, deadline)
                except (TimeoutError, ConnectionError, OSError):
                    if gone():
                        return ended("mid-command")
                    break
                answered = True
                logged = reply.get("console") or []
                last_probe = logged[-1] if logged else ""
                if last_probe.startswith("D"):
                    return classify(last_probe[1:], output(),
                                    time.monotonic() - started)
                # A SCRIPT ERROR THE HARNESS NEVER SAW. testharness.js installs
                # its own error handler, so a page that reaches this has thrown
                # somewhere the harness could not catch - during load, most
                # often, which is the case where nothing will ever be published.
                if not reply.get("ok", True) and reply.get("error"):
                    return DriverResult(Outcome.HARNESS_ERROR,
                                        f"eval failed: {reply['error']}", log=output(),
                                        seconds=time.monotonic() - started)
                time.sleep(0.05)

            # The deadline passed with nothing published.
            info = {}
            try:
                info = send_command(sock, {"cmd": "info"}, time.monotonic() + 2.0)
            except Exception:
                pass
            script_error = (info.get("script_error") or "").strip()
            if not answered:
                # The driver listened and then never ran another frame. It is a
                # TIMEOUT rather than a harness error: the page is what stopped,
                # and the harness was never given a chance to say anything.
                return DriverResult(Outcome.TIMEOUT,
                                    script_error or
                                    "driver listened but answered no command - the page "
                                    "never yielded (an infinite loop during load)",
                                    log=output(), seconds=time.monotonic() - started)
            if last_probe.startswith("W") and "function" in last_probe:
                # The harness was there and never finished: a test that never
                # calls done, or one waiting on something this engine will not
                # deliver. That is a TIMEOUT and is not a harness error.
                return DriverResult(Outcome.TIMEOUT,
                                    script_error or "harness loaded, never completed",
                                    log=output(), seconds=time.monotonic() - started)
            return DriverResult(Outcome.HARNESS_ERROR,
                                script_error or
                                "testharness.js never defined add_completion_callback "
                                f"(probe {last_probe!r})",
                                log=output(), seconds=time.monotonic() - started)
        finally:
            sock.close()
    finally:
        if process.poll() is None:
            process.kill()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                pass
        log.close()


def classify(payload: str, log: str, seconds: float) -> DriverResult:
    try:
        state = json.loads(payload)
    except json.JSONDecodeError as bad:
        return DriverResult(Outcome.HARNESS_ERROR,
                            f"unreadable report payload: {bad}", log=log, seconds=seconds)
    harness = state.get("harness", "UNKNOWN")
    subtests = state.get("subtests", [])
    message = state.get("message", "")
    if harness == "ERROR":
        return DriverResult(Outcome.HARNESS_ERROR, message, subtests, log=log, seconds=seconds)
    if harness == "TIMEOUT":
        return DriverResult(Outcome.TIMEOUT, message, subtests, log=log, seconds=seconds)
    if harness != "OK":
        return DriverResult(Outcome.HARNESS_ERROR, f"harness status {harness}: {message}",
                            subtests, log=log, seconds=seconds)
    # A harness that finished with no subtests at all ran nothing. Calling that
    # a PASS is exactly the mistake this whole file exists to avoid.
    if not subtests:
        return DriverResult(Outcome.HARNESS_ERROR, "harness OK but reported no subtests",
                            log=log, seconds=seconds)
    worst = Outcome.PASS
    for one in subtests:
        if one.get("status") != "PASS":
            worst = Outcome.FAIL
    return DriverResult(worst, message, subtests, log=log, seconds=seconds)


# --- expectations -----------------------------------------------------------

def expectation_lines(results):
    """Every deviation from `everything passes`, as sorted TSV.

    Subtest names are JSON-quoted, so a name containing a tab or a newline - WPT
    has both - cannot split a line. The file is meant to be read and diffed by a
    person, which is why it is not simply a JSON dump.

    NO MESSAGES. A test-level line carries its status and nothing else, because
    an assertion message contains addresses and property values that differ run
    to run, and an expectations file that churns is one nobody re-reads. The
    exception is SKIP, whose message IS the reason and is the whole point of the
    line - a skip that stopped naming a feature would slip past unnoticed.
    """
    lines = []
    for result in sorted(results, key=lambda r: r.rel):
        if result.status == Outcome.SKIP:
            lines.append(f"{result.rel}\tSKIP\t{result.message}")
            continue
        if result.status != Outcome.PASS:
            lines.append(f"{result.rel}\t{result.status}")
        for one in result.subtests:
            if one.get("status") != "PASS":
                lines.append(f"{result.rel}\tSUBTEST\t{one['status']}\t"
                             f"{json.dumps(one.get('name', ''))}")
    return lines


def parse_expectations(path: Path):
    if not path.exists():
        return set()
    return {line for line in path.read_text(encoding="utf-8").splitlines()
            if line and not line.startswith("#")}


def gate(results, path: Path):
    """No regressions AND no unexpected passes.

    Both directions fail, and the second is the point: an expectation that has
    quietly started passing is a file telling a lie about the engine, and a gate
    that only checked one direction would keep it there forever.

    SCOPED TO THE TESTS THIS RUN ACTUALLY RAN, which is what makes one file
    serve both `--gate` (a fixed subset, in under two minutes) and `--dir
    dom/nodes --check` (one suite). Without the scope the gate's 40 tests would
    be compared against the whole file and every line belonging to a test it did
    not run would be reported as an unexpected pass - a gate that is red on a
    green tree, which is a gate that gets switched off.
    """
    ran = {result.rel for result in results}
    expected = {line for line in parse_expectations(path)
                if line.split("\t", 1)[0] in ran}
    actual = set(expectation_lines(results))
    return sorted(actual - expected), sorted(expected - actual)


# --- reporting --------------------------------------------------------------

class Result:
    def __init__(self, rel, status, message="", subtests=None, signal=None, seconds=0.0):
        self.rel = rel
        self.status = status
        self.message = message
        self.subtests = subtests or []
        self.signal = signal
        self.seconds = seconds


def table(results, corpus, elapsed):
    by_dir = {}
    for result in results:
        by_dir.setdefault(str(Path(result.rel).parent), []).append(result)
    sub_total = {}
    print(f"\nwpt {corpus[:10]}  {len(results)} files  {elapsed:.1f}s")
    print(f"{'directory':<34}" + "".join(f"{name:>10}" for name in ORDER) + f"{'subtests':>10}")
    print("-" * (34 + 10 * len(ORDER) + 10))
    totals = {name: 0 for name in ORDER}
    for where in sorted(by_dir):
        counts = {name: 0 for name in ORDER}
        subs = 0
        for result in by_dir[where]:
            counts[result.status] = counts.get(result.status, 0) + 1
            totals[result.status] = totals.get(result.status, 0) + 1
            subs += len(result.subtests)
            for one in result.subtests:
                key = one.get("status", "UNKNOWN")
                sub_total[key] = sub_total.get(key, 0) + 1
        print(f"{where:<34}" + "".join(f"{counts[n]:>10}" for n in ORDER) + f"{subs:>10}")
    print("-" * (34 + 10 * len(ORDER) + 10))
    print(f"{'TOTAL':<34}" + "".join(f"{totals[n]:>10}" for n in ORDER))
    print("\nsubtests: " + "  ".join(f"{k}={v}" for k, v in sorted(sub_total.items()) if v))
    return totals, sub_total


# An assertion message with the values taken out, so that a thousand of them
# collapse into the handful of CAUSES they actually are. `assert_equals: foo
# expected 3 but got 7` and the same line with 4 and 9 are one finding, and a
# ranked list that cannot see that is a list of noise.
NUMBERS = re.compile(r"\d+")
QUOTED = re.compile(r'"[^"]*"|\'[^\']*\'')


# A message full of escaped code points - WPT assertion diffs are, because the
# corpus tests astral characters on purpose - must be collapsed BEFORE the digit
# substitution below, not after: `\uXXXX` is mostly digits, so substituting
# first turns the escapes into an unreadable hash of `U+N` and destroys the very
# text that would have identified the cause.
ESCAPE_RUN = re.compile(r"(?:U\+[0-9A-Fa-f]{2,6}\s*){2,}")


def cause(message: str) -> str:
    first = (message or "").strip().splitlines()[0] if message else "(no message)"
    first = ESCAPE_RUN.sub("<escapes>", first)
    first = QUOTED.sub("_", NUMBERS.sub("N", first))
    return first[:110]


def causes(rows, label, limit=12):
    """The ranked reasons, so the biggest one is fixed first rather than the
    first one that was noticed."""
    if not rows:
        return
    counted = {}
    for message in rows:
        key = cause(message)
        counted[key] = counted.get(key, 0) + 1
    print(f"\n{label} ({len(rows)}), by cause:")
    for key, count in sorted(counted.items(), key=lambda kv: (-kv[1], kv[0]))[:limit]:
        print(f"  {count:>6}  {key}")


def pinned_commit():
    """The pin fetch-wpt.sh names, read from the script rather than repeated."""
    if not FETCH_SCRIPT.exists():
        return None
    found = re.search(r'WPT_COMMIT="\$\{WPT_COMMIT:-([0-9a-f]{40})\}"',
                      FETCH_SCRIPT.read_text(encoding="utf-8"))
    return found.group(1) if found else None


# --- the negative proofs ----------------------------------------------------
#
# THE INSTRUMENT IS NOT EVIDENCE UNTIL IT HAS BEEN SEEN TO FAIL. Every fixture
# in tools/wpt/selftest/ has one required outcome, asserted here rather than
# read off a table by eye. A run of these that passes is what makes a run of the
# real corpus mean anything - and each of the four failure modes below has, at
# some point in this file's life, been reported as a pass by something.
SELFTESTS = [
    ("selftest/must-pass.html", Outcome.PASS, 2, {"PASS": 2}),
    ("selftest/must-fail.html", Outcome.FAIL, 2, {"PASS": 1, "FAIL": 1}),
    ("selftest/never-done.html", Outcome.TIMEOUT, None, None),
    ("selftest/throws-on-load.html", Outcome.HARNESS_ERROR, None, None),
    ("selftest/no-harness.html", Outcome.HARNESS_ERROR, 0, {}),
]


def selftest(driver: Path, wpt: Path, memory_mb: int) -> int:
    print("SELF-TEST: does this harness report a failure as a failure?\n")
    print(f"{'fixture':<34}{'want':>15}{'got':>15}   subtests")
    print("-" * 86)
    bad = 0
    for rel, want, want_subs, want_status in SELFTESTS:
        page = SELFTEST_DIR / Path(rel).name
        plan = Plan(rel, page=page, timeout=TIMEOUT_NORMAL)
        got = run_one(plan, driver, wpt, memory_mb)
        seen = {}
        for one in got.subtests:
            seen[one.get("status")] = seen.get(one.get("status"), 0) + 1
        ok = got.status == want
        if want_subs is not None and len(got.subtests) != want_subs:
            ok = False
        if want_status is not None and seen != want_status:
            ok = False
        print(f"{Path(rel).name:<34}{want:>15}{got.status:>15}   "
              f"{len(got.subtests)} {seen or ''}  {'ok' if ok else 'WRONG'}")
        if not ok:
            bad += 1
            print(f"    message: {got.message[:200]}")
            print(f"    driver log: {got.log.strip()[-400:]}")
    print("-" * 86)
    if bad:
        print(f"\n{bad} of {len(SELFTESTS)} self-tests reported the wrong outcome. "
              "The harness is NOT load-bearing until they all pass.")
        return 1
    print(f"\nall {len(SELFTESTS)} self-tests reported the outcome they must: a failing "
          "test fails, a page that never finishes times out, and neither a page that "
          "threw during load nor one with no harness at all is called a pass.")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dir", action="append", default=[],
                        help="a WPT directory, e.g. dom/nodes (repeatable)")
    parser.add_argument("--gate", action="store_true",
                        help="run the fixed ctest subset and check it against "
                             "the expectations")
    parser.add_argument("--selftest", action="store_true",
                        help="run tools/wpt/selftest/ and assert each outcome")
    parser.add_argument("--filter", help="only tests whose path contains this")
    parser.add_argument("--limit", type=int, help="stop after this many tests")
    parser.add_argument("--jobs", type=int, default=4,
                        help="parallel drivers (default 4: the box has 8 vCPUs "
                             "and shares them)")
    parser.add_argument("--memory-mb", type=int, default=4096,
                        help="per-driver address-space cap (ulimit -v)")
    parser.add_argument("--wpt-dir", type=Path, default=DEFAULT_WPT)
    parser.add_argument("--driver", type=Path,
                        default=ROOT / "build" / "tools" / "ctdrive")
    parser.add_argument("--json", type=Path, help="write the full results here")
    parser.add_argument("--tsv", type=Path, help="write one line per test here")
    parser.add_argument("--expectations", type=Path, default=EXPECTATIONS)
    parser.add_argument("--update-expectations", action="store_true",
                        help="rewrite the expectations file from this run")
    parser.add_argument("--check", action="store_true",
                        help="fail on any difference from the expectations")
    args = parser.parse_args()

    wpt = args.wpt_dir.expanduser()
    if not (wpt / "resources" / "testharness.js").exists():
        sys.exit(f"no WPT harness at {wpt} - run tools/wpt/fetch-wpt.sh")
    if not args.driver.exists():
        sys.exit(f"{args.driver} not built - "
                 "cmake --build --preset default --target ctbrowser-tool-ctdrive")
    corpus = subprocess.run(["git", "-C", str(wpt), "rev-parse", "HEAD"],
                            capture_output=True, text=True).stdout.strip()
    # A TABLE READ AGAINST THE WRONG CORPUS IS WORSE THAN NO TABLE. The pin is
    # in fetch-wpt.sh and is not repeated here; this is the one place the two
    # are compared, and it refuses rather than warns.
    pinned = pinned_commit()
    if pinned and corpus and corpus != pinned:
        sys.exit(f"{wpt} is at {corpus}, but tools/wpt/fetch-wpt.sh pins {pinned}.\n"
                 "Re-run tools/wpt/fetch-wpt.sh, or move the pin deliberately.")

    # OUR REPORT HOOK, INSTALLED. WPT ships a do-nothing testharnessreport.js
    # and expects the vendor to replace it; this is that replacement, copied in
    # on every run so a re-fetch can never leave the corpus reporting nothing.
    shutil.copyfile(HERE / "testharnessreport.js", wpt / "resources" / "testharnessreport.js")

    if args.selftest:
        return selftest(args.driver, wpt, args.memory_mb)

    if args.gate:
        pairs = GATE_SELECTION
    else:
        pairs = [(where, args.filter) for where in args.dir]
    if not pairs:
        sys.exit("nothing to run: pass --dir <suite>, --gate or --selftest")

    candidates = []
    swept = 0
    for where, filter_text in pairs:
        swept += sweep_wrappers(wpt, where)
        candidates += collect(wpt, where, filter_text)
    if swept:
        print(f"swept {swept} generated wrapper(s) an interrupted run left behind")
    if args.limit:
        candidates = candidates[:args.limit]

    plans = [plan_for(path, wpt) for path in candidates]
    runnable = [p for p in plans if p.skip is None]
    directories = [where for where, _ in pairs]
    print(f"wpt {corpus[:10]}: {len(plans)} candidates, {len(runnable)} to run, "
          f"{len(plans) - len(runnable)} skipped, {args.jobs} workers, "
          f"{args.memory_mb} MB cap")

    results = [Result(p.rel, Outcome.SKIP, p.skip) for p in plans if p.skip]
    started = time.monotonic()
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = {pool.submit(run_one, plan, args.driver, wpt, args.memory_mb): plan
                       for plan in runnable}
            done = 0
            for future in concurrent.futures.as_completed(futures):
                plan = futures[future]
                try:
                    outcome = future.result()
                except Exception as bad:  # a bug in the runner is not a test result
                    outcome = DriverResult(Outcome.HARNESS_ERROR, f"runner error: {bad!r}")
                results.append(Result(plan.rel, outcome.status, outcome.message,
                                      outcome.subtests, outcome.signal, outcome.seconds))
                done += 1
                if done % 25 == 0 or done == len(runnable):
                    print(f"  {done}/{len(runnable)}", file=sys.stderr)
    finally:
        for plan in plans:
            if plan.wrapper is not None:
                plan.wrapper.unlink(missing_ok=True)

    elapsed = time.monotonic() - started
    totals, sub_total = table(results, corpus, elapsed)

    crashes = [r for r in results if r.status == Outcome.CRASH]
    if crashes:
        print(f"\nCRASHES ({len(crashes)}) - the most valuable finding here:")
        for one in sorted(crashes, key=lambda r: r.rel)[:20]:
            print(f"  {one.rel}: {one.message}")
    causes([r.message for r in crashes], "crash sites")
    causes([r.message for r in results if r.status == Outcome.HARNESS_ERROR],
           "harness errors")
    causes([one.get("message", "") for r in results for one in r.subtests
            if one.get("status") == "FAIL"], "failing subtests")
    causes([r.message for r in results if r.status == Outcome.SKIP], "skips", limit=20)

    slowest = sorted((r for r in results if r.status != Outcome.SKIP),
                     key=lambda r: -r.seconds)[:5]
    if slowest:
        print("\nslowest: " + ", ".join(f"{r.rel.rsplit('/', 1)[-1]} {r.seconds:.1f}s"
                                        for r in slowest))

    if args.json:
        args.json.write_text(json.dumps({
            "wpt_commit": corpus,
            "date": time.strftime("%Y-%m-%d"),
            "directories": directories,
            "seconds": round(elapsed, 1),
            "totals": totals,
            "subtest_totals": sub_total,
            "tests": [{"test": r.rel, "status": r.status, "message": r.message,
                       "subtests": r.subtests, "seconds": round(r.seconds, 2)}
                      for r in sorted(results, key=lambda r: r.rel)],
        }, indent=1), encoding="utf-8")
        print(f"\nwrote {args.json}")
    if args.tsv:
        args.tsv.write_text("".join(
            f"{r.rel}\t{r.status}\t{len(r.subtests)}\t"
            f"{r.message.splitlines()[0] if r.message else ''}\n"
            for r in sorted(results, key=lambda r: r.rel)), encoding="utf-8")
        print(f"wrote {args.tsv}")

    if args.update_expectations:
        # MERGED, NOT OVERWRITTEN. `--dir dom/nodes --update-expectations` must
        # rewrite what it measured and leave every other suite exactly as it
        # was; a whole-file rewrite would silently delete four suites' worth of
        # recorded failures because one directory was re-run, and the next gate
        # would then pass on a file that had lost its contents.
        ran = {result.rel for result in results}
        kept = [line for line in parse_expectations(args.expectations)
                if line.split("\t", 1)[0] not in ran]
        lines = sorted(kept + expectation_lines(results))
        header = [
            "# ctbrowser's known web-platform-test results. Written by",
            "# tools/wpt/run-wpt.py --update-expectations; read by --check and --gate.",
            "#",
            "# EVERY LINE IS A DEVIATION FROM `it passes`. A test with no line here",
            "# is expected to pass with every subtest passing, so the file only ever",
            "# grows when the engine is wrong and only shrinks when it is fixed.",
            "#",
            "# The gate fails on a difference in EITHER direction: a new failure is a",
            "# regression, and a line that has started passing is this file telling a",
            "# lie. Re-run with --update-expectations to record a change deliberately.",
            "#",
            f"# wpt {corpus}",
            f"# {', '.join(directories)} re-measured on {time.strftime('%Y-%m-%d')}; "
            f"{len(kept)} line(s) carried over from suites this run did not touch",
            "",
        ]
        args.expectations.write_text("\n".join(header + lines) + "\n", encoding="utf-8")
        print(f"wrote {args.expectations} ({len(lines)} lines, {len(kept)} carried over)")
        return 0

    if args.check or args.gate:
        regressions, fixed = gate(results, args.expectations)
        if regressions:
            print(f"\n{len(regressions)} UNEXPECTED FAILURE(S):")
            for line in regressions[:40]:
                print(f"  + {line}")
        if fixed:
            print(f"\n{len(fixed)} UNEXPECTED PASS(ES) - the expectations are now wrong:")
            for line in fixed[:40]:
                print(f"  - {line}")
        if regressions or fixed:
            print("\nIf these changes are intended:")
            print("  tools/wpt/run-wpt.py --gate --update-expectations")
            return 1
        print(f"\nmatches {args.expectations.name} exactly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
