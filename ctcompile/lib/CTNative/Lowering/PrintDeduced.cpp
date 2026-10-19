//===- PrintDeduced.cpp - Mark declarations printable as auto -------------===//
//
// Decides which EmitC declarations the C++ printer may spell as `auto`. The
// IR keeps the proved type on every value; this pass only adds a
// `ctnative.deduced` attribute, which the project's patched TranslateToCpp
// reads to print `auto` instead of the type. It runs last, after every
// verifier and analysis, on an EmitC module that already compiles and runs
// with every type spelled out, so deduction is only ever added to a working
// program.
//
// For each deduced declaration the printer also emits a CTCOMPILE_PIN
// static_assert (the macro this pass defines), so if ctcompile's proved type
// and the C++ compiler's deduced type ever disagree, the build fails with an
// error naming the JavaScript source location.
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

/// Whether a local declaration of `o`'s result may print as `auto`: its
/// initialiser must fix the type. Everything not listed is never deduced:
/// constants, loads, variables (no initialiser), opaque calls (the
/// standard-library boundary), and ops that are never declared at all.
bool isDeducible(mlir::Operation * o) {
    // C++ promotes bool and narrow integer operands of arithmetic and bitwise
    // operators to int. The explicit result type narrows back, which `auto`
    // would lose; SCF canonicalization can introduce a boolean xor like this.
    const bool arithmetic =
        llvm::isa<ec::AddOp, ec::SubOp, ec::MulOp, ec::DivOp, ec::RemOp, ec::UnaryMinusOp,
                  ec::UnaryPlusOp, ec::BitwiseAndOp, ec::BitwiseOrOp, ec::BitwiseXorOp,
                  ec::BitwiseNotOp, ec::BitwiseLeftShiftOp, ec::BitwiseRightShiftOp>(o);
    auto integer = llvm::dyn_cast<mlir::IntegerType>(o->getResult(0).getType());
    if (integer && integer.getWidth() < 32 && arithmetic) { return false; }
    return arithmetic || llvm::isa<ec::CmpOp, ec::CastOp, ec::CallOp, ec::LogicalAndOp,
                                    ec::LogicalOrOp, ec::LogicalNotOp, ec::ConditionalOp>(o);
}

/// The CTCOMPILE_PIN macro. The type is the last macro argument so a type
/// spelled with a comma still pins; `site` is a string literal the printer
/// fills from the op's location. -DCTCOMPILE_NO_TYPE_PINS disables the pins.
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
        unsigned numbers = 0;
        bool mutated = false;
        module.walk([&](ec::FuncOp fn) {
            fn.getBody().walk([&](mlir::Operation * o) {
                if (o->getNumResults() != 1 || !isDeducible(o)) { return; }
                // A result nothing reads is printed as a bare statement, not a
                // declaration (see emitAssignPrefix in the fork's emitter), so
                // there is nothing to deduce and nothing to pin. Marking it
                // would emit a CTCOMPILE_PIN naming a variable that does not
                // exist.
                if (o->getResult(0).use_empty()) { return; }
                // An op inside an emitc.expression is printed inline and never
                // declared.
                if (o->getParentOfType<ec::ExpressionOp>()) { return; }
                o->setAttr("ctnative.deduced", mlir::UnitAttr::get(ctx));
                ++deduced;
                const auto type = o->getResult(0).getType();
                // The `mutate=N` option pins the Nth deduced number as int32_t,
                // so the generated file must fail to build naming its site; a
                // test uses it to prove the pins are checked.
                const auto opaque = llvm::dyn_cast<ec::OpaqueType>(type);
                if ((type.isF64() || (opaque && opaque.getValue() == "ctnative::js_num")) &&
                    ++numbers == mutate) {
                    o->setAttr("ctnative.pinned",
                               mlir::TypeAttr::get(mlir::IntegerType::get(ctx, 32)));
                    mutated = true;
                }
            });
        });
        // A mutation that lands nowhere proves nothing, so fail instead of
        // passing silently.
        if (mutate != 0 && !mutated) {
            module.emitError("--ctnative-print-deduced mutate=")
                << static_cast<unsigned>(mutate)
                << " names no deduced number declaration; the module has " << numbers;
            return signalPassFailure();
        }
        module->setAttr("ctnative.deduced_count",
                        mlir::IntegerAttr::get(mlir::IntegerType::get(ctx, 64), deduced));
        if (deduced == 0) { return; }
        // Insert <type_traits> and the pin macro after the last leading
        // include.
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
