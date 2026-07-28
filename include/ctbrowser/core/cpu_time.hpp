#pragma once

// PROCESS CPU TIME, portably.
//
// Not std::clock(): on some Windows runtimes it returns WALL time since the
// process started, which makes the one number this exists to report a lie -
// and "how much CPU does an idle browser use" is exactly the question it is
// here to answer. That trap was walked into twice: once in the profiler and
// once in the test that measures whether an idle worker pool sleeps.
//
// The implementation is in cpu_time.cpp on purpose: it needs <windows.h> on
// Windows and <sys/resource.h> everywhere else, and neither belongs in a header
// every consumer of the engine parses.

namespace ctbrowser {

// Seconds of CPU - user plus kernel - this process has consumed. Summed across
// threads, so a four-thread pool spinning for one second reports four.
[[nodiscard]] double process_cpu_seconds();

} // namespace ctbrowser
