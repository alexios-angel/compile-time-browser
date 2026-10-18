# --- check-ctcompile: the lit suite -------------------------------------------
#
# The lit suite is a separate target from ctbrowser's tests on purpose, so the
# browser's golden and corpus infrastructure is unchanged. C++ tests stay
# executables; lit is for IR, where the output under test is text and FileCheck
# is the right tool.
#
# lit is not installed by Homebrew: the llvm bottle ships FileCheck but no
# llvm-lit. It comes from the uv environment at the repository root instead:
# pyproject.toml's `test` dependency group, pinned by uv.lock, which
# tools/remote-build.sh syncs on the build box (`uv sync --frozen --group test`).
# ~/.lit-venv, the hand-made virtual environment that came before it, is still
# searched after it so boxes provisioned that way keep working.
#
# find_program caches what it finds, so a build tree configured before the uv
# environment existed keeps its old lit until `cmake -U CTCOMPILE_LIT`.
if(CTCOMPILE_ENABLE_MLIR)
  find_program(CTCOMPILE_LIT
    NAMES lit llvm-lit
    HINTS "${CTBROWSER_MONOREPO_ROOT}/.venv/bin" "$ENV{HOME}/.lit-venv/bin"
          "${LLVM_TOOLS_BINARY_DIR}"
    DOC "The lit test runner, for check-ctcompile")

  if(NOT CTCOMPILE_LIT)
    # Report the missing target instead of skipping it silently: a check
    # target that quietly does not exist looks the same as one that passes.
    message(STATUS
      "ctcompile: lit not found - check-ctcompile is unavailable. "
      "Run `uv sync --group test` at the repository root")
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
    # The boxed tier's emitted C++ includes ctbrowser/script headers, which
    # include Boost. %cxx and %cxx_exe (lit.cfg.py) take the include
    # directories the CMake targets get from Boost::headers (find_package in
    # Runtime.cmake), since a Boost installed under a prefix such as Homebrew's
    # is not on the host compiler's default path.
    set(CTCOMPILE_LIT_BOOST_INCLUDE "")
    if(TARGET Boost::headers)
      get_target_property(CTCOMPILE_LIT_BOOST_INCLUDE Boost::headers INTERFACE_INCLUDE_DIRECTORIES)
      if(NOT CTCOMPILE_LIT_BOOST_INCLUDE)
        set(CTCOMPILE_LIT_BOOST_INCLUDE "")
      endif()
    endif()
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

    # Everything the CTNative/Fixtures tests read from the build tree rather
    # than make themselves: every native pipeline module (%{obj}) and the
    # interpreter-linked binary they run nm on. Building only ctjs-opt and
    # ctjs-translate leaves those modules from an older compiler, so the target
    # touches fixture-inputs.stamp once they are current, and lit.cfg.py points
    # %{obj} at a missing directory named after this target while the stamp is
    # missing or older than either tool.
    add_custom_target(ctcompile-fixture-inputs ALL
      COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_CURRENT_BINARY_DIR}/fixture-inputs.stamp"
      COMMENT "Native fixture inputs are current")
    add_dependencies(ctcompile-fixture-inputs ${_native_pipelines} ctcompile-test-type-oracle)
    set(_ctcompile_check_depends
      ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference
      ctcompile-test-type-oracle ctcompile-test-type-claims
      ctcompile-test-escape-claims ctcompile-test-escape-oracle-aot
      ctcompile-test-escape-oracle-aot-return
      ctcompile-test-launcher-vm ctcompile-test-launcher-aot
      ctcompile-test-launcher-page-vm ctcompile-test-launcher-page-aot
      ctcompile-test-native-vm-linked ctcompile-test-native-values-vm-linked
      ctcompile-test-app_bundle ctbrowser-tool-ctrun ctbrowser-tool-ctbrowse
      ctcompile-fixture-inputs
      FileCheck count not)
    add_lit_testsuite(check-ctcompile "Running the ctcompile regression tests"
      "${CMAKE_CURRENT_BINARY_DIR}"
      DEPENDS ${_ctcompile_check_depends})

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

    # The drivers converted to pytest (conftest.py beside lit.cfg.py), run
    # from the same uv environment and the same generated lit.site.cfg.py, so
    # both runners use the same tools. -n auto is lit's all-cores pool;
    # --basetemp keeps each case's %t in the build tree, as lit's Output/ does.
    # A case whose .test still exists runs under both gates until its switch.
    find_program(CTCOMPILE_UV uv DOC "uv, which runs the ctcompile pytest drivers")
    if(NOT CTCOMPILE_UV)
      message(STATUS "ctcompile: uv not found - ctcompile_pytest is unavailable")
    else()
      set(_ctcompile_pytest
        "${CTCOMPILE_UV}" run --project "${CTBROWSER_MONOREPO_ROOT}" --frozen --group test
        pytest -n auto -p no:cacheprovider --ctcompile-build "${CMAKE_BINARY_DIR}"
        --basetemp "${CMAKE_CURRENT_BINARY_DIR}/pytest" "${CMAKE_CURRENT_SOURCE_DIR}")
      add_custom_target(check-ctcompile-pytest
        COMMAND ${_ctcompile_pytest}
        DEPENDS ${_ctcompile_check_depends}
        USES_TERMINAL
        COMMENT "Running the ctcompile pytest drivers")
      add_test(NAME ctcompile_pytest COMMAND ${_ctcompile_pytest})
      set_tests_properties(ctcompile_pytest PROPERTIES
        TIMEOUT 3600 PROCESSORS ${ctcompile_lit_processors})
    endif()
  endif()
endif()
