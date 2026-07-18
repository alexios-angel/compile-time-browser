#ifndef CTBROWSER__HPP
#define CTBROWSER__HPP

#include "ctbrowser/page.hpp"
#include "ctbrowser/dom.hpp"
#include "ctbrowser/layout.hpp"
#include "ctbrowser/script.hpp"
#include "ctbrowser/engine.hpp"

// ctbrowser: the web platform, compiled.
//
//   using app = ctbrowser::page<R"(<!DOCTYPE html>
//       <title>counter</title>
//       <style>
//           body    { font-size: 16px; }
//           #count  { color: #ff8800; font-size: 32px; }
//           .zero   { color: gray; }
//       </style>
//       <h1>Clicks</h1>
//       <p id=count class=zero>0</p>
//       <script>
//           let clicks = 0;
//           function onClick(id) {
//               clicks += 1;
//               let el = getElementById("count");
//               el.setText(String(clicks));
//               el.removeClass("zero");
//           }
//       </script>)">;
//
//   int main() { return ctbrowser::run_app<app>(); }  // needs app.hpp + SDL3
//
// ONE HTML source is the whole application. At COMPILE time: cthtml
// parses the page into a DOM type; the <style> text is collected from
// that type and parsed by ctcss (the stylesheet is a type; initial
// styles can be static_asserted); the <script> text is parsed by ctjs
// (a JS syntax error in your page fails the build). At RUNTIME: the
// DOM type instantiates a mutable tree, the script runs against real
// DOM bindings (getElementById, classes, inline styles, <canvas>
// pixels), ctcss's query() restyles after every mutation, a block
// layout pass produces a paint list, and - with ctbrowser/app.hpp -
// SDL3 draws it in a window on Windows, macOS, Linux and friends,
// delivering clicks, keys and frames back into the script.
//
// This header is the ENGINE only (headless, testable); include
// <ctbrowser/app.hpp> for the SDL3 shell.

namespace ctbrowser {

// the page type for a source string; `ctbrowser::source<Src>` is a
// convenient value-form handle to it
template <CTJS_STRING_INPUT Src> using page_t = page<Src>;

#if CTLL_CNTTP_COMPILER_CHECK
template <ctll::fixed_string Src> inline constexpr page<Src> source{};
#endif

} // namespace ctbrowser

#endif
