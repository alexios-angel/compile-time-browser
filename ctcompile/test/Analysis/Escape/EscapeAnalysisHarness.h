#pragma once
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
//
// SPLIT 2026-09-08: this was the top of a single 2,763-line test/EscapeAnalysis.cpp.
// The harness - `row`, `kPrologue`, `check` and the printers every row file
// needs - lives here; the rows are EscapeAnalysisSinks.cpp,
// EscapeAnalysisCompletion.cpp and EscapeAnalysisStorage.cpp, and the array
// contents/retention tables are EscapeAnalysisArrays.cpp. Everything here is
// `inline` in a named namespace only so that four executables can include it.

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSEscapeEffects.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/EscapeAnalysis.h"
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

namespace ctcompile::test::escape {

using namespace ctcompile::ctnative;
namespace ctjs = ctcompile::ctjs;

inline int failures = 0;

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
inline constexpr const char * kPrologue =
    "ctjs.func @f(%receiver: !ctjs.value, %new_target: !ctjs.value, "
    "%callee: !ctjs.value, %p: !ctjs.value, %q: !ctjs.value) -> !ctjs.value "
    "attributes {upvalue_count = 0 : i32} {\n";

inline std::string verdictString(const EscapeVerdicts & verdicts, mlir::Operation * site) {
    auto found = verdicts.sites.find(site);
    if (found == verdicts.sites.end()) { return "<no verdict>"; }
    if (found->second.reason == EscapeReason::Confined) { return "confined"; }
    return "escapes:" + stringifyEscapeReason(found->second.reason).str();
}

inline std::string roleString(const RoleOf & role) {
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
inline std::string roleThroughInterface(ctjs::EscapeEffectOpInterface roles, unsigned index) {
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

inline void fail(const row & r, const std::string & message) {
    std::printf("FAIL %s\n  %s\n", r.what, message.c_str());
    ++failures;
}

inline std::string labelledAliases(const AliasValue & aliases) {
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

inline void checkRoles(const row & r, mlir::ModuleOp module) {
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

inline void check(mlir::MLIRContext & context, const row & r) {
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

inline std::string contentsLabel(mlir::Operation * operation) {
    if (auto label = operation->getAttrOfType<mlir::StringAttr>("storage_test_id")) {
        return label.getValue().str();
    }
    return operation->getName().getStringRef().str();
}

} // namespace ctcompile::test::escape
