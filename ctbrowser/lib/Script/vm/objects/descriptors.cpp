// ctbrowser.script context - property descriptors: [[GetOwnProperty]],
// [[Delete]] and [[DefineOwnProperty]], and extensibility.
//
// One of six files carved out of a 1,396-line vm/objects.cpp on 2026-09-08 -
// which was itself one of four carved out of a 3,232-line vm.cpp on
// 2026-08-09. All members of `context`, declared in
// include/ctbrowser/script/vm.hpp - so they split across translation units
// with nothing to declare.

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <ctbrowser/core/number_format.hpp>
#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

// ArraySetLength's shrink (10.4.2.4 steps 12-19): the elements from the top
// down are deleted one by one and a non-configurable one STOPS it - the
// length lands one above that element and the answer is false, which
// defineProperty turns into a TypeError and a sloppy `a.length = n` into a
// silent refusal. Only an array with element attributes of its own or a seal
// can have one; everything else truncates outright.
bool array_set_length(array_object & arr, double n) {
    const double current = static_cast<double>(arr.js_length());
    if (n < current && !arr.items.empty()) {
        std::uint32_t stop = 0;
        bool blocked = false;
        if (!arr.elements_configurable) {
            // Sealed: nothing at or above n can go, so the length stays.
            const std::size_t top = std::min(arr.items.size(), static_cast<std::size_t>(current));
            if (static_cast<double>(top) > n) {
                stop = static_cast<std::uint32_t>(top - 1);
                blocked = true;
            }
        } else {
            for (auto it = arr.element_attrs.rbegin(); it != arr.element_attrs.rend(); ++it) {
                if (static_cast<double>(it->first) < n) { break; }
                if ((it->second & array_object::elem_hole) == 0 &&
                    (it->second & attr_configurable) == 0) {
                    stop = it->first;
                    blocked = true;
                    break;
                }
            }
        }
        if (blocked) {
            (void)arr.set_js_length(static_cast<double>(stop) + 1);
            return false;
        }
    }
    // Growing makes holes, not elements (steps 17-18 delete; nothing adds).
    return arr.set_js_length_with_holes(n);
}

namespace {

// 10.4.2.1 step 2 for a NEW element at `at`: a length that is not writable
// refuses an index at or past it, and the slot is materialised the way
// store_index would - dense up to array_object::dense_limit, sparse beyond.
[[nodiscard]] bool array_slot_for_define(array_object & arr, std::uint32_t at, bool exists) {
    if (exists) { return true; }
    if (!arr.length_writable && static_cast<std::size_t>(at) >= arr.js_length()) { return false; }
    if (at < arr.items.size()) { return true; }
    if (static_cast<std::size_t>(at) + 1 - arr.items.size() > array_object::dense_limit) {
        return true; // the caller records it sparse
    }
    const std::size_t before = arr.items.size();
    arr.items.resize(static_cast<std::size_t>(at) + 1, value::undefined());
    arr.add_holes(before, at); // what it jumps over is not elements
    return true;
}

// IsCompatiblePropertyDescriptor (10.1.6.2): ValidateAndApplyPropertyDescriptor
// with no object to apply to - may a property described by `current` (null
// when absent) become `wanted`? [[DefineOwnProperty]] asks it of every
// ordinary object, and a proxy's getOwnPropertyDescriptor and defineProperty
// traps are held to the same answer about their target.
[[nodiscard]] bool compatible_descriptor(bool extensible,
                                         const context::property_descriptor & wanted,
                                         const context::property_descriptor * current) {
    if (current == nullptr) { return extensible; }
    if (current->configurable) { return true; }
    if (wanted.has_configurable && wanted.configurable) { return false; }
    if (wanted.has_enumerable && wanted.enumerable != current->enumerable) { return false; }
    // A non-configurable property cannot change between data and accessor.
    if (wanted.is_accessor() && !current->is_accessor()) { return false; }
    if (wanted.is_data() && current->is_accessor()) { return false; }
    if (current->is_accessor()) {
        if (wanted.has_get && !wanted.getter.same_value(current->getter)) { return false; }
        if (wanted.has_set && !wanted.setter.same_value(current->setter)) { return false; }
    } else if (!current->writable) {
        if (wanted.has_writable && wanted.writable) { return false; }
        if (wanted.has_value && !wanted.held.same_value(current->held)) { return false; }
    }
    return true;
}

} // namespace

value context::from_property_descriptor(const property_descriptor & from) {
    value made = make_object();
    auto * out = static_cast<object_object *>(made.as_heap());
    if (from.has_value) { out->set("value", from.held); }
    if (from.has_writable) { out->set("writable", value::boolean(from.writable)); }
    if (from.has_get) { out->set("get", from.getter); }
    if (from.has_set) { out->set("set", from.setter); }
    if (from.has_enumerable) { out->set("enumerable", value::boolean(from.enumerable)); }
    if (from.has_configurable) { out->set("configurable", value::boolean(from.configurable)); }
    return made;
}

context::property_descriptor context::to_property_descriptor(value from) {
    property_descriptor out;
    const auto field = [&](const char * name, bool & has, value & into) {
        if (has_property(from, string(name))) {
            has = true;
            into = lookup_property(from, name);
        }
    };
    const auto flag = [&](const char * name, bool & has, bool & into) {
        value held = value::undefined();
        bool present = false;
        field(name, present, held);
        if (present) {
            has = true;
            into = truthy(held);
        }
    };
    // 6.2.6.5's ORDER, which a getter or a proxy on the descriptor observes.
    flag("enumerable", out.has_enumerable, out.enumerable);
    flag("configurable", out.has_configurable, out.configurable);
    field("value", out.has_value, out.held);
    flag("writable", out.has_writable, out.writable);
    field("get", out.has_get, out.getter);
    field("set", out.has_set, out.setter);
    return out;
}

void context::delete_named(value target, const std::string & name) {
    // TODO(strict): a false answer here is a TypeError under "use strict",
    // and strict `delete` does not throw yet - the answer is discarded and
    // the compiler emits a constant `true` (see the note on
    // delete_own_property).
    (void)delete_own_property(target, name);
}

void context::delete_index(value target, value key) {
    (void)delete_own_property(target, to_string(key));
}

// --- [[GetOwnProperty]] ---------------------------------------------------
//
// FOUR TABLES AND A HANDFUL OF SYNTHESISED SLOTS, behind one answer. Before
// this, `Object.getOwnPropertyDescriptor` handled object_object and nothing
// else, so it answered undefined for `Array.prototype.indexOf.name`, for
// `[1,2].length` and for every static on a built-in constructor - which is the
// first thing test262's verifyProperty asks about any of them.
bool context::own_property(value target, const std::string & name, property_descriptor & out) {
    out = property_descriptor{};
    if (is_private_key(name) && target.is_kind(heap_kind::proxy)) [[unlikely]] {
        const value elements = static_cast<proxy_object *>(target.as_heap())->private_elements;
        return elements.is_object() && own_property(elements, name, out);
    }

    // 10.5.5 [[GetOwnProperty]] of a proxy: the `getOwnPropertyDescriptor`
    // trap, or the target when there is none. The answer is ToPropertyDescriptor
    // then CompletePropertyDescriptor (steps 12-13), so a trap that says
    // `{value: v}` describes a non-writable, non-enumerable, non-configurable
    // property exactly as Object.defineProperty would read it - and then it is
    // held to the TARGET (steps 9-11, 14-16): a trap may not hide a
    // non-configurable property or any property of a non-extensible target,
    // report one the target could not have, or call a property
    // non-configurable (or non-writable and non-configurable) that is not so.
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        bool failed = false;
        const value trap = proxy_trap(target, "getOwnPropertyDescriptor", &failed);
        if (failed || throw_pending()) { return false; }
        if (!trap.is_callable()) { return own_property(p->target, name, out); }
        const value args[2] = {p->target, key_value(name)};
        const std::size_t before = unwinds();
        const value answer = call(trap, args, p->handler);
        if (throw_pending() || unwinds() != before) { return false; }
        const auto refuse = [&](const char * why) {
            throw_error("TypeError", "'getOwnPropertyDescriptor' on proxy: trap " +
                                         std::string{why} + " for property '" + name + "'");
            out = property_descriptor{};
            return false;
        };
        if (!answer.is_undefined() && !answer.is_object_like()) {
            return refuse("returned neither object nor undefined");
        }
        const rooted keep{*this, answer};
        property_descriptor held;
        const bool target_has = own_property(p->target, name, held);
        if (throw_pending() || unwinds() != before) { return false; }
        if (answer.is_undefined()) {
            if (!target_has) { return false; }
            if (!held.configurable) {
                return refuse("reported non-existent a property that is non-configurable on "
                              "the proxy target");
            }
            const bool extensible = is_extensible(p->target);
            if (throw_pending() || unwinds() != before) { return false; }
            if (!extensible) {
                return refuse("reported non-existent a property of a non-extensible target");
            }
            return false;
        }
        const bool extensible = is_extensible(p->target);
        if (throw_pending() || unwinds() != before) { return false; }
        out = to_property_descriptor(answer);
        if (throw_pending() || unwinds() != before) { return false; }
        // ToPropertyDescriptor's own refusals (6.2.6.5 steps 7.b, 8.b, 10).
        if ((out.has_get && !out.getter.is_undefined() && !out.getter.is_callable()) ||
            (out.has_set && !out.setter.is_undefined() && !out.setter.is_callable()) ||
            (out.is_accessor() && out.is_data())) {
            return refuse("returned an invalid property descriptor");
        }
        if (out.is_accessor()) {
            out.has_get = out.has_set = true;
        } else {
            out.has_value = out.has_writable = true;
        }
        out.has_enumerable = out.has_configurable = true;
        if (!compatible_descriptor(extensible, out, target_has ? &held : nullptr)) {
            return refuse("returned a descriptor incompatible with the proxy target's");
        }
        if (!out.configurable) {
            if (!target_has || held.configurable) {
                return refuse("reported non-configurability for a property that is "
                              "configurable or absent on the proxy target");
            }
            if (out.has_writable && !out.writable && held.writable) {
                return refuse("reported a non-configurable, non-writable property that is "
                              "writable on the proxy target");
            }
        }
        return true;
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
            // A MODULE NAMESPACE'S EXPORT (10.4.6.5): { [[Value]]: the
            // binding's, writable, enumerable, !configurable } - a data
            // property whose storage here is a getter over the cell, so the
            // value is read through it (a native: no page code runs).
            if (entry->getter.is_callable() && obj->find(module_namespace_key) != nullptr) {
                const value getter = entry->getter;
                out = property_descriptor::data(call(getter, {}, target),
                                                attr_writable | attr_enumerable);
                out.virtual_slot = true;
                return true;
            }
            out = property_descriptor::accessor(entry->getter, entry->setter, entry->attrs);
            return true;
        }
        // A STRING WRAPPER'S OWN `length` AND INDICES (10.4.3.1), the same two
        // answers the String primitive arm below gives.
        if (name == "length" || (!name.empty() && name[0] >= '0' && name[0] <= '9')) {
            if (value * slot = primitive_slot(target); slot != nullptr && slot->is_string()) {
                return own_property(*slot, name, out);
            }
        }
        return false;
    }

    if (target.is_array()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        // A TYPED ARRAY HAS NO OWN `length` (23.2.3.19 is a getter on
        // %TypedArray%.prototype), so one a page defines lands in `named`
        // like any other name: the three arms below say the same.
        if (name == "length" && arr->elements == element_kind::none) {
            // 10.4.2: { [[Writable]]: true, [[Enumerable]]: false,
            // [[Configurable]]: false }. A freeze, or defineProperty with
            // writable: false, clears the writable bit.
            out = property_descriptor::data(value::number(static_cast<double>(arr->js_length())),
                                            arr->length_writable ? attr_writable : attr_none);
            out.virtual_slot = true;
            return true;
        }
        std::uint32_t at = 0;
        if (arr->elements != element_kind::none && !object_object::array_index_key(name, at) &&
            canonical_numeric_key(name)) {
            return false;
        }
        if (object_object::array_index_key(name, at)) {
            if (arr->is_view()) {
                if (at < arr->length()) {
                    // 10.4.5.1: { [[Writable]]: true, [[Enumerable]]: true,
                    // [[Configurable]]: true } around the element - a bigint
                    // for a BigInt kind, which is why the read is not
                    // view_get's double.
                    out = property_descriptor::data(is_bigint_kind(arr->elements)
                                                        ? typed_element_get(*this, *arr, at)
                                                        : value::number(view_get(*arr, at)),
                                                    attr_default);
                    out.virtual_slot = true;
                    return true;
                }
                return false;
            }
            // The element's own attributes (array_object::element_attrs),
            // masked by the integrity bools; a hole is not a property and an
            // accessor element's pair lives in `named`.
            const std::uint8_t a = arr->element_attrs_at(at);
            if ((a & array_object::elem_hole) != 0) { return false; }
            if ((a & array_object::elem_accessor) != 0 && arr->named) {
                if (accessor_entry * entry = arr->named->find_accessor(name)) {
                    out = property_descriptor::accessor(entry->getter, entry->setter, a);
                    return true;
                }
            }
            const auto bits = static_cast<std::uint8_t>(a & attr_default);
            if (at < arr->items.size()) {
                out = property_descriptor::data(arr->items[at], bits);
                out.virtual_slot = true;
                return true;
            }
            if (value * found = arr->find_sparse(at)) {
                out = property_descriptor::data(*found, bits);
                out.virtual_slot = true;
                return true;
            }
        } else if (arr->named) {
            // A named own property lives in the array's own table, which is
            // an object_object - so the object arm answers for it.
            return own_property(value::object(arr->named.get()), name, out);
        }
        return false;
    }

    if (target.is_string()) {
        const auto & str = *static_cast<const string_object *>(target.as_heap());
        if (name == "length") {
            // 10.4.3.5: a String exotic object's length is { false, false, false }.
            out = property_descriptor::data(value::number(static_cast<double>(str.length())),
                                            attr_none);
            out.virtual_slot = true;
            return true;
        }
        std::uint32_t at = 0;
        if (object_object::array_index_key(name, at) && at < str.length()) {
            out = property_descriptor::data(string(str.unit_text(at)), attr_enumerable);
            out.virtual_slot = true;
            return true;
        }
        return false;
    }

    if (target.is_kind(heap_kind::native)) {
        auto * fn = static_cast<native_object *>(target.as_heap());
        if (accessor_entry * entry = fn->find_accessor(name)) {
            out = property_descriptor::accessor(entry->getter, entry->setter, entry->attrs);
            return true;
        }
        if (value * held = fn->find(name)) {
            out = property_descriptor::data(*held, fn->attrs_of(name));
            return true;
        }
        // A built-in function's `name` is { false, false, true } (10.2.5) and
        // lives on the C++ object rather than in the table, so it is
        // synthesised here. `length` is NOT: a native_fn takes a span and its
        // declared arity is not recorded anywhere, so this engine cannot answer
        // for it and says so by leaving the property absent.
        //
        // ...AND NOT AFTER A `delete`. The synthesised slot has no memory, so
        // deleting a native's own `name` uncovered it again and
        // `hasOwnProperty("name")` stayed true - which is exactly the question
        // test262's `verifyProperty` asks to decide the descriptor is
        // configurable, so every `name.js` in the suite failed on a property
        // that had just been removed.
        if (name == "name" && !fn->name_erased) {
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
            out = property_descriptor::accessor(entry->getter, entry->setter, entry->attrs);
            return true;
        }
        if (name == "prototype") {
            if (method_without_prototype(*closure)) { return false; }
            const value made = ensure_prototype(target);
            if (made.is_undefined()) { return false; } // an arrow has none
            out = property_descriptor::data(made, attr_writable);
            return true;
        }
        if (closure->proto != nullptr) {
            // 10.2.5 again: both are { false, false, true } - until deleted.
            if (name == "name" && !closure->name_erased) {
                out = property_descriptor::data(string(closure->proto->display_name()),
                                                attr_configurable);
                out.virtual_slot = true;
                return true;
            }
            if (name == "length" && !closure->length_erased) {
                out = property_descriptor::data(value::number(closure->proto->length),
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
    // 10.5.3 [[IsExtensible]] of a proxy: the `isExtensible` trap, whose
    // answer must be the target's own (step 9) - a proxy cannot lie about
    // extensibility, because every other invariant is judged against it.
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        bool failed = false;
        const value trap = proxy_trap(target, "isExtensible", &failed);
        if (failed || throw_pending()) { return false; }
        if (!trap.is_callable()) { return is_extensible(p->target); }
        const value args[1] = {p->target};
        const std::size_t before = unwinds();
        const bool answered = truthy(call(trap, args, p->handler));
        if (throw_pending() || unwinds() != before) { return false; }
        const bool actual = is_extensible(p->target);
        if (throw_pending() || unwinds() != before) { return false; }
        if (answered != actual) {
            throw_error("TypeError", "'isExtensible' on proxy: trap result does not reflect "
                                     "extensibility of proxy target");
            return false;
        }
        return answered;
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
    // 10.5.10: the `deleteProperty` trap, or the target. `delete el.dataset.x`
    // is the one a page reaches for - the DOMStringMap's handler removes the
    // attribute, which no delete on its target could do.
    if (target.is_kind(heap_kind::proxy)) {
        // A THROW ANSWERS TRUE, as in define_own_property below: a strict
        // `delete` turns false into a TypeError of its own, and one throw -
        // a revoked handler, a trap that is not callable, a trap that threw -
        // must not become two over the handler the first one consumed.
        auto * p = static_cast<proxy_object *>(target.as_heap());
        bool failed = false;
        const value trap = proxy_trap(target, "deleteProperty", &failed);
        if (failed || throw_pending()) { return true; }
        if (!trap.is_callable()) { return delete_own_property(p->target, name); }
        const value args[2] = {p->target, key_value(name)};
        const std::size_t before = unwinds();
        const bool answered = truthy(call(trap, args, p->handler));
        if (throw_pending() || unwinds() != before) { return true; }
        if (!answered) { return false; }
        // Steps 9-13: `true` may not claim a non-configurable property went,
        // nor that any property of a non-extensible target did.
        property_descriptor held;
        const bool target_has = own_property(p->target, name, held);
        if (throw_pending() || unwinds() != before) { return true; }
        if (!target_has) { return true; }
        const bool extensible = held.configurable && is_extensible(p->target);
        if (throw_pending() || unwinds() != before) { return true; }
        if (!extensible) {
            throw_error("TypeError", "'deleteProperty' on proxy: trap returned truish for "
                                     "property '" +
                                         name +
                                         "' which is non-configurable in the proxy target, "
                                         "or the target is non-extensible");
        }
        return true;
    }
    if (target.is_object()) {
        auto * obj = static_cast<object_object *>(target.as_heap());
        if (accessor_entry * entry = obj->find_accessor(name)) {
            if ((entry->attrs & attr_configurable) == 0) { return false; }
            return obj->erase_accessor(name);
        }
        if (obj->find(name) == nullptr) {
            // absent: delete succeeds - unless it is a String wrapper's own
            // `length` or index, which are non-configurable (10.4.3.1).
            property_descriptor slot_owned;
            return !own_property(target, name, slot_owned);
        }
        if ((obj->attrs_of(name) & attr_configurable) == 0) { return false; }
        return obj->erase(name);
    }
    if (target.is_kind(heap_kind::native)) {
        auto * fn = static_cast<native_object *>(target.as_heap());
        if (accessor_entry * entry = fn->find_accessor(name)) {
            if ((entry->attrs & attr_configurable) == 0) { return false; }
            return fn->erase(name);
        }
        if (fn->find(name) == nullptr) {
            // The synthesised `name` (see own_property) is configurable, and
            // deleting it has to be remembered.
            if (name == "name") { fn->name_erased = true; }
            return true;
        }
        if ((fn->attrs_of(name) & attr_configurable) == 0) { return false; }
        return fn->erase(name);
    }
    if (target.is_kind(heap_kind::function)) {
        auto * closure = static_cast<closure_object *>(target.as_heap());
        // AN ACCESSOR IS A PROPERTY TOO - a class's `static get x()` - and
        // deleting a configurable one removes it, as the object arm does.
        // It answered true and stayed.
        if (accessor_entry * entry = closure->find_accessor(name)) {
            if ((entry->attrs & attr_configurable) == 0) { return false; }
            return closure->accessors.erase(name);
        }
        if (closure->find(name) == nullptr) {
            // The synthesised `name` and `length` - see closure_object::name_erased.
            if (name == "name") { closure->name_erased = true; }
            if (name == "length") { closure->length_erased = true; }
            return true;
        }
        if ((closure->attrs_of(name) & attr_configurable) == 0) { return false; }
        // A REDEFINED `length` or `name` lives in the table, and deleting it
        // must not uncover the synthesised one underneath.
        if (name == "name") { closure->name_erased = true; }
        if (name == "length") { closure->length_erased = true; }
        return closure->erase(name);
    }
    if (target.is_array()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        if (name == "length" && arr->elements == element_kind::none) {
            return false; // non-configurable, 10.4.2
        }
        std::uint32_t at = 0;
        const bool typed = arr->elements != element_kind::none;
        // 10.4.5.6: a typed array's numeric key is deleted exactly when it
        // names no element - `true` for "-0" or an index past the end, `false`
        // for a live element - and never reaches the named table.
        if (!object_object::array_index_key(name, at)) {
            if (typed && canonical_numeric_key(name)) { return true; }
            return arr->named ? delete_own_property(value::object(arr->named.get()), name) : true;
        }
        if (typed) { return at >= arr->length(); }
        // AN ELEMENT LEAVES A HOLE (10.4.2.1 via 10.1.10): `items` cannot shift
        // - `delete a[0]` must not change a.length - so the slot stays as a
        // placeholder and array_object::element_attrs records that it is not
        // a property any more. A non-configurable element (a sealed array,
        // or one defineProperty pinned) refuses.
        if (at < arr->items.size()) {
            const std::uint8_t a = arr->element_attrs_at(at);
            if ((a & array_object::elem_hole) != 0) { return true; }
            if ((a & attr_configurable) == 0) { return false; }
            if ((a & array_object::elem_accessor) != 0 && arr->named) {
                (void)arr->named->erase_accessor(name);
            }
            arr->items[at] = value::undefined();
            arr->set_element_attrs(at, array_object::elem_hole);
            return true;
        }
        if (arr->find_sparse(at) != nullptr) {
            if (!arr->elements_configurable) { return false; }
            std::erase_if(arr->sparse, [at](const std::pair<std::uint32_t, value> & e) {
                return e.first == at;
            });
        }
        return true;
    }
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
    if (is_private_key(name) && target.is_kind(heap_kind::proxy)) [[unlikely]] {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        if (!p->private_elements.is_object()) {
            p->private_elements = value::object(allocate<object_object>());
        }
        return define_own_property(p->private_elements, name, wanted);
    }
    // 10.4.6.6 [[DefineOwnProperty]] OF A MODULE NAMESPACE, for a string key:
    // nothing is ever defined - a description the export already satisfies
    // answers true, any other false. A symbol key (@@toStringTag) is an
    // ordinary property's.
    if (target.is_object() && !name.starts_with(symbol_key_mark) &&
        static_cast<object_object *>(target.as_heap())->find(module_namespace_key) != nullptr) {
        property_descriptor current;
        if (!own_property(target, name, current) || !current.is_data()) { return false; }
        if ((wanted.has_configurable && wanted.configurable) ||
            (wanted.has_enumerable && !wanted.enumerable) || wanted.is_accessor() ||
            (wanted.has_writable && !wanted.writable)) {
            return false;
        }
        return !wanted.has_value || wanted.held.same_value(current.held);
    }
    // 10.5.6: the `defineProperty` trap sees the descriptor as an OBJECT
    // (FromPropertyDescriptor, step 8), or the target defines it.
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        bool failed = false;
        const value trap = proxy_trap(target, "defineProperty", &failed);
        // A revoked handler or a trap that is not callable has thrown: true,
        // as for a trap that throws below.
        if (failed || throw_pending()) { return true; }
        if (!trap.is_callable()) { return define_own_property(p->target, name, wanted); }
        const value args[3] = {p->target, key_value(name), from_property_descriptor(wanted)};
        const std::size_t before = unwinds();
        const bool answered = truthy(call(trap, args, p->handler));
        // A throw answers TRUE: the caller turns false into a TypeError of its
        // own, and one throw must not become two (see ArraySetLength below).
        if (throw_pending() || unwinds() != before) { return true; }
        if (!answered) { return false; }
        // Steps 10-16: `true` must describe something the target could hold.
        property_descriptor held;
        const bool target_has = own_property(p->target, name, held);
        if (throw_pending() || unwinds() != before) { return true; }
        const bool extensible = is_extensible(p->target);
        if (throw_pending() || unwinds() != before) { return true; }
        const bool setting_non_configurable = wanted.has_configurable && !wanted.configurable;
        const char * why = nullptr;
        if (!target_has) {
            if (!extensible) {
                why = "which cannot be added to the non-extensible proxy target";
            } else if (setting_non_configurable) {
                why = "which is either non-existent or configurable in the proxy target";
            }
        } else if (!compatible_descriptor(extensible, wanted, &held)) {
            why = "whose descriptor is incompatible with the proxy target's";
        } else if (setting_non_configurable && held.configurable) {
            why = "which is either non-existent or configurable in the proxy target";
        } else if (held.is_data() && !held.configurable && held.writable && wanted.has_writable &&
                   !wanted.writable) {
            why = "which cannot be non-writable, unless there exists a corresponding "
                  "non-configurable, non-writable own property of the target object";
        }
        if (why != nullptr) {
            throw_error("TypeError", "'defineProperty' on proxy: trap returned truish for "
                                     "defining property '" +
                                         name + "' " + why);
        }
        return true;
    }

    // 10.4.2.4 ArraySetLength, when the descriptor carries a value: IN ITS
    // ORDER, which is observable. ToUint32 and then ToNumber of the value -
    // two coercions, each of which may run a valueOf that redefines `length`
    // - and the RangeError, all BEFORE any check against the property; only
    // then is `length` as it stands NOW asked whether it may change. A throw
    // answers true: the caller's own answer to false is a TypeError.
    if (target.is_array() && name == "length" && wanted.has_value &&
        static_cast<array_object *>(target.as_heap())->elements == element_kind::none) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        const std::size_t before = unwinds();
        const rooted keep{*this, wanted.held};
        const double first = to_number_value(wanted.held);
        if (throw_pending() || unwinds() != before) { return true; }
        const auto new_len = static_cast<double>(ctbrowser::number_to_uint32(first));
        const double number_len = to_number_value(wanted.held);
        if (throw_pending() || unwinds() != before) { return true; }
        if (new_len != number_len) {
            throw_error("RangeError", "Invalid array length");
            return true;
        }
        // `length` is { configurable: false, enumerable: false } and data.
        if ((wanted.has_configurable && wanted.configurable) ||
            (wanted.has_enumerable && wanted.enumerable) || wanted.is_accessor()) {
            return false;
        }
        const auto old_len = static_cast<double>(arr->js_length());
        if (!arr->length_writable &&
            (new_len != old_len || (wanted.has_writable && wanted.writable))) {
            return false;
        }
        const bool ok = new_len == old_len || array_set_length(*arr, new_len);
        if (wanted.has_writable && !wanted.writable) { arr->length_writable = false; }
        return ok;
    }

    // 10.4.5.3: A TYPED ARRAY'S INTEGER INDEX is its own algorithm, before
    // ValidateAndApplyPropertyDescriptor. The index must be valid now, the
    // descriptor may not make the element non-configurable, non-enumerable,
    // an accessor or read-only, and a value goes through the kind's coercion
    // (TypedArraySetElement) - which may throw, and answers true past it.
    if (target.is_array()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        std::uint32_t at = 0;
        if (arr->elements != element_kind::none && object_object::array_index_key(name, at)) {
            if (at >= arr->length()) { return false; }
            if ((wanted.has_configurable && !wanted.configurable) ||
                (wanted.has_enumerable && !wanted.enumerable) || wanted.is_accessor() ||
                (wanted.has_writable && !wanted.writable)) {
                return false;
            }
            if (wanted.has_value) { (void)typed_element_set(*this, *arr, at, wanted.held); }
            return true;
        }
        if (arr->elements != element_kind::none && canonical_numeric_key(name)) { return false; }
    }

    property_descriptor current;
    const bool exists = own_property(target, name, current);

    if (!compatible_descriptor(exists || is_extensible(target), wanted,
                               exists ? &current : nullptr)) {
        return false;
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
        // `getter` and `setter` are the RESULT - the wanted half or the current
        // one - so an existing accessor takes both as they are: a table's
        // define_accessor keeps a half passed as undefined (an object
        // literal's `get x` and `set x` arrive one at a time), which would
        // make `defineProperty(o, "x", {set: undefined})` a no-op.
        const auto replace = [&](auto * table) {
            if (accessor_entry * entry = table->find_accessor(name)) {
                entry->getter = getter;
                entry->setter = setter;
                entry->attrs = accessor_attrs;
            } else {
                table->define_accessor(name, getter, setter, accessor_attrs);
            }
            return true;
        };
        if (target.is_object()) { return replace(static_cast<object_object *>(target.as_heap())); }
        if (target.is_kind(heap_kind::function)) {
            return replace(static_cast<closure_object *>(target.as_heap()));
        }
        if (target.is_kind(heap_kind::native)) {
            return replace(static_cast<native_object *>(target.as_heap()));
        }
        // AN ARRAY'S ELEMENTS HAVE NOWHERE TO PUT ONE, and answer true.
        //
        // An array's elements are a std::vector. Answering FALSE here would
        // turn what has always been a
        // silent no-op into a TypeError - `Object.defineProperty(arr, "0",
        // {get() {...}})` is real test262 code and real library code - so this
        // keeps the previous behaviour and names it. Measured: answering false
        // cost 6 tests that had passed (built-ins/Array/prototype/indexOf,
        // reduce, flatMap and Function/prototype/bind), which is how the gap
        // was found rather than argued about. A NAMED accessor goes into the
        // array's own table (array_object::named).
        if (target.is_array()) {
            auto * arr = static_cast<array_object *>(target.as_heap());
            std::uint32_t at = 0;
            if (name == "length" && arr->elements == element_kind::none) { return false; }
            if (!object_object::array_index_key(name, at)) {
                arr->named_table().define_accessor(name, getter, setter, accessor_attrs);
                return true;
            }
            // AN ACCESSOR ELEMENT: the pair goes in `named` under the index's
            // canonical string, the `items` slot is a placeholder, and
            // element_attrs says which it is - see array_object.
            if (!array_slot_for_define(*arr, at, exists)) { return false; }
            if (at >= arr->items.size()) { return true; } // recorded sparse; attributes dropped
            arr->items[at] = value::undefined();
            arr->named_table().define_accessor(name, getter, setter, accessor_attrs);
            arr->set_element_attrs(
                at, static_cast<std::uint8_t>(accessor_attrs | array_object::elem_accessor));
            return true;
        }
        return true;
    }

    const value held = wanted.has_value
                           ? wanted.held
                           : (exists && !current.is_accessor() ? current.held : value::undefined());
    if (target.is_object()) {
        static_cast<object_object *>(target.as_heap())->define_over_accessor(name, held, attrs);
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
        if (name == "length" && arr->elements == element_kind::none) {
            // ArraySetLength, 10.4.2.4. The value goes through ToNumber (a
            // valueOf runs) and must be a uint32 - `{value: undefined}` is NaN
            // against 0 and a RangeError, not a refusal. The RangeError is
            // thrown HERE and `true` answered, because the caller's own answer
            // to `false` is a TypeError and one throw must not become two.
            if (wanted.has_value) {
                const double n = to_number_value(held);
                if (throw_pending()) { return true; }
                if (!(n >= 0) || n > array_object::max_length || n != std::trunc(n)) {
                    throw_error("RangeError", "Invalid array length");
                    return true;
                }
                if (!arr->length_writable && n != static_cast<double>(arr->js_length())) {
                    return false;
                }
                if (!array_set_length(*arr, n)) { return false; }
            }
            if (wanted.has_writable && !wanted.writable) { arr->length_writable = false; }
            return true;
        }
        std::uint32_t at = 0;
        if (object_object::array_index_key(name, at)) {
            if (!array_slot_for_define(*arr, at, exists)) { return false; }
            if (at >= arr->items.size()) {
                // Recorded sparse (array_object::dense_limit); attributes dropped.
                if (wanted.has_value) { arr->set_sparse(at, held); }
                return true;
            }
            if (exists && current.is_accessor() && arr->named) {
                (void)arr->named->erase_accessor(name);
            }
            arr->items[at] = held;
            arr->set_element_attrs(at, attrs);
            return true;
        }
        // A NAMED PROPERTY goes into the array's own table (see
        // array_object::named), through the object arm so every attribute
        // rule is the one an object has.
        return define_own_property(value::object(&arr->named_table()), name, wanted);
    }
    return false;
}

// THE ONE BODY behind op::define_data, ct_aot_define_data and the
// define_own_name native, so no tier can drift into a [[Set]].
void context::define_data_property(value target, value key, value v, bool enumerable) {
    if (!target.is_object_like()) { return; }
    property_descriptor wanted;
    wanted.has_value = wanted.has_writable = wanted.has_enumerable = true;
    wanted.has_configurable = true;
    wanted.held = v;
    wanted.writable = wanted.configurable = true;
    wanted.enumerable = enumerable;
    const std::string name = to_string(key);
    if (!define_own_property(target, name, wanted)) {
        throw_error("TypeError", "Cannot redefine property: " + name);
    }
}

} // namespace ctbrowser::script
