//===- iterator_internal.hpp - Iterator protocol for natives ----*- C++ -*-===//
//
// Private to lib/Script/builtins/; not installed and in no file set. The
// iterator-protocol abstract operations of ECMA-262 7.4 as natives use them
// (the Iterator Record, GetIteratorDirect, IteratorStepValue, IteratorClose),
// plus the one device every IfAbruptCloseIterator and IfAbruptRejectPromise
// needs: a sequence of operations run as a single completion record. Used by
// the iterator helpers (iterator.cpp, iterator/), async.cpp (the Promise
// combinators and AsyncFromSyncIterator), disposable.cpp, keyed.cpp,
// object.cpp and shadow_realm.cpp.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../internal.hpp"

namespace ctbrowser::script::detail {

/// The outcome of fenced(): the thrown value when `threw`, else the result.
struct completion {
    bool threw = false;
    value result = value::undefined();
};
/// Runs a sequence of abstract operations as one completion record.
///
/// A native sees a throw in two ways and can read neither: one its own
/// `call` deferred (context::throw_pending; the value sits in a private slot
/// until the native returns), or one it raised itself with throw_error, which
/// has already unwound to the nearest handler by the time the next line runs
/// (context::unwinds). The specification wants the thrown value at every
/// IfAbruptCloseIterator and IfAbruptRejectPromise, and wants to run more
/// JavaScript (`return()`, `reject()`) before rethrowing it. Running `body`
/// under call_fenced makes both kinds stop at the fence, which hands the value
/// back. `body` runs with what it captured; the caller roots those values.
template <class Body> [[nodiscard]] completion fenced(context & cx, Body && body) {
    auto * runner = cx.allocate<native_object>(
        "", [body = std::forward<Body>(body)](context & c, std::span<value>) { return body(c); });
    runner->is_constructor = false;
    completion out;
    value thrown = value::undefined();
    const value produced =
        cx.call_fenced(value::object(runner), {}, value::undefined(), out.threw, thrown);
    out.result = out.threw ? thrown : produced;
    return out;
}

/// The Iterator Record of ECMA-262 7.4.1. `done` is the record's [[Done]]: set
/// by a step that threw or ended, and consulted by IteratorClose.
struct iterator_record {
    value iterator = value::undefined();
    value next = value::undefined();
    bool done = false;
};

// What "did that throw" means in this file. A throw out of `call` is deferred
// (context::throw_pending). A getter or a proxy trap reached through
// lookup_property runs through `call`, so it is deferred too; lookup_property
// on null or undefined throws by itself, which unwinds at once, so a receiver
// is tested for nullish before it is read. context::unwinds is not used after
// a call: a generator's `return()` and a callee's own caught exception both
// unwind without anything having been thrown at the caller, and unwind_watch
// would read either as a throw.

/// GetIteratorDirect, ECMA-262 7.4.4: the object and its `next`, read once.
/// Returns false when the read threw.
[[nodiscard]] inline bool iterator_direct(context & cx, value obj, iterator_record & out) {
    if (obj.is_nullish()) {
        cx.throw_error("TypeError", "Cannot read properties of null or undefined");
        return false;
    }
    out.iterator = obj;
    out.next = cx.lookup_property(obj, "next");
    out.done = false;
    return !cx.throw_pending();
}

/// GetIteratorFlattenable, ECMA-262 7.4.5: a string iterates when `strings`
/// is true, any other primitive is a TypeError, and an object without
/// @@iterator is taken to be an iterator itself.
[[nodiscard]] inline bool iterator_flattenable(context & cx, value obj, bool strings,
                                               iterator_record & out) {
    if (!obj.is_object_like() && !(strings && obj.is_string())) {
        cx.throw_error("TypeError", std::string{context::type_of(obj)} + " is not an iterator");
        return false;
    }
    const value method = cx.lookup_property(obj, CTBROWSER_SYMBOL_KEY("iterator"));
    if (cx.throw_pending()) { return false; }
    value iterator = obj;
    if (!method.is_nullish()) {
        if (!method.is_callable()) {
            cx.throw_error("TypeError", "[Symbol.iterator] is not a function");
            return false;
        }
        iterator = cx.call(method, {}, obj);
        if (cx.throw_pending()) { return false; }
        if (!iterator.is_object_like()) {
            cx.throw_error("TypeError", "Result of the Symbol.iterator method is not an object");
            return false;
        }
    }
    return iterator_direct(cx, iterator, out);
}

/// IteratorStepValue, ECMA-262 7.4.9. Returns false when a throw is in flight,
/// and the record is then done, so no caller closes it. `done` reports the end
/// of the iterator.
[[nodiscard]] inline bool iterator_step_value(context & cx, iterator_record & rec, bool & done,
                                              value & out) {
    done = false;
    out = value::undefined();
    if (!rec.next.is_callable()) {
        rec.done = true;
        cx.throw_error("TypeError", "iterator.next is not a function");
        return false;
    }
    const value result = cx.call(rec.next, {}, rec.iterator);
    if (cx.throw_pending()) {
        rec.done = true;
        return false;
    }
    if (!result.is_object_like()) {
        rec.done = true;
        cx.throw_error("TypeError", "Iterator result is not an object");
        return false;
    }
    const context::rooted keep{cx, result};
    const value finished = cx.lookup_property(result, "done");
    if (cx.throw_pending()) {
        rec.done = true;
        return false;
    }
    if (context::truthy(finished)) {
        rec.done = true;
        done = true;
        return true;
    }
    out = cx.lookup_property(result, "value");
    if (cx.throw_pending()) {
        rec.done = true;
        return false;
    }
    return true;
}

/// IteratorClose, ECMA-262 7.4.11, with a normal (or return) completion:
/// `return()` is called, its throw propagates, and a non-object answer is a
/// TypeError. Returns false when a throw is in flight.
[[nodiscard]] inline bool iterator_close(context & cx, value iterator) {
    if (!iterator.is_object_like()) { return true; }
    const value back = cx.lookup_property(iterator, "return");
    if (cx.throw_pending()) { return false; }
    if (back.is_nullish()) { return true; }
    if (!back.is_callable()) {
        cx.throw_error("TypeError", "iterator.return is not a function");
        return false;
    }
    const value result = cx.call(back, {}, iterator);
    if (cx.throw_pending()) { return false; }
    if (!result.is_object_like()) {
        cx.throw_error("TypeError", "iterator.return() did not return an object");
        return false;
    }
    return true;
}

/// IteratorClose with a throw completion the caller is holding: `return()` is
/// called and whatever it does (throw or answer) is discarded, because the
/// original throw wins. Must run with no throw pending: the caller holds the
/// thrown value and rethrows it afterwards with throw_value.
inline void iterator_close_quietly(context & cx, value iterator) {
    const completion ignored = fenced(cx, [iterator](context & c) -> value {
        (void)iterator_close(c, iterator);
        return value::undefined();
    });
    (void)ignored;
}

/// PerformPromiseThen(promise, onFulfilled, onRejected) with no result
/// capability, which is what Await is made of. Defined in
/// ../async/promise_reactions.cpp with the rest of the promise machinery;
/// disposable.cpp's async walk waits on it.
void perform_promise_then(context & cx, value promise, value on_ok, value on_err);

} // namespace ctbrowser::script::detail
