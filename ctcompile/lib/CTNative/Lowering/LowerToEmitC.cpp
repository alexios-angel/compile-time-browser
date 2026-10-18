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
#include "../Reachability/Analysis.h"
#include "Admission/Admission.h"
#include "ClosureLifting/ClassInheritance.h"
#include "ClosureLifting/ClosureLifter.h"
#include "EmitC/Emitter.h"
#include "EmitC/Vectors.h"
#include "Exceptions/Recovery.h"
#include "Exceptions/SuppressedCloses.h"
#include "Exceptions/TypeErrors.h"
#include "ForOf.h"
#include "Refinements.h"
#include "ctcompile/CTJS/Lowering/NativeRealm.h"
#include "ctcompile/CTJS/Transforms/Passes.h"
#include "ctcompile/CTNative/Analysis/StaticTables.h"
#include "ctcompile/CTNative/Analysis/WorkBudget.h"
#include "ctcompile/CTNative/Transforms/Passes.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/MemoryBuffer.h"

#include <optional>

namespace ctcompile::ctnative {

#define GEN_PASS_DEF_CTNATIVELOWERTOEMITC
#include "ctcompile/CTNative/Transforms/Passes.h.inc"

namespace {
using namespace lowering_detail;

/// Folds to undefined each read of a global object property that
/// --ctjs-resolve-globals marked kAbsentGlobalProperty: a name no script
/// writes and the native realm does not provide, left for the VM only because
/// something could write the globals table dynamically. Native code cannot:
/// every such write is refused where it sits, and a program with a refused
/// function has no native build. A key that something else in the module
/// spells (a write by that name through another value, or a call that passes
/// it) keeps its reads. The folded names go on the module as
/// ctnative.absent_globals. As in --ctjs-resolve-globals' own fold, the page
/// is taken to have no element of that id or name, which named access on the
/// Window object (HTML 7.3.3) would return.
void foldAbsentGlobalReads(mlir::ModuleOp module) {
    llvm::SmallVector<ctjs::GetPropertyOp> marked;
    module.walk([&](ctjs::GetPropertyOp get) {
        if (get->hasAttr(ctjs::kAbsentGlobalProperty)) { marked.push_back(get); }
    });
    if (marked.empty()) { return; }
    llvm::StringSet<> spelled;
    module.walk([&](ctjs::ConstantOp constant) {
        auto text = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue());
        if (!text) { return; }
        for (mlir::OpOperand & use : constant.getResult().getUses()) {
            auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
            if (!llvm::isa<ctjs::RootOp>(use.getOwner()) &&
                (!get || use.getOperandNumber() != 1 ||
                 !get->hasAttr(ctjs::kAbsentGlobalProperty))) {
                spelled.insert(text.getValue());
                return;
            }
        }
    });
    llvm::SetVector<mlir::Attribute> folded;
    for (ctjs::GetPropertyOp get : marked) {
        get->removeAttr(ctjs::kAbsentGlobalProperty);
        const llvm::StringRef key = ctjs::constantKey(get.getKey());
        auto holder = get.getObject().getDefiningOp<ctjs::LoadGlobalOp>();
        if (!holder || key.empty() || spelled.contains(key) || ctjs::nativeRealmProvides(key)) {
            continue;
        }
        mlir::OpBuilder at(get);
        auto undefined = ctjs::ConstantOp::create(at, get.getLoc(), get.getType(),
                                                  ctjs::UndefinedAttr::get(module.getContext()));
        get.getResult().replaceAllUsesWith(undefined.getResult());
        folded.insert(mlir::StringAttr::get(module.getContext(), key));
        mlir::Operation * keyed = get.getKey().getDefiningOp();
        get.erase();
        if (keyed && keyed->use_empty()) { keyed->erase(); }
        if (holder->use_empty()) { holder->erase(); }
    }
    if (!folded.empty()) {
        module->setAttr("ctnative.absent_globals",
                        mlir::ArrayAttr::get(module.getContext(), folded.getArrayRef()));
    }
}

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

    /// Erases the refused functions that nothing able to run can create or
    /// call, once admission has settled and before any function is lowered.
    /// A module under a host contract keeps every function, as the default
    /// optimizations do.
    /// The default pruning before closure lifting keeps every function the
    /// importer left public, and a closure's target is public until the lift
    /// makes it private; so a closure made only in a branch precomputation
    /// removed (a jQuery plugin when the page has no jQuery) leaves a refused
    /// function behind that no code reaches. This walk starts only from the
    /// script, the functions the embedder calls by name and what a module
    /// attribute names (reachability::roots::entries), and it never removes
    /// an admitted function. A module with no top-level script also keeps
    /// every public function, since it is not a whole script and something
    /// outside it may call them. Removal is never silent: each function gets a
    /// row in `ctnative.unreachable_refusals`, which tools/check counts as
    /// removed, not as native. One walk finds every unreachable function, so
    /// nothing a removal orphans is left for a second round.
    void removeUnreachableRefusals(llvm::SmallVectorImpl<ctjs::FuncOp> & functions,
                                   bool underHostContract) {
        mlir::ModuleOp module = getOperation();
        module->removeAttr("ctnative.unreachable_refusals");
        module->removeAttr("ctnative.unreachable_reason");
        if (underHostContract || !optimize || !pruneUnreachable) { return; }
        const auto proof =
            reachability::analyze(module, reachabilityMaxSteps, reachability::roots::entries);
        mlir::Builder at(&getContext());
        if (!proof.reason.empty()) {
            module->setAttr("ctnative.unreachable_reason", at.getStringAttr(proof.reason));
            return;
        }
        llvm::SmallVector<mlir::Attribute> rows;
        for (ctjs::FuncOp fn : proof.dead) {
            const auto refusal = fn->getAttrOfType<mlir::StringAttr>("ctnative.not_native");
            if (!refusal) { continue; }
            rows.push_back(at.getDictionaryAttr(
                {at.getNamedAttr("function", at.getStringAttr(fn.getSymName())),
                 at.getNamedAttr("reason",
                                 at.getStringAttr(llvm::Twine("removed: unreachable (no function "
                                                              "that can run creates or calls it; "
                                                              "it was refused: ") +
                                                  refusal.getValue() + ")"))}));
            llvm::erase(functions, fn);
            fn.erase();
        }
        if (!rows.empty()) {
            module->setAttr("ctnative.unreachable_refusals", at.getArrayAttr(rows));
        }
        if (optimizationReport) {
            module.emitRemark() << "reachability: " << rows.size()
                                << " refused function(s) removed as unreachable after admission";
        }
    }

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

        // Before the default optimizations, so that precomputation drops the
        // branches the folded reads guard, and before the closure lift, so that
        // the helpers the class statics rewrite copies have lost them too.
        if (!hostContract) { foldAbsentGlobalReads(module); }

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
        // Before the lift: an object parameter must be read only through
        // constant keys, which `config[key]` in a loop over a table parameter
        // becomes only once the loop is written out per key.
        // A handler that only drops an iterator close's errors before a throw
        // becomes an invocation of the close first; it is not a catch that
        // could see the throw (Exceptions/SuppressedCloses.h). A `throw new
        // TypeError(message)` the lift left beside the function's returns is
        // put in a saved-throw region, and the functions are lifted again
        // (Exceptions/TypeErrors.h).
        const bool closes = !hostContract && structureSuppressedCloses(module);
        if (!hostContract && (structureTypeErrorThrows(module) || closes)) {
            mlir::OpPassManager relift(mlir::ModuleOp::getOperationName());
            relift.addPass(ctjs::createCTJSLiftToSCF());
            if (failed(runPipeline(relift, module))) { return signalPassFailure(); }
        }
        if (!hostContract) { unrollParameterTableLoops(module); }
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
            cells.census(/*cellFactsOnly=*/true);
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
        // Data records returned by value join their callers' groups.
        TypeInference::markRecordReturns(module);
        // After the table loops are unrolled, so a pattern read from a
        // constant table is a constant (`new RegExp(expected).test(type)`).
        markStaticTables(module);
        TypeInference::markStringMethodCalls(module);
        // After markStaticTables, whose unrolled loops over a constant table
        // read `config[key]` under each key as a constant: a literal read only
        // that way is a record, not a table.
        if (!hostContract) { prepareNativeDictionaries(module); }
        // A function called with values of different exact types gets one copy
        // per type, so no parameter needs a String union. Before the alias
        // groups and the solve, so both see the copies as ordinary functions.
        // A function whose original body is kept for a refusal is not copied.
        // The copies' type tests have constant answers now; the guard folding
        // keeps only the branches they select, before admission reads them.
        if (!hostContract) {
            const bool copied = copyPerArgumentType(module, [&](ctjs::FuncOp fn) {
                const auto keeps = [&](const auto & originals) {
                    return llvm::any_of(originals,
                                        [&](const auto & row) { return row.first == fn; });
                };
                return keeps(recoveryOriginals) || keeps(forOfOriginals);
            });
            if (copied && optimize && precompute) {
                mlir::OpPassManager guards(mlir::ModuleOp::getOperationName());
                guards.addPass(createCTNativePrecompute(
                    {.maxSteps = precomputeMaxSteps, .report = optimizationReport}));
                if (failed(runPipeline(guards, module))) { return signalPassFailure(); }
            }
        }
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
        // The fingerprint is checked against the module as it arrived (the
        // first analysis, above); the lowering's markers have annotated the
        // module since, and every other DOM check reruns here on it as it is.
        auto admittedDOM =
            domEntry ? std::make_unique<DOMEntryAnalysis>(module, *hostContract, hostMaxSteps,
                                                          domEntry->fingerprint())
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
        // ways, each step naming the function that caused it, except back to
        // a static member that a refused caller can call through a closure.
        // The global census below can refuse the script entry after this has
        // run, so it runs again then.
        //
        // A refused caller of a static member that the class statics rewrite
        // turned into a direct call (kNativeStaticMemberCall), when the member
        // is native, takes no parameters and reads neither new.target nor its
        // own closure (a static getter), calls it through a closure instead, as
        // it did through the class before that rewrite. A closure names its
        // function by index, not by symbol, so the member stays native for its
        // native callers, and the call means the same. Any other direct call
        // keeps the rule above: its callee is refused with its caller.
        const auto callThroughClosure = [&](ctjs::FuncOp fn, ctjs::CallDirectOp call) {
            if (!call->hasAttr(kNativeStaticMemberCall)) { return false; }
            auto callee = llvm::dyn_cast_or_null<ctjs::FuncOp>(symbols.lookup(call.getCallee()));
            mlir::Block * entry =
                callee && !callee.getBody().empty() ? &callee.getBody().front() : nullptr;
            const std::optional<unsigned> index =
                entry ? ctjs::functionIndex(callee) : std::nullopt;
            if (!index || !nativeSet.contains(callee.getOperation()) ||
                entry->getNumArguments() != ctjs::implicit_arguments ||
                !entry->getArgument(ctjs::arg_new_target).use_empty() ||
                !entry->getArgument(ctjs::arg_callee).use_empty()) {
                return false;
            }
            mlir::OpBuilder at(call);
            const mlir::Type type = call.getResult().getType();
            auto undefined = ctjs::ConstantOp::create(at, call.getLoc(), type,
                                                      ctjs::UndefinedAttr::get(&getContext()));
            auto closure = ctjs::CreateClosureOp::create(
                at, call.getLoc(), type, fn.getBody().front().getArgument(ctjs::arg_callee),
                undefined, at.getI32IntegerAttr(static_cast<std::int32_t>(*index)),
                mlir::ValueRange{}, mlir::DenseI32ArrayAttr{});
            auto through = ctjs::CallOp::create(at, call.getLoc(), type, closure,
                                                call.getReceiver(), call.getArgs());
            call.getResult().replaceAllUsesWith(through.getResult());
            call.erase();
            return true;
        };
        // A refused function that no function able to run creates or calls is
        // removed after admission (removeUnreachableRefusals), so its calls
        // never run and do not refuse their callees. The per-call-site copies
        // leave such originals (ClosureLifting/CallSiteCopies.cpp), and one
        // that shares a callee with its copies would otherwise refuse it.
        // Asked once, before the fixpoint, under the same conditions as the
        // removal: the fixpoint only refuses, which removes roots, so a
        // function dead now is dead then too.
        llvm::DenseSet<mlir::Operation *> removedLater;
        if (!hostContract && optimize && pruneUnreachable) {
            const auto proof =
                reachability::analyze(module, reachabilityMaxSteps, reachability::roots::entries);
            if (proof.reason.empty()) {
                for (ctjs::FuncOp fn : proof.dead) {
                    if (fn->hasAttr("ctnative.not_native")) {
                        removedLater.insert(fn.getOperation());
                    }
                }
            }
        }
        const auto closeNativeSet = [&] {
            for (bool changed = true; changed;) {
                changed = false;
                for (ctjs::FuncOp fn : functions) {
                    if (removedLater.contains(fn.getOperation())) { continue; }
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
                        if (!native && callThroughClosure(fn, call)) { return; }
                        edge(symbols.lookup(call.getCallee()),
                             "calls `" + call.getCallee() + "`, which is not native",
                             "called by `" + fn.getSymName() + "`, which is not native");
                    });
                    // A specialised Array-method call or replace call names its
                    // callback, and an addEventListener call its listener.
                    fn.getBody().walk([&](ctjs::CallOp call) {
                        auto callback =
                            call->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeArrayCallback);
                        if (!callback) {
                            callback =
                                call->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeStringCallback);
                        }
                        if (!callback) {
                            callback =
                                call->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeHostCallback);
                        }
                        if (!callback) { return; }
                        edge(symbols.lookup(callback.getValue()),
                             "calls back `" + callback.getValue() + "`, which is not native",
                             "called back by `" + fn.getSymName() + "`, which is not native");
                    });
                    // A promise operation names its executor or handler.
                    fn.getBody().walk([&](mlir::Operation * op) {
                        auto callback =
                            op->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativePromiseCallback);
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
        lower.pageViews = !hostContract;
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
        // A program with string entries is embedded: its host asks the
        // entries, and its top level runs inside the host's own output, as a
        // script's top level prints nothing in the VM. So it prints no
        // globals.
        if (!stringEntries.empty()) { lower.explicitObservations = true; }
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
                        if (nativeDictionaryAction(load) == "plumbing") { return; }
                        // The RegExp literal factory behind split(/\s+/) and a
                        // specialised replace lowers to nothing; it is not a
                        // program global.
                        if (isVectorCallPlumbing(load) ||
                            TypeInference::consumedByStringCallback(load)) {
                            return;
                        }
                        // Nor is the TypeError a `throw new TypeError(m)`
                        // spells itself (Exceptions/TypeErrors.h).
                        if (consumedByTypeErrorThrow(load)) { return; }
                        // Nor are `document` and `window` when only their
                        // page reads use them; those lower to runtime calls.
                        if (!hostContract && isPageGlobal(load)) { return; }
                        // Nor is a declared function's value handed to the
                        // listener table: it becomes the declaration's
                        // identity (EmitC/HostCallbacks.cpp).
                        if (load->hasAttr(kNativeHostDeclared)) { return; }
                        // Nor is the global `undefined`, which is a constant.
                        if (!hostContract && TypeInference::isUndefinedGlobal(load)) { return; }
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
        // functions get back the bodies speculative recovery replaced. The
        // static tables are read first: a refused run-once function's original
        // body predates markStaticTables, so its literals carry no mark.
        lower.censusStaticTables(accepted);
        for (auto & [fn, original] : recoveryOriginals) {
            if (llvm::is_contained(accepted, fn)) { continue; }
            fn.getBody().takeBody(original->getBody());
            setOrRemoveAttr(fn, "ctjs.not_structured", (*original)->getAttr("ctjs.not_structured"));
        }
        // After recovery's, which may hold the folded body.
        for (auto & [fn, original] : forOfOriginals) {
            if (!llvm::is_contained(accepted, fn)) { fn.getBody().takeBody(original->getBody()); }
        }
        // A body put back still calls a native callee by symbol where the body
        // it replaced was given a call through a closure.
        for (ctjs::FuncOp fn : functions) {
            if (nativeSet.contains(fn.getOperation())) { continue; }
            fn.getBody().walk([&](ctjs::CallDirectOp call) { callThroughClosure(fn, call); });
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

        removeUnreachableRefusals(functions, hostContract.has_value());
        // Which element-array parameters borrow by address, read while every
        // function is still ctjs.
        markWrittenArrays(accepted);
        for (ctjs::FuncOp fn : accepted) { lower.lower(fn); }
        dropRefinements(module);
        if (!accepted.empty()) { lower.declareGlobals(); }
        lower.finish();
    }
};

} // namespace
} // namespace ctcompile::ctnative
