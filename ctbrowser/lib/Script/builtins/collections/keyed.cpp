// ctbrowser.script builtins - the keyed collections: Map, Set, WeakMap, WeakSet.
//
// One of four files carved out of a 1,824-line builtins/collections.cpp on
// 2026-09-08 - which was itself one of five carved out of builtins.cpp on
// 2026-08-09. Everything shared - the argument helpers, namespace detail, and
// these functions' declarations - is in ../internal.hpp.

#include "../internal.hpp"
#include "iterator_internal.hpp"

namespace ctbrowser::script::builtins_detail {

using detail::list_iterator;

// `Map`, `Set`, `WeakMap`, `WeakSet`. 20 and 59 uses in p5.js, and `new Map()`
// is what stopped it once Array and Number were there.
//
// Both are built on the object model rather than on a new heap kind: entries
// live in a plain array on the instance under the PRIVATE `entries_slot_key`
// (value/objects.hpp; it was the page-writable `__entries`) - a Map's as
// [key, value] pairs, a Set's as bare members, which is exactly the shape
// for-of and spread want, and which context::iterable_values and the compiled
// for-of open read by that key (vm/call/invoke.cpp). Which of the four
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
    case kind::map: return CTBROWSER_PRIVATE_KEY("MapData");
    case kind::set: return CTBROWSER_PRIVATE_KEY("SetData");
    case kind::weak_map: return CTBROWSER_PRIVATE_KEY("WeakMapData");
    case kind::weak_set: return CTBROWSER_PRIVATE_KEY("WeakSetData");
    }
    return CTBROWSER_PRIVATE_KEY("MapData");
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
            if (value * held = o->find(entries_slot_key); held != nullptr && held->is_array()) {
                return static_cast<array_object *>(held->as_heap());
            }
        }
    }
    c.throw_error("TypeError", std::string{method} + " called on an incompatible receiver");
    return nullptr;
}

// 24.1.3.9 step 6 / 24.2.3.1 step 5: -0 is stored as +0, so `has(-0)` and
// `has(0)` are one question and a key read back is never negative zero.
[[nodiscard]] value normalise_key(value k) {
    return k.is_number() && k.as_number() == 0 ? value::number(0) : k;
}

// CanBeHeldWeakly, 9.13: an object, or a symbol that is not registered with
// Symbol.for (whose key starts registered_symbol_key_prefix, text/symbol.cpp).
[[nodiscard]] bool can_be_held_weakly(value v) {
    if (v.is_object_like()) { return true; }
    if (!v.is_kind(heap_kind::symbol)) { return false; }
    return !static_cast<symbol_object *>(v.as_heap())
                ->key.starts_with(registered_symbol_key_prefix);
}

[[nodiscard]] value * find_pair(array_object & entries, value key) {
    for (value & entry : entries.items) {
        auto * pair = static_cast<array_object *>(entry.as_heap());
        if (!pair->items.empty() && pair->items[0].same_value_zero(key)) { return &entry; }
    }
    return nullptr;
}
[[nodiscard]] std::ptrdiff_t find_member(array_object & entries, value v) {
    for (std::size_t i = 0; i < entries.items.size(); ++i) {
        if (entries.items[i].same_value_zero(v)) { return static_cast<std::ptrdiff_t>(i); }
    }
    return -1;
}

void put_pair(context & c, array_object & entries, value key, value held) {
    if (value * entry = find_pair(entries, key)) {
        static_cast<array_object *>(entry->as_heap())->items[1] = held;
        return;
    }
    const value pair = c.make_array();
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
// with a throw in flight. `opened` runs once the iterator and its `next` are
// in hand and before the first step - where union and symmetricDifference
// copy the receiver (24.2.4.14 / 24.2.4.12 step 5: the `next` read is page
// code, and may change it).
template <typename Fn, typename Opened>
[[nodiscard]] bool each_key(context & c, const set_record & r, Fn && visit, Opened && opened) {
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
    opened();
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
        // IteratorClose with a normal completion - asked of throw_pending, not
        // the watch: a generator's return() unwinds its own frame through
        // any `finally` without throwing at all (iterator_internal.hpp).
        if (!visit(item)) { return detail::iterator_close(c, iterator); }
        if (watch.threw()) { return false; }
    }
    return true;
}
template <typename Fn> [[nodiscard]] bool each_key(context & c, const set_record & r, Fn && visit) {
    return each_key(c, r, std::forward<Fn>(visit), [] {});
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
    const value list = c.make_array();
    static_cast<array_object *>(list.as_heap())->items = std::move(items);
    o->define(entries_slot_key, list, attr_none);
    return out;
}

// `this`'s entries for a method of class `k`, or null with the TypeError.
[[nodiscard]] array_object * self_entries(context & c, kind k, const char * method) {
    return entries_of(c, c.current_this(), k, method);
}

// A WALK IN PROGRESS over an entry list - forEach, and the set algebra that
// calls a set-like's `has` between members - and the index it visits next.
//
// The specification's [[MapData]] and [[SetData]] never shrink: a delete
// leaves an ~empty~ slot the walk steps over, so an entry deleted before its
// turn is not visited and one deleted and re-added is visited again at the
// end. This list COMPACTS, which moved every later entry one place left
// under the walk and made it skip the next one. So a delete or a clear moves
// each live walk's cursor with the entries it moved; the registry only ever
// holds walks whose C++ frames are running.
struct live_walk {
    const array_object * entries;
    std::size_t next;
};
[[nodiscard]] std::vector<live_walk *> & live_walks() {
    static thread_local std::vector<live_walk *> walks;
    return walks;
}
struct walk_scope {
    live_walk walk;
    explicit walk_scope(const array_object * entries) : walk{entries, 0} {
        live_walks().push_back(&walk);
    }
    ~walk_scope() { std::erase(live_walks(), &walk); }
    walk_scope(const walk_scope &) = delete;
    walk_scope & operator=(const walk_scope &) = delete;
};
// The entry at `at` is gone and everything after it moved down one.
void entry_removed(const array_object & entries, std::size_t at) {
    for (live_walk * w : live_walks()) {
        if (w->entries == &entries && at < w->next) { --w->next; }
    }
}
// Every entry is gone: a walk resumes with whatever is added next.
void entries_cleared(const array_object & entries) {
    for (live_walk * w : live_walks()) {
        if (w->entries == &entries) { w->next = 0; }
    }
}

// --- %MapIteratorPrototype% / %SetIteratorPrototype%, LIVE --------------------
//
// CreateMapIterator / CreateSetIterator (24.1.5.1, 24.2.5.1) are closures over
// the collection and an index into [[MapData]]: an entry added during the
// walk is visited, one deleted before its turn is not, and a finished
// iterator stays finished. An iterator OUTLIVES the call that made it, so it
// cannot be a live_walk; it registers on its collection instead, and delete
// and clear move its index as they move the entries.
//
// ponytail: at most max_live_iterators registrations per collection, oldest
// dropped - an iterator abandoned half-way (`m.keys().next().value`) is
// otherwise held for the collection's life. One dropped past the cap walks on
// unadjusted; a weak list is the upgrade if a page ever holds 32 at once.
inline constexpr std::string_view live_iterators_key = CTBROWSER_PRIVATE_KEY("LiveIterators");
inline constexpr std::string_view iter_index_key = CTBROWSER_PRIVATE_KEY("CollectionNextIndex");
inline constexpr std::string_view iter_kind_key = CTBROWSER_PRIVATE_KEY("CollectionIterationKind");
inline constexpr std::size_t max_live_iterators = 32;
constexpr std::string_view iterated_slot(bool map) {
    return map ? CTBROWSER_PRIVATE_KEY("IteratedMap") : CTBROWSER_PRIVATE_KEY("IteratedSet");
}

[[nodiscard]] array_object * live_iterators(value collection) {
    if (!collection.is_object()) { return nullptr; }
    value * list = static_cast<object_object *>(collection.as_heap())->find(live_iterators_key);
    return list != nullptr && list->is_array() ? static_cast<array_object *>(list->as_heap())
                                               : nullptr;
}
// Each registered iterator's index, moved as `move` says.
template <typename Fn> void move_iterators(value collection, Fn && move) {
    array_object * list = live_iterators(collection);
    if (list == nullptr) { return; }
    for (const value & it : list->items) {
        value * index = static_cast<object_object *>(it.as_heap())->find(iter_index_key);
        if (index != nullptr && index->is_number()) {
            *index = value::number(move(index->as_number()));
        }
    }
}
void iterators_removed(value collection, std::size_t at) {
    move_iterators(collection, [at](double index) {
        return static_cast<double>(at) < index ? index - 1 : index;
    });
}
void iterators_cleared(value collection) {
    move_iterators(collection, [](double) { return 0.0; });
}

value collection_iterator_next(context & c, bool map) {
    const value self = c.current_this();
    auto * it = self.is_object() ? static_cast<object_object *>(self.as_heap()) : nullptr;
    const value * slot = it == nullptr ? nullptr : it->find(iterated_slot(map));
    const value * index = it == nullptr ? nullptr : it->find(iter_index_key);
    const value * kind = it == nullptr ? nullptr : it->find(iter_kind_key);
    if (slot == nullptr || index == nullptr || kind == nullptr) {
        c.throw_error("TypeError", std::string{map ? "Map" : "Set"} +
                                       " Iterator next called on an incompatible receiver");
        return value::undefined();
    }
    const value collection = *slot;
    const double at = index->is_number() ? index->as_number() : 0;
    const double which = kind->is_number() ? kind->as_number() : 0;
    if (collection.is_undefined()) { return c.iter_result(value::undefined(), true); }
    const value * held =
        collection.is_object()
            ? static_cast<object_object *>(collection.as_heap())->find(entries_slot_key)
            : nullptr;
    auto * entries = held != nullptr && held->is_array()
                         ? static_cast<array_object *>(held->as_heap())
                         : nullptr;
    if (entries == nullptr || at >= static_cast<double>(entries->items.size())) {
        // Finished for good: unregistered, and the collection let go.
        if (array_object * list = live_iterators(collection)) {
            std::erase_if(list->items, [&](value v) { return v.bits() == self.bits(); });
        }
        if (value * mine = it->find(iterated_slot(map))) { *mine = value::undefined(); }
        return c.iter_result(value::undefined(), true);
    }
    const value item = entries->items[static_cast<std::size_t>(at)];
    if (value * next = it->find(iter_index_key)) { *next = value::number(at + 1); }
    value key = item;
    value held_value = item;
    if (map) {
        const auto & pair = static_cast<array_object *>(item.as_heap())->items;
        key = pair[0];
        held_value = pair[1];
    }
    if (which == 0) { return c.iter_result(key, false); }
    if (which == 1) { return c.iter_result(held_value, false); }
    // A FRESH [key, value] each time (CreateArrayFromList) - never the pair
    // the map keeps, which a page could otherwise write through.
    const context::rooted keep_key{c, key};
    const context::rooted keep_value{c, held_value};
    const value pair = c.make_array();
    const context::rooted keep_pair{c, pair};
    auto * both = static_cast<array_object *>(pair.as_heap());
    both->items.push_back(key);
    both->items.push_back(held_value);
    return c.iter_result(pair, false);
}

// `which`: 0 keys, 1 values, 2 entries.
[[nodiscard]] value collection_iterator(context & c, value collection, bool map, int which) {
    const context::rooted keep{c, collection};
    const value proto = detail::iterator_kind_prototype(
        c, map ? "Map Iterator" : "Set Iterator",
        [map](context & cc, std::span<value>) { return collection_iterator_next(cc, map); });
    const context::rooted keep_proto{c, proto};
    const value out = c.make_object();
    const context::rooted keep_out{c, out};
    auto * it = static_cast<object_object *>(out.as_heap());
    it->prototype = proto;
    it->define(iterated_slot(map), collection, attr_none);
    it->define(iter_index_key, value::number(0), attr_none);
    it->define(iter_kind_key, value::number(which), attr_none);
    array_object * list = live_iterators(collection);
    if (list == nullptr) {
        const value made = c.make_array();
        static_cast<object_object *>(collection.as_heap())
            ->define(live_iterators_key, made, attr_none);
        list = static_cast<array_object *>(made.as_heap());
    }
    list->items.push_back(out);
    if (list->items.size() > max_live_iterators) { list->items.erase(list->items.begin()); }
    return out;
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
                made->define(entries_slot_key, c.make_array(), attr_none);
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
                // AddEntriesFromIterable (24.1.1.2) / the Set loop, ONE STEP
                // AT A TIME: the iterator is stepped, the adder called, and
                // an abrupt completion - a non-object entry, a throwing
                // `set` - CLOSES the iterator (IteratorClose, `return()`
                // called and the original throw kept). Draining the iterable
                // first ran an endless iterator to the allocation ceiling
                // and never closed anything.
                const value iterator = c.get_iterator(seed);
                if (watch.threw()) { return value::undefined(); }
                if (!iterator.is_object_like()) {
                    c.throw_error("TypeError", "the argument is not iterable");
                    return value::undefined();
                }
                const context::rooted keep_iterator{c, iterator};
                detail::iterator_record rec;
                if (!detail::iterator_direct(c, iterator, rec)) { return value::undefined(); }
                const context::rooted keep_next{c, rec.next};
                // A throw with the iterator open: close it quietly, rethrow.
                const auto fail_closing = [&](value thrown) {
                    const context::rooted keep_thrown{c, thrown};
                    detail::iterator_close_quietly(c, iterator);
                    c.throw_value(thrown);
                    return value::undefined();
                };
                for (;;) {
                    bool done = false;
                    value item = value::undefined();
                    if (!detail::iterator_step_value(c, rec, done, item)) {
                        return value::undefined();
                    }
                    if (done) { break; }
                    const context::rooted keep_item{c, item};
                    if (keyed(k)) {
                        if (!item.is_object_like()) {
                            return fail_closing(c.make_error(
                                "TypeError", "an iterator value is not an entry object"));
                        }
                        const detail::completion key = detail::fenced(c, [item](context & cc) {
                            return cc.lookup_index(item, value::number(0));
                        });
                        if (key.threw) { return fail_closing(key.result); }
                        const context::rooted keep_key{c, key.result};
                        const detail::completion held = detail::fenced(c, [item](context & cc) {
                            return cc.lookup_index(item, value::number(1));
                        });
                        if (held.threw) { return fail_closing(held.result); }
                        const context::rooted keep_held{c, held.result};
                        const value args[2] = {key.result, held.result};
                        bool threw = false;
                        value thrown = value::undefined();
                        (void)c.call_fenced(adder, args, self, threw, thrown);
                        if (threw) { return fail_closing(thrown); }
                    } else {
                        bool threw = false;
                        value thrown = value::undefined();
                        (void)c.call_fenced(adder, std::span<const value>{&item, 1}, self, threw,
                                            thrown);
                        if (threw) { return fail_closing(thrown); }
                    }
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
        proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string(name_of(k)),
                      attr_configurable);
        if (!weak(k)) {
            cx.set_prototype(keyed(k) ? context::proto_kind::map : context::proto_kind::set, proto);
            // 24.1.2.2 / 24.2.2.2 get [@@species]: `this`.
            auto * species = detail::method_native(
                cx, "get [Symbol.species]",
                [](context & c, std::span<value>) { return c.current_this(); });
            detail::install_arity(cx, species, 0);
            ctor->define_accessor(CTBROWSER_SYMBOL_KEY("species"), value::object(species),
                                  value::undefined(), attr_configurable);
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
                if (!pair->items.empty() && pair->items[0].same_value_zero(arg_at(a, 0))) {
                    entries->items.erase(entries->items.begin() + static_cast<std::ptrdiff_t>(i));
                    entry_removed(*entries, i);
                    iterators_removed(c.current_this(), i);
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
            entries_cleared(*entries);
            iterators_cleared(c.current_this());
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
            // turn, which a callback that mutates the map can see - the
            // cursor is a live_walk, which delete and clear keep in step.
            walk_scope live{entries};
            while (live.walk.next < entries->items.size()) {
                const value entry = entries->items[live.walk.next++];
                const auto & pair = static_cast<array_object *>(entry.as_heap())->items;
                const value args[3] = {pair[1], pair[0], self};
                // 24.1.3.5 step 5: the callback's `this` is thisArg. Phaser's
                // loader walks its file Sets with `list.forEach(fn, this)`.
                (void)c.call(a[0], args, arg_at(a, 1));
                if (watch.threw()) { return value::undefined(); }
                entries = self_entries(c, k, "Map.prototype.forEach");
                if (entries == nullptr) { return value::undefined(); }
                live.walk.entries = entries;
            }
            return value::undefined();
        });
        // LIVE ITERATORS (collection_iterator). They were arrays once, with a
        // comment saying `map.keys().next()` does not work - and Babylon
        // drives exactly that, by hand, for the Map of shadow generators a
        // light owns - and then snapshots, which a map changed mid-walk
        // could see through.
        const auto column = [k](context & c, int which, const char * name) -> value {
            if (self_entries(c, k, name) == nullptr) { return value::undefined(); }
            return collection_iterator(c, c.current_this(), true, which);
        };
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
            proto->define(CTBROWSER_SYMBOL_KEY("iterator"), *entries, attr_builtin);
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
            // GetIterator (GroupBy step 4), as Object.groupBy asks it: an
            // object whose @@iterator is not callable is a TypeError, not the
            // array-like iterable_values would walk instead.
            if (a[0].is_object_like() && !a[0].is_array() &&
                !c.lookup_property(a[0], CTBROWSER_SYMBOL_KEY("iterator")).is_callable()) {
                if (!watch.threw()) { c.throw_error("TypeError", "Map.groupBy: not iterable"); }
                return value::undefined();
            }
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
                // Parked if it threw; the unwind count also moves for a
                // callback that throws and catches inside itself.
                if (c.throw_pending()) { return value::undefined(); }
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
            entry_removed(*entries, static_cast<std::size_t>(at));
            iterators_removed(c.current_this(), static_cast<std::size_t>(at));
            return value::boolean(true);
        });
        if (weak(k)) { continue; }
        m("clear", 0, [k](context & c, std::span<value>) -> value {
            array_object * entries = self_entries(c, k, "Set.prototype.clear");
            if (entries == nullptr) { return value::undefined(); }
            entries->items.clear();
            entries_cleared(*entries);
            iterators_cleared(c.current_this());
            return value::undefined();
        });
        m("forEach", 1, [k](context & c, std::span<value> a) -> value {
            array_object * entries = self_entries(c, k, "Set.prototype.forEach");
            if (entries == nullptr) { return value::undefined(); }
            if (!detail::callable_arg(c, arg_at(a, 0), "callback")) { return value::undefined(); }
            const value self = c.current_this();
            const detail::unwind_watch watch{c};
            // Live, as Map's forEach is - see live_walk.
            walk_scope live{entries};
            while (live.walk.next < entries->items.size()) {
                const value item = entries->items[live.walk.next++];
                const value args[3] = {item, item, self};
                (void)c.call(a[0], args, arg_at(a, 1)); // thisArg, 24.2.3.6 step 5
                if (watch.threw()) { return value::undefined(); }
                entries = self_entries(c, k, "Set.prototype.forEach");
                if (entries == nullptr) { return value::undefined(); }
                live.walk.entries = entries;
            }
            return value::undefined();
        });
        // Live, as a Map's are (collection_iterator).
        m("values", 0, [k](context & c, std::span<value>) -> value {
            if (self_entries(c, k, "Set.prototype.values") == nullptr) {
                return value::undefined();
            }
            return collection_iterator(c, c.current_this(), false, 1);
        });
        // 24.2.3.10/11: `keys` and @@iterator are both the `values` function.
        if (value * values = proto->find("values")) {
            // Adding the first alias can reallocate the property vector.
            const value values_fn = *values;
            proto->define("keys", values_fn, attr_builtin);
            proto->define(CTBROWSER_SYMBOL_KEY("iterator"), values_fn, attr_builtin);
        }
        // A Set's `entries` pairs each member WITH ITSELF, which looks odd and
        // is the spec: it exists so a Set and a Map can be walked by one code.
        m("entries", 0, [k](context & c, std::span<value>) -> value {
            if (self_entries(c, k, "Set.prototype.entries") == nullptr) {
                return value::undefined();
            }
            return collection_iterator(c, c.current_this(), false, 2);
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
            const value out = make_set(c, {});
            const context::rooted keep_out{c, out};
            auto * list = static_cast<array_object *>(
                static_cast<object_object *>(out.as_heap())->find(entries_slot_key)->as_heap());
            const bool ok = each_key(
                c, other,
                [&](value key) {
                    const value v = normalise_key(key);
                    if (find_member(*list, v) < 0) { list->items.push_back(v); }
                    return true;
                },
                [&] { list->items = self.items; });
            return ok ? out : value::undefined();
        });
        algebra(
            "intersection",
            [](context & c, array_object & self, const set_record & other) -> value {
                const std::vector<value> mine = self.items;
                const context::rooted_values keep{c, mine};
                const value out = make_set(c, {});
                const context::rooted keep_out{c, out};
                auto * list = static_cast<array_object *>(
                    static_cast<object_object *>(out.as_heap())->find(entries_slot_key)->as_heap());
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
        algebra(
            "difference", [](context & c, array_object & self, const set_record & other) -> value {
                std::vector<value> result = self.items;
                const context::rooted_values keep{c, result};
                const value out = make_set(c, result);
                const context::rooted keep_out{c, out};
                auto * list = static_cast<array_object *>(
                    static_cast<object_object *>(out.as_heap())->find(entries_slot_key)->as_heap());
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
        algebra(
            "symmetricDifference",
            [](context & c, array_object & self, const set_record & other) -> value {
                const value out = make_set(c, {});
                const context::rooted keep_out{c, out};
                auto * list = static_cast<array_object *>(
                    static_cast<object_object *>(out.as_heap())->find(entries_slot_key)->as_heap());
                const bool ok = each_key(
                    c, other,
                    [&](value key) {
                        const value v = normalise_key(key);
                        const std::ptrdiff_t at = find_member(*list, v);
                        if (find_member(self, v) >= 0) {
                            if (at >= 0) { list->items.erase(list->items.begin() + at); }
                        } else if (at < 0) {
                            list->items.push_back(v);
                        }
                        return true;
                    },
                    [&] { list->items = self.items; });
                return ok ? out : value::undefined();
            });
        algebra("isSubsetOf",
                [](context & c, array_object & self, const set_record & other) -> value {
                    if (static_cast<double>(self.items.size()) > other.size) {
                        return value::boolean(false);
                    }
                    // BY INDEX over the live list (the proposal's step 6 walks
                    // O.[[SetData]] as it is at each step): `has` may mutate
                    // the receiver, and an element it removes is not asked about.
                    const detail::unwind_watch watch{c};
                    walk_scope live{&self};
                    while (live.walk.next < self.items.size()) {
                        const value e = self.items[live.walk.next++];
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
                    const detail::unwind_watch watch{c};
                    if (static_cast<double>(self.items.size()) <= other.size) {
                        // Live, as isSubsetOf's walk is.
                        walk_scope live{&self};
                        while (live.walk.next < self.items.size()) {
                            const value e = self.items[live.walk.next++];
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
