//===- Helpers.cpp - Shared helpers for the type inference tests ----------===//
//
// The solver driver behind check(), and the text builders for invoke and
// throwing-helper fixtures declared in Tests.h.
//
//===----------------------------------------------------------------------===//

#include "Tests.h"

#include "check.hpp"

namespace ctcompile::test::type_inference {

std::string prologue() {
    return std::string{kPrologue};
}

void check(mlir::ModuleOp module, const char * what, const char * expected,
           const ctcompile::ctnative::OwnedGlobalRoots * owner) {
    // DeadCodeAnalysis is required. The sparse framework gets its predecessor
    // information from it; without it a block argument never receives what was
    // branched into it and every answer is uninitialized.
    mlir::DataFlowSolver solver;
    solver.load<mlir::dataflow::DeadCodeAnalysis>();
    // SparseConstantPropagation is required too: DeadCodeAnalysis marks a
    // branch successor live only after reading the branch operands'
    // ConstantValue lattices. With no provider those stay uninitialized, no
    // successor is marked live, and every non-entry block is silently skipped
    // (single-block tests still pass).
    solver.load<mlir::dataflow::SparseConstantPropagation>();
    solver.load<TypeInference>(owner);
    if (failed(solver.initializeAndRun(module))) {
        fail("{}: the solver did not converge", what);
        return;
    }

    mlir::Operation * marked = nullptr;
    module.walk([&](mlir::Operation * op) {
        if (op->hasAttr("check")) { marked = op; }
    });
    if (marked == nullptr || marked->getNumResults() != 1) {
        fail("{}: no single-result operation carried `check`", what);
        return;
    }

    const TypeLattice * lattice = solver.lookupState<TypeLattice>(marked->getResult(0));
    std::string got;
    if (lattice == nullptr) {
        got = "<no lattice>";
    } else {
        llvm::raw_string_ostream os{got};
        lattice->getValue().print(os);
    }
    if (got != expected) { fail("{}\n  expected {}\n  got      {}", what, expected, got.c_str()); }
}

void check(mlir::MLIRContext & context, const row & r) {
    std::string text = r.wholeModule ? r.body : prologue() + r.body + "  ctjs.return %p\n}\n";
    mlir::OwningOpRef<mlir::ModuleOp> module =
        mlir::parseSourceString<mlir::ModuleOp>(text, &context);
    if (!module) {
        fail("{}: the module did not parse\n{}", r.what, text.c_str());
        return;
    }
    check(*module, r.what, r.expected);
    if (r.fieldAssigned >= 0) {
        mlir::Operation * read = nullptr;
        module->walk([&](mlir::Operation * op) {
            if (op->hasAttr("check")) { read = op; }
        });
        const auto proof = ctcompile::ctnative::queryNativeObjectFieldPresence(read);
        if (proof.exhausted || proof.assigned != (r.fieldAssigned != 0)) {
            fail("{}: live field presence={} exhausted={}", r.what,
                 static_cast<int>(proof.assigned), static_cast<int>(proof.exhausted));
        }
    }
}

std::string invokeModule(const std::string & helpers, llvm::StringRef observed,
                         bool observeNormal) {
    // %argument below is the IEEE-754 bit pattern of 5.0.
    const std::string observation = "    %observed = scf.execute_region -> !ctjs.value {\n"
                                    "      scf.yield " +
                                    observed.str() + " : !ctjs.value\n    } {check}\n";
    return R"mlir(
module {
  ctjs.func @caller(%receiver: !ctjs.value, %new_target: !ctjs.value,
                    %callee: !ctjs.value, %condition: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined
    %before = ctjs.constant #ctjs.string<"before call">
    %argument = ctjs.constant #ctjs.number<4617315517961601024>
    %result = ctjs.invoke {
      %called = ctjs.call_direct @helper(%nil, %nil, %nil, %condition, %argument)
      ctjs.invoke_exit %called state(%before)
    } normal {
    ^bb0(%returned: !ctjs.value):
)mlir" + (observeNormal ? observation : "") +
           R"mlir(
      ctjs.invoke_yield(%before)
    } unwind {
    ^bb0(%payload: !ctjs.value, %saved: !ctjs.value):
)mlir" + (!observeNormal ? observation : "") +
           R"mlir(
      ctjs.invoke_yield(%saved)
    } : !ctjs.value
    ctjs.return %result
  }
)mlir" + helpers +
           "}\n";
}

std::string throwingHelper(llvm::StringRef payload, llvm::StringRef prefix) {
    return R"mlir(
  ctjs.func private @helper(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value, %condition: !ctjs.value,
                            %argument: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
)mlir" + prefix.str() +
           "    %thrown = ctjs.constant " + payload.str() + "\n    ctjs.throw %thrown\n  }\n";
}

std::string conditionalHelper(llvm::StringRef result, llvm::StringRef payload,
                              llvm::StringRef prefix) {
    return R"mlir(
  ctjs.func private @helper(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value, %condition: !ctjs.value,
                            %argument: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32, ctnative.nothrow} {
)mlir" + prefix.str() +
           R"mlir(
    %bit = ctjs.truthy %condition
    cf.cond_br %bit, ^normal, ^throwing
  ^normal:
)mlir" + "    %normal = ctjs.constant " +
           result.str() + R"mlir(
    ctjs.return %normal
  ^throwing:
)mlir" + "    %thrown = ctjs.constant " +
           payload.str() + "\n    ctjs.throw %thrown\n  }\n";
}

std::string helperChain(unsigned depth) {
    std::string text;
    for (unsigned index = 0; index < depth; ++index) {
        const std::string name = index == 0 ? "helper" : "helper" + std::to_string(index);
        text += "  ctjs.func private @" + name + R"mlir((%receiver: !ctjs.value,
            %new_target: !ctjs.value, %callee: !ctjs.value,
            %condition: !ctjs.value, %argument: !ctjs.value) -> !ctjs.value
        attributes {upvalue_count = 0 : i32} {
)mlir";
        if (index + 1 == depth) {
            text += "    ctjs.throw %argument\n";
        } else {
            text += "    %r = ctjs.call_direct @helper" + std::to_string(index + 1) +
                    "(%receiver, %new_target, %callee, %condition, %argument)\n"
                    "    ctjs.return %r\n";
        }
        text += "  }\n";
    }
    return text;
}

} // namespace ctcompile::test::type_inference
