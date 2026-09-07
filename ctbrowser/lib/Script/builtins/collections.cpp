// ctbrowser.script builtins - Array, the keyed collections, and the typed arrays.
//
// One of five files carved out of a 4,118-line builtins.cpp on 2026-08-09.
// Everything shared - the argument helpers, namespace detail, and these
// functions' declarations - is in internal.hpp.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

namespace {

// A REAL ITERATOR over a list that already exists.
//
// `keys()`, `values()` and `entries()` are specified to return an Iterator, and
// this file used to hand back a plain Array with a comment saying that for..of
// walks one and that is what they are for. It is - `op::iterable` materialises
// through `context::iterable_values` and never touches `next` - but a PAGE may
// drive an iterator by hand, and one does: Babylon walks a Map of shadow
// generators with `for (let n = i.next(); !0 !== n.done; n = i.next())`, which
// threw "`next` is undefined" and took its shadows with it.
//
// So the object answers BOTH protocols. `next` and `@@iterator` are the real
// ones; `__items` is what `iterable_values` recognises, so `for (const x of
// m.values())` still costs one array copy and no interpretation. A partly
// consumed iterator handed to for..of restarts, which is the one place the two
// disagree and is written down here rather than discovered - nothing drives an
// iterator halfway and then spreads it.
[[nodiscard]] value list_iterator(context & cx, value items, const char * tag) {
    auto * it = static_cast<object_object *>(cx.make_object().as_heap());
    // NON-ENUMERABLE, all five. `__items` and `__at` are internal slots wearing
    // property names, and an iterator's own methods and tag are not enumerable
    // either - so `JSON.stringify(xs.entries())` is `{}` and `Object.keys` of one
    // is empty, which is what a browser answers and what the first version of
    // this got wrong by publishing its own bookkeeping.
    it->define("__items", items, attr_builtin);
    it->define("__at", value::number(0), attr_builtin);
    it->define("@@toStringTag", cx.string(std::string{tag}), attr_configurable);
    const auto method_on = [&](const char * name, native_fn fn) {
        it->define(name, value::object(cx.allocate<native_object>(name, std::move(fn))),
                   attr_builtin);
    };
    // Reads its state off the RECEIVER rather than out of the closure, so the
    // collector sees one object holding everything and a native captures
    // nothing it would have to root.
    method_on("next", [](context & c, std::span<value>) {
        auto * out = static_cast<object_object *>(c.make_object().as_heap());
        const value self = c.current_this();
        array_object * items = nullptr;
        std::size_t at = 0;
        if (self.is_object()) {
            auto * holder = static_cast<object_object *>(self.as_heap());
            if (value * list = holder->find("__items"); list != nullptr && list->is_array()) {
                items = static_cast<array_object *>(list->as_heap());
            }
            if (value * cursor = holder->find("__at"); cursor != nullptr) {
                const double n = context::to_number(*cursor);
                at = n > 0 ? static_cast<std::size_t>(n) : 0;
            }
            if (items != nullptr && at < items->items.size()) {
                holder->define("__at", value::number(static_cast<double>(at + 1)), attr_builtin);
            }
        }
        const bool done = items == nullptr || at >= items->items.size();
        out->set("done", value::boolean(done));
        out->set("value", done ? value::undefined() : items->items[at]);
        return value::object(out);
    });
    method_on("@@iterator", [](context & c, std::span<value>) { return c.current_this(); });
    return value::object(it);
}

} // namespace

// Array.prototype
void install_array(context & cx) {
    using detail::method;
    using detail::new_table;

    // `Array` itself. 88 uses of isArray in p5.js alone - it is how every
    // overloaded signature in the library decides what it was handed.
    auto * array_ctor = cx.allocate<native_object>("Array", [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * made = static_cast<array_object *>(out.as_heap());
        // `Array(n)` is a length, `Array(a, b, ...)` is the elements.
        if (a.size() == 1 && a[0].is_number()) {
            // THROUGH set_js_length, not `assign`. `new Array(4294967295)` is
            // legal JavaScript and `assign` made it 34 GB of `value` - one of
            // the SIGABRTs test262 found (built-ins/Array/length/
            // S15.4.2.2_A2.1_T1) - while `new Array(1.5)` is a RangeError the
            // silent std::max swallowed.
            if (!made->set_js_length(a[0].as_number())) {
                c.throw_error("RangeError", "Invalid array length");
            }
        } else {
            made->items.assign(a.begin(), a.end());
        }
        return out;
    });
    const auto static_method = [&](const char * name, double arity, native_fn fn) {
        method(cx, array_ctor, name, arity, std::move(fn));
    };
    static_method("isArray", 1, [](context &, std::span<value> a) {
        return value::boolean(arg_at(a, 0).is_array());
    });
    static_method("of", 0, [](context & c, std::span<value> a) {
        value out = c.make_array();
        static_cast<array_object *>(out.as_heap())->items.assign(a.begin(), a.end());
        return out;
    });
    static_method("from", 1, [](context & c, std::span<value> a) {
        value out = c.make_array();
        // A mapping function that is PRESENT AND NOT CALLABLE is a TypeError
        // (23.1.2.1 step 2), checked before the source is touched. It was
        // silently ignored, so `Array.from(xs, 'nope')` copied xs and said
        // nothing.
        const value mapper = arg_at(a, 1);
        if (!mapper.is_undefined() && !mapper.is_callable()) {
            c.throw_error("TypeError", "the map function is not a function");
            return out;
        }
        const context::rooted keep(c, out);
        auto * made = static_cast<array_object *>(out.as_heap());
        // Through the one conversion for..of and spread use, so all three agree
        // about what "iterable" means - a Map, a Set, a string, an array or
        // anything array-LIKE (a NodeList, `arguments`, a typed-array shim).
        const value from = c.iterable_values(arg_at(a, 0));
        if (from.is_array()) { made->items = static_cast<array_object *>(from.as_heap())->items; }
        if (mapper.is_callable()) {
            // `thisArg` is argument 3 and was dropped, exactly as it was on
            // every Array.prototype method.
            const value this_arg = arg_at(a, 2);
            for (std::size_t i = 0; i < made->items.size(); ++i) {
                const value args[2] = {made->items[i], value::number(static_cast<double>(i))};
                made->items[i] = c.call(mapper, args, this_arg);
            }
        }
        return out;
    });
    cx.define_global("Array", value::object(array_ctor));

    object_object * array_proto = new_table(cx);
    // The prototype methods p5.js uses that were not here. `at` and `fill` are
    // the ones it leans on hardest - 43 and 31 uses - because a typed-array
    // shim reaches for both.
    method(cx, array_proto, "at", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "at")) { return value::undefined(); }
        const double len = detail::array_like_length(c, self);
        // integer_arg, not num_at: ToIntegerOrInfinity runs a `valueOf`, so
        // `xs.at({valueOf: () => 1})` is `xs[1]` rather than `xs[NaN]`.
        double i = integer_arg(c, a, 0);
        if (i < 0) { i += len; }
        if (i < 0 || i >= len) { return value::undefined(); }
        return detail::element_at(c, self, i);
    });
    method(cx, array_proto, "fill", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "fill")) { return self; }
        const double len = detail::array_like_length(c, self);
        const value filler = arg_at(a, 0);
        const double start = integer_arg(c, a, 1);
        double k = start < 0 ? std::max(len + start, 0.0) : std::min(start, len);
        // AN ABSENT `end` AND AN EXPLICIT `undefined` BOTH MEAN "to the end".
        // A count test sees three arguments for `fill(0, 0, undefined)`,
        // coerces the undefined to 0 and fills nothing.
        const double raw_end = has_index(a, 2) ? integer_arg(c, a, 2) : len;
        const double end = raw_end < 0 ? std::max(len + raw_end, 0.0) : std::min(raw_end, len);
        for (; k < end; k += 1.0) { detail::put_element(c, self, k, filler); }
        return self;
    });
    // 23.1.3.13, GENERIC, and its depth goes through ToIntegerOrInfinity.
    //
    // `flat(undefined)` is the DEFAULT depth of 1 and `flat('TestString')`,
    // `flat({})` and `flat(-Infinity)` are all depth 0. The old spelling tested
    // the argument COUNT for the default - so an explicit undefined flattened
    // nothing - and then coerced with the STATIC to_number, which answers NaN
    // for an object and compares false against every bound.
    method(cx, array_proto, "flat", 0, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        value out = c.make_array();
        if (!detail::coercible_this(c, self, "flat")) { return out; }
        const double depth = has_index(a, 0) ? integer_arg(c, a, 0) : 1.0;
        const double len = detail::array_like_length(c, self);
        if (!detail::generic_walk_ok(c, len)) { return out; }
        const context::rooted keep(c, out);
        auto * result = static_cast<array_object *>(out.as_heap());
        // An explicit worklist rather than recursion: `flat(Infinity)` on a
        // deep structure must not be bounded by the C++ stack.
        //
        // THE WORKLIST LIVES IN A ROOTED ARRAY, which is what reading through
        // [[Get]] costs. A getter allocates, an allocation can collect, and a
        // value sitting only in a std::vector<value> is in none of the
        // collector's roots - the hazard `map` and `sort` document at length.
        // `scratch` holds the pending VALUES and is rooted; the depths beside
        // them are plain doubles with nothing to trace.
        value scratch = c.make_array();
        const context::rooted keep_scratch(c, scratch);
        auto & pending = static_cast<array_object *>(scratch.as_heap())->items;
        std::vector<double> depths;
        for (double k = len; k-- > 0;) {
            if (!detail::has_element(c, self, k)) { continue; }
            pending.push_back(detail::element_at(c, self, k));
            depths.push_back(depth);
        }
        while (!pending.empty()) {
            const value item = pending.back();
            const double left = depths.back();
            pending.pop_back();
            depths.pop_back();
            // ONLY A REAL Array IS FLATTENED - IsArray (7.2.2) and nothing
            // else, so an array-LIKE element is one element however many
            // indices it claims to have.
            if (!item.is_array() || left <= 0) {
                result->items.push_back(item);
                continue;
            }
            const double inner = detail::array_like_length(c, item);
            if (!detail::generic_walk_ok(c, inner)) { return out; }
            for (double k = inner; k-- > 0;) {
                if (!detail::has_element(c, item, k)) { continue; }
                pending.push_back(detail::element_at(c, item, k));
                depths.push_back(left - 1);
            }
        }
        return out;
    });
    // 23.1.3.14, whose depth is ALWAYS 1. Generic, it takes its `thisArg` - the
    // second argument, accepted and dropped before, while the receiver was
    // passed as the callback's `this` instead - and a mapper that is absent or
    // not callable is a TypeError rather than an empty result.
    method(cx, array_proto, "flatMap", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        value out = c.make_array();
        if (!detail::coercible_this(c, self, "flatMap")) { return out; }
        const value callback = arg_at(a, 0);
        if (!detail::callable_arg(c, callback, "callback")) { return out; }
        const value this_arg = arg_at(a, 1);
        const double len = detail::array_like_length(c, self);
        if (!detail::generic_walk_ok(c, len)) { return out; }
        const context::rooted keep(c, out); // as `map` - see the note there
        auto * result = static_cast<array_object *>(out.as_heap());
        for (double k = 0; k < len; k += 1.0) {
            if (!detail::has_element(c, self, k)) { continue; }
            const value args[3] = {detail::element_at(c, self, k), value::number(k), self};
            value mapped = c.call(callback, args, this_arg);
            if (!mapped.is_array()) {
                result->items.push_back(mapped);
                continue;
            }
            // THE MAPPED ARRAY IS A C++ LOCAL and, once the callback's frame
            // has gone, the only reference to it there is - and reading it can
            // run a getter, which can collect.
            const context::rooted keep_mapped(c, mapped);
            const double inner = detail::array_like_length(c, mapped);
            if (!detail::generic_walk_ok(c, inner)) { return out; }
            for (double j = 0; j < inner; j += 1.0) {
                result->items.push_back(detail::element_at(c, mapped, j));
            }
        }
        return out;
    });
    method(cx, array_proto, "findLast", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "findLast")) { return value::undefined(); }
        const value callback = arg_at(a, 0);
        if (!detail::callable_arg(c, callback, "callback")) { return value::undefined(); }
        const value this_arg = arg_at(a, 1);
        for (double k = detail::array_like_length(c, self) - 1; k >= 0; k -= 1.0) {
            const value item = detail::element_at(c, self, k);
            const value args[3] = {item, value::number(k), self};
            if (context::truthy(c.call(callback, args, this_arg))) { return item; }
        }
        return value::undefined();
    });
    method(cx, array_proto, "findLastIndex", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "findLastIndex")) { return value::number(-1); }
        const value callback = arg_at(a, 0);
        if (!detail::callable_arg(c, callback, "callback")) { return value::number(-1); }
        const value this_arg = arg_at(a, 1);
        for (double k = detail::array_like_length(c, self) - 1; k >= 0; k -= 1.0) {
            const value args[3] = {detail::element_at(c, self, k), value::number(k), self};
            if (context::truthy(c.call(callback, args, this_arg))) { return value::number(k); }
        }
        return value::number(-1);
    });
    // --- THE FOUR THAT MUTATE AT AN END ------------------------------------
    //
    // push, pop, shift and unshift are as generic as the eighteen that read
    // (see detail::array_like_length): `this` is ToObject'd, `length` is read
    // through [[Get]] and ToLength, every element moves through [[Set]] or
    // [[Delete]], and the algorithm ENDS with Set(O, "length", n, true).
    // Writing the length back is the half a read-only method never had, and it
    // is what `Array.prototype.push.call(obj, x)` needs to be worth calling.
    //
    // EACH OPENS WITH A BRANCH, not with a second algorithm. A real, ordinary
    // Array is its own std::vector and does the vector operation; anything else
    // - an array-like, a Proxy, `arguments` from another realm - takes the
    // specified walk. detail::dense_array_this is that branch.
    //
    // 2^53-1 IS A TypeError, NOT A CLAMP (23.1.3.23 step 5, 23.1.3.32 step 4a).
    // `length` itself clamps there through ToLength, so `push()` with no
    // arguments on `{length: Infinity}` writes back 2^53-1 and succeeds while
    // `push(null)` on the same object throws.
    method(cx, array_proto, "push", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "push")) { return value::number(0); }
        if (array_object * dense = detail::dense_array_this(self)) {
            // FROZEN MEANS FROZEN, and it is a THROW here rather than a silent
            // drop: the Sets in 23.1.3.23 carry Throw=true, so this is a
            // TypeError in sloppy mode too - unlike a bare `a[0] = 1`, which
            // context::store_index discards (TODO(strict) there).
            if (!dense->extensible && (!a.empty() || !dense->elements_writable)) {
                c.throw_error("TypeError", "Cannot add a property to a non-extensible array");
                return value::number(static_cast<double>(dense->items.size()));
            }
            dense->items.insert(dense->items.end(), a.begin(), a.end());
            return value::number(static_cast<double>(dense->items.size()));
        }
        if (!detail::mutable_receiver(c, self, "push")) { return value::number(0); }
        double len = detail::array_like_length(c, self);
        if (len + static_cast<double>(a.size()) > max_safe_integer) {
            c.throw_error("TypeError", "Invalid array length");
            return value::number(len);
        }
        for (const value & item : a) {
            detail::put_element(c, self, len, item);
            len += 1.0;
        }
        detail::put_length(c, self, len);
        return value::number(len);
    });
    // 23.1.3.22. An EMPTY receiver still writes `length` back - that is step
    // 3a, and it is what turns `{length: NaN}` into `{length: 0}`.
    method(cx, array_proto, "pop", 0, [](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "pop")) { return value::undefined(); }
        if (array_object * dense = detail::dense_array_this(self)) {
            if (dense->items.empty()) {
                if (!dense->elements_writable) {
                    c.throw_error("TypeError", "Cannot set length of a frozen array");
                }
                return value::undefined();
            }
            if (!dense->elements_configurable) {
                c.throw_error("TypeError", "Cannot delete an element of a sealed array");
                return value::undefined();
            }
            const value out = dense->items.back();
            dense->items.pop_back();
            return out;
        }
        if (!detail::mutable_receiver(c, self, "pop")) { return value::undefined(); }
        const double len = detail::array_like_length(c, self);
        if (len == 0) {
            detail::put_length(c, self, 0);
            return value::undefined();
        }
        const value out = detail::element_at(c, self, len - 1);
        // ROOTED ACROSS THE DELETE AND THE LENGTH WRITE. Both can run user
        // code - a Proxy trap, a `length` setter - and the value being returned
        // is by then held only by this C++ local.
        const context::rooted keep(c, out);
        detail::delete_element(c, self, len - 1);
        detail::put_length(c, self, len - 1);
        return out;
    });
    // 23.1.3.25. Every element moves DOWN one, a hole moving down deletes what
    // it lands on rather than filling it with undefined, and the vacated slot
    // at the top is deleted before `length` is written.
    method(cx, array_proto, "shift", 0, [](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "shift")) { return value::undefined(); }
        if (array_object * dense = detail::dense_array_this(self)) {
            if (dense->items.empty()) {
                if (!dense->elements_writable) {
                    c.throw_error("TypeError", "Cannot set length of a frozen array");
                }
                return value::undefined();
            }
            if (!dense->elements_configurable) {
                c.throw_error("TypeError", "Cannot delete an element of a sealed array");
                return value::undefined();
            }
            const value out = dense->items.front();
            dense->items.erase(dense->items.begin());
            return out;
        }
        if (!detail::mutable_receiver(c, self, "shift")) { return value::undefined(); }
        const double len = detail::array_like_length(c, self);
        if (len == 0) {
            detail::put_length(c, self, 0);
            return value::undefined();
        }
        if (!detail::generic_walk_ok(c, len)) { return value::undefined(); }
        const value out = detail::element_at(c, self, 0);
        const context::rooted keep(c, out);
        for (double k = 1; k < len; k += 1.0) {
            if (detail::has_element(c, self, k)) {
                detail::put_element(c, self, k - 1, detail::element_at(c, self, k));
            } else {
                detail::delete_element(c, self, k - 1);
            }
        }
        detail::delete_element(c, self, len - 1);
        detail::put_length(c, self, len - 1);
        return out;
    });
    // 23.1.3.32. The tail moves UP, walked from the top down so that an
    // overlapping move never overwrites a source before it is read.
    method(cx, array_proto, "unshift", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "unshift")) { return value::number(0); }
        if (array_object * dense = detail::dense_array_this(self)) {
            if (!dense->extensible && (!a.empty() || !dense->elements_writable)) {
                c.throw_error("TypeError", "Cannot add a property to a non-extensible array");
                return value::number(static_cast<double>(dense->items.size()));
            }
            dense->items.insert(dense->items.begin(), a.begin(), a.end());
            return value::number(static_cast<double>(dense->items.size()));
        }
        if (!detail::mutable_receiver(c, self, "unshift")) { return value::number(0); }
        const double len = detail::array_like_length(c, self);
        const auto count = static_cast<double>(a.size());
        if (count > 0) {
            if (len + count > max_safe_integer) {
                c.throw_error("TypeError", "Invalid array length");
                return value::number(len);
            }
            if (!detail::generic_walk_ok(c, len)) { return value::number(len); }
            for (double k = len; k > 0; k -= 1.0) {
                const double from = k - 1;
                const double to = k + count - 1;
                if (detail::has_element(c, self, from)) {
                    detail::put_element(c, self, to, detail::element_at(c, self, from));
                } else {
                    detail::delete_element(c, self, to);
                }
            }
            for (std::size_t i = 0; i < a.size(); ++i) {
                detail::put_element(c, self, static_cast<double>(i), a[i]);
            }
        }
        detail::put_length(c, self, len + count);
        return value::number(len + count);
    });
    method(cx, array_proto, "slice", 2, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        value out = c.make_array();
        if (!detail::coercible_this(c, self, "slice")) { return out; }
        const double len = detail::array_like_length(c, self);
        const double raw_from = integer_arg(c, a, 0);
        double k = raw_from < 0 ? std::max(len + raw_from, 0.0) : std::min(raw_from, len);
        const double raw_to = has_index(a, 1) ? integer_arg(c, a, 1) : len;
        const double to = raw_to < 0 ? std::max(len + raw_to, 0.0) : std::min(raw_to, len);
        const context::rooted keep(c, out);
        auto * result = static_cast<array_object *>(out.as_heap());
        // The count is the SPAN, not the number of elements found: a hole in
        // the middle leaves an undefined behind rather than shortening the
        // result, which is what `A.length = n` at the end of 23.1.3.28 says.
        for (; k < to; k += 1.0) { result->items.push_back(detail::element_at(c, self, k)); }
        return out;
    });
    // 23.1.3.29, THE WHOLE OF IT, and the ORDER is the point: the deleted range
    // is copied out first, then the tail is shifted - left through ascending
    // indices and right through descending ones, so an overlap never overwrites
    // a source before it is read - then the inserted items are written, and
    // `length` last.
    //
    // THREE CASES FOR THE COUNT, not two. `splice()` with no argument at all
    // deletes NOTHING (step 6); only `splice(i)` deletes to the end (step 7).
    // The old spelling tested `a.size() > 1` for both and so read the
    // no-argument call as "delete everything from index 0".
    method(cx, array_proto, "splice", 2, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        value removed = c.make_array();
        if (!detail::coercible_this(c, self, "splice")) { return removed; }
        array_object * dense = detail::dense_array_this(self);
        const double len = dense != nullptr ? static_cast<double>(dense->items.size())
                                            : detail::array_like_length(c, self);
        // ToIntegerOrInfinity, NOT the static coercion, and BEFORE anything
        // moves: `splice({valueOf: f})` is ordinary and `splice(i, Infinity)`
        // is the ordinary way to say "to the end". Infinity does not convert to
        // an integer at all - that is undefined behaviour rather than a large
        // number, which UBSan caught going through the old cast silently - so
        // every bound here is clamped as a DOUBLE and cast afterwards.
        const double raw_start = integer_arg(c, a, 0);
        const double start =
            raw_start < 0 ? std::max(len + raw_start, 0.0) : std::min(raw_start, len);
        double skipped = 0;
        if (a.size() == 1) {
            skipped = len - start;
        } else if (a.size() > 1) {
            skipped = std::min(std::max(integer_arg(c, a, 1), 0.0), len - start);
        }
        const double inserted = a.size() > 2 ? static_cast<double>(a.size() - 2) : 0.0;
        if (len + inserted - skipped > max_safe_integer) {
            c.throw_error("TypeError", "Invalid array length");
            return removed;
        }
        const context::rooted keep(c, removed);
        auto * out = static_cast<array_object *>(removed.as_heap());
        if (dense != nullptr) {
            const auto from = static_cast<std::size_t>(start);
            const auto count = static_cast<std::size_t>(skipped);
            const auto first = dense->items.begin() + static_cast<std::ptrdiff_t>(from);
            out->items.assign(first, first + static_cast<std::ptrdiff_t>(count));
            dense->items.erase(first, first + static_cast<std::ptrdiff_t>(count));
            if (a.size() > 2) {
                dense->items.insert(dense->items.begin() + static_cast<std::ptrdiff_t>(from),
                                    a.begin() + 2, a.end());
            }
            return removed;
        }
        if (!detail::mutable_receiver(c, self, "splice")) { return removed; }
        // BOUNDED BY THE WORK, not by `length`. Step 15's shift runs from
        // actualStart to len-actualDeleteCount, so splicing one element out of
        // `{length: 4294967296}` at the very end is two operations and not four
        // billion - which is what S15.4.4.12_A3_T1 asks for.
        if (!detail::generic_walk_ok(c, skipped) || !detail::generic_walk_ok(c, len - start)) {
            return removed;
        }
        for (double k = 0; k < skipped; k += 1.0) {
            // A HOLE STAYS A HOLE IN LENGTH ONLY. CreateDataProperty is skipped
            // for an absent index and `A.length` is set to the count anyway
            // (step 14), and an array here cannot hold a hole - so the span is
            // preserved with an undefined, exactly as `slice` documents.
            out->items.push_back(detail::has_element(c, self, start + k)
                                     ? detail::element_at(c, self, start + k)
                                     : value::undefined());
        }
        if (inserted < skipped) {
            for (double k = start; k < len - skipped; k += 1.0) {
                if (detail::has_element(c, self, k + skipped)) {
                    detail::put_element(c, self, k + inserted,
                                        detail::element_at(c, self, k + skipped));
                } else {
                    detail::delete_element(c, self, k + inserted);
                }
            }
            for (double k = len; k > len - skipped + inserted; k -= 1.0) {
                detail::delete_element(c, self, k - 1);
            }
        } else if (inserted > skipped) {
            for (double k = len - skipped; k > start; k -= 1.0) {
                if (detail::has_element(c, self, k + skipped - 1)) {
                    detail::put_element(c, self, k + inserted - 1,
                                        detail::element_at(c, self, k + skipped - 1));
                } else {
                    detail::delete_element(c, self, k + inserted - 1);
                }
            }
        }
        for (std::size_t i = 2; i < a.size(); ++i) {
            detail::put_element(c, self, start + static_cast<double>(i - 2), a[i]);
        }
        detail::put_length(c, self, len - skipped + inserted);
        return removed;
    });
    // `fromIndex`, WHICH BOTH SEARCHES ACCEPTED AND NEITHER READ. `xs.indexOf(v,
    // 5)` searched from 0, so a scan-from-here loop - the standard way to find
    // every occurrence - found the first one forever.
    method(cx, array_proto, "indexOf", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "indexOf")) { return value::number(-1); }
        const double len = detail::array_like_length(c, self);
        if (len == 0) { return value::number(-1); }
        const double n = a.size() > 1 ? integer_arg(c, a, 1) : 0.0;
        if (std::isinf(n) && n > 0) { return value::number(-1); }
        double k = n >= 0 ? n : len + n;
        if (k < 0) { k = 0; }
        const value target = arg_at(a, 0);
        for (; k < len; k += 1.0) {
            if (detail::has_element(c, self, k) &&
                detail::element_at(c, self, k).strict_equals(target)) {
                return value::number(k);
            }
        }
        return value::number(-1);
    });
    method(cx, array_proto, "lastIndexOf", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "lastIndexOf")) { return value::number(-1); }
        const double len = detail::array_like_length(c, self);
        if (len == 0) { return value::number(-1); }
        // The default is the LAST index, not the first, and a negative
        // fromIndex counts back from the end without clamping up to 0 - it
        // clamps the search away entirely.
        const double n = a.size() > 1 ? integer_arg(c, a, 1) : len - 1;
        if (std::isinf(n) && n < 0) { return value::number(-1); }
        double k = n >= 0 ? std::min(n, len - 1) : len + n;
        const value target = arg_at(a, 0);
        for (; k >= 0; k -= 1.0) {
            if (detail::has_element(c, self, k) &&
                detail::element_at(c, self, k).strict_equals(target)) {
                return value::number(k);
            }
        }
        return value::number(-1);
    });
    method(cx, array_proto, "includes", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "includes")) { return value::boolean(false); }
        const double len = detail::array_like_length(c, self);
        if (len == 0) { return value::boolean(false); }
        const double n = a.size() > 1 ? integer_arg(c, a, 1) : 0.0;
        if (std::isinf(n) && n > 0) { return value::boolean(false); }
        double k = n >= 0 ? n : len + n;
        if (k < 0) { k = 0; }
        const value target = arg_at(a, 0);
        const bool want_nan = target.is_number() && std::isnan(target.as_number());
        for (; k < len; k += 1.0) {
            // SameValueZero (7.2.11), not strict equality: `includes` is the
            // one search that FINDS A NaN, which is the whole reason it exists
            // beside `indexOf`. And it does not skip a hole - it reads one as
            // undefined - so `[, 1].includes(undefined)` is true.
            const value item = detail::element_at(c, self, k);
            if (want_nan ? (item.is_number() && std::isnan(item.as_number()))
                         : item.strict_equals(target)) {
                return value::boolean(true);
            }
        }
        return value::boolean(false);
    });
    method(cx, array_proto, "join", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "join")) { return c.string(std::string{}); }
        const double len = detail::array_like_length(c, self);
        // AN ABSENT SEPARATOR AND AN EXPLICIT `undefined` BOTH MEAN ",". The
        // argument COUNT was tested instead, so `[1, 2].join(undefined)` was
        // "1undefined2".
        const std::string sep = has_index(a, 0) ? c.to_string(a[0]) : std::string{","};
        std::string out;
        for (double k = 0; k < len; k += 1.0) {
            if (k > 0) { out += sep; }
            // null and undefined join as EMPTY, not as "null"/"undefined".
            const value item = detail::element_at(c, self, k);
            if (!item.is_nullish()) { out += c.to_string(item); }
        }
        return c.string(out);
    });
    // 23.1.3.32. Every element is asked for its OWN `toLocaleString`, which is
    // the only difference from `join(",")` and is what makes a Date or a Number
    // in an array format itself.
    method(cx, array_proto, "toLocaleString", 0, [](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "toLocaleString")) { return c.string(std::string{}); }
        const double len = detail::array_like_length(c, self);
        std::string out;
        for (double k = 0; k < len; k += 1.0) {
            if (k > 0) { out += ','; }
            const value item = detail::element_at(c, self, k);
            if (item.is_nullish()) { continue; }
            const value fn = c.lookup_property(item, "toLocaleString");
            out += fn.is_callable() ? c.to_string(c.call(fn, std::span<const value>{}, item))
                                    : c.to_string(item);
        }
        return c.string(out);
    });
    // 23.1.3.1. Generic - and for `concat` that means something specific: the
    // RECEIVER is spread only when IsArray says it is an array, so
    // `Array.prototype.concat.call({length: 2, 0: \'a\'}, 4)` is `[obj, 4]` and
    // not `[\'a\', undefined, 4]`. The old spelling read the receiver with
    // this_array() and dropped a non-array one entirely.
    //
    // NOTHING EXOTIC, deliberately. There is no `Symbol.isConcatSpreadable`
    // here and no ArraySpeciesCreate: the result is always an ordinary Array
    // and only a real Array spreads. Honouring the symbol halfway - a truthy
    // one but not a false one, say - would be worse than not having it, because
    // a page that sets it would get an answer wrong in a NEW way rather than in
    // the documented one.
    method(cx, array_proto, "concat", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        value out = c.make_array();
        if (!detail::coercible_this(c, self, "concat")) { return out; }
        const context::rooted keep(c, out);
        auto * result = static_cast<array_object *>(out.as_heap());
        // One element, or one spread. False means a throw is already in flight
        // and the caller must stop.
        const auto append = [&](value item) {
            if (!item.is_array()) {
                result->items.push_back(item);
                return true;
            }
            if (array_object * dense = detail::dense_array_this(item)) {
                result->items.insert(result->items.end(), dense->items.begin(), dense->items.end());
                return true;
            }
            const double len = detail::array_like_length(c, item);
            if (!detail::generic_walk_ok(c, len)) { return false; }
            for (double k = 0; k < len; k += 1.0) {
                result->items.push_back(detail::element_at(c, item, k));
            }
            return true;
        };
        if (!append(self)) { return out; }
        for (const value & item : a) {
            if (!append(item)) { return out; }
        }
        return out;
    });
    // 23.1.3.26, in place and generic. The swap is HasProperty-then-Get on BOTH
    // ends: a hole opposite an element DELETES the far side rather than filling
    // it with undefined, which is the only thing that distinguishes reverse
    // from "read it all and write it back".
    method(cx, array_proto, "reverse", 0, [](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "reverse")) { return self; }
        if (array_object * dense = detail::dense_array_this(self)) {
            std::ranges::reverse(dense->items);
            return self;
        }
        if (!detail::mutable_receiver(c, self, "reverse")) { return self; }
        const double len = detail::array_like_length(c, self);
        if (!detail::generic_walk_ok(c, len)) { return self; }
        const double middle = std::floor(len / 2);
        for (double lower = 0; lower < middle; lower += 1.0) {
            const double upper = len - lower - 1;
            const bool lower_there = detail::has_element(c, self, lower);
            const value lower_value =
                lower_there ? detail::element_at(c, self, lower) : value::undefined();
            // ROOTED ACROSS THE SECOND READ. A getter on the far end allocates,
            // an allocation can collect, and what the near end just answered is
            // by then held only by this C++ local.
            const context::rooted keep(c, lower_value);
            const bool upper_there = detail::has_element(c, self, upper);
            const value upper_value =
                upper_there ? detail::element_at(c, self, upper) : value::undefined();
            if (lower_there && upper_there) {
                detail::put_element(c, self, lower, upper_value);
                detail::put_element(c, self, upper, lower_value);
            } else if (upper_there) {
                detail::put_element(c, self, lower, upper_value);
                detail::delete_element(c, self, upper);
            } else if (lower_there) {
                detail::delete_element(c, self, lower);
                detail::put_element(c, self, upper, lower_value);
            }
        }
        return self;
    });
    // The iteration methods call back INTO the VM, which is what
    // context::call() exists for. Each snapshots the item first, because the
    // callback may mutate the array underneath it.
    //
    // ALL OF THEM ARE GENERIC OVER THE RECEIVER now (detail::array_like_length
    // and the two accessors beside it say why), ALL OF THEM TAKE A `thisArg`,
    // and all of them refuse a non-callable callback with a TypeError. The
    // second was accepted and dropped - a callback written as a method and
    // handed its object explicitly ran against `undefined` - and the third is
    // 141 of built-ins/Array's "Expected a TypeError to be thrown but no
    // exception was thrown at all" (measured 2026-09-07).
    //
    // `body` gets the index, the element and what the callback answered, and
    // returns false to stop. `each` reports whether it ran to the end, which is
    // what `every` needs and what a `return` out of the middle is not.
    const auto each = [](context & c, std::span<value> a, const char * name, auto && body) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, name)) { return false; }
        const value callback = arg_at(a, 0);
        if (!detail::callable_arg(c, callback, "callback")) { return false; }
        const value this_arg = arg_at(a, 1);
        const double len = detail::array_like_length(c, self);
        for (double k = 0; k < len; k += 1.0) {
            // A HOLE IS SKIPPED, not visited with undefined. That is the whole
            // difference between `[, 1].forEach(f)` calling back once and
            // twice, and it is what the array-like tests are written around.
            if (!detail::has_element(c, self, k)) { continue; }
            const value item = detail::element_at(c, self, k);
            const value call_args[3] = {item, value::number(k), self};
            if (!body(k, item, c.call(callback, call_args, this_arg))) { return false; }
        }
        return true;
    };
    method(cx, array_proto, "forEach", 1, [each](context & c, std::span<value> a) {
        (void)each(c, a, "forEach", [](double, value, value) { return true; });
        return value::undefined();
    });
    method(cx, array_proto, "map", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        value out = c.make_array();
        if (!detail::coercible_this(c, self, "map")) { return out; }
        const value callback = arg_at(a, 0);
        if (!detail::callable_arg(c, callback, "callback")) { return out; }
        const value this_arg = arg_at(a, 1);
        const double len = detail::array_like_length(c, self);
        // THE RESULT IS A C++ LOCAL ACROSS EVERY CALLBACK, and a C++ local is
        // in none of the collector's roots. It is allocated BEFORE the first
        // call, so a callback that collects - `$262.gc()`, or gc stress, which
        // is where ctcompile's differential gate runs this - freed the array
        // the remaining iterations then pushed into. Under the asan preset
        // that is a heap-use-after-free WRITE; without one it is silent, and
        // two distinct object literals become one allocation because the first
        // was only reachable through the array that had already been freed.
        // context::rooted is the primitive for exactly this.
        const context::rooted keep(c, out);
        // ArrayCreate(len) FIRST, and it is what refuses a `length` of 2^32.
        // Sizing it up front is also the only way a hole in the source can
        // stay a hole in the result rather than shifting everything after it.
        if (detail::new_array_of_length(c, out, len) == nullptr) { return out; }
        for (double k = 0; k < len; k += 1.0) {
            if (!detail::has_element(c, self, k)) { continue; }
            const value call_args[3] = {detail::element_at(c, self, k), value::number(k), self};
            detail::put_element(c, out, k, c.call(callback, call_args, this_arg));
        }
        return out;
    });
    method(cx, array_proto, "filter", 1, [each](context & c, std::span<value> a) {
        value out = c.make_array();
        // Unrooted exactly as `map`'s was. It survived only because the values
        // it collects are also in the rooted source array - the ARRAY itself
        // was still freed under the push, which asan reports and which is not
        // something to leave standing on the strength of a coincidence.
        const context::rooted keep(c, out);
        auto * result = static_cast<array_object *>(out.as_heap());
        (void)each(c, a, "filter", [&](double, value item, value verdict) {
            if (context::truthy(verdict)) { result->items.push_back(item); }
            return true;
        });
        return out;
    });
    // `find` and `findIndex` DO NOT SKIP A HOLE - 23.1.3.9 reads every index
    // with [[Get]] and hands the callback an undefined - which is why neither
    // goes through `each`.
    method(cx, array_proto, "find", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "find")) { return value::undefined(); }
        const value callback = arg_at(a, 0);
        if (!detail::callable_arg(c, callback, "callback")) { return value::undefined(); }
        const value this_arg = arg_at(a, 1);
        const double len = detail::array_like_length(c, self);
        for (double k = 0; k < len; k += 1.0) {
            const value item = detail::element_at(c, self, k);
            const value call_args[3] = {item, value::number(k), self};
            if (context::truthy(c.call(callback, call_args, this_arg))) { return item; }
        }
        return value::undefined();
    });
    method(cx, array_proto, "findIndex", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "findIndex")) { return value::number(-1); }
        const value callback = arg_at(a, 0);
        if (!detail::callable_arg(c, callback, "callback")) { return value::number(-1); }
        const value this_arg = arg_at(a, 1);
        const double len = detail::array_like_length(c, self);
        for (double k = 0; k < len; k += 1.0) {
            const value call_args[3] = {detail::element_at(c, self, k), value::number(k), self};
            if (context::truthy(c.call(callback, call_args, this_arg))) { return value::number(k); }
        }
        return value::number(-1);
    });
    method(cx, array_proto, "some", 1, [each](context & c, std::span<value> a) {
        bool answer = false;
        (void)each(c, a, "some", [&](double, value, value verdict) {
            answer = context::truthy(verdict);
            return !answer;
        });
        return value::boolean(answer);
    });
    method(cx, array_proto, "every", 1, [each](context & c, std::span<value> a) {
        bool answer = true;
        (void)each(c, a, "every", [&](double, value, value verdict) {
            answer = context::truthy(verdict);
            return answer;
        });
        // A THROW MUST NOT READ AS `true`. `each` stops on a refusal from the
        // callback AND on a TypeError it raised itself; `answer` is only
        // meaningful in the first case, and it starts true for the empty array
        // that 23.1.3.6 says is vacuously every.
        return value::boolean(answer);
    });
    // `reduce` and `reduceRight` are one shape walked in two directions. Both
    // throw a TypeError on an empty array with no initial value - which was an
    // `undefined` here, and is the one error every fold is written to rely on.
    const auto fold = [](context & c, std::span<value> a, const char * name, bool backwards) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, name)) { return value::undefined(); }
        const value callback = arg_at(a, 0);
        if (!detail::callable_arg(c, callback, "callback")) { return value::undefined(); }
        const double len = detail::array_like_length(c, self);
        double k = backwards ? len - 1 : 0;
        const double step = backwards ? -1.0 : 1.0;
        const auto in_range = [&] { return backwards ? k >= 0 : k < len; };
        value total;
        if (a.size() > 1) {
            total = a[1];
        } else {
            bool found = false;
            for (; !found && in_range(); k += step) {
                if (detail::has_element(c, self, k)) {
                    total = detail::element_at(c, self, k);
                    found = true;
                }
            }
            if (!found) {
                c.throw_error("TypeError", "Reduce of empty array with no initial value");
                return value::undefined();
            }
        }
        // The callback is a C++ local across every call it makes, and the only
        // reference to it once whatever produced it has been overwritten -
        // exactly the hazard `map` and `sort` document above.
        const context::rooted keep_callback(c, callback);
        for (; in_range(); k += step) {
            // THE ACCUMULATOR IS ROOTED PER ITERATION, not once: `rooted` takes
            // a COPY, so a root taken before the loop would pin the initial
            // value and leave every accumulator the callback produces after it
            // unreachable. Constructing it first thing in the body covers the
            // element read and the call; between the destructor here and the
            // constructor on the next turn nothing allocates.
            const context::rooted keep_total(c, total);
            if (!detail::has_element(c, self, k)) { continue; }
            const value call_args[4] = {total, detail::element_at(c, self, k), value::number(k),
                                        self};
            total = c.call(callback, call_args);
        }
        return total;
    };
    method(cx, array_proto, "reduce", 1,
           [fold](context & c, std::span<value> a) { return fold(c, a, "reduce", false); });
    method(cx, array_proto, "reduceRight", 1,
           [fold](context & c, std::span<value> a) { return fold(c, a, "reduceRight", true); });
    // SORTING A VECTOR, once, for both `sort` and `toSorted`.
    //
    // It was inline in `sort` and `toSorted` needs exactly the same thing over
    // a copy; two spellings of a merge sort that has this much reasoning behind
    // it is how the two of them come to disagree about stability.
    const auto sort_values = [](context & c, std::vector<value> & work, value comparator) {
        // ...AND THE VECTOR IS THE ONLY REFERENCE THERE IS once the comparator
        // has emptied the array it came from. A bare std::vector<value> is in
        // none of the collector's roots, so `a.sort(function (x, y) {
        // a.length = 0; $262.gc(); ... })` freed every element that was not
        // currently an argument and the next merge step read them - a segfault
        // in Release and a heap-use-after-free under asan. The work vector is
        // permuted, never added to, so rooting the initial contents roots every
        // value this loop can reach.
        const context::rooted_values keep_work(c, work);
        // The comparator is a C++ local too, and the only reference to it once
        // whatever produced it has been overwritten.
        const context::rooted keep_comparator(c, comparator);
        if (!comparator.is_callable()) {
            // The default really is lexicographic on the STRING form, which is
            // why [10, 9].sort() is [10, 9].
            //
            // THE KEYS ARE COMPUTED ONCE. `stable_sort` with `to_string` inside
            // the comparison converts each element about 2 log n times and
            // allocates a std::string for every one of them; a page sorting a
            // thousand items paid for twenty thousand conversions to answer a
            // thousand questions.
            //
            // undefined SORTS LAST, ahead of the comparison rather than
            // through it: 23.1.3.30 moves every undefined to the end and never
            // asks about one, and ToString(undefined) is "undefined", which
            // lands between "u" and "v" instead.
            std::vector<std::pair<std::string, value>> keyed;
            std::size_t undefined_count = 0;
            keyed.reserve(work.size());
            for (const value & item : work) {
                if (item.is_undefined()) {
                    ++undefined_count;
                } else {
                    keyed.emplace_back(c.to_string(item), item);
                }
            }
            std::ranges::stable_sort(keyed, {}, &std::pair<std::string, value>::first);
            work.clear();
            for (const auto & [key, item] : keyed) { work.push_back(item); }
            work.resize(work.size() + undefined_count, value::undefined());
            return;
        }
        // A BOTTOM-UP MERGE SORT, and every word of that is load-bearing.
        //
        // `std::sort` would be undefined behaviour with an inconsistent
        // comparator, and a comparator written in JavaScript can be anything at
        // all - it can return random numbers, or mutate the array it is
        // sorting. That is why this was a stable INSERTION sort: it cannot be
        // talked into reading out of bounds.
        //
        // But it was O(n^2), measured: 250 elements 0.5 ms, 4000 elements
        // 104 ms, tracking n^2 to within a tenth. Ten thousand would be most of
        // a second and a page would look hung.
        //
        // Merge sort keeps the property that mattered. Each merge reads only
        // within two index ranges it computed itself, so no answer the
        // comparator gives can move an index out of them - the safety comes
        // from the STRUCTURE rather than from the comparator behaving. It is
        // stable, which the specification requires. And it is n log n.
        const std::size_t n = work.size();
        if (n <= 1) { return; }
        std::vector<value> spare(n);
        for (std::size_t width = 1; width < n; width *= 2) {
            for (std::size_t lo = 0; lo < n; lo += 2 * width) {
                const std::size_t mid = std::min(lo + width, n);
                const std::size_t hi = std::min(lo + 2 * width, n);
                std::size_t left = lo, right = mid, out = lo;
                while (left < mid && right < hi) {
                    // undefined IS NEVER COMPARED, in this arm either: it sorts
                    // after everything, so it loses every merge it is on the
                    // left of and wins none.
                    double order = 0;
                    if (work[left].is_undefined()) {
                        order = work[right].is_undefined() ? 0.0 : 1.0;
                    } else if (work[right].is_undefined()) {
                        order = -1.0;
                    } else {
                        const value pair[2] = {work[left], work[right]};
                        order = context::to_number(c.call(comparator, pair));
                    }
                    // `<= 0` TAKES THE LEFT, which is what makes this stable:
                    // equal elements keep their order. A NaN comparator answer
                    // is treated as 0 by the same test, which 23.1.3.30 says.
                    spare[out++] = order <= 0 ? work[left++] : work[right++];
                }
                while (left < mid) { spare[out++] = work[left++]; }
                while (right < hi) { spare[out++] = work[right++]; }
            }
            work.swap(spare);
        }
    };
    method(cx, array_proto, "sort", 1, [sort_values](context & c, std::span<value> a) {
        const value comparator = arg_at(a, 0);
        // The comparator is checked BEFORE the receiver is touched (23.1.3.30
        // step 1), so `[].sort(1)` is a TypeError rather than a sorted nothing.
        if (!comparator.is_undefined() && !comparator.is_callable()) {
            c.throw_error("TypeError",
                          "The comparison function must be either a function or undefined");
            return c.current_this();
        }
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "sort")) { return self; }
        if (array_object * dense = detail::dense_array_this(self)) {
            // ON A SNAPSHOT, which is a robustness fix rather than a speed one.
            // The old loop indexed `items` while calling out to the comparator,
            // so a comparator that shortened the array - `a.sort(() => {
            // a.length = 0; return 0; })` - left it indexing past the end.
            // Sorting a copy and writing it back cannot: the comparator may do
            // what it likes to the array meanwhile.
            std::vector<value> work = dense->items;
            sort_values(c, work, comparator);
            // ASSIGNED, not written slot by slot, so both arms agree about what
            // the array holds afterwards. Where nothing mutated during the sort
            // the two spellings are identical; where something did, this is
            // defined and the old one was not.
            dense->items = std::move(work);
            return self;
        }
        if (!detail::mutable_receiver(c, self, "sort")) { return self; }
        const double len = detail::array_like_length(c, self);
        if (!detail::generic_walk_ok(c, len)) { return self; }
        // SortIndexedProperties, 23.1.3.30.1, with holes SKIPPED: the present
        // elements are read out, sorted, written back over 0..n-1, and every
        // index the holes used to occupy is deleted - which is what moves them
        // all to the end.
        //
        // INTO A ROOTED ARRAY rather than a bare std::vector<value>, for the
        // reason `toSorted` gives: a getter can collect and a value held only
        // by a C++ vector is in none of the collector's roots.
        value holder = c.make_array();
        const context::rooted keep(c, holder);
        auto & work = static_cast<array_object *>(holder.as_heap())->items;
        for (double k = 0; k < len; k += 1.0) {
            if (detail::has_element(c, self, k)) { work.push_back(detail::element_at(c, self, k)); }
        }
        sort_values(c, work, comparator);
        const auto kept = static_cast<double>(work.size());
        for (double k = 0; k < kept; k += 1.0) {
            detail::put_element(c, self, k, work[static_cast<std::size_t>(k)]);
        }
        for (double k = kept; k < len; k += 1.0) { detail::delete_element(c, self, k); }
        return self;
    });
    detail::constant(array_ctor, "prototype", value::object(array_proto));
    // `Array.prototype.toString` IS join(','). The C++ conversion always knew
    // that; the prototype did not, so once conversion started going through an
    // object's own toString an array fell back to Object.prototype's and
    // stringified as "[object Array]".
    method(cx, array_proto, "toString", 0, [](context & c, std::span<value>) {
        auto * self = detail::this_array(c);
        if (self == nullptr) { return c.string(""); }
        std::string out;
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            if (i != 0) { out += ','; }
            if (!self->items[i].is_nullish()) { out += c.to_string(self->items[i]); }
        }
        return c.string(out);
    });
    // `entries`, `keys` and `values`, as real iterators - see list_iterator for
    // why they answer both protocols and where the two disagree.
    method(cx, array_proto, "entries", 0, [](context & c, std::span<value>) {
        auto * self = detail::this_array(c);
        value out = c.make_array();
        if (self == nullptr) { return out; }
        auto * pairs = static_cast<array_object *>(out.as_heap());
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            value pair = c.make_array();
            auto * both = static_cast<array_object *>(pair.as_heap());
            both->items.push_back(value::number(static_cast<double>(i)));
            both->items.push_back(self->items[i]);
            pairs->items.push_back(pair);
        }
        return list_iterator(c, out, "Array Iterator");
    });
    method(cx, array_proto, "keys", 0, [](context & c, std::span<value>) {
        auto * self = detail::this_array(c);
        value out = c.make_array();
        if (self != nullptr) {
            auto * items = static_cast<array_object *>(out.as_heap());
            for (std::size_t i = 0; i < self->items.size(); ++i) {
                items->items.push_back(value::number(static_cast<double>(i)));
            }
        }
        return list_iterator(c, out, "Array Iterator");
    });
    method(cx, array_proto, "values", 0, [](context & c, std::span<value>) {
        auto * self = detail::this_array(c);
        value out = c.make_array();
        if (self != nullptr) { static_cast<array_object *>(out.as_heap())->items = self->items; }
        return list_iterator(c, out, "Array Iterator");
    });
    // --- THE FIVE THAT WERE NOT HERE AT ALL ---------------------------------
    //
    // `copyWithin` (23.1.3.4) and the four change-by-copy methods added in
    // ES2023 - `with`, `toReversed`, `toSorted`, `toSpliced` (23.1.3.39, .33,
    // .34, .35). test262 spends 39, 21, 17, 21 and 30 files on them and every
    // one read "TypeError: X is undefined, not a function". All five are
    // generic over the receiver; the four copying ones build an ordinary Array
    // whatever they were called on, which is what the specification says and
    // what makes `Array.prototype.toReversed.call({length: 2, ...})` an array.
    method(cx, array_proto, "copyWithin", 2, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!detail::coercible_this(c, self, "copyWithin")) { return self; }
        const double len = detail::array_like_length(c, self);
        const double raw_to = integer_arg(c, a, 0);
        double to = raw_to < 0 ? std::max(len + raw_to, 0.0) : std::min(raw_to, len);
        const double raw_from = integer_arg(c, a, 1);
        double from = raw_from < 0 ? std::max(len + raw_from, 0.0) : std::min(raw_from, len);
        const double raw_end = has_index(a, 2) ? integer_arg(c, a, 2) : len;
        const double end = raw_end < 0 ? std::max(len + raw_end, 0.0) : std::min(raw_end, len);
        double count = std::min(end - from, len - to);
        // OVERLAPPING RANGES COPY BACKWARDS. Forwards would overwrite a source
        // element before reading it, which is the one thing memmove semantics
        // are for and the one thing a naive loop gets wrong.
        double direction = 1.0;
        if (count > 0 && from < to && to < from + count) {
            direction = -1.0;
            from += count - 1;
            to += count - 1;
        }
        for (; count > 0; count -= 1.0, from += direction, to += direction) {
            if (detail::has_element(c, self, from)) {
                detail::put_element(c, self, to, detail::element_at(c, self, from));
            } else {
                c.delete_index(self, value::number(to));
            }
        }
        return self;
    });
    method(cx, array_proto, "with", 2, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        value out = c.make_array();
        if (!detail::coercible_this(c, self, "with")) { return out; }
        const double len = detail::array_like_length(c, self);
        const double relative = integer_arg(c, a, 0);
        const double at = relative >= 0 ? relative : len + relative;
        // OUT OF RANGE IS A RangeError, not a silent grow: `with` exists to
        // hand back a copy of the same shape, so an index it cannot hold is
        // a question with no answer.
        if (at >= len || at < 0) {
            c.throw_error("RangeError", "Invalid index");
            return out;
        }
        const context::rooted keep(c, out);
        if (detail::new_array_of_length(c, out, len) == nullptr) { return out; }
        const value replacement = arg_at(a, 1);
        for (double k = 0; k < len; k += 1.0) {
            detail::put_element(c, out, k, k == at ? replacement : detail::element_at(c, self, k));
        }
        return out;
    });
    method(cx, array_proto, "toReversed", 0, [](context & c, std::span<value>) {
        const value self = c.current_this();
        value out = c.make_array();
        if (!detail::coercible_this(c, self, "toReversed")) { return out; }
        const double len = detail::array_like_length(c, self);
        const context::rooted keep(c, out);
        if (detail::new_array_of_length(c, out, len) == nullptr) { return out; }
        for (double k = 0; k < len; k += 1.0) {
            detail::put_element(c, out, k, detail::element_at(c, self, len - k - 1));
        }
        return out;
    });
    // `toSorted` and `toSpliced` READ THE WHOLE RECEIVER FIRST, into a rooted
    // vector, and then build the result out of it. That is what the
    // specification's SortIndexedProperties does and it is also the only shape
    // that survives a comparator which mutates the array it was handed.
    method(cx, array_proto, "toSorted", 1, [sort_values](context & c, std::span<value> a) {
        const value self = c.current_this();
        value out = c.make_array();
        const value comparator = arg_at(a, 0);
        // The comparator is checked BEFORE the receiver is read, which is the
        // order 23.1.3.34 gives and which one test262 file per method asserts.
        if (!comparator.is_undefined() && !comparator.is_callable()) {
            c.throw_error("TypeError", "The comparison function must be either a function or "
                                       "undefined");
            return out;
        }
        if (!detail::coercible_this(c, self, "toSorted")) { return out; }
        const double len = detail::array_like_length(c, self);
        if (len > max_array_length) {
            c.throw_error("RangeError", "Invalid array length");
            return out;
        }
        const context::rooted keep(c, out);
        // READ INTO THE RESULT, not into a bare std::vector. `element_at` can
        // run a getter, a getter can collect, and a value sitting only in a C++
        // vector is in none of the collector's roots - the same hazard `sort`
        // documents. Everything pushed here is reachable through `out`, which
        // is rooted, from the moment it lands.
        auto * result = static_cast<array_object *>(out.as_heap());
        result->items.reserve(static_cast<std::size_t>(std::min(len, 65536.0)));
        for (double k = 0; k < len; k += 1.0) {
            result->items.push_back(detail::element_at(c, self, k));
        }
        sort_values(c, result->items, comparator);
        return out;
    });
    method(cx, array_proto, "toSpliced", 2, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        value out = c.make_array();
        if (!detail::coercible_this(c, self, "toSpliced")) { return out; }
        const double len = detail::array_like_length(c, self);
        const double raw_start = integer_arg(c, a, 0);
        const double start =
            raw_start < 0 ? std::max(len + raw_start, 0.0) : std::min(raw_start, len);
        const double inserted = a.size() > 2 ? static_cast<double>(a.size() - 2) : 0.0;
        double skipped = 0;
        if (a.empty()) {
            skipped = 0;
        } else if (a.size() == 1) {
            skipped = len - start;
        } else {
            skipped = std::min(std::max(integer_arg(c, a, 1), 0.0), len - start);
        }
        const double new_len = len + inserted - skipped;
        // 2^53-1 is a TypeError here rather than the RangeError ArrayCreate
        // gives, because 23.1.3.35 step 12 checks it BEFORE creating anything.
        if (new_len > max_safe_integer) {
            c.throw_error("TypeError", "Invalid array length");
            return out;
        }
        const context::rooted keep(c, out);
        if (detail::new_array_of_length(c, out, new_len) == nullptr) { return out; }
        double at = 0;
        for (; at < start; at += 1.0) {
            detail::put_element(c, out, at, detail::element_at(c, self, at));
        }
        for (std::size_t i = 2; i < a.size(); ++i, at += 1.0) {
            detail::put_element(c, out, at, a[i]);
        }
        for (double from = start + skipped; at < new_len; at += 1.0, from += 1.0) {
            detail::put_element(c, out, at, detail::element_at(c, self, from));
        }
        return out;
    });
    link_constructor(cx, array_proto, "Array", 1, value::object(array_ctor));
    cx.set_prototype(context::proto_kind::array, array_proto);
}

// `Map`, `Set`, `WeakMap`, `WeakSet`. 20 and 59 uses in p5.js, and `new Map()`
// is what stopped it once Array and Number were there.
//
// Both are built on the object model rather than on a new heap kind: entries
// live in a plain array on the instance, which makes lookup linear. That is the
// wrong complexity and it is written down rather than hidden - the maps p5
// builds are small and keyed by strings, and a hash keyed on a NaN-boxed value
// wants SameValueZero over every value kind, which is a bigger piece of work
// than this needs to be today.
//
// TODO: Map and Set lookup is LINEAR - a hash keyed on a NaN-boxed value wants
// SameValueZero over every value kind. Fine for the small string-keyed maps p5
// builds; wrong complexity for anything larger.
// TODO: WeakMap and WeakSet keep their keys alive. That is a leak, not a wrong
// answer, and it needs weak references the collector understands.
// WeakMap and WeakSet are the strong versions under different names: nothing
// here has weak references, so an entry keeps its key alive. Said out loud
// because the difference is a leak, not a wrong answer.
void install_collections(context & cx) {
    using detail::method;
    using detail::new_table;

    // The entry list of the receiver, or null when called on something else.
    const auto entries_of = [](context & c) -> array_object * {
        const value self = c.current_this();
        if (!self.is_object()) { return nullptr; }
        value * held = static_cast<object_object *>(self.as_heap())->find("__entries");
        return held != nullptr && held->is_array() ? static_cast<array_object *>(held->as_heap())
                                                   : nullptr;
    };
    // SameValueZero, which is what Map and Set key on: like ===, except NaN
    // matches NaN. A page that uses NaN as a key is doing something odd, but
    // getting it wrong here would be a silent miss.
    const auto same_value_zero = [](value a, value b) { return a.same_value_zero(b); };

    const auto build = [&](const char * name, bool keyed) {
        object_object * proto = new_table(cx);
        auto * ctor = cx.allocate<native_object>(name, [keyed, same_value_zero](
                                                           context & c, std::span<value> a) {
            // INITIALISE THE RECEIVER when there is one. `class MySet extends
            // Set {}` reaches here through super() with the new instance as
            // `this`; making a fresh object instead left the instance with none
            // of Set's state, and every method on it then failed somewhere
            // else entirely.
            value self = c.current_this();
            if (!self.is_object()) { self = c.make_object(); }
            auto * made = static_cast<object_object *>(self.as_heap());
            made->set("__entries", c.make_array());
            if (!made->prototype.is_object()) {
                if (object_object * table =
                        c.prototype(keyed ? context::proto_kind::map : context::proto_kind::set)) {
                    made->prototype = value::object(table);
                }
            }
            // `new Map([[k, v], ...])` and `new Set([...])` seed from ANY
            // iterable, through the same conversion for..of uses - so `new
            // Set(otherSet)` and `new Map(map.entries())` work, which is how a page
            // copies one.
            const value seed = a.empty() ? value::undefined() : c.iterable_values(a[0]);
            if (seed.is_array()) {
                auto * entries = static_cast<array_object *>(
                    static_cast<object_object *>(self.as_heap())->find("__entries")->as_heap());
                for (const value & item : static_cast<array_object *>(seed.as_heap())->items) {
                    if (keyed) {
                        if (!item.is_array()) { continue; }
                        const auto & pair = static_cast<array_object *>(item.as_heap())->items;
                        const value key = pair.empty() ? value::undefined() : pair[0];
                        const value held = pair.size() > 1 ? pair[1] : value::undefined();
                        // A REPEATED KEY REPLACES, it does not append. `new Map([['a',
                        // 1], ['a', 2]])` has ONE entry and it holds 2 - and a
                        // duplicate that merely sat there would be found by get()
                        // and missed by size(), which is two answers to one question.
                        bool replaced = false;
                        for (const value & existing : entries->items) {
                            if (!existing.is_array()) { continue; }
                            auto & cell = static_cast<array_object *>(existing.as_heap())->items;
                            if (!cell.empty() && same_value_zero(cell[0], key)) {
                                if (cell.size() > 1) {
                                    cell[1] = held;
                                } else {
                                    cell.push_back(held);
                                }
                                replaced = true;
                                break;
                            }
                        }
                        if (replaced) { continue; }
                        value entry = c.make_array();
                        auto * cell = static_cast<array_object *>(entry.as_heap());
                        cell->items.push_back(key);
                        cell->items.push_back(held);
                        entries->items.push_back(entry);
                        continue;
                    }
                    // A SET DEDUPES, which is the entire reason to use one.
                    // `new Set([1, 2, 2, 3]).size` was 4 - so `[...new Set(v)]`
                    // was not a unique list, and p5's colour-space registry is
                    // exactly that idiom.
                    bool seen = false;
                    for (const value & existing : entries->items) {
                        if (same_value_zero(existing, item)) {
                            seen = true;
                            break;
                        }
                    }
                    if (!seen) { entries->items.push_back(item); }
                }
            }
            return self;
        });
        // `class X extends Map` calls `super()`, which resolves through the
        // parent prototype's `constructor` - absent, and the class could not be
        // instantiated at all.
        proto->set("constructor", value::object(ctor));
        detail::constant(ctor, "prototype", value::object(proto));
        cx.set_prototype(keyed ? context::proto_kind::map : context::proto_kind::set, proto);
        cx.define_global(name, value::object(ctor));
        return proto;
    };

    // --- Map ---------------------------------------------------------------
    object_object * map_proto = build("Map", true);
    const auto find_entry = [entries_of, same_value_zero](context & c, value key) -> value * {
        array_object * entries = entries_of(c);
        if (entries == nullptr) { return nullptr; }
        for (value & entry : entries->items) {
            auto * pair = static_cast<array_object *>(entry.as_heap());
            if (!pair->items.empty() && same_value_zero(pair->items[0], key)) { return &entry; }
        }
        return nullptr;
    };
    method(cx, map_proto, "get", 1, [find_entry](context & c, std::span<value> a) {
        value * entry = find_entry(c, arg_at(a, 0));
        if (entry == nullptr) { return value::undefined(); }
        const auto & pair = static_cast<array_object *>(entry->as_heap())->items;
        return pair.size() > 1 ? pair[1] : value::undefined();
    });
    method(cx, map_proto, "has", 1, [find_entry](context & c, std::span<value> a) {
        return value::boolean(find_entry(c, arg_at(a, 0)) != nullptr);
    });
    method(cx, map_proto, "set", 2, [find_entry, entries_of](context & c, std::span<value> a) {
        if (value * entry = find_entry(c, arg_at(a, 0))) {
            static_cast<array_object *>(entry->as_heap())->items[1] = arg_at(a, 1);
            return c.current_this();
        }
        if (array_object * entries = entries_of(c)) {
            value pair = c.make_array();
            auto * cell = static_cast<array_object *>(pair.as_heap());
            cell->items.push_back(arg_at(a, 0));
            cell->items.push_back(arg_at(a, 1));
            entries->items.push_back(pair);
        }
        return c.current_this();
    });
    method(
        cx, map_proto, "delete", 1, [entries_of, same_value_zero](context & c, std::span<value> a) {
            array_object * entries = entries_of(c);
            if (entries == nullptr) { return value::boolean(false); }
            for (std::size_t i = 0; i < entries->items.size(); ++i) {
                auto * pair = static_cast<array_object *>(entries->items[i].as_heap());
                if (!pair->items.empty() && same_value_zero(pair->items[0], arg_at(a, 0))) {
                    entries->items.erase(entries->items.begin() + static_cast<std::ptrdiff_t>(i));
                    return value::boolean(true);
                }
            }
            return value::boolean(false);
        });
    method(cx, map_proto, "clear", 0, [entries_of](context & c, std::span<value>) {
        if (array_object * entries = entries_of(c)) { entries->items.clear(); }
        return value::undefined();
    });
    method(cx, map_proto, "forEach", 1, [entries_of](context & c, std::span<value> a) {
        array_object * entries = entries_of(c);
        if (entries == nullptr || a.empty() || !a[0].is_callable()) { return value::undefined(); }
        // A copy: a callback that mutates the map must not invalidate the walk.
        const std::vector<value> snapshot = entries->items;
        for (const value & entry : snapshot) {
            const auto & pair = static_cast<array_object *>(entry.as_heap())->items;
            const value args[3] = {pair.size() > 1 ? pair[1] : value::undefined(),
                                   pair.empty() ? value::undefined() : pair[0], c.current_this()};
            (void)c.call(a[0], args);
        }
        return value::undefined();
    });
    const auto column = [entries_of](context & c, int which) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        if (array_object * entries = entries_of(c)) {
            for (const value & entry : entries->items) {
                const auto & pair = static_cast<array_object *>(entry.as_heap())->items;
                if (which == 2) {
                    result->items.push_back(entry);
                } else if (static_cast<std::size_t>(which) < pair.size()) {
                    result->items.push_back(pair[static_cast<std::size_t>(which)]);
                }
            }
        }
        return out;
    };
    // REAL ITERATORS. They were arrays, with a comment saying `map.keys().next()`
    // does not work - and Babylon drives exactly that, by hand, for the Map of
    // shadow generators a light owns. See list_iterator.
    method(cx, map_proto, "keys", 0, [column](context & c, std::span<value>) {
        return list_iterator(c, column(c, 0), "Map Iterator");
    });
    method(cx, map_proto, "values", 0, [column](context & c, std::span<value>) {
        return list_iterator(c, column(c, 1), "Map Iterator");
    });
    method(cx, map_proto, "entries", 0, [column](context & c, std::span<value>) {
        return list_iterator(c, column(c, 2), "Map Iterator");
    });
    map_proto->define_accessor(
        "size",
        value::object(cx.allocate<native_object>(
            "size",
            [entries_of](context & c, std::span<value>) {
                array_object * e = entries_of(c);
                return value::number(e == nullptr ? 0.0 : static_cast<double>(e->items.size()));
            })),
        value::undefined());

    // --- Set ---------------------------------------------------------------
    object_object * set_proto = build("Set", false);
    const auto set_index = [entries_of, same_value_zero](context & c, value v) -> std::ptrdiff_t {
        array_object * entries = entries_of(c);
        if (entries == nullptr) { return -1; }
        for (std::size_t i = 0; i < entries->items.size(); ++i) {
            if (same_value_zero(entries->items[i], v)) { return static_cast<std::ptrdiff_t>(i); }
        }
        return -1;
    };
    method(cx, set_proto, "has", 1, [set_index](context & c, std::span<value> a) {
        return value::boolean(set_index(c, arg_at(a, 0)) >= 0);
    });
    method(cx, set_proto, "add", 1, [set_index, entries_of](context & c, std::span<value> a) {
        if (set_index(c, arg_at(a, 0)) < 0) {
            if (array_object * entries = entries_of(c)) { entries->items.push_back(arg_at(a, 0)); }
        }
        return c.current_this();
    });
    method(cx, set_proto, "delete", 1, [set_index, entries_of](context & c, std::span<value> a) {
        const std::ptrdiff_t at = set_index(c, arg_at(a, 0));
        if (at < 0) { return value::boolean(false); }
        array_object * entries = entries_of(c);
        entries->items.erase(entries->items.begin() + at);
        return value::boolean(true);
    });
    method(cx, set_proto, "clear", 0, [entries_of](context & c, std::span<value>) {
        if (array_object * entries = entries_of(c)) { entries->items.clear(); }
        return value::undefined();
    });
    method(cx, set_proto, "forEach", 1, [entries_of](context & c, std::span<value> a) {
        array_object * entries = entries_of(c);
        if (entries == nullptr || a.empty() || !a[0].is_callable()) { return value::undefined(); }
        const std::vector<value> snapshot = entries->items;
        for (const value & item : snapshot) {
            const value args[3] = {item, item, c.current_this()};
            (void)c.call(a[0], args);
        }
        return value::undefined();
    });
    const auto members = [entries_of](context & c) {
        value out = c.make_array();
        if (array_object * entries = entries_of(c)) {
            static_cast<array_object *>(out.as_heap())->items = entries->items;
        }
        return out;
    };
    method(cx, set_proto, "values", 0, [members](context & c, std::span<value>) {
        return list_iterator(c, members(c), "Set Iterator");
    });
    method(cx, set_proto, "keys", 0, [members](context & c, std::span<value>) {
        return list_iterator(c, members(c), "Set Iterator");
    });
    // A Set's `entries` pairs each member WITH ITSELF, which looks odd and is
    // the spec: it exists so a Set and a Map can be walked by the same code.
    method(cx, set_proto, "entries", 0, [members](context & c, std::span<value>) {
        const value all = members(c);
        value out = c.make_array();
        if (!all.is_array()) { return out; }
        auto * pairs = static_cast<array_object *>(out.as_heap());
        for (const value & member : static_cast<array_object *>(all.as_heap())->items) {
            value pair = c.make_array();
            auto * both = static_cast<array_object *>(pair.as_heap());
            both->items.push_back(member);
            both->items.push_back(member);
            pairs->items.push_back(pair);
        }
        return list_iterator(c, out, "Set Iterator");
    });
    set_proto->define_accessor(
        "size",
        value::object(cx.allocate<native_object>(
            "size",
            [entries_of](context & c, std::span<value>) {
                array_object * e = entries_of(c);
                return value::number(e == nullptr ? 0.0 : static_cast<double>(e->items.size()));
            })),
        value::undefined());

    // Strong, under a weak name - see the note at the top.
    cx.define_global("WeakMap", cx.global("Map"));
    cx.define_global("WeakSet", cx.global("Set"));
}

// TYPED ARRAYS. 123 uses in p5.js - Uint8Array for pixels, Float32Array for
// matrices - and `new Uint32Array(n)` is what stopped the bundle once
// localStorage was there.
//
// Stored as ordinary arrays of values rather than packed bytes: that costs
// memory and buys the whole existing array machinery - indexing, length,
// iteration, every prototype method - for nothing. What it does NOT cost is
// correctness on write, which is where a shortcut would have hurt: the element
// coercion is real, so a Uint8ClampedArray clamps and a Uint8Array wraps.
//
// AN ARRAYBUFFER IS SHARED STORAGE. A view over the WHOLE of one is that
// storage rather than a copy, so two views see each other's writes - which is
// the entire reason a page wraps `await res.arrayBuffer()` in one.
//
// The gap that remains is a SUB-RANGE view: `new Uint8Array(buf, 4, 8)` cannot
// be expressed while a view owns its own elements, and it REFUSES with a
// RangeError rather than handing back a copy that would silently not alias.
// Expressing it wants a view to address a span of someone else's storage, which
// is a change to every one of the ~176 places that reach for `array_object
// ::items` - worth doing when something needs it, and worth refusing rather
// than faking until then.
void install_typed_arrays(context & cx) {
    using detail::method;
    using detail::new_table;

    struct spec {
        const char * name;
        element_kind kind;
        int bytes;
    };
    static constexpr spec kinds[] = {
        {"Int8Array", element_kind::i8, 1},
        {"Uint8Array", element_kind::u8, 1},
        {"Uint8ClampedArray", element_kind::u8_clamped, 1},
        {"Int16Array", element_kind::i16, 2},
        {"Uint16Array", element_kind::u16, 2},
        {"Int32Array", element_kind::i32, 4},
        {"Uint32Array", element_kind::u32, 4},
        {"Float32Array", element_kind::f32, 4},
        {"Float64Array", element_kind::f64, 8},
    };

    object_object * typed_proto = new_table(cx);
    method(cx, typed_proto, "set", 1, [](context & c, std::span<value> a) {
        auto * self = detail::this_array(c);
        if (self == nullptr || !arg_at(a, 0).is_array()) { return value::undefined(); }
        auto * source = static_cast<array_object *>(a[0].as_heap());
        const auto at = static_cast<std::size_t>(std::max(0.0, num_at(a, 1)));
        // EITHER SIDE MAY BE A VIEW, so both go through the accessors rather
        // than touching `items` - a view's `items` is empty by design.
        for (std::size_t i = 0; i < source->length() && at + i < self->length(); ++i) {
            const double each =
                source->is_view() ? view_get(*source, i) : context::to_number(source->items[i]);
            if (self->is_view()) {
                view_set(*self, at + i, each);
            } else {
                self->items[at + i] = value::number(coerce_element(self->elements, each));
            }
        }
        return value::undefined();
    });
    method(cx, typed_proto, "subarray", 2, [](context & c, std::span<value> a) {
        auto * self = detail::this_array(c);
        value out = c.make_array();
        if (self == nullptr) { return out; }
        auto * made = static_cast<array_object *>(out.as_heap());
        made->elements = self->elements;
        const std::size_t n = self->length();
        const std::size_t from = a.empty() ? 0 : clamp_index(num_at(a, 0), n);
        const std::size_t to = a.size() > 1 ? clamp_index(num_at(a, 1), n) : n;
        // SHARES THE BYTES when the receiver does. `subarray` is a view onto
        // the same storage, not a copy - a page uploads
        // `view.subarray(0, used)` and expects writes made through the parent
        // to be in it. `slice` is the copying one, and is a different method.
        if (self->is_view()) {
            made->viewed = self->viewed;
            made->byte_offset = static_cast<std::uint32_t>(
                self->byte_offset + from * bytes_per_element(self->elements));
            made->view_length = static_cast<std::uint32_t>(to > from ? to - from : 0);
            return out;
        }
        for (std::size_t i = from; i < to; ++i) { made->items.push_back(self->items[i]); }
        return out;
    });
    cx.set_prototype(context::proto_kind::typed_array, typed_proto);

    for (const spec & each : kinds) {
        const element_kind kind = each.kind;
        auto * ctor = cx.allocate<native_object>(each.name, [kind](context & c,
                                                                   std::span<value> a) {
            value out = c.make_array();
            auto * made = static_cast<array_object *>(out.as_heap());
            made->elements = kind;
            const value from = arg_at(a, 0);
            if (from.is_array()) {
                // from another array, coerced element by element
                for (const value & v : static_cast<array_object *>(from.as_heap())->items) {
                    made->items.push_back(
                        value::number(coerce_element(kind, context::to_number(v))));
                }
            } else if (from.is_object()) {
                // AN ARRAYBUFFER IS SHARED STORAGE, so a view over the whole of
                // one IS that storage rather than a copy of it: two views over
                // a buffer see each other's writes, which is the entire reason
                // a page wraps `await res.arrayBuffer()` in one.
                //
                // A SUB-RANGE view - `new Uint8Array(buf, 4, 8)` - cannot be
                // expressed while a view owns its own elements, so it REFUSES
                // rather than handing back a silently independent copy. That is
                // the same choice WEBGL and `new Function` were given: a page
                // that reaches the gap is told.
                const value bytes = c.lookup_property(from, "__bytes");
                if (bytes.is_array()) {
                    // A VIEW, WITH ITS OWN KIND. This used to hand back the
                    // buffer's own array with its element kind overwritten,
                    // which meant several views over one buffer were the SAME
                    // object and only the last one's kind survived. Phaser
                    // makes four, so its float writes were stored as integers.
                    auto * store = static_cast<array_object *>(bytes.as_heap());
                    const auto width = bytes_per_element(kind);
                    const auto total = store->items.size();
                    const auto offset =
                        a.size() > 1 ? static_cast<std::size_t>(std::max(0.0, num_at(a, 1))) : 0;
                    const std::size_t rest = offset < total ? total - offset : 0;
                    const auto count = a.size() > 2
                                           ? static_cast<std::size_t>(std::max(0.0, num_at(a, 2)))
                                           : rest / width;
                    made->viewed = bytes;
                    made->byte_offset = static_cast<std::uint32_t>(offset);
                    made->view_length = static_cast<std::uint32_t>(std::min(count, rest / width));
                    return out;
                }
                // Anything else with a length: a fresh zeroed view of that size.
                const double length = context::to_number(c.lookup_property(from, "length"));
                const double n = std::isnan(length)
                                     ? context::to_number(c.lookup_property(from, "byteLength"))
                                     : length;
                made->items.assign(static_cast<std::size_t>(std::max(0.0, n)), value::number(0));
            } else {
                made->items.assign(static_cast<std::size_t>(std::max(0.0, num_at(a, 0))),
                                   value::number(0));
            }
            return out;
        });
        detail::constant(ctor, "BYTES_PER_ELEMENT", value::number(each.bytes));

        // `Float32Array.from` and `.of`, WHICH ARE NOT THE SAME FUNCTIONS AS
        // `Array.from` and `.of`: they coerce into this view's element kind, so
        // `Float32Array.from([1.5])` keeps 1.5 and `Uint8Array.from([1.5])`
        // does not. Delegating to the Array versions would have been the wrong
        // answer rather than a missing one.
        //
        // p5's WEBGL renderer builds its matrices with `Float32Array.from`, so
        // without these the constructor threw ``from` is undefined` and p5 fell
        // back to Renderer2D - which the API probe saw only as the wrong
        // renderer, several layers away from the cause.
        const auto build = [kind](context & c, std::span<value> items, const value * mapper) {
            value out = c.make_array();
            auto * made = static_cast<array_object *>(out.as_heap());
            made->elements = kind;
            for (std::size_t i = 0; i < items.size(); ++i) {
                value v = items[i];
                if (mapper != nullptr && mapper->is_callable()) {
                    const value call_args[2]{v, value::number(static_cast<double>(i))};
                    v = c.call(*mapper, call_args);
                }
                made->items.push_back(value::number(coerce_element(kind, context::to_number(v))));
            }
            return out;
        };
        method(cx, ctor, "of", 0,
               [build](context & c, std::span<value> a) { return build(c, a, nullptr); });
        method(cx, ctor, "from", 1, [build](context & c, std::span<value> a) {
            const value source = arg_at(a, 0);
            const value mapper = arg_at(a, 1);
            // An iterable OR an array-like, because both reach here: p5 passes
            // real arrays, and `from(gl.getParameter(...))` passes a view.
            std::vector<value> items;
            if (source.is_array()) {
                items = static_cast<array_object *>(source.as_heap())->items;
            } else if (source.is_object()) {
                const value seq = c.iterable_values(source);
                if (seq.is_array()) { items = static_cast<array_object *>(seq.as_heap())->items; }
                if (items.empty()) {
                    // Array-LIKE rather than iterable: `{length: 2, 0: ..., 1: ...}`,
                    // which is what `arguments` and several DOM lists are. An empty
                    // iterable lands here too and simply finds no length, so the
                    // ambiguity costs a lookup and not an answer.
                    const double n = context::to_number(c.lookup_property(source, "length"));
                    for (double i = 0; i < n; ++i) {
                        items.push_back(
                            c.lookup_property(source, std::to_string(static_cast<long long>(i))));
                    }
                }
            }
            return build(c, items, &mapper);
        });
        cx.define_global(each.name, value::object(ctor));
    }

    // An ArrayBuffer is a LENGTH here, not storage - see the note above.
    // AN ARRAYBUFFER OWNS BYTES, and hands the same storage to every view made
    // over the whole of it. It used to be a length and nothing else, so two
    // views were silently independent and a page that wrote through one and
    // read through the other got zeroes.
    cx.define_native("ArrayBuffer", [](context & c, std::span<value> a) {
        value out = c.make_object();
        auto * made = static_cast<object_object *>(out.as_heap());
        const auto n = static_cast<std::size_t>(std::max(0.0, num_at(a, 0)));
        made->set("byteLength", value::number(static_cast<double>(n)));
        made->set("length", value::number(static_cast<double>(n)));
        value bytes = c.make_array();
        auto * store = static_cast<array_object *>(bytes.as_heap());
        store->elements = element_kind::u8;
        store->items.assign(n, value::number(0));
        made->set("__bytes", bytes);
        return out;
    });

    // `ArrayBuffer.isView(x)` - IS THIS A TYPED ARRAY OR A DataView.
    //
    // One method, and without it the whole of Babylon's physically-based
    // material path was unreachable: PBRMaterial and
    // PBRMetallicRoughnessMaterial both threw `isView is not a function` in
    // their constructors. It is the cheapest item docs/plans/babylon.md
    // measured, by a wide margin.
    //
    // THE ANSWER IS THE OBJECT'S OWN, not a guess from its shape: an
    // array_object records the element kind it views bytes with, so a typed
    // array and an ordinary array are told apart by what they ARE rather than
    // by whether they happen to have a `BYTES_PER_ELEMENT` property.
    //
    // ON THE NATIVE'S OWN TABLE. `ArrayBuffer` is a native_object, not an
    // object_object - define_native makes a function that is also an object -
    // so it carries its statics in `props`, and casting it to object_object
    // writes the property somewhere nothing will ever look for it.
    const value array_buffer = cx.global("ArrayBuffer");
    if (array_buffer.is_kind(heap_kind::native)) {
        auto * table = static_cast<native_object *>(array_buffer.as_heap());
        table->set(
            "isView",
            value::object(cx.allocate<native_object>("isView", [](context &, std::span<value> a) {
                if (a.empty() || !a[0].is_array()) { return value::boolean(false); }
                const auto * made = static_cast<array_object *>(a[0].as_heap());
                return value::boolean(made->elements != element_kind::none);
            })));
    }
}

} // namespace ctbrowser::script::builtins_detail
