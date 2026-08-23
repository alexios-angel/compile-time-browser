# Shared build helpers for the monorepo's projects.
#
# What belongs here is what BOTH projects would otherwise each spell: the LLVM
# version guard and the TableGen wiring. What does not belong here is anything
# only ctbrowser needs - that is ctbrowser/cmake/ - or anything only ctcompile
# needs, which is ctcompile/cmake/.

# WHERE THIS MODULE IS, captured while it is being READ.
#
# CMAKE_CURRENT_LIST_DIR inside a function body is the directory of the file
# that CALLED the function, not the one that defined it - so the include below
# resolved to <caller>/../LLVMVersion.cmake and the pin was never read. The
# check that used it then compared LLVM_VERSION_MAJOR against an empty string,
# which CMake's LESS/GREATER treat as 0: it accepted everything.
set(CT_PROJECT_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# ct_require_llvm_version()
#
# Fails configuration with an explicit message when the discovered LLVM/MLIR is
# outside the pinned range, rather than failing later inside mlir-tblgen with a
# diagnostic that names a template rather than a version. Principle 12.
function(ct_require_llvm_version)
  include("${CT_PROJECT_MODULE_DIR}/../LLVMVersion.cmake")
  if(NOT DEFINED LLVM_PACKAGE_VERSION)
    message(FATAL_ERROR "ct_require_llvm_version(): call find_package(LLVM CONFIG) first")
  endif()
  if(LLVM_VERSION_MAJOR LESS CTCOMPILE_REQUIRED_LLVM_MAJOR
     OR LLVM_VERSION_MAJOR GREATER CTCOMPILE_MAX_LLVM_MAJOR)
    message(FATAL_ERROR
      "ctcompile needs LLVM/MLIR ${CTCOMPILE_REQUIRED_LLVM_MAJOR} "
      "(tested at ${CTCOMPILE_TESTED_LLVM_VERSION}), found ${LLVM_PACKAGE_VERSION} at "
      "${LLVM_DIR}. Upgrading LLVM is its own change - see ctcompile/docs/LLVMUpgrade.md.")
  endif()
endfunction()

# ct_add_tablegen_component(NAME <name> TD <file.td> ARGS <tblgen args...>)
#
# One dialect-like component, wired identically every time: mlir_tablegen for
# each output plus add_public_tablegen_target, with the resulting target
# recorded in a global property so a library can attach every generator it
# needs without naming them one at a time.
#
# THE DEPENDENCY EDGE IS THE POINT. A library that consumes generated headers
# without depending on the generator builds correctly right up until someone
# runs a parallel build on a cold tree, and then fails in a file nobody edited.
# Principle 10 makes the edge mandatory even where a build happens to succeed
# without it; this function is how it stops being something to remember.
#
# Unused until Phase 7 stands MLIR up - it is here now so that phase adds
# dialects rather than build plumbing, which is what Phase -1 asks for.
function(ct_add_tablegen_component)
  cmake_parse_arguments(CT "" "NAME;TD" "ARGS" ${ARGN})
  if(NOT CT_NAME OR NOT CT_TD)
    message(FATAL_ERROR "ct_add_tablegen_component(): NAME and TD are both required")
  endif()
  if(NOT COMMAND mlir_tablegen)
    message(FATAL_ERROR
      "ct_add_tablegen_component(${CT_NAME}): MLIR's CMake helpers are not loaded. "
      "This needs find_package(MLIR CONFIG) and its AddMLIR module first.")
  endif()
  set(LLVM_TARGET_DEFINITIONS "${CT_TD}")
  foreach(arg IN LISTS CT_ARGS)
    separate_arguments(one UNIX_COMMAND "${arg}")
    list(GET one 0 kind)
    list(GET one 1 output)
    mlir_tablegen("${output}" ${kind})
  endforeach()
  add_public_tablegen_target(${CT_NAME}IncGen)
  set_property(GLOBAL APPEND PROPERTY CTCOMPILE_TABLEGEN_TARGETS ${CT_NAME}IncGen)
endfunction()
