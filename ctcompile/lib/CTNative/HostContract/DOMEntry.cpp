#include "Analysis.h"
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
    if ((contract.provider != HostContract::Provider::ctbrowserDOM &&
         contract.provider != HostContract::Provider::ctbrowserDOMSession) ||
        contract.elementParameters.empty() || !contract.roots.empty() ||
        !contract.observations.empty() || !contract.absentBindings.empty() ||
        !contract.undefinedBindings.empty() || contract.initialIntrinsics.size() > 11 ||
        llvm::any_of(
            contract.initialIntrinsics,
            [&](const auto & name) {
                return (name != "Object" && name != "Number" && name != "decodeURIComponent" &&
                        name != "JSON" && name != "Array" && name != "String" && name != "RegExp" &&
                        name != "__ctbrowser_regexp" && name != "__ctbrowser_for_of_open" &&
                        name != "__ctbrowser_iter_next" && name != "__ctbrowser_iter_close") ||
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
        startsWith,
        replacePrefix,
        charAt,
        slice,
        lowercaseUnit,
        regexpFactory,
        prefixRegExp,
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
        jsonAggregate, // The typeof object alternatives: null, array or object.
        numberToString,
        number,
        string,
        boolean,
        null,
        undefined
    };
    std::vector<ctjs::GetPropertyOp> provedTokens, provedDatasets, provedStringVectorLengths,
        provedStringVectorIndices;
    std::vector<std::pair<ctjs::GetPropertyOp, mlir::Value>> provedDatasetValues;
    std::vector<ctjs::LoadGlobalOp> provedNumberIntrinsics, provedURIIntrinsics,
        provedJSONIntrinsics, provedObjectIntrinsics, provedRegExpIntrinsics;
    std::vector<ctjs::CallOp> provedPrefixRegExps;
    std::vector<ctjs::InvokeOp> provedInvocations;
    std::vector<std::pair<ctjs::GetPropertyOp, HostDOMMethod>> provedMethods;
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
            if (llvm::is_contained(contract.initialIntrinsics, store.getName())) {
                refusal = "DOM entry source declaration replaces an initial intrinsic";
                return;
            }
        }
    }
    llvm::DenseSet<mlir::Operation *> prefixCallbacks;
    auto functions = callbackFunctions;
    functions.push_back(target);
    for (ctjs::FuncOp function : functions) {
        const bool callbackBody = function != target;
        auto & block = function.getBody().front();
        llvm::DenseMap<mlir::Value, Kind> values;
        llvm::DenseSet<mlir::Value> increasingIndices;
        unsigned mutationEpoch = 0;
        llvm::DenseMap<mlir::Value, unsigned> datasetEpochs;
        struct DatasetOrigin {
            mlir::Value element;
            unsigned epoch;
        };
        llvm::DenseMap<mlir::Value, DatasetOrigin> snapshotOrigins, keyOrigins;
        // Boolean truth requires the callback input to start with "bs". This
        // implication proves filtered-key uniqueness after removing that prefix.
        llvm::DenseSet<mlir::Value> prefixRequired, prefixSnapshots;
        llvm::DenseMap<mlir::Value, ctjs::GetPropertyOp> strippedAssignmentKeys;
        llvm::DenseMap<mlir::Value, mlir::Value> firstUnits, stringTails, loweredFirstUnits;
        struct Predicate {
            mlir::Value optional;
            bool stringOnTrue;
        };
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
                                                                                  : Kind::element;
        }
        const auto hasKind = [&](mlir::Value value, Kind kind) {
            const auto found = values.find(value);
            return found != values.end() && found->second == kind;
        };
        // Both arms are checked. Loop-carried values have invariant scalar
        // kinds; indexed snapshots additionally require the exact 0/+1 proof.
        const auto visit = [&](auto && self, mlir::Block & body, unsigned depth,
                               mlir::Value & frame) -> bool {
            if (depth == 64 ||
                (!body.getArguments().empty() && depth != 0 &&
                 !llvm::isa<ctjs::InvokeOp, mlir::scf::WhileOp>(body.getParentOp()))) {
                refusal = "DOM entry branch depth or block arguments are unsupported";
                return false;
            }
            bool entered = false, returned = false;
            for (mlir::Operation & operation : body) {
                if (!spend()) { return false; }
                // Pure callbacks observe only their String argument. No implicit
                // receiver, allocation, global, capture, callback or browser effect.
                if (callbackBody &&
                    !llvm::isa<ctjs::ConstantOp, ctjs::FrameEnterOp, ctjs::FrameExitOp,
                               ctjs::RootOp, ctjs::ReturnOp, ctjs::GetPropertyOp, ctjs::CallOp,
                               ctjs::TruthyOp, ctjs::UnaryOp, mlir::scf::IfOp, mlir::scf::YieldOp>(
                        operation)) {
                    refusal = "DOM filter callback contains an unsupported source effect";
                    return false;
                }
                if ((operation.getNumRegions() != 0 &&
                     !llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp, ctjs::InvokeOp>(operation)) ||
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
                if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(operation)) {
                    if (!loop.getBefore().hasOneBlock() || !loop.getAfter().hasOneBlock()) {
                        refusal = "DOM loop requires complete scalar regions";
                        return false;
                    }
                    auto & before = loop.getBefore().front();
                    auto & after = loop.getAfter().front();
                    auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(before.getTerminator());
                    auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(after.getTerminator());
                    if (!condition || !yield || !llvm::hasSingleElement(after) ||
                        before.getNumArguments() != loop.getInits().size() ||
                        after.getNumArguments() != loop.getNumResults() ||
                        yield.getNumOperands() != loop.getInits().size() ||
                        condition.getArgs().size() != loop.getNumResults()) {
                        refusal = "DOM loop lost scalar state correspondence";
                        return false;
                    }
                    for (auto [argument, initial] :
                         llvm::zip(before.getArguments(), loop.getInits())) {
                        if (!spend()) { return false; }
                        if (!hasKind(initial, Kind::number) && !hasKind(initial, Kind::string) &&
                            !hasKind(initial, Kind::boolean)) {
                            refusal = "DOM loop cannot carry borrowed or unknown values";
                            return false;
                        }
                        values[argument] = values[initial];
                        auto constant = initial.getDefiningOp<ctjs::ConstantOp>();
                        auto zero = constant ? llvm::dyn_cast<ctjs::NumberAttr>(constant.getValue())
                                             : ctjs::NumberAttr{};
                        auto forwarded = llvm::dyn_cast<mlir::BlockArgument>(
                            yield.getOperand(argument.getArgNumber()));
                        if (!zero || zero.getDouble() != 0 || !forwarded ||
                            forwarded.getOwner() != &after) {
                            continue;
                        }
                        const unsigned slot = forwarded.getArgNumber();
                        // The normalizer returns the condition and its entire
                        // continuation tuple together from each selected arm.
                        const auto advances = [&](auto && check, mlir::Value flag, mlir::Value next,
                                                  unsigned level) -> bool {
                            if (!spend() || level == 64) { return false; }
                            if (auto branch = flag.getDefiningOp<mlir::scf::IfOp>()) {
                                auto flagResult = llvm::cast<mlir::OpResult>(flag);
                                for (mlir::Region & region : branch->getRegions()) {
                                    if (!spend() || !region.hasOneBlock()) { return false; }
                                    auto exit = llvm::dyn_cast<mlir::scf::YieldOp>(
                                        region.front().getTerminator());
                                    if (!exit) { return false; }
                                    auto nextResult = llvm::dyn_cast<mlir::OpResult>(next);
                                    const auto value =
                                        nextResult && nextResult.getOwner() == branch
                                            ? exit.getOperand(nextResult.getResultNumber())
                                            : next;
                                    if (!check(check, exit.getOperand(flagResult.getResultNumber()),
                                               value, level + 1)) {
                                        return false;
                                    }
                                }
                                return true;
                            }
                            auto constant = flag.getDefiningOp<mlir::arith::ConstantOp>();
                            auto bit = constant
                                           ? llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue())
                                           : mlir::IntegerAttr{};
                            if (!bit || !bit.getType().isInteger(1)) { return false; }
                            if (!bit.getInt()) { return true; }
                            auto add = next.getDefiningOp<ctjs::BinaryStaticOp>();
                            auto rhs = add ? add.getRhs().getDefiningOp<ctjs::ConstantOp>()
                                           : ctjs::ConstantOp{};
                            auto one = rhs ? llvm::dyn_cast<ctjs::NumberAttr>(rhs.getValue())
                                           : ctjs::NumberAttr{};
                            return add && add.getKind() == ctjs::BinaryKind::Add &&
                                   add.getLhs() == argument && one && one.getDouble() == 1;
                        };
                        if (advances(advances, condition.getCondition(), condition.getArgs()[slot],
                                     0)) {
                            increasingIndices.insert(argument);
                        }
                        if (budgetExhausted) { return false; }
                    }
                    const unsigned loopEpoch = mutationEpoch;
                    if (!self(self, before, depth + 1, frame)) { return false; }
                    if (mutationEpoch != loopEpoch) {
                        refusal = "DOM loop mutation needs a backedge dataset-alias proof";
                        return false;
                    }
                    for (auto [argument, result, value] :
                         llvm::zip(after.getArguments(), loop.getResults(), condition.getArgs())) {
                        if (!spend()) { return false; }
                        if (!hasKind(value, Kind::number) && !hasKind(value, Kind::string) &&
                            !hasKind(value, Kind::boolean)) {
                            refusal = "DOM loop result is not an invariant scalar";
                            return false;
                        }
                        values[argument] = values[result] = values[value];
                        if (values[value] == Kind::string) {
                            provedStrings.push_back(argument);
                            provedStrings.push_back(result);
                        }
                    }
                    if (!self(self, after, depth + 1, frame)) { return false; }
                    for (auto [value, initial] : llvm::zip(yield.getOperands(), loop.getInits())) {
                        if (!spend()) { return false; }
                        if (values[value] != values[initial]) {
                            refusal = "DOM loop changes its scalar state kind";
                            return false;
                        }
                    }
                    continue;
                }
                if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(operation)) {
                    if (!llvm::isa<mlir::scf::WhileOp>(body.getParentOp()) ||
                        !hasKind(condition.getCondition(), Kind::boolean)) {
                        refusal = "DOM loop condition lacks a proved Boolean";
                        return false;
                    }
                    returned = true;
                    continue;
                }
                if (auto constant = llvm::dyn_cast<mlir::arith::ConstantOp>(operation)) {
                    if (!constant.getType().isInteger(1)) {
                        refusal = "DOM completion arithmetic must be a Boolean constant";
                        return false;
                    }
                    values[constant.getResult()] = Kind::boolean;
                    continue;
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
                    mlir::Value thenFrame = frame, elseFrame = frame;
                    const auto known = constantBooleans.find(branch.getCondition());
                    const std::optional<bool> selected = known == constantBooleans.end()
                                                             ? std::nullopt
                                                             : std::optional(known->second);
                    for (mlir::Region & region : branch->getRegions()) {
                        if (region.empty()) { continue; }
                        if (!spend()) { return false; }
                        const bool first = &region == &branch.getThenRegion();
                        mlir::Value refined;
                        Kind previous = Kind::implicit;
                        const auto restore = llvm::make_scope_exit([&] {
                            if (!refined) { return; }
                            // Look up by key: recursive visits can rehash both maps.
                            values[refined] = previous;
                            activeRefinements.erase(refined);
                        });
                        if (auto predicate = predicates.find(branch.getCondition());
                            predicate != predicates.end() &&
                            (hasKind(predicate->second.optional, Kind::optionalString) ||
                             hasKind(predicate->second.optional, Kind::json))) {
                            // Reserve save/restore and the new evidence before visiting.
                            if (!spend() || !spend() || !spend()) { return false; }
                            refined = predicate->second.optional;
                            previous = values[refined];
                            const bool present = first == predicate->second.stringOnTrue;
                            values[refined] = previous == Kind::json
                                                  ? (present ? Kind::jsonAggregate : Kind::json)
                                                  : (present ? Kind::string : Kind::null);
                            if (present && previous == Kind::optionalString) {
                                activeRefinements[refined] = provedRefinements.size();
                                provedRefinements.push_back({&region.front(), refined, {}});
                            }
                        }
                        auto & armFrame = first ? thenFrame : elseFrame;
                        if (!self(self, region.front(), depth + 1, armFrame)) { return false; }
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
                                kind != Kind::json && kind != Kind::jsonAggregate) {
                                refusal =
                                    "DOM entry branch cannot carry a borrowed or callable value";
                                return false;
                            }
                            // Both arms retain the complete typed effect proof, even
                            // when an original argument decides the default branch.
                            if (selected && first != *selected) { continue; }
                            if (first || selected) {
                                joined.push_back(kind);
                                continue;
                            }
                            if (joined[index] == kind) { continue; }
                            const auto json = [](Kind k) {
                                return k == Kind::json || k == Kind::jsonAggregate;
                            };
                            const auto jsonScalar = [](Kind k) {
                                return k == Kind::string || k == Kind::boolean ||
                                       k == Kind::number || k == Kind::null ||
                                       k == Kind::optionalString;
                            };
                            // Primitive arms become owning JSON alternatives. Undefined
                            // and borrowed browser values have no JSON representation.
                            if ((json(joined[index]) && (json(kind) || jsonScalar(kind))) ||
                                (jsonScalar(joined[index]) && json(kind))) {
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
                    if (thenFrame != elseFrame) {
                        refusal = "DOM entry branch has inconsistent shadow frame exits";
                        return false;
                    }
                    frame = thenFrame;
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
                    if (callbackBody && branch.getNumResults()) {
                        auto yes = llvm::cast<mlir::scf::YieldOp>(
                            branch.getThenRegion().front().getTerminator());
                        auto no = llvm::cast<mlir::scf::YieldOp>(
                            branch.getElseRegion().front().getTerminator());
                        for (auto [result, first, second] :
                             llvm::zip(branch.getResults(), yes.getOperands(), no.getOperands())) {
                            if (!spend() || !spend() || !spend()) { return false; }
                            if (prefixRequired.contains(second) &&
                                (prefixRequired.contains(branch.getCondition()) ||
                                 prefixRequired.contains(first))) {
                                prefixRequired.insert(result);
                            }
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
                        if (callbackBody &&
                            !llvm::cast<ctjs::BooleanAttr>(constant.getValue()).getValue()) {
                            prefixRequired.insert(constant.getResult());
                        }
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
                if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(operation)) {
                    values[object.getResult()] = Kind::jsonAggregate;
                    provedJSONObjects.push_back(object);
                    continue;
                }
                if (auto copy = llvm::dyn_cast<ctjs::CopyPropsOp>(operation)) {
                    auto target = copy.getTarget().getDefiningOp<ctjs::CreateObjectOp>();
                    if (!target || target->getBlock() != copy->getBlock() ||
                        copy.getTarget() == copy.getSource() ||
                        !hasKind(copy.getSource(), Kind::jsonAggregate)) {
                        refusal = "DOM JSON spread requires a fresh local target and an "
                                  "object-tagged JSON source";
                        return false;
                    }
                    provedJSONCopies.push_back(copy);
                    continue;
                }
                if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
                    auto constant = write.getKey().getDefiningOp<ctjs::ConstantOp>();
                    auto name = constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue())
                                         : ctjs::StringAttr{};
                    auto target = write.getObject().getDefiningOp<ctjs::CreateObjectOp>();
                    if (!target || write.getObject() == write.getValue() ||
                        (!hasKind(write.getValue(), Kind::string) &&
                         !hasKind(write.getValue(), Kind::optionalString) &&
                         !hasKind(write.getValue(), Kind::null) &&
                         !hasKind(write.getValue(), Kind::boolean) &&
                         !hasKind(write.getValue(), Kind::number) &&
                         !hasKind(write.getValue(), Kind::json) &&
                         !hasKind(write.getValue(), Kind::jsonAggregate))) {
                        refusal = "DOM JSON assignment requires a fresh target and an owning value";
                        return false;
                    }
                    if (!name || name.getValue() == "__proto__") {
                        // The proved transforms have only one __proto__ preimage.
                        // This permits one inherited setter invocation, whose
                        // prototype is unobservable under the final owning data
                        // contract. Ordinary collisions retain ordered overwrites.
                        auto key = write.getKey().getDefiningOp<ctjs::GetPropertyOp>();
                        if (!key) {
                            if (!spend()) { return false; }
                            key = strippedAssignmentKeys.lookup(write.getKey());
                        }
                        auto index = key ? llvm::dyn_cast<mlir::BlockArgument>(key.getKey())
                                         : mlir::BlockArgument{};
                        auto loop = index ? llvm::dyn_cast<mlir::scf::WhileOp>(
                                                index.getOwner()->getParentOp())
                                          : mlir::scf::WhileOp{};
                        auto * snapshot = key ? key.getObject().getDefiningOp() : nullptr;
                        if (!key || !keyOrigins.contains(key.getResult()) ||
                            !increasingIndices.contains(index) || !loop || !snapshot ||
                            loop->isAncestor(snapshot) || loop->isAncestor(target) ||
                            !loop->isAncestor(write)) {
                            refusal = "DOM JSON assignment key requires a constant non-prototype "
                                      "String or one direct snapshot traversal";
                            return false;
                        }
                        for (auto * parent = write->getParentOp(); parent != function;
                             parent = parent->getParentOp()) {
                            if (!spend()) { return false; }
                            if (llvm::isa<mlir::scf::WhileOp>(parent) && parent != loop) {
                                refusal =
                                    "DOM JSON snapshot assignment cannot repeat in another loop";
                                return false;
                            }
                        }
                        for (mlir::OpOperand & use : target.getResult().getUses()) {
                            if (!spend()) { return false; }
                            auto * user = use.getOwner();
                            if (use.getOperandNumber() == 0 && user != write &&
                                llvm::isa<ctjs::SetPropertyOp, ctjs::CopyPropsOp>(user)) {
                                refusal =
                                    "DOM JSON snapshot assignment requires the sole target writer";
                                return false;
                            }
                        }
                        provedSnapshotAssignments.push_back(write);
                    }
                    provedJSONAssignments.push_back(write);
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
                    if (!frame || exit.getContext() != frame) {
                        refusal = "DOM entry exits an unknown shadow frame";
                        return false;
                    }
                    for (auto * parent = exit->getParentOp(); parent != function;
                         parent = parent->getParentOp()) {
                        if (!spend()) { return false; }
                        if (!llvm::isa<mlir::scf::IfOp>(parent)) {
                            refusal = "DOM entry frame exit requires an acyclic conditional path";
                            return false;
                        }
                    }
                    frame = {};
                    continue;
                }
                if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                    if (callbackBody && !hasKind(result.getValue(), Kind::boolean)) {
                        refusal = "DOM filter callback must return a proved Boolean";
                        return false;
                    }
                    if (depth || frame ||
                        (!hasKind(result.getValue(), Kind::undefined) &&
                         !hasKind(result.getValue(), Kind::boolean) &&
                         !hasKind(result.getValue(), Kind::number) &&
                         !hasKind(result.getValue(), Kind::string) &&
                         !hasKind(result.getValue(), Kind::optionalString) &&
                         !hasKind(result.getValue(), Kind::json) &&
                         !hasKind(result.getValue(), Kind::jsonAggregate) &&
                         !hasKind(result.getValue(), Kind::stringVector))) {
                        refusal =
                            "DOM entry return must be a scalar with no borrowed browser handle";
                        return false;
                    }
                    returned = true;
                    if (!callbackBody) {
                        provedUndefinedReturn = hasKind(result.getValue(), Kind::undefined);
                    }
                    continue;
                }
                if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation)) {
                    auto callback =
                        indexedCallbacks.lookup(static_cast<unsigned>(closure.getFunction()));
                    if (closure.getFunctionAttr().getInt() < 0 || !suppliedArray ||
                        !suppliedString || !callback || !closure.getUpvalues().empty() ||
                        closure.getEnclosingClosure() != block.getArgument(ctjs::arg_callee) ||
                        (closure.getEnclosingThis() != block.getArgument(ctjs::arg_receiver) &&
                         !hasKind(closure.getEnclosingThis(), Kind::undefined))) {
                        refusal = "DOM filter callback lacks original intrinsics or a capture-free "
                                  "target";
                        return false;
                    }
                    unsigned calls = 0;
                    for (mlir::OpOperand & use : closure.getResult().getUses()) {
                        if (!spend()) { return false; }
                        if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                        if (!call || use.getOperandNumber() != 2 || call.getArgs().size() != 1 ||
                            !dominance.properlyDominates(closure.getOperation(),
                                                         call.getOperation())) {
                            refusal = "DOM filter callback escapes its exact local invocation";
                            return false;
                        }
                        ++calls;
                    }
                    if (!calls) {
                        refusal = "DOM filter callback has no source invocation";
                        return false;
                    }
                    values[closure.getResult()] = Kind::callback;
                    provedClosures.emplace_back(closure, callback);
                    usedCallbacks.insert(callback);
                    continue;
                }
                if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
                    // The DOM provider fixes this initial binding. The complete
                    // source census admits no replacement or script reentry.
                    if (load.getName() == "undefined") {
                        values[load.getResult()] = Kind::undefined;
                        continue;
                    }
                    if (suppliedString && suppliedRegExp &&
                        load.getName() == "__ctbrowser_regexp") {
                        values[load.getResult()] = Kind::regexpFactory;
                        provedRegExpIntrinsics.push_back(load);
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
                    if (suppliedObject && load.getName() == "Object") {
                        values[load.getResult()] = Kind::objectIntrinsic;
                        provedObjectIntrinsics.push_back(load);
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
                    if (hasKind(read.getObject(), Kind::stringVector) && key == "length") {
                        values[read.getResult()] = Kind::number;
                        provedStringVectorLengths.push_back(read);
                        continue;
                    }
                    if (hasKind(read.getObject(), Kind::stringVector) &&
                        increasingIndices.contains(read.getKey())) {
                        bool guarded = false;
                        for (mlir::Operation * parent = read->getParentOp(); parent != function;
                             parent = parent->getParentOp()) {
                            if (!spend()) { return false; }
                            auto branch = llvm::dyn_cast<mlir::scf::IfOp>(parent);
                            if (!branch ||
                                !branch.getThenRegion().isAncestor(read->getParentRegion())) {
                                continue;
                            }
                            auto truth = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
                            auto compare = truth ? truth.getValue().getDefiningOp<ctjs::CompareOp>()
                                                 : ctjs::CompareOp{};
                            auto length =
                                compare ? compare.getRhs().getDefiningOp<ctjs::GetPropertyOp>()
                                        : ctjs::GetPropertyOp{};
                            guarded |= compare && compare.getKind() == ctjs::CompareKind::Lt &&
                                       compare.getLhs() == read.getKey() && length &&
                                       length.getObject() == read.getObject() &&
                                       llvm::is_contained(provedStringVectorLengths, length);
                        }
                        if (guarded) {
                            if (!spend()) { return false; }
                            if (auto origin = snapshotOrigins.find(read.getObject());
                                origin != snapshotOrigins.end()) {
                                if (!spend()) { return false; }
                                keyOrigins.try_emplace(read.getResult(), origin->second);
                            }
                            values[read.getResult()] = Kind::string;
                            provedStringVectorIndices.push_back(read);
                            provedStrings.push_back(read.getResult());
                            continue;
                        }
                    }
                    if (suppliedArray && hasKind(read.getObject(), Kind::stringVector) &&
                        key == "filter") {
                        values[read.getResult()] = Kind::filterStrings;
                        provedMethods.emplace_back(read, HostDOMMethod::filterStrings);
                        continue;
                    }
                    if (suppliedString && hasKind(read.getObject(), Kind::string) &&
                        key == "startsWith") {
                        values[read.getResult()] = Kind::startsWith;
                        provedMethods.emplace_back(read, HostDOMMethod::startsWith);
                        continue;
                    }
                    if (suppliedString && suppliedRegExp &&
                        hasKind(read.getObject(), Kind::string) && key == "replace") {
                        values[read.getResult()] = Kind::replacePrefix;
                        provedMethods.emplace_back(read, HostDOMMethod::removeStringPrefix);
                        continue;
                    }
                    if (suppliedString && hasKind(read.getObject(), Kind::string) &&
                        (key == "charAt" || key == "slice")) {
                        values[read.getResult()] = key == "charAt" ? Kind::charAt : Kind::slice;
                        provedMethods.emplace_back(read, key == "charAt"
                                                             ? HostDOMMethod::stringCharAt
                                                             : HostDOMMethod::stringSlice);
                        continue;
                    }
                    if (suppliedString && hasKind(read.getObject(), Kind::string) &&
                        key == "toLowerCase" && firstUnits.contains(read.getObject())) {
                        values[read.getResult()] = Kind::lowercaseUnit;
                        provedMethods.emplace_back(read, HostDOMMethod::stringLowercaseUnit);
                        continue;
                    }
                    if (hasKind(read.getObject(), Kind::element) && key == "dataset" &&
                        llvm::is_contained(provedDatasetElements, read.getObject())) {
                        values[read.getResult()] = Kind::dataset;
                        datasetEpochs[read.getResult()] = mutationEpoch;
                        provedDatasets.push_back(read);
                        continue;
                    }
                    if (hasKind(read.getObject(), Kind::dataset)) {
                        if (!spend() || !spend() || !spend()) { return false; }
                        const auto origin = keyOrigins.find(read.getKey());
                        auto dataset = read.getObject().getDefiningOp<ctjs::GetPropertyOp>();
                        // ponytail: only direct snapshot members carry presence.
                        // Joined or transformed keys need a separate provenance proof.
                        if (!dataset || origin == keyOrigins.end() ||
                            origin->second.element != dataset.getObject() ||
                            origin->second.epoch != mutationEpoch ||
                            datasetEpochs.lookup(read.getObject()) != mutationEpoch) {
                            refusal = "DOM dataset value requires a live member key from the same "
                                      "element";
                            return false;
                        }
                        provedDatasetValues.emplace_back(read, dataset.getObject());
                        values[read.getResult()] = Kind::string;
                        provedStrings.push_back(read.getResult());
                        continue;
                    }
                    if (hasKind(read.getObject(), Kind::objectIntrinsic) && key == "keys") {
                        values[read.getResult()] = Kind::objectKeys;
                        provedMethods.emplace_back(read, HostDOMMethod::datasetKeys);
                        continue;
                    }
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
                    if (hasKind(invoke.getCallee(), Kind::regexpFactory)) {
                        if (!hasKind(invoke.getReceiver(), Kind::undefined) ||
                            arguments.size() != 2 || ctjs::constantKey(arguments[0]) != "^bs" ||
                            !emptyString(arguments[1])) {
                            refusal = "DOM prefix removal requires the original /^bs/ literal";
                            return false;
                        }
                        unsigned uses = 0;
                        for (mlir::OpOperand & use : invoke.getResult().getUses()) {
                            if (!spend()) { return false; }
                            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                            auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                            if (!call || use.getOperandNumber() != 2 ||
                                call.getArgs().size() != 2) {
                                refusal = "DOM prefix RegExp escapes its single replacement";
                                return false;
                            }
                            ++uses;
                        }
                        if (uses != 1) {
                            refusal = "DOM prefix RegExp requires one confined replacement";
                            return false;
                        }
                        values[invoke.getResult()] = Kind::prefixRegExp;
                        provedPrefixRegExps.push_back(invoke);
                        continue;
                    }
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
                    if (hasKind(invoke.getCallee(), Kind::charAt) ||
                        hasKind(invoke.getCallee(), Kind::slice)) {
                        const bool first = hasKind(invoke.getCallee(), Kind::charAt);
                        auto literal = arguments.size() == 1
                                           ? arguments[0].getDefiningOp<ctjs::ConstantOp>()
                                           : ctjs::ConstantOp{};
                        auto index = literal ? llvm::dyn_cast<ctjs::NumberAttr>(literal.getValue())
                                             : ctjs::NumberAttr{};
                        // ponytail: Bootstrap's exact indices avoid general
                        // ToIntegerOrInfinity; broader slices need that proof.
                        if (!index || index.getDouble() != (first ? 0 : 1)) {
                            refusal = "DOM String indexing requires charAt(0) or slice(1)";
                            return false;
                        }
                        provedCalls.push_back(
                            {invoke,
                             first ? HostDOMMethod::stringCharAt : HostDOMMethod::stringSlice,
                             invoke.getReceiver()});
                        values[invoke.getResult()] = Kind::string;
                        if (!spend()) { return false; }
                        (first ? firstUnits : stringTails)[invoke.getResult()] =
                            invoke.getReceiver();
                        continue;
                    }
                    if (hasKind(invoke.getCallee(), Kind::lowercaseUnit) && arguments.empty()) {
                        if (!spend()) { return false; }
                        provedCalls.push_back(
                            {invoke, HostDOMMethod::stringLowercaseUnit, invoke.getReceiver()});
                        loweredFirstUnits[invoke.getResult()] =
                            firstUnits.lookup(invoke.getReceiver());
                        values[invoke.getResult()] = Kind::string;
                        continue;
                    }
                    if (hasKind(invoke.getCallee(), Kind::replacePrefix) && arguments.size() == 2 &&
                        hasKind(arguments[0], Kind::prefixRegExp) && emptyString(arguments[1])) {
                        // ponytail: the exact ASCII /^bs/ with no flags needs no
                        // regex engine. Broader patterns require their own proof.
                        // Initial String/RegExp/factory identities fix @@replace,
                        // exec and flag accessors; the census excludes mutation
                        // and reentry, and the literal has no observable state.
                        provedCalls.push_back(
                            {invoke, HostDOMMethod::removeStringPrefix, invoke.getReceiver()});
                        values[invoke.getResult()] = Kind::string;
                        auto key = invoke.getReceiver().getDefiningOp<ctjs::GetPropertyOp>();
                        if (!spend() || !spend()) { return false; }
                        if (key && keyOrigins.contains(key.getResult()) &&
                            prefixSnapshots.contains(key.getObject())) {
                            // Removing a guaranteed prefix is injective. Keep this
                            // authority separate from dataset membership: the new
                            // key does not name the original dataset property.
                            strippedAssignmentKeys[invoke.getResult()] = key;
                        }
                        continue;
                    }
                    if (hasKind(invoke.getCallee(), Kind::filterStrings) && arguments.size() == 1 &&
                        hasKind(arguments[0], Kind::callback)) {
                        if (!spend()) { return false; }
                        if (auto found = snapshotOrigins.find(invoke.getReceiver());
                            found != snapshotOrigins.end()) {
                            if (!spend()) { return false; }
                            const auto origin = found->second;
                            snapshotOrigins.try_emplace(invoke.getResult(), origin);
                        }
                        auto closure = arguments[0].getDefiningOp<ctjs::CreateClosureOp>();
                        auto callback =
                            indexedCallbacks.lookup(static_cast<unsigned>(closure.getFunction()));
                        if (!spend() || !spend()) { return false; }
                        if (prefixCallbacks.contains(callback) ||
                            prefixSnapshots.contains(invoke.getReceiver())) {
                            prefixSnapshots.insert(invoke.getResult());
                        }
                        provedCalls.push_back(
                            {invoke, HostDOMMethod::filterStrings, invoke.getReceiver(), callback});
                        values[invoke.getResult()] = Kind::stringVector;
                        continue;
                    }
                    if (hasKind(invoke.getCallee(), Kind::startsWith) && arguments.size() == 1) {
                        auto prefix = arguments[0].getDefiningOp<ctjs::ConstantOp>();
                        auto text = prefix ? llvm::dyn_cast<ctjs::StringAttr>(prefix.getValue())
                                           : ctjs::StringAttr{};
                        // ponytail: ASCII prefixes make byte and UTF-16 prefix tests
                        // equivalent. General prefixes need code-unit String proof.
                        if (!text) {
                            refusal = "DOM startsWith requires one constant ASCII prefix";
                            return false;
                        }
                        for (unsigned char c : text.getValue()) {
                            if (!spend()) { return false; }
                            if (c > 127) {
                                refusal = "DOM startsWith requires one constant ASCII prefix";
                                return false;
                            }
                        }
                        provedCalls.push_back(
                            {invoke, HostDOMMethod::startsWith, invoke.getReceiver()});
                        values[invoke.getResult()] = Kind::boolean;
                        if (callbackBody &&
                            invoke.getReceiver() == block.getArgument(ctjs::implicit_arguments) &&
                            text.getValue().starts_with("bs")) {
                            prefixRequired.insert(invoke.getResult());
                        }
                        continue;
                    }
                    if (hasKind(invoke.getCallee(), Kind::objectKeys) && arguments.size() == 1 &&
                        hasKind(arguments[0], Kind::dataset)) {
                        if (datasetEpochs.lookup(arguments[0]) != mutationEpoch) {
                            refusal = "DOM dataset enumeration crosses a source mutation";
                            return false;
                        }
                        auto dataset = arguments[0].getDefiningOp<ctjs::GetPropertyOp>();
                        if (!spend()) { return false; }
                        snapshotOrigins.try_emplace(
                            invoke.getResult(), DatasetOrigin{dataset.getObject(), mutationEpoch});
                        provedCalls.push_back(
                            {invoke, HostDOMMethod::datasetKeys, dataset.getObject()});
                        values[invoke.getResult()] = Kind::stringVector;
                        continue;
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
                        ++mutationEpoch;
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
                          hasKind(arguments[1], Kind::boolean) ||
                          hasKind(arguments[1], Kind::optionalString))) ||
                        (hasKind(invoke.getCallee(), Kind::removeAttribute) &&
                         arguments.size() == 1 && hasKind(arguments[0], Kind::string))) {
                        ++mutationEpoch;
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
                if (auto binary = llvm::dyn_cast<ctjs::BinaryStaticOp>(operation);
                    binary && binary.getKind() == ctjs::BinaryKind::Add &&
                    hasKind(binary.getLhs(), Kind::number) &&
                    hasKind(binary.getRhs(), Kind::number)) {
                    values[binary.getResult()] = Kind::number;
                    continue;
                }
                if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(operation);
                    binary && binary.getKind() == ctjs::BinaryKind::Add &&
                    hasKind(binary.getLhs(), Kind::number) &&
                    hasKind(binary.getRhs(), Kind::number)) {
                    values[binary.getResult()] = Kind::number;
                    continue;
                }
                if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation);
                    compare && compare.getKind() == ctjs::CompareKind::Lt &&
                    hasKind(compare.getLhs(), Kind::number) &&
                    hasKind(compare.getRhs(), Kind::number)) {
                    values[compare.getResult()] = Kind::boolean;
                    continue;
                }
                if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(operation);
                    binary &&
                    (binary.getKind() == ctjs::BinaryKind::Add ||
                     binary.getKind() == ctjs::BinaryKind::Concat) &&
                    hasKind(binary.getLhs(), Kind::string) &&
                    hasKind(binary.getRhs(), Kind::string)) {
                    values[binary.getResult()] = Kind::string;
                    if (!spend() || !spend()) { return false; }
                    const auto original = loweredFirstUnits.lookup(binary.getLhs());
                    if (original && stringTails.lookup(binary.getRhs()) == original) {
                        if (!spend()) { return false; }
                        if (auto key = strippedAssignmentKeys.lookup(original)) {
                            // Full lowercase may expand or collide, but only '_'
                            // lowers to '_'. With the unchanged tail, __proto__
                            // still has exactly one original bs__proto__ preimage.
                            // This never grants membership in the source dataset.
                            strippedAssignmentKeys[binary.getResult()] = key;
                        }
                    }
                    continue;
                }
                if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation);
                    unary && unary.getKind() == ctjs::UnaryKind::TypeOf &&
                    (hasKind(unary.getOperand(), Kind::optionalString) ||
                     hasKind(unary.getOperand(), Kind::string) ||
                     hasKind(unary.getOperand(), Kind::null) ||
                     hasKind(unary.getOperand(), Kind::json) ||
                     hasKind(unary.getOperand(), Kind::jsonAggregate))) {
                    if (!spend()) { return false; }
                    values[unary.getResult()] = Kind::string;
                    // JSON's object tag includes null and arrays, never member
                    // authority. It permits only their own-data spread.
                    if (hasKind(unary.getOperand(), Kind::optionalString) ||
                        hasKind(unary.getOperand(), Kind::json)) {
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
                    if (strict && (hasKind(compare.getLhs(), Kind::undefined) ||
                                   hasKind(compare.getRhs(), Kind::undefined))) {
                        const auto scalar = [&](mlir::Value value) {
                            return stringOrNull(value) || hasKind(value, Kind::undefined) ||
                                   hasKind(value, Kind::boolean) || hasKind(value, Kind::number) ||
                                   hasKind(value, Kind::json) ||
                                   hasKind(value, Kind::jsonAggregate);
                        };
                        if (scalar(compare.getLhs()) && scalar(compare.getRhs())) {
                            if (!spend()) { return false; }
                            const bool equal = hasKind(compare.getLhs(), Kind::undefined) &&
                                               hasKind(compare.getRhs(), Kind::undefined);
                            values[compare.getResult()] = Kind::boolean;
                            constantBooleans[compare.getResult()] = equal;
                            provedBooleans.emplace_back(compare.getResult(), equal);
                            continue;
                        }
                    }
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
                            if (hasKind(query, Kind::optionalString) &&
                                hasKind(literal, Kind::null)) {
                                predicates[compare.getResult()] = {query, false};
                            }
                            if (found != typeQueries.end()) {
                                const bool json = hasKind(found->second, Kind::json);
                                if ((!json && (name == "string" || name == "object")) ||
                                    (json && name == "object")) {
                                    predicates[compare.getResult()] = {found->second,
                                                                       json || name == "string"};
                                }
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
                    if (auto found = constantBooleans.find(truth.getValue());
                        found != constantBooleans.end()) {
                        const bool value = found->second;
                        constantBooleans[truth.getResult()] = value;
                        provedBooleans.emplace_back(truth.getResult(), value);
                    }
                    if (auto found = predicates.find(truth.getValue()); found != predicates.end()) {
                        const Predicate predicate = found->second;
                        predicates[truth.getResult()] = predicate;
                    }
                    if (callbackBody) {
                        if (!spend()) { return false; }
                        if (prefixRequired.contains(truth.getValue())) {
                            prefixRequired.insert(truth.getResult());
                        }
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
        mlir::Value frame;
        if (!visit(visit, block, 0, frame)) { return; }
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
    undefinedReturn = provedUndefinedReturn;
    checkedWrapper = declaration;
    elements = std::move(provedElements);
    tokenLists = std::move(provedTokens);
    datasets = std::move(provedDatasets);
    stringVectorLengths = std::move(provedStringVectorLengths);
    stringVectorIndices = std::move(provedStringVectorIndices);
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

bool DOMEntryAnalysis::isDataset(mlir::Value value) const {
    return llvm::any_of(datasets,
                        [&](ctjs::GetPropertyOp read) { return read.getResult() == value; });
}

bool DOMEntryAnalysis::isDatasetElement(mlir::Value value) const {
    return llvm::is_contained(datasetElements, value);
}

bool DOMEntryAnalysis::isStringVectorLength(ctjs::GetPropertyOp read) const {
    return llvm::is_contained(stringVectorLengths, read);
}

bool DOMEntryAnalysis::isStringVectorIndex(ctjs::GetPropertyOp read) const {
    return llvm::is_contained(stringVectorIndices, read);
}

mlir::Value DOMEntryAnalysis::datasetValueElement(ctjs::GetPropertyOp read) const {
    for (const auto & [candidate, element] : datasetValues) {
        if (candidate == read) { return element; }
    }
    return {};
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
           llvm::is_contained(regexpIntrinsics, load);
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
