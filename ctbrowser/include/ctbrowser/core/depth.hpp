#pragma once

// ONE LEVEL OF A RECURSION THAT COUNTS ITS OWN DEPTH. A parser or a walk that
// recurses once per level of an author's nesting overflows the C++ stack on
// `((((...` long before it runs out of anything else, so each such recursion
// keeps a counter, refuses past its own limit, and holds one of these for
// every level it enters: the count is back where it was on every way out,
// exceptions included.
//
//     if (depth_ >= max_depth) { return refused(); }
//     const depth_guard level{depth_};

namespace ctbrowser {

class depth_guard {
public:
    explicit depth_guard(int & at) : at_(at) { ++at_; }
    ~depth_guard() { --at_; }
    depth_guard(const depth_guard &) = delete;
    depth_guard & operator=(const depth_guard &) = delete;
    depth_guard(depth_guard &&) = delete;
    depth_guard & operator=(depth_guard &&) = delete;

private:
    int & at_;
};

} // namespace ctbrowser
