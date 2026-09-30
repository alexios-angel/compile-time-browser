// ctbrowser.script builtins - Array, continued: the callback-taking methods,
// sort, the iterators, and the five ES2023 additions. Also list_iterator, the
// real Iterator over an existing list that Map and Set share.
//
// One of four files carved out of a 1,824-line builtins/collections.cpp on
// 2026-09-08 - which was itself one of five carved out of builtins.cpp on
// 2026-08-09. Everything shared - the argument helpers, namespace detail, and
// these functions' declarations - is in ../internal.hpp.

#include "typed_arrays/internal.hpp"

namespace ctbrowser::script::detail {

namespace {

// ONE PROTOTYPE PER KIND - %ArrayIteratorPrototype% and its siblings
// (23.1.5.2, 22.1.5.1, 24.1.5.1, 24.2.5.1): `next` and the tag live
// there, its [[Prototype]] is %Iterator.prototype% (so `@@iterator`, the
// helpers and `Object.getPrototypeOf(Object.getPrototypeOf([].values()))`
// are what the standard says), and two iterators of a kind share it.
// Made on first use, because Iterator installs after the collections do,
// and kept under a PRIVATE key on Array.prototype (value.hpp: no source
// text can spell one and OwnPropertyKeys never reports one) - as a
// global it was an enumerable object on `window`, and the one global of
// a kind ctcompile's native convention has no form for. Each iterator
// used to carry its own two natives and a null prototype.
} // namespace

value iterator_kind_prototype(context & cx, const char * tag, native_fn next) {
    const std::string key = std::string{CTBROWSER_PRIVATE_KEY("IteratorPrototype:")} + tag;
    object_object * home = nullptr; // null only once a page has deleted `Array`
    if (const value array = cx.global("Array"); array.is_object_like()) {
        if (const value proto = cx.lookup_property(array, "prototype"); proto.is_object()) {
            home = static_cast<object_object *>(proto.as_heap());
        }
    }
    const value * kept = home == nullptr ? nullptr : home->find(key);
    if (kept != nullptr && kept->is_object()) { return *kept; }
    object_object * made = new_table(cx);
    if (const value iterator = cx.global("Iterator"); iterator.is_object_like()) {
        const value proto = cx.lookup_property(iterator, "prototype");
        if (proto.is_object()) { made->prototype = proto; }
    }
    made->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string(std::string{tag}),
                 attr_configurable);
    detail::method(cx, made, "next", 0, std::move(next));
    if (home != nullptr) { home->define(key, value::object(made), attr_none); }
    return value::object(made);
}

namespace {

[[nodiscard]] value kind_prototype(context & cx, const char * tag, native_fn next) {
    return iterator_kind_prototype(cx, tag, std::move(next));
}

// CreateArrayIterator's state (23.1.5.1), as private slots on the iterator.
// [[IteratedArrayLike]] undefined means the closure has RETURNED: exhausted,
// or ended by a throw, and every later `next` is done.
inline constexpr std::string_view iterated_key = CTBROWSER_PRIVATE_KEY("IteratedArrayLike");
inline constexpr std::string_view next_index_key = CTBROWSER_PRIVATE_KEY("ArrayLikeNextIndex");
inline constexpr std::string_view kind_key = CTBROWSER_PRIVATE_KEY("ArrayLikeIterationKind");

// %ArrayIteratorPrototype%.next (23.1.5.2.1): one step of CreateArrayIterator's
// closure, LIVE over the array-like. `length` is read on EVERY step (a
// typed array's through the out-of-bounds TypeError), so a push during the
// walk is seen and a truncation ends it, and each element is a [[Get]] as it
// is reached - a snapshot answered neither.
value array_iterator_next(context & c, std::span<value>) {
    const value self = c.current_this();
    auto * it = self.is_object() ? static_cast<object_object *>(self.as_heap()) : nullptr;
    value * target = it == nullptr ? nullptr : it->find(iterated_key);
    value * index = it == nullptr ? nullptr : it->find(next_index_key);
    value * kind = it == nullptr ? nullptr : it->find(kind_key);
    if (target == nullptr || index == nullptr || kind == nullptr) {
        c.throw_error("TypeError", "Array Iterator next called on an incompatible receiver");
        return value::undefined();
    }
    if (target->is_undefined()) { return c.iter_result(value::undefined(), true); }
    // COPIED before anything can run script: a `length` getter may add
    // properties to this very iterator, and the pointers into its table
    // would not survive the rehash.
    const value array = *target;
    const double at = index->is_number() ? index->as_number() : 0;
    const double which = kind->is_number() ? kind->as_number() : 0;
    const context::rooted keep{c, array};
    const std::size_t before = c.unwinds();
    const auto finish = [&] {
        if (value * slot = it->find(iterated_key)) { *slot = value::undefined(); }
        return value::undefined();
    };
    double len = 0;
    if (builtins_detail::is_typed_array(array)) {
        auto * arr = static_cast<array_object *>(array.as_heap());
        if (builtins_detail::typed_array_out_of_bounds(arr)) {
            c.throw_error("TypeError", "Array Iterator next: the typed array is detached or out "
                                       "of bounds");
            return finish();
        }
        len = static_cast<double>(arr->length());
    } else {
        len = array_like_length(c, array);
        if (c.throw_pending() || c.unwinds() != before) { return finish(); }
    }
    if (at >= len) {
        (void)finish();
        return c.iter_result(value::undefined(), true);
    }
    if (value * slot = it->find(next_index_key)) { *slot = value::number(at + 1); }
    if (which == 0) { return c.iter_result(value::number(at), false); }
    const value item = c.lookup_index(array, value::number(at));
    if (c.throw_pending() || c.unwinds() != before) { return finish(); }
    if (which == 1) { return c.iter_result(item, false); }
    const context::rooted keep_item{c, item};
    const value pair = c.make_array();
    auto * entry = static_cast<array_object *>(pair.as_heap());
    entry->items.push_back(value::number(at));
    entry->items.push_back(item);
    const context::rooted keep_pair{c, pair};
    return c.iter_result(pair, false);
}

} // namespace

value array_iterator(context & cx, value target, double kind) {
    const context::rooted keep{cx, target};
    const value proto = kind_prototype(cx, "Array Iterator", array_iterator_next);
    const context::rooted keep_proto{cx, proto};
    const value out = cx.make_object();
    auto * it = static_cast<object_object *>(out.as_heap());
    it->prototype = proto;
    it->define(iterated_key, target, attr_none);
    it->define(next_index_key, value::number(0), attr_none);
    it->define(kind_key, value::number(kind), attr_none);
    return out;
}

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
// ones; iterated_list_key is what `iterable_values` recognises, so `for (const x of
// m.values())` still costs one array copy and no interpretation. A partly
// consumed iterator handed to for..of restarts, which is the one place the two
// disagree and is written down here rather than discovered - nothing drives an
// iterator halfway and then spreads it.
[[nodiscard]] value list_iterator(context & cx, value items, const char * tag) {
    // Reads its state off the RECEIVER rather than out of the closure, so
    // the collector sees one object holding everything and a native
    // captures nothing it would have to root.
    const value table =
        kind_prototype(cx, tag, [kind = std::string{tag}](context & c, std::span<value>) {
            const value self = detail::array_this(c);
            // RequireInternalSlot (22.1.5.1, 24.1.5.1, 24.2.5.1): the
            // receiver is an iterator of THIS kind - iterated_list_key is
            // the slot, iterator_kind_key which prototype made it - or it is a TypeError.
            const value * of_kind =
                self.is_object()
                    ? static_cast<object_object *>(self.as_heap())->find(iterator_kind_key)
                    : nullptr;
            if (of_kind == nullptr || !of_kind->is_string() || c.to_string(*of_kind) != kind) {
                c.throw_error("TypeError", kind + " next called on an incompatible receiver");
                return value::undefined();
            }
            auto * out = static_cast<object_object *>(c.make_object().as_heap());
            array_object * items = nullptr;
            std::size_t at = 0;
            if (self.is_object()) {
                auto * holder = static_cast<object_object *>(self.as_heap());
                if (value * list = holder->find(iterated_list_key);
                    list != nullptr && list->is_array()) {
                    items = static_cast<array_object *>(list->as_heap());
                }
                if (value * cursor = holder->find(iterated_index_key); cursor != nullptr) {
                    const double n = context::to_number(*cursor);
                    at = n > 0 ? static_cast<std::size_t>(n) : 0;
                }
                if (items != nullptr && at < items->items.size()) {
                    holder->define(iterated_index_key, value::number(static_cast<double>(at + 1)),
                                   attr_none);
                }
            }
            const bool done = items == nullptr || at >= items->items.size();
            out->set("done", value::boolean(done));
            out->set("value", done ? value::undefined() : items->items[at]);
            return value::object(out);
        });
    auto * it = static_cast<object_object *>(cx.make_object().as_heap());
    it->prototype = table;
    // PRIVATE-keyed internal slots (value/objects.hpp): no own property a
    // page can see, read or forge - `JSON.stringify(xs.entries())` is `{}`.
    it->define(iterated_list_key, items, attr_none);
    it->define(iterated_index_key, value::number(0), attr_none);
    it->define(iterator_kind_key, cx.string(std::string{tag}), attr_none);
    return value::object(it);
}

} // namespace ctbrowser::script::detail

namespace ctbrowser::script::builtins_detail {

using detail::list_iterator;

// `find` and its three siblings DO NOT SKIP A HOLE - 23.1.3.9 reads every
// index with [[Get]] and hands the callback an undefined - which is why none
// of them goes through `each` below. The miss is undefined for the two that
// answer the item, -1 for the two that answer the index.
value array_find(context & c, std::span<value> a, const char * name, bool backwards,
                 bool want_index) {
    const value miss = want_index ? value::number(-1) : value::undefined();
    const value self = detail::array_this(c);
    if (!detail::coercible_this(c, self, name)) { return miss; }
    // LengthOfArrayLike BEFORE the callback is examined (steps 2-4): a
    // `length` getter runs, and its throw wins, even for a callback that
    // is not callable.
    const double len = detail::array_like_length(c, self);
    if (c.throw_pending()) { return miss; }
    const value callback = arg_at(a, 0);
    if (!detail::callable_arg(c, callback, "callback")) { return miss; }
    const value this_arg = arg_at(a, 1);
    const double step = backwards ? -1.0 : 1.0;
    detail::unwind_watch watch{c};
    for (double k = backwards ? len - 1 : 0; backwards ? k >= 0 : k < len; k += step) {
        const value item = detail::element_at(c, self, k);
        if (watch.threw()) { return miss; }
        const value call_args[3] = {item, value::number(k), self};
        const bool hit = context::truthy(c.call(callback, call_args, this_arg));
        if (c.throw_pending()) { return miss; } // parked, as in `each` below
        watch.rearm();
        if (hit) { return want_index ? value::number(k) : item; }
    }
    return miss;
}

void install_array_iteration(context & cx, native_object * array_ctor,
                             object_object * array_proto) {
    using detail::method;

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
        const value self = detail::array_this(c);
        if (!detail::coercible_this(c, self, name)) { return false; }
        // LengthOfArrayLike BEFORE the callback is examined (steps 2-4): a
        // `length` getter runs, and its throw wins, even for a callback that
        // is not callable.
        detail::unwind_watch watch{c};
        const double len = detail::array_like_length(c, self);
        if (watch.threw()) { return false; }
        const value callback = arg_at(a, 0);
        if (!detail::callable_arg(c, callback, "callback")) { return false; }
        const value this_arg = arg_at(a, 1);
        for (double k = 0; k < len; k += 1.0) {
            // A HOLE IS SKIPPED, not visited with undefined. That is the whole
            // difference between `[, 1].forEach(f)` calling back once and
            // twice, and it is what the array-like tests are written around.
            const bool present = detail::has_element(c, self, k);
            if (watch.threw()) { return false; }
            if (!present) { continue; }
            const value item = detail::element_at(c, self, k);
            if (watch.threw()) { return false; }
            const value call_args[3] = {item, value::number(k), self};
            const value verdict = c.call(callback, call_args, this_arg);
            // A THROW ENDS THE WALK: the rest of an array-like of 2^53 - 1
            // is not walked to find each read and call a no-op. A callback's
            // throw is PARKED (context::call), which throw_pending sees; the
            // unwind count is no signal after a call, since a callback that
            // throws and catches inside itself moves it too.
            if (c.throw_pending() || !body(k, item, verdict)) { return false; }
            watch.rearm();
        }
        return true;
    };
    method(cx, array_proto, "forEach", 1, [each](context & c, std::span<value> a) {
        (void)each(c, a, "forEach", [](double, value, value) { return true; });
        return value::undefined();
    });
    method(cx, array_proto, "map", 1, [](context & c, std::span<value> a) {
        const value self = detail::array_this(c);
        value out = c.make_array();
        if (!detail::coercible_this(c, self, "map")) { return out; }
        // LengthOfArrayLike BEFORE the callback is examined (steps 2-4): a
        // `length` getter runs, and its throw wins, even for a callback that
        // is not callable.
        detail::unwind_watch watch{c};
        const double len = detail::array_like_length(c, self);
        if (watch.threw()) { return out; }
        const value callback = arg_at(a, 0);
        if (!detail::callable_arg(c, callback, "callback")) { return out; }
        const value this_arg = arg_at(a, 1);
        // ArraySpeciesCreate(O, len) FIRST (step 5), and for an Array it is
        // ArrayCreate(len), which refuses a `length` of 2^32. Sizing it up
        // front is also the only way a hole in the source can stay a hole in
        // the result rather than shifting everything after it.
        out = detail::array_species_create(c, self, len);
        if (out.is_undefined()) { return out; }
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
        for (double k = 0; k < len; k += 1.0) {
            const bool present = detail::has_element(c, self, k);
            if (watch.threw()) { return out; }
            if (!present) { continue; }
            const value item = detail::element_at(c, self, k);
            if (watch.threw()) { return out; }
            const value call_args[3] = {item, value::number(k), self};
            const value mapped = c.call(callback, call_args, this_arg);
            if (c.throw_pending()) { return out; }
            watch.rearm();
            if (!detail::create_element(c, out, k, mapped)) { return out; }
        }
        return out;
    });
    // 23.1.3.8, written out rather than through `each` because
    // ArraySpeciesCreate(O, 0) sits between the callback check and the loop.
    method(cx, array_proto, "filter", 1, [](context & c, std::span<value> a) {
        const value self = detail::array_this(c);
        value out = c.make_array();
        if (!detail::coercible_this(c, self, "filter")) { return out; }
        detail::unwind_watch watch{c};
        const double len = detail::array_like_length(c, self);
        if (watch.threw()) { return out; }
        const value callback = arg_at(a, 0);
        if (!detail::callable_arg(c, callback, "callback")) { return out; }
        const value this_arg = arg_at(a, 1);
        out = detail::array_species_create(c, self, 0);
        if (out.is_undefined()) { return out; }
        const context::rooted keep(c, out); // as `map` - see the note there
        double to = 0;
        for (double k = 0; k < len; k += 1.0) {
            const bool present = detail::has_element(c, self, k);
            if (watch.threw()) { return out; }
            if (!present) { continue; }
            const value item = detail::element_at(c, self, k);
            if (watch.threw()) { return out; }
            const context::rooted keep_item(c, item);
            const value call_args[3] = {item, value::number(k), self};
            const bool kept = context::truthy(c.call(callback, call_args, this_arg));
            if (c.throw_pending()) { return out; }
            watch.rearm();
            if (!kept) { continue; }
            if (!detail::create_element(c, out, to, item)) { return out; }
            to += 1.0;
        }
        return out;
    });
    // `find`, `findIndex`, `findLast` and `findLastIndex` (array.cpp) are
    // array_find, walked in one direction or the other.
    method(cx, array_proto, "find", 1,
           [](context & c, std::span<value> a) { return array_find(c, a, "find", false, false); });
    method(cx, array_proto, "findIndex", 1, [](context & c, std::span<value> a) {
        return array_find(c, a, "findIndex", false, true);
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
        const value self = detail::array_this(c);
        if (!detail::coercible_this(c, self, name)) { return value::undefined(); }
        // LengthOfArrayLike BEFORE the callback is examined (steps 2-4): a
        // `length` getter runs, and its throw wins, even for a callback that
        // is not callable.
        detail::unwind_watch watch{c};
        const double len = detail::array_like_length(c, self);
        if (watch.threw()) { return value::undefined(); }
        const value callback = arg_at(a, 0);
        if (!detail::callable_arg(c, callback, "callback")) { return value::undefined(); }
        double k = backwards ? len - 1 : 0;
        const double step = backwards ? -1.0 : 1.0;
        const auto in_range = [&] { return backwards ? k >= 0 : k < len; };
        value total;
        if (a.size() > 1) {
            total = a[1];
        } else {
            bool found = false;
            for (; !found && in_range(); k += step) {
                const bool present = detail::has_element(c, self, k);
                if (watch.threw()) { return value::undefined(); }
                if (present) {
                    total = detail::element_at(c, self, k);
                    if (watch.threw()) { return value::undefined(); }
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
            const bool present = detail::has_element(c, self, k);
            if (watch.threw()) { return value::undefined(); }
            if (!present) { continue; }
            const value item = detail::element_at(c, self, k);
            if (watch.threw()) { return value::undefined(); }
            const value call_args[4] = {total, item, value::number(k), self};
            total = c.call(callback, call_args);
            // A THROW ENDS THE FOLD, rather than walking the rest of an
            // array-like of 2^53 - 1 to call a callback that cannot run.
            if (c.throw_pending()) { return value::undefined(); }
            watch.rearm();
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
            // CODE UNIT order (23.1.3.30.2 via IsLessThan), not the bytes'.
            std::ranges::stable_sort(keyed, [](const auto & x, const auto & y) {
                return utf16::compare(x.first, y.first) < 0;
            });
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
                    // NOT `> 0` TAKES THE LEFT, which is what makes this
                    // stable: equal elements keep their order. Spelled so that
                    // a NaN answer is +0 as 23.1.3.30.2 says - `order <= 0`
                    // was false for a NaN and swapped them.
                    spare[out++] = !(order > 0) ? work[left++] : work[right++];
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
        const value self = detail::array_this(c);
        if (!detail::coercible_this(c, self, "sort")) { return self; }
        // A COMPARATOR THAT THROWS leaves the array as it was: the throw is
        // SortIndexedProperties' and comes before any element is written back.
        const detail::unwind_watch watch{c};
        if (array_object * dense = detail::dense_array_this(self)) {
            // ON A SNAPSHOT, which is a robustness fix rather than a speed one.
            // The old loop indexed `items` while calling out to the comparator,
            // so a comparator that shortened the array - `a.sort(() => {
            // a.length = 0; return 0; })` - left it indexing past the end.
            // Sorting a copy and writing it back cannot: the comparator may do
            // what it likes to the array meanwhile.
            std::vector<value> work = dense->items;
            sort_values(c, work, comparator);
            if (c.throw_pending()) { return self; } // the comparator's throw is parked
            // ASSIGNED, not written slot by slot, so both arms agree about what
            // the array holds afterwards. Where nothing mutated during the sort
            // the two spellings are identical; where something did, this is
            // defined and the old one was not.
            dense->items = std::move(work);
            return self;
        }
        const double len = detail::array_like_length(c, self);
        // A String's indices refuse the write-back - but an EMPTY one has
        // nothing to write, and `[].sort.call("")` is its wrapper.
        if (len > 0 && !detail::mutable_receiver(c, self, "sort")) { return self; }
        if (!detail::generic_walk_ok(c, len)) { return self; }
        // SortIndexedProperties, 23.1.3.30.1, with holes SKIPPED: the present
        // elements are read out, sorted, written back over 0..n-1, and every
        // index the holes used to occupy is deleted - which is what moves them
        // all to the end.
        //
        // INTO A ROOTED ARRAY rather than a bare std::vector<value>, for the
        // reason `toSorted` gives: a getter can collect and a value held only
        // by a C++ vector is in none of the collector's roots.
        const value holder = c.make_array();
        const context::rooted keep(c, holder);
        auto & work = static_cast<array_object *>(holder.as_heap())->items;
        for (double k = 0; k < len; k += 1.0) {
            const bool present = detail::has_element(c, self, k);
            if (watch.threw()) { return self; }
            if (!present) { continue; }
            work.push_back(detail::element_at(c, self, k));
            if (watch.threw()) { return self; }
        }
        sort_values(c, work, comparator);
        if (c.throw_pending()) { return self; }
        const auto kept = static_cast<double>(work.size());
        for (double k = 0; k < kept; k += 1.0) {
            if (!detail::put_element(c, self, k, work[static_cast<std::size_t>(k)])) {
                return self;
            }
        }
        for (double k = kept; k < len; k += 1.0) {
            if (!detail::delete_element(c, self, k)) { return self; }
        }
        return self;
    });
    detail::constant(array_ctor, "prototype", value::object(array_proto));
    // `Array.prototype.toString` IS join(','). The C++ conversion always knew
    // that; the prototype did not, so once conversion started going through an
    // object's own toString an array fell back to Object.prototype's and
    // stringified as "[object Array]".
    //
    // 23.1.3.36 as written: ToObject(this), then its own `join` if callable,
    // else %Object.prototype.toString% - so it is GENERIC (`toString.call(
    // {join() {...}})`) and a non-array receiver answers its tag, where this
    // answered "" for anything but an Array.
    method(cx, array_proto, "toString", 0, [](context & c, std::span<value>) {
        const value self = detail::array_this(c);
        if (!detail::coercible_this(c, self, "toString")) { return value::undefined(); }
        const context::rooted keep{c, self};
        const detail::unwind_watch watch{c};
        value fn = c.lookup_property(self, "join");
        if (watch.threw()) { return value::undefined(); }
        if (!fn.is_callable()) {
            object_object * table = c.prototype(context::proto_kind::object);
            const value * intrinsic =
                table == nullptr ? nullptr
                                 : table->find(CTBROWSER_PRIVATE_KEY("IntrinsicObjectToString:"));
            if (intrinsic == nullptr) { return c.string("[object Object]"); }
            fn = *intrinsic;
        }
        return c.call(fn, {}, self);
    });
    // 23.2.3.32: %TypedArray%.prototype.toString IS this function object -
    // installed here because install_typed_arrays runs first.
    if (object_object * typed = cx.prototype(context::proto_kind::typed_array)) {
        if (const value * fn = array_proto->find("toString")) {
            typed->define("toString", *fn, attr_builtin);
        }
    }
    // 23.1.3.5 entries, 23.1.3.19 keys, 23.1.3.38 values: ToObject(this),
    // then CreateArrayIterator - LIVE over any array-like, not a copy of an
    // Array's items (see array_iterator).
    const auto iterator_method = [&](const char * name, double kind) {
        method(cx, array_proto, name, 0, [name, kind](context & c, std::span<value>) {
            const value self = detail::array_this(c);
            if (!detail::coercible_this(c, self, name)) { return value::undefined(); }
            return detail::array_iterator(c, self, kind);
        });
    };
    iterator_method("entries", 2);
    iterator_method("keys", 0);
    iterator_method("values", 1);
    // 23.1.3.40: Array.prototype[@@iterator] IS `values` - the same function
    // object. `for (x of a)` never asks (op::iterable reads the items), but
    // `for await`, spread of an array into a Map, and anything driving the
    // protocol by hand does.
    if (value * values = array_proto->find("values")) {
        array_proto->define(CTBROWSER_SYMBOL_KEY("iterator"), *values,
                            attr_writable | attr_configurable);
    }
    // 23.1.3.41 Array.prototype[@@unscopables]: a null-prototype object naming
    // the methods a `with (array)` block must not resolve, each `true` and
    // enumerable, in the specification's order; the property itself is
    // { writable: false, enumerable: false, configurable: true }.
    {
        value made = cx.make_object();
        auto * blocked = static_cast<object_object *>(made.as_heap());
        blocked->prototype = value::undefined(); // an EXPLICIT null (object_object::prototype)
        for (const char * name : {"at", "copyWithin", "entries", "fill", "find", "findIndex",
                                  "findLast", "findLastIndex", "flat", "flatMap", "includes",
                                  "keys", "toReversed", "toSorted", "toSpliced", "values"}) {
            blocked->set(name, value::boolean(true));
        }
        array_proto->define(CTBROWSER_SYMBOL_KEY("unscopables"), made, attr_configurable);
    }
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
        const value self = detail::array_this(c);
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
        const detail::unwind_watch watch{c};
        for (; count > 0; count -= 1.0, from += direction, to += direction) {
            const bool present = detail::has_element(c, self, from);
            if (watch.threw()) { return self; }
            if (present) {
                const value moved = detail::element_at(c, self, from);
                if (watch.threw()) { return self; }
                if (!detail::put_element(c, self, to, moved)) { return self; }
            } else if (!detail::delete_element(c, self, to)) {
                // DeletePropertyOrThrow (step 17.e.i): a non-configurable
                // target index is the TypeError, not a silent keep.
                return self;
            }
        }
        return self;
    });
    method(cx, array_proto, "with", 2, [](context & c, std::span<value> a) {
        const value self = detail::array_this(c);
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
            if (!detail::put_element(c, out, k,
                                     k == at ? replacement : detail::element_at(c, self, k))) {
                return out;
            }
        }
        return out;
    });
    method(cx, array_proto, "toReversed", 0, [](context & c, std::span<value>) {
        const value self = detail::array_this(c);
        value out = c.make_array();
        if (!detail::coercible_this(c, self, "toReversed")) { return out; }
        const double len = detail::array_like_length(c, self);
        const context::rooted keep(c, out);
        if (detail::new_array_of_length(c, out, len) == nullptr) { return out; }
        for (double k = 0; k < len; k += 1.0) {
            if (!detail::put_element(c, out, k, detail::element_at(c, self, len - k - 1))) {
                return out;
            }
        }
        return out;
    });
    // `toSorted` and `toSpliced` READ THE WHOLE RECEIVER FIRST, into a rooted
    // vector, and then build the result out of it. That is what the
    // specification's SortIndexedProperties does and it is also the only shape
    // that survives a comparator which mutates the array it was handed.
    method(cx, array_proto, "toSorted", 1, [sort_values](context & c, std::span<value> a) {
        const value self = detail::array_this(c);
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
        const value self = detail::array_this(c);
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
            if (!detail::put_element(c, out, at, detail::element_at(c, self, at))) { return out; }
        }
        for (std::size_t i = 2; i < a.size(); ++i, at += 1.0) {
            if (!detail::put_element(c, out, at, a[i])) { return out; }
        }
        for (double from = start + skipped; at < new_len; at += 1.0, from += 1.0) {
            if (!detail::put_element(c, out, at, detail::element_at(c, self, from))) { return out; }
        }
        return out;
    });
}

} // namespace ctbrowser::script::builtins_detail
