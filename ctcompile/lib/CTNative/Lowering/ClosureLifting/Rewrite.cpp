//===- Rewrite.cpp - Closure lift driver and rewrite ---------- -*- C++ -*-===//
//
// closureLifter::run() classifies every ctjs.create_closure, lifts the
// admissible ones to direct calls with their captures as leading parameters,
// iterates to a fixpoint, and records a refusal reason on the rest. lift()
// performs the rewrite for one target.
//
//===----------------------------------------------------------------------===//
#include "ClassInheritance.h"
#include "ClosureLifter.h"
#include "ctcompile/CTNative/Analysis/Instances.h"

namespace ctcompile::ctnative::lowering_detail {

/// Lifts every admissible closure in the module and returns the counts.
liftReport closureLifter::run() {
    liftReport out;
    // A class that extends another becomes one that extends nothing, before
    // any census reads a class (ClassInheritance.cpp).
    flattenClassInheritance(module);
    // Environment attributes are conclusions of this run's proof. Strip any
    // already present so input IR cannot opt into an owning representation.
    module.walk([](mlir::Operation * op) {
        op->removeAttr(kNativeEnvironment);
        op->removeAttr(kNativeEnvironmentRead);
        op->removeAttr(kNativeStoredCallable);
        op->removeAttr(kNativeStoredCall);
        op->removeAttr(kNativeStoredRead);
        op->removeAttr(kNativeMethodTable);
        op->removeAttr(kNativeTableField);
        op->removeAttr(kNativeArrayCallback);
        op->removeAttr(kNativeBorrowedCallback);
        op->removeAttr(kNativeClosureSet); // ClosureSets.cpp
        op->removeAttr(kNativeBorrowedCall);
        op->removeAttr(kNativeHostCallback);
        op->removeAttr(kNativeHostTimer);
        op->removeAttr(kNativeHostTarget);
        op->removeAttr(kNativeHostCapture);
        op->removeAttr(kNativeHostIdentity);
        op->removeAttr(kNativeHostIdentified);
        op->removeAttr(kNativeHostRemove);
    });
    // Bind local functions directly (specializeCallbacks, bindLocalFunctions)
    // before any census. These rewrites erase boxes, stores, reads and calls,
    // and the censuses below record the uses of every cell, so a verdict taken
    // before them would describe operations that no longer exist.
    // indexAndNewTargets() supplies the two census walks they need.
    indexAndNewTargets();
    captureLexicalThis(); // ArrowThis.cpp: an arrow's `this` becomes a capture.
    // Bindings of functions that run once, which a closure the lift would
    // refuse captures, become static variables first (RunOnce.cpp), so those
    // closures no longer capture them. Boxes holding a function wait for a
    // second round: once the data they capture is static, bindLocalFunctions
    // can often bind them, which turns calls through them into direct calls;
    // a static variable would keep every such call indirect.
    if (!staticsDone) {
        makeRunOnceCellsStatic(out, /*functions=*/false);
        makeRunOnceCellsStatic(out, /*functions=*/true);
    }
    specializeCallbacks(out);
    if (!allowSiblingCaptures && bindLocalFunctions(out, true)) {
        // Erasing a sibling's callable binding is valid only when both sides
        // lift. Try it on a clone and adopt the clone only if every sibling
        // target and capture holder lifted; otherwise fall back to the
        // stricter rule below, so refused ctjs stays executable.
        mlir::OwningOpRef<mlir::ModuleOp> prepared(module.clone());
        closureLifter trial{*prepared, censusOn};
        trial.allowSiblingCaptures = true;
        trial.staticsDone = true; // The static rewrite above is already in the clone.
        liftReport result = trial.run();
        const bool complete =
            llvm::all_of(trial.siblingBindingTargets, [&](mlir::Operation * target) {
                auto closure = trial.uniqueClosureByTarget.lookup(target);
                return closure && trial.lifted.contains(closure.getOperation()) &&
                       llvm::all_of(trial.boundCaptureCalls.lookup(target),
                                    [&](const boundCaptureCall & site) {
                                        return trial.lifted.contains(site.holder.operator->());
                                    });
            });
        if (complete) {
            result.calls += out.calls;
            result.callbackCalls += out.callbackCalls;
            result.callbackParameters += out.callbackParameters;
            module->setAttrs((*prepared)->getAttrs());
            module.getBodyRegion().takeBody(prepared->getBodyRegion());
            trial.module = module;
            trial.dominance.invalidate();
            *this = std::move(trial);
            return result;
        }
    }
    bindLocalFunctions(out);
    // A static variable holding a table of methods is called through
    // directly (Singletons.cpp). After the binding rule, which removes the
    // methods' captures of the functions it binds.
    bindSingletonMethods(out);
    // Static members of a class are called directly (ClassStatics.cpp), so
    // that the class census below can prove the class.
    bindClassStatics(out);
    // Unbox object literals that are proved uncaptured before the
    // argument/receiver census. A separate lifter does this because the
    // rewrite erases stores its census points at; the main census below then
    // indexes the resulting IR afresh.
    {
        closureLifter locals{module};
        locals.census();
        locals.unboxCells(out, true);
    }
    // A registry Map's class: its `dispose` loop over its own property
    // names becomes one store per field (Analysis/Instances.h), before the
    // census below indexes the calls that loop makes.
    rewriteInstanceRegistries(module);
    census();
    // Run the argument census before the method census, because methodCensus
    // reads closedAfterLift, which reads the argument slots. All censuses run
    // before any rewrite: a lifted call is a ctjs.call_direct that
    // closedAfterLift does not recognise, so a decision made after the first
    // lift would differ. constructorCensus indexes `new` sites first; it only
    // routes diagnostics, and makesAnInstance asks the full constructor proof
    // before treating a constructed argument as a future local object.
    constructorCensus();
    argumentCensus();
    methodCensus();
    arrayCallbackCensus();
    stringCallbackCensus();
    hostCallbackCensus();
    promiseCallbackCensus();
    keptCellCensus();
    llvm::DenseMap<mlir::Operation *, std::string> reasonOf; // Closure -> its own reason.
    // The closure that names each function, for the chained reason below: a
    // capture filled from the enclosing closure is refused with that
    // enclosing closure's reason.
    llvm::DenseMap<mlir::Operation *, ctjs::CreateClosureOp> closureOf;
    for (ctjs::CreateClosureOp c : closures) {
        if (ctjs::FuncOp target = targetOf(c)) { closureOf[target.getOperation()] = c; }
    }
    // Classify, lift, repeat until nothing changes. A capture that comes from
    // the closure's own frame can be judged before any rewrite, but a capture
    // filled from the *enclosing* closure is a ctjs.load_upvalue until the
    // enclosing function is lifted, and that function's capture parameter
    // afterwards. So a closure nested one level in is refused in the round
    // its enclosing function lifts and admitted in the next; a chain of N
    // levels settles in N rounds. Every unlifted closure is re-judged each
    // round (cheap in practice: unmeasurable on the 7,725-function Phaser
    // workload), and the loop stops at the first round that lifts nothing.
    // That round's verdicts are the final refusal reasons.
    //
    // Bound: a productive round lifts at least one closure and nothing is
    // ever unlifted, so there are at most as many productive rounds as
    // closures, plus the empty one that ends the loop. Exceeding that means a
    // rule admits a closure that lift() does not lift, which is a named fatal
    // rather than an endless loop.
    for (unsigned round = 0;; ++round) {
        if (round > closures.size()) {
            llvm::report_fatal_error(
                "ctnative lowering: the closure lift ran more rounds than there are closures "
                "- a round that lifts nothing ends the fixpoint and every other round lifts "
                "at least one closure it never unlifts, so a rule is admitting a closure "
                "that lift() then leaves in place");
        }
        // A lift can make a factory or helper private and change its
        // signature, so rebuild the returned-value flows from the current IR
        // each round. A refusal kept from when the function was public would
        // miss a closure returned by a factory that has just become private.
        returnedClosures.clear();
        returnedClosureCensus();
        returnedMethodTableCensus();
        borrowedCallbackCensus();
        closureSetCensus(); // ClosureSets.cpp
        // Per target: the closures that name it, and the first reason any
        // of them could not be lifted. A target's signature changes for the
        // whole program, so one unliftable creation site blocks every other.
        llvm::MapVector<mlir::Operation *, llvm::SmallVector<ctjs::CreateClosureOp>> byTarget;
        llvm::DenseMap<mlir::Operation *, std::string> blocked;
        reasonOf.clear();
        chainedThrough.clear();
        for (ctjs::CreateClosureOp c : closures) {
            // A declaration is a binding, not a value, and already lowers to
            // nothing; admission::isDeclarationClosure handles it.
            if (isDeclarationClosure(c)) { continue; }
            if (lifted.contains(c.getOperation())) { continue; }
            ctjs::FuncOp target = targetOf(c);
            // Each closure kind has its own admission rule, sharing the capture
            // clauses: a returned closure, a constructor (the callee of a
            // ctjs.construct), a method field (stored on an object and reached
            // via get_property), or a plain called closure. One ctjs.func is
            // one C++ signature, so a body used both as a free function and as
            // a constructor needs two and whyNotLiftable refuses that mix ("it
            // is used as a constructor").
            const std::optional<std::string> why =
                returnedClosures.contains(c.getOperation())      ? whyNotReturnedClosure(c)
                : constructorClosures.contains(c.getOperation()) ? whyNotLiftableConstructor(c)
                : methodClosures.contains(c.getOperation())      ? whyNotLiftableMethod(c)
                : arrayCallbacks.contains(c.getOperation())      ? whyNotArrayCallback(c)
                : borrowedCallbacks.contains(c.getOperation())   ? whyNotBorrowedCallback(c)
                : stringCallbacks.contains(c.getOperation())     ? whyNotStringCallback(c)
                : hostCallbacks.contains(c.getOperation())       ? whyNotHostCallback(c)
                : promiseCallbacks.contains(c.getOperation())    ? whyNotPromiseCallback(c)
                : closureSets.contains(c.getOperation())         ? whyNotInClosureSet(c)
                                                                 : whyNotLiftable(c);
            if (why) {
                reasonOf[c.getOperation()] = *why;
                if (target) { blocked.try_emplace(target.getOperation(), *why); }
                continue;
            }
            byTarget[target.getOperation()].push_back(c);
        }
        // Invariant: each ctjs.func is named by exactly one ctjs.create_closure
        // (the bytecode compiler emits one `closure` opcode per function
        // proto). The receiver lift's capture list and method test rely on it,
        // so a violation is a named fatal instead of a bad cast in lift().
        for (auto & [target, made] : byTarget) {
            if (made.size() > 1) {
                llvm::report_fatal_error(
                    llvm::Twine("ctnative lowering: `") +
                    llvm::cast<ctjs::FuncOp>(target).getSymName() +
                    "` is named by more than one ctjs.create_closure - one function proto "
                    "has one `closure` opcode, and the receiver lift's capture list and its "
                    "method test both assume it");
            }
        }
        unsigned liftedThisRound = 0;
        for (auto & [target, made] : byTarget) {
            if (blocked.contains(target)) {
                for (ctjs::CreateClosureOp c : made) {
                    reasonOf[c.getOperation()] =
                        "the function it names is also made somewhere this tier cannot "
                        "lift: " +
                        blocked.lookup(target);
                }
                continue;
            }
            lift(llvm::cast<ctjs::FuncOp>(target), made, out);
            for (ctjs::CreateClosureOp c : made) { lifted.insert(c.getOperation()); }
            ++liftedThisRound;
        }
        if (liftedThisRound == 0) { break; }
    }
    // Write the chained reasons only now that the fixpoint has settled every
    // verdict. A closure refused for a capture the enclosing closure fills is
    // refused because the enclosing function did not lift, so the actionable
    // message names why that did not: the enclosing closure's own reason,
    // which may itself be chained one level further out. The recursion walks
    // outward through strictly enclosing functions, so it ends; the depth
    // guard turns a cycle into a fatal rather than a hang.
    llvm::DenseMap<mlir::Operation *, std::string> settled;
    auto finalReason = [&](auto & self, mlir::Operation * op, unsigned depth) -> std::string {
        if (const auto done = settled.find(op); done != settled.end()) { return done->second; }
        if (depth > closures.size()) {
            llvm::report_fatal_error(
                "ctnative lowering: a chain of `filled from the enclosing closure` reasons is "
                "longer than the module has closures - the enclosing-function walk has cycled");
        }
        std::string why = reasonOf.lookup(op);
        if (const auto through = chainedThrough.find(op); through != chainedThrough.end()) {
            ctjs::FuncOp enclosing = through->second;
            const auto maker = closureOf.find(enclosing.getOperation());
            if (maker == closureOf.end()) {
                why += ": no ctjs.create_closure in this module names `" +
                       enclosing.getSymName().str() + "`";
            } else if (lifted.contains(maker->second.getOperation())) {
                // The fixpoint's invariant: a closure is chained through its
                // enclosing function only while that function is unlifted,
                // and the last round lifted nothing, so the enclosing function
                // must still be unlifted here. If it is lifted, the loop
                // stopped a round early and would refuse, with a stale reason,
                // a closure the next round would have lifted.
                llvm::report_fatal_error(
                    llvm::Twine("ctnative lowering: `") + enclosing.getSymName() +
                    "` was lifted after the closure inside it was last judged - the "
                    "classify-then-lift fixpoint stopped before it settled");
            } else if (isDeclarationClosure(maker->second)) {
                why += ": `" + enclosing.getSymName().str() +
                       "` is bound to a global by a declaration, which is not lifted";
            } else {
                why += ": " + self(self, maker->second.getOperation(), depth + 1);
            }
        }
        settled[op] = why;
        return why;
    };
    // Attach each final reason to its closure so the enclosing function's
    // refusal can name it. Method fields are also marked, because "a closure
    // used as a value" is true of `{f: function(){}}` but points the reader at
    // the wrong admission rule.
    for (const auto & [op, why] : reasonOf) {
        op->setAttr(kNativeClosureReason,
                    mlir::StringAttr::get(context, finalReason(finalReason, op, 0)));
        if (methodClosures.contains(op)) {
            op->setAttr(kNativeMethodRefusal, mlir::UnitAttr::get(context));
        }
    }
    releaseLexicalThisCells(); // ArrowThis.cpp: before unboxCells sees the cells.
    unboxCells(out);
    return out;
}

/// Rewrites `target` to take its captures as leading parameters and turns
/// every call through the closures in `made` (exactly one, enforced by run())
/// into a ctjs.call_direct. Constructors and methods get their own call-site
/// rewrites. With `preserveReceiver`, the real receiver is passed even when
/// the body does not read `this`.
void closureLifter::lift(ctjs::FuncOp target, llvm::ArrayRef<ctjs::CreateClosureOp> made,
                         liftReport & out, bool preserveReceiver) {
    mlir::Block & entry = target.getBody().front();
    const auto valueType = ctjs::ValueType::get(context);
    const unsigned captures = static_cast<unsigned>(target.getUpvalueCount());
    const unsigned parameters = entry.getNumArguments() - ctjs::implicit_arguments;

    // Which parameters are object pointers, read before the capture
    // parameters are inserted and shift them. The census works in JavaScript
    // parameter numbers; the attribute uses entry-block indices, because
    // ctjs.call_direct's operands and every downstream reader count in those.
    // All creation sites must agree (one function is one signature), and
    // `made` holds exactly one closure here, enforced by run().
    llvm::SmallVector<int32_t> objectArgs;
    for (unsigned j = 0; j < parameters; ++j) {
        if (llvm::all_of(made,
                         [&](ctjs::CreateClosureOp c) { return slotCarriesAnObject(c, j); })) {
            objectArgs.push_back(static_cast<int32_t>(captureArgument(captures) + j));
        }
    }
    // An arrow's lexical `this` capture may be an object pointer too (ArrowThis.cpp).
    const llvm::SmallVector<int32_t> lexicalThis = lexicalThisObjectArgs(made);
    objectArgs.insert(objectArgs.begin(), lexicalThis.begin(), lexicalThis.end());

    // Which captures are passed as a pointer to the owning frame's variable
    // rather than by value. Read before the capture parameters are inserted
    // (same reason as objectArgs) and recorded as entry-block indices. `made`
    // holds exactly one closure; `all_of` mirrors the objectArgs loop so the
    // two cannot drift.
    llvm::SmallVector<int32_t> cellArgs;
    for (unsigned i = 0; i < captures; ++i) {
        if (llvm::all_of(made, [&](ctjs::CreateClosureOp c) { return slotIsCarried(c, i); })) {
            cellArgs.push_back(static_cast<int32_t>(captureArgument(i)));
        }
    }
    // Which of those receive a shared cell's handle rather than a pointer
    // (rule 2b in ClosureLifter.h).
    llvm::SmallVector<int32_t> sharedArgs;
    for (unsigned i = 0; i < captures; ++i) {
        if (llvm::all_of(made, [&](ctjs::CreateClosureOp c) { return slotIsShared(c, i); })) {
            sharedArgs.push_back(static_cast<int32_t>(captureArgument(i)));
        }
    }
    const auto sharedSlots = mlir::Builder(context).getDenseI32ArrayAttr(sharedArgs);
    const auto cellSlots = mlir::Builder(context).getDenseI32ArrayAttr(cellArgs);
    const auto objectSlots = mlir::Builder(context).getDenseI32ArrayAttr(objectArgs);
    const auto carriedSlot = [&](unsigned k) {
        return llvm::is_contained(cellArgs, static_cast<int32_t>(captureArgument(k)));
    };
    // Copies the slot lists onto a rewritten call, matching its callee's.
    const auto tagSlots = [&](mlir::Operation * site) {
        if (!cellArgs.empty()) { site->setAttr(kNativeCellArgs, cellSlots); }
        if (!sharedArgs.empty()) { site->setAttr(kNativeSharedCellArgs, sharedSlots); }
        if (!objectArgs.empty()) { site->setAttr(kNativeObjectArgs, objectSlots); }
    };

    // Captures become leading parameters, inserted after the three implicit
    // arguments (captureArgument(k) == 3 + k), so ctjs.call_direct's operand
    // order still matches the entry block's argument order.
    for (unsigned i = 0; i < captures; ++i) {
        entry.insertArgument(captureArgument(i), valueType, target.getLoc());
    }
    llvm::SmallVector<mlir::Type> inputs(entry.getNumArguments(), valueType);
    target.setFunctionTypeAttr(
        mlir::TypeAttr::get(mlir::FunctionType::get(context, inputs, {valueType})));
    // Set the attribute before the rewrite: a closure nested in this body is
    // judged in a later round, and slotIsCarried() reads this function's
    // cell-argument list to decide whether the nested closure's slot is also
    // a pointer.
    if (!cellArgs.empty()) {
        target->setAttr(kNativeCellArgs, cellSlots);
        out.carried += static_cast<unsigned>(cellArgs.size());
    }
    if (!sharedArgs.empty()) { target->setAttr(kNativeSharedCellArgs, sharedSlots); }

    llvm::SmallVector<ctjs::LoadUpvalueOp> reads;
    target.getBody().walk([&](ctjs::LoadUpvalueOp read) {
        if (!read->hasAttr(kNativeEnvironmentRead)) { reads.push_back(read); }
    });
    for (ctjs::LoadUpvalueOp read : reads) {
        const auto k = static_cast<unsigned>(read.getIndex());
        const mlir::Value slot = entry.getArgument(captureArgument(k));
        if (carriedSlot(k)) {
            // A read through the pointer. The parameter holds the address of
            // the owning frame's variable, so the value is one indirection
            // away: ctjs.cell_get, which the emitter prints as `*p`. The
            // interpreter reads the box when the closure runs (run_loop.cpp,
            // get_upvalue: `reg = cell->slot`); this reads it when the call
            // runs, which is the same moment.
            mlir::OpBuilder at(read);
            auto through = ctjs::CellGetOp::create(at, read.getLoc(), valueType, slot);
            read.getResult().replaceAllUsesWith(through.getResult());
        } else {
            read.getResult().replaceAllUsesWith(slot);
        }
        read.erase();
    }
    // Writes to a capture become ctjs.cell_set through the pointer.
    // whyUpvalueReadsDoNotLift already refused any target that stores to a
    // slot not carried by pointer, so reaching one here means the rule and
    // this rewrite have drifted: fatal.
    llvm::SmallVector<ctjs::StoreUpvalueOp> writes;
    target.getBody().walk([&](ctjs::StoreUpvalueOp write) { writes.push_back(write); });
    for (ctjs::StoreUpvalueOp write : writes) {
        const auto k = static_cast<unsigned>(write.getIndex());
        if (!carriedSlot(k)) {
            llvm::report_fatal_error(
                llvm::Twine("ctnative lowering: `") + target.getSymName() + "` writes capture " +
                llvm::Twine(k) +
                ", which the lift did not carry by pointer - whyUpvalueReadsDoNotLift "
                "admitted a store the rewrite cannot place");
        }
        mlir::OpBuilder at(write);
        ctjs::CellSetOp::create(at, write.getLoc(), entry.getArgument(captureArgument(k)),
                                write.getValue());
        write.erase();
    }
    // No upvalues are left: the function now reads its bindings from its own
    // parameters.
    target->setAttr("upvalue_count", mlir::Builder(context).getI32IntegerAttr(0));
    target->setAttr(kNativeCaptures,
                    mlir::Builder(context).getI32IntegerAttr(static_cast<int>(captures)));
    if (!objectArgs.empty()) {
        target->setAttr(kNativeObjectArgs, objectSlots);
        // Calls made by bindLocalFunctions have no closure-value use for the
        // ordinary rewrite below to visit. Their targets have no captures.
        if (captures == 0) {
            module.walk([&](ctjs::CallDirectOp call) {
                if (call.getTarget() == target) { call->setAttr(kNativeObjectArgs, objectSlots); }
            });
        }
        out.objects += static_cast<unsigned>(objectArgs.size());
    }
    liftSelfCalls(target, captures, parameters);
    // Making the function private is required, not cosmetic. MLIR's
    // DeadCodeAnalysis gives a public symbol unknown predecessors, so
    // TypeInference falls back to setToEntryState and every parameter,
    // captures included, reads `!ctnative.boxed` however many call sites the
    // module holds; every lifted function would then be refused with
    // "capture 0 is !ctnative.boxed - no caller proves it". ResolveGlobals
    // does the same under the same condition, which the checks above have
    // established: every caller of this function is visible.
    mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Private);
    ++out.functions;

    if (arrayCallbacks.contains(made.front())) {
        rewriteArrayCallback(made.front(), target, out);
        return;
    }
    if (borrowedCallbacks.contains(made.front())) {
        rewriteBorrowedCallback(made.front(), target, out);
        return;
    }
    if (stringCallbacks.contains(made.front())) {
        return rewriteStringCallback(made.front(), target, out);
    }
    if (hostCallbacks.contains(made.front())) {
        rewriteHostCallback(made.front(), target, out);
        return;
    }
    if (promiseCallbacks.contains(made.front())) {
        rewritePromiseCallback(made.front(), target, out);
        return;
    }
    if (closureSets.contains(made.front())) { // ClosureSets.cpp
        rewriteClosureSetMember(made.front(), target, out);
        return;
    }

    if (returnedClosures.contains(made.front())) {
        liftReturnedClosure(made.front(), target, captures, parameters, out, preserveReceiver);
        return;
    }

    // A method carries its receiver only when `%arg0` is read: a method that
    // never touches `this` gets undefined instead of the object, as the
    // importer passes undefined for a non-arrow's `$enclosing_this`. An
    // emitted parameter nothing reads fails -Wunused-parameter under -Werror,
    // and passing the object would be a use that opens its shape for no gain.
    const bool constructor = llvm::all_of(made, [&](ctjs::CreateClosureOp c) {
        return constructorClosures.contains(c.getOperation());
    });
    const bool method = !constructor && callsOfTarget.count(target.getOperation()) != 0 &&
                        llvm::all_of(made, [&](ctjs::CreateClosureOp c) {
                            return methodClosures.contains(c.getOperation());
                        });
    // A constructor uses the same test: the instance is the receiver, and a
    // constructor that never touches `this` builds the empty shape, so
    // passing it would open the shape for no gain.
    const bool carriesReceiver =
        (method || constructor) && (preserveReceiver || !entry.getArgument(0).use_empty());
    if (carriesReceiver) {
        target->setAttr(kNativeReceiver, mlir::UnitAttr::get(context));
        ++out.receivers;
    }

    if (constructor) {
        // `new X(a, b)` becomes an object literal plus the direct call the
        // receiver lift already emits.
        //
        // The instance is an empty ctjs.create_object placed where the `new`
        // was, and the constructor is entered through a ctjs.call_direct
        // carrying it as operand 0 with `ctnative.receiver` on the call.
        // Afterwards no constructor is left in the IR, only a closed object
        // literal and a free function that writes through a pointer to it, so
        // hasClosedShape, groupReceivers, fieldsOf, censusShapes and replace
        // need no constructor case. The instance lowers to a frame-scope
        // `ctn_X` variable like any other literal: no allocation, and the
        // object lives in the caller's frame.
        //
        // Host preparation (preserveReceiver) passes the source callable so
        // the rewritten graph can be proved independently; the ordinary lift
        // passes undefined. Native emission drops this unused implicit
        // argument either way.
        for (ctjs::CreateClosureOp c : made) { markLifted(c, out, captures); }
        ctjs::CreateClosureOp only = made.front();
        auto prototype = immutablePrototype(only);
        const auto captured = liftedCaptures(only);
        for (ctjs::ConstructOp built : constructsOfTarget[target.getOperation()]) {
            mlir::OpBuilder at(built);
            const mlir::Value undefined = undefinedAt(at, built.getLoc());
            // The instance starts as an empty literal: the constructor writes
            // every field through `this`, and fieldsOf collects those over the
            // alias group the receiver mark creates. Non-method fields of an
            // immutable prototype are copied onto it.
            auto instance = ctjs::CreateObjectOp::create(at, built.getLoc(), valueType);
            // An instance of a JavaScript class is spelled after the class.
            if (jsClasses.count(only.getOperation())) {
                instance->setAttr(kNativeClassName,
                                  mlir::StringAttr::get(context, classNameOf(target)));
            }
            if (prototype) {
                for (ctjs::SetPropertyOp field : prototype->fields) {
                    // Methods become direct calls with no runtime field. Their
                    // original stores may already carry a method-lift marker.
                    if (field.getValue().getDefiningOp<ctjs::CreateClosureOp>()) { continue; }
                    ctjs::SetPropertyOp::create(at, built.getLoc(), instance.getResult(),
                                                field.getKey(), field.getValue());
                }
            }
            auto direct = directCall(at, built.getLoc(), target,
                                     carriesReceiver ? instance.getResult() : undefined,
                                     preserveReceiver ? built.getCallee() : undefined, undefined,
                                     captured, built.getArgs(), captures + parameters);
            if (carriesReceiver) { direct->setAttr(kNativeReceiver, mlir::UnitAttr::get(context)); }
            // Mark which arguments are addresses on the call as well as the
            // callee, like the receiver mark: replace() reads it once per
            // operand, and a symbol lookup there would cost one lookup per
            // argument per call.
            if (!cellArgs.empty()) { direct->setAttr(kNativeCellArgs, cellSlots); }
            if (!sharedArgs.empty()) { direct->setAttr(kNativeSharedCellArgs, sharedSlots); }
            // `new` evaluates to the instance, not to the call's result. The
            // interpreter's context::construct ends with
            // `produced.is_object_like() ? produced : self`, and
            // whyConstructorReturnsAnObject proved `produced` is never
            // object-like, so the answer is always `self`.
            built.getResult().replaceAllUsesWith(instance.getResult());
            built.erase();
            ++out.calls;
            ++out.constructors;
        }
        // A class's setup goes first: its edges name the prototype and the
        // constructor, and the class helper call names the constructor.
        if (auto cls = jsClasses.find(only.getOperation()); cls != jsClasses.end()) {
            eraseClassSetup(cls->second);
        }
        if (prototype) {
            auto literal = prototype->attachment.getValue().getDefiningOp<ctjs::CreateObjectOp>();
            prototype->attachment.erase();
            for (ctjs::SetPropertyOp field : prototype->fields) { field.erase(); }
            llvm::erase(objects, literal);
            literal.erase();
        }
        return;
    }

    if (method) {
        // The field lowers to nothing, and so does the closure in it: the
        // method is a free function, not a member. The decision is recorded
        // as attributes because admission meets the store and the closure
        // long before it meets the call.
        for (ctjs::CreateClosureOp c : made) {
            for (mlir::Operation * user : c.getResult().getUsers()) {
                user->setAttr(kNativeMethod, mlir::UnitAttr::get(context));
            }
            markLifted(c, out, captures);
            ++out.methods;
        }
        const auto captured = liftedCaptures(made.front());
        for (methodCall at : callsOfTarget[target.getOperation()]) {
            mlir::OpBuilder builder(at.call);
            // Constructor lifting may already have replaced and erased the
            // receiver the census recorded; the live call operand is current.
            at.receiver = at.call.getReceiver();
            const mlir::Value undefined = undefinedAt(builder, at.call.getLoc());
            // The receiver is operand 0: ctjs.call_direct's operands match
            // the callee's entry block in order, so the object lands in
            // `%arg0` with no reordering.
            //
            // The callee value is undefined, unlike in the closure lift. Here
            // it would be `at.load`, a property read of the object, and
            // keeping it would leave a `!ctnative.boxed` result in an accepted
            // function with no consumer: the native call arm drops operand 2,
            // and this rewrite runs inside --ctnative-lower-to-emitc so the
            // boxed lowering never sees the op. The load is erased below.
            auto direct = directCall(builder, at.call.getLoc(), target,
                                     carriesReceiver ? at.receiver : undefined, undefined,
                                     undefined, captured, at.call.getArgs(), captures + parameters);
            if (carriesReceiver) { direct->setAttr(kNativeReceiver, mlir::UnitAttr::get(context)); }
            // Mark address arguments on the call too, as in the constructor
            // branch above.
            if (!cellArgs.empty()) { direct->setAttr(kNativeCellArgs, cellSlots); }
            if (!sharedArgs.empty()) { direct->setAttr(kNativeSharedCellArgs, sharedSlots); }
            at.call.getResult().replaceAllUsesWith(direct.getResult());
            at.call.erase();
            // Erase the method load too. Leaving it would keep a use of the
            // object that hasClosedShape reads as open, refusing the literal
            // this rewrite just admitted.
            //
            // Erase its key constant as well if nothing else uses it. A string
            // constant with no users is no longer recognised as a property key
            // (isKeyOnlyString needs a use), so admission would refuse the
            // whole function over "a constant that is not a number, a boolean
            // or undefined". The importer may or may not share the constant
            // with the store that binds the field, so check rather than assume.
            mlir::Value key = at.load.getKey();
            at.load.erase();
            if (mlir::Operation * made = key.getDefiningOp();
                made != nullptr && made->use_empty()) {
                made->erase();
            }
            ++out.calls;
        }
        return;
    }

    if (captures != 0) {
        const auto found = boundCaptureCalls.find(target.getOperation());
        if (found != boundCaptureCalls.end()) {
            for (boundCaptureCall & site : found->second) {
                llvm::SmallVector<mlir::Value> arguments;
                for (unsigned i = 0; i < captures; ++i) {
                    const mlir::Value value = siblingCapturedValue(made.front(), i, site.holder);
                    if (!value) {
                        llvm::report_fatal_error("ctnative lowering: a proved sibling capture no "
                                                 "longer reaches its call");
                    }
                    arguments.push_back(value);
                }
                arguments.append(site.call.getArgs().begin(), site.call.getArgs().end());
                site.call.getArgsMutable().assign(arguments);
                tagSlots(site.call);
            }
        }
    }

    for (ctjs::CreateClosureOp c : made) {
        // Pass the captured value, not the cell: either a constant cell's
        // initial value, or the enclosing function's capture parameter as-is
        // (it already holds the value). The slot index selects between them,
        // because a slot the enclosing closure fills carries only a
        // placeholder operand.
        const auto captured = liftedCaptures(c);
        // Handle both call-site shapes, because --ctjs-resolve-globals may
        // already have made this one direct. whyNotLiftable admits a ctjs.call
        // at operand 0 and a ctjs.call_direct at operand 2; they differ only in
        // where the receiver and arguments are read from. The captures must be
        // prepended either way, which is why an already-direct call is
        // rewritten too.
        llvm::SmallVector<mlir::Operation *> calls(c.getResult().getUsers().begin(),
                                                   c.getResult().getUsers().end());
        for (mlir::Operation * user : calls) {
            auto call = llvm::dyn_cast<ctjs::CallOp>(user);
            auto named = llvm::dyn_cast<ctjs::CallDirectOp>(user);
            // whyNotLiftable's condition 4 (every use of the closure value is a
            // ctjs.call at operand 0 or a ctjs.call_direct at operand 2)
            // guarantees one of `call`/`named` is non-null. That condition is
            // what keeps a pointer to this frame from escaping, so a violation
            // is a named fatal rather than a null dereference.
            if (!call && !named) {
                llvm::report_fatal_error(
                    llvm::Twine("ctnative lowering: `") + target.getSymName() +
                    "` is lifted but its closure reaches `" + user->getName().getStringRef() +
                    "`, which is not a call - whyNotLiftable's condition 4 admitted a use it "
                    "should have refused, and for a carried binding that use is what would "
                    "take the address of this frame out of it");
            }
            mlir::OpBuilder at(user);
            const mlir::Value undefined = undefinedAt(at, user->getLoc());
            // Same padding rule as the resolver: op::call fills a missing
            // parameter with undefined, so a short call becomes a full one.
            auto direct = directCall(at, user->getLoc(), target,
                                     call ? call.getReceiver() : named.getReceiver(), c.getResult(),
                                     undefined, captured, call ? call.getArgs() : named.getArgs(),
                                     captures + parameters);
            // Tag the call as well as the callee, like the receiver mark:
            // TypeInference::hasClosedShape is asked once per property access
            // per solver visit, and a symbol lookup there would be quadratic
            // in the module.
            tagSlots(direct);
            user->getResult(0).replaceAllUsesWith(direct.getResult());
            user->erase();
            ++out.calls;
        }
        markLifted(c, out, captures);
    }
}

} // namespace ctcompile::ctnative::lowering_detail
