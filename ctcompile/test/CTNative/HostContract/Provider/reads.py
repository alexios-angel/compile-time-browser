#!/usr/bin/env python3
"""Check that the specializer summarizes reads of a private, empty Map.

A factory's closure captures a `new Map`, and its `read` method calls `has`,
`get` or `size` on it. The specializer must summarize those reads without
editing the reusable method body. Effects that could change the Map, a
replaced Map provider, a missing publication option, an exhausted work budget
and forged metadata must leave no provider facts.
"""

import argparse
from pathlib import Path
import re

from CTNative.HostContract import prefix
from CTNative.harness import forge_module, prepare_js

host = prefix.host
OPTIONS = "follow-publication=true follow-provider-reads=true"


def source(body="return resource.has(key);", *, middle="", before="", twice=False, ending=None):
    """A program whose factory returns {read, next} over one private Map.

    `body` is the body of `read(key)`. By default the program ends by storing
    `host.slot.read('x')` in `trace`, running `middle`, then calling
    `host.slot.next()`; `ending` replaces that whole tail and `before` goes
    in front of it. `twice` calls the factory a second time for `first`.
    """
    calls = "first = factory(); host.slot = factory();" if twice else "host.slot = factory();"
    suffix = ending or ("var trace = host.slot.read('x'); " + middle + " host.slot.next();")
    return (
        "var host = {}; var alias; var first; var trace = 0; "
        + "(function(factory) { "
        + calls
        + " })(function() { const resource = new Map; "
        + "return {read: function read(key) { "
        + body
        + " }, "
        + "next: function next() { return 42; }}; }); "
        + before
        + suffix
        + "\n"
    )


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args(argv)
    args.work.mkdir(parents=True, exist_ok=True)
    sources = {
        "has": source(),
        "get": source("return resource.get(key);"),
        "size": source("return resource.size;"),
        "unselected_write": source(
            "if (resource.has(key)) { resource.set(key, 42); } return resource.get(key);"
        ),
        "observer": source(
            "return resource.get(key);",
            ending="var trace = host.slot.read('x') === undefined ? 1 : 0; host.slot.next();",
        ),
        "null_distinct": source(
            "return resource.get(key);",
            ending="var trace = host.slot.read('x') === null ? 1 : 0; if (trace) { host.slot.bad(); } else { host.slot.next(); }",
        ),
        "method_replaced": source(middle="host.slot.next = function replacement() { return 9; };"),
        "alias_replaced": source(
            middle="alias = host.slot; alias.next = function replacement() { return 9; };"
        ),
        "two_factories": source(
            twice=True, ending="var trace = first.read('x'); host.slot.read('x'); host.slot.next();"
        ),
        "captured_alias": source(
            twice=True,
            before="host.slot.read = first.read;",
            ending="var trace = host.slot.read('x'); host.slot.next();",
        ),
        "write_after_read": source("resource.has(key); resource.set(key, 42); return 0;"),
        "delete_after_read": source("resource.get(key); resource.delete(key); return 0;"),
        "unknown_after_read": source("resource.has(key); globalThis.foreign(); return 0;"),
        "throw_after_read": source("resource.has(key); throw 7;"),
        "own_override": source(
            "resource.has = function() { return true; }; return resource.has(key);"
        ),
        "resource_escape": source("resource.has(key); return resource;"),
        "method_escape": source("resource.has(key); return resource.has;"),
        "detached_receiver": source("const has = resource.has; return has(key);"),
        "argument_property": source("resource.has(key); return key.value;"),
        "unknown_between": source(middle="globalThis.foreign();"),
        "accessor_between": source(
            middle="Object.defineProperty(host.slot, 'next', {get: function() { return function() { return 7; }; }});"
        ),
        "prototype_changed": "Map.prototype.has = function() { return true; }; " + source(),
        "provider_changed": "Map = function() {}; " + source(),
    }
    positive = {
        "has",
        "get",
        "size",
        "unselected_write",
        "observer",
        "null_distinct",
        "method_replaced",
        "alias_replaced",
        "two_factories",
        "captured_alias",
    }
    prepared = {}
    for name, program in sources.items():
        js = args.work / f"{name}.js"
        ir = args.work / f"{name}.mlir"
        js.write_text(program)
        prepare_js(args, js, args.work / f"{name}.raw.mlir", ir)
        prepared[name] = ir
        contract = dict(
            host.manifest(args.opt, ir, undefined=("undefined",)),
            initial_intrinsics=["Map"],
            realm_global_this=True,
        )
        report, output, _ = prefix.specialize(
            args.opt,
            ir,
            contract,
            args.work / f"{name}-check",
            options=OPTIONS,
            success=not name.endswith("changed"),
        )
        if name.endswith("changed"):
            if report["valid"] or report["summarized_provider_calls"] or report["resolved_calls"]:
                raise RuntimeError(f"{name}: changed provider retained a proof")
            continue
        if not report["valid"] or report["full_host_contract_claimed"]:
            raise RuntimeError(f"{name}: incorrect complete-host boundary: {report}")
        if name in positive:
            # Expected counts: one summary per factory whose `read` is called,
            # one runtime read per Map read executed (unselected_write runs
            # `has` and `get`), and one resolved call per factory call and per
            # summary on top of a fixed one.
            summaries = 2 if name == "two_factories" else 1
            factories = 2 if name in {"two_factories", "captured_alias"} else 1
            read_count = 2 if name in {"two_factories", "unselected_write"} else 1
            if (
                report["summarized_provider_calls"],
                report["runtime_provider_reads"],
                report["resolved_calls"],
            ) != (summaries, read_count, factories + summaries + 1):
                raise RuntimeError(
                    f"{name}: read path did not advance to the next actual call: {report}"
                )
            indices = [entry["factory_index"] for entry in report["provider_reads"]]
            if indices != ([0, 1] if name == "two_factories" else [0]):
                raise RuntimeError(f"{name}: closure lost its actual factory invocation: {report}")
            expected_target = "replacement$" if name.endswith("replaced") else "next$"
            if not report["targets"][-1].startswith(expected_target):
                raise RuntimeError(f"{name}: next callee was frozen or read from an unexecuted arm")
            if report["selected_branches"]:
                raise RuntimeError(
                    f"{name}: source observation/method branches entered the rewrite plan"
                )
            result = report["provider_reads"][0]["result"]
            if name in {"get", "observer", "null_distinct"} and result != "#ctjs.undefined":
                raise RuntimeError(f"{name}: absent Map.get did not preserve undefined: {result}")
            before, after = ir.read_text(), output.read_text()
            for operation in (
                "scf.if",
                "ctjs.construct",
                "ctjs.create_cell",
                "ctjs.create_closure",
                "ctjs.set_property",
            ):
                if before.count(operation) != after.count(operation):
                    raise RuntimeError(f"{name}: runtime operation {operation} was removed")
            # SSA numbering is local to each function, so the read method's text
            # can be compared directly: a call-site summary must not change it.
            pattern = r"ctjs\.func @read\$\d+\(.*?(?=\n  ctjs\.func |\n})"
            if re.search(pattern, before, re.S)[0] != re.search(pattern, after, re.S)[0]:
                raise RuntimeError(f"{name}: reusable method body was specialized")
        elif name == "unknown_between":
            if report["summarized_provider_calls"] != 1 or report["resolved_calls"] != 2:
                raise RuntimeError("an unknown intervening effect was crossed")
        elif report["summarized_provider_calls"] or report["runtime_provider_reads"]:
            raise RuntimeError(f"{name}: unsupported path leaked partial read facts: {report}")

    original = prepared["has"]
    contract = dict(
        host.manifest(args.opt, original), initial_intrinsics=["Map"], realm_global_this=True
    )
    legacy, _, _ = prefix.specialize(
        args.opt, original, contract, args.work / "legacy", options="follow-publication=true"
    )
    if legacy["summarized_provider_calls"] or legacy["resolved_calls"] != 2:
        raise RuntimeError("provider traversal became implicit")
    invalid, _, _ = prefix.specialize(
        args.opt,
        original,
        contract,
        args.work / "no-publication",
        options="follow-provider-reads=true",
        success=False,
    )
    if invalid["valid"] or invalid["summarized_provider_calls"]:
        raise RuntimeError("provider traversal accepted a missing publication prerequisite")
    _, changed, _ = prefix.specialize(
        args.opt, original, contract, args.work / "once", options=OPTIONS
    )
    stale, _, _ = prefix.specialize(
        args.opt, changed, contract, args.work / "stale", options=OPTIONS, success=False
    )
    if stale["valid"] or stale["summarized_provider_calls"] or stale["resolved_calls"]:
        raise RuntimeError("stale source retained provider facts")
    for limit in (0, 200):
        limited, _, _ = prefix.specialize(
            args.opt,
            original,
            contract,
            args.work / f"budget-{limit}",
            options=OPTIONS + f" max-steps={limit}",
            success=False,
        )
        if (
            limited["valid"]
            or limited["summarized_provider_calls"]
            or limited["runtime_provider_reads"]
            or limited["resolved_calls"]
        ):
            raise RuntimeError("exhausted analysis retained partial provider facts")
    unsafe = prepared["write_after_read"]
    forged = args.work / "forged.mlir"
    forged.write_text(
        forge_module(
            unsafe.read_text(),
            "ctnative.host_empty_map = true, ctnative.host_provider_reads = 99 : i64",
        )
    )
    forged_contract = dict(
        host.manifest(args.opt, unsafe), initial_intrinsics=["Map"], realm_global_this=True
    )
    report, _, _ = prefix.specialize(
        args.opt, forged, forged_contract, args.work / "forged-check", options=OPTIONS
    )
    if report["summarized_provider_calls"] or report["runtime_provider_reads"]:
        raise RuntimeError("forged metadata replaced the source effect proof")
    uncontracted, _, _ = prefix.specialize(
        args.opt,
        original,
        host.manifest(args.opt, original),
        args.work / "no-provider",
        options=OPTIONS,
    )
    if uncontracted["summarized_provider_calls"]:
        raise RuntimeError("an undeclared provider supplied a Map read proof")
    print(
        "host provider reads: empty Map paths, per-invocation captures, live calls, runtime bodies and effect guards passed"
    )


def test_reads(lit):
    main(lit.args("--translate ctjs-translate --opt ctjs-opt --work %t"))


if __name__ == "__main__":
    main()
