#include "Analysis.h"
#include "DOMEntry/Body.hpp"
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

namespace ctcompile::ctnative {

DOMEntryAnalysis::DOMEntryAnalysis(mlir::ModuleOp module, const HostContract & contract,
                                   unsigned maxSteps) {
    // THE CENSUS FIRST, AND IT IS CHARGED. The fingerprint below prints the
    // whole module, which is work proportional to its size, and it used to run
    // before the first charged step - so maxSteps bounded the structural walk
    // and nothing else, and a zero budget still paid for a full print. This is
    // normalizeDOMURI's rule: one step per operation plus one per operand,
    // refused as exhausted if it does not fit, and the same cost reserved
    // again for the fingerprint's own pass over the module. workSteps carries
    // both, so steps() is still the exact budget that reproduces the proof.
    const auto scanned = module.walk([&](mlir::Operation * operation) {
        const uint64_t cost = uint64_t(1) + operation->getNumOperands();
        if (cost > maxSteps - workSteps) { return mlir::WalkResult::interrupt(); }
        workSteps += static_cast<unsigned>(cost);
        return mlir::WalkResult::advance();
    });
    if (scanned.wasInterrupted() || workSteps > maxSteps - workSteps) {
        budgetExhausted = true;
        refusal = "DOM entry analysis work budget exhausted";
        return;
    }
    workSteps *= 2;
    if (hostContractFingerprint(module) != contract.moduleSha256) {
        refusal = "host contract module fingerprint mismatch";
        return;
    }
    if (module->hasAttr("ctjs.skipped")) {
        refusal = "DOM entry source contains unimported functions";
        return;
    }
    llvm::StringSet<> intrinsicNames;
    const auto & supportedIntrinsics = host_detail::domInitialIntrinsics();
    const bool intrinsicEntry = contract.provider == HostContract::Provider::ctbrowserIntrinsics;
    if ((contract.provider != HostContract::Provider::ctbrowserDOM &&
         contract.provider != HostContract::Provider::ctbrowserDOMSession && !intrinsicEntry) ||
        (intrinsicEntry ? !contract.elementParameters.empty()
                        : contract.elementParameters.empty()) ||
        (!intrinsicEntry && !contract.intrinsicParameters.empty()) ||
        (intrinsicEntry &&
         (contract.currentDocumentParameter || !contract.datasetParameters.empty() ||
          !llvm::is_contained(contract.initialIntrinsics, "Symbol") ||
          llvm::any_of(contract.initialIntrinsics,
                       [](const auto & name) { return name != "Symbol" && name != "String"; }))) ||
        !contract.roots.empty() || !contract.observations.empty() ||
        !contract.absentBindings.empty() || !contract.undefinedBindings.empty() ||
        contract.initialIntrinsics.size() > supportedIntrinsics.size() ||
        llvm::any_of(contract.initialIntrinsics,
                     [&](const auto & name) {
                         return name.size() > host_detail::maxDOMIntrinsicNameLength ||
                                !supportedIntrinsics.contains(name) ||
                                !intrinsicNames.insert(name).second;
                     }) ||
        contract.realmGlobalThis || contract.classicScriptRealm ||
        !contract.realmOwnDataProperties.empty()) {
        refusal = intrinsicEntry ? "intrinsic entry requires an isolated Symbol/String declaration"
                                 : "DOM entry requires the isolated ctbrowser-dom-v1 declaration";
        return;
    }
    const auto spend = [&] {
        if (workSteps == maxSteps) {
            budgetExhausted = true;
            refusal = "DOM entry analysis work budget exhausted";
            return false;
        }
        ++workSteps;
        return true;
    };
    if (!spend()) { return; }
    auto target = module.lookupSymbol<ctjs::FuncOp>(contract.entry);
    if (!target || target.getBody().empty()) {
        refusal = "DOM entry function is missing or external";
        return;
    }
    if (intrinsicEntry && functionIndex(target) == 0) {
        refusal = "intrinsic entry requires a named function export";
        return;
    }
    ctjs::FuncOp declaration;
    std::vector<ctjs::FuncOp> callbackFunctions;
    llvm::DenseMap<unsigned, ctjs::FuncOp> indexedCallbacks;
    llvm::DenseSet<unsigned> functionIndices;
    for (mlir::Operation & operation : module.getBody()->getOperations()) {
        if (!spend()) { return; }
        auto function = llvm::dyn_cast<ctjs::FuncOp>(operation);
        if (!function || !functionIndex(function) ||
            !functionIndices.insert(*functionIndex(function)).second ||
            !llvm::hasSingleElement(function.getBody()) || function.getUpvalueCount() != 0 ||
            function->hasAttr("ctjs.skipped") ||
            function.getBody().front().getNumArguments() < ctjs::implicit_arguments) {
            refusal = "DOM entry requires complete single-block functions without captures";
            return;
        }
        if (function != target && functionIndex(function) != 0) {
            if (intrinsicEntry) {
                refusal = "intrinsic entry permits only its named function and inert declaration";
                return;
            }
            auto index = functionIndex(function);
            if (!index || !indexedCallbacks.try_emplace(*index, function).second ||
                function.getBody().front().getNumArguments() != ctjs::implicit_arguments + 1) {
                refusal = "DOM filter callback requires one String parameter and unique identity";
                return;
            }
            callbackFunctions.push_back(function);
        } else if (function != target) {
            if (declaration || functionIndex(function) != 0 ||
                function.getBody().front().getNumArguments() != ctjs::implicit_arguments) {
                refusal = "DOM entry permits only its own source declaration wrapper";
                return;
            }
            declaration = function;
        }
    }
    if (declaration && functionIndex(target) == 0) {
        refusal = "DOM entry has an ambiguous source function identity";
        return;
    }
    auto & targetBlock = target.getBody().front();
    if ((intrinsicEntry ? contract.intrinsicParameters.size()
                        : contract.elementParameters.size()) !=
        targetBlock.getNumArguments() - ctjs::implicit_arguments) {
        refusal = intrinsicEntry ? "intrinsic entry must declare every explicit parameter type"
                                 : "DOM entry must declare every explicit parameter as an element";
        return;
    }
    std::vector<mlir::BlockArgument> provedElements;
    llvm::DenseSet<mlir::Value> provedElementIdentities;
    for (const auto [position, index] : llvm::enumerate(contract.elementParameters)) {
        if (!spend()) { return; }
        if (position != index) {
            refusal = "DOM element_parameters must list every explicit parameter in order";
            return;
        }
        provedElements.push_back(targetBlock.getArgument(index + ctjs::implicit_arguments));
    }
    mlir::BlockArgument documentParameter;
    if (contract.currentDocumentParameter) {
        if (!spend()) { return; }
        if (*contract.currentDocumentParameter >= provedElements.size()) {
            refusal = "DOM current_document_parameter must name a declared element input";
            return;
        }
        documentParameter = provedElements[*contract.currentDocumentParameter];
    }
    llvm::DenseSet<ctjs::LoadGlobalOp> provedDocumentLoads;
    llvm::DenseSet<ctjs::GetPropertyOp> provedDocumentRoots;

    std::vector<mlir::BlockArgument> provedDatasetElements;
    for (unsigned index : contract.datasetParameters) {
        if (!spend()) { return; }
        if (index >= provedElements.size() ||
            (!provedDatasetElements.empty() &&
             index + ctjs::implicit_arguments <= provedDatasetElements.back().getArgNumber())) {
            refusal = "DOM dataset_parameters must be an ordered element subset";
            return;
        }
        provedDatasetElements.push_back(provedElements[index]);
    }

    using Kind = dom_entry_detail::Kind;
    llvm::DenseMap<mlir::Value, HostIntrinsicParameter> provedIntrinsicParameters;
    llvm::DenseMap<mlir::Value, Kind> intrinsicKinds;
    for (const auto [index, parameter] : llvm::enumerate(contract.intrinsicParameters)) {
        if (!spend()) { return; }
        Kind kind;
        switch (parameter) {
        case HostIntrinsicParameter::boolean: kind = Kind::boolean; break;
        case HostIntrinsicParameter::number: kind = Kind::number; break;
        case HostIntrinsicParameter::string: kind = Kind::string; break;
        case HostIntrinsicParameter::symbol: kind = Kind::symbol; break;
        default: refusal = "intrinsic entry parameter has an unsupported category"; return;
        }
        auto argument =
            targetBlock.getArgument(static_cast<unsigned>(index) + ctjs::implicit_arguments);
        provedIntrinsicParameters[argument] = parameter;
        intrinsicKinds[argument] = kind;
    }
    std::vector<ctjs::GetPropertyOp> provedTokens, provedDatasets, provedStringVectorLengths,
        provedStringVectorIndices;
    llvm::DenseSet<ctjs::GetPropertyOp> provedElementVectorLengths, provedElementVectorIndices;
    llvm::DenseSet<ctjs::GetPropertyOp> provedElementPrototypes;
    llvm::DenseSet<ctjs::LoadGlobalOp> provedElementIntrinsics, provedSymbolIntrinsics;
    llvm::DenseMap<ctjs::GetPropertyOp, llvm::StringRef> provedSymbols;
    llvm::DenseSet<ctjs::GetPropertyOp> provedSymbolDescriptions;
    llvm::DenseMap<ctjs::GetPropertyOp, mlir::Value> provedDatasetValues;
    std::vector<ctjs::LoadGlobalOp> provedNumberIntrinsics, provedURIIntrinsics,
        provedJSONIntrinsics, provedObjectIntrinsics, provedRegExpIntrinsics;
    std::vector<ctjs::CallOp> provedPrefixRegExps;
    std::vector<ctjs::InvokeOp> provedInvocations;
    llvm::DenseMap<ctjs::GetPropertyOp, HostDOMMethod> provedMethods;
    std::vector<HostDOMCall> provedCalls;
    std::vector<ctjs::CreateObjectOp> provedJSONObjects;
    std::vector<ctjs::CopyPropsOp> provedJSONCopies;
    std::vector<ctjs::SetPropertyOp> provedJSONAssignments;
    std::vector<ctjs::SetPropertyOp> provedSnapshotAssignments;
    std::vector<mlir::Value> provedOptionalStrings;
    std::vector<HostDOMStringRefinement> provedRefinements;
    std::vector<mlir::Value> provedStrings;
    std::vector<std::pair<mlir::Value, bool>> provedBooleans;
    bool provedUndefinedReturn = false;
    mlir::DominanceInfo dominance(module);
    std::vector<std::pair<ctjs::CreateClosureOp, ctjs::FuncOp>> provedClosures;
    llvm::DenseSet<mlir::Operation *> usedCallbacks;
    if (declaration && !host_detail::isInertEntryDeclaration(declaration, target, spend)) {
        if (refusal.empty()) {
            refusal = "DOM entry wrapper contains observable source operations";
        }
        return;
    }
    const bool suppliedNumber = llvm::is_contained(contract.initialIntrinsics, "Number");
    const bool suppliedURI = llvm::is_contained(contract.initialIntrinsics, "decodeURIComponent");
    const bool suppliedObject = llvm::is_contained(contract.initialIntrinsics, "Object");
    const bool suppliedJSON = llvm::is_contained(contract.initialIntrinsics, "JSON");
    const bool suppliedArray = llvm::is_contained(contract.initialIntrinsics, "Array");
    const bool suppliedString = llvm::is_contained(contract.initialIntrinsics, "String");
    const bool suppliedElement = llvm::is_contained(contract.initialIntrinsics, "Element");
    const bool suppliedFunction = llvm::is_contained(contract.initialIntrinsics, "Function");
    const bool suppliedSymbol = llvm::is_contained(contract.initialIntrinsics, "Symbol");
    const bool suppliedRegExp =
        llvm::is_contained(contract.initialIntrinsics, "RegExp") &&
        llvm::is_contained(contract.initialIntrinsics, "__ctbrowser_regexp");
    const auto emptyString = [](mlir::Value value) {
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        auto string =
            constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue()) : ctjs::StringAttr{};
        return string && string.getValue().empty();
    };
    if (declaration) {
        for (ctjs::StoreGlobalOp store :
             declaration.getBody().front().getOps<ctjs::StoreGlobalOp>()) {
            if (!spend()) { return; }
            if (llvm::is_contained(contract.initialIntrinsics, store.getName()) ||
                (documentParameter && store.getName() == "document")) {
                refusal = "DOM entry source declaration replaces an initial intrinsic";
                return;
            }
        }
    }
    llvm::DenseSet<mlir::Operation *> prefixCallbacks;
    llvm::DenseSet<mlir::Operation *> replacementCallbacks;
    for (ctjs::FuncOp callback : callbackFunctions) {
        if (host_detail::isLowercaseReplacement(callback, spend)) {
            replacementCallbacks.insert(callback);
        }
        if (budgetExhausted) { return; }
    }
    auto functions = callbackFunctions;
    functions.push_back(target);
    for (ctjs::FuncOp function : functions) {
        const bool callbackBody = function != target;
        const bool replacementBody = replacementCallbacks.contains(function);
        auto & block = function.getBody().front();
        llvm::DenseMap<mlir::Value, Kind> values;
        llvm::DenseSet<mlir::Value> increasingIndices;
        unsigned mutationEpoch = 0;
        llvm::DenseMap<mlir::Value, unsigned> datasetEpochs;
        using DatasetOrigin = dom_entry_detail::DatasetOrigin;
        llvm::DenseMap<mlir::Value, DatasetOrigin> snapshotOrigins, keyOrigins;
        // Boolean truth requires the callback input to start with "bs". This
        // implication proves filtered-key uniqueness after removing that prefix.
        llvm::DenseSet<mlir::Value> prefixRequired, prefixSnapshots;
        llvm::DenseMap<mlir::Value, ctjs::GetPropertyOp> strippedAssignmentKeys;
        llvm::DenseMap<mlir::Value, mlir::Value> firstUnits, stringTails, loweredFirstUnits;
        if (replacementBody) {
            auto input = block.getArgument(ctjs::implicit_arguments);
            firstUnits[input] = input;
        }
        using Predicate = dom_entry_detail::Predicate;
        llvm::DenseMap<mlir::Value, mlir::Value> typeQueries;
        llvm::DenseMap<mlir::Value, Predicate> predicates;
        llvm::DenseMap<mlir::Value, bool> constantBooleans;
        llvm::DenseMap<mlir::Value, std::size_t> activeRefinements;
        for (mlir::BlockArgument argument : block.getArguments()) {
            if (!spend()) { return; }
            if (!llvm::isa<ctjs::ValueType>(argument.getType())) {
                refusal = "DOM entry requires original JavaScript parameter types";
                return;
            }
            values[argument] = argument.getArgNumber() < ctjs::implicit_arguments ? Kind::implicit
                               : callbackBody                                     ? Kind::string
                               : intrinsicEntry ? intrinsicKinds.lookup(argument)
                                                : Kind::element;
        }

        // Both arms are checked. Transport preserves invariant kinds and
        // element owner/id pairs. Every admitted effect preserves node lifetime;
        // escaping handles, structural mutation and script reentry still refuse.
        dom_entry_detail::Body visitor{refusal,
                                       budgetExhausted,
                                       block,
                                       provedDatasetElements,
                                       callbackBody,
                                       replacementBody,
                                       suppliedNumber,
                                       suppliedURI,
                                       suppliedObject,
                                       suppliedJSON,
                                       suppliedArray,
                                       suppliedString,
                                       suppliedRegExp,
                                       suppliedElement,
                                       suppliedFunction,
                                       suppliedSymbol,
                                       documentParameter,
                                       provedDocumentLoads,
                                       provedDocumentRoots,
                                       provedUndefinedReturn,
                                       values,
                                       increasingIndices,
                                       prefixRequired,
                                       prefixSnapshots,
                                       mutationEpoch,
                                       datasetEpochs,
                                       snapshotOrigins,
                                       keyOrigins,
                                       strippedAssignmentKeys,
                                       firstUnits,
                                       stringTails,
                                       loweredFirstUnits,
                                       typeQueries,
                                       predicates,
                                       constantBooleans,
                                       activeRefinements,
                                       provedTokens,
                                       provedDatasets,
                                       provedStringVectorLengths,
                                       provedStringVectorIndices,
                                       provedElementVectorLengths,
                                       provedElementVectorIndices,
                                       provedElementPrototypes,
                                       provedElementIntrinsics,
                                       provedSymbolIntrinsics,
                                       provedSymbols,
                                       provedSymbolDescriptions,
                                       provedDatasetValues,
                                       provedNumberIntrinsics,
                                       provedURIIntrinsics,
                                       provedJSONIntrinsics,
                                       provedObjectIntrinsics,
                                       provedRegExpIntrinsics,
                                       provedPrefixRegExps,
                                       provedInvocations,
                                       provedMethods,
                                       provedCalls,
                                       provedJSONObjects,
                                       provedJSONCopies,
                                       provedJSONAssignments,
                                       provedSnapshotAssignments,
                                       provedOptionalStrings,
                                       provedStrings,
                                       provedRefinements,
                                       provedBooleans,
                                       dominance,
                                       provedClosures,
                                       usedCallbacks,
                                       prefixCallbacks,
                                       replacementCallbacks,
                                       indexedCallbacks,
                                       function,
                                       target,
                                       spend,
                                       emptyString};
        mlir::Value frame;
        if (!visitor.visit(block, 0, frame)) { return; }
        for (auto [value, kind] : values) {
            if (!spend()) { return; }
            if (kind == Kind::element || kind == Kind::nullableElement) {
                provedElementIdentities.insert(value);
            }
        }
        if (callbackBody) {
            if (!spend()) { return; }
            auto returned = llvm::cast<ctjs::ReturnOp>(block.getTerminator());
            if (prefixRequired.contains(returned.getValue())) { prefixCallbacks.insert(function); }
        }
    }
    if (usedCallbacks.size() != callbackFunctions.size()) {
        refusal = "DOM entry contains an uninvoked callback function";
        return;
    }
    llvm::DenseMap<mlir::Value, mlir::Value> selectorOrigins;
    for (const auto & call : provedCalls) {
        if (!spend()) { return; }
        if (call.returnsElement() || call.returnsElementVector()) {
            selectorOrigins[call.operation->getResult(0)] = call.element;
        }
    }
    llvm::SmallVector<mlir::Value> provedStyleValues;
    llvm::DenseSet<mlir::Value> seenStyleValues;
    for (auto & call : provedCalls) {
        if (!spend()) { return; }
        if (!call.usesStyle()) { continue; }
        // Follow every incoming edge, including zero-trip initialization and
        // backedges. Cycles grant no authority: the closure must reach exactly
        // original parameters. Different inputs keep distinct Style engines.
        llvm::SmallVector<mlir::Value> pending{call.element};
        llvm::DenseSet<mlir::Value> visited;
        llvm::SmallVector<mlir::BlockArgument> roots;
        llvm::SmallVector<mlir::Value> values;
        const auto append = [&](mlir::Value value) {
            if (!spend()) { return false; }
            pending.push_back(value);
            return true;
        };
        bool complete = true;
        while (!pending.empty()) {
            if (!spend()) { return; }
            auto value = pending.pop_back_val();
            if (!visited.insert(value).second) { continue; }
            values.push_back(value);
            if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(value)) {
                if (argument.getOwner() == &targetBlock &&
                    argument.getArgNumber() >= ctjs::implicit_arguments) {
                    roots.push_back(argument);
                    continue;
                }
                auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(argument.getOwner()->getParentOp());
                if (!loop) {
                    complete = false;
                    break;
                }
                const auto index = argument.getArgNumber();
                if (argument.getOwner() == &loop.getBefore().front()) {
                    auto yield =
                        llvm::cast<mlir::scf::YieldOp>(loop.getAfter().front().getTerminator());
                    if (!append(loop.getInits()[index]) || !append(yield.getOperand(index))) {
                        return;
                    }
                } else {
                    auto condition = llvm::cast<mlir::scf::ConditionOp>(
                        loop.getBefore().front().getTerminator());
                    if (!append(condition.getArgs()[index])) { return; }
                }
                continue;
            }
            if (auto branch = value.getDefiningOp<mlir::scf::IfOp>()) {
                const auto index = llvm::cast<mlir::OpResult>(value).getResultNumber();
                for (mlir::Region & region : branch->getRegions()) {
                    auto yield = llvm::cast<mlir::scf::YieldOp>(region.front().getTerminator());
                    if (!append(yield.getOperand(index))) { return; }
                }
                continue;
            }
            if (auto loop = value.getDefiningOp<mlir::scf::WhileOp>()) {
                auto condition =
                    llvm::cast<mlir::scf::ConditionOp>(loop.getBefore().front().getTerminator());
                const auto index = llvm::cast<mlir::OpResult>(value).getResultNumber();
                if (!append(condition.getArgs()[index])) { return; }
                continue;
            }
            if (auto origin = selectorOrigins.lookup(value)) {
                if (!append(origin)) { return; }
                continue;
            }
            auto read = value.getDefiningOp<ctjs::GetPropertyOp>();
            if (read && provedDocumentRoots.contains(read)) {
                if (!append(documentParameter)) { return; }
            } else if (read && provedElementVectorIndices.contains(read)) {
                if (!append(read.getObject())) { return; }
            } else {
                complete = false;
                break;
            }
        }
        if (!complete || roots.empty()) {
            refusal = "DOM selector requires complete original Style associations";
            return;
        }
        if (roots.size() == 1) {
            call.styleParameter = roots.front();
        } else {
            call.styleParameters = std::move(roots);
            for (auto value : values) {
                if (!spend()) { return; }
                if (seenStyleValues.insert(value).second) { provedStyleValues.push_back(value); }
            }
        }
    }
    if (intrinsicEntry &&
        (!provedJSONObjects.empty() || llvm::any_of(provedCalls, [](const HostDOMCall & call) {
            return !call.returnsSymbol() && call.kind != HostDOMMethod::symbolToString &&
                   call.kind != HostDOMMethod::startsWith &&
                   call.kind != HostDOMMethod::stringCharAt &&
                   call.kind != HostDOMMethod::stringSlice &&
                   call.kind != HostDOMMethod::stringLowercaseUnit;
        }))) {
        refusal =
            "intrinsic entry permits only primitive values and proved Symbol/String operations";
        return;
    }
    // Joins and source spreads copy trees by value. A mutable target must
    // finish every write before any such observation can snapshot it. No
    // member/identity observation or descendant mutation passed the census,
    // so the final single owning result cannot distinguish shallow aliases.
    for (ctjs::CopyPropsOp copy : provedJSONCopies) {
        for (mlir::OpOperand & use : copy.getTarget().getUses()) {
            if (!spend()) { return; }
            auto * user = use.getOwner();
            if (llvm::isa<ctjs::RootOp>(user)) { continue; }
            if (llvm::isa<ctjs::CopyPropsOp>(user) && use.getOperandNumber() == 0) { continue; }
            if (llvm::isa<ctjs::SetPropertyOp>(user) && use.getOperandNumber() == 0) { continue; }
            if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(user);
                unary && unary.getKind() == ctjs::UnaryKind::TypeOf) {
                continue;
            }
            if (!dominance.properlyDominates(copy, user)) {
                refusal = "DOM JSON target is observed before its final spread write";
                return;
            }
        }
    }
    for (ctjs::SetPropertyOp write : provedJSONAssignments) {
        for (mlir::OpOperand & use : write.getObject().getUses()) {
            if (!spend()) { return; }
            auto * user = use.getOwner();
            if (llvm::isa<ctjs::RootOp>(user) ||
                (llvm::isa<ctjs::CopyPropsOp, ctjs::SetPropertyOp>(user) &&
                 use.getOperandNumber() == 0)) {
                continue;
            }
            if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(user);
                unary && unary.getKind() == ctjs::UnaryKind::TypeOf) {
                continue;
            }
            // A copy in the same loop would retain a JavaScript alias that a
            // later iteration can mutate. Only observations after the entire
            // loop may snapshot the owning tree.
            auto * ordered = write.getOperation();
            for (auto * parent = ordered->getParentOp(); parent && !llvm::isa<ctjs::FuncOp>(parent);
                 parent = parent->getParentOp()) {
                if (!spend()) { return; }
                if (llvm::isa<mlir::scf::WhileOp>(parent) && parent->isAncestor(user)) {
                    refusal = "DOM JSON assignment target is observed inside its mutation loop";
                    return;
                }
            }
            // Conditional and repeated writes need not execute, but their
            // entire source region must finish before an owning observation.
            while (!dominance.properlyDominates(ordered, user) && ordered->getParentOp() &&
                   !llvm::isa<ctjs::FuncOp>(ordered->getParentOp())) {
                if (!spend()) { return; }
                ordered = ordered->getParentOp();
                if (ordered->isAncestor(user)) { break; }
            }
            if (ordered->isAncestor(user) || !dominance.properlyDominates(ordered, user)) {
                refusal = "DOM JSON target is observed before its final assignment";
                return;
            }
        }
    }
    optionalStrings = std::move(provedOptionalStrings);
    refinements = std::move(provedRefinements);
    strings = std::move(provedStrings);
    booleans = std::move(provedBooleans);
    checkedCallbacks = std::move(callbackFunctions);
    callbackClosures = std::move(provedClosures);
    checkedEntry = target;
    carriedStyles.assign(provedStyleValues.begin(), provedStyleValues.end());
    undefinedReturn = provedUndefinedReturn;
    checkedWrapper = declaration;
    elements = std::move(provedElements);
    elementIdentities = std::move(provedElementIdentities);
    if (!provedDocumentLoads.empty()) { documentAnchor = documentParameter; }
    documentLoads = std::move(provedDocumentLoads);
    documentRoots = std::move(provedDocumentRoots);
    intrinsicParameters = std::move(provedIntrinsicParameters);
    tokenLists = std::move(provedTokens);
    datasets = std::move(provedDatasets);
    stringVectorLengths = std::move(provedStringVectorLengths);
    stringVectorIndices = std::move(provedStringVectorIndices);
    elementVectorLengths = std::move(provedElementVectorLengths);
    elementVectorIndices = std::move(provedElementVectorIndices);
    elementPrototypes = std::move(provedElementPrototypes);
    elementIntrinsics = std::move(provedElementIntrinsics);
    symbolIntrinsics = std::move(provedSymbolIntrinsics);
    symbols = std::move(provedSymbols);
    symbolDescriptions = std::move(provedSymbolDescriptions);
    datasetValues = std::move(provedDatasetValues);
    stringPrefixRegExps = std::move(provedPrefixRegExps);
    datasetElements = std::move(provedDatasetElements);
    objectIntrinsics = std::move(provedObjectIntrinsics);
    regexpIntrinsics = std::move(provedRegExpIntrinsics);
    numberIntrinsics = std::move(provedNumberIntrinsics);
    uriIntrinsics = std::move(provedURIIntrinsics);
    jsonIntrinsics = std::move(provedJSONIntrinsics);
    invocations = std::move(provedInvocations);
    methods = std::move(provedMethods);
    calls = std::move(provedCalls);
    jsonObjects = std::move(provedJSONObjects);
    jsonCopies = std::move(provedJSONCopies);
    jsonAssignments = std::move(provedJSONAssignments);
    snapshotAssignments = std::move(provedSnapshotAssignments);
}

ctjs::FuncOp DOMEntryAnalysis::callback(ctjs::CreateClosureOp closure) const {
    for (const auto & [made, target] : callbackClosures) {
        if (made == closure) { return target; }
    }
    return {};
}

bool DOMEntryAnalysis::isCallbackParameter(mlir::Value value) const {
    return llvm::any_of(checkedCallbacks, [&](ctjs::FuncOp callback) {
        return callback.getBody().front().getArgument(ctjs::implicit_arguments) == value;
    });
}

std::optional<HostIntrinsicParameter> DOMEntryAnalysis::intrinsicParameter(
    mlir::Value value) const {
    const auto found = intrinsicParameters.find(value);
    if (found == intrinsicParameters.end()) { return {}; }
    return found->second;
}

bool DOMEntryAnalysis::isElement(mlir::Value value) const {
    if (!value) { return false; }
    auto read = value.getDefiningOp<ctjs::GetPropertyOp>();
    return llvm::is_contained(elements, value) || (read && isElementVectorIndex(read));
}

bool DOMEntryAnalysis::isElementIdentity(mlir::Value value) const {
    if (!value) { return false; }
    auto read = value.getDefiningOp<ctjs::GetPropertyOp>();
    return elementIdentities.contains(value) || isElement(value) ||
           (read && isDocumentElement(read)) || llvm::any_of(calls, [&](const HostDOMCall & call) {
               return call.returnsElement() && call.operation->getResult(0) == value;
           });
}

bool DOMEntryAnalysis::isTokenList(mlir::Value value) const {
    return llvm::any_of(tokenLists,
                        [&](ctjs::GetPropertyOp read) { return read.getResult() == value; });
}

bool DOMEntryAnalysis::isDataset(mlir::Value value) const {
    return llvm::any_of(datasets,
                        [&](ctjs::GetPropertyOp read) { return read.getResult() == value; });
}

bool DOMEntryAnalysis::isDatasetElement(mlir::Value value) const {
    return llvm::is_contained(datasetElements, value);
}

bool DOMEntryAnalysis::isElementPrototype(ctjs::GetPropertyOp read) const {
    return elementPrototypes.contains(read);
}

bool DOMEntryAnalysis::isCurrentDocument(ctjs::LoadGlobalOp load) const {
    return documentLoads.contains(load);
}

bool DOMEntryAnalysis::isDocumentElement(ctjs::GetPropertyOp read) const {
    return documentRoots.contains(read);
}

bool DOMEntryAnalysis::isStringVectorLength(ctjs::GetPropertyOp read) const {
    return llvm::is_contained(stringVectorLengths, read);
}

bool DOMEntryAnalysis::isStringVectorIndex(ctjs::GetPropertyOp read) const {
    return llvm::is_contained(stringVectorIndices, read);
}

bool DOMEntryAnalysis::isElementVectorLength(ctjs::GetPropertyOp read) const {
    return elementVectorLengths.contains(read);
}

bool DOMEntryAnalysis::isElementVectorIndex(ctjs::GetPropertyOp read) const {
    return elementVectorIndices.contains(read);
}

mlir::Value DOMEntryAnalysis::datasetValueElement(ctjs::GetPropertyOp read) const {
    return datasetValues.lookup(read);
}

bool DOMEntryAnalysis::isStringPrefixRegExp(ctjs::CallOp call) const {
    return llvm::is_contained(stringPrefixRegExps, call);
}

bool DOMEntryAnalysis::isNumberIntrinsic(ctjs::LoadGlobalOp load) const {
    return llvm::is_contained(numberIntrinsics, load);
}

bool DOMEntryAnalysis::isInitialIntrinsic(ctjs::LoadGlobalOp load) const {
    return isNumberIntrinsic(load) || llvm::is_contained(uriIntrinsics, load) ||
           llvm::is_contained(jsonIntrinsics, load) || llvm::is_contained(objectIntrinsics, load) ||
           llvm::is_contained(regexpIntrinsics, load) || elementIntrinsics.contains(load) ||
           symbolIntrinsics.contains(load);
}

llvm::StringRef DOMEntryAnalysis::wellKnownSymbol(ctjs::GetPropertyOp read) const {
    return symbols.lookup(read);
}

bool DOMEntryAnalysis::isSymbolDescription(ctjs::GetPropertyOp read) const {
    return symbolDescriptions.contains(read);
}

bool DOMEntryAnalysis::invocation(ctjs::InvokeOp operation) const {
    return llvm::is_contained(invocations, operation);
}

std::optional<HostDOMMethod> DOMEntryAnalysis::method(ctjs::GetPropertyOp read) const {
    const auto found = methods.find(read);
    if (found == methods.end()) { return {}; }
    return found->second;
}

const HostDOMCall * DOMEntryAnalysis::call(ctjs::CallOp operation) const {
    for (const auto & call : calls) {
        if (call.operation == operation) { return &call; }
    }
    return nullptr;
}

bool DOMEntryAnalysis::jsonObject(ctjs::CreateObjectOp operation) const {
    return llvm::is_contained(jsonObjects, operation);
}

bool DOMEntryAnalysis::jsonCopy(ctjs::CopyPropsOp operation) const {
    return llvm::is_contained(jsonCopies, operation);
}

bool DOMEntryAnalysis::jsonAssignment(ctjs::SetPropertyOp operation) const {
    return llvm::is_contained(jsonAssignments, operation);
}

bool DOMEntryAnalysis::jsonSnapshotAssignment(ctjs::SetPropertyOp operation) const {
    return llvm::is_contained(snapshotAssignments, operation);
}

} // namespace ctcompile::ctnative
