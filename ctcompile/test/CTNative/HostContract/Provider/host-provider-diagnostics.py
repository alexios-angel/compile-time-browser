#!/usr/bin/env python3
"""Check diagnostic snapshots and source callback effects without erasing either."""

import argparse
from dataclasses import dataclass, field
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from urllib.parse import quote


sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    "provider_mutations", Path(__file__).with_name("host-provider-mutations.py"))
mutations = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = mutations
spec.loader.exec_module(mutations)
prefix, host = mutations.prefix, mutations.host
BASE = mutations.OPTIONS
DIAGNOSTICS = BASE + " follow-provider-diagnostics=true"
CALLBACKS = DIAGNOSTICS + " follow-provider-callbacks=true"
MESSAGE = "Bootstrap doesn't allow more than one instance per element. Bound instance: alpha."
LABEL = "'Bootstrap doesn\\'t allow more than one instance per element. Bound instance: '"
METHODS = {
    "store": ("key, value", "resource.set(key, value); return 0;"),
    "erase": ("key", "return resource.delete(key);"),
    "read": ("key", "return resource.get(key);"),
    "count": ("", "return resource.size;"),
    "first": ("", "return Array.from(resource.keys())[0];"),
    "second": ("", "return Array.from(resource.keys())[1];"),
    "third": ("", "return Array.from(resource.keys())[2];"),
    "fractional": ("", "return Array.from(resource.keys())[0.5];"),
    "label": ("", "return " + LABEL + " + Array.from(resource.keys())[0] + '.';"),
    "diagnose": ("", "console.error(" + LABEL + " + Array.from(resource.keys())[0] + '.'); return 0;"),
    "next": ("", "return 99;"),
    "wrong": ("", "return -1;"),
}
RECORDER = "callCount = callCount + 1; lastMatch = message === " + json.dumps(MESSAGE) + " ? 41 : 99;"
# The existing interpreter represents Map.keys as an eager Array; Node uses a
# live one-use iterator, and truncates fractional numeric array indices. Its
# generic object tag also lacks Map's built-in tag.
# Pin these separate expectations only on refused diagnostic paths. Supported
# immediate snapshots and recorder callbacks must agree with Node exactly.
INTERPRETER_DIVERGENCES = {
    "fractional_index": {"trace1": "alpha"},
    "iterator_mutation": {"trace1": mutations.UNDEFINED},
    "iterator_reuse": {"trace1": "alpha"},
    "iterator_override": {"trace1": "alpha"},
    "object_coercion": {"trace1": "Map: [object Object]"},
}


@dataclass
class Case:
    name: str
    # Expected values are source observations. A third element marks calls
    # whose normal-return provider summaries should complete.
    steps: list
    methods: dict = field(default_factory=dict)
    recorder: str = RECORDER
    console: str | None = None
    before: str = ""
    setup: str = ""
    initial_count: str = "0"
    final_count: object = 0
    final_match: object = 0
    completed: int | None = None
    calls: int | None = None
    boundary: str | None = "next"
    invalid: bool = False
    error: str | None = None
    callbacks: list = field(default_factory=list)
    callback_mode: bool = False

    def observations(self):
        return [expected for _, expected, _ in self.steps if expected is not None]

    def provider_observations(self):
        return [(expression, expected) for expression, expected, provider in self.steps if provider]

    def summaries(self):
        return len(self.provider_observations()) if self.completed is None else self.completed

    def expected(self):
        values = {f"trace{index}": value for index, value in enumerate(self.observations())}
        values.update(callCount=self.final_count, lastMatch=self.final_match)
        return values

    def program(self):
        declarations = " ".join(f"var trace{index} = 0;" for index in range(len(self.observations())))
        fields = [f"{name}: function {name}({parameters}) {{ {body} }}"
                  for name, (parameters, body) in (METHODS | self.methods).items()]
        console = self.console or ("{error: function recorder(message) { " + self.recorder + " }}")
        lines = [self.before, "var host = {}; var alias; var iterator;",
                 f"var callCount = {self.initial_count}; var lastMatch = 0; " + declarations,
                 "var console = " + console + ";",
                 "(function(factory) { host.slot = factory(); })(function() { "
                 "const resource = new Map; return {" + ", ".join(fields) + "}; });", self.setup]
        observation = 0
        for expression, expected, _ in self.steps:
            if expected is None:
                lines.append(expression)
            else:
                lines.append(f"trace{observation} = {expression};")
                observation += 1
        # This next actual callee depends on the callback post-state. Merely
        # reporting callback writes while retaining stale globals must fail.
        lines.append("if (callCount === " + json.dumps(self.final_count) +
                     ") { host.slot.next(); } else { host.slot.wrong(); }")
        return "\n".join(lines) + "\n"


def call(expression, expected):
    return expression, expected, True


def statement(text):
    return text, None, False


def cases():
    store = call("host.slot.store('alpha', 1)", 0)
    diagnose = call("host.slot.diagnose()", 0)
    result = [
        Case("insertion_order", [store, call("host.slot.store('beta', 2)", 0),
                                call("host.slot.store('gamma', 3)", 0),
                                call("host.slot.first()", "alpha"),
                                call("host.slot.store('alpha', 4)", 0),
                                call("host.slot.first()", "alpha"),
                                call("host.slot.erase('alpha')", True),
                                call("host.slot.first()", "beta"),
                                call("host.slot.store('alpha', 5)", 0),
                                call("host.slot.third()", "alpha"),
                                call("host.slot.second()", "gamma")]),
        Case("message", [store, call("host.slot.label()", MESSAGE)]),
        Case("diagnostic_arguments_only", [store, diagnose, call("host.slot.read('alpha')", 1)],
             completed=1, boundary="diagnose", final_count=1, final_match=41),
        Case("repeated_callback", [store, diagnose, diagnose, call("host.slot.read('alpha')", 1)],
             final_count=2, final_match=41, callback_mode=True,
             callbacks=[("recorder", {"callCount": 1, "lastMatch": 41}),
                        ("recorder", {"callCount": 2, "lastMatch": 41})]),
        Case("callback_read_after_write", [store, diagnose, call("host.slot.read('alpha')", 1)],
             recorder="callCount = callCount + 1; lastMatch = callCount === 1 ? 41 : 99;",
             final_count=1, final_match=41, callback_mode=True,
             callbacks=[("recorder", {"callCount": 1, "lastMatch": 41})]),
        Case("changed_callback", [store, diagnose,
                                 statement("console.error = function replacement(message) { "
                                           "callCount = callCount + 10; lastMatch = message === " +
                                           json.dumps(MESSAGE) + " ? 42 : 99; };"),
                                 diagnose, call("host.slot.read('alpha')", 1)],
             final_count=11, final_match=42, callback_mode=True,
             callbacks=[("recorder", {"callCount": 1, "lastMatch": 41}),
                        ("replacement", {"callCount": 11, "lastMatch": 42})]),
        Case("empty_snapshot", [call("host.slot.first()", mutations.UNDEFINED)],
             completed=0, boundary="first"),
        Case("out_of_range", [store, call("host.slot.second()", mutations.UNDEFINED)],
             completed=1, boundary="second"),
        Case("fractional_index", [store, call("host.slot.fractional()", mutations.UNDEFINED)],
             completed=1, boundary="fractional"),
        Case("non_string_key", [call("host.slot.store(1, 7)", 0), call("host.slot.first()", 1)],
             completed=1, boundary="first"),
        Case("snapshot_escape", [store, call("host.slot.bad()", mutations.OBJECT)],
             methods={"bad": ("", "return Array.from(resource.keys());")},
             completed=1, boundary="bad"),
        Case("snapshot_write", [store, call("host.slot.bad()", "changed")],
             methods={"bad": ("", "const snapshot = Array.from(resource.keys()); "
                                  "snapshot[0] = 'changed'; return snapshot[0];")},
             completed=1, boundary="bad"),
        Case("iterator_mutation", [store, call("host.slot.bad()", "late")],
             methods={"bad": ("", "const keys = resource.keys(); resource.set('late', 2); "
                                  "return Array.from(keys)[1];")},
             completed=1, boundary="bad"),
        Case("iterator_reuse", [store, call("host.slot.bad()", mutations.UNDEFINED)],
             methods={"bad": ("", "const keys = resource.keys(); Array.from(keys); "
                                  "return Array.from(keys)[0];")},
             completed=1, boundary="bad"),
        Case("iterator_override", [store, call("host.slot.bad()", mutations.UNDEFINED)],
             methods={"bad": ("", "const keys = resource.keys(); "
                                  "keys.next = function() { return {done: true}; }; "
                                  "return Array.from(keys)[0];")},
             # Its nested source closure already exceeds the factory identity
             # proof, before the attempted iterator customization is reached.
             completed=0, calls=1, boundary="fn"),
        Case("detached_keys", [store, call("host.slot.bad()", 0)],
             methods={"bad": ("", "const keys = resource.keys; return Array.from(keys())[0];")},
             completed=1, boundary="bad", error="TypeError"),
        Case("wrong_receiver", [store, call("host.slot.bad()", 0)],
             methods={"bad": ("", "return Array.from(resource.keys.call({}))[0];")},
             completed=1, boundary="bad", error="TypeError"),
        Case("array_from_replaced", [store, call("host.slot.first()", "custom")],
             before="Array.from = function() { return ['custom']; };", invalid=True),
        Case("map_keys_replaced", [store, call("host.slot.first()", "custom")],
             before="Map.prototype.keys = function() { return ['custom']; };", invalid=True),
        Case("object_coercion", [store, call("host.slot.bad()", "Map: [object Map]")],
             methods={"bad": ("", "return 'Map: ' + resource;")}, completed=1, boundary="bad"),
    ]
    refused_callbacks = {
        "unknown_callback_call": (RECORDER + " globalThis.foreign();", 1, 41, "TypeError"),
        "reentrant_callback": (RECORDER + " host.slot.erase('alpha'); host.slot.store('alpha', 7);", 1, 41, None),
        "publication_callback": (RECORDER + " host.slot = host.slot;", 1, 41, None),
        "property_callback": (RECORDER + " host.slot.changed = 1;", 1, 41, None),
        "throwing_callback": (RECORDER + " throw 7;", 1, 41, "number:7"),
        "receiver_callback": (RECORDER + " this.changed = 1;", 1, 41, None),
    }
    for name, (body, count, match, error) in refused_callbacks.items():
        result.append(Case(name, [store, diagnose], recorder=body, completed=1, boundary="diagnose",
                           final_count=count, final_match=match, error=error, callback_mode=True))
    result.append(Case("nonprimitive_global", [store, diagnose], initial_count="{}",
                       recorder="callCount = 1; lastMatch = 41;", final_count=1, final_match=41,
                       completed=1, boundary="diagnose", callback_mode=True))
    result.append(Case("console_getter", [store, diagnose],
                       console="{get error() { host.slot = host.slot; return function recorder(message) { " + RECORDER + " }; }}",
                       completed=0, calls=0, boundary=None, final_count=1, final_match=41,
                       callback_mode=True))
    return result


def build_path(opt, relative):
    executable = Path(shutil.which(opt) or opt).resolve()
    for parent in executable.parents:
        candidate = parent / relative
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"cannot locate {relative} beside {opt}")


def node_executable(args):
    node = args.node or os.environ.get("CTCOMPILE_NODE") or shutil.which("node")
    if node:
        return node
    if args.opt:
        cache = build_path(args.opt, "CMakeCache.txt").read_text()
        match = re.search(r"^CTCOMPILE_BOOTSTRAP_NODE:FILEPATH=(.+)$", cache, re.M)
        if match and Path(match[1]).is_file():
            return match[1]
    raise RuntimeError("provider diagnostic regression requires independent Node; pass --node")


def oracle(node, folder, case, js):
    driver = folder / "node.cjs"
    request = folder / "node-input.json"
    driver.write_text(mutations.NODE)
    request.write_text(json.dumps({"source": str(js.resolve()), "observations": list(case.expected())}))
    observed = json.loads(host.run([node, str(driver), str(request)]).stdout)
    if observed["error"] != case.error:
        raise RuntimeError(f"{case.name}: unexpected Node error: {observed}")
    expected = {name: mutations.primitive(value) for name, value in case.expected().items()}
    if case.error:
        # A failing call never stores its normal result. All observations after
        # it retain their source initializer; callback writes preceding throw
        # remain visible and must still match the separately checked globals.
        for index, (_, _, provider) in enumerate(case.steps):
            if provider and index >= case.summaries():
                expected[f"trace{index}"] = mutations.primitive(0)
    if observed["observations"] != expected:
        raise RuntimeError(f"{case.name}: Node observations {observed['observations']} != {expected}")
    (folder / "node.json").write_text(json.dumps(observed, indent=2) + "\n")
    return observed


def reference_value(value):
    encoded = mutations.primitive(value)
    kind = encoded["kind"]
    if kind in {"undefined", "null"}:
        return kind
    if kind == "boolean":
        return "true" if encoded["value"] else "false"
    if kind == "string":
        return '"' + quote(encoded["value"], safe="-._~") + '"'
    if kind == "number":
        return encoded["value"]
    return None


def interpreter(reference, folder, case, js):
    if case.error:
        # The reference printer does not expose globals after an exception.
        # Node checks the observable partial effects in these refusal cases.
        return {"checked": False, "reason": "reference printer stops on thrown completion"}
    result = host.run([str(reference), str(js)])
    values = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
    expected_values = case.expected() | INTERPRETER_DIVERGENCES.get(case.name, {})
    expected = {name: text for name, value in expected_values.items()
                if (text := reference_value(value)) is not None}
    observed = {name: values.get(name) for name in expected}
    if observed != expected:
        raise RuntimeError(f"{case.name}: interpreter observations {observed} != {expected}")
    evidence = {"checked": True, "agrees_with_node": case.name not in INTERPRETER_DIVERGENCES,
                "observations": observed}
    (folder / "interpreter.json").write_text(json.dumps(evidence, indent=2) + "\n")
    return evidence


def contract(opt, ir):
    observations = sorted(set(re.findall(r'ctjs.store_global "(trace\d+)"', ir.read_text())))
    return dict(host.manifest(opt, ir, undefined=("undefined",)),
                observations=observations + ["callCount", "lastMatch"],
                initial_intrinsics=["Map", "Array"], realm_global_this=True)


def no_facts(report, label):
    mutations.no_facts(report, label)
    if report["runtime_provider_callbacks"] or report["runtime_provider_global_writes"]:
        raise RuntimeError(f"{label}: invalid analysis retained callback effects: {report}")


def check_report(case, report, before, after):
    if case.invalid:
        no_facts(report, case.name)
        return
    if not report["valid"] or report["full_host_contract_claimed"]:
        raise RuntimeError(f"{case.name}: wrong complete-host boundary: {report}")
    rows = report["provider_calls"]
    completed = case.summaries()
    calls = case.calls if case.calls is not None else completed + 2
    if (len(rows), report["summarized_provider_calls"], report["resolved_calls"]) != (completed, completed, calls):
        raise RuntimeError(f"{case.name}: wrong normal-return boundary: {report}")
    for row, (expression, expected) in zip(rows, case.provider_observations()):
        if mutations.attribute(row["result"]) != mutations.primitive(expected):
            raise RuntimeError(f"{case.name}: {expression} returned {row['result']}")
        method = re.search(r"\.([A-Za-z]+)\(", expression)[1]
        if not row["target"].startswith(method + "$"):
            raise RuntimeError(f"{case.name}: wrong live provider method: {row}")
    if case.boundary and not report["targets"][-1].startswith(case.boundary + "$"):
        raise RuntimeError(f"{case.name}: wrong next source callee: {report['targets']}")
    if case.name == "diagnostic_arguments_only" and "`ctjs.call`" not in report["provider_boundary"]:
        raise RuntimeError("diagnostic argument proof did not reach the actual callback call")
    if case.name == "iterator_override" and (report["summarized_factories"] or
            report["capture_edges"] or report["provider_boundary"] or
            report["boundary"] != "resolved call body remains a runtime effect boundary at `ctjs.call`"):
        raise RuntimeError("nested iterator customization crossed the factory identity boundary")
    if report["selected_branches"] or report["provider_reads"]:
        raise RuntimeError(f"{case.name}: diagnostics changed branches or consulted old empty state")
    callbacks = [(row, callback) for row in rows for callback in row["callbacks"]]
    if (report["runtime_provider_callbacks"], len(callbacks)) != (len(case.callbacks), len(case.callbacks)):
        raise RuntimeError(f"{case.name}: partial or missing callback facts: {report}")
    writes = sum(len(callback["writes"]) for _, callback in callbacks)
    if report["runtime_provider_global_writes"] != writes:
        raise RuntimeError(f"{case.name}: incorrect callback global-write count")
    for (row, callback), (target, expected) in zip(callbacks, case.callbacks):
        if not callback["target"].startswith(target + "$"):
            raise RuntimeError(f"{case.name}: callback retained a stale source identity: {callback}")
        if mutations.attribute(callback["result"]) != mutations.UNDEFINED:
            raise RuntimeError(f"{case.name}: source callback gained a return value: {callback}")
        observed = {write["binding"]: mutations.attribute(write["value"])
                    for write in callback["writes"]}
        if observed != {name: mutations.primitive(value) for name, value in expected.items()}:
            raise RuntimeError(f"{case.name}: wrong callback post-state: {callback}")
        if callback["call_operation"] < 0 or row["call_operation"] < 0:
            raise RuntimeError(f"{case.name}: callback lost its source call or enclosing invocation")
    if len(callbacks) > 1 and len({row["call_operation"] for row, _ in callbacks}) != len(callbacks):
        raise RuntimeError(f"{case.name}: repeated callback invocations share one proof identity")
    if len(re.findall(r"\bctjs\.func\b", before)) != len(re.findall(r"\bctjs\.func\b", after)):
        raise RuntimeError(f"{case.name}: changed source function denominator")
    for operation in ("ctjs.construct", "ctjs.create_cell", "ctjs.create_closure", "ctjs.set_property",
                      "ctjs.get_property", "ctjs.binary", "ctjs.store_global", "scf.if"):
        if before.count(operation) != after.count(operation):
            raise RuntimeError(f"{case.name}: runtime {operation} changed")
    for name in set(METHODS | case.methods) | {"recorder", "replacement"}:
        pattern = r"ctjs\.func @" + name + r"\$\d+\(.*?(?=\n  ctjs\.func |\n})"
        bodies = re.findall(pattern, before, re.S)
        if bodies != re.findall(pattern, after, re.S):
            raise RuntimeError(f"{case.name}: reusable {name} body was specialized")


def controls(args, prepared):
    original = prepared["repeated_callback"]
    manifest = contract(args.opt, original)
    for label, options in [("legacy", BASE), ("arguments-only", DIAGNOSTICS)]:
        report, _, _ = mutations.specialize(args.opt, original, manifest, args.work / label, options=options)
        if (report["summarized_provider_calls"], report["resolved_calls"], report["runtime_provider_callbacks"]) != (1, 3, 0):
            raise RuntimeError(f"{label}: callback following became implicit: {report}")
    for label, options in [
        ("no-mutations", "follow-publication=true follow-provider-reads=true follow-provider-diagnostics=true"),
        ("no-diagnostics", BASE + " follow-provider-callbacks=true"),
    ]:
        report, _, _ = mutations.specialize(args.opt, original, manifest, args.work / label,
                                            options=options, success=False)
        no_facts(report, label)
    for intrinsic in ("Map", "Array"):
        limited, _, _ = mutations.specialize(args.opt, original,
            dict(manifest, initial_intrinsics=[name for name in ("Map", "Array") if name != intrinsic]),
            args.work / f"no-{intrinsic}", options=CALLBACKS)
        if limited["runtime_provider_callbacks"] or limited["summarized_provider_calls"] > 1:
            raise RuntimeError(f"undeclared {intrinsic} supplied a diagnostic proof")
    _, rewritten, _ = mutations.specialize(args.opt, original, manifest, args.work / "once", options=CALLBACKS)
    stale, _, _ = mutations.specialize(args.opt, rewritten, manifest, args.work / "stale",
                                      options=CALLBACKS, success=False)
    no_facts(stale, "stale source")
    unsafe = prepared["throwing_callback"]
    forged = args.work / "forged.mlir"
    forged.write_text(unsafe.read_text().replace("module attributes {",
        'module attributes {ctnative.host_provider_callbacks = "proved", '
        'ctnative.host_global_writes = 99 : i64, ctnative.host_snapshots = true, ', 1))
    report, _, _ = mutations.specialize(args.opt, forged, contract(args.opt, unsafe),
                                        args.work / "forged-check", options=CALLBACKS)
    if report["summarized_provider_calls"] != 1 or report["runtime_provider_callbacks"] or report["runtime_provider_global_writes"]:
        raise RuntimeError("forged annotations crossed a write-then-throw callback")

    budget_manifest = args.work / "budget.json"
    budget_manifest.write_text(json.dumps(manifest))
    budget_report = args.work / "budget.report.json"

    def run_budget(limit):
        budget_report.unlink(missing_ok=True)
        result = subprocess.run([args.opt, str(original),
            f"--ctnative-specialize-host-prefix=manifest={budget_manifest} output={budget_report} {CALLBACKS} max-steps={limit}",
            "-o", "/dev/null"], capture_output=True, text=True, timeout=60)
        report = json.loads(budget_report.read_text())
        if not report["valid"]:
            if result.returncode != 1 or report["reason"] != "host prefix analysis work budget exhausted":
                raise RuntimeError(f"unexpected budget refusal: {result.stderr}")
            no_facts(report, f"budget {limit}")
        elif result.returncode or report["summarized_provider_calls"] != 4 or report["runtime_provider_callbacks"] != 2:
            raise RuntimeError(f"completed budget lost callback or provider state: {report}")
        return report["valid"]

    # Locate the public completion threshold instead of mirroring an internal
    # step count. The last insufficient limit has attempted snapshots and
    # global writes, yet every usable fact must roll back with the transaction.
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
        raise RuntimeError("callback completion boundary is unstable")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--translate")
    parser.add_argument("--opt")
    parser.add_argument("--node")
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--oracle-only", action="store_true")
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    if not args.oracle_only and (not args.translate or not args.opt):
        parser.error("compiler checks require --translate and --opt")
    args.work.mkdir(parents=True, exist_ok=True)
    node = node_executable(args)
    reference = None if args.oracle_only else args.reference or build_path(args.opt, "test/ctcompile-test-native-reference")
    prepared, evidence = {}, {}
    for case in cases():
        folder = args.work / case.name
        folder.mkdir(exist_ok=True)
        js = folder / "program.js"
        js.write_text(case.program())
        evidence[case.name] = {"node": oracle(node, folder, case, js)}
        if args.oracle_only:
            continue
        evidence[case.name]["interpreter"] = interpreter(reference, folder, case, js)
        raw, ir = folder / "raw.mlir", folder / "prepared.mlir"
        host.run([args.translate, "--ctbrowser-js-to-ctjs", str(js), "-o", str(raw)])
        if "ctjs.skipped" in raw.read_text():
            raise RuntimeError(f"{case.name}: importer dropped a source function")
        host.run([args.opt, str(raw), "--ctjs-resolve-globals", "--ctjs-lift-to-scf", "-o", str(ir)])
        prepared[case.name] = ir
        options = CALLBACKS if case.callback_mode else DIAGNOSTICS
        report, output, _ = mutations.specialize(args.opt, ir, contract(args.opt, ir), folder / "checked",
                                                  options=options, success=not case.invalid)
        check_report(case, report, ir.read_text(), output.read_text() if output.exists() else "")
        evidence[case.name]["provider"] = report
    if not args.oracle_only:
        controls(args, prepared)
    (args.work / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    mode = "Node source cases" if args.oracle_only else "source cases, runtime bodies, callbacks and rollback controls"
    print(f"host provider diagnostics: {len(cases())} {mode} passed")


if __name__ == "__main__":
    main()
