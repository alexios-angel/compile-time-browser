// Admission/Operations.cpp - native lowering implementation.
#include "Admission.h"

namespace ctcompile::ctnative::lowering_detail {

bool admission::op(mlir::Operation * o) {
    using namespace ctjs;
    if (llvm::isa<CreateObjectOp>(o) && o->hasAttr(kNativeObjectIdentity)) { return true; }
    if (!methodTableName(o).empty()) {
        if (llvm::isa<CreateObjectOp>(o)) { return true; }
        mlir::Value object;
        if (auto get = llvm::dyn_cast<GetPropertyOp>(o)) { object = get.getObject(); }
        if (auto set = llvm::dyn_cast<SetPropertyOp>(o)) {
            object = set.getObject();
            if (!llvm::isa_and_nonnull<ClosureType>(typeOf(set.getValue()))) {
                return refuse("returned method table field has no proved callable");
            }
        }
        if (object) {
            auto table = llvm::dyn_cast_or_null<MethodTableType>(typeOf(object));
            return (table && table.getSite() == methodTableName(o)) ||
                   refuse("returned method table access has no single proved schema");
        }
    }
    if (auto made = llvm::dyn_cast<CreateClosureOp>(o); made && !environmentTarget(o).empty()) {
        for (mlir::Value captured : made.getUpvalues()) {
            const auto c = carrierOf(typeOf(captured));
            if (!isScalarCarrier(c) && c != carrier::string && c != carrier::map &&
                c != carrier::objectIdentity) {
                return refuse("returned closure capture needs an owning scalar, Map or object "
                              "identity carrier; got " +
                              printed(typeOf(captured)));
            }
        }
        return true;
    }
    if (auto read = llvm::dyn_cast<LoadUpvalueOp>(o); read && o->hasAttr(kNativeEnvironmentRead)) {
        auto closure = llvm::dyn_cast_or_null<ClosureType>(typeOf(read.getClosure()));
        return (closure && closure.getTarget() == environmentTarget(o)) ||
               refuse("returned closure invocation has no single proved target");
    }
    if (auto reason = o->getAttrOfType<mlir::StringAttr>(kNativeMapReason)) {
        return refuse(reason.getValue().str());
    }
    if (isNativeMapBookkeeping(o)) { return true; }
    if (auto made = llvm::dyn_cast<ConstructOp>(o)) {
        if (o->hasAttr(kNativeMapSite)) {
            return carrierOf(typeOf(made.getResult())) == carrier::map ||
                   refuse("native Map needs supported keys and definite numeric or acyclic "
                          "Map values; inferred " +
                          printed(typeOf(made.getResult())));
        }
    }
    if (const llvm::StringRef action = nativeMapAction(o); !action.empty()) {
        if (action == "size") { return true; }
        auto call = llvm::cast<CallOp>(o);
        if (carrierOf(typeOf(call.getReceiver())) != carrier::map) {
            return refuse("native Map receiver has no supported key/value carrier");
        }
        if (action == "keys" || action == "values") {
            return (isVectorSite(call.getResult()) &&
                    carrierOf(typeOf(call.getResult())) == carrier::vector) ||
                   refuse("native Map snapshot requires confined numeric elements");
        }
        return true;
    }
    if (llvm::isa<FrameEnterOp, FrameExitOp, RootOp>(o)) { return true; }
    // THE METHOD FIELD'S STORE LOWERS TO NOTHING, with the closure in it.
    // Checked before the closed-shape arms below because the value it
    // stores is a closure, which has no carrier and would be refused as a
    // field the moment the literal is examined.
    if (o->hasAttr("ctnative.method")) { return true; }
    if (auto object = llvm::dyn_cast<CreateObjectOp>(o)) {
        if (!isClosedObject(object.getResult())) { return refuse(whyOpen(object.getResult())); }
        // EVERY ACCESS IN THE GROUP, NOT ONLY THE LITERAL'S OWN. A lifted
        // method reaches these fields through its `%arg0`, in a different
        // function, and those reads and writes are this shape's too - so
        // `this.class = 1` has to be refused HERE, at the one place that
        // checks a key, or it reaches the emitter as `self->class`.
        llvm::SmallVector<mlir::Operation *> accesses;
        for (mlir::Value alias : aliasesOf(groups, object.getResult())) {
            for (mlir::OpOperand & use : alias.getUses()) {
                if (use.getOperandNumber() == 0 &&
                    llvm::isa<GetPropertyOp, SetPropertyOp>(use.getOwner())) {
                    accesses.push_back(use.getOwner());
                }
            }
        }
        // The keys this literal is ever WRITTEN with. A read of one of
        // them is an own property; a read of anything else falls through
        // to the prototype, which is what makes an inherited name wrong.
        llvm::StringSet<> written;
        for (mlir::Operation * user : accesses) {
            if (auto set = llvm::dyn_cast<SetPropertyOp>(user)) {
                // A METHOD FIELD IS NOT A FIELD. Its store lowers to
                // nothing and it takes no space in the class, so it is not
                // a key that shadows an inherited name either.
                if (!user->hasAttr("ctnative.method")) { written.insert(keyOf(set.getKey())); }
            }
        }
        for (mlir::Operation * user : accesses) {
            if (user->hasAttr("ctnative.method")) { continue; }
            const llvm::StringRef key = llvm::isa<GetPropertyOp>(user)
                                            ? keyOf(llvm::cast<GetPropertyOp>(user).getKey())
                                            : keyOf(llvm::cast<SetPropertyOp>(user).getKey());
            if (!isCIdentifier(key)) {
                return refuse(("field `" + key + "` is not a C identifier").str());
            }
            if (isReservedInCpp(key)) {
                return refuse(("field `" + key +
                               "` is a C++ keyword or a macro of <cmath>/<cstdio>, so the "
                               "generated struct would not compile")
                                  .str());
            }
            if (!written.contains(key) && namesObjectPrototypeMember(key)) {
                return refuse(("field `" + key +
                               "` is read but never written, and Object.prototype answers "
                               "that name - the interpreter finds a function where this "
                               "would find undefined")
                                  .str());
            }
            if (auto set = llvm::dyn_cast<SetPropertyOp>(user)) {
                const carrier c = carrierOf(typeOf(set.getValue()));
                if (!isScalarCarrier(c)) {
                    return refuse(("field `" + key + "` is stored a " +
                                   printed(typeOf(set.getValue())) + ", not a number or a boolean")
                                      .str());
                }
                // The shape census joins all scalar writes before choosing
                // storage, including a field nobody reads. Mixed boolean
                // and number stores therefore retain their runtime tags.
            }
        }
        return true;
    }
    if (auto array = llvm::dyn_cast<CreateArrayOp>(o)) {
        if (!isVectorSite(array.getResult())) { return refuse(whyNotDense(array.getResult())); }
        // ONE FRAME SLOT, WHICH IS OBLIGATION O-4. A literal made inside an
        // `if` or a loop body would declare its vector inside that block
        // and the storage would end at the closing brace; the function's
        // own entry block is the only place a frame-scope declaration can
        // go.
        if (!llvm::isa<ctjs::FuncOp>(o->getParentOp()) || !o->getBlock()->isEntryBlock()) {
            return refuse("an array literal created inside a branch or a loop - its storage "
                          "has to be one frame slot (obligation O-4)");
        }
        if (carrierOf(typeOf(array.getResult())) != carrier::vector) {
            auto elements = llvm::dyn_cast_or_null<VecType>(typeOf(array.getResult()));
            const mlir::Type element = elements ? elements.getElementType() : mlir::Type{};
            const auto optional = llvm::dyn_cast_or_null<OptType>(element);
            if (carrierOf(optional ? optional.getElementType() : element) == carrier::boolean) {
                return refuse("an array of booleans - `std::vector<bool>` is a bit-packed "
                              "specialisation whose elements are a proxy, not a `bool`");
            }
            return refuse("an array whose elements are " + printed(element) + ", not numbers");
        }
        for (mlir::Value element : array.getElements()) {
            if (carrierOf(typeOf(element)) != carrier::number) {
                return refuse("dense array storage requires definite numbers");
            }
        }
        return true;
    }
    if (auto push = llvm::dyn_cast<AppendOp>(o)) {
        if (!isVectorSite(push.getArray())) {
            return refuse("an append onto an array that is not a dense literal");
        }
        return carrierOf(typeOf(push.getElement())) == carrier::number ||
               refuse("dense array storage requires definite numbers");
    }
    if (auto get = llvm::dyn_cast<GetPropertyOp>(o)) {
        if (isVectorSite(get.getObject())) {
            // `length` is `size()`, exactly, BECAUSE the site proof is what
            // rules out a hole; every other key is an index, and the index
            // has to be a number - `a[k]` with a string `k` reads a
            // property, and `a["push"]` is a function.
            if (keyOf(get.getKey()) == "length") { return true; }
            return numeric(get.getKey(), "array index");
        }
        if (!isClosedObject(get.getObject())) {
            return refuse("a property read on an object that is not a closed-shape literal");
        }
        return true; // its result's carrier is checked with every other value
    }
    if (auto set = llvm::dyn_cast<SetPropertyOp>(o)) {
        // An array literal written through is not a vector site at all, so
        // the site's own diagnostic names the sparsity route rather than
        // this one naming a closed shape the program never asked for.
        if (set.getObject().getDefiningOp<CreateArrayOp>()) {
            return refuse(whyNotDense(set.getObject()));
        }
        if (!isClosedObject(set.getObject())) {
            return refuse("a property write on an object that is not a closed-shape literal");
        }
        return true; // the value's carrier was checked at the object
    }
    if (isKeyOnlyString(o) || isVectorKeyString(o)) { return true; }
    if (auto load = llvm::dyn_cast<LoadGlobalOp>(o);
        load && feedsOnlyDirectCallees(load.getResult())) {
        return true;
    }
    if (auto call = llvm::dyn_cast<CallDirectOp>(o)) {
        if (o->hasAttr(kNativeStoredCall)) {
            auto callable = llvm::dyn_cast_or_null<ClosureType>(typeOf(call.getCalleeValue()));
            if (!callable || callable.getTarget() != call.getCallee()) {
                return refuse("stored callable invocation has no single proved target");
            }
        }
        // new.target and the callee value are dropped; each remaining
        // argument needs a proved carrier. Whether the CALLEE is native is the fixpoint in
        // runOnOperation, not a question for one function.
        //
        // THE RECEIVER IS NOT DROPPED WHEN THE LIFT MARKED THIS CALL. It
        // is operand 0, it becomes the callee's first C++ parameter, and
        // its carrier is the generated class - so what has to hold here is
        // that it really is a closed-shape object and not some other
        // value the lift never looked at.
        const auto operands = call.getArgOperands();
        if (o->hasAttr("ctnative.receiver") && !isClosedObject(call.getReceiver())) {
            return refuse("a method call whose receiver is not a closed-shape object");
        }
        for (unsigned i = 3; i < operands.size(); ++i) {
            // AN OBJECT ARGUMENT IS NOT A NUMBER AND MUST NOT BE ASKED TO
            // BE ONE. The lift proved the literal closed before it wrote
            // the index; this asks the same question of the IR that came
            // out, exactly as the receiver arm above does, because a
            // refusal since then can have opened it.
            if (isObjectArg(o, i)) {
                if (!isClosedObject(operands[i])) {
                    return refuse("it passes an object whose shape is no longer closed to a "
                                  "parameter this tier gave a pointer");
                }
                continue;
            }
            // A SHARED BINDING IS NOT A NUMBER EITHER: what is passed is
            // the ADDRESS of a variable in this frame, so what has to hold
            // is that the variable is still one this tier can spell and
            // that the operand still names it. A carrier of `none` is
            // refused at the box itself; this catches the operand that
            // stopped being a box at all, which would be the lift and this
            // check disagreeing.
            if (isCellArg(o, i)) {
                if (!namesASharedCell(operands[i])) {
                    return refuse("it passes something that is not a shared binding of this "
                                  "frame to a parameter this tier gave a pointer");
                }
                if (carrierOf(typeOf(operands[i])) == carrier::none) {
                    return refuse("it passes a shared binding of type " +
                                  printed(typeOf(operands[i])) + ", which has no native carrier");
                }
                continue;
            }
            if (carrierOf(typeOf(operands[i])) != carrier::methodTable &&
                carrierOf(typeOf(operands[i])) != carrier::objectIdentity &&
                carrierOf(typeOf(operands[i])) != carrier::closure &&
                carrierOf(typeOf(operands[i])) != carrier::boolean &&
                carrierOf(typeOf(operands[i])) != carrier::string &&
                !(carrierOf(typeOf(operands[i])) == carrier::map &&
                  nativeMapGroup(operands[i]) >= 0) &&
                !numeric(operands[i], "argument")) {
                return false;
            }
        }
        return true;
    }
    if (isDeclarationClosure(o) || isDeclarationStore(o)) { return true; }
    // PHASE 59 SLICE 1. A lifted closure and the constant cell it captured
    // are both gone by the time the emitter sees anything; a closure that
    // could NOT be lifted carries the reason the lift wrote onto it, which
    // is what turns "`ctjs.create_closure` is not native yet" - a name for
    // a whole phase - into a work item.
    if (isLiftedClosure(o) || isUnboxedCell(o)) { return true; }
    if (llvm::isa<CreateClosureOp>(o)) { return refuse(closureRefusal(o)); }
    // PHASE 59 SLICE 2 STEP 2: THE SHARED BINDING'S BOX IS A VARIABLE IN
    // THIS FRAME, and this is where its carrier is proved. The lift ran
    // before the solve and could not ask; nothing else between here and
    // the emitter does. A cell of a type with no C++ representation is
    // refused HERE, so no pointer to one is ever taken - which is the
    // second of the three conditions the design rests on, and the only one
    // that cannot be asked at the lift.
    if (auto cell = llvm::dyn_cast<CreateCellOp>(o); cell && isCarriedCell(o)) {
        if (carrierOf(typeOf(cell.getResult())) == carrier::none) {
            return refuse("a shared binding of type " + printed(typeOf(cell.getResult())) +
                          ", which has no native carrier - a variable this tier cannot "
                          "spell is not one it may point at");
        }
        if (carrierOf(typeOf(cell.getResult())) == carrier::string &&
            carrierOf(typeOf(cell.getInitial())) != carrier::string &&
            !cell->hasAttr(kAssignedBeforeRead)) {
            return refuse("a shared string binding whose non-string initial is observable");
        }
        return true;
    }
    // A READ AND A WRITE OF ONE, in this frame or through the pointer a
    // lifted call handed us. The read's result carrier is asked by the
    // per-result walk in function(); what is asked here is that the write
    // stores the carrier the variable holds - two carriers in one variable
    // is a `double` assigned a `bool`, and the join that produced the
    // cell's type must select tagged storage for mixed scalar assignments.
    // A refusal here names the store rather than the box.
    if (auto get = llvm::dyn_cast<CellGetOp>(o); get && namesASharedCell(get.getCell())) {
        return true;
    }
    if (auto set = llvm::dyn_cast<CellSetOp>(o); set && namesASharedCell(set.getCell())) {
        const carrier held = carrierOf(typeOf(set.getCell()));
        const carrier stored = carrierOf(typeOf(set.getValue()));
        if (held == carrier::none ||
            (stored != held && !(held == carrier::nullable && isScalarCarrier(stored)))) {
            return refuse("an assignment of " + printed(typeOf(set.getValue())) +
                          " to a shared binding of type " + printed(typeOf(set.getCell())));
        }
        return true;
    }
    if (llvm::isa<CreateCellOp>(o)) {
        auto why = o->getAttrOfType<mlir::StringAttr>("ctnative.cell_reason");
        return refuse(why ? ("a captured binding that stays a cell: " + why.getValue()).str()
                          : std::string{"a captured binding that stays a cell - Phase 59"});
    }
    // THE LIFT'S UNDEFINED VALUE for a block argument no predecessor sets:
    // never read on any executed path. Lowering chooses NaN or an empty
    // string per destination slot, so poison itself needs no carrier.
    if (o->getName().getStringRef() == "ub.poison") { return true; }
    // THE STRUCTURING PASS'S MULTIPLEXERS: --ctjs-lift-to-scf encodes which
    // edge a merged block came from as i32 flags, in arith. They carry no
    // JavaScript value and --convert-arith-to-emitc lowers them.
    if (o->getDialect() != nullptr && o->getDialect()->getNamespace() == "arith") {
        for (mlir::Value v : o->getOperands()) {
            if (llvm::isa<ctjs::ValueType>(v.getType())) {
                return refuse("an arith op on a JavaScript value");
            }
        }
        return true;
    }
    if (auto k = llvm::dyn_cast<ConstantOp>(o)) {
        if (llvm::isa<NumberAttr, BooleanAttr, UndefinedAttr, NullAttr>(k.getValue())) {
            return true;
        }
        if (llvm::isa<StringAttr>(k.getValue()) &&
            carrierOf(typeOf(k.getResult())) == carrier::string) {
            return true;
        }
        return refuse("a constant that is not a number, a boolean or undefined");
    }
    if (auto b = llvm::dyn_cast<BinaryOp>(o)) {
        switch (b.getKind()) {
        case BinaryKind::Add:
            if (strings(b.getLhs(), b.getRhs())) { return true; }
            return numeric(b.getLhs(), "binary") && numeric(b.getRhs(), "binary");
        case BinaryKind::Concat:
            return strings(b.getLhs(), b.getRhs()) ||
                   refuse("concatenation requires two proved owning UTF-8 strings");
        case BinaryKind::Sub:
        case BinaryKind::Mul:
        case BinaryKind::Div:
        case BinaryKind::Mod:
        case BinaryKind::Pow: return numeric(b.getLhs(), "binary") && numeric(b.getRhs(), "binary");
        // (`**` is not std::pow; exponentiate() below is why.)
        default: return refuse("a bitwise or string operator is not native yet");
        }
    }
    if (auto b = llvm::dyn_cast<BinaryStaticOp>(o)) {
        if (b.getKind() != BinaryKind::Add) {
            return refuse("a static bitwise operator is not native yet");
        }
        return numeric(b.getLhs(), "++") && numeric(b.getRhs(), "++");
    }
    if (auto u = llvm::dyn_cast<UnaryOp>(o)) {
        switch (u.getKind()) {
        case UnaryKind::Neg:
        case UnaryKind::Plus: return numeric(u.getOperand(), "unary");
        case UnaryKind::TypeOf:
            return isScalarCarrier(carrierOf(typeOf(u.getOperand()))) ||
                   carrierOf(typeOf(u.getOperand())) == carrier::string ||
                   refuse("typeof requires a scalar or owning string carrier");
        case UnaryKind::Not:
            // `!x` applies the carrier's exact truthiness conversion, then
            // negates it. Tagged null/undefined and numeric NaN are falsy.
            if (carrierOf(typeOf(u.getOperand())) == carrier::none) {
                return refuse("! of " + printed(typeOf(u.getOperand())));
            }
            return true;
        default: return refuse("void and ~ are not native yet");
        }
    }
    if (auto cmp = llvm::dyn_cast<CompareOp>(o)) {
        switch (cmp.getKind()) {
        case CompareKind::Lt:
        case CompareKind::Le:
        case CompareKind::Gt:
        case CompareKind::Ge:
            return numeric(cmp.getLhs(), "compare") && numeric(cmp.getRhs(), "compare");
        case CompareKind::Eq:
        case CompareKind::StrictEq:
            if (strings(cmp.getLhs(), cmp.getRhs())) { return true; }
            return numeric(cmp.getLhs(), "equality") && numeric(cmp.getRhs(), "equality");
        }
        return refuse("an unknown comparison");
    }
    if (auto t = llvm::dyn_cast<TruthyOp>(o)) {
        const carrier c = carrierOf(typeOf(t.getValue()));
        if (c == carrier::none) { return refuse("truthiness of " + printed(typeOf(t.getValue()))); }
        return true;
    }
    if (auto load = llvm::dyn_cast<LoadGlobalOp>(o)) {
        if (carrierOf(typeOf(load.getResult())) != carrier::number &&
            carrierOf(typeOf(load.getResult())) != carrier::nullable) {
            return refuse(("global `" + load.getName() + "` is " +
                           printed(typeOf(load.getResult())) + ", not a number")
                              .str());
        }
        return true;
    }
    if (auto store = llvm::dyn_cast<StoreGlobalOp>(o)) {
        // Tagged storage preserves an early global read as undefined, but
        // the standalone observation convention still prints only numbers.
        // A possibly absent store must therefore refuse. Narrowing globals
        // needs the closed world's complete store set: a local dominating
        // write alone cannot rule out mutations by a callee.
        const std::string where = ("store to global `" + store.getName() + "`").str();
        return (carrierOf(typeOf(store.getValue())) == carrier::number ||
                carrierOf(typeOf(store.getValue())) == carrier::nullable ||
                refuse(where + " requires a numeric global")) &&
               printable(store.getValue(), where);
    }
    if (auto ret = llvm::dyn_cast<ReturnOp>(o)) {
        const carrier c = carrierOf(typeOf(ret.getValue()));
        if (c == carrier::none) { return refuse("returns " + printed(typeOf(ret.getValue()))); }
        if (returns == carrier::none) { returns = c; }
        if (returns != c) {
            if (isScalarCarrier(returns) && isScalarCarrier(c)) {
                returns = carrier::nullable;
            } else {
                return refuse("returns different native carriers on different paths");
            }
        }
        return true;
    }
    if (llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp, mlir::scf::ForOp, mlir::scf::ConditionOp,
                  mlir::scf::YieldOp>(o)) {
        return true;
    }
    if (llvm::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp, mlir::cf::SwitchOp>(o)) {
        return refuse("unstructured control flow - run --ctjs-lift-to-scf first");
    }
    return refuse(("`" + o->getName().getStringRef() + "` is not native yet").str());
}

} // namespace ctcompile::ctnative::lowering_detail
