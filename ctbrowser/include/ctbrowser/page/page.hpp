//===- page.hpp - Page umbrella header ------------------------ -*- C++ -*-===//
//
// Page: what a laid-out page answers about its elements, with no script
// engine.
//
//   view            the trees a query reads, borrowed for one call
//   computed_style  getComputedStyle's values
//   client_rects    getBoundingClientRect's and getClientRects' rectangles
//   intersection    IntersectionObserver's observations and their updates
//   author_styles   a document's <style> and <link> sheets as one text
//   pipeline        the cascade and layout passes of a frame
//   static_page     a document loaded and laid out with all of the above
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctbrowser/page/author_styles.hpp>
#include <ctbrowser/page/client_rects.hpp>
#include <ctbrowser/page/computed_style.hpp>
#include <ctbrowser/page/intersection.hpp>
#include <ctbrowser/page/pipeline.hpp>
#include <ctbrowser/page/static_page.hpp>
#include <ctbrowser/page/view.hpp>
