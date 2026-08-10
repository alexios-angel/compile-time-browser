#pragma once
// Layout: from styled elements to placed geometry.
//
//   values     style strings into layout numbers, parsed where they are used
//   box        the BOX TREE - deliberately not the DOM tree, which is what
//              allows anonymous boxes and what allows concurrency
//   fragment   the immutable OUTPUT; consumers can read it while the next
//              pass runs
//   algorithm  formatting contexts as types behind a concept, so flex and
//              grid arrive as new types rather than new if-statements
//   flex       the fourth of those, in its own file because §9's freeze
//              loop is as long as the other three contexts together
//   engine     the driver, sequential and parallel
//
// the previous engine wrote geometry back onto the DOM node. That single decision is what made
// its layout unparallelisable, what put thirty layout-only fields on `node`,
// and what left nowhere to put a box with no element behind it.

#include <ctbrowser/layout/algorithm.hpp>
#include <ctbrowser/layout/box.hpp>
#include <ctbrowser/layout/engine.hpp>
#include <ctbrowser/layout/flex.hpp>
#include <ctbrowser/layout/fragment.hpp>
#include <ctbrowser/layout/values.hpp>
