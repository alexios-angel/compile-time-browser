#pragma once
// Private to lib/Shell/bindings/window/. NOT installed and in no file set:
// include/ctbrowser/shell/bindings.hpp declares dom_bindings whole, and this
// exists only so its window half can be more than one file - it was 1,106
// lines in one until 2026-09-08. The includes are window.cpp's, so every
// file here sees exactly what that one saw. Nothing is declared here: the
// two files share no helper.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>
#include <numbers>
