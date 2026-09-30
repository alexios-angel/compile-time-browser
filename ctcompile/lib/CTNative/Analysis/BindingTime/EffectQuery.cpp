#include "Analysis.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace ctcompile::ctnative::binding_time_detail {
bool safeEffectKey(llvm::StringRef name) {
    return name != "__proto__" && name != "prototype" && name != "constructor" &&
           name != "__defineGetter__" && name != "__defineSetter__";
}

bool effectCalleeMatches(ctjs::CallDirectOp call, mlir::ModuleOp module) {
    if (!ctjs::isUndefinedConstant(call.getNewTarget())) { return false; }
    auto target = call.getTarget();
    if (!target) { return false; }
    auto index = functionIndex(target);
    auto value = call.getCalleeValue();
    // Handwritten native-only functions have no bytecode identity. Preserve
    // their inert undefined callee slot, but never accept an actual callable
    // value for a different function on the strength of the symbolic target.
    if (!index) { return ctjs::isUndefinedConstant(value); }
    ctjs::CreateClosureOp made = value.getDefiningOp<ctjs::CreateClosureOp>();
    if (auto load = value.getDefiningOp<ctjs::LoadGlobalOp>()) {
        auto store = ctjs::uniqueGlobalStore(module, load.getName());
        if (!store || !closedCallableProblem(target, module).empty()) { return false; }
        made = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
    }
    if (!made || made.getFunction() < 0 || static_cast<unsigned>(made.getFunction()) != *index) {
        return false;
    }
    return static_cast<bool>(ctjs::uniqueFunction(module, *index));
}

bool closedEffectEnvironment(mlir::ModuleOp module) {
    bool safe = true;
    // This proves a closed source universe with the standard initial
    // prototypes. A public runtime object argument or host value can already
    // carry accessors, so neither is covered by this first query contract.
    module.walk([&](mlir::Operation * op) {
        if (!safe || op == module.getOperation()) { return; }
        if (auto function = llvm::dyn_cast<ctjs::FuncOp>(op)) {
            if (function.getBody().empty()) {
                safe = false;
                return;
            }
            auto & entry = function.getBody().front();
            if (entry.getNumArguments() < 3 ||
                (!function.isPrivate() && entry.getNumArguments() != 3)) {
                safe = false;
                return;
            }
            for (unsigned i = 0; i < 2; ++i) {
                if (!llvm::all_of(entry.getArgument(i).getUsers(),
                                  llvm::IsaPred<ctjs::RootOp, ctjs::CreateClosureOp>)) {
                    safe = false;
                }
            }
            return;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
            if (load->hasAttr(kNativeMapConstructor)) { return; }
            safe = calleeSlotOnly(load, true);
            return;
        }
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
            auto target = call.getTarget();
            safe = target && !target.getBody().empty();
            return;
        }
        if (llvm::isa<ctjs::CallOp>(op)) {
            safe = !nativeMapAction(op).empty();
            return;
        }
        if (llvm::isa<ctjs::ConstructOp>(op)) {
            safe = op->hasAttr(kNativeMapSite);
            return;
        }
        if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
            safe = made.getFunction() >= 0 &&
                   ctjs::uniqueFunction(module, static_cast<unsigned>(made.getFunction()));
            return;
        }
        if (mlir::Value key = ctjs::propertyKey(op)) {
            auto name = ctjs::constantAttr<ctjs::StringAttr>(key);
            safe = name && safeEffectKey(name.getValue());
            return;
        }
        // No reflection, dynamic invocation, accessors, iterators, promise
        // assimilation or other implicit host entry is silently whitelisted.
        safe = llvm::isa<ctjs::ConstantOp, ctjs::CreateObjectOp, ctjs::CreateCellOp,
                         ctjs::CellGetOp, ctjs::CellSetOp, ctjs::LoadUpvalueOp,
                         ctjs::StoreUpvalueOp, ctjs::StoreGlobalOp, ctjs::FrameEnterOp,
                         ctjs::FrameExitOp, ctjs::RootOp, ctjs::ReturnOp, ctjs::BinaryOp,
                         ctjs::BinaryStaticOp, ctjs::UnaryOp, ctjs::CompareOp, ctjs::TruthyOp,
                         mlir::arith::ConstantOp, mlir::arith::TruncIOp, mlir::scf::IfOp,
                         mlir::scf::WhileOp, mlir::scf::YieldOp, mlir::scf::ConditionOp,
                         mlir::cf::BranchOp, mlir::cf::CondBranchOp>(op) ||
               op->getName().getStringRef() == "ub.poison";
    });
    return safe;
}

namespace {
constexpr unsigned maxQuerySteps = 4096, maxReachable = 256;
bool local(const fact & value, ctjs::FuncOp caller) {
    return llvm::all_of(value.nodes, [&](mlir::Operation * id) {
        return id->getParentOfType<ctjs::FuncOp>() == caller;
    });
}
} // namespace

const effectSummary * effectQueries::knownSummary(ctjs::CallDirectOp call) const {
    // effectCalleeMatches also requires an undefined new.target.
    if (!effectCalleeMatches(call, module)) { return nullptr; }
    auto target = call.getTarget();
    const auto found = summaries.find(target);
    if (found == summaries.end() || !found->second.known ||
        call->getNumOperands() != target.getBody().front().getNumArguments()) {
        return nullptr;
    }
    return &found->second;
}

bool effectQueries::invalidateCall(ctjs::CallDirectOp call, flow & state) const {
    const effectSummary * summary = closedEnvironment ? knownSummary(call) : nullptr;
    if (!summary) { return false; }
    auto caller = call->getParentOfType<ctjs::FuncOp>();
    unsigned steps = 0;
    llvm::SmallVector<mlir::Operation *> pending;
    for (const auto & access : summary->accesses) {
        fact receiver = state.values.lookup(call->getOperand(access.object.argument));
        for (mlir::StringAttr field : access.object.fields) {
            if (++steps > maxQuerySteps || receiver.domain != fact::kind::object ||
                !clean(receiver, state) || !local(receiver, caller)) {
                return false;
            }
            auto next = joinAcross(receiver.nodes, [&](mlir::Operation * id) -> const fact * {
                const auto & fields = state.heaps.find(id)->second.fields;
                auto entry = fields.find(field.getValue());
                return entry == fields.end() ? nullptr : &entry->second;
            });
            if (!next) { return false; }
            receiver = std::move(*next);
        }
        if (++steps > maxQuerySteps || !clean(receiver, state) || !local(receiver, caller)) {
            return false;
        }
        const bool object = access.action == effectAccess::kind::objectRead ||
                            access.action == effectAccess::kind::objectWrite;
        if (receiver.domain != (object ? fact::kind::object : fact::kind::map)) { return false; }
        for (mlir::Operation * id : receiver.nodes) {
            if (object && !state.heaps.find(id)->second.fields.contains(access.key.getValue())) {
                return false;
            }
            if (access.writes()) { pending.push_back(id); }
        }
    }
    llvm::SmallPtrSet<mlir::Operation *, 32> affected;
    const auto follow = [&](const fact & data) {
        if (++steps > maxQuerySteps || data.time != BindingTime::Static) { return false; }
        if (data.domain == fact::kind::primitive && data.nodes.empty()) { return true; }
        if (!clean(data, state) || !local(data, caller)) { return false; }
        llvm::append_range(pending, data.nodes);
        return true;
    };
    while (!pending.empty()) {
        auto * id = pending.pop_back_val();
        if (!affected.insert(id).second) { continue; }
        if (affected.size() > maxReachable || ++steps > maxQuerySteps) { return false; }
        auto entry = state.heaps.find(id);
        if (entry == state.heaps.end() || entry->second.dynamic) { return false; }
        const auto & data = entry->second;
        if (!follow(data.contents)) { return false; }
        for (const auto & field : data.fields) {
            if (!follow(field.second)) { return false; }
        }
        for (const auto & retained : data.retained) {
            if (!follow(retained)) { return false; }
        }
    }
    // Resolve every condition and graph edge before changing any fact. An
    // unresolved query leaves the caller to apply its normal broad invalidation.
    for (mlir::Operation * id : affected) { state.heaps[id].dynamic = true; }
    return true;
}
} // namespace ctcompile::ctnative::binding_time_detail
