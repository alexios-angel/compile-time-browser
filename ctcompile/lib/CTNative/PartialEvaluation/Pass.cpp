#include "Heap.h"

#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "ctcompile/CTNative/Transforms/Passes.h"
#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative {

#define GEN_PASS_DEF_CTNATIVEPARTIALEVALUATE
#include "ctcompile/CTNative/Transforms/Passes.h.inc"

namespace {

// create_closure names a bytecode index, not an MLIR symbol use. Private
// visibility alone therefore does not establish that every call is visible.
// Recheck these value uses before replacing a parameterized function's body.
std::optional<unsigned> functionIndex(ctjs::FuncOp function) {
    const llvm::StringRef name = function.getSymName();
    const auto dollar = name.rfind('$');
    unsigned index = 0;
    if (dollar == llvm::StringRef::npos || name.substr(dollar + 1).getAsInteger(10, index)) {
        return {};
    }
    return index;
}

bool directCalleeUse(mlir::OpOperand & use, ctjs::FuncOp target) {
    auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
    return direct && use.getOperandNumber() == 2 && direct.getCallee() == target.getSymName();
}

bool closedDeclaration(ctjs::StoreGlobalOp store, ctjs::FuncOp target, mlir::ModuleOp module) {
    auto parent = store->getParentOfType<ctjs::FuncOp>();
    if (!parent || functionIndex(parent) != 0 || parent.getBody().empty() ||
        store->getBlock() != &parent.getBody().front()) {
        return false;
    }
    for (mlir::Operation & before : *store->getBlock()) {
        if (&before == store.getOperation()) { break; }
        if (!llvm::isa<ctjs::FrameEnterOp, ctjs::ConstantOp, ctjs::CreateClosureOp,
                       ctjs::StoreGlobalOp, ctjs::RootOp>(before)) {
            return false;
        }
    }
    unsigned stores = 0;
    bool closed = true;
    module.walk([&](mlir::Operation * op) {
        if (auto other = llvm::dyn_cast<ctjs::StoreGlobalOp>(op);
            other && other.getName() == store.getName()) {
            ++stores;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op);
            load && load.getName() == store.getName()) {
            for (mlir::OpOperand & use : load.getResult().getUses()) {
                if (!llvm::isa<ctjs::RootOp>(use.getOwner()) && !directCalleeUse(use, target)) {
                    closed = false;
                }
            }
        }
    });
    return closed && stores == 1;
}

std::string callableProblem(ctjs::FuncOp function, mlir::ModuleOp module) {
    const auto index = functionIndex(function);
    if (!index) { return {}; } // No numeric-index closure can name this symbol.
    std::string reason;
    module.walk([&](ctjs::CreateClosureOp made) {
        if (!reason.empty() || made.getFunction() < 0 ||
            static_cast<unsigned>(made.getFunction()) != *index) {
            return;
        }
        for (mlir::OpOperand & use : made.getResult().getUses()) {
            if (llvm::isa<ctjs::RootOp>(use.getOwner()) || directCalleeUse(use, function)) {
                continue;
            }
            if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(use.getOwner());
                store && closedDeclaration(store, function, module)) {
                continue;
            }
            reason = ("function closure escapes through `" +
                      use.getOwner()->getName().getStringRef() + "`")
                         .str();
            return;
        }
    });
    return reason;
}

// Pure fresh-object writes still require that no user prototype setters can
// intervene. This first slice excludes every route to shared prototypes or
// host state, including dynamic property names and the top-level receiver.
std::string environmentProblem(mlir::ModuleOp module) {
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
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(op); call && nativeMapAction(call).empty()) {
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
        prepareNativeMaps(module);
        const std::string environment = environmentProblem(module);
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
            if (const auto problem = callableProblem(fn, module); !problem.empty()) {
                decline(problem);
                continue;
            }
            llvm::SmallVector<partial_eval::value> args(fn.getBody().front().getNumArguments());
            bool known = true;
            for (unsigned i = 3; i < args.size(); ++i) {
                mlir::Attribute expected;
                for (ctjs::CallDirectOp call : callers[fn]) {
                    if (i >= call->getNumOperands()) {
                        known = false;
                        break;
                    }
                    auto constant = call->getOperand(i).getDefiningOp<ctjs::ConstantOp>();
                    if (!constant ||
                        !llvm::isa<ctjs::NumberAttr, ctjs::BooleanAttr, ctjs::NullAttr,
                                   ctjs::UndefinedAttr, ctjs::StringAttr>(constant.getValue()) ||
                        (expected && expected != constant.getValue())) {
                        known = false;
                        break;
                    }
                    expected = constant.getValue();
                }
                if (!known) { break; }
                args[i] = partial_eval::value::primitive(expected);
            }
            if (!known) {
                decline("callers do not supply one set of constant arguments");
                continue;
            }
            partial_eval::evaluator engine(module, maxSteps, maxNodes, maxDepth);
            auto state = engine.run(fn, args);
            if (!state) {
                decline(engine.reason());
                continue;
            }
            const auto live = partial_eval::reachable(*state);
            if (!live) {
                decline("returned heap has an ownership cycle or unsupported value");
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
