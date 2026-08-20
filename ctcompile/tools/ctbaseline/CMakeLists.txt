# The Phase 0 startup baseline. EXCLUDE_FROM_ALL, like the engine's benchmarks
# and for the same reason: a measurement is not a gate, its numbers move with
# the machine, and a perf change should be READ rather than silently failed.
add_executable(ctcompile-tool-ctbaseline EXCLUDE_FROM_ALL ctbaseline.cpp)
target_link_libraries(ctcompile-tool-ctbaseline PRIVATE ctcompile::support ctcompile::css
                                                        ctcompile::javascript ctbrowser::dom)
ctcompile_target(ctcompile-tool-ctbaseline)
set_target_properties(ctcompile-tool-ctbaseline PROPERTIES OUTPUT_NAME ctbaseline)
