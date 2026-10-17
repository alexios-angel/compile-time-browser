//===- TypeInference.cpp - Type transfer rules ----------------------------===//
//
// The transfer function of the CTNative type inference: what JavaScript
// guarantees about each operation's result, and nothing more.
//
// The worklist, the fixpoint, the join at block arguments and the handling of
// dead code all come from MLIR's DataFlowFramework. This file supplies the
// per-operation rules, which is where any soundness bug would live, so every
// rule that claims something narrower than `boxed` says why.
//
// BigInt is the reason the rules look at operand types at all. `a | b` looks
// like it must be an int32 because ECMAScript's ToInt32 says so, but
// `1n | 2n` is `3n`, a BigInt. context::binary_op consults its BigInt arm
// before any numeric conversion, and so does context::binary_op_static, even
// though ctjs.binary_static's description says it "uses to_number and
// to_int32": that description is about re-entrancy (no user valueOf runs),
// not about BigInt. In ctbrowser/lib/Script/vm/coerce.cpp, `binary_op_static`
// opens with `if (value made; bigint_binary(kind, lhs, rhs, made)) { return
// made; }`.
//
// So a numeric claim needs a proof that neither operand is a BigInt, and
// couldBeBigInt below is that proof. CTNative has no BigInt type, so `boxed`
// and `json` may hold one; the burden falls on the analysis, not the lattice.
//
// The one operator exempt from this is `>>>`. A BigInt has no width, so the
// specification gives it no unsigned right shift and the VM throws a
// TypeError. `>>>` therefore always yields a Number, typed `num<f64>` rather
// than `num<i32>` because the result is a uint32: `(-1) >>> 0` is 4294967295,
// which does not fit an int32.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "OwnedGlobalRoots.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/EscapeAnalysis.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "ctcompile/CTNative/Analysis/NativeClosure.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "ctcompile/CTNative/Analysis/NativeObjectIdentity.h"
#include "ctcompile/CTNative/Analysis/StaticTables.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"
#include "ctcompile/CTNative/IR/CTNativeOps.h"

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/Support/raw_ostream.h"

#include <cmath>

namespace ctcompile::ctnative {
using ctjs::constantKey;

mlir::Type TypeInference::mapTagType(mlir::MLIRContext * c, llvm::StringRef tag) {
    if (tag == "bool") { return BoolType::get(c); }
    if (tag == "number") { return NumType::getDouble(c); }
    if (tag == "string") { return StrType::get(c, StrEncoding::UTF8); }
    if (tag == "nullable_string") { return OptType::get(c, StrType::get(c, StrEncoding::UTF8)); }
    return {};
}

void TypeValue::print(llvm::raw_ostream & os) const {
    if (type_ == nullptr) {
        os << "<uninitialized>";
        return;
    }
    os << type_;
}

namespace {

// Small type constructors, so the rules below read as JavaScript.

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

/// The type of `undefined` and of `null`, which share one type here: an empty
/// optional. CTNative_OptType's description admits this is a real narrowing
/// (`typeof` tells them apart and `null === undefined` is false), so the
/// lowering that consumes these types must prove the difference is never
/// observed; ctcompile/docs/native-divergences.md records this. Inference
/// cannot report them as distinct because the lattice has no way to express it.
mlir::Type absentType(mlir::MLIRContext * c) {
    return OptType::get(c, BottomType::get(c));
}

/// Whether a value of this type could be a BigInt.
///
/// Conservative by construction: an unknown type answers true, because the
/// only use of a false answer is to license a numeric claim. A null Type means
/// "nothing known yet" and answers true for the same reason.
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
    // Boxed, json and every container type. A container cannot be a BigInt, but
    // no rule in this file produces a container operand here, so no case is
    // written for one.
    return true;
}

/// A proved string: `str` or `strview`, and not an optional of one. An
/// undefined-or-string does not concatenate the way a string does.
bool isProvedString(const TypeLattice * operand) {
    return llvm::isa_and_nonnull<StrType, StrViewType>(operand->getValue().getType());
}

/// Whether every alternative of `type` is a primitive on which ToPrimitive
/// cannot invoke object hooks: bool, number, undefined/null, and string when
/// `allowString`. Without strings, generic `+` is necessarily numeric addition.
bool isProvedPrimitiveType(mlir::Type type, bool allowString) {
    if (type == nullptr) { return false; }
    if (llvm::isa<BoolType, NumType>(type) ||
        (allowString && llvm::isa<StrType, StrViewType>(type))) {
        return true;
    }
    if (auto opt = llvm::dyn_cast<OptType>(type)) {
        return llvm::isa<BottomType>(opt.getElementType()) ||
               isProvedPrimitiveType(opt.getElementType(), allowString);
    }
    if (auto variant = llvm::dyn_cast<VariantType>(type)) {
        return llvm::all_of(variant.getAlternatives(), [&](mlir::Type alternative) {
            return isProvedPrimitiveType(alternative, allowString);
        });
    }
    return false;
}

/// A proved non-string primitive, so generic `+` on two of them adds.
bool isProvedNumeric(const TypeLattice * operand) {
    return isProvedPrimitiveType(operand->getValue().getType(), false);
}

bool noneAreBigInt(llvm::ArrayRef<const TypeLattice *> operands) {
    for (const TypeLattice * operand : operands) {
        if (couldBeBigInt(operand->getValue().getType())) { return false; }
    }
    return true;
}

/// The type of a literal. This is the only place an i32 comes from without a
/// bound proof, because the value is right there in the attribute.
///
/// A double fails to be an i32 in exactly three ways: a fractional part, a
/// magnitude outside int32, and negative zero. The last is easy to forget:
/// `-0` is integral and in range, but `Object.is(-0, 0)` is false, so calling
/// it an int32 loses an observable difference. tools/check/type-oracle.py
/// counts NUM_NEGATIVE_ZERO as not-an-i32 for the same reason, and the two
/// must agree.
mlir::Type typeOfConstant(mlir::MLIRContext * c, mlir::Attribute value) {
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
    // A BigInt literal. CTNative has no type for one, so no type is claimed.
    return {};
}

mlir::Type typeOfConstant(ctjs::ConstantOp constant) {
    return typeOfConstant(constant.getContext(), constant.getValue());
}

} // namespace

/// The result type an operation has whatever its operands are, or a null Type
/// when the answer depends on the operands (or is not expressible).
mlir::Type staticResultType(mlir::Operation * op) {
    mlir::MLIRContext * c = op->getContext();

    // `ub.poison` is the absence of a value, and the lattice's identity
    // (bottom) is the only correct type for it. The structuring pass yields one
    // for every loop-carried value on the path that leaves the loop, a path on
    // which nothing reads them. A poison typed `boxed` would absorb the
    // counter's type at the join and refuse every `for` loop the lift builds.
    // It is matched by name so this file needs no dependency on the ub dialect.

    if (op->getName().getStringRef() == "ub.poison") { return BottomType::get(c); }

    if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(op)) { return typeOfConstant(constant); }

    // Every comparison is a boolean, including the relational ones on BigInts:
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
        // Unary plus is ToNumber, which throws on a BigInt rather than
        // returning one (context::to_number in coerce.cpp: "Cannot convert a
        // BigInt value to a number"). So if it produces a value at all, that
        // value is a Number. No operand proof is needed.
        case ctjs::UnaryKind::Plus: return doubleType(c);
        // Negation and `~` are not, because `-1n` is `-1n`. They depend on the
        // operands; see visitOperation.
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
        // ToPropertyKey is a string or a symbol, ToObject is any object, and
        // ToPrimitive is anything that is not one. None is expressible.
        case ctjs::ConvertKind::ToPropertyKey:
        case ctjs::ConvertKind::ToObject:
        case ctjs::ConvertKind::ToPrimitive: return {};
        }
        return {};
    }

    if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(op)) {
        // Concat never consults the BigInt arm (coerce.cpp says so), so it is a
        // string whatever reaches it. It is the only unconditional claim in the
        // binary family.
        if (binary.getKind() == ctjs::BinaryKind::Concat) { return stringType(c); }
        // `>>>` has no BigInt meaning: the VM throws a TypeError. The result is
        // a uint32, so f64 and not i32.
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

mlir::Type TypeInference::elementTypeOf(mlir::Operation * op, mlir::Value array) {
    // The join starts from undefined on purpose: `[1, 2][7]` is `undefined`,
    // and so is every index no `append` ever wrote. Starting from `num` would
    // claim a number for a read the interpreter answers `undefined` for, which
    // is the one direction the lattice cannot undo.
    mlir::Type element = absentType(op->getContext());
    if (auto call = array.getDefiningOp<ctjs::CallOp>(); call && isNativeMapSnapshot(call)) {
        const TypeLattice * lattice = getLatticeElementFor(getProgramPointAfter(op), array);
        if (auto vector = llvm::dyn_cast_or_null<VecType>(lattice->getValue().getType())) {
            return meet(element, vector.getElementType());
        }
        return element;
    }
    const auto appended = appends_.find(array);
    return appended == appends_.end() ? element : joinLattices(op, appended->second, element);
}

bool TypeInference::isRectTest(mlir::Operation * op) {
    auto read = llvm::dyn_cast_or_null<ctjs::GetPropertyOp>(op);
    return read && ctjs::constantKey(read.getKey()) == "getBoundingClientRect" &&
           !read.getResult().use_empty() &&
           llvm::all_of(read.getResult().getUsers(),
                        [](mlir::Operation * user) { return llvm::isa<ctjs::TruthyOp>(user); });
}

bool TypeInference::isRectCall(mlir::Operation * op) {
    auto call = llvm::dyn_cast_or_null<ctjs::CallOp>(op);
    if (!call || !call.getArgs().empty() || ctjs::methodKey(call) != "getBoundingClientRect") {
        return false;
    }
    auto read = ctjs::receiverMethod(call);
    return read.getObject() == call.getReceiver() && read.getResult().hasOneUse();
}

mlir::Value TypeInference::deadRectReceiver(mlir::Operation * op) {
    auto null = llvm::dyn_cast_or_null<ctjs::ConstantOp>(op);
    if (!null || !llvm::isa<ctjs::NullAttr>(null.getValue()) || !null.getResult().hasOneUse()) {
        return {};
    }
    mlir::OpOperand & use = *null.getResult().getUses().begin();
    auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(use.getOwner());
    auto selected =
        yield ? llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp()) : mlir::scf::IfOp{};
    if (!selected || yield->getParentRegion() != &selected.getElseRegion()) { return {}; }
    auto truthy = selected.getCondition().getDefiningOp<ctjs::TruthyOp>();
    auto test =
        truthy ? truthy.getValue().getDefiningOp<ctjs::GetPropertyOp>() : ctjs::GetPropertyOp{};
    if (!test || !isRectTest(test)) { return {}; }
    mlir::Operation * made =
        selected.thenYield().getOperand(use.getOperandNumber()).getDefiningOp();
    if (!isRectCall(made) || llvm::cast<ctjs::CallOp>(made).getReceiver() != test.getObject()) {
        return {};
    }
    return test.getObject();
}

bool TypeInference::isStyleCall(mlir::Operation * op) {
    auto call = llvm::dyn_cast_or_null<ctjs::CallOp>(op);
    return call && call.getArgs().size() == 1 &&
           isRealmGlobal(call.getCallee().getDefiningOp(), "getComputedStyle");
}

bool TypeInference::isStylePropertyCall(mlir::Operation * op) {
    auto call = llvm::dyn_cast_or_null<ctjs::CallOp>(op);
    if (!call || call.getArgs().size() != 1 || ctjs::methodKey(call) != "getPropertyValue") {
        return false;
    }
    auto read = ctjs::receiverMethod(call);
    return read.getObject() == call.getReceiver() && read.getResult().hasOneUse();
}

mlir::Type TypeInference::domReadType(mlir::Operation * op,
                                      llvm::ArrayRef<const TypeLattice *> operands) {
    mlir::MLIRContext * c = op->getContext();
    const auto element = [&](mlir::Value value) {
        return llvm::isa_and_nonnull<DOMElementType>(joinLattices(op, value, mlir::Type{}));
    };
    // `el.getBoundingClientRect ? el.getBoundingClientRect() : null` on an
    // element: the method exists, so the test is true, the call answers a rect
    // and the null is on a branch that never runs. Typing that null as the
    // rect keeps the join a rect rather than an optional one.
    if (isRectCall(op) && element(llvm::cast<ctjs::CallOp>(op).getReceiver())) {
        return DOMRectType::get(c);
    }
    if (const mlir::Value receiver = deadRectReceiver(op); receiver && element(receiver)) {
        return DOMRectType::get(c);
    }
    // getComputedStyle(el) and its getPropertyValue(name), a String.
    if (isStyleCall(op) && element(llvm::cast<ctjs::CallOp>(op).getArgs()[0])) {
        return ComputedStyleType::get(c);
    }
    if (isStylePropertyCall(op)) {
        auto call = llvm::cast<ctjs::CallOp>(op);
        const bool style = llvm::isa_and_nonnull<ComputedStyleType>(
            joinLattices(op, call.getReceiver(), mlir::Type{}));
        const bool name =
            llvm::isa_and_nonnull<StrType>(joinLattices(op, call.getArgs()[0], mlir::Type{}));
        if (style && name) { return stringType(c); }
    }
    auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
    if (!read) { return {}; }
    if (llvm::isa_and_nonnull<DOMRectType>(operands[0]->getValue().getType())) {
        const llvm::StringRef field = ctjs::constantKey(read.getKey());
        return field == "left" || field == "top" || field == "width" || field == "height"
                   ? doubleType(c)
                   : mlir::Type{};
    }
    const llvm::StringRef key = ctjs::constantKey(read.getKey());
    if (key == "documentElement" && isRealmGlobal(read.getObject().getDefiningOp(), "document")) {
        return DOMElementType::get(c);
    }
    if (key == "devicePixelRatio" && isRealmGlobal(read.getObject().getDefiningOp(), "window")) {
        return doubleType(c);
    }
    if (!llvm::isa_and_nonnull<DOMElementType>(operands[0]->getValue().getType())) { return {}; }
    if (key == "tagName" || key == "id" || key == "className") { return stringType(c); }
    // The method itself, read only to test that it exists: a function, so true.
    if (isRectTest(op)) { return boolType(c); }
    if (key == "children") { return VecType::get(c, DOMElementType::get(c)); }
    // Only the root's client size is native (Page.hpp's clientWidth throws
    // for any other element), so only a read straight off documentElement.
    auto root = read.getObject().getDefiningOp<ctjs::GetPropertyOp>();
    if ((key == "clientWidth" || key == "clientHeight") && root &&
        ctjs::constantKey(root.getKey()) == "documentElement") {
        return doubleType(c);
    }
    return {};
}

mlir::Type TypeInference::elementVectorType(mlir::Operation * op,
                                            llvm::ArrayRef<const TypeLattice *> operands,
                                            bool & pending) {
    mlir::MLIRContext * c = op->getContext();
    // The element type of the array `members` names: the join of everything
    // stored into it, plus String when an owner is a split result. Null with
    // `pending` set while every stored value is still unvisited.
    const auto element = [&](llvm::ArrayRef<mlir::Value> members) -> mlir::Type {
        mlir::Type joined = joinLattices(op, elementVectorStores(members), mlir::Type{});
        joined = presentElements(op, members, joined);
        if (splitsAString(members)) { joined = meet(joined, stringType(c)); }
        return joined;
    };
    const auto typeOf = [](const TypeLattice * lattice) { return lattice->getValue().getType(); };
    // The element types lowered this way: Strings, tuples, and an ordinary
    // script's DOM elements (a host contract's elements are its own proof's).
    const bool pageElements = !ownedRoots_ && !domEntry_;
    const auto lowered = [pageElements](mlir::Type type) {
        return llvm::isa_and_nonnull<StrType, TupleType>(type) ||
               (pageElements && llvm::isa_and_nonnull<DOMElementType>(type));
    };
    if (auto array = llvm::dyn_cast<ctjs::CreateArrayOp>(op); array && isTupleLiteral(array)) {
        // One type per position, each the type of what that position holds.
        llvm::SmallVector<mlir::Type, 2> positions;
        for (mlir::Value part : tupleParts(array.getResult())) {
            const mlir::Type held = joinLattices(op, part, mlir::Type{});
            if (!held) {
                pending = true;
                return {};
            }
            positions.push_back(held);
        }
        return TupleType::get(c, positions);
    }
    if (auto array = llvm::dyn_cast<ctjs::CreateArrayOp>(op)) {
        const auto members = elementVectorAliases(array.getResult());
        if (members.empty()) { return {}; }
        // A dense array the numeric proof also accepts keeps that proof's
        // type unless it holds Strings, and an empty one stays numeric.
        const bool dense = isDenseVectorSite(array.getResult());
        if (elementVectorStores(members).empty() && !splitsAString(members)) {
            return dense ? mlir::Type{} : mlir::Type(VecType::get(c, stringType(c)));
        }
        const mlir::Type stored = element(members);
        if (!stored) {
            pending = true;
            return {};
        }
        // Only Strings and tuples are lowered this way yet. Any other element
        // type keeps the dense proof's type, or none, and with it that proof's
        // refusal.
        if (!lowered(stored)) { return {}; }
        return VecType::get(c, stored);
    }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
        // `t[k]` on a tuple at a constant position is that position's type.
        if (auto tuple = llvm::dyn_cast_or_null<TupleType>(typeOf(operands[0]))) {
            const int64_t position = constantIndex(read.getKey());
            return position >= 0 && position < static_cast<int64_t>(tuple.getElements().size())
                       ? tuple.getElements()[static_cast<size_t>(position)]
                       : mlir::Type{};
        }
        auto vector = llvm::dyn_cast_or_null<VecType>(typeOf(operands[0]));
        if (!vector ||
            (!lowered(vector.getElementType()) && !(llvm::isa<NumType>(vector.getElementType()) &&
                                                    isNumberElementVector(read.getObject()))) ||
            elementVectorAliases(read.getObject()).empty()) {
            return {};
        }
        const llvm::StringRef key = ctjs::constantKey(read.getKey());
        if (key == "length") { return doubleType(c); }
        // An index proved in bounds reads the element; otherwise one past
        // the end, a fraction or a negative reads undefined.
        if (key.empty() && isProvedNumeric(operands[1])) {
            const bool exists = isBoundedIndexRead(op) ||
                                (llvm::isa<TupleType>(vector.getElementType()) && isRowRead(op));
            return exists ? vector.getElementType() : meet(absentType(c), vector.getElementType());
        }
        return {};
    }
    // push answers on a proved element vector, join and sort only on one of
    // Strings.
    const auto ofStrings = [&](const TypeLattice * lattice) {
        auto vector = llvm::dyn_cast_or_null<VecType>(typeOf(lattice));
        return vector && llvm::isa<StrType>(vector.getElementType());
    };
    const auto ofElements = [&](const TypeLattice * lattice) {
        auto vector = llvm::dyn_cast_or_null<VecType>(typeOf(lattice));
        return vector && lowered(vector.getElementType());
    };
    switch (vectorCallOf(op)) {
    case VectorCall::push: return ofElements(operands[1]) ? doubleType(c) : mlir::Type{};
    case VectorCall::join: return ofStrings(operands[1]) ? stringType(c) : mlir::Type{};
    // `a.sort()` is `a`.
    case VectorCall::sort: return ofStrings(operands[1]) ? typeOf(operands[1]) : mlir::Type{};
    case VectorCall::split:
    case VectorCall::splitSpace: {
        if (!llvm::isa_and_nonnull<StrType>(typeOf(operands[1]))) { return {}; }
        const auto members = elementVectorAliases(op->getResult(0));
        if (members.empty()) { return VecType::get(c, stringType(c)); }
        const mlir::Type stored = element(members);
        return VecType::get(c, stored ? stored : stringType(c));
    }
    case VectorCall::builtinStatic: {
        // The row's result once each String parameter is given a String;
        // admission checks the others.
        const BuiltinStatic & row = *builtinStaticOf(op);
        for (std::size_t i = 2; i < operands.size(); ++i) {
            if (row.params[i - 2] == BuiltinStatic::Param::string &&
                !llvm::isa_and_nonnull<StrType>(typeOf(operands[i]))) {
                return {};
            }
        }
        switch (row.result) {
        case BuiltinStatic::Result::string: return stringType(c);
        case BuiltinStatic::Result::elementVector: return VecType::get(c, DOMElementType::get(c));
        case BuiltinStatic::Result::nullableElement: return OptType::get(c, DOMElementType::get(c));
        case BuiltinStatic::Result::undefined: return absentType(c);
        case BuiltinStatic::Result::number: return doubleType(c);
        // Only a call the constant-table proof marked: every field a String.
        case BuiltinStatic::Result::tableEntries:
            return op->hasAttr(kNativeStaticTableEnumeration)
                       ? VecType::get(
                             c, TupleType::get(c, llvm::SmallVector<mlir::Type, 2>{stringType(c),
                                                                                   stringType(c)}))
                       : mlir::Type{};
        case BuiltinStatic::Result::tableKeys:
        case BuiltinStatic::Result::tableValues:
            return op->hasAttr(kNativeStaticTableEnumeration) ? VecType::get(c, stringType(c))
                                                              : mlir::Type{};
        }
        return {};
    }
    case VectorCall::includes: return ofStrings(operands[1]) ? boolType(c) : mlir::Type{};
    case VectorCall::callback: return arrayCallbackType(op, operands, pending);
    case VectorCall::regexp:
    case VectorCall::none: return {};
    }
    return {};
}

mlir::Type TypeInference::joinLattices(mlir::Operation * op, mlir::ValueRange values,
                                       mlir::Type seed) {
    for (mlir::Value value : values) {
        const TypeLattice * lattice = getLatticeElementFor(getProgramPointAfter(op), value);
        // A value never visited is dead code and never executes.
        if (!lattice->getValue().isUninitialized()) {
            seed = meet(seed, lattice->getValue().getType());
        }
    }
    return seed;
}

// Returns true when a store of `key` to `object` is guaranteed to run before
// `read`, so the read can never see the field missing. The conditions and the
// reason for each are documented on the declaration in TypeInference.h.
bool TypeInference::fieldIsAssignedBefore(mlir::Value object, llvm::StringRef key,
                                          mlir::Operation * read) {
    if (nativeObjectFieldGroup(read) >= 0) { return assignedIdentityFields_.contains(read); }
    llvm::DenseSet<mlir::Value> active;
    unsigned work = 0;
    const auto assigned = [&](auto && self, mlir::Value value, mlir::Operation * before) -> bool {
        // ponytail: at most 64 forwarding frames and 65536 queries. Larger or
        // recursive borrows would need a cached interprocedural presence proof.
        if (++work > 65536 || active.size() >= 64) { return false; }
        if (auto origin = nativeMapRecordOrigin(value)) { return self(self, origin, before); }
        const auto sites = fieldStoreSites_.find({value, key});
        auto * owner = before->getParentOfType<ctjs::FuncOp>().getOperation();
        if (sites != fieldStoreSites_.end()) {
            for (mlir::Operation * store : sites->second) {
                // A module body is a graph region: dominance across functions
                // says nothing about execution order. Compare owners first.
                if (store->getParentOfType<ctjs::FuncOp>().getOperation() != owner) { continue; }
                if (dominance_.properlyDominates(store, before)) { return true; }
            }
        }
        // A record call's field is assigned when the callee stored it before
        // every return of the literal it returns.
        if (isRecordCall(value)) {
            if (!active.insert(value).second) { return false; }
            bool all = true;
            value.getDefiningOp<ctjs::CallDirectOp>().getTarget().walk(
                [&](ctjs::ReturnOp ret) { all = all && self(self, ret.getValue(), ret); });
            active.erase(value);
            return all;
        }
        const auto callers = objectParameterCallSites_.find(value);
        if (callers == objectParameterCallSites_.end() || !active.insert(value).second) {
            return false;
        }
        const unsigned index = llvm::cast<mlir::BlockArgument>(value).getArgNumber();
        for (mlir::Operation * call : callers->second) {
            const mlir::Value actual = call->getOperand(index);
            // Each forwarded slot needs its own complete callable census.
            // A schema group alone proves neither origin nor initialization.
            if ((!actual.getDefiningOp<ctjs::CreateObjectOp>() && !isRecordCall(actual) &&
                 !objectParameterCallSites_.contains(actual)) ||
                !self(self, actual, call)) {
                return false;
            }
        }
        active.erase(value);
        return true;
    };
    return assigned(assigned, object, read);
}

mlir::Type TypeInference::cellTypeOf(mlir::Operation * op, mlir::Value cell) {
    // The join starts from the cell's initial value, and that is what makes it
    // sound: a read on a path that reached no assignment yields whatever the
    // box was created with. `compiler_impl::predeclare_locals` boxes
    // `undefined`, so that is normally `opt<>` and the join is `opt<num>`: a
    // double whose undefined is NaN, exact in arithmetic, comparison and
    // truthiness, and refused at equality by `defined()`. No dominance proof is
    // needed here because the lowering keeps the box rather than replacing it.
    //
    // Exception: when the closure lift proved that a write dominates every read
    // of the binding, it marked the cell `kAssignedBeforeRead` and
    // `initialize()` left the initial out of `cellStores_`. This join is then
    // over the writes alone, so a hoisted `var` assigned before it is read is
    // `num`, not `opt<num>`. This function trusts that set and does not
    // re-check it.
    const auto stored = cellStores_.find(cell);
    if (stored == cellStores_.end()) { return BoxedType::get(op->getContext()); }
    // No seed: the join starts empty, because `cellStores_` already holds the
    // initial when it can be observed. `n = n + 1` is the cycle this must
    // tolerate: the stored value reads the box, so it may not be visited yet.
    return joinLattices(op, stored->second, {});
}

mlir::Type TypeInference::mapTypeOf(mlir::Operation * op, mlir::Value map) {
    mlir::Type key = BottomType::get(op->getContext());
    if (auto found = mapKeys_.find(nativeMapGroup(map)); found != mapKeys_.end()) {
        for (const auto & [value, exact] : found->second) {
            if (exact && !llvm::isa<NumType>(exact)) {
                key = meet(key, exact);
                continue;
            }
            const auto * lattice = getLatticeElementFor(getProgramPointAfter(op), value);
            if (!lattice->getValue().isUninitialized()) {
                const auto inferred = lattice->getValue().getType();
                // A semantic Number tag does not discard an independently
                // inferred integer width. Wait for that lattice rather than
                // prematurely widening a literal key to double.
                key = meet(key,
                           exact && !llvm::isa<NumType, BottomType>(inferred) ? exact : inferred);
            }
        }
    }
    mlir::Type value = BottomType::get(op->getContext());
    if (auto found = mapValues_.find(nativeMapGroup(map)); found != mapValues_.end()) {
        value = joinLattices(op, found->second, value);
    }
    return MapType::get(op->getContext(), key, value);
}

void TypeInference::setToEntryState(TypeLattice * lattice) {
    auto value = lattice->getAnchor();
    if (isArrayCallbackParameter(value) || isHostCallbackParameter(value)) { return; }
    // A specialised replace callback's parameters come from its call.
    if (isStringCallbackParameter(value)) { return; }
    const bool domInput =
        ownedRoots_ && ownedRoots_->proved() && llvm::is_contained(ownedRoots_->domInputs(), value);
    mlir::Type type = domInput || (domEntry_ && domEntry_->isElement(value))
                          ? mlir::Type(DOMElementType::get(value.getContext()))
                      : domEntry_ && domEntry_->isCallbackParameter(value)
                          ? mlir::Type(StrType::get(value.getContext(), StrEncoding::UTF8))
                          : mlir::Type(BoxedType::get(value.getContext()));
    std::optional<HostIntrinsicParameter> parameter;
    if (domEntry_) { parameter = domEntry_->intrinsicParameter(value); }
    // A declared parameter of a function the embedder calls with Strings
    // (--ctnative-lower-to-emitc string-entries). The first three block
    // arguments are the implicit receiver, new.target and callee.
    if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
        argument && argument.getArgNumber() >= 3 && argument.getOwner()->isEntryBlock()) {
        auto fn = llvm::dyn_cast<ctjs::FuncOp>(argument.getOwner()->getParentOp());
        if (fn && fn->hasAttr("ctnative.string_entry")) {
            parameter = HostIntrinsicParameter::string;
        }
    }
    if (parameter) {
        switch (*parameter) {
        case HostIntrinsicParameter::boolean: type = BoolType::get(value.getContext()); break;
        case HostIntrinsicParameter::number: type = NumType::getDouble(value.getContext()); break;
        case HostIntrinsicParameter::string:
            type = StrType::get(value.getContext(), StrEncoding::UTF8);
            break;
        case HostIntrinsicParameter::symbol: type = SymbolType::get(value.getContext()); break;
        }
    }
    // A borrowed callback's parameters are joined from its closure and calls.
    if (isBorrowedCallbackParameter(value)) { return; }
    propagateIfChanged(lattice, lattice->join(TypeValue{type}));
}

mlir::LogicalResult TypeInference::visitOperation(mlir::Operation * op,
                                                  llvm::ArrayRef<const TypeLattice *> operands,
                                                  llvm::ArrayRef<TypeLattice *> results) {
    mlir::MLIRContext * c = op->getContext();
    // Answers `type` for the first result and ends the visit.
    const auto answer = [&](mlir::Type type) {
        propagateIfChanged(results[0], results[0]->join(TypeValue{type}));
        return mlir::success();
    };
    // Answers `value`'s type and subscribes to it; answers nothing while it is
    // still pending.
    const auto forward = [&](mlir::Value value) {
        const auto * source = getLatticeElementFor(getProgramPointAfter(op), value);
        if (!source->getValue().isUninitialized()) {
            propagateIfChanged(results[0], results[0]->join(source->getValue()));
        }
        return mlir::success();
    };

    if (auto refine = llvm::dyn_cast<RefineOp>(op)) {
        const TypeValue & from = operands[0]->getValue();
        if (from.isUninitialized() && refine.getKind() == "truthy") { return mlir::success(); }
        return answer(refinedType(c, refine.getKind(), from.getType()));
    }

    if (auto visited = visitBorrowedCallback(op, operands, results)) { return *visited; }
    // A run-once static's constant table (Inference/StaticTables.cpp): the
    // literal is the table, and a proved read is the constant in its field.
    if (auto name = op->getAttrOfType<mlir::StringAttr>(kNativeStaticTable)) {
        if (llvm::isa<ctjs::CreateObjectOp>(op)) {
            return answer(StaticTableType::get(c, name.getValue()));
        }
        if (auto field = op->getAttr(kNativeStaticTableField)) {
            // A spread's read of a table that may still be undefined.
            const auto type = typeOfConstant(c, field);
            return answer(op->hasAttr(kNativeStaticTableSpread)
                              ? TypeValue::join(TypeValue{type}, TypeValue{absentType(c)}).getType()
                              : type);
        }
    }

    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
        read && domEntry_ && domEntry_->isSymbolDescription(read)) {
        return answer(OptType::get(c, stringType(c)));
    }

    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
        read && domEntry_ && !domEntry_->wellKnownSymbol(read).empty()) {
        return answer(SymbolType::get(c));
    }

    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
        read && domEntry_ &&
        (domEntry_->isElementVectorIndex(read) || domEntry_->isDocumentElement(read))) {
        return answer(DOMElementType::get(c));
    }

    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
        read && domEntry_ &&
        (domEntry_->isStringVectorIndex(read) || domEntry_->datasetValueElement(read))) {
        return answer(stringType(c));
    }

    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
        read && domEntry_ &&
        (domEntry_->isStringVectorLength(read) || domEntry_->isElementVectorLength(read))) {
        return answer(doubleType(c));
    }

    if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(op);
        object && domEntry_ && domEntry_->jsonObject(object)) {
        return answer(JsonType::get(c));
    }

    if (auto call = llvm::dyn_cast<ctjs::CallOp>(op); call && domEntry_) {
        if (const auto * edge = domEntry_->call(call)) {
            const mlir::Type type =
                edge->returnsOptionalString()  ? mlir::Type(OptType::get(c, stringType(c)))
                : edge->returnsNumber()        ? doubleType(c)
                : edge->returnsSymbol()        ? mlir::Type(SymbolType::get(c))
                : edge->returnsString()        ? mlir::Type(stringType(c))
                : edge->returnsElement()       ? mlir::Type(DOMElementType::get(c))
                : edge->returnsStringVector()  ? mlir::Type(VecType::get(c, stringType(c)))
                : edge->returnsElementVector() ? mlir::Type(VecType::get(c, DOMElementType::get(c)))
                : edge->returnsJSON()          ? mlir::Type(JsonType::get(c))
                : edge->returnsBoolean()       ? boolType(c)
                                               : absentType(c);
            return answer(type);
        }
    }

    if (const auto * root = ownedRoots_ ? ownedRoots_->lookup(op) : nullptr) {
        if (llvm::isa<ctjs::CreateObjectOp, ctjs::LoadGlobalOp>(op)) {
            return answer(GlobalObjectType::get(c, root->binding));
        }
        if (llvm::isa<ctjs::GetPropertyOp>(op)) {
            // This exact write precedes every read in the complete live
            // proof. Subscribe to its value; no absent or prototype arm is
            // possible, even when the receiver came from a global load.
            auto write = root->fieldInitialization;
            return forward(write.getValue());
        }
    }

    if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op);
        load && ownedRoots_ && ownedRoots_->proved()) {
        if (const auto * edge = ownedRoots_->objectGlobal(load)) {
            // The owner proof establishes this exact allocation/store/load
            // edge. Its type still comes from the original live allocation.
            auto object = edge->object;
            return forward(object.getResult());
        }
    }
    if (llvm::isa<ctjs::CreateObjectOp>(op) && op->hasAttr(kNativeObjectIdentity)) {
        return answer(ObjectIdentityType::get(c));
    }
    if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(op);
        object && !methodTableName(op).empty()) {
        return answer(MethodTableType::get(c, methodTableName(op)));
    }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op); read && !methodTableName(op).empty()) {
        return answer(ClosureType::get(c, environmentTarget(op)));
    }

    // The callable's nominal identity is known before any capture type. Its
    // environment reads subscribe to the original captured value; requiring
    // all capture types first would deadlock Map inference through a closure.
    if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(op);
        made && !environmentTarget(op).empty()) {
        return answer(ClosureType::get(c, environmentTarget(op)));
    }
    if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op);
        read && op->hasAttr(kNativeEnvironmentRead)) {
        const auto at = environmentCaptures_.find(environmentTarget(op));
        if (at == environmentCaptures_.end() || read.getIndex() < 0 ||
            static_cast<size_t>(read.getIndex()) >= at->second.size()) {
            return op->emitError("native environment read has no proved capture");
        }
        return forward(at->second[static_cast<size_t>(read.getIndex())]);
    }

    // An uninitialized operand means "not yet", not "unknown". The framework
    // visits an operation before every value feeding it has an answer (a
    // loop's back edge, a recursive call's result) and re-visits it when they
    // do. Answering `boxed` for an operand that has merely not arrived is wrong
    // in the one direction the lattice cannot undo: boxed absorbs, so a counter
    // incremented on a back edge, or fib's own result, would stay boxed
    // forever, and the native lowering would refuse every recursive function
    // and every `for` loop. Returning here leaves the result uninitialized,
    // which is what its consumers must wait for; a value that never
    // initializes is dead code and is boxed by whoever reads the final state.
    for (const TypeLattice * operand : operands) {
        if (operand->getValue().isUninitialized()) { return mlir::success(); }
    }
    // Instances a registry Map owns (Inference/Instances.cpp).
    if (const auto instance = instanceTypeOf(op, operands)) {
        return *instance ? answer(*instance) : mlir::success();
    }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
        read && isProvedString(operands[0]) && constantKey(read.getKey()) == "length") {
        return answer(doubleType(c));
    }

    if (op->getNumResults() == 1) {
        if (const mlir::Type table = dictionaryType(op, operands)) { return answer(table); }
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
                // This finite result is independent of the family schema,
                // including unvisited writes that depend on this same read.
                // Seed it immediately: widening first cannot be undone by a
                // later iteration of the monotone solver.
                auto readType = call->getAttrOfType<mlir::StringAttr>(kNativeMapReadType);
                if (auto tagged = readType ? mapTagType(c, readType.getValue()) : mlir::Type{}) {
                    mapAnswer = tagged;
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

    // The DOM reads of an ordinary script (no host contract): the page's root
    // element, an element's text members and children, the root's client
    // size and the window's device pixel ratio (Runtime/Browser.hpp, Page.hpp).
    if (!ownedRoots_ && !domEntry_) {
        if (const mlir::Type member = elementMemberType(op, operands)) { return answer(member); }
        if (const mlir::Type read = domReadType(op, operands)) { return answer(read); }
        if (const mlir::Type registered = hostCallbackType(op, operands)) {
            return answer(registered);
        }
    }
    // RegExp.prototype.test of a String: a Boolean (Inference/StringCallbacks.cpp).
    if (isStringMethodCall(op) &&
        op->getAttrOfType<mlir::StringAttr>(kNativeStringMethod) == "test") {
        if (isProvedString(operands[2])) { return answer(BoolType::get(c)); }
    } else if (isStringMethodCall(op) && isProvedString(operands[1])) {
        // String.prototype.toLowerCase, and replace with a specialised function.
        return answer(stringType(c));
    }
    // `text[i]` with a Number index: a one-unit String, or undefined past the
    // end (ECMA-262 10.4.3.5).
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
        read && ctjs::constantKey(read.getKey()).empty() && isProvedString(operands[0]) &&
        llvm::isa_and_nonnull<NumType>(operands[1]->getValue().getType())) {
        return answer(meet(absentType(c), stringType(c)));
    }
    if (isStringCallbackCall(op)) {
        bool pending = false;
        const mlir::Type replaced = stringCallbackType(op, operands, pending);
        if (pending) { return mlir::success(); }
        if (replaced) { return answer(replaced); }
    }

    // Vectors of one proved element type, and split on a String.
    bool pendingVector = false;
    if (const mlir::Type vectorAnswer = elementVectorType(op, operands, pendingVector)) {
        return answer(vectorAnswer);
    }
    if (pendingVector) { return mlir::success(); }

    // The operand-sensitive rules, which exist only because of BigInt. Each one
    // is "the numeric answer, if neither operand can be a BigInt".
    mlir::Type numeric{};
    if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op)) {
        if (noneAreBigInt(operands)) {
            // `-x` is a double, not an int32: `-0` is not an int32, and
            // negating INT32_MIN leaves the int32 range.
            if (unary.getKind() == ctjs::UnaryKind::Neg) { numeric = doubleType(c); }
            // `~x` is ToInt32 then a bitwise complement, which stays in int32.
            if (unary.getKind() == ctjs::UnaryKind::BitNot) { numeric = int32Type(c); }
        }
    } else if (llvm::isa<ctjs::BinaryOp, ctjs::BinaryStaticOp>(op)) {
        const auto kind = llvm::isa<ctjs::BinaryOp>(op)
                              ? llvm::cast<ctjs::BinaryOp>(op).getKind()
                              : llvm::cast<ctjs::BinaryStaticOp>(op).getKind();
        // A proved string on either side of generic `+` makes a string, and
        // this sits outside the BigInt guard on purpose: `"x" + 1n` is "x1".
        // The other operand's ToPrimitive then ToString always yields a
        // string; a Symbol throws and produces no value at all.
        if (llvm::isa<ctjs::BinaryOp>(op) && kind == ctjs::BinaryKind::Add &&
            (isProvedString(operands[0]) || isProvedString(operands[1]))) {
            numeric = stringType(c);
        }
        if (numeric == nullptr && noneAreBigInt(operands)) {
            switch (kind) {
            // The int32 family. ECMAScript defines all five through ToInt32,
            // and to_int32 in this VM returns an int32_t, so the result is one.
            case ctjs::BinaryKind::BitAnd:
            case ctjs::BinaryKind::BitOr:
            case ctjs::BinaryKind::BitXor:
            case ctjs::BinaryKind::Shl:
            case ctjs::BinaryKind::Shr: numeric = int32Type(c); break;
            // Arithmetic yields a double, not an int32: `2**31` overflows an
            // int32 but not a double.
            //
            // Only for the generic family. binary_op_static has no arm for
            // these and answers undefined; the verifier on ctjs.binary_static
            // already rejects them, and this check is a second guard.
            case ctjs::BinaryKind::Sub:
            case ctjs::BinaryKind::Mul:
            case ctjs::BinaryKind::Div:
            case ctjs::BinaryKind::Mod:
            case ctjs::BinaryKind::Pow:
                if (llvm::isa<ctjs::BinaryOp>(op)) { numeric = doubleType(c); }
                break;
            // Generic `+` concatenates when either side is a string and adds
            // otherwise. The string case is handled above. Two operands that
            // are each a number, a boolean, undefined or null add numerically,
            // because ToPrimitive is the identity on all four and none is a
            // string or a BigInt. Anything else (an object with its own
            // valueOf, a value nothing proved) is boxed. The static family
            // reaches `+` only from `++` and its counters, through to_number,
            // so there it is a number outright.
            case ctjs::BinaryKind::Add:
                if (llvm::isa<ctjs::BinaryStaticOp>(op)) {
                    numeric = doubleType(c);
                } else if (isProvedNumeric(operands[0]) && isProvedNumeric(operands[1])) {
                    numeric = doubleType(c);
                } else if (isProvedPrimitiveType(operands[0]->getValue().getType(), true) &&
                           isProvedPrimitiveType(operands[1]->getValue().getType(), true)) {
                    // A finite primitive union may select either Number addition
                    // or String concatenation; neither requires object hooks.
                    numeric = meet(doubleType(c), stringType(c));
                }
                break;
            default: break;
            }
        }
    }

    // Closed-world globals (see TypeInference.h). getLatticeElementFor
    // subscribes this load to every store's operand, so a store whose type
    // widens later re-visits the load.
    std::optional<mlir::Type> global;
    // Only on the ordinary closed-source path: a host contract's owner and DOM
    // proofs settle `undefined` themselves (materializeHostPrimitives).
    if (!ownedRoots_ && !domEntry_ && isUndefinedGlobal(op)) {
        global = absentType(c);
    } else if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
        const auto stores = globalStores_.find(load.getName());
        if (!globalsAreDynamic_ && stores != globalStores_.end() && !stores->second.empty()) {
            // A global can be read before its first store. Only the complete
            // live owner proof can remove that implicit absence for an exact
            // scalar load. Check its edge against this module's store index;
            // the host category supplies no native type or value.
            global = absentType(c);
            const auto * scalar =
                ownedRoots_ && ownedRoots_->proved() ? ownedRoots_->scalarRead(load) : nullptr;
            if (scalar && scalar->read == load && stores->second.size() == 1) {
                auto initialization = scalar->initialization;
                if (initialization && initialization.getName() == load.getName() &&
                    initialization.getValue() == scalar->value &&
                    stores->second.front() == scalar->value) {
                    global = mlir::Type{};
                }
            }
            global = joinLattices(op, stores->second, *global);
            // A proved initialized load may precede its producer in solver
            // order. Keep it pending until that subscription produces a type;
            // boxed here would permanently absorb the eventual native type.
            if (!*global) { return mlir::success(); }
        }
    }

    // Closed-shape field reads: the join over the stores of that key to that
    // object, starting from undefined. Same mechanism as the global rule, with
    // getLatticeElementFor subscribing this read to every store.
    std::optional<mlir::Type> field;
    if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
        const int64_t identityGroup = nativeObjectFieldGroup(op);
        if (identityGroup >= 0) {
            // The schema joins all stored value types, including explicit
            // Undefined. Only an independent live per-read allocation and
            // initialization proof can remove implicit field absence.
            field = fieldIsAssignedBefore(get.getObject(), constantKey(get.getKey()), op)
                        ? mlir::Type{}
                        : absentType(c);
            const auto stores =
                identityFieldStores_.find({identityGroup, constantKey(get.getKey())});
            if (stores != identityFieldStores_.end()) {
                field = joinLattices(op, stores->second, *field);
            }
            // With absence proved impossible, stored-value lattices may all
            // still be pending. Subscriptions above revisit this read; boxed
            // here would irreversibly absorb the eventual scalar type.
            if (!*field) { return mlir::success(); }
        } else if (hasClosedShape(get.getObject())) {
            const llvm::StringRef key = constantKey(get.getKey());
            // The `undefined` seed is dropped when a `ctjs.set_property` of
            // this key on this object comes before the read on every path.
            // `var p = { n: 8 }; return p.n;` is then `num` rather than
            // `opt<num>`, which lets a `ctjs.store_global` of it through (a
            // global is printed as `%.17g` and cannot spell `undefined`). The
            // join below still covers the whole alias group: this removes only
            // the value the field cannot hold, not any value it can.
            field = fieldIsAssignedBefore(get.getObject(), key, op) ? mlir::Type{} : absentType(c);
            const auto stores = fieldStores_.find({get.getObject(), key});
            if (stores != fieldStores_.end()) { field = joinLattices(op, stores->second, *field); }
            // An empty join means "not yet", not `boxed`, for the reason given
            // at the carried cell below, and it is reachable here likewise:
            // with the `undefined` seed dropped, every member of this join is a
            // stored value the solver may not have visited yet. A closed shape
            // can never also be a dense array (`hasClosedShape` demands a
            // `ctjs.create_object` or an object parameter), so returning here
            // skips no other row.
            if (!*field) { return mlir::success(); }
        }
    }

    // Dense arrays. The literal itself is a `vec<element>`, a read of `length`
    // is a Number, and a read through an index is the element type. As in the
    // two rules above, getLatticeElementFor subscribes the read to every
    // appended value.
    std::optional<mlir::Type> vector;
    if (auto array = llvm::dyn_cast<ctjs::CreateArrayOp>(op)) {
        if (isDenseVectorSite(array.getResult())) {
            vector = VecType::get(c, elementTypeOf(op, array.getResult()));
        }
    } else if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
        if (isDenseVectorSite(get.getObject())) {
            if (constantKey(get.getKey()) == "length") {
                // Not an i32: an array's length is a uint32, which does not
                // fit an int32, and nothing here proves this one is small.
                // `f64` is exact for every possible length.
                vector = doubleType(c);
            } else if (isProvedNumeric(operands[1])) {
                // The key has to be proved a number, not merely "not a
                // constant string". `a[k]` with a string `k` reads a property:
                // `a["push"]` is a function, and claiming the element type for
                // it would be unsound on any array whose only other uses are
                // appends and index reads. A boolean, undefined or null key
                // names a property nothing wrote, which is `undefined` - and
                // undefined is where the join below starts.
                if (const auto found = arrayReadValues_.find(op); found != arrayReadValues_.end()) {
                    // Unlike the storage schema, this read sees only its exact
                    // current origins. No undefined seed: every path owns this
                    // index. Subscribe to all origins and wait for the first type.
                    vector = joinLattices(op, found->second, mlir::Type{});
                    if (!*vector) { return mlir::success(); }
                } else {
                    vector = elementTypeOf(op, get.getObject());
                }
            }
        }
    }

    // Shared (carried) bindings. The cell and every read of it get the join
    // over everything ever assigned to it, starting from its initial value, so
    // the `ctjs.create_cell` and each `ctjs.cell_get` agree on one type.
    std::optional<mlir::Type> cell;
    if (auto made = llvm::dyn_cast<ctjs::CreateCellOp>(op)) {
        if (made->hasAttr("ctnative.carried")) {
            cell = cellTypeOf(op, made.getResult());
            // An empty join means "not yet", not `boxed`, as in the operand
            // loop at the top of this function. When the initial is left out
            // of the join (see cellTypeOf), the cell can be visited before any
            // of its assignments. Answering `boxed` would absorb every later
            // type, and the binding would never recover: `n = n + 1` is exactly
            // that cycle, because the stored value reads the cell.
            if (!*cell) { return mlir::success(); }
        }
    } else if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(op)) {
        // The operand's own type, not a second join: `operands[0]` is the box

        // in this frame or the pointer a lifted call handed us, and either
        // already carries the answer the row above computed. A second walk
        // here would be a second chance for the two to disagree.
        if (namesACarriedCell(read.getCell())) { cell = operands[0]->getValue().getType(); }
    }

    const mlir::Type fromOperation = cell     ? *cell
                                     : vector ? *vector
                                     : field  ? *field
                                     : global
                                         ? *global
                                         : (numeric != nullptr ? numeric : staticResultType(op));
    for (TypeLattice * result : results) {
        const mlir::Type answer = fromOperation != nullptr ? fromOperation : BoxedType::get(c);
        propagateIfChanged(result, result->join(TypeValue{answer}));
    }
    return mlir::success();
}

} // namespace ctcompile::ctnative
