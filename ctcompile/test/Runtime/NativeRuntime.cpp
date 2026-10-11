//===- ctcompile/test/Runtime/NativeRuntime.cpp - native runtime checks ---===//
//
// Compiles and runs ctcompile/CTNative/Runtime/ctnative.hpp exactly as a
// generated program includes it, with CTNATIVE_DOM and CTNATIVE_ORDERED_MAPS
// on, under -Werror, so a warning or contract break in a runtime helper fails
// here instead of in whichever native test happens to use it.
//
// The checks are the contracts a generated program relies on and no lit test
// pins directly: an out-of-range index is undefined, a NaN key is one key, -0
// is stored as +0, a snapshot keeps insertion order across delete and
// reinsert, and null == undefined but null !== undefined.
//
//===----------------------------------------------------------------------===//
#define CTNATIVE_DOM 1
#define CTNATIVE_ORDERED_MAPS 1
#include "ctcompile/CTNative/Runtime/Array.hpp"
#include "ctcompile/CTNative/Runtime/Browser.hpp"
#include "ctcompile/CTNative/Runtime/Json.hpp"
#include "ctcompile/CTNative/Runtime/ctnative.hpp"

#include <cstdio>
#include <cstdlib>
#include <thread>

#include "check.hpp"

namespace ctnative {
struct identity_object {};
} // namespace ctnative

namespace {
template <class T>
concept string_addable = requires(ctnative::js_string text, T value) {
    text + value;
    value + text;
};
template <class T>
concept number_bitwise_operand = requires(ctnative::js_num number, T value) {
    number & value;
    value & number;
    number | value;
    value | number;
    number ^ value;
    value ^ number;
    number << value;
    value << number;
    number >> value;
    value >> number;
    number.unsigned_shift_right(value);
};
template <class T>
concept primitive_addable = requires(ctnative::number_string result, const T & value) {
    ctnative::add(result, value);
    ctnative::add(value, result);
};
template <class T>
concept primitive_equality_comparable = requires(ctnative::js_string text, const T & value) {
    ctnative::primitive_equal(text, value);
    ctnative::primitive_equal(value, text);
    ctnative::primitive_strict_equal(text, value);
    ctnative::primitive_strict_equal(value, text);
};
struct convertible_number {
    operator double() const { return 3.0; }
};
enum unrelated_number {
    three = 3
};
} // namespace

// The build is -DNDEBUG, so assert() would check nothing: every line is a CHECK().
int main() {
    using namespace ctnative;
    static_assert(std::is_same_v<ctnative::js_num, js_basic_num<double>>);
    static_assert(std::is_constructible_v<ctnative::js_num, double>);
    static_assert(!std::is_convertible_v<double, ctnative::js_num>);
    static_assert(!std::is_convertible_v<ctnative::js_num, double>);
    static_assert(!std::is_convertible_v<ctnative::js_num, bool>);
    static_assert(!std::is_constructible_v<ctnative::js_num, bool>);
    static_assert(!std::is_constructible_v<ctnative::js_num, int>);
    static_assert(!std::is_constructible_v<ctnative::js_num, const char *>);
    static_assert(std::is_constructible_v<ctnative::js_num, js_nan_t>);
    static_assert(!std::is_convertible_v<js_nan_t, ctnative::js_num>);
    static_assert(std::is_same_v<decltype(global_number(nullable_scalar{})), ctnative::js_num>);
    static_assert(std::is_same_v<decltype(to_number(nullable_scalar{})), ctnative::js_num>);
    constexpr ctnative::js_num zero{}, one{1.0}, two{2.0}, negativeZero{-0.0};
    static_assert((one + two).value() == 3.0 && (one - two).value() == -1.0);
    static_assert((two * two).value() == 4.0 && (one / two).value() == 0.5);
    static_assert((-one).value() == -1.0 && static_cast<double>(+two) == 2.0);
    constexpr ctnative::js_num notANumber{js_nan_t{}};
    CHECK(!zero && !negativeZero && !notANumber && one);
    CHECK(zero == negativeZero && !std::signbit(zero.value()));
    CHECK(std::signbit(negativeZero.value()) && std::signbit((-zero).value()));
    CHECK(notANumber != notANumber && (notANumber <=> one) == std::partial_ordering::unordered);
    CHECK(one < two && two >= one);
    CHECK(std::isinf((one / zero).value()) && std::isnan((zero / zero).value()));
    CHECK(std::signbit((one / negativeZero).value()));
    CHECK(std::signbit(global_number(to_nullable(negativeZero)).value()));
    CHECK(std::signbit(to_number(to_nullable(negativeZero)).value()));
    CHECK(!std::signbit(to_number(to_nullable(zero)).value()));
    CHECK(std::isnan(global_number(object_value{notANumber}).value()));
    CHECK(global_number(object_value{two}) == two);
    CHECK(!scalar_strict_equal(to_nullable(one), nullable_scalar{true}));

    static_assert(number_bitwise_operand<ctnative::js_num>);
    static_assert(!number_bitwise_operand<int> && !number_bitwise_operand<double>);
    static_assert(!number_bitwise_operand<bool> && !number_bitwise_operand<js_boolean_t>);
    static_assert(!number_bitwise_operand<js_string> && !number_bitwise_operand<nullable_scalar>);
    static_assert(!number_bitwise_operand<convertible_number>);
    static_assert(std::is_same_v<decltype(one.to_int32()), std::int32_t>);
    static_assert(std::is_same_v<decltype(one.to_uint32()), std::uint32_t>);
    static_assert(std::is_same_v<decltype(~one), ctnative::js_num>);
    static_assert(std::is_same_v<decltype(one & two), ctnative::js_num>);
    static_assert(std::is_same_v<decltype(one | two), ctnative::js_num>);
    static_assert(std::is_same_v<decltype(one ^ two), ctnative::js_num>);
    static_assert(std::is_same_v<decltype(one << two), ctnative::js_num>);
    static_assert(std::is_same_v<decltype(one >> two), ctnative::js_num>);
    static_assert(std::is_same_v<decltype(one.unsigned_shift_right(two)), ctnative::js_num>);
    struct integer_conversion {
        double input;
        std::int32_t signed_result;
        std::uint32_t unsigned_result;
    };
    const integer_conversion integer_cases[] = {
        {0.0, 0, 0},
        {-0.0, 0, 0},
        {std::numeric_limits<double>::quiet_NaN(), 0, 0},
        {std::numeric_limits<double>::infinity(), 0, 0},
        {-std::numeric_limits<double>::infinity(), 0, 0},
        {std::numeric_limits<double>::max(), 0, 0},
        {-std::numeric_limits<double>::max(), 0, 0},
        {std::numeric_limits<double>::denorm_min(), 0, 0},
        {-std::numeric_limits<double>::denorm_min(), 0, 0},
        {0.9, 0, 0},
        {-0.9, 0, 0},
        {1.9, 1, 1},
        {-1.9, -1, 4294967295u},
        {2147483647.9, 2147483647, 2147483647u},
        {2147483648.0, -2147483647 - 1, 2147483648u},
        {-2147483648.0, -2147483647 - 1, 2147483648u},
        {-2147483649.0, 2147483647, 2147483647u},
        {4294967295.0, -1, 4294967295u},
        {4294967296.0, 0, 0},
        {-4294967296.0, 0, 0},
        {4294967297.9, 1, 1},
        {-4294967297.9, -1, 4294967295u},
        {9007199254740991.0, -1, 4294967295u},
    };
    for (const auto & test : integer_cases) {
        CHECK(ctnative::js_num{test.input}.to_int32() == test.signed_result);
        CHECK(ctnative::js_num{test.input}.to_uint32() == test.unsigned_result);
    }
    CHECK(~one == ctnative::js_num{-2.0} && ~notANumber == -one);
    CHECK((ctnative::js_num{6.9} & ctnative::js_num{3.9}) == two);
    CHECK((ctnative::js_num{6.0} | ctnative::js_num{3.0}) == ctnative::js_num{7.0});
    CHECK((ctnative::js_num{6.0} ^ ctnative::js_num{3.0}) == ctnative::js_num{5.0});
    CHECK((ctnative::js_num{4294967295.0} | zero) == -one);
    CHECK((notANumber & -one) == zero && (negativeZero ^ notANumber) == zero);
    CHECK(!std::signbit((negativeZero | zero).value()));
    CHECK((one << ctnative::js_num{31.0}) == ctnative::js_num{-2147483648.0});
    CHECK((-one << one) == ctnative::js_num{-2.0});
    CHECK((one << ctnative::js_num{33.0}) == two);
    CHECK((one << -one) == ctnative::js_num{-2147483648.0});
    CHECK((one << ctnative::js_num{-32.0}) == one);
    CHECK((one << ctnative::js_num{4294967329.0}) == two);
    CHECK((one << ctnative::js_num{1.9}) == two);
    CHECK((one << ctnative::js_num{-1.9}) == ctnative::js_num{-2147483648.0});
    CHECK((one << notANumber) == one);
    CHECK((one << ctnative::js_num{std::numeric_limits<double>::infinity()}) == one);
    CHECK((ctnative::js_num{-3.0} >> one) == ctnative::js_num{-2.0});
    CHECK((ctnative::js_num{2147483648.0} >> ctnative::js_num{31.0}) == -one);
    CHECK((ctnative::js_num{4294967296.0} >> zero) == zero);
    CHECK((-one).unsigned_shift_right(zero) == ctnative::js_num{4294967295.0});
    CHECK((-one).unsigned_shift_right(one) == ctnative::js_num{2147483647.0});
    CHECK((-one).unsigned_shift_right(-one) == one);
    CHECK((-one).unsigned_shift_right(ctnative::js_num{32.0}) == ctnative::js_num{4294967295.0});
    CHECK(notANumber.unsigned_shift_right(two) == zero);
    CHECK(!std::signbit(negativeZero.unsigned_shift_right(zero).value()));

    static_assert(std::is_constructible_v<js_boolean_t, bool>);
    static_assert(!std::is_convertible_v<bool, js_boolean_t>);
    static_assert(!std::is_convertible_v<js_boolean_t, bool>);
    static_assert(!std::is_constructible_v<double, js_boolean_t>);
    static_assert(!std::is_constructible_v<js_boolean_t, int>);
    static_assert(!std::is_constructible_v<js_boolean_t, double>);
    static_assert(!std::is_constructible_v<js_boolean_t, const char *>);
    static_assert(std::is_same_v<decltype(js_boolean_t{} == js_boolean_t{}), bool>);
    constexpr js_boolean_t enabled{true}, disabled{false};
    static_assert(std::is_same_v<decltype(enabled.to_number()), ctnative::js_num>);
    static_assert(std::is_same_v<decltype(to_number(disabled)), ctnative::js_num>);
    static_assert(enabled.to_number().value() == 1.0 && to_number(disabled).value() == 0.0);
    CHECK(enabled && !disabled && enabled != disabled);
    CHECK(!std::signbit(to_number(disabled).value()));
    CHECK(to_number(to_nullable(enabled)) == one && to_number(to_nullable(disabled)) == zero);
    CHECK(global_boolean(to_nullable(enabled)) == enabled);
    CHECK(to_nullable(disabled).tag == nullable_scalar::kind::boolean);
    CHECK(scalar_equal(enabled, 1.0) && !scalar_strict_equal(enabled, 1.0));
    CHECK(object_strict_equal(object_value{enabled}, object_value{true}));
    CHECK(!object_strict_equal(object_value{enabled}, object_value{1.0}));
    CHECK(boolean_string_truthy(std::variant<js_boolean_t, std::string>{enabled}));
    CHECK(!boolean_string_truthy(std::variant<js_boolean_t, std::string>{disabled}));
    CHECK(!boolean_string_truthy(std::variant<js_boolean_t, std::string>{std::string{}}));
    CHECK(boolean_string_truthy(std::variant<js_boolean_t, std::string>{std::string{"false"}}));
    CHECK(boolean_string_text(std::variant<js_boolean_t, std::string>{enabled}) ==
          js_string{"true"});
    CHECK(boolean_string_text(std::variant<js_boolean_t, std::string>{disabled}) ==
          js_string{"false"});
    CHECK(boolean_string_text(std::variant<js_boolean_t, std::string>{std::string{}}) ==
          js_string{});
    const std::variant<js_boolean_t, std::string> binaryText{std::string{"a\0b", 3}};
    CHECK(boolean_string_text(binaryText) == js_string{"a\0b"});
    CHECK(std::holds_alternative<std::string>(binaryText));
    static_assert(std::is_same_v<decltype(to_number(binaryText)), ctnative::js_num>);
    CHECK(to_number(std::variant<js_boolean_t, std::string>{enabled}) == one);
    const auto unionZero = to_number(std::variant<js_boolean_t, std::string>{disabled});
    CHECK(unionZero == zero && !std::signbit(unionZero.value()));
    CHECK(to_number(std::variant<js_boolean_t, std::string>{std::string{}}) == zero);
    CHECK(std::signbit(
        to_number(std::variant<js_boolean_t, std::string>{std::string{"-0"}}).value()));
    CHECK(std::isnan(to_number(binaryText).value()));

    const std::string high = "\xED\xA0\xBD", low = "\xED\xB8\x80";
    static_assert(std::is_same_v<js_string, js_basic_string<char>>);
    static_assert(std::is_constructible_v<js_string, std::string>);
    static_assert(std::is_constructible_v<js_string, std::string_view>);
    static_assert(!std::is_convertible_v<std::string, js_string>);
    static_assert(!std::is_convertible_v<std::string_view, js_string>);
    static_assert(!std::is_convertible_v<js_string, std::string>);
    static_assert(!std::is_convertible_v<js_string, std::string_view>);
    static_assert(!std::is_convertible_v<js_string, bool>);
    static_assert(!std::is_constructible_v<js_string, bool>);
    static_assert(!std::is_constructible_v<js_string, double>);
    static_assert(!std::is_constructible_v<js_string, ctnative::js_num>);
    static_assert(
        std::is_same_v<decltype(std::declval<const js_string &>().value()), const std::string &>);
    static_assert(std::is_same_v<decltype(std::declval<js_string &&>().value()), std::string>);
    static_assert(std::is_same_v<decltype(js_string{} == js_string{}), bool>);
    static_assert(std::is_same_v<decltype(js_string{} + js_string{}), js_string>);
    static_assert(std::is_same_v<decltype(js_string{}.to_number()), ctnative::js_num>);
    static_assert(std::is_same_v<decltype(js_string{} + two), js_string>);
    static_assert(std::is_same_v<decltype(two + js_string{}), js_string>);
    CHECK(js_string{"  0x10\n"}.to_number() == ctnative::js_num{16.0});
    CHECK(js_string{}.to_number() == ctnative::js_num{0.0});
    CHECK(std::signbit(js_string{"-0"}.to_number().value()));
    CHECK(std::isnan(js_string{"1\0"}.to_number().value()));
    CHECK(std::isnan(js_string{high}.to_number().value()));
    CHECK((js_string{"b"} + js_string{"a"} + js_string{"a"}.to_number() + js_string{"a"}) ==
          js_string{"baNaNa"});
    CHECK((js_string{"x"} + negativeZero) == js_string{"x0"});
    CHECK((two + js_string{"\0x"}) == js_string{"2\0x"});
    static_assert(!string_addable<const char *>);
    static_assert(!string_addable<std::nullptr_t>);
    static_assert(!string_addable<convertible_number>);
    static_assert(!string_addable<unrelated_number>);
    const auto checkPrimitiveConcat = []<class U>(U value, std::string_view expected) {
        static_assert(std::is_same_v<decltype(js_string{} + value), js_string>);
        static_assert(std::is_same_v<decltype(value + js_string{}), js_string>);
        CHECK((js_string{"x"} + value).value() == "x" + std::string{expected});
        CHECK((value + js_string{"x"}).value() == std::string{expected} + "x");
    };
    checkPrimitiveConcat(short{-3}, "-3");
    checkPrimitiveConcat(42, "42");
    checkPrimitiveConcat(42L, "42");
    checkPrimitiveConcat(9007199254740993LL, "9007199254740992");
    checkPrimitiveConcat(42U, "42");
    checkPrimitiveConcat(42UL, "42");
    checkPrimitiveConcat(42ULL, "42");
    checkPrimitiveConcat(2.5F, "2.5");
    checkPrimitiveConcat(2.5, "2.5");
    checkPrimitiveConcat(2.5L, "2.5");
    checkPrimitiveConcat(-0.0, "0");
    checkPrimitiveConcat(std::numeric_limits<double>::quiet_NaN(), "NaN");
    if constexpr (std::numeric_limits<long double>::max() > std::numeric_limits<double>::max()) {
        checkPrimitiveConcat(std::numeric_limits<long double>::max(), "Infinity");
        checkPrimitiveConcat(-std::numeric_limits<long double>::max(), "-Infinity");
    }
    checkPrimitiveConcat(true, "true");
    checkPrimitiveConcat(false, "false");
    checkPrimitiveConcat(js_boolean_t{true}, "true");
    checkPrimitiveConcat(js_boolean_t{false}, "false");
    CHECK(nullable_scalar{undefined_t{}}.to_string() == js_string{"undefined"});
    CHECK(nullable_scalar{js_null_t{}}.to_string() == js_string{"null"});
    CHECK(nullable_scalar{js_boolean_t{false}}.to_string() == js_string{"false"});
    CHECK(nullable_scalar{js_boolean_t{true}}.to_string() == js_string{"true"});
    CHECK(nullable_scalar{-0.0}.to_string() == js_string{"0"});
    CHECK(nullable_scalar{notANumber}.to_string() == js_string{"NaN"});
    CHECK(!js_string{} && js_string{"false"} && js_string{"\0"});
    CHECK(js_string{"a\0b"}.value() == std::string("a\0b", 3));
    CHECK((js_string{"a\0b", 3} == js_string{"a\0b"}));
    std::string borrowed = "original";
    const js_string owned{std::string_view{borrowed}};
    borrowed[0] = 'x';
    CHECK(owned == js_string{"original"});
    const js_string highUnit{high}, lowUnit{low};
    CHECK((highUnit + lowUnit) == js_string{"\xF0\x9F\x98\x80"});
    CHECK(highUnit.value() == high && lowUnit.value() == low);
    CHECK((highUnit + js_string{"x"}).value() == high + "x");
    CHECK((js_string{"x"} + lowUnit).value() == "x" + low);
    CHECK((js_string{"a\0"} + js_string{"b"}) == js_string{"a\0b"});
    CHECK(js_string{"bs\xF0\x9F\x98\x80"}.startsWith(js_string{"bs"}));
    CHECK(!js_string{"\xF0\x9F\x98\x80"}.startsWith(js_string{"bs"}));
    CHECK(js_string{}.startsWith(js_string{}));
    CHECK(js_string{"a\0b"}.startsWith(js_string{"a\0"}));
    CHECK(string_concat(high, low) == "\xF0\x9F\x98\x80");
    CHECK(string_concat(high, "x") == high + "x");
    CHECK(string_concat("x", low) == "x" + low);
    CHECK(string_concat(std::string("a\0", 2), "b") == std::string("a\0b", 3));
    CHECK(high.size() == 3 && low.size() == 3);
    // `length` is UTF-16 code units: a pair's code point is two, a lone half one.
    CHECK(string_length(std::string("\xC3\xA9\xF0\x9F\x98\x80\0", 7)) == 4);
    CHECK(string_length(high) == 1 && string_length(high + low) == 2);
    CHECK(string_length("") == 0 && string_length("ab") == 2);
    const std::vector<double> three{10, 20, 30};
    // A fractional, out-of-range or null key names a property, not an element,
    // so vec_at answers undefined (see element_index in ctnative.hpp).
    CHECK(vec_at(three, nullable_scalar{-0.5}).tag == nullable_scalar::kind::undefined);
    CHECK(vec_at(three, nullable_scalar{3.0}).tag == nullable_scalar::kind::undefined);
    CHECK(vec_at(three, nullable_scalar::null()).tag == nullable_scalar::kind::undefined);
    static_assert(std::is_same_v<decltype(vec_length(three)), ctnative::js_num>);
    CHECK(vec_length(three) == ctnative::js_num{3.0});
    std::vector<double> storedNumbers;
    vec_push(storedNumbers, negativeZero);
    vec_push(storedNumbers, notANumber);
    CHECK(vec_length(storedNumbers) == two && std::signbit(storedNumbers.front()));
    CHECK(vec_at(storedNumbers, one).tag == nullable_scalar::kind::number &&
          std::isnan(vec_at(storedNumbers, one).value));
    static_assert(std::is_same_v<decltype(dom_number(std::nullopt)), ctnative::js_num>);
    CHECK(dom_number(std::nullopt) == zero && !std::signbit(dom_number(std::nullopt).value()));
    CHECK(std::signbit(dom_number(std::string("-0")).value()));
    CHECK(std::isnan(dom_number(std::string("not a number")).value()));

    auto numbers = make_number_map<double>();
    map_set(numbers, NAN, 1.0);
    map_set(numbers, NAN, 2.0);
    map_set(numbers, -0.0, 3.0);
    static_assert(std::is_same_v<decltype(map_size(numbers)), ctnative::js_num>);
    CHECK(map_size(numbers) == two && !std::signbit(numbers->entries[1].first));
    CHECK(map_get(numbers, 0.0).value == 3.0);
    CHECK(map_get(numbers, 7.0).tag == nullable_scalar::kind::undefined);
    CHECK(map_delete(numbers, NAN) && !map_has(numbers, NAN));
    map_set(numbers, NAN, 4.0);
    CHECK(map_values(numbers) == (std::vector<double>{3.0, 4.0}));
    CHECK(map_snapshot_at<false>(numbers, nullable_scalar{1.0}).value == 4.0);
    CHECK(std::isnan(map_snapshot_at<true>(numbers, nullable_scalar{1.0}).value));

    auto strings = make_map<std::string, std::string>();
    map_set(strings, std::string("a"), std::string("x"));
    CHECK(global_string(map_get(strings, std::string("a"))) == "x");
    CHECK(map_values(strings) == (std::vector<std::string>{"x"}));
    CHECK(map_keys(strings) == (std::vector<std::string>{"a"}));

    auto booleans = make_map<js_boolean_t, js_boolean_t>();
    map_set(booleans, enabled, disabled);
    CHECK(global_boolean(map_get(booleans, enabled)) == disabled);
    CHECK(map_get(booleans, disabled).tag == nullable_scalar::kind::undefined);
    CHECK(map_values(booleans) == std::vector<js_boolean_t>{disabled});
    CHECK(map_snapshot_at<false>(booleans, nullable_scalar{0.0}).tag ==
          nullable_scalar::kind::boolean);
    const auto booleanMap = [&](const auto & map) {
        map_set(map, enabled, disabled);
        map_set(map, disabled, enabled);
        CHECK(map_size(map) == two && map_has(map, enabled));
        CHECK(map_get_present(map, disabled) == enabled);
        CHECK(map_delete(map, enabled) && !map_has(map, enabled));
    };
    booleanMap(booleans);
    booleanMap(
        std::make_shared<std::map<js_boolean_t, js_boolean_t, map_key_less<js_boolean_t>>>());

    // The associative layout's comparator: every NaN is one key, before all.
    map_key_less<double> less;
    CHECK(!less(NAN, NAN) && less(NAN, 1.0) && !less(1.0, NAN));

    const std::vector<nullable_scalar> scalarKeys{
        {}, nullable_scalar::null(), false, true, -0.0, 0.0, NAN, -NAN, -INFINITY, INFINITY, 1.0};
    map_key_less<nullable_scalar> scalarLess;
    for (const auto left : scalarKeys) {
        for (const auto right : scalarKeys) {
            CHECK(map_key_equal(left, right) ==
                  (!scalarLess(left, right) && !scalarLess(right, left)));
            for (const auto third : scalarKeys) {
                CHECK(!(scalarLess(left, right) && scalarLess(right, third)) ||
                      scalarLess(left, third));
            }
        }
    }
    // Exercise both storage layouts regardless of this translation unit's switch.
    const auto scalarMap = [&](const auto & map) {
        for (const auto key : scalarKeys) { map_set(map, key, -0.0); }
        CHECK(map_size(map) == ctnative::js_num{9.0});
        const auto zero = map->find(nullable_scalar{0.0});
        CHECK(zero != map->end() && !std::signbit(zero->first.value) && std::signbit(zero->second));
        const auto saved = map_get_present(map, nullable_scalar{NAN});
        CHECK(map_delete(map, nullable_scalar{-NAN}));
        CHECK(!map_has(map, nullable_scalar{NAN}) && std::signbit(saved));
        map_set(map, nullable_scalar{NAN}, 7.0);
        CHECK(map_get_present(map, nullable_scalar{-NAN}) == 7.0);
        CHECK(map_has(map, nullable_scalar{}) && map_has(map, nullable_scalar::null()));
    };
    scalarMap(make_number_map<nullable_scalar>());
    scalarMap(std::make_shared<std::map<nullable_scalar, double, map_key_less<nullable_scalar>>>());

    const nullable_scalar undefined{undefined_t{}}, null{js_null_t{}}, nan{notANumber};
    CHECK(undefined.tag == nullable_scalar::kind::undefined);
    CHECK(null.tag == nullable_scalar::kind::null);
    CHECK(nan.tag == nullable_scalar::kind::number && std::isnan(nan.value));
    CHECK(scalar_strict_equal(undefined, nullable_scalar{}));
    CHECK(scalar_strict_equal(null, nullable_scalar::null()));
    CHECK(scalar_equal(null, undefined));
    CHECK(!scalar_strict_equal(null, undefined));
    CHECK(!scalar_equal(nan, undefined) && !scalar_equal(nan, null));
    CHECK(std::isnan(to_number(undefined).value()) && to_number(null) == zero);
    CHECK(!std::signbit(to_number(null).value()) && std::isnan(to_number(nan).value()));
    CHECK(!scalar_truthy(undefined) && !scalar_truthy(null));
    CHECK(scalar_typeof(null) == "object" && scalar_typeof(undefined) == "undefined");
    CHECK(scalar_typeof(nan) == "number");
    CHECK(string_equal(nullable_scalar::null(), nullable_string{}));
    CHECK(!string_truthy(nullable_string{std::string()}));
    static_assert(std::is_same_v<decltype(nullable_string{}.to_number()), ctnative::js_num>);
    const auto absentText = to_nullable_string(undefined), nullText = to_nullable_string(null);
    CHECK(std::isnan(absentText.to_number().value()));
    CHECK(nullText.to_number() == zero && !std::signbit(nullText.to_number().value()));
    CHECK(absentText.tag == nullable_string::kind::undefined);
    CHECK(nullText.tag == nullable_string::kind::null_value);
    const nullable_string minusZeroText{std::string{"-0"}};
    CHECK(std::signbit(minusZeroText.to_number().value()));
    CHECK(minusZeroText.tag == nullable_string::kind::string && minusZeroText.value == "-0");
    CHECK(nullable_string{std::string{}}.to_number() == zero);
    CHECK(nullable_string{std::string{" 0x10\n"}}.to_number() == ctnative::js_num{16.0});
    CHECK(std::isnan(nullable_string{std::string{"1\0", 2}}.to_number().value()));
    CHECK(std::isnan(nullable_string{high}.to_number().value()));

    static_assert(std::is_same_v<number_string, std::variant<ctnative::js_num, js_string>>);
    static_assert(std::is_same_v<decltype(add(one, enabled)), number_string>);
    static_assert(std::is_same_v<decltype(to_number(one)), ctnative::js_num>);
    static_assert(std::is_same_v<decltype(to_number(number_string{})), ctnative::js_num>);
    static_assert(std::is_same_v<decltype(number_string_text(number_string{})), js_string>);
    static_assert(std::is_same_v<decltype(global_number_string(std::nullopt)), number_string>);
    static_assert(!primitive_addable<double> && !primitive_addable<bool>);
    static_assert(!primitive_addable<std::string> && !primitive_addable<const char *>);
    static_assert(!primitive_addable<convertible_number> && !primitive_addable<unrelated_number>);
    static_assert(!primitive_addable<object_value> && !primitive_addable<std::nullptr_t>);
    static_assert(!primitive_addable<std::optional<number_string>>);
    // Every supported carrier participates in both positions. Boolean true
    // adds like one numerically but retains its spelling in concatenation.
    const auto numericOnes = std::tuple{one,
                                        enabled,
                                        nullable_scalar{one},
                                        std::variant<js_boolean_t, std::string>{enabled},
                                        number_string{one},
                                        nullable_number_string{one},
                                        boolean_string{enabled},
                                        nullable_boolean_string{enabled}};
    const auto stringTwos = std::tuple{js_string{"2"},
                                       nullable_string{std::string{"2"}},
                                       std::variant<js_boolean_t, std::string>{std::string{"2"}},
                                       number_string{js_string{"2"}},
                                       nullable_number_string{js_string{"2"}},
                                       boolean_string{js_string{"2"}},
                                       nullable_boolean_string{js_string{"2"}}};
    const auto exactOnes =
        std::tuple{one, nullable_scalar{one}, number_string{one}, nullable_number_string{one}};
    const auto trueValues = std::tuple{enabled, nullable_scalar{enabled},
                                       std::variant<js_boolean_t, std::string>{enabled},
                                       boolean_string{enabled}, nullable_boolean_string{enabled}};
    const auto checkAdditions = [](const auto & leftValues, const auto & rightValues,
                                   const auto & expected) {
        std::apply(
            [&](const auto &... left) {
                const auto checkLeft = [&](const auto & value) {
                    std::apply(
                        [&](const auto &... right) {
                            (check(add(value, right) == number_string{expected},
                                   "add(value, right) == number_string{expected}"),
                             ...);
                        },
                        rightValues);
                };
                (checkLeft(left), ...);
            },
            leftValues);
    };
    checkAdditions(numericOnes, numericOnes, two);
    checkAdditions(exactOnes, stringTwos, js_string{"12"});
    checkAdditions(stringTwos, exactOnes, js_string{"21"});
    checkAdditions(trueValues, stringTwos, js_string{"true2"});
    checkAdditions(stringTwos, trueValues, js_string{"2true"});
    checkAdditions(stringTwos, stringTwos, js_string{"22"});
    CHECK(add(disabled, disabled) == number_string{zero});
    CHECK(add(js_string{"x"}, disabled) == number_string{js_string{"xfalse"}});
    CHECK(add(disabled, js_string{"x"}) == number_string{js_string{"falsex"}});
    CHECK(add(std::variant<js_boolean_t, std::string>{disabled}, js_string{"x"}) ==
          number_string{js_string{"falsex"}});
    CHECK(add(null, one) == number_string{one} && add(one, nullText) == number_string{one});
    CHECK(add(nullText, null) == number_string{zero});
    CHECK(std::isnan(to_number(add(undefined, one)).value()));
    CHECK(std::isnan(to_number(add(one, absentText)).value()));
    CHECK(add(nullText, js_string{"x"}) == number_string{js_string{"nullx"}});
    CHECK(add(js_string{"x"}, null) == number_string{js_string{"xnull"}});
    CHECK(add(absentText, js_string{"x"}) == number_string{js_string{"undefinedx"}});
    CHECK(add(js_string{"x"}, undefined) == number_string{js_string{"xundefined"}});
    CHECK(std::signbit(to_number(add(negativeZero, negativeZero)).value()));
    CHECK(!std::signbit(to_number(add(negativeZero, nullText)).value()));
    CHECK(std::isnan(to_number(add(notANumber, one)).value()));
    CHECK(add(number_string{notANumber}, js_string{}) == number_string{js_string{"NaN"}});
    CHECK(add(number_string{negativeZero}, js_string{}) == number_string{js_string{"0"}});
    CHECK(add(binaryText, number_string{js_string{"\0c"}}) == number_string{js_string{"a\0b\0c"}});
    CHECK(add(nullable_string{high}, number_string{lowUnit}) ==
          number_string{js_string{"\xF0\x9F\x98\x80"}});
    CHECK(add(add(one, two), js_string{"4"}) == number_string{js_string{"34"}});
    CHECK(add(one, add(two, js_string{"4"})) == number_string{js_string{"124"}});
    CHECK(to_number(number_string{js_string{" 0x10\n"}}) == ctnative::js_num{16.0});
    CHECK(std::signbit(to_number(number_string{js_string{"-0"}}).value()));
    CHECK(std::isnan(to_number(number_string{js_string{"1\0"}}).value()));
    CHECK(!number_string_truthy(number_string{zero}));
    CHECK(!number_string_truthy(number_string{negativeZero}));
    CHECK(!number_string_truthy(number_string{notANumber}));
    CHECK(!number_string_truthy(number_string{js_string{}}));
    CHECK(number_string_truthy(number_string{js_string{"0"}}));
    CHECK(number_string_truthy(number_string{js_string{"\0"}}));
    CHECK(number_string_typeof(number_string{notANumber}) == "number");
    CHECK(number_string_typeof(number_string{js_string{"NaN"}}) == "string");
    CHECK(number_string_text(number_string{negativeZero}) == js_string{"0"});
    CHECK(number_string_text(number_string{js_string{high + low}}).value() == high + low);
    std::optional<number_string> storedAddition{js_string{"saved\0text"}};
    const auto savedAddition = global_number_string(storedAddition);
    storedAddition = one;
    CHECK(savedAddition == number_string{js_string{"saved\0text"}});
    CHECK(global_number_string(storedAddition) == number_string{one});

    static_assert(
        std::is_same_v<nullable_number_string,
                       std::variant<undefined_t, js_null_t, ctnative::js_num, js_string>>);
    static_assert(primitive_addable<nullable_number_string>);
    static_assert(!std::is_constructible_v<nullable_number_string, bool>);
    static_assert(!std::is_constructible_v<nullable_number_string, js_boolean_t>);
    static_assert(!std::is_constructible_v<nullable_number_string, double>);
    static_assert(!std::is_constructible_v<nullable_number_string, const char *>);
    static_assert(!std::is_constructible_v<nullable_number_string, object_value>);
    static_assert(!std::is_convertible_v<nullable_number_string, number_string>);
    static_assert(std::is_same_v<decltype(to_number(js_string{})), ctnative::js_num>);
    static_assert(std::is_same_v<decltype(to_number(undefined_t{})), ctnative::js_num>);
    static_assert(std::is_same_v<decltype(to_number(js_null_t{})), ctnative::js_num>);
    const nullable_number_string optionalUndefined{}, optionalNull{js_null_t{}},
        optionalZero{negativeZero}, optionalNaN{notANumber}, optionalText{js_string{"a\0b"}};
    CHECK(std::holds_alternative<undefined_t>(optionalUndefined));
    CHECK(std::holds_alternative<js_null_t>(optionalNull));
    CHECK(std::holds_alternative<ctnative::js_num>(optionalZero));
    CHECK(std::holds_alternative<js_string>(optionalText));
    CHECK(std::isnan(to_number(optionalUndefined).value()));
    CHECK(to_number(optionalNull) == zero && !std::signbit(to_number(optionalNull).value()));
    CHECK(std::signbit(to_number(optionalZero).value()));
    CHECK(std::isnan(to_number(optionalNaN).value()));
    CHECK(std::isnan(to_number(optionalText).value()));
    CHECK(to_number(nullable_number_string{js_string{" 0x10\n"}}) == ctnative::js_num{16.0});
    CHECK(to_number(nullable_number_string{js_string{}}) == zero);
    CHECK(nullable_number_string_text(optionalUndefined) == js_string{"undefined"});
    CHECK(nullable_number_string_text(optionalNull) == js_string{"null"});
    CHECK(nullable_number_string_text(optionalZero) == js_string{"0"});
    CHECK(nullable_number_string_text(optionalNaN) == js_string{"NaN"});
    CHECK(nullable_number_string_text(optionalText) == js_string{"a\0b"});
    CHECK(!nullable_number_string_truthy(optionalUndefined));
    CHECK(!nullable_number_string_truthy(optionalNull));
    CHECK(!nullable_number_string_truthy(optionalZero));
    CHECK(!nullable_number_string_truthy(optionalNaN));
    CHECK(!nullable_number_string_truthy(nullable_number_string{js_string{}}));
    CHECK(nullable_number_string_truthy(optionalText));
    CHECK(nullable_number_string_truthy(nullable_number_string{one}));
    CHECK(nullable_number_string_typeof(optionalUndefined) == "undefined");
    CHECK(nullable_number_string_typeof(optionalNull) == "object");
    CHECK(nullable_number_string_typeof(optionalNaN) == "number");
    CHECK(nullable_number_string_typeof(optionalText) == "string");
    CHECK(std::holds_alternative<undefined_t>(to_nullable_number_string(undefined)));
    CHECK(std::holds_alternative<undefined_t>(to_nullable_number_string(absentText)));
    CHECK(std::holds_alternative<js_null_t>(to_nullable_number_string(null)));
    CHECK(std::holds_alternative<js_null_t>(to_nullable_number_string(nullText)));
    CHECK(std::signbit(
        global_number(to_nullable_number_string(nullable_scalar{negativeZero})).value()));
    CHECK(std::signbit(
        global_number(to_nullable_number_string(number_string{negativeZero})).value()));
    CHECK(global_string(to_nullable_number_string(number_string{js_string{"a\0b"}})) ==
          std::string("a\0b", 3));
    CHECK(global_string(to_nullable_number_string(nullable_string{high + low})) == high + low);
    CHECK(global_number_string(optionalText) == number_string{js_string{"a\0b"}});
    CHECK(std::signbit(global_number(optionalZero).value()));
    CHECK(std::isnan(global_number(optionalNaN).value()));
    CHECK(std::isnan(to_number(add(optionalUndefined, one)).value()));
    CHECK(add(optionalNull, one) == number_string{one});
    CHECK(add(js_string{"x"}, optionalUndefined) == number_string{js_string{"xundefined"}});
    CHECK(add(optionalNull, js_string{"x"}) == number_string{js_string{"nullx"}});
    CHECK(add(optionalText, optionalNull) == number_string{js_string{"a\0bnull"}});
    CHECK(std::signbit(to_number(add(optionalZero, optionalZero)).value()));
    CHECK(add(nullable_number_string{highUnit}, nullable_number_string{lowUnit}) ==
          number_string{js_string{"\xF0\x9F\x98\x80"}});
    nullable_number_string changingOptional = optionalText;
    const auto copiedOptional = changingOptional;
    changingOptional = undefined_t{};
    CHECK(std::holds_alternative<undefined_t>(changingOptional));
    CHECK(global_string(copiedOptional) == std::string("a\0b", 3));
    changingOptional = negativeZero;
    CHECK(std::signbit(global_number(changingOptional).value()));

    static_assert(std::is_same_v<boolean_string, std::variant<js_boolean_t, js_string>>);
    static_assert(std::is_same_v<nullable_boolean_string,
                                 std::variant<undefined_t, js_null_t, js_boolean_t, js_string>>);
    static_assert(primitive_addable<boolean_string> && primitive_addable<nullable_boolean_string>);
    static_assert(!std::is_constructible_v<boolean_string, bool>);
    static_assert(!std::is_constructible_v<boolean_string, ctnative::js_num>);
    static_assert(!std::is_constructible_v<boolean_string, std::string>);
    static_assert(!std::is_constructible_v<nullable_boolean_string, bool>);
    static_assert(!std::is_constructible_v<nullable_boolean_string, ctnative::js_num>);
    static_assert(!std::is_constructible_v<nullable_boolean_string, std::string>);
    static_assert(!std::is_constructible_v<nullable_boolean_string, object_value>);
    static_assert(!std::is_convertible_v<nullable_boolean_string, boolean_string>);
    static_assert(std::is_same_v<decltype(to_number(boolean_string{})), ctnative::js_num>);
    static_assert(std::is_same_v<decltype(boolean_string_text(boolean_string{})), js_string>);
    const boolean_string unionTrue{enabled}, unionFalse{disabled},
        unionFalseText{js_string{"false"}}, unionEmptyText{js_string{}};
    CHECK(std::holds_alternative<js_boolean_t>(boolean_string{}));
    CHECK(to_number(unionTrue) == one && to_number(unionFalse) == zero);
    CHECK(!std::signbit(to_number(unionFalse).value()));
    CHECK(std::isnan(to_number(unionFalseText).value()));
    CHECK(to_number(unionEmptyText) == zero);
    CHECK(std::signbit(to_number(boolean_string{js_string{"-0"}}).value()));
    CHECK(boolean_string_text(unionTrue) == js_string{"true"});
    CHECK(boolean_string_text(unionFalse) == js_string{"false"});
    CHECK(boolean_string_text(unionFalseText) == js_string{"false"});
    CHECK(boolean_string_text(boolean_string{js_string{high + low}}).value() == high + low);
    CHECK(boolean_string_truthy(unionTrue) && boolean_string_truthy(unionFalseText));
    CHECK(!boolean_string_truthy(unionFalse) && !boolean_string_truthy(unionEmptyText));
    CHECK(boolean_string_typeof(unionFalse) == "boolean");
    CHECK(boolean_string_typeof(unionFalseText) == "string");
    CHECK(add(unionFalse, one) == number_string{one});
    CHECK(add(unionFalseText, one) == number_string{js_string{"false1"}});
    CHECK(add(unionFalse, js_string{"x"}) == number_string{js_string{"falsex"}});
    CHECK(add(js_string{"x"}, unionTrue) == number_string{js_string{"xtrue"}});
    const nullable_boolean_string optionalBooleanUndefined{}, optionalBooleanNull{js_null_t{}},
        optionalBooleanFalse{disabled}, optionalBooleanText{js_string{"a\0b"}};
    CHECK(std::holds_alternative<undefined_t>(optionalBooleanUndefined));
    CHECK(std::isnan(to_number(optionalBooleanUndefined).value()));
    CHECK(to_number(optionalBooleanNull) == zero &&
          !std::signbit(to_number(optionalBooleanNull).value()));
    CHECK(to_number(optionalBooleanFalse) == zero &&
          !std::signbit(to_number(optionalBooleanFalse).value()));
    CHECK(std::isnan(to_number(optionalBooleanText).value()));
    CHECK(nullable_boolean_string_text(optionalBooleanUndefined) == js_string{"undefined"});
    CHECK(nullable_boolean_string_text(optionalBooleanNull) == js_string{"null"});
    CHECK(nullable_boolean_string_text(optionalBooleanFalse) == js_string{"false"});
    CHECK(nullable_boolean_string_text(optionalBooleanText) == js_string{"a\0b"});
    CHECK(!nullable_boolean_string_truthy(optionalBooleanUndefined));
    CHECK(!nullable_boolean_string_truthy(optionalBooleanNull));
    CHECK(!nullable_boolean_string_truthy(optionalBooleanFalse));
    CHECK(nullable_boolean_string_truthy(optionalBooleanText));
    CHECK(nullable_boolean_string_typeof(optionalBooleanUndefined) == "undefined");
    CHECK(nullable_boolean_string_typeof(optionalBooleanNull) == "object");
    CHECK(nullable_boolean_string_typeof(optionalBooleanFalse) == "boolean");
    CHECK(nullable_boolean_string_typeof(optionalBooleanText) == "string");
    CHECK(std::holds_alternative<undefined_t>(to_nullable_boolean_string(undefined)));
    CHECK(std::holds_alternative<undefined_t>(to_nullable_boolean_string(absentText)));
    CHECK(std::holds_alternative<js_null_t>(to_nullable_boolean_string(null)));
    CHECK(std::holds_alternative<js_null_t>(to_nullable_boolean_string(nullText)));
    CHECK(global_boolean(to_nullable_boolean_string(nullable_scalar{enabled})) == enabled);
    CHECK(global_boolean(to_nullable_boolean_string(nullable_scalar{disabled})) == disabled);
    CHECK(global_boolean_string(to_nullable_boolean_string(unionTrue)) == unionTrue);
    CHECK(global_boolean_string(to_nullable_boolean_string(unionFalseText)) == unionFalseText);
    CHECK(global_string(to_nullable_boolean_string(nullable_string{high + low})) == high + low);
    CHECK(add(optionalBooleanText, optionalBooleanFalse) == number_string{js_string{"a\0bfalse"}});
    CHECK(add(optionalBooleanNull, unionFalse) == number_string{zero});
    CHECK(add(unionTrue, optionalBooleanNull) == number_string{one});
    CHECK(std::isnan(to_number(add(optionalBooleanUndefined, unionFalse)).value()));
    CHECK(add(js_string{"x"}, optionalBooleanUndefined) == number_string{js_string{"xundefined"}});
    CHECK(add(optionalBooleanNull, js_string{"x"}) == number_string{js_string{"nullx"}});
    CHECK(add(boolean_string{highUnit}, nullable_boolean_string{lowUnit}) ==
          number_string{js_string{"\xF0\x9F\x98\x80"}});
    nullable_boolean_string changingBoolean = optionalBooleanText;
    const auto savedBoolean = global_boolean_string(changingBoolean);
    const auto copiedBoolean = changingBoolean;
    changingBoolean = disabled;
    CHECK(global_boolean(changingBoolean) == disabled);
    CHECK(savedBoolean == boolean_string{js_string{"a\0b"}});
    CHECK(global_string(copiedBoolean) == std::string("a\0b", 3));
    changingBoolean = undefined_t{};
    CHECK(std::holds_alternative<undefined_t>(changingBoolean));

    static_assert(std::is_same_v<decltype(primitive_equal(one, js_string{"1"})), js_boolean_t>);
    static_assert(std::is_same_v<decltype(primitive_strict_equal(one, one)), js_boolean_t>);
    static_assert(!primitive_equality_comparable<double> && !primitive_equality_comparable<bool>);
    static_assert(!primitive_equality_comparable<std::string>);
    static_assert(!primitive_equality_comparable<const char *>);
    static_assert(!primitive_equality_comparable<convertible_number>);
    static_assert(!primitive_equality_comparable<unrelated_number>);
    static_assert(!primitive_equality_comparable<object_value>);
    static_assert(!primitive_equality_comparable<std::variant<js_boolean_t, std::string>>);
    static_assert(!primitive_equality_comparable<std::optional<number_string>>);
    const auto sameNumbers =
        std::tuple{one, nullable_scalar{one}, number_string{one}, nullable_number_string{one}};
    const auto sameBooleans = std::tuple{enabled, nullable_scalar{enabled}, boolean_string{enabled},
                                         nullable_boolean_string{enabled}};
    const auto sameStrings = std::tuple{js_string{"1"},
                                        nullable_string{std::string{"1"}},
                                        number_string{js_string{"1"}},
                                        nullable_number_string{js_string{"1"}},
                                        boolean_string{js_string{"1"}},
                                        nullable_boolean_string{js_string{"1"}}};
    const auto sameUndefined = std::tuple{undefined_t{}, nullable_scalar{}, nullable_string{},
                                          nullable_number_string{}, nullable_boolean_string{}};
    const auto sameNull =
        std::tuple{js_null_t{}, null, nullText, nullable_number_string{js_null_t{}},
                   nullable_boolean_string{js_null_t{}}};
    const auto nanValues =
        std::tuple{notANumber, nullable_scalar{notANumber}, number_string{notANumber},
                   nullable_number_string{notANumber}};
    // Equivalent values traverse every finite carrier pair; strict equality
    // keeps their JS kinds even when loose equality gives the same answer.
    const auto checkEqualities = [](const auto & leftValues, const auto & rightValues, bool loose,
                                    bool strict) {
        std::apply(
            [&](const auto &... left) {
                const auto checkLeft = [&](const auto & a) {
                    const auto checkPair = [&](const auto & b) {
                        CHECK(primitive_equal(a, b) == js_boolean_t{loose});
                        CHECK(primitive_equal(b, a) == js_boolean_t{loose});
                        CHECK(primitive_strict_equal(a, b) == js_boolean_t{strict});
                        CHECK(primitive_strict_equal(b, a) == js_boolean_t{strict});
                    };
                    std::apply([&](const auto &... right) { (checkPair(right), ...); },
                               rightValues);
                };
                (checkLeft(left), ...);
            },
            leftValues);
    };
    checkEqualities(sameNumbers, sameNumbers, true, true);
    checkEqualities(sameBooleans, sameBooleans, true, true);
    checkEqualities(sameStrings, sameStrings, true, true);
    checkEqualities(sameNumbers, sameBooleans, true, false);
    checkEqualities(sameNumbers, sameStrings, true, false);
    checkEqualities(sameBooleans, sameStrings, true, false);
    checkEqualities(sameUndefined, sameUndefined, true, true);
    checkEqualities(sameNull, sameNull, true, true);
    checkEqualities(sameNull, sameUndefined, true, false);
    checkEqualities(sameNull, sameNumbers, false, false);
    checkEqualities(sameUndefined, sameStrings, false, false);
    checkEqualities(nanValues, nanValues, false, false);
    CHECK(primitive_strict_equal(nullable_number_string{negativeZero}, nullable_scalar{zero}));
    CHECK(primitive_equal(unionFalse, negativeZero));
    CHECK(!primitive_strict_equal(unionFalse, negativeZero));
    for (const auto & text : {js_string{}, js_string{"0"}, js_string{"-0"}, js_string{" \t\n"}}) {
        CHECK(primitive_equal(disabled, text) && primitive_equal(text, disabled));
        CHECK(primitive_equal(zero, text) && primitive_equal(text, zero));
        CHECK(!primitive_strict_equal(text, disabled) && !primitive_strict_equal(text, zero));
    }
    CHECK(!primitive_equal(unionFalse, unionFalseText));
    CHECK(!primitive_equal(js_string{"true"}, enabled));
    CHECK(!primitive_equal(js_string{"0"}, js_string{}));
    CHECK(!primitive_equal(js_string{" 1 "}, js_string{"1"}));
    CHECK(primitive_equal(js_string{" 1 "}, one));
    CHECK(!primitive_equal(optionalBooleanNull, zero));
    CHECK(!primitive_equal(optionalUndefined, disabled));
    CHECK(!primitive_equal(optionalNull, js_string{}));
    CHECK(!primitive_equal(js_string{"null"}, optionalNull));
    CHECK(!primitive_equal(js_string{"undefined"}, optionalUndefined));
    CHECK(!primitive_equal(number_string{notANumber}, js_string{"NaN"}));
    CHECK(primitive_equal(js_string{"NaN"}, boolean_string{js_string{"NaN"}}));
    CHECK(primitive_strict_equal(optionalText, boolean_string{js_string{"a\0b"}}));
    CHECK(primitive_equal(optionalBooleanText, js_string{"a\0b"}));
    CHECK(!primitive_equal(optionalBooleanText, js_string{"a"}));
    CHECK(!primitive_equal(one, nullable_string{std::string{"1\0", 2}}));
    CHECK(primitive_strict_equal(js_string{high}, nullable_boolean_string{js_string{high}}));
    CHECK(!primitive_equal(highUnit, lowUnit));

    // Borrowed browser views reuse the live public DOM/Style objects. Absence
    // is explicit; an invalid incoming handle never becomes a nullable element.
    static_assert(!std::is_default_constructible_v<js_document_t>);
    static_assert(!std::is_default_constructible_v<js_element_t>);
    static_assert(std::is_trivially_copyable_v<js_document_t>);
    static_assert(std::is_trivially_copyable_v<js_element_t>);
    static_assert(!std::is_move_constructible_v<ctbrowser::document>);
    static_assert(!std::is_convertible_v<js_element_t, ctbrowser::element_ref>);
    static_assert(!std::is_constructible_v<js_element_t, undefined_t>);
    static_assert(!std::is_constructible_v<js_element_t, js_null_t>);
    ctbrowser::atom_table atoms;
    ctbrowser::document dom{atoms};
    ctbrowser::style::engine styles{atoms};
    const js_document_t document{dom, styles};
    static_assert(
        std::is_same_v<decltype(document.documentElement()), std::optional<js_element_t>>);
    static_assert(
        std::is_same_v<decltype(document.querySelector(js_string{})), std::optional<js_element_t>>);
    static_assert(std::is_same_v<decltype(document.querySelectorAll(js_string{})),
                                 std::vector<js_element_t>>);
    CHECK(!document.documentElement());
    CHECK(!document.querySelector(js_string{"html"}));
    CHECK(document.querySelectorAll(js_string{"*"}).empty());
    const auto root = dom.create_element(atoms.intern("html"));
    dom.set_document_element(root);
    const auto button = dom.create_element(atoms.intern("button"));
    dom.append_child(root, button).value();
    dom.set_attribute(button, atoms.intern("class"), "selected").value();
    const js_element_t element{{&dom, button}, styles};
    const auto rootView = document.documentElement().value();
    const js_string selected{".selected"};
    CHECK(document.querySelector(js_string{"html"}) == rootView);
    CHECK(document.querySelector(selected) == element);
    CHECK(rootView.querySelector(selected) == element);
    CHECK(!rootView.querySelector(js_string{"html"}));
    CHECK(Element.prototype.querySelector.call(rootView, selected) == element);
    CHECK(element.matches(selected));
    CHECK(Element.prototype.matches.call(element, selected));
    CHECK(element.closest(js_string{"html"}) == rootView);
    CHECK(Element.prototype.closest.call(element, js_string{"html"}) == rootView);
    CHECK(!element.closest(js_string{".missing"}));
    CHECK(!Element.prototype.querySelector.call(rootView, js_string{".missing"}));
    const auto snapshot = Element.prototype.querySelectorAll.call(rootView, selected);
    CHECK(snapshot.size() == 1 && snapshot.front() == element);
    CHECK(document.querySelectorAll(js_string{"html, button"}) ==
          (std::vector<js_element_t>{rootView, element}));
    static_assert(std::is_same_v<decltype(element.matches(selected)), js_boolean_t>);
    static_assert(std::is_same_v<decltype(Element.prototype.closest.call(element, selected)),
                                 std::optional<js_element_t>>);
    static_assert(
        std::is_same_v<decltype(Element.prototype.querySelectorAll.call(element, selected)),
                       std::vector<js_element_t>>);
    CHECK(!element.matches(js_string{":hover"}));
    styles.set_state(button, ctbrowser::style::engine::state_hover, true);
    CHECK(Element.prototype.matches.call(element, js_string{":hover"}));
    dom.remove_attribute(button, atoms.intern("class")).value();
    CHECK(!document.querySelector(selected));
    CHECK(snapshot.front() == element && !snapshot.front().matches(selected));
    dom.remove_child(button).value();
    CHECK(element.matches(js_string{"button"}));
    CHECK(!element.closest(js_string{"html"}));
    CHECK(document.querySelectorAll(js_string{"button"}).empty());
    dom.remove_document_element();
    CHECK(!document.documentElement());
    const auto replacement = dom.create_element(atoms.intern("body"));
    dom.set_document_element(replacement);
    CHECK(document.documentElement()->value() == (ctbrowser::element_ref{&dom, replacement}));
    CHECK(document.documentElement() != rootView);

    ctbrowser::document otherDOM{atoms};
    const auto otherRoot = otherDOM.create_element(atoms.intern("html"));
    CHECK(otherRoot == root);
    const js_element_t otherElement{{&otherDOM, otherRoot}, styles};
    CHECK(otherElement != rootView);
    ctbrowser::style::engine sameAtoms{atoms};
    CHECK((js_element_t{{&dom, root}, sameAtoms}) == rootView);
    const auto rejects = [](auto action) {
        try {
            action();
        } catch (const std::exception &) { return true; }
        return false;
    };
    CHECK(rejects([&] { (void)js_element_t{{}, styles}; }));
    CHECK(rejects([&] { (void)js_element_t{{&dom, dom.document_node()}, styles}; }));
    auto stale = button;
    ++stale.generation;
    CHECK(rejects([&] { (void)js_element_t{{&dom, stale}, styles}; }));
    CHECK(rejects([&] { (void)document.querySelector(js_string{"["}); }));
    CHECK(rejects([&] { (void)Element.prototype.matches.call(element, js_string{"["}); }));
    ctbrowser::atom_table otherAtoms;
    ctbrowser::style::engine wrongStyles{otherAtoms};
    CHECK(rejects([&] { (void)js_document_t{dom, wrongStyles}; }));
    CHECK(rejects([&] { (void)js_element_t{{&dom, button}, wrongStyles}; }));

    // require_entry_element and require_dataset_element refuse an HTML <form>,
    // whose named controls shadow its members ([LegacyOverrideBuiltIns], HTML
    // 4.10.3). require_element, used for keys, and querySelector results accept
    // one, and an SVG <form> is not an HTML form.
    const auto refusesForm = [](auto check, ctbrowser::element_ref input) {
        try {
            check(input);
        } catch (const std::invalid_argument &) { return true; }
        return false;
    };
    const auto entry = [](ctbrowser::element_ref input) { require_entry_element(input); };
    const auto dataset = [](ctbrowser::element_ref input) { require_dataset_element(input); };
    const auto key = [](ctbrowser::element_ref input) { require_element(input); };
    const auto form = dom.create_element(atoms.intern("form"));
    dom.append_child(replacement, form).value();
    CHECK(refusesForm(entry, {&dom, form}) && refusesForm(dataset, {&dom, form}));
    CHECK(!refusesForm(key, {&dom, form}));
    CHECK(!refusesForm(entry, {&dom, replacement}));
    const auto svgForm = dom.create_element(atoms.intern("form"), ctbrowser::node_ns::svg);
    CHECK(!refusesForm(entry, {&dom, svgForm}));
    CHECK(document.querySelector(js_string{"form"})->value() ==
          (ctbrowser::element_ref{&dom, form}));
    dom.remove_child(form).value();

    // The global facade follows a scoped binding; explicit views keep their owner.
    static_assert(std::is_empty_v<document_object>);
    static_assert(!std::is_default_constructible_v<document_scope>);
    static_assert(!std::is_copy_constructible_v<document_scope>);
    static_assert(!std::is_copy_assignable_v<document_scope>);
    static_assert(!std::is_move_constructible_v<document_scope>);
    static_assert(!std::is_move_assignable_v<document_scope>);
    const auto unbound = [] {
        try {
            (void)ctnative::document.documentElement();
        } catch (const std::logic_error &) { return true; }
        return false;
    };
    CHECK(unbound());
    otherDOM.set_document_element(otherRoot);
    {
        const document_scope outer{dom, styles};
        const auto outerRoot = ctnative::document.documentElement();
        const auto saved = ctnative::document.querySelectorAll(js_string{"*"});
        CHECK(outerRoot == document.documentElement());
        CHECK(ctnative::document.querySelector(js_string{"body"}) == outerRoot);
        CHECK(saved == (std::vector<js_element_t>{outerRoot.value()}));
        CHECK(rejects([&] { const document_scope invalid{otherDOM, wrongStyles}; }));
        CHECK(ctnative::document.documentElement() == outerRoot);
        {
            const document_scope inner{otherDOM, styles};
            CHECK(ctnative::document.documentElement() == otherElement);
            CHECK(ctnative::document.querySelector(js_string{"html"}) == otherElement);
            CHECK(!ctnative::document.querySelector(js_string{"body"}));
            CHECK(document.documentElement() == outerRoot);
            CHECK(saved.front() == outerRoot.value());
        }
        CHECK(ctnative::document.documentElement() == outerRoot);
        CHECK(rejects([&] {
            const document_scope inner{otherDOM, styles};
            (void)ctnative::document.querySelector(js_string{"["});
        }));
        CHECK(ctnative::document.documentElement() == outerRoot);
        bool workerUnbound = false, workerBound = false, workerRestored = false;
        {
            const std::jthread worker{[&] {
                workerUnbound = unbound();
                ctbrowser::atom_table workerAtoms;
                ctbrowser::document workerDOM{workerAtoms};
                ctbrowser::style::engine workerStyles{workerAtoms};
                const auto workerRoot = workerDOM.create_element(workerAtoms.intern("html"));
                workerDOM.set_document_element(workerRoot);
                {
                    const document_scope inner{workerDOM, workerStyles};
                    workerBound = ctnative::document.documentElement()->value() ==
                                  (ctbrowser::element_ref{&workerDOM, workerRoot});
                }
                workerRestored = unbound();
            }};
        }
        CHECK(workerUnbound && workerBound && workerRestored);
        CHECK(ctnative::document.documentElement() == outerRoot);
    }
    // The built-in objects are stateless and usable in constant expressions,
    // like Element: generated code names them, never constructs them.
    static_assert(
        std::is_empty_v<ctnative::to_lower_case_method> && std::is_empty_v<ctnative::trim_method> &&
        std::is_empty_v<ctnative::split_method> && std::is_empty_v<ctnative::push_method> &&
        std::is_empty_v<ctnative::join_method> && std::is_empty_v<ctnative::sort_method> &&
        std::is_empty_v<ctnative::min_method> && std::is_empty_v<ctnative::stringify_method> &&
        std::is_empty_v<ctnative::number_constructor> &&
        std::is_empty_v<ctnative::boolean_constructor>);
    {
        constexpr ctnative::string_constructor string = ctnative::String;
        constexpr ctnative::array_constructor array = ctnative::Array;
        constexpr ctnative::math_object math = ctnative::Math;
        constexpr ctnative::json_object json = ctnative::JSON;
        constexpr ctnative::number_constructor number = ctnative::Number;
        constexpr ctnative::boolean_constructor boolean = ctnative::Boolean;
        (void)boolean;
        (void)number;
        (void)string;
        (void)array;
        (void)math;
        (void)json;
    }

    // Number(value) is ToNumber for each carrier: whitespace and radix
    // prefixes, the empty string as 0, trailing junk as NaN, booleans, and
    // undefined as NaN but null as 0.
    CHECK(ctnative::Number(js_string{"  0x10\n"}) == ctnative::js_num{16.0});
    CHECK(ctnative::Number(js_string{}) == ctnative::js_num{0.0});
    CHECK(std::isnan(ctnative::Number(js_string{"1px"}).value()));
    CHECK(std::signbit(ctnative::Number(js_string{"-0"}).value()));
    CHECK(ctnative::Number(js_boolean_t{true}) == ctnative::js_num{1.0});
    CHECK(std::isnan(ctnative::Number(nullable_scalar{}).value()));
    CHECK(ctnative::Number(nullable_scalar{js_null_t{}}) == ctnative::js_num{0.0});
    CHECK(ctnative::Number(ctnative::js_num{2.5}) == ctnative::js_num{2.5});

    // Number.parseInt (19.2.5): leading whitespace and a sign, radix 10, 16
    // and 0 (10, or 16 after 0x), a radix out of [2, 36] as NaN, the 0x prefix
    // only for 16 or 0, a garbage suffix ignored, and no digits as NaN.
    {
        const auto parse = [](const char * text, double radix) {
            return ctnative::Number.parseInt(js_string{std::string{text}}, ctnative::js_num{radix})
                .value();
        };
        CHECK(parse("  \t42px", 10) == 42.0);
        CHECK(parse("-17", 10) == -17.0);
        CHECK(parse("+8", 10) == 8.0);
        CHECK(parse("ff", 16) == 255.0);
        CHECK(parse("0x1F", 16) == 31.0);
        CHECK(parse("0x1F", 0) == 31.0);
        CHECK(parse("0x1F", 10) == 0.0);
        CHECK(parse("12", 0) == 12.0);
        CHECK(parse("101", 2) == 5.0);
        CHECK(parse("z", 36) == 35.0);
        CHECK(std::isnan(parse("7", 1)));
        CHECK(std::isnan(parse("7", 37)));
        CHECK(std::isnan(parse("", 10)));
        CHECK(std::isnan(parse("px", 10)));
        CHECK(std::isnan(parse("-", 10)));
        CHECK(std::signbit(parse("-0", 10)));
        // A missing radix is undefined, which ToInt32 makes 0.
        CHECK(ctnative::Number.parseInt(js_string{"0x10"}).value() == 16.0);
        // ToInt32 wraps the radix: 2^32 + 16 is 16.
        CHECK(parse("10", 4294967312.0) == 16.0);
    }

    // Boolean(value) is ToBoolean for each carrier: NaN, 0 and -0, the empty
    // String, undefined and null are false; a String union follows its
    // alternative.
    static_assert(ctnative::Boolean(ctnative::js_num{2.0}) == js_boolean_t{true});
    CHECK(ctnative::Boolean(ctnative::js_num{js_nan_t{}}) == js_boolean_t{false});
    CHECK(ctnative::Boolean(ctnative::js_num{-0.0}) == js_boolean_t{false});
    CHECK(ctnative::Boolean(js_string{}) == js_boolean_t{false});
    CHECK(ctnative::Boolean(js_string{"0"}) == js_boolean_t{true});
    CHECK(ctnative::Boolean(nullable_scalar{}) == js_boolean_t{false});
    CHECK(ctnative::Boolean(nullable_scalar{js_null_t{}}) == js_boolean_t{false});
    CHECK(ctnative::Boolean(to_nullable_string(js_string{"x"})) == js_boolean_t{true});
    CHECK(ctnative::Boolean(number_string{js_string{}}) == js_boolean_t{false});
    CHECK(ctnative::Boolean(number_string{ctnative::js_num{1.0}}) == js_boolean_t{true});

    // String.prototype.toLowerCase: ASCII, Latin-1, the U+0130 expansion, a
    // final sigma against a lone capital one, ß unchanged, a lone surrogate
    // copied as it was, and the empty string.
    CHECK(ctnative::String.prototype.toLowerCase.call(js_string{"ABC xyz"}) ==
          js_string{"abc xyz"});
    CHECK(ctnative::String.prototype.toLowerCase.call(js_string{"\xC3\x80"
                                                                "B"}) == js_string{"\xC3\xA0"
                                                                                   "b"});
    CHECK(ctnative::String.prototype.toLowerCase.call(js_string{"\xC4\xB0"}) ==
          js_string{"i\xCC\x87"});
    CHECK(ctnative::String.prototype.toLowerCase.call(
              js_string{"\xCE\x9F\xCE\x94\xCE\x9F\xCE\xA3"}) ==
          js_string{"\xCE\xBF\xCE\xB4\xCE\xBF\xCF\x82"});
    CHECK(ctnative::String.prototype.toLowerCase.call(js_string{"\xCE\xA3"}) ==
          js_string{"\xCF\x83"});
    CHECK(ctnative::String.prototype.toLowerCase.call(js_string{"\xC3\x9F"}) ==
          js_string{"\xC3\x9F"});
    CHECK(ctnative::String.prototype.toLowerCase.call(js_string{"\xED\xA0\x80"}) ==
          js_string{"\xED\xA0\x80"});
    CHECK(ctnative::String.prototype.toLowerCase.call(js_string{}) == js_string{});
    CHECK(ctbrowser::unicode_case("\xC3\x9F", true) == "SS");

    // String.prototype.trim: ASCII space and line terminators, NBSP, LS and
    // the BOM are all StrWhiteSpaceChar; interior space stays.
    CHECK(ctnative::String.prototype.trim.call(js_string{" \t a b \r\n"}) == js_string{"a b"});
    CHECK(ctnative::String.prototype.trim.call(js_string{"\xC2\xA0x\xE2\x80\xA8"}) ==
          js_string{"x"});
    CHECK(ctnative::String.prototype.trim.call(js_string{"\xEF\xBB\xBF"}) == js_string{});
    CHECK(ctnative::String.prototype.trim.call(js_string{}) == js_string{});

    // The replace(/[\r ]+$/, '') shape: only the trailing run goes.
    CHECK(strip_trailing_run(js_string{"color\r"}, "\r ") == js_string{"color"});
    CHECK(strip_trailing_run(js_string{"a \r \r"}, "\r ") == js_string{"a"});
    CHECK(strip_trailing_run(js_string{" a b"}, "\r ") == js_string{" a b"});
    CHECK(strip_trailing_run(js_string{"\xC3\xA9 \r"}, "\r ") == js_string{"\xC3\xA9"});
    CHECK(strip_trailing_run(js_string{" \r "}, "\r ") == js_string{});
    CHECK(strip_trailing_run(js_string{}, "\r ") == js_string{});

    // text[index]: UTF-16 units, -0 is index 0, and everything that names no
    // element is undefined, in the nullable_string carrier.
    CHECK(string_at(js_string{"abc"}, ctnative::js_num{0.0}) == to_nullable_string(js_string{"a"}));
    CHECK(string_at(js_string{"abc"}, ctnative::js_num{-0.0}) ==
          to_nullable_string(js_string{"a"}));
    CHECK(string_at(js_string{"abc"}, ctnative::js_num{2.0}) == to_nullable_string(js_string{"c"}));
    CHECK(string_at(js_string{"abc"}, ctnative::js_num{3.0}).tag ==
          nullable_string::kind::undefined);
    CHECK(string_at(js_string{"abc"}, ctnative::js_num{1.5}).tag ==
          nullable_string::kind::undefined);
    CHECK(string_at(js_string{"abc"}, ctnative::js_num{-1.0}).tag ==
          nullable_string::kind::undefined);
    CHECK(string_at(js_string{"abc"}, ctnative::js_num{js_nan_t{}}).tag ==
          nullable_string::kind::undefined);
    CHECK(string_at(js_string{"abc"}, ctnative::js_num{std::numeric_limits<double>::infinity()})
              .tag == nullable_string::kind::undefined);
    CHECK(string_at(js_string{}, ctnative::js_num{0.0}).tag == nullable_string::kind::undefined);
    CHECK(string_at(js_string{"\xC3\xA9"}, ctnative::js_num{0.0}) ==
          to_nullable_string(js_string{"\xC3\xA9"}));
    // U+1F600 is the pair D83D DE00; each index answers its lone half.
    CHECK(string_at(js_string{"\xF0\x9F\x98\x80"}, ctnative::js_num{0.0}) ==
          to_nullable_string(js_string{"\xED\xA0\xBD"}));
    CHECK(string_at(js_string{"\xF0\x9F\x98\x80"}, ctnative::js_num{1.0}) ==
          to_nullable_string(js_string{"\xED\xB8\x80"}));
    CHECK(string_at(js_string{"\xF0\x9F\x98\x80"}, ctnative::js_num{2.0}).tag ==
          nullable_string::kind::undefined);

    // Math.min: NaN wins on either side, -0 is below +0 in both orders.
    CHECK(ctnative::Math.min(ctnative::js_num{1.0}, ctnative::js_num{2.0}) ==
          ctnative::js_num{1.0});
    CHECK(ctnative::Math.min(ctnative::js_num{2.0}, ctnative::js_num{-3.5}) ==
          ctnative::js_num{-3.5});
    CHECK(std::isnan(
        ctnative::Math.min(ctnative::js_num{js_nan_t{}}, ctnative::js_num{1.0}).value()));
    CHECK(std::isnan(
        ctnative::Math.min(ctnative::js_num{1.0}, ctnative::js_num{js_nan_t{}}).value()));
    CHECK(std::signbit(ctnative::Math.min(ctnative::js_num{0.0}, ctnative::js_num{-0.0}).value()));
    CHECK(std::signbit(ctnative::Math.min(ctnative::js_num{-0.0}, ctnative::js_num{0.0}).value()));
    CHECK(!std::signbit(ctnative::Math.min(ctnative::js_num{0.0}, ctnative::js_num{0.0}).value()));
    const double infinity = std::numeric_limits<double>::infinity();
    CHECK(ctnative::Math.min(ctnative::js_num{-infinity}, ctnative::js_num{1.0}) ==
          ctnative::js_num{-infinity});
    CHECK(ctnative::Math.min(ctnative::js_num{infinity}, ctnative::js_num{infinity}) ==
          ctnative::js_num{infinity});

    // QuoteJSONString, through JSON.stringify of a String: short escapes,
    // \u00XX for other controls, a lone surrogate as \uXXXX, non-ASCII passed
    // through, the empty string.
    const auto quoted = [](std::string text = {}) {
        return ctnative::JSON.stringify(ctbrowser::json_value{std::move(text)});
    };
    CHECK(quoted("a\"b\\c\n\t\x01") == js_string{"\"a\\\"b\\\\c\\n\\t\\u0001\""});
    CHECK(quoted("\xED\xA0\x80") == js_string{"\"\\ud800\""});
    CHECK(quoted("\xC3\xA9") == js_string{"\"\xC3\xA9\""});
    CHECK(quoted() == js_string{"\"\""});

    // JSON.stringify of a built tree: member order kept, -0 as 0, NaN and the
    // infinities as null, Number::toString spellings, nested arrays.
    {
        using ctbrowser::json_value;
        const auto member = [](const char * key, json_value value) {
            return json_value::member{key, std::move(value)};
        };
        const json_value tree{json_value::object{
            member("v", json_value{1.0}),
            member("n", json_value{-0.0}),
            member("x", json_value{std::numeric_limits<double>::quiet_NaN()}),
            member("y", json_value{-infinity}),
            member("big", json_value{1e21}),
            member("s", json_value{std::string{"q\""}}),
            member("b", json_value{true}),
            member("z", json_value{nullptr}),
            member("a", json_value{json_value::array{json_value{1.5}, json_value{std::string{}}}}),
        }};
        CHECK(ctnative::JSON.stringify(tree) ==
              js_string{"{\"v\":1,\"n\":0,\"x\":null,\"y\":null,\"big\":1e+21,\"s\":\"q\\\"\","
                        "\"b\":true,\"z\":null,\"a\":[1.5,\"\"]}"});
        CHECK(ctnative::JSON.stringify(json_value{json_value::object{}}) == js_string{"{}"});
        CHECK(ctnative::JSON.stringify(json_value{json_value::array{}}) == js_string{"[]"});
        CHECK(ctnative::JSON.stringify(json_value{false}) == js_string{"false"});
    }

    // Arrays of Strings: push returns the new length, an index past the end or
    // a fractional one is undefined, join of none or one element, sort in
    // UTF-16 order (U+1F600 sorts before U+FF21 there, after it by code point).
    {
        const auto strings = [](std::initializer_list<const char *> texts) {
            std::vector<js_string> out;
            for (const char * text : texts) { out.emplace_back(std::string{text}); }
            return out;
        };
        std::vector<js_string> list;
        CHECK(ctnative::Array.prototype.join.call(list, js_string{","}) == js_string{});
        CHECK(vec_length(list) == ctnative::js_num{0.0});
        CHECK(ctnative::Array.prototype.push.call(list, js_string{"b"}) == ctnative::js_num{1.0});
        CHECK(ctnative::Array.prototype.join.call(list, js_string{"."}) == js_string{"b"});
        vec_push(list, js_string{"a"});
        CHECK(vec_at(list, nullable_scalar{1.0}) == to_nullable_string(js_string{"a"}));
        CHECK(vec_at(list, nullable_scalar{-0.0}) == to_nullable_string(js_string{"b"}));
        CHECK(vec_at(list, nullable_scalar{2.0}).tag == nullable_string::kind::undefined);
        CHECK(vec_at(list, nullable_scalar{0.5}).tag == nullable_string::kind::undefined);
        // A proved in-bounds read is the element; rows are std::pair values
        // pushed whole, with a Number|String second position.
        CHECK(vec_element(list, ctnative::js_num{1.0}) == js_string{"a"});
        std::vector<std::pair<js_string, number_string>> rows;
        CHECK(ctnative::Array.prototype.push.call(
                  rows, std::pair<js_string, number_string>{
                            js_string{"@x"}, ctnative::js_num{1.5}}) == ctnative::js_num{1.0});
        ctnative::Array.prototype.push.call(
            rows, std::pair<js_string, number_string>{js_string{"color"}, js_string{"red"}});
        CHECK(std::get<0>(vec_element(rows, ctnative::js_num{1.0})) == js_string{"color"});
        CHECK(number_string_text(std::get<1>(vec_element(rows, ctnative::js_num{0.0}))) ==
              js_string{"1.5"});
        CHECK(ctnative::Array.prototype.join.call(list, js_string{", "}) == js_string{"b, a"});
        auto mixed = strings({"\xEF\xBC\xA1", "\xF0\x9F\x98\x80", "b", "", "B", "ab", "a"});
        ctnative::Array.prototype.sort.call(mixed);
        CHECK(mixed == strings({"", "B", "a", "ab", "b", "\xF0\x9F\x98\x80", "\xEF\xBC\xA1"}));
        // Two halves that meet at the seam become one code point.
        CHECK(ctnative::Array.prototype.join.call(strings({"\xED\xA0\xBD", "\xED\xB8\x80"}),
                                                  js_string{}) == js_string{"\xF0\x9F\x98\x80"});

        CHECK(ctnative::String.prototype.split.call(js_string{"a,b,,c"}, js_string{","}) ==
              strings({"a", "b", "", "c"}));
        CHECK(ctnative::String.prototype.split.call(js_string{"abc"}, js_string{"x"}) ==
              strings({"abc"}));
        CHECK(ctnative::String.prototype.split.call(js_string{""}, js_string{"x"}) ==
              strings({""}));
        CHECK(ctnative::String.prototype.split.call(js_string{""}, js_string{""}).empty());
        CHECK(ctnative::String.prototype.split.call(js_string{"ab"}, js_string{""}) ==
              strings({"a", "b"}));
        CHECK(ctnative::String.prototype.split.call(js_string{"\xF0\x9F\x98\x80"}, js_string{""}) ==
              strings({"\xED\xA0\xBD", "\xED\xB8\x80"}));
        CHECK(ctnative::String.prototype.split.call(js_string{"x\xF0\x9F\x98\x80y"},
                                                    js_string{"\xED\xB8\x80"}) ==
              strings({"x\xED\xA0\xBD", "y"}));
        CHECK(ctnative::String.prototype.split.call(js_string{"a\nb\n"}, js_string{"\n"}) ==
              strings({"a", "b", ""}));

        CHECK(ctnative::String.prototype.split.call(js_string{"btn  btn-primary\tx"},
                                                    whitespace_run) ==
              strings({"btn", "btn-primary", "x"}));
        CHECK(ctnative::String.prototype.split.call(js_string{" a\xC2\xA0"
                                                              "b "},
                                                    whitespace_run) == strings({"", "a", "b", ""}));
        CHECK(ctnative::String.prototype.split.call(js_string{}, whitespace_run) == strings({""}));
        CHECK(ctnative::String.prototype.split.call(js_string{"\xC3\xA9"}, whitespace_run) ==
              strings({"\xC3\xA9"}));
    }

    CHECK(unbound());
    REPORT("native_runtime");
}
