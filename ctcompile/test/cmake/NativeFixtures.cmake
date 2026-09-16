# === THE NATIVE FIXTURES: one JavaScript program per row ===
#
# Every fixture under CTNative/Fixtures/ goes through the same two gates
# Native.cmake defines:
#
#   * `ctcompile_add_native_pipeline` - the closed world, the lift and the
#     lowering over the .js, refusing to write a module while any function
#     carries `ctnative.not_native`; then the Phase 62½-D gate compiles it with
#     no ctbrowser library, runs it, and compares every global with the
#     interpreter's own answer, and Step 7's two-toolchain compile follows. THE
#     GATE IS THE POINT, NOT THE IR: a lift that passes the wrong capture, a
#     copy where a pointer was meant, or the receiver in the wrong slot produces
#     a module that verifies, compiles clean under -Werror and prints a wrong
#     number, and only the interpreter can say so. A trailing global name is
#     the `_off_by_one` negative proof - it must be one the printing prelude
#     loads after the first global it prints, and the driver checks that.
#   * `ctcompile_add_native_claims` - `CLAIMS claimed resolved direct lifted`,
#     the four MEASURED counts on the day the fixture landed, as floors. A
#     change that stops lifting one function fails here even if the pipeline
#     gate were deleted. `lifted` is a fourth number because the lift's own
#     ctjs.call_direct are made inside --ctnative-lower-to-emitc, where the
#     resolver's `direct` count cannot see them - reading one stage and
#     reporting the total is what made three vendor bundles read as 0 for a
#     rewrite that was happening.
#
# THE REFUSALS ARE NOT HERE, and cannot be: pipeline.cmake writes no module
# while a function carries a diagnostic, and a refusal is contagious in both
# directions. They are one program apiece, under split-file, in
# CTNative/Lowering/{Closures,Objects}/*-refusals.mlir and
# CTNative/Lowering/Admission/divergence-refusals.mlir. What each fixture is a
# witness for, and the numbers it was measured against, are in its own header.
#
# SINCE 2026-09-15 THE GATES THEMSELVES ARE LIT: the fixture's .test beside its
# .js (CTNative/Fixtures/<Group>/<name>.test) runs the compilation unit, its
# off-by-one negative proof, the deduced twin, both clean compiles and the
# printing gate over the modules this function has the build write. Only the
# module and the census floors are registered here.
#
# The two functions have different preconditions (claims also needs Python3),
# so each is checked on its own rather than gating the file.
function(ctcompile_native_fixture name rel)
  cmake_parse_arguments(_fx "NO_DEFAULT_OPTIMIZATIONS" "" "CLAIMS" ${ARGN})
  set(_js "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Fixtures/${rel}")
  if(COMMAND ctcompile_add_native_pipeline)
    set(_options)
    if(_fx_NO_DEFAULT_OPTIMIZATIONS)
      set(_options NO_DEFAULT_OPTIMIZATIONS)
    endif()
    ctcompile_add_native_pipeline(${name} "${_js}" ${_options})
  endif()
  if(DEFINED _fx_CLAIMS AND COMMAND ctcompile_add_native_claims)
    ctcompile_add_native_claims(${name} "${_js}" ${_fx_CLAIMS})
  endif()
endfunction()

# Phase 63 Step 5: the EMITTED half of docs/native-divergences.md - `**` and
# its guard, `%` as fmod, -0, undefined as NaN, an out-of-range index as NaN.
ctcompile_native_fixture(divergence Scalars/divergence.js)
# ND-8's defect witness, kept as a passing differential now the reference
# truncates indices before bounds checks.
ctcompile_native_fixture(index_truncation Objects/index-truncation.js)

# Phase 59 slice 1: closures carried by lifting. Twenty functions claimed: the
# top level, nine that make a closure, and ten closures.
ctcompile_native_fixture(closures Closures/closure.js CLAIMS 20 9 23 17)
# The receiver is a parameter: sixteen claimed - the top level, seven that
# build a method table, eight methods.
ctcompile_native_fixture(receivers Objects/receiver.js CLAIMS 16 7 8 10)
# An argument is a parameter too: the top level, seven that build a literal,
# seven callees, `both()` making two.
ctcompile_native_fixture(object_arguments Objects/object-argument.js CLAIMS 16 7 15 9)
# `new` builds a struct in the frame; every call here is the lift's own.
ctcompile_native_fixture(constructors Objects/constructor.js CLAIMS 14 6 7 8)
# Slice 1b: a capture filled from the enclosing closure (`enclosing_indices`).
ctcompile_native_fixture(nested_closures Closures/nested-closure.js CLAIMS 27 8 33 27)

# AND THE TWO FIGURES THE COMMENTS CITE (219 of bootstrap's 1,023 capture
# operands are the placeholder that carries an index) are a gate too:
# CTNative/Checks/capture-census.test.

# Slice 2 step 1: a binding with one dominating write - 18 of 18 claimed, 7
# resolved, 21 direct from the resolver, 14 from the lift.
ctcompile_native_fixture(hoisted_captures Closures/hoisted-capture.js CLAIMS 18 7 21 14)
# Step 2: a shared mutable cell, by pointer - 25 of 25, 10, 28 and 20.
ctcompile_native_fixture(shared_cells Closures/shared-cell.js CLAIMS 25 10 28 20)
# Step 4: a local binding that holds a function - 28 of 28, 8, 16 and 23 (13
# of those written by this step, in a frame where the closure is not in scope).
ctcompile_native_fixture(local_functions Closures/local-function.js CLAIMS 28 8 16 23)
# Step 5: a binding named from two frames in; seven programs, seven answers.
ctcompile_native_fixture(deep_bindings Closures/deep-binding.js CLAIMS 44 9 29 37)
ctcompile_native_fixture(callbacks Closures/callback.js CLAIMS 21 2 13 33)
ctcompile_native_fixture(strings Scalars/string.js CLAIMS 17 12 28 11)
ctcompile_native_fixture(string_snapshots Maps/string-snapshots.js)

# Confined standard Maps: owning identity, primitive keys and numeric values,
# with NaN/signed-zero keys, mutation, aliasing and ordered snapshots.
ctcompile_native_fixture(maps Maps/map.js CLAIMS 22 19 23 2)
# Owned Map handles survive closed factory returns and lifted captures.
ctcompile_native_fixture(map_flow Maps/map-flow.js CLAIMS 29 22 44 8)
# A returned callable owns immutable captures and preserves Map aliasing.
ctcompile_native_fixture(returned_closures Closures/returned-closure.js CLAIMS 35 22 36 23)
# Stored functions own their captures through a returned method table.
ctcompile_native_fixture(method_tables Closures/method-table.js CLAIMS 28 15 24 21)
# A confined ordinary field owns a returned table beyond its container's lifetime.
ctcompile_native_fixture(owned_method_table_slots Closures/owned-method-table-slot.js NO_DEFAULT_OPTIMIZATIONS)
# Finite nested Map schemas own child handles; every child lookup proves presence.
ctcompile_native_fixture(nested_maps Maps/nested-map.js CLAIMS 14 12 25 1)
# Identity-only object keys own fresh allocations across closed calls/returns.
ctcompile_native_fixture(object_keys Maps/object-key.js CLAIMS 14 10 22 12)
# Conditional has/set/get follows must-presence and erasure effects.
ctcompile_native_fixture(map_presence Maps/map-presence.js CLAIMS 15 14 30 0)
# Tagged optional scalars distinguish null, undefined and present NaN.
ctcompile_native_fixture(optional_scalars Scalars/optional-scalars.js CLAIMS 29 25 119 11)
# A closed scalar union preserves boolean/number identity, including Data's
# short-circuit getter and its retained nested Map environment.
ctcompile_native_fixture(scalar_unions Scalars/scalar-unions.js CLAIMS 24 18 87 18)
