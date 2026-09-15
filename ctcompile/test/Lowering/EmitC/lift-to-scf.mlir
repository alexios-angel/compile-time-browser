// RECOVERING THE LOOPS AND BRANCHES THE BYTECODE THREW AWAY.
//
// The importer emits a CFG because bytecode is jumps, and the C++ that falls
// out is `goto label9`. --ctjs-lift-to-scf puts the structure back, using
// upstream's transformCFGToSCF - Bahmann et al.'s reconstruction algorithm,
// which handles even irreducible control flow by inserting edge multiplexers.
//
// WHY THE PASS IS OURS WHEN THE ALGORITHM IS NOT. Upstream ships this as
// --lift-cf-to-scf, and that pass does `op->walk([](func::FuncOp))` - it
// matches by TYPE, not by FunctionOpInterface. `ctjs.func` IS a
// FunctionOpInterface and is NOT a func::FuncOp, so upstream's pass walks the
// module, matches nothing, reports success and changes nothing. Measured on
// this very file's first case: twelve cf operations before, twelve after, zero
// scf. Our pass is that walk and nothing else.
//
// MEASURED ON THE CORPORA, because a structuring pass that only works on
// hand-written IR is worth nothing:
//
//   bootstrap   570 functions   273 already straight-line ->  552 structured (96.8%)
//   p5         4309             2029                       -> 4125 (95.7%)
//   phaser     7628             4523                       -> 7575 (99.3%)
//
// IT IS NOT IN THE COMPILE PIPELINE. The EmitC backend reads a CFG and has no
// conversion for scf, so running this before it would refuse every function it
// structured. It is the foundation of the lexical backend and it earns its
// place by being measurable on its own.

// RUN: ctjs-opt %s --ctjs-lift-to-scf | FileCheck %s
// RUN: ctjs-opt %s '--ctjs-lift-to-scf=report=true' 2>&1 | FileCheck --check-prefix=REPORT %s

// THE COUNT IS ASSERTED, NOT PRINTED IN A COMMENT. One argument is dropped in
// this file, %inv in @invariant below, and no other case here has one. It is a
// remark and not the ODS statistic because Homebrew's release LLVM 23 compiles
// pass statistics out: --mlir-pass-statistics prints an empty report for every
// pass in this project, ResolveGlobals' three included. Measured, same knob:
// native-struct.mlir's program 3 (`acc` in looped and in leaked, and `hi` in
// switching, which the loop only carries; NOT `acc` in reassigned or in
// switching, which are real phis, and never `i`, which is assigned inside
// every one of those loops), native-struct-fixture.js 2 (`acc` and `n` in
// accumulate), native-fixture.js 1, native-pipeline-fixture.js 0.
//
// REPORT: remark: dropped 1 self-carried block argument(s)

// --- A BRANCH BECOMES AN IF -------------------------------------------------
//
// Both arms return, so the whole diamond collapses into one scf.if yielding a
// value - which is what makes the emitted C++ an `if/else` with a result
// rather than two labels and a join.
//
// CHECK-LABEL: ctjs.func @pick
// CHECK-NOT: cf.cond_br
// CHECK: scf.if
// CHECK: scf.yield
// CHECK: else
// CHECK: scf.yield
ctjs.func @pick(%receiver: !ctjs.value, %new_target: !ctjs.value,
                %callee: !ctjs.value, %c: !ctjs.value, %a: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %bit = ctjs.truthy %c
  cf.cond_br %bit, ^yes(%a : !ctjs.value), ^no(%c : !ctjs.value)
^yes(%x: !ctjs.value):
  ctjs.return %x
^no(%y: !ctjs.value):
  ctjs.return %y
}

// --- A BACK EDGE BECOMES A LOOP ---------------------------------------------
//
// The shape every `while` compiles to: a header that tests, a body that jumps
// back. transformCFGToSCF turns it into an scf.while, which is a real loop a
// C++ compiler can reason about rather than a cycle in a goto graph.
//
// SCF.WHILE AND NOT SCF.FOR, and that is not a shortcoming to fix: the trip
// count is not recoverable from bytecode, because `for` and `while` compile to
// the same jumps. Claiming a `for` here would be inventing information.
//
// CHECK-LABEL: ctjs.func @counter
// CHECK-NOT: cf.br
// CHECK: scf.while
ctjs.func @counter(%receiver: !ctjs.value, %new_target: !ctjs.value,
                   %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  cf.br ^head(%n : !ctjs.value)
^head(%i: !ctjs.value):
  %bit = ctjs.truthy %i
  cf.cond_br %bit, ^body(%i : !ctjs.value), ^done(%i : !ctjs.value)
^body(%j: !ctjs.value):
  %next = ctjs.unary neg %j
  cf.br ^head(%next : !ctjs.value)
^done(%k: !ctjs.value):
  ctjs.return %k
}

// --- A VALUE THE LOOP ONLY CARRIES IS NOT CARRIED ---------------------------
//
// The importer's loop header takes the whole register file, so a variable
// assigned before the loop and never inside it arrives as an argument fed by
// itself on the back edge and by the variable on the entry edge - the one
// shape simplifyRegions' dropRedundantArguments leaves alone, because the two
// operands differ. Lifted as it stands it becomes an iteration argument of
// the scf.while, and a result too when it is read after the loop: four values
// through a loop with one counter. For a number that is a copy; for an object
// literal it is a refusal, because its shape is no longer closed (part 24
// Phase 56B, obligation O-3). The pass drops the argument first - a trivial
// phi, Braun et al. 2013 §3.1 - so the loop carries the counter and its
// post-loop export and nothing else. Guard recovery now lets the upstream SCF
// rewrite merge those duplicate counter slots, leaving one loop argument/result.
//
// PROVED LOAD-BEARING: with dropSelfCarriedArguments removed from the pass,
// the invariant would remain loop-carried. The report above still checks the
// CFG removal itself; this shape also checks the subsequent SCF cleanup.
//
// CHECK-LABEL: ctjs.func @invariant
// CHECK-NOT: cf.br
// CHECK: %[[COUNTER:.*]] = scf.while ({{.*}}) : (!ctjs.value) -> !ctjs.value
// CHECK: ctjs.binary add %[[COUNTER]], %arg4
ctjs.func @invariant(%receiver: !ctjs.value, %new_target: !ctjs.value,
                     %callee: !ctjs.value, %n: !ctjs.value, %m: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  cf.br ^head(%n, %m : !ctjs.value, !ctjs.value)
^head(%i: !ctjs.value, %inv: !ctjs.value):
  %bit = ctjs.truthy %i
  cf.cond_br %bit, ^body(%i, %inv : !ctjs.value, !ctjs.value),
                   ^done(%i, %inv : !ctjs.value, !ctjs.value)
^body(%j: !ctjs.value, %jinv: !ctjs.value):
  %next = ctjs.unary neg %j
  cf.br ^head(%next, %jinv : !ctjs.value, !ctjs.value)
^done(%k: !ctjs.value, %kinv: !ctjs.value):
  %sum = ctjs.binary add %k, %kinv
  ctjs.return %sum
}

// --- AND WHAT IT REFUSES, WHICH IS RECORDED RATHER THAN FATAL ---------------
//
// ctjs.push_handler is a TERMINATOR WITH SIDE EFFECTS - it installs a handler -
// and transformCFGToSCF refuses any region containing one, because it reorders
// and duplicates terminators freely.
//
// A REFUSAL MUST NOT FAIL THE RUN. The utility reports one by emitting a
// diagnostic, and an emitted error would fail the whole ctjs-opt invocation -
// so a single try/catch anywhere in a bundle would produce no output at all.
// The pass swallows the diagnostic and records the reason on the function,
// where this project's work lists already live. The function keeps its CFG and
// the CFG backend still compiles it.
//
// CHECK-LABEL: ctjs.func @guarded
// CHECK-SAME: ctjs.not_structured = "{{.*}}side effects{{.*}}"
// CHECK: ctjs.push_handler
ctjs.func @guarded(%receiver: !ctjs.value, %new_target: !ctjs.value,
                   %callee: !ctjs.value, %f: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  ctjs.push_handler ^body(%f : !ctjs.value) catch ^pad(%f : !ctjs.value)
^body(%b: !ctjs.value):
  ctjs.pop_handler
  ctjs.return %b
^pad(%p: !ctjs.value):
  ctjs.return %p
}

// Ordinary property/call effects do not permit simplification to erase the
// importer's register-slot correspondence. The early return and prefix call
// stay outside the non-entry handler, and both checked edges keep two slots.
// This preserves representation only; no callee or effect is proved here.
// CHECK-LABEL: ctjs.func @guarded_property_call
// CHECK-SAME: ctjs.not_structured = "{{.*}}side effects{{.*}}"
// CHECK: cf.cond_br %{{.*}}, ^[[EARLY:bb[0-9]+]]({{.*}}), ^[[INSTALL:bb[0-9]+]]({{.*}})
// CHECK: ^[[EARLY]](%[[EARLY_VALUE:[^:]+]]: !ctjs.value, %{{[^:]+}}: !ctjs.value):
// CHECK: ctjs.return %[[EARLY_VALUE]]
// CHECK: ^[[INSTALL]](%[[PREFIX:[^:]+]]: !ctjs.value, %[[OLD:[^:]+]]: !ctjs.value):
// CHECK-NEXT: %[[GLOBAL:[^ ]+]] = ctjs.load_global "Number"
// CHECK-NEXT: %[[CALLED:[^ ]+]] = ctjs.call %[[GLOBAL]](%{{[^,]+}}, %[[PREFIX]])
// CHECK-NEXT: ctjs.push_handler ^[[BODY:bb[0-9]+]](%[[CALLED]], %[[OLD]] : !ctjs.value, !ctjs.value) catch ^[[PAD:bb[0-9]+]](%[[CALLED]], %[[OLD]] : !ctjs.value, !ctjs.value)
// CHECK: ^[[BODY]](%[[VALUE:[^:]+]]: !ctjs.value, %[[SCRATCH:[^:]+]]: !ctjs.value):
// CHECK-NEXT: %[[PROPERTY:[^ ]+]] = ctjs.get_property %[[VALUE]][%{{[^]]+}}]
// CHECK-NEXT: ctjs.check ^[[CALL:bb[0-9]+]](%[[VALUE]], %[[PROPERTY]] : !ctjs.value, !ctjs.value) caught ^[[PAD]](%[[VALUE]], %[[SCRATCH]] : !ctjs.value, !ctjs.value)
// CHECK: ^[[CALL]](%[[RECEIVER:[^:]+]]: !ctjs.value, %[[METHOD:[^:]+]]: !ctjs.value):
// CHECK-NEXT: %[[RESULT:[^ ]+]] = ctjs.call %[[METHOD]](%[[RECEIVER]])
// CHECK-NEXT: ctjs.check ^[[DONE:bb[0-9]+]](%[[RECEIVER]], %[[RESULT]] : !ctjs.value, !ctjs.value) caught ^[[PAD]](%[[RECEIVER]], %[[METHOD]] : !ctjs.value, !ctjs.value)
// CHECK: ^[[DONE]](%{{[^:]+}}: !ctjs.value, %[[RETURNED:[^:]+]]: !ctjs.value):
// CHECK: ctjs.return %[[RETURNED]]
// CHECK: ^[[PAD]](%[[SAVED:[^:]+]]: !ctjs.value, %{{[^:]+}}: !ctjs.value):
// CHECK: ctjs.catch_land
// CHECK: ctjs.return %[[SAVED]]
ctjs.func @guarded_property_call(%receiver: !ctjs.value, %new_target: !ctjs.value,
                                %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %frame = ctjs.frame_enter 2
  %undefined = ctjs.constant #ctjs.undefined
  %key = ctjs.constant #ctjs.string<"toString">
  %bit = ctjs.truthy %input
  cf.cond_br %bit, ^early(%input, %undefined : !ctjs.value, !ctjs.value),
                   ^install(%input, %undefined : !ctjs.value, !ctjs.value)
^early(%early_value: !ctjs.value, %early_scratch: !ctjs.value):
  ctjs.frame_exit %frame
  ctjs.return %early_value
^install(%prefix: !ctjs.value, %old: !ctjs.value):
  %global = ctjs.load_global "Number"
  %called = ctjs.call %global(%undefined, %prefix)
  ctjs.push_handler ^body(%called, %old : !ctjs.value, !ctjs.value)
    catch ^pad(%called, %old : !ctjs.value, !ctjs.value)
^body(%value: !ctjs.value, %scratch: !ctjs.value):
  %property = ctjs.get_property %value[%key]
  ctjs.check ^call(%value, %property : !ctjs.value, !ctjs.value)
    caught ^pad(%value, %scratch : !ctjs.value, !ctjs.value)
^call(%object: !ctjs.value, %method: !ctjs.value):
  %result = ctjs.call %method(%object)
  ctjs.check ^done(%object, %result : !ctjs.value, !ctjs.value)
    caught ^pad(%object, %method : !ctjs.value, !ctjs.value)
^done(%unused: !ctjs.value, %returned: !ctjs.value):
  ctjs.pop_handler
  ctjs.frame_exit %frame
  ctjs.return %returned
^pad(%saved: !ctjs.value, %old_scratch: !ctjs.value):
  %pad_id, %thrown = ctjs.catch_land
  ctjs.frame_exit %frame
  ctjs.return %saved
}

// Closure/object families still take the existing simplification path, which
// exposes their value identity to the closure and callback analyses.
// CHECK-LABEL: ctjs.func @guarded_allocation
// CHECK-SAME: ctjs.not_structured = "{{.*}}side effects{{.*}}"
// CHECK: %[[OBJECT:[^ ]+]] = ctjs.create_object
// CHECK-NEXT: cf.br ^[[ALLOC_INSTALL:bb[0-9]+]]
// CHECK-NEXT: ^[[ALLOC_INSTALL]]:
// CHECK-NEXT: ctjs.push_handler ^[[NORMAL:bb[0-9]+]] catch ^[[CAUGHT:bb[0-9]+]]
// CHECK: ^[[NORMAL]]:
// CHECK: ctjs.return %[[OBJECT]]
// CHECK: ^[[CAUGHT]]:
// CHECK: ctjs.return %[[OBJECT]]
ctjs.func @guarded_allocation(%receiver: !ctjs.value, %new_target: !ctjs.value,
                             %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %object = ctjs.create_object
  cf.br ^install(%object : !ctjs.value)
^install(%prefix: !ctjs.value):
  ctjs.push_handler ^body(%prefix : !ctjs.value) catch ^pad(%prefix : !ctjs.value)
^body(%value: !ctjs.value):
  ctjs.pop_handler
  ctjs.return %value
^pad(%saved: !ctjs.value):
  ctjs.return %saved
}

// Recover only the importer's exact 1/0 Boolean mux. The upstream while
// rewrite preserves the guarded effect in the body, after the condition.
// CHECK-LABEL: ctjs.func @flag_guard
// CHECK: scf.while
// CHECK-NOT: scf.if
// CHECK: scf.condition
// CHECK: do {
// CHECK: ctjs.store_global "step"
// CHECK-NOT: arith.trunci
// CHECK: ctjs.return
ctjs.func @flag_guard(%receiver: !ctjs.value, %new_target: !ctjs.value,
                     %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %one = arith.constant 1 : i32
  %zero = arith.constant 0 : i32
  %result = scf.while (%value = %n) : (!ctjs.value) -> !ctjs.value {
    %guard = ctjs.truthy %value
    %next, %continue = scf.if %guard -> (!ctjs.value, i32) {
      %step = ctjs.unary neg %value
      ctjs.store_global "step", %step
      scf.yield %step, %one : !ctjs.value, i32
    } else {
      scf.yield %value, %zero : !ctjs.value, i32
    }
    %bit = arith.trunci %continue : i32 to i1
    scf.condition(%bit) %next : !ctjs.value
  } do {
  ^body(%carried: !ctjs.value):
    scf.yield %carried : !ctjs.value
  }
  ctjs.return %result
}

// Two truncates to false, so these flags cannot inherit the branch guard.
// CHECK-LABEL: ctjs.func @non_boolean_guard
// CHECK: scf.if
// CHECK: ctjs.store_global "step"
// CHECK: arith.trunci
// CHECK: scf.condition
ctjs.func @non_boolean_guard(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %two = arith.constant 2 : i32
  %zero = arith.constant 0 : i32
  %result = scf.while (%value = %n) : (!ctjs.value) -> !ctjs.value {
    %guard = ctjs.truthy %value
    %next, %continue = scf.if %guard -> (!ctjs.value, i32) {
      %step = ctjs.unary neg %value
      ctjs.store_global "step", %step
      scf.yield %step, %two : !ctjs.value, i32
    } else {
      scf.yield %value, %zero : !ctjs.value, i32
    }
    %bit = arith.trunci %continue : i32 to i1
    scf.condition(%bit) %next : !ctjs.value
  } do {
  ^body(%carried: !ctjs.value):
    scf.yield %carried : !ctjs.value
  }
  ctjs.return %result
}

// An exit effect must remain in the header, even when the flags are Boolean.
// CHECK-LABEL: ctjs.func @exit_effect
// CHECK: scf.if
// CHECK: else
// CHECK: ctjs.store_global "exit"
// CHECK: scf.condition
ctjs.func @exit_effect(%receiver: !ctjs.value, %new_target: !ctjs.value,
                      %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %one = arith.constant 1 : i32
  %zero = arith.constant 0 : i32
  %result = scf.while (%value = %n) : (!ctjs.value) -> !ctjs.value {
    %guard = ctjs.truthy %value
    %next, %continue = scf.if %guard -> (!ctjs.value, i32) {
      %step = ctjs.unary neg %value
      scf.yield %step, %one : !ctjs.value, i32
    } else {
      ctjs.store_global "exit", %value
      scf.yield %value, %zero : !ctjs.value, i32
    }
    %bit = arith.trunci %continue : i32 to i1
    scf.condition(%bit) %next : !ctjs.value
  } do {
  ^body(%carried: !ctjs.value):
    scf.yield %carried : !ctjs.value
  }
  ctjs.return %result
}

// Repeated if results must not reach the upstream all-uses replacement bug.
// CHECK-LABEL: ctjs.func @duplicate_guard
// CHECK: %[[DUP:.*]]:2 = scf.if
// CHECK: arith.trunci %[[DUP]]#1
// CHECK: scf.condition({{.*}}) %[[DUP]]#0, %[[DUP]]#0
// CHECK: ctjs.store_global "left"
// CHECK: ctjs.store_global "right"
ctjs.func @duplicate_guard(%receiver: !ctjs.value, %new_target: !ctjs.value,
                          %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %one = arith.constant 1 : i32
  %zero = arith.constant 0 : i32
  %result:2 = scf.while (%value = %n) : (!ctjs.value) -> (!ctjs.value, !ctjs.value) {
    %guard = ctjs.truthy %value
    %next, %continue = scf.if %guard -> (!ctjs.value, i32) {
      %step = ctjs.unary neg %value
      scf.yield %step, %one : !ctjs.value, i32
    } else {
      scf.yield %value, %zero : !ctjs.value, i32
    }
    %bit = arith.trunci %continue : i32 to i1
    scf.condition(%bit) %next, %next : !ctjs.value, !ctjs.value
  } do {
  ^body(%left: !ctjs.value, %right: !ctjs.value):
    ctjs.store_global "left", %left
    ctjs.store_global "right", %right
    scf.yield %left : !ctjs.value
  }
  ctjs.return %result#0
}

// A do-while can compute its next value before the guard. Moving that value
// directly to an after-region use would violate sibling-region dominance.
// CHECK-LABEL: ctjs.func @header_passthrough_guard
// CHECK: scf.while
// CHECK: %[[STEP:.*]] = ctjs.unary neg
// CHECK-NOT: scf.if
// CHECK-NOT: arith.trunci
// CHECK: scf.condition({{.*}}) {{.*}}%[[STEP]]
// CHECK: ^bb0({{.*}}%[[CARRIED:.*]]: !ctjs.value):
// CHECK: scf.yield %[[CARRIED]]
ctjs.func @header_passthrough_guard(%receiver: !ctjs.value, %new_target: !ctjs.value,
                                  %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %one = arith.constant 1 : i32
  %zero = arith.constant 0 : i32
  %result = scf.while (%value = %n) : (!ctjs.value) -> !ctjs.value {
    %step = ctjs.unary neg %value
    %guard = ctjs.truthy %step
    %next, %continue = scf.if %guard -> (!ctjs.value, i32) {
      scf.yield %step, %one : !ctjs.value, i32
    } else {
      scf.yield %value, %zero : !ctjs.value, i32
    }
    %bit = arith.trunci %continue : i32 to i1
    scf.condition(%bit) %next : !ctjs.value
  } do {
  ^body(%carried: !ctjs.value):
    scf.yield %carried : !ctjs.value
  }
  ctjs.return %result
}

// Normalizing one loop must not visit an unrelated already-Boolean loop.
// CHECK-LABEL: ctjs.func @unrelated_duplicate_guard
// CHECK: scf.while
// CHECK-NOT: scf.if
// CHECK: scf.condition
// CHECK: scf.while
// CHECK: %[[NEXT:.*]] = scf.if
// CHECK: scf.condition({{.*}}) %[[NEXT]], %[[NEXT]]
// CHECK: ctjs.store_global "left"
// CHECK: ctjs.store_global "right"
ctjs.func @unrelated_duplicate_guard(%receiver: !ctjs.value, %new_target: !ctjs.value,
                                    %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %one = arith.constant 1 : i32
  %zero = arith.constant 0 : i32
  %first = scf.while (%value = %n) : (!ctjs.value) -> !ctjs.value {
    %guard = ctjs.truthy %value
    %next, %continue = scf.if %guard -> (!ctjs.value, i32) {
      %step = ctjs.unary neg %value
      scf.yield %step, %one : !ctjs.value, i32
    } else {
      scf.yield %value, %zero : !ctjs.value, i32
    }
    %bit = arith.trunci %continue : i32 to i1
    scf.condition(%bit) %next : !ctjs.value
  } do {
  ^body(%carried: !ctjs.value):
    scf.yield %carried : !ctjs.value
  }
  %result:2 = scf.while (%value = %first) : (!ctjs.value) -> (!ctjs.value, !ctjs.value) {
    %guard = ctjs.truthy %value
    %next = scf.if %guard -> !ctjs.value {
      %step = ctjs.unary neg %value
      scf.yield %step : !ctjs.value
    } else {
      scf.yield %value : !ctjs.value
    }
    scf.condition(%guard) %next, %next : !ctjs.value, !ctjs.value
  } do {
  ^body(%left: !ctjs.value, %right: !ctjs.value):
    ctjs.store_global "left", %left
    ctjs.store_global "right", %right
    scf.yield %left : !ctjs.value
  }
  ctjs.return %result#0
}

// Moving the outer then block must not admit an unsafe nested while to the
// greedy worklist without a fresh check at the actual pattern application.
// CHECK-LABEL: ctjs.func @nested_duplicate_guard
// CHECK: scf.while
// CHECK-NOT: scf.if
// CHECK: scf.condition
// CHECK: do {
// CHECK: scf.while
// CHECK: %[[INNER:.*]] = scf.if
// CHECK: scf.condition({{.*}}) %[[INNER]], %[[INNER]]
// CHECK: ctjs.store_global "left"
// CHECK: ctjs.store_global "right"
ctjs.func @nested_duplicate_guard(%receiver: !ctjs.value, %new_target: !ctjs.value,
                                 %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %one = arith.constant 1 : i32
  %zero = arith.constant 0 : i32
  %result = scf.while (%value = %n) : (!ctjs.value) -> !ctjs.value {
    %guard = ctjs.truthy %value
    %next, %continue = scf.if %guard -> (!ctjs.value, i32) {
      %inner:2 = scf.while (%current = %value) : (!ctjs.value) -> (!ctjs.value, !ctjs.value) {
        %innerGuard = ctjs.truthy %current
        %step = scf.if %innerGuard -> !ctjs.value {
          %negative = ctjs.unary neg %current
          scf.yield %negative : !ctjs.value
        } else {
          scf.yield %current : !ctjs.value
        }
        scf.condition(%innerGuard) %step, %step : !ctjs.value, !ctjs.value
      } do {
      ^innerBody(%left: !ctjs.value, %right: !ctjs.value):
        ctjs.store_global "left", %left
        ctjs.store_global "right", %right
        scf.yield %left : !ctjs.value
      }
      scf.yield %inner#0, %one : !ctjs.value, i32
    } else {
      scf.yield %value, %zero : !ctjs.value, i32
    }
    %bit = arith.trunci %continue : i32 to i1
    scf.condition(%bit) %next : !ctjs.value
  } do {
  ^body(%carried: !ctjs.value):
    scf.yield %carried : !ctjs.value
  }
  ctjs.return %result
}

// An inner loop's simplification can introduce duplicate forwarding while
// its parent is already on the worklist. Revalidate before every match.
// CHECK-LABEL: ctjs.func @collapsed_duplicate_guard
// CHECK: scf.while
// CHECK-NOT: scf.if
// CHECK: scf.condition
// CHECK: scf.while
// CHECK: %[[COLLAPSED:.*]] = scf.if
// CHECK: scf.condition({{.*}}) %[[COLLAPSED]], %[[COLLAPSED]]
// CHECK: ctjs.store_global "left"
// CHECK: ctjs.store_global "right"
ctjs.func @collapsed_duplicate_guard(%receiver: !ctjs.value, %new_target: !ctjs.value,
                                    %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %one = arith.constant 1 : i32
  %zero = arith.constant 0 : i32
  %first = scf.while (%value = %n) : (!ctjs.value) -> !ctjs.value {
    %guard = ctjs.truthy %value
    %next, %continue = scf.if %guard -> (!ctjs.value, i32) {
      %step = ctjs.unary neg %value
      scf.yield %step, %one : !ctjs.value, i32
    } else {
      scf.yield %value, %zero : !ctjs.value, i32
    }
    %bit = arith.trunci %continue : i32 to i1
    scf.condition(%bit) %next : !ctjs.value
  } do {
  ^body(%carried: !ctjs.value):
    scf.yield %carried : !ctjs.value
  }
  %result:2 = scf.while (%value = %first) : (!ctjs.value) -> (!ctjs.value, !ctjs.value) {
    %guard = ctjs.truthy %value
    %next = scf.if %guard -> !ctjs.value {
      %step = ctjs.unary neg %value
      scf.yield %step : !ctjs.value
    } else {
      scf.yield %value : !ctjs.value
    }
    %stop = arith.constant false
    %alias = scf.while (%copy = %next) : (!ctjs.value) -> !ctjs.value {
      scf.condition(%stop) %copy : !ctjs.value
    } do {
    ^copyBody(%copy: !ctjs.value):
      scf.yield %copy : !ctjs.value
    }
    scf.condition(%guard) %next, %alias : !ctjs.value, !ctjs.value
  } do {
  ^body(%left: !ctjs.value, %right: !ctjs.value):
    ctjs.store_global "left", %left
    ctjs.store_global "right", %right
    scf.yield %left : !ctjs.value
  }
  ctjs.return %result#0
}
