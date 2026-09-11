#!/usr/bin/env python3
"""Check live entry-object identities and transactional provider field effects."""

import argparse
from dataclasses import dataclass, field
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys


sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    "provider_diagnostics", Path(__file__).with_name("host-provider-diagnostics.py"))
diagnostics = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = diagnostics
spec.loader.exec_module(diagnostics)
mutations, host = diagnostics.mutations, diagnostics.host
OPTIONS = mutations.OPTIONS + " follow-provider-objects=true"
OBJECT, UNDEFINED = mutations.OBJECT, mutations.UNDEFINED

METHODS = {
    "store": ("key, value", "resource.set(key, value); return 0;"),
    "read": ("key", "return resource.get(key);"),
    "value": ("key", "return resource.get(key).value;"),
    "label": ("key", "return resource.get(key).label;"),
    "same": ("key, value", "return resource.get(key) === value;"),
    "equal": ("left, right", "return resource.get(left) === resource.get(right);"),
    "replace": ("key, value", "resource.get(key).value = value; return resource.get(key).value;"),
    "removeField": ("key", "return delete resource.get(key).value;"),
    "contains": ("key", "return resource.has(key);"),
    "count": ("", "return resource.size;"),
    "erase": ("key", "return resource.delete(key);"),
    "next": ("", "return 99;"),
}
NESTED = {
    "nest": ("key, value", "const inner = new Map; inner.set('payload', value); "
                           "resource.set(key, inner); return 0;"),
    "peek": ("key", "return resource.get(key).get('payload').value;"),
    "payload": ("key", "return resource.get(key).get('payload');"),
}


@dataclass
class Step:
    expression: str
    expected: object = None
    provider: bool = False
    identity: str | None = None


def call(expression, expected, identity=None):
    return Step(expression, expected, True, identity)


def observe(expression, expected):
    return Step(expression, expected)


def statement(expression):
    return Step(expression)


@dataclass
class Case:
    name: str
    steps: list
    methods: dict = field(default_factory=dict)
    setup: str = ""
    first_object: str = "{value: 11, label: 'same'}"
    second_object: str = "{value: 11, label: 'same'}"
    twice: bool = False
    completed: int | None = None
    calls: int | None = None
    boundary: str | None = "next"
    error: str | None = None
    callback: str | None = None
    final_count: int = 0

    def provider_steps(self):
        return [step for step in self.steps if step.provider]

    def summaries(self):
        return len(self.provider_steps()) if self.completed is None else self.completed

    def expected(self):
        result = {f"trace{index}": step.expected for index, step in enumerate(
                  step for step in self.steps if step.expected is not None)}
        result["callCount"] = self.final_count
        return result

    def program(self):
        fields = [f"{name}: function {name}({parameters}) {{ {body} }}"
                  for name, (parameters, body) in (METHODS | self.methods).items()]
        invocation = "first = factory(); host.slot = factory();" if self.twice else "host.slot = factory();"
        declarations = " ".join(f"var {name} = 0;" for name in self.expected())
        lines = ["var host = {}; var first; var alias; var keyA = {}; var keyB = {};",
                 f"var objectA = {self.first_object}; var objectB = {self.second_object};",
                 declarations]
        if self.callback is not None:
            lines.append("var console = {error: function recorder(message) { " + self.callback + " }};")
        lines.extend(["(function(factory) { " + invocation + " })(function() { "
                      "const resource = new Map; return {" + ", ".join(fields) + "}; });", self.setup])
        index = 0
        for step in self.steps:
            if step.expected is None:
                lines.append(step.expression)
            else:
                lines.append(f"trace{index} = {step.expression};")
                index += 1
        lines.append("host.slot.next();")
        return "\n".join(lines) + "\n"

    def options(self):
        return OPTIONS + (" follow-provider-diagnostics=true follow-provider-callbacks=true"
                          if self.callback is not None else "")


def cases():
    store = call("host.slot.store('a', objectA)", 0)
    result = [
        Case("basic", [store, call("host.slot.value('a')", 11), call("host.slot.label('a')", "same"),
                       call("host.slot.read('a')", OBJECT, "objectA"),
                       call("host.slot.same('a', objectA)", True)]),
        Case("equal_fields", [store, call("host.slot.store('b', objectB)", 0),
                              call("host.slot.read('a')", OBJECT, "objectA"),
                              call("host.slot.read('b')", OBJECT, "objectB"),
                              call("host.slot.same('a', objectA)", True),
                              call("host.slot.same('a', objectB)", False),
                              call("host.slot.equal('a', 'b')", False),
                              statement("objectB.value = 21;"), call("host.slot.value('a')", 11),
                              call("host.slot.value('b')", 21)]),
        Case("entry_alias", [statement("alias = objectA;"), store,
                             statement("alias.value = 22;"), call("host.slot.value('a')", 22),
                             call("host.slot.read('a')", OBJECT, "objectA"),
                             call("host.slot.same('a', alias)", True)]),
        Case("returned_alias", [store, call("alias = host.slot.read('a')", OBJECT, "objectA"),
                                observe("alias === objectA", True), statement("alias.value = 23;"),
                                call("host.slot.value('a')", 23), observe("objectA.value", 23),
                                call("host.slot.read('a')", OBJECT, "objectA")]),
        Case("rebound_entry", [store, call("alias = host.slot.read('a')", OBJECT, "objectA"),
                               statement("objectA = {value: 29, label: 'same'};"),
                               call("host.slot.same('a', objectA)", False),
                               call("host.slot.same('a', alias)", True),
                               call("host.slot.value('a')", 11), observe("objectA.value", 29)]),
        Case("shared_payload", [store, call("host.slot.store('b', objectA)", 0),
                                call("host.slot.equal('a', 'b')", True),
                                call("host.slot.replace('a', 24)", 24),
                                call("host.slot.value('b')", 24), observe("objectA.value", 24)]),
        Case("object_key_and_value", [call("host.slot.store(objectA, objectB)", 0),
                                      call("host.slot.same(objectA, objectB)", True),
                                      statement("objectA.value = 1; objectB.value = 25;"),
                                      call("host.slot.value(objectA)", 25),
                                      call("host.slot.count()", 1)]),
        Case("self_key_value", [call("host.slot.store(objectA, objectA)", 0),
                                call("host.slot.same(objectA, objectA)", True),
                                call("host.slot.replace(objectA, 26)", 26),
                                call("host.slot.value(objectA)", 26)]),
        Case("scalar_replacements", [store, statement("objectA.value = 'changed';"),
                                     call("host.slot.value('a')", "changed"),
                                     call("host.slot.replace('a', true)", True),
                                     call("host.slot.value('a')", True),
                                     statement("objectA.value = null;"),
                                     call("host.slot.value('a')", {"kind": "null"}),
                                     statement("objectA.value = undefined;"),
                                     call("host.slot.value('a')", UNDEFINED)]),
        Case("entry_delete_reinsert", [store, statement("delete objectA.value; objectA.value = 27;"),
                                       call("host.slot.value('a')", 27),
                                       call("host.slot.same('a', objectA)", True)]),
        Case("entry_computed_delete_reinsert", [store, statement("delete objectA['value']; objectA.value = 27;"),
                                                call("host.slot.value('a')", 27)]),
        Case("provider_delete_reinsert", [store, call("host.slot.removeField('a')", True),
                                          call("host.slot.replace('a', 28)", 28),
                                          call("host.slot.value('a')", 28), observe("objectA.value", 28)]),
        Case("provider_computed_delete_reinsert", [store, call("host.slot.removeField('a')", True),
                                                   call("host.slot.replace('a', 28)", 28),
                                                   call("host.slot.value('a')", 28)],
             methods={"removeField": ("key", "return delete resource.get(key)['value'];")}),
        Case("replacement_reinsert", [store, call("host.slot.store('a', objectB)", 0),
                                      call("host.slot.same('a', objectB)", True),
                                      call("host.slot.same('a', objectA)", False),
                                      call("host.slot.erase('a')", True),
                                      call("host.slot.contains('a')", False),
                                      call("host.slot.read('a')", UNDEFINED),
                                      call("host.slot.store('a', objectA)", 0),
                                      call("host.slot.read('a')", OBJECT, "objectA"),
                                      call("host.slot.count()", 1)]),
        Case("two_factories", [call("first.store('a', objectA)", 0),
                               call("host.slot.store('a', objectB)", 0),
                               call("first.read('a')", OBJECT, "objectA"),
                               call("host.slot.read('a')", OBJECT, "objectB"),
                               call("first.replace('a', 31)", 31),
                               call("host.slot.value('a')", 11),
                               call("first.same('a', objectB)", False)], twice=True),
        Case("nested_retention", [call("host.slot.nest(keyA, objectA)", 0),
                                  call("host.slot.nest(keyB, objectB)", 0),
                                  call("host.slot.payload(keyA)", OBJECT, "objectA"),
                                  call("host.slot.payload(keyB)", OBJECT, "objectB"),
                                  statement("objectA.value = 32;"),
                                  call("host.slot.peek(keyA)", 32), call("host.slot.peek(keyB)", 11),
                                  call("host.slot.nest(keyA, objectB)", 0),
                                  call("host.slot.payload(keyA)", OBJECT, "objectB"),
                                  call("host.slot.peek(keyA)", 11)], methods=NESTED),
        Case("nested_shared_alias", [call("host.slot.nest(keyA, objectA)", 0),
                                     call("host.slot.nest(keyB, objectA)", 0),
                                     call("alias = host.slot.payload(keyA)", OBJECT, "objectA"),
                                     statement("alias.value = 33;"), call("host.slot.peek(keyB)", 33),
                                     call("host.slot.payload(keyB)", OBJECT, "objectA")], methods=NESTED),
        Case("missing_own_field", [store, statement("delete objectA.value;"),
                                   call("host.slot.value('a')", UNDEFINED)], completed=1, boundary="value"),
        Case("provider_missing_own_field", [store, call("host.slot.removeField('a')", True),
                                            call("host.slot.value('a')", UNDEFINED)],
             completed=2, boundary="value"),
        Case("dynamic_field", [store, call("host.slot.dynamic('a', 'value')", 11)],
             methods={"dynamic": ("key, name", "return resource.get(key)[name];")},
             completed=0, calls=0, boundary=None),
        Case("non_scalar_field", [store], first_object="{value: 11, child: {value: 1}}",
             completed=0, boundary="store"),
        Case("self_cycle", [store], setup="objectA.self = objectA;", completed=0, boundary="store"),
        Case("accessor", [store], first_object="{get value() { return 11; }}",
             completed=0, calls=0, boundary=None),
        Case("prototype", [store], setup="objectA.__proto__ = {value: 31};",
             completed=0, calls=0, boundary=None),
        Case("host_payload", [call("host.slot.store('a', host)", 0)], completed=0, boundary="store"),
        Case("table_payload", [call("host.slot.store('a', host.slot)", 0)], completed=0, boundary="store"),
        Case("non_scalar_replacement", [store, statement("objectA.value = objectB;"),
                                        call("host.slot.value('a')", OBJECT)],
             completed=1, calls=2, boundary=None),
        Case("published_payload", [store, statement("host.extra = objectA;"),
                                   call("host.slot.value('a')", 11)],
             completed=1, calls=2, boundary=None),
    ]
    refused = {
        "map_object_cycle": ("resource.get(key).saved = resource; return 0;", 0, None),
        "object_object_cycle": ("const payload = resource.get(key); payload.self = payload; return 0;", 0, None),
        "provider_local_object": ("const payload = {value: 5}; resource.set(key, payload); return 0;", 0, None),
        "identity_escape": ("globalThis.leaked = resource.get(key); return 0;", 0, None),
        "object_return_without_read": ("return value;", OBJECT, None),
        "dynamic_delete": ("return delete resource.get(key)[value];", True, None),
        "unknown_after_writes": ("resource.set('tentative', value); resource.get(key).value = 42; "
                                 "globalThis.foreign(); return 0;", 0, "TypeError"),
        "throw_after_writes": ("resource.set('tentative', value); resource.get(key).value = 43; "
                               "throw 7;", 0, "number:7"),
    }
    for name, (body, expected, error) in refused.items():
        result.append(Case(name, [store, call("host.slot.bad('a', objectB)", expected)],
                           methods={"bad": ("key, value", body)}, completed=1, boundary="bad", error=error))
    callback = "callCount = callCount + 1;"
    combined = {"combined": ("key, value", "resource.set(key, value); value.value = 44; "
                                           "console.error('record'); return resource.get(key).value;")}
    result.extend([
        Case("combined_transaction", [call("host.slot.combined('a', objectA)", 44),
                                      call("host.slot.read('a')", OBJECT, "objectA"),
                                      call("host.slot.value('a')", 44), observe("objectA.value", 44)],
             methods=combined, callback=callback, final_count=1),
        Case("callback_throw_after_writes", [call("host.slot.combined('a', objectA)", 0)],
             methods=combined, callback=callback + " throw 7;", final_count=1,
             completed=0, boundary="combined", error="number:7"),
    ])
    return result


def oracle(node, folder, case, js):
    driver, request = folder / "node.cjs", folder / "node-input.json"
    driver.write_text(mutations.NODE)
    request.write_text(json.dumps({"source": str(js.resolve()), "observations": list(case.expected())}))
    result = json.loads(host.run([node, str(driver), str(request)]).stdout)
    expected = {name: mutations.primitive(value) for name, value in case.expected().items()}
    if result["error"] != case.error or result["observations"] != expected:
        raise RuntimeError(f"{case.name}: Node returned {result}, expected {case.error}, {expected}")
    (folder / "node.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def interpreter(reference, folder, case, js):
    if case.error:
        return {"checked": False, "reason": "reference printer stops on thrown completion"}
    result = host.run([str(reference), str(js)])
    values = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
    expected = {name: text for name, value in case.expected().items()
                if (text := diagnostics.reference_value(value)) is not None}
    observed = {name: values.get(name) for name in expected}
    if observed != expected:
        raise RuntimeError(f"{case.name}: interpreter observations {observed} != {expected}")
    evidence = {"checked": True, "observations": observed}
    (folder / "interpreter.json").write_text(json.dumps(evidence, indent=2) + "\n")
    return evidence


def contract(opt, ir):
    observations = sorted(set(re.findall(r'ctjs.store_global "(trace\d+)"', ir.read_text())))
    return dict(host.manifest(opt, ir, undefined=("undefined",)),
                observations=observations + ["callCount"], initial_intrinsics=["Map", "Array"],
                realm_global_this=True)


def no_facts(report, label):
    diagnostics.no_facts(report, label)
    if any(value for key, value in report.items() if key.startswith("runtime_")):
        raise RuntimeError(f"{label}: invalid analysis retained runtime object facts: {report}")


def check_report(case, report, before, after):
    if not report["valid"] or report["full_host_contract_claimed"]:
        raise RuntimeError(f"{case.name}: wrong complete-host boundary: {report}")
    rows, completed = report["provider_calls"], case.summaries()
    calls = case.calls if case.calls is not None else completed + (3 if case.twice else 2)
    if (len(rows), report["summarized_provider_calls"], report["resolved_calls"]) != (completed, completed, calls):
        raise RuntimeError(f"{case.name}: wrong normal-return boundary: {report}")
    identities, provenance = {}, {}
    for row, step in zip(rows, case.provider_steps()):
        object_id = row["result_object_id"]
        if step.identity:
            if object_id <= 0 or row["result"]:
                raise RuntimeError(f"{case.name}: object return lacks a proved identity: {row}")
            if step.identity in identities and identities[step.identity] != object_id:
                raise RuntimeError(f"{case.name}: one live object changed identity: {row}")
            if any(name != step.identity and value == object_id for name, value in identities.items()):
                raise RuntimeError(f"{case.name}: distinct equal-field objects share an identity")
            identities[step.identity] = object_id
            gets = [operation for operation in row["operations"] if operation["member"] == "get"]
            if not gets or gets[-1]["result_object_id"] != object_id:
                raise RuntimeError(f"{case.name}: Map.get and its returned object disagree: {row}")
        elif object_id or mutations.attribute(row["result"]) != mutations.primitive(step.expected):
            raise RuntimeError(f"{case.name}: {step.expression} has the wrong summary result: {row}")
        method = re.search(r"\.([A-Za-z]+)\(", step.expression)[1]
        if not row["target"].startswith(method + "$"):
            raise RuntimeError(f"{case.name}: stale source method identity: {row}")
        for operation in row["object_operations"]:
            object_id = operation["object_id"]
            origin = operation["allocation_operation"], operation["invocation_operation"]
            if object_id <= 0 or min(origin) < 0:
                raise RuntimeError(f"{case.name}: object lost allocation/invocation provenance: {operation}")
            if object_id in provenance and provenance[object_id] != origin:
                raise RuntimeError(f"{case.name}: live object changed allocation/invocation provenance")
            provenance[object_id] = origin
            if operation["action"] not in {"read", "write", "delete", "retain"}:
                raise RuntimeError(f"{case.name}: unrecognized object effect: {operation}")
        if method in {"value", "label", "peek", "replace"}:
            member = "label" if method == "label" else "value"
            reads = [operation for operation in row["object_operations"]
                     if operation["action"] == "read" and operation["member"] == member]
            if not reads or mutations.attribute(reads[-1]["result"]) != mutations.primitive(step.expected):
                raise RuntimeError(f"{case.name}: own field read used stale object state: {row}")
        if method in {"replace", "removeField", "combined"}:
            action = "delete" if method == "removeField" else "write"
            writes = [operation for operation in row["object_operations"]
                      if operation["action"] == action and operation["member"] == "value"]
            if not writes:
                raise RuntimeError(f"{case.name}: completed object effect has no source proof: {row}")
    if case.boundary and not report["targets"][-1].startswith(case.boundary + "$"):
        raise RuntimeError(f"{case.name}: wrong next source callee: {report['targets']}")
    if report["provider_reads"] or report["selected_branches"]:
        raise RuntimeError(f"{case.name}: object following reused old empty state or erased observers")
    operations = [operation for row in rows for operation in row["operations"]]
    allocations = [allocation for row in rows for allocation in row["allocations"]]
    if report["runtime_provider_reads"] != sum(item["member"] in {"get", "has", "size"} for item in operations):
        raise RuntimeError(f"{case.name}: partial or missing committed Map reads")
    if report["runtime_provider_mutations"] != sum(item["member"] in {"set", "delete"} for item in operations):
        raise RuntimeError(f"{case.name}: partial or missing committed Map mutations")
    if report["runtime_nested_provider_allocations"] != len(allocations):
        raise RuntimeError(f"{case.name}: partial or missing nested allocations")
    for item in operations + allocations:
        if item["map_id"] <= 0 or item["allocation_operation"] < 0 or item["invocation_operation"] < 0:
            raise RuntimeError(f"{case.name}: missing Map allocation/invocation provenance")
    if case.name == "two_factories":
        if [row["factory_index"] for row in rows] != [0, 1, 0, 1, 0, 1, 0]:
            raise RuntimeError("separate factory invocations share object/Map state")
        if rows[0]["operations"][0]["map_id"] == rows[1]["operations"][0]["map_id"]:
            raise RuntimeError("separate factory Maps share one identity")
    elif any(row["factory_index"] for row in rows):
        raise RuntimeError(f"{case.name}: summary uses a different factory")
    if case.name == "nested_retention":
        if len(allocations) != 3 or len({item["map_id"] for item in allocations}) != 3:
            raise RuntimeError("repeated nested construction collapsed object-holding Maps")
        if len({item["allocation_operation"] for item in allocations}) != 1 or len(
                {item["invocation_operation"] for item in allocations}) != 3:
            raise RuntimeError("nested object retention lost allocation/invocation provenance")
    expected_callbacks = 1 if case.name == "combined_transaction" else 0
    if (report["runtime_provider_callbacks"], report["runtime_provider_global_writes"]) != (
            expected_callbacks, expected_callbacks):
        raise RuntimeError(f"{case.name}: partial callback/global transaction committed: {report}")
    if len(re.findall(r"\bctjs\.func\b", before)) != len(re.findall(r"\bctjs\.func\b", after)):
        raise RuntimeError(f"{case.name}: changed source function denominator")
    for operation in ("ctjs.construct", "ctjs.create_object", "ctjs.create_cell", "ctjs.create_closure",
                      "ctjs.set_property", "ctjs.get_property", "ctjs.delete_property", "ctjs.delete_named",
                      "ctjs.store_global", "scf.if"):
        if before.count(operation) != after.count(operation):
            raise RuntimeError(f"{case.name}: runtime {operation} changed")
    for name in set(METHODS | case.methods) | {"recorder"}:
        pattern = r"ctjs\.func @" + name + r"\$\d+\(.*?(?=\n  ctjs\.func |\n})"
        if re.findall(pattern, before, re.S) != re.findall(pattern, after, re.S):
            raise RuntimeError(f"{case.name}: reusable {name} body was specialized")


def controls(args, prepared):
    original = prepared["basic"]
    manifest = contract(args.opt, original)
    legacy, _, _ = mutations.specialize(args.opt, original, manifest, args.work / "legacy",
                                         options=mutations.OPTIONS)
    if legacy["summarized_provider_calls"] or legacy["resolved_calls"] != 2:
        raise RuntimeError("ordinary object following became implicit")
    for dependency in ("follow-publication=true", "follow-provider-reads=true", "follow-provider-mutations=true"):
        report, _, _ = mutations.specialize(args.opt, original, manifest,
            args.work / ("without-" + dependency.split("=")[0]), options=OPTIONS.replace(dependency, ""),
            success=False)
        no_facts(report, dependency)
    no_map, _, _ = mutations.specialize(args.opt, original, dict(manifest, initial_intrinsics=[]),
                                         args.work / "no-map", options=OPTIONS)
    if no_map["provider_calls"]:
        raise RuntimeError("undeclared Map provider authorized object retention")
    _, rewritten, _ = mutations.specialize(args.opt, original, manifest, args.work / "once", options=OPTIONS)
    stale, _, _ = mutations.specialize(args.opt, rewritten, manifest, args.work / "stale",
                                       options=OPTIONS, success=False)
    no_facts(stale, "stale source")
    rerun, _, _ = mutations.specialize(args.opt, rewritten, contract(args.opt, rewritten),
                                       args.work / "rerun", options=OPTIONS)
    if rerun["provider_calls"]:
        raise RuntimeError("fresh analysis reused object state from earlier invocations")
    unsafe = prepared["throw_after_writes"]
    forged = args.work / "forged.mlir"
    forged.write_text(unsafe.read_text().replace("module attributes {",
        'module attributes {ctnative.host_provider_objects = "committed", '
        'ctnative.host_object_identity = 99 : i64, ctnative.host_object_fields = true, ', 1))
    report, _, _ = mutations.specialize(args.opt, forged, contract(args.opt, unsafe),
                                         args.work / "forged-check", options=OPTIONS)
    if report["summarized_provider_calls"] != 1 or report["runtime_provider_mutations"] != 1:
        raise RuntimeError("forged object facts crossed a write-then-throw boundary")

    # The full threshold is discovered from completion, rather than duplicating
    # internal charges. Every incomplete run discards even prior summaries, and
    # the last insufficient limit has attempted Map, object and global effects.
    for name in ("nested_retention", "combined_transaction"):
        source = prepared[name]
        budget_manifest, budget_report = args.work / f"{name}-budget.json", args.work / f"{name}-budget.report.json"
        budget_manifest.write_text(json.dumps(contract(args.opt, source)))
        case = next(case for case in cases() if case.name == name)

        def run_budget(limit):
            budget_report.unlink(missing_ok=True)
            result = subprocess.run([args.opt, str(source),
                f"--ctnative-specialize-host-prefix=manifest={budget_manifest} output={budget_report} {case.options()} max-steps={limit}",
                "-o", "/dev/null"], capture_output=True, text=True, timeout=60)
            report = json.loads(budget_report.read_text())
            if not report["valid"]:
                if result.returncode != 1 or report["reason"] != "host prefix analysis work budget exhausted":
                    raise RuntimeError(f"{name}: unexpected work-limit refusal: {result.stderr}")
                no_facts(report, f"{name} budget {limit}")
            elif result.returncode or report["summarized_provider_calls"] != case.summaries():
                raise RuntimeError(f"{name}: complete work limit lost provider/object state: {report}")
            return report["valid"]

        low, high = 0, 100000
        if run_budget(low) or not run_budget(high):
            raise RuntimeError(f"{name}: unexpected work-limit bracket")
        while high - low > 1:
            middle = (high + low) // 2
            if run_budget(middle):
                high = middle
            else:
                low = middle
        if run_budget(high - 1) or not run_budget(high):
            raise RuntimeError(f"{name}: unstable object transaction completion threshold")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--translate")
    parser.add_argument("--opt")
    parser.add_argument("--node")
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--oracle-only", action="store_true")
    parser.add_argument("--case", action="append", choices=[case.name for case in cases()],
                        help="run a named source case without the cross-case controls")
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    if not args.oracle_only and (not args.translate or not args.opt):
        parser.error("compiler checks require --translate and --opt")
    args.work.mkdir(parents=True, exist_ok=True)
    node = diagnostics.node_executable(args)
    reference = None if args.oracle_only else args.reference or diagnostics.build_path(
        args.opt, "test/ctcompile-test-native-reference")
    evidence, prepared = {}, {}
    selected = [case for case in cases() if args.case is None or case.name in args.case]
    for case in selected:
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
        report, output, _ = mutations.specialize(args.opt, ir, contract(args.opt, ir),
                                                  folder / "checked", options=case.options())
        check_report(case, report, ir.read_text(), output.read_text())
        evidence[case.name]["provider"] = report
    if not args.oracle_only and args.case is None:
        controls(args, prepared)
    (args.work / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    mode = "Node source cases" if args.oracle_only else "source cases, live identities, own fields and rollback controls"
    print(f"host provider objects: {len(selected)} {mode} passed")


if __name__ == "__main__":
    main()
