"""Keep the original browser UMD source at the native ownership boundary."""

import hashlib
import importlib.util
import json
from pathlib import Path
import re

from .driver_common import (
    CONSTANT_GLOBAL_NODE,
    SOURCE,
    boundary,
    check_call_preservation,
    contract,
    forge_leaf_evidence,
    host,
    methods,
    owned,
    source_calls,
)


def umd_source():
    root = Path(__file__).resolve().parents[5]
    modules = []
    for name in ("bootstrap-data-probe", "bootstrap-host-prefix"):
        spec = importlib.util.spec_from_file_location(name, root / "tools/check" / f"{name}.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        modules.append(module)
    probe, prefix = modules
    fragment, _ = probe.extract(
        (root / "ctbrowser/vendor/bootstrap/bootstrap.bundle.js").read_text()
    )
    source = probe.generate(fragment, "browser")
    if (len(source.encode()), hashlib.sha256(source.encode()).hexdigest()) != (
        3218,
        prefix.PROVIDER_PROGRAM_SHA256["browser"],
    ):
        raise RuntimeError("browser UMD source changed its original 3218-byte pin")
    return source, probe.EXPECTED, prefix


def umd_contract(args, ir, name, observations):
    config = args.work / f"{name}.contract.json"
    value = host.manifest(
        args.opt, ir, absent=("define", "exports", "module", "self"), undefined=("undefined",)
    )
    value.update(
        roots=[{"binding": "globalThis", "properties": ["bootstrap"]}],
        observations=sorted(observations),
        initial_intrinsics=["Map", "Array"],
        realm_global_this=True,
    )
    config.write_text(json.dumps(value, indent=2) + "\n")
    return config


def prepare_umd(args):
    source, values, prefix = umd_source()
    js, prepared, functions = boundary.prepare(args, "umd-browser", source.removesuffix("\n"))
    if js.read_bytes() != source.encode() or functions != 7:
        raise RuntimeError("browser UMD preparation changed its source or seven functions")
    config = umd_contract(args, prepared, "umd-browser", values)
    report_file = args.work / "umd-browser.prefix.json"
    specialized = args.work / "umd-browser.specialized.mlir"
    host.run(
        [
            args.opt,
            str(prepared),
            f"--ctnative-specialize-host-prefix=manifest={config} output={report_file} "
            + boundary.FOLLOW
            + " follow-provider-objects=true",
            "-o",
            str(specialized),
        ]
    )
    report = json.loads(report_file.read_text())
    prefix.check_provider_objects(report)
    if (
        not report["valid"]
        or report["full_host_contract_claimed"]
        or report["selected_branches"] != 5
        or report["targets"] != ["fn$3", *prefix.PROVIDER_OBJECT_TARGETS]
    ):
        raise RuntimeError("browser prefix no longer proves the exact UMD selection")
    before, after = prepared.read_text(), specialized.read_text()
    wrapper = re.search(r"ctjs\.func @fn\$2\(.*?(?=\n  ctjs\.func |\n})", after, re.S)
    if (
        len(boundary.FUNCTION.findall(after)) != functions
        or not wrapper
        or "scf.if" in wrapper[0]
        or len(re.findall(r"ctjs.call_direct @fn\$3\([^,]+, [^,]+, %arg4\)", wrapper[0])) != 1
        or not re.search(r"ctjs.call_direct @fn\$2\([^,]+, [^,]+, [^,]+, %arg0, [^)]+\)", after)
    ):
        raise RuntimeError("browser prefix lost the original factory/fallback operands")
    for operation in ("construct", "create_cell", "create_closure", "store_global"):
        if before.count("ctjs." + operation) != after.count("ctjs." + operation):
            raise RuntimeError(f"browser prefix removed runtime {operation}")
    return js, prepared, specialized, values


def observe_umd(args, js, values):
    expected = "".join(f"{key}={value}\n" for key, value in sorted(values.items()))
    result = host.run([args.node, "-e", CONSTANT_GLOBAL_NODE, str(js), json.dumps(sorted(values))])
    if result.stdout != expected or result.stderr:
        raise RuntimeError("browser UMD typed Node observations changed")
    result = host.run([str(args.reference), str(js)])
    if (
        result.stdout != expected
        or f"({len(values)} number, 0 boolean, 0 string, 0 null, 0 undefined)" not in result.stderr
    ):
        raise RuntimeError("browser UMD typed VM observations changed")
    return expected


def umd_mutations(text):
    match = re.search(r"ctjs\.func @fn\$2\(.*?(?=\n  ctjs\.func |\n})", text, re.S)
    if not match:
        raise RuntimeError("UMD mutations lost the actual wrapper")
    wrapper = match[0]
    exports = re.search(r'(%[-\w.$]+) = ctjs.load_global "exports"', wrapper)
    publication = re.search(r"ctjs.set_property (%[-\w.$]+)\[", wrapper)
    if not exports or not publication:
        raise RuntimeError("UMD mutations lost the retained absent load or publication")
    cases = {
        "fallback_returned": (
            re.sub(r"ctjs.return %[-\w.$]+", "ctjs.return %arg3", wrapper),
            None,
        ),
        "fallback_published": (
            wrapper.replace(publication[0], "ctjs.set_property %arg3["),
            None,
        ),
        "absent_bare_read": (
            wrapper.replace(
                exports[0],
                exports[0] + f'\n    ctjs.store_global "traceErrorCount", {exports[1]}',
            ),
            None,
        ),
        "absent_written": (
            wrapper.replace(
                exports[0],
                "%umd_written = ctjs.constant #ctjs.undefined\n"
                '    ctjs.store_global "exports", %umd_written\n    ' + exports[0],
            ),
            "an absent host binding has a source write",
        ),
    }
    return {
        name: (text[: match.start()] + changed + text[match.end() :], reason)
        for name, (changed, reason) in cases.items()
    }


def check_umd_preparation(args, compilers, nm):
    source = (
        SOURCE.replace(
            "var host = {};",
            "var host = {}; var tracePre = 0; var traceKind = typeof host === 'object' ? 1 : 0;",
        )
        .replace("function(factory)", "function(unused, factory)")
        .replace("})(function()", "})((tracePre = 1, this), function()")
    )
    js, ir, functions = boundary.prepare(args, "umd-safe-preparation", source.removesuffix("\n"))
    if functions != 4 or js.read_text() != source:
        raise RuntimeError("safe UMD preparation changed its four-function source")
    expected = observe_umd(args, js, {"trace": 0, "traceKind": 1, "tracePre": 1})

    def configure(subject, name):
        config = contract(args, subject, name)
        value = json.loads(config.read_text())
        value["observations"] = ["trace", "traceKind", "tracePre"]
        config.write_text(json.dumps(value, indent=2) + "\n")
        return config

    config = configure(ir, "umd-safe-preparation")
    for policy, options in (("default", ""), ("disabled", "optimize=false")):
        output = owned.lower(args, ir, "umd-safe-" + policy, config, options=options)
        methods.census(output, 4, policy, admitted=4)
        owned.standalone(args, output, "umd-safe-" + policy, expected, compilers, nm)
    refusals = {
        "returned": source.replace(
            "host.slot = factory();", "host.slot = factory(); return unused;"
        ),
        "arguments": source.replace(
            "host.slot = factory();", "host.slot = factory(); tracePre = arguments.length;"
        ),
        "unproved_increment": source.replace("tracePre = 1, this", "tracePre = tracePre + 1, this"),
    }
    for name, changed in refusals.items():
        js, subject, count = boundary.prepare(args, "umd-safe-" + name, changed.removesuffix("\n"))
        if count != 4:
            raise RuntimeError("observed UMD actual lost its original four functions")
        observe_umd(
            args, js, {"trace": 0, "traceKind": 1, "tracePre": 2 if name == "arguments" else 1}
        )
        fresh = configure(subject, "umd-safe-" + name)
        for policy, options in (("default", ""), ("disabled", "optimize=false")):
            output = methods.refused(
                args,
                subject,
                "umd-safe-" + name + "-" + policy,
                fresh,
                options=options,
                admitted=0,
            )
            check_call_preservation(subject.read_text(), output.read_text(), name)


def check_umd(args, node, reference, compilers, nm):
    js, prepared, specialized, values = prepare_umd(args)
    observe_umd(args, js, values)
    raw_config = umd_contract(args, prepared, "umd-raw", values)
    config = umd_contract(args, specialized, "umd-specialized", values)
    _, _, prefix = umd_source()

    def refused(subject, name, manifest, options, reason=None):
        output = methods.refused(
            args, subject, name, manifest, options=options, reason=reason, admitted=0
        )
        check_call_preservation(subject.read_text(), output.read_text(), name)
        signatures = r"ctjs.func @[^\n]+?\) -> !ctjs.value"
        if re.findall(signatures, subject.read_text()) != re.findall(
            signatures, output.read_text()
        ):
            raise RuntimeError(f"{name}: failed proof changed original function arguments")
        return output

    def owned_boundary(subject, name, manifest, options):
        output = owned.lower(args, subject, name, manifest, options=options, cleanup=False)
        text = methods.census(output, 7, name, admitted=0)
        entry = re.search(r"ctjs\.func @_script_\$0\(.*?(?=\n  ctjs\.func |\n})", text, re.S)
        if (
            "ctnative.host_owner_proved = true" not in text
            or "standard Map identity is unproved" not in text
            or len(source_calls(text)) != len(source_calls(specialized.read_text()))
            or not entry
            or re.findall(r"ctjs.call_direct @(fn\$[456])\(", entry[0])
            != prefix.PROVIDER_OBJECT_TARGETS
        ):
            raise RuntimeError(f"{name}: original UMD lost its owned 23-call native refusal")
        return output

    for policy, options in (("default", ""), ("disabled", "optimize=false")):
        flags = options + " host-max-steps=1000000"
        refused(prepared, "umd-raw-" + policy, raw_config, flags)
        output = owned_boundary(specialized, "umd-" + policy, config, flags)
        for steps in (0, 32, 100_000):
            refused(
                specialized,
                f"umd-{policy}-budget-{steps}",
                config,
                options + f" host-max-steps={steps}",
                "budget" if steps < 100_000 else None,
            )
        for field in ("absent_bindings", "undefined_bindings"):
            missing = args.work / f"umd-{policy}-missing-{field}.json"
            value = json.loads(config.read_text())
            value[field] = []
            missing.write_text(json.dumps(value, indent=2) + "\n")
            refused(specialized, f"umd-{policy}-missing-{field}", missing, flags)
        forged = args.work / f"umd-{policy}-forged.mlir"
        forged.write_text(forge_leaf_evidence(specialized.read_text()))
        refused(forged, f"umd-{policy}-forged-stale", config, flags, "fingerprint mismatch")
        fresh = umd_contract(args, forged, f"umd-{policy}-forged-fresh", values)
        checked = owned_boundary(forged, f"umd-{policy}-forged-fresh", fresh, flags)
        check_call_preservation(output.read_text(), checked.read_text(), "umd-fresh")
        for name, (text, reason) in umd_mutations(specialized.read_text()).items():
            subject = args.work / f"umd-{policy}-{name}.mlir"
            unprinted = subject.with_suffix(".unprinted.mlir")
            unprinted.write_text(text)
            # Canonical SSA names keep operand-preservation comparisons exact
            # when the mutation inserts another result before existing calls.
            host.run([args.opt, str(unprinted), "-o", str(subject)])
            refused(subject, f"umd-{policy}-{name}-stale", config, flags, "fingerprint mismatch")
            fresh = umd_contract(args, subject, f"umd-{policy}-{name}", values)
            refused(subject, f"umd-{policy}-{name}", fresh, flags, reason)
    check_umd_preparation(args, compilers, nm)
    print(
        "original browser UMD: 3218 unchanged bytes, 19 typed observations, 23 live Data calls; "
        "complete specialized ownership at 1m, 0/7 native; raw/default and observed/absent/stale "
        "controls refuse; four-function evaluated-actual/root-typeof native control passes"
    )
