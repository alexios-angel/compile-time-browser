//===- CTJSOps.td - every CTJS operation ----------------------------------===//
//
// Variants of one operation share a record and differ by a `kind` enum
// attribute (e.g. ctjs.binary add/sub), which roughly halves the op count.
//
// Most of these are CTJS_RuntimeOp, which names a real
// ctbrowser::aot::helper_id enumerator, so an operation cannot claim a helper
// the runtime does not declare: that is a C++ compile error rather than a
// lowering that calls the wrong thing.
//
// This file includes the types on purpose: add_mlir_dialect(CTJSOps ctjs) is
// the only TableGen invocation that generates typedefs, and it runs over this
// file.
//
// The operand decorators are also the escape-analysis table read by
// lib/CTNative/Analysis/EscapeAnalysis.cpp. Each `Arg<..., [CTJS_Sink...]>`
// is one cell of the sinks-and-carriers table: SINK(reason) hands the
// operand's object to something that outlives the frame or can run user code
// with it; CTJS_Carry says the result may be the operand's object; an operand
// with no decorator, on an operation that has the interface, is NEITHER,
// proved by the VM lines in the comment beside it; CTJS_NoEscapeEffects
// declares an operation whose every operand is NEITHER; and
// `Res<..., [CTJS_Boxed...]>` marks an untracked allocation as
// `escapes(reason)`. An operation with no annotation sinks every !ctjs.value
// operand with reason `unknown_op` (the default rule), so omission is sound.
// ctjs.unary, ctjs.compare and ctjs.convert carry none on purpose: their role
// depends on the kind attribute, which a decorator cannot express, so they are
// handled by the one C++ switch in EscapeAnalysis.cpp.
//
// Review rule: every operation whose row in
// ctbrowser/include/ctbrowser/script/bytecode_opcodes.def has may_reenter 1
// has at least one SINK operand, or a comment naming the re-entering arm and
// why no object or array site of the enclosing function reaches it. The
// NEITHER rows are valid for those two tracked kinds only; a closure base
// breaks them (lookup_property -> ensure_prototype), which is why
// ctjs.create_closure is not a tracked site.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTJS_OPS
#define CTCOMPILE_CTJS_OPS

include "mlir/Interfaces/ControlFlowInterfaces.td"
include "ctcompile/CTJS/IR/CTJSBase.td"
include "ctcompile/CTJS/IR/CTJSTypes.td"
include "ctcompile/CTJS/IR/CTJSAttrs.td"
include "ctcompile/CTJS/IR/CTJSEnums.td"

//===----------------------------------------------------------------------===//
include "ctcompile/CTJS/IR/Ops/Bindings.td"
include "ctcompile/CTJS/IR/Ops/Properties.td"
include "ctcompile/CTJS/IR/Ops/Runtime.td"
include "ctcompile/CTJS/IR/Ops/Operators.td"
include "ctcompile/CTJS/IR/Ops/Containers.td"
include "ctcompile/CTJS/IR/Ops/Modules.td"
include "ctcompile/CTJS/IR/Ops/Calls.td"
include "ctcompile/CTJS/IR/Ops/Functions.td"
include "ctcompile/CTJS/IR/Ops/Exceptions.td"
include "ctcompile/CTJS/IR/Ops/Suspension.td"
include "ctcompile/CTJS/IR/Ops/Frames.td"

#endif // CTCOMPILE_CTJS_OPS
