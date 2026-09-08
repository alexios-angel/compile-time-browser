// ctbrowser.script builtins - the keyed collections: Map, Set, WeakMap, WeakSet.
//
// One of four files carved out of a 1,824-line builtins/collections.cpp on
// 2026-09-08 - which was itself one of five carved out of builtins.cpp on
// 2026-08-09. Everything shared - the argument helpers, namespace detail, and
// these functions' declarations - is in ../internal.hpp; what only this
// directory shares is in internal.hpp beside this.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

using detail::list_iterator;

// `Map`, `Set`, `WeakMap`, `WeakSet`. 20 and 59 uses in p5.js, and `new Map()`
// is what stopped it once Array and Number were there.
//
// Both are built on the object model rather than on a new heap kind: entries
// live in a plain array on the instance, which makes lookup linear. That is the
// wrong complexity and it is written down rather than hidden - the maps p5
// builds are small and keyed by strings, and a hash keyed on a NaN-boxed value
// wants SameValueZero over every value kind, which is a bigger piece of work
// than this needs to be today.
//
// TODO: Map and Set lookup is LINEAR - a hash keyed on a NaN-boxed value wants
// SameValueZero over every value kind. Fine for the small string-keyed maps p5
// builds; wrong complexity for anything larger.
// TODO: WeakMap and WeakSet keep their keys alive. That is a leak, not a wrong
// answer, and it needs weak references the collector understands.
// WeakMap and WeakSet are the strong versions under different names: nothing
// here has weak references, so an entry keeps its key alive. Said out loud
// because the difference is a leak, not a wrong answer.
void install_collections(context & cx) {
    using detail::method;
    using detail::new_table;

    // The entry list of the receiver, or null when called on something else.
    const auto entries_of = [](context & c) -> array_object * {
        const value self = c.current_this();
        if (!self.is_object()) { return nullptr; }
        value * held = static_cast<object_object *>(self.as_heap())->find("__entries");
        return held != nullptr && held->is_array() ? static_cast<array_object *>(held->as_heap())
                                                   : nullptr;
    };
    // SameValueZero, which is what Map and Set key on: like ===, except NaN
    // matches NaN. A page that uses NaN as a key is doing something odd, but
    // getting it wrong here would be a silent miss.
    const auto same_value_zero = [](value a, value b) { return a.same_value_zero(b); };

    const auto build = [&](const char * name, bool keyed) {
        object_object * proto = new_table(cx);
        auto * ctor = cx.allocate<native_object>(name, [keyed, same_value_zero](
                                                           context & c, std::span<value> a) {
            // INITIALISE THE RECEIVER when there is one. `class MySet extends
            // Set {}` reaches here through super() with the new instance as
            // `this`; making a fresh object instead left the instance with none
            // of Set's state, and every method on it then failed somewhere
            // else entirely.
            value self = c.current_this();
            if (!self.is_object()) { self = c.make_object(); }
            auto * made = static_cast<object_object *>(self.as_heap());
            made->set("__entries", c.make_array());
            if (!made->prototype.is_object()) {
                if (object_object * table =
                        c.prototype(keyed ? context::proto_kind::map : context::proto_kind::set)) {
                    made->prototype = value::object(table);
                }
            }
            // `new Map([[k, v], ...])` and `new Set([...])` seed from ANY
            // iterable, through the same conversion for..of uses - so `new
            // Set(otherSet)` and `new Map(map.entries())` work, which is how a page
            // copies one.
            const value seed = a.empty() ? value::undefined() : c.iterable_values(a[0]);
            if (seed.is_array()) {
                auto * entries = static_cast<array_object *>(
                    static_cast<object_object *>(self.as_heap())->find("__entries")->as_heap());
                for (const value & item : static_cast<array_object *>(seed.as_heap())->items) {
                    if (keyed) {
                        if (!item.is_array()) { continue; }
                        const auto & pair = static_cast<array_object *>(item.as_heap())->items;
                        const value key = pair.empty() ? value::undefined() : pair[0];
                        const value held = pair.size() > 1 ? pair[1] : value::undefined();
                        // A REPEATED KEY REPLACES, it does not append. `new Map([['a',
                        // 1], ['a', 2]])` has ONE entry and it holds 2 - and a
                        // duplicate that merely sat there would be found by get()
                        // and missed by size(), which is two answers to one question.
                        bool replaced = false;
                        for (const value & existing : entries->items) {
                            if (!existing.is_array()) { continue; }
                            auto & cell = static_cast<array_object *>(existing.as_heap())->items;
                            if (!cell.empty() && same_value_zero(cell[0], key)) {
                                if (cell.size() > 1) {
                                    cell[1] = held;
                                } else {
                                    cell.push_back(held);
                                }
                                replaced = true;
                                break;
                            }
                        }
                        if (replaced) { continue; }
                        value entry = c.make_array();
                        auto * cell = static_cast<array_object *>(entry.as_heap());
                        cell->items.push_back(key);
                        cell->items.push_back(held);
                        entries->items.push_back(entry);
                        continue;
                    }
                    // A SET DEDUPES, which is the entire reason to use one.
                    // `new Set([1, 2, 2, 3]).size` was 4 - so `[...new Set(v)]`
                    // was not a unique list, and p5's colour-space registry is
                    // exactly that idiom.
                    bool seen = false;
                    for (const value & existing : entries->items) {
                        if (same_value_zero(existing, item)) {
                            seen = true;
                            break;
                        }
                    }
                    if (!seen) { entries->items.push_back(item); }
                }
            }
            return self;
        });
        // `class X extends Map` calls `super()`, which resolves through the
        // parent prototype's `constructor` - absent, and the class could not be
        // instantiated at all.
        proto->set("constructor", value::object(ctor));
        detail::constant(ctor, "prototype", value::object(proto));
        cx.set_prototype(keyed ? context::proto_kind::map : context::proto_kind::set, proto);
        cx.define_global(name, value::object(ctor));
        return proto;
    };

    // --- Map ---------------------------------------------------------------
    object_object * map_proto = build("Map", true);
    const auto find_entry = [entries_of, same_value_zero](context & c, value key) -> value * {
        array_object * entries = entries_of(c);
        if (entries == nullptr) { return nullptr; }
        for (value & entry : entries->items) {
            auto * pair = static_cast<array_object *>(entry.as_heap());
            if (!pair->items.empty() && same_value_zero(pair->items[0], key)) { return &entry; }
        }
        return nullptr;
    };
    method(cx, map_proto, "get", 1, [find_entry](context & c, std::span<value> a) {
        value * entry = find_entry(c, arg_at(a, 0));
        if (entry == nullptr) { return value::undefined(); }
        const auto & pair = static_cast<array_object *>(entry->as_heap())->items;
        return pair.size() > 1 ? pair[1] : value::undefined();
    });
    method(cx, map_proto, "has", 1, [find_entry](context & c, std::span<value> a) {
        return value::boolean(find_entry(c, arg_at(a, 0)) != nullptr);
    });
    method(cx, map_proto, "set", 2, [find_entry, entries_of](context & c, std::span<value> a) {
        if (value * entry = find_entry(c, arg_at(a, 0))) {
            static_cast<array_object *>(entry->as_heap())->items[1] = arg_at(a, 1);
            return c.current_this();
        }
        if (array_object * entries = entries_of(c)) {
            value pair = c.make_array();
            auto * cell = static_cast<array_object *>(pair.as_heap());
            cell->items.push_back(arg_at(a, 0));
            cell->items.push_back(arg_at(a, 1));
            entries->items.push_back(pair);
        }
        return c.current_this();
    });
    method(
        cx, map_proto, "delete", 1, [entries_of, same_value_zero](context & c, std::span<value> a) {
            array_object * entries = entries_of(c);
            if (entries == nullptr) { return value::boolean(false); }
            for (std::size_t i = 0; i < entries->items.size(); ++i) {
                auto * pair = static_cast<array_object *>(entries->items[i].as_heap());
                if (!pair->items.empty() && same_value_zero(pair->items[0], arg_at(a, 0))) {
                    entries->items.erase(entries->items.begin() + static_cast<std::ptrdiff_t>(i));
                    return value::boolean(true);
                }
            }
            return value::boolean(false);
        });
    method(cx, map_proto, "clear", 0, [entries_of](context & c, std::span<value>) {
        if (array_object * entries = entries_of(c)) { entries->items.clear(); }
        return value::undefined();
    });
    method(cx, map_proto, "forEach", 1, [entries_of](context & c, std::span<value> a) {
        array_object * entries = entries_of(c);
        if (entries == nullptr || a.empty() || !a[0].is_callable()) { return value::undefined(); }
        // A copy: a callback that mutates the map must not invalidate the walk.
        const std::vector<value> snapshot = entries->items;
        for (const value & entry : snapshot) {
            const auto & pair = static_cast<array_object *>(entry.as_heap())->items;
            const value args[3] = {pair.size() > 1 ? pair[1] : value::undefined(),
                                   pair.empty() ? value::undefined() : pair[0], c.current_this()};
            (void)c.call(a[0], args);
        }
        return value::undefined();
    });
    const auto column = [entries_of](context & c, int which) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        if (array_object * entries = entries_of(c)) {
            for (const value & entry : entries->items) {
                const auto & pair = static_cast<array_object *>(entry.as_heap())->items;
                if (which == 2) {
                    result->items.push_back(entry);
                } else if (static_cast<std::size_t>(which) < pair.size()) {
                    result->items.push_back(pair[static_cast<std::size_t>(which)]);
                }
            }
        }
        return out;
    };
    // REAL ITERATORS. They were arrays, with a comment saying `map.keys().next()`
    // does not work - and Babylon drives exactly that, by hand, for the Map of
    // shadow generators a light owns. See list_iterator.
    method(cx, map_proto, "keys", 0, [column](context & c, std::span<value>) {
        return list_iterator(c, column(c, 0), "Map Iterator");
    });
    method(cx, map_proto, "values", 0, [column](context & c, std::span<value>) {
        return list_iterator(c, column(c, 1), "Map Iterator");
    });
    method(cx, map_proto, "entries", 0, [column](context & c, std::span<value>) {
        return list_iterator(c, column(c, 2), "Map Iterator");
    });
    map_proto->define_accessor(
        "size",
        value::object(cx.allocate<native_object>(
            "size",
            [entries_of](context & c, std::span<value>) {
                array_object * e = entries_of(c);
                return value::number(e == nullptr ? 0.0 : static_cast<double>(e->items.size()));
            })),
        value::undefined());

    // --- Set ---------------------------------------------------------------
    object_object * set_proto = build("Set", false);
    const auto set_index = [entries_of, same_value_zero](context & c, value v) -> std::ptrdiff_t {
        array_object * entries = entries_of(c);
        if (entries == nullptr) { return -1; }
        for (std::size_t i = 0; i < entries->items.size(); ++i) {
            if (same_value_zero(entries->items[i], v)) { return static_cast<std::ptrdiff_t>(i); }
        }
        return -1;
    };
    method(cx, set_proto, "has", 1, [set_index](context & c, std::span<value> a) {
        return value::boolean(set_index(c, arg_at(a, 0)) >= 0);
    });
    method(cx, set_proto, "add", 1, [set_index, entries_of](context & c, std::span<value> a) {
        if (set_index(c, arg_at(a, 0)) < 0) {
            if (array_object * entries = entries_of(c)) { entries->items.push_back(arg_at(a, 0)); }
        }
        return c.current_this();
    });
    method(cx, set_proto, "delete", 1, [set_index, entries_of](context & c, std::span<value> a) {
        const std::ptrdiff_t at = set_index(c, arg_at(a, 0));
        if (at < 0) { return value::boolean(false); }
        array_object * entries = entries_of(c);
        entries->items.erase(entries->items.begin() + at);
        return value::boolean(true);
    });
    method(cx, set_proto, "clear", 0, [entries_of](context & c, std::span<value>) {
        if (array_object * entries = entries_of(c)) { entries->items.clear(); }
        return value::undefined();
    });
    method(cx, set_proto, "forEach", 1, [entries_of](context & c, std::span<value> a) {
        array_object * entries = entries_of(c);
        if (entries == nullptr || a.empty() || !a[0].is_callable()) { return value::undefined(); }
        const std::vector<value> snapshot = entries->items;
        for (const value & item : snapshot) {
            const value args[3] = {item, item, c.current_this()};
            (void)c.call(a[0], args);
        }
        return value::undefined();
    });
    const auto members = [entries_of](context & c) {
        value out = c.make_array();
        if (array_object * entries = entries_of(c)) {
            static_cast<array_object *>(out.as_heap())->items = entries->items;
        }
        return out;
    };
    method(cx, set_proto, "values", 0, [members](context & c, std::span<value>) {
        return list_iterator(c, members(c), "Set Iterator");
    });
    method(cx, set_proto, "keys", 0, [members](context & c, std::span<value>) {
        return list_iterator(c, members(c), "Set Iterator");
    });
    // A Set's `entries` pairs each member WITH ITSELF, which looks odd and is
    // the spec: it exists so a Set and a Map can be walked by the same code.
    method(cx, set_proto, "entries", 0, [members](context & c, std::span<value>) {
        const value all = members(c);
        value out = c.make_array();
        if (!all.is_array()) { return out; }
        auto * pairs = static_cast<array_object *>(out.as_heap());
        for (const value & member : static_cast<array_object *>(all.as_heap())->items) {
            value pair = c.make_array();
            auto * both = static_cast<array_object *>(pair.as_heap());
            both->items.push_back(member);
            both->items.push_back(member);
            pairs->items.push_back(pair);
        }
        return list_iterator(c, out, "Set Iterator");
    });
    set_proto->define_accessor(
        "size",
        value::object(cx.allocate<native_object>(
            "size",
            [entries_of](context & c, std::span<value>) {
                array_object * e = entries_of(c);
                return value::number(e == nullptr ? 0.0 : static_cast<double>(e->items.size()));
            })),
        value::undefined());

    // Strong, under a weak name - see the note at the top.
    cx.define_global("WeakMap", cx.global("Map"));
    cx.define_global("WeakSet", cx.global("Set"));
}

} // namespace ctbrowser::script::builtins_detail
