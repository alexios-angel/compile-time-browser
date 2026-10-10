# --- check-ctcompile: the lit suite -------------------------------------------
#
# The lit suite is a separate target from ctbrowser's tests on purpose, so the
# browser's golden and corpus infrastructure is unchanged. C++ tests stay
# executables; lit is for IR, where the output under test is text and FileCheck
# is the right tool.
#
# lit is not installed by Homebrew: the llvm bottle ships FileCheck but no
# llvm-lit, and both Ubuntu's and Homebrew's Python refuse `pip install` under
# PEP 668. It lives in a virtual environment instead, and tools/Brewfile says so
# for whoever provisions a machine.
if(CTCOMPILE_ENABLE_MLIR)
  find_program(CTCOMPILE_LIT
    NAMES lit llvm-lit
    HINTS "$ENV{HOME}/.lit-venv/bin" "${LLVM_TOOLS_BINARY_DIR}"
    DOC "The lit test runner, for check-ctcompile")

  if(NOT CTCOMPILE_LIT)
    # Report the missing target instead of skipping it silently: a check
    # target that quietly does not exist looks the same as one that passes.
    message(STATUS
      "ctcompile: lit not found - check-ctcompile is unavailable. "
      "python3 -m venv ~/.lit-venv && ~/.lit-venv/bin/pip install lit")
  else()
    message(STATUS "ctcompile: lit at ${CTCOMPILE_LIT}")
    # Point AddLLVM at this lit. add_lit_testsuite goes through
    # get_llvm_lit_path, which defaults LLVM_EXTERNAL_LIT to
    # <llvm-prefix>/bin/llvm-lit; Homebrew's LLVM ships no llvm-lit, so without
    # this a fresh tree fails to configure.
    #
    # FORCE, because get_llvm_lit_path reads the cache entry, and LLVM's own
    # AddLLVM.cmake has already created it with the wrong default by the time
    # find_package(MLIR) returns.
    set(LLVM_EXTERNAL_LIT "${CTCOMPILE_LIT}" CACHE FILEPATH
      "The lit ctcompile found - see ctcompile/test/CMakeLists.txt" FORCE)
    set(CTCOMPILE_SOURCE_DIR "${PROJECT_SOURCE_DIR}")
    set(CTCOMPILE_BINARY_DIR "${PROJECT_BINARY_DIR}")
    set(CTCOMPILE_TOOLS_DIRS
      "${PROJECT_BINARY_DIR}/tools/ctjs-opt"
      "${PROJECT_BINARY_DIR}/tools/ctjs-translate"
      "${PROJECT_BINARY_DIR}/tools/ctcompile")
    set(LLVM_TOOLS_DIR "${LLVM_TOOLS_BINARY_DIR}")
    configure_lit_site_cfg(
      "${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in"
      "${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py"
      MAIN_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py")

    # Everything a RUN line names, so `ninja check-ctcompile` builds what lit
    # runs: the tools, the C++ test executables the lit tests drive
    # (lit.cfg.py lists them as tool substitutions), ctbrowser's launcher and
    # browser for the packaging round trip, and every native pipeline module
    # (Native.cmake's ctcompile_add_native_pipeline records its target).
    get_property(_native_pipelines GLOBAL PROPERTY CTCOMPILE_NATIVE_PIPELINES)
    add_lit_testsuite(check-ctcompile "Running the ctcompile regression tests"
      "${CMAKE_CURRENT_BINARY_DIR}"
      DEPENDS ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference
              ctcompile-test-type-oracle ctcompile-test-type-claims
              ctcompile-test-escape-claims ctcompile-test-escape-oracle-aot
              ctcompile-test-escape-oracle-aot-return
              ctcompile-test-launcher-vm ctcompile-test-launcher-aot
              ctcompile-test-launcher-page-vm ctcompile-test-launcher-page-aot
              ctcompile-test-native-vm-linked ctcompile-test-native-values-vm-linked
              ctcompile-test-app_bundle ctbrowser-tool-ctrun ctbrowser-tool-ctbrowse
              ${_native_pipelines}
              FileCheck count not)

    # Also run lit from ctest, so a green `ctest` means the IR tests ran.
    # tools/remote-build.sh runs ctest and nothing else, so without this the
    # lit suite would be a target somebody has to remember.
    add_test(NAME ctcompile_lit
             COMMAND "${CTCOMPILE_LIT}" -v "${CMAKE_CURRENT_BINARY_DIR}")
    # Lit has its own CPU-sized worker pool. Reserve those CTest slots so
    # another pool of native compilation tests does not run alongside it.
    include(ProcessorCount)
    ProcessorCount(ctcompile_lit_processors)
    if(ctcompile_lit_processors LESS 1)
      set(ctcompile_lit_processors 1)
    endif()
    # The suite builds native ownership cases under GCC, Clang and sanitizers
    # and runs every native fixture's compile and print checks, the claims and
    # escape corpora, the launcher tests, the packaging round trip and the
    # browser drivers. It took 567 s alone at -j4 and over 2400 s on a loaded
    # machine.
    #
    # The timeout is one hour, like each case inside it (lit.cfg.py): a hung
    # run should fail, not be waited out.
    set_tests_properties(ctcompile_lit PROPERTIES
      TIMEOUT 3600 PROCESSORS ${ctcompile_lit_processors})
  endif()
endif()
