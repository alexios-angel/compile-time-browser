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

#include <type_traits>

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
    const char * objects = nullptr;
    const char * propertyReads = nullptr;
    const char * propertyWrites = nullptr;
    const char * propertyDeletions = nullptr;
    const char * propertyCopies = nullptr;
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
               result.objects.empty() && result.propertyReads.empty() &&
               result.propertyWrites.empty() && result.propertyDeletions.empty() &&
               result.propertyCopies.empty() && result.exits.empty();
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
        std::string objects;
        std::vector<mlir::Operation *> objectSites;
        for (const ArrayContentsExit & edge : contents.exits) {
            if (!objects.empty()) { objects += " | "; }
            bool first = true;
            for (const auto & [object, properties] : edge.objects) {
                if (!first) { objects += "; "; }
                first = false;
                objects += contentsLabel(object) + ":{";
                bool firstProperty = true;
                for (const auto & [key, value] : properties) {
                    if (!firstProperty) { objects += ","; }
                    firstProperty = false;
                    objects += key.getValue().str() + ":" + contentsLabel(value.getDefiningOp());
                }
                objects += "}";
                if (!llvm::is_contained(objectSites, object)) { objectSites.push_back(object); }
            }
        }
        if (contents.objects.size() != objectSites.size() ||
            !llvm::all_of(objectSites, [&](mlir::Operation * site) {
                return llvm::count(contents.objects, site) == 1;
            })) {
            fail(r, "the object inventory differs from the complete path snapshots");
        }
        if (expected.objects != nullptr) { equal("objects", objects, expected.objects); }
        if (expected.propertyReads != nullptr) {
            std::string propertyReads;
            for (const ObjectPropertyRead & read : contents.propertyReads) {
                if (!propertyReads.empty()) { propertyReads += "; "; }
                propertyReads += contentsLabel(read.object) + "[" + read.key.getValue().str() +
                                 "]=" + contentsLabel(read.value.getDefiningOp());
            }
            equal("property reads", propertyReads, expected.propertyReads);
        }
        if (expected.propertyWrites != nullptr) {
            std::string propertyWrites;
            for (const ObjectPropertyWrite & write : contents.propertyWrites) {
                if (!propertyWrites.empty()) { propertyWrites += "; "; }
                propertyWrites +=
                    write.by->getName().getStringRef().str() + "[" +
                    std::to_string(write.position) + "]:" + contentsLabel(write.object) + "[" +
                    write.key.getValue().str() + "]=" + contentsLabel(write.value.getDefiningOp());
            }
            equal("property writes", propertyWrites, expected.propertyWrites);
        }
        if (expected.propertyDeletions != nullptr) {
            std::string propertyDeletions;
            for (const ObjectPropertyDeletion & deletion : contents.propertyDeletions) {
                if (!propertyDeletions.empty()) { propertyDeletions += "; "; }
                propertyDeletions +=
                    deletion.by->getName().getStringRef().str() + ":" +
                    contentsLabel(deletion.object) + "[" + deletion.key.getValue().str() + "]=" +
                    (deletion.value ? contentsLabel(deletion.value.getDefiningOp()) : "absent");
            }
            equal("property deletions", propertyDeletions, expected.propertyDeletions);
        }
        if (expected.propertyCopies != nullptr) {
            std::string propertyCopies;
            for (const ObjectPropertyCopy & copy : contents.propertyCopies) {
                if (!propertyCopies.empty()) { propertyCopies += "; "; }
                propertyCopies += copy.by->getName().getStringRef().str() + ":" +
                                  contentsLabel(copy.source) + "[" + copy.key.getValue().str() +
                                  "] -> " + contentsLabel(copy.target) + "=" +
                                  contentsLabel(copy.value.getDefiningOp());
            }
            equal("property copies", propertyCopies, expected.propertyCopies);
        }
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
        {.what = "contents refuses numeric property coercion on a fresh ordinary object",
         .body = array + "  %read = ctjs.get_property %x[%zero]\n" + done,
         .failure = ArrayContentsFailure::UnknownPropertyKey},
        {.what = "contents refuses a dynamic key",
         .body = array + "  %read = ctjs.get_property %a[%p]\n" + done,
         .failure = ArrayContentsFailure::UnknownIndex},
        {.what = "a direct String array write cannot erase the VM's unchanged child",
         .body = array + read +
                 "  %key = ctjs.constant #ctjs.string<\"0\">\n"
                 "  ctjs.set_property %a[%key], %y\n  ctjs.return %a\n",
         .failure = ArrayContentsFailure::UnknownIndex},
        {.what = "an array-loaded String write key cannot erase an unchanged child",
         .body = array + "  %string = ctjs.constant #ctjs.string<\"0\">\n"
                         "  %keys = ctjs.create_array [%string]\n"
                         "  %key = ctjs.get_property %keys[%zero]\n"
                         "  ctjs.set_property %a[%key], %y\n  ctjs.return %a\n",
         .failure = ArrayContentsFailure::UnknownIndex},
        {.what = "an array-loaded String read key cannot assume a dense own element",
         .body = array + "  %string = ctjs.constant #ctjs.string<\"0\">\n"
                         "  %keys = ctjs.create_array [%string]\n"
                         "  %key = ctjs.get_property %keys[%zero]\n"
                         "  %read = ctjs.get_property %a[%key]\n  ctjs.return %read\n",
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
    // cast. Fractional/NaN/infinite numbers must never borrow a dense slot
    // proof. Number -0 qualifies. Even canonical String "0" stays refused
    // because the VM's named-property path does not access dense array slots.
    const std::vector<std::pair<std::string, bool>> keys = {
        {"#ctjs.number<9223372036854775808>", true}, // -0
        {"#ctjs.string<\"0\">", false},
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

void checkObjectContents(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %key = ctjs.constant #ctjs.string<\"child\">\n"
        "  %other = ctjs.constant #ctjs.string<\"other\">\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %y = ctjs.create_object {storage_test_id = \"y\"}\n"
        "  %o = ctjs.create_object {storage_test_id = \"o\"}\n"
        "  ctjs.set_property %o[%key], %x\n";
    const std::string read = "  %before = ctjs.get_property %o[%key]\n";
    const std::string replace = "  ctjs.set_property %o[%key], %y\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string split = "  %condition = ctjs.truthy %p\n"
                              "  cf.cond_br %condition, ^left, ^right\n^left:\n";
    struct object_row {
        contents_row contents;
        const char * discharged = "";
        bool acyclic = true;
    };
    const std::vector<object_row> rows = {
        {.contents = {.what = "private own object fields discharge their children",
                      .body = values + done,
                      .exit = "zero -> {}",
                      .objects = "x:{}; y:{}; o:{child:x}",
                      .propertyReads = "",
                      .propertyWrites = "ctjs.set_property[2]:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "a returned object retains its current own child",
                      .body = values + "  ctjs.return %o\n",
                      .exit = "o -> {o,x}",
                      .objects = "x:{}; y:{}; o:{child:x}"}},
        {.contents = {.what = "a saved own read keeps its origin after replacement",
                      .body = values + read + replace + "  ctjs.return %before\n",
                      .exit = "x -> {x}",
                      .objects = "x:{}; y:{}; o:{child:y}",
                      .propertyReads = "o[child]=x"},
         .discharged = "y"},
        {.contents = {.what = "returned object replacement preserves every earlier read",
                      .body = values + read + replace +
                              "  %after = ctjs.get_property %o[%key]\n  ctjs.return %o\n",
                      .exit = "o -> {o,y}",
                      .objects = "x:{}; y:{}; o:{child:y}",
                      .propertyReads = "o[child]=x; o[child]=y",
                      .propertyWrites =
                          "ctjs.set_property[2]:o[child]=x; ctjs.set_property[2]:o[child]=y"},
         .discharged = "x"},
        {.contents = {.what = "equal String keys from distinct constants name one own field",
                      .body = values +
                              "  %same = ctjs.constant #ctjs.string<\"child\">\n"
                              "  ctjs.set_property %o[%same], %y\n" +
                              read + "  ctjs.return %before\n",
                      .exit = "y -> {y}",
                      .objects = "x:{}; y:{}; o:{child:y}",
                      .propertyReads = "o[child]=y"},
         .discharged = "x"},
        {.contents = {.what = "an object loaded through an array mutates the same own fields",
                      .body = values + "  %a = ctjs.create_array [%o] {storage_test_id = \"a\"}\n"
                                       "  %alias = ctjs.get_property %a[%zero]\n"
                                       "  ctjs.set_property %alias[%key], %y\n"
                                       "  ctjs.return %a\n",
                      .arrays = "a:[o]",
                      .reads = "a[0]=o",
                      .exit = "a -> {a,o,y}",
                      .objects = "x:{}; y:{}; o:{child:y}"},
         .discharged = "x"},
        {.contents = {.what = "a returned object follows an array field to its current children",
                      .body = values + "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n"
                                       "  ctjs.set_property %o[%key], %a\n"
                                       "  %alias = ctjs.get_property %o[%key]\n"
                                       "  ctjs.set_property %alias[%zero], %y\n"
                                       "  ctjs.return %o\n",
                      .arrays = "a:[y]",
                      .exit = "o -> {a,o,y}",
                      .objects = "x:{}; y:{}; o:{child:a}",
                      .propertyReads = "o[child]=a"},
         .discharged = "x"},
        {.contents = {.what = "an own read stored in a returned array keeps its earlier child",
                      .body = values + read + replace +
                              "  %a = ctjs.create_array [%before] {storage_test_id = \"a\"}\n"
                              "  ctjs.return %a\n",
                      .arrays = "a:[x]",
                      .exit = "a -> {a,x}",
                      .objects = "x:{}; y:{}; o:{child:y}",
                      .propertyReads = "o[child]=x"},
         .discharged = "y"},
        {.contents = {.what = "an absent own field cannot use prototype or builtin lookup",
                      .body = values + "  %read = ctjs.get_property %o[%other]\n" + done,
                      .failure = ArrayContentsFailure::MissingProperty}},
        {.contents = {.what = "constructor lookup still needs an actual own write",
                      .body = values +
                              "  %constructor = ctjs.constant #ctjs.string<\"constructor\">\n"
                              "  %read = ctjs.get_property %o[%constructor]\n" +
                              done,
                      .failure = ArrayContentsFailure::MissingProperty}},
        {.contents = {.what = "literal array append cannot initialize an object property",
                      .body = values + "  ctjs.append %y to %o\n" + done,
                      .failure = ArrayContentsFailure::UnknownArray}},
        {.contents = {.what = "external own-property values remain unknown",
                      .body = values + "  ctjs.set_property %o[%key], %p\n" + done,
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "external property keys cannot borrow fixed own fields",
                      .body = values + "  %read = ctjs.get_property %o[%p]\n" + done,
                      .failure = ArrayContentsFailure::UnknownPropertyKey}},
        {.contents = {.what =
                          "an object-loaded String write key preserves the original array child",
                      .body = values + "  %string = ctjs.constant #ctjs.string<\"0\">\n"
                                       "  ctjs.set_property %o[%other], %string\n"
                                       "  %keyValue = ctjs.get_property %o[%other]\n"
                                       "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n"
                                       "  ctjs.set_property %a[%keyValue], %y\n  ctjs.return %a\n",
                      .failure = ArrayContentsFailure::UnknownIndex}},
        {.contents = {.what = "an object-loaded String read key cannot borrow dense array contents",
                      .body = values + "  %string = ctjs.constant #ctjs.string<\"0\">\n"
                                       "  ctjs.set_property %o[%other], %string\n"
                                       "  %keyValue = ctjs.get_property %o[%other]\n"
                                       "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n"
                                       "  %read = ctjs.get_property %a[%keyValue]\n"
                                       "  ctjs.return %read\n",
                      .failure = ArrayContentsFailure::UnknownIndex}},
        {.contents = {.what = "__proto__ spelling never establishes an ordinary own field",
                      .body = values +
                              "  %proto = ctjs.constant #ctjs.string<\"__proto__\">\n"
                              "  ctjs.set_property %o[%proto], %x\n" +
                              done,
                      .failure = ArrayContentsFailure::UnknownPropertyKey}},
        {.contents = {.what = "a late prototype change invalidates prior own-field evidence",
                      .body = values + read + "  ctjs.set_proto %x on %o\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a late accessor definition invalidates own-field evidence",
                      .body = values + read +
                              "  ctjs.define_accessor \"child\" on %o get %p set %q\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "private own-field deletion preserves its saved read origin",
                      .body = values + read + "  ctjs.delete_property %o[%key]\n" + done,
                      .exit = "zero -> {}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyReads = "o[child]=x",
                      .propertyWrites = "ctjs.set_property[2]:o[child]=x",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "an unrelated late call still invalidates own-field evidence",
                      .body = values + read + "  %call = ctjs.call %p(%q)\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "late publication cannot hide behind an earlier own read",
                      .body = values + read + "  ctjs.store_global \"held\", %before\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "an own-field self cycle terminates without selecting an owner",
                      .body = values + "  ctjs.set_property %o[%key], %o\n  ctjs.return %o\n",
                      .exit = "o -> {o}",
                      .objects = "x:{}; y:{}; o:{child:o}"},
         .acyclic = false},
        {.contents = {.what = "an object-to-object cycle enters the complete all-write graph",
                      .body = values + "  ctjs.set_property %x[%key], %o\n" + done,
                      .exit = "zero -> {}",
                      .objects = "x:{child:o}; y:{}; o:{child:x}"},
         .acyclic = false},
        {.contents = {.what = "a transient mixed array-object cycle remains refused",
                      .body = values +
                              "  %a = ctjs.create_array [%o] {storage_test_id = \"a\"}\n"
                              "  ctjs.set_property %o[%key], %a\n"
                              "  ctjs.set_property %o[%key], %zero\n" +
                              done,
                      .arrays = "a:[o]",
                      .exit = "zero -> {}",
                      .objects = "x:{}; y:{}; o:{child:zero}"},
         .acyclic = false},
        {.contents = {.what = "both object branches overwrite a child before their shared return",
                      .body = values + split + replace + "  cf.br ^join\n^right:\n" + replace +
                              "  cf.br ^join\n^join:\n  ctjs.return %o\n",
                      .exit = "o -> {o,y}; o -> {o,y}",
                      .objects = "x:{}; y:{}; o:{child:y} | x:{}; y:{}; o:{child:y}"},
         .discharged = "x"},
        {.contents = {.what = "an untouched object branch retains its original child",
                      .body = values + split + replace +
                              "  ctjs.return %o\n^right:\n  ctjs.return %o\n",
                      .exit = "o -> {o,y}; o -> {o,x}",
                      .objects = "x:{}; y:{}; o:{child:y} | x:{}; y:{}; o:{child:x}"}},
        {.contents = {.what = "a joined object target cannot overwrite both incoming objects",
                      .body = values +
                              "  %b = ctjs.create_object {storage_test_id = \"b\"}\n"
                              "  ctjs.set_property %b[%key], %y\n"
                              "  %a = ctjs.create_array [%o, %b] {storage_test_id = \"a\"}\n"
                              "  %condition = ctjs.truthy %p\n"
                              "  cf.cond_br %condition, ^join(%o : !ctjs.value), "
                              "^join(%b : !ctjs.value)\n"
                              "^join(%selected: !ctjs.value):\n"
                              "  ctjs.set_property %selected[%key], %zero\n"
                              "  ctjs.return %a\n",
                      .arrays = "a:[o,b] | a:[o,b]",
                      .exit = "a -> {a,b,o,y}; a -> {a,b,o,x}",
                      .objects = "x:{}; y:{}; o:{child:zero}; b:{child:y} | "
                                 "x:{}; y:{}; o:{child:x}; b:{child:zero}"}},
        {.contents = {.what = "one missing own field discards a previous path's successful read",
                      .body = values + split +
                              "  ctjs.set_property %o[%other], %y\n"
                              "  cf.br ^join\n^right:\n  cf.br ^join\n"
                              "^join:\n"
                              "  %read = ctjs.get_property %o[%other]\n" +
                              done,
                      .failure = ArrayContentsFailure::MissingProperty}},
        {.contents = {.what = "unknown second-path own values discard an earlier complete exit",
                      .body = values + split + done +
                              "^right:\n  ctjs.set_property %o[%key], %p\n" + done,
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "mutually exclusive mixed edges still refuse a cycle owner",
                      .body = values + "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n" +
                              split + "  ctjs.set_property %o[%key], %a\n" + done +
                              "^right:\n  ctjs.append %o to %a\n" + done,
                      .arrays = "a:[] | a:[o]",
                      .exit = "zero -> {}; zero -> {}",
                      .objects = "x:{}; y:{}; o:{child:a} | x:{}; y:{}; o:{child:x}"},
         .acyclic = false},
        {.contents = {.what = "matching imported frame roots do not publish private object fields",
                      .body = "  %frame = ctjs.frame_enter 4\n" + values +
                              "  ctjs.root %o in %frame\n  ctjs.root %x in %frame\n"
                              "  ctjs.frame_exit %frame\n" +
                              done,
                      .exit = "zero -> {}"},
         .discharged = "x"},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const object_row & expected) {
        checkArrayContents(module, expected.contents);
        budgets += checkArrayRetention(
            module, {.what = expected.contents.what,
                     .body = expected.contents.body,
                     .discharged = expected.discharged,
                     .complete = expected.contents.failure == ArrayContentsFailure::None &&
                                 expected.acyclic});
    };
    const auto run = [&](const object_row & expected) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
        if (!module) {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the object contents fixture did not parse");
            return;
        }
        check(*module, expected);
    };
    for (const object_row & expected : rows) { run(expected); }

    const std::vector<std::pair<std::string, bool>> keys = {
        {"#ctjs.string<\"\">", true},
        {"#ctjs.string<\"0\">", true},
        {"#ctjs.string<\"00\">", true},
        {"#ctjs.string<\"length\">", true},
        {"#ctjs.string<\"constructor\">", true},
        {"#ctjs.string<\"x\\00y\">", true},
        {"#ctjs.string<\"" + std::string(256, 'k') + "\">", true},
        {"#ctjs.string<\"" + std::string(257, 'k') + "\">", false},
        {"#ctjs.string<\"__proto__\">", false},
        {"#ctjs.number<0>", false},
        {"#ctjs.boolean<true>", false},
    };
    for (const auto & [key, supported] : keys) {
        run({.contents = {.what = "fixed object keys are bounded Strings without coercion",
                          .body = values + "  %fixed = ctjs.constant " + key +
                                  "\n  ctjs.set_property %o[%fixed], %y\n"
                                  "  %result = ctjs.get_property %o[%fixed]\n"
                                  "  ctjs.return %result\n",
                          .failure = supported ? ArrayContentsFailure::None
                                               : ArrayContentsFailure::UnknownPropertyKey,
                          .exit = supported ? "y -> {y}" : ""},
             .discharged = supported ? "x" : ""});
    }

    object_row mutation = rows[3];
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.contents.body + "}\n", &context);
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp changed;
        ctjs::GetPropertyOp readOp;
        module->walk([&](ctjs::SetPropertyOp op) { changed = op; });
        module->walk([&](ctjs::GetPropertyOp op) { readOp = op; });
        const mlir::Value original = changed.getValue();
        const mlir::Value base = changed.getObject();
        const mlir::Value key = readOp.getKey();
        const mlir::Value parameter = function.getBody().front().getArgument(3);
        mlir::Value child;
        mlir::Value other;
        module->walk([&](ctjs::CreateObjectOp op) {
            if (contentsLabel(op) == "x") { child = op.getResult(); }
        });
        module->walk([&](ctjs::ConstantOp op) {
            auto string = llvm::dyn_cast<ctjs::StringAttr>(op.getValue());
            if (string && string.getValue() == "other") { other = op.getResult(); }
        });
        mlir::OpBuilder builder(function.getBody().front().getTerminator());
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        child.getDefiningOp()->setAttr("ctnative.confined", builder.getUnitAttr());
        check(*module, mutation);
        changed->setOperand(2, child);
        mutation.contents.objects = "x:{}; y:{}; o:{child:x}";
        mutation.contents.exit = "o -> {o,x}";
        mutation.contents.propertyReads = "o[child]=x; o[child]=x";
        mutation.contents.propertyWrites = nullptr;
        mutation.discharged = "";
        check(*module, mutation);
        changed->setOperand(2, parameter);
        mutation.contents.failure = ArrayContentsFailure::UnknownValue;
        check(*module, mutation);
        changed->setOperand(2, original);
        readOp->setOperand(1, other);
        mutation.contents.failure = ArrayContentsFailure::MissingProperty;
        check(*module, mutation);
        readOp->setOperand(1, parameter);
        mutation.contents.failure = ArrayContentsFailure::UnknownPropertyKey;
        check(*module, mutation);
        readOp->setOperand(1, key);
        readOp->setOperand(0, parameter);
        mutation.contents.failure = ArrayContentsFailure::UnknownArray;
        check(*module, mutation);
        readOp->setOperand(0, base);
        changed->setOperand(0, child);
        mutation.contents.failure = ArrayContentsFailure::None;
        mutation.contents.objects = "x:{child:y}; y:{}; o:{child:x}";
        mutation.contents.exit = "o -> {o,x,y}";
        check(*module, mutation);
        changed->setOperand(0, base);
        auto publication = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", base);
        mutation.contents.failure = ArrayContentsFailure::UnsupportedOperation;
        check(*module, mutation);
        publication.erase();
        mutation = rows[3];
        check(*module, mutation);
    } else {
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live object contents fixture did not parse");
    }
    object_row keyMutation{
        .contents = {.what = "live Number-to-String array keys invalidate forged contents proofs",
                     .body = values + "  ctjs.set_property %o[%other], %zero\n"
                                      "  %keyValue = ctjs.get_property %o[%other]\n"
                                      "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n"
                                      "  ctjs.set_property %a[%keyValue], %y\n  ctjs.return %a\n",
                     .arrays = "a:[y]",
                     .exit = "a -> {a,y}",
                     .objects = "x:{}; y:{}; o:{child:x,other:zero}",
                     .propertyReads = "o[other]=zero"},
        .discharged = "x"};
    auto keyModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + keyMutation.contents.body + "}\n", &context);
    if (keyModule) {
        ctjs::FuncOp function = *keyModule->getOps<ctjs::FuncOp>().begin();
        ctjs::ConstantOp index;
        keyModule->walk([&](ctjs::ConstantOp op) {
            if (llvm::isa<ctjs::NumberAttr>(op.getValue())) { index = op; }
        });
        const mlir::Attribute number = index.getValue();
        mlir::OpBuilder builder(function);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.confined", builder.getUnitAttr());
        check(*keyModule, keyMutation);
        index->setAttr("value", ctjs::StringAttr::get(&context, "0"));
        keyMutation.contents.failure = ArrayContentsFailure::UnknownIndex;
        keyMutation.discharged = "";
        check(*keyModule, keyMutation);
        index->setAttr("value", number);
        keyMutation.contents.failure = ArrayContentsFailure::None;
        keyMutation.discharged = "x";
        check(*keyModule, keyMutation);
    } else {
        fail(row{.what = keyMutation.contents.what,
                 .body = keyMutation.contents.body,
                 .expected = ""},
             "the live array key mutation fixture did not parse");
    }
    std::printf("object contents/retention: %zu rows, %zu key controls, twelve live states, "
                "%zu retention budget cutoffs\n",
                rows.size(), keys.size(), budgets);
}

void checkObjectDeletions(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %key = ctjs.constant #ctjs.string<\"child\"> {storage_test_id = \"key\"}\n"
        "  %other = ctjs.constant #ctjs.string<\"other\">\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %y = ctjs.create_object {storage_test_id = \"y\"}\n"
        "  %o = ctjs.create_object {storage_test_id = \"o\"}\n"
        "  ctjs.set_property %o[%key], %x\n";
    const std::string read = "  %saved = ctjs.get_property %o[%key]\n";
    const std::string computed = "  ctjs.delete_property %o[%key]\n";
    const std::string named = "  ctjs.delete_named \"child\" from %o\n";
    const std::string returned = "  ctjs.return %o\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string split = "  %condition = ctjs.truthy %p\n"
                              "  cf.cond_br %condition, ^left, ^right\n^left:\n";
    struct deletion_row {
        contents_row contents;
        const char * discharged = "";
        bool acyclic = true;
    };
    const std::vector<deletion_row> rows = {
        {.contents = {.what = "computed deletion removes a child from the returned object",
                      .body = values + computed + returned,
                      .exit = "o -> {o}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyWrites = "ctjs.set_property[2]:o[child]=x",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "named deletion preserves a saved child returned directly",
                      .body = values + read + named + "  ctjs.return %saved\n",
                      .exit = "x -> {x}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyReads = "o[child]=x",
                      .propertyWrites = "ctjs.set_property[2]:o[child]=x",
                      .propertyDeletions = "ctjs.delete_named:o[child]=x"}},
        {.contents = {.what = "a returned array keeps a saved own read across deletion",
                      .body = values + read + computed +
                              "  %a = ctjs.create_array [%saved] {storage_test_id = \"a\"}\n"
                              "  ctjs.return %a\n",
                      .arrays = "a:[x]",
                      .exit = "a -> {a,x}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyReads = "o[child]=x",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x"}},
        {.contents = {.what = "repeated named and computed deletion records exact absence",
                      .body = values + named + computed + named + returned,
                      .exit = "o -> {o}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyWrites = "ctjs.set_property[2]:o[child]=x",
                      .propertyDeletions = "ctjs.delete_named:o[child]=x; "
                                           "ctjs.delete_property:o[child]=absent; "
                                           "ctjs.delete_named:o[child]=absent"},
         .discharged = "x"},
        {.contents = {.what = "absent own deletion does not consult a builtin or prototype",
                      .body = values +
                              "  ctjs.delete_named \"constructor\" from %o\n"
                              "  ctjs.delete_property %o[%other]\n" +
                              returned,
                      .exit = "o -> {o,x}",
                      .objects = "x:{}; y:{}; o:{child:x}",
                      .propertyDeletions = "ctjs.delete_named:o[constructor]=absent; "
                                           "ctjs.delete_property:o[other]=absent"}},
        {.contents = {.what = "deletion and reinsertion preserve unrelated own fields",
                      .body = values + "  ctjs.set_property %o[%other], %y\n" + computed +
                              "  ctjs.set_property %o[%key], %y\n" + read + returned,
                      .exit = "o -> {o,y}",
                      .objects = "x:{}; y:{}; o:{other:y,child:y}",
                      .propertyReads = "o[child]=y",
                      .propertyWrites = "ctjs.set_property[2]:o[child]=x; "
                                        "ctjs.set_property[2]:o[other]=y; "
                                        "ctjs.set_property[2]:o[child]=y",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "a saved object alias still receives writes after its field deletion",
                      .body = values + read + named +
                              "  ctjs.set_property %saved[%other], %y\n"
                              "  ctjs.return %saved\n",
                      .exit = "x -> {x,y}",
                      .objects = "x:{other:y}; y:{}; o:{}",
                      .propertyReads = "o[child]=x",
                      .propertyDeletions = "ctjs.delete_named:o[child]=x"}},
        {.contents = {.what = "deletion through an array-loaded object alias updates one object",
                      .body = values + "  %a = ctjs.create_array [%o] {storage_test_id = \"a\"}\n"
                                       "  %alias = ctjs.get_property %a[%zero]\n"
                                       "  ctjs.delete_named \"child\" from %alias\n"
                                       "  ctjs.return %a\n",
                      .arrays = "a:[o]",
                      .reads = "a[0]=o",
                      .exit = "a -> {a,o}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyDeletions = "ctjs.delete_named:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "deletion through an object-loaded alias updates its own snapshot",
                      .body = values + "  ctjs.set_property %y[%other], %o\n"
                                       "  %alias = ctjs.get_property %y[%other]\n"
                                       "  ctjs.delete_property %alias[%key]\n"
                                       "  ctjs.return %y\n",
                      .exit = "y -> {o,y}",
                      .objects = "x:{}; y:{other:o}; o:{}",
                      .propertyReads = "y[other]=o",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "a saved String key remains usable after its own field is deleted",
                      .body = values +
                              "  ctjs.set_property %o[%other], %key\n"
                              "  %savedKey = ctjs.get_property %o[%other]\n"
                              "  ctjs.delete_named \"other\" from %o\n"
                              "  ctjs.delete_property %o[%savedKey]\n" +
                              returned,
                      .exit = "o -> {o}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyReads = "o[other]=key",
                      .propertyDeletions = "ctjs.delete_named:o[other]=key; "
                                           "ctjs.delete_property:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "a missing read after deletion refuses the whole contents proof",
                      .body = values + computed + read + returned,
                      .failure = ArrayContentsFailure::MissingProperty}},
        {.contents = {.what = "named deletion cannot authorize a later builtin lookup",
                      .body = values +
                              "  %constructor = ctjs.constant #ctjs.string<\"constructor\">\n"
                              "  ctjs.set_property %o[%constructor], %y\n"
                              "  ctjs.delete_named \"constructor\" from %o\n"
                              "  %read = ctjs.get_property %o[%constructor]\n" +
                              done,
                      .failure = ArrayContentsFailure::MissingProperty}},
        {.contents = {.what = "an undeleted conditional edge still retains the original child",
                      .body = values + split + computed + returned + "^right:\n" + returned,
                      .exit = "o -> {o}; o -> {o,x}",
                      .objects = "x:{}; y:{}; o:{} | x:{}; y:{}; o:{child:x}",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x"}},
        {.contents = {.what = "deleting on both conditional edges discharges the child",
                      .body = values + split + computed + "  cf.br ^join\n^right:\n" + named +
                              "  cf.br ^join\n^join:\n" + returned,
                      .exit = "o -> {o}; o -> {o}",
                      .objects = "x:{}; y:{}; o:{} | x:{}; y:{}; o:{}",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x; "
                                           "ctjs.delete_named:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "a joined deletion target cannot erase both alternative objects",
                      .body = values +
                              "  %b = ctjs.create_object {storage_test_id = \"b\"}\n"
                              "  ctjs.set_property %b[%key], %y\n"
                              "  %a = ctjs.create_array [%o, %b] {storage_test_id = \"a\"}\n"
                              "  %condition = ctjs.truthy %p\n"
                              "  cf.cond_br %condition, ^join(%o : !ctjs.value), "
                              "^join(%b : !ctjs.value)\n"
                              "^join(%selected: !ctjs.value):\n"
                              "  ctjs.delete_property %selected[%key]\n"
                              "  ctjs.return %a\n",
                      .arrays = "a:[o,b] | a:[o,b]",
                      .exit = "a -> {a,b,o,y}; a -> {a,b,o,x}",
                      .objects = "x:{}; y:{}; o:{}; b:{child:y} | "
                                 "x:{}; y:{}; o:{child:x}; b:{}",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x; "
                                           "ctjs.delete_property:b[child]=y"}},
        {.contents = {.what = "all switch edges preserve their exact deletion and exit records",
                      .body = values +
                              "  %flag = ctjs.truthy %p\n"
                              "  cf.switch %flag : i1, [default: ^default, 0: ^left, 1: ^right]\n"
                              "^default:\n" +
                              computed + returned + "^left:\n" + named + returned + "^right:\n" +
                              computed + named + returned,
                      .exit = "o -> {o}; o -> {o}; o -> {o}",
                      .objects = "x:{}; y:{}; o:{} | x:{}; y:{}; o:{} | x:{}; y:{}; o:{}",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x; "
                                           "ctjs.delete_named:o[child]=x; "
                                           "ctjs.delete_property:o[child]=x; "
                                           "ctjs.delete_named:o[child]=absent"},
         .discharged = "x"},
        {.contents = {.what = "a second-path missing read discards earlier deletion records",
                      .body = values + split + computed + returned + "^right:\n" + named + read +
                              returned,
                      .failure = ArrayContentsFailure::MissingProperty}},
        {.contents = {.what = "a second-path unknown delete key discards an earlier complete exit",
                      .body = values + split + computed + returned +
                              "^right:\n  ctjs.delete_property %o[%p]\n" + returned,
                      .failure = ArrayContentsFailure::UnknownPropertyKey}},
        {.contents = {.what = "late publication after deletion preserves every original verdict",
                      .body = values + read + computed + "  ctjs.store_global \"held\", %saved\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "an accessor cannot be hidden by deleting its field",
                      .body = values + "  ctjs.define_accessor \"child\" on %o get %p set %q\n" +
                              named + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "prototype changes after deletion still refuse complete contents",
                      .body = values + named + "  ctjs.set_proto %y on %o\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a deleted self edge remains in the all-write cycle graph",
                      .body = values +
                              "  ctjs.set_property %o[%other], %o\n"
                              "  ctjs.delete_named \"other\" from %o\n" +
                              done,
                      .exit = "zero -> {}",
                      .objects = "x:{}; y:{}; o:{child:x}",
                      .propertyWrites = "ctjs.set_property[2]:o[child]=x; "
                                        "ctjs.set_property[2]:o[other]=o",
                      .propertyDeletions = "ctjs.delete_named:o[other]=o"},
         .acyclic = false},
        {.contents = {.what = "deleting a mixed array-object cycle does not select its owner",
                      .body = values +
                              "  %a = ctjs.create_array [%o] {storage_test_id = \"a\"}\n"
                              "  ctjs.set_property %o[%key], %a\n" +
                              computed + returned,
                      .arrays = "a:[o]",
                      .exit = "o -> {o}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyDeletions = "ctjs.delete_property:o[child]=a"},
         .acyclic = false},
        {.contents = {.what = "imported roots keep saved reads until the matching frame exit",
                      .body = "  %frame = ctjs.frame_enter 4\n" + values + read + computed +
                              "  ctjs.root %saved in %frame\n"
                              "  ctjs.frame_exit %frame\n" +
                              returned,
                      .exit = "o -> {o}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyReads = "o[child]=x",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "an external deletion target cannot borrow local own properties",
                      .body = values + "  ctjs.delete_property %p[%key]\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "computed array deletion cannot claim JavaScript hole semantics",
                      .body = values +
                              "  %a = ctjs.create_array [%x]\n"
                              "  ctjs.delete_property %a[%zero]\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "named array deletion remains outside own-object contents",
                      .body = values +
                              "  %a = ctjs.create_array [%x]\n"
                              "  ctjs.delete_named \"0\" from %a\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "String array deletion cannot borrow ordinary object erasure",
                      .body = values +
                              "  %a = ctjs.create_array [%x]\n"
                              "  %index = ctjs.constant #ctjs.string<\"0\">\n"
                              "  ctjs.delete_property %a[%index]\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "coercing numeric deletion keys remain outside the object proof",
                      .body = values + "  ctjs.delete_property %o[%zero]\n" + returned,
                      .failure = ArrayContentsFailure::UnknownPropertyKey}},
        {.contents = {.what = "coercing Boolean deletion keys remain outside the object proof",
                      .body = values +
                              "  %boolean = ctjs.constant #ctjs.boolean<true>\n"
                              "  ctjs.delete_property %o[%boolean]\n" +
                              returned,
                      .failure = ArrayContentsFailure::UnknownPropertyKey}},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const deletion_row & expected) {
        checkArrayContents(module, expected.contents);
        budgets += checkArrayRetention(
            module, {.what = expected.contents.what,
                     .body = expected.contents.body,
                     .discharged = expected.discharged,
                     .complete = expected.contents.failure == ArrayContentsFailure::None &&
                                 expected.acyclic});
    };
    const auto run = [&](const deletion_row & expected) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
        if (!module) {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the object deletion fixture did not parse");
            return;
        }
        check(*module, expected);
    };
    for (const deletion_row & expected : rows) { run(expected); }

    const std::vector<std::pair<std::string, bool>> keys = {
        {"\"\"", true},
        {"\"0\"", true},
        {"\"00\"", true},
        {"\"length\"", true},
        {"\"constructor\"", true},
        {"\"x\\00y\"", true},
        {"\"" + std::string(256, 'k') + "\"", true},
        {"\"" + std::string(257, 'k') + "\"", false},
        {"\"__proto__\"", false},
    };
    for (const auto & [key, supported] : keys) {
        for (bool isNamed : {false, true}) {
            // Unsupported keys reach the deletion itself, rather than being
            // refused by an earlier write using the same invalid key.
            const std::string prefix = values + "  %fixed = ctjs.constant #ctjs.string<" + key +
                                       ">\n" +
                                       (supported ? "  ctjs.set_property %o[%fixed], %y\n" : "");
            const std::string deletion = isNamed ? "  ctjs.delete_named " + key + " from %o\n"
                                                 : "  ctjs.delete_property %o[%fixed]\n";
            run({.contents = {.what = "both deletion forms validate exact bounded String keys",
                              .body = prefix + deletion + returned,
                              .failure = supported ? ArrayContentsFailure::None
                                                   : ArrayContentsFailure::UnknownPropertyKey,
                              .exit = supported ? "o -> {o,x}" : ""},
                 .discharged = supported ? "y" : ""});
        }
    }

    deletion_row mutation = rows[0];
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.contents.body + "}\n", &context);
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::DeletePropertyOp deletion;
        mlir::Value child;
        mlir::Value other;
        module->walk([&](ctjs::DeletePropertyOp op) { deletion = op; });
        module->walk([&](ctjs::CreateObjectOp op) {
            if (contentsLabel(op) == "x") { child = op.getResult(); }
        });
        module->walk([&](ctjs::ConstantOp op) {
            auto string = llvm::dyn_cast<ctjs::StringAttr>(op.getValue());
            if (string && string.getValue() == "other") { other = op.getResult(); }
        });
        const mlir::Value base = deletion.getObject();
        const mlir::Value key = deletion.getKey();
        const mlir::Value parameter = function.getBody().front().getArgument(3);
        mlir::OpBuilder builder(function.getBody().front().getTerminator());
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        child.getDefiningOp()->setAttr("ctnative.confined", builder.getUnitAttr());
        check(*module, mutation);
        deletion->setOperand(1, other);
        mutation.contents.exit = "o -> {o,x}";
        mutation.contents.objects = "x:{}; y:{}; o:{child:x}";
        mutation.contents.propertyDeletions = "ctjs.delete_property:o[other]=absent";
        mutation.discharged = "";
        check(*module, mutation);
        deletion->setOperand(1, parameter);
        mutation.contents.failure = ArrayContentsFailure::UnknownPropertyKey;
        check(*module, mutation);
        deletion->setOperand(1, key);
        deletion->setOperand(0, parameter);
        mutation.contents.failure = ArrayContentsFailure::UnsupportedOperation;
        check(*module, mutation);
        deletion->setOperand(0, child);
        mutation.contents.failure = ArrayContentsFailure::None;
        mutation.contents.propertyDeletions = "ctjs.delete_property:x[child]=absent";
        check(*module, mutation);
        deletion->setOperand(0, base);
        auto publication = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", base);
        mutation.contents.failure = ArrayContentsFailure::UnsupportedOperation;
        check(*module, mutation);
        publication.erase();
        auto missing = ctjs::GetPropertyOp::create(builder, function.getLoc(),
                                                   ctjs::ValueType::get(&context), base, key);
        mutation.contents.failure = ArrayContentsFailure::MissingProperty;
        check(*module, mutation);
        missing.erase();
        mutation = rows[0];
        check(*module, mutation);
        checkArrayRetention(*module, {.what = "deletion refinement still requires alias lattices",
                                      .body = mutation.contents.body,
                                      .complete = false,
                                      .withAnalysis = false});
    } else {
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live computed deletion fixture did not parse");
    }

    deletion_row namedMutation{
        .contents = {.what = "live named deletion changes invalidate forged second-path proofs",
                     .body = values + split + computed + returned + "^right:\n" + named + returned,
                     .exit = "o -> {o}; o -> {o}",
                     .objects = "x:{}; y:{}; o:{} | x:{}; y:{}; o:{}",
                     .propertyDeletions = "ctjs.delete_property:o[child]=x; "
                                          "ctjs.delete_named:o[child]=x"},
        .discharged = "x"};
    auto namedModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + namedMutation.contents.body + "}\n", &context);
    if (namedModule) {
        ctjs::FuncOp function = *namedModule->getOps<ctjs::FuncOp>().begin();
        ctjs::DeleteNamedOp deletion;
        namedModule->walk([&](ctjs::DeleteNamedOp op) { deletion = op; });
        const mlir::Value base = deletion.getObject();
        const mlir::StringAttr name = deletion.getNameAttr();
        const mlir::Value parameter = function.getBody().front().getArgument(3);
        mlir::OpBuilder builder(function);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.confined", builder.getUnitAttr());
        check(*namedModule, namedMutation);
        deletion->setAttr("name", builder.getStringAttr("other"));
        namedMutation.contents.exit = "o -> {o}; o -> {o,x}";
        namedMutation.contents.objects = "x:{}; y:{}; o:{} | x:{}; y:{}; o:{child:x}";
        namedMutation.contents.propertyDeletions = "ctjs.delete_property:o[child]=x; "
                                                   "ctjs.delete_named:o[other]=absent";
        namedMutation.discharged = "";
        check(*namedModule, namedMutation);
        deletion->setAttr("name", builder.getStringAttr("__proto__"));
        namedMutation.contents.failure = ArrayContentsFailure::UnknownPropertyKey;
        check(*namedModule, namedMutation);
        deletion->setAttr("name", builder.getStringAttr(std::string(257, 'k')));
        check(*namedModule, namedMutation);
        deletion->setAttr("name", name);
        deletion->setOperand(0, parameter);
        namedMutation.contents.failure = ArrayContentsFailure::UnsupportedOperation;
        check(*namedModule, namedMutation);
        deletion->setOperand(0, base);
        namedMutation.contents.failure = ArrayContentsFailure::None;
        namedMutation.contents.exit = "o -> {o}; o -> {o}";
        namedMutation.contents.objects = "x:{}; y:{}; o:{} | x:{}; y:{}; o:{}";
        namedMutation.contents.propertyDeletions = "ctjs.delete_property:o[child]=x; "
                                                   "ctjs.delete_named:o[child]=x";
        namedMutation.discharged = "x";
        check(*namedModule, namedMutation);
    } else {
        fail(row{.what = namedMutation.contents.what,
                 .body = namedMutation.contents.body,
                 .expected = ""},
             "the live named deletion fixture did not parse");
    }
    std::printf("object deletion: %zu rows, %zu key controls, fourteen live states, "
                "one missing-lattice control, "
                "%zu retention budget cutoffs\n",
                rows.size(), keys.size() * 2, budgets);
}

void checkObjectCopies(mlir::MLIRContext & context) {
    struct copy_row {
        contents_row contents;
        const char * discharged = "";
        bool acyclic = true;
    };
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %one = ctjs.constant #ctjs.number<4607182418800017408> "
        "{storage_test_id = \"one\"}\n"
        "  %key = ctjs.constant #ctjs.string<\"child\">\n"
        "  %other = ctjs.constant #ctjs.string<\"other\">\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %y = ctjs.create_object {storage_test_id = \"y\"}\n"
        "  %s = ctjs.create_object {storage_test_id = \"s\"}\n"
        "  %t = ctjs.create_object {storage_test_id = \"t\"}\n"
        "  ctjs.set_property %s[%key], %x\n";
    const std::string copy = "  ctjs.copy_props %s into %t\n";
    const std::string returned = "  ctjs.return %t\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string split = "  %flag = ctjs.truthy %p\n"
                              "  cf.cond_br %flag, ^left, ^right\n^left:\n";
    const std::vector<copy_row> rows = {
        {.contents = {.what = "an own-data copy retains the child without retaining its source",
                      .body = values + copy + returned,
                      .exit = "t -> {t,x}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{child:x}",
                      .propertyWrites = "ctjs.set_property[2]:s[child]=x",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"}},
        {.contents = {.what = "a private copied child can be discharged",
                      .body = values + copy + done,
                      .exit = "zero -> {}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{child:x}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"},
         .discharged = "x"},
        {.contents = {.what = "an empty source copies nothing and preserves target fields",
                      .body = values +
                              "  ctjs.delete_named \"child\" from %s\n"
                              "  ctjs.set_property %t[%other], %y\n" +
                              copy + returned,
                      .exit = "t -> {t,y}",
                      .objects = "x:{}; y:{}; s:{}; t:{other:y}",
                      .propertyCopies = ""},
         .discharged = "x"},
        {.contents = {.what = "copy overwrites equal keys and preserves unrelated target fields",
                      .body = values +
                              "  ctjs.set_property %t[%key], %y\n"
                              "  ctjs.set_property %t[%other], %zero\n" +
                              copy + returned,
                      .exit = "t -> {t,x}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{child:x,other:zero}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"},
         .discharged = "y"},
        {.contents = {.what = "a target read saved before copy keeps the overwritten child",
                      .body = values +
                              "  ctjs.set_property %t[%key], %y\n"
                              "  %saved = ctjs.get_property %t[%key]\n" +
                              copy + "  ctjs.return %saved\n",
                      .exit = "y -> {y}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{child:x}",
                      .propertyReads = "t[child]=y",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"},
         .discharged = "x"},
        {.contents = {.what = "a copied read saved before both deletions keeps its exact origin",
                      .body = values + copy +
                              "  %saved = ctjs.get_property %t[%key]\n"
                              "  ctjs.delete_named \"child\" from %s\n"
                              "  ctjs.delete_named \"child\" from %t\n"
                              "  ctjs.return %saved\n",
                      .exit = "x -> {x}",
                      .objects = "x:{}; y:{}; s:{}; t:{}",
                      .propertyReads = "t[child]=x",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"}},
        {.contents = {.what = "source deletion does not erase a copied target property",
                      .body = values + copy + "  ctjs.delete_named \"child\" from %s\n" + returned,
                      .exit = "t -> {t,x}",
                      .objects = "x:{}; y:{}; s:{}; t:{child:x}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"}},
        {.contents = {.what = "target deletion does not make the source container escape",
                      .body = values + copy + "  ctjs.delete_property %t[%key]\n" + returned,
                      .exit = "t -> {t}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"},
         .discharged = "x"},
        {.contents = {.what = "source replacement does not change an earlier copied origin",
                      .body = values + copy + "  ctjs.set_property %s[%key], %y\n" + returned,
                      .exit = "t -> {t,x}",
                      .objects = "x:{}; y:{}; s:{child:y}; t:{child:x}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"},
         .discharged = "y"},
        {.contents = {.what = "a later copy reads the current source field",
                      .body = values + copy + "  ctjs.set_property %s[%key], %y\n" + copy +
                              "  ctjs.delete_named \"child\" from %s\n" + returned,
                      .exit = "t -> {t,y}",
                      .objects = "x:{}; y:{}; s:{}; t:{child:y}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x; "
                                        "ctjs.copy_props:s[child] -> t=y"},
         .discharged = "x"},
        {.contents = {.what = "copy chains retain one child after earlier containers are emptied",
                      .body = values + copy +
                              "  %b = ctjs.create_object {storage_test_id = \"b\"}\n"
                              "  ctjs.copy_props %t into %b\n"
                              "  ctjs.delete_named \"child\" from %s\n"
                              "  ctjs.delete_named \"child\" from %t\n"
                              "  ctjs.return %b\n",
                      .exit = "b -> {b,x}",
                      .objects = "x:{}; y:{}; s:{}; t:{}; b:{child:x}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x; "
                                        "ctjs.copy_props:t[child] -> b=x"}},
        {.contents = {.what = "self-copy snapshots own data and does not invent a source edge",
                      .body = values + "  ctjs.set_property %s[%other], %y\n"
                                       "  ctjs.copy_props %s into %s\n"
                                       "  ctjs.return %s\n",
                      .exit = "s -> {s,x,y}",
                      .objects = "x:{}; y:{}; s:{child:x,other:y}; t:{}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> s=x; "
                                        "ctjs.copy_props:s[other] -> s=y"}},
        {.contents = {.what = "array-loaded source and target aliases preserve exact identities",
                      .body = values +
                              "  %a = ctjs.create_array [%s, %t] {storage_test_id = \"a\"}\n"
                              "  %source = ctjs.get_property %a[%zero]\n"
                              "  %target = ctjs.get_property %a[%one]\n"
                              "  ctjs.copy_props %source into %target\n"
                              "  ctjs.delete_named \"child\" from %s\n"
                              "  ctjs.return %target\n",
                      .arrays = "a:[s,t]",
                      .reads = "a[0]=s; a[1]=t",
                      .exit = "t -> {t,x}",
                      .objects = "x:{}; y:{}; s:{}; t:{child:x}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"},
         .discharged = "s"},
        {.contents = {.what = "object-loaded copy aliases preserve mixed-container descendants",
                      .body = values + "  ctjs.set_property %y[%key], %s\n"
                                       "  ctjs.set_property %y[%other], %t\n"
                                       "  %source = ctjs.get_property %y[%key]\n"
                                       "  %target = ctjs.get_property %y[%other]\n"
                                       "  ctjs.copy_props %source into %target\n"
                                       "  ctjs.return %y\n",
                      .exit = "y -> {s,t,x,y}",
                      .objects = "x:{}; y:{child:s,other:t}; s:{child:x}; t:{child:x}",
                      .propertyReads = "y[child]=s; y[other]=t",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"}},
        {.contents = {.what = "a copied String key keeps its origin after source replacement",
                      .body = values + "  ctjs.set_property %s[%other], %key\n" + copy +
                              "  ctjs.set_property %s[%other], %zero\n"
                              "  %copiedKey = ctjs.get_property %t[%other]\n"
                              "  ctjs.delete_property %t[%copiedKey]\n" +
                              returned,
                      .exit = "t -> {t}",
                      .objects = "x:{}; y:{}; s:{child:x,other:zero}; t:{other:ctjs.constant}",
                      .propertyReads = "t[other]=ctjs.constant",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x; "
                                        "ctjs.copy_props:s[other] -> t=ctjs.constant"},
         .discharged = "x"},
        {.contents = {.what = "a copied String array index still refuses dense element semantics",
                      .body = values +
                              "  %index = ctjs.constant #ctjs.string<\"0\">\n"
                              "  ctjs.set_property %s[%other], %index\n" +
                              copy +
                              "  %copiedKey = ctjs.get_property %t[%other]\n"
                              "  %a = ctjs.create_array [%x]\n"
                              "  ctjs.set_property %a[%copiedKey], %y\n" +
                              returned,
                      .failure = ArrayContentsFailure::UnknownIndex}},
        {.contents = {.what = "conditional copies keep separate selected source contents",
                      .body = values +
                              "  %b = ctjs.create_object {storage_test_id = \"b\"}\n"
                              "  ctjs.set_property %b[%key], %y\n"
                              "  %flag = ctjs.truthy %p\n"
                              "  cf.cond_br %flag, ^join(%s : !ctjs.value), "
                              "^join(%b : !ctjs.value)\n"
                              "^join(%source: !ctjs.value):\n"
                              "  ctjs.copy_props %source into %t\n" +
                              returned,
                      .exit = "t -> {t,x}; t -> {t,y}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{child:x}; b:{child:y} | "
                                 "x:{}; y:{}; s:{child:x}; t:{child:y}; b:{child:y}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x; "
                                        "ctjs.copy_props:b[child] -> t=y"}},
        {.contents = {.what = "a conditional copy target cannot overwrite both alternatives",
                      .body = values + "  ctjs.set_property %t[%key], %y\n"
                                       "  %a = ctjs.create_array [%s, %t] "
                                       "{storage_test_id = \"a\"}\n"
                                       "  %flag = ctjs.truthy %p\n"
                                       "  cf.cond_br %flag, ^join(%s : !ctjs.value), "
                                       "^join(%t : !ctjs.value)\n"
                                       "^join(%target: !ctjs.value):\n"
                                       "  ctjs.copy_props %s into %target\n"
                                       "  ctjs.return %a\n",
                      .arrays = "a:[s,t] | a:[s,t]",
                      .exit = "a -> {a,s,t,x,y}; a -> {a,s,t,x}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{child:y} | "
                                 "x:{}; y:{}; s:{child:x}; t:{child:x}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> s=x; "
                                        "ctjs.copy_props:s[child] -> t=x"}},
        {.contents = {.what = "all switch source alternatives contribute copied retention",
                      .body = values +
                              "  ctjs.set_property %y[%key], %zero\n"
                              "  %flag = ctjs.truthy %p\n"
                              "  cf.switch %flag : i1, [default: ^join(%s : !ctjs.value), "
                              "0: ^join(%y : !ctjs.value), 1: ^join(%x : !ctjs.value)]\n"
                              "^join(%source: !ctjs.value):\n"
                              "  ctjs.copy_props %source into %t\n" +
                              returned,
                      .exit = "t -> {t,x}; t -> {t}; t -> {t}",
                      .objects = "x:{}; y:{child:zero}; s:{child:x}; t:{child:x} | "
                                 "x:{}; y:{child:zero}; s:{child:x}; t:{child:zero} | "
                                 "x:{}; y:{child:zero}; s:{child:x}; t:{}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x; "
                                        "ctjs.copy_props:y[child] -> t=zero"}},
        {.contents = {.what = "deletion on one source path cannot erase another path's child",
                      .body = values + split + "  ctjs.delete_named \"child\" from %s\n" + copy +
                              returned + "^right:\n" + copy + returned,
                      .exit = "t -> {t}; t -> {t,x}",
                      .objects = "x:{}; y:{}; s:{}; t:{} | x:{}; y:{}; s:{child:x}; t:{child:x}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"}},
        {.contents = {.what = "deleting copied fields on every path can discharge their child",
                      .body = values + copy + split + "  ctjs.delete_property %t[%key]\n" +
                              returned + "^right:\n  ctjs.delete_named \"child\" from %t\n" +
                              returned,
                      .exit = "t -> {t}; t -> {t}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{} | x:{}; y:{}; s:{child:x}; t:{}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"},
         .discharged = "x"},
        {.contents = {.what = "a late unsupported copy path discards all earlier copied records",
                      .body = values + split + copy + returned +
                              "^right:\n  ctjs.copy_props %p into %t\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a literal predicate cannot hide an unsupported source copy",
                      .body = values +
                              "  %false = ctjs.constant #ctjs.boolean<false>\n"
                              "  %flag = ctjs.truthy %false\n"
                              "  cf.cond_br %flag, ^left, ^right\n^left:\n"
                              "  ctjs.copy_props %p into %t\n" +
                              returned + "^right:\n" + copy + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "an empty copy cannot authorize an absent inherited read",
                      .body = values + "  ctjs.delete_named \"child\" from %s\n" + copy +
                              "  %read = ctjs.get_property %t[%key]\n" + returned,
                      .failure = ArrayContentsFailure::MissingProperty}},
        {.contents = {.what = "copied self edges remain in the historical cycle graph",
                      .body = values + "  ctjs.set_property %s[%key], %t\n" + copy +
                              "  ctjs.delete_named \"child\" from %t\n" + done,
                      .exit = "zero -> {}",
                      .objects = "x:{}; y:{}; s:{child:t}; t:{}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=t"},
         .acyclic = false},
        {.contents = {.what = "a transient copied array edge cannot discharge a mixed cycle",
                      .body = values +
                              "  %a = ctjs.create_array [%t] {storage_test_id = \"a\"}\n"
                              "  ctjs.set_property %s[%key], %a\n" +
                              copy + "  ctjs.delete_named \"child\" from %t\n" + done,
                      .arrays = "a:[t]",
                      .exit = "zero -> {}",
                      .objects = "x:{}; y:{}; s:{child:a}; t:{}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=a"},
         .acyclic = false},
        {.contents = {.what = "imported roots release copied contents at the matching frame exit",
                      .body = "  %frame = ctjs.frame_enter 8\n" + values + copy +
                              "  ctjs.root %t in %frame\n  ctjs.frame_exit %frame\n" + done,
                      .exit = "zero -> {}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{child:x}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"},
         .discharged = "x"},
        {.contents = {.what = "copy after an imported frame exit cannot borrow the old window",
                      .body = "  %frame = ctjs.frame_enter 8\n" + values +
                              "  ctjs.frame_exit %frame\n" + copy + done,
                      .failure = ArrayContentsFailure::InvalidFrame}},
        {.contents = {.what = "an external source never supplies enumerable own-data evidence",
                      .body = values + "  ctjs.copy_props %p into %t\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "an external target never borrows the fresh source's ownership",
                      .body = values + "  ctjs.copy_props %s into %p\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "an array source remains outside exact own-object copy contents",
                      .body = values +
                              "  %a = ctjs.create_array [%x]\n"
                              "  ctjs.copy_props %a into %t\n" +
                              returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "an array target cannot be mistaken for an own-data object",
                      .body = values +
                              "  %a = ctjs.create_array [%x]\n"
                              "  ctjs.copy_props %s into %a\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "primitive source no-op behavior supplies no object proof",
                      .body = values + "  ctjs.copy_props %zero into %t\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "primitive targets stay outside object copy evidence",
                      .body = values + "  ctjs.copy_props %s into %zero\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "source accessors refuse before copy can invoke their getters",
                      .body = values + "  ctjs.define_accessor \"child\" on %s get %p set %q\n" +
                              copy + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "target descriptors cannot borrow fresh writable data semantics",
                      .body = values + "  ctjs.define_accessor \"child\" on %t get %p set %q\n" +
                              copy + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a prototype mutation invalidates the complete copy query",
                      .body = values + "  ctjs.set_proto %y on %s\n" + copy + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "late target publication discards copied local contents",
                      .body = values + copy + "  ctjs.store_global \"held\", %t\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "unknown stored values cannot be laundered through an own copy",
                      .body = values + "  ctjs.set_property %s[%other], %p\n" + copy + returned,
                      .failure = ArrayContentsFailure::UnknownValue}},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const copy_row & expected) {
        checkArrayContents(module, expected.contents);
        budgets += checkArrayRetention(
            module, {.what = expected.contents.what,
                     .body = expected.contents.body,
                     .discharged = expected.discharged,
                     .complete = expected.contents.failure == ArrayContentsFailure::None &&
                                 expected.acyclic});
    };
    const auto run = [&](const copy_row & expected) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
        if (!module) {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the own-object copy fixture did not parse");
            return;
        }
        check(*module, expected);
    };
    for (const copy_row & expected : rows) { run(expected); }

    const std::vector<std::pair<std::string, bool>> keys = {
        {"\"\"", true},
        {"\"0\"", true},
        {"\"00\"", true},
        {"\"4294967294\"", true},
        {"\"4294967295\"", true},
        {"\"length\"", true},
        {"\"constructor\"", true},
        {"\"x\\00y\"", true},
        {"\"" + std::string(256, 'k') + "\"", true},
        {"\"" + std::string(257, 'k') + "\"", false},
        {"\"__proto__\"", false},
    };
    for (const auto & [key, supported] : keys) {
        run({.contents = {.what = "copy accepts only previously proved bounded own String keys",
                          .body = values + "  %fixed = ctjs.constant #ctjs.string<" + key +
                                  ">\n"
                                  "  ctjs.set_property %s[%fixed], %y\n" +
                                  copy + returned,
                          .failure = supported ? ArrayContentsFailure::None
                                               : ArrayContentsFailure::UnknownPropertyKey,
                          .exit = supported ? "t -> {t,x,y}" : ""}});
    }

    // Charge a complete field snapshot before its allocation and every copied
    // edge after it. Every cutoff above checks rollback at both kinds of work.
    std::string wide = values;
    std::string wideCopies = "ctjs.copy_props:s[child] -> t=x";
    for (unsigned i = 0; i != 32; ++i) {
        const std::string number = std::to_string(i);
        wide += "  %k" + number + " = ctjs.constant #ctjs.string<\"key" + number +
                "\">\n"
                "  ctjs.set_property %s[%k" +
                number + "], %y\n";
        wideCopies += "; ctjs.copy_props:s[key" + number + "] -> t=y";
    }
    wideCopies += "; ctjs.copy_props:t[child] -> t=x";
    for (unsigned i = 0; i != 32; ++i) {
        wideCopies += "; ctjs.copy_props:t[key" + std::to_string(i) + "] -> t=y";
    }
    run({.contents = {.what = "wide copies charge complete snapshots and every indirect edge",
                      .body = wide + copy + "  ctjs.copy_props %t into %t\n" + done,
                      .exit = "zero -> {}",
                      .propertyCopies = wideCopies.c_str()},
         .discharged = "x,y"});

    copy_row mutation = rows.front();
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.contents.body + "}\n", &context);
    unsigned liveStates = 0;
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::CopyPropsOp copied;
        ctjs::SetPropertyOp stored;
        mlir::Value child;
        mlir::Value alternate;
        module->walk([&](ctjs::CopyPropsOp op) { copied = op; });
        module->walk([&](ctjs::SetPropertyOp op) { stored = op; });
        module->walk([&](ctjs::CreateObjectOp op) {
            if (contentsLabel(op) == "x") { child = op.getResult(); }
            if (contentsLabel(op) == "y") { alternate = op.getResult(); }
        });
        const mlir::Value source = copied.getSource();
        const mlir::Value target = copied.getTarget();
        const mlir::Value parameter = function.getBody().front().getArgument(4);
        mlir::OpBuilder builder(copied);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        copied->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        child.getDefiningOp()->setAttr("ctnative.confined", builder.getUnitAttr());
        const auto current = [&]() {
            ++liveStates;
            check(*module, mutation);
        };
        current();
        copied->setOperand(1, parameter);
        mutation.contents.failure = ArrayContentsFailure::UnsupportedOperation;
        current();
        copied->setOperand(1, source);
        mutation = rows.front();
        current();
        copied->setOperand(0, parameter);
        mutation.contents.failure = ArrayContentsFailure::UnsupportedOperation;
        current();
        copied->setOperand(0, alternate);
        mutation.contents.failure = ArrayContentsFailure::None;
        mutation.contents.exit = "t -> {t}";
        mutation.contents.objects = "x:{}; y:{child:x}; s:{child:x}; t:{}";
        mutation.contents.propertyCopies = "ctjs.copy_props:s[child] -> y=x";
        mutation.discharged = "x";
        current();
        copied->setOperand(0, target);
        copied->setOperand(1, child);
        mutation.contents.objects = "x:{}; y:{}; s:{child:x}; t:{}";
        mutation.contents.propertyCopies = "";
        current();
        copied->setOperand(1, source);
        stored->setOperand(2, target);
        mutation.contents.exit = "t -> {t}";
        mutation.contents.objects = "x:{}; y:{}; s:{child:t}; t:{child:t}";
        mutation.contents.propertyWrites = "ctjs.set_property[2]:s[child]=t";
        mutation.contents.propertyCopies = "ctjs.copy_props:s[child] -> t=t";
        mutation.discharged = "";
        mutation.acyclic = false;
        current();
        stored->setOperand(2, child);
        mutation = rows.front();
        current();
        auto deletion = ctjs::DeleteNamedOp::create(builder, function.getLoc(), source, "child");
        mutation.contents.exit = "t -> {t}";
        mutation.contents.objects = "x:{}; y:{}; s:{}; t:{}";
        mutation.contents.propertyCopies = "";
        mutation.discharged = "x";
        current();
        deletion.erase();
        mutation = rows.front();
        current();
        auto accessor = ctjs::DefineAccessorOp::create(builder, function.getLoc(), source, "child",
                                                       parameter, parameter);
        mutation.contents.failure = ArrayContentsFailure::UnsupportedOperation;
        current();
        accessor.erase();
        mutation = rows.front();
        current();
        builder.setInsertionPoint(function.getBody().front().getTerminator());
        auto publication = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", target);
        mutation.contents.failure = ArrayContentsFailure::UnsupportedOperation;
        current();
        publication.erase();
        mutation = rows.front();
        current();
        budgets +=
            checkArrayRetention(*module, {.what = "copy retention requires current alias lattices",
                                          .body = mutation.contents.body,
                                          .complete = false,
                                          .withAnalysis = false});
    } else {
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live own-object copy fixture did not parse");
    }

    copy_row secondPath{
        .contents = {.what = "a late copy operand edit invalidates all earlier path records",
                     .body = values + split + copy + returned + "^right:\n" + copy + returned,
                     .exit = "t -> {t,x}; t -> {t,x}",
                     .objects = "x:{}; y:{}; s:{child:x}; t:{child:x} | "
                                "x:{}; y:{}; s:{child:x}; t:{child:x}",
                     .propertyCopies = "ctjs.copy_props:s[child] -> t=x; "
                                       "ctjs.copy_props:s[child] -> t=x"}};
    auto branchModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + secondPath.contents.body + "}\n", &context);
    if (branchModule) {
        ctjs::FuncOp function = *branchModule->getOps<ctjs::FuncOp>().begin();
        ctjs::CopyPropsOp finalCopy;
        branchModule->walk([&](ctjs::CopyPropsOp op) { finalCopy = op; });
        const mlir::Value source = finalCopy.getSource();
        mlir::OpBuilder builder(function);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.confined", builder.getUnitAttr());
        check(*branchModule, secondPath);
        finalCopy->setOperand(1, function.getBody().front().getArgument(3));
        secondPath.contents.failure = ArrayContentsFailure::UnsupportedOperation;
        check(*branchModule, secondPath);
        finalCopy->setOperand(1, source);
        secondPath.contents.failure = ArrayContentsFailure::None;
        check(*branchModule, secondPath);
        liveStates += 3;
    } else {
        fail(
            row{.what = secondPath.contents.what, .body = secondPath.contents.body, .expected = ""},
            "the live later-path copy fixture did not parse");
    }

    std::string expanding = values + "  %flag = ctjs.truthy %p\n  cf.br ^branch0\n";
    for (unsigned i = 0; i != 10; ++i) {
        const std::string next = i == 9 ? "exit" : "branch" + std::to_string(i + 1);
        expanding += "^branch" + std::to_string(i) + ":\n" + copy + "  cf.cond_br %flag, ^" + next +
                     ", ^" + next + "\n";
    }
    expanding += "^exit:\n" + done;
    auto expandingModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + expanding + "}\n", &context);
    if (expandingModule) {
        ctjs::FuncOp function = *expandingModule->getOps<ctjs::FuncOp>().begin();
        mlir::DataFlowSolver solver;
        solver.load<mlir::dataflow::DeadCodeAnalysis>();
        solver.load<mlir::dataflow::SparseConstantPropagation>();
        solver.load<EscapeAnalysis>();
        const row r{.what = "copy path explosion rolls back all copied records",
                    .body = expanding,
                    .expected = ""};
        if (failed(solver.initializeAndRun(*expandingModule))) {
            fail(r, "the copy path-budget fixture's solver did not converge");
        } else {
            const EscapeVerdicts original = computeVerdicts(solver, function, 0);
            for (std::size_t limit : {0U, 1U, 32U, 256U, 4096U}) {
                const ArrayContentsEvidence contents = computeArrayContents(function, limit);
                const EscapeVerdicts refined = computeVerdicts(solver, function, limit);
                if (contents.complete || contents.failure != ArrayContentsFailure::WorkLimit ||
                    contents.work != limit || !contents.arrays.empty() ||
                    !contents.objects.empty() || !contents.writes.empty() ||
                    !contents.reads.empty() || !contents.propertyWrites.empty() ||
                    !contents.propertyReads.empty() || !contents.propertyDeletions.empty() ||
                    !contents.propertyCopies.empty() || !contents.exits.empty() ||
                    refined.arrayRetentionComplete || refined.confinedStoredSites != 0 ||
                    refined.arrayRetentionWork != limit ||
                    !llvm::all_of(original.sites, [&](const auto & entry) {
                        auto found = refined.sites.find(entry.first);
                        return found != refined.sites.end() &&
                               found->second.reason == entry.second.reason &&
                               found->second.by == entry.second.by &&
                               found->second.position == entry.second.position;
                    })) {
                    fail(r, "bounded copy paths published partial evidence or verdicts");
                }
            }
        }
    } else {
        fail(row{.what = "copy paths are budgeted", .body = expanding, .expected = ""},
             "the copy path-budget fixture did not parse");
    }
    std::printf("object copies: %zu rows, %zu key controls, %u live states, "
                "one wide snapshot, one missing-lattice control, "
                "%zu retention budget cutoffs, five path-explosion cutoffs\n",
                rows.size(), keys.size(), liveStates, budgets);
}

void checkOpaqueEntryTransport(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %key = ctjs.constant #ctjs.string<\"child\">\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string forward =
        "  cf.br ^next(%p, %q, %a : !ctjs.value, !ctjs.value, !ctjs.value)\n"
        "^next(%opaque: !ctjs.value, %other: !ctjs.value, %base: !ctjs.value):\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::vector<contents_row> rows = {
        {.what = "opaque entry aliases survive two reordered raw register vectors",
         .body = values + forward +
                 "  cf.br ^last(%other, %opaque, %base : !ctjs.value, !ctjs.value, !ctjs.value)\n"
                 "^last(%second: !ctjs.value, %first: !ctjs.value, %array: !ctjs.value):\n"
                 "  %flag = ctjs.truthy %first\n"
                 "  cf.cond_br %flag, ^yes, ^no\n^yes:\n" +
                 done + "^no:\n" + done,
         .arrays = "a:[x] | a:[x]",
         .exit = "zero -> {}; zero -> {}"},
        {.what = "a known and opaque join value may both be tested without merging heap origins",
         .body = values +
                 "  %flag = ctjs.truthy %p\n"
                 "  cf.cond_br %flag, ^join(%x : !ctjs.value), "
                 "^join(%q : !ctjs.value)\n"
                 "^join(%selected: !ctjs.value):\n"
                 "  %test = ctjs.truthy %selected\n" +
                 done,
         .arrays = "a:[x] | a:[x]",
         .exit = "zero -> {}; zero -> {}"},
        {.what = "forwarded opaque entries never become initializer contents",
         .body = values + forward + "  %bad = ctjs.create_array [%opaque]\n" + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "forwarded opaque entries never become appended contents",
         .body = values + forward + "  ctjs.append %opaque to %base\n" + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "forwarded opaque entries never become replacement contents",
         .body = values + forward + "  ctjs.set_property %base[%zero], %opaque\n" + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "forwarded opaque entries never become own-property contents",
         .body = values + forward + "  ctjs.set_property %x[%key], %opaque\n" + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "a forwarded opaque array base remains unknown",
         .body = values + forward + "  %read = ctjs.get_property %opaque[%zero]\n" + done,
         .failure = ArrayContentsFailure::UnknownArray},
        {.what = "a forwarded opaque index remains unknown",
         .body = values + forward + "  %read = ctjs.get_property %base[%opaque]\n" + done,
         .failure = ArrayContentsFailure::UnknownIndex},
        {.what = "a forwarded opaque property key remains unknown",
         .body = values + forward + "  ctjs.set_property %x[%opaque], %zero\n" + done,
         .failure = ArrayContentsFailure::UnknownPropertyKey},
        {.what = "a forwarded opaque deletion key remains unknown",
         .body = values + forward + "  ctjs.delete_property %x[%opaque]\n" + done,
         .failure = ArrayContentsFailure::UnknownPropertyKey},
        {.what = "a forwarded opaque copy source cannot lend own-data evidence",
         .body = values + forward + "  ctjs.copy_props %opaque into %x\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "a forwarded opaque copy target cannot borrow a local owner",
         .body = values + forward + "  ctjs.copy_props %x into %opaque\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "a forwarded opaque return remains outside complete retention",
         .body = values + forward + "  ctjs.return %opaque\n",
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "a forwarded opaque root remains outside the matched frame proof",
         .body = "  %frame = ctjs.frame_enter 8\n" + values + forward +
                 "  ctjs.root %opaque in %frame\n  ctjs.frame_exit %frame\n" + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "opaque forwarding does not authorize an unknown effect",
         .body = values + forward + "  \"test.effect\"(%opaque) : (!ctjs.value) -> ()\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "opaque forwarding does not authorize global publication",
         .body = values + forward + "  ctjs.store_global \"held\", %opaque\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "opaque forwarding does not authorize a call",
         .body = values + forward + "  %called = ctjs.call %opaque(%base)\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "an unsupported producer cannot mint an opaque forwarding origin",
         .body = values +
                 "  %bad = \"test.value\"() : () -> !ctjs.value\n"
                 "  cf.br ^next(%bad : !ctjs.value)\n"
                 "^next(%unused: !ctjs.value):\n" +
                 done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "an opaque branch alternative cannot borrow the known alternative's write",
         .body = values +
                 "  %flag = ctjs.truthy %p\n"
                 "  cf.cond_br %flag, ^join(%x : !ctjs.value), "
                 "^join(%q : !ctjs.value)\n"
                 "^join(%selected: !ctjs.value):\n"
                 "  ctjs.set_property %a[%zero], %selected\n" +
                 done,
         .failure = ArrayContentsFailure::UnknownValue},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const contents_row & expected) {
        checkArrayContents(module, expected);
        const bool complete = expected.failure == ArrayContentsFailure::None;
        budgets += checkArrayRetention(module, {.what = expected.what,
                                                .body = expected.body,
                                                .discharged = complete ? "x" : "",
                                                .complete = complete});
    };
    for (const contents_row & expected : rows) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.body + "}\n", &context);
        if (module) {
            check(*module, expected);
        } else {
            fail(row{.what = expected.what, .body = expected.body, .expected = ""},
                 "the opaque forwarding fixture did not parse");
        }
    }

    // Extra unused entry arguments cost one seed and one opaque entry in the
    // only conditional snapshot. Pin both charges independently of whatever
    // completion budget an accidentally uncharged implementation reports.
    std::string widePrologue = kPrologue;
    std::string extraArguments;
    for (unsigned i = 0; i < 32; ++i) {
        extraArguments += ", %unused_" + std::to_string(i) + ": !ctjs.value";
    }
    widePrologue.insert(widePrologue.find(") -> !ctjs.value"), extraArguments);
    auto narrow = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + rows.front().body + "}\n", &context);
    auto wide =
        mlir::parseSourceString<mlir::ModuleOp>(widePrologue + rows.front().body + "}\n", &context);
    if (narrow && wide) {
        const auto narrowResult = computeArrayContents(*narrow->getOps<ctjs::FuncOp>().begin());
        const auto wideResult = computeArrayContents(*wide->getOps<ctjs::FuncOp>().begin());
        if (!narrowResult.complete || !wideResult.complete ||
            wideResult.work != narrowResult.work + 64) {
            fail(row{.what = "entry seeding and opaque snapshots charge every identity",
                     .body = rows.front().body,
                     .expected = ""},
                 "32 additional opaque entries did not cost 64 work units");
        }
        check(*wide, rows.front());
    } else {
        fail(row{.what = "wide opaque snapshot", .body = rows.front().body, .expected = ""},
             "the opaque snapshot charge fixture did not parse");
    }

    // Prove the live origin class after each edit, including edits to an edge
    // after a previous successful query. Input annotations never repair it.
    contents_row mutation{
        .what = "live opaque and known origins stay separate under forged completion markers",
        .body = "  %frame = ctjs.frame_enter 8\n" + values +
                "  cf.br ^next(%p, %x : !ctjs.value, !ctjs.value)\n"
                "^next(%opaque: !ctjs.value, %known: !ctjs.value):\n"
                "  ctjs.root %known in %frame\n  ctjs.frame_exit %frame\n" +
                done,
        .arrays = "a:[x]",
        .exit = "zero -> {}"};
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.body + "}\n", &context);
    unsigned liveStates = 0;
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        mlir::Block & entry = function.getBody().front();
        mlir::Block & next = function.getBody().back();
        auto branch = llvm::cast<mlir::cf::BranchOp>(entry.getTerminator());
        auto root = llvm::cast<ctjs::RootOp>(&next.front());
        const mlir::Value original = branch.getDestOperands()[1];
        mlir::OpBuilder builder(root);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        original.getDefiningOp()->setAttr("ctnative.confined", builder.getUnitAttr());
        const auto inspect = [&](ArrayContentsFailure failure) {
            mutation.failure = failure;
            check(*module, mutation);
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        root->setOperand(1, next.getArgument(0));
        inspect(ArrayContentsFailure::UnknownValue);
        root->setOperand(1, next.getArgument(1));
        inspect(ArrayContentsFailure::None);
        branch->setOperand(1, entry.getArgument(3));
        inspect(ArrayContentsFailure::UnknownValue);
        branch->setOperand(1, original);
        inspect(ArrayContentsFailure::None);
        const mlir::Value returned = next.getTerminator()->getOperand(0);
        next.getTerminator()->setOperand(0, next.getArgument(0));
        inspect(ArrayContentsFailure::UnknownValue);
        next.getTerminator()->setOperand(0, returned);
        auto published =
            ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", next.getArgument(0));
        inspect(ArrayContentsFailure::UnsupportedOperation);
        published.erase();
        inspect(ArrayContentsFailure::None);
    } else {
        fail(row{.what = mutation.what, .body = mutation.body, .expected = ""},
             "the live opaque forwarding fixture did not parse");
    }
    std::printf("opaque entry transport: %zu rows, %u live states, one wide snapshot, "
                "%zu retention budget cutoffs\n",
                rows.size(), liveStates, budgets);
}

void checkSelectorProducers(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string compare =
        "  %same = ctjs.compare strict_eq %p, %q {storage_test_id = \"same\"}\n";
    const std::string boolean =
        "  %boolean = ctjs.convert to_boolean %same {storage_test_id = \"boolean\"}\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %boolean\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct selector_row {
        contents_row contents;
        const char * discharged = "x";
    };
    const std::vector<selector_row> rows = {
        {.contents = {.what = "source selector producers discharge only after both overwrites",
                      .body =
                          values + compare + boolean + branch + overwrite + "^no:\n" + overwrite,
                      .arrays = "a:[zero] | a:[zero]",
                      .exit = "a -> {a}; a -> {a}"}},
        {.contents = {.what = "strict comparison observes object identity without retaining it",
                      .body = values + "  %same = ctjs.compare strict_eq %x, %p "
                                       "{storage_test_id = \"same\"}\n  ctjs.return %same\n",
                      .arrays = "a:[x]",
                      .exit = "same -> {}"}},
        {.contents = {.what = "ToBoolean returns a primitive without proving an opaque input",
                      .body = values + "  %boolean = ctjs.convert to_boolean %p "
                                       "{storage_test_id = \"boolean\"}\n  ctjs.return %boolean\n",
                      .arrays = "a:[x]",
                      .exit = "boolean -> {}"}},
        {.contents = {.what = "a stored comparison Boolean carries no operand heap origin",
                      .body = values + compare +
                              "  ctjs.set_property %a[%zero], %same\n  ctjs.return %a\n",
                      .arrays = "a:[same]",
                      .exit = "a -> {a}"}},
        {.contents = {.what = "a stored ToBoolean result carries no local operand heap origin",
                      .body = values +
                              "  %boolean = ctjs.convert to_boolean %x "
                              "{storage_test_id = \"boolean\"}\n"
                              "  ctjs.set_property %a[%zero], %boolean\n  ctjs.return %a\n",
                      .arrays = "a:[boolean]",
                      .exit = "a -> {a}"}},
        {.contents = {.what = "the derived Boolean may root while opaque registers only forward",
                      .body = "  %frame = ctjs.frame_enter 8\n" + values + compare + boolean +
                              "  cf.br ^next(%p, %boolean : !ctjs.value, !ctjs.value)\n"
                              "^next(%opaque: !ctjs.value, %test: !ctjs.value):\n"
                              "  ctjs.root %test in %frame\n  ctjs.frame_exit %frame\n"
                              "  ctjs.return %test\n",
                      .arrays = "a:[x]",
                      .exit = "boolean -> {}"}},
        {.contents = {.what = "a selector on known and opaque joins keeps operand origins apart",
                      .body = values + compare +
                              "  %flag = ctjs.truthy %same\n"
                              "  cf.cond_br %flag, ^join(%x : !ctjs.value), "
                              "^join(%p : !ctjs.value)\n"
                              "^join(%selected: !ctjs.value):\n"
                              "  %boolean = ctjs.convert to_boolean %selected\n" +
                              done,
                      .arrays = "a:[x] | a:[x]",
                      .exit = "zero -> {}; zero -> {}"}},
        {.contents = {.what = "the final selector arm preserves its retained child",
                      .body = values + compare + boolean + branch + overwrite +
                              "^no:\n  ctjs.return %a\n",
                      .arrays = "a:[zero] | a:[x]",
                      .exit = "a -> {a}; a -> {a,x}"},
         .discharged = ""},
        {.contents = {.what = "self equality cannot prune an unsupported structural arm",
                      .body = values + "  %same = ctjs.compare strict_eq %x, %x\n" + boolean +
                              branch + done + "^no:\n  ctjs.store_global \"held\", %a\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a comparison Boolean is not an exact array index",
                      .body = values + compare + "  %read = ctjs.get_property %a[%same]\n" + done,
                      .failure = ArrayContentsFailure::UnknownIndex}},
        {.contents = {.what = "a converted Boolean is not an exact own String key",
                      .body = values + compare + boolean +
                              "  ctjs.set_property %x[%boolean], %zero\n" + done,
                      .failure = ArrayContentsFailure::UnknownPropertyKey}},
        {.contents = {.what = "a comparison Boolean cannot become a container",
                      .body =
                          values + compare + "  %read = ctjs.get_property %same[%zero]\n" + done,
                      .failure = ArrayContentsFailure::UnknownArray}},
        {.contents = {.what = "a converted Boolean cannot become a copy endpoint",
                      .body = values + compare + boolean + "  ctjs.copy_props %boolean into %x\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a comparison never authorizes returning its opaque operand",
                      .body = values + compare + "  ctjs.return %p\n",
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "ToBoolean never authorizes storing its opaque operand",
                      .body = values +
                              "  %boolean = ctjs.convert to_boolean %p\n"
                              "  ctjs.append %p to %a\n" +
                              done,
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "ToBoolean never authorizes rooting its opaque operand",
                      .body = "  %frame = ctjs.frame_enter 8\n" + values +
                              "  %boolean = ctjs.convert to_boolean %p\n"
                              "  ctjs.root %p in %frame\n  ctjs.frame_exit %frame\n" +
                              done,
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "a primitive Boolean does not authorize unknown effects",
                      .body = values + compare + boolean +
                              "  \"test.effect\"(%boolean) : (!ctjs.value) -> ()\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "safe selectors cannot conceal an unsupported operand producer",
                      .body = values +
                              "  %bad = \"test.value\"() : () -> !ctjs.value\n"
                              "  %same = ctjs.compare strict_eq %bad, %p\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const selector_row & expected) {
        checkArrayContents(module, expected.contents);
        const bool complete = expected.contents.failure == ArrayContentsFailure::None;
        budgets += checkArrayRetention(module, {.what = expected.contents.what,
                                                .body = expected.contents.body,
                                                .discharged = complete ? expected.discharged : "",
                                                .complete = complete});
    };
    const auto parseAndCheck = [&](const selector_row & expected) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
        if (module) {
            check(*module, expected);
        } else {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the selector producer fixture did not parse");
        }
    };
    for (const auto & expected : rows) { parseAndCheck(expected); }
    // Inspect every other enum kind, including apparently harmless primitive
    // cases: this increment proves no loose equality or coercion behavior.
    const std::vector<std::string> comparisons = {"eq", "lt", "le", "gt", "ge"};
    for (const auto & kind : comparisons) {
        parseAndCheck(
            {.contents = {.what = "all coercing comparison kinds refuse local and opaque values",
                          .body = values + "  %bad = ctjs.compare " + kind + " %x, %p\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}});
    }
    const std::vector<std::string> conversions = {"to_number", "to_string", "to_primitive",
                                                  "to_property_key", "to_object"};
    for (const auto & kind : conversions) {
        parseAndCheck(
            {.contents = {.what = "all non-Boolean conversions refuse opaque values",
                          .body = values + "  %bad = ctjs.convert " + kind + " %p\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}});
    }

    // Every additional primitive origin costs its producer visit and its
    // copied entry in the one path snapshot, even when no later use needs it.
    selector_row wide = rows.front();
    std::string extras;
    for (unsigned i = 0; i < 32; ++i) {
        extras +=
            "  %extra_" + std::to_string(i) +
            (i % 2 == 0 ? " = ctjs.compare strict_eq %p, %q\n" : " = ctjs.convert to_boolean %p\n");
    }
    wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
    auto narrowModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + rows.front().contents.body + "}\n", &context);
    auto wideModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + wide.contents.body + "}\n", &context);
    if (narrowModule && wideModule) {
        const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
        const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
        if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
            fail(row{.what = "selector producers and snapshots charge every primitive origin",
                     .body = wide.contents.body,
                     .expected = ""},
                 "32 extra Boolean origins did not cost 64 work units");
        }
        check(*wideModule, wide);
    } else {
        fail(row{.what = "wide selector snapshot", .body = wide.contents.body, .expected = ""},
             "the selector snapshot charge fixture did not parse");
    }

    selector_row mutation = rows.front();
    mutation.contents.what = "live selector kinds and uses defeat stale forged completion markers";
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.contents.body + "}\n", &context);
    unsigned liveStates = 0;
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::CompareOp comparison;
        ctjs::ConvertOp conversion;
        ctjs::CreateObjectOp child;
        module->walk([&](ctjs::CompareOp op) { comparison = op; });
        module->walk([&](ctjs::ConvertOp op) { conversion = op; });
        module->walk([&](ctjs::CreateObjectOp op) { child = op; });
        mlir::OpBuilder builder(comparison);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        child->setAttr("ctnative.confined", builder.getUnitAttr());
        const auto inspect = [&](ArrayContentsFailure failure) {
            mutation.contents.failure = failure;
            check(*module, mutation);
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        comparison.setKindAttr(ctjs::CompareKindAttr::get(&context, ctjs::CompareKind::Eq));
        inspect(ArrayContentsFailure::UnsupportedOperation);
        comparison.setKindAttr(ctjs::CompareKindAttr::get(&context, ctjs::CompareKind::StrictEq));
        inspect(ArrayContentsFailure::None);
        conversion.setKindAttr(ctjs::ConvertKindAttr::get(&context, ctjs::ConvertKind::ToNumber));
        inspect(ArrayContentsFailure::UnsupportedOperation);
        conversion.setKindAttr(ctjs::ConvertKindAttr::get(&context, ctjs::ConvertKind::ToBoolean));
        inspect(ArrayContentsFailure::None);
        comparison->setOperand(0, child.getResult());
        inspect(ArrayContentsFailure::None);
        mlir::Block & last = function.getBody().back();
        auto store = llvm::cast<ctjs::SetPropertyOp>(&last.front());
        const mlir::Value replacement = store.getValue();
        store->setOperand(2, child.getResult());
        mutation.contents.arrays = "a:[zero] | a:[x]";
        mutation.contents.exit = "a -> {a}; a -> {a,x}";
        mutation.discharged = "";
        inspect(ArrayContentsFailure::None);
        store->setOperand(2, function.getBody().front().getArgument(3));
        inspect(ArrayContentsFailure::UnknownValue);
        store->setOperand(2, replacement);
        mutation.contents.arrays = rows.front().contents.arrays;
        mutation.contents.exit = rows.front().contents.exit;
        mutation.discharged = "x";
        inspect(ArrayContentsFailure::None);
        builder.setInsertionPoint(last.getTerminator());
        auto published = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held",
                                                     function.getBody().front().getArgument(3));
        inspect(ArrayContentsFailure::UnsupportedOperation);
        published.erase();
        inspect(ArrayContentsFailure::None);
    } else {
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live selector producer fixture did not parse");
    }
    std::printf("selector producers: %zu rows, %u live states, one wide snapshot, "
                "%zu retention budget cutoffs\n",
                rows.size() + comparisons.size() + conversions.size(), liveStates, budgets);
}

void checkLogicalNegation(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string negate = "  %negated = ctjs.unary not %p {storage_test_id = \"negated\"}\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %negated\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct negation_row {
        contents_row contents;
        const char * discharged = "x";
    };
    const std::vector<negation_row> rows = {
        {.contents = {.what = "negation releases a stored child only after both overwrites",
                      .body = values + negate + branch + overwrite + "^no:\n" + overwrite,
                      .arrays = "a:[zero] | a:[zero]",
                      .exit = "a -> {a}; a -> {a}"}},
        {.contents = {.what = "negating a local object returns a primitive without its origin",
                      .body = values + "  %negated = ctjs.unary not %x "
                                       "{storage_test_id = \"negated\"}\n"
                                       "  ctjs.return %negated\n",
                      .arrays = "a:[x]",
                      .exit = "negated -> {}"}},
        {.contents = {.what = "negating an opaque entry returns an independent Boolean",
                      .body = values + negate + "  ctjs.return %negated\n",
                      .arrays = "a:[x]",
                      .exit = "negated -> {}"}},
        {.contents = {.what = "a stored negation carries no operand heap origin",
                      .body = values + negate +
                              "  ctjs.set_property %a[%zero], %negated\n  ctjs.return %a\n",
                      .arrays = "a:[negated]",
                      .exit = "a -> {a}"}},
        {.contents = {.what = "a forwarded negation may root while its opaque input only forwards",
                      .body = "  %frame = ctjs.frame_enter 8\n" + values + negate +
                              "  cf.br ^next(%p, %negated : !ctjs.value, !ctjs.value)\n"
                              "^next(%opaque: !ctjs.value, %test: !ctjs.value):\n"
                              "  ctjs.root %test in %frame\n  ctjs.frame_exit %frame\n"
                              "  ctjs.return %test\n",
                      .arrays = "a:[x]",
                      .exit = "negated -> {}"}},
        {.contents = {.what = "negating a local or opaque join does not merge their origins",
                      .body = values + "  %flag = ctjs.truthy %p\n"
                                       "  cf.cond_br %flag, ^join(%x : !ctjs.value), "
                                       "^join(%p : !ctjs.value)\n"
                                       "^join(%selected: !ctjs.value):\n"
                                       "  %negated = ctjs.unary not %selected "
                                       "{storage_test_id = \"negated\"}\n"
                                       "  ctjs.return %negated\n",
                      .arrays = "a:[x] | a:[x]",
                      .exit = "negated -> {}; negated -> {}"}},
        {.contents = {.what = "negation does not erase a saved child's retention identity",
                      .body = values + "  %saved = ctjs.get_property %a[%zero]\n"
                                       "  %negated = ctjs.unary not %saved\n"
                                       "  ctjs.set_property %a[%zero], %negated\n"
                                       "  ctjs.return %saved\n",
                      .arrays = "a:[ctjs.unary]",
                      .reads = "a[0]=x",
                      .exit = "x -> {x}"},
         .discharged = ""},
        {.contents = {.what = "negating zero cannot prune an unsupported untaken arm",
                      .body = values + "  %negated = ctjs.unary not %zero\n" + branch + done +
                              "^no:\n  ctjs.store_global \"held\", %a\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a negation Boolean is not an exact array index",
                      .body = values + negate + "  %read = ctjs.get_property %a[%negated]\n" + done,
                      .failure = ArrayContentsFailure::UnknownIndex}},
        {.contents = {.what = "a negation Boolean is not an own String key",
                      .body = values + negate + "  ctjs.set_property %x[%negated], %zero\n" + done,
                      .failure = ArrayContentsFailure::UnknownPropertyKey}},
        {.contents = {.what = "a negation Boolean is not a copy endpoint",
                      .body = values + negate + "  ctjs.copy_props %negated into %x\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "negation does not authorize returning its opaque operand",
                      .body = values + negate + "  ctjs.return %p\n",
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "negation does not authorize storing its opaque operand",
                      .body = values + negate + "  ctjs.append %p to %a\n" + done,
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "negation does not authorize rooting its opaque operand",
                      .body = "  %frame = ctjs.frame_enter 8\n" + values + negate +
                              "  ctjs.root %p in %frame\n  ctjs.frame_exit %frame\n" + done,
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "negation cannot conceal an unsupported operand producer",
                      .body = values +
                              "  %bad = \"test.value\"() : () -> !ctjs.value\n"
                              "  %negated = ctjs.unary not %bad\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a negation result does not authorize unknown effects",
                      .body = values + negate +
                              "  \"test.effect\"(%negated) : (!ctjs.value) -> ()\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a negation Boolean cannot become a container",
                      .body =
                          values + negate + "  %read = ctjs.get_property %negated[%zero]\n" + done,
                      .failure = ArrayContentsFailure::UnknownArray}},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const negation_row & expected) {
        checkArrayContents(module, expected.contents);
        const bool complete = expected.contents.failure == ArrayContentsFailure::None;
        budgets += checkArrayRetention(module, {.what = expected.contents.what,
                                                .body = expected.contents.body,
                                                .discharged = complete ? expected.discharged : "",
                                                .complete = complete});
    };
    const auto parse = [&](const negation_row & expected) {
        return mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
    };
    const auto parseAndCheck = [&](const negation_row & expected) {
        if (auto module = parse(expected)) {
            check(*module, expected);
        } else {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the logical negation fixture did not parse");
        }
    };
    for (const auto & expected : rows) { parseAndCheck(expected); }
    // Preserve all five historical controls. TypeOf/Void now have independent
    // primitive-result proofs; no arithmetic kind borrows the Boolean proof.
    const std::vector<std::string> otherKinds = {"neg", "plus", "bitnot", "typeof", "void"};
    for (const auto & kind : otherKinds) {
        const bool total = kind == "typeof" || kind == "void";
        parseAndCheck({.contents = {.what = "other unary kinds require their own result proof",
                                    .body = values + "  %bad = ctjs.unary " + kind + " %p\n" + done,
                                    .failure = total ? ArrayContentsFailure::None
                                                     : ArrayContentsFailure::UnsupportedOperation,
                                    .arrays = total ? "a:[x]" : "",
                                    .exit = total ? "zero -> {}" : ""}});
    }

    negation_row wide = rows.front();
    std::string extras;
    for (unsigned i = 0; i < 32; ++i) {
        extras += "  %extra_" + std::to_string(i) + " = ctjs.unary not %p\n";
    }
    wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
    auto narrowModule = parse(rows.front());
    auto wideModule = parse(wide);
    if (narrowModule && wideModule) {
        const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
        const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
        if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
            fail(row{.what = "negation snapshots charge every primitive origin",
                     .body = wide.contents.body,
                     .expected = ""},
                 "32 extra negations did not cost one producer and one snapshot visit each");
        }
        check(*wideModule, wide);
    } else {
        fail(row{.what = "wide negation snapshot", .body = wide.contents.body, .expected = ""},
             "the logical negation snapshot fixture did not parse");
    }

    negation_row mutation = rows.front();
    mutation.contents.what = "live negation edits defeat stale forged completion markers";
    unsigned liveStates = 0;
    if (auto module = parse(mutation)) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::UnaryOp unary;
        ctjs::CreateObjectOp child;
        module->walk([&](ctjs::UnaryOp op) { unary = op; });
        module->walk([&](ctjs::CreateObjectOp op) { child = op; });
        mlir::OpBuilder builder(unary);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        child->setAttr("ctnative.confined", builder.getUnitAttr());
        const auto inspect = [&](ArrayContentsFailure failure) {
            mutation.contents.failure = failure;
            check(*module, mutation);
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::Neg));
        inspect(ArrayContentsFailure::UnsupportedOperation);
        unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::Not));
        inspect(ArrayContentsFailure::None);
        unary->setOperand(0, child.getResult());
        inspect(ArrayContentsFailure::None);
        mlir::Block & last = function.getBody().back();
        auto store = llvm::cast<ctjs::SetPropertyOp>(&last.front());
        const mlir::Value replacement = store.getValue();
        store->setOperand(2, child.getResult());
        mutation.contents.arrays = "a:[zero] | a:[x]";
        mutation.contents.exit = "a -> {a}; a -> {a,x}";
        mutation.discharged = "";
        inspect(ArrayContentsFailure::None);
        store->setOperand(2, function.getBody().front().getArgument(3));
        inspect(ArrayContentsFailure::UnknownValue);
        store->setOperand(2, replacement);
        mutation.contents.arrays = rows.front().contents.arrays;
        mutation.contents.exit = rows.front().contents.exit;
        mutation.discharged = "x";
        inspect(ArrayContentsFailure::None);
        builder.setInsertionPoint(last.getTerminator());
        auto published = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held",
                                                     function.getBody().front().getArgument(3));
        inspect(ArrayContentsFailure::UnsupportedOperation);
        published.erase();
        inspect(ArrayContentsFailure::None);
        unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::TypeOf));
        inspect(ArrayContentsFailure::None);
        unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::Not));
        inspect(ArrayContentsFailure::None);
    } else {
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live logical negation fixture did not parse");
    }
    std::printf("logical negation: %zu rows, %u live states, one wide snapshot, "
                "%zu retention budget cutoffs\n",
                rows.size() + otherKinds.size(), liveStates, budgets);
}

void checkTotalUnaryProducers(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %produced\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct unary_row {
        contents_row contents;
        const char * discharged = "x";
    };
    for (const std::string kind : {"typeof", "void"}) {
        const std::string operation = "  %produced = ctjs.unary " + kind;
        const std::string produce = operation + " %p {storage_test_id = \"produced\"}\n";
        const ctjs::UnaryKind originalKind =
            kind == "typeof" ? ctjs::UnaryKind::TypeOf : ctjs::UnaryKind::Void;
        const std::vector<unary_row> rows = {
            {.contents = {.what = "total unary results do not prune either overwrite arm",
                          .body = values + produce + branch + overwrite + "^no:\n" + overwrite,
                          .arrays = "a:[zero] | a:[zero]",
                          .exit = "a -> {a}; a -> {a}"}},
            {.contents = {.what = "an opaque operand produces an independent primitive terminal",
                          .body = values + produce + "  ctjs.return %produced\n",
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "a local operand is not retained by a unary result",
                          .body = values + operation +
                                  " %x {storage_test_id = \"produced\"}\n"
                                  "  ctjs.return %produced\n",
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "stored total unary results carry no operand object identity",
                          .body = values + produce +
                                  "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %a\n",
                          .arrays = "a:[produced]",
                          .exit = "a -> {a}"}},
            {.contents = {.what = "a forwarded primitive may root while its opaque operand cannot",
                          .body = "  %frame = ctjs.frame_enter 8\n" + values + produce +
                                  "  cf.br ^next(%p, %produced : !ctjs.value, !ctjs.value)\n"
                                  "^next(%opaque: !ctjs.value, %result: !ctjs.value):\n"
                                  "  ctjs.root %result in %frame\n  ctjs.frame_exit %frame\n"
                                  "  ctjs.return %result\n",
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "total unary observes a local or opaque join without aliasing it",
                          .body = values +
                                  "  %flag = ctjs.truthy %p\n"
                                  "  cf.cond_br %flag, ^join(%x : !ctjs.value), "
                                  "^join(%p : !ctjs.value)\n"
                                  "^join(%selected: !ctjs.value):\n" +
                                  operation +
                                  " %selected {storage_test_id = \"produced\"}\n"
                                  "  ctjs.return %produced\n",
                          .arrays = "a:[x] | a:[x]",
                          .exit = "produced -> {}; produced -> {}"}},
            {.contents = {.what = "a saved child still retains its original identity",
                          .body = values + "  %saved = ctjs.get_property %a[%zero]\n" + operation +
                                  " %saved {storage_test_id = \"produced\"}\n"
                                  "  ctjs.set_property %a[%zero], %produced\n"
                                  "  ctjs.return %saved\n",
                          .arrays = "a:[produced]",
                          .reads = "a[0]=x",
                          .exit = "x -> {x}"},
             .discharged = ""},
            {.contents = {.what = "a unary result is not an exact Number array index",
                          .body = values + produce + "  %read = ctjs.get_property %a[%produced]\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownIndex}},
            {.contents = {.what = "a unary String or Undefined is not a proved own String key",
                          .body = values + produce + "  ctjs.set_property %x[%produced], %zero\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownPropertyKey}},
            {.contents = {.what = "a unary result is not an own-data copy endpoint",
                          .body = values + produce + "  ctjs.copy_props %produced into %x\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a unary result is not a proved container",
                          .body = values + produce +
                                  "  %read = ctjs.get_property %produced[%zero]\n" + done,
                          .failure = ArrayContentsFailure::UnknownArray}},
            {.contents = {.what = "total unary does not authorize returning its opaque operand",
                          .body = values + produce + "  ctjs.return %p\n",
                          .failure = ArrayContentsFailure::UnknownValue}},
            {.contents = {.what = "total unary does not authorize storing its opaque operand",
                          .body = values + produce + "  ctjs.append %p to %a\n" + done,
                          .failure = ArrayContentsFailure::UnknownValue}},
            {.contents = {.what = "total unary does not authorize rooting its opaque operand",
                          .body = "  %frame = ctjs.frame_enter 8\n" + values + produce +
                                  "  ctjs.root %p in %frame\n  ctjs.frame_exit %frame\n" + done,
                          .failure = ArrayContentsFailure::UnknownValue}},
            {.contents = {.what = "discarding an unsupported operand producer cannot hide it",
                          .body = values + "  %bad = \"test.value\"() : () -> !ctjs.value\n" +
                                  operation + " %bad\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a total result cannot authorize a later unknown effect",
                          .body = values + produce +
                                  "  \"test.effect\"(%produced) : (!ctjs.value) -> ()\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "void or typeof cannot erase an already-evaluated publication",
                          .body = values + "  ctjs.store_global \"held\", %x\n" + produce + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "total primitive truthiness cannot prune an unsupported arm",
                          .body = values + operation + " %zero\n" + branch + done +
                                  "^no:\n  ctjs.store_global \"held\", %a\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a later retained arm prevents Stored refinement",
                          .body =
                              values + produce + branch + overwrite + "^no:\n  ctjs.return %a\n",
                          .arrays = "a:[zero] | a:[x]",
                          .exit = "a -> {a}; a -> {a,x}"},
             .discharged = ""},
            {.contents = {.what = "a literal operand does not infer the result property key",
                          .body = values + operation +
                                  " %zero\n"
                                  "  ctjs.set_property %x[%produced], %zero\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownPropertyKey}},
        };
        std::size_t budgets = 0;
        const auto check = [&](mlir::ModuleOp module, const unary_row & expected) {
            checkArrayContents(module, expected.contents);
            const bool complete = expected.contents.failure == ArrayContentsFailure::None;
            budgets +=
                checkArrayRetention(module, {.what = expected.contents.what,
                                             .body = expected.contents.body,
                                             .discharged = complete ? expected.discharged : "",
                                             .complete = complete});
        };
        const auto parse = [&](const unary_row & expected) {
            return mlir::parseSourceString<mlir::ModuleOp>(
                std::string{kPrologue} + expected.contents.body + "}\n", &context);
        };
        for (const auto & expected : rows) {
            if (auto module = parse(expected)) {
                check(*module, expected);
            } else {
                fail(row{.what = expected.contents.what,
                         .body = expected.contents.body,
                         .expected = ""},
                     "the total unary fixture did not parse");
            }
        }
        unary_row wide = rows.front();
        std::string extras;
        for (unsigned i = 0; i < 32; ++i) {
            extras += "  %extra_" + std::to_string(i) + " = ctjs.unary " + kind + " %p\n";
        }
        wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
        auto narrowModule = parse(rows.front());
        auto wideModule = parse(wide);
        if (narrowModule && wideModule) {
            const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
            const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
            if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
                fail(row{.what = "total unary snapshots charge every primitive origin",
                         .body = wide.contents.body,
                         .expected = ""},
                     "32 extra unary results did not cost one producer and one snapshot each");
            }
            check(*wideModule, wide);
        } else {
            fail(row{.what = "wide total unary snapshot",
                     .body = wide.contents.body,
                     .expected = ""},
                 "the total unary snapshot fixture did not parse");
        }

        unary_row mutation = rows.front();
        mutation.contents.what = "live total unary edits defeat stale forged completion markers";
        unsigned liveStates = 0;
        if (auto module = parse(mutation)) {
            ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
            ctjs::UnaryOp unary;
            ctjs::CreateObjectOp child;
            module->walk([&](ctjs::UnaryOp op) { unary = op; });
            module->walk([&](ctjs::CreateObjectOp op) { child = op; });
            mlir::OpBuilder builder(unary);
            function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
            function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
            child->setAttr("ctnative.confined", builder.getUnitAttr());
            const auto inspect = [&](ArrayContentsFailure failure) {
                mutation.contents.failure = failure;
                check(*module, mutation);
                ++liveStates;
            };
            inspect(ArrayContentsFailure::None);
            for (const auto coercing :
                 {ctjs::UnaryKind::Neg, ctjs::UnaryKind::Plus, ctjs::UnaryKind::BitNot}) {
                unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, coercing));
                inspect(ArrayContentsFailure::UnsupportedOperation);
            }
            unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, originalKind));
            inspect(ArrayContentsFailure::None);
            unary->setOperand(0, child.getResult());
            inspect(ArrayContentsFailure::None);
            mlir::Block & last = function.getBody().back();
            auto store = llvm::cast<ctjs::SetPropertyOp>(&last.front());
            const mlir::Value replacement = store.getValue();
            store->setOperand(2, child.getResult());
            mutation.contents.arrays = "a:[zero] | a:[x]";
            mutation.contents.exit = "a -> {a}; a -> {a,x}";
            mutation.discharged = "";
            inspect(ArrayContentsFailure::None);
            store->setOperand(2, function.getBody().front().getArgument(3));
            inspect(ArrayContentsFailure::UnknownValue);
            store->setOperand(2, replacement);
            mutation.contents.arrays = rows.front().contents.arrays;
            mutation.contents.exit = rows.front().contents.exit;
            mutation.discharged = "x";
            inspect(ArrayContentsFailure::None);
            builder.setInsertionPoint(last.getTerminator());
            auto published = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held",
                                                         function.getBody().front().getArgument(3));
            inspect(ArrayContentsFailure::UnsupportedOperation);
            published.erase();
            inspect(ArrayContentsFailure::None);
            unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::Not));
            inspect(ArrayContentsFailure::None);
        } else {
            fail(
                row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
                "the live total unary fixture did not parse");
        }
        std::printf("total unary %s: %zu rows, %u live states, one wide snapshot, "
                    "%zu retention budget cutoffs\n",
                    kind.c_str(), rows.size(), liveStates, budgets);
    }
}

void checkStaticBinaryProducers(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %big = ctjs.constant #ctjs.bigint<\"1\">\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %produced\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct binary_row {
        contents_row contents;
        const char * discharged = "x";
    };
    for (const auto kind : {ctjs::BinaryKind::Add, ctjs::BinaryKind::BitAnd,
                            ctjs::BinaryKind::BitOr, ctjs::BinaryKind::BitXor,
                            ctjs::BinaryKind::Shl, ctjs::BinaryKind::Shr, ctjs::BinaryKind::UShr}) {
        const std::string spelling = ctjs::stringifyBinaryKind(kind).str();
        const std::string operation = "  %produced = ctjs.binary_static " + spelling;
        const std::string produce = operation + " %zero, %zero {storage_test_id = \"produced\"}\n";
        const std::vector<binary_row> rows = {
            {.contents = {.what = "static Number results do not prune either overwrite arm",
                          .body = values + produce + branch + overwrite + "^no:\n" + overwrite,
                          .arrays = "a:[zero] | a:[zero]",
                          .exit = "a -> {a}; a -> {a}"}},
            {.contents = {.what = "static conversion of local objects retains neither operand",
                          .body = values + operation +
                                  " %x, %a {storage_test_id = \"produced\"}\n"
                                  "  ctjs.return %produced\n",
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "stored static Number results carry no operand identity",
                          .body = values + produce +
                                  "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %a\n",
                          .arrays = "a:[produced]",
                          .exit = "a -> {a}"}},
            {.contents = {.what = "a static result may forward and root independently",
                          .body = "  %frame = ctjs.frame_enter 8\n" + values + produce +
                                  "  cf.br ^next(%p, %produced : !ctjs.value, !ctjs.value)\n"
                                  "^next(%opaque: !ctjs.value, %result: !ctjs.value):\n"
                                  "  ctjs.root %result in %frame\n  ctjs.frame_exit %frame\n"
                                  "  ctjs.return %result\n",
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "a saved child still retains its original identity",
                          .body = values + "  %saved = ctjs.get_property %a[%zero]\n" + operation +
                                  " %saved, %zero {storage_test_id = \"produced\"}\n"
                                  "  ctjs.set_property %a[%zero], %produced\n"
                                  "  ctjs.return %saved\n",
                          .arrays = "a:[produced]",
                          .reads = "a[0]=x",
                          .exit = "x -> {x}"},
             .discharged = ""},
            {.contents = {.what = "a static operation cannot exclude BigInt on an opaque lhs",
                          .body = values + operation + " %p, %zero\n" + done,
                          .failure = ArrayContentsFailure::UnknownValue}},
            {.contents = {.what = "a static operation cannot exclude BigInt on an opaque rhs",
                          .body = values + operation + " %zero, %p\n" + done,
                          .failure = ArrayContentsFailure::UnknownValue}},
            {.contents = {.what = "mixed BigInt lhs can throw before static conversion",
                          .body = values + operation + " %big, %zero\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "mixed BigInt rhs can throw before static conversion",
                          .body = values + operation + " %zero, %big\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "two BigInts do not borrow the Number-only proof",
                          .body = values + operation + " %big, %big\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a forwarded BigInt arm refuses the complete transaction",
                          .body = values +
                                  "  %flag = ctjs.truthy %p\n"
                                  "  cf.cond_br %flag, ^join(%zero : !ctjs.value), "
                                  "^join(%big : !ctjs.value)\n"
                                  "^join(%operand: !ctjs.value):\n" +
                                  operation + " %operand, %zero\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a later retained structural arm prevents Stored refinement",
                          .body =
                              values + produce + branch + overwrite + "^no:\n  ctjs.return %a\n",
                          .arrays = "a:[zero] | a:[x]",
                          .exit = "a -> {a}; a -> {a,x}"},
             .discharged = ""},
            {.contents = {.what = "a known numeric value cannot prune an unsupported arm",
                          .body = values + produce + branch + done +
                                  "^no:\n  ctjs.store_global \"held\", %x\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a static Number result is not an exact array index",
                          .body = values + produce + "  %read = ctjs.get_property %a[%produced]\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownIndex}},
            {.contents = {.what = "a static Number result is not an exact own String key",
                          .body = values + produce + "  ctjs.set_property %x[%produced], %zero\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownPropertyKey}},
            {.contents = {.what = "a static result cannot authorize returning an opaque value",
                          .body = values + produce + "  ctjs.return %p\n",
                          .failure = ArrayContentsFailure::UnknownValue}},
            {.contents = {.what = "static conversion cannot conceal an unsupported producer",
                          .body = values + "  %bad = \"test.value\"() : () -> !ctjs.value\n" +
                                  operation + " %zero, %bad\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "static results do not authorize a later unknown effect",
                          .body = values + produce +
                                  "  \"test.effect\"(%produced) : (!ctjs.value) -> ()\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "the generic binary family cannot borrow static conversion",
                          .body = values + "  %produced = ctjs.binary add %x, %zero\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
        };
        std::size_t budgets = 0;
        const auto check = [&](mlir::ModuleOp module, const binary_row & expected) {
            checkArrayContents(module, expected.contents);
            const bool complete = expected.contents.failure == ArrayContentsFailure::None;
            budgets +=
                checkArrayRetention(module, {.what = expected.contents.what,
                                             .body = expected.contents.body,
                                             .discharged = complete ? expected.discharged : "",
                                             .complete = complete});
        };
        const auto parse = [&](const binary_row & expected) {
            return mlir::parseSourceString<mlir::ModuleOp>(
                std::string{kPrologue} + expected.contents.body + "}\n", &context);
        };
        const auto run = [&](const binary_row & expected) {
            if (auto module = parse(expected)) {
                check(*module, expected);
            } else {
                fail(row{.what = expected.contents.what,
                         .body = expected.contents.body,
                         .expected = ""},
                     "the static binary fixture did not parse");
            }
        };
        for (const auto & expected : rows) { run(expected); }

        // Each already-admitted primitive origin excludes BigInt independently.
        // Values loaded from containers must follow the original saved value,
        // not the container's newer contents or the get_property operation.
        const std::vector<std::string> origins = {
            "  %operand = ctjs.constant #ctjs.undefined\n",
            "  %operand = ctjs.constant #ctjs.null\n",
            "  %operand = ctjs.constant #ctjs.boolean<true>\n",
            "  %operand = ctjs.constant #ctjs.string<\"4294967297\">\n",
            "  %operand = ctjs.compare strict_eq %p, %zero\n",
            "  %operand = ctjs.convert to_boolean %p\n",
            "  %operand = ctjs.unary not %p\n",
            "  %operand = ctjs.unary typeof %p\n",
            "  %operand = ctjs.unary void %p\n",
            "  %operand = ctjs.binary_static " + spelling + " %zero, %zero\n",
        };
        for (const std::string & input : origins) {
            run({.contents = {.what = "a separately proved primitive origin excludes BigInt",
                              .body = values + input + operation +
                                      " %operand, %zero {storage_test_id = \"produced\"}\n"
                                      "  ctjs.return %produced\n",
                              .arrays = "a:[x]",
                              .exit = "produced -> {}"}});
        }
        for (const bool savedBigInt : {false, true}) {
            const std::string saved = savedBigInt ? "%big" : "%zero";
            const std::string replaced = savedBigInt ? "%zero" : "%big";
            run({.contents = {.what = "saved reads preserve their original BigInt exclusion",
                              .body = values + "  ctjs.set_property %a[%zero], " + saved +
                                      "\n  %operand = ctjs.get_property %a[%zero]\n"
                                      "  ctjs.set_property %a[%zero], " +
                                      replaced + "\n" + operation +
                                      " %operand, %zero {storage_test_id = \"produced\"}\n"
                                      "  ctjs.return %produced\n",
                              .failure = savedBigInt ? ArrayContentsFailure::UnsupportedOperation
                                                     : ArrayContentsFailure::None,
                              .arrays = "a:[ctjs.constant]",
                              .reads = "a[0]=zero",
                              .exit = "produced -> {}"}});
        }

        binary_row wide = rows.front();
        std::string extras;
        for (unsigned i = 0; i < 32; ++i) {
            extras += "  %extra_" + std::to_string(i) + " = ctjs.binary_static " + spelling +
                      " %zero, %zero\n";
        }
        wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
        auto narrowModule = parse(rows.front());
        auto wideModule = parse(wide);
        if (narrowModule && wideModule) {
            const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
            const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
            if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
                fail(row{.what = "static binary snapshots charge every primitive origin",
                         .body = wide.contents.body,
                         .expected = ""},
                     "32 extra results did not cost one producer and one snapshot each");
            }
            check(*wideModule, wide);
        } else {
            fail(row{.what = "wide static binary snapshot",
                     .body = wide.contents.body,
                     .expected = ""},
                 "the static binary snapshot fixture did not parse");
        }

        binary_row mutation = rows.front();
        mutation.contents.what = "live static operand and kind edits defeat forged completion";
        unsigned liveStates = 0;
        if (auto module = parse(mutation)) {
            ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
            ctjs::BinaryStaticOp binary;
            ctjs::CreateObjectOp child;
            ctjs::ConstantOp big;
            module->walk([&](ctjs::BinaryStaticOp op) { binary = op; });
            module->walk([&](ctjs::CreateObjectOp op) { child = op; });
            module->walk([&](ctjs::ConstantOp op) {
                if (llvm::isa<ctjs::BigIntAttr>(op.getValue())) { big = op; }
            });
            mlir::OpBuilder builder(binary);
            function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
            function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
            child->setAttr("ctnative.confined", builder.getUnitAttr());
            const auto inspect = [&](ArrayContentsFailure failure) {
                mutation.contents.failure = failure;
                check(*module, mutation);
                ++liveStates;
            };
            inspect(ArrayContentsFailure::None);
            const mlir::Value number = binary.getLhs();
            for (unsigned position : {0U, 1U}) {
                binary->setOperand(position, big.getResult());
                inspect(ArrayContentsFailure::UnsupportedOperation);
                binary->setOperand(position, function.getBody().front().getArgument(3));
                inspect(ArrayContentsFailure::UnknownValue);
                binary->setOperand(position, child.getResult());
                inspect(ArrayContentsFailure::None);
                binary->setOperand(position, number);
            }
            for (const auto invalid :
                 {ctjs::BinaryKind::Sub, ctjs::BinaryKind::Mul, ctjs::BinaryKind::Div,
                  ctjs::BinaryKind::Mod, ctjs::BinaryKind::Pow, ctjs::BinaryKind::Concat}) {
                binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, invalid));
                inspect(ArrayContentsFailure::UnsupportedOperation);
            }
            binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, kind));
            inspect(ArrayContentsFailure::None);
            auto constant = number.getDefiningOp<ctjs::ConstantOp>();
            const mlir::Attribute oldValue = constant.getValue();
            constant.setValueAttr(big.getValue());
            inspect(ArrayContentsFailure::UnsupportedOperation);
            constant.setValueAttr(oldValue);
            inspect(ArrayContentsFailure::None);
            mlir::Block & last = function.getBody().back();
            auto store = llvm::cast<ctjs::SetPropertyOp>(&last.front());
            store->setOperand(2, child.getResult());
            mutation.contents.arrays = "a:[zero] | a:[x]";
            mutation.contents.exit = "a -> {a}; a -> {a,x}";
            mutation.discharged = "";
            inspect(ArrayContentsFailure::None);
            store->setOperand(2, number);
            mutation.contents.arrays = rows.front().contents.arrays;
            mutation.contents.exit = rows.front().contents.exit;
            mutation.discharged = "x";
            inspect(ArrayContentsFailure::None);
        } else {
            fail(
                row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
                "the live static binary fixture did not parse");
        }
        std::printf("static binary %s: %zu rows, %u live states, one wide snapshot, "
                    "%zu retention budget cutoffs\n",
                    spelling.c_str(), rows.size() + origins.size() + 2, liveStates, budgets);
    }
}

void checkArithmeticUnaryProducers(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %number = ctjs.constant #ctjs.number<33>\n"
        "  %big = ctjs.constant #ctjs.bigint<\"1\">\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %produced\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct arithmetic_row {
        contents_row contents;
        const char * discharged = "x";
    };
    for (const auto kind : {ctjs::UnaryKind::Neg, ctjs::UnaryKind::Plus, ctjs::UnaryKind::BitNot}) {
        const std::string spelling = ctjs::stringifyUnaryKind(kind).str();
        const std::string operation = "  %produced = ctjs.unary " + spelling;
        const std::string produce = operation + " %number {storage_test_id = \"produced\"}\n";
        const std::vector<arithmetic_row> rows = {
            {.contents = {.what = "arithmetic Number results cannot prune either overwrite arm",
                          .body = values + produce + branch + overwrite + "^no:\n" + overwrite,
                          .arrays = "a:[zero] | a:[zero]",
                          .exit = "a -> {a}; a -> {a}"}},
            {.contents = {.what = "arithmetic returns a primitive without an operand alias",
                          .body = values + produce + "  ctjs.return %produced\n",
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "stored arithmetic Number results carry no object identity",
                          .body = values + produce +
                                  "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %a\n",
                          .arrays = "a:[produced]",
                          .exit = "a -> {a}"}},
            {.contents = {.what = "a primitive result forwards and roots independently",
                          .body = "  %frame = ctjs.frame_enter 8\n" + values + produce +
                                  "  cf.br ^next(%p, %produced : !ctjs.value, !ctjs.value)\n"
                                  "^next(%opaque: !ctjs.value, %result: !ctjs.value):\n"
                                  "  ctjs.root %result in %frame\n  ctjs.frame_exit %frame\n"
                                  "  ctjs.return %result\n",
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "a saved child remains retained after a primitive overwrite",
                          .body = values + "  %saved = ctjs.get_property %a[%zero]\n" + produce +
                                  "  ctjs.set_property %a[%zero], %produced\n"
                                  "  ctjs.return %saved\n",
                          .arrays = "a:[produced]",
                          .reads = "a[0]=x",
                          .exit = "x -> {x}"},
             .discharged = ""},
            {.contents = {.what = "an opaque input cannot establish primitive numeric coercion",
                          .body = values + operation + " %p\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a BigInt result needs its own category and refuses unary Plus",
                          .body = values + operation + " %big\n" + done,
                          .failure = kind == ctjs::UnaryKind::Plus
                                         ? ArrayContentsFailure::UnsupportedOperation
                                         : ArrayContentsFailure::None,
                          .arrays = "a:[x]",
                          .exit = "zero -> {}"}},
            {.contents = {.what = "fresh objects can reenter source unary conversion",
                          .body = values + operation + " %x\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "fresh arrays do not supply primitive unary inputs",
                          .body = values + operation + " %a\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a local input alias still can reenter numeric conversion",
                          .body = values + "  %saved = ctjs.get_property %a[%zero]\n" + operation +
                                  " %saved\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a later retained arm prevents Stored refinement",
                          .body =
                              values + produce + branch + overwrite + "^no:\n  ctjs.return %a\n",
                          .arrays = "a:[zero] | a:[x]",
                          .exit = "a -> {a}; a -> {a,x}"},
             .discharged = ""},
            {.contents = {.what = "known arithmetic cannot hide an unsupported structural arm",
                          .body = values + produce + branch + done +
                                  "^no:\n  ctjs.store_global \"held\", %x\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a Number origin is not a literal array index",
                          .body = values + produce + "  %read = ctjs.get_property %a[%produced]\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownIndex}},
            {.contents = {.what = "a Number origin is not a literal own String key",
                          .body = values + produce + "  ctjs.set_property %x[%produced], %zero\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownPropertyKey}},
            {.contents = {.what = "an independent Number cannot authorize an opaque return",
                          .body = values + produce + "  ctjs.return %p\n",
                          .failure = ArrayContentsFailure::UnknownValue}},
            {.contents = {.what = "arithmetic cannot conceal an unsupported operand producer",
                          .body = values + "  %bad = \"test.value\"() : () -> !ctjs.value\n" +
                                  operation + " %bad\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "an independent Number does not authorize later effects",
                          .body = values + produce +
                                  "  \"test.effect\"(%produced) : (!ctjs.value) -> ()\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
        };
        std::size_t budgets = 0;
        const auto check = [&](mlir::ModuleOp module, const arithmetic_row & expected) {
            checkArrayContents(module, expected.contents);
            const bool complete = expected.contents.failure == ArrayContentsFailure::None;
            budgets +=
                checkArrayRetention(module, {.what = expected.contents.what,
                                             .body = expected.contents.body,
                                             .discharged = complete ? expected.discharged : "",
                                             .complete = complete});
        };
        const auto parse = [&](const arithmetic_row & expected) {
            return mlir::parseSourceString<mlir::ModuleOp>(
                std::string{kPrologue} + expected.contents.body + "}\n", &context);
        };
        const auto run = [&](const arithmetic_row & expected) {
            if (auto module = parse(expected)) {
                check(*module, expected);
            } else {
                fail(row{.what = expected.contents.what,
                         .body = expected.contents.body,
                         .expected = ""},
                     "the arithmetic unary fixture did not parse");
            }
        };
        for (const auto & expected : rows) { run(expected); }
        const std::vector<std::string> origins = {
            "  %operand = ctjs.constant #ctjs.undefined\n",
            "  %operand = ctjs.constant #ctjs.null\n",
            "  %operand = ctjs.constant #ctjs.boolean<true>\n",
            "  %operand = ctjs.constant #ctjs.string<\"4294967297.\">\n",
            "  %operand = ctjs.compare strict_eq %p, %zero\n",
            "  %operand = ctjs.convert to_boolean %p\n",
            "  %operand = ctjs.unary not %p\n",
            "  %operand = ctjs.unary typeof %p\n",
            "  %operand = ctjs.unary void %p\n",
            "  %operand = ctjs.binary_static add %zero, %zero\n",
            "  %operand = ctjs.unary " + spelling + " %number\n",
        };
        for (const std::string & input : origins) {
            run({.contents = {.what = "every admitted primitive producer excludes object coercion",
                              .body = values + input + operation +
                                      " %operand {storage_test_id = \"produced\"}\n"
                                      "  ctjs.return %produced\n",
                              .arrays = "a:[x]",
                              .exit = "produced -> {}"}});
        }
        // Path identity and saved SSA values, not a container's current tag,
        // establish which value a coercion actually receives.
        for (const std::string input : {"%zero", "%big", "%x", "%p"}) {
            const bool primitive =
                input == "%zero" || (input == "%big" && kind != ctjs::UnaryKind::Plus);
            run({.contents = {.what = "every incoming unary operand must be a known primitive",
                              .body = values +
                                      "  %flag = ctjs.truthy %p\n"
                                      "  cf.cond_br %flag, ^join(%number : !ctjs.value), ^join(" +
                                      input + " : !ctjs.value)\n^join(%operand: !ctjs.value):\n" +
                                      operation +
                                      " %operand {storage_test_id = \"produced\"}\n"
                                      "  ctjs.return %produced\n",
                              .failure = primitive ? ArrayContentsFailure::None
                                                   : ArrayContentsFailure::UnsupportedOperation,
                              .arrays = "a:[x] | a:[x]",
                              .exit = "produced -> {}; produced -> {}"}});
        }
        for (const bool savedBigInt : {false, true}) {
            const std::string saved = savedBigInt ? "%big" : "%zero";
            const std::string replacement = savedBigInt ? "%zero" : "%big";
            run({.contents = {.what = "unary conversion follows saved values across tag overwrites",
                              .body = values + "  ctjs.set_property %a[%zero], " + saved +
                                      "\n  %operand = ctjs.get_property %a[%zero]\n"
                                      "  ctjs.set_property %a[%zero], " +
                                      replacement + "\n" + operation +
                                      " %operand {storage_test_id = \"produced\"}\n"
                                      "  ctjs.return %produced\n",
                              .failure = savedBigInt && kind == ctjs::UnaryKind::Plus
                                             ? ArrayContentsFailure::UnsupportedOperation
                                             : ArrayContentsFailure::None,
                              .arrays = savedBigInt ? "a:[zero]" : "a:[ctjs.constant]",
                              .reads = savedBigInt ? "a[0]=ctjs.constant" : "a[0]=zero",
                              .exit = "produced -> {}"}});
        }
        arithmetic_row wide = rows.front();
        std::string extras;
        for (unsigned i = 0; i < 32; ++i) {
            extras += "  %extra_" + std::to_string(i) + " = ctjs.unary " + spelling + " %number\n";
        }
        wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
        auto narrowModule = parse(rows.front());
        auto wideModule = parse(wide);
        if (narrowModule && wideModule) {
            const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
            const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
            if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
                fail(row{.what = "arithmetic snapshots charge every primitive origin",
                         .body = wide.contents.body,
                         .expected = ""},
                     "32 extra results did not cost one producer and one snapshot each");
            }
            check(*wideModule, wide);
        } else {
            fail(
                row{.what = "wide arithmetic snapshot", .body = wide.contents.body, .expected = ""},
                "the arithmetic snapshot fixture did not parse");
        }
        arithmetic_row mutation = rows.front();
        mutation.contents.what = "live unary tags defeat stale and forged completion";
        unsigned liveStates = 0;
        if (auto module = parse(mutation)) {
            ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
            ctjs::UnaryOp unary;
            ctjs::CreateObjectOp child;
            ctjs::CreateArrayOp array;
            ctjs::ConstantOp big;
            module->walk([&](ctjs::UnaryOp op) { unary = op; });
            module->walk([&](ctjs::CreateObjectOp op) { child = op; });
            module->walk([&](ctjs::CreateArrayOp op) { array = op; });
            module->walk([&](ctjs::ConstantOp op) {
                if (llvm::isa<ctjs::BigIntAttr>(op.getValue())) { big = op; }
            });
            mlir::OpBuilder builder(unary);
            function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
            function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
            child->setAttr("ctnative.confined", builder.getUnitAttr());
            const auto inspect = [&](ArrayContentsFailure failure) {
                mutation.contents.failure = failure;
                check(*module, mutation);
                ++liveStates;
            };
            inspect(ArrayContentsFailure::None);
            const mlir::Value number = unary.getOperand();
            const mlir::Value invalid[] = {big.getResult(), child.getResult(), array.getResult(),
                                           function.getBody().front().getArgument(3)};
            for (mlir::Value bad : invalid) {
                unary->setOperand(0, bad);
                inspect(bad == big.getResult() && kind != ctjs::UnaryKind::Plus
                            ? ArrayContentsFailure::None
                            : ArrayContentsFailure::UnsupportedOperation);
                unary->setOperand(0, number);
                inspect(ArrayContentsFailure::None);
            }
            auto constant = number.getDefiningOp<ctjs::ConstantOp>();
            const mlir::Attribute oldValue = constant.getValue();
            constant.setValueAttr(big.getValue());
            inspect(kind == ctjs::UnaryKind::Plus ? ArrayContentsFailure::UnsupportedOperation
                                                  : ArrayContentsFailure::None);
            constant.setValueAttr(oldValue);
            inspect(ArrayContentsFailure::None);
            for (const auto admitted :
                 {ctjs::UnaryKind::Not, ctjs::UnaryKind::TypeOf, ctjs::UnaryKind::Void,
                  ctjs::UnaryKind::Neg, ctjs::UnaryKind::Plus, ctjs::UnaryKind::BitNot}) {
                unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, admitted));
                inspect(ArrayContentsFailure::None);
            }
            unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, kind));
            mlir::Block & last = function.getBody().back();
            auto store = llvm::cast<ctjs::SetPropertyOp>(&last.front());
            const mlir::Value replacement = store.getValue();
            store->setOperand(2, child.getResult());
            mutation.contents.arrays = "a:[zero] | a:[x]";
            mutation.contents.exit = "a -> {a}; a -> {a,x}";
            mutation.discharged = "";
            inspect(ArrayContentsFailure::None);
            store->setOperand(2, replacement);
            mutation.contents.arrays = rows.front().contents.arrays;
            mutation.contents.exit = rows.front().contents.exit;
            mutation.discharged = "x";
            inspect(ArrayContentsFailure::None);
        } else {
            fail(
                row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
                "the live arithmetic unary fixture did not parse");
        }
        std::printf("arithmetic unary %s: %zu rows, %u live states, one wide snapshot, "
                    "%zu retention budget cutoffs\n",
                    spelling.c_str(), rows.size() + origins.size() + 6, liveStates, budgets);
    }
}

// Both families require independent original primitive operands, and share
// the same contents/retention obligations. Operator-specific mutation controls
// keep their distinct whitelists independent.
template <typename ProducerOp, typename Kind>
void checkPrimitiveBinaryProducer(mlir::MLIRContext & context, Kind producerKind) {
    constexpr bool isComparison = std::is_same_v<ProducerOp, ctjs::CompareOp>;
    using KindAttr = std::conditional_t<isComparison, ctjs::CompareKindAttr, ctjs::BinaryKindAttr>;
    const std::string spelling = [&] {
        if constexpr (isComparison) {
            return ctjs::stringifyCompareKind(producerKind).str();
        } else {
            return ctjs::stringifyBinaryKind(producerKind).str();
        }
    }();
    const std::string mnemonic = isComparison ? "ctjs.compare" : "ctjs.binary";
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %number = ctjs.constant #ctjs.number<17>\n"
        "  %big = ctjs.constant #ctjs.bigint<\"1\">\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const auto compare = [&](const std::string & lhs, const std::string & rhs) {
        return "  %produced = " + mnemonic + " " + spelling + " " + lhs + ", " + rhs +
               " {storage_test_id = \"produced\"}\n";
    };
    const std::string produce = compare("%number", "%zero");
    const std::string done = "  ctjs.return %zero\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %produced\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct comparison_row {
        contents_row contents;
        const char * discharged = "x";
    };
    const std::vector<comparison_row> rows = {
        {.contents = {.what = "primitive comparison cannot prune either structural overwrite arm",
                      .body = values + produce + branch + overwrite + "^no:\n" + overwrite,
                      .arrays = "a:[zero] | a:[zero]",
                      .exit = "a -> {a}; a -> {a}"}},
        {.contents = {.what = "primitive binary result returns independently of both operands",
                      .body = values + produce + "  ctjs.return %produced\n",
                      .arrays = "a:[x]",
                      .exit = "produced -> {}"}},
        {.contents = {.what = "storing a comparison result retains no former child identity",
                      .body = values + produce +
                              "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %a\n",
                      .arrays = "a:[produced]",
                      .exit = "a -> {a}"}},
        {.contents = {.what = "a comparison result forwards and roots independently",
                      .body = "  %frame = ctjs.frame_enter 8\n" + values + produce +
                              "  cf.br ^next(%p, %produced : !ctjs.value, !ctjs.value)\n"
                              "^next(%opaque: !ctjs.value, %result: !ctjs.value):\n"
                              "  ctjs.root %result in %frame\n  ctjs.frame_exit %frame\n"
                              "  ctjs.return %result\n",
                      .arrays = "a:[x]",
                      .exit = "produced -> {}"}},
        {.contents = {.what = "a saved child remains retained after a primitive overwrite",
                      .body = values + "  %saved = ctjs.get_property %a[%zero]\n" + produce +
                              "  ctjs.set_property %a[%zero], %produced\n"
                              "  ctjs.return %saved\n",
                      .arrays = "a:[produced]",
                      .reads = "a[0]=x",
                      .exit = "x -> {x}"},
         .discharged = ""},
        {.contents = {.what = "a later retained arm prevents comparison-based Stored refinement",
                      .body = values + produce + branch + overwrite + "^no:\n  ctjs.return %a\n",
                      .arrays = "a:[zero] | a:[x]",
                      .exit = "a -> {a}; a -> {a,x}"},
         .discharged = ""},
        {.contents = {.what = "literal comparison cannot conceal an unsupported structural arm",
                      .body = values + compare("%zero", "%zero") + branch + done +
                              "^no:\n  ctjs.store_global \"held\", %x\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a primitive binary result is not a literal array index",
                      .body =
                          values + produce + "  %read = ctjs.get_property %a[%produced]\n" + done,
                      .failure = ArrayContentsFailure::UnknownIndex}},
        {.contents = {.what = "a primitive binary result is not a literal own String key",
                      .body =
                          values + produce + "  ctjs.set_property %x[%produced], %zero\n" + done,
                      .failure = ArrayContentsFailure::UnknownPropertyKey}},
        {.contents = {.what = "a primitive comparison does not authorize an opaque return",
                      .body = values + produce + "  ctjs.return %p\n",
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "comparison cannot conceal an unsupported operand producer",
                      .body = values + "  %bad = \"test.value\"() : () -> !ctjs.value\n" +
                              compare("%bad", "%zero") + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a primitive comparison does not authorize later effects",
                      .body = values + produce +
                              "  \"test.effect\"(%produced) : (!ctjs.value) -> ()\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
    };
    std::size_t budgets = 0;
    unsigned rowCount = 0;
    const auto check = [&](mlir::ModuleOp module, const comparison_row & expected) {
        checkArrayContents(module, expected.contents);
        const bool complete = expected.contents.failure == ArrayContentsFailure::None;
        budgets += checkArrayRetention(module, {.what = expected.contents.what,
                                                .body = expected.contents.body,
                                                .discharged = complete ? expected.discharged : "",
                                                .complete = complete});
    };
    const auto parse = [&](const comparison_row & expected) {
        return mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
    };
    const auto run = [&](const comparison_row & expected) {
        if (auto module = parse(expected)) {
            check(*module, expected);
        } else {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the primitive comparison fixture did not parse");
        }
        ++rowCount;
    };
    for (const auto & expected : rows) { run(expected); }
    const std::vector<std::string> origins = {
        "  %operand = ctjs.constant #ctjs.undefined\n",
        "  %operand = ctjs.constant #ctjs.null\n",
        "  %operand = ctjs.constant #ctjs.boolean<true>\n",
        "  %operand = ctjs.constant #ctjs.number<17>\n",
        "  %operand = ctjs.constant #ctjs.string<\"17.\">\n",
        "  %operand = ctjs.compare strict_eq %p, %zero\n",
        "  %operand = ctjs.compare eq %number, %zero\n",
        "  %operand = ctjs.convert to_boolean %p\n",
        "  %operand = ctjs.unary not %p\n",
        "  %operand = ctjs.unary typeof %p\n",
        "  %operand = ctjs.unary void %p\n",
        "  %operand = ctjs.binary_static add %zero, %zero\n",
        "  %operand = ctjs.unary bitnot %number\n",
    };
    for (const bool left : {false, true}) {
        const auto compareInput = [&](const std::string & input) {
            return left ? compare(input, "%number") : compare("%number", input);
        };
        for (const std::string & input : origins) {
            run({.contents = {.what = "each comparison side needs its own primitive proof",
                              .body = values + input + compareInput("%operand") +
                                      "  ctjs.return %produced\n",
                              .arrays = "a:[x]",
                              .exit = "produced -> {}"}});
        }
        for (const std::string input : {"%big", "%x", "%a", "%p"}) {
            run({.contents = {.what = "neither comparison side borrows the other primitive proof",
                              .body = values + compareInput(input) + done,
                              .failure = ArrayContentsFailure::UnsupportedOperation}});
        }
        for (const std::string input : {"%zero", "%big", "%x", "%a", "%p"}) {
            run({.contents = {.what = "every structural comparison input needs a primitive origin",
                              .body = values +
                                      "  %flag = ctjs.truthy %p\n"
                                      "  cf.cond_br %flag, ^join(%number : !ctjs.value), ^join(" +
                                      input + " : !ctjs.value)\n^join(%operand: !ctjs.value):\n" +
                                      compareInput("%operand") + "  ctjs.return %produced\n",
                              .failure = input == "%zero"
                                             ? ArrayContentsFailure::None
                                             : ArrayContentsFailure::UnsupportedOperation,
                              .arrays = "a:[x] | a:[x]",
                              .exit = "produced -> {}; produced -> {}"}});
        }
        // An overwritten slot's new tag never cleans the saved old origin;
        // conversely, a later BigInt/object cannot taint an earlier primitive.
        for (const std::string saved : {"%zero", "%big", "%x"}) {
            const std::string replacement = saved == "%zero" ? "%big" : "%zero";
            run({.contents = {.what = "comparison follows saved array values across tag overwrites",
                              .body = values + "  ctjs.set_property %a[%zero], " + saved +
                                      "\n  %operand = ctjs.get_property %a[%zero]\n"
                                      "  ctjs.set_property %a[%zero], " +
                                      replacement + "\n" + compareInput("%operand") +
                                      "  ctjs.return %produced\n",
                              .failure = saved == "%zero"
                                             ? ArrayContentsFailure::None
                                             : ArrayContentsFailure::UnsupportedOperation,
                              .arrays = "a:[ctjs.constant]",
                              .reads = "a[0]=zero",
                              .exit = "produced -> {}"}});
        }
        for (const bool savedBigInt : {false, true}) {
            const std::string saved = savedBigInt ? "%big" : "%number";
            const std::string replacement = savedBigInt ? "%number" : "%big";
            run({.contents = {
                     .what = "comparison preserves saved own fields after overwrite and deletion",
                     .body = values + "  %key = ctjs.constant #ctjs.string<\"operand\">\n" +
                             "  ctjs.set_property %x[%key], " + saved +
                             "\n  %operand = ctjs.get_property %x[%key]\n"
                             "  ctjs.set_property %x[%key], " +
                             replacement + "\n  ctjs.delete_named \"operand\" from %x\n" +
                             compareInput("%operand") + "  ctjs.return %produced\n",
                     .failure = savedBigInt ? ArrayContentsFailure::UnsupportedOperation
                                            : ArrayContentsFailure::None,
                     .arrays = "a:[x]",
                     .exit = "produced -> {}"}});
        }
    }
    run({.contents = {.what = "primitive comparison cannot authorize publication before it",
                      .body = values + "  ctjs.store_global \"held\", %a\n" + produce + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}});
    run({.contents = {.what = "primitive comparison cannot authorize publication after it",
                      .body = values + produce + "  ctjs.store_global \"held\", %a\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}});
    run({.contents = {.what = "primitive comparison cannot authorize a retaining call",
                      .body = values + produce + "  %call = ctjs.call %p(%x)\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}});
    run({.contents = {.what = "primitive comparison cannot authorize explicit throwing",
                      .body = values + produce + "  ctjs.throw %x\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation}});
    run({.contents = {.what = "primitive comparison cannot authorize a local exception handler",
                      .body = values + "  ctjs.push_handler ^body catch ^pad\n^body:\n" + produce +
                              "  ctjs.pop_handler\n" + overwrite +
                              "^pad:\n  %id, %exception = ctjs.catch_land\n"
                              "  ctjs.return %a\n",
                      .failure = ArrayContentsFailure::UnsupportedControlFlow}});
    // Every admitted relational result is an independent primitive origin,
    // including when consumed by a different comparison kind. This contents
    // query supplies no completion or effect contract for the guarded paths.
    for (const std::string kind : {"lt", "le", "gt", "ge"}) {
        run({.contents = {
                 .what = "relational result origins feed independently checked comparisons",
                 .body = values + "  %operand = ctjs.compare " + kind + " %number, %zero\n" +
                         compare("%operand", "%zero") + "  ctjs.return %produced\n",
                 .arrays = "a:[x]",
                 .exit = "produced -> {}"}});
    }
    for (const std::string kind : {"sub", "mul", "div", "mod", "pow", "add", "concat"}) {
        run({.contents = {
                 .what = "arithmetic result origins feed independently checked binary producers",
                 .body = values + "  %operand = ctjs.binary " + kind + " %number, %zero\n" +
                         compare("%operand", "%zero") + "  ctjs.return %produced\n",
                 .arrays = "a:[x]",
                 .exit = "produced -> {}"}});
    }
    // Add can yield either Number or String; its result is primitive without
    // a concrete tag fact. Every consumer must work with both original cases.
    for (const bool stringLeft : {false, true}) {
        const std::string operands = stringLeft ? "%text, %zero" : "%zero, %text";
        run({.contents = {.what =
                              "String-producing addition remains an independent primitive origin",
                          .body = values + "  %text = ctjs.constant #ctjs.string<\"17.\">\n" +
                                  "  %operand = ctjs.binary add " + operands + "\n" +
                                  compare("%operand", "%zero") + "  ctjs.return %produced\n",
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}});
    }
    comparison_row wide = rows.front();
    std::string extras;
    for (unsigned i = 0; i < 32; ++i) {
        extras += "  %extra_" + std::to_string(i) + " = " + mnemonic + " " + spelling +
                  " %number, %zero\n";
    }
    wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
    auto narrowModule = parse(rows.front());
    auto wideModule = parse(wide);
    if (narrowModule && wideModule) {
        const auto narrow =
            computeArrayContents(*narrowModule->template getOps<ctjs::FuncOp>().begin());
        const auto expanded =
            computeArrayContents(*wideModule->template getOps<ctjs::FuncOp>().begin());
        if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
            fail(row{.what = "comparison snapshots charge every independent primitive origin",
                     .body = wide.contents.body,
                     .expected = ""},
                 "32 extra results did not cost one producer and one snapshot each");
        }
        check(*wideModule, wide);
    } else {
        fail(row{.what = "wide comparison snapshot", .body = wide.contents.body, .expected = ""},
             "the comparison snapshot fixture did not parse");
    }
    comparison_row mutation = rows.front();
    mutation.contents.what = "live comparison inputs and kinds defeat forged completion";
    unsigned liveStates = 0;
    if (auto module = parse(mutation)) {
        ctjs::FuncOp function = *module->template getOps<ctjs::FuncOp>().begin();
        ProducerOp comparison;
        ctjs::CreateObjectOp child;
        ctjs::CreateArrayOp array;
        ctjs::ConstantOp big;
        module->walk([&](ProducerOp op) { comparison = op; });
        module->walk([&](ctjs::CreateObjectOp op) { child = op; });
        module->walk([&](ctjs::CreateArrayOp op) { array = op; });
        module->walk([&](ctjs::ConstantOp op) {
            if (llvm::isa<ctjs::BigIntAttr>(op.getValue())) { big = op; }
        });
        mlir::OpBuilder builder(comparison);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        child->setAttr("ctnative.confined", builder.getUnitAttr());
        const auto inspect = [&](ArrayContentsFailure failure) {
            mutation.contents.failure = failure;
            check(*module, mutation);
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        const mlir::Value original[] = {comparison.getLhs(), comparison.getRhs()};
        const mlir::Value opaque = function.getBody().front().getArgument(3);
        const mlir::Value invalid[] = {big.getResult(), child.getResult(), array.getResult(),
                                       opaque};
        for (unsigned position = 0; position < 2; ++position) {
            for (mlir::Value bad : invalid) {
                comparison->setOperand(position, bad);
                inspect(ArrayContentsFailure::UnsupportedOperation);
                comparison->setOperand(position, original[position]);
                inspect(ArrayContentsFailure::None);
            }
            auto constant = original[position].template getDefiningOp<ctjs::ConstantOp>();
            const mlir::Attribute oldValue = constant.getValue();
            constant.setValueAttr(big.getValue());
            inspect(ArrayContentsFailure::UnsupportedOperation);
            constant.setValueAttr(oldValue);
            inspect(ArrayContentsFailure::None);
        }
        if constexpr (isComparison) {
            for (const auto kind : {ctjs::CompareKind::Lt, ctjs::CompareKind::Le,
                                    ctjs::CompareKind::Gt, ctjs::CompareKind::Ge}) {
                comparison.setKindAttr(KindAttr::get(&context, kind));
                inspect(ArrayContentsFailure::None);
                comparison.setKindAttr(KindAttr::get(&context, producerKind));
                inspect(ArrayContentsFailure::None);
            }
            comparison->setOperand(0, opaque);
            comparison.setKindAttr(KindAttr::get(&context, ctjs::CompareKind::StrictEq));
            inspect(ArrayContentsFailure::None);
            comparison.setKindAttr(KindAttr::get(&context, producerKind));
            inspect(ArrayContentsFailure::UnsupportedOperation);
            comparison->setOperand(0, original[0]);
            inspect(ArrayContentsFailure::None);
        } else {
            for (const auto kind :
                 {ctjs::BinaryKind::Sub, ctjs::BinaryKind::Mul, ctjs::BinaryKind::Div,
                  ctjs::BinaryKind::Mod, ctjs::BinaryKind::Pow, ctjs::BinaryKind::Add,
                  ctjs::BinaryKind::Concat, ctjs::BinaryKind::BitAnd, ctjs::BinaryKind::BitOr,
                  ctjs::BinaryKind::BitXor, ctjs::BinaryKind::Shl, ctjs::BinaryKind::Shr,
                  ctjs::BinaryKind::UShr}) {
                comparison.setKindAttr(KindAttr::get(&context, kind));
                const bool supported =
                    kind == ctjs::BinaryKind::Sub || kind == ctjs::BinaryKind::Mul ||
                    kind == ctjs::BinaryKind::Div || kind == ctjs::BinaryKind::Mod ||
                    kind == ctjs::BinaryKind::Pow || kind == ctjs::BinaryKind::Add ||
                    kind == ctjs::BinaryKind::Concat;
                inspect(supported ? ArrayContentsFailure::None
                                  : ArrayContentsFailure::UnsupportedOperation);
                comparison.setKindAttr(KindAttr::get(&context, producerKind));
                inspect(ArrayContentsFailure::None);
            }
        }
        mlir::Block & last = function.getBody().back();
        auto store = llvm::cast<ctjs::SetPropertyOp>(&last.front());
        const mlir::Value replacement = store.getValue();
        store->setOperand(2, child.getResult());
        mutation.contents.arrays = "a:[zero] | a:[x]";
        mutation.contents.exit = "a -> {a}; a -> {a,x}";
        mutation.discharged = "";
        inspect(ArrayContentsFailure::None);
        store->setOperand(2, replacement);
        mutation.contents.arrays = rows.front().contents.arrays;
        mutation.contents.exit = rows.front().contents.exit;
        mutation.discharged = "x";
        inspect(ArrayContentsFailure::None);
    } else {
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live primitive comparison fixture did not parse");
    }
    std::printf("primitive %s %s: %u rows, %u live states, one wide snapshot, "
                "%zu retention budget cutoffs\n",
                isComparison ? "comparison" : "arithmetic binary", spelling.c_str(), rowCount,
                liveStates, budgets);
}

void checkBigIntUnaryProducers(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %big = ctjs.constant #ctjs.bigint<\"9007199254740993\"> "
        "{storage_test_id = \"big\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string done = "  ctjs.return %produced\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %produced\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct unary_row {
        contents_row contents;
        const char * discharged = "x";
    };
    for (const auto kind : {ctjs::UnaryKind::Neg, ctjs::UnaryKind::BitNot}) {
        const std::string spelling = ctjs::stringifyUnaryKind(kind).str();
        const std::string produce =
            "  %produced = ctjs.unary " + spelling + " %big {storage_test_id = \"produced\"}\n";
        const std::vector<unary_row> rows = {
            {.contents = {.what = "a computed BigInt cannot prune either overwrite path",
                          .body = values + produce + branch + overwrite + "^no:\n" + overwrite,
                          .arrays = "a:[zero] | a:[zero]",
                          .exit = "a -> {a}; a -> {a}"}},
            {.contents = {.what = "BigInt unary result carries no operand object identity",
                          .body = values + produce + done,
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "storing an independent BigInt releases the old child",
                          .body = values + produce +
                                  "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %a\n",
                          .arrays = "a:[produced]",
                          .exit = "a -> {a}"}},
            {.contents = {.what = "a separately saved child stays retained after BigInt overwrite",
                          .body = values + "  %saved = ctjs.get_property %a[%zero]\n" + produce +
                                  "  ctjs.set_property %a[%zero], %produced\n"
                                  "  ctjs.return %saved\n",
                          .arrays = "a:[produced]",
                          .reads = "a[0]=x",
                          .exit = "x -> {x}"},
             .discharged = ""},
            {.contents = {.what = "computed BigInt category survives frame and edge transport",
                          .body = "  %frame = ctjs.frame_enter 8\n" + values + produce +
                                  "  cf.br ^next(%produced : !ctjs.value)\n"
                                  "^next(%operand: !ctjs.value):\n"
                                  "  %next = ctjs.unary " +
                                  spelling +
                                  " %operand {storage_test_id = \"next\"}\n"
                                  "  ctjs.root %next in %frame\n  ctjs.frame_exit %frame\n"
                                  "  ctjs.return %next\n",
                          .arrays = "a:[x]",
                          .exit = "next -> {}"}},
            {.contents = {.what = "saved computed BigInt array origins survive Number overwrite",
                          .body = values + produce +
                                  "  ctjs.set_property %a[%zero], %produced\n"
                                  "  %saved = ctjs.get_property %a[%zero]\n"
                                  "  ctjs.set_property %a[%zero], %zero\n"
                                  "  %next = ctjs.compare eq %saved, %big "
                                  "{storage_test_id = \"next\"}\n  ctjs.return %next\n",
                          .arrays = "a:[zero]",
                          .reads = "a[0]=produced",
                          .exit = "next -> {}"}},
            {.contents = {.what = "saved computed BigInt own fields survive overwrite and deletion",
                          .body = values + produce +
                                  "  %key = ctjs.constant #ctjs.string<\"value\">\n"
                                  "  ctjs.set_property %x[%key], %produced\n"
                                  "  %saved = ctjs.get_property %x[%key]\n"
                                  "  ctjs.set_property %x[%key], %zero\n"
                                  "  ctjs.delete_named \"value\" from %x\n"
                                  "  %next = ctjs.compare eq %saved, %big "
                                  "{storage_test_id = \"next\"}\n  ctjs.return %next\n",
                          .arrays = "a:[x]",
                          .exit = "next -> {}"}},
            {.contents = {.what = "computed BigInt cannot supply a numeric array index",
                          .body = values + produce + "  %read = ctjs.get_property %a[%produced]\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownIndex}},
            {.contents = {.what = "computed BigInt cannot supply an own String key",
                          .body = values + produce + "  ctjs.set_property %x[%produced], %zero\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownPropertyKey}},
            {.contents = {.what = "computed BigInt remains refused by unary Plus",
                          .body = values + produce + "  %next = ctjs.unary plus %produced\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "an unrelated primitive does not establish opaque retention",
                          .body = values + produce + "  ctjs.return %p\n",
                          .failure = ArrayContentsFailure::UnknownValue}},
        };
        unsigned rowCount = 0;
        unsigned liveStates = 0;
        std::size_t budgets = 0;
        const auto check = [&](mlir::ModuleOp module, const unary_row & expected) {
            checkArrayContents(module, expected.contents);
            const bool complete = expected.contents.failure == ArrayContentsFailure::None;
            budgets +=
                checkArrayRetention(module, {.what = expected.contents.what,
                                             .body = expected.contents.body,
                                             .discharged = complete ? expected.discharged : "",
                                             .complete = complete});
        };
        const auto parse = [&](const unary_row & expected) {
            return mlir::parseSourceString<mlir::ModuleOp>(
                std::string{kPrologue} + expected.contents.body + "}\n", &context);
        };
        const auto run = [&](const unary_row & expected) {
            if (auto module = parse(expected)) {
                check(*module, expected);
            } else {
                fail(row{.what = expected.contents.what,
                         .body = expected.contents.body,
                         .expected = ""},
                     "the computed BigInt unary fixture did not parse");
            }
            ++rowCount;
        };
        for (const auto & expected : rows) { run(expected); }
        for (const std::string form : {"binary", "binary_static"}) {
            const std::vector<std::string> kinds =
                form == "binary"
                    ? std::vector<std::string>{"add", "sub", "mul", "div", "mod", "pow", "concat"}
                    : std::vector<std::string>{"add", "bitand", "bitor", "bitxor",
                                               "shl", "shr",    "ushr"};
            for (const std::string & operation : kinds) {
                for (const std::string operands :
                     {"%produced, %zero", "%zero, %produced", "%produced, %big"}) {
                    run({.contents = {
                             .what = "computed BigInt never borrows the non-BigInt binary proof",
                             .body = values + produce + "  %next = ctjs." + form + " " + operation +
                                     " " + operands + "\n" + done,
                             .failure = ArrayContentsFailure::UnsupportedOperation}});
                }
            }
        }
        for (const std::string comparison : {"eq", "lt", "le", "gt", "ge"}) {
            for (const bool mixed : {false, true}) {
                for (const bool left : {false, true}) {
                    const std::string other = mixed ? "%zero" : "%big";
                    const std::string operands =
                        left ? "%produced, " + other : other + ", %produced";
                    run({.contents = {.what = "both comparison operands independently prove their "
                                              "original category",
                                      .body =
                                          values + produce + "  %next = ctjs.compare " +
                                          comparison + " " + operands +
                                          " {storage_test_id = \"next\"}\n  ctjs.return %next\n",
                                      .failure = mixed ? ArrayContentsFailure::UnsupportedOperation
                                                       : ArrayContentsFailure::None,
                                      .arrays = "a:[x]",
                                      .exit = "next -> {}"}});
                }
            }
        }
        for (const std::string operation : {"not", "typeof", "void"}) {
            run({.contents = {
                     .what = "a later total unary result has its independent non-BigInt category",
                     .body = values + produce + "  %next = ctjs.unary " + operation +
                             " %produced\n  %sum = ctjs.binary add %next, %zero "
                             "{storage_test_id = \"sum\"}\n  ctjs.return %sum\n",
                     .arrays = "a:[x]",
                     .exit = "sum -> {}"}});
        }
        for (const bool reversed : {false, true}) {
            const std::string actuals =
                reversed ? "%zero : !ctjs.value), ^join(%big" : "%big : !ctjs.value), ^join(%zero";
            const std::string paths =
                values + "  %flag = ctjs.truthy %p\n  cf.cond_br %flag, ^join(" + actuals +
                " : !ctjs.value)\n^join(%operand: !ctjs.value):\n"
                "  %produced = ctjs.unary " +
                spelling + " %operand {storage_test_id = \"produced\"}\n";
            run({.contents = {.what = "one SSA producer has independent BigInt and Number "
                                      "categories on separate paths",
                              .body = paths + done,
                              .arrays = "a:[x] | a:[x]",
                              .exit = "produced -> {}; produced -> {}"}});
            run({.contents = {.what =
                                  "either BigInt incoming path blocks a later non-BigInt operation",
                              .body = paths + "  %next = ctjs.binary add %produced, %zero\n" + done,
                              .failure = ArrayContentsFailure::UnsupportedOperation}});
        }
        for (const std::string effect :
             {"  ctjs.store_global \"held\", %a\n", "  %called = ctjs.call %p(%a)\n",
              "  \"test.effect\"(%produced) : (!ctjs.value) -> ()\n"}) {
            run({.contents = {
                     .what =
                         "BigInt production cannot authorize later publication calls or effects",
                     .body = values + produce + effect + done,
                     .failure = ArrayContentsFailure::UnsupportedOperation}});
        }
        unary_row wide = rows.front();
        std::string extras;
        for (unsigned i = 0; i < 32; ++i) {
            extras += "  %extra_" + std::to_string(i) + " = ctjs.unary " + spelling + " %big\n";
        }
        wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
        auto narrowModule = parse(rows.front());
        auto wideModule = parse(wide);
        if (narrowModule && wideModule) {
            const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
            const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
            if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 128) {
                fail(row{.what = "BigInt snapshots charge result origins and categories separately",
                         .body = wide.contents.body,
                         .expected = ""},
                     "32 results did not charge their operations, categories and both snapshot "
                     "entries");
            }
            check(*wideModule, wide);
        } else {
            fail(row{.what = "wide BigInt unary snapshot",
                     .body = wide.contents.body,
                     .expected = ""},
                 "the BigInt unary snapshot fixture did not parse");
        }
        unary_row mutation = rows.front();
        if (auto module = parse(mutation)) {
            auto function = *module->getOps<ctjs::FuncOp>().begin();
            ctjs::UnaryOp unary;
            ctjs::CreateObjectOp child;
            ctjs::CreateArrayOp array;
            module->walk([&](ctjs::UnaryOp op) { unary = op; });
            module->walk([&](ctjs::CreateObjectOp op) { child = op; });
            module->walk([&](ctjs::CreateArrayOp op) { array = op; });
            mlir::OpBuilder builder(unary);
            function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
            function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
            child->setAttr("ctnative.confined", builder.getUnitAttr());
            const auto inspect = [&](ArrayContentsFailure failure) {
                mutation.contents.failure = failure;
                check(*module, mutation);
                ++liveStates;
            };
            inspect(ArrayContentsFailure::None);
            const mlir::Value input = unary.getOperand();
            const mlir::Value invalidInputs[] = {child.getResult(), array.getResult(),
                                                 function.getBody().front().getArgument(3)};
            for (mlir::Value invalid : invalidInputs) {
                unary->setOperand(0, invalid);
                inspect(ArrayContentsFailure::UnsupportedOperation);
                unary->setOperand(0, input);
                inspect(ArrayContentsFailure::None);
            }
            for (const auto other : {ctjs::UnaryKind::Plus, static_cast<ctjs::UnaryKind>(255)}) {
                unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, other));
                inspect(ArrayContentsFailure::UnsupportedOperation);
                unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, kind));
                inspect(ArrayContentsFailure::None);
            }
            auto constant = input.getDefiningOp<ctjs::ConstantOp>();
            const mlir::Attribute oldValue = constant.getValue();
            constant.setValueAttr(ctjs::NumberAttr::get(&context, 0));
            inspect(ArrayContentsFailure::None);
            constant.setValueAttr(oldValue);
            inspect(ArrayContentsFailure::None);
            auto store = llvm::cast<ctjs::SetPropertyOp>(&function.getBody().back().front());
            const mlir::Value replacement = store.getValue();
            store->setOperand(2, child.getResult());
            mutation.contents.arrays = "a:[zero] | a:[x]";
            mutation.contents.exit = "a -> {a}; a -> {a,x}";
            mutation.discharged = "";
            inspect(ArrayContentsFailure::None);
            store->setOperand(2, replacement);
            mutation.contents = rows.front().contents;
            mutation.discharged = "x";
            inspect(ArrayContentsFailure::None);
        } else {
            fail(row{.what = "live computed BigInt unary",
                     .body = mutation.contents.body,
                     .expected = ""},
                 "the BigInt unary mutation fixture did not parse");
        }
        std::printf("BigInt unary %s: %u rows, %u live states, one wide snapshot, %zu retention "
                    "budget cutoffs\n",
                    spelling.c_str(), rowCount, liveStates, budgets);
    }
}

void checkBigIntComparison(mlir::MLIRContext & context, ctjs::CompareKind producerKind) {
    const std::string spelling = ctjs::stringifyCompareKind(producerKind).str();
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %lhs = ctjs.constant #ctjs.bigint<\"9007199254740993\"> "
        "{storage_test_id = \"lhs\"}\n"
        "  %rhs = ctjs.constant #ctjs.bigint<\"9007199254740992\"> "
        "{storage_test_id = \"rhs\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const auto compare = [&](const std::string & lhs, const std::string & rhs) {
        return "  %produced = ctjs.compare " + spelling + " " + lhs + ", " + rhs +
               " {storage_test_id = \"produced\"}\n";
    };
    const std::string produce = compare("%lhs", "%rhs");
    const std::string done = "  ctjs.return %produced\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %produced\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct bigint_row {
        contents_row contents;
        const char * discharged = "x";
    };
    const std::vector<bigint_row> rows = {
        {.contents = {.what = "BigInt comparison cannot prune either structural overwrite arm",
                      .body = values + produce + branch + overwrite + "^no:\n" + overwrite,
                      .arrays = "a:[zero] | a:[zero]",
                      .exit = "a -> {a}; a -> {a}"}},
        {.contents = {.what = "BigInt comparison returns an independent Boolean",
                      .body = values + produce + done,
                      .arrays = "a:[x]",
                      .exit = "produced -> {}"}},
        {.contents = {.what = "a stored BigInt comparison result retains no child identity",
                      .body = values + produce +
                              "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %a\n",
                      .arrays = "a:[produced]",
                      .exit = "a -> {a}"}},
        {.contents = {.what = "saved children retain their identity after BigInt comparison",
                      .body = values + "  %saved = ctjs.get_property %a[%zero]\n" + produce +
                              "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %saved\n",
                      .arrays = "a:[produced]",
                      .reads = "a[0]=x",
                      .exit = "x -> {x}"},
         .discharged = ""},
        {.contents = {.what = "a BigInt comparison result forwards and roots independently",
                      .body = "  %frame = ctjs.frame_enter 8\n" + values + produce +
                              "  cf.br ^next(%produced : !ctjs.value)\n"
                              "^next(%result: !ctjs.value):\n"
                              "  ctjs.root %result in %frame\n  ctjs.frame_exit %frame\n"
                              "  ctjs.return %result\n",
                      .arrays = "a:[x]",
                      .exit = "produced -> {}"}},
        {.contents = {.what = "a BigInt comparison result is not a literal array index",
                      .body =
                          values + produce + "  %read = ctjs.get_property %a[%produced]\n" + done,
                      .failure = ArrayContentsFailure::UnknownIndex}},
        {.contents = {.what = "a BigInt comparison result is not a literal own String key",
                      .body =
                          values + produce + "  ctjs.set_property %x[%produced], %zero\n" + done,
                      .failure = ArrayContentsFailure::UnknownPropertyKey}},
        {.contents = {.what = "even equal BigInts cannot hide publication on an untaken arm",
                      .body = values + compare("%lhs", "%lhs") + branch + done +
                              "^no:\n  ctjs.store_global \"held\", %x\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "BigInt comparison cannot authorize an opaque return",
                      .body = values + produce + "  ctjs.return %p\n",
                      .failure = ArrayContentsFailure::UnknownValue}},
    };
    unsigned rowCount = 0;
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const bigint_row & expected) {
        checkArrayContents(module, expected.contents);
        const bool complete = expected.contents.failure == ArrayContentsFailure::None;
        budgets += checkArrayRetention(module, {.what = expected.contents.what,
                                                .body = expected.contents.body,
                                                .discharged = complete ? expected.discharged : "",
                                                .complete = complete});
    };
    const auto parse = [&](const bigint_row & expected) {
        return mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
    };
    const auto run = [&](const bigint_row & expected) {
        if (auto module = parse(expected)) {
            check(*module, expected);
        } else {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the BigInt comparison fixture did not parse");
        }
        ++rowCount;
    };
    for (const auto & expected : rows) { run(expected); }
    const std::vector<std::string> rejectedOrigins = {
        "  %operand = ctjs.constant #ctjs.undefined\n",
        "  %operand = ctjs.constant #ctjs.null\n",
        "  %operand = ctjs.constant #ctjs.boolean<true>\n",
        "  %operand = ctjs.constant #ctjs.number<0>\n",
        "  %operand = ctjs.constant #ctjs.string<\"9007199254740993\">\n",
        "  %operand = ctjs.compare strict_eq %lhs, %rhs\n",
    };
    for (const bool left : {false, true}) {
        const auto compareInput = [&](const std::string & input) {
            return left ? compare(input, "%rhs") : compare("%lhs", input);
        };
        if (producerKind != ctjs::CompareKind::Eq) {
            for (const std::string producer : {
                     "  %operand = ctjs.binary add %lhs, %rhs\n",
                     "  %operand = ctjs.unary neg %lhs\n",
                 }) {
                const bool unary = producer.find("ctjs.unary") != std::string::npos;
                run({.contents = {.what = "computed BigInts require independent category evidence",
                                  .body = values + producer + compareInput("%operand") + done,
                                  .failure = unary ? ArrayContentsFailure::None
                                                   : ArrayContentsFailure::UnsupportedOperation,
                                  .arrays = "a:[x]",
                                  .exit = "produced -> {}"}});
            }
            run({.contents = {.what =
                                  "a saved BigInt array value survives a later Number overwrite",
                              .body = values +
                                      "  ctjs.set_property %a[%zero], %lhs\n"
                                      "  %operand = ctjs.get_property %a[%zero]\n"
                                      "  ctjs.set_property %a[%zero], %zero\n" +
                                      compareInput("%operand") + done,
                              .arrays = "a:[zero]",
                              .reads = "a[0]=lhs",
                              .exit = "produced -> {}"}});
            run({.contents = {.what =
                                  "a saved BigInt own field survives a changed tag and deletion",
                              .body = values +
                                      "  %key = ctjs.constant #ctjs.string<\"operand\">\n"
                                      "  ctjs.set_property %x[%key], %lhs\n"
                                      "  %operand = ctjs.get_property %x[%key]\n"
                                      "  ctjs.set_property %x[%key], %zero\n"
                                      "  ctjs.delete_named \"operand\" from %x\n" +
                                      compareInput("%operand") + done,
                              .arrays = "a:[x]",
                              .exit = "produced -> {}"}});
        }
        for (const std::string & input : rejectedOrigins) {
            run({.contents = {.what =
                                  "a BigInt operand cannot authorize a mixed primitive category",
                              .body = values + input + compareInput("%operand") + done,
                              .failure = ArrayContentsFailure::UnsupportedOperation}});
        }
        for (const std::string input : {"%lhs", "%zero", "%x", "%a", "%p"}) {
            const bool big = input == "%lhs";
            run({.contents = {
                     .what = "every incoming BigInt comparison operand must independently qualify",
                     .body = values +
                             "  %flag = ctjs.truthy %p\n"
                             "  cf.cond_br %flag, ^join(%rhs : !ctjs.value), ^join(" +
                             input + " : !ctjs.value)\n^join(%operand: !ctjs.value):\n" +
                             compareInput("%operand") + done,
                     .failure = big ? ArrayContentsFailure::None
                                    : ArrayContentsFailure::UnsupportedOperation,
                     .arrays = "a:[x] | a:[x]",
                     .exit = "produced -> {}; produced -> {}"}});
            run({.contents = {.what = "BigInt comparison keeps saved array origins after overwrite",
                              .body = values + "  ctjs.set_property %a[%zero], " + input +
                                      "\n  %operand = ctjs.get_property %a[%zero]\n"
                                      "  ctjs.set_property %a[%zero], %rhs\n" +
                                      compareInput("%operand") + done,
                              .failure = big ? ArrayContentsFailure::None
                                         : input == "%p"
                                             ? ArrayContentsFailure::UnknownValue
                                             : ArrayContentsFailure::UnsupportedOperation,
                              .arrays = "a:[rhs]",
                              .reads = "a[0]=lhs",
                              .exit = "produced -> {}"}});
        }
        for (const std::string input : {"%lhs", "%zero", "%x"}) {
            run({.contents = {
                     .what =
                         "BigInt comparison keeps saved own fields after overwrite and deletion",
                     .body = values +
                             "  %key = ctjs.constant #ctjs.string<\"operand\">\n"
                             "  ctjs.set_property %x[%key], " +
                             input +
                             "\n  %operand = ctjs.get_property %x[%key]\n"
                             "  ctjs.set_property %x[%key], %rhs\n"
                             "  ctjs.delete_named \"operand\" from %x\n" +
                             compareInput("%operand") + done,
                     .failure = input == "%lhs" ? ArrayContentsFailure::None
                                                : ArrayContentsFailure::UnsupportedOperation,
                     .arrays = "a:[x]",
                     .exit = "produced -> {}"}});
        }
    }
    for (const std::string effect : {
             "  ctjs.store_global \"held\", %a\n",
             "  %called = ctjs.call %p(%a)\n",
             "  \"test.effect\"(%produced) : (!ctjs.value) -> ()\n",
         }) {
        run({.contents = {
                 .what = "BigInt comparison cannot authorize publication calls or unknown effects",
                 .body = values + produce + effect + done,
                 .failure = ArrayContentsFailure::UnsupportedOperation}});
    }
    run({.contents = {.what = "BigInt comparison cannot authorize an explicit throw",
                      .body = values + produce + "  ctjs.throw %x\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation}});
    run({.contents = {.what = "BigInt comparison cannot authorize a local exception handler",
                      .body = values + "  ctjs.push_handler ^body catch ^pad\n^body:\n" + produce +
                              "  ctjs.pop_handler\n" + overwrite +
                              "^pad:\n  %id, %exception = ctjs.catch_land\n  ctjs.return %a\n",
                      .failure = ArrayContentsFailure::UnsupportedControlFlow}});
    bigint_row wide = rows.front();
    std::string extras;
    for (unsigned i = 0; i < 32; ++i) {
        extras += "  %extra_" + std::to_string(i) + " = ctjs.compare " + spelling + " %lhs, %rhs\n";
    }
    wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
    auto narrowModule = parse(rows.front());
    auto wideModule = parse(wide);
    if (narrowModule && wideModule) {
        const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
        const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
        if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
            fail(row{.what = "BigInt comparison charges each independent result and snapshot",
                     .body = wide.contents.body,
                     .expected = ""},
                 "32 extra results did not cost one producer and one snapshot each");
        }
        check(*wideModule, wide);
    } else {
        fail(row{.what = "wide BigInt comparison snapshot",
                 .body = wide.contents.body,
                 .expected = ""},
             "the wide BigInt comparison fixture did not parse");
    }
    bigint_row mutation = rows.front();
    mutation.contents.what = "live BigInt comparison origins defeat forged completion";
    unsigned liveStates = 0;
    if (auto module = parse(mutation)) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::CompareOp comparison;
        ctjs::CreateObjectOp child;
        ctjs::CreateArrayOp array;
        ctjs::ConstantOp zero;
        module->walk([&](ctjs::CompareOp op) { comparison = op; });
        module->walk([&](ctjs::CreateObjectOp op) { child = op; });
        module->walk([&](ctjs::CreateArrayOp op) { array = op; });
        module->walk([&](ctjs::ConstantOp op) {
            if (llvm::isa<ctjs::NumberAttr>(op.getValue())) { zero = op; }
        });
        mlir::OpBuilder builder(comparison);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        child->setAttr("ctnative.confined", builder.getUnitAttr());
        const auto inspect = [&](ArrayContentsFailure failure) {
            mutation.contents.failure = failure;
            check(*module, mutation);
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        for (unsigned position = 0; position < 2; ++position) {
            const mlir::Value original = comparison->getOperand(position);
            const mlir::Value invalid[] = {zero.getResult(), child.getResult(), array.getResult(),
                                           function.getBody().front().getArgument(3)};
            for (mlir::Value bad : invalid) {
                comparison->setOperand(position, bad);
                inspect(ArrayContentsFailure::UnsupportedOperation);
                comparison->setOperand(position, original);
                inspect(ArrayContentsFailure::None);
            }
            auto constant = original.getDefiningOp<ctjs::ConstantOp>();
            const mlir::Attribute oldValue = constant.getValue();
            constant.setValueAttr(zero.getValue());
            inspect(ArrayContentsFailure::UnsupportedOperation);
            constant.setValueAttr(oldValue);
            inspect(ArrayContentsFailure::None);
        }
        for (const auto kind :
             {ctjs::CompareKind::StrictEq, ctjs::CompareKind::Eq, ctjs::CompareKind::Lt,
              ctjs::CompareKind::Le, ctjs::CompareKind::Gt, ctjs::CompareKind::Ge}) {
            comparison.setKindAttr(ctjs::CompareKindAttr::get(&context, kind));
            inspect(ArrayContentsFailure::None);
        }
        comparison.setKindAttr(ctjs::CompareKindAttr::get(&context, producerKind));
        inspect(ArrayContentsFailure::None);
        if (producerKind != ctjs::CompareKind::Eq) {
            comparison.setKindAttr(
                ctjs::CompareKindAttr::get(&context, static_cast<ctjs::CompareKind>(255)));
            inspect(ArrayContentsFailure::UnsupportedOperation);
            comparison.setKindAttr(ctjs::CompareKindAttr::get(&context, producerKind));
            inspect(ArrayContentsFailure::None);
        }
        auto store = llvm::cast<ctjs::SetPropertyOp>(&function.getBody().back().front());
        const mlir::Value replacement = store.getValue();
        store->setOperand(2, child.getResult());
        mutation.contents.arrays = "a:[zero] | a:[x]";
        mutation.contents.exit = "a -> {a}; a -> {a,x}";
        mutation.discharged = "";
        inspect(ArrayContentsFailure::None);
        store->setOperand(2, replacement);
        mutation.contents = rows.front().contents;
        mutation.discharged = "x";
        inspect(ArrayContentsFailure::None);
    } else {
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live BigInt comparison fixture did not parse");
    }
    std::printf("BigInt comparison %s: %u rows, %u live states, one wide snapshot, "
                "%zu retention budget cutoffs\n",
                spelling.c_str(), rowCount, liveStates, budgets);
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
        {.what = "an unused opaque entry value forwards without proving its contents",
         .body = enter + array +
                 "  cf.br ^next(%p : !ctjs.value)\n"
                 "^next(%unused: !ctjs.value):\n" +
                 leave + done,
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
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
        // The successor no longer uses this forwarded value after its old
        // publication was erased. It remains opaque, never a known root.
        mutation.failure = ArrayContentsFailure::None;
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
        {.contents = {.what = "an unused opaque alternative stays separate from local join values",
                      .body = array +
                              "  cf.cond_br %condition, ^join(%a : !ctjs.value), "
                              "^join(%p : !ctjs.value)\n"
                              "^join(%unused: !ctjs.value):\n" +
                              done,
                      .arrays = "a:[x] | a:[x]",
                      .exit = "zero -> {}; zero -> {}"},
         .discharged = "x"},
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
                !result.writes.empty() || !result.objects.empty() ||
                !result.propertyReads.empty() || !result.propertyWrites.empty() ||
                !result.propertyDeletions.empty() || !result.propertyCopies.empty() ||
                !result.exits.empty() || refined.arrayRetentionComplete ||
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

void checkContainerSwitches(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %one = ctjs.constant #ctjs.number<4607182418800017408> "
        "{storage_test_id = \"one\"}\n"
        "  %key = ctjs.constant #ctjs.string<\"child\">\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %y = ctjs.create_object {storage_test_id = \"y\"}\n"
        "  %flag = ctjs.truthy %p\n";
    const std::string array = values + "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string split =
        "  cf.switch %flag : i1, [default: ^fallback, 0: ^zero, 1: ^one]\n^fallback:\n";
    const std::string done = "  ctjs.return %zero\n";
    struct switch_row {
        contents_row contents;
        const char * discharged = "";
        bool acyclicWrites = true;
    };
    const std::vector<switch_row> rows = {
        {.contents = {.what = "a default-only switch forwards exact values without a snapshot",
                      .body = array + "  cf.switch %flag : i1, [default: ^join(%a : !ctjs.value)]\n"
                                      "^join(%selected: !ctjs.value):\n"
                                      "  ctjs.set_property %selected[%zero], %y\n"
                                      "  ctjs.return %selected\n",
                      .arrays = "a:[y]",
                      .exit = "a -> {a,y}"},
         .discharged = "x"},
        {.contents = {.what = "all switch edges overwrite the old child before a shared return",
                      .body = array +
                              "  cf.switch %flag : i1, [default: ^join(%y : !ctjs.value), "
                              "0: ^join(%zero : !ctjs.value), 1: ^join(%one : !ctjs.value)]\n"
                              "^join(%replacement: !ctjs.value):\n"
                              "  ctjs.set_property %a[%zero], %replacement\n"
                              "  ctjs.return %a\n",
                      .arrays = "a:[y] | a:[zero] | a:[one]",
                      .exit = "a -> {a,y}; a -> {a}; a -> {a}"},
         .discharged = "x"},
        {.contents = {.what =
                          "three edges to the same block preserve distinct target and value pairs",
                      .body = array +
                              "  %b = ctjs.create_array [%y] {storage_test_id = \"b\"}\n"
                              "  %c = ctjs.create_array [%a, %b] {storage_test_id = \"c\"}\n"
                              "  cf.switch %flag : i1, ["
                              "default: ^join(%a, %zero : !ctjs.value, !ctjs.value), "
                              "0: ^join(%b, %zero : !ctjs.value, !ctjs.value), "
                              "1: ^join(%a, %y : !ctjs.value, !ctjs.value)]\n"
                              "^join(%target: !ctjs.value, %replacement: !ctjs.value):\n"
                              "  ctjs.set_property %target[%zero], %replacement\n"
                              "  ctjs.return %c\n",
                      .arrays = "a:[zero]; b:[y]; c:[a,b] | a:[x]; b:[zero]; c:[a,b] | "
                                "a:[y]; b:[y]; c:[a,b]",
                      .exit = "c -> {a,b,c,y}; c -> {a,b,c,x}; c -> {a,b,c,y}"}},
        {.contents = {.what = "the final switch case retains a child overwritten on earlier edges",
                      .body = array + split +
                              "  ctjs.set_property %a[%zero], %y\n  ctjs.return %a\n"
                              "^zero:\n  ctjs.set_property %a[%zero], %zero\n"
                              "  ctjs.return %a\n^one:\n  ctjs.return %a\n",
                      .arrays = "a:[y] | a:[zero] | a:[x]",
                      .exit = "a -> {a,y}; a -> {a}; a -> {a,x}"}},
        {.contents = {.what =
                          "a saved child returned on one switch edge survives later replacement",
                      .body = array +
                              "  %saved = ctjs.get_property %a[%zero]\n"
                              "  cf.switch %flag : i1, [default: ^join(%saved : !ctjs.value), "
                              "0: ^join(%y : !ctjs.value), 1: ^join(%zero : !ctjs.value)]\n"
                              "^join(%result: !ctjs.value):\n"
                              "  ctjs.set_property %a[%zero], %y\n"
                              "  ctjs.return %result\n",
                      .arrays = "a:[y] | a:[y] | a:[y]",
                      .reads = "a[0]=x",
                      .exit = "x -> {x}; y -> {y}; zero -> {}"}},
        {.contents = {.what = "switch joins allocate one exact container instance per path",
                      .body = values +
                              "  cf.switch %flag : i1, [default: ^join(%x : !ctjs.value), "
                              "0: ^join(%y : !ctjs.value), 1: ^join(%zero : !ctjs.value)]\n"
                              "^join(%element: !ctjs.value):\n"
                              "  %b = ctjs.create_array [%element] {storage_test_id = \"b\"}\n"
                              "  ctjs.return %b\n",
                      .arrays = "b:[x] | b:[y] | b:[zero]",
                      .exit = "b -> {b,x}; b -> {b,y}; b -> {b}"}},
        {.contents = {.what = "own object reads use the replacement from their switch edge",
                      .body = values +
                              "  %o = ctjs.create_object {storage_test_id = \"o\"}\n"
                              "  ctjs.set_property %o[%key], %x\n"
                              "  cf.switch %flag : i1, [default: ^join(%y : !ctjs.value), "
                              "0: ^join(%zero : !ctjs.value), 1: ^join(%one : !ctjs.value)]\n"
                              "^join(%replacement: !ctjs.value):\n"
                              "  ctjs.set_property %o[%key], %replacement\n"
                              "  %read = ctjs.get_property %o[%key]\n  ctjs.return %read\n",
                      .exit = "y -> {y}; zero -> {}; one -> {}",
                      .objects = "x:{}; y:{}; o:{child:y} | x:{}; y:{}; o:{child:zero} | "
                                 "x:{}; y:{}; o:{child:one}",
                      .propertyReads = "o[child]=y; o[child]=zero; o[child]=one"},
         .discharged = "x"},
        {.contents = {.what = "switch object targets retain exact own fields through array aliases",
                      .body = values +
                              "  %o = ctjs.create_object {storage_test_id = \"o\"}\n"
                              "  %b = ctjs.create_object {storage_test_id = \"b\"}\n"
                              "  ctjs.set_property %o[%key], %x\n"
                              "  ctjs.set_property %b[%key], %y\n"
                              "  %c = ctjs.create_array [%o, %b] {storage_test_id = \"c\"}\n"
                              "  cf.switch %flag : i1, ["
                              "default: ^join(%zero, %zero : !ctjs.value, !ctjs.value), "
                              "0: ^join(%one, %zero : !ctjs.value, !ctjs.value), "
                              "1: ^join(%zero, %y : !ctjs.value, !ctjs.value)]\n"
                              "^join(%index: !ctjs.value, %replacement: !ctjs.value):\n"
                              "  %target = ctjs.get_property %c[%index]\n"
                              "  ctjs.set_property %target[%key], %replacement\n"
                              "  ctjs.return %c\n",
                      .arrays = "c:[o,b] | c:[o,b] | c:[o,b]",
                      .reads = "c[0]=o; c[1]=b; c[0]=o",
                      .exit = "c -> {b,c,o,y}; c -> {b,c,o,x}; c -> {b,c,o,y}",
                      .objects = "x:{}; y:{}; o:{child:zero}; b:{child:y} | "
                                 "x:{}; y:{}; o:{child:x}; b:{child:zero} | "
                                 "x:{}; y:{}; o:{child:y}; b:{child:y}",
                      .propertyWrites = "ctjs.set_property[2]:o[child]=x; "
                                        "ctjs.set_property[2]:b[child]=y; "
                                        "ctjs.set_property[2]:o[child]=zero; "
                                        "ctjs.set_property[2]:b[child]=zero; "
                                        "ctjs.set_property[2]:o[child]=y"}},
        {.contents = {.what =
                          "mutually exclusive switch writes cannot hide a mixed-container cycle",
                      .body = values +
                              "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n"
                              "  %o = ctjs.create_object {storage_test_id = \"o\"}\n" +
                              split + "  ctjs.append %o to %a\n" + done +
                              "^zero:\n  ctjs.set_property %o[%key], %a\n" + done + "^one:\n" +
                              done,
                      .arrays = "a:[o] | a:[] | a:[]",
                      .exit = "zero -> {}; zero -> {}; zero -> {}",
                      .objects = "x:{}; y:{}; o:{} | x:{}; y:{}; o:{child:a} | "
                                 "x:{}; y:{}; o:{}"},
         .acyclicWrites = false},
        {.contents = {.what =
                          "a transient cycle on a later case survives the final contents overwrite",
                      .body = array + split + done +
                              "^zero:\n  ctjs.set_property %a[%zero], %a\n"
                              "  ctjs.set_property %a[%zero], %zero\n" +
                              done + "^one:\n" + done,
                      .arrays = "a:[x] | a:[zero] | a:[x]",
                      .exit = "zero -> {}; zero -> {}; zero -> {}"},
         .acyclicWrites = false},
        {.contents = {.what = "switch forwarding retains an unused opaque alternative separately",
                      .body = array +
                              "  cf.switch %flag : i1, [default: ^join(%a : !ctjs.value), "
                              "0: ^join(%x : !ctjs.value), 1: ^join(%p : !ctjs.value)]\n"
                              "^join(%unused: !ctjs.value):\n" +
                              done,
                      .arrays = "a:[x] | a:[x] | a:[x]",
                      .exit = "zero -> {}; zero -> {}; zero -> {}"},
         .discharged = "x"},
        {.contents = {.what = "the last switch path cannot borrow an earlier path's appended slot",
                      .body = array + split +
                              "  ctjs.append %y to %a\n  cf.br ^join\n"
                              "^zero:\n  ctjs.append %y to %a\n  cf.br ^join\n"
                              "^one:\n  cf.br ^join\n^join:\n"
                              "  %read = ctjs.get_property %a[%one]\n  ctjs.return %read\n",
                      .failure = ArrayContentsFailure::MissingElement}},
        {.contents = {.what = "the last switch path cannot borrow an earlier path's own property",
                      .body = values + "  %o = ctjs.create_object\n" + split +
                              "  ctjs.set_property %o[%key], %x\n  cf.br ^join\n"
                              "^zero:\n  ctjs.set_property %o[%key], %y\n  cf.br ^join\n"
                              "^one:\n  cf.br ^join\n^join:\n"
                              "  %read = ctjs.get_property %o[%key]\n  ctjs.return %read\n",
                      .failure = ArrayContentsFailure::MissingProperty}},
        {.contents = {.what = "exhaustive case values cannot hide publication on the default edge",
                      .body = array + split + "  ctjs.store_global \"held\", %a\n" + done +
                              "^zero:\n" + done + "^one:\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "late publication on the last switch case discards earlier exits",
                      .body = array + split + done + "^zero:\n" + done +
                              "^one:\n  ctjs.store_global \"held\", %a\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a constant selector cannot conceal an unsupported case",
                      .body = array +
                              "  %constant = ctjs.truthy %zero\n"
                              "  cf.switch %constant : i1, [default: ^fallback, 1: ^one]\n"
                              "^fallback:\n" +
                              done + "^one:\n  ctjs.store_global \"held\", %a\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a switch back edge refuses repeated allocation-site instances",
                      .body = array + split + done + "^zero:\n" + done + "^one:\n  cf.br ^one\n",
                      .failure = ArrayContentsFailure::UnsupportedControlFlow}},
        {.contents = {.what = "unknown selector producers do not borrow the switch whitelist",
                      .body = array +
                              "  %opaque = \"test.selector\"() : () -> i32\n"
                              "  cf.switch %opaque : i32, [default: ^join]\n^join:\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what =
                          "all switch edges preserve the active frame and an exact forwarded flag",
                      .body = "  %frame = ctjs.frame_enter 4\n" + array +
                              "  cf.switch %flag : i1, ["
                              "default: ^join(%frame, %a, %flag : !ctjs.context, !ctjs.value, i1), "
                              "0: ^join(%frame, %x, %flag : !ctjs.context, !ctjs.value, i1), "
                              "1: ^join(%frame, %y, %flag : !ctjs.context, !ctjs.value, i1)]\n"
                              "^join(%active: !ctjs.context, %root: !ctjs.value, %test: i1):\n"
                              "  ctjs.root %root in %active\n"
                              "  cf.switch %test : i1, [default: ^exit]\n^exit:\n"
                              "  ctjs.frame_exit %active\n" +
                              done,
                      .arrays = "a:[x] | a:[x] | a:[x]",
                      .exit = "zero -> {}; zero -> {}; zero -> {}"},
         .discharged = "x"},
        {.contents = {.what = "a missing frame exit on the last switch path refuses every root",
                      .body = "  %frame = ctjs.frame_enter 4\n" + array + split +
                              "  ctjs.frame_exit %frame\n" + done +
                              "^zero:\n  ctjs.frame_exit %frame\n" + done + "^one:\n" + done,
                      .failure = ArrayContentsFailure::InvalidFrame}},
        {.contents = {.what = "an external rooted value on the last switch path remains unknown",
                      .body = "  %frame = ctjs.frame_enter 4\n" + array + split +
                              "  ctjs.frame_exit %frame\n" + done +
                              "^zero:\n  ctjs.frame_exit %frame\n" + done +
                              "^one:\n  ctjs.root %p in %frame\n  ctjs.frame_exit %frame\n" + done,
                      .failure = ArrayContentsFailure::UnknownValue}},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const switch_row & expected) {
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
        if (module) {
            check(*module, expected);
        } else {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the switch fixture did not parse");
        }
    }

    // Change only the last case's successor operands, retaining forged markers
    // while its target, value and use sites invalidate earlier complete proofs.
    switch_row mutation = rows[1];
    mutation.contents.what = "live switch edges invalidate and restore complete retention";
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.contents.body + "}\n", &context);
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        mlir::cf::SwitchOp branch;
        ctjs::SetPropertyOp changed;
        ctjs::CreateArrayOp container;
        module->walk([&](mlir::cf::SwitchOp op) { branch = op; });
        module->walk([&](ctjs::SetPropertyOp op) { changed = op; });
        module->walk([&](ctjs::CreateArrayOp op) { container = op; });
        const mlir::Value original = branch.getCaseOperands(1).front();
        const mlir::Value child = container.getElements().front();
        mlir::OpBuilder builder(changed);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        child.getDefiningOp()->setAttr("ctnative.confined", builder.getUnitAttr());
        check(*module, mutation);
        branch.getCaseOperandsMutable(1).assign(mlir::ValueRange{child});
        mutation.contents.arrays = "a:[y] | a:[zero] | a:[x]";
        mutation.contents.exit = "a -> {a,y}; a -> {a}; a -> {a,x}";
        mutation.discharged = "";
        check(*module, mutation);
        branch.getCaseOperandsMutable(1).assign(
            mlir::ValueRange{function.getBody().front().getArgument(3)});
        mutation.contents.failure = ArrayContentsFailure::UnknownValue;
        check(*module, mutation);
        branch.getCaseOperandsMutable(1).assign(mlir::ValueRange{original});
        auto publication =
            ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", container.getResult());
        mutation.contents.failure = ArrayContentsFailure::UnsupportedOperation;
        check(*module, mutation);
        publication.erase();
        mutation = rows[1];
        check(*module, mutation);

        // These temporarily malformed edge layouts are checked directly,
        // without feeding invalid IR to the legacy sparse solver. Every
        // incomplete prefix still discards all records, and restoring the
        // valid operation restores the complete query and its refinement.
        const auto malformed = [&](ArrayContentsFailure failure) {
            const row r{.what = "malformed switch edges cannot publish partial proof",
                        .body = mutation.contents.body,
                        .expected = ""};
            const auto empty = [](const ArrayContentsEvidence & result) {
                return result.arrays.empty() && result.objects.empty() && result.writes.empty() &&
                       result.reads.empty() && result.propertyWrites.empty() &&
                       result.propertyReads.empty() && result.propertyDeletions.empty() &&
                       result.propertyCopies.empty() && result.exits.empty();
            };
            const auto result = computeArrayContents(function);
            if (result.complete || result.failure != failure || !result.refusedBy ||
                !empty(result)) {
                fail(r, "malformed edge was accepted or kept partial contents");
            }
            for (std::size_t limit = 0; limit < result.work; ++limit) {
                const auto partial = computeArrayContents(function, limit);
                if (partial.complete || partial.failure != ArrayContentsFailure::WorkLimit ||
                    partial.work != limit || !empty(partial)) {
                    fail(r, "malformed edge's incomplete budget retained evidence");
                    break;
                }
            }
            const auto exact = computeArrayContents(function, result.work);
            if (exact.complete || exact.failure != result.failure || exact.work != result.work ||
                !empty(exact)) {
                fail(r, "malformed edge's exact budget changed the refusal");
            }
        };
        const mlir::Value defaultValue = branch.getDefaultOperands().front();
        branch.getDefaultOperandsMutable().assign(mlir::ValueRange{});
        malformed(ArrayContentsFailure::UnsupportedControlFlow);
        branch.getDefaultOperandsMutable().assign(mlir::ValueRange{defaultValue});
        branch.getCaseOperandsMutable(1).assign(mlir::ValueRange{});
        malformed(ArrayContentsFailure::UnsupportedControlFlow);
        branch.getCaseOperandsMutable(1).assign(mlir::ValueRange{original});
        mlir::Block * oldTarget = branch.getCaseDestinations()[1];
        auto * emptyBlock = new mlir::Block;
        function.getBody().push_back(emptyBlock);
        branch->setSuccessor(emptyBlock, 2);
        malformed(ArrayContentsFailure::UnsupportedControlFlow);
        branch->setSuccessor(oldTarget, 2);
        emptyBlock->erase();
        const mlir::Value flag = branch.getFlag();
        const auto unknown =
            function.getBody().front().addArgument(builder.getI1Type(), function.getLoc());
        branch.getFlagMutable().set(unknown);
        malformed(ArrayContentsFailure::UnknownValue);
        branch.getFlagMutable().set(flag);
        function.getBody().front().eraseArgument(unknown.getArgNumber());
        check(*module, mutation);
    } else {
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live switch fixture did not parse");
    }

    // Three edges at each of ten joins create 3^10 paths from a small CFG.
    // Snapshot charges include both array slots and object properties.
    std::string expanding = array + "  %o = ctjs.create_object {storage_test_id = \"o\"}\n"
                                    "  ctjs.set_property %o[%key], %a\n  cf.br ^b0\n";
    for (unsigned i = 0; i < 10; ++i) {
        const std::string next = "^b" + std::to_string(i + 1);
        expanding += "^b" + std::to_string(i) + ":\n  cf.switch %flag : i1, [default: " + next +
                     ", 0: " + next + ", 1: " + next + "]\n";
    }
    expanding += "^b10:\n" + done;
    auto explosion = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + expanding + "}\n", &context);
    if (explosion) {
        ctjs::FuncOp function = *explosion->getOps<ctjs::FuncOp>().begin();
        mlir::DataFlowSolver solver;
        solver.load<mlir::dataflow::DeadCodeAnalysis>();
        solver.load<mlir::dataflow::SparseConstantPropagation>();
        solver.load<EscapeAnalysis>();
        const row r{.what = "switch path explosion is budgeted", .body = expanding, .expected = ""};
        if (failed(solver.initializeAndRun(*explosion))) {
            fail(r, "the switch path-budget fixture's solver did not converge");
            return;
        }
        const EscapeVerdicts original = computeVerdicts(solver, function, 0);
        for (std::size_t limit : {0U, 1U, 32U, 128U, 1024U}) {
            const auto result = computeArrayContents(function, limit);
            const EscapeVerdicts refined = computeVerdicts(solver, function, limit);
            if (result.complete || result.failure != ArrayContentsFailure::WorkLimit ||
                result.work != limit || !result.arrays.empty() || !result.reads.empty() ||
                !result.writes.empty() || !result.objects.empty() ||
                !result.propertyReads.empty() || !result.propertyWrites.empty() ||
                !result.propertyDeletions.empty() || !result.propertyCopies.empty() ||
                !result.exits.empty() || refined.arrayRetentionComplete ||
                refined.confinedStoredSites != 0 || refined.arrayRetentionWork != limit ||
                !llvm::all_of(original.sites, [&](const auto & entry) {
                    auto found = refined.sites.find(entry.first);
                    return found != refined.sites.end() &&
                           found->second.reason == entry.second.reason &&
                           found->second.by == entry.second.by &&
                           found->second.position == entry.second.position;
                })) {
                fail(r, "bounded switch enumeration published partial evidence");
            }
        }
    } else {
        fail(row{.what = "switch path explosion is budgeted", .body = expanding, .expected = ""},
             "the switch path-budget fixture did not parse");
    }
    std::printf("container switches: %zu rows, six live states, four malformed controls, "
                "%zu retention budget cutoffs, five path-explosion cutoffs\n",
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
    checkObjectContents(context);
    checkObjectDeletions(context);
    checkObjectCopies(context);
    checkOpaqueEntryTransport(context);
    checkSelectorProducers(context);
    checkLogicalNegation(context);
    checkTotalUnaryProducers(context);
    checkStaticBinaryProducers(context);
    checkArithmeticUnaryProducers(context);
    checkBigIntUnaryProducers(context);
    for (const auto kind : {ctjs::CompareKind::Eq, ctjs::CompareKind::Lt, ctjs::CompareKind::Le,
                            ctjs::CompareKind::Gt, ctjs::CompareKind::Ge}) {
        checkBigIntComparison(context, kind);
        checkPrimitiveBinaryProducer<ctjs::CompareOp>(context, kind);
    }
    for (const auto kind : {ctjs::BinaryKind::Sub, ctjs::BinaryKind::Mul, ctjs::BinaryKind::Div,
                            ctjs::BinaryKind::Mod, ctjs::BinaryKind::Pow, ctjs::BinaryKind::Add,
                            ctjs::BinaryKind::Concat}) {
        checkPrimitiveBinaryProducer<ctjs::BinaryOp>(context, kind);
    }
    checkArrayFrames(context);
    checkArrayConditionals(context);
    checkContainerSwitches(context);

    if (failures != 0) {
        std::printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
