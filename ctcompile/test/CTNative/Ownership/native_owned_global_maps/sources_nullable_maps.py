from .sources_scalar_maps import (
    CALL_RESULT,
    PARAMETER,
    SEEDED_RESULT,
    SOURCE,
    STRING_RESULT,
    joined_result_sources,
    key_fact_sources,
    mixed_result_sources,
    parameter_sources,
    payload_result_sources,
    shortcircuit_sources,
    size_result_sources,
)


def nullable_result_sources():
    saved = shortcircuit_sources()["shortcircuit_same_tag"][0].replace(
        "return result;", "return result || null;"
    )
    saved = saved.replace("state.set(key, true)", "state.set(key || 'missing', true)")
    threeway = saved.replace("get(flag)", "get(flag, nullish)")
    threeway = threeway.replace(
        "return result || null;", "return result || (nullish ? null : (void 0));"
    )
    threeway = threeway.replace("host.slot.get(false)", "host.slot.get(false, false)")
    threeway = threeway.replace("host.slot.get(true)", "host.slot.get(true, true)")
    threeway = threeway.replace(
        "var trace =", "host.slot.set(host.slot.get(true, false)); var trace ="
    )
    owning = saved.replace(
        "state.set('', ''); state.set('other', 'future');",
        f"state.set('', ''); state.set('other', '{STRING_RESULT}'); "
        f"state.set('{STRING_RESULT}', 'stored'); state.set('missing', 'stored');",
    )
    owning = owning.replace(
        "state.set(false, true);",
        "state.set('', false); state.delete(''); "
        "state.set('other', false); state.delete('other'); state.set(false, true);",
    )
    owning = owning.replace("state.delete(false);", "state.set(false, false); state.delete(false);")
    owning = owning.replace(" host.slot.set(host.slot.get(true));", "")
    homogeneous = saved.replace(
        "state.set('', ''); state.set('other', 'future'); "
        "if (flag) { state.delete('other'); } "
        "const saved = (state.has('other') && state.get('other')) || state.get(''); "
        "state.set(false, true); state.set(false, saved); "
        "const result = state.get(false); state.delete(false);",
        "state.set('seed', 'future'); const result = flag ? '' : state.get('seed'); "
        "state.delete('seed');",
    )
    return {
        # Same eighteen calls as nullable_or: only the consuming key is
        # normalized, separating the callable contract from real null Map keys.
        "nullable_normalized": (saved, "host", 3),
        "nullable_ternary": (
            saved.replace("return result || null;", "return result ? result : null;"),
            "host",
            3,
        ),
        # void 0 imports as a literal; bare undefined is a separate host read.
        "nullable_undefined": (
            saved.replace("return result || null;", "return result || (void 0);"),
            "host",
            3,
        ),
        "nullable_empty": (
            saved.replace("state.set('other', 'future');", "state.set('other', '');").replace(
                " host.slot.set(host.slot.get(true));", ""
            ),
            "host",
            3,
        ),
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
    "nullable_threeway": [
        ("false, false", "string", "future"),
        ("true, true", "null_value", ""),
        ("true, false", "undefined", ""),
    ],
    "nullable_homogeneous_key": [("false", "string", "future"), ("true", "null_value", "")],
    "nullable_string_saved": [("false", "string", STRING_RESULT), ("true", "null_value", "")],
}


def nullable_result_refusals():
    saved = nullable_result_sources()["nullable_normalized"][0]
    effect = saved.replace("if (flag) { state.delete('other'); }", "if (flag) { inspect(state); }")
    effect = effect.replace(" host.slot.set(host.slot.get(true));", "")
    return {
        "nullable_unknown_result": (
            "var unknownResult = null;\n"
            + saved.replace("return result || null;", "return result || unknownResult;"),
            3,
            "return result || unknownResult;",
            "return result || null;",
            3,
        ),
        "nullable_unproved_read": (
            saved.replace("return result || null;", "return result || state.get('missing');"),
            3,
            "return result || state.get('missing');",
            "return result || null;",
            3,
        ),
        "nullable_object_result": (
            saved.replace("return result || null;", "return result || {};"),
            3,
            "return result || {};",
            "return result || null;",
            3,
        ),
        "nullable_number_result": (
            saved.replace("return result || null;", "return result || 7;"),
            3,
            "return result || 7;",
            "return result || null;",
            3,
        ),
        # A future true branch is checked even though startup only uses false.
        "nullable_late_effect": (
            effect,
            3,
            "if (flag) { inspect(state); }",
            "if (flag) { state.delete('other'); }",
            3,
        ),
    }


def nullable_key_sources():
    normalized = nullable_result_sources()["nullable_homogeneous_key"][0]
    homogeneous = normalized.replace("state.set(key || 'missing', true)", "state.set(key, true)")
    identity = homogeneous.replace(
        "var trace =", "host.slot.set(void 0); host.slot.set(''); var trace ="
    )
    mixed = homogeneous.replace(
        "state.delete('seed');",
        "state.delete('seed'); state.set(false, true); state.delete(false);",
    )
    saved = nullable_result_sources()["nullable_normalized"][0]
    owning = homogeneous.replace(
        "state.set('seed', 'future');", f"state.set('seed', '{STRING_RESULT}');"
    )
    owning = owning.replace(
        "state.delete('seed');", "state.set('seed', 'overwritten'); state.delete('seed');"
    )
    owning = owning.replace("state.set(key, true);", "state.delete(key); state.set(key, true);")
    owning = owning.replace(" host.slot.set(host.slot.get(true));", "")
    owning = owning.replace("var trace =", "host.slot.set(void 0); var trace =")
    return {
        # Keep the exact eleven-call boundary and its thirteen-call identity
        # extension independent of Boolean keys and nullable stored payloads.
        "nullable_key_homogeneous": (homogeneous, "host", 2),
        "nullable_key_identity": (identity, "host", 4),
        "nullable_key_identity_normalized": (
            identity.replace("state.set(key, true)", "state.set(key || 'missing', true)"),
            "host",
            2,
        ),
        "nullable_key_second_use": (
            normalized.replace(
                "state.set(key || 'missing', true)",
                "state.set(key || 'missing', true); state.set(key, true)",
            ),
            "host",
            3,
        ),
        "nullable_key_mixed": (mixed, "host", 2),
        "nullable_original_key": (
            saved.replace("state.set(key || 'missing', true)", "state.set(key, true)"),
            "host",
            3,
        ),
        # The normalized first use proves its own String key. The original
        # formal keeps null at the second use, widening only the Map schema.
        "nullable_second_key_use": (
            saved.replace(
                "state.set(key || 'missing', true)",
                "state.set(key || 'missing', true); state.set(key, true)",
            ),
            "host",
            4,
        ),
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

NULLABLE_OBSERVATIONS.update(
    {
        name: [
            ("false", "string", STRING_RESULT if name == "nullable_key_string_saved" else "future"),
            ("true", "null_value", ""),
        ]
        for name in nullable_key_sources()
    }
)


def nullable_payload_sources():
    source = nullable_key_sources()["nullable_key_homogeneous"][0]
    write = source.replace("state.set(key, true)", "state.set(key, key)")
    readback = write.replace(
        "set(key) { state.set(key, key); return state.size; }",
        "set(key) { state.set(key, key); return state.get(key); }",
    )
    identity = readback.replace(
        "var trace =", "host.slot.set(void 0); host.slot.set(''); var trace ="
    )
    saved = write.replace("state.set('seed', 'future');", f"state.set('seed', '{STRING_RESULT}');")
    saved = saved.replace(
        "state.delete('seed');", "state.set('seed', 'overwritten'); state.delete('seed');"
    )
    saved = saved.replace(
        "set(key) { state.set(key, key); return state.size; }",
        "set(key) { state.set(key, key); const saved = state.get(key); "
        "state.set(key, 'overwritten'); state.delete(key); return saved; }",
    )
    saved = saved.replace(" host.slot.set(host.slot.get(true));", "")
    saved = saved.replace("var trace =", "host.slot.set(void 0); var trace =")
    mixed_readback = readback.replace(
        "set(key) { state.set(key, key);",
        "set(key) { state.set('extra', true); state.delete('extra'); state.set(key, key);",
    )
    original_mixed_readback = nullable_key_sources()["nullable_original_key"][0].replace(
        "set(key) { state.set(key, true); return state.size; }",
        "set(key) { state.set(key, key); return state.get(key); }",
    )
    return {
        # Preserve both exact sources from 1c7985a5 before adding observations.
        "nullable_payload_write": (write, "host", 2),
        "nullable_payload_readback": (readback, "host", 2),
        # Full nullable String storage represents a deleted read as Undefined;
        # its return must not reuse either the payload or its absent tag.
        "nullable_payload_deleted": (
            readback.replace("return state.get(key);", "state.delete(key); return state.get(key);"),
            "host",
            0,
        ),
        "nullable_payload_identity": (identity, "host", 4),
        # Scalar reads remain independently proved inside the getter even
        # when the full storage schema also contains null and Boolean.
        "nullable_payload_mixed": (
            nullable_key_sources()["nullable_original_key"][0].replace(
                "state.set(key, true)", "state.set(key, key)"
            ),
            "host",
            3,
        ),
        # Startup sees only false; saved methods later run both flags and
        # return an owning payload copied before overwrite and deletion.
        "nullable_payload_saved": (saved, "host", 0),
        # The exact fourteen-call checkpoint broadens storage with a Boolean
        # while each later get retains its independent nullable payload fact.
        "nullable_payload_mixed_readback": (mixed_readback, "host", 2),
        # Preserve the former nineteen-call refusal unchanged: its actual
        # String/Null result is a supported subset of the full Map schema.
        "nullable_mixed_payload_readback": (original_mixed_readback, "host", 3),
        "nullable_payload_mixed_identity": (
            mixed_readback.replace(
                "var trace =", "host.slot.set(void 0); host.slot.set(''); var trace ="
            ),
            "host",
            4,
        ),
        # Broader storage and a subsequent same-key Boolean overwrite cannot
        # widen a saved nullable read or leave it borrowing the old entry.
        "nullable_payload_mixed_saved": (
            saved.replace("state.set(key, 'overwritten');", "state.set(key, true);"),
            "host",
            0,
        ),
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
    "nullable_payload_readback": [
        ("'future'", "string", "future", "string", "future"),
        ("null", "null_value", "", "null_value", ""),
    ],
    **{
        name: [
            (argument, tag, value, tag, value)
            for argument, tag, value in (
                (repr(STRING_RESULT), "string", STRING_RESULT),
                ("null", "null_value", ""),
                ("void 0", "undefined", ""),
                ("''", "string", ""),
            )
        ]
        for name in (
            "nullable_payload_identity",
            "nullable_payload_saved",
            "nullable_payload_mixed_readback",
            "nullable_mixed_payload_readback",
            "nullable_payload_mixed_identity",
            "nullable_payload_mixed_saved",
        )
    },
    "nullable_payload_deleted": [
        (argument, tag, value, "undefined", "")
        for argument, tag, value in (
            (repr(STRING_RESULT), "string", STRING_RESULT),
            ("null", "null_value", ""),
            ("void 0", "undefined", ""),
            ("''", "string", ""),
        )
    ],
}

NULLABLE_OBSERVATIONS.update(
    {
        name: [
            (
                "false",
                "string",
                (
                    STRING_RESULT
                    if name in {"nullable_payload_saved", "nullable_payload_mixed_saved"}
                    else "future"
                ),
            ),
            ("true", "null_value", ""),
        ]
        for name in nullable_payload_sources()
    }
)


def nullable_host_result_sources():
    source = nullable_payload_sources()["nullable_payload_mixed_readback"][0].replace(
        "size() { return state.size; }", "size(key) { state.set(key, key); return state.size; }"
    )
    source = source.replace(
        "host.slot.set(host.slot.get(true)); var trace = host.slot.size();",
        "var trace = host.slot.size(host.slot.set(host.slot.get(false)));",
    )
    both = source.replace(
        "host.slot.set(host.slot.get(false)); var trace =",
        "host.slot.set(host.slot.get(true)); var trace =",
    )
    conditional = source.replace(
        "state.set(key, key); return state.get(key);",
        "state.set(key, key); if (key) { state.set(key, 'selected'); } return state.get(key);",
    )
    saved = source.replace(
        "state.set('seed', 'future');", f"state.set('seed', '{STRING_RESULT}');"
    ).replace("state.delete('seed');", "state.set('seed', 'overwritten'); state.delete('seed');")
    saved = saved.replace(
        "state.set(key, key); return state.get(key);",
        "state.set(key, key); const saved = state.get(key); "
        "state.set(key, true); state.delete(key); return saved;",
    )
    return {
        # Preserve the exact acyclic fifteen-call, trace=1 source from ae8e021a.
        # The final size argument needs the setter's independent result proof.
        "nullable_host_result": (source, "host", 1),
        "nullable_host_result_both": (both, "host", 2),
        "nullable_host_result_identity": (
            source.replace(
                "var trace =",
                "host.slot.set(host.slot.get(true)); host.slot.set(void 0); host.slot.set(''); "
                "var trace =",
            ),
            "host",
            4,
        ),
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

NULLABLE_OBSERVATIONS.update(
    {
        name: [
            (
                "false",
                "string",
                STRING_RESULT if name == "nullable_host_result_saved" else "future",
            ),
            ("true", "null_value", ""),
        ]
        for name in nullable_host_result_sources()
    }
)
NULLABLE_PAYLOAD_READBACKS.update(
    {
        name: [
            (
                argument,
                tag,
                value,
                tag,
                "selected" if name == "nullable_host_result_conditional" and value else value,
            )
            for argument, tag, value in (
                (repr(STRING_RESULT), "string", STRING_RESULT),
                ("null", "null_value", ""),
                ("void 0", "undefined", ""),
                ("''", "string", ""),
            )
        ]
        for name in nullable_host_result_sources()
    }
)


def nullable_host_result_refusals():
    source = nullable_host_result_sources()["nullable_host_result"][0]
    both = nullable_host_result_sources()["nullable_host_result_both"][0]
    return {
        # These call graphs are acyclic; only the live host result fact is
        # missing. Each exact repair is a separately admitted positive source.
        "nullable_host_result_unknown": (
            "var unknownResult = null;\n"
            + source.replace(
                "state.set(key, key); return state.get(key);",
                "state.set(key, unknownResult); return state.get(key);",
            ),
            2,
            "state.set(key, unknownResult);",
            "state.set(key, key);",
            1,
        ),
        "nullable_host_result_missing": (
            source.replace("return state.get(key);", "return state.get('missing');"),
            2,
            "return state.get('missing');",
            "return state.get(key);",
            1,
        ),
        "nullable_host_result_deleted": (
            both.replace("return state.get(key);", "state.delete(key); return state.get(key);"),
            1,
            "state.delete(key); ",
            "",
            2,
        ),
        "nullable_host_result_aliasing": (
            source.replace(
                "return state.get(key);",
                "state.set(key || 'fallback', true); return state.get(key);",
            ),
            2,
            "state.set(key || 'fallback', true); ",
            "",
            1,
        ),
    }


def nullable_nested_result_sources():
    source = nullable_payload_sources()["nullable_payload_mixed_readback"][0].replace(
        "host.slot.set(host.slot.get(false));",
        "host.slot.set(host.slot.set(host.slot.get(false)));",
    )
    saved = nullable_payload_sources()["nullable_payload_mixed_saved"][0].replace(
        "host.slot.set(host.slot.get(false));",
        "host.slot.set(host.slot.set(host.slot.get(false)));",
    )
    return {
        # Preserve the exact fifteen-call trace=2 refusal recorded in a8da7c27.
        # A proved invocation result seeds the next call to this same method;
        # the final generalized census still includes every actual and sibling.
        "nullable_nested_result": (source, "host", 2),
        "nullable_nested_result_same": (
            source.replace("host.slot.get(true)", "host.slot.get(false)"),
            "host",
            1,
        ),
        # These independent actuals occur after the nested invocation. The
        # first call cannot define the complete published method signature.
        "nullable_nested_result_identity": (
            source.replace(
                "var trace =",
                "host.slot.set(void 0); host.slot.set(''); host.slot.set('later'); var trace =",
            ),
            "host",
            5,
        ),
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

NULLABLE_OBSERVATIONS.update(
    {
        name: [
            (
                "false",
                "string",
                STRING_RESULT if name == "nullable_nested_result_saved" else "future",
            ),
            ("true", "null_value", ""),
        ]
        for name in nullable_nested_result_sources()
    }
)
NULLABLE_PAYLOAD_READBACKS.update(
    {
        name: [
            (argument, tag, value, tag, value)
            for argument, tag, value in (
                (repr(STRING_RESULT), "string", STRING_RESULT),
                ("null", "null_value", ""),
                ("void 0", "undefined", ""),
                ("''", "string", ""),
            )
        ]
        for name in nullable_nested_result_sources()
    }
)


def nullable_nested_result_refusals():
    source = nullable_nested_result_sources()["nullable_nested_result"][0]
    same = nullable_nested_result_sources()["nullable_nested_result_same"][0]
    return {
        # Every repair is exactly a separately gated positive. An earlier
        # valid invocation cannot authorize unknown or foreign result evidence,
        # a self-dependent unseeded read, a later bad actual or a bad sibling.
        "nullable_nested_unknown": (
            "var unknownResult = null;\n"
            + same.replace("state.set(key, key);", "state.set(key, unknownResult);"),
            2,
            "state.set(key, unknownResult);",
            "state.set(key, key);",
            "nullable_nested_result_same",
            15,
        ),
        "nullable_nested_foreign": (
            same.replace("return state.get(key);", "return new Map().get(key);"),
            2,
            "return new Map().get(key);",
            "return state.get(key);",
            "nullable_nested_result_same",
            15,
        ),
        "nullable_nested_unseeded": (
            same.replace("state.set(key, key);", "state.set(key, state.get(key));"),
            2,
            "state.set(key, state.get(key));",
            "state.set(key, key);",
            "nullable_nested_result_same",
            16,
        ),
        "nullable_nested_later_actual": (
            source.replace("var trace =", "host.slot.set({}); var trace ="),
            3,
            "host.slot.set({}); ",
            "",
            "nullable_nested_result",
            16,
        ),
        "nullable_nested_sibling": (
            source.replace(
                "size() { return state.size; }",
                "size() { state.set('late', {}); return state.size; }",
            ),
            3,
            "state.set('late', {}); ",
            "",
            "nullable_nested_result",
            16,
        ),
    }


def nullable_payload_refusals():
    source = nullable_payload_sources()["nullable_payload_readback"][0]
    mixed = nullable_payload_sources()["nullable_payload_mixed"][0]
    return {
        # Representing a missing result does not invent the host's independent
        # payload proof, nor authorize a snapshot or an object graph.
        "nullable_payload_missing": (
            source.replace("return state.get(key);", "return state.get('missing');").replace(
                "host.slot.set(host.slot.get(false));",
                "host.slot.set(host.slot.set(host.slot.get(false)));",
            ),
            3,
            "return state.get('missing');",
            "return state.get(key);",
            2,
        ),
        "nullable_payload_snapshot": (
            mixed.replace("state.set(key, key);", "state.set(key, key); state.values();"),
            3,
            "state.values();",
            "state.size;",
            3,
        ),
        "nullable_payload_object": (
            nullable_payload_sources()["nullable_payload_write"][0].replace(
                "state.set(key, key);", "state.set(key, {value: 'instance'});"
            ),
            2,
            "state.set(key, {value: 'instance'});",
            "state.set(key, key);",
            2,
        ),
    }


def mixed_nullable_payload_refusals():
    source = nullable_payload_sources()["nullable_payload_mixed_readback"][0]
    return {
        # Ownership and the actual census stay complete. A missing entry has
        # no independent nullable read proof, even though its key is typed.
        "nullable_mixed_read_missing": (
            source.replace("state.set(key, key);", "state.has(key);"),
            0,
            "state.has(key);",
            "state.set(key, key);",
            2,
        ),
        "nullable_mixed_read_deleted": (
            source.replace("return state.get(key);", "state.delete(key); return state.get(key);"),
            0,
            "state.delete(key); ",
            "",
            2,
        ),
        # A possibly-equal key really overwrites the String arm with Boolean.
        # The independent live return is then mixed and has no native carrier.
        "nullable_mixed_read_aliasing": (
            source.replace(
                "return state.get(key);",
                "state.set(key || 'fallback', true); return state.get(key);",
            ),
            3,
            "state.set(key || 'fallback', true); ",
            "",
            2,
        ),
    }


def nullable_key_refusals():
    source = nullable_key_sources()["nullable_key_mixed"][0]
    return {
        # A supported key carrier does not expose mixed-key snapshots through
        # the published method contract or manufacture an iterator proof.
        "nullable_key_snapshot": (
            source.replace("state.set(key, true);", "state.set(key, true); state.keys();"),
            2,
            "state.keys();",
            "state.size;",
            2,
        ),
    }


def seeded_carrier_refusals():
    return {
        "result_seeded_number_string_contents": (
            mixed_result_sources()["result_seeded_mixed_contents"][0].replace(
                "state.set(0, true)", "state.set(0, 'old')"
            ),
            2,
        ),
    }


MIXED_RESULT_TYPES = {
    "result_seeded_mixed_contents": ("js_num", "double"),
    "result_seeded_join_reseed": ("js_num", "double"),
    "result_seeded_bool_string_contents": ("ctnative::js_boolean_t", "std::string"),
    "result_seeded_mixed_false_zero": ("ctnative::js_boolean_t", "double"),
    "result_seeded_mixed_false": ("ctnative::js_boolean_t", "double"),
    "result_seeded_mixed_empty_key": ("std::string", "std::string"),
    "result_seeded_mixed_string_saved": ("std::string", "std::string"),
    "saved_read_write": ("std::string", "std::string"),
    "saved_read_write_false": ("ctnative::js_boolean_t", "std::string"),
    "saved_read_write_number": ("js_num", "double"),
    "saved_read_write_repeated": ("std::string", "std::string"),
    "saved_read_write_string_saved": ("std::string", "std::string"),
    "saved_read_write_wrong_tag": ("ctnative::js_boolean_t", "std::string"),
    "saved_read_write_overwritten": ("ctnative::js_boolean_t", "std::string"),
    "saved_join": ("std::string", "std::string"),
    "saved_join_always_empty": ("std::string", "std::string"),
    "saved_join_distinct": ("std::string", "std::string"),
    "saved_join_bool": ("ctnative::js_boolean_t", "std::string"),
    "saved_join_number": ("js_num", "double"),
    "saved_join_string_saved": ("std::string", "std::string"),
    "guarded_saved_read": ("std::string", "std::string"),
    "guarded_saved_no_delete": ("std::string", "std::string"),
    "guarded_saved_always_empty": ("std::string", "std::string"),
    "guarded_saved_bool": ("ctnative::js_boolean_t", "std::string"),
    "guarded_saved_number": ("js_num", "double"),
    "guarded_saved_string_saved": ("std::string", "std::string"),
    "shortcircuit_same_tag": ("std::string", "std::string"),
    "shortcircuit_distinct": ("std::string", "std::string"),
    "shortcircuit_empty_string": ("std::string", "std::string"),
    "shortcircuit_bool": ("ctnative::js_boolean_t", "std::string"),
    "shortcircuit_false": ("ctnative::js_boolean_t", "std::string"),
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
    "result_bool": ("ctnative::js_boolean_t", "ctnative::js_boolean_t", 6),
    "result_delete": ("ctnative::js_boolean_t", "ctnative::js_boolean_t", 6),
    "result_string": ("std::string", "std::string", 6),
    "result_formal": ("js_num", "js_num", 6),
    "result_seeded_map_get": ("js_num", "js_num", 5),
    "result_seeded_repeated": ("js_num", "js_num", 5),
    "result_seeded_overwrite": ("js_num", "js_num", 5),
    "result_seeded_growing": ("js_num", "js_num", 5),
    "result_seeded_formal": ("js_num", "js_num", 6),
    **{name: ("js_num", "js_num", 5) for name in key_fact_sources()},
    **{
        name: ("js_num", "js_num", 6 if name == "seeded_dynamic_formal" else 5)
        for name in joined_result_sources()
    },
    **{name: ("js_num", "js_num", 5) for name in size_result_sources()},
    **{
        name: (
            ("std::string" if "string" in name else "ctnative::js_boolean_t"),
            ("std::string" if "string" in name else "ctnative::js_boolean_t"),
            6,
        )
        for name in payload_result_sources()
    },
    **{name: (result, result, 6) for name, (result, _) in MIXED_RESULT_TYPES.items()},
    **{
        name: ("ctnative::nullable_string", "ctnative::nullable_string", 6)
        for name in {
            **nullable_result_sources(),
            **nullable_key_sources(),
            **nullable_payload_sources(),
            **nullable_host_result_sources(),
            **nullable_nested_result_sources(),
        }
    },
}


def refusal_sources():
    return {
        "mutable_capture": SOURCE.replace("return {", "state = new Map(); return {"),
        "cyclic_payload": SOURCE.replace(
            "return state.size;", "state.set('x', state); return state.size;"
        ),
        "object_payload": SOURCE.replace(
            "return state.size;", "state.set('x', {}); return state.size;"
        ),
        "map_key": SOURCE.replace("return state.size;", "state.set(state, 1); return state.size;"),
        "detached_method": SOURCE.replace(
            "return state.size;", "var set = state.set; set('x', 1); return state.size;"
        ),
        "set_wrong_arity": SOURCE.replace(
            "return state.size;", "state.set('x'); return state.size;"
        ),
        "get_wrong_arity": SOURCE.replace(
            "return state.size;", "state.get('x', 1); return state.size;"
        ),
        "method_escape": SOURCE.replace("return state.size;", "return state.set;"),
        "map_return": SOURCE.replace("return state.size;", "return state.set('x', 1);"),
        "method_replaced": SOURCE.replace(
            "return state.size;", "state.set = 1; return state.size;"
        ),
        "snapshot": SOURCE.replace("return state.size;", "state.keys(); return state.size;"),
        "published_map": SOURCE.replace("return {", "host.map = state; return {"),
        "map_alias": SOURCE.replace("return {", "var saved = state; return {"),
        "replaced_map": "Map = function() {};\n" + SOURCE,
        "late_replaced_map": SOURCE + "\nMap = function() {};",
        "map_prototype": SOURCE.replace("const state", "Map.prototype.extra = 1; const state"),
        "constructed_args": SOURCE.replace("new Map()", "new Map([])"),
        "second_map": SOURCE.replace("return {", "new Map(); return {"),
        "second_factory": SOURCE.replace(
            "host.slot = factory();", "host.slot = factory(); factory();"
        ),
        "published_owner": SOURCE + "\nvar alias = host;",
        "published_table": SOURCE + "\nvar alias = host.slot;",
        "published_callable": SOURCE + "\nvar alias = host.slot.get;",
        "table_replaced": SOURCE + "\nhost.slot = {};",
        "getter_replaced": SOURCE + "\nhost.slot.get = function() { return 7; };",
        "receiver": SOURCE.replace("return state.size;", "return this;"),
        "arguments": SOURCE.replace("return state.size;", "return arguments.length;"),
        "call_argument": SOURCE.replace("host.slot.get();", "host.slot.get(1);"),
        "effect": "var side = 0;\n"
        + SOURCE.replace("return state.size;", "side = 1; return state.size;"),
        "throw": SOURCE.replace("return state.size;", "throw 7;"),
        "unknown": SOURCE + "\ninspect(host);",
    }


def parameter_refusals():
    return {
        "parameter_missing": PARAMETER + "\nhost.slot.set();",
        "parameter_extra": PARAMETER + "\nhost.slot.set('y', 1);",
        "parameter_heterogeneous": PARAMETER + "\nhost.slot.set(1);",
        "parameter_object": PARAMETER.replace("host.slot.set('x');", "host.slot.set({});"),
        "parameter_callback": PARAMETER.replace(
            "host.slot.set('x');", "host.slot.set(function() { return 'x'; });"
        ),
        "parameter_unproved": PARAMETER.replace("host.slot.set('x');", "host.slot.set(host.key);"),
        "parameter_getter_extra": PARAMETER.replace("host.slot.get();", "host.slot.get('x');"),
        "parameter_second_missing": parameter_sources()["shared_two_parameters"][0]
        + "\nhost.slot.set('z');",
        "parameter_second_heterogeneous": parameter_sources()["shared_two_parameters"][0]
        + "\nhost.slot.set('z', true);",
    }


def result_refusals():
    return {
        "result_unknown_map_get": CALL_RESULT.replace(
            "get() { return state.size; }", "get() { return state.get(0); }"
        ),
        "result_unknown_effect": CALL_RESULT.replace(
            "get() { return state.size; }", "get() { inspect(state); return state.size; }"
        ),
        "result_unknown_call": CALL_RESULT.replace(
            "get() { return state.size; }", "get() { state.has(0); return inspect(); }"
        ),
        "result_recursive": CALL_RESULT.replace(
            "get() { return state.size; }", "get() { state.has(0); return host.slot.get(); }"
        ),
        "result_mixed_return": CALL_RESULT.replace(
            "get() { return state.size; }",
            "get() { if (state.has(0)) { return 1; } return false; }",
        ),
        "result_mixed_actual": CALL_RESULT + "\nhost.slot.set('x');",
        "result_missing_actual": CALL_RESULT.replace(
            "get() { return state.size; }", "get(key) { state.has(key); return state.size; }"
        ),
        "result_map_return": CALL_RESULT.replace(
            "get() { return state.size; }", "get() { return state; }"
        ),
    }


def seeded_result_refusals():
    # A nonempty size is distinct from key zero. Seed key one so these writes
    # really can overwrite the queried entry with an incompatible payload.
    aliasing = SEEDED_RESULT.replace("state.set(0, 1)", "state.set(1, 1)")
    aliasing = aliasing.replace("state.get(0)", "state.get(1)")
    return {
        "seeded_missing_key": SEEDED_RESULT.replace("return state.get(0);", "return state.get(1);"),
        "seeded_cleared": SEEDED_RESULT.replace(
            "return state.get(0);", "state.clear(); return state.get(0);"
        ),
        "seeded_deleted": SEEDED_RESULT.replace(
            "return state.get(0);", "state.delete(0); return state.get(0);"
        ),
        "seeded_dynamic_bool_join": aliasing.replace(
            "return state.get(1);", "state.set(state.size, true); return state.get(1);"
        ),
        "seeded_dynamic_string_join": aliasing.replace(
            "return state.get(1);", "state.set(state.size, 'other'); return state.get(1);"
        ),
        "seeded_dynamic_unknown_join": aliasing.replace(
            "return state.get(1);", "state.set(state.size, state.get(9)); return state.get(1);"
        ),
        "seeded_dynamic_later_join": aliasing.replace(
            "return state.get(1);",
            "state.set(state.size, true); state.set(state.size, 2); return state.get(1);",
        ),
        "seeded_dynamic_unseeded": SEEDED_RESULT.replace(
            "state.set(0, 1);", "state.set(state.size, 2);"
        ),
        "seeded_overwritten_unknown": SEEDED_RESULT.replace(
            "return state.get(0);", "state.set(0, state.get(9)); return state.get(0);"
        ),
        "seeded_deleted_earlier": SEEDED_RESULT.replace(
            "return state.get(0);", "state.set(1, 2); state.delete(0); return state.get(0);"
        ),
        "seeded_unknown_payload": SEEDED_RESULT.replace(
            "state.set(0, 1);", "state.set(0, state.get(9));"
        ),
        "seeded_distinct_formals": SEEDED_RESULT.replace(
            "get() { state.set(0, 1); return state.get(0); }",
            "get(key, other) { state.set(key, 1); return state.get(other); }",
        ).replace("host.slot.get()", "host.slot.get(7, 8)"),
    }
