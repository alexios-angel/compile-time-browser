// EmitC/Operations.cpp - native lowering implementation.
#include "../Admission/Admission.h"
#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

// THE ONLY ERASE. An operation with uses is never erased: in a release
// build that is a use-after-free with no diagnostic - it surfaced as a
// crash at context teardown that came and went with the heap layout.
void lowering::eraseIfUnused(mlir::Operation * o) {
    if (!o->use_empty()) {
        llvm::report_fatal_error(llvm::Twine("ctnative lowering: erasing `") +
                                 o->getName().getStringRef() + "` while it still has uses");
    }
    o->erase();
}

// `o.x` OR `self->x`, DECIDED BY WHAT THE OBJECT IS. A literal in this
// frame is an `emitc.variable` and takes `emitc.member`; a lifted method's
// receiver arrived as a pointer and takes `emitc.member_of_ptr` through the
// local `lower()` made for it. Both give an lvalue, so the read and the
// write above are the same two lines either way.
// `v` OR `*p`, DECIDED BY WHAT THE BINDING IS - the exact shape of
// memberAccess one level down, and here for the same reason: a read and a
// write of a shared binding are the same two lines whichever side of the
// call they are on. The variable in the owning frame is already an lvalue;
// a capture parameter is a pointer, and `emitc.dereference` is the lvalue
// one indirection through it.
mlir::Value lowering::cellPlace(mlir::OpBuilder & b, mlir::Location where, mlir::Value cell) {
    if (auto pointer = llvm::dyn_cast<ec::PointerType>(cell.getType())) {
        return ec::DereferenceOp::create(b, where, ec::LValueType::get(pointer.getPointee()), cell);
    }
    return cell;
}

mlir::Value lowering::memberAccess(mlir::OpBuilder & b, mlir::Location where, mlir::Value object,
                                   llvm::StringRef member, mlir::Type type) {
    if (!receiverArgs.contains(object)) {
        return ec::MemberOp::create(b, where, ec::LValueType::get(type), member, object);
    }
    // AT THE FIRST FIELD, NOT AT THE FUNCTION'S FIRST LINE - AND THIS IS A
    // TIDINESS CHOICE, WHICH IS NOT WHAT THE COMMENT HERE FIRST CLAIMED.
    // `twice() { return this.area() * 2; }` reads no field of its own, so
    // building the local eagerly leaves `ctn_h_w * v2; v2 = v1;` with
    // nothing reading v2, and the prediction was that -Werror would reject
    // it. Measured: it does not. The pipeline's `canonicalize` deletes the
    // dead `emitc.variable` before any C++ is printed, `twice` compiles
    // clean either way, and the whole suite is green with the eager form.
    // So this buys nothing the pipeline was not already buying - what it
    // buys is that the RAW `--ctnative-lower-to-emitc` output, which the
    // lit and the printing gate read, has no variable no one reads in it.
    // Block start still dominates every use, so building it here is free.
    mlir::Value & local = receiverLocal[object];
    if (!local) {
        mlir::OpBuilder at =
            mlir::OpBuilder::atBlockBegin(llvm::cast<mlir::BlockArgument>(object).getOwner());
        local = ec::VariableOp::create(at, where, ec::LValueType::get(object.getType()),
                                       ec::OpaqueAttr::get(context, ""));
        ec::AssignOp::create(at, where, local, object);
    }
    return ec::MemberOfPtrOp::create(b, where, ec::LValueType::get(type), member, local);
}

void lowering::replace(mlir::Operation * o, bool isEntry, mlir::Type returnType) {
    using namespace ctjs;
    mlir::OpBuilder b(o);
    const mlir::Location where = o->getLoc();
    const auto f64 = mlir::Float64Type::get(context);
    const auto i1 = mlir::IntegerType::get(context, 1);
    const auto swap = [&](mlir::Value with) {
        o->getResult(0).replaceAllUsesWith(with);
        eraseIfUnused(o);
    };

    if (isNativeMapBookkeeping(o)) {
        swap(f64Constant(b, where, std::numeric_limits<double>::quiet_NaN()));
        return;
    }
    if (auto made = llvm::dyn_cast<ConstructOp>(o); made && o->hasAttr(kNativeMapSite)) {
        auto map = llvm::cast<MapType>(typeOf(made.getResult()));
        const std::string callee =
            ("ctnative::make_number_map<" + mapKeySpelling(map.getKeyType()) + ">").str();
        swap(ec::CallOpaqueOp::create(b, where, mlir::TypeRange{made.getResult().getType()},
                                      b.getStringAttr(callee), mlir::ValueRange{})
                 .getResult(0));
        return;
    }
    if (const llvm::StringRef action = nativeMapAction(o); !action.empty()) {
        llvm::SmallVector<mlir::Value> args;
        if (auto call = llvm::dyn_cast<CallOp>(o)) {
            args.push_back(call.getReceiver());
            llvm::append_range(args, call.getArgs());
        } else {
            args.push_back(llvm::cast<GetPropertyOp>(o).getObject());
        }
        const auto name = b.getStringAttr(("ctnative::map_" + action).str());
        if (action == "clear") {
            ec::CallOpaqueOp::create(b, where, mlir::TypeRange{}, name, args);
            swap(f64Constant(b, where, std::numeric_limits<double>::quiet_NaN()));
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
        return;
    }

    // FRAME BOOKKEEPING LOWERS TO NOTHING - but frame_enter's result is
    // used by every frame_exit and root after it, and walk order visits
    // it first, so its users go now and it goes in the sweep at the end.
    if (llvm::isa<FrameExitOp, RootOp>(o)) {
        eraseIfUnused(o);
        return;
    }
    if (llvm::isa<FrameEnterOp>(o)) { return; }
    if (o->getName().getStringRef() == "ub.poison") {
        // CFG structuring uses poison for dead carried slots. The
        // lattice joins bottom with the live incoming type, so one
        // poison can feed both string and numeric slots. Choose an
        // inert value per destination instead of giving every use NaN.
        mlir::Value emptyString;
        for (mlir::OpOperand & use : llvm::make_early_inc_range(o->getResult(0).getUses())) {
            mlir::Operation * user = use.getOwner();
            const unsigned index = use.getOperandNumber();
            mlir::Type expected;
            if (llvm::isa<mlir::scf::YieldOp>(user)) {
                mlir::Operation * parent = user->getParentOp();
                if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(parent)) {
                    expected = loop.getBefore().front().getArgument(index).getType();
                } else if (llvm::isa<mlir::scf::IfOp, mlir::scf::ForOp>(parent)) {
                    expected = parent->getResult(index).getType();
                }
            } else if (llvm::isa<mlir::scf::ConditionOp>(user) && index > 0) {
                expected = user->getParentOp()->getResult(index - 1).getType();
            } else if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(user)) {
                expected = loop.getBefore().front().getArgument(index).getType();
            } else if (llvm::isa<mlir::scf::ForOp>(user) && index >= 3) {
                expected = user->getResult(index - 3).getType();
            }
            if (expected == carrierType(context, carrier::string)) {
                if (!emptyString) { emptyString = stringConstant(b, where, ""); }
                use.set(emptyString);
            }
        }
        swap(f64Constant(b, where, std::numeric_limits<double>::quiet_NaN()));
        return;
    }
    // PHASE 59 SLICE 2 STEP 2: THE SHARED BINDING, AS A VARIABLE AND TWO
    // ACCESSES OF IT.
    //
    // `ctjs.create_cell` is a `double v;` in this frame, assigned the
    // box's INITIAL - which for a hoisted `var` is the `undefined` the
    // compiler boxed, carried as NaN. That assignment is not decoration:
    // a read on a path that reaches no `ctjs.cell_set` yields exactly what
    // the interpreter yields, which is the whole reason this rule needs no
    // dominance proof where step 1 needed two.
    if (auto cell = llvm::dyn_cast<CreateCellOp>(o); cell && admission::isCarriedCell(o)) {
        mlir::Value local = ec::VariableOp::create(b, where, cell.getResult().getType(),
                                                   ec::OpaqueAttr::get(context, ""));
        if (llvm::cast<ec::LValueType>(local.getType()).getValueType() ==
                carrierType(context, carrier::string) &&
            cell.getInitial().getType() != carrierType(context, carrier::string)) {
            if (!cell->hasAttr(kAssignedBeforeRead)) {
                llvm::report_fatal_error("ctnative lowering: a string cell has an observable "
                                         "non-string initial - admission should refuse it");
            }
        } else {
            ec::AssignOp::create(b, where, local, cell.getInitial());
        }
        swap(local);
        return;
    }
    if (auto get = llvm::dyn_cast<CellGetOp>(o)) {
        mlir::Value place = cellPlace(b, where, get.getCell());
        swap(ec::LoadOp::create(b, where,
                                llvm::cast<ec::LValueType>(place.getType()).getValueType(), place));
        return;
    }
    if (auto set = llvm::dyn_cast<CellSetOp>(o)) {
        ec::AssignOp::create(b, where, cellPlace(b, where, set.getCell()), set.getValue());
        eraseIfUnused(o);
        return;
    }
    if (auto object = llvm::dyn_cast<CreateObjectOp>(o)) {
        // The struct, by value, in this frame; every field set to its
        // undefined - NaN for a number, false for a boolean - before the
        // first store, so a read before a write is exact.
        const siteShape & site = shapeAt(object.getResult());
        const family & f = families[site.family];
        mlir::Value local =
            ec::VariableOp::create(b, where, classType(site), ec::OpaqueAttr::get(context, ""));
        // THE MEMBER TAKES THE SITE'S CONCRETE CARRIER, never the family's
        // template parameter: the assign and the load after it are typed
        // ops over a `double` or a `bool`, and `T0` is a spelling that
        // exists only inside the class.
        for (unsigned i = 0; i < f.fields.size(); ++i) {
            const mlir::Type type = site.types[i];
            mlir::Value member =
                ec::MemberOp::create(b, where, ec::LValueType::get(type), f.fields[i], local);
            mlir::Value init =
                llvm::isa<mlir::IntegerType>(type)
                    ? boolConstant(b, where, false)
                    : f64Constant(b, where, std::numeric_limits<double>::quiet_NaN());
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
        swap(ec::CallOpaqueOp::create(b, where, mlir::TypeRange{f64},
                                      b.getStringAttr("ctnative::vec_length"),
                                      mlir::ValueRange{o->getOperand(0)})
                 .getResult(0));
        return;
    }
    if (vectorIndexReads.contains(o)) {
        swap(ec::CallOpaqueOp::create(b, where, mlir::TypeRange{f64},
                                      b.getStringAttr("ctnative::vec_at"),
                                      mlir::ValueRange{o->getOperand(0), o->getOperand(1)})
                 .getResult(0));
        return;
    }
    // THE METHOD FIELD IS NOT A FIELD, so its store goes and the closure it
    // held loses its last use and goes in the sweep. `this` is a parameter
    // and the method is a free function; there is nothing to write.
    if (o->hasAttr("ctnative.method")) {
        eraseIfUnused(o);
        return;
    }
    if (auto get = llvm::dyn_cast<GetPropertyOp>(o)) {
        const mlir::Type type = get.getResult().getType();
        swap(ec::LoadOp::create(b, where, type,
                                memberAccess(b, where, get.getObject(), memberName(o), type)));
        return;
    }
    if (auto set = llvm::dyn_cast<SetPropertyOp>(o)) {
        ec::AssignOp::create(
            b, where,
            memberAccess(b, where, set.getObject(), memberName(o), set.getValue().getType()),
            set.getValue());
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
            swap(f64Constant(b, where, n.getDouble()));
        } else if (auto bo = llvm::dyn_cast<BooleanAttr>(k.getValue())) {
            swap(boolConstant(b, where, bo.getValue()));
        } else if (auto string = llvm::dyn_cast<StringAttr>(k.getValue());
                   string && k.getResult().getType() == carrierType(context, carrier::string)) {
            swap(stringConstant(b, where, string.getValue()));
        } else {
            // undefined, as the NaN the representation table says it is.
            swap(f64Constant(b, where, std::numeric_limits<double>::quiet_NaN()));
        }
        return;
    }
    if (auto bin = llvm::dyn_cast<BinaryOp>(o)) {
        const mlir::Value l = bin.getLhs(), r = bin.getRhs();
        switch (bin.getKind()) {
        case BinaryKind::Add:
        case BinaryKind::Concat:
            swap(ec::AddOp::create(b, where, bin.getResult().getType(), l, r));
            return;
        case BinaryKind::Sub: swap(ec::SubOp::create(b, where, f64, l, r)); return;
        case BinaryKind::Mul: swap(ec::MulOp::create(b, where, f64, l, r)); return;
        case BinaryKind::Div: swap(ec::DivOp::create(b, where, f64, l, r)); return;
        case BinaryKind::Mod: swap(libmCall(b, where, "std::fmod", {l, r})); return;
        case BinaryKind::Pow: swap(exponentiate(b, where, l, r)); return;
        default: llvm_unreachable("admission refused it");
        }
    }
    if (auto bin = llvm::dyn_cast<BinaryStaticOp>(o)) {
        swap(ec::AddOp::create(b, where, f64, bin.getLhs(), bin.getRhs()));
        return;
    }
    if (auto u = llvm::dyn_cast<UnaryOp>(o)) {
        switch (u.getKind()) {
        case UnaryKind::Neg: swap(ec::UnaryMinusOp::create(b, where, f64, u.getOperand())); return;
        // `+x` IS GONE BY NOW, ERASED BY UnaryPlusIsIdentity.pdll in
        // applyDeclarativeRules() above. This arm is not dead code and it
        // is not llvm_unreachable: PDL has NO DIAGNOSTIC ON A NON-MATCH, so
        // a pattern that silently stopped firing - a rename in CTJSOps.td,
        // a guard the constraint gets wrong, a driver that never ran - would
        // otherwise reach the default arm and abort with a message blaming
        // admission. Naming the file that owed the rewrite is the whole
        // difference between a bug report and a wild goose chase.
        case UnaryKind::Plus:
            llvm::report_fatal_error("ctnative lowering: a `ctjs.unary plus` reached replace() "
                                     "- UnaryPlusIsIdentity.pdll was supposed to have erased "
                                     "it, and PDL does not report a non-match");
        case UnaryKind::Not: {
            swap(ec::LogicalNotOp::create(b, where, i1, truthy(b, where, u.getOperand())));
            return;
        }
        default: llvm_unreachable("admission refused it");
        }
    }
    if (auto cmp = llvm::dyn_cast<CompareOp>(o)) {
        ec::CmpPredicate p = ec::CmpPredicate::eq;
        switch (cmp.getKind()) {
        case CompareKind::Lt: p = ec::CmpPredicate::lt; break;
        case CompareKind::Le: p = ec::CmpPredicate::le; break;
        case CompareKind::Gt: p = ec::CmpPredicate::gt; break;
        case CompareKind::Ge: p = ec::CmpPredicate::ge; break;
        case CompareKind::Eq:
        case CompareKind::StrictEq: p = ec::CmpPredicate::eq; break;
        }
        swap(ec::CmpOp::create(b, where, i1, p, cmp.getLhs(), cmp.getRhs()));
        return;
    }
    if (auto t = llvm::dyn_cast<TruthyOp>(o)) {
        swap(truthy(b, where, t.getValue()));
        return;
    }
    if (auto load = llvm::dyn_cast<LoadGlobalOp>(o)) {
        if (admission::feedsOnlyDirectCallees(load.getResult())) {
            // Every use is a call_direct's callee-value operand, and the
            // call is rewritten below without it; by the sweep it is dead.
            return;
        }
        swap(ec::LoadOp::create(b, where, f64, lvalueOfGlobal(b, where, load.getName())));
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
        // THE RECEIVER GOES FIRST WHEN THE LIFT MARKED THE CALL, and this
        // one line is where the tier stops dropping it. The callee takes a
        // pointer, so a literal in this frame - an `emitc.variable`, an
        // lvalue - needs its address, and a receiver being PASSED ON by
        // `this.other()` is already one.
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
        if (o->hasAttr("ctnative.receiver")) { args.push_back(asPointer(call.getReceiver())); }
        // AND THE SAME ADDRESS-OF FOR AN OBJECT ARGUMENT, through the one
        // lambda above - so the receiver and an argument cannot drift into
        // two different ways of taking one address.
        for (unsigned i = 3; i < operands.size(); ++i) {
            // AND A SHARED BINDING GOES THE SAME WAY, THROUGH THE SAME
            // LAMBDA: `&n` for the variable in this frame, and the pointer
            // unchanged when this function received one itself - which is
            // how a binding two levels out reaches the innermost closure.
            const bool byAddress = admission::isObjectArg(o, i) || admission::isCellArg(o, i);
            args.push_back(byAddress ? asPointer(operands[i]) : operands[i]);
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
        ec::AssignOp::create(b, where, lvalueOfGlobal(b, where, store.getName()), store.getValue());
        eraseIfUnused(o);
        return;
    }
    if (auto ret = llvm::dyn_cast<ReturnOp>(o)) {
        if (isEntry) {
            // main: print the globals, return 0. The convention the gate
            // reads: `name=%.17g`, one per line, sorted by name.
            llvm::SmallVector<llvm::StringRef> names(globals.keys().begin(), globals.keys().end());
            llvm::sort(names);
            for (llvm::StringRef name : names) {
                mlir::Value current =
                    ec::LoadOp::create(b, where, f64, lvalueOfGlobal(b, where, name));
                mlir::Value format = ec::LiteralOp::create(
                    b, where, ec::PointerType::get(ec::OpaqueType::get(context, "const char")),
                    b.getStringAttr(("\"" + name + "=%.17g\\n\"").str()));
                ec::CallOpaqueOp::create(b, where, mlir::TypeRange{}, b.getStringAttr("printf"),
                                         mlir::ValueRange{format, current});
            }
            mlir::Value zero = ec::ConstantOp::create(b, where, mlir::IntegerType::get(context, 32),
                                                      b.getI32IntegerAttr(0));
            ec::ReturnOp::create(b, where, zero);
        } else {
            (void)returnType;
            ec::ReturnOp::create(b, where, ret.getValue());
        }
        eraseIfUnused(o);
        return;
    }
    // scf ops stay for --convert-scf-to-emitc.
}

} // namespace ctcompile::ctnative::lowering_detail
