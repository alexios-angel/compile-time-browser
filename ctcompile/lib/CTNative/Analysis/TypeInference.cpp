// THE TRANSFER FUNCTION - what JavaScript guarantees about each operation's
// result, and nothing more than that.
//
// Everything structural is upstream's: the worklist, the fixpoint, the join at
// block arguments and the interaction with dead code all belong to MLIR's
// DataFlowFramework. What is ours is the table below, and the table is where
// every soundness bug will live, so each row that claims anything narrower than
// `boxed` says WHY, and the ones that were verified against the interpreter say
// where.
//
// BIGINT IS THE REASON THIS FILE IS OPERAND-SENSITIVE AT ALL, and it is worth
// stating up front because the obvious version of this analysis is WRONG.
// `a | b` looks like it must be an int32 - ECMAScript's ToInt32 says so - but
// `1n | 2n` is `3n`, a BigInt. context::binary_op consults its BigInt arm
// BEFORE any numeric conversion, and so does context::binary_op_static despite
// its name and despite ctjs.binary_static's own description saying it "uses
// to_number and to_int32": that description is about RE-ENTRANCY - no user
// valueOf runs - and not about BigInt. Measured in
// ctbrowser/lib/Script/vm/coerce.cpp: `binary_op_static` opens with
// `if (value made; bigint_binary(kind, lhs, rhs, made)) { return made; }`.
//
// So a numeric claim needs a proof that neither operand is a BigInt, and
// couldBeBigInt below is that proof. ctnative has no BigInt type, which means
// `boxed` and `json` may be one and the burden falls where it should: on the
// analysis, not on the lattice.
//
// THE ONE OPERATOR THAT ESCAPES THIS is `>>>`. A BigInt has no width, so the
// specification gives it no unsigned right shift and the VM throws a TypeError
// rather than producing one. `>>>` therefore yields a Number unconditionally -
// and a `num<f64>` rather than a `num<i32>`, because the result is a uint32 and
// `(-1) >>> 0` is 4294967295, which does not fit an int32. That pair of facts
// is the file in miniature.
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "Inference/PropertyKey.h"
#include "OwnedGlobalRoots.h"
#include "ctcompile/CTNative/Analysis/NativeClosure.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "ctcompile/CTNative/Analysis/NativeObjectIdentity.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/Support/raw_ostream.h"

#include <cmath>

namespace ctcompile::ctnative {
using inference_detail::constantKey;

void TypeValue::print(llvm::raw_ostream & os) const {
    if (type_ == nullptr) {
        os << "<uninitialized>";
        return;
    }
    os << type_;
}

namespace {

// --- the small constructors, so the table below reads as JavaScript ---------

mlir::Type boolType(mlir::MLIRContext * c) {
    return BoolType::get(c);
}
mlir::Type doubleType(mlir::MLIRContext * c) {
    return NumType::get(c, NumKind::F64);
}
mlir::Type int32Type(mlir::MLIRContext * c) {
    return NumType::get(c, NumKind::I32);
}
mlir::Type stringType(mlir::MLIRContext * c) {
    return defaultStringType(c);
}

// `undefined` AND `null` ARE THE SAME TYPE HERE, which CTNative_OptType's own
// description admits is a real narrowing: `typeof` tells them apart and
// `null === undefined` is false. An empty optional is what part 24 chose to
// represent both, so the obligation to check that the difference is never
// observed belongs to the phase that USES this - it is recorded in
// ctcompile/docs/native-divergences.md - and an inference that reported them
// as distinct types would be inventing a distinction the lattice cannot carry.
mlir::Type absentType(mlir::MLIRContext * c) {
    return OptType::get(c, BottomType::get(c));
}

// --- the BigInt proof -------------------------------------------------------

// Could a value of this type be a BigInt?
//
// CONSERVATIVE BY CONSTRUCTION: an unknown type must answer true, because the
// only use of a false answer is to license a numeric claim. A null Type is
// "nothing known yet" and answers true for the same reason.
bool couldBeBigInt(mlir::Type type) {
    if (type == nullptr) { return true; }
    if (llvm::isa<BoolType, NumType, StrType, StrViewType, BottomType>(type)) { return false; }
    if (auto opt = llvm::dyn_cast<OptType>(type)) { return couldBeBigInt(opt.getElementType()); }
    if (auto variant = llvm::dyn_cast<VariantType>(type)) {
        for (mlir::Type alternative : variant.getAlternatives()) {
            if (couldBeBigInt(alternative)) { return true; }
        }
        return false;
    }
    // boxed, json, and every container - a container cannot itself BE a BigInt,
    // but nothing in this phase produces one, so saying so would be a rule
    // written for no caller.
    return true;
}

// A proved string: `str` or `strview`, and not an optional of one - an
// undefined-or-string does not concatenate the way a string does.
bool isProvedString(const TypeLattice * operand) {
    return llvm::isa_and_nonnull<StrType, StrViewType>(operand->getValue().getType());
}

// A value on which `+` is numeric addition: a number, a boolean, undefined
// or null - the types ToPrimitive leaves alone and ToNumber accepts.
bool isProvedNumericType(mlir::Type type) {
    if (type == nullptr) { return false; }
    if (llvm::isa<BoolType, NumType>(type)) { return true; }
    if (auto opt = llvm::dyn_cast<OptType>(type)) {
        return llvm::isa<BottomType>(opt.getElementType()) ||
               isProvedNumericType(opt.getElementType());
    }
    if (auto variant = llvm::dyn_cast<VariantType>(type)) {
        return llvm::all_of(variant.getAlternatives(), isProvedNumericType);
    }
    return false;
}

bool isProvedNumeric(const TypeLattice * operand) {
    return isProvedNumericType(operand->getValue().getType());
}

bool noneAreBigInt(llvm::ArrayRef<const TypeLattice *> operands) {
    for (const TypeLattice * operand : operands) {
        if (couldBeBigInt(operand->getValue().getType())) { return false; }
    }
    return true;
}

// --- constants --------------------------------------------------------------

// The type of a literal, which is the only place an i32 comes from without a
// bound proof - because the bound is right there in the attribute.
//
// THREE WAYS A DOUBLE FAILS TO BE AN i32 and all three are the oracle's
// business: a fractional part, a magnitude outside int32, and NEGATIVE ZERO.
// The last is the one that gets forgotten: `-0` is integral and in range, and
// `Object.is(-0, 0)` is false, so calling it an int32 loses an observable
// difference. tools/check/type-oracle.py counts NUM_NEGATIVE_ZERO as
// not-an-i32 for exactly this reason, and the two must agree.
mlir::Type typeOfConstant(ctjs::ConstantOp constant) {
    mlir::MLIRContext * c = constant.getContext();
    mlir::Attribute value = constant.getValue();

    if (llvm::isa<ctjs::UndefinedAttr, ctjs::NullAttr>(value)) { return absentType(c); }
    if (llvm::isa<ctjs::BooleanAttr>(value)) { return boolType(c); }
    if (llvm::isa<ctjs::StringAttr>(value)) { return stringType(c); }
    if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(value)) {
        const double d = number.getDouble();
        const bool negativeZero = d == 0.0 && std::signbit(d);
        const bool representable = std::isfinite(d) && !negativeZero && d == std::trunc(d) &&
                                   d >= -2147483648.0 && d <= 2147483647.0;
        return representable ? int32Type(c) : doubleType(c);
    }
    // A BigInt literal. ctnative has no type for one, so this is the honest
    // answer rather than a wrong one.
    return {};
}

} // namespace

// --- the operation-only half of the table -----------------------------------

mlir::Type staticResultType(mlir::Operation * op) {
    mlir::MLIRContext * c = op->getContext();

    // `ub.poison` IS THE ABSENCE OF A VALUE, and the lattice's identity is
    // the only honest type for it. The structuring pass yields one for every
    // loop-carried value on the path that leaves the loop - a path on which
    // nothing reads them - and a poison typed `boxed` would absorb the
    // counter's type at the join, refusing every `for` loop the lift builds.
    // Matched by name so this file needs no dependency on the ub dialect.
    if (op->getName().getStringRef() == "ub.poison") { return BottomType::get(c); }

    if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(op)) { return typeOfConstant(constant); }

    // EVERY COMPARISON IS A BOOLEAN, including the relational ones on BigInts:
    // `1n < 2` is a perfectly good comparison and its answer is still a bool.
    if (llvm::isa<ctjs::CompareOp, ctjs::InstanceOfOp, ctjs::HasPropertyOp, ctjs::DeletePropertyOp,
                  ctjs::DeleteNamedOp, ctjs::FromBoolOp>(op)) {
        return boolType(c);
    }

    if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op)) {
        switch (unary.getKind()) {
        // `!x` is a boolean for every x there is.
        case ctjs::UnaryKind::Not: return boolType(c);
        // `typeof x` is one of a fixed set of strings.
        case ctjs::UnaryKind::TypeOf: return stringType(c);
        // `void x` evaluates x and yields undefined.
        case ctjs::UnaryKind::Void: return absentType(c);
        // UNARY PLUS IS ToNumber, WHICH THROWS ON A BIGINT rather than
        // returning one - context::to_number, coerce.cpp: "Cannot convert a
        // BigInt value to a number". So if it produces a value at all, that
        // value is a Number. No operand proof needed.
        case ctjs::UnaryKind::Plus: return doubleType(c);
        // AND NEGATION IS NOT, because `-1n` is `-1n`. Operand-sensitive; see
        // visitOperation.
        case ctjs::UnaryKind::Neg:
        case ctjs::UnaryKind::BitNot: return {};
        }
        return {};
    }

    if (auto convert = llvm::dyn_cast<ctjs::ConvertOp>(op)) {
        switch (convert.getKind()) {
        case ctjs::ConvertKind::ToBoolean: return boolType(c);
        // As above: ToNumber throws on a BigInt rather than yielding one.
        case ctjs::ConvertKind::ToNumber: return doubleType(c);
        case ctjs::ConvertKind::ToString: return stringType(c);
        // ToPropertyKey is a string OR A SYMBOL, ToObject is any object, and
        // ToPrimitive is anything that is not one. None is expressible.
        case ctjs::ConvertKind::ToPropertyKey:
        case ctjs::ConvertKind::ToObject:
        case ctjs::ConvertKind::ToPrimitive: return {};
        }
        return {};
    }

    if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(op)) {
        // CONCAT NEVER CONSULTS THE BIGINT ARM - coerce.cpp says so in as many
        // words - so it is a string whatever reaches it. It is the only
        // unconditional claim in the binary family.
        if (binary.getKind() == ctjs::BinaryKind::Concat) { return stringType(c); }
        // `>>>` has no BigInt meaning: the VM throws a TypeError. A uint32, so
        // f64 and NOT i32.
        if (binary.getKind() == ctjs::BinaryKind::UShr) { return doubleType(c); }
        return {};
    }

    if (auto binary = llvm::dyn_cast<ctjs::BinaryStaticOp>(op)) {
        if (binary.getKind() == ctjs::BinaryKind::UShr) { return doubleType(c); }
        return {};
    }

    // A `ctjs.truthy` result is an i1 and not a JavaScript value at all - it
    // never occupies a register, so naming a JavaScript type for it would be a
    // category error rather than a precision win.
    return {};
}

// --- the analysis ------------------------------------------------------------

mlir::Type TypeInference::elementTypeOf(mlir::Operation * op, mlir::Value array) {
    // FROM UNDEFINED, and that start is not decoration: `[1, 2][7]` is
    // `undefined`, and so is every index no `append` ever wrote. Starting from
    // `num` would claim a number for a read the interpreter answers
    // `undefined` for, which is the one direction the lattice cannot undo.
    mlir::Type element = absentType(op->getContext());
    if (auto call = array.getDefiningOp<ctjs::CallOp>(); call && isNativeMapSnapshot(call)) {
        const TypeLattice * lattice = getLatticeElementFor(getProgramPointAfter(op), array);
        if (auto vector = llvm::dyn_cast_or_null<VecType>(lattice->getValue().getType())) {
            return meet(element, vector.getElementType());
        }
        return element;
    }
    const auto appended = appends_.find(array);
    if (appended == appends_.end()) { return element; }
    for (mlir::Value value : appended->second) {
        const TypeLattice * lattice = getLatticeElementFor(getProgramPointAfter(op), value);
        // Not yet visited contributes nothing NOW and re-visits this read when
        // it is; never visited is dead code and never executes.
        if (lattice->getValue().isUninitialized()) { continue; }
        element = meet(element, lattice->getValue().getType());
    }
    return element;
}

// PHASE 59 SLICE 2 STEP 3, THE FIELD HALF. The three conditions and the reason
// for each are in TypeInference.h; this is the query.
bool TypeInference::fieldIsAssignedBefore(mlir::Value object, llvm::StringRef key,
                                          mlir::Operation * read) {
    const auto sites = fieldStoreSites_.find({object, key});
    if (sites == fieldStoreSites_.end()) { return false; }
    auto * owner = read->getParentOfType<ctjs::FuncOp>().getOperation();
    for (mlir::Operation * store : sites->second) {
        // THE SAME FUNCTION FIRST, THEN DOMINANCE. Asked in this order because
        // the second question is meaningless without the first: two operations
        // in different `ctjs.func`s sit in `builtin.module`'s body, which is a
        // GRAPH region, and MLIR answers "dominates" for every pair in one.
        // The closure lift's `byValueMissesACall` compares the two functions
        // for exactly this reason and says so in the same words.
        if (store->getParentOfType<ctjs::FuncOp>().getOperation() != owner) { continue; }
        if (dominance_.properlyDominates(store, read)) { return true; }
    }
    return false;
}

mlir::Type TypeInference::cellTypeOf(mlir::Operation * op, mlir::Value cell) {
    // FROM THE INITIAL, and the start is the whole soundness of the rule: a
    // read of a shared binding on a path that reached no assignment yields
    // what the box was built with. `compiler_impl::predeclare_locals` boxes
    // `undefined`, so that is normally `opt<>` and the join is `opt<num>` -
    // a double whose undefined is NaN, exact in arithmetic, comparison and
    // truthiness, refused at equality by `defined()`. Step 1 needed two
    // dominance proofs to make the initial unobservable; step 2 needs none,
    // because it does not replace the box - it emits it.
    //
    // UNLESS THERE IS NO SUCH PATH - PHASE 59 SLICE 2 STEP 3. When the lift
    // proved a write dominates every read of the binding it wrote
    // `kAssignedBeforeRead` onto the cell and `initialize()` left the initial
    // out of `cellStores_`, so this join is over the WRITES alone and a hoisted
    // `var` that is assigned before it is read is `num` rather than `opt<num>`.
    // The set is the input; nothing here has a second opinion about it.
    mlir::Type held{};
    const auto stored = cellStores_.find(cell);
    if (stored == cellStores_.end()) { return BoxedType::get(op->getContext()); }
    for (mlir::Value value : stored->second) {
        const TypeLattice * lattice = getLatticeElementFor(getProgramPointAfter(op), value);
        // Not yet visited contributes nothing NOW and re-visits this when it
        // is; never visited is dead code and never executes. `n = n + 1` is
        // exactly the cycle that needs it - the stored value reads the box.
        if (lattice->getValue().isUninitialized()) { continue; }
        held = held == nullptr ? lattice->getValue().getType()
                               : meet(held, lattice->getValue().getType());
    }
    return held;
}

mlir::Type TypeInference::mapTypeOf(mlir::Operation * op, mlir::Value map) {
    const auto joined = [&](const auto & index) {
        mlir::Type type = BottomType::get(op->getContext());
        const auto found = index.find(nativeMapGroup(map));
        if (found != index.end()) {
            for (mlir::Value value : found->second) {
                const TypeLattice * lattice = getLatticeElementFor(getProgramPointAfter(op), value);
                if (!lattice->getValue().isUninitialized()) {
                    type = meet(type, lattice->getValue().getType());
                }
            }
        }
        return type;
    };
    return MapType::get(op->getContext(), joined(mapKeys_), joined(mapValues_));
}

mlir::LogicalResult TypeInference::initialize(mlir::Operation * top) {
    globalStores_.clear();
    globalsAreDynamic_ = false;
    fieldStores_.clear();
    identityFieldStores_.clear();
    fieldStoreSites_.clear();
    appends_.clear();
    cellStores_.clear();
    mapKeys_.clear();
    mapValues_.clear();
    environmentCaptures_.clear();
    top->walk([&](ctjs::CreateClosureOp made) {
        const auto target = environmentTarget(made);
        if (!target.empty()) {
            environmentCaptures_[target].assign(made.getUpvalues().begin(),
                                                made.getUpvalues().end());
        }
    });
    top->walk([&](ctjs::CallOp call) {
        const llvm::StringRef action = nativeMapAction(call);
        if (action.empty()) { return; }
        const int64_t group = nativeMapGroup(call.getReceiver());
        if (group < 0) { return; }
        if (action == "set" || action == "get" || action == "has" || action == "delete") {
            mapKeys_[group].push_back(call.getArgs()[0]);
        }
        if (action == "set") { mapValues_[group].push_back(call.getArgs()[1]); }
    });
    // THE FIELD INDEX IS OVER THE GROUP, NOT OVER ONE VALUE, and that is the
    // whole of what a receiver parameter costs this analysis. `this.x = 5`
    // inside a lifted method is a store the CALLER's `o.x` has to see, and
    // `this.x` inside it is a read of the store the caller made - two values,
    // `%arg0` and the literal, naming one object. Every member of a group gets
    // every store made through any of them; a literal no method is lifted onto
    // is a group of one, which is the row this file had before.
    const auto groups = groupReceivers(top);
    top->walk([&](ctjs::SetPropertyOp store) {
        const int64_t identityGroup = nativeObjectFieldGroup(store);
        if (identityGroup >= 0) {
            identityFieldStores_[{identityGroup, constantKey(store.getKey())}].push_back(
                store.getValue());
            return;
        }
        if (!hasClosedShape(store.getObject())) { return; }
        const llvm::StringRef key = constantKey(store.getKey());
        // PHASE 59 SLICE 2 STEP 3, THE FIELD HALF: the store SITE, on its own
        // object and not on the group's. Beside the value index rather than in
        // a second walk, so the two cannot disagree about which stores exist -
        // they differ only in what they are keyed on, and the header says why.
        fieldStoreSites_[{store.getObject(), key}].push_back(store.getOperation());
        const auto group = groups.find(store.getObject());
        if (group == groups.end()) {
            fieldStores_[{store.getObject(), key}].push_back(store.getValue());
            return;
        }
        for (mlir::Value member : group->second) {
            fieldStores_[{member, key}].push_back(store.getValue());
        }
    });
    // THE APPENDS INDEX, beside fieldStores_ and for the same reason: an
    // element read has to find every value the array was ever built from, and
    // walking the uses at each read would be the same walk done once per read.
    // A literal's own inline elements come first - the importer emits an empty
    // `create_array` and one `append` per element, but the operation carries
    // them and a lowering that ignored them would drop values.
    top->walk([&](ctjs::CreateArrayOp array) {
        if (!isDenseVectorSite(array.getResult())) { return; }
        llvm::SmallVector<mlir::Value, 4> & into = appends_[array.getResult()];
        for (mlir::Value element : array.getElements()) { into.push_back(element); }
    });
    top->walk([&](ctjs::AppendOp push) {
        if (!isDenseVectorSite(push.getArray())) { return; }
        appends_[push.getArray()].push_back(push.getElement());
    });
    // THE SHARED-BINDING INDEX, over the group and not over one value - the
    // reason the field index is, one operand along. A `ctjs.cell_set` through
    // a capture pointer is in a DIFFERENT ctjs.func from the box, and the
    // owning frame's read has to see it: `var n = 0; function tick() { n = n +
    // 1; }` writes a double from inside `tick` into a box the frame built
    // holding `undefined`, and a rule that indexed only the frame's own store
    // would report `opt<i32>` for a binding that holds a double.
    const auto cells = groupCells(top);
    top->walk([&](ctjs::CreateCellOp cell) {
        if (!cell->hasAttr("ctnative.carried")) { return; }
        // PHASE 59 SLICE 2 STEP 3: AND THE INITIAL IS LEFT OUT WHEN NO READ CAN
        // SEE IT. The lift proved one `ctjs.cell_set` of this box properly
        // dominates every `ctjs.cell_get` of it AND every call of every closure
        // that captured it, so from that store onwards the binding holds a
        // stored value on every path that reaches any read - and the hoisted
        // `undefined` the box was built with is unreachable. Dropping it here
        // rather than in `cellTypeOf` keeps the join in one place: the type is
        // still "everything this binding can hold", over a set the lift made
        // one element smaller. `kAssignedBeforeRead` says what the lift proved.
        if (cell->hasAttr(kAssignedBeforeRead)) { return; }
        const auto group = cells.find(cell.getResult());
        if (group == cells.end()) {
            cellStores_[cell.getResult()].push_back(cell.getInitial());
            return;
        }
        for (mlir::Value member : group->second) {
            cellStores_[member].push_back(cell.getInitial());
        }
    });
    top->walk([&](ctjs::CellSetOp store) {
        if (!namesACarriedCell(store.getCell())) { return; }
        const auto group = cells.find(store.getCell());
        if (group == cells.end()) {
            cellStores_[store.getCell()].push_back(store.getValue());
            return;
        }
        for (mlir::Value member : group->second) {
            cellStores_[member].push_back(store.getValue());
        }
    });
    top->walk([&](mlir::Operation * op) {
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(op)) {
            globalStores_[store.getName()].push_back(store.getValue());
            return;
        }
        // A PROPERTY WRITE THROUGH THE GLOBAL OBJECT is a write to the globals
        // table this index cannot see. `globalThis` and `window` are the two
        // names that reach it; a load of either anywhere in the program is
        // taken as "the table may be written dynamically" - coarse, and the
        // safe side of coarse.
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
            if (load.getName() == "globalThis" || load.getName() == "window") {
                globalsAreDynamic_ = true;
            }
        }
    });
    return SparseForwardDataFlowAnalysis::initialize(top);
}

void TypeInference::setToEntryState(TypeLattice * lattice) {
    propagateIfChanged(lattice,
                       lattice->join(TypeValue{BoxedType::get(lattice->getAnchor().getContext())}));
}

mlir::LogicalResult TypeInference::visitOperation(mlir::Operation * op,
                                                  llvm::ArrayRef<const TypeLattice *> operands,
                                                  llvm::ArrayRef<TypeLattice *> results) {
    mlir::MLIRContext * c = op->getContext();

    if (const auto * root = ownedRoots_ ? ownedRoots_->lookup(op) : nullptr) {
        if (llvm::isa<ctjs::CreateObjectOp, ctjs::LoadGlobalOp>(op)) {
            propagateIfChanged(
                results[0], results[0]->join(TypeValue{GlobalObjectType::get(c, root->binding)}));
            return mlir::success();
        }
        if (llvm::isa<ctjs::GetPropertyOp>(op)) {
            // This exact write precedes every read in the complete live
            // proof. Subscribe to its value; no absent or prototype arm is
            // possible, even when the receiver came from a global load.
            auto write = root->fieldInitialization;
            const auto * stored = getLatticeElementFor(getProgramPointAfter(op), write.getValue());
            if (!stored->getValue().isUninitialized()) {
                propagateIfChanged(results[0], results[0]->join(stored->getValue()));
            }
            return mlir::success();
        }
    }

    if (llvm::isa<ctjs::CreateObjectOp>(op) && op->hasAttr(kNativeObjectIdentity)) {
        propagateIfChanged(results[0], results[0]->join(TypeValue{ObjectIdentityType::get(c)}));
        return mlir::success();
    }
    if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(op);
        object && !methodTableName(op).empty()) {
        propagateIfChanged(
            results[0], results[0]->join(TypeValue{MethodTableType::get(c, methodTableName(op))}));
        return mlir::success();
    }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op); read && !methodTableName(op).empty()) {
        propagateIfChanged(results[0],
                           results[0]->join(TypeValue{ClosureType::get(c, environmentTarget(op))}));
        return mlir::success();
    }

    // The callable's nominal identity is known before any capture type. Its
    // environment reads subscribe to the original captured value; requiring
    // all capture types first would deadlock Map inference through a closure.
    if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(op);
        made && !environmentTarget(op).empty()) {
        propagateIfChanged(results[0],
                           results[0]->join(TypeValue{ClosureType::get(c, environmentTarget(op))}));
        return mlir::success();
    }
    if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op);
        read && op->hasAttr(kNativeEnvironmentRead)) {
        const auto at = environmentCaptures_.find(environmentTarget(op));
        if (at == environmentCaptures_.end() || read.getIndex() < 0 ||
            static_cast<size_t>(read.getIndex()) >= at->second.size()) {
            return op->emitError("native environment read has no proved capture");
        }
        const auto * captured = getLatticeElementFor(
            getProgramPointAfter(op), at->second[static_cast<size_t>(read.getIndex())]);
        if (!captured->getValue().isUninitialized()) {
            propagateIfChanged(results[0], results[0]->join(captured->getValue()));
        }
        return mlir::success();
    }

    // AN UNINITIALIZED OPERAND MEANS "NOT YET", NOT "UNKNOWN". The framework
    // visits an operation before every value feeding it has an answer - a
    // loop's back edge, a recursive call's result - and re-visits it when
    // they do. Answering `boxed` for an operand that has merely not arrived
    // is wrong in the one direction the lattice cannot undo: boxed absorbs,
    // so a counter incremented on a back edge, or fib's own result, would
    // stay boxed forever. Measured: every recursive function and every
    // `for` loop refused by the native lowering until this line. Bailing
    // leaves the result uninitialized, which is exactly what its consumers
    // must wait for; a value that NEVER initializes is dead code and is
    // boxed by whoever reads the final state.
    for (const TypeLattice * operand : operands) {
        if (operand->getValue().isUninitialized()) { return mlir::success(); }
    }

    mlir::Type mapAnswer;
    if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(op); made && made->hasAttr(kNativeMapSite)) {
        mapAnswer = mapTypeOf(op, made.getResult());
    } else if (op->hasAttr(kNativeMapSnapshotCopy)) {
        mapAnswer = operands[2]->getValue().getType();
    } else if (const llvm::StringRef action = nativeMapAction(op); !action.empty()) {
        if (action == "size") {
            mapAnswer = doubleType(c);
        } else if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
            auto map = llvm::dyn_cast_or_null<MapType>(operands[1]->getValue().getType());
            if (!map) { return mlir::success(); }
            if (action == "set") {
                mapAnswer = map;
            } else if (action == "has" || action == "delete") {
                mapAnswer = boolType(c);
            } else if (action == "get") {
                mapAnswer = call->hasAttr(kNativeMapPresent)
                                ? map.getValueType()
                                : meet(absentType(c), map.getValueType());
                // This literal result is independent of the family schema,
                // including unvisited writes that depend on this same read.
                // Seed it immediately: widening first cannot be undone by a
                // later iteration of the monotone solver.
                if (auto readType = call->getAttrOfType<mlir::StringAttr>(kNativeMapReadType)) {
                    const auto tag = readType.getValue();
                    if (tag == "bool") { mapAnswer = BoolType::get(c); }
                    if (tag == "number") { mapAnswer = NumType::getDouble(c); }
                    if (tag == "string") { mapAnswer = StrType::get(c, StrEncoding::UTF8); }
                }
            } else if (action == "clear") {
                mapAnswer = absentType(c);
            } else if (action == "keys" || action == "values") {
                mlir::Type element = action == "keys" ? map.getKeyType() : map.getValueType();
                mapAnswer = VecType::get(c, element);
            }
        }
    }
    if (mapAnswer) {
        for (TypeLattice * result : results) {
            propagateIfChanged(result, result->join(TypeValue{mapAnswer}));
        }
        return mlir::success();
    }

    // The operand-sensitive rows, which exist only because of BigInt. Each one
    // is "the numeric answer, IF neither operand can be a BigInt".
    mlir::Type numeric{};
    if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op)) {
        if (noneAreBigInt(operands)) {
            // `-x` is a double: negating the int32 minimum leaves int32.
            if (unary.getKind() == ctjs::UnaryKind::Neg) { numeric = doubleType(c); }
            // `~x` is ToInt32 then a bitwise complement, which stays in int32.
            if (unary.getKind() == ctjs::UnaryKind::BitNot) { numeric = int32Type(c); }
        }
    } else if (llvm::isa<ctjs::BinaryOp, ctjs::BinaryStaticOp>(op)) {
        const auto kind = llvm::isa<ctjs::BinaryOp>(op)
                              ? llvm::cast<ctjs::BinaryOp>(op).getKind()
                              : llvm::cast<ctjs::BinaryStaticOp>(op).getKind();
        // A PROVED STRING ON EITHER SIDE OF GENERIC `+` MAKES A STRING, and
        // this sits OUTSIDE the BigInt guard on purpose: `"x" + 1n` is "x1".
        // The other operand's ToPrimitive then ToString always yields a
        // string; a Symbol throws and produces no value at all.
        if (llvm::isa<ctjs::BinaryOp>(op) && kind == ctjs::BinaryKind::Add &&
            (isProvedString(operands[0]) || isProvedString(operands[1]))) {
            numeric = stringType(c);
        }
        if (numeric == nullptr && noneAreBigInt(operands)) {
            switch (kind) {
            // THE INT32 FAMILY. ECMAScript defines all five through ToInt32,
            // and to_int32 in this VM returns an int32_t, so the result is one.
            case ctjs::BinaryKind::BitAnd:
            case ctjs::BinaryKind::BitOr:
            case ctjs::BinaryKind::BitXor:
            case ctjs::BinaryKind::Shl:
            case ctjs::BinaryKind::Shr: numeric = int32Type(c); break;
            // ARITHMETIC IS A DOUBLE AND NOT AN INT32: `2**31` overflows one
            // and not the other, which is part 24 §1.1's own counterexample.
            //
            // THE GENERIC FAMILY ONLY. binary_op_static has no arm for these
            // and answers undefined; the verifier on ctjs.binary_static
            // rejects them, and this is the belt to that brace.
            case ctjs::BinaryKind::Sub:
            case ctjs::BinaryKind::Mul:
            case ctjs::BinaryKind::Div:
            case ctjs::BinaryKind::Mod:
            case ctjs::BinaryKind::Pow:
                if (llvm::isa<ctjs::BinaryOp>(op)) { numeric = doubleType(c); }
                break;
            // `+` IN THE GENERIC FAMILY concatenates when either side is a
            // string and adds otherwise, and both halves are provable from
            // the operand types: a proved string on either side makes the
            // result a string (ToPrimitive on the other side then ToString -
            // every value has one; a Symbol throws and produces nothing);
            // two operands that are each a number, a boolean, undefined or
            // null add numerically, because ToPrimitive is the identity on
            // all four and none is a string or a BigInt. Anything else - an
            // object with its own valueOf, a value nothing proved - is boxed.
            // The STATIC family reaches `+` only from `++` and its counters,
            // through to_number, so there it is a number outright.
            case ctjs::BinaryKind::Add:
                if (llvm::isa<ctjs::BinaryStaticOp>(op)) {
                    numeric = doubleType(c);
                } else if (isProvedNumeric(operands[0]) && isProvedNumeric(operands[1])) {
                    numeric = doubleType(c);
                }
                break;
            default: break;
            }
        }
    }

    // THE CLOSED-WORLD GLOBAL: see TypeInference.h. getLatticeElementFor
    // subscribes this load to every store's operand, so a store whose type
    // widens later re-visits the load.
    mlir::Type global{};
    bool globalKnown = false;
    if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
        const auto stores = globalStores_.find(load.getName());
        if (!globalsAreDynamic_ && stores != globalStores_.end() && !stores->second.empty()) {
            globalKnown = true;
            // A GLOBAL IS UNDEFINED UNTIL ITS FIRST STORE RUNS, and nothing here
            // proves a load comes after one - `function f() { return g; }` can
            // be called before `var g = 5` executes. So the join starts from
            // the absent case: a numeric global is `opt<num>`, number OR
            // undefined, and the lowering represents that as a double whose
            // undefined is NaN - right in every arithmetic and relational
            // context, refused where the difference is observable.
            global = absentType(c);
            for (mlir::Value stored : stores->second) {
                const TypeLattice * lattice =
                    getLatticeElementFor(getProgramPointAfter(op), stored);
                // A store the solver has not reached yet contributes nothing
                // now and re-visits this load when it does; a store it will
                // NEVER reach is dead code and never executes.
                if (lattice->getValue().isUninitialized()) { continue; }
                global = meet(global, lattice->getValue().getType());
            }
        }
    }

    // THE CLOSED-SHAPE FIELD READ: the join over the stores of that key to
    // that object, from undefined. Same mechanism as the global rule, with
    // getLatticeElementFor subscribing this read to every store.
    mlir::Type field{};
    bool fieldKnown = false;
    if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
        const int64_t identityGroup = nativeObjectFieldGroup(op);
        if (identityGroup >= 0) {
            fieldKnown = true;
            // A schema group can contain distinct allocations and missing
            // fields. Never infer presence from a store on another alias/site.
            field = absentType(c);
            const auto stores =
                identityFieldStores_.find({identityGroup, constantKey(get.getKey())});
            if (stores != identityFieldStores_.end()) {
                for (mlir::Value stored : stores->second) {
                    const TypeLattice * lattice =
                        getLatticeElementFor(getProgramPointAfter(op), stored);
                    if (!lattice->getValue().isUninitialized()) {
                        field = meet(field, lattice->getValue().getType());
                    }
                }
            }
        } else if (hasClosedShape(get.getObject())) {
            const llvm::StringRef key = constantKey(get.getKey());
            fieldKnown = true;
            // AND THE SEED IS DROPPED WHERE A STORE DOMINATES - PHASE 59 SLICE
            // 2 STEP 3, the field half. "Nothing orders the read after a store"
            // is true of a field in general and false of THIS read when a
            // `ctjs.set_property` of this key on this object comes before it on
            // every path. `var p = { n: 8 }; return p.n;` is then `num` rather
            // than `opt<num>`, which is what lets a `ctjs.store_global` of it
            // through: a global is printed as `%.17g` and cannot spell
            // `undefined`. The join below is unchanged and still over the whole
            // alias group - this drops what the field cannot hold, not what it
            // can.
            field = fieldIsAssignedBefore(get.getObject(), key, op) ? mlir::Type{} : absentType(c);
            const auto stores = fieldStores_.find({get.getObject(), key});
            if (stores != fieldStores_.end()) {
                for (mlir::Value stored : stores->second) {
                    const TypeLattice * lattice =
                        getLatticeElementFor(getProgramPointAfter(op), stored);
                    if (lattice->getValue().isUninitialized()) { continue; }
                    field = meet(field, lattice->getValue().getType());
                }
            }
            // AN EMPTY JOIN IS "NOT YET", NOT `boxed` - the reason spelled at
            // the carried cell below, and reachable here for the same reason:
            // with the `undefined` seed dropped, every member of this join is a
            // stored value the solver may not have visited yet. A closed shape
            // can never also be a dense array (`hasClosedShape` demands a
            // `ctjs.create_object` or an object parameter), so returning here
            // skips no other row.
            if (field == nullptr) { return mlir::success(); }
        }
    }

    // THE DENSE ARRAY (part 24 Phase 57A). The literal itself is a
    // `vec<element>`; a read of `length` is a Number; a read through an index
    // is the element type. Same mechanism as the two rules above -
    // getLatticeElementFor subscribes the read to every appended value.
    mlir::Type vector{};
    bool vectorKnown = false;
    if (auto array = llvm::dyn_cast<ctjs::CreateArrayOp>(op)) {
        if (isDenseVectorSite(array.getResult())) {
            vectorKnown = true;
            vector = VecType::get(c, elementTypeOf(op, array.getResult()));
        }
    } else if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
        if (isDenseVectorSite(get.getObject())) {
            if (constantKey(get.getKey()) == "length") {
                // NOT AN i32, AND THE BOUND IS THE REASON. An array's length
                // is a uint32, which does not fit an int32, and nothing here
                // proves this one is small. `f64` is exact for every length
                // there is.
                vectorKnown = true;
                vector = doubleType(c);
            } else if (isProvedNumeric(operands[1])) {
                // THE KEY HAS TO BE PROVED A NUMBER, and not merely "not a
                // constant string". `a[k]` with a string `k` reads a PROPERTY:
                // `a["push"]` is a function, and claiming the element type for
                // it would be unsound on any array whose only other uses are
                // appends and index reads. A boolean, undefined or null key
                // names a property nothing wrote, which is `undefined` - and
                // undefined is where the join below starts.
                vectorKnown = true;
                vector = elementTypeOf(op, get.getObject());
            }
        }
    }

    // THE SHARED BINDING (part 24 Phase 59 slice 2 step 2). The box and every
    // read of it answer the join over everything ever assigned to it, from its
    // initial - one type for one `double`, whichever of the two spellings asks.
    mlir::Type cell{};
    bool cellKnown = false;
    if (auto made = llvm::dyn_cast<ctjs::CreateCellOp>(op)) {
        if (made->hasAttr("ctnative.carried")) {
            cellKnown = true;
            cell = cellTypeOf(op, made.getResult());
            // AND AN EMPTY JOIN IS "NOT YET", NOT `boxed` - the same
            // distinction the operand loop at the top of this function draws,
            // and it became reachable with slice 2 step 3. While the initial
            // was always in the set, one member of the join was an OPERAND of
            // this op and so was guaranteed visited; a cell whose initial is
            // left out can be visited before any of its assignments is. Falling
            // through would answer `boxed`, which absorbs, and the binding
            // would never recover - `n = n + 1` is exactly the cycle, since the
            // stored value reads the box.
            if (cell == nullptr) { return mlir::success(); }
        }
    } else if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(op)) {
        // THE OPERAND'S OWN TYPE, not a second join: `operands[0]` is the box
        // in this frame or the pointer a lifted call handed us, and either
        // already carries the answer the row above computed. A second walk
        // here would be a second chance for the two to disagree.
        if (namesACarriedCell(read.getCell())) {
            cellKnown = true;
            cell = operands[0]->getValue().getType();
        }
    }

    const mlir::Type fromOperation = cellKnown     ? cell
                                     : vectorKnown ? vector
                                     : fieldKnown  ? field
                                     : globalKnown
                                         ? global
                                         : (numeric != nullptr ? numeric : staticResultType(op));
    for (TypeLattice * result : results) {
        const mlir::Type answer = fromOperation != nullptr ? fromOperation : BoxedType::get(c);
        propagateIfChanged(result, result->join(TypeValue{answer}));
    }
    return mlir::success();
}

} // namespace ctcompile::ctnative
