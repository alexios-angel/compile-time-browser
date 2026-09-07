#include "Recovery.h"

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
#include "mlir/Transforms/CFGToSCF.h"
#include "mlir/Transforms/RegionUtils.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
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
    unsigned remaining;
    std::string refusal;
    ctjs::PushHandlerOp push;
    ctjs::CatchLandOp landing;
    ctjs::FrameEnterOp frame;
    unsigned width = 0;
    unsigned throws = 0;

    recovery(ctjs::FuncOp function, unsigned maxSteps) : function(function), remaining(maxSteps) {}

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
                if (!active || check.getHandler() != push.getHandler() ||
                    check.getHandlerOperands().size() != width ||
                    !llvm::equal(check.getHandlerOperands(), block->getArguments())) {
                    return reject(
                        "native exception check lacks its current pre-operation register vector");
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
            for (mlir::Operation & operation : *block) {
                if (!spend(uint64_t(1) + operation.getNumOperands())) { return false; }
                if (llvm::isa<ctjs::PopHandlerOp, ctjs::FrameExitOp, ctjs::CatchLandOp>(
                        operation)) {
                    continue;
                }
                mlir::Operation * copied = nullptr;
                llvm::SmallVector<mlir::Value> sources;
                if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
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
                    llvm::append_range(sources, check.getContOperands());
                    copied = mlir::cf::BranchOp::create(builder, operation.getLoc(),
                                                        mapping.lookup(check.getCont()), sources);
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
        if (throws == 0) {
            return reject("native try/catch needs an explicit throw in its active handler");
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

ExceptionRecoveryResult recoverNumericExceptionRegion(ctjs::FuncOp function, unsigned maxSteps) {
    // Bound the source scan and reserve another scan's cost for the initial
    // clone before allocating it. The caller selects handler-containing
    // functions; no rewrite is visible until every stage succeeds.
    recovery attempt{function, maxSteps};
    if (!attempt.inspect() || !attempt.spend(maxSteps - attempt.remaining)) {
        return {false, std::move(attempt.refusal)};
    }
    function.getContext()->getOrLoadDialect<mlir::arith::ArithDialect>();
    function.getContext()->getOrLoadDialect<mlir::ub::UBDialect>();
    function.getContext()->getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    mlir::OwningOpRef<ctjs::FuncOp> scratch(llvm::cast<ctjs::FuncOp>(function->clone()));
    attempt.function = *scratch;
    if (!attempt.run()) { return {false, std::move(attempt.refusal)}; }
    mlir::Region original;
    original.takeBody(function.getBody());
    function.getBody().takeBody(scratch->getBody());
    scratch->getBody().takeBody(original);
    // run() only changed this diagnostic attribute. The returned snapshot
    // must retain the original attributes as well as the original body.
    (*scratch)->setAttrs(function->getAttrs());
    function->removeAttr("ctjs.not_structured");
    return {true, {}, std::move(scratch)};
}

} // namespace ctcompile::ctnative::lowering_detail
