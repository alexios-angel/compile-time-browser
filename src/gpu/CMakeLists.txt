# ctbrowser.gpu - SDL_GPUDevice composition, and the fallback when there is no adapter.
#
# Sources are relative to this directory; headers are named in full under
# ${CTBROWSER_INCLUDE_DIR}. Adding a file to this subsystem now touches this
# file and nothing else - which is why the 897-line src/CMakeLists.txt was
# split on 2026-08-09. Dependencies are found in cmake/dependencies.cmake.

# --- ctbrowser.gpu -------------------------------------------------------
# OPTIONAL by construction. Without SDL3 the engine still builds and still
# renders - through the software backend - because raster::renderer is a
# runtime seam rather than a compile-time one. That is the same reason a
# machine with no GPU still gets a browser.
if(SDL3_FOUND)
  add_library(ctbrowser-gpu)
  target_sources(ctbrowser-gpu
    PRIVATE
      device.cpp
    PUBLIC
      FILE_SET HEADERS
        BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
        FILES
          "${CTBROWSER_INCLUDE_DIR}/ctbrowser/gpu/gpu.hpp"
          "${CTBROWSER_INCLUDE_DIR}/ctbrowser/gpu/shaders/tile_spv.hpp"
          "${CTBROWSER_INCLUDE_DIR}/ctbrowser/gpu/device.hpp"
          "${CTBROWSER_INCLUDE_DIR}/ctbrowser/gpu/select.hpp")
  target_compile_features(ctbrowser-gpu PUBLIC cxx_std_23)
  ctbrowser_target(ctbrowser-gpu)
  target_link_libraries(ctbrowser-gpu PUBLIC ctbrowser-core ctbrowser-paint ctbrowser-raster
                                             ${CTBROWSER_SDL3_TARGET})
  add_library(ctbrowser::gpu ALIAS ctbrowser-gpu)
endif()
