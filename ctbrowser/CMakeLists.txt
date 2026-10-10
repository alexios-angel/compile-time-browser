cmake_minimum_required(VERSION 3.20)

# ctbrowser: a browser engine in C++23. Headers are `include/ctbrowser/`, the
# implementations are `lib/`; `test/`, `unittests/`, `benchmarks/`, `examples/`
# and `tools/` are added from here.
#
# This directory is the configure root (`cmake -S ctbrowser -B build`). The
# repository above it is a monorepo of sibling projects, as LLVM's is. One level
# up lives only what is shared: `third-party/` (the two submodules and the
# fetched ANGLE release), `tools/` (scripts that drive both projects), `cmake/`
# (toolchains and the LLVM pin) and the licence files.
# CTBROWSER_ENABLE_PROJECTS names the siblings to build beside this one.
#
# Any stock clang or gcc with C++23 builds this; there are no C++ modules and
# no special compiler requirements. The compiler needs its compiler-rt
# runtimes, because the sanitizer presets (asan, tsan) depend on them.

project(ctbrowser
  HOMEPAGE_URL "https://github.com/alexios-angel/compile-time-browser"
  VERSION 2.0.0
  LANGUAGES CXX)
set(PROJECT_DESCRIPTION "A browser engine: HTML, CSS and JavaScript, rendered with SDL3.")

# The monorepo root. Every path outside this project goes through this variable
# rather than a bare `..`, so the few shared directories are searchable by name
# and a standalone install of ctbrowser needs none of them.
get_filename_component(CTBROWSER_MONOREPO_ROOT "${PROJECT_SOURCE_DIR}" DIRECTORY)
set(CTBROWSER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")

# ccache is not automatic: CMake uses it only as the compiler launcher. It is
# used whenever it is installed (the devbox's cloud-init installs it, with one
# cache shared by every project). These are cache variables, so a configured
# tree keeps them when a checkout without these lines is synced over it;
# -DCMAKE_CXX_COMPILER_LAUNCHER= turns it off.
find_program(CTBROWSER_CCACHE ccache)
if(CTBROWSER_CCACHE)
  set(CMAKE_C_COMPILER_LAUNCHER "${CTBROWSER_CCACHE}" CACHE STRING "Launcher for C compiles")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CTBROWSER_CCACHE}" CACHE STRING "Launcher for C++ compiles")
endif()

include(CMakePackageConfigHelpers)
include(CMakeDependentOption)
include(GNUInstallDirs)
include(CTest)

cmake_dependent_option(CTBROWSER_BUILD_TESTS "Build ctbrowser tests" ON
  "BUILD_TESTING;CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR" OFF)
cmake_dependent_option(CTBROWSER_BUILD_EXAMPLES "Build ctbrowser examples" ON
  "CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR" OFF)

# A Windows exe is a console application unless told otherwise, so
# double-clicking one opens a terminal beside its window. ON keeps that
# terminal, which is where script errors and --shot paths are printed during
# development. OFF builds the graphical examples for the Windows subsystem,
# with no terminal, as a shipped application would be. ctbrowse and ctdrive
# keep their console either way, because they are CLIs. Windows only.
option(CTBROWSER_WINDOWS_CONSOLE "Keep the console window behind a Windows exe" ON)

# Homebrew's install prefixes. Nothing adds them to CMake's default search
# path, so without these hints a machine with brew's mimalloc, simdutf or
# cpptrace and no distribution copy reports "not found". Set at the top level
# because both lib/ and the test trees need it, and a variable set in one
# subdirectory is invisible to its siblings.
set(CTBROWSER_BREW_HINTS "$ENV{HOMEBREW_PREFIX}" /home/linuxbrew/.linuxbrew /opt/homebrew
                         /usr/local)

# mimalloc backs operator new/delete. It is ON because it measured -4.2%
# instructions and -4.4% wall time on a real Phaser frame (docs/performance.md).
# It is switchable so a platform without it still builds, and so the
# measurement can be repeated.
option(CTBROWSER_USE_MIMALLOC "Back operator new/delete with mimalloc" ON)

# ANGLE: a real OpenGL ES device for the WebGL path. ON when present, not
# unconditionally, like plutosvg, SDL3_ttf and SDL3_image.
#
# ANGLE is fetched, not built: tools/fetch-angle.sh downloads a pinned release
# into third-party/angle/, which is gitignored, so a fresh checkout has none.
# Building it would need GN, depot_tools and, on Windows, clang-cl and the
# Windows SDK, a second toolchain this repository does not otherwise require.
#
# A clone that has not fetched it still configures, builds and passes: WebGL
# has no device, unittests/unit/gl_basics skips itself with a reason, and
# configure prints a warning so someone who passed -DCTBROWSER_WITH_ANGLE=ON
# sees the downgrade instead of discovering it from a failing golden.
option(CTBROWSER_WITH_ANGLE "Use ANGLE for the WebGL back end (tools/fetch-angle.sh fetches it)" ON)

# Resolved here, before add_subdirectory, so lib/Raster and examples/ (which
# registers the ANGLE goldens based on this variable) see the same answer.
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

# Computed-goto dispatch in the script VM. OFF because it measured slower here:
# +3.0% instructions and +3.3% wall time against the switch on the
# dispatch-bound benchmark (benchmarks/bench_script; table in
# docs/performance.md). It is kept because the result depends on the branch
# predictor, and the technique still wins on some microarchitectures. GNU
# compilers only; any other compiler silently keeps the switch.
option(CTBROWSER_COMPUTED_GOTO "Dispatch the script VM with computed gotos (GNU only)" OFF)

# Local variable names and per-instruction source offsets on every compiled
# proto. They let a stack trace and the AOT backend's C++ say `_isEnabled`
# rather than `v18[4]`.
#
# ON, and the size cost is significant. Measured on the three corpora in vendor/
# by `CTBROWSER_DEBUG_TABLE_COST=1 ctbrowser-test-script_debug`, which repeats
# the measurement:
#
#     image, source kept     bootstrap +31.6%   p5 +20.0%   phaser +15.7%
#     image, source dropped  bootstrap +12.7%   p5 +12.5%   phaser +14.0%
#
# The two rows differ because a source-dropped image also drops the
# per-instruction offsets (an offset into text the image does not carry cannot
# be turned into a line), so the shipped cost is the names alone. See
# image_option::drop_source.
#
# OFF leaves both vectors empty on every proto and stops the compiler moving
# its source cursor. It is for a build that will never read them; the packaging
# lever is `image_option`, which needs no rebuild.
option(CTBROWSER_SCRIPT_DEBUG_NAMES "Compile local names and per-instruction source offsets" ON)

# The type-recording hook that ctcompile uses in the script VM's dispatch loop
# has no build option because it costs nothing when unused: run_loop is a
# template on one bool, and the instantiation used without a recorder contains
# no trace of the hook. A per-instruction `if (recorder_)` measured +0.53% on
# bench_script and +0.48% on phaser_invaders, which is why the template is
# used instead.

# Two compile-time libraries are still submodules, for different reasons: ctjs
# is the script compiler's runtime parser; ctcss is only the comparison oracle
# for benchmarks/bench_style.cpp. cthtml is not one: the DOM has its own WHATWG
# tokenizer and tree builder. The full ctjs interpreter used by vm_operators
# also needs ctc (compile-time-containers), found through ctjs's nested copy.
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

# SDL3 and its satellite libraries. The satellites are optional and enable
# richer paths (SDL3_image decodes more formats, SDL3_ttf draws outline text);
# built-in fallbacks are used without them.
find_package(SDL3 CONFIG QUIET)

# Static or shared. SDL's package exports both `SDL3::SDL3-shared` and
# `SDL3::SDL3-static` and aliases whichever exists as `SDL3::SDL3`, so a build
# that links the generic name gets whatever is installed and discovers only at
# run time whether it needs a DLL beside the .exe. Choosing here makes it a
# property of the build: with a static SDL3 in the sysroot (llvm-mingw's
# build-sdl3.sh puts one there), an application is a single file.
option(CTBROWSER_SDL3_STATIC "Link SDL3 statically when a static build is available" ON)

# Maps an SDL package name to the target to link (`out_var`) and whether that
# target is static (`out_static`; OFF means its DLL must be shipped).
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

# Chosen once, here; lib/Raster, lib/App and examples/ link and read these.
set(CTBROWSER_SDL3_TARGET SDL3::SDL3)
set(CTBROWSER_SDL3_IS_STATIC OFF)
set(CTBROWSER_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf)
set(CTBROWSER_SDL3_TTF_IS_STATIC OFF)
set(CTBROWSER_SDL3_IMAGE_TARGET SDL3_image::SDL3_image)
if(SDL3_FOUND)
  ctbrowser_pick_sdl_target(CTBROWSER_SDL3_TARGET CTBROWSER_SDL3_IS_STATIC SDL3::SDL3)
  find_package(SDL3_image CONFIG QUIET)
  if(SDL3_image_FOUND)
    ctbrowser_pick_sdl_target(CTBROWSER_SDL3_IMAGE_TARGET image_static SDL3_image::SDL3_image)
  endif()
  find_package(SDL3_ttf CONFIG QUIET)
  if(SDL3_ttf_FOUND)
    ctbrowser_pick_sdl_target(CTBROWSER_SDL3_TTF_TARGET CTBROWSER_SDL3_TTF_IS_STATIC
                              SDL3_ttf::SDL3_ttf)
  endif()
  message(STATUS "ctbrowser: SDL3 found (${CTBROWSER_SDL3_TARGET})")
endif()

# SVG, through plutosvg (which brings plutovg with it). It is not an SDL
# satellite: it is a plain C rasteriser the engine calls directly. SDL3_ttf
# already links the same library for colour glyphs on the Windows cross build,
# so it is already in that sysroot.
#
# Optional. Without it, `<img src=x.svg>` and inline `<svg>` lay out identically
# and draw nothing; see include/ctbrowser/raster/svg.hpp.
find_package(plutosvg CONFIG QUIET)
if(plutosvg_FOUND)
  message(STATUS "ctbrowser: plutosvg found - SVG renders")
else()
  message(STATUS "ctbrowser: no plutosvg - <svg> and .svg images do not draw")
endif()

add_subdirectory(lib)
add_subdirectory(include)

# examples/, tools/ and the test trees are added here, at project level, so the
# engine under lib/ never reaches out to its siblings. They need only
# ctbrowser_target() and the engine targets, which exist by now. tools/ holds
# the two CLIs (ctbrowse and ctdrive); lib/ is reusable implementation only.
#
# tools/ must come before examples/: the Windows dist is assembled in
# examples/CMakeLists.txt and ships the tools, so their targets must exist.
add_subdirectory(tools)
add_subdirectory(examples)

if(CTBROWSER_BUILD_TESTS)
  # One function, three trees. unittests/ is focused C++ per subsystem, test/ is
  # system-level (corpus ratchets, lint, stress, the golden and baseline data),
  # and benchmarks/ is measurement rather than pass/fail. The probes and the
  # ctbrowser_test() function they configure live in a module all three include.
  include("${PROJECT_SOURCE_DIR}/cmake/modules/CTTest.cmake")
  add_subdirectory(test)
  add_subdirectory(unittests)
  add_subdirectory(benchmarks)
endif()

# --- Sibling projects ----------------------------------------------------
# As in LLVM, this project is the configure root and CTBROWSER_ENABLE_PROJECTS
# names the siblings to build beside it (the default preset passes `ctcompile`).
# Added last on purpose: a sibling consumes the engine's targets and cannot
# influence how the engine is configured.
set(CTBROWSER_ENABLE_PROJECTS "" CACHE STRING
    "Sibling projects to build beside ctbrowser (e.g. ctcompile)")
foreach(project IN LISTS CTBROWSER_ENABLE_PROJECTS)
  message(STATUS "ctbrowser: also building sibling project ${project}")
  add_subdirectory("${CTBROWSER_MONOREPO_ROOT}/${project}" "${CMAKE_BINARY_DIR}/${project}")
endforeach()

# LLVM and MLIR are ctcompile's dependencies, not the engine's: the engine must
# build without them. A find_package in the sibling's own scope does not define these targets here,
# so a hit means one crept into the engine's build.
foreach(forbidden LLVMCore MLIRIR MLIRSupport)
  if(TARGET ${forbidden})
    message(FATAL_ERROR
      "ctbrowser's own scope defines ${forbidden}: LLVM/MLIR belong to ctcompile, not the engine")
  endif()
endforeach()

# === WPT ===================================================================
# web-platform-tests, opt-in. `-DCTBROWSER_WPT=ON` registers two ctest entries;
# without it this block registers nothing.
#
# Off by default because the corpus is not in this repository: it is ~40 MB,
# fetched to ~/.cache/wpt at a pinned commit by tools/wpt/fetch-wpt.sh, and a
# test that silently passes when the corpus is absent is worse than none.
#
# Two tests; the first must pass before the second means anything:
#
#   wpt-selftest  Negative proofs: five fixtures whose outcomes are asserted,
#                 including a test that must fail and a page with no harness
#                 at all. Runs in seconds.
#   wpt-gate      A fixed subset compared against tools/wpt/expectations.txt,
#                 budgeted at two minutes. Fails on a difference in either
#                 direction, a regression or an unexpected pass, so the
#                 expectations file cannot silently go stale.
#
# The full run is a documented command, not a build step (see
# ctbrowser/docs/wpt.md); it takes tens of minutes.
option(CTBROWSER_WPT "Register the web-platform-tests ctest entries" OFF)
if(CTBROWSER_WPT AND CTBROWSER_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(wpt-runner "${CTBROWSER_MONOREPO_ROOT}/tools/wpt/run-wpt.py")
  # The driver is passed by generator expression, not a guessed path:
  # run-wpt.py's default is build/tools/ctdrive, which is wrong for any build
  # tree not called `build`.
  add_test(NAME wpt-selftest COMMAND ${Python3_EXECUTABLE} "${wpt-runner}"
           --selftest --driver "$<TARGET_FILE:ctbrowser-tool-ctdrive>")
  add_test(NAME wpt-gate COMMAND ${Python3_EXECUTABLE} "${wpt-runner}"
           --gate --jobs 4 --driver "$<TARGET_FILE:ctbrowser-tool-ctdrive>")
  # Labelled, so `ctest -L wpt` runs exactly these two and `ctest -LE wpt` runs
  # everything else. TIMEOUT is a backstop above the runner's own per-test one:
  # the runner kills a hung page itself, and this catches the runner hanging.
  set_tests_properties(wpt-selftest wpt-gate PROPERTIES LABELS wpt TIMEOUT 300)
endif()
