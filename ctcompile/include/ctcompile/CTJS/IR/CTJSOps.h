#pragma once
// Every CTJS operation. None yet - Phase 8 defines them.
#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Interfaces/InferTypeOpInterface.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/StringSwitch.h"

#include <optional>

#include "ctcompile/CTJS/IR/CTJSAttrs.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSEnums.h"
#include "ctcompile/CTJS/IR/CTJSInterfaces.h"
#include "ctcompile/CTJS/IR/CTJSTraits.h"
#include "ctcompile/CTJS/IR/CTJSTypes.h"

#define GET_OP_CLASSES
#include "ctcompile/CTJS/IR/CTJSOps.h.inc"

namespace ctcompile::ctjs {
// THE IMPLICIT ARGUMENTS THE IMPORTER PREPENDS to every ctjs.func's entry block
// - receiver, new.target, callee - ahead of the declared parameters, and what
// every ctjs.call_direct passes in the same positions (BytecodeImport.cpp).
inline constexpr unsigned arg_receiver = 0;
inline constexpr unsigned arg_new_target = 1;
inline constexpr unsigned arg_callee = 2;
inline constexpr unsigned implicit_arguments = 3;

// An object literal's own data property is op::define_data - DEFINED
// (CreateDataPropertyOrThrow), not assigned - and the importer makes it the
// ctjs.set_property every native proof already reads, carrying this marker.
// The boxed backend lowers a marked store to ct_aot_define_data: assignment
// would run an inherited setter and set the prototype for "__proto__".
inline constexpr llvm::StringLiteral defineOwnMarker = "ctjs.define_own";

// The function index the importer put after the last `$` of the symbol - the
// only link between a `ctjs.create_closure`'s `$function` attribute and the
// `ctjs.func` it names. Every tier reads it this way.
inline std::optional<unsigned> functionIndex(FuncOp function) {
    const llvm::StringRef name = function.getSymName();
    const std::size_t dollar = name.rfind('$');
    if (dollar == llvm::StringRef::npos) { return std::nullopt; }
    unsigned index = 0;
    if (name.substr(dollar + 1).getAsInteger(10, index)) { return std::nullopt; }
    return index;
}

// The attribute of kind A a ctjs.constant operand carries, or null - for a
// value no constant defines and a constant of another kind alike.
template <class A> A constantAttr(mlir::Value value) {
    auto constant = value.getDefiningOp<ConstantOp>();
    return constant ? llvm::dyn_cast<A>(constant.getValue()) : A{};
}

inline bool isUndefinedConstant(mlir::Value value) {
    return static_cast<bool>(constantAttr<UndefinedAttr>(value));
}

// The key operand of a get_property or set_property, or null for any other op.
inline mlir::Value propertyKey(mlir::Operation * op) {
    if (auto get = llvm::dyn_cast<GetPropertyOp>(op)) { return get.getKey(); }
    if (auto set = llvm::dyn_cast<SetPropertyOp>(op)) { return set.getKey(); }
    return {};
}

// The constant string a property key operand carries, or empty - for a
// non-constant key, a constant that is not a string, and `""` alike.
inline llvm::StringRef constantKey(mlir::Value key) {
    auto text = constantAttr<StringAttr>(key);
    return text ? text.getValue() : llvm::StringRef{};
}

// The prototype hooks and the members every object inherits from
// Object.prototype - the names an own-data slot cannot be.
inline bool reservedKey(llvm::StringRef key) {
    return llvm::StringSwitch<bool>(key)
        .Cases({"__proto__", "prototype", "constructor"}, true)
        .Cases({"toString", "valueOf", "toLocaleString"}, true)
        .Cases({"hasOwnProperty", "isPrototypeOf", "propertyIsEnumerable"}, true)
        .Cases({"__defineGetter__", "__defineSetter__"}, true)
        .Cases({"__lookupGetter__", "__lookupSetter__"}, true)
        .Default(false);
}

// A constant own-data key BY NAME, for callers that already hold the text:
// empty is "no constant key" here, because that is what constantKey returns
// for one.
inline bool ordinaryKey(llvm::StringRef key) {
    return !key.empty() && !reservedKey(key);
}

// A constant own-data key BY OPERAND. `o[""] = 1` is an own property like any
// other and the object-field analysis names it, so the empty string is
// ordinary HERE and only here - the by-name overload cannot tell it from a
// dynamic key. (The unification that dropped this distinction refused the
// object-fields fixture.)
inline bool ordinaryKey(mlir::Value key) {
    auto text = constantAttr<StringAttr>(key);
    return text && !reservedKey(text.getValue());
}

// Truthy, unary Not/TypeOf/Void and StrictEq: total on any operand, and none
// converts an object or calls user code.
inline bool nonCoercing(mlir::Operation * op) {
    if (auto unary = llvm::dyn_cast<UnaryOp>(op)) {
        return unary.getKind() == UnaryKind::Not || unary.getKind() == UnaryKind::TypeOf ||
               unary.getKind() == UnaryKind::Void;
    }
    auto compare = llvm::dyn_cast<CompareOp>(op);
    return llvm::isa<TruthyOp>(op) || (compare && compare.getKind() == CompareKind::StrictEq);
}

// The first user of `function`'s own closure (its callee argument) that is not
// a nested closure's enclosing operand or an upvalue access through it, or
// null when it feeds nothing else. The entry block must exist.
inline mlir::Operation * ownClosureEscape(FuncOp function) {
    for (mlir::OpOperand & use : function.getBody().front().getArgument(arg_callee).getUses()) {
        if (use.getOperandNumber() != 0 ||
            !llvm::isa<CreateClosureOp, LoadUpvalueOp, StoreUpvalueOp>(use.getOwner())) {
            return use.getOwner();
        }
    }
    return nullptr;
}

// The one ctjs.func under `scope` with function index `index`, or null when
// none or several have it.
inline FuncOp uniqueFunction(mlir::Operation * scope, unsigned index) {
    FuncOp found;
    unsigned matches = 0;
    scope->walk([&](FuncOp function) {
        if (functionIndex(function) == index) {
            found = function;
            ++matches;
        }
    });
    return matches == 1 ? found : FuncOp{};
}

// The one ctjs.store_global of `name` under `scope`, or null when none or
// several store it.
inline StoreGlobalOp uniqueGlobalStore(mlir::Operation * scope, llvm::StringRef name) {
    StoreGlobalOp found;
    unsigned matches = 0;
    scope->walk([&](StoreGlobalOp store) {
        if (store.getName() == name) {
            found = store;
            ++matches;
        }
    });
    return matches == 1 ? found : StoreGlobalOp{};
}
} // namespace ctcompile::ctjs
