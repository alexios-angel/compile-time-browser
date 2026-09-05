#!/usr/bin/env python3
"""Report binding-time boundaries in the actual Bootstrap Data initializer.

Reuse the publication probe's exact vendor extraction and browser harness.
Reference observations, binding-time facts and PE eligibility are separate evidence;
this check does not compile or execute a native Bootstrap program.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys


_SPEC = importlib.util.spec_from_file_location(
    "bootstrap_data_probe", Path(__file__).with_name("bootstrap-data-probe.py")
)
_SOURCE = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_SOURCE)
ProbeError = _SOURCE.ProbeError

FUNCTION = re.compile(r"^\s*ctjs\.func\s+(?:private\s+)?@([^\s(]+)")
BINDING = re.compile(r'ctnative\.binding_time = "(static|dynamic)"')
REASON = re.compile(r'ctnative\.binding_time_reason = "((?:[^"\\]|\\.)*)"')
PE_REASON = re.compile(r'ctnative\.partial_eval_reason = "((?:[^"\\]|\\.)*)"')
ROLES = ("script", "console recorder", "UMD wrapper", "Data factory", "Data.set", "Data.get", "Data.remove")


def run(command: list[str], text: str | None = None) -> subprocess.CompletedProcess:
    result = subprocess.run(command, input=text, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise ProbeError(f"command failed ({result.returncode}): {command[0]}\n{result.stdout}{result.stderr}")
    return result


def functions(ir: str) -> dict[str, dict]:
    result = {}
    current = None
    for line in ir.splitlines():
        match = FUNCTION.match(line)
        if match:
            current = {"header": line, "lines": []}
            result[match.group(1)] = current
        elif current is not None:
            current["lines"].append(line)
    if len(result) != len(ROLES):
        raise ProbeError(f"source functions lost: expected 7 imported, got {len(result)}")
    return result


def require_dynamic(line: str, boundary: str) -> dict:
    binding = BINDING.search(line)
    reason = REASON.search(line)
    if not binding or binding.group(1) != "dynamic":
        raise ProbeError(f"{boundary} must remain dynamic")
    if not reason or not reason.group(1):
        raise ProbeError(f"{boundary} has no binding-time reason")
    return {"binding_time": binding.group(1), "reason": reason.group(1)}


def inspect(ir: str) -> dict:
    found = functions(ir)
    reports = []
    console = []
    publications = []
    unknown_calls = []
    static_literals = 0
    for name, function in found.items():
        index = int(name.rsplit("$", 1)[1])
        if index >= len(ROLES):
            raise ProbeError(f"unexpected source function identity: {name}")
        if "ctnative.argument_binding_times = " not in function["header"] or \
                "ctnative.binding_time_summary = " not in function["header"]:
            raise ProbeError(f"function {name} has no binding-time argument/summary facts")
        counts = {"static": 0, "dynamic": 0}
        for line in function["lines"]:
            binding = BINDING.search(line)
            if re.search(r"(?:^\s*|=\s*)ctjs\.(?!func\b)", line):
                if not binding or not REASON.search(line) or \
                        "ctnative.result_binding_times = " not in line:
                    raise ProbeError(f"function {name} has incomplete operation facts: {line.strip()}")
            if binding:
                counts[binding.group(1)] += 1
            if 'ctjs.load_global "console"' in line:
                console.append(require_dynamic(line, "console host read"))
            if "ctjs.store_global " in line:
                publications.append(require_dynamic(line, "global publication"))
            if "ctjs.call " in line:
                unknown_calls.append(require_dynamic(line, "unproved call"))
            if "ctjs.constant " in line and binding and binding.group(1) == "static":
                static_literals += 1
        if sum(counts.values()) == 0:
            raise ProbeError(f"function {name} has no annotated operations")
        reports.append({"function": name, "role": ROLES[index], "operations": counts})
    if len(console) != 1 or not publications or not unknown_calls or not static_literals:
        raise ProbeError("binding-time probe lacks its host boundary or known literal witnesses")
    return {
        "functions": reports,
        "console_host_read": console[0],
        "dynamic_global_publications": len(publications),
        "dynamic_unproved_calls": len(unknown_calls),
        "static_literal_operations": static_literals,
    }


def forge_console(ir: str) -> str:
    lines = ir.splitlines(keepends=True)
    for i, line in enumerate(lines):
        if 'ctjs.load_global "console"' in line:
            lines[i] = BINDING.sub('ctnative.binding_time = "static"', line)
            lines[i] = REASON.sub('ctnative.binding_time_reason = "forged source proof"', lines[i])
            lines[i] = re.sub(
                r'ctnative\.result_binding_times = \[[^]]*\]',
                'ctnative.result_binding_times = ["static"]', lines[i]
            )
            return "".join(lines)
    raise ProbeError("console host read missing before metadata control")


def check(args: argparse.Namespace) -> None:
    source = args.bootstrap.read_bytes().decode("utf-8")
    fragment, provenance = _SOURCE.extract(source)
    generated = _SOURCE.generate(fragment, "browser")
    args.work.mkdir(parents=True, exist_ok=True)
    stem = args.work / "bootstrap-data-binding-time"
    program = stem.with_suffix(".js")
    program.write_bytes(generated.encode("utf-8"))
    provenance.update({
        "vendor": str(args.bootstrap), "program": str(program),
        "program_sha256": _SOURCE.sha256(generated),
        "scope": "source-derived Data initialization analysis; no native host execution",
    })
    stem.with_suffix(".provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    if args.generate_only:
        print(f"bootstrap-data binding time: generated {program}")
        return

    reference = run([args.reference, str(program)])
    stem.with_suffix(".reference.txt").write_text(reference.stdout)
    stem.with_suffix(".reference.log").write_text(reference.stderr)
    observed = {}
    for line in reference.stdout.splitlines():
        name, separator, value = line.partition("=")
        if not separator or name in observed:
            raise ProbeError(f"invalid reference observation: {line!r}")
        observed[name] = value
    if observed != _SOURCE.EXPECTED:
        raise ProbeError(f"source reference observations changed: {observed}")

    imported = run([args.translate, "--ctbrowser-js-to-ctjs", str(program)])
    stem.with_suffix(".import.log").write_text(imported.stderr)
    if "is not compiled" in imported.stderr:
        raise ProbeError("source function skipped by importer")
    functions(imported.stdout)
    prepared = run([args.opt, "--ctjs-resolve-globals", "--ctjs-lift-to-scf"], imported.stdout)
    stem.with_suffix(".prepared.mlir").write_text(prepared.stdout)
    analysed = run([args.opt, "--ctnative-binding-time-analysis"], prepared.stdout)
    stem.with_suffix(".mlir").write_text(analysed.stdout)
    inspected = forge_console(analysed.stdout) if args.negative_control else analysed.stdout
    facts = inspect(inspected)

    # Re-running the analysis must derive facts afresh, including result facts;
    # a forged constant tag on the real console read is never a proof input.
    repeated = run([args.opt, "--ctnative-binding-time-analysis"], analysed.stdout)
    repaired = run([args.opt, "--ctnative-binding-time-analysis"], forge_console(analysed.stdout))
    inspect(repaired.stdout)
    if repeated.stdout != repaired.stdout:
        raise ProbeError("forged binding-time metadata changed rederived analysis")
    if repeated.stdout != analysed.stdout:
        raise ProbeError("repeated binding-time analysis changed its facts")

    partial = run([args.opt, "--ctnative-partial-evaluate=report=true"], prepared.stdout)
    stem.with_suffix(".partial.mlir").write_text(partial.stdout)
    stem.with_suffix(".partial.log").write_text(partial.stderr)
    declined = []
    for name, function in functions(partial.stdout).items():
        reason = PE_REASON.search(function["header"])
        if reason:
            declined.append({"function": name, "reason": reason.group(1)})
    summary = re.search(r"partial evaluation: (\d+) function\(s\), (\d+) residual heap node\(s\), (\d+) declined", partial.stderr)
    if not summary or tuple(map(int, summary.groups())) != (0, 0, len(declined)) or \
            "ctnative.partial_evaluated = " in partial.stdout or \
            'ctjs.load_global "console"' not in partial.stdout:
        raise ProbeError("Data effect boundary was specialized or lacks an intact host read")
    # The imported UMD functions remain public and indirect, outside the closed
    # private factory candidate set. Zero attempts must not be reported as seven
    # failed evaluations: record non-candidates separately from actual refusals.
    outside_candidates = [name for name, function in functions(partial.stdout).items()
                          if not PE_REASON.search(function["header"])]
    factory = next(value for name, value in functions(partial.stdout).items() if name.endswith("$3"))
    if not any("ctjs.construct " in line for line in factory["lines"]) or \
            sum("ctjs.create_closure " in line for line in factory["lines"]) != 3:
        raise ProbeError("Data initializer no longer retains its Map and three latent methods")

    report = {**provenance, "reference_observations": observed, "binding_time": facts,
              "partial_evaluation_refusals": declined,
              "partial_evaluation_non_candidates": outside_candidates,
              "partial_evaluation_specialized": 0, "metadata_rederived": True}
    stem.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"bootstrap-data binding time: 7 source functions, {len(observed)} reference observations; "
          f"{facts['dynamic_global_publications']} dynamic publications, "
          f"{len(declined)} PE refusal(s), {len(outside_candidates)} outside candidate set; "
          "no native host execution")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootstrap", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--reference")
    parser.add_argument("--translate")
    parser.add_argument("--opt")
    parser.add_argument("--generate-only", action="store_true")
    parser.add_argument("--negative-control", action="store_true")
    args = parser.parse_args()
    if not args.generate_only and not all((args.reference, args.translate, args.opt)):
        parser.error("checking requires --reference, --translate and --opt")
    if args.generate_only and args.negative_control:
        parser.error("--generate-only cannot run a negative control")
    try:
        check(args)
    except (ProbeError, OSError, subprocess.TimeoutExpired) as error:
        if args.negative_control and str(error) == "console host read must remain dynamic":
            print(f"bootstrap-data binding time: negative control rejected: {error}")
            return
        sys.exit(f"bootstrap-data binding time: {error}")
    if args.negative_control:
        sys.exit("bootstrap-data binding time: negative control was not rejected")


if __name__ == "__main__":
    main()
