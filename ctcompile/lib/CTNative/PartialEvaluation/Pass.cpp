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

// Pure fresh-object writes still require that no user prototype setters can
// intervene. This first slice excludes every route to shared prototypes or
// host state, including dynamic property names and the top-level receiver.
std::string environmentProblem(mlir::ModuleOp module,
                               const partial_eval::closureHeapProof * proof = nullptr) {
    std::string reason;
    llvm::DenseMap<unsigned, ctjs::FuncOp> indexed;
    module.walk([&](ctjs::FuncOp function) {
        if (auto index = functionIndex(function);
            index && !indexed.try_emplace(*index, function).second) {
            reason = "module has ambiguous numeric function identities";
        }
    });
    module.walk([&](mlir::Operation * op) {
        if (!reason.empty()) { return; }
        if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
            if (made.getFunction() < 0 ||
                !indexed.contains(static_cast<unsigned>(made.getFunction()))) {
                reason = "module has an unproved closure target";
                return;
            }
        }
        if (auto fn = llvm::dyn_cast<ctjs::FuncOp>(op); fn && !fn.getBody().empty()) {
            for (unsigned i = 0; i < 2 && i < fn.getBody().front().getNumArguments(); ++i) {
                for (mlir::Operation * use : fn.getBody().front().getArgument(i).getUsers()) {
                    if (!llvm::isa<ctjs::CreateClosureOp, ctjs::RootOp>(use)) {
                        reason = "module observes receiver or constructor state";
                    }
                }
            }
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
            if (load->hasAttr(kNativeMapConstructor)) { return; }
            for (mlir::OpOperand & use : load.getResult().getUses()) {
                if (!llvm::isa<ctjs::CallDirectOp>(use.getOwner()) || use.getOperandNumber() != 2) {
                    reason = "module has host or mutable global reads";
                }
            }
        }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(op);
            call && nativeMapAction(call).empty() &&
            !(proof && proof->checkedEnvironment && proof->closedCalls.contains(op))) {
            reason = "module has an unknown call";
        }
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
            auto target = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                call, call.getCalleeAttr());
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
        // These operations can expose or invoke callbacks without a CallOp:
        // accessors through property reads, iterators, promise assimilation,
        // and exception formatting. Their effects are outside this slice.
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
        if (auto text = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue())) {
            if (text.getValue() == "__proto__" || text.getValue() == "prototype" ||
                text.getValue() == "constructor" || text.getValue() == "__defineGetter__" ||
                text.getValue() == "__defineSetter__") {
                reason = "module can inspect or mutate shared prototypes";
            }
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
            auto target = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                call, call.getCalleeAttr());
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
            if (fn.getBody().empty() || callers[fn].empty() ||
                mlir::SymbolTable::getSymbolVisibility(fn) !=
                    mlir::SymbolTable::Visibility::Private) {
                continue;
            }
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
            for (unsigned i = 3; i < args.size(); ++i) {
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
            // Resource exhaustion always rolls back. Otherwise a dynamic or
            // effectful suffix can remain after a separately evaluated prefix.
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
        // Evaluate against one unchanged module. Rewriting an earlier caller
        // must not invalidate another candidate's recorded call sites.
        for (auto & rewrite : rewrites) {
            partial_eval::residualize(rewrite.function, rewrite.state, rewrite.live);
        }
        // New residual operations carry no input proofs. Recompute Map facts
        // for consumers that run before native lowering's own proof pass.
        prepareNativeMaps(module);
        if (report) {
            module.emitRemark() << "partial evaluation: " << evaluated << " function(s), " << nodes
                                << " residual heap node(s), " << declined << " declined";
        }
    }
};

} // namespace

} // namespace ctcompile::ctnative
