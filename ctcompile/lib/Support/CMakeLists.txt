add_library(ctcompile-support)
target_sources(ctcompile-support
  PRIVATE
    Manifest.cpp
    Version.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${PROJECT_SOURCE_DIR}/include"
      FILES
        "${PROJECT_SOURCE_DIR}/include/ctcompile/Support/Manifest.hpp"
        "${PROJECT_SOURCE_DIR}/include/ctcompile/Support/Version.hpp")
ctcompile_target(ctcompile-support)

# THE NARROW ENGINE DEPENDENCY, and it is narrow on purpose. Reading a
# program's bytecode needs Core (atoms, handles) and Script; it does not need
# Raster, Shell, App or SDL3. Linking the ctbrowser umbrella here would drag all
# of them in and make the compiler need a windowing system to inspect a
# function, which is exactly what Phase -1's "expose narrow compiler-facing
# libraries" is about.
target_link_libraries(ctcompile-support PUBLIC ctbrowser::core ctbrowser::script)

# The two versions the binary reports, injected rather than written into the
# source so they cannot drift from what CMake actually built.
#
# ctbrowser's version comes from whichever mechanism supplied its targets:
# project(ctbrowser VERSION) sets ctbrowser_VERSION for a monorepo build, and a
# standalone build gets it from the installed package's config - which does not
# write a version file today, so "unknown" is the honest answer there rather
# than a number this project made up.
set(engine-version "${ctbrowser_VERSION}")
if(NOT engine-version)
  set(engine-version "unknown")
endif()
target_compile_definitions(ctcompile-support PRIVATE
  CTCOMPILE_VERSION="${PROJECT_VERSION}"
  CTCOMPILE_CTBROWSER_VERSION="${engine-version}")

add_library(ctcompile::support ALIAS ctcompile-support)
