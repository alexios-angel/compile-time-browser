//===- Pass.cpp - The partial evaluation pass -----------------------------===//
//
// --ctnative-partial-evaluate. For each private function whose callers all
// pass the same constant arguments (per BindingTimeAnalysis), runs the
// function in the abstract evaluator (Heap.h). If the whole call evaluates,
// its body is replaced by IR that rebuilds the resulting heap and result
// (Residualize.cpp); otherwise the static initialization prefix of the entry
// block may be evaluated and replaced instead (Prefix.cpp). A function that
// cannot be evaluated gets a `ctnative.partial_eval_reason` attribute.
//
//===----------------------------------------------------------------------===//

#include "../Analysis/BindingTime/Effects.h"
#include "ClosureProof.h"
#include "Heap.h"

#include "ctcompile/CTNative/Analysis/BindingTime.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "ctcompile/CTNative/Transforms/Passes.h"
#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative {

#define GEN_PASS_DEF_CTNATIVEPARTIALEVALUATE
#include "ctcompile/CTNative/Transforms/Passes.h.inc"

namespace {

/// Why `module` cannot be partially evaluated at all, or an empty string if it
/// can. Writes to fresh objects are only pure if no user prototype setter can
/// run, so any route to shared prototypes or host state refuses the whole
/// module, including dynamic property names, the receiver and unknown calls.
/// `proof`, when given, admits the calls prepareClosureHeapFacts proved closed.
std::string environmentProblem(mlir::ModuleOp module,
                               const partial_eval::closureHeapProof * proof = nullptr) {
    std::string reason;
    const auto indexed = partial_eval::uniqueFunctionIndex(module);
    if (!indexed) { reason = "module has ambiguous numeric function identities"; }
    module.walk([&](mlir::Operation * op) {
        if (!reason.empty()) { return; }
        if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
            if (made.getFunction() < 0 ||
                !indexed->contains(static_cast<unsigned>(made.getFunction()))) {
                reason = "module has an unproved closure target";
                return;
            }
        }
        if (auto fn = llvm::dyn_cast<ctjs::FuncOp>(op); fn && !fn.getBody().empty()) {
            for (unsigned i = 0; i < ctjs::arg_callee && i < fn.getBody().front().getNumArguments();
                 ++i) {
                for (mlir::Operation * use : fn.getBody().front().getArgument(i).getUsers()) {
                    if (!llvm::isa<ctjs::CreateClosureOp, ctjs::RootOp>(use)) {
                        reason = "module observes receiver or constructor state";
                    }
                }
            }
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
            // prepareClosureHeapFacts proves these standard builtins afresh,
            // including each iterator's single immediate Array.from consumer.
            // The evaluator materializes them when it runs.
            if (load->hasAttr(kNativeMapConstructor) || load->hasAttr(kNativeMapSnapshotBuiltin)) {
                return;
            }
            if (!calleeSlotOnly(load)) { reason = "module has host or mutable global reads"; }
        }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(op);
            call && nativeMapAction(call).empty() && !call->hasAttr(kNativeMapSnapshotCopy) &&
            !(proof && proof->checkedEnvironment && proof->closedCalls.contains(op))) {
            reason = "module has an unknown call";
        }
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
            auto target = call.getTarget();
            if (!target || target.getBody().empty()) {
                reason = "module has a call without a visible body";
            }
        }
        if (auto construct = llvm::dyn_cast<ctjs::ConstructOp>(op);
            construct && !op->hasAttr(kNativeMapSite)) {
            reason = "module has an unproved constructor";
        }
        if (llvm::isa<ctjs::CallSpreadOp, ctjs::ConstructSpreadOp, ctjs::DynamicImportOp,
                      ctjs::DeletePropertyOp, ctjs::DeleteNamedOp, ctjs::PassNewTargetOp>(op) ||
            op->getName().getStringRef().starts_with("ctjs.module_")) {
            reason = "module has dynamic invocation or prototype effects";
        }
        // These operations can run callbacks without a CallOp: accessors
        // through property reads, iterators, promise resolution and exception
        // formatting. The evaluator does not model those effects, so it
        // refuses them.
        if (llvm::isa<ctjs::DefineAccessorOp, ctjs::GetProtoOp, ctjs::SetProtoOp, ctjs::LoadHomeOp,
                      ctjs::OwnKeysOp, ctjs::IterableOp, ctjs::WrapPromiseOp, ctjs::SuspendOp,
                      ctjs::ThrowOp, ctjs::ResumeThrowOp>(op)) {
            reason = "module has implicit invocation or prototype effects";
        }
        mlir::Value key;
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) { key = get.getKey(); }
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) { key = set.getKey(); }
        if (!key) { return; }
        auto constant = key.getDefiningOp<ctjs::ConstantOp>();
        if (!constant) {
            reason = "module has dynamic property names";
            return;
        }
        if (auto text = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue());
            text && !binding_time_detail::safeEffectKey(text.getValue())) {
            reason = "module can inspect or mutate shared prototypes";
        }
    });
    return reason;
}

struct CTNativePartialEvaluatePass
    : impl::CTNativePartialEvaluateBase<CTNativePartialEvaluatePass> {
    using CTNativePartialEvaluateBase::CTNativePartialEvaluateBase;
    void runOnOperation() override {
        auto module = getOperation();
        module.walk([](mlir::Operation * op) {
            op->removeAttr("ctnative.partial_evaluated");
            op->removeAttr("ctnative.partial_eval_reason");
        });
        partial_eval::closureHeapProof closureProof;
        BindingTimeAnalysis bindingTime(module, [&](mlir::ModuleOp input) {
            closureProof = partial_eval::prepareClosureHeapFacts(
                input, [](mlir::ModuleOp shadow) { return environmentProblem(shadow).empty(); });
        });
        const std::string environment = environmentProblem(module, &closureProof);
        llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> callers;
        module.walk([&](ctjs::CallDirectOp call) {
            auto target = call.getTarget();
            if (target) { callers[target].push_back(call); }
        });
        llvm::SmallVector<ctjs::FuncOp> functions;
        module.walk([&](ctjs::FuncOp fn) { functions.push_back(fn); });
        struct rewrite {
            ctjs::FuncOp function;
            partial_eval::snapshot state;
            std::vector<unsigned> live;
        };
        std::vector<rewrite> rewrites;
        unsigned evaluated = 0, nodes = 0, declined = 0;
        for (ctjs::FuncOp fn : functions) {
            if (fn.getBody().empty() || callers[fn].empty() || !fn.isPrivate()) { continue; }
            const auto decline = [&](llvm::StringRef why) {
                fn->setAttr("ctnative.partial_eval_reason",
                            mlir::StringAttr::get(&getContext(), why));
                ++declined;
            };
            if (!environment.empty()) {
                decline(environment);
                continue;
            }
            if (const auto problem = closedCallableProblem(fn, module); !problem.empty()) {
                decline(problem);
                continue;
            }
            llvm::SmallVector<partial_eval::value> args(fn.getBody().front().getNumArguments());
            bool known = true;
            for (unsigned i = ctjs::implicit_arguments; i < args.size(); ++i) {
                if (auto argument = bindingTime.knownArgument(fn, i)) {
                    args[i] = partial_eval::value::primitive(argument);
                } else {
                    known = false;
                }
            }
            std::optional<partial_eval::snapshot> state;
            std::string wholeProblem = "callers do not supply one set of constant arguments";
            if (known) {
                partial_eval::evaluator engine(module, maxSteps, maxNodes, maxDepth);
                state = engine.run(fn, args);
                if (!state) { wholeProblem = engine.reason(); }
            }
            // A whole-function failure falls back to evaluating only the
            // static prefix, leaving the dynamic or effectful rest in place.
            // Running out of budget never falls back; it always rolls back.
            if (!state && !llvm::StringRef(wholeProblem).contains("budget exhausted")) {
                partial_eval::evaluator engine(module, maxSteps, maxNodes, maxDepth);
                state = engine.runPrefix(
                    fn, args, [&](mlir::Operation * op) { return bindingTime.isStatic(op); });
                if (!state && engine.reason() != "no static initialization prefix") {
                    wholeProblem = engine.reason();
                }
            }
            if (!state) {
                decline(wholeProblem);
                continue;
            }
            const auto live = partial_eval::reachable(*state);
            if (!live) {
                decline(state->boundary
                            ? "prefix live heap has an ownership cycle or unsupported value"
                            : "returned heap has an ownership cycle or unsupported value");
                continue;
            }
            rewrites.push_back({fn, std::move(*state), *live});
            ++evaluated;
            nodes += static_cast<unsigned>(live->size());
        }
        // Every function was evaluated against the same unchanged module;
        // rewrite only now, so rewriting one function cannot invalidate
        // another candidate's recorded call sites.
        for (auto & rewrite : rewrites) {
            partial_eval::residualize(rewrite.function, rewrite.state, rewrite.live);
        }
        // New residual operations carry no Map proofs. Recompute the Map
        // facts for passes that run before native lowering's own proof.
        prepareNativeMaps(module);
        if (report) {
            module.emitRemark() << "partial evaluation: " << evaluated << " function(s), " << nodes
                                << " residual heap node(s), " << declined << " declined";
        }
    }
};

} // namespace

} // namespace ctcompile::ctnative
