#pragma once

#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "mlir/IR/Dominance.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"

#include <optional>

namespace ctcompile::ctnative::host_detail {

struct analyzer {
    mlir::ModuleOp module;
    const HostContract & contract;
    unsigned remaining;
    bool exhausted = false;
    bool ambiguousFunctions = false;
    ctjs::FuncOp entry;
    mlir::DominanceInfo dominance;
    llvm::StringMap<llvm::SmallVector<ctjs::StoreGlobalOp>> globals;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Operation *>> callers;
    llvm::DenseMap<mlir::Operation *, ctjs::FuncOp> indirectFactories;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::ReturnOp>> returns;
    llvm::DenseMap<unsigned, ctjs::FuncOp> functions;
    llvm::DenseSet<mlir::Value> evaluating;
    std::vector<HostCallableEdge> checkedCalls;
    llvm::DenseMap<mlir::Operation *, HostCallableEdge> capturedCalls;
    llvm::DenseSet<mlir::Operation *> capturedOperations;
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> capturedResults;

    analyzer(mlir::ModuleOp module, const HostContract & contract, unsigned steps);
    bool step();
    ctjs::FuncOp target(mlir::Operation * call) const;
    ctjs::FuncOp callable(mlir::Value value, unsigned depth = 0);
    ctjs::SetPropertyOp currentWrite(ctjs::GetPropertyOp read, unsigned depth = 0);
    std::optional<HostCallableEdge> propertyCall(mlir::Operation * call);
    std::optional<HostCapturedMap> capturedMap(ctjs::CreateClosureOp closure, ctjs::FuncOp function,
                                               mlir::Operation * call, ctjs::GetPropertyOp read);
    bool capturedMapCalls(ctjs::FuncOp function, ctjs::SetPropertyOp publication, bool prepared,
                          llvm::SmallVectorImpl<mlir::Operation *> & calls);
    bool capturedMapParameters(ctjs::FuncOp function, bool prepared,
                               llvm::ArrayRef<mlir::Operation *> calls,
                               const llvm::DenseMap<mlir::Value, PrimitiveAlternatives> & results,
                               HostMethodParameters & result);
    PrimitiveAlternatives entryCategories(
        mlir::Value value, const llvm::DenseMap<mlir::Value, PrimitiveAlternatives> & results,
        mlir::Operation * consumer = nullptr, unsigned depth = 0,
        std::vector<mlir::Value> * dependencies = nullptr);
    std::optional<HostScalarGlobalRead> scalarGlobalRead(ctjs::LoadGlobalOp read);
    bool capturedMapBody(ctjs::FuncOp function, bool prepared, bool primitiveContents,
                         const HostMethodParameters & parameters, HostCapturedMap & result,
                         PrimitiveAlternatives & returnAlternatives);
    ctjs::CreateObjectOp object(mlir::Value value, unsigned depth = 0);
    mlir::Attribute primitive(mlir::Value value, unsigned depth = 0);
    std::optional<bool> truth(mlir::Value value, unsigned depth = 0);
    bool active(mlir::Operation * operation);
    bool before(mlir::Operation * first, mlir::Operation * second);
    mlir::Operation * anchor(mlir::Operation * operation);
    bool exactCall(mlir::Operation * call);
    bool transportedCallable(ctjs::FuncOp function);
    bool singleInvocation(ctjs::FuncOp function);
    std::string environmentProblem();
    HostSlotReport slot(const HostRootRequest & root, llvm::StringRef key);
};

llvm::StringRef keyOf(mlir::Value value);
bool ordinaryKey(llvm::StringRef key);
std::string initialBindingProblem(mlir::ModuleOp module, const HostContract & contract);

} // namespace ctcompile::ctnative::host_detail
