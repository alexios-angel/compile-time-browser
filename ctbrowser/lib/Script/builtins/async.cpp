// ctbrowser.script builtins - JSON and Promise.
//
// One of five files carved out of a 4,118-line builtins.cpp on 2026-08-09.
// Everything shared - the argument helpers, namespace detail, and these
// functions' declarations - is in internal.hpp.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

// JSON
void install_json(context & cx) {
    using detail::method;
    using detail::new_table;
    object_object * json = new_table(cx);
    method(cx, json, "stringify", [](context & c, std::span<value> a) {
        // AT THE TOP LEVEL an unserialisable value yields UNDEFINED, not the
        // string "null" - 25.5.2 step 12. Inside an array the same value
        // becomes null, which is why write_json cannot decide this and the
        // caller must. A page testing `if (json === undefined)` was told the
        // string "null" instead.
        const value subject = arg_at(a, 0);
        if (subject.is_undefined() || subject.is_callable()) { return value::undefined(); }
        std::string out;
        detail::write_json(c, subject, out);
        return c.string(out);
    });
    method(cx, json, "parse", [](context & c, std::span<value> a) {
        // The source is held in a NAMED local: json_reader keeps a string_view
        // into it, and passing the temporary directly leaves the view dangling
        // for the whole parse.
        const std::string source = str_at(c, a, 0);
        detail::json_reader reader{c, source, 0, true};
        const value out = reader.parse();
        return reader.ok ? out : value::undefined();
    });
    cx.define_global("JSON", value::object(json));
}

// Promise
void install_promise(context & cx) {
    using detail::method;
    using detail::new_table;
    cx.set_promise_factory(
        [](context & c, value v, bool rejected) { return detail::make_promise(c, v, rejected); });
    // What `await` needs to suspend: a promise that has not settled, and a way
    // to settle one. The VM can READ a promise - it always could - but making
    // and settling run this library's own logic, queue included.
    cx.set_pending_promise_factory([](context & c) {
        const value made = detail::make_promise(c, value::undefined(), false);
        static_cast<object_object *>(made.as_heap())->set("__settled", value::boolean(false));
        return made;
    });
    cx.set_promise_settler([](context & c, value promise, value with, bool rejected) {
        detail::settle(c, promise, with, rejected);
    });
    object_object * promise_ctor = new_table(cx);
    method(cx, promise_ctor, "resolve", [](context & c, std::span<value> a) {
        return detail::make_promise(c, a.empty() ? value::undefined() : a[0], false);
    });
    method(cx, promise_ctor, "reject", [](context & c, std::span<value> a) {
        return detail::make_promise(c, a.empty() ? value::undefined() : a[0], true);
    });
    // Settled promises make `all` a plain unwrap-each: the first rejection wins,
    // otherwise the result is an array of the values in order.
    method(cx, promise_ctor, "all", [](context & c, std::span<value> a) {
        const value out = c.make_array();
        auto * items = static_cast<array_object *>(out.as_heap());
        if (!a.empty() && a[0].is_array()) {
            for (const value & entry : static_cast<array_object *>(a[0].as_heap())->items) {
                if (!entry.is_object()) {
                    items->items.push_back(entry);
                    continue;
                }
                auto * promise = static_cast<object_object *>(entry.as_heap());
                value * state = promise->find("__rejected");
                value * held = promise->find("__value");
                if (state != nullptr && context::truthy(*state)) {
                    return detail::make_promise(c, held != nullptr ? *held : value::undefined(),
                                                true);
                }
                items->items.push_back(held != nullptr ? *held : entry);
            }
        }
        return detail::make_promise(c, out, false);
    });
    // `new Promise(executor)`. The executor runs IMMEDIATELY and is handed
    // resolve and reject; a promise it does not settle stays pending until
    // something later calls one of them. That is the whole of what was missing,
    // and p5.js opens with it:
    //
    //   new Promise((resolve) => {
    //     if (document.readyState === 'complete') { resolve(); }
    //     else { window.addEventListener('load', resolve, false); }
    //   })
    //
    // Callable AND a namespace, so `Promise.resolve` still reads off it.
    auto * promise_new = cx.allocate<native_object>("Promise", [](context & c, std::span<value> a) {
        const value promise = detail::make_promise(c, value::undefined(), false);
        auto * made = static_cast<object_object *>(promise.as_heap());
        made->set("__settled", value::boolean(false)); // pending until told otherwise
        if (a.empty() || !a[0].is_callable()) { return promise; }
        // A `value` CAPTURED BY A C++ LAMBDA IS INVISIBLE TO THE COLLECTOR.
        //
        // These two hold the only reference to the promise that outlives the
        // constructor: a page keeps `resolve`, not the promise. The lambda
        // capture is not a root, so the promise was collected out from under it
        // and calling resolve() later settled freed memory - which failed
        // SILENTLY, because settle() checks is_object() and a recycled cell is
        // usually not one.
        //
        // The cost was an async function that could suspend exactly ONCE. The
        // first await's promise was still in a live frame's registers; the
        // second one's existed only inside these captures and in the awaited
        // promise's handler list - a cycle with no root - so it went, and the
        // frame never came back. Every p5 loader awaits twice.
        //
        // The fix is to make the reference REACHABLE rather than to stop
        // capturing: a native's props are traced, so a property the page never
        // reads is exactly the root this needs. Anything else that captures a
        // value in a native lambda needs the same treatment.
        auto * resolve_fn =
            c.allocate<native_object>("resolve", [promise](context & inner, std::span<value> args) {
                detail::settle(inner, promise, args.empty() ? value::undefined() : args[0], false);
                return value::undefined();
            });
        auto * reject_fn =
            c.allocate<native_object>("reject", [promise](context & inner, std::span<value> args) {
                detail::settle(inner, promise, args.empty() ? value::undefined() : args[0], true);
                return value::undefined();
            });
        resolve_fn->set("__promise", promise);
        reject_fn->set("__promise", promise);
        const value resolve = value::object(resolve_fn);
        const value reject = value::object(reject_fn);
        const value args[2] = {resolve, reject};
        (void)c.call(a[0], args);
        return promise;
    });
    for (const auto & [key, item] : promise_ctor->props) { promise_new->set(key, item); }
    promise_new->set("prototype", value::object(detail::promise_prototype(cx)));
    cx.define_global("Promise", value::object(promise_new));

    cx.define_native("isNaN", [](context &, std::span<value> a) {
        return value::boolean(std::isnan(num_at(a, 0)));
    });
    cx.define_native("isFinite", [](context &, std::span<value> a) {
        return value::boolean(std::isfinite(num_at(a, 0)));
    });
    // `String` is a NAMESPACE as well as a coercion, the same way Number is.
    // `String.fromCharCode.apply(null, bytes)` is how a page turns a byte array
    // into text - 27 uses in p5.js - and it read undefined and applied it.
    {
        auto * string_ctor = cx.allocate<native_object>("String", [](context & c,
                                                                     std::span<value> a) {
            // A SYMBOL IS DESCRIBED, NOT COERCED. `String(sym)` is the one
            // conversion the specification allows on a symbol (22.1.1.1
            // step 2) and it yields "Symbol(description)". Everything else
            // here goes through `to_string`, which for a symbol returns its
            // internal KEY - that is deliberate and load-bearing, because
            // computed property access resolves `o[sym]` through the same
            // call, so it cannot be changed without separating
            // ToPropertyKey from ToString. Special-casing the explicit
            // conversion is the part that can be had cheaply.
            if (!a.empty() && a[0].is_kind(heap_kind::symbol)) {
                return c.string("Symbol(" +
                                static_cast<symbol_object *>(a[0].as_heap())->description + ")");
            }
            return c.string(a.empty() ? std::string{} : c.to_string(a[0]));
        });
        // A CONVERSION, not a constructor of wrappers - see context::construct. `new
        // String(x)` evaluates to the converted value here rather than to a wrapper
        // object; before the flag it evaluated to an empty object and the value was
        // gone.
        string_ctor->set("__conversion", value::boolean(true));
        const auto stat = [&](const char * name, native_fn fn) {
            string_ctor->set(name, value::object(cx.allocate<native_object>(name, std::move(fn))));
        };
        // UTF-8 out, because strings here are bytes: a code point above 0x7F
        // becomes its encoding rather than one char, which is what makes the
        // round trip through String.prototype work.
        const auto encode = [](std::string & out, std::uint32_t code) {
            if (code < 0x80) {
                out += static_cast<char>(code);
            } else if (code < 0x800) {
                out += static_cast<char>(0xC0 | (code >> 6));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else if (code < 0x10000) {
                out += static_cast<char>(0xE0 | (code >> 12));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (code >> 18));
                out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            }
        };
        stat("fromCharCode", [encode](context & c, std::span<value> a) {
            std::string out;
            for (std::size_t i = 0; i < a.size(); ++i) {
                encode(out, static_cast<std::uint32_t>(context::to_uint32(a[i]) & 0xFFFFu));
            }
            return c.string(out);
        });
        stat("fromCodePoint", [encode](context & c, std::span<value> a) {
            std::string out;
            for (std::size_t i = 0; i < a.size(); ++i) { encode(out, context::to_uint32(a[i])); }
            return c.string(out);
        });
        if (object_object * table = cx.prototype(context::proto_kind::string)) {
            string_ctor->set("prototype", value::object(table));
            link_constructor(cx, table, "String", value::object(string_ctor));
        }
        cx.define_global("String", value::object(string_ctor));
    }
    // `Number` is installed by install_number, which gives it the statics as
    // well as the coercion. Defining it again here would replace the whole
    // thing with a bare function and silently drop Number.isFinite and its
    // siblings - which is exactly what it used to do.
    //
    // `Boolean` is the same, and it WAS being redefined here, one line under
    // that warning: install_boolean gives it a prototype and this replaced it
    // with a bare coercion, so `Object.getPrototypeOf(true) === Boolean.prototype`
    // was false and `true.toString()` found nothing.
}

} // namespace ctbrowser::script::builtins_detail
