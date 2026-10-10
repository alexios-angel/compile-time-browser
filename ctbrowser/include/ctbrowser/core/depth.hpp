//===- depth.hpp - RAII recursion depth counter --------------- -*- C++ -*-===//
//
// depth_guard, one level of a recursion that counts its own depth.
//
//===----------------------------------------------------------------------===//

#pragma once

namespace ctbrowser {

/// One level of a recursion that counts its own depth. A parser or walk that
/// recurses once per level of an author's nesting overflows the C++ stack on
/// `((((...`, so each such recursion keeps a counter, refuses past its own
/// limit, and holds one of these per level: the count is restored on every
/// exit, exceptions included.
///
///     if (depth_ >= max_depth) { return refused(); }
///     const depth_guard level{depth_};
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
