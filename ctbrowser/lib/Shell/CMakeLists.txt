# ctbrowser.shell - the engine assembled into something that loads a page.
#
# browser/ is the assembly itself, bindings/ the web platform bound to the VM,
# net/ where bytes come from, image/ the codecs (each behind a two-function
# header, none of them SDL) and page/ what a PAGE can do. Every subdirectory's
# internal.hpp is private to it; bindings/names.hpp is the one header two of
# them share.
add_library(ctbrowser-shell)
ctbrowser_subsystem_sources(ctbrowser-shell shell)

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
# beside it. The SOURCE tree spells that directory resources/fonts/; a SHIPPED
# application spells it fonts/, beside the exe. See CTBROWSER_FONT_PATH.
# beside it; leave it off and it reads the directory, as it always has.
option(CTBROWSER_EMBED_FONTS "Bake the vendored OFL faces into the binary" OFF)
if(CTBROWSER_EMBED_FONTS)
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_FLAGS "--embed-dir=${PROJECT_SOURCE_DIR}/resources/fonts")
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
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/embedded_fonts.cpp" PROPERTIES COMPILE_OPTIONS
      "--embed-dir=${PROJECT_SOURCE_DIR}/resources/fonts")
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
