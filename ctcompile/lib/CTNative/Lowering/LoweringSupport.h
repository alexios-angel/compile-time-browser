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
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ctcompile::ctnative::lowering_detail {

namespace ec = mlir::emitc;
enum class carrier {
    none,
    boolean,
    number,
    nullable,
    string,
    nullableString,
    booleanString,
    map,
    closure,
    methodTable,
    objectIdentity,
    objectValue,
    domElement,
    structure,
    vector,
    stringVector
};
inline constexpr llvm::StringLiteral kVectorType = "std::vector<double>";
inline constexpr llvm::StringLiteral kStringVectorType = "std::vector<std::string>";
inline constexpr llvm::StringLiteral kBooleanStringType = "std::variant<bool, std::string>";
inline constexpr llvm::StringLiteral kNullableStringType = "ctnative::nullable_string";
inline constexpr llvm::StringLiteral kNullableType = "ctnative::nullable_scalar";
inline constexpr llvm::StringLiteral kObjectValueType = "ctnative::object_value";
inline constexpr llvm::StringLiteral kDOMElementType = "ctbrowser::element_ref";
bool isScalarCarrier(carrier value);
mlir::Type scalarObservationType(mlir::MLIRContext * context, PrimitiveAlternatives alternatives);
bool isNullableCarrier(mlir::Type type);
bool isNullableStringCarrier(mlir::Type type);
bool isBooleanStringCarrier(mlir::Type type);
bool isStringCarrier(carrier value);
bool isVectorCarrier(carrier value);
bool stringConcatenation(mlir::Type left, mlir::Type right);
bool stringEquality(mlir::Type left, mlir::Type right);
bool isObjectCarrier(carrier value);
bool isObjectValueCarrier(mlir::Type type);
bool isIdentityCarrier(mlir::Type type);
bool isObjectValueType(mlir::Type type);
bool identityOrAbsent(mlir::Type type);
bool onlyAbsent(mlir::Type type);
inline constexpr llvm::StringLiteral kObjectIdentityType =
    "std::shared_ptr<ctnative::identity_object>";
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
mlir::FrozenRewritePatternSet declarativePatterns(mlir::MLIRContext * context);

bool isScriptEntry(ctjs::FuncOp fn);
// Where a `ctjs.create_closure`'s captures start: after $enclosing_closure and
// $enclosing_this, which are operands and not attributes.
inline constexpr unsigned kFirstCapture = 2;
bool isUndefinedConstant(mlir::Value value);
// All source stores to each emitted identity member, before any retyping.
// A read's narrower result never selects storage shared by other allocations.
llvm::StringMap<mlir::Type> identityFieldStoreTypes(mlir::DataFlowSolver & solver,
                                                    llvm::ArrayRef<ctjs::FuncOp> functions);

} // namespace ctcompile::ctnative::lowering_detail
