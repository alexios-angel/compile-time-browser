# Every find_package/find_library the engine needs, in one place. Discovery
# happens here, first; each lib/<Subsystem>/CMakeLists.txt then uses what was
# found.
#
# Keep the blocks in this order. Several comments below depend on it (which
# hints are searched first, which failure a find_package would hide).
#
# Target-specific configuration (ANGLE, plutosvg, SDL3_ttf) lives with
# ctbrowser-raster, not here.

# CONFIG mode: FindBoost was removed in CMake 4 (policy CMP0167), and Boost has
# shipped its own package config for years.
#
# Mostly header-only, because every compiled Boost library must also be
# cross-compiled for the llvm-mingw Windows presets. Two compiled libraries are
# linked on purpose:
#
# - Boost.URL, which is compiled-only since 1.87 (`boost/url/src.hpp` now fails
#   with "src.hpp is discontinued"). See NOTICE.
# - Boost.ProgramOptions, which only the tools and the test programs that take
#   arguments link, through the ctbrowser-options target in tools/
#   (tools/support/options.hpp). The engine does not link it.
#
# Boost.Context, Coroutine and Fiber stay excluded: they break the cross build,
# and nothing here needs them.
#
# Cross builds have no BoostConfig for the target: the headers come from an
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
  find_library(CTBROWSER_BOOST_PROGRAM_OPTIONS_LIBRARY
    NAMES boost_program_options libboost_program_options)
  if(NOT CTBROWSER_BOOST_PROGRAM_OPTIONS_LIBRARY)
    message(FATAL_ERROR
      "ctbrowser: no libboost_program_options for this target, and the tools and the tests "
      "that take arguments need it. tools/mingw/build-boost-mingw.sh builds it into the mingw "
      "sysroot.")
  endif()
  if(NOT TARGET Boost::program_options)
    add_library(Boost::program_options UNKNOWN IMPORTED)
    set_target_properties(Boost::program_options PROPERTIES
      IMPORTED_LOCATION "${CTBROWSER_BOOST_PROGRAM_OPTIONS_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${CTBROWSER_BOOST_INCLUDE_DIR}")
  endif()
  # The cross path has no version to check, so it checks for the header that
  # sets the 1.88 minimum instead. Without this, a sysroot Boost older than 1.88
  # fails inside program_image/write.cpp with "boost/hash2/xxhash.hpp: No such
  # file", which names a file rather than a version.
  if(NOT EXISTS "${CTBROWSER_BOOST_INCLUDE_DIR}/boost/hash2/xxhash.hpp")
    message(FATAL_ERROR
      "ctbrowser: this Boost has no Boost.Hash2 (added in 1.88). The script image's source "
      "hash is boost::hash2::xxhash_64 - see ctbrowser/lib/Script/program_image/write.cpp for why "
      "it is not written out by hand. Point CTBROWSER_BOOST_INCLUDE_DIR at 1.88 or newer.")
  endif()
  message(STATUS "ctbrowser: Boost headers ${CTBROWSER_BOOST_INCLUDE_DIR}, "
                 "url ${CTBROWSER_BOOST_URL_LIBRARY}")
else()
  # 1.88 is the first release with Boost.Hash2, whose xxhash_64 is the script
  # image's source hash.
  find_package(Boost 1.88 REQUIRED CONFIG COMPONENTS url program_options)
endif()
# Global, because this file runs in lib/'s scope and an imported target is
# visible only there and below, but ctbrowser-options (in tools/), the tests and
# ctcompile all link it.
set_target_properties(Boost::program_options PROPERTIES IMPORTED_GLOBAL TRUE)

find_package(Threads REQUIRED)

# The allocator. mimalloc backs operator new/delete (lib/Core/allocator.cpp)
# because allocator traffic is ~4.8% of a Phaser frame, and a drop-in allocator
# is a link-line change that cannot alter results. docs/performance.md also
# records why jemalloc lost (a tie on wall clock, and no cross build).
#
# Required when CTBROWSER_USE_MIMALLOC is ON, and linked PUBLIC on purpose: an
# executable that links the engine but not mimalloc would resolve operator new
# from libstdc++ and silently run on the system allocator.
# unittests/unit/core/basics asks ctbrowser::allocator_name() so that mistake
# fails a test. The Windows sysroot gets mimalloc from
# tools/mingw/build-libs-mingw.sh.
#
# Found by path, not find_package: mimalloc's exported config advertises a
# `mimalloc-static` target even on distributions that ship only the shared
# library, which then fails at link time. Searching for the file finds what is
# actually installed. The static library is preferred where both exist, because
# it makes a Windows .exe self-contained.
#
# Brew's prefixes are hinted explicitly and searched first, so brew's copy wins
# over a distribution's: apt ships v2 and this tree pins v3, two different
# allocators behind the same header name.
find_library(CTBROWSER_MIMALLOC NAMES mimalloc-static mimalloc
  HINTS ${CTBROWSER_BREW_HINTS} PATH_SUFFIXES lib)
find_path(CTBROWSER_MIMALLOC_INCLUDE mimalloc.h
  HINTS ${CTBROWSER_BREW_HINTS} PATH_SUFFIXES include)
if(CTBROWSER_USE_MIMALLOC AND (NOT CTBROWSER_MIMALLOC OR NOT CTBROWSER_MIMALLOC_INCLUDE))
  message(FATAL_ERROR
    "mimalloc not found. Linux: brew install mimalloc. "
    "Windows cross: run tools/mingw/build-libs-mingw.sh. "
    "Or configure with -DCTBROWSER_USE_MIMALLOC=OFF to use the system allocator.")
endif()

# simdutf. It backs base64_decode's fast path (42x on well-formed input). The
# hand-written loop in lib/Core/algorithms.cpp still handles anything simdutf's
# strict mode refuses, so the lenient behaviour is unchanged. docs/performance.md
# records the differential test that ruled out using simdutf alone.
find_library(CTBROWSER_SIMDUTF NAMES simdutf HINTS ${CTBROWSER_BREW_HINTS} PATH_SUFFIXES lib)
find_path(CTBROWSER_SIMDUTF_INCLUDE simdutf.h HINTS ${CTBROWSER_BREW_HINTS} PATH_SUFFIXES include)
if(NOT CTBROWSER_SIMDUTF OR NOT CTBROWSER_SIMDUTF_INCLUDE)
  message(FATAL_ERROR
    "simdutf not found. Linux: brew install simdutf. "
    "Windows cross: run tools/mingw/build-libs-mingw.sh")
endif()

# --- HTTP transport -------------------------------------------------------
#
# libcurl is the only transport. It provides HTTP framing (request lines,
# chunked decoding, redirects and so on) and TLS, using SChannel on Windows, so
# the cross build has https without OpenSSL. See docs/build.md.
#
# Required rather than optional: an engine that silently fell back to a
# transport with no TLS would be worse than one that does not configure.
find_package(CURL REQUIRED)

# --- PNG, without SDL ----------------------------------------------------
# PNG is decoded inside the engine, not through SDL3_image, because the test
# suite is SDL-free (test/lint/api_surface enforces this). A PNG decoded only
# when SDL was present would be a zero-sized image in every test.
#
# libpng, the format's reference implementation, brings zlib with it. Linked
# PRIVATE, like curl and Boost.URL: only png.cpp includes png.h.
find_package(PNG REQUIRED)

# --- JPEG, without SDL ---------------------------------------------------
# Decoded inside the engine for the same reason as PNG: a format that decodes
# only when SDL3_image is found is one no test can assert on and no golden can
# compare.
#
# libjpeg-turbo through its TurboJPEG API (libjpeg's decoder with SIMD, the one
# browsers and Android ship). Linked PRIVATE: only jpeg.cpp includes
# turbojpeg.h. No CMake config module is guaranteed for it, so pkg-config is
# asked first, with a plain library search as the fallback the cross build
# uses.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(CTB_TURBOJPEG QUIET IMPORTED_TARGET libturbojpeg)
endif()
# The fallback is only searched for when pkg-config did not answer, so a box
# with both does not pay for the second lookup.
if(NOT TARGET PkgConfig::CTB_TURBOJPEG)
  find_path(CTBROWSER_TURBOJPEG_INCLUDE_DIR turbojpeg.h)
  find_library(CTBROWSER_TURBOJPEG_LIBRARY NAMES turbojpeg libturbojpeg)
  if(NOT CTBROWSER_TURBOJPEG_INCLUDE_DIR OR NOT CTBROWSER_TURBOJPEG_LIBRARY)
    message(FATAL_ERROR
      "ctbrowser: no libjpeg-turbo for this target. It decodes JPEG in the "
      "SDL-free engine, so tests can assert on one. Install it (brew: "
      "jpeg-turbo) or build it into the mingw sysroot for a cross build.")
  endif()
endif()


