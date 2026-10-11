//===- LowerToEmitC.cpp - Lower proved ctjs functions to EmitC ------------===//
//
// The native lowering. Everything before this pass is analysis. A `ctjs.func`
// whose every value has a proved type with a native C++ carrier becomes an
// `emitc.func` over those carriers. Any other function is refused: it gets a
// `ctnative.not_native` attribute naming the first value or operation that
// failed. The output links neither the interpreter nor its garbage collector,
// and nothing falls back to boxed values. Whether the whole program is native
// is decided from those per-function refusals.
//
// JavaScript Booleans and Numbers use ctnative::js_boolean_t and js_num.
// Strings own their bytes; nullable scalars keep distinct undefined/null tags.
// Collection and public ctbrowser storage boundaries extract raw C++ values
// explicitly. The carrier table (kCarriers) lives in LoweringSupport.h.
//
// This is deliberately not a dialect conversion. After --ctjs-lift-to-scf the
// body is structured, and after type inference every value's type is known.
// Retyping each value in place from the lattice and replacing each ctjs
// operation with its EmitC form leaves all of `scf` for upstream's
// --convert-scf-to-emitc, which already handles `scf.if`, `scf.for` and
// `scf.while`. A TypeConverter converts by type, but every JavaScript value has
// the same MLIR type; the lattice is per value.
//
//===----------------------------------------------------------------------===//

#include "../Analysis/OwnedGlobalRoots.h"
#include "../Analysis/OwnedMethodTableSlots.h"
#include "../HostContract/Preparation.h"
#include "Admission/Admission.h"
#include "ClosureLifting/ClosureLifter.h"
#include "EmitC/Emitter.h"
#include "Exceptions/Recovery.h"
#include "ForOf.h"
#include "Refinements.h"
#include "ctcompile/CTNative/Analysis/WorkBudget.h"
#include "ctcompile/CTNative/Transforms/Passes.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/Support/MemoryBuffer.h"

#include <optional>

namespace ctcompile::ctnative {

#define GEN_PASS_DEF_CTNATIVELOWERTOEMITC
#include "ctcompile/CTNative/Transforms/Passes.h.inc"

namespace {
using namespace lowering_detail;

/// Replaces host-declared undefined bindings (and absent bindings read through
/// `typeof`) with an `undefined` constant, and `typeof` of a proved fresh owner
/// with the constant "object". Called only on the private clone after complete
/// ownership validation; source writes never reach this path. All budget is
/// charged before any mutation, and the caller reproves the clone afterwards.
/// Returns false when `remaining` steps are not enough.
bool materializeHostPrimitives(mlir::ModuleOp module, const HostContract & contract,
                               const OwnedGlobalRoots & owners, unsigned remaining) {
    WorkBudget budget{remaining};
    llvm::SmallVector<ctjs::LoadGlobalOp> reads;
    llvm::SmallVector<ctjs::UnaryOp> objectTypes;
    const auto walked = module.walk<mlir::WalkOrder::PreOrder>(
        [&](mlir::Operation * operation) -> mlir::WalkResult {
            if (!budget.spend()) { return mlir::WalkResult::interrupt(); }
            if (auto query = llvm::dyn_cast<ctjs::UnaryOp>(operation);
                query && query.getKind() == ctjs::UnaryKind::TypeOf) {
                auto * origin = query.getOperand().getDefiningOp();
                // Root fields can hold other types. Only the actual fresh
                // owner and its checked global loads are ordinary objects.
                if (llvm::isa_and_nonnull<ctjs::CreateObjectOp, ctjs::LoadGlobalOp>(origin) &&
                    owners.lookup(origin)) {
                    if (!budget.spend(1 + llvm::range_size(query.getResult().getUses()))) {
                        return mlir::WalkResult::interrupt();
                    }
                    objectTypes.push_back(query);
                }
            }
            auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation);
            if (!load) { return mlir::WalkResult::advance(); }
            for (const auto * names : {&contract.undefinedBindings, &contract.absentBindings}) {
                if (names == &contract.absentBindings && !load.getTypeofLookup()) { continue; }
                for (const std::string & name : *names) {
                    if (!budget.spend()) { return mlir::WalkResult::interrupt(); }
                    if (load.getName() != name) { continue; }
                    if (!budget.spend(1 + llvm::range_size(load.getResult().getUses()))) {
                        return mlir::WalkResult::interrupt();
                    }
                    reads.push_back(load);
                    return mlir::WalkResult::advance();
                }
            }
            return mlir::WalkResult::advance();
        });
    if (walked.wasInterrupted()) { return false; }
    for (ctjs::LoadGlobalOp load : reads) {
        mlir::OpBuilder at(load);
        auto constant = ctjs::ConstantOp::create(at, load.getLoc(), load.getResult().getType(),
                                                 ctjs::UndefinedAttr::get(module.getContext()));
        load.getResult().replaceAllUsesWith(constant.getResult());
        load.erase();
    }
    for (ctjs::UnaryOp query : objectTypes) {
        mlir::OpBuilder at(query);
        auto constant =
            ctjs::ConstantOp::create(at, query.getLoc(), query.getResult().getType(),
                                     ctjs::StringAttr::get(module.getContext(), "object"));
        query.getResult().replaceAllUsesWith(constant.getResult());
        query.erase();
    }
    return true;
}

/// Marks each function bound to one of `names` as a string entry: the embedder
/// calls it from outside the script, after the script has run, with every
/// declared parameter a String. The function becomes public, so the solver
/// treats its callers as unknown, and carries `ctnative.string_entry` set to
/// the global's name: TypeInference::setToEntryState reads it to type those
/// parameters, and the emitter names the C++ function after it. Returns the
/// error for a name that is not one global bound once to one function.
std::optional<std::string> markStringEntries(mlir::ModuleOp module,
                                             llvm::ArrayRef<std::string> names) {
    auto rows = module->getAttrOfType<mlir::ArrayAttr>("ctjs.globals");
    if (!rows) { return "string-entries needs --ctjs-resolve-globals to have run first"; }
    for (const std::string & name : names) {
        if (!admission::isCIdentifier(name) || admission::isReservedInCpp(name) || name == "main") {
            return "string entry `" + name + "` cannot be a C++ function name";
        }
        ctjs::FuncOp fn;
        for (mlir::Attribute row : rows) {
            auto fields = llvm::dyn_cast<mlir::DictionaryAttr>(row);
            if (!fields) { continue; }
            auto bound = fields.getAs<mlir::StringAttr>("name");
            auto resolved = fields.getAs<mlir::FlatSymbolRefAttr>("resolved");
            if (bound && resolved && bound.getValue() == name) {
                fn = module.lookupSymbol<ctjs::FuncOp>(resolved);
            }
        }
        if (!fn) {
            return "string entry `" + name +
                   "` is not a global bound once to a function in this program";
        }
        // Every other function's C++ name is its mangled symbol, which ends in
        // the importer's index; refuse the one spelling that could collide.
        for (auto other : module.getOps<ctjs::FuncOp>()) {
            if (other != fn && cIdentifier(other.getSymName()) == name) {
                return "string entry `" + name + "` collides with function " +
                       other.getSymName().str();
            }
        }
        if (auto earlier = fn->getAttrOfType<mlir::StringAttr>("ctnative.string_entry")) {
            return "string entries `" + earlier.str() + "` and `" + name +
                   "` name the same function";
        }
        fn.setPublic();
        fn->setAttr("ctnative.string_entry", mlir::StringAttr::get(module.getContext(), name));
    }
    return std::nullopt;
}

/// The private-clone protocol that every host preparation follows. The module
/// is never mutated speculatively: `prepare` works on a clone under a copy of
/// the contract, refreshing its fingerprint whenever it reproves a step of its
/// own. The prepared clone is then fingerprinted and reproved by `Analysis`,
/// and only a complete proof publishes the clone's attributes and body into
/// the module and the refreshed contract into `hostContract`. `prepare` may
/// replace the clone with a further clone of it.
///
/// Returns the refusal, with `prefix` in front of an analysis refusal, or an
/// empty string when the module now holds the prepared body.
template <typename Analysis, typename Prepare>
std::string withProvedClone(mlir::ModuleOp module, std::optional<HostContract> & hostContract,
                            unsigned maxSteps, llvm::StringRef prefix, Prepare prepare) {
    mlir::OwningOpRef<mlir::ModuleOp> prepared(module.clone());
    HostContract transformed = *hostContract;
    if (std::string refusal = prepare(prepared, transformed); !refusal.empty()) { return refusal; }
    transformed.moduleSha256 = hostContractFingerprint(*prepared);
    const Analysis checked(*prepared, transformed, maxSteps);
    if (!checked.proved()) { return (prefix + checked.reason()).str(); }
    module->setAttrs((*prepared)->getAttrs());
    module.getBodyRegion().takeBody(prepared->getBodyRegion());
    hostContract = std::move(transformed);
    return {};
}

/// Prepares a DOM entry's local standard Maps. The complete DOM proof of the
/// clone types every other read and call, which is the identity evidence
/// prepareNativeMaps needs. A Map it cannot prove refuses the entry rather than
/// lowering boxed. Returns the refusal, or empty on success.
std::string prepareDOMMaps(mlir::ModuleOp module, std::optional<HostContract> & hostContract,
                           unsigned maxSteps) {
    bool maps = false;
    module.walk([&](ctjs::LoadGlobalOp load) { maps |= load.getName() == "Map"; });
    if (!maps) { return {}; }
    return withProvedClone<DOMEntryAnalysis>(
        module, hostContract, maxSteps, "native DOM Map entry: ",
        [&](mlir::OwningOpRef<mlir::ModuleOp> & prepared,
            HostContract & transformed) -> std::string {
            const DOMEntryAnalysis source(*prepared, transformed, maxSteps);
            if (!source.proved()) { return ("native DOM Map entry: " + source.reason()).str(); }
            // ponytail: the Map proof is unbudgeted here, as on the owned-global
            // path; thread a WorkBudget through its fixpoints if host-max-steps
            // must bound it too.
            prepareNativeMaps(
                *prepared, nullptr, [&](mlir::Operation * op) { return source.typedOperation(op); },
                [&](ctjs::CreateObjectOp object) { return source.recordObject(object); });
            std::string refusal;
            prepared->walk([&](mlir::Operation * op) {
                auto reason = op->getAttrOfType<mlir::StringAttr>(kNativeMapReason);
                if (reason && refusal.empty()) {
                    refusal = ("native DOM Map: " + reason.getValue()).str();
                }
            });
            return refusal;
        });
}

struct CTNativeLowerToEmitCPass : impl::CTNativeLowerToEmitCBase<CTNativeLowerToEmitCPass> {
    using CTNativeLowerToEmitCBase::CTNativeLowerToEmitCBase;

    void runOnOperation() override {
        mlir::ModuleOp module = getOperation();
        const auto fail = [&](const llvm::Twine & message) {
            module.emitError(message);
            signalPassFailure();
        };
        const auto notNative = [&](mlir::Operation * op, const llvm::Twine & reason) {
            op->setAttr("ctnative.not_native", mlir::StringAttr::get(&getContext(), reason));
        };
        std::optional<HostContract> hostContract;
        if (!hostManifest.empty()) {
            auto buffer = llvm::MemoryBuffer::getFile(hostManifest);
            if (!buffer) {
                return fail("cannot read native host contract: " + buffer.getError().message());
            }
            auto parsed = parseHostContract((*buffer)->getBuffer());
            if (!parsed) { return fail(llvm::toString(parsed.takeError())); }
            hostContract = std::move(*parsed);
        }
        if (!stringEntries.empty()) {
            if (hostContract) {
                return fail("string-entries cannot be combined with host-manifest");
            }
            if (auto error = markStringEntries(module, stringEntries)) { return fail(*error); }
        }
        const auto normalizeOwnedSource = [](mlir::ModuleOp candidate) {
            closureLifter local{candidate};
            local.discardNativeSourceFacts();
            local.census();
            liftReport locals;
            local.unboxCells(locals);
            candidate.walk([&](ctjs::CreateClosureOp closure) {
                if (local.whyTargetIsNotLiftable(closure) ||
                    ctjs::isUndefinedConstant(closure.getEnclosingThis())) {
                    return;
                }
                mlir::OpBuilder at(closure);
                closure.getEnclosingThisMutable().assign(ctjs::ConstantOp::create(
                    at, closure.getLoc(), ctjs::ValueType::get(candidate.getContext()),
                    ctjs::UndefinedAttr::get(candidate.getContext())));
            });
            local.eraseUnusedUnboxedCells();
            return locals;
        };
        const bool domData = hostContract && hostContract->provider ==
                                                 HostContract::Provider::ctbrowserDOMDataSession;
        if (domData) {
            if (hostContract->moduleSha256 != hostContractFingerprint(module)) {
                return fail("native DOM Data source: host contract module fingerprint mismatch");
            }
            const std::string refusal = withProvedClone<OwnedGlobalRoots>(
                module, hostContract, hostMaxSteps, "native DOM Data preparation: ",
                [&](mlir::OwningOpRef<mlir::ModuleOp> & prepared,
                    HostContract & transformed) -> std::string {
                    normalizeOwnedSource(*prepared);
                    transformed.moduleSha256 = hostContractFingerprint(*prepared);
                    const OwnedGlobalRoots source(*prepared, transformed, hostMaxSteps);
                    if (!source.proved()) {
                        return ("native DOM Data source: " + source.reason()).str();
                    }
                    if (auto wrapper = source.wrapper()) {
                        prepared->lookupSymbol<ctjs::FuncOp>(wrapper.getSymName()).erase();
                    }
                    prepared->walk(
                        [](mlir::Operation * op) { removeAttrsWithPrefix(op, "ctnative."); });
                    prepared->lookupSymbol<ctjs::FuncOp>(hostContract->entry).setPublic();
                    return {};
                });
            if (!refusal.empty()) { return fail(refusal); }
        }

        std::unique_ptr<DOMEntryAnalysis> domEntry;
        if (hostContract &&
            (hostContract->provider == HostContract::Provider::ctbrowserDOM ||
             hostContract->provider == HostContract::Provider::ctbrowserDOMSession ||
             hostContract->provider == HostContract::Provider::ctbrowserIntrinsics)) {
            if (auto error = prepareDOMEntry(module, *hostContract, hostMaxSteps)) {
                return fail(llvm::toString(std::move(error)));
            }
            if (auto refusal = prepareDOMMaps(module, hostContract, hostMaxSteps);
                !refusal.empty()) {
                return fail(refusal);
            }
            domEntry = std::make_unique<DOMEntryAnalysis>(module, *hostContract, hostMaxSteps);
        }

        // Run the default optimizations here, so clients do not need to
        // rebuild them in a shell or test pipeline. They run before closure
        // lifting: reachability still sees ctjs's complete reference forms,
        // and an unreachable private body never reaches type admission. A
        // nested pass manager owns analysis invalidation and instrumentation.
        // They are skipped under a host contract, whose fingerprinted input
        // is validated separately below, so general optimization cannot
        // silently invalidate a driver manifest.
        if (!hostContract && optimize && (precompute || pruneUnreachable)) {
            mlir::OpPassManager defaults(mlir::ModuleOp::getOperationName());
            if (precompute) {
                defaults.addPass(createCTNativePrecompute(
                    {.maxSteps = precomputeMaxSteps, .report = optimizationReport}));
            }
            if (pruneUnreachable) {
                CTNativePruneUnreachableOptions options;
                options.maxSteps = reachabilityMaxSteps;
                options.report = optimizationReport;
                defaults.addPass(createCTNativePruneUnreachable(options));
            }
            if (failed(runPipeline(defaults, module))) { return signalPassFailure(); }
        }

        // Closure lifting runs before the type solve. Lifting turns a closure
        // call into a ctjs.call_direct. That makes the target reachable to
        // DeadCodeAnalysis (an uncalled private function is dead, and every
        // type in it reads `<unvisited>`), and lets TypeInference carry each
        // capture's proved type into the leading parameter it became. Lifting
        // after the solve would need a second solve.
        liftReport lifted;
        if (hostContract && !domEntry && hostMaxSteps &&
            hostContract->moduleSha256 == hostContractFingerprint(module)) {
            // Normalizing local cells and unused receivers needs no host
            // assumptions. It is speculative: only a complete fresh-owner proof
            // can publish it. Nothing here is a diagnostic; a refusal leaves the
            // module as it was.
            liftReport locals;
            std::optional<liftReport> result;
            const std::string refusal = withProvedClone<OwnedGlobalRoots>(
                module, hostContract, hostMaxSteps, "",
                [&](mlir::OwningOpRef<mlir::ModuleOp> & prepared,
                    HostContract & transformed) -> std::string {
                    locals = normalizeOwnedSource(*prepared);
                    transformed.moduleSha256 = hostContractFingerprint(*prepared);
                    const OwnedGlobalRoots original(*prepared, transformed, hostMaxSteps);
                    if (!original.proved() || original.roots().empty() ||
                        !materializeHostPrimitives(*prepared, transformed, original,
                                                   hostMaxSteps - original.steps())) {
                        return "no complete fresh owner proof";
                    }
                    // Prepare only the checked table and environment,
                    // speculatively. A stale input never reaches this rewrite.
                    // Its internally derived contract is usable only if the
                    // complete live owner and callable queries succeed again
                    // on the transformed IR.
                    transformed.moduleSha256 = hostContractFingerprint(*prepared);
                    const OwnedGlobalRoots source(*prepared, transformed, hostMaxSteps);
                    closureLifter preparation{*prepared};
                    if (source.proved() && !source.roots().empty()) {
                        if (source.roots().front().methodTable) {
                            result = preparation.prepareOwnedGlobalMethodTables(source, transformed,
                                                                                hostMaxSteps);
                        } else {
                            // Scalar owners need no closure rewrite, but stale
                            // native facts already on the IR must not erase
                            // their live stores either.
                            preparation.discardNativeSourceFacts();
                            result = liftReport{};
                        }
                    }
                    if (!result) { return "no owned global preparation"; }
                    transformed.moduleSha256 = hostContractFingerprint(*prepared);
                    const OwnedGlobalRoots checked(*prepared, transformed, hostMaxSteps);
                    if (!checked.proved()) { return checked.reason().str(); }
                    // Map identity may pass the proved ordinary-root reads,
                    // never arbitrary host reads. Its annotations change the
                    // prepared fingerprint, so final ownership is independently
                    // checked once more before publication.
                    if (checked.roots().front().methodTable &&
                        checked.roots().front().methodTable->capturedMap) {
                        prepareNativeMaps(*prepared, &checked);
                        // The host body proves confinement of future local
                        // leaves, not their C++ representation. Rebuild the
                        // existing object/field use proof after Map actions are
                        // known, just as for an ordinary native source.
                        prepareNativeObjectIdentities(*prepared, &checked);
                    }
                    return {};
                });
            if (refusal.empty()) {
                lifted = *result;
                lifted.cells += locals.cells;
            }
        }
        closureLifter lifter{module, census};
        if (!hostContract) { lifted = lifter.run(); }
        // Method and local-binding lifts expose the complete direct caller set.
        // Reuse scalar guard folding before shape admission inspects dead arms.
        if (!hostContract && optimize && precompute && (lifted.calls || lifted.bound)) {
            mlir::OpPassManager guards(mlir::ModuleOp::getOperationName());
            guards.addPass(createCTNativePrecompute(
                {.maxSteps = precomputeMaxSteps, .report = optimizationReport}));
            if (failed(runPipeline(guards, module))) { return signalPassFailure(); }
        }
        // Values a branch tested get a narrowed name in that branch, after the
        // lift has unboxed the cells it could; a fresh census answers which of
        // the remaining cells hold one value (Refinements.cpp).
        // A for-of over an array keeps only its index loop (ForOf.cpp); a
        // refused function gets its body back below.
        ForOfOriginals forOfOriginals;
        if (!hostContract) { forOfOriginals = foldArrayForOf(module); }
        if (!hostContract) {
            closureLifter cells{module};
            cells.census();
            insertRefinements(module,
                              [&](ctjs::CreateCellOp cell) { return cells.isConstantCell(cell); });
        }
        // Exception recovery and structured-exit normalization are
        // speculative. They stay only if the function and its whole closed
        // call component pass admission. Refused functions get their original
        // bodies back (the restore loop before the shape census below). A
        // diagnostic is never a proof.
        llvm::SmallVector<std::pair<ctjs::FuncOp, mlir::OwningOpRef<ctjs::FuncOp>>, 0>
            recoveryOriginals;
        module.walk([&](ctjs::FuncOp fn) {
            if (hostContract) { return; }
            fn->removeAttr("ctnative.exception_refusal");
            fn->removeAttr("ctnative.structure_refusal");
            bool handlers = false;
            fn.walk([&](ctjs::PushHandlerOp) { handlers = true; });
            if (!handlers) {
                bool switches = false;
                fn.walk([&](mlir::scf::IndexSwitchOp) { switches = true; });
                if (!switches) { return; }
                // Reserve the copy's cost before allocating it, as class
                // method normalization and exception recovery already do.
                WorkBudget budget{structureMaxSteps};
                if (!budget.spendWalk(fn, [](mlir::Operation * op) {
                        return uint64_t(1) + op->getNumOperands() + op->getNumResults();
                    })) {
                    fn->setAttr("ctnative.structure_refusal",
                                mlir::StringAttr::get(
                                    &getContext(), "native structured-exit work budget exhausted"));
                    return;
                }
                mlir::OwningOpRef<ctjs::FuncOp> normalized(llvm::cast<ctjs::FuncOp>(fn->clone()));
                auto remaining = static_cast<unsigned>(budget.remaining);
                if (auto error = normalizeStructuredExits(*normalized, remaining)) {
                    fn->setAttr(
                        "ctnative.structure_refusal",
                        mlir::StringAttr::get(&getContext(), llvm::toString(std::move(error))));
                    return;
                }
                // Keep the original body for the same final admission rollback
                // used by exception recovery. No solver facts exist yet.
                mlir::OwningOpRef<ctjs::FuncOp> original(
                    llvm::cast<ctjs::FuncOp>(fn->cloneWithoutRegions()));
                original->getBody().takeBody(fn.getBody());
                fn.getBody().takeBody(normalized->getBody());
                recoveryOriginals.emplace_back(fn, std::move(original));
                return;
            }
            auto recovery = recoverPrimitiveExceptionRegion(fn, exceptionMaxSteps);
            if (recovery.recovered) {
                recoveryOriginals.emplace_back(fn, std::move(recovery.original));
            } else if (!recovery.refusal.empty()) {
                fn->setAttr("ctnative.exception_refusal",
                            mlir::StringAttr::get(&getContext(), recovery.refusal));
            }
        });
        if (!hostContract) {
            prepareNativeMaps(module);
            prepareNativeObjectIdentities(module);
        }
        if (census) {
            // The census is one deterministic line. StringMap iterates in hash
            // order, so entries are sorted by count and then by name; text that
            // moved between runs could not be pinned by a lit test or diffed
            // between two corpus runs.
            const auto ordered = [](const llvm::StringMap<unsigned> & from) {
                llvm::SmallVector<std::pair<llvm::StringRef, unsigned>> out;
                for (const auto & entry : from) { out.emplace_back(entry.first(), entry.second); }
                llvm::sort(out, [](const auto & a, const auto & b) {
                    return a.second != b.second ? a.second > b.second : a.first < b.first;
                });
                return out;
            };
            std::string text;
            llvm::raw_string_ostream into(text);
            into << "ctnative census: " << lifter.censusOpenObjects
                 << " method-bearing literal(s) refused as open, holding "
                 << lifter.censusMethodFields << " method field(s); blocking uses:";
            const auto counts = [&](const auto & census) {
                for (const auto & [label, count] : ordered(census)) {
                    into << " " << label << "=" << count;
                }
            };
            counts(lifter.censusUses);
            into << "; sole blocker:";
            counts(lifter.censusSole);
            into << "; sole blocker by method field:";
            counts(lifter.censusSoleFields);
            into << "; root of the nesting:";
            for (const auto & [label, count] : ordered(lifter.censusRoot)) {
                into << " " << label << "=" << count;
                const auto at = lifter.censusExample.find(label);
                if (at != lifter.censusExample.end()) { into << "@" << at->second; }
            }
            into << "; nesting depth:";
            counts(lifter.censusDepth);
            module.emitRemark() << text;
        }
        if (report) {
            module.emitRemark() << "ctnative: lifted " << lifted.closures << " closure(s) over "
                                << lifted.captures << " capture(s) into " << lifted.functions
                                << " function(s), rewrote " << lifted.calls << " call(s), unboxed "
                                << lifted.cells << " cell(s), " << lifted.methods
                                << " method(s) of which " << lifted.receivers
                                << " take a receiver, " << lifted.objects
                                << " object parameter(s), " << lifted.constructors
                                << " constructor site(s), " << lifted.locals
                                << " shared cell(s) made a frame-local variable carried through "
                                << lifted.carried << " pointer parameter(s), " << lifted.bindings
                                << " local binding(s) that held a function, of whose calls "
                                << lifted.bound << " were made direct from another frame; "
                                << lifted.callbackCalls << " callback call(s) named, "
                                << lifted.callbackParameters
                                << " call-only function parameter(s) erased; " << lifted.statics
                                << " box(es) of run-once functions made static variables";
        }

        // Compute the alias groups once the lift has written its attributes
        // and before anything reads a field. TypeInference::groupReceivers is
        // the one walk that says which values name the same object; admission
        // and the shape census both read it, so a field a method touches is
        // the same field the literal has in both.
        auto ownedGlobals =
            hostContract && !domEntry
                ? std::make_unique<OwnedGlobalRoots>(module, *hostContract, hostMaxSteps)
                : nullptr;
        receiverGroups groups = TypeInference::groupReceivers(module);
        if (ownedGlobals) {
            for (const auto & root : ownedGlobals->roots()) {
                auto owner = root.owner;
                llvm::SmallVector<mlir::Value, 2> aliases{owner.getResult()};
                for (ctjs::LoadGlobalOp load : root.loads) { aliases.push_back(load.getResult()); }
                for (mlir::Value alias : aliases) { groups[alias] = aliases; }
            }
        }
        const OwnedMethodTableSlots ownedTableSlots(module);

        // All three analyses are required; TypeInference.h says why.
        mlir::DataFlowSolver solver;
        solver.load<mlir::dataflow::DeadCodeAnalysis>();
        solver.load<mlir::dataflow::SparseConstantPropagation>();
        solver.load<TypeInference>(ownedGlobals.get(), domEntry.get());
        if (failed(solver.initializeAndRun(module))) {
            return fail("the type inference did not converge");
        }

        llvm::SmallVector<ctjs::FuncOp> functions;
        module.walk([&](ctjs::FuncOp fn) { functions.push_back(fn); });

        llvm::SmallVector<ctjs::FuncOp> accepted;
        // Rebuild the ownership proofs here, at their final consumer. The
        // solver did not mutate the source IR, and no diagnostic annotation may
        // stand in for this live query.
        auto admittedGlobals =
            hostContract && !domEntry
                ? std::make_unique<OwnedGlobalRoots>(module, *hostContract, hostMaxSteps)
                : nullptr;
        auto admittedDOM =
            domEntry ? std::make_unique<DOMEntryAnalysis>(module, *hostContract, hostMaxSteps)
                     : nullptr;
        if (admittedDOM && !admittedDOM->proved()) {
            return fail("native DOM entry admission: " + admittedDOM->reason());
        }
        if (admittedGlobals) {
            std::int64_t outerKeyObjects = 0;
            for (const auto & root : admittedGlobals->roots()) {
                if (root.methodTable && root.methodTable->capturedMap) {
                    outerKeyObjects += static_cast<std::int64_t>(
                        root.methodTable->capturedMap->outerKeyObjects.size());
                }
            }
            module->setAttr(
                "ctnative.host_outer_key_objects",
                mlir::IntegerAttr::get(mlir::IntegerType::get(&getContext(), 64), outerKeyObjects));
            module->setAttr("ctnative.host_owner_proved",
                            mlir::BoolAttr::get(&getContext(), admittedGlobals->proved()));
            module->setAttr("ctnative.host_owner_reason",
                            mlir::StringAttr::get(&getContext(), admittedGlobals->reason()));
        }
        for (ctjs::FuncOp fn : functions) {
            if (fn->hasAttr("ctjs.not_structured")) {
                notNative(fn, "unstructured control flow");
                continue;
            }
            admission check{solver,           {}, &groups, &ownedTableSlots, admittedGlobals.get(),
                            admittedDOM.get()};
            if (check.function(fn)) {
                accepted.push_back(fn);
            } else {
                notNative(fn, check.why);
            }
        }

        // Identity-object members are shared by property name across
        // allocation sites. Check every candidate store before removing any
        // call component, so that neither a narrowed read nor a separately
        // admitted function can choose an incompatible carrier for the same
        // emitted member.
        const auto fieldTypes = identityFieldStoreTypes(solver, accepted);
        llvm::erase_if(accepted, [&](ctjs::FuncOp fn) {
            std::string reason;
            fn.getBody().walk([&](mlir::Operation * op) {
                if (!reason.empty() || nativeObjectFieldGroup(op) < 0) { return; }
                const auto key = ctjs::constantKey(op->getOperand(1));
                const auto storage = carrierOf(fieldTypes.lookup(key));
                if (!isScalarCarrier(storage) && !isStringCarrier(storage)) {
                    reason = "owning field `" + key.str() +
                             "` has incompatible types across its complete store census";
                }
            });
            if (reason.empty()) { return false; }
            notNative(fn, reason);
            return true;
        });

        // The fixpoint: a function is native only if every function it calls
        // directly is. Drop any accepted function that calls a refused one,
        // name the callee in its diagnostic, and repeat until nothing changes,
        // so a refusal anywhere in a call chain reaches every caller.
        llvm::DenseSet<mlir::Operation *> nativeSet;
        for (ctjs::FuncOp fn : accepted) { nativeSet.insert(fn.getOperation()); }
        mlir::SymbolTable symbols(module);
        // The native set is closed in both directions. A native caller needs a
        // native callee to emit an emitc.call to, and a native callee needs
        // every caller native too: a refused caller keeps a ctjs.call_direct,
        // which must name a ctjs.func with a body, and a lowered function no
        // longer has one. So a refusal propagates along the call graph both
        // ways, each step naming the function that caused it. The global
        // census below can refuse the script entry after this has run, so it
        // runs again then.
        const auto closeNativeSet = [&] {
            for (bool changed = true; changed;) {
                changed = false;
                for (ctjs::FuncOp fn : functions) {
                    const bool native = nativeSet.contains(fn.getOperation());
                    // A native end of an edge whose other end is not native stops
                    // being native; the reason names the other end.
                    const auto edge = [&](mlir::Operation * target, const llvm::Twine & forward,
                                          const llvm::Twine & backward) {
                        const bool targetNative = target && nativeSet.contains(target);
                        if (native && !targetNative) {
                            nativeSet.erase(fn.getOperation());
                            notNative(fn, forward);
                            changed = true;
                        } else if (!native && targetNative) {
                            nativeSet.erase(target);
                            notNative(target, backward);
                            changed = true;
                        }
                    };
                    // A retained closure is an edge too: create_closure names its
                    // target even when the enclosing function never invokes it.
                    fn.getBody().walk([&](ctjs::CreateClosureOp made) {
                        // A borrowed callback names its callback the same way.
                        if (const auto target = borrowedTarget(made); !target.empty()) {
                            edge(symbols.lookup(target),
                                 "binds `" + target + "`, which is not native",
                                 "bound by `" + fn.getSymName() + "`, which is not native");
                            return;
                        }
                        if (environmentTarget(made).empty()) { return; }
                        edge(symbols.lookup(environmentTarget(made)),
                             "retains `" + environmentTarget(made) + "`, which is not native",
                             "retained by `" + fn.getSymName() + "`, which is not native");
                    });
                    fn.getBody().walk([&](ctjs::CallDirectOp call) {
                        edge(symbols.lookup(call.getCallee()),
                             "calls `" + call.getCallee() + "`, which is not native",
                             "called by `" + fn.getSymName() + "`, which is not native");
                    });
                    // A specialised Array-method call names its callback.
                    fn.getBody().walk([&](ctjs::CallOp call) {
                        auto callback =
                            call->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeArrayCallback);
                        if (!callback) { return; }
                        edge(symbols.lookup(callback.getValue()),
                             "calls back `" + callback.getValue() + "`, which is not native",
                             "called back by `" + fn.getSymName() + "`, which is not native");
                    });
                }
            }
            llvm::erase_if(accepted,
                           [&](ctjs::FuncOp fn) { return !nativeSet.contains(fn.getOperation()); });
        };
        closeNativeSet();
        if (admittedDOM && (!llvm::is_contained(accepted, admittedDOM->entry()) ||
                            llvm::any_of(admittedDOM->callbacks(), [&](ctjs::FuncOp callback) {
                                return !llvm::is_contained(accepted, callback);
                            }))) {
            auto reason =
                admittedDOM->entry()->getAttrOfType<mlir::StringAttr>("ctnative.not_native");
            return fail("native DOM entry did not pass complete native admission" +
                        (reason ? ": " + reason.getValue().str() : std::string{}));
        }

        lowering lower{solver, &getContext(), module};
        lower.groups = &groups;
        if (domData) {
            if (!admittedGlobals || !admittedGlobals->proved() ||
                accepted.size() != functions.size()) {
                return fail("native DOM Data requires complete family admission");
            }
            lower.domDataEntry = cIdentifier(hostContract->entry);
            lower.domDataSession = lower.domDataEntry + "_session";
            lower.needsDOM = true;
            for (auto parameter : admittedGlobals->domInputs()) {
                lower.domParameters.insert(parameter);
            }
        }
        if (admittedGlobals && admittedGlobals->proved()) {
            lower.explicitObservations = true;
            for (const auto & name : hostContract->observations) {
                lower.observations.insert(name);
            }
        }
        for (ctjs::FuncOp fn : accepted) {
            // A string entry is named after the global the embedder calls;
            // markStringEntries checked that C++ can spell it.
            const auto entry = fn->getAttrOfType<mlir::StringAttr>("ctnative.string_entry");
            lower.names[fn.getSymName()] = !admittedDOM && !domData && isScriptEntry(fn) ? "main"
                                           : entry ? entry.str()
                                                   : cIdentifier(fn.getSymName());
        }

        // The global census, over the whole accepted set and before any
        // function is lowered.
        //
        // Two things depend on it. First, `main` prints the globals in this
        // set, and main (the importer's function 0) is lowered first, so the
        // set must be complete before any function is lowered or a global used
        // only inside a helper is never printed.
        //
        // Second, a global with no store anywhere in the unit is `undefined`,
        // and native code carries undefined as NaN. That is exact for
        // arithmetic and comparison but not for printing, where the
        // interpreter does not print a number and the binary would print
        // `nan`. The difference is observable, so it is refused rather than
        // represented, which is the rule this file is built on.
        //
        // A top-level function's name is a global too, but not one of these:
        // `function f(){}` stores a closure into global "f" and every call
        // loads it, and both lower to nothing because the calls are direct.
        // Counting them would declare and print a spurious `f=0`.
        const auto bindsAFunction = [](ctjs::StoreGlobalOp store) {
            return llvm::isa_and_nonnull<ctjs::CreateClosureOp>(store.getValue().getDefiningOp());
        };
        const auto callsOnly = [](ctjs::LoadGlobalOp load) {
            return !load.getResult().use_empty() &&
                   llvm::all_of(load.getResult().getUsers(), [](mlir::Operation * user) {
                       return llvm::isa<ctjs::CallDirectOp>(user);
                   });
        };
        // A refused entry leaves its callees without a native caller, so the
        // call graph is closed again and the census redone over what is left.
        for (bool refusedEntry = true; refusedEntry;) {
            refusedEntry = false;
            lower.globals.clear();
            llvm::StringSet<> storedGlobals;
            for (ctjs::FuncOp fn : accepted) {
                fn.getBody().walk([&](ctjs::StoreGlobalOp store) {
                    if (!bindsAFunction(store)) { storedGlobals.insert(store.getName()); }
                });
            }
            llvm::SmallVector<llvm::StringRef> neverStored;
            for (ctjs::FuncOp fn : accepted) {
                fn.getBody().walk([&](mlir::Operation * o) {
                    if (admittedGlobals &&
                        (admittedGlobals->lookup(o) || admittedGlobals->objectGlobal(o))) {
                        return;
                    }
                    llvm::StringRef name;
                    if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(o)) {
                        if (admittedDOM && domBookkeeping(*admittedDOM, load)) { return; }
                        if (callsOnly(load) || isNativeMapBookkeeping(load)) { return; }
                        // The RegExp literal factory behind split(/\s+/) lowers
                        // to nothing; it is not a program global.
                        if (isVectorCallPlumbing(load)) { return; }
                        name = load.getName();
                    }
                    if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(o)) {
                        if (bindsAFunction(store)) { return; }
                        name = store.getName();
                    }
                    if (name.empty()) { return; }
                    if (lower.globals.insert(name).second && !storedGlobals.contains(name)) {
                        neverStored.push_back(name);
                    }
                });
            }
            if (!neverStored.empty()) {
                llvm::sort(neverStored);
                const auto entry = llvm::find_if(accepted, isScriptEntry);
                if (entry != accepted.end()) {
                    notNative(*entry, "global `" + neverStored.front() +
                                          "` is read but never stored, so it is undefined - which "
                                          "this tier carries as NaN, and printing that as a number "
                                          "is not what the interpreter answers");
                    nativeSet.erase(entry->getOperation());
                    closeNativeSet();
                    refusedEntry = true;
                }
            }
        }

        // The shape census runs for the same reason and in the same place:
        // after the last function has left the accepted set and before the
        // first is lowered. Emitted structs are keyed on the object's shape,
        // not on its creation site, and whether a shape's definition is a
        // template depends on every site in the program, so no site's type can
        // be written until all of them have been seen. First, refused
        // functions get back the bodies speculative recovery replaced.
        for (auto & [fn, original] : recoveryOriginals) {
            if (llvm::is_contained(accepted, fn)) { continue; }
            fn.getBody().takeBody(original->getBody());
            setOrRemoveAttr(fn, "ctjs.not_structured", (*original)->getAttr("ctjs.not_structured"));
        }
        // After recovery's, which may hold the folded body.
        for (auto & [fn, original] : forOfOriginals) {
            if (!llvm::is_contained(accepted, fn)) { fn.getBody().takeBody(original->getBody()); }
        }
        if (admittedDOM && llvm::is_contained(accepted, admittedDOM->entry())) {
            lower.censusDOM(*admittedDOM,
                            hostContract->provider == HostContract::Provider::ctbrowserDOMSession);
        }
        lower.censusScalars(accepted, admittedGlobals.get());
        if (domData && !lower.censusSession(*admittedGlobals, accepted)) {
            return fail("native DOM Data requires a private captured Map table");
        }
        lower.censusShapes(accepted);
        if (admittedGlobals) { lower.censusOwnedGlobals(*admittedGlobals, accepted); }
        lower.censusIdentityFields(accepted);
        if (hostContract && hostContract->provider == HostContract::Provider::closedSourceSession &&
            (!admittedGlobals || !lower.censusSession(*admittedGlobals, accepted))) {
            return fail("native session requires a completely admitted captured method table");
        }
        lower.censusEnvironments(accepted);
        lower.censusMethodTables(accepted);

        for (ctjs::FuncOp fn : accepted) { lower.lower(fn); }
        dropRefinements(module);
        if (!accepted.empty()) { lower.declareGlobals(); }
        lower.finish();
    }
};

} // namespace
} // namespace ctcompile::ctnative
