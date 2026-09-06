#!/usr/bin/env python3
"""Report checked host-root/slot evidence for the unchanged exact Data probes.

This does not compile a native Bootstrap executable. Native census/reference
observations remain the separate bootstrap-data-probe.py gate.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess


def run(command: list[str]) -> subprocess.CompletedProcess:
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError(f"command failed: {command}\n{result.stdout}{result.stderr}")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootstrap", type=Path, required=True)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--mode", choices=("commonjs", "browser", "amd"), required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)

    source = Path(__file__).with_name("bootstrap-data-probe.py")
    spec = importlib.util.spec_from_file_location("bootstrap_data_probe", source)
    probe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(probe)
    fragment, provenance = probe.extract(args.bootstrap.read_bytes().decode("utf-8"))
    program = probe.generate(fragment, args.mode)
    stem = args.work / f"bootstrap-host-slots-{args.mode}"
    js = stem.with_suffix(".js")
    raw = stem.with_suffix(".raw.mlir")
    prepared = stem.with_suffix(".mlir")
    js.write_bytes(program.encode("utf-8"))
    run([args.translate, "--ctbrowser-js-to-ctjs", str(js), "-o", str(raw)])
    run([args.opt, str(raw), "--ctjs-resolve-globals", "--ctjs-lift-to-scf", "-o", str(prepared)])
    ir = prepared.read_text()
    functions = len(re.findall(r"\bctjs\.func\s+(?:private\s+)?@", ir))
    expected_functions = 8 if args.mode == "amd" else 7
    if functions != expected_functions or "ctjs.skipped" in ir:
        raise RuntimeError(f"exact source accounting changed: {functions}, expected {expected_functions}")
    result = run([args.opt, str(prepared), "--ctnative-host-contract=fingerprint=true", "-o", "/dev/null"])
    matched = re.search(r"host-contract fingerprint: ([0-9a-f]{64})", result.stderr)
    if not matched:
        raise RuntimeError("host analysis did not print a canonical module fingerprint")

    binding, key = {"commonjs": ("module", "exports"), "browser": ("globalThis", "bootstrap"),
                    "amd": ("define", "amd")}[args.mode]
    present = {"commonjs": {"module", "exports"}, "browser": {"globalThis"}, "amd": {"define"}}[args.mode]
    roots = [{"binding": binding, "properties": [key]}]
    observations = sorted(probe.EXPECTED | ({"traceDelayed": "1"} if args.mode == "amd" else {}))
    contract = {
        "version": 1, "provider": "closed-source-v1", "module_sha256": matched[1],
        "entry": "_script_$0", "roots": roots, "observations": observations,
        # Bare contexts now supply the realm's globalThis view. It remains
        # unmodeled unless this driver replaces it with its own source object.
        "absent_bindings": sorted({"module", "exports", "define", "self"} - present),
        "undefined_bindings": ["undefined"],
        "initial_intrinsics": ["Map", "Array"], "realm_global_this": True,
    }
    manifest = stem.with_suffix(".contract.json")
    report_path = stem.with_suffix(".report.json")
    output = stem.with_suffix(".reported.mlir")
    manifest.write_text(json.dumps(contract, indent=2) + "\n")
    result = run([args.opt, str(prepared),
                  f"--ctnative-host-contract=manifest={manifest} output={report_path} report=true",
                  "-o", str(output)])
    stem.with_suffix(".log").write_text(result.stderr)
    report = json.loads(report_path.read_text())
    if len(report["slots"]) != 1:
        raise RuntimeError(f"requested exact publication slot was lost: {report}")
    slot = report["slots"][0]
    if (slot["binding"], slot["property"]) != (binding, key):
        raise RuntimeError("host report changed the driver-declared root")
    if args.mode != "amd":
        minimum_writes = 2 if args.mode == "commonjs" else 1
        if not slot["fresh_source_root"] or slot["source_writes"] < minimum_writes or slot["source_reads"] < 10:
            raise RuntimeError(f"exact source publication evidence is missing: {slot}")
    elif slot["fresh_source_root"] or "fresh object" not in slot["reason"]:
        raise RuntimeError("retained AMD callable should retain its explicit unsupported owner frontier")
    if not report["proved"] and (not report["reason"] or slot["proved_edges"]):
        raise RuntimeError("partial slot evidence was exposed as an executable contract proof")
    provenance.update({
        "mode": args.mode, "program_sha256": probe.sha256(program),
        "source_functions": functions, "declared_observations": observations,
        "native_execution_claimed": False, "host_report": report,
    })
    stem.with_suffix(".json").write_text(json.dumps(provenance, indent=2) + "\n")
    print(f"bootstrap host slots ({args.mode}): {functions} source functions, "
          f"{len(observations)} declared observations; {json.dumps(slot, sort_keys=True)}; "
          f"contract {'proved' if report['proved'] else 'refused'}: {report['reason']}")


if __name__ == "__main__":
    main()
