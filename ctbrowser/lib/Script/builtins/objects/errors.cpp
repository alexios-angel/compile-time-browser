// ctbrowser.script builtins - the error types.
//
// One of five files carved out of a 1,429-line builtins/objects.cpp on
// 2026-09-08 - which was itself one of five carved out of builtins.cpp on
// 2026-08-09. Everything shared - the argument helpers, namespace detail, and
// these functions' declarations - is in ../internal.hpp; what only this
// directory shares is in internal.hpp beside this.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

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
    method(cx, error_proto, "toString", 0, [](context & c, std::span<value>) {
        const value self = c.current_this();
        const std::string name = c.to_string(c.lookup_property(self, "name"));
        const std::string message = c.to_string(c.lookup_property(self, "message"));
        return c.string(message.empty() ? name : name + ": " + message);
    });

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
            // 20.5.8.1: an instance's `message` is { true, false, true }, and
            // an ABSENT argument installs no own property at all - which is why
            // `new TypeError().hasOwnProperty('message')` is false.
            if (!a.empty() && !a[0].is_undefined()) {
                made->define("message", c.string(c.to_string(a[0])), attr_builtin);
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
            made->define("stack",
                         c.string(c.to_string(c.lookup_property(self, "name")) +
                                  (a.empty() ? std::string{} : ": " + c.to_string(a[0])) +
                                  c.current_stack()),
                         attr_builtin);
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
    for (const char * name :
         {"TypeError", "RangeError", "ReferenceError", "SyntaxError", "EvalError", "URIError"}) {
        (void)define(name, error_proto);
    }
    cx.set_prototype(context::proto_kind::error, error_proto);
}

} // namespace ctbrowser::script::builtins_detail
