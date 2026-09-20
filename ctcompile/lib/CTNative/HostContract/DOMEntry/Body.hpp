#pragma once

#include "../Analysis.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Dominance.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringSet.h"

#include <cstddef>
#include <cstdint>

#include "llvm/ADT/STLFunctionalExtras.h"

namespace ctcompile::ctnative::dom_entry_detail {

enum class Kind {
    implicit,
    element,
    nullableElement,
    tokenList,
    dataset,
    objectIntrinsic,
    objectKeys,
    stringVector,
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
    null,
    undefined
};

struct DatasetOrigin {
    mlir::Value element;
    unsigned epoch;
};

struct Predicate {
    mlir::Value optional;
    bool stringOnTrue;
};

struct Body {
    std::string & refusal;
    bool & budgetExhausted;
    mlir::Block & block;
    std::vector<mlir::BlockArgument> & provedDatasetElements;
    const bool & callbackBody;
    const bool & replacementBody;
    const bool & suppliedNumber;
    const bool & suppliedURI;
    const bool & suppliedObject;
    const bool & suppliedJSON;
    const bool & suppliedArray;
    const bool & suppliedString;
    const bool & suppliedRegExp;
    bool & provedUndefinedReturn;
    llvm::DenseMap<mlir::Value, Kind> & values;
    llvm::DenseSet<mlir::Value> & increasingIndices;
    llvm::DenseSet<mlir::Value> & prefixRequired;
    llvm::DenseSet<mlir::Value> & prefixSnapshots;
    unsigned & mutationEpoch;
    llvm::DenseMap<mlir::Value, unsigned> & datasetEpochs;
    llvm::DenseMap<mlir::Value, DatasetOrigin> & snapshotOrigins;
    llvm::DenseMap<mlir::Value, DatasetOrigin> & keyOrigins;
    llvm::DenseMap<mlir::Value, ctjs::GetPropertyOp> & strippedAssignmentKeys;
    llvm::DenseMap<mlir::Value, mlir::Value> & firstUnits;
    llvm::DenseMap<mlir::Value, mlir::Value> & stringTails;
    llvm::DenseMap<mlir::Value, mlir::Value> & loweredFirstUnits;
    llvm::DenseMap<mlir::Value, mlir::Value> & typeQueries;
    llvm::DenseMap<mlir::Value, Predicate> & predicates;
    llvm::DenseMap<mlir::Value, bool> & constantBooleans;
    llvm::DenseMap<mlir::Value, std::size_t> & activeRefinements;
    std::vector<ctjs::GetPropertyOp> & provedTokens;
    std::vector<ctjs::GetPropertyOp> & provedDatasets;
    std::vector<ctjs::GetPropertyOp> & provedStringVectorLengths;
    std::vector<ctjs::GetPropertyOp> & provedStringVectorIndices;
    std::vector<std::pair<ctjs::GetPropertyOp, mlir::Value>> & provedDatasetValues;
    std::vector<ctjs::LoadGlobalOp> & provedNumberIntrinsics;
    std::vector<ctjs::LoadGlobalOp> & provedURIIntrinsics;
    std::vector<ctjs::LoadGlobalOp> & provedJSONIntrinsics;
    std::vector<ctjs::LoadGlobalOp> & provedObjectIntrinsics;
    std::vector<ctjs::LoadGlobalOp> & provedRegExpIntrinsics;
    std::vector<ctjs::CallOp> & provedPrefixRegExps;
    std::vector<ctjs::InvokeOp> & provedInvocations;
    std::vector<std::pair<ctjs::GetPropertyOp, HostDOMMethod>> & provedMethods;
    std::vector<HostDOMCall> & provedCalls;
    std::vector<ctjs::CreateObjectOp> & provedJSONObjects;
    std::vector<ctjs::CopyPropsOp> & provedJSONCopies;
    std::vector<ctjs::SetPropertyOp> & provedJSONAssignments;
    std::vector<ctjs::SetPropertyOp> & provedSnapshotAssignments;
    std::vector<mlir::Value> & provedOptionalStrings;
    std::vector<mlir::Value> & provedStrings;
    std::vector<HostDOMStringRefinement> & provedRefinements;
    std::vector<std::pair<mlir::Value, bool>> & provedBooleans;
    mlir::DominanceInfo & dominance;
    std::vector<std::pair<ctjs::CreateClosureOp, ctjs::FuncOp>> & provedClosures;
    llvm::DenseSet<mlir::Operation *> & usedCallbacks;
    llvm::DenseSet<mlir::Operation *> & prefixCallbacks;
    llvm::DenseSet<mlir::Operation *> & replacementCallbacks;
    llvm::DenseMap<unsigned, ctjs::FuncOp> & indexedCallbacks;
    ctjs::FuncOp & function;
    ctjs::FuncOp & target;
    llvm::function_ref<bool()> spend;
    llvm::function_ref<bool(mlir::Value)> emptyString;

    bool hasKind(mlir::Value value, Kind kind) const {
        const auto found = values.find(value);
        return found != values.end() && found->second == kind;
    }
    bool visit(mlir::Block & body, unsigned depth, mlir::Value & frame);
    std::optional<bool> controlFlow(mlir::Operation & operation, mlir::Block & body, unsigned depth,
                                    mlir::Value & frame, bool & returned);
    std::optional<bool> browserOperation(mlir::Operation & operation);
};

} // namespace ctcompile::ctnative::dom_entry_detail
