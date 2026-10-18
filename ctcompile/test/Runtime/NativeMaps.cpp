//===- ctcompile/test/Runtime/NativeMaps.cpp - Map keys of two kinds ------===//
//
// Tests the tagged structs a Map stores a key or payload of two kinds in
// (ctnative::boolean_or_number, boolean_or_string, nullable_boolean_or_string,
// and object_value for a Number-or-object key), in both storage layouts: the
// insertion-ordered vector and std::map with map_key_less.
//
// The cases pin SameValueZero (ECMA-262 7.2.11): NaN is one key, -0 is
// stored as +0 while a payload keeps its sign, and keys of different kinds
// never meet, so `true` is not 1 or "true", and null, undefined, "null" and
// "undefined" are four keys. They also pin that map_key_less agrees with
// map_key_equal, which std::map needs, and that each typed read returns the
// alternative the payload holds as an owned copy.
//
//===----------------------------------------------------------------------===//
#define CTNATIVE_ORDERED_MAPS 1
#include "ctcompile/CTNative/Runtime/ctnative.hpp"

#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "check.hpp"

namespace ctnative {
struct identity_object {};
} // namespace ctnative

namespace {

using namespace ctnative;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInfinity = std::numeric_limits<double>::infinity();

/// map_key_less must be a strict weak order whose equivalence is exactly
/// map_key_equal, or std::map storage and the ordered storage disagree.
template <class K> void check_order_agrees_with_equality(const std::vector<K> & keys) {
    map_key_less<K> less;
    for (const auto & left : keys) {
        CHECK(!less(left, left));
        for (const auto & right : keys) {
            CHECK(map_key_equal(left, right) == (!less(left, right) && !less(right, left)));
            for (const auto & third : keys) {
                CHECK(!(less(left, right) && less(right, third)) || less(left, third));
            }
        }
    }
}

/// Inserts every key with payload -0 into `map` and returns its size.
template <class Map, class K> double insert_all(const Map & map, const std::vector<K> & keys) {
    for (const auto & key : keys) { map_set(map, key, -0.0); }
    return map_size(map).value();
}

void test_boolean_or_number_keys() {
    const std::vector<boolean_or_number> keys{
        js_boolean_t{false}, js_boolean_t{true}, -0.0, 0.0, kNaN, -kNaN,
        -kInfinity,          kInfinity,          1.0};
    check_order_agrees_with_equality(keys);
    // true and 1, false and 0 are different keys; -0 and 0, and every NaN, are one.
    CHECK(!map_key_equal(boolean_or_number{js_boolean_t{true}}, boolean_or_number{1.0}));
    CHECK(!map_key_equal(boolean_or_number{js_boolean_t{false}}, boolean_or_number{0.0}));
    CHECK(map_key_equal(boolean_or_number{-0.0}, boolean_or_number{0.0}));
    CHECK(map_key_equal(boolean_or_number{kNaN}, boolean_or_number{-kNaN}));
    const auto layout = [&](const auto & map) {
        CHECK(insert_all(map, keys) == 7.0);
        const auto zero = map->find(boolean_or_number{0.0});
        CHECK(zero != map->end() && zero->first.tag == boolean_or_number::kind::number);
        CHECK(!std::signbit(zero->first.value) && std::signbit(zero->second));
        CHECK(map_has(map, boolean_or_number{-kNaN}) && map_has(map, boolean_or_number{true}));
        CHECK(map_delete(map, boolean_or_number{js_boolean_t{true}}));
        CHECK(!map_has(map, boolean_or_number{js_boolean_t{true}}));
        CHECK(map_has(map, boolean_or_number{1.0}));
    };
    layout(make_number_map<boolean_or_number>());
    layout(
        std::make_shared<std::map<boolean_or_number, double, map_key_less<boolean_or_number>>>());
}

void test_boolean_or_string_keys() {
    const std::vector<boolean_or_string> keys{
        js_boolean_t{false}, js_boolean_t{true},     std::string{},   std::string{"false"},
        std::string{"true"}, std::string{"a\0b", 3}, std::string{"a"}};
    check_order_agrees_with_equality(keys);
    CHECK(!map_key_equal(boolean_or_string{js_boolean_t{true}}, boolean_or_string{"true"}));
    CHECK(!map_key_equal(boolean_or_string{js_boolean_t{false}}, boolean_or_string{""}));
    CHECK(map_key_equal(boolean_or_string{"a"}, boolean_or_string{std::string{"a"}}));
    const auto layout = [&](const auto & map) {
        CHECK(insert_all(map, keys) == 7.0);
        CHECK(map_has(map, boolean_or_string{std::string{"a\0b", 3}}));
        CHECK(!map_has(map, boolean_or_string{std::string{"a\0c", 3}}));
        CHECK(map_delete(map, boolean_or_string{js_boolean_t{false}}));
        CHECK(map_has(map, boolean_or_string{"false"}) && map_has(map, boolean_or_string{""}));
    };
    layout(make_number_map<boolean_or_string>());
    layout(
        std::make_shared<std::map<boolean_or_string, double, map_key_less<boolean_or_string>>>());
}

void test_nullable_boolean_or_string_keys() {
    nullable_string null_text;
    null_text.tag = nullable_string::kind::null_value;
    const std::vector<nullable_boolean_or_string> keys{nullable_string{},
                                                       null_text,
                                                       js_boolean_t{false},
                                                       js_boolean_t{true},
                                                       nullable_string{std::string{}},
                                                       nullable_string{"null"},
                                                       nullable_string{"undefined"},
                                                       nullable_string{"false"}};
    check_order_agrees_with_equality(keys);
    CHECK(nullable_boolean_or_string{null_text}.tag == nullable_boolean_or_string::kind::null);
    CHECK(nullable_boolean_or_string{nullable_string{}}.tag ==
          nullable_boolean_or_string::kind::undefined);
    CHECK(!map_key_equal(nullable_boolean_or_string{null_text}, nullable_boolean_or_string{}));
    CHECK(!map_key_equal(nullable_boolean_or_string{null_text},
                         nullable_boolean_or_string{nullable_string{"null"}}));
    const auto layout = [&](const auto & map) {
        CHECK(insert_all(map, keys) == 8.0);
        CHECK(map_delete(map, nullable_boolean_or_string{null_text}));
        CHECK(!map_has(map, nullable_boolean_or_string{null_text}));
        CHECK(map_has(map, nullable_boolean_or_string{}));
        CHECK(map_has(map, nullable_boolean_or_string{nullable_string{"null"}}));
    };
    layout(make_number_map<nullable_boolean_or_string>());
    layout(std::make_shared<std::map<nullable_boolean_or_string, double,
                                     map_key_less<nullable_boolean_or_string>>>());
}

void test_number_or_object_keys() {
    const auto first = std::make_shared<identity_object>();
    const auto second = std::make_shared<identity_object>();
    const std::vector<object_value> keys{-0.0, 0.0, kNaN, -kNaN, 1.0, first, second};
    check_order_agrees_with_equality(keys);
    // Objects compare by identity, never by contents or against a Number.
    CHECK(!map_key_equal(object_value{first}, object_value{second}));
    CHECK(map_key_equal(object_value{first}, object_value{first}));
    CHECK(!map_key_equal(object_value{first}, object_value{0.0}));
    const auto layout = [&](const auto & map) {
        CHECK(insert_all(map, keys) == 5.0);
        const auto zero = map->find(object_value{0.0});
        CHECK(zero != map->end() && !zero->first.object && !std::signbit(zero->first.scalar.value));
        CHECK(std::signbit(zero->second));
        CHECK(map_delete(map, object_value{first}) && !map_has(map, object_value{first}));
        CHECK(map_has(map, object_value{second}) && map_has(map, object_value{-kNaN}));
    };
    layout(make_number_map<object_value>());
    layout(std::make_shared<std::map<object_value, double, map_key_less<object_value>>>());
}

void test_typed_reads_return_the_alternative_held() {
    const std::string text = "an owned payload longer than the small String buffer";
    const auto numbers = make_map<std::string, boolean_or_number>();
    map_set(numbers, std::string{"n"}, boolean_or_number{-0.0});
    map_set(numbers, std::string{"b"}, boolean_or_number{js_boolean_t{true}});
    CHECK(std::signbit(map_get_present_as<double>(numbers, std::string{"n"})));
    CHECK(map_get_present_as<js_boolean_t>(numbers.get(), std::string{"b"}) == js_boolean_t{true});

    const auto strings = make_map<double, boolean_or_string>();
    map_set(strings, 1.0, boolean_or_string{text});
    map_set(strings, 2.0, boolean_or_string{js_boolean_t{false}});
    const auto saved = map_get_present_as<std::string>(strings, 1.0);
    CHECK(map_get_present_as<js_boolean_t>(strings, 2.0) == js_boolean_t{false});
    map_set(strings, 1.0, boolean_or_string{std::string{"overwritten"}});
    map_delete(strings, 1.0);
    CHECK(saved == text);

    nullable_string null_text;
    null_text.tag = nullable_string::kind::null_value;
    const auto nullable = make_map<double, nullable_boolean_or_string>();
    map_set(nullable, 1.0, nullable_boolean_or_string{nullable_string{text}});
    map_set(nullable, 2.0, nullable_boolean_or_string{null_text});
    map_set(nullable, 3.0, nullable_boolean_or_string{});
    map_set(nullable, 4.0, nullable_boolean_or_string{js_boolean_t{true}});
    CHECK(map_get_present_nullable_as<std::string>(nullable, 1.0) == text);
    const auto held = map_get_present_nullable_as<nullable_string>(nullable.get(), 1.0);
    CHECK(held.tag == nullable_string::kind::string && held.value == text);
    CHECK(map_get_present_nullable_as<nullable_string>(nullable, 2.0).tag ==
          nullable_string::kind::null_value);
    CHECK(map_get_present_nullable_as<nullable_string>(nullable, 3.0).tag ==
          nullable_string::kind::undefined);
    CHECK(map_get_present_nullable_as<js_boolean_t>(nullable, 4.0) == js_boolean_t{true});
}

} // namespace

int main() {
    test_boolean_or_number_keys();
    test_boolean_or_string_keys();
    test_nullable_boolean_or_string_keys();
    test_number_or_object_keys();
    test_typed_reads_return_the_alternative_held();
    REPORT("native_maps");
}
