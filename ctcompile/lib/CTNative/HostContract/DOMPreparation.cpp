#include "../Lowering/ClosureLifting/ClosureLifter.h"
#include "../Lowering/Exceptions/Recovery.h"
#include "Analysis.h"
#include "Preparation.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/IRMapping.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctnative {

static llvm::Error normalizeIntrinsicGlobals(mlir::ModuleOp module, llvm::StringRef entry,
                                             unsigned & remaining, unsigned sourceSize) {
    const auto refuse = [](llvm::StringRef reason) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), reason);
    };
    const auto spend = [&](uint64_t cost = 1) {
        if (cost > remaining) { return false; }
        remaining -= static_cast<unsigned>(cost);
        return true;
    };
    const auto undefined = [](mlir::Value value) {
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        return constant && llvm::isa<ctjs::UndefinedAttr>(constant.getValue());
    };
    llvm::DenseMap<unsigned, ctjs::FuncOp> functions;
    ctjs::FuncOp wrapper;
    auto target = module.lookupSymbol<ctjs::FuncOp>(entry);
    for (ctjs::FuncOp function : module.getOps<ctjs::FuncOp>()) {
        const auto index = functionIndex(function);
        if (!spend() || !index || !functions.try_emplace(*index, function).second) {
            return refuse("intrinsic helper function identity is ambiguous or over budget");
        }
        if (*index == 0) { wrapper = function; }
    }
    llvm::StringMap<ctjs::StoreGlobalOp> declarations;
    llvm::SmallVector<ctjs::LoadGlobalOp> loads;
    llvm::DenseMap<unsigned, unsigned> creations;
    const auto scanned = module.walk([&](mlir::Operation * operation) {
        if (!spend()) { return mlir::WalkResult::interrupt(); }
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation)) {
            if (!wrapper || !wrapper.getBody().hasOneBlock() ||
                wrapper.getBody().front().getNumArguments() != ctjs::implicit_arguments ||
                store->getParentOp() != wrapper ||
                !declarations.try_emplace(store.getName(), store).second) {
                return mlir::WalkResult::interrupt();
            }
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) { loads.push_back(load); }
        if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation);
            closure && closure.getFunction() >= 0) {
            ++creations[static_cast<unsigned>(closure.getFunction())];
        }
        return mlir::WalkResult::advance();
    });
    if (scanned.wasInterrupted()) {
        return refuse("intrinsic helper globals require unique wrapper declarations within budget");
    }
    llvm::StringMap<ctjs::FuncOp> helpers;
    for (auto & declaration : declarations) {
        if (!spend()) { return refuse("intrinsic helper work budget exhausted"); }
        auto store = declaration.second;
        auto closure = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
        auto function = closure && closure.getFunction() >= 0
                            ? functions.lookup(static_cast<unsigned>(closure.getFunction()))
                            : ctjs::FuncOp{};
        if (!function || function == wrapper || !function.isPrivate() ||
            store.getName() == "Symbol" || store.getName() == "undefined" ||
            store.getName() != function.getSymName().rsplit('$').first ||
            closure->getParentOp() != wrapper || !closure->isBeforeInBlock(store) ||
            closure.getEnclosingClosure() !=
                wrapper.getBody().front().getArgument(ctjs::arg_callee) ||
            (closure.getEnclosingThis() !=
                 wrapper.getBody().front().getArgument(ctjs::arg_receiver) &&
             !undefined(closure.getEnclosingThis())) ||
            !closure.getUpvalues().empty() ||
            creations.lookup(static_cast<unsigned>(closure.getFunction())) != 1) {
            return refuse("intrinsic helper requires an exact uncaptured declaration");
        }
        if (function == target) { continue; }
        for (mlir::OpOperand & use : closure.getResult().getUses()) {
            if (!spend() || (use.getOwner() != store && !(llvm::isa<ctjs::RootOp>(use.getOwner()) &&
                                                          use.getOperandNumber() == 1))) {
                return refuse("intrinsic helper declaration has an observable closure use");
            }
        }
        helpers[store.getName()] = function;
    }
    for (ctjs::LoadGlobalOp load : loads) {
        if (!spend()) { return refuse("intrinsic helper work budget exhausted"); }
        if (load.getName() == "Symbol" || load.getName() == "undefined") { continue; }
        auto function = helpers.lookup(load.getName());
        if (!function) { return refuse("intrinsic helper reads an unproved global binding"); }
        for (mlir::OpOperand & use : llvm::make_early_inc_range(load.getResult().getUses())) {
            if (!spend()) { return refuse("intrinsic helper work budget exhausted"); }
            auto * operation = use.getOwner();
            if (llvm::isa<ctjs::RootOp>(operation) && use.getOperandNumber() == 1) { continue; }
            if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
                if (use.getOperandNumber() == 2 && direct.getCallee() == function.getSymName() &&
                    undefined(direct.getReceiver()) && undefined(direct.getNewTarget())) {
                    continue;
                }
            } else if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
                       call && use.getOperandNumber() == 0 && undefined(call.getReceiver())) {
                if (!spend(uint64_t(3) + call->getNumOperands())) {
                    return refuse("intrinsic helper call rewrite work budget exhausted");
                }
                mlir::OpBuilder at(call);
                auto direct = ctjs::CallDirectOp::create(
                    at, call.getLoc(), call.getType(),
                    mlir::FlatSymbolRefAttr::get(function.getSymNameAttr()), call.getReceiver(),
                    call.getReceiver(), load.getResult(), call.getArgs(), nullptr, nullptr);
                for (mlir::OpOperand & resultUse :
                     llvm::make_early_inc_range(call.getResult().getUses())) {
                    if (!spend()) { return refuse("intrinsic helper work budget exhausted"); }
                    resultUse.set(direct.getResult());
                }
                call.erase();
                continue;
            }
            return refuse("intrinsic helper global must remain an exact direct callee");
        }
    }
    for (auto & helper : helpers) {
        auto store = declarations.lookup(helper.first());
        // ponytail: one charged complete census per declaration; index global
        // uses if large helper sets exhaust the existing preparation budget.
        if (uint64_t(2) * sourceSize > remaining) {
            return refuse("intrinsic helper declaration work budget exhausted");
        }
        remaining -= 2 * sourceSize;
        if (!closedDeclaration(store, helper.second, module)) {
            return refuse("intrinsic helper declaration is not closed");
        }
    }
    for (ctjs::LoadGlobalOp load : loads) {
        if (!spend()) { return refuse("intrinsic helper work budget exhausted"); }
        if (!helpers.contains(load.getName())) { continue; }
        if (!spend(2)) { return refuse("intrinsic helper load rewrite work budget exhausted"); }
        mlir::OpBuilder at(load);
        auto absent = ctjs::ConstantOp::create(at, load.getLoc(),
                                               ctjs::UndefinedAttr::get(module.getContext()));
        for (mlir::OpOperand & use : llvm::make_early_inc_range(load.getResult().getUses())) {
            if (!spend()) { return refuse("intrinsic helper work budget exhausted"); }
            use.set(absent.getResult());
        }
        load.erase();
    }
    for (auto & helper : helpers) {
        if (!spend()) { return refuse("intrinsic helper work budget exhausted"); }
        auto store = declarations.lookup(helper.first());
        auto closure = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
        store.erase();
        // Preserve every root/frame operation for the existing inert-wrapper
        // proof; only the unobserved callable value becomes its inert enclosure.
        for (mlir::OpOperand & use : llvm::make_early_inc_range(closure.getResult().getUses())) {
            if (!spend()) { return refuse("intrinsic helper work budget exhausted"); }
            use.set(closure.getEnclosingClosure());
        }
        closure.erase();
    }
    if (wrapper && !host_detail::isInertEntryDeclaration(wrapper, target, spend)) {
        return refuse("intrinsic helper wrapper is not an inert declaration");
    }
    return llvm::Error::success();
}

llvm::Error liftDOMClasses(mlir::ModuleOp module, unsigned maxSteps) {
    const auto refuse = [](const llvm::Twine & reason) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), reason);
    };
    // ponytail: quadratic input-size ceiling around the existing finite
    // lift; thread a step budget through its censuses before widening it.
    uint64_t operations = 0, size = 0;
    auto counted = module.walk([&](mlir::Operation * op) {
        ++operations;
        size += uint64_t(1) + op->getNumOperands();
        for (mlir::Region & region : op->getRegions()) {
            for (mlir::Block & block : region) { size += block.getNumArguments(); }
        }
        return operations * size > maxSteps ? mlir::WalkResult::interrupt()
                                            : mlir::WalkResult::advance();
    });
    if (counted.wasInterrupted()) { return refuse("DOM class lifting work budget exhausted"); }
    lowering_detail::closureLifter lifter{module};
    lifter.discardNativeSourceFacts();
    lifter.run();
    for (mlir::Operation * operation : lifter.lifted) {
        auto closure = llvm::cast<ctjs::CreateClosureOp>(operation);
        auto target = lifter.targetOf(closure);
        for (mlir::OpOperand & use : llvm::make_early_inc_range(closure.getResult().getUses())) {
            auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
            if (!call || use.getOperandNumber() != 2 || call.getTarget() != target ||
                !target.getBody().front().getArgument(ctjs::arg_callee).use_empty()) {
                continue;
            }
            // Ordinary closure lifting retains the callee as bookkeeping. Its
            // proved direct target does not observe it; DOM expansion needs only
            // the symbol and still proves every argument and original operation.
            mlir::OpBuilder at(call);
            use.set(ctjs::ConstantOp::create(at, call.getLoc(),
                                             ctjs::UndefinedAttr::get(module.getContext())));
        }
    }
    // Check all callee premises before deleting a child closure can make an
    // enclosing callee argument unused; DenseSet iteration grants no authority.
    for (mlir::Operation * operation : lifter.lifted) {
        auto closure = llvm::cast<ctjs::CreateClosureOp>(operation);
        if (llvm::any_of(closure->getUsers(),
                         [](mlir::Operation * user) { return !llvm::isa<ctjs::RootOp>(user); })) {
            return refuse("DOM class lifted closure retains an observable use");
        }
        for (mlir::Operation * root : llvm::make_early_inc_range(closure->getUsers())) {
            root->erase();
        }
        closure.erase();
    }
    module.walk([](mlir::Operation * op) { removeAttrsWithPrefix(op, "ctnative."); });
    return llvm::Error::success();
}

llvm::Error prepareDOMEntry(mlir::ModuleOp module, HostContract & contract, unsigned maxSteps) {
    const auto refuse = [](const llvm::Twine & reason) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), reason);
    };
    if (contract.provider == HostContract::Provider::ctbrowserIntrinsics) {
        // Exact helpers reuse the DOM normalizer, but not its prototype
        // or initialization premises. Check those boundaries on original source;
        // complete typed/effect proof follows expansion on a private candidate.
        mlir::OwningOpRef<mlir::ModuleOp> prepared;
        unsigned remaining = maxSteps;
        const auto spend = [&] {
            if (!remaining) { return false; }
            --remaining;
            return true;
        };
        bool helpers = false;
        for (mlir::Operation & operation : module.getBody()->getOperations()) {
            if (!spend()) { return refuse("native intrinsic helper work budget exhausted"); }
            auto function = llvm::dyn_cast<ctjs::FuncOp>(operation);
            helpers |=
                function && function.getSymName() != contract.entry && functionIndex(function) != 0;
        }
        mlir::OwningOpRef<mlir::ModuleOp> expanded;
        auto sourceModule = module;
        HostContract sourceContract = contract;
        if (helpers) {
            bool unsupported = false;
            unsigned sourceSize = 0;
            const auto scanned = module.walk([&](mlir::Operation * operation) {
                // Reserve source inspection, clone and original fingerprint.
                uint64_t cost = 3 * (uint64_t(1) + operation->getNumOperands() +
                                     operation->getNumResults() + operation->getAttrs().size());
                for (mlir::Region & region : operation->getRegions()) {
                    for (mlir::Block & block : region) {
                        cost += 3 * (uint64_t(1) + block.getNumArguments());
                    }
                }
                if (cost > remaining) { return mlir::WalkResult::interrupt(); }
                remaining -= static_cast<unsigned>(cost);
                sourceSize += static_cast<unsigned>(cost / 3);
                unsupported |= llvm::isa<ctjs::CreateObjectOp, ctjs::SetPropertyOp>(operation);
                return mlir::WalkResult::advance();
            });
            if (scanned.wasInterrupted()) {
                return refuse("native intrinsic helper work budget exhausted");
            }
            if (contract.moduleSha256 != hostContractFingerprint(module) ||
                module->hasAttr("ctjs.skipped")) {
                return refuse("native intrinsic entry: fingerprint mismatch or incomplete source");
            }
            if (unsupported) { return refuse("native intrinsic helpers require primitive source"); }
            auto target = module.lookupSymbol<ctjs::FuncOp>(contract.entry);
            if (!target) { return refuse("native intrinsic entry function is missing"); }
            for (ctjs::FuncOp function : module.getOps<ctjs::FuncOp>()) {
                if (!spend()) { return refuse("native intrinsic helper work budget exhausted"); }
                if (function == target || functionIndex(function) == 0 ||
                    function.getBody().empty()) {
                    continue;
                }
                for (auto argument :
                     function.getBody().front().getArguments().take_front(std::min<unsigned>(
                         ctjs::implicit_arguments, function.getBody().front().getNumArguments()))) {
                    for (mlir::OpOperand & use : argument.getUses()) {
                        if (!spend()) {
                            return refuse("native intrinsic helper work budget exhausted");
                        }
                        // Only exact callee-slot reads may reach the existing
                        // immutable-cell/call census. Receiver/new.target
                        // observations and capture writes remain unsupported.
                        auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(use.getOwner());
                        if (!llvm::isa<ctjs::RootOp, ctjs::CreateClosureOp>(use.getOwner()) &&
                            !(load && argument.getArgNumber() == ctjs::arg_callee &&
                              use.getOperandNumber() == 0)) {
                            return refuse("native intrinsic helper observes an implicit argument");
                        }
                    }
                }
            }
            expanded = module.clone();
            if (auto error =
                    normalizeIntrinsicGlobals(*expanded, contract.entry, remaining, sourceSize)) {
                return refuse("native intrinsic helper: " + llvm::toString(std::move(error)));
            }
            unsigned expansionSteps = 0;
            if (auto error =
                    expandDOMHelpers(*expanded, contract.entry, remaining, &expansionSteps)) {
                return refuse("native intrinsic helper: " + llvm::toString(std::move(error)));
            }
            remaining -= expansionSteps;
            const auto counted = expanded->walk([&](mlir::Operation * operation) {
                const uint64_t cost = uint64_t(1) + operation->getNumOperands();
                if (cost > remaining) { return mlir::WalkResult::interrupt(); }
                remaining -= static_cast<unsigned>(cost);
                return mlir::WalkResult::advance();
            });
            if (counted.wasInterrupted()) {
                return refuse("native intrinsic helper fingerprint work budget exhausted");
            }
            sourceModule = *expanded;
            sourceContract.moduleSha256 = hostContractFingerprint(sourceModule);
        }
        {
            const DOMEntryAnalysis source(sourceModule, sourceContract, remaining);
            if (!source.proved()) { return refuse("native intrinsic entry: " + source.reason()); }
            remaining -= source.steps();
            const auto counted = sourceModule.walk([&](mlir::Operation * operation) {
                // Reserve the clone, cleanup walks and refreshed fingerprint.
                uint64_t cost = uint64_t(1) + operation->getNumOperands() +
                                operation->getNumResults() + operation->getAttrs().size();
                for (mlir::Region & region : operation->getRegions()) {
                    for (mlir::Block & block : region) {
                        cost += uint64_t(1) + block.getNumArguments();
                    }
                }
                cost *= 4;
                if (cost > remaining) { return mlir::WalkResult::interrupt(); }
                remaining -= static_cast<unsigned>(cost);
                return mlir::WalkResult::advance();
            });
            if (counted.wasInterrupted()) {
                return refuse("native intrinsic entry preparation work budget exhausted");
            }
            prepared = sourceModule.clone();
            if (auto wrapper = source.wrapper()) {
                prepared->lookupSymbol<ctjs::FuncOp>(wrapper.getSymName()).erase();
            }
        }
        prepared->walk([](ctjs::LoadGlobalOp load) {
            if (load.getName() != "undefined") { return; }
            mlir::OpBuilder at(load);
            auto constant = ctjs::ConstantOp::create(at, load.getLoc(),
                                                     ctjs::UndefinedAttr::get(load.getContext()));
            load.getResult().replaceAllUsesWith(constant.getResult());
            load.erase();
        });
        prepared->walk([](mlir::Operation * op) { removeAttrsWithPrefix(op, "ctnative."); });
        prepared->lookupSymbol<ctjs::FuncOp>(contract.entry).setPublic();
        HostContract transformed = contract;
        transformed.moduleSha256 = hostContractFingerprint(*prepared);
        const DOMEntryAnalysis checked(*prepared, transformed, remaining);
        if (!checked.proved()) {
            return refuse("native intrinsic entry preparation: " + checked.reason());
        }
        module->setAttrs((*prepared)->getAttrs());
        module.getBodyRegion().takeBody(prepared->getBodyRegion());
        contract = std::move(transformed);
        return llvm::Error::success();
    }
    if (contract.moduleSha256 != hostContractFingerprint(module) ||
        module->hasAttr("ctjs.skipped")) {
        return refuse("native DOM entry: fingerprint mismatch or incomplete source");
    }
    mlir::OwningOpRef<mlir::ModuleOp> composed(module.clone());
    HostContract transformed = contract;
    if (llvm::is_contained(contract.initialIntrinsics, host_detail::classDefinedIntrinsic)) {
        if (auto error = normalizeDOMClasses(*composed, transformed, maxSteps)) {
            return refuse("native DOM class: " + llvm::toString(std::move(error)));
        }
        if (auto error = liftDOMClasses(*composed, maxSteps)) { return error; }
        transformed.moduleSha256 = hostContractFingerprint(*composed);
    }
    // A handler in the entry is normalized in place. A handler
    // owned by a local helper is normalized first, under the
    // same fingerprinted proof, so helper expansion then
    // inlines one structured invoke.
    llvm::SmallVector<std::string> handlers;
    for (auto function : composed->getOps<ctjs::FuncOp>()) {
        bool hasHandler = false;
        function.walk([&](ctjs::PushHandlerOp) { hasHandler = true; });
        if (hasHandler) { handlers.push_back(function.getSymName().str()); }
    }
    bool entryHandler = llvm::is_contained(handlers, contract.entry);
    llvm::Error sourceError = llvm::Error::success();
    for (const std::string & handler : handlers) {
        if (sourceError) { break; }
        if (handler == contract.entry) {
            auto close = normalizeDOMIteratorClose(*composed, transformed, maxSteps);
            if (!close) {
                sourceError = close.takeError();
                break;
            }
            if (*close) {
                entryHandler = false;
                transformed.moduleSha256 = hostContractFingerprint(*composed);
                continue;
            }
        }
        auto caught = lowering_detail::normalizeDOMCaughtThrow(
            composed->lookupSymbol<ctjs::FuncOp>(handler), maxSteps,
            handler == contract.entry ? llvm::ArrayRef<unsigned>(contract.elementParameters)
                                      : llvm::ArrayRef<unsigned>{});
        if (!caught) {
            sourceError = caught.takeError();
            break;
        }
        if (*caught) {
            if (handler == contract.entry) { entryHandler = false; }
            transformed.moduleSha256 = hostContractFingerprint(*composed);
            continue;
        }
        sourceError = lowering_detail::normalizeDOMURI(*composed, transformed, maxSteps, handler);
        transformed.moduleSha256 = hostContractFingerprint(*composed);
    }
    if (!sourceError && !entryHandler) {
        std::vector<mlir::Value> inactiveFillers;
        sourceError =
            normalizeDOMCustomIteration(*composed, transformed, maxSteps, &inactiveFillers);
        if (!sourceError) {
            sourceError =
                expandDOMHelpers(*composed, contract.entry, maxSteps, nullptr, inactiveFillers);
        }
    }
    if (sourceError) {
        return refuse("native DOM source: " + llvm::toString(std::move(sourceError)));
    }
    transformed.moduleSha256 = hostContractFingerprint(*composed);
    // Helper expansion resolves proved local cells and maps
    // helper formals back to the validated entry parameters.
    if (auto error = normalizeDOMElementGuards(*composed, transformed, maxSteps)) {
        return refuse("native DOM element guard: " + llvm::toString(std::move(error)));
    }
    transformed.moduleSha256 = hostContractFingerprint(*composed);
    if (auto error = normalizeDOMIteration(*composed, transformed, maxSteps)) {
        return refuse("native DOM iteration: " + llvm::toString(std::move(error)));
    }
    transformed.moduleSha256 = hostContractFingerprint(*composed);
    if (auto error = normalizeDOMSnapshotLengths(*composed, transformed, maxSteps)) {
        return refuse("native DOM snapshot: " + llvm::toString(std::move(error)));
    }
    transformed.moduleSha256 = hostContractFingerprint(*composed);
    // An explicit library entry may replace only its proved
    // inert declaration wrapper. Prepare privately, discard
    // supplied native reports, and reprove before publishing
    // the exported function. `source` borrows `composed`, so
    // the swap waits until it is gone.
    mlir::OwningOpRef<mlir::ModuleOp> prepared;
    {
        const DOMEntryAnalysis source(*composed, transformed, maxSteps);
        if (!source.proved()) { return refuse("native DOM entry: " + source.reason()); }
        mlir::IRMapping mapping;
        prepared = llvm::cast<mlir::ModuleOp>((*composed)->clone(mapping));
        // A saved throw cannot reach its structural yield. Keep that padding
        // in the live sibling's proved primitive kind before sparse inference,
        // so an unreachable undefined does not widen an ordinary result.
        for (const auto & padding : source.unreachableYields()) {
            auto * yield = mapping.lookup(padding.operation);
            auto at = mlir::OpBuilder::atBlockBegin(yield->getBlock());
            for (auto [index, value] : llvm::enumerate(padding.values)) {
                auto constant = ctjs::ConstantOp::create(at, yield->getLoc(), value);
                yield->setOperand(static_cast<unsigned>(index), constant.getResult());
            }
        }
        if (auto wrapper = source.wrapper()) {
            prepared->lookupSymbol<ctjs::FuncOp>(wrapper.getSymName()).erase();
        }
        // Make proved callback bodies reachable to sparse dataflow;
        // they still emit as ordinary internal C++ functions.
        for (ctjs::FuncOp callback : source.callbacks()) {
            prepared->lookupSymbol<ctjs::FuncOp>(callback.getSymName()).setPublic();
        }
        // Only the initialized DOM provider and complete source
        // proof authorize this binding. Normalize in the private
        // clone, then reprove it; no VM lookup or C++ global
        // survives emission.
        prepared->walk([](ctjs::LoadGlobalOp load) {
            if (load.getName() != "undefined") { return; }
            mlir::OpBuilder at(load);
            auto constant = ctjs::ConstantOp::create(at, load.getLoc(),
                                                     ctjs::UndefinedAttr::get(load.getContext()));
            load.getResult().replaceAllUsesWith(constant.getResult());
            load.erase();
        });
        // Normalize optional force while the original method
        // proof is available. Token lists omit undefined;
        // Element coerces it to false. The emitter then needs
        // only ordinary Boolean arguments.
        source.entry().walk([&](ctjs::CallOp original) {
            const auto * edge = source.call(original);
            if (!edge || (edge->kind != HostDOMMethod::toggleClass &&
                          edge->kind != HostDOMMethod::toggleAttribute)) {
                return;
            }
            auto call = llvm::cast<ctjs::CallOp>(mapping.lookup(original.getOperation()));
            if (call.getArgs().size() != 2) { return; }
            auto force = call.getArgs()[1].getDefiningOp<ctjs::ConstantOp>();
            if (!force || !llvm::isa<ctjs::UndefinedAttr>(force.getValue())) { return; }
            if (edge->kind == HostDOMMethod::toggleClass) {
                call.getArgsMutable().erase(1);
            } else {
                mlir::OpBuilder at(call);
                auto value = ctjs::ConstantOp::create(
                    at, call.getLoc(), ctjs::BooleanAttr::get(call.getContext(), false));
                call.getArgsMutable().slice(1, 1).assign(value.getResult());
            }
        });
        // Only a complete source proof can resolve defaults. Retain argument
        // producers and the selected arm in place; never evaluate a skipped default.
        for (auto [original, value] : source.constantBooleans()) {
            auto result = mapping.lookup(original);
            mlir::OpBuilder at(result.getDefiningOp());
            mlir::Value constant;
            if (result.getType().isInteger(1)) {
                constant = mlir::arith::ConstantIntOp::create(at, result.getLoc(), value, 1);
            } else {
                constant = ctjs::ConstantOp::create(
                    at, result.getLoc(), ctjs::BooleanAttr::get(module.getContext(), value));
            }
            result.replaceAllUsesWith(constant);
            result.getDefiningOp()->erase();
        }
        prepared->walk<mlir::WalkOrder::PostOrder>([](mlir::scf::IfOp branch) {
            auto condition = branch.getCondition().getDefiningOp<mlir::arith::ConstantIntOp>();
            if (!condition) { return; }
            auto & region = condition.value() ? branch.getThenRegion() : branch.getElseRegion();
            if (region.empty()) {
                branch.erase();
                return;
            }
            auto & body = region.front();
            auto yield = llvm::cast<mlir::scf::YieldOp>(body.getTerminator());
            branch.replaceAllUsesWith(yield.getOperands());
            yield.erase();
            branch->getBlock()->getOperations().splice(branch->getIterator(), body.getOperations());
            branch.erase();
        });
    }
    prepared->walk([](mlir::Operation * op) { removeAttrsWithPrefix(op, "ctnative."); });
    prepared->lookupSymbol<ctjs::FuncOp>(contract.entry).setPublic();

    // The complete DOM proof above establishes the replacement's structure.
    // An earlier CFG-lifting failure describes the original handler body only.
    prepared->lookupSymbol<ctjs::FuncOp>(contract.entry)->removeAttr("ctjs.not_structured");

    transformed.moduleSha256 = hostContractFingerprint(*prepared);
    const DOMEntryAnalysis checked(*prepared, transformed, maxSteps);
    if (!checked.proved()) { return refuse("native DOM entry preparation: " + checked.reason()); }
    module->setAttrs((*prepared)->getAttrs());
    module.getBodyRegion().takeBody(prepared->getBodyRegion());
    contract = std::move(transformed);
    return llvm::Error::success();
}

} // namespace ctcompile::ctnative
