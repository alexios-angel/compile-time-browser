//===- ctbrowser/lib/Script/vm/objects/store.cpp - Property writes --------===//
//
// Property writes on script values: `store_index`, `store_property` and the
// accessor path an assignment can take. These are members of `context`,
// declared in include/ctbrowser/script/vm.hpp, plus two free helpers shared
// with the built-ins (set_for_receiver, array_chain_holds_indices).
//
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

/// ArraySetLength's shrink, defined beside [[DefineOwnProperty]] in
/// descriptors.cpp.
bool array_set_length(array_object & arr, double n);

/// OrdinarySet(O, P, V, Receiver) (ECMA-262 10.1.9.2) for a Receiver that is
/// not O: used when a write reaches a proxy on a prototype chain, when an
/// array's new named property consults its prototypes, and for `super.x = v`.
/// Setters found on the chain run with `receiver` as `this`; a proxy is asked
/// with it; a data property lands on the receiver through its own
/// [[GetOwnProperty]] and [[DefineOwnProperty]]. Returns false when the write
/// is rejected (strict code then throws). Returns true if script threw, so the
/// caller does not raise a second error. Declared in script/vm.hpp.
bool set_for_receiver(context & cx, value o, const std::string & name, value v, value receiver) {
    const std::size_t before = cx.unwinds();
    const auto threw = [&] { return cx.throw_pending() || cx.unwinds() != before; };
    context::property_descriptor own;
    bool found = false;
    value walk = o;
    for (int hops = 0; hops < 4096 && walk.is_object_like() && !found; ++hops) {
        if (walk.is_kind(heap_kind::proxy)) {
            // A proxy on the way is asked with the same receiver.
            auto * p = static_cast<proxy_object *>(walk.as_heap());
            bool failed = false;
            const value trap = cx.proxy_trap(walk, "set", &failed);
            if (failed || threw()) { return true; }
            if (trap.is_callable()) {
                const value args[4] = {p->target, cx.key_value(name), v, receiver};
                const bool answered = context::truthy(cx.call(trap, args, p->handler));
                return answered || threw();
            }
            walk = p->target;
            continue;
        }
        found = cx.own_property(walk, name, own);
        if (threw()) { return true; }
        if (found) { break; }
        const value up = cx.get_prototype(walk);
        if (up.is_null()) {
            // The implicit chain (Object.prototype and the like), which no
            // explicit link names.
            for (object_object * table : cx.implicit_prototypes(walk)) {
                if (table != nullptr && cx.own_property(value::object(table), name, own)) {
                    found = true;
                    break;
                }
            }
            break;
        }
        walk = up;
    }
    if (found && own.is_accessor()) {
        if (!own.setter.is_callable()) { return false; }
        const value args[1] = {v};
        (void)cx.call(own.setter, args, receiver);
        return true;
    }
    if (found && !own.writable) { return false; }
    if (!receiver.is_object_like()) { return false; }
    context::property_descriptor existing;
    const bool exists = cx.own_property(receiver, name, existing);
    if (threw()) { return true; }
    context::property_descriptor wanted;
    wanted.has_value = true;
    wanted.held = v;
    if (exists) {
        if (existing.is_accessor() || !existing.writable) { return false; }
    } else {
        wanted.has_writable = wanted.has_enumerable = wanted.has_configurable = true;
        wanted.writable = wanted.enumerable = wanted.configurable = true;
    }
    return cx.define_own_property(receiver, name, wanted);
}

/// Returns whether anything on an ordinary array's prototype chain could
/// answer an index. A write to an index the array does not have (a hole, or
/// past the end, as every push is) is OrdinarySet's step 2, the prototype's
/// [[Set]]: a setter `Object.defineProperty(Array.prototype, "0", ...)` put
/// there runs, a read-only one refuses, a proxy is asked. Most pages have none,
/// and object_object::indexed records that in one bool per table, so the
/// append fast path stays cheap. The array built-ins' fast paths call this too
/// (declared in builtins/internal.hpp).
bool array_chain_holds_indices(context & cx, const array_object & arr) {
    value up = arr.prototype;
    if (up.is_null()) {
        object_object * table = cx.prototype(context::proto_kind::array);
        if (table == nullptr) { return false; }
        if (table->indexed) { return true; }
        up = table->prototype;
    }
    for (int depth = 0; depth < 64; ++depth) {
        if (up.is_null()) { // The implicit Object.prototype ends the chain.
            object_object * table = cx.prototype(context::proto_kind::object);
            return table != nullptr && table->indexed;
        }
        if (up.is_undefined()) { return false; } // An explicit null.
        // A proxy or array prototype might answer an index, so assume it does.
        if (!up.is_object()) { return up.is_heap(); }
        auto * table = static_cast<object_object *>(up.as_heap());
        if (table->indexed) { return true; }
        up = table->prototype;
    }
    return true;
}

/// Body of the `append` opcode, shared with the AOT helper ct_aot_append.
void context::array_append(value target, value v) {
    if (target.is_array()) { static_cast<array_object *>(target.as_heap())->items.push_back(v); }
}

/// Body of the `set_index` opcode (`target[key] = v`), shared by the
/// interpreter and the AOT helper ct_aot_set_index.
void context::store_index(value target, value key, value v) {
    if (target.is_array() && key.is_number()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        // A Number that is not an index names the property its ToString
        // spells (`a[-1] = v` is a["-1"]); a typed array coerces the value
        // and writes nothing (10.4.5.5, TypedArraySetElement).
        if (!array_index_number(key.as_number())) {
            if (arr->elements != element_kind::none) {
                (void)typed_element_set(*this, *arr, static_cast<std::size_t>(-1), v);
                return;
            }
            store_property(target, to_string(key), v);
            return;
        }
        const auto i = static_cast<std::ptrdiff_t>(key.as_number());
        // A typed array coerces on write and does not grow: `pixels[i] = 300`
        // is 255 in a clamped byte array, and a write past the end is dropped.
        //
        // TypedArraySetElement (10.4.5.16) coerces first (ToNumber, or
        // ToBigInt for a BigInt kind, where a Number is a TypeError), then
        // checks the index against the length as it is after that. Anything
        // but a number goes through typed_element_set for that order, because
        // an object's valueOf can run script; a number, which is what every
        // pixel and matrix write is, stays on this fast path.
        if (arr->elements != element_kind::none) {
            // A BigInt kind accepts no Number at all (ToBigInt(1) is a
            // TypeError), so it always takes the slow path.
            if (!v.is_number() || is_bigint_kind(arr->elements)) {
                (void)typed_element_set(*this, *arr, static_cast<std::size_t>(i), v);
                return;
            }
            if (i < 0 || static_cast<std::size_t>(i) >= arr->length()) { return; }
            if (arr->is_view()) {
                view_set(*arr, static_cast<std::size_t>(i), v.as_number());
                return;
            }
            arr->items[static_cast<std::size_t>(i)] =
                value::number(coerce_element(arr->elements, v.as_number()));
            return;
        }
        if (i >= 0) {
            const auto index = static_cast<std::uint64_t>(i);
            // An index the array does not have asks its chain first, when
            // anything there could answer (array_chain_holds_indices); the
            // receiver is this array, as OrdinarySet passes it on.
            const auto inherited_write = [&] {
                object_object * table = prototype(proto_kind::array);
                const value proto = !arr->prototype.is_null() ? arr->prototype
                                    : table != nullptr        ? value::object(table)
                                                              : value::undefined();
                if (!set_for_receiver(*this, proto, number_to_string(key.as_number()), v, target)) {
                    store_rejected_ = true;
                }
            };
            if (index < arr->items.size()) {
                if ((arr->in_hole_run(index) ||
                     (!arr->element_attrs.empty() &&
                      arr->is_hole(static_cast<std::uint32_t>(index)))) &&
                    array_chain_holds_indices(*this, *arr)) [[unlikely]] {
                    inherited_write();
                    return;
                }
                // A frozen array rejects the write: silently in sloppy mode, a
                // TypeError under "use strict" (strict_store_check raises it
                // from store_rejected_).
                if (!arr->elements_writable) {
                    store_rejected_ = true;
                    return;
                }
                // A hole a length left (array_object::holes_begin) becomes an
                // element, unless the array takes no new properties.
                if (arr->in_hole_run(index)) [[unlikely]] {
                    if (!arr->extensible) {
                        store_rejected_ = true;
                        return;
                    }
                    arr->fill_hole(static_cast<std::uint32_t>(index));
                } else if (!arr->element_attrs.empty()) [[unlikely]] {
                    // An element with attributes of its own, an accessor
                    // element, or a hole (see array_object::element_attrs).
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
            if (index <= array_object::max_index && array_chain_holds_indices(*this, *arr))
                [[unlikely]] {
                inherited_write();
                return;
            }
            // A sealed or frozen array gains no elements, and neither does one
            // whose `length` is not writable (10.4.2.1 step 2.d).
            if (!arr->extensible || (!arr->length_writable && index >= arr->js_length())) {
                store_rejected_ = true;
                return;
            }
            // A write that would add more than dense_limit slots at once goes
            // sparse instead (`a[4294967295] = x` must not allocate 34 GB).
            // Testing the jump size leaves sequential fills, which jump by
            // one, on the dense path. 4294967295 is 2^32-1, the largest
            // uint32 key.
            if (index <= 4294967295ull &&
                index + 1 - arr->items.size() > array_object::dense_limit) {
                arr->set_sparse(static_cast<std::uint32_t>(index), v);
                return;
            }
            // Past 2^32-1 a numeric key is not an index and not a slot either;
            // it is an ordinary property, which an array here cannot hold.
            if (index > 4294967295ull) { return; }
            // What the write jumps over is holes, not undefined elements:
            // `a = []; a[3] = x` has no element 0.
            const std::size_t before = arr->items.size();
            arr->items.resize(static_cast<std::size_t>(index) + 1, value::undefined());
            arr->items[static_cast<std::size_t>(index)] = v;
            arr->add_holes(before, static_cast<std::size_t>(index));
        }
        return;
    }
    // ToObject(base) before ToPropertyKey, as lookup_index (PutValue step 3.a).
    if (target.is_nullish()) [[unlikely]] {
        const bool plain = key.is_string() || key.is_number() || key.is_boolean();
        throw_error("TypeError", "Cannot set properties of " +
                                     std::string{target.is_null() ? "null" : "undefined"} +
                                     (plain ? " (setting '" + to_string(key) + "')" : ""));
        return;
    }
    const std::size_t unwound = unwinds();
    const bool pending = throw_pending();
    const std::string name = to_string(key);
    if ((!pending && throw_pending()) || unwinds() != unwound) { return; }
    store_property(target, name, v);
}

/// Body of `op::pass_new_target`: records the new.target the next frame push
/// (a super() call) should receive. When the current closure carries a
/// captured new.target (its private NewTarget slot), that one wins over `from`.
void context::pass_new_target(value from) {
    if (!frames_.empty() && frames_.back().closure != nullptr) {
        if (const value * held = frames_.back().closure->find(CTBROWSER_PRIVATE_KEY("NewTarget"))) {
            from = *held;
        }
    }
    pending_new_target_ = from;
}

/// `target.name = v`: the property write for every kind of value. A rejected
/// write sets store_rejected_ so strict code can throw.
void context::store_property(value target, const std::string & name, value v) {
    // `null.x = v` is a TypeError (7.3.4 PutValue step 5.a); see the read
    // side in lookup_property.
    if (target.is_nullish()) [[unlikely]] {
        throw_error("TypeError", "Cannot set properties of " +
                                     std::string{target.is_null() ? "null" : "undefined"} +
                                     " (setting '" + name + "')");
        return;
    }
    // A private name is a brand check on the write too (7.3.32 PrivateSet):
    // an object whose class did not add the element throws TypeError, as
    // lookup_property's read does. A private method is not writable at all; a
    // private accessor invokes its setter on the original receiver. A field
    // is defined by the class's initialiser, not written, so it never comes
    // through here before it exists.
    if (is_private_key(name)) [[unlikely]] {
        const std::size_t colon = name.find(':');
        const std::string shown =
            name.substr(1, colon == std::string::npos ? std::string::npos : colon - 1);
        auto found = find_private_element(target, name);
        if (!found) {
            throw_error("TypeError", "Cannot write private member " + shown +
                                         " to an object whose class did not declare it");
            return;
        }
        if (found->is_accessor()) {
            if (!found->setter.is_callable()) {
                throw_error("TypeError", "Private accessor " + shown + " has no setter");
                return;
            }
            const value args[1] = {v};
            (void)call(found->setter, args, target);
            return;
        }
        if (!has_own_property(target, name)) {
            throw_error("TypeError", "Private method " + shown + " is not writable");
            return;
        }
        found->held = v;
        if (!define_own_property(target, name, *found)) {
            throw_error("TypeError", "Cannot write private member " + shown);
        }
        return;
    }
    // A proxy's `set` trap first: it alone can decide the write does not land
    // on the target at all.
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        bool failed = false;
        const value trap = proxy_trap(target, "set", &failed);
        if (failed || throw_pending()) { return; }
        if (trap.is_callable()) {
            const value args[4] = {p->target, key_value(name), v, target};
            // 10.5.9 step 9: a trap answering false is a rejected write,
            // silent here and a TypeError in strict code (strict_store_check).
            // An HTMLCollection's index is the everyday case.
            const std::size_t before = unwinds();
            const bool answered = truthy(call(trap, args, p->handler));
            if (throw_pending() || unwinds() != before) { return; }
            if (!answered) {
                store_rejected_ = true;
                return;
            }
            // Steps 10-12, the invariants: a true answer over a target property
            // that is non-configurable and either non-writable data holding a
            // different value, or an accessor with no setter, is a TypeError.
            property_descriptor held;
            if (own_property(p->target, name, held) && held.has_configurable &&
                !held.configurable) {
                if (held.is_data() && held.has_writable && !held.writable &&
                    !held.held.same_value(v)) {
                    throw_error("TypeError", "'set' on proxy: trap returned truish for property '" +
                                                 name +
                                                 "' which exists in the proxy target as a "
                                                 "non-configurable and non-writable data "
                                                 "property with a different value");
                } else if (held.is_accessor() && !held.setter.is_callable()) {
                    throw_error("TypeError", "'set' on proxy: trap returned truish for property '" +
                                                 name +
                                                 "' which exists in the proxy target as a "
                                                 "non-configurable and non-writable "
                                                 "accessor property without a setter");
                }
            }
            return;
        }
        // No `set` trap: the target's own [[Set]], deliberately with the
        // target as the receiver where 10.5.9 step 6 passes the proxy. The
        // document is a proxy with no `set` trap (shell/bindings/document/
        // named_access.cpp), and Document.prototype's native event-handler
        // setters take `this` to be the document object itself, so handing
        // them the proxy would lose `document.onreadystatechange = f`. A proxy
        // on a chain does pass the receiver (assign_through_accessor).
        store_property(p->target, name, v);
        return;
    }
    if (target.is_object()) {
        // A method being installed on a class that is being defined (see
        // object_object::defining_class) is an own definition, whatever the
        // chain holds.
        if (static_cast<object_object *>(target.as_heap())->defining_class) [[unlikely]] {
            static_cast<object_object *>(target.as_heap())
                ->define_over_accessor(name, v, attr_default);
            return;
        }
        if (assign_through_accessor(target, name, v)) { return; }
        auto * obj = static_cast<object_object *>(target.as_heap());
        // [[Set]] (10.1.9) and the three things it checks. An assignment is
        // not a definition: an own data property that is not writable rejects
        // the write; so does an inherited one (`Object.freeze(proto)` stops a
        // write through every instance); and a new property needs the receiver
        // to be extensible.
        //
        // Each rejection below sets store_rejected_: sloppy code discards the
        // write, and strict code throws the TypeError from strict_store_check
        // in the run loop.
        //
        // The hit path does one hash lookup, not `find` then `set`: this is the
        // hottest write in the engine.
        obj->normalise();
        if (const auto it = obj->index.find(name); it != obj->index.end()) {
            if ((obj->attrs_at(it->second) & attr_writable) == 0) {
                store_rejected_ = true;
                return;
            }
            obj->props[it->second].second = v;
            return;
        }
        for (value up = obj->prototype; up.is_heap();) {
            // A typed array up the chain answers a numeric key's [[Set]] for
            // this receiver (10.4.5.5 step 1.b.ii): an index it does not have
            // is a quiet success that creates nothing; one it has is a
            // writable element, so the receiver gets its own property below.
            if (up.is_array()) {
                auto * arr = static_cast<array_object *>(up.as_heap());
                std::uint32_t at = 0;
                if (arr->elements != element_kind::none && canonical_numeric_key(name) &&
                    !(object_object::array_index_key(name, at) && at < arr->length())) {
                    return;
                }
                break;
            }
            if (!up.is_object()) { break; }
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
        // A String wrapper's `length` and indices are own and non-writable
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
    // Array writes: `a.length = n` resizes the array (growing pads with holes;
    // `a.length = 0` is the common way to empty one), index-like names go to
    // store_index, and other names land in the array's named-property table.
    if (target.is_array()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        // `a["0"] = v` is the element, not a named property - see lookup_property.
        if (std::uint32_t at = 0; object_object::array_index_key(name, at)) {
            store_index(target, value::number(static_cast<double>(at)), v);
            return;
        }
        // A typed array's other canonical numeric keys ("-0", "1.5") name no
        // element: the value is coerced and nothing is written (10.4.5.5).
        if (arr->elements != element_kind::none && canonical_numeric_key(name)) {
            (void)typed_element_set(*this, *arr, static_cast<std::size_t>(-1), v);
            return;
        }
        // A typed array's `length` is %TypedArray%.prototype's getter, so an
        // own one a page defined is a named property like any other, and
        // without one the write finds no setter: a no-op (10.4.5 / OrdinarySet).
        const bool typed_length = name == "length" && arr->elements != element_kind::none;
        if (typed_length && (!arr->named || (arr->named->find(name) == nullptr &&
                                             arr->named->find_accessor(name) == nullptr))) {
            return;
        }
        if (name != "length" || typed_length) {
            // A named property, in the array's own table (see
            // array_object::named). The same checks as an object's: an own
            // accessor's setter, a non-writable own data property, then the
            // chain, and extensibility for a new one.
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
            // A new one asks the chain first (10.1.9.2 step 2): a setter on
            // Array.prototype or a subclass's prototype takes the write, and
            // an inherited read-only property refuses it; otherwise the
            // property is defined on the array.
            if (arr->elements == element_kind::none) {
                object_object * table = prototype(proto_kind::array);
                const value proto = !arr->prototype.is_null() ? arr->prototype
                                    : table != nullptr        ? value::object(table)
                                                              : value::undefined();
                if (proto.is_object_like()) {
                    if (!set_for_receiver(*this, proto, name, v, target)) {
                        store_rejected_ = true;
                    }
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
        // A typed array's length is fixed: it is a view over bytes that were
        // sized once, and resizing it here would leave the view and its buffer
        // disagreeing. The spec makes the write a no-op, not an error.
        if (arr->elements != element_kind::none) { return; }
        // A non-writable length (`Object.defineProperty(a, "length",
        // {writable: false})`, or a freeze) is checked before the coercions
        // (OrdinarySet step 2.a) and again after them (ArraySetLength step 12),
        // since a valueOf may make it non-writable in between.
        if (!arr->length_writable) {
            store_rejected_ = true;
            return;
        }
        // ArraySetLength (10.4.2.4 steps 3-4): ToUint32 then ToNumber, each of
        // which may run valueOf (`a.length = new Number(6)` is 6). A length
        // that is not a uint32 is a RangeError. set_js_length does not
        // materialise huge lengths; see array_object::dense_limit.
        const std::size_t before = unwinds();
        const double first = to_number_value(v);
        if (throw_pending() || unwinds() != before) { return; }
        const double n = to_number_value(v);
        if (throw_pending() || unwinds() != before) { return; }
        if (static_cast<double>(number_to_uint32(first)) != n) {
            throw_error("RangeError", "Invalid array length");
            return;
        }
        if (!arr->length_writable && n != static_cast<double>(arr->js_length())) {
            store_rejected_ = true;
            return;
        }
        // A non-configurable element stops the shrink (descriptors.cpp).
        if (!array_set_length(*arr, n)) { store_rejected_ = true; }
        return;
    }
    // A function's write to a name it does not own: OrdinarySet asks the chain
    // from `up` (10.1.9.2 step 2.a). An inherited setter takes the write and
    // an inherited read-only data property refuses it. Returns true when the
    // chain decided; false means create an own property. Function.prototype's
    // `caller` and `arguments` are %ThrowTypeError% accessors, which is how
    // assigning either on a strict or bound function throws, and
    // `class D extends B` inherits B's static setters.
    const auto inherited_set = [&](value up) {
        for (int depth = 0; depth < 64; ++depth) {
            if (up.is_null()) { // The implicit Object.prototype ends a chain.
                object_object * table = prototype(proto_kind::object);
                up = table != nullptr ? value::object(table) : value::undefined();
                depth = 63;
            }
            if (!up.is_object_like()) { return false; }
            property_descriptor found;
            if (own_property(up, name, found)) {
                if (found.is_accessor()) {
                    if (found.setter.is_callable()) {
                        const value args[1] = {v};
                        (void)call(found.setter, args, target);
                    } else {
                        store_rejected_ = true;
                    }
                    return true;
                }
                if (!found.writable) {
                    store_rejected_ = true;
                    return true;
                }
                return false;
            }
            if (up.is_kind(heap_kind::native)) {
                // A native's null link is Function.prototype, not the end.
                const value link = static_cast<native_object *>(up.as_heap())->proto_link;
                object_object * fn_table = prototype(proto_kind::function);
                up = !link.is_null()       ? link
                     : fn_table != nullptr ? value::object(fn_table)
                                           : value::undefined();
            } else {
                up = get_prototype(up);
            }
        }
        return false;
    };
    if (target.is_kind(heap_kind::native)) {
        auto * fn = static_cast<native_object *>(target.as_heap());
        if (accessor_entry * entry = fn->find_accessor(name)) {
            if (entry->setter.is_callable()) {
                const value args[1] = {v};
                (void)call(entry->setter, args, target);
            } else {
                store_rejected_ = true; // An accessor with no setter refuses.
            }
            return;
        }
        // The same three checks as an object's (see above, store_rejected_
        // and all). `Array.prototype = x` is the common accidental case.
        if (fn->find(name) != nullptr) {
            if ((fn->attrs_of(name) & attr_writable) == 0) {
                store_rejected_ = true;
                return;
            }
            fn->set(name, v);
            return;
        }
        if (name == "name" && !fn->name_erased) {
            // ...and a native's synthesised `name` is non-writable too.
            store_rejected_ = true;
            return;
        }
        object_object * fn_table = prototype(proto_kind::function);
        const value up = !fn->proto_link.is_null() ? fn->proto_link
                         : fn_table != nullptr     ? value::object(fn_table)
                                                   : value::undefined();
        if (inherited_set(up)) { return; }
        if (!fn->extensible) {
            store_rejected_ = true;
            return;
        }
        fn->set(name, v);
        return;
    }
    if (target.is_kind(heap_kind::function)) {
        auto * closure = static_cast<closure_object *>(target.as_heap());
        // A constructor's `prototype` is own from the moment the function
        // exists (10.2.5 MakeConstructor); it is only created lazily, so make
        // it now. Otherwise the chain walk below would find the parent's
        // read-only `prototype` and refuse `F.prototype = x` for a function
        // whose [[Prototype]] is a class or native constructor (Babel's
        // `_inherits`, `class A extends Array`).
        if (name == "prototype" && !method_without_prototype(*closure)) {
            (void)ensure_prototype(target);
        }
        if (accessor_entry * entry = closure->find_accessor(name)) {
            // An own accessor with no setter refuses the write; it is never
            // shadowed by a data property of the same name.
            if (entry->setter.is_callable()) {
                const value args[1] = {v};
                (void)call(entry->setter, args, target);
            } else {
                store_rejected_ = true;
            }
            return;
        }
        if (closure->find(name) != nullptr) {
            if ((closure->attrs_of(name) & attr_writable) == 0) {
                store_rejected_ = true;
                return;
            }
            closure->set(name, v);
            return;
        }
        if (closure->proto != nullptr &&
            (((name == "length" && !closure->length_erased) ||
              (name == "name" && !closure->name_erased)) ||
             // A sloppy function's `caller` and `arguments` read null
             // (lookup_property's closure arm), as every browser's own
             // non-writable pair does, and refuse a write the same way.
             (!closure->proto->is_strict && !closure->proto->is_arrow &&
              !closure->proto->is_generator && (name == "caller" || name == "arguments")))) {
            // The synthesised `length` and `name` (own_property answers them
            // from the compiled function as non-writable, non-enumerable,
            // configurable) refuse the write rather than being shadowed by a
            // new own entry. defineProperty still redefines them.
            store_rejected_ = true;
            return;
        }
        // A static method of a class being defined is defined, not handed
        // to the parent class's setter (closure_object::defining_class).
        if (!closure->defining_class && inherited_set(get_prototype(target))) {
            return; // See inherited_set.
        }
        if (!closure->extensible) {
            store_rejected_ = true;
            return;
        }
        closure->set(name, v);
        return;
    }
    // A write to a primitive (a number, a string) is silently dropped in
    // sloppy code and a TypeError in strict code.
    store_rejected_ = true;
}

/// Handles an assignment to a plain object that a prototype decides: an
/// inherited setter, an inherited accessor with no setter, or a proxy on the
/// chain. Returns true when the write was handled (or rejected), false when
/// the caller should store an own data property.
bool context::assign_through_accessor(value target, const std::string & name, value v) {
    if (!target.is_object()) { return false; }
    auto * obj = static_cast<object_object *>(target.as_heap());
    for (int depth = 0; obj != nullptr && depth < 64; ++depth) {
        // An own data property wins outright: nothing on the prototype gets a
        // say.
        if (obj->find(name) != nullptr) { return false; }
        if (accessor_entry * entry = obj->find_accessor(name)) {
            if (entry->setter.is_callable()) {
                const value args[1] = {v};
                (void)call(entry->setter, args, target);
                return true;
            }
            // Getter with no setter: the write is discarded (a TypeError in
            // strict code). Defining a data property over it would shadow the
            // getter forever.
            store_rejected_ = true;
            return true;
        }
        if (obj->prototype.is_undefined()) { return false; } // An explicit null [[Prototype]].
        // A proxy on the chain decides the write (10.1.9.2 step 2.b,
        // parent.[[Set]](P, V, Receiver)): its `set` trap sees `target` as the
        // receiver, or its target's chain carries on with it.
        if (obj->prototype.is_kind(heap_kind::proxy)) {
            if (!set_for_receiver(*this, obj->prototype, name, v, target)) {
                store_rejected_ = true;
            }
            return true;
        }
        obj = obj->prototype.is_object() ? static_cast<object_object *>(obj->prototype.as_heap())
                                         : nullptr;
    }
    // ...and the implicit Object.prototype the chain fell through to, which is
    // where `o.__proto__ = p` and an object literal's `__proto__: p` land: both
    // are a [[Set]] that B.2.2.1's setter turns into [[SetPrototypeOf]].
    object_object * table = prototype(proto_kind::object);
    if (table == nullptr) { return false; }
    if (table->find(name) != nullptr && (table->attrs_of(name) & attr_writable) == 0) {
        store_rejected_ = true;
        return true;
    }
    if (accessor_entry * entry = table->find_accessor(name)) {
        if (entry->setter.is_callable()) {
            const value args[1] = {v};
            (void)call(entry->setter, args, target);
        } else {
            store_rejected_ = true;
        }
        return true;
    }
    return false;
}

} // namespace ctbrowser::script
