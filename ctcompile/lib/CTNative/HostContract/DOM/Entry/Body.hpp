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

#include <array>
#include <cstddef>
#include <cstdint>

#include "llvm/ADT/STLFunctionalExtras.h"

namespace ctcompile::ctnative::dom_entry_detail {

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
    jsonAggregate, // The typeof object alternatives: null, array or object.
    numberToString,
    number,
    string,
    boolean,
    optionalBoolean, // Boolean or null, never undefined.
    optionalNumber,  // Number or null, never undefined.
    null,
    undefined
};

// Scalars whose truth test runs no conversion hook.
inline constexpr std::array truthyKinds{
    Kind::boolean,         Kind::number,          Kind::string, Kind::optionalString,
    Kind::undefinedString, Kind::nullableElement, Kind::null,   Kind::symbol};
// Invariant scalars a loop may carry.
inline constexpr std::array loopKinds{Kind::number, Kind::string,          Kind::boolean,
                                      Kind::symbol, Kind::undefinedString, Kind::element};

struct DatasetOrigin {
    mlir::Value element;
    unsigned epoch;
};

struct Predicate {
    mlir::Value optional;
    bool stringOnTrue;
};

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
    // Boolean truth requires the callback input to start with "bs". This
    // implication proves filtered-key uniqueness after removing that prefix.
    llvm::DenseSet<mlir::Value> prefixRequired{}, prefixSnapshots{};
    llvm::DenseMap<mlir::Value, ctjs::GetPropertyOp> strippedAssignmentKeys{};
    llvm::DenseMap<mlir::Value, mlir::Value> firstUnits{}, stringTails{}, loweredFirstUnits{};
    llvm::DenseMap<mlir::Value, mlir::Value> typeQueries{};
    llvm::DenseMap<mlir::Value, Predicate> predicates{};
    llvm::DenseMap<mlir::Value, bool> constantBooleans{};
    llvm::DenseMap<mlir::Value, std::size_t> activeRefinements{};
    bool documentRootPresent = false;
    llvm::DenseSet<mlir::Block *> nonReturning{};

    bool supplied(llvm::StringRef name) const { return intrinsics.contains(name); }
    // The literal factory stands in for the original RegExp constructor.
    bool suppliedRegExp() const { return supplied("RegExp") && supplied("__ctbrowser_regexp"); }
    bool hasKind(mlir::Value value, Kind kind) const {
        const auto found = values.find(value);
        return found != values.end() && found->second == kind;
    }
    bool hasKind(mlir::Value value, llvm::ArrayRef<Kind> kinds) const {
        const auto found = values.find(value);
        return found != values.end() && llvm::is_contained(kinds, found->second);
    }
    bool visit(mlir::Block & body, unsigned depth, mlir::Value & frame);
    std::optional<bool> controlFlow(mlir::Operation & operation, mlir::Block & body, unsigned depth,
                                    mlir::Value & frame, bool & returned);
    std::optional<bool> browserOperation(mlir::Operation & operation);
};

} // namespace ctcompile::ctnative::dom_entry_detail
