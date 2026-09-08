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


def nullable_carrier_refusals():
    source = nullable_key_sources()["nullable_key_homogeneous"][0]
    return {
        # Key support cannot authorize an optional stored payload. Ownership
        # still succeeds, so refusal preserves the prepared producer edge.
        "nullable_key_payload": (source.replace("state.set(key, true)", "state.set(key, key)"), 2),
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
       for name in {**nullable_result_sources(), **nullable_key_sources()}},
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
