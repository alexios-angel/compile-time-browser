add_executable(ctbrowser-tool-ctdrive ctdrive.cpp)
target_link_libraries(ctbrowser-tool-ctdrive PRIVATE ctbrowser::ctbrowser)
ctbrowser_target(ctbrowser-tool-ctdrive)
set_target_properties(ctbrowser-tool-ctdrive PROPERTIES OUTPUT_NAME ctdrive)
