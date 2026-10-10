//===- ctcompile/probes/deduction.cpp - C++ deduction probe -----*- C++ -*-===//
//
// Measures which C++ deduction features the two supported compilers actually
// accept at the flags generated code is built with. The emitter's design relies
// on these results: each numbered block is one feature, and a block that fails
// to compile is a constraint the emitter must design around, not a bug to fix
// here. Built on both compilers by check-deduction-probe.cmake.
//
//===----------------------------------------------------------------------===//
#include <cmath>
#include <concepts>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// 1. A constrained auto parameter (an abbreviated function template): how
//    generated code specialises a function per call site. Every generated
//    `auto` parameter is constrained.
template <class T, class... Us>
concept one_of = (std::same_as<T, Us> || ...);
double twice(std::same_as<double> auto x) {
    return x * 2.0;
}
double either(one_of<double, int> auto x) {
    return static_cast<double>(x);
}

// 2. A lambda with an explicit template parameter list: how callbacks are
//    emitted without type erasure.
auto apply_twice = []<class T>(T v) { return v + v; };

// 3. Aggregate CTAD (P1816) on a struct shaped like a generated object
//    literal. Without it every construction spells its template arguments.
template <class X, class Y> struct pt {
    X x;
    Y y;
};

// 4. A std::variant visited through an overload set: how union-typed values
//    are lowered.
template <class... Fs> struct overload : Fs... {
    using Fs::operator()...;
};
template <class... Fs> overload(Fs...) -> overload<Fs...>;

// 5. A deduced return type used from a second function in the same
//    translation unit; generated code relies on a deduced return type only
//    within one translation unit.
auto deduced() {
    return 1.5;
}
double uses_deduced() {
    return deduced() * 2.0;
}

// 6/7. std::move_only_function and std::ranges::to are C++23 library features
//    that may be absent (libstdc++ 13.3 lacks them). They are probed by feature
//    macro, so a header that exists without the feature is not taken as support.
#if __has_include(<functional>)
#include <functional>
#endif
#if __has_include(<ranges>)
#include <ranges>
#endif
#ifdef __cpp_lib_move_only_function
#define CT_PROBE_MOVE_ONLY_FUNCTION 1
#else
#define CT_PROBE_MOVE_ONLY_FUNCTION 0
#endif
#ifdef __cpp_lib_ranges_to_container
#define CT_PROBE_RANGES_TO 1
#else
#define CT_PROBE_RANGES_TO 0
#endif

int main() {
    // 1
    const double a = twice(2.0);
    const double b = either(3) + either(4.0);
    // 2
    const double c = apply_twice(1.25);
    // 3: aggregate CTAD, so `pt{1.0, 2.0}` must deduce pt<double, double>.
    auto p = pt{1.0, 2.0};
    static_assert(std::is_same_v<decltype(p), pt<double, double>>,
                  "ctcompile probe: aggregate CTAD");
    auto q = pt{1.0, 2};
    static_assert(std::is_same_v<decltype(q), pt<double, int>>,
                  "ctcompile probe: aggregate CTAD, mixed");
    // 3b: Why the emitter does not construct generated structs with CTAD. A
    // JavaScript `undefined` field is emitted as <cmath>'s `NAN`, which is a
    // float ([cmath.syn]), so `pt{NAN, false}` deduces `pt<float, bool>` where
    // the field is a double. A declaration or assignment converts; a braced
    // initialiser deduces. So the emitter spells the instantiation and assigns.
    auto r = pt{NAN, false};
    static_assert(std::is_same_v<decltype(r), pt<float, bool>>,
                  "ctcompile probe: NAN is a float, so CTAD over it deduces float");
    // 4
    std::variant<double, int> v = 2.5;
    const double d = std::visit(
        overload{[](double x) { return x; }, [](int x) { return static_cast<double>(x); }}, v);
    // 5: a static_assert over the decltype of a deduced declaration, which is how
    // generated code pins a deduced type.
    auto n = deduced();
    static_assert(std::is_same_v<decltype(n), double>, "ctcompile: n @ probe");
    const double e = uses_deduced();
    // Every probed value is used, so -Wunused cannot hide a row.
    const double sum = a + b + c + p.x + p.y + q.x + static_cast<double>(q.y) + d + n + e +
                       (std::isnan(static_cast<double>(r.x)) && !r.y ? 0.0 : 1.0);
    return sum > 0.0 ? CT_PROBE_MOVE_ONLY_FUNCTION * 2 + CT_PROBE_RANGES_TO : 1;
}
