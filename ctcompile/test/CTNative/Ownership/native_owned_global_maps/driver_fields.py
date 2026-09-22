from .driver_common import (
    CONSTANT_GLOBAL_NODE,
    StringValue,
    boundary,
    check_call_preservation,
    comparable_provenance,
    contract,
    forge_leaf_evidence,
    host,
    leaf_readback_refusals,
    methods,
    nullable_payload_refusals,
    owned,
    re,
    scalar_global_output,
    source_calls,
    standalone,
    string_field_cases,
    string_field_observer_source,
    subprocess,
)

STRING_FIELD_OBSERVERS = (
    "leaf_object_string_field",
    "field_string_alias_write",
    "field_string_saved",
    "field_string_saved_overwrite",
    "field_string_lifetime",
)


def check_string_field_census(args, ir, name):
    if name not in string_field_cases():
        return
    row = string_field_cases()[name]
    raw = (args.work / f"{name}.raw.mlir").read_text()
    if (
        len(boundary.FUNCTION.findall(raw)) != 5
        or len(source_calls(raw)) != row["raw_calls"]
        or len(source_calls(ir.read_text())) != row["prepared_calls"]
    ):
        raise RuntimeError(f"{name}: changed the exact five-function field source/call census")


def check_string_field_observations(args, node, reference):
    cases = string_field_cases()
    for name, row in cases.items():
        js = args.work / f"{name}-field-observed.js"
        js.write_text(row["source"])
        value = row["expected_trace"]
        expected = scalar_global_output(name, value)
        node_result = host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]'])
        if node_result.stdout != expected:
            raise RuntimeError(
                f"{name}: Node typed field bytes changed\n{node_result.stdout}\n{expected}"
            )
        if reference:
            result = host.run([str(reference), str(js)])
            counts = (
                "0 number, 0 boolean, 1 string, 0 null, 0 undefined"
                if isinstance(value, StringValue)
                else (
                    "0 number, 0 boolean, 0 string, 0 null, 1 undefined"
                    if value == "undefined"
                    else "1 number, 0 boolean, 0 string, 0 null, 0 undefined"
                )
            )
            if result.stdout != expected or "(" + counts + ")" not in result.stderr:
                raise RuntimeError(
                    f"{name}: interpreter lost independently observed field tags/bytes"
                )
    mutations = (
        ("field_string_read", "value: 'instance'", "value: ''"),
        ("field_string_empty", "return item.value;", "return void 0;"),
        ("field_string_empty", "return item.value;", "return null;"),
        ("field_string_bytes", "return item.value;", "return 'tail';"),
        ("field_string_saved", "return saved;", "return alias.value;"),
        ("field_string_alias_write", "alias.value = 'changed';", "alias.value = 'first';"),
        ("field_string_saved_overwrite", "return saved;", "return item.value;"),
        ("field_string_lifetime", "return saved;", "return alias.value;"),
    )
    for index, (name, old, replacement) in enumerate(mutations):
        row = cases[name]
        assert row["source"].count(old) == 1, name
        js = args.work / f"{name}-field-blind-{index}.js"
        js.write_text(row["source"].replace(old, replacement))
        result = host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]'])
        if result.stdout == scalar_global_output(name, row["expected_trace"]):
            raise RuntimeError(f"{name}: typed field observer cannot distinguish {replacement}")
    for name in STRING_FIELD_OBSERVERS:
        source = cases[name]["source"]
        observed, value = string_field_observer_source(source, name)
        js = args.work / f"{name}-field-future.js"
        js.write_text(observed)
        expected = f"trace={value}\n"
        if (
            host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
            or reference
            and host.run([str(reference), str(js)]).stdout != expected
        ):
            raise RuntimeError(
                f"{name}: future String field identity or saved-read observation changed"
            )
        mutations = []
        if "state.delete(key);" in source:
            mutations += [
                ("state.delete(key);", "state.has(key);"),
                ("return saved;", "return alias.value;"),
            ]
        if name == "field_string_lifetime":
            mutations += [
                ("alias.value = 'left';", "alias.value = 'right';"),
                ("item.value = 'right';", "item.value = 'left';"),
            ]
        elif name == "field_string_alias_write":
            mutations += [("alias.value = 'changed';", "alias.value = 'first';")]
        elif name == "leaf_object_string_field":
            mutations += [("value: 'instance'", "value: ''")]
        for index, (old, replacement) in enumerate(mutations):
            assert source.count(old) == 1, name
            blind, _ = string_field_observer_source(source.replace(old, replacement), name)
            js = args.work / f"{name}-field-future-blind-{index}.js"
            js.write_text(blind)
            if host.run([node, "-e", boundary.NODE, str(js)]).stdout == expected:
                raise RuntimeError(
                    f"{name}: future field observer cannot distinguish {replacement}"
                )


def check_string_field_refusals(args, positives):
    for name, row in string_field_cases().items():
        if row["admitted"]:
            continue
        _, ir, count = boundary.prepare(args, name, row["source"])
        check_string_field_census(args, ir, name)
        repair = row["repair"]
        if (
            row["source"].replace(row["removed_text"], row["replacement_text"])
            != positives[repair][0]
        ):
            raise RuntimeError(
                f"{name}: field repair no longer restores its independently executed source"
            )
        _, restored, repaired_count = boundary.prepare(
            args, name + "-restored", positives[repair][0]
        )
        if count != 5 or repaired_count != 5:
            raise RuntimeError(f"{name}: exact field repair changed function count")
        config = contract(args, ir, name)
        repaired_config = contract(args, restored, name + "-restored")
        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            label = name + "-" + mode

            def reject(input_ir, current, current_config):
                failed = owned.lower(
                    args, input_ir, current, current_config, options=options, cleanup=False
                )
                text = methods.census(failed, 5, current, admitted=0)
                if f'ctnative.host_owner_proved = {str(row["owner"]).lower()}' not in text:
                    raise RuntimeError(
                        f"{current}: field refusal lost its independent ownership boundary"
                    )
                if not row["owner"]:
                    check_call_preservation(input_ir.read_text(), text, current)
                else:
                    calls = re.findall(
                        r"^\s*(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\) "
                        r"\{ctnative\.stored_call = 1 : i32\}",
                        text,
                        re.M,
                    )
                    if (
                        len(source_calls(text)) != row["prepared_calls"]
                        or [callee for _, callee, _ in calls] != ["fn$3", "fn$4"]
                        or [len(actuals.split(", ")) for _, _, actuals in calls] != [4, 5]
                        or f'ctjs.store_global "trace", {calls[-1][0]}' not in text
                    ):
                        raise RuntimeError(
                            f"{current}: mixed field storage erased current result/capture operands"
                        )
                return failed

            reject(ir, label, config)
            repaired = owned.lower(
                args, restored, label + "-restored", repaired_config, options=options
            )
            if "ctnative.host_owner_proved = true" not in methods.census(
                repaired, 5, label, admitted=5
            ):
                raise RuntimeError(f"{label}: exact field repair lost native ownership")
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
                failed = reject(forged, current + "-fresh", fresh)
                rerun = methods.refused(
                    args, failed, current + "-rerun", fresh, options=options, admitted=0
                )
                check_call_preservation(failed.read_text(), rerun.read_text(), current + "-rerun")


def check_string_field_tag_mutations(args, compilers, nm):
    for name, replacement in (
        ("field_string_read", "ctnative::nullable_string{}"),
        ("field_string_empty", "ctnative::nullable_string{}"),
        ("field_string_read", "ctnative::to_nullable_string(ctnative::nullable_scalar::null())"),
    ):
        for mode in ("explicit", "deduced"):
            original = (args.work / f"{name}.{mode}.cpp").read_text()
            pattern = r"ctnative::object_set_field_76616c7565\((\w+), (\w+)\);"
            matches = list(re.finditer(pattern, original))
            if len(matches) != 1:
                raise RuntimeError(f"{name}/{mode}: lost exact field-store mutation site")
            match = matches[0]
            changed = (
                original[: match.start()]
                + "ctnative::object_set_field_76616c7565("
                + match[1]
                + ", "
                + replacement
                + "); (void)"
                + match[2]
                + ";"
                + original[match.end() :]
            )
            changed, count = re.subn(
                r"(\bmain\(\)\s*\{)",
                r"\1\n    std::set_terminate([] { std::fflush(stdout); std::_Exit(211); });",
                changed,
            )
            if count != 1:
                raise RuntimeError(f"{name}/{mode}: lost exact field-read termination observer")
            tag = "null" if "::null" in replacement else "undefined"
            source = args.work / f"{name}.{mode}.{tag}-field.cpp"
            source.write_text(
                "#include <cstdio>\n#include <cstdlib>\n#include <exception>\n" + changed
            )
            binary = source.with_suffix(".mutated").resolve()
            host.run([compilers[1], *owned.FLAGS, str(source), "-o", str(binary)])
            if owned.VM.search(host.run([nm, "-C", str(binary)]).stdout):
                raise RuntimeError(f"{name}/{mode}: field tag mutation linked a VM symbol")
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            if result.returncode != 211 or result.stdout or result.stderr:
                raise RuntimeError(
                    f"{name}/{mode}: {tag} field bypassed exact String read tag check\n"
                    f"{result.returncode}: {result.stdout}{result.stderr}"
                )


HISTORICAL_STRING_FIELD_CARRIERS = {"leaf_readback_unknown_alias_field", "nullable_payload_object"}


def string_field_method_graph(text, function, prepared):
    body = re.search(
        r"^  ctjs\.func(?: private)? @" + re.escape(function) + r"\([^\n]*\n(.*?)^  \}",
        text,
        re.M | re.S,
    )
    if not body:
        raise RuntimeError(f"{function}: missing historical String field method")
    values = {
        f"%arg{index}": value for index, value in enumerate(("receiver", "new_target", "closure"))
    }
    if prepared:
        values["%arg3"] = "capture0"
    values["%arg4" if prepared else "%arg3"] = "actual0"
    graph = []
    for raw in body[1].splitlines():
        line = re.sub(r" \{[^{}\n]*\}$", "", raw.strip())
        capture = re.fullmatch(r"(%[-\w.$]+) = ctjs\.load_upvalue %arg2\[0\]", line)
        if capture:
            values[capture[1]] = "capture0"
            continue
        result = re.match(r"(%[-\w.$]+) = ", line)
        if result:
            values[result[1]] = f"value{len(graph)}"
        try:
            graph.append(re.sub(r"%[-\w.$]+", lambda match: values[match[0]], line))
        except KeyError as error:
            raise RuntimeError(f"{function}: unknown source operand in {line}") from error
    return graph


def check_historical_string_field_preparation(text, original, name):
    diagnostic = (
        "native Map needs supported keys and numeric, boolean, closed mixed, owning-string, "
        "object-identity union or acyclic Map values; inferred "
        "!ctnative.map<!ctnative.opt<!ctnative.str<utf8>>, !ctnative.boxed>"
    )
    if (
        name != "nullable_payload_object"
        or "ctnative.host_owner_proved = true" not in text
        or diagnostic not in text
        or len(boundary.FUNCTION.findall(text)) != 6
        or boundary.NATIVE.search(text)
        or len(source_calls(original)) != 11
        or len(source_calls(text)) != 11
    ):
        raise RuntimeError(
            f"{name}: lost exact ownership, native refusal or historical call census"
        )
    for function in ("fn$3", "fn$4", "fn$5"):
        if string_field_method_graph(original, function, False) != string_field_method_graph(
            text, function, True
        ):
            raise RuntimeError(
                f"{name}: preparation changed {function} source field/Map/branch operands"
            )
    published = re.findall(
        r"^\s*(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\) "
        r"\{ctnative\.stored_call = 1 : i32\}",
        text,
        re.M,
    )
    actuals = [arguments.split(", ") for _, _, arguments in published]
    expected = ["fn$4", "fn$5", "fn$4", "fn$5", "fn$3"]
    arities = [5, 5, 5, 5, 4]
    if (
        [target for _, target, _ in published] != expected
        or list(map(len, actuals)) != arities
        or f'ctjs.store_global "trace", {published[-1][0]}' not in text
    ):
        raise RuntimeError(f"{name}: changed prepared source call order, arguments or trace result")
    getters = dict(re.findall(r"^\s*(%[-\w.$]+) = ctjs\.get_property (%[-\w.$]+)\[", text, re.M))
    captures = dict(
        re.findall(r"^\s*(%[-\w.$]+) = ctjs\.load_upvalue (%[-\w.$]+)\[0\]", text, re.M)
    )
    for arguments in actuals:
        if getters.get(arguments[2]) != arguments[0] or captures.get(arguments[3]) != arguments[2]:
            raise RuntimeError(
                f"{name}: published call lost its current receiver/callee/Map capture"
            )
    for producer, consumer, flag in ((0, 1, "false"), (2, 3, "true")):
        if (
            actuals[consumer][-1] != published[producer][0]
            or f"{actuals[producer][-1]} = ctjs.constant #ctjs.boolean<{flag}>" not in text
        ):
            raise RuntimeError(f"{name}: mixed Object/String refusal lost a producer-result actual")


def check_historical_string_field_carriers(args, positives, node, reference, compilers, nm):
    controls = {
        "leaf_readback_unknown_alias_field": leaf_readback_refusals()[
            "leaf_readback_unknown_alias_field"
        ]
    }
    source, value, old, replacement, _ = nullable_payload_refusals()["nullable_payload_object"]
    controls["nullable_payload_object"] = (
        source,
        value,
        old,
        replacement,
        "nullable_payload_write",
        11,
    )
    for name, (source, value, old, replacement, repair, calls) in controls.items():
        js, ir, count = boundary.prepare(args, name, source)
        expected = f"trace={value}\n"
        if (
            host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
            or host.run([str(reference), str(js)]).stdout != expected
            or len(source_calls((args.work / f"{name}.raw.mlir").read_text())) != calls
            or len(source_calls(ir.read_text())) != calls
        ):
            raise RuntimeError(f"{name}: changed historical source observation or original calls")
        if source.count(old) != 1 or source.replace(old, replacement) != positives[repair][0]:
            raise RuntimeError(
                f"{name}: exact repair no longer restores its independently gated source"
            )
        _, restored, restored_count = boundary.prepare(
            args, name + "-restored", positives[repair][0]
        )
        if count != restored_count:
            raise RuntimeError(f"{name}: exact repair changed function census")
        config = contract(args, ir, name)
        restored_config = contract(args, restored, name + "-restored")
        admitted = name == "leaf_readback_unknown_alias_field"
        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            label = name + "-" + mode

            def lower(input_ir, current, current_config):
                output = owned.lower(
                    args, input_ir, current, current_config, options=options, cleanup=admitted
                )
                if admitted:
                    text = methods.census(output, count, current, admitted=count)
                    if "ctnative.host_owner_proved = true" not in text:
                        raise RuntimeError(f"{current}: lost the original String field owner")
                else:
                    check_historical_string_field_preparation(
                        output.read_text(), input_ir.read_text(), name
                    )
                return output

            output = lower(ir, label, config)
            if admitted:
                standalone(args, output, label, value, compilers, nm)
                expected_cpp = comparable_provenance(
                    host.run([args.translate, "--mlir-to-cpp", str(output)]).stdout, ir
                )
            repaired = owned.lower(
                args, restored, label + "-restored", restored_config, options=options
            )
            if "ctnative.host_owner_proved = true" not in methods.census(
                repaired, count, label, admitted=count
            ):
                raise RuntimeError(f"{name}: original exact repair lost its native owner")
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
                failed = lower(forged, current + "-fresh", fresh)
                if (
                    admitted
                    and comparable_provenance(
                        host.run([args.translate, "--mlir-to-cpp", str(failed)]).stdout, forged
                    )
                    != expected_cpp
                ):
                    raise RuntimeError(f"{current}: forged facts changed native String equality")
                rerun = owned.lower(
                    args,
                    failed,
                    current + "-rerun",
                    fresh,
                    options=options,
                    cleanup=False,
                )
                text = methods.census(rerun, count, current, admitted=count if admitted else 0)
                if (
                    "ctnative.host_owner_proved = false" not in text
                    or "fingerprint mismatch" not in text
                ):
                    raise RuntimeError(f"{current}: prepared output reused source authority")
                check_call_preservation(failed.read_text(), rerun.read_text(), current + "-rerun")
