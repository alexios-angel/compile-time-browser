//===- Promises.cpp - Emission of promise operations ----------------------===//
//
// Spells the promise operations of an ordinary script
// (TypeInference::isPromiseCall) with their JavaScript names
// (Runtime/Promise.hpp):
//
//   new Promise(f)       ctnative::Promise.construct<T>(std::bind_front(f, captures...))
//   p.then(f)            ctnative::Promise.prototype.then.call(p, std::bind_front(f, captures...))
//   Promise.resolve(v)   ctnative::Promise.resolve(v)
//   resolve(v)           ctnative::invoke_callable(resolve, v)
//
// with the bare callback when it captures nothing. The runtime owns the bound
// callable from then on.
//
//===----------------------------------------------------------------------===//

#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

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
            llvm::append_range(bind, captures);
            bound = callValue(b, where, any, "std::bind_front", bind);
        }
        if (construct) {
            auto promise = llvm::cast<PromiseType>(lower.typeOf(construct.getResult()));
            const llvm::StringRef value = carrierInfo(carrierOf(promise.getElementType())).spelling;
            return swap(callValue(b, where, construct.getResult().getType(),
                                  "ctnative::Promise.construct<" + value + ">", bound));
        }
        const mlir::Value operands[] = {llvm::cast<ctjs::CallOp>(o).getReceiver(), bound};
        callWithConstValueOperands(b, where, mlir::TypeRange{},
                                   b.getStringAttr("ctnative::Promise.prototype.then.call"),
                                   operands);
        lowering::eraseIfUnused(o);
        return true;
    }
    auto call = llvm::cast<ctjs::CallOp>(o);
    if (llvm::isa<mlir::BlockArgument>(call.getCallee())) {
        // `resolve(v)` in an executor answers undefined, which nothing reads.
        auto resolve = llvm::cast<PromiseResolveType>(lower.typeOf(call.getCallee()));
        const mlir::Value operands[] = {call.getCallee(),
                                        asValue(call.getArgs().front(), resolve.getElementType())};
        callWithConstValueOperands(b, where, mlir::TypeRange{},
                                   b.getStringAttr("ctnative::invoke_callable"), operands);
        if (!call.getResult().use_empty()) { return swap(lower.absentConstant(b, where)); }
        lowering::eraseIfUnused(o);
        return true;
    }
    auto promise = llvm::cast<PromiseType>(lower.typeOf(call.getResult()));
    return swap(callValue(b, where, call.getResult().getType(), "ctnative::Promise.resolve",
                          asValue(call.getArgs().front(), promise.getElementType())));
}

} // namespace ctcompile::ctnative::lowering_detail
