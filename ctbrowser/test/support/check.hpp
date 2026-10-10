//===- ctbrowser/test/support/check.hpp - Test assertions -------*- C++ -*-===//
//
// The assertion macros every ctbrowser test uses (CHECK, CHECK_EQ, REPORT,
// and the sentence-style check()), small file helpers, and handlers that print
// a stack trace when a test dies from an uncaught exception or a fatal signal.
// A test passes when it exits 0; every failure prints its own file:line so a
// log says what broke without a debugger.
//
//===----------------------------------------------------------------------===//
#ifndef CTBROWSER_V2_TEST_CHECK_HPP
#define CTBROWSER_V2_TEST_CHECK_HPP

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <version>

#include <csignal>

// cpptrace rather than <stacktrace>: the llvm-mingw cross build has <format> but
// not <stacktrace>, and Windows-only bugs are the ones that most need a trace.
// cpptrace supports mingw explicitly (StackWalk64, libgcc _Unwind_Backtrace,
// dbghelp) and gives both platforms the same output.
#if __has_include(<cpptrace/cpptrace.hpp>)
#include <cpptrace/cpptrace.hpp>
#define CTBROWSER_TEST_HAS_TRACE 1
#else
#define CTBROWSER_TEST_HAS_TRACE 0
#endif

/// Number of failed checks so far; REPORT() exits non-zero when it is not 0,
/// which fails the test under ctest.
inline int ctbrowser_test_failures = 0;

/// Records a failure when `ok` is false, printing `what`. CHECK() prints the
/// failed expression; this prints a sentence instead, which reads better for
/// layout and page checks ("second block stacks below the first").
inline void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %.*s\n", static_cast<int>(what.size()), what.data());
        ++ctbrowser_test_failures;
    }
}

/// Returns a file's bytes as a string, or an empty string when it is missing.
/// ctest runs from ctbrowser/, so relative paths name files in the checkout.
[[nodiscard]] inline std::string read_file(const std::filesystem::path & path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    std::ostringstream all;
    all << in.rdbuf();
    return all.str();
}

/// Converts text to the byte vector an asset registry takes.
[[nodiscard]] inline std::vector<std::byte> bytes_of(std::string_view text) {
    const auto * begin = reinterpret_cast<const std::byte *>(text.data());
    return {begin, begin + text.size()};
}

namespace ctbrowser_test {

/// Formats a value for CHECK_EQ's failure message, or "<not printable>" when
/// T has no std::formatter. The suite also compares bitmaps, spans and engine
/// structs, so CHECK_EQ must not require formattability.
template <typename T> [[nodiscard]] std::string shown(const T & value) {
    if constexpr (std::formattable<const T &, char>) {
        return std::format("{}", value);
    } else {
        return "<not printable>";
    }
}

/// Terminate handler. An uncaught exception reaches std::terminate, and ctest
/// then reports only "Subprocess aborted" with no location. This prints the
/// exception and a stack trace first.
inline void report_terminate() {
    std::printf("\nTERMINATED without reporting a failure\n");
    if (const std::exception_ptr held = std::current_exception()) {
        try {
            std::rethrow_exception(held);
        } catch (const std::exception & failed) {
            std::printf("  uncaught exception: %s\n", failed.what());
        } catch (...) { std::printf("  uncaught exception of a non-std type\n"); }
    }
#if CTBROWSER_TEST_HAS_TRACE
    // Needs -g to name lines; without it this is still frames and addresses,
    // which beats nothing. cmake/modules/CTTest.cmake asks for both.
    cpptrace::generate_trace().print();
#else
    std::printf("  (no cpptrace in this toolchain - install it for a trace)\n");
#endif
    std::fflush(stdout);
    std::abort();
}

/// Fatal-signal handler. A segfault is not an exception: std::terminate never
/// runs and ctest reports only "Subprocess aborted". Print a trace, flush stdout
/// (buffered output is lost otherwise), then re-raise with the default handler
/// so the shell still sees a signal death.
extern "C" inline void report_signal(int number) {
    const char * name = number == SIGSEGV   ? "SIGSEGV (bad memory access)"
                        : number == SIGABRT ? "SIGABRT"
                        : number == SIGFPE  ? "SIGFPE (arithmetic)"
                        : number == SIGILL  ? "SIGILL (illegal instruction)"
                                            : "a fatal signal";
    std::printf("\nDIED ON %s\n", name);
#if CTBROWSER_TEST_HAS_TRACE
    // Strictly, only async-signal-safe calls belong in a handler and this is
    // not one. It is the right trade for a test binary: the process is already
    // dying, and a trace that occasionally deadlocks beats no trace at all.
    cpptrace::generate_trace().print();
#endif
    std::fflush(stdout);
    std::signal(number, SIG_DFL);
    std::raise(number);
}

/// Installs both handlers from a namespace-scope initialiser, because terminate
/// can happen before main's first statement (a throwing static initialiser).
inline const bool trace_installed = [] {
    std::set_terminate(&report_terminate);
    for (const int fatal : {SIGSEGV, SIGABRT, SIGFPE, SIGILL}) {
        std::signal(fatal, &report_signal);
    }
    return true;
}();

} // namespace ctbrowser_test

/// Fails the test, printing the expression and its location, when `cond` is false.
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++ctbrowser_test_failures;                                                             \
        }                                                                                          \
    } while (0)

/// Fails the test when `a != b`, printing both values (see shown()).
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

/// Prints "ok <name>" when nothing failed and returns main's exit status.
#define REPORT(name)                                                                               \
    do {                                                                                           \
        if (ctbrowser_test_failures == 0) { std::printf("ok %s\n", name); }                        \
        return ctbrowser_test_failures == 0 ? 0 : 1;                                               \
    } while (0)

#endif
