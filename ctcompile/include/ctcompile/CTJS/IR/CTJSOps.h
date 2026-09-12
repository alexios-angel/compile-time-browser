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

// The constant string a property key operand carries, or empty - for a
// non-constant key, a constant that is not a string, and `""` alike.
inline llvm::StringRef constantKey(mlir::Value key) {
    auto constant = key.getDefiningOp<ConstantOp>();
    if (!constant) { return {}; }
    auto text = llvm::dyn_cast<StringAttr>(constant.getValue());
    return text ? text.getValue() : llvm::StringRef{};
}

// A constant own-data key: not the prototype hooks and not the members every
// object inherits from Object.prototype.
inline bool ordinaryKey(llvm::StringRef key) {
    return !key.empty() &&
           !llvm::StringSwitch<bool>(key)
                .Cases({"__proto__", "prototype", "constructor"}, true)
                .Cases({"toString", "valueOf", "toLocaleString"}, true)
                .Cases({"hasOwnProperty", "isPrototypeOf", "propertyIsEnumerable"}, true)
                .Cases({"__defineGetter__", "__defineSetter__"}, true)
                .Cases({"__lookupGetter__", "__lookupSetter__"}, true)
                .Default(false);
}
} // namespace ctcompile::ctjs
