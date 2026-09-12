from .driver_common import (
    CONSTANT_GLOBAL_NODE,
    boundary,
    check_budgets,
    check_call_preservation,
    contract,
    forge_leaf_evidence,
    host,
    json,
    methods,
    object_argument_cases,
    object_argument_observer_source,
    object_argument_sources,
    owned,
    re,
    retained_key_observer_source,
    source_calls,
    subprocess,
)
from .driver_fields import (
    string_field_method_graph,
)
from .driver_object_maps import (
    check_leaf_object_forgeries,
)
from .harness_objects import object_payload_observer_source


def check_object_argument_observations(args, node, reference):
    cases = object_argument_cases()

    def observe(name, source, value, undefined_globals=()):
        js = args.work / f"{name}-object-observed.js"
        js.write_text(source)
        names = sorted(["trace", *undefined_globals])
        expected = "".join(f'{key}={value if key == "trace" else "undefined"}\n' for key in names)
        if (
            host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), json.dumps(names)]).stdout
            != expected
        ):
            raise RuntimeError(f"{name}: typed Node object-key observation changed")
        if reference:
            result = host.run([str(reference), str(js)])
            if (
                result.stdout != expected
                or f"(1 number, 0 boolean, 0 string, 0 null, {len(undefined_globals)} undefined)"
                not in result.stderr
            ):
                raise RuntimeError(
                    f"{name}: interpreter object-key observations changed\n"
                    f"{result.stdout}\n{result.stderr}"
                )
        return expected

    for name, row in cases.items():
        undefined_globals = row.get("undefined_globals", ())
        assert not undefined_globals or not row["admitted"]
        observe(name, row["source"], row["expected_trace"], undefined_globals)
    named, value = object_argument_observer_source(
        cases["object_argument_global"]["source"], global_key=True
    )
    expected_named = observe("object_argument_global_future", named, value)
    named_mutations = (
        ("const first = key,", "const first = {},"),
        ("key = {};\n    const distinct", "key = first;\n    const distinct"),
    )
    for index, (old, replacement) in enumerate(named_mutations):
        assert named.count(old) == 1, old
        js = args.work / f"global-key-blinded-{index}.js"
        js.write_text(named.replace(old, replacement))
        result = subprocess.run(
            [node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]'],
            capture_output=True,
            text=True,
            timeout=30,
        )
        if not result.returncode and result.stdout == expected_named:
            raise RuntimeError(f"global key observer cannot distinguish {replacement}")
    alias_mutations = 0
    for name, observer, mutations in (
        (
            "object_argument_global_alias",
            object_argument_observer_source,
            (("var alias = key;", "var alias = {};"),),
        ),
        (
            "object_argument_global_alias_chain",
            object_argument_observer_source,
            (("var copy = alias;", "var copy = {};"),),
        ),
        (
            "object_argument_siblings_global",
            retained_key_observer_source,
            (("alias = key,", "alias = {},"), ("other = {};", "other = key;")),
        ),
        (
            "object_argument_siblings_global_chain",
            retained_key_observer_source,
            (
                ("copy = alias,", "copy = {},"),
                ("tail = copy,", "tail = {},"),
                ("branch = alias,", "branch = {},"),
                ("other = {};", "other = key;"),
            ),
        ),
    ):
        aliased, value = observer(
            cases[name]["source"],
            global_alias=True,
            global_chain=cases[name].get("global_chain", ()),
        )
        expected_alias = observe(name + "_future", aliased, value)
        for index, (old, replacement) in enumerate(mutations):
            assert aliased.count(old) == 1, old
            js = args.work / f"{name}-blinded-{index}.js"
            js.write_text(aliased.replace(old, replacement))
            result = subprocess.run(
                [node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]'],
                capture_output=True,
                text=True,
                timeout=30,
            )
            if not result.returncode and result.stdout == expected_alias:
                raise RuntimeError(f"{name}: alias observer cannot distinguish {replacement}")
            alias_mutations += 1
    source, value = object_argument_observer_source(cases["object_argument_exact"]["source"])
    expected = observe("object_argument_future", source, value)
    mutations = (
        ("return t.has(e) ? 1 : 0;", "t.has(e); return 0;"),
        ("return t.has(e) ? 1 : 0;", "t.has(e); return 1;"),
        ("return t.has(e) ? 1 : 0;", "return t.has({}) ? 1 : 0;"),
        ("alias = first", "alias = {}"),
        ("captured.delete(first);", "captured.has(first);"),
        ("captured.clear();", "captured.has(other);"),
        ("get({}) !== 0", "get(key) !== 0"),
    )
    for index, (old, replacement) in enumerate(mutations):
        assert source.count(old) == 1, old
        js = args.work / f"object-argument-blinded-{index}.js"
        js.write_text(source.replace(old, replacement))
        result = subprocess.run(
            [node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]'],
            capture_output=True,
            text=True,
            timeout=30,
        )
        if not result.returncode and result.stdout == expected:
            raise RuntimeError(f"object argument observer cannot distinguish {replacement}")
    retained, value = retained_key_observer_source(cases["object_argument_siblings"]["source"])
    expected = observe("retained_key_future", retained, value)
    retained_mutations = (
        ("t.set(e, value);", "t.has(e);"),
        ("t.get(e) : 0;", "0 : 0;"),
        ("t.delete(e)", "t.has(e)"),
        ("clear() { t.clear();", "clear() { t.size;"),
        ("alias = first", "alias = {}"),
        ("get(other) === 0", "get(first) === 0"),
    )
    for index, (old, replacement) in enumerate(retained_mutations):
        assert retained.count(old) == 1, old
        js = args.work / f"retained-key-blinded-{index}.js"
        js.write_text(retained.replace(old, replacement))
        result = subprocess.run(
            [node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]'],
            capture_output=True,
            text=True,
            timeout=30,
        )
        if not result.returncode and result.stdout == expected:
            raise RuntimeError(f"retained-key observer cannot distinguish {replacement}")
    payload_observations = payload_mutations = 0
    for name, row in cases.items():
        if not row.get("object_payload"):
            continue
        payload, value = object_payload_observer_source(row["source"], name)
        expected_payload = observe(name + "_future", payload, value)
        payload_observations += 1
        key = "1" if row.get("payload_only") else "e"
        old = f"t.set({key}, e);"
        assert payload.count(old) == 1
        for index, replacement in enumerate(
            (f"t.set({key}, {{}});", f"t.set({key}, 1);", f"t.has({key});", "t.set(2, e);")
        ):
            js = args.work / f"{name}-payload-blinded-{index}.js"
            js.write_text(payload.replace(old, replacement))
            result = subprocess.run(
                [node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]'],
                capture_output=True,
                text=True,
                timeout=30,
            )
            if not result.returncode and result.stdout == expected_payload:
                raise RuntimeError(f"{name}: payload observer cannot distinguish {replacement}")
            payload_mutations += 1
    source = cases["parameter_object"]["source"]
    old = "state.set(key, 1);"
    assert source.count(old) == 1
    js = args.work / "parameter-object-blinded.js"
    js.write_text(source.replace(old, "state.has(key);"))
    result = subprocess.run(
        [node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]'],
        capture_output=True,
        text=True,
        timeout=30,
    )
    if not result.returncode and result.stdout == "trace=1\n":
        raise RuntimeError(
            "historical object setter observation cannot distinguish a skipped write"
        )
    return dict(
        sources=len(cases),
        observations=len(cases) + 7 + payload_observations,
        mutations=len(mutations)
        + len(retained_mutations)
        + len(named_mutations)
        + alias_mutations
        + payload_mutations
        + 1,
    )


def check_object_argument_census(args, ir, name):
    row = object_argument_cases().get(name)
    if row is None:
        return
    raw = (args.work / f"{name}.raw.mlir").read_text()
    prepared = ir.read_text()
    if (
        len(boundary.FUNCTION.findall(raw)) != row["functions"]
        or len(source_calls(raw)) != row["raw_calls"]
        or len(source_calls(prepared)) != row["prepared_calls"]
    ):
        raise RuntimeError(f"{name}: changed exact object actual function/call census")
    for operation in (
        "create_object",
        "construct",
        "get_property",
        "set_property",
        "load_global",
        "store_global",
    ):
        if raw.count(operation) != prepared.count(operation):
            raise RuntimeError(f"{name}: preparation changed the live {operation} census")
    if raw.count("cf.cond_br") != prepared.count("scf.if"):
        raise RuntimeError(f"{name}: preparation lost an object-key result branch")
    if row.get("global_alias"):
        bindings = "|".join(("key", "alias", *row.get("global_chain", ()), "other"))
        initializers = re.findall(r"\b(" + bindings + r") = (\{\}|\w+)(?=[,;])", row["source"])
        predecessors = [value for _, value in initializers if value != "{}"]
        arguments = re.findall(r"host\.slot\.\w+\((" + bindings + r")[,)]", row["source"])
        for text in (raw, prepared):
            entry = text.split("\n  }", 1)[0]
            created = re.findall(r"(%[-\w.$]+) = ctjs\.create_object", entry)[1:]
            loads = re.findall(r'(%[-\w.$]+) = ctjs\.load_global "(' + bindings + r')"', entry)
            stores = re.findall(r'ctjs\.store_global "(' + bindings + r')", (%[-\w.$]+)', entry)
            actuals = re.findall(
                r"ctjs\.call %[-\w.$]+\(%[-\w.$]+, (%[-\w.$]+)(?:, %[-\w.$]+)?\)", entry
            )
            allocations, reads = iter(created), iter(value for value, _ in loads)
            expected_stores = [
                (binding, next(allocations) if value == "{}" else next(reads))
                for binding, value in initializers
            ]
            # The raw factory invocation has a closure argument before the key calls.
            actuals = [value for value in actuals if value in dict(loads)]
            if (
                stores != expected_stores
                or [binding for _, binding in loads] != [*predecessors, *arguments]
                or actuals != [value for value, _ in loads[len(predecessors) :]]
            ):
                raise RuntimeError(
                    f"{name}: changed the exact global alias initializer or actual edges"
                )


def check_object_argument_preparation(text, original, name):
    row = object_argument_cases()[name]
    if len(source_calls(text)) != row["prepared_calls"] or string_field_method_graph(
        original, "fn$3", False
    ) != string_field_method_graph(text, "fn$3", True):
        raise RuntimeError(f"{name}: preparation changed the actual object/Map body")
    calls = re.findall(
        r"^\s*(%[-\w.$]+) = ctjs\.call_direct @fn\$3\(([^\n]+)\) "
        r"\{ctnative\.stored_call = 1 : i32\}",
        text,
        re.M,
    )
    entry = text.split("\n  }", 1)[0]
    original_entry = original.split("\n  }", 1)[0]
    original_calls = re.findall(
        r"^\s*(%[-\w.$]+) = ctjs\.call %[-\w.$]+\(" r"%[-\w.$]+, (%[-\w.$]+)\)",
        original_entry,
        re.M,
    )
    receivers = dict(re.findall(r"(%[-\w.$]+) = ctjs\.get_property (%[-\w.$]+)\[", entry))
    captures = dict(re.findall(r"(%[-\w.$]+) = ctjs\.load_upvalue (%[-\w.$]+)\[0\]", entry))
    if (
        not calls
        or len(calls) != len(original_calls)
        or len(calls) != row["source"].count("host.slot.get(")
    ):
        raise RuntimeError(f"{name}: preparation changed the published call census")
    arguments = []
    for _, operands in calls:
        actuals = operands.split(", ")
        if (
            len(actuals) != 5
            or receivers.get(actuals[2]) != actuals[0]
            or captures.get(actuals[3]) != actuals[2]
        ):
            raise RuntimeError(f"{name}: lost the current receiver or capture")
        arguments.append(actuals[-1])

    def origins(body, actuals):
        objects = re.findall(r"(%[-\w.$]+) = ctjs\.create_object", body)
        loads = re.findall(r'(%[-\w.$]+) = ctjs\.load_global "([^"\n]+)"', body)
        values = {value: ("object", index) for index, value in enumerate(objects)}
        values.update(
            {value: ("global", index, binding) for index, (value, binding) in enumerate(loads)}
        )
        values.update(
            {
                value: ("constant", literal.strip())
                for value, literal in re.findall(r"(%[-\w.$]+) = ctjs\.constant ([^\n{]+)", body)
            }
        )
        if any(value not in values for value in actuals):
            raise RuntimeError(f"{name}: unsupported current caller argument origin")
        return [values[value] for value in actuals]

    if origins(entry, arguments) != origins(
        original_entry, [value for _, value in original_calls]
    ) or re.findall(r'ctjs\.store_global "trace", (%[-\w.$]+)', entry) != [calls[-1][0]]:
        raise RuntimeError(f"{name}: changed source actual identities, categories or final result")


def check_object_argument_controls(args, saved):
    check_leaf_object_forgeries(args, saved, object_argument_sources())
    for name in object_argument_sources():
        _, config, output = saved[name]
        rerun = owned.lower(args, output, name + "-rerun", config, cleanup=False)
        functions = object_argument_cases()[name]["functions"]
        text = methods.census(rerun, functions, name, admitted=functions)
        if "ctnative.host_owner_proved = false" not in text or "fingerprint mismatch" not in text:
            raise RuntimeError(f"{name}: emitted object signature reused original source authority")
    ir, config, _ = saved["object_argument_exact"]
    check_budgets(args, ir, config, "object_argument_exact", functions=4)
    ir, config, _ = saved["object_argument_key_write"]
    check_budgets(args, ir, config, "object_argument_key_write", functions=4)
    ir, config, _ = saved["object_argument_global"]
    check_budgets(args, ir, config, "object_argument_global", functions=4)
    ir, config, _ = saved["object_argument_global_alias"]
    check_budgets(args, ir, config, "object_argument_global_alias", functions=4)
    ir, config, _ = saved["object_argument_siblings_global"]
    check_budgets(args, ir, config, "object_argument_siblings_global", functions=7)
    for name in ("object_argument_global_alias_chain", "object_argument_siblings_global_chain"):
        ir, config, _ = saved[name]
        check_budgets(args, ir, config, name, functions=object_argument_cases()[name]["functions"])
    for name, row in object_argument_cases().items():
        if row.get("object_payload"):
            ir, config, _ = saved[name]
            check_budgets(args, ir, config, name, functions=row["functions"])
    for case, bindings in (
        ("object_argument_global", ("key",)),
        ("object_argument_global_object_payload", ("key",)),
        ("object_argument_scalar_key_payload", ("key",)),
        ("object_argument_global_alias", ("key", "alias")),
        ("object_argument_global_alias_chain", ("key", "alias", "copy")),
        ("object_argument_siblings_global", ("key", "alias", "other")),
        (
            "object_argument_siblings_global_chain",
            ("key", "alias", "copy", "tail", "branch", "other"),
        ),
    ):
        ir, config, _ = saved[case]
        for binding in bindings:
            observation = json.loads(config.read_text())
            observation["observations"] = ["trace", binding]
            invalid = args.work / f"{case}-{binding}-object-observation.contract.json"
            invalid.write_text(json.dumps(observation, indent=2) + "\n")
            for mode, options in (("default", ""), ("disabled", "optimize=false")):
                name = f"{case}-{binding}-object-observation-{mode}"
                failed = methods.refused(
                    args,
                    ir,
                    name,
                    invalid,
                    options=options,
                    admitted=0,
                    reason="object key global cannot be a scalar observation",
                )
                check_call_preservation(ir.read_text(), failed.read_text(), name)
    ir, config, _ = saved["object_argument_siblings_named"]
    check_budgets(args, ir, config, "object_argument_siblings_named", functions=7)
    ir, config, _ = saved["parameter_object"]
    check_budgets(args, ir, config, "parameter_object", functions=5)
    for name, row in object_argument_cases().items():
        if row["admitted"]:
            continue
        _, ir, count = boundary.prepare(args, name, row["source"])
        check_object_argument_census(args, ir, name)
        if count != row["functions"]:
            raise RuntimeError(f"{name}: changed the exact source function count")
        config = contract(args, ir, name)
        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            label = name + "-" + mode

            def reject(input_ir, current, current_config):
                output = owned.lower(
                    args, input_ir, current, current_config, options=options, cleanup=False
                )
                text = methods.census(output, row["functions"], current, admitted=0)
                if "ctnative.host_owner_proved = " + str(row["owner"]).lower() not in text:
                    raise RuntimeError(f"{current}: changed independent object argument ownership")
                if row["owner"]:
                    check_object_argument_preparation(text, input_ir.read_text(), name)
                else:
                    check_call_preservation(input_ir.read_text(), text, current)
                return output

            reject(ir, label, config)
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
            check_call_preservation(forged.read_text(), stale.read_text(), label + "-stale")
            fresh = contract(args, forged, label + "-forged")
            failed = reject(forged, label + "-fresh", fresh)
            rerun = methods.refused(
                args, failed, label + "-rerun", fresh, options=options, admitted=0
            )
            check_call_preservation(failed.read_text(), rerun.read_text(), label + "-rerun")
