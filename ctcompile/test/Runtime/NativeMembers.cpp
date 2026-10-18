//===- ctcompile/test/Runtime/NativeMembers.cpp - JavaScript spellings ----===//
//
// Tests the operators and JavaScript-named members of the native runtime's
// carriers (CTNative/Runtime/ctnative.hpp, Number.hpp, String.hpp and
// Browser.hpp): `%` and `<=>` on Numbers; a String's length, case, trim,
// split, `[]` and `<=>`; `===` as `==` and ToBoolean as an explicit `bool` on
// every carrier; loose_equals; js_array; and an element's attribute and
// classList members. Each case pins where JavaScript differs from the plain
// C++ reading: NaN and -0, an index past the end reading undefined, UTF-16
// code-unit order rather than byte order, and fmod's signs.
//
//===----------------------------------------------------------------------===//
#define CTNATIVE_DOM 1
#include "ctcompile/CTNative/Runtime/Array.hpp"
#include "ctcompile/CTNative/Runtime/Browser.hpp"
#include "ctcompile/CTNative/Runtime/ctnative.hpp"

#include <cmath>
#include <compare>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "check.hpp"

namespace ctnative {
/// Each generated program defines this with its proved scalar fields.
struct identity_object {};
} // namespace ctnative

namespace {

using ctnative::js_array;
using ctnative::js_boolean_t;
using ctnative::js_num;
using ctnative::js_string;
using ctnative::nullable_scalar;
using ctnative::nullable_string;

constexpr double nan = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

bool is_negative_zero(js_num value) {
    return value.value() == 0.0 && std::signbit(value.value());
}
bool undefined(const nullable_string & value) {
    return value.tag == nullable_string::kind::undefined;
}

void test_remainder_is_fmod() {
    static_assert(std::is_same_v<decltype(js_num{} % js_num{}), js_num>);
    CHECK(js_num{5.5} % js_num{2.0} == js_num{1.5});
    // The dividend's sign, not the divisor's.
    CHECK(js_num{-5.0} % js_num{3.0} == js_num{-2.0});
    CHECK(js_num{5.0} % js_num{-3.0} == js_num{2.0});
    CHECK(is_negative_zero(js_num{-0.0} % js_num{5.0}));
    CHECK(is_negative_zero(js_num{-4.0} % js_num{2.0}));
    CHECK(std::isnan((js_num{5.0} % js_num{0.0}).value()));
    CHECK(std::isnan((js_num{infinity} % js_num{2.0}).value()));
    CHECK(std::isnan((js_num{nan} % js_num{1.0}).value()));
    CHECK(js_num{5.0} % js_num{infinity} == js_num{5.0});
    CHECK(!std::signbit((js_num{0.0} % js_num{-infinity}).value()));
}

void test_string_members() {
    // length counts UTF-16 code units: U+1F600 is two, a lone surrogate one.
    static_assert(std::is_same_v<decltype(js_string{}.length()), js_num>);
    CHECK(js_string{}.length() == js_num{0.0});
    CHECK(js_string{"abc"}.length() == js_num{3.0});
    CHECK(js_string{"\xF0\x9F\x98\x80"}.length() == js_num{2.0});
    CHECK(js_string{"\xED\xA0\xBD"}.length() == js_num{1.0});

    CHECK(js_string{"\xC3\x9F"}.toUpperCase() == js_string{"SS"});
    // Final_Sigma: a capital sigma ending a word lowercases to final sigma.
    CHECK(js_string{"\xCE\x91\xCE\xA3"}.toLowerCase() == js_string{"\xCE\xB1\xCF\x82"});
    CHECK(js_string{"\xC2\xA0\t x \n\xE2\x80\xA8"}.trim() == js_string{"x"});
    CHECK(ctnative::String.prototype.trim.call(js_string{" y "}) == js_string{"y"});

    const auto pieces = js_string{"a,b,,c"}.split(js_string{","});
    CHECK((pieces ==
           std::vector<js_string>{js_string{"a"}, js_string{"b"}, js_string{}, js_string{"c"}}));
    CHECK((js_string{}.split(js_string{","}) == std::vector<js_string>{js_string{}}));
    CHECK(js_string{}.split(js_string{}).empty());
    // An empty separator splits a pair into its two lone surrogates.
    CHECK((js_string{"\xF0\x9F\x98\x80"}.split(js_string{}) ==
           std::vector<js_string>{js_string{"\xED\xA0\xBD"}, js_string{"\xED\xB8\x80"}}));
    CHECK((js_string{" a \t b "}.split(ctnative::whitespace_run) ==
           std::vector<js_string>{js_string{}, js_string{"a"}, js_string{"b"}, js_string{}}));
    CHECK(js_string{"a b"}.split(ctnative::whitespace_run) ==
          ctnative::String.prototype.split.call(js_string{"a b"}, ctnative::whitespace_run));
}

void test_string_index() {
    static_assert(std::is_same_v<decltype(js_string{}[js_num{}]), nullable_string>);
    const js_string text{"abc"};
    CHECK(text[js_num{1.0}] == nullable_string{"b"});
    CHECK(text[js_num{-0.0}] == nullable_string{"a"});
    CHECK(undefined(text[js_num{3.0}]));
    CHECK(undefined(text[js_num{-1.0}]));
    CHECK(undefined(text[js_num{0.5}]));
    CHECK(undefined(text[js_num{nan}]));
    // Index 0 of a pair is its high surrogate, alone.
    CHECK(js_string{"\xF0\x9F\x98\x80"}[js_num{0.0}] == nullable_string{"\xED\xA0\xBD"});
}

void test_number_order() {
    // `<=>` is a partial order: NaN is unordered, so every relational
    // operator on it is false, as JavaScript's are; -0 and +0 are equal.
    static_assert(std::is_same_v<decltype(js_num{} <=> js_num{}), std::partial_ordering>);
    const js_num one{1.0}, two{2.0}, nan_number{nan}, zero{0.0}, negative_zero{-0.0};
    CHECK(one < two && two > one && one <= one && one >= one);
    CHECK((nan_number <=> one) == std::partial_ordering::unordered);
    CHECK(!(nan_number < one) && !(nan_number > one) && !(nan_number <= one) &&
          !(nan_number >= one));
    CHECK(!(nan_number <= nan_number) && !(nan_number >= nan_number));
    CHECK(!(negative_zero < zero) && negative_zero <= zero && negative_zero >= zero);
    CHECK(js_num{-infinity} < one && one < js_num{infinity});
}

void test_string_order() {
    static_assert(std::is_same_v<decltype(js_string{} <=> js_string{}), std::strong_ordering>);
    const js_string a{"a"}, b{"b"}, aa{"aa"}, empty{};
    CHECK(a < b && !(b < a) && b > a);
    CHECK(a < aa && empty < a && !(a < empty));
    CHECK(a <= a && a >= a && !(a < a));
    // U+1F600 is D83D DE00 in UTF-16 and U+FF21 is FF21: the pair sorts first
    // in code units, though its WTF-8 lead byte F0 is above EF.
    const js_string grin{"\xF0\x9F\x98\x80"}, fullwidth{"\xEF\xBC\xA1"};
    CHECK(grin < fullwidth);
    CHECK(!(fullwidth < grin));
    CHECK(ctnative::detail::string_less(grin, fullwidth));
}

// `==` is strict equality, so two carriers of different kinds have none: loose
// equality, which coerces, is the named loose_equals.
template <class L, class R>
concept equality_comparable = requires(const L & left, const R & right) { left == right; };

void test_strict_equality_on_every_carrier() {
    static_assert(!equality_comparable<js_string, js_num>);
    static_assert(!equality_comparable<js_num, js_string>);
    static_assert(!equality_comparable<js_boolean_t, js_num>);
    CHECK(!(js_num{nan} == js_num{nan}) && js_num{-0.0} == js_num{0.0});
    CHECK(js_boolean_t{true} == js_boolean_t{true} && !(js_boolean_t{true} == js_boolean_t{}));
    CHECK(js_string{"a"} == js_string{"a"} && !(js_string{"a"} == js_string{"A"}));

    nullable_string null;
    null.tag = nullable_string::kind::null_value;
    CHECK(nullable_string{} == nullable_string{} && null == null);
    CHECK(!(null == nullable_string{}) && !(nullable_string{std::string{}} == nullable_string{}));

    using ctnative::object_value;
    static_assert(!std::is_convertible_v<object_value, bool>);
    const auto object = std::make_shared<ctnative::identity_object>();
    const object_value same{object}, alias{object};
    const object_value other{std::make_shared<ctnative::identity_object>()};
    CHECK(same == alias && !(same == other));
    CHECK(!(same == object_value{nullable_scalar::null()}));
    CHECK(object_value{js_num{-0.0}} == object_value{js_num{0.0}});
    CHECK(!(object_value{js_num{nan}} == object_value{js_num{nan}}));
    CHECK(static_cast<bool>(same) && !static_cast<bool>(object_value{}) &&
          !static_cast<bool>(object_value{js_num{nan}}));
    CHECK(ctnative::object_strict_equal(same, alias) && ctnative::object_truthy(other));
}

void test_loose_equals() {
    using ctnative::loose_equals;
    using ctnative::object_value;
    // Not an equivalence: '0' == 0 and 0 == '', but '0' != ''.
    CHECK(loose_equals(js_string{"0"}, js_num{0.0}));
    CHECK(loose_equals(js_num{0.0}, js_string{}));
    CHECK(!loose_equals(js_string{"0"}, js_string{}));
    CHECK(loose_equals(nullable_scalar::null(), nullable_scalar{}));
    CHECK(!loose_equals(nullable_scalar::null(), js_num{0.0}));
    CHECK(!loose_equals(js_num{nan}, js_num{nan}));
    CHECK(loose_equals(js_boolean_t{true}, js_string{"1"}));
    const auto object = std::make_shared<ctnative::identity_object>();
    CHECK(loose_equals(object_value{object}, object_value{object}));
    CHECK(!loose_equals(object_value{object}, object_value{nullable_scalar::null()}));
    CHECK(loose_equals(object_value{nullable_scalar::null()}, object_value{nullable_scalar{}}));
}

void test_to_string() {
    // Number::toString: the shortest round trip, the exponent from 1e21 on,
    // and "0" for -0.
    CHECK(js_num{1e21}.toString() == js_string{"1e+21"});
    CHECK(js_num{1e20}.toString() == js_string{"100000000000000000000"});
    CHECK(js_num{-0.0}.toString() == js_string{"0"});
    CHECK(js_num{nan}.toString() == js_string{"NaN"});
    CHECK(js_num{-infinity}.toString() == js_string{"-Infinity"});
    CHECK(js_num{0.1}.toString() == js_string{"0.1"});
    CHECK(js_num{1e-7}.toString() == js_string{"1e-7"});
    CHECK(js_num{123.456}.toString() == js_string{"123.456"});
    CHECK(js_boolean_t{false}.toString() == js_string{"false"});
    CHECK(js_string{"s"}.toString() == js_string{"s"});

    // String(x) of every primitive carrier.
    using ctnative::String;
    nullable_string null_text;
    null_text.tag = nullable_string::kind::null_value;
    CHECK(String(js_num{-0.0}) == js_string{"0"});
    CHECK(String(js_boolean_t{true}) == js_string{"true"});
    CHECK(String(nullable_scalar{}) == js_string{"undefined"});
    CHECK(String(nullable_scalar::null()) == js_string{"null"});
    CHECK(String(nullable_scalar{1e21}) == js_string{"1e+21"});
    CHECK(String(null_text) == js_string{"null"} &&
          String(nullable_string{}) == js_string{"undefined"});
    CHECK(String(ctnative::number_string{js_num{0.5}}) == js_string{"0.5"});
    CHECK(String(ctnative::js_null_t{}) == js_string{"null"});
    CHECK(String(js_string{"x"}) == js_string{"x"});
}

void test_to_fixed() {
    // Ties round away from zero on the exact binary value: 1.005 is below
    // the tie, 2.5 is on it.
    CHECK(js_num{1.005}.toFixed(js_num{2.0}) == js_string{"1.00"});
    CHECK(js_num{2.5}.toFixed(js_num{0.0}) == js_string{"3"});
    CHECK(js_num{-2.5}.toFixed(js_num{0.0}) == js_string{"-3"});
    CHECK(js_num{-0.0}.toFixed(js_num{2.0}) == js_string{"0.00"});
    CHECK(js_num{1e21}.toFixed(js_num{2.0}) == js_string{"1e+21"});
    CHECK(js_num{nan}.toFixed(js_num{2.0}) == js_string{"NaN"});
    // ToIntegerOrInfinity: NaN digits are 0, fractions truncate.
    CHECK(js_num{1.5}.toFixed(js_num{nan}) == js_string{"2"});
    CHECK(js_num{1.25}.toFixed(js_num{1.9}) == js_string{"1.3"});
    const auto range_error = [](double digits) {
        try {
            (void)js_num{1.0}.toFixed(js_num{digits});
        } catch (const ctnative::range_error &) { return true; }
        return false;
    };
    CHECK(range_error(101.0) && range_error(-1.0) && range_error(infinity));
    CHECK(!range_error(100.0) && !range_error(-0.5));
}

void test_parse_globals() {
    using ctnative::parseFloat;
    using ctnative::parseInt;
    CHECK(parseInt(js_string{"08"}) == js_num{8.0});
    CHECK(parseInt(js_string{" 0x1F"}) == js_num{31.0});
    CHECK(parseInt(js_string{"1e3"}) == js_num{1.0});
    CHECK(parseInt(js_string{"10"}, js_num{2.0}) == js_num{2.0});
    CHECK(std::isnan(parseInt(js_string{"12"}, js_num{37.0}).value()));
    CHECK(parseFloat(js_string{"3.14abc"}) == js_num{3.14});
    CHECK(parseFloat(js_string{".5"}) == js_num{0.5});
    CHECK(std::isnan(parseFloat(js_string{"abc"}).value()));
    CHECK(parseInt(js_string{"7"}) == ctnative::Number.parseInt(js_string{"7"}));
}

template <class L, class R>
concept addable = requires(const L & left, const R & right) { left + right; };

void test_mixed_kind_operators() {
    static_assert(std::is_same_v<decltype(js_string{} * js_num{}), js_num>);
    static_assert(std::is_same_v<decltype(js_string{} + nullable_scalar{}), js_string>);
    // A carrier that may hold a String has no `+`: whether it concatenates is
    // only known at run time, so it stays ctnative::add.
    static_assert(!addable<nullable_string, js_num> && !addable<js_num, nullable_string>);
    const nullable_scalar null = nullable_scalar::null(), undef{};
    CHECK(js_string{"3"} * js_num{2.0} == js_num{6.0});
    CHECK(js_string{"10"} - js_string{"4"} == js_num{6.0});
    CHECK(js_boolean_t{true} - js_string{"1"} == js_num{0.0});
    CHECK(js_string{" 12 "} / js_num{4.0} == js_num{3.0});
    CHECK(js_string{"7"} % js_string{"4"} == js_num{3.0});
    CHECK(js_string{""} * js_num{1.0} == js_num{0.0});
    CHECK(std::isnan((js_string{"a"} * js_num{1.0}).value()));
    CHECK(std::isnan((null - undef).value()));
    CHECK(null + js_num{1.0} == js_num{1.0});
    CHECK(std::isnan((undef + js_num{1.0}).value()));
    CHECK(js_boolean_t{true} + js_boolean_t{true} == js_num{2.0});

    CHECK(js_string{"a"} + null == js_string{"anull"});
    CHECK(js_string{"a"} + undef == js_string{"aundefined"});
    CHECK(null + js_string{"b"} == js_string{"nullb"});
    CHECK(js_string{"a"} + ctnative::js_null_t{} == js_string{"anull"});
    nullable_string null_text;
    null_text.tag = nullable_string::kind::null_value;
    CHECK(js_string{"a"} + null_text == js_string{"anull"});
    CHECK(js_string{"x"} + ctnative::number_string{js_num{1.0}} == js_string{"x1"});

    // Relational operators apply ToNumber unless both are Strings.
    CHECK(!(js_string{"10"} < js_num{9.0}) && js_string{"10"} > js_num{9.0});
    CHECK(js_string{"10"} < js_string{"9"});
    CHECK(null < js_num{1.0} && js_string{""} < js_num{1.0});
    CHECK(!(undef < js_num{1.0}) && !(undef > js_num{1.0}) && !(undef >= js_num{0.0}));
    CHECK(!(js_string{"abc"} < js_num{1.0}) && !(js_string{"abc"} >= js_num{1.0}));
    CHECK(js_boolean_t{true} > js_boolean_t{false});
    CHECK(js_string{"-0"} <= js_num{0.0} && !(js_string{"-0"} < js_num{0.0}));
}

void test_scalar_truthiness_and_equality() {
    static_assert(!std::is_convertible_v<nullable_scalar, bool>);
    const nullable_scalar undef{}, null = nullable_scalar::null();
    CHECK(!static_cast<bool>(undef) && !static_cast<bool>(null));
    CHECK(!static_cast<bool>(nullable_scalar{0.0}));
    CHECK(!static_cast<bool>(nullable_scalar{-0.0}));
    CHECK(!static_cast<bool>(nullable_scalar{nan}));
    CHECK(!static_cast<bool>(nullable_scalar{js_boolean_t{false}}));
    CHECK(static_cast<bool>(nullable_scalar{1.0}) &&
          static_cast<bool>(nullable_scalar{js_boolean_t{true}}));
    CHECK(ctnative::scalar_truthy(nullable_scalar{2.0}) && !ctnative::scalar_truthy(null));

    CHECK(undef == nullable_scalar{} && null == nullable_scalar::null());
    CHECK(!(undef == null));
    CHECK(!(nullable_scalar{nan} == nullable_scalar{nan}));
    CHECK(nullable_scalar{-0.0} == nullable_scalar{0.0});
    // 1 !== true: a Number and a Boolean never compare equal.
    CHECK(!(nullable_scalar{1.0} == nullable_scalar{js_boolean_t{true}}));
    CHECK(nullable_scalar{js_boolean_t{true}} == nullable_scalar{js_boolean_t{true}});
    CHECK(ctnative::scalar_strict_equal(nullable_scalar{-0.0}, nullable_scalar{0.0}));
    CHECK(!ctnative::scalar_strict_equal(nullable_scalar{nan}, nullable_scalar{nan}));
}

void test_string_carrier_truthiness() {
    static_assert(!std::is_convertible_v<nullable_string, bool>);
    nullable_string null;
    null.tag = nullable_string::kind::null_value;
    CHECK(!static_cast<bool>(nullable_string{}) && !static_cast<bool>(null));
    CHECK(!static_cast<bool>(nullable_string{std::string{}}));
    CHECK(static_cast<bool>(nullable_string{"0"}) && static_cast<bool>(nullable_string{" "}));
    CHECK(ctnative::string_truthy(nullable_string{"x"}) && !ctnative::string_truthy(null));

    using ctnative::number_string;
    static_assert(!std::is_convertible_v<number_string, bool>);
    CHECK(!static_cast<bool>(number_string{js_num{0.0}}));
    CHECK(!static_cast<bool>(number_string{js_num{nan}}));
    CHECK(!static_cast<bool>(number_string{js_string{}}));
    CHECK(static_cast<bool>(number_string{js_string{"0"}}));
    CHECK(static_cast<bool>(number_string{js_num{-1.0}}));
    CHECK(!static_cast<bool>(ctnative::nullable_number_string{ctnative::js_null_t{}}));
    // `===` keeps the kinds apart and NaN unequal to itself.
    CHECK(!(number_string{js_num{1.0}} == number_string{js_string{"1"}}));
    CHECK(!(number_string{js_num{nan}} == number_string{js_num{nan}}));
}

void test_array_members() {
    static_assert(std::is_constructible_v<js_array<double>, std::vector<double>>);
    static_assert(!std::is_convertible_v<std::vector<double>, js_array<double>>);
    static_assert(std::is_same_v<decltype(js_array<double>{}.push(js_num{})), js_num>);
    js_array<double> numbers;
    CHECK(numbers.length() == js_num{0.0});
    CHECK(numbers.push(js_num{-0.0}) == js_num{1.0});
    CHECK(numbers.push(js_num{nan}) == js_num{2.0});
    CHECK(numbers.length() == js_num{2.0});
    // The stored -0 and NaN read back as themselves.
    CHECK(numbers[js_num{0.0}].tag == nullable_scalar::kind::number &&
          std::signbit(numbers[js_num{0.0}].value));
    CHECK(std::isnan(numbers[js_num{1.0}].value));
    // -0 names index 0; past the end, fractions, negatives, NaN and null name
    // no element, so the read is undefined, never NaN.
    CHECK(numbers[js_num{-0.0}].tag == nullable_scalar::kind::number);
    CHECK(numbers[js_num{2.0}].tag == nullable_scalar::kind::undefined);
    CHECK(numbers[js_num{0.5}].tag == nullable_scalar::kind::undefined);
    CHECK(numbers[js_num{-1.0}].tag == nullable_scalar::kind::undefined);
    CHECK(numbers[js_num{nan}].tag == nullable_scalar::kind::undefined);
    CHECK(numbers[nullable_scalar::null()].tag == nullable_scalar::kind::undefined);
    // It is still a std::vector to the C++ algorithms and to the free helpers.
    CHECK(numbers.size() == 2 && ctnative::vec_length(numbers) == numbers.length());

    js_array<js_string> strings{std::vector<js_string>{js_string{"a"}}};
    CHECK(strings.push(js_string{"b"}) == js_num{2.0});
    CHECK(strings[js_num{1.0}] == nullable_string{"b"});
    CHECK(undefined(strings[js_num{2.0}]));
}

void test_element_members() {
    using ctnative::js_element_t;
    ctbrowser::atom_table atoms;
    ctbrowser::document dom{atoms};
    ctbrowser::style::engine styles{atoms};
    const auto root = dom.create_element(atoms.intern("html"));
    dom.set_document_element(root);
    const auto button = dom.create_element(atoms.intern("button"));
    dom.append_child(root, button).value();
    const js_element_t element{{&dom, button}, styles};

    CHECK(!element.hasAttribute(js_string{"hidden"}));
    CHECK(element.toggleAttribute(js_string{"hidden"}));
    CHECK(element.hasAttribute(js_string{"hidden"}));
    CHECK(element.getAttribute(js_string{"hidden"}) == nullable_string{""});
    CHECK(!element.toggleAttribute(js_string{"hidden"}));
    CHECK(element.toggleAttribute(js_string{"hidden"}, js_boolean_t{true}));
    CHECK(element.toggleAttribute(js_string{"hidden"}, js_boolean_t{true}));
    CHECK(!element.toggleAttribute(js_string{"hidden"}, js_boolean_t{false}));
    CHECK(!element.hasAttribute(js_string{"hidden"}));

    const auto classes = element.classList();
    CHECK(!classes.contains(js_string{"a"}));
    classes.add(js_string{"a"}, js_string{"b"}, js_string{"a"});
    CHECK(element.className() == js_string{"a b"});
    CHECK(classes.contains(js_string{"b"}));
    CHECK(!classes.toggle(js_string{"a"}));
    CHECK(classes.toggle(js_string{"c"}));
    CHECK(element.className() == js_string{"b c"});
    CHECK(classes.toggle(js_string{"c"}, js_boolean_t{true}));
    CHECK(!classes.toggle(js_string{"d"}, js_boolean_t{false}));
    classes.remove(js_string{"b"}, js_string{"missing"});
    CHECK(element.className() == js_string{"c"});
    CHECK(element.classList().contains(js_string{"c"}));
}

} // namespace

int main() {
    test_remainder_is_fmod();
    test_string_members();
    test_string_index();
    test_number_order();
    test_string_order();
    test_strict_equality_on_every_carrier();
    test_loose_equals();
    test_to_string();
    test_to_fixed();
    test_parse_globals();
    test_mixed_kind_operators();
    test_scalar_truthiness_and_equality();
    test_string_carrier_truthiness();
    test_array_members();
    test_element_members();
    REPORT("native_members");
}
