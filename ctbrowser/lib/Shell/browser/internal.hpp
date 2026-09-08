#pragma once
// Private to lib/Shell/browser/. NOT installed and in no file set:
// include/ctbrowser/shell/browser.hpp declares the class whole, and this
// exists only so its method bodies can be more than one file - they were
// 2,871 lines in one until 2026-09-08. The includes are browser.cpp's, so
// every file here sees exactly what that one saw.
//
// The browser's method bodies.
//
// browser.hpp was 2,356 lines because the class was defined with every body
// inline, so reading "what can a browser do" meant scrolling past how each
// answer works - and every translation unit that included it parsed the lot.
// The header is the list now; this is the how.

#include <chrono>
#include <cstdlib>
#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/script/program_image.hpp>
#include <ctbrowser/shell/browser.hpp>
