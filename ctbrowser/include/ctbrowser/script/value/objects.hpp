#pragma once
//===- objects.hpp - Ordinary objects and property attributes ---*- C++ -*-===//
//
// The ordinary JavaScript object (object_object): its insertion-ordered data
// properties, the parallel attribute bits, the accessor table, the
// OwnPropertyKeys walks, and the private keys of the standard library's
// internal slots.
//
//===----------------------------------------------------------------------===//

#include "base.hpp"

namespace ctbrowser::script {

// --- Property attributes ---------------------------------------------------
//
// [[Writable]], [[Enumerable]] and [[Configurable]], three bits per property.
//
// One byte per property, kept in a vector parallel to the property table
// (object_object::attrs) rather than inside it, so `props` stays a vector of
// (key, value) pairs that every structured-binding loop over it in the
// engine, the DOM bindings and the Shell relies on. The accessor table sits
// beside the data properties for the same reason.
inline constexpr std::uint8_t attr_writable = 1;
inline constexpr std::uint8_t attr_enumerable = 2;
inline constexpr std::uint8_t attr_configurable = 4;

/// Writable, enumerable and configurable: what an ordinary assignment or an
/// object literal produces, and what object_object::set() gives a new
/// property.
inline constexpr std::uint8_t attr_default = attr_writable | attr_enumerable | attr_configurable;
/// What a built-in method gets (17, "Every other data property described in
/// clauses 19 through 28 ... has the attributes { [[Writable]]: true,
/// [[Enumerable]]: false, [[Configurable]]: true }").
inline constexpr std::uint8_t attr_builtin = attr_writable | attr_configurable;
/// What `Object.defineProperty` gives a field it was not told about: nothing.
inline constexpr std::uint8_t attr_none = 0;

/// One `get x()` / `set x(v)` pair, stored in an accessor_table.
///
/// A property is either data or accessor, never both, which is what lets the
/// table sit beside the data properties instead of widening every one of them
/// into a descriptor. Shared by objects and closures because a class is a
/// closure: `static get w()` has to go somewhere, and that somewhere is the
/// constructor.
struct accessor_entry {
    std::string key;
    value getter;
    value setter;
    /// How many data properties existed when this accessor was defined.
    /// Property order is observable in JavaScript (Object.keys and for-in both
    /// report insertion order across data and accessors alike), and two
    /// separate tables lose the interleaving. Recording the position restores
    /// it without giving every data property a sequence number.
    std::uint32_t after = 0;
    /// An accessor has no [[Writable]]: whether `set` is present is the
    /// writable question. Only the other two bits are meaningful, and the
    /// default is what `get x() {}` in a class or object literal produces.
    std::uint8_t attrs = attr_enumerable | attr_configurable;
};

/// An object's accessor properties, beside its data properties.
struct accessor_table {
    /// Insertion-ordered, since readers walk it, and looked up through `index`
    /// past a handful: a computed style declaration carries an accessor per
    /// property, in both spellings, and finding one of 800 by a linear scan
    /// made getComputedStyle quadratic in the table it publishes.
    std::vector<accessor_entry> entries;
    string_flat_map<std::uint32_t> index;
    /// Empty on the vast majority of objects, so this bool is what keeps
    /// property lookup fast.
    bool any = false;

    // Up to 8 entries a linear scan beats the hash index, so `index` is only
    // built and kept once the table grows past 8.
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
                // The positions after it moved: rebuild, since an erase is
                // rare (a data property redefining an accessor) and a scan is
                // not.
                reindex();
                return true;
            }
        }
        return false;
    }
    /// Inserts a new entry at vector position `slot`, which orders it among
    /// accessors sharing its `after`, for a data property turning into an
    /// accessor in place. Rare, so the index is rebuilt rather than patched.
    void insert(std::size_t slot, accessor_entry entry) {
        entries.insert(entries.begin() +
                           static_cast<std::ptrdiff_t>(std::min(slot, entries.size())),
                       std::move(entry));
        any = true;
        reindex();
    }
    /// Rebuilds `index` from scratch (only past 8 entries).
    void reindex() {
        index.clear();
        if (entries.size() > 8) {
            for (std::uint32_t k = 0; k < entries.size(); ++k) { index.emplace(entries[k].key, k); }
        }
    }
};

/// An ordinary JavaScript object. Insertion-ordered, like a JS object, with a
/// flat hash index over the property names so lookup is O(1).
struct object_object final : heap_object {
    std::vector<std::pair<std::string, value>> props;
    string_flat_map<std::uint32_t> index;
    /// The [[Prototype]]. Null means the implicit Object.prototype (every
    /// lookup falls through to it), and undefined means an explicit null
    /// [[Prototype]]: Object.create(null), setPrototypeOf(o, null),
    /// `__proto__ = null`. The chain walks in vm/objects/ stop at the second
    /// without the fallback.
    value prototype = value::null();

    accessor_table accessors;

    /// The attribute bits, parallel to `props` and usually empty.
    ///
    /// Entry i describes props[i]. It is grown lazily: an object all of whose
    /// properties have the default attributes, which is every object a page
    /// makes with a literal or an assignment, carries no vector at all. That
    /// keeps the memory and the property-store fast path cheap, and is why
    /// `attrs_at` answers `attr_default` for an index the vector does not reach
    /// rather than indexing it.
    ///
    /// One place outside this class mutates `props` directly:
    /// lib/Shell/bindings/window/window.cpp clears a localStorage table.
    /// `normalise()` makes that safe: every mutator calls it, so a vector left
    /// longer than the table it describes is trimmed before it can answer for
    /// the wrong property.
    std::vector<std::uint8_t> attrs;

    /// [[Extensible]]. False after Object.preventExtensions / seal / freeze.
    bool extensible = true;

    /// Whether any own key looks like an array index. Enumeration order depends
    /// on it and almost no object has one, so recording the answer keeps the
    /// walk that every for-in performs a straight line.
    bool indexed = false;

    /// Set on a derived class's prototype while its body is being evaluated,
    /// by the class_heritage native, and cleared by class_defined. A method is
    /// installed with DefineMethodProperty (15.4.4), not [[Set]], and the
    /// compiler spells it set_prop: this flag is what makes store_property
    /// define it as an own property rather than hand it to a setter or a
    /// read-only property up the parent's chain
    /// (`class S extends Set { size() {} }`).
    bool defining_class = false;

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
        // Heterogeneous lookup: string_flat_map's hash and equality are
        // transparent, so no std::string temporary is built.
        const auto it = index.find(name);
        return it == index.end() ? nullptr : &props[it->second].second;
    }
    /// The same lookup with the hash already in hand, for walking a prototype
    /// chain: every level is asked for the same name, and hashing it once per
    /// level cost 2.25 hashes per property access on a Phaser frame.
    [[nodiscard]] value * find(prehashed_name name) {
        const auto it = index.find(name);
        return it == index.end() ? nullptr : &props[it->second].second;
    }
    [[nodiscard]] accessor_entry * find_accessor(std::string_view name) {
        return accessors.find(name);
    }
    /// Defining an accessor removes any data property of the same name: they
    /// are the same property, described two ways. The accessor takes that
    /// property's place in OwnPropertyKeys (10.1.11.1 orders by creation, and a
    /// redefinition creates nothing): after the accessors that preceded it,
    /// before the ones that followed.
    void define_accessor(std::string_view name, value getter, value setter,
                         std::uint8_t a = attr_enumerable | attr_configurable) {
        std::uint32_t at = 0;
        if (!indexed && array_index_key(name, at)) { indexed = true; }
        const auto it = index.find(name);
        if (it == index.end() || accessors.find(name) != nullptr) {
            (void)erase(name);
            accessors.define(name, getter, setter, static_cast<std::uint32_t>(props.size()), a);
            return;
        }
        const std::uint32_t place = it->second;
        std::size_t slot = accessors.entries.size();
        for (std::size_t i = 0; i < accessors.entries.size(); ++i) {
            if (accessors.entries[i].after > place) {
                slot = i;
                break;
            }
        }
        (void)erase(name); // Shifts the later accessors' positions down.
        accessors.insert(slot, accessor_entry{std::string{name}, getter, setter, place, a});
    }
    bool erase_accessor(std::string_view name) { return accessors.erase(name); }
    /// The converse of define_accessor: a data property where an accessor of
    /// that name stands takes the accessor's place, as define_accessor keeps a
    /// data property's. A name with no accessor is an ordinary define.
    void define_over_accessor(std::string_view name, value v, std::uint8_t a) {
        accessor_entry * entry = accessors.find(name);
        if (entry == nullptr) {
            define(name, v, a);
            return;
        }
        normalise();
        const std::uint32_t place =
            std::min<std::uint32_t>(entry->after, static_cast<std::uint32_t>(props.size()));
        const auto own = static_cast<std::size_t>(entry - accessors.entries.data());
        // Accessors positioned after this one now follow the data property.
        for (std::size_t i = 0; i < accessors.entries.size(); ++i) {
            accessor_entry & other = accessors.entries[i];
            if (other.after > place || (other.after == place && i > own)) { ++other.after; }
        }
        (void)accessors.erase(name);
        props.insert(props.begin() + static_cast<std::ptrdiff_t>(place), {std::string{name}, v});
        if (!attrs.empty()) {
            attrs.insert(attrs.begin() + static_cast<std::ptrdiff_t>(place), attr_default);
        }
        index.clear();
        for (std::uint32_t i = 0; i < props.size(); ++i) { index.emplace(props[i].first, i); }
        set_attrs_at(place, a);
    }

    /// The straight-line walk: definition order, data and accessors
    /// interleaved. It runs over a snapshot, because a visitor may run a page
    /// getter (object spread reads each property) that adds or deletes
    /// properties here, reallocating the tables under a live iterator; and
    /// OwnPropertyKeys is a list taken once.
    template <typename Fn> void each_own_entry_in_order(Fn && visit) const {
        std::vector<std::pair<std::string, std::uint8_t>> order;
        order.reserve(props.size() + accessors.entries.size());
        for (std::size_t i = 0; i <= props.size(); ++i) {
            for (const accessor_entry & entry : accessors.entries) {
                if (entry.after == i) { order.emplace_back(entry.key, entry.attrs); }
            }
            if (i < props.size()) { order.emplace_back(props[i].first, attrs_at(i)); }
        }
        for (const auto & [key, a] : order) { visit(key, a); }
    }

    /// Whether this key is an array index (0 .. 2^32-2, spelled canonically),
    /// stored in `out`. "01" and "1.0" are ordinary string keys, and getting
    /// that wrong would move a property a page can see.
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

    /// Every own property, key and attributes, in the order
    /// OrdinaryOwnPropertyKeys reports them. The one place that knows how the
    /// two tables interleave, so Object.keys, for-in and getOwnPropertyNames
    /// cannot disagree about it.
    ///
    /// Integer-index keys come first, ascending, then everything else in
    /// insertion order (6.1.7.1). `{2: 'a', b: 'b', 1: 'c'}` enumerates
    /// "1","2","b" in every browser, and this table is insertion-ordered, so
    /// the reordering has to happen here. It costs a copy and a sort, and only
    /// on an object that has an index-shaped key, which `indexed` records as
    /// properties are added, so the vast majority of objects take the
    /// straight-line walk.
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

    /// The same walk, enumerable only: what Object.keys/values/entries, for-in,
    /// Object.assign, object spread and JSON.stringify are each specified to
    /// see. getOwnPropertyNames and Reflect.ownKeys keep the unfiltered walks
    /// above, because those two report every own property by definition.
    /// String keys only: every symbol key, whether from `Symbol()`,
    /// `Symbol.for` or a well-known symbol, starts with symbol_key_mark and is
    /// skipped, as are private keys.
    template <typename Fn> void each_own_enumerable_key(Fn && visit) const {
        each_own_entry([&](const std::string & key, std::uint8_t a) {
            if ((a & attr_enumerable) != 0 && !key.starts_with(symbol_key_mark) &&
                !is_private_key(key)) {
                visit(key);
            }
        });
    }
    /// Sets a data property. An existing property keeps its attributes; a new
    /// one gets `attr_default`. `o.x = 1` on an existing non-writable x is not
    /// this function's problem: see context::store_property, which is [[Set]]
    /// and does the checking.
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
    /// [[DefineOwnProperty]] with the attributes stated: what a built-in
    /// installation and Object.defineProperty both need, and what `set` above
    /// deliberately is not.
    void define(std::string_view name, value v, std::uint8_t a) {
        set(name, v);
        set_attrs(name, a);
    }
    /// `delete o.x`. The index maps names to positions in props, so removing
    /// one shifts every position after it. The index is rebuilt rather than
    /// patched, because delete is rare and a half-updated index is a silent
    /// wrong answer.
    bool erase(std::string_view name) {
        normalise();
        const auto it = index.find(name);
        if (it == index.end()) { return false; }
        const auto at = static_cast<std::ptrdiff_t>(it->second);
        props.erase(props.begin() + at);
        if (!attrs.empty()) { attrs.erase(attrs.begin() + at); }
        index.clear();
        for (std::uint32_t i = 0; i < props.size(); ++i) { index.emplace(props[i].first, i); }
        // An accessor defined after the erased property sat one data property
        // further along than it now does.
        for (accessor_entry & entry : accessors.entries) {
            if (entry.after > static_cast<std::uint32_t>(at)) { --entry.after; }
        }
        return true;
    }
};

// --- Primitive wrapper objects ----------------------------------------------
//
// `new Number(5)`, `new Boolean(false)`, `Object("ab")`: an ordinary object
// whose [[NumberData]] / [[BooleanData]] / [[StringData]] slot is one
// private-keyed own property. A private key (the internal lead byte, then `#`;
// see CTBROWSER_PRIVATE_KEY) is what no source text can spell and what
// OwnPropertyKeys, for-in, JSON and hasOwnProperty already skip, so the slot
// is exactly as invisible as an internal slot and the object is otherwise the
// object_object every other path already handles. A String wrapper's `length`
// and indices are answered by lookup_property, own_property and
// own_property_names off this slot (10.4.3, the String exotic object).
inline constexpr std::string_view primitive_slot_key = CTBROWSER_PRIVATE_KEY("PrimitiveValue");
// The standard library's other internal slots, private-keyed for the same
// reason: natives cast whatever they find in these slots, so a page must not
// be able to read, overwrite or forge them on a plain object (with ordinary
// string keys, `m.__entries = [1.5e-300]; m.get(0)` did exactly that). In
// order: a Map/Set/WeakMap/WeakSet's entry list; an array iterator's list,
// cursor and kind; a generator's coroutine; a promise's state, result and
// reaction list; a Date's time value.
inline constexpr std::string_view entries_slot_key = CTBROWSER_PRIVATE_KEY("Entries");
inline constexpr std::string_view iterated_list_key = CTBROWSER_PRIVATE_KEY("IteratedList");
inline constexpr std::string_view iterated_index_key = CTBROWSER_PRIVATE_KEY("IteratedIndex");
inline constexpr std::string_view iterator_kind_key = CTBROWSER_PRIVATE_KEY("IteratorKind");
inline constexpr std::string_view generator_slot_key = CTBROWSER_PRIVATE_KEY("GeneratorState");
inline constexpr std::string_view promise_settled_key = CTBROWSER_PRIVATE_KEY("PromiseSettled");
inline constexpr std::string_view promise_result_key = CTBROWSER_PRIVATE_KEY("PromiseResult");
inline constexpr std::string_view promise_rejected_key = CTBROWSER_PRIVATE_KEY("PromiseRejected");
inline constexpr std::string_view promise_reactions_key = CTBROWSER_PRIVATE_KEY("PromiseReactions");
inline constexpr std::string_view date_value_key = CTBROWSER_PRIVATE_KEY("DateValue");
/// The wrapped primitive, or null when `v` is not a wrapper.
[[nodiscard]] inline value * primitive_slot(value v) noexcept {
    if (!v.is_object()) { return nullptr; }
    auto * obj = static_cast<object_object *>(v.as_heap());
    return obj->props.empty() ? nullptr : obj->find(primitive_slot_key);
}

} // namespace ctbrowser::script
