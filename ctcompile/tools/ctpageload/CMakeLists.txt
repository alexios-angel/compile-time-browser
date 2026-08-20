# What an image saves on a real page load. EXCLUDE_FROM_ALL, like ctbaseline and
# the engine's benchmarks: a measurement is not a gate.
add_executable(ctcompile-tool-ctpageload EXCLUDE_FROM_ALL ctpageload.cpp)
target_link_libraries(ctcompile-tool-ctpageload PRIVATE ctbrowser::ctbrowser)
ctcompile_target(ctcompile-tool-ctpageload)
set_target_properties(ctcompile-tool-ctpageload PROPERTIES OUTPUT_NAME ctpageload)
