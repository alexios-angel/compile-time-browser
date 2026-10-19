//===- Promises.cpp - Emission of promise operations ----------------------===//
//
// Spells the promise operations of an ordinary script
// (TypeInference::isPromiseCall) with their JavaScript names
// (Runtime/Promise.hpp):
//
//   new Promise(f)       ctnative::Promise.construct<T>(std::bind_front(f, captures...))
//   p.then(f)            ctnative::Promise.prototype.then.call(p, std::bind_front(f, captures...))
//   Promise.resolve(v)   ctnative::Promise.resolve(v), with undefined for no v
//   resolve(v)           ctnative::invoke_callable(resolve, v)
//
// with the bare callback when it captures nothing. The runtime owns the bound
// callable from then on.
//
//===----------------------------------------------------------------------===//

#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

namespace {

/// Whether `value` holds a promise. It asks the C++ type, since an operand
/// another replacement produced has no lattice type.
bool holdsPromise(mlir::Value value) {
    auto opaque = llvm::dyn_cast<ec::OpaqueType>(value.getType());
    return opaque && opaque.getValue().starts_with("ctnative::js_promise<");
}

} // namespace

bool replacePromise(lowering & lower, mlir::Operation * o) {
    mlir::MLIRContext * context = o->getContext();
    mlir::OpBuilder b(o);
    const mlir::Location where = o->getLoc();
    const auto swap = [&](mlir::Value with) {
        o->getResult(0).replaceAllUsesWith(with);
        lowering::eraseIfUnused(o);
        return true;
    };
    // A value converted to the carrier of the promise's value type.
    const auto asValue = [&](mlir::Value value, mlir::Type element) {
        return lower.convertScalar(b, where, value, carrierType(context, carrierOf(element)));
    };
    if (auto symbol = o->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativePromiseCallback)) {
        const auto named = lower.names.find(symbol.getValue());
        const std::string callback =
            named == lower.names.end() ? cIdentifier(symbol.getValue()) : named->second;
        const auto any = ec::OpaqueType::get(context, "auto");
        mlir::Value bound = ec::LiteralOp::create(b, where, any, callback);
        auto construct = llvm::dyn_cast<ctjs::ConstructOp>(o);
        const mlir::ValueRange captures =
            construct ? construct.getArgs() : llvm::cast<ctjs::CallOp>(o).getArgs();
        if (!captures.empty()) {
            llvm::SmallVector<mlir::Value> bind{bound};
            // A shared cell is bound as its handle (rule 2b in ClosureLifter.h).
            for (mlir::Value capture : captures) {
                bind.push_back(loadedHandle(b, where, capture));
            }
            bound = callValue(b, where, any, "std::bind_front", bind);
        }
        if (construct) {
            auto promise = llvm::cast<PromiseType>(lower.typeOf(construct.getResult()));
            const llvm::StringRef value = carrierInfo(carrierOf(promise.getElementType())).spelling;
            return swap(callValue(b, where, construct.getResult().getType(),
                                  "ctnative::Promise.construct<" + value + ">", bound));
        }
        auto then = llvm::cast<ctjs::CallOp>(o);
        const mlir::Value operands[] = {then.getReceiver(), bound};
        if (!then.getResult().use_empty()) {
            return swap(callValue(b, where, then.getResult().getType(),
                                  "ctnative::Promise.prototype.then.call", operands));
        }
        callWithConstValueOperands(b, where, mlir::TypeRange{},
                                   b.getStringAttr("ctnative::Promise.prototype.then.call"),
                                   operands);
        lowering::eraseIfUnused(o);
        return true;
    }
    auto call = llvm::cast<ctjs::CallOp>(o);
    if (llvm::isa<mlir::BlockArgument>(call.getCallee())) {
        // `resolve(v)` in an executor answers undefined, which nothing reads.
        // A promise argument is passed as it is, to be adopted.
        auto resolve = llvm::cast<PromiseResolveType>(lower.typeOf(call.getCallee()));
        const mlir::Value given = call.getArgs().front();
        const mlir::Value operands[] = {
            call.getCallee(),
            holdsPromise(given) ? given : asValue(given, resolve.getElementType())};
        callWithConstValueOperands(b, where, mlir::TypeRange{},
                                   b.getStringAttr("ctnative::invoke_callable"), operands);
        if (!call.getResult().use_empty()) { return swap(lower.absentConstant(b, where)); }
        lowering::eraseIfUnused(o);
        return true;
    }
    // Promise.resolve of a promise returns that promise; Promise.resolve()
    // resolves with undefined.
    auto promise = llvm::cast<PromiseType>(lower.typeOf(call.getResult()));
    const mlir::Value given =
        call.getArgs().empty() ? lower.absentConstant(b, where) : call.getArgs().front();
    return swap(callValue(b, where, call.getResult().getType(), "ctnative::Promise.resolve",
                          holdsPromise(given) ? given : asValue(given, promise.getElementType())));
}

} // namespace ctcompile::ctnative::lowering_detail
