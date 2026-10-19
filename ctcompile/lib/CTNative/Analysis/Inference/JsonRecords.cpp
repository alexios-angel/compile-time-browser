//===- JsonRecords.cpp - Object literals JSON.stringify serializes ---------===//
//
// `JSON.stringify({v: a, n: b})`: the literal stays a closed-shape C++ struct,
// and the call serializes its fields in the order the literal defined them
// (ctnative::json_record, Runtime/Json.hpp). That order is the object's own
// key order only when every key was defined before the call and none is an
// array index, which this file checks.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/StaticTables.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctnative {

bool TypeInference::passesARecord(mlir::OpOperand & use) {
    // A ctjs.call's operands are the callee, the receiver, then the arguments.
    const BuiltinStatic * row = builtinStaticOf(use.getOwner());
    const unsigned index = use.getOperandNumber();
    return row && index >= 2 && index - 2 < row->arity &&
           row->params[index - 2] == BuiltinStatic::Param::record;
}

std::string TypeInference::stringifiedRecordProblem(mlir::Value object, mlir::Operation * call) {
    auto literal = object.getDefiningOp<ctjs::CreateObjectOp>();
    if (!literal) { return "it is not an object literal"; }
    if (call->getBlock() != literal->getBlock()) {
        return "the call is not in the literal's own block";
    }
    for (mlir::OpOperand & use : object.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (user == call || llvm::isa<ctjs::GetPropertyOp>(user)) { continue; }
        auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
        if (!write || use.getOperandNumber() != 0) {
            return ("it is also used by `" + user->getName().getStringRef() + "`").str();
        }
        const llvm::StringRef key = ctjs::constantKey(write.getKey());
        if (key.empty() || arrayIndexKey(key)) {
            return "a field is written under a key that is not constant or is an array index - "
                   "such keys are enumerated first";
        }
        if (write->getBlock() != literal->getBlock() || !write->isBeforeInBlock(call)) {
            return ("its field `" + key + "` is not written in its own block before the call")
                .str();
        }
    }
    return {};
}

llvm::SmallVector<llvm::StringRef> TypeInference::recordKeys(mlir::Value object) {
    llvm::SmallVector<ctjs::SetPropertyOp> writes;
    for (mlir::Operation * user : object.getUsers()) {
        if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) { writes.push_back(write); }
    }
    // Use lists are not in program order; the block order is.
    llvm::sort(writes,
               [](ctjs::SetPropertyOp a, ctjs::SetPropertyOp b) { return a->isBeforeInBlock(b); });
    llvm::SmallVector<llvm::StringRef> keys;
    for (ctjs::SetPropertyOp write : writes) {
        const llvm::StringRef key = ctjs::constantKey(write.getKey());
        if (!llvm::is_contained(keys, key)) { keys.push_back(key); }
    }
    return keys;
}

} // namespace ctcompile::ctnative
