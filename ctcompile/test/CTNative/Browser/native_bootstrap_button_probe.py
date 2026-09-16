#!/usr/bin/env python3
"""Preserve original Bootstrap Button lifecycle and its current native boundary.

The element below is an explicit JavaScript test double for Node/the interpreter,
not a browser API implementation or evidence of native DOM/component support.
Vendor Config, BaseComponent, Button and their helper dependencies stay verbatim.
"""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

from CTNative.Exports import boundary
from CTNative.harness import run
from CTNative.Ownership.native_owned_global_maps.driver_common import CONSTANT_GLOBAL_NODE

ROOT = Path(__file__).resolve().parents[4]
VENDOR_SHA256 = "5b29f1692a632853edc37b45bc1deedd595a777c9b234e8262ccd74ebfcf3d65"
PROGRAM_SHA256 = "00220390a5b500dc7db5958503fa6895668bf4a47f914b70298d1991f1ee255c"
PRELUDE = """var globalThis = {};
var traceErrorCount = 0;
var console = {error: function(message) { traceErrorCount = traceErrorCount + 1; }};
"""
PUBLICATION = """    return {Button: U, Data: e, eventRegistry: E, nextEventId: () => T};
});
"""
LIFECYCLE = """
var traceConfigReads = 0;
var traceAttributeWrites = 0;
var traceToggleCalls = 0;
var element = {
    nodeType: 1,
    dataset: {bsProbe: "12", bsConfigIgnored: "ignored"},
    attributes: {},
    getAttribute: function(name) {
        traceConfigReads = traceConfigReads + 1;
        return name === "data-bs-config" ? '{"answer":7}' : null;
    },
    setAttribute: function(name, value) {
        traceAttributeWrites = traceAttributeWrites + 1;
        this.attributes[name] = String(value);
    },
    classList: {
        active: false,
        toggle: function(name) {
            traceToggleCalls = traceToggleCalls + 1;
            if (name !== "active") throw new Error("unexpected token");
            this.active = !this.active;
            return this.active;
        }
    }
};
var bootstrap = globalThis.bootstrap;
var traceBefore = bootstrap.Data.get(element, "bs.button") === null ? 1 : 0;
var component = new bootstrap.Button(element);
var traceIdentity = bootstrap.Button.getInstance(element) === component ? 1 : 0;
var traceConstructor = component.constructor === bootstrap.Button ? 1 : 0;
var traceConfigAnswer = component._config.answer;
var traceConfigProbe = component._config.probe;
var traceConfigIgnored = component._config.configIgnored === undefined ? 1 : 0;
var traceEventBefore = Object.keys(bootstrap.eventRegistry).length;
var traceEventNextBefore = bootstrap.nextEventId();
component.toggle();
var traceFirstToggle = element.classList.active && element.attributes["aria-pressed"] === "true" ? 1 : 0;
component.toggle();
var traceSecondToggle = !element.classList.active && element.attributes["aria-pressed"] === "false" ? 1 : 0;
component.dispose();
var traceRemoved = bootstrap.Button.getInstance(element) === null ? 1 : 0;
var traceNullElement = component._element === null ? 1 : 0;
var traceNullConfig = component._config === null ? 1 : 0;
var traceOwnFields = Object.getOwnPropertyNames(component).length;
var traceEventUid = typeof element.uidEvent === "number" ? element.uidEvent : -1;
var traceEventAfter = Object.keys(bootstrap.eventRegistry).length;
var traceEventEmpty = bootstrap.eventRegistry[element.uidEvent] === undefined ? -1 : Object.keys(bootstrap.eventRegistry[element.uidEvent]).length;
var traceEventNextAfter = bootstrap.nextEventId();
"""
EXPECTED = {
    "traceErrorCount": "0",
    "traceConfigReads": "1",
    "traceAttributeWrites": "2",
    "traceToggleCalls": "2",
    "traceBefore": "1",
    "traceIdentity": "1",
    "traceConstructor": "1",
    "traceConfigAnswer": "7",
    "traceConfigProbe": "12",
    "traceConfigIgnored": "1",
    "traceEventBefore": "0",
    "traceEventNextBefore": "1",
    "traceFirstToggle": "1",
    "traceSecondToggle": "1",
    "traceRemoved": "1",
    "traceNullElement": "1",
    "traceNullConfig": "1",
    "traceOwnFields": "2",
    "traceEventUid": "1",
    "traceEventAfter": "1",
    "traceEventEmpty": "0",
    "traceEventNextAfter": "2",
}
STATIC_PROBE = """
var traceDefault = typeof bootstrap.Button.Default === "object" ? 1 : 0;
var traceDefaultType = typeof bootstrap.Button.DefaultType === "object" ? 1 : 0;
var traceGetInstance = typeof bootstrap.Button.getInstance === "function" ? 1 : 0;
var traceDataKey = bootstrap.Button.DATA_KEY === "bs.button" ? 1 : 0;
var traceEventKey = bootstrap.Button.EVENT_KEY === ".bs.button" ? 1 : 0;
var traceName = bootstrap.Button.NAME === "button" ? 1 : 0;
"""
STATIC_EXPECTED = {
    "traceErrorCount": "0",
    "traceConfigReads": "0",
    "traceAttributeWrites": "0",
    "traceToggleCalls": "0",
    "traceBefore": "1",
    "traceDefault": "1",
    "traceDefaultType": "1",
    "traceGetInstance": "1",
    "traceDataKey": "1",
    "traceEventKey": "1",
    "traceName": "1",
}
INHERITANCE = {
    "inherited-method": """class A { static m() { return 7; } }
class D extends A {}
var trace = typeof D.m === "function" ? 1 : 0;
""",
    # Explicit linkage also exercises accessor lookup with a derived receiver.
    "inherited-getter": """class A { static get k() { return this.n; } }
class D extends A { static get n() { return 7; } }
Object.setPrototypeOf(D, A);
var trace = D.k === 7 ? 1 : 0;
""",
}


def source():
    spec = importlib.util.spec_from_file_location(
        "bootstrap_data_probe", ROOT / "tools/check/bootstrap-data-probe.py"
    )
    probe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(probe)
    vendor = (ROOT / "ctbrowser/vendor/bootstrap/bootstrap.bundle.js").read_bytes().decode()
    if hashlib.sha256(vendor.encode()).hexdigest() != VENDOR_SHA256:
        raise RuntimeError("Bootstrap 5.3.8 vendor source pin changed")
    _, provenance = probe.extract(vendor)
    prefix_end = probe.unique_position(vendor, "    const z = t => {", "BaseComponent end")
    button_start = probe.unique_position(vendor, "    class U extends B {", "Button start")
    button_end = probe.unique_position(
        vendor, '    P.on(document, "click.bs.button.data-api",', "Button end"
    )
    # Keep original line positions across the omitted Alert/selector section.
    prefix = vendor[:prefix_end]
    button = vendor[button_start:button_end]
    selected = prefix + "\n" * vendor[prefix_end:button_start].count("\n") + button
    program = PRELUDE + selected + PUBLICATION + LIFECYCLE
    if hashlib.sha256(program.encode()).hexdigest() != PROGRAM_SHA256:
        raise RuntimeError("original Button lifecycle source pin changed")
    provenance.update(
        program_sha256=hashlib.sha256(program.encode()).hexdigest(),
        program_bytes=len(program.encode()),
        preserved_vendor_ranges=[[1, 330], [420, 433]],
        vendor_line_offset=PRELUDE.count("\n"),
        preserved_prefix_sha256=hashlib.sha256(prefix.encode()).hexdigest(),
        preserved_button_sha256=hashlib.sha256(button.encode()).hexdigest(),
        synthetic_element=True,
        native_component_claimed=False,
    )
    if prefix.count("    class W {") != 1 or prefix.count("    class B extends W {") != 1:
        raise RuntimeError("Config/BaseComponent extraction changed")
    if selected.count("P.off(this._element, this.constructor.EVENT_KEY)") != 1:
        raise RuntimeError("Button lifecycle lost the original disposal event effects")
    return program, provenance


def observe(args, name, program, expected):
    js = args.work / f"{name}.js"
    js.write_text(program)
    node = run([args.node, "-e", CONSTANT_GLOBAL_NODE, str(js), json.dumps(sorted(expected))])
    reference = run([args.reference, str(js)])
    for engine, result, values in (
        ("node", node, expected),
        ("reference", reference, expected),
    ):
        (args.work / f"{name}.{engine}.txt").write_text(result.stdout)
        (args.work / f"{name}.{engine}.log").write_text(result.stderr)
        text = "".join(f"{key}={value}\n" for key, value in sorted(values.items()))
        if result.stdout != text or (engine == "node" and result.stderr):
            raise RuntimeError(
                f"{name}: {engine} observations changed:\n{result.stdout}{result.stderr}"
            )
    return dict(
        program_sha256=hashlib.sha256(program.encode()).hexdigest(),
        node=expected,
        reference=expected,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "node", "reference"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    program, provenance = source()
    js = args.work / "bootstrap-button.js"
    js.write_text(program)
    (args.work / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    observations = {
        "lifecycle": observe(args, "bootstrap-button", program, EXPECTED),
        "static": observe(
            args,
            "bootstrap-button-static",
            program[: program.index("var component = new bootstrap.Button(element);")]
            + STATIC_PROBE,
            STATIC_EXPECTED,
        ),
    }
    for name, witness in INHERITANCE.items():
        observations[name] = observe(args, name, witness, {"trace": "1"})
    raw = args.work / "bootstrap-button.raw.mlir"
    prepared = args.work / "bootstrap-button.prepared.mlir"
    imported = run(
        [
            args.translate,
            "--ctbrowser-js-to-ctjs",
            str(js),
            "--mlir-print-debuginfo",
            "-o",
            str(raw),
        ]
    )
    if "is not compiled:" in imported.stderr or "ctjs.skipped" in raw.read_text():
        raise RuntimeError("original Button probe lost imported source functions")
    run(
        [
            args.opt,
            str(raw),
            "--ctjs-resolve-globals",
            "--ctjs-lift-to-scf",
            "--mlir-print-debuginfo",
            # Upstream cf.switch custom syntax cannot reparse a multi-result
            # operand (%v#1); generic MLIR preserves the prepared module.
            "--mlir-print-op-generic",
            "-o",
            str(prepared),
        ]
    )
    functions = len(boundary.FUNCTION.findall(raw.read_text()))
    reports = []
    for optimize in (False, True):
        output = args.work / f"bootstrap-button.{optimize}.native.mlir"
        result = run(
            [
                args.opt,
                str(prepared),
                f"--ctnative-lower-to-emitc=optimize={str(optimize).lower()}",
                "--mlir-print-debuginfo",
                "-o",
                str(output),
            ]
        )
        text = output.read_text()
        remaining = len(boundary.FUNCTION.findall(text))
        native = len(boundary.NATIVE.findall(text))
        reasons = boundary.REFUSAL.findall(text)
        refusals = {}
        for line in text.splitlines():
            if boundary.FUNCTION.match(line):
                symbol = re.search(r"@([^\s(]+)\(", line).group(1)
                refusals[symbol] = boundary.REFUSAL.search(line).group(1)
        if (
            remaining + native != functions
            or len(reasons) != remaining
            or len(refusals) != remaining
            or not all(reasons)
        ):
            raise RuntimeError("Button native census lost functions or precise refusals")
        if (functions, native) != (86, 4) or "_script_$0" not in refusals:
            raise RuntimeError("Button admission changed: require a real native lifecycle gate")
        reports.append(
            dict(optimize=optimize, functions=functions, native=native, refusals=refusals)
        )
        (args.work / f"bootstrap-button.{optimize}.log").write_text(result.stderr)
    (args.work / "measured.json").write_text(
        json.dumps(dict(provenance, observations=observations, policies=reports), indent=2) + "\n"
    )
    print(
        f"Original Bootstrap Button: {len(EXPECTED)} Node/VM lifecycle observations agree; "
        "static inheritance agrees; native refusals retained"
    )


if __name__ == "__main__":
    main()
