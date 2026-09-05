// CTJS containers operations; included by CTJSOps.td.
#ifndef CTCOMPILE_CTJS_OPS_CONTAINERS_TD
#define CTCOMPILE_CTJS_OPS_CONTAINERS_TD

// Allocation
//===----------------------------------------------------------------------===//

def CTJS_CreateCellOp
    : CTJS_RuntimeOp<"create_cell", "ct_aot_cell_new", [CTJS_MayThrow, CTJS_Safepoint]> {
  let summary = "Box a value so a closure can share it";
  let description = [{
    A cell is a box this compiler creates, not anything a page can see. It is
    what makes a captured `var` shared rather than copied: two closures over the
    same binding hold the same cell, so one writing it is the other reading it.
  }];
  // PHASE 55: $initial SINK(stored) - run_loop.cpp:1205 boxes it and the
  // slot is traced (objects.cpp:59). The result is a boxed site, reason
  // phase59: cells are Phase 59's, not a tracked kind here.
  let arguments = (ins Arg<CTJS_ValueType, "", [CTJS_SinkStored]>:$initial);
  let results = (outs Res<CTJS_ValueType, "", [CTJS_BoxedPhase59]>:$result);
  let assemblyFormat = "$initial attr-dict";
}

// PHASE 55: NEITHER - run_loop.cpp:1210-1214 reads the slot.
def CTJS_CellGetOp : CTJS_RuntimeOp<"cell_get", "ct_aot_cell_get", [CTJS_NoEscapeEffects]> {
  let summary = "Read a cell's slot";
  let description = [{
    No effects at all, and the runtime row agrees - ct_aot_cell_get is
    (0, 0, 0). Reading a box cannot run user code, throw, or collect.
  }];
  let arguments = (ins CTJS_ValueType:$cell);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$cell attr-dict";
}

def CTJS_CellSetOp : CTJS_RuntimeOp<"cell_set", "ct_aot_cell_set"> {
  let summary = "Write a cell's slot";
  // PHASE 55: $cell NEITHER; $value SINK(stored) - the slot is traced
  // (objects.cpp:59).
  let arguments = (ins CTJS_ValueType:$cell, Arg<CTJS_ValueType, "", [CTJS_SinkStored]>:$value);
  let assemblyFormat = "$cell `,` $value attr-dict";
}

def CTJS_CreateObjectOp
    : CTJS_RuntimeOp<"create_object", "ct_aot_new_object", [CTJS_MayThrow, CTJS_Safepoint]> {
  let summary = "A fresh empty object";
  // PHASE 55: A TRACKED SITE, one of two (EscapeAnalysis::isTrackedSite).
  // run_loop.cpp:552-553 make_object, vm.hpp:245 allocate<object_object>.
  // No operands, so nothing to annotate; the site is a C++ fact because the
  // NEITHER proofs above are valid for this kind and arrays only.
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "attr-dict";
}

def CTJS_CreateArrayOp
    : CTJS_RuntimeOp<"create_array", "ct_aot_new_array", [CTJS_MayThrow, CTJS_Safepoint]> {
  let summary = "A fresh array, optionally with its elements";
  // PHASE 55: A TRACKED SITE, the other of the two. Each element
  // SINK(stored): items are traced (objects.cpp:42).
  let arguments = (ins Arg<Variadic<CTJS_ValueType>, "", [CTJS_SinkStored]>:$elements);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "`[` ($elements^)? `]` attr-dict";
}

def CTJS_AppendOp : CTJS_RuntimeOp<"append", "ct_aot_append", [CTJS_MayThrow, CTJS_Safepoint]> {
  let summary = "Push one element onto an array literal under construction";
  let description = [{
    An array literal is `new_array` followed by one `append` per element, which
    is how the bytecode builds one - so importing `[a, b]` without this means
    importing it as an empty array.
  }];
  // PHASE 55: $array NEITHER - context::array_append is a push_back and
  // nothing else (objects.cpp:161-163); $element SINK(stored) - traced
  // (objects.cpp:42).
  let arguments = (ins CTJS_ValueType:$array, Arg<CTJS_ValueType, "", [CTJS_SinkStored]>:$element);
  let assemblyFormat = "$element `to` $array attr-dict";
}

def CTJS_CreateRegExpOp : CTJS_Op<"create_regexp", [CTJS_MayThrow, CTJS_Safepoint]> {
  let summary = "A RegExp literal";
  let description = [{
    NOT a CTJS_RuntimeOp: aot_helpers.def declares no helper for a regexp
    literal, so there is nothing for getHelperID to name. Saying so here is
    better than pointing it at a helper that does something else - and it is
    a real gap in the ABI rather than in this file.
  }];
  // PHASE 55: a boxed site, reason not_tracked - an untracked kind with no
  // helper.
  let arguments = (ins StrAttr:$pattern, StrAttr:$flags);
  let results = (outs Res<CTJS_ValueType, "", [CTJS_BoxedNotTracked]>:$result);
  let assemblyFormat = "$pattern `,` $flags attr-dict";
}

//===----------------------------------------------------------------------===//

#endif
