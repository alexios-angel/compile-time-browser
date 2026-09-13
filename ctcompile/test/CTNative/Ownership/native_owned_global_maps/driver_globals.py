from .driver_common import (
    CONSTANT_GLOBAL_CARRIERS,
    CONSTANT_GLOBAL_NODE,
    CONSTANT_GLOBAL_UNOWNED,
    NUMERIC_ENTRY_SAVED_GLOBALS,
    SCALAR_GLOBAL_CARRIERS,
    StringValue,
    boundary,
    check_call_preservation,
    constant_global_cases,
    constant_global_sources,
    contract,
    forge_leaf_evidence,
    host,
    json,
    leaf_absence_cases,
    leaf_clear_cases,
    methods,
    normalized_scalar_output,
    numeric_entry_cases,
    numeric_reference_output,
    owned,
    re,
    scalar_global_cases,
    scalar_global_output,
    scalar_global_sources,
    source_calls,
    shutil,
    struct,
    subprocess,
)


def check_numeric_global_preparation(text, original, name):
    entry = text.split("\n  }", 1)[0]
    calls = re.findall(
        r"^\s*(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\) "
        r"\{ctnative\.stored_call = 1 : i32\}",
        entry,
        re.M,
    )
    actuals = [arguments.split(", ") for _, _, arguments in calls]
    if (
        len(source_calls(text)) != 8
        or len(source_calls(original)) != 8
        or [(target, len(arguments)) for (_, target, _), arguments in zip(calls, actuals)]
        != [("fn$3", 4), ("fn$4", 5), ("fn$4", 5), ("fn$4", 5)]
    ):
        raise RuntimeError(f"{name}: saved-global refusal changed the eight evaluated calls")
    receivers = dict(re.findall(r"(%[-\w.$]+) = ctjs\.get_property (%[-\w.$]+)\[", entry))
    captures = dict(re.findall(r"(%[-\w.$]+) = ctjs\.load_upvalue (%[-\w.$]+)\[0\]", entry))
    for arguments in actuals:
        if (
            receivers.get(arguments[2]) != arguments[0]
            or captures.get(arguments[3]) != arguments[2]
        ):
            raise RuntimeError(
                f"{name}: saved-global refusal changed receiver/callee/capture provenance"
            )
    snapshot = "saved_snapshot" in name
    names = ("first", "second") if snapshot else ("first", "second", "third")
    for index, binding in enumerate(names, 1):
        if re.findall(rf'ctjs\.store_global "{binding}", (%[-\w.$]+)', entry) != [calls[index][0]]:
            raise RuntimeError(f"{name}: changed the live {binding} result store")
    values, binaries = {}, []
    for line in entry.splitlines():
        if match := re.search(r'(%[-\w.$]+) = ctjs\.load_global "([^\"]+)"', line):
            values[match[1]] = "global:" + match[2]
        elif match := re.search(r"(%[-\w.$]+) = ctjs\.constant #ctjs\.number<(\d+)>", line):
            values[match[1]] = struct.unpack("d", struct.pack("Q", int(match[2])))[0]
        elif match := re.search(r"(%[-\w.$]+) = ctjs\.binary (\w+) (%[-\w.$]+), (%[-\w.$]+)", line):
            binaries.append((match[2], values.get(match[3]), values.get(match[4])))
            values[match[1]] = "binary:" + str(len(binaries) - 1)
    expected = (
        [("mul", "global:first", 10.0), ("add", "binary:0", "global:second")]
        if snapshot
        else [("add", "global:first", "global:second"), ("add", "binary:0", "global:third")]
    )
    trace = re.findall(r'ctjs\.store_global "trace", (%[-\w.$]+)', entry)
    if binaries != expected or len(trace) != 1 or values.get(trace[0]) != "binary:1":
        raise RuntimeError(f"{name}: changed saved-global arithmetic operands: {binaries}")
    for operation in ("scf.if", "scf.yield", "ctjs.create_object", "ctjs.construct"):
        if text.count(operation) != original.count(operation):
            raise RuntimeError(f"{name}: saved-global refusal changed live {operation}")


def scalar_string_literal(text, mlir=False):
    """Compare literal bytes despite MLIR hex and C++ octal spellings."""
    if text.startswith('R"('):
        return text[3:-2]
    out = bytearray()
    content = text[1:-1]
    while content:
        if content[0] != "\\":
            out.extend(content[0].encode("utf-8", "surrogatepass"))
            content = content[1:]
            continue
        pattern = r"\\([0-9A-Fa-f]{2})" if mlir else r"\\([0-7]{1,3})"
        match = re.match(pattern, content)
        if match:
            out.append(int(match[1], 16 if mlir else 8))
            content = content[match.end() :]
        else:
            out.extend({"n": b"\n", "t": b"\t", "r": b"\r"}.get(content[1], content[1].encode()))
            content = content[2:]
    return out.decode("utf-8", "surrogatepass")


def scalar_global_graph(text, name):
    """Compare current scalar dataflow before and after callable preparation."""
    entry = text.split("\n  }", 1)[0]
    values, properties, captures = {}, {}, {}
    events, calls, binaries = [], 0, 0
    for line in entry.splitlines():
        if match := re.search(r'(%[-\w.$]+) = ctjs\.load_global "([^\"]+)"', line):
            values[match[1]] = "global:" + match[2]
            if match[2] != "host":
                events.append(("load", match[2]))
        elif match := re.search(r'ctjs\.store_global "([^\"]+)", (%[-\w.$]+)', line):
            if match[1] != "host":
                events.append(("store", match[1], values.get(match[2], "unknown")))
        elif match := re.search(
            r'(%[-\w.$]+) = ctjs\.constant #ctjs\.string<("(?:[^"\\]|\\.)*")>', line
        ):
            values[match[1]] = ("string", scalar_string_literal(match[2], mlir=True))
        elif match := re.search(r"(%[-\w.$]+) = ctjs\.constant #ctjs\.number<(\d+)>", line):
            values[match[1]] = struct.unpack("d", struct.pack("Q", int(match[2])))[0]
        elif match := re.search(
            r"(%[-\w.$]+) = ctjs\.constant #ctjs\.(?:boolean|bool)<(true|false)>", line
        ):
            values[match[1]] = ("boolean", match[2])
        elif match := re.search(
            r"(%[-\w.$]+) = ctjs\.get_property (%[-\w.$]+)\[(%[-\w.$]+)\]", line
        ):
            properties[match[1]] = (match[2], values.get(match[3]))
        elif match := re.search(r"(%[-\w.$]+) = ctjs\.load_upvalue (%[-\w.$]+)\[0\]", line):
            captures[match[1]] = match[2]
        elif match := re.search(r"(%[-\w.$]+) = ctjs\.binary (\w+) (%[-\w.$]+), (%[-\w.$]+)", line):
            events.append(
                (
                    "binary",
                    match[2],
                    values.get(match[3], "unknown"),
                    values.get(match[4], "unknown"),
                )
            )
            values[match[1]] = "binary:" + str(binaries)
            binaries += 1
        else:
            direct = re.search(
                r"(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\).*ctnative\.stored_call",
                line,
            )
            ordinary = re.search(r"(%[-\w.$]+) = ctjs\.call (%[-\w.$]+)\(([^\n)]+)\)", line)
            if direct:
                result, target, arguments = direct.groups()
                actuals = arguments.split(", ")
                receiver, callee = actuals[0], actuals[2]
                if captures.get(actuals[3]) != callee:
                    raise RuntimeError(f"{name}: changed current published callable capture")
                arguments = actuals[4:]
            elif ordinary:
                result, callee, arguments = ordinary.groups()
                actuals = arguments.split(", ")
                receiver, arguments = actuals[0], actuals[1:]
                target = None
            else:
                continue
            method = properties.get(callee)
            if not method or method[1] not in {("string", "size"), ("string", "set")}:
                continue
            table = properties.get(receiver)
            if (
                method[0] != receiver
                or not table
                or table[1] != ("string", "slot")
                or values.get(table[0]) != "global:host"
            ):
                raise RuntimeError(f"{name}: changed current published receiver/callee")
            method = method[1][1]
            if target and target != ("fn$3" if method == "size" else "fn$4"):
                raise RuntimeError(f"{name}: changed current published callee target")
            events.append(
                ("call", method, tuple(values.get(argument, "unknown") for argument in arguments))
            )
            values[result] = "call:" + str(calls)
            calls += 1
    return events


def check_scalar_global_preparation(text, original, name):
    before, after = scalar_global_graph(original, name), scalar_global_graph(text, name)
    if before != after or not before:
        raise RuntimeError(
            f"{name}: preparation changed live scalar stores/loads/calls/arithmetic\n{before}\n{after}"
        )
    for operation in (
        "scf.if",
        "scf.yield",
        "ctjs.create_object",
        "ctjs.construct",
        "ctjs.store_property",
    ):
        if text.count(operation) != original.count(operation):
            raise RuntimeError(f"{name}: preparation changed live {operation}")


def check_scalar_global_source(ir, name):
    if (
        name not in NUMERIC_ENTRY_SAVED_GLOBALS
        and name not in scalar_global_cases()
        and name not in constant_global_cases()
    ):
        return
    graph = scalar_global_graph(ir.read_text(), name)
    calls = [event for event in graph if event[0] == "call"]
    stores = [event for event in graph if event[0] == "store"]
    loads = [event for event in graph if event[0] == "load"]
    if not calls or calls[0] != ("call", "size", ()):
        raise RuntimeError(f"{name}: lost the first evaluated size observation")
    cases = {**numeric_entry_cases(), **scalar_global_cases(), **constant_global_cases()}
    source = cases[name]["source"]
    if len(calls) != len(re.findall(r"host\.slot\.(?:size|set)\(", source)):
        raise RuntimeError(f"{name}: lost an evaluated published call")
    if name in NUMERIC_ENTRY_SAVED_GLOBALS:
        snapshot = name == "local_numeric_saved_snapshot"
        names = ("first", "second") if snapshot else ("first", "second", "third")
        if [event for event in stores if event[1] != "trace"] != [
            ("store", binding, "call:" + str(index)) for index, binding in enumerate(names, 1)
        ]:
            raise RuntimeError(f"{name}: lost the exact original call-result stores")
        if loads != [("load", binding) for binding in names]:
            raise RuntimeError(f"{name}: lost the exact original scalar loads")
        expected = (
            [
                ("binary", "mul", "global:first", 10.0),
                ("binary", "add", "binary:0", "global:second"),
            ]
            if snapshot
            else [
                ("binary", "add", "global:first", "global:second"),
                ("binary", "add", "binary:0", "global:third"),
            ]
        )
        if [event for event in graph if event[0] == "binary"] != expected:
            raise RuntimeError(f"{name}: lost the original two arithmetic dependencies")
    written = set()
    early = []
    for event in graph:
        if event[0] == "store":
            written.add(event[1])
        elif event[0] == "load" and event[1] not in written:
            early.append(event[1])
    expected_early = {
        "scalar_read_before_write": ["first"],
        "constant_read_before_write": ["fixed"],
        "constant_dynamic_global": ["globalThis"],
        "constant_boolean_read_before_write": ["fixed"],
        "constant_boolean_dynamic_global": ["globalThis"],
        "constant_string_read_before_write": ["fixed"],
    }.get(name, [])
    if early != expected_early:
        raise RuntimeError(f"{name}: changed source store/load order: {early}")
    first_writes = sum(event[:2] == ("store", "first") for event in stores)
    if name.startswith("scalar_duplicate_") and first_writes != 2:
        raise RuntimeError(f"{name}: erased a second live scalar write")
    aliases = {
        "scalar_alias": [("alias", "first")],
        "scalar_single_write_repair": [("trace", "first")],
        "scalar_alias_chain": [("saved", "first"), ("alias", "saved")],
        "scalar_alias_arithmetic": [("alias", "total")],
        "scalar_alias_branch_lifetime": [
            ("left", "first"),
            ("middle", "second"),
            ("right", "third"),
        ],
        "scalar_constant_only": [("copy", "fixed")],
        "constant_alias_chain": [("offset", "fixed"), ("copy", "offset")],
        "constant_alias_arithmetic": [("copy", "offset")],
        "constant_branch_lifetime": [
            ("left", "first"),
            ("middle", "second"),
            ("right", "third"),
            ("offset", "fixed"),
            ("copy", "offset"),
        ],
    }.get(name, [])
    if (
        name
        in {
            "constant_duplicate_write",
            "constant_boolean_duplicate_write",
            "constant_boolean_mixed_write",
            "constant_string_duplicate_write",
            "constant_string_mixed_write",
        }
        and sum(event[:2] == ("store", "fixed") for event in stores) != 2
    ):
        raise RuntimeError(f"{name}: erased the second constant-only global write")
    constant = constant_global_cases().get(name)
    if constant:
        source = constant["source"]
        aliases += [
            (destination, origin)
            for destination, origin in re.findall(
                r"(?:const|var) (\w+) = (\w+);", source.rsplit("});\n", 1)[1]
            )
            if origin in constant["saved"]
        ]
    for destination, origin in aliases:
        expected = ("store", destination, "global:" + origin)
        writes = [event for event in stores if event[1] == destination]
        if writes != [expected] or ("load", origin) not in loads:
            raise RuntimeError(f"{name}: lost the exact {origin} to {destination} alias edge")


def check_scalar_global_emission(args, ir, name):
    if (
        name not in NUMERIC_ENTRY_SAVED_GLOBALS
        and name not in scalar_global_sources()
        and name not in constant_global_sources()
    ):
        return
    expected = scalar_global_graph(ir.read_text(), name)
    for mode in ("explicit", "deduced"):
        cpp = (args.work / f"{name}.{mode}.cpp").read_text()
        entry = re.search(r"\bmain\(\)\s*\{(.*?)^\}", cpp, re.M | re.S)
        if not entry:
            raise RuntimeError(f"{name}/{mode}: lost the scalar entry")
        observed = set(re.findall(r"ctnative::global_(?:number|boolean|string)\((\w+)\)", entry[1]))
        methods_by_value = dict(
            re.findall(r"(\w+)\s*=\s*ctnative::method_get<&[^>\n]+::m_(\w+)>\(", entry[1])
        )
        values, actual, calls, binaries = {}, [], 0, 0
        for line in entry[1].splitlines():
            assignment = re.search(r"\b(\w+)\s*=\s*(.*);$", line)
            result, expression = assignment.groups() if assignment else (None, "")
            if call := re.search(r"ctnative::invoke_callable\((\w+)([^;\n]*)\);", line):
                arguments = [arg.strip() for arg in call[2].split(",")[1:]]
                actual.append(
                    (
                        "call",
                        methods_by_value.get(call[1]),
                        tuple(values.get(arg, "unknown") for arg in arguments),
                    )
                )
                if result:
                    values[result] = "call:" + str(calls)
                calls += 1
            elif not result:
                continue
            elif result.startswith("g_"):
                if result != "g_host":
                    actual.append(("store", result[2:], values.get(expression, "unknown")))
            elif expression.startswith("g_"):
                values[result] = "global:" + expression[2:]
                if expression != "g_host" and result not in observed:
                    actual.append(("load", expression[2:]))
            elif match := re.fullmatch(
                r"ctnative::(?:to_number|to_nullable|to_nullable_string|string_text|scalar_truthy)\((\w+)\)",
                expression,
            ):
                values[result] = values.get(match[1], "unknown")
            elif match := re.fullmatch(
                r'(?:ctnative::js_string|std::string)\((R"\(.*\)"|"(?:[^"\\]|\\.)*")(?:, (\d+))?\)',
                expression,
            ):
                literal = scalar_string_literal(match[1])
                if match[2] and len(literal.encode("utf-8", "surrogatepass")) != int(match[2]):
                    raise RuntimeError(f"{name}/{mode}: emitted String lost its exact byte length")
                values[result] = ("string", literal)
            elif expression in {"true", "false"}:
                values[result] = ("boolean", expression)
            elif re.fullmatch(r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?", expression):
                values[result] = float(expression)
            elif match := re.fullmatch(r"(\w+) ([+*/-]) (\w+)", expression):
                operation = {"+": "add", "-": "sub", "*": "mul", "/": "div"}[match[2]]
                actual.append(
                    (
                        "binary",
                        operation,
                        values.get(match[1], "unknown"),
                        values.get(match[3], "unknown"),
                    )
                )
                values[result] = "binary:" + str(binaries)
                binaries += 1
            elif expression in values:
                values[result] = values[expression]
        if actual != expected:
            raise RuntimeError(
                f"{name}/{mode}: emitted scalar dataflow changed\n{expected}\n{actual}"
            )
        case = constant_global_cases().get(name)
        if case:
            requested = {**case["saved"], "trace": case["expected_trace"]}
            for binding, value in requested.items():
                tag = (
                    "string"
                    if isinstance(value, StringValue) or value == "owned scalar"
                    else "boolean" if isinstance(value, bool) else "number"
                )
                carrier = "nullable_string" if tag == "string" else "nullable_scalar"
                if not re.search(rf"\bctnative::{carrier}\s+g_{binding}\s*;", cpp):
                    raise RuntimeError(
                        f"{name}/{mode}: {binding} lost its independently typed owning storage"
                    )
                loaded = re.findall(rf"\b(\w+)\s*=\s*g_{binding};", entry[1])
                checked = [
                    temporary
                    for temporary in loaded
                    if re.search(rf"ctnative::global_{tag}\({temporary}\)", entry[1])
                ]
                if len(checked) != 1:
                    raise RuntimeError(
                        f"{name}/{mode}: {binding} lost its exact {tag} observation check"
                    )


def check_scalar_global_carriers(args, positives, node, reference):
    cases = scalar_global_cases()
    for name in sorted(SCALAR_GLOBAL_CARRIERS):
        case = cases[name]
        source, value = case["source"], case["expected_trace"]
        js, ir, count = boundary.prepare(args, name, source)
        check_leaf_absence_census(args, ir, name)
        if count != 5 or (
            host.run([node, "-e", boundary.NODE, str(js)]).stdout != f"trace={value}\n"
            or host.run([str(reference), str(js)]).stdout != numeric_reference_output(name, value)
        ):
            raise RuntimeError(f"{name}: saved-global source census or observation changed")
        repaired_source = source.replace(case["removed_text"], case["replacement_text"])
        if (
            source.count(case["removed_text"]) != 1
            or repaired_source != positives[case["repair"]][0]
        ):
            raise RuntimeError(f"{name}: scalar repair no longer restores its exact source")
        _, repaired, repaired_count = boundary.prepare(args, name + "-restored", repaired_source)
        if (
            repaired_count != count
            or len(source_calls(repaired.read_text())) != case["prepared_calls"]
        ):
            raise RuntimeError(f"{name}: scalar repair lost an evaluated method call")
        config = contract(args, ir, name)
        repaired_config = contract(args, repaired, name + "-restored")
        for mode, options in (("default", ""), ("disabled", "optimize=false")):

            def reject(input_ir, label, current_config):
                failed = owned.lower(
                    args, input_ir, label, current_config, options=options, cleanup=False
                )
                text = methods.census(failed, count, label, admitted=0)
                reason = "standard Map identity is unproved with other host/global value reads"
                if "ctnative.host_owner_proved = true" not in text or reason not in text:
                    raise RuntimeError(
                        f"{label}: lost independent scalar ownership/carrier boundary"
                    )
                check_scalar_global_preparation(text, input_ir.read_text(), name)
                return failed

            label = name + "-" + mode
            reject(ir, label, config)
            output = owned.lower(
                args, repaired, label + "-restored", repaired_config, options=options
            )
            checked = methods.census(output, count, label + "-restored", admitted=count)
            if "ctnative.host_owner_proved = true" not in checked:
                raise RuntimeError(f"{label}: scalar repair lost complete ownership")
            for payload in ("bool", "string", "nullable_string"):
                forged_name = label + "-forged-" + payload
                forged = args.work / f"{forged_name}.mlir"
                forged.write_text(forge_leaf_evidence(ir.read_text(), payload))
                stale = methods.refused(
                    args,
                    forged,
                    forged_name + "-stale",
                    config,
                    options=options,
                    reason="fingerprint mismatch",
                    admitted=0,
                )
                check_call_preservation(
                    forged.read_text(), stale.read_text(), forged_name + "-stale"
                )
                check_scalar_global_preparation(stale.read_text(), forged.read_text(), name)
                fresh = contract(args, forged, forged_name)
                failed = reject(forged, forged_name, fresh)
                rerun = methods.refused(
                    args,
                    failed,
                    forged_name + "-rerun",
                    fresh,
                    options=options,
                    reason="fingerprint mismatch",
                    admitted=0,
                )
                check_call_preservation(
                    failed.read_text(), rerun.read_text(), forged_name + "-rerun"
                )
                check_scalar_global_preparation(rerun.read_text(), failed.read_text(), name)


def check_constant_global_observations(args, node, reference):
    for name, row in constant_global_cases().items():
        js = args.work / f"{name}-observed.js"
        js.write_text(row["source"])
        names = json.dumps(sorted(["trace", *row["saved"]]))
        expected = scalar_global_output(name, row["expected_trace"])
        node_output = host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), names]).stdout
        result = host.run([str(reference), str(js)])
        if node_output != expected or normalized_scalar_output(result.stdout) != expected:
            raise RuntimeError(
                f"{name}: exact typed scalar observations changed\n{expected}\n"
                f"{node_output}\n{result.stdout}"
            )
        counts = [0, 0, 0, 0, 0]
        for value in [row["expected_trace"], *row["saved"].values()]:
            index = (
                1
                if isinstance(value, bool)
                else (
                    2
                    if isinstance(value, StringValue) or value == "owned scalar"
                    else (4 if value == "undefined" else 0)
                )
            )
            counts[index] += 1
        types = re.search(
            r"\((\d+) number, (\d+) boolean, (\d+) string, (\d+) null, (\d+) undefined\)",
            result.stderr,
        )
        if not types or list(map(int, types.groups())) != counts:
            raise RuntimeError(f"{name}: reference lost its independently observed scalar tags")
    # A trace-only observer would miss substitutions in the historical copy.
    # Every mutation must instead change the complete value-and-tag observation.
    for name, old, replacement in (
        ("constant_exact_historical", "const copy = fixed;", "const copy = 0;"),
        ("constant_feeds_trace", "const copy = fixed;", "const copy = first;"),
        ("constant_negative_zero", "0 / (0 - 1)", "0"),
        ("constant_nan", "0 / 0", "0"),
        ("constant_nan", "0 / 0", "void 0"),
        ("constant_nan", "0 / 0", "'NaN'"),
        ("constant_boolean", "const fixed = false;", "const fixed = 0;"),
        ("constant_boolean", "const copy = fixed;", "const copy = true;"),
        ("constant_boolean_true", "const copy = fixed;", "const copy = 1;"),
        ("constant_boolean_alias_chain", "const copy = offset;", "const copy = enabled;"),
        ("constant_boolean_trace_false", "var trace = copy;", "var trace = 0;"),
        ("constant_boolean_trace_true", "var trace = copy;", "var trace = 1;"),
        ("constant_boolean_saved_result", "const copy = first;", "const copy = fixed;"),
        (
            "constant_boolean_branch_lifetime",
            "const copy_flag = fixed_flag;",
            "const copy_flag = enabled;",
        ),
        ("constant_string", "'owned scalar'", "7"),
        ("constant_string_empty", "const copy = fixed;", "const copy = void 0;"),
        ("constant_string_empty", "const copy = fixed;", "const copy = null;"),
        ("constant_string_bytes", "const copy = fixed;", "const copy = 'tail';"),
        ("constant_string_long", "const copy = fixed;", "const copy = '';"),
        ("constant_string_alias_chain", "const copy = offset;", "const copy = false;"),
        ("constant_undefined", "void 0", "0 / 0"),
        ("constant_alias_chain", "const copy = offset;", "const copy = first;"),
        ("constant_alias_arithmetic", "const copy = offset;", "const copy = fixed;"),
        ("constant_branch_lifetime", "const copy = offset;", "const copy = first;"),
    ):
        row = constant_global_cases()[name]
        assert row["source"].count(old) == 1, name
        js = args.work / f"{name}-constant-blind.js"
        js.write_text(row["source"].replace(old, replacement))
        names = json.dumps(sorted(["trace", *row["saved"]]))
        if host.run(
            [node, "-e", CONSTANT_GLOBAL_NODE, str(js), names]
        ).stdout == scalar_global_output(name, row["expected_trace"]):
            raise RuntimeError(f"{name}: typed observation cannot distinguish {replacement}")


def check_boolean_observation_mutations(args, compilers, nm):
    # Mutate only an emitted store after all source proofs and successful native
    # executions. A missing store or a numerically equal value of another tag
    # must fail the final observation instead of printing a plausible Boolean.
    check_scalar_observation_mutations(
        args,
        compilers,
        nm,
        (
            ("constant_boolean", "copy", "ctnative::nullable_scalar(0.0)"),
            ("constant_boolean_true", "copy", "ctnative::nullable_scalar(1.0)"),
            ("constant_boolean", "copy", "ctnative::nullable_scalar::null()"),
            ("constant_boolean", "copy", None),
            ("constant_boolean_true", "copy", None),
            ("constant_boolean", "first", "ctnative::nullable_scalar(true)"),
            ("constant_boolean", "first", None),
        ),
    )


def check_string_observation_mutations(args, compilers, nm):
    check_scalar_observation_mutations(
        args,
        compilers,
        nm,
        (
            (
                "constant_string",
                "copy",
                "ctnative::to_nullable_string(ctnative::nullable_scalar::null())",
            ),
            ("constant_string", "copy", "ctnative::nullable_string{}"),
            ("constant_string", "copy", None),
            ("constant_string_empty", "copy", None),
            ("constant_string_trace_empty", "trace", None),
        ),
    )


def check_scalar_observation_mutations(args, compilers, nm, mutations):
    for name, binding, replacement in mutations:
        kind = (
            "missing"
            if replacement is None
            else ("null" if "::null" in replacement else "wrong-tag")
        )
        row = constant_global_cases()[name]
        output = scalar_global_output(name, row["expected_trace"])
        preceding = output.split(binding + "=", 1)[0]
        for mode in ("explicit", "deduced"):
            original = (args.work / f"{name}.{mode}.cpp").read_text()
            pattern = rf"(?m)^(\s*)g_{binding} = ([^;\n]+);$"
            matches = list(re.finditer(pattern, original))
            if len(matches) != 1:
                raise RuntimeError(f"{name}/{mode}: lost unique {binding} store mutation")
            match = matches[0]
            assignment = (
                f"{match[1]}(void){match[2]};"
                if replacement is None
                else f"{match[1]}g_{binding} = {replacement};\n{match[1]}(void){match[2]};"
            )
            changed = original[: match.start()] + assignment + original[match.end() :]
            changed, count = re.subn(
                r"(\bmain\(\)\s*\{)",
                r"\1\n    std::set_terminate([] { std::fflush(stdout); std::_Exit(211); });",
                changed,
            )
            if count != 1:
                raise RuntimeError(f"{name}/{mode}: lost exact observation termination witness")
            source = args.work / f"{name}.{mode}.{binding}-{kind}.cpp"
            source.write_text(
                "#include <cstdio>\n#include <cstdlib>\n#include <exception>\n" + changed
            )
            binary = source.with_suffix(".mutated").resolve()
            host.run([compilers[1], *owned.FLAGS, str(source), "-o", str(binary)])
            if owned.VM.search(host.run([nm, "-C", str(binary)]).stdout):
                raise RuntimeError(f"{name}/{mode}: observation control linked a VM symbol")
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            if result.returncode != 211 or result.stdout != preceding or result.stderr:
                raise RuntimeError(
                    f"{name}/{mode}: {binding} {kind} bypassed exact observation tag check "
                    f"(exit {result.returncode})\n{result.stdout}{result.stderr}"
                )


def check_constant_global_refusals(args, positives, node, reference):
    cases = constant_global_cases()
    for name in sorted(CONSTANT_GLOBAL_UNOWNED | CONSTANT_GLOBAL_CARRIERS):
        row = cases[name]
        source = row["source"]
        repair = row.get("candidate")
        if repair in constant_global_sources():
            old, replacement = row["removed_text"], row["replacement_text"]
        else:
            # Literal substitution alone does not repair an unsupported global.
            # Restore both declarations to the exact independently gated source.
            old = source.rsplit("\n", 2)[-2]
            replacement = "const fixed = 7; const copy = fixed;"
            repair = "scalar_constant_only"
        if source.count(old) != 1 or source.replace(old, replacement) != positives[repair][0]:
            raise RuntimeError(f"{name}: lost its exact independently gated repair")
        _, ir, count = boundary.prepare(args, name, source)
        check_leaf_absence_census(args, ir, name)
        _, restored, repair_count = boundary.prepare(
            args, repair + "-restored-for-" + name, positives[repair][0]
        )
        if (
            count != 5
            or repair_count != count
            or len(source_calls(restored.read_text())) != row["prepared_calls"]
        ):
            raise RuntimeError(
                f"{name}: constant-global repair changed the five-function/eight-call source"
            )
        config = contract(args, ir, name)
        restored_config = contract(args, restored, repair + "-restored-for-" + name)
        for mode, options in (("default", ""), ("disabled", "optimize=false")):

            def check_current(input_ir, label, current_config):
                promoted = name == "constant_undefined_candidate"
                failed = owned.lower(
                    args, input_ir, label, current_config, options=options, cleanup=promoted
                )
                text = methods.census(failed, count, label, admitted=count if promoted else 0)
                owner = name not in CONSTANT_GLOBAL_UNOWNED
                if f"ctnative.host_owner_proved = {str(owner).lower()}" not in text:
                    raise RuntimeError(
                        f"{label}: constant-global refusal changed its independent owner boundary"
                    )
                if promoted:
                    compilers = [
                        shutil.which("g++-13") or shutil.which("g++"),
                        shutil.which("clang++-18") or shutil.which("clang++"),
                    ]
                    owned.standalone(
                        args,
                        failed,
                        label,
                        scalar_global_output(name, row["expected_trace"]),
                        compilers,
                        shutil.which("nm"),
                    )
                    return failed
                if owner:
                    attribute = "ctnative.not_native"
                    reason = "standard Map identity is unproved with other host/global value reads"
                else:
                    attribute = "ctnative.host_owner_reason"
                    reason = {
                        "constant_read_before_write": "global read lacks definite source initialization",
                        "constant_boolean_read_before_write": "global read lacks definite source initialization",
                        "constant_string_read_before_write": "global read lacks definite source initialization",
                        "constant_dynamic_global": "unproved host binding `globalThis`",
                        "constant_boolean_dynamic_global": "unproved host binding `globalThis`",
                        "constant_future_method_write": "property call lacks a current source getter proof",
                        "constant_boolean_future_method_write": "property call lacks a current source getter proof",
                        "constant_string_future_method_write": "property call lacks a current source getter proof",
                    }[name]
                if f'{attribute} = "{reason}"' not in text:
                    raise RuntimeError(
                        f"{label}: lost its independent ownership/Map identity/global carrier boundary"
                    )
                check_scalar_global_preparation(text, input_ir.read_text(), name)
                if not owner:
                    check_call_preservation(input_ir.read_text(), text, label)
                return failed

            label = name + "-" + mode
            check_current(ir, label, config)
            output = owned.lower(
                args, restored, label + "-restored", restored_config, options=options
            )
            if "ctnative.host_owner_proved = true" not in methods.census(
                output, count, label, admitted=count
            ):
                raise RuntimeError(f"{label}: exact constant-global repair lost ownership")
            forged = args.work / f"{label}-forged.mlir"
            forged.write_text(forge_leaf_evidence(ir.read_text(), "string"))
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
            check_scalar_global_preparation(stale.read_text(), forged.read_text(), name)
            fresh = contract(args, forged, label + "-forged")
            failed = check_current(forged, label + "-fresh", fresh)
            if name == "constant_undefined_candidate":
                continue  # Successful lowering leaves no source calls for a refusal rerun.
            rerun = methods.refused(
                args,
                failed,
                label + "-rerun",
                fresh,
                options=options,
                reason="fingerprint mismatch",
                admitted=0,
            )
            check_call_preservation(failed.read_text(), rerun.read_text(), label + "-rerun")
            check_scalar_global_preparation(rerun.read_text(), failed.read_text(), name)


def check_leaf_absence_census(args, ir, name):
    case = {
        **leaf_absence_cases(),
        **leaf_clear_cases(),
        **numeric_entry_cases(),
        **scalar_global_cases(),
        **constant_global_cases(),
    }.get(name)
    if case is None:
        return
    check_scalar_global_source(ir, name)
    raw = args.work / f"{name}.raw.mlir"
    if (len(source_calls(raw.read_text())), len(source_calls(ir.read_text()))) != (
        case["raw_calls"],
        case["prepared_calls"],
    ):
        raise RuntimeError(f"{name}: changed the independent raw/prepared call census")
    if (
        "distinct_branches_" in name or name.startswith("local_clear_both_branches_")
    ) and ir.read_text().count("scf.if") < 2:
        raise RuntimeError(f"{name}: lost the nonidentical two-arm absence join")
