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
// ONE HTML source is the whole application. At COMPILE time the
// page's <style>/<script>/<title> text is extracted and the bricks'
// constexpr value parsers can prove the page well-formed (a JS
// structural break in your page can fail the build via
// ctjs::is_valid). At RUNTIME: cthtml::parse instantiates the mutable
// DOM tree, the script runs against real
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
template <ctc::string Src> using page_t = page<Src>;

template <ctc::string Src> inline constexpr page<Src> source{};

} // namespace ctbrowser

#endif
