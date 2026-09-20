from .harness_scalar_maps import (
    LEAF_COMPARISON_CASES,
    inline_runtime,
    LEAF_OBJECT_FIELDS,
    host,
    leaf_object_sources,
    leaf_readback_sources,
    object_argument_cases,
    os,
    owned,
    re,
    subprocess,
)


def leaf_object_observer_source(source, name):
    # Only this independent reference observer replaces Map.set. The compiled
    # source and its standard intrinsic contract retain their original bytes.
    observed = source + """
(function() {
    const seen = [];
    const original = Map.prototype.set;
    Map.prototype.set = function(key, value) {
        seen.push(value); return original.call(this, key, value);
    };
    const before = host.slot.size();
    host.slot.set('leaf-observer-future-key');
    host.slot.set('leaf-observer-future-key');
    Map.prototype.set = original;
    const first = seen[0], second = seen[1];
    trace = 0;
"""
    checks = ["seen.length === 2", "first !== second", "host.slot.size() === before + 1"]
    style = LEAF_OBJECT_FIELDS.get(name)
    if style:
        checks.extend(
            [
                "first.value === " + ("before" if style == "scalar" else "1"),
                "second.value === " + ("before + 1" if style == "scalar" else "1"),
            ]
        )
    if style == "scalar":
        checks.extend(
            [
                "first.flag === false && second.flag === false",
                "first.empty === null && second.empty === null",
                "first.absent === undefined && second.absent === undefined",
            ]
        )
    for index, check in enumerate(checks):
        observed += f"    if ({check}) {{ trace = trace + {1 << index}; }}\n"
    return observed + "})();\n", (1 << len(checks)) - 1


def instrument_leaf_objects(cpp, allocations=1):
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", inline_runtime(cpp))
    if count != 1:
        raise RuntimeError("leaf object observer needs exactly one entry")
    changed = (
        "#include <memory>\n#include <type_traits>\n#include <vector>\n"
        "static std::vector<std::weak_ptr<const void>> ctn_test_maps;\n"
        "static std::vector<std::weak_ptr<const void>> ctn_test_objects;\n"
        "template <class T> std::shared_ptr<T> ctn_test_make_leaf() {\n"
        "    auto made = std::make_shared<T>();\n"
        "    ctn_test_objects.emplace_back(made); return made;\n}\n" + changed
    )
    changed, count = re.subn(
        r"return std::make_shared<(map_storage<K, V>|number_map<K>)>\(\);",
        lambda match: "auto made = std::make_shared<" + match[1] + ">(); "
        "ctn_test_maps.emplace_back(made); return made;",
        changed,
    )
    if count != 2 or changed.count("std::make_shared<ctnative::identity_object>()") != allocations:
        raise RuntimeError("leaf object observer lost its Map and setter-local allocation sites")
    return changed.replace(
        "std::make_shared<ctnative::identity_object>()",
        "ctn_test_make_leaf<ctnative::identity_object>()",
    )


def leaf_field_failures(variable, style, expected):
    if not style:
        return []
    scalar = "ctnative::nullable_scalar::kind::"
    checks = [
        f"{variable}->field_76616c7565.tag != {scalar}number",
        f"{variable}->field_76616c7565.value != {expected}",
    ]
    if style == "scalar":
        checks.extend(
            [
                f"{variable}->field_666c6167.tag != {scalar}boolean",
                f"{variable}->field_666c6167.value != 0",
                f"{variable}->field_656d707479.tag != {scalar}null",
                f"{variable}->field_616273656e74.tag != {scalar}undefined",
            ]
        )
    return checks


def leaf_object_identity_cpp(cpp, name):
    changed = instrument_leaf_objects(cpp)
    changed += r"""
int main() {
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1) { return 90; }
    const auto before = g_host->slot->m_size();
    const auto count = ctn_test_objects.size();
    g_host->slot->m_set(std::string{"leaf-observer-future-key"});
    auto first = std::static_pointer_cast<const ctnative::identity_object>(ctn_test_objects.back().lock());
    g_host->slot->m_set(std::string{"leaf-observer-future-key"});
    auto second = std::static_pointer_cast<const ctnative::identity_object>(ctn_test_objects.back().lock());
    if (ctn_test_objects.size() != count + 2 || !first || !second || first == second ||
        g_host->slot->m_size() != before + 1) { return 91; }
    g_host.reset();
    if (!ctn_test_maps[0].expired() || ctn_test_objects[count].expired() ||
        ctn_test_objects[count + 1].expired()) { return 92; }
"""
    style = LEAF_OBJECT_FIELDS.get(name)
    checks = leaf_field_failures("first", style, "before" if style == "scalar" else "1")
    checks += leaf_field_failures("second", style, "before + 1" if style == "scalar" else "1")
    if checks:
        changed += "    if (" + " ||\n        ".join(checks) + ") { return 93; }\n"
    return changed + r"""
    first.reset();
    second.reset();
    for (const auto & object : ctn_test_objects) {
        if (!object.expired()) { return 94; }
    }
    return 0;
}
"""


def check_leaf_object_calls(cpp, name, mode):
    source = leaf_object_sources()[name][0]
    entry = re.search(r"\bmain\(\)\s*\{(.*?)^\}", cpp, re.M | re.S)
    if not entry or "std::function<::js_num(std::string)>" not in cpp:
        raise RuntimeError(f"{name}/{mode}: missing numeric leaf setter ABI")
    methods_by_value = dict(
        re.findall(r"(\w+)\s*=\s*ctnative::method_get<&[^>\n]+::m_(\w+)>\(", entry[1])
    )
    calls = re.findall(r"ctnative::invoke_callable\((\w+)([^;\n]*)\);", entry[1])
    sequence = [methods_by_value.get(callee) for callee, _ in calls]
    expected = ["set", "set", "set", "size"]
    if name == "leaf_object_lifetime":
        expected = ["set", "set", "set", "erase", "size"]
    elif name == "leaf_object_identity_repair":
        expected = ["size", "set"]
    if sequence != expected:
        raise RuntimeError(f"{name}/{mode}: changed live leaf-method order")
    for method in ("set", "get", "has", "delete"):
        original = len(re.findall(rf"\bstate\.{method}\(", source))
        lowered = len(re.findall(rf"\bctnative::map_{method}(?:_\w+)?(?:<[^>]+>)?\(", cpp))
        if lowered != original:
            raise RuntimeError(f"{name}/{mode}: changed the {original} live Map.{method} calls")
    if name.endswith("_repair") and name != "leaf_object_identity_repair":
        return
    style = LEAF_OBJECT_FIELDS.get(name)
    writes = 6 if style == "scalar" else 1 if style else 0
    if (
        "std::shared_ptr<ctnative::map_storage<std::string, ctnative::object_value>>" not in cpp
        or cpp.count("std::make_shared<ctnative::identity_object>()") != 1
        or len(re.findall(r"ctnative::object_set_field_[0-9a-f]+\(", cpp)) != writes
    ):
        raise RuntimeError(
            f"{name}/{mode}: lost the leaf owner, exact payload schema or field writes"
        )


def check_leaf_readback_calls(cpp, name, mode):
    source = leaf_readback_sources()[name][0]
    body = source.split("set(key", 1)[1].split("\n", 1)[0]
    allocations = body.count("{") - 1
    params = (
        "std::string, ::js_num"
        if name.startswith("local_field_readback_lifetime")
        else "std::string"
    )
    if (
        f"std::function<::js_num({params})>" not in cpp
        or "std::shared_ptr<ctnative::map_storage<std::string, ctnative::object_value>>" not in cpp
        or cpp.count("std::make_shared<ctnative::identity_object>()") != allocations
    ):
        raise RuntimeError(f"{name}/{mode}: lost fresh leaf allocations or numeric published ABI")
    for method in ("set", "get", "has", "delete"):
        original = len(re.findall(rf"\bstate\.{method}\(", source))
        lowered = len(re.findall(rf"\bctnative::map_{method}(?:_\w+)?(?:<[^>]+>)?\(", cpp))
        if lowered != original:
            raise RuntimeError(f"{name}/{mode}: changed the {original} live Map.{method} calls")
    fields = list(re.finditer(r"\b(?:item|saved)\.\w+\b", body))
    reads = sum(not re.match(r"\s*=(?!=)", body[match.end() :]) for match in fields)
    writes = sum(bool(re.match(r"\s*=(?!=)", body[match.end() :])) for match in fields)
    writes += len(re.findall(r"\bvalue:", body))
    if (
        len(re.findall(r"ctnative::object_get_field_[0-9a-f]+\(", cpp)) != reads
        or len(re.findall(r"ctnative::object_set_field_[0-9a-f]+\(", cpp)) != writes
    ):
        raise RuntimeError(
            f"{name}/{mode}: changed the {reads} live field reads or {writes} writes"
        )
    entry = re.search(r"\bmain\(\)\s*\{(.*?)^\}", cpp, re.M | re.S)
    if not entry:
        raise RuntimeError(f"{name}/{mode}: missing native readback entry")
    methods_by_value = dict(
        re.findall(r"(\w+)\s*=\s*ctnative::method_get<&[^>\n]+::m_(\w+)>\(", entry[1])
    )
    calls = re.findall(r"ctnative::invoke_callable\((\w+)([^;\n]*)\);", entry[1])
    expected = (
        ["size", "set", "set", "set"] if name == "local_identity_repeated_keys" else ["size", "set"]
    )
    if name == "local_field_export_repair":
        expected += ["size"]
    if [methods_by_value.get(callee) for callee, _ in calls] != expected:
        raise RuntimeError(f"{name}/{mode}: changed the live readback method order")
    if name in LEAF_COMPARISON_CASES:
        comparisons = re.findall(r"ctnative::object_strict_equal\((\w+), (\w+)\)", cpp)
        reads = re.findall(r"\b(\w+)\s*=\s*ctnative::map_get_present_identity\(", cpp)
        created = re.findall(r"\b(\w+)\s*=\s*std::make_shared<ctnative::identity_object>\(\)", cpp)
        if (
            len(comparisons) != 1
            or len(reads) != 1
            or comparisons[0][0] != reads[0]
            or comparisons[0][1] not in created
            or ("distinct" in name and comparisons[0][1] != created[-1])
        ):
            raise RuntimeError(
                f"{name}/{mode}: strict comparison lost its live saved/fresh operands"
            )


def comparison_identity_observer_source(source, name):
    # These future calls remain separate from the byte-preserved compiled
    # source. Retaining Map payloads is observation, never production storage.
    historical = name.startswith("historical_")
    writes = 2 if historical else 1
    result = leaf_readback_sources()[name][2]
    observed = source + """
(function() {
    const seen = [], results = [];
    const original = Map.prototype.set;
    Map.prototype.set = function(key, value) {
        seen.push(value); return original.call(this, key, value);
    };
    const setter = host.slot.set, size = host.slot.size;
    host = {};
    results.push(setter('future-key'));
    results.push(setter('future-key'));
    results.push(setter('other-key'));
    results.push(setter('other-key'));
    Map.prototype.set = original;
    trace = 0;
"""
    checks = [
        f"seen.length === {4 * writes}",
        f"results.length === 4 && results.every(value => value === {result})",
        f"size() === {0 if historical else 3}",
    ]
    for left in range(4 * writes):
        for right in range(left + 1, 4 * writes):
            checks.append(f"seen[{left}] !== seen[{right}]")
    if historical:
        checks.append("seen.every(value => value.value === 1)")
    # Avoid bitwise accumulation: the full pairwise historical census has
    # more than 31 independent checks, and JavaScript bitwise values are Int32.
    for check in checks:
        observed += f"    if ({check}) {{ trace += 1; }}\n"
    return observed + "})();\n", len(checks)


def comparison_identity_cpp(cpp, name):
    historical = name.startswith("historical_")
    distinct = "distinct" in name
    allocations = 1 + historical + distinct
    result = leaf_readback_sources()[name][2]
    changed = instrument_leaf_objects(cpp, allocations=allocations)
    changed = changed.replace(
        "template <class T> std::shared_ptr<T> ctn_test_make_leaf() {",
        "static bool ctn_test_capture = false;\n"
        "static std::vector<std::shared_ptr<const void>> ctn_test_retained;\n"
        "template <class T> std::shared_ptr<T> ctn_test_make_leaf() {",
    )
    changed = changed.replace(
        "ctn_test_objects.emplace_back(made); return made;",
        "ctn_test_objects.emplace_back(made); "
        "if (ctn_test_capture) { ctn_test_retained.emplace_back(made); } return made;",
    )
    changed += r"""
int main() {
    constexpr std::size_t allocations = CTN_ALLOCATIONS;
    constexpr bool deleted = CTN_DELETED;
    constexpr js_num result = CTN_RESULT;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1 ||
        ctn_test_objects.size() != allocations) { return 120; }
    for (std::size_t index = 0; index < allocations; ++index) {
        if (ctn_test_objects[index].expired() != (deleted || index != 0)) { return 121; }
    }
    auto owner = g_host;
    auto table = owner->slot;
    auto size = table->m_size;
    auto setter = table->m_set;
    static_assert(std::is_same_v<decltype(size), std::function<js_num()>>);
    static_assert(std::is_same_v<decltype(setter), std::function<js_num(std::string)>>);
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() || ctn_test_maps[0].expired()) {
        return 122;
    }
    const std::string original(160, 'k');
    for (int call = 0; call < 128; ++call) {
        const auto count = ctn_test_objects.size();
        auto caller = original;
        if (setter(caller) != result || size() != (deleted ? 0 : 2) ||
            ctn_test_objects.size() != count + allocations) { return 123; }
        caller.assign(original.size(), 'q');
        for (std::size_t index = allocations; index < count; ++index) {
            if (!ctn_test_objects[index].expired()) { return 124; }
        }
        for (std::size_t index = 0; index < allocations; ++index) {
            if (ctn_test_objects[count + index].expired() != (deleted || index != 0)) { return 125; }
        }
    }
    ctn_test_capture = true;
    if (setter(original) != result || setter(original) != result ||
        ctn_test_retained.size() != allocations * 2) { return 126; }
    ctn_test_capture = false;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock() ||
        size() != (deleted ? 0 : 2) || g_host->slot->m_size() != (deleted ? 0 : 1)) { return 127; }
    setter = {};
    if (ctn_test_maps[0].expired()) { return 128; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired()) { return 129; }
    g_host.reset();
    std::vector<std::string> churn;
    for (int index = 0; index < 4096; ++index) { churn.emplace_back(original.size(), 'w'); }
    if (!ctn_test_maps[1].expired()) { return 130; }
    for (std::size_t left = 0; left < ctn_test_retained.size(); ++left) {
        if (!ctn_test_retained[left]) { return 131; }
        for (std::size_t right = left + 1; right < ctn_test_retained.size(); ++right) {
            if (ctn_test_retained[left] == ctn_test_retained[right]) { return 132; }
        }
        CTN_FIELD_CHECK
    }
    ctn_test_retained.clear();
    for (const auto & object : ctn_test_objects) {
        if (!object.expired()) { return 134; }
    }
    return 0;
}
"""
    fields = ""
    if historical:
        fields = (
            "const auto leaf = std::static_pointer_cast<const ctnative::identity_object>"
            "(ctn_test_retained[left]);\n"
            "        if (leaf->field_76616c7565.tag != ctnative::nullable_scalar::kind::number ||\n"
            "            leaf->field_76616c7565.value != 1) { return 133; }"
        )
    return (
        changed.replace("CTN_ALLOCATIONS", str(allocations))
        .replace("CTN_DELETED", "true" if historical else "false")
        .replace("CTN_RESULT", str(result))
        .replace("CTN_FIELD_CHECK", fields)
    )


def comparison_identity_lifetime(args, cpp, name, mode, compiler):
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    source.write_text(comparison_identity_cpp(cpp, name))
    binary = (args.work / f"{name}.{mode}.sanitized").resolve()
    host.run(
        [
            compiler,
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
    expected = f"trace={leaf_readback_sources()[name][2]}\n" * 2
    if result.returncode or result.stdout != expected:
        raise RuntimeError(
            f"{name}/{mode}: comparison identity lifetime failure (exit {result.returncode})\n"
            f"{result.stdout}{result.stderr}"
        )


def leaf_readback_lifetime(args, cpp, name, mode, compiler):
    changed = instrument_leaf_objects(cpp, allocations=2)
    changed = changed.replace(
        "template <class T> std::shared_ptr<T> ctn_test_make_leaf() {",
        "static bool ctn_test_capture_next = false;\n"
        "static std::shared_ptr<const void> ctn_test_retained;\n"
        "template <class T> std::shared_ptr<T> ctn_test_make_leaf() {",
    )
    changed = changed.replace(
        "ctn_test_objects.emplace_back(made); return made;",
        "ctn_test_objects.emplace_back(made); "
        "if (ctn_test_capture_next) { ctn_test_capture_next = false; ctn_test_retained = made; } "
        "return made;",
    )
    changed += r"""
int main() {
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1 || ctn_test_objects.size() != 2 ||
        !ctn_test_objects[0].expired() || !ctn_test_objects[1].expired()) { return 110; }
    auto owner = g_host;
    auto table = owner->slot;
    auto size = table->m_size;
    auto setter = table->m_set;
    static_assert(std::is_same_v<decltype(size), std::function<js_num()>>);
    static_assert(std::is_same_v<decltype(setter), std::function<js_num(std::string, js_num)>>);
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() || ctn_test_maps[0].expired()) {
        return 111;
    }
    const std::string original(160, 'k');
    auto caller = original;
    for (int index = 0; index < 128; ++index) {
        const auto count = ctn_test_objects.size();
        const auto expected = static_cast<js_num>(index) - 63.5;
        if (setter(caller, expected) != expected || size() != 0 || ctn_test_objects.size() != count + 2 ||
            !ctn_test_objects[count].expired() || !ctn_test_objects[count + 1].expired()) { return 112; }
        caller.assign(original.size(), index % 2 ? 'q' : 'k');
    }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock()) { return 113; }
    const auto count = ctn_test_objects.size();
    ctn_test_capture_next = true;
    if (setter(original, 41) != 41 || !ctn_test_retained || ctn_test_objects.size() != count + 2 ||
        ctn_test_objects[count].expired() || !ctn_test_objects[count + 1].expired() ||
        size() != 0 || g_host->slot->m_size() != 0) { return 114; }
    setter = {};
    if (ctn_test_maps[0].expired()) { return 115; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired()) { return 116; }
    g_host.reset();
    std::vector<std::string> churn;
    for (int index = 0; index < 4096; ++index) { churn.emplace_back(original.size(), 'w'); }
    {
        const auto kept = std::static_pointer_cast<const ctnative::identity_object>(ctn_test_retained);
        if (!ctn_test_maps[1].expired() || kept->field_76616c7565.tag !=
            ctnative::nullable_scalar::kind::number || kept->field_76616c7565.value != 41) { return 117; }
    }
    ctn_test_retained.reset();
    for (const auto & object : ctn_test_objects) {
        if (!object.expired()) { return 118; }
    }
    return 0;
}
"""
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    source.write_text(changed)
    binary = (args.work / f"{name}.{mode}.sanitized").resolve()
    host.run(
        [
            compiler,
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
    if result.returncode or result.stdout != "trace=2\n" * 2:
        raise RuntimeError(
            f"{name}/{mode}: saved leaf readback lifetime failure (exit {result.returncode})\n"
            f"{result.stdout}{result.stderr}"
        )


def leaf_object_lifetime(args, cpp, name, mode, compiler):
    changed = instrument_leaf_objects(cpp)
    changed += r"""
int main() {
    const auto fields_match = [](const std::shared_ptr<const void> & value, double expected) {
        const auto object = std::static_pointer_cast<const ctnative::identity_object>(value);
        using kind = ctnative::nullable_scalar::kind;
        return object && object->field_76616c7565.tag == kind::number &&
            object->field_76616c7565.value == expected &&
            object->field_666c6167.tag == kind::boolean && object->field_666c6167.value == 0 &&
            object->field_656d707479.tag == kind::null && object->field_616273656e74.tag == kind::undefined;
    };
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1 || ctn_test_objects.size() != 3 ||
        !ctn_test_objects[0].expired() || !fields_match(ctn_test_objects[1].lock(), 1) ||
        !ctn_test_objects[2].expired()) { return 90; }
    auto owner = g_host;
    auto table = owner->slot;
    auto size = table->m_size;
    auto setter = table->m_set;
    auto erase = table->m_erase;
    static_assert(std::is_same_v<decltype(size), std::function<js_num()>>);
    static_assert(std::is_same_v<decltype(setter), std::function<js_num(std::string)>>);
    static_assert(std::is_same_v<decltype(erase), std::function<js_num(std::string)>>);
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset();
    owner.reset();
    table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() || ctn_test_maps[0].expired()) {
        return 91;
    }
    const std::string expected(160, 'k');
    auto caller = expected;
    if (setter(caller) != 2 || !fields_match(ctn_test_objects.back().lock(), 1)) { return 92; }
    caller.assign(expected.size(), 'q');
    if (setter(expected) != 2 || ctn_test_objects.size() != 5 || !ctn_test_objects[3].expired() ||
        !fields_match(ctn_test_objects[4].lock(), 2)) { return 93; }
    // The temporary strong lock used to inspect fields must die before erase.
    if (erase(expected) != 1 ||
        !ctn_test_objects[4].expired()) { return 93; }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 || ctn_test_objects.size() != 8 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock()) { return 94; }
    for (int index = 0; index < 128; ++index) {
        const std::string key = expected + std::to_string(index);
        const auto count = ctn_test_objects.size();
        if (setter(key) != 2 || !fields_match(ctn_test_objects[count].lock(), 1)) { return 95; }
        if (setter(key) != 2 || !ctn_test_objects[count].expired() ||
            !fields_match(ctn_test_objects[count + 1].lock(), 2)) { return 95; }
        if (erase(key) != 1 ||
            !ctn_test_objects[count + 1].expired() || size() != 1 || g_host->slot->m_size() != 1) {
            return 95;
        }
    }
    if (setter(std::string{"kept"}) != 2) { return 96; }
    const auto kept = ctn_test_objects.size() - 1;
    auto saved = ctn_test_objects[kept].lock();
    setter = {};
    if (erase(std::string{"x"}) != 1 || !ctn_test_objects[1].expired()) { return 97; }
    erase = {};
    if (ctn_test_maps[0].expired() || size() != 1) { return 98; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired() ||
        ctn_test_objects[kept].expired()) { return 99; }
    g_host.reset();
    std::vector<std::string> churn;
    for (int index = 0; index < 4096; ++index) { churn.emplace_back(expected.size(), 'w'); }
    if (!ctn_test_maps[1].expired() || !fields_match(saved, 1)) { return 100; }
    saved.reset();
    for (const auto & object : ctn_test_objects) {
        if (!object.expired()) { return 101; }
    }
    return 0;
}
"""
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    source.write_text(changed)
    binary = (args.work / f"{name}.{mode}.sanitized").resolve()
    host.run(
        [
            compiler,
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
    if result.returncode or result.stdout != "trace=1\n" * 2:
        raise RuntimeError(
            f"{name}/{mode}: leaf Map/callable/object lifetime failure "
            f"(exit {result.returncode})\n"
            f"{result.stdout}{result.stderr}"
        )
