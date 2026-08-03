cmake_minimum_required(VERSION 3.20)

# ctbrowser: a browser engine in C++23. Headers are `include/ctbrowser/`, the
# implementations are `src/`; `tests/` and `examples/` are added from there.
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

include(CMakePackageConfigHelpers)
include(CMakeDependentOption)
include(GNUInstallDirs)
include(CTest)

cmake_dependent_option(CTBROWSER_BUILD_TESTS "Build ctbrowser tests" ON
  "BUILD_TESTING;CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR" OFF)
cmake_dependent_option(CTBROWSER_BUILD_EXAMPLES "Build ctbrowser examples (need SDL3)" ON
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
# benchmark (docs/performance.md has the table, tests/bench_script is the
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
# to the top level because both src/ and tests/ need it and a variable set in
# one subdirectory is invisible to its sibling.
set(CTBROWSER_BREW_HINTS "$ENV{HOMEBREW_PREFIX}" /home/linuxbrew/.linuxbrew /opt/homebrew
                         /usr/local)

option(CTBROWSER_USE_MIMALLOC "Back operator new/delete with mimalloc" ON)

option(CTBROWSER_COMPUTED_GOTO "Dispatch the script VM with computed gotos (GNU only)" OFF)

# The strict flags every test and example builds with, kept off the exported
# interface: a consumer's warnings are their own business.
set(CTBROWSER_WARNING_OPTIONS
  -O2 -pedantic -Wall -Wextra -Werror -Wconversion -Wno-overlength-strings)

# Two of the compile-time bricks are still submodules: ctcss is the style
# engine's CSS parser and ctjs is the script parser. cthtml is NOT - the DOM
# has its own WHATWG tokenizer and tree builder. ctc (compile-time-containers)
# resolves through ctjs's nested copy, so exactly one is on the include path.
set(CTBROWSER_CTC "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/external/compile-time-containers")
set(CTBROWSER_SUBMODULE_INCLUDE_DIRS
  "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/include"
  "${PROJECT_SOURCE_DIR}/external/compile-time-css/include"
  "${CTBROWSER_CTC}/include")

if(NOT EXISTS "${CTBROWSER_CTC}/include/ctc.hpp")
  message(FATAL_ERROR
    "ctbrowser: submodules are missing - run `git submodule update --init --recursive`")
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

add_subdirectory(src)
