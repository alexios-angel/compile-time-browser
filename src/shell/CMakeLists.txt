# ctbrowser.shell - the engine assembled into something that loads a page.
#
# Sources are relative to this directory; headers are named in full under
# ${CTBROWSER_INCLUDE_DIR}. Adding a file to this subsystem now touches this
# file and nothing else - which is why the 897-line src/CMakeLists.txt was
# split on 2026-08-09. Dependencies are found in cmake/dependencies.cmake.

add_library(ctbrowser-shell)
target_sources(ctbrowser-shell
  PRIVATE
    # the assembly itself
    browser.cpp
    # The web platform bound to the VM. bindings.cpp was 3,926 lines and
    # webgl_bindings.cpp another 1,431, filed under page/ although it is the
    # same job as its five siblings here.
    bindings/document.cpp
    bindings/element.cpp
    bindings/events.cpp
    # getComputedStyle: its own file because it is property dispatch over three
    # sources, and element.cpp is already 1,100 lines.
    bindings/computed_style.cpp
    bindings/window.cpp
    bindings/canvas.cpp
    bindings/resources.cpp
    bindings/webgl.cpp
    embedded_fonts.cpp
    # resources: where bytes come from. net.cpp was net_curl.cpp until
    # 2026-08-09; the suffix implied a choice of transport, and the Asio one has
    # been gone for a while.
    net/net.cpp
    net/url.cpp
    # image codecs, each behind a two-function header, none of them SDL
    image/png.cpp
    image/jpeg.cpp
    image/images.cpp
    # what a PAGE can do
    page/canvas.cpp
    page/forms.cpp
    page/svg_cache.cpp
    page/webgl.cpp
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
    set_source_files_properties(embedded_fonts.cpp PROPERTIES COMPILE_OPTIONS
      "--embed-dir=${PROJECT_SOURCE_DIR}/fonts")
    message(STATUS "ctbrowser: fonts embedded in the binary (+5.2 MB)")
  else()
    message(STATUS "ctbrowser: no #embed here; fonts load from fonts/ at runtime")
  endif()
endif()
target_link_libraries(ctbrowser-shell PUBLIC ctbrowser-core ctbrowser-dom ctbrowser-style
                                             ctbrowser-layout ctbrowser-paint ctbrowser-raster
                                             ctbrowser-script Boost::headers)
# PRIVATE: boost/url is 1.2 MB of headers and url.cpp is the only place it
# appears. Nothing that links the engine should have to parse it - the same rule
# net.hpp states for curl.h.
target_link_libraries(ctbrowser-shell PRIVATE Boost::url)
# PRIVATE for the same reason: curl.h is the transport's business and
# net_curl.cpp is the only place it appears.
target_link_libraries(ctbrowser-shell PRIVATE CURL::libcurl)

# --- PNG AND JPEG, WITHOUT SDL -------------------------------------------
# Both found in cmake/dependencies.cmake, which records why the engine decodes
# them itself rather than through SDL3_image: a format that only works when a
# satellite happened to be found is one tests/ cannot assert on and no golden
# can compare, which is exactly how PNG stayed broken until Phaser arrived.
#
# PRIVATE, like curl and boost/url: png.h is png.cpp's business.
target_link_libraries(ctbrowser-shell PRIVATE PNG::PNG)

# TurboJPEG arrives one of two ways and the exported NAME differs, which is
# the only reason this is a branch: the pkg-config route yields an imported
# TARGET a consumer has to be able to recreate, the fallback a resolved PATH
# that needs nothing found again. cmake/dependencies.cmake does the looking.
if(TARGET PkgConfig::CTB_TURBOJPEG)
  target_link_libraries(ctbrowser-shell PRIVATE PkgConfig::CTB_TURBOJPEG)
  message(STATUS "ctbrowser: JPEG through libjpeg-turbo ${CTB_TURBOJPEG_VERSION}")
else()
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
