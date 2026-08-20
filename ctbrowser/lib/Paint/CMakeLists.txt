# ctbrowser.paint - geometry -> a recorded display list of layers.
#
# Sources are relative to this directory; headers are named in full under
# ${CTBROWSER_INCLUDE_DIR}. Adding a file to this subsystem now touches this
# file and nothing else - which is why the 897-line src/CMakeLists.txt was
# split on 2026-08-09. Dependencies are found in cmake/dependencies.cmake.

# --- ctbrowser.paint -----------------------------------------------------
# Records a display list from the fragment tree. Nothing downstream of layout
# knows paint exists; paint knows nothing about how the list is drawn, which
# is what allows a software and a GPU backend to consume the same recording.
add_library(ctbrowser-paint)
target_sources(ctbrowser-paint
  PRIVATE
    record.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/paint/paint.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/paint/values.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/paint/command.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/paint/layer.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/paint/record.hpp")
target_compile_features(ctbrowser-paint PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-paint)
target_link_libraries(ctbrowser-paint PUBLIC ctbrowser-core ctbrowser-dom ctbrowser-style ctbrowser-layout)

add_library(ctbrowser::paint ALIAS ctbrowser-paint)
