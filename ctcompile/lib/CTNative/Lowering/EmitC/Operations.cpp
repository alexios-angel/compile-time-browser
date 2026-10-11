//===- Operations.cpp - Rewrite ctjs ops to EmitC ---------------*- C++ -*-===//
//
// Per-operation rewrites of the EmitC lowering: lowering::replace() turns each
// ctjs operation of an admitted function into EmitC operations over the native
// carrier types, plus the shared helpers for cells, object members and the
// entry's printed observations.
//
//===----------------------------------------------------------------------===//
#include "../Admission/Admission.h"
#include "Emitter.h"
#include "Vectors.h"
#include "ctcompile/CTNative/IR/CTNativeOps.h"
#include "ctcompile/Support/CppLiterals.hpp"
#include "mlir/Dialect/Arith/IR/Arith.h"

namespace ctcompile::ctnative::lowering_detail {

/// The only way this lowering erases an operation. Erasing one that still has
/// uses is a fatal error: in a release build it would be a silent
/// use-after-free, which once showed up only as an intermittent crash at
/// context teardown.
void lowering::eraseIfUnused(mlir::Operation * o) {
    if (!o->use_empty()) {
        llvm::report_fatal_error(llvm::Twine("ctnative lowering: erasing `") +
                                 o->getName().getStringRef() + "` while it still has uses");
    }
    o->erase();
}

/// Returns the place a shared binding (a cell) is read from and written to.
/// In the frame that owns it the cell is an `emitc.variable`, already an
/// lvalue; a closure that captured it receives a pointer, and
/// `emitc.dereference` turns that into an lvalue, so reads and writes are
/// emitted the same way on both sides of the call.
mlir::Value lowering::cellPlace(mlir::OpBuilder & b, mlir::Location where, mlir::Value cell) {
    if (auto pointer = llvm::dyn_cast<ec::PointerType>(cell.getType())) {
        return ec::DereferenceOp::create(b, where, ec::LValueType::get(pointer.getPointee()), cell);
    }
    return cell;
}

/// Returns the lvalue for `object.member`. An object literal in this frame is
/// an `emitc.variable` and is accessed with `emitc.member` (`o.x`); a lifted
/// method's receiver arrives as a pointer and is accessed through a local
/// copy with `emitc.member_of_ptr` (`self->x`).
mlir::Value lowering::memberAccess(mlir::OpBuilder & b, mlir::Location where, mlir::Value object,
                                   llvm::StringRef member, mlir::Type type) {
    // An instance (EmitC/Instances.cpp) is a std::shared_ptr, read through
    // `->` like a pointer.
    if (!receiverArgs.contains(object) && !llvm::isa<ec::PointerType>(object.getType()) &&
        !isInstanceValue(object)) {
        return ec::MemberOp::create(b, where, ec::LValueType::get(type), member, object);
    }
    // The receiver's local is created lazily, at the first field access, so a
    // method that reads no field of its own gets no unused variable in the raw
    // `--ctnative-lower-to-emitc` output that lit tests check. (canonicalize
    // would delete it anyway.) It is inserted at the block start or right after
    // the receiver's definition, which dominates every use.
    mlir::Value & local = receiverLocal[object];
    if (!local) {
        mlir::OpBuilder at(context);
        if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(object)) {
            at.setInsertionPointToStart(argument.getOwner());
        } else {
            at.setInsertionPointAfter(object.getDefiningOp());
        }
        local = ec::VariableOp::create(at, where, ec::LValueType::get(object.getType()),
                                       ec::OpaqueAttr::get(context, ""));
        ec::AssignOp::create(at, where, local, object);
    }
    return ec::MemberOfPtrOp::create(b, where, ec::LValueType::get(type), member, local);
}

/// Emits the entry's observation of each printed global, before its `return 0`.
void lowering::printObservations(mlir::OpBuilder & b, mlir::Location where) {
    using namespace ctjs;
    const auto boolean = carrierType(context, carrier::boolean);
    // Print each global using the complete source-store type. Definite
    // observations check their tag; optional observations retain it.
    const auto names = sortedKeys(explicitObservations ? observations : globals);
    const auto textType = ec::PointerType::get(ec::OpaqueType::get(context, "const char"));
    for (llvm::StringRef name : names) {
        const auto print = [&](llvm::StringRef helper, mlir::Value value) {
            mlir::Value label = ec::LiteralOp::create(
                b, where, textType, b.getStringAttr(cpp::c_string_literal(name.str())));
            callWithConstValueOperands(b, where, mlir::TypeRange{}, b.getStringAttr(helper),
                                       mlir::ValueRange{label, value});
        };
        const auto storedType = globalTypes.lookup(name);
        static const llvm::DenseMap<carrier, llvm::StringRef> printHelpers{
            {carrier::booleanString, "ctnative::print_boolean_string"},
            {carrier::nullableBooleanString, "ctnative::print_nullable_boolean_string"},
            {carrier::numberString, "ctnative::print_number_string"},
            {carrier::nullableNumberString, "ctnative::print_nullable_number_string"}};
        if (auto helper = printHelpers.find(carrierOf(storedType)); helper != printHelpers.end()) {
            print(helper->second, convertScalar(b, where, lvalueOfGlobal(b, where, name),
                                                carrierType(context, carrierOf(storedType))));
            continue;
        }
        if (!isPrimitive(storedType)) {
            if (!isScalarCarrier(carrierOf(storedType)) &&
                !isStringCarrier(carrierOf(storedType))) {
                llvm::report_fatal_error("native global observation lacks a proved scalar carrier");
            }
            print("ctnative::print_scalar",
                  convertScalar(b, where, lvalueOfGlobal(b, where, name), globalStorageType(name)));
            continue;
        }
        if (llvm::isa<StrType>(storedType)) {
            mlir::Value loaded =
                convertScalar(b, where, lvalueOfGlobal(b, where, name), globalStorageType(name));
            print("ctnative::print_string",
                  callValue(b, where, ec::OpaqueType::get(context, kRawStringType),
                            "ctnative::global_string", loaded));
            continue;
        }
        const bool isBoolean = llvm::isa<BoolType>(storedType);
        mlir::Value loaded = convertScalar(b, where, lvalueOfGlobal(b, where, name),
                                           carrierType(context, carrier::nullable));
        mlir::Value current =
            callValue(b, where, isBoolean ? boolean : ec::OpaqueType::get(context, kNumberType),
                      isBoolean ? "ctnative::global_boolean" : "ctnative::global_number", loaded);
        if (isBoolean) {
            const auto literal = [&](llvm::StringRef spelling) -> mlir::Value {
                return ec::LiteralOp::create(b, where, textType, b.getStringAttr(spelling));
            };
            current = ec::ConditionalOp::create(b, where, textType, truthy(b, where, current),
                                                literal("\"true\""), literal("\"false\""));
        } else {
            // C varargs consume binary64, not the JavaScript value class.
            current = memberCall(b, where, b.getF64Type(), current, "value");
        }
        mlir::Value format = ec::LiteralOp::create(
            b, where, textType,
            b.getStringAttr(("\"" + name + (isBoolean ? "=%s\\n\"" : "=%.17g\\n\"")).str()));
        callWithConstValueOperands(b, where, mlir::TypeRange{}, b.getStringAttr("printf"),
                                   mlir::ValueRange{format, current});
    }
}

void lowering::replace(mlir::Operation * o, bool isEntry, mlir::Type returnType) {
    if (replaceStaticTable(o)) { return; }
    if (replaceInstance(o)) { return; }
    if (replaceDOM(o)) { return; }
    if (replaceOwnedGlobal(o)) { return; }
    if (replaceException(o)) { return; }
    if (replaceMethodTable(o)) { return; }
    if (replaceEnvironment(o)) { return; }
    using namespace ctjs;
    mlir::OpBuilder b(o);
    const mlir::Location where = o->getLoc();
    const auto numeric = carrierType(context, carrier::number);
    const auto i1 = mlir::IntegerType::get(context, 1);
    const auto boolean = carrierType(context, carrier::boolean);
    const auto swap = [&](mlir::Value with) {
        if (isBooleanCarrier(o->getResult(0).getType()) && with.getType().isInteger(1)) {
            with = convertScalar(b, where, with, boolean);
        }
        o->getResult(0).replaceAllUsesWith(with);
        eraseIfUnused(o);
    };

    if (replaceMap(o)) { return; }
    if (replaceIdentityField(o)) { return; }
    if (replaceStringCallback(o)) { return; }
    if (replaceStringValue(o)) { return; }
    if (replaceStringArray(*this, o)) { return; }
    if (auto refined = llvm::dyn_cast<RefineOp>(o)) {
        // Admission allowed only the operand's own carrier or its String or
        // Number alternative.
        const mlir::Value value = refined.getValue();
        const mlir::Type result = o->getResult(0).getType();
        swap(value.getType() == result
                 ? value
                 : callValue(b, where, result,
                             refined.getKind() == "number" ? "ctnative::as_number"
                                                           : "ctnative::as_string",
                             value));
        return;
    }

    // Frame bookkeeping lowers to nothing. frame_enter's result is used by
    // every frame_exit and root after it, and walk order visits it first, so
    // its users are erased now and it is erased in the sweep at the end.
    if (llvm::isa<FrameExitOp, RootOp>(o)) {
        eraseIfUnused(o);
        return;
    }
    if (llvm::isa<FrameEnterOp>(o)) { return; }
    if (o->getName().getStringRef() == "ub.poison") {
        // Lifted control flags already have a native integer/index type.
        // Preserve it on every edge, including uses by arith operations.
        const auto type = o->getResult(0).getType();
        if (llvm::isa<mlir::IndexType>(type)) {
            // The arith conversion owns index -> size_t, including its users.
            swap(mlir::arith::ConstantIndexOp::create(b, where, 0));
            return;
        }
        if (llvm::isa<mlir::IntegerType>(type)) {
            swap(ec::ConstantOp::create(b, where, type, b.getZeroAttr(type)));
            return;
        }
        // CFG structuring uses poison for dead carried slots. The
        // lattice joins bottom with the live incoming type, so one
        // poison can feed both string and numeric slots. Choose an
        // inert value per destination instead of giving every use NaN.
        mlir::Value emptyString, emptyNullableString, emptyNullable, emptyBoolean, emptyObjectValue,
            emptyIdentity;
        llvm::DenseMap<mlir::Type, mlir::Value> emptyPairs;
        // Each inert value is built once, at its first use.
        const auto once = [](mlir::Value & slot, auto make) {
            if (!slot) { slot = make(); }
            return slot;
        };
        const auto opaque = [&](carrier c, llvm::StringRef spelling) {
            return ec::ConstantOp::create(b, where, carrierType(context, c),
                                          ec::OpaqueAttr::get(context, spelling))
                .getResult();
        };
        for (mlir::OpOperand & use : llvm::make_early_inc_range(o->getResult(0).getUses())) {
            mlir::Operation * user = use.getOwner();
            const unsigned index = use.getOperandNumber();
            mlir::Type expected;
            if (!llvm::isa<mlir::scf::YieldOp>(user) ||
                llvm::isa<mlir::scf::WhileOp, mlir::scf::IfOp, mlir::scf::ForOp>(
                    user->getParentOp())) {
                expected = edgeTarget(user, index);
            }
            if (auto exit = llvm::dyn_cast<ctjs::TryExitOp>(user)) {
                auto parent = exit->getParentOfType<ctjs::TryOp>();
                if (index == 1) {
                    expected = parent.getResult().getType();
                } else if (index == 2) {
                    expected = parent.getCatchBody().front().getArgument(0).getType();
                } else if (index > 2) {
                    expected = llvm::cast<ec::LValueType>(
                                   exceptionSlots[parent].state[index - 3].getType())
                                   .getValueType();
                }
            }
            const auto blank = [&] {
                return once(emptyString, [&] { return stringConstant(b, where, ""); });
            };
            if (expected == carrierType(context, carrier::string)) {
                use.set(blank());
            } else if (isNumberStringCarrier(expected) || isBooleanStringCarrier(expected) ||
                       isNullableNumberStringCarrier(expected) ||
                       isNullableBooleanStringCarrier(expected)) {
                use.set(convertScalar(b, where, blank(), expected));
            } else if (isNullableStringCarrier(expected)) {
                use.set(once(emptyNullableString, [&] {
                    return opaque(carrier::nullableString, "ctnative::nullable_string{}");
                }));
            } else if (isObjectValueCarrier(expected)) {
                needsObjectValue = true;
                use.set(once(emptyObjectValue, [&] {
                    return opaque(carrier::objectValue, "ctnative::object_value{}");
                }));
            } else if (isIdentityCarrier(expected)) {
                needsObjectIdentity = true;
                use.set(once(emptyIdentity, [&] {
                    return opaque(carrier::objectIdentity,
                                  "std::shared_ptr<ctnative::identity_object>{}");
                }));
            } else if (isNullableCarrier(expected)) {
                use.set(once(emptyNullable, [&] { return absentConstant(b, where); }));
            } else if (expected == carrierType(context, carrier::nullableElement)) {
                use.set(opaque(carrier::nullableElement, "std::nullopt"));
            } else if (expected == i1 || isBooleanCarrier(expected)) {
                use.set(convertScalar(
                    b, where, once(emptyBoolean, [&] { return boolConstant(b, where, false); }),
                    expected));
            } else if (auto pair = llvm::dyn_cast_or_null<ec::OpaqueType>(expected);
                       pair && pair.getValue().starts_with("std::pair<")) {
                // A row of a pair vector, such as a folded for-of's element.
                use.set(once(emptyPairs[expected], [&] {
                    return ec::ConstantOp::create(
                               b, where, expected,
                               ec::OpaqueAttr::get(context, (pair.getValue() + "{}").str()))
                        .getResult();
                }));
            }
        }
        swap(numberConstant(b, where, std::numeric_limits<double>::quiet_NaN()));
        return;
    }
    // A static cell (ClosureLifting/RunOnce.cpp) is a namespace-scope variable
    // rather than a frame-local one. The box itself assigns the initial where
    // the frame made it; a reference in another function only names the
    // variable.
    if (auto cell = llvm::dyn_cast<CreateCellOp>(o)) {
        if (auto name = o->getAttrOfType<mlir::StringAttr>(kNativeStaticCell)) {
            const auto type = llvm::cast<ec::LValueType>(cell.getResult().getType());
            statics.try_emplace(name.str(), type.getValueType());
            mlir::Value place = ec::GetGlobalOp::create(
                b, where, type, mlir::FlatSymbolRefAttr::get(context, name.getValue()));
            if (!o->hasAttr(kNativeStaticReference)) {
                ec::AssignOp::create(
                    b, where, place,
                    convertScalar(b, where, cell.getInitial(), type.getValueType()));
            }
            swap(place);
            return;
        }
    }
    // A shared binding (`ctjs.create_cell`) becomes a local variable in this
    // frame, initialised to the cell's initial value; `cell_get` and `cell_set`
    // load and assign it. A hoisted `var` starts as tagged undefined, so a read
    // on a path with no `cell_set` yields what the interpreter yields, and no
    // dominance proof is needed.
    if (auto cell = llvm::dyn_cast<CreateCellOp>(o); cell && admission::isCarriedCell(o)) {
        mlir::Value local = ec::VariableOp::create(b, where, cell.getResult().getType(),
                                                   ec::OpaqueAttr::get(context, ""));
        // Inference omits this initial only when a write dominates every
        // read, including captured reads. A definite union then need not
        // represent the hoisted undefined; observable initials still do.
        if (!cell->hasAttr(kAssignedBeforeRead)) {
            ec::AssignOp::create(
                b, where, local,
                convertScalar(b, where, cell.getInitial(),
                              llvm::cast<ec::LValueType>(local.getType()).getValueType()));
        }
        swap(local);
        return;
    }
    if (auto get = llvm::dyn_cast<CellGetOp>(o)) {
        mlir::Value place = cellPlace(b, where, get.getCell());
        auto loaded = ec::LoadOp::create(
            b, where, llvm::cast<ec::LValueType>(place.getType()).getValueType(), place);
        swap(convertScalar(b, where, loaded, get.getResult().getType()));
        return;
    }
    if (auto set = llvm::dyn_cast<CellSetOp>(o)) {
        mlir::Value place = cellPlace(b, where, set.getCell());
        ec::AssignOp::create(
            b, where, place,
            convertScalar(b, where, set.getValue(),
                          llvm::cast<ec::LValueType>(place.getType()).getValueType()));
        eraseIfUnused(o);
        return;
    }
    if (auto object = llvm::dyn_cast<CreateObjectOp>(o)) {
        if (o->hasAttr(kNativeObjectIdentity)) {
            swap(callValue(b, where, object.getResult().getType(),
                           "std::make_shared<ctnative::identity_object>", mlir::ValueRange{}));
            return;
        }
        // The struct, by value, in this frame. Fields with observable
        // absence start as tagged undefined. A definite field's initial
        // numeric/boolean value is overwritten before any proved read.
        const siteShape & site = shapeAt(object.getResult());
        const family & f = families[site.family];
        mlir::Value local =
            ec::VariableOp::create(b, where, classType(site), ec::OpaqueAttr::get(context, ""));
        // Each member takes the site's concrete carrier, never the family's
        // template parameter: the assign and the load after it are typed
        // ops over the actual carrier, and `T0` is a spelling that exists
        // only inside the class.
        for (unsigned i = 0; i < f.fields.size(); ++i) {
            const mlir::Type type = site.types[i];
            // The other admitted opaque field is an owning method-table
            // shared_ptr. Its default constructor creates an empty handle;
            // the checked slot initialization precedes every possible read.
            if (llvm::isa<ec::OpaqueType>(type) && !isNullableCarrier(type)) { continue; }
            mlir::Value member =
                ec::MemberOp::create(b, where, ec::LValueType::get(type), f.fields[i], local);
            mlir::Value init =
                isNullableCarrier(type) ? absentConstant(b, where)
                : isBooleanCarrier(type)
                    ? boolConstant(b, where, false)
                    : numberConstant(b, where, std::numeric_limits<double>::quiet_NaN());
            ec::AssignOp::create(b, where, member, init);
        }
        swap(local);
        return;
    }
    if (auto array = llvm::dyn_cast<CreateArrayOp>(o)) {
        // The vector, by value, in this frame - default-constructed, which
        // is the empty array the appends below fill.
        mlir::Value local = ec::VariableOp::create(b, where, vectorCarrierType(context),
                                                   ec::OpaqueAttr::get(context, ""));
        for (mlir::Value element : array.getElements()) { push(b, where, local, element); }
        swap(local);
        return;
    }
    if (auto append = llvm::dyn_cast<AppendOp>(o)) {
        push(b, where, append.getArray(), append.getElement());
        eraseIfUnused(o);
        return;
    }
    if (vectorLengthReads.contains(o)) {
        swap(callValue(b, where, numeric, "ctnative::vec_length",
                       cellPlace(b, where, o->getOperand(0))));
        return;
    }
    if (vectorIndexReads.contains(o)) {
        const auto resultType = o->getResult(0).getType();
        // Only a proved Number drops absence. String snapshots use their own
        // vec_at overload and retain that overload's nullable String carrier.
        const auto storageType =
            isNumberCarrier(resultType) ? carrierType(context, carrier::nullable) : resultType;
        const auto value =
            callValue(b, where, storageType, "ctnative::vec_at",
                      mlir::ValueRange{cellPlace(b, where, o->getOperand(0)), o->getOperand(1)});
        swap(convertScalar(b, where, value, resultType));
        return;
    }
    if (vectorLengthWrites.contains(o)) {
        ec::VerbatimOp::create(
            b, where,
            llvm::isa<ec::PointerType>(o->getOperand(0).getType())
                ? "{}->resize(static_cast<std::vector<double>::size_type>({}));"
                : "{}.resize(static_cast<std::vector<double>::size_type>({}));",
            mlir::ValueRange{o->getOperand(0),
                             convertScalar(b, where, o->getOperand(2), b.getF64Type())});
        eraseIfUnused(o);
        return;
    }
    if (vectorIndexWrites.contains(o)) {
        // EmitC subscript cannot take an opaque lvalue; loading it would copy
        // the vector. Keep this ordinary assignment on the original storage.
        ec::VerbatimOp::create(
            b, where,
            llvm::isa<ec::PointerType>(o->getOperand(0).getType())
                ? "(*{})[static_cast<std::vector<double>::size_type>({})] = {};"
                : "{}[static_cast<std::vector<double>::size_type>({})] = {};",
            mlir::ValueRange{o->getOperand(0),
                             convertScalar(b, where, o->getOperand(1), b.getF64Type()),
                             convertScalar(b, where, o->getOperand(2), b.getF64Type())});
        eraseIfUnused(o);
        return;
    }
    // A method field is not stored in the object, so its store is erased and
    // the closure it held loses its last use and is erased in the sweep. `this` is a parameter
    // and the method is a free function; there is nothing to write.
    if (o->hasAttr("ctnative.method")) {
        eraseIfUnused(o);
        return;
    }
    if (auto get = llvm::dyn_cast<GetPropertyOp>(o)) {
        const mlir::Type type = get.getResult().getType();
        const mlir::Type storage = accessType.lookup(o);
        auto loaded = ec::LoadOp::create(
            b, where, storage, memberAccess(b, where, get.getObject(), memberName(o), storage));
        swap(convertScalar(b, where, loaded, type));
        return;
    }
    if (auto set = llvm::dyn_cast<SetPropertyOp>(o)) {
        ec::AssignOp::create(
            b, where, memberAccess(b, where, set.getObject(), memberName(o), accessType.lookup(o)),
            convertScalar(b, where, set.getValue(), accessType.lookup(o)));
        eraseIfUnused(o);
        return;
    }
    if (admission::isDeclarationStore(o)) {
        mlir::Operation * closure = llvm::cast<StoreGlobalOp>(o).getValue().getDefiningOp();
        eraseIfUnused(o);
        eraseIfUnused(closure);
        return;
    }
    if (auto k = llvm::dyn_cast<ConstantOp>(o)) {
        if (auto n = llvm::dyn_cast<NumberAttr>(k.getValue())) {
            swap(numberConstant(b, where, n.getDouble()));
        } else if (auto bo = llvm::dyn_cast<BooleanAttr>(k.getValue())) {
            swap(boolConstant(b, where, bo.getValue()));
        } else if (llvm::isa<UndefinedAttr, NullAttr>(k.getValue())) {
            swap(absentConstant(b, where, llvm::isa<NullAttr>(k.getValue())));
        } else if (auto string = llvm::dyn_cast<StringAttr>(k.getValue());
                   string && k.getResult().getType() == carrierType(context, carrier::string)) {
            swap(stringConstant(b, where, string.getValue()));
        } else {
            // Erased property-key constants need only a dead placeholder.
            swap(numberConstant(b, where, std::numeric_limits<double>::quiet_NaN()));
        }
        return;
    }
    if (llvm::isa<BinaryOp, BinaryStaticOp>(o)) {
        const auto kind = llvm::isa<BinaryOp>(o) ? llvm::cast<BinaryOp>(o).getKind()
                                                 : llvm::cast<BinaryStaticOp>(o).getKind();
        const auto resultType = o->getResult(0).getType();
        const bool strings = resultType == carrierType(context, carrier::string);
        const mlir::Value l = strings ? o->getOperand(0) : number(b, where, o->getOperand(0));
        const mlir::Value r = strings ? o->getOperand(1) : number(b, where, o->getOperand(1));
        switch (kind) {
        case BinaryKind::Add:
        case BinaryKind::Concat: swap(ec::AddOp::create(b, where, resultType, l, r)); return;
        case BinaryKind::Sub: swap(ec::SubOp::create(b, where, numeric, l, r)); return;
        case BinaryKind::Mul: swap(ec::MulOp::create(b, where, numeric, l, r)); return;
        case BinaryKind::Div: swap(ec::DivOp::create(b, where, numeric, l, r)); return;
        case BinaryKind::Mod: swap(libmCall(b, where, "std::fmod", {l, r})); return;
        case BinaryKind::Pow: swap(exponentiate(b, where, l, r)); return;
        case BinaryKind::BitAnd: swap(ec::BitwiseAndOp::create(b, where, numeric, l, r)); return;
        case BinaryKind::BitOr: swap(ec::BitwiseOrOp::create(b, where, numeric, l, r)); return;
        case BinaryKind::BitXor: swap(ec::BitwiseXorOp::create(b, where, numeric, l, r)); return;
        case BinaryKind::Shl: swap(ec::BitwiseLeftShiftOp::create(b, where, numeric, l, r)); return;
        case BinaryKind::Shr:
            swap(ec::BitwiseRightShiftOp::create(b, where, numeric, l, r));
            return;
        case BinaryKind::UShr:
            swap(memberCall(b, where, numeric, l, "unsigned_shift_right", r));
            return;
        default: llvm_unreachable("admission refused it");
        }
    }
    if (auto u = llvm::dyn_cast<UnaryOp>(o)) {
        switch (u.getKind()) {
        case UnaryKind::BitNot:
            swap(ec::BitwiseNotOp::create(b, where, numeric, number(b, where, u.getOperand())));
            return;
        case UnaryKind::Neg:
            swap(ec::UnaryMinusOp::create(b, where, numeric, number(b, where, u.getOperand())));
            return;
        // Numeric `+x` was already erased by the UnaryPlusIsIdentity pattern
        // (LoweringSupport.cpp, run from applyDeclarativeRules() in Functions.cpp).
        // A pattern that silently stops matching produces no diagnostic, so this
        // arm reports the pattern by name instead of reaching the generic
        // "admission refused it" abort.
        case UnaryKind::Plus:
            if (u.getOperand().getType() == numeric) {
                llvm::report_fatal_error("ctnative lowering: numeric unary plus survived "
                                         "UnaryPlusIsIdentity");
            }
            swap(number(b, where, u.getOperand()));
            return;
        case UnaryKind::TypeOf:
            if (u.getOperand().getType() == carrierType(context, carrier::symbol)) {
                swap(stringConstant(b, where, "symbol"));
            } else if (isObjectValueCarrier(u.getOperand().getType()) ||
                       isNullableCarrier(u.getOperand().getType())) {
                const bool object = isObjectValueCarrier(u.getOperand().getType());
                needsObjectValue |= object;
                swap(convertScalar(
                    b, where,
                    callValue(b, where, ec::OpaqueType::get(context, kRawStringType),
                              object ? "ctnative::object_typeof" : "ctnative::scalar_typeof",
                              u.getOperand()),
                    carrierType(context, carrier::string)));
            } else if (isIdentityCarrier(u.getOperand().getType())) {
                swap(stringConstant(b, where, "object"));
            } else {
                swap(stringConstant(b, where,
                                    u.getOperand().getType() == numeric          ? "number"
                                    : isBooleanCarrier(u.getOperand().getType()) ? "boolean"
                                                                                 : "string"));
            }
            return;
        case UnaryKind::Not: {
            swap(ec::LogicalNotOp::create(b, where, i1, truthy(b, where, u.getOperand())));
            return;
        }
        default: llvm_unreachable("admission refused it");
        }
    }
    if (auto cmp = llvm::dyn_cast<CompareOp>(o)) {
        const bool equality =
            cmp.getKind() == CompareKind::Eq || cmp.getKind() == CompareKind::StrictEq;
        mlir::Value left = cmp.getLhs(), right = cmp.getRhs();
        const bool objects =
            isObjectValueCarrier(left.getType()) || isObjectValueCarrier(right.getType()) ||
            isIdentityCarrier(left.getType()) || isIdentityCarrier(right.getType());
        if (equality && (objects || isNullableCarrier(left.getType()) ||
                         isNullableCarrier(right.getType()) || left.getType() != right.getType())) {
            needsObjectValue |= objects;
            const bool loose = cmp.getKind() == CompareKind::Eq;
            const auto helper =
                objects ? (loose ? "ctnative::object_equal" : "ctnative::object_strict_equal")
                        : (loose ? "ctnative::scalar_equal" : "ctnative::scalar_strict_equal");
            swap(callValue(b, where, boolean, helper, mlir::ValueRange{left, right}));
            return;
        }
        if (!equality) {
            left = number(b, where, left);
            right = number(b, where, right);
        }
        ec::CmpPredicate p = ec::CmpPredicate::eq;
        switch (cmp.getKind()) {
        case CompareKind::Lt: p = ec::CmpPredicate::lt; break;
        case CompareKind::Le: p = ec::CmpPredicate::le; break;
        case CompareKind::Gt: p = ec::CmpPredicate::gt; break;
        case CompareKind::Ge: p = ec::CmpPredicate::ge; break;
        case CompareKind::Eq:
        case CompareKind::StrictEq: p = ec::CmpPredicate::eq; break;
        }
        swap(ec::CmpOp::create(b, where, i1, p, left, right));
        return;
    }
    if (auto t = llvm::dyn_cast<TruthyOp>(o)) {
        swap(truthy(b, where, t.getValue()));
        return;
    }
    if (auto load = llvm::dyn_cast<LoadGlobalOp>(o)) {
        // The global `undefined` is the undefined value; it has no storage.
        if (TypeInference::isUndefinedGlobal(o)) {
            swap(convertScalar(b, where, absentConstant(b, where), load.getResult().getType()));
            return;
        }
        if (admission::feedsOnlyDirectCallees(load.getResult())) {
            // Every use is a call_direct's callee-value operand, and the
            // call is rewritten below without it; by the sweep it is dead.
            return;
        }
        swap(convertScalar(b, where, lvalueOfGlobal(b, where, load.getName()),
                           load.getResult().getType()));
        return;
    }
    if (auto call = llvm::dyn_cast<CallDirectOp>(o)) {
        // The three implicit operands go; the rest are the parameters,
        // in the callee's own order. The callee symbol still names the
        // ctjs.func here; SymbolTable::replaceAllSymbolUses renames it
        // to the emitc.func when that function is lowered, whichever
        // order the two are visited in.
        const auto operands = call.getArgOperands();
        llvm::SmallVector<mlir::Value> args;
        // A call to a lifted method (marked `ctnative.receiver`) passes the
        // receiver as the first argument, by pointer: an object literal in this
        // frame is an lvalue and needs its address taken; a receiver forwarded
        // by `this.other()` is already a pointer.
        const auto asPointer = [&](mlir::Value v) {
            return llvm::isa<ec::PointerType>(v.getType())
                       ? v
                       : ec::AddressOfOp::create(
                             b, where,
                             ec::PointerType::get(
                                 llvm::cast<ec::LValueType>(v.getType()).getValueType()),
                             v)
                             .getResult();
        };
        if (o->hasAttr("ctnative.receiver")) {
            args.push_back(isInstanceValue(call.getReceiver()) ? receiverOperand(b, where, call)
                                                               : asPointer(call.getReceiver()));
        }
        // Object arguments and shared bindings (cells) are also passed by
        // pointer through asPointer, so all three cases take an address the same
        // way. A function that itself received a cell pointer forwards it
        // unchanged, which is how a binding several closures out reaches the
        // innermost one. Operands 0-2 are the implicit arguments dropped above.
        for (unsigned i = 3; i < operands.size(); ++i) {
            const bool byAddress = admission::isObjectArg(o, i) || admission::isCellArg(o, i);
            args.push_back(byAddress                      ? asPointer(operands[i])
                           : isInstanceValue(operands[i]) ? instanceArgument(b, where, call, i)
                                                          : operands[i]);
        }
        const auto named = names.find(call.getCallee());
        const std::string target =
            named == names.end() ? cIdentifier(call.getCallee()) : named->second;
        auto made = ec::CallOp::create(b, where, mlir::SymbolRefAttr::get(context, target),
                                       mlir::TypeRange{o->getResult(0).getType()}, args);
        swap(made.getResult(0));
        return;
    }
    if (auto store = llvm::dyn_cast<StoreGlobalOp>(o)) {
        mlir::Value value = store.getValue();
        if (scalarStores.erase(o) && isObjectValueCarrier(value.getType())) {
            value = callValue(b, where, carrierType(context, carrier::nullable),
                              "ctnative::global_scalar", value);
        }
        ec::AssignOp::create(b, where, lvalueOfGlobal(b, where, store.getName()),
                             convertScalar(b, where, value, globalStorageType(store.getName())));
        eraseIfUnused(o);
        return;
    }
    if (auto ret = llvm::dyn_cast<ReturnOp>(o)) {
        if (isEntry) {
            printObservations(b, where);
            mlir::Value zero = ec::ConstantOp::create(b, where, mlir::IntegerType::get(context, 32),
                                                      b.getI32IntegerAttr(0));
            ec::ReturnOp::create(b, where, zero);
        } else if (llvm::isa<mlir::NoneType>(returnType)) {
            ec::ReturnOp::create(b, where, mlir::Value{});
        } else {
            ec::ReturnOp::create(b, where, convertScalar(b, where, ret.getValue(), returnType));
        }
        eraseIfUnused(o);
        return;
    }
    // scf ops stay for --convert-scf-to-emitc.
}

} // namespace ctcompile::ctnative::lowering_detail
