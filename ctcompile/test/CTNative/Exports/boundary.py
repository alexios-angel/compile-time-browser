#!/usr/bin/env python3
"""Check that host-publication analysis results do not by themselves make code native.

A host proof or prefix rewrite says what a global holds; it does not give the
native backend an owner for that global or a proof that a call is safe. The
expected native/refused counts below change only when the lowering pass gains
a consumer that supplies that ownership or call proof.
"""

import json
from pathlib import Path
import re

from CTNative.HostContract import contract as host
from CTNative.harness import REFUSAL, forge_module, native_args, prepare_js

FUNCTION = re.compile(r"^\s*ctjs\.func\b", re.M)
NATIVE = re.compile(r"^\s*emitc\.func\b", re.M)
GLOBAL_ESCAPE = "an object literal that escapes - it reaches `ctjs.store_global`"
TABLE_ESCAPE = "returned method table is written through an alias or stored into another object"
FOLLOW = (
    "follow-publication=true follow-provider-reads=true follow-provider-mutations=true "
    "follow-provider-diagnostics=true follow-provider-callbacks=true"
)
NODE = r"""const fs = require('node:fs');
const vm = require('node:vm');
const context = vm.createContext({});
vm.runInContext(fs.readFileSync(process.argv[1], 'utf8'), context);
const trace = vm.runInContext('trace', context);
if (typeof trace !== 'number' || !Number.isFinite(trace)) throw new Error('non-numeric trace');
process.stdout.write('trace=' + String(trace) + '\n');
"""


def prepare(args, name, source):
    """Write `source` to <name>.js, import and prepare it, and count its functions."""
    js = args.work / f"{name}.js"
    prepared = args.work / f"{name}.prepared.mlir"
    js.write_text(source + "\n")
    text = prepare_js(args, js, args.work / f"{name}.raw.mlir", prepared, complete=True)
    return js, prepared, len(FUNCTION.findall(text))


def native(args, ir, name, denominator, *, claimed=0, options=""):
    """Run native lowering on `ir` and check that exactly `claimed` of the
    `denominator` source functions became native and every other one carries a
    named refusal. Returns the output path and the refusal reasons."""
    output = args.work / f"{name}.native.mlir"
    host.run(
        [
            args.opt,
            str(ir),
            f"--ctnative-lower-to-emitc=optimize=false {options}",
            "-o",
            str(output),
        ]
    )
    text = output.read_text()
    reasons = REFUSAL.findall(text)
    remaining = len(FUNCTION.findall(text))
    admitted = len(NATIVE.findall(text))
    if remaining + admitted != denominator or len(reasons) != remaining:
        raise RuntimeError(f"{name}: native analysis lost source functions or named refusals")
    if admitted != claimed or (remaining and re.search(r"\bemitc\.func @main\(", text)):
        raise RuntimeError(
            f"{name}: expected {claimed}/{denominator} native, got {admitted}\n{text}"
        )
    return output, reasons


def follow(args, ir, name):
    """Run host-prefix specialization with every follow option on. Returns the
    JSON report and the rewritten module's path."""
    contract = dict(host.manifest(args.opt, ir), initial_intrinsics=["Map"], realm_global_this=True)
    config = args.work / f"{name}.contract.json"
    report = args.work / f"{name}.prefix.json"
    output = args.work / f"{name}.prefix.mlir"
    config.write_text(json.dumps(contract, indent=2) + "\n")
    host.run(
        [
            args.opt,
            str(ir),
            f"--ctnative-specialize-host-prefix=manifest={config} output={report} {FOLLOW}",
            "-o",
            str(output),
        ]
    )
    return json.loads(report.read_text()), output


def main(argv=None):
    args = native_args(__doc__, argv=argv)
    reference, node = args.reference, args.node
    sources = {
        "scalar": "var host = {}; host.slot = 42; var trace = host.slot;",
        "plain_table": (
            "var host = {}; function make() { return {get() { return 42; }}; } "
            "host.slot = make(); var trace = host.slot.get();"
        ),
        "published": Path(__file__).with_suffix(".js").read_text(),
        "confined": (
            "function run() { const ns = {slot: (function() { const state = new Map(); "
            "return { get() { return state.size; } }; })()}; return ns.slot.get(); } "
            "var trace = run();"
        ),
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
        if name in {"plain_table", "published"} and not any(
            TABLE_ESCAPE in reason for reason in reasons
        ):
            raise RuntimeError(f"{name}: native refusal no longer names the table storage boundary")
        prepared[name] = ir, count, reasons

    # A complete, positive host slot proof does not give the global an owner:
    # the native refusals stay the same with the proof attributes attached.
    ir, count, reasons = prepared["scalar"]
    contract = host.manifest(args.opt, ir)
    report, reported, _ = host.analyze(
        args.opt, ir, contract, args.work / "scalar-proof", strict=True
    )
    if not report["proved"] or len(report["slots"]) != 1 or report["slots"][0]["proved_edges"] != 1:
        raise RuntimeError("scalar: missing complete, live host slot proof")
    slot = report["slots"][0]
    if (
        not slot["fresh_source_root"]
        or slot["binding"] != "host"
        or slot["property"] != "slot"
        or (slot["source_writes"], slot["source_reads"], slot["candidate_edges"]) != (1, 1, 1)
        or report["observation_stores"] != 1
    ):
        raise RuntimeError(f"scalar: wrong source root, field flow or observation: {report}")
    _, with_report = native(args, reported, "scalar-reported", count)
    if with_report != reasons:
        raise RuntimeError("scalar: report attributes supplied native ownership")

    # Host analysis identifies the getter that plain_table's method table
    # holds. That per-call proof alone does not give the table a native owner.
    ir, _, _ = prepared["plain_table"]
    report, _, _ = host.analyze(
        args.opt, ir, host.manifest(args.opt, ir), args.work / "plain-table-proof"
    )
    if (
        not report["proved"]
        or report["reason"]
        or len(report["slots"]) != 1
        or report["slots"][0]["proved_edges"] != 1
    ):
        raise RuntimeError(f"plain_table: missing the complete current getter proof: {report}")

    # With the host manifest, all three functions of plain_table are admitted:
    # the global method table gets an owning native slot and its getter is
    # called natively. Execution and lifetime of that output are tested in
    # test/CTNative/Ownership/global-methods.py. Without the manifest (above)
    # only 1 of the 3 is admitted. A rerun on the lowered output must not reuse
    # the manifest, because its module fingerprint no longer matches.
    config = args.work / "plain-table-proof.json"
    checked, checked_reasons = native(
        args, ir, "plain-table-owned-proof", 3, claimed=3, options=f"host-manifest={config}"
    )
    checked_text = checked.read_text()
    if (
        "ctnative.host_owner_proved = true" not in checked_text
        or checked_reasons
        or "owned_global_set" not in checked_text
        or "invoke_callable" not in checked_text
    ):
        raise RuntimeError("plain_table: missing owning storage or callable execution")
    repeated, _ = native(
        args,
        checked,
        "plain-table-owned-proof-rerun",
        3,
        claimed=3,
        options=f"host-manifest={config}",
    )
    if (
        "ctnative.host_owner_proved = false" not in repeated.read_text()
        or "host contract module fingerprint mismatch" not in repeated.read_text()
    ):
        raise RuntimeError("plain_table: semantic native mutation reused the earlier manifest")

    # The published fixture's owning proof needs the manifest to declare Map as
    # the standard intrinsic. The prefix rewrite that follows must keep the
    # allocation, closure and publication operations, which still run at runtime.
    ir, count, reasons = prepared["published"]
    complete, _, _ = host.analyze(
        args.opt, ir, host.manifest(args.opt, ir), args.work / "published-complete-proof"
    )
    if (
        complete["proved"]
        or complete["reason"] != "property receiver lacks a fresh own-data object proof"
        or any(slot["proved_edges"] for slot in complete["slots"])
    ):
        raise RuntimeError(f"published: missing the complete ownership/call boundary: {complete}")
    contract = dict(host.manifest(args.opt, ir), initial_intrinsics=["Map"])
    complete, _, _ = host.analyze(
        args.opt, ir, contract, args.work / "published-map-proof", strict=True
    )
    if not complete["proved"] or complete["slots"][0]["proved_edges"] != 1:
        raise RuntimeError("published: missing the live captured Map publication proof")
    config = args.work / "published-map-proof.json"
    checked, checked_reasons = native(
        args, ir, "published-map-owned", count, claimed=4, options=f"host-manifest={config}"
    )
    if (
        checked_reasons
        or "ctnative.host_owner_proved = true" not in checked.read_text()
        or "ctnative::map_size" not in checked.read_text()
    ):
        raise RuntimeError("published: missing the owning captured Map native component")
    report, followed = follow(args, ir, "published")
    measured = tuple(
        report[key]
        for key in (
            "resolved_calls",
            "summarized_factories",
            "summarized_provider_calls",
            "runtime_provider_allocations",
            "capture_edges",
            "publication_writes",
            "runtime_provider_reads",
        )
    )
    if (
        not report["valid"]
        or report["full_host_contract_claimed"]
        or measured != (2, 1, 1, 1, 1, 1, 1)
    ):
        raise RuntimeError(f"published: startup evidence changed: {report}")
    if report["boundary"] or report["provider_boundary"]:
        raise RuntimeError(f"published: expected a completed startup prefix: {report}")
    complete, _, _ = host.analyze(
        args.opt,
        followed,
        host.manifest(args.opt, followed),
        args.work / "published-followed-complete-proof",
    )
    if complete["proved"] or any(slot["proved_edges"] for slot in complete["slots"]):
        raise RuntimeError(
            f"published: prefix rewrite supplied a complete export proof: {complete}"
        )
    before, after = ir.read_text(), followed.read_text()
    for operation in (
        "ctjs.construct",
        "ctjs.create_closure",
        "ctjs.create_cell",
        "ctjs.set_property",
    ):
        if before.count(operation) != after.count(operation):
            raise RuntimeError(f"published: {operation} was removed by prefix discovery")
    first, followed_reasons = native(args, followed, "published-followed", count)
    if followed_reasons != reasons:
        raise RuntimeError("published: prefix evidence changed native ownership/callee admission")
    _, repeated_reasons = native(args, first, "published-rerun", count)
    if repeated_reasons != reasons:
        raise RuntimeError("published: rerun consumed its earlier diagnostic annotations")

    # A forged module attribute claiming a complete host proof must not be
    # trusted: the refusals for storing into a global and for the method table's
    # storage still apply, with the same reasons as without it.
    forged = args.work / "published-forged.mlir"
    forged.write_text(
        forge_module(
            after,
            "ctnative.host_proved = true, ctnative.host_slots = [], "
            'ctnative.host_fingerprint = "forged"',
        )
    )
    first, forged_reasons = native(args, forged, "published-forged", count)
    _, rerun_reasons = native(args, first, "published-forged-rerun", count)
    if forged_reasons != reasons or rerun_reasons != reasons:
        raise RuntimeError("published: forged report supplied native authority")
    print(
        "native export boundary: scalar/table without manifest remain 0/1 and 1/3 native; "
        "checked table 3/3, checked captured Map 4/4; "
        "published startup prefix without manifest remains 0/4; confined control 4/4; "
        "four Node/interpreter observations and forged/rerun controls pass"
    )


def test_boundary(lit):
    main(
        lit.args(
            "--translate ctjs-translate --opt ctjs-opt --work %t --node %node --reference %native_reference"
        )
    )


if __name__ == "__main__":
    main()
