# ctbrowser.app - the window and the event loop; the only place that knows SDL exists.
#
# ALWAYS built. SDL3 is a compile definition, not a gate: without it the module
# still provides run_app() over the headless backend, which is what makes "SDL3
# is optional" true of the BUILD and not just of the engine.
add_library(ctbrowser-app)
ctbrowser_subsystem_sources(ctbrowser-app app)
target_compile_features(ctbrowser-app PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-app)
target_link_libraries(ctbrowser-app PUBLIC ctbrowser-core ctbrowser-paint ctbrowser-raster
                                           ctbrowser-shell)
if(SDL3_FOUND)
  target_compile_definitions(ctbrowser-app PRIVATE CTBROWSER_WITH_SDL3=1)
  target_link_libraries(ctbrowser-app PUBLIC ${CTBROWSER_SDL3_TARGET})
  # PNG/JPEG/WebP, if the satellite library is there (found by the root
  # CMakeLists). BMP is decoded by the engine itself, so a build without this
  # still shows images.
  if(SDL3_image_FOUND)
    target_compile_definitions(ctbrowser-app PRIVATE CTBROWSER_WITH_IMAGE=1)
    target_link_libraries(ctbrowser-app PUBLIC ${CTBROWSER_SDL3_IMAGE_TARGET})
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
