// ctbrowser.script context - property writes: `store_index`, `store_property`
// and the accessor path an assignment can take.
//
// One of six files carved out of a 1,396-line vm/objects.cpp on 2026-09-08 -
// which was itself one of four carved out of a 3,232-line vm.cpp on
// 2026-08-09. All members of `context`, declared in
// include/ctbrowser/script/vm.hpp - so they split across translation units
// with nothing to declare.

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/number_format.hpp>
#include <ctbrowser/script/vm.hpp>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// The VM's implementation.
//
// `run_loop` alone is 15 KB of object code - the whole instruction dispatch -
// and while it lived in the interface every translation unit that imported the
// module emitted its own copy and optimised it again. The class declaration
// stays in :vm; the bodies live here and are compiled once.

namespace ctbrowser::script {

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
        if (accessor_entry * entry = fn->find_accessor(name)) {
            if (entry->setter.is_callable()) {
                const value args[1] = {v};
                (void)call(entry->setter, args, target);
            }
            return;
        }
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

} // namespace ctbrowser::script
