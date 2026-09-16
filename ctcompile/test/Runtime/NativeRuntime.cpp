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
#define CHECK(expression) check((expression), #expression)

int main() {
    using namespace ctnative;
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

    // The associative layout's comparator: every NaN is one key, before all.
    map_key_less<js_num> less;
    CHECK(!less(NAN, NAN) && less(NAN, 1.0) && !less(1.0, NAN));

    CHECK(scalar_equal(nullable_scalar::null(), nullable_scalar{}));
    CHECK(!scalar_strict_equal(nullable_scalar::null(), nullable_scalar{}));
    CHECK(scalar_typeof(nullable_scalar::null()) == "object");
    CHECK(string_equal(nullable_scalar::null(), nullable_string{}));
    CHECK(!string_truthy(nullable_string{std::string()}));
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
