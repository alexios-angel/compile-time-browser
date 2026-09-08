// CTJS bindings operations; included by CTJSOps.td.
#ifndef CTCOMPILE_CTJS_OPS_BINDINGS_TD
#define CTCOMPILE_CTJS_OPS_BINDINGS_TD

// Constants and bindings
//===----------------------------------------------------------------------===//

def CTJS_ConstantOp : CTJS_Op<"constant", [Pure, ConstantLike]> {
  let summary = "Materialize a statically known JavaScript value";
  let description = [{
    PURE, AND ONE OF THE VERY FEW. The policy: mark an operation Pure "only
    when it cannot allocate, cannot throw, cannot call user code, and cannot
    observe or mutate runtime state. In practice that is a very short list."
    A constant qualifies because its value is an attribute - nothing is
    computed, nothing is allocated, and materializing it twice is
    indistinguishable from materializing it once.

    Its attribute must be one of this dialect's constant attributes rather
    than an arbitrary builtin one, which is what the verifier checks: a
    builtin FloatAttr would lose the difference between -0.0 and 0.0.
  }];
  let arguments = (ins AnyAttr:$value);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$value attr-dict";
  let hasVerifier = 1;
}

def CTJS_LoadGlobalOp : CTJS_RuntimeOp<"load_global", "ct_aot_global_get", CTJS_GenericEffects> {
  let summary = "Read a global binding by name";
  let description = [{
    Generic effects because a global may be an accessor on the global object,
    and reading one runs a getter.
  }];
  let arguments = (ins StrAttr:$name);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$name attr-dict";
}

def CTJS_StoreGlobalOp : CTJS_RuntimeOp<"store_global", "ct_aot_global_set", CTJS_GenericEffects> {
  let summary = "Write a global binding by name";
  // PHASE 55: SINK(stored_global). op::set_global writes globals_
  // (run_loop.cpp:252-255), the first root the collector walks
  // (each_root in vm.hpp, walked by vm/objects/gc.cpp).
  let arguments = (ins StrAttr:$name, Arg<CTJS_ValueType, "", [CTJS_SinkStoredGlobal]>:$value);
  let assemblyFormat = "$name `,` $value attr-dict";
}

def CTJS_LoadUpvalueOp : CTJS_Op<"load_upvalue", [CTJS_NoEscapeEffects]> {
  let summary = "Read a captured binding out of its cell";
  let description = [{
    NOT a CTJS_RuntimeOp, and that is a correction. It named ct_aot_cell_get,
    whose only parameter is `(uint64_t cell)` - so the $index went nowhere.
    The lowering matched on arity, passed the closure where the CELL belongs,
    and dropped the index in silence; ct_aot_cell_get "no-ops on a non-cell",
    so every read of a captured variable would have compiled to `undefined`
    rather than to a failure anybody could see.

    IT IS TWO CALLS, which is why no single helper can be named here - the
    same reason ctjs.unary and ctjs.compare are not CTJS_RuntimeOps.
    aot_helpers.def:568-586 spells the decomposition out: get_upvalue is
    ct_aot_upvalue_cell(fr, index), which reads call_frame::closure from the
    FRAME rather than from any operand, and then ct_aot_cell_get on what that
    returns. "Pairing it with ct_aot_cell_get/ct_aot_cell_set reproduces the
    fused opcodes."

    NO GENERIC EFFECTS, which is unusual here and is justified: a cell is a
    box this compiler created, reading one cannot run user code, cannot
    throw, and cannot collect. Both runtime rows agree - ct_aot_upvalue_cell
    and ct_aot_cell_get are (0, 0, 0).
  }];
  // THE CLOSURE IS A !ctjs.value, not a !ctjs.closure, and the change is the
  // importer's doing: the bytecode's get_upvalue reads the CURRENT frame's
  // closure, which arrives as an ordinary function argument - and ctjs.func
  // takes only !ctjs.value parameters. A closure is a value at run time; the
  // distinct type is for the one place that BUILDS one.
  // PHASE 55: $closure NEITHER - a cell read, rows (0,0,0); the closure is
  // never one of this function's tracked sites anyway (only object/array
  // literals are). The result is an external object.
  let arguments = (ins CTJS_ValueType:$closure, I32Attr:$index);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$closure `[` $index `]` attr-dict";
}

def CTJS_StoreUpvalueOp : CTJS_Op<"store_upvalue"> {
  let summary = "Write a captured binding into its cell";
  let description = [{
    NOT a CTJS_RuntimeOp, for the same reason as ctjs.load_upvalue and with
    the same consequence reversed: it named ct_aot_cell_set, the index went
    nowhere, and the write landed on whatever the closure value was - which
    cell_set no-ops on. A captured variable would never have been updated.
  }];
  // PHASE 55: $closure NEITHER (run_loop.cpp:1240-1249 writes a cell slot
  // and stores nothing else); $value SINK(stored) - the slot is traced from
  // the cell through the closure's upvalues (trace_object, vm/objects/gc.cpp).
  let arguments = (ins CTJS_ValueType:$closure, I32Attr:$index,
                       Arg<CTJS_ValueType, "", [CTJS_SinkStored]>:$value);
  let assemblyFormat = "$closure `[` $index `]` `,` $value attr-dict";
}

def CTJS_CreateClosureOp
    : CTJS_RuntimeOp<"create_closure", "ct_aot_make_closure", [CTJS_MayThrow, CTJS_Safepoint]> {
  let summary = "Build a closure over a function and its captured cells";
  let description = [{
    Allocating, so a safepoint and a throw edge for the allocation ceiling -
    but NOT re-entering: building a closure runs none of the function's own
    code.

    ITS FIRST OPERAND WAS A !ctjs.program AND THAT WAS WRONG. Nothing produces
    one - which is why the importer could not emit this operation at all and
    skipped every `closure` opcode, and with it every function that declares
    another. ct_aot_make_closure takes the ENCLOSING CLOSURE as a value,
    because the program is selected from `enclosing->owner`: a context can be
    running functions from more than one program, and a function index means
    nothing outside the one it was compiled in.

    $upvalues IS INDEXED IN PARALLEL WITH THE TARGET'S DESCRIPTORS, not packed.
    Only the entries the compiler marked from_parent_local are read; the helper
    fills the rest from the enclosing closure. A packed list is the plausible
    wrong reading and captures the wrong bindings silently.

    AN ENTRY THE HELPER FILLS IS AN `undefined` PLACEHOLDER, AND $enclosing_indices
    SAYS WHICH UPVALUE FILLS IT. The placeholder keeps the operand list parallel
    with the descriptors and is read by nobody - not the helper, not either
    lowering - and the one fact it cannot carry, WHICH upvalue of the enclosing
    closure the VM copies into the slot, is the attribute's whole business.

    $enclosing_indices IS OPTIONAL AND, WHEN PRESENT, EXACTLY AS LONG AS
    $upvalues - and every slot it NAMES carries the `undefined` placeholder and
    no real binding. Its verifier enforces both, because those are the two ways
    a reader could write a list that means something other than this paragraph
    says, and the second one is the one the lift would act on. Entry i is the
    descriptor's `up.index` when slot i is filled from the enclosing closure,
    and -1 when the operand beside it is the real cell (from_parent_local) - or
    when the index names no upvalue the enclosing frame has, which the VM
    answers with undefined and this answers with the placeholder alone. Absent
    means every slot is from_parent_local. The importer writes it, the boxed
    lowering ignores it exactly as it ignores the placeholder, and the reader
    is the native tier's closure lift (Phase 59 slice 1b, LowerToEmitC.cpp).

    THE INDEX NAMES A CELL AND THE LIFT READS A VALUE, and that is not a
    contradiction. context::make_closure copies `enclosing->upvalues[up.index]`
    - the CELL - into the new closure's slot (make_closure,
    vm/call/coroutines.cpp), and every read of
    that slot goes through ctjs.load_upvalue, which yields `cell->slot`, the
    VALUE (run_loop.cpp, VM_CASE(get_upvalue)). So the lift never wants the box:
    once the enclosing function is lifted, its upvalue `up.index` IS its capture
    parameter - an entry-block argument holding the initial of a cell an outer
    frame proved constant - and the nested closure captures that same constant.

    AN ATTRIBUTE RATHER THAN AN OPERAND, and the difference is the boxed tier's
    bill. Writing the index as a live `ctjs.load_upvalue` of this frame's own
    closure put the same fact in the IR, but CTJSToEmitC parks EVERY capture
    operand into ct_aot_make_closure's argument window - so each one became a
    runtime upvalue read, parked, and then ignored by a helper that fills the
    slot itself. Measured on bootstrap: 219 of the 1,021 capture operands, and
    12,371 more bytes of emitted C++. An attribute costs the boxed tier nothing.

    BOTH NUMBERS ARE REPRODUCIBLE, which is why they are allowed to be here.
    tools/check/capture-census.py counts the operands over the module
    `ctjs-translate --ctbrowser-js-to-ctjs` writes - 573 create_closure, 376
    with a capture list, 1,021 operands, 414 of them ctjs.create_cell, 388 block
    arguments and 219 the placeholder. It scopes SSA names to the ctjs.func that
    defines them, because `%0` exists in all 574 of bootstrap's; on this module
    that precaution does not change the answer, since a capture operand is
    always defined earlier in the same function than the create_closure that
    reads it and an in-order flat table therefore agrees, 414/388/219 either
    way. AND THE 427 AN EARLIER BRIEF REPORTED IS NOT REPRODUCED by any reading
    tried here - not the in-order flat table, not a whole-module two-pass one -
    so where it came from is unknown; 219 is the figure three commits and the
    ctjs.load_upvalue delta agree on. The bytes are a comparison between
    commits, so they name them: bootstrap through the boxed pipeline
    compile-js-to-cpp.cmake spells is 10,976,150 bytes at 69ea710 and at THIS
    encoding, and 10,988,521
    at 0bf7501, where the index was an operand. ctjs.load_upvalue in the
    imported module moves with it: 1,111, 1,111, and 1,330 - and 1,330 - 1,111
    is the same 219.

    AND $enclosing_this IS THE EFFECTIVE RECEIVER, used only when the target is
    an arrow - because an arrow's `this` is decided where it is WRITTEN, not
    where it is called. Reading the effective one rather than the raw receiver
    is what makes an arrow inside an arrow inside a method still see the
    method's object.

    THE RESULT IS A !ctjs.value, NOT A !ctjs.closure, and that is the second
    half of the same correction. The importer's register file is entirely
    !ctjs.value - because at run time a closure IS one, as ctjs.load_upvalue's
    note already says - so a !ctjs.closure result could not be stored in the
    register it is assigned to. The distinct type was "for the one place that
    BUILDS one", and that place cannot use it either: it leaves !ctjs.closure
    and !ctjs.program with no producer AND no consumer.
  }];
  // THE VARIADIC IS LAST, and it has to be: a Variadic followed by a fixed
  // operand cannot be resolved without AttrSizedOperandSegments, and the
  // verifier reports it as a type error on operand #0 rather than as the
  // ordering problem it is. The ABI's own argument order is different again -
  // (fr, closure, index, upvalues, count, this) - and the lowering maps
  // between them, which is what a lowering is for.
  // PHASE 55: $enclosing_closure NEITHER - context::make_closure reads its
  // `owner` and its `upvalues` (make_closure, vm/call/coroutines.cpp) and
  // stores the closure itself nowhere; $enclosing_this SINK(captured) - written
  // to captured_this (make_closure), traced (trace_object, vm/objects/gc.cpp);
  // $upvalues SINK(captured) - written (make_closure), traced (trace_object).
  // The RESULT is a boxed site, reason phase59: a
  // closure is not a tracked site (see the file header) and Phase 59 owns it.
  let arguments = (ins CTJS_ValueType:$enclosing_closure,
                       Arg<CTJS_ValueType, "", [CTJS_SinkCaptured]>:$enclosing_this,
                       I32Attr:$function,
                       Arg<Variadic<CTJS_ValueType>, "", [CTJS_SinkCaptured]>:$upvalues,
                       OptionalAttr<DenseI32ArrayAttr>:$enclosing_indices);
  let results = (outs Res<CTJS_ValueType, "", [CTJS_BoxedPhase59]>:$result);
  let assemblyFormat = "$enclosing_closure `[` $function `]` `this` $enclosing_this"
                       " (`captures` $upvalues^)? attr-dict";
  // THE PARALLEL LIST, ENFORCED. `$enclosing_indices` is meaningless unless it
  // is indexed the same way `$upvalues` is, and a shorter one is precisely the
  // PACKED reading this operation's description refuses - which captures the
  // wrong bindings and says nothing.
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//

#endif
