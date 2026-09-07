#include "../lib/CTNative/Lowering/Exceptions/Recovery.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/Import/BytecodeImport.hpp"
#include "ctcompile/CTJS/Transforms/Passes.h"

#include "ctbrowser/script/compile.hpp"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <bit>
#include <cstdio>
#include <optional>
#include <string>

namespace {
namespace ctjs = ctcompile::ctjs;
using ctcompile::ctnative::lowering_detail::ExceptionRecoveryMode;
using ctcompile::ctnative::lowering_detail::recoverPrimitiveExceptionRegion;

int failures = 0;

bool check(bool condition, llvm::StringRef label) {
    if (!condition) {
        llvm::errs() << "FAIL " << label << '\n';
        ++failures;
    }
    return condition;
}

std::string printed(mlir::Operation * operation) {
    std::string text;
    llvm::raw_string_ostream stream(text);
    operation->print(stream);
    return text;
}

ctjs::FuncOp guarded(mlir::ModuleOp module) {
    for (auto function : module.getOps<ctjs::FuncOp>()) {
        if (function.getSymName().starts_with("guarded$")) { return function; }
    }
    return {};
}

ctjs::CallDirectOp firstCall(ctjs::FuncOp function) {
    ctjs::CallDirectOp found;
    function.walk([&](ctjs::CallDirectOp call) {
        if (!found) { found = call; }
    });
    return found;
}

unsigned countChecks(ctjs::FuncOp function) {
    unsigned count = 0;
    function.walk([&](ctjs::CheckOp) { ++count; });
    return count;
}

mlir::OwningOpRef<mlir::ModuleOp> import(mlir::MLIRContext & context, llvm::StringRef source,
                                         bool resolve = true) {
    auto program = ctbrowser::script::compiler::compile(source.str());
    if (!check(program.ok, "source compiles")) { return {}; }
    auto imported = ctcompile::js::import_program(program, "invocation-recovery", &context);
    if (!check(static_cast<bool>(imported.module), "source imports")) { return {}; }
    if (resolve) {
        mlir::PassManager passes(&context);
        passes.addPass(ctjs::createCTJSResolveGlobals());
        passes.addPass(ctjs::createCTJSLiftToSCF());
        if (!check(mlir::succeeded(passes.run(*imported.module)), "source prepares")) { return {}; }
    }
    return std::move(imported.module);
}

// This checks the selected completion's SSA wiring. It supplies fixed helper
// outcomes, independently of helper implementation/type inference: every normal
// call returns 20; one selected call throws the fixture's known payload. Poison
// is never a number and cannot satisfy a selected completion observation.
struct completionTrace {
    ctjs::InvokeOp failing;
    double payload;
    unsigned remaining = 4096;

    std::optional<double> value(mlir::Value value) {
        if (remaining == 0) { return {}; }
        --remaining;
        if (auto literal = value.getDefiningOp<ctjs::ConstantOp>()) {
            auto attr = literal->getAttr("value");
            if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(attr)) {
                return std::bit_cast<double>(number.getBits());
            }
            if (auto boolean = llvm::dyn_cast<ctjs::BooleanAttr>(attr)) {
                return boolean.getValue() ? 1 : 0;
            }
        }
        if (auto literal = value.getDefiningOp<mlir::arith::ConstantOp>()) {
            if (auto integer = llvm::dyn_cast<mlir::IntegerAttr>(literal.getValue())) {
                if (integer.getType().isInteger(1)) { return integer.getValue().isOne() ? 1 : 0; }
                return static_cast<double>(integer.getInt());
            }
        }
        if (auto truthy = value.getDefiningOp<ctjs::TruthyOp>()) {
            return this->value(truthy->getOperand(0));
        }
        if (auto comparison = value.getDefiningOp<mlir::arith::CmpIOp>()) {
            auto left = this->value(comparison.getLhs());
            auto right = this->value(comparison.getRhs());
            if (left && right && comparison.getPredicate() == mlir::arith::CmpIPredicate::eq) {
                return *left == *right ? 1 : 0;
            }
            return {};
        }
        if (llvm::isa_and_nonnull<ctjs::FromBoolOp, mlir::arith::IndexCastUIOp,
                                  mlir::arith::ExtUIOp, mlir::arith::TruncIOp>(
                value.getDefiningOp())) {
            return this->value(value.getDefiningOp()->getOperand(0));
        }
        if (auto invocation = value.getDefiningOp<ctjs::InvokeOp>()) {
            const unsigned index = llvm::cast<mlir::OpResult>(value).getResultNumber();
            const bool failed = invocation == failing;
            if (index == 0) { return failed ? 1 : 0; }
            if (index == 1) { return failed ? std::optional<double>{} : 20; }
            if (!failed) { return {}; }
            if (index == 2) { return payload; }
            auto exit =
                llvm::cast<ctjs::InvokeExitOp>(invocation.getBody().front().getTerminator());
            return this->value(exit.getState()[index - 3]);
        }
        if (auto branch = value.getDefiningOp<mlir::scf::IfOp>()) {
            auto condition = this->value(branch.getCondition());
            if (!condition) { return {}; }
            auto & region = *condition != 0 ? branch.getThenRegion() : branch.getElseRegion();
            return this->value(region.front().getTerminator()->getOperand(
                llvm::cast<mlir::OpResult>(value).getResultNumber()));
        }
        return {};
    }
};

void checkCompletion(ctjs::TryOp attempt, ctjs::InvokeOp failing, double payload,
                     double expectedState) {
    auto exit = llvm::cast<ctjs::TryExitOp>(attempt.getBody().front().getTerminator());
    completionTrace trace{failing, payload};
    check(trace.value(exit.getIsThrow()) == (failing ? 1 : 0),
          "enclosing try selects call completion");
    if (!failing) {
        check(trace.value(exit.getNormalResult()) == 20, "normal return publishes the call result");
        return;
    }
    auto & handler = attempt.getCatchBody().front();
    ctjs::BinaryOp add;
    for (auto binary : handler.getOps<ctjs::BinaryOp>()) { add = binary; }
    if (!check(static_cast<bool>(add), "fixture catch keeps its state-plus-payload addition")) {
        return;
    }
    auto saved = llvm::dyn_cast<mlir::BlockArgument>(add->getOperand(0));
    auto thrown = llvm::dyn_cast<mlir::BlockArgument>(add->getOperand(1));
    if (!check(saved && thrown && saved.getOwner() == &handler && thrown.getArgNumber() == 0,
               "catch reads its own payload and saved state")) {
        return;
    }
    check(trace.value(exit.getCaughtValues()[saved.getArgNumber()]) == expectedState,
          "catch receives state immediately before the failed call");
    check(trace.value(exit.getCaughtValues()[0]) == payload,
          "catch receives the invocation's thrown payload");
}

void testSource(mlir::MLIRContext & context, llvm::StringRef name, llvm::StringRef source,
                unsigned expectedCalls, double saved, double payload) {
    auto module = import(context, source);
    if (!module) { return; }
    auto function = guarded(*module);
    if (!check(static_cast<bool>(function), "guarded source function survives")) { return; }
    const auto original = printed(function);
    mlir::OwningOpRef<ctjs::FuncOp> detached(llvm::cast<ctjs::FuncOp>(function->clone()));
    const auto snapshot = printed(*detached);
    const auto checks = countChecks(function);
    auto ordinary = recoverPrimitiveExceptionRegion(function);
    check(!ordinary.recovered &&
              ordinary.refusal ==
                  "native try/catch needs an explicit throw in its active handler" &&
              printed(function) == original,
          "ordinary native recovery keeps its exact throwing-call refusal and source graph");

    auto recovered = recoverPrimitiveExceptionRegion(function, 100000,
                                                     ExceptionRecoveryMode::CheckedInvocations);
    if (!check(recovered.recovered, ("checked invocation recovery succeeds for " + name).str())) {
        llvm::errs() << recovered.refusal << '\n';
        return;
    }
    check(mlir::succeeded(mlir::verify(*module)), "recovered completion IR verifies");
    check(recovered.original && printed(*recovered.original) == snapshot &&
              countChecks(*recovered.original) == checks && checks > expectedCalls,
          "rollback retains every original call and non-call status edge");
    llvm::SmallVector<ctjs::InvokeOp> invocations;
    ctjs::TryOp attempt;
    function.walk<mlir::WalkOrder::PreOrder>(
        [&](ctjs::InvokeOp invocation) { invocations.push_back(invocation); });
    function.walk([&](ctjs::TryOp found) { attempt = found; });
    if (!check(invocations.size() == expectedCalls && attempt,
               "one invocation per source call reaches the enclosing try")) {
        return;
    }
    for (auto invocation : invocations) {
        auto call = llvm::cast<ctjs::CallDirectOp>(invocation.getBody().front().front());
        auto dispatch = llvm::cast<ctjs::InvokeExitOp>(invocation.getBody().front().back());
        check(call.getResult().hasOneUse() && dispatch.getNormalResult() == call.getResult(),
              "call result has only its normal dispatch use");
        check(llvm::all_of(dispatch.getState(),
                           [&](mlir::Value state) {
                               return !invocation.getBody().isAncestor(state.getParentRegion());
                           }),
              "every saved register is defined before the call body");
        auto normal = llvm::cast<ctjs::InvokeYieldOp>(invocation.getNormalBody().front().back());
        auto unwind = llvm::cast<ctjs::InvokeYieldOp>(invocation.getUnwindBody().front().back());
        check(
            normal.getValues()[1] == invocation.getNormalBody().front().getArgument(0) &&
                llvm::isa_and_nonnull<mlir::ub::PoisonOp>(unwind.getValues()[1].getDefiningOp()) &&
                llvm::equal(unwind.getValues().drop_front(2),
                            invocation.getUnwindBody().front().getArguments()),
            "normal publication and unwind payload/state stay separate");
    }
    checkCompletion(attempt, invocations.back(), payload, saved);
    checkCompletion(attempt, {}, payload, 0);
    if (expectedCalls == 2) { checkCompletion(attempt, invocations.front(), payload, 0); }
    // The first argument-free handler cannot be recovered a second time. A
    // failed rerun must not consume its existing continuations.
    const auto structured = printed(function);
    auto rerun = recoverPrimitiveExceptionRegion(function, 100000,
                                                 ExceptionRecoveryMode::CheckedInvocations);
    check(!rerun.recovered && printed(function) == structured,
          "recovery rerun preserves the existing invocation graph");

    // Restore exactly as the native transaction does after admission refuses.
    function.getBody().takeBody(recovered.original->getBody());
    function->setAttrs((*recovered.original)->getAttrs());
    check(printed(function) == original, "source rollback is byte-identical");
    for (unsigned budget : {0u, 1u, 64u, 256u}) {
        auto limited = recoverPrimitiveExceptionRegion(function, budget,
                                                       ExceptionRecoveryMode::CheckedInvocations);
        check(!limited.recovered && limited.refusal.find("budget exhausted") != std::string::npos &&
                  printed(function) == original,
              "bounded recovery leaves all original status edges on failure");
    }
    llvm::outs() << name << ": " << expectedCalls << " checked invocation(s), " << checks
                 << " original checks retained for admission rollback\n";
}

void testMutations(mlir::MLIRContext & context, llvm::StringRef source) {
    for (unsigned mutation = 0; mutation != 6; ++mutation) {
        auto module = import(context, source);
        if (!module) { return; }
        auto function = guarded(*module);
        auto call = firstCall(function);
        auto status = llvm::cast<ctjs::CheckOp>(call->getBlock()->getTerminator());
        const unsigned width = static_cast<unsigned>(status.getContOperands().size());
        mlir::OpBuilder at(status);
        if (mutation == 0) {
            status->setOperand(width, call.getResult());
        } else if (mutation == 1) {
            status->setOperand(width, function.getBody().front().getArgument(3));
        } else if (mutation == 2) {
            status->setOperand(0, call.getResult());
        } else if (mutation == 3) {
            ctjs::ConstantOp::create(at, call.getLoc(), ctjs::ValueType::get(&context),
                                     ctjs::BooleanAttr::get(&context, true));
        } else if (mutation == 4) {
            mlir::OperationState store(call.getLoc(), "ctjs.store_global");
            store.addAttribute("name", at.getStringAttr("published"));
            store.addOperands(call.getResult());
            at.create(store);
        } else {
            at.setInsertionPoint(call);
            ctjs::LoadGlobalOp::create(at, call.getLoc(), "falliblePrefix");
        }
        const auto before = printed(function);
        auto result = recoverPrimitiveExceptionRegion(function, 100000,
                                                      ExceptionRecoveryMode::CheckedInvocations);
        check(!result.recovered && result.refusal.find("invocation") != std::string::npos &&
                  printed(function) == before,
              "late call/state/publication mutation refuses without changing the source");
    }
    auto unresolved = import(context, source, false);
    if (!unresolved) { return; }
    auto function = guarded(*unresolved);
    const auto before = printed(function);
    auto result = recoverPrimitiveExceptionRegion(function, 100000,
                                                  ExceptionRecoveryMode::CheckedInvocations);
    check(!result.recovered && printed(function) == before,
          "an unresolved source call supplies no invocation recovery permission");
}

} // namespace

int main(int argc, char ** argv) {
    if (argc != 2) {
        std::fputs("usage: ctcompile-test-exception-recovery invocation-state.mlir\n", stderr);
        return 1;
    }
    auto file = llvm::MemoryBuffer::getFile(argv[1]);
    if (!file) { return 1; }
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    context.getOrLoadDialect<mlir::arith::ArithDialect>();
    context.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    context.getOrLoadDialect<mlir::ub::UBDialect>();
    const auto source = [&](llvm::StringRef name) {
        auto tail = (*file)->getBuffer().split(("//--- " + name + ".js\n").str()).second;
        return tail.split("//--- ").first;
    };
    testSource(context, "assignment", source("assignment"), 1, 10, 32);
    testSource(context, "sequential", source("sequential"), 2, 20, 32);
    testSource(context, "argument", source("argument"), 1, 14, 14);
    testMutations(context, source("assignment"));
    return failures == 0 ? 0 : 1;
}
