//===- CTJSOps.td - every CTJS operation ----------------------------------===//
//
// THE INVENTORY, FOLDED. The plan's original list of ctjs.* names is
// "reorganized to fold variants into kind-carrying operations, per the enum
// rule in the policy. The result is roughly half as many ODS records with
// identical expressive power."
//
// MOST OF THESE ARE CTJS_RuntimeOp, which names a real ctbrowser::aot::helper_id
// enumerator - so an operation cannot claim a helper the runtime does not
// declare, and the check is a C++ compile error rather than a lowering that
// calls the wrong thing.
//
// IT INCLUDES THE TYPES, and that is load-bearing rather than tidy:
// add_mlir_dialect(CTJSOps ctjs) is the only TableGen invocation the policy's
// CMakeLists makes for typedefs, and it runs over THIS file.
//
// IT IS ALSO PHASE 55'S ESCAPE TABLE. Every `Arg<..., [CTJS_Sink...]>` below
// is one cell of the sinks-and-carriers table (25-escape-analysis.md §2.2):
// SINK(reason) hands the operand's object to something that outlives the
// frame or can run user code with it; CTJS_Carry says the result may BE the
// operand's object; an operand with no decorator on an operation that has
// the interface is NEITHER, proved by the VM lines in the comment beside it;
// CTJS_NoEscapeEffects declares an operation whose every operand is NEITHER;
// `Res<..., [CTJS_Boxed...]>` claims an untracked allocation `escapes(reason)`.
// An operation with NO annotation sinks every !ctjs.value operand with reason
// `unknown_op` (the default rule), so omission is sound. ctjs.unary,
// ctjs.compare and ctjs.convert carry none on purpose: their role depends on
// the kind attribute, which a decorator cannot express, and they are the one
// C++ switch in EscapeAnalysis.cpp.
//
// REVIEW RULE: every operation whose bytecode row is `may_reenter 1` (31 of
// 93, bytecode_opcodes.def) has at least one SINK operand, or a comment
// naming the re-entering arm and why no object/array site of the enclosing
// function reaches it. The NEITHER rows are valid for those two tracked kinds
// ONLY - a closure base breaks them (objects.cpp:590 -> call.cpp:502) - which
// is why ctjs.create_closure is not a tracked site.
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
include "ctcompile/CTJS/IR/Ops/Suspension.td"
include "ctcompile/CTJS/IR/Ops/Frames.td"

#endif // CTCOMPILE_CTJS_OPS
