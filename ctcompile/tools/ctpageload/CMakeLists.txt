# What an image saves on a real page load. Not a ctest gate, like ctbaseline -
# but built by `all`, because THIS is the tool whose stale binary put a wrong
# number into docs/baseline/page-load.json. See ctcompile/tools/CMakeLists.txt.
add_executable(ctcompile-tool-ctpageload ctpageload.cpp)
target_link_libraries(ctcompile-tool-ctpageload PRIVATE ctbrowser::ctbrowser)
ctcompile_target(ctcompile-tool-ctpageload)
set_target_properties(ctcompile-tool-ctpageload PROPERTIES OUTPUT_NAME ctpageload)
