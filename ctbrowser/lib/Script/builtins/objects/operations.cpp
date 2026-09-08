// ctbrowser.script builtins - the abstract operations Object and Reflect both
// answer through: FromPropertyDescriptor, OwnPropertyKeys over every kind of
// value, [[GetPrototypeOf]], and ToPropertyDescriptor's own refusals.
//
// One of five files carved out of a 1,429-line builtins/objects.cpp on
// 2026-09-08 - which was itself one of five carved out of builtins.cpp on
// 2026-08-09. Everything shared - the argument helpers, namespace detail, and
// these functions' declarations - is in ../internal.hpp; what only this
// directory shares is in internal.hpp beside this.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

using detail::key_filter;

namespace {

[[nodiscard]] inline bool wanted_key(key_filter which, const std::string & key) {
    const bool symbol = key.starts_with(symbol_key_prefix);
    return which == key_filter::all || (which == key_filter::symbols) == symbol;
}

} // namespace

namespace detail {

// A context::property_descriptor AS JAVASCRIPT SEES IT (6.2.6.4,
// FromPropertyDescriptor). Four callers needed the same object and each built
// its own, which is why three of them reported `writable: true` for every
// property whether or not it was one.
[[nodiscard]] object_object * descriptor_object(context & cx,
                                                const context::property_descriptor & from) {
    object_object * out = detail::new_table(cx);
    if (from.is_accessor()) {
        out->set("get", from.getter);
        out->set("set", from.setter);
    } else {
        out->set("value", from.held);
        out->set("writable", value::boolean(from.writable));
    }
    out->set("enumerable", value::boolean(from.enumerable));
    out->set("configurable", value::boolean(from.configurable));
    return out;
}

// EVERY OWN KEY OF ANY VALUE, including the synthesised ones. An array
// has `length` and its indices, a string has `length` and its characters, a
// function has `name`, `length` and `prototype` - and getOwnPropertyNames
// reported none of them because it only knew about object_object.
[[nodiscard]] std::vector<std::string> own_property_names(context & cx, value of,
                                                          key_filter which) {
    std::vector<std::string> out;
    if (of.is_kind(heap_kind::proxy)) {
        return own_property_names(cx, static_cast<proxy_object *>(of.as_heap())->target, which);
    }
    if (of.is_object()) {
        static_cast<object_object *>(of.as_heap())->each_own_key([&](const std::string & k) {
            if (wanted_key(which, k)) { out.push_back(k); }
        });
        return out;
    }
    // Nothing but an object_object can hold a symbol key: the other three
    // tables are only ever written by the engine itself.
    if (which == key_filter::symbols) { return out; }
    if (of.is_array()) {
        auto * arr = static_cast<array_object *>(of.as_heap());
        for (std::size_t i = 0; i < arr->length(); ++i) { out.push_back(std::to_string(i)); }
        for (const auto & [at, held] : arr->sparse) {
            (void)held;
            out.push_back(std::to_string(at));
        }
        out.emplace_back("length");
        return out;
    }
    if (of.is_string()) {
        const std::size_t n = static_cast<string_object *>(of.as_heap())->text.size();
        for (std::size_t i = 0; i < n; ++i) { out.push_back(std::to_string(i)); }
        out.emplace_back("length");
        return out;
    }
    if (of.is_kind(heap_kind::native)) {
        auto * fn = static_cast<native_object *>(of.as_heap());
        bool named = false;
        for (const auto & [key, held] : fn->props) {
            (void)held;
            out.push_back(key);
            named = named || key == "name";
        }
        if (!named) { out.emplace_back("name"); }
        return out;
    }
    if (of.is_kind(heap_kind::function)) {
        auto * closure = static_cast<closure_object *>(of.as_heap());
        // 10.2.5 CREATION ORDER: `length`, then `name`, then `prototype`. All
        // three are synthesised by context::own_property unless the page has
        // redefined one, so each is asked for rather than asserted - an arrow
        // has no `prototype` and a closure with no compiled proto has neither
        // of the other two - and skipped here when the table already carries
        // it, so a redefined `length` is reported once rather than twice.
        //
        // ASKING FOR `prototype` CREATES IT. `context::ensure_prototype` is lazy
        // and materialises the slot into the table the moment anything looks, so
        // the loop below then found the entry this loop had just caused and
        // reported `prototype` TWICE. The names taken here are remembered and
        // skipped there rather than the test being taken twice.
        std::vector<std::string_view> taken;
        for (const char * synthesised : {"length", "name", "prototype"}) {
            if (closure->find(synthesised) == nullptr &&
                closure->find_accessor(synthesised) == nullptr &&
                cx.has_own_property(of, synthesised)) {
                out.emplace_back(synthesised);
                taken.emplace_back(synthesised);
            }
        }
        for (const auto & [key, held] : closure->props) {
            (void)held;
            if (std::ranges::find(taken, key) != taken.end()) { continue; }
            out.push_back(key);
        }
        for (const accessor_entry & entry : closure->accessors.entries) {
            out.push_back(entry.key);
        }
        return out;
    }
    return out;
}

// [[GetPrototypeOf]], FOR EVERY KIND OF VALUE, and the one place that decides
// what a plain object's prototype IS.
//
// It answered `object_object::prototype` - which is `null` for every object
// that did not come from `class` or `Object.create` - so
// `Object.getPrototypeOf({}) === Object.prototype` was false. That is not what
// this engine does: context::lookup_property ends EVERY chain walk at the
// `proto_kind::object` table, which is why `({}).hasOwnProperty` resolves at
// all. Reporting null was a lie about behaviour the engine already had.
//
// The cost is stated rather than hidden: `Object.create(null)` cannot make an
// object that does not inherit Object.prototype here, so this now reports
// Object.prototype for one - which is the truthful answer about the object it
// actually built, and the honest half of a deviation that used to be reported
// two contradictory ways at once.
[[nodiscard]] value prototype_of(context & cx, value of) {
    if (of.is_kind(heap_kind::proxy)) {
        return prototype_of(cx, static_cast<proxy_object *>(of.as_heap())->target);
    }
    const auto table = [&](context::proto_kind kind) {
        object_object * found = cx.prototype(kind);
        return found == nullptr ? value::null() : value::object(found);
    };
    if (of.is_object()) {
        auto * obj = static_cast<object_object *>(of.as_heap());
        if (obj->prototype.is_object()) { return obj->prototype; }
        // Object.prototype's own [[Prototype]] is null, and it is the only
        // table for which that is true.
        if (obj == cx.prototype(context::proto_kind::object)) { return value::null(); }
        return table(context::proto_kind::object);
    }
    // A FUNCTION HAS A [[Prototype]] TOO, and it is not its `prototype`
    // property. Babel's `_inherits` sets both - the subclass's prototype
    // property for instance methods, the subclass FUNCTION for static ones -
    // and answering null for a function broke every transpiled `extends`.
    // For the NativeErrors the link is `Error` rather than %Function.prototype%
    // (20.5.6.2), which is why it is consulted before the table.
    if (of.is_kind(heap_kind::function)) {
        auto * closure = static_cast<closure_object *>(of.as_heap());
        if (!closure->proto_link.is_null()) { return closure->proto_link; }
        return table(context::proto_kind::function);
    }
    if (of.is_kind(heap_kind::native)) {
        auto * fn = static_cast<native_object *>(of.as_heap());
        if (!fn->proto_link.is_null()) { return fn->proto_link; }
        return table(context::proto_kind::function);
    }
    // A PRIMITIVE HAS A PROTOTYPE TOO. `Object.getPrototypeOf('x')` is
    // String.prototype, not null - the primitive is boxed for the lookup, which
    // is the same reason `'x'.toUpperCase()` works at all.
    //
    // Returning null made an ordinary type-detection loop draw the wrong
    // conclusion rather than none: colorjs walks
    // `Object.getPrototypeOf(arg)?.constructor?.name` and compares it to the
    // constructor's name. With null on one side and a nameless class on the
    // other, undefined === undefined reported a MATCH.
    if (of.is_string()) { return table(context::proto_kind::string); }
    if (of.is_number()) { return table(context::proto_kind::number); }
    if (of.is_boolean()) { return table(context::proto_kind::boolean); }
    if (of.is_array()) { return table(context::proto_kind::array); }
    if (of.is_kind(heap_kind::symbol)) { return table(context::proto_kind::symbol); }
    if (of.is_kind(heap_kind::bigint)) { return table(context::proto_kind::bigint); }
    return value::null();
}

// 6.2.6.6 ToPropertyDescriptor's OWN three refusals, which nothing here made.
//
// ValidateAndApplyPropertyDescriptor - context::define_own_property - is a
// different question: it asks whether a well-formed descriptor may be applied
// to the property that is already there. These three are about the descriptor
// OBJECT, they are checked before any property is touched, and without them
// `Object.defineProperty(o, 'x', {get: 1})` installed a getter that could not
// be called and `{get: g, value: 1}` installed one of the two silently.
[[nodiscard]] bool valid_descriptor(context & cx, const context::property_descriptor & d) {
    if (d.has_get && !d.getter.is_undefined() && !d.getter.is_callable()) {
        cx.throw_error("TypeError", "Getter must be a function");
        return false;
    }
    if (d.has_set && !d.setter.is_undefined() && !d.setter.is_callable()) {
        cx.throw_error("TypeError", "Setter must be a function");
        return false;
    }
    if (d.is_accessor() && d.is_data()) {
        cx.throw_error("TypeError", "Invalid property descriptor. Cannot both specify accessors "
                                    "and a value or writable attribute");
        return false;
    }
    return true;
}

} // namespace detail

} // namespace ctbrowser::script::builtins_detail
