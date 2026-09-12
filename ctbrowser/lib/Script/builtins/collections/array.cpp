// ctbrowser.script builtins - Array: the constructor, its statics, and the
// prototype methods up to the callback-taking ones; install_array_iteration in
// array_iteration.cpp installs the rest, in the same order as before.
//
// One of four files carved out of a 1,824-line builtins/collections.cpp on
// 2026-09-08 - which was itself one of five carved out of builtins.cpp on
// 2026-08-09. Everything shared - the argument helpers, namespace detail, and
// these functions' declarations - is in ../internal.hpp; what only this
// directory shares is in internal.hpp beside this.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

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
    // IsArray, 7.2.2: a Proxy is asked through to its target, and a revoked
    // one (both slots null) is a TypeError.
    static_method("isArray", 1, [](context & c, std::span<value> a) {
        value v = arg_at(a, 0);
        for (int hops = 0; hops < 64 && v.is_kind(heap_kind::proxy); ++hops) {
            auto * p = static_cast<proxy_object *>(v.as_heap());
            if (p->handler.is_null()) {
                c.throw_error("TypeError",
                              "Cannot perform 'IsArray' on a proxy that has been revoked");
                return value::boolean(false);
            }
            v = p->target;
        }
        return value::boolean(v.is_array());
    });
    // --- Array.of AND Array.from, 23.1.2.3 AND 23.1.2.1 ---------------------
    //
    // BOTH HONOUR A CONSTRUCTOR `this`: `Array.of.call(C, ...)` builds through
    // C and lands every element with CreateDataPropertyOrThrow, then sets
    // `length` with Throw=true - which is what makes a subclass's `of` and
    // `from` answer instances of the subclass. `this` being Array itself, or
    // nothing callable, takes the straight line into a fresh Array.
    const auto through_constructor = [array_ctor](context & c, value ctor, double len,
                                                  bool pass_len) -> value {
        // IsConstructor (7.2.4), as near as a native can be told: a built-in
        // METHOD has no `prototype` property and a constructor does, so
        // `Array.of.call(Math.cos)` builds a plain Array (step 5).
        if (!ctor.is_callable() || ctor.as_heap() == array_ctor) { return value::undefined(); }
        if (ctor.is_kind(heap_kind::native) &&
            static_cast<native_object *>(ctor.as_heap())->find("prototype") == nullptr) {
            return value::undefined();
        }
        const value args[1] = {value::number(len)};
        return c.construct(ctor,
                           pass_len ? std::span<const value>{args} : std::span<const value>{});
    };
    // CreateDataPropertyOrThrow(A, k, v), 7.3.5 - and the refusal is a TypeError.
    const auto create_element = [](context & c, value target, double k, value v) {
        if (target.is_array() && detail::dense_array_this(target) != nullptr) {
            return detail::put_element(c, target, k, v);
        }
        context::property_descriptor wanted;
        wanted.has_value = wanted.has_writable = wanted.has_enumerable = wanted.has_configurable =
            true;
        wanted.held = v;
        wanted.writable = wanted.enumerable = wanted.configurable = true;
        if (c.define_own_property(target, number_to_string(k), wanted)) { return true; }
        c.throw_error("TypeError", "Cannot define element " + number_to_string(k));
        return false;
    };
    static_method("of", 0, [through_constructor, create_element](context & c, std::span<value> a) {
        const auto len = static_cast<double>(a.size());
        value out = through_constructor(c, c.current_this(), len, true);
        if (c.throw_pending()) { return value::undefined(); }
        if (out.is_undefined()) {
            out = c.make_array();
            static_cast<array_object *>(out.as_heap())->items.assign(a.begin(), a.end());
            return out;
        }
        const context::rooted keep(c, out);
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!create_element(c, out, static_cast<double>(i), a[i])) {
                return value::undefined();
            }
        }
        if (!detail::put_length(c, out, len)) { return value::undefined(); }
        return out;
    });
    // 23.1.2.2 Array.fromAsync - WRITTEN IN JAVASCRIPT, compiled on first
    // use. It is `for await` over the items (an async iterable, a sync
    // iterable, or an array-like), awaiting the mapper's result per element,
    // and the language now has every piece; a C++ version would re-implement
    // the async iteration protocol the compiler already lowers. Compiled
    // lazily because `call` refuses a closure before the first program runs.
    // A hidden global keeps the compiled helper between calls.
    static_method("fromAsync", 1, [](context & c, std::span<value> a) {
        value helper = c.global("__ctbrowser_from_async");
        if (!helper.is_callable()) {
            program compiled = compiler::compile(
                "return (async function (items, mapfn, thisArg) {"
                "  if (items == null) { throw new TypeError('Array.fromAsync: items is not "
                "iterable'); }"
                "  const mapping = mapfn !== undefined;"
                "  if (mapping && typeof mapfn !== 'function') {"
                "    throw new TypeError('Array.fromAsync: mapfn is not callable');"
                "  }"
                // `this` may be a constructor (steps 3.a / 5.c): its instance takes the
                // elements through CreateDataPropertyOrThrow, so a non-writable slot is
                // a TypeError rather than an endless push into nowhere.
                "  const C = this;"
                "  const custom = typeof C === 'function' && C !== Array;"
                "  const out = custom ? new C() : [];"
                "  let i = 0;"
                "  const put = function (v) {"
                "    if (custom) {"
                "      try { Object.defineProperty(out, i, {value: v, writable: true, "
                "enumerable: true, configurable: true}); }"
                "      catch (e) { throw new TypeError('Array.fromAsync: cannot define element ' "
                "+ i); }"
                "    } else { out.push(v); }"
                "    i++;"
                "  };"
                "  const iterable = typeof items[Symbol.asyncIterator] === 'function' ||"
                "                   typeof items[Symbol.iterator] === 'function';"
                "  if (iterable) {"
                "    for await (const v of items) { put(mapping ? await mapfn.call(thisArg, v, i) "
                ": v); }"
                "    if (custom) { out.length = i; }"
                "    return out;"
                "  }"
                "  const len = Math.min(Math.max(Math.trunc(Number(items.length)) || 0, 0), 2 ** "
                "53 - 1);"
                "  if (!custom && len > 4294967295) { throw new RangeError('Invalid array "
                "length'); }"
                "  while (i < len) {"
                "    const v = await items[i];"
                "    put(mapping ? await mapfn.call(thisArg, v, i) : v);"
                "  }"
                "  if (custom) { out.length = i; }"
                "  return out;"
                "});");
            if (!compiled.ok) {
                c.throw_error("SyntaxError", compiled.error);
                return value::undefined();
            }
            helper = c.run_nested(c.own_program(std::move(compiled)));
            c.define_global("__ctbrowser_from_async", helper);
        }
        return c.call(helper, a, c.current_this()); // `this` may be a constructor
    });
    static_method(
        "from", 1, [through_constructor, create_element](context & c, std::span<value> a) {
            // A mapping function that is PRESENT AND NOT CALLABLE is a TypeError
            // (23.1.2.1 step 2), checked before the source is touched. It was
            // silently ignored, so `Array.from(xs, 'nope')` copied xs and said
            // nothing.
            const value mapper = arg_at(a, 1);
            const bool mapping = !mapper.is_undefined();
            if (mapping && !mapper.is_callable()) {
                c.throw_error("TypeError", "the map function is not a function");
                return value::undefined();
            }
            const value this_arg = arg_at(a, 2);
            const value items = arg_at(a, 0);
            if (!detail::coercible_this(c, items, "from")) { return value::undefined(); }
            // Step 4, GetMethod(items, @@iterator): a throwing getter propagates
            // and a non-callable, non-nullish method is a TypeError.
            const value using_iterator = c.lookup_property(items, "@@iterator");
            if (c.throw_pending()) { return value::undefined(); }
            if (!using_iterator.is_nullish() && !using_iterator.is_callable()) {
                c.throw_error("TypeError", "Symbol.iterator is not a function");
                return value::undefined();
            }
            // The mapper's call is FENCED so an abrupt completion can close the
            // iterator (IteratorClose, step 5.e.vi.2) before it is rethrown.
            const auto map = [&](value item, double k, value & out, value iterator) {
                if (!mapping) {
                    out = item;
                    return true;
                }
                const value args[2] = {item, value::number(k)};
                bool threw = false;
                value thrown = value::undefined();
                out = c.call_fenced(mapper, args, this_arg, threw, thrown);
                if (!threw) { return true; }
                if (iterator.is_object()) {
                    const value ret = c.lookup_property(iterator, "return");
                    if (ret.is_callable()) {
                        (void)c.call(ret, std::span<const value>{}, iterator);
                    }
                }
                c.throw_value(thrown);
                return false;
            };
            if (using_iterator.is_callable()) {
                value out = through_constructor(c, c.current_this(), 0, false);
                if (c.throw_pending()) { return value::undefined(); }
                if (out.is_undefined()) { out = c.make_array(); }
                // THE STRAIGHT LINE for `Array.from(realArray)`: an Array whose
                // @@iterator is still the built-in `values` is copied rather than
                // stepped through ten thousand `next()` records, and nothing is
                // observable either way since the built-in reads nothing else.
                if (items.is_array() && !mapping && using_iterator.is_kind(heap_kind::native) &&
                    static_cast<native_object *>(using_iterator.as_heap())->name == "values" &&
                    detail::dense_array_this(out) != nullptr &&
                    detail::dense_array_this(items) != nullptr) {
                    static_cast<array_object *>(out.as_heap())->items =
                        static_cast<array_object *>(items.as_heap())->items;
                    return out;
                }
                const context::rooted keep(c, out);
                const value iterator = c.get_iterator(items);
                if (c.throw_pending() || !iterator.is_object()) { return value::undefined(); }
                const context::rooted keep_iterator(c, iterator);
                const value next = c.lookup_property(iterator, "next");
                if (c.throw_pending()) { return value::undefined(); }
                double k = 0;
                for (;;) {
                    bool done = false;
                    const value item = c.iterator_step(iterator, next, done);
                    if (c.throw_pending()) { return value::undefined(); }
                    if (done) { break; }
                    const context::rooted keep_item(c, item);
                    value mapped = value::undefined();
                    if (!map(item, k, mapped, iterator)) { return value::undefined(); }
                    const context::rooted keep_mapped(c, mapped);
                    if (!create_element(c, out, k, mapped)) {
                        const value ret = c.lookup_property(iterator, "return");
                        if (ret.is_callable()) {
                            (void)c.call(ret, std::span<const value>{}, iterator);
                        }
                        return value::undefined();
                    }
                    k += 1.0;
                }
                if (!detail::put_length(c, out, k)) { return value::undefined(); }
                return out;
            }
            // Steps 7-12, the array-like path: ToObject, LengthOfArrayLike, then
            // every index through [[Get]].
            const value array_like = detail::box_primitive(c, items);
            const double len = detail::array_like_length(c, array_like);
            if (c.throw_pending()) { return value::undefined(); }
            value out = through_constructor(c, c.current_this(), len, true);
            if (c.throw_pending()) { return value::undefined(); }
            if (out.is_undefined()) {
                out = c.make_array();
                if (detail::new_array_of_length(c, out, len) == nullptr) {
                    return value::undefined();
                }
            }
            const context::rooted keep(c, out);
            for (double k = 0; k < len; k += 1.0) {
                const value item = detail::element_at(c, array_like, k);
                if (c.throw_pending()) { return value::undefined(); }
                const context::rooted keep_item(c, item);
                value mapped = value::undefined();
                if (!map(item, k, mapped, value::undefined())) { return value::undefined(); }
                const context::rooted keep_mapped(c, mapped);
                if (!create_element(c, out, k, mapped)) { return value::undefined(); }
            }
            if (!detail::put_length(c, out, len)) { return value::undefined(); }
            return out;
        });
    cx.define_global("Array", value::object(array_ctor));

    object_object * array_proto = new_table(cx);
    // The prototype methods p5.js uses that were not here. `at` and `fill` are
    // the ones it leans on hardest - 43 and 31 uses - because a typed-array
    // shim reaches for both.
    method(cx, array_proto, "at", 1, [](context & c, std::span<value> a) {
        const value self = detail::array_this(c);
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
        const value self = detail::array_this(c);
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
        for (; k < end; k += 1.0) {
            if (!detail::put_element(c, self, k, filler)) { return self; }
        }
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
        const value self = detail::array_this(c);
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
        const value self = detail::array_this(c);
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
        const value self = detail::array_this(c);
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
        const value self = detail::array_this(c);
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
        const value self = detail::array_this(c);
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
            if (!detail::put_element(c, self, len, item)) { return value::number(0); }
            len += 1.0;
        }
        if (!detail::put_length(c, self, len)) { return value::number(0); }
        return value::number(len);
    });
    // 23.1.3.22. An EMPTY receiver still writes `length` back - that is step
    // 3a, and it is what turns `{length: NaN}` into `{length: 0}`.
    method(cx, array_proto, "pop", 0, [](context & c, std::span<value>) {
        const value self = detail::array_this(c);
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
            if (!detail::put_length(c, self, 0)) { return value::undefined(); }
            return value::undefined();
        }
        const value out = detail::element_at(c, self, len - 1);
        // ROOTED ACROSS THE DELETE AND THE LENGTH WRITE. Both can run user
        // code - a Proxy trap, a `length` setter - and the value being returned
        // is by then held only by this C++ local.
        const context::rooted keep(c, out);
        if (!detail::delete_element(c, self, len - 1)) { return value::undefined(); }
        if (!detail::put_length(c, self, len - 1)) { return value::undefined(); }
        return out;
    });
    // 23.1.3.25. Every element moves DOWN one, a hole moving down deletes what
    // it lands on rather than filling it with undefined, and the vacated slot
    // at the top is deleted before `length` is written.
    method(cx, array_proto, "shift", 0, [](context & c, std::span<value>) {
        const value self = detail::array_this(c);
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
            if (!detail::put_length(c, self, 0)) { return value::undefined(); }
            return value::undefined();
        }
        if (!detail::generic_walk_ok(c, len)) { return value::undefined(); }
        const value out = detail::element_at(c, self, 0);
        const context::rooted keep(c, out);
        for (double k = 1; k < len; k += 1.0) {
            if (detail::has_element(c, self, k)) {
                if (!detail::put_element(c, self, k - 1, detail::element_at(c, self, k))) {
                    return value::undefined();
                }
            } else {
                if (!detail::delete_element(c, self, k - 1)) { return value::undefined(); }
            }
        }
        if (!detail::delete_element(c, self, len - 1)) { return value::undefined(); }
        if (!detail::put_length(c, self, len - 1)) { return value::undefined(); }
        return out;
    });
    // 23.1.3.32. The tail moves UP, walked from the top down so that an
    // overlapping move never overwrites a source before it is read.
    method(cx, array_proto, "unshift", 1, [](context & c, std::span<value> a) {
        const value self = detail::array_this(c);
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
                    if (!detail::put_element(c, self, to, detail::element_at(c, self, from))) {
                        return value::number(0);
                    }
                } else {
                    if (!detail::delete_element(c, self, to)) { return value::number(0); }
                }
            }
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (!detail::put_element(c, self, static_cast<double>(i), a[i])) {
                    return value::number(0);
                }
            }
        }
        if (!detail::put_length(c, self, len + count)) { return value::number(0); }
        return value::number(len + count);
    });
    method(cx, array_proto, "slice", 2, [](context & c, std::span<value> a) {
        const value self = detail::array_this(c);
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
        const value self = detail::array_this(c);
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
                    if (!detail::put_element(c, self, k + inserted,
                                             detail::element_at(c, self, k + skipped))) {
                        return removed;
                    }
                } else {
                    if (!detail::delete_element(c, self, k + inserted)) { return removed; }
                }
            }
            for (double k = len; k > len - skipped + inserted; k -= 1.0) {
                if (!detail::delete_element(c, self, k - 1)) { return removed; }
            }
        } else if (inserted > skipped) {
            for (double k = len - skipped; k > start; k -= 1.0) {
                if (detail::has_element(c, self, k + skipped - 1)) {
                    if (!detail::put_element(c, self, k + inserted - 1,
                                             detail::element_at(c, self, k + skipped - 1))) {
                        return removed;
                    }
                } else {
                    if (!detail::delete_element(c, self, k + inserted - 1)) { return removed; }
                }
            }
        }
        for (std::size_t i = 2; i < a.size(); ++i) {
            if (!detail::put_element(c, self, start + static_cast<double>(i - 2), a[i])) {
                return removed;
            }
        }
        if (!detail::put_length(c, self, len - skipped + inserted)) { return removed; }
        return removed;
    });
    // `fromIndex`, WHICH BOTH SEARCHES ACCEPTED AND NEITHER READ. `xs.indexOf(v,
    // 5)` searched from 0, so a scan-from-here loop - the standard way to find
    // every occurrence - found the first one forever.
    method(cx, array_proto, "indexOf", 1, [](context & c, std::span<value> a) {
        const value self = detail::array_this(c);
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
        const value self = detail::array_this(c);
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
        const value self = detail::array_this(c);
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
        const value self = detail::array_this(c);
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
        const value self = detail::array_this(c);
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
        const value self = detail::array_this(c);
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
        const value self = detail::array_this(c);
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
                if (!detail::put_element(c, self, lower, upper_value)) { return self; }
                if (!detail::put_element(c, self, upper, lower_value)) { return self; }
            } else if (upper_there) {
                if (!detail::put_element(c, self, lower, upper_value)) { return self; }
                if (!detail::delete_element(c, self, upper)) { return self; }
            } else if (lower_there) {
                if (!detail::delete_element(c, self, lower)) { return self; }
                if (!detail::put_element(c, self, upper, lower_value)) { return self; }
            }
        }
        return self;
    });

    // The rest of the prototype, in the order it was always installed - one
    // file each; the two halves share only what is passed here.
    install_array_iteration(cx, array_ctor, array_proto);

    link_constructor(cx, array_proto, "Array", 1, value::object(array_ctor));
    cx.set_prototype(context::proto_kind::array, array_proto);
}

} // namespace ctbrowser::script::builtins_detail
