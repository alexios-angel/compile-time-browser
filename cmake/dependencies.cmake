# Everything the engine LOOKS FOR, in one place.
#
# Split out of src/CMakeLists.txt on 2026-08-09, when that file was 897 lines
# doing two jobs: finding dependencies and defining ten targets. Discovery all
# happens here, first; each src/<subsystem>/CMakeLists.txt then just USES what
# was found.
#
# THE ORDER IS THE ORDER IT WAS IN, deliberately. Several of these blocks say
# why they are shaped the way they are - which hints come first, which failure
# mode a find_package would hide - and reordering them to look tidier is how
# that reasoning gets quietly invalidated.
#
# Target-specific configuration did NOT move here. ANGLE, plutosvg and SDL3_ttf
# are link lines and compile definitions on ctbrowser-raster, so they live with
# it; the same goes for GMP on ctbrowser-script.

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

find_package(Threads REQUIRED)

# THE ALLOCATOR. mimalloc backs operator new/delete (core/allocator.cpp),
# because allocator traffic is ~4.8% of a Phaser frame and a drop-in allocator
# is a link-line change that cannot alter results - see docs/performance.md,
# which also records why jemalloc lost (a tie on wall clock, and no
# cross-build).
#
# REQUIRED once found, and PUBLIC on purpose: an executable that links the
# engine but not mimalloc would resolve operator new from libstdc++ instead and
# silently run on the system allocator. tests/unit/core_basics asks
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

# --- PNG, WITHOUT SDL ----------------------------------------------------
# The engine decoded BMP itself and got every other format from a hook the
# application layer fills in from SDL3_image. That split held until `tests/`
# needed a PNG: the suite is SDL-FREE by an invariant `tests/lint/api_surface`
# lints for, so every test saw a PNG as a zero-sized image and nothing said so.
# Phaser's texture manager loads three base64 PNGs during boot and waits for
# all three, which is how it was found.
#
# LIBPNG, the format's reference implementation, and zlib with it. PRIVATE for
# the same reason as curl and boost/url: png.h is png.cpp's business.
find_package(PNG REQUIRED)

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

# PNG/JPEG/WebP for <img> through SDL3's satellite, when SDL3 itself was found.
# Asked here rather than inside src/app/ because CTBROWSER_CONFIG_DEPS at the
# bottom of src/CMakeLists.txt has to know the answer too.
if(SDL3_FOUND)
  find_package(SDL3_image QUIET)
endif()

# GMP, for the OPTIONAL BigInt backend. The root CMakeLists says why this is
# opt-in rather than on-when-found: GMP is LGPL and this engine links
# statically, and it measured SLOWER at the width a JavaScript BigInt has.
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
endif()
