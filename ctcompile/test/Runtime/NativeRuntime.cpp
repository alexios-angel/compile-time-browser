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
template <class T>
concept primitive_addable = requires(ctnative::number_string result, const T & value) {
    ctnative::add(result, value);
    ctnative::add(value, result);
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
                            (CHECK(add(value, right) == number_string{expected}), ...);
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
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
