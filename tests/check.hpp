#ifndef CTBROWSER_V2_TEST_CHECK_HPP
#define CTBROWSER_V2_TEST_CHECK_HPP

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <string>
#include <version>

#if defined(__cpp_lib_stacktrace) && __cpp_lib_stacktrace >= 202011L
#    include <stacktrace>
#    define CTBROWSER_TEST_HAS_STACKTRACE 1
#else
// The llvm-mingw cross build has <format> but NOT <stacktrace>, so this is a
// real branch rather than defensive decoration. Checked, not assumed.
#    define CTBROWSER_TEST_HAS_STACKTRACE 0
#endif

// Same shape as the previous engine's tests: a non-zero exit fails ctest, and every failure
// prints its own file:line so a CI log says what broke without a debugger.
inline int ctbrowser_test_failures = 0;

namespace ctbrowser_test {

// PRINT THE VALUE, NOT THE EXPRESSION. CHECK_EQ used to report `a == b` and
// leave you to work out what a and b were, which turns a five-second failure
// into a rebuild-with-printfs.
//
// The `if constexpr` matters: this suite compares bitmaps, spans and engine
// structs as well as ints and strings, and a hard requirement on formattability
// would make CHECK_EQ unusable for exactly the types whose failures are worst.
template <typename T> [[nodiscard]] std::string shown(const T & value) {
    if constexpr (std::formattable<const T &, char>) {
        return std::format("{}", value);
    } else {
        return "<not printable>";
    }
}

// WHY A TEST DIED, when it did not die through CHECK.
//
// An uncaught exception reaches std::terminate and ctest reports "Subprocess
// aborted" with no location at all. That cost real time this session: a dangling
// url_view surfaced as a thrown "leftover" from valid input, and the message
// named the error while nothing named the line.
inline void report_terminate() {
    std::printf("\nTERMINATED without reporting a failure\n");
    if (const std::exception_ptr held = std::current_exception()) {
        try {
            std::rethrow_exception(held);
        } catch (const std::exception & failed) {
            std::printf("  uncaught exception: %s\n", failed.what());
        } catch (...) {
            std::printf("  uncaught exception of a non-std type\n");
        }
    }
#if CTBROWSER_TEST_HAS_STACKTRACE
    // Needs -g to name lines; without it this is still frames and addresses,
    // which beats nothing. tests/CMakeLists.txt asks for both.
    std::printf("%s\n", std::to_string(std::stacktrace::current()).c_str());
#else
    std::printf("  (no <stacktrace> in this toolchain - build on Linux for a trace)\n");
#endif
    std::fflush(stdout);
    std::abort();
}

// Installed by a namespace-scope initialiser, because terminate can happen long
// before main's first statement - a throwing static initialiser, for one.
inline const bool trace_installed = [] {
    std::set_terminate(&report_terminate);
    return true;
}();

} // namespace ctbrowser_test

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++ctbrowser_test_failures;                                                             \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(a, b)                                                                             \
    do {                                                                                           \
        const auto lhs_ = (a);                                                                     \
        const auto rhs_ = (b);                                                                     \
        if (!(lhs_ == rhs_)) {                                                                     \
            std::printf("FAIL %s:%d: %s == %s\n  left:  %s\n  right: %s\n", __FILE__, __LINE__,    \
                        #a, #b, ctbrowser_test::shown(lhs_).c_str(),                               \
                        ctbrowser_test::shown(rhs_).c_str());                                      \
            ++ctbrowser_test_failures;                                                             \
        }                                                                                          \
    } while (0)

#define REPORT(name)                                                                               \
    do {                                                                                           \
        if (ctbrowser_test_failures == 0) { std::printf("ok %s\n", name); }                        \
        return ctbrowser_test_failures == 0 ? 0 : 1;                                               \
    } while (0)

#endif
