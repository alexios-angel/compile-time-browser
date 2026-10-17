//===- ctcompile/test/Runtime/NativeCallable.cpp - Borrowed callables -----===//
//
// Tests ctnative::function_ref (CTNative/Runtime/Callable.hpp) the way
// generated code uses it: a callee whose parameter is a function_ref, called
// with a local holding a lifted callback bound to its captures by
// std::bind_front. The cases pin that the reference is two pointers and
// trivially copyable, that it refers only to lvalues, that it calls the
// caller's object rather than a copy, and that the declared result type
// converts the callable's answer.
//
// It also tests a closure set: a std::variant of ctnative::bound members,
// called through invoke_callable. The cases pin that two members with one
// signature are still two alternatives, that the member the variant holds is
// the one called, with its own captures, and that the captures are copies.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Runtime/Callable.hpp"

#include <functional>
#include <string>
#include <type_traits>

#include "check.hpp"

namespace {

using ctnative::function_ref;

static_assert(std::is_trivially_copyable_v<function_ref<double(double)>>);
static_assert(sizeof(function_ref<double(double)>) == 2 * sizeof(void *));
// A function name is not an object: generated code binds it first.
static_assert(!std::is_constructible_v<function_ref<double(double)>, double (&)(double)>);
// The callable must be invocable with the declared arguments.
static_assert(!std::is_constructible_v<function_ref<double(double)>, std::string (*&)()>);
// A temporary would be gone by the next statement, so it is refused.
using bound_offset = decltype(std::bind_front(std::declval<double (*)(double, double)>(), 1.0));
static_assert(std::is_constructible_v<function_ref<double(double)>, bound_offset &>);
static_assert(!std::is_constructible_v<function_ref<double(double)>, bound_offset>);

double offset(double by, double value) {
    return value + by;
}

/// The shape of a lifted callee: it calls its callback parameter and keeps
/// nothing.
double twice(function_ref<double(double)> callback, double value) {
    return callback(callback(value));
}

void test_bound_captures_are_passed_first() {
    // `twice(v => v + 3, 1)` with the 3 captured.
    auto bound = std::bind_front(offset, 3.0);
    CHECK(twice(bound, 1.0) == 7.0);
}

void test_capture_free_callbacks_are_bound_too() {
    const auto square = [](double v) { return v * v; };
    auto bound = std::bind_front(square);
    CHECK(twice(bound, 3.0) == 81.0);
}

void test_the_callers_object_is_called_not_a_copy() {
    int calls = 0;
    auto counting = [&calls](double v) {
        ++calls;
        return v;
    };
    CHECK(twice(counting, 5.0) == 5.0);
    CHECK(calls == 2);
}

void test_the_result_converts_to_the_declared_type() {
    const auto pixels = [](const std::string & s) { return s + "px"; };
    const function_ref<std::string(std::string)> suffix = pixels;
    CHECK(suffix("4") == "4px");
    const auto truthy = [](double v) { return v != 0; };
    const function_ref<double(double)> widened = truthy;
    CHECK(widened(2) == 1.0);
}

void test_a_reference_copies_as_the_same_callable() {
    double total = 0;
    auto add = [&total](double v) {
        total += v;
        return total;
    };
    const function_ref<double(double)> first = add;
    const function_ref<double(double)> second = first;
    first(1);
    second(2);
    CHECK(total == 3.0);
}

double scaled(double by, double value) {
    return value * by;
}

/// `c ? (v => v + k) : (v => v * k)`: one signature, two members.
using offset_or_scaled =
    std::variant<ctnative::bound<offset, double>, ctnative::bound<scaled, double>>;
static_assert(std::variant_size_v<offset_or_scaled> == 2);
static_assert(!std::is_same_v<ctnative::bound<offset, double>, ctnative::bound<scaled, double>>);

void test_a_set_calls_the_member_it_holds() {
    offset_or_scaled f = ctnative::bind<offset>(3.0);
    CHECK(ctnative::invoke_callable(f, 2.0) == 5.0);
    f = ctnative::bind<scaled>(3.0);
    CHECK(ctnative::invoke_callable(f, 2.0) == 6.0);
}

void test_a_set_member_copies_its_captures() {
    double k = 2.0;
    const offset_or_scaled f = ctnative::bind<offset>(k);
    k = 100.0;
    CHECK(ctnative::invoke_callable(f, 1.0) == 3.0);
}

} // namespace

int main() {
    test_bound_captures_are_passed_first();
    test_capture_free_callbacks_are_bound_too();
    test_the_callers_object_is_called_not_a_copy();
    test_the_result_converts_to_the_declared_type();
    test_a_reference_copies_as_the_same_callable();
    test_a_set_calls_the_member_it_holds();
    test_a_set_member_copies_its_captures();
    REPORT("native_callable");
}
