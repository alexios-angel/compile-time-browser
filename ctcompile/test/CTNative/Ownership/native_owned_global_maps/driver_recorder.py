"""Preserved Data sources, recorder effects and captured Map snapshot execution."""

import hashlib
import json
import os
import re
import subprocess

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
    source_calls,
)

from .harness_objects import instrument_leaf_objects

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
# Separate invocation-proof probe; EXACT_DATA still has host/carrier boundaries.
ENTRY_OBJECTS = (
    DATA_PREFIX + """var element = {}; var other = {}; var absent = {}; var alias = element;
var instance = {value: 64};
host.slot.set(element, "bs.alert", instance);
host.slot.set(other, "bs.alert", 21);
var traceDistinct = host.slot.get(element, "bs.alert").value;
var traceAlias = host.slot.get(alias, "bs.alert").value;
var traceOther = host.slot.get(other, "bs.alert") === 21 ? 1 : 0;
var traceAbsent = host.slot.get(absent, "bs.alert") === null ? 1 : 0;
host.slot.remove(element, "bs.missing");
var traceWrongRemove = host.slot.get(element, "bs.alert").value;
host.slot.set(element, "bs.collapse", 99);
var traceConflict = host.slot.get(element, "bs.alert").value;
host.slot.remove(element, "bs.alert");
var traceRemoved = host.slot.get(element, "bs.alert") === null ? 1 : 0;
host.slot.set(alias, "bs.collapse", instance);
var traceReinserted = host.slot.get(element, "bs.collapse").value;
var traceIdentity = host.slot.get(alias, "bs.collapse") === instance ? 1 : 0;
var traceOtherAfter = host.slot.get(other, "bs.alert") === 21 ? 1 : 0;
"""
)


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
        "recorder_entry_objects": dict(
            source=ENTRY_OBJECTS,
            entry_objects=True,
            values=dict(
                traceDistinct=64,
                traceAlias=64,
                traceOther=1,
                traceAbsent=1,
                traceWrongRemove=64,
                traceConflict=64,
                traceRemoved=1,
                traceReinserted=64,
                traceIdentity=1,
                traceOtherAfter=1,
                traceErrorCount=1,
                traceErrorMessage=1,
            ),
        ),
    }
    # Keep the original empty-manifest refusal and measure the same bytes with
    # the existing fixed-undefined host contract. Ownership now completes;
    # native Map recognition still needs that fact at its own environment check.
    cases["recorder_exact_data_undefined"] = dict(
        cases["recorder_exact_data"], fixed_undefined=True
    )
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
        "recorder_entry_objects": (
            1999,
            "5dc98179c1a06bcac5b7136ce8a86d11690e7a7e93de8229e82488c09c52e030",
        ),
    }
    pins["recorder_exact_data_undefined"] = pins["recorder_exact_data"]
    assert hashlib.sha256(DATA_PREFIX.encode()).hexdigest() == (
        "c87ab961b1186537b86b5c96e35a5bec0905c97dbaa90c217f99c4175192efc9"
    )
    for name, row in cases.items():
        source = row["source"].encode()
        assert (len(source), hashlib.sha256(source).hexdigest()) == pins[name], name
        assert row["source"].startswith(DATA_PREFIX), name
        row.update(
            functions=7,
            admitted=name not in ("recorder_exact_data", "recorder_exact_data_undefined"),
            recorder=True,
        )
        if name != "recorder_single":
            row["max_steps"] = 1_000_000
    return cases


def recorder_refusals():
    original = recorder_cases()["recorder_single"]
    callback = "    traceErrorCount = traceErrorCount + 1;"
    replacement = "function(message) { traceErrorCount = 99; }"
    cases = {}
    for name, source, functions in (
        ("slot_replaced", SINGLE_CONFLICT + f"console.error = {replacement};\n", 8),
        ("global_replaced", SINGLE_CONFLICT + f"console = {{error: {replacement}}};\n", 8),
        (
            "alias_replaced",
            SINGLE_CONFLICT + f"var alias = console; alias.error = {replacement};\n",
            8,
        ),
        ("detached_callback", SINGLE_CONFLICT + "var detached = console.error;\n", 7),
        (
            "entry_conditional_call",
            SINGLE_CONFLICT
            + 'if (traceErrorCount === 0) { host.slot.remove(element, "bs.alert"); }\n',
            7,
        ),
        (
            "entry_conditional_store",
            SINGLE_CONFLICT + "if (traceErrorCount === 0) { traceExtra = 1; }\n",
            7,
        ),
        (
            "unsafe_sibling",
            SINGLE_CONFLICT.replace(
                "        e = {\n",
                "        e = {\n" f"            poison() {{ console.error = {replacement}; }},\n",
            ),
            9,
        ),
        (
            "inactive_message",
            SINGLE_CONFLICT.replace('bs.alert." ? 1 : 0;', 'bs.alert." ? 1 : "poison";'),
            7,
        ),
        (
            "before_initialization",
            "var traceBefore = traceErrorCount === undefined ? 1 : 0;\n" + SINGLE_CONFLICT,
            7,
        ),
    ):
        assert source != SINGLE_CONFLICT, name
        cases["recorder_" + name] = dict(original, source=source, functions=functions)
    for name, effect in (
        ("reentry", 'host.slot.remove(element, "bs.alert");'),
        ("inactive_count", 'traceErrorCount = "poison";'),
        ("inactive_unknown", "unknown(message);"),
        ("intrinsic_write", "Map = 0;"),
    ):
        cases["recorder_" + name] = dict(
            original,
            source=SINGLE_CONFLICT.replace(
                callback, f'    if (message === "never") {{ {effect} }}\n' + callback
            ),
        )
    cases["recorder_before_initialization"]["values"] = dict(original["values"], traceBefore=1)
    entry = recorder_cases()["recorder_entry_objects"]
    for name, source in (
        (
            "dynamic_snapshot_index",
            ENTRY_OBJECTS.replace(
                "Array.from(s.keys())[0]", "Array.from(s.keys())[traceErrorCount]"
            ),
        ),
        ("prototype_effect", ENTRY_OBJECTS + "instance.__proto__ = {};\n"),
    ):
        assert source != ENTRY_OBJECTS, name
        cases["recorder_entry_" + name] = dict(entry, source=source)
    for row in cases.values():
        # A completed source rejection must not be disguised as a budget cutoff.
        row.update(admitted=False, max_steps=1_000_000)
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
            "recorder_entry_remove_other",
            ENTRY_OBJECTS,
            'host.slot.remove(element, "bs.missing");',
            'host.slot.remove(other, "bs.alert");',
            dict(cases["recorder_entry_objects"]["values"], traceOtherAfter=0),
        ),
        (
            "recorder_entry_fresh_reinsert",
            ENTRY_OBJECTS,
            'host.slot.set(alias, "bs.collapse", instance);',
            'host.slot.set(alias, "bs.collapse", {value: 65});',
            dict(cases["recorder_entry_objects"]["values"], traceReinserted=65, traceIdentity=0),
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


# Separate measured template probes; the original recorder bodies above stay unchanged.
TEMPLATE_OUTER = """var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map;
    return {
        set(key) { state.set(key, 1); },
        get() {
            const message = `Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(state.keys())[0]}.`;
            return message === "Bootstrap doesn't allow more than one instance per element. Bound instance: bs.alert." ? 42 :
                message === "Bootstrap doesn't allow more than one instance per element. Bound instance: undefined." ? 43 :
                message === "Bootstrap doesn't allow more than one instance per element. Bound instance: ." ? 47 : 0;
        },
        clear() { state.clear(); }
    };
});
var traceEmpty = host.slot.get();
host.slot.set("");
var traceEmptyKey = host.slot.get();
host.slot.clear();
host.slot.set("bs.alert");
host.slot.set("bs.collapse");
var traceInsertionOrder = host.slot.get();
host.slot.clear();
host.slot.set("bs.collapse");
var traceReinserted = host.slot.get();
host.slot.clear();
host.slot.set("bs.alert");
var trace = host.slot.get();
"""
TEMPLATE_CHILD = """var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map;
    return {
        set(element, key) {
            state.has(element) || state.set(element, new Map);
            const child = state.get(element);
            child.set(key, 1);
        },
        get(element) {
            if (!state.has(element)) return 0;
            const child = state.get(element);
            const message = `Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(child.keys())[0]}.`;
            return message === "Bootstrap doesn't allow more than one instance per element. Bound instance: bs.alert." ? 42 : 0;
        },
        remove(element, key) {
            if (!state.has(element)) return;
            const child = state.get(element);
            child.delete(key);
            if (child.size === 0) state.delete(element);
        }
    };
});
var element = {};
host.slot.set(element, "bs.alert");
host.slot.set(element, "bs.collapse");
var traceInsertionOrder = host.slot.get(element);
host.slot.remove(element, "bs.alert");
var traceReordered = host.slot.get(element);
host.slot.remove(element, "bs.collapse");
var traceAbsent = host.slot.get(element);
host.slot.set(element, "bs.alert");
var trace = host.slot.get(element);
"""


def template_cases():
    cases = {
        "captured_template_outer": dict(
            source=TEMPLATE_OUTER,
            values=dict(
                traceEmpty=43, traceEmptyKey=47, traceInsertionOrder=42, traceReinserted=0, trace=42
            ),
            child=False,
            max_steps=1_000_000,
        ),
        "captured_template_child": dict(
            source=TEMPLATE_CHILD,
            values=dict(traceInsertionOrder=42, traceReordered=0, traceAbsent=0, trace=42),
            child=True,
        ),
    }
    for name, size, digest in (
        (
            "captured_template_outer",
            1108,
            "db9730efbaeb237a8a123801cb40ed79288162c99a39369b21a3fe4ecd14a81e",
        ),
        (
            "captured_template_child",
            1303,
            "e39ea88765761d962f82aefa4b100ef5f6b508ef7f45cfe7a32fc31c0a36bc86",
        ),
    ):
        source = cases[name]["source"].encode()
        assert (len(source), hashlib.sha256(source).hexdigest()) == (size, digest), name
        cases[name].update(functions=6, admitted=True)
    # Late calls poison the complete formal category; sibling stores separately
    # poison the Map family even when every observed setter actual is a String.
    for role, category, key in (
        ("outer", "number", "7"),
        ("outer", "object", "{}"),
        ("child", "null", "null"),
        ("child", "undefined", "void 0"),
    ):
        original = cases["captured_template_" + role]
        child = original["child"]
        prefix = "element, " if child else ""
        cases[f"captured_template_{role}_late_{category}"] = dict(
            original,
            source=original["source"] + f"host.slot.set({prefix}{key});\n",
            admitted=False,
            max_steps=1_000_000,
        )
        insertion = (
            f"        poison(element) {{ if (state.has(element)) state.get(element).set({key}, 1); }},\n"
            if child
            else f"        poison() {{ state.set({key}, 1); }},\n"
        )
        assert original["source"].count("    return {\n") == 1
        cases[f"captured_template_{role}_sibling_{category}"] = dict(
            original,
            source=original["source"].replace("    return {\n", "    return {\n" + insertion)
            + f"host.slot.poison({'element' if child else ''});\n",
            functions=7,
            admitted=False,
            max_steps=1_000_000,
        )
    return cases


def template_mutations():
    cases = template_cases()
    for name, role, old, replacement, values in (
        (
            "template_outer_stale_key",
            "outer",
            "Array.from(state.keys())[0]",
            '"bs.alert"',
            dict(
                traceEmpty=42,
                traceEmptyKey=42,
                traceInsertionOrder=42,
                traceReinserted=42,
                trace=42,
            ),
        ),
        (
            "template_outer_absent_is_null",
            "outer",
            "Bound instance: undefined.",
            "Bound instance: null.",
            dict(cases["captured_template_outer"]["values"], traceEmpty=0),
        ),
        (
            "template_child_stale_key",
            "child",
            "Array.from(child.keys())[0]",
            '"bs.alert"',
            dict(cases["captured_template_child"]["values"], traceReordered=42),
        ),
    ):
        source = cases["captured_template_" + role]["source"]
        assert source.count(old) == 1, (name, old)
        yield name, source.replace(old, replacement), values


def template_future_body(child):
    if child:
        return """    remove(element, "bs.alert");
    if (get(element) !== 0) { traceFuture = 0; }
    set(element, "");
    set(element, "bs.alert");
    if (get(element) !== 0) { traceFuture = 0; }
    remove(element, "");
    if (get(element) !== 42) { traceFuture = 0; }
    let key = "future-" + i + "-long-owned-template-key";
    const original = key;
    set(element, key);
    key = "";
    remove(element, "bs.alert");
    if (get(element) !== 0) { traceFuture = 0; }
    set(element, "bs.alert");
    if (get(element) !== 0) { traceFuture = 0; }
    remove(element, original);
    if (get(element) !== 42) { traceFuture = 0; }
    remove(element, "bs.alert");
    if (get(element) !== 0) { traceFuture = 0; }
    key = "bs.alert";
    set(element, key);
    key = "changed caller storage";
    if (get(element) !== 42) { traceFuture = 0; }
"""
    return """    clear();
    if (get() !== 43) { traceFuture = 0; }
    set("");
    if (get() !== 47) { traceFuture = 0; }
    clear();
    let key = "bs.alert";
    set(key);
    key = "changed caller storage";
    set("bs.collapse");
    if (get() !== 42) { traceFuture = 0; }
    clear();
    key = "future-" + i + "-long-owned-template-key";
    set(key);
    key = "";
    set("bs.alert");
    if (get() !== 0) { traceFuture = 0; }
    clear();
    set("bs.alert");
    if (get() !== 42) { traceFuture = 0; }
"""


def template_future_source(child):
    method = "remove" if child else "clear"
    return (
        "var traceFuture = 1;\n(function() {\n"
        "const set = host.slot.set, get = host.slot.get;\n"
        f"const {method} = host.slot.{method};\n"
        "for (let i = 0; i < 1024; ++i) {\n" + template_future_body(child) + "}\n})();\n"
    )


def template_lifetime_cpp(cpp, child):
    # The existing observer records weak allocation witnesses without changing
    # the generated Map implementation or its ownership.
    changed = instrument_leaf_objects(cpp, allocations=1 if child else 0)
    argument = "element" if child else ""
    method = "remove" if child else "clear"
    changed += f"""
int main() {{
    if (ctnative_test_entry() != 0 || ctn_test_maps.empty()) {{ return 100; }}
    auto first = g_host;
    auto table = first->slot;
    auto get = table->m_get;
    auto set = table->m_set;
    auto {method} = table->m_{method};
    std::weak_ptr first_lifetime = first;
    std::weak_ptr table_lifetime = table;
"""
    if child:
        changed += "    auto element = g_element;\n    g_element.reset();\n"
    changed += f"""    g_host.reset();
    if (first->slot->m_get({argument}) != 42 || table->m_get({argument}) != 42 ||
        get({argument}) != 42) {{ return 101; }}
    first.reset();
    if (!first_lifetime.expired() || table_lifetime.expired() ||
        table->m_get({argument}) != 42) {{ return 102; }}
    table.reset();
    if (!table_lifetime.expired() || ctn_test_maps[0].expired()) {{ return 103; }}
    for (int i = 0; i < 1024; ++i) {{
"""
    changed += (
        template_future_body(child)
        .replace("!==", "!=")
        .replace("traceFuture = 0;", "return 104;")
        .replace("let key =", "std::string key =")
        .replace("const original =", "const std::string original =")
        .replace('"future-" + i', '"future-" + std::to_string(i)')
    )
    changed += f"""    }}
    const auto next = ctn_test_maps.size();
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() <= next ||
        ctn_test_maps[0].lock() == ctn_test_maps[next].lock() ||
        get({argument}) != 42) {{ return 105; }}
"""
    if child:
        changed += """    if (element == g_element || get(g_element) != 0 ||
        g_host->slot->m_get(g_element) != 42 || g_host->slot->m_get(element) != 0) {
        return 106;
    }
"""
    else:
        changed += "    if (g_host->slot->m_get() != 42) { return 106; }\n"
    changed += f"""    get = {{}};
    if (ctn_test_maps[0].expired()) {{ return 107; }}
    set = {{}};
    if (ctn_test_maps[0].expired()) {{ return 108; }}
    {method} = {{}};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[next].expired()) {{ return 109; }}
    g_host.reset();
"""
    if child:
        changed += "    element.reset();\n    g_element.reset();\n"
    return changed + """    for (const auto & map : ctn_test_maps) {
        if (!map.expired()) { return 110; }
    }
    for (const auto & object : ctn_test_objects) {
        if (!object.expired()) { return 111; }
    }
    return 0;
}
"""


def recorder_future_body(entry_objects=False):
    body = """    remove(element, "bs.alert");
    remove(element, "bs.collapse");
    const before = traceErrorCount;
    let key = "bs.alert";
    set(element, key, 42);
    key = "changed caller storage";
    set(element, "bs.alert", 43);
    if (get(element, "bs.alert") !== 43 || traceErrorCount !== before) { traceFuture = 0; }
    set(element, "bs.collapse", 99);
    if (get(element, "bs.alert") !== 43 || get(element, "bs.collapse") !== null ||
        traceErrorCount !== before + 1 || traceErrorMessage !== 1) { traceFuture = 0; }
    remove(element, "bs.alert");
    if (get(element, "bs.alert") !== null) { traceFuture = 0; }
    key = "future-" + i + "-long-owned-recorder-key";
    const original = key;
    set(element, key, 21);
    key = "";
    set(element, "bs.alert", 64);
    if (get(element, original) !== 21 || get(element, "bs.alert") !== null ||
        traceErrorCount !== before + 2 || traceErrorMessage !== 0) { traceFuture = 0; }
    remove(element, original);
    if (get(element, original) !== null) { traceFuture = 0; }
"""
    if entry_objects:
        body += """    const payload = {value: i + 0.5};
    const alias = payload;
    const other = {}, absent = {};
    set(element, "bs.alert", payload);
    set(other, "bs.alert", 21);
    const saved = get(element, "bs.alert");
    payload.value = i + 2;
    if (saved !== alias || saved.value !== i + 2 ||
        get(other, "bs.alert") !== 21 || get(absent, "bs.alert") !== null) { traceFuture = 0; }
    remove(element, "bs.missing"); remove(absent, "bs.alert");
    if (get(element, "bs.alert") !== payload || get(other, "bs.alert") !== 21 ||
        traceErrorCount !== before + 2) { traceFuture = 0; }
    set(element, "bs.alert", 43);
    if (get(element, "bs.alert") !== 43 || saved.value !== i + 2) { traceFuture = 0; }
    remove(element, "bs.alert");
    if (get(element, "bs.alert") !== null || saved !== payload) { traceFuture = 0; }
    set(element, "bs.collapse", payload);
    if (get(element, "bs.collapse") !== payload ||
        get(element, "bs.alert") !== null || get(other, "bs.alert") !== 21) { traceFuture = 0; }
    remove(element, "bs.collapse"); remove(other, "bs.alert");
"""
    return body


def recorder_future_source(entry_objects=False):
    return (
        "var traceFuture = 1;\n(function(element) {\n"
        "const set = host.slot.set, get = host.slot.get, remove = host.slot.remove;\n"
        "host = {};\nfor (let i = 0; i < 1024; ++i) {\n"
        + recorder_future_body(entry_objects)
        + "}\n})(element);\n"
    )


def recorder_lifetime_cpp(cpp, values, entry_objects=False):
    changed = instrument_leaf_objects(cpp, allocations=4 if entry_objects else 1)
    equal = "object_strict_equal" if entry_objects else "scalar_strict_equal"
    body = re.sub(
        r"(get\((?:element|other|absent), [^)]+\)) !== (null|[0-9]+|payload)",
        lambda match: "!ctnative::"
        + equal
        + "("
        + match[1]
        + ", "
        + (
            "ctnative::nullable_scalar::null()"
            if match[2] == "null"
            else match[2] if match[2] == "payload" else match[2] + ".0"
        )
        + ")",
        recorder_future_body(entry_objects),
    )
    if entry_objects:
        body = (
            body.replace(
                "const payload = {value: i + 0.5};",
                "auto payload = std::make_shared<ctnative::identity_object>();\n"
                "    payload->field_76616c7565 = ctnative::nullable_scalar{i + 0.5};\n"
                "    std::weak_ptr payload_lifetime = payload;",
            )
            .replace("const alias = payload;", "auto alias = payload;")
            .replace(
                "const other = {}, absent = {};",
                "auto other = std::make_shared<ctnative::identity_object>();\n"
                "    auto absent = std::make_shared<ctnative::identity_object>();",
            )
            .replace("const saved =", "auto saved =")
            .replace("saved !== alias", "!ctnative::object_strict_equal(saved, alias)")
            .replace("saved !== payload", "!ctnative::object_strict_equal(saved, payload)")
            .replace("saved.value", "saved.object->field_76616c7565.value")
            .replace("payload.value = i + 2;", "payload->field_76616c7565.value = i + 2;")
        )
        body += """    payload.reset(); alias.reset();
    if (payload_lifetime.expired() || saved.object->field_76616c7565.value != i + 2) { return 138; }
    saved = {};
    if (!payload_lifetime.expired()) { return 139; }
"""
    body = (
        body.replace("!==", "!=")
        .replace("traceFuture = 0;", "return 124;")
        .replace("const before =", "const auto before =")
        .replace("let key =", "std::string key =")
        .replace("const original =", "const std::string original =")
        .replace('"future-" + i', '"future-" + std::to_string(i)')
    )
    for binding in ("traceErrorCount", "traceErrorMessage"):
        body = body.replace(binding, f"ctnative::global_number(g_{binding})")
    changed += (
        """
int main() {
    if (ctnative_test_entry() != 0 || ctn_test_maps.empty()) { return 120; }
    static_assert(std::is_same_v<decltype(g_traceErrorCount), ctnative::nullable_scalar>);
    static_assert(std::is_same_v<decltype(g_traceErrorMessage), ctnative::nullable_scalar>);
    std::weak_ptr recorder_owner_lifetime = g_console;
    auto error = g_console->error;
    auto first = g_host;
    auto table = first->slot;
    auto get = table->m_get;
    auto set = table->m_set;
    auto remove = table->m_remove;
    auto element = g_element;
    std::weak_ptr first_lifetime = first;
    std::weak_ptr table_lifetime = table;
    std::weak_ptr element_lifetime = element;
    const auto initial_count = ctnative::global_number(g_traceErrorCount);
    g_host.reset(); g_element.reset();
    first.reset();
    if (!first_lifetime.expired() || table_lifetime.expired()) { return 121; }
    table.reset();
    if (!table_lifetime.expired() || ctn_test_maps[0].expired()) { return 122; }
    for (int i = 0; i < 1024; ++i) {
"""
        + body
        + """    }
    if (ctnative::global_number(g_traceErrorCount) != initial_count + 2048 ||
        ctnative::global_number(g_traceErrorMessage) != 0) { return 125; }
"""
    )
    for binding, value in sorted(values.items()):
        if binding not in {"traceErrorCount", "traceErrorMessage"}:
            changed += (
                f"    if (ctnative::global_number(g_{binding}) != {value}) {{ return 126; }}\n"
            )
    changed += """    set(element, "bs.alert", 42);
    const auto next = ctn_test_maps.size();
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() <= next || element == g_element ||
        !recorder_owner_lifetime.expired() ||
        ctn_test_maps[0].lock() == ctn_test_maps[next].lock() ||
        ctnative::global_number(get(element, "bs.alert")) != 42 ||
        !ctnative::scalar_strict_equal(get(g_element, "bs.alert"), ctnative::nullable_scalar::null()) ||
        !ctnative::scalar_strict_equal(g_host->slot->m_get(element, "bs.alert"),
                                      ctnative::nullable_scalar::null())) { return 127; }
    // Old and new callable families must both use the current scalar globals.
    set(element, "bs.collapse", 99);
    if (ctnative::global_number(g_traceErrorCount) != initial_count + 1 ||
        ctnative::global_number(g_traceErrorMessage) != 1) { return 128; }
    g_host->slot->m_remove(g_element, "bs.collapse");
    g_host->slot->m_set(g_element, "bs.alert", 7);
    g_host->slot->m_set(g_element, "bs.collapse", 9);
    if (ctnative::global_number(g_traceErrorCount) != initial_count + 2 ||
        ctnative::global_number(g_traceErrorMessage) != 1) { return 129; }
    element.reset();
    get = {};
    if (ctn_test_maps[0].expired() || element_lifetime.expired()) { return 130; }
    set = {};
    if (ctn_test_maps[0].expired() || element_lifetime.expired()) { return 131; }
    remove = {};
    if (!ctn_test_maps[0].expired() || !element_lifetime.expired() ||
        ctn_test_maps[next].expired()) { return 132; }
    g_host.reset(); g_element.reset();
    for (const auto & map : ctn_test_maps) {
        if (!map.expired()) { return 133; }
    }
    for (const auto & object : ctn_test_objects) {
        if (!object.expired()) { return 134; }
    }
    std::weak_ptr current_recorder_owner = g_console;
    auto current_error = g_console->error;
    g_console.reset();
    if (!current_recorder_owner.expired()) { return 135; }
    // Both detached closures outlive their owners and update the current globals.
    error("Bootstrap doesn't allow more than one instance per element. Bound instance: bs.alert.");
    if (ctnative::global_number(g_traceErrorCount) != initial_count + 3 ||
        ctnative::global_number(g_traceErrorMessage) != 1) { return 136; }
    error = {};
    current_error("a different future message");
    if (ctnative::global_number(g_traceErrorCount) != initial_count + 4 ||
        ctnative::global_number(g_traceErrorMessage) != 0) { return 137; }
    current_error = {};
    return 0;
}
"""
    if entry_objects:
        changed = (
            changed.replace(
                "g_host.reset(); g_element.reset();",
                "g_host.reset(); g_element.reset(); g_other.reset(); g_absent.reset();\n"
                "    g_alias.reset(); g_instance.reset();",
            )
            .replace("ctnative::scalar_strict_equal(get(", "ctnative::object_strict_equal(get(")
            .replace(
                "ctnative::scalar_strict_equal(g_host->slot->m_get(",
                "ctnative::object_strict_equal(g_host->slot->m_get(",
            )
            .replace(
                'ctnative::global_number(get(element, "bs.alert")) != 42',
                '!ctnative::object_strict_equal(get(element, "bs.alert"), js_num{42})',
            )
        )
        changed = re.sub(
            r"\b(set|m_set)\(([^;\n]+), ([0-9]+)\)",
            r"\1(\2, js_num{\3})",
            changed,
        )
        signature = (
            "std::function<ctnative::object_value("
            "std::shared_ptr<ctnative::identity_object>, std::string)>"
        )
        changed = changed.replace(
            "auto get = table->m_get;",
            "auto get = table->m_get;\n"
            f"    static_assert(std::is_same_v<decltype(get), {signature}>);",
        )
    return changed


def template_lifetime(
    args, cpp, name, mode, expected, compilers, nm, child, recorder_values=None, entry_objects=False
):
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    source.write_text(
        recorder_lifetime_cpp(cpp, recorder_values, entry_objects)
        if recorder_values is not None
        else template_lifetime_cpp(cpp, child)
    )
    for index, compiler in enumerate(compilers):
        binary = source.with_suffix(f".{index}").resolve()
        host.run([compiler, *owned.FLAGS, str(source), "-o", str(binary)])
        if owned.VM.search(host.run([nm, "-C", str(binary)]).stdout):
            raise RuntimeError(f"{name}/{mode}: linked VM symbols in template lifetime")
        if host.run([str(binary)]).stdout != expected * 2:
            raise RuntimeError(f"{name}/{mode}: future template lifetime mismatch")
    binary = source.with_suffix(".sanitized").resolve()
    host.run(
        [
            compilers[1],
            *owned.FLAGS,
            "-O1",
            "-g",
            "-fno-omit-frame-pointer",
            "-fsanitize=address,undefined",
            "-fsanitize-address-use-after-scope",
            str(source),
            "-o",
            str(binary),
        ]
    )
    result = subprocess.run(
        [str(binary)],
        capture_output=True,
        text=True,
        timeout=60,
        env=dict(
            os.environ,
            ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
            UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
        ),
    )
    if result.returncode or result.stdout != expected * 2 or result.stderr:
        raise RuntimeError(
            f"{name}/{mode}: sanitized template lifetime failed\n{result.stdout}{result.stderr}"
        )


def check_recorders(args, node, reference, compilers, nm):
    cases = {**recorder_cases(), **recorder_refusals(), **snapshot_cases(), **template_cases()}
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
            values = dict(row["values"], traceFuture=1)
            future = row["source"] + (
                recorder_future_source(row.get("entry_objects", False))
                if row.get("recorder")
                else (
                    template_future_source(row["child"])
                    if "child" in row
                    else """var traceFuture = 1;
for (let i = 0; i < 1024; ++i) {
    if (host.slot.get() !== 42) { traceFuture = 0; }
}
"""
                )
            )
            if row.get("recorder"):
                values.update(traceErrorCount=values["traceErrorCount"] + 2048, traceErrorMessage=0)
            observe(name + "-future", future, values)
            observations += len(row["values"]) + 1
    mutations = [*recorder_mutations(), *template_mutations()]
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
            value.update(
                initial_intrinsics=["Map", "Array"],
                observations=sorted(row["values"]),
                undefined_bindings=["undefined"] if row.get("fixed_undefined") else [],
            )
            config.write_text(json.dumps(value, indent=2) + "\n")
            return config

        config = observed_contract(ir, name)
        for policy, options in (("default", ""), ("disabled", "optimize=false")):
            label = name + "-" + policy
            if row.get("recorder") and row["admitted"]:
                for steps in (0, 32):
                    refused = methods.refused(
                        args,
                        ir,
                        label + f"-budget-{steps}",
                        config,
                        options=options + f" host-max-steps={steps}",
                        reason="budget",
                        admitted=0,
                    )
                    check_call_preservation(ir.read_text(), refused.read_text(), label)
            # Keep exact sources; the longer outer probe needs this budget,
            # and unsafe-key controls must finish their semantic refusal.
            if "max_steps" in row:
                options += f" host-max-steps={row['max_steps']}"
            output = owned.lower(args, ir, label, config, options=options, cleanup=row["admitted"])
            methods.census(output, functions, label, admitted=functions if row["admitted"] else 0)
            if not row["admitted"]:
                if not row.get("fixed_undefined"):
                    check_call_preservation(ir.read_text(), output.read_text(), label)
                if (
                    "child" in row or row.get("recorder")
                ) and "budget exhausted" in output.read_text():
                    raise RuntimeError(f"{label}: source refusal exhausted its proof budget")
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
            if row.get("fixed_undefined"):
                # Successful ownership prepares calls even when native emission
                # refuses. Preserve every call and the exact entry method order.
                targets = {"set": "fn$4", "get": "fn$5", "remove": "fn$6"}
                expected = [
                    targets[method]
                    for method in re.findall(r"host\.slot\.(set|get|remove)\(", row["source"])
                ]
                for subject in (output, checked):
                    text = subject.read_text()
                    calls = re.findall(
                        r"ctjs\.call_direct @([-\w.$]+)\([^\n]+\) "
                        r"\{[^}\n]*ctnative\.stored_call = 1 : i32",
                        text,
                    )
                    if (
                        "ctnative.host_owner_proved = true" not in text
                        or len(source_calls(text)) != len(source_calls(ir.read_text()))
                        or calls != expected
                    ):
                        raise RuntimeError(f"{label}: declared undefined lost owned source calls")
                check_call_preservation(output.read_text(), checked.read_text(), label + "-fresh")
            elif not row["admitted"]:
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
                if row.get("entry_objects"):
                    entry = re.search(r"\bmain\(\)\s*\{(.*?)^\}", cpp, re.M | re.S)
                    if not entry:
                        raise RuntimeError(f"{name}/{mode}: missing original entry")
                    methods_by_value = dict(
                        re.findall(
                            r"(\w+)\s*=\s*ctnative::method_get<&[^>\n]+::m_(\w+)>\(", entry[1]
                        )
                    )
                    calls = re.findall(r"ctnative::invoke_callable\((\w+)([^;\n]*)\);", entry[1])
                    sequence = [
                        methods_by_value[callee]
                        for callee, _ in calls
                        if callee in methods_by_value
                    ]
                    if (
                        sequence != re.findall(r"host\.slot\.(set|get|remove)\(", row["source"])
                        or len(re.findall(r"ctnative::object_get_field_76616c7565\(", cpp)) != 5
                    ):
                        raise RuntimeError(
                            f"{name}/{mode}: changed live entry calls or field reads"
                        )
                if "child" in row or row.get("recorder"):
                    template_lifetime(
                        args,
                        cpp,
                        name,
                        mode,
                        expected,
                        compilers,
                        nm,
                        row.get("child", False),
                        recorder_values=row["values"] if row.get("recorder") else None,
                        entry_objects=row.get("entry_objects", False),
                    )
                else:
                    methods.lifetime(args, cpp, name, mode, expected, compilers[1])
    native = sum(row["admitted"] for row in cases.values())
    print(
        f"recorder boundary: {native} native programs, {len(cases) - native} refusals, "
        f"{observations} typed observations, {len(mutations)} distinguishing mutations"
    )
