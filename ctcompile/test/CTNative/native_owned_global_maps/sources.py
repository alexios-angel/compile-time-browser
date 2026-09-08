"""The programs under test and their refusal variants, and the sibling drivers.

Split out of native-owned-global-maps.py on 2026-09-08 (it was 1,013 lines); the
definitions are verbatim, only the two sibling-file paths changed, because this
module lives one directory below them.
"""

import importlib.util
from pathlib import Path


# The directory of the driver scripts, which is this package's parent.
DRIVERS = Path(__file__).resolve().parent

spec = importlib.util.spec_from_file_location(
    "methods", DRIVERS.with_name("native-owned-global-methods.py"))
methods = importlib.util.module_from_spec(spec)
spec.loader.exec_module(methods)
owned, boundary, host = methods.owned, methods.boundary, methods.host
SOURCE = DRIVERS.with_name("native-export-boundary.js").read_text()
SHARED = SOURCE.replace("get() { return state.size; }",
    "get() { return state.size; }, set() { state.set('x', 1); return state.size; }")
SHARED = SHARED.replace("var trace = host.slot.get();",
    "host.slot.set(); var trace = host.slot.get();")
PARAMETER = SHARED.replace("set() { state.set('x', 1)", "set(key) { state.set(key, 1)")
PARAMETER = PARAMETER.replace("host.slot.set();", "host.slot.set('x');")
CALL_RESULT = PARAMETER.replace("host.slot.set('x');", "host.slot.set(host.slot.get());")
SEEDED_RESULT = CALL_RESULT.replace("get() { return state.size; }",
    "get() { state.set(0, 1); return state.get(0); }")
STRING_RESULT = "result-key-" * 12


def parameter_sources():
    return {
        "shared_parameter": (PARAMETER, "host", 1),
        "shared_parameter_repeated": (PARAMETER.replace("host.slot.set('x');",
            "host.slot.set('x'); host.slot.set('y'); host.slot.set('x');"), "host", 2),
        "shared_parameter_number": (PARAMETER.replace("host.slot.set('x');",
            "host.slot.set(1); host.slot.set(2); host.slot.set(1);"), "host", 2),
        "shared_parameter_bool": (PARAMETER.replace("host.slot.set('x');",
            "host.slot.set(true); host.slot.set(false); host.slot.set(true);"), "host", 2),
        "shared_parameter_alias": (PARAMETER.replace("set(key) { state.set(key, 1)",
            "set(key) { const alias = key; state.set(alias, 1)"), "host", 1),
        "shared_two_parameters": (PARAMETER.replace("set(key) { state.set(key, 1)",
            "set(key, value) { state.set(key, value)").replace("host.slot.set('x');",
            "host.slot.set('x', 1); host.slot.set('y', 2); host.slot.set('x', 3);"), "host", 2),
    }


def result_sources():
    observed_size = CALL_RESULT.replace("get() { return state.size; },",
        "size() { return state.size; }, get() { return state.has(false); },")
    observed_size = observed_size.replace("var trace = host.slot.get();",
                                          "var trace = host.slot.size();")
    repeated_bool = observed_size.replace("host.slot.set(host.slot.get());",
        "host.slot.set(host.slot.get()); host.slot.set(host.slot.get());")
    return {
        "parameter_call_result": (CALL_RESULT, "host", 1),
        "result_reverse_members": (CALL_RESULT.replace(
            "get() { return state.size; }, set(key) { state.set(key, 1); return state.size; }",
            "set(key) { state.set(key, 1); return state.size; }, get() { return state.size; }"),
            "host", 1),
        "result_repeated": (CALL_RESULT.replace("host.slot.set(host.slot.get());",
            "host.slot.set(host.slot.get()); host.slot.set(host.slot.get());"), "host", 2),
        "result_alias": (CALL_RESULT.replace("get() { return state.size; }",
            "get() { const result = state.size; return result; }")
            .replace("set(key) { state.set(key, 1)",
                     "set(key) { const alias = key; state.set(alias, 1)"), "host", 1),
        # JS evaluates the two actuals left to right. The first inserts key 0
        # and yields 1; the second inserts key 1 and yields 2. The setter must
        # overwrite key 1. Reversing the actuals instead inserts key 2, making
        # the final growing getter return 4 rather than 3.
        "result_argument_order": (CALL_RESULT.replace("get() { return state.size; }",
            "get() { state.set(state.size, 1); return state.size; }")
            .replace("set(key) { state.set(key, 1)", "set(key, value) { state.set(key, value)")
            .replace("host.slot.set(host.slot.get());",
                     "host.slot.set(host.slot.get(), host.slot.get());"), "host", 3),
        "result_bool": (repeated_bool, "host", 2),
        "result_delete": (observed_size.replace("state.has(false)", "state.delete(false)")
            .replace("host.slot.set(host.slot.get());",
                     "host.slot.set(host.slot.get()); " * 3), "host", 2),
        "result_string": (observed_size.replace("return state.has(false);",
            "state.set('produced', 1); return '" + "result-key-" * 12 + "';"), "host", 2),
        "result_formal": (observed_size.replace("get() { return state.has(false); }",
            "get(key) { state.has(key); return key; }")
            .replace("host.slot.set(host.slot.get());", "host.slot.set(host.slot.get(7));"),
            "host", 1),
    }


def seeded_result_sources():
    observed_size = CALL_RESULT.replace("get() { return state.size; },",
        "size() { return state.size; }, get() { state.set(0, 1); return state.get(0); },")
    observed_size = observed_size.replace("var trace = host.slot.get();",
                                          "var trace = host.slot.size();")
    return {
        "result_seeded_map_get": (SEEDED_RESULT, "host", 1),
        "result_seeded_repeated": (SEEDED_RESULT.replace("host.slot.set(host.slot.get());",
            "host.slot.set(host.slot.get()); host.slot.set(host.slot.get());"), "host", 1),
        "result_seeded_overwrite": (SEEDED_RESULT.replace("state.set(0, 1);",
            "state.set(0, 1); state.set(0, 2);"), "host", 2),
        "result_seeded_growing": (SEEDED_RESULT.replace(
            "state.set(0, 1); return state.get(0);",
            "const key = state.size; state.set(key, key); return state.get(key);"), "host", 1),
        # Distinct current actuals prevent the formal from being proved as a
        # single literal. Each invocation must match its own set/get SSA key.
        "result_seeded_formal": (observed_size.replace(
            "get() { state.set(0, 1); return state.get(0); }",
            "get(key) { state.set(key, 1); return state.get(key); }")
            .replace("host.slot.set(host.slot.get());",
                     "host.slot.set(host.slot.get(7)); host.slot.set(host.slot.get(8));"),
            "host", 3),
    }


def key_fact_sources():
    return {
        "seeded_earlier_key": (SEEDED_RESULT.replace("return state.get(0);",
            "state.set(1, 2); return state.get(0);"), "host", 1),
        "seeded_other_delete": (SEEDED_RESULT.replace("return state.get(0);",
            "state.delete(9); return state.get(0);"), "host", 1),
        "seeded_earlier_overwrite": (SEEDED_RESULT.replace("return state.get(0);",
            "state.set(1, 9); state.set(0, 2); state.set(1, 3); return state.get(0);"), "host", 2),
        "seeded_reseed": (SEEDED_RESULT.replace("return state.get(0);",
            "state.delete(0); state.set(0, 3); state.set(1, 2); return state.get(0);"), "host", 3),
        "seeded_many_keys": (SEEDED_RESULT.replace("return state.get(0);",
            " ".join(f"state.set({key}, {key + 1});" for key in range(1, 9))
            + " return state.get(0);"), "host", 1),
        "seeded_string_keys": (SEEDED_RESULT.replace("state.set(0, 1); return state.get(0);",
            "state.set('seed', 1); state.set('other', 2); state.delete('missing'); "
            "return state.get('seed');").replace("state.set(key, 1)", "state.set('sink', key)"),
            "host", 1),
        "seeded_bool_keys": (SEEDED_RESULT.replace("state.set(0, 1); return state.get(0);",
            "state.set(false, 1); state.set(true, 2); state.delete(true); return state.get(false);")
            .replace("state.set(key, 1)", "state.set(true, key)"), "host", 1),
    }


def joined_result_sources():
    dynamic = SEEDED_RESULT.replace("return state.get(0);",
        "state.set(state.size, 2); return state.get(0);")
    observed_size = dynamic.replace("get() {",
        "size() { return state.size; }, get() {").replace("var trace = host.slot.get();",
                                                         "var trace = host.slot.size();")
    return {
        "seeded_dynamic_write": (dynamic, "host", 1),
        # The first getter's size key really overwrites key 1, producing 2.
        # Reusing the old payload 1 changes the setter's key and leaves one
        # entry rather than two. Observe the final setter's returned size.
        "seeded_dynamic_overwrite": (dynamic.replace("state.set(0, 1)", "state.set(1, 1)")
            .replace("state.get(0)", "state.get(1)")
            .replace("var trace = host.slot.get();",
                     "var trace = host.slot.set(host.slot.get());"), "host", 2),
        "seeded_dynamic_saved_key": (SEEDED_RESULT.replace(
            "state.set(0, 1); return state.get(0);",
            "const key = state.size; state.set(key, 1); "
            "state.set(state.size, 2); return state.get(key);"), "host", 1),
        "seeded_dynamic_repeated": (dynamic.replace("return state.get(0);",
            "state.set(state.size, 3); return state.get(0);")
            .replace("host.slot.set(host.slot.get());",
                     "host.slot.set(host.slot.get()); host.slot.set(host.slot.get());"), "host", 1),
        "seeded_dynamic_reseed": (dynamic.replace("return state.get(0);",
            "state.set(0, 3); state.set(state.size, 4); return state.get(0);"), "host", 3),
        "seeded_dynamic_runtime_payload": (dynamic.replace("state.set(state.size, 2)",
            "state.set(state.size, state.size)"), "host", 1),
        # Distinct calls make this a numeric formal, never a guessed constant.
        # The first getter's write aliases key 1; the second keeps key 7.
        "seeded_dynamic_formal": (observed_size.replace(
            "get() { state.set(0, 1); state.set(state.size, 2); return state.get(0); }",
            "get(key) { state.set(key, 1); state.set(state.size, 2); return state.get(key); }")
            .replace("host.slot.set(host.slot.get());",
                     "host.slot.set(host.slot.get(1)); host.slot.set(host.slot.get(7));"),
            "host", 4),
        "seeded_dynamic_saved_overwrite": (SEEDED_RESULT.replace(
            "state.set(0, 1); return state.get(0);",
            "const key = state.size; state.set(key, 1); state.set(state.size, 2); "
            "state.set(key, 3); state.set(state.size, 4); return state.get(key);"), "host", 3),
    }


def size_result_sources():
    saved = SEEDED_RESULT.replace("state.set(0, 1); return state.get(0);",
        "const alias = state; alias.set(0, 1); const saved = alias.size; "
        "alias.set(saved, 2); state.delete(saved); return state.get(0);")
    saved = saved.replace("var trace = host.slot.get();",
                          "var trace = host.slot.set(host.slot.get());")
    return {
        "seeded_dynamic_delete": (SEEDED_RESULT.replace("return state.get(0);",
            "state.delete(state.size); return state.get(0);"), "host", 1),
        # The alias reads one immutable size before growing the Map. Re-reading
        # the current size at delete misses the saved key and leaves trace=3.
        "seeded_size_saved": (saved, "host", 2),
        # Removing all known entries cannot revoke a previous nonempty snapshot.
        "seeded_size_saved_empty": (SEEDED_RESULT.replace("return state.get(0);",
            "const saved = state.size; state.delete(0); state.set(0, 3); "
            "state.delete(saved); return state.get(0);"), "host", 3),
    }


def size_result_refusals():
    observed_size = SEEDED_RESULT.replace("var trace = host.slot.get();",
        "var trace = host.slot.set(host.slot.get());")
    return {
        # Reading size before the seed can produce zero, deleting that seed.
        "seeded_size_zero": (observed_size.replace("state.set(0, 1); return state.get(0);",
            "const saved = state.size; state.set(0, 1); state.delete(saved); "
            "return state.get(0);"), 3),
        # A nonempty size can equal a positive literal key.
        "seeded_size_equal_positive": (observed_size.replace(
            "state.set(0, 1); return state.get(0);",
            "state.set(1, 1); state.delete(state.size); return state.get(1);"), 2),
        # Separate nonempty reads can still be the same SameValueZero key.
        "seeded_size_equal_snapshots": (observed_size.replace(
            "state.set(0, 1); return state.get(0);",
            "state.set(0, 1); const first = state.size; const second = state.size; "
            "state.set(first, 2); state.delete(second); return state.get(first);"), 2),
    }


def seeded_carrier_refusals():
    observed_size = SEEDED_RESULT.replace("get() {",
        "size() { return state.size; }, get() {").replace("var trace = host.slot.get();",
                                                         "var trace = host.slot.size();")
    return {
        "result_seeded_string": (observed_size.replace("state.set(0, 1); return state.get(0);",
            f"state.set('seed', '{STRING_RESULT}'); return state.get('seed');")
            .replace("state.set(key, 1)", "state.set(key, 'stored')"), 2),
        "result_seeded_bool": (observed_size.replace("state.set(0, 1); return state.get(0);",
            "state.set(false, true); return state.get(false);")
            .replace("state.set(key, 1)", "state.set(key, true)"), 2),
        "result_seeded_mixed_contents": (observed_size.replace("state.set(0, 1);",
            "state.set(0, true); state.set(0, 1);"), 2),
        "result_seeded_join_reseed": (observed_size.replace("return state.get(0);",
            "state.set(state.size, true); state.set(0, 3); return state.get(0);"), 3),
    }


RESULT_SIGNATURES = {
    "parameter_call_result": ("js_num", "js_num", 5),
    "result_reverse_members": ("js_num", "js_num", 5),
    "result_repeated": ("js_num", "js_num", 5),
    "result_alias": ("js_num", "js_num", 5),
    "result_argument_order": ("js_num", "js_num, js_num", 5),
    "result_bool": ("bool", "bool", 6),
    "result_delete": ("bool", "bool", 6),
    "result_string": ("std::string", "std::string", 6),
    "result_formal": ("js_num", "js_num", 6),
    "result_seeded_map_get": ("js_num", "js_num", 5),
    "result_seeded_repeated": ("js_num", "js_num", 5),
    "result_seeded_overwrite": ("js_num", "js_num", 5),
    "result_seeded_growing": ("js_num", "js_num", 5),
    "result_seeded_formal": ("js_num", "js_num", 6),
    **{name: ("js_num", "js_num", 5) for name in key_fact_sources()},
    **{name: ("js_num", "js_num", 6 if name == "seeded_dynamic_formal" else 5)
       for name in joined_result_sources()},
    **{name: ("js_num", "js_num", 5) for name in size_result_sources()},
}


def refusal_sources():
    return {
        "mutable_capture": SOURCE.replace("return {", "state = new Map(); return {"),
        "cyclic_payload": SOURCE.replace("return state.size;", "state.set('x', state); return state.size;"),
        "object_payload": SOURCE.replace("return state.size;", "state.set('x', {}); return state.size;"),
        "map_key": SOURCE.replace("return state.size;", "state.set(state, 1); return state.size;"),
        "detached_method": SOURCE.replace("return state.size;", "var set = state.set; set('x', 1); return state.size;"),
        "set_wrong_arity": SOURCE.replace("return state.size;", "state.set('x'); return state.size;"),
        "get_wrong_arity": SOURCE.replace("return state.size;", "state.get('x', 1); return state.size;"),
        "method_escape": SOURCE.replace("return state.size;", "return state.set;"),
        "map_return": SOURCE.replace("return state.size;", "return state.set('x', 1);"),
        "method_replaced": SOURCE.replace("return state.size;", "state.set = 1; return state.size;"),
        "snapshot": SOURCE.replace("return state.size;", "state.keys(); return state.size;"),
        "published_map": SOURCE.replace("return {", "host.map = state; return {"),
        "map_alias": SOURCE.replace("return {", "var saved = state; return {"),
        "replaced_map": "Map = function() {};\n" + SOURCE,
        "late_replaced_map": SOURCE + "\nMap = function() {};",
        "map_prototype": SOURCE.replace("const state", "Map.prototype.extra = 1; const state"),
        "constructed_args": SOURCE.replace("new Map()", "new Map([])"),
        "second_map": SOURCE.replace("return {", "new Map(); return {"),
        "second_factory": SOURCE.replace("host.slot = factory();", "host.slot = factory(); factory();"),
        "published_owner": SOURCE + "\nvar alias = host;",
        "published_table": SOURCE + "\nvar alias = host.slot;",
        "published_callable": SOURCE + "\nvar alias = host.slot.get;",
        "table_replaced": SOURCE + "\nhost.slot = {};",
        "getter_replaced": SOURCE + "\nhost.slot.get = function() { return 7; };",
        "receiver": SOURCE.replace("return state.size;", "return this;"),
        "arguments": SOURCE.replace("return state.size;", "return arguments.length;"),
        "call_argument": SOURCE.replace("host.slot.get();", "host.slot.get(1);"),
        "effect": "var side = 0;\n" + SOURCE.replace("return state.size;", "side = 1; return state.size;"),
        "throw": SOURCE.replace("return state.size;", "throw 7;"),
        "unknown": SOURCE + "\ninspect(host);",
    }


def parameter_refusals():
    return {
        "parameter_missing": PARAMETER + "\nhost.slot.set();",
        "parameter_extra": PARAMETER + "\nhost.slot.set('y', 1);",
        "parameter_heterogeneous": PARAMETER + "\nhost.slot.set(1);",
        "parameter_object": PARAMETER.replace("host.slot.set('x');", "host.slot.set({});"),
        "parameter_callback": PARAMETER.replace("host.slot.set('x');",
            "host.slot.set(function() { return 'x'; });"),
        "parameter_unproved": PARAMETER.replace("host.slot.set('x');", "host.slot.set(host.key);"),
        "parameter_getter_extra": PARAMETER.replace("host.slot.get();", "host.slot.get('x');"),
        "parameter_second_missing": parameter_sources()["shared_two_parameters"][0]
                                    + "\nhost.slot.set('z');",
        "parameter_second_heterogeneous": parameter_sources()["shared_two_parameters"][0]
                                          + "\nhost.slot.set('z', true);",
    }


def result_refusals():
    return {
        "result_unknown_map_get": CALL_RESULT.replace("get() { return state.size; }",
            "get() { return state.get(0); }"),
        "result_unknown_effect": CALL_RESULT.replace("get() { return state.size; }",
            "get() { inspect(state); return state.size; }"),
        "result_unknown_call": CALL_RESULT.replace("get() { return state.size; }",
            "get() { state.has(0); return inspect(); }"),
        "result_recursive": CALL_RESULT.replace("get() { return state.size; }",
            "get() { state.has(0); return host.slot.get(); }"),
        "result_mixed_return": CALL_RESULT.replace("get() { return state.size; }",
            "get() { if (state.has(0)) { return 1; } return false; }"),
        "result_mixed_actual": CALL_RESULT + "\nhost.slot.set('x');",
        "result_missing_actual": CALL_RESULT.replace("get() { return state.size; }",
            "get(key) { state.has(key); return state.size; }"),
        "result_map_return": CALL_RESULT.replace("get() { return state.size; }",
            "get() { return state; }"),
    }


def seeded_result_refusals():
    # A nonempty size is distinct from key zero. Seed key one so these writes
    # really can overwrite the queried entry with an incompatible payload.
    aliasing = SEEDED_RESULT.replace("state.set(0, 1)", "state.set(1, 1)")
    aliasing = aliasing.replace("state.get(0)", "state.get(1)")
    return {
        "seeded_missing_key": SEEDED_RESULT.replace("return state.get(0);", "return state.get(1);"),
        "seeded_cleared": SEEDED_RESULT.replace("return state.get(0);",
            "state.clear(); return state.get(0);"),
        "seeded_deleted": SEEDED_RESULT.replace("return state.get(0);",
            "state.delete(0); return state.get(0);"),
        "seeded_dynamic_bool_join": aliasing.replace("return state.get(1);",
            "state.set(state.size, true); return state.get(1);"),
        "seeded_dynamic_string_join": aliasing.replace("return state.get(1);",
            "state.set(state.size, 'other'); return state.get(1);"),
        "seeded_dynamic_unknown_join": aliasing.replace("return state.get(1);",
            "state.set(state.size, state.get(9)); return state.get(1);"),
        "seeded_dynamic_later_join": aliasing.replace("return state.get(1);",
            "state.set(state.size, true); state.set(state.size, 2); return state.get(1);"),
        "seeded_dynamic_unseeded": SEEDED_RESULT.replace("state.set(0, 1);",
            "state.set(state.size, 2);"),
        "seeded_overwritten_unknown": SEEDED_RESULT.replace("return state.get(0);",
            "state.set(0, state.get(9)); return state.get(0);"),
        "seeded_deleted_earlier": SEEDED_RESULT.replace("return state.get(0);",
            "state.set(1, 2); state.delete(0); return state.get(0);"),
        "seeded_unknown_payload": SEEDED_RESULT.replace("state.set(0, 1);",
            "state.set(0, state.get(9));"),
        "seeded_distinct_formals": SEEDED_RESULT.replace(
            "get() { state.set(0, 1); return state.get(0); }",
            "get(key, other) { state.set(key, 1); return state.get(other); }")
            .replace("host.slot.get()", "host.slot.get(7, 8)"),
    }
