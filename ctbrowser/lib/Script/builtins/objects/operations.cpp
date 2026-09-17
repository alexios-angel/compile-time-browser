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

namespace ctbrowser::script {
// Defined in vm/objects/lookup.cpp; see the note there.
object_object * typed_array_prototype(context & cx, element_kind kind);
} // namespace ctbrowser::script

namespace ctbrowser::script::detail {

namespace {

[[nodiscard]] inline bool wanted_key(key_filter which, const std::string & key) {
    if (is_private_key(key)) { return false; } // a private name is not a property key
    const bool symbol = key.starts_with("@@"); // "@@sym:<n>:" or a well-known "@@iterator"
    return which == key_filter::all || (which == key_filter::symbols) == symbol;
}

} // namespace

// See context::key_value.
[[nodiscard]] value key_value(context & cx, const std::string & key) {
    return cx.key_value(key);
}

// EVERY OWN KEY OF ANY VALUE, including the synthesised ones. An array
// has `length` and its indices, a string has `length` and its characters, a
// function has `name`, `length` and `prototype` - and getOwnPropertyNames
// reported none of them because it only knew about object_object.
[[nodiscard]] std::vector<std::string> own_property_names(context & cx, value of,
                                                          key_filter which) {
    std::vector<std::string> out;
    if (of.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(of.as_heap());
        const value trap = cx.proxy_trap(of, "ownKeys");
        if (cx.throw_pending()) { return out; }
        if (!trap.is_callable()) { return own_property_names(cx, p->target, which); }
        // 10.5.11 [[OwnPropertyKeys]]: the trap's list, each a String or a
        // Symbol and none twice (steps 7-8), then the invariants against the
        // target - every non-configurable key is reported, and a
        // non-extensible target's keys are reported exactly (steps 9-23).
        const value args[1] = {p->target};
        const value listed = cx.call(trap, args, p->handler);
        if (cx.throw_pending()) { return out; }
        if (!listed.is_object_like()) {
            cx.throw_error("TypeError", "ownKeys trap result must be an object");
            return out;
        }
        std::vector<std::string> keys;
        const double n = array_like_length(cx, listed);
        if (cx.throw_pending()) { return out; }
        for (double i = 0; i < n; i += 1.0) {
            const value k = element_at(cx, listed, i);
            if (cx.throw_pending()) { return out; }
            std::string key;
            if (k.is_string()) {
                key = static_cast<string_object *>(k.as_heap())->text;
            } else if (k.is_kind(heap_kind::symbol)) {
                key = static_cast<symbol_object *>(k.as_heap())->key;
            } else {
                cx.throw_error("TypeError",
                               "ownKeys trap result must contain only strings and symbols");
                return out;
            }
            if (std::find(keys.begin(), keys.end(), key) != keys.end()) {
                cx.throw_error("TypeError", "ownKeys trap result contains duplicate entries");
                return out;
            }
            keys.push_back(std::move(key));
        }
        const bool extensible = cx.is_extensible(p->target);
        std::vector<std::string> target_keys = own_property_names(cx, p->target, key_filter::all);
        std::size_t matched = 0;
        for (const std::string & key : target_keys) {
            const bool reported = std::find(keys.begin(), keys.end(), key) != keys.end();
            if (reported) { ++matched; }
            if (reported && extensible) { continue; }
            context::property_descriptor found;
            const bool configurable = !cx.own_property(p->target, key, found) || found.configurable;
            if (!reported && (!configurable || !extensible)) {
                cx.throw_error("TypeError", "ownKeys trap result must include '" + key + "'");
                return out;
            }
        }
        if (!extensible && matched != keys.size()) {
            cx.throw_error("TypeError", "ownKeys trap result must not add keys to a "
                                        "non-extensible target");
            return out;
        }
        for (const std::string & key : keys) {
            if (wanted_key(which, key)) { out.push_back(key); }
        }
        return out;
    }
    if (of.is_object()) {
        // A String wrapper's indices and `length` come first (10.4.3.3), as
        // they do for the primitive below.
        if (value * slot = primitive_slot(of); slot != nullptr && slot->is_string()) {
            if (which != key_filter::symbols) {
                const std::size_t n = static_cast<string_object *>(slot->as_heap())->text.size();
                for (std::size_t i = 0; i < n; ++i) { out.push_back(std::to_string(i)); }
                out.emplace_back("length");
            }
        }
        static_cast<object_object *>(of.as_heap())->each_own_key([&](const std::string & k) {
            if (wanted_key(which, k)) { out.push_back(k); }
        });
        // 10.1.11.1 OrdinaryOwnPropertyKeys: integer keys ascending (the walk's
        // own order), then strings, then SYMBOLS, each in creation order.
        if (which == key_filter::all) {
            std::stable_partition(out.begin(), out.end(),
                                  [](const std::string & k) { return !k.starts_with("@@"); });
        }
        return out;
    }
    // Nothing but an object_object can hold a symbol key: the other three
    // tables are only ever written by the engine itself.
    if (which == key_filter::symbols) { return out; }
    if (of.is_array()) {
        auto * arr = static_cast<array_object *>(of.as_heap());
        for (std::size_t i = 0; i < arr->length(); ++i) {
            if (!arr->is_hole(static_cast<std::uint32_t>(i))) { out.push_back(std::to_string(i)); }
        }
        for (const auto & [at, held] : arr->sparse) {
            (void)held;
            out.push_back(std::to_string(at));
        }
        // A typed array has NO own `length` (23.2.3.19 is a prototype getter).
        if (arr->elements == element_kind::none) { out.emplace_back("length"); }
        // Then the named own properties - see array_object::named.
        if (arr->named) {
            arr->named->each_own_key([&](const std::string & k) {
                // An accessor ELEMENT's pair also lives here, under its
                // index; it was reported above.
                std::uint32_t at = 0;
                if (wanted_key(which, k) && !object_object::array_index_key(k, at)) {
                    out.push_back(k);
                }
            });
        }
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
            if (!wanted_key(which, key)) { continue; } // a bound function's private target slot
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
        // 10.5.1 [[GetPrototypeOf]] of a proxy: the `getPrototypeOf` trap,
        // checked against a non-extensible target; a revoked proxy is a
        // TypeError. GetMethod reads through the handler's chain, which
        // context::proxy_trap (own keys only) does not.
        auto * p = static_cast<proxy_object *>(of.as_heap());
        if (!p->handler.is_object_like()) {
            cx.throw_error("TypeError",
                           "Cannot perform 'getPrototypeOf' on a proxy that has been revoked");
            return value::null();
        }
        const value trap = cx.lookup_property(p->handler, "getPrototypeOf");
        if (cx.throw_pending()) { return value::null(); }
        if (trap.is_nullish()) { return prototype_of(cx, p->target); }
        if (!trap.is_callable()) {
            cx.throw_error("TypeError", "proxy trap 'getPrototypeOf' is not a function");
            return value::null();
        }
        const value args[1] = {p->target};
        const value answered = cx.call(trap, args, p->handler);
        if (cx.throw_pending()) { return value::null(); }
        if (!answered.is_object_like() && !answered.is_null()) {
            cx.throw_error("TypeError", "'getPrototypeOf' on proxy: trap returned neither object "
                                        "nor null");
            return value::null();
        }
        if (cx.is_extensible(p->target)) { return answered; }
        const value real = prototype_of(cx, p->target);
        if (cx.throw_pending()) { return value::null(); }
        if (!real.strict_equals(answered)) {
            cx.throw_error("TypeError", "'getPrototypeOf' on proxy: proxy target is "
                                        "non-extensible but the trap did not return its "
                                        "actual prototype");
            return value::null();
        }
        return answered;
    }
    const auto table = [&](context::proto_kind kind) {
        object_object * found = cx.prototype(kind);
        return found == nullptr ? value::null() : value::object(found);
    };
    if (of.is_object()) {
        auto * obj = static_cast<object_object *>(of.as_heap());
        if (obj->prototype.is_object_like()) { return obj->prototype; }
        // An EXPLICIT null (object_object::prototype says how the two nulls
        // are told apart); and Object.prototype's own [[Prototype]] is null.
        if (obj->prototype.is_undefined()) { return value::null(); }
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
        // A generator or async function's is its own intrinsic (27.3.3 etc.).
        const context::proto_kind own = context::function_proto_kind(of);
        if (cx.prototype(own) != nullptr) { return table(own); }
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
    if (of.is_array()) {
        // Its own, when it has one (array_object::prototype: a subclass
        // instance's, or one a page set); undefined there is an explicit null.
        auto * arr = static_cast<array_object *>(of.as_heap());
        if (arr->prototype.is_object_like()) { return arr->prototype; }
        if (arr->prototype.is_undefined()) { return value::null(); }
        // A typed array's is its kind's own prototype object (23.2.7).
        if (object_object * own = typed_array_prototype(cx, arr->elements)) {
            return value::object(own);
        }
        return table(context::proto_kind::array);
    }
    if (of.is_kind(heap_kind::symbol)) { return table(context::proto_kind::symbol); }
    if (of.is_kind(heap_kind::bigint)) { return table(context::proto_kind::bigint); }
    return value::null();
}

// [[SetPrototypeOf]] over the three tables that carry a link - shared by
// Object.setPrototypeOf, Reflect.setPrototypeOf and the `__proto__` setter.
// A primitive receiver is a no-op that succeeds. FALSE is 10.1.2.1's refusal:
// a non-extensible object keeps the prototype it has, a chain may not be made
// cyclic, and Object.prototype's own [[Prototype]] is immutable (10.4.7).
[[nodiscard]] bool set_prototype_of(context & cx, value of, value proto) {
    if (!of.is_object_like()) { return true; }
    if (of.is_kind(heap_kind::proxy)) {
        // 10.5.2 [[SetPrototypeOf]] of a proxy: the `setPrototypeOf` trap's
        // boolean, and a true over a non-extensible target must be the truth.
        // THE CONTRACT IS THE CALLER'S THROW: Object.setPrototypeOf and the
        // __proto__ setter turn false into their TypeError, so the two
        // refusals 10.5.2 spells as TypeErrors (a revoked proxy, a trap
        // lying about a non-extensible target) answer false rather than
        // throwing here and again there - a throw this native raises has
        // landed by the time the caller raises its own, which is then
        // uncaught. Reflect.setPrototypeOf checks the revoked case itself.
        auto * p = static_cast<proxy_object *>(of.as_heap());
        if (!p->handler.is_object_like()) { return false; }
        const value trap = cx.lookup_property(p->handler, "setPrototypeOf");
        if (cx.throw_pending()) { return false; }
        if (trap.is_nullish()) { return set_prototype_of(cx, p->target, proto); }
        if (!trap.is_callable()) {
            cx.throw_error("TypeError", "proxy trap 'setPrototypeOf' is not a function");
            return false;
        }
        const value args[2] = {p->target, proto};
        const bool ok = context::truthy(cx.call(trap, args, p->handler));
        if (cx.throw_pending()) { return false; }
        if (!ok) { return false; }
        if (cx.is_extensible(p->target)) { return true; }
        const value real = prototype_of(cx, p->target);
        if (cx.throw_pending()) { return false; }
        return real.strict_equals(proto);
    }
    if (prototype_of(cx, of) == proto) { return true; } // step 4: the same one is always fine
    if (of.is_object() && of.as_heap() == cx.prototype(context::proto_kind::object)) {
        return false;
    }
    if (!cx.is_extensible(of)) { return false; }
    for (value walk = proto; walk.is_heap();) {
        if (walk.as_heap() == of.as_heap()) { return false; }
        if (walk.is_kind(heap_kind::proxy)) { break; } // step 8.c.i: a proxy ends the walk
        walk = prototype_of(cx, walk);
    }
    if (of.is_object()) {
        // null is an EXPLICIT null here - see object_object::prototype.
        static_cast<object_object *>(of.as_heap())->prototype =
            proto.is_null() ? value::undefined() : proto;
    } else if (of.is_array()) {
        static_cast<array_object *>(of.as_heap())->prototype =
            proto.is_null() ? value::undefined() : proto;
    } else if (of.is_kind(heap_kind::function)) {
        static_cast<closure_object *>(of.as_heap())->proto_link = proto;
    } else if (of.is_kind(heap_kind::native)) {
        static_cast<native_object *>(of.as_heap())->proto_link = proto;
    }
    return true;
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

} // namespace ctbrowser::script::detail
