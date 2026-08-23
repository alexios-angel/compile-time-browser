// BLOCK ARGUMENTS, TURNED INTO VARIABLES, BECAUSE THE C++ EMITTER LOSES A COPY.
//
// mlir-translate --mlir-to-cpp serialises the parallel copy on a branch edge
// naively - one assignment at a time, in order, with no temporaries - so an edge
// where one block argument's new value is another block argument of the SAME
// block reads a destination that has already been overwritten.
// test/Lowering/EmitC/block-argument-hazard.mlir compiles and RUNS that
// miscompile: a loop that swaps two values returns 20 where the IR says 10, with
// no diagnostic and exit status 0.
//
// THIS PROJECT MEETS IT ON ORDINARY PROGRAMS. The bytecode importer models the
// register file as block arguments - chosen over SSA construction deliberately -
// so every register live across a branch is a block argument, and any loop that
// permutes two registers across its back edge is exactly that shape.
//
// SO THE FIX IS NOT A WORKAROUND, IT IS THE LOWERING. A block argument becomes
// an emitc.variable declared once in the entry block; the block reads it at its
// top, and each incoming edge writes it. Every read is issued before any write
// because the reads sit at the top of the block and the writes sit on the edges
// into it, which is what makes the swap come out right.
//
// EDGES ARE SPLIT RATHER THAN ASSIGNED IN PLACE, and that is not tidiness. A
// cf.cond_br may name the SAME successor twice with different operands -
// `cf.cond_br %c, ^B(%x), ^B(%y)` is legal and the importer can produce it - and
// writing both sets before the branch would perform both assignments on
// whichever path is taken. The second would win, unconditionally. Putting each
// edge's writes in a block of their own is the only placement that is correct
// for every terminator, so it is used for all of them rather than only where the
// hazard is visible.
#include "ctcompile/CTJS/Transforms/Passes.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"

namespace ctcompile::ctjs {

#define GEN_PASS_DEF_EMITCELIMINATEBLOCKARGUMENTS
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

namespace {

namespace ec = mlir::emitc;

struct EmitCEliminateBlockArgumentsPass
    : impl::EmitCEliminateBlockArgumentsBase<EmitCEliminateBlockArgumentsPass> {
    using EmitCEliminateBlockArgumentsBase::EmitCEliminateBlockArgumentsBase;

    void runOnOperation() override {
        llvm::SmallVector<ec::FuncOp> functions;
        getOperation().walk([&](ec::FuncOp function) { functions.push_back(function); });
        for (ec::FuncOp function : functions) {
            if (mlir::failed(eliminate(function))) {
                signalPassFailure();
                return;
            }
        }
    }

    mlir::LogicalResult eliminate(ec::FuncOp function) {
        mlir::Region & region = function.getBody();
        if (region.empty()) { return mlir::success(); }

        // THE ENTRY BLOCK'S ARGUMENTS ARE THE FUNCTION'S PARAMETERS and stay
        // exactly as they are: they are real C++ parameters, no edge writes
        // them, and the hazard cannot reach them.
        llvm::SmallVector<mlir::Block *> carrying;
        for (mlir::Block & block : llvm::drop_begin(region)) {
            if (block.getNumArguments() > 0) { carrying.push_back(&block); }
        }
        if (carrying.empty()) { return mlir::success(); }

        mlir::OpBuilder build(function.getContext());
        mlir::Block & entry = region.front();

        // ---- one variable per block argument, declared in the entry --------
        //
        // In the ENTRY BLOCK because a definition has to dominate its uses, and
        // the entry dominates every block. The C++ emitter hoists all
        // declarations to the top of the function anyway under
        // --declare-variables-at-top, which it requires for a multi-block
        // function - so this costs nothing in the output.
        llvm::DenseMap<mlir::Block *, llvm::SmallVector<mlir::Value>> slots;
        for (mlir::Block * block : carrying) {
            build.setInsertionPointToStart(&entry);
            llvm::SmallVector<mlir::Value> here;
            here.reserve(block->getNumArguments());
            for (const mlir::BlockArgument argument : block->getArguments()) {
                if (!ec::isSupportedEmitCType(argument.getType())) {
                    return function.emitOpError()
                           << "a block argument's type is not one EmitC can declare a variable of: "
                           << argument.getType();
                }
                here.push_back(ec::VariableOp::create(
                    build, argument.getLoc(), ec::LValueType::get(argument.getType()),
                    ec::OpaqueAttr::get(function.getContext(), "")));
            }
            slots[block] = std::move(here);
        }

        // ---- every read, at the top of the block that reads ----------------
        for (mlir::Block * block : carrying) {
            build.setInsertionPointToStart(block);
            const llvm::SmallVector<mlir::Value> & here = slots[block];
            for (auto [index, argument] : llvm::enumerate(block->getArguments())) {
                auto read = ec::LoadOp::create(build, argument.getLoc(), argument.getType(),
                                               here[index]);
                argument.replaceAllUsesWith(read.getResult());
            }
        }

        // ---- every write, on an edge of its own ----------------------------
        //
        // Collected before anything is rewritten: creating the edge blocks
        // appends to the region, and iterating a region while adding blocks to
        // it walks over the ones just added.
        llvm::SmallVector<mlir::Operation *> terminators;
        for (mlir::Block & block : region) {
            if (mlir::Operation * end = block.getTerminator()) { terminators.push_back(end); }
        }
        for (mlir::Operation * end : terminators) {
            auto branch = mlir::dyn_cast<mlir::BranchOpInterface>(end);
            if (!branch) {
                // A terminator with successors that cannot describe its
                // successor operands is one this pass cannot rewrite - and
                // guessing would silently drop the values it carries.
                if (end->getNumSuccessors() > 0) {
                    return end->emitOpError()
                           << "has successors but does not implement BranchOpInterface, so its "
                              "edge operands cannot be turned into assignments";
                }
                continue;
            }
            for (unsigned index = 0; index < end->getNumSuccessors(); ++index) {
                mlir::Block * target = end->getSuccessor(index);
                if (!slots.contains(target)) { continue; }
                mlir::SuccessorOperands passed = branch.getSuccessorOperands(index);
                if (passed.size() == 0) { continue; }

                mlir::Block * edge = build.createBlock(&region, region.end());
                build.setInsertionPointToStart(edge);
                const llvm::SmallVector<mlir::Value> & here = slots[target];
                for (unsigned j = 0; j < passed.size(); ++j) {
                    ec::AssignOp::create(build, end->getLoc(), here[j], passed[j]);
                }
                mlir::cf::BranchOp::create(build, end->getLoc(), target);

                passed.erase(0, passed.size());
                end->setSuccessor(edge, index);
            }
        }

        // ---- and the arguments are gone -----------------------------------
        for (mlir::Block * block : carrying) {
            block->eraseArguments(0, block->getNumArguments());
        }
        return mlir::success();
    }
};

} // namespace

} // namespace ctcompile::ctjs
