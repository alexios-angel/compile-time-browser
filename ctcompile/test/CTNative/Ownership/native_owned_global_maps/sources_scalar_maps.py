"""sources scalar maps: continued from sources_scalar_results."""

from .sources_scalar_results import *


def saved_read_sources():
    boolean = payload_result_sources()["result_seeded_bool"][0]
    getter = "state.set(false, true); return state.get(false);"
    saved = boolean.replace(
        getter,
        "state.set('', ''); const saved = state.get(''); state.set(false, true); "
        "state.set(false, saved); const result = state.get(false); "
        "state.delete(false); return result;",
    )
    number = mixed_result_sources()["result_seeded_mixed_contents"][0].replace(
        "state.set(0, true); state.set(0, 1); return state.get(0);",
        "state.set(1, 1); const saved = state.get(1); state.set(1, true); "
        "state.set(false, true); state.set(false, saved); const result = state.get(false); "
        "state.delete(false); return result;",
    )
    return {
        # Exact next boundary from HANDOFF: an empty String read passes through
        # a nonliteral write into mixed storage and remains a present String.
        "saved_read_write": (saved, "host", 1),
        # The payload's old scalar value survives mutation of its source entry;
        # its SSA tag must not be recovered from the entry's later contents.
        "saved_read_write_false": (
            boolean.replace(
                getter,
                "state.set('', 'old'); state.set(false, false); const saved = state.get(false); "
                "state.set(false, true); state.set('', saved); const result = state.get(''); "
                "state.delete(''); return result;",
            ),
            "host",
            1,
        ),
        "saved_read_write_number": (number, "host", 1),
        "saved_read_write_repeated": (
            saved.replace(
                "host.slot.set(host.slot.get());",
                "host.slot.set(host.slot.get()); host.slot.set(host.slot.get());",
            ),
            "host",
            1,
        ),
        # Both intermediate reads must own their String. The first source entry
        # is overwritten and deleted before the saved value is written back;
        # the second is overwritten and deleted before the method returns.
        "saved_read_write_string_saved": (
            boolean.replace(
                getter,
                f"state.set('seed', '{STRING_RESULT}'); state.set('{STRING_RESULT}', 'stored'); "
                "const saved = state.get('seed'); state.set('seed', false); state.delete('seed'); "
                "state.set(false, true); state.set(false, saved); const result = state.get(false); "
                "state.set(false, false); state.delete(false); return result;",
            ),
            "host",
            1,
        ),
        # Valid Boolean results must keep their actual tag, even after a saved
        # String read exists or has briefly been stored at the same key.
        "saved_read_write_wrong_tag": (
            saved.replace("state.set(false, saved);", "state.set(false, true);"),
            "host",
            2,
        ),
        "saved_read_write_overwritten": (
            saved.replace(
                "const result = state.get(false);",
                "state.set(false, true); const result = state.get(false);",
            ),
            "host",
            2,
        ),
    }


def saved_read_refusals():
    saved = saved_read_sources()["saved_read_write"][0]
    return {
        # A later read after deletion is missing even though an earlier read
        # of another key still carries the same saved scalar tag.
        "saved_read_write_deleted": (
            saved.replace(
                "const result = state.get(false); state.delete(false);",
                "state.delete(false); const result = state.get(false);",
            ),
            2,
            "return result;",
            "return saved;",
        ),
        "saved_read_write_missing_key": (
            saved.replace("const result = state.get(false);", "const result = state.get(true);"),
            2,
            "const result = state.get(true);",
            "const result = state.get(false);",
        ),
        # Unknown must not become String merely because the Map also stores
        # Strings. Reseeding the empty key in the setter makes undefined differ
        # from the real empty String in the final size observation.
        "saved_read_write_missing_source": (
            saved.replace(
                "const saved = state.get('');", "state.delete(''); const saved = state.get('');"
            ).replace("state.set(key, true)", "state.set('', true); state.set(key, true)"),
            2,
            "state.delete('');",
            "state.has('');",
        ),
    }


def saved_join_sources():
    saved = saved_read_sources()["saved_read_write"][0].replace("get() {", "get(flag) {")
    saved = saved.replace(
        "state.set('', ''); const saved = state.get('');",
        "state.set('', ''); state.set('other', 'future'); "
        "const saved = flag ? state.get('other') : state.get('');",
    )
    saved = saved.replace(
        "host.slot.set(host.slot.get());",
        "host.slot.set(host.slot.get(false)); host.slot.set(host.slot.get(true));",
    )
    distinct = saved.replace("state.set('', '');", "state.set('', 'first');")
    boolean = distinct.replace(
        "state.set('', 'first'); state.set('other', 'future');",
        "state.set('', false); state.set('other', true);",
    )
    boolean = boolean.replace(
        "state.set(false, true); state.set(false, saved);",
        "state.set('', 'changed'); state.set('other', 'changed'); "
        "state.set('temp', 'old'); state.set('temp', saved);",
    )
    boolean = boolean.replace(
        "const result = state.get(false); state.delete(false);",
        "const result = state.get('temp'); state.delete('temp');",
    )
    number = distinct.replace(
        "state.set('', 'first'); state.set('other', 'future');", "state.set(0, 2); state.set(1, 3);"
    )
    number = number.replace(
        "flag ? state.get('other') : state.get('')", "flag ? state.get(1) : state.get(0)"
    )
    number = number.replace(
        "state.set(false, true); state.set(false, saved);",
        "state.set(0, true); state.set(1, true); "
        "state.set(false, true); state.set(false, saved);",
    )
    owning = saved.replace(
        "state.set('', ''); state.set('other', 'future');",
        f"state.set('', '{STRING_RESULT}'); state.set('other', '{OTHER_STRING_RESULT}'); "
        f"state.set('{STRING_RESULT}', 'stored'); state.set('{OTHER_STRING_RESULT}', 'stored');",
    )
    owning = owning.replace(
        "state.set(false, true);",
        "state.set('', false); state.delete(''); "
        "state.set('other', false); state.delete('other'); state.set(false, true);",
    )
    owning = owning.replace("state.delete(false);", "state.set(false, false); state.delete(false);")
    # Startup sees false only. The typed C++ lifetime caller later selects both
    # branches, after the publishing owner and table have already been released.
    owning = owning.replace(" host.slot.set(host.slot.get(true));", "")
    return {
        # Exact 16-call boundary from the preceding handoff. Both runtime calls
        # must remain: choosing the empty arm unconditionally yields size 2.
        "saved_join": (saved, "host", 3),
        "saved_join_always_empty": (
            saved.replace("flag ? state.get('other') : state.get('')", "state.get('')"),
            "host",
            2,
        ),
        # Distinct result keys make either constant-arm substitution observable.
        "saved_join_distinct": (distinct, "host", 4),
        "saved_join_bool": (boolean, "host", 4),
        "saved_join_number": (number, "host", 4),
        "saved_join_string_saved": (owning, "host", 2),
    }


def saved_join_refusals():
    saved = saved_join_sources()["saved_join_distinct"][0]
    true_key = saved.replace(
        "state.set(key, true)", "state.set('future', true); state.set(key, true)"
    )
    false_key = saved.replace(
        "state.set(key, true)", "state.set('first', true); state.set(key, true)"
    )
    deleted = true_key.replace(
        "const saved = flag ?", "state.delete('other'); const saved = flag ?"
    )
    deleted = deleted.replace(
        "state.set(false, true);", "state.set('other', 'restored'); state.set(false, true);"
    )
    return {
        # The untouched arm cannot supply the other arm's presence or scalar
        # type. Preseed the expected result key so undefined stays observable.
        "saved_join_missing_true": (
            true_key.replace("flag ? state.get('other')", "flag ? state.get('missing')"),
            5,
            "state.get('missing')",
            "state.get('other')",
            4,
        ),
        "saved_join_missing_false": (
            false_key.replace(": state.get('');", ": state.get('missing');"),
            5,
            "state.get('missing')",
            "state.get('')",
            4,
        ),
        # Restoring the source after the read must not restore its old value.
        "saved_join_deleted_true": (deleted, 5, "state.delete('other');", "state.has('other');", 4),
        "saved_join_mixed_tags": (
            true_key.replace("state.set('other', 'future');", "state.set('other', true);"),
            5,
            "state.set('other', true);",
            "state.set('other', 'future');",
            4,
        ),
    }


def guarded_saved_sources():
    conditional = saved_join_sources()
    saved = conditional["saved_join"][0].replace(
        "const saved = flag ? state.get('other') : state.get('');",
        "if (flag) { state.delete('other'); } "
        "const saved = state.has('other') ? state.get('other') : state.get('');",
    )
    boolean = conditional["saved_join_bool"][0].replace(
        "const saved = flag ? state.get('other') : state.get('');",
        "if (flag) { state.delete('other'); } "
        "const saved = state.has('other') ? state.get('other') : state.get('');",
    )
    number = conditional["saved_join_number"][0].replace(
        "const saved = flag ? state.get(1) : state.get(0);",
        "if (flag) { state.delete(1); } "
        "const saved = state.has(1) ? state.get(1) : state.get(0);",
    )
    owning = conditional["saved_join_string_saved"][0].replace(
        f"state.set('', '{STRING_RESULT}'); state.set('other', '{OTHER_STRING_RESULT}');",
        f"state.set('', '{OTHER_STRING_RESULT}'); state.set('other', '{STRING_RESULT}');",
    )
    owning = owning.replace(
        "const saved = flag ? state.get('other') : state.get('');",
        "if (flag) { state.delete('other'); } "
        "const saved = state.has('other') ? state.get('other') : state.get('');",
    )
    return {
        # Exact 18-call next boundary from HANDOFF. A deletion join loses
        # unconditional membership, but preserves the tag whenever present.
        "guarded_saved_read": (saved, "host", 2),
        "guarded_saved_no_delete": (
            saved.replace("if (flag) { state.delete('other'); }", "state.has('other');"),
            "host",
            3,
        ),
        "guarded_saved_always_empty": (
            saved.replace(
                "state.has('other') ? state.get('other') : state.get('')", "state.get('')"
            ),
            "host",
            1,
        ),
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
    stale = saved.replace(deletion, "const present = state.has('other'); " + deletion)
    stale = stale.replace(guard, "present ? state.get('other') : state.get('')")
    other_map = saved.replace(
        deletion, "const guardState = new Map(); guardState.set('other', true); " + deletion
    )
    other_map = other_map.replace(
        guard, "guardState.has('other') ? state.get('other') : state.get('')"
    )
    mutated = saved.replace(
        guard, "state.has('other') ? (state.delete('other'), state.get('other')) : state.get('')"
    )
    mutated = mutated.replace(
        "state.set(key, true)", "state.set('future', true); state.set(key, true)"
    )
    return {
        # Membership cannot supply a tag absent from the method's live local
        # facts, even when startup always takes the independently typed fallback.
        "guarded_saved_missing_tag": (
            missing,
            1,
            "state.has('other'); " + deletion,
            "state.set('other', 'future'); " + deletion,
            2,
        ),
        "guarded_saved_wrong_key": (
            saved.replace(guard, "state.has('') ? state.get('other') : state.get('')"),
            3,
            "state.has('') ?",
            "state.has('other') ?",
            2,
        ),
        "guarded_saved_wrong_map": (
            other_map,
            3,
            "guardState.has('other') ?",
            "state.has('other') ?",
            2,
        ),
        # A saved Boolean is not a membership proof after a same-key mutation.
        "guarded_saved_stale_has": (
            stale,
            3,
            "const present = state.has('other'); " + deletion,
            deletion + " const present = state.has('other');",
            2,
        ),
        "guarded_saved_mutated_arm": (
            mutated,
            3,
            "(state.delete('other'), state.get('other'))",
            "state.get('other')",
            2,
        ),
        # Both entries exist here, but the guarded key has different payload
        # tags on the two paths. A has check must not erase that disagreement.
        "guarded_saved_disagreeing_tag": (
            saved.replace(deletion, "if (flag) { state.set('other', true); }"),
            4,
            "if (flag) { state.set('other', true); }",
            deletion,
            2,
        ),
        # Both syntactic arms still require proof for a literal predicate.
        "guarded_saved_literal_false": (
            saved.replace(guard, "false ? state.get('other') : state.get('')"),
            1,
            "false ?",
            "state.has('other') ?",
            2,
        ),
        "guarded_saved_literal_true": (
            saved.replace(guard, "true ? state.get('other') : state.get('')"),
            3,
            "true ?",
            "state.has('other') ?",
            2,
        ),
    }


def shortcircuit_sources():
    guarded = guarded_saved_sources()
    guard = "state.has('other') ? state.get('other') : state.get('')"
    short = "(state.has('other') && state.get('other')) || state.get('')"
    saved = guarded["guarded_saved_read"][0].replace(guard, short)
    distinct = saved.replace("state.set('', '');", "state.set('', 'first');")
    boolean = guarded["guarded_saved_bool"][0].replace(guard, short)
    false_value = boolean.replace(
        "state.set('', false); state.set('other', true);",
        "state.set('', true); state.set('other', false);",
    )
    false_value = false_value.replace(
        "state.set(key, true)", "state.set(true, true); state.set(key, true)"
    )
    number = guarded["guarded_saved_number"][0].replace(
        "state.has(1) ? state.get(1) : state.get(0)",
        "(state.has(1) && state.get(1)) || state.get(0)",
    )
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
    other_map = saved.replace(
        deletion, "const guardState = new Map(); guardState.set('different', true); " + deletion
    )
    other_map = other_map.replace(
        short, "(guardState.has('other') && state.get('other')) || state.get('')"
    )
    stale = saved.replace(
        "state.set('other', 'future');",
        "const present = state.has('other'); state.set('other', 'future');",
    )
    stale = stale.replace(short, "(present && state.get('other')) || state.get('')")
    mutated = saved.replace(
        short,
        "(state.has('other') && (state.delete('other'), state.get('other'))) || state.get('')",
    )
    # The unknown call is unexecuted during startup, but its future flag arm
    # remains part of the published method and cannot receive a complete proof.
    effect = saved.replace(deletion, "if (flag) { inspect(state); }")
    effect = effect.replace(" host.slot.set(host.slot.get(true));", "")
    return {
        "shortcircuit_missing_tag": (
            missing,
            1,
            "state.has('other'); " + deletion,
            "state.set('other', 'future'); " + deletion,
            2,
        ),
        "shortcircuit_wrong_key": (
            saved.replace(short, "(state.has('missing') && state.get('other')) || state.get('')"),
            1,
            "state.has('missing')",
            "state.has('other')",
            2,
        ),
        "shortcircuit_wrong_map": (
            other_map,
            1,
            "guardState.has('other')",
            "state.has('other')",
            2,
        ),
        "shortcircuit_stale_has": (
            stale,
            1,
            "const present = state.has('other'); state.set('other', 'future'); " + deletion,
            "state.set('other', 'future'); " + deletion + " const present = state.has('other');",
            2,
        ),
        "shortcircuit_mutated_arm": (
            mutated,
            1,
            "(state.delete('other'), state.get('other'))",
            "state.get('other')",
            2,
        ),
        "shortcircuit_disagreeing_tag": (
            saved.replace(deletion, "if (flag) { state.set('other', true); }"),
            4,
            "if (flag) { state.set('other', true); }",
            deletion,
            2,
        ),
        "shortcircuit_missing_fallback": (
            saved.replace(
                short, "(state.has('other') && state.get('other')) || state.get('missing')"
            ),
            3,
            "state.get('missing')",
            "state.get('')",
            2,
        ),
        "shortcircuit_nullable": (
            saved.replace(short, "(state.has('other') && state.get('other')) || null"),
            3,
            "|| null",
            "|| state.get('')",
            2,
        ),
        "shortcircuit_unknown_effect": (effect, 3, "if (flag) { inspect(state); }", deletion, 3),
    }
