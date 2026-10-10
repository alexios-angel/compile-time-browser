"""JavaScript sources for the global-Map ownership tests where a Map read can
return null or undefined.

Most sources are built by string replacement from base sources in
scalar/maps.py. `hosted(source, trace)` pairs a source with the `trace` value
it must print. The NULLABLE_*_CALLS tables pin how many ctjs calls each source
imports to; driver/execution.py fails if an edit changes that count. Refusal
tuples are (source, trace, snippet causing the refusal, replacement that fixes
it, trace after the fix[, name of the fixed positive source, its call count]).
"""

from .scalar.maps import *


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
        # Keeps the eighteen ctjs calls of shortcircuit_same_tag; only the key
        # passed to state.set is normalized (`key || 'missing'`), so the
        # nullable return is tested without real null Map keys.
        "nullable_normalized": hosted(saved, 3),
        "nullable_ternary": hosted(
            saved.replace("return result || null;", "return result ? result : null;"), 3
        ),
        # `void 0` imports as a literal; a bare `undefined` would be a separate
        # host global read.
        "nullable_undefined": hosted(
            saved.replace("return result || null;", "return result || (void 0);"), 3
        ),
        "nullable_empty": hosted(
            saved.replace("state.set('other', 'future');", "state.set('other', '');").replace(
                " host.slot.set(host.slot.get(true));", ""
            ),
            3,
        ),
        "nullable_threeway": hosted(threeway, 3),
        # No Boolean key here, so an incorrect nullable-to-String conversion
        # cannot hide behind the Boolean/String variant wrapper.
        "nullable_homogeneous_key": hosted(homogeneous, 2),
        # Startup observes String only. A saved getter later returns null on
        # true; both results survive deletion and destruction of their Map.
        "nullable_string_saved": hosted(owning, 2),
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
        # The 11-call source and its 13-call identity variant (see
        # NULLABLE_KEY_CALLS) use only String/null keys and store only `true`,
        # so they test nullable keys without Boolean keys or nullable stored
        # values.
        "nullable_key_homogeneous": hosted(homogeneous, 2),
        "nullable_key_identity": hosted(identity, 4),
        "nullable_key_identity_normalized": hosted(
            identity.replace("state.set(key, true)", "state.set(key || 'missing', true)"), 2
        ),
        "nullable_key_second_use": hosted(
            normalized.replace(
                "state.set(key || 'missing', true)",
                "state.set(key || 'missing', true); state.set(key, true)",
            ),
            3,
        ),
        "nullable_key_mixed": hosted(mixed, 2),
        "nullable_original_key": hosted(
            saved.replace("state.set(key || 'missing', true)", "state.set(key, true)"), 3
        ),
        # The normalized first use proves its own String key. The second use
        # passes the raw parameter, which may be null; that widens only the
        # Map's key type.
        "nullable_second_key_use": hosted(
            saved.replace(
                "state.set(key || 'missing', true)",
                "state.set(key || 'missing', true); state.set(key, true)",
            ),
            4,
        ),
        # Startup calls only with false; the saved methods later delete and
        # reinsert keys of every nullable tag and must keep their own copy of
        # the String bytes after the caller's entry changes.
        "nullable_key_string_saved": hosted(owning, 2),
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
        # These two sources must stay byte-for-byte as they are: their ctjs
        # call counts are pinned in NULLABLE_PAYLOAD_CALLS (11 and 12).
        "nullable_payload_write": hosted(write, 2),
        "nullable_payload_readback": hosted(readback, 2),
        # Nullable String storage represents a read after delete as undefined;
        # the return must not reuse the old payload or its absent tag.
        "nullable_payload_deleted": hosted(
            readback.replace("return state.get(key);", "state.delete(key); return state.get(key);"),
            0,
        ),
        "nullable_payload_identity": hosted(identity, 4),
        # Each read inside the getter is proved on its own, even though the
        # Map as a whole also stores null and Boolean.
        "nullable_payload_mixed": hosted(
            nullable_key_sources()["nullable_original_key"][0].replace(
                "state.set(key, true)", "state.set(key, key)"
            ),
            3,
        ),
        # Startup sees only false; the saved methods later run with both flags
        # and return an owned copy of the payload taken before the entry is
        # overwritten and deleted.
        "nullable_payload_saved": hosted(saved, 0),
        # Fourteen ctjs calls, pinned. The extra state.set('extra', true) adds
        # Boolean to what the Map stores, but each later get must still be
        # proved to return String or null.
        "nullable_payload_mixed_readback": hosted(mixed_readback, 2),
        # Nineteen ctjs calls, pinned. It compiles because the getter only
        # ever returns String or null, which the nullable-string carrier can
        # hold even though the Map also stores Booleans.
        "nullable_mixed_payload_readback": hosted(original_mixed_readback, 3),
        "nullable_payload_mixed_identity": hosted(
            mixed_readback.replace(
                "var trace =", "host.slot.set(void 0); host.slot.set(''); var trace ="
            ),
            4,
        ),
        # Wider storage and a later Boolean overwrite of the same key must not
        # widen a saved nullable read or leave it borrowing the old entry.
        "nullable_payload_mixed_saved": hosted(
            saved.replace("state.set(key, 'overwritten');", "state.set(key, true);"), 0
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
    # Rows are (JS argument, input tag, input bytes, expected return tag,
    # expected return bytes); the input and the expected return are listed
    # separately so they can differ.
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
        # Do not edit this source: its fifteen ctjs calls
        # (NULLABLE_HOST_RESULT_CALLS) and trace=1 are pinned, and its call
        # graph has no cycles. The last call passes the setter's nullable
        # result to size(), so lowering must prove that result on its own.
        "nullable_host_result": hosted(source, 1),
        "nullable_host_result_both": hosted(both, 2),
        "nullable_host_result_identity": hosted(
            source.replace(
                "var trace =",
                "host.slot.set(host.slot.get(true)); host.slot.set(void 0); host.slot.set(''); "
                "var trace =",
            ),
            4,
        ),
        # The conditional write means the stored value may be the key or
        # 'selected', so the result proof must merge both. The NativeMap
        # Presence analysis (lib/CTNative/Analysis/NativeMap/Presence.cpp)
        # does not cover this case.
        "nullable_host_result_conditional": hosted(conditional, 2),
        # The copied result reaches size() after its Map entry was overwritten
        # with a Boolean and deleted. Startup passes false; saved calls pass both.
        "nullable_host_result_saved": hosted(saved, 1),
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
        # These call graphs have no cycles. The only thing missing is a proof
        # of the host method's result. Applying each row's replacement gives a
        # source that must compile (nullable_host_result or
        # nullable_host_result_both).
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
        # Do not edit this source: its fifteen ctjs calls and trace=2 are
        # pinned. The proved result of one host.slot.set call is passed
        # straight into another call to the same method, and the method's
        # final signature must still cover every argument passed to it and
        # every sibling method.
        "nullable_nested_result": hosted(source, 2),
        "nullable_nested_result_same": hosted(
            source.replace("host.slot.get(true)", "host.slot.get(false)"), 1
        ),
        # These further arguments are passed after the nested call, so the
        # first call alone cannot define the method's full signature.
        "nullable_nested_result_identity": hosted(
            source.replace(
                "var trace =",
                "host.slot.set(void 0); host.slot.set(''); host.slot.set('later'); var trace =",
            ),
            5,
        ),
        # Both nested calls copy their nullable read before the same key is
        # overwritten with a Boolean and deleted. Startup passes false; later
        # calls pass both flags.
        "nullable_nested_result_saved": hosted(saved, 0),
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
        # Applying each row's replacement gives the named positive source,
        # which is tested on its own. These rows check that a valid earlier
        # call does not make the compiler accept an unknown or foreign result,
        # a read that depends on itself, or a bad later argument or sibling.
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
        # Being able to represent a missing result does not prove what the
        # host stored, and does not allow a snapshot (values()) or an object
        # payload.
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
        # Ownership and the set of arguments are fully known here, but a read
        # of a missing or deleted entry still has no nullable-read proof, even
        # though its key type is known.
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
        # A key that may equal `key` really overwrites the String value with a
        # Boolean, so the returned value is mixed and has no native carrier.
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
        # Supporting the key type does not make a mixed-key snapshot
        # (keys()) available through the method's signature, nor prove the
        # iterator.
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
    # A nonempty size is not the same as key 0 being present. Seed key 1 so
    # these writes really can overwrite the queried entry with an
    # incompatible payload.
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
