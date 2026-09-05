// EmitC/Maps.cpp - standalone owning Map operations.
#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

bool lowering::replaceMap(mlir::Operation * o) {
    using namespace ctjs;
    mlir::OpBuilder b(o);
    const mlir::Location where = o->getLoc();
    const auto swap = [&](mlir::Value with) {
        o->getResult(0).replaceAllUsesWith(with);
        eraseIfUnused(o);
    };
    if (isNativeMapBookkeeping(o)) {
        swap(f64Constant(b, where, std::numeric_limits<double>::quiet_NaN()));
        return true;
    }
    if (auto made = llvm::dyn_cast<ConstructOp>(o); made && o->hasAttr(kNativeMapSite)) {
        auto map = llvm::cast<MapType>(typeOf(made.getResult()));
        const std::string callee =
            (llvm::isa<MapType>(map.getValueType()) || isObjectValueType(map.getValueType()))
                ? ("ctnative::make_map<" + mapKeySpelling(map.getKeyType()) + ", " +
                   mapValueSpelling(map.getValueType()) + ">")
                      .str()
                : ("ctnative::make_number_map<" + mapKeySpelling(map.getKeyType()) + ">").str();
        swap(ec::CallOpaqueOp::create(b, where, mlir::TypeRange{made.getResult().getType()},
                                      b.getStringAttr(callee), mlir::ValueRange{})
                 .getResult(0));
        return true;
    }
    if (const llvm::StringRef action = nativeMapAction(o); !action.empty()) {
        llvm::SmallVector<mlir::Value> args;
        if (auto call = llvm::dyn_cast<CallOp>(o)) {
            args.push_back(call.getReceiver());
            llvm::append_range(args, call.getArgs());
        } else {
            args.push_back(llvm::cast<GetPropertyOp>(o).getObject());
        }
        if (action == "set") {
            auto call = llvm::cast<CallOp>(o);
            // set returns the same Map schema. Its result still has a solver
            // fact here; the receiver may already be a replacement EmitC SSA
            // value, which deliberately has no entry in that analysis.
            const auto map = llvm::cast<MapType>(typeOf(call.getResult()));
            if (isObjectValueType(map.getValueType())) {
                args[2] =
                    convertScalar(b, where, args[2], carrierType(context, carrier::objectValue));
            }
        }
        const auto helper = o->hasAttr(kNativeMapPresent) ? "get_present" : action;
        const auto name = b.getStringAttr(("ctnative::map_" + helper).str());
        if (action == "clear") {
            ec::CallOpaqueOp::create(b, where, mlir::TypeRange{}, name, args);
            swap(absentConstant(b, where));
        } else if (action == "keys" || action == "values") {
            const auto type = ec::OpaqueType::get(context, kVectorType);
            mlir::Value result =
                ec::CallOpaqueOp::create(b, where, mlir::TypeRange{type}, name, args).getResult(0);
            mlir::Value local = ec::VariableOp::create(b, where, vectorCarrierType(context),
                                                       ec::OpaqueAttr::get(context, ""));
            ec::AssignOp::create(b, where, local, result);
            swap(local);
        } else {
            swap(ec::CallOpaqueOp::create(b, where, mlir::TypeRange{o->getResult(0).getType()},
                                          name, args)
                     .getResult(0));
        }
        return true;
    }

    return false;
}

} // namespace ctcompile::ctnative::lowering_detail
