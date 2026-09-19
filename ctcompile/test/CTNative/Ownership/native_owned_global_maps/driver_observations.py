"""driver observations: continued from driver_nullable_observations."""

from .driver_nullable_observations import *


def check_source_observations(args, node, reference, positives):
    check_leaf_object_observations(args, node, reference)
    check_leaf_readback_observations(args, node, reference)
    check_comparison_identity_observations(args, node, reference)
    check_leaf_absence_observations(args, node, reference)
    check_primitive_absence_observations(args, node, reference)
    check_leaf_clear_observations(args, node, reference)
    check_numeric_entry_observations(args, node, reference)
    check_string_field_observations(args, node, reference)
    check_zero_size_observations(args, node, reference)
    check_one_size_observations(args, node, reference)
    check_delete_size_observations(args, node, reference)
    check_join_size_observations(args, node, reference)
    check_mutation_size_observations(args, node, reference)
    overwrite_source, _, overwrite_value = positives["seeded_dynamic_overwrite"]
    blind = args.work / "seeded-dynamic-overwrite-blinded.js"
    blind.write_text(overwrite_source.replace("return state.get(1);", "return 1;"))
    if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == f"trace={overwrite_value}\n":
        raise RuntimeError("dynamic overwrite witness cannot distinguish retaining the old payload")
    for name in ("seeded_size_saved", "seeded_size_two_saved", "seeded_size_two_saved_empty"):
        saved_source, _, saved_value = positives[name]
        blind = args.work / f"{name}-snapshot-blinded.js"
        blind.write_text(saved_source.replace("state.delete(saved)", "state.delete(state.size)"))
        if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == f"trace={saved_value}\n":
            raise RuntimeError(f"{name}: saved size witness cannot distinguish a current-size read")
    for name in ("seeded_size_two_saved", "seeded_size_after_delete"):
        live_source, _, live_value = positives[name]
        blind = args.work / f"{name}-delete-blinded.js"
        blind.write_text(live_source.replace("state.delete(", "state.has("))
        if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == f"trace={live_value}\n":
            raise RuntimeError(f"{name}: size witness cannot distinguish a real deletion")
    for name, old, replacements in (
        ("result_seeded_false", "return state.get(false);", ("return undefined;",)),
        ("result_seeded_empty_string", "return state.get('');", ("return undefined;",)),
        ("result_seeded_false_overwrite", "return state.get(false);", ("return true;",)),
        (
            "result_seeded_string_saved",
            "return saved;",
            ("return state.get('seed');", "return 'changed';"),
        ),
        ("result_seeded_mixed_false_zero", "state.set(false, 1);", ("state.set(0, 1);",)),
        (
            "result_seeded_mixed_false",
            "return state.get(true);",
            ("return 0;", "return undefined;"),
        ),
        ("result_seeded_mixed_empty_key", "state.set(false, false);", ("state.set('', false);",)),
        (
            "result_seeded_mixed_string_saved",
            "return saved;",
            ("return state.get('seed');", "return false;"),
        ),
        (
            "saved_read_write",
            "state.set(false, saved);",
            ("state.has(false);", "state.set(false, true);"),
        ),
        ("saved_read_write", "state.delete(false);", ("state.has(false);",)),
        (
            "saved_read_write_false",
            "state.set('', saved);",
            ("state.has('');", "state.set('', state.get(false));"),
        ),
        (
            "saved_read_write_number",
            "state.set(false, saved);",
            ("state.has(false);", "state.set(false, state.get(1));"),
        ),
        ("saved_read_write_repeated", "state.set(false, saved);", ("state.has(false);",)),
        ("saved_read_write_string_saved", "state.set(false, saved);", ("state.has(false);",)),
        ("saved_read_write_string_saved", "return result;", ("return state.get(false);",)),
        ("saved_read_write_string_saved", "state.delete(false);", ("state.has(false);",)),
        (
            "saved_read_write_wrong_tag",
            "state.set(false, true); const result",
            ("state.set(false, saved); const result",),
        ),
        ("saved_read_write_overwritten", "state.set(false, true); const result", ("const result",)),
        ("saved_join", "flag ? state.get('other') : state.get('')", ("state.get('')",)),
        (
            "saved_join_distinct",
            "flag ? state.get('other') : state.get('')",
            ("state.get('')", "state.get('other')"),
        ),
        (
            "saved_join_bool",
            "flag ? state.get('other') : state.get('')",
            ("state.get('')", "state.get('other')"),
        ),
        (
            "saved_join_bool",
            "state.set('temp', saved);",
            ("state.has('temp');", "state.set('temp', state.get(''));"),
        ),
        (
            "saved_join_number",
            "flag ? state.get(1) : state.get(0)",
            ("state.get(0)", "state.get(1)"),
        ),
        (
            "saved_join_number",
            "state.set(false, saved);",
            ("state.has(false);", "state.set(false, state.get(0));"),
        ),
        ("saved_join_string_saved", "state.set(false, saved);", ("state.has(false);",)),
        ("saved_join_string_saved", "return result;", ("return state.get(false);",)),
        ("saved_join_string_saved", "state.delete(false);", ("state.has(false);",)),
        ("saved_join_string_saved", "state.delete('');", ("state.has('');",)),
        ("guarded_saved_read", "if (flag) { state.delete('other'); }", ("state.has('other');",)),
        (
            "guarded_saved_read",
            "state.has('other') ? state.get('other') : state.get('')",
            ("state.get('')",),
        ),
        ("guarded_saved_read", "state.delete(false);", ("state.has(false);",)),
        ("guarded_saved_bool", "if (flag) { state.delete('other'); }", ("state.has('other');",)),
        (
            "guarded_saved_bool",
            "state.has('other') ? state.get('other') : state.get('')",
            ("state.get('')",),
        ),
        (
            "guarded_saved_bool",
            "state.set('temp', saved);",
            ("state.has('temp');", "state.set('temp', state.get(''));"),
        ),
        ("guarded_saved_number", "if (flag) { state.delete(1); }", ("state.has(1);",)),
        ("guarded_saved_number", "state.has(1) ? state.get(1) : state.get(0)", ("state.get(0)",)),
        (
            "guarded_saved_number",
            "state.set(false, saved);",
            ("state.has(false);", "state.set(false, state.get(0));"),
        ),
        ("guarded_saved_string_saved", "state.set(false, saved);", ("state.has(false);",)),
        ("guarded_saved_string_saved", "return result;", ("return state.get(false);",)),
        ("guarded_saved_string_saved", "state.delete(false);", ("state.has(false);",)),
        ("guarded_saved_string_saved", "state.delete('');", ("state.has('');",)),
        (
            "guarded_saved_string_saved",
            "state.set('other', false); state.delete('other');",
            ("state.set('other', false); state.has('other');",),
        ),
        ("shortcircuit_same_tag", "if (flag) { state.delete('other'); }", ("state.has('other');",)),
        ("shortcircuit_same_tag", "state.delete(false);", ("state.has(false);",)),
        (
            "shortcircuit_distinct",
            "(state.has('other') && state.get('other')) || state.get('')",
            ("state.get('')", "'future'"),
        ),
        ("shortcircuit_distinct", "state.set(false, saved);", ("state.has(false);",)),
        (
            "shortcircuit_empty_string",
            "(state.has('other') && state.get('other')) || state.get('')",
            ("state.has('other') ? state.get('other') : state.get('')", "state.get('other')"),
        ),
        (
            "shortcircuit_bool",
            "(state.has('other') && state.get('other')) || state.get('')",
            ("state.get('')", "true"),
        ),
        (
            "shortcircuit_bool",
            "state.set('temp', saved);",
            ("state.has('temp');", "state.set('temp', state.get(''));"),
        ),
        (
            "shortcircuit_false",
            "(state.has('other') && state.get('other')) || state.get('')",
            (
                "state.has('other') ? state.get('other') : state.get('')",
                "state.has('other') && state.get('other')",
            ),
        ),
        (
            "shortcircuit_number",
            "(state.has(1) && state.get(1)) || state.get(0)",
            ("state.get(0)", "3"),
        ),
        (
            "shortcircuit_number",
            "state.set(false, saved);",
            ("state.has(false);", "state.set(false, state.get(0));"),
        ),
        (
            "shortcircuit_zero",
            "(state.has(1) && state.get(1)) || state.get(0)",
            ("state.has(1) ? state.get(1) : state.get(0)", "state.get(1)"),
        ),
        ("shortcircuit_string_saved", "state.set(false, saved);", ("state.has(false);",)),
        ("shortcircuit_string_saved", "return result;", ("return state.get(false);",)),
        ("shortcircuit_string_saved", "state.delete(false);", ("state.has(false);",)),
        ("shortcircuit_string_saved", "state.delete('');", ("state.has('');",)),
        (
            "shortcircuit_string_saved",
            "state.set('other', false); state.delete('other');",
            ("state.set('other', false); state.has('other');",),
        ),
        ("nullable_key_identity", "state.set(key, true);", ("state.set(key || 'missing', true);",)),
        ("nullable_key_identity", "host.slot.set(void 0);", ("host.slot.set(null);",)),
        ("nullable_key_identity", "host.slot.set('');", ("host.slot.set(null);",)),
        ("nullable_key_identity", "return result || null;", ("return result || (void 0);",)),
        ("nullable_key_second_use", "state.set(key, true);", ("state.has(key);",)),
        ("nullable_key_mixed", "state.delete(false);", ("state.has(false);",)),
        ("nullable_original_key", "return result || null;", ("return result;",)),
        ("nullable_second_key_use", "state.set(key, true);", ("state.has(key);",)),
        ("nullable_key_string_saved", "state.delete('seed');", ("state.has('seed');",)),
        ("nullable_payload_saved", "state.delete(key);", ("state.has(key);",)),
        ("nullable_payload_deleted", "state.delete(key);", ("state.has(key);",)),
        ("nullable_payload_mixed_readback", "state.delete('extra');", ("state.has('extra');",)),
        ("nullable_payload_mixed_identity", "state.delete('extra');", ("state.has('extra');",)),
        ("nullable_mixed_payload_readback", "state.delete(false);", ("state.has(false);",)),
        ("nullable_payload_mixed_saved", "state.delete(key);", ("state.has(key);",)),
        (
            "nullable_host_result",
            "return state.get(key);",
            ("return null;", "return void 0;", "return '';", "return true;"),
        ),
        ("nullable_host_result_identity", "host.slot.set(void 0);", ("host.slot.set(null);",)),
        ("nullable_host_result_identity", "host.slot.set('');", ("host.slot.set(null);",)),
        (
            "nullable_host_result_conditional",
            "if (key) { state.set(key, 'selected'); }",
            ("state.has(key);",),
        ),
        ("nullable_host_result_saved", "state.delete('seed');", ("state.has('seed');",)),
        (
            "nullable_nested_result_same",
            "return state.get(key);",
            ("return null;", "return void 0;", "return '';", "return true;"),
        ),
        ("nullable_nested_result_identity", "host.slot.set(void 0);", ("host.slot.set(null);",)),
        ("nullable_nested_result_identity", "host.slot.set('');", ("host.slot.set(null);",)),
        (
            "nullable_nested_result_identity",
            "host.slot.set('later');",
            ("host.slot.set('future');",),
        ),
        ("nullable_nested_result_saved", "state.delete(key);", ("state.has(key);",)),
    ):
        live_source, _, live_value = positives[name]
        if live_source.count(old) != 1:
            raise RuntimeError(f"{name}: lost the payload observation")
        for index, replacement in enumerate(replacements):
            blind = args.work / f"{name}-payload-blinded-{index}.js"
            blind.write_text(live_source.replace(old, replacement))
            if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == f"trace={live_value}\n":
                raise RuntimeError(f"{name}: payload witness cannot distinguish {replacement}")
    for name, (source, _, _) in {
        **nullable_result_sources(),
        **nullable_key_sources(),
        **nullable_payload_sources(),
        **nullable_host_result_sources(),
        **nullable_nested_result_sources(),
    }.items():
        identity = args.work / f"{name}-identity.js"
        identity.write_text(nullable_observer_source(source, name))
        observations = len(NULLABLE_OBSERVATIONS[name]) + len(
            NULLABLE_PAYLOAD_READBACKS.get(name, ())
        )
        expected = f"trace={(1 << observations) - 1}\n"
        if (
            host.run([node, "-e", boundary.NODE, str(identity)]).stdout != expected
            or host.run([str(reference), str(identity)]).stdout != expected
        ):
            raise RuntimeError(f"{name}: Node/interpreter String/null/undefined identity mismatch")
        original_return = (
            "return result ? result : null;"
            if name == "nullable_ternary"
            else (
                "return result || (void 0);"
                if name == "nullable_undefined"
                else (
                    "return result || (nullish ? null : (void 0));"
                    if name == "nullable_threeway"
                    else "return result || null;"
                )
            )
        )
        replacements = ("return result;", "return null;", "return undefined;")
        if name == "nullable_empty":
            replacements = ("return result;", "return undefined;")
        if name == "nullable_string_saved":
            replacements += ("return state.get(false) || null;",)
        if name == "nullable_key_string_saved":
            replacements += ("return state.get('seed') || null;",)
        if name in {
            "nullable_payload_saved",
            "nullable_payload_mixed_saved",
            "nullable_host_result_saved",
            "nullable_nested_result_saved",
        }:
            replacements += ("return state.get('seed') || null;",)
        for index, replacement in enumerate(replacements):
            if source.count(original_return) != 1:
                raise RuntimeError(f"{name}: lost the nullable return observation")
            blind = args.work / f"{name}-identity-blinded-{index}.js"
            blind.write_text(
                nullable_observer_source(source.replace(original_return, replacement), name)
            )
            if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == expected:
                raise RuntimeError(f"{name}: nullable identity cannot distinguish {replacement}")
    for name, old, replacements in (
        (
            "nullable_payload_identity",
            "state.set(key, key);",
            ("state.set(key, null);", "state.set(key, void 0);", "state.set(key, '');"),
        ),
        (
            "nullable_payload_saved",
            "return saved;",
            ("return state.get(key);", "return 'overwritten';"),
        ),
        ("nullable_payload_deleted", "state.delete(key);", ("state.has(key);",)),
        (
            "nullable_payload_mixed_readback",
            "state.set(key, key);",
            ("state.set(key, null);", "state.set(key, void 0);", "state.set(key, '');"),
        ),
        (
            "nullable_mixed_payload_readback",
            "state.set(key, key);",
            ("state.set(key, null);", "state.set(key, void 0);", "state.set(key, '');"),
        ),
        (
            "nullable_payload_mixed_identity",
            "state.set(key, key);",
            ("state.set(key, null);", "state.set(key, void 0);", "state.set(key, '');"),
        ),
        (
            "nullable_payload_mixed_saved",
            "return saved;",
            ("return state.get(key);", "return true;"),
        ),
        (
            "nullable_payload_mixed_saved",
            "const saved = state.get(key); state.set(key, true);",
            ("state.set(key, true); const saved = state.get(key);",),
        ),
        (
            "nullable_host_result",
            "return state.get(key);",
            ("return null;", "return void 0;", "return '';", "return true;"),
        ),
        (
            "nullable_host_result_conditional",
            "if (key) { state.set(key, 'selected'); }",
            ("state.has(key);", "state.set(key, 'selected');"),
        ),
        ("nullable_host_result_saved", "return saved;", ("return state.get(key);", "return true;")),
        (
            "nullable_host_result_saved",
            "const saved = state.get(key); state.set(key, true);",
            ("state.set(key, true); const saved = state.get(key);",),
        ),
        (
            "nullable_nested_result",
            "return state.get(key);",
            ("return null;", "return void 0;", "return '';", "return true;"),
        ),
        (
            "nullable_nested_result_saved",
            "return saved;",
            ("return state.get(key);", "return true;"),
        ),
        (
            "nullable_nested_result_saved",
            "const saved = state.get(key); state.set(key, true);",
            ("state.set(key, true); const saved = state.get(key);",),
        ),
    ):
        source = positives[name][0]
        observations = len(NULLABLE_OBSERVATIONS[name]) + len(NULLABLE_PAYLOAD_READBACKS[name])
        expected = f"trace={(1 << observations) - 1}\n"
        if source.count(old) != 1:
            raise RuntimeError(f"{name}: lost the stored nullable payload observation")
        for index, replacement in enumerate(replacements):
            blind = args.work / f"{name}-stored-payload-blinded-{index}.js"
            blind.write_text(nullable_observer_source(source.replace(old, replacement), name))
            if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == expected:
                raise RuntimeError(f"{name}: stored payload cannot distinguish {replacement}")
    saved_source = positives["nullable_host_result_saved"][0]
    saved_source += "\nhost.slot.set(host.slot.get(false)); trace = host.slot.size('anchor');\n"
    deletion = args.work / "nullable-host-result-saved-deletion.js"
    deletion.write_text(saved_source)
    if (
        host.run([node, "-e", boundary.NODE, str(deletion)]).stdout != "trace=1\n"
        or host.run([str(reference), str(deletion)]).stdout != "trace=1\n"
    ):
        raise RuntimeError("nullable host result: saved payload deletion observation changed")
    deletion.write_text(saved_source.replace("state.delete(key);", "state.has(key);"))
    if host.run([node, "-e", boundary.NODE, str(deletion)]).stdout != "trace=2\n":
        raise RuntimeError(
            "nullable host result: saved payload lifetime cannot distinguish deletion"
        )
