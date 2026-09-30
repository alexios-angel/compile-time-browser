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
#include "llvm/ADT/bit.h"

#include <cstdint>
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

// The entry block of `function` when it has one carrying the implicit
// arguments, or null.
inline mlir::Block * implicitEntry(FuncOp function) {
    if (function.getBody().empty()) { return nullptr; }
    mlir::Block & entry = function.getBody().front();
    return entry.getNumArguments() >= implicit_arguments ? &entry : nullptr;
}

// The declared parameters of an entry block implicitEntry returned.
inline unsigned parameterCount(mlir::Block & entry) {
    return entry.getNumArguments() - implicit_arguments;
}

// DOES THIS BODY READ ITS RAW ARGUMENT WINDOW? `arguments` and a rest
// parameter copy the frame's registers - every value the site passed, not the
// declared ones - so padding a short call is visible to such a body.
inline bool readsRawArguments(FuncOp function) {
    return function
        .walk([](mlir::Operation * op) {
            return llvm::isa<MakeArgumentsOp, GatherRestOp>(op) ? mlir::WalkResult::interrupt()
                                                                : mlir::WalkResult::advance();
        })
        .wasInterrupted();
}

// An object literal's own data property is op::define_data - DEFINED
// (CreateDataPropertyOrThrow), not assigned - and the importer makes it the
// ctjs.set_property every native proof already reads, carrying this marker.
// The boxed backend lowers a marked store to ct_aot_define_data: assignment
// would run an inherited setter and set the prototype for "__proto__".
inline constexpr llvm::StringLiteral defineOwnMarker = "ctjs.define_own";

// A ctjs.func the EmitC admission refused carries this, with the reason.
inline constexpr llvm::StringLiteral notLoweredMarker = "ctjs.not_lowered";

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

// The function index a create_closure names, or none for a negative one.
inline std::optional<unsigned> closureFunction(CreateClosureOp made) {
    if (made.getFunction() < 0) { return std::nullopt; }
    return static_cast<unsigned>(made.getFunction());
}

// The attribute of kind A a ctjs.constant operand carries, or null - for a
// value no constant defines and a constant of another kind alike.
template <class A> A constantAttr(mlir::Value value) {
    auto constant = value.getDefiningOp<ConstantOp>();
    return constant ? llvm::dyn_cast<A>(constant.getValue()) : A{};
}

// One of the attributes a ctjs.constant may carry (ConstantOp::verify).
inline bool isConstantAttr(mlir::Attribute value) {
    return llvm::isa<UndefinedAttr, NullAttr, BooleanAttr, NumberAttr, StringAttr, BigIntAttr>(
        value);
}

// A #ctjs.number for `value`. The attribute stores the double's BITS, so the
// uint64_t builder is never the one to call with a count.
inline NumberAttr numberAttr(mlir::MLIRContext * context, double value) {
    return NumberAttr::get(context, llvm::bit_cast<std::uint64_t>(value));
}

// The string attribute `name` on `op`, or empty.
inline llvm::StringRef stringAttrOf(mlir::Operation * op, llvm::StringRef name) {
    auto text = op->getAttrOfType<mlir::StringAttr>(name);
    return text ? text.getValue() : llvm::StringRef{};
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

// The get_property a method call reads its callee from, or null.
inline GetPropertyOp receiverMethod(CallOp call) {
    return call.getCallee().getDefiningOp<GetPropertyOp>();
}

// The constant name of the method `call` invokes (`o.set(...)` is "set"), or
// empty when the callee is not a constant-keyed get_property.
inline llvm::StringRef methodKey(CallOp call) {
    auto read = receiverMethod(call);
    return read ? constantKey(read.getKey()) : llvm::StringRef{};
}

// The global a call's callee was loaded from (`f(...)` after `load_global
// "f"`), or empty. For any op with a callee VALUE: CallOp, ConstructOp.
template <class Call> llvm::StringRef calleeGlobalName(Call call) {
    auto load = call.getCallee().template getDefiningOp<LoadGlobalOp>();
    return load ? load.getName() : llvm::StringRef{};
}

// A ctjs.call or ctjs.call_direct read the same way: the callee value (the
// direct call's callee_value), the receiver and the declared arguments.
struct CallView {
    mlir::Value callee;
    mlir::Value receiver;
    mlir::OperandRange args;
};

inline std::optional<CallView> callView(mlir::Operation * op) {
    if (auto call = llvm::dyn_cast<CallOp>(op)) {
        return CallView{call.getCallee(), call.getReceiver(), call.getArgs()};
    }
    if (auto direct = llvm::dyn_cast<CallDirectOp>(op)) {
        return CallView{direct.getCalleeValue(), direct.getReceiver(), direct.getArgs()};
    }
    return std::nullopt;
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

// The one OpT under `scope` satisfying `matches`, or null when none or several
// do.
template <class OpT, class Pred> OpT uniqueOp(mlir::Operation * scope, Pred matches) {
    OpT found;
    unsigned count = 0;
    scope->walk([&](OpT op) {
        if (matches(op)) {
            found = op;
            ++count;
        }
    });
    return count == 1 ? found : OpT{};
}

// The one ctjs.func under `scope` with function index `index`, or null when
// none or several have it.
inline FuncOp uniqueFunction(mlir::Operation * scope, unsigned index) {
    return uniqueOp<FuncOp>(scope,
                            [&](FuncOp function) { return functionIndex(function) == index; });
}

// The one ctjs.store_global of `name` under `scope`, or null when none or
// several store it.
inline StoreGlobalOp uniqueGlobalStore(mlir::Operation * scope, llvm::StringRef name) {
    return uniqueOp<StoreGlobalOp>(scope,
                                   [&](StoreGlobalOp store) { return store.getName() == name; });
}
} // namespace ctcompile::ctjs
