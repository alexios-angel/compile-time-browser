// THE RUNTIME HEADER, COMPILED AND RUN - the one translation unit in the build
// that includes Runtime/ctnative.hpp the way a generated program does, with
// both of its switches on, under -Werror. Until 2026-09-15 the helpers were
// string literals that nothing compiled until a generated program did, so a
// warning in one was found by whichever native test happened to emit it.
//
// The checks are the contracts a generated program relies on and no lit test
// pins directly: an out-of-range index is undefined, a NaN key is one key, -0
// is stored as +0, a snapshot keeps insertion order across delete and
// reinsert, and null == undefined but null !== undefined.
#define CTNATIVE_DOM 1
#define CTNATIVE_ORDERED_MAPS 1
#include "ctcompile/CTNative/Runtime/ctnative.hpp"

#include <cstdio>
#include <cstdlib>

namespace ctnative {
struct identity_object {};
} // namespace ctnative

namespace {
template <class T>
concept string_addable = requires(ctnative::js_string text, T value) {
    text + value;
    value + text;
};
struct convertible_number {
    operator double() const { return 3.0; }
};
enum unrelated_number {
    three = 3
};

int failures = 0;
void check(bool value, const char * message) {
    if (value) { return; }
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}
} // namespace

// The build is -DNDEBUG, so assert() would check nothing: every line is a check().
#define CHECK(expression) check(static_cast<bool>(expression), #expression)

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
    const std::vector<double> three{10, 20, 30};
    CHECK(vec_at(three, nullable_scalar{-0.5}).value == 10); // the engine truncates
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
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
