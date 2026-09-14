#include "Analysis.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Transforms/Passes.h"
#include "mlir/IR/IRMapping.h"

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/MemoryBuffer.h"

namespace ctcompile::ctnative {
#define GEN_PASS_DEF_CTNATIVESPECIALIZECLASSINITIALIZATION
#include "ctcompile/CTNative/Transforms/Passes.h.inc"
namespace {

struct classInitialization {
    mlir::ModuleOp module;
    unsigned remaining;
    std::string reason;
    llvm::DenseMap<unsigned, ctjs::FuncOp> functions;
    llvm::StringMap<ctjs::StoreGlobalOp> globals;
    llvm::SmallVector<ctjs::CallOp> calls;
    llvm::DenseSet<mlir::Operation *> setup;
    llvm::DenseSet<mlir::Operation *> retainedSetup;
    llvm::DenseSet<mlir::Operation *> constructors;
    llvm::DenseSet<mlir::Operation *> methods;
    llvm::DenseSet<mlir::Operation *> methodCalls;
    llvm::DenseSet<mlir::Operation *> getters;
    llvm::SmallVector<std::pair<ctjs::GetPropertyOp, ctjs::FuncOp>> getterReads;
    llvm::SmallVector<ctjs::FuncOp> getterOrder;
    llvm::SmallVector<ctjs::CreateClosureOp> getterClosures;

    classInitialization(mlir::ModuleOp module, unsigned steps) : module(module), remaining(steps) {}

    bool step() {
        if (!remaining) { return refuse("class initialization work budget exhausted"); }
        --remaining;
        return true;
    }
    bool refuse(llvm::StringRef message) {
        if (reason.empty()) { reason = message.str(); }
        return false;
    }
    static bool undefined(mlir::Value value) {
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        return constant && llvm::isa<ctjs::UndefinedAttr>(constant.getValue());
    }
    ctjs::FuncOp target(ctjs::CreateClosureOp closure) {
        if (!closure || closure.getFunction() < 0) { return {}; }
        // Numeric function indices belong to the enclosing source program.
        auto scope = closure->getParentOfType<ctjs::FuncOp>();
        if (!scope || scope.getBody().empty() || scope.getBody().front().getNumArguments() < 3 ||
            closure.getEnclosingClosure() !=
                scope.getBody().front().getArgument(ctjs::arg_callee)) {
            return {};
        }
        return functions.lookup(static_cast<unsigned>(closure.getFunction()));
    }
    ctjs::CreateClosureOp sourceClosure(mlir::Value value) {
        if (auto closure = value.getDefiningOp<ctjs::CreateClosureOp>()) { return closure; }
        auto load = value.getDefiningOp<ctjs::LoadGlobalOp>();
        auto store = load ? globals.lookup(load.getName()) : ctjs::StoreGlobalOp{};
        if (!store || store->getBlock() != load->getBlock() || !store->isBeforeInBlock(load)) {
            return {};
        }
        return store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
    }
    bool fieldsOnly(mlir::Value object, const llvm::StringSet<> & methodKeys,
                    bool methodsAvailable = false) {
        for (mlir::OpOperand & use : object.getUses()) {
            if (!step()) { return false; }
            auto * op = use.getOwner();
            if (llvm::isa<ctjs::RootOp>(op)) { continue; }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op); call && methodsAvailable) {
                auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (use.getOperandNumber() == 1 && read && read.getObject() == object &&
                    read.getResult().hasOneUse() &&
                    methodKeys.contains(ctjs::constantKey(read.getKey()))) {
                    methodCalls.insert(call);
                    continue;
                }
            }
            mlir::Value key;
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) { key = read.getKey(); }
            if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) { key = write.getKey(); }
            if (use.getOperandNumber() != 0 || !key || !ctjs::ordinaryKey(key)) {
                return refuse("class receiver escapes or observes a prototype/descriptor");
            }
            if (methodKeys.contains(ctjs::constantKey(key))) {
                auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
                auto call = read && read.getResult().hasOneUse()
                                ? llvm::dyn_cast<ctjs::CallOp>(*read.getResult().getUsers().begin())
                                : ctjs::CallOp{};
                if (!methodsAvailable || !call || call.getCallee() != read.getResult() ||
                    call.getReceiver() != object) {
                    return refuse("class method is observed or shadowed");
                }
            }
        }
        return true;
    }

    bool staticGetters(const llvm::StringMap<ctjs::DefineAccessorOp> & definitions,
                       llvm::ArrayRef<ctjs::GetPropertyOp> reads, ctjs::CallOp helper) {
        llvm::MapVector<mlir::Operation *, llvm::SmallVector<mlir::Operation *>> dependencies;
        const auto firstRead = getterReads.size();
        const auto resolve = [&](ctjs::GetPropertyOp read) -> ctjs::FuncOp {
            auto definition = definitions.lookup(ctjs::constantKey(read.getKey()));
            if (!ctjs::ordinaryKey(read.getKey()) || !definition) { return {}; }
            return target(definition.getGetter().getDefiningOp<ctjs::CreateClosureOp>());
        };
        for (const auto & item : definitions) {
            // Closure metadata and the compiler's home field precede accessors
            // in the interpreter. Keep their source reads until that boundary
            // has one semantics across the interpreter and native output.
            const auto key = item.first();
            if (key == "name" || key == "length" || key == "__home" || key == "caller" ||
                key == "arguments") {
                return refuse("static getter shadows closure metadata");
            }
            auto definition = item.second;
            auto closure = definition.getGetter().getDefiningOp<ctjs::CreateClosureOp>();
            auto fn = target(closure);
            if (!step() || !fn || !ctjs::ordinaryKey(item.first()) ||
                !undefined(definition.getSetter()) || closure->getBlock() != helper->getBlock() ||
                !closure->isBeforeInBlock(definition) || !undefined(closure.getEnclosingThis()) ||
                !closure.getUpvalues().empty()) {
                return refuse(
                    "static getter needs a unique local capture-free getter without a setter");
            }
            for (mlir::OpOperand & use : closure.getResult().getUses()) {
                if (!step()) { return false; }
                if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                if (use.getOwner() != definition || use.getOperandNumber() != 1) {
                    return refuse("static getter callable identity escapes its definition");
                }
            }
            auto & entry = fn.getBody().front();
            if (entry.getNumArguments() != 3 || !entry.getArgument(ctjs::arg_callee).use_empty() ||
                !entry.getArgument(ctjs::arg_new_target).use_empty()) {
                return refuse("static getter observes its callable identity or parameters");
            }
            auto & required = dependencies[fn];
            for (mlir::Operation & op : entry) {
                if (!step()) { return false; }
                if (!llvm::isa<ctjs::ConstantOp, ctjs::BinaryOp, ctjs::UnaryOp, ctjs::CompareOp,
                               ctjs::TruthyOp, ctjs::FromBoolOp, ctjs::FrameEnterOp,
                               ctjs::FrameExitOp, ctjs::ReturnOp>(op)) {
                    auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
                    if (!read || read.getObject() != entry.getArgument(ctjs::arg_receiver)) {
                        return refuse("static getter body is not a closed scalar expression");
                    }
                }
            }
            for (mlir::OpOperand & use : entry.getArgument(ctjs::arg_receiver).getUses()) {
                if (!step()) { return false; }
                auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
                auto callee = read ? resolve(read) : ctjs::FuncOp{};
                if (use.getOperandNumber() != 0 || !callee) {
                    return refuse("static getter receiver lacks an exact local getter dependency");
                }
                required.push_back(callee);
                getterReads.emplace_back(read, callee);
            }
            getters.insert(fn);
            getterClosures.push_back(closure);
            setup.insert(definition);
        }
        // ponytail: bounded fixpoint over local getters; inherited/dynamic
        // receivers need a separate provenance proof before widening.
        llvm::DenseSet<mlir::Operation *> completed;
        llvm::DenseMap<mlir::Operation *, unsigned> expansion;
        while (completed.size() != dependencies.size()) {
            const auto before = completed.size();
            for (const auto & [fn, required] : dependencies) {
                if (!step()) { return false; }
                if (completed.contains(fn)) { continue; }
                bool ready = true;
                for (mlir::Operation * callee : required) {
                    if (!step()) { return false; }
                    ready &= completed.contains(callee);
                }
                if (!ready) { continue; }
                unsigned cost = 0;
                for (mlir::Operation & op : llvm::cast<ctjs::FuncOp>(fn).getBody().front()) {
                    if (!step()) { return false; }
                    if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::ReturnOp>(op)) {
                        continue;
                    }
                    auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
                    const unsigned added = read ? expansion.lookup(resolve(read)) : 1;
                    if (cost > remaining || added > remaining - cost) {
                        return refuse("class initialization work budget exhausted");
                    }
                    cost += added;
                }
                expansion[fn] = cost;
                completed.insert(fn);
                getterOrder.push_back(llvm::cast<ctjs::FuncOp>(fn));
            }
            if (completed.size() == before) { return refuse("static getter dependency cycle"); }
        }
        for (ctjs::GetPropertyOp read : reads) {
            if (!step()) { return false; }
            auto callee = resolve(read);
            if (!callee) { return refuse("constructor read lacks an exact local static getter"); }
            getterReads.emplace_back(read, callee);
        }
        // Charge every clone before any mutation, including repeated reads and
        // the transitive expansion copied from already-expanded getter bodies.
        for (const auto & item : llvm::drop_begin(getterReads, firstRead)) {
            const unsigned cost = expansion.lookup(item.second);
            if (cost > remaining) { return refuse("class initialization work budget exhausted"); }
            remaining -= cost;
        }
        return true;
    }

    // ponytail: immutable local base methods/getters; inheritance needs a complete
    // receiver/home proof before widening.
    bool examine(ctjs::CallOp call) {
        if (!step() || call.getArgs().size() != 1 || !undefined(call.getReceiver()) ||
            !call.getResult().use_empty()) {
            return refuse("class helper needs one local constructor and an unused result");
        }
        auto closure = call.getArgs().front().getDefiningOp<ctjs::CreateClosureOp>();
        auto function = target(closure);
        if (!function || !undefined(closure.getEnclosingThis()) || !closure.getUpvalues().empty() ||
            closure->getBlock() != call->getBlock() || !closure->isBeforeInBlock(call)) {
            return refuse("class constructor lacks a local ordinary closure identity");
        }
        ctjs::SetPropertyOp attachment, home, backedge;
        llvm::SmallVector<ctjs::ConstructOp> instances;
        llvm::StringMap<ctjs::DefineAccessorOp> staticDefinitions;
        llvm::SmallVector<ctjs::GetPropertyOp> staticReads;
        for (mlir::OpOperand & use : closure.getResult().getUses()) {
            if (!step()) { return false; }
            auto * op = use.getOwner();
            if (op == call && use.getOperandNumber() == 2) { continue; }
            if (llvm::isa<ctjs::RootOp>(op)) { continue; }
            if (auto definition = llvm::dyn_cast<ctjs::DefineAccessorOp>(op)) {
                if (use.getOperandNumber() != 0 || definition->getBlock() != call->getBlock() ||
                    !definition->isBeforeInBlock(call) ||
                    !staticDefinitions.try_emplace(definition.getName(), definition).second) {
                    return refuse("static accessor setup is repeated or outside initialization");
                }
                continue;
            }
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
                if (use.getOperandNumber() != 0 || read->getBlock() != call->getBlock() ||
                    !call->isBeforeInBlock(read)) {
                    return refuse("static getter read lacks completed local initialization");
                }
                staticReads.push_back(read);
                continue;
            }
            if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(op)) {
                if (use.getOperandNumber() >= 2 || made.getCallee() != closure.getResult() ||
                    made.getNewTarget() != closure.getResult() ||
                    made->getBlock() != call->getBlock() || !call->isBeforeInBlock(made)) {
                    return refuse("class construction escapes or lacks exact local new.target");
                }
                if (use.getOperandNumber() == 0) { instances.push_back(made); }
                continue;
            }
            auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
            if (!write || write->getBlock() != call->getBlock() || !write->isBeforeInBlock(call)) {
                return refuse("class constructor has an observable use outside its setup");
            }
            const auto key = ctjs::constantKey(write.getKey());
            if (use.getOperandNumber() == 0 && key == "prototype" && !attachment) {
                attachment = write;
            } else if (use.getOperandNumber() == 0 && key == "__home" && !home) {
                home = write;
            } else if (use.getOperandNumber() == 2 && key == "constructor" && !backedge) {
                backedge = write;
            } else {
                return refuse("class methods, static fields or repeated setup remain unsupported");
            }
        }
        auto prototype = attachment ? attachment.getValue().getDefiningOp<ctjs::CreateObjectOp>()
                                    : ctjs::CreateObjectOp{};
        if (!prototype || !home || !backedge || instances.empty() ||
            prototype->getBlock() != call->getBlock() || home.getValue() != prototype.getResult() ||
            backedge.getObject() != prototype.getResult()) {
            return refuse("class setup needs its exact fresh prototype, constructor and home");
        }
        if (!staticGetters(staticDefinitions, staticReads, call)) { return false; }
        llvm::StringSet<> methodKeys;
        llvm::SmallVector<ctjs::SetPropertyOp> definitions;
        for (mlir::OpOperand & use : prototype.getResult().getUses()) {
            if (!step()) { return false; }
            auto * op = use.getOwner();
            if ((op == attachment || op == home) && use.getOperandNumber() == 2) { continue; }
            if (op == backedge && use.getOperandNumber() == 0) { continue; }
            if (llvm::isa<ctjs::RootOp>(op)) { continue; }
            auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
            if (!write || write->getBlock() != call->getBlock() || !write->isBeforeInBlock(call)) {
                return refuse("class prototype is observed or mutated outside initialization");
            }
            if (use.getOperandNumber() == 2 && ctjs::constantKey(write.getKey()) == "__home") {
                continue; // Rechecked against the exact method closure below.
            }
            if (use.getOperandNumber() != 0 || !ctjs::ordinaryKey(write.getKey()) ||
                !methodKeys.insert(ctjs::constantKey(write.getKey())).second) {
                return refuse("class prototype needs unique ordinary method definitions");
            }
            definitions.push_back(write);
        }
        llvm::DenseSet<mlir::Operation *> methodHomes;
        for (ctjs::SetPropertyOp definition : definitions) {
            auto method = definition.getValue().getDefiningOp<ctjs::CreateClosureOp>();
            auto fn = target(method);
            if (!step() || !fn || method->getBlock() != call->getBlock() ||
                !method->isBeforeInBlock(definition) || !undefined(method.getEnclosingThis()) ||
                !method.getUpvalues().empty()) {
                return refuse("class method needs a local ordinary capture-free closure");
            }
            ctjs::SetPropertyOp methodHome;
            for (mlir::OpOperand & use : method.getResult().getUses()) {
                if (!step()) { return false; }
                auto * op = use.getOwner();
                if (op == definition && use.getOperandNumber() == 2) { continue; }
                if (llvm::isa<ctjs::RootOp>(op)) { continue; }
                auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
                if (methodHome || !write || use.getOperandNumber() != 0 ||
                    ctjs::constantKey(write.getKey()) != "__home" ||
                    write.getValue() != prototype.getResult() ||
                    write->getBlock() != call->getBlock() || !write->isBeforeInBlock(call)) {
                    return refuse("class method identity or lexical home escapes initialization");
                }
                methodHome = write;
            }
            auto & block = fn.getBody().front();
            if (!methodHome || !block.getArgument(ctjs::arg_callee).use_empty() ||
                !block.getArgument(ctjs::arg_new_target).use_empty() ||
                !fieldsOnly(block.getArgument(ctjs::arg_receiver), methodKeys, true)) {
                return refuse("class method observes its identity, home or an unproved receiver");
            }
            methods.insert(fn);
            methodHomes.insert(methodHome);
            setup.insert(methodHome);
        }
        for (mlir::OpOperand & use : prototype.getResult().getUses()) {
            if (!step()) { return false; }
            if (use.getOperandNumber() == 2 && use.getOwner() != attachment &&
                use.getOwner() != home && !methodHomes.contains(use.getOwner())) {
                return refuse("class prototype reaches an unrelated lexical home");
            }
        }
        for (ctjs::ConstructOp made : instances) {
            if (!fieldsOnly(made.getResult(), methodKeys, true)) { return false; }
        }
        if (!definitions.empty()) {
            for (ctjs::ReturnOp returned : function.getBody().front().getOps<ctjs::ReturnOp>()) {
                if (!step() || !returned.getValue().getDefiningOp<ctjs::ConstantOp>()) {
                    return refuse(
                        "class method initialization needs a primitive constructor return");
                }
            }
        }
        auto & entry = function.getBody().front();
        if (!entry.getArgument(ctjs::arg_new_target).use_empty() ||
            !entry.getArgument(ctjs::arg_callee).use_empty() ||
            !fieldsOnly(entry.getArgument(ctjs::arg_receiver), methodKeys, true)) {
            return refuse(
                "class constructor observes new.target, lexical home or receiver identity");
        }
        constructors.insert(function);
        // Keep method definitions on the prototype until constructor lowering.
        // They must already be available when the constructor body runs.
        if (definitions.empty()) {
            setup.insert(attachment);
        } else {
            retainedSetup.insert(attachment);
        }
        setup.insert(home);
        setup.insert(backedge);
        return true;
    }

    bool prove(const HostContract & contract) {
        auto entry = module.lookupSymbol<ctjs::FuncOp>(contract.entry);
        if (!entry || functionIndex(entry) != 0) {
            return refuse("class initialization requires the closed script entry");
        }
        for (mlir::Operation & op : module.getBody()->getOperations()) {
            if (!step()) { return false; }
            auto fn = llvm::dyn_cast<ctjs::FuncOp>(op);
            auto index = fn ? functionIndex(fn) : std::nullopt;
            if (!fn || !index || !llvm::hasSingleElement(fn.getBody()) ||
                fn.getUpvalueCount() != 0 || fn.getBody().front().getNumArguments() < 3 ||
                !functions.try_emplace(*index, fn).second) {
                return refuse(
                    "class initialization requires complete capture-free source functions");
            }
        }
        llvm::DenseSet<int64_t> createdFunctions;
        auto walked = module.walk([&](mlir::Operation * op) -> mlir::WalkResult {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (op->hasAttr("ctjs.skipped")) {
                refuse("class initialization cannot omit source functions");
                return mlir::WalkResult::interrupt();
            }
            if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(op);
                closure && !createdFunctions.insert(closure.getFunction()).second) {
                refuse("class initialization requires unique source closure creation sites");
                return mlir::WalkResult::interrupt();
            }
            if (op->getNumRegions() && op != module.getOperation() &&
                !(llvm::isa<ctjs::FuncOp>(op) && op->getParentOp() == module.getOperation())) {
                refuse("class initialization requires only its checked top-level function regions");
                return mlir::WalkResult::interrupt();
            }
            if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(op)) {
                auto [at, fresh] = globals.try_emplace(store.getName(), store);
                if (!fresh) { at->second = {}; }
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
                auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
                if (load && load.getName() == host_detail::classDefinedIntrinsic) {
                    calls.push_back(call);
                }
            }
            return mlir::WalkResult::advance();
        });
        if (walked.wasInterrupted()) { return false; }
        if (calls.empty()) { return refuse("source has no class initialization calls"); }
        for (ctjs::CallOp call : calls) {
            if (!examine(call)) { return false; }
        }
        // No ambient object, unknown callee, accessor, dynamic key or reflective
        // instruction can replace the fixed helper between entry and any call.
        // Reject the whole module, including suffixes and uncalled bodies.
        walked = module.walk([&](mlir::Operation * op) -> mlir::WalkResult {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (setup.contains(op) || retainedSetup.contains(op) || methodCalls.contains(op) ||
                llvm::is_contained(calls, op)) {
                return mlir::WalkResult::advance();
            }
            bool accepted =
                llvm::isa<mlir::ModuleOp, ctjs::ConstantOp, ctjs::CreateObjectOp,
                          ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp, ctjs::ReturnOp,
                          ctjs::StoreGlobalOp, ctjs::BinaryOp, ctjs::UnaryOp, ctjs::CompareOp,
                          ctjs::TruthyOp, ctjs::FromBoolOp>(op);
            if (auto fn = llvm::dyn_cast<ctjs::FuncOp>(op)) {
                auto & block = fn.getBody().front();
                accepted = constructors.contains(fn) || methods.contains(fn) ||
                           getters.contains(fn) ||
                           (block.getNumArguments() == 3 &&
                            block.getArgument(ctjs::arg_receiver).use_empty() &&
                            block.getArgument(ctjs::arg_new_target).use_empty());
            }
            if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
                accepted = target(closure) && target(closure) != entry &&
                           undefined(closure.getEnclosingThis()) && closure.getUpvalues().empty();
            }
            if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
                accepted = load.getName() == host_detail::classDefinedIntrinsic ||
                           static_cast<bool>(sourceClosure(load.getResult()));
            }
            if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
                auto closure = sourceClosure(call.getCalleeValue());
                accepted = target(closure) && call.getTarget() == target(closure) &&
                           !constructors.contains(target(closure)) &&
                           !methods.contains(target(closure)) &&
                           !getters.contains(target(closure)) && undefined(call.getReceiver()) &&
                           undefined(call.getNewTarget()) && call.getArgs().empty();
            }
            if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(op)) {
                accepted = constructors.contains(
                    target(made.getCallee().getDefiningOp<ctjs::CreateClosureOp>()));
            }
            mlir::Value key;
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) { key = read.getKey(); }
            if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) { key = write.getKey(); }
            if (key) { accepted = ctjs::ordinaryKey(key); }
            if (accepted) { return mlir::WalkResult::advance(); }
            refuse("class initialization source contains an unknown call, binding or reflective "
                   "effect");
            return mlir::WalkResult::interrupt();
        });
        return !walked.wasInterrupted();
    }
};

struct CTNativeSpecializeClassInitializationPass
    : impl::CTNativeSpecializeClassInitializationBase<CTNativeSpecializeClassInitializationPass> {
    using CTNativeSpecializeClassInitializationBase::CTNativeSpecializeClassInitializationBase;

    void runOnOperation() override {
        auto module = getOperation();
        auto buffer = llvm::MemoryBuffer::getFile(manifest);
        if (!buffer) {
            module.emitError("class initialization requires a readable host manifest");
            return signalPassFailure();
        }
        auto contract = parseHostContract((*buffer)->getBuffer());
        if (!contract) {
            module.emitError() << llvm::toString(contract.takeError());
            return signalPassFailure();
        }
        if (hostContractFingerprint(module) != contract->moduleSha256) {
            module.emitError("class initialization host fingerprint mismatch");
            return signalPassFailure();
        }
        if (contract->provider != HostContract::Provider::closedSource ||
            contract->initialIntrinsics !=
                std::vector<std::string>{host_detail::classDefinedIntrinsic.str()} ||
            contract->realmGlobalThis || contract->classicScriptRealm ||
            !contract->absentBindings.empty() || !contract->undefinedBindings.empty()) {
            module.emitError(
                "class initialization requires only the declared standard class helper identity");
            return signalPassFailure();
        }
        classInitialization proof{module, maxSteps};
        if (!proof.prove(*contract)) {
            module.emitError() << proof.reason;
            return signalPassFailure();
        }
        if (auto problem = host_detail::initialBindingProblem(module, *contract);
            !problem.empty()) {
            module.emitError() << problem;
            return signalPassFailure();
        }
        // All current-IR checks precede the first mutation. Nothing inferred
        // from report attributes authorizes removal, even on a repeated run.
        module.walk([](mlir::Operation * op) { removeAttrsWithPrefix(op, "ctnative."); });
        llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::GetPropertyOp>> reads;
        for (auto [read, target] : proof.getterReads) { reads[target].push_back(read); }
        for (ctjs::FuncOp target : proof.getterOrder) {
            for (ctjs::GetPropertyOp read : reads[target]) {
                mlir::OpBuilder at(read);
                mlir::IRMapping mapping;
                // Dependencies have already been expanded. This scalar body
                // has no remaining implicit-argument or external-value uses.
                // Clone at each original read, preserving evaluation order.
                for (mlir::Operation & op : target.getBody().front()) {
                    if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp>(op)) { continue; }
                    if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(op)) {
                        read.getResult().replaceAllUsesWith(mapping.lookup(returned.getValue()));
                    } else {
                        at.clone(op, mapping);
                    }
                }
                read.erase();
            }
        }
        for (ctjs::CallOp call : proof.calls) { call.erase(); }
        for (mlir::Operation * op : proof.setup) { op->erase(); }
        for (ctjs::CreateClosureOp closure : proof.getterClosures) {
            for (mlir::Operation * root : llvm::make_early_inc_range(closure->getUsers())) {
                root->erase(); // Only inert roots remain after descriptor removal.
            }
            closure.erase();
        }
        module.walk([&](ctjs::LoadGlobalOp load) {
            if (load.getName() == host_detail::classDefinedIntrinsic &&
                load.getResult().use_empty()) {
                load.erase();
            }
        });
    }
};

} // namespace
} // namespace ctcompile::ctnative
