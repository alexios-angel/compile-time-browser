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

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
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
    // AND SparseConstantPropagation, WHICH IS NOT OPTIONAL EITHER, and the
    // reason is a trap worth naming. DeadCodeAnalysis decides which successor
    // of a branch is live by asking for every branch operand's ConstantValue
    // lattice; if nothing provides one, those lattices stay uninitialized, it
    // bails out, and NO successor is ever marked live. The sparse analysis
    // then skips every op in every non-entry block - measured: 109 unvisited
    // values and zero registers beating `boxed` on the fixture, while the
    // single-block unit rows all passed.
    solver.load<mlir::dataflow::SparseConstantPropagation>();
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
    context.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    context.getOrLoadDialect<mlir::ub::UBDialect>();
    context.getOrLoadDialect<ctcompile::ctnative::CTNativeDialect>();

    const std::string five = std::string{"  %a = ctjs.constant "} + kFive + "\n" +
                             "  %b = ctjs.constant " + kFive + "\n";
    // The array rows need their own constant names: `five` spells %a and %b,
    // and %a would collide with the array in the rows below.
    const std::string ints = std::string{"  %i1 = ctjs.constant "} + kFive + "\n" +
                             "  %i2 = ctjs.constant " + kFive + "\n";

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
        {"generic `+` on unknown operands could concatenate or be BigInt",
         "  %r = ctjs.binary add %p, %q {check}\n", "!ctnative.boxed"},
        {"generic `+` on one unknown operand stays boxed even beside a number",
         five + "  %r = ctjs.binary add %a, %p {check}\n", "!ctnative.boxed"},
        // --- and the two halves of `+` that ARE provable ---------------------
        {"generic `+` on two numbers is a double", five + "  %r = ctjs.binary add %a, %b {check}\n",
         "!ctnative.num<f64>"},
        {"generic `+` on a number and undefined is a double (NaN, but a number)",
         five + "  %u = ctjs.constant #ctjs.undefined\n"
                "  %r = ctjs.binary add %a, %u {check}\n",
         "!ctnative.num<f64>"},
        {"generic `+` with a proved string on either side is a string",
         five + "  %s = ctjs.constant #ctjs.string<\"x\">\n"
                "  %r = ctjs.binary add %a, %s {check}\n",
         "!ctnative.str<utf8>"},
        {"generic `+` of a string and an unknown operand is still a string",
         "  %s = ctjs.constant #ctjs.string<\"x\">\n"
         "  %r = ctjs.binary add %s, %p {check}\n",
         "!ctnative.str<utf8>"},

        // --- A SECOND BLOCK, which the single-block rows above cannot test --
        //
        // Every other row lives in the entry block, and the entry block is
        // live by fiat. This one puts the checked op behind a branch so the
        // solver has to decide the successor is live - which it cannot do
        // without SparseConstantPropagation loaded. Without it this row reads
        // `<uninitialized>`.
        {"a value behind a branch is still visited",
         "  %t = ctjs.truthy %p\n"
         "  cf.cond_br %t, ^yes, ^no\n"
         "^yes:\n"
         "  %r = ctjs.unary typeof %p {check}\n"
         "  ctjs.return %r\n"
         "^no:\n",
         "!ctnative.str<utf8>"},

        // --- THE CLOSED WORLD FOR GLOBALS (part 24 Phase 62½-A) -------------
        //
        // A load of a global is the join of every store of that name in the
        // module. Three rows: a stored number is a number; a name nothing
        // stores is boxed (it is a builtin or undeclared); and the mere
        // presence of a `globalThis` load anywhere makes every global boxed,
        // because the table may then be written by a path this rule cannot
        // see.
        {"a global stored a number loads as a number OR undefined - nothing orders the load after "
         "the store",
         five + "  ctjs.store_global \"g\", %a\n"
                "  %r = ctjs.load_global \"g\" {check}\n",
         "!ctnative.opt<!ctnative.num<i32>>"},
        {"a global stored a number and a string loads as their join",
         five + "  ctjs.store_global \"g\", %a\n"
                "  %s = ctjs.constant #ctjs.string<\"x\">\n"
                "  ctjs.store_global \"g\", %s\n"
                "  %r = ctjs.load_global \"g\" {check}\n",
         "!ctnative.opt<!ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>>>"},
        {"a global nothing stores is boxed - it is a builtin or undeclared",
         "  %r = ctjs.load_global \"Math\" {check}\n", "!ctnative.boxed"},
        {"a globalThis load anywhere makes every global boxed",
         five + "  ctjs.store_global \"g\", %a\n"
                "  %w = ctjs.load_global \"globalThis\"\n"
                "  %r = ctjs.load_global \"g\" {check}\n",
         "!ctnative.boxed"},

        // --- THE LIFT'S POISON IS THE IDENTITY --------------------------------
        //
        // --ctjs-lift-to-scf yields ub.poison for a loop-carried value on the
        // path that leaves the loop. Joined with a number it must stay that
        // number; typed boxed it would absorb, and every `for` loop would be
        // refused by the native lowering - which is how this row was found.
        {"a number joined with the lift's poison is still that number",
         five + "  %t = ctjs.truthy %p\n"
                "  %z = ub.poison : !ctjs.value\n"
                "  %r = scf.if %t -> (!ctjs.value) {\n"
                "    scf.yield %a : !ctjs.value\n"
                "  } else {\n"
                "    scf.yield %z : !ctjs.value\n"
                "  } {check}\n",
         "!ctnative.num<i32>"},

        // --- THE CLOSED SHAPE (part 24 Phase 56A) ----------------------------
        //
        // An object literal used only through constant keys: a read of a key
        // is the join of its stores, from undefined. Any other use opens the
        // shape and every read is boxed.
        {"a closed object's field reads as its store, or undefined",
         five + "  %o = ctjs.create_object\n"
                "  %k = ctjs.constant #ctjs.string<\"x\">\n"
                "  ctjs.set_property %o[%k], %a\n"
                "  %k2 = ctjs.constant #ctjs.string<\"x\">\n"
                "  %r = ctjs.get_property %o[%k2] {check}\n",
         "!ctnative.opt<!ctnative.num<i32>>"},
        {"a key never stored reads as undefined",
         "  %o = ctjs.create_object\n"
         "  %k = ctjs.constant #ctjs.string<\"x\">\n"
         "  %r = ctjs.get_property %o[%k] {check}\n",
         "!ctnative.opt<!ctnative.bottom>"},
        {"a dynamic key opens the shape: every read is boxed",
         five + "  %o = ctjs.create_object\n"
                "  %k = ctjs.constant #ctjs.string<\"x\">\n"
                "  ctjs.set_property %o[%k], %a\n"
                "  ctjs.set_property %o[%p], %a\n"
                "  %r = ctjs.get_property %o[%k] {check}\n",
         "!ctnative.boxed"},
        {"an object that reaches a call has an open shape",
         five + "  %o = ctjs.create_object\n"
                "  %k = ctjs.constant #ctjs.string<\"x\">\n"
                "  ctjs.set_property %o[%k], %a\n"
                "  %c = ctjs.call %p(%o)\n"
                "  %r = ctjs.get_property %o[%k] {check}\n",
         "!ctnative.boxed"},

        // --- THE DENSE ARRAY (part 24 Phase 57A) -----------------------------
        //
        // WHY THESE ROWS EXIST AT ALL. The emitter hardcodes `vector<double>`
        // and PrintDeduced::isDeducible excludes both `emitc.call_opaque` and
        // `emitc.variable`, so nothing downstream ever prints the element type
        // - a join that widened wrongly would still lower, still compile and
        // still agree with the interpreter on the fixture. The element type is
        // observable HERE and in the oracle corpus, and nowhere else.
        {"a dense array literal is a vector of the join of its appends, from undefined",
         ints + "  %arr = ctjs.create_array [] {check}\n"
                "  ctjs.append %i1 to %arr\n"
                "  ctjs.append %i2 to %arr\n",
         "!ctnative.vec<!ctnative.opt<!ctnative.num<i32>>>"},
        // TWO WIDTHS MERGE, THEY DO NOT UNION. Stage 53G normalises `num<i32>`
        // and `num<f64>` into the wider number, so `[1, 2.5]` is a
        // vector<double> and not a vector of a two-alternative variant.
        {"two numeric widths in one array are the wider number, not a union",
         ints + "  %h = ctjs.constant " + kOneAndAHalf +
             "\n"
             "  %arr = ctjs.create_array [] {check}\n"
             "  ctjs.append %i1 to %arr\n"
             "  ctjs.append %h to %arr\n",
         "!ctnative.vec<!ctnative.opt<!ctnative.num<f64>>>"},
        {"a literal's own inline elements count toward the element type too",
         ints + "  %arr = ctjs.create_array [%i1] {check}\n",
         "!ctnative.vec<!ctnative.opt<!ctnative.num<i32>>>"},
        {"an index read is the element type - undefined among it, because a[7] is undefined",
         ints + "  %arr = ctjs.create_array []\n"
                "  ctjs.append %i1 to %arr\n"
                "  %r = ctjs.get_property %arr[%i1] {check}\n",
         "!ctnative.opt<!ctnative.num<i32>>"},
        // `length` IS NEVER UNDEFINED and is never an i32 either: an array's
        // length is a uint32, which does not fit one, and nothing here proves
        // this array is short.
        {"`length` on a dense array is a number, and an f64 rather than an i32",
         ints + "  %arr = ctjs.create_array []\n"
                "  ctjs.append %i1 to %arr\n"
                "  %k = ctjs.constant #ctjs.string<\"length\">\n"
                "  %r = ctjs.get_property %arr[%k] {check}\n",
         "!ctnative.num<f64>"},

        // --- AND THE FOUR NEGATIVE ROWS, which are why the rule is a proof ---
        //
        // An index STORE is the sparsity route part 24 Stage 57A names by
        // hand: `a[100] = 1` gives `length` 101 with one element. It opens the
        // site, so the literal and every read of it are boxed.
        {"an index store opens the site: a[100] = 1 makes it sparse",
         ints + "  %arr = ctjs.create_array []\n"
                "  ctjs.append %i1 to %arr\n"
                "  ctjs.set_property %arr[%i1], %i2\n"
                "  %r = ctjs.get_property %arr[%i1] {check}\n",
         "!ctnative.boxed"},
        // A KEY NOTHING PROVED A NUMBER READS A PROPERTY, NOT AN ELEMENT:
        // `a["push"]` is a function. The site is still dense - a read is a
        // read - so only the key check stands between this and a claim of
        // `opt<num<i32>>` for a function.
        //
        // THIS ROW IS THE ONLY GATE ON THAT GUARD, and the oracle is not, which
        // was measured: a claim is per REGISTER, the bytecode puts the array
        // literal and the read in one slot, and the join over the two is
        // `boxed` whatever the read claims. Deleting the guard turns this row
        // red with `!ctnative.opt<!ctnative.num<i32>>` and leaves every corpus
        // in check-type-claims.cmake green.
        {"a key nothing proved a number reads a property, and is boxed",
         ints + "  %arr = ctjs.create_array []\n"
                "  ctjs.append %i1 to %arr\n"
                "  %r = ctjs.get_property %arr[%p] {check}\n",
         "!ctnative.boxed"},
        {"a read through a named key opens the site",
         ints + "  %arr = ctjs.create_array [] {check}\n"
                "  ctjs.append %i1 to %arr\n"
                "  %k = ctjs.constant #ctjs.string<\"foo\">\n"
                "  %r = ctjs.get_property %arr[%k]\n",
         "!ctnative.boxed"},
        {"an array that escapes into a global is not a vector",
         ints + "  %arr = ctjs.create_array [] {check}\n"
                "  ctjs.append %i1 to %arr\n"
                "  ctjs.store_global \"g\", %arr\n",
         "!ctnative.boxed"},

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

    // AND ONE MODULE THAT MUST NOT PARSE. `ctjs.binary_static sub` names a
    // kind context::binary_op_static has no arm for - it answers undefined -
    // so an inference claiming f64 for it would be wrong, and the fix is not
    // a narrower claim but a verifier that refuses the IR. This is the
    // refutation Phase 54A's adversarial review raised, kept as a test.
    {
        const std::string text =
            prologue() + "  %r = ctjs.binary_static sub %p, %q\n" + "  ctjs.return %r\n}\n";
        mlir::ScopedDiagnosticHandler quiet{&context,
                                            [](mlir::Diagnostic &) { return mlir::success(); }};
        mlir::OwningOpRef<mlir::ModuleOp> module =
            mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        if (module) {
            std::printf("FAIL ctjs.binary_static sub verified, and the helper has no arm for it\n");
            ++failures;
        }
    }

    if (failures != 0) {
        std::printf("\n%d row(s) failed\n", failures);
        return 1;
    }
    std::printf("type inference: every row agrees with JavaScript\n");
    return 0;
}
