//===- array_iteration.cpp - Array callbacks and iterators ------*- C++ -*-===//
//
// Array's callback-taking methods, sort, the array iterators, copyWithin and
// the ES2023 change-by-copy methods; also list_iterator, the Iterator over an
// existing list that Map and Set share. Shared argument helpers, namespace
// detail and these functions' declarations are in ../internal.hpp. Section
// numbers are ECMA-262 (ES2024 numbering).
//
//===----------------------------------------------------------------------===//

#include "typed_arrays/internal.hpp"

namespace ctbrowser::script::detail {

namespace {} // namespace

/// Returns the shared prototype for one iterator kind (%ArrayIteratorPrototype%
/// and its String/Map/Set siblings, 23.1.5.2, 22.1.5.1, 24.1.5.1, 24.2.5.1). It
/// holds `next` and @@toStringTag, and inherits from %Iterator.prototype%, so
/// @@iterator and the iterator helpers work on every iterator of the kind. It
/// is created on first use, because Iterator is installed after the
/// collections, and cached under a private key on Array.prototype (see
/// CTBROWSER_PRIVATE_KEY in value/base.hpp) so no script can see or enumerate
/// it.
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

/// Shorthand for iterator_kind_prototype inside this file.
[[nodiscard]] value kind_prototype(context & cx, const char * tag, native_fn next) {
    return iterator_kind_prototype(cx, tag, std::move(next));
}

/// CreateArrayIterator's state (23.1.5.1), as private slots on the iterator.
/// [[IteratedArrayLike]] undefined means the closure has returned (exhausted,
/// or ended by a throw), and every later `next` is done. The kind is 0 for
/// keys, 1 for values and 2 for entries.
inline constexpr std::string_view iterated_key = CTBROWSER_PRIVATE_KEY("IteratedArrayLike");
inline constexpr std::string_view next_index_key = CTBROWSER_PRIVATE_KEY("ArrayLikeNextIndex");
inline constexpr std::string_view kind_key = CTBROWSER_PRIVATE_KEY("ArrayLikeIterationKind");

/// %ArrayIteratorPrototype%.next (23.1.5.2.1): one step of
/// CreateArrayIterator's closure, live over the array-like. `length` is read on
/// every step (a typed array's through the out-of-bounds TypeError), so a push
/// during the walk is seen and a truncation ends it, and each element is a
/// [[Get]] as it is reached rather than read from a snapshot.
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
    // Copied before anything can run script: a `length` getter may add
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

/// CreateArrayIterator(target, kind): kind 0 is keys, 1 values, 2 entries.
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

/// Returns a real Iterator over an existing list (used by Map and Set
/// keys/values/entries). It supports both protocols: `next` and @@iterator for
/// code that drives it by hand, and iterated_list_key, which
/// context::iterable_values recognises so `for..of` copies the list once
/// without calling `next`. A partly consumed iterator passed to for..of
/// restarts from the beginning; nothing currently relies on that case.
[[nodiscard]] value list_iterator(context & cx, value items, const char * tag) {
    // Reads its state off the receiver rather than out of the closure, so
    // the collector sees one object holding everything and a native
    // captures nothing it would have to root.
    const value table =
        kind_prototype(cx, tag, [kind = std::string{tag}](context & c, std::span<value>) {
            const value self = detail::array_this(c);
            // RequireInternalSlot (22.1.5.1, 24.1.5.1, 24.2.5.1): the
            // receiver must be an iterator of this kind (iterated_list_key is
            // the slot, iterator_kind_key names the kind), or it is a TypeError.
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
    // Private-keyed internal slots (value/objects.hpp): no own property a
    // page can see, read or forge, so `JSON.stringify(xs.entries())` is `{}`.
    it->define(iterated_list_key, items, attr_none);
    it->define(iterated_index_key, value::number(0), attr_none);
    it->define(iterator_kind_key, cx.string(std::string{tag}), attr_none);
    return value::object(it);
}

} // namespace ctbrowser::script::detail

namespace ctbrowser::script::builtins_detail {

using detail::list_iterator;

/// find, findIndex, findLast and findLastIndex. They do not skip a hole:
/// 23.1.3.9 reads every index with [[Get]] and hands the callback an
/// undefined, which is why none of them goes through `each` below. The miss is
/// undefined for the two that answer the item, -1 for the two that answer the
/// index.
value array_find(context & c, std::span<value> a, const char * name, bool backwards,
                 bool want_index) {
    const value miss = want_index ? value::number(-1) : value::undefined();
    const value self = detail::array_this(c);
    if (!detail::coercible_this(c, self, name)) { return miss; }
    // LengthOfArrayLike before the callback is examined (steps 2-4): a
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
        if (c.throw_pending()) { return miss; } // pending, as in `each` below
        watch.rearm();
        if (hit) { return want_index ? value::number(k) : item; }
    }
    return miss;
}

/// Installs the second half of Array.prototype (see the file comment) and sets
/// Array.prototype on `array_ctor`.
void install_array_iteration(context & cx, native_object * array_ctor,
                             object_object * array_proto) {
    using detail::method;

    // The iteration methods call back into the VM through context::call().
    // Each reads the item before the call, because the callback may mutate
    // the array underneath it.
    //
    // All of these methods accept any array-like receiver (see
    // detail::array_like_length), take an optional `thisArg`, and throw a
    // TypeError for a non-callable callback.
    //
    // `body` gets the index, the element and what the callback answered, and
    // returns false to stop. `each` reports whether it ran to the end, which is
    // what `every` needs and what a `return` out of the middle is not.
    const auto each = [](context & c, std::span<value> a, const char * name, auto && body) {
        const value self = detail::array_this(c);
        if (!detail::coercible_this(c, self, name)) { return false; }
        // LengthOfArrayLike before the callback is examined (steps 2-4): a
        // `length` getter runs, and its throw wins, even for a callback that
        // is not callable.
        detail::unwind_watch watch{c};
        const double len = detail::array_like_length(c, self);
        if (watch.threw()) { return false; }
        const value callback = arg_at(a, 0);
        if (!detail::callable_arg(c, callback, "callback")) { return false; }
        const value this_arg = arg_at(a, 1);
        for (double k = 0; k < len; k += 1.0) {
            // A hole is skipped, not visited with undefined, so
            // `[, 1].forEach(f)` calls back once.
            const bool present = detail::has_element(c, self, k);
            if (watch.threw()) { return false; }
            if (!present) { continue; }
            const value item = detail::element_at(c, self, k);
            if (watch.threw()) { return false; }
            const value call_args[3] = {item, value::number(k), self};
            const value verdict = c.call(callback, call_args, this_arg);
            // A throw ends the walk, so the rest of an array-like of 2^53 - 1
            // is not walked. A callback's throw is left pending
            // (context::call), which throw_pending sees; the unwind count is
            // no signal after a call, since a callback that throws and catches
            // inside itself moves it too.
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
        // LengthOfArrayLike before the callback is examined (steps 2-4): a
        // `length` getter runs, and its throw wins, even for a callback that
        // is not callable.
        detail::unwind_watch watch{c};
        const double len = detail::array_like_length(c, self);
        if (watch.threw()) { return out; }
        const value callback = arg_at(a, 0);
        if (!detail::callable_arg(c, callback, "callback")) { return out; }
        const value this_arg = arg_at(a, 1);
        // ArraySpeciesCreate(O, len) first (step 5), and for an Array it is
        // ArrayCreate(len), which refuses a `length` of 2^32. Sizing it up
        // front is also the only way a hole in the source can stay a hole in
        // the result rather than shifting everything after it.
        out = detail::array_species_create(c, self, len);
        if (out.is_undefined()) { return out; }
        // The result is a C++ local that lives across every callback call, and
        // C++ locals are not garbage-collector roots. A callback that triggers
        // a collection (`$262.gc()`, or GC stress mode) would free the array
        // while later iterations still write into it: a heap-use-after-free
        // under ASan and silent corruption otherwise. context::rooted keeps it
        // alive.
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
    // 23.1.3.8 filter, written out rather than through `each` because
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
        const context::rooted keep(c, out); // as in `map`: see the note there
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
        // A throw must not read as `true`. `each` stops on a refusal from the
        // callback and on a TypeError it raised itself; `answer` is only
        // meaningful in the first case, and it starts true for the empty array
        // that 23.1.3.6 says is vacuously every.
        return value::boolean(answer);
    });
    // `reduce` and `reduceRight` are one fold walked in two directions. Both
    // throw a TypeError for an empty array with no initial value.
    const auto fold = [](context & c, std::span<value> a, const char * name, bool backwards) {
        const value self = detail::array_this(c);
        if (!detail::coercible_this(c, self, name)) { return value::undefined(); }
        // LengthOfArrayLike before the callback is examined (steps 2-4): a
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
        // Root the callback: it is a C++ local and may be the only reference
        // left (see the note in `map`).
        const context::rooted keep_callback(c, callback);
        for (; in_range(); k += step) {
            // The accumulator is rooted per iteration, not once: `rooted` takes
            // a copy, so a root taken before the loop would pin the initial
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
            // A throw ends the fold, rather than walking the rest of an
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
    // Sorts `work` in place for both `sort` and `toSorted`, so the two cannot
    // disagree about stability.
    const auto sort_values = [](context & c, std::vector<value> & work, value comparator) {
        // The vector may be the only reference to its elements once the
        // comparator has emptied the array they came from (`a.sort(function
        // (x, y) { a.length = 0; $262.gc(); ... })`), and a bare
        // std::vector<value> is not a GC root. The work vector is permuted,
        // never added to, so rooting its initial contents roots every value
        // this loop can reach.
        const context::rooted_values keep_work(c, work);
        // The comparator is a C++ local too, and the only reference to it once
        // whatever produced it has been overwritten.
        const context::rooted keep_comparator(c, comparator);
        if (!comparator.is_callable()) {
            // The default order is lexicographic on the string form, which is
            // why [10, 9].sort() is [10, 9].
            //
            // The string keys are computed once per element rather than inside
            // the comparison, which would convert each element about 2 log n
            // times.
            //
            // undefined sorts last, separately from the comparison:
            // 23.1.3.30 moves every undefined to the end and never asks about
            // one, while ToString(undefined) would land between "u" and "v".
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
            // UTF-16 code unit order (23.1.3.30.2 via IsLessThan), not the
            // UTF-8 bytes' order.
            std::ranges::stable_sort(keyed, [](const auto & x, const auto & y) {
                return utf16::compare(x.first, y.first) < 0;
            });
            work.clear();
            for (const auto & [key, item] : keyed) { work.push_back(item); }
            work.resize(work.size() + undefined_count, value::undefined());
            return;
        }
        // Bottom-up merge sort. std::sort is undefined behaviour with an
        // inconsistent comparator, and a JavaScript comparator can return
        // anything or mutate the array. Each merge reads only inside index
        // ranges it computed itself, so no comparator result can cause an
        // out-of-bounds read. It is also stable (required by the specification)
        // and O(n log n).
        const std::size_t n = work.size();
        if (n <= 1) { return; }
        std::vector<value> spare(n);
        for (std::size_t width = 1; width < n; width *= 2) {
            for (std::size_t lo = 0; lo < n; lo += 2 * width) {
                const std::size_t mid = std::min(lo + width, n);
                const std::size_t hi = std::min(lo + 2 * width, n);
                std::size_t left = lo, right = mid, out = lo;
                while (left < mid && right < hi) {
                    // undefined is never compared in this arm either: it sorts
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
                    // "Not greater than 0" takes the left, which makes this
                    // stable: equal elements keep their order. Written as
                    // `!(order > 0)` so a NaN answer counts as +0, as
                    // 23.1.3.30.2 says (`order <= 0` is false for NaN).
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
        // The comparator is checked before the receiver is touched (23.1.3.30
        // step 1), so `[].sort(1)` is a TypeError rather than a sorted nothing.
        if (!comparator.is_undefined() && !comparator.is_callable()) {
            c.throw_error("TypeError",
                          "The comparison function must be either a function or undefined");
            return c.current_this();
        }
        const value self = detail::array_this(c);
        if (!detail::coercible_this(c, self, "sort")) { return self; }
        // A comparator that throws leaves the array as it was: the throw is
        // SortIndexedProperties' and comes before any element is written back.
        const detail::unwind_watch watch{c};
        if (array_object * dense = detail::dense_array_this(self)) {
            // Sort a copy and assign it back: the comparator may shrink or
            // otherwise mutate the array while the sort runs.
            std::vector<value> work = dense->items;
            sort_values(c, work, comparator);
            if (c.throw_pending()) { return self; } // the comparator's throw is pending
            // Assign the whole vector, so the result is well defined even if the
            // comparator mutated the array.
            dense->items = std::move(work);
            return self;
        }
        const double len = detail::array_like_length(c, self);
        // A String's indices refuse the write-back, but an empty one has
        // nothing to write, and `[].sort.call("")` is its wrapper.
        if (len > 0 && !detail::mutable_receiver(c, self, "sort")) { return self; }
        if (!detail::generic_walk_ok(c, len)) { return self; }
        // SortIndexedProperties, 23.1.3.30.1, with holes skipped: the present
        // elements are read out, sorted, written back over 0..n-1, and every
        // remaining index up to `len` is deleted, which moves the holes to the
        // end.
        //
        // They are read into a rooted array rather than a bare
        // std::vector<value>: a getter can trigger a collection, and a value
        // held only by a C++ vector is not a GC root.
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
    // Array.prototype.toString (23.1.3.36): ToObject(this), then call its
    // `join` if callable, otherwise %Object.prototype.toString%. It is generic,
    // so a non-array receiver without `join` gets its tag.
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
    // 23.2.3.32: %TypedArray%.prototype.toString is this same function object,
    // installed here because install_typed_arrays runs first.
    if (object_object * typed = cx.prototype(context::proto_kind::typed_array)) {
        if (const value * fn = array_proto->find("toString")) {
            typed->define("toString", *fn, attr_builtin);
        }
    }
    // 23.1.3.5 entries, 23.1.3.19 keys, 23.1.3.38 values: ToObject(this),
    // then CreateArrayIterator, live over any array-like, not a copy of an
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
    // 23.1.3.40: Array.prototype[@@iterator] is `values`, the same function
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
        blocked->prototype = value::undefined(); // an explicit null (object_object::prototype)
        for (const char * name : {"at", "copyWithin", "entries", "fill", "find", "findIndex",
                                  "findLast", "findLastIndex", "flat", "flatMap", "includes",
                                  "keys", "toReversed", "toSorted", "toSpliced", "values"}) {
            blocked->set(name, value::boolean(true));
        }
        array_proto->define(CTBROWSER_SYMBOL_KEY("unscopables"), made, attr_configurable);
    }
    // --- copyWithin and the ES2023 change-by-copy methods -------------------
    //
    // copyWithin (23.1.3.4), and `with`, `toReversed`, `toSorted`, `toSpliced`
    // (23.1.3.39, .33, .34, .35). All are generic over the receiver; the four
    // copying methods always build an ordinary Array, so
    // `Array.prototype.toReversed.call({length: 2, ...})` is an array.
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
        // Overlapping ranges copy backwards (memmove semantics): copying
        // forwards would overwrite a source element before reading it.
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
        // An out-of-range index is a RangeError, not a silent grow: `with`
        // returns a copy of the same length.
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
    // `toSorted` reads the whole receiver first, into its rooted result, and
    // then sorts that. This is what the specification's SortIndexedProperties
    // does, and it survives a comparator that mutates the source array.
    method(cx, array_proto, "toSorted", 1, [sort_values](context & c, std::span<value> a) {
        const value self = detail::array_this(c);
        value out = c.make_array();
        const value comparator = arg_at(a, 0);
        // The comparator is checked before the receiver is read, the order
        // 23.1.3.34 gives.
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
        // Read into the result, not into a bare std::vector: `element_at` can
        // run a getter that triggers a collection, and a value held only by a
        // C++ vector is not a GC root. Everything pushed here is reachable
        // through the rooted `out` from the moment it lands. The reservation
        // is capped because `len` comes from an array-like and can be up to
        // max_array_length.
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
        // gives, because 23.1.3.35 step 12 checks it before creating anything.
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
