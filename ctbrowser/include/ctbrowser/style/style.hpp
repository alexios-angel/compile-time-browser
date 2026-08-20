#pragma once
// Style resolution.
//
//   computed   the resolved style of one element, interned and refcounted so
//              the thousands of elements that resolve identically share one
//   selector   compiled selectors, bucketed by their rightmost simple
//              selector, plus the counting ancestor filter
//   engine     matching and the cascade
//   css/       the CSS front end - the tokenizer, and above it the grammar
//
// Two things differ from the previous engine beyond speed. It resolves an element ONCE into a
// whole computed style rather than answering one property at a time by
// rescanning the sheet. And it takes a document read transaction rather than
// the live tree, so matching observes a stable view and writes nothing that
// another thread can see - which is what makes it safe to run in parallel.

#include <ctbrowser/style/computed.hpp>
#include <ctbrowser/style/css/calc.hpp>
#include <ctbrowser/style/css/media.hpp>
#include <ctbrowser/style/css/parser.hpp>
#include <ctbrowser/style/css/substitute.hpp>
#include <ctbrowser/style/css/token.hpp>
#include <ctbrowser/style/engine.hpp>
#include <ctbrowser/style/selector.hpp>
#include <ctbrowser/style/ua.hpp>
