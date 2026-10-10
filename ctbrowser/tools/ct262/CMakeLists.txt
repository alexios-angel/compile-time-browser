# ct262: runs one test262 file for tools/check/test262.py.
add_executable(ctbrowser-tool-ct262 ct262.cpp)
target_link_libraries(ctbrowser-tool-ct262 PRIVATE ctbrowser::ctbrowser ctbrowser-options)
ctbrowser_target(ctbrowser-tool-ct262)
set_target_properties(ctbrowser-tool-ct262 PROPERTIES OUTPUT_NAME ct262)
