"""Captured outer Maps retain fresh and reused child Map owners."""

from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
import re
import subprocess

from .driver_common import (
    CONSTANT_GLOBAL_NODE,
    boundary,
    check_budgets,
    check_call_preservation,
    comparable_provenance,
    contract,
    forge_leaf_evidence,
    host,
    methods,
    owned,
    source_calls,
)
from .harness_objects import (
    instrument_leaf_objects,
    object_argument_cases,
    object_payload_lifetime_cpp,
)


def nested_map_cases():
    # These are bounded ownership witnesses, not Bootstrap's exact Data source.
    base = """var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const t = new Map;
    return { get(value) {
        const child = new Map;
        child.set('value', 0);
        t.set(1, child);
        const saved = t.get(1);
        child.set('value', value);
        return saved.get('value');
    } };
});
var trace = host.slot.get(41);
"""
    rows = {}

    def add(
        name,
        source,
        calls,
        *,
        admitted=False,
        functions=4,
        children=1,
        retained=False,
        reused=False,
        separate=False,
        repeated=False,
        previous=False,
        dynamic=False,
        owner=None,
    ):
        rows["nested_map_" + name] = dict(
            source=source,
            functions=functions,
            calls=calls,
            sha256=hashlib.sha256(source.encode()).hexdigest(),
            admitted=admitted,
            children=children,
            retained=retained,
            reused=reused,
            separate=separate,
            repeated=repeated,
            previous=previous,
            dynamic=dynamic,
            owner=admitted if owner is None else owner,
            expected_trace=41,
        )

    add("retained", base, 8, admitted=True, retained=True)
    add(
        "saved_delete",
        base.replace(
            "        child.set('value', value);",
            "        t.delete(1);\n        child.set('value', value);",
        ),
        9,
        admitted=True,
    )
    distinct = base.replace(
        """        const child = new Map;
        child.set('value', 0);
        t.set(1, child);
        const saved = t.get(1);
        child.set('value', value);
        return saved.get('value');""",
        """        const first = new Map, second = new Map;
        first.set('value', 17);
        second.set('value', 23);
        t.set(1, first);
        t.set(2, second);
        const saved = t.get(1), other = t.get(2);
        t.set(1, second);
        t.delete(2);
        t.clear();
        saved.set('value', value);
        return other.get('value') === 23 ? saved.get('value') : 0;""",
    )
    add("distinct_saved", distinct, 15, admitted=True, children=2)
    add(
        "distinct_absence",
        distinct.replace("second.set('value', 23);", "second.set('other', 23);").replace(
            "other.get('value') === 23", "other.get('value') === void 0"
        ),
        15,
        admitted=True,
        children=2,
    )
    add(
        "unproved_key",
        base.replace("get(value)", "get(value, key)")
        .replace("const saved = t.get(1);", "const saved = t.get(key);")
        .replace("host.slot.get(41)", "host.slot.get(41, 1)"),
        8,
    )
    for action in ("delete(1)", "clear()"):
        add(
            "stale_" + action.split("(")[0],
            base.replace("get(value)", "get(value, flag)")
            .replace(
                "const saved = t.get(1);",
                "if (flag) { t." + action + "; }\n        const saved = t.get(1);",
            )
            .replace("host.slot.get(41)", "host.slot.get(41, false)"),
            9,
        )
    for name, payload in (
        ("self_cycle", "child"),
        ("outer_cycle", "t"),
        ("child_owns_map", "new Map"),
    ):
        add(
            name,
            base.replace(
                "        child.set('value', value);",
                "        child.set('edge', " + payload + ");\n        child.set('value', value);",
            ),
            9,
        )
    add(
        "child_as_key",
        base.replace(
            "        const saved = t.get(1);",
            "        t.set(child, child);\n        const saved = t.get(1);",
        ),
        9,
    )
    add(
        "nonstandard_child", base.replace("const child = new Map;", "const child = new Map([]);"), 8
    )
    add(
        "foreign_consumer",
        "function consume(item) { return 0; }\n"
        + base.replace(
            "        const saved = t.get(1);",
            "        consume(child);\n        const saved = t.get(1);",
        ),
        9,
        functions=5,
    )
    add(
        "returned_child",
        base.replace("return saved.get('value');", "return saved;").replace(
            "host.slot.get(41);", "host.slot.get(41).get('value');"
        ),
        8,
    )
    add(
        "conditional_initialize",
        base.replace(
            """        const child = new Map;
        child.set('value', 0);
        t.set(1, child);""",
            "        t.has(1) || t.set(1, new Map);",
        ).replace("child.set('value', value);", "saved.set('value', value);"),
        8,
        admitted=True,
        retained=True,
        reused=True,
    )
    add(
        "cross_invocation",
        """var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const t = new Map;
    return {
        set(value) { const child = new Map; child.set('value', value); t.set(1, child); return value; },
        get() { if (t.has(1)) { return t.get(1).get('value'); } return 0; }
    };
});
host.slot.set(41);
var trace = host.slot.get();
""",
        9,
        functions=5,
        admitted=True,
        retained=True,
        separate=True,
    )
    for name, digest in {
        "conditional_initialize": "74539aebaf2ec85ee44e45f9ea5fc4c7e2b37523bb0224507d6cdd171acffe26",
        "cross_invocation": "369d7ceafb8d173d004395715e833aa9ec9f94e494c742e3ddab6a405d735a28",
    }.items():
        if rows["nested_map_" + name]["sha256"] != digest:
            raise RuntimeError(f"{name}: changed the historical child Map source")
    conditional = rows["nested_map_conditional_initialize"]["source"]
    add(
        "repeated_lookup",
        conditional.replace(
            "        return saved.get('value');",
            "        const again = t.get(1);\n        return again.get('value');",
        ),
        9,
        admitted=True,
        retained=True,
        reused=True,
        repeated=True,
    )
    mixed = "mix(value) { t.set(1, value); return value; }"
    add(
        "conditional_mixed_before",
        conditional.replace("return { get(value) {", "return { " + mixed + ", get(value) {")
        + "host.slot.mix(0);\n",
        10,
        functions=5,
    )
    add(
        "conditional_mixed_after",
        conditional.replace("    } };", "    }, " + mixed + " };") + "host.slot.mix(0);\n",
        10,
        functions=5,
    )
    add(
        "conditional_unknown_contents",
        conditional.replace(
            "        saved.set('value', value);",
            "        const prior = saved.get('value');\n        saved.set('value', value);",
        ).replace(
            "return saved.get('value');", "return prior === void 0 ? saved.get('value') : prior;"
        ),
        9,
        retained=True,
        reused=True,
        previous=True,
    )
    add(
        "conditional_alias_delete",
        conditional.replace(
            "        return saved.get('value');",
            "        t.has(2) || t.set(2, new Map);\n        const other = t.get(2);\n"
            "        other.delete('value');\n        return saved.get('value');",
        ),
        12,
        children=2,
    )
    cross = rows["nested_map_cross_invocation"]["source"]
    inverted = cross.replace(
        "if (t.has(1)) { return t.get(1).get('value'); } return 0;",
        "if (!t.has(1)) { return 0; } return t.get(1).get('value');",
    )
    add(
        "cross_inverted_guard",
        inverted,
        9,
        functions=5,
        admitted=True,
        retained=True,
        separate=True,
    )
    add("cross_inverted_wrong_key", inverted.replace("!t.has(1)", "!t.has(2)"), 9, functions=5)
    rows["nested_map_cross_inverted_wrong_key"]["expected_trace"] = 0
    for name, mutation, calls in (
        ("unseeded", "t.set(1, new Map);", 11),
        ("delete", "if (t.has(1)) { t.get(1).delete('value'); }", 13),
        ("clear", "if (t.has(1)) { t.get(1).clear(); }", 13),
        ("mixed", "if (t.has(1)) { t.get(1).set('value', true); }", 13),
    ):
        sibling = "poison() { " + mutation + " return 0; }"
        for order in ("before", "after"):
            source = (
                cross.replace("    return {\n", "    return {\n        " + sibling + ",\n")
                if order == "before"
                else cross.replace("\n    };", ",\n        " + sibling + "\n    };")
            )
            source = source.replace("host.slot.set(41);", "host.slot.poison();\nhost.slot.set(41);")
            add("cross_" + name + "_" + order, source, calls, functions=6)
            rows["nested_map_cross_" + name + "_" + order]["poison"] = (
                mutation,
                "true" if name == "mixed" else "undefined",
            )
    previous = rows["nested_map_conditional_unknown_contents"]["source"]
    if hashlib.sha256(previous.encode()).hexdigest() != (
        "99954babb9c04e417826a46d8a90096d672b3f101bb914b2279900dacc66ed63"
    ):
        raise RuntimeError("changed the historical nullable child Map source")
    for name, payload, prefix, expected, calls, functions in (
        ("boolean", "true", "", "true", 13, 5),
        ("string", "'poison'", "", "'poison'", 13, 5),
        ("opaque", "opaque()", "function opaque() { return true; }\n", "true", 14, 6),
    ):
        mutation = "if (t.has(1)) { t.get(1).set('value', " + payload + "); }"
        sibling = "poison() { " + mutation + " return 0; }"
        for order in ("before", "after"):
            source = (
                previous.replace("return { get(value) {", "return { " + sibling + ", get(value) {")
                if order == "before"
                else previous.replace("    } };", "    }, " + sibling + " };")
            )
            key = "previous_" + name + "_" + order
            add(key, prefix + source + "host.slot.poison();\n", calls, functions=functions)
            rows["nested_map_" + key]["previous_poison"] = (mutation, expected)
    for family in ("cross_mixed", "previous_boolean", "previous_string"):
        for order in ("before", "after"):
            rows[f"nested_map_{family}_{order}"].update(
                owner=True,
                refusal=(
                    "which has no native carrier yet"
                    if family == "previous_string"
                    else "mixed native Map read needs independent present payload type evidence"
                ),
            )
    add(
        "dynamic_nullable",
        """var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const t = new Map;
    return {
        set(value, key) {
            t.has(1) || t.set(1, new Map);
            t.get(1).set(key, value);
            return value;
        },
        get(key) {
            if (!t.has(1)) { return null; }
            return t.get(1).get(key) || null;
        },
        remove(key) {
            if (!t.has(1)) { return 0; }
            const saved = t.get(1);
            saved.delete(key);
            if (!saved.size) { t.delete(1); }
            return 0;
        }
    };
});
host.slot.set(41, 'value');
var trace = host.slot.get('value');
""",
        15,
        functions=6,
        retained=True,
        reused=True,
        separate=True,
        dynamic=True,
    )
    # Preserve the original programs when admitting their exact nullable output.
    for name in (
        "conditional_unknown_contents",
        "conditional_alias_delete",
        "cross_unseeded_before",
        "cross_unseeded_after",
        "cross_delete_before",
        "cross_delete_after",
        "cross_clear_before",
        "cross_clear_after",
    ):
        rows["nested_map_" + name].update(owner=True, admitted=True, retained=True)
    rows["nested_map_conditional_alias_delete"].update(reused=True, alias=True)
    for mutation in ("unseeded", "delete", "clear"):
        for order in ("before", "after"):
            name = "cross_" + mutation + "_" + order
            row = rows["nested_map_" + name]
            row.update(separate=True, nullable=True, replacement=mutation == "unseeded")
            # A separate source observes actual absence without rewriting a method.
            source = row["source"].replace(
                "var trace = host.slot.get();", "host.slot.poison();\nvar trace = host.slot.get();"
            )
            add(
                name + "_undefined",
                source,
                row["calls"] + 1,
                functions=6,
                admitted=True,
                retained=True,
                separate=True,
            )
            rows["nested_map_" + name + "_undefined"].update(
                poison=row["poison"],
                nullable=True,
                replacement=row["replacement"],
                expected_trace="undefined",
            )
    add(
        "previous_observed",
        previous.replace("var trace = host.slot.get(41);", "var trace = host.slot.get(41) === 41;"),
        9,
        admitted=True,
        retained=True,
        reused=True,
        previous=True,
    )
    dynamic = rows["nested_map_dynamic_nullable"]["source"]
    add(
        "dynamic_nullable_observed",
        dynamic.replace(
            "var trace = host.slot.get('value');",
            "host.slot.remove('missing');\nvar trace = host.slot.get('value') === 41;",
        ),
        16,
        functions=6,
        admitted=True,
        retained=True,
        reused=True,
        separate=True,
        dynamic=True,
    )
    rows["nested_map_previous_observed"]["expected_trace"] = True
    rows["nested_map_dynamic_nullable_observed"]["expected_trace"] = True
    # Caller fields are a separate flat-Map boundary. No child-content or
    # returned-object authority follows from these scalar-result methods.
    payload = (
        object_argument_cases()["object_argument_scalar_key_payload"]["source"]
        .replace("var key = {};", "var key = {value: 64};")
        .replace(
            "var trace = host.slot.get(key);",
            "var trace = host.slot.get(key) === 1 ? key.value : 0;",
        )
    )
    variants = (
        ("fields", payload, 10, 64, False, False),
        (
            "alias_mutation",
            payload.replace(
                "host.slot.get(key); host.slot.erase();",
                "var alias = key; host.slot.get(key); alias.value = 65; host.slot.erase();",
            ).replace(
                "host.slot.get(key) === 1 ? key.value : 0",
                "host.slot.get(alias) === 1 && alias === key ? alias.value : 0",
            ),
            10,
            65,
            False,
            True,
        ),
        (
            "number_first",
            payload.replace("var key =", "host.slot.get(7); var key ="),
            11,
            64,
            True,
            False,
        ),
        ("number_last", payload + "host.slot.get(7);\n", 11, 64, True, False),
    )
    for name, source, calls, expected, mixed, alias in variants:
        # Preserve the first conditional-entry probes: the field access in
        # their selected arm is outside the unconditional caller proof.
        add("caller_payload_" + name, source, calls, functions=6)
        rows["nested_map_caller_payload_" + name]["expected_trace"] = expected
        source = source.replace(
            "var trace = host.slot.get(alias) === 1 && alias === key ? alias.value : 0;",
            "var traceCall = host.slot.get(alias) === 1; var traceIdentity = alias === key; "
            "var trace = alias.value;",
        ).replace(
            "var trace = host.slot.get(key) === 1 ? key.value : 0;",
            "var traceCall = host.slot.get(key) === 1; var trace = key.value;",
        )
        add("caller_payload_" + name + "_observed", source, calls, functions=6, admitted=True)
        rows["nested_map_caller_payload_" + name + "_observed"].update(
            caller_payload=True,
            mixed=mixed,
            global_alias=alias,
            expected_trace=expected,
            number_last=name == "number_last",
            observations={"traceCall": True, **({"traceIdentity": True} if alias else {})},
        )
    for name, source, calls, expected, functions in (
        ("returned", payload.replace("return t.has(1) ? 1 : 0;", "return e;"), 9, 0, 6),
        ("method_field", payload.replace("return t.has(1) ? 1 : 0;", "return e.value;"), 9, 0, 6),
        (
            "cycle",
            payload.replace(
                "host.slot.get(key); host.slot.erase();",
                "key.self = key; host.slot.get(key); host.slot.erase();",
            ),
            10,
            64,
            6,
        ),
        ("missing_field", payload.replace("{value: 64}", "{}"), 10, "undefined", 6),
        (
            "foreign_consumer",
            "function consume(item) { return 0; }\n"
            + payload.replace(
                "host.slot.get(key); host.slot.erase();",
                "consume(key); host.slot.get(key); host.slot.erase();",
            ),
            11,
            64,
            7,
        ),
        ("string_mixed", payload + "host.slot.get('other');\n", 11, 64, 6),
    ):
        add("caller_payload_" + name, source, calls, functions=functions)
        rows["nested_map_caller_payload_" + name]["expected_trace"] = expected
    # Keep mixed child contents separate from owning method results and fields.
    mixed_child = """var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const t = new Map;
    return {
        set(value) {
            t.has(1) || t.set(1, new Map);
            t.get(1).set('value', value);
            return 0;
        },
        get() {
            if (!t.has(1)) { return false; }
            return t.get(1).get('value') === 64;
        }
    };
});
var key = {value: 64};
var traceMissing = host.slot.get();
host.slot.set(64); var traceNumber = host.slot.get();
host.slot.set(key); var traceObject = host.slot.get();
host.slot.set(0); var trace = host.slot.get();
"""
    for order in ("number_first", "object_first"):
        source = mixed_child
        if order == "object_first":
            source = source.replace(
                "host.slot.set(64); var traceNumber = host.slot.get();\n"
                "host.slot.set(key); var traceObject = host.slot.get();",
                "host.slot.set(key); var traceObject = host.slot.get();\n"
                "host.slot.set(64); var traceNumber = host.slot.get();",
            )
        add("mixed_child_" + order, source, 16, functions=5, admitted=True)
        rows["nested_map_mixed_child_" + order].update(
            mixed_child=True,
            expected_trace=False,
            observations={"traceMissing": False, "traceNumber": True, "traceObject": False},
        )
    for name, mutation, calls in (
        ("delete", "if (t.has(1)) { t.get(1).delete('value'); }", 23),
        ("clear", "if (t.has(1)) { t.get(1).clear(); }", 23),
        ("replace", "t.set(1, new Map);", 21),
    ):
        source = (
            mixed_child.replace(
                "    return {\n", "    return {\n        poison() { " + mutation + " return 0; },\n"
            )
            + """host.slot.set(64); var traceBefore = host.slot.get();
host.slot.poison(); trace = host.slot.get();
"""
        )
        add("mixed_child_" + name, source, calls, functions=6, admitted=True)
        rows["nested_map_mixed_child_" + name].update(
            mixed_child=True,
            expected_trace=False,
            child_mutation=mutation,
            replacement=name == "replace",
            observations={
                "traceMissing": False,
                "traceNumber": True,
                "traceObject": False,
                "traceBefore": True,
            },
        )
    null_child = mixed_child.replace("=== 64;", "=== null;").replace(
        "host.slot.set(0); var trace = host.slot.get();",
        "host.slot.set(null); var trace = host.slot.get();",
    )
    add("mixed_child_null", null_child, 16, functions=5, admitted=True)
    rows["nested_map_mixed_child_null"].update(
        mixed_child=True,
        mixed_child_kind="null",
        expected_trace=True,
        observations={"traceMissing": False, "traceNumber": False, "traceObject": False},
    )
    missing = rows["nested_map_mixed_child_delete"]["source"].replace("=== 64;", "=== void 0;")
    add("mixed_child_missing", missing, 23, functions=6, admitted=True)
    rows["nested_map_mixed_child_missing"].update(
        mixed_child=True,
        mixed_child_kind="undefined",
        expected_trace=True,
        child_mutation="if (t.has(1)) { t.get(1).delete('value'); }",
        replacement=False,
        observations={
            "traceMissing": False,
            "traceNumber": False,
            "traceObject": False,
            "traceBefore": False,
        },
    )
    truthy = mixed_child.replace(
        "return t.get(1).get('value') === 64;", "return !!t.get(1).get('value');"
    )
    add("mixed_child_truthy", truthy, 16, functions=5, admitted=True)
    rows["nested_map_mixed_child_truthy"].update(
        mixed_child=True,
        mixed_child_kind="truthy",
        expected_trace=False,
        observations={"traceMissing": False, "traceNumber": True, "traceObject": True},
    )
    identity = (
        mixed_child.replace("get() {", "get(value) {")
        .replace("=== 64;", "=== value;")
        .replace("var traceMissing = host.slot.get();", "var traceMissing = host.slot.get(key);")
        .replace("var traceNumber = host.slot.get();", "var traceNumber = host.slot.get(64);")
        .replace("var traceObject = host.slot.get();", "var traceObject = host.slot.get(key);")
        .replace("var trace = host.slot.get();", "var trace = host.slot.get(0);")
    )
    add("mixed_child_identity", identity, 16, functions=5, admitted=True)
    rows["nested_map_mixed_child_identity"].update(
        mixed_child=True,
        mixed_child_kind="identity",
        expected_trace=True,
        observations={"traceMissing": False, "traceNumber": True, "traceObject": True},
    )
    # These separate probes retain real returned values; admission is measured
    # independently from the Boolean-result child-content programs above.
    returned = (
        mixed_child.replace(
            "if (!t.has(1)) { return false; }\n" "            return t.get(1).get('value') === 64;",
            "return t.has(1) && t.get(1).get('value') || null;",
        )
        .replace(
            "var traceMissing = host.slot.get();", "var traceMissing = host.slot.get() === null;"
        )
        .replace("var traceNumber = host.slot.get();", "var traceNumber = host.slot.get() === 64;")
        .replace("var traceObject = host.slot.get();", "var traceObject = host.slot.get() === key;")
        .replace("var trace = host.slot.get();", "var trace = host.slot.get() === null;")
    )
    add("mixed_child_returned_identity", returned, 16, functions=5, admitted=True)
    rows["nested_map_mixed_child_returned_identity"].update(
        mixed_child=True,
        mixed_child_kind="returned",
        expected_trace=True,
        observations={"traceMissing": True, "traceNumber": True, "traceObject": True},
    )
    add(
        "mixed_child_returned_field",
        returned.replace(
            "var traceObject = host.slot.get() === key;", "var traceObject = host.slot.get().value;"
        ),
        16,
        functions=5,
    )
    rows["nested_map_mixed_child_returned_field"].update(
        expected_trace=True,
        observations={"traceMissing": True, "traceNumber": True, "traceObject": 64},
    )
    return rows


def caller_payload_observer(source, row):
    return (
        source
        + """
(function() {
    const get = host.slot.get, erase = host.slot.erase, clear = host.slot.clear;
    host = {};
    const original = Map.prototype.has;
    let map;
    Map.prototype.has = function(key) { map = this; return original.call(this, key); };
    let ok = get(key) === 1 && key.value === INITIAL_FIELD;
    Map.prototype.has = original;
    let saved = map.get(1);
    ok = ok && saved === key;
    key.value = 73;
    ok = ok && saved.value === 73 && erase() === 1 && map.size === 0;
    key.value = 74;
    ok = ok && saved.value === 74 && clear() === 0 && saved === key;
    for (let i = 0; i < 128; ++i) {
        const value = i % 2 === 0 ? -i : i + 0.5;
        const item = {value}, alias = item, other = {value: value + 1};
        ok = ok && get(item) === 1 && get(alias) === 1 && map.get(1) === item;
        saved = map.get(1);
        item.value = value + 2;
        ok = ok && saved.value === value + 2 && get(other) === 1 && map.get(1) === other;
        ok = ok && saved === item && saved !== other && saved.value === value + 2;
        ok = ok && erase() === 1 && erase() === 0 && get(other) === 1 && clear() === 0;
        ok = ok && map.size === 0 && other.value === value + 1 && saved.value === value + 2;
        MIXED
    }
    trace = ok ? 1 : 0;
})();
""".replace("INITIAL_FIELD", str(row["expected_trace"])).replace(
            "MIXED",
            (
                """
        ok = ok && get(value) === 1 && map.get(1) === value && erase() === 1 &&
             map.size === 0 && saved.value === value + 2;"""
                if row["mixed"]
                else ""
            ),
        )
    )


def caller_payload_lifetime_cpp(cpp, row):
    changed = object_payload_lifetime_cpp(cpp, "object_argument_scalar_key_payload")
    changed = changed.replace(
        "    auto owner = g_host;",
        """    if (g_key->field_76616c7565.tag != ctnative::nullable_scalar::kind::number ||
        g_key->field_76616c7565.value != INITIAL_FIELD) { return 311; }
    auto owner = g_host;""",
    ).replace("INITIAL_FIELD", str(row["expected_trace"]))
    changed = changed.replace(
        "        auto alias = first;",
        """        first->field_76616c7565 = ctnative::nullable_scalar{static_cast<js_num>(call)};
        auto alias = first;""",
    ).replace(
        "        first.reset(); alias.reset();",
        """        first->field_76616c7565 = ctnative::nullable_scalar{static_cast<js_num>(call) + 0.5};
        if (map->at(js_num{1}).object->field_76616c7565.value != call + 0.5) { return 312; }
        first.reset(); alias.reset();""",
    )
    changed = changed.replace(
        "    auto last = std::make_shared<ctnative::identity_object>();",
        """    auto retained = std::make_shared<ctnative::identity_object>();
    retained->field_76616c7565 = ctnative::nullable_scalar{73.0};
    std::weak_ptr retained_lifetime = retained;
    if (get(retained) != 1) { return 313; }
    auto saved = ctnative::map_get(map, js_num{1});
    retained.reset();
    if (erase() != 1 || !map->empty() || retained_lifetime.expired() ||
        saved.object->field_76616c7565.value != 73) { return 314; }
    saved.object->field_76616c7565 = ctnative::nullable_scalar{74.0};
    if (clear() != 0 || saved.object->field_76616c7565.value != 74) { return 315; }
    saved = {};
    if (!retained_lifetime.expired()) { return 316; }
    auto last = std::make_shared<ctnative::identity_object>();
    last->field_76616c7565 = ctnative::nullable_scalar{91.0};""",
    ).replace(
        "    map.reset();",
        """    if (map->begin()->second.object->field_76616c7565.value != 91) { return 317; }
    map.reset();""",
    )
    if row["global_alias"]:
        changed = changed.replace("g_key.reset();", "g_key.reset(); g_alias.reset();")
    if row["mixed"]:
        changed = (
            changed.replace(
                "std::function<js_num(Object)>", "std::function<js_num(ctnative::object_value)>"
            )
            .replace(
                "static_assert(!std::is_invocable_v<decltype(get), int>);",
                "static_assert(std::is_invocable_v<decltype(get), Object>);\n"
                "    static_assert(std::is_invocable_v<decltype(get), js_num>);",
            )
            .replace(
                "    if (map->size() != 1 || map->begin()->second.object != ctn_test_objects[0].lock() ||",
                "    if (get(g_key) != 1) { return 318; }\n"
                "    if (map->size() != 1 || map->begin()->second.object != ctn_test_objects[0].lock() ||",
            )
        )
        # The historical helper drops g_key before its Map observation.
        changed = changed.replace("} g_key.reset();", "}", 1).replace(
            "    if (clear() != 0) { return 265; }",
            "    g_key.reset();\n    if (clear() != 0) { return 265; }",
            1,
        )
        changed = changed.replace(
            "        if (!other_lifetime.expired() || !map->empty()) { return 259; }",
            """        if (!other_lifetime.expired() || !map->empty()) { return 259; }
        const js_num number = call % 2 == 0 ? -call : call + 0.5;
        if (get(number) != 1) { return 319; }
        const auto scalar = ctnative::map_get(map, js_num{1});
        if (scalar.object || scalar.scalar.tag != ctnative::nullable_scalar::kind::number ||
            scalar.scalar.value != number || erase() != 1 || !map->empty()) { return 320; }""",
        )
        if row["number_last"]:
            changed = changed.replace(
                "    if (get(g_key) != 1)",
                """    const auto initial = ctnative::map_get(map, js_num{1});
    if (initial.object || initial.scalar.tag != ctnative::nullable_scalar::kind::number ||
        initial.scalar.value != 7) { return 321; }
    if (get(g_key) != 1)""",
            ).replace(
                "    if (g_key != ctn_test_objects[1].lock())",
                "    if (g_host->slot->m_get(g_key) != 1) { return 322; }\n"
                "    if (g_key != ctn_test_objects[1].lock())",
            )
    return changed


def mixed_child_observer(source, row):
    kind = row.get("mixed_child_kind", "number")
    expected = {
        "number": "value === 64",
        "null": "value === null",
        "undefined": "value === undefined",
        "truthy": "!!value",
        "identity": "true",
        "returned": "true",
    }[kind]
    observed = source + """
(function() {
    const set = host.slot.set, get = host.slot.get;
    const read = value => GET;
    const expected = value => EXPECTED;
    POISON_BINDING
    host = {};
    const original = Map.prototype.get;
    let outer, child;
    Map.prototype.get = function(key) {
        const result = original.call(this, key);
        if (result instanceof Map) { outer = this; child = result; }
        return result;
    };
    let ok = set(64) === 0 && read(64) === expected(64);
    Map.prototype.get = original;
    const detached = child;
    outer.clear();
    detached.set('value', key);
    ok = ok && read(64) === false && set(64) === 0 && outer.get(1) !== detached &&
         detached.get('value') === key && read(64) === expected(64);
    for (let i = 0; i < 128; ++i) {
        const value = i % 2 === 0 ? -i : i + 0.5;
        const item = {value}, alias = item, other = {value: value + 1};
        ok = ok && set(item) === 0 && read(item) === expected(item);
        child = outer.get(1);
        const saved = child.get('value');
        item.value = value + 2;
        ok = ok && saved === alias && saved.value === value + 2;
        ok = ok && set(other) === 0 && child.get('value') === other &&
             read(other) === expected(other) DISTINCT;
        ok = ok && saved === item && saved !== other && saved.value === value + 2;
        ok = ok && set(value) === 0 && child.get('value') === value &&
             read(value) === expected(value) && saved.value === value + 2;
        POISON_CHECK
    }
    NULL_CHECK
    child = outer.get(1);
    child.clear();
    ok = ok && read(64) === MISSING && child.size === 0;
    outer.clear();
    ok = ok && read(64) === false && set(64) === 0 && outer.get(1) !== child &&
         read(64) === expected(64);
    trace = ok ? 1 : 0;
})();
"""
    if kind == "returned":
        observed = observed.replace(
            "const saved = child.get('value');", "const saved = get();"
        ).replace(
            "        POISON_CHECK",
            "        child.delete('value'); child.clear();\n"
            "        ok = ok && saved === item && saved.value === value + 2;",
        )
    return (
        observed.replace(
            "GET",
            (
                "get(value)"
                if kind == "identity"
                else "get() === (value || null)" if kind == "returned" else "get()"
            ),
        )
        .replace("EXPECTED", expected)
        .replace(
            "POISON_BINDING", "const poison = host.slot.poison;" if "child_mutation" in row else ""
        )
        .replace("DISTINCT", "&& read(item) === false" if kind in {"identity", "returned"} else "")
        .replace(
            "POISON_CHECK",
            (
                """const prior = outer.get(1);
        ok = ok && set(64) === 0 && poison() === 0 && read(64) === MISSING &&
             (outer.get(1) !== prior) === REPLACEMENT &&
             prior.get('value') === PRIOR;""".replace(
                    "REPLACEMENT", str(row["replacement"]).lower()
                ).replace("PRIOR", "64" if row["replacement"] else "undefined")
                if "child_mutation" in row
                else ""
            ),
        )
        .replace(
            "NULL_CHECK",
            (
                "ok = ok && set(null) === 0 && read(null) === true;"
                if kind == "null"
                else (
                    "for (const value of [null, false, 0, -0, NaN]) { "
                    "ok = ok && set(value) === 0 && get() === null; }"
                    if kind == "returned"
                    else ""
                )
            ),
        )
        .replace("MISSING", "true" if kind == "undefined" else "false")
    )


def mixed_child_lifetime_cpp(cpp, row):
    kind = row.get("mixed_child_kind", "number")
    changed = instrument_leaf_objects(cpp) + r"""
int main() {
    using Value = ctnative::object_value;
    using Scalar = ctnative::nullable_scalar;
    using Child = ctnative::map_storage<std::string, Value>;
    using Outer = ctnative::map_storage<js_num, std::shared_ptr<Child>>;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != INITIAL_MAPS ||
        ctn_test_objects.size() != 1) { return 330; }
    auto owner = g_host;
    auto table = owner->slot;
    auto set = table->m_set;
    auto get = table->m_get;
    POISON_BINDING
    static_assert(std::is_same_v<decltype(set), std::function<js_num(Value)>>);
    static_assert(std::is_same_v<decltype(get), std::function<bool(GET_SIGNATURE)>>);
    auto read = [&get](Value value) { (void)value; return GET; };
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); g_key.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired() || !ctn_test_objects[0].expired()) { return 331; }
    auto outer = std::const_pointer_cast<Outer>(
        std::static_pointer_cast<const Outer>(ctn_test_maps[0].lock()));
    auto detached = outer->at(js_num{1});
    std::weak_ptr detached_lifetime = detached;
    outer->clear();
    ctnative::map_set(detached, std::string{"value"}, Value{js_num{73}});
    if (read(js_num{64}) || set(js_num{64}) != 0 || outer->at(js_num{1}) == detached ||
        ctnative::map_get(detached, std::string{"value"}).scalar.value != 73 ||
        read(js_num{64}) != NUMBER_64) { return 332; }
    detached.reset();
    if (!detached_lifetime.expired()) { return 333; }
    for (int call = 0; call < 128; ++call) {
        const js_num number = call % 2 == 0 ? -call : call + 0.5;
        auto first = std::make_shared<ctnative::identity_object>();
        first->field_76616c7565 = Scalar{number};
        auto alias = first;
        std::weak_ptr first_lifetime = first;
        if (set(first) != 0 || read(first) != OBJECT_RESULT) { return 334; }
        auto child = outer->at(js_num{1});
        auto saved = ctnative::map_get(child, std::string{"value"});
        first->field_76616c7565 = Scalar{number + 2};
        if (saved.object != alias || saved.object->field_76616c7565.value != number + 2) { return 335; }
        first.reset(); alias.reset();
        auto other = std::make_shared<ctnative::identity_object>();
        other->field_76616c7565 = Scalar{number + 1};
        std::weak_ptr other_lifetime = other;
        if (set(other) != 0 || child->at("value").object != other ||
            read(other) != OBJECT_RESULT DISTINCT) { return 336; }
        other.reset();
        if (set(number) != 0 || !other_lifetime.expired() || first_lifetime.expired() ||
            child->at("value").object || child->at("value").scalar.tag != Scalar::kind::number ||
            child->at("value").scalar.value != number || read(number) != NUMBER_RESULT ||
            saved.object->field_76616c7565.value != number + 2) { return 337; }
        POISON_CHECK
        saved = {};
        if (!first_lifetime.expired()) { return 338; }
    }
    NULL_CHECK
    auto child = outer->at(js_num{1});
    child->clear();
    if (read(js_num{64}) != MISSING || !child->empty()) { return 339; }
    outer->clear();
    if (read(js_num{64}) || set(js_num{64}) != 0 || outer->at(js_num{1}) == child ||
        read(js_num{64}) != NUMBER_64) { return 340; }
    child.reset();
    auto last = std::make_shared<ctnative::identity_object>();
    last->field_76616c7565 = Scalar{91.0};
    std::weak_ptr last_lifetime = last;
    if (set(last) != 0 || read(last) != OBJECT_RESULT) { return 341; }
    auto saved = ctnative::map_get(outer->at(js_num{1}), std::string{"value"});
    last.reset();
    const auto next = ctn_test_maps.size();
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != next + INITIAL_MAPS ||
        ctn_test_objects.size() != 2 || ctn_test_maps[0].lock() == ctn_test_maps[next].lock() ||
        last_lifetime.expired() || saved.object->field_76616c7565.value != 91) { return 342; }
    outer.reset(); set = {}; POISON_DROP
    if (ctn_test_maps[0].expired() || read(saved) != OBJECT_RESULT) { return 343; }
    get = {};
    if (!ctn_test_maps[0].expired() || last_lifetime.expired() ||
        saved.object->field_76616c7565.value != 91) { return 344; }
    saved = {};
    if (!last_lifetime.expired()) { return 345; }
    g_host.reset(); g_key.reset();
    for (const auto & map : ctn_test_maps) {
        if (!map.expired()) { return 346; }
    }
    for (const auto & object : ctn_test_objects) {
        if (!object.expired()) { return 347; }
    }
    return 0;
}
"""
    if kind == "returned":
        changed = (
            changed.replace("std::function<bool(GET_SIGNATURE)>", "std::function<Value()>")
            .replace(
                'auto saved = ctnative::map_get(child, std::string{"value"});',
                "auto saved = get();",
            )
            .replace(
                'auto saved = ctnative::map_get(outer->at(js_num{1}), std::string{"value"});',
                "auto saved = get();",
            )
            .replace(
                "        POISON_CHECK",
                '        ctnative::map_delete(child, std::string{"value"}); ctnative::map_clear(child);\n'
                "        if (first_lifetime.expired() || saved.object->field_76616c7565.value != number + 2) "
                "{ return 352; }",
            )
        )
    return (
        changed.replace("INITIAL_MAPS", "3" if row.get("replacement") else "2")
        .replace(
            "POISON_BINDING", "auto poison = table->m_poison;" if "child_mutation" in row else ""
        )
        .replace("POISON_DROP", "poison = {};" if "child_mutation" in row else "")
        .replace("GET_SIGNATURE", "Value" if kind == "identity" else "")
        .replace(
            "GET",
            (
                "get(value)"
                if kind == "identity"
                else (
                    "ctnative::object_strict_equal(get(), ctnative::object_truthy(value) ? value : Value{Scalar::null()})"
                    if kind == "returned"
                    else "get()"
                )
            ),
        )
        .replace(
            "NUMBER_64", "true" if kind in {"number", "truthy", "identity", "returned"} else "false"
        )
        .replace("OBJECT_RESULT", "true" if kind in {"truthy", "identity", "returned"} else "false")
        .replace(
            "NUMBER_RESULT",
            (
                "true"
                if kind in {"identity", "returned"}
                else (
                    "(number != 0)"
                    if kind == "truthy"
                    else "(number == 64)" if kind == "number" else "false"
                )
            ),
        )
        .replace("DISTINCT", "|| read(saved)" if kind in {"identity", "returned"} else "")
        .replace(
            "POISON_CHECK",
            (
                """if (set(js_num{64}) != 0 || poison() != 0 ||
            read(js_num{64}) != MISSING || (outer->at(js_num{1}) != child) != REPLACEMENT) { return 348; }
        const auto prior = ctnative::map_get(child, std::string{"value"});
        if (PRIOR) { return 349; }""".replace(
                    "REPLACEMENT", str(row["replacement"]).lower()
                ).replace(
                    "PRIOR",
                    (
                        "prior.object || prior.scalar.tag != Scalar::kind::number || prior.scalar.value != 64"
                        if row["replacement"]
                        else "prior.object || prior.scalar.tag != Scalar::kind::undefined"
                    ),
                )
                if "child_mutation" in row
                else ""
            ),
        )
        .replace(
            "NULL_CHECK",
            (
                "if (set(Scalar::null()) != 0 || !read(Scalar::null())) { return 350; }"
                if kind == "null"
                else (
                    "for (Value value : {Value{Scalar::null()}, Value{false}, Value{0.0}, Value{-0.0}, "
                    'Value{std::nan("")}}) { if (set(value) != 0 || get().object || '
                    "get().scalar.tag != Scalar::kind::null) { return 351; } }"
                    if kind == "returned"
                    else ""
                )
            ),
        )
        .replace("MISSING", "true" if kind == "undefined" else "false")
    )


def nested_map_observer(source, row):
    if row.get("mixed_child"):
        return mixed_child_observer(source, row)
    observed = source + """
(function() {
    const get = host.slot.get;
    host = {};
    const seen = [], original = Map.prototype.METHOD;
    Map.prototype.METHOD = function(key, value) {
        const result = original.call(this, key, value);
        if (ITEM instanceof Map) { seen.push([this, key, ITEM]); }
        return result;
    };
    let ok = get(17) === 17 && get(-3) === -3;
    Map.prototype.METHOD = original;
    const stride = STRIDE;
    ok = ok && seen.length === stride * 2 && seen[0][0] === seen[stride][0] &&
         seen[0][2] IDENTITY seen[stride][2] && seen[0][2].get('value') === FIRST_VALUE &&
         seen[stride][2].get('value') === -3 && seen[0][0].size === OUTER_SIZE;
    DISTINCT
    for (let i = 0; i < 128; ++i) {
        const value = i % 2 === 0 ? -i : i + 0.5;
        const answer = get(value);
        if (typeof answer !== 'number' || answer !== value) { ok = false; }
        REUSE_CHECK
    }
    RECREATE
    trace = ok ? 1 : 0;
})();
"""
    distinct = (
        ""
        if row["children"] == 1
        else """ok = ok && seen[0][2] !== seen[1][2] &&
        seen[1][2] === seen[2][2] && seen[stride + 1][2] === seen[stride + 2][2];"""
    )
    if row["repeated"]:
        distinct = "ok = ok && seen[0][2] === seen[1][2] && seen[2][2] === seen[3][2];"
    if row.get("alias"):
        distinct = """ok = ok && seen[0][2] !== seen[1][2] && seen[1][2] === seen[3][2] &&
        seen[1][2].get('value') === undefined;"""
    reused = row["reused"]
    recreate = (
        """const outer = seen[0][0], saved = seen[0][2];
    outer.clear(); saved.set('value', 47);
    ok = ok && get(19) === 19 && outer.size === 1 && outer.get(1) !== saved &&
         saved.get('value') === 47;"""
        if reused
        else ""
    )
    if row["separate"]:
        observed = (
            observed.replace(
                "const get = host.slot.get;", "const set = host.slot.set, get = host.slot.get;"
            )
            .replace(
                "get(17) === 17 && get(-3) === -3",
                "set(17) === 17 && get() === 17 && set(-3) === -3 && get() === -3",
            )
            .replace(
                "const answer = get(value);", "const answer = set(value) === value ? get() : NaN;"
            )
        )
        recreate = """const outer = seen[0][0], saved = seen[0][2];
    outer.clear(); saved.set('value', 47);
    ok = ok && get() === 0 && set(19) === 19 && get() === 19 && outer.size === 1 &&
         outer.get(1) !== saved && saved.get('value') === 47;"""
    if row.get("nullable"):
        observed = observed.replace(
            "const set = host.slot.set, get = host.slot.get;",
            "const set = host.slot.set, get = host.slot.get, poison = host.slot.poison;",
        )
        recreate = """const poisoned = seen[0][0].get(1);
    poison();
    ok = ok && get() === undefined && seen[0][0].size === 1 &&
         (seen[0][0].get(1) !== poisoned) === REPLACEMENT &&
         poisoned.get('value') === POISONED_VALUE;
    set(29);
    """ + recreate
        recreate = recreate.replace("REPLACEMENT", str(row["replacement"]).lower()).replace(
            "POISONED_VALUE", "127.5" if row["replacement"] else "undefined"
        )
    if row["previous"]:
        observed = (
            observed.replace("get(17) === 17 && get(-3) === -3", "get(17) === 41 && get(-3) === 17")
            .replace(
                "for (let i = 0; i < 128; ++i)",
                "let previous = -3;\n    for (let i = 0; i < 128; ++i)",
            )
            .replace(
                "answer !== value) { ok = false; }",
                "answer !== previous) { ok = false; }\n" "        previous = value;",
            )
        )
        recreate = recreate.replace(
            "    outer.clear();",
            """    saved.clear();
    ok = ok && get(13) === 13 && outer.get(1) === saved && saved.get('value') === 13;
    outer.clear();""",
        )
    if row["dynamic"]:
        observed = (
            observed.replace(
                "const set = host.slot.set, get = host.slot.get;",
                "const write = host.slot.set, read = host.slot.get, remove = host.slot.remove;\n"
                "    const set = value => write(value, 'value'), get = () => read('value');",
            )
            .replace("typeof answer !== 'number' || answer !== value", "answer !== (value || null)")
            .replace(
                "        REUSE_CHECK",
                """        REUSE_CHECK
        const key = 'caller-' + i;
        if (write(value, key) !== value || read(key) !== (value || null) ||
            remove(key) !== 0 || read(key) !== null) { ok = false; }""",
            )
            .replace(
                "    RECREATE",
                """    const outer = seen[0][0], saved = seen[0][2];
    ok = ok && read('missing') === null && write(7, 'other') === 7 && read('other') === 7;
    remove('value');
    ok = ok && get() === null && read('other') === 7 && outer.size === 1;
    remove('other');
    ok = ok && get() === null && saved.size === 0 && outer.size === 0;
    ok = ok && write(0, 'empty') === 0 && read('empty') === null;
    remove('empty');
    ok = ok && outer.size === 0;
    saved.set('value', 47);
    ok = ok && set(19) === 19 && get() === 19 && outer.size === 1 &&
         outer.get(1) !== saved && saved.get('value') === 47;""",
            )
        )
    if row.get("alias"):
        recreate = recreate.replace("outer.size === 1", "outer.size === 2")
    stride = (
        2
        if row["repeated"] or row["dynamic"] or row.get("alias")
        else 1 if row["children"] == 1 else 3
    )
    return (
        observed.replace("STRIDE", str(stride))
        .replace("OUTER_SIZE", "2" if row.get("alias") else "1" if row["retained"] else "0")
        .replace("DISTINCT", distinct)
        .replace("METHOD", "get" if reused else "set")
        .replace("ITEM", "result" if reused else "value")
        .replace("IDENTITY", "===" if reused else "!==")
        .replace("FIRST_VALUE", "-3" if reused else "17")
        .replace(
            "REUSE_CHECK",
            "if (seen[0][2].get('value') !== value) { ok = false; }" if reused else "",
        )
        .replace("RECREATE", recreate)
    )


def nested_map_lifetime_cpp(cpp, row):
    # Reuse the existing weak allocation observer; generated ownership is unchanged.
    if row.get("caller_payload"):
        return caller_payload_lifetime_cpp(cpp, row)
    if row.get("mixed_child"):
        return mixed_child_lifetime_cpp(cpp, row)
    if row.get("nullable"):
        return nested_map_mutation_lifetime_cpp(cpp, row)
    changed = instrument_leaf_objects(cpp, allocations=0) + r"""
int main() {
    using Child = ctnative::number_map<std::string>;
    using Outer = ctnative::map_storage<js_num, std::shared_ptr<Child>>;
    constexpr std::size_t children = CHILDREN;
    constexpr bool retained = RETAINED;
    constexpr bool reused = REUSED;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1 + children) { return 270; }
    auto owner = g_host;
    auto table = owner->slot;
    auto get = table->m_get;
    static_assert(std::is_same_v<decltype(get), std::function<js_num(js_num)>>);
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired()) { return 271; }
    auto outer = std::const_pointer_cast<Outer>(
        std::static_pointer_cast<const Outer>(ctn_test_maps[0].lock()));
    if (outer->size() != (retained ? 1U : 0U)) { return 272; }
    std::shared_ptr<Child> saved;
    std::weak_ptr<Child> saved_lifetime;
    if constexpr (retained) {
        saved = outer->at(js_num{1}); saved_lifetime = saved;
        if (saved != ctn_test_maps[1].lock() || saved->at("value") != 41) { return 273; }
    }
    for (int call = 0; call < 128; ++call) {
        const auto before = ctn_test_maps.size();
        const js_num value = call % 2 == 0 ? -call : call + 0.5;
        if (get(value) != value ||
            ctn_test_maps.size() != before + (reused ? 0U : children)) { return 274; }
        for (std::size_t index = before; index < ctn_test_maps.size(); ++index) {
            if (ctn_test_maps[index].expired() == retained) { return 275; }
        }
        if constexpr (retained) {
            auto child = outer->at(js_num{1});
            if ((reused ? child != saved : child == saved) || child != ctn_test_maps.back().lock() ||
                child->at("value") != value || saved->at("value") != (reused ? value : 41)) {
                return 276;
            }
        }
    }
    ctnative::map_clear(outer);
    if constexpr (retained) {
        if (outer->size() != 0U || saved_lifetime.expired() ||
            (!reused && !ctn_test_maps.back().expired())) { return 277; }
        ctnative::map_set(saved, std::string{"value"}, js_num{47});
        if (ctnative::map_get_present(saved, std::string{"value"}) != 47) { return 278; }
        saved.reset();
        if (!saved_lifetime.expired()) { return 279; }
    }
    const auto cleared = ctn_test_maps.size();
    if (get(19) != 19 || ctn_test_maps.size() != cleared + children) { return 280; }
    const auto next = ctn_test_maps.size();
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != next + children + 1 ||
        ctn_test_maps[0].lock() == ctn_test_maps[next].lock()) { return 281; }
    outer.reset();
    if (ctn_test_maps[0].expired() || get(23) != 23) { return 282; }
    get = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[next].expired()) { return 283; }
    g_host.reset();
    for (const auto & map : ctn_test_maps) {
        if (!map.expired()) { return 284; }
    }
    return 0;
}
"""
    if row["separate"]:
        changed = (
            changed.replace(
                "auto get = table->m_get;", "auto get = table->m_get;\n    auto set = table->m_set;"
            )
            .replace(
                "static_assert(std::is_same_v<decltype(get), std::function<js_num(js_num)>>);",
                "static_assert(std::is_same_v<decltype(get), std::function<js_num()>>);\n"
                "    static_assert(std::is_same_v<decltype(set), std::function<js_num(js_num)>>);",
            )
            .replace("get(value) != value", "set(value) != value || get() != value")
            .replace(
                "const auto cleared = ctn_test_maps.size();",
                "if (get() != 0) { return 285; }\n    const auto cleared = ctn_test_maps.size();",
            )
            .replace("get(19) != 19", "set(19) != 19 || get() != 19")
            .replace("get(23) != 23", "set(23) != 23 || get() != 23")
            .replace(
                "get = {};",
                "get = {};\n    if (ctn_test_maps[0].expired() || set(29) != 29) "
                "{ return 286; }\n    set = {};",
            )
        )
    if row["previous"]:
        changed = (
            changed.replace(
                "for (int call = 0; call < 128; ++call)",
                "js_num previous = 41;\n    for (int call = 0; call < 128; ++call)",
            )
            .replace("get(value) != value", "get(value) != previous")
            .replace(
                "        for (std::size_t index = before;",
                "        previous = value;\n        for (std::size_t index = before;",
            )
            .replace("get(23) != 23", "get(23) != 19")
            .replace(
                "    ctnative::map_clear(outer);",
                """    ctnative::map_clear(saved);
    if (get(13) != 13 || outer->at(js_num{1}) != saved || saved->at("value") != 13) { return 293; }
    ctnative::map_clear(outer);""",
            )
        )
    if row.get("alias"):
        changed = (
            changed.replace("(retained ? 1U : 0U)", "2U")
            .replace(
                "child != ctn_test_maps.back().lock()",
                "(child != ctn_test_maps[1].lock() || outer->at(js_num{2}) != ctn_test_maps[2].lock() || "
                "outer->at(js_num{2})->size() != 0U)",
            )
            .replace(
                "    ctnative::map_clear(outer);",
                "    ctnative::map_clear(outer);\n"
                "    if (!ctn_test_maps[2].expired()) { return 294; }",
            )
            .replace(
                "get(19) != 19",
                "get(19) != 19 || outer->size() != 2U || "
                "outer->at(js_num{1}) == outer->at(js_num{2})",
            )
        )
    if row["previous"] or row.get("alias"):
        changed = changed.replace(
            "std::function<js_num(js_num)>", "std::function<ctnative::nullable_scalar(js_num)>"
        )
        changed = re.sub(r"\bget\((value|13|19|23)\)", r"ctnative::global_number(get(\1))", changed)
        if row["expected_trace"] is not True:
            changed = changed.replace(
                "    auto owner = g_host;",
                "    static_assert(std::is_same_v<decltype(g_trace), ctnative::nullable_scalar>);\n"
                "    if (ctnative::global_number(g_trace) != 41) { return 310; }\n"
                "    auto owner = g_host;",
            )
    if row["dynamic"]:
        changed = (
            changed.replace(
                "auto set = table->m_set;",
                """auto set = table->m_set;
    auto remove = table->m_remove;
    using Result = ctnative::nullable_scalar;
    const auto matches = [](Result result, js_num value) {
        return value == 0 ? result.tag == Result::kind::null
            : result.tag == Result::kind::number && result.value == value;
    };""",
            )
            .replace("std::function<js_num()>", "std::function<Result(std::string)>")
            .replace("std::function<js_num(js_num)>", "std::function<js_num(js_num, std::string)>")
            .replace(
                "set(value) != value || get() != value",
                'set(value, "value") != value || !matches(get("value"), value)',
            )
            .replace("get() != 0", 'get("value").tag != Result::kind::null')
            .replace(
                "set(19) != 19 || get() != 19",
                'set(19, "value") != 19 || !matches(get("value"), 19)',
            )
            .replace(
                "set(23) != 23 || get() != 23",
                'set(23, "value") != 23 || !matches(get("value"), 23)',
            )
            .replace("set(29) != 29", 'set(29, "value") != 29')
            .replace(
                "        for (std::size_t index = before;",
                """        const std::string spelling = "caller-" + std::to_string(call);
        std::string key = spelling;
        if (set(value, key) != value) { return 291; }
        key.assign(key.size(), 'x');
        if (!matches(get(spelling), value) || remove(spelling) != 0 ||
            get(spelling).tag != Result::kind::null) { return 292; }
        for (std::size_t index = before;""",
            )
            .replace(
                "    ctnative::map_clear(outer);",
                """    if (get("missing").tag != Result::kind::null || set(7, "other") != 7 ||
        !matches(get("other"), 7) || remove("value") != 0 ||
        get("value").tag != Result::kind::null || !matches(get("other"), 7) ||
        outer->size() != 1U) { return 287; }
    if (remove("other") != 0 || get("value").tag != Result::kind::null ||
        saved->size() != 0U || outer->size() != 0U || saved_lifetime.expired()) { return 288; }
    if (set(0, "empty") != 0 || get("empty").tag != Result::kind::null ||
        remove("empty") != 0 || outer->size() != 0U) { return 289; }
    ctnative::map_clear(outer);""",
            )
            .replace(
                "    set = {};",
                "    set = {};\n    if (ctn_test_maps[0].expired()) { return 290; }\n    remove = {};",
            )
        )
    return (
        changed.replace("CHILDREN", str(row["children"]))
        .replace("RETAINED", str(row["retained"]).lower())
        .replace("REUSED", str(row["reused"]).lower())
    )


def nested_map_mutation_lifetime_cpp(cpp, row):
    # Call the real sibling after publication; neither its tag nor its child
    # lifetime can be inferred from the initial Number observation.
    changed = instrument_leaf_objects(cpp, allocations=0) + r"""
int main() {
    using Child = ctnative::number_map<std::string>;
    using Outer = ctnative::map_storage<js_num, std::shared_ptr<Child>>;
    using Result = ctnative::nullable_scalar;
    constexpr bool replacement = REPLACEMENT;
    constexpr std::size_t initial_maps = INITIAL_MAPS;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != initial_maps) { return 295; }
    auto owner = g_host;
    auto table = owner->slot;
    auto get = table->m_get;
    auto set = table->m_set;
    auto poison = table->m_poison;
    static_assert(std::is_same_v<decltype(get), std::function<Result()>>);
    static_assert(std::is_same_v<decltype(set), std::function<js_num(js_num)>>);
    static_assert(std::is_same_v<decltype(poison), std::function<js_num()>>);
    static_assert(std::is_same_v<decltype(g_trace), Result>);
    if (get().tag != Result::kind::INITIAL_TAG ||
        g_trace.tag != Result::kind::INITIAL_TAG) { return 296; }
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired()) { return 297; }
    auto outer = std::const_pointer_cast<Outer>(
        std::static_pointer_cast<const Outer>(ctn_test_maps[0].lock()));
    for (int call = 0; call < 128; ++call) {
        const auto before = ctn_test_maps.size();
        const js_num value = call % 2 == 0 ? -call : call + 0.5;
        if (set(value) != value || ctnative::global_number(get()) != value ||
            ctn_test_maps.size() != before + 1) { return 298; }
        auto saved = outer->at(js_num{1});
        std::weak_ptr saved_lifetime = saved;
        if (poison() != 0 || get().tag != Result::kind::undefined || outer->size() != 1U ||
            ctn_test_maps.size() != before + 1 + (replacement ? 1U : 0U) ||
            (outer->at(js_num{1}) != saved) != replacement) { return 299; }
        const auto prior = ctnative::map_get(saved, std::string{"value"});
        if (replacement ? prior.tag != Result::kind::number || prior.value != value
                        : prior.tag != Result::kind::undefined) { return 300; }
        saved.reset();
        if (saved_lifetime.expired() != replacement) { return 301; }
    }
    ctnative::map_clear(outer);
    if (ctnative::global_number(get()) != 0 || set(19) != 19 ||
        ctnative::global_number(get()) != 19) { return 302; }
    auto saved = outer->at(js_num{1});
    std::weak_ptr saved_lifetime = saved;
    ctnative::map_clear(outer);
    if (saved_lifetime.expired() || saved->at("value") != 19 ||
        ctnative::global_number(get()) != 0 || set(23) != 23 ||
        ctnative::global_number(get()) != 23 || outer->at(js_num{1}) == saved) { return 303; }
    saved.reset();
    if (!saved_lifetime.expired()) { return 304; }
    const auto next = ctn_test_maps.size();
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != next + initial_maps ||
        ctn_test_maps[0].lock() == ctn_test_maps[next].lock() ||
        ctnative::global_number(get()) != 23) { return 305; }
    outer.reset();
    get = {};
    if (ctn_test_maps[0].expired() || set(31) != 31) { return 306; }
    set = {};
    if (ctn_test_maps[0].expired() || poison() != 0) { return 307; }
    poison = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[next].expired()) { return 308; }
    g_host.reset();
    for (const auto & map : ctn_test_maps) {
        if (!map.expired()) { return 309; }
    }
    return 0;
}
"""
    initial_maps = 2 + row["replacement"] * (1 + (row["expected_trace"] == "undefined"))
    return (
        changed.replace("REPLACEMENT", str(row["replacement"]).lower())
        .replace("INITIAL_MAPS", str(initial_maps))
        .replace("INITIAL_TAG", "undefined" if row["expected_trace"] == "undefined" else "number")
    )


def nested_map_census(args, ir, name, row):
    raw = (args.work / f"{name}.raw.mlir").read_text()
    prepared = ir.read_text()
    if len(boundary.FUNCTION.findall(raw)) != row["functions"]:
        raise RuntimeError(f"{name}: changed source function census")
    for text in (raw, prepared):
        if len(source_calls(text)) != row["calls"]:
            raise RuntimeError(f"{name}: changed source call census")
    for operation in (
        "create_object",
        "construct",
        "get_property",
        "set_property",
        "load_global",
        "store_global",
    ):
        if raw.count(operation) != prepared.count(operation):
            raise RuntimeError(f"{name}: preparation changed {operation} census")


def nested_map_preserved(original, output, name, prepared=False):
    if prepared:
        # Successful ownership lifts the environment into each direct call. Check
        # that actual receiver/callee/capture edges and every source effect survive.
        calls = re.findall(
            r"^\s*(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\) "
            r"\{ctnative\.stored_call = 1 : i32\}",
            output,
            re.M,
        )
        targets = (
            (["fn$3", "fn$4", "fn$5"] if "before" in name else ["fn$5", "fn$3", "fn$4"])
            if "cross_" in name
            else ["fn$3"]
        )
        arities = [4, 5, 4] if "cross_" in name else [5]
        previous = "previous_" in name
        if previous:
            targets = ["fn$4", "fn$3"] if "before" in name else ["fn$3", "fn$4"]
            arities = [5, 4]
        entry = output.split("\n  }", 1)[0]
        reads = dict(re.findall(r"(%[-\w.$]+) = ctjs\.get_property (%[-\w.$]+)\[", entry))
        captures = dict(re.findall(r"(%[-\w.$]+) = ctjs\.load_upvalue (%[-\w.$]+)\[0\]", entry))
        actuals = [args.split(", ") for _, _, args in calls]
        if (
            len(source_calls(original)) != len(source_calls(output))
            or [target for _, target, _ in calls] != targets
            or [len(args) for args in actuals] != arities
            or any(
                reads.get(args[2]) != args[0] or captures.get(args[3]) != args[2]
                for args in actuals
            )
            or f'ctjs.store_global "trace", {calls[0 if previous else -1][0]}' not in output
        ):
            raise RuntimeError(f"{name}: nullable refusal lost a prepared source call edge")
        if previous or "cross_mixed_" in name:
            number = actuals[0 if previous else 1][-1]
            if f"{number} = ctjs.constant #ctjs.number<4630967054332067840>" not in entry:
                raise RuntimeError(f"{name}: mixed refusal lost its original Number actual")
        for op in (
            "ctjs.create_object",
            "ctjs.construct",
            "ctjs.get_property",
            "ctjs.set_property",
            "ctjs.load_global",
            "ctjs.store_global",
            "ctjs.compare",
            "ctjs.unary",
            "ctjs.binary",
            "ctjs.truthy",
            "scf.if",
            "scf.yield",
        ):
            pattern = r"^\s*(?:%[-\w.$]+(?::\d+)? = )?" + re.escape(op) + r"\b"
            if len(re.findall(pattern, original, re.M)) != len(re.findall(pattern, output, re.M)):
                raise RuntimeError(f"{name}: nullable refusal changed source {op} census")
        return
    check_call_preservation(original, output, name)
    pattern = (
        r"^\s*((?:%[-\w.$]+(?::\d+)? = )?(?:ctjs\.(?:create_object|construct|"
        r"get_property|set_property|load_global|store_global|compare|unary|binary|truthy)|"
        r"scf\.(?:if|yield))\b[^\n{]*)"
    )
    if [line.strip() for line in re.findall(pattern, original, re.M)] != [
        line.strip() for line in re.findall(pattern, output, re.M)
    ]:
        raise RuntimeError(f"{name}: refusal changed original nested Map or branch edges")


def check_nested_maps(args, node, reference, compilers, nm):
    cases = nested_map_cases()
    observations = mutations = 0

    def observe(name, source, expected, extra=()):
        js = args.work / f"{name}-observed.js"
        js.write_text(source)
        result = host.run([str(reference), str(js)]) if reference else None
        values = {"trace": expected, **dict(extra)}
        expected_text = "".join(
            f"{key}={str(value).lower()}\n" for key, value in sorted(values.items())
        )
        kinds = [
            (
                "boolean"
                if isinstance(value, bool)
                else value if value in ("null", "undefined") else "number"
            )
            for value in values.values()
        ]
        types = (
            "("
            + ", ".join(
                f"{kinds.count(tag)} {tag}"
                for tag in ("number", "boolean", "string", "null", "undefined")
            )
            + ")"
        )
        if (
            host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), json.dumps(sorted(values))]).stdout
            != expected_text
            or result
            and (result.stdout != expected_text or types not in result.stderr)
        ):
            raise RuntimeError(f"{name}: typed source observation changed")

    for name, row in cases.items():
        extra = row.get("observations", {})
        observe(name, row["source"], row["expected_trace"], extra)
        observations += 1 + len(extra)
        if "poison" in row:
            mutation, expected = row["poison"]
            assert row["source"].count(mutation) == 1, (name, mutation)
            observer = (
                "\nhost.slot.set(73); host.slot.poison();\n"
                "trace = host.slot.get() === " + expected + " ? 1 : 0;\n"
            )
            observe(name + "-poisoned", row["source"] + observer, 1)
            observe(name + "-mutation-removed", row["source"].replace(mutation, "") + observer, 0)
            observations += 2
            mutations += 1
        if "previous_poison" in row:
            mutation, expected = row["previous_poison"]
            assert row["source"].count(mutation) == 1, (name, mutation)
            observer = "\ntrace = host.slot.get(73) === " + expected + " ? 1 : 0;\n"
            observe(name + "-poisoned", row["source"] + observer, 1)
            observe(name + "-mutation-removed", row["source"].replace(mutation, "") + observer, 0)
            observations += 2
            mutations += 1
        if not row["admitted"] and not row["previous"] and not row["dynamic"]:
            continue
        observer = caller_payload_observer if row.get("caller_payload") else nested_map_observer
        observed = observer(row["source"], row)
        observe(name + "-future", observed, 1, extra)
        observations += 1 + len(extra)
        readback = "again.get('value')" if row["repeated"] else "saved.get('value')"
        replacements = [
            ("t.get(1).get('value')" if row["separate"] else readback, "41"),
            (".set('value', value);", ".set('value', 0);"),
        ]
        if row.get("alias"):
            replacements.append(("t.set(2, new Map);", "t.set(2, t.get(1));"))
        elif row["children"] == 2:
            replacements.append(("second = new Map;", "second = first;"))
        if row["retained"]:
            replacements.append(
                ("t.set(1, child);", "t.set(1, child); t.clear();")
                if row["separate"]
                else ("return " + readback, "t.clear(); return " + readback)
            )
        if row["separate"]:
            replacements.append(("return value;", "return 0;"))
        if row["reused"]:
            replacements.append(("t.has(1) || t.set(1, new Map);", "t.set(1, new Map);"))
        if row["repeated"]:
            replacements.append(
                ("const again = t.get(1);", "t.set(1, new Map); const again = t.get(1);")
            )
        if row["previous"]:
            readback = "return prior === void 0 ? saved.get('value') : prior;"
            replacements = [
                (readback, "return value;"),
                (readback, "return prior;"),
                ("const prior = saved.get('value');", "const prior = void 0;"),
                ("saved.set('value', value);", "saved.set('value', 0);"),
                (readback, "t.clear(); " + readback),
                ("t.has(1) || t.set(1, new Map);", "t.set(1, new Map);"),
            ]
        if row["dynamic"]:
            readback = "return t.get(1).get(key) || null;"
            replacements = [
                (readback, "return 41;"),
                (readback, "return t.get(1).get(key);"),
                ("t.get(1).set(key, value);", "t.get(1).set(key, 0);"),
                ("t.has(1) || t.set(1, new Map);", "t.set(1, new Map);"),
                ("saved.delete(key);", "saved.has(key);"),
                ("if (!saved.size) { t.delete(1); }", ""),
            ]
        if row.get("caller_payload"):
            replacements = [
                ("t.set(1, e);", "t.set(1, {});"),
                ("t.set(1, e);", "t.set(1, 1);"),
                ("t.set(1, e);", "t.has(1);"),
                ("t.delete(1)", "t.has(1)"),
                ("clear() { t.clear();", "clear() { t.size;"),
                ("{value: 64}", "{value: 0}"),
            ]
            if row["global_alias"]:
                replacements[-1] = ("alias.value = 65;", "alias.value = 0;")
        if row.get("mixed_child"):
            replacements = [
                ("t.get(1).get('value')", "64"),
                ("t.get(1).set('value', value);", "t.get(1).set('value', 0);"),
            ]
            if "child_mutation" in row:
                replacements.append(
                    (
                        "poison() { " + row["child_mutation"] + " return 0; }",
                        "poison() { return 0; }",
                    )
                )
            if row.get("mixed_child_kind") == "returned":
                replacements.append((" || null;", ";"))
        for index, (old, replacement) in enumerate(replacements):
            assert row["source"].count(old) == 1, (name, old)
            blinded = args.work / f"{name}-blinded-{index}.js"
            blinded.write_text(observer(row["source"].replace(old, replacement), row))
            result = subprocess.run(
                [node, "-e", boundary.NODE, str(blinded)],
                capture_output=True,
                text=True,
                timeout=30,
            )
            if not result.returncode and result.stdout == "trace=1\n":
                raise RuntimeError(f"{name}: observer cannot distinguish {replacement}")
            mutations += 1

    def check_case(item):
        name, row = item

        def observed_contract(ir, label):
            config = contract(args, ir, label)
            if row.get("observations"):
                value = json.loads(config.read_text())
                value["observations"] = sorted(["trace", *row["observations"]])
                config.write_text(json.dumps(value, indent=2) + "\n")
            return config

        _, ir, functions = boundary.prepare(args, name, row["source"])
        assert functions == row["functions"]
        nested_map_census(args, ir, name, row)
        config = observed_contract(ir, name)
        for policy, options in (("default", ""), ("disabled", "optimize=false")):
            label = name + "-" + policy
            if not row["admitted"]:
                output = owned.lower(args, ir, label, config, options=options, cleanup=False)
                methods.census(output, functions, label, admitted=0)
                if row.get("refusal") and row["refusal"] not in output.read_text():
                    raise RuntimeError(f"{label}: lost independent mixed carrier refusal")
                nested_map_preserved(ir.read_text(), output.read_text(), label, row["owner"])
                if ("ctnative.host_owner_proved = true" in output.read_text()) != row["owner"]:
                    raise RuntimeError(f"{label}: nullable ownership outcome changed")
            else:
                output = owned.lower(args, ir, label, config, options=options)
                text = methods.census(output, functions, label, admitted=functions)
                if "ctnative.host_owner_proved = true" not in text:
                    raise RuntimeError(f"{label}: lost captured child ownership")
                if policy == "default":
                    default = output
                elif output.read_text() != default.read_text():
                    raise RuntimeError(f"{label}: child proof depends on optimization policy")
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
            nested_map_preserved(forged.read_text(), stale.read_text(), label)
            fresh = observed_contract(forged, label + "-forged")
            checked = owned.lower(
                args, forged, label + "-fresh", fresh, options=options, cleanup=row["admitted"]
            )
            methods.census(checked, functions, label, admitted=functions if row["admitted"] else 0)
            if not row["admitted"]:
                if row.get("refusal") and row["refusal"] not in checked.read_text():
                    raise RuntimeError(f"{label}: forged reports changed mixed carrier refusal")
                nested_map_preserved(forged.read_text(), checked.read_text(), label, row["owner"])
                if ("ctnative.host_owner_proved = true" in checked.read_text()) != row["owner"]:
                    raise RuntimeError(f"{label}: forged presence changed nullable ownership")
            elif comparable_provenance(
                host.run([args.translate, "--mlir-to-cpp", str(checked)]).stdout, forged
            ) != comparable_provenance(
                host.run([args.translate, "--mlir-to-cpp", str(output)]).stdout, ir
            ):
                raise RuntimeError(f"{label}: forged leaf/presence facts changed nested C++")
        if not row["admitted"]:
            return
        values = {"trace": row["expected_trace"], **row.get("observations", {})}
        expected_output = (
            "".join(f"{key}={str(value).lower()}\n" for key, value in sorted(values.items())) * 2
        )
        deduced = args.work / f"{name}.deduced.mlir"
        host.run([args.opt, str(default), "--ctnative-print-deduced", "-o", str(deduced)])
        for mode, native in (("explicit", default), ("deduced", deduced)):
            cpp = host.run([args.translate, "--mlir-to-cpp", str(native)]).stdout
            signature = (
                "std::function<js_num(js_num, std::string)>"
                if row["dynamic"]
                else (
                    "std::function<js_num(ctnative::object_value)>"
                    if row.get("mixed_child") or row.get("caller_payload") and row["mixed"]
                    else (
                        "std::function<js_num(std::shared_ptr<ctnative::identity_object>)>"
                        if row.get("caller_payload")
                        else (
                            "std::function<ctnative::nullable_scalar()>"
                            if row.get("nullable")
                            else (
                                "std::function<ctnative::nullable_scalar(js_num)>"
                                if row["previous"] or row.get("alias")
                                else "std::function<js_num(js_num)>"
                            )
                        )
                    )
                )
            )
            if owned.VM.search(cpp) or signature not in cpp:
                raise RuntimeError(f"{name}/{mode}: lost typed standalone nested Map output")
            generated = args.work / f"{name}.{mode}.cpp"
            generated.write_text(cpp)
            source = args.work / f"{name}.{mode}.lifetime.cpp"
            source.write_text(nested_map_lifetime_cpp(cpp, row))
            for index, compiler in enumerate(compilers):
                binary = source.with_suffix(f".{index}").resolve()
                host.run([compiler, *owned.FLAGS, str(source), "-o", str(binary)])
                if (
                    owned.VM.search(host.run([nm, "-C", str(binary)]).stdout)
                    or host.run([str(binary)]).stdout != expected_output
                ):
                    raise RuntimeError(f"{name}/{mode}: standalone child lifetime mismatch")
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
                env={
                    **os.environ,
                    "ASAN_OPTIONS": "detect_stack_use_after_return=1:detect_leaks=1",
                    "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1",
                },
            )
            if result.returncode or result.stdout != expected_output or result.stderr:
                raise RuntimeError(
                    f"{name}/{mode}: sanitized child lifetime failed\n"
                    f"{result.returncode}: {result.stdout}{result.stderr}"
                )
        if name in {
            "nested_map_retained",
            "nested_map_distinct_saved",
            "nested_map_conditional_initialize",
            "nested_map_cross_invocation",
            "nested_map_repeated_lookup",
            "nested_map_cross_inverted_guard",
            "nested_map_conditional_unknown_contents",
            "nested_map_dynamic_nullable",
            "nested_map_previous_observed",
            "nested_map_dynamic_nullable_observed",
            "nested_map_caller_payload_fields_observed",
            "nested_map_caller_payload_number_last_observed",
            "nested_map_mixed_child_identity",
            "nested_map_mixed_child_replace",
            "nested_map_mixed_child_returned_identity",
        }:
            check_budgets(args, ir, config, name, functions=functions)

    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        list(executor.map(check_case, cases.items()))
    positives = sum(row["admitted"] for row in cases.values())
    print(
        f"nested Maps: {positives} native programs, {len(cases) - positives} refusals, "
        f"{observations} typed observations, {mutations} distinguishing mutations"
    )
