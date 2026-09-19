"""harness nested payloads: continued from sources_nested_maps."""

from .sources_nested_maps import *


def caller_payload_observer(source, row):
    return (
        source
        + """
(function() {
    const get = host.slot.get, erase = host.slot.erase, clear = host.slot.clear;
    host = {};
    const original = Map.prototype.has;
    let map;
    Map.prototype.has = function(key) { map = this; return original.call(this, key); };
    let ok = get(key) === 1 && key.value === INITIAL_FIELD;
    Map.prototype.has = original;
    let saved = map.get(1);
    ok = ok && saved === key;
    key.value = 73;
    ok = ok && saved.value === 73 && erase() === 1 && map.size === 0;
    key.value = 74;
    ok = ok && saved.value === 74 && clear() === 0 && saved === key;
    for (let i = 0; i < 128; ++i) {
        const value = i % 2 === 0 ? -i : i + 0.5;
        const item = {value}, alias = item, other = {value: value + 1};
        ok = ok && get(item) === 1 && get(alias) === 1 && map.get(1) === item;
        saved = map.get(1);
        item.value = value + 2;
        ok = ok && saved.value === value + 2 && get(other) === 1 && map.get(1) === other;
        ok = ok && saved === item && saved !== other && saved.value === value + 2;
        ok = ok && erase() === 1 && erase() === 0 && get(other) === 1 && clear() === 0;
        ok = ok && map.size === 0 && other.value === value + 1 && saved.value === value + 2;
        MIXED
    }
    trace = ok ? 1 : 0;
})();
""".replace("INITIAL_FIELD", str(row["expected_trace"])).replace(
            "MIXED",
            (
                """
        ok = ok && get(value) === 1 && map.get(1) === value && erase() === 1 &&
             map.size === 0 && saved.value === value + 2;"""
                if row["mixed"]
                else ""
            ),
        )
    )


def caller_payload_lifetime_cpp(cpp, row):
    changed = object_payload_lifetime_cpp(cpp, "object_argument_scalar_key_payload")
    changed = changed.replace(
        "    auto owner = g_host;",
        """    if (g_key->field_76616c7565.tag != ctnative::nullable_scalar::kind::number ||
        g_key->field_76616c7565.value != INITIAL_FIELD) { return 311; }
    auto owner = g_host;""",
    ).replace("INITIAL_FIELD", str(row["expected_trace"]))
    changed = changed.replace(
        "        auto alias = first;",
        """        first->field_76616c7565 = ctnative::nullable_scalar{static_cast<js_num>(call)};
        auto alias = first;""",
    ).replace(
        "        first.reset(); alias.reset();",
        """        first->field_76616c7565 = ctnative::nullable_scalar{static_cast<js_num>(call) + 0.5};
        if (map->at(js_num{1}).object->field_76616c7565.value != call + 0.5) { return 312; }
        first.reset(); alias.reset();""",
    )
    changed = changed.replace(
        "    auto last = std::make_shared<ctnative::identity_object>();",
        """    auto retained = std::make_shared<ctnative::identity_object>();
    retained->field_76616c7565 = ctnative::nullable_scalar{73.0};
    std::weak_ptr retained_lifetime = retained;
    if (get(retained) != 1) { return 313; }
    auto saved = ctnative::map_get(map, js_num{1});
    retained.reset();
    if (erase() != 1 || !map->empty() || retained_lifetime.expired() ||
        saved.object->field_76616c7565.value != 73) { return 314; }
    saved.object->field_76616c7565 = ctnative::nullable_scalar{74.0};
    if (clear() != 0 || saved.object->field_76616c7565.value != 74) { return 315; }
    saved = {};
    if (!retained_lifetime.expired()) { return 316; }
    auto last = std::make_shared<ctnative::identity_object>();
    last->field_76616c7565 = ctnative::nullable_scalar{91.0};""",
    ).replace(
        "    map.reset();",
        """    if (map->begin()->second.object->field_76616c7565.value != 91) { return 317; }
    map.reset();""",
    )
    if row["global_alias"]:
        changed = changed.replace("g_key.reset();", "g_key.reset(); g_alias.reset();")
    if row["mixed"]:
        changed = (
            changed.replace(
                "std::function<js_num(Object)>", "std::function<js_num(ctnative::object_value)>"
            )
            .replace(
                "static_assert(!std::is_invocable_v<decltype(get), int>);",
                "static_assert(std::is_invocable_v<decltype(get), Object>);\n"
                "    static_assert(std::is_invocable_v<decltype(get), js_num>);",
            )
            .replace(
                "    if (map->size() != 1 || map->begin()->second.object != ctn_test_objects[0].lock() ||",
                "    if (get(g_key) != 1) { return 318; }\n"
                "    if (map->size() != 1 || map->begin()->second.object != ctn_test_objects[0].lock() ||",
            )
        )
        # The historical helper drops g_key before its Map observation.
        changed = changed.replace("} g_key.reset();", "}", 1).replace(
            "    if (clear() != 0) { return 265; }",
            "    g_key.reset();\n    if (clear() != 0) { return 265; }",
            1,
        )
        changed = changed.replace(
            "        if (!other_lifetime.expired() || !map->empty()) { return 259; }",
            """        if (!other_lifetime.expired() || !map->empty()) { return 259; }
        const js_num number = call % 2 == 0 ? -call : call + 0.5;
        if (get(number) != 1) { return 319; }
        const auto scalar = ctnative::map_get(map, js_num{1});
        if (scalar.object || scalar.scalar.tag != ctnative::nullable_scalar::kind::number ||
            scalar.scalar.value != number || erase() != 1 || !map->empty()) { return 320; }""",
        )
        if row["number_last"]:
            changed = changed.replace(
                "    if (get(g_key) != 1)",
                """    const auto initial = ctnative::map_get(map, js_num{1});
    if (initial.object || initial.scalar.tag != ctnative::nullable_scalar::kind::number ||
        initial.scalar.value != 7) { return 321; }
    if (get(g_key) != 1)""",
            ).replace(
                "    if (g_key != ctn_test_objects[1].lock())",
                "    if (g_host->slot->m_get(g_key) != 1) { return 322; }\n"
                "    if (g_key != ctn_test_objects[1].lock())",
            )
    return changed


def mixed_child_observer(source, row):
    kind = row.get("mixed_child_kind", "number")
    expected = {
        "number": "value === 64",
        "null": "value === null",
        "undefined": "value === undefined",
        "truthy": "!!value",
        "identity": "true",
        "returned": "true",
    }[kind]
    observed = source + """
(function() {
    const set = host.slot.set, get = host.slot.get;
    const read = value => GET;
    const expected = value => EXPECTED;
    POISON_BINDING
    host = {};
    const original = Map.prototype.get;
    let outer, child;
    Map.prototype.get = function(key) {
        const result = original.call(this, key);
        if (result instanceof Map) { outer = this; child = result; }
        return result;
    };
    let ok = set(64) === 0 && read(64) === expected(64);
    Map.prototype.get = original;
    const detached = child;
    outer.clear();
    detached.set('value', key);
    ok = ok && read(64) === false && set(64) === 0 && outer.get(1) !== detached &&
         detached.get('value') === key && read(64) === expected(64);
    for (let i = 0; i < 128; ++i) {
        const value = i % 2 === 0 ? -i : i + 0.5;
        const item = {value}, alias = item, other = {value: value + 1};
        ok = ok && set(item) === 0 && read(item) === expected(item);
        child = outer.get(1);
        const saved = child.get('value');
        item.value = value + 2;
        ok = ok && saved === alias && saved.value === value + 2;
        ok = ok && set(other) === 0 && child.get('value') === other &&
             read(other) === expected(other) DISTINCT;
        ok = ok && saved === item && saved !== other && saved.value === value + 2;
        ok = ok && set(value) === 0 && child.get('value') === value &&
             read(value) === expected(value) && saved.value === value + 2;
        POISON_CHECK
    }
    NULL_CHECK
    child = outer.get(1);
    child.clear();
    ok = ok && read(64) === MISSING && child.size === 0;
    outer.clear();
    ok = ok && read(64) === false && set(64) === 0 && outer.get(1) !== child &&
         read(64) === expected(64);
    trace = ok ? 1 : 0;
})();
"""
    if row.get("entry_fields"):
        initial = " && ".join(
            f"{name} === {json.dumps(value)}" for name, value in row["observations"].items()
        )
        observed = observed.replace(
            "let ok = set(64)",
            "let ok = " + initial + " && set(64)",
        )
    if kind == "returned":
        observed = observed.replace(
            "const saved = child.get('value');", "const saved = get();"
        ).replace(
            "        POISON_CHECK",
            "        child.delete('value'); child.clear();\n"
            "        ok = ok && saved === item && saved.value === value + 2;",
        )
    return (
        observed.replace(
            "GET",
            (
                "get(value)"
                if kind == "identity"
                else "get() === (value || null)" if kind == "returned" else "get()"
            ),
        )
        .replace("EXPECTED", expected)
        .replace(
            "POISON_BINDING", "const poison = host.slot.poison;" if "child_mutation" in row else ""
        )
        .replace("DISTINCT", "&& read(item) === false" if kind in {"identity", "returned"} else "")
        .replace(
            "POISON_CHECK",
            (
                """const prior = outer.get(1);
        ok = ok && set(64) === 0 && poison() === 0 && read(64) === MISSING &&
             (outer.get(1) !== prior) === REPLACEMENT &&
             prior.get('value') === PRIOR;""".replace(
                    "REPLACEMENT", str(row["replacement"]).lower()
                ).replace("PRIOR", "64" if row["replacement"] else "undefined")
                if "child_mutation" in row
                else ""
            ),
        )
        .replace(
            "NULL_CHECK",
            (
                "ok = ok && set(null) === 0 && read(null) === true;"
                if kind == "null"
                else (
                    "for (const value of [null, false, 0, -0, NaN]) { "
                    "ok = ok && set(value) === 0 && get() === null; }"
                    if kind == "returned"
                    else ""
                )
            ),
        )
        .replace("MISSING", "true" if kind == "undefined" else "false")
    )


def mixed_child_lifetime_cpp(cpp, row):
    kind = row.get("mixed_child_kind", "number")
    changed = instrument_leaf_objects(cpp) + r"""
int main() {
    using Value = ctnative::object_value;
    using Scalar = ctnative::nullable_scalar;
    using Child = ctnative::map_storage<std::string, Value>;
    using Outer = ctnative::map_storage<js_num, std::shared_ptr<Child>>;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != INITIAL_MAPS ||
        ctn_test_objects.size() != 1) { return 330; }
    auto owner = g_host;
    auto table = owner->slot;
    auto set = table->m_set;
    auto get = table->m_get;
    POISON_BINDING
    static_assert(std::is_same_v<decltype(set), std::function<js_num(Value)>>);
    static_assert(std::is_same_v<decltype(get), std::function<bool(GET_SIGNATURE)>>);
    auto read = [&get](Value value) { (void)value; return GET; };
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); g_key.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired() || !ctn_test_objects[0].expired()) { return 331; }
    auto outer = std::const_pointer_cast<Outer>(
        std::static_pointer_cast<const Outer>(ctn_test_maps[0].lock()));
    auto detached = outer->at(js_num{1});
    std::weak_ptr detached_lifetime = detached;
    outer->clear();
    ctnative::map_set(detached, std::string{"value"}, Value{js_num{73}});
    if (read(js_num{64}) || set(js_num{64}) != 0 || outer->at(js_num{1}) == detached ||
        ctnative::map_get(detached, std::string{"value"}).scalar.value != 73 ||
        read(js_num{64}) != NUMBER_64) { return 332; }
    detached.reset();
    if (!detached_lifetime.expired()) { return 333; }
    for (int call = 0; call < 128; ++call) {
        const js_num number = call % 2 == 0 ? -call : call + 0.5;
        auto first = std::make_shared<ctnative::identity_object>();
        first->field_76616c7565 = Scalar{number};
        auto alias = first;
        std::weak_ptr first_lifetime = first;
        if (set(first) != 0 || read(first) != OBJECT_RESULT) { return 334; }
        auto child = outer->at(js_num{1});
        auto saved = ctnative::map_get(child, std::string{"value"});
        first->field_76616c7565 = Scalar{number + 2};
        if (saved.object != alias || saved.object->field_76616c7565.value != number + 2) { return 335; }
        first.reset(); alias.reset();
        auto other = std::make_shared<ctnative::identity_object>();
        other->field_76616c7565 = Scalar{number + 1};
        std::weak_ptr other_lifetime = other;
        if (set(other) != 0 || child->at("value").object != other ||
            read(other) != OBJECT_RESULT DISTINCT) { return 336; }
        other.reset();
        if (set(number) != 0 || !other_lifetime.expired() || first_lifetime.expired() ||
            child->at("value").object || child->at("value").scalar.tag != Scalar::kind::number ||
            child->at("value").scalar.value != number || read(number) != NUMBER_RESULT ||
            saved.object->field_76616c7565.value != number + 2) { return 337; }
        POISON_CHECK
        saved = {};
        if (!first_lifetime.expired()) { return 338; }
    }
    NULL_CHECK
    auto child = outer->at(js_num{1});
    child->clear();
    if (read(js_num{64}) != MISSING || !child->empty()) { return 339; }
    outer->clear();
    if (read(js_num{64}) || set(js_num{64}) != 0 || outer->at(js_num{1}) == child ||
        read(js_num{64}) != NUMBER_64) { return 340; }
    child.reset();
    auto last = std::make_shared<ctnative::identity_object>();
    last->field_76616c7565 = Scalar{91.0};
    std::weak_ptr last_lifetime = last;
    if (set(last) != 0 || read(last) != OBJECT_RESULT) { return 341; }
    auto saved = ctnative::map_get(outer->at(js_num{1}), std::string{"value"});
    last.reset();
    const auto next = ctn_test_maps.size();
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != next + INITIAL_MAPS ||
        ctn_test_objects.size() != 2 || ctn_test_maps[0].lock() == ctn_test_maps[next].lock() ||
        last_lifetime.expired() || saved.object->field_76616c7565.value != 91) { return 342; }
    outer.reset(); set = {}; POISON_DROP
    if (ctn_test_maps[0].expired() || read(saved) != OBJECT_RESULT) { return 343; }
    get = {};
    if (!ctn_test_maps[0].expired() || last_lifetime.expired() ||
        saved.object->field_76616c7565.value != 91) { return 344; }
    saved = {};
    if (!last_lifetime.expired()) { return 345; }
    g_host.reset(); g_key.reset();
    for (const auto & map : ctn_test_maps) {
        if (!map.expired()) { return 346; }
    }
    for (const auto & object : ctn_test_objects) {
        if (!object.expired()) { return 347; }
    }
    return 0;
}
"""
    if kind == "returned":
        changed = (
            changed.replace("std::function<bool(GET_SIGNATURE)>", "std::function<Value()>")
            .replace(
                'auto saved = ctnative::map_get(child, std::string{"value"});',
                "auto saved = get();",
            )
            .replace(
                'auto saved = ctnative::map_get(outer->at(js_num{1}), std::string{"value"});',
                "auto saved = get();",
            )
            .replace(
                "        POISON_CHECK",
                '        ctnative::map_delete(child, std::string{"value"}); ctnative::map_clear(child);\n'
                "        if (first_lifetime.expired() || saved.object->field_76616c7565.value != number + 2) "
                "{ return 352; }",
            )
        )
    return (
        changed.replace("INITIAL_MAPS", "3" if row.get("replacement") else "2")
        .replace(
            "POISON_BINDING", "auto poison = table->m_poison;" if "child_mutation" in row else ""
        )
        .replace("POISON_DROP", "poison = {};" if "child_mutation" in row else "")
        .replace("GET_SIGNATURE", "Value" if kind == "identity" else "")
        .replace(
            "GET",
            (
                "get(value)"
                if kind == "identity"
                else (
                    "ctnative::object_strict_equal(get(), ctnative::object_truthy(value) ? value : Value{Scalar::null()})"
                    if kind == "returned"
                    else "get()"
                )
            ),
        )
        .replace(
            "NUMBER_64", "true" if kind in {"number", "truthy", "identity", "returned"} else "false"
        )
        .replace("OBJECT_RESULT", "true" if kind in {"truthy", "identity", "returned"} else "false")
        .replace(
            "NUMBER_RESULT",
            (
                "true"
                if kind in {"identity", "returned"}
                else (
                    "(number != 0)"
                    if kind == "truthy"
                    else "(number == 64)" if kind == "number" else "false"
                )
            ),
        )
        .replace("DISTINCT", "|| read(saved)" if kind in {"identity", "returned"} else "")
        .replace(
            "POISON_CHECK",
            (
                """if (set(js_num{64}) != 0 || poison() != 0 ||
            read(js_num{64}) != MISSING || (outer->at(js_num{1}) != child) != REPLACEMENT) { return 348; }
        const auto prior = ctnative::map_get(child, std::string{"value"});
        if (PRIOR) { return 349; }""".replace(
                    "REPLACEMENT", str(row["replacement"]).lower()
                ).replace(
                    "PRIOR",
                    (
                        "prior.object || prior.scalar.tag != Scalar::kind::number || prior.scalar.value != 64"
                        if row["replacement"]
                        else "prior.object || prior.scalar.tag != Scalar::kind::undefined"
                    ),
                )
                if "child_mutation" in row
                else ""
            ),
        )
        .replace(
            "NULL_CHECK",
            (
                "if (set(Scalar::null()) != 0 || !read(Scalar::null())) { return 350; }"
                if kind == "null"
                else (
                    "for (Value value : {Value{Scalar::null()}, Value{false}, Value{0.0}, Value{-0.0}, "
                    'Value{std::nan("")}}) { if (set(value) != 0 || get().object || '
                    "get().scalar.tag != Scalar::kind::null) { return 351; } }"
                    if kind == "returned"
                    else ""
                )
            ),
        )
        .replace("MISSING", "true" if kind == "undefined" else "false")
    )
