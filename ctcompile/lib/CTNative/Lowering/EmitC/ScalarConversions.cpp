//===- ScalarConversions.cpp - Scalar carrier conversions -------*- C++ -*-===//
//
// Conversions between the native scalar carriers (Number, Boolean, String and
// their nullable and union forms), the module-wide census that chooses the
// carrier of each global, parameter and result, and the pass that inserts
// conversions at every region and call boundary.
//
//===----------------------------------------------------------------------===//

#include "../../Analysis/OwnedGlobalRoots.h"
#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

/// Returns the nullable-scalar constant for `null` or, by default, `undefined`.
mlir::Value lowering::absentConstant(mlir::OpBuilder & b, mlir::Location where, bool isNull) {
    return ec::ConstantOp::create(
        b, where, carrierType(context, carrier::nullable),
        ec::OpaqueAttr::get(context, isNull
                                         ? "ctnative::nullable_scalar{ctnative::js_null_t{}}"
                                         : "ctnative::nullable_scalar{ctnative::undefined_t{}}"));
}

mlir::Value lowering::number(mlir::OpBuilder & b, mlir::Location where, mlir::Value value) {
    return convertScalar(b, where, value, carrierType(context, carrier::number));
}

/// Converts `value` to the `target` carrier type, inserting the cast or
/// ctnative:: helper call the pair needs; reports a fatal error for a pair
/// admission should have refused. An lvalue is loaded first.
mlir::Value lowering::convertScalar(mlir::OpBuilder & b, mlir::Location where, mlir::Value value,
                                    mlir::Type target) {
    if (auto lvalue = llvm::dyn_cast<ec::LValueType>(value.getType())) {
        value = ec::LoadOp::create(b, where, lvalue.getValueType(), value);
    }
    if (value.getType() == target) { return value; }
    // An ordinary script's element or null: an element view converts, and an
    // absent scalar (null or undefined) is the empty optional.
    if (target == carrierType(context, carrier::nullableElement)) {
        if (value.getType() == ec::OpaqueType::get(context, kPageElementType)) {
            return ec::CastOp::create(b, where, target, value);
        }
        if (isNullableCarrier(value.getType())) {
            return ec::ConstantOp::create(b, where, target,
                                          ec::OpaqueAttr::get(context, "std::nullopt"));
        }
    }
    // Joined into an instance (EmitC/Instances.cpp), anything that is not one
    // is null, undefined or a falsy `has` result: the registry proof allows no
    // other, and a null pointer stands for each.
    if (instanceSpellings.contains(opaqueSpelling(target)) && !isInstanceValue(value)) {
        return ec::LiteralOp::create(b, where, target, b.getStringAttr("nullptr"));
    }
    // Host null uses optional String storage. Preserve its exact null tag in
    // scalar joins, or convert it to positive zero for numeric consumers.
    // Region edges are converted before ctjs constants become EmitC constants.
    if ((isNumberCarrier(target) || isNullableCarrier(target)) &&
        value.getType() == ec::OpaqueType::get(context, kDOMOptionalStringType)) {
        auto constant = value.getDefiningOp<ec::ConstantOp>();
        auto source = value.getDefiningOp<ctjs::ConstantOp>();
        auto literal =
            constant ? llvm::dyn_cast<ec::OpaqueAttr>(constant.getValue()) : ec::OpaqueAttr{};
        // The host join excludes undefined; an absent-only lattice excludes String.
        auto optional = llvm::dyn_cast_or_null<OptType>(typeOf(value));
        const bool nullJoin = domOptionalStrings.contains(value) && optional &&
                              llvm::isa<BottomType>(optional.getElementType());
        if ((literal && literal.getValue() == "std::nullopt") ||
            (source && llvm::isa<ctjs::NullAttr>(source.getValue())) || nullJoin) {
            return isNullableCarrier(target) ? absentConstant(b, where, true)
                                             : numberConstant(b, where, 0.0);
        }
    }
    const auto string = carrierType(context, carrier::string);
    const auto rawString = ec::OpaqueType::get(context, kRawStringType);
    // The Boolean/String and Number/String unions convert by the same rules.
    const auto c = [&](carrier which) { return carrierType(context, which); };
    for (auto [scalar, whole, optional, widen, narrow, text] :
         {std::tuple{c(carrier::boolean), c(carrier::booleanString),
                     c(carrier::nullableBooleanString), "ctnative::to_nullable_boolean_string",
                     "ctnative::global_boolean_string", "ctnative::nullable_boolean_string_text"},
          std::tuple{c(carrier::number), c(carrier::numberString), c(carrier::nullableNumberString),
                     "ctnative::to_nullable_number_string", "ctnative::global_number_string",
                     "ctnative::nullable_number_string_text"}}) {
        const bool member = value.getType() == scalar || value.getType() == string;
        if ((target == whole || target == optional) && member) {
            return ec::CastOp::create(b, where, target, value);
        }
        if (target == optional) { return callValue(b, where, target, widen, value); }
        if (value.getType() == optional && target == whole) {
            return callValue(b, where, target, narrow, value);
        }
        if (value.getType() == optional && target == string) {
            return callValue(b, where, target, text, value);
        }
    }
    if (isNullableCarrier(value.getType()) && target == string) {
        return memberCall(b, where, target, value, "to_string");
    }
    if ((value.getType() == string || isNullableStringCarrier(value.getType())) &&
        isNumberCarrier(target)) {
        return memberCall(b, where, target, value, "to_number");
    }
    if ((isBooleanStringCarrier(value.getType()) || isNumberStringCarrier(value.getType())) &&
        target == string) {
        return callValue(b, where, target,
                         isNumberStringCarrier(value.getType()) ? "ctnative::number_string_text"
                                                                : "ctnative::boolean_string_text",
                         value);
    }
    if (value.getType() == rawString && target == string) {
        return ec::CastOp::create(b, where, target, value);
    }
    if (value.getType() == string && target == rawString) {
        return memberCall(b, where, rawString, value, "value");
    }
    if (value.getType() == string &&
        (target == ec::OpaqueType::get(context, kDOMOptionalStringType) ||
         target == ec::OpaqueType::get(context, kDOMJSONType) || isNullableStringCarrier(target))) {
        value = convertScalar(b, where, value, rawString);
    }
    if (target.isF64() &&
        (isBooleanCarrier(value.getType()) || isNullableCarrier(value.getType()))) {
        return convertScalar(b, where, number(b, where, value), target);
    }
    if (isNumberCarrier(target) && llvm::isa<mlir::IntegerType>(value.getType())) {
        return convertScalar(b, where, ec::CastOp::create(b, where, b.getF64Type(), value), target);
    }
    if (isNumberCarrier(value.getType()) && target.isF64()) {
        return memberCall(b, where, target, value, "value");
    }
    if ((value.getType().isF64() && isNumberCarrier(target)) ||
        (isNumberCarrier(value.getType()) && target.isInteger(1))) {
        return ec::CastOp::create(b, where, target, value);
    }
    if ((isBooleanCarrier(value.getType()) && target.isInteger(1)) ||
        (value.getType().isInteger(1) && isBooleanCarrier(target))) {
        return ec::CastOp::create(b, where, target, value);
    }
    if (isBooleanCarrier(target)) {
        // Optional carriers retain their existing truthiness conversion; the
        // resulting C++ condition becomes a JavaScript Boolean value explicitly.
        return ec::CastOp::create(b, where, target, truthy(b, where, value));
    }
    llvm::StringRef helper;
    if (target == ec::OpaqueType::get(context, kDOMOptionalStringType) &&
        value.getType() == rawString) {
        helper = kDOMOptionalStringType;
    } else if (target == ec::OpaqueType::get(context, kDOMJSONType) &&
               (value.getType() == rawString || value.getType().isF64() ||
                isNumberCarrier(value.getType()) || value.getType().isInteger(1) ||
                isBooleanCarrier(value.getType()))) {
        // Primitive arms keep their exact alternatives in the owning tree.
        if (isBooleanCarrier(value.getType())) {
            value = ec::CastOp::create(b, where, b.getI1Type(), value);
        }
        if (isNumberCarrier(value.getType())) {
            value = convertScalar(b, where, value, b.getF64Type());
        }
        helper = kDOMJSONType;
    } else if (target == ec::OpaqueType::get(context, kDOMJSONType) &&
               value.getType() == ec::OpaqueType::get(context, kDOMOptionalStringType)) {
        auto expression =
            ec::ExpressionOp::create(b, where, target, mlir::ValueRange{value}, false);
        expression.createBody();
        mlir::OpBuilder inside = mlir::OpBuilder::atBlockBegin(&expression.getRegion().front());
        auto optional = expression.getRegion().front().getArgument(0);
        auto present = memberCall(inside, where, inside.getI1Type(), optional, "has_value");
        auto text = memberCall(inside, where, rawString, optional, "value");
        auto string =
            ec::CallOpaqueOp::create(inside, where, mlir::TypeRange{target},
                                     inside.getStringAttr(kDOMJSONType), mlir::ValueRange{text});
        auto null = ec::ConstantOp::create(inside, where, target,
                                           ec::OpaqueAttr::get(context, "ctbrowser::json_value{}"));
        // The member access is inside the conditional expression's selected arm.
        auto joined =
            ec::ConditionalOp::create(inside, where, target, present, string.getResult(0), null);
        ec::YieldOp::create(inside, where, joined);
        return expression.getResult();
    } else if (isNullableStringCarrier(target)) {
        helper = "ctnative::to_nullable_string";
    } else if (isNullableStringCarrier(value.getType())) {
        if (target == string || target == rawString) {
            helper = "ctnative::string_text";
        } else if (llvm::isa<mlir::IntegerType>(target)) {
            helper = "ctnative::string_truthy";
        } else {
            llvm::report_fatal_error("nullable string has no admitted numeric conversion");
        }
    } else if (isObjectValueCarrier(target)) {
        needsObjectValue = true;
        helper = "ctnative::to_object_value";
    } else if (isObjectValueCarrier(value.getType()) && llvm::isa<mlir::IntegerType>(target)) {
        needsObjectValue = true;
        helper = "ctnative::object_truthy";
    } else if (isNullableCarrier(target)) {
        helper = "ctnative::to_nullable";
    } else if (isNullableCarrier(value.getType())) {
        helper = llvm::isa<mlir::IntegerType>(target) ? "ctnative::scalar_truthy"
                                                      : "ctnative::to_number";
    } else if ((isBooleanCarrier(value.getType()) || isBooleanStringCarrier(value.getType()) ||
                isNumberStringCarrier(value.getType()) ||
                isNullableBooleanStringCarrier(value.getType()) ||
                isNullableNumberStringCarrier(value.getType())) &&
               isNumberCarrier(target)) {
        helper = "ctnative::to_number";
    } else if (llvm::isa<mlir::Float64Type>(target) &&
               llvm::isa<mlir::IntegerType>(value.getType())) {
        helper = "static_cast<double>";
    } else {
        llvm::report_fatal_error("native scalar boundary has incompatible proved carriers");
    }
    const mlir::Type resultType = target == string ? mlir::Type(rawString) : target;
    auto result = callValue(b, where, resultType, helper, value);
    return resultType == target ? result : convertScalar(b, where, result, target);
}

/// The lattice meet of every value `fn` returns.
mlir::Type lowering::joinedReturnType(ctjs::FuncOp fn) const {
    mlir::Type result = BottomType::get(context);
    fn.getBody().walk([&](ctjs::ReturnOp ret) { result = meet(result, typeOf(ret.getValue())); });
    return result;
}

/// Chooses the native carrier of every global, function result and parameter
/// in the accepted functions, before any of them is lowered.
void lowering::censusScalars(llvm::ArrayRef<ctjs::FuncOp> accepted,
                             const OwnedGlobalRoots * roots) {
    const auto scalarType = [&](mlir::Type type) -> mlir::Type {
        const auto c = carrierOf(type);
        // An element or null converts at a call edge like a scalar: an
        // omitted argument is the empty optional.
        if (!isPrimitiveCarrier(c) && !isObjectCarrier(c) && c != carrier::nullableElement) {
            return {};
        }
        needsObjectValue |= c == carrier::objectValue;
        return carrierType(context, c);
    };
    for (ctjs::FuncOp fn : accepted) {
        fn.getBody().walk([&](ctjs::StoreGlobalOp store) {
            if (!globals.contains(store.getName())) { return; }
            auto type = typeOf(store.getValue());
            if (roots) {
                if (auto scalar =
                        scalarObservationType(context, roots->returnedScalar(store.getValue()))) {
                    type = scalar;
                    // Keep the exact store after source calls are erased. The
                    // callee/result census below deliberately retains its broad ABI.
                    scalarStores.insert(store);
                }
            }
            auto [position, inserted] = globalTypes.try_emplace(store.getName(), type);
            if (!inserted) { position->second = meet(position->second, type); }
        });
        // Preserve the proved DOM String-or-null return, including a checked
        // branch join, as an owning optional before the generic
        // scalar census can request a tagged nullable String value model.
        mlir::Type domResult;
        fn.getBody().walk([&](ctjs::ReturnOp ret) {
            const auto call = domCalls.find(ret.getValue().getDefiningOp());
            if (domOptionalStrings.contains(ret.getValue()) ||
                (call != domCalls.end() && call->second.returnsOptionalString())) {
                domResult = ec::OpaqueType::get(context, kDOMOptionalStringType);
            }
        });
        if (!domResult) {
            fn.getBody().walk([&](ctjs::ReturnOp ret) {
                if (domStringResults.contains(ret.getValue())) {
                    domResult = carrierType(context, carrier::string);
                } else if (carrierOf(typeOf(ret.getValue())) == carrier::json) {
                    domResult = carrierType(context, carrier::json);
                }
            });
        }
        // A proved undefined DOM entry has no C++ result, rather than a tagged carrier.
        resultTypes.try_emplace(fn.getSymName(),
                                domResult ? domResult : scalarType(joinedReturnType(fn)));
        auto & params = parameterTypes[fn.getSymName()];
        for (mlir::BlockArgument arg : fn.getBody().front().getArguments()) {
            params.push_back(scalarType(typeOf(arg)));
        }
    }
}

/// SCF preserves JavaScript values through typed region edges. Materialize the
/// scalar widening at each edge, including initial loop operands and backedges.
/// Calls use the callee's joined parameter type, not the actual argument type.
void lowering::convertBoundaries(ctjs::FuncOp fn) {
    llvm::SmallVector<mlir::Operation *> ops;
    fn.getBody().walk([&](mlir::Operation * op) { ops.push_back(op); });
    for (mlir::Operation * op : ops) {
        mlir::OpBuilder at(op);
        const auto convert = [&](unsigned index, mlir::Type target) {
            mlir::Value value = op->getOperand(index);
            if (!target || value.getType() == target ||
                (value.getDefiningOp() &&
                 value.getDefiningOp()->getName().getStringRef() == "ub.poison")) {
                return;
            }
            op->setOperand(index, convertScalar(at, op->getLoc(), value, target));
        };
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
            const auto & params = parameterTypes[call.getCallee()];
            // Operands 0-2 are the importer's three implicit arguments, which
            // have no parameter carrier; convert only the source arguments.
            for (unsigned i = 3; i < params.size() && i < op->getNumOperands(); ++i) {
                if (!llvm::isa<ec::PointerType, ec::LValueType>(op->getOperand(i).getType())) {
                    convert(i, params[i]);
                }
            }
        } else if (auto exit = llvm::dyn_cast<ctjs::TryExitOp>(op)) {
            auto parent = exit->getParentOfType<ctjs::TryOp>();
            convert(1, parent.getResult().getType());
            for (auto [index, argument] :
                 llvm::enumerate(parent.getCatchBody().front().getArguments())) {
                convert(static_cast<unsigned>(index) + 2, argument.getType());
            }
        } else if (auto yielded = llvm::dyn_cast<ctjs::TryYieldOp>(op)) {
            convert(0, yielded->getParentOfType<ctjs::TryOp>().getResult().getType());
        } else if (llvm::isa<ctjs::InvokeYieldOp>(op)) {
            // A proved invoke's String failure arm widens into its json result.
            for (unsigned i = 0; i < op->getNumOperands(); ++i) {
                convert(i, op->getParentOp()->getResult(i).getType());
            }
        } else {
            for (unsigned i = 0; i < op->getNumOperands(); ++i) { convert(i, edgeTarget(op, i)); }
        }
    }
}

} // namespace ctcompile::ctnative::lowering_detail
