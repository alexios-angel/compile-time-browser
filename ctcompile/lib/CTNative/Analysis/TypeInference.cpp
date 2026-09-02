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

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "llvm/Support/raw_ostream.h"

#include <cmath>

namespace ctcompile::ctnative {

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
bool isProvedNumeric(const TypeLattice * operand) {
    const mlir::Type type = operand->getValue().getType();
    if (type == nullptr) { return false; }
    if (llvm::isa<BoolType, NumType>(type)) { return true; }
    if (auto opt = llvm::dyn_cast<OptType>(type)) {
        return llvm::isa<BottomType, NumType, BoolType>(opt.getElementType());
    }
    return false;
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

mlir::LogicalResult TypeInference::initialize(mlir::Operation * top) {
    globalStores_.clear();
    globalsAreDynamic_ = false;
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

    const mlir::Type fromOperation =
        globalKnown ? global : (numeric != nullptr ? numeric : staticResultType(op));
    for (TypeLattice * result : results) {
        const mlir::Type answer = fromOperation != nullptr ? fromOperation : BoxedType::get(c);
        propagateIfChanged(result, result->join(TypeValue{answer}));
    }
    return mlir::success();
}

} // namespace ctcompile::ctnative
