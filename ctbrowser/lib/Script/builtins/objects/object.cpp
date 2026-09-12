// ctbrowser.script builtins - Object: its statics and its prototype, with the
// helpers only they need - ObjectDefineProperties, EnumerableOwnProperties and
// the integrity levels.
//
// One of five files carved out of a 1,429-line builtins/objects.cpp on
// 2026-09-08 - which was itself one of five carved out of builtins.cpp on
// 2026-08-09. Everything shared - the argument helpers, namespace detail, and
// these functions' declarations - is in ../internal.hpp; what only this
// directory shares is in internal.hpp beside this.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

using detail::descriptor_object;
using detail::key_filter;
using detail::own_property_names;
using detail::prototype_of;
using detail::set_prototype_of;
using detail::valid_descriptor;

namespace {

// 7.1.18 ToObject's step 1, which is the first line of a dozen of the statics
// below and of every Object.prototype method. FALSE means the TypeError is
// already in flight and the caller must return at once.
[[nodiscard]] bool object_coercible(context & cx, value v, const char * what) {
    if (!v.is_nullish()) { return true; }
    cx.throw_error("TypeError", std::string{what} + " called on null or undefined");
    return false;
}

// Is `table` anywhere on `of`'s prototype chain? The depth cap is the one
// lookup_property uses, for the same reason: a page can make a chain cyclic and
// no question about one may hang.
[[nodiscard]] bool inherits_from(context & cx, value of, const object_object * table) {
    if (table == nullptr) { return false; }
    value walk = prototype_of(cx, of);
    for (int depth = 0; depth < 64 && walk.is_heap(); ++depth) {
        if (walk.as_heap() == table) { return true; }
        walk = prototype_of(cx, walk);
    }
    return false;
}

// 7.3.7 ObjectDefineProperties, shared by `Object.defineProperties` and the
// second argument of `Object.create` - which ignored it entirely, and which is
// 304 of test262's 320 files in built-ins/Object/create.
//
// TWO PASSES, and the specification is explicit about the split: every
// descriptor is READ and validated first, and only then is any of them applied.
// A source whose second descriptor is malformed must leave the first
// undefined - and reading can run a getter that mutates the source, which is
// the other reason the keys are snapshotted.
//
// The value of each key comes through [[Get]] rather than out of the source's
// own table: 7.3.7 step 5.b.i is `Get(props, nextKey)`, so a descriptor held in
// an ACCESSOR is read by calling it. `Object.defineProperties(o, funObj)` and
// `(o, arrObj)` are legal for the same reason - the source is any object, not
// only an object literal.
[[nodiscard]] bool define_properties(context & cx, value target, value from, const char * called) {
    if (!object_coercible(cx, from, called)) { return false; }
    std::vector<std::pair<std::string, context::property_descriptor>> wanted;
    for (const std::string & key : own_property_names(cx, from, key_filter::all)) {
        context::property_descriptor found;
        if (!cx.own_property(from, key, found) || !found.enumerable) { continue; }
        // An own DATA property's [[Get]] is its value; only an accessor has to
        // be called. Reading every key back by NAME would have missed an
        // array's elements, whose values do not come out of lookup_property -
        // `Object.defineProperties(o, [{value: 1}])` is legal and defines "0".
        const value descriptor = found.is_accessor() ? cx.lookup_property(from, key) : found.held;
        if (!descriptor.is_object_like()) {
            cx.throw_error("TypeError", "Property description must be an object");
            return false;
        }
        const context::property_descriptor read = cx.to_property_descriptor(descriptor);
        if (!valid_descriptor(cx, read)) { return false; }
        wanted.emplace_back(key, read);
    }
    for (const auto & [key, descriptor] : wanted) {
        if (!cx.define_own_property(target, key, descriptor)) {
            cx.throw_error("TypeError", "Cannot redefine property: " + key);
            return false;
        }
    }
    return true;
}

// EnumerableOwnProperties, 7.3.23 - the walk behind `Object.keys`, `values`,
// `entries` and `Object.assign`'s source loop.
//
// GENERIC over the receiver, which it was not: all four opened with
// `is_object()` and answered an empty list for everything else, so
// `Object.keys([1,2])` was `[]` and `Object.entries('ab')` was `[]`. Every one
// of them is specified as ToObject followed by OwnPropertyKeys, and this engine
// already has one answer for OwnPropertyKeys over all four tables.
template <typename Fn> void each_enumerable_own(context & cx, value of, Fn && visit) {
    // A REAL ARRAY TAKES THE STRAIGHT LINE. own_property_names would build one
    // std::string per element to answer a question the vector answers directly,
    // and `Object.values` of a large array is not a rare call.
    if (of.is_array()) {
        auto * arr = static_cast<array_object *>(of.as_heap());
        for (std::size_t i = 0; i < arr->length(); ++i) {
            if (!arr->element_attrs.empty()) {
                const std::uint8_t a = arr->element_attrs_at(static_cast<std::uint32_t>(i));
                if ((a & array_object::elem_hole) != 0 || (a & attr_enumerable) == 0) { continue; }
            }
            visit(std::to_string(i), cx.lookup_index(of, value::number(static_cast<double>(i))));
        }
        // ...and the named own properties after the indices (10.4.2.1).
        if (arr->named) {
            std::vector<std::string> keys;
            arr->named->each_own_enumerable_key([&](const std::string & k) {
                // An accessor ELEMENT's pair also lives here, under its index;
                // it was reported above.
                if (std::uint32_t at = 0; !object_object::array_index_key(k, at)) {
                    keys.push_back(k);
                }
            });
            for (const std::string & k : keys) { visit(k, cx.lookup_property(of, k)); }
        }
        return;
    }
    for (const std::string & key : own_property_names(cx, of, key_filter::strings)) {
        context::property_descriptor found;
        if (!cx.own_property(of, key, found) || !found.enumerable) { continue; }
        // AN OWN DATA PROPERTY'S [[Get]] IS ITS VALUE, and taking it from the
        // descriptor is not just an optimisation: context::lookup_property
        // answers `undefined` for a string's INDEX - `"ab"[0]` goes through
        // lookup_index, which is a different entry point - so reading back by
        // name would have made `Object.values('ab')` two undefineds. An
        // accessor is the one case that has to be a call.
        visit(key, found.is_accessor() ? cx.lookup_property(of, key) : found.held);
    }
}

// [[PreventExtensions]] with the proxy's trap consulted (10.5.4): a trap that
// answers false makes Object.freeze, seal and preventExtensions throw
// (20.1.2.6 step 2), where context::prevent_extensions goes straight to the
// target. FALSE means the TypeError is in flight.
[[nodiscard]] bool prevent_extensions_or_throw(context & cx, value target, const char * called) {
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        const value trap = cx.proxy_trap(target, "preventExtensions");
        if (trap.is_callable()) {
            const value args[1] = {p->target};
            const bool ok = context::truthy(cx.call(trap, args, p->handler));
            if (cx.throw_pending()) { return false; }
            if (!ok) {
                cx.throw_error("TypeError", std::string{called} +
                                                ": 'preventExtensions' on proxy returned false");
                return false;
            }
            return true;
        }
    }
    cx.prevent_extensions(target);
    return true;
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
        if (frozen) {
            arr->elements_writable = false;
            arr->length_writable = false;
        }
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
        for (accessor_entry & entry : fn->accessors.entries) {
            entry.attrs = static_cast<std::uint8_t>(entry.attrs & ~attr_configurable);
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
        return !arr->elements_configurable && (!frozen || !arr->elements_writable) &&
               (!frozen || !arr->length_writable);
    }
    // EVERY own key, symbols included: 7.3.16 walks OwnPropertyKeys, and a
    // symbol-keyed property that is still configurable makes the object neither
    // sealed nor frozen however the string keys look.
    for (const std::string & key : own_property_names(cx, target, key_filter::all)) {
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
    method(cx, object_proto, "hasOwnProperty", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        // ToPropertyKey FIRST (20.1.3.2 step 1), then ToObject - and the key is
        // ToString of the ARGUMENT, so `({}).hasOwnProperty()` asks about
        // "undefined" rather than about "". str_at answered "" for an absent
        // argument, which is a key nothing has.
        const std::string key = c.to_string(arg_at(a, 0));
        if (!object_coercible(c, self, "Object.prototype.hasOwnProperty")) {
            return value::boolean(false);
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
        // AN ARRAY, A FUNCTION, A NATIVE AND A STRING each have own properties
        // too - `a.length`, a sparse element, a NAMED property on an array
        // (which an arm here used to answer false about, so `verifyProperty`
        // reported every `arguments` object's property as not own), `f.name`,
        // `Array.prototype` and `"abc".length` among them. context::
        // has_own_property is the one answer all four tables share.
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
    method(cx, object_proto, "toString", 0, [](context & c, std::span<value>) {
        const value self = c.current_this();
        // Steps 1 and 2, before ToObject: these two are the only tags that name
        // something that is not an object at all.
        if (self.is_undefined()) { return c.string("[object Undefined]"); }
        if (self.is_null()) { return c.string("[object Null]"); }
        std::string_view tag = "Object";
        if (self.is_array()) {
            tag = "Array";
        } else if (self.is_callable()) {
            tag = "Function";
        } else if (self.is_string()) {
            tag = "String";
        } else if (self.is_number()) {
            tag = "Number";
        } else if (self.is_boolean()) {
            tag = "Boolean";
        } else if (self.is_kind(heap_kind::symbol)) {
            tag = "Symbol";
        } else if (value * slot = primitive_slot(self); slot != nullptr) {
            // A WRAPPER answers for what it wraps (20.1.3.6 steps 6-11); a
            // Symbol or BigInt wrapper reaches its @@toStringTag below.
            tag = slot->is_number()    ? "Number"
                  : slot->is_string()  ? "String"
                  : slot->is_boolean() ? "Boolean"
                                       : "Object";
            // [[ErrorData]] and [[RegExpMatcher]] are slots this engine does not
            // have: an error and a regular expression are both ordinary objects
            // here, distinguishable only by the prototype they were built on.
            // That is exact for everything the constructors make, and it also
            // answers "Error" for `Object.create(TypeError.prototype)`, which a
            // real slot would not. The alternative is a marker property, which
            // a page could see.
        } else if (inherits_from(c, self, c.prototype(context::proto_kind::error))) {
            tag = "Error";
        } else if (inherits_from(c, self, c.prototype(context::proto_kind::regexp))) {
            tag = "RegExp";
        } else if (self.is_object() && static_cast<object_object *>(self.as_heap())->find("__ms")) {
            tag = "Date"; // [[DateValue]] is the `__ms` slot install_date defines
        } else if (self.is_object() && self.as_heap() == c.prototype(context::proto_kind::number)) {
            tag = "Number"; // 21.1.3: Number.prototype has [[NumberData]] +0
        } else if (self.is_object() && self.as_heap() == c.prototype(context::proto_kind::string)) {
            tag = "String"; // 22.1.3: [[StringData]] ""
        } else if (self.is_object() &&
                   self.as_heap() == c.prototype(context::proto_kind::boolean)) {
            tag = "Boolean"; // 20.3.3: [[BooleanData]] false
        }
        // 20.1.3.6 step 15: a STRING @@toStringTag replaces the built-in tag,
        // and anything else is ignored rather than stringified. This is the
        // extension point `class X { get [Symbol.toStringTag]() {...} }` uses,
        // and the reason a Map prints as "[object Map]" without Object knowing
        // what a Map is.
        const value custom = c.lookup_property(self, "@@toStringTag");
        if (custom.is_string()) { return c.string("[object " + c.to_string(custom) + "]"); }
        return c.string("[object " + std::string{tag} + "]");
    });
    method(cx, object_proto, "valueOf", 0, [](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!object_coercible(c, self, "Object.prototype.valueOf")) { return value::undefined(); }
        // 20.1.3.7: ToObject(this value), so a primitive receiver answers its
        // WRAPPER - `typeof Object.prototype.valueOf.call(true)` is "object".
        return detail::box_primitive(c, self);
    });
    // 20.1.3.5: Invoke(O, "toString"), NOT a second copy of the tag logic. A
    // receiver that overrides `toString` must be seen through this too, which
    // is the whole point of the method existing.
    method(cx, object_proto, "toLocaleString", 0, [](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!object_coercible(c, self, "Object.prototype.toLocaleString")) {
            return value::undefined();
        }
        const value fn = c.lookup_property(self, "toString");
        if (!fn.is_callable()) {
            c.throw_error("TypeError", "toString is not a function");
            return value::undefined();
        }
        return c.call(fn, std::span<const value>{}, self);
    });
    method(cx, object_proto, "isPrototypeOf", 1, [](context & c, std::span<value> a) {
        // 20.1.3.3 step 1: a non-object argument is FALSE before ToObject(this)
        // gets to refuse a null receiver.
        const value of = arg_at(a, 0);
        if (!of.is_object_like()) { return value::boolean(false); }
        const value self = c.current_this();
        if (!object_coercible(c, self, "Object.prototype.isPrototypeOf")) {
            return value::boolean(false);
        }
        value walk = prototype_of(c, of);
        for (int depth = 0; depth < 64 && walk.is_heap(); ++depth) {
            if (self.is_heap() && walk.as_heap() == self.as_heap()) { return value::boolean(true); }
            walk = prototype_of(c, walk);
        }
        return value::boolean(false);
    });
    method(cx, object_proto, "propertyIsEnumerable", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        const std::string key = c.to_string(arg_at(a, 0));
        if (!object_coercible(c, self, "Object.prototype.propertyIsEnumerable")) {
            return value::boolean(false);
        }
        context::property_descriptor found;
        if (!c.own_property(self, key, found)) { return value::boolean(false); }
        return value::boolean(found.enumerable);
    });
    // --- B.2.2, THE FOUR __*etter__ METHODS ------------------------------
    //
    // Annex B rather than the main body, and normative for a web browser: they
    // are the pre-ES5 way to define and read an accessor and they are still in
    // shipped code. All four were absent, which is 54 files in
    // built-ins/Object/prototype. Each is ToObject(this) followed by
    // ToPropertyKey - in that order, which is what `this-non-obj.js` asserts by
    // counting the key's `toString` calls after the throw.
    const auto define_accessor = [](bool getter) {
        return [getter](context & c, std::span<value> a) {
            const char * const called =
                getter ? "Object.prototype.__defineGetter__" : "Object.prototype.__defineSetter__";
            const value self = c.current_this();
            if (!object_coercible(c, self, called)) { return value::undefined(); }
            const value fn = arg_at(a, 1);
            if (!fn.is_callable()) {
                c.throw_error("TypeError", std::string{called} + ": Expecting function");
                return value::undefined();
            }
            context::property_descriptor wanted;
            (getter ? wanted.has_get : wanted.has_set) = true;
            (getter ? wanted.getter : wanted.setter) = fn;
            // B.2.2.2 step 3: { [[Enumerable]]: true, [[Configurable]]: true },
            // which is NOT defineProperty's all-false default.
            wanted.has_enumerable = wanted.has_configurable = true;
            wanted.enumerable = wanted.configurable = true;
            if (!c.define_own_property(self, c.to_string(arg_at(a, 0)), wanted)) {
                c.throw_error("TypeError", "Cannot redefine property");
            }
            return value::undefined();
        };
    };
    method(cx, object_proto, "__defineGetter__", 2, define_accessor(true));
    method(cx, object_proto, "__defineSetter__", 2, define_accessor(false));
    const auto lookup_accessor = [](bool getter) {
        return [getter](context & c, std::span<value> a) {
            const value self = c.current_this();
            if (!object_coercible(c, self,
                                  getter ? "Object.prototype.__lookupGetter__"
                                         : "Object.prototype.__lookupSetter__")) {
                return value::undefined();
            }
            const std::string key = c.to_string(arg_at(a, 0));
            // THE WHOLE CHAIN, own property first: B.2.2.4 walks
            // [[GetPrototypeOf]] and stops at the first level that HAS the
            // property, so a data property SHADOWS an inherited accessor and
            // the answer is undefined rather than the one further up.
            value walk = self;
            for (int depth = 0; depth < 64 && walk.is_heap(); ++depth) {
                context::property_descriptor found;
                if (c.own_property(walk, key, found)) {
                    if (!found.is_accessor()) { return value::undefined(); }
                    return getter ? found.getter : found.setter;
                }
                walk = prototype_of(c, walk);
            }
            return value::undefined();
        };
    };
    method(cx, object_proto, "__lookupGetter__", 1, lookup_accessor(true));
    method(cx, object_proto, "__lookupSetter__", 1, lookup_accessor(false));
    // B.2.2.1 `Object.prototype.__proto__` - an ACCESSOR whose getter is
    // [[GetPrototypeOf]] and whose setter is [[SetPrototypeOf]], both over the
    // same answers Object.getPrototypeOf and Object.setPrototypeOf give. It was
    // absent, so `x.__proto__.hasOwnProperty(...)` - which is how a WPT page
    // asks whether an interface prototype carries a property - read `undefined`
    // and called it. Non-enumerable and configurable, as every Object.prototype
    // member is.
    object_proto->define_accessor(
        "__proto__",
        detail::accessor_fn(cx, "get __proto__",
                            [](context & c, std::span<value>) {
                                const value self = c.current_this();
                                if (!object_coercible(c, self, "Object.prototype.__proto__")) {
                                    return value::undefined();
                                }
                                return prototype_of(c, self);
                            }),
        detail::accessor_fn(cx, "set __proto__",
                            [](context & c, std::span<value> a) {
                                const value self = c.current_this();
                                if (!object_coercible(c, self, "Object.prototype.__proto__")) {
                                    return value::undefined();
                                }
                                // B.2.2.1.2 steps 2-3: a non-object prototype and a primitive
                                // receiver are each a silent no-op, NOT the TypeError
                                // Object.setPrototypeOf raises - and an object literal's
                                // `__proto__: 5` relies on the first.
                                const value proto = arg_at(a, 0);
                                if (!proto.is_object_like() && !proto.is_null()) {
                                    return value::undefined();
                                }
                                // B.2.2.1.2 step 5: a refused [[SetPrototypeOf]] IS an error here.
                                if (!set_prototype_of(c, self, proto)) {
                                    c.throw_error(
                                        "TypeError",
                                        "Cyclic __proto__ value or object is not extensible");
                                }
                                return value::undefined();
                            }),
        attr_configurable);
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
        // 20.1.1.1: an object passes through, null and undefined make a fresh
        // object, and a primitive is BOXED (ToObject) - see
        // detail::wrap_primitive.
        const value v = arg_at(a, 0);
        if (v.is_object_like()) { return v; }
        if (v.is_nullish()) { return c.make_object(); }
        return detail::box_primitive(c, v);
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
    link_constructor(cx, object_proto, "Object", 1, value::object(object_ctor));
    method(cx, object_ctor, "hasOwn", 2, [](context & c, std::span<value> a) {
        if (!object_coercible(c, arg_at(a, 0), "Object.hasOwn")) { return value::boolean(false); }
        return value::boolean(c.has_own_property(a[0], c.to_string(arg_at(a, 1))));
    });

    // `Object.defineProperty(o, key, descriptor)` - 51 uses in p5.js, and the
    // reason the object model grew accessors at all. A descriptor is either
    // data (`value`) or accessor (`get`/`set`); the two are the same property
    // described two ways, so defining one removes the other.
    method(cx, object_ctor, "defineProperty", 3, [](context & c, std::span<value> a) {
        // A NON-OBJECT TARGET IS A TypeError (19.1.2.4 step 1) and so is a
        // non-object descriptor (10.1.6.3 via ToPropertyDescriptor). Returning
        // the argument instead is how `Object.defineProperty(undefined, ...)`
        // looked like it had worked.
        if (!arg_at(a, 0).is_object_like()) {
            c.throw_error("TypeError", "Object.defineProperty called on non-object");
            return value::undefined();
        }
        // ToPropertyKey BEFORE ToPropertyDescriptor (20.1.2.4 steps 2 and 3),
        // and ONCE: it was run twice - a second time to build the error
        // message - so a key object's `toString` ran twice for one call.
        const std::string key = c.to_string(arg_at(a, 1));
        // A FUNCTION AND AN ARRAY ARE OBJECTS. `is_object()` is true only of
        // object_object here, so `Object.defineProperty(o, k, funObj)` - which
        // 8.10.5 permits and 24 of test262's files in this directory use -
        // was refused as "not an object".
        if (!arg_at(a, 2).is_object_like()) {
            c.throw_error("TypeError", "Property description must be an object");
            return value::undefined();
        }
        const context::property_descriptor wanted = c.to_property_descriptor(a[2]);
        if (!valid_descriptor(c, wanted)) { return value::undefined(); }
        if (!c.define_own_property(a[0], key, wanted)) {
            c.throw_error("TypeError", "Cannot redefine property: " + key);
            return value::undefined();
        }
        return a[0];
    });
    method(cx, object_ctor, "defineProperties", 2, [](context & c, std::span<value> a) {
        if (!arg_at(a, 0).is_object_like()) {
            c.throw_error("TypeError", "Object.defineProperties called on non-object");
            return value::undefined();
        }
        if (!define_properties(c, a[0], arg_at(a, 1), "Object.defineProperties")) {
            return value::undefined();
        }
        return a[0];
    });
    method(cx, object_ctor, "getOwnPropertyDescriptor", 2, [](context & c, std::span<value> a) {
        // ToObject, 20.1.2.8 step 1: a primitive HAS own properties here
        // (`'ab'` has 0, 1 and length) and null is the only refusal.
        if (!object_coercible(c, arg_at(a, 0), "Object.getOwnPropertyDescriptor")) {
            return value::undefined();
        }
        context::property_descriptor found;
        if (!c.own_property(a[0], c.to_string(arg_at(a, 1)), found)) { return value::undefined(); }
        return value::object(descriptor_object(c, found));
    });
    // `Object.create(proto, properties)`. A real chain has existed since
    // `extends`; what was missing was any way for a page to reach it, and then
    // the SECOND ARGUMENT, which was accepted and ignored - so
    // `Object.create({}, {x: {value: 1}})` made an empty object and said
    // nothing. p5.js uses create 19 times; test262 devotes 304 files to that
    // argument alone.
    method(cx, object_ctor, "create", 2, [](context & c, std::span<value> a) {
        // 20.1.2.2 step 1: the prototype must be an Object or null. A number or
        // a string is a TypeError rather than a silent Object.prototype.
        const value proto = arg_at(a, 0);
        if (!proto.is_object_like() && !proto.is_null()) {
            c.throw_error("TypeError", "Object prototype may only be an Object or null");
            return value::undefined();
        }
        object_object * out = new_table(c);
        // Any object - a function or an array too, which lookup_property now
        // walks through (see its object arm). null is the EXPLICIT null, which
        // object_object::prototype spells as undefined.
        out->prototype = proto.is_object_like() ? proto : value::undefined();
        const value made = value::object(out);
        // A ROOT WHILE THE DESCRIPTORS RUN. Each one is read through [[Get]],
        // which can call a page's getter, which can collect - and `out` lives
        // only in a C++ local until this returns.
        const context::rooted keep{c, made};
        if (has_index(a, 1) && !define_properties(c, made, a[1], "Object.create")) {
            return value::undefined();
        }
        return made;
    });
    // 20.1.2.12: ToObject(O), then O.[[GetPrototypeOf]]() - so a primitive
    // answers its wrapper's prototype and only null and undefined refuse. See
    // `prototype_of` above for what a plain object's answer now is and why.
    method(cx, object_ctor, "getPrototypeOf", 1, [](context & c, std::span<value> a) {
        if (!object_coercible(c, arg_at(a, 0), "Object.getPrototypeOf")) {
            return value::undefined();
        }
        return prototype_of(c, a[0]);
    });
    method(cx, object_ctor, "setPrototypeOf", 2, [](context & c, std::span<value> a) {
        const value of = arg_at(a, 0);
        if (!object_coercible(c, of, "Object.setPrototypeOf")) { return value::undefined(); }
        // 20.1.2.22 step 2: the prototype must be an Object or null. Anything
        // else is a TypeError, not a silent unlinking - and a NON-OBJECT
        // receiver is returned unchanged rather than refused, because
        // [[SetPrototypeOf]] on a primitive's wrapper is a no-op that succeeds.
        const value proto = arg_at(a, 1);
        if (!proto.is_object_like() && !proto.is_null()) {
            c.throw_error("TypeError", "Object prototype may only be an Object or null");
            return value::undefined();
        }
        if (!set_prototype_of(c, of, proto)) {
            c.throw_error("TypeError",
                          "Object.setPrototypeOf: cyclic prototype or object is not extensible");
            return value::undefined();
        }
        return of;
    });
    // NAMES, so string keys only - Reflect.ownKeys is the one that reports
    // symbols as well, and it keeps the unfiltered walk.
    method(cx, object_ctor, "getOwnPropertyNames", 1, [](context & c, std::span<value> a) {
        if (!object_coercible(c, arg_at(a, 0), "Object.getOwnPropertyNames")) {
            return value::undefined();
        }
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        for (const std::string & key : own_property_names(c, a[0])) {
            result->items.push_back(c.string(key));
        }
        return out;
    });
    // ...AND THE OTHER HALF OF OwnPropertyKeys, 20.1.2.11, which did not exist
    // at all - so the guarded `if (Object.getOwnPropertySymbols)` every spread
    // helper opens with took the other branch. A symbol key here is a string
    // with a reserved prefix, so the two are one walk with two filters rather
    // than two tables.
    //
    // THE SYMBOL IT HANDS BACK IS NOT `===` TO THE ONE THAT DEFINED THE
    // PROPERTY, and that is stated rather than hidden. A property table keeps
    // only the KEY STRING, so the original symbol_object is not recoverable
    // from it; what comes back has the same key and the same description, works
    // as a property key everywhere (every access goes through to_string of the
    // key), and compares false under `===`, which compares symbols by POINTER.
    // One line in value.hpp would close it - `strict_equals` comparing two
    // symbols by `key`, which is what symbol_object's own comment says identity
    // IS here - and that file belongs to the object model, not to this one.
    //
    // A WELL-KNOWN symbol key is `@@iterator`, not `@@sym:N:...`, so it is not
    // reported here and IS reported by getOwnPropertyNames. That is
    // value.hpp's `each_own_enumerable_key` filter, which for-in, JSON.stringify
    // and Object.keys all share; making this walk disagree with those four
    // would be worse than the gap.
    method(cx, object_ctor, "getOwnPropertySymbols", 1, [](context & c, std::span<value> a) {
        if (!object_coercible(c, arg_at(a, 0), "Object.getOwnPropertySymbols")) {
            return value::undefined();
        }
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        for (const std::string & key : own_property_names(c, a[0], key_filter::symbols)) {
            result->items.push_back(detail::key_value(c, key));
        }
        return out;
    });
    method(cx, object_ctor, "getOwnPropertyDescriptors", 1, [](context & c, std::span<value> a) {
        if (!object_coercible(c, arg_at(a, 0), "Object.getOwnPropertyDescriptors")) {
            return value::undefined();
        }
        object_object * out = new_table(c);
        const value from = a[0];
        const context::rooted keep{c, value::object(out)};
        // EVERY own key, symbols included: 20.1.2.9 walks OwnPropertyKeys, and
        // a descriptor map that silently drops the symbol-keyed properties is
        // the wrong input to `Object.defineProperties`, which is what the pair
        // is nearly always used for.
        for (const std::string & key : own_property_names(c, from, key_filter::all)) {
            context::property_descriptor found;
            if (c.own_property(from, key, found)) {
                out->set(key, value::object(descriptor_object(c, found)));
            }
        }
        return value::object(out);
    });
    // 20.1.2.7. RequireObjectCoercible first (step 1), then each entry read
    // through [[Get]] of "0" and "1" - so an entry may be any object with those
    // two, not only an Array, and an entry that is NOT an object is a TypeError
    // rather than a silently skipped element.
    // 20.1.2.7 Object.fromEntries: AddEntriesFromIterable over the ITERATOR
    // PROTOCOL - GetIterator refuses a non-iterable, an entry that is not an
    // object is a TypeError that CLOSES the iterator (step 4.d), a key or
    // value read that throws closes it too, and a `next` that throws does not.
    // Each entry lands with CreateDataProperty. `iterable_values` would have
    // drained the source first and lost every one of those orderings.
    method(cx, object_ctor, "fromEntries", 1, [](context & c, std::span<value> a) {
        if (!object_coercible(c, arg_at(a, 0), "Object.fromEntries")) { return value::undefined(); }
        object_object * out = new_table(c);
        const value made = value::object(out);
        const context::rooted keep{c, made};
        const value iterator = c.get_iterator(a[0]);
        if (c.throw_pending() || !iterator.is_object()) { return value::undefined(); }
        const context::rooted keep_iterator{c, iterator};
        const value next = c.lookup_property(iterator, "next");
        if (c.throw_pending()) { return value::undefined(); }
        const auto close = [&] {
            const value ret = c.lookup_property(iterator, "return");
            if (ret.is_callable()) { (void)c.call(ret, std::span<const value>{}, iterator); }
        };
        for (;;) {
            bool done = false;
            const value entry = c.iterator_step(iterator, next, done);
            if (c.throw_pending()) { return value::undefined(); }
            if (done) { break; }
            if (!entry.is_object_like()) {
                close();
                c.throw_error("TypeError", "Iterator value is not an entry object");
                return value::undefined();
            }
            const context::rooted keep_entry{c, entry};
            const value key = c.lookup_index(entry, value::number(0));
            if (c.throw_pending()) {
                close();
                return value::undefined();
            }
            const context::rooted keep_key{c, key};
            const value held = c.lookup_index(entry, value::number(1));
            if (c.throw_pending()) {
                close();
                return value::undefined();
            }
            const context::rooted keep_held{c, held};
            const std::string name = c.to_string(key);
            if (c.throw_pending()) {
                close();
                return value::undefined();
            }
            out->set(name, held);
        }
        return made;
    });
    // --- INTEGRITY LEVELS, which used to be theatre ----------------------
    //
    // `freeze` returned its argument and did nothing; `isFrozen` answered false
    // about everything, including an object it had just been asked to freeze.
    // Both now do what 7.3.15/7.3.16 say: seal clears [[Configurable]] on every
    // own property and [[Extensible]] on the object; freeze clears
    // [[Writable]] as well, except on an accessor, which has none.
    method(cx, object_ctor, "freeze", 1, [](context & c, std::span<value> a) {
        if (!prevent_extensions_or_throw(c, arg_at(a, 0), "Object.freeze")) {
            return value::undefined();
        }
        set_integrity(c, arg_at(a, 0), true);
        return arg_at(a, 0);
    });
    method(cx, object_ctor, "seal", 1, [](context & c, std::span<value> a) {
        if (!prevent_extensions_or_throw(c, arg_at(a, 0), "Object.seal")) {
            return value::undefined();
        }
        set_integrity(c, arg_at(a, 0), false);
        return arg_at(a, 0);
    });
    method(cx, object_ctor, "preventExtensions", 1, [](context & c, std::span<value> a) {
        if (!prevent_extensions_or_throw(c, arg_at(a, 0), "Object.preventExtensions")) {
            return value::undefined();
        }
        return arg_at(a, 0);
    });
    method(cx, object_ctor, "isFrozen", 1, [](context & c, std::span<value> a) {
        return value::boolean(test_integrity(c, arg_at(a, 0), true));
    });
    method(cx, object_ctor, "isSealed", 1, [](context & c, std::span<value> a) {
        return value::boolean(test_integrity(c, arg_at(a, 0), false));
    });
    method(cx, object_ctor, "isExtensible", 1, [](context & c, std::span<value> a) {
        return value::boolean(c.is_extensible(arg_at(a, 0)));
    });
    // SameValue, 7.2.11 - which is `===` except that it separates the two
    // zeros and calls NaN equal to itself. Those are exactly the two questions
    // `===` cannot answer, which is why every test in this directory that cares
    // about -0 had to spell it `1/x === -Infinity` instead.
    method(cx, object_ctor, "is", 2, [](context &, std::span<value> a) {
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
    // 20.1.2.17/20.1.2.23/20.1.2.6, all three EnumerableOwnProperties over
    // ToObject(O). They opened with `is_object()` and answered `[]` for
    // everything else, so `Object.keys([1,2])` was empty, `Object.values('ab')`
    // was empty and `Object.entries(f)` was empty - and `Object.keys(null)`
    // was empty rather than the TypeError step 1 requires.
    //
    // An accessor IS a property, and definition order is observable. STRING
    // keys only - a symbol-keyed property is invisible to all three.
    method(cx, object_ctor, "keys", 1, [](context & c, std::span<value> a) {
        if (!object_coercible(c, arg_at(a, 0), "Object.keys")) { return value::undefined(); }
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        const context::rooted keep{c, out};
        each_enumerable_own(
            c, a[0], [&](const std::string & k, value) { result->items.push_back(c.string(k)); });
        return out;
    });
    method(cx, object_ctor, "values", 1, [](context & c, std::span<value> a) {
        if (!object_coercible(c, arg_at(a, 0), "Object.values")) { return value::undefined(); }
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        const context::rooted keep{c, out};
        each_enumerable_own(
            c, a[0], [&](const std::string &, value held) { result->items.push_back(held); });
        return out;
    });
    method(cx, object_ctor, "entries", 1, [](context & c, std::span<value> a) {
        if (!object_coercible(c, arg_at(a, 0), "Object.entries")) { return value::undefined(); }
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        const context::rooted keep{c, out};
        each_enumerable_own(c, a[0], [&](const std::string & key, value held) {
            value pair = c.make_array();
            auto * entry = static_cast<array_object *>(pair.as_heap());
            entry->items.push_back(c.string(key));
            entry->items.push_back(held);
            result->items.push_back(pair);
        });
        return out;
    });
    method(cx, object_ctor, "assign", 2, [](context & c, std::span<value> a) {
        const value target = arg_at(a, 0);
        // 20.1.2.1 step 1 is ToObject(target), so null and undefined throw. A
        // primitive target is returned as itself here: there is no wrapper to
        // hand back and the writes have nowhere to land.
        if (!object_coercible(c, target, "Object.assign")) { return value::undefined(); }
        if (!target.is_object_like()) { return detail::box_primitive(c, target); }
        for (std::size_t i = 1; i < a.size(); ++i) {
            // Step 4.a: a null or undefined source is SKIPPED rather than an
            // error, which is what makes `Object.assign({}, maybe)` idiomatic.
            if (a[i].is_nullish()) { continue; }
            // A SNAPSHOT, because storing into the target can run a setter that
            // mutates the source; ENUMERABLE own keys only (7.3.24 step 5);
            // and through store_property rather than set(), because
            // Object.assign is specified as [[Set]] and a frozen target must
            // therefore reject the write.
            std::vector<std::pair<std::string, value>> entries;
            // SYMBOL KEYS INCLUDED: 7.3.24 walks OwnPropertyKeys, which reports
            // them - the same exception object spread needs - and any SOURCE,
            // so `Object.assign({}, 'ab')` copies the two characters.
            for (const std::string & key : own_property_names(c, a[i], key_filter::all)) {
                context::property_descriptor found;
                if (!c.own_property(a[i], key, found) || !found.enumerable) { continue; }
                entries.emplace_back(key, found.is_accessor() ? c.lookup_property(a[i], key)
                                                              : found.held);
            }
            // THE SNAPSHOT IS A ROOT. A [[Set]] on the target can run a page's
            // setter, which can allocate, which can collect - and until it is
            // stored, a copied value's only reference is this vector, which no
            // root in GCRoots.def reaches.
            std::vector<value> held;
            held.reserve(entries.size());
            for (const auto & entry : entries) { held.push_back(entry.second); }
            const context::rooted_values keep{c, held};
            // Set(to, key, value, TRUE) - 20.1.2.1 step 4.c.iii: a write the
            // target refuses is a TypeError, sloppy or not.
            for (const auto & [key, item] : entries) {
                c.clear_store_rejected();
                c.store_property(target, key, item);
                c.strict_store_check(key);
                if (c.throw_pending()) { return target; }
            }
        }
        return target;
    });
    // 20.1.2.9 Object.groupBy - GroupBy with property keys: every value of
    // the iterable goes to the callback with its index, and the answer is an
    // object of arrays keyed by ToPropertyKey of what it returned, in first-
    // seen order. (The specification gives it a null prototype; this engine
    // cannot make one - see prototype_of.)
    method(cx, object_ctor, "groupBy", 2, [](context & c, std::span<value> a) {
        if (!object_coercible(c, arg_at(a, 0), "Object.groupBy")) { return value::undefined(); }
        const value callback = arg_at(a, 1);
        if (!callback.is_callable()) {
            c.throw_error("TypeError", "Object.groupBy: callback is not a function");
            return value::undefined();
        }
        // GetIterator (step 4 of GroupBy): an object without a callable
        // @@iterator is a TypeError, not an empty result.
        if (a[0].is_object_like() && !a[0].is_array() &&
            !c.lookup_property(a[0], "@@iterator").is_callable()) {
            if (!c.throw_pending()) { c.throw_error("TypeError", "Object.groupBy: not iterable"); }
            return value::undefined();
        }
        const value items = c.iterable_values(a[0]);
        if (c.throw_pending() || !items.is_array()) { return value::undefined(); }
        const context::rooted keep_items{c, items};
        object_object * out = new_table(c);
        const value made = value::object(out);
        const context::rooted keep{c, made};
        // A COPY, ROOTED: the callback may empty the very array `items` is.
        const std::vector<value> snapshot = static_cast<array_object *>(items.as_heap())->items;
        const context::rooted_values keep_snapshot{c, snapshot};
        for (std::size_t i = 0; i < snapshot.size(); ++i) {
            const value args[2] = {snapshot[i], value::number(static_cast<double>(i))};
            const value key = c.call(callback, args);
            if (c.throw_pending()) { return value::undefined(); }
            const std::string name = c.to_string(key);
            value * group = out->find(name);
            if (group == nullptr) {
                out->set(name, c.make_array());
                group = out->find(name);
            }
            static_cast<array_object *>(group->as_heap())->items.push_back(snapshot[i]);
        }
        return made;
    });
    cx.define_global("Object", value::object(object_ctor));
}

} // namespace ctbrowser::script::builtins_detail
