//===- Entry.cpp - prove a DOM entry function -----------------------------===//
//
// The DOMEntryAnalysis constructor and its queries. The analysis checks that a
// DOM entry function (and its filter callbacks) uses only operations the native
// host can run without script: it validates the host contract, runs
// dom_entry_detail::Body over every function body, then proves Style origins and
// JSON snapshot ordering. Lowering reads the result through the query methods.
//
//===----------------------------------------------------------------------===//

#include "../Analysis.h"
#include "Entry/Body.hpp"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "ctcompile/CTNative/Analysis/WorkBudget.h"

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
                                   unsigned maxSteps, llvm::StringRef inputFingerprint) {
    // Charge for the module walk before anything else: one step per operation
    // plus one per operand (the same cost rule normalizeDOMURI uses), and the
    // same amount again for the fingerprint, which prints the whole module.
    // A budget too small for both refuses before any work, and workSteps
    // records both charges, so steps() is the exact budget that reproduces
    // the proof.
    WorkBudget budget{maxSteps};
    const llvm::scope_exit recordSteps(
        [&] { workSteps = static_cast<unsigned>(maxSteps - budget.remaining); });
    const auto spend = [&](uint64_t cost = 1) {
        if (budget.spend(cost)) { return true; }
        budgetExhausted = true;
        refusal = "DOM entry analysis work budget exhausted";
        return false;
    };
    if (!budget.spendWalk(module,
                          [](mlir::Operation * operation) {
                              return uint64_t(1) + operation->getNumOperands();
                          }) ||
        !spend(maxSteps - budget.remaining)) {
        budgetExhausted = true;
        refusal = "DOM entry analysis work budget exhausted";
        return;
    }
    moduleFingerprint =
        inputFingerprint.empty() ? hostContractFingerprint(module) : inputFingerprint.str();
    if (moduleFingerprint != contract.moduleSha256) {
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
    Evidence proved;
    ctjs::FuncOp declaration;
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
            proved.callbacks.push_back(function);
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
    for (const auto [position, index] : llvm::enumerate(contract.elementParameters)) {
        if (!spend()) { return; }
        if (position != index) {
            refusal = "DOM element_parameters must list every explicit parameter in order";
            return;
        }
        proved.elements.push_back(targetBlock.getArgument(index + ctjs::implicit_arguments));
    }
    mlir::BlockArgument documentParameter;
    if (contract.currentDocumentParameter) {
        if (!spend()) { return; }
        if (*contract.currentDocumentParameter >= proved.elements.size()) {
            refusal = "DOM current_document_parameter must name a declared element input";
            return;
        }
        documentParameter = proved.elements[*contract.currentDocumentParameter];
    }
    for (unsigned index : contract.datasetParameters) {
        if (!spend()) { return; }
        if (index >= proved.elements.size() ||
            (!proved.datasetElements.empty() &&
             index + ctjs::implicit_arguments <= proved.datasetElements.back().getArgNumber())) {
            refusal = "DOM dataset_parameters must be an ordered element subset";
            return;
        }
        proved.datasetElements.push_back(proved.elements[index]);
    }

    using Kind = dom_entry_detail::Kind;
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
        proved.intrinsicParameters[argument] = parameter;
        intrinsicKinds[argument] = kind;
    }
    mlir::DominanceInfo dominance(module);
    llvm::DenseSet<mlir::Operation *> usedCallbacks;
    if (declaration && !host_detail::isInertEntryDeclaration(declaration, target, spend)) {
        if (refusal.empty()) {
            refusal = "DOM entry wrapper contains observable source operations";
        }
        return;
    }
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
    for (ctjs::FuncOp callback : proved.callbacks) {
        if (host_detail::isLowercaseReplacement(callback, spend)) {
            replacementCallbacks.insert(callback);
        }
        if (budgetExhausted) { return; }
    }
    auto functions = proved.callbacks;
    functions.push_back(target);
    for (ctjs::FuncOp function : functions) {
        const bool callbackBody = function != target;
        auto & block = function.getBody().front();
        // The entry function and every filter callback get the same check.
        // Values passed through block arguments keep their proved kind and
        // element identity. Accepted operations never end a node's lifetime;
        // escaping handles, tree mutation and calls back into script are
        // refused.
        dom_entry_detail::Body visitor{refusal,
                                       budgetExhausted,
                                       proved,
                                       intrinsicNames,
                                       documentParameter,
                                       dominance,
                                       usedCallbacks,
                                       prefixCallbacks,
                                       replacementCallbacks,
                                       indexedCallbacks,
                                       spend,
                                       function,
                                       block,
                                       callbackBody,
                                       replacementCallbacks.contains(function)};
        if (visitor.replacementBody) {
            auto input = block.getArgument(ctjs::implicit_arguments);
            visitor.firstUnits[input] = input;
        }
        for (mlir::BlockArgument argument : block.getArguments()) {
            if (!spend()) { return; }
            if (!llvm::isa<ctjs::ValueType>(argument.getType())) {
                refusal = "DOM entry requires original JavaScript parameter types";
                return;
            }
            visitor.values[argument] = argument.getArgNumber() < ctjs::implicit_arguments
                                           ? Kind::implicit
                                       : callbackBody   ? Kind::string
                                       : intrinsicEntry ? intrinsicKinds.lookup(argument)
                                                        : Kind::element;
        }
        mlir::Value frame;
        if (!visitor.visit(block, 0, frame)) { return; }
        for (auto [value, kind] : visitor.values) {
            if (!spend()) { return; }
            if (kind == Kind::element || kind == Kind::nullableElement) {
                proved.elementIdentities.insert(value);
            }
        }
        if (callbackBody) {
            if (!spend()) { return; }
            auto returned = llvm::cast<ctjs::ReturnOp>(block.getTerminator());
            if (visitor.prefixRequired.contains(returned.getValue())) {
                prefixCallbacks.insert(function);
            }
        }
    }
    if (usedCallbacks.size() != proved.callbacks.size()) {
        refusal = "DOM entry contains an uninvoked callback function";
        return;
    }
    llvm::DenseMap<mlir::Value, mlir::Value> selectorOrigins;
    for (const auto & call : proved.calls) {
        if (!spend()) { return; }
        if (call.returnsElement() || call.returnsElementVector()) {
            selectorOrigins[call.operation->getResult(0)] = call.element;
        }
    }
    llvm::DenseSet<mlir::Value> seenStyleValues;
    for (auto & call : proved.calls) {
        if (!spend()) { return; }
        if (!call.usesStyle()) { continue; }
        // Walk every incoming edge back to its source, including zero-trip
        // loop initialization and backedges. A cycle proves nothing by itself:
        // every path must end at an original entry parameter. Selectors whose
        // element comes from several parameters keep one Style engine per
        // parameter.
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
            if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
                argument && argument.getOwner() == &targetBlock &&
                argument.getArgNumber() >= ctjs::implicit_arguments) {
                roots.push_back(argument);
                continue;
            }
            llvm::SmallVector<mlir::Value, 2> inputs;
            if (transportInputs(value, inputs)) {
                for (mlir::Value input : inputs) {
                    if (!append(input)) { return; }
                }
                continue;
            }
            if (auto origin = selectorOrigins.lookup(value)) {
                if (!append(origin)) { return; }
                continue;
            }
            auto read = value.getDefiningOp<ctjs::GetPropertyOp>();
            if (read && proved.documentRoots.contains(read)) {
                if (!append(documentParameter)) { return; }
            } else if (read && proved.elementVectorIndices.contains(read)) {
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
                if (seenStyleValues.insert(value).second) { proved.styles.push_back(value); }
            }
        }
    }
    if (intrinsicEntry &&
        (!proved.jsonObjects.empty() || llvm::any_of(proved.calls, [](const HostDOMCall & call) {
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
    // JSON-like objects are copied by value at joins and spreads, so a
    // mutable target must finish every write before anything can read and
    // snapshot it. The body check above already refused member reads,
    // identity tests and nested mutation, so the single owning result cannot
    // tell shallow aliases apart. Roots, later writes into the target and
    // `typeof` read no member and are allowed anywhere.
    const auto inertJsonUse = [](mlir::OpOperand & use) {
        auto * user = use.getOwner();
        auto unary = llvm::dyn_cast<ctjs::UnaryOp>(user);
        return llvm::isa<ctjs::RootOp>(user) ||
               (llvm::isa<ctjs::CopyPropsOp, ctjs::SetPropertyOp>(user) &&
                use.getOperandNumber() == 0) ||
               (unary && unary.getKind() == ctjs::UnaryKind::TypeOf);
    };
    for (ctjs::CopyPropsOp copy : proved.jsonCopies) {
        for (mlir::OpOperand & use : copy.getTarget().getUses()) {
            if (!spend()) { return; }
            if (inertJsonUse(use)) { continue; }
            if (!dominance.properlyDominates(copy, use.getOwner())) {
                refusal = "DOM JSON target is observed before its final spread write";
                return;
            }
        }
    }
    for (ctjs::SetPropertyOp write : proved.jsonAssignments) {
        for (mlir::OpOperand & use : write.getObject().getUses()) {
            if (!spend()) { return; }
            if (inertJsonUse(use)) { continue; }
            auto * user = use.getOwner();
            // A copy in the same loop would keep a JavaScript alias that a
            // later iteration can mutate. Only reads after the whole loop may
            // snapshot the owning tree.
            auto * ordered = write.getOperation();
            for (auto * parent = ordered->getParentOp(); parent && !llvm::isa<ctjs::FuncOp>(parent);
                 parent = parent->getParentOp()) {
                if (!spend()) { return; }
                if (llvm::isa<mlir::scf::WhileOp>(parent) && parent->isAncestor(user)) {
                    refusal = "DOM JSON assignment target is observed inside its mutation loop";
                    return;
                }
            }
            // Conditional and repeated writes may not run at all, but the
            // whole region holding them must finish before a snapshotting read.
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
    proved.entry = target;
    proved.wrapper = declaration;
    if (!proved.documentLoads.empty()) { proved.document = documentParameter; }
    proof = std::move(proved);
}

ctjs::FuncOp DOMEntryAnalysis::callback(ctjs::CreateClosureOp closure) const {
    for (const auto & [made, target] : proof.closures) {
        if (made == closure) { return target; }
    }
    return {};
}

bool DOMEntryAnalysis::isCallbackParameter(mlir::Value value) const {
    return llvm::any_of(proof.callbacks, [&](ctjs::FuncOp callback) {
        return callback.getBody().front().getArgument(ctjs::implicit_arguments) == value;
    });
}

std::optional<HostIntrinsicParameter> DOMEntryAnalysis::intrinsicParameter(
    mlir::Value value) const {
    const auto found = proof.intrinsicParameters.find(value);
    if (found == proof.intrinsicParameters.end()) { return {}; }
    return found->second;
}

bool DOMEntryAnalysis::isElement(mlir::Value value) const {
    if (!value) { return false; }
    auto read = value.getDefiningOp<ctjs::GetPropertyOp>();
    return llvm::is_contained(proof.elements, value) || (read && isElementVectorIndex(read));
}

bool DOMEntryAnalysis::isElementIdentity(mlir::Value value) const {
    if (!value) { return false; }
    auto read = value.getDefiningOp<ctjs::GetPropertyOp>();
    return proof.elementIdentities.contains(value) || isElement(value) ||
           (read && isDocumentElement(read)) ||
           llvm::any_of(proof.calls, [&](const HostDOMCall & call) {
               return call.returnsElement() && call.operation->getResult(0) == value;
           });
}

bool DOMEntryAnalysis::isTokenList(mlir::Value value) const {
    return llvm::any_of(proof.tokenLists,
                        [&](ctjs::GetPropertyOp read) { return read.getResult() == value; });
}

bool DOMEntryAnalysis::isDataset(mlir::Value value) const {
    return llvm::any_of(proof.datasets,
                        [&](ctjs::GetPropertyOp read) { return read.getResult() == value; });
}

bool DOMEntryAnalysis::isDatasetElement(mlir::Value value) const {
    return llvm::is_contained(proof.datasetElements, value);
}

bool DOMEntryAnalysis::isElementPrototype(ctjs::GetPropertyOp read) const {
    return proof.elementPrototypes.contains(read);
}

bool DOMEntryAnalysis::isCurrentDocument(ctjs::LoadGlobalOp load) const {
    return proof.documentLoads.contains(load);
}

bool DOMEntryAnalysis::isDocumentElement(ctjs::GetPropertyOp read) const {
    return proof.documentRoots.contains(read);
}

bool DOMEntryAnalysis::isStringVectorLength(ctjs::GetPropertyOp read) const {
    return llvm::is_contained(proof.stringVectorLengths, read);
}

bool DOMEntryAnalysis::isStringVectorIndex(ctjs::GetPropertyOp read) const {
    return llvm::is_contained(proof.stringVectorIndices, read);
}

bool DOMEntryAnalysis::isElementVectorLength(ctjs::GetPropertyOp read) const {
    return proof.elementVectorLengths.contains(read);
}

bool DOMEntryAnalysis::isElementVectorIndex(ctjs::GetPropertyOp read) const {
    return proof.elementVectorIndices.contains(read);
}

mlir::Value DOMEntryAnalysis::datasetValueElement(ctjs::GetPropertyOp read) const {
    return proof.datasetValues.lookup(read);
}

bool DOMEntryAnalysis::isStringPrefixRegExp(ctjs::CallOp call) const {
    return llvm::is_contained(proof.stringPrefixRegExps, call);
}

bool DOMEntryAnalysis::isNumberIntrinsic(ctjs::LoadGlobalOp load) const {
    return proof.intrinsics.contains(load) && load.getName() == "Number";
}

bool DOMEntryAnalysis::isInitialIntrinsic(ctjs::LoadGlobalOp load) const {
    return proof.intrinsics.contains(load);
}

llvm::StringRef DOMEntryAnalysis::wellKnownSymbol(ctjs::GetPropertyOp read) const {
    return proof.symbols.lookup(read);
}

bool DOMEntryAnalysis::isSymbolDescription(ctjs::GetPropertyOp read) const {
    return proof.symbolDescriptions.contains(read);
}

bool DOMEntryAnalysis::invocation(ctjs::InvokeOp operation) const {
    return llvm::is_contained(proof.invocations, operation);
}

std::optional<HostDOMMethod> DOMEntryAnalysis::method(ctjs::GetPropertyOp read) const {
    const auto found = proof.methods.find(read);
    if (found == proof.methods.end()) { return {}; }
    return found->second;
}

const HostDOMCall * DOMEntryAnalysis::call(ctjs::CallOp operation) const {
    for (const auto & call : proof.calls) {
        if (call.operation == operation) { return &call; }
    }
    return nullptr;
}

bool DOMEntryAnalysis::jsonObject(ctjs::CreateObjectOp operation) const {
    return llvm::is_contained(proof.jsonObjects, operation);
}

bool DOMEntryAnalysis::jsonCopy(ctjs::CopyPropsOp operation) const {
    return llvm::is_contained(proof.jsonCopies, operation);
}

bool DOMEntryAnalysis::jsonAssignment(ctjs::SetPropertyOp operation) const {
    return llvm::is_contained(proof.jsonAssignments, operation);
}

bool DOMEntryAnalysis::jsonSnapshotAssignment(ctjs::SetPropertyOp operation) const {
    return llvm::is_contained(proof.snapshotAssignments, operation);
}

bool DOMEntryAnalysis::recordObject(ctjs::CreateObjectOp operation) const {
    return llvm::is_contained(proof.records, operation);
}

bool DOMEntryAnalysis::typedOperation(mlir::Operation * operation) const {
    if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
        return isInitialIntrinsic(load) || isCurrentDocument(load);
    }
    if (auto invoke = llvm::dyn_cast<ctjs::InvokeOp>(operation)) { return invocation(invoke); }
    auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
    return call && (this->call(call) || isStringPrefixRegExp(call));
}

} // namespace ctcompile::ctnative
