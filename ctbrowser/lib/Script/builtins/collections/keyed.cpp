// ctbrowser.script builtins - the keyed collections: Map, Set, WeakMap, WeakSet.
//
// One of four files carved out of a 1,824-line builtins/collections.cpp on
// 2026-09-08 - which was itself one of five carved out of builtins.cpp on
// 2026-08-09. Everything shared - the argument helpers, namespace detail, and
// these functions' declarations - is in ../internal.hpp.

#include "../internal.hpp"

namespace ctbrowser::script::builtins_detail {

using detail::list_iterator;

// `Map`, `Set`, `WeakMap`, `WeakSet`. 20 and 59 uses in p5.js, and `new Map()`
// is what stopped it once Array and Number were there.
//
// Both are built on the object model rather than on a new heap kind: entries
// live in a plain array on the instance under `__entries` - a Map's as
// [key, value] pairs, a Set's as bare members, which is exactly the shape
// for-of and spread want, and which context::iterable_values and the compiled
// for-of open read by that name (vm/call/invoke.cpp). Which of the four
// classes made the instance is a PRIVATE-keyed slot beside it, and every
// method checks for its own: `Map.prototype.get.call(new Set)` is a TypeError
// (24.1.3.6 step 2), not undefined.
//
// ponytail: lookup is LINEAR - a hash keyed on a NaN-boxed value wants
// SameValueZero over every value kind. Fine for the small string-keyed maps
// p5 builds; a hash when something larger turns up.
// TODO: WeakMap and WeakSet keep their keys alive. That is a leak, not a wrong
// answer, and it needs weak references the collector understands. They ARE
// their own classes now - a key that cannot be held weakly is the TypeError
// 24.3.3.5 and 24.4.3.1 make it - but the storage is the strong one.

namespace {

enum class kind : std::uint8_t {
    map,
    set,
    weak_map,
    weak_set
};

constexpr std::string_view slot_of(kind k) {
    switch (k) {
    case kind::map: return "@#MapData";
    case kind::set: return "@#SetData";
    case kind::weak_map: return "@#WeakMapData";
    case kind::weak_set: return "@#WeakSetData";
    }
    return "@#MapData";
}
constexpr const char * name_of(kind k) {
    switch (k) {
    case kind::map: return "Map";
    case kind::set: return "Set";
    case kind::weak_map: return "WeakMap";
    case kind::weak_set: return "WeakSet";
    }
    return "Map";
}
constexpr bool keyed(kind k) {
    return k == kind::map || k == kind::weak_map;
}
constexpr bool weak(kind k) {
    return k == kind::weak_map || k == kind::weak_set;
}

// The entry list of the receiver, or null with a TypeError in flight when the
// receiver was not made by this class (24.1.3.x step 2, RequireInternalSlot).
[[nodiscard]] array_object * entries_of(context & c, value self, kind k, const char * method) {
    if (self.is_object()) {
        auto * o = static_cast<object_object *>(self.as_heap());
        if (o->find(slot_of(k)) != nullptr) {
            if (value * held = o->find("__entries"); held != nullptr && held->is_array()) {
                return static_cast<array_object *>(held->as_heap());
            }
        }
    }
    c.throw_error("TypeError", std::string{method} + " called on an incompatible receiver");
    return nullptr;
}

// SameValueZero, which is what Map and Set key on: like ===, except NaN
// matches NaN. A page that uses NaN as a key is doing something odd, but
// getting it wrong here would be a silent miss.
[[nodiscard]] bool same_value_zero(value a, value b) {
    return a.same_value_zero(b);
}

// 24.1.3.9 step 6 / 24.2.3.1 step 5: -0 is stored as +0, so `has(-0)` and
// `has(0)` are one question and a key read back is never negative zero.
[[nodiscard]] value normalise_key(value k) {
    return k.is_number() && k.as_number() == 0 ? value::number(0) : k;
}

// CanBeHeldWeakly, 9.13: an object, or a symbol that is not registered with
// Symbol.for (whose key is "@@for:<description>", text/symbol.cpp).
[[nodiscard]] bool can_be_held_weakly(value v) {
    if (v.is_object_like()) { return true; }
    if (!v.is_kind(heap_kind::symbol)) { return false; }
    return !static_cast<symbol_object *>(v.as_heap())->key.starts_with("@@for:");
}

[[nodiscard]] value * find_pair(array_object & entries, value key) {
    for (value & entry : entries.items) {
        auto * pair = static_cast<array_object *>(entry.as_heap());
        if (!pair->items.empty() && same_value_zero(pair->items[0], key)) { return &entry; }
    }
    return nullptr;
}
[[nodiscard]] std::ptrdiff_t find_member(array_object & entries, value v) {
    for (std::size_t i = 0; i < entries.items.size(); ++i) {
        if (same_value_zero(entries.items[i], v)) { return static_cast<std::ptrdiff_t>(i); }
    }
    return -1;
}

void put_pair(context & c, array_object & entries, value key, value held) {
    if (value * entry = find_pair(entries, key)) {
        static_cast<array_object *>(entry->as_heap())->items[1] = held;
        return;
    }
    value pair = c.make_array();
    auto * cell = static_cast<array_object *>(pair.as_heap());
    cell->items.push_back(key);
    cell->items.push_back(held);
    entries.items.push_back(pair);
}

// GetSetRecord, 24.2.1.2: what the seven set-algebra methods make of their
// argument. `size` is read and coerced (NaN is a TypeError, a negative a
// RangeError), and `has` and `keys` must be callable. FALSE with the throw.
struct set_record {
    value object;
    double size = 0;
    value has;
    value keys;
};
[[nodiscard]] bool get_set_record(context & c, value other, set_record & out) {
    if (!other.is_object_like()) {
        c.throw_error("TypeError", "the argument must be a set-like object");
        return false;
    }
    const detail::unwind_watch watch{c};
    const value raw_size = c.lookup_property(other, "size");
    if (watch.threw() || !numeric_arg(c, raw_size)) { return false; }
    const double num = c.to_number_value(raw_size);
    if (watch.threw()) { return false; }
    if (std::isnan(num)) {
        c.throw_error("TypeError", "the set-like's size is not a number");
        return false;
    }
    const double size = std::isinf(num) ? num : std::trunc(num);
    if (size < 0) {
        c.throw_error("RangeError", "the set-like's size is negative");
        return false;
    }
    const value has = c.lookup_property(other, "has");
    if (watch.threw()) { return false; }
    if (!has.is_callable()) {
        c.throw_error("TypeError", "the set-like's has is not a function");
        return false;
    }
    const value keys = c.lookup_property(other, "keys");
    if (watch.threw()) { return false; }
    if (!keys.is_callable()) {
        c.throw_error("TypeError", "the set-like's keys is not a function");
        return false;
    }
    out = {other, size, has, keys};
    return true;
}

// Call(record.has, record.object, [v]), as a bool. FALSE with a throw in
// flight is told apart by the watch the caller holds.
[[nodiscard]] bool record_has(context & c, const set_record & r, value v) {
    return context::truthy(c.call(r.has, std::span<const value>{&v, 1}, r.object));
}

// GetIteratorFromMethod(record.object, record.keys) then IteratorStepValue,
// each key handed to `visit`, which answers false to stop (IteratorClose is
// then run: the specification closes the iterator on every early exit). FALSE
// with a throw in flight.
template <typename Fn> [[nodiscard]] bool each_key(context & c, const set_record & r, Fn && visit) {
    const detail::unwind_watch watch{c};
    const value iterator = c.call(r.keys, std::span<const value>{}, r.object);
    if (watch.threw()) { return false; }
    if (!iterator.is_object_like()) {
        c.throw_error("TypeError", "keys() did not return an object");
        return false;
    }
    const context::rooted keep{c, iterator};
    const value next = c.lookup_property(iterator, "next");
    if (watch.threw()) { return false; }
    for (std::size_t guard = 0; guard < (1u << 24); ++guard) {
        if (!next.is_callable()) {
            c.throw_error("TypeError", "the iterator's next is not a function");
            return false;
        }
        const value step = c.call(next, std::span<const value>{}, iterator);
        if (watch.threw()) { return false; }
        if (!step.is_object_like()) {
            c.throw_error("TypeError", "the iterator result is not an object");
            return false;
        }
        const context::rooted keep_step{c, step};
        const value done = c.lookup_property(step, "done");
        if (watch.threw()) { return false; }
        if (context::truthy(done)) { return true; }
        const value item = c.lookup_property(step, "value");
        if (watch.threw()) { return false; }
        if (!visit(item)) {
            const value close = c.lookup_property(iterator, "return");
            if (watch.threw()) { return false; }
            if (close.is_callable()) { (void)c.call(close, std::span<const value>{}, iterator); }
            return !watch.threw();
        }
        if (watch.threw()) { return false; }
    }
    return true;
}

// A fresh Set holding `items`, on Set.prototype - what the algebra methods
// answer (24.2.4.x: OrdinaryObjectCreate(%Set.prototype%), never a species).
[[nodiscard]] value make_set(context & c, std::vector<value> items) {
    value out = c.make_object();
    auto * o = static_cast<object_object *>(out.as_heap());
    if (object_object * table = c.prototype(context::proto_kind::set)) {
        o->prototype = value::object(table);
    }
    o->define(slot_of(kind::set), value::boolean(true), attr_none);
    value list = c.make_array();
    static_cast<array_object *>(list.as_heap())->items = std::move(items);
    o->define("__entries", list, attr_builtin);
    return out;
}

// `this`'s entries for a method of class `k`, or null with the TypeError.
[[nodiscard]] array_object * self_entries(context & c, kind k, const char * method) {
    return entries_of(c, c.current_this(), k, method);
}

} // namespace

void install_collections(context & cx) {
    using detail::method;
    using detail::new_table;

    const auto build = [&](kind k) {
        object_object * proto = new_table(cx);
        auto * ctor =
            cx.allocate<native_object>(name_of(k), [k](context & c, std::span<value> a) -> value {
                // 24.1.1.1 step 1: `Map()` without new is a TypeError. INITIALISE
                // THE RECEIVER `new` made: `class MySet extends Set {}` reaches here
                // through super() with the new instance as `this`; making a fresh
                // object instead left the instance with none of Set's state. (A
                // derived instance may already carry its fields - this engine runs
                // initialisers before the body - so the test is "an object", not
                // constructing_this's "an EMPTY object".)
                const value self = c.current_this();
                if (!self.is_object()) {
                    c.throw_error("TypeError",
                                  std::string{"Constructor "} + name_of(k) + " requires 'new'");
                    return value::undefined();
                }
                auto * made = static_cast<object_object *>(self.as_heap());
                made->define(slot_of(k), value::boolean(true), attr_none);
                made->define("__entries", c.make_array(), attr_builtin);
                if (!made->prototype.is_object()) {
                    const auto which =
                        keyed(k) ? context::proto_kind::map : context::proto_kind::set;
                    if (object_object * table = c.prototype(which)) {
                        made->prototype = value::object(table);
                    }
                }
                // Steps 5-9 / AddEntriesFromIterable: the seed goes through the
                // receiver's OWN `set` or `add` (a subclass that overrides it sees
                // every element), and a Map's entries must each be an object.
                const value seed = arg_at(a, 0);
                if (seed.is_nullish()) { return self; }
                const detail::unwind_watch watch{c};
                const value adder = c.lookup_property(self, keyed(k) ? "set" : "add");
                if (watch.threw()) { return value::undefined(); }
                if (!adder.is_callable()) {
                    c.throw_error("TypeError",
                                  std::string{name_of(k)} + "'s adder is not a function");
                    return value::undefined();
                }
                const value items = c.iterable_values(seed);
                if (watch.threw()) { return value::undefined(); }
                if (!items.is_array()) {
                    c.throw_error("TypeError", "the argument is not iterable");
                    return value::undefined();
                }
                const context::rooted keep{c, items};
                const std::vector<value> snapshot =
                    static_cast<array_object *>(items.as_heap())->items;
                const context::rooted_values keep_all{c, snapshot};
                for (const value & item : snapshot) {
                    if (keyed(k)) {
                        if (!item.is_object_like()) {
                            c.throw_error("TypeError", "an iterator value is not an entry object");
                            return value::undefined();
                        }
                        const value key = c.lookup_index(item, value::number(0));
                        if (watch.threw()) { return value::undefined(); }
                        const value held = c.lookup_index(item, value::number(1));
                        if (watch.threw()) { return value::undefined(); }
                        const value args[2] = {key, held};
                        (void)c.call(adder, args, self);
                    } else {
                        (void)c.call(adder, std::span<const value>{&item, 1}, self);
                    }
                    if (watch.threw()) { return value::undefined(); }
                }
                return self;
            });
        // `class X extends Map` calls `super()`, which resolves through the
        // parent prototype's `constructor` - absent, and the class could not be
        // instantiated at all.
        proto->define("constructor", value::object(ctor), attr_builtin);
        detail::constant(ctor, "prototype", value::object(proto));
        detail::install_arity(cx, ctor, 0);
        // 24.1.3.13 / 24.2.3.12 / 24.3.3.6 / 24.4.3.5: the tag
        // Object.prototype.toString reads.
        proto->define("@@toStringTag", cx.string(name_of(k)), attr_configurable);
        if (!weak(k)) {
            cx.set_prototype(keyed(k) ? context::proto_kind::map : context::proto_kind::set, proto);
            // 24.1.2.2 / 24.2.2.2 get [@@species]: `this`.
            auto * species = detail::method_native(
                cx, "get [Symbol.species]",
                [](context & c, std::span<value>) { return c.current_this(); });
            detail::install_arity(cx, species, 0);
            ctor->define_accessor("@@species", value::object(species), value::undefined(),
                                  attr_configurable);
        }
        cx.define_global(name_of(k), value::object(ctor));
        return std::pair{proto, ctor};
    };

    // --- Map and WeakMap ------------------------------------------------------
    for (const kind k : {kind::map, kind::weak_map}) {
        auto [proto, ctor] = build(k);
        const std::string prefix = std::string{name_of(k)} + ".prototype.";
        const auto m = [&](const char * name, double arity, native_fn fn) {
            method(cx, proto, name, arity, std::move(fn));
        };
        m("get", 1, [k, prefix](context & c, std::span<value> a) -> value {
            array_object * entries = self_entries(c, k, (prefix + "get").c_str());
            if (entries == nullptr) { return value::undefined(); }
            value * entry = find_pair(*entries, arg_at(a, 0));
            if (entry == nullptr) { return value::undefined(); }
            return static_cast<array_object *>(entry->as_heap())->items[1];
        });
        m("has", 1, [k, prefix](context & c, std::span<value> a) -> value {
            array_object * entries = self_entries(c, k, (prefix + "has").c_str());
            if (entries == nullptr) { return value::undefined(); }
            return value::boolean(find_pair(*entries, arg_at(a, 0)) != nullptr);
        });
        m("set", 2, [k, prefix](context & c, std::span<value> a) -> value {
            array_object * entries = self_entries(c, k, (prefix + "set").c_str());
            if (entries == nullptr) { return value::undefined(); }
            if (weak(k) && !can_be_held_weakly(arg_at(a, 0))) {
                c.throw_error("TypeError", "Invalid value used as weak map key");
                return value::undefined();
            }
            put_pair(c, *entries, normalise_key(arg_at(a, 0)), arg_at(a, 1));
            return c.current_this();
        });
        m("delete", 1, [k, prefix](context & c, std::span<value> a) -> value {
            array_object * entries = self_entries(c, k, (prefix + "delete").c_str());
            if (entries == nullptr) { return value::undefined(); }
            for (std::size_t i = 0; i < entries->items.size(); ++i) {
                auto * pair = static_cast<array_object *>(entries->items[i].as_heap());
                if (!pair->items.empty() && same_value_zero(pair->items[0], arg_at(a, 0))) {
                    entries->items.erase(entries->items.begin() + static_cast<std::ptrdiff_t>(i));
                    return value::boolean(true);
                }
            }
            return value::boolean(false);
        });
        // Map.prototype.getOrInsert / getOrInsertComputed (the upsert
        // proposal, in test262's corpus): the value under the key, or the
        // one given / computed by the callback, which is stored first.
        m("getOrInsert", 2, [k, prefix](context & c, std::span<value> a) -> value {
            array_object * entries = self_entries(c, k, (prefix + "getOrInsert").c_str());
            if (entries == nullptr) { return value::undefined(); }
            if (weak(k) && !can_be_held_weakly(arg_at(a, 0))) {
                c.throw_error("TypeError", "Invalid value used as weak map key");
                return value::undefined();
            }
            const value key = normalise_key(arg_at(a, 0));
            if (value * entry = find_pair(*entries, key)) {
                return static_cast<array_object *>(entry->as_heap())->items[1];
            }
            put_pair(c, *entries, key, arg_at(a, 1));
            return arg_at(a, 1);
        });
        m("getOrInsertComputed", 2, [k, prefix](context & c, std::span<value> a) -> value {
            array_object * entries = self_entries(c, k, (prefix + "getOrInsertComputed").c_str());
            if (entries == nullptr) { return value::undefined(); }
            if (!detail::callable_arg(c, arg_at(a, 1), "callback")) { return value::undefined(); }
            if (weak(k) && !can_be_held_weakly(arg_at(a, 0))) {
                c.throw_error("TypeError", "Invalid value used as weak map key");
                return value::undefined();
            }
            const value key = normalise_key(arg_at(a, 0));
            if (value * entry = find_pair(*entries, key)) {
                return static_cast<array_object *>(entry->as_heap())->items[1];
            }
            const detail::unwind_watch watch{c};
            const value made = c.call(a[1], std::span<const value>{&key, 1});
            if (watch.threw()) { return value::undefined(); }
            // The callback may have touched the map: look the list up again.
            const context::rooted keep{c, made};
            array_object * again = self_entries(c, k, "getOrInsertComputed");
            if (again == nullptr) { return value::undefined(); }
            put_pair(c, *again, key, made);
            return made;
        });
        if (weak(k)) { continue; }
        m("clear", 0, [k](context & c, std::span<value>) -> value {
            array_object * entries = self_entries(c, k, "Map.prototype.clear");
            if (entries == nullptr) { return value::undefined(); }
            entries->items.clear();
            return value::undefined();
        });
        m("forEach", 1, [k](context & c, std::span<value> a) -> value {
            array_object * entries = self_entries(c, k, "Map.prototype.forEach");
            if (entries == nullptr) { return value::undefined(); }
            if (!detail::callable_arg(c, arg_at(a, 0), "callback")) { return value::undefined(); }
            const value self = c.current_this();
            const detail::unwind_watch watch{c};
            // BY INDEX over the live list, not a snapshot: 24.1.3.5 visits an
            // entry added during the walk and skips one deleted before its
            // turn, which a callback that mutates the map can see.
            for (std::size_t i = 0; i < entries->items.size(); ++i) {
                const value entry = entries->items[i];
                const auto & pair = static_cast<array_object *>(entry.as_heap())->items;
                const value args[3] = {pair[1], pair[0], self};
                // 24.1.3.5 step 5: the callback's `this` is thisArg. Phaser's
                // loader walks its file Sets with `list.forEach(fn, this)`.
                (void)c.call(a[0], args, arg_at(a, 1));
                if (watch.threw()) { return value::undefined(); }
                entries = self_entries(c, k, "Map.prototype.forEach");
                if (entries == nullptr) { return value::undefined(); }
            }
            return value::undefined();
        });
        const auto column = [k](context & c, int which, const char * name) -> value {
            array_object * entries = self_entries(c, k, name);
            if (entries == nullptr) { return value::undefined(); }
            value out = c.make_array();
            auto * result = static_cast<array_object *>(out.as_heap());
            for (const value & entry : entries->items) {
                const auto & pair = static_cast<array_object *>(entry.as_heap())->items;
                result->items.push_back(which == 2 ? entry : pair[static_cast<std::size_t>(which)]);
            }
            return list_iterator(c, out, "Map Iterator");
        };
        // REAL ITERATORS. They were arrays, with a comment saying
        // `map.keys().next()` does not work - and Babylon drives exactly that,
        // by hand, for the Map of shadow generators a light owns. See
        // list_iterator. (A snapshot, not a live view - said out loud.)
        m("keys", 0,
          [column](context & c, std::span<value>) { return column(c, 0, "Map.prototype.keys"); });
        m("values", 0,
          [column](context & c, std::span<value>) { return column(c, 1, "Map.prototype.values"); });
        m("entries", 0, [column](context & c, std::span<value>) {
            return column(c, 2, "Map.prototype.entries");
        });
        // 24.1.3.12: Map.prototype[@@iterator] IS `entries`, the same function
        // object - which is what Array.from and any hand-driven iteration reach
        // for (for-of and spread take context::iterable_values' shortcut).
        if (value * entries = proto->find("entries")) {
            proto->define("@@iterator", *entries, attr_builtin);
        }
        {
            auto * getter =
                detail::method_native(cx, "get size", [k](context & c, std::span<value>) {
                    array_object * e = self_entries(c, k, "Map.prototype.size getter");
                    return e == nullptr ? value::undefined()
                                        : value::number(static_cast<double>(e->items.size()));
                });
            detail::install_arity(cx, getter, 0);
            proto->define_accessor("size", value::object(getter), value::undefined(),
                                   attr_configurable);
        }
        // 24.1.2.1 Map.groupBy(items, callback): keys by SameValueZero, each
        // holding the array of the items the callback mapped to it, in order.
        const value map_ctor = value::object(ctor);
        method(cx, ctor, "groupBy", 2, [map_ctor](context & c, std::span<value> a) -> value {
            if (!detail::callable_arg(c, arg_at(a, 1), "callback")) { return value::undefined(); }
            if (arg_at(a, 0).is_nullish()) {
                c.throw_error("TypeError", "Map.groupBy called on null or undefined");
                return value::undefined();
            }
            const detail::unwind_watch watch{c};
            const value items = c.iterable_values(arg_at(a, 0));
            if (watch.threw()) { return value::undefined(); }
            if (!items.is_array()) {
                c.throw_error("TypeError", "the argument is not iterable");
                return value::undefined();
            }
            const context::rooted keep_items{c, items};
            const value out = c.construct(map_ctor, std::span<const value>{});
            if (watch.threw()) { return value::undefined(); }
            const context::rooted keep_out{c, out};
            array_object * groups = entries_of(c, out, kind::map, "Map.groupBy");
            if (groups == nullptr) { return value::undefined(); }
            const std::vector<value> snapshot = static_cast<array_object *>(items.as_heap())->items;
            const context::rooted_values keep_all{c, snapshot};
            double index = 0;
            for (const value & item : snapshot) {
                const value args[2] = {item, value::number(index++)};
                const value key = normalise_key(c.call(a[1], args));
                if (watch.threw()) { return value::undefined(); }
                value * entry = find_pair(*groups, key);
                if (entry == nullptr) {
                    put_pair(c, *groups, key, c.make_array());
                    entry = find_pair(*groups, key);
                }
                auto * bucket = static_cast<array_object *>(
                    static_cast<array_object *>(entry->as_heap())->items[1].as_heap());
                bucket->items.push_back(item);
            }
            return out;
        });
    }

    // --- Set and WeakSet ------------------------------------------------------
    for (const kind k : {kind::set, kind::weak_set}) {
        auto [proto, ctor] = build(k);
        (void)ctor;
        const std::string prefix = std::string{name_of(k)} + ".prototype.";
        const auto m = [&](const char * name, double arity, native_fn fn) {
            method(cx, proto, name, arity, std::move(fn));
        };
        m("has", 1, [k, prefix](context & c, std::span<value> a) -> value {
            array_object * entries = self_entries(c, k, (prefix + "has").c_str());
            if (entries == nullptr) { return value::undefined(); }
            return value::boolean(find_member(*entries, arg_at(a, 0)) >= 0);
        });
        m("add", 1, [k, prefix](context & c, std::span<value> a) -> value {
            array_object * entries = self_entries(c, k, (prefix + "add").c_str());
            if (entries == nullptr) { return value::undefined(); }
            if (weak(k) && !can_be_held_weakly(arg_at(a, 0))) {
                c.throw_error("TypeError", "Invalid value used in weak set");
                return value::undefined();
            }
            const value v = normalise_key(arg_at(a, 0));
            if (find_member(*entries, v) < 0) { entries->items.push_back(v); }
            return c.current_this();
        });
        m("delete", 1, [k, prefix](context & c, std::span<value> a) -> value {
            array_object * entries = self_entries(c, k, (prefix + "delete").c_str());
            if (entries == nullptr) { return value::undefined(); }
            const std::ptrdiff_t at = find_member(*entries, arg_at(a, 0));
            if (at < 0) { return value::boolean(false); }
            entries->items.erase(entries->items.begin() + at);
            return value::boolean(true);
        });
        if (weak(k)) { continue; }
        m("clear", 0, [k](context & c, std::span<value>) -> value {
            array_object * entries = self_entries(c, k, "Set.prototype.clear");
            if (entries == nullptr) { return value::undefined(); }
            entries->items.clear();
            return value::undefined();
        });
        m("forEach", 1, [k](context & c, std::span<value> a) -> value {
            array_object * entries = self_entries(c, k, "Set.prototype.forEach");
            if (entries == nullptr) { return value::undefined(); }
            if (!detail::callable_arg(c, arg_at(a, 0), "callback")) { return value::undefined(); }
            const value self = c.current_this();
            const detail::unwind_watch watch{c};
            for (std::size_t i = 0; i < entries->items.size(); ++i) {
                const value item = entries->items[i];
                const value args[3] = {item, item, self};
                (void)c.call(a[0], args, arg_at(a, 1)); // thisArg, 24.2.3.6 step 5
                if (watch.threw()) { return value::undefined(); }
                entries = self_entries(c, k, "Set.prototype.forEach");
                if (entries == nullptr) { return value::undefined(); }
            }
            return value::undefined();
        });
        const auto members = [k](context & c, const char * name) -> value {
            array_object * entries = self_entries(c, k, name);
            if (entries == nullptr) { return value::undefined(); }
            value out = c.make_array();
            static_cast<array_object *>(out.as_heap())->items = entries->items;
            return out;
        };
        m("values", 0, [members](context & c, std::span<value>) -> value {
            const value all = members(c, "Set.prototype.values");
            return all.is_undefined() ? all : list_iterator(c, all, "Set Iterator");
        });
        // 24.2.3.10/11: `keys` and @@iterator are both the `values` function.
        if (value * values = proto->find("values")) {
            proto->define("keys", *values, attr_builtin);
            proto->define("@@iterator", *values, attr_builtin);
        }
        // A Set's `entries` pairs each member WITH ITSELF, which looks odd and
        // is the spec: it exists so a Set and a Map can be walked by one code.
        m("entries", 0, [members](context & c, std::span<value>) -> value {
            const value all = members(c, "Set.prototype.entries");
            if (all.is_undefined()) { return all; }
            const context::rooted keep{c, all};
            value out = c.make_array();
            const context::rooted keep_out{c, out};
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
        {
            auto * getter =
                detail::method_native(cx, "get size", [k](context & c, std::span<value>) {
                    array_object * e = self_entries(c, k, "Set.prototype.size getter");
                    return e == nullptr ? value::undefined()
                                        : value::number(static_cast<double>(e->items.size()));
                });
            detail::install_arity(cx, getter, 0);
            proto->define_accessor("size", value::object(getter), value::undefined(),
                                   attr_configurable);
        }

        // --- the set algebra, 24.2.4.5-4.11 (ES2025) --------------------------
        //
        // Each takes a SET-LIKE (GetSetRecord above), walks the smaller side
        // where the specification says to, and answers a fresh Set on
        // Set.prototype or a boolean. `this`'s members are read as a snapshot
        // taken before any call out, because the other side's `has` and `keys`
        // are page code that may mutate this set.
        const auto algebra = [&](const char * name,
                                 value (*body)(context &, array_object &, const set_record &)) {
            const std::string full = prefix + name;
            m(name, 1, [k, full, body](context & c, std::span<value> a) -> value {
                array_object * entries = self_entries(c, k, full.c_str());
                if (entries == nullptr) { return value::undefined(); }
                set_record other;
                if (!get_set_record(c, arg_at(a, 0), other)) { return value::undefined(); }
                const context::rooted keep_has{c, other.has};
                const context::rooted keep_keys{c, other.keys};
                return body(c, *entries, other);
            });
        };
        algebra("union", [](context & c, array_object & self, const set_record & other) -> value {
            std::vector<value> result = self.items;
            const context::rooted_values keep{c, result};
            const value out = make_set(c, result);
            const context::rooted keep_out{c, out};
            auto * list = static_cast<array_object *>(
                static_cast<object_object *>(out.as_heap())->find("__entries")->as_heap());
            const bool ok = each_key(c, other, [&](value key) {
                const value v = normalise_key(key);
                if (find_member(*list, v) < 0) { list->items.push_back(v); }
                return true;
            });
            return ok ? out : value::undefined();
        });
        algebra("intersection",
                [](context & c, array_object & self, const set_record & other) -> value {
                    const std::vector<value> mine = self.items;
                    const context::rooted_values keep{c, mine};
                    const value out = make_set(c, {});
                    const context::rooted keep_out{c, out};
                    auto * list = static_cast<array_object *>(
                        static_cast<object_object *>(out.as_heap())->find("__entries")->as_heap());
                    const detail::unwind_watch watch{c};
                    if (static_cast<double>(mine.size()) <= other.size) {
                        for (const value & e : mine) {
                            const bool in = record_has(c, other, e);
                            if (watch.threw()) { return value::undefined(); }
                            if (in && find_member(*list, e) < 0) { list->items.push_back(e); }
                        }
                        return out;
                    }
                    const bool ok = each_key(c, other, [&](value key) {
                        const value v = normalise_key(key);
                        if (find_member(self, v) >= 0 && find_member(*list, v) < 0) {
                            list->items.push_back(v);
                        }
                        return true;
                    });
                    return ok ? out : value::undefined();
                });
        algebra("difference",
                [](context & c, array_object & self, const set_record & other) -> value {
                    std::vector<value> result = self.items;
                    const context::rooted_values keep{c, result};
                    const value out = make_set(c, result);
                    const context::rooted keep_out{c, out};
                    auto * list = static_cast<array_object *>(
                        static_cast<object_object *>(out.as_heap())->find("__entries")->as_heap());
                    const detail::unwind_watch watch{c};
                    if (static_cast<double>(result.size()) <= other.size) {
                        for (const value & e : result) {
                            const bool in = record_has(c, other, e);
                            if (watch.threw()) { return value::undefined(); }
                            if (const std::ptrdiff_t at = find_member(*list, e); in && at >= 0) {
                                list->items.erase(list->items.begin() + at);
                            }
                        }
                        return out;
                    }
                    const bool ok = each_key(c, other, [&](value key) {
                        if (const std::ptrdiff_t at = find_member(*list, key); at >= 0) {
                            list->items.erase(list->items.begin() + at);
                        }
                        return true;
                    });
                    return ok ? out : value::undefined();
                });
        algebra("symmetricDifference",
                [](context & c, array_object & self, const set_record & other) -> value {
                    std::vector<value> result = self.items;
                    const context::rooted_values keep{c, result};
                    const value out = make_set(c, result);
                    const context::rooted keep_out{c, out};
                    auto * list = static_cast<array_object *>(
                        static_cast<object_object *>(out.as_heap())->find("__entries")->as_heap());
                    const bool ok = each_key(c, other, [&](value key) {
                        const value v = normalise_key(key);
                        const std::ptrdiff_t at = find_member(*list, v);
                        if (find_member(self, v) >= 0) {
                            if (at >= 0) { list->items.erase(list->items.begin() + at); }
                        } else if (at < 0) {
                            list->items.push_back(v);
                        }
                        return true;
                    });
                    return ok ? out : value::undefined();
                });
        algebra("isSubsetOf",
                [](context & c, array_object & self, const set_record & other) -> value {
                    if (static_cast<double>(self.items.size()) > other.size) {
                        return value::boolean(false);
                    }
                    const std::vector<value> mine = self.items;
                    const context::rooted_values keep{c, mine};
                    const detail::unwind_watch watch{c};
                    for (const value & e : mine) {
                        const bool in = record_has(c, other, e);
                        if (watch.threw()) { return value::undefined(); }
                        if (!in) { return value::boolean(false); }
                    }
                    return value::boolean(true);
                });
        algebra("isSupersetOf",
                [](context & c, array_object & self, const set_record & other) -> value {
                    if (static_cast<double>(self.items.size()) < other.size) {
                        return value::boolean(false);
                    }
                    bool all = true;
                    const bool ok = each_key(c, other, [&](value key) {
                        if (find_member(self, key) < 0) {
                            all = false;
                            return false;
                        }
                        return true;
                    });
                    return ok ? value::boolean(all) : value::undefined();
                });
        algebra("isDisjointFrom",
                [](context & c, array_object & self, const set_record & other) -> value {
                    const std::vector<value> mine = self.items;
                    const context::rooted_values keep{c, mine};
                    const detail::unwind_watch watch{c};
                    if (static_cast<double>(mine.size()) <= other.size) {
                        for (const value & e : mine) {
                            const bool in = record_has(c, other, e);
                            if (watch.threw()) { return value::undefined(); }
                            if (in) { return value::boolean(false); }
                        }
                        return value::boolean(true);
                    }
                    bool disjoint = true;
                    const bool ok = each_key(c, other, [&](value key) {
                        if (find_member(self, key) >= 0) {
                            disjoint = false;
                            return false;
                        }
                        return true;
                    });
                    return ok ? value::boolean(disjoint) : value::undefined();
                });
    }
}

} // namespace ctbrowser::script::builtins_detail
