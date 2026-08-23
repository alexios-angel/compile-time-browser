#include "ctcompile/CTJS/Transforms/Passes.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTJS/Lowering/RuntimeHelpers.hpp"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

namespace ctcompile::ctjs {

#define GEN_PASS_DEF_CTJSLOWERTORUNTIME
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

namespace {

// THE ONE PATTERN. Every operation implementing RuntimeCallOpInterface lowers
// through this and none of them needs a pattern of its own.
//
// IT MATCHES ON THE INTERFACE, WHICH IS THE ACCEPTANCE CRITERION FOR THE PHASE
// rather than a nicety: "a pass that switches on operation names must be
// revisited every time an operation is added. A pass that queries a trait never
// is." Adding an operation to CTJSOps.td adds nothing to this file.
struct RuntimeCallLowering : mlir::OpInterfaceRewritePattern<RuntimeCallOpInterface> {
    using OpInterfaceRewritePattern::OpInterfaceRewritePattern;

    mlir::LogicalResult matchAndRewrite(RuntimeCallOpInterface op,
                                        mlir::PatternRewriter & rewriter) const override {
        const runtime_helper & helper = helper_for(op.getHelperID());

        // THE SHAPE IS READ FROM THE ABI, NOT INFERRED FROM A COUNT. See the
        // note on `values_only` in RuntimeHelpers.hpp: matching on arity alone
        // lowered ctjs.call - a VARIADIC operation - against ct_aot_call's
        // eight parameters whenever a call site happened to have five
        // arguments, and passed a value where the helper wants an argv pointer.
        if (!helper.values_only) {
            return rewriter.notifyMatchFailure(
                op, "the helper takes arguments that are not JavaScript values - a kind, an "
                    "out-parameter, a site or caller-allocated storage - and materialising each "
                    "is its own decision");
        }

        // THE FRAME HANDLE FIRST, then the operation's operands in ODS order.
        // That order is the ABI's, and the table is where it is written down.
        //
        // WHICH FRAME. Every helper that takes one takes THIS function's, and
        // ctjs.frame_enter is emitted once in the entry block - so there is
        // exactly one to find, and finding it by walking rather than by
        // threading it through every operation is what keeps push_handler's
        // operands free of a !ctjs.context they could not carry.
        mlir::Value frame;
        if (auto function = op->getParentOfType<FuncOp>()) {
            // BY RESULT INDEX, NOT BY NAME. The result is called `context`, and
            // ODS will not generate a `getContext()` accessor for it because
            // Operation already has one - so the generated name is decorated
            // and depending on which decoration is a hostage to the version.
            function.walk([&](FrameEnterOp enter) {
                if (!frame) { frame = enter->getResult(0); }
            });
        }

        llvm::SmallVector<mlir::Value> arguments;
        const bool takes_frame = helper.arity > op->getNumOperands();
        if (takes_frame) {
            if (!frame) {
                return rewriter.notifyMatchFailure(
                    op, "the helper takes a frame handle and the function has no ctjs.frame_enter");
            }
            arguments.push_back(frame);
        }
        llvm::append_range(arguments, op->getOperands());

        // THE ARITY CHECK, AND WHAT IT FOUND.
        //
        // It closes the ABI-drift hole from the side the name check cannot see:
        // a helper that does not exist is already a compile error, and an
        // operation whose operands have drifted from its helper's parameters
        // reads fine in both files and produces a call with a garbage argument.
        //
        // ON ITS FIRST RUN IT REFUSED MOST OF THE TABLE, and it was right to.
        // The plan's sketch assumes "context, then the operation's operands in
        // ODS order" - and that is not what these helpers look like:
        //
        //   ct_aot_binary_op(fr, op_kind, lhs, rhs, out)   ODS has 2 operands
        //   ct_aot_enter(ctx, site, reg_count, receiver, storage)   ODS has 0
        //
        // `op_kind` is an ATTRIBUTE on the CTJS operation, `out` is an
        // out-parameter carrying the result, `site` is the baked diagnostic and
        // `storage` is caller-allocated frame space. None of them is an
        // operand, and materialising each is its own small decision - an
        // attribute becomes a constant, an out-parameter becomes an alloca the
        // call writes through and the caller loads.
        //
        // SO A MISMATCH IS "NOT YET", NOT "WRONG". Failing the match leaves the
        // operation for a later pattern rather than failing the module, which
        // is what lets this pass be useful while that work is done - and the
        // count of what it leaves behind is the work list, exactly as the
        // importer's refusals were.
        if (arguments.size() != helper.arity) {
            return rewriter.notifyMatchFailure(op, "helper takes arguments this pattern cannot "
                                                   "materialise yet - see the note above");
        }

        // A DECLARATION PER HELPER, ONCE. The helpers are extern "C" symbols the
        // runtime already exports; the module needs a func.func private for each
        // one it calls so the call verifies.
        auto module = op->getParentOfType<mlir::ModuleOp>();
        auto callee = module.lookupSymbol<mlir::func::FuncOp>(helper.symbol);
        if (callee) {
            // AND THE TYPES MUST AGREE WITH THE DECLARATION ALREADY THERE.
            //
            // ARITY ALONE IS NOT ENOUGH, which running this over p5.js proved
            // within a minute: `ctjs.call` is VARIADIC, so its operand count
            // varies per call site - and one site with seven arguments matched
            // ct_aot_call's eight parameters by ACCIDENT, producing a call that
            // passed a !ctjs.value where the declaration wanted a
            // !ctjs.context. It verified as far as this pattern was concerned
            // and failed in the module verifier, which is the good outcome; the
            // bad one is a helper where the types happen to line up too.
            //
            // A count is a weak check on a variadic operation. The types are
            // the rest of it.
            const auto declared = callee.getFunctionType().getInputs();
            if (declared.size() != arguments.size()) {
                return rewriter.notifyMatchFailure(op, "argument count disagrees with the "
                                                       "declaration already emitted");
            }
            for (std::size_t i = 0; i < declared.size(); ++i) {
                if (declared[i] != arguments[i].getType()) {
                    return rewriter.notifyMatchFailure(
                        op, "an argument type disagrees with the declaration already emitted");
                }
            }
        }
        if (!callee) {
            mlir::OpBuilder::InsertionGuard guard(rewriter);
            rewriter.setInsertionPointToStart(module.getBody());
            llvm::SmallVector<mlir::Type> inputs;
            for (const mlir::Value argument : arguments) { inputs.push_back(argument.getType()); }
            llvm::SmallVector<mlir::Type> results{op->getResultTypes()};
            callee = mlir::func::FuncOp::create(rewriter, op.getLoc(), helper.symbol,
                                                rewriter.getFunctionType(inputs, results));
            callee.setPrivate();
        }

        auto call = mlir::func::CallOp::create(rewriter, op.getLoc(), callee, arguments);
        rewriter.replaceOp(op, call.getResults());
        return mlir::success();
    }
};

struct CTJSLowerToRuntimePass : impl::CTJSLowerToRuntimeBase<CTJSLowerToRuntimePass> {
    using CTJSLowerToRuntimeBase::CTJSLowerToRuntimeBase;

    void runOnOperation() override {
        mlir::RewritePatternSet patterns(&getContext());
        patterns.add<RuntimeCallLowering>(&getContext());
        if (mlir::failed(mlir::applyPatternsGreedily(getOperation(), std::move(patterns)))) {
            signalPassFailure();
        }
    }
};

} // namespace

} // namespace ctcompile::ctjs
