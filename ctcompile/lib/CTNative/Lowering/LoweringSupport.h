//===- LoweringSupport.h - Carriers for EmitC lowering ----------*- C++ -*-===//
//
// Declarations shared by the native EmitC lowering: the carrier enum (the C++
// representation each lowered JavaScript value takes) with its spelling table,
// carrier predicates, Map/closure/method-table storage spellings, and the
// naming and source-site helpers the emitter uses.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/NativeClosure.h"
#include "ctcompile/CTNative/Analysis/NativeDictionary.h"
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
/// The C++ representation a lowered JavaScript value takes in generated code.
/// kCarriers gives each one's spelling, in enum order. `none` means the value
/// has no native representation and must be refused.
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
    /// One of a proved set of callables (Analysis lattice `closure_set`), a
    /// std::variant spelled per set by EmitC/ClosureSets.cpp.
    closureSet,
    methodTable,
    objectIdentity,
    objectValue,
    domElement,
    domElementVector,
    json,
    structure,
    vector,
    stringVector,
    /// A two-position array literal read only at constant indices, as a
    /// std::pair of its positions' carriers (elementSpelling).
    tuple,
    /// A vector of such tuples (elementArraySpelling).
    tupleVector,
    /// An ordinary script's getBoundingClientRect answer (Runtime/Page.hpp).
    domRect,
    /// An ordinary script's getComputedStyle answer (Runtime/Page.hpp).
    computedStyle,
    /// A run-once static's constant table, or undefined before it is stored: a
    /// pointer to its namespace-scope struct (Runtime/StaticTable.hpp).
    staticTable,
    /// An object used as a table, std::shared_ptr<ctnative::dictionary<V>>
    /// (Runtime/Dictionary.hpp), spelled by dictionarySpelling.
    dictionary,
    /// The Event a native listener receives (Runtime/Events.hpp).
    domEvent,
    /// An ordinary script's element or null (Runtime/Browser.hpp).
    nullableElement,
    /// A timer's handle (Runtime/Timers.hpp).
    timer,
    /// An instance of a class a registry Map owns, or null or undefined:
    /// `std::shared_ptr<C>`, spelled per class (EmitC/Instances.cpp).
    instance
};
inline constexpr llvm::StringLiteral kVectorType = "std::vector<double>";
inline constexpr llvm::StringLiteral kStringVectorType = "std::vector<std::string>";
/// An array of Strings the element-vector proof owns (Runtime/Array.hpp), as
/// opposed to the raw std::string snapshots above.
inline constexpr llvm::StringLiteral kStringArrayType = "std::vector<ctnative::js_string>";
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
/// An ordinary script's element: the runtime's view, which carries the style
/// engine its page reads need (Runtime/Browser.hpp).
inline constexpr llvm::StringLiteral kPageElementType = "ctnative::js_element_t";
inline constexpr llvm::StringLiteral kDOMRectType = "ctnative::js_dom_rect";
inline constexpr llvm::StringLiteral kComputedStyleType = "ctnative::js_computed_style";
inline constexpr llvm::StringLiteral kEventType = "ctnative::js_event";
inline constexpr llvm::StringLiteral kNullableElementType = "std::optional<ctnative::js_element_t>";
inline constexpr llvm::StringLiteral kTimerType = "ctnative::timer_id";
/// The carrier for a JSON value in the type lattice: the owning tree built by
/// ctbrowser's shared JSON parser.
inline constexpr llvm::StringLiteral kDOMJSONType = "ctbrowser::json_value";
/// A run-once static's constant table (Runtime/StaticTable.hpp).
inline constexpr llvm::StringLiteral kStaticTableType = "ctnative::static_table_ptr";
inline constexpr llvm::StringLiteral kObjectIdentityType =
    "std::shared_ptr<ctnative::identity_object>";
/// One row per carrier, in enum order: its C++ spelling (empty when the
/// carrier has no fixed one) and whether it is a String-union variant.
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
    {carrier::closureSet, ""},
    {carrier::methodTable, ""},
    {carrier::objectIdentity, kObjectIdentityType},
    {carrier::objectValue, kObjectValueType},
    {carrier::domElement, kDOMElementType},
    {carrier::domElementVector, kDOMElementVectorType},
    {carrier::json, kDOMJSONType},
    {carrier::structure, ""},
    {carrier::vector, ""},
    {carrier::stringVector, ""},
    {carrier::tuple, ""},
    {carrier::tupleVector, ""},
    {carrier::domRect, kDOMRectType},
    {carrier::computedStyle, kComputedStyleType},
    {carrier::staticTable, kStaticTableType},
    {carrier::dictionary, ""},
    {carrier::domEvent, kEventType},
    {carrier::nullableElement, kNullableElementType},
    {carrier::timer, kTimerType},
    {carrier::instance, ""},
};
static_assert(std::size(kCarriers) == static_cast<size_t>(carrier::instance) + 1,
              "a carrier without a row");
static_assert(
    [] {
        for (size_t i = 0; i < std::size(kCarriers); ++i) {
            if (kCarriers[i].which != static_cast<carrier>(i)) { return false; }
        }
        return true;
    }(),
    "carrier rows out of enum order");
/// The kCarriers row for `which`.
inline const carrierRow & carrierInfo(carrier which) {
    return kCarriers[static_cast<size_t>(which)];
}
/// Number, Boolean, or a nullable/Number|Boolean scalar.
bool isScalarCarrier(carrier value);
bool isStringUnionCarrier(carrier value);
bool isPrimitiveCarrier(carrier value);
/// The carriers a stored callable's concrete signature may use.
bool isCallableCarrier(carrier value);
/// The lattice type for an observed scalar whose possible primitives are
/// `alternatives`: Boolean and/or Number, made optional when null or undefined
/// is possible. Null when any other primitive is possible or nothing is known.
mlir::Type scalarObservationType(mlir::MLIRContext * context, PrimitiveAlternatives alternatives);
/// An emitc opaque type's spelling, looking through an lvalue; empty for any
/// other type.
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
/// The type a closed-shape field holds when its reads and writes join to an
/// optional String or a union of String with Boolean or Number (and maybe
/// absence), which a struct member stores as that union carrier; null for any
/// other field. `accesses` are the field's reads and writes, `typeOf` the
/// lattice type of a value (ObjectValues/VariantFields.cpp).
mlir::Type variantFieldType(llvm::ArrayRef<mlir::Operation *> accesses,
                            llvm::function_ref<mlir::Type(mlir::Value)> typeOf);
/// A definite Number, Boolean or String lattice type.
inline bool isPrimitive(mlir::Type type) {
    return llvm::isa_and_nonnull<NumType, BoolType, StrType>(type);
}
/// A definite primitive, or only null/undefined.
inline bool primitiveOrAbsent(mlir::Type type) {
    return isPrimitive(type) || onlyAbsent(type);
}
carrier carrierOf(mlir::Type type);
/// Whether `v`, whose proved type is `type`, is an element array: a vector of
/// Strings, tuples or DOM elements whose group the element-vector proof
/// (TypeInference::elementVectorAliases) accepts. It lowers to
/// elementArraySpelling(type). Elements count only with `pageViews` (an
/// ordinary script, lowering::pageViews): a host contract's element vectors
/// are its own proof's.
bool isElementArray(mlir::Value v, mlir::Type type, bool pageViews);
/// The C++ type of one element of an element array or one tuple position:
/// ctnative::js_string, ctnative::js_num, ctnative::number_string,
/// ctnative::js_element_t, or std::pair of two of those for a tuple. Empty for
/// anything else.
std::string elementSpelling(mlir::Type type);
/// `std::vector<elementSpelling(element)>` for an element array's VecType.
std::string elementArraySpelling(mlir::Type type);
/// `std::span<const elementSpelling(element)>`: how a function borrows an
/// element array its caller owns.
std::string elementSpanSpelling(mlir::Type type);
/// Whether `o` lowers to nothing because it only names a vector call: the
/// method read that is its callee, or the RegExp literal (its factory load,
/// the factory call) that is `split(/\s+/)`'s separator.
bool isVectorCallPlumbing(mlir::Operation * o);
/// Whether `o` loads the realm's `document` or `window` only to read
/// `document.documentElement` or `window.devicePixelRatio` off it, or its
/// `getComputedStyle` only to call it. The read or call lowers to a runtime
/// call, so the load lowers to nothing.
bool isPageGlobal(mlir::Operation * o);
/// Whether `o` reads a page method only to call it on the object it read it
/// from (`el.getBoundingClientRect()`, `style.getPropertyValue(name)`,
/// `el.addEventListener(type, f)`). The call lowers to a runtime call, so the
/// read lowers to nothing.
bool isPageMethodRead(mlir::Operation * o);
mlir::Type vectorCarrierType(mlir::MLIRContext * context, bool strings = false);
llvm::StringRef mixedMapSpelling(mlir::Type type);
llvm::StringRef mixedMapKeySpelling(mlir::Type type);
llvm::StringRef nullableMapSpelling(mlir::Type type);
llvm::StringRef mapKeySpelling(mlir::Type type);
std::string mapValueSpelling(mlir::Type type);
/// The C++ name of an identity class's struct, as nameFamilies gives it.
std::string instanceClassSpelling(llvm::StringRef className);
/// The class C of `instance<C>` or `opt<instance<C>>`.
llvm::StringRef instanceClassName(mlir::Type type);
/// `std::shared_ptr<C>`, the carrier of `instance<C>` and `opt<instance<C>>`.
mlir::Type instanceCarrierType(mlir::Type type);
bool mapNeedsString(MapType type);
bool mapNeedsNullableString(MapType type);
bool mapNeedsNullableStringKey(MapType type);
bool mapNeedsObjectValues(MapType type);
mlir::Type mapCarrierType(MapType type);
/// The C++ type of a table with values of `type`'s element type:
/// std::shared_ptr<ctnative::dictionary<V>>, V the value's carrier.
std::string dictionarySpelling(DictType type);
mlir::Type closureCarrierType(ClosureType type);
mlir::Type methodTableCarrierType(MethodTableType type);
mlir::Type carrierType(mlir::MLIRContext * context, carrier which);
/// `type` printed as text, or `<unvisited>` for a null type (a value the
/// dataflow solver never reached).
std::string printed(mlir::Type type);
/// Each value mapped to every value that names the same object.
using receiverGroups = llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 2>>;
llvm::SmallVector<mlir::Value, 2> aliasesOf(const receiverGroups * groups, mlir::Value value);
std::string siteOf(mlir::Location loc);
std::string siteOfFunction(ctjs::FuncOp fn);
/// `symbol` made into a valid C++ identifier.
std::string cIdentifier(llvm::StringRef symbol);
/// The generated C++ names, each spelled once: a method table's class and a
/// closure's environment type (both in namespace ctnative), and its binder.
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

/// Whether `fn` is the importer's top-level script function, `_script_$0`.
bool isScriptEntry(ctjs::FuncOp fn);
/// Where a `ctjs.create_closure`'s captures start: after $enclosing_closure
/// and $enclosing_this, which are operands and not attributes.
inline constexpr unsigned kFirstCapture = 2;
/// Whether `o` is a function declaration: a ctjs.create_closure whose only use
/// is a ctjs.store_global. A declaration is a binding, not a value. In the
/// closed world the function is an emitc.func called directly, so the pair
/// lowers to nothing. Closures used as values are handled by the closure lift
/// (ClosureLifting/).
bool isDeclarationClosure(mlir::Operation * o);
/// Whether `use` is the object operand of a get/set_property with a constant
/// key.
inline bool isConstantKeyAccess(mlir::OpOperand & use) {
    const auto key = ctjs::propertyKey(use.getOwner());
    return key && use.getOperandNumber() == 0 && !ctjs::constantKey(key).empty();
}
/// Whether `op` exists only so a DOM entry can reach its proved host calls: a
/// callback closure, the initial intrinsics, a prefix RegExp or a method-like
/// read. Such operations lower to nothing.
bool domBookkeeping(const DOMEntryAnalysis & entry, mlir::Operation * op);
/// The storage type of each identity-object member, keyed by property name:
/// the meet of every source store to it across `functions`, computed before any
/// retyping. A member that is only read stays opt<bottom>, and a store with no
/// inferred type makes it boxed (which admission then refuses). A read's
/// narrower result never selects storage shared by other allocations.
llvm::StringMap<mlir::Type> identityFieldStoreTypes(mlir::DataFlowSolver & solver,
                                                    llvm::ArrayRef<ctjs::FuncOp> functions);

} // namespace ctcompile::ctnative::lowering_detail
