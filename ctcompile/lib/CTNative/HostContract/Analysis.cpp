#include "Analysis.h"

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "llvm/ADT/ScopeExit.h"

namespace ctcompile::ctnative::host_detail {

std::string analyzer::environmentProblem() {
    std::string reason;
    const auto reject = [&](llvm::StringRef why) {
        if (reason.empty()) { reason = why.str(); }
    };
    if (ambiguousFunctions) { reject("ambiguous numeric source function identities"); }
    for (const std::string & name : contract.absentBindings) {
        if (!globals[name].empty()) { reject("an absent host binding has a source write"); }
    }
    for (const std::string & name : contract.undefinedBindings) {
        if (!globals[name].empty()) { reject("a fixed undefined host binding has a source write"); }
    }
    module.walk([&](mlir::Operation * operation) {
        if (!step() || !active(operation)) { return; }
        if (auto function = llvm::dyn_cast<ctjs::FuncOp>(operation)) {
            if (function.getBody().empty()) { reject("external function provider is unsupported"); }
            return;
        }
        if (operation->hasAttr("ctjs.skipped")) { reject("source contains unimported functions"); }
        if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(operation)) {
            if (!callable(made.getResult())) {
                reject("closure lacks an exact source function identity");
            }
            return;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
            const auto & definitions = globals[load.getName()];
            if (definitions.empty() &&
                !llvm::is_contained(contract.absentBindings, load.getName()) &&
                !llvm::is_contained(contract.undefinedBindings, load.getName())) {
                reject(("unproved host binding `" + load.getName() + "`").str());
            }
            if (llvm::is_contained(contract.absentBindings, load.getName())) {
                for (mlir::Operation * user : load.getResult().getUsers()) {
                    auto unary = llvm::dyn_cast<ctjs::UnaryOp>(user);
                    if (active(user) && (!unary || unary.getKind() != ctjs::UnaryKind::TypeOf)) {
                        reject("absent binding is read outside typeof");
                    }
                }
            } else if (!definitions.empty() &&
                       !llvm::any_of(definitions, [&](ctjs::StoreGlobalOp store) {
                           return before(store, load);
                       })) {
                reject("global read lacks definite source initialization");
            }
            return;
        }
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
            if (!exactCall(call)) { reject("direct call lacks exact source callable identity"); }
            return;
        }
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
            if (!ordinaryKey(keyOf(get.getKey()))) {
                reject("dynamic/prototype property read is unsupported");
            }
            if (!object(get.getObject())) {
                reject("property receiver lacks a fresh own-data object proof");
            }
            return;
        }
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
            if (!ordinaryKey(keyOf(set.getKey()))) {
                reject("dynamic/prototype property write is unsupported");
            }
            if (!object(set.getObject())) {
                reject("property receiver lacks a fresh own-data object proof");
            }
            return;
        }
        if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation)) {
            if (unary.getKind() != ctjs::UnaryKind::TypeOf &&
                unary.getKind() != ctjs::UnaryKind::Not &&
                unary.getKind() != ctjs::UnaryKind::Void) {
                reject("unproved conversion behavior is unsupported");
            }
            return;
        }
        if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
            if (compare.getKind() != ctjs::CompareKind::StrictEq &&
                !primitive(compare.getResult())) {
                reject("unproved comparison behavior is unsupported");
            }
            return;
        }
        if (llvm::isa<mlir::ModuleOp, ctjs::ConstantOp, ctjs::CreateObjectOp, ctjs::CreateClosureOp,
                      ctjs::StoreGlobalOp, ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::ReturnOp,
                      ctjs::RootOp, ctjs::TruthyOp, mlir::scf::IfOp, mlir::scf::YieldOp>(
                operation) ||
            operation->getName().getDialectNamespace() == "arith" ||
            operation->getName().getStringRef() == "ub.poison") {
            return;
        }
        reject(
            ("unsupported provider behavior through `" + operation->getName().getStringRef() + "`")
                .str());
    });
    return reason;
}

} // namespace ctcompile::ctnative::host_detail

namespace ctcompile::ctnative {

HostContractAnalysis::HostContractAnalysis(mlir::ModuleOp module, const HostContract & contract,
                                           unsigned maxSteps) {
    // A fresh instance ignores all old/forged reports, including a forged
    // success flag. Its supplied contract is bound to the actual current IR.
    if (hostContractFingerprint(module) != contract.moduleSha256) {
        refusal = "host contract module fingerprint mismatch";
        return;
    }
    host_detail::analyzer analysis(module, contract, maxSteps);
    const llvm::scope_exit recordWork([&] {
        workSteps = maxSteps - analysis.remaining;
        budgetExhausted = analysis.exhausted;
    });
    if (auto problem = host_detail::initialBindingProblem(module, contract); !problem.empty()) {
        refusal = problem;
        return;
    }
    if (!analysis.entry || analysis.entry.getBody().empty()) {
        refusal = "host contract script entry is missing or external";
        return;
    }
    if (analysis.entry.getBody().front().getNumArguments() != 3 ||
        analysis.entry.getUpvalueCount() != 0) {
        refusal = "closed-source script entry cannot take host arguments or captures";
        return;
    }
    for (const auto & root : contract.roots) {
        for (const std::string & key : root.properties) {
            reports.push_back(analysis.slot(root, key));
        }
    }
    refusal = analysis.environmentProblem();
    for (const std::string & name : contract.observations) {
        const auto & stores = analysis.globals[name];
        if (stores.empty() && refusal.empty()) {
            refusal = "declared observation has no source store: " + name;
        }
        observed.insert(observed.end(), stores.begin(), stores.end());
    }
    if (analysis.exhausted) { refusal = "host contract analysis work budget exhausted"; }
    if (refusal.empty()) {
        for (const HostSlotReport & report : reports) {
            if (!report.reason.empty()) {
                refusal = report.binding + "." + report.property + ": " + report.reason;
                break;
            }
        }
    }
}

const HostSlotEdge * HostContractAnalysis::property(ctjs::GetPropertyOp read) const {
    if (!proved()) { return nullptr; }
    for (const HostSlotReport & report : reports) {
        for (const HostSlotEdge & edge : report.edges) {
            if (edge.read == read) { return &edge; }
        }
    }
    return nullptr;
}

} // namespace ctcompile::ctnative
