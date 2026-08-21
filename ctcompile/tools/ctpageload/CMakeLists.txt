# What an image saves on a real page load. Not a ctest gate, like ctbaseline -
# but built by `all` through ctcompile-tools, because THIS is the tool whose
# stale binary put a wrong number into docs/baseline/page-load.json. See
# ../CMakeLists.txt.
add_executable(ctcompile-tool-ctpageload EXCLUDE_FROM_ALL ctpageload.cpp)
add_dependencies(ctcompile-tools ctcompile-tool-ctpageload)
target_link_libraries(ctcompile-tool-ctpageload PRIVATE ctbrowser::ctbrowser)
ctcompile_target(ctcompile-tool-ctpageload)
set_target_properties(ctcompile-tool-ctpageload PROPERTIES OUTPUT_NAME ctpageload)
