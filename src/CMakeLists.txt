# ctbrowser 2.0 - the concurrent, GPU-composited engine.
#
# the engine builds ALONGSIDE the previous engine until the staging plan retires the previous engine, so this subtree is
# self-contained: it does not touch the the previous engine interface target, its PCH, or its
# tests. the previous engine's 33 tests must keep passing throughout the rewrite.
#
include(GNUInstallDirs)

# CONFIG mode: FindBoost was removed in CMake 4 (policy CMP0167), and Boost has
# shipped its own package config for years. Header-only usage only - see the
# plan's dependency policy: compiled Boost would mean cross-compiling Boost
# itself for the llvm-mingw Windows presets.
#
# CROSS BUILDS have no BoostConfig for the target and do not need one: what the engine
# links is Boost::headers, so an isolated include directory (the toolchain finds
# it, the same way it finds GLM's) is the entire dependency.
if(CTBROWSER_BOOST_INCLUDE_DIR)
  if(NOT TARGET Boost::headers)
    add_library(Boost::headers INTERFACE IMPORTED)
    set_target_properties(Boost::headers PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${CTBROWSER_BOOST_INCLUDE_DIR}")
  endif()
  message(STATUS "ctbrowser: header-only Boost from ${CTBROWSER_BOOST_INCLUDE_DIR}")
else()
  find_package(Boost 1.80 REQUIRED CONFIG)
endif()

# the engine is NOT header-only and does NOT depend on std::embed. Keeping the warning
# set identical to the previous engine's is deliberate - the rewrite should not quietly relax the
# bar - minus the specific to the previous engine -Wno-overlength-strings, which existed only for
# page-as-NTTP raw strings that the engine no longer has.
set(CTBROWSER_STRICT_WARNINGS -O2 -pedantic -Wall -Wextra -Werror -Wconversion)

# --- LINK WITH LLD ------------------------------------------------------
# Measured on this tree: the same executable links in 0.65 s with lld and
# 4.31 s with the default ld, and the engine produces TWENTY-SIX of them. That is the
# single cheapest thing in this file. Checked rather than assumed, because a
# toolchain without lld must still build.
include(CheckLinkerFlag OPTIONAL RESULT_VARIABLE CTBROWSER_HAVE_LINKER_CHECK)
option(CTBROWSER_USE_LLD "Link with lld when it is available" ON)
set(CTBROWSER_LINK_OPTIONS "")
if(CTBROWSER_USE_LLD AND CTBROWSER_HAVE_LINKER_CHECK)
  check_linker_flag(CXX "-fuse-ld=lld" CTBROWSER_LLD_WORKS)
  if(CTBROWSER_LLD_WORKS)
    set(CTBROWSER_LINK_OPTIONS -fuse-ld=lld)
    message(STATUS "ctbrowser: linking with lld")
  endif()
endif()

# --- WHOLE-PROGRAM INLINING, IF YOU WANT IT BACK ------------------------
# Moving the engine's code out of the module interfaces is what made consumer
# builds fast, and the thing it gives up is the compiler's freedom to inline
# ACROSS the library boundary - which is what an all-inline (or header-only)
# engine gave for free, at the cost of recompiling it in every translation unit.
#
# LTO buys that back at link time instead of at compile time, which is the same
# trade made in the right place. Off by default because it makes linking slow
# again, and the default here is meant to be fast to build.
option(CTBROWSER_LTO "Inline across the engine boundary again, at link time" OFF)

# Everything the engine builds - libraries, tests and examples - goes through this, so
# there is one place that decides how the engine is compiled and linked.
function(ctbrowser_target name)
  target_compile_options(${name} PRIVATE ${CTBROWSER_STRICT_WARNINGS})
  if(CTBROWSER_LINK_OPTIONS)
    target_link_options(${name} PRIVATE ${CTBROWSER_LINK_OPTIONS})
  endif()
  if(CTBROWSER_LTO)
    set_property(TARGET ${name} PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
  endif()
endfunction()

# Headers live in include/ctbrowser/<subsystem>/ and implementations in
# src/<subsystem>/. A header declares, its .cpp defines, and the few that are
# templates (slab, handle) or constexpr value types (geometry) stay whole in
# the header because there is nothing to move out of them.
set(CTBROWSER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")

add_library(ctbrowser-core)
target_sources(ctbrowser-core
  PRIVATE
    core/atom.cpp
    core/epoch.cpp
    core/scheduler.cpp
    core/cpu_time.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/core.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/containers.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/handle.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/epoch.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/slab.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/atom.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/scheduler.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/cpu_time.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/geometry.hpp")
target_compile_features(ctbrowser-core PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-core)
target_link_libraries(ctbrowser-core PUBLIC Boost::headers)

find_package(Threads REQUIRED)
target_link_libraries(ctbrowser-core PUBLIC Threads::Threads)

add_library(ctbrowser::core ALIAS ctbrowser-core)

# --- ctbrowser.dom -------------------------------------------------------
# Its own WHATWG tokenizer and tree builder. This wrapped cthtml's value parser
# at first; the conformant one replaced it, and with it the last reason the DOM
# needed a parser from outside.
add_library(ctbrowser-dom)
target_sources(ctbrowser-dom
  PRIVATE
    dom/document.cpp
    dom/tokenizer.cpp
    dom/treebuilder.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/dom/dom.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/dom/node.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/dom/document.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/dom/tokenizer.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/dom/treebuilder.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/dom/html.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/dom/entities.hpp")
target_compile_features(ctbrowser-dom PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-dom)
target_link_libraries(ctbrowser-dom PUBLIC ctbrowser-core)

add_library(ctbrowser::dom ALIAS ctbrowser-dom)

# --- ctbrowser.script ----------------------------------------------------
# The script engine. Reuses ctjs's Pratt parser (vparse.hpp) and replaces everything
# after it; the tree-walk interpreter and the refcounted variant value model
# are gone. Depends on core only for the warning set - the VM shares no state
# with the DOM by design (one agent per thread).
add_library(ctbrowser-script)
target_sources(ctbrowser-script
  PRIVATE
    # Where the code lives: compiled ONCE into the library rather than in every
    # translation unit that uses it. compile.cpp holds the whole compiler -
    # only compiler::compile() is declared in the header.
    script/compile.cpp
    script/builtins.cpp
    script/vm.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/script.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/value.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/bytecode.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/compile.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/vm.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/builtins.hpp")
target_compile_features(ctbrowser-script PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-script)
target_include_directories(ctbrowser-script PRIVATE
  "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/include"
  "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/external/compile-time-containers/include")
target_link_libraries(ctbrowser-script PUBLIC ctbrowser-core Boost::headers)

add_library(ctbrowser::script ALIAS ctbrowser-script)

# --- ctbrowser.style -----------------------------------------------------
# Reuses ctcss's parser and replaces its matching: the previous engine's query() rescanned the
# whole sheet per property per element, this buckets rules by their rightmost
# simple selector and rejects descendant selectors with an ancestor filter.
# Depends on dom for read_txn - matching consumes a stable read view, never
# the live tree, which is what makes it parallelisable.
add_library(ctbrowser-style)
target_sources(ctbrowser-style
  PRIVATE
    style/engine.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/style.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/computed.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/selector.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/engine.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/ua.hpp")
target_compile_features(ctbrowser-style PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-style)
# ctcss's headers are part of style's own headers, so a consumer needs the path.
# BUILD_INTERFACE only: an exported target may not name a source-tree path.
target_include_directories(ctbrowser-style PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/external/compile-time-css/include>")
target_link_libraries(ctbrowser-style PUBLIC ctbrowser-core ctbrowser-dom Boost::headers)

add_library(ctbrowser::style ALIAS ctbrowser-style)

# --- ctbrowser.layout ----------------------------------------------------
# Consumes a styled box tree and produces an immutable fragment tree. Depends
# on core (scheduler, geometry), dom (read_txn) and style (computed styles) -
# but NOT the other way round: nothing upstream knows layout exists, which is
# what keeps the pipeline acyclic.
add_library(ctbrowser-layout)
target_sources(ctbrowser-layout
  PRIVATE
    layout/box.cpp
    layout/algorithm.cpp
    layout/engine.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/layout/layout.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/layout/values.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/layout/box.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/layout/fragment.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/layout/algorithm.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/layout/engine.hpp")
target_compile_features(ctbrowser-layout PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-layout)
target_link_libraries(ctbrowser-layout PUBLIC ctbrowser-core ctbrowser-dom ctbrowser-style)

add_library(ctbrowser::layout ALIAS ctbrowser-layout)

# --- ctbrowser.paint -----------------------------------------------------
# Records a display list from the fragment tree. Nothing downstream of layout
# knows paint exists; paint knows nothing about how the list is drawn, which
# is what allows a software and a GPU backend to consume the same recording.
add_library(ctbrowser-paint)
target_sources(ctbrowser-paint
  PRIVATE
    paint/record.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/paint/paint.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/paint/values.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/paint/command.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/paint/layer.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/paint/record.hpp")
target_compile_features(ctbrowser-paint PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-paint)
target_link_libraries(ctbrowser-paint PUBLIC ctbrowser-core ctbrowser-dom ctbrowser-style ctbrowser-layout)

add_library(ctbrowser::paint ALIAS ctbrowser-paint)

# --- ctbrowser.raster ----------------------------------------------------
# Turns display lists into pixels. Knows nothing about the DOM, style or
# layout - only about paint's recorded commands, which is what lets a second
# backend (SDL3 GPU, stage 6) drop in beside the software one.
add_library(ctbrowser-raster)
target_sources(ctbrowser-raster
  PRIVATE
    raster/draw.cpp
    raster/ttf.cpp
    raster/svg.cpp
    raster/software.cpp
    raster/renderer.cpp
    raster/glsl.cpp
    raster/glsl_preprocess.cpp
    raster/glsl_eval.cpp
    raster/softgl.cpp
    raster/spirv.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/raster.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/surface.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/draw.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/ttf.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/svg.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/font8x8.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/tile.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/backend.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/software.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/renderer.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/compositor.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/pipeline.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/glsl.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/softgl.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/spirv.hpp")
target_compile_features(ctbrowser-raster PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-raster)
target_link_libraries(ctbrowser-raster PUBLIC ctbrowser-core ctbrowser-paint)
# Real fonts, through SDL3_ttf. THE ONE PLACE THE ENGINE LINKS SDL, and a
# deliberate exception rather than an oversight - see the header of
# raster/ttf.cppm and the note in tests/api_surface. TTF_Init needs no video
# subsystem, so real text stays testable with no display, which is what makes
# the exception safe.
#
# OPTIONAL, like everything else here: without it the raster layer still builds
# and still draws font8x8, which keeps the goldens reproducible on a machine
# with no font libraries at all.
if(SDL3_FOUND AND SDL3_ttf_FOUND)
  ctbrowser_pick_sdl_target(ttf_target ttf_static SDL3_ttf::SDL3_ttf)
  ctbrowser_pick_sdl_target(sdl_target sdl_static SDL3::SDL3)
  target_link_libraries(ctbrowser-raster PUBLIC ${ttf_target} ${sdl_target})
  target_compile_definitions(ctbrowser-raster PUBLIC CTBROWSER_WITH_TTF=1)
  message(STATUS "ctbrowser: SDL3_ttf - real fonts available")
else()
  target_compile_definitions(ctbrowser-raster PUBLIC CTBROWSER_WITH_TTF=0)
  message(STATUS "ctbrowser: no SDL3_ttf - text renders with font8x8 only")
endif()

# SVG, through plutosvg. PRIVATE link and PUBLIC definition, deliberately split:
# only raster/svg.cpp includes a plutosvg header, so no consumer needs the
# include path, but raster/svg.hpp's svg_available() is constexpr and every
# caller has to agree with it about what this build can do.
if(plutosvg_FOUND)
  target_link_libraries(ctbrowser-raster PRIVATE plutosvg::plutosvg)
  target_compile_definitions(ctbrowser-raster PUBLIC CTBROWSER_WITH_SVG=1)
  message(STATUS "ctbrowser: plutosvg ${plutosvg_VERSION} - SVG renders")

  # VERSION PARITY IS LOAD-BEARING, so it is checked rather than hoped for.
  # tests/golden/svg.ppm is ONE image compared byte-for-byte on both Linux and
  # Windows, and the Windows sysroot pins whatever ../llvm-mingw/build-sdl3.sh
  # fetched. If a host's plutosvg drifts ahead of that, the golden moves on one
  # platform and not the other - which is exactly how the renders diverged over
  # a HarfBuzz version once already (docs/raster.md).
  set(CTBROWSER_PLUTOSVG_PINNED 0.0.8)
  if(NOT plutosvg_VERSION VERSION_EQUAL CTBROWSER_PLUTOSVG_PINNED)
    message(WARNING
      "ctbrowser: plutosvg ${plutosvg_VERSION}, expected ${CTBROWSER_PLUTOSVG_PINNED}. "
      "The SVG golden may not compare across platforms; regenerate with REGOLDEN=1 "
      "and check the Windows render still matches.")
  endif()
else()
  target_compile_definitions(ctbrowser-raster PUBLIC CTBROWSER_WITH_SVG=0)
  message(STATUS "ctbrowser: no plutosvg - <svg> and .svg images do not draw")
endif()

add_library(ctbrowser::raster ALIAS ctbrowser-raster)

# --- ctbrowser.shell -----------------------------------------------------
# The engine assembled. SDL-free on purpose: a browser you can drive without a
# display is a browser you can test.
add_library(ctbrowser-shell)
target_sources(ctbrowser-shell
  PRIVATE
    shell/webgl.cpp
    shell/webgl_bindings.cpp
    shell/browser.cpp
    shell/bindings.cpp
    shell/canvas.cpp
    shell/svg.cpp
    shell/forms.cpp
    shell/net.cpp
    shell/embedded_fonts.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/embedded_fonts.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/shell.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/input.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/metrics.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/assets.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/images.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/net.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/canvas.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/webgl.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/svg.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/forms.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/bindings.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/browser.hpp")
target_compile_features(ctbrowser-shell PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-shell)

# --- the fonts, in the binary --------------------------------------------
#
# `#embed` is C23 and C++26. Clang has had it since 19 and the llvm-mingw cross
# toolchain has it; GCC gained it in 15, so GCC 13 - which builds this tree
# perfectly well otherwise - does not. Hence a PROBE rather than a version
# check: compile the directive and see.
#
# --embed-dir rather than -I. `#embed` has its own search path and the quoted
# form looks beside the source file, so the angle-bracket form plus this flag is
# the only combination that finds a file in another directory.
#
# OFF by default and deliberately: the twelve faces are 5.2 MB, and they would
# be linked into every test binary as well as every example - about 160 MB of
# build output for something only a DISTRIBUTABLE build needs. Turn it on for
# one (`-DCTBROWSER_EMBED_FONTS=ON`) and the exe runs anywhere with no fonts/
# beside it; leave it off and it reads the directory, as it always has.
option(CTBROWSER_EMBED_FONTS "Bake the vendored OFL faces into the binary" OFF)
if(CTBROWSER_EMBED_FONTS)
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_FLAGS "--embed-dir=${PROJECT_SOURCE_DIR}/fonts")
  check_cxx_source_compiles("
    #ifndef __has_embed
    #error no __has_embed
    #endif
    #if __has_embed(<Tinos-Regular.ttf>) != 1
    #error not on the embed path
    #endif
    int main() {}
  " CTBROWSER_COMPILER_HAS_EMBED)
  unset(CMAKE_REQUIRED_FLAGS)
  if(CTBROWSER_COMPILER_HAS_EMBED)
    set_source_files_properties(shell/embedded_fonts.cpp PROPERTIES COMPILE_OPTIONS
      "--embed-dir=${PROJECT_SOURCE_DIR}/fonts")
    message(STATUS "ctbrowser: fonts embedded in the binary (+5.2 MB)")
  else()
    message(STATUS "ctbrowser: no #embed here; fonts load from fonts/ at runtime")
  endif()
endif()
target_link_libraries(ctbrowser-shell PUBLIC ctbrowser-core ctbrowser-dom ctbrowser-style
                                             ctbrowser-layout ctbrowser-paint ctbrowser-raster
                                             ctbrowser-script Boost::headers)

# Asio comes from Boost by default - Boost::headers is already a dependency of
# three the engine targets and is header-only. A build that would rather not have Boost
# at all can point at STANDALONE Asio instead; the module supports both.
find_path(CTBROWSER_STANDALONE_ASIO_INCLUDE asio.hpp)
option(CTBROWSER_ASIO_STANDALONE "Use standalone Asio instead of Boost.Asio"
       ${CTBROWSER_PREFER_STANDALONE_ASIO})
if(CTBROWSER_ASIO_STANDALONE AND CTBROWSER_STANDALONE_ASIO_INCLUDE)
  target_include_directories(ctbrowser-shell PUBLIC ${CTBROWSER_STANDALONE_ASIO_INCLUDE})
  target_compile_definitions(ctbrowser-shell PUBLIC CTBROWSER_ASIO_STANDALONE=1 ASIO_STANDALONE=1)
  message(STATUS "ctbrowser: networking on standalone Asio")
else()
  target_compile_definitions(ctbrowser-shell PUBLIC CTBROWSER_ASIO_STANDALONE=0)
endif()

# https:// needs TLS, and TLS needs OpenSSL - which is NOT Boost, so it does not
# touch the header-only-Boost rule. It IS optional: without it the build still
# does http:// and rejects https:// by name.
# Asio's Windows backend is winsock; nothing links it implicitly.
if(WIN32)
  target_link_libraries(ctbrowser-shell PUBLIC ws2_32 mswsock)
endif()

find_package(OpenSSL QUIET COMPONENTS SSL Crypto)
if(OpenSSL_FOUND)
  target_link_libraries(ctbrowser-shell PUBLIC OpenSSL::SSL OpenSSL::Crypto)
  target_compile_definitions(ctbrowser-shell PUBLIC CTBROWSER_WITH_TLS=1)
  message(STATUS "ctbrowser: https:// enabled (OpenSSL ${OPENSSL_VERSION})")
else()
  target_compile_definitions(ctbrowser-shell PUBLIC CTBROWSER_WITH_TLS=0)
  message(STATUS "ctbrowser: no OpenSSL - fetch() does http:// only")
endif()

add_library(ctbrowser::shell ALIAS ctbrowser-shell)

# --- ctbrowser.app -------------------------------------------------------
# The window and the event loop, and the only module that knows SDL exists.
# ALWAYS built. SDL3 is a compile definition, not a gate: without it the module
# still provides run_app() over the headless backend, which is what makes "SDL3
# is optional" true of the BUILD and not just of the engine.
add_library(ctbrowser-app)
target_sources(ctbrowser-app
  PRIVATE
    shell/app.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/app.hpp")
target_compile_features(ctbrowser-app PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-app)
target_link_libraries(ctbrowser-app PUBLIC ctbrowser-core ctbrowser-paint ctbrowser-raster
                                           ctbrowser-shell)
if(SDL3_FOUND)
  target_compile_definitions(ctbrowser-app PRIVATE CTBROWSER_WITH_SDL3=1)
  target_link_libraries(ctbrowser-app PUBLIC ${CTBROWSER_SDL3_TARGET})
  # PNG/JPEG/WebP, if the satellite library is there. BMP is decoded by the
  # engine itself, so a build without this still shows images.
  find_package(SDL3_image QUIET)
  if(SDL3_image_FOUND)
    target_compile_definitions(ctbrowser-app PRIVATE CTBROWSER_WITH_IMAGE=1)
    ctbrowser_pick_sdl_target(image_target image_static SDL3_image::SDL3_image)
    target_link_libraries(ctbrowser-app PUBLIC ${image_target})
    message(STATUS "ctbrowser: SDL3_image found - <img> reads PNG/JPEG too")
  else()
    target_compile_definitions(ctbrowser-app PRIVATE CTBROWSER_WITH_IMAGE=0)
    message(STATUS "ctbrowser: no SDL3_image - <img> reads BMP only")
  endif()
else()
  target_compile_definitions(ctbrowser-app PRIVATE CTBROWSER_WITH_SDL3=0 CTBROWSER_WITH_IMAGE=0)
  message(STATUS "ctbrowser: SDL3 not found - builds headless (run_app still works)")
endif()
add_library(ctbrowser::app ALIAS ctbrowser-app)

# --- ctbrowser (umbrella) ------------------------------------------------
# What an application imports and links. One of each.
add_library(ctbrowser INTERFACE)
target_sources(ctbrowser
  INTERFACE
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser.hpp")
target_compile_features(ctbrowser INTERFACE cxx_std_23)
target_link_libraries(ctbrowser INTERFACE
  ctbrowser-core ctbrowser-dom ctbrowser-script ctbrowser-style ctbrowser-layout
  ctbrowser-paint ctbrowser-raster ctbrowser-shell ctbrowser-app)
add_library(ctbrowser::ctbrowser ALIAS ctbrowser)

# --- ONE ARCHIVE ---------------------------------------------------------
# The engine is nine libraries because it is nine modules, and every one of
# them ends up on every link line. `libctbrowser-all.a` is all of them merged,
# so an application - or a build system that is not CMake - links ONE file.
#
# `-all` rather than `libctbrowser.a`, which is the umbrella TARGET's own
# output: two rules producing one path is a build that refuses to generate.
#
# It is a merge, not a rebuild: the same objects, collected with the archiver.
# CMake users need nothing new; `ctbrowser::ctbrowser` still works and still carries
# the include paths and module BMIs, which an archive cannot.
#
# CTBROWSER_SINGLE_LIB=OFF skips it.
option(CTBROWSER_SINGLE_LIB "Also produce one merged libctbrowser-all.a" ON)
if(CTBROWSER_SINGLE_LIB)
  set(ctbrowser-merged "${CMAKE_CURRENT_BINARY_DIR}/libctbrowser-all.a")
  set(ctbrowser-parts ctbrowser-core ctbrowser-dom ctbrowser-script ctbrowser-style
                      ctbrowser-layout ctbrowser-paint ctbrowser-raster ctbrowser-shell
                      ctbrowser-app ctbrowser)
  # A header-only subsystem is an INTERFACE library: it has no archive to merge
  # because it contributes no objects, so asking for its TARGET_FILE is an
  # error rather than an omission.
  set(ctbrowser-part-files "")
  foreach(part IN LISTS ctbrowser-parts)
    get_target_property(part_type ${part} TYPE)
    if(NOT part_type STREQUAL "INTERFACE_LIBRARY")
      list(APPEND ctbrowser-part-files "$<TARGET_FILE:${part}>")
    endif()
  endforeach()
  # The MRI script is how llvm-ar and GNU ar both merge archives; writing it at
  # build time keeps the generator expressions resolvable.
  set(ctbrowser-mri "${CMAKE_CURRENT_BINARY_DIR}/ctbrowser-merge.mri")
  file(GENERATE OUTPUT "${ctbrowser-mri}" CONTENT
       "create ${ctbrowser-merged}\n$<JOIN:$<LIST:TRANSFORM,${ctbrowser-part-files},PREPEND,addlib >,\n>\nsave\nend\n")
  add_custom_command(
    OUTPUT "${ctbrowser-merged}"
    COMMAND "${CMAKE_AR}" -M < "${ctbrowser-mri}"
    DEPENDS ${ctbrowser-parts} "${ctbrowser-mri}"
    COMMENT "merging the engine into one libctbrowser-all.a"
    VERBATIM)
  add_custom_target(ctbrowser-single-lib ALL DEPENDS "${ctbrowser-merged}")
endif()

# --- ctbrowser.gpu -------------------------------------------------------
# OPTIONAL by construction. Without SDL3 the engine still builds and still
# renders - through the software backend - because raster::renderer is a
# runtime seam rather than a compile-time one. That is the same reason a
# machine with no GPU still gets a browser.
if(SDL3_FOUND)
  add_library(ctbrowser-gpu)
  target_sources(ctbrowser-gpu
    PRIVATE
      gpu/device.cpp
    PUBLIC
      FILE_SET HEADERS
        BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
        FILES
          "${CTBROWSER_INCLUDE_DIR}/ctbrowser/gpu/gpu.hpp"
          "${CTBROWSER_INCLUDE_DIR}/ctbrowser/gpu/shaders/tile_spv.hpp"
          "${CTBROWSER_INCLUDE_DIR}/ctbrowser/gpu/device.hpp"
          "${CTBROWSER_INCLUDE_DIR}/ctbrowser/gpu/select.hpp")
  target_compile_features(ctbrowser-gpu PUBLIC cxx_std_23)
  ctbrowser_target(ctbrowser-gpu)
  target_link_libraries(ctbrowser-gpu PUBLIC ctbrowser-core ctbrowser-paint ctbrowser-raster
                                             ${CTBROWSER_SDL3_TARGET})
  add_library(ctbrowser::gpu ALIAS ctbrowser-gpu)
endif()

# --- installing the engine -------------------------------------------------------
# Headers install to include/, archives to lib/. This was a great deal more
# involved when the engine was modules: a CXX_MODULES file set, a
# CXX_MODULES_DIRECTORY on the export, and a consumer that had to be able to
# rebuild BMIs from the installed sources.
set(CTBROWSER_TARGETS
  ctbrowser-core ctbrowser-dom ctbrowser-script ctbrowser-style ctbrowser-layout
  ctbrowser-paint ctbrowser-raster ctbrowser-shell ctbrowser-app ctbrowser)
if(TARGET ctbrowser-gpu)
  list(APPEND CTBROWSER_TARGETS ctbrowser-gpu)
endif()

install(TARGETS ${CTBROWSER_TARGETS}
  EXPORT ctbrowser-targets
  FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")

install(EXPORT ctbrowser-targets
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ctbrowser"
  NAMESPACE ctbrowser::)

# The alias an application links. `ctbrowser::ctbrowser` is the build-tree name;
# this is what survives an install.
# Whatever the exported targets name in their link interface, the consumer has
# to be able to resolve. SDL3 is conditional because the app module links it
# only when it was found - a headless install must not demand it back.
set(CTBROWSER_CONFIG_DEPS "find_dependency(Boost 1.80 CONFIG)\nfind_dependency(Threads)")
if(SDL3_FOUND)
  string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(SDL3 CONFIG)")
endif()
# EVERY package an exported target links has to be findable by the consumer,
# or find_package(ctbrowser) fails on a target it cannot resolve. Adding an
# optional dependency to a library and forgetting this line is exactly the kind
# of break tools/check-package.sh exists to catch - it caught these two.
if(Freetype_FOUND)
  string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(Freetype)")
endif()
if(OpenSSL_FOUND)
  string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(OpenSSL COMPONENTS SSL Crypto)")
endif()
if(SDL3_image_FOUND)
  string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(SDL3_image CONFIG)")
endif()
if(SDL3_ttf_FOUND)
  string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(SDL3_ttf CONFIG)")
endif()
# PRIVATE-linked, so it is not in the exported link interface and a consumer
# never resolves it - but a STATIC ctbrowser-raster carries the dependency into
# the consumer's link line anyway, and check-package.sh is what proves whether
# that matters here rather than reasoning about it.
if(plutosvg_FOUND)
  string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(plutosvg CONFIG)")
endif()
file(WRITE "${PROJECT_BINARY_DIR}/ctbrowser-config.cmake"
"include(CMakeFindDependencyMacro)
${CTBROWSER_CONFIG_DEPS}
include(\"\${CMAKE_CURRENT_LIST_DIR}/ctbrowser-targets.cmake\")
")
install(FILES "${PROJECT_BINARY_DIR}/ctbrowser-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ctbrowser")

add_subdirectory(../examples "${CMAKE_CURRENT_BINARY_DIR}/examples")

if(CTBROWSER_BUILD_TESTS)
  add_subdirectory(../tests "${CMAKE_CURRENT_BINARY_DIR}/tests")
endif()
