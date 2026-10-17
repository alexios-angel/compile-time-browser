//===- Array.hpp - Native arrays of proved element type ---------*- C++ -*-===//
//
// ctnative::Array, whose prototype methods generated code calls on a
// `std::vector<T>` whose element type the compiler proved
// (`ctnative::Array.prototype.push.call(a, x)`), and the literal and index
// helpers the emitter uses for such arrays. Only programs that build one
// include this header. T is js_string or a std::pair row today for the literal
// and index helpers; the callback methods (forEach, map, filter, some, every,
// find, reduce) take any element type and any callable the compiler
// specialised for the call.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctnative.hpp"

#include <ctbrowser/core/algorithms.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ctnative {

/// Appends one element of an array literal, in source order.
inline void vec_push(std::vector<js_string> & v, js_string x) {
    v.push_back(std::move(x));
}

/// `a[i]` (ECMA-262 10.4.2, an Array's [[Get]] of an array index): the element
/// at an integral index below the length, else undefined.
inline nullable_string vec_at(const std::vector<js_string> & values, nullable_scalar key) {
    std::size_t index = 0;
    if (!element_index(key, values.size(), index)) { return {}; }
    return values[index].value();
}

/// `a[i]` of an array of Numbers (a map result): the element, else undefined.
inline nullable_scalar vec_at(std::span<const js_num> values, nullable_scalar key) {
    std::size_t index = 0;
    if (!element_index(key, values.size(), index)) { return {}; }
    return values[index];
}
inline nullable_scalar vec_at(const std::vector<js_num> & values, nullable_scalar key) {
    return vec_at(std::span<const js_num>{values}, key);
}

/// `a[i]` where the compiler proved `i` an integer in [0, a.length) (a loop
/// counter checked against this array's length; element arrays never shrink),
/// so the read is the element itself rather than an optional.
template <class T> const T & vec_element(const std::vector<T> & values, js_num index) {
    return values[static_cast<std::size_t>(index.value())];
}

/// `rows[i]` of an array of rows where the program goes on to read position
/// `reading` of the row (`rows[i][1]`): the row, or, when `i` names no
/// element, the TypeError that position read of undefined throws (ECMA-262
/// 7.1.18 ToObject), with the interpreter's message.
template <class T> const T & vec_row(std::span<const T> values, js_num key, int reading) {
    std::size_t index = 0;
    if (!element_index(nullable_scalar{key}, values.size(), index)) {
        throw type_error("Cannot read properties of undefined (reading '" +
                         std::to_string(reading) + "')");
    }
    return values[index];
}
template <class T> const T & vec_row(const std::vector<T> & values, js_num key, int reading) {
    return vec_row(std::span<const T>{values}, key, reading);
}

// The same reads on an array a function borrows as a read-only span (a
// parameter it only reads): length, `a[i]` and a proved-index element.

/// `a.length` of a borrowed array.
template <class T> js_num vec_length(std::span<const T> values) {
    return js_num{static_cast<double>(values.size())};
}

/// `a[i]` of a borrowed String array; see vec_at above.
inline nullable_string vec_at(std::span<const js_string> values, nullable_scalar key) {
    std::size_t index = 0;
    if (!element_index(key, values.size(), index)) { return {}; }
    return values[index].value();
}

/// `a[i]` of a borrowed array at a proved index; see vec_element above.
template <class T> const T & vec_element(std::span<const T> values, js_num index) {
    return values[static_cast<std::size_t>(index.value())];
}

namespace detail {
/// The UTF-16 code units of WTF-8 text, one at a time, without converting the
/// whole string.
class utf16_units {
    std::string_view text;
    std::size_t at = 0;
    char16_t pending = 0;

public:
    explicit utf16_units(std::string_view wtf8) : text(wtf8) {}
    /// The next unit, or false at the end.
    bool next(char16_t & unit) {
        if (pending != 0) {
            unit = std::exchange(pending, char16_t{0});
            return true;
        }
        if (at >= text.size()) { return false; }
        char32_t cp = ctbrowser::decode_utf8(text, at);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            unit = static_cast<char16_t>(0xD800 + (cp >> 10));
            pending = static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
        } else {
            unit = static_cast<char16_t>(cp);
        }
        return true;
    }
};

/// IsLessThan on two Strings (ECMA-262 7.2.13 step 3): code-unit order over
/// UTF-16, which is not byte order over WTF-8 once a code point above U+FFFF
/// (high surrogate D800-DBFF) meets one in U+E000-U+FFFF.
inline bool string_less(const js_string & left, const js_string & right) {
    utf16_units a{left.value()}, b{right.value()};
    for (;;) {
        char16_t x = 0, y = 0;
        const bool hasX = a.next(x), hasY = b.next(y);
        if (!hasX || !hasY) { return !hasX && hasY; }
        if (x != y) { return x < y; }
    }
}
/// The receivers the Array methods take: an owned array, or one borrowed as a
/// read-only span (a parameter the callee only reads).
template <class Self> struct is_native_array : std::false_type {};
template <class T> struct is_native_array<std::vector<T>> : std::true_type {};
template <class T> struct is_native_array<std::span<const T>> : std::true_type {};
template <class Self>
concept native_array = is_native_array<Self>::value;

/// Calls an Array method's callback with as many of (element, index, array)
/// as it takes. The compiler specialises each callback for its call, and a
/// callback that ignores its trailing parameters is not given them.
template <class Callback, class Self, class T = typename Self::value_type>
decltype(auto) call_back(Callback & callback, const T & element, std::size_t index,
                         const Self & self) {
    const js_num k{static_cast<double>(index)};
    if constexpr (std::invocable<Callback &, const T &, js_num, const Self &>) {
        return std::invoke(callback, element, k, self);
    } else if constexpr (std::invocable<Callback &, const T &, js_num>) {
        return std::invoke(callback, element, k);
    } else {
        return std::invoke(callback, element);
    }
}

/// Fails when a callback has shortened the array below `index`, for the
/// methods whose answer would then need a value a native array cannot hold.
template <class Self>
void require_index(const Self & self, std::size_t index, const char * method) {
    if (index >= self.size()) {
        throw std::logic_error(std::string{"Array.prototype."} + method +
                               "'s callback shortened the array, which the compiler must "
                               "not admit");
    }
}

/// ToBoolean (ECMA-262 7.1.2) of a callback's result: every native carrier's
/// explicit bool conversion is ToBoolean for its type.
template <class Result> bool to_boolean(const Result & result) {
    return static_cast<bool>(result);
}

/// What `find` answers for an element type: the type's nullable carrier where
/// one exists (undefined when nothing matched), std::optional otherwise.
template <class T> struct find_result {
    using type = std::optional<T>;
};
template <> struct find_result<js_string> {
    using type = nullable_string;
};
template <> struct find_result<js_num> {
    using type = nullable_scalar;
};
template <> struct find_result<js_boolean_t> {
    using type = nullable_scalar;
};
template <class T> typename find_result<T>::type found(const T & element) {
    if constexpr (std::is_same_v<T, js_string>) {
        return nullable_string{element.value()};
    } else {
        return typename find_result<T>::type{element};
    }
}
} // namespace detail

// The callback methods share the iteration of ECMA-262 23.1.3: the length is
// read once, before the first call; an index the array no longer has (a
// callback shortened it) is skipped, as HasProperty would; each element is
// read, and copied, before its callback runs; the index is a Number; thisArg
// is undefined, since the compiler specialises only callbacks that do not read
// `this`. Each takes an owned array or a borrowed std::span; on a span the
// lowering also proves the callback leaves the viewed array unchanged, since a
// span cannot see it grow or shrink. Each takes the callback as a template
// parameter and never stores it, so a callback that captures by reference is
// safe.

/// Array.prototype.forEach (ECMA-262 23.1.3.15): calls `callback` on each
/// element and answers undefined.
struct for_each_method {
    template <class Self, class Callback, class T = typename Self::value_type>
    requires detail::native_array<Self>
    undefined_t call(const Self & self, Callback callback) const {
        const std::size_t length = self.size();
        for (std::size_t k = 0; k < length && k < self.size(); ++k) {
            const T element = self[k];
            (void)detail::call_back(callback, element, k, self);
        }
        return {};
    }
};

/// Array.prototype.map (ECMA-262 23.1.3.21): the callback's result for each
/// element, in a new array of the same length.
///
/// Precondition, which the lowering proves before it emits this call: the
/// callback does not shorten `self`. An index it removed would be a hole in
/// the result, which a native array cannot hold; a call that breaks the
/// precondition throws std::logic_error rather than answer differently from
/// the VM.
struct map_method {
    template <class Self, class Callback, class T = typename Self::value_type>
    requires detail::native_array<Self>
    auto call(const Self & self, Callback callback) const {
        using Result = std::remove_cvref_t<decltype(detail::call_back(
            callback, std::declval<const T &>(), std::size_t{}, self))>;
        std::vector<Result> out;
        const std::size_t length = self.size();
        out.reserve(length);
        for (std::size_t k = 0; k < length; ++k) {
            detail::require_index(self, k, "map");
            const T element = self[k];
            out.push_back(detail::call_back(callback, element, k, self));
        }
        return out;
    }
};

/// Array.prototype.filter (ECMA-262 23.1.3.10): the elements whose callback
/// result is truthy, as they were read before the call.
struct filter_method {
    template <class Self, class Callback, class T = typename Self::value_type>
    requires detail::native_array<Self>
    std::vector<T> call(const Self & self, Callback callback) const {
        std::vector<T> out;
        const std::size_t length = self.size();
        for (std::size_t k = 0; k < length && k < self.size(); ++k) {
            T element = self[k];
            if (detail::to_boolean(detail::call_back(callback, element, k, self))) {
                out.push_back(std::move(element));
            }
        }
        return out;
    }
};

/// Array.prototype.some (ECMA-262 23.1.3.29): whether any element's callback
/// result is truthy, stopping at the first.
struct some_method {
    template <class Self, class Callback, class T = typename Self::value_type>
    requires detail::native_array<Self>
    js_boolean_t call(const Self & self, Callback callback) const {
        const std::size_t length = self.size();
        for (std::size_t k = 0; k < length && k < self.size(); ++k) {
            const T element = self[k];
            if (detail::to_boolean(detail::call_back(callback, element, k, self))) {
                return js_boolean_t{true};
            }
        }
        return js_boolean_t{false};
    }
};

/// Array.prototype.every (ECMA-262 23.1.3.6): whether every element's
/// callback result is truthy, stopping at the first that is not. True for no
/// elements.
struct every_method {
    template <class Self, class Callback, class T = typename Self::value_type>
    requires detail::native_array<Self>
    js_boolean_t call(const Self & self, Callback callback) const {
        const std::size_t length = self.size();
        for (std::size_t k = 0; k < length && k < self.size(); ++k) {
            const T element = self[k];
            if (!detail::to_boolean(detail::call_back(callback, element, k, self))) {
                return js_boolean_t{false};
            }
        }
        return js_boolean_t{true};
    }
};

/// Array.prototype.find (ECMA-262 23.1.3.9): the first element whose callback
/// result is truthy, or undefined. Unlike the methods above, find calls the
/// callback for every index below the length taken at the start, with
/// undefined for one the array no longer has.
///
/// Precondition, which the lowering proves before it emits this call: the
/// callback does not shorten `self`, since a native array of T cannot pass
/// undefined as a T. A call that breaks it throws std::logic_error rather
/// than answer differently from the VM.
struct find_method {
    template <class Self, class Callback, class T = typename Self::value_type>
    requires detail::native_array<Self>
    typename detail::find_result<T>::type call(const Self & self, Callback callback) const {
        const std::size_t length = self.size();
        for (std::size_t k = 0; k < length; ++k) {
            detail::require_index(self, k, "find");
            const T element = self[k];
            if (detail::to_boolean(detail::call_back(callback, element, k, self))) {
                return detail::found(element);
            }
        }
        return {};
    }
};

/// Array.prototype.reduce (ECMA-262 23.1.3.24) with an initial value: the
/// accumulator threaded through the callback over each element. The compiler
/// refuses a reduce with no initial value, which throws on an empty array.
struct reduce_method {
    template <class Self, class Callback, class Accumulator, class T = typename Self::value_type>
    requires detail::native_array<Self>
    Accumulator call(const Self & self, Callback callback, Accumulator accumulator) const {
        const std::size_t length = self.size();
        for (std::size_t k = 0; k < length && k < self.size(); ++k) {
            const T element = self[k];
            const js_num index{static_cast<double>(k)};
            if constexpr (std::invocable<Callback &, Accumulator, const T &, js_num,
                                         const Self &>) {
                accumulator = std::invoke(callback, std::move(accumulator), element, index, self);
            } else if constexpr (std::invocable<Callback &, Accumulator, const T &, js_num>) {
                accumulator = std::invoke(callback, std::move(accumulator), element, index);
            } else {
                accumulator = std::invoke(callback, std::move(accumulator), element);
            }
        }
        return accumulator;
    }
};

/// Array.prototype.push (ECMA-262 23.1.3.23) with one argument: appends it and
/// returns the new length.
struct push_method {
    template <class T> js_num call(std::vector<T> & self, T value) const {
        self.push_back(std::move(value));
        return js_num{static_cast<double>(self.size())};
    }
};

/// Array.prototype.join (ECMA-262 23.1.3.18) of Strings: the elements with
/// `separator` between them, "" for no elements. Lone surrogates that meet at
/// a seam pair up, as String concatenation does. The emitter passes "," for
/// a join with no argument (step 3).
struct join_method {
    js_string call(const std::vector<js_string> & self, const js_string & separator) const {
        return call(std::span<const js_string>{self}, separator);
    }
    /// The same on a borrowed array.
    js_string call(std::span<const js_string> self, const js_string & separator) const {
        std::string out;
        for (std::size_t i = 0; i < self.size(); ++i) {
            if (i != 0) { out += separator.value(); }
            out += self[i].value();
        }
        ctbrowser::join_surrogates(out);
        return js_string{std::move(out)};
    }
};

/// Array.prototype.sort (ECMA-262 23.1.3.30) of Strings with no comparator:
/// SortCompare's String order (UTF-16 code units), stable as the spec
/// requires. Sorts in place and answers the array, as `a.sort()` answers `a`.
struct sort_method {
    std::vector<js_string> & call(std::vector<js_string> & self) const {
        std::ranges::stable_sort(self, detail::string_less);
        return self;
    }
};

/// Array.prototype.includes (ECMA-262 23.1.3.16) with one argument: whether
/// an element is SameValueZero to `value`. The compiler lowers it on arrays
/// of Strings, for which that is equality of the code units.
struct includes_method {
    template <class Self, class T = typename Self::value_type>
    requires detail::native_array<Self>
    js_boolean_t call(const Self & self, const T & value) const {
        return js_boolean_t{std::ranges::find(self, value) != self.end()};
    }
};

/// The Array.prototype methods native code calls, as stateless method
/// objects: generated code reads `ctnative::Array.prototype.push.call(a, x)`.
/// Only the methods the compiler lowers are here.
struct array_prototype {
    push_method push;
    join_method join;
    sort_method sort;
    includes_method includes;
    for_each_method forEach;
    map_method map;
    filter_method filter;
    some_method some;
    every_method every;
    find_method find;
    reduce_method reduce;
};
/// The Array constructor's members. `Array(n)` and `new Array` are not
/// lowered, so it has no call operator.
struct array_constructor {
    array_prototype prototype;
};
inline constexpr array_constructor Array{};

} // namespace ctnative
