# ctbaseline: measures interpreter startup as the baseline the compiler is
# compared against. It is not a ctest test, because its numbers vary by machine
# and a performance change should be read rather than failed automatically. It
# is built by `all` so it never runs stale (see ctcompile/tools/CMakeLists.txt).
add_executable(ctcompile-tool-ctbaseline ctbaseline.cpp)
target_link_libraries(ctcompile-tool-ctbaseline PRIVATE LLVMSupport ctcompile::support ctcompile::css
                                                        ctcompile::javascript ctbrowser::dom)
ctcompile_target(ctcompile-tool-ctbaseline)
set_target_properties(ctcompile-tool-ctbaseline PROPERTIES OUTPUT_NAME ctbaseline)
