"""Captured outer Maps retain fresh and reused child Map owners."""

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
        admitted=True,
    )
    rows["nested_map_mixed_child_returned_field"].update(
        mixed_child=True,
        mixed_child_kind="returned",
        entry_fields=1,
        entry_field_mutations=[
            ("var traceObject = host.slot.get().value;", "var traceObject = 0;"),
        ],
        expected_trace=True,
        observations={"traceMissing": True, "traceNumber": True, "traceObject": 64},
    )
    original_field = rows["nested_map_mixed_child_returned_field"]["source"]
    assert len(original_field.encode()) == 595
    assert hashlib.sha256(original_field.encode()).hexdigest() == (
        "9d4c8b9c5e52297f6dd7f37f2a3a8c1a955f0d8348912cf35365de1ec00f57a0"
    )
    # Strict identity refines this exact lexical result, never every later get().
    # Script-scope const is a global in this importer, so each saved result lives
    # in its own block; the public scalar observations remain ordinary globals.
    for name, expression in (
        ("same", "saved === key ? saved.value : 0"),
        ("reversed", "key === saved ? saved.value : 0"),
        ("inverted", "!(saved === key) ? 0 : saved.value"),
    ):
        source = returned
        for observation, comparison in (
            ("traceMissing", "null"),
            ("traceNumber", "64"),
            ("traceObject", "key"),
        ):
            source = source.replace(
                f"var {observation} = host.slot.get() === {comparison};",
                "{ const saved = host.slot.get(); " f"var {observation} = {expression}; }}",
            )
        add("returned_fields_" + name, source, 16, functions=5, admitted=True)
        row = rows["nested_map_returned_fields_" + name]
        row.update(
            mixed_child=True,
            mixed_child_kind="returned",
            entry_fields=3,
            expected_trace=True,
            observations={"traceMissing": 0, "traceNumber": 0, "traceObject": 64},
            entry_field_mutations=[
                (f"var traceObject = {expression};", "var traceObject = 0;"),
                (f"var traceNumber = {expression};", "var traceNumber = saved.value;"),
                (
                    f"var traceMissing = {expression};",
                    "var traceMissing = saved.value;",
                ),
            ],
        )
    source = rows["nested_map_returned_fields_same"]["source"]
    object_read = "var traceObject = saved === key ? saved.value : 0;"
    for name, changed, calls, observations in (
        (
            "truthy",
            source.replace("saved === key ? saved.value : 0", "saved ? saved.value : 0"),
            16,
            {"traceMissing": 0, "traceNumber": "undefined", "traceObject": 64},
        ),
        (
            "other_lookup",
            source.replace(
                object_read,
                "var traceObject = host.slot.get() === key ? saved.value : 0;",
            ),
            17,
            {"traceMissing": 0, "traceNumber": 0, "traceObject": 64},
        ),
        (
            "other_receiver",
            source.replace(
                object_read,
                "const other = host.slot.get(); "
                "var traceObject = saved === key ? other.value : 0;",
            ),
            17,
            {"traceMissing": 0, "traceNumber": 0, "traceObject": 64},
        ),
        (
            "missing_field",
            source.replace(object_read, "var traceObject = saved === key ? saved.missing : 0;"),
            16,
            {"traceMissing": 0, "traceNumber": 0, "traceObject": "undefined"},
        ),
        (
            "scalar_guard",
            source.replace(object_read, "var traceObject = saved === 64 ? saved.value : 0;"),
            16,
            {"traceMissing": 0, "traceNumber": 0, "traceObject": 0},
        ),
    ):
        add("returned_fields_" + name, changed, calls, functions=5)
        rows["nested_map_returned_fields_" + name].update(
            expected_trace=True,
            observations=observations,
        )
        if name in {"other_lookup", "other_receiver", "scalar_guard"}:
            rows["nested_map_returned_fields_" + name].update(
                admitted=True,
                owner=True,
                mixed_child=True,
                mixed_child_kind="returned",
                entry_fields=3,
            )
    # A later call's exact result never inherits an earlier object's fields.
    # Recreating the child executes the same constructor at another invocation.
    removal = original_field.replace(
        "        get() {", "        remove() { t.delete(1); return 0; },\n        get() {"
    )
    for name, source, calls, functions, admitted in (
        (
            "overwritten_scalar",
            original_field.replace("var traceObject =", "host.slot.set(64); var traceObject ="),
            17,
            5,
            False,
        ),
        (
            "recreated_scalar",
            removal.replace(
                "var traceObject =", "host.slot.remove(); host.slot.set(64); var traceObject ="
            ),
            19,
            6,
            False,
        ),
        (
            "recreated_object",
            removal.replace("host.slot.set(key);", "host.slot.remove(); host.slot.set(key);"),
            18,
            6,
            True,
        ),
        (
            "inactive_unknown_effect",
            original_field.replace("set(value) {", "set(value) { if (false) { unknown(value); }"),
            17,
            5,
            False,
        ),
    ):
        add("returned_fields_" + name, source, calls, functions=functions, admitted=admitted)
        rows["nested_map_returned_fields_" + name].update(
            expected_trace=True,
            observations={
                "traceMissing": True,
                "traceNumber": True,
                "traceObject": "undefined" if name.endswith("scalar") else 64,
            },
        )
        if admitted:
            rows["nested_map_returned_fields_" + name].update(
                mixed_child=True,
                mixed_child_kind="returned",
                entry_fields=1,
                replacement=True,
            )
    return rows
