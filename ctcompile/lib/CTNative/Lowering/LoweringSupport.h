#pragma once

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/NativeClosure.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "ctcompile/CTNative/Analysis/NativeObjectIdentity.h"
#include "ctcompile/CTNative/Analysis/PrimitiveAlternatives.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Rewrite/FrozenRewritePatternSet.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ctcompile::ctnative {
class DOMEntryAnalysis;
}

namespace ctcompile::ctnative::lowering_detail {

namespace ec = mlir::emitc;
enum class carrier {
    none,
    boolean,
    symbol,
    number,
    nullable,
    string,
    nullableString,
    booleanString,
    nullableBooleanString,
    numberString,
    nullableNumberString,
    map,
    closure,
    methodTable,
    objectIdentity,
    objectValue,
    domElement,
    domElementVector,
    json,
    structure,
    vector,
    stringVector
};
inline constexpr llvm::StringLiteral kVectorType = "std::vector<double>";
inline constexpr llvm::StringLiteral kStringVectorType = "std::vector<std::string>";
inline constexpr llvm::StringLiteral kStringType = "ctnative::js_string";
inline constexpr llvm::StringLiteral kRawStringType = "std::string";
inline constexpr llvm::StringLiteral kNumberType = "ctnative::js_num";
inline constexpr llvm::StringLiteral kBooleanType = "ctnative::js_boolean_t";
inline constexpr llvm::StringLiteral kSymbolType = "ctnative::js_symbol_t";
inline constexpr llvm::StringLiteral kBooleanStringType = "ctnative::boolean_string";
inline constexpr llvm::StringLiteral kNullableBooleanStringType =
    "ctnative::nullable_boolean_string";
inline constexpr llvm::StringLiteral kBooleanStringMapType =
    "std::variant<ctnative::js_boolean_t, std::string>";
inline constexpr llvm::StringLiteral kNumberStringType = "ctnative::number_string";
inline constexpr llvm::StringLiteral kNullableNumberStringType = "ctnative::nullable_number_string";
inline constexpr llvm::StringLiteral kNullableStringType = "ctnative::nullable_string";
inline constexpr llvm::StringLiteral kNullableType = "ctnative::nullable_scalar";
inline constexpr llvm::StringLiteral kObjectValueType = "ctnative::object_value";
inline constexpr llvm::StringLiteral kDOMElementType = "ctbrowser::element_ref";
inline constexpr llvm::StringLiteral kDOMElementVectorType = "std::vector<ctbrowser::element_ref>";
// The lattice's json rung, carried by the shared Core parser's owning tree.
inline constexpr llvm::StringLiteral kDOMJSONType = "ctbrowser::json_value";
inline constexpr llvm::StringLiteral kObjectIdentityType =
    "std::shared_ptr<ctnative::identity_object>";
// ONE ROW PER CARRIER, in enum order: its C++ spelling (empty when the
// carrier has no fixed one) and whether it is a String-union variant.
struct carrierRow {
    carrier which;
    llvm::StringLiteral spelling;
    bool stringUnion = false;
};
inline constexpr carrierRow kCarriers[] = {
    {carrier::none, ""},
    {carrier::boolean, kBooleanType},
    {carrier::symbol, kSymbolType},
    {carrier::number, kNumberType},
    {carrier::nullable, kNullableType},
    {carrier::string, kStringType},
    {carrier::nullableString, kNullableStringType},
    {carrier::booleanString, kBooleanStringType, true},
    {carrier::nullableBooleanString, kNullableBooleanStringType, true},
    {carrier::numberString, kNumberStringType, true},
    {carrier::nullableNumberString, kNullableNumberStringType, true},
    {carrier::map, ""},
    {carrier::closure, ""},
    {carrier::methodTable, ""},
    {carrier::objectIdentity, kObjectIdentityType},
    {carrier::objectValue, kObjectValueType},
    {carrier::domElement, kDOMElementType},
    {carrier::domElementVector, kDOMElementVectorType},
    {carrier::json, kDOMJSONType},
    {carrier::structure, ""},
    {carrier::vector, ""},
    {carrier::stringVector, ""},
};
static_assert(std::size(kCarriers) == static_cast<size_t>(carrier::stringVector) + 1,
              "a carrier without a row");
static_assert(
    [] {
        for (size_t i = 0; i < std::size(kCarriers); ++i) {
            if (kCarriers[i].which != static_cast<carrier>(i)) { return false; }
        }
        return true;
    }(),
    "carrier rows out of enum order");
inline const carrierRow & carrierInfo(carrier which) {
    return kCarriers[static_cast<size_t>(which)];
}
bool isScalarCarrier(carrier value);
bool isStringUnionCarrier(carrier value);
bool isPrimitiveCarrier(carrier value);
// The carriers a stored callable's concrete signature may use.
bool isCallableCarrier(carrier value);
mlir::Type scalarObservationType(mlir::MLIRContext * context, PrimitiveAlternatives alternatives);
// An emitc opaque type's spelling, through an lvalue; empty for anything else.
llvm::StringRef opaqueSpelling(mlir::Type type);
inline bool isNullableCarrier(mlir::Type type) {
    return opaqueSpelling(type) == kNullableType;
}
inline bool isNumberCarrier(mlir::Type type) {
    return opaqueSpelling(type) == kNumberType;
}
inline bool isBooleanCarrier(mlir::Type type) {
    return opaqueSpelling(type) == kBooleanType;
}
inline bool isNullableStringCarrier(mlir::Type type) {
    return opaqueSpelling(type) == kNullableStringType;
}
inline bool isBooleanStringCarrier(mlir::Type type) {
    return opaqueSpelling(type) == kBooleanStringType;
}
inline bool isNullableBooleanStringCarrier(mlir::Type type) {
    return opaqueSpelling(type) == kNullableBooleanStringType;
}
inline bool isNumberStringCarrier(mlir::Type type) {
    return opaqueSpelling(type) == kNumberStringType;
}
inline bool isNullableNumberStringCarrier(mlir::Type type) {
    return opaqueSpelling(type) == kNullableNumberStringType;
}
bool isStringCarrier(carrier value);
bool isVectorCarrier(carrier value);
bool stringConcatenation(mlir::Type left, mlir::Type right);
bool stringEquality(mlir::Type left, mlir::Type right);
bool isObjectCarrier(carrier value);
inline bool isObjectValueCarrier(mlir::Type type) {
    return opaqueSpelling(type) == kObjectValueType;
}
inline bool isIdentityCarrier(mlir::Type type) {
    return opaqueSpelling(type) == kObjectIdentityType;
}
bool isObjectValueType(mlir::Type type);
bool identityOrAbsent(mlir::Type type);
bool onlyAbsent(mlir::Type type);
// A definite Number, Boolean or String lattice type.
inline bool isPrimitive(mlir::Type type) {
    return llvm::isa_and_nonnull<NumType, BoolType, StrType>(type);
}
// A definite primitive, or only null/undefined.
inline bool primitiveOrAbsent(mlir::Type type) {
    return isPrimitive(type) || onlyAbsent(type);
}
carrier carrierOf(mlir::Type type);
mlir::Type vectorCarrierType(mlir::MLIRContext * context, bool strings = false);
llvm::StringRef mixedMapSpelling(mlir::Type type);
llvm::StringRef mixedMapKeySpelling(mlir::Type type);
llvm::StringRef nullableMapSpelling(mlir::Type type);
llvm::StringRef mapKeySpelling(mlir::Type type);
std::string mapValueSpelling(mlir::Type type);
bool mapNeedsString(MapType type);
bool mapNeedsNullableString(MapType type);
bool mapNeedsNullableStringKey(MapType type);
bool mapNeedsObjectValues(MapType type);
mlir::Type mapCarrierType(MapType type);
mlir::Type closureCarrierType(ClosureType type);
mlir::Type methodTableCarrierType(MethodTableType type);
mlir::Type carrierType(mlir::MLIRContext * context, carrier which);
std::string printed(mlir::Type type);
using receiverGroups = llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 2>>;
llvm::SmallVector<mlir::Value, 2> aliasesOf(const receiverGroups * groups, mlir::Value value);
std::string siteOf(mlir::Location loc);
std::string siteOfFunction(ctjs::FuncOp fn);
std::string cIdentifier(llvm::StringRef symbol);
// The generated C++ names, each spelled once: a method table's class and a
// closure's environment type (both in namespace ctnative), and its binder.
inline std::string methodTableClass(llvm::StringRef site) {
    return "method_" + cIdentifier(site);
}
inline std::string environmentType(llvm::StringRef target) {
    return "ctn_env_" + cIdentifier(target);
}
inline std::string binderName(llvm::StringRef target) {
    return "ctn_bind_" + cIdentifier(target);
}
mlir::FrozenRewritePatternSet declarativePatterns(mlir::MLIRContext * context);

bool isScriptEntry(ctjs::FuncOp fn);
// Where a `ctjs.create_closure`'s captures start: after $enclosing_closure and
// $enclosing_this, which are operands and not attributes.
inline constexpr unsigned kFirstCapture = 2;
// A FUNCTION DECLARATION IS A BINDING, NOT A VALUE. `function f() {}` at
// any level is `create_closure` whose only use is `store_global "f"`; in
// the closed world the function exists as an emitc.func and calls of it
// are direct, so the pair lowers to nothing. A closure used as a VALUE -
// passed, returned, stored anywhere else - is not native yet (Phase 59).
bool isDeclarationClosure(mlir::Operation * o);
// A get/set_property of a constant key on the object `use` supplies.
inline bool isConstantKeyAccess(mlir::OpOperand & use) {
    const auto key = ctjs::propertyKey(use.getOwner());
    return key && use.getOperandNumber() == 0 && !ctjs::constantKey(key).empty();
}
// What a DOM entry reads only to reach its proved host calls: a callback
// closure, the initial intrinsics, a prefix RegExp and method-like reads.
bool domBookkeeping(const DOMEntryAnalysis & entry, mlir::Operation * op);
// All source stores to each emitted identity member, before any retyping.
// A read's narrower result never selects storage shared by other allocations.
llvm::StringMap<mlir::Type> identityFieldStoreTypes(mlir::DataFlowSolver & solver,
                                                    llvm::ArrayRef<ctjs::FuncOp> functions);

} // namespace ctcompile::ctnative::lowering_detail
