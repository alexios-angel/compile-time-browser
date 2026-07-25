# ctbrowser 2.0 - the concurrent, GPU-composited engine.
#
# v2 builds ALONGSIDE v1 until the staging plan retires v1, so this subtree is
# self-contained: it does not touch the v1 interface target, its PCH, or its
# tests. v1's 33 tests must keep passing throughout the rewrite.
#
# Modules need a newer CMake than the top level asks for, and they need the
# scanner on, so the requirement is raised here rather than repo-wide.
cmake_minimum_required(VERSION 3.28)
include(GNUInstallDirs)

if(CMAKE_VERSION VERSION_LESS 3.28)
  message(FATAL_ERROR "ctbrowser v2 needs CMake >= 3.28 for C++ module support")
endif()

# CONFIG mode: FindBoost was removed in CMake 4 (policy CMP0167), and Boost has
# shipped its own package config for years. Header-only usage only - see the
# plan's dependency policy: compiled Boost would mean cross-compiling Boost
# itself for the llvm-mingw Windows presets.
#
# CROSS BUILDS have no BoostConfig for the target and do not need one: what v2
# links is Boost::headers, so an isolated include directory (the toolchain finds
# it, the same way it finds GLM's) is the entire dependency.
if(CTBROWSER_BOOST_INCLUDE_DIR)
  if(NOT TARGET Boost::headers)
    add_library(Boost::headers INTERFACE IMPORTED)
    set_target_properties(Boost::headers PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${CTBROWSER_BOOST_INCLUDE_DIR}")
  endif()
  message(STATUS "ctbrowser v2: header-only Boost from ${CTBROWSER_BOOST_INCLUDE_DIR}")
else()
  find_package(Boost 1.80 REQUIRED CONFIG)
endif()

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
      core/containers.cppm
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
      dom/tokenizer.cppm
      dom/treebuilder.cppm
      dom/html.cppm)
target_compile_features(ctbrowser-dom PUBLIC cxx_std_23)
target_compile_options(ctbrowser-dom PRIVATE ${CTBROWSER_V2_WARNINGS})
target_include_directories(ctbrowser-dom PRIVATE)
target_include_directories(ctbrowser-dom PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/dom")
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
      script/vm.cppm
      script/builtins.cppm)
target_compile_features(ctbrowser-script PUBLIC cxx_std_23)
target_compile_options(ctbrowser-script PRIVATE ${CTBROWSER_V2_WARNINGS})
target_include_directories(ctbrowser-script PRIVATE
  "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/include"
  "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/external/compile-time-containers/include")
target_link_libraries(ctbrowser-script PUBLIC ctbrowser-core Boost::headers)
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
      shell/assets.cppm
      shell/images.cppm
      shell/net.cppm
      shell/canvas.cppm
      shell/forms.cppm
      shell/bindings.cppm
      shell/browser.cppm)
target_compile_features(ctbrowser-shell PUBLIC cxx_std_23)
target_compile_options(ctbrowser-shell PRIVATE ${CTBROWSER_V2_WARNINGS})
target_link_libraries(ctbrowser-shell PUBLIC ctbrowser-core ctbrowser-dom ctbrowser-style
                                             ctbrowser-layout ctbrowser-paint ctbrowser-raster
                                             ctbrowser-script Boost::headers)

# Asio comes from Boost by default - Boost::headers is already a dependency of
# three v2 targets and is header-only. A build that would rather not have Boost
# at all can point at STANDALONE Asio instead; the module supports both.
find_path(CTBROWSER_STANDALONE_ASIO_INCLUDE asio.hpp)
option(CTBROWSER_ASIO_STANDALONE "Use standalone Asio instead of Boost.Asio"
       ${CTBROWSER_PREFER_STANDALONE_ASIO})
if(CTBROWSER_ASIO_STANDALONE AND CTBROWSER_STANDALONE_ASIO_INCLUDE)
  target_include_directories(ctbrowser-shell PUBLIC ${CTBROWSER_STANDALONE_ASIO_INCLUDE})
  target_compile_definitions(ctbrowser-shell PUBLIC CTBROWSER_ASIO_STANDALONE=1 ASIO_STANDALONE=1)
  message(STATUS "ctbrowser v2: networking on standalone Asio")
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
  message(STATUS "ctbrowser v2: https:// enabled (OpenSSL ${OPENSSL_VERSION})")
else()
  target_compile_definitions(ctbrowser-shell PUBLIC CTBROWSER_WITH_TLS=0)
  message(STATUS "ctbrowser v2: no OpenSSL - fetch() does http:// only")
endif()
set_target_properties(ctbrowser-shell PROPERTIES CXX_SCAN_FOR_MODULES ON)

add_library(ctbrowser::shell ALIAS ctbrowser-shell)

# --- ctbrowser.app -------------------------------------------------------
# The window and the event loop, and the only module that knows SDL exists.
# ALWAYS built. SDL3 is a compile definition, not a gate: without it the module
# still provides run_app() over the headless backend, which is what makes "SDL3
# is optional" true of the BUILD and not just of the engine.
add_library(ctbrowser-app)
target_sources(ctbrowser-app
  PUBLIC
    FILE_SET CXX_MODULES FILES shell/app.cppm)
target_compile_features(ctbrowser-app PUBLIC cxx_std_23)
target_compile_options(ctbrowser-app PRIVATE ${CTBROWSER_V2_WARNINGS})
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
    message(STATUS "ctbrowser v2: SDL3_image found - <img> reads PNG/JPEG too")
  else()
    target_compile_definitions(ctbrowser-app PRIVATE CTBROWSER_WITH_IMAGE=0)
    message(STATUS "ctbrowser v2: no SDL3_image - <img> reads BMP only")
  endif()
else()
  target_compile_definitions(ctbrowser-app PRIVATE CTBROWSER_WITH_SDL3=0 CTBROWSER_WITH_IMAGE=0)
  message(STATUS "ctbrowser: SDL3 not found - v2 builds headless (run_app still works)")
endif()
set_target_properties(ctbrowser-app PROPERTIES CXX_SCAN_FOR_MODULES ON)
add_library(ctbrowser::app ALIAS ctbrowser-app)

# --- ctbrowser (umbrella) ------------------------------------------------
# What an application imports and links. One of each.
add_library(ctbrowser-v2)
target_sources(ctbrowser-v2
  PUBLIC
    FILE_SET CXX_MODULES FILES ctbrowser.cppm)
target_compile_features(ctbrowser-v2 PUBLIC cxx_std_23)
target_compile_options(ctbrowser-v2 PRIVATE ${CTBROWSER_V2_WARNINGS})
target_link_libraries(ctbrowser-v2 PUBLIC
  ctbrowser-core ctbrowser-dom ctbrowser-script ctbrowser-style ctbrowser-layout
  ctbrowser-paint ctbrowser-raster ctbrowser-shell ctbrowser-app)
set_target_properties(ctbrowser-v2 PROPERTIES CXX_SCAN_FOR_MODULES ON)
add_library(ctbrowser::v2 ALIAS ctbrowser-v2)

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
                                             ${CTBROWSER_SDL3_TARGET})
  set_target_properties(ctbrowser-gpu PROPERTIES CXX_SCAN_FOR_MODULES ON)
  add_library(ctbrowser::gpu ALIAS ctbrowser-gpu)
endif()

# --- installing v2 -------------------------------------------------------
# C++ modules install as a FILE_SET, and consuming them from an installed tree
# needs CXX_MODULES_DIRECTORY on the export. Without this the only way to use
# v2 was add_subdirectory() of this repo - every install() in the project was
# v1's, so `find_package(ctbrowser)` handed back a header-only interface target
# and none of the engine.
set(CTBROWSER_V2_TARGETS
  ctbrowser-core ctbrowser-dom ctbrowser-script ctbrowser-style ctbrowser-layout
  ctbrowser-paint ctbrowser-raster ctbrowser-shell ctbrowser-app ctbrowser-v2)
if(TARGET ctbrowser-gpu)
  list(APPEND CTBROWSER_V2_TARGETS ctbrowser-gpu)
endif()

install(TARGETS ${CTBROWSER_V2_TARGETS}
  EXPORT ctbrowser-v2-targets
  FILE_SET CXX_MODULES DESTINATION "${CMAKE_INSTALL_LIBDIR}/ctbrowser/modules"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")

install(EXPORT ctbrowser-v2-targets
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ctbrowser-v2"
  NAMESPACE ctbrowser::
  CXX_MODULES_DIRECTORY .)

# The alias an application links. `ctbrowser::v2` is the build-tree name;
# this is what survives an install.
# Whatever the exported targets name in their link interface, the consumer has
# to be able to resolve. SDL3 is conditional because the app module links it
# only when it was found - a headless install must not demand it back.
set(CTBROWSER_V2_CONFIG_DEPS "find_dependency(Boost 1.80 CONFIG)\nfind_dependency(Threads)")
if(SDL3_FOUND)
  string(APPEND CTBROWSER_V2_CONFIG_DEPS "\nfind_dependency(SDL3 CONFIG)")
endif()
file(WRITE "${PROJECT_BINARY_DIR}/ctbrowser-v2-config.cmake"
"include(CMakeFindDependencyMacro)
${CTBROWSER_V2_CONFIG_DEPS}
include(\"\${CMAKE_CURRENT_LIST_DIR}/ctbrowser-v2-targets.cmake\")
")
install(FILES "${PROJECT_BINARY_DIR}/ctbrowser-v2-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ctbrowser-v2")

add_subdirectory(../examples-v2 "${CMAKE_CURRENT_BINARY_DIR}/examples-v2")

if(CTBROWSER_BUILD_TESTS)
  add_subdirectory(../tests-v2 "${CMAKE_CURRENT_BINARY_DIR}/tests-v2")
endif()
