#include "ctcompile/CTJS/IR/CTJSOps.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/FunctionImplementation.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

// VERIFIERS, FOLDERS AND CANONICALIZER HOOKS ONLY - never a parser, a printer,
// a builder or an accessor, all of which TableGen produces.
//
// FOUR OPERATIONS HAVE ONE, and the plan names exactly these four: everything
// else "is expressible with SameOperandsAndResultType, AllTypesMatch, or the
// operand type declarations themselves". A verifier written where a constraint
// would do is a check that runs later and reads worse.
//
// PLUS ONE SYMBOL-USE VERIFIER (Phase 62½-A): ctjs.call_direct's arity against
// the ctjs.func it names is a symbol-table question, which ODS cannot spell
// and which MLIR routes through SymbolUserOpInterface::verifySymbolUses.
//
// AND ONE MORE (Phase 59 slice 1b): ctjs.create_closure's `enclosing_indices`
// against the length of its variadic capture list. That is an ATTRIBUTE
// measured against an OPERAND COUNT, which is the same shape of question the
// bar above admits - no constraint reaches across the two - and the operation's
// description already says the list is parallel rather than packed. Saying it
// twice, once where it can fail, is the point.

#define GET_OP_CLASSES
#include "ctcompile/CTJS/IR/CTJSOps.cpp.inc"

namespace ctcompile::ctjs {

//===----------------------------------------------------------------------===//
// ctjs.constant
//===----------------------------------------------------------------------===//

mlir::LogicalResult ConstantOp::verify() {
    // THE ATTRIBUTE MUST BE ONE OF THIS DIALECT'S, and the reason is -0.0. A
    // builtin FloatAttr compares -0.0 equal to 0.0, so a fold across two
    // constants that differ only in the sign of zero would silently pick
    // either - and `1 / -0` is -Infinity where `1 / 0` is Infinity. The same
    // argument applies to NaN, which no builtin attribute distinguishes from
    // itself.
    //
    // AnyAttr in the ODS and a verifier here, rather than a constrained
    // operand: the set of acceptable attributes is "the ones this dialect
    // defines", which is a predicate over a C++ type rather than something a
    // TableGen constraint expresses.
    const mlir::Attribute value = getValue();
    if (mlir::isa<UndefinedAttr, NullAttr, BooleanAttr, NumberAttr, StringAttr, BigIntAttr>(
            value)) {
        return mlir::success();
    }
    return emitOpError("expects a CTJS constant attribute (undefined, null, boolean, number, "
                       "string or bigint), but got ")
           << value;
}

//===----------------------------------------------------------------------===//
// ctjs.func
//===----------------------------------------------------------------------===//

// THE THREE FunctionOpInterface / CallableOpInterface HOOKS, defined here
// rather than in extraClassDeclaration.
//
// MLIR 22 DECLARES them for the operation and defines none of them, which is a
// combination that produces two different errors depending on which half you
// supply: declaring them inline in the ODS is "class member cannot be
// redeclared", and omitting them entirely is an undefined reference from the
// interface's own Model. Out-of-line definitions are what it wants.
llvm::ArrayRef<mlir::Type> FuncOp::getArgumentTypes() {
    return getFunctionType().getInputs();
}

llvm::ArrayRef<mlir::Type> FuncOp::getResultTypes() {
    return getFunctionType().getResults();
}

mlir::Region * FuncOp::getCallableRegion() {
    return &getBody();
}

mlir::ParseResult FuncOp::parse(mlir::OpAsmParser & parser, mlir::OperationState & result) {
    // hasCustomAssemblyFormat, and the policy requires a written justification
    // for it: a FunctionOpInterface operation's signature is a type attribute
    // built from separately parsed argument and result lists, which
    // assemblyFormat cannot express. MLIR ships the parser for exactly this
    // shape, so this is a call into it rather than a hand-written parser.
    auto buildFuncType = [](mlir::Builder & builder, llvm::ArrayRef<mlir::Type> argTypes,
                            llvm::ArrayRef<mlir::Type> results,
                            mlir::function_interface_impl::VariadicFlag,
                            std::string &) { return builder.getFunctionType(argTypes, results); };
    return mlir::function_interface_impl::parseFunctionOp(
        parser, result, /*allowVariadic=*/false, getFunctionTypeAttrName(result.name),
        buildFuncType, getArgAttrsAttrName(result.name), getResAttrsAttrName(result.name));
}

void FuncOp::print(mlir::OpAsmPrinter & printer) {
    mlir::function_interface_impl::printFunctionOp(printer, *this, /*isVariadic=*/false,
                                                   getFunctionTypeAttrName(), getArgAttrsAttrName(),
                                                   getResAttrsAttrName());
}

mlir::LogicalResult FuncOp::verify() {
    // SIGNATURE CONSISTENCY, which is what the plan asks this verifier for.
    //
    // A JavaScript function returns exactly one value. There is no `void`: a
    // body that falls off the end returns undefined, and the importer emits
    // that explicitly rather than leaving a function with no result.
    if (getFunctionType().getNumResults() != 1) {
        return emitOpError("must return exactly one value - a JavaScript function that falls off "
                           "its end returns undefined, which the importer makes explicit");
    }
    for (const mlir::Type result : getFunctionType().getResults()) {
        if (!mlir::isa<ValueType>(result)) {
            return emitOpError("must return a !ctjs.value, but returns ") << result;
        }
    }
    for (const mlir::Type argument : getFunctionType().getInputs()) {
        if (!mlir::isa<ValueType>(argument)) {
            return emitOpError("takes only !ctjs.value parameters, but takes ") << argument;
        }
    }
    return mlir::success();
}

//===----------------------------------------------------------------------===//
// ctjs.call_direct
//===----------------------------------------------------------------------===//

mlir::LogicalResult CallDirectOp::verifySymbolUses(mlir::SymbolTableCollection & symbols) {
    // THE SYMBOL MUST BE A ctjs.func WITH A BODY, and its entry block must
    // take exactly what this call passes - which is the closed-world claim
    // checked where MLIR checks symbol uses, through the same interface
    // func.call uses. A skipped function emits no ctjs.func at all
    // (ctjs-translate's record_skipped), so "no such symbol" is also what a
    // call into a refusal looks like.
    auto callee = symbols.lookupNearestSymbolFrom<FuncOp>(*this, getCalleeAttr());
    if (!callee) {
        return emitOpError("'") << getCallee() << "' does not name a ctjs.func in this module";
    }
    if (callee.getBody().empty()) {
        return emitOpError("'") << getCallee() << "' has no body to call";
    }
    // THE ENTRY BLOCK'S COUNT, which ctjs.func's own verifier holds equal to
    // its function type: receiver, new.target, callee, then the parameters.
    const unsigned expected = callee.getBody().front().getNumArguments();
    const unsigned passed = static_cast<unsigned>(getArgOperands().size());
    if (passed != expected) {
        return emitOpError("passes ")
               << passed << " operand(s), but the entry block of '" << getCallee() << "' takes "
               << expected << " (receiver, new.target, callee and " << (expected - 3)
               << " parameter(s)) - the resolver pads a short call with undefined and refuses "
                  "a long one";
    }
    return mlir::success();
}

//===----------------------------------------------------------------------===//
// ctjs.push_handler / ctjs.pop_handler
//===----------------------------------------------------------------------===//

// BranchOpInterface, which is what makes the handler block REACHABLE.
//
// Without it the handler is a successor no pass can see operands flowing into,
// and MLIR's own verifier cannot check that the block's arguments match what
// the branch supplies. Modelling push_handler as an ordinary operation instead
// would leave the handler block unreachable in the CFG and every pass would
// treat it as dead code.
mlir::SuccessorOperands PushHandlerOp::getSuccessorOperands(unsigned index) {
    assert(index < 2 && "push_handler has exactly two successors");
    return mlir::SuccessorOperands(index == 0 ? getBodyOperandsMutable()
                                              : getHandlerOperandsMutable());
}

mlir::LogicalResult PushHandlerOp::verify() {
    // THE HANDLER TARGET MUST BE A BLOCK IN THE SAME REGION, which the plan
    // asks for. A successor in another region is not something the unwinder
    // could transfer to: a handler records a FRAME and an address within it.
    mlir::Region * here = getOperation()->getParentRegion();
    if (getHandler()->getParent() != here || getBody()->getParent() != here) {
        return emitOpError("both successors must be blocks in the same region");
    }
    return mlir::success();
}

// ctjs.check IS SHAPED LIKE push_handler AND MEANS THE OPPOSITE END OF THE SAME
// EDGE. push_handler's handler successor exists to keep the pad reachable;
// this one is the edge the emitted code actually takes, and it carries the
// register file AS OF THE THROW rather than as of the `try`.
mlir::SuccessorOperands CheckOp::getSuccessorOperands(unsigned index) {
    assert(index < 2 && "check has exactly two successors");
    return mlir::SuccessorOperands(index == 0 ? getContOperandsMutable()
                                              : getHandlerOperandsMutable());
}

mlir::LogicalResult BinaryStaticOp::verify() {
    switch (getKind()) {
    case BinaryKind::Add:
    case BinaryKind::BitAnd:
    case BinaryKind::BitOr:
    case BinaryKind::BitXor:
    case BinaryKind::Shl:
    case BinaryKind::Shr:
    case BinaryKind::UShr: return mlir::success();
    default:
        return emitOpError("kind ")
               << stringifyBinaryKind(getKind())
               << " has no static form: context::binary_op_static implements add, bitand, "
                  "bitor, bitxor, shl, shr and ushr and answers undefined for the rest";
    }
}

mlir::LogicalResult CheckOp::verify() {
    mlir::Region * here = getOperation()->getParentRegion();
    if (getHandler()->getParent() != here || getCont()->getParent() != here) {
        return emitOpError("both successors must be blocks in the same region");
    }
    return mlir::success();
}

mlir::LogicalResult CatchLandOp::verify() {
    // FIRST IN ITS BLOCK, because ct_aot_catch_land CLEARS the pad marker. A
    // second one is not a second catch, and one reached on a normal path reads
    // a pad nothing set - neither of which the runtime can report.
    if (getOperation() != &getOperation()->getBlock()->front()) {
        return emitOpError("must be the first operation of its block - it consumes the pad "
                           "marker, so anything before it can reach a second one");
    }
    return mlir::success();
}

mlir::LogicalResult PopHandlerOp::verify() {
    // BALANCED WITHIN A BLOCK. The runtime's pop takes the globally innermost
    // handler without consulting the frame, so a body that pops one it never
    // pushed silently takes its CALLER's catch - a bug class the ABI row names
    // explicitly and that nothing at run time reports.
    //
    // WHAT THIS CAN CHECK is one block's worth: a pop with no push before it in
    // the same block is always wrong, because a push is a TERMINATOR and so
    // cannot precede a pop in any other block's straight line. Whole-function
    // balance across the CFG is a dataflow question and belongs to a pass, not
    // to a verifier that sees one operation at a time.
    mlir::Block * block = getOperation()->getBlock();
    if (block == nullptr) { return mlir::success(); }
    for (mlir::Operation & op : *block) {
        if (&op == getOperation()) { break; }
        if (mlir::isa<PopHandlerOp>(op)) {
            return emitOpError("two pops in one block with no push between them - handler balance "
                               "is a compiler invariant and the runtime cannot check it");
        }
    }
    return mlir::success();
}

//===----------------------------------------------------------------------===//
// ctjs.resume_point
//===----------------------------------------------------------------------===//

mlir::LogicalResult ResumePointOp::verify() {
    // INDICES UNIQUE AND DENSE WITHIN THE FUNCTION, which the plan asks for.
    // Phase 14 turns them into a switch over a saved state, and a duplicate
    // index means two states that cannot be told apart while a gap means a
    // switch arm that resumes nowhere.
    auto function = getOperation()->getParentOfType<FuncOp>();
    if (!function) { return mlir::success(); }
    llvm::SmallVector<bool> seen;
    bool duplicated = false;
    function.walk([&](ResumePointOp point) {
        const auto index = static_cast<std::size_t>(point.getIndex());
        if (seen.size() <= index) { seen.resize(index + 1, false); }
        if (seen[index]) { duplicated = true; }
        seen[index] = true;
    });
    if (duplicated) { return emitOpError("resume point indices must be unique in a function"); }
    for (std::size_t i = 0; i < seen.size(); ++i) {
        if (!seen[i]) {
            return emitOpError("resume point indices must be dense: ")
                   << i << " is missing while " << (seen.size() - 1) << " is present";
        }
    }
    return mlir::success();
}

//===----------------------------------------------------------------------===//
// ctjs.create_closure
//===----------------------------------------------------------------------===//

mlir::LogicalResult CreateClosureOp::verify() {
    // PARALLEL WITH THE CAPTURE LIST, NOT PACKED - the same claim the
    // operation's description makes about $upvalues, made about the attribute
    // indexed alongside it. A list holding only the slots filled from the
    // enclosing closure is the plausible wrong reading, and it is wrong
    // SILENTLY: every index past the first from_parent_local slot would name a
    // different capture, and the native lift would pass the wrong binding into
    // a function that compiles clean and prints a wrong number.
    //
    // AN ODS CONSTRAINT CANNOT SAY IT. The length this must match is the number
    // of variadic OPERANDS, which no constraint over an attribute can reach -
    // which is this file's stated bar for writing a verifier at all.
    const mlir::DenseI32ArrayAttr indices = getEnclosingIndicesAttr();
    if (!indices) { return mlir::success(); }
    if (indices.size() != static_cast<std::int64_t>(getUpvalues().size())) {
        return emitOpError("`enclosing_indices` has ")
               << indices.size() << " entries for " << getUpvalues().size()
               << " capture operand(s) - the two are indexed in parallel with the target's "
                  "upvalue descriptors, and a packed list captures the wrong bindings silently";
    }
    // -1 IS THE ONLY SENTINEL. It means "the operand beside me is the cell",
    // and any other negative number is a reader's guess about which.
    const llvm::ArrayRef<std::int32_t> entries = indices.asArrayRef();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i] < -1) {
            return emitOpError("`enclosing_indices[")
                   << i << "]` is " << entries[i]
                   << "; the only negative entry is -1, which means the slot is filled from the "
                      "operand rather than from the enclosing closure";
        }
        // AND THE TWO HALVES OF A SLOT SAY THE SAME THING. An index names an
        // upvalue of the ENCLOSING closure, which is what the VM fills the slot
        // from; the operand beside it is then the importer's placeholder,
        // because there is no binding of this frame to point at. A slot that
        // names an index AND holds a real cell of this frame states both, and
        // one of the two is a lie.
        //
        // IT IS A LIE THAT COMPILES. The native lift asks the operand first
        // (LowerToEmitC.cpp, capturedValue): a ctjs.create_cell wins and the
        // index is never read, so an index written where a cell already stands
        // is silently discarded - and if the index was the truth, the lifted
        // call passes this frame's binding where the enclosing closure's
        // upvalue belonged. The module verifies, lowers, compiles under -Werror
        // and prints a wrong number. The operand encoding this replaced could
        // not express the disagreement at all, because there was only one place
        // to write the answer; an attribute beside an operand can, so it is
        // asked here.
        //
        // WHAT THIS CANNOT SAY is that a permuted list is permuted: the target's
        // upvalue descriptors are not in the module, so `array<i32: 1, 0>` where
        // the descriptors want `0, 1` is two well-formed slots. That one is the
        // importer's to get right, and native-nested-closure-fixture.js is where
        // it would be caught - by the answer, not by a verifier.
        if (entries[i] >= 0 && getUpvalues()[i].getDefiningOp<CreateCellOp>() != nullptr) {
            return emitOpError("`enclosing_indices[")
                   << i << "]` is " << entries[i]
                   << ", so slot " << i
                   << " is filled from the enclosing closure - but its capture operand is a "
                      "ctjs.create_cell of this frame, and the two describe the same slot "
                      "differently; the native lift believes the cell and the index is lost";
        }
    }
    return mlir::success();
}

} // namespace ctcompile::ctjs
