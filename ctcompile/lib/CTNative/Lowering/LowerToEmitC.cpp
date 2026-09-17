// THE FIRST NATIVE ARTEFACT - part 24 Phase 62½-C.
//
// Everything before this file is an analysis. This is the lowering: a
// `ctjs.func` whose every value has a PROVED type the native tier can carry
// becomes an `emitc.func` over native C++ carriers, and a function that does
// not is refused with a diagnostic naming the first value or operation that
// failed. Part 24 §1.2: the output links neither the interpreter nor its
// collector, and there is no boxed fallback - a refusal is a reason on the
// function, and the compilation-unit gate says whether the program is native.
//
// HOW A VALUE IS REPRESENTED, and where the representation is exact:
//
//   bool               bool     exactly
//   num<i32|i64|f64>   double   exactly - every JavaScript number is one
//   str<utf8>         std::string  owning bytes, including NUL and WTF-8
//   opt<num<...>>      double   with undefined as NaN. EXACT in arithmetic
//   opt<bottom>                 (undefined + 1 is NaN), relational comparison
//                               (undefined < 1 is false, as NaN < 1 is), and
//                               truthiness (both are falsy). NOT exact for
//                               equality with undefined/null, `typeof`, or
//                               printing - so every use in which the
//                               difference is observable is refused.
//
// The two `opt` rows exist because of the closed-world global rule: a global
// is undefined until its first store runs and nothing orders a load after
// one, so a numeric global is `opt<num>` (TypeInference.h). Numbers stay
// `double` even when proved `i32`: an int32_t representation is a Phase 63
// measurement, not a Phase 62½ obligation, and `double` is always correct.
//
// NOT A DIALECT CONVERSION, deliberately. After --ctjs-lift-to-scf the body is
// structured, and after the inference every value's type is known; retyping
// each value in place from the lattice and replacing each ctjs operation with
// its EmitC form leaves the whole of `scf` untouched for upstream's
// --convert-scf-to-emitc, which already handles `scf.if`, `scf.for` and
// `scf.while`. A TypeConverter converts by TYPE, and every JavaScript value
// has the same type; the lattice is per VALUE.
#include "../Analysis/OwnedGlobalRoots.h"
#include "../Analysis/OwnedMethodTableSlots.h"
#include "Admission/Admission.h"
#include "ClosureLifting/ClosureLifter.h"
#include "EmitC/Emitter.h"
#include "Exceptions/Recovery.h"
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

// Called only on the private clone after complete ownership validation. A
// declared absent binding also requires source typeof-lookup provenance; source
// writes never reach this path. Charge before mutation, then reprove the clone.
bool materializeHostPrimitives(mlir::ModuleOp module, const HostContract & contract,
                               const OwnedGlobalRoots & owners, unsigned remaining) {
    const auto spend = [&] {
        if (!remaining) { return false; }
        --remaining;
        return true;
    };
    llvm::SmallVector<ctjs::LoadGlobalOp> reads;
    llvm::SmallVector<ctjs::UnaryOp> objectTypes;
    const auto walked = module.walk<mlir::WalkOrder::PreOrder>(
        [&](mlir::Operation * operation) -> mlir::WalkResult {
            if (!spend()) { return mlir::WalkResult::interrupt(); }
            if (auto query = llvm::dyn_cast<ctjs::UnaryOp>(operation);
                query && query.getKind() == ctjs::UnaryKind::TypeOf) {
                auto * origin = query.getOperand().getDefiningOp();
                // Root fields can hold other types. Only the actual fresh
                // owner and its checked global loads are ordinary objects.
                if (llvm::isa_and_nonnull<ctjs::CreateObjectOp, ctjs::LoadGlobalOp>(origin) &&
                    owners.lookup(origin)) {
                    for ([[maybe_unused]] mlir::OpOperand & use : query.getResult().getUses()) {
                        if (!spend()) { return mlir::WalkResult::interrupt(); }
                    }
                    if (!spend()) { return mlir::WalkResult::interrupt(); }
                    objectTypes.push_back(query);
                }
            }
            auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation);
            if (!load) { return mlir::WalkResult::advance(); }
            for (const auto * names : {&contract.undefinedBindings, &contract.absentBindings}) {
                if (names == &contract.absentBindings && !load.getTypeofLookup()) { continue; }
                for (const std::string & name : *names) {
                    if (!spend()) { return mlir::WalkResult::interrupt(); }
                    if (load.getName() != name) { continue; }
                    for ([[maybe_unused]] mlir::OpOperand & use : load.getResult().getUses()) {
                        if (!spend()) { return mlir::WalkResult::interrupt(); }
                    }
                    if (!spend()) { return mlir::WalkResult::interrupt(); }
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

// THE PRIVATE-CLONE PROTOCOL every host preparation follows, once. The
// module is never mutated speculatively: `prepare` works on a clone under a
// copy of the contract whose fingerprint it refreshes whenever it reproves a
// stage of its own, the prepared clone is fingerprinted and reproved by
// `Analysis`, and only a complete proof publishes the clone's attributes and
// body into the module and the refreshed contract into `hostContract`.
// `prepare` may swap the clone for a further clone of it. The return is the
// refusal - `prefix` heads an analysis refusal so each caller's diagnostic
// reads as it always has - and empty means the module now holds the prepared
// body.
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

struct CTNativeLowerToEmitCPass : impl::CTNativeLowerToEmitCBase<CTNativeLowerToEmitCPass> {
    using CTNativeLowerToEmitCBase::CTNativeLowerToEmitCBase;

    void runOnOperation() override {
        mlir::ModuleOp module = getOperation();
        std::optional<HostContract> hostContract;
        if (!hostManifest.empty()) {
            auto buffer = llvm::MemoryBuffer::getFile(hostManifest);
            if (!buffer) {
                module.emitError()
                    << "cannot read native host contract: " << buffer.getError().message();
                return signalPassFailure();
            }
            auto parsed = parseHostContract((*buffer)->getBuffer());
            if (!parsed) {
                module.emitError() << llvm::toString(parsed.takeError());
                return signalPassFailure();
            }
            hostContract = std::move(*parsed);
        }
        const auto normalizeOwnedSource = [](mlir::ModuleOp candidate) {
            closureLifter local{candidate};
            local.discardNativeSourceFacts();
            local.census();
            liftReport locals;
            local.unboxCells(locals);
            candidate.walk([&](ctjs::CreateClosureOp closure) {
                if (local.whyTargetIsNotLiftable(closure) ||
                    isUndefinedConstant(closure.getEnclosingThis())) {
                    return;
                }
                mlir::OpBuilder at(closure);
                closure.getEnclosingThisMutable().assign(ctjs::ConstantOp::create(
                    at, closure.getLoc(), ctjs::ValueType::get(candidate.getContext()),
                    ctjs::UndefinedAttr::get(candidate.getContext())));
            });
            llvm::SmallVector<ctjs::CreateCellOp> dead;
            candidate.walk([&](ctjs::CreateCellOp cell) {
                if (cell->hasAttr("ctnative.unboxed") && cell.getResult().use_empty()) {
                    dead.push_back(cell);
                }
            });
            for (ctjs::CreateCellOp cell : dead) { cell.erase(); }
            return locals;
        };
        const bool domData = hostContract && hostContract->provider ==
                                                 HostContract::Provider::ctbrowserDOMDataSession;
        if (domData) {
            if (hostContract->moduleSha256 != hostContractFingerprint(module)) {
                module.emitError(
                    "native DOM Data source: host contract module fingerprint mismatch");
                return signalPassFailure();
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
            if (!refusal.empty()) {
                module.emitError() << refusal;
                return signalPassFailure();
            }
        }

        std::unique_ptr<DOMEntryAnalysis> domEntry;
        if (hostContract &&
            (hostContract->provider == HostContract::Provider::ctbrowserDOM ||
             hostContract->provider == HostContract::Provider::ctbrowserDOMSession)) {
            if (hostContract->moduleSha256 != hostContractFingerprint(module) ||
                module->hasAttr("ctjs.skipped")) {
                module.emitError("native DOM entry: fingerprint mismatch or incomplete source");
                return signalPassFailure();
            }
            const std::string refusal = withProvedClone<DOMEntryAnalysis>(
                module, hostContract, hostMaxSteps, "native DOM entry preparation: ",
                [&](mlir::OwningOpRef<mlir::ModuleOp> & composed,
                    HostContract & transformed) -> std::string {
                    // A handler in the entry is normalized in place. A handler
                    // owned by a local helper is normalized first, under the
                    // same fingerprinted proof, so helper expansion then
                    // inlines one structured invoke.
                    llvm::SmallVector<std::string> handlers;
                    for (auto function : composed->getOps<ctjs::FuncOp>()) {
                        bool hasHandler = false;
                        function.walk([&](ctjs::PushHandlerOp) { hasHandler = true; });
                        if (hasHandler) { handlers.push_back(function.getSymName().str()); }
                    }
                    const bool entryHandler = llvm::is_contained(handlers, hostContract->entry);
                    llvm::Error sourceError = llvm::Error::success();
                    for (const std::string & handler : handlers) {
                        if (sourceError) { break; }
                        sourceError =
                            normalizeDOMURI(*composed, transformed, hostMaxSteps, handler);
                        transformed.moduleSha256 = hostContractFingerprint(*composed);
                    }
                    if (!sourceError && !entryHandler) {
                        sourceError =
                            expandDOMHelpers(*composed, hostContract->entry, hostMaxSteps);
                    }
                    if (sourceError) {
                        return "native DOM source: " + llvm::toString(std::move(sourceError));
                    }
                    transformed.moduleSha256 = hostContractFingerprint(*composed);
                    // Helper expansion resolves proved local cells and maps
                    // helper formals back to the validated entry parameters.
                    if (auto error =
                            normalizeDOMElementGuards(*composed, transformed, hostMaxSteps)) {
                        return "native DOM element guard: " + llvm::toString(std::move(error));
                    }
                    transformed.moduleSha256 = hostContractFingerprint(*composed);
                    if (auto error = normalizeDOMIteration(*composed, transformed, hostMaxSteps)) {
                        return "native DOM iteration: " + llvm::toString(std::move(error));
                    }
                    transformed.moduleSha256 = hostContractFingerprint(*composed);
                    // An explicit library entry may replace only its proved
                    // inert declaration wrapper. Prepare privately, discard
                    // supplied native reports, and reprove before publishing
                    // the exported function. `source` borrows `composed`, so
                    // the swap waits until it is gone.
                    mlir::OwningOpRef<mlir::ModuleOp> prepared;
                    {
                        const DOMEntryAnalysis source(*composed, transformed, hostMaxSteps);
                        if (!source.proved()) {
                            return ("native DOM entry: " + source.reason()).str();
                        }
                        mlir::IRMapping mapping;
                        prepared = llvm::cast<mlir::ModuleOp>((*composed)->clone(mapping));
                        if (auto wrapper = source.wrapper()) {
                            prepared->lookupSymbol<ctjs::FuncOp>(wrapper.getSymName()).erase();
                        }
                        // Make proved callback bodies reachable to sparse dataflow;
                        // they still emit as ordinary internal C++ functions.
                        for (ctjs::FuncOp callback : source.callbacks()) {
                            prepared->lookupSymbol<ctjs::FuncOp>(callback.getSymName()).setPublic();
                        }
                        // Only the initialized DOM provider and complete source
                        // proof authorize this binding. Normalize in the private
                        // clone, then reprove it; no VM lookup or C++ global
                        // survives emission.
                        prepared->walk([](ctjs::LoadGlobalOp load) {
                            if (load.getName() != "undefined") { return; }
                            mlir::OpBuilder at(load);
                            auto constant = ctjs::ConstantOp::create(
                                at, load.getLoc(), ctjs::UndefinedAttr::get(load.getContext()));
                            load.getResult().replaceAllUsesWith(constant.getResult());
                            load.erase();
                        });
                        // Normalize optional force while the original method
                        // proof is available. Token lists omit undefined;
                        // Element coerces it to false. The emitter then needs
                        // only ordinary Boolean arguments.
                        source.entry().walk([&](ctjs::CallOp original) {
                            const auto * edge = source.call(original);
                            if (!edge || (edge->kind != HostDOMMethod::toggleClass &&
                                          edge->kind != HostDOMMethod::toggleAttribute)) {
                                return;
                            }
                            auto call =
                                llvm::cast<ctjs::CallOp>(mapping.lookup(original.getOperation()));
                            if (call.getArgs().size() != 2) { return; }
                            auto force = call.getArgs()[1].getDefiningOp<ctjs::ConstantOp>();
                            if (!force || !llvm::isa<ctjs::UndefinedAttr>(force.getValue())) {
                                return;
                            }
                            if (edge->kind == HostDOMMethod::toggleClass) {
                                call.getArgsMutable().erase(1);
                            } else {
                                mlir::OpBuilder at(call);
                                auto value = ctjs::ConstantOp::create(
                                    at, call.getLoc(),
                                    ctjs::BooleanAttr::get(call.getContext(), false));
                                call.getArgsMutable().slice(1, 1).assign(value.getResult());
                            }
                        });
                    }
                    prepared->walk(
                        [](mlir::Operation * op) { removeAttrsWithPrefix(op, "ctnative."); });
                    prepared->lookupSymbol<ctjs::FuncOp>(hostContract->entry).setPublic();
                    composed = std::move(prepared);
                    return {};
                });
            if (!refusal.empty()) {
                module.emitError() << refusal;
                return signalPassFailure();
            }
            domEntry = std::make_unique<DOMEntryAnalysis>(module, *hostContract, hostMaxSteps);
        }

        // Keep the default policy on the native entry, so clients do not need
        // to reconstruct it in a shell/test pipeline. Run before closure
        // lifting: reachability still sees CTJS's complete reference forms,
        // and an unreachable private body never reaches type admission.
        // The pass manager owns analysis invalidation and instrumentation.
        // Host preparation validates the fingerprinted input separately below;
        // general optimization cannot silently rebind a driver manifest.
        if (!hostContract && optimize && (precompute || pruneUnreachable)) {
            mlir::OpPassManager defaults(mlir::ModuleOp::getOperationName());
            if (precompute) {
                CTNativePrecomputeOptions options;
                options.maxSteps = precomputeMaxSteps;
                options.report = optimizationReport;
                defaults.addPass(createCTNativePrecompute(options));
            }
            if (pruneUnreachable) {
                CTNativePruneUnreachableOptions options;
                options.maxSteps = reachabilityMaxSteps;
                options.report = optimizationReport;
                defaults.addPass(createCTNativePruneUnreachable(options));
            }
            if (failed(runPipeline(defaults, module))) { return signalPassFailure(); }
        }

        // PHASE 59 SLICE 1, AND IT RUNS BEFORE THE SOLVE. Lifting turns a
        // closure call into a ctjs.call_direct, which is what makes the target
        // reachable to DeadCodeAnalysis at all - an uncalled private function
        // is dead, and every type in it reads `<unvisited>` - and what lets
        // TypeInference carry each capture's proved type into the leading
        // parameter it became. Doing it after the solve would need a second
        // one.
        liftReport lifted;
        if (hostContract && !domEntry && hostMaxSteps &&
            hostContract->moduleSha256 == hostContractFingerprint(module)) {
            // Local cell/unused receiver normalization needs no host assumptions.
            // It is speculative: only a complete fresh owner proof can publish it.
            // Nothing here is a diagnostic - a refusal leaves the module as it was.
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
                            // Scalar owners need no closure rewrite, but old
                            // native facts must not erase their live stores
                            // either.
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
        // Recovery is speculative until type/effect admission and the entire
        // closed call component pass. Refused functions keep their original
        // status edges for boxed lowering; a diagnostic is never a proof.
        llvm::SmallVector<std::pair<ctjs::FuncOp, mlir::OwningOpRef<ctjs::FuncOp>>, 0>
            exceptionOriginals;
        module.walk([&](ctjs::FuncOp fn) {
            if (hostContract) { return; }
            fn->removeAttr("ctnative.exception_refusal");
            bool handlers = false;
            fn.walk([&](ctjs::PushHandlerOp) { handlers = true; });
            if (!handlers) { return; }
            auto recovery = recoverPrimitiveExceptionRegion(fn, exceptionMaxSteps);
            if (recovery.recovered) {
                exceptionOriginals.emplace_back(fn, std::move(recovery.original));
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
            // ONE LINE, DETERMINISTIC. StringMap iterates in hash order, so it
            // is sorted by count and then by name - a census whose text moves
            // between runs cannot be pinned by a lit and cannot be diffed
            // between two corpora runs either.
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
            for (const auto & [label, count] : ordered(lifter.censusUses)) {
                into << " " << label << "=" << count;
            }
            into << "; sole blocker:";
            for (const auto & [label, count] : ordered(lifter.censusSole)) {
                into << " " << label << "=" << count;
            }
            into << "; sole blocker by method field:";
            for (const auto & [label, count] : ordered(lifter.censusSoleFields)) {
                into << " " << label << "=" << count;
            }
            into << "; root of the nesting:";
            for (const auto & [label, count] : ordered(lifter.censusRoot)) {
                into << " " << label << "=" << count;
                const auto at = lifter.censusExample.find(label);
                if (at != lifter.censusExample.end()) { into << "@" << at->second; }
            }
            into << "; nesting depth:";
            for (const auto & [label, count] : ordered(lifter.censusDepth)) {
                into << " " << label << "=" << count;
            }
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
                                << " call-only function parameter(s) erased";
        }

        // THE ALIAS GROUPS, once the lift has written its attributes and before
        // anything reads a field. TypeInference::groupReceivers is the one walk
        // that says which values name one object; admission and the shape
        // census both read it, so a field a method touches is the same field
        // the literal has in both.
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

        // All three, and none optional - TypeInference.h says why.
        mlir::DataFlowSolver solver;
        solver.load<mlir::dataflow::DeadCodeAnalysis>();
        solver.load<mlir::dataflow::SparseConstantPropagation>();
        solver.load<TypeInference>(ownedGlobals.get(), domEntry.get());
        if (failed(solver.initializeAndRun(module))) {
            module.emitError("the type inference did not converge");
            return signalPassFailure();
        }

        llvm::SmallVector<ctjs::FuncOp> functions;
        module.walk([&](ctjs::FuncOp fn) { functions.push_back(fn); });

        llvm::SmallVector<ctjs::FuncOp> accepted;
        // Rebuild at the final consumer. The solver did not mutate source IR,
        // and no diagnostic annotation may stand in for this live query.
        auto admittedGlobals =
            hostContract && !domEntry
                ? std::make_unique<OwnedGlobalRoots>(module, *hostContract, hostMaxSteps)
                : nullptr;
        auto admittedDOM =
            domEntry ? std::make_unique<DOMEntryAnalysis>(module, *hostContract, hostMaxSteps)
                     : nullptr;
        if (admittedDOM && !admittedDOM->proved()) {
            module.emitError() << "native DOM entry admission: " << admittedDOM->reason();
            return signalPassFailure();
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
                fn->setAttr("ctnative.not_native",
                            mlir::StringAttr::get(&getContext(), "unstructured control flow"));
                continue;
            }
            admission check{solver,           {}, &groups, &ownedTableSlots, admittedGlobals.get(),
                            admittedDOM.get()};
            if (check.function(fn)) {
                accepted.push_back(fn);
            } else {
                fn->setAttr("ctnative.not_native", mlir::StringAttr::get(&getContext(), check.why));
            }
        }

        // Identity members are shared by property name across allocation sites.
        // Check every candidate store before removing any call component; neither
        // a final narrowed read nor a separately admitted function may choose an
        // incompatible carrier for the same emitted member.
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
            fn->setAttr("ctnative.not_native", mlir::StringAttr::get(&getContext(), reason));
            return true;
        });

        // THE FIXPOINT: a function is native only if every function it calls
        // directly is. Drop any accepted function that calls a refused one,
        // name the callee in its diagnostic, and repeat until nothing moves -
        // a refusal anywhere in a call chain reaches every caller.
        llvm::DenseSet<mlir::Operation *> nativeSet;
        for (ctjs::FuncOp fn : accepted) { nativeSet.insert(fn.getOperation()); }
        mlir::SymbolTable symbols(module);
        // CLOSED IN BOTH DIRECTIONS. A native caller needs a native callee to
        // emit an emitc.call to; and a native CALLEE needs every caller native
        // too, because a refused caller keeps a ctjs.call_direct that must
        // name a ctjs.func with a body - which a lowered function no longer
        // is. So a refusal propagates along the call graph both ways, each
        // step naming the function that caused it, until nothing moves.
        for (bool changed = true; changed;) {
            changed = false;
            for (ctjs::FuncOp fn : functions) {
                const bool native = nativeSet.contains(fn.getOperation());
                // Retained code is an edge too. A callable builder names its
                // target even when the factory itself never invokes it.
                fn.getBody().walk([&](ctjs::CreateClosureOp made) {
                    if (environmentTarget(made).empty()) { return; }
                    mlir::Operation * target = symbols.lookup(environmentTarget(made));
                    const bool targetNative = target && nativeSet.contains(target);
                    if (native && !targetNative) {
                        nativeSet.erase(fn);
                        fn->setAttr("ctnative.not_native",
                                    mlir::StringAttr::get(
                                        &getContext(), "retains `" + environmentTarget(made).str() +
                                                           "`, which is not native"));
                        changed = true;
                    } else if (!native && targetNative) {
                        nativeSet.erase(target);
                        target->setAttr("ctnative.not_native",
                                        mlir::StringAttr::get(
                                            &getContext(), "retained by `" + fn.getSymName().str() +
                                                               "`, which is not native"));
                        changed = true;
                    }
                });
                fn.getBody().walk([&](ctjs::CallDirectOp call) {
                    mlir::Operation * callee = symbols.lookup(call.getCallee());
                    const bool calleeNative = callee != nullptr && nativeSet.contains(callee);
                    if (native && !calleeNative) {
                        nativeSet.erase(fn.getOperation());
                        fn->setAttr(
                            "ctnative.not_native",
                            mlir::StringAttr::get(
                                &getContext(),
                                ("calls `" + call.getCallee() + "`, which is not native").str()));
                        changed = true;
                    } else if (!native && calleeNative) {
                        nativeSet.erase(callee);
                        callee->setAttr(
                            "ctnative.not_native",
                            mlir::StringAttr::get(&getContext(), ("called by `" + fn.getSymName() +
                                                                  "`, which is not native")
                                                                     .str()));
                        changed = true;
                    }
                });
            }
        }
        llvm::erase_if(accepted,
                       [&](ctjs::FuncOp fn) { return !nativeSet.contains(fn.getOperation()); });
        if (admittedDOM && (!llvm::is_contained(accepted, admittedDOM->entry()) ||
                            llvm::any_of(admittedDOM->callbacks(), [&](ctjs::FuncOp callback) {
                                return !llvm::is_contained(accepted, callback);
                            }))) {
            module.emitError("native DOM entry did not pass complete native admission");
            return signalPassFailure();
        }

        lowering lower{solver, &getContext(), module};
        lower.groups = &groups;
        if (domData) {
            if (!admittedGlobals || !admittedGlobals->proved() ||
                accepted.size() != functions.size()) {
                module.emitError("native DOM Data requires complete family admission");
                return signalPassFailure();
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
            lower.names[fn.getSymName()] = !admittedDOM && !domData && isScriptEntry(fn)
                                               ? "main"
                                               : cIdentifier(fn.getSymName());
        }

        // THE GLOBAL CENSUS, over the whole accepted set and BEFORE any
        // function is lowered.
        //
        // Two things depend on it. First, `main` prints the globals from this
        // set, and it used to be filled lazily as each global was first
        // touched - so a global written and read only inside a helper was
        // declared and never printed, because main is the importer's function
        // 0 and is lowered first. The differential then failed by naming the
        // missing line rather than the ordering, which is a bug report
        // pointing at the wrong file.
        //
        // Second, a global with no store anywhere in the unit is `undefined`,
        // and this tier carries undefined as NaN - exact for arithmetic and
        // comparison, NOT for printing, where the interpreter reports "not a
        // Number" and the binary would print `nan`. That difference is
        // observable, so it is refused rather than represented, which is the
        // rule this file is built on.
        // A FUNCTION'S OWN NAME IS A GLOBAL TOO, and it is not one of these.
        // `function f(){}` at the top level is a store of a closure into the
        // global "f", and every call is a load of it; both lower to nothing,
        // because the closed world turned the call into a direct one. Counting
        // them would declare `static double g_accumulate;` and print
        // `accumulate=0` beside the numbers - which is exactly what happened
        // the first time this census ran.
        const auto bindsAFunction = [](ctjs::StoreGlobalOp store) {
            return llvm::isa_and_nonnull<ctjs::CreateClosureOp>(store.getValue().getDefiningOp());
        };
        const auto callsOnly = [](ctjs::LoadGlobalOp load) {
            return !load.getResult().use_empty() &&
                   llvm::all_of(load.getResult().getUsers(), [](mlir::Operation * user) {
                       return llvm::isa<ctjs::CallDirectOp>(user);
                   });
        };
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
                    if (admittedDOM && admittedDOM->isInitialIntrinsic(load)) { return; }
                    if (callsOnly(load) || isNativeMapBookkeeping(load)) { return; }
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
                entry->getOperation()->setAttr(
                    "ctnative.not_native",
                    mlir::StringAttr::get(
                        &getContext(),
                        ("global `" + neverStored.front() +
                         "` is read but never stored, so it is undefined - which this tier "
                         "carries as NaN, and printing that as a number is not what the "
                         "interpreter answers")
                            .str()));
                accepted.erase(entry);
            }
        }

        // AND THE SHAPE CENSUS, for the same reason and in the same place -
        // after the last function has left the accepted set and before the
        // first is lowered. Phase 56C keys a class on the SHAPE and not on the
        // creation site, and whether a shape's definition is a template is a
        // property of every site in the program at once, so no site's type can
        // be spelled until all of them have been seen.
        for (auto & [fn, original] : exceptionOriginals) {
            if (llvm::is_contained(accepted, fn)) { continue; }
            fn.getBody().takeBody(original->getBody());
            if (auto marker = (*original)->getAttr("ctjs.not_structured")) {
                fn->setAttr("ctjs.not_structured", marker);
            } else {
                fn->removeAttr("ctjs.not_structured");
            }
        }
        if (admittedDOM && llvm::is_contained(accepted, admittedDOM->entry())) {
            lower.censusDOM(*admittedDOM,
                            hostContract->provider == HostContract::Provider::ctbrowserDOMSession);
        }
        lower.censusScalars(accepted, admittedGlobals.get());
        if (domData && !lower.censusSession(*admittedGlobals, accepted)) {
            module.emitError("native DOM Data requires a private captured Map table");
            return signalPassFailure();
        }
        lower.censusShapes(accepted);
        if (admittedGlobals) { lower.censusOwnedGlobals(*admittedGlobals, accepted); }
        lower.censusIdentityFields(accepted);
        if (hostContract && hostContract->provider == HostContract::Provider::closedSourceSession &&
            (!admittedGlobals || !lower.censusSession(*admittedGlobals, accepted))) {
            module.emitError("native session requires a completely admitted captured method table");
            return signalPassFailure();
        }
        lower.censusEnvironments(accepted);
        lower.censusMethodTables(accepted);

        for (ctjs::FuncOp fn : accepted) { lower.lower(fn); }
        if (!accepted.empty()) { lower.declareGlobals(); }
        lower.finish();
    }
};

} // namespace
} // namespace ctcompile::ctnative
