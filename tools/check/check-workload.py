#!/usr/bin/env python3
"""Decide whether a workload is DONE: its ctbrowser tests run on native C++ from ctcompile.

A workload (Bootstrap, p5, Phaser, Babylon, ...) is DONE when every JS, CSS and
HTML test ctbrowser has for it runs on the native C++ that ctcompile generates,
with no VM, and passes the same checks it passes on the VM. This measures how
far each workload is from that, stage by stage, and says where the chain
breaks. The text output is written for an LLM to act on; --format json carries
the same data (schema 1).

  tools/check/check-workload.py bootstrap            # one workload
  tools/check/check-workload.py all --format json    # every listed workload
  tools/check/check-workload.py fixture              # proves the checker can say DONE
  tools/check/check-workload.py --list               # workloads and stages
  tools/check/check-workload.py --self-test          # needs no build

Run it where the build tree is (the shared devbox), while holding
/tmp/ctbrowser-devbox-build.lock. A missing build output is reported as ERROR
with the target to build.

Extending: a workload is a JSON file in tools/check/workloads/; defaults.json
is merged under every one, so one edit there changes them all. A stage is a
function registered with @stage(...). Every .py file in that directory is
loaded first with `stage`, `SetupError` and `StepTimeout` in its globals, so a
plugin can register a stage that a workload file then names. Statuses are
fixed: PASS, FAIL, MISSING (a capability that does not exist yet), BLOCKED (an
earlier stage did not pass for the same item), TIMEOUT, ERROR (the environment).

Time limits: every command has a limit (timeouts.command, or a stage's own
"timeout" option), every item a limit across its stages (timeouts.item), and
the run a budget (--timeout, default timeouts.total). A command over its limit
is killed with its whole process group.

Exit status: 0 every workload DONE, 1 some NOT DONE, 2 CANNOT CHECK (setup
error), 3 TIMED OUT (a command, an item or the run exceeded its limit).
"""

import argparse
import collections
import fnmatch
import hashlib
import html.parser
import importlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True  # Plugins must not leave __pycache__ in the tree.

ROOT = Path(__file__).resolve().parents[2]
WORKLOADS = Path(__file__).resolve().parent / "workloads"
SCHEMA = 1
SEVERITY = ("ERROR", "TIMEOUT", "FAIL", "MISSING", "BLOCKED", "PASS")  # Worst first.
# Verdict -> process exit status.
VERDICTS = {"DONE": 0, "NOT DONE": 1, "CANNOT CHECK": 2, "TIMED OUT": 3}
STAGES = {}  # Stage name -> registration record; filled by @stage.


class SetupError(Exception):
    """The environment cannot run a stage: a missing build output or tool."""


class StepTimeout(Exception):
    """One command, or one item's stages together, exceeded the time limit."""


class RunExhausted(Exception):
    """The run's --timeout budget is spent."""


def stage(name, describes, next_step, needs=None):
    """Register a stage. `describes` is what PASS proves, `next_step` what to do
    when it is the first gap, `needs` the stage an item must pass first."""

    def register(function):
        STAGES[name] = {"run": function, "describes": describes, "next": next_step, "needs": needs}
        return function

    return register


def tail(text, count=20):
    """Last `count` non-blank lines of `text`, noting how many were dropped."""
    lines = [line for line in text.splitlines() if line.strip()]
    hidden = len(lines) - count
    return ([f"... {hidden} earlier lines omitted"] if hidden > 0 else []) + lines[-count:]


def slug(item):
    """`item` reduced to a short, filesystem-safe name."""
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", item)[-120:]


class Context:
    """One workload's run: what it found, what it produced and every result."""

    def __init__(self, workload, build, deadline, root=ROOT):
        self.workload, self.build, self.deadline, self.root = workload, Path(build), deadline, root
        self.results, self.scripts, self.pages, self.ctests = [], {}, [], []
        self.spent, self.artifacts, self.scratch = collections.Counter(), {}, None

    def options(self, name):
        return dict(self.workload.get("options", {}).get(name) or {})

    def add(self, name, item, status, summary, details=(), metrics=None):
        assert status in SEVERITY, status
        record = {"stage": name, "item": item, "status": status, "summary": summary}
        self.results.append({**record, "details": list(details), "metrics": metrics or {}})

    def relative(self, path):
        path = Path(path).resolve()
        return str(path.relative_to(self.root)) if path.is_relative_to(self.root) else str(path)

    def tool(self, relative, target):
        path = self.build / relative
        if not path.exists():
            raise SetupError(f"{path} is missing; build it first: tools/remote-build.sh {target}")
        return path

    def run(self, command, name, item):
        """Run one command; return its exit code and stdout with stderr merged in."""
        code, output, _ = self.run_split(command, name, item, merge=True)
        return code, output

    def run_split(self, command, name, item, merge=False):
        """Run one command under the tightest of the command, item and run limits;
        return the exit code, stdout and stderr (None when merged into stdout)."""
        timeouts = self.workload["timeouts"]
        run_left = self.deadline - time.monotonic()
        if run_left <= 0:
            raise RunExhausted()
        item_left = timeouts["item"] - self.spent[item]
        if item_left <= 0:
            raise StepTimeout(f"the item exceeded its {timeouts['item']} s limit across stages")
        limit = min(
            float(self.options(name).get("timeout", timeouts["command"])), item_left, run_left
        )
        command = [str(part) for part in command]
        start = time.monotonic()
        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT if merge else subprocess.PIPE,
            text=True,
            errors="replace",
            start_new_session=True,
        )
        try:
            output, errors = process.communicate(timeout=limit)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.communicate()
            self.spent[item] += time.monotonic() - start
            if self.deadline - time.monotonic() <= 0:
                raise RunExhausted()
            raise StepTimeout(f"killed after {limit:.1f} s: {' '.join(command)}")
        self.spent[item] += time.monotonic() - start
        return process.returncode, output, errors

    def each(self, name, items, body):
        """Run `body` per item, unless an earlier stage left the item unfinished.

        An item satisfies `needs` with a result from that stage or a later one: a
        later stage may have introduced it (page-scripts adds the scripts it finds)."""
        order = self.workload["stages"]
        needs = STAGES[name]["needs"]
        needed = needs in order[: order.index(name)]
        for item in items:
            earlier = [r for r in self.results if r["item"] == item]
            failed = next((r for r in earlier if r["status"] != "PASS"), None)
            reached = [order.index(r["stage"]) for r in earlier if r["stage"] in order]
            if failed:
                self.add(name, item, "BLOCKED", f"blocked: {failed['stage']} is {failed['status']}")
            elif needed and max(reached, default=-1) < order.index(needs):
                self.add(name, item, "BLOCKED", f"blocked: {needs} produced nothing for it")
            else:
                try:
                    body(item)
                except StepTimeout as error:
                    self.add(name, item, "TIMEOUT", str(error))
                except SetupError as error:
                    self.add(name, item, "ERROR", str(error))


def harness(module):
    """The lit harness's own constants and helpers, so flags live in one place."""
    tests = str(ROOT / "ctcompile/test")
    if tests not in sys.path:
        sys.path.insert(0, tests)
    try:
        return importlib.import_module(module)
    except ImportError as error:
        raise SetupError(f"cannot import {module} from ctcompile/test: {error}")


def patterns(entries):
    """Yield (pattern, optional) for each entry. An entry is a glob string, or
    {"pattern": ..., "optional": bool}; an optional pattern may match nothing.
    A script entry may also name "string_entries": the functions the embedder
    calls from outside the script with String arguments (see string_entries)."""
    for entry in entries:
        yield (
            (entry, False)
            if isinstance(entry, str)
            else (entry["pattern"], entry.get("optional", False))
        )


def string_entries(script):
    """The lowering's string-entries value for `script`, or "" when it has none."""
    return ",".join(script.get("string_entries", []))


def skim(text, regexes=False):
    """`text` with comments and string/template literals blanked to spaces,
    keeping newlines, so structure scans (braces, declarations) see code only.
    With `regexes`, also blanks what looks like a regular-expression literal: a
    `/` after anything but an identifier, number, `)`, `]`, `++` or `--` (the
    usual heuristic); a division after one of those stays readable."""
    out, i, n = list(text), 0, len(text)
    significant = ""  # the last non-space character kept (quoted text reads as its quote)
    tail = ""  # the last two, so `++` and `--` read as one token
    while i < n:
        c = text[i]
        if c in "\"'`":
            quote = c
            out[i] = " "
            i += 1
            while i < n:
                if text[i] == "\\":
                    out[i] = out[i + 1] = " "
                    i += 2
                    continue
                done = text[i] == quote
                if text[i] != "\n":
                    out[i] = " "
                i += 1
                if done:
                    break
            significant = quote
            tail = (tail + quote)[-2:]
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            out[i] = out[i + 1] = " "
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i + 1 < n:
                out[i] = out[i + 1] = " "
                i += 2
        elif regexes and c == "/" and not (
            significant.isalnum() or significant in "_$)]" or tail.endswith(("++", "--"))
        ):
            out[i] = " "
            i += 1
            in_class = False
            while i < n and text[i] != "\n":
                if text[i] == "\\" and i + 1 < n:
                    out[i] = out[i + 1] = " "
                    i += 2
                    continue
                if text[i] == "[":
                    in_class = True
                elif text[i] == "]":
                    in_class = False
                done = text[i] == "/" and not in_class
                out[i] = " "
                i += 1
                if done:
                    break
            significant = "/"
            tail = (tail + "/")[-2:]
        else:
            if not c.isspace():
                significant = c
                tail = (tail + c)[-2:]
            i += 1
    return "".join(out)


def directives(text):
    """A script's directive prologue (such as {"use strict"}): the string
    literals of its leading expression statements, comments skipped. Two
    scripts joined into one keep only the first script's prologue, so a join
    is safe only when the prologues match."""
    found = set()
    i, n = 0, len(text)
    while True:
        while i < n:
            if text[i].isspace():
                i += 1
            elif text.startswith("//", i):
                i = text.find("\n", i)
                if i < 0:
                    return found
            elif text.startswith("/*", i):
                end = text.find("*/", i + 2)
                if end < 0:
                    return found
                i = end + 2
            else:
                break
        if i >= n or text[i] not in "\"'":
            return found
        quote = text[i]
        i += 1
        value = []
        while i < n and text[i] != quote:
            if text[i] == "\\" and i + 1 < n:
                i += 1
            value.append(text[i])
            i += 1
        if i >= n:
            return found
        i += 1  # past the closing quote
        found.add("".join(value))
        while i < n and text[i] in " \t":
            i += 1
        if i < n and text[i] == ";":
            i += 1


WORD = re.compile(r"[A-Za-z_$][\w$]*")


def binding_names(code, start):
    """The names one declarator binds, from its start up to `=` or the list's
    end: a plain name, or the identifiers of a destructuring pattern at its
    first nesting level (defaults after `=` and keys before `:` skipped, rest
    elements read by their last name)."""
    pos = start
    while pos < len(code) and code[pos].isspace():
        pos += 1
    if pos >= len(code):
        return set(), pos
    if code[pos] not in "{[":
        match = WORD.match(code, pos)
        return ({match.group(0)}, match.end()) if match else (set(), pos)
    # A destructuring pattern: every identifier at nesting level 1 that is not
    # a property key (not followed by `:`) and not inside a default value.
    names, depth, pos = set(), 0, pos
    key_or_default = False
    while pos < len(code):
        c = code[pos]
        if c in "{[":
            depth += 1
            key_or_default = False
        elif c in "}]":
            depth -= 1
            if depth == 0:
                return names, pos + 1
        elif depth == 1:
            if c == ":":
                key_or_default = True
            elif c == ",":
                key_or_default = False
            elif c == "=":
                # A default value: skip it to the next `,` at level 1 or out.
                inner = 0
                pos += 1
                while pos < len(code) and (inner or code[pos] not in ",}]"):
                    if code[pos] in "{[(":
                        inner += 1
                    elif code[pos] in "})]":
                        if inner:
                            inner -= 1
                        elif code[pos] in "}]":
                            break
                    pos += 1
                continue
            elif not key_or_default:
                match = WORD.match(code, pos)
                if match and match.group(0) not in (
                    "true", "false", "null", "undefined", "new", "typeof", "in", "of", "instanceof",
                ):
                    names.add(match.group(0))
                    pos = match.end()
                    continue
        pos += 1
    return names, pos


def top_level_declarations(text):
    """The names `text` declares at its top level: function and class
    declarations and var/let/const bindings at brace depth 0, including
    generator functions, comma-separated binding lists and destructuring
    patterns. A declaration at depth 0 of a later script hoists into a joined
    program, changing what an earlier script sees; one inside a block or
    function body does not."""
    code = skim(text)
    names, pos, n = set(), 0, len(code)
    depth = 0
    depth_at = [0] * n
    for i, c in enumerate(code):
        depth_at[i] = depth
        if c == "{":
            depth += 1
        elif c == "}":
            depth = max(0, depth - 1)
    while pos < n:
        match = WORD.search(code, pos)
        if not match:
            break
        word = match.group(0)
        if depth_at[match.start()] != 0:
            pos = match.end()
            continue
        if word in ("function", "class"):
            # function NAME, function* NAME, class NAME; an anonymous
            # `export default function () {}` binds nothing the library names.
            at = match.end()
            while at < n and code[at].isspace():
                at += 1
            if word == "function" and at < n and code[at] == "*":
                at += 1
                while at < n and code[at].isspace():
                    at += 1
            named = WORD.match(code, at)
            if named:
                names.add(named.group(0))
            pos = match.end()
        elif word in ("var", "let", "const"):
            pos = match.end()
            while pos < n:
                bound, pos = binding_names(code, pos)
                names.update(bound)
                while pos < n and code[pos].isspace():
                    pos += 1
                if pos < n and code[pos] == "=":
                    # Skip the initializer to the next depth-0 comma or `;`.
                    inner = 0
                    pos += 1
                    while pos < n and (inner or code[pos] not in ",;"):
                        if code[pos] in "({[":
                            inner += 1
                        elif code[pos] in ")}]":
                            if inner:
                                inner -= 1
                        pos += 1
                if pos >= n or code[pos] == ";":
                    break
                if code[pos] == ",":
                    pos += 1
                    continue
                break
        else:
            pos = match.end()
    return names


def join_problem(library, probe):
    """Why joining `library` and `probe` into one classic script would change
    what they do as two scripts, or None when the join is safe. Two scripts run
    as two programs: the first before the second's declarations exist, and each
    with its own directive prologue. When the scan cannot read the probe
    safely, it says so and the join is refused: a refused join is safe, a wrong
    join is not."""
    if directives(library) != directives(probe):
        return (
            "their directive prologues differ "
            f"({sorted(directives(library))} vs {sorted(directives(probe))}), "
            "and one joined script keeps only the first"
        )
    # A `/` outside comments is a regular-expression literal or a division;
    # skim would blank a quoted character inside one and could hide a probe
    # declaration, so the scan refuses rather than risk a wrong join.
    if "/" in skim(probe):
        return (
            "the probe uses a regular expression or a division, which the "
            "declaration scan cannot read safely"
        )
    # The regex/division ambiguity in the library is undecidable by a lexical
    # scan, so read it both ways and refuse on any mention either reading
    # shows: blanking too little and blanking too much are both covered.
    readings = (skim(library), skim(library, regexes=True))
    mentioned = {
        name
        for name in top_level_declarations(probe)
        if any(re.search(rf"(?<![\w$]){re.escape(name)}(?![\w$])", code) for code in readings)
    }
    if mentioned:
        return (
            f"the probe declares {sorted(mentioned)} at its top level, which the "
            "library mentions; joined, the declaration hoists above the library"
        )
    return None


@stage(
    "discover",
    "every script, page and CTest the workload file declares exists",
    "Fix tools/check/workloads/<workload>.json: a declared path or test no longer exists.",
)
def discover(ctx, name):
    for kind in ("scripts", "pages"):
        for entry in ctx.workload.get(kind, []):
            ((pattern, optional),) = patterns([entry])
            entries = [] if isinstance(entry, str) else entry.get("string_entries", [])
            opt_out = None if isinstance(entry, str) else entry.get("script-run")
            matches = sorted(path for path in ctx.root.glob(pattern) if path.is_file())
            if not matches:
                status = "PASS" if optional else "ERROR"
                ctx.add(name, f"{kind[:-1]}:{pattern}", status, "matches no file")
            for path in matches:
                if kind == "pages":
                    ctx.pages.append(path)
                    continue
                item = f"script:{ctx.relative(path)}"
                ctx.scripts[item] = {
                    "path": path,
                    "origin": "declared",
                    "module": False,
                    "string_entries": entries,
                    "script_run": opt_out,
                }
                digest = hashlib.sha256(path.read_bytes()).hexdigest()[:12]
                ctx.add(name, item, "PASS", f"{path.stat().st_size} bytes, sha256 {digest}")
    if not ctx.workload.get("ctests"):
        return
    code, output = ctx.run(["ctest", "--test-dir", ctx.build, "-N"], name, "workload")
    known = re.findall(r"^\s*Test\s+#\d+: (\S+)", output, re.M)
    if code or not known:
        raise SetupError(f"ctest lists no tests in {ctx.build}; configure and build there first")
    for pattern, optional in patterns(ctx.workload["ctests"]):
        hits = fnmatch.filter(known, pattern)
        if not hits:
            status = "PASS" if optional else "ERROR"
            ctx.add(name, f"ctest:{pattern}", status, "no configured CTest matches")
        for test in hits:
            if test not in ctx.ctests:
                ctx.ctests.append(test)
                ctx.add(name, f"ctest:{test}", "PASS", "configured")


@stage(
    "vm-reference",
    "each CTest passes on the VM, the reference a native run must match",
    "Fix the browser or the test first: a native run cannot match a failing reference.",
    needs="discover",
)
def vm_reference(ctx, name):
    def body(item):
        test = item.split(":", 1)[1]
        if not re.fullmatch(r"[\w.+-]+", test):
            raise SetupError(f"test name {test!r} cannot be selected safely with ctest -R")
        command = ["ctest", "--test-dir", ctx.build, "--output-on-failure", "--no-tests=error"]
        code, output = ctx.run([*command, "-R", f"^{test}$"], name, item)
        if code == 0:
            ctx.add(name, item, "PASS", "passes on the VM")
        elif "Could not find executable" in output or "Not Run" in output:
            raise SetupError(f"{test} is not built; build its ctbrowser-test-* or example target")
        else:
            ctx.add(name, item, "FAIL", "fails on the VM", tail(output))

    ctx.each(name, [f"ctest:{test}" for test in ctx.ctests], body)


class ScriptTags(html.parser.HTMLParser):
    """Collect each <script>'s attributes and inline text, in document order."""

    def __init__(self):
        super().__init__()
        self.scripts, self.open = [], None

    def handle_starttag(self, tag, attrs):
        if tag == "script":
            self.open = (dict(attrs), [])

    def handle_data(self, data):
        if self.open:
            self.open[1].append(data)

    def handle_endtag(self, tag):
        if tag == "script" and self.open:
            self.scripts.append((self.open[0], "".join(self.open[1])))
            self.open = None


@stage(
    "page-scripts",
    "every page's scripts, inline and external, join the scripts to compile",
    "Fix the page or the workload file: a page names a script that is not in the tree.",
    needs="discover",
)
def page_scripts(ctx, name):
    known = {script["path"].resolve(): item for item, script in ctx.scripts.items()}
    for page in ctx.pages:
        page_item = f"page:{ctx.relative(page)}"
        tags = ScriptTags()
        tags.feed(page.read_text(errors="replace"))
        inline = external = 0
        problems = []
        for index, (attrs, text) in enumerate(tags.scripts):
            module = attrs.get("type") == "module"
            if attrs.get("src"):
                target = (page.parent / attrs["src"]).resolve()
                if not target.is_file() or not target.is_relative_to(ctx.root):
                    problems.append(f"script {index}: src={attrs['src']} is not a file in the tree")
                    continue
                external += 1
                if target not in known:
                    item = f"script:{ctx.relative(target)}"
                    known[target] = item
                    ctx.scripts[item] = {"path": target, "origin": page_item, "module": module}
                    ctx.add(name, item, "PASS", f"loaded by {page_item}")
            elif text.strip():
                inline += 1
                path = ctx.scratch / "pages" / f"{page.stem}.{index}.js"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
                item = f"script:{ctx.relative(page)}#{index}"
                ctx.scripts[item] = {"path": path, "origin": page_item, "module": module}
                ctx.add(name, item, "PASS", f"inline script {index} of {page_item}")
        summary = f"{inline} inline and {external} external script(s)"
        ctx.add(name, page_item, "FAIL" if problems else "PASS", summary, problems)


@stage(
    "admission",
    "ctcompile claims every function that can run natively: native-claims.py finds no refusal "
    "and no skip, and every function it does not claim was removed as unreachable",
    "Take the most common refusal reason in DETAILS, find the lowering that refuses it, and "
    "teach it the construct under the native-output invariants; rerun to see the next reason.",
)
def admission(ctx, name):
    translate = ctx.tool("ctcompile/tools/ctjs-translate/ctjs-translate", "ctjs-translate")
    opt = ctx.tool("ctcompile/tools/ctjs-opt/ctjs-opt", "ctjs-opt")
    claims = ctx.root / "tools/check/native-claims.py"
    seconds = int(ctx.options(name).get("timeout", ctx.workload["timeouts"]["command"]))

    def body(item):
        script = ctx.scripts[item]
        if script["module"]:
            ctx.add(name, item, "MISSING", "module scripts have no native pipeline yet")
            return
        out = ctx.scratch / "claims" / f"{slug(item)}.json"
        out.parent.mkdir(parents=True, exist_ok=True)
        command = [sys.executable, claims, "--translate", translate, "--opt", opt]
        command += ["--corpus", script["path"], "--name", slug(item), "--json", out]
        if string_entries(script):
            command += ["--lower-options", f"string-entries={string_entries(script)}"]
        code, output = ctx.run([*command, "--timeout", seconds], name, item)
        if code or not out.exists():
            ctx.add(name, item, "FAIL", "native-claims.py failed", tail(output))
            return
        claim = json.loads(out.read_text())
        keep = (
            "claimed",
            "total",
            "refused",
            "skipped",
            "pruned",
            "removed",
            "globals",
            "globals_resolved",
        )
        metrics = {key: claim.get(key) for key in keep}
        summary = (
            f"{claim['claimed']} of {claim['total']} functions native "
            f"({claim['claimed_percent']}%), {claim['refused']} refused, "
            f"{claim['skipped']} never imported, {claim['pruned']} pruned, "
            f"{claim.get('removed', 0)} removed as unreachable"
        )
        # A function removed as unreachable is not native: it cannot run, so
        # it counts toward the total without being claimed, and each one is
        # listed by name, on a pass too, rather than folded into the native
        # count.
        unreachable = [
            f"pruned {function}: unreachable before admission"
            for function in claim.get("pruned_names", [])
        ]
        unreachable += [
            f"removed {function}: {reason}" for function, reason in claim.get("removed_rows", [])
        ]
        if (
            claim["claimed"] + claim["pruned"] + claim.get("removed", 0) == claim["total"]
            and not claim["refused"]
            and not claim["skipped"]
        ):
            ctx.add(name, item, "PASS", summary, unreachable, metrics)
            return
        details = [*unreachable, "most common refusal reasons (count, reason):"]
        details += [f"  {count:6d}  {reason}" for reason, count in claim["refusals"][:8]]
        details += [
            f"  {count:6d}  [not imported] {reason}" for reason, count in claim["skips"][:4]
        ]
        ctx.add(name, item, "FAIL", summary, details, metrics)

    ctx.each(name, list(ctx.scripts), body)


@stage(
    "emit-cpp",
    "the native pipeline (ctcompile/test/CTNative/Checks/pipeline.cmake) lowers the script "
    "and ctjs-translate --mlir-to-cpp writes C++",
    "Read the pipeline's diagnostic in DETAILS; it names the stage and the function.",
    needs="admission",
)
def emit_cpp(ctx, name):
    translate = ctx.tool("ctcompile/tools/ctjs-translate/ctjs-translate", "ctjs-translate")
    opt = ctx.tool("ctcompile/tools/ctjs-opt/ctjs-opt", "ctjs-opt")
    pipeline = ctx.root / "ctcompile/test/CTNative/Checks/pipeline.cmake"
    defines = [f"-D{key}={value}" for key, value in ctx.options(name).get("defines", {}).items()]

    def body(item):
        base = ctx.scratch / "native" / slug(item)
        base.parent.mkdir(parents=True, exist_ok=True)
        emitc, cpp = Path(f"{base}.emitc.mlir"), Path(f"{base}.cpp")
        source = ctx.scripts[item]["path"]
        command = ["cmake", f"-DTRANSLATE={translate}", f"-DOPT={opt}", f"-DSOURCE={source}"]
        if string_entries(ctx.scripts[item]):
            command.append(f"-DSTRING_ENTRIES={string_entries(ctx.scripts[item])}")
        code, output = ctx.run(
            [*command, f"-DOUTPUT={emitc}", *defines, "-P", pipeline], name, item
        )
        if code:
            ctx.add(name, item, "FAIL", "pipeline.cmake failed", tail(output))
            return
        code, output = ctx.run([translate, "--mlir-to-cpp", emitc, "-o", cpp], name, item)
        if code or not cpp.exists():
            ctx.add(name, item, "FAIL", "ctjs-translate --mlir-to-cpp failed", tail(output))
            return
        ctx.artifacts[item] = {"cpp": cpp}
        lines = len(cpp.read_text(errors="replace").splitlines())
        ctx.add(name, item, "PASS", f"{lines} lines of C++")

    ctx.each(name, list(ctx.scripts), body)


def strip_refused(text):
    """`text`, a lowered module, without its top-level ctjs.func operations, and
    how many there were. A refused function stays a ctjs.func; a native one is
    an emitc.func. A body starts with a line ending in `{` and ends at the next
    line that starts with `  }` (the module's own indentation). Only a dry run
    (never DONE evidence) uses this, to reach the stages after admission."""
    kept, removed, inside = [], 0, False
    for line in text.splitlines(keepends=True):
        if inside:
            inside = not line.startswith("  }")
            continue
        if line.startswith("  ctjs.func "):
            removed += 1
            inside = line.rstrip().endswith("{")
            continue
        kept.append(line)
    return "".join(kept), removed


# A VM-free test that links ctbrowser::page (ctcompile/test/cmake/
# NativeWorkloads.cmake): its link line in build.ninja is the one CMake works
# out for the page library and everything under it (raster's ANGLE, fonts,
# simdutf, mimalloc), which a hand-written list would get wrong.
PAGE_LINKED = "ctcompile/test/ctcompile-test-native-page"


def dom_toolchain(ctx):
    """For build-native's "dom" option: the Clang the build chose for programs
    that use ctbrowser's DOM (Native.cmake's %dom_clang, recorded in the
    generated lit.site.cfg.py; never a fallback such as /usr/bin/clang++, whose
    libstdc++ pairing has no std::expected), the include flags that
    ctcompile/test/CTNative/Browser/dom/dom.py's link_options() gives the lit
    drivers (Boost and simdutf headers), and PAGE_LINKED's libraries."""
    site = ctx.build / "ctcompile/test/lit.site.cfg.py"
    match = site.exists() and re.search(r'^config\.dom_clang = "([^"]+)"', site.read_text(), re.M)
    if not match:
        raise SetupError(
            f"{site} names no dom_clang; configure the build: tools/remote-build.sh ctjs-opt"
        )
    dom = harness("CTNative.Browser.dom.dom")
    paths = argparse.Namespace(build=ctx.build, include=ctx.root / "ctbrowser/include")
    try:
        includes, _ = dom.link_options(paths, selectors=True)
    except (RuntimeError, OSError) as error:
        raise SetupError(f"{error}; build it first: tools/remote-build.sh ctbrowser-dom")
    ninja = ctx.build / "build.ninja"
    rule = re.search(
        rf"^build {re.escape(PAGE_LINKED)}: [^\n]*(?:\n  [^\n]*)*?\n  LINK_LIBRARIES = ([^\n]*)",
        ninja.read_text() if ninja.exists() else "",
        re.M,
    )
    built = (ctx.build / PAGE_LINKED).exists()
    if not rule or not built:
        raise SetupError(
            f"{PAGE_LINKED} is not built, and build-native takes the page library's link "
            "line from it: tools/remote-build.sh ctcompile-test-native-page"
        )
    libraries = [
        str(ctx.build / part) if part.startswith("lib/") else part for part in rule[1].split()
    ]
    return match[1], includes, libraries


@stage(
    "build-native",
    "the C++ compiles with GCC and Clang under the gate's flags (-std=c++23 -Werror ...)",
    "Read the compiler diagnostic in DETAILS and fix the emitter or the lowering that produced it.",
    needs="emit-cpp",
)
def build_native(ctx, name):
    cpp_harness = harness("Target.Cpp.harness")
    try:
        compilers = cpp_harness.find_compilers()
    except RuntimeError as error:
        raise SetupError(str(error))
    flags = [*cpp_harness.FLAGS, *ctx.options(name).get("flags", [])]
    link = ctx.options(name).get("link", [])
    if ctx.options(name).get("dom"):
        clang, includes, libraries = dom_toolchain(ctx)
        compilers = [compilers[0], clang]
        flags, link = [*flags, *includes], [*link, *libraries]

    def body(item):
        cpp, binaries = ctx.artifacts[item]["cpp"], []
        for compiler in compilers:
            binary = cpp.with_suffix("." + Path(compiler).name)
            code, output = ctx.run([compiler, *flags, cpp, "-o", binary, *link], name, item)
            if code:
                ctx.add(name, item, "FAIL", f"{Path(compiler).name} rejects the C++", tail(output))
                return
            binaries.append(binary)
        ctx.artifacts[item]["binaries"] = binaries
        ctx.add(name, item, "PASS", "built with " + " and ".join(Path(c).name for c in compilers))

    ctx.each(name, list(ctx.scripts), body)


@stage(
    "no-vm-gate",
    "no VM in the output: neither the C++ nor the binaries mention ctbrowser::script, "
    "ctbrowser::aot or ct_aot_",
    "The lowering left a call into the VM; find the construct it boxed and lower it natively.",
    needs="build-native",
)
def no_vm_gate(ctx, name):
    vm = harness("CTNative.harness").VM_SYMBOL
    include = re.compile(r"#include\s*[<\"]ctbrowser/(?:script/|aot/)")
    nm = shutil.which("llvm-nm") or shutil.which("nm")
    if not nm:
        raise SetupError("no nm or llvm-nm on PATH; the gate cannot read the binaries")

    def body(item):
        artifacts = ctx.artifacts[item]
        text = artifacts["cpp"].read_text(errors="replace")
        hits = [f"C++: {m.group(0)}" for m in [*vm.finditer(text), *include.finditer(text)]]
        for binary in artifacts["binaries"]:
            code, symbols = ctx.run([nm, "-C", binary], name, item)
            if code or not re.search(r"\bmain\b", symbols):
                raise SetupError(f"nm could not list {binary}; the gate would pass vacuously")
            hits += [
                f"{binary.name}: {line.strip()}" for line in symbols.splitlines() if vm.search(line)
            ]
        if hits:
            ctx.add(name, item, "FAIL", f"{len(hits)} reference(s) to the VM", hits[:12])
        else:
            ctx.add(name, item, "PASS", "no VM symbol or include")

    ctx.each(name, list(ctx.scripts), body)


@stage(
    "script-run",
    "every native binary prints the same globals as the VM (ctcompile-test-native-reference), "
    "and the VM prints at least one",
    "The native program computes something the VM does not; reduce the script to the first "
    "differing global and compare the lowering with the interpreter's semantics.",
    needs="build-native",
)
def script_run(ctx, name):
    reference = ctx.tool(
        "ctcompile/test/ctcompile-test-native-reference", "ctcompile-test-native-reference"
    )

    # A library whose globals say nothing (bootstrap.bundle.js publishes one
    # object) is checked by behaviour instead: {"probe": <a .js file>} runs the
    # script and then the probe in each of the workload's pages, and compares
    # the console.log lines the probe prints.
    def probed(item, probe):
        if not ctx.pages:
            ctx.add(name, item, "FAIL", "a script-run probe needs the workload's pages")
            return
        expected, lines = {}, 0
        for page in ctx.pages:
            command = [reference, "--page", page, "--script", ctx.scripts[item]["path"]]
            code, output, notes = ctx.run_split([*command, "--script", probe], name, item)
            if code:
                summary = f"the VM reference failed probing {ctx.relative(page)}"
                ctx.add(name, item, "FAIL", summary, tail(output + notes))
                return
            expected[page] = output.splitlines()
            lines += len(expected[page])
        if not lines:
            ctx.add(name, item, "FAIL", "the probe printed nothing, so it would prove nothing")
            return
        binaries = page_program(item, probe)
        if binaries is None:
            return
        for binary in binaries:
            for page, want in expected.items():
                code, actual, errors = ctx.run_split([binary, page], name, item)
                got = actual.splitlines()
                if code or got != want:
                    where = f"{binary.name} on {ctx.relative(page)}"
                    summary = f"{where} exited {code}" if code else f"{where} differs"
                    diff = [
                        f"line {i + 1}: VM {want[i : i + 1]} native {got[i : i + 1]}"
                        for i in range(max(len(got), len(want)))
                        if got[i : i + 1] != want[i : i + 1]
                    ]
                    ctx.add(name, item, "FAIL", summary + " from the VM", diff[:6] or tail(errors))
                    return
        summary = f"{lines} probe line(s) on {len(ctx.pages)} page(s) match the VM"
        ctx.add(name, item, "PASS", summary)

    # The probe's native side (ctcompile/docs/native-page-programs.md): the
    # script and the probe lowered as one program, wrapped by
    # tools/check/workloads/page-program-main.cpp, which loads a page and runs
    # the program's top level on it, and built as build-native builds a DOM
    # script. Returns the binaries, or None after recording why there are none.
    # The dry-run workload sets this stage's "dry_run_allow_refused" option:
    # refused functions are then deleted, as assume-admitted deletes them.
    def page_program(item, probe):
        translate = ctx.tool("ctcompile/tools/ctjs-translate/ctjs-translate", "ctjs-translate")
        opt = ctx.tool("ctcompile/tools/ctjs-opt/ctjs-opt", "ctjs-opt")
        allow = bool(ctx.options(name).get("dry_run_allow_refused"))
        base = ctx.build / "check-workload-page-programs" / ctx.scripts[item]["path"].stem
        base.parent.mkdir(parents=True, exist_ok=True)
        source, module, cpp = (Path(f"{base}{suffix}") for suffix in (".js", ".emitc.mlir", ".cpp"))
        # One classic-script program: the library's statements, then the probe's.
        # The join is refused when it would change what the two do as two
        # scripts (hoisting or strictness); a changed join must be two
        # compilations, which build-native does not do yet.
        library_text, probe_text = ctx.scripts[item]["path"].read_text(), probe.read_text()
        problem = join_problem(library_text, probe_text)
        if problem:
            summary = f"the library and the probe cannot join into one script: {problem}"
            ctx.add(name, item, "FAIL", summary)
            return None
        source.write_text(library_text + "\n;\n" + probe_text)
        command = ["cmake", f"-DTRANSLATE={translate}", f"-DOPT={opt}", f"-DSOURCE={source}"]
        command += [f"-DOUTPUT={module}", *(["-DALLOW_REFUSED=TRUE"] if allow else [])]
        pipeline = ctx.root / "ctcompile/test/CTNative/Checks/pipeline.cmake"
        code, output = ctx.run([*command, "-P", pipeline], name, item)
        if code or not module.exists():
            summary = "the page program (script + probe) is not native"
            ctx.add(name, item, "FAIL", summary, tail(output))
            return None
        if allow:
            module.write_text(strip_refused(module.read_text(errors="replace"))[0])
        code, output = ctx.run([translate, "--mlir-to-cpp", module, "-o", cpp], name, item)
        if code or not cpp.exists():
            ctx.add(name, item, "FAIL", "--mlir-to-cpp failed on the page program", tail(output))
            return None
        if allow and not re.search(r"^(?:int|int32_t) main\(", cpp.read_text(), re.M):
            cpp.write_text(
                cpp.read_text()
                + "\n// Dry run: the top level was refused.\nint main() { return 0; }\n"
            )
        clang, includes, libraries = dom_toolchain(ctx)
        cpp_harness = harness("Target.Cpp.harness")
        try:
            compilers = [cpp_harness.find_compilers()[0], clang]
        except RuntimeError as error:
            raise SetupError(str(error))
        main = ctx.root / "tools/check/workloads/page-program-main.cpp"
        binaries = []
        for compiler in compilers:
            binary = base.with_suffix("." + Path(compiler).name)
            command = [compiler, *cpp_harness.FLAGS, *includes, f'-DCTCOMPILE_PAGE_PROGRAM="{cpp}"']
            code, output = ctx.run([*command, main, "-o", binary, *libraries], name, item)
            if code:
                summary = f"{Path(compiler).name} rejects the page program"
                ctx.add(name, item, "FAIL", summary, tail(output))
                return None
            binaries.append(binary)
        return binaries

    # A script whose globals say nothing (css-dump.js only defines functions) may
    # opt out, as {"skip": <why>, "covered_by": <CTest>}, only by naming the
    # workload CTest whose native variant runs it; the opt-out holds only while
    # that variant passes.
    def covered(item, opt_out):
        test, why = opt_out.get("covered_by"), opt_out.get("skip")
        if not why or test not in ctx.ctests:
            summary = f"the script-run opt-out names {test!r}, not one of this workload's CTests"
            ctx.add(name, item, "FAIL", summary)
            return
        result = run_native_test(ctx, test, name, item)
        if result is None or result[0] != 0:
            state = "has no native runner" if result is None else f"exited {result[0]}"
            ctx.add(name, item, "FAIL", f"skipped ({why}), but {test}.native {state}")
            return
        ctx.add(name, item, "PASS", f"skipped: {why}; {test}.native passes", metrics={"skip": why})

    # Compare stdout only, in order, as Checks/compilation-unit.py does: the reference's
    # stderr carries its counts, which the native program does not print. A NaN's
    # sign is unobservable in JavaScript, so "=-nan" reads as "=nan" there too.
    def globals_of(text):
        return text.replace("=-nan", "=nan").splitlines()

    def body(item):
        source = ctx.scripts[item]["path"]
        opt_out = ctx.scripts[item].get("script_run") or {}
        if opt_out.get("probe"):
            probed(item, ctx.root / opt_out["probe"])
            return
        if opt_out:
            covered(item, opt_out)
            return
        code, expected, notes = ctx.run_split([reference, source], name, item)
        if code:
            details = tail(expected + notes)
            ctx.add(name, item, "FAIL", "the VM reference failed on this script", details)
            return
        want = globals_of(expected)
        if not want:
            # Nothing to compare: a native program that printed nothing would match.
            summary = "the VM printed no globals, so the comparison would prove nothing"
            ctx.add(name, item, "FAIL", summary, tail(notes))
            return
        for binary in ctx.artifacts[item]["binaries"]:
            code, actual, errors = ctx.run_split([binary], name, item)
            got = globals_of(actual)
            if code or got != want:
                differ = [
                    i for i in range(max(len(got), len(want))) if got[i : i + 1] != want[i : i + 1]
                ]
                diff = []
                for i in differ[:6]:
                    diff += [f"line {i + 1}: VM {want[i : i + 1]} native {got[i : i + 1]}"]
                summary = f"{binary.name} exited {code}" if code else f"{binary.name} differs"
                ctx.add(name, item, "FAIL", summary + " from the VM", diff or tail(errors))
                return
        ctx.add(name, item, "PASS", f"{len(want)} line(s) of globals match the VM")

    ctx.each(name, list(ctx.scripts), body)


NO_RUNNER = [
    "No native variant of this test exists yet. The workload's tests keep their",
    "JavaScript in .js and .html files, as named functions, and reach it only",
    "through ctbrowser/test/support/engine.hpp (load, call). What is missing:",
    "  1. a native engine: make_engine() returning one for",
    "     CTBROWSER_TEST_ENGINE=native, calling what ctcompile compiled from them",
    "  2. a native host (page load, event loop, timers, events, canvas, WebGL,",
    "     console) without ctbrowser-shell, which links the VM",
    "  3. <name>.native registered beside <name>: the same test source,",
    "     records, baselines and goldens, built without the VM (a CSS test",
    "     loads CSS ctcompile compiled instead of parsing it), with",
    "     CTBROWSER_TEST_ENGINE=native in its ENVIRONMENT and run through",
    "     ctbrowser/cmake/run-without-vm.cmake, which fails on any VM symbol;",
    "     never weaken an assertion. A page test (example-*, render-*) needs",
    "     its page built native instead.",
    "A separate name, not the variable alone: a test that never calls",
    "make_engine() would pass on the VM, and --no-tests=error fails a test with",
    "no native variant. Then set, in the workload's file under",
    "tools/check/workloads/, options.native-run.runner to:",
    '["ctest", "--test-dir", "{build}", "--output-on-failure", "--no-tests=error",',
    ' "-R", "^{test}[.]native$"]   (placeholders: {test} {build} {root} {scratch})',
]


def run_native_test(ctx, test, name, item):
    """Run `test`'s native variant with native-run's runner, once per check:
    (exit code, output), or None when no runner is configured."""
    runner = ctx.options("native-run").get("runner")
    if not runner:
        return None
    key = f"native-run:{test}"
    if key not in ctx.artifacts:
        values = {"test": test, "build": ctx.build, "root": ctx.root, "scratch": ctx.scratch}
        ctx.artifacts[key] = ctx.run([part.format(**values) for part in runner], name, item)
    return ctx.artifacts[key]


@stage(
    "native-run",
    "each CTest runs on native C++ from ctcompile, with no VM, and passes its own checks",
    "Build the missing native runner capability named in DETAILS, then configure it.",
    needs="vm-reference",
)
def native_run(ctx, name):
    runner = ctx.options(name).get("runner")

    def body(item):
        if not runner:
            told = any(r["stage"] == name and r["status"] == "MISSING" for r in ctx.results)
            details = ["(the same gap as the first native-run item above)"] if told else NO_RUNNER
            ctx.add(name, item, "MISSING", "no native variant of this test exists yet", details)
            return
        code, output = run_native_test(ctx, item.split(":", 1)[1], name, item)
        status = "PASS" if code == 0 else "FAIL"
        ctx.add(name, item, status, f"native run exited {code}", [] if code == 0 else tail(output))

    ctx.each(name, [f"ctest:{test}" for test in ctx.ctests], body)


def merge(base, override):
    """Recursively merge dict `override` over `base`; non-dict values replace."""
    merged = dict(base)
    for key, value in override.items():
        nested = isinstance(value, dict) and isinstance(merged.get(key), dict)
        merged[key] = merge(merged[key], value) if nested else value
    return merged


def load(directory):
    """Plugins first, then defaults.json merged under every workload file."""
    for plugin in sorted(directory.glob("*.py")):
        spec = importlib.util.spec_from_file_location(f"workload_plugin_{plugin.stem}", plugin)
        module = importlib.util.module_from_spec(spec)
        module.__dict__.update(stage=stage, SetupError=SetupError, StepTimeout=StepTimeout)
        module.__dict__.update(strip_refused=strip_refused)
        spec.loader.exec_module(module)
    defaults_file = directory / "defaults.json"
    defaults = json.loads(defaults_file.read_text()) if defaults_file.exists() else {}
    workloads = {}
    for path in sorted(directory.glob("*.json")):
        if path == defaults_file:
            continue
        workload = merge(defaults, json.loads(path.read_text()))
        workload.setdefault("name", path.stem)
        if workload.get("schema") != SCHEMA:
            raise SetupError(
                f"{path}: schema {workload.get('schema')!r}, this script reads {SCHEMA}"
            )
        workloads[workload["name"]] = workload
    return workloads


def check(workload, build, deadline, root=ROOT):
    """Run every stage of `workload` in order and return its summary."""
    ctx = Context(workload, build, deadline, root)
    with tempfile.TemporaryDirectory(prefix=f"check-workload-{workload['name']}-") as scratch:
        ctx.scratch = Path(scratch)
        exhausted = None
        for name in workload["stages"]:
            if exhausted:
                ctx.add(name, "workload", "TIMEOUT", "not run: the run's --timeout budget is spent")
            elif name not in STAGES:
                ctx.add(name, "workload", "ERROR", f"no stage {name!r}; known: {', '.join(STAGES)}")
            else:
                try:
                    STAGES[name]["run"](ctx, name)
                except RunExhausted:
                    exhausted = name
                    ctx.add(name, "workload", "TIMEOUT", "the run's --timeout budget ran out here")
                except SetupError as error:
                    ctx.add(name, "workload", "ERROR", str(error))
    return summarize(ctx)


def summarize(ctx):
    """Reduce a finished Context to per-stage statuses and the workload verdict."""
    stages = []
    for name in ctx.workload["stages"]:
        mine = [r for r in ctx.results if r["stage"] == name]
        counts = collections.Counter(r["status"] for r in mine)
        status = next((s for s in SEVERITY if counts[s]), "PASS")
        first = next((r for r in mine if r["status"] == status), None)
        tally = ", ".join(f"{counts[s]} {s}" for s in SEVERITY if counts[s]) or "nothing to check"
        summary = tally + (
            f"; first: {first['item']}: {first['summary']}" if status != "PASS" else ""
        )
        skips = [f"{r['item']}: {r['metrics']['skip']}" for r in mine if r["metrics"].get("skip")]
        summary += "".join(f"; skipped {skip}" for skip in skips)
        info = STAGES.get(name, {})
        entry = {"name": name, "status": status, "summary": summary, "counts": dict(counts)}
        stages.append(
            {**entry, "describes": info.get("describes", ""), "next": info.get("next", "")}
        )
    statuses = {s["status"] for s in stages}
    if "TIMEOUT" in statuses:
        verdict = "TIMED OUT"
    elif "ERROR" in statuses:
        verdict = "CANNOT CHECK"
    elif statuses <= {"PASS"}:
        verdict = "DONE"
    else:
        verdict = "NOT DONE"
    gap = next((s for s in stages if s["status"] != "PASS"), None)
    return {
        "name": ctx.workload["name"],
        "title": ctx.workload.get("title", ctx.workload["name"]),
        "done_means": ctx.workload.get("done_means", ""),
        "verdict": verdict,
        "first_gap": gap and {"stage": gap["name"], "status": gap["status"], "next": gap["next"]},
        "inventory": {
            "scripts": len(ctx.scripts),
            "pages": len(ctx.pages),
            "ctests": len(ctx.ctests),
        },
        "stages": stages,
        "results": ctx.results,
    }


def render_text(report, details):
    out = []
    for workload in report["workloads"]:
        inventory = workload["inventory"]
        out += [
            f"WORKLOAD CHECK - {workload['name']}: {workload['title']}",
            f"VERDICT: {workload['verdict']}",
            "",
            "WHAT DONE MEANS",
            workload["done_means"],
            "",
            f"FOUND: {inventory['scripts']} script(s), {inventory['pages']} page(s), "
            f"{inventory['ctests']} CTest(s)",
            "",
            "STAGES, in order (each item needs the earlier stages to pass)",
        ]
        for entry in workload["stages"]:
            out.append(f"- {entry['name']:<13} {entry['status']:<8} {entry['summary']}")
        out += ["", "WHAT EACH STAGE PROVES WHEN IT PASSES"]
        out += [f"- {entry['name']}: {entry['describes']}" for entry in workload["stages"]]
        bad = [r for r in workload["results"] if r["status"] not in ("PASS", "BLOCKED")]
        if bad:
            out += ["", f"DETAILS (up to {details} per stage; --details N for more)"]
            shown = collections.Counter()
            for result in bad:
                shown[result["stage"]] += 1
                if shown[result["stage"]] > details:
                    continue
                head = f"[{result['stage']}] {result['status']} {result['item']}"
                out.append(f"{head}: {result['summary']}")
                out += [f"    {line}" for line in result["details"][:24]]
            for name, count in shown.items():
                if count > details:
                    out.append(f"[{name}] ... {count - details} more not shown")
        gap = workload["first_gap"]
        out += ["", "NEXT STEP"]
        if gap is None:
            out.append("Nothing: every stage passed. Record DONE in the HANDOFF.md summary.")
        elif gap["status"] == "ERROR":
            out.append(f"Fix the environment first ({gap['stage']} could not run); see DETAILS.")
        elif gap["status"] == "TIMEOUT":
            out.append(
                f"{gap['stage']} exceeded a time limit. If the command is just slow, raise "
                "--timeout or the stage's timeout option; if it hung, investigate it."
            )
        else:
            out.append(f"The first gap is {gap['stage']} ({gap['status']}). {gap['next']}")
        out.append("")
    out.append(
        f"RUN: {report['seconds']:.0f} s of a {report['budget']:.0f} s budget; exit "
        f"{report['exit']} ({report['meaning']})"
    )
    if report["exit"] >= 2:
        out.insert(0, f"ERROR: {report['meaning']}; see the stages marked ERROR or TIMEOUT.\n")
    return "\n".join(out)


def finish(workloads, budget, started):
    """Build the top-level report; the exit status is the worst verdict's."""
    exit_code = max((VERDICTS[w["verdict"]] for w in workloads), default=0)
    meaning = {0: "every workload DONE", 1: "some workload NOT DONE"}
    meaning.update({2: "CANNOT CHECK: setup error", 3: "TIMED OUT: a time limit was exceeded"})
    report = {"schema": SCHEMA, "workloads": workloads, "budget": budget, "exit": exit_code}
    report.update(seconds=time.monotonic() - started, meaning=meaning[exit_code])
    return report


def self_test():
    """Run the checks in test_check_workload.py; return the exit status."""
    import selftest

    return selftest.run(__file__)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("workloads", nargs="*", help="workload names, or all")
    parser.add_argument("--build", type=Path, default=ROOT / "build", help="the build tree")
    parser.add_argument("--definitions", type=Path, default=WORKLOADS, help="workload directory")
    parser.add_argument("--timeout", type=float, help="the run's budget in seconds")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    parser.add_argument("--details", type=int, default=5, help="failing items shown per stage")
    parser.add_argument("--list", action="store_true", help="list workloads and stages")
    parser.add_argument("--self-test", action="store_true", help="check this script, no build")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    try:
        available = load(args.definitions)
    except (SetupError, OSError, ValueError) as error:
        print(f"check-workload: ERROR: cannot load workloads: {error}", file=sys.stderr)
        return 2
    if args.list or not args.workloads:
        for name, workload in available.items():
            hidden = " (hidden: not part of all)" if workload.get("hidden") else ""
            print(f"workload {name}: {workload.get('title', name)}{hidden}")
        for name, info in STAGES.items():
            print(f"stage {name}: {info['describes']}")
        return 0 if args.list else 2
    names = list(args.workloads)
    if names == ["all"]:
        names = [name for name, workload in available.items() if not workload.get("hidden")]
    unknown = [name for name in names if name not in available]
    if unknown:
        print(f"check-workload: ERROR: unknown workload(s) {', '.join(unknown)}", file=sys.stderr)
        return 2
    budget = args.timeout or max(available[name]["timeouts"]["total"] for name in names)
    started = time.monotonic()
    results = [check(available[name], args.build, started + budget) for name in names]
    report = finish(results, budget, started)
    if args.format == "json":
        print(json.dumps(report, indent=2, default=str))
    else:
        print(render_text(report, args.details))
    if report["exit"] >= 2:
        print(f"check-workload: ERROR: {report['meaning']}", file=sys.stderr)
    return report["exit"]


if __name__ == "__main__":
    sys.exit(main())
