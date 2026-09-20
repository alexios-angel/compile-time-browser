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
    static_assert(std::is_constructible_v<js_boolean_t, bool>);
    static_assert(!std::is_convertible_v<bool, js_boolean_t>);
    static_assert(!std::is_convertible_v<js_boolean_t, bool>);
    static_assert(!std::is_constructible_v<double, js_boolean_t>);
    static_assert(!std::is_constructible_v<js_boolean_t, int>);
    static_assert(!std::is_constructible_v<js_boolean_t, double>);
    static_assert(!std::is_constructible_v<js_boolean_t, const char *>);
    static_assert(std::is_same_v<decltype(js_boolean_t{} == js_boolean_t{}), bool>);
    constexpr js_boolean_t enabled{true}, disabled{false};
    static_assert(enabled.to_number() == 1.0 && to_number(disabled) == 0.0);
    CHECK(enabled && !disabled && enabled != disabled);
    CHECK(!std::signbit(to_number(disabled)));
    CHECK(global_boolean(to_nullable(enabled)) == enabled);
    CHECK(to_nullable(disabled).tag == nullable_scalar::kind::boolean);
    CHECK(scalar_equal(enabled, 1.0) && !scalar_strict_equal(enabled, 1.0));
    CHECK(object_strict_equal(object_value{enabled}, object_value{true}));
    CHECK(!object_strict_equal(object_value{enabled}, object_value{1.0}));
    CHECK(boolean_string_truthy(std::variant<js_boolean_t, std::string>{enabled}));
    CHECK(!boolean_string_truthy(std::variant<js_boolean_t, std::string>{disabled}));
    CHECK(!boolean_string_truthy(std::variant<js_boolean_t, std::string>{std::string{}}));
    CHECK(boolean_string_truthy(std::variant<js_boolean_t, std::string>{std::string{"false"}}));

    const std::string high = "\xED\xA0\xBD", low = "\xED\xB8\x80";
    CHECK(string_concat(high, low) == "\xF0\x9F\x98\x80");
    CHECK(string_concat(high, "x") == high + "x");
    CHECK(string_concat("x", low) == "x" + low);
    CHECK(string_concat(std::string("a\0", 2), "b") == std::string("a\0b", 3));
    CHECK(high.size() == 3 && low.size() == 3);
    const std::vector<double> three{10, 20, 30};
    CHECK(vec_at(three, nullable_scalar{-0.5}).value == 10); // the engine truncates
    CHECK(vec_at(three, nullable_scalar{3.0}).tag == nullable_scalar::kind::undefined);
    CHECK(vec_at(three, nullable_scalar::null()).tag == nullable_scalar::kind::undefined);
    CHECK(vec_length(three) == 3);

    auto numbers = make_number_map<js_num>();
    map_set(numbers, NAN, 1.0);
    map_set(numbers, NAN, 2.0);
    map_set(numbers, -0.0, 3.0);
    CHECK(map_size(numbers) == 2 && !std::signbit(numbers->entries[1].first));
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
        CHECK(map_size(map) == 2 && map_has(map, enabled));
        CHECK(map_get_present(map, disabled) == enabled);
        CHECK(map_delete(map, enabled) && !map_has(map, enabled));
    };
    booleanMap(booleans);
    booleanMap(
        std::make_shared<std::map<js_boolean_t, js_boolean_t, map_key_less<js_boolean_t>>>());

    // The associative layout's comparator: every NaN is one key, before all.
    map_key_less<js_num> less;
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
        CHECK(map_size(map) == 9);
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
    scalarMap(std::make_shared<std::map<nullable_scalar, js_num, map_key_less<nullable_scalar>>>());

    const nullable_scalar undefined{undefined_t{}}, null{js_null_t{}}, nan{NAN};
    CHECK(undefined.tag == nullable_scalar::kind::undefined);
    CHECK(null.tag == nullable_scalar::kind::null);
    CHECK(nan.tag == nullable_scalar::kind::number && std::isnan(nan.value));
    CHECK(scalar_strict_equal(undefined, nullable_scalar{}));
    CHECK(scalar_strict_equal(null, nullable_scalar::null()));
    CHECK(scalar_equal(null, undefined));
    CHECK(!scalar_strict_equal(null, undefined));
    CHECK(!scalar_equal(nan, undefined) && !scalar_equal(nan, null));
    CHECK(std::isnan(to_number(undefined)) && to_number(null) == 0.0);
    CHECK(!scalar_truthy(undefined) && !scalar_truthy(null));
    CHECK(scalar_typeof(null) == "object" && scalar_typeof(undefined) == "undefined");
    CHECK(string_equal(nullable_scalar::null(), nullable_string{}));
    CHECK(!string_truthy(nullable_string{std::string()}));
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
