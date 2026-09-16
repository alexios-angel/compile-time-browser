// ctbrowser.script context - property descriptors: [[GetOwnProperty]],
// [[Delete]] and [[DefineOwnProperty]], and extensibility.
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
#include <vector>

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
    return arr.set_js_length(n);
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
    arr.items.resize(static_cast<std::size_t>(at) + 1, value::undefined());
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
    field("value", out.has_value, out.held);
    field("get", out.has_get, out.getter);
    field("set", out.has_set, out.setter);
    flag("writable", out.has_writable, out.writable);
    flag("enumerable", out.has_enumerable, out.enumerable);
    flag("configurable", out.has_configurable, out.configurable);
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

    // 10.5.5 [[GetOwnProperty]] of a proxy: the `getOwnPropertyDescriptor`
    // trap, or the target when there is none. The answer is ToPropertyDescriptor
    // then CompletePropertyDescriptor (steps 13-14), so a trap that says
    // `{value: v}` describes a non-writable, non-enumerable, non-configurable
    // property exactly as Object.defineProperty would read it. The invariant
    // checks against the target (steps 15-22) are not made.
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        const value trap = proxy_trap(target, "getOwnPropertyDescriptor");
        if (!trap.is_callable()) { return own_property(p->target, name, out); }
        const value args[2] = {p->target, key_value(name)};
        const value answer = call(trap, args, p->handler);
        if (answer.is_undefined()) { return false; }
        if (!answer.is_object_like()) {
            throw_error("TypeError", "getOwnPropertyDescriptor trap returned neither object nor "
                                     "undefined for property '" +
                                         name + "'");
            return false;
        }
        const rooted keep{*this, answer};
        out = to_property_descriptor(answer);
        if (out.is_accessor()) {
            out.has_get = out.has_set = true;
        } else {
            out.has_value = out.has_writable = true;
        }
        out.has_enumerable = out.has_configurable = true;
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
        if (name == "length") {
            // 10.4.2: { [[Writable]]: true, [[Enumerable]]: false,
            // [[Configurable]]: false }. A freeze, or defineProperty with
            // writable: false, clears the writable bit.
            out = property_descriptor::data(value::number(static_cast<double>(arr->js_length())),
                                            arr->length_writable ? attr_writable : attr_none);
            out.virtual_slot = true;
            return true;
        }
        std::uint32_t at = 0;
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
        const std::string & text = static_cast<string_object *>(target.as_heap())->text;
        if (name == "length") {
            // 10.4.3.5: a String exotic object's length is { false, false, false }.
            out = property_descriptor::data(value::number(static_cast<double>(text.size())),
                                            attr_none);
            out.virtual_slot = true;
            return true;
        }
        std::uint32_t at = 0;
        if (object_object::array_index_key(name, at) && at < text.size()) {
            out = property_descriptor::data(string(std::string{text[at]}), attr_enumerable);
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
    // 10.5.10: the `deleteProperty` trap, or the target. `delete el.dataset.x`
    // is the one a page reaches for - the DOMStringMap's handler removes the
    // attribute, which no delete on its target could do.
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        const value trap = proxy_trap(target, "deleteProperty");
        if (!trap.is_callable()) { return delete_own_property(p->target, name); }
        const value args[2] = {p->target, key_value(name)};
        return truthy(call(trap, args, p->handler));
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
        if (closure->find(name) == nullptr) {
            // The synthesised `name` and `length` - see closure_object::name_erased.
            if (name == "name") { closure->name_erased = true; }
            if (name == "length") { closure->length_erased = true; }
            return true;
        }
        if ((closure->attrs_of(name) & attr_configurable) == 0) { return false; }
        return closure->erase(name);
    }
    if (target.is_array()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        if (name == "length") { return false; } // non-configurable, 10.4.2
        std::uint32_t at = 0;
        if (!object_object::array_index_key(name, at)) {
            return arr->named ? delete_own_property(value::object(arr->named.get()), name) : true;
        }
        if (arr->is_view()) { return at >= arr->length(); }
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
    // 10.5.6: the `defineProperty` trap sees the descriptor as an OBJECT
    // (FromPropertyDescriptor, step 8), or the target defines it.
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        const value trap = proxy_trap(target, "defineProperty");
        if (!trap.is_callable()) { return define_own_property(p->target, name, wanted); }
        const value args[3] = {p->target, key_value(name), from_property_descriptor(wanted)};
        return truthy(call(trap, args, p->handler));
    }

    // 10.4.5.3: A TYPED ARRAY'S INTEGER INDEX is its own algorithm, before
    // ValidateAndApplyPropertyDescriptor. The index must be valid now, the
    // descriptor may not make the element non-configurable, non-enumerable,
    // an accessor or read-only, and a value goes through the kind's coercion
    // (TypedArraySetElement) - which may throw, and answers true past it.
    if (target.is_array()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        if (std::uint32_t at = 0;
            arr->elements != element_kind::none && object_object::array_index_key(name, at)) {
            if (at >= arr->length()) { return false; }
            if ((wanted.has_configurable && !wanted.configurable) ||
                (wanted.has_enumerable && !wanted.enumerable) || wanted.is_accessor() ||
                (wanted.has_writable && !wanted.writable)) {
                return false;
            }
            if (wanted.has_value) { (void)typed_element_set(*this, *arr, at, wanted.held); }
            return true;
        }
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
            if (wanted.has_get && !wanted.getter.same_value(current.getter)) { return false; }
            if (wanted.has_set && !wanted.setter.same_value(current.setter)) { return false; }
        } else if (!current.writable) {
            if (wanted.has_writable && wanted.writable) { return false; }
            if (wanted.has_value && !wanted.held.same_value(current.held)) { return false; }
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
        if (target.is_kind(heap_kind::native)) {
            static_cast<native_object *>(target.as_heap())
                ->define_accessor(name, getter, setter, accessor_attrs);
            return true;
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
            if (name == "length") { return false; }
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

} // namespace ctbrowser::script
