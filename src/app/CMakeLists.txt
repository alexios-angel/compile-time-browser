# ctbrowser.app - the window and the event loop; the only place that knows SDL exists.
#
# Sources are relative to this directory; headers are named in full under
# ${CTBROWSER_INCLUDE_DIR}. Adding a file to this subsystem now touches this
# file and nothing else - which is why the 897-line src/CMakeLists.txt was
# split on 2026-08-09. Dependencies are found in cmake/dependencies.cmake.

# --- ctbrowser.app -------------------------------------------------------
# The window and the event loop, and the only module that knows SDL exists.
# ALWAYS built. SDL3 is a compile definition, not a gate: without it the module
# still provides run_app() over the headless backend, which is what makes "SDL3
# is optional" true of the BUILD and not just of the engine.
add_library(ctbrowser-app)
target_sources(ctbrowser-app
  PRIVATE
    app.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/app/app.hpp")
target_compile_features(ctbrowser-app PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-app)
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
    message(STATUS "ctbrowser: SDL3_image found - <img> reads PNG/JPEG too")
  else()
    target_compile_definitions(ctbrowser-app PRIVATE CTBROWSER_WITH_IMAGE=0)
    message(STATUS "ctbrowser: no SDL3_image - <img> reads BMP only")
  endif()
else()
  target_compile_definitions(ctbrowser-app PRIVATE CTBROWSER_WITH_SDL3=0 CTBROWSER_WITH_IMAGE=0)
  message(STATUS "ctbrowser: SDL3 not found - builds headless (run_app still works)")
endif()
add_library(ctbrowser::app ALIAS ctbrowser-app)
