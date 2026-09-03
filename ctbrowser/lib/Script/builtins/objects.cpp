// ctbrowser.script builtins - Object, the error types, Proxy, Function and generators.
//
// One of five files carved out of a 4,118-line builtins.cpp on 2026-08-09.
// Everything shared - the argument helpers, namespace detail, and these
// functions' declarations - is in internal.hpp.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

namespace {

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

// EVERY OWN STRING KEY OF ANY VALUE, including the synthesised ones. An array
// has `length` and its indices, a string has `length` and its characters, a
// function has `name`, `length` and `prototype` - and getOwnPropertyNames
// reported none of them because it only knew about object_object.
[[nodiscard]] std::vector<std::string> own_property_names(context & cx, value of) {
    std::vector<std::string> out;
    if (of.is_object()) {
        static_cast<object_object *>(of.as_heap())->each_own_string_key([&](const std::string & k) {
            out.push_back(k);
        });
        return out;
    }
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
        if (cx.has_own_property(of, "prototype")) { out.emplace_back("prototype"); }
        for (const auto & [key, held] : closure->props) {
            (void)held;
            if (key != "prototype") { out.push_back(key); }
        }
        for (const accessor_entry & entry : closure->accessors.entries) {
            out.push_back(entry.key);
        }
        // An arrow has neither, and a closure with no compiled proto has
        // neither: ask rather than assert.
        if (cx.has_own_property(of, "name")) { out.emplace_back("name"); }
        if (cx.has_own_property(of, "length")) { out.emplace_back("length"); }
        return out;
    }
    return out;
}

// 7.3.15 SetIntegrityLevel. `frozen` false is "sealed": configurable off
// everywhere, writable left alone.
inline void set_integrity(context & cx, value target, bool frozen) {
    cx.prevent_extensions(target);
    if (target.is_object()) {
        auto * obj = static_cast<object_object *>(target.as_heap());
        obj->normalise();
        if (obj->attrs.size() < obj->props.size()) {
            obj->attrs.resize(obj->props.size(), attr_default);
        }
        for (std::uint8_t & a : obj->attrs) {
            a = static_cast<std::uint8_t>(a & ~attr_configurable);
            if (frozen) { a = static_cast<std::uint8_t>(a & ~attr_writable); }
        }
        for (accessor_entry & entry : obj->accessors.entries) {
            entry.attrs = static_cast<std::uint8_t>(entry.attrs & ~attr_configurable);
        }
        return;
    }
    if (target.is_array()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        arr->elements_configurable = false;
        if (frozen) { arr->elements_writable = false; }
        return;
    }
    if (target.is_kind(heap_kind::native)) {
        auto * fn = static_cast<native_object *>(target.as_heap());
        if (fn->attrs.size() < fn->props.size()) {
            fn->attrs.resize(fn->props.size(), attr_default);
        }
        for (std::uint8_t & a : fn->attrs) {
            a = static_cast<std::uint8_t>(a & ~attr_configurable);
            if (frozen) { a = static_cast<std::uint8_t>(a & ~attr_writable); }
        }
        return;
    }
    if (target.is_kind(heap_kind::function)) {
        auto * closure = static_cast<closure_object *>(target.as_heap());
        if (closure->attrs.size() < closure->props.size()) {
            closure->attrs.resize(closure->props.size(), attr_default);
        }
        for (std::uint8_t & a : closure->attrs) {
            a = static_cast<std::uint8_t>(a & ~attr_configurable);
            if (frozen) { a = static_cast<std::uint8_t>(a & ~attr_writable); }
        }
        for (accessor_entry & entry : closure->accessors.entries) {
            entry.attrs = static_cast<std::uint8_t>(entry.attrs & ~attr_configurable);
        }
    }
}

// 7.3.16 TestIntegrityLevel. A PRIMITIVE IS FROZEN AND SEALED - 19.1.2.15 says
// isFrozen(1) is true, because there is nothing about it to change.
[[nodiscard]] inline bool test_integrity(context & cx, value target, bool frozen) {
    if (!target.is_object_like()) { return true; }
    if (cx.is_extensible(target)) { return false; }
    // AN ARRAY ANSWERS FROM ITS TWO BOOLS rather than from a walk: a million
    // elements would otherwise mean a million descriptor objects to answer one
    // question about all of them.
    if (target.is_array()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        return !arr->elements_configurable && (!frozen || !arr->elements_writable);
    }
    for (const std::string & key : own_property_names(cx, target)) {
        context::property_descriptor found;
        if (!cx.own_property(target, key, found)) { continue; }
        if (found.configurable) { return false; }
        if (frozen && !found.is_accessor() && found.writable) { return false; }
    }
    return true;
}

} // namespace

void install_object(context & cx) {
    using detail::method;
    using detail::new_table;

    // `Object.prototype`. There was NO table for it, so every object in the
    // engine was missing hasOwnProperty, toString and valueOf - and
    // `hasOwnProperty` in particular is how half the library code in existence
    // asks whether a key is really there rather than inherited.
    object_object * object_proto = new_table(cx);
    method(cx, object_proto, "hasOwnProperty", [](context & c, std::span<value> a) {
        const value self = c.current_this();
        const std::string key = str_at(c, a, 0);
        if (self.is_object()) {
            auto * obj = static_cast<object_object *>(self.as_heap());
            return value::boolean(obj->find(key) != nullptr || obj->find_accessor(key) != nullptr);
        }
        // A PROXY ANSWERS FOR ITSELF, the way `in` already lets it: the trap is
        // the only thing that knows what the proxy is standing in for. `window`
        // is one, and `window.hasOwnProperty('HTMLVideoElement')` reaching past
        // its handler to a bare object would say "no" about every global there
        // is. Falling back to the target when there is no trap matches the
        // `has_property` opcode exactly.
        if (self.is_kind(heap_kind::proxy)) {
            auto * p = static_cast<proxy_object *>(self.as_heap());
            const value trap = c.proxy_trap(self, "has");
            if (trap.is_callable()) {
                const value args[2] = {p->target, c.string(key)};
                return value::boolean(c.truthy(c.call(trap, args, p->handler)));
            }
            return value::boolean(!c.lookup_property(p->target, key).is_undefined());
        }
        if (self.is_array()) {
            auto * arr = static_cast<array_object *>(self.as_heap());
            if (key == "length") { return value::boolean(true); }
            // AN INDEX, so the whole key must be one - `"1x" in a` is false.
            // from_chars reports where it stopped, which is the same check
            // without strtod's locale sensitivity.
            double at = 0.0;
            const auto [stopped, failed] = std::from_chars(key.data(), key.data() + key.size(), at);
            return value::boolean(failed == std::errc{} && stopped == key.data() + key.size() &&
                                  at >= 0 && at < static_cast<double>(arr->items.size()));
        }
        // A FUNCTION, A NATIVE AND A STRING each have own properties too -
        // `f.name`, `f.length`, `Array.prototype` and `"abc".length` among
        // them - and answering false about all of them is what made
        // test262's verifyProperty report "should be an own property" for
        // every built-in it looked at. context::has_own_property is the one
        // answer all four tables share.
        return value::boolean(c.has_own_property(self, key));
    });
    // `[object Type]`, for whatever the receiver actually is.
    //
    // Returning "[object Object]" unconditionally is right for a plain object
    // and wrong for every other receiver, and the reason this matters is that
    // `Object.prototype.toString.call(x)` is THE type-detection idiom - the one
    // way to tell an array from a plain object from a null from a string
    // without trusting a constructor a page may have replaced. Libraries then
    // parse the result: colorjs, bundled inside p5.js, does
    // `str.match(/^\[object\s+(.*?)\]$/)[1].toLowerCase()`, which against a
    // string that is not in that shape indexes null.
    method(cx, object_proto, "toString", [](context & c, std::span<value>) {
        const value self = c.current_this();
        std::string_view tag = "Object";
        if (self.is_undefined()) {
            tag = "Undefined";
        } else if (self.is_null()) {
            tag = "Null";
        } else if (self.is_array()) {
            tag = "Array";
        } else if (self.is_string()) {
            tag = "String";
        } else if (self.is_number()) {
            tag = "Number";
        } else if (self.is_boolean()) {
            tag = "Boolean";
        } else if (self.is_callable()) {
            tag = "Function";
        } else if (self.is_kind(heap_kind::symbol)) {
            tag = "Symbol";
        }
        return c.string("[object " + std::string{tag} + "]");
    });
    method(cx, object_proto, "valueOf",
           [](context & c, std::span<value>) { return c.current_this(); });
    method(cx, object_proto, "isPrototypeOf", [](context & c, std::span<value> a) {
        const value self = c.current_this();
        value walk = arg_at(a, 0);
        for (int depth = 0; depth < 64 && walk.is_object(); ++depth) {
            walk = static_cast<object_object *>(walk.as_heap())->prototype;
            if (walk.is_heap() && self.is_heap() && walk.as_heap() == self.as_heap()) {
                return value::boolean(true);
            }
        }
        return value::boolean(false);
    });
    method(cx, object_proto, "propertyIsEnumerable", [](context & c, std::span<value> a) {
        context::property_descriptor found;
        if (!c.own_property(c.current_this(), str_at(c, a, 0), found)) {
            return value::boolean(false);
        }
        return value::boolean(found.enumerable);
    });
    cx.set_prototype(context::proto_kind::object, object_proto);

    // `Object` IS CALLABLE. `Object(x)` coerces to an object and is what a
    // spread helper reaches for - Babel's `_objectSpread` opens with
    // `Object(source)` - so a plain namespace table is not enough: it has the
    // statics and cannot be called, which fails as "Object is not a function"
    // from inside a helper that has nothing to do with Object.
    auto * object_ctor = cx.allocate<native_object>("Object", [](context & c, std::span<value> a) {
        // An object passes through; a primitive is boxed, which here means the
        // nearest thing this engine has - an empty object - because there are
        // no wrapper types. Nothing but identity is observable either way for
        // the uses that matter, and `Object(x) === x` for an object is the
        // property helpers actually depend on.
        const value v = arg_at(a, 0);
        return v.is_object_like() ? v : c.make_object();
    });
    // `Object.prototype` REACHABLE FROM SCRIPT, not just consulted by lookup.
    //
    // The tables existed and property lookup fell back to them, but nothing
    // exposed them - so `Object.prototype` was undefined, and the very common
    // `var hasOwnProperty = Object.prototype.hasOwnProperty` read undefined and
    // called it. acorn opens with exactly that, which is where p5's error
    // system stopped.
    //
    // It is the same object lookup uses, so a page that adds to it is seen by
    // every object, which is what a page doing that expects.
    detail::constant(object_ctor, "prototype", value::object(object_proto));
    link_constructor(cx, object_proto, "Object", value::object(object_ctor));
    method(cx, object_ctor, "hasOwn", [](context & c, std::span<value> a) {
        return value::boolean(c.has_own_property(arg_at(a, 0), str_at(c, a, 1)));
    });

    // `Object.defineProperty(o, key, descriptor)` - 51 uses in p5.js, and the
    // reason the object model grew accessors at all. A descriptor is either
    // data (`value`) or accessor (`get`/`set`); the two are the same property
    // described two ways, so defining one removes the other.
    method(cx, object_ctor, "defineProperty", [](context & c, std::span<value> a) {
        // A NON-OBJECT TARGET IS A TypeError (19.1.2.4 step 1) and so is a
        // non-object descriptor (10.1.6.3 via ToPropertyDescriptor). Returning
        // the argument instead is how `Object.defineProperty(undefined, ...)`
        // looked like it had worked.
        if (!arg_at(a, 0).is_object_like()) {
            c.throw_error("TypeError", "Object.defineProperty called on non-object");
            return value::undefined();
        }
        if (!arg_at(a, 2).is_object()) {
            c.throw_error("TypeError", "Property description must be an object");
            return value::undefined();
        }
        if (!define_one(c, a[0], c.to_string(arg_at(a, 1)), a[2])) {
            c.throw_error("TypeError", "Cannot redefine property: " + c.to_string(arg_at(a, 1)));
            return value::undefined();
        }
        return a[0];
    });
    method(cx, object_ctor, "defineProperties", [](context & c, std::span<value> a) {
        if (!arg_at(a, 0).is_object_like()) {
            c.throw_error("TypeError", "Object.defineProperties called on non-object");
            return value::undefined();
        }
        if (!arg_at(a, 1).is_object()) { return a[0]; }
        // ENUMERABLE OWN KEYS ONLY (7.3.7 step 3 walks OwnPropertyKeys and
        // skips a non-enumerable one), and a snapshot first because defining
        // can run a getter that mutates the source.
        auto * from = static_cast<object_object *>(a[1].as_heap());
        std::vector<std::pair<std::string, value>> wanted;
        from->each_own_enumerable_key([&](const std::string & key) {
            if (value * held = from->find(key)) { wanted.emplace_back(key, *held); }
        });
        for (const auto & [key, descriptor] : wanted) {
            if (!descriptor.is_object()) {
                c.throw_error("TypeError", "Property description must be an object");
                return value::undefined();
            }
            if (!define_one(c, a[0], key, descriptor)) {
                c.throw_error("TypeError", "Cannot redefine property: " + key);
                return value::undefined();
            }
        }
        return a[0];
    });
    method(cx, object_ctor, "getOwnPropertyDescriptor", [](context & c, std::span<value> a) {
        context::property_descriptor found;
        if (!c.own_property(arg_at(a, 0), c.to_string(arg_at(a, 1)), found)) {
            return value::undefined();
        }
        return value::object(descriptor_object(c, found));
    });
    // `Object.create(proto)` and the two prototype accessors. A real chain has
    // existed since `extends`; what was missing was any way for a page to reach
    // it. p5.js uses create 19 times.
    method(cx, object_ctor, "create", [](context & c, std::span<value> a) {
        object_object * out = new_table(c);
        if (arg_at(a, 0).is_object()) { out->prototype = a[0]; }
        return value::object(out);
    });
    // A FUNCTION HAS A [[Prototype]] TOO, and it is not its `prototype`
    // property. Babel's `_inherits` sets both - the subclass's prototype
    // property for instance methods, the subclass FUNCTION for static ones -
    // and answering null for a function broke every transpiled `extends`.
    // A PRIMITIVE HAS A PROTOTYPE TOO. `Object.getPrototypeOf('x')` is
    // String.prototype, not null - the primitive is boxed for the lookup, which
    // is the same reason `'x'.toUpperCase()` works at all.
    //
    // Returning null made an ordinary type-detection loop draw the wrong
    // conclusion rather than none: colorjs walks
    // `Object.getPrototypeOf(arg)?.constructor?.name` and compares it to the
    // constructor's name. With null on one side and a nameless class on the
    // other, undefined === undefined reported a MATCH - so a plain string
    // passed as an instance of a colour space, and every conversion through it
    // silently handed the string straight back.
    method(cx, object_ctor, "getPrototypeOf", [](context & c, std::span<value> a) {
        const value of = arg_at(a, 0);
        if (of.is_object()) { return static_cast<object_object *>(of.as_heap())->prototype; }
        if (of.is_kind(heap_kind::function)) {
            return static_cast<closure_object *>(of.as_heap())->proto_link;
        }
        const auto table = [&](context::proto_kind kind) {
            object_object * found = c.prototype(kind);
            return found == nullptr ? value::null() : value::object(found);
        };
        if (of.is_string()) { return table(context::proto_kind::string); }
        if (of.is_number()) { return table(context::proto_kind::number); }
        if (of.is_boolean()) { return table(context::proto_kind::boolean); }
        if (of.is_array()) { return table(context::proto_kind::array); }
        if (of.is_kind(heap_kind::native)) { return table(context::proto_kind::function); }
        if (of.is_kind(heap_kind::symbol)) { return table(context::proto_kind::symbol); }
        return value::null();
    });
    method(cx, object_ctor, "setPrototypeOf", [](context &, std::span<value> a) {
        const value of = arg_at(a, 0);
        if (of.is_object()) {
            static_cast<object_object *>(of.as_heap())->prototype = arg_at(a, 1);
        } else if (of.is_kind(heap_kind::function)) {
            static_cast<closure_object *>(of.as_heap())->proto_link = arg_at(a, 1);
        }
        return of;
    });
    // NAMES, so string keys only - Reflect.ownKeys is the one that reports
    // symbols as well, and it keeps the unfiltered walk.
    method(cx, object_ctor, "getOwnPropertyNames", [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        for (const std::string & key : own_property_names(c, arg_at(a, 0))) {
            result->items.push_back(c.string(key));
        }
        return out;
    });
    method(cx, object_ctor, "getOwnPropertyDescriptors", [](context & c, std::span<value> a) {
        object_object * out = new_table(c);
        const value from = arg_at(a, 0);
        for (const std::string & key : own_property_names(c, from)) {
            context::property_descriptor found;
            if (c.own_property(from, key, found)) {
                out->set(key, value::object(descriptor_object(c, found)));
            }
        }
        return value::object(out);
    });
    method(cx, object_ctor, "fromEntries", [](context & c, std::span<value> a) {
        object_object * out = new_table(c);
        if (arg_at(a, 0).is_array()) {
            for (const value & pair : static_cast<array_object *>(a[0].as_heap())->items) {
                if (!pair.is_array()) { continue; }
                const auto & items = static_cast<array_object *>(pair.as_heap())->items;
                if (items.size() >= 2) { out->set(c.to_string(items[0]), items[1]); }
            }
        }
        return value::object(out);
    });
    // --- INTEGRITY LEVELS, which used to be theatre ----------------------
    //
    // `freeze` returned its argument and did nothing; `isFrozen` answered false
    // about everything, including an object it had just been asked to freeze.
    // Both now do what 7.3.15/7.3.16 say: seal clears [[Configurable]] on every
    // own property and [[Extensible]] on the object; freeze clears
    // [[Writable]] as well, except on an accessor, which has none.
    method(cx, object_ctor, "freeze", [](context & c, std::span<value> a) {
        set_integrity(c, arg_at(a, 0), true);
        return arg_at(a, 0);
    });
    method(cx, object_ctor, "seal", [](context & c, std::span<value> a) {
        set_integrity(c, arg_at(a, 0), false);
        return arg_at(a, 0);
    });
    method(cx, object_ctor, "preventExtensions", [](context & c, std::span<value> a) {
        c.prevent_extensions(arg_at(a, 0));
        return arg_at(a, 0);
    });
    method(cx, object_ctor, "isFrozen", [](context & c, std::span<value> a) {
        return value::boolean(test_integrity(c, arg_at(a, 0), true));
    });
    method(cx, object_ctor, "isSealed", [](context & c, std::span<value> a) {
        return value::boolean(test_integrity(c, arg_at(a, 0), false));
    });
    method(cx, object_ctor, "isExtensible", [](context & c, std::span<value> a) {
        return value::boolean(c.is_extensible(arg_at(a, 0)));
    });
    // SameValue, 7.2.11 - which is `===` except that it separates the two
    // zeros and calls NaN equal to itself. Those are exactly the two questions
    // `===` cannot answer, which is why every test in this directory that cares
    // about -0 had to spell it `1/x === -Infinity` instead.
    method(cx, object_ctor, "is", [](context &, std::span<value> a) {
        const value x = arg_at(a, 0);
        const value y = arg_at(a, 1);
        if (x.is_number() && y.is_number()) {
            const double p = x.as_number();
            const double q = y.as_number();
            if (std::isnan(p) && std::isnan(q)) { return value::boolean(true); }
            // Same magnitude AND same sign: std::signbit is what tells +0 from
            // -0, since they compare equal under every operator.
            if (p == q) { return value::boolean(std::signbit(p) == std::signbit(q)); }
            return value::boolean(false);
        }
        return value::boolean(x.strict_equals(y));
    });
    method(cx, object_ctor, "keys", [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        if (arg_at(a, 0).is_object()) {
            // An accessor IS a property, and definition order is observable.
            // STRING keys only - a symbol-keyed property is invisible here -
            // and ENUMERABLE ones only, which is the half that did not exist
            // before there were attributes.
            static_cast<object_object *>(a[0].as_heap())
                ->each_own_enumerable_key(
                    [&](const std::string & k) { result->items.push_back(c.string(k)); });
        }
        return out;
    });
    method(cx, object_ctor, "values", [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        if (arg_at(a, 0).is_object()) {
            auto * from = static_cast<object_object *>(a[0].as_heap());
            from->each_own_enumerable_key([&](const std::string & key) {
                result->items.push_back(c.lookup_property(a[0], key));
            });
        }
        return out;
    });
    method(cx, object_ctor, "entries", [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        if (arg_at(a, 0).is_object()) {
            auto * from = static_cast<object_object *>(a[0].as_heap());
            from->each_own_enumerable_key([&](const std::string & key) {
                value pair = c.make_array();
                auto * entry = static_cast<array_object *>(pair.as_heap());
                entry->items.push_back(c.string(key));
                entry->items.push_back(c.lookup_property(a[0], key));
                result->items.push_back(pair);
            });
        }
        return out;
    });
    method(cx, object_ctor, "assign", [](context & c, std::span<value> a) {
        const value target = arg_at(a, 0);
        if (!target.is_object()) { return target; }
        for (std::size_t i = 1; i < a.size(); ++i) {
            if (!a[i].is_object()) { continue; }
            // A SNAPSHOT, because storing into the target can run a setter that
            // mutates the source; ENUMERABLE own keys only (7.3.24 step 5);
            // and through store_property rather than set(), because
            // Object.assign is specified as [[Set]] and a frozen target must
            // therefore reject the write.
            auto * from = static_cast<object_object *>(a[i].as_heap());
            std::vector<std::pair<std::string, value>> entries;
            from->each_own_entry([&](const std::string & key, std::uint8_t attrs) {
                // SYMBOL KEYS INCLUDED: 7.3.24 walks OwnPropertyKeys, which
                // reports them - the same exception object spread needs.
                if ((attrs & attr_enumerable) != 0) {
                    entries.emplace_back(key, c.lookup_property(a[i], key));
                }
            });
            for (const auto & [key, item] : entries) { c.store_property(target, key, item); }
        }
        return target;
    });
    cx.define_global("Object", value::object(object_ctor));
}

// `Error` and the standard subclasses. 239 `throw new` in p5.js, and every one
// of them used to construct undefined - which raised "attempted to construct a
// non-function" and killed the run outright, uncatchably, from inside a `try`
// that was there to handle exactly that.
//
// An error is an ordinary object with `name`, `message` and `stack`, and each
// constructor's `prototype` chains to Error's so `e instanceof Error` holds for
// a TypeError and for a page's own `class MyError extends Error`.
void install_errors(context & cx) {
    using detail::method;
    using detail::new_table;

    object_object * error_proto = new_table(cx);
    error_proto->set("name", cx.string("Error"));
    error_proto->set("message", cx.string(""));
    method(cx, error_proto, "toString", [](context & c, std::span<value>) {
        const value self = c.current_this();
        const std::string name = c.to_string(c.lookup_property(self, "name"));
        const std::string message = c.to_string(c.lookup_property(self, "message"));
        return c.string(message.empty() ? name : name + ": " + message);
    });

    // One constructor shape, five names. `parent` is Error's prototype for the
    // subclasses, so the chain a page walks is the one it expects.
    const auto define = [&](const char * name, object_object * parent) {
        object_object * proto = parent == nullptr ? error_proto : new_table(cx);
        if (parent != nullptr) {
            proto->prototype = value::object(parent);
            proto->set("name", cx.string(name));
        }
        auto * ctor = cx.allocate<native_object>(name, [proto](context & c, std::span<value> a) {
            // `this` is the instance when called through `new`; a bare
            // `Error(...)` makes one anyway, which is what the spec says.
            value self = c.current_this();
            if (!self.is_object()) { self = c.make_object(); }
            auto * made = static_cast<object_object *>(self.as_heap());
            if (!made->prototype.is_object()) { made->prototype = value::object(proto); }
            if (!a.empty()) { made->set("message", c.string(c.to_string(a[0]))); }
            // `stack` CARRIES THE FRAMES, not just the message.
            //
            // It said "TypeError: whatever" and stopped there, which reads as a
            // stack to code that only prints it and is useless to code that
            // wants to know WHERE - and a library that reports the site of an
            // error is the normal reason to look at one. p5's Friendly Error
            // System is exactly that.
            //
            // No frame is skipped: a native pushes none of its own, so the top
            // of the stack is already the JS function that wrote `new Error`,
            // which is the line a reader wants named first.
            made->set("stack", c.string(c.to_string(c.lookup_property(self, "name")) +
                                        (a.empty() ? std::string{} : ": " + c.to_string(a[0])) +
                                        c.current_stack()));
            return self;
        });
        proto->set("constructor", value::object(ctor));
        ctor->set("prototype", value::object(proto));
        cx.define_global(name, value::object(ctor));
        return proto;
    };
    define("Error", nullptr);
    for (const char * name :
         {"TypeError", "RangeError", "ReferenceError", "SyntaxError", "EvalError", "URIError"}) {
        (void)define(name, error_proto);
    }
    cx.set_prototype(context::proto_kind::error, error_proto);
}

// `Proxy` and `Reflect`. p5.js has three proxies, and one of them runs at the
// bundle's top level - `p5.renderers['p2d-p3'] = new Proxy(Renderer2D,
// {construct(...) {...}})` - so the bundle could not finish loading without it.
//
// Three traps: `get`, `has` and `construct`, which are the three p5 uses. A
// trap that is not implemented is not silently skipped - the operation falls
// through to the target, which is what an absent trap means anyway. The traps
// that ARE missing (set, deleteProperty, ownKeys, apply and the rest) behave
// the same way, so a page using one gets the target's behaviour rather than a
// wrong answer; that is a real gap and it is named here rather than discovered.
void install_proxy(context & cx) {
    using detail::method;
    using detail::new_table;

    cx.define_native("Proxy", [](context & c, std::span<value> a) {
        return value::object(c.allocate<proxy_object>(arg_at(a, 0), arg_at(a, 1)));
    });

    // Reflect is the un-trapped operation a handler calls to do the default
    // thing - `Reflect.get(t, k)` inside a `get` trap is how a proxy adds
    // behaviour instead of replacing it.
    object_object * reflect = new_table(cx);
    method(cx, reflect, "get", [](context & c, std::span<value> a) {
        return c.lookup_index(arg_at(a, 0), arg_at(a, 1));
    });
    method(cx, reflect, "set", [](context & c, std::span<value> a) {
        if (arg_at(a, 0).is_object()) {
            static_cast<object_object *>(a[0].as_heap())
                ->set(c.to_string(arg_at(a, 1)), arg_at(a, 2));
        }
        return value::boolean(true);
    });
    method(cx, reflect, "has", [](context & c, std::span<value> a) {
        return value::boolean(!c.lookup_index(arg_at(a, 0), arg_at(a, 1)).is_undefined());
    });
    method(cx, reflect, "construct", [](context & c, std::span<value> a) {
        std::vector<value> args;
        if (arg_at(a, 1).is_array()) { args = static_cast<array_object *>(a[1].as_heap())->items; }
        return c.construct(arg_at(a, 0), args);
    });
    method(cx, reflect, "apply", [](context & c, std::span<value> a) {
        std::vector<value> args;
        if (arg_at(a, 2).is_array()) { args = static_cast<array_object *>(a[2].as_heap())->items; }
        return c.call(arg_at(a, 0), args, arg_at(a, 1));
    });
    method(cx, reflect, "ownKeys", [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        if (arg_at(a, 0).is_object()) {
            static_cast<object_object *>(a[0].as_heap())->each_own_key([&](const std::string & k) {
                result->items.push_back(c.string(k));
            });
        }
        return out;
    });
    // The un-throwing halves of Object.defineProperty and friends: Reflect
    // ANSWERS FALSE where Object throws, which is the whole difference between
    // the two namespaces.
    method(cx, reflect, "defineProperty", [](context & c, std::span<value> a) {
        if (!arg_at(a, 2).is_object()) { return value::boolean(false); }
        return value::boolean(define_one(c, arg_at(a, 0), c.to_string(arg_at(a, 1)), a[2]));
    });
    method(cx, reflect, "getOwnPropertyDescriptor", [](context & c, std::span<value> a) {
        context::property_descriptor found;
        if (!c.own_property(arg_at(a, 0), c.to_string(arg_at(a, 1)), found)) {
            return value::undefined();
        }
        return value::object(descriptor_object(c, found));
    });
    method(cx, reflect, "deleteProperty", [](context & c, std::span<value> a) {
        return value::boolean(c.delete_own_property(arg_at(a, 0), c.to_string(arg_at(a, 1))));
    });
    method(cx, reflect, "isExtensible", [](context & c, std::span<value> a) {
        return value::boolean(c.is_extensible(arg_at(a, 0)));
    });
    method(cx, reflect, "preventExtensions", [](context & c, std::span<value> a) {
        c.prevent_extensions(arg_at(a, 0));
        return value::boolean(true);
    });
    method(cx, reflect, "getPrototypeOf", [](context &, std::span<value> a) {
        if (!arg_at(a, 0).is_object()) { return value::null(); }
        return static_cast<object_object *>(a[0].as_heap())->prototype;
    });
    cx.define_global("Reflect", value::object(reflect));
}

// `Function.prototype`. 84 `.call(`, 78 `.apply(` and 26 `.bind(` in p5.js -
// and it cannot install one event listener without bind:
// `window.addEventListener(e, this['_on' + e].bind(this), {...})`.
void install_function(context & cx) {
    using detail::method;
    using detail::new_table;
    object_object * function_proto = new_table(cx);

    method(cx, function_proto, "call", [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!self.is_callable()) { return value::undefined(); }
        const std::vector<value> rest(a.begin() + (a.empty() ? 0 : 1), a.end());
        return c.call(self, rest, arg_at(a, 0));
    });
    method(cx, function_proto, "apply", [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!self.is_callable()) { return value::undefined(); }
        std::vector<value> args;
        if (arg_at(a, 1).is_array()) { args = static_cast<array_object *>(a[1].as_heap())->items; }
        return c.call(self, args, arg_at(a, 0));
    });
    method(cx, function_proto, "bind", [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!self.is_callable()) { return value::undefined(); }
        const value receiver = arg_at(a, 0);
        // The arguments bound NOW are prepended to the ones supplied later,
        // which is what makes `f.bind(o, 1)` a partial application rather than
        // just a receiver change.
        const auto bound =
            std::make_shared<std::vector<value>>(a.begin() + (a.empty() ? 0 : 1), a.end());
        return value::object(detail::cx_native(
            c, "bound", [self, receiver, bound](context & inner, std::span<value> later) {
                std::vector<value> args = *bound;
                args.insert(args.end(), later.begin(), later.end());
                return inner.call(self, args, receiver);
            }));
    });
    // TODO: return the REAL source. p5's Friendly Error System parses a sketch
    // with `f.toString()` and gets "[native code]", which is where the ratchet
    // stops. Needs a source span on function_proto - the ctjs node's `text` is
    // a string_view INTO the source, so the offset is a subtraction - plus the
    // program keeping its source string. Error.stack wants the same thing, and
    // would get real line numbers from it.
    method(cx, function_proto, "toString", [](context & c, std::span<value>) {
        // THE REAL SOURCE, when there is any. A closure knows which program its
        // protos came from, and the program kept the text - so this is a
        // substring, not a reconstruction, and what comes back is exactly what
        // was written.
        const value self = c.current_this();
        if (self.is_kind(heap_kind::function)) {
            auto * closure = static_cast<closure_object *>(self.as_heap());
            if (closure->owner != nullptr && closure->proto != nullptr) {
                const struct function_proto & fp = *closure->proto;
                const std::string & text = closure->owner->source;
                if (fp.source_end > fp.source_begin && fp.source_end <= text.size()) {
                    return c.string(text.substr(fp.source_begin, fp.source_end - fp.source_begin));
                }
            }
        }
        // A native has no source; saying so the way every engine does keeps a
        // caller that concatenates the result from producing something strange.
        return c.string("function () { [native code] }");
    });
    cx.set_prototype(context::proto_kind::function, function_proto);
}

// `new Function(body)` - A COMPILER AT RUN TIME.
//
// It existed and refused, because a closure holds a `const function_proto *`
// into the program it came from and nothing owned a program compiled here. Two
// things closed that: `closure_object::owner` records which program a closure's
// nested functions live in, so a frame from one program can call into another;
// and the context now OWNS the programs it compiles, so they outlive the
// closures that point into them.
//
// The body is wrapped in a function expression and returned, so the parameters
// and the body go through exactly the path a written-out function does. p5.js
// builds three of these for shader source; a bundle may build any number.
void install_dynamic_function(context & cx) {
    cx.define_native("Function", [](context & c, std::span<value> a) {
        // `new Function(a, b, 'return a + b')` - every argument but the last
        // names a parameter, and the last is the body. `new Function()` is a
        // function that does nothing, which is what the spec says.
        std::string params;
        for (std::size_t i = 0; i + 1 < a.size(); ++i) {
            if (!params.empty()) { params += ","; }
            params += c.to_string(a[i]);
        }
        const std::string body = a.empty() ? std::string{} : c.to_string(a[a.size() - 1]);
        // RETURNED, not left as an expression statement: the program's value is
        // what its top level returns, and a bare expression yields nothing.
        // The newlines are the spec's own formatting, and they matter - they
        // keep a `//` comment at the end of the body from swallowing the brace.
        const std::string source =
            "return (function anonymous(" + params + "\n) {\n" + body + "\n});";

        program compiled = compiler::compile(source);
        if (!compiled.ok) {
            // A SyntaxError a page can catch, because `new Function` on
            // user-supplied text is exactly where one is expected.
            c.throw_error("SyntaxError", compiled.error);
            return value::undefined();
        }
        const program & kept = c.own_program(std::move(compiled));
        return c.run_nested(kept);
    });
    // `Function.prototype`, reachable from script rather than only consulted by
    // lookup. `Function.prototype.call.bind(...)` and
    // `Function.prototype.hasOwnProperty` are ordinary idioms, and this is the
    // same table lookup already walks - so a page that adds to it is seen by
    // every function, which is what a page doing that expects.
    if (object_object * table = cx.prototype(context::proto_kind::function)) {
        static_cast<native_object *>(cx.global("Function").as_heap())
            ->set("prototype", value::object(table));
        link_constructor(cx, table, "Function", cx.global("Function"));
    }
}

// `.next(v)`, `.throw(e)`, `.return(v)` - the iterator protocol, for every
// generator object at once. On a prototype for the same reason a promise's
// then/catch/finally are: three natives for the whole program rather than
// three per generator, and two generator objects then compare alike.
//
// Installed EAGERLY, unlike the promise table, because the object is built by
// context::make_generator over in the VM - which cannot reach into builtins to
// construct a table lazily.
void install_generator(context & cx) {
    object_object * table = detail::new_table(cx);
    const auto driver = [](context::resume_mode how) {
        return [how](context & c, std::span<value> a) {
            return c.generator_resume(c.current_this(), arg_at(a, 0), how);
        };
    };
    detail::method(cx, table, "next", driver(context::resume_mode::next));
    detail::method(cx, table, "throw", driver(context::resume_mode::thrown));
    detail::method(cx, table, "return", driver(context::resume_mode::returned));
    // A GENERATOR IS ITS OWN ITERATOR, which is what `for (x of gen())` needs
    // and what makes `[...gen()]` work.
    detail::method(cx, table, "@@iterator",
                   [](context & c, std::span<value>) { return c.current_this(); });
    cx.set_prototype(context::proto_kind::generator, table);
}

} // namespace ctbrowser::script::builtins_detail
