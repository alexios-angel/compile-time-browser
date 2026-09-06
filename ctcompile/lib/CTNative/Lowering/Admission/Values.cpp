// Admission/Values.cpp - native lowering implementation.
#include "Admission.h"

namespace ctcompile::ctnative::lowering_detail {

mlir::Type admission::typeOf(mlir::Value v) const {
    const TypeLattice * lattice = solver.lookupState<TypeLattice>(v);
    return lattice == nullptr ? mlir::Type{} : lattice->getValue().getType();
}

bool admission::refuse(std::string reason) {
    if (why.empty()) { why = std::move(reason); }
    return false;
}

// Scalar operands have an exact numeric conversion; equality keeps their tags.
bool admission::numeric(mlir::Value v, llvm::StringRef where) {
    if (!isScalarCarrier(carrierOf(typeOf(v)))) {
        return refuse((where + " operand is " + printed(typeOf(v)) + ", not a number").str());
    }
    return true;
}

// A value used as a BOOLEAN.
bool admission::boolean(mlir::Value v, llvm::StringRef where) {
    if (carrierOf(typeOf(v)) != carrier::boolean) {
        return refuse((where + " operand is " + printed(typeOf(v)) + ", not a boolean").str());
    }
    return true;
}

// Global observations still use the numeric printing convention. Internal
// optional scalars are exact, but that does not add optional-global output.
bool admission::printable(mlir::Value v, llvm::StringRef where) {
    if (mayBeUndefined(typeOf(v))) {
        return refuse((where + " may be null or undefined; native global observations require "
                               "a definite number")
                          .str());
    }
    if (!llvm::isa<NumType>(typeOf(v))) {
        return refuse((where + " is " + printed(typeOf(v)) +
                       "; native global observations require a definite number")
                          .str());
    }
    return true;
}

// A FUNCTION DECLARATION IS A BINDING, NOT A VALUE. `function f() {}` at
// any level is `create_closure` whose only use is `store_global "f"`; in
// the closed world the function exists as an emitc.func and calls of it
// are direct, so the pair lowers to nothing. A closure used as a VALUE -
// passed, returned, stored anywhere else - is not native yet (Phase 59).
bool admission::isDeclarationClosure(mlir::Operation * o) {
    auto closure = llvm::dyn_cast_or_null<ctjs::CreateClosureOp>(o);
    if (!closure || !closure.getResult().hasOneUse()) { return false; }
    return llvm::isa<ctjs::StoreGlobalOp>(*closure.getResult().getUsers().begin());
}

bool admission::isDeclarationStore(mlir::Operation * o) {
    auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(o);
    return store && isDeclarationClosure(store.getValue().getDefiningOp());
}

// PHASE 59 SLICE 1. The lift above already moved this closure's captures
// to the call sites and rewrote every call to a ctjs.call_direct; what is
// left of it is the `$callee_value` operand, which the call arm drops -
// so, exactly like a declaration closure, it lowers to nothing. The cell
// it captured is the same story one step down: the box was proved
// constant, every read of it is already the value, and what remains is the
// capture operand of a closure that is about to go.
bool admission::isLiftedClosure(mlir::Operation * o) {
    return llvm::isa_and_nonnull<ctjs::CreateClosureOp>(o) && o->hasAttr("ctnative.lifted");
}

bool admission::isUnboxedCell(mlir::Operation * o) {
    return llvm::isa_and_nonnull<ctjs::CreateCellOp>(o) && o->hasAttr("ctnative.unboxed");
}

bool admission::closureLowersToNothing(mlir::Operation * o) {
    return isDeclarationClosure(o) || isLiftedClosure(o);
}

// WHY THIS CLOSURE IS NOT ONE OF THOSE, in the words the lift wrote onto
// it. Spelled once because it is asked in two places that used to give
// different answers: at the operation, and at the `%arg2` operand it takes
// - and the operand's answer was "uses its own closure", which is a
// sentence about the ENCLOSING function reading a value it never reads.
std::string admission::closureRefusal(mlir::Operation * o) {
    const llvm::StringRef what =
        o->hasAttr("ctnative.method_refusal") ? "a method field" : "a closure used as a value";
    auto why = o->getAttrOfType<mlir::StringAttr>("ctnative.closure_reason");
    return why ? (what + ": " + why.getValue()).str() : (what + " - Phase 59").str();
}

// THE CALLEE VALUE OF A DIRECT CALL lowers to nothing: the call names its
// function by symbol, and the boxed closure the interpreter would have
// called through is only carried so the boxed tier can still lower the
// same op. A load_global whose every use is that operand is exempt from
// the carrier check for exactly that reason.
bool admission::feedsOnlyDirectCallees(mlir::Value v) {
    if (v.use_empty()) { return false; }
    for (mlir::OpOperand & use : v.getUses()) {
        auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
        if (!call || use.getOperandNumber() != 2) { return false; }
    }
    return true;
}

// THE ENTRY-BLOCK INDICES THAT CARRY A `ctn_x *`, off a ctjs.func or off
// one of its ctjs.call_directs - the same list on both, which is what lets
// the caller and the callee be lowered in either order.
llvm::ArrayRef<int32_t> admission::objectArgsOf(mlir::Operation * o) {
    auto listed = o->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.object_args");
    return listed ? listed.asArrayRef() : llvm::ArrayRef<int32_t>{};
}

bool admission::isObjectArg(mlir::Operation * o, unsigned index) {
    return llvm::is_contained(objectArgsOf(o), static_cast<int32_t>(index));
}

// AND THE ONES THAT CARRY A `double *` - PHASE 59 SLICE 2 STEP 2. Same
// shape, same two places, same reason: the shared binding's box is a
// variable in the CALLER's frame and the callee reads and writes it
// through a pointer.
llvm::ArrayRef<int32_t> admission::cellArgsOf(mlir::Operation * o) {
    auto listed = o->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.cell_args");
    return listed ? listed.asArrayRef() : llvm::ArrayRef<int32_t>{};
}

bool admission::isCellArg(mlir::Operation * o, unsigned index) {
    return llvm::is_contained(cellArgsOf(o), static_cast<int32_t>(index));
}

// A `ctjs.create_cell` the lift made a frame-local variable.
bool admission::isCarriedCell(mlir::Operation * o) {
    return llvm::isa_and_nonnull<ctjs::CreateCellOp>(o) && o->hasAttr("ctnative.carried");
}

// A capture parameter that arrived as one - an entry-block argument of a
// ctjs.func whose `ctnative.cell_args` lists its number.
bool admission::isCellParameter(mlir::Value v) {
    auto arg = llvm::dyn_cast<mlir::BlockArgument>(v);
    if (!arg || !arg.getOwner()->isEntryBlock()) { return false; }
    auto fn = llvm::dyn_cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
    return fn && isCellArg(fn.getOperation(), arg.getArgNumber());
}

// WHERE A SHARED BINDING IS REACHED FROM: the variable in this frame, or
// the pointer a lifted call handed this one. Both are lvalues after
// lowering, and every rule below that asks about a cell asks about either.
bool admission::namesASharedCell(mlir::Value v) {
    return isCarriedCell(v.getDefiningOp()) || isCellParameter(v);
}

llvm::StringRef admission::keyOf(mlir::Value key) {
    auto constant = key.getDefiningOp<ctjs::ConstantOp>();
    if (!constant) { return {}; }
    auto str = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue());
    return str ? str.getValue() : llvm::StringRef{};
}

bool admission::isCIdentifier(llvm::StringRef key) {
    if (key.empty() || std::isdigit(static_cast<unsigned char>(key.front()))) { return false; }
    return llvm::all_of(
        key, [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; });
}

// A KEY THAT IS A C IDENTIFIER AND STILL CANNOT BE A FIELD NAME. Field
// names are emitted verbatim - `cIdentifier()` sanitises symbols, not
// members - so `o.class = 3` emitted `double class;` and `o.NAN = 5`
// emitted `double NAN;` under this file's own `#include <cmath>`. Both
// were admitted with no refusal and both are hard -Werror build failures
// on a program the tier had declared native. Refused rather than mangled:
// a generated field keeps the JavaScript name a reader is looking for, and
// mangling every field to buy this rare case is a trade Phase 56C should
// make deliberately, not this fix.
bool admission::isReservedInCpp(llvm::StringRef key) {
    static constexpr llvm::StringLiteral kReserved[] = {
        // keywords a member may not be named
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
        // macros the two headers this file emits are allowed to define
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

// NAMES OBJECT.PROTOTYPE ANSWERS FOR. A field that is only ever READ is
// `undefined` for a plain key, which this tier preserves with a tag - but these
// names are not undefined: the literal's prototype answers them, and the
// interpreter finds a function where the generated struct finds undefined. So
// `if (o.constructor)` took the else branch natively and the then branch
// in the interpreter, with no refusal anywhere. A key that IS stored
// shadows the inherited one and is fine; only a read-only key is refused.
bool admission::namesObjectPrototypeMember(llvm::StringRef key) {
    static constexpr llvm::StringLiteral kInherited[] = {
        "constructor",      "hasOwnProperty",   "isPrototypeOf",    "propertyIsEnumerable",
        "toLocaleString",   "toString",         "valueOf",          "__proto__",
        "__defineGetter__", "__defineSetter__", "__lookupGetter__", "__lookupSetter__",
    };
    return llvm::is_contained(kInherited, key);
}

// A string constant whose every use is a property key of a closed object
// lowers to nothing: the key becomes a member name.
bool admission::isKeyOnlyString(mlir::Operation * o) {
    auto constant = llvm::dyn_cast<ctjs::ConstantOp>(o);
    if (!constant || !llvm::isa<ctjs::StringAttr>(constant.getValue()) ||
        constant.getResult().use_empty()) {
        return false;
    }
    for (mlir::OpOperand & use : constant.getResult().getUses()) {
        mlir::Operation * user = use.getOwner();
        mlir::Value object;
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
            object = get.getObject();
        } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            object = set.getObject();
        }
        if (!object || use.getOperandNumber() != 1 ||
            (!isClosedObject(object) && nativeObjectFieldGroup(user) < 0)) {
            return false;
        }
    }
    return true;
}

// PHASE 57A: A DENSE ARRAY IS A `std::vector<double>` BY VALUE.
// TypeInference::isDenseVectorSite is the proof - every use is an append
// onto it or a read of an index or of `length`, so nothing can make it
// sparse, nothing renames an element, and it never leaves the frame.
bool admission::isVectorSite(mlir::Value v) {
    return TypeInference::isDenseVectorSite(v);
}

// WHY AN ARRAY LITERAL IS NOT A DENSE VECTOR: the first use that is not an
// append or a read, named by what it is. The two sparsity routes come
// first, because they are the ones part 24 Stage 57A names by hand and the
// ones a reader will not expect to be refused.
std::string admission::whyNotDense(mlir::Value array) {
    for (mlir::OpOperand & use : array.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            if (use.getOperandNumber() == 0) {
                const llvm::StringRef key = keyOf(set.getKey());
                if (key == "length") {
                    return "an array literal whose `length` is assigned - that resizes it, "
                           "and a resize leaves holes no `std::vector` can hold";
                }
                if (key.empty()) {
                    return "an array literal written through an index - `a[100] = 1` gives "
                           "`length` 101 with one element, so density is not proved";
                }
                return ("an array literal given the named property `" + key + "`").str();
            }
            if (use.getOperandNumber() == 2) {
                return "an array literal that escapes - it is stored into another object";
            }
        }
        if (llvm::isa<ctjs::DeletePropertyOp, ctjs::DeleteNamedOp>(user)) {
            return "an array literal with an element deleted - `delete a[0]` punches a hole "
                   "in it, so density is not proved";
        }
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
            if (use.getOperandNumber() == 0) {
                const llvm::StringRef key = keyOf(get.getKey());
                if (key.empty() || key == "length") { continue; }
                return ("an array literal read through the named property `" + key + "`").str();
            }
        }
        if (llvm::isa<ctjs::AppendOp>(user) && use.getOperandNumber() == 0) { continue; }
        if (llvm::isa<mlir::scf::WhileOp, mlir::scf::YieldOp, mlir::scf::ConditionOp>(user)) {
            return "an array literal that is loop-carried - more than one value reaches the "
                   "variable that holds it (assigned again inside a loop, or on only one "
                   "path before it)";
        }
        if (llvm::isa<ctjs::ReturnOp>(user)) {
            return "an array literal that escapes - it is returned";
        }
        return ("an array literal that escapes - it reaches `" + user->getName().getStringRef() +
                "`")
            .str();
    }
    return "an array literal that is not a dense vector";
}

// A string constant whose every use is the `length` key of a dense array
// lowers to nothing: the read becomes a call to the size helper.
//
// The object-key predicate requires isClosedObject, which is false for
// an array. Recognizing array keys separately keeps an erased `length`
// name from requiring a runtime string carrier or its header.
bool admission::isVectorKeyString(mlir::Operation * o) {
    auto constant = llvm::dyn_cast_or_null<ctjs::ConstantOp>(o);
    if (!constant || !llvm::isa<ctjs::StringAttr>(constant.getValue()) ||
        constant.getResult().use_empty()) {
        return false;
    }
    for (mlir::OpOperand & use : constant.getResult().getUses()) {
        auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
        if (!get || use.getOperandNumber() != 1 || !isVectorSite(get.getObject())) { return false; }
    }
    return true;
}

// A VALUE THAT LOWERS TO NOTHING NEEDS NO CARRIER, and these are the only
// ones: the three implicit arguments (erased once their declaration
// closures are gone), a declaration closure's result, the lift's poison
// (replaced by NaN), a key constant (a member name) and a load_global
// that only names a direct call's callee. function() exempts exactly this
// list from the carrier check; retype() asks the same question.
bool admission::lowersToNothing(mlir::Value v) {
    if (auto arg = llvm::dyn_cast<mlir::BlockArgument>(v)) {
        return arg.getOwner()->isEntryBlock() &&
               llvm::isa<ctjs::FuncOp>(arg.getOwner()->getParentOp()) && arg.getArgNumber() < 3;
    }
    mlir::Operation * o = v.getDefiningOp();
    if (isNativeMapBookkeeping(o)) { return true; }
    if (isDeclarationClosure(o) || isKeyOnlyString(o) || isVectorKeyString(o)) { return true; }
    if (isLiftedClosure(o) || isUnboxedCell(o)) { return true; }
    if (o->getName().getStringRef() == "ub.poison") { return true; }
    return llvm::isa<ctjs::LoadGlobalOp>(o) && feedsOnlyDirectCallees(v);
}

} // namespace ctcompile::ctnative::lowering_detail
