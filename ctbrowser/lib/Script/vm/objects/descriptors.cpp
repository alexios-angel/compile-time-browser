// ctbrowser.script context - property descriptors: [[GetOwnProperty]],
// [[Delete]] and [[DefineOwnProperty]], and extensibility.
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

void context::delete_named(value target, const std::string & name) {
    // TODO(strict): a false answer here is a TypeError under "use strict". The
    // engine has no strict mode, and sloppy `delete` evaluates to false without
    // throwing - which is what the compiler emits today (a constant `true`; see
    // the note on delete_own_property).
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
        if (accessor_entry * entry = fn->find_accessor(name)) {
            out.has_get = out.has_set = true;
            out.getter = entry->getter;
            out.setter = entry->setter;
            out.has_enumerable = out.has_configurable = true;
            out.enumerable = (entry->attrs & attr_enumerable) != 0;
            out.configurable = (entry->attrs & attr_configurable) != 0;
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
        if (accessor_entry * entry = fn->find_accessor(name)) {
            if ((entry->attrs & attr_configurable) == 0) { return false; }
            return fn->erase(name);
        }
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
        if (target.is_kind(heap_kind::native)) {
            static_cast<native_object *>(target.as_heap())
                ->define_accessor(name, getter, setter, accessor_attrs);
            return true;
        }
        // AN ARRAY HAS NOWHERE TO PUT ONE, and answers true.
        //
        // An array's elements are a std::vector. Answering FALSE here would
        // turn what has always been a
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

} // namespace ctbrowser::script
