from .harness_objects import (
    instrument_leaf_objects,
)
from .harness_scalar_maps import (
    STRING_GLOBAL_LONG,
    constant_global_sources,
    host,
    leaf_absence_cases,
    leaf_absence_sources,
    leaf_clear_cases,
    leaf_clear_sources,
    numeric_entry_sources,
    os,
    owned,
    re,
    scalar_global_output,
    scalar_global_sources,
    subprocess,
)

LEAF_ABSENCE_LIFETIMES = ("local_absence_saved_undefined", "local_absence_distinct_branches_false")


def leaf_absence_observer_source(source, name):
    reseeded = name == "local_absence_saved_undefined"
    observed = source + """
(function() {
    const seen = [], results = [];
    const original = Map.prototype.set;
    Map.prototype.set = function(key, value) {
        seen.push(value); return original.call(this, key, value);
    };
    const setter = host.slot.set, size = host.slot.size;
    host = {};
    results.push(setter('future-key', false));
    results.push(setter('future-key', true));
    results.push(setter('other-key', false));
    results.push(setter('other-key', true));
    Map.prototype.set = original;
    trace = 0;
"""
    writes = 2 if reseeded else 1
    checks = [
        f"seen.length === {4 * writes}",
        "results.every(value => value === 1)",
        f"size() === {3 if reseeded else 0}",
        "seen.every(value => value.value === 1)",
    ]
    for left in range(4):
        if reseeded:
            checks.append(f"seen[{2 * left}] === seen[{2 * left + 1}]")
        for right in range(left + 1, 4):
            checks.append(f"seen[{writes * left}] !== seen[{writes * right}]")
    for check in checks:
        observed += f"    if ({check}) {{ trace += 1; }}\n"
    return observed + "})();\n", len(checks)


def leaf_absence_lifetime_cpp(cpp, name):
    reseeded = name == "local_absence_saved_undefined"
    changed = instrument_leaf_objects(cpp)
    changed += r"""
int main() {
    constexpr bool reseeded = CTN_RESEEDED;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1 ||
        ctn_test_objects.size() != 1 || ctn_test_objects[0].expired() == reseeded) { return 140; }
    auto owner = g_host;
    auto table = owner->slot;
    auto setter = table->m_set;
    auto size = table->m_size;
    static_assert(std::is_same_v<decltype(size), std::function<ctnative::js_num()>>);
    static_assert(std::is_same_v<decltype(setter), std::function<ctnative::js_num(CTN_PARAMS)>>);
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() || ctn_test_maps[0].expired()) {
        return 141;
    }
    const std::string original(160, 'k');
    for (int call = 0; call < 128; ++call) {
        auto caller = original;
        const auto before = ctn_test_objects.size();
        if (setter(caller CTN_FLAG).value() != 1 || size().value() != (reseeded ? 2 : 0) ||
            ctn_test_objects.size() != before + 1) { return 142; }
        caller.assign(original.size(), 'q');
        for (std::size_t index = 1; index < before; ++index) {
            if (!ctn_test_objects[index].expired()) { return 143; }
        }
        if (ctn_test_objects.back().expired() == reseeded) { return 144; }
        if (reseeded) {
            const auto leaf = std::static_pointer_cast<const ctnative::identity_object>(
                ctn_test_objects.back().lock());
            if (!leaf || leaf->field_76616c7565.tag != ctnative::nullable_scalar::kind::number ||
                leaf->field_76616c7565.value != 1) { return 145; }
        }
    }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock() ||
        size().value() != (reseeded ? 2 : 0) || g_host->slot->m_size().value() != (reseeded ? 1 : 0)) { return 146; }
    setter = {};
    if (ctn_test_maps[0].expired()) { return 147; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired()) { return 148; }
    for (std::size_t index = 0; index + 1 < ctn_test_objects.size(); ++index) {
        if (!ctn_test_objects[index].expired()) { return 149; }
    }
    g_host.reset();
    if (!ctn_test_maps[1].expired()) { return 150; }
    for (const auto & object : ctn_test_objects) {
        if (!object.expired()) { return 151; }
    }
    return 0;
}
"""
    return (
        changed.replace("CTN_RESEEDED", "true" if reseeded else "false")
        .replace("CTN_PARAMS", "std::string" if reseeded else "std::string, ctnative::js_boolean_t")
        .replace("CTN_FLAG", "" if reseeded else ", ctnative::js_boolean_t{call % 2 != 0}")
    )


def leaf_absence_lifetime(args, cpp, name, mode, compiler):
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    build = leaf_clear_lifetime_cpp if name in LEAF_CLEAR_LIFETIMES else leaf_absence_lifetime_cpp
    source.write_text(build(cpp, name))
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
            f"{name}/{mode}: absence lifetime failure (exit {result.returncode})\n"
            f"{result.stdout}{result.stderr}"
        )


def check_leaf_absence_calls(cpp, name, mode):
    source = {**leaf_absence_sources(), **leaf_clear_sources()}[name][0]
    params = (
        "std::string, ctnative::js_boolean_t"
        if "set(key, flag)" in source
        else "std::string, std::string" if "set(key, other)" in source else "std::string"
    )
    if f"std::function<ctnative::js_num({params})>" not in cpp:
        raise RuntimeError(f"{name}/{mode}: absence changed the numeric published ABI")
    allocations = source.count("{value:") + source.count("const item = {};")
    if cpp.count("std::make_shared<ctnative::identity_object>()") != allocations or (
        allocations
        and "std::shared_ptr<ctnative::map_storage<std::string, ctnative::object_value>>" not in cpp
    ):
        raise RuntimeError(f"{name}/{mode}: absence erased an object allocation or owning Map")
    if len(re.findall(r"ctnative::object_set_field_[0-9a-f]+\(", cpp)) != source.count("value:"):
        raise RuntimeError(f"{name}/{mode}: absence erased an original numeric field write")
    case = {**leaf_absence_cases(), **leaf_clear_cases()}.get(name)
    for method in ("set", "get", "has", "delete", "clear"):
        original = len(re.findall(rf"\b(?:state|alias)\.{method}\(", source))
        if case and case["raw_calls"] != case["prepared_calls"]:
            collapsed = "has" if name.endswith("_repair") else "delete"
            if method == collapsed:
                original -= 1
        lowered = len(re.findall(rf"\bctnative::map_{method}(?:_\w+)?(?:<[^>]+>)?\(", cpp))
        if lowered != original:
            raise RuntimeError(
                f"{name}/{mode}: changed {original} prepared Map.{method} calls to {lowered}"
            )
    entry = re.search(r"\bmain\(\)\s*\{(.*?)^\}", cpp, re.M | re.S)
    if not entry:
        raise RuntimeError(f"{name}/{mode}: missing native absence entry")
    methods_by_value = dict(
        re.findall(r"(\w+)\s*=\s*ctnative::method_get<&[^>\n]+::m_(\w+)>\(", entry[1])
    )
    calls = re.findall(r"ctnative::invoke_callable\((\w+)([^;\n]*)\);", entry[1])
    if [methods_by_value.get(callee) for callee, _ in calls] != ["size", "set"]:
        raise RuntimeError(f"{name}/{mode}: absence changed published method call order")


def primitive_absence_observer_source(source):
    return source + """
(function() {
    const get = host.slot.get, set = host.slot.set, size = host.slot.size;
    let observed = trace === 2 ? 1 : 0;
    if (get() === void 0) { observed += 2; }
    if (size() === 1) { observed += 4; }
    if (set('') === 2) { observed += 8; }
    if (set(void 0) === 2) { observed += 16; }
    if (set('future-key') === 3) { observed += 32; }
    if (get() === void 0) { observed += 64; }
    if (size() === 2) { observed += 128; }
    trace = observed;
})();
"""


def primitive_absence_cpp(cpp):
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", cpp)
    if count != 1:
        raise RuntimeError("primitive absence observer needs exactly one entry")
    return changed + r"""
int main() {
    if (ctnative_test_entry() != 0) { return 160; }
    const auto get = g_host->slot->m_get;
    const auto set = g_host->slot->m_set;
    const auto size = g_host->slot->m_size;
    using text = ctnative::nullable_string;
    if (get().tag != text::kind::undefined || size().value() != 1 ||
        set(text{std::string{}}).value() != 2 || set(text{}).value() != 2 ||
        set(text{std::string{"future-key"}}).value() != 3 ||
        get().tag != text::kind::undefined || size().value() != 2) { return 161; }
    return 0;
}
"""


LEAF_CLEAR_LIFETIMES = (
    "local_clear_saved_field",
    "local_clear_saved_undefined",
    "local_clear_both_branches_false",
)


def leaf_clear_observer_source(source, name):
    reseeded = name == "local_clear_saved_undefined"
    observed = source + """
(function() {
    const seen = [], results = [], sizes = [];
    const originalSet = Map.prototype.set, originalClear = Map.prototype.clear;
    Map.prototype.set = function(key, value) {
        seen.push(value); return originalSet.call(this, key, value);
    };
    Map.prototype.clear = function() {
        const result = originalClear.call(this);
        sizes.push(this.size);
        return result;
    };
    const setter = host.slot.set, size = host.slot.size;
    host = {};
    for (let call = 0; call < 4; ++call) {
        try { results.push(setter(call < 2 ? 'future-key' : 'other-key', call % 2 !== 0)); }
        catch (error) { results.push(-1); }
    }
    Map.prototype.set = originalSet;
    Map.prototype.clear = originalClear;
    trace = 0;
"""
    writes = 2 if reseeded else 1
    checks = [
        f"seen.length === {4 * writes}",
        "results.every(value => value === 1)",
        f"size() === {int(reseeded)}",
        "seen.every(value => value.value === 1)",
        "sizes.length === 4",
        "sizes.every(value => value === 0)",
    ]
    for left in range(4):
        if reseeded:
            checks.append(f"seen[{2 * left}] === seen[{2 * left + 1}]")
        for right in range(left + 1, 4):
            checks.append(f"seen[{writes * left}] !== seen[{writes * right}]")
    for check in checks:
        observed += f"    if ({check}) {{ trace += 1; }}\n"
    return observed + "})();\n", len(checks)


def leaf_clear_lifetime_cpp(cpp, name):
    reseeded = name == "local_clear_saved_undefined"
    branch = name == "local_clear_both_branches_false"
    changed = instrument_leaf_objects(cpp)
    # The observer retains exactly the last leaf. All earlier leaves still
    # have only the emitted program's owners, so clearing and overwrite must
    # reclaim them. The final observed leaf outlives both Maps independently.
    changed = changed.replace(
        "static std::vector<std::weak_ptr<const void>> ctn_test_objects;",
        "static std::vector<std::weak_ptr<const void>> ctn_test_objects;\n"
        "static std::shared_ptr<const void> ctn_test_retained;\n"
        "static bool ctn_test_keep_leaf = false;",
    )
    changed = changed.replace(
        "ctn_test_objects.emplace_back(made); return made;",
        "ctn_test_objects.emplace_back(made);\n"
        "    if (ctn_test_keep_leaf) { ctn_test_retained = made; }\n"
        "    return made;",
    )
    changed += r"""
int main() {
    constexpr bool reseeded = CTN_RESEEDED;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1 ||
        ctn_test_objects.size() != 1 || ctn_test_objects[0].expired() == reseeded) { return 160; }
    auto owner = g_host;
    auto table = owner->slot;
    auto setter = table->m_set;
    auto size = table->m_size;
    static_assert(std::is_same_v<decltype(size), std::function<ctnative::js_num()>>);
    static_assert(std::is_same_v<decltype(setter), std::function<ctnative::js_num(CTN_PARAMS)>>);
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() || ctn_test_maps[0].expired()) {
        return 161;
    }
    const std::string original(160, 'k');
    ctn_test_keep_leaf = true;
    for (int call = 0; call < 128; ++call) {
        auto caller = original;
        if (call % 2) { caller += 'r'; }
        const auto before = ctn_test_objects.size();
        if (setter(caller CTN_FLAG).value() != 1 || size().value() != (reseeded ? 1 : 0) ||
            ctn_test_objects.size() != before + 1) { return 162; }
        caller.assign(original.size(), 'q');
        for (std::size_t index = 0; index < before; ++index) {
            if (!ctn_test_objects[index].expired()) { return 163; }
        }
        const auto leaf = std::static_pointer_cast<const ctnative::identity_object>(ctn_test_retained);
        if (!leaf || ctn_test_objects.back().expired() ||
            leaf->field_76616c7565.tag != ctnative::nullable_scalar::kind::number ||
            leaf->field_76616c7565.value != 1) { return 164; }
    }
    ctn_test_keep_leaf = false;
    const auto retained_index = ctn_test_objects.size() - 1;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock() ||
        size().value() != (reseeded ? 1 : 0) || g_host->slot->m_size().value() != (reseeded ? 1 : 0)) { return 165; }
    setter = {};
    if (ctn_test_maps[0].expired()) { return 166; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired() ||
        ctn_test_objects[retained_index].expired()) { return 167; }
    g_host.reset();
    if (!ctn_test_maps[1].expired()) { return 168; }
    for (std::size_t index = 0; index < ctn_test_objects.size(); ++index) {
        if (ctn_test_objects[index].expired() != (index != retained_index)) { return 169; }
    }
    {
        const auto leaf = std::static_pointer_cast<const ctnative::identity_object>(ctn_test_retained);
        if (!leaf || leaf->field_76616c7565.tag != ctnative::nullable_scalar::kind::number ||
            leaf->field_76616c7565.value != 1) { return 170; }
    }
    ctn_test_retained.reset();
    if (!ctn_test_objects[retained_index].expired()) { return 171; }
    return 0;
}
"""
    return (
        changed.replace("CTN_RESEEDED", "true" if reseeded else "false")
        .replace("CTN_PARAMS", "std::string, ctnative::js_boolean_t" if branch else "std::string")
        .replace("CTN_FLAG", ", ctnative::js_boolean_t{call % 2 != 0}" if branch else "")
    )


NUMERIC_ENTRY_LIFETIMES = (
    "local_numeric_saved_lifetime",
    "local_numeric_branch_lifetime",
    "scalar_saved_branch_lifetime",
    "scalar_alias_branch_lifetime",
    "constant_branch_lifetime",
    "constant_boolean_branch_lifetime",
    "constant_string_branch_lifetime",
)


def check_numeric_entry_calls(cpp, name, mode):
    source = {**numeric_entry_sources(), **scalar_global_sources(), **constant_global_sources()}[
        name
    ][0]
    entry = re.search(r"\bmain\(\)\s*\{(.*?)^\}", cpp, re.M | re.S)
    if not entry:
        raise RuntimeError(f"{name}/{mode}: missing numeric entry")
    params = (
        "std::string, ctnative::js_num, ctnative::js_boolean_t"
        if "set(key, value, flag)" in source
        else (
            "std::string, ctnative::js_num"
            if "set(key, value)" in source
            else (
                "ctnative::js_num"
                if name
                in {
                    "local_add_result_key",
                    "local_numeric_nested_key",
                    "local_numeric_nan_key",
                    "local_clear_zero_literal_repair",
                    "local_clear_zero_size_key",
                    "scalar_result_key",
                }
                else "std::string"
            )
        )
    )
    result = (
        "ctnative::js_boolean_t" if name == "constant_boolean_saved_result" else "ctnative::js_num"
    )
    if f"std::function<{result}({params})>" not in cpp:
        raise RuntimeError(
            f"{name}/{mode}: arithmetic changed the independently typed callable ABI"
        )
    if name == "scalar_result_key" and (
        "std::shared_ptr<ctnative::map_storage<double, ctnative::object_value>>" not in cpp
    ):
        raise RuntimeError(
            f"{name}/{mode}: scalar argument carrier changed the independently Number Map key"
        )
    allocations = source.count("const item = {};") + source.count("{value:")
    if cpp.count("std::make_shared<ctnative::identity_object>()") != allocations:
        raise RuntimeError(f"{name}/{mode}: arithmetic erased a real leaf allocation")
    for method in ("set", "get", "has", "delete", "clear"):
        original = len(re.findall(rf"\bstate\.{method}\(", source))
        lowered = len(re.findall(rf"\bctnative::map_{method}(?:_\w+)?(?:<[^>]+>)?\(", cpp))
        if original != lowered:
            raise RuntimeError(
                f"{name}/{mode}: changed {original} evaluated Map.{method} calls to {lowered}"
            )
    methods_by_value = dict(
        re.findall(r"(\w+)\s*=\s*ctnative::method_get<&[^>\n]+::m_(\w+)>\(", entry[1])
    )
    calls = re.findall(r"ctnative::invoke_callable\((\w+)([^;\n]*)\);", entry[1])
    expected = re.findall(r"host\.slot\.(size|set)\(", source)
    if name == "local_numeric_nan_key":
        expected = ["size", "size", "size", "set", "size", "size", "set"]
    if [methods_by_value.get(callee) for callee, _ in calls] != expected:
        raise RuntimeError(f"{name}/{mode}: changed evaluated published call order: {calls}")
    # Native arithmetic must still consume evaluated operands. Every original
    # source binary has a corresponding entry expression, not a trace constant.
    source_entry = source.rsplit("});\n", 1)[1]
    expressions = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', "", source_entry)
    expected_ops = re.findall(r"\*\*|[+*/%-]", expressions)
    cpp_ops = re.findall(r"= [^;\n]+? ([+*/-]) [^;\n]+;", entry[1])
    for symbol in ("+", "-", "*", "/"):
        if cpp_ops.count(symbol) < expected_ops.count(symbol):
            raise RuntimeError(f"{name}/{mode}: erased a source numeric {symbol} operand")
    if entry[1].count("std::fmod(") < expected_ops.count("%") or entry[1].count(
        "std::pow("
    ) < expected_ops.count("**"):
        raise RuntimeError(f"{name}/{mode}: erased evaluated remainder or power operands")


def numeric_entry_observer_source(source, name):
    branch = name in {
        "local_numeric_branch_lifetime",
        "scalar_saved_branch_lifetime",
        "scalar_alias_branch_lifetime",
        "constant_branch_lifetime",
        "constant_boolean_branch_lifetime",
        "constant_string_branch_lifetime",
    }
    observed = source + """
(function() {
    const seen = [], results = [], sizes = [];
    const originalSet = Map.prototype.set;
    Map.prototype.set = function(key, value) {
        seen.push(value); return originalSet.call(this, key, value);
    };
    const setter = host.slot.set, size = host.slot.size;
    host = {};
    const values = [2, -3, 0, 2.5];
    for (let call = 0; call < values.length; ++call) {
        results.push(setter(call < 2 ? 'future-key' : 'other-key', values[call] CTN_FLAG));
        sizes.push(size());
    }
    Map.prototype.set = originalSet;
    trace = 0;
""".replace(" CTN_FLAG", ", call % 2 !== 0" if branch else "")
    checks = [
        "seen.length === 4",
        "sizes.every(value => value === 0)",
        "results.every((value, index) => value === values[index])",
        "seen.every((value, index) => value.value === values[index])",
    ]
    checks += [
        f"seen[{left}] !== seen[{right}]" for left in range(4) for right in range(left + 1, 4)
    ]
    for check in checks:
        observed += f"    if ({check}) {{ trace += 1; }}\n"
    observed += "    for (const item of seen) { item.value = 99; }\n"
    observed += (
        "    if (results.every((value, index) => value === values[index])) { trace += 1; }\n"
    )
    return observed + "})();\n", len(checks) + 1


def numeric_entry_lifetime_cpp(cpp, name):
    branch = name in {
        "local_numeric_branch_lifetime",
        "scalar_saved_branch_lifetime",
        "scalar_alias_branch_lifetime",
        "constant_branch_lifetime",
        "constant_boolean_branch_lifetime",
        "constant_string_branch_lifetime",
    }
    changed = instrument_leaf_objects(cpp)
    changed = changed.replace(
        "static std::vector<std::weak_ptr<const void>> ctn_test_objects;",
        "static std::vector<std::weak_ptr<const void>> ctn_test_objects;\n"
        "static std::shared_ptr<const void> ctn_test_retained;\n"
        "static bool ctn_test_keep_leaf = false;",
    )
    changed = changed.replace(
        "ctn_test_objects.emplace_back(made); return made;",
        "ctn_test_objects.emplace_back(made);\n"
        "    if (ctn_test_keep_leaf) { ctn_test_retained = made; }\n"
        "    return made;",
    )
    changed += r"""
int main() {
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1 ||
        ctn_test_objects.size() != 3) { return 180; }
    for (const auto & leaf : ctn_test_objects) {
        if (!leaf.expired()) { return 181; }
    }
    auto owner = g_host;
    auto table = owner->slot;
    auto setter = table->m_set;
    auto size = table->m_size;
    static_assert(std::is_same_v<decltype(size), std::function<ctnative::js_num()>>);
    static_assert(std::is_same_v<decltype(setter), std::function<ctnative::js_num(CTN_PARAMS)>>);
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() || ctn_test_maps[0].expired()) {
        return 182;
    }
    const std::string original(160, 'k');
    std::vector<js_num> saved;
    ctn_test_keep_leaf = true;
    for (int call = 0; call < 128; ++call) {
        auto caller = original;
        if (call % 2) { caller += 'r'; }
        const auto before = ctn_test_objects.size();
        const js_num value = static_cast<js_num>(call - 64) / 4;
        saved.push_back(setter(caller, ctnative::js_num{value} CTN_FLAG).value());
        caller.assign(original.size(), 'q');
        if (saved.back() != value || size().value() != 0 || ctn_test_objects.size() != before + 1) {
            return 183;
        }
        for (std::size_t index = 0; index < before; ++index) {
            if (!ctn_test_objects[index].expired()) { return 184; }
        }
        const auto leaf = std::static_pointer_cast<const ctnative::identity_object>(ctn_test_retained);
        if (!leaf || ctn_test_objects.back().expired() ||
            leaf->field_76616c7565.tag != ctnative::nullable_scalar::kind::number ||
            leaf->field_76616c7565.value != value) { return 185; }
    }
    ctn_test_keep_leaf = false;
    const auto retained_index = ctn_test_objects.size() - 1;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock() ||
        size().value() != 0 || g_host->slot->m_size().value() != 0) { return 186; }
    setter = {};
    if (ctn_test_maps[0].expired()) { return 187; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired() ||
        ctn_test_objects[retained_index].expired()) { return 188; }
    g_host.reset();
    if (!ctn_test_maps[1].expired()) { return 189; }
    for (std::size_t index = 0; index < ctn_test_objects.size(); ++index) {
        if (ctn_test_objects[index].expired() != (index != retained_index)) { return 190; }
    }
    for (std::size_t index = 0; index < saved.size(); ++index) {
        if (saved[index] != (static_cast<js_num>(index) - 64) / 4) { return 191; }
    }
    {
        const auto leaf = std::static_pointer_cast<const ctnative::identity_object>(ctn_test_retained);
        if (!leaf || leaf->field_76616c7565.tag != ctnative::nullable_scalar::kind::number ||
            leaf->field_76616c7565.value != saved.back()) { return 192; }
    }
    ctn_test_retained.reset();
    if (!ctn_test_objects[retained_index].expired()) { return 193; }
    return 0;
}
"""
    if name in {
        "scalar_saved_branch_lifetime",
        "scalar_alias_branch_lifetime",
        "constant_branch_lifetime",
        "constant_boolean_branch_lifetime",
        "constant_string_branch_lifetime",
    }:
        changed = changed.replace(
            "    auto owner = g_host;",
            """
    const auto first_snapshot = ctnative::global_number(g_first).value();
    const auto second_snapshot = ctnative::global_number(g_second).value();
    const auto third_snapshot = ctnative::global_number(g_third).value();
    static_assert(std::is_same_v<decltype(first_snapshot), const js_num>);
    if (first_snapshot != 2 || second_snapshot != 3 || third_snapshot != 4 ||
        ctnative::global_number(g_trace).value() != first_snapshot + second_snapshot * third_snapshot) {
        return 194;
    }
    auto owner = g_host;""",
        )
        changed = changed.replace(
            "    ctn_test_retained.reset();",
            """
    if (first_snapshot != 2 || second_snapshot != 3 || third_snapshot != 4 ||
        ctnative::global_number(g_first).value() != first_snapshot ||
        ctnative::global_number(g_second).value() != second_snapshot ||
        ctnative::global_number(g_third).value() != third_snapshot ||
        ctnative::global_number(g_trace).value() != first_snapshot + second_snapshot * third_snapshot) {
        return 195;
    }
    {
        auto leaf = std::const_pointer_cast<ctnative::identity_object>(
            std::static_pointer_cast<const ctnative::identity_object>(ctn_test_retained));
        leaf->field_76616c7565.value = 99;
        if (saved.back() != 15.75 || first_snapshot != 2 || second_snapshot != 3 ||
            third_snapshot != 4 || ctnative::global_number(g_first).value() != 2) { return 196; }
    }
    ctn_test_retained.reset();""",
        )
    if name in {
        "scalar_alias_branch_lifetime",
        "constant_branch_lifetime",
        "constant_boolean_branch_lifetime",
        "constant_string_branch_lifetime",
    }:
        changed = changed.replace(
            "    auto owner = g_host;",
            """
    const auto left_snapshot = ctnative::global_number(g_left).value();
    const auto middle_snapshot = ctnative::global_number(g_middle).value();
    const auto right_snapshot = ctnative::global_number(g_right).value();
    static_assert(std::is_same_v<decltype(left_snapshot), const js_num>);
    if (left_snapshot != first_snapshot || middle_snapshot != second_snapshot ||
        right_snapshot != third_snapshot) { return 197; }
    auto owner = g_host;""",
        )
        changed = changed.replace(
            "    ctn_test_keep_leaf = false;",
            """
    if (ctnative::global_number(g_left).value() != left_snapshot ||
        ctnative::global_number(g_middle).value() != middle_snapshot ||
        ctnative::global_number(g_right).value() != right_snapshot ||
        ctnative::global_number(g_trace).value() != left_snapshot + middle_snapshot * right_snapshot) {
        return 199;
    }
    ctn_test_keep_leaf = false;""",
        )
        changed = changed.replace(
            "    ctn_test_retained.reset();",
            """
    if (left_snapshot != 2 || middle_snapshot != 3 || right_snapshot != 4 ||
        ctnative::global_number(g_left).value() != left_snapshot ||
        ctnative::global_number(g_middle).value() != middle_snapshot ||
        ctnative::global_number(g_right).value() != right_snapshot ||
        ctnative::global_number(g_trace).value() != left_snapshot + middle_snapshot * right_snapshot) {
        return 198;
    }
    ctn_test_retained.reset();""",
        )
    if name in {
        "constant_branch_lifetime",
        "constant_boolean_branch_lifetime",
        "constant_string_branch_lifetime",
    }:
        changed = changed.replace(
            "    auto owner = g_host;",
            """
    const auto fixed_snapshot = ctnative::global_number(g_fixed).value();
    const auto offset_snapshot = ctnative::global_number(g_offset).value();
    const auto copy_snapshot = ctnative::global_number(g_copy).value();
    static_assert(std::is_same_v<decltype(copy_snapshot), const js_num>);
    if (fixed_snapshot != 7 || offset_snapshot != fixed_snapshot || copy_snapshot != offset_snapshot) {
        return 200;
    }
    auto owner = g_host;""",
        )
        checks = """
    if (fixed_snapshot != 7 || offset_snapshot != 7 || copy_snapshot != 7 ||
        ctnative::global_number(g_fixed).value() != fixed_snapshot ||
        ctnative::global_number(g_offset).value() != offset_snapshot ||
        ctnative::global_number(g_copy).value() != copy_snapshot ||
        ctnative::global_number(g_trace).value() != left_snapshot + middle_snapshot * right_snapshot +
                                           copy_snapshot - fixed_snapshot) {
        return 201;
    }
"""
        # Check once after 128 calls and owner destruction, before reentry can
        # overwrite globals, then again after both Maps and the retained leaf die.
        changed = changed.replace(
            "    ctn_test_keep_leaf = false;", checks + "    ctn_test_keep_leaf = false;"
        )
        released = "    if (!ctn_test_objects[retained_index].expired()) { return 193; }"
        assert changed.count(released) == 1
        changed = changed.replace(released, released + checks)
    if name == "constant_boolean_branch_lifetime":
        changed = changed.replace(
            "    auto owner = g_host;",
            """
    const auto fixed_flag_snapshot = ctnative::global_boolean(g_fixed_flag);
    const auto enabled_snapshot = ctnative::global_boolean(g_enabled);
    const auto copy_flag_snapshot = ctnative::global_boolean(g_copy_flag);
    const auto active_snapshot = ctnative::global_boolean(g_active);
    static_assert(std::is_same_v<decltype(copy_flag_snapshot), const ctnative::js_boolean_t>);
    if (fixed_flag_snapshot || !enabled_snapshot || copy_flag_snapshot || !active_snapshot) {
        return 202;
    }
    auto owner = g_host;""",
        )
        checks = """
    if (fixed_flag_snapshot || !enabled_snapshot || copy_flag_snapshot || !active_snapshot ||
        ctnative::global_boolean(g_fixed_flag) != fixed_flag_snapshot ||
        ctnative::global_boolean(g_enabled) != enabled_snapshot ||
        ctnative::global_boolean(g_copy_flag) != copy_flag_snapshot ||
        ctnative::global_boolean(g_active) != active_snapshot) {
        return 203;
    }
"""
        changed = changed.replace(
            "    ctn_test_keep_leaf = false;", checks + "    ctn_test_keep_leaf = false;"
        )
        released = "    if (!ctn_test_objects[retained_index].expired()) { return 193; }"
        assert changed.count(released) == 1
        changed = changed.replace(released, released + checks)
    if name == "constant_string_branch_lifetime":
        raw = str(STRING_GLOBAL_LONG).encode("utf-8")
        literal = 'std::string("' + "".join(f"\\{byte:03o}" for byte in raw) + f'", {len(raw)})'
        changed = changed.replace(
            "    auto owner = g_host;",
            """
    const std::string expected_text = CTN_EXPECTED_TEXT;
    const auto owned_snapshot = ctnative::global_string(g_owned_text);
    const auto alias_snapshot = ctnative::global_string(g_text_alias);
    const auto saved_snapshot = ctnative::global_string(g_saved_text);
    const auto empty_snapshot = ctnative::global_string(g_empty_text);
    static_assert(std::is_same_v<decltype(saved_snapshot), const std::string>);
    if (owned_snapshot != expected_text || alias_snapshot != expected_text ||
        saved_snapshot != expected_text || !empty_snapshot.empty()) { return 204; }
    auto owner = g_host;""".replace("CTN_EXPECTED_TEXT", literal),
        )
        checks = """
    if (owned_snapshot != expected_text || alias_snapshot != expected_text ||
        saved_snapshot != expected_text || !empty_snapshot.empty() ||
        ctnative::global_string(g_owned_text) != expected_text ||
        ctnative::global_string(g_text_alias) != expected_text ||
        ctnative::global_string(g_saved_text) != expected_text ||
        !ctnative::global_string(g_empty_text).empty()) { return 205; }
"""
        changed = changed.replace(
            "    ctn_test_keep_leaf = false;",
            checks + """
    g_owned_text.value.assign(2048, 'm');
    g_text_alias = {};
    g_saved_text = {};
    g_empty_text = {};
    std::vector<std::string> text_churn(128, std::string(2048, 'c'));
    if (owned_snapshot != expected_text || alias_snapshot != expected_text ||
        saved_snapshot != expected_text || !empty_snapshot.empty()) { return 206; }
    ctn_test_keep_leaf = false;""",
        )
        released = "    if (!ctn_test_objects[retained_index].expired()) { return 193; }"
        assert changed.count(released) == 1
        changed = changed.replace(released, released + checks)
    return changed.replace(
        "CTN_PARAMS",
        (
            "std::string, ctnative::js_num, ctnative::js_boolean_t"
            if branch
            else "std::string, ctnative::js_num"
        ),
    ).replace("CTN_FLAG", ", ctnative::js_boolean_t{call % 2 != 0}" if branch else "")


def numeric_entry_lifetime(args, cpp, name, mode, compiler):
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    source.write_text(numeric_entry_lifetime_cpp(cpp, name))
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
    if result.returncode or result.stdout != scalar_global_output(name, 14) * 2:
        raise RuntimeError(
            f"{name}/{mode}: saved numeric lifetime failure (exit {result.returncode})\n"
            f"{result.stdout}{result.stderr}"
        )
