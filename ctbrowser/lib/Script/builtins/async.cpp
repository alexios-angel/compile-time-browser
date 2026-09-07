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
    method(cx, json, "stringify", 3, [](context & c, std::span<value> a) {
        detail::json_writer state{c};
        detail::read_stringify_options(state, arg_at(a, 1), arg_at(a, 2));
        // THE VALUE IS SERIALISED AS A MEMBER OF A WRAPPER, 25.5.2 step 10, and
        // that is not ceremony: SerializeJSONProperty reads its value out of a
        // holder with a key, so the replacer gets `("", value)` and an object
        // to be `this` on its first call exactly as it does on every later one.
        // Without the wrapper the top level is a special case that no replacer
        // and no `toJSON` sees.
        const value wrapper = c.make_object();
        static_cast<object_object *>(wrapper.as_heap())->set("", arg_at(a, 0));
        std::string out;
        // AT THE TOP LEVEL an unserialisable value yields UNDEFINED, not the
        // string "null" - step 12. Inside an array the same value becomes null,
        // which is why the serialiser reports "omit" and the caller decides. A
        // page testing `if (json === undefined)` was told the string "null".
        if (!state.serialize(wrapper, "", arg_at(a, 0), out)) { return value::undefined(); }
        return c.string(out);
    });
    method(cx, json, "parse", 2, [](context & c, std::span<value> a) {
        // The source is held in a NAMED local: json_reader keeps a string_view
        // into it, and passing the temporary directly leaves the view dangling
        // for the whole parse.
        const std::string source = str_at(c, a, 0);
        detail::json_reader reader{c, source};
        const value out = reader.parse_text();
        // 25.5.1 step 3: a document that does not fit the JSON grammar is a
        // SyntaxError. It used to be `undefined`, which is a value a page can
        // and does mistake for a successfully parsed `null`-ish document.
        if (!reader.ok) {
            c.throw_error("SyntaxError",
                          "Unexpected token in JSON at position " + std::to_string(reader.at));
            return value::undefined();
        }
        const value reviver = arg_at(a, 1);
        if (!reviver.is_callable()) { return out; }
        // Step 7: the reviver walks a WRAPPER whose one property is "", for the
        // same reason stringify's does - the root has to be a (holder, key)
        // pair so the reviver can replace it.
        const value wrapper = c.make_object();
        static_cast<object_object *>(wrapper.as_heap())->set("", out);
        return detail::internalize_json(c, wrapper, "", out, reviver, 0);
    });
    cx.define_global("JSON", value::object(json));
}

namespace {

// A PROMISE THAT HAS NOT SETTLED. Three of the combinators below hand one back
// and settle it later, which is the whole difference between them and
// `Promise.resolve`.
[[nodiscard]] value pending_promise(context & cx) {
    const value made = detail::make_promise(cx, value::undefined(), false);
    static_cast<object_object *>(made.as_heap())
        ->define("__settled", value::boolean(false), attr_builtin);
    return made;
}

// ATTACH A REACTION THE WAY `then` ATTACHES ONE, and for the same reason it has
// to be the same way: a combinator is specified over arbitrary values, so all
// three of "already settled", "still pending" and "not a promise at all" have
// to reach the same code. `Promise.allSettled([p, 1])` must wait for `p` and
// must report the 1 as fulfilled.
//
// The reaction runs at the END OF THE TURN in every case, because that is what
// `settle` and `settle_with` do - a handler that runs the instant it is
// attached would order `Promise.allSettled([1]).then(f); after();` backwards.
void react(context & cx, value input, native_object * on_ok, native_object * on_err) {
    object_object * record = detail::new_table(cx);
    record->set("ok", value::object(on_ok));
    record->set("err", value::object(on_err));
    // `deliver` DROPS A RECORD WITH NO `next` before it calls anything: that
    // slot is the promise a `then` would have returned. Nothing reads this one,
    // and leaving it out silently loses the reaction.
    record->set("next", pending_promise(cx));

    if (input.is_object()) {
        auto * p = static_cast<object_object *>(input.as_heap());
        if (value * settled = p->find("__settled"); settled != nullptr) {
            if (context::truthy(*settled)) {
                const value * held = p->find("__value");
                const value * state = p->find("__rejected");
                detail::enqueue_delivery(cx, value::object(record),
                                         held == nullptr ? value::undefined() : *held,
                                         state != nullptr && context::truthy(*state));
                return;
            }
            if (value * handlers = p->find("__handlers");
                handlers != nullptr && handlers->is_array()) {
                static_cast<array_object *>(handlers->as_heap())
                    ->items.push_back(value::object(record));
                return;
            }
        }
    }
    // Not a promise: 27.2.4.7.1 resolves it with itself.
    detail::enqueue_delivery(cx, value::object(record), input, false);
}

// The argument, as the list of things to wait for.
//
// AN ARRAY, NOT AN ITERABLE. A general iterable needs Symbol.iterator dispatch,
// which is the gap `for..of` has here too (docs/script.md), and `Promise.all`
// has always read its argument this way. A non-array is an empty list rather
// than the TypeError the specification asks for, which is the same deviation
// and is named rather than fixed in passing.
[[nodiscard]] std::vector<value> entries_of(std::span<value> a) {
    if (!a.empty() && a[0].is_array()) {
        return static_cast<array_object *>(a[0].as_heap())->items;
    }
    return {};
}

// A COMBINATOR'S SHARED STATE, reachable rather than captured.
//
// A `value` held only by a C++ lambda is invisible to the collector - the
// comment on `new Promise`'s resolve/reject says what that cost - so the
// result promise, the results array and the counter live in one object that
// every reaction RETAINS.
[[nodiscard]] object_object * combinator_state(context & cx, value out, value results,
                                               std::size_t count) {
    object_object * state = detail::new_table(cx);
    state->set("out", out);
    state->set("results", results);
    state->set("left", value::number(static_cast<double>(count)));
    return state;
}

[[nodiscard]] double count_down(object_object * state) {
    const value * left = state->find("left");
    const double remaining = (left == nullptr ? 0.0 : left->as_number()) - 1.0;
    state->set("left", value::number(remaining));
    return remaining;
}

[[nodiscard]] value slot_of(object_object * state, const char * name) {
    const value * held = state->find(name);
    return held == nullptr ? value::undefined() : *held;
}

void put_result(object_object * state, std::size_t index, value entry) {
    const value * results = state->find("results");
    if (results == nullptr || !results->is_array()) { return; }
    auto & items = static_cast<array_object *>(results->as_heap())->items;
    if (index < items.size()) { items[index] = entry; }
}

// 27.2.4.1.2: every fulfilment writes its value into its own slot and the last
// one to arrive resolves the result with the whole array; ONE rejection settles
// it outright, and the reactions still queued after it find the result already
// settled - `settle` is settle-once, which is what makes that safe rather than
// needing a flag of its own.
[[nodiscard]] native_object * all_reaction(context & cx, value state, std::size_t index,
                                           bool rejected) {
    auto * made = cx.allocate<native_object>(
        rejected ? "rejected" : "fulfilled",
        [state, index, rejected](context & c, std::span<value> args) {
            auto * held = static_cast<object_object *>(state.as_heap());
            const value with = args.empty() ? value::undefined() : args[0];
            if (rejected) {
                detail::settle(c, slot_of(held, "out"), with, true);
                return value::undefined();
            }
            put_result(held, index, with);
            if (count_down(held) <= 0) {
                detail::settle(c, slot_of(held, "out"), slot_of(held, "results"), false);
            }
            return value::undefined();
        });
    made->retained.push_back(state);
    return made;
}

// 27.2.4.2.2: every reaction writes `{ status, value }` or
// `{ status, reason }` into its own slot, and the last one to finish resolves.
[[nodiscard]] native_object * allsettled_reaction(context & cx, value state, std::size_t index,
                                                  bool rejected) {
    auto * made = cx.allocate<native_object>(
        rejected ? "rejected" : "fulfilled",
        [state, index, rejected](context & c, std::span<value> args) {
            auto * held = static_cast<object_object *>(state.as_heap());
            const value entry = c.make_object();
            auto * record = static_cast<object_object *>(entry.as_heap());
            record->set("status", c.string(rejected ? "rejected" : "fulfilled"));
            record->set(rejected ? "reason" : "value", args.empty() ? value::undefined() : args[0]);
            put_result(held, index, entry);
            if (count_down(held) <= 0) {
                detail::settle(c, slot_of(held, "out"), slot_of(held, "results"), false);
            }
            return value::undefined();
        });
    made->retained.push_back(state);
    return made;
}

// THE ERROR `Promise.any` REJECTS WITH when every input rejected.
//
// AggregateError is a constructor this engine does not have (docs/test262.md
// names it as deliberately absent), so `make_error` falls back to
// Error.prototype plus an own `name` - which keeps `e.name === "AggregateError"`
// and `e instanceof Error` true. `errors` is the array 27.2.4.3.2 requires, in
// input order.
[[nodiscard]] value aggregate_error(context & cx, value errors) {
    const value made = cx.make_error("AggregateError", "All promises were rejected");
    static_cast<object_object *>(made.as_heap())->set("errors", errors);
    return made;
}

// 27.2.4.3.2, and it is `all` with the two outcomes swapped: the first
// FULFILMENT wins, and it takes every rejection to fail.
[[nodiscard]] native_object * any_reaction(context & cx, value state, std::size_t index,
                                           bool rejected) {
    auto * made = cx.allocate<native_object>(
        rejected ? "rejected" : "fulfilled",
        [state, index, rejected](context & c, std::span<value> args) {
            auto * held = static_cast<object_object *>(state.as_heap());
            const value with = args.empty() ? value::undefined() : args[0];
            if (!rejected) {
                detail::settle(c, slot_of(held, "out"), with, false);
                return value::undefined();
            }
            put_result(held, index, with);
            if (count_down(held) <= 0) {
                detail::settle(c, slot_of(held, "out"),
                               aggregate_error(c, slot_of(held, "results")), true);
            }
            return value::undefined();
        });
    made->retained.push_back(state);
    return made;
}

// 27.2.4.5.2: whichever settles first settles the result, however it settled.
[[nodiscard]] native_object * race_reaction(context & cx, value state, bool rejected) {
    auto * made = cx.allocate<native_object>(
        rejected ? "rejected" : "fulfilled", [state, rejected](context & c, std::span<value> args) {
            auto * held = static_cast<object_object *>(state.as_heap());
            detail::settle(c, slot_of(held, "out"), args.empty() ? value::undefined() : args[0],
                           rejected);
            return value::undefined();
        });
    made->retained.push_back(state);
    return made;
}

} // namespace

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
        static_cast<object_object *>(made.as_heap())
            ->define("__settled", value::boolean(false), attr_builtin);
        return made;
    });
    cx.set_promise_settler([](context & c, value promise, value with, bool rejected) {
        detail::settle(c, promise, with, rejected);
    });
    object_object * promise_ctor = new_table(cx);
    method(cx, promise_ctor, "resolve", 1, [](context & c, std::span<value> a) {
        return detail::make_promise(c, a.empty() ? value::undefined() : a[0], false);
    });
    method(cx, promise_ctor, "reject", 1, [](context & c, std::span<value> a) {
        return detail::make_promise(c, a.empty() ? value::undefined() : a[0], true);
    });
    // `Promise.all` - 27.2.4.1. The first rejection wins; otherwise the result
    // is an array of the values in input order.
    //
    // IT WAITS, and it did not. This read `__value` off each entry as it walked
    // the array, which answers immediately - and wrongly - for an input that has
    // not settled: `Promise.all([d.promise]).then(f)` ran `f` with `[undefined]`
    // in the same turn and then never ran it again when `d.resolve` arrived. It
    // goes through `react` now, the same path `then` takes and the one the other
    // three combinators were rewritten onto, so "already settled", "still
    // pending" and "not a promise at all" all reach one implementation.
    //
    // WHAT THAT CHANGES FOR A PAGE, said plainly: a `Promise.all` over an input
    // that never settles no longer resolves. p5.js opens with
    // `Promise.all([waitForDocumentReady(), waitingForTranslator]).then(_globalInit)`
    // (vendor/p5/p5.js:138934); the first of those resolves at once here because
    // `document.readyState` is "complete", and the second is i18next's `init`,
    // whose backend fetches a CDN URL. If that promise never settles headless
    // then p5 never boots, where before it booted on a wrong answer - so the p5
    // ratchet is the thing to watch on this change.
    method(cx, promise_ctor, "all", 1, [](context & c, std::span<value> a) {
        const std::vector<value> entries = entries_of(a);
        const value out = pending_promise(c);
        const value results = c.make_array();
        static_cast<array_object *>(results.as_heap())
            ->items.assign(entries.size(), value::undefined());
        if (entries.empty()) {
            detail::settle(c, out, results, false);
            return out;
        }
        const value state = value::object(combinator_state(c, out, results, entries.size()));
        for (std::size_t i = 0; i < entries.size(); ++i) {
            react(c, entries[i], all_reaction(c, state, i, false), all_reaction(c, state, i, true));
        }
        return out;
    });
    // `Promise.allSettled` - 27.2.4.2. It never rejects: every input's outcome
    // is reported, in input order, as `{ status: "fulfilled", value }` or
    // `{ status: "rejected", reason }`.
    //
    // LIKE `all` ABOVE, this one waits: all four go through `react`, which is
    // the same path `then` takes. `all` was the last one that did not.
    method(cx, promise_ctor, "allSettled", 1, [](context & c, std::span<value> a) {
        const std::vector<value> entries = entries_of(a);
        const value out = pending_promise(c);
        const value results = c.make_array();
        static_cast<array_object *>(results.as_heap())
            ->items.assign(entries.size(), value::undefined());
        if (entries.empty()) {
            detail::settle(c, out, results, false);
            return out;
        }
        const value state = value::object(combinator_state(c, out, results, entries.size()));
        for (std::size_t i = 0; i < entries.size(); ++i) {
            react(c, entries[i], allsettled_reaction(c, state, i, false),
                  allsettled_reaction(c, state, i, true));
        }
        return out;
    });
    // `Promise.any` - 27.2.4.3. The first FULFILMENT wins; if every input
    // rejects it rejects with an AggregateError carrying `errors` in order.
    method(cx, promise_ctor, "any", 1, [](context & c, std::span<value> a) {
        const std::vector<value> entries = entries_of(a);
        const value out = pending_promise(c);
        const value errors = c.make_array();
        static_cast<array_object *>(errors.as_heap())
            ->items.assign(entries.size(), value::undefined());
        if (entries.empty()) {
            // 27.2.4.3.1 step 5: an empty list is already "all rejected".
            detail::settle(c, out, aggregate_error(c, errors), true);
            return out;
        }
        const value state = value::object(combinator_state(c, out, errors, entries.size()));
        for (std::size_t i = 0; i < entries.size(); ++i) {
            react(c, entries[i], any_reaction(c, state, i, false), any_reaction(c, state, i, true));
        }
        return out;
    });
    // `Promise.race` - 27.2.4.5. An EMPTY list stays pending forever, which is
    // the specified answer and not an oversight.
    method(cx, promise_ctor, "race", 1, [](context & c, std::span<value> a) {
        const std::vector<value> entries = entries_of(a);
        const value out = pending_promise(c);
        const value state = value::object(combinator_state(c, out, value::undefined(), 0));
        for (const value & entry : entries) {
            react(c, entry, race_reaction(c, state, false), race_reaction(c, state, true));
        }
        return out;
    });
    // `Promise.withResolvers` - 27.2.4.8, and the reason this file was opened:
    // WPT's `url-import-referrer-policy.html` fails on exactly this name. It is
    // `new Promise(executor)` turned inside out - the same promise and the same
    // two functions, handed back as an object instead of to a callback, so a
    // page does not have to smuggle them out of the executor's scope.
    method(cx, promise_ctor, "withResolvers", 0, [](context & c, std::span<value>) {
        const value promise = pending_promise(c);
        // RETAINED, not merely captured. These two hold the only reference to
        // the promise that outlives this call - a page keeps `resolve` - and a
        // C++ lambda capture is not a root. See `new Promise` below, where the
        // same omission cost an async function that could suspend exactly once.
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
        resolve_fn->retained.push_back(promise);
        reject_fn->retained.push_back(promise);
        const value out = c.make_object();
        auto * fields = static_cast<object_object *>(out.as_heap());
        fields->set("promise", promise);
        fields->set("resolve", value::object(resolve_fn));
        fields->set("reject", value::object(reject_fn));
        return out;
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
        made->define("__settled", value::boolean(false),
                     attr_builtin); // pending until told otherwise
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
        // capturing. That was first done with a PROPERTY, `__promise`, because
        // a native's props are traced - and the mechanism it asked for in this
        // comment now exists: `native_object::retained` is a traced list that
        // is not a property, so it costs no name, is not walked by `find` on
        // every lookup, and - the reason this migrated rather than being left
        // alone - is not visible to `Object.getOwnPropertyNames(resolve)`,
        // which `__promise` was.
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
        resolve_fn->retained.push_back(promise);
        reject_fn->retained.push_back(promise);
        const value resolve = value::object(resolve_fn);
        const value reject = value::object(reject_fn);
        const value args[2] = {resolve, reject};
        (void)c.call(a[0], args);
        return promise;
    });
    for (const auto & [key, item] : promise_ctor->props) { promise_new->set(key, item); }
    detail::constant(promise_new, "prototype", value::object(detail::promise_prototype(cx)));
    cx.define_global("Promise", value::object(promise_new));

    cx.define_native("isNaN", [](context &, std::span<value> a) {
        return value::boolean(std::isnan(num_at(a, 0)));
    });
    cx.define_native("isFinite", [](context &, std::span<value> a) {
        return value::boolean(std::isfinite(num_at(a, 0)));
    });
    // Their own `name` and `length` (19.2.2, 19.2.3, both of arity 1).
    // define_native allocates a bare native, so `length` was absent and `name`
    // came from context::own_property's synthesised fallback - which cannot
    // refuse a write or be deleted, and which verifyProperty reports as two
    // failures at once.
    const auto slots = [&](const char * name, double arity) {
        const value fn = cx.global(name);
        if (!fn.is_kind(heap_kind::native)) { return; }
        auto * made = static_cast<native_object *>(fn.as_heap());
        made->define("length", value::number(arity), attr_configurable);
        made->define("name", cx.string(name), attr_configurable);
    };
    slots("isNaN", 1);
    slots("isFinite", 1);
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
        detail::constant(string_ctor, "__conversion", value::boolean(true));
        // detail::method, not `set`: clause 17 makes every one of these
        // { writable: true, enumerable: FALSE, configurable: true }, and `set`
        // gave them the default attributes - so `Object.keys(String)` listed
        // fromCharCode and fromCodePoint and `for (k in String)` walked them.
        const auto stat = [&](const char * name, double arity, native_fn fn) {
            detail::method(cx, string_ctor, name, arity, std::move(fn));
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
        stat("fromCharCode", 1, [encode](context & c, std::span<value> a) {
            std::string out;
            for (std::size_t i = 0; i < a.size(); ++i) {
                encode(out, static_cast<std::uint32_t>(context::to_uint32(a[i]) & 0xFFFFu));
            }
            return c.string(out);
        });
        stat("fromCodePoint", 1, [encode](context & c, std::span<value> a) {
            std::string out;
            for (std::size_t i = 0; i < a.size(); ++i) {
                // 22.1.2.2 step 2c: a code point must be an INTEGER in
                // [0, 0x10FFFF] and anything else is a RangeError. to_uint32
                // wrapped instead, so `String.fromCodePoint(-1)` produced the
                // encoding of 0xFFFFFFFF and `fromCodePoint(1.5)` produced one
                // for 1.
                const double code = c.to_number_value(a[i]);
                if (code != std::trunc(code) || std::isnan(code) || code < 0 || code > 0x10FFFF) {
                    c.throw_error("RangeError", "Invalid code point");
                    return c.string(std::string{});
                }
                encode(out, static_cast<std::uint32_t>(code));
            }
            return c.string(out);
        });
        // `String.raw`, 22.1.2.4. It is the tag every template-literal library
        // reaches for and it is also callable directly, which is the only way
        // this engine can reach it - the compiler still refuses a TAGGED
        // template (docs/script.md names it), so `String.raw({raw: [...]}, ...)`
        // is the whole surface. 25 of the 30 test262 files read "TypeError:
        // raw is undefined, not a function" before this.
        stat("raw", 1, [](context & c, std::span<value> a) {
            const value cooked = arg_at(a, 0);
            const value literals = c.lookup_property(cooked, "raw");
            const value raw_len = c.lookup_property(literals, "length");
            const double count = to_length(c.to_number_value(raw_len));
            std::string out;
            for (double k = 0; k < count; k += 1.0) {
                out += c.to_string(c.lookup_index(literals, value::number(k)));
                if (k + 1 == count) { break; }
                // One substitution BETWEEN each pair of literals, and running
                // out of them ends the interpolation rather than the string:
                // `String.raw({raw: ['a','b','c']}, 'x')` is "axbc".
                const std::size_t at = static_cast<std::size_t>(k) + 1;
                if (at < a.size()) { out += c.to_string(a[at]); }
            }
            return c.string(out);
        });
        if (object_object * table = cx.prototype(context::proto_kind::string)) {
            detail::constant(string_ctor, "prototype", value::object(table));
            link_constructor(cx, table, "String", 1, value::object(string_ctor));
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
