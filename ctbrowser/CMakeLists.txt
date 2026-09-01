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
# exactly the build the OFF default used to give, and unittests/unit/gl_basics skips
# itself with a reason when no device comes up.
#
# ASKING for it explicitly is a REQUIREMENT and still fails loudly. Someone who
# typed -DCTBROWSER_WITH_ANGLE=ON wants a GLES device, not a quiet downgrade to
# the software path - and would otherwise find out from a golden.
#
# Recorded in its own cache entry because `option()` writes CTBROWSER_WITH_ANGLE
# itself: on the second configure the variable is always defined, so "did the
# user pass -D" cannot be asked again and has to be remembered.
if(NOT DEFINED CTBROWSER_ANGLE_REQUESTED)
  if(DEFINED CTBROWSER_WITH_ANGLE)
    set(CTBROWSER_ANGLE_REQUESTED ON CACHE INTERNAL "ANGLE was asked for explicitly")
  else()
    set(CTBROWSER_ANGLE_REQUESTED OFF CACHE INTERNAL "ANGLE was asked for explicitly")
  endif()
endif()

option(CTBROWSER_WITH_ANGLE "Use ANGLE for the WebGL back end (tools/fetch-angle.sh fetches it)" ON)

# Resolved HERE, above add_subdirectory, so lib/Raster and examples/ - which
# wires the four ANGLE goldens on this variable - both see the same answer.
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL Windows)
  set(CTBROWSER_ANGLE_DIR "${CTBROWSER_MONOREPO_ROOT}/third-party/angle/windows-x64")
else()
  set(CTBROWSER_ANGLE_DIR "${CTBROWSER_MONOREPO_ROOT}/third-party/angle/linux-x86_64")
endif()
if(CTBROWSER_WITH_ANGLE AND NOT EXISTS "${CTBROWSER_ANGLE_DIR}")
  if(CTBROWSER_ANGLE_REQUESTED)
    message(FATAL_ERROR
      "CTBROWSER_WITH_ANGLE is ON but ${CTBROWSER_ANGLE_DIR} is missing - run tools/fetch-angle.sh")
  endif()
  message(STATUS "ctbrowser: no third-party/angle - WebGL has no device. "
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

# GMP behind Boost.Multiprecision, for the BigInt type. OFF BY DEFAULT AND
# DELIBERATELY NOT "on when GMP is found", which is the obvious spelling and the
# wrong one, for two reasons measured rather than assumed:
#
#   - GMP IS SLOWER FOR THIS WORKLOAD. It wins on numbers thousands of bits
#     wide; a JavaScript BigInt is an id, a timestamp or a 64-bit hash. At 64
#     bits GMP measured 2.9x slower than cpp_int on Linux and 5.5x on Windows,
#     because cpp_int keeps a small value inline and every mpz_t is a heap
#     allocation. Tuning GMP for a modern CPU does not close it - the cost is
#     allocation, not instruction selection. Numbers in docs/script.md.
#   - IT CHANGES THE LICENCE OF THE BINARY. GMP is LGPLv3+ or GPLv2+ and this
#     engine ships statically linked, self-contained .exe files under Apache-2.0
#     with LLVM exceptions. Static LGPL linking carries relinking obligations,
#     and auto-detection would attach them to anyone who happened to have GMP
#     installed, silently. See NOTICE and docs/script.md.
#
# It builds and passes on both backends - unittests/js/bigint_basics.cpp is the gate -
# and the Windows cross-build works with GMP's assembly enabled
# (tools/mingw/build-gmp-mingw.sh).
option(CTBROWSER_WITH_GMP "Use GNU GMP as the Boost.Multiprecision BigInt backend (LGPL)" OFF)

# The strict flags every test and example builds with, kept off the exported
# interface: a consumer's warnings are their own business.
set(CTBROWSER_WARNING_OPTIONS
  -O2 -pedantic -Wall -Wextra -Werror -Wconversion -Wno-overlength-strings)

# Two compile-time bricks are still submodules, for different reasons: ctjs is
# the script compiler's runtime parser; ctcss is only the comparison oracle for
# benchmarks/bench_style.cpp. cthtml is NOT one - the DOM has its own WHATWG
# tokenizer and tree builder. The full ctjs interpreter used by vm_basics also
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
    set(CTBROWSER_SDL3_IMAGE_IS_STATIC ${image_static})
  endif()
  find_package(SDL3_mixer CONFIG QUIET)
  if(SDL3_mixer_FOUND)
    ctbrowser_pick_sdl_target(mixer_target mixer_static SDL3_mixer::SDL3_mixer)
    list(APPEND CTBROWSER_SAT_DEFS CTBROWSER_WITH_MIXER)
    list(APPEND CTBROWSER_SAT_LIBS ${mixer_target})
    set(CTBROWSER_SDL3_MIXER_IS_STATIC ${mixer_static})
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
# The LLVM model: this project is the configure root and names the siblings to
# build beside it. `ctcompile` is the compiler, and it depends on ctbrowser's
# targets in this direction and never the other one.
#
# LAST, deliberately. A sibling consumes the engine's targets, so they have to
# exist before it is added - which also means a sibling cannot influence how the
# engine is configured, and that is exactly the guarantee Principle 8 wants.
set(CTBROWSER_ENABLE_PROJECTS "" CACHE STRING
    "Sibling projects to build beside ctbrowser (e.g. ctcompile)")
foreach(project IN LISTS CTBROWSER_ENABLE_PROJECTS)
  set(project-dir "${CTBROWSER_MONOREPO_ROOT}/${project}")
  if(NOT EXISTS "${project-dir}/CMakeLists.txt")
    message(FATAL_ERROR
      "CTBROWSER_ENABLE_PROJECTS names '${project}', but ${project-dir}/CMakeLists.txt does not exist")
  endif()
  message(STATUS "ctbrowser: also building sibling project ${project}")
  add_subdirectory("${project-dir}" "${CMAKE_BINARY_DIR}/${project}")
endforeach()

# --- PRINCIPLE 8, ENFORCED -----------------------------------------------
# LLVM and MLIR are compiler-only dependencies. A runtime-only build must not
# need them installed, and the way that stops being true is gradual: someone
# adds `find_package(MLIR)` here rather than in ctcompile, everything keeps
# building on the box that has it, and the small machine silently stops being
# able to build the engine at all.
#
# So it is checked rather than stated. The siblings are added above, in a
# subdirectory scope of their own, and a find_package there does not define
# these targets in THIS scope - which is what makes the distinction testable
# from here at all.
foreach(forbidden LLVMCore MLIRIR MLIRSupport)
  if(TARGET ${forbidden})
    message(FATAL_ERROR
      "ctbrowser's own scope defines ${forbidden}: LLVM/MLIR are ctcompile's dependencies, "
      "not the engine's (Principle 8). Move the find_package into ctcompile.")
  endif()
endforeach()
