#include "../../Analysis/OwnedGlobalRoots.h"
#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

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

mlir::Value lowering::convertScalar(mlir::OpBuilder & b, mlir::Location where, mlir::Value value,
                                    mlir::Type target) {
    if (auto lvalue = llvm::dyn_cast<ec::LValueType>(value.getType())) {
        value = ec::LoadOp::create(b, where, lvalue.getValueType(), value);
    }
    if (value.getType() == target) { return value; }
    const auto string = carrierType(context, carrier::string);
    const auto rawString = ec::OpaqueType::get(context, kRawStringType);
    const auto numberString = carrierType(context, carrier::numberString);
    const auto nullableNumberString = carrierType(context, carrier::nullableNumberString);
    if (target == numberString && (isNumberCarrier(value.getType()) || value.getType() == string)) {
        return ec::CastOp::create(b, where, target, value);
    }
    if (target == nullableNumberString) {
        if (isNumberCarrier(value.getType()) || value.getType() == string) {
            return ec::CastOp::create(b, where, target, value);
        }
        return callWithConstValueOperands(b, where, mlir::TypeRange{target},
                                          b.getStringAttr("ctnative::to_nullable_number_string"),
                                          mlir::ValueRange{value})
            .getResult(0);
    }
    if (value.getType() == nullableNumberString && target == numberString) {
        return callWithConstValueOperands(b, where, mlir::TypeRange{target},
                                          b.getStringAttr("ctnative::global_number_string"),
                                          mlir::ValueRange{value})
            .getResult(0);
    }
    if (value.getType() == nullableNumberString && target == string) {
        return callWithConstValueOperands(b, where, mlir::TypeRange{target},
                                          b.getStringAttr("ctnative::nullable_number_string_text"),
                                          mlir::ValueRange{value})
            .getResult(0);
    }
    if (isNullableCarrier(value.getType()) && target == string) {
        return ec::MemberCallOpaqueOp::create(b, where, mlir::TypeRange{target}, value,
                                              b.getStringAttr("to_string"), mlir::ArrayAttr{},
                                              mlir::ArrayAttr{}, mlir::ValueRange{})
            .getResult(0);
    }
    if ((value.getType() == string || isNullableStringCarrier(value.getType())) &&
        isNumberCarrier(target)) {
        return ec::MemberCallOpaqueOp::create(b, where, mlir::TypeRange{target}, value,
                                              b.getStringAttr("to_number"), mlir::ArrayAttr{},
                                              mlir::ArrayAttr{}, mlir::ValueRange{})
            .getResult(0);
    }
    if ((isBooleanStringCarrier(value.getType()) || isNumberStringCarrier(value.getType())) &&
        target == string) {
        return callWithConstValueOperands(b, where, mlir::TypeRange{target},
                                          b.getStringAttr(isNumberStringCarrier(value.getType())
                                                              ? "ctnative::number_string_text"
                                                              : "ctnative::boolean_string_text"),
                                          mlir::ValueRange{value})
            .getResult(0);
    }
    if (value.getType() == rawString && target == string) {
        return ec::CastOp::create(b, where, target, value);
    }
    if (value.getType() == string && target == rawString) {
        return ec::MemberCallOpaqueOp::create(b, where, mlir::TypeRange{rawString}, value,
                                              b.getStringAttr("value"), mlir::ArrayAttr{},
                                              mlir::ArrayAttr{}, mlir::ValueRange{})
            .getResult(0);
    }
    if (value.getType() == string &&
        (target == ec::OpaqueType::get(context, kDOMOptionalStringType) ||
         target == ec::OpaqueType::get(context, kDOMJSONType) || isNullableStringCarrier(target) ||
         isBooleanStringCarrier(target))) {
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
        return ec::MemberCallOpaqueOp::create(b, where, mlir::TypeRange{target}, value,
                                              b.getStringAttr("value"), mlir::ArrayAttr{},
                                              mlir::ArrayAttr{}, mlir::ValueRange{})
            .getResult(0);
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
        auto present = ec::MemberCallOpaqueOp::create(
            inside, where, mlir::TypeRange{inside.getI1Type()}, optional,
            inside.getStringAttr("has_value"), mlir::ArrayAttr{}, mlir::ArrayAttr{},
            mlir::ValueRange{});
        auto text = ec::MemberCallOpaqueOp::create(
            inside, where, mlir::TypeRange{rawString}, optional, inside.getStringAttr("value"),
            mlir::ArrayAttr{}, mlir::ArrayAttr{}, mlir::ValueRange{});
        auto string = ec::CallOpaqueOp::create(inside, where, mlir::TypeRange{target},
                                               inside.getStringAttr(kDOMJSONType),
                                               mlir::ValueRange{text.getResult(0)});
        auto null = ec::ConstantOp::create(inside, where, target,
                                           ec::OpaqueAttr::get(context, "ctbrowser::json_value{}"));
        // The member access is inside the conditional expression's selected arm.
        auto joined = ec::ConditionalOp::create(inside, where, target, present.getResult(0),
                                                string.getResult(0), null);
        ec::YieldOp::create(inside, where, joined);
        return expression.getResult();
    } else if (isBooleanStringCarrier(target)) {
        helper = kBooleanStringType;
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
    auto result = callWithConstValueOperands(b, where, mlir::TypeRange{resultType},
                                             b.getStringAttr(helper), mlir::ValueRange{value})
                      .getResult(0);
    return resultType == target ? result : convertScalar(b, where, result, target);
}

mlir::Type lowering::joinedReturnType(ctjs::FuncOp fn) const {
    mlir::Type result = BottomType::get(context);
    fn.getBody().walk([&](ctjs::ReturnOp ret) { result = meet(result, typeOf(ret.getValue())); });
    return result;
}

void lowering::censusScalars(llvm::ArrayRef<ctjs::FuncOp> accepted,
                             const OwnedGlobalRoots * roots) {
    const auto scalarType = [&](mlir::Type type) -> mlir::Type {
        const auto c = carrierOf(type);
        if (!isScalarCarrier(c) && !isObjectCarrier(c) && !isStringCarrier(c) &&
            c != carrier::numberString && c != carrier::nullableNumberString) {
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

// SCF preserves JavaScript values through typed region edges. Materialize the
// scalar widening at each edge, including initial loop operands and backedges.
// Calls use the callee's joined parameter type, not the actual argument type.
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
        } else if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(op)) {
            for (unsigned i = 0; i < op->getNumOperands(); ++i) {
                convert(i, loop.getBefore().front().getArgument(i).getType());
            }
        } else if (auto loop = llvm::dyn_cast<mlir::scf::ForOp>(op)) {
            for (unsigned i = 3; i < op->getNumOperands(); ++i) {
                convert(i, loop->getResult(i - 3).getType());
            }
        } else if (llvm::isa<mlir::scf::YieldOp, ctjs::InvokeYieldOp>(op)) {
            // A proved invoke's String failure arm widens into its json result.
            mlir::Operation * parent = op->getParentOp();
            for (unsigned i = 0; i < op->getNumOperands(); ++i) {
                auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(parent);
                convert(i, loop ? loop.getBefore().front().getArgument(i).getType()
                                : parent->getResult(i).getType());
            }
        } else if (llvm::isa<mlir::scf::ConditionOp>(op)) {
            for (unsigned i = 1; i < op->getNumOperands(); ++i) {
                convert(i, op->getParentOp()->getResult(i - 1).getType());
            }
        }
    }
}

} // namespace ctcompile::ctnative::lowering_detail
