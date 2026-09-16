# --- check-ctcompile: the lit suite -------------------------------------------
#
# SEPARATE FROM ctest ON PURPOSE. The plan: "Keep it separate from the existing
# ctbrowser test targets; the browser's golden and corpus infrastructure stays
# exactly as it is, per the Phase -1 constraints." The tests above are C++
# executables and stay that way; lit is for IR, where the thing under test is
# TEXT and FileCheck is the right instrument.
#
# lit IS NOT A BREW FORMULA and cannot be. The llvm bottle ships FileCheck but
# no llvm-lit, and both Ubuntu's python and brew's refuse `pip install` under
# PEP 668 - so it lives in a virtual environment, and tools/Brewfile says so
# where somebody provisioning a box will read it.
if(CTCOMPILE_ENABLE_MLIR)
  find_program(CTCOMPILE_LIT
    NAMES lit llvm-lit
    HINTS "$ENV{HOME}/.lit-venv/bin" "${LLVM_TOOLS_BINARY_DIR}"
    DOC "The lit test runner, for check-ctcompile")

  if(NOT CTCOMPILE_LIT)
    # SAID OUT LOUD RATHER THAN SKIPPED. A check target that quietly does not
    # exist is indistinguishable from one that passes, and this project has
    # already been bitten once by a test that stopped existing.
    message(STATUS
      "ctcompile: lit not found - check-ctcompile is unavailable. "
      "python3 -m venv ~/.lit-venv && ~/.lit-venv/bin/pip install lit")
  else()
    message(STATUS "ctcompile: lit at ${CTCOMPILE_LIT}")
    # AND TELL AddLLVM WHICH ONE, or a FRESH TREE CANNOT CONFIGURE AT ALL.
    #
    # add_lit_testsuite goes through get_llvm_lit_path, which defaults
    # LLVM_EXTERNAL_LIT to <llvm-prefix>/bin/llvm-lit - and brew's LLVM bottle
    # ships no llvm-lit, so that path does not exist and configure FAILS. The
    # find_program above had already located a working lit two lines earlier;
    # the two were simply never introduced.
    #
    # IT ONLY STARTED MATTERING WHEN ctcompile JOINED THE GATE. This block runs
    # under CTCOMPILE_ENABLE_MLIR, which used to default OFF - so the failure
    # was invisible to anyone whose tree had been configured by hand once and
    # kept a warm cache. Making tools/remote-build.sh default it ON turned that
    # into "the documented build command does not work on a fresh checkout".
    #
    # FORCE, because get_llvm_lit_path reads the CACHE entry, and LLVM's own
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

    # EVERYTHING A RUN LINE NAMES, so `ninja check-ctcompile` builds what lit
    # runs: the tools, the test executables the former `cmake -P` checks drove
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

    # AND RUN BY ctest TOO, so that a green `ctest` means the IR tests ran.
    # Without this the lit suite is a target somebody has to remember, and the
    # gate this repository actually uses is tools/remote-build.sh, which runs
    # ctest and nothing else.
    add_test(NAME ctcompile_lit
             COMMAND "${CTCOMPILE_LIT}" -v "${CMAKE_CURRENT_BINARY_DIR}")
    # Lit has its own CPU-sized worker pool. Reserve those CTest slots so
    # another pool of native compilation tests does not run alongside it.
    include(ProcessorCount)
    ProcessorCount(ctcompile_lit_processors)
    if(ctcompile_lit_processors LESS 1)
      set(ctcompile_lit_processors 1)
    endif()
    # Native ownership cases build both C++ layouts under GCC, Clang and
    # sanitizers, and since 2026-09-15 the ~320 registrations that were
    # `cmake -P` ctest tests - every native fixture's compilation-unit,
    # clean-compile and printing gates, the claims and escape corpora, the
    # launcher arms, the packaging round trip, the Browser drivers - run here
    # too. Measured 567 s alone at -j4; over 2400 s on a box carrying two
    # other ctests and two operator loops, which is what the cap has to hold.
    set_tests_properties(ctcompile_lit PROPERTIES
      TIMEOUT 5400 PROCESSORS ${ctcompile_lit_processors})
  endif()
endif()
