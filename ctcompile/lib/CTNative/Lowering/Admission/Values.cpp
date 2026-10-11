//===- Admission/Values.cpp - Value-level native admission predicates -----===//
//
// The small predicates admission uses to classify values: which closures and
// cells lower to nothing, which arguments carry an object or a shared cell by
// pointer, which keys can be C++ field names, and which array literals are
// dense vectors.
//
//===----------------------------------------------------------------------===//

#include "Admission.h"
#include "ctcompile/Support/CppLiterals.hpp"

namespace ctcompile::ctnative::lowering_detail {

mlir::Type admission::typeOf(mlir::Value v) const {
    const TypeLattice * lattice = solver.lookupState<TypeLattice>(v);
    return lattice == nullptr ? mlir::Type{} : lattice->getValue().getType();
}

bool admission::refuse(std::string reason) {
    if (why.empty()) { why = std::move(reason); }
    return false;
}

/// Scalar operands have an exact numeric conversion; equality keeps their tags.
bool admission::numeric(mlir::Value v, llvm::StringRef where) {
    if (!isScalarCarrier(carrierOf(typeOf(v)))) {
        return refuse((where + " operand is " + printed(typeOf(v)) + ", not a number").str());
    }
    return true;
}

/// An observation preserves the alternatives of a proved scalar carrier.
bool admission::printable(mlir::Value v, llvm::StringRef where) {
    const auto stored = carrierOf(typeOf(v));
    if (!isPrimitiveCarrier(stored)) {
        return refuse((where + " is " + printed(typeOf(v)) +
                       "; native global observations require a supported scalar type")
                          .str());
    }
    return true;
}

bool admission::isDeclarationStore(mlir::Operation * o) {
    auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(o);
    return store && isDeclarationClosure(store.getValue().getDefiningOp());
}

/// A closure that closure lifting (kNativeLifted) already replaced: its
/// captures moved to the call sites and every call became a ctjs.call_direct,
/// so what is left is the `$callee_value` operand the call arm drops. Like a
/// declaration closure, it lowers to nothing. isUnboxedCell is the same for the
/// cell it captured: the cell was proved constant and every read of it is
/// already the value.
bool admission::isLiftedClosure(mlir::Operation * o) {
    return llvm::isa_and_nonnull<ctjs::CreateClosureOp>(o) && o->hasAttr(kNativeLifted);
}

bool admission::isUnboxedCell(mlir::Operation * o) {
    return llvm::isa_and_nonnull<ctjs::CreateCellOp>(o) && o->hasAttr(kNativeUnboxed);
}

bool admission::closureLowersToNothing(mlir::Operation * o) {
    return isDeclarationClosure(o) || isLiftedClosure(o);
}

/// Why this closure does not lower to nothing, in the words the lift wrote
/// onto it. Both the operation itself and its use of the enclosing `%arg2`
/// report this message, so the enclosing function is not wrongly refused as
/// "uses its own closure".
std::string admission::closureRefusal(mlir::Operation * o) {
    const llvm::StringRef what =
        o->hasAttr(kNativeMethodRefusal) ? "a method field" : "a closure used as a value";
    auto why = o->getAttrOfType<mlir::StringAttr>(kNativeClosureReason);
    return why ? (what + ": " + why.getValue()).str() : (what + " - Phase 59").str();
}

/// The callee value of a direct call lowers to nothing: the call names its
/// function by symbol, and the boxed closure the interpreter would have called
/// through is only carried so the boxed backend can still lower the same op.
/// A load_global whose every use is that operand is exempt from the carrier
/// check for that reason.
bool admission::feedsOnlyDirectCallees(mlir::Value v) {
    if (v.use_empty()) { return false; }
    for (mlir::OpOperand & use : v.getUses()) {
        auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
        if (!call || use.getOperandNumber() != 2) { return false; }
    }
    return true;
}

/// The entry-block indices that carry a `ctn_x *` (an object struct pointer),
/// read from a ctjs.func or one of its ctjs.call_directs. Both carry the same
/// list, which lets the caller and the callee be lowered in either order.
llvm::ArrayRef<int32_t> admission::objectArgsOf(mlir::Operation * o) {
    return listedSlots(o, kNativeObjectArgs);
}

bool admission::isObjectArg(mlir::Operation * o, unsigned index) {
    return llvm::is_contained(objectArgsOf(o), static_cast<int32_t>(index));
}

/// The entry-block indices that carry a `double *` (a shared binding passed by
/// address), listed on both a ctjs.func and its ctjs.call_directs: the box is
/// a variable in the caller's frame and the callee reads and writes it through
/// the pointer.
llvm::ArrayRef<int32_t> admission::cellArgsOf(mlir::Operation * o) {
    return listedSlots(o, kNativeCellArgs);
}

bool admission::isCellArg(mlir::Operation * o, unsigned index) {
    return llvm::is_contained(cellArgsOf(o), static_cast<int32_t>(index));
}

/// A `ctjs.create_cell` the lift made a frame-local variable.
bool admission::isCarriedCell(mlir::Operation * o) {
    return llvm::isa_and_nonnull<ctjs::CreateCellOp>(o) && o->hasAttr(kNativeCarried);
}

/// A capture parameter that receives a shared cell: an entry-block argument of
/// a ctjs.func whose `ctnative.cell_args` lists its number.
bool admission::isCellParameter(mlir::Value v) {
    auto arg = llvm::dyn_cast<mlir::BlockArgument>(v);
    if (!arg || !arg.getOwner()->isEntryBlock()) { return false; }
    auto fn = llvm::dyn_cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
    return fn && isCellArg(fn.getOperation(), arg.getArgNumber());
}

/// Where a shared binding is reached from: the variable in this frame, or the
/// pointer a lifted call handed this one. Both are lvalues after lowering, and
/// every rule that asks about a cell accepts either.
bool admission::namesASharedCell(mlir::Value v) {
    return isCarriedCell(v.getDefiningOp()) || isCellParameter(v);
}

bool admission::isCIdentifier(llvm::StringRef key) {
    if (key.empty() || (key.front() >= '0' && key.front() <= '9')) { return false; }
    return llvm::all_of(key, cpp::is_identifier_char);
}

/// A key that is a valid C identifier but still cannot be a struct field name.
/// Field names are emitted verbatim (cIdentifier() sanitises symbols, not
/// members), so `o.class = 3` would emit `double class;` and `o.NAN = 5`
/// `double NAN;`, which clashes with the macro from the generated file's
/// `#include <cmath>`. Both fail to compile. Such keys are refused rather than
/// mangled so generated fields keep the JavaScript name.
bool admission::isReservedInCpp(llvm::StringRef key) {
    static constexpr llvm::StringLiteral kReserved[] = {
        // Keywords a member may not be named.
        "alignas",
        "alignof",
        "and",
        "and_eq",
        "asm",
        "auto",
        "bitand",
        "bitor",
        "bool",
        "break",
        "case",
        "catch",
        "char",
        "char8_t",
        "char16_t",
        "char32_t",
        "class",
        "compl",
        "concept",
        "const",
        "consteval",
        "constexpr",
        "constinit",
        "const_cast",
        "continue",
        "co_await",
        "co_return",
        "co_yield",
        "decltype",
        "default",
        "delete",
        "do",
        "double",
        "dynamic_cast",
        "else",
        "enum",
        "explicit",
        "export",
        "extern",
        "false",
        "float",
        "for",
        "friend",
        "goto",
        "if",
        "inline",
        "int",
        "long",
        "mutable",
        "namespace",
        "new",
        "noexcept",
        "not",
        "not_eq",
        "nullptr",
        "operator",
        "or",
        "or_eq",
        "private",
        "protected",
        "public",
        "register",
        "reinterpret_cast",
        "requires",
        "return",
        "short",
        "signed",
        "sizeof",
        "static",
        "static_assert",
        "static_cast",
        "struct",
        "switch",
        "template",
        "this",
        "thread_local",
        "throw",
        "true",
        "try",
        "typedef",
        "typeid",
        "typename",
        "union",
        "unsigned",
        "using",
        "virtual",
        "void",
        "volatile",
        "wchar_t",
        "while",
        "xor",
        "xor_eq",
        // Macros that <cmath> and <cstdio>, included by the generated C++, may define.
        "NAN",
        "INFINITY",
        "HUGE_VAL",
        "HUGE_VALF",
        "HUGE_VALL",
        "EOF",
        "NULL",
        "BUFSIZ",
        "FILENAME_MAX",
        "FOPEN_MAX",
        "L_tmpnam",
        "TMP_MAX",
        "SEEK_SET",
        "SEEK_CUR",
        "SEEK_END",
        "stdin",
        "stdout",
        "stderr",
        "errno",
        "MATH_ERRNO",
        "MATH_ERREXCEPT",
        "FP_FAST_FMA",
        "FP_INFINITE",
        "FP_NAN",
        "FP_NORMAL",
        "FP_SUBNORMAL",
        "FP_ZERO",
        "FP_ILOGB0",
        "FP_ILOGBNAN",
    };
    return llvm::is_contained(kReserved, key);
}

/// Names that Object.prototype answers for. A field that is only ever read is
/// `undefined` for a plain key, which native lowering preserves with a tag, but
/// these names are not undefined: the literal's prototype answers them, and the
/// interpreter finds a function where the generated struct would find
/// undefined (`if (o.constructor)` would take different branches). A key that
/// is stored shadows the inherited one and is fine; only a read-only key is
/// refused.
bool admission::namesObjectPrototypeMember(llvm::StringRef key) {
    static constexpr llvm::StringLiteral kInherited[] = {
        "constructor",      "hasOwnProperty",   "isPrototypeOf",    "propertyIsEnumerable",
        "toLocaleString",   "toString",         "valueOf",          "__proto__",
        "__defineGetter__", "__defineSetter__", "__lookupGetter__", "__lookupSetter__",
    };
    return llvm::is_contained(kInherited, key);
}

bool admission::stringConstantUsedOnlyAs(mlir::Operation * o,
                                         llvm::function_ref<bool(mlir::OpOperand &)> key) {
    auto constant = llvm::dyn_cast_or_null<ctjs::ConstantOp>(o);
    return constant && llvm::isa<ctjs::StringAttr>(constant.getValue()) &&
           !constant.getResult().use_empty() && llvm::all_of(constant.getResult().getUses(), key);
}

/// A string constant whose every use is a property key of a closed object
/// lowers to nothing: the key becomes a member name.
bool admission::isKeyOnlyString(mlir::Operation * o) {
    return stringConstantUsedOnlyAs(o, [](mlir::OpOperand & use) {
        mlir::Operation * user = use.getOwner();
        return llvm::isa<ctjs::GetPropertyOp, ctjs::SetPropertyOp>(user) &&
               use.getOperandNumber() == 1 &&
               (isClosedObject(user->getOperand(0)) || nativeObjectFieldGroup(user) >= 0);
    });
}

/// A dense array lowers to a `std::vector<double>` held by value.
/// TypeInference::isDenseVectorSite proves local lifetime and density for
/// appends, reads and complete current own-element overwrites.
bool admission::isVectorSite(mlir::Value v) {
    return TypeInference::isDenseVectorSite(v);
}

/// Why an array literal is not a dense vector: the first use that is not an
/// append or a read, asked after the complete density query failed. A write
/// can fail its current contents proof even when it looks locally bounded.
std::string admission::whyNotDense(mlir::Value array) {
    for (mlir::OpOperand & use : array.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            if (use.getOperandNumber() == 0) {
                const llvm::StringRef key = ctjs::constantKey(set.getKey());
                if (key == "length") {
                    return "an array literal whose assigned `length` lacks a complete "
                           "non-growing contents proof";
                }
                if (key.empty()) {
                    return "an array literal written through an index - `a[100] = 1` gives "
                           "`length` 101 with one element, so density is not proved";
                }
                return ("an array literal given the named property `" + key + "`").str();
            }
        }
        if (llvm::isa<ctjs::DeletePropertyOp, ctjs::DeleteNamedOp>(user)) {
            return "an array literal with an element deleted - `delete a[0]` punches a hole "
                   "in it, so density is not proved";
        }
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
            if (use.getOperandNumber() == 0) {
                const llvm::StringRef key = ctjs::constantKey(get.getKey());
                if (key.empty() || key == "length") { continue; }
                return ("an array literal read through the named property `" + key + "`").str();
            }
        }
        if (llvm::isa<ctjs::AppendOp>(user) && use.getOperandNumber() == 0) { continue; }
        if (auto why = escapeRoute("an array literal", use)) { return *why; }
        return escapesTo("an array literal", user);
    }
    return "an array literal that is not a dense vector";
}

/// A string constant whose every use is the `length` key of a dense array
/// lowers to nothing: reads become the size helper and shrinks become resize.
///
/// The object-key predicate requires isClosedObject, which is false for
/// an array. Recognizing array keys separately keeps an erased `length`
/// name from requiring a runtime string carrier or its header.
///
/// The same holds for the keys of an element vector's `length` and its method
/// reads, a String's `split` read, and the pattern and flags of the RegExp
/// literal split(/\s+/) takes: each lowers to a helper call that needs no
/// text.
bool admission::isVectorKeyString(mlir::Operation * o) {
    return stringConstantUsedOnlyAs(o, [](mlir::OpOperand & use) {
        mlir::Operation * user = use.getOwner();
        if (isVectorCallPlumbing(user)) { return true; }
        const bool key = llvm::isa<ctjs::GetPropertyOp, ctjs::SetPropertyOp>(user) &&
                         use.getOperandNumber() == 1;
        if (!key) { return false; }
        if (isVectorSite(user->getOperand(0))) { return true; }
        return llvm::isa<ctjs::GetPropertyOp>(user) && ctjs::constantKey(use.get()) == "length" &&
               !TypeInference::elementVectorAliases(user->getOperand(0)).empty();
    });
}

/// A value that lowers to nothing needs no carrier, and these are the only
/// ones: the three implicit arguments (erased once their declaration closures
/// are gone), a declaration or lifted closure's result, an unboxed cell, map
/// bookkeeping, the lift's poison (replaced by NaN), a key constant (a member
/// name), a load_global that only names a direct call's callee, and one of
/// `document` or `window` that only its page read uses, and a page method read
/// only to be called. function()
/// exempts this list from the carrier check; retype() asks the same question.
bool admission::lowersToNothing(mlir::Value v) {
    if (auto arg = llvm::dyn_cast<mlir::BlockArgument>(v)) {
        return arg.getOwner()->isEntryBlock() &&
               llvm::isa<ctjs::FuncOp>(arg.getOwner()->getParentOp()) && arg.getArgNumber() < 3;
    }
    mlir::Operation * o = v.getDefiningOp();
    if (isNativeMapBookkeeping(o)) { return true; }
    if (isDeclarationClosure(o) || isKeyOnlyString(o) || isVectorKeyString(o)) { return true; }
    if (isLiftedClosure(o) || isUnboxedCell(o)) { return true; }
    if (isVectorCallPlumbing(o) || isPageGlobal(o) || isPageMethodRead(o)) { return true; }
    if (o->getName().getStringRef() == "ub.poison") { return true; }
    return llvm::isa<ctjs::LoadGlobalOp>(o) && feedsOnlyDirectCallees(v);
}

} // namespace ctcompile::ctnative::lowering_detail
