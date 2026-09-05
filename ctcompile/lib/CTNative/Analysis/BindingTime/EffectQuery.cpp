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
    auto newTarget = call.getNewTarget().getDefiningOp<ctjs::ConstantOp>();
    if (!newTarget || !llvm::isa<ctjs::UndefinedAttr>(newTarget.getValue())) { return false; }
    auto target =
        mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, call.getCalleeAttr());
    if (!target) { return false; }
    auto index = functionIndex(target);
    auto value = call.getCalleeValue();
    // Handwritten native-only functions have no bytecode identity. Preserve
    // their inert undefined callee slot, but never accept an actual callable
    // value for a different function on the strength of the symbolic target.
    if (!index) {
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        return constant && llvm::isa<ctjs::UndefinedAttr>(constant.getValue());
    }
    ctjs::CreateClosureOp made = value.getDefiningOp<ctjs::CreateClosureOp>();
    if (auto load = value.getDefiningOp<ctjs::LoadGlobalOp>()) {
        unsigned stores = 0;
        module.walk([&](ctjs::StoreGlobalOp store) {
            if (store.getName() != load.getName()) { return; }
            ++stores;
            made = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
        });
        if (stores != 1 || !closedCallableProblem(target, module).empty()) { return false; }
    }
    if (!made || made.getFunction() < 0 || static_cast<unsigned>(made.getFunction()) != *index) {
        return false;
    }
    unsigned matches = 0;
    module.walk([&](ctjs::FuncOp function) { matches += functionIndex(function) == index; });
    return matches == 1;
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
            if (entry.getNumArguments() < 3 || (mlir::SymbolTable::getSymbolVisibility(function) !=
                                                    mlir::SymbolTable::Visibility::Private &&
                                                entry.getNumArguments() != 3)) {
                safe = false;
                return;
            }
            for (unsigned i = 0; i < 2; ++i) {
                for (mlir::Operation * use : entry.getArgument(i).getUsers()) {
                    if (!llvm::isa<ctjs::RootOp, ctjs::CreateClosureOp>(use)) { safe = false; }
                }
            }
            return;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
            if (load->hasAttr(kNativeMapConstructor)) { return; }
            safe = llvm::all_of(load.getResult().getUses(), [](mlir::OpOperand & use) {
                return llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                       (llvm::isa<ctjs::CallDirectOp>(use.getOwner()) &&
                        use.getOperandNumber() == 2);
            });
            return;
        }
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
            auto target = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                call, call.getCalleeAttr());
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
            unsigned matches = 0;
            module.walk([&](ctjs::FuncOp function) {
                matches += made.getFunction() >= 0 &&
                           functionIndex(function) == static_cast<unsigned>(made.getFunction());
            });
            safe = matches == 1;
            return;
        }
        mlir::Value key;
        if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) { key = read.getKey(); }
        if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) { key = write.getKey(); }
        if (key) {
            auto constant = key.getDefiningOp<ctjs::ConstantOp>();
            auto name = constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue())
                                 : ctjs::StringAttr{};
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
bool clean(const fact & value, const flow & state) {
    return value.time == BindingTime::Static && !value.nodes.empty() &&
           llvm::all_of(value.nodes, [&](mlir::Operation * id) {
               auto found = state.heaps.find(id);
               return found != state.heaps.end() && !found->second.dynamic;
           });
}
bool local(const fact & value, ctjs::FuncOp caller) {
    return llvm::all_of(value.nodes, [&](mlir::Operation * id) {
        return id->getParentOfType<ctjs::FuncOp>() == caller;
    });
}
} // namespace

bool effectQueries::invalidateCall(ctjs::CallDirectOp call, flow & state) const {
    if (!closedEnvironment || !effectCalleeMatches(call, module)) { return false; }
    auto target =
        mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, call.getCalleeAttr());
    const auto found = summaries.find(target);
    auto newTarget = call.getNewTarget().getDefiningOp<ctjs::ConstantOp>();
    if (!target || found == summaries.end() || !found->second.known || !newTarget ||
        !llvm::isa<ctjs::UndefinedAttr>(newTarget.getValue()) ||
        call->getNumOperands() != target.getBody().front().getNumArguments()) {
        return false;
    }
    auto caller = call->getParentOfType<ctjs::FuncOp>();
    unsigned steps = 0;
    llvm::SmallVector<mlir::Operation *> pending;
    for (const auto & access : found->second.accesses) {
        fact receiver = state.values.lookup(call->getOperand(access.object.argument));
        for (mlir::StringAttr field : access.object.fields) {
            if (++steps > maxQuerySteps || receiver.domain != fact::kind::object ||
                !clean(receiver, state) || !local(receiver, caller)) {
                return false;
            }
            fact next;
            bool first = true;
            for (mlir::Operation * id : receiver.nodes) {
                const auto & fields = state.heaps.find(id)->second.fields;
                auto entry = fields.find(field.getValue());
                if (entry == fields.end()) { return false; }
                next = first ? entry->second : join(next, entry->second, true);
                first = false;
            }
            receiver = std::move(next);
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
