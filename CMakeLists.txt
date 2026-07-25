cmake_minimum_required(VERSION 3.16...4.3)

# V1's compiler: the project's own std::embed clang, C++23 and up (must be
# chosen BEFORE project() locks the toolchain). std::embed
# (__builtin_std_embed) is load-bearing for v1 - assets.hpp - so a compiler
# without it, including stock clang, cannot build v1; verified below.
#
# V2 does NOT need it. Retiring the compile-time thesis retired the std::embed
# requirement with it, so v2 builds on stock clang and gcc - which is what makes
# the sanitizer presets possible at all: the clang-std-embed fork ships no
# compiler-rt sanitizer runtimes (no libclang_rt.tsan.a), and TSan is the
# primary evidence that v2's lock-free read path is sound. Configure with
# -DCTBROWSER_BUILD_V1=OFF and a stock compiler to get v2 alone.
# v1 is AUTO-DETECTED as of stage 7: it builds when the compiler can, and is
# skipped with a message when it cannot. std::embed stopped being load bearing
# for this project the moment v2 became the engine - v2 parses at runtime and
# needs no builtin, no fork and no fetched toolchain, so requiring one to
# configure AT ALL was a requirement v1 imposed on everybody.
#
# Set it explicitly to force the issue either way: ON to get a hard error when
# the toolchain is wrong, OFF to skip v1 even where it would build.
set(CTBROWSER_BUILD_V1 "AUTO" CACHE STRING
    "Build the v1 compile-time engine: ON, OFF, or AUTO (build it if the compiler can)")
set_property(CACHE CTBROWSER_BUILD_V1 PROPERTY STRINGS AUTO ON OFF)

if(NOT CTBROWSER_BUILD_V1 STREQUAL "OFF" AND NOT CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX}
   AND NOT CMAKE_TOOLCHAIN_FILE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/clang-std-embed/bin/clang++")
  set(CMAKE_CXX_COMPILER "${CMAKE_CURRENT_SOURCE_DIR}/tools/clang-std-embed/bin/clang++")
endif()

project(ctbrowser
  HOMEPAGE_URL "https://github.com/alexios-angel/compile-time-browser"
  VERSION 0.1.0
  LANGUAGES CXX)
set(PROJECT_DESCRIPTION "Compile-time browser: HTML, CSS and JS parsed at compile time, rendered with SDL3.")

include(CMakePackageConfigHelpers)
include(CMakeDependentOption)
include(GNUInstallDirs)
include(CTest)

# Does this compiler have the std::embed builtin v1 needs? Asked whenever v1 is
# not explicitly off, because the answer decides between building it, skipping
# it, and refusing to configure.
if(NOT CTBROWSER_BUILD_V1 STREQUAL "OFF")
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_FLAGS "-std=c++23")   # the builtin only exists in C++23 mode
  check_cxx_source_compiles([[
#if !defined(__has_builtin) || !__has_builtin(__builtin_std_embed)
#error no std::embed builtin
#endif
int main() { return 0; }
]] CTBROWSER_HAS_STD_EMBED_BUILTIN)
  unset(CMAKE_REQUIRED_FLAGS)
endif()

if(CTBROWSER_BUILD_V1 STREQUAL "AUTO")
  if(CTBROWSER_HAS_STD_EMBED_BUILTIN)
    set(CTBROWSER_BUILD_V1 ON)
  else()
    set(CTBROWSER_BUILD_V1 OFF)
    message(STATUS
      "ctbrowser: no __builtin_std_embed in ${CMAKE_CXX_COMPILER_ID} "
      "${CMAKE_CXX_COMPILER_VERSION} - building v2 only. This is a supported "
      "configuration: the v2 engine parses at runtime and needs no fork.")
  endif()
elseif(CTBROWSER_BUILD_V1 AND NOT CTBROWSER_HAS_STD_EMBED_BUILTIN)
  # Explicitly asked for, and impossible. That IS an error - silently building
  # something other than what was requested is how a CI job reports success for
  # a target it never built.
  message(FATAL_ERROR
    "CTBROWSER_BUILD_V1=ON was requested but ${CMAKE_CXX_COMPILER_ID} "
    "${CMAKE_CXX_COMPILER_VERSION} has no __builtin_std_embed. v1 supports ONLY "
    "the std::embed clang (tools/clang-std-embed; fetch the clang-std-embed "
    "release from the embed repo). Use -DCTBROWSER_BUILD_V1=OFF or AUTO for v2 alone.")
endif()

cmake_dependent_option(CTBROWSER_BUILD_TESTS "Build ctbrowser tests" ON
  "BUILD_TESTING;CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR" OFF)
cmake_dependent_option(CTBROWSER_BUILD_EXAMPLES "Build ctbrowser examples (need SDL3)" ON
  "CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR" OFF)

# the bricks' value parsers fold whole pages in the constant
# evaluator; the interface carries the raised limits so consumers do
# not have to know the flags
option(CTBROWSER_CONSTEXPR_LIMITS "Raise constexpr evaluation limits on the interface" ON)
option(CTBROWSER_PCH "Precompile ctbrowser.hpp for tests and examples" ON)

# the strict flags every test and example builds with (kept off the
# exported interface); -Wno-overlength-strings: a bundled app is one
# raw-string NTTP far past the 64 KB standard minimum
set(CTBROWSER_WARNING_OPTIONS
  -O2 -pedantic -Wall -Wextra -Werror -Wconversion -Wno-overlength-strings)

set(CTBROWSER_CONSTEXPR_OPTIONS
  -fconstexpr-steps=500000000 -fconstexpr-depth=1024
  --embed-dir=${PROJECT_SOURCE_DIR})

# The three bricks are git submodules (git submodule update --init
# --recursive); ctc (compile-time-containers - ctc::string carries the
# page NTTP, ctc::cfunction the layout hooks) resolves through
# compile-time-javascript's nested copy so exactly one is in play.
set(CTBROWSER_CTC "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/external/compile-time-containers")
set(CTBROWSER_SUBMODULE_INCLUDE_DIRS
  "${PROJECT_SOURCE_DIR}/external/compile-time-html/include"
  "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/include"
  "${PROJECT_SOURCE_DIR}/external/compile-time-css/include"
  "${CTBROWSER_CTC}/include")
# v1 needs all four bricks. v2 needs ctcss (its style engine) and ctjs (its
# script parser) but not cthtml - it has its own HTML parser since stage 8 - so
# the hard failure belongs to v1 and a v2-only build is told, not stopped.
if(NOT EXISTS "${CTBROWSER_CTC}/include/ctc.hpp")
  if(CTBROWSER_BUILD_V1)
    message(FATAL_ERROR
      "ctbrowser: submodules are missing - run `git submodule update --init --recursive`")
  else()
    message(STATUS "ctbrowser: submodules missing - v2 needs ctcss and ctjs, so this may fail")
  endif()
endif()

# GLM (header-only) powers the software 3D math in babylon.hpp - it
# rides the PCH via <glm/glm.hpp>. brew glm >= 1.0 preferred (constexpr
# dot/normalize); system roots serve as fallbacks.
find_path(CTBROWSER_GLM_INCLUDE_DIR glm/glm.hpp
  PATHS /home/linuxbrew/.linuxbrew/include /opt/homebrew/include
        /usr/local/include /usr/include)
# GLM serves ONLY v1's babylon.hpp. A v2-only configure used to hard-fail
# without it, which is a requirement v1 was imposing on everybody.
if(NOT CTBROWSER_GLM_INCLUDE_DIR AND CTBROWSER_BUILD_V1)
  message(FATAL_ERROR
    "ctbrowser: GLM not found (glm/glm.hpp) - `brew install glm` or "
    "`apt-get install libglm-dev`, or set CTBROWSER_GLM_INCLUDE_DIR. "
    "Cross builds want an ISOLATED dir (see cmake/toolchain-windows-*.cmake): "
    "point GLM_INC at a directory holding only glm/. "
    "Only v1 needs it: -DCTBROWSER_BUILD_V1=OFF builds v2 without it.")
endif()

add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME} INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  "$<BUILD_INTERFACE:${CTBROWSER_SUBMODULE_INCLUDE_DIRS}>"
  "$<BUILD_INTERFACE:${CTBROWSER_GLM_INCLUDE_DIR}>"
  $<INSTALL_INTERFACE:include>)

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_23)
if(CTBROWSER_CONSTEXPR_LIMITS)
  target_compile_options(${PROJECT_NAME} INTERFACE ${CTBROWSER_CONSTEXPR_OPTIONS})
endif()
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_SCAN_FOR_MODULES 0)

install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}-targets)
install(DIRECTORY include/ DESTINATION include FILES_MATCHING PATTERN *.hpp)
# the installed tree stays flat - include/{ctbrowser,cthtml,ctjs,ctcss,
# ctc} side by side - so relative includes keep working
install(DIRECTORY external/compile-time-html/include/ DESTINATION include
    FILES_MATCHING PATTERN *.hpp)
install(DIRECTORY external/compile-time-javascript/include/ DESTINATION include
    FILES_MATCHING PATTERN *.hpp)
install(DIRECTORY external/compile-time-css/include/ DESTINATION include
    FILES_MATCHING PATTERN *.hpp)
install(DIRECTORY "${CTBROWSER_CTC}/include/" DESTINATION include
    FILES_MATCHING PATTERN *.hpp)

add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

if (NOT EXISTS "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake.in")
  file(WRITE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake.in [[
    @PACKAGE_INIT@
    include("${CMAKE_CURRENT_LIST_DIR}/@PROJECT_NAME@-targets.cmake")
  ]])
endif()

configure_package_config_file(
  "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake.in"
  "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
  NO_SET_AND_CHECK_MACRO
  NO_CHECK_REQUIRED_COMPONENTS_MACRO)

write_basic_package_version_file(ctbrowser-config-version.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(EXPORT ${PROJECT_NAME}-targets
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
  NAMESPACE ${PROJECT_NAME}::)
install(
  FILES
    "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake"
    "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

# One shared PCH: the three bricks' umbrellas compile once (seconds) -
# the anchor owns the PCH and every test/example REUSE_FROMs it.
if(CTBROWSER_BUILD_V1 AND CTBROWSER_PCH AND (CTBROWSER_BUILD_TESTS OR CTBROWSER_BUILD_EXAMPLES))
  file(WRITE "${CMAKE_BINARY_DIR}/ctbrowser-pch-anchor.cpp"
    "int ctbrowser_pch_anchor_symbol = 0;\n")
  add_library(ctbrowser-pch-anchor OBJECT "${CMAKE_BINARY_DIR}/ctbrowser-pch-anchor.cpp")
  target_link_libraries(ctbrowser-pch-anchor PRIVATE ctbrowser::ctbrowser)
  # the anchor MUST compile with the same flags as every REUSE_FROM consumer
  target_compile_options(ctbrowser-pch-anchor PRIVATE ${CTBROWSER_WARNING_OPTIONS})
  target_precompile_headers(ctbrowser-pch-anchor PRIVATE
    "${PROJECT_SOURCE_DIR}/include/ctbrowser.hpp")
endif()

# SDL3 and its satellite libraries: the satellites are optional and
# light up richer paths (SDL3_image sprites, SDL3_mixer sound,
# SDL3_ttf TrueType text); the built-in fallbacks serve without them
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

# *.html -> examples/*.inc: the REAL .html file becomes a raw-string
# literal that #include's into the page<...> template argument
# (consumed by tests/pong.cpp and examples/{pong,fetchboard,space-invaders}.cpp)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  foreach(html-page pong fetchboard space-invaders)
    add_custom_command(
      OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/examples/${html-page}.inc"
      COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/html-to-inc.py"
              "${CMAKE_CURRENT_SOURCE_DIR}/examples/${html-page}.html"
              "${CMAKE_CURRENT_SOURCE_DIR}/examples/${html-page}.inc"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/${html-page}.html"
              "${CMAKE_CURRENT_SOURCE_DIR}/tools/html-to-inc.py"
      COMMENT "Wrapping examples/${html-page}.html as a raw-string literal")
    add_custom_target(ctbrowser-${html-page}-inc
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/${html-page}.inc")
  endforeach()
endif()

# -DCTBROWSER_EXAMPLES_FETCH=ON: authorize fetchboard.html's compile-
# time fetches (the constant evaluator performs the HTTP GETs and
# embeds the responses). OFF = never touch the network; the page's
# catch branches explain what is missing.
option(CTBROWSER_EXAMPLES_FETCH "compile-time-fetch the example pages' URLs" OFF)

# v1's tests compile the whole compile-time stack, so they need the fork
if(CTBROWSER_BUILD_V1 AND CTBROWSER_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# ctbrowser 2.0 - the concurrent, GPU-composited rewrite. Builds alongside v1
# until the staging plan retires v1, and is entirely self-contained: v1's
# targets, PCH and tests are untouched by it. Needs CMake >= 3.28 for C++
# modules, so it is opt-out on older toolchains rather than a hard failure.
option(CTBROWSER_BUILD_V2 "Build the ctbrowser 2.0 engine (needs CMake >= 3.28)" ON)
if(CTBROWSER_BUILD_V2)
  if(CMAKE_VERSION VERSION_LESS 3.28)
    message(STATUS "ctbrowser: CMake < 3.28 - skipping v2 (no C++ module support)")
  else()
    add_subdirectory(src)
  endif()
endif()

if(CTBROWSER_BUILD_V1 AND CTBROWSER_BUILD_EXAMPLES)
  if(SDL3_FOUND)
    add_subdirectory(examples)
  else()
    message(STATUS "ctbrowser: SDL3 not found - skipping examples (install SDL3 or set SDL3_DIR)")
  endif()
endif()

set(PKG_CONFIG_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/packaging/pkgconfig.pc.in" "${PKG_CONFIG_FILE_NAME}" @ONLY)
install(FILES "${PKG_CONFIG_FILE_NAME}"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
