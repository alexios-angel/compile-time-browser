// ctbrowser.script context - property lookup and storage, and the mark-sweep collector.
//
// One of four files carved out of a 3,232-line vm.cpp on 2026-08-09. All
// members of `context`, declared in include/ctbrowser/script/vm.hpp - so
// they split across translation units with nothing to declare.

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/number_format.hpp>
#include <ctbrowser/script/vm.hpp>

// The VM's implementation.
//
// `run_loop` alone is 15 KB of object code - the whole instruction dispatch -
// and while it lived in the interface every translation unit that imported the
// module emitted its own copy and optimised it again. The class declaration
// stays in :vm; the bodies live here and are compiled once.

namespace ctbrowser::script {

// ===================== gc ================================================

// THE MARK PHASE, AS A LOOP. `push_mark` greys and `trace_object` blackens;
// neither can call the other, so the C++ stack is O(1) in the shape of the
// object graph rather than O(its longest chain). See mark_worklist_ in vm.hpp
// for what that cost was.
//
// The drain is here rather than in the header because trace_object is: one
// copy of the per-kind edge table, and nothing else in the engine may have an
// opinion about what an object points at.
void context::mark_object(heap_object * o) {
    push_mark(o);
    while (!mark_worklist_.empty()) {
        heap_object * grey = mark_worklist_.back();
        mark_worklist_.pop_back();
        trace_object(grey);
    }
}

void context::trace_object(heap_object * o) {
    // EVERY EDGE GOES THROUGH push_mark, NOT THROUGH mark_object. mark_object
    // drains, so calling it from here would put the whole recursion back one
    // level down and the fix would measure as working while doing nothing.
    const auto edge = [this](value v) {
        if (v.is_heap()) { push_mark(v.as_heap()); }
    };
    switch (o->kind) {
    case heap_kind::array: {
        auto * arr = static_cast<array_object *>(o);
        for (const value & v : arr->items) { edge(v); }
        // AND THE SPARSE HALF. An element that is only reachable through
        // `sparse` is reachable, and a collector that walked `items` alone
        // would free it under a page that can still read it by index.
        for (const auto & [index, v] : arr->sparse) { edge(v); }
        edge(arr->viewed);
        edge(arr->index);
        edge(arr->input);
        edge(arr->groups);
        break;
    }
    case heap_kind::object: {
        auto * obj = static_cast<object_object *>(o);
        for (const auto & [name, v] : obj->props) { edge(v); }
        for (const accessor_entry & entry : obj->accessors.entries) {
            edge(entry.getter);
            edge(entry.setter);
        }
        edge(obj->prototype);
        break;
    }
    case heap_kind::cell: edge(static_cast<cell_object *>(o)->slot); break;
    case heap_kind::function: {
        auto * closure = static_cast<closure_object *>(o);
        // A closure OWNS its upvalue cells. Missing this frees a captured
        // variable while the closure that captured it is still reachable.
        for (const value & up : closure->upvalues) { edge(up); }
        // ...and its own properties, which is where a class keeps its statics
        // and its prototype.
        for (const auto & [name, v] : closure->props) { edge(v); }
        for (const accessor_entry & entry : closure->accessors.entries) {
            edge(entry.getter);
            edge(entry.setter);
        }
        // ...and an arrow's captured `this`, which nothing else can reach.
        edge(closure->captured_this);
        edge(closure->proto_link);
        break;
    }
    case heap_kind::native: {
        auto * fn = static_cast<native_object *>(o);
        for (const auto & [name, v] : fn->props) { edge(v); }
        // ...AND WHAT ITS C++ LAMBDA CAPTURED. A capture is invisible to a
        // precise collector - it lives inside a std::function's erased
        // storage, which no root walk can reach - so a native that closed over
        // a value held a pointer the sweep was free to free. See
        // native_object::retained.
        for (const value & v : fn->retained) { edge(v); }
        // ...AND ITS OWN [[Prototype]]. `TypeError.__proto__` is `Error`, and a
        // constructor reachable only through another one would otherwise be
        // swept out from under it.
        edge(fn->proto_link);
        break;
    }
    case heap_kind::proxy: {
        auto * proxy = static_cast<proxy_object *>(o);
        edge(proxy->target);
        edge(proxy->handler);
        break;
    }
    case heap_kind::coroutine: {
        // A SUSPENDED FRAME IS A ROOT LIKE ANY OTHER. Its registers are the
        // only reference to everything the function had in hand, and they are
        // out of the register stack the collector normally walks - so without
        // this every local of every waiting function is freed.
        auto * saved = static_cast<coroutine_object *>(o);
        for (const value & v : saved->window) { edge(v); }
        edge(saved->receiver);
        edge(saved->promise);
        push_mark(saved->closure);
        break;
    }
    default: break; // strings and symbols own no values
    }
}

void context::mark(value v) {
    if (v.is_heap()) { mark_object(v.as_heap()); }
}

// VM_CASE(load_string)'s memo, extracted so a compiled body shares it.
value context::interned_bigint_literal(const void * site, std::uint32_t slot,
                                       std::string_view text) {
    const auto parse = [&] {
        // A LITERAL THE LEXER ACCEPTED BUT THAT IS NOT AN INTEGER - `1.5n` -
        // has no value to load, and 0n is the honest answer for a program that
        // should have been refused at compile time. It does NOT throw.
        const std::optional<bigint> parsed = bigint_from_literal(std::string{text});
        return value::object(allocate<bigint_object>(parsed.value_or(bigint{0})));
    };
    if (site == nullptr) { return parse(); }
    auto & cache = bigint_cache_[site];
    const auto found = cache.find(slot);
    if (found != cache.end()) { return found->second; }
    const value made = parse();
    cache.emplace(slot, made);
    return made;
}

value context::interned_string(const void * site, std::uint32_t slot, std::string_view text) {
    if (site == nullptr) { return string(std::string{text}); }
    auto & cache = string_cache_[site];
    const auto found = cache.find(slot);
    if (found != cache.end()) { return found->second; }
    const value made = string(std::string{text});
    cache.emplace(slot, made);
    return made;
}

value context::lookup_index(value target, value key) {
    if (target.is_array() && key.is_number()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        const auto i = static_cast<std::ptrdiff_t>(key.as_number());
        if (arr->is_view()) {
            return i >= 0 && static_cast<std::size_t>(i) < arr->length()
                       ? value::number(view_get(*arr, static_cast<std::size_t>(i)))
                       : value::undefined();
        }
        if (i >= 0 && static_cast<std::size_t>(i) < arr->items.size()) {
            return arr->items[static_cast<std::size_t>(i)];
        }
        // AND THE SPARSE HALF, second and behind an empty test so that reading
        // past the end of an ordinary array is the one branch it was before.
        if (!arr->sparse.empty() && i >= 0 && static_cast<std::uint64_t>(i) <= 4294967295ull) {
            if (value * found = arr->find_sparse(static_cast<std::uint32_t>(i))) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_string() && key.is_number()) {
        const std::string & text = static_cast<string_object *>(target.as_heap())->text;
        const auto i = static_cast<std::size_t>(key.as_number());
        return i < text.size() ? string(std::string{text[i]}) : value::undefined();
    }
    return lookup_property(target, to_string(key));
}

// op::set_index's body, extracted verbatim so the interpreter and a compiled
// body run one implementation rather than two - ct_aot_set_index is the other
// caller.
// VM_CASE(append) verbatim.
void context::array_append(value target, value v) {
    if (target.is_array()) { static_cast<array_object *>(target.as_heap())->items.push_back(v); }
}

void context::store_index(value target, value key, value v) {
    if (target.is_array() && key.is_number()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        const auto i = static_cast<std::ptrdiff_t>(key.as_number());
        // A TYPED ARRAY COERCES ON WRITE AND DOES NOT GROW. Both are what makes
        // it typed: `pixels[i] = 300` is 255 in a clamped byte array, and a
        // write past the end is DROPPED rather than extending it.
        if (arr->is_view()) {
            if (i >= 0 && static_cast<std::size_t>(i) < arr->length()) {
                view_set(*arr, static_cast<std::size_t>(i), to_number(v));
            }
            return;
        }
        if (arr->elements != element_kind::none) {
            if (i >= 0 && static_cast<std::size_t>(i) < arr->items.size()) {
                arr->items[static_cast<std::size_t>(i)] =
                    value::number(coerce_element(arr->elements, to_number(v)));
            }
            return;
        }
        if (i >= 0) {
            const auto index = static_cast<std::uint64_t>(i);
            if (index < arr->items.size()) {
                // FROZEN MEANS FROZEN. Silently in sloppy mode - TODO(strict):
                // this is a TypeError under "use strict", which the engine does
                // not have (docs/test262.md names the gap).
                if (!arr->elements_writable) { return; }
                arr->items[static_cast<std::size_t>(index)] = v;
                return;
            }
            // A SEALED OR FROZEN ARRAY GAINS NO ELEMENTS.
            if (!arr->extensible) { return; }
            // HOW MANY SLOTS THIS ONE WRITE WOULD MATERIALISE. `a[4294967295]
            // = "x"` asked for 34 GB and std::bad_alloc ended the process; the
            // test is on the SIZE OF THE JUMP so that a sequential fill, whose
            // jump is always one, is untouched. See array_object::dense_limit.
            if (index <= 4294967295ull &&
                index + 1 - arr->items.size() > array_object::dense_limit) {
                arr->set_sparse(static_cast<std::uint32_t>(index), v);
                return;
            }
            // Past 2^32-1 a numeric key is not an index and not a slot either;
            // it is an ordinary property, which an array here cannot hold.
            if (index > 4294967295ull) { return; }
            arr->items.resize(static_cast<std::size_t>(index) + 1, value::undefined());
            arr->items[static_cast<std::size_t>(index)] = v;
        }
        return;
    }
    store_property(target, to_string(key), v);
}

void context::pass_new_target(value from) {
    pending_new_target_ = from;
}

void context::delete_named(value target, const std::string & name) {
    // TODO(strict): a false answer here is a TypeError under "use strict". The
    // engine has no strict mode, and sloppy `delete` evaluates to false without
    // throwing - which is what the compiler emits today (a constant `true`; see
    // the note on delete_own_property).
    (void)delete_own_property(target, name);
}

value context::own_keys(value source) {
    value out = make_array();
    auto * keys = static_cast<array_object *>(out.as_heap());
    if (source.is_object()) {
        // In DEFINITION ORDER, data and accessors interleaved - which is what a
        // page sees from for-in and has to match Object.keys. for-in
        // enumerates STRING keys only, and ENUMERABLE ones only: 13.7.5.15
        // filters on [[Enumerable]], which is why a built-in method never turns
        // up in a `for (k in Math)`.
        static_cast<object_object *>(source.as_heap())
            ->each_own_enumerable_key(
                [&](const std::string & name) { keys->items.push_back(string(name)); });
    } else if (source.is_array()) {
        const std::size_t n = static_cast<array_object *>(source.as_heap())->items.size();
        for (std::size_t i = 0; i < n; ++i) { keys->items.push_back(string(std::to_string(i))); }
    }
    return out;
}

void context::define_accessor(value target, const std::string & name, value getter, value setter) {
    if (target.is_object()) {
        static_cast<object_object *>(target.as_heap())->define_accessor(name, getter, setter);
    } else if (target.is_kind(heap_kind::function)) {
        // a `static get` on a class, which IS the constructor closure
        static_cast<closure_object *>(target.as_heap())->define_accessor(name, getter, setter);
    }
}

void context::copy_own_properties(value target, value source) {
    if (!target.is_object()) { return; }
    auto * into = static_cast<object_object *>(target.as_heap());
    if (source.is_object()) {
        // A COPY OF THE SOURCE'S ENTRIES FIRST: `set` can reallocate the
        // target's storage, and target and source may be the same object.
        //
        // ENUMERABLE OWN PROPERTIES ONLY. `{...o}` is CopyDataProperties
        // (7.3.25), which skips a non-enumerable one - so spreading a class
        // instance no longer drags its prototype's plumbing along.
        //
        // A SYMBOL KEY IS COPIED, unlike in Object.keys or for-in:
        // CopyDataProperties takes OwnPropertyKeys, which reports both. That is
        // the one place the enumerable walk must NOT filter symbols, and
        // filtering them cost four tests (…/spread-obj-symbol-property.js).
        //
        // ...and an accessor is READ rather than copied: the spec does a Get,
        // so what lands on the target is the getter's answer as a data property.
        std::vector<std::pair<std::string, value>> entries;
        auto * from = static_cast<object_object *>(source.as_heap());
        from->each_own_entry([&](const std::string & name, std::uint8_t attrs) {
            if ((attrs & attr_enumerable) != 0) {
                entries.emplace_back(name, lookup_property(source, name));
            }
        });
        for (const auto & [name, item] : entries) { into->set(name, item); }
    } else if (source.is_array()) {
        const std::vector<value> items = static_cast<array_object *>(source.as_heap())->items;
        for (std::size_t i = 0; i < items.size(); ++i) { into->set(std::to_string(i), items[i]); }
    }
}

value context::get_prototype(value target) {
    return target.is_object() ? static_cast<object_object *>(target.as_heap())->prototype
                              : value::undefined();
}

void context::set_prototype(value target, value proto) {
    if (target.is_object()) { static_cast<object_object *>(target.as_heap())->prototype = proto; }
}

bool context::has_property(value target, value key) {
    // A PROXY ANSWERS `in` ITSELF, or hands it to the target.
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        const value trap = proxy_trap(target, "has");
        if (trap.is_callable()) {
            const value args[2] = {p->target, key};
            return truthy(call(trap, args, p->handler));
        }
        return !lookup_property(p->target, to_string(key)).is_undefined();
    }
    // --- HasProperty, 7.3.11: THE WHOLE CHAIN, not the own table ----------
    //
    // `in` used to answer about own DATA properties of an object_object and
    // about array indices, and about nothing else - so `'toString' in {}` was
    // false, `'x' in obj` was false for an accessor, `'length' in [1]` was
    // false, and an inherited property was invisible to the operator whose
    // entire job is to see one. It is also what ToPropertyDescriptor asks with,
    // which is how a descriptor object built by `new Con()` over a prototype
    // carrying a `writable` getter described nothing at all.
    //
    // own_property is the shared [[GetOwnProperty]] over all four tables, so
    // this is that walked up the chain: the explicit prototype links first,
    // then the implicit tables property lookup falls back to.
    const std::string name = to_string(key);
    property_descriptor found;
    if (own_property(target, name, found)) { return true; }
    value link = target.is_object() ? static_cast<object_object *>(target.as_heap())->prototype
                                    : value::undefined();
    // A depth cap because a page can make the chain cyclic, exactly as
    // lookup_property does.
    for (int depth = 0; depth < 64 && link.is_object(); ++depth) {
        if (own_property(link, name, found)) { return true; }
        link = static_cast<object_object *>(link.as_heap())->prototype;
    }
    for (object_object * table : implicit_prototypes(target)) {
        if (table != nullptr &&
            (table->find(name) != nullptr || table->find_accessor(name) != nullptr)) {
            return true;
        }
    }
    // ...and a function's STATIC chain, which is a third kind of link again.
    for (value up = target; up.is_callable();) {
        if (up.is_kind(heap_kind::function)) {
            auto * fn = static_cast<closure_object *>(up.as_heap());
            up = fn->proto_link;
        } else if (up.is_kind(heap_kind::native)) {
            up = value::null();
        } else {
            break;
        }
        if (!up.is_callable()) { break; }
        if (own_property(up, name, found)) { return true; }
    }
    return false;
}

bool context::instance_of(value target, value ctor) {
    value wanted = value::undefined();
    if (ctor.is_kind(heap_kind::function)) {
        wanted = ensure_prototype(ctor);
    } else if (ctor.is_kind(heap_kind::native)) {
        // A BUILT-IN constructor is a native, and every one of them is now
        // something a page can extend - `class E extends Error`. Without this,
        // `e instanceof Error` was false for every one.
        if (value * p = static_cast<native_object *>(ctor.as_heap())->find("prototype")) {
            wanted = *p;
        }
    } else if (ctor.is_object()) {
        if (value * p = static_cast<object_object *>(ctor.as_heap())->find("prototype")) {
            wanted = *p;
        }
    }
    if (!wanted.is_object()) { return false; }
    // The EXPLICIT chain first - a page's own classes, and every builtin whose
    // instances carry a prototype (Error, Map, Blob).
    value link = target.is_object() ? static_cast<object_object *>(target.as_heap())->prototype
                                    : value::undefined();
    for (int depth = 0; depth < 64 && link.is_object(); ++depth) {
        if (link.as_heap() == wanted.as_heap()) { return true; }
        link = static_cast<object_object *>(link.as_heap())->prototype;
    }
    // Then the IMPLICIT one. An array, a function, a string and a plain object
    // have no prototype field to walk - their chain is the tables property
    // lookup falls back to - so instanceof answered false for every builtin
    // while answering correctly for a page's own classes.
    //
    // OBJECT-LIKE ONLY. `5 instanceof Number` and `'x' instanceof String` are
    // FALSE in JavaScript however many methods a primitive resolves -
    // instanceof asks about a prototype chain and a primitive does not have
    // one. Applying the fallback to everything made both of those true, which
    // is the mirror image of the bug being fixed.
    if (target.is_object_like()) {
        for (object_object * table : implicit_prototypes(target)) {
            if (table != nullptr && table == wanted.as_heap()) { return true; }
        }
    }
    return false;
}

void context::delete_index(value target, value key) {
    (void)delete_own_property(target, to_string(key));
}

void context::store_property(value target, const std::string & name, value v) {
    // A proxy's `set` trap first: it is the only thing that can decide the
    // write does not land on the target at all, which is the point of it.
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        const value trap = proxy_trap(target, "set");
        if (trap.is_callable()) {
            const value args[4] = {p->target, string(name), v, target};
            (void)call(trap, args, p->handler);
            return;
        }
        store_property(p->target, name, v);
        return;
    }
    if (target.is_object()) {
        if (assign_through_accessor(target, name, v)) { return; }
        auto * obj = static_cast<object_object *>(target.as_heap());
        // --- [[Set]], 10.1.9, AND THE THREE BITS IT CONSULTS ---------------
        //
        // An assignment is not a definition. An own data property that is not
        // writable rejects the write; so does an INHERITED one, which is the
        // half that surprises people - `Object.freeze(proto)` stops a write
        // through every instance. And a fresh property needs the receiver to be
        // extensible.
        //
        // TODO(strict): each of these three is a TypeError under "use strict".
        // This engine has no strict mode at all (docs/test262.md names the gap
        // and the 678 onlyStrict tests it silently runs sloppy), so the write
        // is DISCARDED, which is exactly what sloppy mode does. When a strict
        // mode arrives, these three `return`s are where it throws.
        // ONE HASH LOOKUP ON THE HIT PATH, not two: `find` then `set` would
        // hash the name twice, and this is the hottest write in the engine.
        obj->normalise();
        if (const auto it = obj->index.find(name); it != obj->index.end()) {
            if ((obj->attrs_at(it->second) & attr_writable) == 0) { return; }
            obj->props[it->second].second = v;
            return;
        }
        for (value up = obj->prototype; up.is_object();) {
            auto * parent = static_cast<object_object *>(up.as_heap());
            if (parent->find(name) != nullptr) {
                if ((parent->attrs_of(name) & attr_writable) == 0) { return; }
                break;
            }
            up = parent->prototype;
        }
        if (!obj->extensible) { return; }
        obj->set(name, v);
        return;
    }
    // `a.length = n` RESIZES THE ARRAY, and dropping the write silently is not
    // a small gap: `a.length = 0` is how a great deal of code empties one -
    // Phaser's scene manager ends its boot with `this._pending.length = 0`, and
    // with the write ignored the queue it had just drained was still full, so
    // the next frame added the same scene a second time and threw "Cannot add
    // Scene with duplicate key". An engine that reads `length` but will not
    // write it looks like it supports arrays right up until it does not.
    //
    // Growing pads with undefined, which is what the spec says and what
    // `a.length = 10` is occasionally used for.
    if (target.is_array()) {
        if (name != "length") { return; } // arrays have no property table here
        auto * arr = static_cast<array_object *>(target.as_heap());
        // A TYPED array's length is fixed - it is a view over bytes that were
        // sized once, and resizing it here would leave the view and its buffer
        // disagreeing. The spec makes the write a no-op, not an error.
        if (arr->elements != element_kind::none) { return; }
        // A RangeError, WHICH IT USED TO SWALLOW. 10.4.2.4 step 3 makes any
        // length that is not a uint32 a RangeError, and dropping the write
        // instead was leniency bought at the cost of a test that checks for the
        // throw (S15.4.5.2_A3_T3) - and, for a length in range, of a resize
        // that asked for 34 GB. set_js_length records what it will not
        // materialise; see array_object::dense_limit.
        if (!arr->set_js_length(to_number(v))) {
            throw_error("RangeError", "Invalid array length");
        }
        return;
    }
    if (target.is_kind(heap_kind::native)) {
        auto * fn = static_cast<native_object *>(target.as_heap());
        // The same three checks as an object's - see above, TODO(strict) and
        // all. `Array.prototype = x` is the one every page tries by accident.
        if (fn->find(name) != nullptr) {
            if ((fn->attrs_of(name) & attr_writable) == 0) { return; }
        } else if (!fn->extensible) {
            return;
        }
        fn->set(name, v);
        return;
    }
    if (target.is_kind(heap_kind::function)) {
        auto * closure = static_cast<closure_object *>(target.as_heap());
        if (accessor_entry * entry = closure->find_accessor(name);
            entry != nullptr && entry->setter.is_callable()) {
            const value args[1] = {v};
            (void)call(entry->setter, args, target);
        } else {
            if (closure->find(name) != nullptr) {
                if ((closure->attrs_of(name) & attr_writable) == 0) { return; }
            } else if (!closure->extensible) {
                return;
            }
            closure->set(name, v);
        }
    }
    // A write to a number, a string or undefined is silently dropped, which is
    // what non-strict JavaScript does.
}

bool context::assign_through_accessor(value target, const std::string & name, value v) {
    if (!target.is_object()) { return false; }
    auto * obj = static_cast<object_object *>(target.as_heap());
    for (int depth = 0; obj != nullptr && depth < 64; ++depth) {
        // An own DATA property wins outright: it is the same property, and it
        // is not an accessor, so nothing on the prototype gets a say.
        if (obj->find(name) != nullptr) { return false; }
        if (accessor_entry * entry = obj->find_accessor(name)) {
            if (entry->setter.is_callable()) {
                const value args[1] = {v};
                (void)call(entry->setter, args, target);
                return true;
            }
            // Getter with no setter: the write is DISCARDED, as in strict-mode
            // JavaScript minus the throw. Silently defining a data property
            // over it would shadow the getter forever.
            return true;
        }
        obj = obj->prototype.is_object() ? static_cast<object_object *>(obj->prototype.as_heap())
                                         : nullptr;
    }
    return false;
}

namespace {

// SameValue (7.2.11) - `===` except that it separates the two zeros and calls
// NaN equal to itself, which is what ValidateAndApplyPropertyDescriptor
// compares descriptor fields with.
[[nodiscard]] bool descriptor_same_value(value a, value b) {
    if (a.is_number() && b.is_number()) {
        const double x = a.as_number();
        const double y = b.as_number();
        if (std::isnan(x) && std::isnan(y)) { return true; }
        return x == y && std::signbit(x) == std::signbit(y);
    }
    return a.strict_equals(b);
}

// Is this key an index into `items`, and which one?
[[nodiscard]] bool index_key(const std::string & name, std::uint32_t & out) {
    return object_object::array_index_key(name, out);
}

} // namespace

// --- [[GetOwnProperty]] ---------------------------------------------------
//
// FOUR TABLES AND A HANDFUL OF SYNTHESISED SLOTS, behind one answer. Before
// this, `Object.getOwnPropertyDescriptor` handled object_object and nothing
// else, so it answered undefined for `Array.prototype.indexOf.name`, for
// `[1,2].length` and for every static on a built-in constructor - which is the
// first thing test262's verifyProperty asks about any of them.
bool context::own_property(value target, const std::string & name, property_descriptor & out) {
    out = property_descriptor{};

    // A proxy has no ownKeys/getOwnPropertyDescriptor trap here, so the
    // question goes to the target - the same fall-through every other absent
    // trap takes.
    if (target.is_kind(heap_kind::proxy)) {
        return own_property(static_cast<proxy_object *>(target.as_heap())->target, name, out);
    }

    if (target.is_object()) {
        // DATA FIRST, then the accessor table - the order lookup_property uses,
        // so a descriptor can never describe a property `.` would not read.
        auto * obj = static_cast<object_object *>(target.as_heap());
        if (value * held = obj->find(name)) {
            out = property_descriptor::data(*held, obj->attrs_of(name));
            return true;
        }
        if (accessor_entry * entry = obj->find_accessor(name)) {
            out.has_get = out.has_set = true;
            out.getter = entry->getter;
            out.setter = entry->setter;
            out.has_enumerable = out.has_configurable = true;
            out.enumerable = (entry->attrs & attr_enumerable) != 0;
            out.configurable = (entry->attrs & attr_configurable) != 0;
            return true;
        }
        return false;
    }

    if (target.is_array()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        if (name == "length") {
            // 10.4.2: { [[Writable]]: true, [[Enumerable]]: false,
            // [[Configurable]]: false }. Freezing clears the writable bit.
            out = property_descriptor::data(value::number(static_cast<double>(arr->js_length())),
                                            arr->elements_writable ? attr_writable : attr_none);
            out.virtual_slot = true;
            return true;
        }
        std::uint32_t at = 0;
        if (index_key(name, at)) {
            const std::uint8_t a = static_cast<std::uint8_t>(
                attr_enumerable | (arr->elements_writable ? attr_writable : 0) |
                (arr->elements_configurable ? attr_configurable : 0));
            if (arr->is_view()) {
                if (at < arr->length()) {
                    out = property_descriptor::data(value::number(view_get(*arr, at)), a);
                    out.virtual_slot = true;
                    return true;
                }
                return false;
            }
            if (at < arr->items.size()) {
                out = property_descriptor::data(arr->items[at], a);
                out.virtual_slot = true;
                return true;
            }
            if (value * found = arr->find_sparse(at)) {
                out = property_descriptor::data(*found, a);
                out.virtual_slot = true;
                return true;
            }
        }
        return false;
    }

    if (target.is_string()) {
        const std::string & text = static_cast<string_object *>(target.as_heap())->text;
        if (name == "length") {
            // 10.4.3.5: a String exotic object's length is { false, false, false }.
            out = property_descriptor::data(value::number(static_cast<double>(text.size())),
                                            attr_none);
            out.virtual_slot = true;
            return true;
        }
        std::uint32_t at = 0;
        if (index_key(name, at) && at < text.size()) {
            out = property_descriptor::data(string(std::string{text[at]}), attr_enumerable);
            out.virtual_slot = true;
            return true;
        }
        return false;
    }

    if (target.is_kind(heap_kind::native)) {
        auto * fn = static_cast<native_object *>(target.as_heap());
        if (value * held = fn->find(name)) {
            out = property_descriptor::data(*held, fn->attrs_of(name));
            return true;
        }
        // A built-in function's `name` is { false, false, true } (10.2.5) and
        // lives on the C++ object rather than in the table, so it is
        // synthesised here. `length` is NOT: a native_fn takes a span and its
        // declared arity is not recorded anywhere, so this engine cannot answer
        // for it and says so by leaving the property absent.
        if (name == "name") {
            out = property_descriptor::data(string(fn->name), attr_configurable);
            out.virtual_slot = true;
            return true;
        }
        return false;
    }

    if (target.is_kind(heap_kind::function)) {
        auto * closure = static_cast<closure_object *>(target.as_heap());
        if (value * held = closure->find(name)) {
            out = property_descriptor::data(*held, closure->attrs_of(name));
            return true;
        }
        if (accessor_entry * entry = closure->find_accessor(name)) {
            out.has_get = out.has_set = true;
            out.getter = entry->getter;
            out.setter = entry->setter;
            out.has_enumerable = out.has_configurable = true;
            out.enumerable = (entry->attrs & attr_enumerable) != 0;
            out.configurable = (entry->attrs & attr_configurable) != 0;
            return true;
        }
        if (name == "prototype") {
            const value made = ensure_prototype(target);
            if (made.is_undefined()) { return false; } // an arrow has none
            out = property_descriptor::data(made, attr_writable);
            return true;
        }
        if (closure->proto != nullptr) {
            // 10.2.5 again: both are { false, false, true }.
            if (name == "name") {
                out = property_descriptor::data(string(closure->proto->name), attr_configurable);
                out.virtual_slot = true;
                return true;
            }
            if (name == "length") {
                out = property_descriptor::data(value::number(closure->proto->param_count),
                                                attr_configurable);
                out.virtual_slot = true;
                return true;
            }
        }
        return false;
    }

    return false;
}

bool context::has_own_property(value target, const std::string & name) {
    property_descriptor found;
    return own_property(target, name, found);
}

bool context::is_extensible(value target) {
    if (target.is_object()) { return static_cast<object_object *>(target.as_heap())->extensible; }
    if (target.is_array()) { return static_cast<array_object *>(target.as_heap())->extensible; }
    if (target.is_kind(heap_kind::native)) {
        return static_cast<native_object *>(target.as_heap())->extensible;
    }
    if (target.is_kind(heap_kind::function)) {
        return static_cast<closure_object *>(target.as_heap())->extensible;
    }
    if (target.is_kind(heap_kind::proxy)) {
        return is_extensible(static_cast<proxy_object *>(target.as_heap())->target);
    }
    // A primitive is not extensible, and Object.isExtensible(1) is false rather
    // than an error (19.1.2.13 returns false for a non-object).
    return false;
}

void context::prevent_extensions(value target) {
    if (target.is_object()) {
        static_cast<object_object *>(target.as_heap())->extensible = false;
    } else if (target.is_array()) {
        static_cast<array_object *>(target.as_heap())->extensible = false;
    } else if (target.is_kind(heap_kind::native)) {
        static_cast<native_object *>(target.as_heap())->extensible = false;
    } else if (target.is_kind(heap_kind::function)) {
        static_cast<closure_object *>(target.as_heap())->extensible = false;
    } else if (target.is_kind(heap_kind::proxy)) {
        prevent_extensions(static_cast<proxy_object *>(target.as_heap())->target);
    }
}

// --- [[Delete]] -----------------------------------------------------------
bool context::delete_own_property(value target, const std::string & name) {
    if (target.is_kind(heap_kind::proxy)) {
        return delete_own_property(static_cast<proxy_object *>(target.as_heap())->target, name);
    }
    if (target.is_object()) {
        auto * obj = static_cast<object_object *>(target.as_heap());
        if (accessor_entry * entry = obj->find_accessor(name)) {
            if ((entry->attrs & attr_configurable) == 0) { return false; }
            return obj->erase_accessor(name);
        }
        if (obj->find(name) == nullptr) { return true; } // absent: delete succeeds
        if ((obj->attrs_of(name) & attr_configurable) == 0) { return false; }
        return obj->erase(name);
    }
    if (target.is_kind(heap_kind::native)) {
        auto * fn = static_cast<native_object *>(target.as_heap());
        if (fn->find(name) == nullptr) { return true; }
        if ((fn->attrs_of(name) & attr_configurable) == 0) { return false; }
        return fn->erase(name);
    }
    if (target.is_kind(heap_kind::function)) {
        auto * closure = static_cast<closure_object *>(target.as_heap());
        if (closure->find(name) == nullptr) { return true; }
        if ((closure->attrs_of(name) & attr_configurable) == 0) { return false; }
        return closure->erase(name);
    }
    // AN ARRAY ELEMENT IS NOT DELETED, and never was: `items` is a dense
    // std::vector with no way to spell a hole, so removing one would shift
    // every element after it and `delete a[0]` would change a.length. The
    // answer is true - which is what sloppy `delete` yields anyway, and what
    // `length` (non-configurable, and correctly rejected above by falling
    // through to here... ) - see the note in docs/test262.md.
    return true;
}

// --- [[DefineOwnProperty]] ------------------------------------------------
//
// 10.1.6.3 ValidateAndApplyPropertyDescriptor, which is the whole reason
// `Object.freeze` and `verifyProperty` can mean anything. False is REJECT; the
// caller turns that into a TypeError (Object.defineProperty) or a false
// (Reflect.defineProperty).
bool context::define_own_property(value target, const std::string & name,
                                  const property_descriptor & wanted) {
    if (target.is_kind(heap_kind::proxy)) {
        return define_own_property(static_cast<proxy_object *>(target.as_heap())->target, name,
                                   wanted);
    }

    property_descriptor current;
    const bool exists = own_property(target, name, current);

    if (!exists && !is_extensible(target)) { return false; }

    if (exists && !current.configurable) {
        if (wanted.has_configurable && wanted.configurable) { return false; }
        if (wanted.has_enumerable && wanted.enumerable != current.enumerable) { return false; }
        // A non-configurable property cannot change between data and accessor.
        if (wanted.is_accessor() && !current.is_accessor()) { return false; }
        if (wanted.is_data() && current.is_accessor()) { return false; }
        if (current.is_accessor()) {
            if (wanted.has_get && !descriptor_same_value(wanted.getter, current.getter)) {
                return false;
            }
            if (wanted.has_set && !descriptor_same_value(wanted.setter, current.setter)) {
                return false;
            }
        } else if (!current.writable) {
            if (wanted.has_writable && wanted.writable) { return false; }
            if (wanted.has_value && !descriptor_same_value(wanted.held, current.held)) {
                return false;
            }
        }
    }

    // WHAT THE PROPERTY ENDS UP AS. An absent field means "unchanged" on an
    // existing property and "false" on a new one - which is the difference
    // between `defineProperty(o, 'x', {value: 1})` making a frozen-shaped
    // property (correct) and an ordinary one (what every engine that skips this
    // step produces).
    const bool making_accessor =
        wanted.is_accessor() || (exists && current.is_accessor() && !wanted.is_data());
    const bool enumerable =
        wanted.has_enumerable ? wanted.enumerable : (exists && current.enumerable);
    const bool configurable =
        wanted.has_configurable ? wanted.configurable : (exists && current.configurable);
    const bool writable = wanted.has_writable
                              ? wanted.writable
                              : (exists && !current.is_accessor() && current.writable);

    const auto attrs = static_cast<std::uint8_t>((writable ? attr_writable : 0) |
                                                 (enumerable ? attr_enumerable : 0) |
                                                 (configurable ? attr_configurable : 0));
    const std::uint8_t accessor_attrs = static_cast<std::uint8_t>(
        (enumerable ? attr_enumerable : 0) | (configurable ? attr_configurable : 0));

    if (making_accessor) {
        const value getter =
            wanted.has_get
                ? wanted.getter
                : (exists && current.is_accessor() ? current.getter : value::undefined());
        const value setter =
            wanted.has_set
                ? wanted.setter
                : (exists && current.is_accessor() ? current.setter : value::undefined());
        if (target.is_object()) {
            static_cast<object_object *>(target.as_heap())
                ->define_accessor(name, getter, setter, accessor_attrs);
            return true;
        }
        if (target.is_kind(heap_kind::function)) {
            static_cast<closure_object *>(target.as_heap())
                ->define_accessor(name, getter, setter, accessor_attrs);
            return true;
        }
        // AN ARRAY AND A NATIVE HAVE NOWHERE TO PUT ONE, and answer true.
        //
        // Neither carries an accessor table: an array's elements are a
        // std::vector and a native's statics are a flat list of data
        // properties. Answering FALSE here would turn what has always been a
        // silent no-op into a TypeError - `Object.defineProperty(arr, "0",
        // {get() {...}})` is real test262 code and real library code - so this
        // keeps the previous behaviour and names it. Measured: answering false
        // cost 6 tests that had passed (built-ins/Array/prototype/indexOf,
        // reduce, flatMap and Function/prototype/bind), which is how the gap
        // was found rather than argued about.
        return true;
    }

    const value held = wanted.has_value
                           ? wanted.held
                           : (exists && !current.is_accessor() ? current.held : value::undefined());
    if (target.is_object()) {
        auto * obj = static_cast<object_object *>(target.as_heap());
        obj->erase_accessor(name);
        obj->define(name, held, attrs);
        return true;
    }
    if (target.is_kind(heap_kind::native)) {
        static_cast<native_object *>(target.as_heap())->define(name, held, attrs);
        return true;
    }
    if (target.is_kind(heap_kind::function)) {
        static_cast<closure_object *>(target.as_heap())->define(name, held, attrs);
        return true;
    }
    if (target.is_array()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        if (name == "length") {
            if (!wanted.has_value) { return true; }
            return arr->set_js_length(to_number(held));
        }
        std::uint32_t at = 0;
        if (index_key(name, at)) {
            // THE ATTRIBUTES ARE DROPPED, deliberately: an array's elements
            // live in a std::vector with no room for three bits each (see
            // array_object's integrity note). The VALUE is stored, which is
            // what `Object.defineProperty(a, 0, {value: x})` is nearly always
            // for; a per-element writable/enumerable/configurable is not
            // modelled and this returns true rather than pretending otherwise
            // in either direction.
            if (wanted.has_value) { store_index(target, value::number(at), held); }
            return true;
        }
        // A NAMED PROPERTY ON AN ARRAY IS DROPPED AND ANSWERS TRUE. An array
        // here has no property table at all, so there is nowhere to put one -
        // and answering false would turn `Object.defineProperty(a, 'x', ...)`
        // from the silent no-op it has always been into a TypeError, which is a
        // behaviour change unrelated to attributes. Stated rather than
        // discovered; see docs/test262.md.
        return true;
    }
    return false;
}

value context::lookup_property(value target, const std::string & name) {
    // A PROXY ANSWERS FIRST, or hands the question to its target. This sits at
    // the top because a proxy's whole purpose is to be asked before anything
    // else looks at the object underneath it.
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        const value trap = proxy_trap(target, "get");
        if (trap.is_callable()) {
            const value args[3] = {p->target, string(name), target};
            return call(trap, args, p->handler);
        }
        return lookup_property(p->target, name);
    }
    // Own properties first: a page that writes `arr.length = 0` or shadows a
    // method on one object must not be overridden by the prototype.
    if (target.is_object()) {
        // Own properties, then the object's OWN prototype chain (what a class
        // instance uses to find its methods), then the shared table. A depth
        // cap because a page can make the chain cyclic and a lookup must not
        // hang because of it.
        auto * obj = static_cast<object_object *>(target.as_heap());
        // HASHED ONCE FOR THE WHOLE CHAIN. Every level below is asked for the
        // same name, and each `find` used to hash it again.
        const prehashed_name key{name, hash_name(name)};
        for (int depth = 0; obj != nullptr && depth < 64; ++depth) {
            if (value * found = obj->find(key)) { return *found; }
            // An accessor found anywhere on the chain is CALLED, with the
            // original target as its receiver - a getter defined on a prototype
            // reads the instance, which is the entire point of putting one
            // there. The has_accessors test is why this costs nothing on the
            // objects that have none, which is nearly all of them.
            if (accessor_entry * entry = obj->find_accessor(name)) {
                if (entry->getter.is_callable()) {
                    return call(entry->getter, std::span<const value>{}, target);
                }
                return value::undefined(); // set-only: reading gives undefined
            }
            obj = obj->prototype.is_object()
                      ? static_cast<object_object *>(obj->prototype.as_heap())
                      : nullptr;
        }
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(key)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_array()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        // js_length, NOT length(): an index too far out to materialise raises
        // `length` without allocating for it, and this is the one read that has
        // to see that. Everything else keeps items.size() and its bounds.
        if (name == "length") { return value::number(static_cast<double>(arr->js_length())); }
        // WHAT A VIEW KNOWS ABOUT ITS BUFFER. `new Uint8Array(f32.buffer)` is
        // how a page makes a second view of a different width over storage it
        // already has - Phaser does exactly that - and it needs `buffer` to
        // hand back something the constructor recognises as one.
        if (arr->is_view()) {
            const auto width = bytes_per_element(arr->elements);
            if (name == "byteLength") {
                return value::number(static_cast<double>(arr->view_length * width));
            }
            if (name == "byteOffset") { return value::number(arr->byte_offset); }
            if (name == "BYTES_PER_ELEMENT") { return value::number(static_cast<double>(width)); }
            if (name == "buffer") {
                value made = make_object();
                auto * buffer = static_cast<object_object *>(made.as_heap());
                const auto * bytes = static_cast<const array_object *>(arr->viewed.as_heap());
                buffer->set("byteLength", value::number(static_cast<double>(bytes->items.size())));
                buffer->set("length", value::number(static_cast<double>(bytes->items.size())));
                buffer->set("__bytes", arr->viewed);
                return made;
            }
        }
        if (arr->is_match) { // an exec() result carries index/input/groups
            if (name == "index") { return arr->index; }
            if (name == "input") { return arr->input; }
            if (name == "groups") { return arr->groups; }
        }
        // A TYPED array's own methods first, then every array's, then every
        // object's - which is the chain JavaScript actually has, and the reason
        // `[1,2].hasOwnProperty(...)` and `bytes.subarray(...)` both work.
        if (arr->elements != element_kind::none) {
            if (object_object * table = prototype(proto_kind::typed_array)) {
                if (value * found = table->find(name)) { return *found; }
            }
        }
        if (object_object * table = prototype(proto_kind::array)) {
            if (value * found = table->find(name)) { return *found; }
        }
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_string()) {
        auto * str = static_cast<string_object *>(target.as_heap());
        if (name == "length") { return value::number(static_cast<double>(str->text.size())); }
        if (object_object * table = prototype(proto_kind::string)) {
            if (value * found = table->find(name)) { return *found; }
        }
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    // A BigInt is a primitive with methods, exactly as a number is - and it
    // reaches its prototype the same way, because it has no own properties for
    // a lookup to find first. Without this `(255n).toString(16)` read
    // undefined and called it.
    if (target.is_kind(heap_kind::bigint)) {
        if (object_object * table = prototype(proto_kind::bigint)) {
            if (value * found = table->find(name)) { return *found; }
        }
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_number()) {
        if (object_object * table = prototype(proto_kind::number)) {
            if (value * found = table->find(name)) { return *found; }
        }
        // ...THEN Object.prototype, because `Number.prototype`'s own prototype
        // IS Object.prototype and a primitive boxes on property access. Without
        // this `(5).hasOwnProperty(...)` read undefined - and library code does
        // exactly that on values whose type it has not checked. Phaser's tween
        // manager asks `hasOwnProperty` of a number while working out which
        // properties of a target to animate.
        //
        // NUMBERS, BOOLEANS AND STRINGS ALL LACKED THIS. Only arrays chained to
        // Object.prototype, and the comment there says it is "the chain
        // JavaScript actually has" - which was true of arrays and of nothing
        // else. The regression test asserted the string case on the assumption
        // it already worked, and it did not.
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    // A boolean is a value with methods too. `flag.toString()` is what a
    // template literal and a string concatenation both do underneath, and code
    // that calls it explicitly - to build a cache key, say - found nothing.
    if (target.is_boolean()) {
        if (object_object * table = prototype(proto_kind::boolean)) {
            if (value * found = table->find(name)) { return *found; }
        }
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_kind(heap_kind::native)) {
        auto * fn = static_cast<native_object *>(target.as_heap());
        if (value * found = fn->find(name)) { return *found; }
        // A BUILT-IN FUNCTION HAS A NAME, and it was undefined for every one
        // that is not a constructor - so test262's own assert.throws printed
        // "Expected a undefined to be thrown", 2,670 times, because it builds
        // its message out of `expectedErrorConstructor.name`. It lives on the
        // C++ object rather than in the table, which is why it is answered here
        // rather than installed on 400 natives.
        if (name == "name") { return string(fn->name); }
        // STATIC INHERITANCE through the constructor's own [[Prototype]] - the
        // same walk a closure does. `TypeError.__proto__` is `Error`, so a
        // static installed on Error is found through all six NativeErrors.
        for (value up = fn->proto_link; up.is_object() || up.is_callable();) {
            if (up.is_kind(heap_kind::native)) {
                auto * parent = static_cast<native_object *>(up.as_heap());
                if (value * found = parent->find(name)) { return *found; }
                up = parent->proto_link;
                continue;
            }
            if (up.is_object()) {
                if (value * found = static_cast<object_object *>(up.as_heap())->find(name)) {
                    return *found;
                }
            }
            break;
        }
        // ...then Function.prototype, so `nativeFn.call(...)` works too.
        if (object_object * table = prototype(proto_kind::function)) {
            if (value * found = table->find(name)) { return *found; }
        }
        // ...AND THEN Object.prototype, because Function.prototype's own
        // [[Prototype]] is Object.prototype. Without it `f.hasOwnProperty` and
        // `f.propertyIsEnumerable` were undefined on every function, which is
        // the same gap numbers, booleans and strings had until they were fixed
        // and functions were left out of.
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_kind(heap_kind::symbol)) {
        auto * sym = static_cast<symbol_object *>(target.as_heap());
        if (name == "description") { return string(sym->description); }
        if (object_object * table = prototype(proto_kind::symbol)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_kind(heap_kind::function)) {
        auto * closure = static_cast<closure_object *>(target.as_heap());
        if (value * found = closure->find(name)) { return *found; }
        if (name == "prototype") { return ensure_prototype(target); }
        // `f.name` and `f.length`, read off the compiled function rather than
        // stored on every closure - most functions are never asked and an extra
        // two properties each is a real cost on a 4,754-function bundle.
        //
        // `name` is not cosmetic: identifying a value by
        // `Object.getPrototypeOf(x).constructor.name` is the standard walk that
        // works where instanceof does not, and an undefined name compares equal
        // to the other undefined it is being tested against - so a nameless
        // class reported a MATCH against anything else with no name.
        if (closure->proto != nullptr) {
            if (name == "name") { return string(closure->proto->name); }
            if (name == "length") { return value::number(closure->proto->param_count); }
        }
        // `static get w()` on a class - the constructor IS the closure, so its
        // accessors live here rather than on any object.
        if (accessor_entry * entry = closure->find_accessor(name)) {
            if (entry->getter.is_callable()) {
                return call(entry->getter, std::span<const value>{}, target);
            }
            return value::undefined();
        }
        // STATIC INHERITANCE: `class D extends B` makes D's own [[Prototype]]
        // B, so `D.staticMethod` finds B's. Babel wires this by hand with
        // _setPrototypeOf, and a real `extends` should do the same.
        for (value up = closure->proto_link; up.is_callable();) {
            if (up.is_kind(heap_kind::function)) {
                auto * parent = static_cast<closure_object *>(up.as_heap());
                if (value * found = parent->find(name)) { return *found; }
                up = parent->proto_link;
                continue;
            }
            if (value * found = static_cast<native_object *>(up.as_heap())->find(name)) {
                return *found;
            }
            break;
        }
        // A FUNCTION IS AN OBJECT WITH A PROTOTYPE OF ITS OWN. `call`, `apply`
        // and `bind` live there, and p5.js cannot install a single event
        // listener without bind. Then Object.prototype, which is
        // Function.prototype's own [[Prototype]] - see the native arm above.
        if (object_object * table = prototype(proto_kind::function)) {
            if (value * found = table->find(name)) { return *found; }
        }
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
    }
    return value::undefined();
}

std::size_t context::collect() {
    // COUNTED, because a stress mode that silently stops collecting looks
    // exactly like one that works: every answer is the same either way. A test
    // that forces GC asserts this number, not the answer.
    ++collections_;

    // AND THE FRAME CHAIN CHECKED, under stress only, which is where the master
    // plan puts it: "have the GC validate the whole frame chain on every
    // collection - reachable, terminated, and with plausible slot counts".
    //
    // A compiled frame's slots live in `registers_` and are addressed as
    // `registers_.data() + base`, so a base past the end of that vector is a
    // body about to read and write somewhere else entirely - the kind of
    // corruption that surfaces a long way from its cause.
    //
    // SAID PLAINLY: no test exercises this. Reaching it needs a corrupted
    // frame stack, which nothing can produce from outside the class, and
    // writing a test that reaches in to corrupt it would be testing the
    // corruption rather than the guard. It is here because it costs one
    // comparison per frame in a mode that already collects the whole heap.
    if (gc_stress_) {
        for (const call_frame & f : frames_) {
            if (f.proto == nullptr || f.base > registers_.size()) {
                raise("a call frame's register span is outside the register file");
                break;
            }
        }
    }

    // Precise roots: everything reachable is reachable from exactly these -
    // and "these" is ONE inventory, `each_root` in vm.hpp, which the escape
    // oracle walks too. The whole register file and every frame: a
    // collection has no dead window.
    mark_roots(registers_.size());
    return sweep();
}

void context::mark_roots(std::size_t register_limit) {
    each_root(register_limit, frames_.size(), [this](root_label, value v) { mark(v); });
}

std::size_t context::sweep() {
    std::size_t freed = 0;
    heap_object ** link = &heap_;
    while (*link != nullptr) {
        heap_object * o = *link;
        if (o->marked) {
            o->marked = false; // clear for the next cycle
            link = &o->next;
        } else {
            *link = o->next;
            // THE ESCAPE ORACLE HEARS ABOUT EVERY FREE, so a record can never
            // be read through a stale pointer: the recorder flags it dead and
            // forgets the address before `delete` reuses it.
            if (recorder_ != nullptr) [[unlikely]] { note_freed(o); }
            delete o;
            ++freed;
            --live_objects_;
        }
    }
    return freed;
}

// THE ESCAPE ORACLE'S EXIT. Everything a bounded mark set, cleared again, and
// nothing freed: the oracle observes and does not collect. collect() itself
// treats the mark bit as transient (sweep clears it on every survivor), so
// this leaves the heap exactly as a collection that freed nothing would.
void context::unmark_all() {
    for (heap_object * o = heap_; o != nullptr; o = o->next) { o->marked = false; }
}

void context::sweep_all() {
    while (heap_ != nullptr) {
        heap_object * next = heap_->next;
        if (recorder_ != nullptr) [[unlikely]] { note_freed(heap_); }
        delete heap_;
        heap_ = next;
    }
    live_objects_ = 0;
}

} // namespace ctbrowser::script
