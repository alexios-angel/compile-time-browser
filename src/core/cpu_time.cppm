module;

// PROCESS CPU TIME, portably.
//
// Not std::clock(): on some Windows runtimes it returns WALL time since the
// process started, which makes the one number this exists to report a lie -
// and "how much CPU does an idle browser use" is exactly the question it is
// here to answer. That trap was walked into twice: once in the profiler and
// once in the test that measures whether an idle worker pool sleeps.
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/resource.h>
#endif

#include <cstdint>

export module ctbrowser.core:cpu_time;

export namespace ctbrowser {

// Seconds of CPU - user plus kernel - this process has consumed. Summed across
// threads, so a four-thread pool spinning for one second reports four.
[[nodiscard]] inline double process_cpu_seconds() {
#if defined(_WIN32)
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) == 0) { return 0; }
    const auto to_seconds = [](const FILETIME & t) {
        return static_cast<double>((static_cast<std::uint64_t>(t.dwHighDateTime) << 32) |
                                   t.dwLowDateTime) *
               1e-7; // 100ns units
    };
    return to_seconds(kernel) + to_seconds(user);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) { return 0; }
    const auto to_seconds = [](const timeval & t) {
        return static_cast<double>(t.tv_sec) + 1e-6 * static_cast<double>(t.tv_usec);
    };
    return to_seconds(usage.ru_utime) + to_seconds(usage.ru_stime);
#endif
}

} // namespace ctbrowser
