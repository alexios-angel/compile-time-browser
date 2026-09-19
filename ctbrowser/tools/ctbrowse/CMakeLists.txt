add_executable(ctbrowser-tool-ctbrowse ctbrowse.cpp)
target_link_libraries(ctbrowser-tool-ctbrowse PRIVATE ctbrowser::ctbrowser)
ctbrowser_target(ctbrowser-tool-ctbrowse)
set_target_properties(ctbrowser-tool-ctbrowse PROPERTIES OUTPUT_NAME ctbrowse)
