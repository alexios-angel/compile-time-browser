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
#include "mlir/Pass/PassManager.h"
#include "llvm/Support/MemoryBuffer.h"

#include <optional>

namespace ctcompile::ctnative {

#define GEN_PASS_DEF_CTNATIVELOWERTOEMITC
#include "ctcompile/CTNative/Transforms/Passes.h.inc"

namespace {
using namespace lowering_detail;

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
        if (hostContract) {
            const OwnedGlobalRoots original(module, *hostContract, hostMaxSteps);
            if (original.proved() && original.roots().size() == 1) {
                // Prepare only the checked table and environment, speculatively.
                // A stale input never reaches this rewrite. Its internally
                // derived contract is usable only if the complete live owner
                // and callable queries succeed again on the transformed IR.
                mlir::OwningOpRef<mlir::ModuleOp> prepared(
                    llvm::cast<mlir::ModuleOp>(module->clone()));
                const OwnedGlobalRoots source(*prepared, *hostContract, hostMaxSteps);
                closureLifter preparation{*prepared};
                std::optional<liftReport> result;
                if (original.roots().front().methodTable) {
                    result = preparation.prepareOwnedGlobalMethodTables(source, *hostContract,
                                                                        hostMaxSteps);
                } else {
                    // Scalar owners need no closure rewrite, but old native
                    // facts must not erase their live stores either.
                    preparation.discardNativeSourceFacts();
                    result = liftReport{};
                }
                if (result) {
                    HostContract transformed = *hostContract;
                    transformed.moduleSha256 = hostContractFingerprint(*prepared);
                    const OwnedGlobalRoots checked(*prepared, transformed, hostMaxSteps);
                    if (checked.proved()) {
                        // Map identity may pass the proved ordinary-root reads,
                        // never arbitrary host reads. Its annotations change
                        // the prepared fingerprint, so final ownership is
                        // independently checked once more before publication.
                        if (checked.roots().front().methodTable &&
                            checked.roots().front().methodTable->capturedMap) {
                            prepareNativeMaps(*prepared, &checked);
                            transformed.moduleSha256 = hostContractFingerprint(*prepared);
                        }
                        const OwnedGlobalRoots final(*prepared, transformed, hostMaxSteps);
                        if (final.proved()) {
                            module.getBodyRegion().takeBody(prepared->getBodyRegion());
                            hostContract = std::move(transformed);
                            lifted = *result;
                        }
                    }
                }
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
                fn->removeAttr("ctjs.not_structured");
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
            hostContract ? std::make_unique<OwnedGlobalRoots>(module, *hostContract, hostMaxSteps)
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
        solver.load<TypeInference>(ownedGlobals.get());
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
            hostContract ? std::make_unique<OwnedGlobalRoots>(module, *hostContract, hostMaxSteps)
                         : nullptr;
        if (admittedGlobals) {
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
            admission check{solver, {}, &groups, &ownedTableSlots, admittedGlobals.get()};
            if (check.function(fn)) {
                accepted.push_back(fn);
            } else {
                fn->setAttr("ctnative.not_native", mlir::StringAttr::get(&getContext(), check.why));
            }
        }

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

        lowering lower{solver, &getContext(), module};
        lower.groups = &groups;
        if (admittedGlobals && admittedGlobals->proved()) {
            lower.explicitObservations = true;
            for (const auto & name : hostContract->observations) {
                lower.observations.insert(name);
            }
        }
        for (ctjs::FuncOp fn : accepted) {
            lower.names[fn.getSymName()] =
                fn.getSymName().starts_with("_script_$") ? "main" : cIdentifier(fn.getSymName());
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
                if (admittedGlobals && admittedGlobals->lookup(o)) { return; }
                llvm::StringRef name;
                if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(o)) {
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
            const auto entry = llvm::find_if(
                accepted, [](ctjs::FuncOp fn) { return fn.getSymName().starts_with("_script_$"); });
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
            }
        }
        lower.censusScalars(accepted);
        lower.censusShapes(accepted);
        if (admittedGlobals) { lower.censusOwnedGlobals(*admittedGlobals, accepted); }
        lower.censusIdentityFields(accepted);
        lower.censusEnvironments(accepted);
        lower.censusMethodTables(accepted);

        for (ctjs::FuncOp fn : accepted) { lower.lower(fn); }
        if (!accepted.empty()) { lower.declareGlobals(); }
        lower.finish();
    }
};

} // namespace
} // namespace ctcompile::ctnative
