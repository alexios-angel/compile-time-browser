#!/usr/bin/env python3
"""Generate an exact Data prefix proof and a boxed differential wrapper.

Only the checked UMD wrapper is installed in the differential executable.
This tests the semantic transformation, not native Bootstrap admission.
"""

import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess


def run(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError(f"failed: {command}\n{result.stdout}{result.stderr}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootstrap", type=Path, required=True)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--mode", choices=("commonjs", "browser", "global_reentry", "self_reentry"), required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location("bootstrap_probe", Path(__file__).with_name("bootstrap-data-probe.py"))
    probe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(probe)
    fragment, provenance = probe.extract(args.bootstrap.read_bytes().decode("utf-8"))
    adversarial = args.mode in {"global_reentry", "self_reentry"}
    if adversarial:
        program = "function route() { if (typeof host === 'object') { trace = 1; } else { trace = 2; } } var host = {}; var trace = 0; route(); host = 0; this.route();\n"
        provenance = {"fixture": "global publication permits a later realm-property invocation"}
        if args.mode == "self_reentry":
            program = "var saved; var host = {}; var trace = 0; (function route() { if (typeof host === 'object') { trace = 1; } else { trace = 2; } saved = route; })(); host = 0; saved();\n"
            provenance = {"fixture": "implicit callee publication permits a later invocation"}
    else:
        program = probe.generate(fragment, args.mode)
    expected = {"trace": "2"} if adversarial else probe.EXPECTED
    function_count = 2 if adversarial else 7
    js = args.work / "program.js"
    raw = args.work / "raw.mlir"
    prepared = args.work / "prepared.mlir"
    specialized = args.work / "specialized.mlir"
    manifest = args.work / "contract.json"
    report_file = args.work / "prefix.json"
    js.write_bytes(program.encode("utf-8"))
    run([args.translate, "--ctbrowser-js-to-ctjs", str(js), "-o", str(raw)])
    run([args.opt, str(raw), "--ctjs-resolve-globals", "--ctjs-lift-to-scf", "-o", str(prepared)])
    before = prepared.read_text()
    if len(re.findall(r"\bctjs\.func\b", before)) != function_count or "ctjs.skipped" in before:
        raise RuntimeError("exact Data source accounting changed")
    fingerprint = run([args.opt, str(prepared), "--ctnative-host-contract=fingerprint=true", "-o", "/dev/null"])
    digest = re.search(r"host-contract fingerprint: ([0-9a-f]{64})", fingerprint.stderr)
    if not digest:
        raise RuntimeError("missing canonical source fingerprint")
    binding, key = (("host", "slot") if adversarial else
                    (("module", "exports") if args.mode == "commonjs" else ("globalThis", "bootstrap")))
    present = {"module", "exports"} if args.mode == "commonjs" else {"globalThis"}
    manifest.write_text(json.dumps({
        "version": 1, "provider": "closed-source-v1", "module_sha256": digest[1],
        "entry": "_script_$0", "roots": [{"binding": binding, "properties": [key]}],
        "observations": sorted(expected),
        "absent_bindings": [] if adversarial else sorted({"module", "exports", "define", "self"} - present),
        "undefined_bindings": ["undefined"],
        "initial_intrinsics": [] if adversarial else ["Map", "Array"],
        "realm_global_this": True,
    }, indent=2) + "\n")
    result = run([args.opt, str(prepared),
                  f"--ctnative-specialize-host-prefix=manifest={manifest} output={report_file} report=true",
                  "-o", str(specialized)])
    (args.work / "prefix.log").write_text(result.stderr)
    report = json.loads(report_file.read_text())
    expected_branches = 0 if adversarial else (2 if args.mode == "commonjs" else 5)
    expected_targets = [] if adversarial else ["fn$3"]
    if not report["valid"] or report["selected_branches"] != expected_branches or report["targets"] != expected_targets:
        raise RuntimeError(f"exact wrapper proof did not advance as expected: {report}")
    after = specialized.read_text()
    if len(re.findall(r"\bctjs\.func\b", after)) != function_count:
        raise RuntimeError("prefix specialization lost a source function")
    wrapper = re.search(r"ctjs\.func (?:private )?@fn\$2\(.*?(?=\n  ctjs\.func |\n})", after, re.S)
    if not adversarial and (not wrapper or "scf.if" in wrapper[0] or len(re.findall(r"ctjs.call_direct @fn\$3\(", wrapper[0])) != 1):
        raise RuntimeError("selected wrapper still has open UMD alternatives or lost its factory call")
    # The retained value is the fifth wrapper operand; no replacement closure
    # or substituted script receiver may stand in for it.
    if not adversarial and not re.search(r"ctjs.call_direct @fn\$3\([^,]+, [^,]+, %arg4\)", wrapper[0]):
        raise RuntimeError("factory call did not preserve its actual callee operand")

    # Native accounting remains an independent four-bucket census. Keep the
    # imported denominator even if ordinary default pruning becomes applicable.
    native_file = args.work / "native.mlir"
    native = run([args.opt, str(specialized), "--ctnative-lower-to-emitc=report=true", "-o", str(native_file)])
    native_text = native_file.read_text()
    claimed = len(re.findall(r"\bemitc\.func\b", native_text))
    refused = len(re.findall(r"\bctjs\.func\b", native_text))
    reasons = re.findall(r'ctnative.not_native = "((?:[^"\\]|\\.)*)"', native_text)
    reachability = re.search(r"ctnative\.reachability_summary = \{removed = (\d+) : i64,", native_text)
    pruned = int(reachability[1]) if reachability else -1
    if refused != len(reasons) or claimed + refused + pruned != function_count:
        raise RuntimeError("native refusal/source accounting is incomplete")
    provenance.update({"mode": args.mode, "program_sha256": probe.sha256(program),
                       "source_functions": function_count, "declared_observations": sorted(expected),
                       "prefix": report, "native_execution_claimed": False,
                       "native_census": {"claimed": claimed, "refused": refused,
                                         "pruned": pruned, "reasons": reasons}})
    (args.work / "evidence.json").write_text(json.dumps(provenance, indent=2) + "\n")
    (args.work / "native.log").write_text(native.stderr)

    boxed = args.work / "boxed.mlir"
    # SCF lifting leaves dead arithmetic markers. CSE removes those without
    # canonicalization forming arithmetic selects over boxed CTJS values.
    lowered = run([args.opt, str(specialized), "--convert-scf-to-cf", "--cse", "--ctjs-lower-to-emitc",
                   "--ctjs-drop-uncompiled", "--emitc-eliminate-block-arguments", "-o", str(boxed)])
    (args.work / "boxed.log").write_text(lowered.stderr)
    cpp = run([args.translate, "--mlir-to-cpp", "--declare-variables-at-top", str(boxed)]).stdout
    entry_name = "route_1" if adversarial else "fn_2"
    if not re.search(r"\b" + entry_name + r"\(", cpp):
        raise RuntimeError("boxed differential wrapper was refused")
    (args.work / "wrapper.cpp").write_text(re.sub(r"\b" + entry_name + r"\b", "ctcompile_host_prefix_wrapper", cpp))
    (args.work / "expected.inc").write_text("".join(
        "{" + json.dumps(name) + ", " + value + ".0},\n" for name, value in sorted(expected.items())))
    print(f"host prefix {args.mode}: {expected_branches} selected branches, {len(expected_targets)} retained factory target(s); "
          f"native {claimed}/{function_count}, {refused} refused; boxed differential wrapper generated")


if __name__ == "__main__":
    main()
