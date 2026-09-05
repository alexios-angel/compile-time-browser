# === PHASE 63 STEP 5: THE DECLARED DIVERGENCES, AS A PROGRAM ===
#
# ctcompile/docs/native-divergences.md is one row per place the native tier's
# answer differs, or must not differ, from the interpreter's. Half of those
# rows are refusals and are pinned by their text
# (CTNative/Lowering/divergence-refusals.mlir); the other half are EMITTED -
# `**` and its guard, `%` as fmod, -0 through the printing convention,
# undefined carried as NaN where that is exact, and an out-of-range index as
# NaN rather than as undefined behaviour. An emitted divergence is a CLAIM
# THAT THE TWO SIDES AGREE, and part 24 §A.2 says a claim like that is not
# done until a binary and an interpreter have both answered. So the witnesses
# go through the same Phase 62½-D gate every other native program does, and
# through Step 7's two-toolchain compile with them.
#
# One appended block, per part 23 Appendix A.3. `ctcompile_add_native_pipeline`
# is defined inside the block above; a CMake function outlives the `if()` it
# was defined in, so this reads it rather than editing that block.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(divergence
                                "${CMAKE_CURRENT_SOURCE_DIR}/native-divergence-fixture.js")
  # ND-8 is now fixed: the reference truncates indices before bounds checks.
  # Keep the original defect witness as a passing differential regression,
  # with an injected off-by-one proving that the comparison still has teeth.
  ctcompile_add_native_pipeline(index_truncation
                                "${CMAKE_CURRENT_SOURCE_DIR}/native-index-truncation-fixture.js"
                                idx_fractional)
endif()

# === PART 24 PHASE 59 SLICE 1: closures, carried by lifting ===
# ============================================================================
# native-closure-fixture.js is nine functions that each make a closure, put it
# in a local and call it through that local - the shape the tier refused whole
# until the lift: the only closure it carried was a function DECLARATION,
# whose create_closure/store_global pair lowers to nothing. It goes through the
# same Phase 62½-D gate as every other native program: the module is written
# only if no function carries a diagnostic, compiled with no ctbrowser library,
# run, and every global compared with the interpreter's own answer.
#
# THE GATE IS THE POINT, NOT THE IR. A lift that passes the wrong capture, or
# passes the right ones in the wrong order, produces a module that verifies,
# compiles clean under -Werror and prints a wrong number; only the interpreter
# can say so. The parameter ORDER in particular is pinned twice - once as a
# signature in CTNative/Lowering/closure-lift.mlir, once as an answer here.
#
# THE REFUSALS ARE NOT HERE, and cannot be: native-pipeline.cmake refuses to
# write a module while any function carries `ctnative.not_native`, and a
# refusal is contagious in both directions anyway. They are one program apiece
# in CTNative/Lowering/closure-refusals.mlir, under split-file.
#
# One appended block, per part 23 Appendix A.3.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(closures "${CMAKE_CURRENT_SOURCE_DIR}/native-closure-fixture.js")
endif()
# AND THE CLAIMED COUNT, AS A FLOOR. Twenty functions and all of them claimed:
# the top level, nine that make a closure, and ten closures. A change that
# stops lifting one of them fails HERE even if the fixture above were deleted,
# which is what --min-claimed is for. `resolved` and `direct` are the closed
# world's own counters. THE LIFT'S OWN ctjs.call_direct ARE A FOURTH NUMBER,
# `lifted`, because they are made inside --ctnative-lower-to-emitc and the
# resolver's count cannot see them: reading one stage and reporting the total
# is what made three vendor bundles read as 0 for a rewrite that was happening.
# `direct` moved too, because --ctjs-resolve-globals now names a call whose
# callee is a create_closure result - which these fixtures are full of.
if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(closures
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-closure-fixture.js" 20 9 23 17)
endif()

# ============================================================================
# === PART 24: THE RECEIVER IS A PARAMETER - methods lift like closures ===
# ============================================================================
# native-receiver-fixture.js is eight functions whose objects are METHOD
# TABLES: closed-shape literals with numeric fields and function-expression
# fields that reach the data through `this`. That is the shape the tier refused
# whole - `uses `this`` is 6,649 of the 12,916 refusals it makes over the three
# corpora - and the blocker was never that the callee is unknown. It is that no
# C++ type carried a receiver.
#
# THE GATE IS THE POINT, NOT THE IR. A lift that passes the receiver by value
# instead of by pointer produces a module that verifies, compiles clean under
# -Werror and prints 3 where the interpreter prints 9, because the mutation
# never reaches the caller's object. Only the interpreter can say so, and
# `mutating()` calling its method TWICE is what makes the difference show.
#
# THE REFUSALS ARE NOT HERE, and cannot be: native-pipeline.cmake refuses to
# write a module while any function carries `ctnative.not_native`, and a
# refusal is contagious in both directions anyway. They are one program apiece
# in CTNative/Lowering/receiver-refusals.mlir, under split-file.
#
# One appended block, per part 23 Appendix A.3.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(receivers "${CMAKE_CURRENT_SOURCE_DIR}/native-receiver-fixture.js")
endif()
# AND THE CLAIMED COUNT, AS A FLOOR. Sixteen functions and all of them claimed:
# the top level, seven that build a method table, and eight methods. A change
# that stops lifting one of them fails HERE even if the fixture above were
# deleted, which is what --min-claimed is for. `resolved` and `direct` are the
# world's own counters. THE LIFT'S OWN ctjs.call_direct ARE A FOURTH NUMBER,
# `lifted`, because they are made inside --ctnative-lower-to-emitc and the
# resolver's count cannot see them: reading one stage and reporting the total
# is what made three vendor bundles read as 0 for a rewrite that was happening.
# `direct` moved too, because --ctjs-resolve-globals now names a call whose
# callee is a create_closure result - which these fixtures are full of.
if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(receivers
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-receiver-fixture.js" 16 7 8 10)
endif()

# ============================================================================
# === PART 24: AN ARGUMENT IS A PARAMETER TOO - the object carrier at any index
# ============================================================================
# native-object-argument-fixture.js is seven functions whose closed-shape
# literals are PASSED to a call rather than (or as well as) called on. The
# receiver lift made a literal reach a function as a `ctn_x *` and its fields as
# `self->x`; nothing in that carrier was about operand 0, so the same proof
# carries an argument - and `closedAfterLift` stops treating "it is passed to a
# call" as an escape when the callee is one function this rewrite makes direct
# and the parameter is only ever read through constant keys.
#
# WHY IT IS A `this` CHANGE. A method table that was ALSO passed anywhere had an
# open shape, and an open shape refused every method on it by name. `both()` is
# that program.
#
# MEASURED, AND THE NUMBER DID NOT MOVE ON THE CORPORA. All 51 method-bearing
# literals that bootstrap, p5 and phaser block on an argument use pass to an
# OPAQUE callee - `--ctnative-lower-to-emitc=census=1` prints the distribution -
# so the 249 refusals reading "a method field of an object whose shape is not
# closed" are 249 before and after. The fixture is what shows the carrier works;
# the census is what says why the bundles do not benefit.
#
# THE GATE IS THE POINT, NOT THE IR - AND THE PREDICTION HERE WAS WRONG, WHICH
# IS WORTH RECORDING. The expectation was that passing the object BY VALUE would
# verify, compile clean and print 8 where the interpreter prints 40, because the
# write in `mutated()` would never reach the caller. Measured by deleting the
# address-of at the call site: EmitC will not express it at all. `emitc.func`
# rejects an lvalue parameter type and `emitc.call` rejects an lvalue operand
# ("operand #0 must be variadic of type supported by EmitC, but got
# `!emitc.lvalue<!emitc.opaque<"ctn_x">>`"), so the by-value lift is a hard
# verifier error and not a wrong number. What the differential gate DOES catch
# here is the index: an object handed to the wrong parameter position, or a
# receiver and an argument swapped in `both()`, both verify and print wrongly.
#
# THE REFUSALS ARE NOT HERE, and cannot be: native-pipeline.cmake refuses to
# write a module while any function carries `ctnative.not_native`, and a refusal
# is contagious in both directions anyway. They are one program apiece in
# CTNative/Lowering/object-argument-refusals.mlir, under split-file, with the
# census remark asserted alongside them.
#
# One appended block, per part 23 Appendix A.3.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(object_arguments
                                "${CMAKE_CURRENT_SOURCE_DIR}/native-object-argument-fixture.js")
endif()
# AND THE CLAIMED COUNT, AS A FLOOR. Fifteen functions and all of them claimed:
# the top level, seven that build a literal, and seven callees - `both()` making
# two. A change that stops lifting one of them fails HERE even if the fixture
# above were deleted, which is what --min-claimed is for. `resolved` and
# world's own counters. THE LIFT'S OWN ctjs.call_direct ARE A FOURTH NUMBER,
# `lifted`, because they are made inside --ctnative-lower-to-emitc and the
# resolver's count cannot see them: reading one stage and reporting the total
# is what made three vendor bundles read as 0 for a rewrite that was happening.
# `direct` moved too, because --ctjs-resolve-globals now names a call whose
# callee is a create_closure result - which these fixtures are full of.
if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(object_arguments
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-object-argument-fixture.js"
                              16 7 15 9)
endif()

# ============================================================================
# === PART 24: `new` BUILDS A STRUCT IN THE FRAME - the slice before Phase 60
# ============================================================================
# native-constructor-fixture.js is six functions whose objects are built by
# `new` rather than written as literals. The rewrite is two operations that
# already lower - an empty ctjs.create_object for the instance, and the
# ctjs.call_direct the RECEIVER lift already emits for the constructor - so
# after it there is no constructor in the IR at all and the instance is an
# ordinary closed literal. `hasClosedShape`, `groupReceivers`, `fieldsOf`,
# `censusShapes` and `replace` needed no constructor case at all.
#
# THE GATE IS THE POINT, NOT THE IR. `new` that evaluated to the wrong thing
# produces a module that verifies and compiles clean under -Werror and prints
# a different number: a constructor whose body returned an object REPLACES the
# instance in the VM (`produced.is_object_like() ? produced : self`,
# vm/call.cpp), so admitting one would print the instance where the
# interpreter prints the returned object. Only the interpreter can say so.
#
# WHAT IS NOT ADMITTED, AND IT IS THE COMMONEST SHAPE IN REAL CODE: a top-level
# `function Point() {}`. That is stored to a global and `new Point(...)` loads
# it back, so the construct's callee is a `ctjs.load_global` and not a closure.
# Measured: the census row reads "open: the binding is used by ctjs.construct".
# Naming it needs the closed world's per-name verdict, which is the next lever.
#
# THE REFUSALS ARE NOT HERE, and cannot be: native-pipeline.cmake refuses to
# write a module while any function carries `ctnative.not_native`, and a
# refusal is contagious in both directions anyway. They are one program apiece
# in CTNative/Lowering/constructor-refusals.mlir, under split-file.
#
# One appended block, per part 23 Appendix A.3.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(constructors
                                "${CMAKE_CURRENT_SOURCE_DIR}/native-constructor-fixture.js")
endif()
# AND THE CLAIMED COUNT, AS A FLOOR. A change that stops lifting one of them
# fails HERE even if the fixture above were deleted, which is what
# --min-claimed is for. `lifted` is the fourth floor: every one of these calls
# is made by the lift inside --ctnative-lower-to-emitc, where the resolver's
# `direct` count cannot see it.
if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(constructors
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-constructor-fixture.js"
                              14 6 7 8)
endif()

# ============================================================================
# === PART 24 PHASE 59 SLICE 1b: a capture filled from the enclosing closure ===
# ============================================================================
# native-nested-closure-fixture.js is eight functions whose innermost closure
# names a binding two or three frames out. The bytecode compiler boxes that
# binding once, in the frame that owns it; every closure between reaches it
# through its ENCLOSING closure's upvalue, not through a cell of its own frame
# - and that is the shape slice 1 refused whole ("capture N is not a cell of
# this frame"). It is also what a UMD bundle is made of: measured on bootstrap
# before this slice, 15 of the 19 callees a direct call reaches were refused
# for exactly it.
#
# The importer leaves an `undefined` PLACEHOLDER in such a capture operand -
# there is no binding of this frame to point at - and writes the descriptor's
# index on the closure's `enclosing_indices` attribute, parallel with the
# capture list and -1 wherever the operand is the real cell. In the module this
# fixture imports, `mid$25`'s closure for `deep` reads
# `captures %7 {enclosing_indices = array<i32: 0>}` with `%7` a
# `ctjs.constant #ctjs.undefined`. Once the enclosing function is lifted, its
# upvalue k IS its capture parameter - the entry-block argument 3 + k, holding
# a value an outer frame proved constant - and the nested closure lifts on that
# index in the next round of a fixpoint over the classify-then-lift loop. The
# call passes the argument straight on: it already holds the VALUE, never the
# box.
#
# THE INDEX IS ON AN ATTRIBUTE BECAUSE AN OPERAND CHARGED THE BOXED TIER FOR IT.
# Writing it as a live ctjs.load_upvalue of this frame's own closure said the
# same thing, and CTJSToEmitC parked every capture operand into
# ct_aot_make_closure's argument window - 219 runtime upvalue reads on bootstrap
# that the helper then ignored. tools/check/capture-census.py is that census.
#
# THE GATE IS THE POINT, NOT THE IR. Every capture is READ in the innermost
# body, so a lift that passed the wrong value, a stale one, or the cell where
# the value was meant produces a module that verifies, compiles clean under
# -Werror and prints a wrong number - which only the interpreter can say. The
# `_off_by_one` proof (trailing argument) asserts that the comparison can still
# fail, on a global the printing prelude loads after the first one it prints.
#
# THE REFUSED CHAIN IS NOT HERE, and cannot be: native-pipeline.cmake refuses
# to write a module while any function carries `ctnative.not_native`. The
# outer frame that reassigns its binding - refused, with the enclosing
# closure's own reason chained into the sentence - is one program apiece in
# CTNative/Lowering/closure-refusals.mlir, under split-file.
#
# One appended block, per part 23 Appendix A.3.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(nested_closures
                                "${CMAKE_CURRENT_SOURCE_DIR}/native-nested-closure-fixture.js"
                                twice727)
endif()
# AND THE CLAIMED COUNT, AS A FLOOR: the four numbers are the MEASURED ones on
# the day this landed, so a change that stops lifting one level fails HERE
# even if the fixture above were deleted. `lifted` is the lift's own
# ctjs.call_direct count, which the resolver's `direct` cannot see.
if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(nested_closures
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-nested-closure-fixture.js"
                              27 8 33 27)
endif()

# AND THE TWO FIGURES THE COMMENTS CITE, AS A GATE.
#
# CTJSOps.td and BytecodeImport.cpp both state that 219 of bootstrap's 1,021
# capture operands are the placeholder that carries an index. Those had a
# command but no gate, which in this tree is the same failure as a measurement
# with neither: the comment is specification, and nothing was checking it had
# stayed true. `--expect-indexed` is a floor AND a ceiling, so this fails in
# both directions - a change that stops writing the attribute and one that
# starts writing it where a real binding sits are both caught.
#
# IT MEASURES THE IMPORTER ONLY, one pass, no lowering: the module ctjs-translate
# writes for the bundle. That is deliberate - the figure is about what the
# BOXED tier is handed, and the boxed tier sees the importer's output.
if(TARGET ctjs-translate)
  add_test(NAME ctcompile_capture_census_bootstrap
           COMMAND ${CMAKE_COMMAND}
                   -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
                   -DJS=${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/bootstrap/bootstrap.bundle.js
                   -DCENSUS=${CTBROWSER_MONOREPO_ROOT}/tools/check/capture-census.py
                   -DINDEXED=219
                   -DOPERANDS=1021
                   -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/check-capture-census.cmake)
endif()

# ============================================================================
# === PART 24 PHASE 59 SLICE 2 STEP 1: a binding with one dominating write ===
# ============================================================================
# native-hoisted-capture-fixture.js is seven functions whose every capture is a
# hoisted `var`. `compiler_impl::predeclare_locals` hoists each `var`/`let`/
# `const` of a body and BOXES it up front holding `undefined`, so the
# declaration is a `ctjs.cell_set` into an already-built cell and slice 1's
# immutability proof - "a cell nothing ever writes" - read every local binding
# in the language as reassigned. That refusal was 715 closures on phaser, 81 on
# p5 and 16 on bootstrap.
#
# A BINDING WRITTEN ONCE IS CONSTANT AFTER THAT WRITE. The value a lifted call
# prepends is then the STORE'S operand instead of the cell's initial, admitted
# on three dominance clauses: exactly one `ctjs.cell_set`, dominating every
# `ctjs.cell_get`, and dominating every CALL of every closure that captured the
# cell.
#
# THE THIRD CLAUSE IS ABOUT THE CALL AND NOT ABOUT THE `ctjs.create_closure`,
# and that is the whole of this step. A FUNCTION DECLARATION IS HOISTED, so
# `function get() { return n; }` beside `var n = 5` emits its `op::closure` in
# the PROLOGUE, before the store: a rule asking the store to dominate the
# create_closure refused all seven programs here, measured. A closure reads its
# box when it RUNS, so the point that must follow the write is the call site -
# which is also the point lift() prepends the value at.
#
# THE INITIAL IS NOT CHECKED. Clauses 2 and 3 make it unobservable: nothing can
# read the box on a path that skips the store. Requiring a constant `undefined`
# there - the shape the census measured - would have narrowed the rule for a
# claim it does not need.
#
# THE GATE IS THE POINT, NOT THE IR. Every capture is READ in the innermost
# body, so a lift that prepended the cell's INITIAL rather than the stored value
# verifies, compiles clean under -Werror and prints `undefined` as NaN - which
# only the interpreter can say. The `_off_by_one` proof (trailing argument)
# asserts the comparison can still fail; `three32` is a global the printing
# prelude loads after the first one it prints.
#
# THE REFUSED CLAUSES ARE NOT HERE, and cannot be: native-pipeline.cmake refuses
# to write a module while any function carries `ctnative.not_native`. A binding
# written twice, a read the write does not dominate, a write inside a loop whose
# call is after it, and a method whose call site is in another function are one
# program apiece in CTNative/Lowering/closure-refusals.mlir, under split-file.
#
# One appended block, per part 23 Appendix A.3.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(hoisted_captures
                                "${CMAKE_CURRENT_SOURCE_DIR}/native-hoisted-capture-fixture.js"
                                three32)
endif()
# AND THE CLAIMED COUNT, AS A FLOOR: the four numbers are the MEASURED ones on
# the day this landed - 18 of 18 functions claimed, 7 globals resolved, 21
# ctjs.call_direct from the resolver and 14 more from the lift - so a change
# that stops carrying a hoisted binding fails HERE even if the fixture above
# were deleted.
if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(hoisted_captures
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-hoisted-capture-fixture.js"
                              18 7 21 14)
endif()

# ============================================================================
# === PART 24 PHASE 59 SLICE 2 STEP 2: a shared mutable cell, by pointer =====
# ============================================================================
# native-shared-cell-fixture.js is ten functions whose captured binding is
# WRITTEN - by the closure, by two closures, on one path only, inside a loop,
# or twice in the frame. Step 1 copied a binding with one dominating write into
# a parameter; none of these has a single value to copy, and copying the wrong
# one compiles clean and prints a wrong number. `counter()` is the whole slice
# in four lines: `var n = 0; function tick() { n = n + 1; return n; }` answers
# 1 then 2 in the interpreter and 1 then 1 under any lowering that gives each
# call its own copy.
#
# THE BOX BECOMES A FRAME-LOCAL VARIABLE AND THE CAPTURE A POINTER TO IT -
# `double * n`, the receiver lift's `ctn_x * self` one operand along - so the
# emitted callee reads and writes the caller's variable. What makes the pointer
# safe is the escape proof the lift was already making: condition 4 of
# `whyNotLiftable` admits a closure only when every use of its value is a call
# this tier lowers, and `whyCapturesDoNotReach` requires the call to be in the
# frame that owns the binding and dominated by the cell. Relax either and the
# emitted C++ takes the address of a dead frame.
#
# EVERY BINDING HERE IS READ AFTER IT IS WRITTEN, in the frame or through a
# second closure, so a lift that carried a copy, a stale value or the wrong
# slot produces a module that verifies, compiles clean under -Werror and prints
# a wrong number - which only the interpreter can say. The `_off_by_one` proof
# (trailing argument) asserts that the comparison can still fail, on a global
# the printing prelude loads after the first one it prints.
#
# THE REFUSED SHAPES ARE NOT HERE, and cannot be: native-pipeline.cmake refuses
# to write a module while any function carries `ctnative.not_native`. A box
# that escapes into an object literal, a closure over a shared binding that is
# returned, and a target that writes a capture this tier does not carry are one
# program apiece in CTNative/Lowering/closure-refusals.mlir, under split-file.
#
# One appended block, per part 23 Appendix A.3.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(shared_cells
                                "${CMAKE_CURRENT_SOURCE_DIR}/native-shared-cell-fixture.js"
                                twice2)
endif()
# AND THE CLAIMED COUNT, AS A FLOOR: the four numbers are the MEASURED ones on
# the day this landed - 25 of 25 functions claimed, 10 globals resolved, 28
# ctjs.call_direct from the resolver and 20 more from the lift - so a change
# that stops carrying a shared binding fails HERE even if the fixture above
# were deleted.
if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(shared_cells
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-shared-cell-fixture.js"
                              25 10 28 20)
endif()

# ============================================================================
# === PART 24 PHASE 59 SLICE 2 STEP 4: a local binding that holds a function ==
# ============================================================================
# native-local-function-fixture.js is eight functions whose helpers live in
# LOCAL BINDINGS rather than in globals. `var f = function () { ... };` inside a
# function body is a hoisted local, and `compiler_impl::declare_local` boxes it
# exactly when a nested function mentions the name (`is_captured`) - so the
# helper a bundle keeps private is a `ctjs.create_cell`, a
# `ctjs.create_closure`, a `ctjs.cell_set` and then a `ctjs.cell_get` here or a
# `ctjs.load_upvalue` one frame in. Slice 1 refused that closure by the
# MECHANISM: "it reaches `ctjs.cell_set`, which slice 1 does not lower", 87 of
# bootstrap's closure refusals and the terminal of 9 of the 19 chains a
# ctjs.call_direct reaches.
#
# A BINDING WRITTEN ONCE WITH A CLOSURE, AND ONLY EVER CALLED THROUGH, IS THAT
# FUNCTION - the declaration case one scope in. The box, its store and its reads
# lower to nothing and each call through the name becomes a ctjs.call_direct of
# the target, so the helper costs no allocation and no indirection.
#
# EVERY BINDING IN THE FIXTURE IS READ FROM ANOTHER FRAME, which is forced and
# not chosen: a local is a box only when a nested function mentions it, and this
# rule admits a mention only when it is a call. `recur15` is the recursive case
# - the one capture is the binding being defined - and `chain27` is the
# composition, a binding whose function closes over another binding.
#
# THE GATE IS THE ANSWER, NOT THE IR. A rewrite that called the wrong function,
# dropped a call, or removed the wrong capture slot produces a module that
# verifies, compiles clean under -Werror and prints a wrong number - which only
# the interpreter can say. The `_off_by_one` proof (trailing argument) asserts
# that the comparison can still fail, on a global the printing prelude loads
# after the first one it prints.
#
# THE REFUSED SHAPES ARE NOT HERE, and cannot be: native-pipeline.cmake refuses
# to write a module while any function carries `ctnative.not_native`. A name
# assigned twice, a name called before it is assigned, a name that is returned
# rather than called, a name a closure ASSIGNS, a name whose function closes
# over a data binding, and mutual recursion are one program apiece in
# CTNative/Lowering/closure-refusals.mlir, under split-file. A name read two
# frames in was among them until slice 2 step 5, which COMPILES it - see the
# deep_bindings block at the end of this file - and what is refused there now is
# an inner level that fails one of the same questions.
#
# One appended block, per part 23 Appendix A.3.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(local_functions
                                "${CMAKE_CURRENT_SOURCE_DIR}/native-local-function-fixture.js"
                                recur15)
endif()
# AND THE CLAIMED COUNT, AS A FLOOR: the four numbers are the MEASURED ones on
# the day this landed - 28 of 28 functions claimed, 8 globals resolved, 16
# ctjs.call_direct from the resolver and 23 more from the lowering (13 of those
# written by this step, in a frame where the closure value is not in scope) - so
# a change that stops calling a local binding directly fails HERE even if the
# fixture above were deleted.
if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(local_functions
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-local-function-fixture.js"
                              28 8 16 23)
endif()

# === PART 24 PHASE 59 SLICE 2 STEP 5: A BINDING NAMED FROM TWO FRAMES IN ===
#
# Step 4 stopped one frame in. A closure made INSIDE the function that captured
# the box can fill a slot of its own from that slot - `enclosing_indices`, slice
# 1b - and step 4 refused that shape, on two grounds neither of which survived
# being checked: `removeCaptureSlots` has always renumbered a nested closure's
# `enclosing_indices`, and `makeBoundCallDirect` has written calls in frames the
# closure value cannot reach since step 4 itself. So the binding is followed
# inward and the slot is removed at every level it reaches, deepest first.
#
# THE FIXTURE IS SEVEN PROGRAMS AND SEVEN DISTINCT ANSWERS: two frames in,
# three, an inner function that also captures data, a name called at every depth
# it reaches, recursion through two frames, two names travelling through the
# same two closures, and a deep name whose function closes over another binding.
# The gate compiles it, runs it, and compares every global with the interpreter;
# the trailing argument is the negative proof that the comparison can still
# fail, on a global the printing prelude loads after the first one it prints.
#
# THE REFUSED SHAPES ARE NOT HERE, and cannot be: native-pipeline.cmake refuses
# to write a module while any function carries `ctnative.not_native`. An inner
# level that ASSIGNS the binding and an inner level that holds the name as a
# VALUE are one program apiece in CTNative/Lowering/closure-refusals.mlir, under
# split-file, and each pins the chained sentence - the outer level's refusal
# with the inner level's reason after it.
#
# One appended block, per part 23 Appendix A.3.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(deep_bindings
                                "${CMAKE_CURRENT_SOURCE_DIR}/native-deep-binding-fixture.js"
                                two10)
endif()

# AND THE CLAIMED COUNT, AS A FLOOR. Left out of the implementing commit on
# purpose - it had no build and said so rather than invent four numbers, which
# is the right call and is why these are measured here instead.
if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(deep_bindings
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-deep-binding-fixture.js"
                              44 9 29 37)
endif()

if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(callbacks
                                "${CMAKE_CURRENT_SOURCE_DIR}/native-callback-fixture.js"
                                startup42)
endif()

if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(callbacks
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-callback-fixture.js"
                              21 2 13 33)
endif()

if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(strings
                                "${CMAKE_CURRENT_SOURCE_DIR}/native-string-fixture.js"
                                startup42)
endif()
if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(strings
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-string-fixture.js"
                              17 12 28 11)
endif()

# Confined standard Maps: owning identity, primitive keys and numeric values.
# Differential execution includes NaN/signed-zero keys, mutation, aliasing,
# ordered snapshots and argument evaluation through lifted shared cells.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(maps
                                "${CMAKE_CURRENT_SOURCE_DIR}/native-map-fixture.js"
                                numeric275)
endif()
if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(maps
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-map-fixture.js"
                              22 19 23 2)
endif()

# Owned Map handles survive closed factory returns and lifted captures.
# Shared schemas must preserve distinct allocations and formal argument slots.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(map_flow
                                "${CMAKE_CURRENT_SOURCE_DIR}/native-map-flow-fixture.js"
                                lifetime42)
endif()
if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(map_flow
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-map-flow-fixture.js"
                              29 22 44 8)
endif()
