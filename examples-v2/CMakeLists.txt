# v2 examples. Unlike v1's, these are ordinary programs: no NTTP, no page<>,
# no compile-time anything - the engine is a runtime library now.

if(SDL3_FOUND)
  add_executable(ctbrowse ctbrowse.cpp)
  target_link_libraries(ctbrowse PRIVATE ctbrowser::core ctbrowser::raster ctbrowser::shell
                                         ctbrowser::gpu ctbrowser::app SDL3::SDL3)
  target_compile_options(ctbrowse PRIVATE ${CTBROWSER_V2_WARNINGS})
endif()
