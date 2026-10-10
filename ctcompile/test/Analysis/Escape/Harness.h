#pragma once
//===- Harness.h - Shared harness for the escape-analysis row tests -------===//
//
// Per-entry checks of the escape analysis's sink/carry table: one small
// function per operand role of each operation, checked individually, so a
// regression names the operand. The corpus oracle (AOTOracle.cpp,
// oracle-checker.test) says whether the analysis is sound overall but cannot
// say which table entry is wrong.
//
// The negative rows are the point. For every SINK entry there is a row in
// which that operand alone flips a site to `escapes:<reason>`; for every
// NEITHER and CARRY entry there is a row in which the site stays confined
// through it. An analysis that sank nothing would pass the positive rows and be
// wrong about `return {}`; an analysis that sank everything would pass the
// negative rows and prove nothing. The two families together pin the table.
//
// Every role is also asserted through the generated interface: each row's
// `roles` string names the operand roles of one operation, and the harness
// reads them back twice, once through ctjs::EscapeEffectOpInterface's
// getEffects (the ODS decorators, resource names and all) and once through
// operandRole (the path the verdict takes). If a decorator moves to the wrong
// operand, the interface check fails before the verdict does; if the C++
// default rule or kind switch drifts, the operandRole check fails while the
// interface still agrees.
//
// Two rows guard specific regressions: the return/throw rows fail if a sink is
// moved into visitOperation (the sparse framework never visits an operation
// with no results), and the default-rule row fails if an unannotated operation
// is made NEITHER.
//
// This is a C++ test rather than an ODS/TableGen declaration because TableGen
// cannot express that `return %s` and `return %p` behave differently.
//
// The rows live in Sinks.cpp, Completion.cpp and Storage.cpp; the array
// contents/retention tables live in Arrays/. Everything here is `inline` so
// several test executables can include it.
//
// Row descriptions cite the VM code behind each role with short names:
// o.cpp, c.cpp and rl.cpp are ctbrowser/lib/Script/vm/objects.cpp, call.cpp
// and run_loop.cpp as they were before being split into the vm/objects/,
// vm/call/ and vm/run_loop/ directories, and def is
// ctbrowser/include/ctbrowser/script/bytecode_opcodes.def. Their line numbers
// predate the split; search for the named function instead.
//
//===----------------------------------------------------------------------===//

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

#include "check.hpp"

namespace ctcompile::test::escape {

using namespace ctcompile::ctnative;
namespace ctjs = ctcompile::ctjs;

/// One table row. `body` owns its text (a `const char *` into a concatenation
/// temporary would dangle) and includes its terminator, since several rows
/// return or throw the site itself.
struct row {
    const char * what;
    std::string body;
    /// The verdict of the operation marked `check` ("confined" or
    /// "escapes:<reason>"), or, when `alias` is set, the printed alias set of
    /// its result.
    const char * expected;
    /// Optional diagnostic witness: the sinking operation name and operand.
    const char * by = nullptr;
    unsigned position = 0;
    /// "<op> <role> <role> ...": the role of every operand of the first
    /// operation of that name, in ODS argument order, each `neither`, `carry`
    /// or `sink:<reason>`. A leading `~` says the operation must not implement
    /// the interface (the kind switch, a branch, the default rule).
    const char * roles = nullptr;
    /// For the operation named in `roles`: "" means not a boxed site, anything
    /// else is its reason.
    const char * boxed = nullptr;
    unsigned unvisitedSites = 0;
    unsigned unvisitedOperands = 0;
    unsigned deadSites = 0;
    bool capturesAllArguments = false;
    const char * wholeFunction = nullptr;
    bool alias = false;
    /// Inspect the first operand's alias set, including a block argument that
    /// has no defining operation to mark with `check`.
    bool aliasOperand = false;
    /// When false, the solver runs without EscapeAnalysis, so every lattice is
    /// missing and the post-pass has to account for a live site it never saw.
    bool withAnalysis = true;
    /// Direct target evidence for the first Stored witness. This never changes
    /// `expected`, which still asserts the original escape verdict.
    const char * storageTarget = nullptr;
    const char * storageTargetVerdicts = nullptr;
    /// Query a synthetic Stored witness on the marked operation to exercise a
    /// missing target lattice or a malformed operand position independently.
    std::optional<unsigned> storageWitnessPosition = std::nullopt;
    /// Full direct-write census, including writes after the first sink and
    /// writes whose value has no tracked site. Semicolon-separated in IR order.
    const char * storageWrites = nullptr;
    std::optional<bool> completeStorage = std::nullopt;
    /// Diagnostic candidate write indices for each live property read. Neither
    /// their presence nor their absence describes the read's current contents.
    const char * loadReads = nullptr;
    std::optional<bool> completeLoads = std::nullopt;
    /// Separate bounded closure: deterministic labelled candidates per read,
    /// and all exposures of the marked site, even after its first Stored sink.
    const char * provenanceReads = nullptr;
    const char * provenanceExposures = nullptr;
    std::optional<bool> provenanceInputs = std::nullopt;
};

/// The function header every row shares: three implicit arguments then two
/// parameters, all aliasing an external object, which makes a parameter's role
/// observable (sinking %p sinks no site).
inline constexpr const char * kPrologue =
    "ctjs.func @f(%receiver: !ctjs.value, %new_target: !ctjs.value, "
    "%callee: !ctjs.value, %p: !ctjs.value, %q: !ctjs.value) -> !ctjs.value "
    "attributes {upvalue_count = 0 : i32} {\n";

/// Prints the verdict for `site` as "confined" or "escapes:<reason>".
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

/// The independent path: the ODS decorators as the generated interface reports
/// them, spelled from the resource's name rather than from the enum, so a
/// decorator on the wrong operand or a route with the wrong name fails here
/// even if operandRole happened to agree with the row.
inline std::string roleThroughInterface(ctjs::EscapeEffectOpInterface roles, unsigned index) {
    llvm::SmallVector<mlir::SideEffects::EffectInstance<ctjs::EscapeEffects::Effect>, 6> effects;
    roles.getEffects(effects);
    std::string role = "neither";
    for (const auto & effect : effects) {
        mlir::OpOperand * on = effect.getEffectValue<mlir::OpOperand *>();
        if (on == nullptr || on->getOperandNumber() != index) { continue; }
        if (llvm::isa<ctjs::EscapeEffects::Sink>(effect.getEffect())) {
            return "sink:" + effect.getResource()->getName().str();
        }
        if (llvm::isa<ctjs::EscapeEffects::Carry>(effect.getEffect())) { role = "carry"; }
    }
    return role;
}

inline void fail(const row & r, const std::string & message) {
    std::printf("FAIL %s\n  %s\n", r.what, message.c_str());
    ++ctbrowser_test_failures;
}

/// Prints an alias set with sites named by operation and `storage_test_id`.
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

/// Checks the operand roles and boxed-site reason that `r.roles` and `r.boxed`
/// name, through both operandRole and the generated interface.
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

/// Parses one row into a module, runs the solver and checks every expectation
/// the row sets, plus the counters and invariants every row must satisfy.
inline void check(mlir::MLIRContext & context, const row & r) {
    const std::string text = std::string{kPrologue} + r.body + "}\n";
    mlir::OwningOpRef<mlir::ModuleOp> module =
        mlir::parseSourceString<mlir::ModuleOp>(text, &context);
    if (!module) {
        fail(r, "the module did not parse:\n" + text);
        return;
    }

    // Both are required (TypeInference.h explains): without DeadCodeAnalysis
    // block arguments never receive the operands branched into them, and
    // without SparseConstantPropagation DeadCodeAnalysis marks no branch
    // successor live, so nothing past the first branch is visited.
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
        // The last result: ctjs.catch_land's first is an i32 pad id.
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

    // Assert every counter: a counter nobody asserts can silently stop
    // counting.
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

    // The closure hole, checked on every row: no alias set anywhere in the
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

/// Names an operation by its `storage_test_id` label, or by its operation name.
inline std::string contentsLabel(mlir::Operation * operation) {
    if (auto label = operation->getAttrOfType<mlir::StringAttr>("storage_test_id")) {
        return label.getValue().str();
    }
    return operation->getName().getStringRef().str();
}

} // namespace ctcompile::test::escape
