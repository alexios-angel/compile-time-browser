// THE DEDUCTION PROBE - part 24 §3.3, constraint 3.
//
// §3.2 of the plan lists what the two devbox compilers are EXPECTED to
// accept; this file is what they are MEASURED to accept, at the flags the
// generated code is built with. Phases 56 to 62½ plan against this file, not
// against the table. Each block is one row of §3.2, and a row that fails to
// compile is a fact the emitter has to design around, not a bug to fix here.
//
// Built by check-deduction-probe.cmake on both compilers; the result of each
// feature is recorded in part 24 §3.1.
#include <concepts>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// 1. A constrained auto parameter (an abbreviated function template) - the
//    vehicle for Phase 62½-B's per-call-site specialisation, with Stage 53E's
//    rule that every generated `auto` parameter is constrained.
template <class T, class... Us>
concept one_of = (std::same_as<T, Us> || ...);
double twice(std::same_as<double> auto x) {
    return x * 2.0;
}
double either(one_of<double, int> auto x) {
    return static_cast<double>(x);
}

// 2. A lambda with an explicit template parameter list - Stage 59A's
//    non-erased callbacks.
auto apply_twice = []<class T>(T v) { return v + v; };

// 3. Aggregate CTAD (P1816) on a generated-shaped struct - Phase 56C's
//    construction rule. Without it every construction spells its arguments.
template <class X, class Y> struct pt {
    X x;
    Y y;
};

// 4. A std::variant visited through an overload set - Stage 53G's lowering.
template <class... Fs> struct overload : Fs... {
    using Fs::operator()...;
};
template <class... Fs> overload(Fs...) -> overload<Fs...>;

// 5. A deduced return type used from a second function in the SAME
//    translation unit - the never-deduce list allows it only here.
auto deduced() {
    return 1.5;
}
double uses_deduced() {
    return deduced() * 2.0;
}

// 6/7. std::move_only_function and std::ranges::to are C++23 library features
//    §3.2 expects to be ABSENT in libstdc++ 13.3; probed by feature macro so a
//    header that exists without the feature is not mistaken for support.
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
    // 3: aggregate CTAD - `pt{1.0, 2.0}` must deduce pt<double, double>
    auto p = pt{1.0, 2.0};
    static_assert(std::is_same_v<decltype(p), pt<double, double>>,
                  "ctcompile probe: aggregate CTAD");
    auto q = pt{1.0, 2};
    static_assert(std::is_same_v<decltype(q), pt<double, int>>,
                  "ctcompile probe: aggregate CTAD, mixed");
    // 4
    std::variant<double, int> v = 2.5;
    const double d = std::visit(
        overload{[](double x) { return x; }, [](int x) { return static_cast<double>(x); }}, v);
    // 5: a static_assert over the decltype of a deduced declaration (Stage 53F's pin)
    auto n = deduced();
    static_assert(std::is_same_v<decltype(n), double>, "ctcompile: n @ probe");
    const double e = uses_deduced();
    // Every probed value is used, so -Wunused cannot hide a row.
    const double sum = a + b + c + p.x + p.y + q.x + static_cast<double>(q.y) + d + n + e;
    return sum > 0.0 ? CT_PROBE_MOVE_ONLY_FUNCTION * 2 + CT_PROBE_RANGES_TO : 1;
}
