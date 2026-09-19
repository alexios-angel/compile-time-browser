add_executable(ctbrowser-tool-ctrun ctrun.cpp)
target_link_libraries(ctbrowser-tool-ctrun PRIVATE ctbrowser::ctbrowser)
ctbrowser_target(ctbrowser-tool-ctrun)
set_target_properties(ctbrowser-tool-ctrun PROPERTIES OUTPUT_NAME ctrun)
