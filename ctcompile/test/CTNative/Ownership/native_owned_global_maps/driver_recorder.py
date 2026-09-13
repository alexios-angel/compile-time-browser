"""Preserved Data recorder refusals and captured Map snapshot execution."""

import hashlib
import json

from .driver_common import (
    CONSTANT_GLOBAL_NODE,
    boundary,
    check_call_preservation,
    comparable_provenance,
    contract,
    forge_leaf_evidence,
    host,
    methods,
    owned,
)

# Keep the measured full Data subject, recorder and factory byte-for-byte.
EXACT_DATA = """var traceErrorCount = 0; var traceErrorMessage = 0;
var console = { error: function(message) {
    traceErrorCount = traceErrorCount + 1;
    traceErrorMessage = message === "Bootstrap doesn't allow more than one instance per element. Bound instance: bs.alert." ? 1 : 0;
} };
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    "use strict";
    const t = new Map,
        e = {
            set(e, i, n) {
                t.has(e) || t.set(e, new Map);
                const s = t.get(e);
                s.has(i) || 0 === s.size ? s.set(i, n) : console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(s.keys())[0]}.`)
            },
            get: (e, i) => t.has(e) && t.get(e).get(i) || null,
            remove(e, i) {
                if (!t.has(e)) return;
                const n = t.get(e);
                n.delete(i), 0 === n.size && t.delete(e)
            }
        };
    return e;
});
var element = {}; var other = {}; var absent = {};
var traceAbsentGet = host.slot.get(absent, "bs.alert") === null ? 1 : 0;
var traceAbsentRemove = host.slot.remove(absent, "bs.alert") === undefined ? 1 : 0;
host.slot.set(element, "bs.alert", 42);
host.slot.set(other, "bs.alert", 21);
var traceGet = host.slot.get(element, "bs.alert");
var traceOther = host.slot.get(other, "bs.alert");
host.slot.set(element, "bs.alert", 43);
var traceReplacement = host.slot.get(element, "bs.alert");
var traceWrongKeyGet = host.slot.get(element, "bs.missing") === null ? 1 : 0;
var traceWrongKeyRemove = host.slot.remove(element, "bs.missing") === undefined ? 1 : 0;
var traceAfterWrongKeyRemove = host.slot.get(element, "bs.alert");
host.slot.set(element, "bs.collapse", 99);
var traceRejectedKey = host.slot.get(element, "bs.collapse") === null ? 1 : 0;
var traceAfterRejectedSet = host.slot.get(element, "bs.alert");
host.slot.remove(element, "bs.alert");
var traceRemoved = host.slot.get(element, "bs.alert") === null ? 1 : 0;
var traceOtherAfterRemove = host.slot.get(other, "bs.alert");
var traceRemovedAgain = host.slot.remove(element, "bs.alert") === undefined ? 1 : 0;
var instance = {value: 64};
host.slot.set(element, "bs.collapse", instance);
var traceReinserted = host.slot.get(element, "bs.collapse").value;
var traceReinsertedIdentity = host.slot.get(element, "bs.collapse") === instance ? 1 : 0;
var traceOldKeyAfterReinsert = host.slot.get(element, "bs.alert") === null ? 1 : 0;
var traceOtherAfterReinsert = host.slot.get(other, "bs.alert");
"""
DATA_PREFIX = EXACT_DATA[:975]
SINGLE_CONFLICT = DATA_PREFIX + """var element = {};
host.slot.set(element, "bs.alert", 42);
host.slot.set(element, "bs.collapse", 99);
var traceKept = host.slot.get(element, "bs.alert") === 42 ? 1 : 0;
var traceRejected = host.slot.get(element, "bs.collapse") === null ? 1 : 0;
host.slot.remove(element, "bs.alert");
var traceGone = host.slot.get(element, "bs.alert") === null ? 1 : 0;
"""
REPEAT_CONFLICT = SINGLE_CONFLICT + """var traceFirstErrorCount = traceErrorCount;
var traceFirstErrorMessage = traceErrorMessage;
host.slot.set(element, "bs.collapse", 21);
var traceReinserted = host.slot.get(element, "bs.collapse") === 21 ? 1 : 0;
host.slot.set(element, "bs.alert", 64);
var traceSecondKept = host.slot.get(element, "bs.collapse") === 21 ? 1 : 0;
var traceSecondRejected = host.slot.get(element, "bs.alert") === null ? 1 : 0;
"""


def recorder_cases():
    single = dict(traceErrorCount=1, traceErrorMessage=1, traceKept=1, traceRejected=1, traceGone=1)
    cases = {
        "recorder_single": dict(source=SINGLE_CONFLICT, values=single),
        "recorder_repeat": dict(
            source=REPEAT_CONFLICT,
            values=dict(
                single,
                traceErrorCount=2,
                traceErrorMessage=0,
                traceFirstErrorCount=1,
                traceFirstErrorMessage=1,
                traceReinserted=1,
                traceSecondKept=1,
                traceSecondRejected=1,
            ),
        ),
        "recorder_exact_data": dict(
            source=EXACT_DATA,
            values=dict(
                traceAbsentGet=1,
                traceAbsentRemove=1,
                traceAfterRejectedSet=43,
                traceAfterWrongKeyRemove=43,
                traceErrorCount=1,
                traceErrorMessage=1,
                traceGet=42,
                traceOldKeyAfterReinsert=1,
                traceOther=21,
                traceOtherAfterReinsert=21,
                traceOtherAfterRemove=21,
                traceReinserted=64,
                traceReinsertedIdentity=1,
                traceRejectedKey=1,
                traceRemoved=1,
                traceRemovedAgain=1,
                traceReplacement=43,
                traceWrongKeyGet=1,
                traceWrongKeyRemove=1,
            ),
        ),
    }
    pins = {
        "recorder_single": (
            1327,
            "4ebf1cd4417eb0f54a3c154a1ba0a286eadfbba21526705808e731e3a85c4479",
        ),
        "recorder_repeat": (
            1733,
            "45d621b3cba163ca44d4e0d9e9fc793f953a505b3664a38b2890e9775caf4e36",
        ),
        "recorder_exact_data": (
            2522,
            "8359592c4d7ff4c78daf03c99a9b874ab48277a4ab17d9374b3043efd43b69b3",
        ),
    }
    assert hashlib.sha256(DATA_PREFIX.encode()).hexdigest() == (
        "c87ab961b1186537b86b5c96e35a5bec0905c97dbaa90c217f99c4175192efc9"
    )
    for name, row in cases.items():
        source = row["source"].encode()
        assert (len(source), hashlib.sha256(source).hexdigest()) == pins[name], name
        assert row["source"].startswith(DATA_PREFIX), name
        row.update(functions=7, admitted=False)
    return cases


SNAPSHOT_LENGTH = """var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map;
    return { get() {
        state.clear();
        state.set("z", 1);
        state.set("a", 2);
        return Array.from(state.keys()).length === 2 ? 42 : 0;
    } };
});
var trace = host.slot.get();
"""
SNAPSHOT_EMPTY = """var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map;
    return { get() {
        state.clear();
        const empty = Array.from(state.keys());
        state.set("z", 1);
        const present = Array.from(state.keys());
        return empty.length === 0 && empty[0] === void 0 &&
            present[1] === void 0 ? 42 : 0;
    } };
});
var trace = host.slot.get();
"""
SNAPSHOT_ORDER = """var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map;
    return { get() {
        state.clear();
        state.set("z", 1);
        state.set("a", 2);
        const original = Array.from(state.keys());
        state.set("z", 3);
        const replaced = Array.from(state.keys());
        state.delete("z");
        state.set("z", 4);
        const reordered = Array.from(state.keys());
        state.clear();
        return original.length === 2 && original[0] === "z" && original[1] === "a" &&
            replaced[0] === "z" && replaced[1] === "a" &&
            reordered[0] === "a" && reordered[1] === "z" ? 42 : 0;
    } };
});
var trace = host.slot.get();
"""

SNAPSHOT_NUMERIC = SNAPSHOT_ORDER.replace('"z"', "9").replace('"a"', "1")


def snapshot_cases():
    assert hashlib.sha256(SNAPSHOT_LENGTH.encode()).hexdigest() == (
        "99972772bb810ce45d36eb8868b8fd8754a1da3252233e6d440d6f7bffe3d334"
    )
    assert hashlib.sha256(SNAPSHOT_ORDER.encode()).hexdigest() == (
        "3d20153d9011f24e8533bd222d3d1b1bc9052d94ad8cb7b3953a784e846b47cb"
    )
    cases = {
        "captured_snapshot_length": dict(source=SNAPSHOT_LENGTH, admitted=True),
        "captured_snapshot_order": dict(source=SNAPSHOT_ORDER, admitted=True),
        "captured_snapshot_empty": dict(source=SNAPSHOT_EMPTY, admitted=True),
        "captured_snapshot_numeric": dict(source=SNAPSHOT_NUMERIC, admitted=True),
    }
    for name, replacement in (
        (
            "iterator_reuse",
            "const iterator = state.keys(); const original = Array.from(iterator); "
            "const duplicate = Array.from(iterator);",
        ),
        (
            "intervening_delete",
            'const iterator = state.keys(); state.delete("z"); '
            "const original = Array.from(iterator);",
        ),
    ):
        cases["captured_snapshot_" + name] = dict(
            source=SNAPSHOT_ORDER.replace(
                "const original = Array.from(state.keys());", replacement
            ),
            admitted=False,
            values={"trace": 42 if name == "iterator_reuse" else 0},
        )
    for name, index in (("fractional", "1.5"), ("nan", "0 / 0"), ("negative", "-1")):
        cases["captured_snapshot_" + name] = dict(
            source=SNAPSHOT_LENGTH.replace(
                "Array.from(state.keys()).length === 2 ? 42 : 0",
                f'Array.from(state.keys())[{index}] === "z" ? 42 : 0',
            ),
            admitted=False,
            values={"trace": 0},
        )
    for row in cases.values():
        row.setdefault("values", {"trace": 42})
        row["functions"] = 4
    return cases


def recorder_mutations():
    message = (
        "console.error(`Bootstrap doesn't allow more than one instance per element. "
        "Bound instance: ${Array.from(s.keys())[0]}.`)"
    )
    cases = recorder_cases()
    for name, source, old, replacement, values in (
        (
            "recorder_removed",
            SINGLE_CONFLICT,
            message,
            "void 0",
            dict(cases["recorder_single"]["values"], traceErrorCount=0, traceErrorMessage=0),
        ),
        (
            "recorder_stale_key",
            REPEAT_CONFLICT,
            "Array.from(s.keys())[0]",
            '"bs.alert"',
            dict(cases["recorder_repeat"]["values"], traceErrorMessage=1),
        ),
        (
            "snapshot_length_removed",
            SNAPSHOT_LENGTH,
            "Array.from(state.keys()).length",
            "0",
            {"trace": 0},
        ),
        (
            "snapshot_delete_removed",
            SNAPSHOT_ORDER,
            'state.delete("z");',
            "",
            {"trace": 0},
        ),
        (
            "snapshot_copy_reused",
            SNAPSHOT_ORDER,
            "const reordered = Array.from(state.keys());",
            "const reordered = original;",
            {"trace": 0},
        ),
        (
            "snapshot_empty_null",
            SNAPSHOT_EMPTY,
            "empty[0] === void 0",
            "empty[0] === null",
            {"trace": 0},
        ),
        (
            "snapshot_wrong_bounds",
            SNAPSHOT_EMPTY,
            "present[1] === void 0",
            "present[0] === void 0",
            {"trace": 0},
        ),
        (
            "snapshot_numeric_delete_removed",
            SNAPSHOT_NUMERIC,
            "state.delete(9);",
            "",
            {"trace": 0},
        ),
    ):
        assert source.count(old) == 1, (name, old)
        yield name, source.replace(old, replacement), values


def check_recorders(args, node, reference, compilers, nm):
    cases = {**recorder_cases(), **snapshot_cases()}
    observations = 0

    def observe(name, source, values):
        js = args.work / f"{name}-observed.js"
        js.write_text(source)
        expected = "".join(f"{key}={value}\n" for key, value in sorted(values.items()))
        result = host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), json.dumps(sorted(values))])
        if result.stdout != expected or result.stderr:
            raise RuntimeError(f"{name}: typed Node observation changed")
        result = host.run([str(reference), str(js)])
        kinds = f"({len(values)} number, 0 boolean, 0 string, 0 null, 0 undefined)"
        if result.stdout != expected or kinds not in result.stderr:
            raise RuntimeError(f"{name}: typed VM observation changed")
        return expected

    for name, row in cases.items():
        observe(name, row["source"], row["values"])
        observations += len(row["values"])
        if row["admitted"]:
            future = row["source"] + """var traceFuture = 1;
for (let i = 0; i < 1024; ++i) {
    if (host.slot.get() !== 42) { traceFuture = 0; }
}
"""
            observe(name + "-future", future, dict(row["values"], traceFuture=1))
            observations += len(row["values"]) + 1
    mutations = list(recorder_mutations())
    for name, source, values in mutations:
        observe(name, source, values)
        observations += len(values)

    for name, row in cases.items():
        # prepare appends one newline; compile the exact pinned source bytes.
        js, ir, functions = boundary.prepare(args, name, row["source"].removesuffix("\n"))
        assert js.read_bytes() == row["source"].encode(), name
        assert functions == row["functions"], name

        def observed_contract(subject, label):
            config = contract(args, subject, label)
            value = json.loads(config.read_text())
            value.update(initial_intrinsics=["Map", "Array"], observations=sorted(row["values"]))
            config.write_text(json.dumps(value, indent=2) + "\n")
            return config

        config = observed_contract(ir, name)
        for policy, options in (("default", ""), ("disabled", "optimize=false")):
            label = name + "-" + policy
            output = owned.lower(args, ir, label, config, options=options, cleanup=row["admitted"])
            methods.census(output, functions, label, admitted=functions if row["admitted"] else 0)
            if not row["admitted"]:
                check_call_preservation(ir.read_text(), output.read_text(), label)
            elif policy == "default":
                default = output
            elif output.read_text() != default.read_text():
                raise RuntimeError(f"{name}: snapshot proof depends on optimization policy")
            forged = args.work / f"{label}-forged.mlir"
            forged.write_text(forge_leaf_evidence(ir.read_text()))
            stale = methods.refused(
                args,
                forged,
                label + "-stale",
                config,
                options=options,
                reason="fingerprint mismatch",
                admitted=0,
            )
            check_call_preservation(forged.read_text(), stale.read_text(), label + "-stale")
            fresh = observed_contract(forged, label + "-fresh")
            checked = owned.lower(
                args,
                forged,
                label + "-fresh",
                fresh,
                options=options,
                cleanup=row["admitted"],
            )
            methods.census(checked, functions, label, admitted=functions if row["admitted"] else 0)
            if not row["admitted"]:
                check_call_preservation(forged.read_text(), checked.read_text(), label + "-fresh")
            elif comparable_provenance(
                host.run([args.translate, "--mlir-to-cpp", str(checked)]).stdout, forged
            ) != comparable_provenance(
                host.run([args.translate, "--mlir-to-cpp", str(output)]).stdout, ir
            ):
                raise RuntimeError(f"{label}: forged reports changed snapshot C++")
        if row["admitted"]:
            expected = "".join(f"{key}={value}\n" for key, value in sorted(row["values"].items()))
            owned.standalone(args, default, name, expected, compilers, nm)
            for mode in ("explicit", "deduced"):
                cpp = (args.work / f"{name}.{mode}.cpp").read_text()
                methods.lifetime(args, cpp, name, mode, expected, compilers[1])
    native = sum(row["admitted"] for row in cases.values())
    print(
        f"recorder boundary: {native} native snapshots, {len(cases) - native} refusals, "
        f"{observations} typed observations, {len(mutations)} distinguishing mutations"
    )
