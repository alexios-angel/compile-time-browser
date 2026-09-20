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
        swap(numberConstant(b, where, std::numeric_limits<double>::quiet_NaN()));
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
        const auto record = recordMapPointers.find(o);
        const std::string callee =
            record != recordMapPointers.end()
                ? "ctnative::make_map<std::string, " + record->second + ">"
            : (llvm::isa<MapType, BoolType, StrType>(map.getValueType()) ||
               isObjectValueType(map.getValueType()) ||
               !mixedMapSpelling(map.getValueType()).empty() ||
               !nullableMapSpelling(map.getValueType()).empty())
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
        const auto extractProvedScalar = [&](mlir::Value value, llvm::StringRef proofName) {
            auto proof = o->getAttrOfType<mlir::StringAttr>(proofName);
            if (!proof) { return value; }
            const auto tag = proof.getValue();
            const auto scalar = tag == "string" ? carrier::string
                                : tag == "bool" ? carrier::boolean
                                                : carrier::number;
            const auto type = scalar == carrier::string
                                  ? ec::OpaqueType::get(context, kRawStringType)
                                  : carrierType(context, scalar);
            if (isBooleanStringCarrier(value.getType())) {
                const auto helper = tag == "string" ? "std::get<ctnative::js_string>"
                                                    : "std::get<ctnative::js_boolean_t>";
                return callWithConstValueOperands(b, where,
                                                  mlir::TypeRange{carrierType(context, scalar)},
                                                  b.getStringAttr(helper), mlir::ValueRange{value})
                    .getResult(0);
            }
            if (isNullableCarrier(value.getType()) || isNullableStringCarrier(value.getType())) {
                // Only a rederived exact tag can narrow an owning temporary.
                // String extraction copies; it cannot borrow the source value.
                return convertScalar(b, where, value, type);
            }
            return value;
        };
        const auto convertAlternative = [&](mlir::Value value, mlir::Type type, bool key = false) {
            // Maps retain their proved raw String/binary64 key/payload storage.
            if (value.getType() == carrierType(context, carrier::string)) {
                value =
                    convertScalar(b, where, value, ec::OpaqueType::get(context, kRawStringType));
            }
            if (isNumberCarrier(value.getType())) {
                value = convertScalar(b, where, value, b.getF64Type());
            }
            auto spelling = nullableMapSpelling(type);
            if (!spelling.empty()) {
                const bool mixed = spelling != kNullableStringType;
                auto argumentType = value.getType();
                if (auto lvalue = llvm::dyn_cast<ec::LValueType>(argumentType)) {
                    argumentType = lvalue.getValueType();
                }
                if (!mixed || !isBooleanCarrier(argumentType)) {
                    // Preserve Null/Undefined tags and own every String in
                    // both key and payload storage, including saved reads.
                    value = convertScalar(b, where, value,
                                          carrierType(context, carrier::nullableString));
                }
                if (!mixed) { return value; }
            } else {
                spelling = key ? mixedMapKeySpelling(type) : mixedMapSpelling(type);
                if (spelling.empty()) { return value; }
            }
            return callWithConstValueOperands(
                       b, where, mlir::TypeRange{ec::OpaqueType::get(context, spelling)},
                       b.getStringAttr(spelling), mlir::ValueRange{value})
                .getResult(0);
        };
        if (map && args.size() >= 2) {
            if (llvm::isa<OptType>(map.getKeyType()) &&
                carrierOf(map.getKeyType()) == carrier::nullable) {
                args[1] = convertScalar(b, where, args[1], carrierType(context, carrier::nullable));
            } else {
                args[1] = convertAlternative(extractProvedScalar(args[1], kNativeMapKeyType),
                                             map.getKeyType(), true);
            }
        }
        if (action == "set") {
            auto call = llvm::cast<CallOp>(o);
            // The result retains its solver fact after the receiver becomes
            // replacement EmitC SSA, which has no analysis entry.
            const auto storedMap = llvm::cast<MapType>(typeOf(call.getResult()));
            if (recordMapPointers.contains(o)) {
                auto type = llvm::cast<ec::LValueType>(args[2].getType()).getValueType();
                args[2] = ec::AddressOfOp::create(b, where, ec::PointerType::get(type), args[2]);
            } else {
                args[2] = convertAlternative(extractProvedScalar(args[2], kNativeMapWriteType),
                                             storedMap.getValueType());
                if (isObjectValueType(storedMap.getValueType())) {
                    args[2] = convertScalar(b, where, args[2],
                                            carrierType(context, carrier::objectValue));
                }
            }
        }
        const auto helper = o->hasAttr(kNativeMapPresent) ? "get_present" : action;
        std::string helperName = ("ctnative::map_" + helper).str();
        if (action == "get" && map) {
            if (isObjectValueType(map.getValueType()) &&
                llvm::isa<ObjectIdentityType>(typeOf(o->getResult(0))) &&
                o->hasAttr(kNativeMapPresent)) {
                // Storage carries the family's finite object/scalar union;
                // this read independently proves a present object. Copy its
                // owner so the saved identity survives replacement/deletion.
                helperName = "ctnative::map_get_present_identity";
            } else if (!mixedMapSpelling(map.getValueType()).empty()) {
                helperName = "ctnative::map_get_present_as<" +
                             mapValueSpelling(typeOf(o->getResult(0))) + ">";
            } else if (!nullableMapSpelling(map.getValueType()).empty() &&
                       o->hasAttr(kNativeMapReadType)) {
                helperName = "ctnative::map_get_present_nullable_as<" +
                             mapValueSpelling(typeOf(o->getResult(0))) + ">";
            }
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
            const auto resultType = o->getResult(0).getType();
            const auto storageType =
                action == "get" && isNumberCarrier(resultType) ? mlir::Type(b.getF64Type())
                : action == "get" && resultType == carrierType(context, carrier::string)
                    ? mlir::Type(ec::OpaqueType::get(context, kRawStringType))
                    : resultType;
            auto value =
                callWithConstValueOperands(b, where, mlir::TypeRange{storageType}, name, args)
                    .getResult(0);
            swap(convertScalar(b, where, value, resultType));
        }
        return true;
    }

    return false;
}

} // namespace ctcompile::ctnative::lowering_detail
