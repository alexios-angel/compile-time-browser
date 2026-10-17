//===- TypeErrors.cpp - throw new TypeError(message) natively -------------===//
//
// Recognises, structures, admits and lowers `throw new TypeError(message)`
// when the new Error is used for nothing but the throw (TypeErrors.h). The
// lowering is one call, `ctnative::throw_type_error(message)`.
//
//===----------------------------------------------------------------------===//

#include "TypeErrors.h"

#include "../Admission/Admission.h"
#include "../EmitC/Emitter.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"

namespace ctcompile::ctnative::lowering_detail {

ctjs::ConstructOp thrownTypeError(ctjs::ThrowOp thrown) {
    auto made = thrown.getValue().getDefiningOp<ctjs::ConstructOp>();
    if (!made || !made.getResult().hasOneUse() || made.getArgs().size() != 1 ||
        ctjs::calleeGlobalName(made) != "TypeError" ||
        !TypeInference::isRealmGlobal(made.getCallee().getDefiningOp(), "TypeError")) {
        return {};
    }
    return made;
}

ctjs::ThrowOp savedTypeErrorThrow(mlir::Operation * operation) {
    auto region = llvm::dyn_cast_or_null<mlir::scf::ExecuteRegionOp>(operation);
    if (!region || !region.getNoInline() || region.getNumResults() ||
        !region.getRegion().hasOneBlock() || !llvm::hasSingleElement(region.getRegion().front())) {
        return {};
    }
    auto thrown = llvm::dyn_cast<ctjs::ThrowOp>(region.getRegion().front().front());
    return thrown && thrownTypeError(thrown) ? thrown : ctjs::ThrowOp{};
}

bool catchesExceptions(mlir::ModuleOp module) {
    return module.walk([](ctjs::PushHandlerOp) { return mlir::WalkResult::interrupt(); })
        .wasInterrupted();
}

bool structureTypeErrorThrows(mlir::ModuleOp module) {
    if (catchesExceptions(module)) { return false; }
    llvm::SmallVector<ctjs::ThrowOp> throws;
    module.walk([&](ctjs::ThrowOp thrown) {
        if (llvm::isa<ctjs::FuncOp>(thrown->getParentOp()) && thrownTypeError(thrown)) {
            throws.push_back(thrown);
        }
    });
    for (ctjs::ThrowOp thrown : throws) {
        auto function = llvm::cast<ctjs::FuncOp>(thrown->getParentOp());
        ctjs::FrameEnterOp frame;
        function.walk([&](ctjs::FrameEnterOp entered) { frame = entered; });
        const mlir::Location where = thrown.getLoc();
        mlir::OpBuilder at(thrown);
        auto region = mlir::scf::ExecuteRegionOp::create(at, where, mlir::TypeRange{});
        region.setNoInline(true);
        mlir::Block & body = region.getRegion().emplaceBlock();
        thrown->moveBefore(&body, body.end());
        at.setInsertionPointAfter(region);
        // Never reached: the region throws. It gives the block the return the
        // lift needs to place it beside the function's other exits.
        if (frame) { ctjs::FrameExitOp::create(at, where, frame.getResult()); }
        ctjs::ReturnOp::create(at, where,
                               mlir::ub::PoisonOp::create(at, where, thrown.getValue().getType()));
    }
    return !throws.empty();
}

bool consumedByTypeErrorThrow(mlir::Operation * operation) {
    if (auto made = llvm::dyn_cast_or_null<ctjs::ConstructOp>(operation)) {
        if (!made->hasOneUse()) { return false; }
        auto thrown = llvm::dyn_cast<ctjs::ThrowOp>(*made->getUsers().begin());
        return thrown && savedTypeErrorThrow(thrown->getParentOp());
    }
    auto load = llvm::dyn_cast_or_null<ctjs::LoadGlobalOp>(operation);
    return load && !load->use_empty() && llvm::all_of(load->getUsers(), [](mlir::Operation * user) {
               return llvm::isa<ctjs::ConstructOp>(user) && consumedByTypeErrorThrow(user);
           });
}

std::optional<bool> typeErrorOperation(admission & self, mlir::Operation * o) {
    if (self.domEntry) { return std::nullopt; }
    if (savedTypeErrorThrow(o) || consumedByTypeErrorThrow(o)) { return true; }
    auto thrown = llvm::dyn_cast<ctjs::ThrowOp>(o);
    if (!thrown || !savedTypeErrorThrow(thrown->getParentOp())) { return std::nullopt; }
    if (catchesExceptions(o->getParentOfType<mlir::ModuleOp>())) {
        return self.refuse("a TypeError thrown where a catch or finally could see the Error");
    }
    const mlir::Type message = self.typeOf(thrownTypeError(thrown).getArgs()[0]);
    if (carrierOf(message) != carrier::string) {
        return self.refuse("a TypeError whose message is " + printed(message) + ", not a String");
    }
    return true;
}

bool lowering::replaceTypeError(mlir::Operation * o) {
    if (consumedByTypeErrorThrow(o)) { return true; }
    auto thrown = llvm::dyn_cast<ctjs::ThrowOp>(o);
    mlir::Operation * region = thrown ? thrown->getParentOp() : nullptr;
    if (!thrown || !savedTypeErrorThrow(region)) { return false; }
    ctjs::ConstructOp made = thrownTypeError(thrown);
    mlir::OpBuilder at(region);
    const mlir::Location where = thrown.getLoc();
    ec::CallOpaqueOp::create(at, where, mlir::TypeRange{},
                             at.getStringAttr("ctnative::throw_type_error"),
                             mlir::ValueRange{cellPlace(at, where, made.getArgs()[0])});
    mlir::Operation * global = made.getCallee().getDefiningOp();
    region->erase();
    made.erase();
    if (global) { eraseIfUnused(global); }
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
