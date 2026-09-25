// ctnative.hpp - the runtime a native program includes.
//
// THE ONE HEADER EVERY GENERATED TRANSLATION UNIT INCLUDES. Until 2026-09-15
// these helpers lived in nine C++ files as STRING LITERALS, each emitted as an
// emitc.verbatim behind its own `needs*` flag, and nothing compiled them until
// a generated program did. This is the same text as a real header: the build
// compiles it under -Werror (test/Runtime/NativeRuntime.cpp), clang-format
// formats it, and a generated program spells one `#include`.
//
// TWO SWITCHES, both set by the emitter BEFORE the include, because they are
// the two things a program decides and a header cannot:
//
//   CTNATIVE_ORDERED_MAPS  every Map keeps insertion order (a snapshot is
//                          observable somewhere in the program); otherwise
//                          lookup is `std::map` with SameValueZero keys
//   CTNATIVE_DOM           the program is a DOM entry and ctbrowser's public
//                          DOM headers are on its include path. Primitive String
//                          conversion/formatting also links public Core without it.
//
// NO ctbrowser::script SYMBOL, ever: Script/ is the interpreter, a dev-time
// oracle and never a dependency of a native program.
#pragma once

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

// Compatibility for arithmetic/signatures not yet migrated to ctnative::js_num.
using js_num = double;

namespace ctnative {

inline std::string string_concat(std::string left, std::string_view right) {
    return (js_string{std::move(left)} + js_string{right}).value();
}

// --- exceptions --------------------------------------------------------------

template <class T> struct js_exception {
    T value;
};

// --- optional scalars --------------------------------------------------------
//
// Opt and scalar-only Variant types share a carrier; the runtime tag preserves
// which JavaScript value arrived. A present NaN is never an absence sentinel.

struct undefined_t {};
struct js_null_t {};

// JavaScript Boolean values cross into C++ conditions and numbers explicitly.
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

// ctcompile: optional scalar values preserve null, undefined and present NaN
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
// Numeric global admission is a proof about the stored tag. Check it at the
// observation boundary so a missing generated store cannot imitate a NaN.
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

inline bool boolean_string_truthy(const std::variant<js_boolean_t, std::string> & value) {
    return std::visit(
        [](const auto & alternative) {
            if constexpr (std::is_same_v<std::decay_t<decltype(alternative)>, js_boolean_t>) {
                return static_cast<bool>(alternative);
            } else {
                return !alternative.empty();
            }
        },
        value);
}
inline js_string boolean_string_text(const std::variant<js_boolean_t, std::string> & value) {
    return std::visit(
        [](const auto & alternative) {
            if constexpr (std::is_same_v<std::decay_t<decltype(alternative)>, js_boolean_t>) {
                return js_string{} + alternative;
            } else {
                return js_string{alternative};
            }
        },
        value);
}
inline js_num to_number(const std::variant<js_boolean_t, std::string> & value) {
    return std::visit(
        [](const auto & alternative) {
            if constexpr (std::is_same_v<std::decay_t<decltype(alternative)>, js_boolean_t>) {
                return alternative.to_number();
            } else {
                return js_num{ctbrowser::string_to_number(alternative)};
            }
        },
        value);
}

// --- owning strings ----------------------------------------------------------

// ctcompile: owning strings retain separate null and undefined tags
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
        // Admission only permits an absent scalar to widen into this carrier.
        std::terminate();
    }
    return out;
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
// Observation never coerces a missing store or a wrong tag into String text.
// Return an owning copy so subsequent stores cannot change the saved value.
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
// Only nullable String key schemas need these comparisons. Both storage
// layouts preserve the tag before comparing owned text; absent keys neither
// coerce to their spelling nor alias the empty String.
inline bool operator==(const nullable_string & a, const nullable_string & b) {
    return a.tag == b.tag && (a.tag != nullable_string::kind::string || a.value == b.value);
}
inline bool operator<(const nullable_string & a, const nullable_string & b) {
    if (a.tag != b.tag) { return a.tag < b.tag; }
    return a.tag == nullable_string::kind::string && a.value < b.value;
}

// A proved Number/String union owns exactly those two alternatives.
using number_string = std::variant<js_num, js_string>;

inline constexpr js_num to_number(js_num value) {
    return value;
}
inline js_num to_number(const js_string & value) {
    return value.to_number();
}
template <class T>
requires(std::is_same_v<T, undefined_t> || std::is_same_v<T, js_null_t>)
inline js_num to_number(T value) {
    return to_number(nullable_scalar{value});
}
inline js_num to_number(const number_string & value) {
    return std::visit(
        [](const auto & alternative) {
            if constexpr (std::is_same_v<std::decay_t<decltype(alternative)>, js_num>) {
                return alternative;
            } else {
                return alternative.to_number();
            }
        },
        value);
}
inline js_string number_string_text(const number_string & value) {
    return std::visit(
        [](const auto & alternative) {
            if constexpr (std::is_same_v<std::decay_t<decltype(alternative)>, js_string>) {
                return alternative;
            } else {
                return js_string{} + alternative;
            }
        },
        value);
}
inline bool number_string_truthy(const number_string & value) {
    return std::visit([](const auto & alternative) { return static_cast<bool>(alternative); },
                      value);
}
inline std::string number_string_typeof(const number_string & value) {
    return std::holds_alternative<js_num>(value) ? "number" : "string";
}
// Compatibility for callers of the earlier optional global storage.
inline number_string global_number_string(const std::optional<number_string> & value) {
    if (!value) { std::terminate(); }
    return *value;
}
inline void print_number_string(const char * name, const number_string & value) {
    std::visit(
        [name](const auto & alternative) {
            if constexpr (std::is_same_v<std::decay_t<decltype(alternative)>, js_num>) {
                print_scalar(name, nullable_scalar{alternative});
            } else {
                print_string(name, alternative.value());
            }
        },
        value);
}

// Source optional unions preserve absence as values, including reads before
// a global store. Neither absence alternative uses NaN or an empty String.
using nullable_number_string = std::variant<undefined_t, js_null_t, js_num, js_string>;

inline nullable_number_string to_nullable_number_string(const number_string & value) {
    return std::visit(
        [](const auto & alternative) -> nullable_number_string { return alternative; }, value);
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
    switch (value.tag) {
    case nullable_string::kind::undefined: return undefined_t{};
    case nullable_string::kind::null_value: return js_null_t{};
    case nullable_string::kind::string: return js_string{value.value};
    }
    std::terminate();
}
inline js_num to_number(const nullable_number_string & value) {
    return std::visit([](const auto & alternative) { return to_number(alternative); }, value);
}

inline js_string nullable_number_string_text(const nullable_number_string & value) {
    return std::visit(
        [](const auto & alternative) {
            if constexpr (std::is_same_v<std::decay_t<decltype(alternative)>, js_string>) {
                return alternative;
            } else {
                return nullable_scalar{alternative}.to_string();
            }
        },
        value);
}
inline bool nullable_number_string_truthy(const nullable_number_string & value) {
    return std::visit(
        [](const auto & alternative) {
            using T = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<T, js_num> || std::is_same_v<T, js_string>) {
                return static_cast<bool>(alternative);
            } else {
                return false;
            }
        },
        value);
}
inline std::string nullable_number_string_typeof(const nullable_number_string & value) {
    return std::visit(
        [](const auto & alternative) {
            if constexpr (std::is_same_v<std::decay_t<decltype(alternative)>, js_string>) {
                return std::string{"string"};
            } else {
                return scalar_typeof(nullable_scalar{alternative});
            }
        },
        value);
}
inline void print_nullable_number_string(const char * name, const nullable_number_string & value) {
    std::visit(
        [name](const auto & alternative) {
            if constexpr (std::is_same_v<std::decay_t<decltype(alternative)>, js_string>) {
                print_string(name, alternative.value());
            } else {
                print_scalar(name, nullable_scalar{alternative});
            }
        },
        value);
}
inline number_string global_number_string(const nullable_number_string & value) {
    return std::visit(
        [](const auto & alternative) -> number_string {
            using T = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<T, js_num> || std::is_same_v<T, js_string>) {
                return alternative;
            } else {
                std::terminate();
            }
        },
        value);
}
inline js_num global_number(const nullable_number_string & value) {
    if (const auto * number = std::get_if<js_num>(&value)) { return *number; }
    std::terminate();
}
inline std::string global_string(const nullable_number_string & value) {
    if (const auto * text = std::get_if<js_string>(&value)) { return text->value(); }
    std::terminate();
}

using boolean_string = std::variant<js_boolean_t, js_string>;
using nullable_boolean_string = std::variant<undefined_t, js_null_t, js_boolean_t, js_string>;

inline js_num to_number(const boolean_string & value) {
    return std::visit([](const auto & alternative) { return alternative.to_number(); }, value);
}
inline js_string boolean_string_text(const boolean_string & value) {
    return std::visit(
        [](const auto & alternative) {
            if constexpr (std::is_same_v<std::decay_t<decltype(alternative)>, js_string>) {
                return alternative;
            } else {
                return js_string{} + alternative;
            }
        },
        value);
}
inline bool boolean_string_truthy(const boolean_string & value) {
    return std::visit([](const auto & alternative) { return static_cast<bool>(alternative); },
                      value);
}
inline std::string boolean_string_typeof(const boolean_string & value) {
    return std::holds_alternative<js_boolean_t>(value) ? "boolean" : "string";
}
inline void print_boolean_string(const char * name, const boolean_string & value) {
    std::visit(
        [name](const auto & alternative) {
            if constexpr (std::is_same_v<std::decay_t<decltype(alternative)>, js_string>) {
                print_string(name, alternative.value());
            } else {
                print_scalar(name, nullable_scalar{alternative});
            }
        },
        value);
}
inline nullable_boolean_string to_nullable_boolean_string(const boolean_string & value) {
    return std::visit(
        [](const auto & alternative) -> nullable_boolean_string { return alternative; }, value);
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
    switch (value.tag) {
    case nullable_string::kind::undefined: return undefined_t{};
    case nullable_string::kind::null_value: return js_null_t{};
    case nullable_string::kind::string: return js_string{value.value};
    }
    std::terminate();
}
inline js_num to_number(const nullable_boolean_string & value) {
    return std::visit([](const auto & alternative) { return to_number(alternative); }, value);
}
inline js_string nullable_boolean_string_text(const nullable_boolean_string & value) {
    return std::visit(
        [](const auto & alternative) {
            if constexpr (std::is_same_v<std::decay_t<decltype(alternative)>, js_string>) {
                return alternative;
            } else {
                return nullable_scalar{alternative}.to_string();
            }
        },
        value);
}
inline bool nullable_boolean_string_truthy(const nullable_boolean_string & value) {
    return std::visit(
        [](const auto & alternative) {
            using T = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<T, js_boolean_t> || std::is_same_v<T, js_string>) {
                return static_cast<bool>(alternative);
            } else {
                return false;
            }
        },
        value);
}
inline std::string nullable_boolean_string_typeof(const nullable_boolean_string & value) {
    return std::visit(
        [](const auto & alternative) {
            if constexpr (std::is_same_v<std::decay_t<decltype(alternative)>, js_string>) {
                return std::string{"string"};
            } else {
                return scalar_typeof(nullable_scalar{alternative});
            }
        },
        value);
}
inline void print_nullable_boolean_string(const char * name,
                                          const nullable_boolean_string & value) {
    std::visit(
        [name](const auto & alternative) {
            if constexpr (std::is_same_v<std::decay_t<decltype(alternative)>, js_string>) {
                print_string(name, alternative.value());
            } else {
                print_scalar(name, nullable_scalar{alternative});
            }
        },
        value);
}
inline boolean_string global_boolean_string(const nullable_boolean_string & value) {
    return std::visit(
        [](const auto & alternative) -> boolean_string {
            using T = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<T, js_boolean_t> || std::is_same_v<T, js_string>) {
                return alternative;
            } else {
                std::terminate();
            }
        },
        value);
}
inline js_boolean_t global_boolean(const nullable_boolean_string & value) {
    if (const auto * boolean = std::get_if<js_boolean_t>(&value)) { return *boolean; }
    std::terminate();
}
inline std::string global_string(const nullable_boolean_string & value) {
    if (const auto * text = std::get_if<js_string>(&value)) { return text->value(); }
    std::terminate();
}

template <class T>
concept primitive_add_operand =
    std::is_same_v<T, js_num> || std::is_same_v<T, js_boolean_t> ||
    std::is_same_v<T, nullable_scalar> || std::is_same_v<T, js_string> ||
    std::is_same_v<T, nullable_string> ||
    std::is_same_v<T, std::variant<js_boolean_t, std::string>> ||
    std::is_same_v<T, number_string> || std::is_same_v<T, nullable_number_string> ||
    std::is_same_v<T, boolean_string> || std::is_same_v<T, nullable_boolean_string>;

template <primitive_add_operand L, primitive_add_operand R>
number_string add(const L & left, const R & right) {
    const auto isString = []<class T>(const T & value) {
        if constexpr (std::is_same_v<T, js_string>) {
            return true;
        } else if constexpr (std::is_same_v<T, nullable_string>) {
            return value.tag == nullable_string::kind::string;
        } else if constexpr (std::is_same_v<T, std::variant<js_boolean_t, std::string>>) {
            return std::holds_alternative<std::string>(value);
        } else if constexpr (std::is_same_v<T, number_string> ||
                             std::is_same_v<T, nullable_number_string> ||
                             std::is_same_v<T, boolean_string> ||
                             std::is_same_v<T, nullable_boolean_string>) {
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
        } else if constexpr (std::is_same_v<T, std::variant<js_boolean_t, std::string>> ||
                             std::is_same_v<T, boolean_string>) {
            return boolean_string_text(value);
        } else if constexpr (std::is_same_v<T, number_string>) {
            return number_string_text(value);
        } else if constexpr (std::is_same_v<T, nullable_number_string>) {
            return nullable_number_string_text(value);
        } else if constexpr (std::is_same_v<T, nullable_boolean_string>) {
            return nullable_boolean_string_text(value);
        } else {
            return js_string{} + value;
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
    std::is_same_v<T, js_num> || std::is_same_v<T, js_boolean_t> || std::is_same_v<T, js_string> ||
    std::is_same_v<T, nullable_scalar> || std::is_same_v<T, nullable_string> ||
    std::is_same_v<T, number_string> || std::is_same_v<T, nullable_number_string> ||
    std::is_same_v<T, boolean_string> || std::is_same_v<T, nullable_boolean_string> ||
    std::is_same_v<T, undefined_t> || std::is_same_v<T, js_null_t>;

namespace detail {
// Borrow String storage only for this comparison; other leaves retain the
// existing scalar tags. No combined value carrier or String copy is needed.
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
    } else if constexpr (std::is_same_v<T, number_string> ||
                         std::is_same_v<T, nullable_number_string> ||
                         std::is_same_v<T, boolean_string> ||
                         std::is_same_v<T, nullable_boolean_string>) {
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

// --- dense arrays - part 24 Phase 57A ----------------------------------------
//
// THREE OF THEM AND NO MORE. `push` and `size` are what the plan's rule names;
// `at` is the one that has to exist rather than being `v[i]`, because `a[7]` on
// a three-element array is `undefined` in JavaScript and undefined behaviour in
// C++, and undefined is this tier's NaN. Every out-of-range, fractional or
// negative index therefore answers NaN, which is EXACTLY what the element type
// says it may be - the join starts from `undefined` for this reason
// (TypeInference::elementTypeOf).

// ctcompile: `a[i]`, whose out-of-range answer is undefined, which is NaN here
inline nullable_scalar vec_at(const std::vector<double> & v, nullable_scalar key) {
    if (key.tag != nullable_scalar::kind::number) { return {}; }
    double i = std::trunc(key.value);
    if (!(i >= 0.0) || i >= static_cast<double>(v.size())) { return {}; }
    return v[static_cast<std::vector<double>::size_type>(i)];
}
// ctcompile: `a.length`, which is `size()` exactly - the site proof is what
// rules out a hole
template <class T> js_num vec_length(const std::vector<T> & v) {
    return js_num{static_cast<double>(v.size())};
}
// ctcompile: one element of an array literal, in source order
inline void vec_push(std::vector<double> & v, js_num x) {
    v.push_back(x.value());
}
// ctcompile: confined owning Map string-key snapshots
inline nullable_string vec_at(const std::vector<std::string> & values, nullable_scalar key) {
    if (key.tag != nullable_scalar::kind::number) { return {}; }
    const double index = std::trunc(key.value);
    if (!(index >= 0.0) || index >= static_cast<double>(values.size())) { return {}; }
    return values[static_cast<std::vector<std::string>::size_type>(index)];
}

// --- object values -----------------------------------------------------------

// The program defines it: its fields are the program's proved scalar fields.
struct identity_object;

// ctcompile: an owning property-free identity or an exact tagged scalar
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
    // Admission excludes any pair that could invoke object-to-primitive conversion.
    if (left.object || right.object) { return js_boolean_t{left.object == right.object}; }
    return scalar_equal(left.scalar, right.scalar);
}
inline std::string object_typeof(const object_value & value) {
    return value.object ? "object" : scalar_typeof(value.scalar);
}

// --- Map storage -------------------------------------------------------------
//
// TWO REPRESENTATIONS, one alias. A module with any snapshot keeps insertion
// order for all Map schemas: the vector-backed storage gives the shared helpers
// the same operations as std::map, with linear lookup. Without iteration, key
// order is unobservable and lookup is std::map - but numeric equivalence still
// needs SameValueZero: std::less<double> alone is not valid for NaN keys.

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
// ctcompile: insertion order is observable through Map snapshots
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
// ctcompile: no Map iteration; ordered lookup with SameValueZero keys
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
// ctcompile: primitive keys, acyclic primitive/Map payloads, owning identity

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
template <class K, class V>
requires(std::is_same_v<V, double> || std::is_same_v<V, js_boolean_t> ||
         std::is_same_v<V, nullable_scalar>)
nullable_scalar map_get(map_storage<K, V> * map, const K & key) {
    const auto found = map->find(key);
    if (found != map->end()) { return found->second; }
    return {};
}
template <class K, class V>
requires(std::is_same_v<V, double> || std::is_same_v<V, js_boolean_t> ||
         std::is_same_v<V, nullable_scalar>)
nullable_scalar map_get(const std::shared_ptr<map_storage<K, V>> & map, const K & key) {
    return map_get(map.get(), key);
}
template <class Map, class K> auto map_get_present(const Map & map, const K & key) {
    const auto found = map->find(key);
    if (found != map->end()) { return found->second; }
    // Reaching this point contradicts the compiler's dominance/identity proof.
    std::terminate();
}
// T is selected only by a rederived present, exact-payload proof. Return
// by value so an owning string survives overwrite, deletion and Map lifetime.
template <class T, class K, class... V>
T map_get_present_as(map_storage<K, std::variant<V...>> * map, const K & key) {
    const auto found = map->find(key);
    if (found == map->end()) { std::terminate(); }
    return std::visit(
        [](const auto & value) -> T {
            if constexpr (std::is_same_v<T, std::decay_t<decltype(value)>>) {
                return value;
            } else {
                std::terminate();
            }
        },
        found->second);
}
template <class T, class K, class... V>
T map_get_present_as(const std::shared_ptr<map_storage<K, std::variant<V...>>> & map,
                     const K & key) {
    return map_get_present_as<T>(map.get(), key);
}
// Map keys use CanonicalizeKeyedCollectionKey; payloads retain their sign.
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

// Nullable Strings also own payload storage. Every read returns a copy;
// neither a full-schema read nor an exact-tag read borrows Map storage.
template <class K> nullable_string map_get(map_storage<K, std::string> * map, const K & key) {
    const auto found = map->find(key);
    if (found != map->end()) { return found->second; }
    return {};
}
template <class K> nullable_string map_get(map_storage<K, nullable_string> * map, const K & key) {
    const auto found = map->find(key);
    if (found != map->end()) { return found->second; }
    return {};
}
template <class K>
nullable_string map_get(const std::shared_ptr<map_storage<K, std::string>> & map, const K & key) {
    return map_get(map.get(), key);
}
template <class K>
nullable_string map_get(const std::shared_ptr<map_storage<K, nullable_string>> & map,
                        const K & key) {
    return map_get(map.get(), key);
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
// The compiler supplies T only from an independent present payload fact.
// The storage schema itself never selects a narrower alternative.
template <class T, class K, class V>
T map_get_present_nullable_as(map_storage<K, V> * map, const K & key) {
    const auto found = map->find(key);
    if (found == map->end()) { std::terminate(); }
    if constexpr (std::is_same_v<V, nullable_string> || std::is_same_v<V, nullable_scalar>) {
        return map_nullable_payload_as<T>(found->second);
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
            found->second);
    }
}
template <class T, class K, class V>
T map_get_present_nullable_as(const std::shared_ptr<map_storage<K, V>> & map, const K & key) {
    return map_get_present_nullable_as<T>(map.get(), key);
}

// ctcompile: absent lookups retain undefined, saved values retain their owner
template <class K> object_value map_get(map_storage<K, object_value> * map, const K & key) {
    const auto found = map->find(key);
    if (found != map->end()) { return found->second; }
    return {};
}
template <class K>
std::shared_ptr<identity_object> map_get_present_identity(map_storage<K, object_value> * map,
                                                          const K & key) {
    const auto found = map->find(key);
    if (found != map->end() && found->second.object) { return found->second.object; }
    // Both presence and the object result type were independently proved.
    std::terminate();
}
template <class K>
object_value map_get(const std::shared_ptr<map_storage<K, object_value>> & map, const K & key) {
    return map_get(map.get(), key);
}
template <class K>
std::shared_ptr<identity_object> map_get_present_identity(
    const std::shared_ptr<map_storage<K, object_value>> & map, const K & key) {
    return map_get_present_identity(map.get(), key);
}

// --- Map snapshots -----------------------------------------------------------
//
// ONLY THE ORDERED STORAGE HAS `entries`, so a lookup-only module that reaches
// one of these fails to compile rather than acquiring an iteration API whose
// sorted order would differ from JavaScript insertion order. Every snapshot is
// an owning copy: it survives overwrite, deletion and the Map's lifetime.

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
// The numeric snapshot indexing contract is vec_at's: check the tag, truncate,
// then bounds-check. In particular -0.5 indexes zero in the current interpreter.
// ctcompile: scalar consumer of a proved unmodified Map snapshot
template <bool Keys, class K, class V>
nullable_scalar map_snapshot_at(const std::shared_ptr<map_storage<K, V>> & map,
                                nullable_scalar key) {
    if (key.tag != nullable_scalar::kind::number) { return {}; }
    const double index = std::trunc(key.value);
    if (!(index >= 0.0) || index >= static_cast<double>(map->entries.size())) { return {}; }
    const auto & entry = map->entries[static_cast<std::size_t>(index)];
    if constexpr (Keys) {
        return entry.first;
    } else {
        return entry.second;
    }
}

// --- owned globals -----------------------------------------------------------

// ctcompile: read a field of a source-proved owned global
template <auto Field, class T> auto owned_global_get(std::shared_ptr<T> const & object) {
    return object.get()->*Field;
}
// ctcompile: write a field of a source-proved owned global
template <auto Field, class T, class V>
void owned_global_set(std::shared_ptr<T> const & object, V value) {
    object.get()->*Field = value;
}
template <auto Field, class T> auto owned_global_get(T * object) {
    return object->*Field;
}
template <auto Field, class T, class V> void owned_global_set(T * object, V value) {
    object->*Field = value;
}
template <class Table> auto data_map(Table * table) {
    return &table->captured_map;
}

// --- method tables -----------------------------------------------------------

// ctcompile: initialize a proved immutable owning callable field
template <auto Member, class Table, class Callable>
void method_set(const std::shared_ptr<Table> & table, Callable callable) {
    table.get()->*Member = std::move(callable);
}
// ctcompile: copy the owning callable from its proved initialized field
template <auto Member, class Table> auto method_get(const std::shared_ptr<Table> & table) {
    return table.get()->*Member;
}
// ctcompile: invoke a stored callable with its concrete argument signature
template <class Callable, class... Args>
auto invoke_callable(const Callable & callable, Args... args) {
    return callable(args...);
}
// ctcompile: invoke a fixed member without extracting an owning callable
template <auto Member, class Table, class... Args>
auto invoke_session(const std::shared_ptr<Table> & table, Args... args) {
    return (table.get()->*Member)(std::move(args)...);
}

#ifdef CTNATIVE_DOM
// --- the DOM entry -----------------------------------------------------------

// ECMAScript array-index keys precede ordinary keys.
inline std::uint32_t json_property_index(std::string_view key) {
    constexpr auto ordinary = std::numeric_limits<std::uint32_t>::max();
    if (key.empty() || (key.size() > 1 && key.front() == '0')) { return ordinary; }
    std::uint32_t value = ordinary;
    const auto [end, error] = std::from_chars(key.data(), key.data() + key.size(), value);
    return error == std::errc{} && end == key.data() + key.size() ? value : ordinary;
}

// Only proved own-data writes use this. Assignment additionally excludes the
// inherited __proto__ setter; spread defines that name as an own data property.
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

// One fresh target, one traversal of unique keys, and only final own data
// observations. The single __proto__ setter changes no own property. Its
// value has already been evaluated, including any URI/JSON failure path.
inline void assign_json_snapshot_property(ctbrowser::json_value & target, std::string key,
                                          ctbrowser::json_value value) {
    if (key != "__proto__") { set_json_property(target, std::move(key), std::move(value)); }
}

// Source is null/array/object, target is a distinct fresh object, and no
// shallow alias can be observed. Both paths retain first key/last value order.
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
inline void require_dataset_element(ctbrowser::element_ref element) {
    require_element(element);
    // The host contract supplies only HTML/SVG elements. Other namespace URIs
    // still live in Shell and cannot be recovered from a public node_id.
    if (element.owner->read().element_ns(element.id) == ctbrowser::node_ns::other) {
        throw std::invalid_argument("DOM dataset requires a contracted HTML or SVG element");
    }
}
// The source proof fixes /[A-Z]/g and the complete callback. ASCII matches
// occupy one byte even in WTF-8; every unmatched byte is preserved unchanged.
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
// THE SELECTOR ENGINE IS NOT INCLUDED HERE. ctbrowser/style/engine.hpp costs
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
// Named method objects keep generated calls close to their source spelling.
// They hold no state: each call borrows its explicit element and Style engine.
// Browser.hpp adds checked borrowed views for receiver-only calls.
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

// Browser.hpp binds this facade to a borrowed document for one native invocation.
struct document_object {
    std::optional<js_element_t> documentElement() const;
    std::optional<js_element_t> querySelector(const js_string & selector) const;
    std::vector<js_element_t> querySelectorAll(const js_string & selector) const;
};
inline constexpr document_object document{};

// Existing callers retain the same method objects while emission migrates.
inline constexpr const auto & matches = Element.prototype.matches;
inline constexpr const auto & closest = Element.prototype.closest;
inline constexpr const auto & querySelector = Element.prototype.querySelector;
inline constexpr const auto & querySelectorAll = Element.prototype.querySelectorAll;
// Comparison never resolves either borrowed owner; both slot and generation
// belong to identity.
template <> struct map_key_less<ctbrowser::element_ref> {
    bool operator()(ctbrowser::element_ref a, ctbrowser::element_ref b) const noexcept {
        return a.owner == b.owner ? a.id < b.id
                                  : std::less<ctbrowser::document *>{}(a.owner, b.owner);
    }
};
#endif

} // namespace ctnative
