#!/usr/bin/env python3
"""Check transactional private Map state; source methods stay runtime."""

import argparse
from dataclasses import dataclass, field
import importlib.util
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import sys


sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("prefix", Path(__file__).with_name("host-prefix.py"))
prefix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prefix)
host = prefix.host
OPTIONS = "follow-publication=true follow-provider-reads=true follow-provider-mutations=true"
UNDEFINED = {"kind": "undefined"}
OBJECT = {"kind": "object"}
FUNCTION = {"kind": "function"}

METHODS = {
    "store": ("key, value", "resource.set(key, value); return 0;"),
    "read": ("key", "return resource.get(key);"),
    "contains": ("key", "return resource.has(key);"),
    "count": ("", "return resource.size;"),
    "erase": ("key", "return resource.delete(key);"),
    "next": ("", "return 99;"),
}


@dataclass
class Case:
    name: str
    steps: list
    extra_methods: dict = field(default_factory=dict)
    setup: str = ""
    before: str = ""
    twice: bool = False
    mutable: bool = False
    completed: int | None = None
    calls: int | None = None
    boundary: str | None = "next"
    invalid: bool = False
    error: str | None = None
    literalize: bool = False

    def program(self):
        methods = METHODS | self.extra_methods
        fields = [f"{name}: function {name}({parameters}) {{ {body} }}"
                  for name, (parameters, body) in methods.items()]
        calls = "first = factory(); host.slot = factory();" if self.twice else "host.slot = factory();"
        declarations = " ".join(f"var trace{index} = 0;" for index in range(len(self.observations())))
        setup = "var host = {}; var first; var alias; var keyA = {}; var keyB = {}; "
        setup += "var nanKey; var minusZero; " + declarations + "\n"
        factory = ("(function(factory) { " + calls + " })(function() { " +
                   ("let" if self.mutable else "const") + " resource = new Map; return {" +
                   ", ".join(fields) + "}; });\n")
        lines = []
        observation = 0
        for expression, expected in self.steps:
            if expected is None:
                lines.append(expression)
            else:
                lines.append(f"trace{observation} = {expression};")
                observation += 1
        return self.before + setup + factory + self.setup + "\n" + "\n".join(lines) + "\nhost.slot.next();\n"

    def observations(self):
        return [expected for _, expected in self.steps if expected is not None]

    def methods(self):
        return [re.search(r"\.([A-Za-z]+)\(", expression)[1]
                for expression, expected in self.steps if expected is not None]

    def summaries(self):
        return len(self.observations()) if self.completed is None else self.completed


def cases():
    result = [
        Case("basic", [("host.slot.read('missing')", UNDEFINED), ("host.slot.contains('missing')", False),
                       ("host.slot.count()", 0), ("host.slot.store('x', 42)", 0),
                       ("host.slot.read('x')", 42), ("host.slot.contains('x')", True), ("host.slot.count()", 1),
                       ("host.slot.one()", True)], extra_methods={"one": ("", "return resource.size === 1;")}),
        Case("replacement", [("host.slot.store('x', 1)", 0), ("host.slot.store('x', 2)", 0),
                             ("host.slot.count()", 1), ("host.slot.read('x')", 2)]),
        Case("failed_delete", [("host.slot.store('x', 1)", 0), ("host.slot.erase('missing')", False),
                               ("host.slot.count()", 1), ("host.slot.read('x')", 1)]),
        Case("reinsert", [("host.slot.store('x', 1)", 0), ("host.slot.erase('x')", True),
                          ("host.slot.contains('x')", False), ("host.slot.read('x')", UNDEFINED),
                          ("host.slot.count()", 0), ("host.slot.store('x', 2)", 0),
                          ("host.slot.read('x')", 2), ("host.slot.count()", 1)]),
        Case("primitive_tags", [("host.slot.store(1, 11)", 0), ("host.slot.store('1', 22)", 0),
                                ("host.slot.store(true, 33)", 0), ("host.slot.store(null, 44)", 0),
                                ("host.slot.store(undefined, 55)", 0), ("host.slot.read(1)", 11),
                                ("host.slot.read('1')", 22), ("host.slot.read(true)", 33),
                                ("host.slot.read(null)", 44), ("host.slot.read(undefined)", 55),
                                ("host.slot.count()", 5)]),
        Case("same_value_zero", [("host.slot.store(minusZero, 7)", 0), ("host.slot.read(0)", 7),
                                 ("host.slot.store(0, 8)", 0), ("host.slot.count()", 1),
                                 ("host.slot.read(minusZero)", 8), ("host.slot.store(nanKey, 9)", 0),
                                 ("host.slot.store(nanKey, 10)", 0), ("host.slot.count()", 2),
                                 ("host.slot.read(nanKey)", 10), ("host.slot.erase(nanKey)", True),
                                 ("host.slot.count()", 1)], setup="nanKey = 0 / 0; minusZero = -0;", literalize=True),
        Case("object_keys", [("host.slot.store(keyA, 11)", 0), ("alias = keyA;", None),
                             ("host.slot.store(alias, 12)", 0), ("host.slot.store(keyB, 21)", 0),
                             ("host.slot.count()", 2), ("host.slot.read(keyA)", 12), ("host.slot.read(keyB)", 21),
                             ("alias.value = 99;", None), ("host.slot.read(alias)", 12)]),
        Case("two_factories", [("first.store('x', 11)", 0), ("host.slot.store('x', 21)", 0),
                               ("first.read('x')", 11), ("host.slot.read('x')", 21)], twice=True),
        Case("copied_method", [("first.store('x', 11)", 0), ("host.slot.store('x', 21)", 0),
                               ("host.slot.read = first.read;", None), ("host.slot.read('x')", 11),
                               ("host.slot.count()", 1)], twice=True),
        Case("selected_state", [("host.slot.store('x', 1)", 0), ("host.slot.adjust('x', 2)", 2),
                                ("host.slot.read('x')", 2)],
             extra_methods={"adjust": ("key, value", "if (resource.has(key)) { resource.set(key, value); } else { globalThis.foreign(); } return resource.get(key);")}),
        Case("chained_set", [("host.slot.chain()", 2), ("host.slot.read('a')", 1), ("host.slot.read('b')", 2)],
             extra_methods={"chain": ("", "resource.set('a', 1).set('b', 2); return resource.size;")}),
        Case("method_replaced", [("host.slot.store('x', 1)", 0), ("host.slot.read('x')", 1),
                                 ("host.slot.read = function replacement(key) { return 77; };", None),
                                 ("host.slot.read('x')", 77)], completed=2, boundary="replacement"),
        Case("table_replaced", [("host.slot.store('x', 1)", 0), ("host.slot.read('x')", 1),
                                ("alias = host.slot; host.slot = {read: function replacement(key) { return 77; }, next: alias.next};", None),
                                ("host.slot.read('x')", 77)], completed=2, boundary="replacement"),
    ]
    nested = {
        "nest": ("key, value", "const inner = new Map; inner.set('value', value); resource.set(key, inner); return 0;"),
        "peek": ("key", "return resource.get(key).get('value');"),
    }
    result.append(Case("nested_identity", [("host.slot.nest(keyA, 11)", 0), ("host.slot.nest(keyB, 21)", 0),
                                           ("host.slot.peek(keyA)", 11), ("host.slot.peek(keyB)", 21),
                                           ("host.slot.nest(keyA, 12)", 0), ("host.slot.peek(keyA)", 12),
                                           ("host.slot.peek(keyB)", 21)], extra_methods=nested))
    result.append(Case("nested_alias", [("host.slot.shared()", 0), ("host.slot.bump('a', 2)", 0),
                                        ("host.slot.peek('b')", 2)], extra_methods=nested | {
        "shared": ("", "const inner = new Map; resource.set('a', inner); resource.set('b', inner); inner.set('value', 1); return 0;"),
        "bump": ("key, value", "resource.get(key).set('value', value); return 0;"),
    }))
    result.extend([
        Case("unknown_key", [("host.slot.store(this, 1)", 0)], completed=0, boundary="store"),
        Case("unknown_value", [("host.slot.store('x', this)", 0)], completed=0, boundary="store"),
        Case("object_payload", [("host.slot.store('x', keyA)", 0)], completed=0, boundary="store"),
        Case("arithmetic_unsupported", [("host.slot.store('x', 0 / 0)", 0)], completed=0, calls=1, boundary=None),
        Case("unknown_between", [("host.slot.store('x', 1)", 0), ("host.slot.read('x')", 1),
                                 ("globalThis.foreign();", None), ("host.slot.read('x')", 1)],
             completed=2, calls=3, boundary=None, error="TypeError"),
        Case("accessor_between", [("host.slot.store('x', 1)", 0),
                                  ("Object.defineProperty(host.slot, 'read', {get: function() { return function(key) { return 77; }; }});", None),
                                  ("host.slot.read('x')", 77)], completed=0, calls=0, boundary=None),
        Case("provider_changed", [("host.slot.store('x', 1)", 0)], before="Map = function() {};\n",
             completed=0, calls=0, boundary=None, invalid=True, error="TypeError"),
        Case("prototype_changed", [("host.slot.store('x', 1)", 0)],
             before="Map.prototype.set = function(key, value) { return this; };\n", completed=0, calls=0, boundary=None, invalid=True),
    ])
    bad = {
        "return_resource": ("resource.set(key, 1); return resource;", OBJECT, None),
        "return_object": ("resource.set(key, 1); return key;", OBJECT, None),
        "return_method": ("resource.set(key, 1); return resource.set;", FUNCTION, None),
        "global_escape": ("resource.set(key, 1); globalThis.leaked = resource; return 0;", 0, None),
        "property_escape": ("resource.set(key, 1); key.saved = resource; return 0;", 0, None),
        "detached_receiver": ("const set = resource.set; set(key, 1); return 0;", 0, "TypeError"),
        "own_override": ("resource.set = 0; return 0;", 0, None),
        "direct_cycle": ("resource.set('self', resource); return 0;", 0, None),
        "indirect_cycle": ("const inner = new Map; resource.set('child', inner); inner.set('parent', resource); return 0;", 0, None),
        "resource_key": ("resource.set(resource, 1); return 0;", 0, None),
        "unknown_after_write": ("resource.set(key, 1); globalThis.foreign(); return 0;", 0, "TypeError"),
        "throw_after_write": ("resource.set(key, 1); throw 7;", 0, "number:7"),
        "argument_property_after_write": ("resource.set(key, 1); return key.value;", UNDEFINED, None),
        "unretained_allocation": ("const inner = new Map; inner.set('value', 1); resource.set(key, 1); return 0;", 0, None),
    }
    for name, (body, observation, error) in bad.items():
        result.append(Case(name, [("host.slot.bad(keyA)", observation)], extra_methods={"bad": ("key", body)},
                           completed=0, boundary="bad", error=error))
    result.append(Case("mutable_capture", [("host.slot.bad(keyA)", 0)],
                       extra_methods={"bad": ("key", "resource = new Map; return 0;")},
                       mutable=True, completed=0, calls=1, boundary=None))
    return result


def primitive(value):
    if isinstance(value, dict):
        return value
    if isinstance(value, bool):
        return {"kind": "boolean", "value": value}
    if isinstance(value, (float, int)):
        text = "NaN" if math.isnan(value) else "-0" if value == 0 and math.copysign(1, value) < 0 else str(value)
        if text.endswith(".0"):
            text = text[:-2]
        return {"kind": "number", "value": text}
    return {"kind": "string", "value": value}


NODE = r"""const fs = require('node:fs');
const vm = require('node:vm');
const input = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const context = vm.createContext({});
function encode(value) {
    if (value === undefined) return {kind: 'undefined'};
    if (value === null) return {kind: 'null'};
    if (typeof value === 'number') return {kind: 'number', value: Object.is(value, -0) ? '-0' : String(value)};
    if (typeof value === 'boolean' || typeof value === 'string') return {kind: typeof value, value};
    return {kind: typeof value};
}
let error = null;
try { vm.runInContext(fs.readFileSync(input.source, 'utf8'), context); }
catch (failure) { error = typeof failure === 'object' ? failure.name : typeof failure + ':' + String(failure); }
const observations = {};
for (const name of input.observations) observations[name] = encode(vm.runInContext(name, context));
process.stdout.write(JSON.stringify({node: process.version, error, observations}) + '\n');
"""


def oracle(node, folder, case, js):
    driver = folder / "node.cjs"
    specification = folder / "node-input.json"
    driver.write_text(NODE)
    names = [f"trace{index}" for index in range(len(case.observations()))]
    specification.write_text(json.dumps({"source": str(js.resolve()), "observations": names}))
    result = host.run([node, str(driver), str(specification)])
    report = json.loads(result.stdout)
    if report["error"] != case.error:
        raise RuntimeError(f"{case.name}: Node error expected {case.error}, got {report['error']}")
    if not case.error:
        expected = dict(zip(names, map(primitive, case.observations())))
        if report["observations"] != expected:
            raise RuntimeError(f"{case.name}: Node observations {report['observations']} differ from {expected}")
    (folder / "node.json").write_text(json.dumps(report, indent=2) + "\n")


def attribute(text):
    if text == "#ctjs.undefined":
        return UNDEFINED
    if text == "#ctjs.null":
        return {"kind": "null"}
    match = re.fullmatch(r"#ctjs.boolean<(true|false)>", text)
    if match:
        return primitive(match[1] == "true")
    match = re.fullmatch(r"#ctjs.number<(\d+)>", text)
    if match:
        return primitive(struct.unpack(">d", int(match[1]).to_bytes(8, "big"))[0])
    match = re.fullmatch(r'#ctjs.string<(".*")>', text)
    if match:
        return primitive(json.loads(match[1]))
    raise RuntimeError(f"unsupported reported primitive {text}")


def literalize_keys(text):
    # This fixture tests Map key equality rather than primitive precomputation.
    # Preserve the Node source, but normalize only its literal 0/0 and -0 in
    # the script entry to exact CTJS constants before the checked fingerprint.
    pattern = r"ctjs\.func @_script_\$0\(.*?(?=\n  ctjs\.func |\n})"
    match = re.search(pattern, text, re.S)
    if not match:
        raise RuntimeError("missing script for SameValueZero normalization")
    body = match[0]
    zeroes = set(re.findall(r"(%\w+) = ctjs.constant #ctjs.number<0>", body))
    changed = {"nan": 0, "zero": 0}

    def division(found):
        if found[2] not in zeroes or found[3] not in zeroes:
            raise RuntimeError("fixture normalization encountered nonzero division")
        changed["nan"] += 1
        return f"{found[1]} = ctjs.constant #ctjs.number<9221120237041090560>"

    def negation(found):
        if found[2] not in zeroes:
            raise RuntimeError("fixture normalization encountered nonzero negation")
        changed["zero"] += 1
        return f"{found[1]} = ctjs.constant #ctjs.number<9223372036854775808>"

    body = re.sub(r"(%\w+) = ctjs.binary div (%\w+), (%\w+)", division, body)
    body = re.sub(r"(%\w+) = ctjs.unary neg (%\w+)", negation, body)
    if changed != {"nan": 1, "zero": 1}:
        raise RuntimeError(f"SameValueZero literal normalization shape changed: {changed}")
    return text[:match.start()] + body + text[match.end():]


def contract(opt, ir):
    observations = sorted(set(re.findall(r'ctjs.store_global "(trace\d+)"', ir.read_text())))
    if not observations:
        raise RuntimeError("fixture has no declared source observations")
    return dict(host.manifest(opt, ir, undefined=("undefined",)), observations=observations,
                initial_intrinsics=["Map"], realm_global_this=True)


def specialize(opt, ir, manifest, path, **options):
    path.with_suffix(".report.json").unlink(missing_ok=True)
    path.with_suffix(".out.mlir").unlink(missing_ok=True)
    report, output, result = prefix.specialize(opt, ir, manifest, path, **options)
    if result.returncode not in (0, 1) or report is None:
        raise RuntimeError(f"missing proof report or abnormal compiler exit: {result.stderr}")
    return report, output, result


def no_facts(report, label):
    fields = ("selected_branches", "resolved_calls", "summarized_factories", "capture_edges",
              "summarized_provider_calls", "runtime_provider_reads", "runtime_provider_mutations",
              "runtime_nested_provider_allocations")
    if report["valid"] or any(report[name] for name in fields) or report["provider_calls"]:
        raise RuntimeError(f"{label}: invalid analysis retained usable facts: {report}")


def check_report(case, report, before, after):
    if case.invalid:
        no_facts(report, case.name)
        return
    if not report["valid"] or report["full_host_contract_claimed"]:
        raise RuntimeError(f"{case.name}: wrong complete-host boundary: {report}")
    rows = report["provider_calls"]
    expected_count = case.summaries()
    expected_calls = case.calls if case.calls is not None else expected_count + (2 if case.twice else 1) + 1
    if (len(rows), report["summarized_provider_calls"], report["resolved_calls"]) != (expected_count, expected_count, expected_calls):
        raise RuntimeError(f"{case.name}: mutation path crossed or missed a boundary: {report}")
    if report["provider_reads"] or report["selected_branches"]:
        raise RuntimeError(f"{case.name}: mutation traversal used legacy empty state or rewrote branches")
    for index, row in enumerate(rows):
        if attribute(row["result"]) != primitive(case.observations()[index]):
            raise RuntimeError(f"{case.name}: invocation {index} returned {row['result']}")
        if not row["target"].startswith(case.methods()[index] + "$"):
            raise RuntimeError(f"{case.name}: invocation {index} resolved the wrong live method")
    if case.boundary and not report["targets"][-1].startswith(case.boundary + "$"):
        raise RuntimeError(f"{case.name}: wrong boundary target: {report['targets']}")
    operations = [operation for row in rows for operation in row["operations"]]
    allocations = [allocation for row in rows for allocation in row["allocations"]]
    if report["runtime_provider_reads"] != sum(op["member"] in {"get", "has", "size"} for op in operations):
        raise RuntimeError(f"{case.name}: incorrect completed read count")
    if report["runtime_provider_mutations"] != sum(op["member"] in {"set", "delete"} for op in operations):
        raise RuntimeError(f"{case.name}: mutation attempts counted as successful changes")
    if report["runtime_nested_provider_allocations"] != len(allocations):
        raise RuntimeError(f"{case.name}: partial or missing nested allocation facts")
    for item in operations + allocations:
        if item["map_id"] <= 0 or item["allocation_operation"] < 0 or item["invocation_operation"] < 0:
            raise RuntimeError(f"{case.name}: missing per-invocation resource identity")
    if case.name in {"two_factories", "copied_method"}:
        if [row["factory_index"] for row in rows] != [0, 1, 0, 1]:
            raise RuntimeError(f"{case.name}: factory or copied-capture identities collapsed")
        if rows[0]["operations"][0]["map_id"] == rows[1]["operations"][0]["map_id"]:
            raise RuntimeError(f"{case.name}: separate factory Maps share one identity")
    elif any(row["factory_index"] != 0 for row in rows):
        raise RuntimeError(f"{case.name}: summary uses a different factory")
    if case.name == "nested_identity":
        if len(allocations) != 3 or len({item["map_id"] for item in allocations}) != 3:
            raise RuntimeError("repeated nested construction collapsed runtime Maps")
        if len({item["allocation_operation"] for item in allocations}) != 1 or len({item["invocation_operation"] for item in allocations}) != 3:
            raise RuntimeError("nested allocation provenance lost its source site or invocation")
    if case.name == "failed_delete":
        deletion = [op for op in operations if op["member"] == "delete"]
        if len(deletion) != 1 or attribute(deletion[0]["result"]) != primitive(False):
            raise RuntimeError("an unsuccessful delete was reported as a successful mutation")
    for operation in ("ctjs.construct", "ctjs.create_cell", "ctjs.create_closure", "ctjs.set_property", "scf.if"):
        if before.count(operation) != after.count(operation):
            raise RuntimeError(f"{case.name}: runtime {operation} changed")
    # The caller may be rewritten; no source method, including mutation/error
    # branches that this particular invocation did not take, may be rewritten.
    for name in METHODS | case.extra_methods:
        pattern = r"ctjs\.func @" + name + r"\$\d+\(.*?(?=\n  ctjs\.func |\n})"
        if re.search(pattern, before, re.S)[0] != re.search(pattern, after, re.S)[0]:
            raise RuntimeError(f"{case.name}: reusable {name} method body changed")


def controls(args, prepared):
    original, _ = prepared["replacement"]
    manifest = contract(args.opt, original)
    readonly, _, _ = specialize(args.opt, original, manifest, args.work / "readonly",
                                        options="follow-publication=true follow-provider-reads=true")
    if readonly["provider_calls"] or readonly["summarized_provider_calls"] or readonly["resolved_calls"] != 2:
        raise RuntimeError("mutation following became implicit in read-only mode")
    for label, options in [("no-publication", "follow-provider-reads=true follow-provider-mutations=true"),
                           ("no-reads", "follow-publication=true follow-provider-mutations=true")]:
        report, _, _ = specialize(args.opt, original, manifest, args.work / label,
                                          options=options, success=False)
        no_facts(report, label)
    _, rewritten, _ = specialize(args.opt, original, manifest, args.work / "once", options=OPTIONS)
    stale, _, _ = specialize(args.opt, rewritten, manifest, args.work / "stale", options=OPTIONS, success=False)
    no_facts(stale, "stale source")
    rerun, _, _ = specialize(args.opt, rewritten, contract(args.opt, rewritten), args.work / "rerun", options=OPTIONS)
    if rerun["provider_calls"] or rerun["runtime_provider_mutations"]:
        raise RuntimeError("fresh analysis reused old mutation state on rewritten source")
    unsafe, _ = prepared["unknown_after_write"]
    forged = args.work / "forged.mlir"
    forged.write_text(unsafe.read_text().replace("module attributes {", 'module attributes {ctnative.host_provider_state = "committed", ctnative.host_mutations = 99 : i64, ', 1))
    report, _, _ = specialize(args.opt, forged, contract(args.opt, unsafe), args.work / "forged-check", options=OPTIONS)
    if report["provider_calls"] or report["runtime_provider_mutations"] or report["resolved_calls"] != 2:
        raise RuntimeError("forged metadata crossed a write-then-unknown effect boundary")
    no_provider, _, _ = specialize(args.opt, original, dict(manifest, initial_intrinsics=[]), args.work / "no-provider", options=OPTIONS)
    if no_provider["provider_calls"] or no_provider["runtime_provider_mutations"]:
        raise RuntimeError("an undeclared Map provider authorized mutations")

    # Discover the completion threshold without copying internal step counts.
    # Every lower work limit must withhold earlier completed summaries and any
    # transaction-local mutations or nested allocations, including late limits.
    budget_case, _ = prepared["nested_identity"]
    budget_manifest = args.work / "budget.json"
    budget_manifest.write_text(json.dumps(contract(args.opt, budget_case)))
    budget_report = args.work / "budget.report.json"

    def run_budget(limit):
        budget_report.unlink(missing_ok=True)
        result = subprocess.run([args.opt, str(budget_case),
                                 f"--ctnative-specialize-host-prefix=manifest={budget_manifest} output={budget_report} {OPTIONS} max-steps={limit}",
                                 "-o", "/dev/null"], capture_output=True, text=True, timeout=60)
        report = json.loads(budget_report.read_text())
        if not report["valid"]:
            if (result.returncode != 1 or report["reason"] != "host prefix analysis work budget exhausted" or
                "host prefix refused: host prefix analysis work budget exhausted" not in result.stderr):
                raise RuntimeError(f"unexpected work-limit refusal: {result.stderr}")
            no_facts(report, f"budget {limit}")
        elif result.returncode or len(report["provider_calls"]) != 7:
            raise RuntimeError(f"a completed work limit lost normal-return summaries: {report}")
        return report["valid"]

    low, high = 0, 100000
    if run_budget(low) or not run_budget(high):
        raise RuntimeError("unexpected initial work-limit bracket")
    while high - low > 1:
        middle = (low + high) // 2
        if run_budget(middle):
            high = middle
        else:
            low = middle
    if run_budget(high - 1) or not run_budget(high):
        raise RuntimeError("work-limit completion boundary is not stable")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--translate")
    parser.add_argument("--opt")
    parser.add_argument("--node")
    parser.add_argument("--oracle-only", action="store_true")
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    if args.oracle_only and not args.node:
        parser.error("--oracle-only requires --node")
    if not args.oracle_only and (not args.translate or not args.opt):
        parser.error("compiler checks require --translate and --opt")
    args.work.mkdir(parents=True, exist_ok=True)
    prepared = {}
    for case in cases():
        folder = args.work / case.name
        folder.mkdir(exist_ok=True)
        js = folder / "program.js"
        js.write_text(case.program())
        if args.node:
            oracle(args.node, folder, case, js)
        if not args.oracle_only:
            raw = folder / "raw.mlir"
            ir = folder / "prepared.mlir"
            host.run([args.translate, "--ctbrowser-js-to-ctjs", str(js), "-o", str(raw)])
            host.run([args.opt, str(raw), "--ctjs-resolve-globals", "--ctjs-lift-to-scf", "-o", str(ir)])
            if case.literalize:
                ir.write_text(literalize_keys(ir.read_text()))
            prepared[case.name] = (ir, case)
            report, output, _ = specialize(args.opt, ir, contract(args.opt, ir), folder / "checked",
                                                  options=OPTIONS, success=not case.invalid)
            check_report(case, report, ir.read_text(), output.read_text() if output.exists() else "")
    if not args.oracle_only:
        controls(args, prepared)
    mode = "Node source cases" if args.oracle_only else "source cases, live identities, runtime bodies and effect/work controls"
    print(f"host provider mutations: {len(cases())} {mode} passed")


if __name__ == "__main__":
    main()
