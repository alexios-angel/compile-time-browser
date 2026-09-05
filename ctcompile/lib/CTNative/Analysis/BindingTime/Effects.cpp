#include "Effects.h"

#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace ctcompile::ctnative::binding_time_detail {
namespace {
constexpr unsigned maxSteps = 100000, maxAccesses = 64, maxPath = 8;
struct operand {
    enum class kind {
        unknown,
        primitive,
        reference,
        method
    } tag = kind::unknown;
    effectPath path;
    llvm::StringRef method;
};
bool ordinaryCall(ctjs::CallDirectOp call) {
    auto target = call.getNewTarget().getDefiningOp<ctjs::ConstantOp>();
    return target && llvm::isa<ctjs::UndefinedAttr>(target.getValue());
}
mlir::StringAttr propertyKey(mlir::Value value) {
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    auto key =
        constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue()) : ctjs::StringAttr{};
    return key && safeEffectKey(key.getValue())
               ? mlir::StringAttr::get(value.getContext(), key.getValue())
               : mlir::StringAttr{};
}
} // namespace

effectQueries::effectQueries(mlir::ModuleOp input) : module(input) {
    closedEnvironment = closedEffectEnvironment(module);
    llvm::SmallVector<ctjs::FuncOp> functions, pending;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::FuncOp>> dependents;
    module.walk([&](ctjs::FuncOp function) { functions.push_back(function); });
    module.walk([&](ctjs::CallDirectOp call) {
        auto caller = call->getParentOfType<ctjs::FuncOp>();
        auto callee =
            mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, call.getCalleeAttr());
        if (caller && callee && !llvm::is_contained(dependents[callee], caller)) {
            dependents[callee].push_back(caller);
        }
    });
    llvm::SmallPtrSet<mlir::Operation *, 32> queued;
    const auto enqueue = [&](ctjs::FuncOp function) {
        if (queued.insert(function).second) { pending.push_back(function); }
    };
    for (ctjs::FuncOp function : llvm::reverse(functions)) { enqueue(function); }
    unsigned steps = 0;
    const uint64_t count = static_cast<uint64_t>(functions.size());
    const uint64_t limit = count * (count + 1);
    uint64_t visits = 0;
    while (!pending.empty() && visits++ < limit && steps < maxSteps) {
        auto function = pending.pop_back_val();
        queued.erase(function);
        auto result = summarize(function, steps);
        if (!(result == summaries.lookup(function))) {
            summaries[function] = std::move(result);
            for (ctjs::FuncOp caller : dependents[function]) { enqueue(caller); }
        }
    }
    // Unknown callees never bootstrap a recursive SCC. Exhaustion invalidates
    // the entire attempt instead of exposing a partially solved query graph.
    if (!pending.empty() || steps >= maxSteps) { summaries.clear(); }
}

effectSummary effectQueries::summarize(ctjs::FuncOp function, unsigned & steps) const {
    if (!llvm::hasSingleElement(function.getBody()) || function->getParentOp() != module ||
        function.getUpvalueCount() != 0 ||
        mlir::SymbolTable::getSymbolVisibility(function) !=
            mlir::SymbolTable::Visibility::Private ||
        !closedCallableProblem(function, module).empty()) {
        return {};
    }
    auto & entry = function.getBody().front();
    if (entry.getNumArguments() < 3 || !entry.hasNoPredecessors()) { return {}; }
    for (unsigned i = 0; i < 3; ++i) {
        if (llvm::any_of(entry.getArgument(i).getUsers(),
                         [](mlir::Operation * use) { return !llvm::isa<ctjs::RootOp>(use); })) {
            return {};
        }
    }
    effectSummary result{true, {}};
    llvm::DenseMap<mlir::Value, operand> values;
    for (unsigned i = 3; i < entry.getNumArguments(); ++i) {
        values[entry.getArgument(i)] = {operand::kind::reference, {i, {}}, {}};
    }
    bool wrote = false;
    const auto access = [&](effectAccess item) {
        if (item.object.fields.size() > maxPath || result.accesses.size() >= maxAccesses) {
            return false;
        }
        wrote |= item.writes();
        if (!llvm::is_contained(result.accesses, item)) {
            result.accesses.push_back(std::move(item));
        }
        return true;
    };
    for (mlir::Operation & op : entry) {
        if (++steps >= maxSteps || op.getNumRegions() != 0) { return {}; }
        operand output;
        if (llvm::isa<ctjs::RootOp, ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::ReturnOp>(op)) {
            continue;
        }
        if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(op)) {
            if (!llvm::isa<ctjs::NumberAttr, ctjs::BooleanAttr, ctjs::StringAttr, ctjs::NullAttr,
                           ctjs::UndefinedAttr>(constant.getValue())) {
                return {};
            }
            output.tag = operand::kind::primitive;
        } else if (llvm::isa<mlir::arith::ConstantOp>(op)) {
            output.tag = operand::kind::primitive;
        } else if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
            if (!llvm::all_of(load.getResult().getUses(), [](mlir::OpOperand & use) {
                    return llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                           (llvm::isa<ctjs::CallDirectOp>(use.getOwner()) &&
                            use.getOperandNumber() == 2);
                })) {
                return {};
            }
        } else if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
            auto receiver = values.lookup(read.getObject());
            auto key = propertyKey(read.getKey());
            if (receiver.tag != operand::kind::reference || !key) { return {}; }
            if (read->hasAttr(kNativeMapMethod)) {
                if (!access({effectAccess::kind::mapRead, receiver.path, {}})) { return {}; }
                output = {operand::kind::method, receiver.path, key.getValue()};
            } else if (nativeMapAction(read) == "size") {
                if (!access({effectAccess::kind::mapRead, receiver.path, {}})) { return {}; }
                output.tag = operand::kind::primitive;
            } else {
                // A pre-call path is no longer a valid description after a
                // write might have replaced one of its edges.
                if (wrote || !access({effectAccess::kind::objectRead, receiver.path, key})) {
                    return {};
                }
                receiver.path.fields.push_back(key);
                if (receiver.path.fields.size() > maxPath) { return {}; }
                output = {operand::kind::reference, std::move(receiver.path), {}};
            }
        } else if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
            auto receiver = values.lookup(write.getObject());
            auto key = propertyKey(write.getKey());
            if (receiver.tag != operand::kind::reference || !key ||
                !access({effectAccess::kind::objectWrite, receiver.path, key})) {
                return {};
            }
        } else if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
            auto method = values.lookup(call.getCallee()),
                 receiver = values.lookup(call.getReceiver());
            const auto action = nativeMapAction(call);
            if (method.tag != operand::kind::method || receiver.tag != operand::kind::reference ||
                !(method.path == receiver.path) || action.empty() || action != method.method) {
                return {};
            }
            const bool writes = action == "set" || action == "delete" || action == "clear";
            if (action != "get" && action != "has" && !writes) { return {}; }
            if (!access({writes ? effectAccess::kind::mapWrite : effectAccess::kind::mapRead,
                         receiver.path,
                         {}})) {
                return {};
            }
            if (action == "set") {
                output = receiver;
            } else if (action != "get") {
                output.tag = operand::kind::primitive;
            }
        } else if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
            if (wrote || !ordinaryCall(call) || !effectCalleeMatches(call, module)) { return {}; }
            auto target = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                call, call.getCalleeAttr());
            const auto found = summaries.find(target);
            if (!target || found == summaries.end() || !found->second.known ||
                call->getNumOperands() != target.getBody().front().getNumArguments()) {
                return {};
            }
            for (const auto & dependency : found->second.accesses) {
                auto actual = values.lookup(call->getOperand(dependency.object.argument));
                if (actual.tag != operand::kind::reference) { return {}; }
                effectAccess mapped = dependency;
                llvm::append_range(actual.path.fields, dependency.object.fields);
                mapped.object = std::move(actual.path);
                if (!access(std::move(mapped))) { return {}; }
            }
            // A callee result may alias any argument or fresh allocation. Do
            // not manufacture a path for it from a return type or schema.
        } else {
            bool safe = llvm::isa<ctjs::TruthyOp>(op);
            if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op)) {
                safe = unary.getKind() == ctjs::UnaryKind::Not ||
                       unary.getKind() == ctjs::UnaryKind::Void ||
                       unary.getKind() == ctjs::UnaryKind::TypeOf;
            }
            if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(op)) {
                safe = compare.getKind() == ctjs::CompareKind::StrictEq;
            }
            if (!safe) {
                safe = llvm::isa<ctjs::BinaryOp, ctjs::BinaryStaticOp, ctjs::UnaryOp,
                                 ctjs::CompareOp, mlir::arith::TruncIOp>(op) &&
                       llvm::all_of(op.getOperands(), [&](mlir::Value value) {
                           return values.lookup(value).tag == operand::kind::primitive;
                       });
            }
            if (!safe) { return {}; }
            output.tag = operand::kind::primitive;
        }
        if (op.getNumResults() == 1) { values[op.getResult(0)] = std::move(output); }
    }
    return llvm::isa<ctjs::ReturnOp>(entry.getTerminator()) ? result : effectSummary{};
}
} // namespace ctcompile::ctnative::binding_time_detail
