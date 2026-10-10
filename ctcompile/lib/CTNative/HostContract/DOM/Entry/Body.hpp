//===- Body.hpp - DOM entry proof internals ---------------------*- C++ -*-===//
//
// Internal state shared by the DOM entry proof files: the Kind enum that
// classifies each admitted value and the Body visitor that walks one function
// body. Only DOM/Entry.cpp and the files in DOM/Entry/ include it.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../../Analysis.h"
#include "../Snapshot/Bounds.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Dominance.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "llvm/ADT/STLFunctionalExtras.h"

namespace ctcompile::ctnative::dom_entry_detail {

/// What the DOM entry proof knows about one SSA value: the browser object,
/// method, intrinsic or primitive it is. A value with no Kind was not admitted.
enum class Kind {
    implicit,
    element,
    nullableElement,
    document,
    documentQuerySelector,
    documentQuerySelectorAll,
    tokenList,
    dataset,
    elementIntrinsic,
    symbolIntrinsic,
    symbol,
    symbolToString,
    symbolValueOf,
    undefinedString,
    elementPrototype,
    prototypeSelector,
    selectorCall,
    objectIntrinsic,
    objectKeys,
    stringVector,
    elementVector,
    filterStrings,
    callback,
    replacementCallback,
    startsWith,
    replacePrefix,
    charAt,
    slice,
    lowercaseUnit,
    regexpFactory,
    prefixRegExp,
    uppercaseRegExp,
    toggle,
    containsClass,
    addClass,
    removeClass,
    attribute,
    getAttribute,
    optionalString,
    toggleAttribute,
    hasAttribute,
    removeAttribute,
    contains,
    matches,
    closest,
    querySelector,
    querySelectorAll,
    numberIntrinsic,
    uriIntrinsic,
    jsonIntrinsic,
    jsonParse,
    json,
    jsonAggregate, // A JSON value typeof calls "object": null, an array or an object.
    numberToString,
    number,
    string,
    boolean,
    optionalBoolean, // Boolean or null, never undefined.
    optionalNumber,  // Number or null, never undefined.
    null,
    undefined,
    mapIntrinsic, // The initial standard Map constructor.
    map,          // A local instance, used only through mapMethod calls and size.
    mapMethod,    // Its set/get/has/delete/clear, called on that same instance.
    mapPayload,   // A get result (a stored Number, String, Boolean or undefined), or
                  // one joined with another primitive in a branch.
    record,       // A fresh local record stored in exactly one local Map.
    recordAlias   // A get result of such a Map: the stored record, never absent.
};

/// Scalar kinds whose truth test runs no conversion hook.
inline constexpr std::array truthyKinds{
    Kind::boolean,         Kind::number, Kind::string, Kind::optionalString, Kind::undefinedString,
    Kind::nullableElement, Kind::null,   Kind::symbol, Kind::mapPayload};
/// Kinds a loop may carry unchanged through its block arguments.
inline constexpr std::array loopKinds{Kind::number, Kind::string,          Kind::boolean,
                                      Kind::symbol, Kind::undefinedString, Kind::element};

/// The element whose dataset a snapshot or key came from, and the mutation
/// epoch (Body::mutationEpoch) it was taken in.
struct DatasetOrigin {
    mlir::Value element;
    unsigned epoch;
};

/// A boolean that tests whether `optional` is a String; `stringOnTrue` says
/// which branch sees the String.
struct Predicate {
    mlir::Value optional;
    bool stringOnTrue;
};

/// The admission walk over one function body of a DOM entry: the entry
/// function itself or one of its filter callbacks. DOMEntryAnalysis builds one
/// per body and calls visit(); facts every body shares live in `proved` and
/// the references at the top, facts local to this body in the maps below.
struct Body {
    // Shared by every function body of one entry.
    std::string & refusal;
    bool & budgetExhausted;
    DOMEntryAnalysis::Evidence & proved;
    const llvm::StringSet<> & intrinsics;
    mlir::BlockArgument documentParameter;
    mlir::DominanceInfo & dominance;
    llvm::DenseSet<mlir::Operation *> & usedCallbacks;
    const llvm::DenseSet<mlir::Operation *> & prefixCallbacks;
    const llvm::DenseSet<mlir::Operation *> & replacementCallbacks;
    const llvm::DenseMap<unsigned, ctjs::FuncOp> & indexedCallbacks;
    llvm::function_ref<bool()> spend;
    // This function body and its local facts.
    ctjs::FuncOp function;
    mlir::Block & block;
    bool callbackBody, replacementBody;
    llvm::DenseMap<mlir::Value, Kind> values{};
    llvm::DenseSet<mlir::Value> increasingIndices{};
    unsigned mutationEpoch = 0;
    llvm::DenseMap<mlir::Value, unsigned> datasetEpochs{};
    llvm::DenseMap<mlir::Value, DatasetOrigin> snapshotOrigins{}, keyOrigins{};
    // Results of `key.startsWith("bs...")` on a filter callback's input:
    // Bootstrap's data-attribute filter keeps only `data-bs-*` dataset keys.
    // When the test is true the key has that prefix, so keys stay unique
    // after it is stripped.
    llvm::DenseSet<mlir::Value> prefixRequired{}, prefixSnapshots{};
    llvm::DenseMap<mlir::Value, ctjs::GetPropertyOp> strippedAssignmentKeys{};
    llvm::DenseMap<mlir::Value, mlir::Value> firstUnits{}, stringTails{}, loweredFirstUnits{};
    llvm::DenseMap<mlir::Value, mlir::Value> typeQueries{};
    llvm::DenseMap<mlir::Value, Predicate> predicates{};
    llvm::DenseMap<mlir::Value, bool> constantBooleans{};
    llvm::DenseMap<mlir::Value, std::size_t> activeRefinements{};
    bool documentRootPresent = false;
    // Every chained-call alias of a local Map whose values are fresh child
    // Maps.
    llvm::DenseSet<mlir::Value> mapParents{};
    // Records: the Map each record and alias belongs to, the scalar fields a
    // record initializes before it is first stored, and each record Map's
    // schema, fixed by its first stored record. primitiveMaps holds the Maps
    // that store primitives instead.
    llvm::DenseMap<mlir::Value, mlir::Value> recordMaps{};
    llvm::DenseMap<mlir::Value, llvm::StringMap<Kind>> recordFields{}, recordSchemas{};
    llvm::DenseSet<mlir::Value> storedRecords{}, primitiveMaps{};
    llvm::DenseSet<mlir::Block *> nonReturning{};

    /// Records \p reason as the refusal and returns false. Each call
    /// overwrites the previous reason, so the last check that failed wins.
    bool fail(const llvm::Twine & reason) {
        refusal = reason.str();
        return false;
    }
    bool supplied(llvm::StringRef name) const { return intrinsics.contains(name); }
    /// True when RegExp and its literal factory `__ctbrowser_regexp`, which
    /// stands in for the original RegExp constructor, are both supplied.
    bool suppliedRegExp() const { return supplied("RegExp") && supplied("__ctbrowser_regexp"); }
    bool hasKind(mlir::Value value, Kind kind) const {
        const auto found = values.find(value);
        return found != values.end() && found->second == kind;
    }
    bool hasKind(mlir::Value value, llvm::ArrayRef<Kind> kinds) const {
        const auto found = values.find(value);
        return found != values.end() && llvm::is_contained(kinds, found->second);
    }
    /// True when every operation enclosing \p operation up to the function is
    /// an scf.if; charges one step per ancestor and fails with \p reason
    /// otherwise.
    bool onlyBranchAncestors(mlir::Operation * operation, llvm::StringRef reason);
    /// Checks every operation in \p body against the DOM entry contract.
    /// Records each admitted value's Kind and the facts lowering needs in
    /// `proved`, and stops at the first operation outside the contract with a
    /// reason from fail(). \p depth is the nesting depth of structured
    /// regions (refused at 64). \p frame is the open shadow frame: set by
    /// FrameEnterOp, cleared by FrameExitOp.
    bool visit(mlir::Block & body, unsigned depth, mlir::Value & frame);
    /// Checks one structured control-flow operation (scf.while, scf.if,
    /// scf.execute_region, ctjs.invoke and their terminators). Returns
    /// std::nullopt when \p operation is not control flow, false on refusal
    /// (reason recorded by fail()), and true when proved.
    std::optional<bool> controlFlow(mlir::Operation & operation, mlir::Block & body, unsigned depth,
                                    mlir::Value & frame, bool & returned);
    /// Classifies \p operation as a proved browser operation (a DOM, String,
    /// Symbol, Number or JSON call or read the native host can run). Returns
    /// std::nullopt when it is not one, false when it is but cannot be proved
    /// (fail() recorded why, or the work budget ran out), and true when it was
    /// proved and recorded in `proved` and `values`.
    std::optional<bool> browserOperation(mlir::Operation & operation);
    std::optional<bool> mapOperation(mlir::Operation & operation);
    std::optional<bool> recordOperation(mlir::Operation & operation);
};

} // namespace ctcompile::ctnative::dom_entry_detail
