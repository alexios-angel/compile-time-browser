# The Phase 0 startup baseline. Not a ctest gate - its numbers move with the
# machine, and a perf change should be READ rather than silently failed - but
# BUILT by `all`, because a measurement tool that is older than the engine
# reports a number about code that is gone. See ctcompile/tools/CMakeLists.txt
# for the three times that has happened here.
add_executable(ctcompile-tool-ctbaseline ctbaseline.cpp)
target_link_libraries(ctcompile-tool-ctbaseline PRIVATE LLVMSupport ctcompile::support ctcompile::css
                                                        ctcompile::javascript ctbrowser::dom)
ctcompile_target(ctcompile-tool-ctbaseline)
set_target_properties(ctcompile-tool-ctbaseline PROPERTIES OUTPUT_NAME ctbaseline)
