// PHASE 54A'S TRANSFER FUNCTION, ONE JAVASCRIPT FACT AT A TIME.
//
// The oracle in TypeOracle.cpp answers "is the inference sound over a corpus",
// which is the question that matters and is also the question that cannot say
// WHICH rule is wrong. This file is the other half: a table of small functions
// whose right answer is a statement about JavaScript, checked individually, so
// a regression names the operator.
//
// THE NEGATIVE ROWS ARE THE POINT OF THE TABLE. An inference that answered
// `num<i32>` for every `|` would pass a table containing only the positive
// rows, and it would be WRONG, because `1n | 2n` is `3n`. So for each numeric
// operator there are two rows - one where the operands are known numbers and
// the claim is allowed, and one where they are function parameters and the
// claim must collapse to `boxed`. If a change makes the analysis unconditional,
// the negative rows go red and say which operator did it.
//
// Part 23 §1.4's ratio note: this file is a TEST, and tests are not counted
// against the ODS-first rule - there is no TableGen way to assert that
// `-0 | 0` is an int32 and `-0` alone is not.
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

using ctcompile::ctnative::TypeInference;
using ctcompile::ctnative::TypeLattice;

int failures = 0;

// One row: a ctjs function, and what the operation marked `check` must infer.
// `body` OWNS ITS TEXT rather than pointing at it. Several rows build their
// body by concatenation, and a `const char *` taken from such a temporary
// dangles at the end of the initializer's full-expression - which reads as a
// parse failure in a row that is written correctly.
struct row {
    const char * what;     // the JavaScript fact, for the failure message
    std::string body;      // the operations, inside a ctjs.func
    const char * expected; // the printed ctnative type
};

// THE FUNCTION HEADER EVERY ROW SHARES. ctjs.func takes three implicit
// arguments before the JavaScript ones - receiver, new_target, callee - and
// %p and %q are ordinary parameters, which is what makes them `boxed`: nothing
// is known about a caller, so nothing is known about an argument. That is
// precisely the state in which a BigInt claim cannot be ruled out.
constexpr const char * kPrologue =
    "ctjs.func @f(%receiver: !ctjs.value, %new_target: !ctjs.value, "
    "%callee: !ctjs.value, %p: !ctjs.value, %q: !ctjs.value) -> !ctjs.value "
    "attributes {upvalue_count = 0 : i32} {\n";

// 5.0, 1.5, -0.0 and 2**31 as the bit patterns ctjs.number carries. Spelled as
// bits because a NumberAttr is bits - see CTJSAttrs.td, which explains that a
// builtin FloatAttr would lose the difference between -0.0 and 0.0, and that
// difference is one of the rows below.
constexpr const char * kFive = "#ctjs.number<4617315517961601024>";         // 5.0
constexpr const char * kOneAndAHalf = "#ctjs.number<4609434218613702656>";  // 1.5
constexpr const char * kNegativeZero = "#ctjs.number<9223372036854775808>"; // -0.0
constexpr const char * kTwoToThe31 = "#ctjs.number<4746794007248502784>";   // 2147483648.0

std::string prologue() {
    return std::string{kPrologue};
}

void check(mlir::MLIRContext & context, const row & r) {
    std::string text = prologue() + r.body + "  ctjs.return %p\n}\n";

    mlir::OwningOpRef<mlir::ModuleOp> module =
        mlir::parseSourceString<mlir::ModuleOp>(text, &context);
    if (!module) {
        std::printf("FAIL %s: the module did not parse\n%s\n", r.what, text.c_str());
        ++failures;
        return;
    }

    // DeadCodeAnalysis IS NOT OPTIONAL. The sparse framework gets its
    // predecessor information from it; without it a block argument never
    // receives what was branched into it and every answer is uninitialized.
    mlir::DataFlowSolver solver;
    solver.load<mlir::dataflow::DeadCodeAnalysis>();
    solver.load<TypeInference>();
    if (failed(solver.initializeAndRun(module->getOperation()))) {
        std::printf("FAIL %s: the solver did not converge\n", r.what);
        ++failures;
        return;
    }

    mlir::Operation * marked = nullptr;
    module->walk([&](mlir::Operation * op) {
        if (op->hasAttr("check")) { marked = op; }
    });
    if (marked == nullptr || marked->getNumResults() != 1) {
        std::printf("FAIL %s: no single-result operation carried `check`\n", r.what);
        ++failures;
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
    if (got != r.expected) {
        std::printf("FAIL %s\n  expected %s\n  got      %s\n", r.what, r.expected, got.c_str());
        ++failures;
    }
}

} // namespace

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctcompile::ctjs::CTJSDialect>();
    context.getOrLoadDialect<ctcompile::ctnative::CTNativeDialect>();

    const std::string five = std::string{"  %a = ctjs.constant "} + kFive + "\n" +
                             "  %b = ctjs.constant " + kFive + "\n";

    const std::vector<row> rows = {
        // --- literals, where the bound proof is the literal itself ----------
        {"5 is an int32", std::string{"  %r = ctjs.constant "} + kFive + " {check}\n",
         "!ctnative.num<i32>"},
        {"1.5 is not an int32", std::string{"  %r = ctjs.constant "} + kOneAndAHalf + " {check}\n",
         "!ctnative.num<f64>"},
        // THE ROW MOST LIKELY TO BE GOT WRONG. -0 is integral and inside int32,
        // and `Object.is(-0, 0)` is false, so calling it an int32 loses a
        // difference JavaScript can see. type-oracle.py agrees by counting
        // NUM_NEGATIVE_ZERO as not-an-i32.
        {"-0 is integral and in range and is still NOT an int32",
         std::string{"  %r = ctjs.constant "} + kNegativeZero + " {check}\n", "!ctnative.num<f64>"},
        {"2**31 is one past the int32 maximum",
         std::string{"  %r = ctjs.constant "} + kTwoToThe31 + " {check}\n", "!ctnative.num<f64>"},
        {"undefined is an empty optional", "  %r = ctjs.constant #ctjs.undefined {check}\n",
         "!ctnative.opt<!ctnative.bottom>"},
        {"null is the same empty optional, which is a declared divergence",
         "  %r = ctjs.constant #ctjs.null {check}\n", "!ctnative.opt<!ctnative.bottom>"},
        {"a boolean literal", "  %r = ctjs.constant #ctjs.boolean<true> {check}\n",
         "!ctnative.bool"},
        {"a string literal", "  %r = ctjs.constant #ctjs.string<\"hi\"> {check}\n",
         "!ctnative.str<utf8>"},

        // --- the operators that need no operand proof -----------------------
        {"`>>>` has no BigInt form, so it is always a number",
         "  %r = ctjs.binary ushr %p, %q {check}\n", "!ctnative.num<f64>"},
        {"`typeof` is always a string", "  %r = ctjs.unary typeof %p {check}\n",
         "!ctnative.str<utf8>"},
        {"unary `+` is ToNumber, which throws on a BigInt", "  %r = ctjs.unary plus %p {check}\n",
         "!ctnative.num<f64>"},
        {"a comparison is always a boolean", "  %r = ctjs.compare lt %p, %q {check}\n",
         "!ctnative.bool"},
        {"concat never consults the BigInt arm", "  %r = ctjs.binary concat %p, %q {check}\n",
         "!ctnative.str<utf8>"},
        {"ToNumber is a double", "  %r = ctjs.convert to_number %p {check}\n",
         "!ctnative.num<f64>"},

        // --- THE NEGATIVE ROWS, which are why the table exists --------------
        {"`|` on unknown operands could be BigInt and must NOT claim i32",
         "  %r = ctjs.binary bitor %p, %q {check}\n", "!ctnative.boxed"},
        {"`-` on unknown operands could be BigInt", "  %r = ctjs.binary sub %p, %q {check}\n",
         "!ctnative.boxed"},
        {"`~` on an unknown operand could be BigInt", "  %r = ctjs.unary bitnot %p {check}\n",
         "!ctnative.boxed"},
        {"generic `+` concatenates, so it stays boxed even on two numbers",
         five + "  %r = ctjs.binary add %a, %b {check}\n", "!ctnative.boxed"},

        // --- and the positive halves of the same operators ------------------
        {"`|` on two numbers is an int32", five + "  %r = ctjs.binary bitor %a, %b {check}\n",
         "!ctnative.num<i32>"},
        {"`-` on two numbers is a double", five + "  %r = ctjs.binary sub %a, %b {check}\n",
         "!ctnative.num<f64>"},
        {"`~` on a number is an int32", five + "  %r = ctjs.unary bitnot %a {check}\n",
         "!ctnative.num<i32>"},
        {"static `+` is ToNumber, so on two numbers it is a double",
         five + "  %r = ctjs.binary_static add %a, %b {check}\n", "!ctnative.num<f64>"},
    };

    for (const row & r : rows) { check(context, r); }

    if (failures != 0) {
        std::printf("\n%d row(s) failed\n", failures);
        return 1;
    }
    std::printf("type inference: every row agrees with JavaScript\n");
    return 0;
}
