# === The native fixtures: one JavaScript program per row ===
#
# Every fixture under CTNative/Fixtures/ is registered with
# ctcompile_native_fixture (Native.cmake), which does two things:
#
#   * builds the native module (ctcompile_add_native_pipeline): the
#     closed-world analysis, closure lifting and lowering over the .js. No
#     module is written while any function carries `ctnative.not_native`.
#   * with CLAIMS, adds ctcompile_add_native_claims: the four counts claimed,
#     resolved, direct and lifted, measured when the fixture landed, as
#     floors. A change that stops lifting a function fails here even if the
#     run check is skipped. `lifted` is separate because the lift's own
#     ctjs.call_direct ops are made inside --ctnative-lower-to-emitc, where the
#     resolver's `direct` count cannot see them.
#
# Running the program is the fixture's lit .test beside its .js
# (CTNative/Fixtures/<Group>/<name>.test): it compiles the module without any
# ctbrowser library, builds it with GCC and Clang, runs it, and compares every
# global with the interpreter's answer, plus an off-by-one negative control,
# the deduced-type variant and the printing check. That run is what matters: a
# wrong capture, a copy where a pointer was meant, or the receiver in the wrong
# slot still verifies and compiles cleanly under -Werror, and only the
# interpreter comparison catches it.
#
# Refusals are not here and cannot be: pipeline.cmake writes no module while a
# function carries a diagnostic, and one refusal spreads to its callers and
# callees. They are one program apiece, under split-file, in
# CTNative/Lowering/Closures/refusals.mlir,
# CTNative/Lowering/Objects/constructor-refusals.mlir and
# CTNative/Lowering/Admission/divergence-refusals.mlir. Each fixture's own
# header says what it demonstrates and the numbers it was measured against.

# The divergences from docs/native-divergences.md that the emitter handles:
# `**` and its guard, `%` as fmod, -0, undefined as NaN, and an out-of-range
# index as NaN.
ctcompile_native_fixture(divergence Scalars/divergence.js)
# Divergence ND-8 (docs/native-divergences.md): fractional Number keys name
# properties; integral ones (and -0) name elements.
ctcompile_native_fixture(index_truncation Objects/index-truncation.js)

# Closures converted by lifting. Twenty functions claimed: the top level, nine
# that make a closure, and ten closures.
ctcompile_native_fixture(closures Closures/closure.js CLAIMS 20 9 23 17)
# The receiver is a parameter: sixteen claimed - the top level, seven that
# build a method table, eight methods.
ctcompile_native_fixture(receivers Objects/receiver.js CLAIMS 16 7 8 10)
# An argument is a parameter too: the top level, seven that build a literal,
# seven callees, `both()` making two.
ctcompile_native_fixture(object_arguments Objects/argument.js CLAIMS 16 7 15 9)
# `new` builds a struct in the frame; every call here is the lift's own.
ctcompile_native_fixture(constructors Objects/constructor.js CLAIMS 14 6 7 8)
# A capture filled from the enclosing closure (`enclosing_indices`).
ctcompile_native_fixture(nested_closures Closures/nested-closure.js CLAIMS 27 8 33 27)

# The capture counts the comments cite (219 of bootstrap's 1,023 capture
# operands are the placeholder that carries an index) are checked by
# CTNative/Checks/capture-census.test.

# A captured binding with one dominating write: 18 of 18 claimed, 7 resolved,
# 21 direct from the resolver, 14 from the lift.
ctcompile_native_fixture(hoisted_captures Closures/hoisted-capture.js CLAIMS 18 7 21 14)
# A shared mutable cell, by pointer: 25 of 25, 10, 28 and 20.
ctcompile_native_fixture(shared_cells Closures/shared-cell.js CLAIMS 25 10 28 20)
# A local binding that holds a function: 28 of 28, 8, 16 and 23 (13 of the
# lifted calls are in a frame where the closure is not in scope).
ctcompile_native_fixture(local_functions Closures/local-function.js CLAIMS 28 8 16 23)
# A binding named from two frames away; seven programs, seven answers.
ctcompile_native_fixture(deep_bindings Closures/deep-binding.js CLAIMS 44 9 29 37)
ctcompile_native_fixture(callbacks Closures/callback.js CLAIMS 21 2 13 33)
ctcompile_native_fixture(strings Scalars/string.js CLAIMS 17 12 28 11)
ctcompile_native_fixture(string_snapshots Maps/string-snapshots.js)

# Confined standard Maps: owning identity, primitive keys and numeric values,
# with NaN/signed-zero keys, mutation, aliasing and ordered snapshots.
ctcompile_native_fixture(maps Maps/map.js CLAIMS 22 19 23 2)
# Owned Map handles survive closed factory returns and lifted captures.
ctcompile_native_fixture(map_flow Maps/flow.js CLAIMS 29 22 44 8)
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
ctcompile_native_fixture(map_presence Maps/presence.js CLAIMS 15 14 30 0)
# Tagged optional scalars distinguish null, undefined and present NaN.
ctcompile_native_fixture(optional_scalars Scalars/optional-scalars.js CLAIMS 29 25 119 11)
# A closed scalar union preserves boolean/number identity, including Data's
# short-circuit getter and its retained nested Map environment.
ctcompile_native_fixture(scalar_unions Scalars/scalar-unions.js CLAIMS 24 18 87 18)
