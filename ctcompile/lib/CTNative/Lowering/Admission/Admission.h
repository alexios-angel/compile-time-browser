#pragma once

#include "../LoweringSupport.h"

namespace ctcompile::ctnative {
class OwnedMethodTableSlots;
class OwnedGlobalRoots;
} // namespace ctcompile::ctnative

namespace ctcompile::ctnative::lowering_detail {

struct admission {
    mlir::DataFlowSolver & solver;
    std::string why;
    // Null in the unit tests that construct an admission directly; every path
    // through it then reads a literal's own uses, which is what it did before
    // a receiver could be a parameter.
    const receiverGroups * groups = nullptr;
    const OwnedMethodTableSlots * ownedTableSlots = nullptr;
    const OwnedGlobalRoots * ownedGlobals = nullptr;
    // The carrier every `return` in the function agrees on; `none` until the
    // first return is seen. A function with no return at all returns NaN -
    // undefined's carrier - which lower() picks when this stays `none`.
    carrier returns = carrier::none;
    [[nodiscard]] mlir::Type typeOf(mlir::Value v) const;

    bool refuse(std::string reason);

    bool numeric(mlir::Value v, llvm::StringRef where);

    bool boolean(mlir::Value v, llvm::StringRef where);

    bool printable(mlir::Value v, llvm::StringRef where);
    bool identityField(mlir::Operation * op);
    bool ownedTableField(ctjs::SetPropertyOp store);
    bool ownedGlobalOperation(mlir::Operation * op);
    bool ownedGlobalValue(mlir::Value value) const;

    static bool isDeclarationClosure(mlir::Operation * o);

    static bool isDeclarationStore(mlir::Operation * o);

    static bool isLiftedClosure(mlir::Operation * o);

    static bool isUnboxedCell(mlir::Operation * o);

    static bool closureLowersToNothing(mlir::Operation * o);

    static std::string closureRefusal(mlir::Operation * o);

    static bool feedsOnlyDirectCallees(mlir::Value v);

    static bool isClosedObject(mlir::Value v);

    static llvm::ArrayRef<int32_t> objectArgsOf(mlir::Operation * o);

    static bool isObjectArg(mlir::Operation * o, unsigned index);

    static llvm::ArrayRef<int32_t> cellArgsOf(mlir::Operation * o);

    static bool isCellArg(mlir::Operation * o, unsigned index);

    static bool isCarriedCell(mlir::Operation * o);

    static bool isCellParameter(mlir::Value v);

    static bool namesASharedCell(mlir::Value v);

    static llvm::StringRef keyOf(mlir::Value key);

    static bool isCIdentifier(llvm::StringRef key);

    static bool isReservedInCpp(llvm::StringRef key);

    static bool namesObjectPrototypeMember(llvm::StringRef key);

    static bool isKeyOnlyString(mlir::Operation * o);

    static bool isVectorSite(mlir::Value v);

    static std::string whyNotDense(mlir::Value array);

    static bool isVectorKeyString(mlir::Operation * o);

    static std::string argumentReason(mlir::Value object);

    static std::string whyOpen(mlir::Value object);

    static std::string whyOpenReceiver(mlir::Value self);

    static bool lowersToNothing(mlir::Value v);

    bool op(mlir::Operation * o);

    bool function(ctjs::FuncOp fn);
    bool exceptionRegion(ctjs::TryOp attempt);
};

} // namespace ctcompile::ctnative::lowering_detail
