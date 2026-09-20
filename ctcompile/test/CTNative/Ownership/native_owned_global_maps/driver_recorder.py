"""driver recorder: continued from sources_recorders."""

from .sources_recorders import *


def template_lifetime_cpp(cpp, child):
    # The existing observer records weak allocation witnesses without changing
    # the generated Map implementation or its ownership.
    changed = instrument_leaf_objects(cpp, allocations=1 if child else 0)
    argument = "element" if child else ""
    method = "remove" if child else "clear"
    changed += f"""
int main() {{
    if (ctnative_test_entry() != 0 || ctn_test_maps.empty()) {{ return 100; }}
    auto first = g_host;
    auto table = first->slot;
    auto get = table->m_get;
    auto set = table->m_set;
    auto {method} = table->m_{method};
    std::weak_ptr first_lifetime = first;
    std::weak_ptr table_lifetime = table;
"""
    if child:
        changed += "    auto element = g_element;\n    g_element.reset();\n"
    changed += f"""    g_host.reset();
    if (first->slot->m_get({argument}) != 42 || table->m_get({argument}) != 42 ||
        get({argument}) != 42) {{ return 101; }}
    first.reset();
    if (!first_lifetime.expired() || table_lifetime.expired() ||
        table->m_get({argument}) != 42) {{ return 102; }}
    table.reset();
    if (!table_lifetime.expired() || ctn_test_maps[0].expired()) {{ return 103; }}
    for (int i = 0; i < 1024; ++i) {{
"""
    changed += (
        template_future_body(child)
        .replace("!==", "!=")
        .replace("traceFuture = 0;", "return 104;")
        .replace("let key =", "std::string key =")
        .replace("const original =", "const std::string original =")
        .replace('"future-" + i', '"future-" + std::to_string(i)')
    )
    changed += f"""    }}
    const auto next = ctn_test_maps.size();
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() <= next ||
        ctn_test_maps[0].lock() == ctn_test_maps[next].lock() ||
        get({argument}) != 42) {{ return 105; }}
"""
    if child:
        changed += """    if (element == g_element || get(g_element) != 0 ||
        g_host->slot->m_get(g_element) != 42 || g_host->slot->m_get(element) != 0) {
        return 106;
    }
"""
    else:
        changed += "    if (g_host->slot->m_get() != 42) { return 106; }\n"
    changed += f"""    get = {{}};
    if (ctn_test_maps[0].expired()) {{ return 107; }}
    set = {{}};
    if (ctn_test_maps[0].expired()) {{ return 108; }}
    {method} = {{}};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[next].expired()) {{ return 109; }}
    g_host.reset();
"""
    if child:
        changed += "    element.reset();\n    g_element.reset();\n"
    return changed + """    for (const auto & map : ctn_test_maps) {
        if (!map.expired()) { return 110; }
    }
    for (const auto & object : ctn_test_objects) {
        if (!object.expired()) { return 111; }
    }
    return 0;
}
"""


def recorder_future_body(entry_objects=False):
    body = """    remove(element, "bs.alert");
    remove(element, "bs.collapse");
    const before = traceErrorCount;
    let key = "bs.alert";
    set(element, key, 42);
    key = "changed caller storage";
    set(element, "bs.alert", 43);
    if (get(element, "bs.alert") !== 43 || traceErrorCount !== before) { traceFuture = 0; }
    set(element, "bs.collapse", 99);
    if (get(element, "bs.alert") !== 43 || get(element, "bs.collapse") !== null ||
        traceErrorCount !== before + 1 || traceErrorMessage !== 1) { traceFuture = 0; }
    remove(element, "bs.alert");
    if (get(element, "bs.alert") !== null) { traceFuture = 0; }
    key = "future-" + i + "-long-owned-recorder-key";
    const original = key;
    set(element, key, 21);
    key = "";
    set(element, "bs.alert", 64);
    if (get(element, original) !== 21 || get(element, "bs.alert") !== null ||
        traceErrorCount !== before + 2 || traceErrorMessage !== 0) { traceFuture = 0; }
    remove(element, original);
    if (get(element, original) !== null) { traceFuture = 0; }
"""
    if entry_objects:
        body += """    set(element, "bs.alert", 0);
    if (get(element, "bs.alert") !== null) { traceFuture = 0; }
    remove(element, "bs.alert");
    const payload = {value: i + 0.5};
    const alias = payload;
    const other = {}, absent = {};
    set(element, "bs.alert", payload);
    set(other, "bs.alert", 21);
    const saved = get(element, "bs.alert");
    payload.value = i + 2;
    if (saved !== alias || saved.value !== i + 2 ||
        get(other, "bs.alert") !== 21 || get(absent, "bs.alert") !== null) { traceFuture = 0; }
    remove(element, "bs.missing"); remove(absent, "bs.alert");
    if (get(element, "bs.alert") !== payload || get(other, "bs.alert") !== 21 ||
        traceErrorCount !== before + 2) { traceFuture = 0; }
    set(element, "bs.alert", 43);
    if (get(element, "bs.alert") !== 43 || saved.value !== i + 2) { traceFuture = 0; }
    remove(element, "bs.alert");
    if (get(element, "bs.alert") !== null || saved !== payload) { traceFuture = 0; }
    set(element, "bs.collapse", payload);
    if (get(element, "bs.collapse") !== payload ||
        get(element, "bs.alert") !== null || get(other, "bs.alert") !== 21) { traceFuture = 0; }
    remove(element, "bs.collapse"); remove(other, "bs.alert");
"""
    return body


def recorder_future_source(entry_objects=False):
    return (
        "var traceFuture = 1;\n(function(element) {\n"
        "const set = host.slot.set, get = host.slot.get, remove = host.slot.remove;\n"
        "host = {};\nfor (let i = 0; i < 1024; ++i) {\n"
        + recorder_future_body(entry_objects)
        + "}\n})(element);\n"
    )


def recorder_lifetime_cpp(cpp, values, entry_objects=False):
    changed = instrument_leaf_objects(cpp, allocations=4 if entry_objects else 1)
    equal = "object_strict_equal" if entry_objects else "scalar_strict_equal"
    body = re.sub(
        r"(get\((?:element|other|absent), [^)]+\)) !== (null|[0-9]+|payload)",
        lambda match: "!ctnative::"
        + equal
        + "("
        + match[1]
        + ", "
        + (
            "ctnative::nullable_scalar::null()"
            if match[2] == "null"
            else match[2] if match[2] == "payload" else match[2] + ".0"
        )
        + ")",
        recorder_future_body(entry_objects),
    )
    if entry_objects:
        body = (
            body.replace(
                "const payload = {value: i + 0.5};",
                "auto payload = std::make_shared<ctnative::identity_object>();\n"
                "    payload->field_76616c7565 = ctnative::nullable_scalar{i + 0.5};\n"
                "    std::weak_ptr payload_lifetime = payload;",
            )
            .replace("const alias = payload;", "auto alias = payload;")
            .replace(
                "const other = {}, absent = {};",
                "auto other = std::make_shared<ctnative::identity_object>();\n"
                "    auto absent = std::make_shared<ctnative::identity_object>();",
            )
            .replace("const saved =", "auto saved =")
            .replace("saved !== alias", "!ctnative::object_strict_equal(saved, alias)")
            .replace("saved !== payload", "!ctnative::object_strict_equal(saved, payload)")
            .replace("saved.value", "saved.object->field_76616c7565.value")
            .replace("payload.value = i + 2;", "payload->field_76616c7565.value = i + 2;")
        )
        body += """    payload.reset(); alias.reset();
    if (payload_lifetime.expired() || saved.object->field_76616c7565.value != i + 2) { return 138; }
    saved = {};
    if (!payload_lifetime.expired()) { return 139; }
"""
    body = (
        body.replace("!==", "!=")
        .replace("traceFuture = 0;", "return 124;")
        .replace("const before =", "const auto before =")
        .replace("let key =", "std::string key =")
        .replace("const original =", "const std::string original =")
        .replace('"future-" + i', '"future-" + std::to_string(i)')
    )
    for binding in ("traceErrorCount", "traceErrorMessage"):
        body = body.replace(binding, f"ctnative::global_number(g_{binding}).value()")
    changed += (
        """
int main() {
    if (ctnative_test_entry() != 0 || ctn_test_maps.empty()) { return 120; }
    static_assert(std::is_same_v<decltype(g_traceErrorCount), ctnative::nullable_scalar>);
    static_assert(std::is_same_v<decltype(g_traceErrorMessage), ctnative::nullable_scalar>);
    std::weak_ptr recorder_owner_lifetime = g_console;
    auto error = g_console->error;
    auto first = g_host;
    auto table = first->slot;
    auto get = table->m_get;
    auto set = table->m_set;
    auto remove = table->m_remove;
    auto element = g_element;
    std::weak_ptr first_lifetime = first;
    std::weak_ptr table_lifetime = table;
    std::weak_ptr element_lifetime = element;
    const auto initial_count = ctnative::global_number(g_traceErrorCount).value();
    g_host.reset(); g_element.reset();
    first.reset();
    if (!first_lifetime.expired() || table_lifetime.expired()) { return 121; }
    table.reset();
    if (!table_lifetime.expired() || ctn_test_maps[0].expired()) { return 122; }
    for (int i = 0; i < 1024; ++i) {
"""
        + body
        + """    }
    if (ctnative::global_number(g_traceErrorCount).value() != initial_count + 2048 ||
        ctnative::global_number(g_traceErrorMessage).value() != 0) { return 125; }
"""
    )
    for binding, value in sorted(values.items()):
        if binding not in {"traceErrorCount", "traceErrorMessage"}:
            changed += f"    if (ctnative::global_number(g_{binding}).value() != {value}) {{ return 126; }}\n"
    changed += """    set(element, "bs.alert", 42);
    const auto next = ctn_test_maps.size();
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() <= next || element == g_element ||
        !recorder_owner_lifetime.expired() ||
        ctn_test_maps[0].lock() == ctn_test_maps[next].lock() ||
        ctnative::global_number(get(element, "bs.alert")).value() != 42 ||
        !ctnative::scalar_strict_equal(get(g_element, "bs.alert"), ctnative::nullable_scalar::null()) ||
        !ctnative::scalar_strict_equal(g_host->slot->m_get(element, "bs.alert"),
                                      ctnative::nullable_scalar::null())) { return 127; }
    // Old and new callable families must both use the current scalar globals.
    set(element, "bs.collapse", 99);
    if (ctnative::global_number(g_traceErrorCount).value() != initial_count + 1 ||
        ctnative::global_number(g_traceErrorMessage).value() != 1) { return 128; }
    g_host->slot->m_remove(g_element, "bs.collapse");
    g_host->slot->m_set(g_element, "bs.alert", 7);
    g_host->slot->m_set(g_element, "bs.collapse", 9);
    if (ctnative::global_number(g_traceErrorCount).value() != initial_count + 2 ||
        ctnative::global_number(g_traceErrorMessage).value() != 1) { return 129; }
    element.reset();
    get = {};
    if (ctn_test_maps[0].expired() || element_lifetime.expired()) { return 130; }
    set = {};
    if (ctn_test_maps[0].expired() || element_lifetime.expired()) { return 131; }
    remove = {};
    if (!ctn_test_maps[0].expired() || !element_lifetime.expired() ||
        ctn_test_maps[next].expired()) { return 132; }
    g_host.reset(); g_element.reset();
    for (const auto & map : ctn_test_maps) {
        if (!map.expired()) { return 133; }
    }
    for (const auto & object : ctn_test_objects) {
        if (!object.expired()) { return 134; }
    }
    std::weak_ptr current_recorder_owner = g_console;
    auto current_error = g_console->error;
    g_console.reset();
    if (!current_recorder_owner.expired()) { return 135; }
    // Both detached closures outlive their owners and update the current globals.
    error("Bootstrap doesn't allow more than one instance per element. Bound instance: bs.alert.");
    if (ctnative::global_number(g_traceErrorCount).value() != initial_count + 3 ||
        ctnative::global_number(g_traceErrorMessage).value() != 1) { return 136; }
    error = {};
    current_error("a different future message");
    if (ctnative::global_number(g_traceErrorCount).value() != initial_count + 4 ||
        ctnative::global_number(g_traceErrorMessage).value() != 0) { return 137; }
    current_error = {};
    return 0;
}
"""
    if entry_objects:
        alias_reset = " g_alias.reset();" if re.search(r"\bg_alias\b", cpp) else ""
        changed = (
            changed.replace(
                "g_host.reset(); g_element.reset();",
                "g_host.reset(); g_element.reset(); g_other.reset(); g_absent.reset();\n"
                "    g_instance.reset();" + alias_reset,
            )
            .replace("ctnative::scalar_strict_equal(get(", "ctnative::object_strict_equal(get(")
            .replace(
                "ctnative::scalar_strict_equal(g_host->slot->m_get(",
                "ctnative::object_strict_equal(g_host->slot->m_get(",
            )
            .replace(
                'ctnative::global_number(get(element, "bs.alert")).value() != 42',
                '!ctnative::object_strict_equal(get(element, "bs.alert"), js_num{42})',
            )
        )
        changed = re.sub(
            r"\b(set|m_set)\(([^;\n]+), ([0-9]+)\)",
            r"\1(\2, js_num{\3})",
            changed,
        )
        signature = (
            "std::function<ctnative::object_value("
            "std::shared_ptr<ctnative::identity_object>, std::string)>"
        )
        changed = changed.replace(
            "auto get = table->m_get;",
            "auto get = table->m_get;\n"
            f"    static_assert(std::is_same_v<decltype(get), {signature}>);",
        )
    return changed


def template_lifetime(
    args, cpp, name, mode, expected, compilers, nm, child, recorder_values=None, entry_objects=False
):
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    source.write_text(
        recorder_lifetime_cpp(cpp, recorder_values, entry_objects)
        if recorder_values is not None
        else template_lifetime_cpp(cpp, child)
    )
    for index, compiler in enumerate(compilers):
        binary = source.with_suffix(f".{index}").resolve()
        host.run([compiler, *owned.FLAGS, str(source), "-o", str(binary)])
        if owned.VM.search(host.run([nm, "-C", str(binary)]).stdout):
            raise RuntimeError(f"{name}/{mode}: linked VM symbols in template lifetime")
        if host.run([str(binary)]).stdout != expected * 2:
            raise RuntimeError(f"{name}/{mode}: future template lifetime mismatch")
    binary = source.with_suffix(".sanitized").resolve()
    host.run(
        [
            compilers[1],
            *owned.FLAGS,
            "-O1",
            "-g",
            "-fno-omit-frame-pointer",
            "-fsanitize=address,undefined",
            "-fsanitize-address-use-after-scope",
            str(source),
            "-o",
            str(binary),
        ]
    )
    result = subprocess.run(
        [str(binary)],
        capture_output=True,
        text=True,
        timeout=60,
        env=dict(
            os.environ,
            ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
            UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
        ),
    )
    if result.returncode or result.stdout != expected * 2 or result.stderr:
        raise RuntimeError(
            f"{name}/{mode}: sanitized template lifetime failed\n{result.stdout}{result.stderr}"
        )


def check_recorders(args, node, reference, compilers, nm):
    cases = {**recorder_cases(), **recorder_refusals(), **snapshot_cases(), **template_cases()}
    observations = 0

    def observe(name, source, values):
        js = args.work / f"{name}-observed.js"
        js.write_text(source)
        expected = "".join(f"{key}={value}\n" for key, value in sorted(values.items()))
        result = host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), json.dumps(sorted(values))])
        if result.stdout != expected or result.stderr:
            raise RuntimeError(f"{name}: typed Node observation changed")
        result = host.run([str(reference), str(js)])
        kinds = f"({len(values)} number, 0 boolean, 0 string, 0 null, 0 undefined)"
        if result.stdout != expected or kinds not in result.stderr:
            raise RuntimeError(f"{name}: typed VM observation changed")
        return expected

    for name, row in cases.items():
        observe(name, row["source"], row["values"])
        observations += len(row["values"])
        if row["admitted"]:
            values = dict(row["values"], traceFuture=1)
            future = row["source"] + (
                recorder_future_source(row.get("entry_objects", False))
                if row.get("recorder")
                else (
                    template_future_source(row["child"])
                    if "child" in row
                    else """var traceFuture = 1;
for (let i = 0; i < 1024; ++i) {
    if (host.slot.get() !== 42) { traceFuture = 0; }
}
"""
                )
            )
            if row.get("recorder"):
                values.update(traceErrorCount=values["traceErrorCount"] + 2048, traceErrorMessage=0)
            observe(name + "-future", future, values)
            observations += len(row["values"]) + 1
    mutations = [*recorder_mutations(), *template_mutations()]
    for name, source, values in mutations:
        observe(name, source, values)
        observations += len(values)

    for name, row in cases.items():
        # prepare appends one newline; compile the exact pinned source bytes.
        js, ir, functions = boundary.prepare(args, name, row["source"].removesuffix("\n"))
        assert js.read_bytes() == row["source"].encode(), name
        assert functions == row["functions"], name

        def observed_contract(subject, label):
            config = contract(args, subject, label)
            value = json.loads(config.read_text())
            value.update(
                initial_intrinsics=["Map", "Array"],
                observations=sorted(row["values"]),
                undefined_bindings=["undefined"] if row.get("fixed_undefined") else [],
            )
            config.write_text(json.dumps(value, indent=2) + "\n")
            return config

        config = observed_contract(ir, name)
        for policy, options in (("default", ""), ("disabled", "optimize=false")):
            label = name + "-" + policy
            if row.get("recorder") and row["admitted"]:
                for steps in ((0, 32, 100_000) if row.get("fixed_undefined") else (0, 32)):
                    refused = methods.refused(
                        args,
                        ir,
                        label + f"-budget-{steps}",
                        config,
                        options=options + f" host-max-steps={steps}",
                        reason="budget" if steps < 100_000 else None,
                        admitted=0,
                    )
                    check_call_preservation(ir.read_text(), refused.read_text(), label)
            # Keep exact sources; the longer outer probe needs this budget,
            # and unsafe-key controls must finish their semantic refusal.
            if "max_steps" in row:
                options += f" host-max-steps={row['max_steps']}"
            output = owned.lower(args, ir, label, config, options=options, cleanup=row["admitted"])
            methods.census(output, functions, label, admitted=functions if row["admitted"] else 0)
            if not row["admitted"]:
                if not row.get("fixed_undefined"):
                    check_call_preservation(ir.read_text(), output.read_text(), label)
                if (
                    "child" in row or row.get("recorder")
                ) and "budget exhausted" in output.read_text():
                    raise RuntimeError(f"{label}: source refusal exhausted its proof budget")
            elif policy == "default":
                default = output
            elif output.read_text() != default.read_text():
                raise RuntimeError(f"{name}: snapshot proof depends on optimization policy")
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
            fresh = observed_contract(forged, label + "-fresh")
            checked = owned.lower(
                args,
                forged,
                label + "-fresh",
                fresh,
                options=options,
                cleanup=row["admitted"],
            )
            methods.census(checked, functions, label, admitted=functions if row["admitted"] else 0)
            if row.get("fixed_undefined") and not row["admitted"]:
                # Successful ownership prepares calls even when native emission
                # refuses. Preserve every call and the exact entry method order.
                targets = {"set": "fn$4", "get": "fn$5", "remove": "fn$6"}
                expected = [
                    targets[method]
                    for method in re.findall(r"host\.slot\.(set|get|remove)\(", row["source"])
                ]
                for subject in (output, checked):
                    text = subject.read_text()
                    calls = re.findall(
                        r"ctjs\.call_direct @([-\w.$]+)\([^\n]+\) "
                        r"\{[^}\n]*ctnative\.stored_call = 1 : i32",
                        text,
                    )
                    if (
                        "ctnative.host_owner_proved = true" not in text
                        or len(source_calls(text)) != len(source_calls(ir.read_text()))
                        or calls != expected
                    ):
                        raise RuntimeError(f"{label}: declared undefined lost owned source calls")
                check_call_preservation(output.read_text(), checked.read_text(), label + "-fresh")
            elif not row["admitted"]:
                check_call_preservation(forged.read_text(), checked.read_text(), label + "-fresh")
            elif comparable_provenance(
                host.run([args.translate, "--mlir-to-cpp", str(checked)]).stdout, forged
            ) != comparable_provenance(
                host.run([args.translate, "--mlir-to-cpp", str(output)]).stdout, ir
            ):
                raise RuntimeError(f"{label}: forged reports changed snapshot C++")
        if row["admitted"]:
            expected = "".join(f"{key}={value}\n" for key, value in sorted(row["values"].items()))
            owned.standalone(args, default, name, expected, compilers, nm)
            for mode in ("explicit", "deduced"):
                cpp = (args.work / f"{name}.{mode}.cpp").read_text()
                if row.get("entry_objects"):
                    entry = re.search(r"\bmain\(\)\s*\{(.*?)^\}", cpp, re.M | re.S)
                    if not entry:
                        raise RuntimeError(f"{name}/{mode}: missing original entry")
                    methods_by_value = dict(
                        re.findall(
                            r"(\w+)\s*=\s*ctnative::method_get<&[^>\n]+::m_(\w+)>\(", entry[1]
                        )
                    )
                    calls = re.findall(r"ctnative::invoke_callable\((\w+)([^;\n]*)\);", entry[1])
                    sequence = [
                        methods_by_value[callee]
                        for callee, _ in calls
                        if callee in methods_by_value
                    ]
                    if sequence != re.findall(
                        r"host\.slot\.(set|get|remove)\(", row["source"]
                    ) or len(
                        re.findall(r"ctnative::object_get_field_76616c7565\(", cpp)
                    ) != row.get(
                        "field_reads", 5
                    ):
                        raise RuntimeError(
                            f"{name}/{mode}: changed live entry calls or field reads"
                        )
                if "child" in row or row.get("recorder"):
                    template_lifetime(
                        args,
                        cpp,
                        name,
                        mode,
                        expected,
                        compilers,
                        nm,
                        row.get("child", False),
                        recorder_values=row["values"] if row.get("recorder") else None,
                        entry_objects=row.get("entry_objects", False),
                    )
                else:
                    methods.lifetime(args, cpp, name, mode, expected, compilers[1])
    native = sum(row["admitted"] for row in cases.values())
    print(
        f"recorder boundary: {native} native programs, {len(cases) - native} refusals, "
        f"{observations} typed observations, {len(mutations)} distinguishing mutations"
    )
