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
  // Escape effects: $initial SINK(stored) - VM_CASE(new_cell) in run_loop.cpp
  // boxes it and the slot is traced (trace_object, vm/objects/gc.cpp). The
  // result is a boxed site (CTJS_BoxedPhase59): escape analysis does not track
  // cells; closure conversion decides their ownership instead.
  let arguments = (ins Arg<CTJS_ValueType, "", [CTJS_SinkStored]>:$initial);
  let results = (outs Res<CTJS_ValueType, "", [CTJS_BoxedPhase59]>:$result);
  let assemblyFormat = "$initial attr-dict";
}

// Escape effects: none (NEITHER). The VM only reads the cell's slot.
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
  // Escape effects: $cell NEITHER; $value SINK(stored) - the slot is traced
  // (trace_object, vm/objects/gc.cpp).
  let arguments = (ins CTJS_ValueType:$cell, Arg<CTJS_ValueType, "", [CTJS_SinkStored]>:$value);
  let assemblyFormat = "$cell `,` $value attr-dict";
}

def CTJS_CreateObjectOp
    : CTJS_RuntimeOp<"create_object", "ct_aot_new_object", [CTJS_MayThrow, CTJS_Safepoint]> {
  let summary = "A fresh empty object";
  // Escape analysis tracks this allocation (one of the two kinds
  // EscapeAnalysis::isTrackedSite accepts; arrays are the other). The VM
  // implements it with make_object(), which calls allocate<object_object>. It
  // has no operands, so nothing to annotate. Tracking is limited to ordinary
  // objects and arrays; property operations still expose their receivers.
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "attr-dict";
}

def CTJS_CreateArrayOp
    : CTJS_RuntimeOp<"create_array", "ct_aot_new_array", [CTJS_MayThrow, CTJS_Safepoint]> {
  let summary = "A fresh array, optionally with its elements";
  // Escape analysis tracks this allocation too (the other of the two kinds).
  // Each element is SINK(stored): items are traced (trace_object,
  // vm/objects/gc.cpp).
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
  // Escape effects: $array NEITHER - context::array_append is a push_back and
  // nothing else (vm/objects/store.cpp); $element SINK(stored) - traced
  // (trace_object, vm/objects/gc.cpp).
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
  // Escape effects: the result is a boxed site with reason not_tracked,
  // because escape analysis does not model RegExp objects.
  let arguments = (ins StrAttr:$pattern, StrAttr:$flags);
  let results = (outs Res<CTJS_ValueType, "", [CTJS_BoxedNotTracked]>:$result);
  let assemblyFormat = "$pattern `,` $flags attr-dict";
}

//===----------------------------------------------------------------------===//

#endif
