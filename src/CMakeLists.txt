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

# --- ctbrowser.script ----------------------------------------------------
# ctjs v2. Reuses ctjs's Pratt parser (vparse.hpp) and replaces everything
# after it; the tree-walk interpreter and the refcounted variant value model
# are gone. Depends on core only for the warning set - the VM shares no state
# with the DOM by design (one agent per thread).
add_library(ctbrowser-script)
target_sources(ctbrowser-script
  PUBLIC
    FILE_SET CXX_MODULES FILES
      script/script.cppm
      script/value.cppm
      script/bytecode.cppm
      script/compile.cppm
      script/vm.cppm)
target_compile_features(ctbrowser-script PUBLIC cxx_std_23)
target_compile_options(ctbrowser-script PRIVATE ${CTBROWSER_V2_WARNINGS})
target_include_directories(ctbrowser-script PRIVATE
  "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/include"
  "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/external/compile-time-containers/include")
target_link_libraries(ctbrowser-script PUBLIC Boost::headers)
set_target_properties(ctbrowser-script PROPERTIES CXX_SCAN_FOR_MODULES ON)

add_library(ctbrowser::script ALIAS ctbrowser-script)

# --- ctbrowser.style -----------------------------------------------------
# Reuses ctcss's parser and replaces its matching: v1's query() rescanned the
# whole sheet per property per element, this buckets rules by their rightmost
# simple selector and rejects descendant selectors with an ancestor filter.
# Depends on dom for read_txn - matching consumes a stable read view, never
# the live tree, which is what makes it parallelisable.
add_library(ctbrowser-style)
target_sources(ctbrowser-style
  PUBLIC
    FILE_SET CXX_MODULES FILES
      style/style.cppm
      style/computed.cppm
      style/selector.cppm
      style/engine.cppm)
target_compile_features(ctbrowser-style PUBLIC cxx_std_23)
target_compile_options(ctbrowser-style PRIVATE ${CTBROWSER_V2_WARNINGS})
target_include_directories(ctbrowser-style PRIVATE
  "${PROJECT_SOURCE_DIR}/external/compile-time-css/include")
target_link_libraries(ctbrowser-style PUBLIC ctbrowser-core ctbrowser-dom Boost::headers)
set_target_properties(ctbrowser-style PROPERTIES CXX_SCAN_FOR_MODULES ON)

add_library(ctbrowser::style ALIAS ctbrowser-style)

if(CTBROWSER_BUILD_TESTS)
  add_subdirectory(../tests-v2 "${CMAKE_CURRENT_BINARY_DIR}/tests-v2")
endif()
