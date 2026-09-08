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
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Parser/Parser.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
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
    // Optional diagnostic witness: the sinking operation name and operand.
    const char * by = nullptr;
    unsigned position = 0;
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
    // Inspect the first operand's alias set, including a block argument that
    // has no defining operation to mark with `check`.
    bool aliasOperand = false;
    // false: the solver runs WITHOUT EscapeAnalysis, so every lattice is
    // missing and the post-pass has to account for a live site it never saw.
    bool withAnalysis = true;
    // Direct target evidence for the first Stored witness. This never changes
    // `expected`, which still asserts the original escape verdict.
    const char * storageTarget = nullptr;
    const char * storageTargetVerdicts = nullptr;
    // Query a synthetic Stored witness on the marked operation to exercise a
    // missing target lattice or a malformed operand position independently.
    std::optional<unsigned> storageWitnessPosition = std::nullopt;
    // Full direct-write census, including writes after the first sink and
    // writes whose value has no tracked site. Semicolon-separated in IR order.
    const char * storageWrites = nullptr;
    std::optional<bool> completeStorage = std::nullopt;
    // Diagnostic candidate write indices for each live property read. Neither
    // their presence nor their absence describes the read's current contents.
    const char * loadReads = nullptr;
    std::optional<bool> completeLoads = std::nullopt;
    // Separate bounded closure: deterministic labelled candidates per read,
    // and ALL exposures of the marked site, even after its first Stored sink.
    const char * provenanceReads = nullptr;
    const char * provenanceExposures = nullptr;
    std::optional<bool> provenanceInputs = std::nullopt;
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

std::string labelledAliases(const AliasValue & aliases) {
    if (aliases.isUninitialized()) { return "<uninitialized>"; }
    // Sort labels, not allocation addresses, so same-kind sites stay distinct.
    std::vector<std::string> names;
    for (mlir::Operation * site : aliases.getSites()) {
        std::string name = site->getName().getStringRef().str();
        if (auto label = site->getAttrOfType<mlir::StringAttr>("storage_test_id")) {
            name += "@" + label.getValue().str();
        }
        names.push_back(std::move(name));
    }
    std::sort(names.begin(), names.end());
    if (aliases.isExternal()) { names.emplace_back("external"); }
    return "{" + llvm::join(names, ", ") + "}";
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

    // These rows pin the ODS sink/carry table independently of the complete
    // array refinement, which has its own retention and refusal controls.
    const EscapeVerdicts verdicts = computeVerdicts(solver, function, 0);

    std::string got;
    if (r.alias || r.aliasOperand) {
        // The LAST result: ctjs.catch_land's first is an i32 pad id.
        mlir::Value value;
        if (r.aliasOperand && marked->getNumOperands() != 0) {
            value = marked->getOperand(0);
        } else if (!r.aliasOperand && marked->getNumResults() != 0) {
            value = marked->getResult(marked->getNumResults() - 1);
        }
        const AliasLattice * lattice = value ? solver.lookupState<AliasLattice>(value) : nullptr;
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
    if (r.by != nullptr) {
        auto found = verdicts.sites.find(marked);
        if (found == verdicts.sites.end() || found->second.by == nullptr ||
            found->second.by->getName().getStringRef() != r.by ||
            found->second.position != r.position) {
            fail(r, "escape witness does not name the expected operation and operand");
        }
    }
    if (r.storageTarget != nullptr) {
        const auto found = verdicts.sites.find(marked);
        const Verdict witness =
            r.storageWitnessPosition
                ? Verdict{EscapeReason::Stored, marked, *r.storageWitnessPosition}
                : (found != verdicts.sites.end() ? found->second : Verdict{});
        const AliasValue target = directStorageTarget(solver, witness);
        std::string aliases;
        llvm::raw_string_ostream os{aliases};
        target.print(os);
        if (aliases != r.storageTarget) {
            fail(r,
                 "storage target: expected " + std::string{r.storageTarget} + ", got " + aliases);
        }
        if (r.storageTargetVerdicts != nullptr) {
            std::vector<std::string> reasons;
            for (mlir::Operation * site : target.getSites()) {
                reasons.push_back(verdictString(verdicts, site));
            }
            std::sort(reasons.begin(), reasons.end());
            std::string joined;
            for (const std::string & reason : reasons) {
                if (!joined.empty()) { joined += ","; }
                joined += reason;
            }
            if (joined != r.storageTargetVerdicts) {
                fail(r, "storage target verdicts: expected " +
                            std::string{r.storageTargetVerdicts} + ", got " + joined);
            }
        }
    }
    if (r.storageWrites != nullptr) {
        std::string writes;
        llvm::raw_string_ostream os{writes};
        for (const DirectStorageWrite & write : verdicts.directStorage.writes) {
            if (!writes.empty()) { os << "; "; }
            os << write.by->getName().getStringRef() << '[' << write.position << "] "
               << labelledAliases(write.value) << " -> " << labelledAliases(write.target);
        }
        if (writes != r.storageWrites) {
            fail(r, "direct writes: expected " + std::string{r.storageWrites} + ", got " + writes);
        }
    }
    if (r.completeStorage && verdicts.directStorage.complete != *r.completeStorage) {
        fail(r, "direct-write census has the wrong completeness marker");
    }
    if (r.loadReads != nullptr) {
        std::string reads;
        llvm::raw_string_ostream os{reads};
        for (const DirectPropertyRead & read : verdicts.directLoads.reads) {
            if (!reads.empty()) { os << "; "; }
            os << read.by->getName().getStringRef() << ' ';
            read.base.print(os);
            os << " <- [";
            llvm::interleaveComma(read.candidateWrites, os);
            os << ']';
        }
        if (reads != r.loadReads) {
            fail(r, "direct reads: expected " + std::string{r.loadReads} + ", got " + reads);
        }
    }
    if (r.completeLoads && verdicts.directLoads.complete != *r.completeLoads) {
        fail(r, "direct-load census has the wrong completeness marker");
    }
    if (r.provenanceReads != nullptr || r.provenanceExposures != nullptr || r.provenanceInputs) {
        const LoadProvenanceEvidence provenance = computeLoadProvenance(solver, function, verdicts);
        if (!provenance.converged) { fail(r, "candidate provenance unexpectedly exhausted work"); }
        if (r.provenanceInputs && provenance.inputsComplete != *r.provenanceInputs) {
            fail(r, "candidate provenance has the wrong input completeness marker");
        }
        if (r.provenanceReads != nullptr) {
            std::string reads;
            llvm::raw_string_ostream os{reads};
            for (const PropertyReadProvenance & read : provenance.reads) {
                if (!reads.empty()) { os << "; "; }
                os << labelledAliases(read.base) << " -> " << labelledAliases(read.value);
                const AliasLattice * lattice =
                    solver.lookupState<AliasLattice>(read.by->getResult(0));
                if (lattice == nullptr || lattice->getValue() != AliasValue::external()) {
                    fail(r, "candidate provenance changed the original load result lattice");
                }
            }
            if (reads != r.provenanceReads) {
                fail(r, "provenance reads: expected " + std::string{r.provenanceReads} + ", got " +
                            reads);
            }
        }
        if (r.provenanceExposures != nullptr) {
            std::string exposures;
            llvm::raw_string_ostream os{exposures};
            for (const EscapeExposure & exposure : provenance.exposures) {
                if (!llvm::is_contained(exposure.value.getSites(), marked)) { continue; }
                if (!exposures.empty()) { os << "; "; }
                os << exposure.by->getName().getStringRef() << '[' << exposure.position
                   << "]:" << stringifyEscapeReason(exposure.reason);
            }
            if (exposures != r.provenanceExposures) {
                fail(r, "provenance exposures: expected " + std::string{r.provenanceExposures} +
                            ", got " + exposures);
            }
        }
    }
    // Every first Stored witness must also occur in the all-write census.
    // One write can contain more than one site after an alias join.
    for (const auto & [site, verdict] : verdicts.sites) {
        if (verdict.reason != EscapeReason::Stored) { continue; }
        if (!llvm::any_of(verdicts.directStorage.writes, [&](const DirectStorageWrite & write) {
                return write.by == verdict.by && write.position == verdict.position &&
                       llvm::is_contained(write.value.getSites(), site);
            })) {
            fail(r, "the direct-write census omitted a first Stored witness");
        }
    }

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

std::string contentsLabel(mlir::Operation * operation) {
    if (auto label = operation->getAttrOfType<mlir::StringAttr>("storage_test_id")) {
        return label.getValue().str();
    }
    return operation->getName().getStringRef().str();
}

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
        for (const auto & [array, elements] : contents.arrays) {
            if (!arrays.empty()) { arrays += "; "; }
            arrays += contentsLabel(array) + ":[";
            for (std::size_t i = 0; i < elements.size(); ++i) {
                if (i != 0) { arrays += ","; }
                arrays += contentsLabel(elements[i].getDefiningOp());
            }
            arrays += "]";
        }
        std::string reads;
        for (const ArrayElementRead & read : contents.reads) {
            if (!reads.empty()) { reads += "; "; }
            reads += contentsLabel(read.array) + "[" + std::to_string(read.index) +
                     "]=" + contentsLabel(read.value.getDefiningOp());
        }
        std::string exit;
        for (const ArrayContentsExit & edge : contents.exits) {
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
        {.what = "contents refuses multiple blocks including an unreachable region",
         .body = array + read + done + "^dead:\n  %args = ctjs.make_arguments\n" + done,
         .failure = ArrayContentsFailure::UnsupportedControlFlow},
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
         .boxed = "",
         .storageTarget = "{ctjs.create_array}",
         .storageTargetVerdicts = "confined"},

        // ===================================================================
        // ALLOCATION - the NEITHER/CARRY positives and the SINK negatives
        // ===================================================================
        {.what = "append: $array NEITHER (o.cpp:161-163 is push_back only)",
         .body = A + "  ctjs.append %p to %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.append neither sink:stored"},
        {.what = "append: $element SINK(stored)",
         .body = S + "  %a = ctjs.create_array []\n  ctjs.append %s to %a\n" + R,
         .expected = "escapes:stored",
         .storageTarget = "{ctjs.create_array}",
         .storageTargetVerdicts = "confined"},
        {.what = "create_cell: $initial SINK(stored), slot traced o.cpp:59; result phase59",
         .body = S + "  %c = ctjs.create_cell %s\n" + R,
         .expected = "escapes:stored",
         .roles = "ctjs.create_cell sink:stored",
         .boxed = "phase59",
         .storageTarget = "<uninitialized>"},
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
        {.what = "reloading a just-published local through a global stays external",
         .body = "  %o = ctjs.create_object\n"
                 "  ctjs.store_global \"g\", %o\n"
                 "  %g = ctjs.load_global \"g\" {check}\n" +
                 R,
         .expected = "{external}",
         .alias = true},
        {.what = "overwriting a global does not revoke the earlier publication",
         .body = S +
                 "  ctjs.store_global \"g\", %s\n"
                 "  %alias = ctjs.load_global \"g\"\n"
                 "  ctjs.store_global \"retained\", %alias\n"
                 "  ctjs.store_global \"g\", %p\n" +
                 R,
         .expected = "escapes:stored_global"},
        {.what = "a child published only through its container remains stored",
         .body = S +
                 "  %outer = ctjs.create_array [%s]\n"
                 "  ctjs.store_global \"g\", %outer\n" +
                 R,
         .expected = "escapes:stored",
         .storageTarget = "{ctjs.create_array}",
         .storageTargetVerdicts = "escapes:stored_global"},
        {.what = "the enclosing container keeps its distinct global-publication reason",
         .body = "  %child = ctjs.create_object\n"
                 "  %outer = ctjs.create_array [%child] {check}\n"
                 "  ctjs.store_global \"g\", %outer\n" +
                 R,
         .expected = "escapes:stored_global"},
        {.what = "set_property: $object NEITHER, the star proof (o.cpp:397-419 walks null)",
         .body = S + "  ctjs.set_property %s[%p], %q\n" + R,
         .expected = "confined",
         .roles = "ctjs.set_property neither sink:converted sink:stored"},
        {.what = "set_property: $key SINK(converted), to_string o.cpp:193 -> coerce.cpp:171-179",
         .body = S + "  ctjs.set_property %p[%s], %q\n" + R,
         .expected = "escapes:converted"},
        {.what = "set_property: $value SINK(stored), value.hpp:632-639",
         .body = S + "  ctjs.set_property %p[%q], %s\n" + R,
         .expected = "escapes:stored",
         .storageTarget = "{external}",
         .storageTargetVerdicts = ""},
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
        // Spread copies the argument array's CONTENTS into the callee window.
        // The array may stay confined while one of those contents outlives it;
        // the create_array/append sink must survive the spread's NEITHER row.
        {.what = "call_spread keeps its local argument array confined despite an object element",
         .body = "  %child = ctjs.create_object\n"
                 "  %args = ctjs.create_array [%child] {check}\n"
                 "  %r = ctjs.call_spread %p(%q, %args)\n" +
                 R,
         .expected = "confined"},
        {.what = "call_spread cannot revoke the escape of an element stored in its arguments",
         .body = S +
                 "  %args = ctjs.create_array [%s]\n"
                 "  %r = ctjs.call_spread %p(%q, %args)\n" +
                 R,
         .expected = "escapes:stored",
         .storageTarget = "{ctjs.create_array}",
         .storageTargetVerdicts = "confined"},
        {.what = "construct_spread keeps its local argument array confined with an object element",
         .body = "  %child = ctjs.create_object\n"
                 "  %args = ctjs.create_array [] {check}\n"
                 "  ctjs.append %child to %args\n"
                 "  %r = ctjs.construct_spread %p(%args)\n" +
                 R,
         .expected = "confined"},
        {.what = "construct_spread cannot revoke an appended element's escape",
         .body = S +
                 "  %args = ctjs.create_array []\n"
                 "  ctjs.append %s to %args\n"
                 "  %r = ctjs.construct_spread %p(%args)\n" +
                 R,
         .expected = "escapes:stored",
         .storageTarget = "{ctjs.create_array}",
         .storageTargetVerdicts = "confined"},
        {.what = "an iterable alias of the spread argument array remains confined",
         .body = A +
                 "  %args = ctjs.iterable of %s\n"
                 "  %r = ctjs.call_spread %p(%q, %args)\n" +
                 R,
         .expected = "confined"},
        {.what = "a spread receiver that aliases the argument array still escapes",
         .body = A +
                 "  %args = ctjs.iterable of %s\n"
                 "  %r = ctjs.call_spread %p(%s, %args)\n" +
                 R,
         .expected = "escapes:passed"},
        {.what = "later publication of an iterable argument alias sinks the original array",
         .body = A +
                 "  %args = ctjs.iterable of %s\n"
                 "  %r = ctjs.call_spread %p(%q, %args)\n"
                 "  ctjs.store_global \"arguments\", %args\n" +
                 R,
         .expected = "escapes:stored_global"},
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
        // A global owner is separate from confinement. Joining a local site
        // with an external value must retain BOTH facts: publication still
        // sinks the site, and the joined value cannot become local-only.
        {.what = "an external alternative does not erase a local site's global escape",
         .body = S +
                 "  %g = ctjs.load_global \"g\"\n"
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^join(%s : !ctjs.value), ^join(%g : !ctjs.value)\n"
                 "^join(%x: !ctjs.value):\n"
                 "  ctjs.store_global \"published\", %x\n" +
                 R,
         .expected = "escapes:stored_global"},
        {.what = "a fresh/global join retains both the local site and external identity",
         .body = "  %s = ctjs.create_object\n"
                 "  %g = ctjs.load_global \"g\"\n"
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^join(%s : !ctjs.value), ^join(%g : !ctjs.value)\n"
                 "^join(%x: !ctjs.value):\n"
                 "  %joined = ctjs.truthy %x {check}\n" +
                 R,
         .expected = "{ctjs.create_object, external}",
         .aliasOperand = true},
        {.what = "an external back edge cannot erase a loop-carried local's global escape",
         .body = S +
                 "  %g = ctjs.load_global \"g\"\n"
                 "  cf.br ^loop(%s : !ctjs.value)\n"
                 "^loop(%x: !ctjs.value):\n"
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^loop(%g : !ctjs.value), ^exit\n"
                 "^exit:\n"
                 "  ctjs.store_global \"published\", %x\n" +
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
         .wholeFunction = "suspended",
         .storageWrites = "ctjs.suspend[0] {external} -> <uninitialized>",
         .completeStorage = false},
        {.what = "suspend's own operand: the refusal is the first reason, not stored",
         .body = S + "  %r = ctjs.suspend yield %s\n" + R,
         .expected = "escapes:suspended",
         .wholeFunction = "suspended",
         .storageWrites = "ctjs.suspend[0] {ctjs.create_object} -> <uninitialized>",
         .completeStorage = false},

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

        // Region operands do not enumerate implicit SSA captures. The query
        // remains CFG-only, so every such capture is an unknown-op sink even
        // if a nested operation would otherwise have a NEITHER operand role.
        {.what = "a region's implicit capture cannot hide a global store",
         .body =
             S +
             "  %t = ctjs.truthy %p\n"
             "  scf.if %t {\n"
             "    ctjs.store_global \"held\", %s\n"
             "  }\n" +
             R,
         .expected = "escapes:unknown_op",
         .by = "ctjs.store_global",
         .roles = "~scf.if neither"},
        {.what = "the region capture scan reaches nested regions",
         .body =
             S +
             "  %t = ctjs.truthy %p\n"
             "  scf.if %t {\n"
             "    scf.if %t {\n"
             "      ctjs.store_global \"held\", %s\n"
             "    }\n"
             "  }\n" +
             R,
         .expected = "escapes:unknown_op"},
        {.what = "region capture of an iterable alias sinks the original array",
         .body =
             A +
             "  %alias = ctjs.iterable of %s\n"
             "  %t = ctjs.truthy %p\n"
             "  scf.if %t {\n"
             "    ctjs.store_global \"held\", %alias\n"
             "  }\n" +
             R,
         .expected = "escapes:unknown_op"},
        {.what = "a nested NEITHER use still requires a region proof",
         .body =
             S +
             "  %t = ctjs.truthy %p\n"
             "  scf.if %t {\n"
             "    %truth = ctjs.truthy %s\n"
             "  }\n" +
             R,
         .expected = "escapes:unknown_op"},
        {.what = "dead nested branches do not grant region semantics",
         .body =
             S +
             "  %no = arith.constant false\n"
             "  scf.if %no {\n"
             "    ctjs.store_global \"held\", %s\n"
             "  }\n" +
             R,
         .expected = "escapes:unknown_op"},
        {.what = "an unrelated region and its local values do not sink an outer site",
         .body =
             S +
             "  %t = ctjs.truthy %p\n"
             "  scf.if %t {\n"
             "    %local = ctjs.create_object\n"
             "    ctjs.store_global \"held\", %local\n"
             "  }\n" +
             R,
         .expected = "confined"},
        {.what = "a region in a dead CFG block cannot sink its outer capture",
         .body =
             S +
             "  cf.br ^exit\n"
             "^dead:\n"
             "  %t = ctjs.truthy %p\n"
             "  scf.if %t {\n"
             "    ctjs.store_global \"held\", %s\n"
             "  }\n"
             "  cf.br ^exit\n"
             "^exit:\n" +
             R,
         .expected = "confined"},
        {.what = "an unregistered region sinks outer captures but not its local block arguments",
         .body =
             S +
             "  \"test.region\"() ({\n"
             "  ^entry(%local: !ctjs.value):\n"
             "    \"test.use\"(%local) : (!ctjs.value) -> ()\n"
             "    ctjs.store_global \"held\", %s\n"
             "    \"test.end\"() : () -> ()\n"
             "  }) : () -> ()\n" +
             R,
         .expected = "escapes:unknown_op",
         .by = "ctjs.store_global"},
        {.what = "an outer CFG alias captured by a region still sinks the original site",
         .body =
             S +
             "  %t = ctjs.truthy %p\n"
             "  cf.cond_br %t, ^join(%s : !ctjs.value), ^join(%p : !ctjs.value)\n"
             "^join(%alias: !ctjs.value):\n"
             "  scf.if %t {\n"
             "    ctjs.store_global \"held\", %alias\n"
             "  }\n" +
             R,
         .expected = "escapes:unknown_op"},
        {.what = "an outer site's boolean result carries no object into a nested region",
         .body =
             S +
             "  %t = ctjs.truthy %s\n"
             "  scf.if %t {\n"
             "    \"test.use\"(%t) : (i1) -> ()\n"
             "  }\n" +
             R,
         .expected = "confined"},
        {.what = "a region-local allocation has no verdict even when another region captures it",
         .body = "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    %local = ctjs.create_object {check}\n"
                 "    scf.if %t {\n"
                 "      ctjs.store_global \"held\", %local\n"
                 "    }\n"
                 "  }\n" +
                 R,
         .expected = "<no verdict>"},
        {.what = "a region-local array yielded to the CFG has no confinement verdict",
         .body = "  %result = scf.execute_region -> !ctjs.value {\n"
                 "    %local = ctjs.create_array [] {check}\n"
                 "    scf.yield %local : !ctjs.value\n"
                 "  }\n"
                 "  ctjs.store_global \"held\", %result\n" +
                 R,
         .expected = "<no verdict>"},
        {.what = "a missing lattice for a region capture is counted without assuming confinement",
         .body = S + "  %t = ctjs.truthy %p\n"
                     "  scf.if %t {\n"
                     "    ctjs.store_global \"held\", %s\n"
                     "  }\n"
                     "  ctjs.resume_throw\n",
         .expected = "escapes:unvisited",
         .unvisitedSites = 1,
         .unvisitedOperands = 1,
         .withAnalysis = false},
        {.what = "a nested suspension retains outer frame sites without capturing their SSA values",
         .body =
             S +
             "  %t = ctjs.truthy %p\n"
             "  scf.if %t {\n"
             "    %r = ctjs.suspend await %p\n"
             "  }\n" +
             R,
         .expected = "escapes:suspended",
         .by = "ctjs.suspend",
         .wholeFunction = "suspended"},
        {.what = "a dead nested suspension still requires a region control-flow proof",
         .body =
             S +
             "  %no = arith.constant false\n"
             "  scf.if %no {\n"
             "    %r = ctjs.suspend yield %p\n"
             "  }\n" +
             R,
         .expected = "escapes:suspended",
         .wholeFunction = "suspended"},
        {.what = "a suspension nested in a dead CFG block does not refuse the live frame",
         .body = S + R +
                 "^dead:\n"
                 "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    %r = ctjs.suspend await %p\n"
                 "  }\n" +
                 R,
         .expected = "confined"},
        {.what = "a nested arguments builder cannot bypass the raw-frame placement guard",
         .body =
             S +
             "  %t = ctjs.truthy %p\n"
             "  scf.if %t {\n"
             "    %a = ctjs.make_arguments\n"
             "  }\n" +
             R,
         .expected = "escapes:arguments_late",
         .by = "ctjs.make_arguments",
         .capturesAllArguments = true,
         .wholeFunction = "arguments_late"},
        {.what = "a nested rest builder before a site still lacks the required prologue proof",
         .body = "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    %a = ctjs.gather_rest from 0\n"
                 "  }\n" +
                 S + R,
         .expected = "escapes:arguments_late",
         .by = "ctjs.gather_rest",
         .capturesAllArguments = true,
         .wholeFunction = "arguments_late"},
        {.what = "an arguments builder nested in a dead CFG block does not mark the live frame",
         .body = S + R +
                 "^dead:\n"
                 "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    %a = ctjs.make_arguments\n"
                 "  }\n" +
                 R,
         .expected = "confined"},
        {.what = "a later nested capture preserves the first escape reason",
         .body =
             S +
             "  ctjs.store_global \"first\", %s\n"
             "  %t = ctjs.truthy %p\n"
             "  scf.if %t {\n"
             "    %truth = ctjs.truthy %s\n"
             "  }\n" +
             R,
         .expected = "escapes:stored_global",
         .by = "ctjs.store_global"},
        {.what = "a nested capture records its actual sinking operand for diagnostics",
         .body =
             S +
             "  %t = ctjs.truthy %p\n"
             "  scf.if %t {\n"
             "    ctjs.set_property %p[%q], %s\n"
             "  }\n" +
             R,
         .expected = "escapes:unknown_op",
         .by = "ctjs.set_property",
         .position = 2},

        // DIRECT STORAGE TARGETS ARE DIAGNOSTICS, not contents proofs. The
        // original Stored verdict remains even when every target is confined.
        {.what = "a fixed property store identifies its confined object target",
         .body =
             S +
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
         .body =
             S +
             "  %outer = ctjs.create_object\n"
             "  ctjs.define_accessor \"child\" on %outer get %p set %q\n"
             "  %key = ctjs.constant #ctjs.string<\"child\">\n"
             "  ctjs.set_property %outer[%key], %s\n" +
             R,
         .expected = "escapes:stored",
         .storageTarget = "{ctjs.create_object}",
         .storageTargetVerdicts = "escapes:accessor_defined"},
        {.what = "a primitive direct target is not an external target",
         .body =
             S +
             "  %zero = ctjs.constant #ctjs.number<0>\n"
             "  ctjs.set_property %zero[%q], %s\n" +
             R,
         .expected = "escapes:stored",
         .storageTarget = "{}",
         .storageTargetVerdicts = ""},
        {.what = "an external alternative survives a storage target join",
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
             "  %zero = ctjs.constant #ctjs.number<0>\n"
             "  ctjs.set_property %s[%zero], %p\n"
             "  ctjs.set_property %s[%zero], %zero\n" +
             R,
         .expected = "confined",
         .storageWrites = "ctjs.set_property[2] {external} -> {ctjs.create_object}; "
                          "ctjs.set_property[2] {} -> {ctjs.create_object}",
         .completeStorage = true},
        {.what = "a joined stored value retains its local and external alternatives",
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
             "  %outer = ctjs.create_array [%s]\n"
             "  %cell = ctjs.create_cell %p\n"
             "  %read = ctjs.get_property %outer[%q]\n" +
             R,
         .expected = "escapes:stored",
         .loadReads = "ctjs.get_property {ctjs.create_array} <- [0]",
         .completeLoads = false,
         .provenanceInputs = false},
        {.what = "nested reads require region proof and leave top-level evidence incomplete",
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
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
         .body =
             S +
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
    checkArrayContents(context);
    checkArrayRetention(context);

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
