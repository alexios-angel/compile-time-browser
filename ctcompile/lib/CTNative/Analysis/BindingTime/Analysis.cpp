#include "Analysis.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace ctcompile::ctnative {
namespace {
bool sameFact(const binding_time_detail::fact & left, const binding_time_detail::fact & right) {
    // Reference identities form a set; traversal order is not a semantic
    // difference and must not keep a recursive worklist alive.
    return left.domain == right.domain && left.time == right.time &&
           left.literal == right.literal && left.nodes.size() == right.nodes.size() &&
           llvm::all_of(left.nodes, [&](mlir::Operation * node) {
               return llvm::is_contained(right.nodes, node);
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

BindingTimeAnalysis::Impl::Impl(mlir::ModuleOp input,
                                llvm::function_ref<void(mlir::ModuleOp)> prepareHeapFacts)
    : module(input) {
    prepareHeapFacts(module);
    effects = std::make_unique<binding_time_detail::effectQueries>(module);
    seedArguments();
    solveSummaries();
}

void BindingTimeAnalysis::Impl::solveSummaries() {
    // Summaries start incomplete; a recursive cycle cannot bootstrap a static
    // call merely because the return expression happens to be a literal.
    const bool solved = binding_time_detail::solveCallerFixpoint(module, [&](ctjs::FuncOp fn) {
        const bool beforeComplete = complete.lookup(fn);
        const fact beforeResult = returns.lookup(fn);
        analyze(fn);
        return beforeComplete != complete.lookup(fn) || !sameFact(beforeResult, returns.lookup(fn));
    });
    if (solved) { return; }
    // An unfinished fixed point is not a proof. Re-derive local facts while
    // making every direct call dynamic; no stale summary survives exhaustion.
    returns.clear();
    complete.clear();
    facts.clear();
    decisions.clear();
    module.walk([&](ctjs::FuncOp fn) {
        analyze(fn);
        returns[fn] = {};
        complete[fn] = false;
        decisions[fn] = {false, "function summary analysis budget exhausted"};
    });
}

void BindingTimeAnalysis::Impl::seedArguments() {
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> callers;
    bool opaque = false;
    module.walk([&](mlir::Operation * op) {
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
            if (auto fn = call.getTarget()) { callers[fn].push_back(call); }
        }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(op);
            call && nativeMapAction(op).empty() && !call->hasAttr(kNativeMapSnapshotCopy)) {
            opaque = true;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op);
            load && !load->hasAttr(kNativeMapConstructor) &&
            !load->hasAttr(kNativeMapSnapshotBuiltin) && !calleeSlotOnly(load, true)) {
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
        if (opaque || callers[fn].empty() || fn.getUpvalueCount() != 0 || !fn.isPrivate() ||
            !closedCallableProblem(fn, module).empty()) {
            return;
        }
        // Native variants retain the original callee value for boxed dispatch.
        // The symbol-only caller set is then incomplete for this original body:
        // freezing it to its remaining generic arguments would change calls
        // that dispatch through the value. Keep its parameters dynamic; this
        // does not prevent evaluation of argument-independent heap prefixes.
        if (hasAlternateCalleeCalls(fn, module)) { return; }
        for (unsigned i = 3; i < known.size(); ++i) {
            mlir::Attribute common;
            bool matches = true;
            for (ctjs::CallDirectOp call : callers[fn]) {
                if (i >= call->getNumOperands()) {
                    matches = false;
                    break;
                }
                auto constant = call->getOperand(i).getDefiningOp<ctjs::ConstantOp>();
                if (!constant || !ctjs::isPrimitiveAttr(constant.getValue()) ||
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
        inputs.push_back(arg ? fact::staticPrimitive(arg) : fact{});
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
    : BindingTimeAnalysis(module, [](mlir::ModuleOp source) { prepareNativeMaps(source); }) {}
BindingTimeAnalysis::BindingTimeAnalysis(mlir::ModuleOp module,
                                         llvm::function_ref<void(mlir::ModuleOp)> prepareHeapFacts)
    : impl(std::make_unique<Impl>(module, prepareHeapFacts)) {}
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
