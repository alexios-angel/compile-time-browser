#include "Analysis.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"

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
    if ((contract.provider != HostContract::Provider::ctbrowserDOM &&
         contract.provider != HostContract::Provider::ctbrowserDOMSession) ||
        contract.elementParameters.empty() || !contract.roots.empty() ||
        !contract.observations.empty() || !contract.absentBindings.empty() ||
        !contract.undefinedBindings.empty() || contract.initialIntrinsics.size() > 3 ||
        llvm::any_of(contract.initialIntrinsics,
                     [&](const auto & name) {
                         return (name != "Number" && name != "decodeURIComponent" &&
                                 name != "JSON") ||
                                !intrinsicNames.insert(name).second;
                     }) ||
        contract.realmGlobalThis || contract.classicScriptRealm ||
        !contract.realmOwnDataProperties.empty()) {
        refusal = "DOM entry requires the isolated ctbrowser-dom-v1 declaration";
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
    ctjs::FuncOp declaration;
    for (mlir::Operation & operation : module.getBody()->getOperations()) {
        if (!spend()) { return; }
        auto function = llvm::dyn_cast<ctjs::FuncOp>(operation);
        if (!function || !llvm::hasSingleElement(function.getBody()) ||
            function.getUpvalueCount() != 0 || function->hasAttr("ctjs.skipped") ||
            function.getBody().front().getNumArguments() < ctjs::implicit_arguments) {
            refusal = "DOM entry requires complete single-block functions without captures";
            return;
        }
        if (function != target) {
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
    if (contract.elementParameters.size() !=
        targetBlock.getNumArguments() - ctjs::implicit_arguments) {
        refusal = "DOM entry must declare every explicit parameter as an element";
        return;
    }
    std::vector<mlir::BlockArgument> provedElements;
    for (const auto [position, index] : llvm::enumerate(contract.elementParameters)) {
        if (!spend()) { return; }
        if (position != index) {
            refusal = "DOM element_parameters must list every explicit parameter in order";
            return;
        }
        provedElements.push_back(targetBlock.getArgument(index + ctjs::implicit_arguments));
    }

    enum class Kind {
        implicit,
        element,
        nullableElement,
        tokenList,
        toggle,
        attribute,
        getAttribute,
        optionalString,
        toggleAttribute,
        hasAttribute,
        removeAttribute,
        contains,
        matches,
        closest,
        numberIntrinsic,
        uriIntrinsic,
        jsonIntrinsic,
        jsonParse,
        json,
        numberToString,
        number,
        string,
        boolean,
        null,
        undefined
    };
    std::vector<ctjs::GetPropertyOp> provedTokens;
    std::vector<ctjs::LoadGlobalOp> provedNumberIntrinsics, provedURIIntrinsics,
        provedJSONIntrinsics;
    std::vector<ctjs::InvokeOp> provedInvocations;
    std::vector<std::pair<ctjs::GetPropertyOp, HostDOMMethod>> provedMethods;
    std::vector<HostDOMCall> provedCalls;
    std::vector<mlir::Value> provedOptionalStrings;
    std::vector<HostDOMStringRefinement> provedRefinements;
    std::vector<mlir::Value> provedStrings;
    mlir::DominanceInfo dominance(target);
    if (declaration && !host_detail::isInertEntryDeclaration(declaration, target, spend)) {
        if (refusal.empty()) {
            refusal = "DOM entry wrapper contains observable source operations";
        }
        return;
    }
    const bool suppliedNumber = llvm::is_contained(contract.initialIntrinsics, "Number");
    const bool suppliedURI = llvm::is_contained(contract.initialIntrinsics, "decodeURIComponent");
    const bool suppliedJSON = llvm::is_contained(contract.initialIntrinsics, "JSON");
    if (declaration) {
        for (ctjs::StoreGlobalOp store :
             declaration.getBody().front().getOps<ctjs::StoreGlobalOp>()) {
            if (!spend()) { return; }
            if (llvm::is_contained(contract.initialIntrinsics, store.getName())) {
                refusal = "DOM entry source declaration replaces an initial intrinsic";
                return;
            }
        }
    }
    {
        auto & block = target.getBody().front();
        llvm::DenseMap<mlir::Value, Kind> values;
        struct Predicate {
            mlir::Value optional;
            bool stringOnTrue;
        };
        llvm::DenseMap<mlir::Value, mlir::Value> typeQueries;
        llvm::DenseMap<mlir::Value, Predicate> predicates;
        llvm::DenseMap<mlir::Value, std::size_t> activeRefinements;
        for (mlir::BlockArgument argument : block.getArguments()) {
            if (!spend()) { return; }
            if (!llvm::isa<ctjs::ValueType>(argument.getType())) {
                refusal = "DOM entry requires original JavaScript parameter types";
                return;
            }
            values[argument] =
                argument.getArgNumber() < ctjs::implicit_arguments ? Kind::implicit : Kind::element;
        }
        const auto hasKind = [&](mlir::Value value, Kind kind) {
            const auto found = values.find(value);
            return found != values.end() && found->second == kind;
        };
        // ponytail: bounded structured branches; loops and early-return CFG
        // need a separate control-flow proof. Both arms are checked, always.
        const auto visit = [&](auto && self, mlir::Block & body, unsigned depth,
                               mlir::Value frame) -> bool {
            if (depth == 64 || (!body.getArguments().empty() && depth != 0 &&
                                !llvm::isa<ctjs::InvokeOp>(body.getParentOp()))) {
                refusal = "DOM entry branch depth or block arguments are unsupported";
                return false;
            }
            bool entered = false, returned = false;
            for (mlir::Operation & operation : body) {
                if (!spend()) { return false; }
                if ((operation.getNumRegions() != 0 &&
                     !llvm::isa<mlir::scf::IfOp, ctjs::InvokeOp>(operation)) ||
                    operation.getNumSuccessors() != 0 || returned) {
                    refusal =
                        "DOM entry does not admit nested control flow or a source continuation";
                    return false;
                }
                for (mlir::OpOperand & use : operation.getOpOperands()) {
                    const mlir::Value operand = use.get();
                    if (!spend()) { return false; }
                    if ((!values.contains(operand) && operand != frame) ||
                        !dominance.dominates(operand, &operation)) {
                        refusal = "DOM entry operand has no preceding local definition";
                        return false;
                    }
                    if (auto found = activeRefinements.find(operand);
                        found != activeRefinements.end() && !llvm::isa<ctjs::RootOp>(operation)) {
                        if (!spend()) { return false; }
                        provedRefinements[found->second].uses.push_back(
                            {&operation, use.getOperandNumber()});
                    }
                }
                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                    if (!hasKind(branch.getCondition(), Kind::boolean) ||
                        !llvm::hasSingleElement(branch.getThenRegion()) ||
                        (!branch.getElseRegion().empty() &&
                         !llvm::hasSingleElement(branch.getElseRegion()))) {
                        refusal = "DOM entry branch lacks a proved Boolean and complete arms";
                        return false;
                    }
                    llvm::SmallVector<Kind> joined;
                    for (mlir::Region & region : branch->getRegions()) {
                        if (region.empty()) { continue; }
                        if (!spend()) { return false; }
                        const bool first = &region == &branch.getThenRegion();
                        mlir::Value refined;
                        const auto restore = llvm::make_scope_exit([&] {
                            if (!refined) { return; }
                            // Look up by key: recursive visits can rehash both maps.
                            values[refined] = Kind::optionalString;
                            activeRefinements.erase(refined);
                        });
                        if (auto predicate = predicates.find(branch.getCondition());
                            predicate != predicates.end() &&
                            hasKind(predicate->second.optional, Kind::optionalString)) {
                            // Reserve save/restore and the new evidence before visiting.
                            if (!spend() || !spend() || !spend()) { return false; }
                            refined = predicate->second.optional;
                            const bool present = first == predicate->second.stringOnTrue;
                            values[refined] = present ? Kind::string : Kind::null;
                            if (present) {
                                activeRefinements[refined] = provedRefinements.size();
                                provedRefinements.push_back({&region.front(), refined, {}});
                            }
                        }
                        if (!self(self, region.front(), depth + 1, frame)) { return false; }
                        auto yielded =
                            llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
                        if (!yielded || yielded.getNumOperands() != branch.getNumResults()) {
                            refusal = "DOM entry branch requires exact scalar yields";
                            return false;
                        }
                        for (auto [index, operand] : llvm::enumerate(yielded.getOperands())) {
                            if (!spend()) { return false; }
                            const auto found = values.find(operand);
                            if (found == values.end()) {
                                refusal = "DOM entry yield lacks a proved value";
                                return false;
                            }
                            const Kind kind = found->second;
                            if (kind != Kind::boolean && kind != Kind::number &&
                                kind != Kind::string && kind != Kind::null &&
                                kind != Kind::optionalString && kind != Kind::undefined &&
                                kind != Kind::json) {
                                refusal =
                                    "DOM entry branch cannot carry a borrowed or callable value";
                                return false;
                            }
                            if (first) {
                                joined.push_back(kind);
                                continue;
                            }
                            if (joined[index] == kind) { continue; }
                            const auto jsonScalar = [](Kind k) {
                                return k == Kind::string || k == Kind::boolean ||
                                       k == Kind::number || k == Kind::null ||
                                       k == Kind::optionalString;
                            };
                            // Primitive arms become owning JSON alternatives. Undefined
                            // and borrowed browser values have no JSON representation.
                            if ((joined[index] == Kind::json && jsonScalar(kind)) ||
                                (jsonScalar(joined[index]) && kind == Kind::json)) {
                                joined[index] = Kind::json;
                                continue;
                            }
                            const auto stringOrNull = [](Kind k) {
                                return k == Kind::string || k == Kind::null ||
                                       k == Kind::optionalString;
                            };
                            if (!stringOrNull(joined[index]) || !stringOrNull(kind)) {
                                refusal = "DOM entry branch has incompatible scalar alternatives";
                                return false;
                            }
                            joined[index] = Kind::optionalString;
                        }
                    }
                    if (joined.size() != branch.getNumResults() ||
                        (branch.getNumResults() && branch.getElseRegion().empty())) {
                        refusal = "DOM entry branch is missing a scalar arm";
                        return false;
                    }
                    for (auto [value, kind] : llvm::zip(branch.getResults(), joined)) {
                        values[value] = kind;
                        if (kind == Kind::optionalString || kind == Kind::null) {
                            provedOptionalStrings.push_back(value);
                        } else if (kind == Kind::string) {
                            provedStrings.push_back(value);
                        }
                    }
                    continue;
                }
                if (auto invocation = llvm::dyn_cast<ctjs::InvokeOp>(operation)) {
                    // A nested invoke may only continue the enclosing success.
                    auto parent = invocation->getParentOfType<ctjs::InvokeOp>();
                    if ((parent && (!llvm::is_contained(provedInvocations, parent) ||
                                    invocation->getParentRegion() != &parent.getNormalBody())) ||
                        invocation.getNumResults() != 1 || !invocation.getBody().hasOneBlock() ||
                        !invocation.getNormalBody().hasOneBlock() ||
                        !invocation.getUnwindBody().hasOneBlock()) {
                        refusal = "DOM URI invocation requires complete unnested continuations";
                        return false;
                    }
                    auto & called = invocation.getBody().front();
                    auto & normal = invocation.getNormalBody().front();
                    auto & caught = invocation.getUnwindBody().front();
                    auto call = called.empty() ? ctjs::CallOp{}
                                               : llvm::dyn_cast<ctjs::CallOp>(called.front());
                    auto exit = called.empty() ? ctjs::InvokeExitOp{}
                                               : llvm::dyn_cast<ctjs::InvokeExitOp>(called.back());
                    if (called.getNumArguments() || !call || !exit || call->getNextNode() != exit ||
                        exit.getNormalResult() != call.getResult() ||
                        !call.getResult().hasOneUse() || !exit.getState().empty() ||
                        normal.getNumArguments() != 1 || caught.getNumArguments() != 1 ||
                        !llvm::isa<ctjs::ValueType>(normal.getArgument(0).getType()) ||
                        !llvm::isa<ctjs::ValueType>(caught.getArgument(0).getType()) ||
                        !llvm::isa<ctjs::ValueType>(invocation.getResult(0).getType()) ||
                        !llvm::isa<ctjs::ValueType>(call.getResult().getType()) ||
                        !caught.getArgument(0).use_empty() ||
                        (!hasKind(call.getCallee(), Kind::uriIntrinsic) &&
                         !hasKind(call.getCallee(), Kind::jsonParse))) {
                        refusal = "DOM URI invocation requires exact call and unused catch payload";
                        return false;
                    }
                    const bool parses = hasKind(call.getCallee(), Kind::jsonParse);
                    // Reserve before nested invokes look this parent up.
                    provedInvocations.push_back(invocation);
                    if (!self(self, called, depth + 1, frame)) { return false; }
                    values[normal.getArgument(0)] = parses ? Kind::json : Kind::string;
                    Kind result = Kind::string;
                    for (mlir::Block * continuation : {&normal, &caught}) {
                        if (!self(self, *continuation, depth + 1, frame)) { return false; }
                        auto yielded =
                            llvm::dyn_cast<ctjs::InvokeYieldOp>(continuation->getTerminator());
                        const bool string = yielded && yielded.getValues().size() == 1 &&
                                            hasKind(yielded.getValues().front(), Kind::string);
                        const bool tree = yielded && yielded.getValues().size() == 1 &&
                                          hasKind(yielded.getValues().front(), Kind::json);
                        if (!string && !tree) {
                            refusal = "DOM URI continuations must both return owning Strings";
                            return false;
                        }
                        if (tree) { result = Kind::json; }
                    }
                    values[invocation.getResult(0)] = result;
                    if (result == Kind::string) {
                        provedStrings.push_back(invocation.getResult(0));
                    }
                    continue;
                }
                if (llvm::isa<ctjs::InvokeExitOp, ctjs::InvokeYieldOp>(operation)) {
                    auto invocation = llvm::dyn_cast<ctjs::InvokeOp>(body.getParentOp());
                    if (!invocation || &operation != body.getTerminator() ||
                        (llvm::isa<ctjs::InvokeExitOp>(operation) !=
                         (&body == &invocation.getBody().front()))) {
                        refusal = "DOM URI completion is outside its original continuation";
                        return false;
                    }
                    returned = true;
                    continue;
                }
                if (llvm::isa<mlir::scf::YieldOp>(operation)) {
                    if (!depth) {
                        refusal = "DOM entry yield is outside a branch";
                        return false;
                    }
                    returned = true;
                    continue;
                }
                if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(operation)) {
                    if (llvm::isa<ctjs::StringAttr>(constant.getValue())) {
                        values[constant.getResult()] = Kind::string;
                    } else if (llvm::isa<ctjs::BooleanAttr>(constant.getValue())) {
                        values[constant.getResult()] = Kind::boolean;
                    } else if (llvm::isa<ctjs::NumberAttr>(constant.getValue())) {
                        values[constant.getResult()] = Kind::number;
                    } else if (llvm::isa<ctjs::NullAttr>(constant.getValue())) {
                        values[constant.getResult()] = Kind::null;
                    } else if (llvm::isa<ctjs::UndefinedAttr>(constant.getValue())) {
                        values[constant.getResult()] = Kind::undefined;
                    } else {
                        refusal = "DOM entry constant has no supported scalar contract";
                        return false;
                    }
                    continue;
                }
                if (auto enter = llvm::dyn_cast<ctjs::FrameEnterOp>(operation)) {
                    if (depth || entered) {
                        refusal = "DOM entry has more than one shadow frame";
                        return false;
                    }
                    entered = true;
                    frame = enter.getContext();
                    continue;
                }
                if (auto root = llvm::dyn_cast<ctjs::RootOp>(operation)) {
                    if (!frame || root.getContext() != frame) {
                        refusal = "DOM entry root is outside its local shadow frame";
                        return false;
                    }
                    continue;
                }
                if (auto exit = llvm::dyn_cast<ctjs::FrameExitOp>(operation)) {
                    if (depth || !frame || exit.getContext() != frame) {
                        refusal = "DOM entry exits an unknown shadow frame";
                        return false;
                    }
                    frame = {};
                    continue;
                }
                if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                    if (depth || frame ||
                        (!hasKind(result.getValue(), Kind::undefined) &&
                         !hasKind(result.getValue(), Kind::boolean) &&
                         !hasKind(result.getValue(), Kind::number) &&
                         !hasKind(result.getValue(), Kind::string) &&
                         !hasKind(result.getValue(), Kind::optionalString) &&
                         !hasKind(result.getValue(), Kind::json))) {
                        refusal =
                            "DOM entry return must be a scalar with no borrowed browser handle";
                        return false;
                    }
                    returned = true;
                    continue;
                }
                if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
                    // The DOM provider fixes this initial binding. The complete
                    // source census admits no replacement or script reentry.
                    if (load.getName() == "undefined") {
                        values[load.getResult()] = Kind::undefined;
                        continue;
                    }
                    if (suppliedURI && load.getName() == "decodeURIComponent") {
                        values[load.getResult()] = Kind::uriIntrinsic;
                        provedURIIntrinsics.push_back(load);
                        continue;
                    }
                    if (suppliedNumber && load.getName() == "Number") {
                        values[load.getResult()] = Kind::numberIntrinsic;
                        provedNumberIntrinsics.push_back(load);
                        continue;
                    }
                    if (suppliedJSON && load.getName() == "JSON") {
                        values[load.getResult()] = Kind::jsonIntrinsic;
                        provedJSONIntrinsics.push_back(load);
                        continue;
                    }
                }
                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                    const auto key = ctjs::constantKey(read.getKey());
                    if (hasKind(read.getObject(), Kind::jsonIntrinsic) && key == "parse") {
                        values[read.getResult()] = Kind::jsonParse;
                        provedMethods.emplace_back(read, HostDOMMethod::jsonParse);
                        continue;
                    }
                    if (suppliedNumber && hasKind(read.getObject(), Kind::number) &&
                        key == "toString") {
                        values[read.getResult()] = Kind::numberToString;
                        provedMethods.emplace_back(read, HostDOMMethod::numberToString);
                        continue;
                    }
                    if (hasKind(read.getObject(), Kind::element) && key == "classList") {
                        values[read.getResult()] = Kind::tokenList;
                        provedTokens.push_back(read);
                        continue;
                    }
                    if (hasKind(read.getObject(), Kind::element) && key == "getAttribute") {
                        values[read.getResult()] = Kind::getAttribute;
                        provedMethods.emplace_back(read, HostDOMMethod::getAttribute);
                        continue;
                    }
                    if (hasKind(read.getObject(), Kind::element) && key == "setAttribute") {
                        values[read.getResult()] = Kind::attribute;
                        provedMethods.emplace_back(read, HostDOMMethod::setAttribute);
                        continue;
                    }
                    if (hasKind(read.getObject(), Kind::element) &&
                        (key == "contains" || key == "matches" || key == "closest")) {
                        values[read.getResult()] = key == "contains"  ? Kind::contains
                                                   : key == "matches" ? Kind::matches
                                                                      : Kind::closest;
                        provedMethods.emplace_back(read, key == "contains" ? HostDOMMethod::contains
                                                         : key == "matches"
                                                             ? HostDOMMethod::matches
                                                             : HostDOMMethod::closest);
                        continue;
                    }
                    if (hasKind(read.getObject(), Kind::element) &&
                        (key == "toggleAttribute" || key == "hasAttribute" ||
                         key == "removeAttribute")) {
                        values[read.getResult()] = key == "toggleAttribute" ? Kind::toggleAttribute
                                                   : key == "hasAttribute"  ? Kind::hasAttribute
                                                                            : Kind::removeAttribute;
                        provedMethods.emplace_back(
                            read, key == "toggleAttribute" ? HostDOMMethod::toggleAttribute
                                  : key == "hasAttribute"  ? HostDOMMethod::hasAttribute
                                                           : HostDOMMethod::removeAttribute);
                        continue;
                    }
                    if (hasKind(read.getObject(), Kind::tokenList) && key == "toggle") {
                        values[read.getResult()] = Kind::toggle;
                        provedMethods.emplace_back(read, HostDOMMethod::toggleClass);
                        continue;
                    }
                    refusal = "DOM property read lacks a proved receiver and supported member";
                    return false;
                }
                if (auto invoke = llvm::dyn_cast<ctjs::CallOp>(operation)) {
                    auto arguments = invoke.getArgs();
                    if (hasKind(invoke.getCallee(), Kind::uriIntrinsic)) {
                        auto parent = llvm::dyn_cast<ctjs::InvokeOp>(invoke->getParentOp());
                        if (!parent || invoke->getParentRegion() != &parent.getBody() ||
                            !hasKind(invoke.getReceiver(), Kind::undefined) ||
                            arguments.size() != 1 || !hasKind(arguments[0], Kind::string)) {
                            refusal = "URI call requires explicit identity, an undefined receiver, "
                                      "one String and preserved failure continuation";
                            return false;
                        }
                        provedCalls.push_back(
                            {invoke, HostDOMMethod::decodeURIComponent, arguments[0]});
                        values[invoke.getResult()] = Kind::string;
                        continue;
                    }
                    if (hasKind(invoke.getCallee(), Kind::jsonParse)) {
                        auto parent = llvm::dyn_cast<ctjs::InvokeOp>(invoke->getParentOp());
                        auto method = invoke.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                        if (!parent || invoke->getParentRegion() != &parent.getBody() || !method ||
                            method.getObject() != invoke.getReceiver() || arguments.size() != 1 ||
                            !hasKind(arguments[0], Kind::string)) {
                            refusal = "JSON.parse call requires the initial JSON receiver, one "
                                      "String and preserved failure continuation";
                            return false;
                        }
                        provedCalls.push_back({invoke, HostDOMMethod::jsonParse, arguments[0]});
                        values[invoke.getResult()] = Kind::json;
                        continue;
                    }
                    if (hasKind(invoke.getCallee(), Kind::numberIntrinsic)) {
                        if (!hasKind(invoke.getReceiver(), Kind::undefined) ||
                            arguments.size() != 1 ||
                            (!hasKind(arguments[0], Kind::string) &&
                             !hasKind(arguments[0], Kind::null) &&
                             !hasKind(arguments[0], Kind::optionalString))) {
                            refusal = "Number call requires its initial builtin, undefined "
                                      "receiver and one String/null input";
                            return false;
                        }
                        provedCalls.push_back({invoke, HostDOMMethod::number, arguments[0]});
                        values[invoke.getResult()] = Kind::number;
                        continue;
                    }
                    auto method = invoke.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                    if (!method || method.getObject() != invoke.getReceiver()) {
                        refusal = "DOM call does not preserve its proved method receiver";
                        return false;
                    }
                    if (hasKind(invoke.getCallee(), Kind::numberToString) && arguments.empty()) {
                        provedCalls.push_back(
                            {invoke, HostDOMMethod::numberToString, invoke.getReceiver()});
                        values[invoke.getResult()] = Kind::string;
                        continue;
                    }
                    const bool contains = hasKind(invoke.getCallee(), Kind::contains);
                    const bool matches = hasKind(invoke.getCallee(), Kind::matches);
                    const bool closest = hasKind(invoke.getCallee(), Kind::closest);
                    if (arguments.size() == 1 &&
                        ((contains && hasKind(arguments[0], Kind::element)) ||
                         ((matches || closest) && hasKind(arguments[0], Kind::string)))) {
                        provedCalls.push_back({invoke,
                                               contains  ? HostDOMMethod::contains
                                               : matches ? HostDOMMethod::matches
                                                         : HostDOMMethod::closest,
                                               invoke.getReceiver()});
                        values[invoke.getResult()] =
                            closest ? Kind::nullableElement : Kind::boolean;
                        continue;
                    }
                    if ((hasKind(invoke.getCallee(), Kind::toggle) ||
                         hasKind(invoke.getCallee(), Kind::toggleAttribute)) &&
                        (arguments.size() == 1 ||
                         (arguments.size() == 2 && (hasKind(arguments[1], Kind::boolean) ||
                                                    hasKind(arguments[1], Kind::undefined)))) &&
                        hasKind(arguments[0], Kind::string)) {
                        const bool classes = hasKind(invoke.getCallee(), Kind::toggle);
                        auto element = classes ? invoke.getReceiver()
                                                     .getDefiningOp<ctjs::GetPropertyOp>()
                                                     .getObject()
                                               : invoke.getReceiver();
                        provedCalls.push_back(
                            {invoke,
                             classes ? HostDOMMethod::toggleClass : HostDOMMethod::toggleAttribute,
                             element});
                        values[invoke.getResult()] = Kind::boolean;
                        continue;
                    }
                    if (hasKind(invoke.getCallee(), Kind::getAttribute) && arguments.size() == 1 &&
                        hasKind(arguments[0], Kind::string)) {
                        provedCalls.push_back(
                            {invoke, HostDOMMethod::getAttribute, invoke.getReceiver()});
                        // Own String or null, with no prototype fallback.
                        values[invoke.getResult()] = Kind::optionalString;
                        continue;
                    }
                    if (hasKind(invoke.getCallee(), Kind::hasAttribute) && arguments.size() == 1 &&
                        hasKind(arguments[0], Kind::string)) {
                        provedCalls.push_back(
                            {invoke, HostDOMMethod::hasAttribute, invoke.getReceiver()});
                        values[invoke.getResult()] = Kind::boolean;
                        continue;
                    }
                    const bool sets = hasKind(invoke.getCallee(), Kind::attribute);
                    if ((sets && arguments.size() == 2 && hasKind(arguments[0], Kind::string) &&
                         (hasKind(arguments[1], Kind::string) ||
                          hasKind(arguments[1], Kind::boolean))) ||
                        (hasKind(invoke.getCallee(), Kind::removeAttribute) &&
                         arguments.size() == 1 && hasKind(arguments[0], Kind::string))) {
                        for (mlir::Operation * user : invoke.getResult().getUsers()) {
                            if (!spend()) { return false; }
                            if (!llvm::isa<ctjs::RootOp>(user)) {
                                refusal = "DOM attribute write result must be unused";
                                return false;
                            }
                        }
                        provedCalls.push_back(
                            {invoke,
                             sets ? HostDOMMethod::setAttribute : HostDOMMethod::removeAttribute,
                             invoke.getReceiver()});
                        values[invoke.getResult()] = Kind::undefined;
                        continue;
                    }
                    refusal = "DOM call arguments lack the supported primitive contract";
                    return false;
                }
                if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(operation);
                    binary &&
                    (binary.getKind() == ctjs::BinaryKind::Add ||
                     binary.getKind() == ctjs::BinaryKind::Concat) &&
                    hasKind(binary.getLhs(), Kind::string) &&
                    hasKind(binary.getRhs(), Kind::string)) {
                    values[binary.getResult()] = Kind::string;
                    continue;
                }
                if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation);
                    unary && unary.getKind() == ctjs::UnaryKind::TypeOf &&
                    (hasKind(unary.getOperand(), Kind::optionalString) ||
                     hasKind(unary.getOperand(), Kind::string) ||
                     hasKind(unary.getOperand(), Kind::null) ||
                     hasKind(unary.getOperand(), Kind::json))) {
                    if (!spend()) { return false; }
                    values[unary.getResult()] = Kind::string;
                    // JSON's "object" includes null and arrays; only the optional
                    // String carrier supplies a two-way narrowing predicate.
                    if (hasKind(unary.getOperand(), Kind::optionalString)) {
                        typeQueries[unary.getResult()] = unary.getOperand();
                    }
                    continue;
                }
                if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation);
                    compare && (compare.getKind() == ctjs::CompareKind::StrictEq ||
                                compare.getKind() == ctjs::CompareKind::Eq)) {
                    const auto elementIdentity = [&](mlir::Value value) {
                        return hasKind(value, Kind::element) ||
                               hasKind(value, Kind::nullableElement);
                    };
                    const auto stringOrNull = [&](mlir::Value value) {
                        return hasKind(value, Kind::string) ||
                               hasKind(value, Kind::optionalString) || hasKind(value, Kind::null);
                    };
                    const bool strings = hasKind(compare.getLhs(), Kind::string) &&
                                         hasKind(compare.getRhs(), Kind::string);
                    const bool strict = compare.getKind() == ctjs::CompareKind::StrictEq;
                    if (strings || (strict && ((elementIdentity(compare.getLhs()) &&
                                                elementIdentity(compare.getRhs())) ||
                                               (stringOrNull(compare.getLhs()) &&
                                                stringOrNull(compare.getRhs()))))) {
                        for (auto [query, literal] :
                             {std::pair{compare.getLhs(), compare.getRhs()},
                              std::pair{compare.getRhs(), compare.getLhs()}}) {
                            if (!spend()) { return false; }
                            const auto found = typeQueries.find(query);
                            const auto name = ctjs::constantKey(literal);
                            if (found != typeQueries.end() &&
                                (name == "string" || name == "object")) {
                                predicates[compare.getResult()] = {found->second, name == "string"};
                            }
                        }
                        values[compare.getResult()] = Kind::boolean;
                        continue;
                    }
                }
                if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(operation);
                    truth && (hasKind(truth.getValue(), Kind::boolean) ||
                              hasKind(truth.getValue(), Kind::number) ||
                              hasKind(truth.getValue(), Kind::string) ||
                              hasKind(truth.getValue(), Kind::optionalString) ||
                              hasKind(truth.getValue(), Kind::null))) {
                    values[truth.getResult()] = Kind::boolean;
                    if (!spend()) { return false; }
                    if (auto found = predicates.find(truth.getValue()); found != predicates.end()) {
                        const Predicate predicate = found->second;
                        predicates[truth.getResult()] = predicate;
                    }
                    continue;
                }
                if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation);
                    unary && unary.getKind() == ctjs::UnaryKind::Not &&
                    (hasKind(unary.getOperand(), Kind::boolean) ||
                     hasKind(unary.getOperand(), Kind::number) ||
                     hasKind(unary.getOperand(), Kind::optionalString) ||
                     hasKind(unary.getOperand(), Kind::string) ||
                     hasKind(unary.getOperand(), Kind::null))) {
                    values[unary.getResult()] = Kind::boolean;
                    if (!spend()) { return false; }
                    if (auto found = predicates.find(unary.getOperand());
                        found != predicates.end()) {
                        const Predicate predicate = found->second;
                        predicates[unary.getResult()] = {predicate.optional,
                                                         !predicate.stringOnTrue};
                    }
                    continue;
                }
                refusal = ("DOM entry operation lacks a typed browser contract: " +
                           operation.getName().getStringRef())
                              .str();
                return false;
            }
            if (!returned) {
                refusal = "DOM entry requires a complete return and exact source declaration";
                return false;
            }
            return true;
        };
        if (!visit(visit, block, 0, {})) { return; }
    }
    optionalStrings = std::move(provedOptionalStrings);
    refinements = std::move(provedRefinements);
    strings = std::move(provedStrings);
    checkedEntry = target;
    checkedWrapper = declaration;
    elements = std::move(provedElements);
    tokenLists = std::move(provedTokens);
    numberIntrinsics = std::move(provedNumberIntrinsics);
    uriIntrinsics = std::move(provedURIIntrinsics);
    jsonIntrinsics = std::move(provedJSONIntrinsics);
    invocations = std::move(provedInvocations);
    methods = std::move(provedMethods);
    calls = std::move(provedCalls);
}

bool DOMEntryAnalysis::isElement(mlir::Value value) const {
    return llvm::is_contained(elements, value);
}

bool DOMEntryAnalysis::isElementIdentity(mlir::Value value) const {
    return isElement(value) || llvm::any_of(calls, [&](const HostDOMCall & call) {
               return call.returnsElement() && call.operation->getResult(0) == value;
           });
}

bool DOMEntryAnalysis::isTokenList(mlir::Value value) const {
    return llvm::any_of(tokenLists,
                        [&](ctjs::GetPropertyOp read) { return read.getResult() == value; });
}

bool DOMEntryAnalysis::isNumberIntrinsic(ctjs::LoadGlobalOp load) const {
    return llvm::is_contained(numberIntrinsics, load);
}

bool DOMEntryAnalysis::isInitialIntrinsic(ctjs::LoadGlobalOp load) const {
    return isNumberIntrinsic(load) || llvm::is_contained(uriIntrinsics, load) ||
           llvm::is_contained(jsonIntrinsics, load);
}

bool DOMEntryAnalysis::invocation(ctjs::InvokeOp operation) const {
    return llvm::is_contained(invocations, operation);
}

std::optional<HostDOMMethod> DOMEntryAnalysis::method(ctjs::GetPropertyOp read) const {
    for (const auto & [candidate, kind] : methods) {
        if (candidate == read) { return kind; }
    }
    return {};
}

const HostDOMCall * DOMEntryAnalysis::call(ctjs::CallOp operation) const {
    for (const auto & call : calls) {
        if (call.operation == operation) { return &call; }
    }
    return nullptr;
}

} // namespace ctcompile::ctnative
