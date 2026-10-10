//===- ctbrowser/lib/Shell/browser/internal.hpp - Internals -----*- C++ -*-===//
//
// Private to lib/Shell/browser/ and not installed: the includes and small
// helpers shared by the files that implement browser's methods.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <chrono>
#include <cstdlib>
#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/page/author_styles.hpp>
#include <ctbrowser/page/pipeline.hpp>
#include <ctbrowser/script/program_image.hpp>
#include <ctbrowser/shell/browser.hpp>
#include <ctbrowser/shell/net/url.hpp>

namespace ctbrowser::shell {

/// See ctbrowser/page/pipeline.hpp.
using page::viewport_less_scroll_root;

} // namespace ctbrowser::shell
