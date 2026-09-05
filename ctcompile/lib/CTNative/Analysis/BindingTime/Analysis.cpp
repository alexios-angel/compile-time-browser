#include "Analysis.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctnative {
namespace {
bool primitive(mlir::Attribute attr) {
    return llvm::isa<ctjs::NumberAttr, ctjs::BooleanAttr, ctjs::NullAttr, ctjs::UndefinedAttr,
                     ctjs::StringAttr>(attr);
}
bool bookkeeping(ctjs::LoadGlobalOp load) {
    return llvm::all_of(load.getResult().getUses(), [](mlir::OpOperand & use) {
        return llvm::isa<ctjs::RootOp>(use.getOwner()) ||
               (llvm::isa<ctjs::CallDirectOp>(use.getOwner()) && use.getOperandNumber() == 2);
    });
}
} // namespace

llvm::StringRef bindingTimeName(BindingTime time) {
    switch (time) {
    case BindingTime::Unknown: return "unknown";
    case BindingTime::Static: return "static";
    case BindingTime::Dynamic: return "dynamic";
    }
    llvm_unreachable("invalid binding time");
}

BindingTimeAnalysis::Impl::Impl(mlir::ModuleOp input) : module(input) {
    prepareNativeMaps(module);
    seedArguments();
    llvm::SmallVector<ctjs::FuncOp> functions;
    module.walk([&](ctjs::FuncOp fn) { functions.push_back(fn); });
    // Bottom-up summaries converge for acyclic call chains. Recursion remains
    // dynamic; no speculative fixed point can turn an unknown call into pure.
    for (size_t round = 0; round <= functions.size(); ++round) {
        bool changed = false;
        facts.clear();
        decisions.clear();
        for (ctjs::FuncOp fn : functions) {
            const bool before = complete.lookup(fn);
            analyze(fn);
            changed |= before != complete.lookup(fn);
        }
        if (!changed) { break; }
    }
}

void BindingTimeAnalysis::Impl::seedArguments() {
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> callers;
    bool opaque = false;
    module.walk([&](mlir::Operation * op) {
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
            if (auto fn = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                    call, call.getCalleeAttr())) {
                callers[fn].push_back(call);
            }
        }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(op); call && nativeMapAction(op).empty()) {
            opaque = true;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op);
            load && !load->hasAttr(kNativeMapConstructor) && !bookkeeping(load)) {
            opaque = true;
        }
        if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(op);
            made && !made->hasAttr(kNativeMapSite)) {
            opaque = true;
        }
        if (llvm::isa<ctjs::CallSpreadOp, ctjs::ConstructSpreadOp, ctjs::DefineAccessorOp,
                      ctjs::DynamicImportOp, ctjs::GetProtoOp, ctjs::SetProtoOp>(op)) {
            opaque = true;
        }
    });
    module.walk([&](ctjs::FuncOp fn) {
        if (fn.getBody().empty()) { return; }
        auto & known = arguments[fn];
        known.resize(fn.getBody().front().getNumArguments());
        if (opaque || callers[fn].empty() || fn.getUpvalueCount() != 0 ||
            mlir::SymbolTable::getSymbolVisibility(fn) != mlir::SymbolTable::Visibility::Private ||
            !closedCallableProblem(fn, module).empty()) {
            return;
        }
        for (unsigned i = 3; i < known.size(); ++i) {
            mlir::Attribute common;
            bool matches = true;
            for (ctjs::CallDirectOp call : callers[fn]) {
                if (i >= call->getNumOperands()) {
                    matches = false;
                    break;
                }
                auto constant = call->getOperand(i).getDefiningOp<ctjs::ConstantOp>();
                if (!constant || !primitive(constant.getValue()) ||
                    (common && common != constant.getValue())) {
                    matches = false;
                    break;
                }
                common = constant.getValue();
            }
            if (matches) { known[i] = common; }
        }
    });
}

void BindingTimeAnalysis::Impl::analyze(ctjs::FuncOp fn) {
    if (fn.getBody().empty()) {
        complete[fn] = false;
        decisions[fn] = {false, "function body is unavailable"};
        return;
    }
    flow state;
    llvm::SmallVector<fact> inputs;
    for (mlir::Attribute arg : arguments[fn]) {
        inputs.push_back(arg ? fact{fact::kind::primitive, BindingTime::Static, arg, {}} : fact{});
    }
    const bool eligible = region(fn.getBody(), state, true, inputs);
    fact result;
    bool first = true;
    fn.getBody().walk([&](ctjs::ReturnOp ret) {
        fact next = facts.lookup(ret.getValue());
        result = first ? next : binding_time_detail::join(result, next, eligible);
        first = false;
    });
    returns[fn] = result;
    complete[fn] = eligible && !first && result.time == BindingTime::Static;
    decisions[fn] = {complete[fn],
                     complete[fn] ? "closed static body" : "body requires runtime work"};
}

BindingTimeAnalysis::BindingTimeAnalysis(mlir::ModuleOp module)
    : impl(std::make_unique<Impl>(module)) {}
BindingTimeAnalysis::~BindingTimeAnalysis() = default;
BindingTime BindingTimeAnalysis::get(mlir::Value value) const {
    const auto found = impl->facts.find(value);
    return found == impl->facts.end() ? BindingTime::Unknown : found->second.time;
}
bool BindingTimeAnalysis::isStatic(mlir::Value value) const {
    return get(value) == BindingTime::Static;
}
bool BindingTimeAnalysis::isStatic(mlir::Operation * op) const {
    const auto found = impl->decisions.find(op);
    return found != impl->decisions.end() && found->second.eligible;
}
mlir::Attribute BindingTimeAnalysis::knownArgument(ctjs::FuncOp fn, unsigned index) const {
    const auto found = impl->arguments.find(fn);
    return found != impl->arguments.end() && index < found->second.size() ? found->second[index]
                                                                          : mlir::Attribute{};
}
llvm::StringRef BindingTimeAnalysis::reason(mlir::Operation * op) const {
    const auto found = impl->decisions.find(op);
    return found == impl->decisions.end() ? llvm::StringRef("unknown operation")
                                          : llvm::StringRef(found->second.reason);
}
} // namespace ctcompile::ctnative
