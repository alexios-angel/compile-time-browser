# ctbrowser 2.0 - the concurrent, GPU-composited engine.
#
# v2 builds ALONGSIDE v1 until the staging plan retires v1, so this subtree is
# self-contained: it does not touch the v1 interface target, its PCH, or its
# tests. v1's 33 tests must keep passing throughout the rewrite.
#
# Modules need a newer CMake than the top level asks for, and they need the
# scanner on, so the requirement is raised here rather than repo-wide.
cmake_minimum_required(VERSION 3.28)

if(CMAKE_VERSION VERSION_LESS 3.28)
  message(FATAL_ERROR "ctbrowser v2 needs CMake >= 3.28 for C++ module support")
endif()

# CONFIG mode: FindBoost was removed in CMake 4 (policy CMP0167), and Boost has
# shipped its own package config for years. Header-only usage only - see the
# plan's dependency policy: compiled Boost would mean cross-compiling Boost
# itself for the llvm-mingw Windows presets.
find_package(Boost 1.80 REQUIRED CONFIG)

# v2 is NOT header-only and does NOT depend on std::embed. Keeping the warning
# set identical to v1's is deliberate - the rewrite should not quietly relax the
# bar - minus the v1-specific -Wno-overlength-strings, which existed only for
# page-as-NTTP raw strings that v2 no longer has.
set(CTBROWSER_V2_WARNINGS -O2 -pedantic -Wall -Wextra -Werror -Wconversion)

add_library(ctbrowser-core)
target_sources(ctbrowser-core
  PUBLIC
    FILE_SET CXX_MODULES FILES
      core/core.cppm
      core/handle.cppm
      core/epoch.cppm
      core/slab.cppm
      core/atom.cppm
      core/scheduler.cppm
      core/geometry.cppm)
target_compile_features(ctbrowser-core PUBLIC cxx_std_23)
target_compile_options(ctbrowser-core PRIVATE ${CTBROWSER_V2_WARNINGS})
target_link_libraries(ctbrowser-core PUBLIC Boost::headers)
set_target_properties(ctbrowser-core PROPERTIES CXX_SCAN_FOR_MODULES ON)

find_package(Threads REQUIRED)
target_link_libraries(ctbrowser-core PUBLIC Threads::Threads)

add_library(ctbrowser::core ALIAS ctbrowser-core)

# --- ctbrowser.dom -------------------------------------------------------
# Still consumes cthtml's value parser (stage 1 wraps it rather than replacing
# it - see src/dom/html.cppm), so it needs that include path. That dependency
# is expected to disappear when the conformant parser lands.
add_library(ctbrowser-dom)
target_sources(ctbrowser-dom
  PUBLIC
    FILE_SET CXX_MODULES FILES
      dom/dom.cppm
      dom/node.cppm
      dom/document.cppm
      dom/html.cppm)
target_compile_features(ctbrowser-dom PUBLIC cxx_std_23)
target_compile_options(ctbrowser-dom PRIVATE ${CTBROWSER_V2_WARNINGS})
target_include_directories(ctbrowser-dom PRIVATE
  "${PROJECT_SOURCE_DIR}/external/compile-time-html/include")
target_link_libraries(ctbrowser-dom PUBLIC ctbrowser-core)
set_target_properties(ctbrowser-dom PROPERTIES CXX_SCAN_FOR_MODULES ON)

add_library(ctbrowser::dom ALIAS ctbrowser-dom)

if(CTBROWSER_BUILD_TESTS)
  add_subdirectory(../tests-v2 "${CMAKE_CURRENT_BINARY_DIR}/tests-v2")
endif()
