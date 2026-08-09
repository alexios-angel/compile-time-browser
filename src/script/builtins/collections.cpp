// ctbrowser.script builtins - Array, the keyed collections, and the typed arrays.
//
// One of five files carved out of a 4,118-line builtins.cpp on 2026-08-09.
// Everything shared - the argument helpers, namespace detail, and these
// functions' declarations - is in internal.hpp.

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
            made->items.assign(static_cast<std::size_t>(std::max(0.0, a[0].as_number())),
                               value::undefined());
        } else {
            made->items.assign(a.begin(), a.end());
        }
        return out;
    });
    const auto static_method = [&](const char * name, native_fn fn) {
        array_ctor->set(name, value::object(cx.allocate<native_object>(name, std::move(fn))));
    };
    static_method("isArray", [](context &, std::span<value> a) {
        return value::boolean(arg_at(a, 0).is_array());
    });
    static_method("of", [](context & c, std::span<value> a) {
        value out = c.make_array();
        static_cast<array_object *>(out.as_heap())->items.assign(a.begin(), a.end());
        return out;
    });
    static_method("from", [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * made = static_cast<array_object *>(out.as_heap());
        // Through the one conversion for..of and spread use, so all three agree
        // about what "iterable" means - a Map, a Set, a string, an array or
        // anything array-LIKE (a NodeList, `arguments`, a typed-array shim).
        const value from = c.iterable_values(arg_at(a, 0));
        if (from.is_array()) { made->items = static_cast<array_object *>(from.as_heap())->items; }
        if (a.size() > 1 && a[1].is_callable()) {
            for (std::size_t i = 0; i < made->items.size(); ++i) {
                const value args[2] = {made->items[i], value::number(static_cast<double>(i))};
                made->items[i] = c.call(a[1], args);
            }
        }
        return out;
    });
    cx.define_global("Array", value::object(array_ctor));

    object_object * array_proto = new_table(cx);
    // The prototype methods p5.js uses that were not here. `at` and `fill` are
    // the ones it leans on hardest - 43 and 31 uses - because a typed-array
    // shim reaches for both.
    method(cx, array_proto, "at", [](context & c, std::span<value> a) {
        auto * self = detail::this_array(c);
        if (self == nullptr) { return value::undefined(); }
        double i = num_at(a, 0);
        if (i < 0) { i += static_cast<double>(self->items.size()); }
        if (i < 0 || i >= static_cast<double>(self->items.size())) { return value::undefined(); }
        return self->items[static_cast<std::size_t>(i)];
    });
    method(cx, array_proto, "fill", [](context & c, std::span<value> a) {
        auto * self = detail::this_array(c);
        if (self == nullptr) { return c.current_this(); }
        const std::size_t n = self->items.size();
        const std::size_t from = a.size() > 1 ? clamp_index(num_at(a, 1), n) : 0;
        const std::size_t to = a.size() > 2 ? clamp_index(num_at(a, 2), n) : n;
        for (std::size_t i = from; i < to; ++i) { self->items[i] = arg_at(a, 0); }
        return c.current_this();
    });
    method(cx, array_proto, "flat", [](context & c, std::span<value> a) {
        // depth defaults to 1, which is what every use in p5 wants
        const double depth = a.empty() ? 1.0 : num_at(a, 0);
        auto * self = detail::this_array(c);
        value out = c.make_array();
        if (self == nullptr) { return out; }
        auto * result = static_cast<array_object *>(out.as_heap());
        // An explicit worklist rather than recursion: `flat(Infinity)` on a
        // deep structure must not be bounded by the C++ stack.
        std::vector<std::pair<value, double>> pending;
        for (std::size_t i = self->items.size(); i-- > 0;) {
            pending.emplace_back(self->items[i], depth);
        }
        while (!pending.empty()) {
            const auto [item, left] = pending.back();
            pending.pop_back();
            if (item.is_array() && left > 0) {
                const auto & inner = static_cast<array_object *>(item.as_heap())->items;
                for (std::size_t i = inner.size(); i-- > 0;) {
                    pending.emplace_back(inner[i], left - 1);
                }
            } else {
                result->items.push_back(item);
            }
        }
        return out;
    });
    method(cx, array_proto, "flatMap", [](context & c, std::span<value> a) {
        auto * self = detail::this_array(c);
        value out = c.make_array();
        if (self == nullptr || a.empty() || !a[0].is_callable()) { return out; }
        auto * result = static_cast<array_object *>(out.as_heap());
        const std::size_t n = self->items.size();
        for (std::size_t i = 0; i < n && i < self->items.size(); ++i) {
            const value args[3] = {self->items[i], value::number(static_cast<double>(i)),
                                   c.current_this()};
            const value mapped = c.call(a[0], args);
            if (mapped.is_array()) {
                for (const value & item : static_cast<array_object *>(mapped.as_heap())->items) {
                    result->items.push_back(item);
                }
            } else {
                result->items.push_back(mapped);
            }
        }
        return out;
    });
    method(cx, array_proto, "findLast", [](context & c, std::span<value> a) {
        auto * self = detail::this_array(c);
        if (self == nullptr || a.empty() || !a[0].is_callable()) { return value::undefined(); }
        for (std::size_t i = self->items.size(); i-- > 0;) {
            const value args[3] = {self->items[i], value::number(static_cast<double>(i)),
                                   c.current_this()};
            if (context::truthy(c.call(a[0], args))) { return self->items[i]; }
        }
        return value::undefined();
    });
    method(cx, array_proto, "findLastIndex", [](context & c, std::span<value> a) {
        auto * self = detail::this_array(c);
        if (self == nullptr || a.empty() || !a[0].is_callable()) { return value::number(-1); }
        for (std::size_t i = self->items.size(); i-- > 0;) {
            const value args[3] = {self->items[i], value::number(static_cast<double>(i)),
                                   c.current_this()};
            if (context::truthy(c.call(a[0], args))) {
                return value::number(static_cast<double>(i));
            }
        }
        return value::number(-1);
    });
    method(cx, array_proto, "push", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::number(0); }
        for (std::size_t i = 0; i < a.size(); ++i) { self->items.push_back(a[i]); }
        return value::number(static_cast<double>(self->items.size()));
    });
    method(cx, array_proto, "pop", [](context & c, std::span<value>) {
        array_object * self = detail::this_array(c);
        if (self == nullptr || self->items.empty()) { return value::undefined(); }
        const value out = self->items.back();
        self->items.pop_back();
        return out;
    });
    method(cx, array_proto, "shift", [](context & c, std::span<value>) {
        array_object * self = detail::this_array(c);
        if (self == nullptr || self->items.empty()) { return value::undefined(); }
        const value out = self->items.front();
        self->items.erase(self->items.begin());
        return out;
    });
    method(cx, array_proto, "unshift", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::number(0); }
        self->items.insert(self->items.begin(), a.begin(), a.end());
        return value::number(static_cast<double>(self->items.size()));
    });
    method(cx, array_proto, "slice", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        value out = c.make_array();
        if (self == nullptr) { return out; }
        const std::size_t n = self->items.size();
        const std::size_t from = clamp_index(a.empty() ? 0 : num_at(a, 0), n);
        const std::size_t to = a.size() > 1 ? clamp_index(num_at(a, 1), n) : n;
        auto * result = static_cast<array_object *>(out.as_heap());
        for (std::size_t i = from; i < to; ++i) { result->items.push_back(self->items[i]); }
        return out;
    });
    method(cx, array_proto, "splice", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        value removed = c.make_array();
        if (self == nullptr) { return removed; }
        const std::size_t n = self->items.size();
        const std::size_t from = clamp_index(num_at(a, 0), n);
        // CLAMPED AS A DOUBLE, THEN CAST. `splice(i, Infinity)` is an ordinary
        // way to say "to the end" and infinity does not convert to an integer -
        // that is undefined behaviour, not a large number, and UBSan caught it
        // going through here silently.
        const double wanted = std::max(0.0, num_at(a, 1));
        const std::size_t count =
            a.size() > 1 ? static_cast<std::size_t>(std::min(wanted, static_cast<double>(n - from)))
                         : n - from;
        auto * out = static_cast<array_object *>(removed.as_heap());
        for (std::size_t i = 0; i < count; ++i) { out->items.push_back(self->items[from + i]); }
        self->items.erase(self->items.begin() + static_cast<std::ptrdiff_t>(from),
                          self->items.begin() + static_cast<std::ptrdiff_t>(from + count));
        if (a.size() > 2) {
            self->items.insert(self->items.begin() + static_cast<std::ptrdiff_t>(from),
                               a.begin() + 2, a.end());
        }
        return removed;
    });
    method(cx, array_proto, "indexOf", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::number(-1); }
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            if (self->items[i].strict_equals(arg_at(a, 0))) {
                return value::number(static_cast<double>(i));
            }
        }
        return value::number(-1);
    });
    method(cx, array_proto, "includes", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::boolean(false); }
        for (const value & item : self->items) {
            if (item.strict_equals(arg_at(a, 0))) { return value::boolean(true); }
        }
        return value::boolean(false);
    });
    method(cx, array_proto, "join", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return c.string(std::string{}); }
        const std::string sep = a.empty() ? "," : c.to_string(a[0]);
        std::string out;
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            if (i > 0) { out += sep; }
            // null and undefined join as EMPTY, not as "null"/"undefined".
            if (!self->items[i].is_undefined() && !self->items[i].is_null()) {
                out += c.to_string(self->items[i]);
            }
        }
        return c.string(out);
    });
    method(cx, array_proto, "concat", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        if (self != nullptr) { result->items = self->items; }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i].is_array()) {
                const auto & other = static_cast<array_object *>(a[i].as_heap())->items;
                result->items.insert(result->items.end(), other.begin(), other.end());
            } else {
                result->items.push_back(a[i]);
            }
        }
        return out;
    });
    method(cx, array_proto, "reverse", [](context & c, std::span<value>) {
        array_object * self = detail::this_array(c);
        if (self != nullptr) { std::ranges::reverse(self->items); }
        return c.current_this();
    });
    // The iteration methods call back INTO the VM, which is what
    // context::call() exists for. Each snapshots the item first, because the
    // callback may mutate the array underneath it.
    const auto each = [](context & c, std::span<value> a, auto && body) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return; }
        const value callback = arg_at(a, 0);
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            const value call_args[3] = {self->items[i], value::number(static_cast<double>(i)),
                                        c.current_this()};
            if (!body(i, c.call(callback, call_args))) { return; }
        }
    };
    method(cx, array_proto, "forEach", [each](context & c, std::span<value> a) {
        each(c, a, [](std::size_t, value) { return true; });
        return value::undefined();
    });
    method(cx, array_proto, "map", [each](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        each(c, a, [&](std::size_t, value produced) {
            result->items.push_back(produced);
            return true;
        });
        return out;
    });
    method(cx, array_proto, "filter", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        value out = c.make_array();
        if (self == nullptr) { return out; }
        auto * result = static_cast<array_object *>(out.as_heap());
        const value callback = arg_at(a, 0);
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            const value item = self->items[i];
            const value call_args[3] = {item, value::number(static_cast<double>(i)),
                                        c.current_this()};
            if (context::truthy(c.call(callback, call_args))) { result->items.push_back(item); }
        }
        return out;
    });
    method(cx, array_proto, "find", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::undefined(); }
        const value callback = arg_at(a, 0);
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            const value item = self->items[i];
            const value call_args[3] = {item, value::number(static_cast<double>(i)),
                                        c.current_this()};
            if (context::truthy(c.call(callback, call_args))) { return item; }
        }
        return value::undefined();
    });
    method(cx, array_proto, "findIndex", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::number(-1); }
        const value callback = arg_at(a, 0);
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            const value call_args[3] = {self->items[i], value::number(static_cast<double>(i)),
                                        c.current_this()};
            if (context::truthy(c.call(callback, call_args))) {
                return value::number(static_cast<double>(i));
            }
        }
        return value::number(-1);
    });
    method(cx, array_proto, "some", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::boolean(false); }
        const value callback = arg_at(a, 0);
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            const value call_args[3] = {self->items[i], value::number(static_cast<double>(i)),
                                        c.current_this()};
            if (context::truthy(c.call(callback, call_args))) { return value::boolean(true); }
        }
        return value::boolean(false);
    });
    method(cx, array_proto, "every", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::boolean(true); }
        const value callback = arg_at(a, 0);
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            const value call_args[3] = {self->items[i], value::number(static_cast<double>(i)),
                                        c.current_this()};
            if (!context::truthy(c.call(callback, call_args))) { return value::boolean(false); }
        }
        return value::boolean(true);
    });
    method(cx, array_proto, "reduce", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::undefined(); }
        const value callback = arg_at(a, 0);
        std::size_t i = 0;
        value total;
        if (a.size() > 1) {
            total = a[1];
        } else {
            if (self->items.empty()) { return value::undefined(); }
            total = self->items[0];
            i = 1;
        }
        for (; i < self->items.size(); ++i) {
            const value call_args[4] = {total, self->items[i],
                                        value::number(static_cast<double>(i)), c.current_this()};
            total = c.call(callback, call_args);
        }
        return total;
    });
    method(cx, array_proto, "sort", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return c.current_this(); }
        const value comparator = arg_at(a, 0);
        if (comparator.is_callable()) {
            // A BOTTOM-UP MERGE SORT, and every word of that is load-bearing.
            //
            // `std::sort` would be undefined behaviour with an inconsistent
            // comparator, and a comparator written in JavaScript can be
            // anything at all - it can return random numbers, or mutate the
            // array it is sorting. That is why this was a stable INSERTION
            // sort: it cannot be talked into reading out of bounds.
            //
            // But it was O(n^2), measured: 250 elements 0.5 ms, 4000 elements
            // 104 ms, tracking n^2 to within a tenth. Ten thousand would be
            // most of a second and a page would look hung.
            //
            // Merge sort keeps the property that mattered. Each merge reads
            // only within two index ranges it computed itself, so no answer the
            // comparator gives can move an index out of them - the safety comes
            // from the STRUCTURE rather than from the comparator behaving. It
            // is stable, which the specification requires. And it is n log n.
            //
            // ON A SNAPSHOT, which is a robustness fix rather than a speed one.
            // The old loop indexed `self->items` while calling out to the
            // comparator, so a comparator that shortened the array - `a.sort(()
            // => { a.length = 0; return 0; })` - left it indexing past the end.
            // Sorting a copy and writing it back cannot: the comparator may do
            // what it likes to the array meanwhile.
            std::vector<value> work = self->items;
            const std::size_t n = work.size();
            if (n > 1) {
                std::vector<value> spare(n);
                for (std::size_t width = 1; width < n; width *= 2) {
                    for (std::size_t lo = 0; lo < n; lo += 2 * width) {
                        const std::size_t mid = std::min(lo + width, n);
                        const std::size_t hi = std::min(lo + 2 * width, n);
                        std::size_t left = lo, right = mid, out = lo;
                        while (left < mid && right < hi) {
                            // `<= 0` TAKES THE LEFT, which is what makes this
                            // stable: equal elements keep their order.
                            const value pair[2] = {work[left], work[right]};
                            const double order = context::to_number(c.call(comparator, pair));
                            spare[out++] = order <= 0 ? work[left++] : work[right++];
                        }
                        while (left < mid) { spare[out++] = work[left++]; }
                        while (right < hi) { spare[out++] = work[right++]; }
                    }
                    work.swap(spare);
                }
            }
            self->items = std::move(work);
        } else {
            // The default really is lexicographic on the STRING form, which is
            // why [10, 9].sort() is [10, 9].
            //
            // THE KEYS ARE COMPUTED ONCE. `stable_sort` with `to_string` inside
            // the comparison converts each element about 2 log n times and
            // allocates a std::string for every one of them; a page sorting a
            // thousand items paid for twenty thousand conversions to answer a
            // thousand questions.
            std::vector<std::pair<std::string, value>> keyed;
            keyed.reserve(self->items.size());
            for (const value & item : self->items) { keyed.emplace_back(c.to_string(item), item); }
            std::ranges::stable_sort(keyed, {}, &std::pair<std::string, value>::first);
            for (std::size_t i = 0; i < keyed.size(); ++i) { self->items[i] = keyed[i].second; }
        }
        return c.current_this();
    });
    array_ctor->set("prototype", value::object(array_proto));
    // `Array.prototype.toString` IS join(','). The C++ conversion always knew
    // that; the prototype did not, so once conversion started going through an
    // object's own toString an array fell back to Object.prototype's and
    // stringified as "[object Array]".
    method(cx, array_proto, "toString", [](context & c, std::span<value>) {
        auto * self = detail::this_array(c);
        if (self == nullptr) { return c.string(""); }
        std::string out;
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            if (i != 0) { out += ','; }
            if (!self->items[i].is_nullish()) { out += c.to_string(self->items[i]); }
        }
        return c.string(out);
    });
    // `entries`, `keys` and `values`. Each hands back an ARRAY where the spec
    // says an iterator, for the same reason matchAll does: `for (const [i, v] of
    // xs.entries())` and a spread both work over one, which is everything
    // anybody does with them. p5's Table walks its rows with entries().
    method(cx, array_proto, "entries", [](context & c, std::span<value>) {
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
        return out;
    });
    method(cx, array_proto, "keys", [](context & c, std::span<value>) {
        auto * self = detail::this_array(c);
        value out = c.make_array();
        if (self == nullptr) { return out; }
        auto * items = static_cast<array_object *>(out.as_heap());
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            items->items.push_back(value::number(static_cast<double>(i)));
        }
        return out;
    });
    method(cx, array_proto, "values", [](context & c, std::span<value>) {
        auto * self = detail::this_array(c);
        value out = c.make_array();
        if (self == nullptr) { return out; }
        static_cast<array_object *>(out.as_heap())->items = self->items;
        return out;
    });
    link_constructor(cx, array_proto, "Array", value::object(array_ctor));
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
    const auto same_value_zero = [](value a, value b) {
        if (a.is_number() && b.is_number()) {
            const double x = a.as_number();
            const double y = b.as_number();
            return (std::isnan(x) && std::isnan(y)) || x == y;
        }
        return a.strict_equals(b);
    };

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
        ctor->set("prototype", value::object(proto));
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
    method(cx, map_proto, "get", [find_entry](context & c, std::span<value> a) {
        value * entry = find_entry(c, arg_at(a, 0));
        if (entry == nullptr) { return value::undefined(); }
        const auto & pair = static_cast<array_object *>(entry->as_heap())->items;
        return pair.size() > 1 ? pair[1] : value::undefined();
    });
    method(cx, map_proto, "has", [find_entry](context & c, std::span<value> a) {
        return value::boolean(find_entry(c, arg_at(a, 0)) != nullptr);
    });
    method(cx, map_proto, "set", [find_entry, entries_of](context & c, std::span<value> a) {
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
    method(cx, map_proto, "delete", [entries_of, same_value_zero](context & c, std::span<value> a) {
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
    method(cx, map_proto, "clear", [entries_of](context & c, std::span<value>) {
        if (array_object * entries = entries_of(c)) { entries->items.clear(); }
        return value::undefined();
    });
    method(cx, map_proto, "forEach", [entries_of](context & c, std::span<value> a) {
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
    // Arrays, not iterators: for..of walks an array, and that is what these are
    // for. `[...map.keys()]` works; `map.keys().next()` does not.
    method(cx, map_proto, "keys", [column](context & c, std::span<value>) { return column(c, 0); });
    method(cx, map_proto, "values",
           [column](context & c, std::span<value>) { return column(c, 1); });
    method(cx, map_proto, "entries",
           [column](context & c, std::span<value>) { return column(c, 2); });
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
    method(cx, set_proto, "has", [set_index](context & c, std::span<value> a) {
        return value::boolean(set_index(c, arg_at(a, 0)) >= 0);
    });
    method(cx, set_proto, "add", [set_index, entries_of](context & c, std::span<value> a) {
        if (set_index(c, arg_at(a, 0)) < 0) {
            if (array_object * entries = entries_of(c)) { entries->items.push_back(arg_at(a, 0)); }
        }
        return c.current_this();
    });
    method(cx, set_proto, "delete", [set_index, entries_of](context & c, std::span<value> a) {
        const std::ptrdiff_t at = set_index(c, arg_at(a, 0));
        if (at < 0) { return value::boolean(false); }
        array_object * entries = entries_of(c);
        entries->items.erase(entries->items.begin() + at);
        return value::boolean(true);
    });
    method(cx, set_proto, "clear", [entries_of](context & c, std::span<value>) {
        if (array_object * entries = entries_of(c)) { entries->items.clear(); }
        return value::undefined();
    });
    method(cx, set_proto, "forEach", [entries_of](context & c, std::span<value> a) {
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
    method(cx, set_proto, "values",
           [members](context & c, std::span<value>) { return members(c); });
    method(cx, set_proto, "keys", [members](context & c, std::span<value>) { return members(c); });
    // A Set's `entries` pairs each member WITH ITSELF, which looks odd and is
    // the spec: it exists so a Set and a Map can be walked by the same code.
    method(cx, set_proto, "entries", [members](context & c, std::span<value>) {
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
        return out;
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
    method(cx, typed_proto, "set", [](context & c, std::span<value> a) {
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
    method(cx, typed_proto, "subarray", [](context & c, std::span<value> a) {
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
        ctor->set("BYTES_PER_ELEMENT", value::number(each.bytes));

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
        method(cx, ctor, "of",
               [build](context & c, std::span<value> a) { return build(c, a, nullptr); });
        method(cx, ctor, "from", [build](context & c, std::span<value> a) {
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
