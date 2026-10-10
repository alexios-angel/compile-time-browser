cmake_minimum_required(VERSION 3.20)

# ctcompile: the whole-application compiler. A sibling of ctbrowser, not a tool
# inside it: the repository is a monorepo and this is the second project in it.
#
# The dependency runs one way: ctcompile depends on ctbrowser, LLVM and MLIR;
# ctbrowser depends on none of them and must keep building with no LLVM
# installed. ctbrowser/CMakeLists.txt checks that it does not reach for LLVM.
#
# Two ways to configure this:
#
#   in the monorepo   cmake -S ctbrowser -B build -DCTBROWSER_ENABLE_PROJECTS=ctcompile
#   standalone        cmake -S ctcompile -B build-ctcompile -Dctbrowser_DIR=<install>/lib/cmake/ctbrowser
#
# The standalone form is not a convenience. It is what proves the boundary is
# real: a project that can only build inside its sibling's tree has an
# undeclared dependency on that tree's variables, and the first person to find
# out is whoever tries to package it.
project(ctcompile
  HOMEPAGE_URL "https://github.com/alexios-angel/compile-time-browser"
  VERSION 0.1.0
  # C is enabled only for LLVM: find_package(MLIR) pulls in LLVM's FindLibEdit,
  # which probes histedit.h with check_include_file (a .c try_compile). With only
  # CXX enabled, configure fails with "try_compile() works only for enabled
  # languages". No C is compiled here.
  LANGUAGES CXX C)
set(PROJECT_DESCRIPTION "Ahead-of-time compiler for ctbrowser applications.")

get_filename_component(CTCOMPILE_MONOREPO_ROOT "${PROJECT_SOURCE_DIR}" DIRECTORY)
include(CTest)
include(GNUInstallDirs)

# The engine. Inside the monorepo its targets already exist and are used
# directly; standalone, the installed package is found. Either way only the
# exported target set is consumed, never a path into ctbrowser's source tree,
# which is the reason the standalone form is worth supporting.
if(NOT TARGET ctbrowser::core)
  find_package(ctbrowser CONFIG REQUIRED)
  message(STATUS "ctcompile: standalone against an installed ctbrowser")
else()
  message(STATUS "ctcompile: building beside ctbrowser in the monorepo")
endif()

# ctcompile's own warning set, separate from the engine's. The engine builds
# with -Werror -Wconversion and that bar does not move. LLVM/MLIR headers and
# mlir-tblgen's generated .inc files do not compile clean under it, so they are
# included as SYSTEM (see ctcompile_includes and the MLIR block below) instead
# of relaxing these flags.
set(CTCOMPILE_WARNING_OPTIONS -O2 -pedantic -Wall -Wextra -Werror -Wconversion)

# The two include roots, source and generated, spelled the same way: a
# generated header is included exactly as a hand-written one is.
function(ctcompile_includes name)
  target_include_directories(${name} PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
  # The generated headers, reached by their project path rather than a relative
  # one. mlir_tablegen writes them into a mirror of the source tree under the
  # binary directory, so a .inc is included exactly as a hand-written header.
  #
  # SYSTEM, because mlir-tblgen's generated code trips -Wunused-parameter (an
  # unused `odsOperandsSize` in every variadic op's adaptor, an unused `impl` in
  # interface Models). Marking only the generated root SYSTEM silences that
  # without turning the warning off for hand-written code.
  target_include_directories(${name} SYSTEM PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)
endfunction()

function(ctcompile_target name)
  target_compile_options(${name} PRIVATE ${CTCOMPILE_WARNING_OPTIONS})
  target_compile_features(${name} PUBLIC cxx_std_23)
  ctcompile_includes(${name})
endfunction()

# LLVM Option/Support supply command-line parsing even without the MLIR pipeline.
# Find MLIR first when enabled so LLVM comes from the same installation.
option(CTCOMPILE_ENABLE_MLIR "Build the MLIR-based compiler pipeline (Phase 7 onwards)" OFF)
if(CTCOMPILE_ENABLE_MLIR)
  # MLIR first, then LLVM: MLIRConfig.cmake locates the LLVM it was built
  # against and sets LLVM_DIR. Finding LLVM first can bind a different
  # installation, and the two then disagree about everything from the ABI to
  # the TableGen syntax.
  find_package(MLIR CONFIG REQUIRED)
endif()
find_package(LLVM CONFIG REQUIRED)
include_directories(BEFORE SYSTEM ${LLVM_INCLUDE_DIRS})

# Pin the option-table generator/API as well as the optional MLIR pipeline.
# An LLVM upgrade is its own change: ctcompile/docs/LLVMUpgrade.md.
set(CTCOMPILE_REQUIRED_LLVM_MAJOR 23)
set(CTCOMPILE_TESTED_LLVM_VERSION "23.1.0")
if(NOT LLVM_VERSION_MAJOR EQUAL CTCOMPILE_REQUIRED_LLVM_MAJOR)
  message(FATAL_ERROR
    "ctcompile needs LLVM ${CTCOMPILE_REQUIRED_LLVM_MAJOR} "
    "(tested at ${CTCOMPILE_TESTED_LLVM_VERSION}), found ${LLVM_PACKAGE_VERSION} at "
    "${LLVM_DIR}. Upgrading LLVM is its own change - see ctcompile/docs/LLVMUpgrade.md.")
endif()

if(CTCOMPILE_ENABLE_MLIR)
  # The chosen LLVM/MLIR headers come first. Homebrew links a formula's headers
  # into <prefix>/include, which other dependencies add as a plain -I, and -I is
  # searched before -isystem, so a second installed LLVM would shadow this one.
  # BEFORE fixes the order; SYSTEM keeps -Werror -Wconversion off these headers.
  include_directories(BEFORE SYSTEM ${MLIR_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})

  message(STATUS "ctcompile: MLIR ${LLVM_PACKAGE_VERSION} at ${MLIR_DIR}")

  # add_mlir_doc writes under ${MLIR_BINARY_DIR}/docs, and out of tree that
  # variable is empty, so the doc target would try to mkdir("/docs") and fail
  # with an unrelated-looking permission error. Point it at this build
  # directory. Generated docs are never committed.
  set(MLIR_BINARY_DIR "${CMAKE_BINARY_DIR}")

  list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}" "${LLVM_CMAKE_DIR}")
  include(TableGen)
  include(AddLLVM)
  include(AddMLIR)
endif()

# Libraries create targets; their public include directories then register
# headers and TableGen generators. Generated-header dependencies are explicit
# target edges and resolve after all subdirectories have been configured.
add_subdirectory(lib)
add_subdirectory(include)
add_subdirectory(tools)
add_subdirectory(probes)

# Install the command-line tools, with the existing launcher beside the packager
# when building in the monorepo. External runtime dependencies stay on the host.
# ponytail: host library paths stay fixed; bundle dependencies for a portable release.
set(CTCOMPILE_INSTALL_TARGETS ctcompile-tool)
if(CTCOMPILE_ENABLE_MLIR)
  list(APPEND CTCOMPILE_INSTALL_TARGETS ctjs-opt ctjs-translate)
endif()
if(TARGET ctbrowser-tool-ctrun)
  list(APPEND CTCOMPILE_INSTALL_TARGETS ctbrowser-tool-ctrun)
endif()
set_target_properties(${CTCOMPILE_INSTALL_TARGETS} PROPERTIES
  INSTALL_RPATH_USE_LINK_PATH TRUE)
install(TARGETS ${CTCOMPILE_INSTALL_TARGETS}
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT ctcompile-tools)
set(CTCOMPILE_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/ctcompile")
install(FILES "${CTCOMPILE_MONOREPO_ROOT}/LICENSE" "${CTCOMPILE_MONOREPO_ROOT}/NOTICE"
  DESTINATION "${CTCOMPILE_INSTALL_DOCDIR}" COMPONENT ctcompile-tools)

if(BUILD_TESTING)
  add_subdirectory(test)
endif()
