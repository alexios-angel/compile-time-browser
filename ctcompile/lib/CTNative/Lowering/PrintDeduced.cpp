//===- PrintDeduced.cpp - Stage 53E's printing policy and 53F's pins ------===//
//
// PART 24 STAGE 53E: `auto` is an ATTRIBUTE, not a type. This pass runs LAST,
// after every verifier and every analysis, over an EmitC module that already
// compiles and runs with every type printed (62½-E's rule: deduction is added
// to a working artefact, never built into the first one). It marks the
// declarations the fork's TranslateToCpp may print as `auto` and defines the
// macro Stage 53F's pins expand to. The IR keeps the proved type on every
// value; only the spelling changes, and the pin turns any disagreement
// between ctcompile's deduction and the C++ compiler's into a build error
// that names the JavaScript site.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Transforms/Passes.h"

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"

namespace ctcompile::ctnative {

#define GEN_PASS_DEF_CTNATIVEPRINTDEDUCED
#include "ctcompile/CTNative/Transforms/Passes.h.inc"

namespace {

namespace ec = mlir::emitc;

// 53E site 1, local declarations: an initialiser that fixes the type. The
// never-deduce list is everything absent here - constants, loads, variables
// (no initialiser), opaque calls (the standard-library boundary), and the
// deferred ops that are never declared at all.
bool isDeducible(mlir::Operation * o) {
    return llvm::isa<ec::AddOp, ec::SubOp, ec::MulOp, ec::DivOp, ec::RemOp, ec::CmpOp, ec::CastOp,
                     ec::CallOp, ec::LogicalAndOp, ec::LogicalOrOp, ec::LogicalNotOp,
                     ec::UnaryMinusOp, ec::UnaryPlusOp, ec::ConditionalOp, ec::BitwiseAndOp,
                     ec::BitwiseOrOp, ec::BitwiseXorOp, ec::BitwiseNotOp, ec::BitwiseLeftShiftOp,
                     ec::BitwiseRightShiftOp>(o);
}

// The type is the LAST macro argument so a type spelled with a comma still
// pins; `site` is a string literal the printer fills from the op's location.
constexpr llvm::StringLiteral kPinMacro =
    "#ifndef CTCOMPILE_NO_TYPE_PINS\n"
    "#define CTCOMPILE_PIN(name, site, ...) static_assert(std::is_same_v<decltype(name), "
    "__VA_ARGS__>, \"ctcompile: \" #name \" @ \" site)\n"
    "#else\n"
    "#define CTCOMPILE_PIN(name, site, ...)\n"
    "#endif";

struct CTNativePrintDeducedPass : impl::CTNativePrintDeducedBase<CTNativePrintDeducedPass> {
    using Base::Base;

    void runOnOperation() override {
        mlir::ModuleOp module = getOperation();
        mlir::MLIRContext * ctx = &getContext();
        unsigned deduced = 0;
        unsigned doubles = 0;
        bool mutated = false;
        module.walk([&](ec::FuncOp fn) {
            fn.getBody().walk([&](mlir::Operation * o) {
                if (o->getNumResults() != 1 || !isDeducible(o)) { return; }
                if (o->getParentOfType<ec::ExpressionOp>()) { return; } // inline, never declared
                o->setAttr("ctnative.deduced", mlir::UnitAttr::get(ctx));
                ++deduced;
                if (llvm::isa<mlir::Float64Type>(o->getResult(0).getType()) &&
                    ++doubles == mutate) {
                    o->setAttr("ctnative.pinned",
                               mlir::TypeAttr::get(mlir::IntegerType::get(ctx, 32)));
                    mutated = true;
                }
            });
        });
        // A mutation that lands nowhere proves nothing; say so instead of
        // passing (the counter is asserted, never the output trusted).
        if (mutate != 0 && !mutated) {
            module.emitError("--ctnative-print-deduced mutate=")
                << static_cast<unsigned>(mutate)
                << " names no deduced double declaration; the module has " << doubles;
            return signalPassFailure();
        }
        module->setAttr("ctnative.deduced_count",
                        mlir::IntegerAttr::get(mlir::IntegerType::get(ctx, 64), deduced));
        if (deduced == 0) { return; }
        // The macro after the includes, and <type_traits> among them.
        mlir::OpBuilder b(ctx);
        mlir::Operation * lastInclude = nullptr;
        for (mlir::Operation & o : module.getBody()->getOperations()) {
            if (!llvm::isa<ec::IncludeOp>(o)) { break; }
            lastInclude = &o;
        }
        if (lastInclude) {
            b.setInsertionPointAfter(lastInclude);
        } else {
            b.setInsertionPointToStart(module.getBody());
        }
        ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("type_traits"),
                              /*is_standard_include=*/b.getUnitAttr());
        ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(kPinMacro));
    }
};

} // namespace

} // namespace ctcompile::ctnative
