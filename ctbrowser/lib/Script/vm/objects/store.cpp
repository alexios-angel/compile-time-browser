// ctbrowser.script context - property writes: `store_index`, `store_property`
// and the accessor path an assignment can take.
//
// One of six files carved out of a 1,396-line vm/objects.cpp on 2026-09-08 -
// which was itself one of four carved out of a 3,232-line vm.cpp on
// 2026-08-09. All members of `context`, declared in
// include/ctbrowser/script/vm.hpp - so they split across translation units
// with nothing to declare.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

// ArraySetLength's shrink, defined beside [[DefineOwnProperty]] in descriptors.cpp.
bool array_set_length(array_object & arr, double n);

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
                // FROZEN MEANS FROZEN. Silently in sloppy mode - a TypeError
                // under "use strict", which strict_store_check raises off
                // store_rejected_.
                if (!arr->elements_writable) {
                    store_rejected_ = true;
                    return;
                }
                // An element with attributes of its own, an accessor element,
                // or a hole - see array_object::element_attrs.
                if (!arr->element_attrs.empty()) [[unlikely]] {
                    const auto at = static_cast<std::uint32_t>(index);
                    if (const std::uint8_t * e = arr->find_element_attrs(at)) {
                        if ((*e & array_object::elem_accessor) != 0) {
                            accessor_entry * entry =
                                arr->named ? arr->named->find_accessor(std::to_string(at))
                                           : nullptr;
                            if (entry != nullptr && entry->setter.is_callable()) {
                                const value args[1] = {v};
                                (void)call(entry->setter, args, target);
                            } else {
                                store_rejected_ = true;
                            }
                            return;
                        }
                        if ((*e & array_object::elem_hole) != 0) {
                            // A hole is not a property: [[Set]] creates one,
                            // which a non-extensible array refuses.
                            if (!arr->extensible) {
                                store_rejected_ = true;
                                return;
                            }
                            arr->set_element_attrs(at, attr_default);
                        } else if ((*e & attr_writable) == 0) {
                            store_rejected_ = true;
                            return;
                        }
                    }
                }
                arr->items[static_cast<std::size_t>(index)] = v;
                return;
            }
            // A SEALED OR FROZEN ARRAY GAINS NO ELEMENTS, and neither does one
            // whose `length` is not writable (10.4.2.1 step 2.d).
            if (!arr->extensible || (!arr->length_writable && index >= arr->js_length())) {
                store_rejected_ = true;
                return;
            }
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
    // `null.x = v` is a TypeError (7.3.4 PutValue step 5.a) - see the read
    // side in lookup_property.
    if (target.is_nullish()) [[unlikely]] {
        throw_error("TypeError", "Cannot set properties of " +
                                     std::string{target.is_null() ? "null" : "undefined"} +
                                     " (setting '" + name + "')");
        return;
    }
    // A PRIVATE NAME IS A BRAND CHECK ON THE WRITE TOO (7.3.32 PrivateSet):
    // an object whose class did not add the element is the TypeError, as
    // lookup_property's read is - and a private METHOD, which lives on the
    // prototype under its key, is not writable at all; only a private
    // accessor's setter (further down the chain walk) may take the value.
    // A field is DEFINED by the class's initialiser, not written, so it never
    // comes through here before it exists.
    if (is_private_key(name)) [[unlikely]] {
        const std::size_t colon = name.find(':');
        const std::string shown =
            name.substr(1, colon == std::string::npos ? std::string::npos : colon - 1);
        if (!target.is_object_like() || target.is_kind(heap_kind::proxy) ||
            !private_element_present(target, name)) {
            throw_error("TypeError", "Cannot write private member " + shown +
                                         " to an object whose class did not declare it");
            return;
        }
        property_descriptor own;
        if (!own_property(target, name, own)) {
            for (value up = get_prototype(target); up.is_object_like(); up = get_prototype(up)) {
                property_descriptor found;
                if (!own_property(up, name, found)) { continue; }
                if (!found.is_accessor()) {
                    throw_error("TypeError", "Private method " + shown + " is not writable");
                    return;
                }
                break;
            }
        }
    }
    // A proxy's `set` trap first: it is the only thing that can decide the
    // write does not land on the target at all, which is the point of it.
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        const value trap = proxy_trap(target, "set");
        if (trap.is_callable()) {
            const value args[4] = {p->target, string(name), v, target};
            // 10.5.9 step 9: a trap answering false is a REJECTED write -
            // silent here, the TypeError in strict code (strict_store_check).
            // An HTMLCollection's index is the everyday case.
            if (!truthy(call(trap, args, p->handler)) && !throw_pending()) {
                store_rejected_ = true;
            }
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
        // Each of the three `return`s below sets store_rejected_: sloppy code
        // discards the write, and strict code throws the TypeError from
        // strict_store_check in the run loop, off that flag.
        // ONE HASH LOOKUP ON THE HIT PATH, not two: `find` then `set` would
        // hash the name twice, and this is the hottest write in the engine.
        obj->normalise();
        if (const auto it = obj->index.find(name); it != obj->index.end()) {
            if ((obj->attrs_at(it->second) & attr_writable) == 0) {
                store_rejected_ = true;
                return;
            }
            obj->props[it->second].second = v;
            return;
        }
        for (value up = obj->prototype; up.is_object();) {
            auto * parent = static_cast<object_object *>(up.as_heap());
            if (parent->find(name) != nullptr) {
                if ((parent->attrs_of(name) & attr_writable) == 0) {
                    store_rejected_ = true;
                    return;
                }
                break;
            }
            up = parent->prototype;
        }
        if (!obj->extensible) {
            store_rejected_ = true;
            return;
        }
        // A STRING WRAPPER'S `length` AND INDICES are own, non-writable
        // (10.4.3.1): the write is refused, not shadowed.
        if (name == "length" || (!name.empty() && name[0] >= '0' && name[0] <= '9')) {
            property_descriptor slot_owned;
            if (primitive_slot(target) != nullptr && own_property(target, name, slot_owned)) {
                store_rejected_ = true;
                return;
            }
        }
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
        auto * arr = static_cast<array_object *>(target.as_heap());
        // `a["0"] = v` is the element, not a named property - see lookup_property.
        if (std::uint32_t at = 0; object_object::array_index_key(name, at)) {
            store_index(target, value::number(static_cast<double>(at)), v);
            return;
        }
        if (name != "length") {
            // A NAMED PROPERTY, in the array's own table - see
            // array_object::named. The same three checks as an object's: an
            // own accessor's setter, a non-writable own data property, and
            // extensibility for a fresh one. (Inherited setters on
            // Array.prototype are not consulted; nothing defines one.)
            if (arr->named) {
                if (accessor_entry * entry = arr->named->find_accessor(name)) {
                    if (entry->setter.is_callable()) {
                        const value args[1] = {v};
                        (void)call(entry->setter, args, target);
                    }
                    return;
                }
                if (arr->named->find(name) != nullptr) {
                    if ((arr->named->attrs_of(name) & attr_writable) == 0) {
                        store_rejected_ = true;
                        return;
                    }
                    arr->named->set(name, v);
                    return;
                }
            }
            if (!arr->extensible) {
                store_rejected_ = true;
                return;
            }
            arr->named_table().set(name, v);
            return;
        }
        // A TYPED array's length is fixed - it is a view over bytes that were
        // sized once, and resizing it here would leave the view and its buffer
        // disagreeing. The spec makes the write a no-op, not an error.
        if (arr->elements != element_kind::none) { return; }
        // `Object.defineProperty(a, "length", {writable: false})`, or a freeze.
        if (!arr->length_writable) {
            store_rejected_ = true;
            return;
        }
        // A RangeError, WHICH IT USED TO SWALLOW. 10.4.2.4 step 3 makes any
        // length that is not a uint32 a RangeError, and dropping the write
        // instead was leniency bought at the cost of a test that checks for the
        // throw (S15.4.5.2_A3_T3) - and, for a length in range, of a resize
        // that asked for 34 GB. set_js_length records what it will not
        // materialise; see array_object::dense_limit.
        // ToNumber runs a valueOf: `a.length = new Number(6)` is 6 (10.4.2.4).
        const double n = to_number_value(v);
        if (throw_pending()) { return; }
        if (!(n >= 0) || n > array_object::max_length || n != std::trunc(n)) {
            throw_error("RangeError", "Invalid array length");
            return;
        }
        // A non-configurable element stops the shrink (descriptors.cpp).
        if (!array_set_length(*arr, n)) { store_rejected_ = true; }
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
        // The same three checks as an object's - see above, store_rejected_
        // and all. `Array.prototype = x` is the one every page tries by accident.
        if (fn->find(name) != nullptr) {
            if ((fn->attrs_of(name) & attr_writable) == 0) {
                store_rejected_ = true;
                return;
            }
        } else if (!fn->extensible || (name == "name" && !fn->name_erased)) {
            // ...and a native's synthesised `name` is non-writable too.
            store_rejected_ = true;
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
                if ((closure->attrs_of(name) & attr_writable) == 0) {
                    store_rejected_ = true;
                    return;
                }
            } else if (!closure->extensible) {
                store_rejected_ = true;
                return;
            } else if (((name == "length" && !closure->length_erased) ||
                        (name == "name" && !closure->name_erased)) &&
                       closure->proto != nullptr) {
                // The SYNTHESISED `length` and `name` (own_property answers
                // them off the compiled function, { false, false, true }) are
                // not writable: the write is refused, not shadowed by a new
                // own entry. defineProperty still redefines them.
                store_rejected_ = true;
                return;
            }
            closure->set(name, v);
        }
        return;
    }
    // A write to a number, a string or undefined is silently dropped, which is
    // what non-strict JavaScript does - and a TypeError in strict code.
    store_rejected_ = true;
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
            // Getter with no setter: the write is DISCARDED (a TypeError in
            // strict code). Silently defining a data property over it would
            // shadow the getter forever.
            store_rejected_ = true;
            return true;
        }
        if (obj->prototype.is_undefined()) { return false; } // an explicit null [[Prototype]]
        obj = obj->prototype.is_object() ? static_cast<object_object *>(obj->prototype.as_heap())
                                         : nullptr;
    }
    // ...AND THE IMPLICIT Object.prototype the chain fell through to, which is
    // where `o.__proto__ = p` and an object literal's `__proto__: p` land: both
    // are a [[Set]] that B.2.2.1's setter turns into [[SetPrototypeOf]].
    object_object * table = prototype(proto_kind::object);
    if (table == nullptr) { return false; }
    if (accessor_entry * entry = table->find_accessor(name)) {
        if (entry->setter.is_callable()) {
            const value args[1] = {v};
            (void)call(entry->setter, args, target);
        }
        return true;
    }
    return false;
}

} // namespace ctbrowser::script
