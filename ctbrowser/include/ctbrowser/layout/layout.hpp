//===- layout.hpp - Layout umbrella header -------------------- -*- C++ -*-===//
//
// Layout: from styled elements to placed geometry.
//
//   values     style strings into layout numbers, parsed where they are used
//   box        the box tree, deliberately not the DOM tree, which is what
//              allows anonymous boxes and what allows concurrency
//   fragment   the immutable output; consumers can read it while the next
//              pass runs
//   algorithm  formatting contexts as types behind a concept, so flex and
//              grid arrive as new types rather than new if-statements
//   flex       the flex formatting context, in its own file because §9's
//              freeze loop is as long as the other three contexts together
//   position   relative and absolute placement, as a pass over the finished
//              fragments: the one part of CSS where a box's position is
//              decided by an ancestor rather than by its parent
//   overflow   the scrollable overflow and scrolling area of a finished box,
//              read off the fragments the same way
//   engine     the driver
//
// Layout results live in the box and fragment trees, never on DOM nodes:
// geometry on the node would make layout impossible to parallelise, add
// layout-only fields to `node`, and leave nowhere for an anonymous box.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctbrowser/layout/algorithm.hpp>
#include <ctbrowser/layout/box.hpp>
#include <ctbrowser/layout/engine.hpp>
#include <ctbrowser/layout/flex.hpp>
#include <ctbrowser/layout/fragment.hpp>
#include <ctbrowser/layout/overflow.hpp>
#include <ctbrowser/layout/position.hpp>
#include <ctbrowser/layout/values.hpp>
