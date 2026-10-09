#!/usr/bin/env python3
"""Trial-merge the forked C++ emitter with upstream MLIR and explain each conflict.

ctcompile/lib/Target/Cpp/TranslateToCpp.cpp is a fork of upstream's
mlir/lib/Target/Cpp/TranslateToCpp.cpp; its header records the fork base (tag,
path, sha256). This fetches that base and a newer upstream file, merges the
candidate (the working copy) and a baseline (HEAD) into upstream with
`git merge-file --diff3`, and reports every conflict: where it is, what the
fork and upstream each changed, and whether the candidate added it. The text
output is written for an LLM to act on; --format json carries the same data.

  tools/check/check-emitter-merge.py                  # working copy vs HEAD
  tools/check/check-emitter-merge.py --upstream main  # against upstream's tip
  tools/check/check-emitter-merge.py --conflict 3 --max-lines 0
  tools/check/check-emitter-merge.py --self-test

Upstream files come from a shallow, blobless, sparse clone (about 33 MB)
cached in ~/.cache/ctcompile-emitter-merge. Exit status: 0 when the candidate
adds no conflict, 1 when it adds one or exceeds --max-conflicts, 2 when setup
fails.
"""

import argparse
import collections
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FORK = "ctcompile/lib/Target/Cpp/TranslateToCpp.cpp"
PIN = "ctcompile/CMakeLists.txt"
UPSTREAM = "llvm/llvm-project"
CACHE = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
CACHE = CACHE / "ctcompile-emitter-merge" / "llvm-project"
ADVICE = (
    "move the added logic into a helper in a neighbouring file (Callables/, Const/, "
    "Exceptions/ beside the fork) and call it from a one-line hook here; leave "
    "upstream's own lines, their order and their formatting untouched"
)
FUNCTION = re.compile(
    r"^(?!(?:if|for|while|switch|return|else|case|do|namespace)\b)"
    r"[^;=]*?\b([A-Za-z_][\w:~]*)\s*\("
)


class SetupError(Exception):
    pass


def run(command, cwd=None, binary=False):
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=not binary)
    if result.returncode != 0:
        error = result.stderr.decode(errors="replace") if binary else result.stderr
        raise SetupError(f"`{' '.join(command)}` failed: {error.strip()}")
    return result.stdout


def header_facts(text):
    facts = {
        "base": re.search(r"upstream:\s+llvm/llvm-project, tag (\S+)", text),
        "path": re.search(r"^//\s+file:\s+(\S+)", text, re.M),
        "sha256": re.search(r"sha256:\s+([0-9a-f]{64})", text),
    }
    missing = [name for name, match in facts.items() if not match]
    if missing:
        raise SetupError(f"the fork's header no longer records: {', '.join(missing)}")
    return {name: match.group(1) for name, match in facts.items()}


def pinned_upstream():
    text = (ROOT / PIN).read_text()
    match = re.search(r'CTCOMPILE_TESTED_LLVM_VERSION\s+"([\d.]+)"', text)
    if not match:
        raise SetupError(f"{PIN} no longer sets CTCOMPILE_TESTED_LLVM_VERSION; pass --upstream")
    return f"llvmorg-{match.group(1)}"


def upstream_file(cache, ref, path, base_tag):
    """The bytes of `path` at `ref`, from a sparse, blobless, shallow clone."""
    if not (cache / ".git").exists():
        cache.parent.mkdir(parents=True, exist_ok=True)
        flags = ["--depth", "1", "--filter=blob:none", "--sparse", "--branch", base_tag]
        if shutil.which("gh"):
            run(["gh", "repo", "clone", UPSTREAM, str(cache), "--", *flags, "-q"])
        else:
            url = f"https://github.com/{UPSTREAM}.git"
            run(["git", "clone", "-q", *flags, url, str(cache)])
        run(["git", "-C", str(cache), "sparse-checkout", "set", str(Path(path).parent)])
    tag = ref.startswith("llvmorg-")
    local = ref if tag else f"refs/remotes/origin/{ref}"
    verify = ["git", "-C", str(cache), "rev-parse", "-q", "--verify", f"{local}^{{commit}}"]
    if not tag or subprocess.run(verify, capture_output=True).returncode != 0:
        spec = ["tag", ref] if tag else [f"+refs/heads/{ref}:{local}"]
        fetch = ["git", "-C", str(cache), "fetch", "-q", "--depth", "1", "--filter=blob:none"]
        run([*fetch, "origin", *spec])
    return run(["git", "-C", str(cache), "show", f"{local}:{path}"], binary=True)


def fork_text(spec):
    """`worktree` is the file on disk; anything else is a git revision."""
    if spec == "worktree":
        return (ROOT / FORK).read_text()
    return run(["git", "show", f"{spec}:{FORK}"], cwd=ROOT)


def merge(fork, base, upstream):
    with tempfile.TemporaryDirectory() as scratch:
        paths = []
        for label, text in (("fork", fork), ("base", base), ("upstream", upstream)):
            path = Path(scratch) / label
            path.write_text(text)
            paths.append(str(path))
        labels = ["-L", "fork", "-L", "base", "-L", "upstream"]
        command = ["git", "merge-file", "-p", "--diff3", *labels, *paths]
        result = subprocess.run(command, capture_output=True, text=True)
    if not 0 <= result.returncode <= 127:
        raise SetupError(f"git merge-file failed: {result.stderr.strip()}")
    return result.stdout


def parse(merged):
    """Each conflict's three sides, plus the merged lines just before it."""
    found, current, side, context = [], None, None, []
    for line in merged.split("\n"):
        if current is None and line.startswith("<<<<<<< "):
            current, side = {"fork": [], "base": [], "upstream": [], "before": context[-3:]}, "fork"
        elif current is not None and line.startswith("||||||| "):
            side = "base"
        elif current is not None and line == "=======":
            side = "upstream"
        elif current is not None and line.startswith(">>>>>>> "):
            found.append(current)
            current = None
        elif current is not None:
            current[side].append(line)
        else:
            context.append(line)
    return found


def find(lines, block, start):
    for begin in (start, 0):
        for at in range(begin, len(lines) - len(block) + 1):
            if lines[at : at + len(block)] == block:
                return at
    return None


def function_at(lines, line):
    for number in range(min(line, len(lines)) - 1, -1, -1):
        text = lines[number]
        if text[:1].isalpha() or text[:1] == "_":
            match = FUNCTION.match(text)
            if match:
                return match.group(1), number + 1
    return None, None


def squash(lines):
    return " ".join(" ".join(lines).split())


def kind(conflict):
    fork, base, upstream = (squash(conflict[s]) for s in ("fork", "base", "upstream"))
    if fork == upstream:
        return "both sides made the same change, differing only in whitespace"
    if fork == base:
        return "the fork side differs from base only in whitespace: take upstream's"
    if upstream == base:
        return "upstream differs from base only in whitespace: keep the fork's"
    if not conflict["base"]:
        return "both sides inserted different lines at the same point"
    if not conflict["fork"]:
        return "the fork deleted lines that upstream changed"
    if not conflict["upstream"]:
        return "upstream deleted lines that the fork changed"
    return "the fork and upstream edited the same lines differently"


def key(conflict):
    text = squash(conflict["base"]) + "\0" + squash(conflict["upstream"])
    return hashlib.sha1(text.encode()).hexdigest()[:12]


def churn(before, after):
    hunks = added = removed = 0
    matcher = difflib.SequenceMatcher(None, before, after, autojunk=False)
    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        if tag != "equal":
            hunks, removed, added = hunks + 1, removed + i2 - i1, added + j2 - j1
    return {"hunks": hunks, "added": added, "removed": removed}


def diff(before, after, label, limit):
    lines = list(difflib.unified_diff(before, after, "base", label, n=1, lineterm=""))[2:]
    if limit and len(lines) > limit:
        hidden = len(lines) - limit
        lines = lines[:limit] + [f"... {hidden} more diff lines (rerun with --max-lines 0)"]
    return lines


def analyse(candidate, baseline, base, upstream, limit):
    lines = candidate.split("\n")
    found, cursor = parse(merge(candidate, base, upstream)), 0
    for index, conflict in enumerate(found, 1):
        probe = conflict["fork"] or conflict["before"]
        at = find(lines, probe, cursor) if probe else None
        if at is not None:
            cursor = at + (0 if conflict["fork"] else len(probe))
        line = cursor + 1 if at is not None else None
        name, defined = function_at(lines, line) if line else (None, None)
        conflict.update(index=index, key=key(conflict), fork_line=line)
        conflict.update(function=name, function_line=defined, kind=kind(conflict))
    old = parse(merge(baseline, base, upstream)) if baseline is not None else []
    remaining = collections.Counter(key(conflict) for conflict in old)
    for conflict in found:
        conflict["status"] = "existing" if remaining[conflict["key"]] > 0 else "new"
        remaining[conflict["key"]] -= 1
    resolved = []
    for conflict in old:
        if remaining[key(conflict)] > 0:
            resolved.append(conflict)
            remaining[key(conflict)] -= 1
    return {
        "candidate_conflicts": len(found),
        "baseline_conflicts": len(old) if baseline is not None else None,
        "new": [c["index"] for c in found if c["status"] == "new"],
        "resolved": [{"base": c["base"][:3], "upstream": c["upstream"][:3]} for c in resolved],
        "divergence": {
            "fork_vs_base": churn(base.split("\n"), lines),
            "upstream_vs_base": churn(base.split("\n"), upstream.split("\n")),
            "candidate_vs_baseline": (
                churn(baseline.split("\n"), lines) if baseline is not None else None
            ),
        },
        "conflicts": [
            {
                "index": c["index"],
                "status": c["status"],
                "fork_line": c["fork_line"],
                "function": c["function"],
                "function_line": c["function_line"],
                "kind": c["kind"],
                "lines": {side: len(c[side]) for side in ("fork", "base", "upstream")},
                "fork_change": diff(c["base"], c["fork"], "fork", limit),
                "upstream_change": diff(c["base"], c["upstream"], "upstream", limit),
            }
            for c in found
        ],
    }


def churn_text(stats):
    return f"+{stats['added']}/-{stats['removed']} lines in {stats['hunks']} hunks"


def render(report, inputs, only):
    out = [f"EMITTER MERGE CHECK - {FORK}", ""]
    out += [
        "WHAT THIS MEASURES",
        "The file is a fork of upstream MLIR's C++ emitter. This is a trial three-way merge",
        "(git merge-file --diff3) of the fork into a newer upstream file, as the next rebase",
        "onto upstream would do it. Every conflict is a hunk someone must resolve by hand at",
        "that rebase. A change to the fork should add no conflict.",
        "",
        "INPUTS",
    ]
    out += [f"- {name}: {value}" for name, value in inputs.items()]
    divergence = report["divergence"]
    out += [
        f"- fork vs base: {churn_text(divergence['fork_vs_base'])}",
        f"- upstream vs base: {churn_text(divergence['upstream_vs_base'])}",
    ]
    if divergence["candidate_vs_baseline"] is not None:
        out.append(f"- candidate vs baseline: {churn_text(divergence['candidate_vs_baseline'])}")
    new, total = report["new"], report["candidate_conflicts"]
    out += ["", "RESULT", f"- conflicts in the candidate: {total}"]
    if report["baseline_conflicts"] is not None:
        out += [
            f"- conflicts in the baseline: {report['baseline_conflicts']}",
            f"- added by the candidate: {len(new)}"
            + (f" (conflicts {', '.join(map(str, new))})" if new else ""),
            f"- resolved by the candidate: {len(report['resolved'])}",
        ]
    out.append(f"- VERDICT: {report['verdict']}")
    functions = collections.defaultdict(list)
    for conflict in report["conflicts"]:
        functions[conflict["function"] or "(outside any function)"].append(conflict["index"])
    if functions:
        out += ["", "CONFLICTS BY FUNCTION"]
        for name, indices in sorted(functions.items(), key=lambda item: -len(item[1])):
            out.append(f"- {name}: {len(indices)} (conflicts {', '.join(map(str, indices))})")
    for conflict in report["conflicts"]:
        if only and conflict["index"] != only:
            continue
        where = f"fork line {conflict['fork_line']}" if conflict["fork_line"] else "not located"
        if conflict["function"]:
            where += f", in {conflict['function']} (defined at line {conflict['function_line']})"
        status = "NEW, added by the candidate" if conflict["status"] == "new" else "existing"
        sizes = conflict["lines"]
        out += [
            "",
            f"CONFLICT {conflict['index']} of {total} - {status}",
            f"- where: {where}",
            f"- kind: {conflict['kind']}",
            f"- size: fork {sizes['fork']} lines, base {sizes['base']}, upstream {sizes['upstream']}",
            "- what the fork changed here (base -> fork):",
        ]
        out += [f"    {line}" for line in conflict["fork_change"] or ["(no change)"]]
        out.append("- what upstream changed here (base -> upstream):")
        out += [f"    {line}" for line in conflict["upstream_change"] or ["(no change)"]]
        if conflict["status"] == "new":
            out.append(f"- to fix: {ADVICE}")
    if report["resolved"]:
        out += ["", "RESOLVED BY THE CANDIDATE (present in the baseline, gone now)"]
        for item in report["resolved"]:
            first = next((line.strip() for line in item["base"] if line.strip()), "(empty)")
            out.append(f"- base text starting: {first}")
    out += [
        "",
        "HOW TO ACT",
        f"- For each NEW conflict: {ADVICE}. Then rerun this check.",
        "- Existing conflicts are rebase debt the fork already carries. Leave them alone",
        "  unless your task is the rebase itself; do not trade one for another.",
        "- After any edit, also run the vendored upstream tests in",
        "  ctcompile/test/Target/Cpp/upstream/ with the affected focused checks.",
        "- Show one conflict in full: --conflict N --max-lines 0. Write the merged file,",
        "  markers included, for inspection: --keep-merged PATH.",
    ]
    return "\n".join(out)


def self_test():
    base = "int a;\nint b;\nint c;\nint d;\n"
    upstream = "int a;\nint B;\nint c;\nint d;\n"
    baseline = "int a;\nint b;\nint c;\nint d2;\n"
    candidate = "Emitter::Emitter(int x) {\nint a;\nint b2;\nint c;\nint d2;\n"
    report = analyse(candidate, baseline, base, upstream, 12)
    assert report["candidate_conflicts"] == 1, report
    assert report["baseline_conflicts"] == 0, report
    conflict = report["conflicts"][0]
    assert conflict["status"] == "new" and report["new"] == [1], conflict
    assert conflict["fork_line"] == 3 and conflict["function"] == "Emitter::Emitter", conflict
    assert analyse(baseline, candidate, base, upstream, 12)["resolved"], "resolution missed"
    print("self-test passed")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--candidate", default="worktree", help="worktree or a git revision")
    parser.add_argument("--baseline", default="HEAD", help="a git revision, or none")
    parser.add_argument("--upstream", help="upstream tag or branch (default: the pinned LLVM)")
    parser.add_argument("--cache", type=Path, default=CACHE, help="the sparse upstream clone")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    parser.add_argument("--conflict", type=int, help="describe only conflict N")
    parser.add_argument("--max-lines", type=int, default=12, help="diff lines per side; 0 = all")
    parser.add_argument("--max-conflicts", type=int, help="also fail above this many conflicts")
    parser.add_argument("--keep-merged", type=Path, help="write the candidate's merged file")
    parser.add_argument("--self-test", action="store_true", help="check the analysis and exit")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    try:
        candidate = fork_text(args.candidate)
        facts = header_facts(candidate)
        upstream_ref = args.upstream or pinned_upstream()
        base_bytes = upstream_file(args.cache, facts["base"], facts["path"], facts["base"])
        upstream_bytes = upstream_file(args.cache, upstream_ref, facts["path"], facts["base"])
        baseline = None if args.baseline == "none" else fork_text(args.baseline)
        if hashlib.sha256(base_bytes).hexdigest() != facts["sha256"]:
            raise SetupError(
                f"upstream {facts['base']} does not match the sha256 in the fork's header; "
                "the header's tag or hash is stale, so every count would be wrong"
            )
        if baseline is not None:
            short = run(["git", "rev-parse", "--short", args.baseline], cwd=ROOT).strip()
    except (SetupError, OSError) as error:
        print(f"check-emitter-merge: setup failed: {error}", file=sys.stderr)
        return 2
    base, upstream = base_bytes.decode(), upstream_bytes.decode()
    report = analyse(candidate, baseline, base, upstream, args.max_lines)
    failed = bool(report["new"]) or (
        args.max_conflicts is not None and report["candidate_conflicts"] > args.max_conflicts
    )
    if report["new"]:
        report["verdict"] = f"FAIL - the candidate adds {len(report['new'])} conflict(s)"
    elif failed:
        report["verdict"] = f"FAIL - more than --max-conflicts {args.max_conflicts}"
    else:
        report["verdict"] = "PASS - the candidate adds no conflict"
    if args.keep_merged:
        args.keep_merged.write_text(merge(candidate, base, upstream))
    inputs = {
        "candidate": "working copy" if args.candidate == "worktree" else args.candidate,
        "baseline": "none" if baseline is None else f"{args.baseline} ({short})",
        "fork base": f"{facts['base']} (sha256 matches the fork's header)",
        "upstream": upstream_ref + ("" if args.upstream else f" (pinned in {PIN})"),
    }
    if args.format == "json":
        print(json.dumps({"inputs": inputs, **report}, indent=2))
    else:
        print(render(report, inputs, args.conflict))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
