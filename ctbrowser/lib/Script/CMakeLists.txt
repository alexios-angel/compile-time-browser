# ctbrowser.script - JS -> bytecode -> a register machine over NaN-boxed values, + stdlib.
#
# Reuses ctjs's Pratt parser (vparse.hpp) and replaces everything after it.
# Depends on core only for the warning set - the VM shares no state with the
# DOM by design (one agent per thread). Dependencies are found in
# cmake/dependencies.cmake. Each subdirectory's internal.hpp is private to it
# and deliberately in NO file set.
add_library(ctbrowser-script)
target_sources(ctbrowser-script
  PRIVATE
    aot_bridge/frames.cpp
    aot_bridge/operators.cpp
    aot_bridge/properties.cpp
    aot_bridge/calls.cpp
    dispatch.cpp
    aot_contract.cpp
    program_image/write.cpp
    program_image/read.cpp
    compile.cpp
    # The static semantics: the source text a conforming implementation must
    # REFUSE. It runs before the compiler and shares nothing with it but the
    # AST, which is why it is a directory rather than a pass inside one.
    compile/early_errors/entry.cpp
    compile/early_errors/scopes.cpp
    compile/early_errors/statements.cpp
    compile/early_errors/functions.cpp
    compile/early_errors/expressions.cpp
    compile/early_errors/patterns.cpp
    compile/ast.cpp
    compile/limits.cpp
    compile/entry.cpp
    compile/helpers.cpp
    compile/destructuring.cpp
    compile/statements/dispatch.cpp
    compile/statements/loops.cpp
    compile/statements/try_finally.cpp
    compile/statements/functions.cpp
    compile/expressions.cpp
    compile/classes.cpp
    compile/frames.cpp
    compile/capture.cpp
    compile/strings.cpp
    source_lines.cpp
    type_record.cpp
    builtins.cpp
    builtins/values.cpp
    builtins/collections/array.cpp
    builtins/collections/array_iteration.cpp
    builtins/collections/keyed.cpp
    builtins/collections/typed_arrays.cpp
    builtins/text/string.cpp
    builtins/text/globals.cpp
    builtins/text/regexp.cpp
    builtins/text/symbol.cpp
    builtins/objects/operations.cpp
    builtins/objects/object.cpp
    builtins/objects/errors.cpp
    builtins/objects/proxy.cpp
    builtins/objects/function.cpp
    builtins/async.cpp
    bigint.cpp
    number_format.cpp
    vm/coerce.cpp
    vm/objects/context.cpp
    vm/objects/gc.cpp
    vm/objects/lookup.cpp
    vm/objects/store.cpp
    vm/objects/chain.cpp
    vm/objects/descriptors.cpp
    vm/call/invoke.cpp
    vm/call/construct.cpp
    vm/call/modules.cpp
    vm/call/run.cpp
    vm/call/coroutines.cpp
    # The dispatch loop stays ONE function - docs/history/computed-goto.md.
    vm/run_loop.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/script.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/value.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/bytecode.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/program_image.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/bigint.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/compile.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/source_lines.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/number_format.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/vm.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/type_record.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/builtins.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/regex.hpp")
target_compile_features(ctbrowser-script PUBLIC cxx_std_23)

# All three PRIVATE, and they must be: none changes the shape of a public
# header. The dispatch strategy is run_loop's alone; the debug side tables are
# read through `debug_names_enabled()`; `context::recorder_` is a member in
# EVERY build and only run_loop's recording instantiation disappears
# (`script::type_recording_enabled()` says which). Gating a member on a flag
# would compile this library against one `context` and every consumer against
# another - which links, and then reads the wrong offsets.
if(CTBROWSER_COMPUTED_GOTO)
  target_compile_definitions(ctbrowser-script PRIVATE CTBROWSER_COMPUTED_GOTO)
endif()
if(NOT CTBROWSER_SCRIPT_DEBUG_NAMES)
  target_compile_definitions(ctbrowser-script PRIVATE CTBROWSER_SCRIPT_DEBUG_NAMES=0)
endif()
if(NOT CTBROWSER_SCRIPT_RECORD_TYPES)
  target_compile_definitions(ctbrowser-script PRIVATE CTBROWSER_SCRIPT_RECORD_TYPES=0)
endif()

ctbrowser_target(ctbrowser-script)
target_include_directories(ctbrowser-script PRIVATE
  "${CTBROWSER_CTJS}/include")
target_link_libraries(ctbrowser-script PUBLIC ctbrowser-core Boost::headers)

add_library(ctbrowser::script ALIAS ctbrowser-script)
