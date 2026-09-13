// ctbrowser.script builtins - %TypedArray%.prototype (ECMA-262 23.2.3): the
// accessors, the iteration methods, and the ones that build a new array
// through TypedArraySpeciesCreate. internal.hpp in this directory says how a
// typed array is laid out and where the VM answers a view's own properties
// before any getter here is asked.
//
// EVERY METHOD STARTS WITH ValidateTypedArray - `this` must be a typed array
// whose buffer is neither detached nor shrunk out from under it - and reads
// its elements through typed_array_get, which answers undefined past the
// length AS IT IS NOW: a callback may have shrunk or detached the buffer,
// and 23.2.3 specifies exactly that Get(O, Pk) answers undefined then.
//
// NOT HERE: `toString`, which 23.2.3.32 says IS Array.prototype.toString.
// install_array runs after this installer, so the function does not exist yet
// - and a typed array's lookup chain falls through to Array.prototype anyway
// (vm/objects/lookup.cpp), so `ta.toString()` finds it; only the property on
// %TypedArray%.prototype itself is missing.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

namespace {

inline constexpr std::string_view iter_array_key = "@#IteratedArray";
inline constexpr std::string_view iter_index_key = "@#ArrayIteratorNextIndex";
inline constexpr std::string_view iter_kind_key = "@#ArrayIterationKind";

[[nodiscard]] double as_number(value v) {
    return v.is_number() ? v.as_number() : 0.0;
}

// The kind's element count now, as a double for the index arithmetic below.
[[nodiscard]] double len_of(array_object * arr) {
    return static_cast<double>(typed_array_length(arr));
}

// IsCallable on the callback, and its message.
[[nodiscard]] bool callback_arg(context & cx, std::span<value> a, const char * method) {
    if (arg_at(a, 0).is_callable()) { return true; }
    cx.throw_error("TypeError",
                   std::string{"TypedArray.prototype."} + method + ": callback is not a function");
    return false;
}

// The (kValue, k, O) call every iteration method makes. False with the throw
// in flight.
[[nodiscard]] bool call_back(context & cx, value fn, value this_arg, value item, double k,
                             value self, value & out) {
    const value args[3] = {item, value::number(k), self};
    out = cx.call(fn, args, this_arg);
    return !cx.throw_pending();
}

// TypedArrayCreateSameType (23.2.4.3): the receiver's own constructor,
// species ignored.
[[nodiscard]] value create_same_type(context & cx, array_object * exemplar, double len) {
    const value args[1] = {value::number(len)};
    return typed_array_create_from_constructor(cx, typed_array_constructor(cx, exemplar->elements),
                                               args);
}

// A relative index argument the way 23.2.3.x spells them: ToIntegerOrInfinity,
// negative from the end, clamped into [0, len]. `fallback` is what an absent
// argument means. False with a throw in flight.
[[nodiscard]] bool relative_arg(context & cx, std::span<value> a, std::size_t i, double len,
                                double fallback, double & out) {
    double rel = fallback;
    if (arg_at(a, i).is_undefined()) {
        out = fallback < 0 ? 0 : std::min(fallback, len);
        return true;
    }
    if (!to_integer_or_infinity(cx, a[i], rel)) { return false; }
    out = rel < 0 ? std::max(len + rel, 0.0) : std::min(rel, len);
    return true;
}

// A byte-for-byte copy between two stores, clamped to what both hold.
void copy_bytes(array_object * from, std::size_t from_at, array_object * to, std::size_t to_at,
                std::size_t count) {
    if (from_at >= from->items.size() || to_at >= to->items.size()) { return; }
    count = std::min({count, from->items.size() - from_at, to->items.size() - to_at});
    if (from == to && to_at > from_at) {
        std::copy_backward(from->items.begin() + static_cast<std::ptrdiff_t>(from_at),
                           from->items.begin() + static_cast<std::ptrdiff_t>(from_at + count),
                           to->items.begin() + static_cast<std::ptrdiff_t>(to_at + count));
        return;
    }
    std::copy_n(from->items.begin() + static_cast<std::ptrdiff_t>(from_at), count,
                to->items.begin() + static_cast<std::ptrdiff_t>(to_at));
}

// SetTypedArrayFromTypedArray (23.2.3.26.1) and ...FromArrayLike (23.2.3.26.2).
value typed_array_set_method(context & c, std::span<value> a) {
    const value self = c.current_this();
    if (!is_typed_array(self)) {
        c.throw_error("TypeError", "TypedArray.prototype.set: this is not a typed array");
        return value::undefined();
    }
    auto * target = static_cast<array_object *>(self.as_heap());
    double offset = 0;
    if (!to_integer_or_infinity(c, arg_at(a, 1), offset)) { return value::undefined(); }
    if (offset < 0) {
        c.throw_error("RangeError", "offset is out of bounds");
        return value::undefined();
    }
    if (validate_typed_array(c, self, "TypedArray.prototype.set") == nullptr) {
        return value::undefined();
    }
    const double target_len = len_of(target);
    const value source = arg_at(a, 0);
    if (is_typed_array(source)) {
        array_object * src = validate_typed_array(c, source, "TypedArray.prototype.set");
        if (src == nullptr) { return value::undefined(); }
        const double src_len = len_of(src);
        if (std::isinf(offset) || src_len + offset > target_len) {
            c.throw_error("RangeError", "offset is out of bounds");
            return value::undefined();
        }
        const auto at = static_cast<std::size_t>(offset);
        const std::size_t n = static_cast<std::size_t>(src_len);
        array_object * from = typed_array_store(src);
        array_object * to = typed_array_store(target);
        if (from != nullptr && to != nullptr && src->elements == target->elements) {
            // The same kind copies bytes - through copy_bytes, whose backward
            // copy is what makes an overlapping same-buffer copy safe.
            const std::size_t width = bytes_per_element(src->elements);
            copy_bytes(from, src->byte_offset, to, target->byte_offset + at * width, n * width);
            return value::undefined();
        }
        // Different kinds over the SAME buffer read every source element
        // before writing any (step 12: "let srcByteIndex ... clone").
        std::vector<double> held(n);
        for (std::size_t i = 0; i < n; ++i) { held[i] = as_number(typed_array_get(src, i)); }
        for (std::size_t i = 0; i < n; ++i) { typed_array_set(target, at + i, held[i]); }
        return value::undefined();
    }
    const value src = detail::box_primitive(c, source);
    if (src.is_nullish()) {
        c.throw_error("TypeError", "TypedArray.prototype.set: source is null or undefined");
        return value::undefined();
    }
    const double src_len = detail::array_like_length(c, src);
    if (c.throw_pending()) { return value::undefined(); }
    if (std::isinf(offset) || src_len + offset > target_len) {
        c.throw_error("RangeError", "offset is out of bounds");
        return value::undefined();
    }
    for (double k = 0; k < src_len; k += 1) {
        const value v = c.lookup_index(src, value::number(k));
        if (c.throw_pending()) { return value::undefined(); }
        if (!numeric_arg(c, v)) { return value::undefined(); }
        const double n = c.to_number_value(v);
        if (c.throw_pending()) { return value::undefined(); }
        typed_array_set(target, static_cast<std::size_t>(offset + k), n);
    }
    return value::undefined();
}

// The %ArrayIteratorPrototype% every typed array's keys/values/entries share:
// LIVE over the array, so a detach between two `next()` calls is the
// TypeError 23.1.5.1 specifies rather than a snapshot's stale answer.
object_object * install_iterator_prototype(context & cx) {
    object_object * table = detail::new_table(cx);
    detail::method(cx, table, "next", 0, [](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!self.is_object()) {
            c.throw_error("TypeError", "next called on a non-iterator");
            return value::undefined();
        }
        auto * it = static_cast<object_object *>(self.as_heap());
        value * target = it->find(iter_array_key);
        value * index = it->find(iter_index_key);
        value * kind = it->find(iter_kind_key);
        if (target == nullptr || index == nullptr || kind == nullptr) {
            c.throw_error("TypeError", "next called on a non-iterator");
            return value::undefined();
        }
        if (target->is_undefined()) { return c.iter_result(value::undefined(), true); }
        array_object * arr = validate_typed_array(c, *target, "Array Iterator next");
        if (arr == nullptr) { return value::undefined(); }
        const double at = as_number(*index);
        if (at >= len_of(arr)) {
            *target = value::undefined();
            return c.iter_result(value::undefined(), true);
        }
        *index = value::number(at + 1);
        const value item = typed_array_get(arr, static_cast<std::size_t>(at));
        const double which = as_number(*kind);
        if (which == 0) { return c.iter_result(value::number(at), false); }
        if (which == 1) { return c.iter_result(item, false); }
        const value pair = c.make_array();
        auto * entry = static_cast<array_object *>(pair.as_heap());
        entry->items.push_back(value::number(at));
        entry->items.push_back(item);
        return c.iter_result(pair, false);
    });
    detail::method(cx, table, "@@iterator", 0,
                   [](context & c, std::span<value>) { return c.current_this(); });
    table->define("@@toStringTag", cx.string("Array Iterator"), attr_configurable);
    return table;
}

[[nodiscard]] value make_iterator(context & c, object_object * proto, value target, double kind) {
    const value out = c.make_object();
    auto * it = static_cast<object_object *>(out.as_heap());
    it->prototype = value::object(proto);
    it->define(iter_array_key, target, attr_none);
    it->define(iter_index_key, value::number(0), attr_none);
    it->define(iter_kind_key, value::number(kind), attr_none);
    return out;
}

} // namespace

bool sort_numbers(context & cx, std::vector<double> & work, value comparator) {
    const std::size_t n = work.size();
    if (n <= 1) { return true; }
    // SortCompare (23.2.4.7) without a comparator: numeric, -0 before +0, NaN
    // last - a total order, so the standard library's stable sort is safe.
    if (!comparator.is_callable()) {
        std::stable_sort(work.begin(), work.end(), [](double x, double y) {
            if (std::isnan(x)) { return false; }
            if (std::isnan(y)) { return true; }
            if (x < y) { return true; }
            if (x > y) { return false; }
            return std::signbit(x) && !std::signbit(y);
        });
        return true;
    }
    // A comparator written in JavaScript can answer anything, so this is the
    // bottom-up merge sort Array.prototype.sort uses: every index it reads is
    // one it computed itself.
    std::vector<double> spare(n);
    for (std::size_t width = 1; width < n; width *= 2) {
        for (std::size_t lo = 0; lo < n; lo += 2 * width) {
            const std::size_t mid = std::min(lo + width, n);
            const std::size_t hi = std::min(lo + 2 * width, n);
            std::size_t left = lo, right = mid, out = lo;
            while (left < mid && right < hi) {
                const value pair[2] = {value::number(work[left]), value::number(work[right])};
                const value answer = cx.call(comparator, pair);
                if (cx.throw_pending()) { return false; }
                if (!numeric_arg(cx, answer)) { return false; }
                const double order = cx.to_number_value(answer);
                if (cx.throw_pending()) { return false; }
                spare[out++] = order <= 0 || std::isnan(order) ? work[left++] : work[right++];
            }
            while (left < mid) { spare[out++] = work[left++]; }
            while (right < hi) { spare[out++] = work[right++]; }
        }
        work.swap(spare);
    }
    return true;
}

void install_typed_array_prototype(context & cx, object_object * proto) {
    using detail::method;

    // --- the accessors, 23.2.3.1-4 and 23.2.3.38 -----------------------------
    const auto getter = [&](const char * name, native_fn fn) {
        auto * made = detail::method_native(cx, std::string{"get "} + name, std::move(fn));
        detail::install_arity(cx, made, 0);
        proto->define_accessor(name, value::object(made), value::undefined(), attr_configurable);
    };
    // The receiver of an accessor: a typed array, whatever the state of its
    // buffer (RequireInternalSlot only), or null with the TypeError in flight.
    const auto receiver = [](context & c, const char * name) -> array_object * {
        const value self = c.current_this();
        if (is_typed_array(self)) { return static_cast<array_object *>(self.as_heap()); }
        c.throw_error("TypeError",
                      std::string{"TypedArray.prototype."} + name + ": this is not a typed array");
        return nullptr;
    };
    getter("buffer", [receiver](context & c, std::span<value>) {
        array_object * arr = receiver(c, "buffer");
        if (arr == nullptr) { return value::undefined(); }
        array_object * store = ensure_store(c, arr);
        return store == nullptr ? value::undefined() : buffer_of_store(c, store);
    });
    getter("byteLength", [receiver](context & c, std::span<value>) {
        array_object * arr = receiver(c, "byteLength");
        if (arr == nullptr) { return value::undefined(); }
        if (typed_array_out_of_bounds(arr)) { return value::number(0); }
        return value::number(static_cast<double>(arr->length() * bytes_per_element(arr->elements)));
    });
    getter("byteOffset", [receiver](context & c, std::span<value>) {
        array_object * arr = receiver(c, "byteOffset");
        if (arr == nullptr) { return value::undefined(); }
        if (typed_array_out_of_bounds(arr)) { return value::number(0); }
        return value::number(static_cast<double>(arr->byte_offset));
    });
    getter("length", [receiver](context & c, std::span<value>) {
        array_object * arr = receiver(c, "length");
        if (arr == nullptr) { return value::undefined(); }
        if (typed_array_out_of_bounds(arr)) { return value::number(0); }
        return value::number(len_of(arr));
    });
    getter("@@toStringTag", [](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!is_typed_array(self)) { return value::undefined(); }
        return c.string(
            typed_array_global_name(static_cast<array_object *>(self.as_heap())->elements));
    });

    // --- 23.2.3.5 at ----------------------------------------------------------
    method(cx, proto, "at", 1, [](context & c, std::span<value> a) {
        array_object * arr = this_typed_array(c, "TypedArray.prototype.at");
        if (arr == nullptr) { return value::undefined(); }
        const double len = len_of(arr);
        double rel = 0;
        if (!to_integer_or_infinity(c, arg_at(a, 0), rel)) { return value::undefined(); }
        const double k = rel >= 0 ? rel : len + rel;
        if (k < 0 || k >= len) { return value::undefined(); }
        return typed_array_get(arr, static_cast<std::size_t>(k));
    });
    // --- 23.2.3.6 copyWithin --------------------------------------------------
    method(cx, proto, "copyWithin", 2, [](context & c, std::span<value> a) {
        array_object * arr = this_typed_array(c, "TypedArray.prototype.copyWithin");
        if (arr == nullptr) { return value::undefined(); }
        const value self = c.current_this();
        double len = len_of(arr);
        double to = 0, from = 0, final = len;
        if (!relative_arg(c, a, 0, len, 0, to) || !relative_arg(c, a, 1, len, 0, from) ||
            !relative_arg(c, a, 2, len, len, final)) {
            return value::undefined();
        }
        const double count = std::min(final - from, len - to);
        if (count <= 0) { return self; }
        if (validate_typed_array(c, self, "TypedArray.prototype.copyWithin") == nullptr) {
            return value::undefined();
        }
        len = len_of(arr);
        const std::size_t width = bytes_per_element(arr->elements);
        if (array_object * store = typed_array_store(arr)) {
            const auto limit = static_cast<std::size_t>(len) * width + arr->byte_offset;
            const auto to_byte = static_cast<std::size_t>(to) * width + arr->byte_offset;
            const auto from_byte = static_cast<std::size_t>(from) * width + arr->byte_offset;
            auto count_bytes = static_cast<std::size_t>(count) * width;
            // Clamped to the array's CURRENT bytes (step 18: the buffer may
            // have shrunk while the arguments were coerced).
            if (to_byte >= limit || from_byte >= limit) { return self; }
            count_bytes = std::min({count_bytes, limit - to_byte, limit - from_byte});
            copy_bytes(store, from_byte, store, to_byte, count_bytes);
            return self;
        }
        const auto n = static_cast<std::size_t>(std::min(count, len - std::max(to, from)));
        std::vector<value> held(arr->items.begin() + static_cast<std::ptrdiff_t>(from),
                                arr->items.begin() + static_cast<std::ptrdiff_t>(from) +
                                    static_cast<std::ptrdiff_t>(n));
        std::copy(held.begin(), held.end(), arr->items.begin() + static_cast<std::ptrdiff_t>(to));
        return self;
    });
    // --- 23.2.3.7 / 23.2.3.19 / 23.2.3.35 entries, keys, values, @@iterator ---
    object_object * iter_proto = install_iterator_prototype(cx);
    const auto iterator_method = [&](const char * name, double kind) {
        method(cx, proto, name, 0, [iter_proto, kind, name](context & c, std::span<value>) {
            array_object * arr =
                this_typed_array(c, (std::string{"TypedArray.prototype."} + name).c_str());
            if (arr == nullptr) { return value::undefined(); }
            return make_iterator(c, iter_proto, c.current_this(), kind);
        });
    };
    iterator_method("entries", 2);
    iterator_method("keys", 0);
    iterator_method("values", 1);
    if (value * values = proto->find("values")) {
        proto->define("@@iterator", *values, attr_builtin);
    }
    // --- 23.2.3.8 every, 23.2.3.29 some, 23.2.3.15 forEach ---------------------
    const auto walk = [&](const char * name, int mode) {
        // mode: 0 forEach, 1 every, 2 some
        method(cx, proto, name, 1, [name, mode](context & c, std::span<value> a) {
            const std::string method_name = std::string{"TypedArray.prototype."} + name;
            array_object * arr = this_typed_array(c, method_name.c_str());
            if (arr == nullptr) { return value::undefined(); }
            const double len = len_of(arr);
            if (!callback_arg(c, a, name)) { return value::undefined(); }
            const value self = c.current_this();
            for (double k = 0; k < len; k += 1) {
                value answer = value::undefined();
                if (!call_back(c, a[0], arg_at(a, 1),
                               typed_array_get(arr, static_cast<std::size_t>(k)), k, self,
                               answer)) {
                    return value::undefined();
                }
                if (mode == 1 && !context::truthy(answer)) { return value::boolean(false); }
                if (mode == 2 && context::truthy(answer)) { return value::boolean(true); }
            }
            return mode == 0 ? value::undefined() : value::boolean(mode == 1);
        });
    };
    walk("every", 1);
    walk("some", 2);
    walk("forEach", 0);
    // --- 23.2.3.9 fill ---------------------------------------------------------
    method(cx, proto, "fill", 1, [](context & c, std::span<value> a) {
        array_object * arr = this_typed_array(c, "TypedArray.prototype.fill");
        if (arr == nullptr) { return value::undefined(); }
        const value self = c.current_this();
        double len = len_of(arr);
        if (!numeric_arg(c, arg_at(a, 0))) { return value::undefined(); }
        const double v = c.to_number_value(arg_at(a, 0));
        if (c.throw_pending()) { return value::undefined(); }
        double k = 0, final = len;
        if (!relative_arg(c, a, 1, len, 0, k) || !relative_arg(c, a, 2, len, len, final)) {
            return value::undefined();
        }
        if (validate_typed_array(c, self, "TypedArray.prototype.fill") == nullptr) {
            return value::undefined();
        }
        len = len_of(arr);
        final = std::min(final, len);
        for (; k < final; k += 1) { typed_array_set(arr, static_cast<std::size_t>(k), v); }
        return self;
    });
    // --- 23.2.3.10 filter ------------------------------------------------------
    method(cx, proto, "filter", 1, [](context & c, std::span<value> a) {
        array_object * arr = this_typed_array(c, "TypedArray.prototype.filter");
        if (arr == nullptr) { return value::undefined(); }
        const double len = len_of(arr);
        if (!callback_arg(c, a, "filter")) { return value::undefined(); }
        const value self = c.current_this();
        const value kept = c.make_array();
        auto * list = static_cast<array_object *>(kept.as_heap());
        for (double k = 0; k < len; k += 1) {
            const value item = typed_array_get(arr, static_cast<std::size_t>(k));
            value answer = value::undefined();
            if (!call_back(c, a[0], arg_at(a, 1), item, k, self, answer)) {
                return value::undefined();
            }
            if (context::truthy(answer)) { list->items.push_back(item); }
        }
        const value args[1] = {value::number(static_cast<double>(list->items.size()))};
        const value out = typed_array_species_create(c, arr, args);
        if (out.is_undefined()) { return out; }
        auto * made = static_cast<array_object *>(out.as_heap());
        for (std::size_t i = 0; i < list->items.size(); ++i) {
            typed_array_set(made, i, as_number(list->items[i]));
        }
        return out;
    });
    // --- 23.2.3.11-14 find, findIndex, findLast, findLastIndex -----------------
    const auto finder = [&](const char * name, bool backwards, bool want_index) {
        method(cx, proto, name, 1, [name, backwards, want_index](context & c, std::span<value> a) {
            const std::string method_name = std::string{"TypedArray.prototype."} + name;
            array_object * arr = this_typed_array(c, method_name.c_str());
            if (arr == nullptr) { return value::undefined(); }
            const double len = len_of(arr);
            if (!callback_arg(c, a, name)) { return value::undefined(); }
            const value self = c.current_this();
            for (double i = 0; i < len; i += 1) {
                const double k = backwards ? len - 1 - i : i;
                const value item = typed_array_get(arr, static_cast<std::size_t>(k));
                value answer = value::undefined();
                if (!call_back(c, a[0], arg_at(a, 1), item, k, self, answer)) {
                    return value::undefined();
                }
                if (context::truthy(answer)) { return want_index ? value::number(k) : item; }
            }
            return want_index ? value::number(-1) : value::undefined();
        });
    };
    finder("find", false, false);
    finder("findIndex", false, true);
    finder("findLast", true, false);
    finder("findLastIndex", true, true);
    // --- 23.2.3.16 includes, 23.2.3.17 indexOf, 23.2.3.20 lastIndexOf ----------
    method(cx, proto, "includes", 1, [](context & c, std::span<value> a) {
        array_object * arr = this_typed_array(c, "TypedArray.prototype.includes");
        if (arr == nullptr) { return value::undefined(); }
        const double len = len_of(arr);
        if (len == 0) { return value::boolean(false); }
        double n = 0;
        if (!to_integer_or_infinity(c, arg_at(a, 1), n)) { return value::undefined(); }
        if (n == std::numeric_limits<double>::infinity()) { return value::boolean(false); }
        double k = n >= 0 ? n : std::max(len + n, 0.0);
        const value wanted = arg_at(a, 0);
        for (; k < len; k += 1) {
            if (typed_array_get(arr, static_cast<std::size_t>(k)).same_value_zero(wanted)) {
                return value::boolean(true);
            }
        }
        return value::boolean(false);
    });
    method(cx, proto, "indexOf", 1, [](context & c, std::span<value> a) {
        array_object * arr = this_typed_array(c, "TypedArray.prototype.indexOf");
        if (arr == nullptr) { return value::undefined(); }
        const double len = len_of(arr);
        if (len == 0) { return value::number(-1); }
        double n = 0;
        if (!to_integer_or_infinity(c, arg_at(a, 1), n)) { return value::undefined(); }
        if (n == std::numeric_limits<double>::infinity()) { return value::number(-1); }
        double k = n >= 0 ? n : std::max(len + n, 0.0);
        const value wanted = arg_at(a, 0);
        // HasProperty(O, k) is false past the CURRENT length (step 10.a).
        const double now = std::min(len, len_of(arr));
        for (; k < now; k += 1) {
            if (typed_array_get(arr, static_cast<std::size_t>(k)).strict_equals(wanted)) {
                return value::number(k);
            }
        }
        return value::number(-1);
    });
    method(cx, proto, "lastIndexOf", 1, [](context & c, std::span<value> a) {
        array_object * arr = this_typed_array(c, "TypedArray.prototype.lastIndexOf");
        if (arr == nullptr) { return value::undefined(); }
        const double len = len_of(arr);
        if (len == 0) { return value::number(-1); }
        double n = len - 1;
        if (a.size() > 1 && !to_integer_or_infinity(c, a[1], n)) { return value::undefined(); }
        if (n == -std::numeric_limits<double>::infinity()) { return value::number(-1); }
        double k = n >= 0 ? std::min(n, len - 1) : len + n;
        const value wanted = arg_at(a, 0);
        const double now = len_of(arr);
        for (; k >= 0; k -= 1) {
            if (k < now &&
                typed_array_get(arr, static_cast<std::size_t>(k)).strict_equals(wanted)) {
                return value::number(k);
            }
        }
        return value::number(-1);
    });
    // --- 23.2.3.18 join, 23.2.3.31 toLocaleString ------------------------------
    method(cx, proto, "join", 1, [](context & c, std::span<value> a) {
        array_object * arr = this_typed_array(c, "TypedArray.prototype.join");
        if (arr == nullptr) { return value::undefined(); }
        const double len = len_of(arr);
        std::string sep = ",";
        if (!arg_at(a, 0).is_undefined()) {
            if (!stringable_arg(c, a[0])) { return value::undefined(); }
            sep = c.to_string(a[0]);
            if (c.throw_pending()) { return value::undefined(); }
        }
        std::string out;
        for (double k = 0; k < len; k += 1) {
            if (k > 0) { out += sep; }
            const value item = typed_array_get(arr, static_cast<std::size_t>(k));
            if (!item.is_undefined()) { out += c.to_string(item); }
        }
        return c.string(std::move(out));
    });
    method(cx, proto, "toLocaleString", 0, [](context & c, std::span<value>) {
        array_object * arr = this_typed_array(c, "TypedArray.prototype.toLocaleString");
        if (arr == nullptr) { return value::undefined(); }
        const double len = len_of(arr);
        std::string out;
        for (double k = 0; k < len; k += 1) {
            if (k > 0) { out += ","; }
            const value item = typed_array_get(arr, static_cast<std::size_t>(k));
            if (item.is_nullish()) { continue; }
            const value fn = c.lookup_property(item, "toLocaleString");
            if (c.throw_pending()) { return value::undefined(); }
            if (!fn.is_callable()) {
                c.throw_error("TypeError", "toLocaleString is not a function");
                return value::undefined();
            }
            const value text = c.call(fn, {}, item);
            if (c.throw_pending()) { return value::undefined(); }
            if (!stringable_arg(c, text)) { return value::undefined(); }
            out += c.to_string(text);
            if (c.throw_pending()) { return value::undefined(); }
        }
        return c.string(std::move(out));
    });
    // --- 23.2.3.22 map ---------------------------------------------------------
    method(cx, proto, "map", 1, [](context & c, std::span<value> a) {
        array_object * arr = this_typed_array(c, "TypedArray.prototype.map");
        if (arr == nullptr) { return value::undefined(); }
        const double len = len_of(arr);
        if (!callback_arg(c, a, "map")) { return value::undefined(); }
        const value self = c.current_this();
        const value args[1] = {value::number(len)};
        const value out = typed_array_species_create(c, arr, args);
        if (out.is_undefined()) { return out; }
        auto * made = static_cast<array_object *>(out.as_heap());
        for (double k = 0; k < len; k += 1) {
            value mapped = value::undefined();
            if (!call_back(c, a[0], arg_at(a, 1), typed_array_get(arr, static_cast<std::size_t>(k)),
                           k, self, mapped)) {
                return value::undefined();
            }
            if (!numeric_arg(c, mapped)) { return value::undefined(); }
            const double n = c.to_number_value(mapped);
            if (c.throw_pending()) { return value::undefined(); }
            typed_array_set(made, static_cast<std::size_t>(k), n);
        }
        return out;
    });
    // --- 23.2.3.23 reduce, 23.2.3.24 reduceRight --------------------------------
    const auto reducer = [&](const char * name, bool backwards) {
        method(cx, proto, name, 1, [name, backwards](context & c, std::span<value> a) {
            const std::string method_name = std::string{"TypedArray.prototype."} + name;
            array_object * arr = this_typed_array(c, method_name.c_str());
            if (arr == nullptr) { return value::undefined(); }
            const double len = len_of(arr);
            if (!callback_arg(c, a, name)) { return value::undefined(); }
            if (len == 0 && a.size() < 2) {
                c.throw_error("TypeError", method_name + " of empty array with no initial value");
                return value::undefined();
            }
            const value self = c.current_this();
            double i = 0;
            value acc = value::undefined();
            if (a.size() >= 2) {
                acc = a[1];
            } else {
                acc = typed_array_get(arr, static_cast<std::size_t>(backwards ? len - 1 : 0));
                i = 1;
            }
            for (; i < len; i += 1) {
                const double k = backwards ? len - 1 - i : i;
                const value args[4] = {acc, typed_array_get(arr, static_cast<std::size_t>(k)),
                                       value::number(k), self};
                acc = c.call(a[0], args);
                if (c.throw_pending()) { return value::undefined(); }
            }
            return acc;
        });
    };
    reducer("reduce", false);
    reducer("reduceRight", true);
    // --- 23.2.3.25 reverse, 23.2.3.33 toReversed ---------------------------------
    method(cx, proto, "reverse", 0, [](context & c, std::span<value>) {
        array_object * arr = this_typed_array(c, "TypedArray.prototype.reverse");
        if (arr == nullptr) { return value::undefined(); }
        const std::size_t len = typed_array_length(arr);
        for (std::size_t lo = 0, hi = len; lo + 1 < hi; ++lo, --hi) {
            const double a = as_number(typed_array_get(arr, lo));
            const double b = as_number(typed_array_get(arr, hi - 1));
            typed_array_set(arr, lo, b);
            typed_array_set(arr, hi - 1, a);
        }
        return c.current_this();
    });
    method(cx, proto, "toReversed", 0, [](context & c, std::span<value>) {
        array_object * arr = this_typed_array(c, "TypedArray.prototype.toReversed");
        if (arr == nullptr) { return value::undefined(); }
        const std::size_t len = typed_array_length(arr);
        const value out = create_same_type(c, arr, static_cast<double>(len));
        if (out.is_undefined()) { return out; }
        auto * made = static_cast<array_object *>(out.as_heap());
        for (std::size_t k = 0; k < len; ++k) {
            typed_array_set(made, k, as_number(typed_array_get(arr, len - 1 - k)));
        }
        return out;
    });
    // --- 23.2.3.26 set -----------------------------------------------------------
    method(cx, proto, "set", 1, typed_array_set_method);
    // --- 23.2.3.27 slice ---------------------------------------------------------
    method(cx, proto, "slice", 2, [](context & c, std::span<value> a) {
        array_object * arr = this_typed_array(c, "TypedArray.prototype.slice");
        if (arr == nullptr) { return value::undefined(); }
        const value self = c.current_this();
        double len = len_of(arr);
        double k = 0, final = len;
        if (!relative_arg(c, a, 0, len, 0, k) || !relative_arg(c, a, 1, len, len, final)) {
            return value::undefined();
        }
        double count = std::max(final - k, 0.0);
        const value args[1] = {value::number(count)};
        const value out = typed_array_species_create(c, arr, args);
        if (out.is_undefined()) { return out; }
        if (count == 0) { return out; }
        if (validate_typed_array(c, self, "TypedArray.prototype.slice") == nullptr) {
            return value::undefined();
        }
        len = len_of(arr);
        final = std::min(final, len);
        count = std::max(final - k, 0.0);
        auto * made = static_cast<array_object *>(out.as_heap());
        array_object * from = typed_array_store(arr);
        array_object * to = typed_array_store(made);
        if (from != nullptr && to != nullptr && made->elements == arr->elements) {
            const std::size_t width = bytes_per_element(arr->elements);
            copy_bytes(from, arr->byte_offset + static_cast<std::size_t>(k) * width, to,
                       made->byte_offset, static_cast<std::size_t>(count) * width);
            return out;
        }
        for (double n = 0; n < count; n += 1) {
            typed_array_set(made, static_cast<std::size_t>(n),
                            as_number(typed_array_get(arr, static_cast<std::size_t>(k + n))));
        }
        return out;
    });
    // --- 23.2.3.30 sort, 23.2.3.34 toSorted --------------------------------------
    const auto sorter = [&](const char * name, bool copy) {
        method(cx, proto, name, 1, [name, copy](context & c, std::span<value> a) {
            const std::string method_name = std::string{"TypedArray.prototype."} + name;
            const value comparator = arg_at(a, 0);
            if (!comparator.is_undefined() && !comparator.is_callable()) {
                c.throw_error("TypeError", method_name + ": comparator is not a function");
                return value::undefined();
            }
            array_object * arr = this_typed_array(c, method_name.c_str());
            if (arr == nullptr) { return value::undefined(); }
            const std::size_t len = typed_array_length(arr);
            value target = c.current_this();
            if (copy) {
                target = create_same_type(c, arr, static_cast<double>(len));
                if (target.is_undefined()) { return target; }
            }
            std::vector<double> work(len);
            for (std::size_t i = 0; i < len; ++i) { work[i] = as_number(typed_array_get(arr, i)); }
            if (!sort_numbers(c, work, comparator)) { return value::undefined(); }
            auto * into = static_cast<array_object *>(target.as_heap());
            for (std::size_t i = 0; i < len; ++i) { typed_array_set(into, i, work[i]); }
            return target;
        });
    };
    sorter("sort", false);
    sorter("toSorted", true);
    // --- 23.2.3.28 subarray -------------------------------------------------------
    method(cx, proto, "subarray", 2, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!is_typed_array(self)) {
            c.throw_error("TypeError", "TypedArray.prototype.subarray: this is not a typed array");
            return value::undefined();
        }
        auto * arr = static_cast<array_object *>(self.as_heap());
        // NOT ValidateTypedArray: a detached or out-of-bounds receiver has
        // length 0 here and the species constructor is what refuses it.
        const double len = typed_array_out_of_bounds(arr) ? 0 : len_of(arr);
        double begin = 0;
        if (!relative_arg(c, a, 0, len, 0, begin)) { return value::undefined(); }
        const auto width = static_cast<double>(bytes_per_element(arr->elements));
        const double begin_byte = static_cast<double>(arr->byte_offset) + begin * width;
        array_object * store = ensure_store(c, arr);
        if (store == nullptr) { return value::undefined(); }
        const value buffer = buffer_of_store(c, store);
        if (arg_at(a, 1).is_undefined() && typed_array_length_tracking(arr)) {
            const value args[2] = {buffer, value::number(begin_byte)};
            return typed_array_species_create(c, arr, args);
        }
        double final = len;
        if (!relative_arg(c, a, 1, len, len, final)) { return value::undefined(); }
        const double count = std::max(final - begin, 0.0);
        const value args[3] = {buffer, value::number(begin_byte), value::number(count)};
        const value out = typed_array_species_create(c, arr, args);
        // The constructor registered the view with the buffer; a subarray of
        // a fixed-length buffer does not need that (internal.hpp says why).
        if (!out.is_undefined() && !store_resizable(store)) {
            if (auto * made = static_cast<array_object *>(out.as_heap());
                typed_array_store(made) == store) {
                if (value * views = store->named ? store->named->find(store_views_key) : nullptr;
                    views != nullptr && views->is_array()) {
                    auto & list = static_cast<array_object *>(views->as_heap())->items;
                    if (!list.empty() && list.back().is_heap() && list.back().as_heap() == made) {
                        list.pop_back();
                    }
                }
            }
        }
        return out;
    });
    // --- 23.2.3.36 with -----------------------------------------------------------
    method(cx, proto, "with", 2, [](context & c, std::span<value> a) {
        array_object * arr = this_typed_array(c, "TypedArray.prototype.with");
        if (arr == nullptr) { return value::undefined(); }
        const double len = len_of(arr);
        double rel = 0;
        if (!to_integer_or_infinity(c, arg_at(a, 0), rel)) { return value::undefined(); }
        const double actual = rel >= 0 ? rel : len + rel;
        if (!numeric_arg(c, arg_at(a, 1))) { return value::undefined(); }
        const double v = c.to_number_value(arg_at(a, 1));
        if (c.throw_pending()) { return value::undefined(); }
        // IsValidIntegerIndex against the length NOW - the coercions ran script.
        if (!(actual >= 0) || actual >= len_of(arr) || typed_array_out_of_bounds(arr)) {
            c.throw_error("RangeError", "Invalid typed array index");
            return value::undefined();
        }
        const value out = create_same_type(c, arr, len);
        if (out.is_undefined()) { return out; }
        auto * made = static_cast<array_object *>(out.as_heap());
        for (double k = 0; k < len; k += 1) {
            typed_array_set(
                made, static_cast<std::size_t>(k),
                k == actual ? v : as_number(typed_array_get(arr, static_cast<std::size_t>(k))));
        }
        return out;
    });
}

} // namespace ctbrowser::script::builtins_detail
