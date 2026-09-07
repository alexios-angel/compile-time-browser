#!/usr/bin/env python3
"""Keep publication evidence separate from native ownership and callable proofs.

These are measured boundary tests. Update the admission expectations only when
an explicit live consumer implements the corresponding ownership/call gate.
"""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil


spec = importlib.util.spec_from_file_location("host", Path(__file__).with_name("host-contract.py"))
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)

FUNCTION = re.compile(r"^\s*ctjs\.func\b", re.M)
NATIVE = re.compile(r"^\s*emitc\.func\b", re.M)
REFUSAL = re.compile(r'ctnative\.not_native = "((?:[^"\\]|\\.)*)"')
GLOBAL_ESCAPE = "an object literal that escapes - it reaches `ctjs.store_global`"
TABLE_ESCAPE = "returned method table is written through an alias or stored into another object"
FOLLOW = ("follow-publication=true follow-provider-reads=true follow-provider-mutations=true "
          "follow-provider-diagnostics=true follow-provider-callbacks=true")
NODE = r"""const fs = require('node:fs');
const vm = require('node:vm');
const context = vm.createContext({});
vm.runInContext(fs.readFileSync(process.argv[1], 'utf8'), context);
const trace = vm.runInContext('trace', context);
if (typeof trace !== 'number' || !Number.isFinite(trace)) throw new Error('non-numeric trace');
process.stdout.write('trace=' + String(trace) + '\n');
"""


def prepare(args, name, source):
    js = args.work / f"{name}.js"
    raw = args.work / f"{name}.raw.mlir"
    prepared = args.work / f"{name}.prepared.mlir"
    js.write_text(source + "\n")
    result = host.run([args.translate, "--ctbrowser-js-to-ctjs", str(js), "-o", str(raw)])
    text = raw.read_text()
    if "ctjs.skipped" in text or "is not compiled:" in result.stderr:
        raise RuntimeError(f"{name}: importer skipped part of the source")
    host.run([args.opt, str(raw), "--ctjs-resolve-globals", "--ctjs-lift-to-scf",
              "-o", str(prepared)])
    return js, prepared, len(FUNCTION.findall(text))


def native(args, ir, name, denominator, *, claimed=0, options=""):
    output = args.work / f"{name}.native.mlir"
    host.run([args.opt, str(ir), f"--ctnative-lower-to-emitc=optimize=false {options}", "-o", str(output)])
    text = output.read_text()
    reasons = REFUSAL.findall(text)
    remaining = len(FUNCTION.findall(text))
    admitted = len(NATIVE.findall(text))
    if remaining + admitted != denominator or len(reasons) != remaining:
        raise RuntimeError(f"{name}: native analysis lost source functions or named refusals")
    if admitted != claimed or (remaining and re.search(r"\bemitc\.func @main\(", text)):
        raise RuntimeError(f"{name}: expected {claimed}/{denominator} native, got {admitted}\n{text}")
    return output, reasons


def follow(args, ir, name):
    contract = dict(host.manifest(args.opt, ir), initial_intrinsics=["Map"], realm_global_this=True)
    config = args.work / f"{name}.contract.json"
    report = args.work / f"{name}.prefix.json"
    output = args.work / f"{name}.prefix.mlir"
    config.write_text(json.dumps(contract, indent=2) + "\n")
    host.run([args.opt, str(ir),
              f"--ctnative-specialize-host-prefix=manifest={config} output={report} {FOLLOW}",
              "-o", str(output)])
    return json.loads(report.read_text()), output


def reference_tool(opt):
    executable = Path(shutil.which(opt) or opt).resolve()
    for parent in executable.parents:
        candidate = parent / "test" / "ctcompile-test-native-reference"
        if candidate.is_file():
            return candidate
    raise RuntimeError("build ctcompile-test-native-reference before running the export boundary")


def node_executable(args):
    node = args.node or os.environ.get("CTCOMPILE_NODE") or shutil.which("node")
    if node:
        return node
    executable = Path(shutil.which(args.opt) or args.opt).resolve()
    for parent in executable.parents:
        cache = parent / "CMakeCache.txt"
        if cache.is_file():
            match = re.search(r"^CTCOMPILE_BOOTSTRAP_NODE:FILEPATH=(.+)$", cache.read_text(), re.M)
            if match and Path(match[1]).is_file():
                return match[1]
    raise RuntimeError("native export boundary requires independent Node; pass --node")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--node")
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    reference = reference_tool(args.opt)
    node = node_executable(args)
    sources = {
        "scalar": "var host = {}; host.slot = 42; var trace = host.slot;",
        "plain_table": ("var host = {}; function make() { return {get() { return 42; }}; } "
                        "host.slot = make(); var trace = host.slot.get();"),
        "published": Path(__file__).with_suffix(".js").read_text(),
        "confined": ("function run() { const ns = {slot: (function() { const state = new Map(); "
                     "return { get() { return state.size; } }; })()}; return ns.slot.get(); } "
                     "var trace = run();"),
    }
    prepared = {}
    for name, source in sources.items():
        js, ir, count = prepare(args, name, source)
        expected_count = {"scalar": 1, "plain_table": 3, "published": 4, "confined": 4}[name]
        if count != expected_count:
            raise RuntimeError(f"{name}: expected {expected_count} source functions, got {count}")
        expected = "trace=42\n" if name in {"scalar", "plain_table"} else "trace=0\n"
        if host.run([node, "-e", NODE, str(js)]).stdout != expected:
            raise RuntimeError(f"{name}: Node disagrees with the source oracle")
        if host.run([str(reference), str(js)]).stdout != expected:
            raise RuntimeError(f"{name}: interpreter disagrees with the source oracle")
        claimed = {"plain_table": 1, "confined": 4}.get(name, 0)
        _, reasons = native(args, ir, name, count, claimed=claimed)
        if name != "confined" and GLOBAL_ESCAPE not in reasons:
            raise RuntimeError(f"{name}: native refusal no longer names the global owner")
        if name in {"plain_table", "published"} and not any(TABLE_ESCAPE in reason for reason in reasons):
            raise RuntimeError(f"{name}: native refusal no longer names the table storage boundary")
        prepared[name] = ir, count, reasons

    # Even a complete positive slot query does not currently own its global
    # root. A future consumer must retain the escape verdict and supply an owner.
    ir, count, reasons = prepared["scalar"]
    contract = host.manifest(args.opt, ir)
    report, reported, _ = host.analyze(args.opt, ir, contract, args.work / "scalar-proof", strict=True)
    if not report["proved"] or len(report["slots"]) != 1 or report["slots"][0]["proved_edges"] != 1:
        raise RuntimeError("scalar: missing complete, live host slot proof")
    slot = report["slots"][0]
    if (not slot["fresh_source_root"] or slot["binding"] != "host" or slot["property"] != "slot"
            or (slot["source_writes"], slot["source_reads"], slot["candidate_edges"]) != (1, 1, 1)
            or report["observation_stores"] != 1):
        raise RuntimeError(f"scalar: wrong source root, field flow or observation: {report}")
    _, with_report = native(args, reported, "scalar-reported", count)
    if with_report != reasons:
        raise RuntimeError("scalar: report attributes supplied native ownership")

    # Complete host analysis now identifies the actual current uncaptured
    # getter. Its per-call evidence alone does not supply native ownership.
    ir, _, _ = prepared["plain_table"]
    report, _, _ = host.analyze(args.opt, ir, host.manifest(args.opt, ir),
                               args.work / "plain-table-proof")
    if (not report["proved"] or report["reason"]
            or len(report["slots"]) != 1 or report["slots"][0]["proved_edges"] != 1):
        raise RuntimeError(f"plain_table: missing the complete current getter proof: {report}")

    # The checked source graph now reaches the owning method-table carrier and
    # the complete native call component. The standalone/lifetime gate lives
    # in native-owned-global-methods.py; no-manifest admission above stays 1/3.
    config = args.work / "plain-table-proof.json"
    checked, checked_reasons = native(args, ir, "plain-table-owned-proof", 3, claimed=3,
                                      options=f"host-manifest={config}")
    checked_text = checked.read_text()
    if ("ctnative.host_owner_proved = true" not in checked_text
            or checked_reasons or "owned_global_set" not in checked_text
            or "invoke_callable" not in checked_text):
        raise RuntimeError("plain_table: missing owning storage or callable execution")
    repeated, _ = native(args, checked, "plain-table-owned-proof-rerun", 3, claimed=3,
                          options=f"host-manifest={config}")
    if ("ctnative.host_owner_proved = false" not in repeated.read_text()
            or "host contract module fingerprint mismatch" not in repeated.read_text()):
        raise RuntimeError("plain_table: semantic native mutation reused the earlier manifest")

    # This source has no remaining startup-prefix boundary. Its Map, closure
    # and publication still execute at runtime and do not yet have a native path.
    ir, count, reasons = prepared["published"]
    complete, _, _ = host.analyze(args.opt, ir, host.manifest(args.opt, ir),
                                  args.work / "published-complete-proof")
    if (complete["proved"] or complete["reason"] != "property receiver lacks a fresh own-data object proof"
            or any(slot["proved_edges"] for slot in complete["slots"])):
        raise RuntimeError(f"published: missing the complete ownership/call boundary: {complete}")
    report, followed = follow(args, ir, "published")
    measured = tuple(report[key] for key in (
        "resolved_calls", "summarized_factories", "summarized_provider_calls",
        "runtime_provider_allocations", "capture_edges", "publication_writes",
        "runtime_provider_reads"))
    if not report["valid"] or report["full_host_contract_claimed"] or measured != (2, 1, 1, 1, 1, 1, 1):
        raise RuntimeError(f"published: startup evidence changed: {report}")
    if report["boundary"] or report["provider_boundary"]:
        raise RuntimeError(f"published: expected a completed startup prefix: {report}")
    complete, _, _ = host.analyze(args.opt, followed, host.manifest(args.opt, followed),
                                  args.work / "published-followed-complete-proof")
    if complete["proved"] or any(slot["proved_edges"] for slot in complete["slots"]):
        raise RuntimeError(f"published: prefix rewrite supplied a complete export proof: {complete}")
    before, after = ir.read_text(), followed.read_text()
    for operation in ("ctjs.construct", "ctjs.create_closure", "ctjs.create_cell", "ctjs.set_property"):
        if before.count(operation) != after.count(operation):
            raise RuntimeError(f"published: {operation} was removed by prefix discovery")
    first, followed_reasons = native(args, followed, "published-followed", count)
    if followed_reasons != reasons:
        raise RuntimeError("published: prefix evidence changed native ownership/callee admission")
    _, repeated_reasons = native(args, first, "published-rerun", count)
    if repeated_reasons != reasons:
        raise RuntimeError("published: rerun consumed its earlier diagnostic annotations")

    # A claimed complete proof copied onto the real published source is not a
    # live export proof. Global R3 and the table's storage refusal still apply.
    forged = args.work / "published-forged.mlir"
    text, changed = re.subn(r"\bmodule attributes \{", "module attributes {"
                           'ctnative.host_proved = true, ctnative.host_slots = [], '
                           'ctnative.host_fingerprint = "forged", ', after, count=1)
    if changed != 1:
        raise RuntimeError("forgery control could not mark the module")
    forged.write_text(text)
    first, forged_reasons = native(args, forged, "published-forged", count)
    _, rerun_reasons = native(args, first, "published-forged-rerun", count)
    if forged_reasons != reasons or rerun_reasons != reasons:
        raise RuntimeError("published: forged report supplied native authority")
    print("native export boundary: scalar/table without manifest remain 0/1 and 1/3 native; "
          "checked table 3/3; complete published startup prefix remains 0/4; confined control 4/4; "
          "four Node/interpreter observations and forged/rerun controls pass")


if __name__ == "__main__":
    main()
