// ClosureLifting/Rewrite.cpp - native lowering implementation.
#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {

liftReport closureLifter::run() {
    liftReport out;
    // Environment annotations are conclusions of this invocation's proof,
    // never a way for input IR to opt into an owning representation.
    module.walk([](mlir::Operation * op) {
        op->removeAttr(kNativeEnvironment);
        op->removeAttr(kNativeEnvironmentRead);
    });
    // PHASE 59 SLICE 2 STEP 4, BEFORE EVERYTHING. It erases boxes, stores,
    // reads and calls, and the three censuses below record the uses of
    // every cell - so a verdict taken before this rewrite would be about
    // operations it has since removed. It needs two of census()'s own
    // walks, which is why they are a function of their own now.
    indexAndNewTargets();
    specializeCallbacks(out);
    bindLocalFunctions(out);
    census();
    // THE ARGUMENT SLOTS FIRST, because `closedAfterLift` reads them and
    // `methodCensus` reads `closedAfterLift`. Both censuses run before any
    // rewrite: a lifted call is a `ctjs.call_direct`, which `closedAfterLift`
    // does not know, so a decision made after the first lift would differ
    // from the same decision made before it.
    // THE `new` SITES FIRST, and the ordering is load-bearing. This census
    // is purely STRUCTURAL - which closures are used as `new` callees and
    // nowhere else - and asks no question about any shape, so it is safe
    // this early. It has to be this early: `closedAfterLift` admits a
    // `ctjs.construct` result as an object, and `argumentCensus`'s fixpoint
    // asks `closedAfterLift` of every argument. Run after, the instance
    // would still be a `ctjs.construct` there, read as "not a literal", and
    // an instance passed to a lifted function would refuse the module.
    constructorCensus();
    argumentCensus();
    methodCensus();
    llvm::DenseMap<mlir::Operation *, std::string> reasonOf; // closure -> its own reason
    llvm::DenseSet<mlir::Operation *> lifted;                // closures lift() has taken
    // THE CLOSURE THAT NAMES EACH FUNCTION, for the chained reason below:
    // a capture filled from the enclosing closure is refused with THAT
    // closure's reason, and the enclosing function's closure is the one
    // create_closure whose target it is.
    llvm::DenseMap<mlir::Operation *, ctjs::CreateClosureOp> closureOf;
    for (ctjs::CreateClosureOp c : closures) {
        if (ctjs::FuncOp target = targetOf(c)) { closureOf[target.getOperation()] = c; }
    }
    // CLASSIFY, LIFT, REPEAT - PHASE 59 SLICE 1b. One pass was enough for
    // slice 1 because every capture it carries is a cell of the closure's
    // own frame, decided before any rewrite. A capture filled from the
    // enclosing closure is decided BY a rewrite: it is the importer's
    // ctjs.load_upvalue until the enclosing function lifts, and that
    // function's capture parameter afterwards. So a closure nested one
    // level in is refused in the round its enclosing function lifts and
    // admitted in the next, and a chain of N levels settles in N rounds.
    // Every closure still unlifted is judged again each round - a full
    // re-classification, measured on phaser (7,725 functions) to cost
    // nothing a reader would notice - and the loop stops at the first
    // round that lifts nothing. That round's verdicts are the final ones,
    // and they are the reasons written below.
    //
    // THE BOUND IS THE INVARIANT: a productive round lifts at least one
    // closure and nothing is ever unlifted, so there are at most as many
    // productive rounds as closures, plus the empty one that ends it. A
    // round past that is a rule admitting a closure it does not lift, and
    // this file reports that as a named fatal rather than looping.
    for (unsigned round = 0;; ++round) {
        if (round > closures.size()) {
            llvm::report_fatal_error(
                "ctnative lowering: the closure lift ran more rounds than there are closures "
                "- a round that lifts nothing ends the fixpoint and every other round lifts "
                "at least one closure it never unlifts, so a rule is admitting a closure "
                "that lift() then leaves in place");
        }
        // A local lift can close a factory/helper and change its signature.
        // Rebuild returned-value flows from the current IR before deciding
        // the next round; retaining a public-boundary refusal would miss the
        // closure returned by a factory that just became private.
        returnedClosures.clear();
        returnedClosureCensus();
        // Per target: the closures that name it, and the first reason any
        // of them could not be lifted. A target's signature changes for the
        // whole program, so ONE unliftable creation site blocks every other.
        llvm::MapVector<mlir::Operation *, llvm::SmallVector<ctjs::CreateClosureOp>> byTarget;
        llvm::DenseMap<mlir::Operation *, std::string> blocked;
        reasonOf.clear();
        chainedThrough.clear();
        for (ctjs::CreateClosureOp c : closures) {
            // A DECLARATION IS A BINDING, NOT A VALUE, and lowers to
            // nothing already. Leave it to admission::isDeclarationClosure.
            if (admissionIsDeclaration(c)) { continue; }
            if (lifted.contains(c.getOperation())) { continue; }
            ctjs::FuncOp target = targetOf(c);
            // A METHOD FIELD IS A DIFFERENT ADMISSION, NOT A SPECIAL CASE
            // OF THE OTHER ONE. Its closure value is never called - it is
            // STORED, which whyNotLiftable refuses by name - and the calls
            // that reach it come through a `get_property` on the object.
            // So the two rules are asked separately and share their
            // capture clauses.
            // AND A CONSTRUCTOR IS A THIRD ADMISSION. Its closure value is
            // never called and never stored - it is the callee of a
            // `ctjs.construct` - so neither of the other two rules
            // describes it, and `whyNotLiftable` refuses it by name ("it is
            // used as a constructor") for the MIXED case this set
            // deliberately excludes: one ctjs.func is one C++ signature,
            // and a body that is a free function at one site and a
            // constructor at another needs two.
            const std::optional<std::string> why =
                returnedClosures.contains(c.getOperation())      ? whyNotReturnedClosure(c)
                : constructorClosures.contains(c.getOperation()) ? whyNotLiftableConstructor(c)
                : methodClosures.contains(c.getOperation())      ? whyNotLiftableMethod(c)
                                                                 : whyNotLiftable(c);
            if (why) {
                reasonOf[c.getOperation()] = *why;
                if (target) { blocked.try_emplace(target.getOperation(), *why); }
                continue;
            }
            byTarget[target.getOperation()].push_back(c);
        }
        // TWO REFUSALS WERE HERE AND ARE GONE, BECAUSE THEY WERE
        // DECORATION. Both asked what happens when two ctjs.create_closures
        // name ONE ctjs.func - a target that is a method field at one site
        // and a plain closure at another, whose receiver would be an
        // argument in one call and not the other; and a method created
        // twice with captures, whose two capture lists cannot both be one
        // parameter list. Neither is reachable: `compiler_impl` emits
        // exactly one `op::closure` per function proto, so a proto has
        // exactly one creation site, and a probe counting `made.size() > 1`
        // measured ZERO across bootstrap, p5 and phaser (13,053 functions)
        // and all three native fixtures. Removing them changed nothing
        // anywhere, so they are not here.
        //
        // WHAT IS HERE IS THE INVARIANT ITSELF, as a named fatal rather
        // than a refusal, which is this file's idiom for "a rule let one
        // through" (carrierType, memberName, shapeAt, eraseIfUnused). If
        // the reasoning above is ever wrong, lift() would cast a
        // ctjs.set_property to a ctjs.call and crash with no message; this
        // says which claim failed.
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
    // THE CHAINED REASON, written only now that the fixpoint has settled
    // every verdict. A closure refused for a capture the enclosing closure
    // fills is refused BECAUSE the enclosing function did not lift, and the
    // sentence a reader can act on names why THAT did not: its own
    // closure's reason, which may itself be chained one level further out.
    // The recursion walks outward through strictly enclosing functions, so
    // it ends; the guard says so as a fatal, not a hang, if it does not.
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
                // THE FIXPOINT'S OWN INVARIANT. A closure is chained
                // through its enclosing function only while that function
                // is unlifted, and the last round lifted nothing - so the
                // enclosing function of every chained closure is unlifted
                // when this runs. One that IS lifted was lifted AFTER the
                // closure inside it was last judged: the loop stopped one
                // round early and is about to refuse, with a stale reason,
                // a closure the next round would have lifted. Cut the
                // fixpoint back to one pass and this is what fires.
                llvm::report_fatal_error(
                    llvm::Twine("ctnative lowering: `") + enclosing.getSymName() +
                    "` was lifted after the closure inside it was last judged - the "
                    "classify-then-lift fixpoint stopped before it settled");
            } else if (admissionIsDeclaration(maker->second)) {
                why += ": `" + enclosing.getSymName().str() +
                       "` is bound to a global by a declaration, which is not lifted";
            } else {
                why += ": " + self(self, maker->second.getOperation(), depth + 1);
            }
        }
        settled[op] = why;
        return why;
    };
    // The reasons, onto the closures that kept them, so that the function
    // holding one is refused by name. A method field says so, because "a
    // closure used as a value" is a true sentence about `{f: function(){}}`
    // that sends a reader to the wrong slice.
    for (const auto & [op, why] : reasonOf) {
        op->setAttr("ctnative.closure_reason",
                    mlir::StringAttr::get(context, finalReason(finalReason, op, 0)));
        if (methodClosures.contains(op)) {
            op->setAttr("ctnative.method_refusal", mlir::UnitAttr::get(context));
        }
    }
    unboxCells(out);
    return out;
}

// isDeclarationClosure, spelled here because admission is declared below
// and this rewrite runs before it. Kept to one line so the two cannot
// drift into disagreeing about what a declaration is.
bool closureLifter::admissionIsDeclaration(ctjs::CreateClosureOp c) {
    return c.getResult().hasOneUse() &&
           llvm::isa<ctjs::StoreGlobalOp>(*c.getResult().getUsers().begin());
}

void closureLifter::lift(ctjs::FuncOp target, llvm::ArrayRef<ctjs::CreateClosureOp> made,
                         liftReport & out) {
    mlir::Block & entry = target.getBody().front();
    const auto valueType = ctjs::ValueType::get(context);
    const unsigned captures = static_cast<unsigned>(target.getUpvalueCount());
    const unsigned parameters = entry.getNumArguments() - 3;

    // THE OBJECT PARAMETERS, READ BEFORE THE CAPTURES SHIFT THEM. The
    // census decided in terms of JS parameter numbers; the attribute is
    // written in terms of ENTRY-BLOCK indices, because that is what
    // ctjs.call_direct's operands are and what every reader downstream
    // counts in. Every creation site has to agree - one function is one
    // signature - and `made` holds exactly one closure here, which the
    // named fatal in run() enforces.
    llvm::SmallVector<int32_t> objectArgs;
    for (unsigned j = 0; j < parameters; ++j) {
        if (llvm::all_of(made,
                         [&](ctjs::CreateClosureOp c) { return slotCarriesAnObject(c, j); })) {
            objectArgs.push_back(static_cast<int32_t>(captureArgument(captures) + j));
        }
    }

    // PHASE 59 SLICE 2 STEP 2: WHICH CAPTURES ARRIVE AS A POINTER, read
    // before the arguments shift for the same reason `objectArgs` is, and
    // recorded in ENTRY-BLOCK indices because that is what
    // ctjs.call_direct's operands are. `made` holds exactly one closure -
    // the named fatal in run() enforces it - so `all_of` here is one
    // question asked of one creation site, spelled the way the object
    // parameters are so the two cannot drift.
    llvm::SmallVector<int32_t> cellArgs;
    for (unsigned i = 0; i < captures; ++i) {
        if (llvm::all_of(made, [&](ctjs::CreateClosureOp c) { return slotIsCarried(c, i); })) {
            cellArgs.push_back(static_cast<int32_t>(captureArgument(i)));
        }
    }
    const auto carriedSlot = [&](unsigned k) {
        return llvm::is_contained(cellArgs, static_cast<int32_t>(captureArgument(k)));
    };

    // THE CAPTURES BECOME LEADING PARAMETERS, inserted after the three
    // implicit arguments so that ctjs.call_direct's operand order - which
    // IS the entry block's argument order - still lines up, and so that
    // lower()'s existing `for (i = 3; ...)` picks them up with no change.
    for (unsigned i = 0; i < captures; ++i) {
        entry.insertArgument(captureArgument(i), valueType, target.getLoc());
    }
    llvm::SmallVector<mlir::Type> inputs(entry.getNumArguments(), valueType);
    target.setFunctionTypeAttr(
        mlir::TypeAttr::get(mlir::FunctionType::get(context, inputs, {valueType})));
    // THE ATTRIBUTE BEFORE THE REWRITE, because a nested closure inside
    // this body is judged in a LATER round and `slotIsCarried` reads it
    // off this function to decide whether its own slot is a pointer -
    // slice 1b, one indirection out.
    if (!cellArgs.empty()) {
        target->setAttr("ctnative.cell_args",
                        mlir::Builder(context).getDenseI32ArrayAttr(cellArgs));
        out.carried += static_cast<unsigned>(cellArgs.size());
    }

    llvm::SmallVector<ctjs::LoadUpvalueOp> reads;
    target.getBody().walk([&](ctjs::LoadUpvalueOp read) {
        if (!read->hasAttr(kNativeEnvironmentRead)) { reads.push_back(read); }
    });
    for (ctjs::LoadUpvalueOp read : reads) {
        const auto k = static_cast<unsigned>(read.getIndex());
        const mlir::Value slot = entry.getArgument(captureArgument(k));
        if (carriedSlot(k)) {
            // A READ THROUGH THE POINTER. The parameter holds the ADDRESS
            // of the owning frame's variable, so the value is one
            // indirection away - which is exactly what ctjs.cell_get says
            // and what the emitter turns into `*p`. The interpreter reads
            // the box when the closure runs (run_loop.cpp, get_upvalue:
            // `reg = cell->slot`); this reads it when the call runs, which
            // is the same moment.
            mlir::OpBuilder at(read);
            auto through = ctjs::CellGetOp::create(at, read.getLoc(), valueType, slot);
            read.getResult().replaceAllUsesWith(through.getResult());
        } else {
            read.getResult().replaceAllUsesWith(slot);
        }
        read.erase();
    }
    // AND THE WRITES, WHICH ARE THE WHOLE OF THIS STEP.
    // whyUpvalueReadsDoNotLift has already refused any target whose store
    // names a slot this closure does not carry, so every one of these is a
    // pointer - and reaching one that is not means that rule and this
    // rewrite have drifted, which is a fatal and never a number.
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
    // NO UPVALUES LEFT, and the attribute says so: after this the function
    // reads its bindings out of its own frame like any other parameter.
    target->setAttr("upvalue_count", mlir::Builder(context).getI32IntegerAttr(0));
    target->setAttr("ctnative.captures",
                    mlir::Builder(context).getI32IntegerAttr(static_cast<int>(captures)));
    if (!objectArgs.empty()) {
        target->setAttr("ctnative.object_args",
                        mlir::Builder(context).getDenseI32ArrayAttr(objectArgs));
        out.objects += static_cast<unsigned>(objectArgs.size());
    }
    // PRIVATE, AND IT IS NOT COSMETIC. MLIR's DeadCodeAnalysis gives a
    // PUBLIC symbol unknown predecessors, so TypeInference falls back to
    // setToEntryState and every parameter - captures included - reads
    // `!ctnative.boxed` however many call sites the module holds. That was
    // measured here: the whole lift worked and every lifted function was
    // then refused with "capture 0 is !ctnative.boxed - no caller proves
    // it". ResolveGlobals sets the same bit for the same reason, and gates
    // it on the same claim: every caller of this function is visible,
    // which the conditions above have just established.
    mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Private);
    ++out.functions;

    if (returnedClosures.contains(made.front())) {
        liftReturnedClosure(made.front(), target, captures, parameters, out);
        return;
    }

    // A METHOD, AND WHETHER ITS RECEIVER IS A PARAMETER AT ALL. The lift
    // marks `ctnative.receiver` only when `%arg0` is READ: a method that
    // never touches `this` needs no receiver, so the call passes undefined
    // instead of the object, exactly as the importer passes undefined for
    // a non-arrow's `$enclosing_this`. That is not a nicety - an emitted
    // parameter nothing reads is `-Wunused-parameter` under -Werror, and
    // passing the object would be a use of it that opens its shape for no
    // gain.
    const bool constructor = llvm::all_of(made, [&](ctjs::CreateClosureOp c) {
        return constructorClosures.contains(c.getOperation());
    });
    const bool method = !constructor && callsOfTarget.count(target.getOperation()) != 0 &&
                        llvm::all_of(made, [&](ctjs::CreateClosureOp c) {
                            return methodClosures.contains(c.getOperation());
                        });
    // A CONSTRUCTOR ALWAYS CARRIES ITS RECEIVER WHEN IT TOUCHES `this` -
    // the instance IS the receiver - and the test is the same one a method
    // gets: `%arg0` read at all. A constructor that never touches `this`
    // builds the empty shape, and passing it would be a use that opens it
    // for no gain.
    const bool carriesReceiver = (method || constructor) && !entry.getArgument(0).use_empty();
    if (carriesReceiver) {
        target->setAttr("ctnative.receiver", mlir::UnitAttr::get(context));
        ++out.receivers;
    }

    if (constructor) {
        // `new X(a, b)` BECOMES A LITERAL PLUS THE CALL THE RECEIVER LIFT
        // ALREADY EMITS, and that is the whole lowering.
        //
        // The instance is an EMPTY ctjs.create_object placed where the
        // `new` was, and the constructor is entered through a
        // ctjs.call_direct carrying it as operand 0 with
        // `ctnative.receiver` on the call. After this rewrite there is no
        // constructor in the IR at all - only a closed object literal and a
        // free function that writes through a pointer to it - so
        // `hasClosedShape`, `groupReceivers`, `fieldsOf`, `censusShapes`
        // and `replace` need no constructor case, and the instance lowers
        // to a frame-scope `ctn_X` variable like any other literal. Zero
        // allocation, and the object lives in the caller's frame.
        //
        // $callee_value IS UNDEFINED, as it is in the method arm and for
        // the same reason: the native call arm drops operand 2, this
        // rewrite runs inside --ctnative-lower-to-emitc, and the boxed tier
        // never sees the op. The closure is erased below.
        for (ctjs::CreateClosureOp c : made) {
            c->setAttr("ctnative.lifted", mlir::UnitAttr::get(context));
            ++out.closures;
            out.captures += captures;
        }
        llvm::SmallVector<mlir::Value> captured;
        ctjs::CreateClosureOp only = made.front();
        for (unsigned i = 0; i < static_cast<unsigned>(only.getUpvalues().size()); ++i) {
            captured.push_back(liftedCapture(only, i));
        }
        for (ctjs::ConstructOp built : constructsOfTarget[target.getOperation()]) {
            mlir::OpBuilder at(built);
            const mlir::Value undefined = ctjs::ConstantOp::create(
                at, built.getLoc(), valueType, ctjs::UndefinedAttr::get(context));
            // THE INSTANCE. An empty literal: every field it has, the
            // constructor writes through `this`, and `fieldsOf` collects
            // those over the alias group the receiver mark creates.
            auto instance = ctjs::CreateObjectOp::create(at, built.getLoc(), valueType);
            llvm::SmallVector<mlir::Value> arguments(captured);
            arguments.append(built.getArgs().begin(), built.getArgs().end());
            while (arguments.size() < captures + parameters) { arguments.push_back(undefined); }
            auto direct = ctjs::CallDirectOp::create(
                at, built.getLoc(), valueType,
                mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()),
                carriesReceiver ? instance.getResult() : undefined, undefined, undefined, arguments,
                /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
            if (carriesReceiver) {
                direct->setAttr("ctnative.receiver", mlir::UnitAttr::get(context));
            }
            // AND WHICH OF ITS ARGUMENTS IS AN ADDRESS. On the CALL as
            // well as the callee, for the reason the receiver mark is on
            // both: `replace()` reads it once per operand and a symbol
            // lookup there would be a lookup per argument per call.
            if (!cellArgs.empty()) {
                direct->setAttr("ctnative.cell_args",
                                mlir::Builder(context).getDenseI32ArrayAttr(cellArgs));
            }
            // AND `new` EVALUATES TO THE INSTANCE, NOT TO THE CALL. That is
            // the whole of context::construct's last line for a body this
            // rule admits: `produced.is_object_like() ? produced : self`,
            // and whyConstructorReturnsAnObject has just proved `produced`
            // is never object-like, so the answer is always `self`.
            built.getResult().replaceAllUsesWith(instance.getResult());
            built.erase();
            ++out.calls;
            ++out.constructors;
        }
        return;
    }

    if (method) {
        // THE FIELD LOWERS TO NOTHING, and so does the closure in it: the
        // method is a free function, not a member. The reasons are written
        // as attributes because admission meets the store and the closure
        // long before it meets the call.
        for (ctjs::CreateClosureOp c : made) {
            for (mlir::Operation * user : c.getResult().getUsers()) {
                user->setAttr("ctnative.method", mlir::UnitAttr::get(context));
            }
            c->setAttr("ctnative.lifted", mlir::UnitAttr::get(context));
            ++out.closures;
            ++out.methods;
            out.captures += captures;
        }
        ctjs::CreateClosureOp only = made.front();
        llvm::SmallVector<mlir::Value> captured;
        for (unsigned i = 0; i < static_cast<unsigned>(only.getUpvalues().size()); ++i) {
            captured.push_back(liftedCapture(only, i));
        }
        for (methodCall at : callsOfTarget[target.getOperation()]) {
            mlir::OpBuilder builder(at.call);
            const mlir::Value undefined = ctjs::ConstantOp::create(
                builder, at.call.getLoc(), valueType, ctjs::UndefinedAttr::get(context));
            llvm::SmallVector<mlir::Value> arguments(captured);
            arguments.append(at.call.getArgs().begin(), at.call.getArgs().end());
            while (arguments.size() < captures + parameters) { arguments.push_back(undefined); }
            // THE RECEIVER IS OPERAND 0 AND ALWAYS WAS. ctjs.call_direct's
            // operands ARE the callee's entry block in order, so passing
            // the object here is the whole of "the receiver is a
            // parameter" - `%arg0` is where it lands with no reordering.
            //
            // $callee_value IS UNDEFINED, and this is the one place this
            // rewrite differs from the closure lift. There, the closure
            // value still exists and is passed. Here the callee VALUE is
            // `at.load`, a property read of the object, and keeping it
            // would leave a `!ctnative.boxed` result in an accepted
            // function for no consumer: the native call arm drops operand
            // 2, and this rewrite runs inside --ctnative-lower-to-emitc so
            // the boxed tier never sees the op. The load is erased below.
            auto direct = ctjs::CallDirectOp::create(
                builder, at.call.getLoc(), valueType,
                mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()),
                carriesReceiver ? at.receiver : undefined, undefined, undefined, arguments,
                /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
            if (carriesReceiver) {
                direct->setAttr("ctnative.receiver", mlir::UnitAttr::get(context));
            }
            // AND WHICH OF ITS ARGUMENTS IS AN ADDRESS. On the CALL as
            // well as the callee, for the reason the receiver mark is on
            // both: `replace()` reads it once per operand and a symbol
            // lookup there would be a lookup per argument per call.
            if (!cellArgs.empty()) {
                direct->setAttr("ctnative.cell_args",
                                mlir::Builder(context).getDenseI32ArrayAttr(cellArgs));
            }
            at.call.getResult().replaceAllUsesWith(direct.getResult());
            at.call.erase();
            // AND THE METHOD LOAD GOES WITH IT. Leaving it would keep a
            // use of the object that `hasClosedShape` reads as open, which
            // would refuse the very literal this rewrite just admitted.
            //
            // WITH ITS KEY CONSTANT, IF NOTHING ELSE HOLDS ONE. A string
            // constant with no users left is not a property key any more -
            // `isKeyOnlyString` needs a use to recognise one - so it falls
            // through admission to "a constant that is not a number, a
            // boolean or undefined" and refuses the whole function. The
            // importer may or may not share the constant with the store
            // that binds the field, so this asks rather than assumes.
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

    for (ctjs::CreateClosureOp c : made) {
        // THE VALUE, NOT THE CELL, in both shapes: a constant cell's
        // initial, or the enclosing function's capture parameter passed
        // as it is - it already holds the value (slice 1b). The slot's
        // INDEX is what selects between them, because a slot the enclosing
        // closure fills carries a placeholder operand and nothing else.
        llvm::SmallVector<mlir::Value> captured;
        for (unsigned i = 0; i < static_cast<unsigned>(c.getUpvalues().size()); ++i) {
            captured.push_back(liftedCapture(c, i));
        }
        // BOTH SHAPES OF CALL SITE, because --ctjs-resolve-globals may have
        // named this one already. `whyNotLiftable` admits a ctjs.call at
        // operand 0 and a ctjs.call_direct at operand 2; the difference
        // between them is where the receiver and the arguments are read
        // from, and nothing else - the captures still have to be prepended,
        // which is the whole reason the lift re-writes an already-direct
        // call rather than leaving it alone.
        llvm::SmallVector<mlir::Operation *> calls(c.getResult().getUsers().begin(),
                                                   c.getResult().getUsers().end());
        for (mlir::Operation * user : calls) {
            auto call = llvm::dyn_cast<ctjs::CallOp>(user);
            auto named = llvm::dyn_cast<ctjs::CallDirectOp>(user);
            // EVERY USE IS A CALL, BECAUSE CONDITION 4 SAID SO - and this
            // says which claim failed when it is not.
            //
            // `whyNotLiftable`'s condition 4 admits a closure only when
            // every use of its value is a ctjs.call at operand 0 or a
            // ctjs.call_direct at operand 2, so `call` and `named` cannot
            // both be null here. Without this the next line dereferenced
            // the null one and ctjs-opt SEGFAULTED - measured by relaxing
            // condition 4, on the new fixture and on bootstrap, p5, phaser,
            // differential and launcher alike. A crash names nothing, and
            // the comment on the SHAREDRETURN pin said the failure was a
            // C++ compile error, which it is not: no C++ is emitted at all.
            // This file's idiom for "a rule let one through" is a named
            // fatal, and condition 4 is the rule that keeps a pointer to
            // this frame from leaving it, so its violation is worth saying
            // out loud rather than discovering in a debugger.
            if (!call && !named) {
                llvm::report_fatal_error(
                    llvm::Twine("ctnative lowering: `") + target.getSymName() +
                    "` is lifted but its closure reaches `" + user->getName().getStringRef() +
                    "`, which is not a call - whyNotLiftable's condition 4 admitted a use it "
                    "should have refused, and for a carried binding that use is what would "
                    "take the address of this frame out of it");
            }
            mlir::OpBuilder at(user);
            const mlir::Value undefined = ctjs::ConstantOp::create(
                at, user->getLoc(), valueType, ctjs::UndefinedAttr::get(context));
            llvm::SmallVector<mlir::Value> arguments(captured);
            const mlir::ValueRange supplied = call ? call.getArgs() : named.getArgs();
            arguments.append(supplied.begin(), supplied.end());
            // The resolver's own padding rule: op::call fills a missing
            // parameter with undefined, so a short call becomes a full one.
            while (arguments.size() < captures + parameters) { arguments.push_back(undefined); }
            auto direct =
                ctjs::CallDirectOp::create(at, user->getLoc(), valueType,
                                           mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()),
                                           call ? call.getReceiver() : named.getReceiver(),
                                           undefined, c.getResult(), arguments,
                                           /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
            // ON THE CALL AS WELL AS THE CALLEE, for the reason the
            // receiver mark is: `TypeInference::hasClosedShape` is asked
            // once per property access per solver visit, and a symbol
            // lookup there would be quadratic in the module.
            if (!objectArgs.empty()) {
                direct->setAttr("ctnative.object_args",
                                mlir::Builder(context).getDenseI32ArrayAttr(objectArgs));
            }
            if (!cellArgs.empty()) {
                direct->setAttr("ctnative.cell_args",
                                mlir::Builder(context).getDenseI32ArrayAttr(cellArgs));
            }
            user->getResult(0).replaceAllUsesWith(direct.getResult());
            user->erase();
            ++out.calls;
        }
        c->setAttr("ctnative.lifted", mlir::UnitAttr::get(context));
        ++out.closures;
        out.captures += captures;
    }
}

} // namespace ctcompile::ctnative::lowering_detail
