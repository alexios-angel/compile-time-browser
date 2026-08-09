# ctbrowser.raster - display list -> pixels, in tiles across the pool.
#
# Sources are relative to this directory; headers are named in full under
# ${CTBROWSER_INCLUDE_DIR}. Adding a file to this subsystem now touches this
# file and nothing else - which is why the 897-line src/CMakeLists.txt was
# split on 2026-08-09. Dependencies are found in cmake/dependencies.cmake.

# --- ctbrowser.raster ----------------------------------------------------
# Turns display lists into pixels. Knows nothing about the DOM, style or
# layout - only about paint's recorded commands, which is what lets a second
# backend (SDL3 GPU, stage 6) drop in beside the software one.
add_library(ctbrowser-raster)
target_sources(ctbrowser-raster
  PRIVATE
    gl.cpp
    draw.cpp
    ttf.cpp
    svg.cpp
    software.cpp
    renderer.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/raster.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/gl.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/surface.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/draw.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/ttf.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/svg.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/font8x8.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/tile.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/backend.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/software.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/renderer.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/compositor.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/raster/pipeline.hpp")
target_compile_features(ctbrowser-raster PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-raster)
target_link_libraries(ctbrowser-raster PUBLIC ctbrowser-core ctbrowser-paint)

# ANGLE, from the pinned release tools/fetch-angle.sh downloads. PRIVATE, and
# nothing that includes it inherits them and tests/api_surface stays satisfied.
#
# The definition is PUBLIC because a consumer needs to know whether the device
# can exist - `gles::available()` answers at run time, but a test still has to
# compile either way.
if(CTBROWSER_WITH_ANGLE)
  if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL Windows)
    set(angle-dir "${PROJECT_SOURCE_DIR}/third_party/angle/windows-x64")
    set(angle-libs "${angle-dir}/libEGL.dll.lib" "${angle-dir}/libGLESv2.dll.lib")
  else()
    set(angle-dir "${PROJECT_SOURCE_DIR}/third_party/angle/linux-x86_64")
    set(angle-libs "${angle-dir}/libEGL.so" "${angle-dir}/libGLESv2.so")
  endif()
  if(NOT EXISTS "${angle-dir}")
    message(FATAL_ERROR
      "CTBROWSER_WITH_ANGLE is ON but ${angle-dir} is missing - run tools/fetch-angle.sh")
  endif()
  target_include_directories(ctbrowser-raster PRIVATE "${angle-dir}/include")
  target_link_libraries(ctbrowser-raster PRIVATE ${angle-libs})
  target_compile_definitions(ctbrowser-raster PUBLIC CTBROWSER_WITH_ANGLE=1)
  # THE LIBRARIES HAVE TO BE FOUND AT RUN TIME TOO, and they are not in any
  # system path: an rpath here is what stops every consumer needing
  # LD_LIBRARY_PATH set by hand.
  if(NOT WIN32)
    set_target_properties(ctbrowser-raster PROPERTIES
      BUILD_RPATH "${angle-dir}" INSTALL_RPATH "${angle-dir}")
  endif()
  message(STATUS "ctbrowser: ANGLE - a real GLES device is available")
endif()
# Real fonts, through SDL3_ttf. THE ONE PLACE THE ENGINE LINKS SDL, and a
# deliberate exception rather than an oversight - see the header of
# ttf.cppm and the note in tests/api_surface. TTF_Init needs no video
# subsystem, so real text stays testable with no display, which is what makes
# the exception safe.
#
# OPTIONAL, like everything else here: without it the raster layer still builds
# and still draws font8x8, which keeps the goldens reproducible on a machine
# with no font libraries at all.
if(SDL3_FOUND AND SDL3_ttf_FOUND)
  ctbrowser_pick_sdl_target(ttf_target ttf_static SDL3_ttf::SDL3_ttf)
  ctbrowser_pick_sdl_target(sdl_target sdl_static SDL3::SDL3)
  target_link_libraries(ctbrowser-raster PUBLIC ${ttf_target} ${sdl_target})
  target_compile_definitions(ctbrowser-raster PUBLIC CTBROWSER_WITH_TTF=1)
  message(STATUS "ctbrowser: SDL3_ttf - real fonts available")
else()
  target_compile_definitions(ctbrowser-raster PUBLIC CTBROWSER_WITH_TTF=0)
  message(STATUS "ctbrowser: no SDL3_ttf - text renders with font8x8 only")
endif()

# SVG, through plutosvg. PRIVATE link and PUBLIC definition, deliberately split:
# only svg.cpp includes a plutosvg header, so no consumer needs the
# include path, but raster/svg.hpp's svg_available() is constexpr and every
# caller has to agree with it about what this build can do.
if(plutosvg_FOUND)
  target_link_libraries(ctbrowser-raster PRIVATE plutosvg::plutosvg)
  target_compile_definitions(ctbrowser-raster PUBLIC CTBROWSER_WITH_SVG=1)
  message(STATUS "ctbrowser: plutosvg ${plutosvg_VERSION} - SVG renders")

  # VERSION PARITY IS LOAD-BEARING, so it is checked rather than hoped for.
  # tests/golden/svg.ppm is ONE image compared byte-for-byte on both Linux and
  # Windows, and the Windows sysroot pins whatever ../llvm-mingw/build-sdl3.sh
  # fetched. If a host's plutosvg drifts ahead of that, the golden moves on one
  # platform and not the other - which is exactly how the renders diverged over
  # a HarfBuzz version once already (docs/raster.md).
  set(CTBROWSER_PLUTOSVG_PINNED 0.0.8)
  if(NOT plutosvg_VERSION VERSION_EQUAL CTBROWSER_PLUTOSVG_PINNED)
    message(WARNING
      "ctbrowser: plutosvg ${plutosvg_VERSION}, expected ${CTBROWSER_PLUTOSVG_PINNED}. "
      "The SVG golden may not compare across platforms; regenerate with REGOLDEN=1 "
      "and check the Windows render still matches.")
  endif()
else()
  target_compile_definitions(ctbrowser-raster PUBLIC CTBROWSER_WITH_SVG=0)
  message(STATUS "ctbrowser: no plutosvg - <svg> and .svg images do not draw")
endif()

add_library(ctbrowser::raster ALIAS ctbrowser-raster)
