# ctbrowser 2.0 - the concurrent, GPU-composited engine.
#
# the engine builds ALONGSIDE the previous engine until the staging plan retires the previous engine, so this subtree is
# self-contained: it does not touch the the previous engine interface target, its PCH, or its
# tests. the previous engine's 33 tests must keep passing throughout the rewrite.
#
include(GNUInstallDirs)

# CONFIG mode: FindBoost was removed in CMake 4 (policy CMP0167), and Boost has
# shipped its own package config for years.
#
# MOSTLY header-only, and the exception is deliberate. This was header-only ONLY
# until 2026-07-31, because a compiled Boost meant cross-compiling Boost for the
# llvm-mingw Windows presets. Boost.URL forced the question: it has been
# compiled-only since 1.87 - `boost/url/src.hpp` now hard-errors, "src.hpp is
# discontinued" - so there is no header-only way to have it. The rule is lifted
# for libraries worth the cost, and Boost.URL is the first. See NOTICE.
#
# STILL EXCLUDED: Boost.Context, Coroutine and Fiber. Those are what actually
# broke the cross build, and nothing here needs them.
#
# CROSS BUILDS have no BoostConfig for the target: the headers come from an
# isolated include directory (the toolchain finds it the way it finds GLM's) and
# the compiled libraries from the mingw sysroot, beside the static SDL3 that
# already lives there.
if(CTBROWSER_BOOST_INCLUDE_DIR)
  if(NOT TARGET Boost::headers)
    add_library(Boost::headers INTERFACE IMPORTED)
    set_target_properties(Boost::headers PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${CTBROWSER_BOOST_INCLUDE_DIR}")
  endif()
  find_library(CTBROWSER_BOOST_URL_LIBRARY NAMES boost_url libboost_url)
  if(NOT CTBROWSER_BOOST_URL_LIBRARY)
    message(FATAL_ERROR
      "ctbrowser: no libboost_url for this target. It is not header-only and cannot be "
      "(boost/url/src.hpp is discontinued upstream). Build it into the mingw sysroot - "
      "tools/mingw/build-boost-mingw.sh does exactly that - and configure again.")
  endif()
  if(NOT TARGET Boost::url)
    add_library(Boost::url UNKNOWN IMPORTED)
    set_target_properties(Boost::url PROPERTIES
      IMPORTED_LOCATION "${CTBROWSER_BOOST_URL_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${CTBROWSER_BOOST_INCLUDE_DIR}")
  endif()
  message(STATUS "ctbrowser: Boost headers ${CTBROWSER_BOOST_INCLUDE_DIR}, "
                 "url ${CTBROWSER_BOOST_URL_LIBRARY}")
else()
  find_package(Boost 1.80 REQUIRED CONFIG COMPONENTS url)
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
    core/algorithms.cpp
    core/allocator.cpp
    core/atom.cpp
    core/epoch.cpp
    core/scheduler.cpp
    core/cpu_time.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/allocator.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/core.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/containers.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/handle.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/epoch.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/slab.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/algorithms.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/atom.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/scheduler.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/cpu_time.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/core/geometry.hpp")
target_compile_features(ctbrowser-core PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-core)
target_link_libraries(ctbrowser-core PUBLIC Boost::headers)

find_package(Threads REQUIRED)
target_link_libraries(ctbrowser-core PUBLIC Threads::Threads)

# THE ALLOCATOR. mimalloc backs operator new/delete (core/allocator.cpp),
# because allocator traffic is ~4.8% of a Phaser frame and a drop-in allocator
# is a link-line change that cannot alter results - see docs/performance.md,
# which also records why jemalloc lost (a tie on wall clock, and no
# cross-build).
#
# REQUIRED once found, and PUBLIC on purpose: an executable that links the
# engine but not mimalloc would resolve operator new from libstdc++ instead and
# silently run on the system allocator. tests/core_basics asks
# ctbrowser::allocator_name() so that mistake is a test failure.
#
# The Windows sysroot gets it from tools/mingw/build-mimalloc-mingw.sh.
# BY PATH, NOT BY find_package. mimalloc's exported config advertises a
# `mimalloc-static` target on distributions that ship only the shared library,
# so taking the config's word for it fails at link time with a target that
# cannot be satisfied. Asking for the file answers what is actually installed,
# and the static one is preferred where both exist because that is what makes a
# Windows .exe self-contained.
# BREW IS HINTED EXPLICITLY. Nothing puts its prefix on CMake's default search
# path, so a box with brew's mimalloc and no apt one configures with "mimalloc
# not found" while the library sits in /home/linuxbrew/.linuxbrew/lib. The
# hints are checked FIRST so brew's copy wins over a distribution's - which
# matters here because apt ships v2 and this tree pins v3, and the two are
# different allocators behind the same header name.
find_library(CTBROWSER_MIMALLOC NAMES mimalloc-static mimalloc
  HINTS ${CTBROWSER_BREW_HINTS} PATH_SUFFIXES lib)
find_path(CTBROWSER_MIMALLOC_INCLUDE mimalloc.h
  HINTS ${CTBROWSER_BREW_HINTS} PATH_SUFFIXES include)
if(CTBROWSER_USE_MIMALLOC AND (NOT CTBROWSER_MIMALLOC OR NOT CTBROWSER_MIMALLOC_INCLUDE))
  message(FATAL_ERROR
    "mimalloc not found. Linux: brew install mimalloc. "
    "Windows cross: run tools/mingw/build-mimalloc-mingw.sh. "
    "Or configure with -DCTBROWSER_USE_MIMALLOC=OFF to use the system allocator.")
endif()
# simdutf. It backs base64_decode's fast path - 42x on well-formed input, with
# this file's own decoder still handling anything simdutf's strict mode refuses,
# so the leniency contract is unchanged. See docs/performance.md, which also
# records the differential test that ruled out the obvious drop-in.
find_library(CTBROWSER_SIMDUTF NAMES simdutf HINTS ${CTBROWSER_BREW_HINTS} PATH_SUFFIXES lib)
find_path(CTBROWSER_SIMDUTF_INCLUDE simdutf.h HINTS ${CTBROWSER_BREW_HINTS} PATH_SUFFIXES include)
if(NOT CTBROWSER_SIMDUTF OR NOT CTBROWSER_SIMDUTF_INCLUDE)
  message(FATAL_ERROR
    "simdutf not found. Linux: brew install simdutf. "
    "Windows cross: run tools/mingw/build-simdutf-mingw.sh")
endif()
target_link_libraries(ctbrowser-core PUBLIC "${CTBROWSER_SIMDUTF}")
target_include_directories(ctbrowser-core SYSTEM PUBLIC "${CTBROWSER_SIMDUTF_INCLUDE}")

if(CTBROWSER_USE_MIMALLOC)
  target_compile_definitions(ctbrowser-core PRIVATE CTBROWSER_WITH_MIMALLOC=1)
  target_link_libraries(ctbrowser-core PUBLIC "${CTBROWSER_MIMALLOC}")
endif()
# SYSTEM, and this is not cosmetic. That path is a whole brew prefix's include
# directory: adding it normally puts every header brew has installed - Boost's
# among them - on the strict-warning path, and the build stops on
# -Wunused-template inside boost/move/algo. Third-party headers are not this
# project's warnings to answer, which is the same rule ctbrowser_target already
# states about its own exported interface.
if(CTBROWSER_USE_MIMALLOC)
  target_include_directories(ctbrowser-core SYSTEM PUBLIC "${CTBROWSER_MIMALLOC_INCLUDE}")
endif()

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
    script/bigint.cpp
    script/number_format.cpp
    script/vm.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/script.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/value.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/bytecode.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/bigint.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/compile.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/number_format.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/vm.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/builtins.hpp"
        # builtins.cpp includes it. Absent from this list until 2026-08-09, so it
        # was compiled against and never installed.
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/regex.hpp")
target_compile_features(ctbrowser-script PUBLIC cxx_std_23)
# PRIVATE: the dispatch strategy is an implementation detail of run_loop and
# nothing a consumer compiles against can see it.
if(CTBROWSER_COMPUTED_GOTO)
  target_compile_definitions(ctbrowser-script PRIVATE CTBROWSER_COMPUTED_GOTO)
endif()

# PUBLIC, unlike the one above, and it MUST be: CTBROWSER_WITH_GMP selects the
# type of `ctbrowser::script::bigint` inside value.hpp, which is a PUBLIC
# header. A private definition would compile this library against mpz_int and
# every consumer against cpp_int - one ODR violation per translation unit, and
# the kind that links cleanly and then reads a bigint through the wrong layout.
#
# The root CMakeLists says why this is opt-in rather than on-when-found. GMP is
# LGPL and this engine links statically; see NOTICE.
if(CTBROWSER_WITH_GMP)
  find_path(CTBROWSER_GMP_INCLUDE_DIR gmp.h)
  find_library(CTBROWSER_GMP_LIBRARY NAMES gmp libgmp)
  if(NOT CTBROWSER_GMP_INCLUDE_DIR OR NOT CTBROWSER_GMP_LIBRARY)
    message(FATAL_ERROR
      "ctbrowser: CTBROWSER_WITH_GMP=ON but no GMP for this target. Install it "
      "(brew install gmp) or, for the Windows presets, run tools/mingw/build-gmp-mingw.sh "
      "to put it in the mingw sysroot. Or leave the option OFF and use cpp_int, "
      "which is the faster default for BigInt-sized numbers anyway.")
  endif()
  target_compile_definitions(ctbrowser-script PUBLIC CTBROWSER_WITH_GMP)
  target_include_directories(ctbrowser-script PUBLIC "${CTBROWSER_GMP_INCLUDE_DIR}")
  target_link_libraries(ctbrowser-script PUBLIC "${CTBROWSER_GMP_LIBRARY}")
  message(STATUS "ctbrowser: BigInt backend GMP ${CTBROWSER_GMP_LIBRARY} (LGPL - see NOTICE)")
endif()

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
# `style/engine.hpp` includes <ctcss.hpp> for one private member's parameter
# type (`ctcss::value_sheet::selector`), and that is enough: every consumer of
# the PUBLIC header parses it.
#
# TWO INTERFACES, and it took an install to notice the second was missing. An
# exported target may not name a source-tree path, so the build tree gets
# BUILD_INTERFACE - and until 2026-08-09 that was the ONLY one, which made the
# comment above true of the build tree and false of an install. The symptom was
# not subtle: `find_package(ctbrowser)` succeeded and then the first
# `#include <ctbrowser.hpp>` died on "ctcss.hpp: No such file or directory".
# The package had never been compilable, and nothing said so because
# everything else in this tree consumes the engine through add_subdirectory.
target_include_directories(ctbrowser-style PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/external/compile-time-css/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

# ...and the headers themselves have to travel, or the path above points at
# nothing. ctcss is Apache-2.0 with LLVM Exceptions - the same licence as this
# repository, which began as a clone of it - so its licence rides along with
# it, and NOTICE records that this package SHIPS those headers rather than
# merely building against them.
#
# THE SKEW THIS CREATES IS REAL AND IS THE PRICE: an installed prefix now holds
# a COPY of ctcss's headers, so a consumer that also has its own ctcss can end
# up with two. `include/` rather than `include/.` on purpose - the trailing
# slash copies the directory's CONTENTS, so `ctcss.hpp` lands beside
# `ctbrowser.hpp` exactly as the build tree has it.
install(DIRECTORY "${PROJECT_SOURCE_DIR}/external/compile-time-css/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${PROJECT_SOURCE_DIR}/external/compile-time-css/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}" RENAME LICENSE-ctcss
        OPTIONAL)
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
    raster/gl.cpp
    raster/draw.cpp
    raster/ttf.cpp
    raster/svg.cpp
    raster/software.cpp
    raster/renderer.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/raster.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/gl.hpp"
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
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/pipeline.hpp")
target_compile_features(ctbrowser-raster PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-raster)
target_link_libraries(ctbrowser-raster PUBLIC ctbrowser-core ctbrowser-paint)

# ANGLE, from the pinned release tools/fetch-angle.sh downloads. PRIVATE, and
# nothing that includes it inherits them and tests/api_surface stays satisfied.
#
# The definition is PUBLIC because a consumer needs to know whether the device
# can exist - `gles::available()` answers at run time, but a test still has to
# compile either way.
if(CTBROWSER_WITH_ANGLE)
  if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL Windows)
    set(angle-dir "${PROJECT_SOURCE_DIR}/third_party/angle/windows-x64")
    set(angle-libs "${angle-dir}/libEGL.dll.lib" "${angle-dir}/libGLESv2.dll.lib")
  else()
    set(angle-dir "${PROJECT_SOURCE_DIR}/third_party/angle/linux-x86_64")
    set(angle-libs "${angle-dir}/libEGL.so" "${angle-dir}/libGLESv2.so")
  endif()
  if(NOT EXISTS "${angle-dir}")
    message(FATAL_ERROR
      "CTBROWSER_WITH_ANGLE is ON but ${angle-dir} is missing - run tools/fetch-angle.sh")
  endif()
  target_include_directories(ctbrowser-raster PRIVATE "${angle-dir}/include")
  target_link_libraries(ctbrowser-raster PRIVATE ${angle-libs})
  target_compile_definitions(ctbrowser-raster PUBLIC CTBROWSER_WITH_ANGLE=1)
  # THE LIBRARIES HAVE TO BE FOUND AT RUN TIME TOO, and they are not in any
  # system path: an rpath here is what stops every consumer needing
  # LD_LIBRARY_PATH set by hand.
  if(NOT WIN32)
    set_target_properties(ctbrowser-raster PROPERTIES
      BUILD_RPATH "${angle-dir}" INSTALL_RPATH "${angle-dir}")
  endif()
  message(STATUS "ctbrowser: ANGLE - a real GLES device is available")
endif()
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
# --- THE HTTP TRANSPORT ---------------------------------------------------
#
# libcurl, and only libcurl. There used to be a hand-written Asio client beside
# it and a switch to choose; both are gone (see docs/build.md). Asio is a
# socket, and everything above the socket - the request line, header folding,
# chunked decoding, redirects - had to be written and maintained here, which is
# precisely the half a browser keeps needing more of. curl brings TLS as well:
# SCHANNEL on Windows, so the cross build has https with no OpenSSL.
#
# REQUIRED rather than optional. An engine that silently falls back to a
# transport with no TLS is worse than one that will not configure.
find_package(CURL REQUIRED)

add_library(ctbrowser-shell)
target_sources(ctbrowser-shell
  PRIVATE
    # the assembly itself
    shell/browser.cpp
    shell/bindings.cpp
    shell/embedded_fonts.cpp
    # resources: where bytes come from. net.cpp was net_curl.cpp until
    # 2026-08-09; the suffix implied a choice of transport, and the Asio one has
    # been gone for a while.
    shell/net/net.cpp
    shell/net/url.cpp
    # image codecs, each behind a two-function header, none of them SDL
    shell/image/png.cpp
    shell/image/jpeg.cpp
    shell/image/images.cpp
    # what a PAGE can do
    shell/page/canvas.cpp
    shell/page/forms.cpp
    shell/page/svg_cache.cpp
    shell/page/webgl.cpp
    shell/page/webgl_bindings.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/embedded_fonts.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/shell.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/page/webgl.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/input.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/metrics.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/page/assets.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/image/images.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/net/url.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/image/png.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/image/jpeg.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/net/net.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/page/canvas.hpp"
        # PUBLIC, and it has to be here: canvas.hpp - which IS installed -
        # includes it. Missing from this list until 2026-08-09, which made the
        # INSTALLED package unusable while the build tree was fine, because a
        # build tree finds a header whether or not anything shipped it.
        # tools/check/check-package.sh is what proves this, and it is the only thing
        # that can.
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/page/composite.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/page/svg_cache.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/shell/page/forms.hpp"
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
# PRIVATE: boost/url is 1.2 MB of headers and shell/url.cpp is the only place it
# appears. Nothing that links the engine should have to parse it - the same rule
# net.hpp states for curl.h.
target_link_libraries(ctbrowser-shell PRIVATE Boost::url)
# PRIVATE for the same reason: curl.h is the transport's business and
# net_curl.cpp is the only place it appears.
target_link_libraries(ctbrowser-shell PRIVATE CURL::libcurl)

# --- PNG, WITHOUT SDL ----------------------------------------------------
# The engine decoded BMP itself and got every other format from a hook the
# application layer fills in from SDL3_image. That split held until `tests/`
# needed a PNG: the suite is SDL-FREE by an invariant `tests/api_surface`
# lints for, so every test saw a PNG as a zero-sized image and nothing said so.
# Phaser's texture manager loads three base64 PNGs during boot and waits for
# all three, which is how it was found.
#
# LIBPNG, the format's reference implementation, and zlib with it. PRIVATE for
# the same reason as curl and boost/url: png.h is png.cpp's business.
find_package(PNG REQUIRED)
target_link_libraries(ctbrowser-shell PRIVATE PNG::PNG)

# --- JPEG, WITHOUT SDL ---------------------------------------------------
# The same move as PNG above and for the same reason: a format that only
# decodes when SDL3_image happened to be found is one no test can assert on
# and no golden can compare.
#
# LIBJPEG-TURBO through its TurboJPEG API - libjpeg's decoder with SIMD, and
# the one browsers and Android ship. PRIVATE: turbojpeg.h is jpeg.cpp's
# business. No CMake config module is guaranteed for it, so pkg-config is the
# portable way to ask, with a plain library search as the fallback the cross
# build lands on.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(CTB_TURBOJPEG QUIET IMPORTED_TARGET libturbojpeg)
endif()
if(TARGET PkgConfig::CTB_TURBOJPEG)
  target_link_libraries(ctbrowser-shell PRIVATE PkgConfig::CTB_TURBOJPEG)
  message(STATUS "ctbrowser: JPEG through libjpeg-turbo ${CTB_TURBOJPEG_VERSION}")
else()
  find_path(CTBROWSER_TURBOJPEG_INCLUDE_DIR turbojpeg.h)
  find_library(CTBROWSER_TURBOJPEG_LIBRARY NAMES turbojpeg libturbojpeg)
  if(NOT CTBROWSER_TURBOJPEG_INCLUDE_DIR OR NOT CTBROWSER_TURBOJPEG_LIBRARY)
    message(FATAL_ERROR
      "ctbrowser: no libjpeg-turbo for this target. It decodes JPEG in the "
      "SDL-free engine, so tests can assert on one. Install it (brew: "
      "jpeg-turbo) or build it into the mingw sysroot for a cross build.")
  endif()
  target_include_directories(ctbrowser-shell PRIVATE ${CTBROWSER_TURBOJPEG_INCLUDE_DIR})
  target_link_libraries(ctbrowser-shell PRIVATE ${CTBROWSER_TURBOJPEG_LIBRARY})
  message(STATUS "ctbrowser: JPEG through ${CTBROWSER_TURBOJPEG_LIBRARY}")
endif()

# TLS BELONGS TO THE TRANSPORT, which is most of the point of moving to libcurl.
# curl brings its own - SCHANNEL on Windows, the OS stack, so the cross build
# gets https:// with no OpenSSL cross-compiled at all. It reports what it has at
# run time through curl_version_info, which is why tls_available() is not a
# constexpr on a build-system guess.
#
# NOTHING HERE LOOKS FOR OpenSSL ANY MORE. The Asio transport needed it and the
# Asio transport is gone.
message(STATUS "ctbrowser: https:// through libcurl's own TLS (Schannel on Windows)")

add_library(ctbrowser::shell ALIAS ctbrowser-shell)

# --- ctbrowser.app -------------------------------------------------------
# The window and the event loop, and the only module that knows SDL exists.
# ALWAYS built. SDL3 is a compile definition, not a gate: without it the module
# still provides run_app() over the headless backend, which is what makes "SDL3
# is optional" true of the BUILD and not just of the engine.
add_library(ctbrowser-app)
target_sources(ctbrowser-app
  PRIVATE
    app/app.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/app/app.hpp")
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
# COMPONENTS url, and CURL beside it. Both are linked PRIVATE, which for a
# STATIC library still puts them on a consumer's link line - so a consumer that
# cannot find them fails to link with no idea why. The Boost line said no
# component at all until 2026-07-31, which was a gap from the day Boost.URL
# landed; tools/check/check-package.sh is what exists to catch exactly this.
set(CTBROWSER_CONFIG_DEPS
    "find_dependency(Boost 1.80 CONFIG COMPONENTS url)\nfind_dependency(CURL)\nfind_dependency(Threads)")
# PNG, and it is NOT optional - `find_package(PNG REQUIRED)` above. It was
# missing from this list until 2026-08-09 and the symptom was exactly what the
# comment above predicts: `find_package(ctbrowser)` failed at GENERATE time,
# before a single line compiled, with "the link interface of target
# ctbrowser::ctbrowser-shell contains PNG::PNG but the target was not found".
# PRIVATE does not save you here - a STATIC archive carries the dependency into
# the consumer's link interface regardless.
string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(PNG)")
if(SDL3_FOUND)
  string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(SDL3 CONFIG)")
endif()
# EVERY package an exported target links has to be findable by the consumer,
# or find_package(ctbrowser) fails on a target it cannot resolve. Adding an
# optional dependency to a library and forgetting this line is exactly the kind
# of break tools/check/check-package.sh exists to catch - it caught these two.
if(Freetype_FOUND)
  string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(Freetype)")
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
# JPEG, and it leaks the same way PNG does - but only in the pkg-config form.
# `PkgConfig::CTB_TURBOJPEG` is a TARGET NAME in the exported link interface, so
# the consumer has to be able to recreate it, and the only thing that can is the
# same pkg_check_modules call under the same prefix. The fallback branch links a
# resolved library PATH instead, and a path needs nothing found again.
if(TARGET PkgConfig::CTB_TURBOJPEG)
  string(APPEND CTBROWSER_CONFIG_DEPS
         "\nfind_dependency(PkgConfig)"
         "\npkg_check_modules(CTB_TURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)")
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
