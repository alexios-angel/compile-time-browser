# The engine: ten subsystems, one per directory, one CMake target each.
#
# This file was 897 lines until 2026-08-09 - dependency discovery and ten target
# definitions in one place, with 78 header paths hardcoded among them. It now
# does only what is genuinely shared or genuinely global:
#
#   cmake/dependencies.cmake   everything the engine LOOKS FOR
#   src/<subsystem>/           that subsystem's target, sources and headers
#   here                       the compile/link policy, the umbrella target,
#                              the merged archive, and the install/export
#
# The point is that adding a file to `layout` is now one edit to
# src/layout/CMakeLists.txt. It used to be an edit to a 900-line file where the
# `.cpp` list and the installed-header list were four hundred lines apart, and
# forgetting the first one silently stopped compiling the file.
include(GNUInstallDirs)

include("${PROJECT_SOURCE_DIR}/cmake/dependencies.cmake")

# the engine is NOT header-only and does NOT depend on std::embed. Keeping the warning
# set identical to the previous engine's is deliberate - the rewrite should not quietly relax the
# bar - minus the specific to the previous engine -Wno-overlength-strings, which existed only for
# page-as-NTTP raw strings that the engine no longer has.
set(CTBROWSER_STRICT_WARNINGS -O2 -pedantic -Wall -Wextra -Werror -Wconversion)

# --- LINK WITH LLD ------------------------------------------------------
# Measured on this tree: the same executable links in 0.65 s with lld and
# 4.31 s with the default ld, and the engine produces TWENTY-SIX of them. That is the
# single cheapest thing in this file. Checked rather than assumed, because a
# toolchain without lld must still build.
include(CheckLinkerFlag OPTIONAL RESULT_VARIABLE CTBROWSER_HAVE_LINKER_CHECK)
option(CTBROWSER_USE_LLD "Link with lld when it is available" ON)
set(CTBROWSER_LINK_OPTIONS "")
if(CTBROWSER_USE_LLD AND CTBROWSER_HAVE_LINKER_CHECK)
  check_linker_flag(CXX "-fuse-ld=lld" CTBROWSER_LLD_WORKS)
  if(CTBROWSER_LLD_WORKS)
    set(CTBROWSER_LINK_OPTIONS -fuse-ld=lld)
    message(STATUS "ctbrowser: linking with lld")
  endif()
endif()

# --- WHOLE-PROGRAM INLINING, IF YOU WANT IT BACK ------------------------
# Moving the engine's code out of the module interfaces is what made consumer
# builds fast, and the thing it gives up is the compiler's freedom to inline
# ACROSS the library boundary - which is what an all-inline (or header-only)
# engine gave for free, at the cost of recompiling it in every translation unit.
#
# LTO buys that back at link time instead of at compile time, which is the same
# trade made in the right place. Off by default because it makes linking slow
# again, and the default here is meant to be fast to build.
option(CTBROWSER_LTO "Inline across the engine boundary again, at link time" OFF)

# Everything the engine builds - libraries, tests and examples - goes through this, so
# there is one place that decides how the engine is compiled and linked.
function(ctbrowser_target name)
  target_compile_options(${name} PRIVATE ${CTBROWSER_STRICT_WARNINGS})
  if(CTBROWSER_LINK_OPTIONS)
    target_link_options(${name} PRIVATE ${CTBROWSER_LINK_OPTIONS})
  endif()
  if(CTBROWSER_LTO)
    set_property(TARGET ${name} PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
  endif()
endfunction()

# Headers live in include/ctbrowser/<subsystem>/ and implementations in
# src/<subsystem>/. A header declares, its .cpp defines, and the few that are
# templates (slab, handle) or constexpr value types (geometry) stay whole in
# the header because there is nothing to move out of them.
set(CTBROWSER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")

# --- the subsystems ------------------------------------------------------
# In dependency order, which is also the order of the pipeline: HTML -> dom ->
# style -> layout -> paint -> raster -> pixels, with script and shell across
# the top. Nothing upstream knows what is downstream of it, and that is what
# keeps the graph acyclic.
add_subdirectory(core)
add_subdirectory(dom)
add_subdirectory(script)
add_subdirectory(style)
add_subdirectory(layout)
add_subdirectory(paint)
add_subdirectory(raster)
add_subdirectory(shell)
add_subdirectory(app)
add_subdirectory(gpu)


# NOTHING OF ctcss'S IS INSTALLED any more. This shipped a copy of its include
# tree because `style/engine.hpp` - a public header - included <ctcss.hpp>, so any
# consumer of the umbrella header parsed it. The CSS front end is ours now, no
# public header names it, and the copy (and the skew of a consumer ending up with
# two ctcss trees) goes with it. tools/check/check-package.sh proves an install
# still compiles.
# --- ctbrowser (umbrella) ------------------------------------------------
# What an application imports and links. One of each.
add_library(ctbrowser INTERFACE)
target_sources(ctbrowser
  INTERFACE
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser.hpp")
target_compile_features(ctbrowser INTERFACE cxx_std_23)
target_link_libraries(ctbrowser INTERFACE
  ctbrowser-core ctbrowser-dom ctbrowser-script ctbrowser-style ctbrowser-layout
  ctbrowser-paint ctbrowser-raster ctbrowser-shell ctbrowser-app)
add_library(ctbrowser::ctbrowser ALIAS ctbrowser)

# --- ONE ARCHIVE ---------------------------------------------------------
# The engine is nine libraries because it is nine modules, and every one of
# them ends up on every link line. `libctbrowser-all.a` is all of them merged,
# so an application - or a build system that is not CMake - links ONE file.
#
# `-all` rather than `libctbrowser.a`, which is the umbrella TARGET's own
# output: two rules producing one path is a build that refuses to generate.
#
# It is a merge, not a rebuild: the same objects, collected with the archiver.
# CMake users need nothing new; `ctbrowser::ctbrowser` still works and still carries
# the include paths and module BMIs, which an archive cannot.
#
# CTBROWSER_SINGLE_LIB=OFF skips it.
option(CTBROWSER_SINGLE_LIB "Also produce one merged libctbrowser-all.a" ON)
if(CTBROWSER_SINGLE_LIB)
  set(ctbrowser-merged "${CMAKE_CURRENT_BINARY_DIR}/libctbrowser-all.a")
  set(ctbrowser-parts ctbrowser-core ctbrowser-dom ctbrowser-script ctbrowser-style
                      ctbrowser-layout ctbrowser-paint ctbrowser-raster ctbrowser-shell
                      ctbrowser-app ctbrowser)
  # A header-only subsystem is an INTERFACE library: it has no archive to merge
  # because it contributes no objects, so asking for its TARGET_FILE is an
  # error rather than an omission.
  set(ctbrowser-part-files "")
  foreach(part IN LISTS ctbrowser-parts)
    get_target_property(part_type ${part} TYPE)
    if(NOT part_type STREQUAL "INTERFACE_LIBRARY")
      list(APPEND ctbrowser-part-files "$<TARGET_FILE:${part}>")
    endif()
  endforeach()
  # The MRI script is how llvm-ar and GNU ar both merge archives; writing it at
  # build time keeps the generator expressions resolvable.
  set(ctbrowser-mri "${CMAKE_CURRENT_BINARY_DIR}/ctbrowser-merge.mri")
  file(GENERATE OUTPUT "${ctbrowser-mri}" CONTENT
       "create ${ctbrowser-merged}\n$<JOIN:$<LIST:TRANSFORM,${ctbrowser-part-files},PREPEND,addlib >,\n>\nsave\nend\n")
  add_custom_command(
    OUTPUT "${ctbrowser-merged}"
    COMMAND "${CMAKE_AR}" -M < "${ctbrowser-mri}"
    DEPENDS ${ctbrowser-parts} "${ctbrowser-mri}"
    COMMENT "merging the engine into one libctbrowser-all.a"
    VERBATIM)
  add_custom_target(ctbrowser-single-lib ALL DEPENDS "${ctbrowser-merged}")
endif()

# --- installing the engine -------------------------------------------------------
# Headers install to include/, archives to lib/. This was a great deal more
# involved when the engine was modules: a CXX_MODULES file set, a
# CXX_MODULES_DIRECTORY on the export, and a consumer that had to be able to
# rebuild BMIs from the installed sources.
set(CTBROWSER_TARGETS
  ctbrowser-core ctbrowser-dom ctbrowser-script ctbrowser-style ctbrowser-layout
  ctbrowser-paint ctbrowser-raster ctbrowser-shell ctbrowser-app ctbrowser)
if(TARGET ctbrowser-gpu)
  list(APPEND CTBROWSER_TARGETS ctbrowser-gpu)
endif()

install(TARGETS ${CTBROWSER_TARGETS}
  EXPORT ctbrowser-targets
  FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")

set(CTBROWSER_INSTALL_DOCDIR "${CMAKE_INSTALL_DOCDIR}")
if(NOT CTBROWSER_INSTALL_DOCDIR)
  set(CTBROWSER_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/${PROJECT_NAME}")
endif()
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/NOTICE"
        DESTINATION "${CTBROWSER_INSTALL_DOCDIR}")

install(EXPORT ctbrowser-targets
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ctbrowser"
  NAMESPACE ctbrowser::)

# The alias an application links. `ctbrowser::ctbrowser` is the build-tree name;
# this is what survives an install.
# Whatever the exported targets name in their link interface, the consumer has
# to be able to resolve. SDL3 is conditional because the app module links it
# only when it was found - a headless install must not demand it back.
# COMPONENTS url, and CURL beside it. Both are linked PRIVATE, which for a
# STATIC library still puts them on a consumer's link line - so a consumer that
# cannot find them fails to link with no idea why. The Boost line said no
# component at all until 2026-07-31, which was a gap from the day Boost.URL
# landed; tools/check/check-package.sh is what exists to catch exactly this.
set(CTBROWSER_CONFIG_DEPS
    "find_dependency(Boost 1.80 CONFIG COMPONENTS url)\nfind_dependency(CURL)\nfind_dependency(Threads)")
# PNG, and it is NOT optional - `find_package(PNG REQUIRED)` above. It was
# missing from this list until 2026-08-09 and the symptom was exactly what the
# comment above predicts: `find_package(ctbrowser)` failed at GENERATE time,
# before a single line compiled, with "the link interface of target
# ctbrowser::ctbrowser-shell contains PNG::PNG but the target was not found".
# PRIVATE does not save you here - a STATIC archive carries the dependency into
# the consumer's link interface regardless.
string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(PNG)")
if(SDL3_FOUND)
  string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(SDL3 CONFIG)")
endif()
# EVERY package an exported target links has to be findable by the consumer,
# or find_package(ctbrowser) fails on a target it cannot resolve. Adding an
# optional dependency to a library and forgetting this line is exactly the kind
# of break tools/check/check-package.sh exists to catch - it caught these two.
if(Freetype_FOUND)
  string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(Freetype)")
endif()
if(SDL3_image_FOUND)
  string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(SDL3_image CONFIG)")
endif()
if(SDL3_ttf_FOUND)
  string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(SDL3_ttf CONFIG)")
endif()
# PRIVATE-linked, so it is not in the exported link interface and a consumer
# never resolves it - but a STATIC ctbrowser-raster carries the dependency into
# the consumer's link line anyway, and check-package.sh is what proves whether
# that matters here rather than reasoning about it.
if(plutosvg_FOUND)
  string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(plutosvg CONFIG)")
endif()
# JPEG, and it leaks the same way PNG does - but only in the pkg-config form.
# `PkgConfig::CTB_TURBOJPEG` is a TARGET NAME in the exported link interface, so
# the consumer has to be able to recreate it, and the only thing that can is the
# same pkg_check_modules call under the same prefix. The fallback branch links a
# resolved library PATH instead, and a path needs nothing found again.
if(TARGET PkgConfig::CTB_TURBOJPEG)
  string(APPEND CTBROWSER_CONFIG_DEPS
         "\nfind_dependency(PkgConfig)"
         "\npkg_check_modules(CTB_TURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)")
endif()
file(WRITE "${PROJECT_BINARY_DIR}/ctbrowser-config.cmake"
"include(CMakeFindDependencyMacro)
${CTBROWSER_CONFIG_DEPS}
include(\"\${CMAKE_CURRENT_LIST_DIR}/ctbrowser-targets.cmake\")
")
install(FILES "${PROJECT_BINARY_DIR}/ctbrowser-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ctbrowser")
