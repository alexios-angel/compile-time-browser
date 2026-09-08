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
OTHER_STRING_RESULT = "other-result-key-" * 12


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
    two_saved = SEEDED_RESULT.replace("state.set(0, 1); return state.get(0);",
        "const alias = state; alias.set(0, 1); alias.set(1, 2); const saved = alias.size; "
        "alias.set(saved, 3); state.delete(saved); return state.get(1);")
    observed_size = SEEDED_RESULT.replace("var trace = host.slot.get();",
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
        "seeded_size_two_entries": (SEEDED_RESULT.replace("state.set(0, 1); return state.get(0);",
            "state.set(0, 1); state.set(1, 2); state.delete(state.size); "
            "return state.get(1);"), "host", 2),
        "seeded_size_three_entries": (SEEDED_RESULT.replace("state.set(0, 1); return state.get(0);",
            "state.set(0, 1); state.set(1, 2); state.set(2, 3); "
            "state.delete(state.size); return state.get(2);"), "host", 3),
        # The alias reads size >= 2 before growing the Map. Both later calls
        # must delete that saved key; rereading current size leaves trace=4.
        "seeded_size_two_saved": (two_saved.replace("var trace = host.slot.get();",
            "var trace = host.slot.set(host.slot.get());"), "host", 3),
        # Remove both seeds and the prior setter's key before reseeding. The
        # saved bound still excludes key 1 after every definite entry is gone.
        "seeded_size_two_saved_empty": (observed_size.replace(
            "state.set(0, 1); return state.get(0);",
            "state.set(0, 1); state.set(1, 2); const saved = state.size; "
            "state.delete(0); state.delete(1); state.delete(3); "
            "state.set(1, 3); state.delete(saved); return state.get(1);"),
            "host", 2),
        # Deleting one known key leaves two distinct definite entries. The
        # numeric payload 7 makes suppressing that real delete observable.
        "seeded_size_after_delete": (observed_size.replace(
            "state.set(0, 1); return state.get(0);",
            "state.set(0, 1); state.set(1, 7); state.set(2, 3); state.delete(2); "
            "state.delete(state.size); return state.get(1);"), "host", 3),
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
        # Three tracked SSA keys may name only two actual entries. In the
        # first call both size snapshots equal 1 and deletion removes key 2.
        "seeded_size_aliasing_facts": (observed_size.replace(
            "state.set(0, 1); return state.get(0);",
            "state.set(2, 1); const first = state.size; const second = state.size; "
            "state.set(first, 2); state.set(second, 3); state.delete(state.size); "
            "return state.get(2);"), 4),
        "seeded_size_duplicate_zero": (observed_size.replace(
            "state.set(0, 1); return state.get(0);",
            "state.set(0, 1); state.set(0, 2); state.set(2, 3); "
            "state.delete(state.size); return state.get(2);"), 4),
        "seeded_size_removed_entry": (observed_size.replace(
            "state.set(0, 1); return state.get(0);",
            "state.set(0, 1); state.set(1, 2); state.delete(0); "
            "state.delete(state.size); return state.get(1);"), 3),
        "seeded_size_before_second_entry": (observed_size.replace(
            "state.set(0, 1); return state.get(0);",
            "state.set(1, 1); const saved = state.size; state.set(0, 2); "
            "state.delete(saved); return state.get(1);"), 3),
        "seeded_size_equal_bound": (observed_size.replace(
            "state.set(0, 1); return state.get(0);",
            "state.set(0, 1); state.set(2, 2); state.delete(state.size); "
            "return state.get(2);"), 3),
        "seeded_size_equal_two_snapshots": (observed_size.replace(
            "state.set(0, 1); return state.get(0);",
            "state.set(0, 1); state.set(1, 2); const first = state.size; "
            "const second = state.size; state.set(first, 3); state.delete(second); "
            "return state.get(first);"), 3),
        "seeded_size_removed_seeds": (observed_size.replace(
            "state.set(0, 1); return state.get(0);",
            "state.set(0, 1); state.set(1, 2); state.delete(0); state.delete(1); "
            "state.set(1, 3); state.delete(state.size); return state.get(1);"), 3),
    }


def payload_result_sources():
    observed_size = SEEDED_RESULT.replace("get() {",
        "size() { return state.size; }, get() {").replace("var trace = host.slot.get();",
                                                         "var trace = host.slot.size();")
    boolean = observed_size.replace("state.set(0, 1); return state.get(0);",
        "state.set(false, true); return state.get(false);")
    boolean = boolean.replace("state.set(key, 1)", "state.set(key, true)")
    false_value = boolean.replace("state.set(false, true)", "state.set(false, false)")
    string = observed_size.replace("state.set(0, 1); return state.get(0);",
        f"state.set('seed', '{STRING_RESULT}'); return state.get('seed');")
    string = string.replace("state.set(key, 1)", "state.set(key, 'stored')")
    return {
        "result_seeded_bool": (boolean, "host", 2),
        "result_seeded_string": (string, "host", 2),
        # False and empty strings overwrite their existing keys. Treating
        # either payload as missing instead creates an undefined key: size 2.
        "result_seeded_false": (false_value, "host", 1),
        "result_seeded_empty_string": (string.replace(
            f"state.set('seed', '{STRING_RESULT}'); return state.get('seed');",
            "state.set('', ''); return state.get('');"), "host", 1),
        "result_seeded_false_overwrite": (false_value.replace("state.set(false, false);",
            "state.set(false, true); state.set(false, false);"), "host", 1),
        # The returned key is present before the read. Its owning string copy
        # must survive both replacement and deletion of the source payload.
        # Returning the new payload or a missing result adds another key.
        "result_seeded_string_saved": (string.replace(
            f"state.set('seed', '{STRING_RESULT}'); return state.get('seed');",
            f"state.set('seed', '{STRING_RESULT}'); state.set('{STRING_RESULT}', 'stored'); "
            "const saved = state.get('seed'); state.set('seed', 'changed'); "
            "state.delete('seed'); return saved;"), "host", 1),
    }


def payload_result_refusals():
    positives = payload_result_sources()
    return {
        "result_seeded_false_deleted": (positives["result_seeded_false"][0]
            .replace("return state.get(false);", "state.delete(false); return state.get(false);")
            .replace("state.set(key, true)", "state.set(false, true); state.set(key, true)"), 2),
        "result_seeded_empty_deleted": (positives["result_seeded_empty_string"][0]
            .replace("return state.get('');", "state.delete(''); return state.get('');")
            .replace("state.set(key, 'stored')", "state.set('', 'stored'); state.set(key, 'stored')"), 2),
    }


def mixed_result_sources():
    observed_size = SEEDED_RESULT.replace("get() {",
        "size() { return state.size; }, get() {").replace("var trace = host.slot.get();",
                                                         "var trace = host.slot.size();")
    false_value = observed_size.replace("state.set(0, 1); return state.get(0);",
        "state.set(1, 0); state.set(true, false); return state.get(true);")
    false_value = false_value.replace("state.set(key, 1)",
        "state.set(false, 1); state.set(key, 1)")
    saved_string = payload_result_sources()["result_seeded_string_saved"][0]
    saved_string = saved_string.replace(f"state.set('seed', '{STRING_RESULT}');",
        f"state.set(false, false); state.set('seed', '{STRING_RESULT}');")
    saved_string = saved_string.replace("state.set('seed', 'changed')",
        "state.set('seed', false)").replace("return saved;", "state.delete(false); return saved;")
    return {
        "result_seeded_mixed_contents": (observed_size.replace("state.set(0, 1);",
            "state.set(0, true); state.set(0, 1);"), "host", 2),
        "result_seeded_join_reseed": (observed_size.replace("return state.get(0);",
            "state.set(state.size, true); state.set(0, 3); return state.get(0);"), "host", 3),
        "result_seeded_bool_string_contents": (payload_result_sources()["result_seeded_bool"][0]
            .replace("state.set(false, true);", "state.set(false, 'old'); state.set(false, true);"),
            "host", 2),
        # SameValueZero compares within a tag: false and numeric zero occupy
        # separate entries, and the produced true key creates a third entry.
        "result_seeded_mixed_false_zero": (observed_size.replace(
            "state.set(0, 1); return state.get(0);",
            "state.set(false, 1); state.set(0, true); return state.get(0);"), "host", 3),
        # Returning false overwrites the setter's false key. Returning zero
        # or missing instead creates a fourth entry in the same mixed Map.
        "result_seeded_mixed_false": (false_value, "host", 3),
        "result_seeded_mixed_empty_key": (observed_size.replace(
            "state.set(0, 1); return state.get(0);",
            "state.set(false, false); state.set('', 'key'); return state.get('');")
            .replace("state.set(key, 1)", "state.set(key, 'stored')"), "host", 3),
        # The saved string owns its bytes after a Boolean replacement, removal
        # of both source entries, and eventually destruction of the entire Map.
        "result_seeded_mixed_string_saved": (saved_string, "host", 1),
    }


def mixed_result_refusals():
    positives = mixed_result_sources()
    return {
        "result_seeded_mixed_false_deleted": (positives["result_seeded_mixed_false_zero"][0]
            .replace("return state.get(0);", "state.delete(0); return state.get(0);"), 2),
        "result_seeded_mixed_string_deleted": (positives["result_seeded_mixed_empty_key"][0]
            .replace("return state.get('');", "state.delete(''); return state.get('');"), 2),
    }


def saved_read_sources():
    boolean = payload_result_sources()["result_seeded_bool"][0]
    getter = "state.set(false, true); return state.get(false);"
    saved = boolean.replace(getter,
        "state.set('', ''); const saved = state.get(''); state.set(false, true); "
        "state.set(false, saved); const result = state.get(false); "
        "state.delete(false); return result;")
    number = mixed_result_sources()["result_seeded_mixed_contents"][0].replace(
        "state.set(0, true); state.set(0, 1); return state.get(0);",
        "state.set(1, 1); const saved = state.get(1); state.set(1, true); "
        "state.set(false, true); state.set(false, saved); const result = state.get(false); "
        "state.delete(false); return result;")
    return {
        # Exact next boundary from HANDOFF: an empty String read passes through
        # a nonliteral write into mixed storage and remains a present String.
        "saved_read_write": (saved, "host", 1),
        # The payload's old scalar value survives mutation of its source entry;
        # its SSA tag must not be recovered from the entry's later contents.
        "saved_read_write_false": (boolean.replace(getter,
            "state.set('', 'old'); state.set(false, false); const saved = state.get(false); "
            "state.set(false, true); state.set('', saved); const result = state.get(''); "
            "state.delete(''); return result;"), "host", 1),
        "saved_read_write_number": (number, "host", 1),
        "saved_read_write_repeated": (saved.replace("host.slot.set(host.slot.get());",
            "host.slot.set(host.slot.get()); host.slot.set(host.slot.get());"), "host", 1),
        # Both intermediate reads must own their String. The first source entry
        # is overwritten and deleted before the saved value is written back;
        # the second is overwritten and deleted before the method returns.
        "saved_read_write_string_saved": (boolean.replace(getter,
            f"state.set('seed', '{STRING_RESULT}'); state.set('{STRING_RESULT}', 'stored'); "
            "const saved = state.get('seed'); state.set('seed', false); state.delete('seed'); "
            "state.set(false, true); state.set(false, saved); const result = state.get(false); "
            "state.set(false, false); state.delete(false); return result;"), "host", 1),
        # Valid Boolean results must keep their actual tag, even after a saved
        # String read exists or has briefly been stored at the same key.
        "saved_read_write_wrong_tag": (saved.replace("state.set(false, saved);",
            "state.set(false, true);"), "host", 2),
        "saved_read_write_overwritten": (saved.replace("const result = state.get(false);",
            "state.set(false, true); const result = state.get(false);"), "host", 2),
    }


def saved_read_refusals():
    saved = saved_read_sources()["saved_read_write"][0]
    return {
        # A later read after deletion is missing even though an earlier read
        # of another key still carries the same saved scalar tag.
        "saved_read_write_deleted": (saved.replace(
            "const result = state.get(false); state.delete(false);",
            "state.delete(false); const result = state.get(false);"), 2,
            "return result;", "return saved;"),
        "saved_read_write_missing_key": (saved.replace("const result = state.get(false);",
            "const result = state.get(true);"), 2,
            "const result = state.get(true);", "const result = state.get(false);"),
        # Unknown must not become String merely because the Map also stores
        # Strings. Reseeding the empty key in the setter makes undefined differ
        # from the real empty String in the final size observation.
        "saved_read_write_missing_source": (saved.replace(
            "const saved = state.get('');", "state.delete(''); const saved = state.get('');")
            .replace("state.set(key, true)", "state.set('', true); state.set(key, true)"), 2,
            "state.delete('');", "state.has('');"),
    }


def saved_join_sources():
    saved = saved_read_sources()["saved_read_write"][0].replace("get() {", "get(flag) {")
    saved = saved.replace("state.set('', ''); const saved = state.get('');",
        "state.set('', ''); state.set('other', 'future'); "
        "const saved = flag ? state.get('other') : state.get('');")
    saved = saved.replace("host.slot.set(host.slot.get());",
        "host.slot.set(host.slot.get(false)); host.slot.set(host.slot.get(true));")
    distinct = saved.replace("state.set('', '');", "state.set('', 'first');")
    boolean = distinct.replace("state.set('', 'first'); state.set('other', 'future');",
        "state.set('', false); state.set('other', true);")
    boolean = boolean.replace("state.set(false, true); state.set(false, saved);",
        "state.set('', 'changed'); state.set('other', 'changed'); "
        "state.set('temp', 'old'); state.set('temp', saved);")
    boolean = boolean.replace("const result = state.get(false); state.delete(false);",
        "const result = state.get('temp'); state.delete('temp');")
    number = distinct.replace("state.set('', 'first'); state.set('other', 'future');",
        "state.set(0, 2); state.set(1, 3);")
    number = number.replace("flag ? state.get('other') : state.get('')",
        "flag ? state.get(1) : state.get(0)")
    number = number.replace("state.set(false, true); state.set(false, saved);",
        "state.set(0, true); state.set(1, true); "
        "state.set(false, true); state.set(false, saved);")
    owning = saved.replace("state.set('', ''); state.set('other', 'future');",
        f"state.set('', '{STRING_RESULT}'); state.set('other', '{OTHER_STRING_RESULT}'); "
        f"state.set('{STRING_RESULT}', 'stored'); state.set('{OTHER_STRING_RESULT}', 'stored');")
    owning = owning.replace("state.set(false, true);",
        "state.set('', false); state.delete(''); "
        "state.set('other', false); state.delete('other'); state.set(false, true);")
    owning = owning.replace("state.delete(false);", "state.set(false, false); state.delete(false);")
    # Startup sees false only. The typed C++ lifetime caller later selects both
    # branches, after the publishing owner and table have already been released.
    owning = owning.replace(" host.slot.set(host.slot.get(true));", "")
    return {
        # Exact 16-call boundary from the preceding handoff. Both runtime calls
        # must remain: choosing the empty arm unconditionally yields size 2.
        "saved_join": (saved, "host", 3),
        "saved_join_always_empty": (saved.replace(
            "flag ? state.get('other') : state.get('')", "state.get('')"), "host", 2),
        # Distinct result keys make either constant-arm substitution observable.
        "saved_join_distinct": (distinct, "host", 4),
        "saved_join_bool": (boolean, "host", 4),
        "saved_join_number": (number, "host", 4),
        "saved_join_string_saved": (owning, "host", 2),
    }


def saved_join_refusals():
    saved = saved_join_sources()["saved_join_distinct"][0]
    true_key = saved.replace("state.set(key, true)",
        "state.set('future', true); state.set(key, true)")
    false_key = saved.replace("state.set(key, true)",
        "state.set('first', true); state.set(key, true)")
    deleted = true_key.replace("const saved = flag ?",
        "state.delete('other'); const saved = flag ?")
    deleted = deleted.replace("state.set(false, true);",
        "state.set('other', 'restored'); state.set(false, true);")
    return {
        # The untouched arm cannot supply the other arm's presence or scalar
        # type. Preseed the expected result key so undefined stays observable.
        "saved_join_missing_true": (true_key.replace(
            "flag ? state.get('other')", "flag ? state.get('missing')"), 5,
            "state.get('missing')", "state.get('other')", 4),
        "saved_join_missing_false": (false_key.replace(
            ": state.get('');", ": state.get('missing');"), 5,
            "state.get('missing')", "state.get('')", 4),
        # Restoring the source after the read must not restore its old value.
        "saved_join_deleted_true": (deleted, 5,
            "state.delete('other');", "state.has('other');", 4),
        "saved_join_mixed_tags": (true_key.replace(
            "state.set('other', 'future');", "state.set('other', true);"), 5,
            "state.set('other', true);", "state.set('other', 'future');", 4),
    }


def guarded_saved_sources():
    conditional = saved_join_sources()
    saved = conditional["saved_join"][0].replace(
        "const saved = flag ? state.get('other') : state.get('');",
        "if (flag) { state.delete('other'); } "
        "const saved = state.has('other') ? state.get('other') : state.get('');")
    boolean = conditional["saved_join_bool"][0].replace(
        "const saved = flag ? state.get('other') : state.get('');",
        "if (flag) { state.delete('other'); } "
        "const saved = state.has('other') ? state.get('other') : state.get('');")
    number = conditional["saved_join_number"][0].replace(
        "const saved = flag ? state.get(1) : state.get(0);",
        "if (flag) { state.delete(1); } "
        "const saved = state.has(1) ? state.get(1) : state.get(0);")
    owning = conditional["saved_join_string_saved"][0].replace(
        f"state.set('', '{STRING_RESULT}'); state.set('other', '{OTHER_STRING_RESULT}');",
        f"state.set('', '{OTHER_STRING_RESULT}'); state.set('other', '{STRING_RESULT}');")
    owning = owning.replace("const saved = flag ? state.get('other') : state.get('');",
        "if (flag) { state.delete('other'); } "
        "const saved = state.has('other') ? state.get('other') : state.get('');")
    return {
        # Exact 18-call next boundary from HANDOFF. A deletion join loses
        # unconditional membership, but preserves the tag whenever present.
        "guarded_saved_read": (saved, "host", 2),
        "guarded_saved_no_delete": (saved.replace(
            "if (flag) { state.delete('other'); }", "state.has('other');"), "host", 3),
        "guarded_saved_always_empty": (saved.replace(
            "state.has('other') ? state.get('other') : state.get('')", "state.get('')"),
            "host", 1),
        "guarded_saved_bool": (boolean, "host", 4),
        "guarded_saved_number": (number, "host", 4),
        # The false-only startup returns STRING_RESULT. Future calls with true
        # delete the guarded entry and return the other independently owned String.
        "guarded_saved_string_saved": (owning, "host", 2),
    }


def guarded_saved_refusals():
    saved = guarded_saved_sources()["guarded_saved_read"][0]
    guard = "state.has('other') ? state.get('other') : state.get('')"
    deletion = "if (flag) { state.delete('other'); }"
    missing = saved.replace("state.set('other', 'future');", "state.has('other');")
    stale = saved.replace(deletion,
        "const present = state.has('other'); " + deletion)
    stale = stale.replace(guard, "present ? state.get('other') : state.get('')")
    other_map = saved.replace(deletion,
        "const guardState = new Map(); guardState.set('other', true); " + deletion)
    other_map = other_map.replace(guard,
        "guardState.has('other') ? state.get('other') : state.get('')")
    mutated = saved.replace(guard,
        "state.has('other') ? (state.delete('other'), state.get('other')) : state.get('')")
    mutated = mutated.replace("state.set(key, true)",
        "state.set('future', true); state.set(key, true)")
    return {
        # Membership cannot supply a tag absent from the method's live local
        # facts, even when startup always takes the independently typed fallback.
        "guarded_saved_missing_tag": (missing, 1,
            "state.has('other'); " + deletion,
            "state.set('other', 'future'); " + deletion, 2),
        "guarded_saved_wrong_key": (saved.replace(guard,
            "state.has('') ? state.get('other') : state.get('')"), 3,
            "state.has('') ?", "state.has('other') ?", 2),
        "guarded_saved_wrong_map": (other_map, 3,
            "guardState.has('other') ?", "state.has('other') ?", 2),
        # A saved Boolean is not a membership proof after a same-key mutation.
        "guarded_saved_stale_has": (stale, 3,
            "const present = state.has('other'); " + deletion,
            deletion + " const present = state.has('other');", 2),
        "guarded_saved_mutated_arm": (mutated, 3,
            "(state.delete('other'), state.get('other'))", "state.get('other')", 2),
        # Both entries exist here, but the guarded key has different payload
        # tags on the two paths. A has check must not erase that disagreement.
        "guarded_saved_disagreeing_tag": (saved.replace(deletion,
            "if (flag) { state.set('other', true); }"), 4,
            "if (flag) { state.set('other', true); }", deletion, 2),
        # Both syntactic arms still require proof for a literal predicate.
        "guarded_saved_literal_false": (saved.replace(guard,
            "false ? state.get('other') : state.get('')"), 1,
            "false ?", "state.has('other') ?", 2),
        "guarded_saved_literal_true": (saved.replace(guard,
            "true ? state.get('other') : state.get('')"), 3,
            "true ?", "state.has('other') ?", 2),
    }


def shortcircuit_sources():
    guarded = guarded_saved_sources()
    guard = "state.has('other') ? state.get('other') : state.get('')"
    short = "(state.has('other') && state.get('other')) || state.get('')"
    saved = guarded["guarded_saved_read"][0].replace(guard, short)
    distinct = saved.replace("state.set('', '');", "state.set('', 'first');")
    boolean = guarded["guarded_saved_bool"][0].replace(guard, short)
    false_value = boolean.replace("state.set('', false); state.set('other', true);",
        "state.set('', true); state.set('other', false);")
    false_value = false_value.replace("state.set(key, true)",
        "state.set(true, true); state.set(key, true)")
    number = guarded["guarded_saved_number"][0].replace(
        "state.has(1) ? state.get(1) : state.get(0)",
        "(state.has(1) && state.get(1)) || state.get(0)")
    # Observe the present-but-falsy first call separately. A later call taking
    # the fallback would insert its key and mask an incorrect ternary result.
    empty = distinct.replace("state.set('other', 'future');", "state.set('other', '');")
    empty = empty.replace(" host.slot.set(host.slot.get(true));", "")
    zero = number.replace("state.set(1, 3);", "state.set(1, 0);")
    zero = zero.replace(" host.slot.set(host.slot.get(true));", "")
    owning = guarded["guarded_saved_string_saved"][0].replace(guard, short)
    return {
        # Exact eighteen-call source from the preceding handoff. The temporary
        # && result includes false, but only String reaches the truthy || arm.
        "shortcircuit_same_tag": (saved, "host", 2),
        "shortcircuit_distinct": (distinct, "host", 3),
        "shortcircuit_empty_string": (empty, "host", 3),
        "shortcircuit_bool": (boolean, "host", 4),
        "shortcircuit_false": (false_value, "host", 3),
        "shortcircuit_number": (number, "host", 4),
        "shortcircuit_zero": (zero, "host", 3),
        # Only false runs at startup; the lifetime harness invokes both flags
        # after owner release and retains both results past final Map release.
        "shortcircuit_string_saved": (owning, "host", 2),
    }


def shortcircuit_refusals():
    saved = shortcircuit_sources()["shortcircuit_same_tag"][0]
    short = "(state.has('other') && state.get('other')) || state.get('')"
    deletion = "if (flag) { state.delete('other'); }"
    missing = saved.replace("state.set('other', 'future');", "state.has('other');")
    other_map = saved.replace(deletion,
        "const guardState = new Map(); guardState.set('different', true); " + deletion)
    other_map = other_map.replace(short,
        "(guardState.has('other') && state.get('other')) || state.get('')")
    stale = saved.replace("state.set('other', 'future');",
        "const present = state.has('other'); state.set('other', 'future');")
    stale = stale.replace(short, "(present && state.get('other')) || state.get('')")
    mutated = saved.replace(short,
        "(state.has('other') && (state.delete('other'), state.get('other'))) || state.get('')")
    # The unknown call is unexecuted during startup, but its future flag arm
    # remains part of the published method and cannot receive a complete proof.
    effect = saved.replace(deletion, "if (flag) { inspect(state); }")
    effect = effect.replace(" host.slot.set(host.slot.get(true));", "")
    return {
        "shortcircuit_missing_tag": (missing, 1,
            "state.has('other'); " + deletion,
            "state.set('other', 'future'); " + deletion, 2),
        "shortcircuit_wrong_key": (saved.replace(short,
            "(state.has('missing') && state.get('other')) || state.get('')"), 1,
            "state.has('missing')", "state.has('other')", 2),
        "shortcircuit_wrong_map": (other_map, 1,
            "guardState.has('other')", "state.has('other')", 2),
        "shortcircuit_stale_has": (stale, 1,
            "const present = state.has('other'); state.set('other', 'future'); " + deletion,
            "state.set('other', 'future'); " + deletion + " const present = state.has('other');", 2),
        "shortcircuit_mutated_arm": (mutated, 1,
            "(state.delete('other'), state.get('other'))", "state.get('other')", 2),
        "shortcircuit_disagreeing_tag": (saved.replace(deletion,
            "if (flag) { state.set('other', true); }"), 4,
            "if (flag) { state.set('other', true); }", deletion, 2),
        "shortcircuit_missing_fallback": (saved.replace(short,
            "(state.has('other') && state.get('other')) || state.get('missing')"), 3,
            "state.get('missing')", "state.get('')", 2),
        "shortcircuit_nullable": (saved.replace(short,
            "(state.has('other') && state.get('other')) || null"), 3,
            "|| null", "|| state.get('')", 2),
        "shortcircuit_unknown_effect": (effect, 3,
            "if (flag) { inspect(state); }", deletion, 3),
    }


def nullable_result_sources():
    saved = shortcircuit_sources()["shortcircuit_same_tag"][0].replace(
        "return result;", "return result || null;")
    saved = saved.replace("state.set(key, true)", "state.set(key || 'missing', true)")
    threeway = saved.replace("get(flag)", "get(flag, nullish)")
    threeway = threeway.replace("return result || null;",
        "return result || (nullish ? null : (void 0));")
    threeway = threeway.replace("host.slot.get(false)", "host.slot.get(false, false)")
    threeway = threeway.replace("host.slot.get(true)", "host.slot.get(true, true)")
    threeway = threeway.replace("var trace =", "host.slot.set(host.slot.get(true, false)); var trace =")
    owning = saved.replace("state.set('', ''); state.set('other', 'future');",
        f"state.set('', ''); state.set('other', '{STRING_RESULT}'); "
        f"state.set('{STRING_RESULT}', 'stored'); state.set('missing', 'stored');")
    owning = owning.replace("state.set(false, true);",
        "state.set('', false); state.delete(''); "
        "state.set('other', false); state.delete('other'); state.set(false, true);")
    owning = owning.replace("state.delete(false);", "state.set(false, false); state.delete(false);")
    owning = owning.replace(" host.slot.set(host.slot.get(true));", "")
    homogeneous = saved.replace(
        "state.set('', ''); state.set('other', 'future'); "
        "if (flag) { state.delete('other'); } "
        "const saved = (state.has('other') && state.get('other')) || state.get(''); "
        "state.set(false, true); state.set(false, saved); "
        "const result = state.get(false); state.delete(false);",
        "state.set('seed', 'future'); const result = flag ? '' : state.get('seed'); "
        "state.delete('seed');")
    return {
        # Same eighteen calls as nullable_or: only the consuming key is
        # normalized, separating the callable contract from real null Map keys.
        "nullable_normalized": (saved, "host", 3),
        "nullable_ternary": (saved.replace("return result || null;",
            "return result ? result : null;"), "host", 3),
        # void 0 imports as a literal; bare undefined is a separate host read.
        "nullable_undefined": (saved.replace("return result || null;",
            "return result || (void 0);"), "host", 3),
        "nullable_empty": (saved.replace("state.set('other', 'future');",
            "state.set('other', '');").replace(" host.slot.set(host.slot.get(true));", ""), "host", 3),
        "nullable_threeway": (threeway, "host", 3),
        # No Bool key can hide an incorrect nullable-to-String conversion
        # behind the existing Bool/String variant wrapper.
        "nullable_homogeneous_key": (homogeneous, "host", 2),
        # Startup observes String only. A saved getter later returns null on
        # true; both results survive deletion and destruction of their Map.
        "nullable_string_saved": (owning, "host", 2),
    }


NULLABLE_OBSERVATIONS = {
    "nullable_normalized": [("false", "string", "future"), ("true", "null_value", "")],
    "nullable_ternary": [("false", "string", "future"), ("true", "null_value", "")],
    "nullable_undefined": [("false", "string", "future"), ("true", "undefined", "")],
    "nullable_empty": [("false", "null_value", ""), ("true", "null_value", "")],
    "nullable_threeway": [("false, false", "string", "future"),
                          ("true, true", "null_value", ""), ("true, false", "undefined", "")],
    "nullable_homogeneous_key": [("false", "string", "future"), ("true", "null_value", "")],
    "nullable_string_saved": [("false", "string", STRING_RESULT), ("true", "null_value", "")],
}


def nullable_result_refusals():
    saved = nullable_result_sources()["nullable_normalized"][0]
    effect = saved.replace("if (flag) { state.delete('other'); }", "if (flag) { inspect(state); }")
    effect = effect.replace(" host.slot.set(host.slot.get(true));", "")
    return {
        "nullable_unknown_result": ("var unknownResult = null;\n" + saved.replace(
            "return result || null;", "return result || unknownResult;"), 3,
            "return result || unknownResult;", "return result || null;", 3),
        "nullable_unproved_read": (saved.replace("return result || null;",
            "return result || state.get('missing');"), 3,
            "return result || state.get('missing');", "return result || null;", 3),
        "nullable_object_result": (saved.replace("return result || null;",
            "return result || {};"), 3,
            "return result || {};", "return result || null;", 3),
        "nullable_number_result": (saved.replace("return result || null;",
            "return result || 7;"), 3,
            "return result || 7;", "return result || null;", 3),
        # A future true branch is checked even though startup only uses false.
        "nullable_late_effect": (effect, 3,
            "if (flag) { inspect(state); }", "if (flag) { state.delete('other'); }", 3),
    }


def nullable_key_sources():
    normalized = nullable_result_sources()["nullable_homogeneous_key"][0]
    homogeneous = normalized.replace("state.set(key || 'missing', true)", "state.set(key, true)")
    identity = homogeneous.replace("var trace =",
        "host.slot.set(void 0); host.slot.set(''); var trace =")
    mixed = homogeneous.replace("state.delete('seed');",
        "state.delete('seed'); state.set(false, true); state.delete(false);")
    saved = nullable_result_sources()["nullable_normalized"][0]
    owning = homogeneous.replace("state.set('seed', 'future');",
        f"state.set('seed', '{STRING_RESULT}');")
    owning = owning.replace("state.delete('seed');",
        "state.set('seed', 'overwritten'); state.delete('seed');")
    owning = owning.replace("state.set(key, true);", "state.delete(key); state.set(key, true);")
    owning = owning.replace(" host.slot.set(host.slot.get(true));", "")
    owning = owning.replace("var trace =", "host.slot.set(void 0); var trace =")
    return {
        # Keep the exact eleven-call boundary and its thirteen-call identity
        # extension independent of Boolean keys and nullable stored payloads.
        "nullable_key_homogeneous": (homogeneous, "host", 2),
        "nullable_key_identity": (identity, "host", 4),
        "nullable_key_identity_normalized": (identity.replace("state.set(key, true)",
            "state.set(key || 'missing', true)"), "host", 2),
        "nullable_key_second_use": (normalized.replace("state.set(key || 'missing', true)",
            "state.set(key || 'missing', true); state.set(key, true)"), "host", 3),
        "nullable_key_mixed": (mixed, "host", 2),
        "nullable_original_key": (saved.replace("state.set(key || 'missing', true)",
            "state.set(key, true)"), "host", 3),
        # The normalized first use proves its own String key. The original
        # formal keeps null at the second use, widening only the Map schema.
        "nullable_second_key_use": (saved.replace("state.set(key || 'missing', true)",
            "state.set(key || 'missing', true); state.set(key, true)"), "host", 4),
        # False-only startup; later saved methods delete/reinsert all nullable
        # key tags and retain owned bytes after the original caller mutates.
        "nullable_key_string_saved": (owning, "host", 2),
    }


NULLABLE_KEY_CALLS = {
    "nullable_key_homogeneous": 11,
    "nullable_key_identity": 13,
    "nullable_key_identity_normalized": 13,
    "nullable_key_second_use": 12,
    "nullable_key_mixed": 13,
    "nullable_original_key": 18,
    "nullable_second_key_use": 19,
    "nullable_key_string_saved": 12,
}

NULLABLE_OBSERVATIONS.update({
    name: [("false", "string", STRING_RESULT if name == "nullable_key_string_saved" else "future"),
           ("true", "null_value", "")]
    for name in nullable_key_sources()
})


def nullable_payload_sources():
    source = nullable_key_sources()["nullable_key_homogeneous"][0]
    write = source.replace("state.set(key, true)", "state.set(key, key)")
    readback = write.replace("set(key) { state.set(key, key); return state.size; }",
        "set(key) { state.set(key, key); return state.get(key); }")
    identity = readback.replace("var trace =",
        "host.slot.set(void 0); host.slot.set(''); var trace =")
    saved = write.replace("state.set('seed', 'future');",
        f"state.set('seed', '{STRING_RESULT}');")
    saved = saved.replace("state.delete('seed');",
        "state.set('seed', 'overwritten'); state.delete('seed');")
    saved = saved.replace("set(key) { state.set(key, key); return state.size; }",
        "set(key) { state.set(key, key); const saved = state.get(key); "
        "state.set(key, 'overwritten'); state.delete(key); return saved; }")
    saved = saved.replace(" host.slot.set(host.slot.get(true));", "")
    saved = saved.replace("var trace =", "host.slot.set(void 0); var trace =")
    mixed_readback = readback.replace("set(key) { state.set(key, key);",
        "set(key) { state.set('extra', true); state.delete('extra'); state.set(key, key);")
    original_mixed_readback = nullable_key_sources()["nullable_original_key"][0].replace(
        "set(key) { state.set(key, true); return state.size; }",
        "set(key) { state.set(key, key); return state.get(key); }")
    return {
        # Preserve both exact sources from 1c7985a5 before adding observations.
        "nullable_payload_write": (write, "host", 2),
        "nullable_payload_readback": (readback, "host", 2),
        # Full nullable String storage represents a deleted read as Undefined;
        # its return must not reuse either the payload or its absent tag.
        "nullable_payload_deleted": (readback.replace("return state.get(key);",
            "state.delete(key); return state.get(key);"), "host", 0),
        "nullable_payload_identity": (identity, "host", 4),
        # Scalar reads remain independently proved inside the getter even
        # when the full storage schema also contains null and Boolean.
        "nullable_payload_mixed": (nullable_key_sources()["nullable_original_key"][0]
            .replace("state.set(key, true)", "state.set(key, key)"), "host", 3),
        # Startup sees only false; saved methods later run both flags and
        # return an owning payload copied before overwrite and deletion.
        "nullable_payload_saved": (saved, "host", 0),
        # The exact fourteen-call checkpoint broadens storage with a Boolean
        # while each later get retains its independent nullable payload fact.
        "nullable_payload_mixed_readback": (mixed_readback, "host", 2),
        # Preserve the former nineteen-call refusal unchanged: its actual
        # String/Null result is a supported subset of the full Map schema.
        "nullable_mixed_payload_readback": (original_mixed_readback, "host", 3),
        "nullable_payload_mixed_identity": (mixed_readback.replace("var trace =",
            "host.slot.set(void 0); host.slot.set(''); var trace ="), "host", 4),
        # Broader storage and a subsequent same-key Boolean overwrite cannot
        # widen a saved nullable read or leave it borrowing the old entry.
        "nullable_payload_mixed_saved": (saved.replace("state.set(key, 'overwritten');",
            "state.set(key, true);"), "host", 0),
    }


NULLABLE_PAYLOAD_CALLS = {
    "nullable_payload_write": 11,
    "nullable_payload_readback": 12,
    "nullable_payload_deleted": 13,
    "nullable_payload_identity": 14,
    "nullable_payload_mixed": 18,
    "nullable_payload_saved": 14,
    "nullable_payload_mixed_readback": 14,
    "nullable_mixed_payload_readback": 19,
    "nullable_payload_mixed_identity": 16,
    "nullable_payload_mixed_saved": 14,
}

NULLABLE_PAYLOAD_READBACKS = {
    # Keep native input tags/bytes independent from the expected return.
    "nullable_payload_readback": [("'future'", "string", "future", "string", "future"),
                                  ("null", "null_value", "", "null_value", "")],
    **{name: [(argument, tag, value, tag, value) for argument, tag, value in (
        (repr(STRING_RESULT), "string", STRING_RESULT), ("null", "null_value", ""),
        ("void 0", "undefined", ""), ("''", "string", ""))]
       for name in ("nullable_payload_identity", "nullable_payload_saved",
                    "nullable_payload_mixed_readback", "nullable_mixed_payload_readback",
                    "nullable_payload_mixed_identity", "nullable_payload_mixed_saved")},
    "nullable_payload_deleted": [(argument, tag, value, "undefined", "")
        for argument, tag, value in ((repr(STRING_RESULT), "string", STRING_RESULT),
                                    ("null", "null_value", ""), ("void 0", "undefined", ""),
                                    ("''", "string", ""))],
}

NULLABLE_OBSERVATIONS.update({
    name: [("false", "string", STRING_RESULT if name in {
        "nullable_payload_saved", "nullable_payload_mixed_saved"} else "future"),
           ("true", "null_value", "")]
    for name in nullable_payload_sources()
})


def nullable_host_result_sources():
    source = nullable_payload_sources()["nullable_payload_mixed_readback"][0].replace(
        "size() { return state.size; }", "size(key) { state.set(key, key); return state.size; }")
    source = source.replace(
        "host.slot.set(host.slot.get(true)); var trace = host.slot.size();",
        "var trace = host.slot.size(host.slot.set(host.slot.get(false)));")
    both = source.replace("host.slot.set(host.slot.get(false)); var trace =",
        "host.slot.set(host.slot.get(true)); var trace =")
    conditional = source.replace("state.set(key, key); return state.get(key);",
        "state.set(key, key); if (key) { state.set(key, 'selected'); } return state.get(key);")
    saved = source.replace("state.set('seed', 'future');",
        f"state.set('seed', '{STRING_RESULT}');").replace("state.delete('seed');",
        "state.set('seed', 'overwritten'); state.delete('seed');")
    saved = saved.replace("state.set(key, key); return state.get(key);",
        "state.set(key, key); const saved = state.get(key); "
        "state.set(key, true); state.delete(key); return saved;")
    return {
        # Preserve the exact acyclic fifteen-call, trace=1 source from ae8e021a.
        # The final size argument needs the setter's independent result proof.
        "nullable_host_result": (source, "host", 1),
        "nullable_host_result_both": (both, "host", 2),
        "nullable_host_result_identity": (source.replace("var trace =",
            "host.slot.set(host.slot.get(true)); host.slot.set(void 0); host.slot.set(''); "
            "var trace ="), "host", 4),
        # A live conditional write joins the stored payload alternatives before
        # the host result proof; it cannot reuse the native Presence analysis.
        "nullable_host_result_conditional": (conditional, "host", 2),
        # The copied result reaches size after its Map entry was overwritten
        # with Boolean and deleted. Startup uses false; saved calls use both.
        "nullable_host_result_saved": (saved, "host", 1),
    }


NULLABLE_HOST_RESULT_CALLS = {
    "nullable_host_result": 15,
    "nullable_host_result_both": 15,
    "nullable_host_result_identity": 19,
    "nullable_host_result_conditional": 16,
    "nullable_host_result_saved": 18,
}

NULLABLE_OBSERVATIONS.update({
    name: [("false", "string", STRING_RESULT if name == "nullable_host_result_saved" else "future"),
           ("true", "null_value", "")]
    for name in nullable_host_result_sources()
})
NULLABLE_PAYLOAD_READBACKS.update({
    name: [(argument, tag, value, tag,
            "selected" if name == "nullable_host_result_conditional" and value else value)
           for argument, tag, value in ((repr(STRING_RESULT), "string", STRING_RESULT),
                                       ("null", "null_value", ""), ("void 0", "undefined", ""),
                                       ("''", "string", ""))]
    for name in nullable_host_result_sources()
})


def nullable_host_result_refusals():
    source = nullable_host_result_sources()["nullable_host_result"][0]
    both = nullable_host_result_sources()["nullable_host_result_both"][0]
    return {
        # These call graphs are acyclic; only the live host result fact is
        # missing. Each exact repair is a separately admitted positive source.
        "nullable_host_result_unknown": ("var unknownResult = null;\n" + source.replace(
            "state.set(key, key); return state.get(key);",
            "state.set(key, unknownResult); return state.get(key);"), 2,
            "state.set(key, unknownResult);", "state.set(key, key);", 1),
        "nullable_host_result_missing": (source.replace("return state.get(key);",
            "return state.get('missing');"), 2,
            "return state.get('missing');", "return state.get(key);", 1),
        "nullable_host_result_deleted": (both.replace("return state.get(key);",
            "state.delete(key); return state.get(key);"), 1,
            "state.delete(key); ", "", 2),
        "nullable_host_result_aliasing": (source.replace("return state.get(key);",
            "state.set(key || 'fallback', true); return state.get(key);"), 2,
            "state.set(key || 'fallback', true); ", "", 1),
    }


def nullable_nested_result_sources():
    source = nullable_payload_sources()["nullable_payload_mixed_readback"][0].replace(
        "host.slot.set(host.slot.get(false));", "host.slot.set(host.slot.set(host.slot.get(false)));")
    saved = nullable_payload_sources()["nullable_payload_mixed_saved"][0].replace(
        "host.slot.set(host.slot.get(false));", "host.slot.set(host.slot.set(host.slot.get(false)));")
    return {
        # Preserve the exact fifteen-call trace=2 refusal recorded in a8da7c27.
        # A proved invocation result seeds the next call to this same method;
        # the final generalized census still includes every actual and sibling.
        "nullable_nested_result": (source, "host", 2),
        "nullable_nested_result_same": (source.replace("host.slot.get(true)",
            "host.slot.get(false)"), "host", 1),
        # These independent actuals occur after the nested invocation. The
        # first call cannot define the complete published method signature.
        "nullable_nested_result_identity": (source.replace("var trace =",
            "host.slot.set(void 0); host.slot.set(''); host.slot.set('later'); var trace ="), "host", 5),
        # Both nested calls copy their nullable read before same-key Boolean
        # overwrite/deletion. Startup sees false; future calls run both flags.
        "nullable_nested_result_saved": (saved, "host", 0),
    }


NULLABLE_NESTED_RESULT_CALLS = {
    "nullable_nested_result": 15,
    "nullable_nested_result_same": 15,
    "nullable_nested_result_identity": 18,
    "nullable_nested_result_saved": 15,
}

NULLABLE_OBSERVATIONS.update({
    name: [("false", "string", STRING_RESULT if name == "nullable_nested_result_saved" else "future"),
           ("true", "null_value", "")]
    for name in nullable_nested_result_sources()
})
NULLABLE_PAYLOAD_READBACKS.update({
    name: [(argument, tag, value, tag, value)
           for argument, tag, value in ((repr(STRING_RESULT), "string", STRING_RESULT),
                                       ("null", "null_value", ""), ("void 0", "undefined", ""),
                                       ("''", "string", ""))]
    for name in nullable_nested_result_sources()
})


def nullable_nested_result_refusals():
    source = nullable_nested_result_sources()["nullable_nested_result"][0]
    same = nullable_nested_result_sources()["nullable_nested_result_same"][0]
    return {
        # Every repair is exactly a separately gated positive. An earlier
        # valid invocation cannot authorize unknown or foreign result evidence,
        # a self-dependent unseeded read, a later bad actual or a bad sibling.
        "nullable_nested_unknown": ("var unknownResult = null;\n" + same.replace(
            "state.set(key, key);", "state.set(key, unknownResult);"), 2,
            "state.set(key, unknownResult);", "state.set(key, key);", "nullable_nested_result_same", 15),
        "nullable_nested_foreign": (same.replace("return state.get(key);",
            "return new Map().get(key);"), 2,
            "return new Map().get(key);", "return state.get(key);", "nullable_nested_result_same", 15),
        "nullable_nested_unseeded": (same.replace("state.set(key, key);",
            "state.set(key, state.get(key));"), 2,
            "state.set(key, state.get(key));", "state.set(key, key);", "nullable_nested_result_same", 16),
        "nullable_nested_later_actual": (source.replace("var trace =",
            "host.slot.set({}); var trace ="), 3,
            "host.slot.set({}); ", "", "nullable_nested_result", 16),
        "nullable_nested_sibling": (source.replace("size() { return state.size; }",
            "size() { state.set('late', {}); return state.size; }"), 3,
            "state.set('late', {}); ", "", "nullable_nested_result", 16),
    }


def nullable_payload_refusals():
    source = nullable_payload_sources()["nullable_payload_readback"][0]
    mixed = nullable_payload_sources()["nullable_payload_mixed"][0]
    return {
        # Representing a missing result does not invent the host's independent
        # payload proof, nor authorize a snapshot or an object graph.
        "nullable_payload_missing": (source.replace("return state.get(key);",
            "return state.get('missing');").replace("host.slot.set(host.slot.get(false));",
            "host.slot.set(host.slot.set(host.slot.get(false)));"), 3,
            "return state.get('missing');", "return state.get(key);", 2),
        "nullable_payload_snapshot": (mixed.replace("state.set(key, key);",
            "state.set(key, key); state.values();"), 3,
            "state.values();", "state.size;", 3),
        "nullable_payload_object": (nullable_payload_sources()["nullable_payload_write"][0]
            .replace("state.set(key, key);", "state.set(key, {value: 'instance'});"), 2,
            "state.set(key, {value: 'instance'});", "state.set(key, key);", 2),
    }


def mixed_nullable_payload_refusals():
    source = nullable_payload_sources()["nullable_payload_mixed_readback"][0]
    return {
        # Ownership and the actual census stay complete. A missing entry has
        # no independent nullable read proof, even though its key is typed.
        "nullable_mixed_read_missing": (source.replace("state.set(key, key);",
            "state.has(key);"), 0,
            "state.has(key);", "state.set(key, key);", 2),
        "nullable_mixed_read_deleted": (source.replace("return state.get(key);",
            "state.delete(key); return state.get(key);"), 0,
            "state.delete(key); ", "", 2),
        # A possibly-equal key really overwrites the String arm with Boolean.
        # The independent live return is then mixed and has no native carrier.
        "nullable_mixed_read_aliasing": (source.replace("return state.get(key);",
            "state.set(key || 'fallback', true); return state.get(key);"), 3,
            "state.set(key || 'fallback', true); ", "", 2),
    }


def nullable_key_refusals():
    source = nullable_key_sources()["nullable_key_mixed"][0]
    return {
        # A supported key carrier does not expose mixed-key snapshots through
        # the published method contract or manufacture an iterator proof.
        "nullable_key_snapshot": (source.replace("state.set(key, true);",
            "state.set(key, true); state.keys();"), 2,
            "state.keys();", "state.size;", 2),
    }


def seeded_carrier_refusals():
    return {
        "result_seeded_number_string_contents": (mixed_result_sources()[
            "result_seeded_mixed_contents"][0].replace("state.set(0, true)",
                                                       "state.set(0, 'old')"), 2),
    }


MIXED_RESULT_TYPES = {
    "result_seeded_mixed_contents": ("js_num", "double"),
    "result_seeded_join_reseed": ("js_num", "double"),
    "result_seeded_bool_string_contents": ("bool", "std::string"),
    "result_seeded_mixed_false_zero": ("bool", "double"),
    "result_seeded_mixed_false": ("bool", "double"),
    "result_seeded_mixed_empty_key": ("std::string", "std::string"),
    "result_seeded_mixed_string_saved": ("std::string", "std::string"),
    "saved_read_write": ("std::string", "std::string"),
    "saved_read_write_false": ("bool", "std::string"),
    "saved_read_write_number": ("js_num", "double"),
    "saved_read_write_repeated": ("std::string", "std::string"),
    "saved_read_write_string_saved": ("std::string", "std::string"),
    "saved_read_write_wrong_tag": ("bool", "std::string"),
    "saved_read_write_overwritten": ("bool", "std::string"),
    "saved_join": ("std::string", "std::string"),
    "saved_join_always_empty": ("std::string", "std::string"),
    "saved_join_distinct": ("std::string", "std::string"),
    "saved_join_bool": ("bool", "std::string"),
    "saved_join_number": ("js_num", "double"),
    "saved_join_string_saved": ("std::string", "std::string"),
    "guarded_saved_read": ("std::string", "std::string"),
    "guarded_saved_no_delete": ("std::string", "std::string"),
    "guarded_saved_always_empty": ("std::string", "std::string"),
    "guarded_saved_bool": ("bool", "std::string"),
    "guarded_saved_number": ("js_num", "double"),
    "guarded_saved_string_saved": ("std::string", "std::string"),
    "shortcircuit_same_tag": ("std::string", "std::string"),
    "shortcircuit_distinct": ("std::string", "std::string"),
    "shortcircuit_empty_string": ("std::string", "std::string"),
    "shortcircuit_bool": ("bool", "std::string"),
    "shortcircuit_false": ("bool", "std::string"),
    "shortcircuit_number": ("js_num", "double"),
    "shortcircuit_zero": ("js_num", "double"),
    "shortcircuit_string_saved": ("std::string", "std::string"),
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
    **{name: (("std::string" if "string" in name else "bool"),
              ("std::string" if "string" in name else "bool"), 6)
       for name in payload_result_sources()},
    **{name: (result, result, 6) for name, (result, _) in MIXED_RESULT_TYPES.items()},
    **{name: ("ctnative::nullable_string", "ctnative::nullable_string", 6)
       for name in {**nullable_result_sources(), **nullable_key_sources(), **nullable_payload_sources(),
                    **nullable_host_result_sources(), **nullable_nested_result_sources()}},
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


LEAF_OBJECT_SOURCE = """var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {}; state.set(key, item); return state.size; }
    };
});
host.slot.set('x'); host.slot.set('x'); host.slot.set('y');
var trace = host.slot.size();
"""


def leaf_object_sources():
    number = LEAF_OBJECT_SOURCE.replace("const item = {};", "const item = {value: 1};")
    fields = LEAF_OBJECT_SOURCE.replace("const item = {};",
        "const item = {value: 1, flag: true, empty: null, absent: void 0}; "
        "item.value = state.size; item.flag = false;")
    lifetime = fields.replace("size() { return state.size; },",
        "size() { return state.size; }, "
        "erase(key) { state.delete(key); return state.size; },")
    lifetime = lifetime.replace("var trace =", "host.slot.erase('y'); var trace =")
    return {
        # These first two programs preserve the exact seven-call/five-function
        # continuation measured in e533a865, including repeated live keys.
        "leaf_object_plain": (LEAF_OBJECT_SOURCE, "host", 2),
        "leaf_object_number_field": (number, "host", 2),
        "leaf_object_scalar_writes": (fields, "host", 2),
        "leaf_object_alias": (fields.replace("state.set(key, item);",
            "const alias = item; state.set(key, alias);"), "host", 2),
        # An independently called deleting sibling provides a numeric ABI for
        # the saved-callable observer. No object crosses a published boundary.
        "leaf_object_lifetime": (lifetime, "host", 1),
        "leaf_object_number_repair": (LEAF_OBJECT_SOURCE.replace("const item = {};",
            "const item = 1;"), "host", 2),
        "leaf_object_string_repair": (LEAF_OBJECT_SOURCE.replace("const item = {};",
            "const item = 'instance';"), "host", 2),
        "leaf_object_identity_repair": (number.replace(
            "host.slot.set('x'); host.slot.set('x'); host.slot.set('y');\nvar trace = host.slot.size();",
            "host.slot.size(); var trace = host.slot.set('x');"), "host", 1),
    }


LEAF_OBJECT_CALLS = {name: 9 if name == "leaf_object_lifetime"
                     else 5 if name == "leaf_object_identity_repair" else 7
                     for name in leaf_object_sources()}
LEAF_OBJECT_FUNCTIONS = {name: 6 if name == "leaf_object_lifetime" else 5
                         for name in leaf_object_sources()}
LEAF_OBJECT_FIELDS = {
    "leaf_object_number_field": "number",
    "leaf_object_identity_repair": "number",
    "leaf_object_scalar_writes": "scalar",
    "leaf_object_alias": "scalar",
    "leaf_object_lifetime": "scalar",
}


def leaf_object_refusals():
    number = leaf_object_sources()["leaf_object_number_field"][0]
    repaired = leaf_object_sources()["leaf_object_identity_repair"][0]
    body = "const item = {value: 1}; state.set(key, item); return state.size;"
    saved = ("const item = {value: 1}; state.set(key, item); const saved = state.get(key); "
             "state.set(key, {value: 1}); state.delete(key); return saved === item ? 1 : 0;")
    rows = {}
    for name, old, replacement, repair, value, calls in (
        ("string_field", "const item = {value: 1};", "const item = {value: 'instance'};",
         "leaf_object_number_field", 2, 7),
        ("nested_field", "const item = {value: 1};", "const item = {value: {}};",
         "leaf_object_number_field", 2, 7),
        ("cycle", "const item = {value: 1};", "const item = {value: 1}; item.value = item;",
         "leaf_object_number_field", 2, 7),
        ("dynamic_field", "const item = {value: 1};", "const item = {value: 1}; item[key] = 1;",
         "leaf_object_number_field", 2, 7),
        ("prototype", "const item = {value: 1};", "const item = {value: 1}; item.__proto__ = {};",
         "leaf_object_number_field", 2, 7),
        ("object_key", "state.set(key, item);", "state.set(item, item);",
         "leaf_object_number_field", 3, 7),
        ("object_return", "state.set(key, item); return state.size;",
         "state.set(key, item); return item;", "leaf_object_number_field", 2, 7),
        ("later_actual", "var trace =", "host.slot.set({}); var trace =",
         "leaf_object_number_field", 3, 8),
        ("unknown_read", "size() { return state.size; }",
         "size() { const item = state.get('x'); return item ? 1 : 0; }",
         "leaf_object_number_field", 1, 8),
        ("unsafe_sibling", "size() { return state.size; }",
         "size() { const item = {}; item.self = item; state.set('cycle', item); return state.size; }",
         "leaf_object_number_field", 3, 8),
    ):
        rows["leaf_object_" + name] = (number.replace(old, replacement), value,
            replacement, old, repair, calls)
    # The exact saved/distinct/deleted identity continuation remains separate
    # from write-only leaf ownership. Each repair restores the same complete
    # numeric-returning source, rather than erasing or trusting forged reads.
    for name, expression, value, calls in (
        ("saved_identity", "saved === item", 1, 8),
        ("distinct_identity", "saved === {value: 1}", 0, 8),
        ("deleted_identity", "state.get(key) === item", 0, 9),
    ):
        changed = saved.replace("saved === item", expression)
        rows["leaf_object_" + name] = (repaired.replace(body, changed), value,
            changed, body, "leaf_object_identity_repair", calls)
    return rows


def leaf_readback_sources():
    # These preserve the sixteen measured next.json sources from 5d2d843a.
    # Keep the two post-delete reads as refusals and the comparison-only
    # fresh allocations as separate native carrier controls below.
    base = leaf_object_sources()["leaf_object_identity_repair"][0]
    old = "const item = {value: 1}; state.set(key, item); return state.size;"
    empty = "const item = {}; state.set(key, item); const saved = state.get(key); "
    field = "const item = {value: 1}; state.set(key, item); const saved = state.get(key); "
    rows = {
        "local_identity_saved": (empty + "return saved === item ? 1 : 0;", 1),
        "local_identity_distinct_fresh": (empty + "return saved === {} ? 1 : 0;", 0),
        "local_field_direct": ("const item = {value: 1}; state.set(key, item); return item.value;", 1),
        "local_field_get": (field + "return saved.value;", 1),
        "local_field_get_guarded": (field + "return saved === item ? saved.value : 0;", 1),
        "local_identity_distinct_stored": (empty + "const replacement = {}; "
            "state.set(key, replacement); return saved === replacement ? 1 : 0;", 0),
        "local_identity_saved_overwrite": (field + "state.set(key, {value: 1}); "
            "return saved === item ? 1 : 0;", 1),
        "local_identity_saved_delete": (field + "state.delete(key); return saved === item ? 1 : 0;", 1),
        "local_field_saved_overwrite": (field + "state.set(key, {value: 2}); "
            "return saved === item ? saved.value : 0;", 1),
        "local_field_saved_delete": (field + "state.delete(key); "
            "return saved === item ? saved.value : 0;", 1),
        "local_field_saved_alias_write": (field + "item.value = 2; "
            "return saved === item ? saved.value : 0;", 2),
    }
    result = {name: (base.replace(old, body), "host", value)
              for name, (body, value) in rows.items()}
    same = result["local_identity_saved"][0]
    result["local_identity_repeated_keys"] = (same.replace("host.slot.set('x');",
        "host.slot.set('x') + host.slot.set('x') + host.slot.set('y');"), "host", 3)
    for name in ("saved_identity", "distinct_identity"):
        source, value, *_ = leaf_object_refusals()["leaf_object_" + name]
        result["historical_object_" + name] = source, "host", value
    result["local_identity_not_equal"] = (same.replace("saved === item", "saved !== item"), "host", 0)
    result["local_field_readback_test"] = (base.replace(old,
        field + "return saved.value === 1 ? 1 : 0;"), "host", 1)
    result["local_field_export_repair"] = (result["local_field_get_guarded"][0].replace(
        "return saved === item ? saved.value : 0;", "return saved === item && saved.value === 1 ? 1 : 0;")
        .replace("var trace = host.slot.set('x');", "host.slot.set('x'); var trace = host.slot.size();"), "host", 1)
    for name, literal in (("boolean", "false"), ("null", "null"), ("undefined", "void 0")):
        result["local_field_" + name] = (base.replace(old,
            "const item = {value: " + literal + "}; state.set(key, item); "
            "const saved = state.get(key); return saved.value === (" + literal + ") ? 1 : 0;"), "host", 1)
    lifetime = base.replace("set(key)", "set(key, value)").replace(old,
        field + "state.set(key, {value: 3}); state.delete(key); item.value = value; "
        "return saved === item ? saved.value : 0;")
    result["local_field_readback_lifetime"] = (lifetime.replace("host.slot.set('x')",
        "host.slot.set('x', 2)"), "host", 2)
    for name, (old_return, checked_return) in LEAF_READBACK_CHECKED_RETURNS.items():
        source, binding, value = result[name]
        result[name + "_checked"] = source.replace(old_return, checked_return), binding, value
    return result


LEAF_READBACK_CHECKED_RETURNS = {
    "local_field_direct": ("return item.value;", "return item.value === 1 ? 1 : 0;"),
    "local_field_get_guarded": ("return saved === item ? saved.value : 0;",
        "return saved === item && saved.value === 1 ? 1 : 0;"),
    "local_field_saved_overwrite": ("return saved === item ? saved.value : 0;",
        "return saved === item && saved.value === 1 ? 1 : 0;"),
    "local_field_saved_delete": ("return saved === item ? saved.value : 0;",
        "return saved === item && saved.value === 1 ? 1 : 0;"),
    "local_field_saved_alias_write": ("return saved === item ? saved.value : 0;",
        "return saved === item && saved.value === 2 ? 2 : 0;"),
    "local_field_readback_lifetime": ("return saved === item ? saved.value : 0;",
        "return saved === item && saved.value === value ? value : 0;"),
}


LEAF_READBACK_CALLS = {
    "local_identity_saved": 6, "local_identity_distinct_fresh": 6,
    "local_field_direct": 5, "local_field_get": 6, "local_field_get_guarded": 6,
    "local_identity_distinct_stored": 7, "local_identity_saved_overwrite": 7,
    "local_identity_saved_delete": 7, "local_field_saved_overwrite": 7,
    "local_field_saved_delete": 7, "local_field_saved_alias_write": 6,
    "local_identity_repeated_keys": 8, "historical_object_saved_identity": 8,
    "historical_object_distinct_identity": 8, "local_identity_not_equal": 6,
    "local_field_boolean": 6, "local_field_null": 6, "local_field_undefined": 6,
    "local_field_readback_lifetime": 8,
    "local_field_readback_test": 6, "local_field_export_repair": 7,
}
LEAF_READBACK_CALLS.update({name + "_checked": LEAF_READBACK_CALLS[name]
                           for name in LEAF_READBACK_CHECKED_RETURNS})

# These preserve both remaining native carrier boundaries: comparison-only
# fresh allocations and nullable numeric field results at numeric exports.
LEAF_READBACK_CARRIERS = {"local_identity_distinct_fresh", "historical_object_distinct_identity",
    "local_field_get", *LEAF_READBACK_CHECKED_RETURNS}
LEAF_READBACK_UNOWNED = {"local_identity_repeated_keys"}


def leaf_readback_refusals():
    positive = leaf_readback_sources()
    field = positive["local_field_get_guarded"][0]
    tested = positive["local_field_readback_test"][0]
    same = positive["local_identity_saved"][0]
    rows = {}
    for name, source, old, replacement, repair, value, calls in (
        ("unknown_incoming", same, "state.set(key, item);", "state.has(key);",
         "local_identity_saved", 0, 6),
        ("other_key", same, "state.get(key)", "state.get('other')",
         "local_identity_saved", 0, 6),
        ("object_export", positive["local_field_export_repair"][0],
         "return saved === item && saved.value === 1 ? 1 : 0;", "return saved;", "local_field_export_repair", 1, 7),
        ("uninitialized_field", tested, "value: 1", "other: 1", "local_field_readback_test", 0, 6),
        ("dynamic_field_read", tested, "saved.value", "saved[key]", "local_field_readback_test", 0, 6),
        ("prototype_field_read", tested, "saved.value", "saved.__proto__",
         "local_field_readback_test", 0, 6),
        ("unknown_alias_field", tested, "value: 1", "value: key", "local_field_readback_test", 0, 6),
        ("deleted_before_read", same, "const saved = state.get(key);",
         "state.delete(key); const saved = state.get(key);", "local_identity_saved", 0, 7),
    ):
        rows["leaf_readback_" + name] = (source.replace(old, replacement), value,
            replacement, old, repair, calls)
    deleted, value, old, replacement, _, calls = leaf_object_refusals()["leaf_object_deleted_identity"]
    rows["historical_object_deleted_identity"] = (deleted, value, old, replacement,
        "leaf_object_identity_repair", calls)
    direct = field.replace("const saved = state.get(key); ", "")
    direct = direct.replace("return saved === item ? saved.value : 0;",
        "state.delete(key); return state.get(key) === item ? 1 : 0;")
    repaired = positive["local_identity_saved_delete"][0]
    rows["local_identity_after_delete"] = (direct, 0,
        "state.delete(key); return state.get(key) === item ? 1 : 0;",
        "const saved = state.get(key); state.delete(key); return saved === item ? 1 : 0;",
        "local_identity_saved_delete", 7)
    assert direct.replace(rows["local_identity_after_delete"][2],
                          rows["local_identity_after_delete"][3]) == repaired
    rows["local_identity_repeated_keys"] = (positive["local_identity_repeated_keys"][0], 3,
        "host.slot.set('x') + host.slot.set('x') + host.slot.set('y')", "host.slot.set('x')",
        "local_identity_saved", 8)
    return rows
