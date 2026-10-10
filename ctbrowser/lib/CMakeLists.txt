# The engine: ten subsystems, one per directory, one CMake target each.
#
#   cmake/dependencies.cmake   every package the engine looks for
#   lib/<Subsystem>/           that subsystem's target and its dependencies
#   here                       the compile/link policy, the umbrella target,
#                              and the install/export
include(GNUInstallDirs)

include("${PROJECT_SOURCE_DIR}/cmake/dependencies.cmake")

# Every engine target builds with this warning set and -Werror (see
# ctbrowser_target below). Do not relax it per target.
set(CTBROWSER_STRICT_WARNINGS -O2 -pedantic -Wall -Wextra -Werror -Wconversion)

# --- Link with lld ------------------------------------------------------
# lld links each executable roughly 6x faster than GNU ld (measured 0.65 s vs
# 4.31 s for the same executable). It is probed, not assumed, so a toolchain
# without lld still builds.
include(CheckLinkerFlag)
option(CTBROWSER_USE_LLD "Link with lld when it is available" ON)
set(CTBROWSER_LINK_OPTIONS "")
if(CTBROWSER_USE_LLD)
  check_linker_flag(CXX "-fuse-ld=lld" CTBROWSER_LLD_WORKS)
  if(CTBROWSER_LLD_WORKS)
    set(CTBROWSER_LINK_OPTIONS -fuse-ld=lld)
    message(STATUS "ctbrowser: linking with lld")
  endif()
endif()

# Everything the engine builds (libraries, tests and examples) goes through this,
# so there is one place that decides how the engine is compiled and linked.
function(ctbrowser_target name)
  target_compile_options(${name} PRIVATE ${CTBROWSER_STRICT_WARNINGS})
  if(CTBROWSER_LINK_OPTIONS)
    target_link_options(${name} PRIVATE ${CTBROWSER_LINK_OPTIONS})
  endif()
endfunction()

# --- The subsystems ------------------------------------------------------
# Each target declares its header set before private include directories so
# the public include root keeps precedence. include/ supplies the files later.
# In dependency order, which is also the order of the pipeline: HTML -> dom ->
# style -> layout -> paint -> raster -> pixels, with script and shell across
# the top. Nothing upstream knows what is downstream of it, and that is what
# keeps the graph acyclic.
add_subdirectory(Core)
add_subdirectory(DOM)
add_subdirectory(Script)
add_subdirectory(Style)
add_subdirectory(Layout)
add_subdirectory(Paint)
add_subdirectory(Raster)
add_subdirectory(Page)
add_subdirectory(Shell)
add_subdirectory(App)


# --- ctbrowser (umbrella) ------------------------------------------------
# What an application links: every subsystem library.
add_library(ctbrowser INTERFACE)
target_sources(ctbrowser INTERFACE FILE_SET HEADERS BASE_DIRS "${CTBROWSER_INCLUDE_DIR}")
target_compile_features(ctbrowser INTERFACE cxx_std_23)
target_link_libraries(ctbrowser INTERFACE
  ctbrowser-core ctbrowser-dom ctbrowser-script ctbrowser-style ctbrowser-layout
  ctbrowser-paint ctbrowser-raster ctbrowser-page ctbrowser-shell ctbrowser-app)
add_library(ctbrowser::ctbrowser ALIAS ctbrowser)

# --- Installing the engine -----------------------------------------------
# Headers install to include/, archives to lib/.
set(CTBROWSER_TARGETS
  ctbrowser-core ctbrowser-dom ctbrowser-script ctbrowser-style ctbrowser-layout
  ctbrowser-paint ctbrowser-raster ctbrowser-page ctbrowser-shell ctbrowser-app ctbrowser)

install(TARGETS ${CTBROWSER_TARGETS}
  EXPORT ctbrowser-targets
  FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")

set(CTBROWSER_INSTALL_DOCDIR "${CMAKE_INSTALL_DOCDIR}")
if(NOT CTBROWSER_INSTALL_DOCDIR)
  set(CTBROWSER_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/${PROJECT_NAME}")
endif()
install(FILES "${CTBROWSER_MONOREPO_ROOT}/LICENSE" "${CTBROWSER_MONOREPO_ROOT}/NOTICE"
        DESTINATION "${CTBROWSER_INSTALL_DOCDIR}")

install(EXPORT ctbrowser-targets
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ctbrowser"
  NAMESPACE ctbrowser::)

# ctbrowser-config.cmake must find again every package an exported target
# names in its link interface. These archives are STATIC, so even PRIVATE
# dependencies end up on the consumer's link line; a missing entry fails
# find_package(ctbrowser) or the consumer's link. tools/check/check-package.sh
# catches a missing one. Boost.URL, CURL and Threads are always needed; the
# optional packages below are listed only when the build found them, so a
# headless install does not demand SDL3 back.
set(CTBROWSER_CONFIG_DEPS
    "find_dependency(Boost 1.80 CONFIG COMPONENTS url)\nfind_dependency(CURL)\nfind_dependency(Threads)")
# PNG is required (cmake/dependencies.cmake), and ctbrowser-shell links
# PNG::PNG, so the consumer must find it too; without it find_package(ctbrowser)
# fails at generate time on an unresolved PNG::PNG.
string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(PNG)")
if(SDL3_FOUND)
  string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(SDL3 CONFIG)")
endif()
if(SDL3_image_FOUND)
  string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(SDL3_image CONFIG)")
endif()
if(SDL3_ttf_FOUND)
  string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(SDL3_ttf CONFIG)")
endif()
# plutosvg is linked PRIVATE by ctbrowser-raster, but the STATIC archive still
# carries it onto the consumer's link line.
if(plutosvg_FOUND)
  string(APPEND CTBROWSER_CONFIG_DEPS "\nfind_dependency(plutosvg CONFIG)")
endif()
# JPEG needs the same treatment, but only in the pkg-config form:
# `PkgConfig::CTB_TURBOJPEG` is a target name in the exported link interface, so
# the consumer has to recreate it with the same pkg_check_modules call under the
# same prefix. The fallback branch in cmake/dependencies.cmake links a resolved
# library path instead, and a path needs nothing found again.
if(TARGET PkgConfig::CTB_TURBOJPEG)
  string(APPEND CTBROWSER_CONFIG_DEPS
         "\nfind_dependency(PkgConfig)"
         "\npkg_check_modules(CTB_TURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)")
endif()
file(WRITE "${PROJECT_BINARY_DIR}/ctbrowser-config.cmake"
"include(CMakeFindDependencyMacro)
${CTBROWSER_CONFIG_DEPS}
include(\"\${CMAKE_CURRENT_LIST_DIR}/ctbrowser-targets.cmake\")
# Match build-tree aliases while retaining the existing exported target names.
foreach(subsystem core dom script style layout paint raster page shell app)
  if(NOT TARGET ctbrowser::\${subsystem})
    add_library(ctbrowser::\${subsystem} ALIAS ctbrowser::ctbrowser-\${subsystem})
  endif()
endforeach()
")
install(FILES "${PROJECT_BINARY_DIR}/ctbrowser-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ctbrowser")
