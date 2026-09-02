// PHASE 55A'S TABLE, ONE CELL AT A TIME.
//
// The oracle (Stage 55O) answers "is the analysis sound over a corpus", which
// is the question that matters and cannot say WHICH cell is wrong. This is the
// other half: one small function per cell of the sinks-and-carriers table
// (25-escape-analysis.md §2.2), checked individually, so a regression names
// the operand.
//
// THE NEGATIVE ROWS ARE THE POINT. For every SINK cell there is a row in which
// that operand ALONE flips a site to `escapes:<reason>`; for every NEITHER and
// CARRY cell there is a row in which the site stays confined through it. An
// analysis that sank nothing would pass the positive rows and be wrong about
// `return {}`; an analysis that sank everything would pass the negative rows
// and prove nothing. The two families together pin the table.
//
// AND EVERY ROLE IS ASSERTED THROUGH THE GENERATED INTERFACE AS WELL: each
// row's `roles` string names the operand roles of one operation, and the
// harness reads them back TWICE - once through ctjs::EscapeEffectOpInterface's
// getEffects directly (the ODS decorators, resource names and all) and once
// through operandRole (the path the verdict takes). A row can therefore only
// be wrong by name: if a decorator moves to the wrong operand, the interface
// check fails before the verdict does; if the C++ default rule or kind switch
// drifts, the operandRole check fails while the interface still agrees.
//
// Two rows exist to be broken on purpose, and were (see the commit): the
// return/throw rows go red if a sink is moved into visitOperation, because
// the sparse framework never visits a result-less operation; the DEFAULT-RULE
// row goes red if an unannotated operation is made NEITHER.
//
// Part 23 §1.4: a TEST, exempt from the ODS-first ratio - there is no TableGen
// way to assert that `return %s` and `return %p` differ.
#include "ctcompile/CTNative/Analysis/EscapeAnalysis.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSEscapeEffects.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Parser/Parser.h"

#include <cstdio>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace ctcompile::ctnative;
namespace ctjs = ctcompile::ctjs;

int failures = 0;

// One row. `body` OWNS its text (TypeInference.cpp says why: a `const char *`
// into a concatenation temporary dangles) and INCLUDES its terminator, since
// several rows return or throw the site itself.
struct row {
    const char * what;
    std::string body;
    // The verdict of the operation marked `check` - "confined" or
    // "escapes:<reason>" - or, when `alias` is set, the printed alias set of
    // its result.
    const char * expected;
    // "<op> <role> <role> ..." - the role of EVERY operand of the first
    // operation of that name, in ODS argument order, each `neither`, `carry`
    // or `sink:<reason>`. A leading `~` says the operation must NOT implement
    // the interface (the kind switch, a branch, the default rule).
    const char * roles = nullptr;
    // For the operation named in `roles`: "" = not a boxed site, else its
    // reason.
    const char * boxed = nullptr;
    unsigned unvisitedSites = 0;
    unsigned unvisitedOperands = 0;
    unsigned deadSites = 0;
    bool capturesAllArguments = false;
    const char * wholeFunction = nullptr;
    bool alias = false;
    // false: the solver runs WITHOUT EscapeAnalysis, so every lattice is
    // missing and the post-pass has to account for a live site it never saw.
    bool withAnalysis = true;
};

// The header every row shares - TypeInference.cpp's: three implicit arguments
// then two parameters, all of which alias an EXTERNAL object, which is what
// makes a parameter's role observable: sinking %p sinks no site.
constexpr const char * kPrologue =
    "ctjs.func @f(%receiver: !ctjs.value, %new_target: !ctjs.value, "
    "%callee: !ctjs.value, %p: !ctjs.value, %q: !ctjs.value) -> !ctjs.value "
    "attributes {upvalue_count = 0 : i32} {\n";

std::string verdictString(const EscapeVerdicts & verdicts, mlir::Operation * site) {
    auto found = verdicts.sites.find(site);
    if (found == verdicts.sites.end()) { return "<no verdict>"; }
    if (found->second.reason == EscapeReason::Confined) { return "confined"; }
    return "escapes:" + stringifyEscapeReason(found->second.reason).str();
}

std::string roleString(const RoleOf & role) {
    switch (role.role) {
    case OperandRole::Neither: return "neither";
    case OperandRole::Carry: return "carry";
    case OperandRole::Sink: return "sink:" + stringifyEscapeReason(role.reason).str();
    }
    return "?";
}

// THE INDEPENDENT PATH: the ODS decorators as the generated interface reports
// them, spelled from the RESOURCE'S NAME rather than from the enum, so a
// decorator on the wrong operand or a route with the wrong name fails here
// even if operandRole happened to agree with the row.
std::string roleThroughInterface(ctjs::EscapeEffectOpInterface roles, unsigned index) {
    llvm::SmallVector<mlir::SideEffects::EffectInstance<ctjs::EscapeEffects::Effect>, 6> effects;
    roles.getEffects(effects);
    for (const auto & effect : effects) {
        mlir::OpOperand * on = effect.getEffectValue<mlir::OpOperand *>();
        if (on == nullptr || on->getOperandNumber() != index) { continue; }
        if (llvm::isa<ctjs::EscapeEffects::Sink>(effect.getEffect())) {
            return "sink:" + effect.getResource()->getName().str();
        }
        if (llvm::isa<ctjs::EscapeEffects::Carry>(effect.getEffect())) { return "carry"; }
    }
    return "neither";
}

void fail(const row & r, const std::string & message) {
    std::printf("FAIL %s\n  %s\n", r.what, message.c_str());
    ++failures;
}

void checkRoles(const row & r, mlir::ModuleOp module) {
    std::istringstream in{r.roles};
    std::string opName;
    in >> opName;
    const bool mustLackInterface = !opName.empty() && opName[0] == '~';
    if (mustLackInterface) { opName.erase(0, 1); }
    std::vector<std::string> expected;
    for (std::string token; in >> token;) { expected.push_back(token); }

    mlir::Operation * target = nullptr;
    module->walk([&](mlir::Operation * op) {
        if (target == nullptr && op->getName().getStringRef() == opName) { target = op; }
    });
    if (target == nullptr) {
        fail(r, "no operation named " + opName + " in the row");
        return;
    }
    if (target->getNumOperands() != expected.size()) {
        fail(r, opName + " has " + std::to_string(target->getNumOperands()) +
                    " operands but the row names " + std::to_string(expected.size()));
        return;
    }

    auto roles = llvm::dyn_cast<ctjs::EscapeEffectOpInterface>(target);
    if (mustLackInterface && roles) {
        fail(r, opName + " implements EscapeEffectOpInterface, and the row says it must not");
    }
    if (!mustLackInterface && !roles) {
        fail(r, opName + " does not implement EscapeEffectOpInterface - is its decorator missing?");
    }
    for (unsigned i = 0; i < expected.size(); ++i) {
        const std::string viaAnalysis = roleString(operandRole(target, i));
        if (viaAnalysis != expected[i]) {
            fail(r, opName + " operand " + std::to_string(i) + ": operandRole says " + viaAnalysis +
                        ", the row says " + expected[i]);
        }
        if (roles) {
            const std::string viaInterface = roleThroughInterface(roles, i);
            if (viaInterface != expected[i]) {
                fail(r, opName + " operand " + std::to_string(i) + ": the ODS interface says " +
                            viaInterface + ", the row says " + expected[i]);
            }
        }
    }

    if (r.boxed != nullptr) {
        EscapeReason reason = EscapeReason::Confined;
        const bool boxed = isBoxedSite(target, reason);
        const std::string got = boxed ? stringifyEscapeReason(reason).str() : "";
        if (got != r.boxed) {
            fail(r,
                 opName + ": isBoxedSite says \"" + got + "\", the row says \"" + r.boxed + "\"");
        }
    }
}

void check(mlir::MLIRContext & context, const row & r) {
    const std::string text = std::string{kPrologue} + r.body + "}\n";
    mlir::OwningOpRef<mlir::ModuleOp> module =
        mlir::parseSourceString<mlir::ModuleOp>(text, &context);
    if (!module) {
        fail(r, "the module did not parse:\n" + text);
        return;
    }

    // DeadCodeAnalysis AND SparseConstantPropagation, neither optional - see
    // TypeInference.h and TypeClaims.cpp for the trap each one closes.
    mlir::DataFlowSolver solver;
    solver.load<mlir::dataflow::DeadCodeAnalysis>();
    solver.load<mlir::dataflow::SparseConstantPropagation>();
    if (r.withAnalysis) { solver.load<EscapeAnalysis>(); }
    if (failed(solver.initializeAndRun(module->getOperation()))) {
        fail(r, "the solver did not converge");
        return;
    }

    ctjs::FuncOp function;
    module->walk([&](ctjs::FuncOp fn) { function = fn; });
    mlir::Operation * marked = nullptr;
    module->walk([&](mlir::Operation * op) {
        if (op->hasAttr("check")) { marked = op; }
    });
    if (!function || marked == nullptr) {
        fail(r, "no ctjs.func or no operation carried `check`");
        return;
    }

    const EscapeVerdicts verdicts = computeVerdicts(solver, function);

    std::string got;
    if (r.alias) {
        // The LAST result: ctjs.catch_land's first is an i32 pad id.
        const AliasLattice * lattice =
            marked->getNumResults() != 0
                ? solver.lookupState<AliasLattice>(marked->getResult(marked->getNumResults() - 1))
                : nullptr;
        if (lattice == nullptr) {
            got = "<no lattice>";
        } else {
            llvm::raw_string_ostream os{got};
            lattice->getValue().print(os);
        }
    } else {
        got = verdictString(verdicts, marked);
    }
    if (got != r.expected) { fail(r, std::string{"expected "} + r.expected + ", got " + got); }

    // THE COUNTERS, every one of them: a counter nobody asserts is a counter
    // that can silently stop counting.
    const auto expectCount = [&](const char * name, unsigned expected, unsigned actual) {
        if (expected != actual) {
            fail(r, std::string{name} + ": expected " + std::to_string(expected) + ", got " +
                        std::to_string(actual));
        }
    };
    expectCount("unvisitedSites", r.unvisitedSites, verdicts.unvisitedSites);
    expectCount("unvisitedOperands", r.unvisitedOperands, verdicts.unvisitedOperands);
    expectCount("deadSites", r.deadSites, verdicts.deadSites);
    expectCount("capturesAllArguments", r.capturesAllArguments ? 1 : 0,
                verdicts.capturesAllArguments ? 1 : 0);
    const std::string whole =
        verdicts.wholeFunction ? stringifyEscapeReason(*verdicts.wholeFunction).str() : "";
    if (whole != (r.wholeFunction != nullptr ? r.wholeFunction : "")) {
        fail(r, "wholeFunction: expected \"" + std::string{r.wholeFunction ? r.wholeFunction : ""} +
                    "\", got \"" + whole + "\"");
    }
    if (verdicts.liveBlocks == 0 || verdicts.liveBlocks > verdicts.blocks) {
        fail(r, "block accounting: " + std::to_string(verdicts.liveBlocks) + " of " +
                    std::to_string(verdicts.blocks) + " live");
    }

    // THE CLOSURE HOLE, CHECKED ON EVERY ROW: no alias set anywhere in the
    // module may hold anything but a create_object or a create_array. The
    // analysis asserts it in debug builds; this is the release-build witness.
    const auto checkValue = [&](mlir::Value value) {
        const AliasLattice * lattice = solver.lookupState<AliasLattice>(value);
        if (lattice != nullptr && !lattice->getValue().onlyTrackedSites()) {
            fail(r, "an alias set holds an operation that is not a tracked site");
        }
    };
    module->walk([&](mlir::Operation * op) {
        for (mlir::Value result : op->getResults()) { checkValue(result); }
        for (mlir::Region & region : op->getRegions()) {
            for (mlir::Block & block : region) {
                for (mlir::BlockArgument argument : block.getArguments()) { checkValue(argument); }
            }
        }
    });

    if (r.roles != nullptr) { checkRoles(r, *module); }
}

} // namespace

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    context.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    context.getOrLoadDialect<CTNativeDialect>();
    // For the DEFAULT-RULE rows: an operation from no dialect at all has no
    // interface and is not a branch, which is exactly the case the rule is for.
    context.allowUnregisteredDialects();

    const std::string S = "  %s = ctjs.create_object {check}\n";
    const std::string A = "  %s = ctjs.create_array [] {check}\n";
    const std::string R = "  ctjs.return %p\n";

    const std::vector<row> rows = {
        // ===================================================================
        // THE SITES
        // ===================================================================
        {.what = "an object nothing touches is confined", .body = S + R, .expected = "confined"},
        {.what = "an array nothing touches is confined", .body = A + R, .expected = "confined"},
        {.what = "create_object is a tracked site, not a boxed one",
         .body = S + R,
         .expected = "confined",
         .roles = "~ctjs.create_object",
         .boxed = ""},
        {.what = "create_array's elements SINK(stored): items traced o.cpp:42",
         .body = S + "  %a = ctjs.create_array [%s]\n" + R,
         .expected = "escapes:stored",
         .roles = "ctjs.create_array sink:stored",
         .boxed = ""},

        // ===================================================================
        // ALLOCATION - the NEITHER/CARRY positives and the SINK negatives
        // ===================================================================
        {.what = "append: $array NEITHER (o.cpp:161-163 is push_back only)",
         .body = A + "  ctjs.append %p to %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.append neither sink:stored"},
        {.what = "append: $element SINK(stored)",
         .body = S + "  %a = ctjs.create_array []\n  ctjs.append %s to %a\n" + R,
         .expected = "escapes:stored"},
        {.what = "create_cell: $initial SINK(stored), slot traced o.cpp:59; result phase59",
         .body = S + "  %c = ctjs.create_cell %s\n" + R,
         .expected = "escapes:stored",
         .roles = "ctjs.create_cell sink:stored",
         .boxed = "phase59"},
        {.what = "create_closure: $enclosing_closure NEITHER (c.cpp:881, 910-911 read only)",
         .body = S + "  %f = ctjs.create_closure %s[0] this %p captures %q\n" + R,
         .expected = "confined",
         .roles = "ctjs.create_closure neither sink:captured sink:captured",
         .boxed = "phase59"},
        {.what = "create_closure: $enclosing_this SINK(captured), c.cpp:919",
         .body = S + "  %f = ctjs.create_closure %callee[0] this %s\n" + R,
         .expected = "escapes:captured"},
        {.what = "create_closure: $upvalues SINK(captured), c.cpp:909",
         .body = S + "  %f = ctjs.create_closure %callee[0] this %p captures %s\n" + R,
         .expected = "escapes:captured"},
        {.what = "own_keys: $source NEITHER (o.cpp:204-219, no call); result runtime_array",
         .body = S + "  %k = ctjs.own_keys of %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.own_keys neither",
         .boxed = "runtime_array"},
        {.what = "iterable: $source CARRY - the site stays confined through it",
         .body = S + "  %i = ctjs.iterable of %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.iterable carry",
         .boxed = "runtime_array"},
        {.what = "iterable CARRIES: returning the iterable returns the site (c.cpp:555)",
         .body = S + "  %i = ctjs.iterable of %s\n  ctjs.return %i\n",
         .expected = "escapes:returned"},
        {.what = "make_arguments is a boxed site, reason arguments",
         .body = "  %a = ctjs.make_arguments\n" + S + R,
         .expected = "confined",
         .roles = "ctjs.make_arguments",
         .boxed = "arguments",
         .capturesAllArguments = true},
        {.what = "gather_rest is a boxed site, reason arguments",
         .body = "  %a = ctjs.gather_rest from 1\n" + S + R,
         .expected = "confined",
         .roles = "ctjs.gather_rest",
         .boxed = "arguments",
         .capturesAllArguments = true},
        {.what = "create_regexp is a boxed site, reason not_tracked",
         .body = S + "  %re = ctjs.create_regexp \"a\", \"g\"\n" + R,
         .expected = "confined",
         .roles = "ctjs.create_regexp",
         .boxed = "not_tracked"},

        // ===================================================================
        // STORES AND PROPERTY ACCESS
        // ===================================================================
        {.what = "store_global SINK(stored_global): globals_ is the first root (o.cpp:667)",
         .body = S + "  ctjs.store_global \"g\", %s\n" + R,
         .expected = "escapes:stored_global",
         .roles = "ctjs.store_global sink:stored_global"},
        {.what = "load_global's result is external, not a site",
         .body = "  %g = ctjs.load_global \"x\" {check}\n" + R,
         .expected = "{external}",
         .alias = true},
        {.what = "set_property: $object NEITHER, the star proof (o.cpp:397-419 walks null)",
         .body = S + "  ctjs.set_property %s[%p], %q\n" + R,
         .expected = "confined",
         .roles = "ctjs.set_property neither sink:converted sink:stored"},
        {.what = "set_property: $key SINK(converted), to_string o.cpp:193 -> coerce.cpp:171-179",
         .body = S + "  ctjs.set_property %p[%s], %q\n" + R,
         .expected = "escapes:converted"},
        {.what = "set_property: $value SINK(stored), value.hpp:632-639",
         .body = S + "  ctjs.set_property %p[%q], %s\n" + R,
         .expected = "escapes:stored"},
        {.what = "get_property: $object NEITHER (o.cpp:445-464, data-only find on the table)",
         .body = S + "  %r = ctjs.get_property %s[%p]\n" + R,
         .expected = "confined",
         .roles = "ctjs.get_property neither sink:converted"},
        {.what = "get_property: $key SINK(converted), o.cpp:154",
         .body = S + "  %r = ctjs.get_property %p[%s]\n" + R,
         .expected = "escapes:converted"},
        {.what = "a property READ off a site is external - contents are not tracked",
         .body = "  %o = ctjs.create_object\n  %r = ctjs.get_property %o[%p] {check}\n" + R,
         .expected = "{external}",
         .alias = true},
        {.what = "delete_property: $object NEITHER (o.cpp:325-329 erase)",
         .body = S + "  ctjs.delete_property %s[%p]\n" + R,
         .expected = "confined",
         .roles = "ctjs.delete_property neither sink:converted"},
        {.what = "delete_property: $key SINK(converted), o.cpp:327",
         .body = S + "  ctjs.delete_property %p[%s]\n" + R,
         .expected = "escapes:converted"},
        {.what = "delete_named: $object NEITHER (o.cpp:200-202)",
         .body = S + "  ctjs.delete_named \"k\" from %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.delete_named neither"},
        {.what = "has_property: $object NEITHER (o.cpp:266-278, no call)",
         .body = S + "  %h = ctjs.has_property %p in %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.has_property neither sink:converted"},
        {.what = "has_property: $key SINK(converted), o.cpp:263/265",
         .body = S + "  %h = ctjs.has_property %s in %p\n" + R,
         .expected = "escapes:converted"},
        {.what = "get_proto NEITHER (o.cpp:245-248 field read)",
         .body = S + "  %g = ctjs.get_proto %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.get_proto neither"},
        {.what = "set_proto: $object SINK(proto_mutated) - the chain can now reach a setter",
         .body = S + "  ctjs.set_proto %p on %s\n" + R,
         .expected = "escapes:proto_mutated",
         .roles = "ctjs.set_proto sink:proto_mutated sink:stored"},
        {.what = "set_proto: $proto SINK(stored), traced o.cpp:56",
         .body = S + "  ctjs.set_proto %s on %p\n" + R,
         .expected = "escapes:stored"},
        {.what = "define_accessor: $target SINK(accessor_defined), o.cpp:221-228 then 452-457",
         .body = S + "  ctjs.define_accessor \"k\" on %s get %p set %q\n" + R,
         .expected = "escapes:accessor_defined",
         .roles = "ctjs.define_accessor sink:accessor_defined sink:stored sink:stored"},
        {.what = "define_accessor: $getter SINK(stored), traced o.cpp:52-55",
         .body = S + "  ctjs.define_accessor \"k\" on %p get %s set %q\n" + R,
         .expected = "escapes:stored"},
        {.what = "define_accessor: $setter SINK(stored)",
         .body = S + "  ctjs.define_accessor \"k\" on %p get %q set %s\n" + R,
         .expected = "escapes:stored"},
        {.what = "copy_props: $target NEITHER (o.cpp:230-243 raw set, no accessor)",
         .body = S + "  ctjs.copy_props %p into %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.copy_props neither neither"},
        {.what = "copy_props: $source NEITHER - its contents were sunk when stored",
         .body = S + "  ctjs.copy_props %s into %p\n" + R,
         .expected = "confined"},
        {.what = "load_upvalue: $closure NEITHER (cell read, rows 0,0,0)",
         .body = S + "  %u = ctjs.load_upvalue %s[0]\n" + R,
         .expected = "confined",
         .roles = "ctjs.load_upvalue neither"},
        {.what = "store_upvalue: $closure NEITHER (rl.cpp:1240-1249 writes a slot)",
         .body = S + "  ctjs.store_upvalue %s[0], %p\n" + R,
         .expected = "confined",
         .roles = "ctjs.store_upvalue neither sink:stored"},
        {.what = "store_upvalue: $value SINK(stored), traced o.cpp:59 via 64",
         .body = S + "  ctjs.store_upvalue %p[0], %s\n" + R,
         .expected = "escapes:stored"},
        {.what = "cell_get NEITHER (rl.cpp:1210-1214)",
         .body = S + "  %c = ctjs.cell_get %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.cell_get neither"},
        {.what = "cell_set: $cell NEITHER",
         .body = S + "  ctjs.cell_set %s, %p\n" + R,
         .expected = "confined",
         .roles = "ctjs.cell_set neither sink:stored"},
        {.what = "cell_set: $value SINK(stored), slot traced o.cpp:59",
         .body = S + "  ctjs.cell_set %p, %s\n" + R,
         .expected = "escapes:stored"},

        // ===================================================================
        // OPERATORS - binary, binary_static, and the kind switch both ways
        // ===================================================================
        {.what = "binary: $lhs SINK(converted) - ToPrimitive runs valueOf with it as receiver",
         .body = S + "  %b = ctjs.binary add %s, %p\n" + R,
         .expected = "escapes:converted",
         .roles = "ctjs.binary sink:converted sink:converted"},
        {.what = "binary: $rhs SINK(converted)",
         .body = S + "  %b = ctjs.binary add %p, %s\n" + R,
         .expected = "escapes:converted"},
        {.what = "binary_static: $lhs NEITHER - static to_number, coerce.cpp:285-299",
         .body = S + "  %b = ctjs.binary_static add %s, %p\n" + R,
         .expected = "confined",
         .roles = "ctjs.binary_static neither neither"},
        {.what = "binary_static: $rhs NEITHER",
         .body = S + "  %b = ctjs.binary_static bitor %p, %s\n" + R,
         .expected = "confined"},
        // The kind switch: three operations, no annotation, C++ decides.
        {.what = "unary not NEITHER (total)",
         .body = S + "  %u = ctjs.unary not %s\n" + R,
         .expected = "confined",
         .roles = "~ctjs.unary neither"},
        {.what = "unary typeof NEITHER",
         .body = S + "  %u = ctjs.unary typeof %s\n" + R,
         .expected = "confined"},
        {.what = "unary void NEITHER",
         .body = S + "  %u = ctjs.unary void %s\n" + R,
         .expected = "confined"},
        {.what = "unary neg SINK(converted), def:337-340 negate may_reenter 1",
         .body = S + "  %u = ctjs.unary neg %s\n" + R,
         .expected = "escapes:converted",
         .roles = "~ctjs.unary sink:converted"},
        {.what = "unary plus SINK(converted), def:354-356 to_number may_reenter 1",
         .body = S + "  %u = ctjs.unary plus %s\n" + R,
         .expected = "escapes:converted"},
        {.what = "unary bitnot SINK(converted)",
         .body = S + "  %u = ctjs.unary bitnot %s\n" + R,
         .expected = "escapes:converted"},
        {.what = "compare strict_eq NEITHER on the lhs (value.hpp:293-311)",
         .body = S + "  %c = ctjs.compare strict_eq %s, %p\n" + R,
         .expected = "confined",
         .roles = "~ctjs.compare neither neither"},
        {.what = "compare strict_eq NEITHER on the rhs",
         .body = S + "  %c = ctjs.compare strict_eq %p, %s\n" + R,
         .expected = "confined"},
        {.what = "compare eq SINK(converted), def:394-396 loose_equal may_reenter 1",
         .body = S + "  %c = ctjs.compare eq %s, %p\n" + R,
         .expected = "escapes:converted",
         .roles = "~ctjs.compare sink:converted sink:converted"},
        {.what = "compare lt SINK(converted) on the rhs, def:411 less",
         .body = S + "  %c = ctjs.compare lt %p, %s\n" + R,
         .expected = "escapes:converted"},
        {.what = "compare le SINK(converted)",
         .body = S + "  %c = ctjs.compare le %s, %p\n" + R,
         .expected = "escapes:converted"},
        {.what = "compare gt SINK(converted)",
         .body = S + "  %c = ctjs.compare gt %s, %p\n" + R,
         .expected = "escapes:converted"},
        {.what = "compare ge SINK(converted)",
         .body = S + "  %c = ctjs.compare ge %s, %p\n" + R,
         .expected = "escapes:converted"},
        {.what = "convert to_boolean NEITHER (total)",
         .body = S + "  %c = ctjs.convert to_boolean %s\n" + R,
         .expected = "confined",
         .roles = "~ctjs.convert neither"},
        {.what = "convert to_number SINK(converted)",
         .body = S + "  %c = ctjs.convert to_number %s\n" + R,
         .expected = "escapes:converted",
         .roles = "~ctjs.convert sink:converted"},
        {.what = "convert to_string SINK(converted)",
         .body = S + "  %c = ctjs.convert to_string %s\n" + R,
         .expected = "escapes:converted"},
        {.what = "convert to_primitive SINK(converted)",
         .body = S + "  %c = ctjs.convert to_primitive %s\n" + R,
         .expected = "escapes:converted"},
        {.what = "convert to_property_key SINK(converted)",
         .body = S + "  %c = ctjs.convert to_property_key %s\n" + R,
         .expected = "escapes:converted"},
        {.what = "convert to_object SINK(converted) in the MVP (a carry in principle)",
         .body = S + "  %c = ctjs.convert to_object %s\n" + R,
         .expected = "escapes:converted"},
        {.what = "truthy NEITHER, and its i1 result is not an object",
         .body = S + "  %t = ctjs.truthy %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.truthy neither"},
        {.what = "instanceof: $object NEITHER (o.cpp:282-323 walks fields, def:451-452)",
         .body = S + "  %i = ctjs.instanceof %s, %p\n" + R,
         .expected = "confined",
         .roles = "ctjs.instanceof neither neither"},
        {.what = "instanceof: $constructor NEITHER - ensure_prototype stores on the ctor only",
         .body = S + "  %i = ctjs.instanceof %p, %s\n" + R,
         .expected = "confined"},

        // ===================================================================
        // CALLS - every position a sink, except the two spread arrays
        // ===================================================================
        {.what = "call: $callee SINK(passed)",
         .body = S + "  %r = ctjs.call %s(%p)\n" + R,
         .expected = "escapes:passed",
         .roles = "ctjs.call sink:passed sink:passed"},
        {.what = "call: $receiver SINK(passed) - call_frame::receiver is a root (o.cpp:683)",
         .body = S + "  %r = ctjs.call %p(%s)\n" + R,
         .expected = "escapes:passed"},
        {.what = "call: $args SINK(passed) - copied into the callee window (c.cpp:82-92)",
         .body = S + "  %r = ctjs.call %p(%q, %s)\n" + R,
         .expected = "escapes:passed",
         .roles = "ctjs.call sink:passed sink:passed sink:passed"},
        {.what = "a call's result is external",
         .body = "  %r = ctjs.call %p(%q) {check}\n" + R,
         .expected = "{external}",
         .alias = true},
        // PHASE 62½-A: the resolved form carries ctjs.call's row, all five
        // positions, and the symbol changes none of them. The callee is @f
        // itself - the only ctjs.func the harness's module has - so the
        // symbol-use verifier sees five operands against five block arguments.
        {.what = "call_direct: $receiver SINK(passed), like call",
         .body = S + "  %r = ctjs.call_direct @f(%s, %p, %q, %p, %q)\n" + R,
         .expected = "escapes:passed",
         .roles = "ctjs.call_direct sink:passed sink:passed sink:passed sink:passed sink:passed"},
        {.what = "call_direct: $new_target SINK(passed)",
         .body = S + "  %r = ctjs.call_direct @f(%p, %s, %q, %p, %q)\n" + R,
         .expected = "escapes:passed"},
        {.what = "call_direct: $callee_value SINK(passed)",
         .body = S + "  %r = ctjs.call_direct @f(%p, %q, %s, %p, %q)\n" + R,
         .expected = "escapes:passed"},
        {.what = "call_direct: $args SINK(passed) - the callee window (c.cpp:82-92)",
         .body = S + "  %r = ctjs.call_direct @f(%p, %q, %p, %q, %s)\n" + R,
         .expected = "escapes:passed"},
        {.what = "a call_direct's result is external",
         .body = "  %r = ctjs.call_direct @f(%p, %q, %p, %q, %p) {check}\n" + R,
         .expected = "{external}",
         .alias = true},
        {.what = "construct: $callee SINK(passed)",
         .body = S + "  %r = ctjs.construct %s(%p)\n" + R,
         .expected = "escapes:passed",
         .roles = "ctjs.construct sink:passed sink:passed",
         .boxed = ""},
        {.what = "construct: $new_target SINK(passed), root o.cpp:686",
         .body = S + "  %r = ctjs.construct %p(%s)\n" + R,
         .expected = "escapes:passed"},
        {.what = "construct: $args SINK(passed)",
         .body = S + "  %r = ctjs.construct %p(%q, %s)\n" + R,
         .expected = "escapes:passed",
         .roles = "ctjs.construct sink:passed sink:passed sink:passed"},
        {.what = "call_spread: $callee SINK(passed)",
         .body = S + "  %r = ctjs.call_spread %s(%p, %q)\n" + R,
         .expected = "escapes:passed",
         .roles = "ctjs.call_spread sink:passed neither sink:passed"},
        {.what = "call_spread: $receiver SINK(passed)",
         .body = S + "  %r = ctjs.call_spread %p(%s, %q)\n" + R,
         .expected = "escapes:passed"},
        {.what = "call_spread: $args NEITHER - spread_arguments copies the items (c.cpp:711-717)",
         .body = A + "  %r = ctjs.call_spread %p(%q, %s)\n" + R,
         .expected = "confined"},
        {.what = "construct_spread: $callee SINK(passed)",
         .body = S + "  %r = ctjs.construct_spread %s(%p)\n" + R,
         .expected = "escapes:passed",
         .roles = "ctjs.construct_spread sink:passed neither"},
        {.what = "construct_spread: $args NEITHER (c.cpp:734-741)",
         .body = A + "  %r = ctjs.construct_spread %p(%s)\n" + R,
         .expected = "confined"},
        {.what = "dynamic_import SINK(converted) - the specifier's toString is user code",
         .body = S + "  %m = ctjs.dynamic_import %s\n" + R,
         .expected = "escapes:converted",
         .roles = "ctjs.dynamic_import sink:converted"},

        // ===================================================================
        // CONTROL FLOW AND COMPLETION - the post-pass rows
        // ===================================================================
        {.what = "return SINK(returned) - the result-less terminator the post-pass exists for",
         .body = S + "  ctjs.return %s\n",
         .expected = "escapes:returned",
         .roles = "ctjs.return sink:returned"},
        {.what = "returning a parameter sinks no site",
         .body = S + R,
         .expected = "confined",
         .roles = "ctjs.return sink:returned"},
        {.what = "throw SINK(thrown) - thrown_ is a root (o.cpp:705)",
         .body = S + "  ctjs.throw %s\n",
         .expected = "escapes:thrown",
         .roles = "ctjs.throw sink:thrown"},
        {.what = "wrap_promise SINK(stored) - the promise's __value (internal.hpp:262, 342)",
         .body = S + "  %w = ctjs.wrap_promise %s\n" + R,
         .expected = "escapes:stored",
         .roles = "ctjs.wrap_promise sink:stored"},
        {.what = "module_export_cell: $current SINK(stored) - alias-returning, c.cpp:240",
         .body = S + "  %e = ctjs.module_export_cell \"n\" adopting %s\n" + R,
         .expected = "escapes:stored",
         .roles = "ctjs.module_export_cell sink:stored"},
        {.what = "root NEITHER - parks into this frame's own window",
         .body = S +
                 "  %ctx = ctjs.frame_enter 4\n  ctjs.root %s in %ctx\n  ctjs.frame_exit %ctx\n" +
                 R,
         .expected = "confined",
         .roles = "ctjs.root neither neither"},
        {.what = "frame_exit has no tracked operand",
         .body = S + "  %ctx = ctjs.frame_enter 4\n  ctjs.frame_exit %ctx\n" + R,
         .expected = "confined",
         .roles = "ctjs.frame_exit neither"},
        {.what = "catch_land's thrown value is external",
         .body = S +
                 "  ctjs.push_handler ^body catch ^pad\n"
                 "^body:\n  ctjs.pop_handler\n" +
                 R +
                 "^pad:\n  %id, %e = ctjs.catch_land {check}\n"
                 "  ctjs.return %e\n",
         .expected = "{external}",
         .alias = true},

        // --- THE LOOP ROW: a site carried round a back edge through block
        // arguments stays confined; the same site returned from inside the
        // loop's exit is returned. Both depend on DeadCodeAnalysis and
        // SparseConstantPropagation being loaded, as TypeClaims.cpp records.
        {.what = "a site carried round a loop through block arguments stays confined",
         .body = S +
                 "  cf.br ^loop(%s : !ctjs.value)\n"
                 "^loop(%x: !ctjs.value):\n"
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^loop(%x : !ctjs.value), ^exit\n"
                 "^exit:\n" +
                 R,
         .expected = "confined",
         .roles = "~cf.br carry"},
        {.what = "the same site returned through the loop's block argument is returned",
         .body = S + "  cf.br ^loop(%s : !ctjs.value)\n"
                     "^loop(%x: !ctjs.value):\n"
                     "  %t = ctjs.truthy %p\n"
                     "  cf.cond_br %t, ^loop(%x : !ctjs.value), ^exit\n"
                     "^exit:\n"
                     "  ctjs.return %x\n",
         .expected = "escapes:returned"},
        {.what = "a block argument joins two sites: sinking it sinks both",
         .body = "  %o = ctjs.create_object\n" + S +
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^join(%s : !ctjs.value), ^join(%o : !ctjs.value)\n"
                 "^join(%x: !ctjs.value):\n"
                 "  ctjs.store_global \"g\", %x\n" +
                 R,
         .expected = "escapes:stored_global"},

        // --- THE ctjs.check HANDLER-OPERAND ROW: the handler edge carries a
        // register snapshot into the pad's block arguments, and a site in it
        // returned from the pad is returned.
        // push_handler must pass the pad's argument too (the verifier checks
        // arity); it passes a PARAMETER, so only ctjs.check's edge carries the
        // site - which is what the row isolates.
        {.what = "ctjs.check's handler operands carry into the pad (BranchOpInterface)",
         .body = S +
                 "  ctjs.push_handler ^body catch ^pad(%p : !ctjs.value)\n"
                 "^body:\n"
                 "  %r = ctjs.call %p(%q)\n"
                 "  ctjs.check ^cont caught ^pad(%s : !ctjs.value)\n"
                 "^cont:\n"
                 "  ctjs.pop_handler\n" +
                 R +
                 "^pad(%e: !ctjs.value):\n"
                 "  %id, %thrown = ctjs.catch_land\n"
                 "  ctjs.return %e\n",
         .expected = "escapes:returned",
         .roles = "~ctjs.check carry"},
        {.what = "push_handler's body operands carry too",
         .body = S +
                 "  ctjs.push_handler ^body(%s : !ctjs.value) catch ^pad\n"
                 "^body(%b: !ctjs.value):\n"
                 "  ctjs.pop_handler\n"
                 "  ctjs.return %b\n"
                 "^pad:\n"
                 "  %id, %thrown = ctjs.catch_land\n" +
                 R,
         .expected = "escapes:returned",
         .roles = "~ctjs.push_handler carry"},

        // ===================================================================
        // THE ACCOUNTING - dead blocks, unvisited sites, unvisited operands
        // ===================================================================
        {.what = "a site in a dead block is dropped and counted, never claimed",
         .body = S + R + "^dead:\n  %d = ctjs.create_object\n  ctjs.return %d\n",
         .expected = "confined",
         .deadSites = 1},
        {.what = "a sink in a dead block does not fire",
         .body = S + R + "^dead:\n  ctjs.store_global \"g\", %s\n" + R,
         .expected = "confined"},
        {.what = "a site in a LIVE block the solver never visited is unvisited, counted",
         .body = S + "  ctjs.resume_throw\n",
         .expected = "escapes:unvisited",
         .unvisitedSites = 1,
         .withAnalysis = false},
        {.what = "a sink operand with no lattice is counted and refuses every site",
         .body = S + R,
         .expected = "escapes:unvisited",
         .unvisitedSites = 1,
         .unvisitedOperands = 1,
         .withAnalysis = false},
        {.what = "an unvisited operand alone: every site becomes unvisited_operand",
         .body = "  %o = ctjs.create_object {check}\n"
                 "  ctjs.resume_throw\n"
                 "^dead:\n"
                 "  ctjs.return %p\n",
         .expected = "escapes:unvisited",
         .unvisitedSites = 1,
         .withAnalysis = false},

        // ===================================================================
        // R1 - arguments, per-site, with the placement guard
        // ===================================================================
        {.what = "make_arguments in the prologue: sites confined, function flagged",
         .body = "  %a = ctjs.make_arguments\n" + S + R,
         .expected = "confined",
         .capturesAllArguments = true},
        {.what = "make_arguments AFTER a site: the whole function is arguments_late",
         .body = S + "  %a = ctjs.make_arguments\n" + R,
         .expected = "escapes:arguments_late",
         .capturesAllArguments = true,
         .wholeFunction = "arguments_late"},
        {.what = "gather_rest after a site is arguments_late too",
         .body = S + "  %a = ctjs.gather_rest from 0\n" + R,
         .expected = "escapes:arguments_late",
         .capturesAllArguments = true,
         .wholeFunction = "arguments_late"},
        {.what = "make_arguments outside the entry block is arguments_late",
         .body = "  cf.br ^b\n^b:\n  %a = ctjs.make_arguments\n" + S + R,
         .expected = "escapes:arguments_late",
         .capturesAllArguments = true,
         .wholeFunction = "arguments_late"},
        {.what = "make_arguments in the prologue leaves a site in a later block confined",
         .body = "  %a = ctjs.make_arguments\n  cf.br ^b\n^b:\n" + S + R,
         .expected = "confined",
         .capturesAllArguments = true},

        // ===================================================================
        // R4 - suspend, whole function
        // ===================================================================
        {.what = "suspend refuses the whole function: a site it never touches escapes",
         .body = S + "  %r = ctjs.suspend await %p\n" + R,
         .expected = "escapes:suspended",
         .roles = "ctjs.suspend sink:stored",
         .wholeFunction = "suspended"},
        {.what = "suspend's own operand: the refusal is the first reason, not stored",
         .body = S + "  %r = ctjs.suspend yield %s\n" + R,
         .expected = "escapes:suspended",
         .wholeFunction = "suspended"},

        // ===================================================================
        // THE CLOSURE HOLE - no untracked allocation ever enters an alias set
        // ===================================================================
        {.what = "a closure is external, never a site (o.cpp:590 -> c.cpp:502)",
         .body = "  %f = ctjs.create_closure %callee[0] this %p {check}\n" + R,
         .expected = "{external}",
         .alias = true},
        {.what = "a closure carried through iterable stays external",
         .body = "  %f = ctjs.create_closure %callee[0] this %p\n"
                 "  %i = ctjs.iterable of %f {check}\n" +
                 R,
         .expected = "{external}",
         .alias = true},
        {.what = "a cell is external",
         .body = "  %c = ctjs.create_cell %p {check}\n" + R,
         .expected = "{external}",
         .alias = true},
        {.what = "own_keys' array is external",
         .body = "  %k = ctjs.own_keys of %p {check}\n" + R,
         .expected = "{external}",
         .alias = true},
        {.what = "a site carried through iterable is the site plus an external array",
         .body = "  %o = ctjs.create_object\n  %i = ctjs.iterable of %o {check}\n" + R,
         .expected = "{ctjs.create_object, external}",
         .alias = true},
        {.what = "a constant is not an object",
         .body = "  %z = ctjs.constant #ctjs.undefined {check}\n" + R,
         .expected = "{}",
         .alias = true},
        {.what = "a site's own alias set is itself",
         .body = S + R,
         .expected = "{ctjs.create_object}",
         .alias = true},

        // ===================================================================
        // THE DEFAULT RULE - an operation with no annotation sinks
        // ===================================================================
        {.what = "DEFAULT RULE: an unannotated operation sinks its value operand as unknown_op",
         .body = S + "  \"test.unknown\"(%s) : (!ctjs.value) -> ()\n" + R,
         .expected = "escapes:unknown_op",
         .roles = "~test.unknown sink:unknown_op"},
        {.what = "DEFAULT RULE: an unannotated operation's result is external",
         .body = "  %r = \"test.unknown\"(%p) {check} : (!ctjs.value) -> !ctjs.value\n" + R,
         .expected = "{external}",
         .alias = true},
        {.what = "DEFAULT RULE: a non-value operand is not sunk",
         .body = S + "  %t = ctjs.truthy %s\n  \"test.unknown\"(%t) : (i1) -> ()\n" + R,
         .expected = "confined",
         .roles = "~test.unknown neither"},
    };

    for (const row & r : rows) { check(context, r); }

    // allocationPc: the importer's NameLoc inside its FusedLoc, and nothing
    // else.
    {
        const std::string text =
            std::string{kPrologue} +
            "  %s = ctjs.create_object loc(fused[\"program:p:3:17\", \"f\":1:2])\n"
            "  %o = ctjs.create_object loc(\"program:p:3:9\")\n"
            "  %n = ctjs.create_object\n"
            "  ctjs.return %p\n}\n";
        mlir::OwningOpRef<mlir::ModuleOp> module =
            mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        std::vector<std::optional<unsigned>> pcs;
        if (module) {
            module->walk([&](ctjs::CreateObjectOp op) { pcs.push_back(allocationPc(op)); });
        }
        const std::vector<std::optional<unsigned>> expected = {17U, 9U, std::nullopt};
        if (!module || pcs != expected) {
            std::printf("FAIL allocationPc: expected 17, 9, none\n");
            ++failures;
        }
    }

    if (failures != 0) {
        std::printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("escape analysis: %zu rows, every cell agrees with the VM\n", rows.size());
    return 0;
}
