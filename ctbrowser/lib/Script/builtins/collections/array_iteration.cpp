// ctbrowser.script builtins - Array, continued: the callback-taking methods,
// sort, the iterators, and the five ES2023 additions. Also list_iterator, the
// real Iterator over an existing list that Map and Set share.
//
// One of four files carved out of a 1,824-line builtins/collections.cpp on
// 2026-09-08 - which was itself one of five carved out of builtins.cpp on
// 2026-08-09. Everything shared - the argument helpers, namespace detail, and
// these functions' declarations - is in ../internal.hpp; what only this
// directory shares is in internal.hpp beside this.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

using detail::list_iterator;

namespace detail {

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

} // namespace detail

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
}

} // namespace ctbrowser::script::builtins_detail
