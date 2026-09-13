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
// It resolves an element ONCE into a whole computed style, and it takes a document
// read transaction rather than the live tree, so matching observes a stable view
// while a page mutates.

#include <ctbrowser/style/computed.hpp>
#include <ctbrowser/style/css/boolean.hpp>
#include <ctbrowser/style/css/calc.hpp>
#include <ctbrowser/style/css/media.hpp>
#include <ctbrowser/style/css/parser.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <ctbrowser/style/css/substitute.hpp>
#include <ctbrowser/style/css/token.hpp>
#include <ctbrowser/style/easing.hpp>
#include <ctbrowser/style/engine.hpp>
#include <ctbrowser/style/selector.hpp>
#include <ctbrowser/style/ua.hpp>
