//===- ctnative.hpp - Runtime for generated native code ---------*- C++ -*-===//
//
// The runtime every generated native translation unit includes: value
// carriers for optional scalars, strings and unions, dense arrays, Maps,
// method tables and (with CTNATIVE_DOM) the DOM entry helpers. It is compiled
// on its own under -Werror by ctcompile/test/Runtime/NativeRuntime.cpp, so
// errors in a helper show up before any generated program uses it.
//
// Two switches, both defined by the emitter before the include, because they
// are the two things a program decides and a header cannot:
//
//   CTNATIVE_ORDERED_MAPS  every Map keeps insertion order (a snapshot is
//                          observable somewhere in the program); otherwise
//                          lookup is `std::map` with SameValueZero keys
//   CTNATIVE_DOM           the program is a DOM entry and ctbrowser's public
//                          DOM headers are on its include path. Primitive String
//                          conversion/formatting links ctbrowser's public Core
//                          library even without it.
//
// Never reference a ctbrowser::script symbol here. Script/ is the interpreter,
// used only as a development-time reference to compare against, and a native
// program must not depend on it.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTNative/Runtime/Error.hpp"
#include "ctcompile/CTNative/Runtime/Math.hpp"
#include "ctcompile/CTNative/Runtime/Number.hpp"
#include "ctcompile/CTNative/Runtime/String.hpp"
#include "ctcompile/CTNative/Runtime/Symbol.hpp"

#include <ctbrowser/core/algorithms.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#ifdef CTNATIVE_DOM
#include <algorithm>
#include <array>
#include <charconv>
#include <expected>
#include <limits>
#include <stdexcept>

#include <ctbrowser/core/json.hpp>
#include <ctbrowser/core/number_format.hpp>
#include <ctbrowser/core/uri.hpp>
#include <ctbrowser/dom/dataset.hpp>
#include <ctbrowser/dom/element.hpp>
#include <ctbrowser/dom/token_list.hpp>
#include <ctbrowser/style/css/parser.hpp>
#endif

/// A plain double, for generated arithmetic and signatures that do not yet use
/// the ctnative::js_num class.
using js_num = double;

namespace ctnative {

/// JavaScript string concatenation of two WTF-8 strings (joins surrogate halves
/// split across the boundary).
inline std::string string_concat(std::string left, std::string_view right) {
    return (js_string{std::move(left)} + js_string{right}).value();
}

// --- exceptions --------------------------------------------------------------

/// What a native `throw` of a primitive of type T throws, so a native catch
/// can match JavaScript exceptions without catching foreign C++ ones.
template <class T> struct js_exception {
    T value;
};

// --- optional scalars --------------------------------------------------------
//
// Optional scalars and scalar-only unions share one carrier, nullable_scalar,
// whose run-time tag records which JavaScript value arrived. A present NaN is
// never used to mean absence.

struct undefined_t {};
struct js_null_t {};

/// A JavaScript Boolean. Conversion to a C++ `bool` or to a number is
/// explicit, so a Boolean never silently becomes 0 or 1.
class js_boolean_t {
    bool value = false;

public:
    constexpr js_boolean_t() = default;
    template <class T>
    requires std::is_same_v<T, bool>
    explicit constexpr js_boolean_t(T boolean) : value(boolean) {}
    explicit constexpr operator bool() const { return value; }
    constexpr js_num to_number() const { return js_num{value ? 1.0 : 0.0}; }
    friend constexpr bool operator==(js_boolean_t, js_boolean_t) = default;
};
inline constexpr js_num to_number(js_boolean_t value) {
    return value.to_number();
}

/// An optional Number or Boolean that keeps null, undefined and a present NaN
/// distinct.
struct nullable_scalar {
    enum class kind {
        undefined,
        null,
        number,
        boolean
    } tag = kind::undefined;
    double value = 0;
    nullable_scalar() = default;
    nullable_scalar(undefined_t) {}
    nullable_scalar(js_null_t) : tag(kind::null) {}
    nullable_scalar(double number) : tag(kind::number), value(number) {}
    nullable_scalar(js_num number) : nullable_scalar(number.value()) {}
    nullable_scalar(js_boolean_t boolean)
        : tag(kind::boolean), value(boolean.to_number().value()) {}
    nullable_scalar(bool boolean) : nullable_scalar(js_boolean_t{boolean}) {}
    js_string to_string() const {
        switch (tag) {
        case kind::undefined: return js_string{"undefined"};
        case kind::null: return js_string{"null"};
        case kind::number: return js_string{ctbrowser::number_to_string(value)};
        case kind::boolean: return js_string{} + js_boolean_t{value != 0.0};
        }
        std::terminate();
    }
    static nullable_scalar null() { return js_null_t{}; }
};
inline nullable_scalar to_nullable(nullable_scalar value) {
    return value;
}
inline js_num to_number(nullable_scalar value) {
    if (value.tag == nullable_scalar::kind::undefined) {
        return js_num{js_nan_t{}};
    }
    if (value.tag == nullable_scalar::kind::null) { return js_num{}; }
    return js_num{value.value};
}
/// The Number in a global the compiler proved holds one. Terminates on any
/// other tag, so a missing generated store cannot pass for NaN.
inline js_num global_number(nullable_scalar value) {
    if (value.tag != nullable_scalar::kind::number) { std::terminate(); }
    return js_num{value.value};
}
inline js_boolean_t global_boolean(nullable_scalar value) {
    if (value.tag != nullable_scalar::kind::boolean) { std::terminate(); }
    return js_boolean_t{value.value != 0.0};
}
inline void print_scalar(const char * name, nullable_scalar value) {
    switch (value.tag) {
    case nullable_scalar::kind::undefined: std::printf("%s=undefined\n", name); return;
    case nullable_scalar::kind::null: std::printf("%s=null\n", name); return;
    case nullable_scalar::kind::number: std::printf("%s=%.17g\n", name, value.value); return;
    case nullable_scalar::kind::boolean:
        std::printf("%s=%s\n", name, value.value != 0.0 ? "true" : "false");
        return;
    }
    std::terminate();
}
inline bool scalar_truthy(nullable_scalar value) {
    return (value.tag == nullable_scalar::kind::number ||
            value.tag == nullable_scalar::kind::boolean) &&
           value.value != 0.0 && !std::isnan(value.value);
}
inline js_boolean_t scalar_strict_equal(nullable_scalar left, nullable_scalar right) {
    if (left.tag != right.tag) { return js_boolean_t{false}; }
    if (left.tag == nullable_scalar::kind::undefined || left.tag == nullable_scalar::kind::null) {
        return js_boolean_t{true};
    }
    return js_boolean_t{left.value == right.value};
}
inline js_boolean_t scalar_equal(nullable_scalar left, nullable_scalar right) {
    const bool lnull =
        left.tag == nullable_scalar::kind::undefined || left.tag == nullable_scalar::kind::null;
    const bool rnull =
        right.tag == nullable_scalar::kind::undefined || right.tag == nullable_scalar::kind::null;
    if (lnull || rnull) { return js_boolean_t{lnull && rnull}; }
    return js_boolean_t{left.value == right.value};
}
inline std::string scalar_typeof(nullable_scalar value) {
    switch (value.tag) {
    case nullable_scalar::kind::undefined: return "undefined";
    case nullable_scalar::kind::null: return "object";
    case nullable_scalar::kind::boolean: return "boolean";
    case nullable_scalar::kind::number: return "number";
    }
    std::terminate();
}

// --- owning strings ----------------------------------------------------------

/// An optional owned string that keeps null and undefined distinct.
struct nullable_string {
    enum class kind {
        undefined,
        null_value,
        string
    };
    kind tag = kind::undefined;
    std::string value;
    nullable_string() = default;
    nullable_string(const std::string & text) : tag(kind::string), value(text) {}
    js_num to_number() const {
        switch (tag) {
        case kind::undefined: return js_num{js_nan_t{}};
        case kind::null_value: return js_num{};
        case kind::string: return js_num{ctbrowser::string_to_number(value)};
        }
        std::terminate();
    }
};
inline nullable_string to_nullable_string(const std::string & value) {
    return value;
}
inline nullable_string to_nullable_string(const js_string & value) {
    return value.value();
}
inline nullable_string to_nullable_string(const nullable_string & value) {
    return value;
}
inline nullable_string to_nullable_string(nullable_scalar value) {
    nullable_string out;
    if (value.tag == nullable_scalar::kind::null) {
        out.tag = nullable_string::kind::null_value;
    } else if (value.tag != nullable_scalar::kind::undefined) {
        // The admission check only lets an absent scalar widen into this
        // carrier.
        std::terminate();
    }
    return out;
}
/// `text[index]` for a Number index (ECMA-262 10.4.3.5, StringGetOwnProperty):
/// the one UTF-16 code unit at a canonical integer index below the length, as
/// a one-unit string (half of a surrogate pair comes back as that lone
/// surrogate's WTF-8), else undefined. -0 names index 0,
/// as ToString(-0) is "0"; NaN, fractions, negatives and out-of-range indices
/// name no element.
inline nullable_string string_at(const js_string & text, js_num index) {
    const double at = index.value();
    if (!(at >= 0.0) || at != std::floor(at) ||
        at >= static_cast<double>(string_length(text.value()))) {
        return {};
    }
    const std::u16string units = ctbrowser::wtf8_to_utf16(text.value());
    return ctbrowser::utf16_to_wtf8(
        std::u16string_view{units}.substr(static_cast<std::size_t>(at), 1));
}
inline bool string_truthy(const nullable_string & value) {
    return value.tag == nullable_string::kind::string && !value.value.empty();
}
inline std::string string_typeof(const nullable_string & value) {
    return value.tag == nullable_string::kind::undefined    ? "undefined"
           : value.tag == nullable_string::kind::null_value ? "object"
                                                            : "string";
}
inline std::string string_text(const nullable_string & value) {
    return value.tag == nullable_string::kind::undefined    ? "undefined"
           : value.tag == nullable_string::kind::null_value ? "null"
                                                            : value.value;
}
/// The String in a global the compiler proved holds one. Terminates on any
/// other tag rather than printing it, and returns a copy so later stores
/// cannot change the saved value.
inline std::string global_string(const nullable_string & value) {
    if (value.tag != nullable_string::kind::string) { std::terminate(); }
    return value.value;
}
inline void print_string(const char * name, const std::string & value) {
    std::printf("%s=\"", name);
    static constexpr char hex[] = "0123456789ABCDEF";
    for (const char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            std::string_view("-._~").contains(raw)) {
            std::putchar(c);
        } else {
            std::putchar('%');
            std::putchar(hex[c >> 4]);
            std::putchar(hex[c & 15]);
        }
    }
    std::printf("\"\n");
}
inline void print_scalar(const char * name, const nullable_string & value) {
    switch (value.tag) {
    case nullable_string::kind::undefined: std::printf("%s=undefined\n", name); return;
    case nullable_string::kind::null_value: std::printf("%s=null\n", name); return;
    case nullable_string::kind::string: print_string(name, value.value); return;
    }
    std::terminate();
}
template <class L, class R> js_boolean_t string_strict_equal(const L & left, const R & right) {
    const auto a = to_nullable_string(left), b = to_nullable_string(right);
    return js_boolean_t{a.tag == b.tag &&
                        (a.tag != nullable_string::kind::string || a.value == b.value)};
}
template <class L, class R> js_boolean_t string_equal(const L & left, const R & right) {
    const auto a = to_nullable_string(left), b = to_nullable_string(right);
    if (a.tag != nullable_string::kind::string && b.tag != nullable_string::kind::string) {
        return js_boolean_t{true};
    }
    return string_strict_equal(a, b);
}
// Only Maps keyed by nullable strings need these comparisons. Both compare the
// tag before the text, so null and undefined keys never equal their spelling
// or the empty string.
inline bool operator==(const nullable_string & a, const nullable_string & b) {
    return a.tag == b.tag && (a.tag != nullable_string::kind::string || a.value == b.value);
}
inline bool operator<(const nullable_string & a, const nullable_string & b) {
    if (a.tag != b.tag) { return a.tag < b.tag; }
    return a.tag == nullable_string::kind::string && a.value < b.value;
}

// --- Boolean/Number/String variant carriers ----------------------------------
//
// Each alternative is either string text or a scalar nullable_scalar can
// carry. Each operation (text, truthiness, typeof, ToNumber, print) is written
// once in `detail` over any such variant; the named helpers below are the
// spellings generated code calls.
namespace detail {
template <class T, class... U>
concept one_of = (std::is_same_v<T, U> || ...);
template <class T> constexpr bool is_absent = one_of<T, undefined_t, js_null_t>;

template <class... T, class Text, class Scalar>
auto visit_carrier(const std::variant<T...> & value, Text text, Scalar scalar) {
    return std::visit(
        [&](const auto & alternative) {
            using A = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<A, js_string>) {
                return text(alternative.value());
            } else if constexpr (std::is_same_v<A, std::string>) {
                return text(alternative);
            } else {
                return scalar(nullable_scalar{alternative});
            }
        },
        value);
}
template <class... T> js_string variant_text(const std::variant<T...> & value) {
    return visit_carrier(
        value, [](const std::string & text) { return js_string{text}; },
        [](nullable_scalar scalar) { return scalar.to_string(); });
}
template <class... T> bool variant_truthy(const std::variant<T...> & value) {
    return visit_carrier(
        value, [](const std::string & text) { return !text.empty(); }, scalar_truthy);
}
template <class... T> std::string variant_typeof(const std::variant<T...> & value) {
    return visit_carrier(
        value, [](const std::string &) { return std::string{"string"}; }, scalar_typeof);
}
template <class... T> js_num variant_number(const std::variant<T...> & value) {
    return visit_carrier(
        value, [](const std::string & text) { return js_num{ctbrowser::string_to_number(text)}; },
        [](nullable_scalar scalar) { return to_number(scalar); });
}
template <class... T> void print_variant(const char * name, const std::variant<T...> & value) {
    visit_carrier(
        value, [name](const std::string & text) { print_string(name, text); },
        [name](nullable_scalar scalar) { print_scalar(name, scalar); });
}
// Widening keeps every alternative; absence stays its own alternative.
template <class R, class... T> R widen(const std::variant<T...> & value) {
    return std::visit([](const auto & alternative) -> R { return alternative; }, value);
}
template <class R> R widen(const nullable_string & value) {
    switch (value.tag) {
    case nullable_string::kind::undefined: return undefined_t{};
    case nullable_string::kind::null_value: return js_null_t{};
    case nullable_string::kind::string: return js_string{value.value};
    }
    std::terminate();
}
// The alternative A of a global the compiler proved holds one; terminates
// otherwise. Absence here means a missing generated store, never a value to
// coerce.
template <class A, class... T> const A & present(const std::variant<T...> & value) {
    if (const auto * alternative = std::get_if<A>(&value)) { return *alternative; }
    std::terminate();
}
template <class R, class... T> R present_carrier(const std::variant<T...> & value) {
    return std::visit(
        [](const auto & alternative) -> R {
            if constexpr (is_absent<std::decay_t<decltype(alternative)>>) {
                std::terminate();
            } else {
                return alternative;
            }
        },
        value);
}
} // namespace detail

inline bool boolean_string_truthy(const std::variant<js_boolean_t, std::string> & value) {
    return detail::variant_truthy(value);
}
inline js_string boolean_string_text(const std::variant<js_boolean_t, std::string> & value) {
    return detail::variant_text(value);
}
inline js_num to_number(const std::variant<js_boolean_t, std::string> & value) {
    return detail::variant_number(value);
}

/// A value proved to be a Number or a String.
using number_string = std::variant<js_num, js_string>;

inline constexpr js_num to_number(js_num value) {
    return value;
}
inline js_num to_number(const js_string & value) {
    return value.to_number();
}
template <class T>
requires(detail::is_absent<T>)
inline js_num to_number(T value) {
    return to_number(nullable_scalar{value});
}
inline js_num to_number(const number_string & value) {
    return detail::variant_number(value);
}
inline js_string number_string_text(const number_string & value) {
    return detail::variant_text(value);
}
inline bool number_string_truthy(const number_string & value) {
    return detail::variant_truthy(value);
}
inline std::string number_string_typeof(const number_string & value) {
    return std::holds_alternative<js_num>(value) ? "number" : "string";
}
/// Unwraps a global stored as std::optional<number_string>. Terminates if the
/// global was never stored, because the compiler proved it present.
inline number_string global_number_string(const std::optional<number_string> & value) {
    if (!value) { std::terminate(); }
    return *value;
}
inline void print_number_string(const char * name, const number_string & value) {
    detail::print_variant(name, value);
}

/// A Number or String that may also be null or undefined (for example a global
/// read before its store). Absence is its own alternative, never NaN or an
/// empty string.
using nullable_number_string = std::variant<undefined_t, js_null_t, js_num, js_string>;

inline nullable_number_string to_nullable_number_string(const number_string & value) {
    return detail::widen<nullable_number_string>(value);
}
inline nullable_number_string to_nullable_number_string(nullable_scalar value) {
    switch (value.tag) {
    case nullable_scalar::kind::undefined: return undefined_t{};
    case nullable_scalar::kind::null: return js_null_t{};
    case nullable_scalar::kind::number: return js_num{value.value};
    case nullable_scalar::kind::boolean: std::terminate();
    }
    std::terminate();
}
inline nullable_number_string to_nullable_number_string(const nullable_string & value) {
    return detail::widen<nullable_number_string>(value);
}
inline js_num to_number(const nullable_number_string & value) {
    return detail::variant_number(value);
}
inline js_string nullable_number_string_text(const nullable_number_string & value) {
    return detail::variant_text(value);
}
inline bool nullable_number_string_truthy(const nullable_number_string & value) {
    return detail::variant_truthy(value);
}
inline std::string nullable_number_string_typeof(const nullable_number_string & value) {
    return detail::variant_typeof(value);
}
inline void print_nullable_number_string(const char * name, const nullable_number_string & value) {
    detail::print_variant(name, value);
}
inline number_string global_number_string(const nullable_number_string & value) {
    return detail::present_carrier<number_string>(value);
}
inline js_num global_number(const nullable_number_string & value) {
    return detail::present<js_num>(value);
}
inline std::string global_string(const nullable_number_string & value) {
    return detail::present<js_string>(value).value();
}

using boolean_string = std::variant<js_boolean_t, js_string>;
using nullable_boolean_string = std::variant<undefined_t, js_null_t, js_boolean_t, js_string>;
namespace detail {
// The four Number/Boolean-or-String unions, each a std::variant holding js_string.
template <class T>
concept primitive_union =
    one_of<T, number_string, nullable_number_string, boolean_string, nullable_boolean_string>;
} // namespace detail

inline js_num to_number(const boolean_string & value) {
    return detail::variant_number(value);
}
inline js_string boolean_string_text(const boolean_string & value) {
    return detail::variant_text(value);
}
inline bool boolean_string_truthy(const boolean_string & value) {
    return detail::variant_truthy(value);
}
inline std::string boolean_string_typeof(const boolean_string & value) {
    return std::holds_alternative<js_boolean_t>(value) ? "boolean" : "string";
}
inline void print_boolean_string(const char * name, const boolean_string & value) {
    detail::print_variant(name, value);
}
inline nullable_boolean_string to_nullable_boolean_string(const boolean_string & value) {
    return detail::widen<nullable_boolean_string>(value);
}
inline nullable_boolean_string to_nullable_boolean_string(nullable_scalar value) {
    switch (value.tag) {
    case nullable_scalar::kind::undefined: return undefined_t{};
    case nullable_scalar::kind::null: return js_null_t{};
    case nullable_scalar::kind::boolean: return js_boolean_t{value.value != 0.0};
    case nullable_scalar::kind::number: std::terminate();
    }
    std::terminate();
}
inline nullable_boolean_string to_nullable_boolean_string(const nullable_string & value) {
    return detail::widen<nullable_boolean_string>(value);
}
inline js_num to_number(const nullable_boolean_string & value) {
    return detail::variant_number(value);
}
inline js_string nullable_boolean_string_text(const nullable_boolean_string & value) {
    return detail::variant_text(value);
}
inline bool nullable_boolean_string_truthy(const nullable_boolean_string & value) {
    return detail::variant_truthy(value);
}
inline std::string nullable_boolean_string_typeof(const nullable_boolean_string & value) {
    return detail::variant_typeof(value);
}
inline void print_nullable_boolean_string(const char * name,
                                          const nullable_boolean_string & value) {
    detail::print_variant(name, value);
}
inline boolean_string global_boolean_string(const nullable_boolean_string & value) {
    return detail::present_carrier<boolean_string>(value);
}
inline js_boolean_t global_boolean(const nullable_boolean_string & value) {
    return detail::present<js_boolean_t>(value);
}
inline std::string global_string(const nullable_boolean_string & value) {
    return detail::present<js_string>(value).value();
}

template <class T>
concept primitive_add_operand =
    detail::one_of<T, js_num, js_boolean_t, nullable_scalar, js_string, nullable_string,
                   std::variant<js_boolean_t, std::string>> ||
    detail::primitive_union<T>;

template <primitive_add_operand L, primitive_add_operand R>
number_string add(const L & left, const R & right) {
    const auto isString = []<class T>(const T & value) {
        if constexpr (std::is_same_v<T, js_string>) {
            return true;
        } else if constexpr (std::is_same_v<T, nullable_string>) {
            return value.tag == nullable_string::kind::string;
        } else if constexpr (std::is_same_v<T, std::variant<js_boolean_t, std::string>>) {
            return std::holds_alternative<std::string>(value);
        } else if constexpr (detail::primitive_union<T>) {
            return std::holds_alternative<js_string>(value);
        } else {
            return false;
        }
    };
    const auto text = []<class T>(const T & value) {
        if constexpr (std::is_same_v<T, js_string>) {
            return value;
        } else if constexpr (std::is_same_v<T, nullable_scalar>) {
            return value.to_string();
        } else if constexpr (std::is_same_v<T, nullable_string>) {
            return js_string{string_text(value)};
        } else if constexpr (std::is_same_v<T, js_num> || std::is_same_v<T, js_boolean_t>) {
            return js_string{} + value;
        } else {
            return detail::variant_text(value);
        }
    };
    const auto number = []<class T>(const T & value) {
        if constexpr (std::is_same_v<T, js_string> || std::is_same_v<T, nullable_string>) {
            return value.to_number();
        } else {
            return to_number(value);
        }
    };
    if (isString(left) || isString(right)) { return text(left) + text(right); }
    return number(left) + number(right);
}

template <class T>
concept primitive_equality_operand =
    detail::one_of<T, js_num, js_boolean_t, js_string, nullable_scalar, nullable_string,
                   undefined_t, js_null_t> ||
    detail::primitive_union<T>;

namespace detail {
// Borrows the string storage just for this comparison; other alternatives
// keep their scalar tags. No combined carrier or string copy is needed.
template <primitive_equality_operand T, class F>
js_boolean_t visit_equality_operand(const T & value, F && visitor) {
    if constexpr (std::is_same_v<T, js_string>) {
        return visitor(value.value());
    } else if constexpr (std::is_same_v<T, nullable_string>) {
        switch (value.tag) {
        case nullable_string::kind::undefined: return visitor(nullable_scalar{});
        case nullable_string::kind::null_value: return visitor(nullable_scalar::null());
        case nullable_string::kind::string: return visitor(value.value);
        }
        std::terminate();
    } else if constexpr (primitive_union<T>) {
        return std::visit(
            [&](const auto & alternative) { return visit_equality_operand(alternative, visitor); },
            value);
    } else {
        return visitor(nullable_scalar{value});
    }
}
} // namespace detail

template <primitive_equality_operand L, primitive_equality_operand R>
js_boolean_t primitive_strict_equal(const L & left, const R & right) {
    return detail::visit_equality_operand(left, [&](const auto & a) {
        return detail::visit_equality_operand(right, [&](const auto & b) {
            using A = std::decay_t<decltype(a)>;
            using B = std::decay_t<decltype(b)>;
            if constexpr (!std::is_same_v<A, B>) {
                return js_boolean_t{false};
            } else if constexpr (std::is_same_v<A, std::string>) {
                return js_boolean_t{a == b};
            } else {
                return scalar_strict_equal(a, b);
            }
        });
    });
}

template <primitive_equality_operand L, primitive_equality_operand R>
js_boolean_t primitive_equal(const L & left, const R & right) {
    return detail::visit_equality_operand(left, [&](const auto & a) {
        return detail::visit_equality_operand(right, [&](const auto & b) {
            constexpr bool leftString = std::is_same_v<std::decay_t<decltype(a)>, std::string>;
            constexpr bool rightString = std::is_same_v<std::decay_t<decltype(b)>, std::string>;
            if constexpr (leftString && rightString) {
                return js_boolean_t{a == b};
            } else if constexpr (!leftString && !rightString) {
                return scalar_equal(a, b);
            } else {
                const auto numericTextEqual = [](const std::string & text, nullable_scalar scalar) {
                    if (scalar.tag == nullable_scalar::kind::undefined ||
                        scalar.tag == nullable_scalar::kind::null) {
                        return js_boolean_t{false};
                    }
                    return js_boolean_t{ctbrowser::string_to_number(text) ==
                                        to_number(scalar).value()};
                };
                if constexpr (leftString) {
                    return numericTextEqual(a, b);
                } else {
                    return numericTextEqual(b, a);
                }
            }
        });
    });
}

// --- dense arrays ------------------------------------------------------------
//
// Three helpers: vec_push, vec_length and vec_at. vec_at exists instead of
// plain `v[i]` because `a[7]` on a three-element array is `undefined` in
// JavaScript but undefined behaviour in C++. Any out-of-range, fractional or
// negative index returns an absent (undefined-tagged) carrier, which is why
// TypeInference::elementTypeOf starts its element join from `undefined`.

/// Sets `index` to the element a Number key selects and returns true, if any.
/// A key selects an element only when it is an integral index below `size`;
/// -0 names "0". Any other Number names a property ("0.5", "-1") that a proved
/// array or snapshot does not have, as in the VM and Node.
inline bool element_index(nullable_scalar key, std::size_t size, std::size_t & index) {
    if (key.tag != nullable_scalar::kind::number || !(key.value >= 0.0) ||
        key.value >= static_cast<double>(size) || std::trunc(key.value) != key.value) {
        return false;
    }
    index = static_cast<std::size_t>(key.value);
    return true;
}
/// The carrier a read of a stored V returns; a default-constructed carrier
/// means absent (undefined).
template <class V> struct read_carrier {};
template <> struct read_carrier<double> {
    using type = nullable_scalar;
};
template <> struct read_carrier<js_boolean_t> {
    using type = nullable_scalar;
};
template <> struct read_carrier<nullable_scalar> {
    using type = nullable_scalar;
};
template <> struct read_carrier<std::string> {
    using type = nullable_string;
};
template <> struct read_carrier<nullable_string> {
    using type = nullable_string;
};
/// `a[i]`. Out of range returns the carrier's undefined value (never NaN).
/// Also used for owning snapshots of Map string keys.
template <class T>
typename read_carrier<T>::type vec_at(const std::vector<T> & values, nullable_scalar key) {
    std::size_t index = 0;
    if (!element_index(key, values.size(), index)) { return {}; }
    return values[index];
}
/// `a.length`, which is exactly `size()` because the dense-array proof rules
/// out holes.
template <class T> js_num vec_length(const std::vector<T> & v) {
    return js_num{static_cast<double>(v.size())};
}
/// Appends one element of an array literal, in source order.
inline void vec_push(std::vector<double> & v, js_num x) {
    v.push_back(x.value());
}

// --- object values -----------------------------------------------------------

/// Defined by each generated program; its fields are the program's proved
/// scalar fields.
struct identity_object;

/// Either a shared owning object identity or an exact tagged scalar.
struct object_value {
    nullable_scalar scalar;
    std::shared_ptr<identity_object> object;
    object_value() = default;
    object_value(nullable_scalar value) : scalar(value) {}
    object_value(double value) : scalar(value) {}
    object_value(js_num value) : scalar(value) {}
    object_value(js_boolean_t value) : scalar(value) {}
    object_value(bool value) : object_value(js_boolean_t{value}) {}
    object_value(std::shared_ptr<identity_object> value) : object(std::move(value)) {}
};
template <> struct read_carrier<object_value> {
    using type = object_value;
};
/// A Map holding instances of a class (ctcompile's registry Maps) reads back
/// the owner itself, or a null one for a missing key: `map.get(k)` is the
/// instance or undefined.
template <class C> struct read_carrier<std::shared_ptr<C>> {
    using type = std::shared_ptr<C>;
};
inline nullable_scalar global_scalar(const object_value & value) {
    if (value.object) { std::terminate(); }
    return value.scalar;
}
inline js_num global_number(const object_value & value) {
    return global_number(global_scalar(value));
}
inline object_value to_object_value(object_value value) {
    return value;
}
inline bool object_truthy(const object_value & value) {
    return value.object || scalar_truthy(value.scalar);
}
inline js_boolean_t object_strict_equal(const object_value & left, const object_value & right) {
    if (left.object || right.object) { return js_boolean_t{left.object == right.object}; }
    return scalar_strict_equal(left.scalar, right.scalar);
}
inline js_boolean_t object_equal(const object_value & left, const object_value & right) {
    // The admission check excludes any pair that could invoke an
    // object-to-primitive conversion.
    if (left.object || right.object) { return js_boolean_t{left.object == right.object}; }
    return scalar_equal(left.scalar, right.scalar);
}
inline std::string object_typeof(const object_value & value) {
    return value.object ? "object" : scalar_typeof(value.scalar);
}

// --- the Boolean object --------------------------------------------------------

/// The Boolean constructor's members native code calls: generated code reads
/// `ctnative::Boolean(x)`. `Boolean(value)` (ECMA-262 20.3.1.1) is ToBoolean
/// (7.1.2), so each call operator forwards to the carrier's existing
/// truthiness: the explicit `bool` of js_num, js_string, js_boolean_t and
/// js_symbol_t, scalar_truthy, string_truthy, detail::variant_truthy for the
/// String unions, and object_truthy. It sits here, after those helpers,
/// rather than beside js_boolean_t. No prototype method is lowered yet.
struct boolean_constructor {
    template <class T>
    requires requires(const T & value) { static_cast<bool>(value); }
    constexpr js_boolean_t operator()(const T & value) const {
        return js_boolean_t{static_cast<bool>(value)};
    }
    js_boolean_t operator()(nullable_scalar value) const {
        return js_boolean_t{scalar_truthy(value)};
    }
    js_boolean_t operator()(const nullable_string & value) const {
        return js_boolean_t{string_truthy(value)};
    }
    template <class... T> js_boolean_t operator()(const std::variant<T...> & value) const {
        return js_boolean_t{detail::variant_truthy(value)};
    }
    js_boolean_t operator()(const object_value & value) const {
        return js_boolean_t{object_truthy(value)};
    }
};
inline constexpr boolean_constructor Boolean{};

// --- Map storage -------------------------------------------------------------
//
// Two representations behind one alias, map_storage. A module with any Map
// snapshot keeps insertion order for every Map: the vector-backed storage
// gives the shared helpers the same operations as std::map, with linear
// lookup. Without iteration, key order is unobservable and lookup uses
// std::map, but numeric keys still need SameValueZero: std::less<double> alone
// is not a valid ordering for NaN keys.

/// SameValueZero key equality: NaN equals NaN, and 0 equals -0.
template <class K> bool map_key_equal(const K & a, const K & b) {
    return a == b;
}
inline bool map_key_equal(double a, double b) {
    return a == b || (std::isnan(a) && std::isnan(b));
}
inline bool map_key_equal(nullable_scalar a, nullable_scalar b) {
    if (a.tag != b.tag) { return false; }
    return a.tag == nullable_scalar::kind::undefined || a.tag == nullable_scalar::kind::null ||
           map_key_equal(a.value, b.value);
}
template <class... T>
bool map_key_equal(const std::variant<T...> & a, const std::variant<T...> & b) {
    return std::visit(
        [](const auto & left, const auto & right) {
            if constexpr (std::is_same_v<std::decay_t<decltype(left)>,
                                         std::decay_t<decltype(right)>>) {
                return map_key_equal(left, right);
            } else {
                return false;
            }
        },
        a, b);
}
/// Map storage that keeps insertion order, used when a snapshot can observe it.
template <class K, class V> struct ordered_map_storage {
    using key_type = K;
    std::vector<std::pair<K, V>> entries;
    auto find(const K & key) {
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            if (map_key_equal(it->first, key)) { return it; }
        }
        return entries.end();
    }
    auto end() { return entries.end(); }
    void insert_or_assign(const K & key, const V & value) {
        const auto found = find(key);
        if (found != end()) {
            found->second = value;
        } else {
            entries.emplace_back(key, value);
        }
    }
    std::size_t erase(const K & key) {
        const auto found = find(key);
        if (found == end()) { return 0; }
        entries.erase(found);
        return 1;
    }
    void clear() { entries.clear(); }
    std::size_t size() const { return entries.size(); }
};
/// Key ordering for std::map storage that agrees with SameValueZero: all NaNs
/// are equivalent and sort first.
template <class K> struct map_key_less {
    bool operator()(const K & a, const K & b) const { return std::less<K>{}(a, b); }
};
template <> struct map_key_less<js_boolean_t> {
    bool operator()(js_boolean_t a, js_boolean_t b) const { return !a && b; }
};
template <> struct map_key_less<double> {
    bool operator()(double a, double b) const {
        if (std::isnan(a)) { return !std::isnan(b); }
        return !std::isnan(b) && a < b;
    }
};
template <> struct map_key_less<nullable_scalar> {
    bool operator()(nullable_scalar a, nullable_scalar b) const {
        if (a.tag != b.tag) { return a.tag < b.tag; }
        return (a.tag == nullable_scalar::kind::number ||
                a.tag == nullable_scalar::kind::boolean) &&
               map_key_less<double>{}(a.value, b.value);
    }
};
template <class... T> struct map_key_less<std::variant<T...>> {
    bool operator()(const std::variant<T...> & a, const std::variant<T...> & b) const {
        if (a.index() != b.index()) { return a.index() < b.index(); }
        return std::visit(
            [](const auto & left, const auto & right) {
                using L = std::decay_t<decltype(left)>;
                if constexpr (std::is_same_v<L, std::decay_t<decltype(right)>>) {
                    return map_key_less<L>{}(left, right);
                } else {
                    return false;
                }
            },
            a, b);
    }
};
#ifdef CTNATIVE_ORDERED_MAPS
template <class K, class V> using map_storage = ordered_map_storage<K, V>;
#else
template <class K, class V> using map_storage = std::map<K, V, map_key_less<K>>;
#endif

// --- Map helpers -------------------------------------------------------------
//
// Keys are primitives; payloads are primitives, owned object identities or
// other Maps (no cycles).

template <class K> using number_map = map_storage<K, double>;
using string_to_number_map = number_map<std::string>;
template <class K, class V> std::shared_ptr<map_storage<K, V>> make_map() {
    return std::make_shared<map_storage<K, V>>();
}
template <class K> std::shared_ptr<number_map<K>> make_number_map() {
    return std::make_shared<number_map<K>>();
}
inline std::shared_ptr<string_to_number_map> make_string_to_number_map() {
    return make_number_map<std::string>();
}
template <class Map, class K> js_boolean_t map_has(const Map & map, const K & key) {
    return js_boolean_t{map->find(key) != map->end()};
}
/// `map.get(key)`. Every read returns a copy (owned strings and object owners
/// included) and never borrows Map storage; a missing key gives undefined.
template <class K, class V>
typename read_carrier<V>::type map_get(map_storage<K, V> * map, const K & key) {
    const auto found = map->find(key);
    if (found != map->end()) { return found->second; }
    return {};
}
template <class K, class V>
typename read_carrier<V>::type map_get(const std::shared_ptr<map_storage<K, V>> & map,
                                       const K & key) {
    return map_get(map.get(), key);
}
/// The payload of a key the compiler proved present. Terminates if it is
/// missing, since that contradicts the compiler's proof.
template <class Map, class K> const auto & present_entry(const Map & map, const K & key) {
    const auto found = map->find(key);
    if (found == map->end()) { std::terminate(); }
    return found->second;
}
template <class Map, class K> auto map_get_present(const Map & map, const K & key) {
    return present_entry(map, key);
}
/// The alternative T of a present key's variant payload; the compiler picks T
/// only from a proof of that exact payload, and the call terminates otherwise.
/// Returns by value so an owned string survives overwrite, deletion and the
/// Map's lifetime.
template <class T, class K, class... V>
T map_get_present_as(map_storage<K, std::variant<V...>> * map, const K & key) {
    return std::visit(
        [](const auto & value) -> T {
            if constexpr (std::is_same_v<T, std::decay_t<decltype(value)>>) {
                return value;
            } else {
                std::terminate();
            }
        },
        present_entry(map, key));
}
template <class T, class K, class... V>
T map_get_present_as(const std::shared_ptr<map_storage<K, std::variant<V...>>> & map,
                     const K & key) {
    return map_get_present_as<T>(map.get(), key);
}
/// Keys are normalized like ECMA-262 CanonicalizeKeyedCollectionKey (-0
/// becomes +0); payloads keep their sign.
template <class K> const K & map_normalize_key(const K & key) {
    return key;
}
inline double map_normalize_key(double key) {
    return key == 0 ? 0.0 : key;
}
inline nullable_scalar map_normalize_key(nullable_scalar key) {
    if (key.tag == nullable_scalar::kind::number) { key.value = map_normalize_key(key.value); }
    return key;
}
template <class... T> std::variant<T...> map_normalize_key(const std::variant<T...> & key) {
    return std::visit(
        [](const auto & value) -> std::variant<T...> { return map_normalize_key(value); }, key);
}
template <class Map, class K, class V>
Map map_set(const Map & map, const K & key, const V & value) {
    map->insert_or_assign(map_normalize_key(key), value);
    return map;
}
template <class Map, class K> js_boolean_t map_delete(const Map & map, const K & key) {
    return js_boolean_t{map->erase(key) != 0};
}
template <class Map> void map_clear(const Map & map) {
    map->clear();
}
template <class Map> js_num map_size(const Map & map) {
    return js_num{static_cast<double>(map->size())};
}

template <class T> T map_nullable_payload_as(const nullable_string & value) {
    if constexpr (std::is_same_v<T, nullable_string>) {
        return value;
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (value.tag != nullable_string::kind::string) { std::terminate(); }
        return value.value;
    } else {
        std::terminate();
    }
}
template <class T> T map_nullable_payload_as(nullable_scalar value) {
    if constexpr (std::is_same_v<T, nullable_scalar>) {
        return value;
    } else if constexpr (std::is_same_v<T, double>) {
        return global_number(value).value();
    } else if constexpr (std::is_same_v<T, js_boolean_t>) {
        return global_boolean(value);
    } else {
        std::terminate();
    }
}
/// Like map_get_present_as for nullable payloads: the compiler supplies T only
/// from a separate proof about this payload; the storage schema alone never
/// selects a narrower alternative.
template <class T, class K, class V>
T map_get_present_nullable_as(map_storage<K, V> * map, const K & key) {
    const V & payload = present_entry(map, key);
    if constexpr (detail::one_of<V, nullable_string, nullable_scalar>) {
        return map_nullable_payload_as<T>(payload);
    } else {
        return std::visit(
            [](const auto & value) -> T {
                using Actual = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Actual, nullable_string>) {
                    return map_nullable_payload_as<T>(value);
                } else if constexpr (std::is_same_v<T, Actual>) {
                    return value;
                } else {
                    std::terminate();
                }
            },
            payload);
    }
}
template <class T, class K, class V>
T map_get_present_nullable_as(const std::shared_ptr<map_storage<K, V>> & map, const K & key) {
    return map_get_present_nullable_as<T>(map.get(), key);
}

template <class K>
std::shared_ptr<identity_object> map_get_present_identity(map_storage<K, object_value> * map,
                                                          const K & key) {
    const object_value & payload = present_entry(map, key);
    // Both presence and the object result type were independently proved.
    if (!payload.object) { std::terminate(); }
    return payload.object;
}
template <class K>
std::shared_ptr<identity_object> map_get_present_identity(
    const std::shared_ptr<map_storage<K, object_value>> & map, const K & key) {
    return map_get_present_identity(map.get(), key);
}

// --- Map snapshots -----------------------------------------------------------
//
// Only the ordered storage has `entries`, so a lookup-only module that reaches
// one of these fails to compile rather than gaining an iteration API whose
// sorted order would differ from JavaScript insertion order. Every snapshot is
// an owned copy: it survives overwrite, deletion and the Map's lifetime.

template <class K, class V> std::vector<V> map_values(map_storage<K, V> * map) {
    std::vector<V> out;
    out.reserve(map->entries.size());
    for (const auto & entry : map->entries) { out.push_back(entry.second); }
    return out;
}
template <class K, class V>
std::vector<V> map_values(const std::shared_ptr<map_storage<K, V>> & map) {
    return map_values(map.get());
}
template <class Map> auto map_keys(const Map & map) {
    using K = typename std::pointer_traits<Map>::element_type::key_type;
    std::vector<K> out;
    out.reserve(map->entries.size());
    for (const auto & entry : map->entries) { out.push_back(entry.first); }
    return out;
}
/// Element `key` of a keys (`Keys`) or values snapshot of a Map proved
/// unmodified since the snapshot. Indexing follows element_index, so -0.5
/// names the property "-0.5", not element zero.
template <bool Keys, class K, class V>
nullable_scalar map_snapshot_at(const std::shared_ptr<map_storage<K, V>> & map,
                                nullable_scalar key) {
    std::size_t index = 0;
    if (!element_index(key, map->entries.size(), index)) { return {}; }
    const auto & entry = map->entries[index];
    if constexpr (Keys) {
        return entry.first;
    } else {
        return entry.second;
    }
}

// --- owned globals -----------------------------------------------------------

/// Reads a field of a proved owned global (through a shared or raw pointer).
template <auto Field, class P> auto owned_global_get(const P & object) {
    return std::to_address(object)->*Field;
}
/// Writes a field of a proved owned global (through a shared or raw pointer).
template <auto Field, class P, class V> void owned_global_set(const P & object, V value) {
    std::to_address(object)->*Field = value;
}
/// The table's captured Data Map.
template <class Table> auto data_map(Table * table) {
    return &table->captured_map;
}

// --- method tables -----------------------------------------------------------

/// Initializes a method-table field the compiler proved is set once.
template <auto Member, class Table, class Callable>
void method_set(const std::shared_ptr<Table> & table, Callable callable) {
    table.get()->*Member = std::move(callable);
}
/// Copies the callable out of a method-table field proved initialized.
template <auto Member, class Table> auto method_get(const std::shared_ptr<Table> & table) {
    return table.get()->*Member;
}
/// Calls a stored callable with its concrete argument types.
template <class Callable, class... Args>
auto invoke_callable(const Callable & callable, Args... args) {
    return callable(args...);
}
/// Calls a fixed member of the table without copying the callable out.
template <auto Member, class Table, class... Args>
auto invoke_session(const std::shared_ptr<Table> & table, Args... args) {
    return (table.get()->*Member)(std::move(args)...);
}

#ifdef CTNATIVE_DOM
// --- the DOM entry -----------------------------------------------------------

/// The array index `key` names, or UINT32_MAX for an ordinary key. ECMAScript
/// orders array-index keys before ordinary keys.
inline std::uint32_t json_property_index(std::string_view key) {
    constexpr auto ordinary = std::numeric_limits<std::uint32_t>::max();
    if (key.empty() || (key.size() > 1 && key.front() == '0')) { return ordinary; }
    std::uint32_t value = ordinary;
    const auto [end, error] = std::from_chars(key.data(), key.data() + key.size(), value);
    return error == std::errc{} && end == key.data() + key.size() ? value : ordinary;
}

/// Defines or overwrites an own data property of a JSON object, keeping
/// ECMAScript key order. Used only for proved own-data writes. Assignment also
/// excludes the inherited __proto__ setter (see
/// assign_json_snapshot_property); spread defines `__proto__` as an own data
/// property.
inline void set_json_property(ctbrowser::json_value & target, std::string key,
                              ctbrowser::json_value value) {
    auto & members = std::get<ctbrowser::json_value::object>(target.data);
    // ponytail: linear overwrite lookup; index the keys if large Config
    // objects make this quadratic work measurable.
    const auto found = std::ranges::find(members, key, &ctbrowser::json_value::member::key);
    if (found != members.end()) {
        found->value = std::move(value);
        return;
    }
    const auto index = json_property_index(key);
    const auto position =
        index == std::numeric_limits<std::uint32_t>::max()
            ? members.end()
            : std::ranges::lower_bound(members, index, {}, [](const auto & member) {
                  return json_property_index(member.key);
              });
    members.insert(position, {std::move(key), std::move(value)});
}

/// Assignment of one snapshot key into a fresh target, visiting each unique
/// key once; only the final own data is observed. Assigning `__proto__` calls
/// the inherited setter, which changes no own property, so it is skipped; its
/// value was already evaluated, including any URI/JSON failure.
inline void assign_json_snapshot_property(ctbrowser::json_value & target, std::string key,
                                          ctbrowser::json_value value) {
    if (key != "__proto__") { set_json_property(target, std::move(key), std::move(value)); }
}

/// Object spread `{...source}` into a distinct fresh object: `source` is null,
/// an array or an object, and no shallow alias can be observed. Keys keep the
/// order of their first definition and the value of their last.
inline void copy_json_properties(ctbrowser::json_value & target,
                                 const ctbrowser::json_value & source) {
    if (const auto * object = std::get_if<ctbrowser::json_value::object>(&source.data)) {
        for (const auto & member : *object) { set_json_property(target, member.key, member.value); }
    } else if (const auto * array = std::get_if<ctbrowser::json_value::array>(&source.data)) {
        for (std::size_t i = 0; i < array->size(); ++i) {
            set_json_property(target, std::to_string(i), (*array)[i]);
        }
    }
}

inline void require_element(ctbrowser::element_ref element) {
    ctbrowser::validate_element(element).value();
}
/// Checks a DOM entry's element input. The host contract never supplies an
/// HTML form: a form exposes its controls by index and by name ahead of every
/// member ([LegacyOverrideBuiltIns], HTML 4.10.3), so a control named
/// `getAttribute` or `jquery` would change what the typed DOM proof and the
/// element facts read, and the past-names map behind that lives in the page's
/// wrapper, not here. A Data session's inputs are only used as keys.
/// ponytail: a form a query returns still reads as an ordinary element; refuse
/// it at its member reads too if a compiled program must match the VM there.
inline void require_entry_element(ctbrowser::element_ref element) {
    require_element(element);
    const auto view = element.owner->read();
    if (view.element_ns(element.id) == ctbrowser::node_ns::html &&
        view.local_name(element.id) == "form") {
        throw std::invalid_argument("DOM entry requires a contracted element other than a form");
    }
}
inline void require_dataset_element(ctbrowser::element_ref element) {
    require_entry_element(element);
    // The host contract supplies only HTML/SVG elements. Other namespace URIs
    // still live in Shell and cannot be recovered from a public node_id.
    if (element.owner->read().element_ns(element.id) == ctbrowser::node_ns::other) {
        throw std::invalid_argument("DOM dataset requires a contracted HTML or SVG element");
    }
}
/// `text.replace(/[A-Z]/g, Replacement)`; the source proof fixed the pattern
/// and the callback. ASCII matches are one byte even in WTF-8, and every
/// unmatched byte is kept unchanged.
template <auto Replacement> js_string replace_uppercase(const js_string & text) {
    std::string result;
    for (char c : text.value()) {
        if (c >= 'A' && c <= 'Z') {
            result += Replacement(js_string{std::string(1, c)}).value();
        } else {
            result += c;
        }
    }
    return js_string{std::move(result)};
}

template <auto Predicate>
std::vector<std::string> filter_strings(const std::vector<std::string> & values) {
    std::vector<std::string> selected;
    std::copy_if(
        values.begin(), values.end(), std::back_inserter(selected),
        [](const std::string & value) { return static_cast<bool>(Predicate(js_string{value})); });
    return selected;
}

inline std::vector<std::string> dataset_keys(ctbrowser::element_ref element) {
    std::vector<std::string> keys;
    for (auto & [key, value] : ctbrowser::dataset_entries(*element.owner, element.id)) {
        keys.push_back(std::move(key));
    }
    return keys;
}
inline std::string dataset_value(ctbrowser::element_ref element, std::string_view key) {
    return ctbrowser::dataset_value(*element.owner, element.id, key).value();
}
inline js_boolean_t toggle_class(ctbrowser::element_ref element, std::string_view token,
                                 std::optional<bool> force = std::nullopt) {
    auto result = ctbrowser::toggle_token(*element.owner, element.id,
                                          element.owner->atoms().intern("class"), token, force)
                      .value();
    result.update.value();
    return js_boolean_t{result.present};
}
inline js_boolean_t toggle_class(ctbrowser::element_ref element, std::string_view token,
                                 js_boolean_t force) {
    return toggle_class(element, token, static_cast<bool>(force));
}
inline js_boolean_t contains_class(ctbrowser::element_ref element, std::string_view token) {
    return js_boolean_t{ctbrowser::contains_token(
        element.owner->read().attribute_value(element.id, element.owner->atoms().intern("class")),
        token)};
}
template <class... Tokens>
void add_class(ctbrowser::element_ref element, const Tokens &... tokens) {
    const std::array<std::string, sizeof...(Tokens)> given{tokens...};
    ctbrowser::add_tokens(*element.owner, element.id, element.owner->atoms().intern("class"), given)
        .value()
        .value();
}
template <class... Tokens>
void remove_class(ctbrowser::element_ref element, const Tokens &... tokens) {
    const std::array<std::string, sizeof...(Tokens)> given{tokens...};
    ctbrowser::remove_tokens(*element.owner, element.id, element.owner->atoms().intern("class"),
                             given)
        .value()
        .value();
}
inline std::optional<std::string> get_attribute(ctbrowser::element_ref element,
                                                std::string_view name) {
    return ctbrowser::get_element_attribute(*element.owner, element.id, name);
}
inline void set_attribute(ctbrowser::element_ref element, std::string_view name,
                          std::string_view text) {
    ctbrowser::set_element_attribute(*element.owner, element.id, name, text).value();
}
inline void set_attribute(ctbrowser::element_ref element, std::string_view name,
                          js_boolean_t value) {
    set_attribute(element, name, value ? std::string_view("true") : std::string_view("false"));
}
inline void set_attribute(ctbrowser::element_ref element, std::string_view name, js_num value) {
    set_attribute(element, name, ctbrowser::number_to_string(value.value()));
}
inline void set_optional_attribute(ctbrowser::element_ref element, std::string_view name,
                                   const std::optional<std::string> & value) {
    set_attribute(element, name, value ? std::string_view(*value) : std::string_view("null"));
}
inline js_num dom_number(const std::optional<std::string> & text) {
    return js_num{text ? ctbrowser::string_to_number(*text) : 0.0};
}
inline js_boolean_t toggle_attribute(ctbrowser::element_ref element, std::string_view name,
                                     std::optional<bool> force = std::nullopt) {
    auto result =
        ctbrowser::toggle_element_attribute(*element.owner, element.id, name, force).value();
    result.update.value();
    return js_boolean_t{result.present};
}
inline js_boolean_t toggle_attribute(ctbrowser::element_ref element, std::string_view name,
                                     js_boolean_t force) {
    return toggle_attribute(element, name, static_cast<bool>(force));
}
inline js_boolean_t has_attribute(ctbrowser::element_ref element, std::string_view name) {
    return js_boolean_t{element.owner->read().has_attribute(
        element.id, ctbrowser::attribute_key(*element.owner, element.id, name))};
}
inline void remove_attribute(ctbrowser::element_ref element, std::string_view name) {
    element.owner
        ->remove_attribute(element.id, ctbrowser::attribute_key(*element.owner, element.id, name))
        .value();
}
inline js_boolean_t contains(ctbrowser::element_ref element, ctbrowser::element_ref other) {
    return js_boolean_t{element.owner == other.owner &&
                        element.owner->read().is_ancestor_of(element.id, other.id)};
}
// The selector engine is not included here. ctbrowser/style/engine.hpp costs
// about as much to parse as the rest of a DOM program put together, so a
// program that takes a `ctbrowser::style::engine &` includes it itself, after
// this header, and the selector helpers instantiate against it there.
template <class Style> void require_style(ctbrowser::document & document, Style & style) {
    if (&style.atoms() != &document.atoms()) {
        throw std::invalid_argument("DOM selector engine uses another atom table");
    }
}
template <class Style> void require_style(ctbrowser::element_ref element, Style & style) {
    require_style(*element.owner, style);
}
inline ctbrowser::style::css::stylesheet parse_selector(ctbrowser::document & document,
                                                        std::string_view selector) {
    bool bad = false;
    auto parsed = ctbrowser::style::css::parse_selector_text(selector, document.atoms(), bad);
    if (bad) { throw std::invalid_argument("DOM selector is invalid"); }
    return parsed;
}
inline ctbrowser::style::css::stylesheet parse_selector(ctbrowser::element_ref element,
                                                        std::string_view selector) {
    return parse_selector(*element.owner, selector);
}
// Named method objects keep generated calls close to their source spelling
// (`Element.prototype.matches.call(...)`). They hold no state: each call
// borrows its explicit element and style engine. Browser.hpp adds the
// overloads that take checked borrowed views.
class js_element_t;
struct matches_method {
    js_boolean_t call(const js_element_t & element, const js_string & selector) const;
    template <class Style>
    js_boolean_t call(ctbrowser::element_ref element, Style & style,
                      std::string_view selector) const {
        const auto parsed = parse_selector(element, selector);
        return js_boolean_t{
            style.element_matches(element.owner->read(), element.id, parsed.selectors)};
    }
};

struct closest_method {
    std::optional<js_element_t> call(const js_element_t & element,
                                     const js_string & selector) const;
    template <class Style>
    ctbrowser::element_ref call(ctbrowser::element_ref element, Style & style,
                                std::string_view selector) const {
        const auto parsed = parse_selector(element, selector);
        const auto found = style.closest(element.owner->read(), element.id, parsed.selectors);
        return found ? ctbrowser::element_ref{element.owner, found} : ctbrowser::element_ref{};
    }
};

struct query_selector_method {
    std::optional<js_element_t> call(const js_element_t & element,
                                     const js_string & selector) const;
    template <class Style>
    ctbrowser::element_ref call(ctbrowser::element_ref element, Style & style,
                                std::string_view selector) const {
        const auto parsed = parse_selector(element, selector);
        const auto found = style.select(element.owner->read(), element.id, parsed.selectors, true);
        return found.empty() ? ctbrowser::element_ref{}
                             : ctbrowser::element_ref{element.owner, found.front()};
    }
};

struct query_selector_all_method {
    std::vector<js_element_t> call(const js_element_t & element, const js_string & selector) const;
    template <class Style>
    std::vector<ctbrowser::element_ref> call(ctbrowser::element_ref element, Style & style,
                                             std::string_view selector) const {
        const auto parsed = parse_selector(element, selector);
        const auto found = style.select(element.owner->read(), element.id, parsed.selectors, false);
        std::vector<ctbrowser::element_ref> snapshot;
        snapshot.reserve(found.size());
        for (const auto id : found) { snapshot.push_back({element.owner, id}); }
        return snapshot;
    }
};
struct element_prototype {
    matches_method matches;
    closest_method closest;
    query_selector_method querySelector;
    query_selector_all_method querySelectorAll;
};
struct element_constructor {
    element_prototype prototype;
};
inline constexpr element_constructor Element{};

/// The global `document`. Browser.hpp's document_scope binds it to a borrowed
/// document for one native invocation.
struct document_object {
    std::optional<js_element_t> documentElement() const;
    std::optional<js_element_t> querySelector(const js_string & selector) const;
    std::vector<js_element_t> querySelectorAll(const js_string & selector) const;
};
inline constexpr document_object document{};

// Bare-name aliases for the Element.prototype method objects, used by
// generated code that calls `matches(...)` instead of
// `Element.prototype.matches.call(...)`.
inline constexpr const auto & matches = Element.prototype.matches;
inline constexpr const auto & closest = Element.prototype.closest;
inline constexpr const auto & querySelector = Element.prototype.querySelector;
inline constexpr const auto & querySelectorAll = Element.prototype.querySelectorAll;
// Orders element keys by document address, then node id, without
// dereferencing either document. Both the node's slot and its generation are
// part of its identity.
template <> struct map_key_less<ctbrowser::element_ref> {
    bool operator()(ctbrowser::element_ref a, ctbrowser::element_ref b) const noexcept {
        return a.owner == b.owner ? a.id < b.id
                                  : std::less<ctbrowser::document *>{}(a.owner, b.owner);
    }
};
#endif

} // namespace ctnative

// The console object, whose members need the carriers above.
#include "ctcompile/CTNative/Runtime/Console.hpp"

// Needs the carriers above, so it comes last.
#include "ctcompile/CTNative/Runtime/Narrowing.hpp"
