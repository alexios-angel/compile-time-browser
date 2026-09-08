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
    if (o->hasAttr(kNativeMapSnapshotCopy)) {
        needsMapOrder = true;
        auto copy = llvm::cast<CallOp>(o);
        mlir::Value local = ec::VariableOp::create(b, where, copy.getResult().getType(),
                                                   ec::OpaqueAttr::get(context, ""));
        const auto type = llvm::cast<ec::LValueType>(local.getType()).getValueType();
        mlir::Value value = ec::LoadOp::create(b, where, type, copy.getArgs().front());
        ec::AssignOp::create(b, where, local, value);
        swap(local);
        return true;
    }
    if (auto made = llvm::dyn_cast<ConstructOp>(o); made && o->hasAttr(kNativeMapSite)) {
        auto map = llvm::cast<MapType>(typeOf(made.getResult()));
        const std::string callee =
            (llvm::isa<MapType, BoolType, StrType>(map.getValueType()) ||
             isObjectValueType(map.getValueType()) || !mixedMapSpelling(map.getValueType()).empty())
                ? ("ctnative::make_map<" + mapKeySpelling(map.getKeyType()) + ", " +
                   mapValueSpelling(map.getValueType()) + ">")
                      .str()
            : llvm::isa<StrType>(map.getKeyType())
                ? std::string{"ctnative::make_string_to_number_map"}
                : ("ctnative::make_number_map<" + mapKeySpelling(map.getKeyType()) + ">").str();
        swap(callWithConstValueOperands(b, where, mlir::TypeRange{made.getResult().getType()},
                                        b.getStringAttr(callee), mlir::ValueRange{})
                 .getResult(0));
        return true;
    }
    if (const llvm::StringRef action = nativeMapAction(o); !action.empty()) {
        needsMapOrder |= action == "keys" || action == "values";
        llvm::SmallVector<mlir::Value> args;
        if (auto call = llvm::dyn_cast<CallOp>(o)) {
            args.push_back(call.getReceiver());
            llvm::append_range(args, call.getArgs());
        } else {
            args.push_back(llvm::cast<GetPropertyOp>(o).getObject());
        }
        const auto map = mapSchemas.lookup(o);
        const auto convertAlternative = [&](mlir::Value value, mlir::Type type) {
            const auto spelling = mixedMapSpelling(type);
            return callWithConstValueOperands(
                       b, where, mlir::TypeRange{ec::OpaqueType::get(context, spelling)},
                       b.getStringAttr(spelling), mlir::ValueRange{value})
                .getResult(0);
        };
        if (map && !mixedMapSpelling(map.getKeyType()).empty() && args.size() >= 2) {
            args[1] = convertAlternative(args[1], map.getKeyType());
        }
        if (action == "set") {
            auto call = llvm::cast<CallOp>(o);
            // set returns the same Map schema. Its result still has a solver
            // fact here; the receiver may already be a replacement EmitC SSA
            // value, which deliberately has no entry in that analysis.
            const auto storedMap = llvm::cast<MapType>(typeOf(call.getResult()));
            if (!mixedMapSpelling(storedMap.getValueType()).empty()) {
                if (auto proof = o->getAttrOfType<mlir::StringAttr>(kNativeMapWriteType)) {
                    const auto tag = proof.getValue();
                    const auto scalar = tag == "string" ? carrier::string
                                        : tag == "bool" ? carrier::boolean
                                                        : carrier::number;
                    const auto type = carrierType(context, scalar);
                    if (isBooleanStringCarrier(args[2].getType())) {
                        const auto helper =
                            tag == "string" ? "std::get<std::string>" : "std::get<bool>";
                        args[2] = callWithConstValueOperands(b, where, mlir::TypeRange{type},
                                                             b.getStringAttr(helper),
                                                             mlir::ValueRange{args[2]})
                                      .getResult(0);
                    } else if (isNullableCarrier(args[2].getType())) {
                        args[2] = convertScalar(b, where, args[2], type);
                    }
                }
                args[2] = convertAlternative(args[2], storedMap.getValueType());
            } else if (isObjectValueType(storedMap.getValueType())) {
                args[2] =
                    convertScalar(b, where, args[2], carrierType(context, carrier::objectValue));
            }
        }
        const auto helper = o->hasAttr(kNativeMapPresent) ? "get_present" : action;
        std::string helperName = ("ctnative::map_" + helper).str();
        if (action == "get" && map && !mixedMapSpelling(map.getValueType()).empty()) {
            helperName =
                "ctnative::map_get_present_as<" + mapValueSpelling(typeOf(o->getResult(0))) + ">";
        }
        const auto name = b.getStringAttr(helperName);
        if (action == "clear") {
            callWithConstValueOperands(b, where, mlir::TypeRange{}, name, args);
            swap(absentConstant(b, where));
        } else if (action == "keys" || action == "values") {
            const auto localType = o->getResult(0).getType();
            const auto type = llvm::cast<ec::LValueType>(localType).getValueType();
            mlir::Value result =
                callWithConstValueOperands(b, where, mlir::TypeRange{type}, name, args)
                    .getResult(0);
            mlir::Value local =
                ec::VariableOp::create(b, where, localType, ec::OpaqueAttr::get(context, ""));
            ec::AssignOp::create(b, where, local, result);
            swap(local);
        } else {
            swap(callWithConstValueOperands(b, where, mlir::TypeRange{o->getResult(0).getType()},
                                            name, args)
                     .getResult(0));
        }
        return true;
    }

    return false;
}

} // namespace ctcompile::ctnative::lowering_detail
