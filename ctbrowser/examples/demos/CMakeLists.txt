file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
foreach(source IN LISTS sources)
  ctbrowser_example("${source}")
endforeach()
# The parent assembles the Windows distribution from these existing targets.
set(example-sources ${example-sources} ${sources} PARENT_SCOPE)
