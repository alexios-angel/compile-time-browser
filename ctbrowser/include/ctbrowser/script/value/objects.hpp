#pragma once

#include "base.hpp"

namespace ctbrowser::script {

// --- PROPERTY ATTRIBUTES -------------------------------------------------
//
// [[Writable]], [[Enumerable]] and [[Configurable]], three bits per property.
//
// A byte rather than three bools because it is stored per property in a vector
// PARALLEL to the property table (see object_object::attrs) rather than inside
// it: widening `std::pair<std::string, value>` into a descriptor struct would
// break every `for (const auto & [key, item] : obj->props)` in the engine, the
// DOM bindings and the Shell - which is exactly the reason the accessor table
// sits beside the data properties instead of inside them.
inline constexpr std::uint8_t attr_writable = 1;
inline constexpr std::uint8_t attr_enumerable = 2;
inline constexpr std::uint8_t attr_configurable = 4;

// WHAT AN ORDINARY ASSIGNMENT AND AN OBJECT LITERAL PRODUCE: all three. The
// default for object_object::set(), which the DOM bindings are written against
// as a periodic re-`set()` of a plain data property.
inline constexpr std::uint8_t attr_default = attr_writable | attr_enumerable | attr_configurable;
// WHAT A BUILT-IN METHOD GETS (17, "Every other data property described in
// clauses 19 through 28 ... has the attributes { [[Writable]]: true,
// [[Enumerable]]: false, [[Configurable]]: true }").
inline constexpr std::uint8_t attr_builtin = attr_writable | attr_configurable;
// WHAT `Object.defineProperty` GIVES A FIELD IT WAS NOT TOLD ABOUT: nothing.
inline constexpr std::uint8_t attr_none = 0;

// One `get x()` / `set x(v)` pair, and the table they live in.
//
// A property is EITHER data or accessor, never both, which is what lets this
// sit BESIDE the data properties instead of widening every one of them into a
// descriptor. Shared by objects and closures because a CLASS is a closure:
// `static get w()` has to go somewhere, and that somewhere is the constructor.
struct accessor_entry {
    std::string key;
    value getter;
    value setter;
    // How many DATA properties existed when this accessor was defined.
    // Property order is observable in JavaScript - Object.keys and for-in both
    // report insertion order across data and accessors alike - and two separate
    // tables lose the interleaving. Recording the position restores it without
    // giving every data property a sequence number it would otherwise not need.
    std::uint32_t after = 0;
    // An accessor has no [[Writable]]: `set` present or absent IS the writable
    // question. Only the other two bits are meaningful, and the default is what
    // `get x() {}` in a class or object literal produces.
    std::uint8_t attrs = attr_enumerable | attr_configurable;
};

struct accessor_table {
    // Insertion-ordered - readers walk it - and READ THROUGH `index` past a
    // handful: a computed style declaration carries an accessor per property,
    // both spellings, and finding one of 800 by a linear scan made
    // getComputedStyle quadratic in the table it publishes.
    std::vector<accessor_entry> entries;
    string_flat_map<std::uint32_t> index;
    // Empty on the overwhelming majority of objects, so this bool is what keeps
    // property lookup as fast as it was.
    bool any = false;

    [[nodiscard]] accessor_entry * find(std::string_view name) {
        if (!any) { return nullptr; }
        if (entries.size() <= 8) {
            for (accessor_entry & entry : entries) {
                if (entry.key == name) { return &entry; }
            }
            return nullptr;
        }
        const auto it = index.find(name);
        return it == index.end() ? nullptr : &entries[it->second];
    }
    void define(std::string_view name, value getter, value setter, std::uint32_t after = 0,
                std::uint8_t attrs = attr_enumerable | attr_configurable) {
        if (accessor_entry * existing = find(name)) {
            if (!getter.is_undefined()) { existing->getter = getter; }
            if (!setter.is_undefined()) { existing->setter = setter; }
            existing->attrs = attrs;
            return;
        }
        entries.push_back(accessor_entry{std::string{name}, getter, setter, after, attrs});
        any = true;
        if (entries.size() > 8) {
            if (index.empty()) {
                for (std::uint32_t i = 0; i < entries.size(); ++i) {
                    index.emplace(entries[i].key, i);
                }
            } else {
                index.emplace(entries.back().key, static_cast<std::uint32_t>(entries.size() - 1));
            }
        }
    }
    bool erase(std::string_view name) {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].key == name) {
                entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
                any = !entries.empty();
                // The positions after it moved: rebuilt, since an erase is rare
                // (a data property redefining an accessor) and a scan is not.
                index.clear();
                if (entries.size() > 8) {
                    for (std::uint32_t k = 0; k < entries.size(); ++k) {
                        index.emplace(entries[k].key, k);
                    }
                }
                return true;
            }
        }
        return false;
    }
};

// Insertion-ordered, like a JS object, with a flat hash index over the
// property names so lookup is O(1).
struct object_object final : heap_object {
    std::vector<std::pair<std::string, value>> props;
    string_flat_map<std::uint32_t> index;
    // NULL MEANS THE IMPLICIT Object.prototype - every lookup falls through to
    // it - and UNDEFINED means an EXPLICIT null [[Prototype]]: Object.create
    // (null), setPrototypeOf(o, null), `__proto__ = null`. The chain walks in
    // vm/objects/ stop at the second without the fallback.
    value prototype = value::null();

    accessor_table accessors;

    // --- the attribute bits, PARALLEL to `props` and usually EMPTY ---------
    //
    // Entry i describes props[i]. It is grown lazily: an object all of whose
    // properties have the default attributes carries no vector at all, which is
    // every object a page makes with a literal or an assignment. That is what
    // keeps the memory and the property-store fast path exactly where they were
    // - and it is also why `attrs_at` answers `attr_default` for an index the
    // vector does not reach rather than indexing it.
    //
    // ONE PLACE IN THE ENGINE MUTATES `props` DIRECTLY past this class:
    // lib/Shell/bindings/window/window.cpp clears a localStorage table. `normalise()`
    // below is what makes that safe - every mutator calls it, so a vector left
    // longer than the table it describes is trimmed before it can answer for
    // the wrong property.
    std::vector<std::uint8_t> attrs;

    // [[Extensible]]. False after Object.preventExtensions / seal / freeze.
    bool extensible = true;

    // Does any own key look like an array index? Enumeration order depends on
    // it and almost no object has one, so recording the answer keeps the walk
    // that every for-in performs a straight line.
    bool indexed = false;

    object_object() : heap_object(heap_kind::object) {}

    void normalise() {
        if (!attrs.empty() && attrs.size() != props.size()) {
            attrs.resize(props.size(), attr_default);
        }
    }
    [[nodiscard]] std::uint8_t attrs_at(std::size_t i) const noexcept {
        return i < attrs.size() ? attrs[i] : attr_default;
    }
    [[nodiscard]] std::uint8_t attrs_of(std::string_view name) const {
        const auto it = index.find(name);
        return it == index.end() ? attr_default : attrs_at(it->second);
    }
    void set_attrs_at(std::size_t i, std::uint8_t a) {
        if (a == attr_default && attrs.empty()) { return; }
        if (attrs.size() < props.size()) { attrs.resize(props.size(), attr_default); }
        if (i < attrs.size()) { attrs[i] = a; }
    }
    void set_attrs(std::string_view name, std::uint8_t a) {
        const auto it = index.find(name);
        if (it != index.end()) { set_attrs_at(it->second, a); }
    }

    [[nodiscard]] value * find(std::string_view name) {
        // NO TEMPORARY - see the note on string_flat_map.
        const auto it = index.find(name);
        return it == index.end() ? nullptr : &props[it->second].second;
    }
    // THE SAME LOOKUP WITH THE HASH ALREADY IN HAND, for walking a prototype
    // chain: every level is asked for the SAME name, and hashing it once per
    // level was 2.25 hashes per property access on a Phaser frame.
    [[nodiscard]] value * find(prehashed_name name) {
        const auto it = index.find(name);
        return it == index.end() ? nullptr : &props[it->second].second;
    }
    [[nodiscard]] accessor_entry * find_accessor(std::string_view name) {
        return accessors.find(name);
    }
    // Defining an accessor removes any data property of the same name: they are
    // the same property, described two ways.
    void define_accessor(std::string_view name, value getter, value setter,
                         std::uint8_t a = attr_enumerable | attr_configurable) {
        (void)erase(name);
        accessors.define(name, getter, setter, static_cast<std::uint32_t>(props.size()), a);
        std::uint32_t at = 0;
        if (!indexed && array_index_key(name, at)) { indexed = true; }
    }
    bool erase_accessor(std::string_view name) { return accessors.erase(name); }

    // The straight-line walk: definition order, data and accessors interleaved.
    template <typename Fn> void each_own_entry_in_order(Fn && visit) const {
        for (std::size_t i = 0; i <= props.size(); ++i) {
            for (const accessor_entry & entry : accessors.entries) {
                if (entry.after == i) { visit(entry.key, entry.attrs); }
            }
            if (i < props.size()) { visit(props[i].first, attrs_at(i)); }
        }
    }

    // Is this key an ARRAY INDEX - 0 .. 2^32-2, spelled canonically? "01" and
    // "1.0" are ordinary string keys, and getting that wrong would move a
    // property a page can see.
    [[nodiscard]] static bool array_index_key(std::string_view key, std::uint32_t & out) noexcept {
        if (key.empty() || key.size() > 10) { return false; }
        if (key.size() > 1 && key[0] == '0') { return false; }
        std::uint64_t at = 0;
        for (const char c : key) {
            if (c < '0' || c > '9') { return false; }
            at = at * 10 + static_cast<std::uint64_t>(c - '0');
        }
        if (at > 4294967294ull) { return false; }
        out = static_cast<std::uint32_t>(at);
        return true;
    }

    // Every own property, key AND attributes, in the order
    // OrdinaryOwnPropertyKeys reports them. The one place that knows how the
    // two tables interleave, so Object.keys, for-in and getOwnPropertyNames
    // cannot disagree about it.
    //
    // INTEGER-INDEX KEYS COME FIRST, ascending, then everything else in
    // insertion order (6.1.7.1). `{2: 'a', b: 'b', 1: 'c'}` enumerates
    // "1","2","b" in every browser, and this table is insertion-ordered, so the
    // reordering has to happen here. It costs a copy and a sort - and only on
    // an object that HAS an index-shaped key, which `indexed` records as
    // properties are added, so the overwhelming majority of objects take the
    // straight-line walk they always did.
    template <typename Fn> void each_own_entry(Fn && visit) const {
        if (!indexed) {
            each_own_entry_in_order(std::forward<Fn>(visit));
            return;
        }
        std::vector<std::pair<std::uint32_t, std::pair<std::string, std::uint8_t>>> at_index;
        std::vector<std::pair<std::string, std::uint8_t>> named;
        each_own_entry_in_order([&](const std::string & key, std::uint8_t a) {
            std::uint32_t at = 0;
            if (array_index_key(key, at)) {
                at_index.emplace_back(at, std::pair{key, a});
            } else {
                named.emplace_back(key, a);
            }
        });
        std::sort(at_index.begin(), at_index.end(),
                  [](const auto & x, const auto & y) { return x.first < y.first; });
        for (const auto & entry : at_index) { visit(entry.second.first, entry.second.second); }
        for (const auto & entry : named) { visit(entry.first, entry.second); }
    }

    template <typename Fn> void each_own_key(Fn && visit) const {
        each_own_entry([&](const std::string & key, std::uint8_t) { visit(key); });
    }

    // THE SAME WALK, ENUMERABLE ONLY - what Object.keys/values/entries, for-in,
    // Object.assign, object spread and JSON.stringify are each specified to
    // see. getOwnPropertyNames and Reflect.ownKeys keep the unfiltered walks
    // above, because those two report every own property by definition.
    template <typename Fn> void each_own_enumerable_key(Fn && visit) const {
        each_own_entry([&](const std::string & key, std::uint8_t a) {
            if ((a & attr_enumerable) != 0 && !key.starts_with(symbol_key_prefix) &&
                !is_private_key(key)) {
                visit(key);
            }
        });
    }
    // AN EXISTING PROPERTY KEEPS ITS ATTRIBUTES; a new one gets `attr_default`.
    // `o.x = 1` on an existing non-writable x is NOT this function's problem -
    // see context::store_property, which is [[Set]] and does the checking.
    void set(std::string_view name, value v) {
        normalise();
        if (value * existing = find(name)) {
            *existing = v;
            return;
        }
        index.emplace(std::string{name}, static_cast<std::uint32_t>(props.size()));
        props.emplace_back(std::string{name}, v);
        if (!attrs.empty()) { attrs.push_back(attr_default); }
        std::uint32_t at = 0;
        if (!indexed && array_index_key(name, at)) { indexed = true; }
    }
    // [[DefineOwnProperty]] with the attributes stated - what a built-in
    // installation and Object.defineProperty both need, and what `set` above
    // deliberately is not.
    void define(std::string_view name, value v, std::uint8_t a) {
        set(name, v);
        set_attrs(name, a);
    }
    // `delete o.x`. The index maps names to POSITIONS in props, so removing one
    // shifts every position after it - the index is rebuilt rather than patched,
    // because delete is rare and a half-updated index is a silent wrong answer.
    bool erase(std::string_view name) {
        normalise();
        const auto it = index.find(name);
        if (it == index.end()) { return false; }
        const auto at = static_cast<std::ptrdiff_t>(it->second);
        props.erase(props.begin() + at);
        if (!attrs.empty()) { attrs.erase(attrs.begin() + at); }
        index.clear();
        for (std::uint32_t i = 0; i < props.size(); ++i) { index.emplace(props[i].first, i); }
        return true;
    }
};

// --- PRIMITIVE WRAPPER OBJECTS ---------------------------------------------
//
// `new Number(5)`, `new Boolean(false)`, `Object("ab")`: an ordinary object
// whose [[NumberData]] / [[BooleanData]] / [[StringData]] slot is one
// PRIVATE-KEYED own property. A private key (`@#...`) is what no source text
// can spell and what OwnPropertyKeys, for-in, JSON and hasOwnProperty already
// skip, so the slot is exactly as invisible as an internal slot and the object
// is otherwise the object_object every other path already handles. A String
// wrapper's `length` and indices are answered by lookup_property, own_property
// and own_property_names off this slot (10.4.3, the String exotic object).
inline constexpr std::string_view primitive_slot_key = "@#PrimitiveValue";
// The wrapped primitive, or null when `v` is not a wrapper.
[[nodiscard]] inline value * primitive_slot(value v) noexcept {
    if (!v.is_object()) { return nullptr; }
    auto * obj = static_cast<object_object *>(v.as_heap());
    return obj->props.empty() ? nullptr : obj->find(primitive_slot_key);
}

} // namespace ctbrowser::script
