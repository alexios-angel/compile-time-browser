#include "../lib/CTNative/Lowering/Exceptions/Recovery.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/Import/BytecodeImport.hpp"
#include "ctcompile/CTJS/Transforms/Passes.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include "ctbrowser/script/compile.hpp"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <tuple>

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
                unsigned expectedCalls, double saved, double payload, ExceptionRecoveryMode mode) {
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

    auto recovered = recoverPrimitiveExceptionRegion(function, 100000, mode);
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
    auto rerun = recoverPrimitiveExceptionRegion(function, 100000, mode);
    check(!rerun.recovered && printed(function) == structured,
          "recovery rerun preserves the existing invocation graph");

    // Restore exactly as the native transaction does after admission refuses.
    function.getBody().takeBody(recovered.original->getBody());
    function->setAttrs((*recovered.original)->getAttrs());
    check(printed(function) == original, "source rollback is byte-identical");
    for (unsigned budget : {0u, 1u, 64u, 256u}) {
        auto limited = recoverPrimitiveExceptionRegion(function, budget, mode);
        check(!limited.recovered && limited.refusal.find("budget exhausted") != std::string::npos &&
                  printed(function) == original,
              "bounded recovery leaves all original status edges on failure");
    }
    llvm::outs() << name << ": " << expectedCalls << " checked invocation(s), " << checks
                 << " original checks retained for admission rollback, " << recovered.steps
                 << " steps, effects=" << (mode == ExceptionRecoveryMode::EffectCheckedInvocations)
                 << '\n';
}

void checkCompletionTypes(mlir::ModuleOp module, ctjs::FuncOp function, llvm::StringRef normalType,
                          llvm::StringRef stateType,
                          llvm::StringRef payloadType = "!ctnative.boxed") {
    mlir::DataFlowSolver solver;
    solver.load<mlir::dataflow::DeadCodeAnalysis>();
    solver.load<mlir::dataflow::SparseConstantPropagation>();
    solver.load<ctcompile::ctnative::TypeInference>();
    const auto before = printed(module);
    if (!check(mlir::succeeded(solver.initializeAndRun(module)),
               "recovered source completion inference converges")) {
        return;
    }
    const auto expect = [&](mlir::Value value, llvm::StringRef wanted, llvm::StringRef label) {
        const auto * lattice = solver.lookupState<ctcompile::ctnative::TypeLattice>(value);
        std::string type;
        if (lattice) {
            llvm::raw_string_ostream into(type);
            lattice->getValue().print(into);
        }
        if (!check(type == wanted, label)) {
            llvm::errs() << "  wanted " << wanted << ", found " << type << '\n';
        }
    };
    unsigned invokes = 0, states = 0;
    function.walk([&](ctjs::InvokeOp invocation) {
        ++invokes;
        auto call = llvm::cast<ctjs::CallDirectOp>(invocation.getBody().front().front());
        expect(call.getResult(), normalType, "successful source call has its own normal type");
        expect(invocation.getNormalBody().front().getArgument(0), normalType,
               "normal continuation receives only the successful returned value");
        expect(invocation.getUnwindBody().front().getArgument(0), payloadType,
               "unwind inference preserves the imported frame's failure alternative");
    });
    function.walk([&](ctjs::TryOp attempt) {
        auto & caught = attempt.getCatchBody().front();
        expect(caught.getArgument(0), payloadType,
               "enclosing catch cannot narrow an unproved frame failure");
        for (auto saved : caught.getArguments().drop_front()) {
            ++states;
            expect(saved, stateType, "pre-call catch state is independent of the payload type");
        }
    });
    check(invokes != 0 && states != 0, "source type witness includes invocation and saved state");
    check(printed(module) == before, "completion inference does not change source operations");
}

void testSourceCompletionTypes(mlir::MLIRContext & context, llvm::StringRef name,
                               llvm::StringRef source, llvm::StringRef normalType,
                               llvm::StringRef stateType) {
    auto module = import(context, source);
    if (!module) { return; }
    auto function = guarded(*module);
    const auto original = printed(*module);
    auto recovered = recoverPrimitiveExceptionRegion(
        function, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
    if (!check(recovered.recovered, "source type witness passes independent effect recovery")) {
        llvm::errs() << recovered.refusal << '\n';
        return;
    }
    checkCompletionTypes(*module, function, normalType, stateType);
    checkCompletionTypes(*module, function, normalType, stateType);
    function.getBody().takeBody(recovered.original->getBody());
    function->setAttrs((*recovered.original)->getAttrs());
    check(printed(*module) == original, "type inference retains exact source rollback");
    auto ordinary = recoverPrimitiveExceptionRegion(function);
    check(!ordinary.recovered && printed(*module) == original,
          "normal type precision does not admit a native throwing source call");
    llvm::outs() << name << ": source normal/state types " << normalType << "/" << stateType
                 << ", unwind remains boxed across frame_enter\n";
}

void testSourceCompletionMutations(mlir::MLIRContext & context, llvm::StringRef source) {
    for (unsigned mutation = 0; mutation != 10; ++mutation) {
        auto module = import(context, source);
        if (!module) { return; }
        auto function = guarded(*module);
        auto recovered = recoverPrimitiveExceptionRegion(
            function, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
        if (!check(recovered.recovered, "live type control starts with checked source recovery")) {
            return;
        }
        checkCompletionTypes(*module, function, "!ctnative.num<i32>", "!ctnative.num<i32>");
        auto call = firstCall(function);
        auto helper =
            mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, call.getCalleeAttr());
        ctjs::ReturnOp returned;
        ctjs::ThrowOp thrown;
        helper.walk([&](ctjs::ReturnOp operation) { returned = operation; });
        helper.walk([&](ctjs::ThrowOp operation) { thrown = operation; });
        if (!check(returned && thrown, "source type control retains both helper completions")) {
            return;
        }
        mlir::OpBuilder at(returned);
        const auto where = returned.getLoc();
        const auto type = ctjs::ValueType::get(&context);
        helper->setAttr("ctnative.nothrow", at.getUnitAttr());
        helper->setAttr("ctnative.exception_effects", at.getUnitAttr());
        call->setAttr("ctnative.type", at.getStringAttr("num<i32>"));
        llvm::StringRef expected = "!ctnative.boxed";
        llvm::StringRef payload = "!ctnative.boxed";
        llvm::SmallVector<mlir::Operation *> added;
        if (mutation < 3) {
            mlir::Attribute literal =
                mutation == 0   ? mlir::Attribute(ctjs::StringAttr::get(&context, "live"))
                : mutation == 1 ? mlir::Attribute(ctjs::BooleanAttr::get(&context, true))
                                : mlir::Attribute(ctjs::NumberAttr::get(
                                      &context, std::bit_cast<uint64_t>(-0.0)));
            auto replacement = ctjs::ConstantOp::create(at, where, type, literal);
            returned->setOperand(0, replacement);
            expected = mutation == 0   ? "!ctnative.str<utf8>"
                       : mutation == 1 ? "!ctnative.bool"
                                       : "!ctnative.num<f64>";
        } else if (mutation == 3) {
            at.setInsertionPoint(thrown);
            auto replacement = ctjs::ConstantOp::create(at, where, type,
                                                        ctjs::StringAttr::get(&context, "failure"));
            thrown->setOperand(0, replacement);
            expected = "!ctnative.num<i32>";
        } else if (mutation == 4) {
            added.push_back(ctjs::StoreGlobalOp::create(at, where, "late", returned.getValue()));
        } else if (mutation == 5) {
            auto key = ctjs::ConstantOp::create(at, where, type,
                                                ctjs::StringAttr::get(&context, "property"));
            ctjs::GetPropertyOp::create(at, where, type, helper.getBody().front().getArgument(0),
                                        key);
        } else if (mutation == 6) {
            auto arguments = helper.getBody().front().getArguments();
            ctjs::CallDirectOp::create(at, where, type, call.getCalleeAttr(), arguments[0],
                                       arguments[1], arguments[2], arguments.drop_front(3));
        } else if (mutation == 7) {
            mlir::SymbolTable::setSymbolVisibility(helper, mlir::SymbolTable::Visibility::Public);
        } else if (mutation == 8) {
            unsigned operations = 0;
            helper.getBody().walk([&](mlir::Operation *) { ++operations; });
            if (!check(operations < 4096, "source completion leaves room for exact work limit")) {
                return;
            }
            for (unsigned index = operations; index != 4096; ++index) {
                added.push_back(ctjs::ConstantOp::create(at, where, type,
                                                         ctjs::BooleanAttr::get(&context, true)));
            }
            checkCompletionTypes(*module, function, "!ctnative.num<i32>", "!ctnative.num<i32>");
            added.push_back(
                ctjs::ConstantOp::create(at, where, type, ctjs::BooleanAttr::get(&context, true)));
        } else {
            // A test-only frame-free counterpart establishes that the boxed
            // payload above is a real frame-failure obligation. This erasure
            // is not a source transform or evidence that native may do it.
            llvm::SmallVector<mlir::Operation *> bookkeeping;
            helper.walk([&](mlir::Operation * operation) {
                if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(operation)) {
                    bookkeeping.push_back(operation);
                }
            });
            for (auto * operation : llvm::reverse(bookkeeping)) { operation->erase(); }
            expected = "!ctnative.num<i32>";
            payload = "!ctnative.num<i32>";
        }
        checkCompletionTypes(*module, function, expected, "!ctnative.num<i32>", payload);
        checkCompletionTypes(*module, function, expected, "!ctnative.num<i32>", payload);
        if (!added.empty()) {
            for (auto * operation : llvm::reverse(added)) { operation->erase(); }
            checkCompletionTypes(*module, function, "!ctnative.num<i32>", "!ctnative.num<i32>");
        }
    }
    llvm::outs() << "source completion types: ten live mutations, fresh solves and reruns\n";
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

constexpr llvm::StringLiteral effectFixture = R"mlir(
module {
  ctjs.func private @leaf(%r: !ctjs.value, %nt: !ctjs.value,
                           %c: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %payload = ctjs.constant #ctjs.number<4629700416936869888>
    ctjs.throw %payload
  }
  ctjs.func @guarded$effects(%r: !ctjs.value, %nt: !ctjs.value,
                              %c: !ctjs.value, %unknown: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32, ctjs.not_structured = "preserved"} {
    %frame = ctjs.frame_enter 2
    %zero = ctjs.constant #ctjs.number<0>
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.push_handler ^body(%zero, %zero : !ctjs.value, !ctjs.value)
      catch ^handler(%zero, %zero : !ctjs.value, !ctjs.value)
  ^body(%a: !ctjs.value, %b: !ctjs.value):
    %condition = ctjs.truthy %unknown
    cf.cond_br %condition, ^arithmetic(%a, %b : !ctjs.value, !ctjs.value),
      ^arithmetic(%one, %b : !ctjs.value, !ctjs.value)
  ^arithmetic(%left: !ctjs.value, %scratch: !ctjs.value):
    %sum = ctjs.binary add %left, %one
    ctjs.check ^call(%sum, %scratch : !ctjs.value, !ctjs.value)
      caught ^handler(%left, %scratch : !ctjs.value, !ctjs.value)
  ^call(%saved: !ctjs.value, %old: !ctjs.value):
    %called = ctjs.call_direct @leaf(%r, %nt, %c)
    ctjs.check ^done(%saved, %called : !ctjs.value, !ctjs.value)
      caught ^handler(%saved, %old : !ctjs.value, !ctjs.value)
  ^done(%state: !ctjs.value, %returned: !ctjs.value):
    ctjs.pop_handler
    ctjs.frame_exit %frame
    ctjs.return %returned
  ^handler(%before: !ctjs.value, %oldScratch: !ctjs.value):
    %pad, %thrown = ctjs.catch_land
    %caught = ctjs.binary add %before, %one
    ctjs.frame_exit %frame
    ctjs.return %caught
  }
}
)mlir";

void restore(ctjs::FuncOp function,
             ctcompile::ctnative::lowering_detail::ExceptionRecoveryResult & result) {
    function.getBody().takeBody(result.original->getBody());
    function->setAttrs((*result.original)->getAttrs());
}

void testBindings(mlir::MLIRContext & context, llvm::StringRef source) {
    auto module = import(context, source);
    if (!module) { return; }
    auto function = guarded(*module);
    const auto before = printed(*module);
    const auto checks = countChecks(function);
    auto result = recoverPrimitiveExceptionRegion(function, 100000,
                                                  ExceptionRecoveryMode::EffectCheckedInvocations);
    if (!check(result.recovered, "source bindings first prove without stored annotations")) {
        llvm::errs() << result.refusal << '\n';
        return;
    }
    const unsigned complete = result.steps;
    restore(function, result);
    check(printed(*module) == before, "source binding proof rollback restores the whole module");
    for (unsigned budget = 0; budget < complete; ++budget) {
        auto limited = recoverPrimitiveExceptionRegion(
            function, budget, ExceptionRecoveryMode::EffectCheckedInvocations);
        if (!check(!limited.recovered &&
                       limited.refusal.find("budget exhausted") != std::string::npos &&
                       printed(*module) == before && countChecks(function) == checks,
                   "every incomplete source binding/effect budget preserves all original IR")) {
            llvm::errs() << "budget " << budget << ": " << limited.refusal << '\n';
            return;
        }
    }
    auto exact = recoverPrimitiveExceptionRegion(function, complete,
                                                 ExceptionRecoveryMode::EffectCheckedInvocations);
    if (!check(exact.recovered && exact.steps == complete,
               "the complete source binding/effect budget succeeds exactly")) {
        return;
    }
    restore(function, exact);
    check(printed(*module) == before, "exact source budget permits a fresh proof after rollback");
    llvm::outs() << "source binding recovery: " << complete
                 << " steps, every incomplete budget preserves " << checks << " checks\n";

    for (unsigned mutation = 0; mutation != 19; ++mutation) {
        auto current = import(context, source);
        if (!current) { return; }
        auto candidate = guarded(*current);
        auto prior = recoverPrimitiveExceptionRegion(
            candidate, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
        if (!check(prior.recovered, "binding mutation first proves the unchanged module")) {
            return;
        }
        restore(candidate, prior);
        auto call = firstCall(candidate);
        ctjs::FuncOp helper;
        ctjs::StoreGlobalOp declaration;
        ctjs::LoadGlobalOp load;
        for (auto body : current->getOps<ctjs::FuncOp>()) {
            if (body.getSymName() == call.getCallee()) { helper = body; }
        }
        current->walk([&](ctjs::StoreGlobalOp store) {
            if (store.getName() == "choose") { declaration = store; }
        });
        candidate.walk([&](ctjs::LoadGlobalOp found) {
            if (!load) { load = found; }
        });
        if (!check(helper && declaration && load, "binding controls find live source operands")) {
            return;
        }
        auto entry = declaration->getParentOfType<ctjs::FuncOp>();
        auto closure = declaration.getValue().getDefiningOp<ctjs::CreateClosureOp>();
        auto type = ctjs::ValueType::get(&context);
        mlir::OpBuilder at(entry.getBody().front().getTerminator());
        auto where = call.getLoc();
        if (mutation == 0) {
            at.clone(*declaration.getOperation());
        } else if (mutation == 1) {
            declaration->setOperand(0, entry.getBody().front().getArgument(0));
        } else if (mutation == 2) {
            declaration->moveBefore(entry.getBody().front().getTerminator());
        } else if (mutation == 3) {
            call.setCalleeAttr(mlir::FlatSymbolRefAttr::get(&context, candidate.getSymName()));
        } else if (mutation == 4) {
            call->setOperand(2, candidate.getBody().front().getArgument(0));
        } else if (mutation == 5) {
            load.setNameAttr(at.getStringAttr("hostAlternative"));
        } else if (mutation == 6 || mutation == 7) {
            auto global = ctjs::LoadGlobalOp::create(at, where, "globalThis");
            auto key = ctjs::ConstantOp::create(at, where, type,
                                                ctjs::StringAttr::get(&context, "choose"));
            if (mutation == 6) {
                ctjs::SetPropertyOp::create(at, where, global, key, declaration.getValue());
            } else {
                ctjs::GetPropertyOp::create(at, where, type, global, key);
            }
        } else if (mutation == 8) {
            (*current)->setAttr("ctjs.skipped", at.getArrayAttr({at.getDictionaryAttr({})}));
        } else if (mutation == 9) {
            mlir::OperationState unknown(where, "ctjs.unproved_binding_effect");
            at.create(unknown);
        } else if (mutation == 10) {
            at.setInsertionPoint(helper.getBody().front().getTerminator());
            ctjs::StoreGlobalOp::create(at, where, "unrelated",
                                        helper.getBody().front().getArgument(3));
        } else if (mutation == 11) {
            at.setInsertionPoint(helper.getBody().front().getTerminator());
            ctjs::UnaryOp::create(at, where, type,
                                  ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::Plus),
                                  helper.getBody().front().getArgument(3));
        } else if (mutation == 12) {
            helper.walk([&](ctjs::ThrowOp thrown) {
                thrown->setOperand(0, helper.getBody().front().getArgument(3));
            });
        } else if (mutation == 13) {
            auto copy = llvm::cast<ctjs::FuncOp>(helper->clone());
            copy.setSymName("duplicate$1");
            current->getBody()->push_back(copy);
        } else if (mutation == 14) {
            helper.walk([&](ctjs::ReturnOp returned) {
                returned->setOperand(0, helper.getBody().front().getArgument(2));
            });
        } else if (mutation == 15) {
            at.setInsertionPoint(call);
            ctjs::StoreGlobalOp::create(at, where, "escapedCallee", call.getCalleeValue());
        } else if (mutation == 16) {
            auto status = llvm::cast<ctjs::CheckOp>(load->getBlock()->getTerminator());
            for (auto [index, operand] : llvm::enumerate(status.getContOperands())) {
                if (operand == load.getResult()) {
                    status->setOperand(static_cast<unsigned>(index),
                                       candidate.getBody().front().getArgument(0));
                }
            }
        } else if (mutation == 17) {
            closure->setOperand(0, entry.getBody().front().getArgument(0));
        } else {
            at.setInsertionPoint(helper.getBody().front().getTerminator());
            auto key = ctjs::ConstantOp::create(at, where, type,
                                                ctjs::StringAttr::get(&context, "getter"));
            ctjs::GetPropertyOp::create(at, where, type, helper.getBody().front().getArgument(0),
                                        key);
        }
        if (mutation == 11 || mutation == 12) {
            // Live caller operands now prove the original boolean formal.
            // Keep an unknown alternative for the coercion/payload control;
            // one known caller cannot supply the whole family's fact.
            entry.walk([&](ctjs::CallDirectOp actual) {
                if (actual.getCallee() == candidate.getSymName()) {
                    actual->setOperand(3, entry.getBody().front().getArgument(0));
                }
            });
        }
        // The entire proof must be rebuilt after real IR changes, even when
        // forged summaries agree with its previous successful conclusion.
        load->setAttr("ctnative.nothrow", at.getUnitAttr());
        helper->setAttr("ctnative.nonthrowing", at.getUnitAttr());
        (*current)->setAttr("ctnative.global_bindings_complete", at.getUnitAttr());
        const auto changed = printed(*current);
        auto refused = recoverPrimitiveExceptionRegion(
            candidate, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
        if (!check(
                !refused.recovered && !refused.refusal.empty() && printed(*current) == changed &&
                    countChecks(candidate) == checks,
                "live binding, identity, getter and completion mutations preserve source checks")) {
            llvm::errs() << "binding mutation " << mutation << ": " << refused.refusal << '\n';
        }
    }
    for (bool knownPayload : {true, false}) {
        auto suffix = knownPayload ? "\nfunction unused(value) { throw 1; }\n"
                                   : "\nfunction unused(value) { throw value; }\n";
        auto current = import(context, source.str() + suffix);
        if (!current) { return; }
        auto candidate = guarded(*current);
        const auto unchanged = printed(*current);
        auto checked = recoverPrimitiveExceptionRegion(
            candidate, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
        check(checked.recovered == knownPayload,
              "uncalled helpers need independent primitive throw payloads for binding stability");
        if (checked.recovered) { restore(candidate, checked); }
        check(printed(*current) == unchanged,
              "uncalled throw effect proof preserves every source function and operation");
    }
}

std::string nestedSource(llvm::StringRef source, unsigned depth, bool payload) {
    std::string text = source.str();
    if (depth < 2) { return text; }
    text.replace(text.find("function choose("), std::string("function choose").size(),
                 "function leaf");
    const auto name = [](unsigned index) {
        return index == 0 ? std::string("choose") : "forward" + std::to_string(index);
    };
    const std::string parameters = payload ? "flag, payload" : "flag";
    for (unsigned index = 0; index < depth - 1; ++index) {
        const auto target = index == depth - 2 ? std::string("leaf") : name(index + 1);
        text += "\nfunction " + name(index) + "(" + parameters + ") { return " + target + "(" +
                parameters + "); }\n";
    }
    return text;
}

ctjs::FuncOp named(mlir::ModuleOp module, llvm::StringRef prefix) {
    for (auto function : module.getOps<ctjs::FuncOp>()) {
        if (function.getSymName().starts_with((prefix + "$").str())) { return function; }
    }
    return {};
}

void testTransitive(mlir::MLIRContext & context, llvm::StringRef source) {
    const auto nested = nestedSource(source, 3, true);
    auto module = import(context, nested);
    if (!module) { return; }
    auto function = guarded(*module);
    const auto before = printed(*module);
    const unsigned checks = countChecks(function);
    auto result = recoverPrimitiveExceptionRegion(function, 100000,
                                                  ExceptionRecoveryMode::EffectCheckedInvocations);
    if (!check(result.recovered, "the complete transitive source family proves")) {
        llvm::errs() << result.refusal << '\n';
        return;
    }
    check(mlir::succeeded(mlir::verify(*module)), "transitive invocation recovery verifies");
    const unsigned complete = result.steps;
    restore(function, result);
    check(printed(*module) == before, "transitive rollback preserves every family body");
    for (unsigned budget = 0; budget < complete; ++budget) {
        auto limited = recoverPrimitiveExceptionRegion(
            function, budget, ExceptionRecoveryMode::EffectCheckedInvocations);
        if (!check(!limited.recovered &&
                       limited.refusal.find("budget exhausted") != std::string::npos &&
                       printed(*module) == before && countChecks(function) == checks,
                   "every transitive proof cutoff preserves the complete source graph")) {
            llvm::errs() << "transitive budget " << budget << ": " << limited.refusal << '\n';
            return;
        }
    }
    auto exact = recoverPrimitiveExceptionRegion(function, complete,
                                                 ExceptionRecoveryMode::EffectCheckedInvocations);
    if (!check(exact.recovered && exact.steps == complete,
               "the exact transitive budget completes")) {
        return;
    }
    restore(function, exact);
    check(printed(*module) == before, "exact transitive rollback retains all source operations");
    llvm::outs() << "transitive recovery: " << complete
                 << " steps, every incomplete budget preserves " << checks << " checks\n";

    for (unsigned mutation = 0; mutation != 8; ++mutation) {
        const auto suffix =
            mutation == 2   ? "\nfunction unused(flag, payload) { return unused(flag, payload); }"
            : mutation == 3 ? "\nleaf(false, this);"
                            : "";
        auto current = import(context, nested + suffix);
        if (!current) { return; }
        auto candidate = guarded(*current);
        if (mutation != 2 && mutation != 3) {
            auto prior = recoverPrimitiveExceptionRegion(
                candidate, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
            if (!check(prior.recovered, "transitive mutation first proves the live family")) {
                return;
            }
            restore(candidate, prior);
        }
        auto forward = named(*current, "forward1");
        auto leaf = named(*current, "leaf");
        auto nestedCall = firstCall(forward);
        ctjs::LoadGlobalOp load;
        forward.walk([&](ctjs::LoadGlobalOp found) { load = found; });
        if (!check(forward && leaf && nestedCall && load, "transitive controls find their edges")) {
            return;
        }
        mlir::OpBuilder at(nestedCall);
        if (mutation == 0 || mutation == 1) {
            // Update both live identity operands. These are genuine recursive
            // source families, rather than a stale named-target mismatch.
            auto target = mutation == 0 ? forward : named(*current, "choose");
            load.setNameAttr(at.getStringAttr(mutation == 0 ? "forward1" : "choose"));
            nestedCall.setCalleeAttr(mlir::FlatSymbolRefAttr::get(&context, target.getSymName()));
        } else if (mutation == 4) {
            at.setInsertionPoint(leaf.getBody().front().getTerminator());
            auto key = ctjs::ConstantOp::create(at, leaf.getLoc(), ctjs::ValueType::get(&context),
                                                ctjs::StringAttr::get(&context, "getter"));
            ctjs::GetPropertyOp::create(at, leaf.getLoc(), ctjs::ValueType::get(&context),
                                        leaf.getBody().front().getArgument(0), key);
        } else if (mutation == 5) {
            ctjs::StoreGlobalOp declaration;
            current->walk([&](ctjs::StoreGlobalOp store) {
                if (store.getName() == "leaf") { declaration = store; }
            });
            at.setInsertionPointAfter(declaration);
            at.clone(*declaration.getOperation());
        } else if (mutation == 6) {
            nestedCall->setOperand(2, forward.getBody().front().getArgument(0));
        } else if (mutation == 7) {
            (*current)->setAttr("ctjs.skipped", at.getArrayAttr({at.getDictionaryAttr({})}));
        }
        (*current)->setAttr("ctnative.global_bindings_complete", at.getUnitAttr());
        leaf->setAttr("ctnative.nonthrowing", at.getUnitAttr());
        nestedCall->setAttr("ctnative.completions_proved", at.getUnitAttr());
        const auto changed = printed(*current);
        auto refused = recoverPrimitiveExceptionRegion(
            candidate, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
        check(!refused.recovered && !refused.refusal.empty() && printed(*current) == changed &&
                  countChecks(candidate) == checks,
              "late transitive identity, effect, actual and recursive alternatives refuse");
        if (mutation <= 2) {
            check(refused.refusal.find("recursive") != std::string::npos,
                  "recursive source components fail the complete live call census");
        }
    }
    for (unsigned depth : {32u, 33u}) {
        auto current = import(context, nestedSource(source, depth, true));
        if (!current) { return; }
        auto candidate = guarded(*current);
        const auto unchanged = printed(*current);
        auto checked = recoverPrimitiveExceptionRegion(
            candidate, 1000000, ExceptionRecoveryMode::EffectCheckedInvocations);
        check(checked.recovered == (depth == 32), "transitive completion depth is bounded at 32");
        if (checked.recovered) {
            check(mlir::succeeded(mlir::verify(*current)), "depth-boundary recovery verifies");
            restore(candidate, checked);
        } else {
            check(checked.refusal.find("depth exceeds 32") != std::string::npos,
                  "an excessive family reports the independent depth limit");
        }
        check(printed(*current) == unchanged, "depth-boundary proof retains exact rollback");
    }
}

void testSelectedActuals(mlir::MLIRContext & context) {
    constexpr llvm::StringLiteral source = R"js(
function leaf(flag, payload) {
    if (flag) { throw 32; }
    return payload;
}
function forward(flag, payload) { return leaf(flag, payload); }
function choose(flag, payload) { return forward(flag, payload); }
function guarded(flag, payload) {
    var mark = 0;
    try {
        mark = choose(false, payload);
        mark = choose(flag, payload);
    } catch (value) { return mark + value; }
    return mark;
}
var caught52 = guarded(true, 20);
var normal20 = guarded(false, 20);
leaf(false, this);
)js";
    // The final leaf caller has an unknown normal return. Its effects and
    // constant payload remain proved, but its result is not the first
    // protected call's saved state. Only that call's own actual chain may
    // establish the catch arithmetic's primitive operand.
    testSource(context, "transitive selected actuals", source, 2, 20, 32,
               ExceptionRecoveryMode::EffectCheckedInvocations);
    auto module = import(context, source);
    if (!module) { return; }
    auto function = guarded(*module);
    auto prior = recoverPrimitiveExceptionRegion(function, 100000,
                                                 ExceptionRecoveryMode::EffectCheckedInvocations);
    if (!check(prior.recovered, "selected actuals first prove independently of other returns")) {
        return;
    }
    restore(function, prior);
    auto entry = *module->getOps<ctjs::FuncOp>().begin();
    bool changed = false;
    entry.walk([&](ctjs::CallDirectOp call) {
        if (!changed && call.getCallee() == function.getSymName()) {
            call->setOperand(4, entry.getBody().front().getArgument(0));
            changed = true;
        }
    });
    check(changed, "selected-actual mutation finds a current root invocation");
    function->setAttr("ctnative.completions_proved", mlir::UnitAttr::get(&context));
    const auto before = printed(*module);
    auto refused = recoverPrimitiveExceptionRegion(function, 100000,
                                                   ExceptionRecoveryMode::EffectCheckedInvocations);
    check(!refused.recovered && !refused.refusal.empty() && printed(*module) == before,
          "one unknown selected actual cannot borrow a sibling caller's primitive result");
}

void testEffects(mlir::MLIRContext & context) {
    auto module = mlir::parseSourceString<mlir::ModuleOp>(effectFixture, &context);
    if (!check(static_cast<bool>(module), "closed primitive effect fixture parses")) { return; }
    auto function = guarded(*module);
    const auto before = printed(function);
    const unsigned checks = countChecks(function);
    auto result = recoverPrimitiveExceptionRegion(function, 100000,
                                                  ExceptionRecoveryMode::EffectCheckedInvocations);
    if (!check(result.recovered, "live primitive status and continuation effects discharge")) {
        llvm::errs() << result.refusal << '\n';
        return;
    }
    check(mlir::succeeded(mlir::verify(*module)), "effect-checked invocation IR verifies");
    check(checks == 2 && countChecks(function) == 0 && countChecks(*result.original) == checks,
          "a proved arithmetic check disappears only with the complete recovery transaction");
    unsigned calls = 0;
    function.walk([&](ctjs::InvokeOp) { ++calls; });
    check(calls == 1, "the throwing call keeps its separate invocation completion");
    const auto firstSteps = result.steps;
    restore(function, result);
    check(printed(function) == before,
          "effect-checked rollback restores exact source and attributes");

    // Every incomplete budget must fail before adoption, including cutoffs
    // inside primitive predecessor walks, the effect scan and structuring.
    for (unsigned budget = 0; budget < firstSteps; ++budget) {
        auto limited = recoverPrimitiveExceptionRegion(
            function, budget, ExceptionRecoveryMode::EffectCheckedInvocations);
        if (!check(!limited.recovered &&
                       limited.refusal.find("budget exhausted") != std::string::npos &&
                       printed(function) == before && countChecks(function) == checks,
                   "every incomplete effect/recovery budget preserves the original graph")) {
            llvm::errs() << "budget " << budget << ": " << limited.refusal << '\n';
            return;
        }
    }
    auto exact = recoverPrimitiveExceptionRegion(function, firstSteps,
                                                 ExceptionRecoveryMode::EffectCheckedInvocations);
    if (!check(exact.recovered && exact.steps == firstSteps,
               "the first complete effect/recovery budget succeeds exactly")) {
        return;
    }
    restore(function, exact);
    check(printed(function) == before, "exact-budget rollback permits a fresh proof");
    llvm::outs() << "effect recovery: " << firstSteps
                 << " steps, every incomplete budget preserves " << checks << " checks\n";

    for (unsigned mutation = 0; mutation != 13; ++mutation) {
        auto current = mlir::parseSourceString<mlir::ModuleOp>(effectFixture, &context);
        if (!current) { return; }
        auto candidate = guarded(*current);
        // Establish and roll back a success before every mutation: neither
        // the earlier success nor forged nothrow/type markers may survive as
        // authority for the next source graph.
        auto prior = recoverPrimitiveExceptionRegion(
            candidate, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
        if (!check(prior.recovered, "mutation fixture first proves unchanged effects")) { return; }
        restore(candidate, prior);
        auto & entry = candidate.getBody().front();
        ctjs::BinaryOp arithmetic, caught;
        mlir::cf::CondBranchOp branch;
        ctjs::PopHandlerOp normal;
        candidate.walk([&](ctjs::BinaryOp binary) {
            if (!arithmetic) {
                arithmetic = binary;
            } else {
                caught = binary;
            }
        });
        candidate.walk([&](mlir::cf::CondBranchOp found) { branch = found; });
        candidate.walk([&](ctjs::PopHandlerOp found) { normal = found; });
        mlir::OpBuilder at(arithmetic);
        const auto where = arithmetic.getLoc();
        const auto unknown = entry.getArgument(3);
        const auto type = ctjs::ValueType::get(&context);
        if (mutation == 0 || mutation == 3 || mutation == 4 || mutation == 9) {
            if (mutation == 3) { at.setInsertionPoint(normal); }
            if (mutation == 4) { at.setInsertionPoint(caught); }
            auto load = ctjs::LoadGlobalOp::create(at, where, "unprovedGetter");
            if (mutation == 9) {
                load->setAttr("ctnative.nothrow", at.getUnitAttr());
                load->setAttr("ctnative.type", at.getStringAttr("num<i32>"));
                candidate->setAttr("ctnative.nonthrowing", at.getUnitAttr());
            }
        } else if (mutation == 1) {
            mlir::OperationState allocation(where, "ctjs.create_object");
            allocation.addTypes(type);
            at.create(allocation);
        } else if (mutation == 2) {
            mlir::OperationState store(where, "ctjs.store_global");
            store.addAttribute("name", at.getStringAttr("published"));
            store.addOperands(arithmetic->getOperand(0));
            at.create(store);
        } else if (mutation == 5) {
            // Both edges have the same successor. Checking only the first
            // incoming operand would incorrectly retain a primitive fact.
            branch->setOperand(1u + static_cast<unsigned>(branch.getTrueDestOperands().size()),
                               unknown);
        } else if (mutation == 6) {
            arithmetic->setOperand(1, unknown);
        } else if (mutation == 7) {
            auto absent =
                ctjs::ConstantOp::create(at, where, type, ctjs::UndefinedAttr::get(&context));
            ctjs::ConvertOp::create(
                at, where, type, ctjs::ConvertKindAttr::get(&context, ctjs::ConvertKind::ToObject),
                absent);
        } else if (mutation == 8) {
            arithmetic.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Concat));
            arithmetic->setOperand(1, unknown);
        } else if (mutation == 10) {
            auto key =
                ctjs::ConstantOp::create(at, where, type, ctjs::StringAttr::get(&context, "x"));
            ctjs::GetPropertyOp::create(at, where, type, unknown, key);
        } else if (mutation == 11) {
            at.setInsertionPoint(normal);
            at.clone(*firstCall(candidate).getOperation());
        } else {
            // Its normal result is always numeric, but coercing the input
            // can run valueOf or throw. Result facts do not discharge it.
            auto numeric = ctjs::UnaryOp::create(
                at, where, type, ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::Plus),
                unknown);
            check(llvm::isa<ctcompile::ctnative::NumType>(
                      ctcompile::ctnative::staticResultType(numeric)),
                  "throwing coercion has an unconditional numeric normal-result fact");
        }
        const auto changed = printed(candidate);
        auto refused = recoverPrimitiveExceptionRegion(
            candidate, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
        check(!refused.recovered && refused.refusal.find("nonthrowing") != std::string::npos &&
                  printed(candidate) == changed && countChecks(candidate) == checks,
              "live status/continuation effect mutation preserves all original edges");
    }
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
    for (auto mode : {ExceptionRecoveryMode::CheckedInvocations,
                      ExceptionRecoveryMode::EffectCheckedInvocations}) {
        testSource(context, "assignment", source("assignment"), 1, 10, 32, mode);
        testSource(context, "sequential", source("sequential"), 2, 20, 32, mode);
        testSource(context, "argument", source("argument"), 1, 14, 14, mode);
    }
    testMutations(context, source("assignment"));
    testBindings(context, source("assignment"));
    for (auto name : {"assignment", "sequential", "argument"}) {
        testSourceCompletionTypes(context, name, source(name), "!ctnative.num<i32>",
                                  "!ctnative.num<i32>");
    }
    testSourceCompletionTypes(context, "string", R"js(
function choose(flag) {
    if (flag) { throw "payload"; }
    return "normal";
}
function guarded(flag) {
    var mark = "entry";
    try { mark = "saved"; mark = choose(flag); }
    catch (value) { return mark + value; }
    return mark;
}
var caught = guarded(true);
var normal = guarded(false);
)js",
                              "!ctnative.str<utf8>", "!ctnative.str<utf8>");
    testSourceCompletionTypes(context, "boolean", R"js(
function choose(flag) {
    if (flag) { throw true; }
    return false;
}
function guarded(flag) {
    var mark = false;
    try { mark = true; mark = choose(flag); }
    catch (value) { return mark === value; }
    return mark;
}
var caught = guarded(true);
var normal = guarded(false);
)js",
                              "!ctnative.bool", "!ctnative.bool");
    testSourceCompletionMutations(context, source("assignment"));
    for (auto [name, calls, saved, payload, parameters] :
         {std::tuple{"assignment", 1u, 10.0, 32.0, false},
          std::tuple{"sequential", 2u, 20.0, 32.0, false},
          std::tuple{"argument", 1u, 14.0, 14.0, true}}) {
        testSource(context, ("transitive " + std::string(name)),
                   nestedSource(source(name), 3, parameters), calls, saved, payload,
                   ExceptionRecoveryMode::EffectCheckedInvocations);
    }
    testTransitive(context, source("argument"));
    testSourceCompletionTypes(context, "transitive callee lookup",
                              nestedSource(source("assignment"), 3, false), "!ctnative.boxed",
                              "!ctnative.num<i32>");
    testSelectedActuals(context);
    testEffects(context);
    return failures == 0 ? 0 : 1;
}
