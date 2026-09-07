#include "ctcompile/CTNative/IR/CTNativeOps.h"

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"

#define GET_OP_CLASSES
#include "ctcompile/CTNative/IR/CTNativeOps.cpp.inc"

namespace ctcompile::ctnative {
namespace {

bool isExceptionPayload(mlir::Type type) {
    if (type.isF64() || type.isSignlessInteger(1)) { return true; }
    auto opaque = llvm::dyn_cast<mlir::emitc::OpaqueType>(type);
    return opaque && opaque.getValue() == "std::string";
}

// A throw in a catch body belongs to an enclosing handler, never to the
// handler already executing. Check the nearest protected region even through
// nested ifs/tries; JavaScript has no payload-dependent catch filtering.
CppTryOp protectedBy(mlir::Operation * operation) {
    for (auto * region = operation->getParentRegion(); region; region = region->getParentRegion()) {
        auto attempt = llvm::dyn_cast_or_null<CppTryOp>(region->getParentOp());
        if (attempt && region == &attempt.getBody()) { return attempt; }
        if (region->getParentOp() &&
            region->getParentOp()->hasTrait<mlir::OpTrait::IsIsolatedFromAbove>()) {
            break;
        }
    }
    return {};
}

// Check the actual direct-call graph on every verification. An unhandled
// primitive throw in a helper reaches the caller's active handler; a local
// helper catch consumes its own protected throws, but throws from that catch
// can still escape. Keep no summary across IR mutations. Opaque C++ calls
// retain their separate foreign-boundary contract: this is a structural
// payload check, not an effect proof for arbitrary callbacks or source code.
struct protectedCalls {
    mlir::Type payload;
    unsigned remaining = 4096;
    llvm::DenseSet<mlir::Operation *> active;
    llvm::DenseSet<mlir::Operation *> proved;
    llvm::StringRef refusal;

    bool reject(llvm::StringRef why) {
        if (refusal.empty()) { refusal = why; }
        return false;
    }

    bool call(mlir::emitc::CallOp direct) {
        auto target = mlir::SymbolTable::lookupNearestSymbolFrom<mlir::emitc::FuncOp>(
            direct, direct.getCalleeAttr());
        if (!target || target.isExternal()) {
            return reject("requires a defined EmitC callee for each protected direct call");
        }
        if (proved.contains(target)) { return true; }
        if (active.contains(target)) {
            return reject("cannot prove the payload of a recursive protected direct call");
        }
        if (active.size() >= 32) {
            return reject("protected direct-call payload depth limit exhausted");
        }
        active.insert(target);
        const bool matched = region(target.getBody(), {});
        active.erase(target);
        if (!matched) { return false; }
        proved.insert(target);
        return true;
    }

    bool region(mlir::Region & body, CppTryOp handler) {
        const auto matched = body.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * op) {
            if (remaining == 0) {
                reject("protected direct-call payload work budget exhausted");
                return mlir::WalkResult::interrupt();
            }
            --remaining;
            if (op->hasTrait<mlir::OpTrait::IsIsolatedFromAbove>()) {
                return mlir::WalkResult::skip();
            }
            if (protectedBy(op) != handler) { return mlir::WalkResult::advance(); }
            if (auto thrown = llvm::dyn_cast<CppThrowOp>(op)) {
                if (thrown.getValue().getType() != payload) {
                    reject(handler ? "requires each protected throw to match the homogeneous catch "
                                     "payload"
                                   : "requires each escaping callee throw to match the homogeneous "
                                     "catch payload");
                    return mlir::WalkResult::interrupt();
                }
            }
            if (auto direct = llvm::dyn_cast<mlir::emitc::CallOp>(op)) {
                if (!call(direct)) { return mlir::WalkResult::interrupt(); }
            }
            return mlir::WalkResult::advance();
        });
        return !matched.wasInterrupted();
    }
};

} // namespace

mlir::LogicalResult CppTryOp::verify() {
    if (!llvm::hasSingleElement(getBody()) || !llvm::hasSingleElement(getCatchBody())) {
        return emitOpError("requires one block in each try and catch region");
    }
    if (getBody().front().getNumArguments() != 0) {
        return emitOpError("requires no try region arguments");
    }
    mlir::Block & handler = getCatchBody().front();
    if (handler.getNumArguments() != 1 || !isExceptionPayload(handler.getArgument(0).getType())) {
        return emitOpError("requires exactly one f64, i1 or owning std::string catch argument");
    }
    for (mlir::Region * region : {&getBody(), &getCatchBody()}) {
        mlir::Block & block = region->front();
        if (block.empty()) {
            return emitOpError("requires non-empty blocks in both try and catch regions");
        }
        if (!mlir::isa<CppTryEndOp>(block.back())) {
            return emitOpError("requires ctnative.cpp_try_end as each try and catch terminator");
        }
    }
    protectedCalls calls{handler.getArgument(0).getType(), 4096, {}, {}, {}};
    if (!calls.region(getBody(), *this)) { return emitOpError(calls.refusal); }
    return mlir::success();
}

mlir::LogicalResult CppThrowOp::verify() {
    if (!isExceptionPayload(getValue().getType())) {
        return emitOpError("requires an f64, i1 or owning std::string payload");
    }
    mlir::Operation * next = getOperation()->getNextNode();
    if (!next || !next->hasTrait<mlir::OpTrait::IsTerminator>()) {
        return emitOpError("must be immediately followed by the enclosing region terminator");
    }
    return mlir::success();
}

} // namespace ctcompile::ctnative
