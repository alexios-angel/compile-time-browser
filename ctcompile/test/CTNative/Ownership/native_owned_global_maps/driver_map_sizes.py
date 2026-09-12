from .driver_common import (
    CONSTANT_GLOBAL_NODE,
    boundary,
    check_call_preservation,
    contract,
    delete_size_cases,
    delete_size_observer_source,
    forge_leaf_evidence,
    host,
    join_size_cases,
    methods,
    mutation_size_cases,
    one_size_cases,
    one_size_observer_source,
    owned,
    re,
    source_calls,
    shutil,
    subprocess,
    zero_size_cases,
    zero_size_observer_source,
)


def check_zero_size_census(args, ir, name):
    if name not in zero_size_cases():
        return
    row = zero_size_cases()[name]
    raw = (args.work / f"{name}.raw.mlir").read_text()
    if (
        len(boundary.FUNCTION.findall(raw)) != 5
        or len(source_calls(raw)) != row["raw_calls"]
        or len(source_calls(ir.read_text())) != row["prepared_calls"]
    ):
        raise RuntimeError(f"{name}: changed exact zero-size function/call census")


def check_zero_size_observations(args, node, reference):
    cases = zero_size_cases()
    for name, row in cases.items():
        js = args.work / f"{name}-zero-observed.js"
        js.write_text(row["source"])
        expected = f'trace={row["expected_trace"]}\n'
        if host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]']).stdout != expected:
            raise RuntimeError(f"{name}: typed Node zero-size observation changed")
        if reference:
            result = host.run([str(reference), str(js)])
            if (
                result.stdout != expected
                or "(1 number, 0 boolean, 0 string, 0 null, 0 undefined)" not in result.stderr
            ):
                raise RuntimeError(f"{name}: interpreter lost zero-size Number observation")
    for name, old, replacement in (
        ("local_clear_zero_size_key", "state.clear();", "state.has(key);"),
        ("local_clear_zero_size_key", "state.get(zero)", "state.get(state.size)"),
        ("zero_size_equal_key", "state.get(zero)", "state.get(state.size)"),
        ("zero_size_negative_zero", "state.get(zero)", "state.get(state.size)"),
        ("zero_size_repeated_clear", "state.get(zero)", "state.get(state.size)"),
        ("zero_size_saved_growth", "state.get(zero)", "state.get(state.size)"),
        (
            "zero_size_read_before_clear",
            "const zero = state.size; state.clear();",
            "state.clear(); const zero = state.size;",
        ),
        (
            "zero_size_read_after_write",
            "state.set(1, item); const zero = state.size;",
            "const zero = state.size; state.set(1, item);",
        ),
    ):
        row = cases[name]
        assert row["source"].count(old) == 1, name
        js = args.work / f"{name}-zero-blind.js"
        js.write_text(row["source"].replace(old, replacement))
        if (
            host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]']).stdout
            == f'trace={row["expected_trace"]}\n'
        ):
            raise RuntimeError(
                f"{name}: observer cannot distinguish saved/live zero from {replacement}"
            )
    name = "zero_size_saved_lifetime"
    source = cases[name]["source"]
    observed, value = zero_size_observer_source(source)
    js = args.work / f"{name}-future.js"
    js.write_text(observed)
    expected = f"trace={value}\n"
    if (
        host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
        or reference
        and host.run([str(reference), str(js)]).stdout != expected
    ):
        raise RuntimeError(f"{name}: future flags or saved-zero object identity changed")
    for index, (old, replacement) in enumerate(
        (
            ("state.get(zero)", "state.get(state.size)"),
            ("state.set(0, item);", "state.set(2, item);"),
            ("const saved = state.get(zero);", "const saved = void 0;"),
            ("(flag ? 2 : 1)", "(flag ? 1 : 2)"),
            ("state.clear(); state.set(1, item);", "state.has(key); state.set(1, item);"),
        )
    ):
        assert source.count(old) == 1, name
        blind, _ = zero_size_observer_source(source.replace(old, replacement))
        js = args.work / f"{name}-future-blind-{index}.js"
        js.write_text(blind)
        if host.run([node, "-e", boundary.NODE, str(js)]).stdout == expected:
            raise RuntimeError(f"{name}: future observer cannot distinguish {replacement}")


def check_zero_size_refusals(args, positives):
    check_exact_size_refusals(args, positives, zero_size_cases(), check_zero_size_census)


def check_one_size_refusals(args, positives):
    check_exact_size_refusals(args, positives, one_size_cases(), check_one_size_census)


def check_exact_size_refusals(args, positives, cases, census):
    for name, row in cases.items():
        if row["admitted"]:
            continue
        _, ir, count = boundary.prepare(args, name, row["source"])
        census(args, ir, name)
        repair = row["repair"]
        if (
            row["source"].replace(row["removed_text"], row["replacement_text"])
            != positives[repair][0]
        ):
            raise RuntimeError(f"{name}: exact repair no longer restores executed size source")
        _, restored, repaired_count = boundary.prepare(
            args, name + "-restored", positives[repair][0]
        )
        if count != 5 or repaired_count != 5:
            raise RuntimeError(f"{name}: exact size repair changed function count")
        config = contract(args, ir, name)
        repaired_config = contract(args, restored, name + "-restored")
        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            label = name + "-" + mode

            def check_current(input_ir, current, current_config):
                owner = row.get("owner", False)
                if owner:
                    failed = owned.lower(args, input_ir, current, current_config, options=options)
                    text = methods.census(failed, 5, current, admitted=5)
                    if "ctnative.host_owner_proved = true" not in text:
                        raise RuntimeError(f"{current}: optional field output lost ownership")
                    compilers = [
                        shutil.which("g++-13") or shutil.which("g++"),
                        shutil.which("clang++-18") or shutil.which("clang++"),
                    ]
                    owned.standalone(
                        args,
                        failed,
                        current,
                        f'trace={row["expected_trace"]}\n',
                        compilers,
                        shutil.which("nm"),
                    )
                    return failed
                else:
                    failed = methods.refused(
                        args, input_ir, current, current_config, options=options, admitted=0
                    )
                    text = failed.read_text()
                    check_call_preservation(input_ir.read_text(), text, current)
                expected_owner = "ctnative.host_owner_proved = " + str(owner).lower()
                reason = "property call lacks a current source getter proof"
                if (
                    expected_owner not in text
                    or reason not in text
                    or "fingerprint mismatch" in text
                ):
                    raise RuntimeError(
                        f"{current}: unknown size bypassed independent current source proof"
                    )
                return failed

            stale = methods.refused(
                args,
                ir,
                label + "-changed-source",
                repaired_config,
                options=options,
                reason="fingerprint mismatch",
                admitted=0,
            )
            check_call_preservation(ir.read_text(), stale.read_text(), label + "-changed-source")
            check_current(ir, label, config)
            repaired = owned.lower(
                args, restored, label + "-restored", repaired_config, options=options
            )
            if "ctnative.host_owner_proved = true" not in methods.census(
                repaired, 5, label, admitted=5
            ):
                raise RuntimeError(f"{label}: exact size repair lost native ownership")
            for payload in ("bool", "string", "nullable_string"):
                current = label + "-forged-" + payload
                forged = args.work / f"{current}.mlir"
                forged.write_text(forge_leaf_evidence(ir.read_text(), payload))
                stale = methods.refused(
                    args,
                    forged,
                    current + "-stale",
                    config,
                    options=options,
                    reason="fingerprint mismatch",
                    admitted=0,
                )
                check_call_preservation(forged.read_text(), stale.read_text(), current + "-stale")
                fresh = contract(args, forged, current)
                failed = check_current(forged, current + "-fresh", fresh)
                if row.get("owner"):
                    continue  # Successful lowering leaves no source calls for a refusal rerun.
                rerun = methods.refused(
                    args, failed, current + "-rerun", fresh, options=options, admitted=0
                )
                check_call_preservation(failed.read_text(), rerun.read_text(), current + "-rerun")


def check_one_size_census(args, ir, name):
    if name not in one_size_cases():
        return
    row = one_size_cases()[name]
    raw = (args.work / f"{name}.raw.mlir").read_text()
    if (
        len(boundary.FUNCTION.findall(raw)) != 5
        or len(source_calls(raw)) != row["raw_calls"]
        or len(source_calls(ir.read_text())) != row["prepared_calls"]
    ):
        raise RuntimeError(f"{name}: changed exact one-size function/call census")


def check_one_size_observations(args, node, reference):
    cases = one_size_cases()

    def observe(name, source, value):
        js = args.work / f"{name}-one-observed.js"
        js.write_text(source)
        expected = f"trace={value}\n"
        if host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]']).stdout != expected:
            raise RuntimeError(f"{name}: typed Node exact-size observation changed")
        if reference:
            result = host.run([str(reference), str(js)])
            if (
                result.stdout != expected
                or "(1 number, 0 boolean, 0 string, 0 null, 0 undefined)" not in result.stderr
            ):
                raise RuntimeError(f"{name}: interpreter lost exact-size Number observation")
        return expected

    def distinguish(name, source, old, replacement, expected):
        assert source.count(old) == 1, name
        js = args.work / f"{name}-one-blind.js"
        js.write_text(source.replace(old, replacement))
        if host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]']).stdout == expected:
            raise RuntimeError(f"{name}: observer cannot distinguish {replacement}")

    for name, row in cases.items():
        observe(name, row["source"], row["expected_trace"])
    for name, old, replacement in (
        ("zero_size_read_after_write", "state.clear();", "state.has(key);"),
        ("size_one_present_field", "{value: 1}", "{value: 2}"),
        ("size_one_present_field_entry_repair", "{value: 1}", "{value: 2}"),
        ("size_one_present_field_checked_repair", "{value: 1}", "{value: 2}"),
        ("size_one_literal_repair", "const zero = 1;", "const zero = 2;"),
        ("size_one_saved_growth_gap", "state.get(zero)", "state.get(state.size)"),
        ("size_one_saved_two_growth_gap", "state.get(zero)", "state.get(state.size)"),
        ("size_one_joined_equal_false", "flag ? state.size : 1;", "flag ? state.size : 2;"),
    ):
        row = cases[name]
        distinguish(name, row["source"], old, replacement, f'trace={row["expected_trace"]}\n')
    for name, value, old, replacement in (
        (
            "size_one_repeated_key",
            1,
            "state.set(1, item); const zero",
            "state.set(2, item); const zero",
        ),
        ("size_one_complete_two", 2, "state.set(2, item);", "state.set(1, item);"),
        ("size_one_saved_growth", 2, "state.set(2, item);", "state.set(1, item);"),
        (
            "size_one_optional_duplicate_true",
            1,
            "if (flag) { state.set(1, item); }",
            "if (flag) { state.set(2, item); }",
        ),
    ):
        source = cases[name]["source"] + "trace = host.slot.size();\n"
        expected = observe(name + "-cardinality", source, value)
        distinguish(name + "-cardinality", source, old, replacement, expected)

    name = "size_one_saved_lifetime"
    source = cases[name]["source"]
    observed, value = one_size_observer_source(source)
    expected = observe(name + "-future", observed, value)
    for index, (old, replacement) in enumerate(
        (
            ("state.get(zero)", "state.get(state.size)"),
            ("const saved = state.get(zero);", "const saved = void 0;"),
            ("(flag ? 2 : 1)", "(flag ? 1 : 2)"),
            ("state.clear(); state.set(2, item);", "state.has(key); state.set(2, item);"),
            ("const zero = state.size;", "const zero = 2;"),
        )
    ):
        assert source.count(old) == 1, name
        blind, _ = one_size_observer_source(source.replace(old, replacement))
        js = args.work / f"{name}-future-blind-{index}.js"
        js.write_text(blind)
        if host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]']).stdout == expected:
            raise RuntimeError(f"{name}: future observer cannot distinguish {replacement}")

    # The concrete startup happens to be safe in both refusal families. Their
    # future observations expose why startup facts cannot establish an exact
    # cardinality for every invocation or decide equality of distinct formals.
    for name, values in (
        ("startup_empty_before_write", (1, 0, 0)),
        ("size_one_possible_formal_key", (1, 0, 1)),
    ):
        source = cases[name]["source"] + f"""
(function() {{
    const startup = trace;
    const setter = host.slot.set;
    const a = setter(1);
    const b = setter(99);
    trace = (typeof startup === 'number' && startup === {values[0]} ? 1 : 0) |
            (typeof a === 'number' && a === {values[1]} ? 2 : 0) |
            (typeof b === 'number' && b === {values[2]} ? 4 : 0);
}})();
"""
        observe(name + "-future", source, 7)


def check_delete_size_census(args, ir, name):
    if name not in delete_size_cases():
        return
    row = delete_size_cases()[name]
    raw = (args.work / f"{name}.raw.mlir").read_text()
    if (
        len(boundary.FUNCTION.findall(raw)) != 5
        or len(source_calls(raw)) != row["raw_calls"]
        or len(source_calls(ir.read_text())) != row["prepared_calls"]
    ):
        raise RuntimeError(f"{name}: changed exact deletion function/call census")


def check_delete_size_refusals(args, positives):
    check_exact_size_refusals(args, positives, delete_size_cases(), check_delete_size_census)


def check_delete_size_observations(args, node, reference):
    cases = delete_size_cases()
    observations = mutations = 0

    def observe(name, source, value):
        nonlocal observations
        js = args.work / f"{name}-delete-observed.js"
        js.write_text(source)
        expected = f"trace={value}\n"
        if host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]']).stdout != expected:
            raise RuntimeError(f"{name}: typed Node deletion observation changed")
        if reference:
            result = host.run([str(reference), str(js)])
            if (
                result.stdout != expected
                or "(1 number, 0 boolean, 0 string, 0 null, 0 undefined)" not in result.stderr
            ):
                raise RuntimeError(f"{name}: interpreter lost deletion Number observation")
        observations += 1
        return expected

    def distinguish(name, source, old, replacement, expected):
        nonlocal mutations
        assert source.count(old) == 1, (name, old)
        js = args.work / f"{name}-delete-blind-{mutations}.js"
        js.write_text(source.replace(old, replacement))
        result = subprocess.run(
            [node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]'],
            capture_output=True,
            text=True,
            timeout=30,
        )
        if not result.returncode and result.stdout == expected:
            raise RuntimeError(f"{name}: deletion observer cannot distinguish {replacement}")
        mutations += 1

    for name, row in cases.items():
        observe(name, row["source"], row["expected_trace"])
    for name, old, replacement in (
        ("size_deleted_literal_last", "state.delete(1);", "state.has(1);"),
        ("size_deleted_literal_last", "state.get(zero)", "state.get(state.size)"),
        ("size_deleted_literal_last_zero_repair", "const zero = 0;", "const zero = 1;"),
        ("size_deleted_one_of_two", "state.delete(1);", "state.has(1);"),
        ("size_saved_two_before_delete", "state.get(zero)", "state.get(state.size)"),
        ("size_deleted_disjoint", "state.delete(9);", "state.delete(1);"),
        ("size_deleted_zero_present_field", "{value: 1}", "{value: 2}"),
        ("size_deleted_zero_present_field", "state.get(zero)", "state.get(state.size)"),
        ("size_deleted_one_present_field", "{value: 1}", "{value: 2}"),
        ("size_deleted_one_present_field", "state.delete(2);", "state.delete(1);"),
        (
            "size_deleted_both_branches_false",
            "else { state.delete(1); state.has(key); }",
            "else { state.has(key); }",
        ),
        (
            "size_deleted_both_branches_true",
            "if (flag) { state.delete(1); }",
            "if (flag) { state.has(1); }",
        ),
    ):
        row = cases[name]
        distinguish(name, row["source"], old, replacement, f'trace={row["expected_trace"]}\n')

    # Future invocations expose possible aliasing that the startup key seven
    # alone cannot establish. Each branch's startup literal remains irrelevant
    # to the independent complete-body proof.
    for name, values in (
        ("size_deleted_possible_alias", (0, 1, 0)),
        ("size_deleted_possible_remaining_key", (0, 1, 0)),
    ):
        source = cases[name]["source"] + f"""
(function() {{
    const startup = trace;
    const setter = host.slot.set;
    const a = setter(1);
    const b = setter(99);
    trace = (typeof startup === 'number' && startup === {values[0]} ? 1 : 0) |
            (typeof a === 'number' && a === {values[1]} ? 2 : 0) |
            (typeof b === 'number' && b === {values[2]} ? 4 : 0);
}})();
"""
        observe(name + "-future", source, 7)

    name = "size_deleted_saved_lifetime"
    source = cases[name]["source"]
    observed, value = delete_size_observer_source(source)
    expected = observe(name + "-future", observed, value)
    for old, replacement in (
        ("state.get(zero)", "state.get(state.size)"),
        ("const saved = state.get(zero);", "const saved = void 0;"),
        ("(flag ? 2 : 1)", "(flag ? 1 : 2)"),
        ("state.delete(0);", "state.has(0);"),
        ("state.clear(); state.set(2, item);", "state.has(key); state.set(2, item);"),
    ):
        if old == "state.delete(0);":
            # The original trailing clear deliberately makes this deletion
            # unobservable; observe its result before that clear independently.
            witnessed = source.replace(
                "state.delete(0); state.clear();", "const deleted = state.delete(0); state.clear();"
            ).replace("return saved === item ?", "return deleted && saved === item ?")
            check, _ = delete_size_observer_source(witnessed)
            observe(name + "-delete-result", check, value)
            distinguish(
                name + "-delete-result",
                check,
                "const deleted = state.delete(0);",
                "const deleted = state.delete(2);",
                expected,
            )
            continue
        assert source.count(old) == 1, (name, old)
        distinguish(name + "-future", observed, old, replacement, expected)
    return dict(sources=len(cases), observations=observations, mutations=mutations)


def check_join_size_census(args, ir, name):
    if name not in join_size_cases():
        return
    row = join_size_cases()[name]
    raw = (args.work / f"{name}.raw.mlir").read_text()
    prepared = ir.read_text()
    if (
        len(boundary.FUNCTION.findall(raw)) != 5
        or len(source_calls(raw)) != row["raw_calls"]
        or len(source_calls(prepared)) != row["prepared_calls"]
    ):
        raise RuntimeError(f"{name}: changed exact branch-cardinality function/call census")
    if "scf.if" not in prepared or prepared.count("scf.yield") < 2:
        raise RuntimeError(f"{name}: lost either structural branch before the cardinality proof")


def check_join_size_refusals(args, positives):
    check_exact_size_refusals(args, positives, join_size_cases(), check_join_size_census)


def check_join_size_observations(args, node, reference):
    cases = join_size_cases()
    observations = mutations = 0

    def observe(name, source, value):
        nonlocal observations
        js = args.work / f"{name}-join-observed.js"
        js.write_text(source)
        expected = f"trace={value}\n"
        if host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]']).stdout != expected:
            raise RuntimeError(f"{name}: typed Node branch-cardinality observation changed")
        if reference:
            result = host.run([str(reference), str(js)])
            if (
                result.stdout != expected
                or "(1 number, 0 boolean, 0 string, 0 null, 0 undefined)" not in result.stderr
            ):
                raise RuntimeError(
                    f"{name}: interpreter lost branch-cardinality Number observation"
                )
        observations += 1
        return expected

    def distinguish(name, source, old, replacement, expected):
        nonlocal mutations
        assert source.count(old) == 1, (name, old)
        js = args.work / f"{name}-join-blind-{mutations}.js"
        js.write_text(source.replace(old, replacement))
        result = subprocess.run(
            [node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]'],
            capture_output=True,
            text=True,
            timeout=30,
        )
        if not result.returncode and result.stdout == expected:
            raise RuntimeError(f"{name}: join observer cannot distinguish {replacement}")
        mutations += 1

    for name, row in cases.items():
        observe(name, row["source"], row["expected_trace"])
    for flag in ("false", "true"):
        name = "joined_delete_disjoint_" + flag
        row = cases[name]
        for old, replacement in (
            ("state.get(saved)", "state.get(state.size)"),
            ("{value: 1}", "{value: 2}"),
            ("const saved = state.size;", "state.size; const saved = 2;"),
            (
                "state.delete(2);" if flag == "false" else "state.delete(1);",
                "state.has(2);" if flag == "false" else "state.has(1);",
            ),
        ):
            distinguish(name, row["source"], old, replacement, "trace=1\n")
        name = "joined_set_disjoint_" + flag
        row = cases[name]
        key = "9" if flag == "false" else "7"
        distinguish(
            name, row["source"], f"state.set({key}, item);", f"state.has({key});", "trace=1\n"
        )

    # Both future flags distinguish unequal cardinality, missing common keys,
    # and writes which overwrite in one arm but insert in the other. The
    # startup argument is no authority for a method callable again later.
    for name, values in (
        ("joined_delete_disjoint_false", (1, 1)),
        ("joined_set_disjoint_false", (1, 1)),
        ("joined_unequal_false", (1, 2)),
        ("joined_missing_common_key_false", (1, 2)),
        ("joined_mutated_size_false", (1, 2)),
    ):
        source = cases[name]["source"] + f"""
(function() {{
    const setter = host.slot.set;
    const a = setter(99, false);
    const b = setter(-8, true);
    trace = (typeof a === 'number' && a === {values[0]} ? 1 : 0) |
            (typeof b === 'number' && b === {values[1]} ? 2 : 0);
}})();
"""
        expected = observe(name + "-future", source, 3)
        if values[0] != values[1]:
            distinguish(name + "-future", source, "setter(-8, true)", "setter(-8, false)", expected)

    name = "joined_size_saved_lifetime"
    source = cases[name]["source"]
    observed, value = zero_size_observer_source(source)
    expected = observe(name + "-future", observed, value)
    for old, replacement in (
        ("state.get(saved)", "state.get(state.size)"),
        ("const leaf = state.get(saved);", "const leaf = void 0;"),
        ("(flag ? 2 : 1)", "(flag ? 1 : 2)"),
        ("state.clear(); state.set(2, item);", "state.has(key); state.set(2, item);"),
    ):
        distinguish(name + "-future", observed, old, replacement, expected)
    return dict(sources=len(cases), observations=observations, mutations=mutations)


def check_mutation_size_census(args, ir, name):
    if name not in mutation_size_cases():
        return
    row = mutation_size_cases()[name]
    raw = (args.work / f"{name}.raw.mlir").read_text()
    prepared = ir.read_text()
    if (
        len(boundary.FUNCTION.findall(raw)) != 5
        or len(source_calls(raw)) != row["raw_calls"]
        or len(source_calls(prepared)) != row["prepared_calls"]
    ):
        raise RuntimeError(f"{name}: changed the exact after-join mutation function/call census")
    if "scf.if" not in prepared or prepared.count("scf.yield") < 2:
        raise RuntimeError(f"{name}: removed a structural arm before the mutation size proof")


def check_mutation_size_refusals(args, positives):
    check_exact_size_refusals(args, positives, mutation_size_cases(), check_mutation_size_census)


def check_mutation_size_observations(args, node, reference):
    cases = mutation_size_cases()
    observations = mutations = 0

    def observe(name, source, value):
        nonlocal observations
        js = args.work / f"{name}-mutation-observed.js"
        js.write_text(source)
        expected = f"trace={value}\n"
        if host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]']).stdout != expected:
            raise RuntimeError(f"{name}: typed Node mutation-size observation changed")
        if reference:
            result = host.run([str(reference), str(js)])
            if (
                result.stdout != expected
                or "(1 number, 0 boolean, 0 string, 0 null, 0 undefined)" not in result.stderr
            ):
                raise RuntimeError(f"{name}: interpreter lost the mutation-size Number observation")
        observations += 1
        return expected

    def distinguish(name, source, old, replacement, expected):
        nonlocal mutations
        assert source.count(old) == 1, (name, old)
        js = args.work / f"{name}-mutation-blind-{mutations}.js"
        js.write_text(source.replace(old, replacement))
        result = subprocess.run(
            [node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]'],
            capture_output=True,
            text=True,
            timeout=30,
        )
        if not result.returncode and result.stdout == expected:
            raise RuntimeError(f"{name}: mutation observer cannot distinguish {replacement}")
        mutations += 1

    for name, row in cases.items():
        observe(name, row["source"], row["expected_trace"])
    for flag in ("false", "true"):
        name = "joined_disjoint_set_" + flag
        source = cases[name]["source"]
        for old, replacement in (
            ("state.set(3, item); const saved", "state.has(3); const saved"),
            ("const saved = state.size;", "state.size; const saved = 1;"),
            ("{value: 1}", "{value: 2}"),
            ("state.set(2, item); state.set(4, item);", "state.set(6, item); state.set(4, item);"),
        ):
            distinguish(name, source, old, replacement, "trace=1\n")
        name = "joined_absent_delete_" + flag
        distinguish(
            name,
            cases[name]["source"],
            "state.delete(3);",
            "state.delete(1);" if flag == "false" else "state.delete(2);",
            "trace=1\n",
        )
        name = "joined_present_overwrite_" + flag
        distinguish(
            name,
            cases[name]["source"],
            "state.set(3, item); const saved",
            "state.set(5, item); const saved",
            "trace=1\n",
        )
        name = "joined_present_delete_" + flag
        distinguish(name, cases[name]["source"], "state.delete(3);", "state.has(3);", "trace=1\n")
        name = "joined_saved_before_set_" + flag
        distinguish(
            name,
            cases[name]["source"],
            "const saved = state.size; state.set(3, item);",
            "state.set(3, item); const saved = state.size;",
            "trace=1\n",
        )

    # Future arguments cover both independently proved mutations and cases in
    # which a startup-only distinct key later becomes an existing branch key.
    for name, values in (
        ("joined_disjoint_set_false", (1, 1, 1)),
        ("joined_absent_delete_false", (1, 1, 1)),
        ("joined_present_overwrite_false", (1, 1, 1)),
        ("joined_present_delete_false", (1, 1, 1)),
        ("joined_known_mutation_chain", (1, 1, 1)),
        ("joined_possible_delete_false", (2, 2, 1)),
        ("joined_formal_insert_false", (1, 2, 1)),
        ("joined_formal_delete_false", (1, 2, 1)),
    ):
        source = cases[name]["source"] + f"""
(function() {{
    const setter = host.slot.set;
    const a = setter(99, false);
    const b = setter(1, false);
    const c = setter(1, true);
    trace = (typeof a === 'number' && a === {values[0]} ? 1 : 0) |
            (typeof b === 'number' && b === {values[1]} ? 2 : 0) |
            (typeof c === 'number' && c === {values[2]} ? 4 : 0);
}})();
"""
        expected = observe(name + "-future", source, 7)
        if values[0] != values[1]:
            distinguish(name + "-future", source, "setter(1, false)", "setter(99, false)", expected)
        if values[1] != values[2]:
            distinguish(name + "-future", source, "setter(1, true)", "setter(1, false)", expected)

    name = "joined_mutation_saved_lifetime"
    observed, value = zero_size_observer_source(cases[name]["source"])
    expected = observe(name + "-future", observed, value)
    for old, replacement in (
        ("state.get(saved)", "state.get(state.size)"),
        ("const leaf = state.get(saved);", "const leaf = void 0;"),
        ("(flag ? 2 : 1)", "(flag ? 1 : 2)"),
        (
            "state.set(3, item); const saved = state.size;",
            "const saved = state.size; state.set(3, item);",
        ),
        ("state.set(4, item);", "state.has(4);"),
    ):
        distinguish(name + "-future", observed, old, replacement, expected)
    return dict(sources=len(cases), observations=observations, mutations=mutations)
