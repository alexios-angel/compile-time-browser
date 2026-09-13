#include "Harness.h"

namespace ctcompile::test::escape::arrays {

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
        } else if ((verdict.reason != EscapeReason::Stored &&
                    !(verdict.reason == EscapeReason::Passed &&
                      llvm::isa<ctjs::CreateArrayOp>(site) && verdict.position == 0 &&
                      llvm::isa_and_nonnull<ctjs::GetPropertyOp, ctjs::SetPropertyOp>(
                          verdict.by))) ||
                   after.reason != EscapeReason::Confined || after.by != nullptr ||
                   after.position != 0 || !refined.arrayRetentionComplete) {
            fail(r, "the consumer changed a verdict outside complete storage/receiver retention");
        } else if (verdict.reason == EscapeReason::Stored) {
            // This counter remains about retained contents, not private receivers.
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

} // namespace ctcompile::test::escape::arrays
