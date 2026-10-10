# ctpageload: measures what a program image saves on a real page load. Not a
# ctest test, like ctbaseline, but built by `all` so it never runs stale; a
# stale ctpageload binary once put a wrong number into
# ctcompile/docs/baseline/page-load.json. See ctcompile/tools/CMakeLists.txt.
add_executable(ctcompile-tool-ctpageload ctpageload.cpp)
target_link_libraries(ctcompile-tool-ctpageload PRIVATE LLVMSupport ctcompile::support ctbrowser::ctbrowser)
ctcompile_target(ctcompile-tool-ctpageload)
set_target_properties(ctcompile-tool-ctpageload PROPERTIES OUTPUT_NAME ctpageload)
