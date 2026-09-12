cmake_minimum_required(VERSION 3.20)

# ctbrowser: a browser engine in C++23. Headers are `include/ctbrowser/`, the
# implementations are `lib/`; `test/`, `unittests/`, `benchmarks/`, `examples/`
# and `tools/` are added from here.
#
# THIS DIRECTORY IS THE CONFIGURE ROOT - `cmake -S ctbrowser -B build` - and the
# repository above it is a monorepo holding sibling projects, the LLVM model.
# What lives one level up is what is genuinely shared: `third-party/` (the two
# submodules and the fetched ANGLE release), `tools/` (the scripts, which drive
# both projects), `cmake/` (toolchains and the LLVM pin) and the licence files.
# CTBROWSER_ENABLE_PROJECTS names the siblings to build beside this one.
#
# The COMPILER is ordinary: stock clang or gcc with C++23, and the BUILD asks
# nothing unusual of it. It used to: the engine was C++ named modules, which
# meant CMake 3.28 as a hard floor and a compiler able to report its import
# graph to the build system - a requirement that announced itself as "the
# compiler does not provide a way to discover the import graph dependencies",
# which says nothing about modules to anyone who has not met it before. This
# file searched for a new-enough clang before project() to soften that. Plain
# headers need none of it.
#
# The std::embed fork this project once required went with the compile-time
# engine it existed for - the engine parses at runtime now - and losing that
# requirement is what makes the sanitizer presets possible at all: the fork
# ships no compiler-rt runtimes, and TSan is the primary evidence that the
# lock-free read path is sound.

project(ctbrowser
  HOMEPAGE_URL "https://github.com/alexios-angel/compile-time-browser"
  VERSION 2.0.0
  LANGUAGES CXX)
set(PROJECT_DESCRIPTION "A browser engine: HTML, CSS and JavaScript, rendered with SDL3.")

# The monorepo root. Every reach OUT of this project goes through this variable
# rather than through a bare `..`, so the handful of shared directories are
# searchable by name and a standalone install of ctbrowser needs none of them.
get_filename_component(CTBROWSER_MONOREPO_ROOT "${PROJECT_SOURCE_DIR}/.." ABSOLUTE)

include(CMakePackageConfigHelpers)
include(CMakeDependentOption)
include(GNUInstallDirs)
include(CTest)

cmake_dependent_option(CTBROWSER_BUILD_TESTS "Build ctbrowser tests" ON
  "BUILD_TESTING;CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR" OFF)
cmake_dependent_option(CTBROWSER_BUILD_EXAMPLES "Build ctbrowser examples" ON
  "CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR" OFF)

# A Windows exe is a CONSOLE application unless told otherwise, so double
# clicking one opens a terminal beside its window. ON keeps that - it is where
# a script error or a --shot path goes, and you want it while developing. OFF
# gives the graphical examples the Windows subsystem and no terminal at all,
# which is what shipping one looks like; ctbrowse and ctdrive keep their console
# either way, being CLIs whose output is the whole point. Windows only.
option(CTBROWSER_WINDOWS_CONSOLE "Keep the console window behind a Windows exe" ON)

# COMPUTED-GOTO DISPATCH IN THE SCRIPT VM. Off, because it MEASURED SLOWER here:
# +3.0% instructions and +3.3% wall against the switch on the dispatch-bound
# benchmark (docs/performance.md has the table, benchmarks/bench_script is the
# measurement). It is kept and switchable rather than deleted because that
# result is a property of the branch predictor rather than of this code - the
# technique still wins on some microarchitectures, and on those it is one flag
# away. GNU compilers only; anything else silently keeps the switch.
# THE ALLOCATOR. mimalloc backs operator new/delete, and it is ON because it
# measured -4.2% instructions and -4.4% wall on a real Phaser frame - see
# docs/performance.md. Switchable because a platform without it should still
# build, and because turning it off is how the measurement is repeated.
# WHERE BREW PUTS THINGS. Nothing adds its prefix to CMake's default search
# path, so a box with brew's mimalloc, simdutf or cpptrace and no distribution
# copy configures with "not found" while the library sits right there. Hoisted
# to the top level because both lib/ and the test trees need it and a variable set in
# one subdirectory is invisible to its sibling.
set(CTBROWSER_BREW_HINTS "$ENV{HOMEBREW_PREFIX}" /home/linuxbrew/.linuxbrew /opt/homebrew
                         /usr/local)

option(CTBROWSER_USE_MIMALLOC "Back operator new/delete with mimalloc" ON)

# ANGLE: a real OpenGL ES device for the WebGL path. ON since 2026-08-09, and
# ON *WHEN PRESENT* rather than ON unconditionally - the same contract plutosvg,
# SDL3_ttf and SDL3_image already have here.
#
# The distinction matters because ANGLE is FETCHED, not built:
# tools/fetch-angle.sh downloads a pinned release into third-party/angle/, which
# is gitignored, so a fresh checkout has none. It is a pinned release rather than
# a build because ANGLE needs GN, depot_tools and, on Windows, clang-cl and the
# Windows SDK - a second toolchain this repository does not otherwise require.
#
# So a clone that has not fetched still configures, builds and passes: it gets
# the build the OFF default used to give, unittests/unit/gl_basics skips itself
# with a reason when no device comes up, and the configure WARNS - loudly enough
# for someone who typed -DCTBROWSER_WITH_ANGLE=ON to see the downgrade rather
# than find out from a golden.
option(CTBROWSER_WITH_ANGLE "Use ANGLE for the WebGL back end (tools/fetch-angle.sh fetches it)" ON)

# Resolved HERE, above add_subdirectory, so lib/Raster and examples/ - which
# wires the four ANGLE goldens on this variable - both see the same answer.
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL Windows)
  set(CTBROWSER_ANGLE_DIR "${CTBROWSER_MONOREPO_ROOT}/third-party/angle/windows-x64")
else()
  set(CTBROWSER_ANGLE_DIR "${CTBROWSER_MONOREPO_ROOT}/third-party/angle/linux-x86_64")
endif()
if(CTBROWSER_WITH_ANGLE AND NOT EXISTS "${CTBROWSER_ANGLE_DIR}")
  message(WARNING "ctbrowser: no ${CTBROWSER_ANGLE_DIR} - WebGL has no device. "
                  "tools/fetch-angle.sh downloads one.")
  set(CTBROWSER_WITH_ANGLE OFF)
endif()

option(CTBROWSER_COMPUTED_GOTO "Dispatch the script VM with computed gotos (GNU only)" OFF)

# Local variable names and per-instruction source offsets, on every compiled
# proto. They are what lets a stack trace and the AOT backend's C++ say
# `_isEnabled` rather than `v18[4]`.
#
# ON, AND THE COST IS NOT SMALL - measured on the three corpora in vendor/ by
# `CTBROWSER_DEBUG_TABLE_COST=1 ctbrowser-test-script_debug`, which retakes it:
#
#     image, source kept     bootstrap +31.6%   p5 +20.0%   phaser +15.7%
#     image, source dropped  bootstrap +12.7%   p5 +12.5%   phaser +14.0%
#
# The two rows differ because a source-dropped image also drops the
# per-instruction offsets - an offset into text the image does not carry cannot
# be turned into a line by anybody - so the shipped number is the names alone.
# See image_option::drop_source.
#
# OFF leaves both vectors empty on every proto and stops the compiler moving
# its source cursor. It is for a build that will never read them; the packaging
# lever is `image_option`, which needs no rebuild.
option(CTBROWSER_SCRIPT_DEBUG_NAMES "Compile local names and per-instruction source offsets" ON)

# THE TYPE ORACLE'S HOOK IN THE DISPATCH LOOP (ctcompile Phase 54B) has no
# option: what it costs when unused is NOTHING - run_loop is a template on one
# bool and the instantiation a build without a recorder runs has no trace of
# the hook in it - and the OFF build the option once offered was never built
# by anything. The per-instruction `if (recorder_)` that came first was
# measured at +0.53% on bench_script and +0.48% on phaser_invaders, which is
# why it is not what is there.

# Two compile-time bricks are still submodules, for different reasons: ctjs is
# the script compiler's runtime parser; ctcss is only the comparison oracle for
# benchmarks/bench_style.cpp. cthtml is NOT one - the DOM has its own WHATWG
# tokenizer and tree builder. The full ctjs interpreter used by vm_operators also
# needs ctc (compile-time-containers), resolved through ctjs's nested copy.
set(CTBROWSER_CTJS "${CTBROWSER_MONOREPO_ROOT}/third-party/compile-time-javascript")
if(NOT EXISTS "${CTBROWSER_CTJS}/include/ctjs/vparse.hpp")
  message(FATAL_ERROR
    "ctbrowser: the ctjs submodule is missing - run `git submodule update --init --recursive`")
endif()

if(CTBROWSER_BUILD_TESTS)
  set(CTBROWSER_CTC "${CTBROWSER_CTJS}/external/compile-time-containers")
  if(NOT EXISTS "${CTBROWSER_CTC}/include/ctc.hpp")
    message(FATAL_ERROR
      "ctbrowser: ctjs's nested ctc test dependency is missing - run `git submodule update --init --recursive`")
  endif()
endif()

# SDL3 and its satellites. The satellites are OPTIONAL and light up richer
# paths - SDL3_image decodes more than BMP, SDL3_ttf draws outline text - and
# the built-in fallbacks serve without them.
find_package(SDL3 CONFIG QUIET)

# STATIC or shared. SDL's package exports both `SDL3::SDL3-shared` and
# `SDL3::SDL3-static` and names whichever exists `SDL3::SDL3`, so a build that
# links the generic name gets whatever happens to be installed - and then
# discovers at run time whether it needed a DLL beside the .exe. Deciding here
# instead makes it a property of the build: with a static SDL3 in the sysroot
# (llvm-mingw's build-sdl3.sh puts one there) an application is ONE FILE.
option(CTBROWSER_SDL3_STATIC "Link SDL3 statically when a static build is available" ON)

# name -> the target to link, and whether that target needs its DLL shipped.
function(ctbrowser_pick_sdl_target out_var out_static name)
  set(static_target "${name}-static")
  set(shared_target "${name}-shared")
  if(CTBROWSER_SDL3_STATIC AND TARGET ${static_target})
    set(${out_var} ${static_target} PARENT_SCOPE)
    set(${out_static} ON PARENT_SCOPE)
  elseif(TARGET ${shared_target})
    set(${out_var} ${shared_target} PARENT_SCOPE)
    set(${out_static} OFF PARENT_SCOPE)
  else()
    # No split targets: an older package, or one that ships only one kind.
    set(${out_var} ${name} PARENT_SCOPE)
    set(${out_static} OFF PARENT_SCOPE)
  endif()
endfunction()

set(CTBROWSER_SAT_DEFS "")
set(CTBROWSER_SAT_LIBS "")
set(CTBROWSER_SDL3_TARGET SDL3::SDL3)
set(CTBROWSER_SDL3_IS_STATIC OFF)
set(CTBROWSER_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf)
set(CTBROWSER_SDL3_TTF_IS_STATIC OFF)
if(SDL3_FOUND)
  ctbrowser_pick_sdl_target(CTBROWSER_SDL3_TARGET CTBROWSER_SDL3_IS_STATIC SDL3::SDL3)
  find_package(SDL3_image CONFIG QUIET)
  if(SDL3_image_FOUND)
    ctbrowser_pick_sdl_target(image_target image_static SDL3_image::SDL3_image)
    list(APPEND CTBROWSER_SAT_DEFS CTBROWSER_WITH_IMAGE)
    list(APPEND CTBROWSER_SAT_LIBS ${image_target})
  endif()
  find_package(SDL3_ttf CONFIG QUIET)
  if(SDL3_ttf_FOUND)
    ctbrowser_pick_sdl_target(CTBROWSER_SDL3_TTF_TARGET CTBROWSER_SDL3_TTF_IS_STATIC
                              SDL3_ttf::SDL3_ttf)
    list(APPEND CTBROWSER_SAT_DEFS CTBROWSER_WITH_TTF)
    list(APPEND CTBROWSER_SAT_LIBS ${CTBROWSER_SDL3_TTF_TARGET})
  endif()
  message(STATUS "ctbrowser: SDL3 found (${CTBROWSER_SDL3_TARGET}); satellites: ${CTBROWSER_SAT_DEFS}")
endif()

# SVG, through plutosvg (which brings plutovg with it). NOT an SDL satellite,
# despite arriving beside them: it is a plain C rasteriser the engine calls
# directly, and it is the SAME library SDL3_ttf already links for colour glyphs
# on the Windows cross-build - so on that side it is already in the sysroot.
#
# Optional like the rest. Without it `<img src=x.svg>` and inline `<svg>` lay
# out identically and draw nothing; see include/ctbrowser/raster/svg.hpp.
find_package(plutosvg CONFIG QUIET)
if(plutosvg_FOUND)
  message(STATUS "ctbrowser: plutosvg found - SVG renders")
else()
  message(STATUS "ctbrowser: no plutosvg - <svg> and .svg images do not draw")
endif()

add_subdirectory(lib)

# examples/, tools/ and the test trees ARE PROJECT-LEVEL, and until 2026-08-09
# they were not: the engine subtree reached back out to `../examples` and
# `../tests`, so it pulled in its own siblings. They only need
# ctbrowser_target() and the targets, both of which exist by now.
#
# tools/ is where the two CLIs live now: `lib/` is reusable implementation and
# `tools/` is built executables, the split Phase -1 asks for, and the reason
# ctbrowse and ctdrive are no longer examples of anything.
#
# tools/ BEFORE examples/, and the order is load-bearing: the Windows dist is
# assembled in examples/CMakeLists.txt and ships the tools too, which it can
# only do once their targets exist.
add_subdirectory(tools)
add_subdirectory(examples)

if(CTBROWSER_BUILD_TESTS)
  # One function, three trees. unittests/ is focused C++ per subsystem, test/ is
  # system-level (corpus ratchets, lint, stress, the golden and baseline DATA),
  # and benchmarks/ is measurement rather than pass/fail - so the probes and the
  # function they configure live in a module both can include.
  include("${PROJECT_SOURCE_DIR}/cmake/modules/CTTest.cmake")
  add_subdirectory(test)
  add_subdirectory(unittests)
  add_subdirectory(benchmarks)
endif()

# --- SIBLING PROJECTS ----------------------------------------------------
# The LLVM model: this project is the configure root and CTBROWSER_ENABLE_PROJECTS
# names the siblings to build beside it (the default preset passes `ctcompile`).
# LAST, deliberately: a sibling consumes the engine's targets and cannot
# influence how the engine is configured.
set(CTBROWSER_ENABLE_PROJECTS "" CACHE STRING
    "Sibling projects to build beside ctbrowser (e.g. ctcompile)")
foreach(project IN LISTS CTBROWSER_ENABLE_PROJECTS)
  message(STATUS "ctbrowser: also building sibling project ${project}")
  add_subdirectory("${CTBROWSER_MONOREPO_ROOT}/${project}" "${CMAKE_BINARY_DIR}/${project}")
endforeach()

# LLVM and MLIR are ctcompile's dependencies, not the engine's (Principle 8): a
# find_package in the sibling's own scope does not define these targets here,
# so a hit means one crept into the engine's build.
foreach(forbidden LLVMCore MLIRIR MLIRSupport)
  if(TARGET ${forbidden})
    message(FATAL_ERROR
      "ctbrowser's own scope defines ${forbidden}: LLVM/MLIR belong to ctcompile, not the engine")
  endif()
endforeach()

# === WPT ===================================================================
# web-platform-tests, OPT-IN. `-DCTBROWSER_WPT=ON` registers two ctest entries;
# without it this block registers nothing and costs one `if`.
#
# OFF BY DEFAULT because the corpus is not in this repository and must not be:
# it is ~40 MB fetched to ~/.cache/wpt at a pinned commit by
# tools/wpt/fetch-wpt.sh, and a test that silently passes when the corpus is
# absent is worse than one that is not registered at all.
#
# TWO TESTS, and the first is the one that matters:
#
#   wpt-selftest  the NEGATIVE PROOFS - five fixtures whose outcomes are
#                 asserted, including a test that must fail and a page with no
#                 harness at all. Seconds to run, and nothing below it means
#                 anything until it passes.
#   wpt-gate      a fixed subset measured against tools/wpt/expectations.txt,
#                 budgeted at two minutes. Fails on a difference in EITHER
#                 direction: a regression, and an unexpected PASS, because an
#                 expectations file that has quietly gone stale is a file that
#                 lies about the engine.
#
# The FULL run is a documented command, not a build step - see
# ctbrowser/docs/wpt.md. It takes tens of minutes and belongs to a person.
option(CTBROWSER_WPT "Register the web-platform-tests ctest entries" OFF)
if(CTBROWSER_WPT AND CTBROWSER_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(wpt-runner "${CTBROWSER_MONOREPO_ROOT}/tools/wpt/run-wpt.py")
  # THE DRIVER BY GENERATOR EXPRESSION, not by a guessed path. run-wpt.py's own
  # default is build/tools/ctdrive, which is right for a person typing the
  # command and wrong for any build tree that is not called `build`.
  add_test(NAME wpt-selftest COMMAND ${Python3_EXECUTABLE} "${wpt-runner}"
           --selftest --driver "$<TARGET_FILE:ctbrowser-tool-ctdrive>")
  add_test(NAME wpt-gate COMMAND ${Python3_EXECUTABLE} "${wpt-runner}"
           --gate --jobs 4 --driver "$<TARGET_FILE:ctbrowser-tool-ctdrive>")
  # LABELLED, so `ctest -L wpt` runs exactly these two and `ctest -LE wpt` runs
  # everything else. TIMEOUT is a backstop above the runner's own per-test one:
  # the runner kills a hung page itself, and this catches the runner hanging.
  set_tests_properties(wpt-selftest wpt-gate PROPERTIES LABELS wpt TIMEOUT 300)
endif()
