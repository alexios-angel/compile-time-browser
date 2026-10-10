//===- errors.cpp - Error and its subclasses --------------------*- C++ -*-===//
//
// The Error constructor, its six NativeError subclasses, Error.prototype
// (toString and the `stack` accessor) and Error.isError. Bare section numbers
// refer to ECMA-262. Shared helpers and declarations are in ../internal.hpp;
// what only this directory shares is in internal.hpp beside this file.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

namespace {
/// [[ErrorData]], holding the stack trace: a private key, invisible to every
/// property walk (value.hpp, private_key_prefix).
constexpr std::string_view error_stack_slot = CTBROWSER_PRIVATE_KEY("ErrorData");
} // namespace

/// Installs `Error` and the standard subclasses. An error is an ordinary object
/// with `name`, `message` and `stack`, and each constructor's `prototype` chains
/// to Error's so `e instanceof Error` holds for a TypeError and for a page's own
/// `class MyError extends Error`.
void install_errors(context & cx) {
    using detail::method;
    using detail::new_table;

    object_object * error_proto = new_table(cx);
    // 20.5.3: `message` is "" and `name` is "Error" on the prototype, both
    // { writable: true, enumerable: false, configurable: true }, so neither
    // shows up in `Object.keys(e)` or `JSON.stringify(e)`.
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
    // `Error.prototype.stack` is an accessor (the error-stacks proposal, which
    // test262 carries as `error-stack-accessor`): an instance has no own
    // `stack`, the getter answers the trace the constructor recorded in a
    // private slot - the [[ErrorData]] marker here - or undefined for anything
    // without one, and the setter writes an own data property
    // (SetterThatIgnoresPrototypeProperties). Errors the engine raises still
    // carry an own `stack` from context::make_error, which shadows this.
    // The getter and setter are built-in functions with their own `length` (0
    // and 1).
    auto * get_stack = detail::method_native(cx, "get stack", [](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!self.is_object_like()) {
            c.throw_error("TypeError", "Error.prototype.stack getter called on non-object");
            return value::undefined();
        }
        if (!self.is_object()) { return value::undefined(); }
        value * trace = static_cast<object_object *>(self.as_heap())->find(error_stack_slot);
        return trace == nullptr ? value::undefined() : *trace;
    });
    detail::install_arity(cx, get_stack, 0);
    auto * set_stack =
        detail::method_native(cx, "set stack", [error_proto](context & c, std::span<value> a) {
            const value self = c.current_this();
            if (!self.is_object_like()) {
                c.throw_error("TypeError", "Error.prototype.stack setter called on non-object");
                return value::undefined();
            }
            // SetterThatIgnoresPrototypeProperties step 2: the home
            // object itself refuses - its own `stack` is this accessor,
            // and writing through it would call this setter again.
            if (self.as_heap() == error_proto) {
                c.throw_error("TypeError", "Cannot set Error.prototype.stack");
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
        });
    detail::install_arity(cx, set_stack, 1);
    error_proto->define_accessor("stack", value::object(get_stack), value::object(set_stack),
                                 attr_configurable);

    // One constructor shape, seven names. `parent` is Error's prototype for the
    // subclasses, so the chain a page walks is the one it expects.
    //
    // `cx.register_error_prototype` lets context::make_error give an
    // engine-raised error the right prototype (TypeError.prototype, not
    // Error.prototype), so `thrown.constructor` names the right class.
    native_object * error_ctor = nullptr;
    const auto define = [&](const char * name, object_object * parent) {
        object_object * proto = parent == nullptr ? error_proto : new_table(cx);
        if (parent != nullptr) {
            proto->prototype = value::object(parent);
            // 20.5.6.3: each NativeError.prototype carries its own `name` and
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
            // ToString'd (a Symbol refuses), and an absent argument installs no
            // own property at all - which is why
            // `new TypeError().hasOwnProperty('message')` is false.
            // ToString is ToPrimitive first: an object whose @@toPrimitive
            // answers a Symbol is the TypeError too (string_arg).
            std::string text;
            if (!a.empty() && !a[0].is_undefined()) {
                const detail::unwind_watch watch{c};
                text = string_arg(c, a[0]);
                if (watch.threw()) { return value::undefined(); }
                made->define("message", c.string(text), attr_builtin);
            }
            // Step 4, InstallErrorCause: `cause` comes off the options object
            // when HasProperty says it is there, as { true, false, true },
            // and after `message` (20.5.8.1's order).
            if (const value options = arg_at(a, 1); options.is_object_like()) {
                const bool has_cause = c.has_property(options, c.string("cause"));
                if (c.throw_pending()) { return value::undefined(); }
                if (has_cause) {
                    const value cause = c.lookup_property(options, "cause");
                    if (c.throw_pending()) { return value::undefined(); }
                    made->define("cause", cause, attr_builtin);
                }
            }
            // `stack` is "Name: message" followed by the call frames. No frame
            // is skipped: a native pushes none, so the top frame is the JS
            // function that wrote `new Error`. It is stored in the private
            // [[ErrorData]] slot, which Error.prototype's `stack` accessor and
            // Error.isError read.
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
