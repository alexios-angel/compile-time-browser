//===- ClosureLifting/Captures.cpp - Closure capture admission --*- C++ -*-===//
//
// The checks that decide whether a closure can be lifted: whether its target
// is liftable, which value each capture slot passes at a call, whether those
// values reach every call site, and whyNotLiftable, which combines them into
// one refusal reason per closure.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {

/// The capture checks shared with the receiver lift. A method is a closure in
/// the IR (`{ f: function () {} }` compiles to a `closure` opcode and a
/// `set_prop`), so a method that also captures a binding has to satisfy the
/// same checks, and sharing them keeps the two rules consistent.
std::optional<std::string> closureLifter::whyTargetIsNotLiftable(ctjs::CreateClosureOp c) {
    ctjs::FuncOp target = targetOf(c);
    if (!target) {
        return "its target emitted no ctjs.func - the importer refused it (ctjs.skipped)";
    }
    if (target.getBody().empty() || target.getBody().front().getNumArguments() < 3) {
        return "its target has no body";
    }
    mlir::Block & entry = target.getBody().front();
    const auto captures = static_cast<unsigned>(c.getUpvalues().size());
    if (captures != static_cast<unsigned>(target.getUpvalueCount())) {
        return "its capture list disagrees with the descriptors of the function it names";
    }
    // An arrow's `this` is lexical, and after the importer's correction a
    // non-undefined $enclosing_this is the only place the IR says a target is
    // an arrow. A lifted call passes the call's receiver as %arg0, which for
    // an arrow is not what the interpreter reads, so an arrow may be lifted
    // only when it never reads `this`.
    //
    // This guard keeps arrows out of the receiver lift, and removing it would
    // give a wrong answer rather than a refusal: `{ f: () => this.x }` reads
    // the enclosing `this`, every use of it is a constant-key read the
    // receiver rule admits, and lifting it would silently rebind `this` to
    // the literal. It is checked first for that reason, before any rule that
    // admits a `this` use.
    if (!ctjs::isUndefinedConstant(c.getEnclosingThis()) && !entry.getArgument(0).use_empty()) {
        return "it is an arrow function that reads its lexical `this` - Stage 59B";
    }
    return std::nullopt;
}

/// Which upvalue of the enclosing closure fills capture slot i, or -1 when the
/// operand beside it is the binding itself. The `enclosing_indices` list is
/// optional and, when present, exactly as long as the capture list (see
/// ctjs.create_closure's description and verifier), so a missing attribute
/// means every slot is from_parent_local, which is most closures.
std::int32_t closureLifter::enclosingIndex(ctjs::CreateClosureOp c, unsigned i) {
    const mlir::DenseI32ArrayAttr indices = c.getEnclosingIndicesAttr();
    if (!indices || i >= static_cast<unsigned>(indices.size())) { return -1; }
    return indices[i];
}

/// The slot of `holder` that captures the same cell as slot `index` of
/// `closure`, when both are made in the same function. Matches cell identity,
/// never its current value or its source spelling.
// ponytail: only existing direct sibling captures; deeper relays or missing
// capture slots need an explicit environment propagation proof.
std::optional<unsigned> closureLifter::siblingCaptureSlot(ctjs::CreateClosureOp closure,
                                                          unsigned index,
                                                          ctjs::CreateClosureOp holder) {
    if (enclosingIndex(closure, index) >= 0 ||
        !closure.getUpvalues()[index].getDefiningOp<ctjs::CreateCellOp>() ||
        closure->getParentOfType<ctjs::FuncOp>() != holder->getParentOfType<ctjs::FuncOp>()) {
        return std::nullopt;
    }
    for (unsigned slot = 0; slot < holder.getUpvalues().size(); ++slot) {
        if (enclosingIndex(holder, slot) < 0 &&
            holder.getUpvalues()[slot] == closure.getUpvalues()[index]) {
            return slot;
        }
    }
    return std::nullopt;
}

/// The lifted capture parameter of sibling caller `holder` that already holds
/// the cell slot `index` of `closure` captures, or null when there is none or
/// it is carried differently.
mlir::Value closureLifter::siblingCapturedValue(ctjs::CreateClosureOp closure, unsigned index,
                                                ctjs::CreateClosureOp holder) {
    if (!lifted.contains(holder.getOperation())) { return {}; }
    const auto slot = siblingCaptureSlot(closure, index, holder);
    auto target = targetOf(holder);
    const auto captures =
        target ? target->getAttrOfType<mlir::IntegerAttr>(kNativeCaptures) : mlir::IntegerAttr{};
    if (!slot || !captures || static_cast<int64_t>(*slot) >= captures.getInt() ||
        target.getBody().empty()) {
        return {};
    }
    const unsigned argument = captureArgument(*slot);
    const bool carried = listsSlot(target, kNativeCellArgs, argument);
    if (argument >= target.getBody().front().getNumArguments() ||
        slotIsCarried(closure, index) != carried) {
        return {};
    }
    return target.getBody().front().getArgument(argument);
}

/// The value a lifted call passes for capture slot i, or null when the slot
/// is neither shape a lift carries:
///
///   * The operand is a ctjs.create_cell of this frame: the cell's constant
///     value (or the cell itself when it is carried by pointer). A read of
///     the capture in the target is a read of that value, because
///     ctjs.load_upvalue reads through the cell (VM_CASE(get_upvalue) in
///     ctbrowser/lib/Script/vm/run_loop.cpp), and isConstantCell has to prove
///     the value never changes.
///   * A capture relayed from the enclosing closure: `enclosing_indices[i]`
///     is k >= 0, so the slot is filled from the enclosing closure's upvalue
///     k. The enclosing ctjs.func has already been lifted, so it carries
///     `ctnative.captures` and lift() made its upvalue k the entry-block
///     argument 3 + k, holding the value of that cell. Passing that argument
///     on passes the same value. The capture operand at i is the importer's
///     `undefined` placeholder and is never read (nor does
///     ct_aot_make_closure read it).
///
/// Nothing else qualifies. A parameter of the enclosing function (index at or
/// past 3 + captures) is not a capture and no index names it (a captured
/// parameter is boxed, so its slot is from_parent_local and its operand is
/// the cell); %arg0-2 are never captures; and a k outside the enclosing
/// function's capture range names an upvalue the lift did not carry.
mlir::Value closureLifter::capturedValue(ctjs::CreateClosureOp c, unsigned i) {
    if (auto cell = c.getUpvalues()[i].getDefiningOp<ctjs::CreateCellOp>()) {
        // A cell carried by pointer: return the cell itself. The call site
        // takes its address (replace() does this from the
        // `ctnative.cell_args` index, through asPointer, as for a receiver),
        // so what must dominate the call is the cell variable, not its value.
        if (isCarried(cell)) { return cell.getResult(); }
        return constantValueOf(cell);
    }
    const std::int32_t k = enclosingIndex(c, i);
    if (k < 0) { return {}; }
    auto enclosing = c->getParentOfType<ctjs::FuncOp>();
    if (!enclosing || enclosing.getBody().empty()) { return {}; }
    const auto captures = enclosing->getAttrOfType<mlir::IntegerAttr>(kNativeCaptures);
    if (!captures || k >= captures.getInt()) { return {}; }
    // lift() guarantees at least 3 + captures arguments by inserting them
    // into a block that already had three. Checked anyway, because this reads
    // an argument by number and the check costs one comparison.
    mlir::Block & entry = enclosing.getBody().front();
    if (entry.getNumArguments() <= captureArgument(static_cast<unsigned>(k))) { return {}; }
    return entry.getArgument(captureArgument(static_cast<unsigned>(k)));
}

/// capturedValue() after admission, where a null value means a rule let a
/// capture through; that is reported as a named fatal error.
mlir::Value closureLifter::liftedCapture(ctjs::CreateClosureOp c, unsigned i) {
    if (const mlir::Value value = capturedValue(c, i)) { return value; }
    llvm::report_fatal_error(
        "ctnative lowering: a capture admitted by whyCapturesDoNotLift is neither a constant "
        "cell of its frame nor a lifted capture parameter of the enclosing function - the "
        "admission and the call-site rewrite have drifted apart");
}

llvm::SmallVector<mlir::Value> closureLifter::liftedCaptures(ctjs::CreateClosureOp c) {
    llvm::SmallVector<mlir::Value> values;
    for (unsigned i = 0; i < static_cast<unsigned>(c.getUpvalues().size()); ++i) {
        values.push_back(liftedCapture(c, i));
    }
    return values;
}

mlir::Value closureLifter::undefinedAt(mlir::OpBuilder & at, mlir::Location where) {
    return ctjs::ConstantOp::create(at, where, ctjs::ValueType::get(context),
                                    ctjs::UndefinedAttr::get(context));
}

void closureLifter::markLifted(ctjs::CreateClosureOp c, liftReport & out, unsigned captures) {
    c->setAttr(kNativeLifted, mlir::UnitAttr::get(context));
    ++out.closures;
    out.captures += captures;
}

bool closureLifter::reachesInFrame(mlir::Value from, mlir::Operation * at) {
    return from.getParentRegion()->getParentOfType<ctjs::FuncOp>() ==
               at->getParentOfType<ctjs::FuncOp>() &&
           dominance.properlyDominates(from, at);
}

bool closureLifter::reachesInFrame(mlir::Operation * from, mlir::Operation * at) {
    return from->getParentOfType<ctjs::FuncOp>() == at->getParentOfType<ctjs::FuncOp>() &&
           dominance.properlyDominates(from, at);
}

ctjs::CallDirectOp closureLifter::directCall(mlir::OpBuilder & at, mlir::Location where,
                                             ctjs::FuncOp target, mlir::Value receiver,
                                             mlir::Value callee, mlir::Value undefined,
                                             llvm::ArrayRef<mlir::Value> prefix,
                                             mlir::ValueRange args, size_t width) {
    llvm::SmallVector<mlir::Value> arguments(prefix);
    llvm::append_range(arguments, args);
    while (arguments.size() < width) { arguments.push_back(undefined); }
    return ctjs::CallDirectOp::create(at, where, ctjs::ValueType::get(context),
                                      mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()),
                                      receiver, undefined, callee, arguments,
                                      /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
}

/// Returns why the captured values of `c` are not available at call site
/// `at`, or nullopt when they are. lift() passes each captured value at the
/// call, and the interpreter reads the box when the closure runs, so each
/// value must be defined in the caller's function and dominate the call, and
/// a single-write cell's store must dominate it too (condition 3 of rule 1 in
/// ClosureLifter.h).
///
/// This is not implied by `c` dominating the call: a function declaration's
/// closure is hoisted above its store, a method is reached through an object
/// that nothing orders after the ctjs.set_property that bound it, and in a
/// graph region (builtin.module) every op dominates every other, so the
/// function is compared before dominance is asked.
std::optional<std::string> closureLifter::whyCapturesDoNotReach(ctjs::CreateClosureOp c,
                                                                mlir::Operation * at) {
    auto here = at->getParentOfType<ctjs::FuncOp>();
    for (unsigned i = 0; i < static_cast<unsigned>(c.getUpvalues().size()); ++i) {
        mlir::Value value = capturedValue(c, i);
        // Null is a slot no rule admits; whyCapturesDoNotLift says which.
        if (!value) { continue; }
        if (value.getParentRegion()->getParentOfType<ctjs::FuncOp>() != here) {
            return "capture " + std::to_string(i) +
                   " is a binding of the frame that built the closure, and this call of it "
                   "is in another function - lifting prepends the captured value at the "
                   "CALL, and there is nothing to prepend here";
        }
        if (!dominance.properlyDominates(value, at)) {
            return "capture " + std::to_string(i) +
                   " is a binding whose value does not reach this call of it - the "
                   "assignment does not dominate the call, so the interpreter reads the "
                   "undefined the binding was hoisted with";
        }
        // The single assignment itself must also dominate the call, not just
        // the value it stores. Store-dominates-call implies
        // value-dominates-call, but not the other way round. A value computed
        // before a branch dominates the call even when the store is inside
        // the branch:
        //
        //     function pick(k) { var v; var t = k * 2;
        //                        if (k > 0) { v = t; }
        //                        function get() { return v; } return get(); }
        //
        // Here pick(-1) must return undefined, not -2. Native lowering may
        // refuse a program but must never compile it to a different answer.
        //
        // A cell carried by pointer is exempt: the callee reads the variable
        // when it runs, and on a path with no store it reads the NaN-boxed
        // `undefined` the variable was initialised with, which matches the
        // interpreter. `writtenOnce` still holds such a cell (it has one
        // ctjs.cell_set), so the test is on isCarried, not on the map.
        if (auto cell = c.getUpvalues()[i].getDefiningOp<ctjs::CreateCellOp>();
            cell && !isCarried(cell)) {
            if (ctjs::CellSetOp write = writtenOnce.lookup(cell.getOperation())) {
                if (!dominance.properlyDominates(write.getOperation(), at)) {
                    return "capture " + std::to_string(i) +
                           " is a binding whose single assignment does not dominate this "
                           "call of it - the call can be reached without the assignment "
                           "having run, and the interpreter reads the undefined the binding "
                           "was hoisted with";
                }
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> closureLifter::whyCallSiteDoesNotLift(mlir::Operation * site,
                                                                 size_t arguments,
                                                                 size_t parameters,
                                                                 llvm::StringRef noun) {
    if (arguments > parameters) {
        return (noun + " passes " + std::to_string(arguments) + " argument(s) to " +
                std::to_string(parameters) + " parameter(s) - the surplus has frame semantics")
            .str();
    }
    auto caller = site->getParentOfType<ctjs::FuncOp>();
    if (caller && passesNewTarget.contains(caller.getOperation())) {
        return (noun + " of it sits in a function that passes new.target").str();
    }
    return std::nullopt;
}

std::optional<std::string> closureLifter::whyCapturesDoNotLift(ctjs::CreateClosureOp c) {
    if (const std::optional<std::string> why = whyTargetIsNotLiftable(c)) { return why; }
    const auto captures = static_cast<unsigned>(c.getUpvalues().size());
    auto enclosing = c->getParentOfType<ctjs::FuncOp>();
    // A capture must be a constant or carried cell in this frame, or a lifted
    // capture parameter of the enclosing function.
    for (unsigned i = 0; i < captures; ++i) {
        const mlir::Value operand = c.getUpvalues()[i];
        if (auto cell = operand.getDefiningOp<ctjs::CreateCellOp>()) {
            // A cell proved constant is copied into a parameter; a cell that
            // is not constant but can be carried is passed by pointer to a
            // frame-local variable. Only a cell that is neither is refused.
            if (isConstantCell(cell) || isCarried(cell)) { continue; }
            // sharedCellCensus records a reason for every cell isConstantCell
            // refuses, so a cell with no recorded reason means the two
            // analyses disagree; that is a fatal error, not a refusal.
            const auto shared = whyNotCarried.find(cell.getOperation());
            if (shared == whyNotCarried.end()) {
                llvm::report_fatal_error(
                    "ctnative lowering: a cell is neither constant nor carried and the "
                    "shared-cell census wrote no reason for it - sharedCellCensus and "
                    "isConstantCell have drifted apart");
            }
            return "capture " + std::to_string(i) +
                   " is a shared binding this tier cannot make a frame-local variable: " +
                   shared->second;
        }
        if (capturedValue(c, i)) { continue; }
        if (enclosingIndex(c, i) >= 0 && enclosing && !enclosing->hasAttr(kNativeCaptures)) {
            chainedThrough[c.getOperation()] = enclosing;
            return "capture " + std::to_string(i) +
                   " is filled from the enclosing closure, which did not lift";
        }
        return "capture " + std::to_string(i) +
               " is neither a cell of this frame nor a capture parameter of the enclosing "
               "function";
    }
    return std::nullopt;
}

/// Refuses a target whose own closure value is used for anything other than
/// nested closures and upvalue reads (the same test ResolveGlobals applies,
/// via ctjs::ownClosureEscape). Lifting marks the target `private`, which
/// claims every caller is visible; a leaked closure value could be called
/// from somewhere this IR cannot see.
std::optional<std::string> closureLifter::whyOwnClosureEscapes(ctjs::FuncOp target) {
    if (mlir::Operation * user = ctjs::ownClosureEscape(target)) {
        return ("its target's own closure escapes into `" + user->getName().getStringRef() +
                "`, so a call of it may come from somewhere this rewrite cannot see")
            .str();
    }
    return std::nullopt;
}

/// The target's own upvalue reads have to be the shape the rewrite replaces,
/// or a load would be left naming a closure that is gone.
///
/// Writes are checked too. A ctjs.store_upvalue is admitted only when the
/// slot it writes is carried by pointer (`slotIsCarried(c, k)`, true for a
/// carried cell of the creating frame and for a slot filled from an enclosing
/// capture that is already a pointer). A by-value copy of a mutated binding
/// would give every call its own copy, which is wrong (see the counter case
/// in test/CTNative/Fixtures/Closures/shared-cell.js). The closure is a parameter
/// because "is this slot carried" depends on the creation site's operands,
/// not on the target. A read or store that names something other than
/// `%arg2`, or an index past the capture list, is refused because the
/// closure operand it names is about to be erased.
std::optional<std::string> closureLifter::whyUpvalueReadsDoNotLift(ctjs::CreateClosureOp c,
                                                                   ctjs::FuncOp target) {
    mlir::Block & entry = target.getBody().front();
    const auto captures = static_cast<unsigned>(target.getUpvalueCount());
    std::optional<std::string> bad;
    target.getBody().walk([&](mlir::Operation * o) {
        if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(o)) {
            if (read->hasAttr(kNativeEnvironmentRead)) { return; }
            if (read.getClosure() != entry.getArgument(2) ||
                static_cast<unsigned>(read.getIndex()) >= captures) {
                bad = "its target reads an upvalue this rewrite cannot name";
            }
        }
        if (auto write = llvm::dyn_cast<ctjs::StoreUpvalueOp>(o)) {
            const auto k = static_cast<unsigned>(write.getIndex());
            if (write.getClosure() != entry.getArgument(2) || k >= captures) {
                bad = "its target writes an upvalue this rewrite cannot name";
            } else if (!slotIsCarried(c, k)) {
                bad = "its target reassigns capture " + std::to_string(k) +
                      ", which is not a binding this tier carries by pointer - copying it "
                      "would give every call its own";
            }
        }
    });
    return bad;
}

/// Returns why closure `c` cannot be lifted to direct calls with its captures
/// passed as arguments, or nullopt when it can. The reason is attached to the
/// closure so that the enclosing function is refused with that reason rather
/// than with the generic "`ctjs.create_closure` is not native yet".
std::optional<std::string> closureLifter::whyNotLiftable(ctjs::CreateClosureOp c) {
    if (const std::optional<std::string> why = whyCapturesDoNotLift(c)) { return why; }
    ctjs::FuncOp target = targetOf(c);
    if (const auto found = boundCaptureCalls.find(target.getOperation());
        found != boundCaptureCalls.end()) {
        for (const boundCaptureCall & site : found->second) {
            for (unsigned i = 0; i < c.getUpvalues().size(); ++i) {
                mlir::Value value = siblingCapturedValue(c, i, site.holder);
                if (!value || !reachesInFrame(value, site.call)) {
                    return "a sibling caller has not lifted the identical captured binding";
                }
            }
        }
    }
    mlir::Block & entry = target.getBody().front();
    const unsigned parameters = entry.getNumArguments() - ctjs::implicit_arguments;
    // Condition 4: every use of the closure value is a call this lowers.
    //
    // A closure bound to a local name may have no uses at all: when the name
    // is only called from inside other functions, each of those calls is a
    // ctjs.call_direct with `undefined` as its callee value, because the
    // closure is not in scope there. bindLocalFunctions has already proved at
    // least one such call exists ("nothing calls it" is one of its refusals).
    if (c.getResult().use_empty() && !bindingClosures.contains(c.getOperation())) {
        return "nothing calls it";
    }
    // Check the local-binding store first, before the use loop below. A
    // binding the local-function rule refused keeps its cell; that cell is
    // then lifted by value and gives the closure a second use as a
    // ctjs.call_direct argument. If the loop ran first, the reported reason
    // would depend on use-list order and would usually be "it is passed as an
    // argument", which is true but not the actual obstacle. The BOUNDVALUE
    // and BOUNDDATA cases in test/CTNative/Lowering/Closures/refusals.mlir
    // check this.
    for (mlir::OpOperand & use : c.getResult().getUses()) {
        if (auto into = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner());
            into && use.getOperandNumber() == 1) {
            return whyNotABoundFunction(into);
        }
    }
    for (mlir::OpOperand & use : c.getResult().getUses()) {
        mlir::Operation * user = use.getOwner();
        // A direct call that --ctjs-resolve-globals made from a ctjs.call
        // whose callee was this closure. The closure is now operand 2
        // ($callee_value), not operand 0, so without this case the use would
        // be refused as "passed as an argument". No arity check is needed:
        // CallDirectOp's verifier requires the operand count to match the
        // target's entry block (the resolver padded a short call and refused
        // a long one).
        if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user);
            direct && use.getOperandNumber() == 2 && direct.getCallee() == target.getSymName()) {
            if (auto why = whyCallSiteDoesNotLift(direct, 0, parameters, "a call")) { return why; }
            continue;
        }
        auto call = llvm::dyn_cast<ctjs::CallOp>(user);
        if (!call || use.getOperandNumber() != 0) {
            if (llvm::isa<ctjs::StoreGlobalOp>(user)) {
                return "it is stored to a global - Phase 59 slice 2";
            }
            if (llvm::isa<ctjs::ReturnOp>(user)) { return "it is returned - Phase 59 slice 2"; }
            // A method field the receiver lift did not take. "It is stored
            // into an object" for `{f: function(){}}` names the mechanism,
            // not the obstacle; the obstacle is always either that the shape
            // is not closed or that the key is written twice, and
            // whyNotAMethodField says which.
            if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                set && use.getOperandNumber() == 2) {
                return whyNotAMethodField(set);
            }
            if (llvm::isa<ctjs::SetPropertyOp, ctjs::CreateObjectOp, ctjs::AppendOp,
                          ctjs::CreateArrayOp>(user)) {
                return "it is stored into an object or an array - Phase 59 slice 2";
            }
            // A closure used with `new` is not refused here: every closure a
            // `ctjs.construct` names is in `constructorClosures` and is
            // handled by whyNotLiftableConstructor.
            if (call || llvm::isa<ctjs::CallDirectOp, ctjs::ConstructOp>(user)) {
                // Passing a closure as an argument cannot be lifted: lifting
                // moves the captures to the call site, and a callee that
                // receives a function value has no fixed call site to move
                // them to. Supporting it needs the callee specialised per
                // closure (a monomorphisation pass), not a lift.
                return "it is passed as an argument - lifting has no call site to move the "
                       "captures to, so this needs a specialised callee (Phase 63), not a "
                       "lift";
            }
            return ("it reaches `" + user->getName().getStringRef() +
                    "`, which slice 1 does "
                    "not lower")
                .str();
        }
        if (auto why = whyCallSiteDoesNotLift(call, call.getArgs().size(), parameters, "a call")) {
            return why;
        }
    }
    // The captured values must also reach every one of those sites. The loop
    // above has established that every use of the closure value is a call
    // this rewrite lowers, so its users are exactly the sites lift() will
    // prepend the captures at.
    return whyLiftTailFails(c, target, c.getResult().getUsers());
}

} // namespace ctcompile::ctnative::lowering_detail
