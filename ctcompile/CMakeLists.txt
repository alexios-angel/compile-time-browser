cmake_minimum_required(VERSION 3.20)

# ctcompile: the whole-application compiler. A SIBLING of ctbrowser, not a tool
# inside it - the repository is a monorepo and this is the second project in it.
#
# THE DEPENDENCY RUNS ONE WAY. ctcompile depends on ctbrowser, on LLVM and on
# MLIR; ctbrowser depends on none of them and must keep building on a machine
# with no LLVM installed at all. That is Principle 2 and Principle 8, and
# ctbrowser/CMakeLists.txt checks the second half of it rather than trusting it.
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
  LANGUAGES CXX)
set(PROJECT_DESCRIPTION "Ahead-of-time compiler for ctbrowser applications.")

get_filename_component(CTCOMPILE_MONOREPO_ROOT "${PROJECT_SOURCE_DIR}/.." ABSOLUTE)
list(APPEND CMAKE_MODULE_PATH "${CTCOMPILE_MONOREPO_ROOT}/cmake/modules"
                              "${PROJECT_SOURCE_DIR}/cmake/modules")
include(CTProject)
include(CTest)

# THE ENGINE. Inside the monorepo its targets already exist and are used
# directly; standalone, the installed package is found. Either way what is
# consumed is the exported target set, never a path into ctbrowser's source
# tree - which is the whole reason the standalone form is worth supporting.
if(NOT TARGET ctbrowser::core)
  find_package(ctbrowser CONFIG REQUIRED)
  set(CTCOMPILE_STANDALONE ON)
  message(STATUS "ctcompile: standalone against an installed ctbrowser")
else()
  set(CTCOMPILE_STANDALONE OFF)
  message(STATUS "ctcompile: building beside ctbrowser in the monorepo")
endif()

# BOOST.PROGRAM_OPTIONS, for the command line. A COMPILED Boost library, which
# is allowed here: the header-only rule was lifted on 2026-07-31 when Boost.URL
# turned compiled-only, and the engine already links one.
#
# Why not llvm::cl, which the dependency order would otherwise prefer: LLVM is
# behind CTCOMPILE_ENABLE_MLIR and OFF until Phase 7, so a command line built on
# llvm::cl would make the stub require a 2 GB dependency to parse `--version`.
# If that trade changes when the MLIR pipeline lands, the switch is one file.
#
# No Windows consequence: ctcompile is devbox-and-Linux only for the
# foreseeable future, and the `windows` preset builds the engine alone.
find_package(Boost 1.80 REQUIRED CONFIG COMPONENTS program_options)

# ctcompile's own warning set, kept SEPARATE from the engine's on purpose.
#
# The engine builds with -Werror -Wconversion and that bar does not move.
# LLVM and MLIR headers, and mlir-tblgen's generated .inc files in particular,
# do not compile clean under it - so when Phase 7 arrives the answer is to
# include those headers as SYSTEM and to demote the specific conversions
# TableGen output triggers HERE, each with a comment saying which construct
# needs it. What must not happen is the engine's flags being relaxed to
# accommodate a dependency the engine does not have.
set(CTCOMPILE_WARNING_OPTIONS -O2 -pedantic -Wall -Wextra -Werror -Wconversion)

function(ctcompile_target name)
  target_compile_options(${name} PRIVATE ${CTCOMPILE_WARNING_OPTIONS})
  target_compile_features(${name} PUBLIC cxx_std_23)
  target_include_directories(${name} PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    # AND THE GENERATED HEADERS, reached by their PROJECT path rather than by a
    # relative one - "Do not #include a generated header by relative path from
    # another project". mlir_tablegen writes them into the mirror of the source
    # tree under the binary directory, so the two roots are spelled the same
    # way and a .inc is included exactly as a hand-written header would be.
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)
endfunction()

# MLIR, and it is OFF until Phase 7 has something to do with it.
#
# Deliberately not "on when MLIR is found": a stub that silently starts
# requiring a 2 GB dependency because it happened to be installed is how a
# runtime-only machine stops being able to build. When it goes ON, the version
# guard runs first and fails with a sentence rather than a template error.
option(CTCOMPILE_ENABLE_MLIR "Build the MLIR-based compiler pipeline (Phase 7 onwards)" OFF)
if(CTCOMPILE_ENABLE_MLIR)
  # MLIR FIRST, THEN LLVM, and the order is not arbitrary: MLIRConfig.cmake
  # locates the LLVM it was built against and sets LLVM_DIR, so finding LLVM
  # first can bind a DIFFERENT installation and the two then disagree about
  # everything from the ABI to the TableGen syntax.
  find_package(MLIR CONFIG REQUIRED)
  find_package(LLVM CONFIG REQUIRED)

  # THE PIN, CHECKED AT CONFIGURE TIME, through the function that already owns
  # that rule. Phase -1 put ct_require_llvm_version() in cmake/modules/ and
  # wrote "unused until Phase 7 stands MLIR up - it is here now so that phase
  # adds dialects rather than build plumbing". This is Phase 7, and a second
  # copy of the check written here instead would be the drift the whole
  # monorepo layout exists to prevent.
  ct_require_llvm_version()
  message(STATUS "ctcompile: MLIR ${LLVM_PACKAGE_VERSION} at ${MLIR_DIR}")

  list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}" "${LLVM_CMAKE_DIR}")
  include(TableGen)
  include(AddLLVM)
  include(AddMLIR)
  # SYSTEM, so ctcompile's -Werror -Wconversion does not have to be relaxed to
  # accommodate a dependency the engine does not have. That trade is spelled
  # out where CTCOMPILE_WARNING_OPTIONS is set; this is the half of it that
  # keeps the engine's bar where it is.
  include_directories(SYSTEM ${LLVM_INCLUDE_DIRS} ${MLIR_INCLUDE_DIRS})
endif()

# THE DIALECT, ITS PASSES AND ITS DRIVERS, only when MLIR is on. Everything
# under CTJS/ and both tools include MLIR headers, so a runtime-only box must
# not descend into them at all - which is gate item 4 of Phase 7 and Principle 8
# underneath it.
if(CTCOMPILE_ENABLE_MLIR)
  add_subdirectory(include/ctcompile/CTJS/IR)
  add_subdirectory(include/ctcompile/CTJS/Transforms)
  add_subdirectory(lib/CTJS/IR)
endif()

add_subdirectory(lib)
add_subdirectory(tools)

if(BUILD_TESTING)
  add_subdirectory(test)
endif()
