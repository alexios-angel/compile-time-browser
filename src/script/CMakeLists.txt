# ctbrowser.script - JS -> bytecode -> a register machine over NaN-boxed values, + stdlib.
#
# Sources are relative to this directory; headers are named in full under
# ${CTBROWSER_INCLUDE_DIR}. Adding a file to this subsystem now touches this
# file and nothing else - which is why the 897-line src/CMakeLists.txt was
# split on 2026-08-09. Dependencies are found in cmake/dependencies.cmake.

# --- ctbrowser.script ----------------------------------------------------
# The script engine. Reuses ctjs's Pratt parser (vparse.hpp) and replaces everything
# after it; the tree-walk interpreter and the refcounted variant value model
# are gone. Depends on core only for the warning set - the VM shares no state
# with the DOM by design (one agent per thread).
add_library(ctbrowser-script)
target_sources(ctbrowser-script
  PRIVATE
    # Where the code lives: compiled ONCE into the library rather than in every
    # translation unit that uses it. compile.cpp holds the whole compiler -
    # only compiler::compile() is declared in the header.
    compile.cpp
    # The standard library. builtins.cpp is now just install_builtins() calling
    # the five files beside it; builtins/internal.hpp is private to them and is
    # deliberately in NO file set - builtins.hpp still declares one function.
    builtins.cpp
    builtins/values.cpp
    builtins/collections.cpp
    builtins/text.cpp
    builtins/objects.cpp
    builtins/async.cpp
    bigint.cpp
    number_format.cpp
    vm.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/script.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/value.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/bytecode.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/bigint.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/compile.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/number_format.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/vm.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/builtins.hpp"
        # builtins.cpp includes it. Absent from this list until 2026-08-09, so it
        # was compiled against and never installed.
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/regex.hpp")
target_compile_features(ctbrowser-script PUBLIC cxx_std_23)
# PRIVATE: the dispatch strategy is an implementation detail of run_loop and
# nothing a consumer compiles against can see it.
if(CTBROWSER_COMPUTED_GOTO)
  target_compile_definitions(ctbrowser-script PRIVATE CTBROWSER_COMPUTED_GOTO)
endif()

# PUBLIC, unlike the one above, and it MUST be: CTBROWSER_WITH_GMP selects the
# type of `ctbrowser::script::bigint` inside value.hpp, which is a PUBLIC
# header. A private definition would compile this library against mpz_int and
# every consumer against cpp_int - one ODR violation per translation unit, and
# the kind that links cleanly and then reads a bigint through the wrong layout.
#
# The root CMakeLists says why this is opt-in rather than on-when-found. GMP is
# LGPL and this engine links statically; see NOTICE.
if(CTBROWSER_WITH_GMP)
  target_compile_definitions(ctbrowser-script PUBLIC CTBROWSER_WITH_GMP)
  target_include_directories(ctbrowser-script PUBLIC "${CTBROWSER_GMP_INCLUDE_DIR}")
  target_link_libraries(ctbrowser-script PUBLIC "${CTBROWSER_GMP_LIBRARY}")
  message(STATUS "ctbrowser: BigInt backend GMP ${CTBROWSER_GMP_LIBRARY} (LGPL - see NOTICE)")
endif()

ctbrowser_target(ctbrowser-script)
target_include_directories(ctbrowser-script PRIVATE
  "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/include"
  "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/external/compile-time-containers/include")
target_link_libraries(ctbrowser-script PUBLIC ctbrowser-core Boost::headers)

add_library(ctbrowser::script ALIAS ctbrowser-script)
