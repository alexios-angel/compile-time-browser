//===- ctcompile/test/Runtime/NativeArrayCallbacks.cpp - Array callbacks --===//
//
// Tests the Array.prototype callback methods of the native runtime
// (CTNative/Runtime/Array.hpp): forEach, map, filter, some, every, find and
// reduce, called as generated code spells them,
// `ctnative::Array.prototype.filter.call(v, callback)`. Each case pins one rule
// of ECMA-262 23.1.3: the length read once, a removed index skipped, the
// element read before the call, ToBoolean of the result, the stopping points,
// and the answer for no elements. The last case reads a borrowed array (a
// std::span) through every helper and method that takes one.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Runtime/Array.hpp"
#include "ctcompile/CTNative/Runtime/ctnative.hpp"

#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "check.hpp"

namespace {

using ctnative::Array;
using ctnative::js_boolean_t;
using ctnative::js_num;
using ctnative::js_string;

[[nodiscard]] js_num num(double value) {
    return js_num{value};
}
[[nodiscard]] std::vector<js_num> nums(std::initializer_list<double> values) {
    std::vector<js_num> out;
    for (const double v : values) { out.push_back(js_num{v}); }
    return out;
}

void test_for_each_visits_in_order_with_the_index() {
    const std::vector<js_num> v = nums({10, 20, 30});
    double sum = 0, indexes = 0;
    const ctnative::undefined_t answer =
        Array.prototype.forEach.call(v, [&](const js_num & x, js_num k) {
            sum += x.value();
            indexes += k.value();
        });
    (void)answer;
    CHECK(sum == 60 && indexes == 3);
    // A callback may take the array too, or only the element.
    std::size_t seen = 0;
    Array.prototype.forEach.call(
        v, [&](const js_num &, js_num, const std::vector<js_num> & a) { seen += a.size(); });
    CHECK(seen == 9);
}

void test_the_length_is_read_once() {
    // Elements a callback appends are not visited (step 2 reads the length
    // once); one it removes is skipped (HasProperty is false for it).
    std::vector<js_num> v = nums({1, 2, 3});
    int calls = 0;
    const std::vector<js_num> kept = Array.prototype.filter.call(v, [&](const js_num & x) {
        ++calls;
        if (x.value() == 1) { v.push_back(js_num{4.0}); }
        return js_boolean_t{true};
    });
    CHECK(calls == 3);
    CHECK(kept.size() == 3);

    std::vector<js_num> shrinking = nums({1, 2, 3, 4});
    calls = 0;
    Array.prototype.forEach.call(shrinking, [&](const js_num &) {
        ++calls;
        shrinking.pop_back();
    });
    CHECK(calls == 2);
}

void test_filter_keeps_the_value_read_before_the_call() {
    std::vector<js_string> v{js_string{"a"}, js_string{"b"}};
    const std::vector<js_string> kept = Array.prototype.filter.call(v, [&](const js_string & s) {
        // Rewriting the element does not change what filter keeps: it pushes
        // the value it read (kValue), not the array's current one.
        if (s.value() == "a") { v[0] = js_string{"changed"}; }
        return js_boolean_t{true};
    });
    CHECK(kept.size() == 2 && kept[0].value() == "a");
}

void test_truthiness_of_the_result() {
    // ToBoolean: 0 and NaN are false, other Numbers true; "" is false.
    const std::vector<js_num> v = nums({0, 1, std::nan(""), -2});
    const std::vector<js_num> kept =
        Array.prototype.filter.call(v, [](const js_num & x) { return x; });
    CHECK(kept.size() == 2 && kept[0].value() == 1 && kept[1].value() == -2);
    const std::vector<js_string> s{js_string{""}, js_string{"x"}};
    const std::vector<js_string> named =
        Array.prototype.filter.call(s, [](const js_string & x) { return x; });
    CHECK(named.size() == 1 && named[0].value() == "x");
}

void test_map_answers_one_result_per_element() {
    const std::vector<js_num> v = nums({1, 2, 3});
    const std::vector<js_string> out = Array.prototype.map.call(
        v, [](const js_num & x, js_num k) { return js_string{"k"} + k + js_string{"="} + x; });
    CHECK(out.size() == 3 && out[2].value() == "k2=3");
    CHECK(Array.prototype.map.call(std::vector<js_num>{}, [](const js_num & x) { return x; })
              .empty());
}

void test_some_and_every_stop_early() {
    const std::vector<js_num> v = nums({1, 2, 3, 4});
    int calls = 0;
    CHECK(static_cast<bool>(Array.prototype.some.call(v, [&](const js_num & x) {
        ++calls;
        return js_boolean_t{x.value() == 2};
    })));
    CHECK(calls == 2);
    calls = 0;
    CHECK(!static_cast<bool>(Array.prototype.every.call(v, [&](const js_num & x) {
        ++calls;
        return js_boolean_t{x.value() < 3};
    })));
    CHECK(calls == 3);
    // No elements: some is false, every is true.
    const std::vector<js_num> none;
    CHECK(!static_cast<bool>(
        Array.prototype.some.call(none, [](const js_num &) { return js_boolean_t{true}; })));
    CHECK(static_cast<bool>(
        Array.prototype.every.call(none, [](const js_num &) { return js_boolean_t{false}; })));
}

struct point {
    double x = 0;
};

void test_find_answers_the_element_or_undefined() {
    const std::vector<js_string> s{js_string{"btn"}, js_string{"card"}};
    const ctnative::nullable_string hit = Array.prototype.find.call(
        s, [](const js_string & x) { return js_boolean_t{x.value() == "card"}; });
    CHECK(hit.tag == ctnative::nullable_string::kind::string && hit.value == "card");
    const ctnative::nullable_string miss =
        Array.prototype.find.call(s, [](const js_string &) { return js_boolean_t{false}; });
    CHECK(miss.tag == ctnative::nullable_string::kind::undefined);

    // A Number element comes back in the scalar carrier, NaN included.
    const std::vector<js_num> v = nums({1, std::nan("")});
    const ctnative::nullable_scalar nan = Array.prototype.find.call(
        v, [](const js_num & x) { return js_boolean_t{std::isnan(x.value())}; });
    CHECK(nan.tag == ctnative::nullable_scalar::kind::number && std::isnan(nan.value));

    // Any other element type answers std::optional.
    const std::vector<point> points{{1}, {2}};
    const std::optional<point> second =
        Array.prototype.find.call(points, [](const point & p) { return p.x == 2; });
    CHECK(second && second->x == 2);
}

void test_map_and_find_refuse_a_shortened_array() {
    // The compiler must not admit these; the runtime fails loudly rather than
    // answering differently from the VM.
    for (int which = 0; which < 2; ++which) {
        std::vector<js_num> v = nums({1, 2, 3});
        bool refused = false;
        try {
            const auto shorten = [&](const js_num &) {
                v.pop_back();
                return js_boolean_t{false};
            };
            if (which == 0) {
                (void)Array.prototype.map.call(v, shorten);
            } else {
                (void)Array.prototype.find.call(v, shorten);
            }
        } catch (const std::logic_error &) { refused = true; }
        CHECK(refused);
    }
}

void test_reduce_threads_the_accumulator() {
    const std::vector<js_num> v = nums({1, 2, 3});
    const js_num sum = Array.prototype.reduce.call(
        v, [](js_num acc, const js_num & x) { return acc + x; }, num(0));
    CHECK(sum.value() == 6);
    // The accumulator's type need not be the element's, and the callback may
    // take the index and the array.
    const js_string joined = Array.prototype.reduce.call(
        v,
        [](js_string acc, const js_num & x, js_num k, const std::vector<js_num> &) {
            return acc + k + js_string{":"} + x + js_string{" "};
        },
        js_string{""});
    CHECK(joined.value() == "0:1 1:2 2:3 ");
    // No elements: the initial value.
    CHECK(Array.prototype.reduce
              .call(
                  std::vector<js_num>{}, [](js_num acc, const js_num &) { return acc; }, num(7))
              .value() == 7);
}

void test_borrowed_arrays_answer_as_owned_ones() {
    // A function that only reads an array parameter borrows it as a
    // std::span; every read answers what it does on the owned array.
    const std::vector<js_string> owned{js_string{"a"}, js_string{"b"}, js_string{"c"}};
    const std::span<const js_string> view{owned};
    CHECK(ctnative::vec_length(view).value() == 3);
    const ctnative::nullable_string second = ctnative::vec_at(view, ctnative::nullable_scalar{1.0});
    CHECK(second.tag == ctnative::nullable_string::kind::string && second.value == "b");
    CHECK(ctnative::vec_at(view, ctnative::nullable_scalar{3.0}).tag ==
          ctnative::nullable_string::kind::undefined);
    CHECK(ctnative::vec_element(view, num(2)).value() == "c");
    CHECK(Array.prototype.join.call(view, js_string{"-"}).value() == "a-b-c");
    CHECK(Array.prototype.join.call(owned, js_string{"-"}).value() == "a-b-c");

    // The callback methods take the span too, and pass it as the array.
    const std::vector<js_string> kept = Array.prototype.filter.call(
        view, [](const js_string & s, js_num, const std::span<const js_string> & a) {
            return js_boolean_t{s.value() != "b" && a.size() == 3};
        });
    CHECK(kept.size() == 2 && kept[1].value() == "c");
    const std::vector<js_num> lengths =
        Array.prototype.map.call(view, [](const js_string &, js_num k) { return k; });
    CHECK(lengths.size() == 3 && lengths[2].value() == 2);
    CHECK(static_cast<bool>(Array.prototype.some.call(
        view, [](const js_string & s) { return js_boolean_t{s.value() == "c"}; })));
    CHECK(
        static_cast<bool>(Array.prototype.every.call(view, [](const js_string & s) { return s; })));
    CHECK(Array.prototype.find
              .call(view, [](const js_string & s) { return js_boolean_t{s.value() == "b"}; })
              .value == "b");
    int calls = 0;
    Array.prototype.forEach.call(view, [&](const js_string &) { ++calls; });
    CHECK(calls == 3);
    CHECK(Array.prototype.reduce
              .call(
                  view, [](js_string acc, const js_string & s) { return acc + s; }, js_string{""})
              .value() == "abc");
}

} // namespace

int main() {
    test_for_each_visits_in_order_with_the_index();
    test_the_length_is_read_once();
    test_filter_keeps_the_value_read_before_the_call();
    test_truthiness_of_the_result();
    test_map_answers_one_result_per_element();
    test_some_and_every_stop_early();
    test_find_answers_the_element_or_undefined();
    test_map_and_find_refuse_a_shortened_array();
    test_reduce_threads_the_accumulator();
    test_borrowed_arrays_answer_as_owned_ones();
    REPORT("native_array_callbacks");
}
