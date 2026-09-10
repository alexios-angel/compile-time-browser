// PHASE 55A'S TABLE, ONE CELL AT A TIME - the storage-evidence rows: direct
// storage targets, the all-write census, direct load evidence and the
// provenance closure; plus the live read-evidence mutation check.
//
// One of four executables carved out of a 2,763-line test/EscapeAnalysis.cpp on
// 2026-09-08. The row harness they share - `row`, `kPrologue`, `check`, the
// role and verdict printers - is EscapeAnalysisHarness.h beside this; the rows
// themselves are verbatim, in their original order, and every one is still
// run. Registered one target each in test/cmake/Analysis.cmake.

#include "EscapeAnalysisHarness.h"

using namespace ctcompile::test::escape;

namespace {

void checkReadEvidenceMutation(mlir::MLIRContext & context) {
    const row r{.what = "read candidates are recomputed from the live base after mutation",
                .body = "  %child = ctjs.create_object\n"
                        "  %a = ctjs.create_array [%child]\n"
                        "  %b = ctjs.create_array [%p]\n"
                        "  %read = ctjs.get_property %a[%q]\n"
                        "  ctjs.return %read\n",
                .expected = ""};
    auto module =
        mlir::parseSourceString<mlir::ModuleOp>(std::string{kPrologue} + r.body + "}\n", &context);
    if (!module) {
        fail(r, "the mutation fixture did not parse");
        return;
    }
    ctjs::FuncOp function;
    ctjs::GetPropertyOp read;
    llvm::SmallVector<ctjs::CreateArrayOp, 2> arrays;
    module->walk([&](ctjs::FuncOp op) { function = op; });
    module->walk([&](ctjs::GetPropertyOp op) { read = op; });
    module->walk([&](ctjs::CreateArrayOp op) { arrays.push_back(op); });
    const auto expect = [&](llvm::SmallVector<std::size_t, 2> indices, bool external) {
        mlir::DataFlowSolver solver;
        solver.load<mlir::dataflow::DeadCodeAnalysis>();
        solver.load<mlir::dataflow::SparseConstantPropagation>();
        solver.load<EscapeAnalysis>();
        if (failed(solver.initializeAndRun(module->getOperation()))) {
            fail(r, "the mutated solver did not converge");
            return;
        }
        const EscapeVerdicts verdicts = computeVerdicts(solver, function);
        if (!verdicts.directLoads.complete || verdicts.directLoads.reads.size() != 1 ||
            verdicts.directLoads.reads.front().candidateWrites != indices ||
            verdicts.directLoads.reads.front().base.isExternal() != external) {
            fail(r, "read evidence retained an obsolete target");
        }
        if (verdictString(verdicts, arrays.front().getElements().front().getDefiningOp()) !=
            "escapes:stored") {
            fail(r, "changing the read base weakened the stored child's verdict");
        }
        const AliasLattice * result = solver.lookupState<AliasLattice>(read.getResult());
        if (result == nullptr || result->getValue() != AliasValue::external()) {
            fail(r, "read evidence changed the load result lattice");
        }
        const LoadProvenanceEvidence provenance = computeLoadProvenance(solver, function, verdicts);
        mlir::Operation * child = arrays.front().getElements().front().getDefiningOp();
        const bool loadsChild = indices == llvm::SmallVector<std::size_t, 2>{0};
        if (!provenance.converged || !provenance.inputsComplete || provenance.reads.size() != 1 ||
            !provenance.reads.front().value.isExternal() ||
            llvm::is_contained(provenance.reads.front().value.getSites(), child) != loadsChild) {
            fail(r, "candidate contents retained an obsolete loaded child");
        }
        const bool returned = llvm::any_of(provenance.exposures, [&](const EscapeExposure & use) {
            return use.reason == EscapeReason::Returned &&
                   llvm::is_contained(use.value.getSites(), child);
        });
        if (returned != loadsChild) { fail(r, "candidate exposure retained an obsolete return"); }

        // Every prefix of this actual fixed-point computation must report
        // incomplete convergence, including cutoffs inside a site's joins.
        for (std::size_t budget = 0; budget < provenance.work; ++budget) {
            const LoadProvenanceEvidence partial =
                computeLoadProvenance(solver, function, verdicts, budget);
            if (partial.converged || partial.work != budget ||
                partial.reads.size() != provenance.reads.size() ||
                partial.writes.size() != provenance.writes.size() ||
                partial.exposures.size() != provenance.exposures.size()) {
                fail(r,
                     "an exhausted provenance prefix claimed convergence or lost census records");
                break;
            }
            const EscapeVerdicts after = computeVerdicts(solver, function);
            if (verdictString(after, child) != "escapes:stored" ||
                result->getValue() != AliasValue::external()) {
                fail(r, "an incomplete provenance query changed an original escape claim");
                break;
            }
        }
        const LoadProvenanceEvidence exact =
            computeLoadProvenance(solver, function, verdicts, provenance.work);
        if (!exact.converged || exact.work != provenance.work) {
            fail(r, "the exact provenance completion budget did not converge");
        }
    };
    expect({0}, false);
    read->setOperand(0, arrays.back().getResult());
    expect({1}, false);
    read->setOperand(0, function.getBody().front().getArgument(3));
    expect({}, true);
}

} // namespace

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    context.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    context.getOrLoadDialect<CTNativeDialect>();
    // For the DEFAULT-RULE rows: an operation from no dialect at all has no
    // interface and is not a branch, which is exactly the case the rule is for.
    context.allowUnregisteredDialects();

    const std::string S = "  %s = ctjs.create_object {check}\n";
    const std::string A = "  %s = ctjs.create_array [] {check}\n";
    const std::string R = "  ctjs.return %p\n";

    const std::vector<row> rows = {
        // DIRECT STORAGE TARGETS ARE DIAGNOSTICS, not contents proofs. The
        // original Stored verdict remains even when every target is confined.
        {.what = "a fixed property store identifies its confined object target",
         .body = S +
                 "  %outer = ctjs.create_object\n"
                 "  %key = ctjs.constant #ctjs.string<\"child\">\n"
                 "  ctjs.set_property %outer[%key], %s\n" +
                 R,
         .expected = "escapes:stored",
         .storageTarget = "{ctjs.create_object}",
         .storageTargetVerdicts = "confined"},
        {.what = "a returned target remains escaping despite its local allocation",
         .body = S + "  %outer = ctjs.create_array [%s]\n"
                     "  ctjs.return %outer\n",
         .expected = "escapes:stored",
         .storageTarget = "{ctjs.create_array}",
         .storageTargetVerdicts = "escapes:returned"},
        {.what = "a self-cycle keeps its Stored verdict when target and value share one site",
         .body = S + "  ctjs.set_property %s[%q], %s\n" + R,
         .expected = "escapes:stored",
         .storageTarget = "{ctjs.create_object}",
         .storageTargetVerdicts = "escapes:stored",
         .storageWrites = "ctjs.set_property[2] {ctjs.create_object} -> {ctjs.create_object}",
         .completeStorage = true},
        {.what = "a local target with an accessor does not prove direct field retention",
         .body = S +
                 "  %outer = ctjs.create_object\n"
                 "  ctjs.define_accessor \"child\" on %outer get %p set %q\n"
                 "  %key = ctjs.constant #ctjs.string<\"child\">\n"
                 "  ctjs.set_property %outer[%key], %s\n" +
                 R,
         .expected = "escapes:stored",
         .storageTarget = "{ctjs.create_object}",
         .storageTargetVerdicts = "escapes:accessor_defined"},
        {.what = "a primitive direct target is not an external target",
         .body = S +
                 "  %zero = ctjs.constant #ctjs.number<0>\n"
                 "  ctjs.set_property %zero[%q], %s\n" +
                 R,
         .expected = "escapes:stored",
         .storageTarget = "{}",
         .storageTargetVerdicts = ""},
        {.what = "an external alternative survives a storage target join",
         .body = S +
                 "  %outer = ctjs.create_object\n"
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^join(%outer : !ctjs.value), ^join(%p : !ctjs.value)\n"
                 "^join(%target: !ctjs.value):\n"
                 "  ctjs.set_property %target[%q], %s\n" +
                 R,
         .expected = "escapes:stored",
         .storageTarget = "{ctjs.create_object, external}",
         .storageTargetVerdicts = "confined",
         .storageWrites =
             "ctjs.set_property[2] {ctjs.create_object} -> {ctjs.create_object, external}",
         .completeStorage = true},
        {.what = "a local storage target join retains both confinement verdicts",
         .body = S +
                 "  %local = ctjs.create_object\n"
                 "  %published = ctjs.create_object\n"
                 "  ctjs.store_global \"g\", %published\n"
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^join(%local : !ctjs.value), ^join(%published : !ctjs.value)\n"
                 "^join(%target: !ctjs.value):\n"
                 "  ctjs.set_property %target[%q], %s\n" +
                 R,
         .expected = "escapes:stored",
         .storageTarget = "{ctjs.create_object, ctjs.create_object}",
         .storageTargetVerdicts = "confined,escapes:stored_global"},
        {.what = "a loop-carried storage target keeps its allocation identity",
         .body = S +
                 "  %outer = ctjs.create_array []\n"
                 "  cf.br ^loop(%outer : !ctjs.value)\n"
                 "^loop(%target: !ctjs.value):\n"
                 "  ctjs.append %s to %target\n"
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^loop(%target : !ctjs.value), ^exit\n"
                 "^exit:\n" +
                 R,
         .expected = "escapes:stored",
         .storageTarget = "{ctjs.create_array}",
         .storageTargetVerdicts = "confined",
         .storageWrites = "ctjs.append[1] {ctjs.create_object} -> {ctjs.create_array}",
         .completeStorage = true},
        {.what = "a property-loaded target stays external without contents tracking",
         .body = S +
                 "  %inner = ctjs.create_object\n"
                 "  %outer = ctjs.create_array [%inner]\n"
                 "  %zero = ctjs.constant #ctjs.number<0>\n"
                 "  %loaded = ctjs.get_property %outer[%zero]\n"
                 "  ctjs.set_property %loaded[%q], %s\n" +
                 R,
         .expected = "escapes:stored",
         .storageTarget = "{external}",
         .storageTargetVerdicts = ""},
        {.what = "target evidence describes the first store, not every later retainer",
         .body = S +
                 "  %outer = ctjs.create_array [%s]\n"
                 "  ctjs.set_property %p[%q], %s\n" +
                 R,
         .expected = "escapes:stored",
         .by = "ctjs.create_array",
         .storageTarget = "{ctjs.create_array}",
         .storageTargetVerdicts = "confined",
         .storageWrites = "ctjs.create_array[0] {ctjs.create_object} -> {ctjs.create_array}; "
                          "ctjs.set_property[2] {ctjs.create_object} -> {external}",
         .completeStorage = true},
        {.what = "an earlier cell store is not reclassified from a later array store",
         .body = S +
                 "  %cell = ctjs.create_cell %s\n"
                 "  %outer = ctjs.create_array [%s]\n" +
                 R,
         .expected = "escapes:stored",
         .by = "ctjs.create_cell",
         .storageTarget = "<uninitialized>",
         .storageWrites = "ctjs.create_cell[0] {ctjs.create_object} -> <uninitialized>; "
                          "ctjs.create_array[0] {ctjs.create_object} -> {ctjs.create_array}",
         .completeStorage = false},
        {.what = "a dead block's store does not produce target evidence",
         .body = S + "  ctjs.return %p\n"
                     "^dead:\n"
                     "  %outer = ctjs.create_array [%s]\n"
                     "  ctjs.return %p\n",
         .expected = "confined",
         .deadSites = 1,
         .storageTarget = "<uninitialized>",
         .storageWrites = "",
         .completeStorage = true},
        {.what = "a missing storage target lattice stays unresolved",
         .body = "  ctjs.append %p to %q {check}\n" + R,
         .expected = "<no verdict>",
         .unvisitedOperands = 2,
         .withAnalysis = false,
         .storageTarget = "<uninitialized>",
         .storageWitnessPosition = 1,
         .storageWrites = "ctjs.append[1] <uninitialized> -> <uninitialized>",
         .completeStorage = false},
        {.what = "an append target is not its Stored operand",
         .body = "  ctjs.append %p to %q {check}\n" + R,
         .expected = "<no verdict>",
         .storageTarget = "<uninitialized>",
         .storageWitnessPosition = 0},
        {.what = "an out-of-range array element witness stays unresolved",
         .body = "  %outer = ctjs.create_array [%p] {check}\n" + R,
         .expected = "confined",
         .storageTarget = "<uninitialized>",
         .storageWitnessPosition = 1},
        {.what = "a property key witness cannot stand in for its stored value",
         .body = "  ctjs.set_property %p[%q], %p {check}\n" + R,
         .expected = "<no verdict>",
         .storageTarget = "<uninitialized>",
         .storageWitnessPosition = 1},

        // ALL-WRITE EVIDENCE: a first witness cannot describe later writes or
        // the external/primitive values stored into a local target. The
        // complete marker describes this census, never a confinement proof.
        {.what = "a prior call escape does not hide later local and external writes",
         .body = S +
                 "  %called = ctjs.call %p(%q, %s)\n"
                 "  %outer = ctjs.create_array [%s]\n"
                 "  ctjs.set_property %p[%q], %s\n" +
                 R,
         .expected = "escapes:passed",
         .by = "ctjs.call",
         .position = 2,
         .storageWrites = "ctjs.create_array[0] {ctjs.create_object} -> {ctjs.create_array}; "
                          "ctjs.set_property[2] {ctjs.create_object} -> {external}",
         .completeStorage = true},
        {.what = "repeated literal elements retain distinct write positions",
         .body = S + "  %outer = ctjs.create_array [%s, %p, %s]\n" + R,
         .expected = "escapes:stored",
         .storageWrites = "ctjs.create_array[0] {ctjs.create_object} -> {ctjs.create_array}; "
                          "ctjs.create_array[1] {external} -> {ctjs.create_array}; "
                          "ctjs.create_array[2] {ctjs.create_object} -> {ctjs.create_array}",
         .completeStorage = true},
        {.what = "a primitive overwrite never erases the earlier object write",
         .body = S +
                 "  %outer = ctjs.create_object\n"
                 "  %zero = ctjs.constant #ctjs.number<0>\n"
                 "  ctjs.set_property %outer[%zero], %s\n"
                 "  ctjs.set_property %outer[%zero], %zero\n" +
                 R,
         .expected = "escapes:stored",
         .storageWrites = "ctjs.set_property[2] {ctjs.create_object} -> {ctjs.create_object}; "
                          "ctjs.set_property[2] {} -> {ctjs.create_object}",
         .completeStorage = true},
        {.what = "a confined target's external and primitive contents both enter the census",
         .body = S +
                 "  %zero = ctjs.constant #ctjs.number<0>\n"
                 "  ctjs.set_property %s[%zero], %p\n"
                 "  ctjs.set_property %s[%zero], %zero\n" +
                 R,
         .expected = "confined",
         .storageWrites = "ctjs.set_property[2] {external} -> {ctjs.create_object}; "
                          "ctjs.set_property[2] {} -> {ctjs.create_object}",
         .completeStorage = true},
        {.what = "a joined stored value retains its local and external alternatives",
         .body = S +
                 "  %outer = ctjs.create_array []\n"
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^join(%s : !ctjs.value), ^join(%p : !ctjs.value)\n"
                 "^join(%value: !ctjs.value):\n"
                 "  ctjs.append %value to %outer\n" +
                 R,
         .expected = "escapes:stored",
         .storageWrites = "ctjs.append[1] {ctjs.create_object, external} -> {ctjs.create_array}",
         .completeStorage = true},
        {.what = "both live branch stores are retained after the first branch wins the verdict",
         .body = S +
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^local, ^external\n"
                 "^local:\n"
                 "  %outer = ctjs.create_array [%s]\n"
                 "  cf.br ^exit\n"
                 "^external:\n"
                 "  ctjs.set_property %p[%q], %s\n"
                 "  cf.br ^exit\n"
                 "^exit:\n" +
                 R,
         .expected = "escapes:stored",
         .by = "ctjs.create_array",
         .storageWrites = "ctjs.create_array[0] {ctjs.create_object} -> {ctjs.create_array}; "
                          "ctjs.set_property[2] {ctjs.create_object} -> {external}",
         .completeStorage = true},
        {.what = "a spread call does not turn a complete direct-write census into confinement",
         .body = S +
                 "  %outer = ctjs.create_array [%s]\n"
                 "  %called = ctjs.call_spread %p(%q, %outer)\n" +
                 R,
         .expected = "escapes:stored",
         .storageWrites = "ctjs.create_array[0] {ctjs.create_object} -> {ctjs.create_array}",
         .completeStorage = true},
        {.what = "a two-object cycle preserves both directed writes without changing escape",
         .body = "  %s = ctjs.create_object {check, storage_test_id = \"s\"}\n"
                 "  %other = ctjs.create_object {storage_test_id = \"other\"}\n"
                 "  ctjs.set_property %s[%q], %other\n"
                 "  ctjs.set_property %other[%q], %s\n" +
                 R,
         .expected = "escapes:stored",
         .storageWrites =
             "ctjs.set_property[2] {ctjs.create_object@other} -> {ctjs.create_object@s}; "
             "ctjs.set_property[2] {ctjs.create_object@s} -> {ctjs.create_object@other}",
         .completeStorage = true},
        {.what = "one joined write retains both distinct local child sites",
         .body = "  %s = ctjs.create_object {check, storage_test_id = \"s\"}\n"
                 "  %other = ctjs.create_object {storage_test_id = \"other\"}\n"
                 "  %outer = ctjs.create_array []\n"
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^join(%s : !ctjs.value), ^join(%other : !ctjs.value)\n"
                 "^join(%value: !ctjs.value):\n"
                 "  ctjs.append %value to %outer\n" +
                 R,
         .expected = "escapes:stored",
         .storageWrites = "ctjs.append[1] {ctjs.create_object@other, ctjs.create_object@s} -> "
                          "{ctjs.create_array}",
         .completeStorage = true},
        {.what = "loop-created instances and carried external provenance share one static write",
         .body = "  %outer = ctjs.create_array []\n"
                 "  cf.br ^loop(%p : !ctjs.value)\n"
                 "^loop(%previous: !ctjs.value):\n" +
                 S +
                 "  ctjs.append %previous to %outer\n"
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^loop(%s : !ctjs.value), ^exit\n"
                 "^exit:\n" +
                 R,
         .expected = "escapes:stored",
         .storageWrites = "ctjs.append[1] {ctjs.create_object, external} -> {ctjs.create_array}",
         .completeStorage = true},
        {.what = "an unsupported destination remains incomplete with no tracked stored child",
         .body = S + "  %cell = ctjs.create_cell %p\n" + R,
         .expected = "confined",
         .storageWrites = "ctjs.create_cell[0] {external} -> <uninitialized>",
         .completeStorage = false},
        {.what = "a nested later store makes the direct-write census incomplete",
         .body = S +
                 "  %outer = ctjs.create_array [%s]\n"
                 "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    ctjs.set_property %p[%q], %s\n"
                 "  }\n" +
                 R,
         .expected = "escapes:stored",
         .storageWrites = "ctjs.create_array[0] {ctjs.create_object} -> {ctjs.create_array}",
         .completeStorage = false},
        {.what = "even an unrelated live nested region prevents a complete direct-write census",
         .body = S +
                 "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    ctjs.set_property %p[%q], %p\n"
                 "  }\n" +
                 R,
         .expected = "confined",
         .storageWrites = "",
         .completeStorage = false},
        {.what = "a nested store in a dead CFG block does not taint the live census",
         .body = S + "  ctjs.return %p\n"
                     "^dead:\n"
                     "  %t = ctjs.truthy %p\n"
                     "  scf.if %t {\n"
                     "    ctjs.set_property %p[%q], %s\n"
                     "  }\n"
                     "  ctjs.return %p\n",
         .expected = "confined",
         .storageWrites = "",
         .completeStorage = true},
        {.what = "late arguments retention prevents a complete direct-write census",
         .body = S + "  %arguments = ctjs.make_arguments\n" + R,
         .expected = "escapes:arguments_late",
         .capturesAllArguments = true,
         .wholeFunction = "arguments_late",
         .storageWrites = "",
         .completeStorage = false},

        // Direct load evidence connects known local target sites only. The
        // links deliberately retain different keys, later stores and distinct
        // dynamic instances; no row grants a new escape or result alias fact.
        {.what = "array reads link both initializer and append writes without weakening Stored",
         .body = S +
                 "  %outer = ctjs.create_array [%s]\n"
                 "  ctjs.append %p to %outer\n"
                 "  %read = ctjs.get_property %outer[%q]\n" +
                 R,
         .expected = "escapes:stored",
         .loadReads = "ctjs.get_property {ctjs.create_array} <- [0, 1]",
         .completeLoads = true},
        {.what = "a linked property result remains external in the alias lattice",
         .body = "  %child = ctjs.create_object\n"
                 "  %outer = ctjs.create_array [%child]\n"
                 "  %read = ctjs.get_property %outer[%q] {check}\n"
                 "  ctjs.return %read\n",
         .expected = "{external}",
         .alias = true,
         .loadReads = "ctjs.get_property {ctjs.create_array} <- [0]",
         .completeLoads = true},
        {.what = "read links retain later writes, overwrites and different keys",
         .body = S +
                 "  %outer = ctjs.create_object\n"
                 "  %zero = ctjs.constant #ctjs.number<0>\n"
                 "  %one = ctjs.constant #ctjs.number<1>\n"
                 "  %read = ctjs.get_property %outer[%zero]\n"
                 "  ctjs.set_property %outer[%zero], %s\n"
                 "  ctjs.set_property %outer[%zero], %zero\n"
                 "  ctjs.set_property %outer[%one], %p\n" +
                 R,
         .expected = "escapes:stored",
         .loadReads = "ctjs.get_property {ctjs.create_object} <- [0, 1, 2]",
         .completeLoads = true},
        {.what = "distinct same-kind bases select their own writes and joined links deduplicate",
         .body = S +
                 "  %a = ctjs.create_object\n"
                 "  %b = ctjs.create_object\n"
                 "  ctjs.set_property %a[%q], %s\n"
                 "  ctjs.set_property %b[%q], %p\n"
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^join(%a : !ctjs.value), ^join(%b : !ctjs.value)\n"
                 "^join(%base: !ctjs.value):\n"
                 "  ctjs.set_property %base[%q], %p\n"
                 "  %read = ctjs.get_property %base[%q]\n"
                 "  %other = ctjs.get_property %a[%q]\n" +
                 R,
         .expected = "escapes:stored",
         .loadReads = "ctjs.get_property {ctjs.create_object, ctjs.create_object} <- [0, 1, 2]; "
                      "ctjs.get_property {ctjs.create_object} <- [0, 2]",
         .completeLoads = true},
        {.what = "iterable carry keeps local read candidates beside its external alternative",
         .body = S +
                 "  %outer = ctjs.create_array [%s]\n"
                 "  %base = ctjs.iterable of %outer\n"
                 "  %read = ctjs.get_property %base[%q]\n"
                 "  %external = ctjs.get_property %p[%q]\n" +
                 R,
         .expected = "escapes:stored",
         .loadReads = "ctjs.get_property {ctjs.create_array, external} <- [0]; "
                      "ctjs.get_property {external} <- []",
         .completeLoads = true},
        {.what = "a load through a loaded value remains outside the local-site candidate graph",
         .body = S +
                 "  %outer = ctjs.create_array [%s]\n"
                 "  ctjs.set_property %s[%q], %p\n"
                 "  %base = ctjs.get_property %outer[%q]\n"
                 "  %read = ctjs.get_property %base[%q]\n" +
                 R,
         .expected = "escapes:stored",
         .loadReads = "ctjs.get_property {ctjs.create_array} <- [0]; "
                      "ctjs.get_property {external} <- []",
         .completeLoads = true},
        {.what = "loop-created bases share static candidates without asserting instance identity",
         .body = "  cf.br ^loop\n"
                 "^loop:\n" +
                 S +
                 "  %outer = ctjs.create_array [%s]\n"
                 "  %read = ctjs.get_property %outer[%q]\n"
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^loop, ^exit\n"
                 "^exit:\n" +
                 R,
         .expected = "escapes:stored",
         .loadReads = "ctjs.get_property {ctjs.create_array} <- [0]",
         .completeLoads = true},
        {.what = "dead CFG reads and writes cannot add candidates to the live census",
         .body = S +
                 "  %outer = ctjs.create_array [%s]\n"
                 "  %read = ctjs.get_property %outer[%q]\n" +
                 R +
                 "^dead:\n"
                 "  ctjs.append %p to %outer\n"
                 "  %dead = ctjs.get_property %outer[%q]\n" +
                 R,
         .expected = "escapes:stored",
         .loadReads = "ctjs.get_property {ctjs.create_array} <- [0]",
         .completeLoads = true},
        {.what = "a missing read base lattice keeps partial load evidence explicit",
         .body = "  %read = ctjs.get_property %p[%q] {check}\n"
                 "  ctjs.resume_throw\n",
         .expected = "<no lattice>",
         .unvisitedOperands = 1,
         .alias = true,
         .withAnalysis = false,
         .loadReads = "ctjs.get_property <uninitialized> <- []",
         .completeLoads = false,
         .provenanceInputs = false},
        {.what = "unsupported write targets preserve known read links with incomplete evidence",
         .body = S +
                 "  %outer = ctjs.create_array [%s]\n"
                 "  %cell = ctjs.create_cell %p\n"
                 "  %read = ctjs.get_property %outer[%q]\n" +
                 R,
         .expected = "escapes:stored",
         .loadReads = "ctjs.get_property {ctjs.create_array} <- [0]",
         .completeLoads = false,
         .provenanceInputs = false},
        {.what = "nested reads require region proof and leave top-level evidence incomplete",
         .body = S +
                 "  %outer = ctjs.create_array [%s]\n"
                 "  %read = ctjs.get_property %outer[%q]\n"
                 "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    %nested = ctjs.get_property %outer[%q]\n"
                 "  }\n" +
                 R,
         .expected = "escapes:stored",
         .loadReads = "ctjs.get_property {ctjs.create_array} <- [0]",
         .completeLoads = false,
         .provenanceInputs = false},
        {.what = "raw-frame retention cannot present load candidates as a complete census",
         .body = S +
                 "  %outer = ctjs.create_array [%s]\n"
                 "  %read = ctjs.get_property %outer[%q]\n"
                 "  %arguments = ctjs.make_arguments\n" +
                 R,
         .expected = "escapes:arguments_late",
         .capturesAllArguments = true,
         .wholeFunction = "arguments_late",
         .loadReads = "ctjs.get_property {ctjs.create_array} <- [0]",
         .completeLoads = false,
         .provenanceInputs = false},

        // The separate provenance closure follows candidate contents without
        // changing the original result lattice or first escape verdict.
        {.what = "nested local loads expose the stored child at every later sink",
         .body = "  %s = ctjs.create_object {check, storage_test_id = \"child\"}\n"
                 "  %inner = ctjs.create_array [%s] {storage_test_id = \"inner\"}\n"
                 "  %outer = ctjs.create_array [%inner] {storage_test_id = \"outer\"}\n"
                 "  %base = ctjs.get_property %outer[%q]\n"
                 "  %read = ctjs.get_property %base[%q]\n"
                 "  ctjs.store_global \"g\", %read\n"
                 "  ctjs.return %read\n",
         .expected = "escapes:stored",
         .provenanceReads =
             "{ctjs.create_array@outer} -> {ctjs.create_array@inner, external}; "
             "{ctjs.create_array@inner, external} -> {ctjs.create_object@child, external}",
         .provenanceExposures = "ctjs.create_array[0]:stored; ctjs.store_global[0]:stored_global; "
                                "ctjs.return[0]:returned",
         .provenanceInputs = true},
        {.what = "a store through a loaded target contributes to that local container",
         .body = "  %s = ctjs.create_object {check}\n"
                 "  %inner = ctjs.create_array [] {storage_test_id = \"inner\"}\n"
                 "  %outer = ctjs.create_array [%inner] {storage_test_id = \"outer\"}\n"
                 "  %base = ctjs.get_property %outer[%q]\n"
                 "  ctjs.set_property %base[%q], %s\n"
                 "  %read = ctjs.get_property %inner[%q]\n"
                 "  ctjs.return %read\n",
         .expected = "escapes:stored",
         .provenanceReads = "{ctjs.create_array@outer} -> {ctjs.create_array@inner, external}; "
                            "{ctjs.create_array@inner} -> {ctjs.create_object, external}",
         .provenanceExposures = "ctjs.set_property[2]:stored; ctjs.return[0]:returned",
         .provenanceInputs = true},
        {.what = "a loaded stored value propagates through a second local container",
         .body = "  %s = ctjs.create_object {check}\n"
                 "  %a = ctjs.create_array [%s] {storage_test_id = \"a\"}\n"
                 "  %b = ctjs.create_array [] {storage_test_id = \"b\"}\n"
                 "  %first = ctjs.get_property %a[%q]\n"
                 "  ctjs.append %first to %b\n"
                 "  %second = ctjs.get_property %b[%q]\n"
                 "  ctjs.return %second\n",
         .expected = "escapes:stored",
         .provenanceReads = "{ctjs.create_array@a} -> {ctjs.create_object, external}; "
                            "{ctjs.create_array@b} -> {ctjs.create_object, external}",
         .provenanceExposures =
             "ctjs.create_array[0]:stored; ctjs.append[1]:stored; ctjs.return[0]:returned",
         .provenanceInputs = true},
        {.what = "loaded candidates cross duplicate successor edges and an ODS carry",
         .body = S +
                 "  %outer = ctjs.create_array [%s]\n"
                 "  %read = ctjs.get_property %outer[%q]\n"
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^join(%read : !ctjs.value), ^join(%p : !ctjs.value)\n"
                 "^join(%value: !ctjs.value):\n"
                 "  %carried = ctjs.iterable of %value\n"
                 "  ctjs.store_global \"g\", %carried\n" +
                 R,
         .expected = "escapes:stored",
         .provenanceReads = "{ctjs.create_array} -> {ctjs.create_object, external}",
         .provenanceExposures = "ctjs.create_array[0]:stored; ctjs.store_global[0]:stored_global",
         .provenanceInputs = true},
        {.what = "self-cycle provenance converges and does not prove the cycle confined",
         .body = S + "  ctjs.set_property %s[%q], %s\n"
                     "  %first = ctjs.get_property %s[%q]\n"
                     "  %second = ctjs.get_property %first[%q]\n"
                     "  ctjs.return %second\n",
         .expected = "escapes:stored",
         .provenanceReads = "{ctjs.create_object} -> {ctjs.create_object, external}; "
                            "{ctjs.create_object, external} -> {ctjs.create_object, external}",
         .provenanceExposures = "ctjs.set_property[2]:stored; ctjs.return[0]:returned",
         .provenanceInputs = true},
        {.what = "mutual-cycle provenance retains exact distinct allocation identities",
         .body = "  %s = ctjs.create_object {check, storage_test_id = \"s\"}\n"
                 "  %other = ctjs.create_object {storage_test_id = \"other\"}\n"
                 "  ctjs.set_property %s[%q], %other\n"
                 "  ctjs.set_property %other[%q], %s\n"
                 "  %first = ctjs.get_property %s[%q]\n"
                 "  %second = ctjs.get_property %first[%q]\n"
                 "  ctjs.return %second\n",
         .expected = "escapes:stored",
         .provenanceReads =
             "{ctjs.create_object@s} -> {ctjs.create_object@other, external}; "
             "{ctjs.create_object@other, external} -> {ctjs.create_object@s, external}",
         .provenanceExposures = "ctjs.set_property[2]:stored; ctjs.return[0]:returned",
         .provenanceInputs = true},
        {.what =
             "loaded candidates cross loop-carried aliases without asserting per-instance identity",
         .body = S + "  %outer = ctjs.create_array [%s]\n"
                     "  %read = ctjs.get_property %outer[%q]\n"
                     "  cf.br ^loop(%read : !ctjs.value)\n"
                     "^loop(%value: !ctjs.value):\n"
                     "  %t = ctjs.truthy %p\n"
                     "  cf.cond_br %t, ^loop(%p : !ctjs.value), ^exit(%value : !ctjs.value)\n"
                     "^exit(%last: !ctjs.value):\n"
                     "  ctjs.return %last\n",
         .expected = "escapes:stored",
         .provenanceReads = "{ctjs.create_array} -> {ctjs.create_object, external}",
         .provenanceExposures = "ctjs.create_array[0]:stored; ctjs.return[0]:returned",
         .provenanceInputs = true},
        {.what = "provenance keeps later writes and excludes dead returns",
         .body = S +
                 "  %outer = ctjs.create_array []\n"
                 "  %read = ctjs.get_property %outer[%q]\n"
                 "  ctjs.append %s to %outer\n"
                 "  ctjs.store_global \"g\", %read\n" +
                 R +
                 "^dead:\n"
                 "  ctjs.return %read\n",
         .expected = "escapes:stored",
         .provenanceReads = "{ctjs.create_array} -> {ctjs.create_object, external}",
         .provenanceExposures = "ctjs.append[1]:stored; ctjs.store_global[0]:stored_global",
         .provenanceInputs = true},
        {.what = "an earlier call verdict cannot hide loaded later exposures",
         .body = S + "  %called = ctjs.call %p(%q, %s)\n"
                     "  %outer = ctjs.create_array [%s]\n"
                     "  %read = ctjs.get_property %outer[%q]\n"
                     "  ctjs.throw %read\n",
         .expected = "escapes:passed",
         .provenanceReads = "{ctjs.create_array} -> {ctjs.create_object, external}",
         .provenanceExposures =
             "ctjs.call[2]:passed; ctjs.create_array[0]:stored; ctjs.throw[0]:thrown",
         .provenanceInputs = true},
    };

    for (const row & r : rows) { check(context, r); }
    checkReadEvidenceMutation(context);

    if (failures != 0) {
        std::printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("escape analysis: %zu rows, every cell agrees with the VM\n", rows.size());
    return 0;
}
