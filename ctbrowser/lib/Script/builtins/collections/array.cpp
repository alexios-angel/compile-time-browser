//===- array.cpp - Array constructor and methods ----------------*- C++ -*-===//
//
// Array builtins: the constructor, its statics, and the prototype methods that
// take no callback. array_iteration.cpp (install_array_iteration) installs the
// callback-taking methods. Shared helpers and declarations are in
// ../internal.hpp. Section numbers are ECMA-262 (ES2024 numbering).
//
//===----------------------------------------------------------------------===//

#include "../internal.hpp"

namespace ctbrowser::script::builtins_detail {

namespace {

/// FlattenIntoArray, 23.1.3.13.1, for `flat` and `flatMap`, in the
/// specification's order, which a proxy source shows trap by trap: for each
/// index ascending, HasProperty, Get, the mapper (flatMap's, on the top level
/// only), then IsArray of the element while depth remains, and either a
/// descent into it (its own LengthOfArrayLike, its own indices) or
/// CreateDataPropertyOrThrow onto the target at the next index.
///
/// Uses an explicit stack of levels rather than the specification's recursion:
/// `flat(Infinity)` on a deep structure must not be bounded by the C++ stack,
/// and one that contains itself gets the RangeError a recursion would reach
/// (at 10000 levels). The sources are held in a rooted array because a getter
/// can trigger a collection. Returns false when a throw is pending.
[[nodiscard]] bool flatten_into_array(context & c, value target, value source, double len,
                                      double depth, value mapper, value this_arg) {
    struct level {
        double len;
        double index;
        double depth;
    };
    detail::generic_walk walk{c};
    const value sources_value = c.make_array();
    const context::rooted keep_sources(c, sources_value);
    auto & sources = static_cast<array_object *>(sources_value.as_heap())->items;
    sources.push_back(source);
    std::vector<level> levels{level{len, 0, depth}};
    double target_index = 0;
    while (!levels.empty()) {
        if (levels.back().index >= levels.back().len) {
            levels.pop_back();
            sources.pop_back();
            continue;
        }
        if (!walk.step()) { return false; }
        const double k = levels.back().index;
        levels.back().index += 1.0;
        const double left = levels.back().depth;
        const value from = sources.back();
        const bool exists = detail::has_element(c, from, k);
        if (walk.threw()) { return false; }
        if (!exists) { continue; }
        value element = detail::element_at(c, from, k);
        if (walk.threw()) { return false; }
        if (levels.size() == 1 && mapper.is_callable()) {
            const context::rooted keep_element(c, element);
            const value args[3] = {element, value::number(k), from};
            element = c.call(mapper, args, this_arg);
            // Pending if it threw (context::call). The unwind count is not used
            // here because it also moves for a mapper that throws and catches
            // inside itself.
            if (c.throw_pending()) { return false; }
            walk.rearm();
        }
        const context::rooted keep_element(c, element);
        bool flatten = false;
        if (left > 0 && !detail::is_array_value(c, element, flatten)) { return false; }
        if (flatten) {
            if (levels.size() >= 10000) {
                c.throw_error("RangeError", "Maximum call stack size exceeded");
                return false;
            }
            const double inner = detail::array_like_length(c, element);
            if (walk.threw()) { return false; }
            sources.push_back(element);
            levels.push_back(level{inner, 0, left - 1});
            continue;
        }
        if (target_index >= max_safe_integer) {
            c.throw_error("TypeError", "flat: the result would be longer than 2^53 - 1");
            return false;
        }
        if (!detail::create_element(c, target, target_index, element)) { return false; }
        target_index += 1.0;
    }
    return true;
}

} // namespace

/// Installs the Array constructor, its static methods and Array.prototype
/// (the rest of the prototype comes from install_array_iteration).
void install_array(context & cx) {
    using detail::method;
    using detail::new_table;

    // `Array` itself.
    auto * array_ctor = cx.allocate<native_object>("Array", [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * made = static_cast<array_object *>(out.as_heap());
        // `Array(n)` is a length, `Array(a, b, ...)` is the elements.
        if (a.size() == 1 && a[0].is_number()) {
            // set_js_length_with_holes, not `items.assign`: `new Array(4294967295)`
            // is legal and must not allocate 4G values, and `new Array(1.5)` is
            // a RangeError. Every index is a hole (23.1.1.1 step 5), which
            // forEach skips and a read takes from Array.prototype.
            if (!made->set_js_length_with_holes(a[0].as_number())) {
                c.throw_error("RangeError", "Invalid array length");
            }
        } else {
            made->items.assign(a.begin(), a.end());
        }
        detail::adopt_subclass_prototype(c, out, c.prototype(context::proto_kind::array));
        return out;
    });
    const auto static_method = [&](const char * name, double arity, native_fn fn) {
        method(cx, array_ctor, name, arity, std::move(fn));
    };
    // 23.1.2.5 get Array[@@species]: `this`, so a subclass's methods build
    // instances of the subclass through ArraySpeciesCreate.
    {
        auto * species =
            detail::method_native(cx, "get [Symbol.species]",
                                  [](context & c, std::span<value>) { return c.current_this(); });
        detail::install_arity(cx, species, 0);
        array_ctor->define_accessor(CTBROWSER_SYMBOL_KEY("species"), value::object(species),
                                    value::undefined(), attr_configurable);
    }
    static_method("isArray", 1, [](context & c, std::span<value> a) {
        bool answer = false;
        (void)detail::is_array_value(c, arg_at(a, 0), answer);
        return value::boolean(answer);
    });
    // --- Array.of and Array.from, 23.1.2.3 and 23.1.2.1 ---------------------
    //
    // Both honour a constructor `this`: `Array.of.call(C, ...)` builds through
    // C and lands every element with CreateDataPropertyOrThrow, then sets
    // `length` with Throw=true, which makes a subclass's `of` and `from` answer
    // instances of the subclass. When `this` is Array itself, or not a
    // constructor, through_constructor returns undefined and the caller builds
    // a fresh Array directly.
    const auto through_constructor = [array_ctor](context & c, value ctor, double len,
                                                  bool pass_len) -> value {
        // IsConstructor (7.2.4), as near as a native can be told: a built-in
        // method has no `prototype` property and a constructor does, so
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
    // CreateDataPropertyOrThrow(A, k, v), 7.3.5 - detail::create_element.
    const auto create_element = detail::create_element;
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
    // 23.1.2.2 Array.fromAsync, written in JavaScript and compiled on first
    // use. It is `for await` over the items (an async iterable, a sync
    // iterable, or an array-like), awaiting the mapper's result per element; a
    // C++ version would re-implement the async iteration protocol the compiler
    // already lowers. Compiled lazily because `call` refuses a closure before
    // the first program runs. A hidden global keeps the compiled helper between
    // calls.
    static_method("fromAsync", 1, [](context & c, std::span<value> a) {
        value helper = c.global("__ctbrowser_from_async");
        if (!helper.is_callable()) {
            program compiled = compiler::compile(
                // The two symbols are passed in by the native (the intrinsic ones):
                // a page may have replaced the global `Symbol` by the first call.
                "return (async function (items, mapfn, thisArg, asyncKey, syncKey) {"
                // Strict, so `out.length = i` on a read-only length is the TypeError
                // Set(O, 'length', len, true) throws (step 3.j.ix / 5.g.iv).
                "  'use strict';"
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
                "  let out;"
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
                // GetMethod (7.3.11): a present @@asyncIterator / @@iterator that is
                // not callable is a TypeError, not an array-like.
                "  const usingAsync = items[asyncKey];"
                "  if (usingAsync != null && typeof usingAsync !== 'function') {"
                "    throw new TypeError('Array.fromAsync: @@asyncIterator is not callable');"
                "  }"
                "  const usingSync = usingAsync != null ? undefined : items[syncKey];"
                "  if (usingSync != null && typeof usingSync !== 'function') {"
                "    throw new TypeError('Array.fromAsync: @@iterator is not callable');"
                "  }"
                "  const iterable = usingAsync != null || usingSync != null;"
                // Construct(C) with no arguments for an iterable (step 3.e); and
                // the method already read is the one iterated, because `for await`
                // over `items` itself would read both symbols a second time.
                "  if (iterable) {"
                "    out = custom ? new C() : [];"
                "    const source = usingAsync != null"
                "      ? {[asyncKey]() { return usingAsync.call(items); }}"
                "      : {[syncKey]() { return usingSync.call(items); }};"
                "    for await (const v of source) { put(mapping ? await mapfn.call(thisArg, v, i) "
                ": v); }"
                "    if (custom) { out.length = i; }"
                "    return out;"
                "  }"
                // LengthOfArrayLike: ToNumber, not Number(); `+1n` is the TypeError
                // ToLength gives a BigInt, where Number(1n) would be 1.
                "  const len = Math.min(Math.max(Math.trunc(+items.length) || 0, 0), 2 ** "
                "53 - 1);"
                "  if (!custom && len > 4294967295) { throw new RangeError('Invalid array "
                "length'); }"
                // ...and Construct(C, [len]) for an array-like (step 5.c).
                "  out = custom ? new C(len) : [];"
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
        // Each is rooted before the next allocates: a collection between the two
        // would free the first.
        const value async_key = c.key_value(CTBROWSER_SYMBOL_KEY("asyncIterator"));
        const context::rooted keep_async{c, async_key};
        const value sync_key = c.key_value(CTBROWSER_SYMBOL_KEY("iterator"));
        const context::rooted keep_sync{c, sync_key};
        const value args[5] = {arg_at(a, 0), arg_at(a, 1), arg_at(a, 2), async_key, sync_key};
        return c.call(helper, args, c.current_this()); // `this` may be a constructor
    });
    static_method(
        "from", 1, [through_constructor, create_element](context & c, std::span<value> a) {
            // A mapping function that is present but not callable is a TypeError
            // (23.1.2.1 step 2), checked before the source is touched.
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
            const value using_iterator = c.lookup_property(items, CTBROWSER_SYMBOL_KEY("iterator"));
            if (c.throw_pending()) { return value::undefined(); }
            if (!using_iterator.is_nullish() && !using_iterator.is_callable()) {
                c.throw_error("TypeError", "Symbol.iterator is not a function");
                return value::undefined();
            }
            // The mapper's call is fenced so an abrupt completion can close the
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
                // Fast path for `Array.from(realArray)`: an Array whose @@iterator
                // is still the built-in `values` is copied rather than stepped
                // through one `next()` record per element. Nothing observable
                // differs, since the built-in iterator reads nothing else.
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
            const value array_like = c.box_primitive(items);
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
    // 23.1.3: Array.prototype is an Array, so it has a `length` of 0,
    // {writable, not enumerable, not configurable} as every Array's is. Only
    // the property is emulated: the table is not an array_object, so an element
    // written to it does not move `length`.
    array_proto->define("length", value::number(0), attr_writable);
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
        // An absent `end` and an explicit `undefined` both mean "to the end",
        // so has_index is tested rather than the argument count.
        const double raw_end = has_index(a, 2) ? integer_arg(c, a, 2) : len;
        const double end = raw_end < 0 ? std::max(len + raw_end, 0.0) : std::min(raw_end, len);
        for (; k < end; k += 1.0) {
            if (!detail::put_element(c, self, k, filler)) { return self; }
        }
        return self;
    });
    // 23.1.3.13 Array.prototype.flat, generic. An absent or undefined depth is
    // 1; anything else goes through ToIntegerOrInfinity, so 'x', {} and
    // -Infinity are depth 0. The length is read before the depth is coerced
    // (steps 2 and 4), and the result is ArraySpeciesCreate(O, 0) (step 5).
    method(cx, array_proto, "flat", 0, [](context & c, std::span<value> a) {
        const value self = detail::array_this(c);
        value out = c.make_array();
        if (!detail::coercible_this(c, self, "flat")) { return out; }
        const detail::unwind_watch watch{c};
        const double len = detail::array_like_length(c, self);
        if (watch.threw()) { return out; }
        double depth = has_index(a, 0) ? integer_arg(c, a, 0) : 1.0;
        if (watch.threw()) { return out; }
        if (depth < 0) { depth = 0; }
        out = detail::array_species_create(c, self, 0);
        if (out.is_undefined()) { return out; }
        const context::rooted keep(c, out);
        (void)flatten_into_array(c, out, self, len, depth, value::undefined(), value::undefined());
        return out;
    });
    // 23.1.3.14 Array.prototype.flatMap, generic: depth is always 1, the second
    // argument is the callback's `this`, and a missing or non-callable callback
    // is a TypeError.
    method(cx, array_proto, "flatMap", 1, [](context & c, std::span<value> a) {
        const value self = detail::array_this(c);
        value out = c.make_array();
        if (!detail::coercible_this(c, self, "flatMap")) { return out; }
        // LengthOfArrayLike before the callback is examined (steps 2-4): a
        // `length` getter runs, and its throw wins, even for a callback that
        // is not callable.
        const detail::unwind_watch watch{c};
        const double len = detail::array_like_length(c, self);
        if (watch.threw()) { return out; }
        const value callback = arg_at(a, 0);
        if (!detail::callable_arg(c, callback, "callback")) { return out; }
        out = detail::array_species_create(c, self, 0); // step 5
        if (out.is_undefined()) { return out; }
        const context::rooted keep(c, out); // the callback can trigger a collection
        (void)flatten_into_array(c, out, self, len, 1.0, callback, arg_at(a, 1));
        return out;
    });
    method(cx, array_proto, "findLast", 1, [](context & c, std::span<value> a) {
        return array_find(c, a, "findLast", true, false);
    });
    method(cx, array_proto, "findLastIndex", 1, [](context & c, std::span<value> a) {
        return array_find(c, a, "findLastIndex", true, true);
    });
    // --- push, pop, shift and unshift: the four that mutate at an end -------
    //
    // These are as generic as the read-only methods (see
    // detail::array_like_length): `this` is ToObject'd, `length` is read
    // through [[Get]] and ToLength, every element moves through [[Set]] or
    // [[Delete]], and the algorithm ends with Set(O, "length", n, true), which
    // is what makes `Array.prototype.push.call(obj, x)` useful.
    //
    // Each opens with a branch, not a second algorithm: a real, ordinary Array
    // is its own std::vector and does the vector operation; anything else (an
    // array-like, a Proxy, `arguments` from another realm) takes the specified
    // walk. detail::dense_array_this is that branch.
    //
    // Growing past 2^53 - 1 is a TypeError, not a clamp (23.1.3.23 step 5,
    // 23.1.3.37 step 4a). `length` itself clamps there through ToLength, so
    // `push()` with no arguments on `{length: Infinity}` writes back 2^53 - 1
    // and succeeds while `push(null)` on the same object throws.
    method(cx, array_proto, "push", 1, [](context & c, std::span<value> a) {
        const value self = detail::array_this(c);
        if (!detail::coercible_this(c, self, "push")) { return value::number(0); }
        // The vector fast path is skipped when the prototype chain holds an
        // index: each Set of a new index is then the prototype's to answer (a
        // setter on Array.prototype[0]), which the generic walk asks and vector
        // surgery would skip.
        if (array_object * dense = detail::dense_array_this(self);
            dense != nullptr && !array_chain_holds_indices(c, *dense)) {
            // A frozen or non-extensible array throws here rather than
            // silently dropping the write: the Sets in 23.1.3.23 carry
            // Throw=true, so this is a TypeError in sloppy mode too, unlike a
            // bare `a[0] = 1`, which context::store_index discards (see the
            // TODO(strict) there).
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
    // 23.1.3.22 Array.prototype.pop. An empty receiver still writes `length`
    // back (step 3a), which turns `{length: NaN}` into `{length: 0}`.
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
        // Rooted across the delete and the length write: both can run user
        // code (a Proxy trap, a `length` setter), and the value being returned
        // is by then held only by this C++ local.
        const context::rooted keep(c, out);
        if (!detail::delete_element(c, self, len - 1)) { return value::undefined(); }
        if (!detail::put_length(c, self, len - 1)) { return value::undefined(); }
        return out;
    });
    // 23.1.3.27 Array.prototype.shift. Every element moves down one, a hole
    // moving down deletes what it lands on rather than filling it with
    // undefined, and the vacated top slot is deleted before `length` is
    // written.
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
    // 23.1.3.37 Array.prototype.unshift. The tail moves up, walked from the top
    // down so an overlapping move never overwrites a source before it is read.
    method(cx, array_proto, "unshift", 1, [](context & c, std::span<value> a) {
        const value self = detail::array_this(c);
        if (!detail::coercible_this(c, self, "unshift")) { return value::number(0); }
        if (array_object * dense = detail::dense_array_this(self);
            dense != nullptr && !array_chain_holds_indices(c, *dense)) {
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
            detail::generic_walk walk{c};
            for (double k = len; k > 0; k -= 1.0) {
                if (!walk.step()) { return value::number(0); }
                const double from = k - 1;
                const double to = k + count - 1;
                const bool present = detail::has_element(c, self, from);
                if (walk.threw()) { return value::number(0); }
                if (present) {
                    const value moved = detail::element_at(c, self, from);
                    if (walk.threw()) { return value::number(0); }
                    if (!detail::put_element(c, self, to, moved)) { return value::number(0); }
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
        const detail::unwind_watch watch{c};
        const double len = detail::array_like_length(c, self);
        const double raw_from = integer_arg(c, a, 0);
        double k = raw_from < 0 ? std::max(len + raw_from, 0.0) : std::min(raw_from, len);
        const double raw_to = has_index(a, 1) ? integer_arg(c, a, 1) : len;
        const double to = raw_to < 0 ? std::max(len + raw_to, 0.0) : std::min(raw_to, len);
        if (watch.threw()) { return out; }
        // ArraySpeciesCreate(O, count), step 9.
        const double count = std::max(to - k, 0.0);
        out = detail::array_species_create(c, self, count);
        if (out.is_undefined()) { return out; }
        const context::rooted keep(c, out);
        // The count is the span, not the number of elements found: a hole in
        // the middle leaves an undefined behind rather than shortening the
        // result, which is what `A.length = n` at the end of 23.1.3.28 says.
        if (array_object * result = detail::dense_array_this(out)) {
            result->items.clear();
            for (; k < to; k += 1.0) { result->items.push_back(detail::element_at(c, self, k)); }
            return out;
        }
        for (double n = 0; k < to; k += 1.0, n += 1.0) {
            if (!detail::has_element(c, self, k)) { continue; }
            if (!detail::create_element(c, out, n, detail::element_at(c, self, k))) { return out; }
        }
        (void)detail::put_length(c, out, count);
        return out;
    });
    // 23.1.3.31 Array.prototype.splice, in the specification's order: the
    // deleted range is copied out, the tail is shifted (ascending when it moves
    // left, descending when it moves right, so an overlap never overwrites an
    // unread source), the inserted items are written, and `length` is set last.
    //
    // The delete count has three cases: `splice()` with no arguments deletes
    // nothing (step 6), `splice(i)` deletes to the end (step 7), and otherwise
    // the second argument is clamped.
    method(cx, array_proto, "splice", 2, [](context & c, std::span<value> a) {
        const value self = detail::array_this(c);
        value removed = c.make_array();
        if (!detail::coercible_this(c, self, "splice")) { return removed; }
        const detail::unwind_watch watch{c};
        array_object * dense = detail::dense_array_this(self);
        const double len = dense != nullptr ? static_cast<double>(dense->items.size())
                                            : detail::array_like_length(c, self);
        // ToIntegerOrInfinity (which runs valueOf), before anything moves.
        // Infinity is a valid count ("to the end") but casting it to an integer
        // is undefined behaviour, so every bound is clamped as a double and cast
        // afterwards.
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
        if (watch.threw()) { return removed; }
        if (len + inserted - skipped > max_safe_integer) {
            c.throw_error("TypeError", "Invalid array length");
            return removed;
        }
        // ArraySpeciesCreate(O, actualDeleteCount), step 10.
        removed = detail::array_species_create(c, self, skipped);
        if (removed.is_undefined()) { return removed; }
        const context::rooted keep(c, removed);
        array_object * out = detail::dense_array_this(removed);
        if (out == nullptr) {
            // A species object takes the deleted range through
            // CreateDataPropertyOrThrow and `length` (steps 11-12); the
            // receiver is then spliced generically below.
            for (double k = 0; k < skipped; k += 1.0) {
                if (!detail::has_element(c, self, start + k)) { continue; }
                if (!detail::create_element(c, removed, k,
                                            detail::element_at(c, self, start + k))) {
                    return removed;
                }
            }
            if (!detail::put_length(c, removed, skipped)) { return removed; }
            dense = nullptr;
        }
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
        // The walk limit is checked against the work done, not `length`. Step
        // 15's shift runs from actualStart to len - actualDeleteCount, so
        // splicing one element out of `{length: 4294967296}` at the very end is
        // two operations, not four billion, and a splice that inserts as many as
        // it deletes shifts nothing at all, whatever the length.
        if (!detail::generic_walk_ok(c, skipped) ||
            (inserted != skipped && !detail::generic_walk_ok(c, len - start - skipped))) {
            return removed;
        }
        if (out != nullptr) { out->items.clear(); } // ArrayCreate sized it; the copy fills it
        for (double k = 0; out != nullptr && k < skipped; k += 1.0) {
            // A hole is kept in length only. CreateDataProperty is skipped for
            // an absent index and `A.length` is set to the count anyway (step
            // 14); the span is preserved here with an undefined, as `slice`
            // does.
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
    // indexOf and lastIndexOf honour `fromIndex` (coerced with
    // ToIntegerOrInfinity; a negative value counts back from the end).
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
                return value::number(k + 0.0); // an index is never -0
            }
        }
        return value::number(-1);
    });
    method(cx, array_proto, "lastIndexOf", 1, [](context & c, std::span<value> a) {
        const value self = detail::array_this(c);
        if (!detail::coercible_this(c, self, "lastIndexOf")) { return value::number(-1); }
        const double len = detail::array_like_length(c, self);
        if (len == 0) { return value::number(-1); }
        // The default is the last index, not the first, and a negative
        // fromIndex counts back from the end without clamping up to 0 - it
        // clamps the search away entirely.
        const double n = a.size() > 1 ? integer_arg(c, a, 1) : len - 1;
        if (std::isinf(n) && n < 0) { return value::number(-1); }
        double k = n >= 0 ? std::min(n, len - 1) : len + n;
        const value target = arg_at(a, 0);
        for (; k >= 0; k -= 1.0) {
            if (detail::has_element(c, self, k) &&
                detail::element_at(c, self, k).strict_equals(target)) {
                return value::number(k + 0.0); // an index is never -0
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
            // one search that finds a NaN, which is why it exists beside
            // `indexOf`. It also does not skip a hole but reads one as
            // undefined, so `[, 1].includes(undefined)` is true.
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
        // An absent separator and an explicit `undefined` both mean ",".
        const std::string sep = has_index(a, 0) ? c.to_string(a[0]) : std::string{","};
        std::string out;
        for (double k = 0; k < len; k += 1.0) {
            if (k > 0) { out += sep; }
            // null and undefined join as empty, not as "null"/"undefined".
            const value item = detail::element_at(c, self, k);
            if (!item.is_nullish()) { out += c.to_string(item); }
        }
        return c.string(out);
    });
    // 23.1.3.32 Array.prototype.toLocaleString. Every element is asked for its
    // own `toLocaleString`, which is the only difference from `join(",")` and is
    // what makes a Date or a Number in an array format itself.
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
    // 23.1.3.2 Array.prototype.concat: ArraySpeciesCreate(O, 0), then the
    // receiver and each argument is spread when IsConcatSpreadable says so (its
    // own @@isConcatSpreadable if defined, else IsArray, which sees through a
    // Proxy and refuses a revoked one), every element through HasProperty and
    // [[Get]], a hole staying a hole, and `length` set last.
    method(cx, array_proto, "concat", 1, [](context & c, std::span<value> a) {
        const value self = detail::array_this(c);
        value out = c.make_array();
        if (!detail::coercible_this(c, self, "concat")) { return out; }
        out = detail::array_species_create(c, self, 0);
        if (out.is_undefined()) { return out; }
        const context::rooted keep(c, out);
        array_object * result = detail::dense_array_this(out);
        double n = 0;
        // Appends one element, or one spread. False means a throw is pending
        // and the caller must stop.
        const auto append = [&](value item) {
            bool spreadable = false;
            if (item.is_object_like()) {
                // [[Get]] on a revoked proxy is a TypeError (10.5.8 step 2),
                // raised here once rather than by the lookup and again by
                // IsArray.
                if (item.is_kind(heap_kind::proxy) &&
                    static_cast<proxy_object *>(item.as_heap())->handler.is_null()) {
                    c.throw_error("TypeError",
                                  "Cannot perform 'get' on a proxy that has been revoked");
                    return false;
                }
                const value flag =
                    c.lookup_property(item, CTBROWSER_SYMBOL_KEY("isConcatSpreadable"));
                if (c.throw_pending()) { return false; }
                if (!flag.is_undefined()) {
                    spreadable = context::truthy(flag);
                } else if (!detail::is_array_value(c, item, spreadable)) {
                    return false;
                }
            }
            if (!spreadable) {
                if (n >= max_safe_integer) {
                    c.throw_error("TypeError", "Array.prototype.concat: length exceeds 2^53-1");
                    return false;
                }
                if (!detail::create_element(c, out, n, item)) { return false; }
                n += 1.0;
                return true;
            }
            if (array_object * dense = detail::dense_array_this(item);
                dense != nullptr && result != nullptr &&
                n == static_cast<double>(result->items.size())) {
                result->items.insert(result->items.end(), dense->items.begin(), dense->items.end());
                n += static_cast<double>(dense->items.size());
                return true;
            }
            const double len = detail::array_like_length(c, item);
            if (c.throw_pending()) { return false; }
            if (n + len > max_safe_integer) {
                c.throw_error("TypeError", "Array.prototype.concat: length exceeds 2^53-1");
                return false;
            }
            detail::generic_walk walk{c};
            for (double k = 0; k < len; k += 1.0, n += 1.0) {
                if (!walk.step()) { return false; }
                const bool present = detail::has_element(c, item, k);
                if (walk.threw()) { return false; }
                if (!present) {
                    if (result == nullptr) { continue; } // a species object: no property
                    // A hole stays a hole: the slot exists in `items` as a
                    // placeholder and element_attrs says it is not a property.
                    const auto at = static_cast<std::size_t>(n);
                    if (result->items.size() <= at) {
                        result->items.resize(at + 1, value::undefined());
                    }
                    result->set_element_attrs(static_cast<std::uint32_t>(at),
                                              array_object::elem_hole);
                    continue;
                }
                const value element = detail::element_at(c, item, k);
                if (walk.threw()) { return false; }
                const context::rooted keep_element(c, element);
                if (!detail::create_element(c, out, n, element)) { return false; }
            }
            return true;
        };
        if (!append(self)) { return out; }
        for (const value & item : a) {
            if (!append(item)) { return out; }
        }
        if (!detail::put_length(c, out, n)) { return out; }
        return out;
    });
    // 23.1.3.26 Array.prototype.reverse, in place and generic. The swap is
    // HasProperty-then-Get on both ends: a hole opposite an element deletes the
    // far side rather than filling it with undefined, which is the only thing
    // that distinguishes reverse from "read it all and write it back".
    method(cx, array_proto, "reverse", 0, [](context & c, std::span<value>) {
        const value self = detail::array_this(c);
        if (!detail::coercible_this(c, self, "reverse")) { return self; }
        if (array_object * dense = detail::dense_array_this(self)) {
            std::ranges::reverse(dense->items);
            return self;
        }
        detail::generic_walk walk{c};
        const double len = detail::array_like_length(c, self);
        if (walk.threw()) { return self; }
        // A String of one character or none has nothing to swap.
        if (len > 1 && !detail::mutable_receiver(c, self, "reverse")) { return self; }
        const double middle = std::floor(len / 2);
        for (double lower = 0; lower < middle; lower += 1.0) {
            if (!walk.step()) { return self; }
            const double upper = len - lower - 1;
            const bool lower_there = detail::has_element(c, self, lower);
            if (walk.threw()) { return self; }
            const value lower_value =
                lower_there ? detail::element_at(c, self, lower) : value::undefined();
            if (walk.threw()) { return self; }
            // Rooted across the second read: a getter on the far end can
            // allocate and trigger a collection, and what the near end just
            // answered is by then held only by this C++ local.
            const context::rooted keep(c, lower_value);
            const bool upper_there = detail::has_element(c, self, upper);
            if (walk.threw()) { return self; }
            const value upper_value =
                upper_there ? detail::element_at(c, self, upper) : value::undefined();
            if (walk.threw()) { return self; }
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

    // The callback-taking methods and iterators (array_iteration.cpp); the two
    // files share only what is passed here.
    install_array_iteration(cx, array_ctor, array_proto);

    link_constructor(cx, array_proto, "Array", 1, value::object(array_ctor));
    cx.set_prototype(context::proto_kind::array, array_proto);
}

} // namespace ctbrowser::script::builtins_detail
