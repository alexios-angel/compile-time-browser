// PHASE 55A'S HALF OF THE ESCAPE ORACLE: the static claim per allocation site,
// in the format tools/check/escape-oracle.py reads.
//
// TypeClaims.cpp's shape, for the same reasons it has that shape: the
// interpreter's recording (TypeOracle.cpp --escape) and this file never meet
// until the checker compares them; this links MLIR and no recorder, that links
// the recorder and no MLIR. See 25-escape-analysis.md §3.4.
//
// A SITE IS AN ALLOCATING OPERATION IN A LIVE BLOCK, keyed by the bytecode pc
// the importer already put in its location - `NameLoc "program:<id>:<fn>:<at>"`
// inside a FusedLoc - so no importer change was needed to join a claim to an
// observation. A site in a block DeadCodeAnalysis proved dead is dropped; a
// site in a live block the solver never visited is claimed `escapes:unvisited`
// and COUNTED, and check-escape-claims.cmake gates that count at zero, exactly
// as TypeClaims.cpp does for values.
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTJS/Import/BytecodeImport.hpp"
#include "ctcompile/CTNative/Analysis/EscapeAnalysis.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "ctbrowser/script/compile.hpp"
#include "ctbrowser/script/type_record.hpp"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/ADT/STLExtras.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>

namespace {

using namespace ctcompile::ctnative;

// The kind the recorder tags an allocation with, per site op. Only the two
// tracked kinds ever claim `confined`; the others are BOXED-SITEs the checker
// reports as IMPRECISE rather than UNCLAIMED, which is what turns Phase 59's
// backlog into a number.
const char * kindOf(mlir::Operation * op) {
    if (llvm::isa<ctcompile::ctjs::CreateObjectOp>(op)) { return "obj"; }
    if (llvm::isa<ctcompile::ctjs::CreateArrayOp, ctcompile::ctjs::GatherRestOp,
                  ctcompile::ctjs::OwnKeysOp, ctcompile::ctjs::MakeArgumentsOp>(op)) {
        return "arr";
    }
    if (llvm::isa<ctcompile::ctjs::CreateClosureOp>(op)) { return "fn"; }
    if (llvm::isa<ctcompile::ctjs::CreateCellOp>(op)) { return "cell"; }
    return nullptr;
}

} // namespace

int main(int argc, char ** argv) {
    std::string script;
    std::string out;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const auto next = [&]() -> std::string { return ++i < argc ? argv[i] : std::string{}; };
        if (flag == "--script") {
            script = next();
        } else if (flag == "--out") {
            out = next();
        } else {
            std::fprintf(stderr, "usage: escape-claims --script BUNDLE.js --out CLAIMS\n");
            return 2;
        }
    }
    if (script.empty() || out.empty()) {
        std::fprintf(stderr, "escape-claims: --script and --out are both required\n");
        return 2;
    }

    std::ifstream in{script, std::ios::binary};
    if (!in) {
        std::fprintf(stderr, "escape-claims: cannot read %s\n", script.c_str());
        return 1;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string source = buffer.str();

    const ctbrowser::script::program prog = ctbrowser::script::compiler::compile(source);
    if (!prog.ok) {
        std::fprintf(stderr, "escape-claims: %s does not compile: %s\n", script.c_str(),
                     prog.error.c_str());
        return 1;
    }
    const std::uint64_t hash = ctbrowser::script::program_source_hash(prog);

    mlir::MLIRContext context;
    context.getOrLoadDialect<ctcompile::ctjs::CTJSDialect>();
    context.getOrLoadDialect<CTNativeDialect>();

    ctcompile::js::import_result imported = ctcompile::js::import_program(prog, "corpus", &context);
    if (!imported.module) {
        std::fprintf(stderr, "escape-claims: %s did not import\n", script.c_str());
        return 1;
    }

    // All three, and none optional - TypeInference.h says why.
    mlir::DataFlowSolver solver;
    solver.load<mlir::dataflow::DeadCodeAnalysis>();
    solver.load<mlir::dataflow::SparseConstantPropagation>();
    solver.load<EscapeAnalysis>();
    if (failed(solver.initializeAndRun(imported.module->getOperation()))) {
        std::fprintf(stderr, "escape-claims: the solver did not converge\n");
        return 1;
    }

    {
        std::map<std::string, std::size_t> reasons;
        for (const ctcompile::js::unsupported_opcode & skip : imported.skipped) {
            ++reasons[skip.reason];
        }
        for (const auto & [reason, count] : reasons) {
            std::fprintf(stderr, "escape-claims: %zu function(s) not imported: %.80s\n", count,
                         reason.c_str());
        }
    }

    std::FILE * file = std::fopen(out.c_str(), "w");
    if (file == nullptr) {
        std::fprintf(stderr, "escape-claims: cannot write %s\n", out.c_str());
        return 1;
    }
    std::fprintf(file, "# ctcompile Phase 55A, over %s\n", script.c_str());

    std::size_t functions = 0;
    std::size_t sites = 0;
    std::size_t confined = 0;
    std::size_t deadSites = 0;
    std::size_t unvisitedSites = 0;
    std::size_t unvisitedOperands = 0;
    std::size_t blocks = 0;
    std::size_t liveBlocks = 0;
    std::size_t unparsed = 0;
    std::size_t stored = 0;
    std::size_t localConfinedTargets = 0;
    std::size_t localEscapingTargets = 0;
    std::size_t mixedLocalTargets = 0;
    std::size_t externalTargets = 0;
    std::size_t primitiveTargets = 0;
    std::size_t unresolvedTargets = 0;
    std::size_t directWrites = 0;
    std::size_t storageSiteEdges = 0;
    std::size_t multipleStoreSites = 0;
    std::size_t otherFirstSites = 0;
    std::size_t unresolvedWriteValues = 0;
    std::size_t unresolvedWriteTargets = 0;
    std::size_t coveredStoredWitnesses = 0;
    std::size_t completeStorageFunctions = 0;
    std::size_t incompleteStorageFunctions = 0;
    std::size_t directReads = 0;
    std::size_t livePropertyReads = 0;
    std::size_t coveredPropertyReads = 0;
    std::size_t linkedReads = 0;
    std::size_t candidateLinks = 0;
    std::size_t candidateStoredSites = 0;
    std::size_t externalReadBases = 0;
    std::size_t unresolvedReadBases = 0;
    std::size_t invalidReadLinks = 0;
    std::size_t completeLoadFunctions = 0;
    std::size_t incompleteLoadFunctions = 0;
    std::size_t provenanceReads = 0;
    std::size_t provenanceReadSites = 0;
    std::size_t provenanceStoredSites = 0;
    std::size_t provenanceExposures = 0;
    std::size_t provenanceExposureSites = 0;
    std::size_t propagatedExposureSites = 0;
    std::size_t invalidProvenance = 0;
    std::size_t liveSinks = 0;
    std::size_t convergedProvenance = 0;
    std::size_t exhaustedProvenance = 0;
    std::size_t completeProvenanceInputs = 0;
    std::size_t incompleteProvenanceInputs = 0;
    std::size_t provenanceWork = 0;

    imported.module->walk([&](ctcompile::ctjs::FuncOp fn) {
        ++functions;
        std::optional<unsigned> functionIndex;

        // THE FUNCTION INDEX is the importer's own naming: every ctjs.func is
        // `@<name>$<index>`, the same index the recorder writes as `fn <index>`.
        {
            const llvm::StringRef name = fn.getSymName();
            const std::size_t dollar = name.rfind('$');
            unsigned parsed = 0;
            if (dollar != llvm::StringRef::npos &&
                !name.substr(dollar + 1).getAsInteger(10, parsed)) {
                functionIndex = parsed;
            }
        }
        EscapeVerdicts verdicts = computeVerdicts(solver, fn);
        const LoadProvenanceEvidence provenance = computeLoadProvenance(solver, fn, verdicts);
        if (provenance.converged) {
            ++convergedProvenance;
        } else {
            ++exhaustedProvenance;
        }
        if (provenance.inputsComplete) {
            ++completeProvenanceInputs;
        } else {
            ++incompleteProvenanceInputs;
        }
        provenanceWork += provenance.work;
        provenanceReads += provenance.reads.size();
        provenanceExposures += provenance.exposures.size();
        const auto extends = [](const AliasValue & original, const AliasValue & candidate) {
            return AliasValue::join(original, candidate) == candidate;
        };
        if (provenance.writes.size() != verdicts.directStorage.writes.size() ||
            provenance.reads.size() != verdicts.directLoads.reads.size()) {
            ++invalidProvenance;
        }
        for (const auto & [index, write] : llvm::enumerate(provenance.writes)) {
            provenanceStoredSites += write.value.getSites().size();
            if (index >= verdicts.directStorage.writes.size()) { continue; }
            const DirectStorageWrite & original = verdicts.directStorage.writes[index];
            if (write.by != original.by || write.position != original.position ||
                !extends(original.value, write.value) || !extends(original.target, write.target)) {
                ++invalidProvenance;
            }
        }
        for (const auto & [index, read] : llvm::enumerate(provenance.reads)) {
            provenanceReadSites += read.value.getSites().size();
            if (index >= verdicts.directLoads.reads.size()) { continue; }
            const DirectPropertyRead & original = verdicts.directLoads.reads[index];
            const AliasLattice * result = solver.lookupState<AliasLattice>(read.by->getResult(0));
            if (read.by != original.by || !extends(original.base, read.base) ||
                (result != nullptr && !extends(result->getValue(), read.value))) {
                ++invalidProvenance;
            }
        }
        for (const EscapeExposure & exposure : provenance.exposures) {
            const RoleOf role = operandRole(exposure.by, exposure.position);
            const AliasLattice * original =
                solver.lookupState<AliasLattice>(exposure.by->getOperand(exposure.position));
            if (role.role != OperandRole::Sink || role.reason != exposure.reason ||
                (original != nullptr && !extends(original->getValue(), exposure.value))) {
                ++invalidProvenance;
            }
            provenanceExposureSites += exposure.value.getSites().size();
            for (mlir::Operation * site : exposure.value.getSites()) {
                if (original == nullptr ||
                    !llvm::is_contained(original->getValue().getSites(), site)) {
                    ++propagatedExposureSites;
                }
            }
        }
        unvisitedSites += verdicts.unvisitedSites;
        unvisitedOperands += verdicts.unvisitedOperands;
        if (verdicts.directStorage.complete) {
            ++completeStorageFunctions;
        } else {
            ++incompleteStorageFunctions;
        }
        if (verdicts.directLoads.complete) {
            ++completeLoadFunctions;
        } else {
            ++incompleteLoadFunctions;
        }
        for (const DirectPropertyRead & read : verdicts.directLoads.reads) {
            ++directReads;
            if (!read.candidateWrites.empty()) { ++linkedReads; }
            if (read.base.isExternal()) { ++externalReadBases; }
            if (read.base.isUninitialized()) { ++unresolvedReadBases; }
            std::optional<std::size_t> previous;
            for (const std::size_t index : read.candidateWrites) {
                ++candidateLinks;
                if (index >= verdicts.directStorage.writes.size() ||
                    (previous && index <= *previous)) {
                    ++invalidReadLinks;
                    continue;
                }
                previous = index;
                const DirectStorageWrite & write = verdicts.directStorage.writes[index];
                candidateStoredSites += write.value.getSites().size();
                if (!llvm::any_of(read.base.getSites(), [&](mlir::Operation * site) {
                        return llvm::is_contained(write.target.getSites(), site);
                    })) {
                    ++invalidReadLinks;
                }
            }
        }
        std::map<mlir::Operation *, std::size_t> storesPerSite;
        for (const DirectStorageWrite & write : verdicts.directStorage.writes) {
            ++directWrites;
            storageSiteEdges += write.value.getSites().size();
            if (write.value.isUninitialized()) { ++unresolvedWriteValues; }
            if (write.target.isUninitialized()) { ++unresolvedWriteTargets; }
            for (mlir::Operation * site : write.value.getSites()) { ++storesPerSite[site]; }
        }
        for (const auto & [site, count] : storesPerSite) {
            if (count > 1) { ++multipleStoreSites; }
            const auto found = verdicts.sites.find(site);
            if (found != verdicts.sites.end() && found->second.reason != EscapeReason::Stored) {
                ++otherFirstSites;
            }
        }
        // Count FIRST WITNESSES, not complete ownership graphs. In particular,
        // a confined argument array can expose its children to a spread callee.
        // Keep this diagnostic separate from the oracle's retention claims.
        for (const auto & [site, verdict] : verdicts.sites) {
            if (verdict.reason != EscapeReason::Stored) { continue; }
            ++stored;
            if (llvm::any_of(verdicts.directStorage.writes, [&](const DirectStorageWrite & write) {
                    return write.by == verdict.by && write.position == verdict.position &&
                           llvm::is_contained(write.value.getSites(), site);
                })) {
                ++coveredStoredWitnesses;
            }
            const AliasValue target = directStorageTarget(solver, verdict);
            bool confinedTarget = false;
            bool escapingTarget = false;
            bool unclassifiedTarget = target.isUninitialized();
            for (mlir::Operation * targetSite : target.getSites()) {
                auto found = verdicts.sites.find(targetSite);
                if (found == verdicts.sites.end()) {
                    unclassifiedTarget = true;
                } else if (found->second.reason == EscapeReason::Confined) {
                    confinedTarget = true;
                } else {
                    escapingTarget = true;
                }
            }
            if (unclassifiedTarget) {
                ++unresolvedTargets;
            } else if (target.isExternal()) {
                ++externalTargets;
            } else if (confinedTarget && escapingTarget) {
                ++mixedLocalTargets;
            } else if (confinedTarget) {
                ++localConfinedTargets;
            } else if (escapingTarget) {
                ++localEscapingTargets;
            } else {
                ++primitiveTargets;
            }
        }

        for (mlir::Block & block : fn.getBody()) {
            ++blocks;
            const auto * executable = solver.lookupState<mlir::dataflow::Executable>(
                solver.getProgramPointBefore(&block));
            const bool live = executable != nullptr && executable->isLive();
            if (live) { ++liveBlocks; }
            for (mlir::Operation & op : block) {
                if (live) {
                    for (unsigned index = 0; index < op.getNumOperands(); ++index) {
                        if (operandRole(&op, index).role == OperandRole::Sink) { ++liveSinks; }
                    }
                }
                if (live && llvm::isa<ctcompile::ctjs::GetPropertyOp>(op)) {
                    ++livePropertyReads;
                    if (llvm::any_of(
                            verdicts.directLoads.reads,
                            [&](const DirectPropertyRead & read) { return read.by == &op; })) {
                        ++coveredPropertyReads;
                    }
                }
                const char * kind = kindOf(&op);
                if (kind == nullptr) { continue; }
                if (!live) {
                    ++deadSites;
                    continue;
                }
                const std::optional<unsigned> pc = allocationPc(&op);
                if (!pc || !functionIndex) {
                    ++unparsed;
                    continue;
                }
                const auto found = verdicts.sites.find(&op);
                std::string verdict;
                EscapeReason boxedReason = EscapeReason::Confined;
                if (isBoxedSite(&op, boxedReason)) {
                    // A BOXED-SITE: an allocating op the MVP does not track
                    // (closures, cells, the runtime's own arrays). Its reason
                    // rides on the op's ODS Res<> decorator, so the checker
                    // files it as IMPRECISE by that reason - the backlog, as a
                    // number - rather than as UNCLAIMED.
                    verdict = std::string{"escapes:"} + stringifyEscapeReason(boxedReason).str();
                } else if (found == verdicts.sites.end()) {
                    // A TRACKED site the post-pass never saw is one the solver
                    // never visited: boxed, and counted by computeVerdicts.
                    verdict = std::string{"escapes:"} +
                              stringifyEscapeReason(EscapeReason::Unvisited).str();
                } else if (found->second.reason == EscapeReason::Confined) {
                    verdict = "confined";
                    ++confined;
                } else {
                    verdict =
                        std::string{"escapes:"} + stringifyEscapeReason(found->second.reason).str();
                }
                std::fprintf(file, "escape %016llx %u %u %s %s\n",
                             static_cast<unsigned long long>(hash), *functionIndex, *pc, kind,
                             verdict.c_str());
                ++sites;
            }
        }
        if (functionIndex && verdicts.wholeFunction) {
            std::fprintf(file, "function %016llx %u refused:%s\n",
                         static_cast<unsigned long long>(hash), *functionIndex,
                         stringifyEscapeReason(*verdicts.wholeFunction).str().c_str());
        } else if (functionIndex && verdicts.capturesAllArguments) {
            std::fprintf(file, "function %016llx %u captures_all_arguments\n",
                         static_cast<unsigned long long>(hash), *functionIndex);
        }
    });
    std::fclose(file);

    std::fprintf(stderr,
                 "escape-claims: %zu functions, %zu sites claimed, %zu confined, %zu dead sites "
                 "dropped, %zu unvisited live sites, %zu unvisited operands, %zu unparsed "
                 "locations, %zu of %zu blocks live, program %016llx -> %s\n",
                 functions, sites, confined, deadSites, unvisitedSites, unvisitedOperands, unparsed,
                 liveBlocks, blocks, static_cast<unsigned long long>(hash), out.c_str());
    std::fprintf(stderr,
                 "stored first-witness targets: %zu sites, %zu local-confined, %zu "
                 "local-escaping, %zu local-mixed, %zu external-or-mixed, %zu primitive, %zu "
                 "unresolved (diagnostic only; Stored verdicts unchanged)\n",
                 stored, localConfinedTargets, localEscapingTargets, mixedLocalTargets,
                 externalTargets, primitiveTargets, unresolvedTargets);
    std::fprintf(stderr,
                 "stored direct-write census: %zu writes, %zu site edges, %zu multiple-store "
                 "sites, %zu other-first sites, %zu unresolved values, %zu unresolved targets\n",
                 directWrites, storageSiteEdges, multipleStoreSites, otherFirstSites,
                 unresolvedWriteValues, unresolvedWriteTargets);
    std::fprintf(stderr,
                 "stored direct-write coverage: %zu first witnesses of %zu Stored sites, %zu "
                 "complete and %zu incomplete of %zu functions (direct writes only; no contents "
                 "or retention proof)\n",
                 coveredStoredWitnesses, stored, completeStorageFunctions,
                 incompleteStorageFunctions, functions);
    std::fprintf(stderr,
                 "direct-load candidates: %zu links across %zu reads, %zu stored-site edges, "
                 "%zu external-or-mixed bases, %zu unresolved bases, %zu invalid links\n",
                 candidateLinks, linkedReads, candidateStoredSites, externalReadBases,
                 unresolvedReadBases, invalidReadLinks);
    std::fprintf(stderr,
                 "direct-load coverage: %zu records, %zu covered of %zu live reads, %zu complete "
                 "and %zu incomplete of %zu functions (shared local sites only; no points-to "
                 "or contents proof)\n",
                 directReads, coveredPropertyReads, livePropertyReads, completeLoadFunctions,
                 incompleteLoadFunctions, functions);
    std::fprintf(stderr,
                 "load-provenance candidates: %zu read-site edges, %zu stored-site edges, "
                 "%zu exposure-site edges, %zu propagated exposure edges, %zu invalid records\n",
                 provenanceReadSites, provenanceStoredSites, provenanceExposureSites,
                 propagatedExposureSites, invalidProvenance);
    std::fprintf(stderr,
                 "load-provenance coverage: %zu reads of %zu live reads, %zu exposures of %zu "
                 "live sinks, %zu converged and %zu exhausted of %zu functions, %zu complete "
                 "and %zu incomplete inputs, %zu work (diagnostic candidates; no contents proof)\n",
                 provenanceReads, livePropertyReads, provenanceExposures, liveSinks,
                 convergedProvenance, exhaustedProvenance, functions, completeProvenanceInputs,
                 incompleteProvenanceInputs, provenanceWork);
    return 0;
}
