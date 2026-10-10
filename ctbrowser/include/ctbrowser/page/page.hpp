//===- page.hpp - Page umbrella header ------------------------ -*- C++ -*-===//
//
// Page: what a laid-out page answers about its elements, with no script
// engine.
//
//   view            the trees a query reads, borrowed for one call
//   computed_style  getComputedStyle's values
//   client_rects    getBoundingClientRect's and getClientRects' rectangles
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctbrowser/page/client_rects.hpp>
#include <ctbrowser/page/computed_style.hpp>
#include <ctbrowser/page/view.hpp>
