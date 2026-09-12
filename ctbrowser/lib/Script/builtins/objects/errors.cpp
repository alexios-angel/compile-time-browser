// ctbrowser.script builtins - the error types.
//
// One of five files carved out of a 1,429-line builtins/objects.cpp on
// 2026-09-08 - which was itself one of five carved out of builtins.cpp on
// 2026-08-09. Everything shared - the argument helpers, namespace detail, and
// these functions' declarations - is in ../internal.hpp; what only this
// directory shares is in internal.hpp beside this.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

namespace {
// [[ErrorData]], and the trace with it: a private key, invisible to every
// property walk (value.hpp, private_key_prefix).
constexpr std::string_view error_stack_slot = "@#ErrorData";
} // namespace

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
    // 20.5.3: `message` is "" and `name` is "Error" on the PROTOTYPE, both
    // { writable: true, enumerable: false, configurable: true }. Enumerable is
    // what put them in `Object.keys(e)` and in `JSON.stringify(e)`.
    error_proto->define("name", cx.string("Error"), attr_builtin);
    error_proto->define("message", cx.string(""), attr_builtin);
    // 20.5.3.4: an object receiver or a TypeError; an undefined `name` is
    // "Error" and an undefined `message` is ""; ToString of either refuses a
    // Symbol; and an empty half yields the other alone.
    method(cx, error_proto, "toString", 0, [](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!self.is_object_like()) {
            c.throw_error("TypeError", "Error.prototype.toString called on non-object");
            return value::undefined();
        }
        const value raw_name = c.lookup_property(self, "name");
        if (c.throw_pending() || !stringable_arg(c, raw_name)) { return value::undefined(); }
        const std::string name = raw_name.is_undefined() ? "Error" : c.to_string(raw_name);
        const value raw_message = c.lookup_property(self, "message");
        if (c.throw_pending() || !stringable_arg(c, raw_message)) { return value::undefined(); }
        const std::string message = raw_message.is_undefined() ? "" : c.to_string(raw_message);
        if (name.empty()) { return c.string(message); }
        return c.string(message.empty() ? name : name + ": " + message);
    });
    // `Error.prototype.stack` IS AN ACCESSOR (the error-stacks proposal, which
    // test262 carries as `error-stack-accessor`): an instance has no own
    // `stack`, the getter answers the trace the constructor recorded in a
    // private slot - the [[ErrorData]] marker here - or undefined for anything
    // without one, and the setter writes an own data property
    // (SetterThatIgnoresPrototypeProperties). Errors the ENGINE raises still
    // carry an own `stack` from context::make_error, which shadows this.
    error_proto->define_accessor(
        "stack",
        value::object(cx.allocate<native_object>(
            "get stack",
            [](context & c, std::span<value>) {
                const value self = c.current_this();
                if (!self.is_object_like()) {
                    c.throw_error("TypeError", "Error.prototype.stack getter called on non-object");
                    return value::undefined();
                }
                if (!self.is_object()) { return value::undefined(); }
                value * trace =
                    static_cast<object_object *>(self.as_heap())->find(error_stack_slot);
                return trace == nullptr ? value::undefined() : *trace;
            })),
        value::object(cx.allocate<native_object>(
            "set stack",
            [](context & c, std::span<value> a) {
                const value self = c.current_this();
                if (!self.is_object_like()) {
                    c.throw_error("TypeError", "Error.prototype.stack setter called on non-object");
                    return value::undefined();
                }
                if (!arg_at(a, 0).is_string()) {
                    c.throw_error("TypeError", "Error.prototype.stack must be set to a string");
                    return value::undefined();
                }
                if (c.has_own_property(self, "stack")) {
                    c.clear_store_rejected();
                    c.store_property(self, "stack", a[0]);
                    c.strict_store_check("stack");
                    return value::undefined();
                }
                context::property_descriptor wanted;
                wanted.has_value = wanted.has_writable = wanted.has_enumerable =
                    wanted.has_configurable = true;
                wanted.held = a[0];
                wanted.writable = wanted.enumerable = wanted.configurable = true;
                if (!c.define_own_property(self, "stack", wanted)) {
                    c.throw_error("TypeError", "Cannot define property stack");
                }
                return value::undefined();
            })),
        attr_configurable);

    // One constructor shape, seven names. `parent` is Error's prototype for the
    // subclasses, so the chain a page walks is the one it expects.
    //
    // AND `cx.register_error_prototype` IS THE HALF THAT WAS MISSING. The
    // constructors and the prototypes were both here already; what was not was
    // any way for `context::make_error` to find the RIGHT one, so every error
    // the engine itself raised was put on Error.prototype and answered `Error`
    // to `thrown.constructor`. test262 measured 336 tests failing on that shape
    // alone (docs/test262.md, 2026-09-02).
    native_object * error_ctor = nullptr;
    const auto define = [&](const char * name, object_object * parent) {
        object_object * proto = parent == nullptr ? error_proto : new_table(cx);
        if (parent != nullptr) {
            proto->prototype = value::object(parent);
            // 20.5.6.3: each NativeError.prototype carries its OWN `name` and
            // its own `message`, rather than inheriting Error's.
            proto->define("name", cx.string(name), attr_builtin);
            proto->define("message", cx.string(""), attr_builtin);
        }
        auto * ctor = cx.allocate<native_object>(name, [proto](context & c, std::span<value> a) {
            // `this` is the instance when called through `new`; a bare
            // `Error(...)` makes one anyway, which is what the spec says.
            value self = c.current_this();
            if (!self.is_object()) { self = c.make_object(); }
            auto * made = static_cast<object_object *>(self.as_heap());
            if (!made->prototype.is_object()) { made->prototype = value::object(proto); }
            // 20.5.1.1 step 3: an instance's `message` is { true, false, true },
            // ToString'd (a Symbol refuses), and an ABSENT argument installs no
            // own property at all - which is why
            // `new TypeError().hasOwnProperty('message')` is false.
            std::string text;
            if (!a.empty() && !a[0].is_undefined()) {
                if (!stringable_arg(c, a[0])) { return value::undefined(); }
                text = c.to_string(a[0]);
                if (c.throw_pending()) { return value::undefined(); }
                made->define("message", c.string(text), attr_builtin);
            }
            // Step 4, InstallErrorCause: `cause` comes off the options object
            // when HasProperty says it is there, as { true, false, true },
            // and AFTER `message` (20.5.8.1's order).
            if (const value options = arg_at(a, 1); options.is_object_like()) {
                const bool has_cause = c.has_property(options, c.string("cause"));
                if (c.throw_pending()) { return value::undefined(); }
                if (has_cause) {
                    const value cause = c.lookup_property(options, "cause");
                    if (c.throw_pending()) { return value::undefined(); }
                    made->define("cause", cause, attr_builtin);
                }
            }
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
            //
            // IN THE PRIVATE SLOT, not as an own property: Error.prototype's
            // `stack` accessor answers it, and the slot is the [[ErrorData]]
            // Error.isError and the accessor test for.
            made->define(error_stack_slot,
                         c.string(c.to_string(c.lookup_property(self, "name")) +
                                  (a.empty() ? std::string{} : ": " + text) + c.current_stack()),
                         attr_none);
            return self;
        });
        // `X.prototype` on a built-in constructor is { false, false, false }
        // (20.5.6.2.1), and `X.length` is 1 - both non-enumerable. link_constructor
        // wires `prototype.constructor` and `X.name` with the right attributes.
        link_constructor(cx, proto, name, 1, value::object(ctor));
        ctor->define("prototype", value::object(proto), attr_none);
        // 20.5.6.2: a NativeError constructor's [[Prototype]] is %Error%.
        if (error_ctor != nullptr) { ctor->proto_link = value::object(error_ctor); }
        cx.register_error_prototype(name, proto);
        cx.define_global(name, value::object(ctor));
        return ctor;
    };
    error_ctor = define("Error", nullptr);
    // 20.5.2.1 Error.isError: an object with [[ErrorData]] - the private stack
    // slot the constructors above record - and nothing else, however much it
    // looks like one.
    method(cx, error_ctor, "isError", 1, [](context &, std::span<value> a) {
        const value v = arg_at(a, 0);
        if (!v.is_object()) { return value::boolean(false); }
        return value::boolean(static_cast<object_object *>(v.as_heap())->find(error_stack_slot) !=
                              nullptr);
    });
    for (const char * name :
         {"TypeError", "RangeError", "ReferenceError", "SyntaxError", "EvalError", "URIError"}) {
        (void)define(name, error_proto);
    }
    cx.set_prototype(context::proto_kind::error, error_proto);
}

} // namespace ctbrowser::script::builtins_detail
