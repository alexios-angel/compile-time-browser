# ctbrowser.paint - geometry -> a recorded display list of layers.
#
# Records a display list from the fragment tree. Nothing downstream of layout
# knows paint exists; paint knows nothing about how the list is drawn, which
# is what allows a software and a GPU backend to consume the same recording.
add_library(ctbrowser-paint)
target_sources(ctbrowser-paint PUBLIC FILE_SET HEADERS BASE_DIRS "${CTBROWSER_INCLUDE_DIR}")
target_sources(ctbrowser-paint PRIVATE
  record.cpp
)
target_compile_features(ctbrowser-paint PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-paint)
target_link_libraries(ctbrowser-paint PUBLIC ctbrowser-core ctbrowser-dom ctbrowser-style ctbrowser-layout)

add_library(ctbrowser::paint ALIAS ctbrowser-paint)
