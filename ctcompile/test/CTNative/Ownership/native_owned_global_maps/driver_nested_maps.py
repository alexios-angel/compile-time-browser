"""driver nested maps: continued from harness_nested_payloads."""

from .harness_nested_payloads import *


def nested_map_observer(source, row):
    if row.get("mixed_child"):
        return mixed_child_observer(source, row)
    observed = source + """
(function() {
    const get = host.slot.get;
    host = {};
    const seen = [], original = Map.prototype.METHOD;
    Map.prototype.METHOD = function(key, value) {
        const result = original.call(this, key, value);
        if (ITEM instanceof Map) { seen.push([this, key, ITEM]); }
        return result;
    };
    let ok = get(17) === 17 && get(-3) === -3;
    Map.prototype.METHOD = original;
    const stride = STRIDE;
    ok = ok && seen.length === stride * 2 && seen[0][0] === seen[stride][0] &&
         seen[0][2] IDENTITY seen[stride][2] && seen[0][2].get('value') === FIRST_VALUE &&
         seen[stride][2].get('value') === -3 && seen[0][0].size === OUTER_SIZE;
    DISTINCT
    for (let i = 0; i < 128; ++i) {
        const value = i % 2 === 0 ? -i : i + 0.5;
        const answer = get(value);
        if (typeof answer !== 'number' || answer !== value) { ok = false; }
        REUSE_CHECK
    }
    RECREATE
    trace = ok ? 1 : 0;
})();
"""
    distinct = (
        ""
        if row["children"] == 1
        else """ok = ok && seen[0][2] !== seen[1][2] &&
        seen[1][2] === seen[2][2] && seen[stride + 1][2] === seen[stride + 2][2];"""
    )
    if row["repeated"]:
        distinct = "ok = ok && seen[0][2] === seen[1][2] && seen[2][2] === seen[3][2];"
    if row.get("alias"):
        distinct = """ok = ok && seen[0][2] !== seen[1][2] && seen[1][2] === seen[3][2] &&
        seen[1][2].get('value') === undefined;"""
    reused = row["reused"]
    recreate = (
        """const outer = seen[0][0], saved = seen[0][2];
    outer.clear(); saved.set('value', 47);
    ok = ok && get(19) === 19 && outer.size === 1 && outer.get(1) !== saved &&
         saved.get('value') === 47;"""
        if reused
        else ""
    )
    if row["separate"]:
        observed = (
            observed.replace(
                "const get = host.slot.get;", "const set = host.slot.set, get = host.slot.get;"
            )
            .replace(
                "get(17) === 17 && get(-3) === -3",
                "set(17) === 17 && get() === 17 && set(-3) === -3 && get() === -3",
            )
            .replace(
                "const answer = get(value);", "const answer = set(value) === value ? get() : NaN;"
            )
        )
        recreate = """const outer = seen[0][0], saved = seen[0][2];
    outer.clear(); saved.set('value', 47);
    ok = ok && get() === 0 && set(19) === 19 && get() === 19 && outer.size === 1 &&
         outer.get(1) !== saved && saved.get('value') === 47;"""
    if row.get("nullable"):
        observed = observed.replace(
            "const set = host.slot.set, get = host.slot.get;",
            "const set = host.slot.set, get = host.slot.get, poison = host.slot.poison;",
        )
        recreate = """const poisoned = seen[0][0].get(1);
    poison();
    ok = ok && get() === undefined && seen[0][0].size === 1 &&
         (seen[0][0].get(1) !== poisoned) === REPLACEMENT &&
         poisoned.get('value') === POISONED_VALUE;
    set(29);
    """ + recreate
        recreate = recreate.replace("REPLACEMENT", str(row["replacement"]).lower()).replace(
            "POISONED_VALUE", "127.5" if row["replacement"] else "undefined"
        )
    if row["previous"]:
        observed = (
            observed.replace("get(17) === 17 && get(-3) === -3", "get(17) === 41 && get(-3) === 17")
            .replace(
                "for (let i = 0; i < 128; ++i)",
                "let previous = -3;\n    for (let i = 0; i < 128; ++i)",
            )
            .replace(
                "answer !== value) { ok = false; }",
                "answer !== previous) { ok = false; }\n" "        previous = value;",
            )
        )
        recreate = recreate.replace(
            "    outer.clear();",
            """    saved.clear();
    ok = ok && get(13) === 13 && outer.get(1) === saved && saved.get('value') === 13;
    outer.clear();""",
        )
    if row["dynamic"]:
        observed = (
            observed.replace(
                "const set = host.slot.set, get = host.slot.get;",
                "const write = host.slot.set, read = host.slot.get, remove = host.slot.remove;\n"
                "    const set = value => write(value, 'value'), get = () => read('value');",
            )
            .replace("typeof answer !== 'number' || answer !== value", "answer !== (value || null)")
            .replace(
                "        REUSE_CHECK",
                """        REUSE_CHECK
        const key = 'caller-' + i;
        if (write(value, key) !== value || read(key) !== (value || null) ||
            remove(key) !== 0 || read(key) !== null) { ok = false; }""",
            )
            .replace(
                "    RECREATE",
                """    const outer = seen[0][0], saved = seen[0][2];
    ok = ok && read('missing') === null && write(7, 'other') === 7 && read('other') === 7;
    remove('value');
    ok = ok && get() === null && read('other') === 7 && outer.size === 1;
    remove('other');
    ok = ok && get() === null && saved.size === 0 && outer.size === 0;
    ok = ok && write(0, 'empty') === 0 && read('empty') === null;
    remove('empty');
    ok = ok && outer.size === 0;
    saved.set('value', 47);
    ok = ok && set(19) === 19 && get() === 19 && outer.size === 1 &&
         outer.get(1) !== saved && saved.get('value') === 47;""",
            )
        )
    if row.get("alias"):
        recreate = recreate.replace("outer.size === 1", "outer.size === 2")
    stride = (
        2
        if row["repeated"] or row["dynamic"] or row.get("alias")
        else 1 if row["children"] == 1 else 3
    )
    return (
        observed.replace("STRIDE", str(stride))
        .replace("OUTER_SIZE", "2" if row.get("alias") else "1" if row["retained"] else "0")
        .replace("DISTINCT", distinct)
        .replace("METHOD", "get" if reused else "set")
        .replace("ITEM", "result" if reused else "value")
        .replace("IDENTITY", "===" if reused else "!==")
        .replace("FIRST_VALUE", "-3" if reused else "17")
        .replace(
            "REUSE_CHECK",
            "if (seen[0][2].get('value') !== value) { ok = false; }" if reused else "",
        )
        .replace("RECREATE", recreate)
    )


def nested_map_lifetime_cpp(cpp, row):
    # Reuse the existing weak allocation observer; generated ownership is unchanged.
    if row.get("caller_payload"):
        return caller_payload_lifetime_cpp(cpp, row)
    if row.get("mixed_child"):
        return mixed_child_lifetime_cpp(cpp, row)
    if row.get("nullable"):
        return nested_map_mutation_lifetime_cpp(cpp, row)
    changed = instrument_leaf_objects(cpp, allocations=0) + r"""
int main() {
    using Child = ctnative::number_map<std::string>;
    using Outer = ctnative::map_storage<js_num, std::shared_ptr<Child>>;
    constexpr std::size_t children = CHILDREN;
    constexpr bool retained = RETAINED;
    constexpr bool reused = REUSED;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1 + children) { return 270; }
    auto owner = g_host;
    auto table = owner->slot;
    auto get = table->m_get;
    static_assert(std::is_same_v<decltype(get), std::function<ctnative::js_num(ctnative::js_num)>>);
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired()) { return 271; }
    auto outer = std::const_pointer_cast<Outer>(
        std::static_pointer_cast<const Outer>(ctn_test_maps[0].lock()));
    if (outer->size() != (retained ? 1U : 0U)) { return 272; }
    std::shared_ptr<Child> saved;
    std::weak_ptr<Child> saved_lifetime;
    if constexpr (retained) {
        saved = outer->at(js_num{1}); saved_lifetime = saved;
        if (saved != ctn_test_maps[1].lock() || saved->at("value") != 41) { return 273; }
    }
    for (int call = 0; call < 128; ++call) {
        const auto before = ctn_test_maps.size();
        const js_num value = call % 2 == 0 ? -call : call + 0.5;
        if (get(value) != value ||
            ctn_test_maps.size() != before + (reused ? 0U : children)) { return 274; }
        for (std::size_t index = before; index < ctn_test_maps.size(); ++index) {
            if (ctn_test_maps[index].expired() == retained) { return 275; }
        }
        if constexpr (retained) {
            auto child = outer->at(js_num{1});
            if ((reused ? child != saved : child == saved) || child != ctn_test_maps.back().lock() ||
                child->at("value") != value || saved->at("value") != (reused ? value : 41)) {
                return 276;
            }
        }
    }
    ctnative::map_clear(outer);
    if constexpr (retained) {
        if (outer->size() != 0U || saved_lifetime.expired() ||
            (!reused && !ctn_test_maps.back().expired())) { return 277; }
        ctnative::map_set(saved, std::string{"value"}, js_num{47});
        if (ctnative::map_get_present(saved, std::string{"value"}) != 47) { return 278; }
        saved.reset();
        if (!saved_lifetime.expired()) { return 279; }
    }
    const auto cleared = ctn_test_maps.size();
    if (get(19) != 19 || ctn_test_maps.size() != cleared + children) { return 280; }
    const auto next = ctn_test_maps.size();
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != next + children + 1 ||
        ctn_test_maps[0].lock() == ctn_test_maps[next].lock()) { return 281; }
    outer.reset();
    if (ctn_test_maps[0].expired() || get(23) != 23) { return 282; }
    get = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[next].expired()) { return 283; }
    g_host.reset();
    for (const auto & map : ctn_test_maps) {
        if (!map.expired()) { return 284; }
    }
    return 0;
}
"""
    if row["separate"]:
        changed = (
            changed.replace(
                "auto get = table->m_get;", "auto get = table->m_get;\n    auto set = table->m_set;"
            )
            .replace(
                "static_assert(std::is_same_v<decltype(get), std::function<ctnative::js_num(ctnative::js_num)>>);",
                "static_assert(std::is_same_v<decltype(get), std::function<ctnative::js_num()>>);\n"
                "    static_assert(std::is_same_v<decltype(set), std::function<ctnative::js_num(ctnative::js_num)>>);",
            )
            .replace("get(value) != value", "set(value) != value || get() != value")
            .replace(
                "const auto cleared = ctn_test_maps.size();",
                "if (get() != 0) { return 285; }\n    const auto cleared = ctn_test_maps.size();",
            )
            .replace("get(19) != 19", "set(19) != 19 || get() != 19")
            .replace("get(23) != 23", "set(23) != 23 || get() != 23")
            .replace(
                "get = {};",
                "get = {};\n    if (ctn_test_maps[0].expired() || set(29) != 29) "
                "{ return 286; }\n    set = {};",
            )
        )
    if row["previous"]:
        changed = (
            changed.replace(
                "for (int call = 0; call < 128; ++call)",
                "js_num previous = 41;\n    for (int call = 0; call < 128; ++call)",
            )
            .replace("get(value) != value", "get(value) != previous")
            .replace(
                "        for (std::size_t index = before;",
                "        previous = value;\n        for (std::size_t index = before;",
            )
            .replace("get(23) != 23", "get(23) != 19")
            .replace(
                "    ctnative::map_clear(outer);",
                """    ctnative::map_clear(saved);
    if (get(13) != 13 || outer->at(js_num{1}) != saved || saved->at("value") != 13) { return 293; }
    ctnative::map_clear(outer);""",
            )
        )
    if row.get("alias"):
        changed = (
            changed.replace("(retained ? 1U : 0U)", "2U")
            .replace(
                "child != ctn_test_maps.back().lock()",
                "(child != ctn_test_maps[1].lock() || outer->at(js_num{2}) != ctn_test_maps[2].lock() || "
                "outer->at(js_num{2})->size() != 0U)",
            )
            .replace(
                "    ctnative::map_clear(outer);",
                "    ctnative::map_clear(outer);\n"
                "    if (!ctn_test_maps[2].expired()) { return 294; }",
            )
            .replace(
                "get(19) != 19",
                "get(19) != 19 || outer->size() != 2U || "
                "outer->at(js_num{1}) == outer->at(js_num{2})",
            )
        )
    if row["previous"] or row.get("alias"):
        changed = changed.replace(
            "std::function<ctnative::js_num(ctnative::js_num)>",
            "std::function<ctnative::nullable_scalar(ctnative::js_num)>",
        )
        changed = re.sub(
            r"\bget\((value|13|19|23)\)", r"ctnative::global_number(get(\1)).value()", changed
        )
        if row["expected_trace"] is not True:
            changed = changed.replace(
                "    auto owner = g_host;",
                "    static_assert(std::is_same_v<decltype(g_trace), ctnative::nullable_scalar>);\n"
                "    if (ctnative::global_number(g_trace).value() != 41) { return 310; }\n"
                "    auto owner = g_host;",
            )
    if row["dynamic"]:
        changed = (
            changed.replace(
                "auto set = table->m_set;",
                """auto set = table->m_set;
    auto remove = table->m_remove;
    using Result = ctnative::nullable_scalar;
    const auto matches = [](Result result, js_num value) {
        return value == 0 ? result.tag == Result::kind::null
            : result.tag == Result::kind::number && result.value == value;
    };""",
            )
            .replace("std::function<ctnative::js_num()>", "std::function<Result(std::string)>")
            .replace(
                "std::function<ctnative::js_num(ctnative::js_num)>",
                "std::function<ctnative::js_num(ctnative::js_num, std::string)>",
            )
            .replace(
                "set(value) != value || get() != value",
                'set(value, "value") != value || !matches(get("value"), value)',
            )
            .replace("get() != 0", 'get("value").tag != Result::kind::null')
            .replace(
                "set(19) != 19 || get() != 19",
                'set(19, "value") != 19 || !matches(get("value"), 19)',
            )
            .replace(
                "set(23) != 23 || get() != 23",
                'set(23, "value") != 23 || !matches(get("value"), 23)',
            )
            .replace("set(29) != 29", 'set(29, "value") != 29')
            .replace(
                "        for (std::size_t index = before;",
                """        const std::string spelling = "caller-" + std::to_string(call);
        std::string key = spelling;
        if (set(value, key) != value) { return 291; }
        key.assign(key.size(), 'x');
        if (!matches(get(spelling), value) || remove(spelling) != 0 ||
            get(spelling).tag != Result::kind::null) { return 292; }
        for (std::size_t index = before;""",
            )
            .replace(
                "    ctnative::map_clear(outer);",
                """    if (get("missing").tag != Result::kind::null || set(7, "other") != 7 ||
        !matches(get("other"), 7) || remove("value") != 0 ||
        get("value").tag != Result::kind::null || !matches(get("other"), 7) ||
        outer->size() != 1U) { return 287; }
    if (remove("other") != 0 || get("value").tag != Result::kind::null ||
        saved->size() != 0U || outer->size() != 0U || saved_lifetime.expired()) { return 288; }
    if (set(0, "empty") != 0 || get("empty").tag != Result::kind::null ||
        remove("empty") != 0 || outer->size() != 0U) { return 289; }
    ctnative::map_clear(outer);""",
            )
            .replace(
                "    set = {};",
                "    set = {};\n    if (ctn_test_maps[0].expired()) { return 290; }\n    remove = {};",
            )
        )
    generated, separator, observer = changed.rpartition("\nint main() {\n")
    if not separator:
        raise RuntimeError("nested Map lifetime observer lost its main function")
    numeric_get = not (row["previous"] or row.get("alias") or row["dynamic"])
    observer = re.sub(
        r"\b(get|set)\((value|[0-9]+)([^()\n]*)\)",
        lambda match: f"{match[1]}(ctnative::js_num{{"
        + (match[2] if match[2] == "value" else match[2] + ".0")
        + "}"
        + match[3]
        + ")"
        + (".value()" if match[1] == "set" or numeric_get else ""),
        observer,
    )
    if numeric_get:
        observer = observer.replace("get()", "get().value()")
    observer = re.sub(r'\bremove\((spelling|"[^"\n]*")\)', r"remove(\1).value()", observer)
    changed = generated + separator + observer
    return (
        changed.replace("CHILDREN", str(row["children"]))
        .replace("RETAINED", str(row["retained"]).lower())
        .replace("REUSED", str(row["reused"]).lower())
    )


def nested_map_mutation_lifetime_cpp(cpp, row):
    # Call the real sibling after publication; neither its tag nor its child
    # lifetime can be inferred from the initial Number observation.
    changed = instrument_leaf_objects(cpp, allocations=0) + r"""
int main() {
    using Child = ctnative::number_map<std::string>;
    using Outer = ctnative::map_storage<js_num, std::shared_ptr<Child>>;
    using Result = ctnative::nullable_scalar;
    constexpr bool replacement = REPLACEMENT;
    constexpr std::size_t initial_maps = INITIAL_MAPS;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != initial_maps) { return 295; }
    auto owner = g_host;
    auto table = owner->slot;
    auto get = table->m_get;
    auto set = table->m_set;
    auto poison = table->m_poison;
    static_assert(std::is_same_v<decltype(get), std::function<Result()>>);
    static_assert(std::is_same_v<decltype(set), std::function<ctnative::js_num(ctnative::js_num)>>);
    static_assert(std::is_same_v<decltype(poison), std::function<ctnative::js_num()>>);
    static_assert(std::is_same_v<decltype(g_trace), Result>);
    if (get().tag != Result::kind::INITIAL_TAG ||
        g_trace.tag != Result::kind::INITIAL_TAG) { return 296; }
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired()) { return 297; }
    auto outer = std::const_pointer_cast<Outer>(
        std::static_pointer_cast<const Outer>(ctn_test_maps[0].lock()));
    for (int call = 0; call < 128; ++call) {
        const auto before = ctn_test_maps.size();
        const js_num value = call % 2 == 0 ? -call : call + 0.5;
        if (set(ctnative::js_num{value}).value() != value || ctnative::global_number(get()).value() != value ||
            ctn_test_maps.size() != before + 1) { return 298; }
        auto saved = outer->at(js_num{1});
        std::weak_ptr saved_lifetime = saved;
        if (poison().value() != 0 || get().tag != Result::kind::undefined || outer->size() != 1U ||
            ctn_test_maps.size() != before + 1 + (replacement ? 1U : 0U) ||
            (outer->at(js_num{1}) != saved) != replacement) { return 299; }
        const auto prior = ctnative::map_get(saved, std::string{"value"});
        if (replacement ? prior.tag != Result::kind::number || prior.value != value
                        : prior.tag != Result::kind::undefined) { return 300; }
        saved.reset();
        if (saved_lifetime.expired() != replacement) { return 301; }
    }
    ctnative::map_clear(outer);
    if (ctnative::global_number(get()).value() != 0 || set(ctnative::js_num{19.0}).value() != 19 ||
        ctnative::global_number(get()).value() != 19) { return 302; }
    auto saved = outer->at(js_num{1});
    std::weak_ptr saved_lifetime = saved;
    ctnative::map_clear(outer);
    if (saved_lifetime.expired() || saved->at("value") != 19 ||
        ctnative::global_number(get()).value() != 0 || set(ctnative::js_num{23.0}).value() != 23 ||
        ctnative::global_number(get()).value() != 23 || outer->at(js_num{1}) == saved) { return 303; }
    saved.reset();
    if (!saved_lifetime.expired()) { return 304; }
    const auto next = ctn_test_maps.size();
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != next + initial_maps ||
        ctn_test_maps[0].lock() == ctn_test_maps[next].lock() ||
        ctnative::global_number(get()).value() != 23) { return 305; }
    outer.reset();
    get = {};
    if (ctn_test_maps[0].expired() || set(ctnative::js_num{31.0}).value() != 31) { return 306; }
    set = {};
    if (ctn_test_maps[0].expired() || poison().value() != 0) { return 307; }
    poison = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[next].expired()) { return 308; }
    g_host.reset();
    for (const auto & map : ctn_test_maps) {
        if (!map.expired()) { return 309; }
    }
    return 0;
}
"""
    initial_maps = 2 + row["replacement"] * (1 + (row["expected_trace"] == "undefined"))
    return (
        changed.replace("REPLACEMENT", str(row["replacement"]).lower())
        .replace("INITIAL_MAPS", str(initial_maps))
        .replace("INITIAL_TAG", "undefined" if row["expected_trace"] == "undefined" else "number")
    )


def nested_map_census(args, ir, name, row):
    raw = (args.work / f"{name}.raw.mlir").read_text()
    prepared = ir.read_text()
    if len(boundary.FUNCTION.findall(raw)) != row["functions"]:
        raise RuntimeError(f"{name}: changed source function census")
    for text in (raw, prepared):
        if len(source_calls(text)) != row["calls"]:
            raise RuntimeError(f"{name}: changed source call census")
    for operation in (
        "create_object",
        "construct",
        "get_property",
        "set_property",
        "load_global",
        "store_global",
    ):
        if raw.count(operation) != prepared.count(operation):
            raise RuntimeError(f"{name}: preparation changed {operation} census")


def nested_map_preserved(original, output, name, prepared=False):
    if prepared:
        # Successful ownership lifts the environment into each direct call. Check
        # that actual receiver/callee/capture edges and every source effect survive.
        calls = re.findall(
            r"^\s*(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\) "
            r"\{ctnative\.stored_call = 1 : i32\}",
            output,
            re.M,
        )
        targets = (
            (["fn$3", "fn$4", "fn$5"] if "before" in name else ["fn$5", "fn$3", "fn$4"])
            if "cross_" in name
            else ["fn$3"]
        )
        arities = [4, 5, 4] if "cross_" in name else [5]
        previous = "previous_" in name
        if previous:
            targets = ["fn$4", "fn$3"] if "before" in name else ["fn$3", "fn$4"]
            arities = [5, 4]
        entry = output.split("\n  }", 1)[0]
        reads = dict(re.findall(r"(%[-\w.$]+) = ctjs\.get_property (%[-\w.$]+)\[", entry))
        captures = dict(re.findall(r"(%[-\w.$]+) = ctjs\.load_upvalue (%[-\w.$]+)\[0\]", entry))
        actuals = [args.split(", ") for _, _, args in calls]
        if (
            len(source_calls(original)) != len(source_calls(output))
            or [target for _, target, _ in calls] != targets
            or [len(args) for args in actuals] != arities
            or any(
                reads.get(args[2]) != args[0] or captures.get(args[3]) != args[2]
                for args in actuals
            )
            or f'ctjs.store_global "trace", {calls[0 if previous else -1][0]}' not in output
        ):
            raise RuntimeError(f"{name}: nullable refusal lost a prepared source call edge")
        if previous or "cross_mixed_" in name:
            number = actuals[0 if previous else 1][-1]
            if f"{number} = ctjs.constant #ctjs.number<4630967054332067840>" not in entry:
                raise RuntimeError(f"{name}: mixed refusal lost its original Number actual")
        for op in (
            "ctjs.create_object",
            "ctjs.construct",
            "ctjs.get_property",
            "ctjs.set_property",
            "ctjs.load_global",
            "ctjs.store_global",
            "ctjs.compare",
            "ctjs.unary",
            "ctjs.binary",
            "ctjs.truthy",
            "scf.if",
            "scf.yield",
        ):
            pattern = r"^\s*(?:%[-\w.$]+(?::\d+)? = )?" + re.escape(op) + r"\b"
            if len(re.findall(pattern, original, re.M)) != len(re.findall(pattern, output, re.M)):
                raise RuntimeError(f"{name}: nullable refusal changed source {op} census")
        return
    check_call_preservation(original, output, name)
    pattern = (
        r"^\s*((?:%[-\w.$]+(?::\d+)? = )?(?:ctjs\.(?:create_object|construct|"
        r"get_property|set_property|load_global|store_global|compare|unary|binary|truthy)|"
        r"scf\.(?:if|yield))\b[^\n{]*)"
    )
    if [line.strip() for line in re.findall(pattern, original, re.M)] != [
        line.strip() for line in re.findall(pattern, output, re.M)
    ]:
        raise RuntimeError(f"{name}: refusal changed original nested Map or branch edges")


def check_nested_maps(args, node, reference, compilers, nm):
    cases = nested_map_cases()
    observations = mutations = 0

    def observe(name, source, expected, extra=()):
        js = args.work / f"{name}-observed.js"
        js.write_text(source)
        result = host.run([str(reference), str(js)]) if reference else None
        values = {"trace": expected, **dict(extra)}
        expected_text = "".join(
            f"{key}={str(value).lower()}\n" for key, value in sorted(values.items())
        )
        kinds = [
            (
                "boolean"
                if isinstance(value, bool)
                else value if value in ("null", "undefined") else "number"
            )
            for value in values.values()
        ]
        types = (
            "("
            + ", ".join(
                f"{kinds.count(tag)} {tag}"
                for tag in ("number", "boolean", "string", "null", "undefined")
            )
            + ")"
        )
        if (
            host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), json.dumps(sorted(values))]).stdout
            != expected_text
            or result
            and (result.stdout != expected_text or types not in result.stderr)
        ):
            raise RuntimeError(f"{name}: typed source observation changed")

    for name, row in cases.items():
        extra = row.get("observations", {})
        observe(name, row["source"], row["expected_trace"], extra)
        observations += 1 + len(extra)
        if "poison" in row:
            mutation, expected = row["poison"]
            assert row["source"].count(mutation) == 1, (name, mutation)
            observer = (
                "\nhost.slot.set(73); host.slot.poison();\n"
                "trace = host.slot.get() === " + expected + " ? 1 : 0;\n"
            )
            observe(name + "-poisoned", row["source"] + observer, 1)
            observe(name + "-mutation-removed", row["source"].replace(mutation, "") + observer, 0)
            observations += 2
            mutations += 1
        if "previous_poison" in row:
            mutation, expected = row["previous_poison"]
            assert row["source"].count(mutation) == 1, (name, mutation)
            observer = "\ntrace = host.slot.get(73) === " + expected + " ? 1 : 0;\n"
            observe(name + "-poisoned", row["source"] + observer, 1)
            observe(name + "-mutation-removed", row["source"].replace(mutation, "") + observer, 0)
            observations += 2
            mutations += 1
        if not row["admitted"] and not row["previous"] and not row["dynamic"]:
            continue
        observer = caller_payload_observer if row.get("caller_payload") else nested_map_observer
        observed = observer(row["source"], row)
        observe(name + "-future", observed, 1, extra)
        observations += 1 + len(extra)
        readback = "again.get('value')" if row["repeated"] else "saved.get('value')"
        replacements = [
            ("t.get(1).get('value')" if row["separate"] else readback, "41"),
            (".set('value', value);", ".set('value', 0);"),
        ]
        if row.get("alias"):
            replacements.append(("t.set(2, new Map);", "t.set(2, t.get(1));"))
        elif row["children"] == 2:
            replacements.append(("second = new Map;", "second = first;"))
        if row["retained"]:
            replacements.append(
                ("t.set(1, child);", "t.set(1, child); t.clear();")
                if row["separate"]
                else ("return " + readback, "t.clear(); return " + readback)
            )
        if row["separate"]:
            replacements.append(("return value;", "return 0;"))
        if row["reused"]:
            replacements.append(("t.has(1) || t.set(1, new Map);", "t.set(1, new Map);"))
        if row["repeated"]:
            replacements.append(
                ("const again = t.get(1);", "t.set(1, new Map); const again = t.get(1);")
            )
        if row["previous"]:
            readback = "return prior === void 0 ? saved.get('value') : prior;"
            replacements = [
                (readback, "return value;"),
                (readback, "return prior;"),
                ("const prior = saved.get('value');", "const prior = void 0;"),
                ("saved.set('value', value);", "saved.set('value', 0);"),
                (readback, "t.clear(); " + readback),
                ("t.has(1) || t.set(1, new Map);", "t.set(1, new Map);"),
            ]
        if row["dynamic"]:
            readback = "return t.get(1).get(key) || null;"
            replacements = [
                (readback, "return 41;"),
                (readback, "return t.get(1).get(key);"),
                ("t.get(1).set(key, value);", "t.get(1).set(key, 0);"),
                ("t.has(1) || t.set(1, new Map);", "t.set(1, new Map);"),
                ("saved.delete(key);", "saved.has(key);"),
                ("if (!saved.size) { t.delete(1); }", ""),
            ]
        if row.get("caller_payload"):
            replacements = [
                ("t.set(1, e);", "t.set(1, {});"),
                ("t.set(1, e);", "t.set(1, 1);"),
                ("t.set(1, e);", "t.has(1);"),
                ("t.delete(1)", "t.has(1)"),
                ("clear() { t.clear();", "clear() { t.size;"),
                ("{value: 64}", "{value: 0}"),
            ]
            if row["global_alias"]:
                replacements[-1] = ("alias.value = 65;", "alias.value = 0;")
        if row.get("mixed_child"):
            replacements = [
                ("t.get(1).get('value')", "64"),
                ("t.get(1).set('value', value);", "t.get(1).set('value', 0);"),
            ]
            if "child_mutation" in row:
                replacements.append(
                    (
                        "poison() { " + row["child_mutation"] + " return 0; }",
                        "poison() { return 0; }",
                    )
                )
            if row.get("mixed_child_kind") == "returned":
                replacements.append((" || null;", ";"))
        replacements.extend(row.get("entry_field_mutations", ()))
        for index, (old, replacement) in enumerate(replacements):
            assert row["source"].count(old) == 1, (name, old)
            blinded = args.work / f"{name}-blinded-{index}.js"
            blinded.write_text(observer(row["source"].replace(old, replacement), row))
            result = subprocess.run(
                [node, "-e", boundary.NODE, str(blinded)],
                capture_output=True,
                text=True,
                timeout=30,
            )
            if not result.returncode and result.stdout == "trace=1\n":
                raise RuntimeError(f"{name}: observer cannot distinguish {replacement}")
            mutations += 1

    def check_case(item):
        name, row = item

        def observed_contract(ir, label):
            config = contract(args, ir, label)
            if row.get("observations"):
                value = json.loads(config.read_text())
                value["observations"] = sorted(["trace", *row["observations"]])
                config.write_text(json.dumps(value, indent=2) + "\n")
            return config

        _, ir, functions = boundary.prepare(args, name, row["source"])
        assert functions == row["functions"]
        nested_map_census(args, ir, name, row)
        config = observed_contract(ir, name)
        for policy, options in (("default", ""), ("disabled", "optimize=false")):
            label = name + "-" + policy
            if not row["admitted"]:
                output = owned.lower(args, ir, label, config, options=options, cleanup=False)
                methods.census(output, functions, label, admitted=0)
                if row.get("refusal") and row["refusal"] not in output.read_text():
                    raise RuntimeError(f"{label}: lost independent mixed carrier refusal")
                nested_map_preserved(ir.read_text(), output.read_text(), label, row["owner"])
                if ("ctnative.host_owner_proved = true" in output.read_text()) != row["owner"]:
                    raise RuntimeError(f"{label}: nullable ownership outcome changed")
            else:
                output = owned.lower(args, ir, label, config, options=options)
                text = methods.census(output, functions, label, admitted=functions)
                if "ctnative.host_owner_proved = true" not in text:
                    raise RuntimeError(f"{label}: lost captured child ownership")
                if policy == "default":
                    default = output
                elif output.read_text() != default.read_text():
                    raise RuntimeError(f"{label}: child proof depends on optimization policy")
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
            nested_map_preserved(forged.read_text(), stale.read_text(), label)
            fresh = observed_contract(forged, label + "-forged")
            checked = owned.lower(
                args, forged, label + "-fresh", fresh, options=options, cleanup=row["admitted"]
            )
            methods.census(checked, functions, label, admitted=functions if row["admitted"] else 0)
            if not row["admitted"]:
                if row.get("refusal") and row["refusal"] not in checked.read_text():
                    raise RuntimeError(f"{label}: forged reports changed mixed carrier refusal")
                nested_map_preserved(forged.read_text(), checked.read_text(), label, row["owner"])
                if ("ctnative.host_owner_proved = true" in checked.read_text()) != row["owner"]:
                    raise RuntimeError(f"{label}: forged presence changed nullable ownership")
            elif comparable_provenance(
                host.run([args.translate, "--mlir-to-cpp", str(checked)]).stdout, forged
            ) != comparable_provenance(
                host.run([args.translate, "--mlir-to-cpp", str(output)]).stdout, ir
            ):
                raise RuntimeError(f"{label}: forged leaf/presence facts changed nested C++")
        if not row["admitted"]:
            return
        values = {"trace": row["expected_trace"], **row.get("observations", {})}
        expected_output = (
            "".join(f"{key}={str(value).lower()}\n" for key, value in sorted(values.items())) * 2
        )
        deduced = args.work / f"{name}.deduced.mlir"
        host.run([args.opt, str(default), "--ctnative-print-deduced", "-o", str(deduced)])
        for mode, native in (("explicit", default), ("deduced", deduced)):
            cpp = host.run([args.translate, "--mlir-to-cpp", str(native)]).stdout
            signature = (
                "std::function<ctnative::js_num(ctnative::js_num, std::string)>"
                if row["dynamic"]
                else (
                    "std::function<ctnative::js_num(ctnative::object_value)>"
                    if row.get("mixed_child") or row.get("caller_payload") and row["mixed"]
                    else (
                        "std::function<ctnative::js_num(std::shared_ptr<ctnative::identity_object>)>"
                        if row.get("caller_payload")
                        else (
                            "std::function<ctnative::nullable_scalar()>"
                            if row.get("nullable")
                            else (
                                "std::function<ctnative::nullable_scalar(ctnative::js_num)>"
                                if row["previous"] or row.get("alias")
                                else "std::function<ctnative::js_num(ctnative::js_num)>"
                            )
                        )
                    )
                )
            )
            if owned.VM.search(cpp) or signature not in cpp:
                raise RuntimeError(f"{name}/{mode}: lost typed standalone nested Map output")
            if (
                row.get("entry_fields")
                and len(re.findall(r"\bctnative::object_get_field_76616c7565\(", cpp))
                != row["entry_fields"]
            ):
                raise RuntimeError(f"{name}/{mode}: lost an evaluated guarded field read")
            generated = args.work / f"{name}.{mode}.cpp"
            generated.write_text(cpp)
            source = args.work / f"{name}.{mode}.lifetime.cpp"
            source.write_text(nested_map_lifetime_cpp(cpp, row))
            for index, compiler in enumerate(compilers):
                binary = source.with_suffix(f".{index}").resolve()
                host.run([compiler, *owned.FLAGS, str(source), "-o", str(binary)])
                if (
                    owned.VM.search(host.run([nm, "-C", str(binary)]).stdout)
                    or host.run([str(binary)]).stdout != expected_output
                ):
                    raise RuntimeError(f"{name}/{mode}: standalone child lifetime mismatch")
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
                env={
                    **os.environ,
                    "ASAN_OPTIONS": "detect_stack_use_after_return=1:detect_leaks=1",
                    "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1",
                },
            )
            if result.returncode or result.stdout != expected_output or result.stderr:
                raise RuntimeError(
                    f"{name}/{mode}: sanitized child lifetime failed\n"
                    f"{result.returncode}: {result.stdout}{result.stderr}"
                )
        if name in {
            "nested_map_retained",
            "nested_map_distinct_saved",
            "nested_map_conditional_initialize",
            "nested_map_cross_invocation",
            "nested_map_repeated_lookup",
            "nested_map_cross_inverted_guard",
            "nested_map_conditional_unknown_contents",
            "nested_map_dynamic_nullable",
            "nested_map_previous_observed",
            "nested_map_dynamic_nullable_observed",
            "nested_map_caller_payload_fields_observed",
            "nested_map_caller_payload_number_last_observed",
            "nested_map_mixed_child_identity",
            "nested_map_mixed_child_replace",
            "nested_map_mixed_child_returned_identity",
            "nested_map_mixed_child_returned_field",
            "nested_map_returned_fields_same",
        }:
            check_budgets(args, ir, config, name, functions=functions)

    for item in cases.items():
        check_case(item)
    positives = sum(row["admitted"] for row in cases.values())
    print(
        f"nested Maps: {positives} native programs, {len(cases) - positives} refusals, "
        f"{observations} typed observations, {mutations} distinguishing mutations"
    )
