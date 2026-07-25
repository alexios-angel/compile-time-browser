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
      style/engine.cppm
      style/ua.cppm)
target_compile_features(ctbrowser-style PUBLIC cxx_std_23)
target_compile_options(ctbrowser-style PRIVATE ${CTBROWSER_V2_WARNINGS})
target_include_directories(ctbrowser-style PRIVATE
  "${PROJECT_SOURCE_DIR}/external/compile-time-css/include")
target_link_libraries(ctbrowser-style PUBLIC ctbrowser-core ctbrowser-dom Boost::headers)
set_target_properties(ctbrowser-style PROPERTIES CXX_SCAN_FOR_MODULES ON)

add_library(ctbrowser::style ALIAS ctbrowser-style)

# --- ctbrowser.layout ----------------------------------------------------
# Consumes a styled box tree and produces an immutable fragment tree. Depends
# on core (scheduler, geometry), dom (read_txn) and style (computed styles) -
# but NOT the other way round: nothing upstream knows layout exists, which is
# what keeps the pipeline acyclic.
add_library(ctbrowser-layout)
target_sources(ctbrowser-layout
  PUBLIC
    FILE_SET CXX_MODULES FILES
      layout/layout.cppm
      layout/values.cppm
      layout/box.cppm
      layout/fragment.cppm
      layout/algorithm.cppm
      layout/engine.cppm)
target_compile_features(ctbrowser-layout PUBLIC cxx_std_23)
target_compile_options(ctbrowser-layout PRIVATE ${CTBROWSER_V2_WARNINGS})
target_link_libraries(ctbrowser-layout PUBLIC ctbrowser-core ctbrowser-dom ctbrowser-style)
set_target_properties(ctbrowser-layout PROPERTIES CXX_SCAN_FOR_MODULES ON)

add_library(ctbrowser::layout ALIAS ctbrowser-layout)

# --- ctbrowser.paint -----------------------------------------------------
# Records a display list from the fragment tree. Nothing downstream of layout
# knows paint exists; paint knows nothing about how the list is drawn, which
# is what allows a software and a GPU backend to consume the same recording.
add_library(ctbrowser-paint)
target_sources(ctbrowser-paint
  PUBLIC
    FILE_SET CXX_MODULES FILES
      paint/paint.cppm
      paint/values.cppm
      paint/command.cppm
      paint/layer.cppm
      paint/record.cppm)
target_compile_features(ctbrowser-paint PUBLIC cxx_std_23)
target_compile_options(ctbrowser-paint PRIVATE ${CTBROWSER_V2_WARNINGS})
target_link_libraries(ctbrowser-paint PUBLIC ctbrowser-core ctbrowser-dom ctbrowser-style ctbrowser-layout)
set_target_properties(ctbrowser-paint PROPERTIES CXX_SCAN_FOR_MODULES ON)

add_library(ctbrowser::paint ALIAS ctbrowser-paint)

# --- ctbrowser.raster ----------------------------------------------------
# Turns display lists into pixels. Knows nothing about the DOM, style or
# layout - only about paint's recorded commands, which is what lets a second
# backend (SDL3 GPU, stage 6) drop in beside the software one.
add_library(ctbrowser-raster)
target_sources(ctbrowser-raster
  PUBLIC
    FILE_SET CXX_MODULES FILES
      raster/raster.cppm
      raster/surface.cppm
      raster/draw.cppm
      raster/tile.cppm
      raster/backend.cppm
      raster/software.cppm
      raster/renderer.cppm
      raster/compositor.cppm
      raster/pipeline.cppm)
target_compile_features(ctbrowser-raster PUBLIC cxx_std_23)
target_compile_options(ctbrowser-raster PRIVATE ${CTBROWSER_V2_WARNINGS})
target_include_directories(ctbrowser-raster PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/raster")
target_link_libraries(ctbrowser-raster PUBLIC ctbrowser-core ctbrowser-paint)
set_target_properties(ctbrowser-raster PROPERTIES CXX_SCAN_FOR_MODULES ON)

add_library(ctbrowser::raster ALIAS ctbrowser-raster)

# --- ctbrowser.shell -----------------------------------------------------
# The engine assembled. SDL-free on purpose: a browser you can drive without a
# display is a browser you can test.
add_library(ctbrowser-shell)
target_sources(ctbrowser-shell
  PUBLIC
    FILE_SET CXX_MODULES FILES
      shell/shell.cppm
      shell/input.cppm
      shell/browser.cppm)
target_compile_features(ctbrowser-shell PUBLIC cxx_std_23)
target_compile_options(ctbrowser-shell PRIVATE ${CTBROWSER_V2_WARNINGS})
target_link_libraries(ctbrowser-shell PUBLIC ctbrowser-core ctbrowser-dom ctbrowser-style
                                             ctbrowser-layout ctbrowser-paint ctbrowser-raster)
set_target_properties(ctbrowser-shell PROPERTIES CXX_SCAN_FOR_MODULES ON)

add_library(ctbrowser::shell ALIAS ctbrowser-shell)

# --- ctbrowser.app -------------------------------------------------------
# The window and the event loop, and the only module that knows SDL exists.
if(SDL3_FOUND)
  add_library(ctbrowser-app)
  target_sources(ctbrowser-app
    PUBLIC
      FILE_SET CXX_MODULES FILES shell/app.cppm)
  target_compile_features(ctbrowser-app PUBLIC cxx_std_23)
  target_compile_options(ctbrowser-app PRIVATE ${CTBROWSER_V2_WARNINGS})
  target_link_libraries(ctbrowser-app PUBLIC ctbrowser-core ctbrowser-raster ctbrowser-shell
                                             SDL3::SDL3)
  set_target_properties(ctbrowser-app PROPERTIES CXX_SCAN_FOR_MODULES ON)
  add_library(ctbrowser::app ALIAS ctbrowser-app)
endif()

# --- ctbrowser.gpu -------------------------------------------------------
# OPTIONAL by construction. Without SDL3 the engine still builds and still
# renders - through the software backend - because raster::renderer is a
# runtime seam rather than a compile-time one. That is the same reason a
# machine with no GPU still gets a browser.
if(SDL3_FOUND)
  add_library(ctbrowser-gpu)
  target_sources(ctbrowser-gpu
    PUBLIC
      FILE_SET CXX_MODULES FILES
        gpu/gpu.cppm
        gpu/device.cppm
        gpu/select.cppm)
  target_compile_features(ctbrowser-gpu PUBLIC cxx_std_23)
  target_compile_options(ctbrowser-gpu PRIVATE ${CTBROWSER_V2_WARNINGS})
  target_include_directories(ctbrowser-gpu PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/gpu")
  target_link_libraries(ctbrowser-gpu PUBLIC ctbrowser-core ctbrowser-paint ctbrowser-raster
                                             SDL3::SDL3)
  set_target_properties(ctbrowser-gpu PROPERTIES CXX_SCAN_FOR_MODULES ON)
  add_library(ctbrowser::gpu ALIAS ctbrowser-gpu)
endif()

add_subdirectory(../examples-v2 "${CMAKE_CURRENT_BINARY_DIR}/examples-v2")

if(CTBROWSER_BUILD_TESTS)
  add_subdirectory(../tests-v2 "${CMAKE_CURRENT_BINARY_DIR}/tests-v2")
endif()
