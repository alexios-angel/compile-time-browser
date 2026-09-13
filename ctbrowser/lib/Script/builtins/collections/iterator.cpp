// ctbrowser.script builtins - Iterator and the iterator helpers (ES2025 27.1.3
// and 27.1.4), plus the ES2026 additions the corpus carries: Iterator.concat,
// chunks, windows, includes, join and @@dispose.
//
// New on 2026-09-12. Before it there was no `Iterator` global at all and no
// %IteratorPrototype%: a generator object's prototype chain ended at the
// generator table, so `gen().map(f)` found nothing. The table built here is
// spliced under %GeneratorPrototype% (install_generator's, objects/function.cpp)
// so a generator is an Iterator, which is what 27.5.1 says.

#include "../objects/internal.hpp" // key_value, for zipKeyed
#include "iterator_internal.hpp"

namespace ctbrowser::script::builtins_detail {

using detail::iterator_record;

namespace {

// %IteratorHelperPrototype% objects (27.1.2.1) are ordinary objects with a
// private-keyed state slot: the underlying Iterator Record, the mapper or
// predicate, the counter, and the generator-like state of 27.1.2.1.1.
constexpr std::string_view helper_slot = "@#IteratorHelper";
constexpr std::string_view wrapped_slot = "@#Iterated";

enum class helper_kind : std::uint8_t {
    map,
    filter,
    take,
    drop,
    flat_map,
    concat,
    chunks,
    windows,
    zip
};
enum class gen_state : std::uint8_t {
    suspended_start,
    suspended_yield,
    executing,
    completed
};

[[nodiscard]] value slot(object_object * o, std::string_view name) {
    const value * held = o->find(name);
    return held == nullptr ? value::undefined() : *held;
}

[[nodiscard]] object_object * helper_state(context & cx, value self, const char * method) {
    if (self.is_object()) {
        const value * held = static_cast<object_object *>(self.as_heap())->find(helper_slot);
        if (held != nullptr && held->is_object()) {
            return static_cast<object_object *>(held->as_heap());
        }
    }
    cx.throw_error("TypeError", std::string{"%IteratorHelperPrototype%."} + method +
                                    " called on a non-Iterator Helper");
    return nullptr;
}

[[nodiscard]] iterator_record record_of(object_object * state, const char * prefix) {
    iterator_record rec;
    rec.iterator = slot(state, std::string{prefix} + "Iterator");
    rec.next = slot(state, std::string{prefix} + "Next");
    rec.done = context::truthy(slot(state, std::string{prefix} + "Done"));
    return rec;
}
void store_record(object_object * state, const char * prefix, const iterator_record & rec) {
    state->set(std::string{prefix} + "Iterator", rec.iterator);
    state->set(std::string{prefix} + "Next", rec.next);
    state->set(std::string{prefix} + "Done", value::boolean(rec.done));
}

// A step of the underlying iterator, with the record written back. `false`
// is a throw in flight (the record is done: nothing closes it).
[[nodiscard]] bool step_underlying(context & cx, object_object * state, const char * prefix,
                                   bool & done, value & out) {
    iterator_record rec = record_of(state, prefix);
    const bool ok = detail::iterator_step_value(cx, rec, done, out);
    store_record(state, prefix, rec);
    return ok;
}

// A call whose abrupt completion CLOSES the underlying iterator first
// (IfAbruptCloseIterator, 7.4.12): fenced, then `return()` quietly, then the
// original throw again.
[[nodiscard]] bool call_or_close(context & cx, value fn, std::span<const value> args, value closing,
                                 value & out) {
    bool threw = false;
    value thrown = value::undefined();
    out = cx.call_fenced(fn, args, value::undefined(), threw, thrown);
    if (!threw) { return true; }
    const context::rooted keep{cx, thrown};
    detail::iterator_close_quietly(cx, closing);
    cx.throw_value(thrown);
    return false;
}

[[nodiscard]] double counter_of(object_object * state) {
    return slot(state, "counter").as_number();
}
void bump_counter(object_object * state) {
    state->set("counter", value::number(counter_of(state) + 1.0));
}

// IteratorCloseAll over what a helper holds open: zip's open rows in
// reverse, or the one underlying iterator. With `quietly` a throw is already
// in hand and every `return()`'s outcome is discarded; otherwise the first
// throw out of a `return()` becomes the completion (and the rest are quiet).
// False with that throw in flight.
[[nodiscard]] bool close_open(context & cx, object_object * state, bool quietly, value) {
    std::vector<value> open;
    const value list = slot(state, "iters");
    if (list.is_array()) {
        for (const value & row : static_cast<array_object *>(list.as_heap())->items) {
            auto & cells = static_cast<array_object *>(row.as_heap())->items;
            if (context::truthy(cells[2])) {
                open.push_back(cells[0]);
                cells[2] = value::boolean(false);
            }
        }
    } else {
        open.push_back(slot(state, "Iterator"));
    }
    const context::rooted_values keep{cx, open};
    bool threw = false;
    value thrown = value::undefined();
    for (std::size_t i = open.size(); i-- > 0;) {
        if (quietly || threw) {
            detail::iterator_close_quietly(cx, open[i]);
            continue;
        }
        const value iterator = open[i];
        const detail::completion closed = detail::fenced(cx, [iterator](context & c) -> value {
            return value::boolean(detail::iterator_close(c, iterator));
        });
        if (closed.threw) {
            threw = true;
            thrown = closed.result;
        }
    }
    if (threw) {
        cx.throw_value(thrown);
        return false;
    }
    return true;
}

// ONE STEP OF EACH HELPER'S ABSTRACT CLOSURE (27.1.4.x): the record it
// yields, or `done` set with undefined. False is a throw in flight, and every
// close the specification asks for has already happened.
[[nodiscard]] bool helper_step(context & cx, helper_kind kind, object_object * state, bool & done,
                               value & out) {
    done = false;
    const value iterated = slot(state, "Iterator");
    switch (kind) {
    case helper_kind::map: {
        value item = value::undefined();
        if (!step_underlying(cx, state, "", done, item)) { return false; }
        if (done) { return true; }
        const context::rooted keep{cx, item};
        const value args[2] = {item, value::number(counter_of(state))};
        if (!call_or_close(cx, slot(state, "fn"), args, iterated, out)) { return false; }
        bump_counter(state);
        return true;
    }
    case helper_kind::filter: {
        for (;;) {
            value item = value::undefined();
            if (!step_underlying(cx, state, "", done, item)) { return false; }
            if (done) { return true; }
            const context::rooted keep{cx, item};
            const value args[2] = {item, value::number(counter_of(state))};
            value selected = value::undefined();
            if (!call_or_close(cx, slot(state, "fn"), args, iterated, selected)) { return false; }
            bump_counter(state);
            if (context::truthy(selected)) {
                out = item;
                return true;
            }
        }
    }
    case helper_kind::take: {
        const double remaining = slot(state, "remaining").as_number();
        if (remaining == 0) {
            done = true;
            store_record(state, "", iterator_record{iterated, value::undefined(), true});
            return detail::iterator_close(cx, iterated);
        }
        if (std::isfinite(remaining)) { state->set("remaining", value::number(remaining - 1)); }
        return step_underlying(cx, state, "", done, out);
    }
    case helper_kind::drop: {
        double remaining = slot(state, "remaining").as_number();
        while (remaining > 0) {
            if (std::isfinite(remaining)) {
                remaining -= 1;
                state->set("remaining", value::number(remaining));
            }
            value skipped = value::undefined();
            if (!step_underlying(cx, state, "", done, skipped)) { return false; }
            if (done) { return true; }
        }
        return step_underlying(cx, state, "", done, out);
    }
    case helper_kind::flat_map: {
        for (;;) {
            if (context::truthy(slot(state, "innerAlive"))) {
                iterator_record inner = record_of(state, "inner");
                bool inner_done = false;
                value item = value::undefined();
                const bool ok = detail::iterator_step_value(cx, inner, inner_done, item);
                store_record(state, "inner", inner);
                if (!ok) {
                    // IfAbruptCloseIterator(innerValue, iterated): the outer
                    // is closed with the inner's throw in hand.
                    // The throw is parked or landed: close under a fence
                    // cannot run, so the outer is closed once the value is
                    // ours - see the caller (helper_next), which is why the
                    // record is marked here.
                    state->set("closeOuter", value::boolean(true));
                    return false;
                }
                if (!inner_done) {
                    out = item;
                    return true;
                }
                state->set("innerAlive", value::boolean(false));
                bump_counter(state);
            }
            value item = value::undefined();
            if (!step_underlying(cx, state, "", done, item)) { return false; }
            if (done) { return true; }
            const context::rooted keep{cx, item};
            const value args[2] = {item, value::number(counter_of(state))};
            value mapped = value::undefined();
            if (!call_or_close(cx, slot(state, "fn"), args, iterated, mapped)) { return false; }
            const context::rooted keep_mapped{cx, mapped};
            // GetIteratorFlattenable(mapped, reject-primitives), its throw
            // closing the outer too.
            iterator_record inner;
            const detail::completion opened = detail::fenced(cx, [&](context & c) -> value {
                if (!detail::iterator_flattenable(c, mapped, false, inner)) {
                    return value::undefined();
                }
                return value::boolean(true);
            });
            if (opened.threw) {
                const context::rooted keep_thrown{cx, opened.result};
                detail::iterator_close_quietly(cx, iterated);
                cx.throw_value(opened.result);
                return false;
            }
            store_record(state, "inner", inner);
            state->set("innerAlive", value::boolean(true));
        }
    }
    case helper_kind::concat: {
        // Iterator.concat's closure (ES2026 27.1.3.2.1): each (iterable,
        // method) pair in turn, opened when reached.
        for (;;) {
            if (context::truthy(slot(state, "innerAlive"))) {
                iterator_record inner = record_of(state, "inner");
                bool inner_done = false;
                value item = value::undefined();
                const bool ok = detail::iterator_step_value(cx, inner, inner_done, item);
                store_record(state, "inner", inner);
                if (!ok) { return false; }
                if (!inner_done) {
                    out = item;
                    return true;
                }
                state->set("innerAlive", value::boolean(false));
            }
            const value list = slot(state, "iterables");
            auto & items = static_cast<array_object *>(list.as_heap())->items;
            const auto at = static_cast<std::size_t>(counter_of(state));
            if (at >= items.size()) {
                done = true;
                return true;
            }
            bump_counter(state);
            auto * pair = static_cast<array_object *>(items[at].as_heap());
            const value iterable = pair->items[0];
            const value method = pair->items[1];
            const value iterator = cx.call(method, {}, iterable);
            if (cx.throw_pending()) { return false; }
            if (!iterator.is_object_like()) {
                cx.throw_error("TypeError",
                               "Result of the Symbol.iterator method is not an object");
                return false;
            }
            iterator_record inner;
            if (!detail::iterator_direct(cx, iterator, inner)) { return false; }
            store_record(state, "inner", inner);
            state->set("innerAlive", value::boolean(true));
        }
    }
    case helper_kind::zip: {
        // IteratorZip's closure (joint iteration, Iterator.zip / zipKeyed):
        // one value from every open iterator per step. `iters` holds
        // [iterator, next, open] rows; `mode` is 0 shortest, 1 longest, 2
        // strict; `padding` fills a finished iterator under "longest".
        const value list = slot(state, "iters");
        auto & rows = static_cast<array_object *>(list.as_heap())->items;
        const std::size_t count = rows.size();
        if (count == 0) {
            done = true;
            return true;
        }
        const int mode = static_cast<int>(slot(state, "mode").as_number());
        const value results = cx.make_array();
        const context::rooted keep_results{cx, results};
        auto * collected = static_cast<array_object *>(results.as_heap());
        const auto open_count = [&] {
            std::size_t n = 0;
            for (const value & row : rows) {
                if (context::truthy(static_cast<array_object *>(row.as_heap())->items[2])) { ++n; }
            }
            return n;
        };
        for (std::size_t i = 0; i < count; ++i) {
            auto & row = static_cast<array_object *>(rows[i].as_heap())->items;
            value result = value::undefined();
            if (!context::truthy(row[2])) {
                result = detail::element_at(cx, slot(state, "padding"), static_cast<double>(i));
            } else {
                iterator_record rec{row[0], row[1], false};
                bool finished = false;
                if (!detail::iterator_step_value(cx, rec, finished, result)) {
                    row[2] = value::boolean(false);
                    state->set("closeAll", value::boolean(true));
                    return false;
                }
                if (finished) {
                    row[2] = value::boolean(false);
                    if (mode == 0) {
                        done = true;
                        return close_open(cx, state, false, value::undefined());
                    }
                    if (mode == 2) {
                        if (i != 0) {
                            (void)close_open(cx, state, true, value::undefined());
                            cx.throw_error("TypeError",
                                           "Iterator.zip: iterators of unequal length");
                            return false;
                        }
                        // Every other iterator must be done too - IteratorStep,
                        // which reads no `value`.
                        for (std::size_t k = 1; k < count; ++k) {
                            auto & other = static_cast<array_object *>(rows[k].as_heap())->items;
                            const value next = other[1];
                            const value iterator = other[0];
                            bool other_done = false;
                            const detail::completion stepped =
                                detail::fenced(cx, [&](context & c) -> value {
                                    if (!next.is_callable()) {
                                        c.throw_error("TypeError",
                                                      "iterator.next is not a function");
                                        return value::undefined();
                                    }
                                    const value r = c.call(next, {}, iterator);
                                    if (c.throw_pending()) { return value::undefined(); }
                                    if (!r.is_object_like()) {
                                        c.throw_error("TypeError",
                                                      "Iterator result is not an object");
                                        return value::undefined();
                                    }
                                    other_done = context::truthy(c.lookup_property(r, "done"));
                                    return value::undefined();
                                });
                            if (stepped.threw) {
                                other[2] = value::boolean(false);
                                const context::rooted keep{cx, stepped.result};
                                (void)close_open(cx, state, true, value::undefined());
                                cx.throw_value(stepped.result);
                                return false;
                            }
                            if (!other_done) {
                                (void)close_open(cx, state, true, value::undefined());
                                cx.throw_error("TypeError",
                                               "Iterator.zip: iterators of unequal length");
                                return false;
                            }
                            other[2] = value::boolean(false);
                        }
                        done = true;
                        return true;
                    }
                    if (open_count() == 0) {
                        done = true;
                        return true;
                    }
                    result = detail::element_at(cx, slot(state, "padding"), static_cast<double>(i));
                }
            }
            collected->items.push_back(result);
        }
        const value keys = slot(state, "keys");
        if (!keys.is_array()) {
            out = results;
            return true;
        }
        // zipKeyed's finishResults: a null-prototype object, one own data
        // property per key.
        object_object * record = detail::new_table(cx);
        record->prototype = value::undefined(); // explicit null, see object_object::prototype
        const auto & names = static_cast<array_object *>(keys.as_heap())->items;
        for (std::size_t i = 0; i < names.size() && i < collected->items.size(); ++i) {
            record->define(cx.to_string(names[i]), collected->items[i], attr_default);
        }
        out = value::object(record);
        return true;
    }
    case helper_kind::chunks:
    case helper_kind::windows: {
        // Iterator chunking: `chunks(n)` yields every n values as an array,
        // the last shorter; `windows(n)` yields every n consecutive values,
        // nothing at all when there are fewer than n.
        const auto size = static_cast<std::size_t>(slot(state, "remaining").as_number());
        value buffer = slot(state, "buffer");
        if (!buffer.is_array()) {
            buffer = cx.make_array();
            state->set("buffer", buffer);
        }
        auto * held = static_cast<array_object *>(buffer.as_heap());
        for (;;) {
            value item = value::undefined();
            if (!step_underlying(cx, state, "", done, item)) { return false; }
            if (done) {
                // A short final chunk always; a short final window only under
                // "allow-partial", and only when no full one was ever yielded
                // (the buffer is then still shorter than the size).
                const bool partial = context::truthy(slot(state, "partial"));
                if (!held->items.empty() &&
                    (kind == helper_kind::chunks || (partial && held->items.size() < size))) {
                    done = false;
                    out = buffer;
                    state->set("buffer", cx.make_array());
                }
                return true;
            }
            // A window keeps its last `size` values BETWEEN steps (the oldest
            // goes as the next arrives), so at exhaustion the buffer is only
            // shorter than the size when no full window was ever yielded.
            if (kind == helper_kind::windows && held->items.size() == size) {
                held->items.erase(held->items.begin());
            }
            held->items.push_back(item);
            if (held->items.size() < size) { continue; }
            const value chunk = cx.make_array();
            auto * chunk_items = static_cast<array_object *>(chunk.as_heap());
            chunk_items->items = held->items;
            if (kind == helper_kind::chunks) { held->items.clear(); }
            out = chunk;
            return true;
        }
    }
    }
    return true;
}

[[nodiscard]] gen_state state_of(object_object * state) {
    return static_cast<gen_state>(static_cast<std::uint8_t>(slot(state, "state").as_number()));
}
void set_state(object_object * state, gen_state s) {
    state->set("state", value::number(static_cast<double>(static_cast<std::uint8_t>(s))));
}

// %IteratorHelperPrototype%.next, 27.1.2.1.1 - GeneratorResume over the
// closure: running is a TypeError, completed is done, otherwise one step.
[[nodiscard]] value helper_next(context & cx) {
    const value self = cx.current_this();
    object_object * state = helper_state(cx, self, "next");
    if (state == nullptr) { return value::undefined(); }
    switch (state_of(state)) {
    case gen_state::executing:
        cx.throw_error("TypeError", "Generator is already running");
        return value::undefined();
    case gen_state::completed: return cx.iter_result(value::undefined(), true);
    default: break;
    }
    set_state(state, gen_state::executing);
    const auto kind =
        static_cast<helper_kind>(static_cast<std::uint8_t>(slot(state, "kind").as_number()));
    bool done = false;
    value out = value::undefined();
    // Under a fence, so an abrupt step's value is in hand for the one close
    // that has to run AFTER the throw and BEFORE it propagates (flatMap's
    // inner step failing closes the outer).
    const detail::completion stepped = detail::fenced(cx, [&](context & c) -> value {
        return value::boolean(helper_step(c, kind, state, done, out));
    });
    if (stepped.threw) {
        set_state(state, gen_state::completed);
        const context::rooted keep{cx, stepped.result};
        if (context::truthy(slot(state, "closeOuter"))) {
            state->set("closeOuter", value::boolean(false));
            detail::iterator_close_quietly(cx, slot(state, "Iterator"));
        }
        if (context::truthy(slot(state, "closeAll"))) {
            state->set("closeAll", value::boolean(false));
            (void)close_open(cx, state, true, value::undefined());
        }
        cx.throw_value(stepped.result);
        return value::undefined();
    }
    if (done) {
        set_state(state, gen_state::completed);
        return cx.iter_result(value::undefined(), true);
    }
    set_state(state, gen_state::suspended_yield);
    return cx.iter_result(out, false);
}

// %IteratorHelperPrototype%.return, 27.1.2.1.2: GeneratorResumeAbrupt with a
// return completion - the closures all answer it by closing what they hold,
// inner iterator first.
[[nodiscard]] value helper_return(context & cx) {
    const value self = cx.current_this();
    object_object * state = helper_state(cx, self, "return");
    if (state == nullptr) { return value::undefined(); }
    switch (state_of(state)) {
    case gen_state::executing:
        cx.throw_error("TypeError", "Generator is already running");
        return value::undefined();
    case gen_state::completed: return cx.iter_result(value::undefined(), true);
    default: break;
    }
    // suspended-start completes BEFORE the close (27.1.2.1.2 step 4: a
    // re-entrant `next()` from `return()` then answers done); suspended-yield
    // is GeneratorResumeAbrupt, EXECUTING while the closure closes what it
    // holds (a re-entrant `next()` is then the TypeError), completed after.
    const bool started = state_of(state) == gen_state::suspended_yield;
    set_state(state, started ? gen_state::executing : gen_state::completed);
    const auto finish = [&](value out) {
        set_state(state, gen_state::completed);
        return out;
    };
    if (slot(state, "iters").is_array()) {
        if (!close_open(cx, state, false, value::undefined())) {
            return finish(value::undefined());
        }
        return finish(cx.iter_result(value::undefined(), true));
    }
    const value iterated = slot(state, "Iterator");
    if (context::truthy(slot(state, "innerAlive"))) {
        state->set("innerAlive", value::boolean(false));
        const value inner = slot(state, "innerIterator");
        const detail::completion closed = detail::fenced(cx, [inner](context & c) -> value {
            return value::boolean(detail::iterator_close(c, inner));
        });
        if (closed.threw) {
            // IfAbruptCloseIterator(backupCompletion, iterated)
            const context::rooted keep{cx, closed.result};
            detail::iterator_close_quietly(cx, iterated);
            set_state(state, gen_state::completed);
            cx.throw_value(closed.result);
            return value::undefined();
        }
    }
    if (!detail::iterator_close(cx, iterated)) { return finish(value::undefined()); }
    return finish(cx.iter_result(value::undefined(), true));
}

// The helper object over a state record whose underlying record is already
// filled in.
[[nodiscard]] value make_helper(context & cx, object_object * helper_proto, helper_kind kind,
                                object_object * state) {
    state->set("kind", value::number(static_cast<double>(static_cast<std::uint8_t>(kind))));
    state->set("counter", value::number(0));
    set_state(state, gen_state::suspended_start);
    object_object * made = detail::new_table(cx);
    made->prototype = value::object(helper_proto);
    made->define(helper_slot, value::object(state), attr_none);
    return value::object(made);
}

// A limit argument of take / drop / chunks / windows: ToNumber, then the
// RangeErrors of 27.1.4.4 - and every refusal closes the receiver first
// (IteratorClose with the error), because the receiver is an Iterator Record
// from step 3 on. `whole` demands a positive integer below 2^32 (chunking).
[[nodiscard]] bool limit_arg(context & cx, value self, value raw, bool whole, double & out) {
    const detail::completion converted = detail::fenced(cx, [raw](context & c) -> value {
        if (!numeric_arg(c, raw)) { return value::undefined(); }
        return value::number(c.to_number_value(raw));
    });
    if (converted.threw) {
        const context::rooted keep{cx, converted.result};
        detail::iterator_close_quietly(cx, self);
        cx.throw_value(converted.result);
        return false;
    }
    const double n = converted.result.as_number();
    const bool bad =
        whole ? (std::isnan(n) || n < 1 || std::trunc(n) != n || n > 4294967295.0)
              : (std::isnan(n) || (std::isfinite(n) && n > max_safe_integer) || std::trunc(n) < 0);
    if (bad) {
        detail::iterator_close_quietly(cx, self);
        cx.throw_error("RangeError", "limit must be a non-negative number");
        return false;
    }
    out = std::isnan(n) ? 0 : std::trunc(n);
    return true;
}

// The size of `chunks` / `windows` (iterator chunking): NO coercion - not a
// Number or not integral is a TypeError, outside [1, 2^32-1] a RangeError -
// and every refusal closes the receiver.
[[nodiscard]] bool size_arg(context & cx, value self, value raw, double & out) {
    if (!raw.is_number() || std::trunc(raw.as_number()) != raw.as_number()) {
        detail::iterator_close_quietly(cx, self);
        cx.throw_error("TypeError", "size must be an integral Number");
        return false;
    }
    const double n = raw.as_number();
    if (n < 1 || n > 4294967295.0) {
        detail::iterator_close_quietly(cx, self);
        cx.throw_error("RangeError", "size must be between 1 and 2^32 - 1");
        return false;
    }
    out = n;
    return true;
}

// `includes`' skippedElements: undefined is 0; otherwise +-Infinity or an
// integral Number (else TypeError), not negative and not past 2^53-1 (else
// RangeError); every refusal closes the receiver.
[[nodiscard]] bool skip_arg(context & cx, value self, value raw, double & out) {
    if (raw.is_undefined()) {
        out = 0;
        return true;
    }
    if (!raw.is_number() ||
        (std::isfinite(raw.as_number()) && std::trunc(raw.as_number()) != raw.as_number()) ||
        std::isnan(raw.as_number())) {
        detail::iterator_close_quietly(cx, self);
        cx.throw_error("TypeError", "skippedElements must be an integral Number");
        return false;
    }
    const double n = raw.as_number();
    if (n < 0 || (std::isfinite(n) && n > max_safe_integer)) {
        detail::iterator_close_quietly(cx, self);
        cx.throw_error("RangeError", "skippedElements is out of range");
        return false;
    }
    out = n;
    return true;
}

// A callable argument, with the same close-on-refusal.
[[nodiscard]] bool callable_or_close(context & cx, value self, value fn, const char * what) {
    if (fn.is_callable()) { return true; }
    detail::iterator_close_quietly(cx, self);
    cx.throw_error("TypeError", std::string{what} + " is not a function");
    return false;
}

// SetterThatIgnoresPrototypeProperties, 27.1.3.2.1.1: the accessor's setter
// writes an OWN property on the receiver rather than touching the prototype.
[[nodiscard]] value ignoring_setter(context & cx, object_object * home, const std::string & key,
                                    value v) {
    const value self = cx.current_this();
    if (!self.is_object_like()) {
        cx.throw_error("TypeError", "Iterator.prototype setter called on a non-object");
        return value::undefined();
    }
    if (self.is_object() && self.as_heap() == home) {
        cx.throw_error("TypeError", "Cannot assign to read only property of Iterator.prototype");
        return value::undefined();
    }
    context::property_descriptor found;
    if (!cx.own_property(self, key, found)) {
        context::property_descriptor wanted;
        wanted.has_value = wanted.has_writable = wanted.has_enumerable = true;
        wanted.has_configurable = true;
        wanted.held = v;
        wanted.writable = wanted.enumerable = wanted.configurable = true;
        if (!cx.define_own_property(self, key, wanted) && !cx.throw_pending()) {
            cx.throw_error("TypeError", "Cannot define property " + key);
        }
        return value::undefined();
    }
    cx.clear_store_rejected();
    cx.store_property(self, key, v);
    if (!cx.throw_pending()) { cx.strict_store_check(key); }
    return value::undefined();
}

} // namespace

void install_iterator(context & cx) {
    using detail::method;
    using detail::new_table;

    // %Iterator.prototype%, 27.1.3.2 - and %GeneratorPrototype% is re-parented
    // onto it, so a generator object IS an Iterator.
    object_object * proto = new_table(cx);
    if (object_object * generators = cx.prototype(context::proto_kind::generator)) {
        generators->prototype = value::object(proto);
    }
    // %IteratorHelperPrototype%, 27.1.2.1.
    object_object * helper_proto = new_table(cx);
    helper_proto->prototype = value::object(proto);
    method(cx, helper_proto, "next", 0,
           [](context & c, std::span<value>) { return helper_next(c); });
    method(cx, helper_proto, "return", 0,
           [](context & c, std::span<value>) { return helper_return(c); });
    helper_proto->define("@@toStringTag", cx.string("Iterator Helper"), attr_configurable);
    const value helper_proto_value = value::object(helper_proto);
    // %WrapForValidIteratorPrototype%, 27.1.3.2.1.2: what Iterator.from hands
    // back for an iterator that is not already an Iterator - `next` and
    // `return` forwarded to the wrapped record.
    object_object * wrap_proto = new_table(cx);
    wrap_proto->prototype = value::object(proto);
    const auto wrapped = [](context & c, const char * name) -> object_object * {
        const value self = c.current_this();
        if (self.is_object()) {
            const value * held = static_cast<object_object *>(self.as_heap())->find(wrapped_slot);
            if (held != nullptr && held->is_object()) {
                return static_cast<object_object *>(held->as_heap());
            }
        }
        c.throw_error("TypeError", std::string{"%WrapForValidIteratorPrototype%."} + name +
                                       " called on an incompatible receiver");
        return nullptr;
    };
    method(cx, wrap_proto, "next", 0, [wrapped](context & c, std::span<value>) {
        object_object * record = wrapped(c, "next");
        if (record == nullptr) { return value::undefined(); }
        return c.call(slot(record, "Next"), {}, slot(record, "Iterator"));
    });
    method(cx, wrap_proto, "return", 0, [wrapped](context & c, std::span<value>) {
        object_object * record = wrapped(c, "return");
        if (record == nullptr) { return value::undefined(); }
        const value iterator = slot(record, "Iterator");
        const value back = c.lookup_property(iterator, "return");
        if (c.throw_pending()) { return value::undefined(); }
        if (back.is_nullish()) { return c.iter_result(value::undefined(), true); }
        if (!back.is_callable()) {
            c.throw_error("TypeError", "iterator.return is not a function");
            return value::undefined();
        }
        return c.call(back, {}, iterator);
    });
    const value wrap_proto_value = value::object(wrap_proto);

    // 27.1.3.1 Iterator(): abstract - `new Iterator()` itself is a TypeError,
    // only a subclass's `super()` reaches here with an instance to accept.
    auto * ctor = cx.allocate<native_object>("Iterator", [proto](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!detail::constructing_this(self)) {
            c.throw_error("TypeError", "Constructor Iterator requires 'new'");
            return value::undefined();
        }
        auto * made = static_cast<object_object *>(self.as_heap());
        if (!made->prototype.is_object_like() || made->prototype.as_heap() == proto) {
            c.throw_error("TypeError", "Abstract class Iterator not directly constructable");
            return value::undefined();
        }
        return self;
    });
    detail::constant(ctor, "prototype", value::object(proto));
    ctor->define("length", value::number(0), attr_configurable);
    ctor->define("name", cx.string("Iterator"), attr_configurable);
    const value ctor_value = value::object(ctor);
    ctor->retained.push_back(helper_proto_value);
    ctor->retained.push_back(wrap_proto_value);

    // 27.1.3.2.3 Iterator.from
    method(cx, ctor, "from", 1, [proto, wrap_proto_value](context & c, std::span<value> a) {
        iterator_record rec;
        if (!detail::iterator_flattenable(c, arg_at(a, 0), true, rec)) {
            return value::undefined();
        }
        // OrdinaryHasInstance(%Iterator%, iterator): already an Iterator
        // when %Iterator.prototype% is on its chain.
        value walk = c.get_prototype(rec.iterator);
        for (int hops = 0; hops < 64 && walk.is_object_like(); ++hops) {
            if (walk.as_heap() == proto) { return rec.iterator; }
            walk = c.get_prototype(walk);
            if (c.throw_pending()) { return value::undefined(); }
        }
        object_object * record = new_table(c);
        record->set("Iterator", rec.iterator);
        record->set("Next", rec.next);
        object_object * made = new_table(c);
        made->prototype = wrap_proto_value;
        made->define(wrapped_slot, value::object(record), attr_none);
        return value::object(made);
    });
    // Iterator.concat (ES2026): every argument is validated - an object with
    // a callable @@iterator - BEFORE anything is opened.
    method(cx, ctor, "concat", 0, [helper_proto](context & c, std::span<value> a) {
        const value list = c.make_array();
        const context::rooted keep{c, list};
        auto * pairs = static_cast<array_object *>(list.as_heap());
        for (const value & item : a) {
            if (!item.is_object_like()) {
                c.throw_error("TypeError", "Iterator.concat: argument is not an object");
                return value::undefined();
            }
            const value method = c.lookup_property(item, "@@iterator");
            if (c.throw_pending()) { return value::undefined(); }
            if (!method.is_callable()) {
                c.throw_error("TypeError", "Iterator.concat: argument is not iterable");
                return value::undefined();
            }
            const value pair = c.make_array();
            auto * held = static_cast<array_object *>(pair.as_heap());
            held->items.push_back(item);
            held->items.push_back(method);
            pairs->items.push_back(pair);
        }
        object_object * state = new_table(c);
        state->set("iterables", list);
        state->set("innerAlive", value::boolean(false));
        // No underlying record of its own: `return` closes the open inner
        // one, and the outer close finds nothing to call.
        state->set("Iterator", c.make_object());
        return make_helper(c, helper_proto, helper_kind::concat, state);
    });

    // Iterator.zip / Iterator.zipKeyed (joint iteration): the options are
    // read first, then every iterable is opened - an abrupt step closing
    // what is already open - then the padding, and the helper does the rest.
    const auto zip = [&](const char * name, bool keyed) {
        method(cx, ctor, name, 1, [helper_proto, keyed](context & c, std::span<value> a) {
            const value iterables = arg_at(a, 0);
            if (!iterables.is_object_like()) {
                c.throw_error("TypeError", "Iterator.zip: iterables is not an object");
                return value::undefined();
            }
            const value options = arg_at(a, 1);
            int mode = 0;
            value padding_option = value::undefined();
            if (!options.is_undefined()) {
                if (!options.is_object_like()) {
                    c.throw_error("TypeError", "Iterator.zip: options is not an object");
                    return value::undefined();
                }
                const value wanted = c.lookup_property(options, "mode");
                if (c.throw_pending()) { return value::undefined(); }
                std::string spelled = "shortest";
                if (!wanted.is_undefined()) {
                    if (!wanted.is_string()) {
                        c.throw_error("TypeError", "Iterator.zip: invalid mode");
                        return value::undefined();
                    }
                    spelled = c.to_string(wanted);
                }
                mode = spelled == "shortest"  ? 0
                       : spelled == "longest" ? 1
                       : spelled == "strict"  ? 2
                                              : -1;
                if (mode < 0) {
                    c.throw_error("TypeError", "Iterator.zip: invalid mode");
                    return value::undefined();
                }
                if (mode == 1) {
                    padding_option = c.lookup_property(options, "padding");
                    if (c.throw_pending()) { return value::undefined(); }
                    if (!padding_option.is_undefined() && !padding_option.is_object_like()) {
                        c.throw_error("TypeError", "Iterator.zip: padding is not an object");
                        return value::undefined();
                    }
                }
            }
            object_object * state = new_table(c);
            const value state_value = value::object(state);
            const context::rooted keep_state{c, state_value};
            const value rows = c.make_array();
            state->set("iters", rows);
            state->set("mode", value::number(mode));
            state->set("padding", c.make_array());
            auto * held = static_cast<array_object *>(rows.as_heap());
            const value keys = c.make_array();
            if (keyed) { state->set("keys", keys); }
            // A row for one opened iterable; false with the throw in flight
            // and everything so far closed.
            const auto add = [&](context & cc, value item) {
                iterator_record rec;
                if (!detail::iterator_flattenable(cc, item, false, rec)) { return false; }
                const value row = cc.make_array();
                static_cast<array_object *>(row.as_heap())->items = {rec.iterator, rec.next,
                                                                     value::boolean(true)};
                held->items.push_back(row);
                return true;
            };
            const detail::completion opened = detail::fenced(c, [&](context & cc) -> value {
                if (!keyed) {
                    const value input = cc.get_iterator(iterables);
                    if (cc.throw_pending() || !input.is_object_like()) {
                        return value::undefined();
                    }
                    const context::rooted keep_input{cc, input};
                    iterator_record source;
                    if (!detail::iterator_direct(cc, input, source)) { return value::undefined(); }
                    for (;;) {
                        bool finished = false;
                        value item = value::undefined();
                        if (!detail::iterator_step_value(cc, source, finished, item)) {
                            return value::undefined();
                        }
                        if (finished) { break; }
                        const context::rooted keep_item{cc, item};
                        // IfAbruptCloseIterators(iter, « inputIter » + iters):
                        // the source is closed too, quietly, first.
                        const detail::completion row = detail::fenced(
                            cc, [&](context & c3) { return value::boolean(add(c3, item)); });
                        if (row.threw) {
                            // IteratorCloseAll over « inputIter » + iters is in
                            // REVERSE: the opened rows first, the source last.
                            const context::rooted keep_thrown{cc, row.result};
                            (void)close_open(cc, state, true, value::undefined());
                            detail::iterator_close_quietly(cc, input);
                            cc.throw_value(row.result);
                            return value::undefined();
                        }
                    }
                    return value::undefined();
                }
                // zipKeyed: every own ENUMERABLE key, read in OwnPropertyKeys
                // order, its enumerability re-checked at each step.
                for (const std::string & key :
                     detail::own_property_names(cc, iterables, detail::key_filter::all)) {
                    context::property_descriptor found;
                    const bool present = cc.own_property(iterables, key, found);
                    if (cc.throw_pending()) { return value::undefined(); }
                    if (!present || !found.enumerable) { continue; }
                    const value item = cc.lookup_index(iterables, detail::key_value(cc, key));
                    if (cc.throw_pending()) { return value::undefined(); }
                    if (item.is_undefined()) { continue; }
                    const context::rooted keep_item{cc, item};
                    static_cast<array_object *>(keys.as_heap())
                        ->items.push_back(detail::key_value(cc, key));
                    if (!add(cc, item)) { return value::undefined(); }
                }
                return value::undefined();
            });
            const auto fail = [&](value thrown) {
                const context::rooted keep_thrown{c, thrown};
                (void)close_open(c, state, true, value::undefined());
                c.throw_value(thrown);
                return value::undefined();
            };
            if (opened.threw) { return fail(opened.result); }
            const std::size_t count = held->items.size();
            auto * padding = static_cast<array_object *>(slot(state, "padding").as_heap());
            padding->items.assign(count, value::undefined());
            if (mode == 1 && !padding_option.is_undefined()) {
                const detail::completion padded = detail::fenced(c, [&](context & cc) -> value {
                    if (keyed) {
                        const auto & names = static_cast<array_object *>(keys.as_heap())->items;
                        for (std::size_t i = 0; i < count; ++i) {
                            padding->items[i] = cc.lookup_index(padding_option, names[i]);
                            if (cc.throw_pending()) { return value::undefined(); }
                        }
                        return value::undefined();
                    }
                    const value input = cc.get_iterator(padding_option);
                    if (cc.throw_pending() || !input.is_object_like()) {
                        return value::undefined();
                    }
                    const context::rooted keep_input{cc, input};
                    iterator_record source;
                    if (!detail::iterator_direct(cc, input, source)) { return value::undefined(); }
                    bool using_iterator = true;
                    for (std::size_t i = 0; i < count && using_iterator; ++i) {
                        bool finished = false;
                        value item = value::undefined();
                        if (!detail::iterator_step_value(cc, source, finished, item)) {
                            return value::undefined();
                        }
                        if (finished) {
                            using_iterator = false;
                        } else {
                            padding->items[i] = item;
                        }
                    }
                    if (using_iterator && !detail::iterator_close(cc, input)) {
                        return value::undefined();
                    }
                    return value::undefined();
                });
                if (padded.threw) { return fail(padded.result); }
            }
            // No single underlying iterator: `return` and the abrupt paths
            // close the rows.
            return make_helper(c, helper_proto, helper_kind::zip, state);
        });
    };
    zip("zip", false);
    zip("zipKeyed", true);

    // --- the prototype ---------------------------------------------------
    // 27.1.3.2.1 / 27.1.3.2.14: `constructor` and @@toStringTag are ACCESSORS
    // whose setter defines an own property on the receiver.
    const auto accessor_pair = [&](const char * key, value got) {
        const std::string name = key;
        auto * getter = detail::method_native(cx, std::string{"get "} + key,
                                              [got](context &, std::span<value>) { return got; });
        detail::install_arity(cx, getter, 0);
        getter->retained.push_back(got);
        auto * setter = detail::method_native(
            cx, std::string{"set "} + key, [proto, name](context & c, std::span<value> a) {
                return ignoring_setter(c, proto, name, arg_at(a, 0));
            });
        detail::install_arity(cx, setter, 1);
        proto->define_accessor(key, value::object(getter), value::object(setter),
                               attr_configurable);
    };
    accessor_pair("constructor", ctor_value);
    accessor_pair("@@toStringTag", cx.string("Iterator"));
    method(cx, proto, "@@iterator", 0,
           [](context & c, std::span<value>) { return c.current_this(); });

    // The receiver as an object, or the TypeError of step 2.
    const auto object_this = [](context & c, const char * name, value & self) {
        self = c.current_this();
        if (self.is_object_like()) { return true; }
        c.throw_error("TypeError",
                      std::string{"Iterator.prototype."} + name + " called on a non-object");
        return false;
    };
    // A helper-making method taking a callback: map, filter, flatMap.
    const auto lazy_fn = [&](const char * name, helper_kind kind) {
        method(cx, proto, name, 1,
               [object_this, helper_proto, kind, name](context & c, std::span<value> a) {
                   value self = value::undefined();
                   if (!object_this(c, name, self)) { return value::undefined(); }
                   if (!callable_or_close(c, self, arg_at(a, 0), "callback")) {
                       return value::undefined();
                   }
                   iterator_record rec;
                   if (!detail::iterator_direct(c, self, rec)) { return value::undefined(); }
                   object_object * state = new_table(c);
                   store_record(state, "", rec);
                   state->set("fn", a[0]);
                   state->set("innerAlive", value::boolean(false));
                   return make_helper(c, helper_proto, kind, state);
               });
    };
    lazy_fn("map", helper_kind::map);
    lazy_fn("filter", helper_kind::filter);
    lazy_fn("flatMap", helper_kind::flat_map);
    // A helper-making method taking a limit: take, drop, chunks, windows.
    const auto lazy_limit = [&](const char * name, helper_kind kind, bool whole) {
        method(cx, proto, name, 1,
               [object_this, helper_proto, kind, whole, name](context & c, std::span<value> a) {
                   value self = value::undefined();
                   if (!object_this(c, name, self)) { return value::undefined(); }
                   double limit = 0;
                   if (whole ? !size_arg(c, self, arg_at(a, 0), limit)
                             : !limit_arg(c, self, arg_at(a, 0), whole, limit)) {
                       return value::undefined();
                   }
                   bool partial = false;
                   if (kind == helper_kind::windows) {
                       // windows(windowSize [, undersized]): "only-full" (the
                       // default) or "allow-partial", anything else a TypeError.
                       const value undersized = arg_at(a, 1);
                       const std::string mode =
                           undersized.is_string() ? c.to_string(undersized) : std::string{};
                       if (!undersized.is_undefined() && mode != "only-full" &&
                           mode != "allow-partial") {
                           detail::iterator_close_quietly(c, self);
                           c.throw_error("TypeError", "undersized must be \"only-full\" or "
                                                      "\"allow-partial\"");
                           return value::undefined();
                       }
                       partial = mode == "allow-partial";
                   }
                   iterator_record rec;
                   if (!detail::iterator_direct(c, self, rec)) { return value::undefined(); }
                   object_object * state = new_table(c);
                   store_record(state, "", rec);
                   state->set("remaining", value::number(limit));
                   state->set("partial", value::boolean(partial));
                   return make_helper(c, helper_proto, kind, state);
               });
    };
    lazy_limit("take", helper_kind::take, false);
    lazy_limit("drop", helper_kind::drop, false);
    lazy_limit("chunks", helper_kind::chunks, true);
    lazy_limit("windows", helper_kind::windows, true);

    // --- the eager ones: 27.1.3.2.4 every, .7 find, .8 forEach, .10 reduce,
    // .11 some, .13 toArray - one walk each, the callback's throw closing the
    // iterator, an early answer closing it too.
    enum class eager : std::uint8_t {
        every,
        find,
        for_each,
        some,
        to_array,
        includes
    };
    const auto walk = [&](const char * name, eager which, bool takes_fn) {
        method(cx, proto, name, which == eager::to_array ? 0 : 1,
               [object_this, which, takes_fn, name](context & c, std::span<value> a) {
                   value self = value::undefined();
                   if (!object_this(c, name, self)) { return value::undefined(); }
                   const value fn = arg_at(a, 0);
                   if (takes_fn && !callable_or_close(c, self, fn, "callback")) {
                       return value::undefined();
                   }
                   double to_skip = 0;
                   if (which == eager::includes && !skip_arg(c, self, arg_at(a, 1), to_skip)) {
                       return value::undefined();
                   }
                   iterator_record rec;
                   if (!detail::iterator_direct(c, self, rec)) { return value::undefined(); }
                   const value collected = c.make_array();
                   const context::rooted keep{c, collected};
                   auto * items = static_cast<array_object *>(collected.as_heap());
                   for (double counter = 0;; counter += 1) {
                       bool done = false;
                       value item = value::undefined();
                       if (!detail::iterator_step_value(c, rec, done, item)) {
                           return value::undefined();
                       }
                       if (done) { break; }
                       const context::rooted keep_item{c, item};
                       if (which == eager::to_array) {
                           items->items.push_back(item);
                           continue;
                       }
                       if (which == eager::includes) {
                           if (counter < to_skip) { continue; }
                           if (item.same_value_zero(fn)) {
                               if (!detail::iterator_close(c, self)) { return value::undefined(); }
                               return value::boolean(true);
                           }
                           continue;
                       }
                       const value args[2] = {item, value::number(counter)};
                       value answer = value::undefined();
                       if (!call_or_close(c, fn, args, self, answer)) { return value::undefined(); }
                       const bool hit = context::truthy(answer);
                       if ((which == eager::some && hit) || (which == eager::every && !hit) ||
                           (which == eager::find && hit)) {
                           if (!detail::iterator_close(c, self)) { return value::undefined(); }
                           return which == eager::find ? item : value::boolean(hit);
                       }
                   }
                   switch (which) {
                   case eager::every: return value::boolean(true);
                   case eager::some: return value::boolean(false);
                   case eager::includes: return value::boolean(false);
                   case eager::to_array: return collected;
                   default: return value::undefined();
                   }
               });
    };
    walk("every", eager::every, true);
    walk("find", eager::find, true);
    walk("forEach", eager::for_each, true);
    walk("some", eager::some, true);
    walk("toArray", eager::to_array, false);
    walk("includes", eager::includes, false);
    // 27.1.3.2.10 reduce
    method(cx, proto, "reduce", 1, [object_this](context & c, std::span<value> a) {
        value self = value::undefined();
        if (!object_this(c, "reduce", self)) { return value::undefined(); }
        const value fn = arg_at(a, 0);
        if (!callable_or_close(c, self, fn, "reducer")) { return value::undefined(); }
        iterator_record rec;
        if (!detail::iterator_direct(c, self, rec)) { return value::undefined(); }
        value accumulator = value::undefined();
        double counter = 0;
        if (a.size() < 2) {
            bool done = false;
            if (!detail::iterator_step_value(c, rec, done, accumulator)) {
                return value::undefined();
            }
            if (done) {
                c.throw_error("TypeError", "Reduce of empty iterator with no initial value");
                return value::undefined();
            }
            counter = 1;
        } else {
            accumulator = a[1];
        }
        for (;; counter += 1) {
            const context::rooted keep{c, accumulator};
            bool done = false;
            value item = value::undefined();
            if (!detail::iterator_step_value(c, rec, done, item)) { return value::undefined(); }
            if (done) { return accumulator; }
            const context::rooted keep_item{c, item};
            const value args[3] = {accumulator, item, value::number(counter)};
            if (!call_or_close(c, fn, args, self, accumulator)) { return value::undefined(); }
        }
    });
    // Iterator.prototype.join (ES2026): ToString of each value - undefined
    // and null as "" - between copies of the separator, "," by default.
    method(cx, proto, "join", 1, [object_this](context & c, std::span<value> a) {
        value self = value::undefined();
        if (!object_this(c, "join", self)) { return value::undefined(); }
        std::string separator = ",";
        if (!arg_at(a, 0).is_undefined()) {
            const detail::completion sep = detail::fenced(
                c, [&a](context & cc) -> value { return cc.string(str_at(cc, a, 0)); });
            if (sep.threw) {
                detail::iterator_close_quietly(c, self);
                c.throw_value(sep.result);
                return value::undefined();
            }
            separator = c.to_string(sep.result);
        }
        iterator_record rec;
        if (!detail::iterator_direct(c, self, rec)) { return value::undefined(); }
        std::string out;
        for (bool first = true;; first = false) {
            bool done = false;
            value item = value::undefined();
            if (!detail::iterator_step_value(c, rec, done, item)) { return value::undefined(); }
            if (done) { break; }
            if (!first) { out += separator; }
            if (item.is_nullish()) { continue; }
            const context::rooted keep{c, item};
            const detail::completion text = detail::fenced(
                c, [item](context & cc) -> value { return cc.string(string_arg(cc, item)); });
            if (text.threw) {
                detail::iterator_close_quietly(c, self);
                c.throw_value(text.result);
                return value::undefined();
            }
            out += c.to_string(text.result);
        }
        return c.string(out);
    });
    // %Iterator.prototype%[@@dispose] (explicit resource management): the
    // iterator's own `return`, if it has one.
    {
        auto * dispose = detail::method_native(
            cx, "[Symbol.dispose]", [object_this](context & c, std::span<value>) {
                value self = value::undefined();
                if (!object_this(c, "[Symbol.dispose]", self)) { return value::undefined(); }
                const value back = c.lookup_property(self, "return");
                if (c.throw_pending()) { return value::undefined(); }
                if (back.is_nullish()) { return value::undefined(); }
                if (!back.is_callable()) {
                    c.throw_error("TypeError", "iterator.return is not a function");
                    return value::undefined();
                }
                (void)c.call(back, {}, self);
                return value::undefined();
            });
        detail::install_arity(cx, dispose, 0);
        proto->define("@@dispose", value::object(dispose), attr_builtin);
    }
    cx.define_global("Iterator", ctor_value);
}

} // namespace ctbrowser::script::builtins_detail
