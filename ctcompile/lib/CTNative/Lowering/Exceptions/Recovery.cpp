#include "Recovery.h"

#include "../../../CTJS/Lowering/Globals/RegisterFlow.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/Conversion/ControlFlowToSCF/ControlFlowToSCF.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/CFGToSCF.h"
#include "mlir/Transforms/RegionUtils.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <utility>

namespace ctcompile::ctnative::lowering_detail {
namespace {

// Recovery runs before any handler CFG simplification. The importer carries
// the complete register vector on ordinary/check edges, while explicit throw
// has no successor. A throw-only block therefore supplies its current vector
// through its block arguments; the installation edge supplies no throw state.
//
// Work on a disposable function clone. Prove one entry handler, its dedicated
// landing, balanced continuation edges and acyclic tails. Clone normal and catch
// tails separately, including any shared continuation. Checks become normal
// edges only under the caller's subsequent native nonthrowing admission. Every
// body exit becomes the SAME completion terminator (flag, normal result,
// payload/state): LLVM can then structure ordinary branches without merging a
// throw with a normal completion. The native consumer must still emit a C++
// throw for the exceptional completion. Catch exits use try_yield. Only after
// both regions structure successfully is the recovered body adopted.
struct recovery {
    ctjs::FuncOp function;
    mlir::ModuleOp module;
    mlir::IRMapping copies;
    unsigned remaining;
    std::string refusal;
    ctjs::PushHandlerOp push;
    ctjs::CatchLandOp landing;
    ctjs::FrameEnterOp frame;
    unsigned width = 0;
    unsigned throws = 0;
    ExceptionRecoveryMode mode;
    llvm::DenseMap<mlir::Operation *, ctjs::CallDirectOp> invocations;
    llvm::DenseMap<mlir::StringAttr, ctjs::FuncOp> bindings;
    llvm::DenseSet<mlir::Operation *> boundTargets;
    llvm::DenseSet<mlir::Operation *> boundCalls;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> boundCallers;

    recovery(ctjs::FuncOp function, unsigned maxSteps, ExceptionRecoveryMode mode)
        : function(function), module(function->getParentOfType<mlir::ModuleOp>()),
          remaining(maxSteps), mode(mode) {}

    struct tail {
        llvm::SmallVector<mlir::Block *> blocks;
        llvm::DenseMap<mlir::Block *, bool> active;
        llvm::DenseMap<mlir::Block *, llvm::SmallVector<mlir::Block *>> edges;
    };

    bool reject(llvm::StringRef why) {
        if (refusal.empty()) { refusal = why.str(); }
        return false;
    }

    bool spend(uint64_t amount = 1) {
        if (amount > remaining) {
            return reject("native exception recovery work budget exhausted");
        }
        remaining -= static_cast<unsigned>(amount);
        return true;
    }

    bool inspect() {
        unsigned pushes = 0, landings = 0, frames = 0;
        for (mlir::Block & block : function.getBody()) {
            if (!spend(uint64_t(1) + block.getNumArguments())) { return false; }
            for (mlir::Operation & operation : block) {
                if (!spend(uint64_t(1) + operation.getNumOperands())) { return false; }
                if (operation.getNumRegions() != 0) {
                    return reject("native exception recovery requires the preserved importer CFG");
                }
                if (auto found = llvm::dyn_cast<ctjs::PushHandlerOp>(operation)) {
                    push = found;
                    ++pushes;
                }
                if (auto found = llvm::dyn_cast<ctjs::CatchLandOp>(operation)) {
                    landing = found;
                    ++landings;
                }
                if (auto found = llvm::dyn_cast<ctjs::FrameEnterOp>(operation)) {
                    frame = found;
                    ++frames;
                }
            }
        }
        if (pushes != 1 || landings != 1 || frames != 1 ||
            push->getBlock() != &function.getBody().front() ||
            frame->getBlock() != push->getBlock() || push.getHandler() != landing->getBlock() ||
            push.getBody() == push.getHandler() || !landing.getPad().use_empty()) {
            return reject(
                "native exception recovery needs one entry handler and dedicated catch landing");
        }
        width = push.getBody()->getNumArguments();
        if (width == 0 || push.getHandler()->getNumArguments() != width ||
            push.getBodyOperands().size() != width || push.getHandlerOperands().size() != width ||
            !llvm::equal(push.getBodyOperands(), push.getHandlerOperands())) {
            return reject(
                "native exception recovery needs matching complete entry register vectors");
        }
        auto count = frame->getAttrOfType<mlir::IntegerAttr>("reg_count");
        if (!count || count.getInt() != width) {
            return reject("native exception recovery register vector does not match its frame");
        }
        for (mlir::Block * predecessor : push.getHandler()->getPredecessors()) {
            if (!spend()) { return false; }
            auto * term = predecessor->getTerminator();
            if (term != push.getOperation() && !llvm::isa<ctjs::CheckOp>(term)) {
                return reject("native catch landing is reachable without throwing");
            }
        }
        return true;
    }

    bool inspectInvocation(ctjs::CheckOp check) {
        ctjs::CallDirectOp call;
        for (mlir::Operation & operation : check->getBlock()->without_terminator()) {
            if (!spend()) { return false; }
            if (auto found = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
                if (call) {
                    return reject("native invocation recovery needs one call per status edge");
                }
                call = found;
            }
        }
        if (!call) { return true; }
        for (mlir::Operation & operation : check->getBlock()->without_terminator()) {
            if (&operation == call.getOperation()) { break; }
            if (!spend()) { return false; }
            if (!llvm::isa<ctjs::RootOp>(operation) && !mlir::isPure(&operation)) {
                return reject("native invocation recovery needs independently checked fallible "
                              "preparation before the call");
            }
        }
        // The importer checks a call before its result is moved into the
        // assignment target. Keep this boundary exact: even a pure operation
        // after the call belongs to the normal continuation, not its unwind.
        if (call->getNextNode() != check.getOperation() || !call.getResult().hasOneUse() ||
            call.getResult().use_begin()->getOwner() != check.getOperation() ||
            !llvm::equal(check.getHandlerOperands(), check->getBlock()->getArguments()) ||
            check.getContOperands().size() != width) {
            return reject("native invocation recovery needs an unpublished call result and its "
                          "complete pre-call register snapshot");
        }
        unsigned resultSlots = 0;
        for (auto [normal, saved] :
             llvm::zip(check.getContOperands(), check.getHandlerOperands())) {
            if (!spend()) { return false; }
            if (normal == call.getResult()) {
                ++resultSlots;
            } else if (normal != saved) {
                return reject("native invocation normal edge changes a non-result register");
            }
        }
        if (resultSlots != 1) {
            return reject("native invocation normal edge must publish one scratch result");
        }
        invocations.try_emplace(check.getOperation(), call);
        return true;
    }

    bool hasOrigin(mlir::Value value, mlir::Value origin) {
        unsigned used = 0;
        bool found = ctjs::globals_detail::registerFlowHasOrigin(value, origin, remaining, &used);
        if (!spend(used)) { return false; }
        if (!found && remaining == 0) { return spend(); }
        return found;
    }

    std::optional<llvm::SmallVector<mlir::OpOperand *>> uses(mlir::Value value) {
        unsigned used = 0;
        auto found = ctjs::globals_detail::registerFlowUses(value, remaining, &used);
        if (!spend(used)) { return {}; }
        if (!found && remaining == 0) { (void)spend(); }
        return found;
    }

    bool bindingFailure(llvm::StringRef why) {
        return reject(("native invocation recovery cannot prove nonthrowing source callee "
                       "bindings: " +
                       why)
                          .str());
    }

    bool proveBindings() {
        // This is a closed source-declaration proof, not permission from the
        // resolved symbol or a host name. Inventory every current body before
        // accepting any load. A missing body, property operation, host call or
        // coercion that could reenter leaves the original checks intact.
        if (!module) { return bindingFailure("the source module is unavailable"); }
        if (auto skipped = module->getAttr("ctjs.skipped")) {
            auto rows = llvm::dyn_cast<mlir::ArrayAttr>(skipped);
            if (!rows || !rows.empty()) { return bindingFailure("source bodies are missing"); }
        }
        llvm::DenseMap<unsigned, ctjs::FuncOp> functions;
        llvm::DenseSet<mlir::StringAttr> symbols;
        llvm::DenseMap<mlir::StringAttr, llvm::SmallVector<ctjs::StoreGlobalOp>> stores;
        llvm::SmallVector<ctjs::LoadGlobalOp> loads;
        llvm::SmallVector<ctjs::CreateClosureOp> closures;
        llvm::SmallVector<ctjs::CallDirectOp> calls;
        llvm::SmallVector<mlir::Operation *> operations;
        for (mlir::Operation & operation : module.getBody()->getOperations()) {
            if (!spend()) { return false; }
            auto body = llvm::dyn_cast<ctjs::FuncOp>(operation);
            auto index = body ? functionIndex(body) : std::optional<unsigned>{};
            if (!body || !index || body.getBody().empty() || body.getUpvalueCount() != 0 ||
                body.getBody().front().getNumArguments() < 3 || body->hasAttr("ctjs.not_lowered") ||
                !functions.try_emplace(*index, body).second ||
                !symbols.insert(body.getSymNameAttr()).second) {
                return bindingFailure("source function identity or body is incomplete");
            }
            auto walked = body.getBody().walk([&](mlir::Operation * op) {
                if (!spend(uint64_t(1) + op->getNumOperands())) {
                    return mlir::WalkResult::interrupt();
                }
                operations.push_back(op);
                if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(op)) {
                    stores[store.getNameAttr()].push_back(store);
                }
                if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) { loads.push_back(load); }
                if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
                    closures.push_back(closure);
                }
                if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) { calls.push_back(call); }
                return mlir::WalkResult::advance();
            });
            if (walked.wasInterrupted()) { return false; }
        }
        auto entry = functions.lookup(0);
        if (!entry) { return bindingFailure("there is no unique source entry"); }
        llvm::DenseSet<mlir::Operation *> declarations;
        for (auto closure : closures) {
            if (!spend()) { return false; }
            auto target = closure.getFunction() < 0
                              ? ctjs::FuncOp{}
                              : functions.lookup(static_cast<unsigned>(closure.getFunction()));
            if (!target || target == entry || !closure.getUpvalues().empty() ||
                closure->getBlock() != &entry.getBody().front() ||
                closure.getEnclosingClosure() != entry.getBody().front().getArgument(2) ||
                mlir::SymbolTable::getSymbolVisibility(target) !=
                    mlir::SymbolTable::Visibility::Private) {
                return bindingFailure("a closure lacks its exact source declaration");
            }
            auto currentUses = uses(closure.getResult());
            if (!currentUses) { return false; }
            ctjs::StoreGlobalOp declaration;
            for (mlir::OpOperand * use : *currentUses) {
                if (!spend()) { return false; }
                if (llvm::isa<ctjs::RootOp>(use->getOwner())) { continue; }
                auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(use->getOwner());
                if (!store || declaration || store.getValue() != closure.getResult() ||
                    stores.find(store.getNameAttr())->second.size() != 1 ||
                    store->getBlock() != &entry.getBody().front()) {
                    return bindingFailure("a function binding is mutated or its closure escapes");
                }
                declaration = store;
            }
            if (!declaration) { return bindingFailure("a closure is not uniquely bound"); }
            for (mlir::Operation & before : entry.getBody().front()) {
                if (!spend()) { return false; }
                if (&before == declaration.getOperation()) { break; }
                if (!llvm::isa<ctjs::FrameEnterOp, ctjs::ConstantOp, ctjs::CreateClosureOp,
                               ctjs::StoreGlobalOp, ctjs::RootOp>(before)) {
                    return bindingFailure("a call may precede declaration initialization");
                }
            }
            bindings.try_emplace(declaration.getNameAttr(), target);
            boundTargets.insert(target);
            declarations.insert(declaration);
        }
        llvm::DenseSet<mlir::Operation *> knownCalls;
        for (auto load : loads) {
            if (!spend()) { return false; }
            auto target = bindings.lookup(load.getNameAttr());
            if (!target) {
                return bindingFailure("a global read has a host or unknown alternative");
            }
            auto currentUses = uses(load.getResult());
            if (!currentUses) { return false; }
            for (mlir::OpOperand * use : *currentUses) {
                if (!spend()) { return false; }
                if (llvm::isa<ctjs::RootOp>(use->getOwner())) { continue; }
                auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use->getOwner());
                if (!call || use->getOperandNumber() != 2 ||
                    call.getCallee() != target.getSymName() ||
                    call.getNumOperands() != target.getBody().front().getNumArguments() ||
                    !hasOrigin(call.getCalleeValue(), load.getResult())) {
                    return bindingFailure("a callee has an unknown use, identity or predecessor");
                }
                knownCalls.insert(call);
            }
        }
        llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::FuncOp>> callees;
        llvm::DenseMap<mlir::Operation *, unsigned> incoming;
        for (auto call : calls) {
            if (!spend()) { return false; }
            if (!knownCalls.contains(call)) {
                return bindingFailure("a direct-call symbol lacks live callee identity");
            }
            auto target = module.lookupSymbol<ctjs::FuncOp>(call.getCallee());
            auto current =
                llvm::cast<ctjs::CallDirectOp>(copies.lookupOrDefault(call.getOperation()));
            boundCalls.insert(current);
            boundCallers[target].push_back(current);
            callees[call->getParentOfType<ctjs::FuncOp>()].push_back(target);
            ++incoming[target];
        }
        // Completion operands may cross more than one call. Establish the
        // entire live source call graph before following formals backwards or
        // payloads forwards: a recursive family cannot borrow a primitive
        // seed from one of its nonrecursive actuals. Include unused bodies and
        // repeated call edges; neither a prior summary nor reachability trims
        // this proof's obligations.
        llvm::SmallVector<ctjs::FuncOp> ready;
        for (auto [index, body] : functions) {
            (void)index;
            if (!spend()) { return false; }
            if (incoming.lookup(body) == 0) { ready.push_back(body); }
        }
        unsigned visited = 0;
        while (!ready.empty()) {
            if (!spend()) { return false; }
            auto body = ready.pop_back_val();
            ++visited;
            for (auto target : callees[body]) {
                if (!spend()) { return false; }
                if (--incoming[target] == 0) { ready.push_back(target); }
            }
        }
        if (visited != functions.size()) {
            return bindingFailure("the source call component is recursive");
        }
        for (auto [index, body] : functions) {
            if (!spend()) { return false; }
            auto currentUses = uses(body.getBody().front().getArgument(2));
            if (!currentUses) { return false; }
            for (mlir::OpOperand * use : *currentUses) {
                if (!spend()) { return false; }
                if (llvm::isa<ctjs::RootOp>(use->getOwner()) ||
                    (index == 0 && llvm::isa<ctjs::CreateClosureOp>(use->getOwner()) &&
                     use->getOperandNumber() == 0)) {
                    continue;
                }
                return bindingFailure("an implicit callee value is observed or escapes");
            }
        }
        // Declarations publish own source bindings before any call. Other
        // named stores are allowed only in the entry, and the complete name
        // census above already excludes writes to a callable binding.
        for (mlir::Operation * original : operations) {
            if (!spend()) { return false; }
            auto * op = copies.lookupOrDefault(original);
            if (auto thrown = llvm::dyn_cast<ctjs::ThrowOp>(op)) {
                // Throw diagnostics may coerce the payload. Prove every
                // actual input through the whole call family too; an uncalled body with
                // an unknown payload cannot borrow a represented invoke's
                // primitive payload fact.
                if (primitive(thrown.getValue())) { continue; }
                if (!refusal.empty()) { return false; }
                return bindingFailure("a throw payload has an unknown actual or uncalled formal");
            }
            if (llvm::isa<ctjs::CreateClosureOp, ctjs::CallDirectOp, ctjs::FrameEnterOp,
                          ctjs::PushHandlerOp>(op)) {
                continue;
            }
            if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(original)) {
                if (declarations.contains(store) ||
                    store->getParentOfType<ctjs::FuncOp>() == entry) {
                    continue;
                }
                return bindingFailure("a non-entry body writes global state");
            }
            if (!nonthrowing(op)) {
                return bindingFailure(("an operation may alter bindings through reentry: `" +
                                       op->getName().getStringRef() + "`")
                                          .str());
            }
        }
        return true;
    }

    using primitiveInput = std::pair<mlir::Value, unsigned>;

    struct completionContext {
        ctjs::CallDirectOp call;
        unsigned parent = 0;
        unsigned depth = 0;
    };

    struct primitiveContexts {
        llvm::SmallVector<completionContext> frames{completionContext{}};
        llvm::DenseMap<std::pair<mlir::Operation *, unsigned>, unsigned> indices;
    };

    std::optional<unsigned> contextFor(ctjs::CallDirectOp call, unsigned parent,
                                       primitiveContexts & contexts) {
        if (!spend()) { return {}; }
        auto key = std::pair{call.getOperation(), parent};
        if (auto found = contexts.indices.find(key); found != contexts.indices.end()) {
            return found->second;
        }
        if (contexts.frames[parent].depth >= 32) {
            reject("native invocation completion call depth exceeds 32");
            return {};
        }
        for (unsigned ancestor = parent; ancestor != 0;
             ancestor = contexts.frames[ancestor].parent) {
            if (!spend()) { return {}; }
            if (contexts.frames[ancestor].call.getCallee() == call.getCallee()) {
                reject("native invocation completion call component is recursive");
                return {};
            }
        }
        const unsigned index = static_cast<unsigned>(contexts.frames.size());
        contexts.frames.push_back({call, parent, contexts.frames[parent].depth + 1});
        contexts.indices.try_emplace(key, index);
        return index;
    }

    bool completionInputs(ctjs::CallDirectOp call, bool thrown, unsigned parent,
                          primitiveContexts & contexts,
                          llvm::SmallVectorImpl<primitiveInput> & inputs) {
        // A bounded source call-family query, separate from carrier and
        // native admission. Returns belong only to the invoked body, whereas
        // an uncaught throw in any descendant can complete its unwind. Keep
        // the exact chain of actual operands for every such completion.
        auto initial = contextFor(call, parent, contexts);
        if (!initial) { return false; }
        llvm::SmallVector<unsigned> pending{*initial};
        llvm::DenseSet<unsigned> seen;
        bool found = false;
        while (!pending.empty()) {
            if (!spend()) { return false; }
            const unsigned context = pending.pop_back_val();
            if (!seen.insert(context).second) { continue; }
            call = contexts.frames[context].call;
            auto target =
                module ? module.lookupSymbol<ctjs::FuncOp>(call.getCallee()) : ctjs::FuncOp{};
            if (!target || !boundTargets.contains(target) || !boundCalls.contains(call) ||
                target.getBody().empty() || target.getUpvalueCount() != 0 ||
                target.getBody().front().getNumArguments() != call.getNumOperands() ||
                mlir::SymbolTable::getSymbolVisibility(target) !=
                    mlir::SymbolTable::Visibility::Private) {
                return false;
            }
            for (mlir::Block & block : target.getBody()) {
                if (!spend(uint64_t(1) + block.getNumArguments())) { return false; }
                for (mlir::Operation & operation : block) {
                    if (!spend(uint64_t(1) + operation.getNumOperands())) { return false; }
                    if (auto exit = llvm::dyn_cast<ctjs::ThrowOp>(operation)) {
                        if (thrown) {
                            inputs.emplace_back(exit.getValue(), context);
                            found = true;
                        }
                    } else if (auto exit = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                        if (!thrown && context == *initial) {
                            inputs.emplace_back(exit.getValue(), context);
                            found = true;
                        }
                    } else if (auto nested = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
                        auto child = contextFor(nested, context, contexts);
                        if (!child) { return false; }
                        pending.push_back(*child);
                    } else if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
                        if (!bindings.contains(load.getNameAttr())) { return false; }
                    } else if (operation.getNumRegions() != 0 ||
                               (!llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp,
                                           mlir::cf::BranchOp, mlir::cf::CondBranchOp>(operation) &&
                                (!mlir::isPure(&operation) ||
                                 operation.hasTrait<ctjs::CTJSMayThrow>() ||
                                 operation.hasTrait<ctjs::CTJSMayReenterJS>() ||
                                 operation.hasTrait<ctjs::CTJSMaySuspend>()))) {
                        return false;
                    }
                }
            }
        }
        return found;
    }

    bool primitive(mlir::Value value) {
        // This source CFG has already passed the acyclic-tail check. Follow
        // every predecessor operand, including status unwind state: a value
        // joining a primitive and an unknown never proves conversion safe.
        // staticResultType supplies only unconditional normal-result facts;
        // its producer's effects are checked separately below. No cached
        // dataflow state or user-supplied native marker is consulted.
        primitiveContexts contexts;
        llvm::SmallVector<primitiveInput> pending{{value, 0}};
        llvm::DenseSet<primitiveInput> seen;
        bool hasDefinition = false;
        while (!pending.empty()) {
            if (!spend()) { return false; }
            auto [current, context] = pending.pop_back_val();
            value = current;
            if (!seen.insert({value, context}).second) { continue; }
            if (auto * definition = value.getDefiningOp()) {
                const auto type = staticResultType(definition);
                if (llvm::isa_and_nonnull<NumType, BoolType, StrType>(type) ||
                    (llvm::isa_and_nonnull<OptType>(type) &&
                     llvm::isa<BottomType>(llvm::cast<OptType>(type).getElementType()))) {
                    hasDefinition = true;
                    continue;
                }
                if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(definition)) {
                    if (!completionInputs(direct, false, context, contexts, pending)) {
                        return false;
                    }
                    continue;
                }
                if (definition == landing.getOperation() && value == landing.getThrown()) {
                    if (context != 0 || invocations.empty() || throws != 0) { return false; }
                    for (auto [check, direct] : invocations) {
                        (void)check;
                        if (!spend() || !completionInputs(direct, true, 0, contexts, pending)) {
                            return false;
                        }
                    }
                    continue;
                }
                if (llvm::isa<ctjs::BinaryOp, ctjs::BinaryStaticOp, ctjs::UnaryOp>(definition)) {
                    for (auto operand : definition->getOperands()) {
                        pending.emplace_back(operand, context);
                    }
                    continue;
                }
                return false;
            }
            auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
            if (argument && argument.getOwner()->isEntryBlock()) {
                auto owner = argument.getOwner()->getParentOp();
                auto body = llvm::dyn_cast<ctjs::FuncOp>(owner);
                if (!body) { return false; }
                if (context != 0) {
                    auto frame = contexts.frames[context];
                    if (body.getSymName() != frame.call.getCallee() ||
                        argument.getArgNumber() >= frame.call.getNumOperands()) {
                        return false;
                    }
                    pending.emplace_back(frame.call->getOperand(argument.getArgNumber()),
                                         frame.parent);
                    continue;
                }
                // No selected invocation: a body-wide effect proof needs all
                // live actuals, including callers outside the protected try.
                // The complete acyclic census above prevents optimistic
                // recursion through a formal with one known incoming value.
                auto target =
                    module ? module.lookupSymbol<ctjs::FuncOp>(body.getSymName()) : ctjs::FuncOp{};
                auto callers = boundCallers.find(target);
                if (!target || !boundTargets.contains(target) || callers == boundCallers.end() ||
                    callers->second.empty()) {
                    return false;
                }
                for (auto call : callers->second) {
                    if (!spend() || argument.getArgNumber() >= call.getNumOperands()) {
                        return false;
                    }
                    pending.emplace_back(call->getOperand(argument.getArgNumber()), 0);
                }
                continue;
            }
            if (!argument || argument.getOwner()->isEntryBlock() ||
                argument.getOwner()->hasNoPredecessors()) {
                return false;
            }
            auto & block = *argument.getOwner();
            for (auto pred = block.pred_begin(), end = block.pred_end(); pred != end; ++pred) {
                if (!spend()) { return false; }
                auto branch = llvm::dyn_cast<mlir::BranchOpInterface>((*pred)->getTerminator());
                if (!branch) { return false; }
                auto operands = branch.getSuccessorOperands(pred.getSuccessorIndex());
                const unsigned index = argument.getArgNumber();
                if (index >= operands.size() || operands.isOperandProduced(index)) { return false; }
                pending.emplace_back(operands[index], context);
            }
        }
        return hasDefinition;
    }

    bool nonthrowing(mlir::Operation * operation) {
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
            return bindings.contains(load.getNameAttr());
        }
        if (llvm::isa<ctjs::CheckOp, ctjs::ReturnOp, ctjs::ThrowOp, ctjs::PopHandlerOp,
                      ctjs::FrameExitOp, ctjs::CatchLandOp, ctjs::RootOp, mlir::cf::BranchOp,
                      mlir::cf::CondBranchOp, mlir::cf::SwitchOp>(operation)) {
            return true;
        }
        // Pure alone is not a substitute for CTJS's exception/reentry traits.
        // Unknown operations have neither an effect proof nor a special case.
        if (mlir::isPure(operation) && !operation->hasTrait<ctjs::CTJSMayThrow>() &&
            !operation->hasTrait<ctjs::CTJSMayReenterJS>() &&
            !operation->hasTrait<ctjs::CTJSMaySuspend>()) {
            return true;
        }
        if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation)) {
            if (unary.getKind() == ctjs::UnaryKind::Not ||
                unary.getKind() == ctjs::UnaryKind::TypeOf ||
                unary.getKind() == ctjs::UnaryKind::Void) {
                return true;
            }
        }
        if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
            if (compare.getKind() == ctjs::CompareKind::StrictEq) { return true; }
        }
        if (auto convert = llvm::dyn_cast<ctjs::ConvertOp>(operation)) {
            if (convert.getKind() == ctjs::ConvertKind::ToBoolean) { return true; }
            if (convert.getKind() == ctjs::ConvertKind::ToObject) { return false; }
            return primitive(convert.getOperand());
        }
        if (llvm::isa<ctjs::BinaryOp, ctjs::BinaryStaticOp, ctjs::UnaryOp, ctjs::CompareOp>(
                operation)) {
            return llvm::all_of(operation->getOperands(),
                                [&](mlir::Value value) { return primitive(value); });
        }
        return false;
    }

    bool proveEffects(const tail & normal, const tail & caught) {
        // The old check still owns both successors while this runs. Only the
        // exact call recorded by inspectInvocation has a represented unwind;
        // a property read, callee lookup, second call or callback cannot borrow
        // that permission. Scan both continuations, including unchecked catch
        // operations, before constructing or adopting any recovered body.
        bool needsBindings = false;
        for (const tail * plan : {&normal, &caught}) {
            for (mlir::Block * block : plan->blocks) {
                for (mlir::Operation & operation : *block) {
                    if (!spend()) { return false; }
                    needsBindings |= llvm::isa<ctjs::LoadGlobalOp>(operation);
                }
            }
        }
        if (needsBindings && !proveBindings()) { return false; }
        for (const tail * plan : {&normal, &caught}) {
            for (mlir::Block * block : plan->blocks) {
                for (mlir::Operation & operation : *block) {
                    if (!spend(uint64_t(1) + operation.getNumOperands())) { return false; }
                    auto invocation = invocations.lookup(block->getTerminator());
                    if (plan == &normal && invocation.getOperation() == &operation) { continue; }
                    if (!nonthrowing(&operation)) {
                        return reject(("native invocation recovery cannot prove a nonthrowing "
                                       "status or continuation operation: `" +
                                       operation.getName().getStringRef() + "`")
                                          .str());
                    }
                }
            }
        }
        return true;
    }

    bool collect(tail & result, mlir::Block * start, bool initiallyActive, bool isCatch) {
        llvm::SmallVector<std::pair<mlir::Block *, bool>> pending{{start, initiallyActive}};
        while (!pending.empty()) {
            if (!spend()) { return false; }
            auto [block, active] = pending.pop_back_val();
            if (block == &function.getBody().front() || block->getParent() != &function.getBody() ||
                block->getNumArguments() != width || (!isCatch && block == push.getHandler())) {
                return reject("native exception tail leaves its preserved register CFG");
            }
            auto [known, inserted] = result.active.try_emplace(block, active);
            if (!inserted) {
                if (known->second != active) {
                    return reject("native exception handler balance differs at a CFG join");
                }
                continue;
            }
            result.blocks.push_back(block);
            for (mlir::Operation & operation : block->without_terminator()) {
                if (!spend()) { return false; }
                if (llvm::isa<ctjs::PopHandlerOp>(operation)) {
                    if (!active) {
                        return reject("native exception path pops an inactive handler");
                    }
                    active = false;
                }
                if (auto found = llvm::dyn_cast<ctjs::CatchLandOp>(operation)) {
                    if (!isCatch || block != start || found != landing) {
                        return reject("native exception tail contains another catch landing");
                    }
                }
            }
            auto * term = block->getTerminator();
            if (llvm::isa<ctjs::ThrowOp>(term)) {
                if (!active || isCatch) {
                    return reject(
                        "native exception recovery does not catch a throw outside the active try");
                }
                for (mlir::Operation & operation : block->without_terminator()) {
                    if (!llvm::isa<ctjs::RootOp>(operation)) {
                        return reject("native throw needs an unchanged throw-site register block");
                    }
                }
                ++throws;
                continue;
            }
            if (llvm::isa<ctjs::ReturnOp>(term)) {
                // A function return also leaves an active handler; the
                // importer emits frame_exit rather than a separate pop for
                // return inside try. Completion returns from the recovered
                // scope and never re-enters its catch.
                continue;
            }
            if (auto check = llvm::dyn_cast<ctjs::CheckOp>(term)) {
                // Completed computations can change registers before a later
                // status check, so its handler values need not equal block
                // entry arguments. Keep the normal edge's values verbatim.
                // Discarding its handler edge still requires admission to
                // prove every protected operation nonthrowing; explicit
                // throws still need an unchanged throw-site block above.
                if (!active || check.getHandler() != push.getHandler() ||
                    check.getHandlerOperands().size() != width) {
                    return reject(
                        "native exception check lacks a complete register vector for its handler");
                }
                if (mode != ExceptionRecoveryMode::ExplicitThrows && !inspectInvocation(check)) {
                    return false;
                }
                result.edges[block].push_back(check.getCont());
            } else if (llvm::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp, mlir::cf::SwitchOp>(
                           term)) {
                for (mlir::Block * successor : term->getSuccessors()) {
                    result.edges[block].push_back(successor);
                }
            } else {
                return reject("native exception tail has an unsupported control-flow exit");
            }
            for (mlir::Block * successor : result.edges[block]) {
                if (!spend()) { return false; }
                pending.emplace_back(successor, active);
            }
        }
        // Kahn's algorithm rejects cycles without recursion or speculative
        // unrolling; shared normal/catch continuations remain ordinary DAGs.
        llvm::DenseMap<mlir::Block *, unsigned> incoming;
        for (mlir::Block * block : result.blocks) {
            for (mlir::Block * successor : result.edges[block]) {
                if (!spend()) { return false; }
                ++incoming[successor];
            }
        }
        llvm::SmallVector<mlir::Block *> ready;
        for (mlir::Block * block : result.blocks) {
            if (incoming.lookup(block) == 0) { ready.push_back(block); }
        }
        unsigned visited = 0;
        while (!ready.empty()) {
            if (!spend()) { return false; }
            auto * block = ready.pop_back_val();
            ++visited;
            for (mlir::Block * successor : result.edges[block]) {
                if (--incoming[successor] == 0) { ready.push_back(successor); }
            }
        }
        return visited == result.blocks.size() ||
               reject("native exception recovery does not support loops");
    }

    bool cloneTail(const tail & plan, mlir::Region & destination, bool isCatch) {
        mlir::IRMapping mapping;
        auto * start = plan.blocks.front();
        for (mlir::Block * block : plan.blocks) {
            if (!spend(uint64_t(1) + block->getNumArguments())) { return false; }
            auto * copy = new mlir::Block;
            destination.push_back(copy);
            mapping.map(block, copy);
            if (block == start && !isCatch) {
                for (auto [argument, value] :
                     llvm::zip(block->getArguments(), push.getBodyOperands())) {
                    mapping.map(argument, value);
                }
                continue;
            }
            if (block == start) {
                copy->addArgument(ctjs::ValueType::get(function.getContext()), landing.getLoc());
            }
            for (mlir::BlockArgument argument : block->getArguments()) {
                mapping.map(argument, copy->addArgument(argument.getType(), argument.getLoc()));
            }
        }
        if (isCatch) { mapping.map(landing.getThrown(), destination.front().getArgument(0)); }
        struct operandsToMap {
            mlir::Operation * operation;
            llvm::SmallVector<mlir::Value> values;
        };
        llvm::SmallVector<operandsToMap> operands;
        for (mlir::Block * block : plan.blocks) {
            mlir::OpBuilder builder = mlir::OpBuilder::atBlockEnd(mapping.lookup(block));
            ctjs::InvokeOp invocation;
            for (mlir::Operation & operation : *block) {
                if (!spend(uint64_t(1) + operation.getNumOperands())) { return false; }
                if (llvm::isa<ctjs::PopHandlerOp, ctjs::FrameExitOp, ctjs::CatchLandOp>(
                        operation)) {
                    continue;
                }
                mlir::Operation * copied = nullptr;
                llvm::SmallVector<mlir::Value> sources;
                if (!isCatch &&
                    invocations.lookup(block->getTerminator()).getOperation() == &operation) {
                    // The invocation returns a value-only completion tuple:
                    // JS boolean, normal result, thrown payload, saved state.
                    // Convert the boolean only after leaving the invocation;
                    // try_exit's i1 never crosses a CTJS value region edge.
                    if (!spend(uint64_t(20) + width * uint64_t(4))) { return false; }
                    auto type = ctjs::ValueType::get(function.getContext());
                    mlir::OperationState state(operation.getLoc(), "ctjs.invoke");
                    llvm::SmallVector<mlir::Type> types(width + 3, type);
                    state.addTypes(types);
                    for (unsigned index = 0; index != 3; ++index) { state.addRegion(); }
                    invocation = llvm::cast<ctjs::InvokeOp>(builder.create(state));
                    auto & body = invocation.getBody().emplaceBlock();
                    auto & normal = invocation.getNormalBody().emplaceBlock();
                    auto & unwind = invocation.getUnwindBody().emplaceBlock();
                    normal.addArgument(type, operation.getLoc());
                    for (unsigned index = 0; index != width + 1; ++index) {
                        unwind.addArgument(type, operation.getLoc());
                    }
                    mlir::OpBuilder callBuilder = mlir::OpBuilder::atBlockEnd(&body);
                    copied = callBuilder.clone(operation, mapping);
                    llvm::append_range(sources, operation.getOperands());
                    auto check = llvm::cast<ctjs::CheckOp>(block->getTerminator());
                    llvm::SmallVector<mlir::Value> stateValues{copied->getResult(0)};
                    llvm::append_range(stateValues, check.getHandlerOperands());
                    mlir::OperationState dispatch(operation.getLoc(), "ctjs.invoke_exit");
                    dispatch.addOperands(stateValues);
                    operands.push_back({callBuilder.create(dispatch), std::move(stateValues)});
                    mapping.map(operation.getResult(0), invocation.getResult(1));
                    for (auto [continuation, failed] :
                         {std::pair{&normal, false}, std::pair{&unwind, true}}) {
                        mlir::OpBuilder at = mlir::OpBuilder::atBlockEnd(continuation);
                        auto flag = ctjs::ConstantOp::create(
                            at, operation.getLoc(), type,
                            ctjs::BooleanAttr::get(function.getContext(), failed));
                        auto poison = mlir::ub::PoisonOp::create(at, operation.getLoc(), type);
                        llvm::SmallVector<mlir::Value> completion{flag.getResult()};
                        if (failed) {
                            completion.push_back(poison.getResult());
                            llvm::append_range(completion, unwind.getArguments());
                        } else {
                            completion.push_back(normal.getArgument(0));
                            completion.append(width + 1, poison.getResult());
                        }
                        mlir::OperationState yield(operation.getLoc(), "ctjs.invoke_yield");
                        yield.addOperands(completion);
                        at.create(yield);
                    }
                } else if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                    mlir::OperationState state(operation.getLoc(),
                                               isCatch ? "ctjs.try_yield" : "ctjs.try_exit");
                    if (!isCatch) {
                        sources.push_back(
                            mlir::arith::ConstantIntOp::create(builder, operation.getLoc(), 0, 1));
                    }
                    sources.push_back(returned.getValue());
                    if (!isCatch) {
                        auto poison =
                            mlir::ub::PoisonOp::create(builder, operation.getLoc(),
                                                       ctjs::ValueType::get(function.getContext()));
                        sources.append(width + 1, poison.getResult());
                    }
                    state.addOperands(sources);
                    copied = builder.create(state);
                } else if (auto thrown = llvm::dyn_cast<ctjs::ThrowOp>(operation)) {
                    sources.push_back(
                        mlir::arith::ConstantIntOp::create(builder, operation.getLoc(), 1, 1));
                    sources.push_back(
                        mlir::ub::PoisonOp::create(builder, operation.getLoc(),
                                                   ctjs::ValueType::get(function.getContext()))
                            .getResult());
                    sources.push_back(thrown.getValue());
                    llvm::append_range(sources, block->getArguments());
                    mlir::OperationState state(operation.getLoc(), "ctjs.try_exit");
                    state.addOperands(sources);
                    copied = builder.create(state);
                } else if (auto check = llvm::dyn_cast<ctjs::CheckOp>(operation)) {
                    if (invocation) {
                        auto * thrown = new mlir::Block;
                        destination.push_back(thrown);
                        mlir::OpBuilder at = mlir::OpBuilder::atBlockEnd(thrown);
                        auto flag =
                            mlir::arith::ConstantIntOp::create(at, operation.getLoc(), 1, 1);
                        auto poison = mlir::ub::PoisonOp::create(
                            at, operation.getLoc(), ctjs::ValueType::get(function.getContext()));
                        llvm::SmallVector<mlir::Value> completion{flag.getResult(),
                                                                  poison.getResult()};
                        llvm::append_range(completion, invocation.getResults().drop_front(2));
                        mlir::OperationState exit(operation.getLoc(), "ctjs.try_exit");
                        exit.addOperands(completion);
                        at.create(exit);
                        auto failed =
                            ctjs::TruthyOp::create(builder, operation.getLoc(), builder.getI1Type(),
                                                   invocation.getResult(0));
                        llvm::append_range(sources, check.getContOperands());
                        copied = mlir::cf::CondBranchOp::create(
                            builder, operation.getLoc(), failed.getResult(), thrown,
                            mlir::ValueRange{}, mapping.lookup(check.getCont()), sources);
                        sources.insert(sources.begin(), failed.getResult());
                    } else {
                        llvm::append_range(sources, check.getContOperands());
                        copied = mlir::cf::BranchOp::create(
                            builder, operation.getLoc(), mapping.lookup(check.getCont()), sources);
                    }
                } else {
                    copied = builder.clone(operation, mapping);
                    llvm::append_range(sources, operation.getOperands());
                }
                operands.push_back({copied, std::move(sources)});
            }
        }
        // Imported block order need not be dominance order. Remap operands
        // only after every defining operation has a clone.
        for (auto & pending : operands) {
            for (auto [index, source] : llvm::enumerate(pending.values)) {
                if (!spend()) { return false; }
                auto value = mapping.lookupOrDefault(source);
                if (value == source &&
                    source.getParentBlock()->getParent() == &function.getBody() &&
                    source.getParentBlock() != &function.getBody().front()) {
                    return reject("native exception tail captures an unmapped register definition");
                }
                pending.operation->setOperand(static_cast<unsigned>(index), value);
            }
        }
        return true;
    }

    bool structure(mlir::Region & region) {
        // The upstream algorithm has no work callback. Precharge a conservative
        // quadratic bound for this acyclic, finite register CFG before calling
        // it; a failure only damages the disposable function clone.
        const uint64_t blocks = region.getBlocks().size();
        const uint64_t scale = uint64_t(width) + 8;
        // Compare by division before multiplying, including the block square.
        if (blocks + 1 > remaining / scale / (blocks + 1)) {
            return reject("native exception recovery work budget exhausted");
        }
        if (!spend((blocks + 1) * (blocks + 1) * scale)) { return false; }
        mlir::IRRewriter rewriter(function.getContext());
        (void)mlir::simplifyRegions(rewriter, llvm::MutableArrayRef<mlir::Region>(region));
        mlir::DominanceInfo dominance(function);
        mlir::ControlFlowToSCFTransformation transformation;
        std::string diagnostic;
        mlir::FailureOr<bool> transformed = mlir::failure();
        {
            mlir::ScopedDiagnosticHandler capture(function.getContext(),
                                                  [&](mlir::Diagnostic & note) {
                                                      if (diagnostic.empty()) {
                                                          diagnostic = note.str();
                                                      }
                                                      return mlir::success();
                                                  });
            transformed = mlir::transformCFGToSCF(region, transformation, dominance);
        }
        if (mlir::failed(transformed) || !region.hasOneBlock()) {
            return reject(diagnostic.empty()
                              ? "native exception region did not structure completely"
                              : diagnostic);
        }
        return true;
    }

    bool normalizeIndexSwitches(mlir::Operation * guarded) {
        // LLVM may use an index_switch to dispatch merged completion exits.
        // Expose that dispatch as comparisons and nested ifs, retaining each
        // selected region and its exact yields. No case body is evaluated or
        // cloned, and the default body remains the final else region.
        llvm::SmallVector<mlir::scf::IndexSwitchOp> switches;
        auto walked = guarded->walk([&](mlir::Operation * op) {
            if (!spend()) { return mlir::WalkResult::interrupt(); }
            if (auto found = llvm::dyn_cast<mlir::scf::IndexSwitchOp>(op)) {
                switches.push_back(found);
            }
            return mlir::WalkResult::advance();
        });
        if (walked.wasInterrupted()) { return false; }
        // The walk is postorder, so moving an enclosing case never leaves a
        // queued switch inside an operation that has already been erased.
        for (mlir::scf::IndexSwitchOp switcher : switches) {
            const unsigned count = switcher.getNumResults();
            if (switcher.getCases().size() != switcher.getCaseRegions().size()) {
                return reject("native exception switch lost its case correspondence");
            }
            for (mlir::Region & region : switcher->getRegions()) {
                if (!spend(uint64_t(1) + count)) { return false; }
                if (!region.hasOneBlock() || region.front().empty() ||
                    region.front().getNumArguments() != 0 ||
                    !llvm::isa<mlir::scf::YieldOp>(region.front().getTerminator()) ||
                    region.front().getTerminator()->getNumOperands() != count) {
                    return reject("native exception switch lost its result correspondence");
                }
            }
            for (mlir::Value result : switcher.getResults()) {
                for (mlir::OpOperand & use : result.getUses()) {
                    (void)use;
                    if (!spend()) { return false; }
                }
            }
            if (switcher.getCases().empty()) {
                if (!spend(uint64_t(2) + count)) { return false; }
                auto & block = switcher.getDefaultRegion().front();
                auto * yield = block.getTerminator();
                llvm::SmallVector<mlir::Value> values(yield->getOperands());
                yield->erase();
                switcher->getBlock()->getOperations().splice(switcher->getIterator(),
                                                             block.getOperations());
                for (auto [result, value] : llvm::zip(switcher.getResults(), values)) {
                    result.replaceAllUsesWith(value);
                }
                switcher.erase();
                continue;
            }
            mlir::OpBuilder builder(switcher);
            mlir::scf::IfOp first;
            for (auto [index, value] : llvm::enumerate(switcher.getCases())) {
                if (!spend(uint64_t(6) + count * uint64_t(2))) { return false; }
                auto key = mlir::arith::ConstantIndexOp::create(builder, switcher.getLoc(), value);
                auto condition = mlir::arith::CmpIOp::create(builder, switcher.getLoc(),
                                                             mlir::arith::CmpIPredicate::eq,
                                                             switcher.getArg(), key);
                auto branch = mlir::scf::IfOp::create(builder, switcher.getLoc(),
                                                      switcher.getResultTypes(), condition);
                branch.getThenRegion().takeBody(switcher.getCaseRegions()[index]);
                if (first) {
                    mlir::scf::YieldOp::create(builder, switcher.getLoc(), branch.getResults());
                } else {
                    first = branch;
                }
                if (index + 1 == switcher.getCases().size()) {
                    branch.getElseRegion().takeBody(switcher.getDefaultRegion());
                } else {
                    branch.getElseRegion().emplaceBlock();
                    builder.setInsertionPointToEnd(&branch.getElseRegion().front());
                }
            }
            for (auto [result, value] : llvm::zip(switcher.getResults(), first.getResults())) {
                result.replaceAllUsesWith(value);
            }
            switcher.erase();
        }
        return true;
    }

    static bool isFullPoison(mlir::Value value) {
        auto poison = value.getDefiningOp<mlir::ub::PoisonOp>();
        return poison && (!poison.getValue() || llvm::isa<mlir::ub::PoisonAttr>(poison.getValue()));
    }

    bool trimUnusedIfResults(mlir::Operation * guarded) {
        // Catch-state pruning can orphan the corresponding SCF results,
        // including slots containing only poison. Trim result/yield pairs
        // explicitly: branch bodies and their effects must still execute.
        // Preorder exposes an outer result's unused inner producers in the
        // same round. A fixpoint also handles producers in earlier siblings
        // and all-poison slots forwarded through nested conditionals. Only
        // explicit, fully poisoned incoming values authorize that collapse;
        // an inferred Bottom type is never sufficient evidence.
        llvm::SmallVector<mlir::scf::IfOp> branches;
        auto walked = guarded->walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * op) {
            if (!spend()) { return mlir::WalkResult::interrupt(); }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) { branches.push_back(branch); }
            return mlir::WalkResult::advance();
        });
        if (walked.wasInterrupted()) { return false; }
        bool changed = true;
        while (changed) {
            changed = false;
            for (mlir::scf::IfOp & branch : branches) {
                const unsigned count = branch.getNumResults();
                if (!spend(uint64_t(1) + count)) { return false; }
                if (count == 0) { continue; }
                for (mlir::Region & region : branch->getRegions()) {
                    if (!region.hasOneBlock() || region.front().empty() ||
                        !llvm::isa<mlir::scf::YieldOp>(region.front().getTerminator()) ||
                        region.front().getTerminator()->getNumOperands() != count) {
                        return reject(
                            "native exception conditional lost its result correspondence");
                    }
                }
                llvm::BitVector unused(count);
                llvm::SmallVector<mlir::Type> types;
                for (auto [index, result] : llvm::enumerate(branch.getResults())) {
                    if (result.use_empty()) {
                        unused.set(static_cast<unsigned>(index));
                        continue;
                    }
                    for (mlir::OpOperand & use : result.getUses()) {
                        (void)use;
                        if (!spend()) { return false; }
                    }
                    if (!spend(2)) { return false; }
                    if (isFullPoison(branch.getThenRegion().front().getTerminator()->getOperand(
                            static_cast<unsigned>(index))) &&
                        isFullPoison(branch.getElseRegion().front().getTerminator()->getOperand(
                            static_cast<unsigned>(index)))) {
                        if (!spend()) { return false; }
                        mlir::OpBuilder before(branch);
                        auto poison =
                            mlir::ub::PoisonOp::create(before, branch.getLoc(), result.getType());
                        result.replaceAllUsesWith(poison);
                        unused.set(static_cast<unsigned>(index));
                        continue;
                    }
                    types.push_back(result.getType());
                }
                if (unused.none()) { continue; }
                if (!spend(uint64_t(3) + count * uint64_t(2))) { return false; }
                mlir::OpBuilder builder(branch);
                mlir::OperationState state(branch.getLoc(), mlir::scf::IfOp::getOperationName());
                state.addOperands(branch.getCondition());
                state.addTypes(types);
                state.addAttributes(branch->getAttrs());
                state.addRegion();
                state.addRegion();
                auto replacement = llvm::cast<mlir::scf::IfOp>(builder.create(state));
                for (auto [source, destination] :
                     llvm::zip(branch->getRegions(), replacement->getRegions())) {
                    source.front().getTerminator()->eraseOperands(unused);
                    destination.takeBody(source);
                }
                unsigned next = 0;
                for (auto [index, result] : llvm::enumerate(branch.getResults())) {
                    if (!unused.test(static_cast<unsigned>(index))) {
                        result.replaceAllUsesWith(replacement.getResult(next++));
                    }
                }
                branch.erase();
                branch = replacement;
                changed = true;
            }
        }
        return true;
    }

    bool run() {
        if (!inspect()) { return false; }
        tail normal, caught;
        if (!collect(normal, push.getBody(), true, false) ||
            !collect(caught, push.getHandler(), false, true)) {
            return false;
        }
        if (throws == 0 && invocations.empty()) {
            return reject("native try/catch needs an explicit throw in its active handler");
        }
        if (mode == ExceptionRecoveryMode::EffectCheckedInvocations &&
            !proveEffects(normal, caught)) {
            return false;
        }
        mlir::OpBuilder builder(push);
        mlir::OperationState state(push.getLoc(), "ctjs.try");
        state.addTypes(ctjs::ValueType::get(function.getContext()));
        state.addRegion();
        state.addRegion();
        auto * guarded = builder.create(state);
        if (!cloneTail(normal, guarded->getRegion(0), false) ||
            !cloneTail(caught, guarded->getRegion(1), true) || !structure(guarded->getRegion(0)) ||
            !structure(guarded->getRegion(1))) {
            return false;
        }
        auto & body = guarded->getRegion(0).front();
        auto & handler = guarded->getRegion(1).front();
        auto * exit = body.getTerminator();
        if (exit->getName().getStringRef() != "ctjs.try_exit" ||
            handler.getTerminator()->getName().getStringRef() != "ctjs.try_yield" ||
            exit->getNumOperands() != handler.getNumArguments() + 2) {
            return reject("native exception completion lost its catch-state correspondence");
        }
        // Payload stays first even when unused. Only live original registers
        // require mutable storage spanning the native try and catch regions.
        for (unsigned index = handler.getNumArguments(); index-- > 1;) {
            if (!spend()) { return false; }
            if (!handler.getArgument(index).use_empty()) { continue; }
            exit->eraseOperand(index + 2);
            handler.eraseArgument(index);
        }
        if (!normalizeIndexSwitches(guarded) || !trimUnusedIfResults(guarded)) { return false; }
        push.erase();
        builder.setInsertionPointToEnd(&function.getBody().front());
        ctjs::FrameExitOp::create(builder, guarded->getLoc(), frame.getResult());
        ctjs::ReturnOp::create(builder, guarded->getLoc(), guarded->getResult(0));
        llvm::SmallVector<mlir::Block *> old;
        for (mlir::Block & block : llvm::drop_begin(function.getBody())) { old.push_back(&block); }
        for (mlir::Block * block : old) { block->dropAllReferences(); }
        for (mlir::Block * block : old) { block->erase(); }
        function->removeAttr("ctjs.not_structured");
        return true;
    }
};

} // namespace

ExceptionRecoveryResult recoverPrimitiveExceptionRegion(ctjs::FuncOp function, unsigned maxSteps,
                                                        ExceptionRecoveryMode mode) {
    // Bound the source scan and reserve another scan's cost for the initial
    // clone before allocating it. The caller selects handler-containing
    // functions; no rewrite is visible until every stage succeeds.
    recovery attempt{function, maxSteps, mode};
    if (!attempt.inspect() || !attempt.spend(maxSteps - attempt.remaining)) {
        return {false, std::move(attempt.refusal), {}, maxSteps - attempt.remaining};
    }
    function.getContext()->getOrLoadDialect<mlir::arith::ArithDialect>();
    function.getContext()->getOrLoadDialect<mlir::ub::UBDialect>();
    function.getContext()->getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    if (mode == ExceptionRecoveryMode::EffectCheckedInvocations) {
        function.getContext()->getOrLoadDialect<CTNativeDialect>();
    }
    mlir::OwningOpRef<ctjs::FuncOp> scratch(
        llvm::cast<ctjs::FuncOp>(function->clone(attempt.copies)));
    attempt.function = *scratch;
    if (!attempt.run()) {
        return {false, std::move(attempt.refusal), {}, maxSteps - attempt.remaining};
    }
    mlir::Region original;
    original.takeBody(function.getBody());
    function.getBody().takeBody(scratch->getBody());
    scratch->getBody().takeBody(original);
    // run() only changed this diagnostic attribute. The returned snapshot
    // must retain the original attributes as well as the original body.
    (*scratch)->setAttrs(function->getAttrs());
    function->removeAttr("ctjs.not_structured");
    return {true, {}, std::move(scratch), maxSteps - attempt.remaining};
}

} // namespace ctcompile::ctnative::lowering_detail
