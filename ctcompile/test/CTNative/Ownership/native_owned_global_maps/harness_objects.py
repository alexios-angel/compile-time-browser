"""harness objects: continued from harness_leaf_objects."""

from .harness_leaf_objects import *


def object_argument_observer_source(source, global_key=False, global_alias=False, global_chain=()):
    # Capture the actual Map in a separate interpreter observer. The native
    # source and its standard intrinsic contract remain untouched.
    observed = source + """
(function() {
    const get = host.slot.get;
    const original = Map.prototype.has;
    let captured;
    Map.prototype.has = function(key) { captured = this; return original.call(this, key); };
    get({});
    Map.prototype.has = original;
    const first = {}, alias = first, other = {};
    captured.set(first, 7);
    const a = get(first), b = get(alias), c = get(other);
    captured.delete(first);
    const d = get(alias);
    captured.set(other, 9);
    const e = get(other), f = get(first);
    captured.clear();
    const g = get(other);
    let future = true;
    for (let i = 0; i < 128; ++i) {
        const key = {};
        if (get(key) !== 0) { future = false; }
        captured.set(key, i);
        if (get(key) !== 1 || get({}) !== 0) { future = false; }
        captured.delete(key);
        if (get(key) !== 0) { future = false; }
    }
    trace = (typeof a === 'number' && a === 1 ? 1 : 0) |
            (typeof b === 'number' && b === 1 ? 2 : 0) |
            (typeof c === 'number' && c === 0 ? 4 : 0) |
            (typeof d === 'number' && d === 0 ? 8 : 0) |
            (typeof e === 'number' && e === 1 ? 16 : 0) |
            (typeof f === 'number' && f === 0 ? 32 : 0) |
            (typeof g === 'number' && g === 0 ? 64 : 0) | (future ? 128 : 0);
})();
"""
    if not global_key and not global_alias:
        return observed, 255
    observed = (
        observed.replace(
            "const first = {}, alias = first, other = {};",
            "const first = key, alias = first, other = {};",
        )
        .replace(
            "    trace = (typeof a",
            """    const startup = key === first;
    captured.set(key, 17);
    key = {};
    const distinct = key !== first && get(first) === 1 && get(key) === 0;
    captured.clear();
    const cleared = get(first) === 0;
    trace = (typeof a""",
        )
        .replace(
            "(future ? 128 : 0);",
            "(future ? 128 : 0) | (startup ? 256 : 0) | (distinct ? 512 : 0) | (cleared ? 1024 : 0);",
        )
    )
    if global_alias:
        observed = (
            observed.replace("(function() {", "(function(globalAlias) {")
            .replace(
                "const first = key, alias = first, other = {};",
                "const first = key, alias = globalAlias, other = {};",
            )
            .replace(
                "const startup = key === first;",
                "const startup = key === first && first === globalAlias;",
            )
            .replace(
                "key !== first && get(first) === 1",
                "key !== first && get(globalAlias) === 1 && get(first) === 1",
            )
            .replace("})();", "})(alias);")
        )
        if global_chain:
            parameters = ", ".join("chain_" + binding for binding in global_chain)
            observed = (
                observed.replace(
                    "(function(globalAlias)", "(function(globalAlias, " + parameters + ")"
                )
                .replace(
                    "first === globalAlias;",
                    "first === globalAlias"
                    + "".join(" && first === chain_" + binding for binding in global_chain)
                    + ";",
                )
                .replace("})(alias);", "})(alias, " + ", ".join(global_chain) + ");")
            )
    return observed, 2047


def check_object_argument_calls(cpp, name, mode):
    source = object_argument_cases()[name]["source"]
    receiver = "state" if name == "parameter_object" else "t"
    entry = re.search(r"\bmain\(\)\s*\{(.*?)^\}", cpp, re.M | re.S)
    arity = 2 if name == "object_argument_two_formals" else 1
    signature = (
        "std::function<::js_num("
        + ", ".join(["std::shared_ptr<ctnative::identity_object>"] * arity)
        + ")>"
    )
    fields = name in {
        "object_argument_field",
        "object_argument_global_field_write",
        "object_argument_payload_field",
    }
    identity = (
        "struct identity_object {\n    nullable_scalar field_76616c7565;\n};"
        if fields
        else "struct identity_object {};"
    )
    field_literal = name in {"object_argument_field", "object_argument_payload_field"}
    if (
        not entry
        or signature not in cpp
        or identity not in cpp
        or entry[1].count("ctnative::invoke_callable(")
        != len(re.findall(r"host\.slot\.\w+\(", source))
        or entry[1].count("std::make_shared<ctnative::identity_object>()")
        != source.count("{}") - 1 + int(field_literal)
        or fields
        and entry[1].count("ctnative::object_set_field_76616c7565(") != 1
    ):
        raise RuntimeError(f"{name}/{mode}: changed live object allocations or callable actuals")
    for method in ("has", "set", "get", "delete", "clear"):
        emitted = len(re.findall(rf"ctnative::map_{method}(?:_\w+)?(?:<[^>]+>)?\(", cpp))
        if emitted != len(re.findall(rf"\b{receiver}\.{method}\(", source)):
            raise RuntimeError(f"{name}/{mode}: changed live Map.{method} calls")
    if name == "parameter_object" and cpp.count("ctnative::map_size(") != 2:
        raise RuntimeError(f"{name}/{mode}: lost the historical setter/getter size reads")
    if name == "object_argument_seeded" and (
        "std::variant<double, std::shared_ptr<ctnative::identity_object>>" not in cpp
    ):
        raise RuntimeError(f"{name}/{mode}: lost independent Number/Object key alternatives")
    row = object_argument_cases()[name]
    if row.get("object_payload"):
        keys = (
            ("double", "js_num")
            if row.get("payload_only")
            else ("std::shared_ptr<ctnative::identity_object>",)
        )
        if not any("map_storage<" + key + ", ctnative::object_value>" in cpp for key in keys):
            raise RuntimeError(f"{name}/{mode}: lost the owning object payload carrier")
    if object_argument_cases()[name].get("global_key"):
        created = re.findall(
            r"(\w+)\s*=\s*std::make_shared<ctnative::identity_object>\(\)", entry[1]
        )
        stores = re.findall(r"\bg_key = (\w+);", entry[1])
        loads = re.findall(r"\b(\w+) = g_key;", entry[1])
        actuals = re.findall(r"ctnative::invoke_callable\(\w+, (\w+)\);", entry[1])
        if (
            not re.search(r"std::shared_ptr<ctnative::identity_object>\s+g_key\s*;", cpp)
            or len(created) != 1
            or stores != created
            or len(loads) != source.count("host.slot.get(key)")
            or actuals != loads
        ):
            raise RuntimeError(
                f"{name}/{mode}: lost the sole global allocation/store/load/call identity"
            )
    if object_argument_cases()[name].get("global_alias"):
        row = object_argument_cases()[name]
        bindings = "|".join(("key", "alias", *row.get("global_chain", ()), "other"))
        initializers = re.findall(r"\b(" + bindings + r") = (\{\}|\w+)(?=[,;])", source)
        predecessors = [value for _, value in initializers if value != "{}"]
        created = re.findall(
            r"(\w+)\s*=\s*std::make_shared<ctnative::identity_object>\(\)", entry[1]
        )
        loads = re.findall(r"\b(\w+) = g_(" + bindings + r");", entry[1])
        stores = re.findall(r"\bg_(" + bindings + r") = (\w+);", entry[1])
        actuals = re.findall(r"ctnative::invoke_callable\(\w+, (\w+)(?:, \w+)?\);", entry[1])
        arguments = re.findall(r"host\.slot\.\w+\((" + bindings + r")[,)]", source)
        allocations, reads = iter(created), iter(value for value, _ in loads)
        expected_stores = [
            (binding, next(allocations) if value == "{}" else next(reads))
            for binding, value in initializers
        ]
        if (
            stores != expected_stores
            or [binding for _, binding in loads] != [*predecessors, *arguments]
            or actuals != [value for value, _ in loads[len(predecessors) :]]
            or any(
                not re.search(
                    r"std::shared_ptr<ctnative::identity_object>\s+g_" + binding + r"\s*;", cpp
                )
                for binding, _ in initializers
            )
        ):
            raise RuntimeError(
                f"{name}/{mode}: lost the original global alias/store/load/call edges"
            )
        for (binding, predecessor), (value, _) in zip(
            ((binding, value) for binding, value in initializers if value != "{}"), loads
        ):
            if not (
                entry[1].index("g_" + predecessor + " = ")
                < entry[1].index(value + " = g_" + predecessor + ";")
                < entry[1].index("g_" + binding + " = ")
                < entry[1].index("ctnative::invoke_callable(")
            ):
                raise RuntimeError(f"{name}/{mode}: reordered a global alias initializer")


def object_argument_lifetime_cpp(cpp, global_key=False, global_alias=False, global_chain=()):
    changed = instrument_leaf_objects(cpp) + r"""
int main() {
    using Key = std::shared_ptr<ctnative::identity_object>;
    using Map = ctnative::number_map<Key>;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1 ||
        ctn_test_objects.size() != 1 || !ctn_test_objects[0].expired()) { return 200; }
    auto owner = g_host;
    auto table = owner->slot;
    auto get = table->m_get;
    static_assert(std::is_same_v<decltype(get), std::function<js_num(Key)>>);
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired()) { return 201; }
    auto map = std::const_pointer_cast<Map>(
        std::static_pointer_cast<const Map>(ctn_test_maps[0].lock()));
    auto other = std::make_shared<ctnative::identity_object>();
    for (int call = 0; call < 128; ++call) {
        auto key = std::make_shared<ctnative::identity_object>();
        auto alias = key;
        std::weak_ptr key_lifetime = key;
        if (get(key) != 0) { return 202; }
        ctnative::map_set(map, key, static_cast<js_num>(call));
        if (get(key) != 1 || get(alias) != 1 || get(other) != 0) { return 203; }
        key.reset();
        if (key_lifetime.expired() || get(alias) != 1 ||
            !ctnative::map_delete(map, alias) || get(alias) != 0) { return 204; }
        alias.reset();
        if (!key_lifetime.expired()) { return 205; }
    }
    ctnative::map_set(map, other, js_num{7});
    std::weak_ptr key_lifetime = other;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock() || get(other) != 1 ||
        g_host->slot->m_get(other) != 0 || !ctn_test_objects[1].expired()) { return 206; }
    other.reset(); map.reset();
    if (key_lifetime.expired() || ctn_test_maps[0].expired()) { return 207; }
    get = {};
    if (!key_lifetime.expired() || !ctn_test_maps[0].expired() ||
        ctn_test_maps[1].expired()) { return 208; }
    g_host.reset();
    if (!ctn_test_maps[1].expired()) { return 209; }
    return 0;
}
"""
    if not global_key and not global_alias:
        return changed
    changed = (
        changed.replace(
            "!ctn_test_objects[0].expired()",
            "ctn_test_objects[0].expired() || !g_key || g_key != ctn_test_objects[0].lock()",
        )
        .replace(
            "    auto other = std::make_shared<ctnative::identity_object>();",
            """    std::weak_ptr original_key = g_key;
    ctnative::map_set(map, g_key, js_num{9});
    if (get(g_key) != 1) { return 240; }
    g_key = std::make_shared<ctnative::identity_object>();
    if (original_key.expired() || get(g_key) != 0) { return 241; }
    auto other = std::make_shared<ctnative::identity_object>();""",
        )
        .replace(
            "    std::weak_ptr key_lifetime = other;",
            "    std::weak_ptr overwritten_key = g_key;\n    std::weak_ptr key_lifetime = other;",
        )
        .replace(
            "!ctn_test_objects[1].expired()",
            "ctn_test_objects[1].expired() || !overwritten_key.expired() || "
            "g_key != ctn_test_objects[1].lock() || g_key == original_key.lock()",
        )
        .replace(
            "if (!key_lifetime.expired() || !ctn_test_maps[0].expired() ||",
            "if (!key_lifetime.expired() || !original_key.expired() || !ctn_test_maps[0].expired() ||",
        )
        .replace(
            "    if (!ctn_test_maps[1].expired()) { return 209; }",
            """    if (!ctn_test_maps[1].expired() || ctn_test_objects[1].expired()) { return 209; }
    g_key.reset();
    if (!ctn_test_objects[1].expired()) { return 242; }""",
        )
    )
    if global_alias:
        changed = (
            changed.replace(
                "g_key != ctn_test_objects[0].lock()",
                "g_key != ctn_test_objects[0].lock() || g_alias != g_key",
            )
            .replace(
                "    ctnative::map_set(map, g_key, js_num{9});",
                """    g_key.reset();
    if (original_key.expired() || !g_alias || g_alias != original_key.lock()) { return 243; }
    g_key = g_alias;
    ctnative::map_set(map, g_key, js_num{9});""",
            )
            .replace(
                "    if (original_key.expired() || get(g_key) != 0)",
                "    g_alias.reset();\n    if (original_key.expired() || get(g_key) != 0)",
            )
            .replace(
                "g_key != ctn_test_objects[1].lock()",
                "g_key != ctn_test_objects[1].lock() || g_alias != g_key",
            )
            .replace(
                "    g_key.reset();\n    if (!ctn_test_objects[1].expired())",
                "    g_alias.reset();\n    g_key.reset();\n    if (!ctn_test_objects[1].expired())",
            )
        )
        if global_chain:
            assert global_chain == ("copy",)
            changed = (
                changed.replace("g_alias != g_key", "g_alias != g_key || g_copy != g_key")
                .replace(
                    "    g_key = g_alias;",
                    """    g_alias.reset();
    if (original_key.expired() || !g_copy || g_copy != original_key.lock()) { return 249; }
    g_alias = g_copy;
    g_key = g_alias;""",
                )
                .replace(
                    "    g_alias.reset();\n    if (original_key.expired() || get(g_key)",
                    "    g_alias.reset(); g_copy.reset();\n    if (original_key.expired() || get(g_key)",
                )
                .replace(
                    "    g_key.reset();\n    if (!ctn_test_objects[1].expired())",
                    """    g_key.reset();
    if (ctn_test_objects[1].expired() || !g_copy ||
        g_copy != ctn_test_objects[1].lock()) { return 250; }
    g_copy.reset();
    if (!ctn_test_objects[1].expired())""",
                )
            )
    return changed


def object_argument_lifetime(args, cpp, name, mode, compiler):
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    observer = (
        retained_key_lifetime_cpp
        if name
        in {
            "object_argument_siblings",
            "object_argument_siblings_named",
            "object_argument_siblings_global",
            "object_argument_siblings_global_chain",
        }
        else (
            parameter_object_lifetime_cpp
            if name == "parameter_object"
            else object_argument_lifetime_cpp
        )
    )
    source.write_text(
        observer(cpp, global_key=True)
        if name == "object_argument_global"
        else (
            observer(cpp, global_alias=True)
            if name == "object_argument_global_alias"
            else (
                observer(cpp, global_alias=True, global_chain=("copy",))
                if name == "object_argument_global_alias_chain"
                else (
                    observer(cpp, 2, 0, global_alias=True)
                    if name == "object_argument_siblings_global"
                    else (
                        observer(
                            cpp, 2, 0, global_alias=True, global_chain=("copy", "tail", "branch")
                        )
                        if name == "object_argument_siblings_global_chain"
                        else (
                            observer(cpp, 2, 0)
                            if name == "object_argument_siblings_named"
                            else (
                                object_payload_lifetime_cpp(cpp, name)
                                if object_argument_cases()[name].get("object_payload")
                                else observer(cpp)
                            )
                        )
                    )
                )
            )
        )
    )
    binary = source.with_suffix(".sanitized").resolve()
    host.run(
        [
            compiler,
            *owned.FLAGS,
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            str(source),
            "-o",
            str(binary),
        ]
    )
    result = subprocess.run(
        [str(binary)],
        capture_output=True,
        text=True,
        timeout=30,
        env={
            **os.environ,
            "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
            "UBSAN_OPTIONS": "halt_on_error=1",
        },
    )
    expected = object_argument_cases()[name]["expected_trace"]
    if result.returncode or result.stdout != f"trace={expected}\n" * 2 or result.stderr:
        raise RuntimeError(
            f"{name}/{mode}: object key lifetime failed\n"
            f"{result.returncode}: {result.stdout}{result.stderr}"
        )


def retained_key_observer_source(source, global_alias=False, global_chain=()):
    observed = source + """
(function() {
    const get = host.slot.get, set = host.slot.set;
    const erase = host.slot.erase, clear = host.slot.clear;
    clear();
    const first = {}, alias = first, other = {};
    const a = set(first, 7) === 7 && get(alias) === 7 && get(other) === 0;
    const b = set(alias, 9) === 9 && get(first) === 9;
    const c = erase(other) === 0 && get(first) === 9;
    const d = erase(alias) === 1 && get(first) === 0 && erase(first) === 0;
    set(first, 11);
    const e = clear() === 0 && get(first) === 0;
    let future = true;
    for (let i = 0; i < 128; ++i) {
        const key = {}, same = key;
        if (get(key) !== 0 || set(key, i + 1) !== i + 1 ||
            get(same) !== i + 1 || get({}) !== 0 || erase(same) !== 1 ||
            get(key) !== 0) { future = false; }
        set(key, i + 2);
        clear();
        if (get(key) !== 0) { future = false; }
    }
    trace = (a ? 1 : 0) | (b ? 2 : 0) | (c ? 4 : 0) |
            (d ? 8 : 0) | (e ? 16 : 0) | (future ? 32 : 0);
})();
"""
    if not global_alias:
        return observed, 63
    observed = (
        observed.replace("(function() {", "(function(first, alias, other) {")
        .replace(
            "const first = {}, alias = first, other = {};",
            "const startup = first === alias && first !== other;",
        )
        .replace("(future ? 32 : 0);", "(future ? 32 : 0) | (startup ? 64 : 0);")
        .replace("})();", "})(key, alias, other);")
    )
    if global_chain:
        parameters = ", ".join("chain_" + binding for binding in global_chain)
        observed = (
            observed.replace(
                "(function(first, alias, other)",
                "(function(first, alias, other, " + parameters + ")",
            )
            .replace(
                "first !== other;",
                "first !== other"
                + "".join(" && first === chain_" + binding for binding in global_chain)
                + ";",
            )
            .replace(
                "})(key, alias, other);", "})(key, alias, other, " + ", ".join(global_chain) + ");"
            )
        )
    return observed, 127


def retained_key_lifetime_cpp(cpp, allocations=6, retained=4, global_alias=False, global_chain=()):
    changed = (instrument_leaf_objects(cpp, allocations=allocations) + r"""
int main() {
    using Key = std::shared_ptr<ctnative::identity_object>;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1 ||
        ctn_test_objects.size() != CTN_ALLOCATIONS) { return 210; }
    for (std::size_t index = 0; index < CTN_ALLOCATIONS; ++index) {
        if (ctn_test_objects[index].expired() != (index != CTN_RETAINED)) { return 224; }
    }
    auto owner = g_host;
    auto table = owner->slot;
    auto get = table->m_get;
    auto set = table->m_set;
    auto erase = table->m_erase;
    auto clear = table->m_clear;
    static_assert(std::is_same_v<decltype(get), std::function<js_num(Key)>>);
    static_assert(std::is_same_v<decltype(set), std::function<js_num(Key, js_num)>>);
    static_assert(std::is_same_v<decltype(erase), std::function<js_num(Key)>>);
    static_assert(std::is_same_v<decltype(clear), std::function<js_num()>>);
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired() || clear() != 0 ||
        !ctn_test_objects[CTN_RETAINED].expired()) { return 211; }
    auto other = std::make_shared<ctnative::identity_object>();
    for (int call = 0; call < 128; ++call) {
        auto key = std::make_shared<ctnative::identity_object>();
        auto alias = key;
        std::weak_ptr key_lifetime = key;
        const auto value = static_cast<js_num>(call + 1);
        if (get(key) != 0 || set(key, value) != value || get(alias) != value ||
            get(other) != 0 || erase(other) != 0) { return 212; }
        key.reset();
        if (set(alias, value + 1) != value + 1 || get(alias) != value + 1) { return 213; }
        alias.reset();
        if (key_lifetime.expired()) { return 214; }
        key = key_lifetime.lock();
        if (get(key) != value + 1 || erase(key) != 1 || get(key) != 0 ||
            erase(key) != 0) { return 215; }
        key.reset();
        if (!key_lifetime.expired()) { return 216; }
        key = std::make_shared<ctnative::identity_object>();
        key_lifetime = key;
        if (set(key, value) != value) { return 217; }
        key.reset();
        if (key_lifetime.expired() || clear() != 0 || !key_lifetime.expired()) { return 218; }
    }
    std::weak_ptr key_lifetime = other;
    if (set(other, 7) != 7 || ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock() || get(other) != 7 ||
        g_host->slot->m_get(other) != 0) { return 219; }
    other.reset();
    set = {}; erase = {}; clear = {};
    if (key_lifetime.expired() || ctn_test_maps[0].expired()) { return 220; }
    get = {};
    if (!key_lifetime.expired() || !ctn_test_maps[0].expired() ||
        ctn_test_maps[1].expired()) { return 221; }
    g_host.reset();
    if (!ctn_test_maps[1].expired()) { return 222; }
    for (const auto & key : ctn_test_objects) {
        if (!key.expired()) { return 223; }
    }
    return 0;
}
""").replace("CTN_ALLOCATIONS", str(allocations)).replace("CTN_RETAINED", str(retained))
    if not global_alias:
        return changed
    assert (allocations, retained) == (2, 0)
    changed = changed.replace(
        "    for (std::size_t index = 0; index < 2; ++index)",
        """    if (!g_key || g_key != g_alias || g_key != ctn_test_objects[0].lock() ||
        !g_other || g_other != ctn_test_objects[1].lock() || g_key == g_other) { return 244; }
    g_key.reset();
    if (ctn_test_objects[0].expired() || !g_alias) { return 245; }
    g_alias.reset(); g_other.reset();
    for (std::size_t index = 0; index < 2; ++index)""",
    ).replace(
        "    other.reset();\n    set = {};",
        """    if (ctn_test_objects.size() != 4 || !g_key || g_key != g_alias ||
        g_key != ctn_test_objects[2].lock() || !g_other ||
        g_other != ctn_test_objects[3].lock() || g_key == g_other) { return 246; }
    g_key.reset();
    if (ctn_test_objects[2].expired() || !g_alias) { return 247; }
    g_alias.reset(); g_other.reset();
    if (ctn_test_objects[2].expired() || !ctn_test_objects[3].expired()) { return 248; }
    other.reset();
    set = {};""",
    )
    if global_chain:
        changed = changed.replace(
            "g_key != g_alias",
            "g_key != g_alias" + "".join(" || g_key != g_" + binding for binding in global_chain),
        ).replace(
            "g_alias.reset(); g_other.reset();",
            "g_alias.reset(); g_other.reset(); "
            + " ".join("g_" + binding + ".reset();" for binding in global_chain),
        )
    return changed


def parameter_object_lifetime_cpp(cpp):
    return instrument_leaf_objects(cpp) + r"""
int main() {
    using Key = std::shared_ptr<ctnative::identity_object>;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1 ||
        ctn_test_objects.size() != 1 || ctn_test_objects[0].expired()) { return 230; }
    auto owner = g_host;
    auto table = owner->slot;
    auto get = table->m_get;
    auto set = table->m_set;
    static_assert(std::is_same_v<decltype(get), std::function<js_num()>>);
    static_assert(std::is_same_v<decltype(set), std::function<js_num(Key)>>);
    static_assert(!std::is_invocable_v<decltype(set), int>);
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired() || get() != 1) { return 231; }
    for (int call = 0; call < 128; ++call) {
        auto key = ctn_test_make_leaf<ctnative::identity_object>();
        auto alias = key;
        std::weak_ptr key_lifetime = key;
        const auto size = static_cast<js_num>(call + 2);
        if (set(key) != size || set(alias) != size || get() != size) { return 232; }
        key.reset(); alias.reset();
        if (key_lifetime.expired()) { return 233; }
    }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_objects.size() != 130 || get() != 129 || g_host->slot->m_get() != 1 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock()) { return 234; }
    set = {};
    if (ctn_test_maps[0].expired() || get() != 129) { return 235; }
    for (const auto & key : ctn_test_objects) {
        if (key.expired()) { return 236; }
    }
    get = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired() ||
        ctn_test_objects.back().expired()) { return 237; }
    for (std::size_t index = 0; index + 1 < ctn_test_objects.size(); ++index) {
        if (!ctn_test_objects[index].expired()) { return 238; }
    }
    g_host.reset();
    if (!ctn_test_maps[1].expired() || !ctn_test_objects.back().expired()) { return 239; }
    return 0;
}
"""


def object_payload_observer_source(source, name):
    row = object_argument_cases()[name]
    scalar = row.get("payload_only")
    observed = source + """
(function() {
    const get = host.slot.get;
    SAVED_CALLABLES
    host = {};
    const first = FIRST_OBJECT, alias = first, other = {};
    const original = Map.prototype.has;
    let captured;
    Map.prototype.has = function(key) { captured = this; return original.call(this, key); };
    const a = get(first);
    Map.prototype.has = original;
    const firstStored = GLOBAL_IDENTITY && captured.get(FIRST_KEY) === first;
    const b = get(alias);
    const aliasStored = alias === first && captured.get(FIRST_KEY) === first;
    const c = get(other);
    const otherStored = captured.get(OTHER_KEY) === other;
    const distinct = first !== other && DISTINCT;
    const deleted = DELETE_OTHER === DELETE_RESULT && !captured.has(OTHER_KEY) &&
                    DELETE_OTHER === ABSENT_DELETE_RESULT;
    const cleared = CLEAR_MAP === CLEAR_RESULT && captured.size === 0;
    let future = true;
    for (let i = 0; i < 128; ++i) {
        const item = {}, replacement = {};
        if (get(item) !== 1 || captured.get(ITEM_KEY) !== item) { future = false; }
        if (get(replacement) !== 1 || captured.get(REPLACEMENT_KEY) !== replacement) { future = false; }
        if (DELETE_REPLACEMENT !== DELETE_RESULT || captured.has(REPLACEMENT_KEY)) { future = false; }
        if (CLEAR_MAP !== CLEAR_RESULT || captured.size !== 0) { future = false; }
    }
    trace = (typeof a === 'number' && a === 1 && firstStored ? 1 : 0) |
            (typeof b === 'number' && b === 1 && aliasStored ? 2 : 0) |
            (typeof c === 'number' && c === 1 && otherStored ? 4 : 0) |
            (distinct ? 8 : 0) | (deleted ? 16 : 0) | (cleared ? 32 : 0) | (future ? 64 : 0);
})();
"""
    replacements = dict(
        SAVED_CALLABLES="const erase = host.slot.erase, clear = host.slot.clear;" if scalar else "",
        GLOBAL_IDENTITY="first === key" if row.get("global_key") else "true",
        FIRST_OBJECT="key" if row.get("global_key") else "{}",
        FIRST_KEY="1" if scalar else "first",
        OTHER_KEY="1" if scalar else "other",
        ITEM_KEY="1" if scalar else "item",
        REPLACEMENT_KEY="1" if scalar else "replacement",
        DISTINCT="captured.get(1) !== first" if scalar else "captured.get(first) === first",
        DELETE_OTHER="erase()" if scalar else "captured.delete(other)",
        DELETE_REPLACEMENT="erase()" if scalar else "captured.delete(replacement)",
        ABSENT_DELETE_RESULT="0" if scalar else "false",
        DELETE_RESULT="1" if scalar else "true",
        CLEAR_MAP="clear()" if scalar else "captured.clear()",
        CLEAR_RESULT="0" if scalar else "undefined",
    )
    for before, after in replacements.items():
        observed = observed.replace(before, after)
    return observed, 127


def object_payload_lifetime_cpp(cpp, name):
    row = object_argument_cases()[name]
    scalar = row.get("payload_only")
    changed = instrument_leaf_objects(cpp) + r"""
int main() {
    using Object = std::shared_ptr<ctnative::identity_object>;
    using Map = ctnative::map_storage<KEY_TYPE, ctnative::object_value>;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1 ||
        ctn_test_objects.size() != 1 || ctn_test_objects[0].expired()) { return 251; }
    auto owner = g_host;
    auto table = owner->slot;
    auto get = table->m_get;
    SAVED_CALLABLES
    static_assert(std::is_same_v<decltype(get), std::function<js_num(Object)>>);
    static_assert(!std::is_invocable_v<decltype(get), int>);
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired()) { return 252; }
    auto map = std::const_pointer_cast<Map>(
        std::static_pointer_cast<const Map>(ctn_test_maps[0].lock()));
    DROP_GLOBAL
    if (map->size() != 1 || map->begin()->second.object != ctn_test_objects[0].lock() ||
        ctn_test_objects[0].expired()) { return 253; }
    CLEAR;
    if (!ctn_test_objects[0].expired() || !map->empty()) { return 254; }
    for (int call = 0; call < 128; ++call) {
        auto first = std::make_shared<ctnative::identity_object>();
        auto alias = first;
        std::weak_ptr first_lifetime = first;
        if (get(first) != 1 || get(alias) != 1 || map->at(FIRST_KEY).object != first) { return 255; }
        first.reset(); alias.reset();
        if (first_lifetime.expired()) { return 256; }
        auto other = std::make_shared<ctnative::identity_object>();
        std::weak_ptr other_lifetime = other;
        if (get(other) != 1 || map->at(OTHER_KEY).object != other) { return 257; }
        other.reset();
        OVERWRITE_DELETE
        if (!first_lifetime.expired() || other_lifetime.expired()) { return 258; }
        CLEAR;
        if (!other_lifetime.expired() || !map->empty()) { return 259; }
    }
    auto last = std::make_shared<ctnative::identity_object>();
    std::weak_ptr last_lifetime = last;
    if (get(last) != 1) { return 260; }
    last.reset();
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_objects.size() != 2 || ctn_test_maps[0].lock() == ctn_test_maps[1].lock() ||
        map->size() != 1 || map->begin()->second.object != last_lifetime.lock() ||
        ctn_test_objects[1].expired() || ctn_test_objects[1].lock() == last_lifetime.lock()) { return 261; }
    DROP_NEW_GLOBAL
    map.reset();
    DROP_CALLABLES
    if (last_lifetime.expired() || ctn_test_maps[0].expired()) { return 262; }
    get = {};
    if (!last_lifetime.expired() || !ctn_test_maps[0].expired() ||
        ctn_test_maps[1].expired() || ctn_test_objects[1].expired()) { return 263; }
    g_host.reset();
    if (!ctn_test_maps[1].expired() || !ctn_test_objects[1].expired()) { return 264; }
    return 0;
}
"""
    replacements = dict(
        KEY_TYPE="js_num" if scalar else "Object",
        FIRST_KEY="js_num{1}" if scalar else "first",
        OTHER_KEY="js_num{1}" if scalar else "other",
        SAVED_CALLABLES=(
            "auto erase = table->m_erase; auto clear = table->m_clear;" if scalar else ""
        ),
        DROP_CALLABLES="erase = {}; clear = {};" if scalar else "",
        CLEAR="if (clear() != 0) { return 265; }" if scalar else "ctnative::map_clear(map)",
        DROP_GLOBAL=(
            "if (g_key != ctn_test_objects[0].lock()) { return 266; } g_key.reset();"
            if row.get("global_key")
            else ""
        ),
        DROP_NEW_GLOBAL=(
            "if (g_key != ctn_test_objects[1].lock()) { return 267; } g_key.reset();"
            if row.get("global_key")
            else ""
        ),
        OVERWRITE_DELETE=(
            r"""if (!first_lifetime.expired() || erase() != 1 ||
            !other_lifetime.expired() || erase() != 0 || !map->empty()) { return 268; }
        other = std::make_shared<ctnative::identity_object>();
        other_lifetime = other;
        if (get(other) != 1 || map->at(js_num{1}).object != other) { return 269; }
        other.reset();"""
            if scalar
            else r"""auto key = first_lifetime.lock();
        if (!key || map->at(key).object != key || !ctnative::map_delete(map, key)) { return 268; }
        key.reset();"""
        ),
    )
    for before, after in replacements.items():
        changed = changed.replace(before, after)
    return changed
