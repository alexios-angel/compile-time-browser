// PHASE 55A'S TABLE, ONE CELL AT A TIME - the array contents prerequisite and
// the Stored retention consumer: their row tables, key controls, live
// mutation states and budget cutoffs.
//
// One of four executables carved out of a 2,763-line test/EscapeAnalysis.cpp on
// 2026-09-08. The row harness they share - `row`, `kPrologue`, `check`, the
// role and verdict printers - is EscapeAnalysisHarness.h beside this; the rows
// themselves are verbatim, in their original order, and every one is still
// run. Registered one target each in test/cmake/Analysis.cmake.

#include "EscapeAnalysisHarness.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

using namespace ctcompile::test::escape;

namespace {

struct contents_row {
    const char * what;
    std::string body;
    ArrayContentsFailure failure = ArrayContentsFailure::None;
    const char * arrays = "";
    const char * reads = "";
    const char * exit = "";
    const char * writes = nullptr;
};

void checkArrayContents(mlir::ModuleOp module, const contents_row & expected) {
    const row r{.what = expected.what, .body = expected.body, .expected = ""};
    ctjs::FuncOp function = *module.getOps<ctjs::FuncOp>().begin();
    mlir::DataFlowSolver solver;
    solver.load<mlir::dataflow::DeadCodeAnalysis>();
    solver.load<mlir::dataflow::SparseConstantPropagation>();
    solver.load<EscapeAnalysis>();
    if (failed(solver.initializeAndRun(module))) {
        fail(r, "the contents fixture's legacy solver did not converge");
        return;
    }
    const EscapeVerdicts before = computeVerdicts(solver, function, 0);
    const ArrayContentsEvidence contents = computeArrayContents(function);
    const bool complete = expected.failure == ArrayContentsFailure::None;
    if (contents.complete != complete || contents.failure != expected.failure) {
        fail(r, "contents failure: expected " + std::to_string(static_cast<int>(expected.failure)) +
                    ", got " + std::to_string(static_cast<int>(contents.failure)));
    }
    const auto empty = [](const ArrayContentsEvidence & result) {
        return result.arrays.empty() && result.reads.empty() && result.writes.empty() &&
               result.exits.empty();
    };
    if (!complete) {
        if (!empty(contents) || contents.refusedBy == nullptr) {
            fail(r, "refused contents retained proof records or lost its refusal witness");
        }
    } else {
        std::string arrays;
        std::vector<mlir::Operation *> arraySites;
        for (const ArrayContentsExit & edge : contents.exits) {
            if (!arrays.empty()) { arrays += " | "; }
            bool first = true;
            for (const auto & [array, elements] : edge.arrays) {
                if (!first) { arrays += "; "; }
                first = false;
                arrays += contentsLabel(array) + ":[";
                for (std::size_t i = 0; i < elements.size(); ++i) {
                    if (i != 0) { arrays += ","; }
                    arrays += contentsLabel(elements[i].getDefiningOp());
                }
                arrays += "]";
                if (!llvm::is_contained(arraySites, array)) { arraySites.push_back(array); }
            }
        }
        if (contents.arrays.size() != arraySites.size() ||
            !llvm::all_of(arraySites, [&](mlir::Operation * site) {
                return llvm::count(contents.arrays, site) == 1;
            })) {
            fail(r, "the array inventory differs from the complete path snapshots");
        }
        std::string reads;
        for (const ArrayElementRead & read : contents.reads) {
            if (!reads.empty()) { reads += "; "; }
            reads += contentsLabel(read.array) + "[" + std::to_string(read.index) +
                     "]=" + contentsLabel(read.value.getDefiningOp());
        }
        std::string exit;
        for (const ArrayContentsExit & edge : contents.exits) {
            if (!exit.empty()) { exit += "; "; }
            std::vector<std::string> sites;
            for (mlir::Operation * site : edge.reachableSites) {
                sites.push_back(contentsLabel(site));
            }
            std::sort(sites.begin(), sites.end());
            exit +=
                contentsLabel(edge.value.getDefiningOp()) + " -> {" + llvm::join(sites, ",") + "}";
        }
        const auto equal = [&](const char * field, const std::string & actual,
                               const char * wanted) {
            if (actual != wanted) {
                fail(r, std::string{field} + ": expected " + wanted + ", got " + actual);
            }
        };
        equal("arrays", arrays, expected.arrays);
        equal("reads", reads, expected.reads);
        equal("exit", exit, expected.exit);
        if (expected.writes != nullptr) {
            std::string writes;
            for (const ArrayElementWrite & write : contents.writes) {
                if (!writes.empty()) { writes += "; "; }
                writes += write.by->getName().getStringRef().str() + "[" +
                          std::to_string(write.position) + "]:" + contentsLabel(write.array) + "[" +
                          std::to_string(write.index) +
                          "]=" + contentsLabel(write.value.getDefiningOp());
            }
            equal("writes", writes, expected.writes);
        }
    }

    // Every incomplete prefix includes the final graph traversal, not merely
    // the read scan. Even a cutoff after the last successful read publishes no
    // evidence. The exact completion/refusal budget reproduces the full query.
    for (std::size_t budget = 0; budget < contents.work; ++budget) {
        const ArrayContentsEvidence partial = computeArrayContents(function, budget);
        if (partial.complete || partial.failure != ArrayContentsFailure::WorkLimit ||
            partial.work != budget || !empty(partial)) {
            fail(r, "an exhausted contents prefix published evidence or misreported work");
            break;
        }
    }
    const ArrayContentsEvidence exact = computeArrayContents(function, contents.work);
    if (exact.complete != contents.complete || exact.failure != contents.failure ||
        exact.work != contents.work) {
        fail(r, "the exact contents completion/refusal budget changed its result");
    }
    const EscapeVerdicts after = computeVerdicts(solver, function, 0);
    for (const auto & [site, verdict] : before.sites) {
        if (verdictString(before, site) != verdictString(after, site) ||
            after.sites.find(site)->second.by != verdict.by) {
            fail(r, "the contents prerequisite changed a legacy escape verdict");
        }
    }
    if (!complete) {
        const EscapeVerdicts refused = computeVerdicts(solver, function);
        if (refused.arrayRetentionComplete || refused.confinedStoredSites != 0) {
            fail(r, "unsupported contents authorized a Stored refinement");
        }
        for (const auto & [site, verdict] : before.sites) {
            if (verdictString(before, site) != verdictString(refused, site) ||
                refused.sites.find(site)->second.by != verdict.by) {
                fail(r, "unsupported contents changed an original escape verdict");
            }
        }
    }
    module.walk([&](ctjs::GetPropertyOp read) {
        const AliasLattice * lattice = solver.lookupState<AliasLattice>(read.getResult());
        if (lattice != nullptr && !lattice->getValue().isUninitialized() &&
            lattice->getValue() != AliasValue::external()) {
            fail(r, "the complete contents query changed a legacy load result lattice");
        }
    });
}

void checkArrayContents(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %one = ctjs.constant #ctjs.number<4607182418800017408> "
        "{storage_test_id = \"one\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %y = ctjs.create_object {storage_test_id = \"y\"}\n";
    const std::string array = values + "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string read = "  %read = ctjs.get_property %a[%zero]\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::vector<contents_row> rows = {
        {.what = "complete contents: inline/append/overwrite preserve exact earlier reads",
         .body = array + read +
                 "  ctjs.append %y to %a\n"
                 "  ctjs.set_property %a[%zero], %y\n"
                 "  %later = ctjs.get_property %a[%zero]\n"
                 "  %second = ctjs.get_property %a[%one]\n"
                 "  ctjs.return %a\n",
         .arrays = "a:[y,y]",
         .reads = "a[0]=x; a[0]=y; a[1]=y",
         .exit = "a -> {a,y}",
         .writes = "ctjs.create_array[0]:a[0]=x; ctjs.append[1]:a[1]=y; "
                   "ctjs.set_property[2]:a[0]=y"},
        {.what = "complete contents: loaded aliases write the same nested array instance",
         .body = values + "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n"
                          "  %b = ctjs.create_array [%a] {storage_test_id = \"b\"}\n"
                          "  %alias = ctjs.get_property %b[%zero]\n"
                          "  ctjs.append %x to %alias\n"
                          "  %read = ctjs.get_property %a[%zero]\n"
                          "  ctjs.return %b\n",
         .arrays = "a:[x]; b:[a]",
         .reads = "b[0]=a; a[0]=x",
         .exit = "b -> {a,b,x}",
         .writes = "ctjs.create_array[0]:b[0]=a; ctjs.append[1]:a[0]=x"},
        {.what = "complete contents: a saved read survives a later overwrite",
         .body = array + read +
                 "  ctjs.set_property %a[%zero], %y\n"
                 "  ctjs.return %read\n",
         .arrays = "a:[y]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        {.what = "complete contents: a loaded key and loaded stored value have exact origins",
         .body = array + "  %keys = ctjs.create_array [%zero] {storage_test_id = \"keys\"}\n"
                         "  %key = ctjs.get_property %keys[%zero]\n"
                         "  %value = ctjs.get_property %a[%key]\n"
                         "  ctjs.append %value to %a\n"
                         "  ctjs.return %a\n",
         .arrays = "a:[x,x]; keys:[zero]",
         .reads = "keys[0]=zero; a[0]=x",
         .exit = "a -> {a,x}"},
        {.what = "complete contents: self cycle has one identity without a confinement claim",
         .body = values + "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n"
                          "  ctjs.append %a to %a\n"
                          "  %read = ctjs.get_property %a[%zero]\n"
                          "  ctjs.return %read\n",
         .arrays = "a:[a]",
         .reads = "a[0]=a",
         .exit = "a -> {a}"},
        {.what = "complete contents: mutual cycles terminate with both exact identities",
         .body = values + "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n"
                          "  %b = ctjs.create_array [%a] {storage_test_id = \"b\"}\n"
                          "  ctjs.append %b to %a\n"
                          "  ctjs.return %b\n",
         .arrays = "a:[b]; b:[a]",
         .exit = "b -> {a,b}"},
        {.what = "complete contents: stored locals need not be reachable from a primitive return",
         .body = array + done,
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
        {.what = "complete contents: an empty returned array has no element paths",
         .body = "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n  ctjs.return %a\n",
         .arrays = "a:[]",
         .exit = "a -> {a}"},
        {.what = "contents refuses an unknown initializer",
         .body = values + "  %a = ctjs.create_array [%p]\n" + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "contents refuses an unknown appended value",
         .body = array + read + "  ctjs.append %p to %a\n" + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "contents refuses an unknown replacement",
         .body = array + read + "  ctjs.set_property %a[%zero], %p\n" + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "contents refuses an external base",
         .body = array + "  %read = ctjs.get_property %p[%zero]\n" + done,
         .failure = ArrayContentsFailure::UnknownArray},
        {.what = "contents refuses even a fresh plain object as an array base",
         .body = array + "  %read = ctjs.get_property %x[%zero]\n" + done,
         .failure = ArrayContentsFailure::UnknownArray},
        {.what = "contents refuses a dynamic key",
         .body = array + "  %read = ctjs.get_property %a[%p]\n" + done,
         .failure = ArrayContentsFailure::UnknownIndex},
        {.what = "contents refuses an uninitialized read",
         .body = array + "  %read = ctjs.get_property %a[%one]\n" + done,
         .failure = ArrayContentsFailure::MissingElement},
        {.what = "contents refuses extension by generic write even without a hole",
         .body = array + "  ctjs.set_property %a[%one], %x\n" + done,
         .failure = ArrayContentsFailure::MissingElement},
        {.what = "contents refuses a late deletion",
         .body = array + read + "  ctjs.delete_property %a[%zero]\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses a late prototype mutation",
         .body = array + read + "  ctjs.set_proto %p on %a\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses a late accessor",
         .body = array + read + "  ctjs.define_accessor \"0\" on %a get %p set %q\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses a late call even when no array is an explicit actual",
         .body = array + read + "  %called = ctjs.call %p(%q)\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses publication after every own read",
         .body = array + read + "  ctjs.store_global \"held\", %read\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses cell retention after every own read",
         .body = array + read + "  %cell = ctjs.create_cell %read\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses an unsupported carrier",
         .body = array + read + "  %carried = ctjs.iterable of %a\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses throw's implicit diagnostic formatting call",
         .body = array + read + "  ctjs.throw %read\n",
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses an unrelated global getter",
         .body = array + read + "  %global = ctjs.load_global \"later\"\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses late arguments retention",
         .body = array + read + "  %args = ctjs.make_arguments\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses late rest retention",
         .body = array + read + "  %rest = ctjs.gather_rest from 0\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses suspension retention",
         .body = array + read + "  %suspended = ctjs.suspend await %p\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses nested region retention",
         .body = array + read +
                 "  %region = scf.execute_region -> !ctjs.value {\n"
                 "    ctjs.store_global \"held\", %read\n"
                 "    scf.yield %read : !ctjs.value\n"
                 "  }\n" +
                 done,
         .failure = ArrayContentsFailure::UnsupportedControlFlow},
        {.what = "contents ignores retention in a structurally unreachable block",
         .body = array + read + done + "^dead:\n  %args = ctjs.make_arguments\n" + done,
         .arrays = "a:[x]",
         .reads = "a[0]=x",
         .exit = "zero -> {}"},
        {.what = "contents follows an unconditional successor with exact local values",
         .body = array + "  cf.br ^next\n^next:\n" + done,
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
        {.what = "contents ignores a structural join's unreachable predecessor",
         .body = array + "  cf.br ^join\n^dead:\n  cf.br ^join\n^join:\n" + done,
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
        {.what = "contents refuses opaque successor semantics",
         .body = array + "  \"test.branch\"()[^next] : () -> ()\n^next:\n" + done,
         .failure = ArrayContentsFailure::UnsupportedControlFlow},
        {.what = "contents checks both conditional edges even when truthy has a constant input",
         .body = array +
                 "  %condition = ctjs.truthy %zero\n"
                 "  cf.cond_br %condition, ^left, ^right\n^left:\n" +
                 done + "^right:\n" + done,
         .arrays = "a:[x] | a:[x]",
         .exit = "zero -> {}; zero -> {}"},
        {.what = "contents refuses a loop rather than collapsing repeated allocation instances",
         .body = "  cf.br ^loop\n^loop:\n  %a = ctjs.create_array []\n  cf.br ^loop\n",
         .failure = ArrayContentsFailure::UnsupportedControlFlow},
    };
    const auto run = [&](const contents_row & r) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + r.body + "}\n", &context);
        if (!module) {
            fail(row{.what = r.what, .body = r.body, .expected = ""},
                 "the contents fixture did not parse");
            return;
        }
        checkArrayContents(*module, r);
    };
    for (const contents_row & r : rows) { run(r); }

    // The key interpretation is independent of the VM's permissive numeric
    // cast. Fractional/NaN/infinite numbers and noncanonical string names must
    // never borrow a dense slot proof. Number -0 and canonical "0" do qualify.
    const std::vector<std::pair<std::string, bool>> keys = {
        {"#ctjs.number<9223372036854775808>", true}, // -0
        {"#ctjs.string<\"0\">", true},
        {"#ctjs.string<\"-0\">", false},
        {"#ctjs.string<\"00\">", false},
        {"#ctjs.string<\"+0\">", false},
        {"#ctjs.string<\"0.0\">", false},
        {"#ctjs.string<\"\">", false},
        {"#ctjs.string<\"length\">", false},
        {"#ctjs.string<\"4294967295\">", false},
        {"#ctjs.string<\"100000000000000000000000000000000000\">", false},
        {"#ctjs.number<4602678819172646912>", false},  // 0.5
        {"#ctjs.number<13830554455654793216>", false}, // -1
        {"#ctjs.number<9221120237041090560>", false},  // NaN
        {"#ctjs.number<9218868437227405312>", false},  // infinity
    };
    for (const auto & [key, supported] : keys) {
        run({.what = "contents independently validates canonical own-element keys",
             .body = array + "  %key = ctjs.constant " + key +
                     "\n"
                     "  %read = ctjs.get_property %a[%key]\n" +
                     done,
             .failure = supported ? ArrayContentsFailure::None : ArrayContentsFailure::UnknownIndex,
             .arrays = supported ? "a:[x]" : "",
             .reads = supported ? "a[0]=x" : "",
             .exit = supported ? "zero -> {}" : ""});
    }

    contents_row mutation{.what =
                              "contents is recomputed after live operand and late-sink mutations",
                          .body = array + read +
                                  "  ctjs.set_property %a[%zero], %y\n"
                                  "  %later = ctjs.get_property %a[%zero]\n"
                                  "  ctjs.return %a\n",
                          .arrays = "a:[y]",
                          .reads = "a[0]=x; a[0]=y",
                          .exit = "a -> {a,y}"};
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.body + "}\n", &context);
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        ctjs::GetPropertyOp load;
        module->walk([&](ctjs::SetPropertyOp op) { store = op; });
        module->walk([&](ctjs::GetPropertyOp op) { load = op; });
        const mlir::Value original = store.getValue();
        const mlir::Value base = load.getObject();
        const mlir::Value key = load.getKey();
        const mlir::Value parameter = function.getBody().front().getArgument(3);
        checkArrayContents(*module, mutation);

        store->setOperand(2,
                          llvm::cast<ctjs::CreateArrayOp>(base.getDefiningOp()).getElements()[0]);
        mutation.arrays = "a:[x]";
        mutation.reads = "a[0]=x; a[0]=x";
        mutation.exit = "a -> {a,x}";
        checkArrayContents(*module, mutation);
        store->setOperand(2, parameter);
        mutation.failure = ArrayContentsFailure::UnknownValue;
        checkArrayContents(*module, mutation);
        store->setOperand(2, original);
        load->setOperand(1, parameter);
        mutation.failure = ArrayContentsFailure::UnknownIndex;
        checkArrayContents(*module, mutation);
        load->setOperand(1, key);
        load->setOperand(0, parameter);
        mutation.failure = ArrayContentsFailure::UnknownArray;
        checkArrayContents(*module, mutation);
        load->setOperand(0, base);

        // A forged completion marker cannot rescue a proof after publication.
        mlir::OpBuilder builder(function.getBody().front().getTerminator());
        auto published = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", base);
        base.getDefiningOp()->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        mutation.failure = ArrayContentsFailure::UnsupportedOperation;
        checkArrayContents(*module, mutation);
        published.erase();
        base.getDefiningOp()->removeAttr("ctnative.array_contents_complete");
        mutation.failure = ArrayContentsFailure::None;
        mutation.arrays = "a:[y]";
        mutation.reads = "a[0]=x; a[0]=y";
        mutation.exit = "a -> {a,y}";
        checkArrayContents(*module, mutation);
    } else {
        fail(row{.what = mutation.what, .body = mutation.body, .expected = ""},
             "the live contents mutation fixture did not parse");
    }
    std::printf("array contents: %zu rows, %zu key controls, seven live states\n", rows.size(),
                keys.size());
}

struct retention_row {
    const char * what;
    std::string body;
    const char * discharged = "";
    bool complete = true;
    bool withAnalysis = true;
};

std::size_t checkArrayRetention(mlir::ModuleOp module, const retention_row & expected) {
    const row r{.what = expected.what, .body = expected.body, .expected = expected.discharged};
    ctjs::FuncOp function = *module.getOps<ctjs::FuncOp>().begin();
    mlir::DataFlowSolver solver;
    solver.load<mlir::dataflow::DeadCodeAnalysis>();
    solver.load<mlir::dataflow::SparseConstantPropagation>();
    if (expected.withAnalysis) { solver.load<EscapeAnalysis>(); }
    if (failed(solver.initializeAndRun(module))) {
        fail(r, "the retention fixture's solver did not converge");
        return 0;
    }
    const auto ir = [&]() {
        std::string text;
        llvm::raw_string_ostream stream(text);
        module->print(stream);
        return text;
    };
    const std::string beforeIR = ir();
    const EscapeVerdicts original = computeVerdicts(solver, function, 0);
    const EscapeVerdicts refined = computeVerdicts(solver, function);
    if (refined.arrayRetentionComplete != expected.complete) {
        fail(r, "the retention completion marker differs");
    }
    const auto sameVerdicts = [&](const EscapeVerdicts & actual, const EscapeVerdicts & wanted) {
        if (actual.sites.size() != wanted.sites.size()) { return false; }
        for (const auto & [site, verdict] : wanted.sites) {
            auto found = actual.sites.find(site);
            if (found == actual.sites.end() || found->second.reason != verdict.reason ||
                found->second.by != verdict.by || found->second.position != verdict.position) {
                return false;
            }
        }
        return true;
    };
    std::vector<std::string> discharged;
    for (const auto & [site, verdict] : original.sites) {
        auto found = refined.sites.find(site);
        if (found == refined.sites.end()) {
            fail(r, "the retention consumer lost an original site");
            continue;
        }
        const Verdict & after = found->second;
        if (after.reason == verdict.reason) {
            if (after.by != verdict.by || after.position != verdict.position) {
                fail(r, "an unchanged retained site lost its original witness");
            }
        } else if (verdict.reason != EscapeReason::Stored ||
                   after.reason != EscapeReason::Confined || after.by != nullptr ||
                   after.position != 0 || !refined.arrayRetentionComplete) {
            fail(r, "the consumer changed a verdict outside the complete Stored refinement");
        } else {
            discharged.push_back(contentsLabel(site));
        }
    }
    const std::string actual = llvm::join(discharged, ",");
    if (actual != expected.discharged || discharged.size() != refined.confinedStoredSites) {
        fail(r,
             "expected discharged sites " + std::string{expected.discharged} + ", got " + actual);
    }
    if (refined.directStorage.writes.size() != original.directStorage.writes.size() ||
        refined.directLoads.reads.size() != original.directLoads.reads.size() ||
        refined.directStorage.complete != original.directStorage.complete ||
        refined.directLoads.complete != original.directLoads.complete ||
        refined.unvisitedSites != original.unvisitedSites ||
        refined.unvisitedOperands != original.unvisitedOperands) {
        fail(r, "the independent retention consumer changed legacy census or gap evidence");
    }

    // Each cutoff is a failed transaction, including the interval after the
    // independent contents proof finished but before graph/verdict completion.
    for (std::size_t budget = 0; budget < refined.arrayRetentionWork; ++budget) {
        const EscapeVerdicts partial = computeVerdicts(solver, function, budget);
        if (partial.arrayRetentionComplete || partial.confinedStoredSites != 0 ||
            partial.arrayRetentionWork != budget || !sameVerdicts(partial, original)) {
            fail(r, "an incomplete retention budget changed an original verdict or work count");
            break;
        }
    }
    const EscapeVerdicts exact = computeVerdicts(solver, function, refined.arrayRetentionWork);
    if (exact.arrayRetentionComplete != refined.arrayRetentionComplete ||
        exact.arrayRetentionWork != refined.arrayRetentionWork ||
        exact.confinedStoredSites != refined.confinedStoredSites || !sameVerdicts(exact, refined)) {
        fail(r, "the exact retention completion/refusal budget changed its result");
    }
    module.walk([&](ctjs::GetPropertyOp read) {
        const AliasLattice * lattice = solver.lookupState<AliasLattice>(read.getResult());
        if (lattice != nullptr && !lattice->getValue().isUninitialized() &&
            lattice->getValue() != AliasValue::external()) {
            fail(r, "retention refinement changed a legacy load result lattice");
        }
    });
    if (ir() != beforeIR) { fail(r, "retention refinement changed the source IR"); }
    return refined.arrayRetentionWork;
}

void checkArrayRetention(mlir::MLIRContext & context) {
    const std::string values = "  %zero = ctjs.constant #ctjs.number<0>\n"
                               "  %one = ctjs.constant #ctjs.number<4607182418800017408>\n"
                               "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
                               "  %y = ctjs.create_object {storage_test_id = \"y\"}\n";
    const std::string array = values + "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string read = "  %read = ctjs.get_property %a[%zero]\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::vector<retention_row> rows = {
        {.what = "retention discharges a local array's unreturned object element",
         .body = array + done,
         .discharged = "x"},
        {.what = "retention discharges every private append and repeated initializer",
         .body = array + "  ctjs.append %x to %a\n  ctjs.append %y to %a\n" + done,
         .discharged = "x,y"},
        {.what = "retention discharges a nested acyclic local array graph",
         .body = array + "  %b = ctjs.create_array [%a] {storage_test_id = \"b\"}\n" + done,
         .discharged = "x,a"},
        {.what = "repeated edges and shared children are not cycles",
         .body = array +
                 "  %b = ctjs.create_array [%a, %a] {storage_test_id = \"b\"}\n"
                 "  %c = ctjs.create_array [%a] {storage_test_id = \"c\"}\n" +
                 done,
         .discharged = "x,a"},
        {.what = "a returned array retains every child with its original Stored witness",
         .body = array + "  ctjs.append %y to %a\n  ctjs.return %a\n"},
        {.what = "a returned array does not retain an overwritten child",
         .body = array + "  ctjs.set_property %a[%zero], %y\n  ctjs.return %a\n",
         .discharged = "x"},
        {.what = "a saved returned read retains the old child after replacement",
         .body = array + read + "  ctjs.set_property %a[%zero], %y\n  ctjs.return %read\n",
         .discharged = "y"},
        {.what = "a loaded array alias updates the returned container's retention graph",
         .body = values + "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n"
                          "  %b = ctjs.create_array [%a] {storage_test_id = \"b\"}\n"
                          "  %alias = ctjs.get_property %b[%zero]\n"
                          "  ctjs.append %x to %alias\n"
                          "  ctjs.return %b\n"},
        {.what = "a loaded child stored in a returned second array remains retained",
         .body = array + read +
                 "  %b = ctjs.create_array [%read] {storage_test_id = \"b\"}\n"
                 "  ctjs.set_property %a[%zero], %y\n"
                 "  ctjs.return %b\n",
         .discharged = "y"},
        {.what = "a saved returned array read retains its own current descendants",
         .body = array + "  %b = ctjs.create_array [%a] {storage_test_id = \"b\"}\n"
                         "  %saved = ctjs.get_property %b[%zero]\n"
                         "  ctjs.set_property %b[%zero], %y\n"
                         "  ctjs.return %saved\n",
         .discharged = "y"},
        {.what = "a direct child return is not hidden by its earlier storage",
         .body = array + "  ctjs.return %x\n"},
        {.what = "a primitive loaded return cannot retain the overwritten object",
         .body = array + "  ctjs.set_property %a[%zero], %zero\n" + read + "  ctjs.return %read\n",
         .discharged = "x"},
        {.what = "branch-carried saved reads retain the old child after a successor overwrite",
         .body = array + read +
                 "  cf.br ^next(%a, %read : !ctjs.value, !ctjs.value)\n"
                 "^next(%base: !ctjs.value, %saved: !ctjs.value):\n"
                 "  ctjs.set_property %base[%zero], %y\n  ctjs.return %saved\n",
         .discharged = "y"},
        {.what = "returning a successor-mutated array releases only its overwritten child",
         .body = array + "  cf.br ^next(%a, %zero : !ctjs.value, !ctjs.value)\n"
                         "^next(%base: !ctjs.value, %key: !ctjs.value):\n"
                         "  ctjs.set_property %base[%key], %y\n  ctjs.return %base\n",
         .discharged = "x"},
        {.what = "an unreturned self cycle does not select a shared graph owner",
         .body = array + "  ctjs.append %a to %a\n" + done,
         .complete = false},
        {.what = "an unreturned mutual cycle stays outside the refinement",
         .body = array +
                 "  %b = ctjs.create_array [%a] {storage_test_id = \"b\"}\n"
                 "  ctjs.append %b to %a\n" +
                 done,
         .complete = false},
        {.what = "a transient cycle stays refused after its last edge is overwritten",
         .body = array +
                 "  ctjs.set_property %a[%zero], %a\n"
                 "  ctjs.set_property %a[%zero], %y\n" +
                 done,
         .complete = false},
        {.what = "a disconnected cycle leaves even an acyclic candidate untouched",
         .body = array +
                 "  %b = ctjs.create_array [] {storage_test_id = \"b\"}\n"
                 "  ctjs.append %b to %b\n" +
                 done,
         .complete = false},
        {.what = "an unknown return does not borrow complete local contents",
         .body = array + "  ctjs.return %p\n",
         .complete = false},
        {.what = "missing solver lattices keep their unvisited refusal",
         .body = array + done,
         .complete = false,
         .withAnalysis = false},
    };
    std::size_t budgets = 0;
    for (const retention_row & r : rows) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + r.body + "}\n", &context);
        if (!module) {
            fail(row{.what = r.what, .body = r.body, .expected = r.discharged},
                 "the retention fixture did not parse");
            continue;
        }
        budgets += checkArrayRetention(*module, r);
    }

    retention_row mutation{
        .what = "retention follows current returns, writes, keys and late exposures",
        .body = array + read + "  ctjs.set_property %a[%zero], %y\n  ctjs.return %a\n",
        .discharged = "x"};
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.body + "}\n", &context);
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        ctjs::GetPropertyOp load;
        module->walk([&](ctjs::SetPropertyOp op) { store = op; });
        module->walk([&](ctjs::GetPropertyOp op) { load = op; });
        mlir::Operation * exit = function.getBody().front().getTerminator();
        const mlir::Value base = load.getObject();
        const mlir::Value key = load.getKey();
        const mlir::Value replacement = store.getValue();
        const mlir::Value first =
            llvm::cast<ctjs::CreateArrayOp>(base.getDefiningOp()).getElements()[0];
        const mlir::Value parameter = function.getBody().front().getArgument(3);
        budgets += checkArrayRetention(*module, mutation);
        exit->setOperand(0, load.getResult());
        mutation.discharged = "y";
        budgets += checkArrayRetention(*module, mutation);
        store->setOperand(2, first);
        mutation.discharged = "";
        budgets += checkArrayRetention(*module, mutation);
        store->setOperand(2, replacement);
        load->setOperand(1, parameter);
        mutation.complete = false;
        budgets += checkArrayRetention(*module, mutation);
        load->setOperand(1, key);
        load->setOperand(0, parameter);
        budgets += checkArrayRetention(*module, mutation);
        load->setOperand(0, base);
        exit->setOperand(0, base);
        mlir::OpBuilder builder(exit);
        auto publication = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", base);
        base.getDefiningOp()->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        base.getDefiningOp()->setAttr("ctnative.confined", builder.getUnitAttr());
        budgets += checkArrayRetention(*module, mutation);
        publication.erase();
        // Forged markers remain present. Only restoring the complete current
        // operation/contents graph permits the same refinement again.
        mutation.complete = true;
        mutation.discharged = "x";
        budgets += checkArrayRetention(*module, mutation);
    } else {
        fail(row{.what = mutation.what, .body = mutation.body, .expected = mutation.discharged},
             "the live retention mutation fixture did not parse");
    }
    std::printf("array retention: %zu rows, seven live states, %zu budget cutoffs\n", rows.size(),
                budgets);
}

void checkArrayFrames(mlir::MLIRContext & context) {
    const std::string enter = "  %frame = ctjs.frame_enter 4\n";
    const std::string array =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n"
        "  ctjs.append %x to %a\n";
    const std::string root = "  ctjs.root %x in %frame\n  ctjs.root %a in %frame\n";
    const std::string leave = "  ctjs.frame_exit %frame\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::vector<contents_row> rows = {
        {.what = "an imported frame with exact local roots discharges private elements",
         .body = enter + array + root + leave + done,
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
        {.what = "matching frame exit does not discharge a returned container's child",
         .body = enter + array + root + leave + "  ctjs.return %a\n",
         .arrays = "a:[x]",
         .exit = "a -> {a,x}"},
        {.what = "a root of a saved read uses the checked original element",
         .body = enter + array +
                 "  %read = ctjs.get_property %a[%zero]\n"
                 "  ctjs.root %read in %frame\n" +
                 leave + done,
         .arrays = "a:[x]",
         .reads = "a[0]=x",
         .exit = "zero -> {}"},
        {.what = "raw import needs no explicit roots to balance its frame",
         .body = enter + array + leave + done,
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
        {.what = "a successor allocation still belongs to the entry's active frame",
         .body = enter + "  cf.br ^next\n^next:\n" + array + root + leave + done,
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
        {.what = "a two-edge chain forwards the exact frame, array, loaded value and index",
         .body = enter + array +
                 "  cf.br ^next(%frame, %a, %zero : !ctjs.context, !ctjs.value, !ctjs.value)\n"
                 "^next(%active: !ctjs.context, %base: !ctjs.value, %key: !ctjs.value):\n"
                 "  %read = ctjs.get_property %base[%key]\n"
                 "  ctjs.root %read in %active\n  ctjs.set_property %base[%key], %zero\n"
                 "  cf.br ^exit(%read, %active : !ctjs.value, !ctjs.context)\n"
                 "^exit(%saved: !ctjs.value, %closing: !ctjs.context):\n"
                 "  ctjs.root %saved in %closing\n  ctjs.frame_exit %closing\n"
                 "  ctjs.return %saved\n",
         .arrays = "a:[zero]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        {.what = "two forwarded aliases update one array even when block layout differs",
         .body = enter + array + "  cf.br ^next(%a, %a : !ctjs.value, !ctjs.value)\n^exit:\n" +
                 leave + done +
                 "^next(%first: !ctjs.value, %second: !ctjs.value):\n"
                 "  ctjs.append %x to %first\n  %read = ctjs.get_property %second[%zero]\n"
                 "  ctjs.root %read in %frame\n  cf.br ^exit\n",
         .arrays = "a:[x,x]",
         .reads = "a[0]=x",
         .exit = "zero -> {}"},
        {.what = "an unknown forwarded value refuses even when its block argument is unused",
         .body = enter + array +
                 "  cf.br ^next(%p : !ctjs.value)\n"
                 "^next(%unused: !ctjs.value):\n" +
                 leave + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "frame exit before a branch cannot release roots used by a later block",
         .body = enter + array + leave + "  cf.br ^next\n^next:\n" + root + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "entry in a successor is too late for the frame-failure proof",
         .body = "  cf.br ^next\n^next:\n" + enter + array + leave + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "a successor's late raw-frame capture invalidates the whole chain",
         .body = enter + array + "  cf.br ^next\n^next:\n  %args = ctjs.make_arguments\n" + leave +
                 done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "the imported unreachable default-return block keeps private children local",
         .body = enter + array + leave + done +
                 "^dead(%unused: !ctjs.value):\n"
                 "  %undefined = ctjs.constant #ctjs.undefined\n" +
                 leave + "  ctjs.return %undefined\n",
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
        {.what = "dead publication, raw-frame capture and unknown effects cannot run",
         .body = enter + array + root + leave + done +
                 "^dead:\n"
                 "  ctjs.store_global \"held\", %a\n"
                 "  %args = ctjs.make_arguments\n"
                 "  \"test.retain_frame\"(%frame) : (!ctjs.context) -> ()\n" +
                 leave + done,
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
        {.what = "a real edge to the publication block refuses independent contents",
         .body = enter + array + root +
                 "  cf.br ^next\n^next:\n"
                 "  ctjs.store_global \"held\", %a\n" +
                 leave + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "frame entry after an allocation cannot borrow the entry-failure proof",
         .body = array + enter + root + leave + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "even a constant before frame entry stays outside the importer shape",
         .body = "  %early = ctjs.constant #ctjs.undefined\n" + enter + array + leave + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "negative frame size refuses the complete query",
         .body = "  %frame = ctjs.frame_enter -1\n" + array + leave + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "a second active frame is not this activation's root window",
         .body = enter + array + "  %other = ctjs.frame_enter 4\n" + leave + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "an unclosed frame cannot discard its retained register window",
         .body = enter + array + root + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "double exit cannot pop a caller frame",
         .body = enter + array + leave + leave + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "a root cannot write a dead frame window",
         .body = enter + array + leave + root + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "an operation after frame exit cannot allocate in a caller frame",
         .body = enter + array + leave + "  %late = ctjs.create_object\n" + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "an unknown rooted value cannot borrow complete local contents",
         .body = enter + array + "  ctjs.root %p in %frame\n" + leave + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "an unknown frame-handle user may keep the whole root window",
         .body = enter + array + root +
                 "  \"test.retain_frame\"(%frame) : (!ctjs.context) -> ()\n" + leave + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "balanced roots do not excuse a late call",
         .body = enter + array + root + "  %called = ctjs.call %p(%q)\n" + leave + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "balanced roots do not excuse implicit arguments retention",
         .body = enter + array + root + "  %args = ctjs.make_arguments\n" + leave + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const contents_row & expected) {
        checkArrayContents(module, expected);
        const bool complete = expected.failure == ArrayContentsFailure::None;
        budgets += checkArrayRetention(
            module,
            {.what = expected.what,
             .body = expected.body,
             .discharged = complete && llvm::StringRef(expected.exit) == "zero -> {}" ? "x" : "",
             .complete = complete});
    };
    for (const contents_row & expected : rows) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.body + "}\n", &context);
        if (module) {
            check(*module, expected);
        } else {
            fail(row{.what = expected.what, .body = expected.body, .expected = ""},
                 "the frame fixture did not parse");
        }
    }

    // Rebuild both solver and queries after each live edit. Forged completion
    // markers never supply a missing frame, root origin or retention proof.
    contents_row mutation = rows.front();
    mutation.what = "live imported frame, root and exit mutations rebuild every proof";
    mutation.body += "^dead:\n  ctjs.store_global \"held\", %a\n" + done;
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.body + "}\n", &context);
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::FrameEnterOp entered;
        ctjs::FrameExitOp exited;
        ctjs::RootOp rooted;
        module->walk([&](ctjs::FrameEnterOp op) { entered = op; });
        module->walk([&](ctjs::FrameExitOp op) { exited = op; });
        module->walk([&](ctjs::RootOp op) { rooted = op; });
        const mlir::Value value = rooted.getValue();
        mlir::OpBuilder builder(exited);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        value.getDefiningOp()->setAttr("ctnative.confined", builder.getUnitAttr());
        check(*module, mutation);
        rooted->setOperand(1, function.getBody().front().getArgument(3));
        mutation.failure = ArrayContentsFailure::UnknownValue;
        check(*module, mutation);
        rooted->setOperand(1, value);
        exited->moveBefore(rooted);
        mutation.failure = ArrayContentsFailure::InvalidFrame;
        check(*module, mutation);
        exited->moveBefore(function.getBody().front().getTerminator());
        entered->moveAfter(value.getDefiningOp());
        check(*module, mutation);
        entered->moveBefore(&function.getBody().front().front());
        auto published = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", value);
        mutation.failure = ArrayContentsFailure::UnsupportedOperation;
        check(*module, mutation);
        published.erase();
        mutation.failure = ArrayContentsFailure::None;
        check(*module, mutation);

        // Make the previously dead publication reachable in the same IR.
        // Even with forged markers, the new edge must invalidate refinement.
        mlir::Operation * returned = function.getBody().front().getTerminator();
        const mlir::Value result = returned->getOperand(0);
        mlir::Block & dead = function.getBody().back();
        builder.setInsertionPoint(returned);
        const mlir::Value argument = dead.addArgument(value.getType(), function.getLoc());
        dead.front().setOperand(0, argument);
        auto edge =
            mlir::cf::BranchOp::create(builder, function.getLoc(), &dead, mlir::ValueRange{value});
        returned->erase();
        exited->moveBefore(dead.getTerminator());
        mutation.failure = ArrayContentsFailure::UnsupportedOperation;
        check(*module, mutation);
        dead.front().erase();
        mutation.failure = ArrayContentsFailure::None;
        check(*module, mutation);
        edge->setOperand(0, function.getBody().front().getArgument(3));
        mutation.failure = ArrayContentsFailure::UnknownValue;
        check(*module, mutation);
        edge->setOperand(0, value);
        mutation.failure = ArrayContentsFailure::None;
        check(*module, mutation);
        builder.setInsertionPointToStart(&dead);
        ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", argument);
        mutation.failure = ArrayContentsFailure::UnsupportedOperation;
        check(*module, mutation);
        builder.setInsertionPoint(edge);
        auto restored = ctjs::ReturnOp::create(builder, function.getLoc(), result);
        edge.erase();
        exited->moveBefore(restored);
        mutation.failure = ArrayContentsFailure::None;
        check(*module, mutation);
    } else {
        fail(row{.what = mutation.what, .body = mutation.body, .expected = ""},
             "the live frame mutation fixture did not parse");
    }
    std::printf("array frames: %zu rows, twelve live states, %zu retention budget cutoffs\n",
                rows.size(), budgets);
}

void checkArrayConditionals(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %one = ctjs.constant #ctjs.number<4607182418800017408> "
        "{storage_test_id = \"one\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %y = ctjs.create_object {storage_test_id = \"y\"}\n"
        "  %condition = ctjs.truthy %p\n";
    const std::string array = values + "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string split = "  cf.cond_br %condition, ^left, ^right\n^left:\n";
    const std::string done = "  ctjs.return %zero\n";
    struct conditional_row {
        contents_row contents;
        const char * discharged = "";
        bool acyclicWrites = true;
    };
    const std::vector<conditional_row> rows = {
        {.contents = {.what = "a child retained on either return path stays Stored",
                      .body = array + split +
                              "  ctjs.set_property %a[%zero], %y\n"
                              "  ctjs.return %a\n^right:\n  ctjs.return %a\n",
                      .arrays = "a:[y] | a:[x]",
                      .exit = "a -> {a,y}; a -> {a,x}"}},
        {.contents = {.what = "both arms overwrite the old child before a shared return",
                      .body = array + split +
                              "  ctjs.set_property %a[%zero], %y\n"
                              "  cf.br ^join\n^right:\n"
                              "  ctjs.set_property %a[%zero], %zero\n"
                              "  cf.br ^join\n^join:\n  ctjs.return %a\n",
                      .arrays = "a:[y] | a:[zero]",
                      .exit = "a -> {a,y}; a -> {a}"},
         .discharged = "x"},
        {.contents = {.what = "duplicate successor edges keep their own overwrite target",
                      .body = array +
                              "  %b = ctjs.create_array [%y] {storage_test_id = \"b\"}\n"
                              "  %c = ctjs.create_array [%a, %b] {storage_test_id = \"c\"}\n"
                              "  cf.cond_br %condition, ^join(%a : !ctjs.value), "
                              "^join(%b : !ctjs.value)\n"
                              "^join(%selected: !ctjs.value):\n"
                              "  ctjs.set_property %selected[%zero], %zero\n"
                              "  ctjs.return %c\n",
                      .arrays = "a:[zero]; b:[y]; c:[a,b] | a:[x]; b:[zero]; c:[a,b]",
                      .exit = "c -> {a,b,c,y}; c -> {a,b,c,x}"}},
        {.contents = {.what = "each path's loaded alias retains its saved pre-overwrite child",
                      .body = array + split +
                              "  %saved = ctjs.get_property %a[%zero]\n"
                              "  ctjs.set_property %a[%zero], %y\n"
                              "  cf.br ^join(%saved : !ctjs.value)\n^right:\n"
                              "  ctjs.set_property %a[%zero], %y\n"
                              "  %later = ctjs.get_property %a[%zero]\n"
                              "  cf.br ^join(%later : !ctjs.value)\n"
                              "^join(%result: !ctjs.value):\n"
                              "  ctjs.return %result\n",
                      .arrays = "a:[y] | a:[y]",
                      .reads = "a[0]=x; a[0]=y",
                      .exit = "x -> {x}; y -> {y}"}},
        {.contents = {.what = "successor-local allocations keep exact path-specific origins",
                      .body = values + split +
                              "  %a = ctjs.create_array [%x] "
                              "{storage_test_id = \"a\"}\n"
                              "  cf.br ^join(%a : !ctjs.value)\n^right:\n"
                              "  %b = ctjs.create_array [%y] "
                              "{storage_test_id = \"b\"}\n"
                              "  cf.br ^join(%b : !ctjs.value)\n"
                              "^join(%base: !ctjs.value):\n"
                              "  ctjs.set_property %base[%zero], %zero\n"
                              "  ctjs.return %base\n",
                      .arrays = "a:[zero] | b:[zero]",
                      .exit = "a -> {a}; b -> {b}"},
         .discharged = "x,y"},
        {.contents = {.what = "join-local allocation may contain a different exact value per path",
                      .body = array +
                              "  cf.cond_br %condition, ^join(%x : !ctjs.value), "
                              "^join(%y : !ctjs.value)\n"
                              "^join(%element: !ctjs.value):\n"
                              "  %b = ctjs.create_array [%element] {storage_test_id = \"b\"}\n"
                              "  ctjs.return %b\n",
                      .arrays = "a:[x]; b:[x] | a:[x]; b:[y]",
                      .exit = "b -> {b,x}; b -> {b,y}"}},
        {.contents = {.what = "a missing own slot on one path refuses all earlier successful reads",
                      .body = array + split +
                              "  ctjs.append %y to %a\n"
                              "  cf.br ^join\n^right:\n  cf.br ^join\n^join:\n"
                              "  %read = ctjs.get_property %a[%one]\n"
                              "  ctjs.return %read\n",
                      .failure = ArrayContentsFailure::MissingElement}},
        {.contents = {.what = "unknown unused values on either edge cannot disappear at a join",
                      .body = array +
                              "  cf.cond_br %condition, ^join(%a : !ctjs.value), "
                              "^join(%p : !ctjs.value)\n"
                              "^join(%unused: !ctjs.value):\n" +
                              done,
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "truthy observes an external predicate without proving its contents",
                      .body =
                          array + split + "  ctjs.append %p to %a\n" + done + "^right:\n" + done,
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "a late publication on the second path invalidates the first return",
                      .body = array + split + done +
                              "^right:\n"
                              "  ctjs.store_global \"held\", %a\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a constant predicate cannot conceal an unsupported structural edge",
                      .body = array +
                              "  %constant = ctjs.truthy %zero\n"
                              "  cf.cond_br %constant, ^left, ^right\n^left:\n"
                              "  ctjs.store_global \"held\", %a\n" +
                              done + "^right:\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what =
                          "a loop on one conditional edge does not reuse an allocation identity",
                      .body = array + split + done + "^right:\n  cf.br ^right\n",
                      .failure = ArrayContentsFailure::UnsupportedControlFlow}},
        {.contents = {.what = "a balanced frame is checked separately at both return paths",
                      .body = "  %frame = ctjs.frame_enter 4\n" + array + split +
                              "  ctjs.root %a in %frame\n  ctjs.frame_exit %frame\n" + done +
                              "^right:\n  ctjs.root %x in %frame\n  ctjs.frame_exit %frame\n" +
                              done,
                      .arrays = "a:[x] | a:[x]",
                      .exit = "zero -> {}; zero -> {}"},
         .discharged = "x"},
        {.contents = {.what = "frame and exact predicate forwarding survive a conditional join",
                      .body = "  %frame = ctjs.frame_enter 4\n" + array +
                              "  cf.cond_br %condition, ^join(%frame, %a, %condition : "
                              "!ctjs.context, !ctjs.value, i1), ^join(%frame, %a, %condition : "
                              "!ctjs.context, !ctjs.value, i1)\n"
                              "^join(%active: !ctjs.context, %base: !ctjs.value, %test: i1):\n"
                              "  ctjs.root %base in %active\n  ctjs.frame_exit %active\n" +
                              done,
                      .arrays = "a:[x] | a:[x]",
                      .exit = "zero -> {}; zero -> {}"},
         .discharged = "x"},
        {.contents = {.what = "missing frame exit on one path refuses the complete retention proof",
                      .body = "  %frame = ctjs.frame_enter 4\n" + array + split +
                              "  ctjs.frame_exit %frame\n" + done + "^right:\n" + done,
                      .failure = ArrayContentsFailure::InvalidFrame}},
        {.contents = {.what = "unknown successor frame roots remain unsupported",
                      .body = "  %frame = ctjs.frame_enter 4\n" + array + split +
                              "  ctjs.frame_exit %frame\n" + done +
                              "^right:\n"
                              "  ctjs.root %p in %frame\n  ctjs.frame_exit %frame\n" +
                              done,
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "cycles in the all-path write union remain conservatively refused",
                      .body = values +
                              "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n"
                              "  %b = ctjs.create_array [] {storage_test_id = \"b\"}\n" +
                              split + "  ctjs.append %b to %a\n" + done +
                              "^right:\n  ctjs.append %a to %b\n" + done,
                      .arrays = "a:[b]; b:[] | a:[]; b:[a]",
                      .exit = "zero -> {}; zero -> {}"},
         .acyclicWrites = false},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const conditional_row & expected) {
        checkArrayContents(module, expected.contents);
        budgets += checkArrayRetention(
            module, {.what = expected.contents.what,
                     .body = expected.contents.body,
                     .discharged = expected.discharged,
                     .complete = expected.contents.failure == ArrayContentsFailure::None &&
                                 expected.acyclicWrites});
    };
    for (const auto & expected : rows) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
        if (!module) {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the conditional fixture did not parse");
            continue;
        }
        check(*module, expected);
    }

    // Rebuild from live IR after changing only the second path. The first path
    // still returns the overwritten array; stale completion markers cannot
    // erase the unchanged child's alternate retention or a later publication.
    conditional_row mutation = rows[1];
    mutation.contents.what = "live alternate-path mutations invalidate and restore every proof";
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.contents.body + "}\n", &context);
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp changed;
        module->walk([&](ctjs::SetPropertyOp op) { changed = op; });
        const mlir::Value original = changed.getValue();
        const mlir::Value base = changed.getObject();
        const mlir::Value child = base.getDefiningOp<ctjs::CreateArrayOp>().getElements()[0];
        mlir::OpBuilder builder(changed);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        child.getDefiningOp()->setAttr("ctnative.confined", builder.getUnitAttr());
        check(*module, mutation);
        changed->setOperand(2, child);
        mutation.contents.arrays = "a:[y] | a:[x]";
        mutation.contents.exit = "a -> {a,y}; a -> {a,x}";
        mutation.discharged = "";
        check(*module, mutation);
        changed->setOperand(2, function.getBody().front().getArgument(3));
        mutation.contents.failure = ArrayContentsFailure::UnknownValue;
        check(*module, mutation);
        changed->setOperand(2, original);
        auto publication = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", base);
        mutation.contents.failure = ArrayContentsFailure::UnsupportedOperation;
        check(*module, mutation);
        publication.erase();
        mutation = rows[1];
        check(*module, mutation);
    } else {
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live conditional fixture did not parse");
    }

    // A short CFG may contain exponentially many paths. Work includes the
    // snapshots themselves; exhaustion after completed earlier exits must
    // return no proof and cannot refine even one original Stored verdict.
    std::string expanding = array + "  cf.br ^b0\n";
    for (unsigned i = 0; i < 16; ++i) {
        const std::string next = "^b" + std::to_string(i + 1);
        expanding +=
            "^b" + std::to_string(i) + ":\n  cf.cond_br %condition, " + next + ", " + next + "\n";
    }
    expanding += "^b16:\n" + done;
    auto explosion = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + expanding + "}\n", &context);
    if (explosion) {
        ctjs::FuncOp function = *explosion->getOps<ctjs::FuncOp>().begin();
        mlir::DataFlowSolver solver;
        solver.load<mlir::dataflow::DeadCodeAnalysis>();
        solver.load<mlir::dataflow::SparseConstantPropagation>();
        solver.load<EscapeAnalysis>();
        if (failed(solver.initializeAndRun(*explosion))) {
            fail(row{.what = "conditional path explosion is budgeted",
                     .body = expanding,
                     .expected = ""},
                 "the path budget fixture's solver did not converge");
            return;
        }
        const EscapeVerdicts original = computeVerdicts(solver, function, 0);
        for (std::size_t limit : {0U, 1U, 32U, 128U, 1024U}) {
            const auto result = computeArrayContents(function, limit);
            const EscapeVerdicts refined = computeVerdicts(solver, function, limit);
            if (result.complete || result.failure != ArrayContentsFailure::WorkLimit ||
                result.work != limit || !result.arrays.empty() || !result.reads.empty() ||
                !result.writes.empty() || !result.exits.empty() || refined.arrayRetentionComplete ||
                refined.confinedStoredSites != 0 || refined.arrayRetentionWork != limit ||
                !llvm::all_of(original.sites, [&](const auto & entry) {
                    auto found = refined.sites.find(entry.first);
                    return found != refined.sites.end() &&
                           found->second.reason == entry.second.reason &&
                           found->second.by == entry.second.by &&
                           found->second.position == entry.second.position;
                })) {
                fail(row{.what = "conditional path explosion is budgeted",
                         .body = expanding,
                         .expected = ""},
                     "bounded path enumeration published partial evidence");
            }
        }
    } else {
        fail(row{.what = "conditional path explosion is budgeted",
                 .body = expanding,
                 .expected = ""},
             "the path budget fixture did not parse");
    }
    std::printf("array conditionals: %zu rows, five live states, %zu retention budget cutoffs, "
                "five path-explosion cutoffs\n",
                rows.size(), budgets);
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

    checkArrayContents(context);
    checkArrayRetention(context);
    checkArrayFrames(context);
    checkArrayConditionals(context);

    if (failures != 0) {
        std::printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
