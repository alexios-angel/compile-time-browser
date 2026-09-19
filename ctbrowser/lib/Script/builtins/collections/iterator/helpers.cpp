#include "helpers.hpp"

namespace ctbrowser::script::builtins_detail {

namespace iterator_helper {

// %IteratorHelperPrototype% objects (27.1.2.1) are ordinary objects with a
// private-keyed state slot: the underlying Iterator Record, the mapper or
// predicate, the counter, and the generator-like state of 27.1.2.1.1.

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
            const auto & items = static_cast<array_object *>(list.as_heap())->items;
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
        const auto & rows = static_cast<array_object *>(list.as_heap())->items;
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
    if (!raw.is_number() || !std::isfinite(raw.as_number()) ||
        std::trunc(raw.as_number()) != raw.as_number()) {
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

} // namespace iterator_helper

} // namespace ctbrowser::script::builtins_detail
