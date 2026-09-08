// CTJS modules operations; included by CTJSOps.td.
#ifndef CTCOMPILE_CTJS_OPS_MODULES_TD
#define CTCOMPILE_CTJS_OPS_MODULES_TD

// ES modules
//===----------------------------------------------------------------------===//
//
// FOUR OPCODES, AND ONLY ONE OF THEM CAN APPEAR IN A FUNCTION. op::load_import,
// op::bind_export and op::load_namespace are emitted by compile_program's
// module arm alone, so they live in functions[0] of a module program and
// nowhere else; op::dyn_import is the only one an ordinary function can
// contain. Anything testing these has to compile with script_kind::module.

def CTJS_ModuleImportCellOp
    : CTJS_RuntimeOp<"module_import_cell", "ct_aot_module_import_cell", [CTJS_MayThrow]> {
  let summary = "The cell another module exports under a name - an imported binding";
  let description = [{
    A CELL, NOT A VALUE, and that is what makes an imported binding LIVE: the
    importer must hold the very box the exporter writes through. Every later
    read goes through ctjs.cell_get, and the importer must NOT box it again -
    compile/frames.cpp marks the local boxed and deliberately emits no
    new_cell, so boxing a box would leave every read seeing a cell containing a
    cell.

    BOTH NAMES ARE ATTRIBUTES BECAUSE BOTH ARE COMPILE-TIME CONSTANTS. The
    specifier is a decoded string literal and the export name is an identifier,
    both read out of the source by the compiler - which is exactly what a
    dynamic import's specifier is not.

    $specifier IS THE SPECIFIER AS WRITTEN, not as the registry is keyed. The
    translation lives in module_record::resolved because `./dep.js` names a
    different file depending on which module wrote it, and the bytecode can
    only carry what was written. The helper does that lookup; baking a resolved
    name here would make the image's answer the compiler's guess.

    THE OPCODE'S OPERANDS ARE THE OTHER WAY ROUND. op::load_import reads b as
    the EXPORT NAME and c as the SPECIFIER - the only opcode that reads c as a
    standalone index - so an importer that fills these in reading order gets
    them swapped, and the mistake compiles and raises a plausible message.

    RAISE TIER: a module that was not loaded and a name it does not export are
    both uncatchable engine faults, not the ReferenceError a spec-conformant
    engine would throw. The helper answers undefined on those paths and a
    caller polls ct_aot_failed at a back edge. NOT a safepoint - it allocates
    nothing, and neither MayReenterJS: two flat_map lookups run no user code.
  }];
  let arguments = (ins StrAttr:$specifier, StrAttr:$export_name);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$export_name `from` $specifier attr-dict";
}

def CTJS_ModuleExportCellOp
    : CTJS_RuntimeOp<"module_export_cell", "ct_aot_module_export_cell",
                     [CTJS_MayThrow, CTJS_Safepoint]> {
  let summary = "Adopt this module's export cell for a name";
  let description = [{
    DATA FLOWS RECORD -> REGISTER, backwards from both the opcode's name and
    bytecode.hpp's comment on it. It ADOPTS the cell the record already holds
    rather than publishing the register's, because instantiate_module created
    every cell before ANY module in the graph ran and a cyclic importer may
    already be holding one - overwriting the record would hand that importer a
    box nobody ever writes to again.

    $current IS THE DESTINATION REGISTER'S VALUE, AND IT IS NOT DECORATION.
    op::bind_export's write is CONDITIONAL: outside a module the interpreter
    writes nothing at all, and the register holds the local being exported, so
    a lowering that stored undefined would destroy it. The ABI row asked for a
    CT_AOT_NO_WRITE status to express that; ct_aot_status has four members and
    none of them is that one. So the lowering seeds the helper's out-parameter
    with this operand instead, and "does not write" becomes "writes back what
    was there" - the same thing, and the only one of the two a register-to-SSA
    translation can say.

    The helper has no parameter for it, which is legal and is why
    CTJSABIShape.cpp checks attributes rather than operands: an operation
    supplying fewer arguments than the ABI takes is the normal case, and this
    supplies one MORE, which the lowering materialises into the out-slot's
    initialiser exactly as ctjs.get_property materialises a null inline cache.

    A SAFEPOINT because of the fallback allocation, which is live rather than
    theoretical: browser::instantiate_module is the only caller of
    context::instantiate_module in the tree, so a host that evaluates a module
    through run_module without instantiating it first arrives with an empty
    exports map and the cell is created here.

    THE NAME COMES FROM bx(), NOT b. b is the high half of the pair and is 0
    for every module with fewer than 65,536 names, so an importer reading b
    alone exports name 0 under this name - silently.
  }];
  // PHASE 55: $current SINK(stored). Alias-returning: module_export_cell
  // (vm/call/modules.cpp) returns `current` itself outside a module, and
  // otherwise adopts a record cell that the module's export table roots
  // (each_root, vm.hpp).
  let arguments = (ins StrAttr:$name, Arg<CTJS_ValueType, "", [CTJS_SinkStored]>:$current);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$name `adopting` $current attr-dict";
}

def CTJS_ModuleNamespaceOp
    : CTJS_RuntimeOp<"module_namespace", "ct_aot_module_namespace",
                     [CTJS_MayThrow, CTJS_Safepoint]> {
  let summary = "The namespace object of another module - `import * as ns`";
  let description = [{
    KEPT SEPARATE FROM ctjs.module_import_cell for a reason stronger than the
    bytecode having made the distinction: the two RESULT KINDS lower
    differently downstream. A cell's reads all funnel through ctjs.cell_get; a
    namespace's are ordinary property reads, and every one of them is an
    ACCESSOR CALL, because context::module_namespace installs a native getter
    per export rather than copying values. One operation with a nullable name
    would turn a static property of the lowering into a run-time one.

    IDENTITY-CACHED PER MODULE, in module_record::namespace_object: two
    `import * as` of the same module are required to be the SAME object, and
    code compares namespaces by identity. A lowering that built one per
    operation would answer false for `ns1 === ns2` and be right about
    everything else.

    A SAFEPOINT: it allocates the object and one native_object per export. Safe
    under a real collector because the half-built object is stored into
    namespace_object - GCRoots.def's `module_namespace` row - before the
    accessor loop runs.

    NOT MayReenterJS. Building the namespace installs getters and invokes none
    of them; the re-entry cost is paid later, by whatever reads a property off
    it, and that is ctjs.get_property's row rather than this one.
  }];
  let arguments = (ins StrAttr:$specifier);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$specifier attr-dict";
}

def CTJS_DynamicImportOp
    : CTJS_RuntimeOp<"dynamic_import", "ct_aot_dynamic_import", CTJS_GenericEffects> {
  let summary = "`import(specifier)` - a promise for a module's namespace";
  let description = [{
    THE ONLY MODULE OPERATION WHOSE SPECIFIER IS AN OPERAND, which is why it is
    the only one an ordinary function can contain: the other three are emitted
    by compile_program's module arm from text the compiler read.

    THE REFERRER IS NOT HERE, AND MUST NOT BE. function_proto::module is
    stamped by the LOADER after compilation - "the compiler cannot do this: a
    specifier is the loader's name for a file" - and is absent from
    engine_contract::proto_header, so an image does not carry it either. The
    helper reads it off the frame. Baking a literal would leave bare and
    absolute specifiers working and break exactly the relative ones, which is
    the worst failure distribution available.

    GENERIC EFFECTS, AND EARNED: two independent user-JavaScript re-entries
    before it returns - the specifier's toString, then a whole module graph
    through run_reentrant - and the loader INSERTS into modules_, so nothing
    may hold a module_record reference across it.

    A MISSING MODULE IS NOT A CONTROL-FLOW EVENT. It answers CT_AOT_OK carrying
    an already-REJECTED settled promise, because that is what the embedder's
    loader returns; only "no loader is installed at all" is a failure, and that
    one is CT_AOT_FAILED rather than a quiet no-write, because the interpreter
    raise()s there and stops.
  }];
  // PHASE 55 (may_reenter 1: dyn_import): SINK(converted) - the specifier's
  // toString is user code.
  let arguments = (ins Arg<CTJS_ValueType, "", [CTJS_SinkConverted]>:$specifier);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$specifier attr-dict";
}

//===----------------------------------------------------------------------===//

#endif
