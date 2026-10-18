//===- ClosureLifting/ClosureSets.cpp - Calls through a closure set -------===//
//
// Lifts closures whose values meet before they are called, such as
//
//   const f = c ? (x => x + k) : (x => x * 3);
//   return f(5) + f(1);
//
// No call site names one closure, so none can receive the captures the way a
// lift passes them. Instead each closure stays a value bound to its captures,
// and the value the call goes through is one of that finite set. Type
// inference gives it the lattice's `closure_set` type, and the emitted C++ is
// one struct per set whose enum class tag names the member, with a call that
// switches on the tag (EmitC/ClosureSets.cpp):
//
//   closure_set_fn_2_fn_3 v8 = closure_set_fn_2_fn_3::make_fn_2(k);
//   ctnative::invoke_callable(v8, 5.0);
//
// Closure lifting decides the structure: every value in the group is a
// closure, an scf.if result or a read of an object-literal field, each fed
// only by the group's values, and every use is a call through it, another
// such merge or a write of such a field. No value leaves the objects and
// frames the group proves, so the bound copies of the captures are all any
// call reads. Each member lifts
// the way a borrowed callback does (BorrowedCallbacks.cpp) and carries the
// same attributes, so type inference types the members and the calls, and
// admission requires one signature for all of them. The `ctnative.closure_set`
// attribute on a member's closure names the whole set, so emission
// (EmitC/ClosureSets.cpp) can spell the set's struct where the closure is
// made.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

#include "ctcompile/CTNative/Analysis/NativeClosure.h"

#include "mlir/Dialect/SCF/IR/SCF.h"

#include <array>
#include <functional>

namespace ctcompile::ctnative::lowering_detail {

namespace {

/// The scf.if result that `use`, an operand of the scf.yield ending one of the
/// scf.if's branches, becomes; null for any other use.
mlir::Value mergedBy(mlir::OpOperand & use) {
    auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(use.getOwner());
    auto merge = yield ? llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp()) : nullptr;
    return merge ? merge.getResult(use.getOperandNumber()) : mlir::Value{};
}

/// The values the two branches of `merge`'s scf.if yield for it.
std::array<mlir::Value, 2> sourcesOf(mlir::OpResult merge) {
    auto branch = llvm::cast<mlir::scf::IfOp>(merge.getOwner());
    const unsigned at = merge.getResultNumber();
    if (branch.getElseRegion().empty()) { return {mlir::Value{}, mlir::Value{}}; }
    return {branch.thenYield().getOperand(at), branch.elseYield().getOperand(at)};
}

} // namespace

/// Groups the closures whose values meet at scf.if results, in a field of an
/// object literal or at a parameter that stores them, and are otherwise only
/// called, and keeps the groups of two or more closures with at least one
/// call.
///
/// A parameter counts when every call of its function is known and passes a
/// value of the group, as an event registry's `on(reg, h) { reg.click = h; }`
/// receives each handler. A field counts when the object is a literal of this
/// program, named directly, through a constant cell or through parameters
/// every call passes it for, and used only through constant keys or passed
/// to such a parameter, so every write of the field is visible: its writes
/// are the group's values and each read is one of the group's values, or
/// absent when no write has run. A call through a field of the object with
/// the object as its receiver passes it only as `this`, which no closure the
/// field can hold reads; rewriteClosureSetMember drops it, so the object stays
/// a closed shape.
void closureLifter::closureSetCensus() {
    closureSets.clear();
    llvm::DenseSet<mlir::Operation *> seen;
    // A function's parameters receive only what its calls pass when every use
    // of each closure of it is a call of that closure, or the one store of a
    // function declaration whose calls --ctjs-resolve-globals made direct
    // (objectArgumentDeclarations). The calls are read from the IR as it is
    // now: this census also runs on trial lifts.
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Operation *>> callsOf;
    llvm::DenseSet<mlir::Operation *> calledOtherwise;
    module.walk([&](mlir::Operation * op) {
        if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
            if (auto target = direct.getTarget()) { callsOf[target.getOperation()].push_back(op); }
            return;
        }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
            if (auto made = call.getCallee().getDefiningOp<ctjs::CreateClosureOp>()) {
                if (ctjs::FuncOp target = targetOf(made)) {
                    callsOf[target.getOperation()].push_back(op);
                }
            }
            return;
        }
        auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(op);
        ctjs::FuncOp target = made ? targetOf(made) : ctjs::FuncOp{};
        if (!target) { return; }
        for (mlir::OpOperand & use : made.getResult().getUses()) {
            mlir::Operation * user = use.getOwner();
            const bool called =
                (llvm::isa<ctjs::CallOp>(user) && use.getOperandNumber() == 0) ||
                (llvm::isa<ctjs::CallDirectOp>(user) && use.getOperandNumber() == 2 &&
                 llvm::cast<ctjs::CallDirectOp>(user).getTarget() == target);
            auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(user);
            const bool declared =
                store && objectArgumentDeclarations.lookup(store.getName()) == made;
            if (!called && !declared) { calledOtherwise.insert(target.getOperation()); }
        }
    });
    // The values every call passes for `parameter`, a declared parameter of
    // such a function; false when that is not proved.
    const auto passedFor = [&](mlir::BlockArgument parameter,
                               llvm::SmallVectorImpl<mlir::Value> & passed) {
        auto function = llvm::dyn_cast<ctjs::FuncOp>(parameter.getOwner()->getParentOp());
        if (!function || !parameter.getOwner()->isEntryBlock() ||
            parameter.getArgNumber() < ctjs::implicit_arguments ||
            calledOtherwise.contains(function.getOperation())) {
            return false;
        }
        const auto calls = callsOf.find(function.getOperation());
        if (calls == callsOf.end()) { return false; }
        const unsigned j = parameter.getArgNumber() - ctjs::implicit_arguments;
        for (mlir::Operation * call : calls->second) {
            const mlir::ValueRange args = argsOfCallSite(call);
            if (j >= args.size()) { return false; }
            passed.push_back(args[j]);
        }
        return true;
    };
    // The parameter `use`, an argument of a call, is passed to, or null.
    const auto parameterOf = [&](mlir::OpOperand & use) -> mlir::BlockArgument {
        mlir::Operation * user = use.getOwner();
        ctjs::FuncOp target;
        unsigned first = 0;
        if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user)) {
            target = direct.getTarget();
            first = ctjs::implicit_arguments;
        } else if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
            if (auto made = call.getCallee().getDefiningOp<ctjs::CreateClosureOp>()) {
                target = targetOf(made);
            }
            first = 2;
        }
        if (!target || target.getBody().empty() || use.getOperandNumber() < first) { return {}; }
        mlir::Block & entry = target.getBody().front();
        const unsigned at = use.getOperandNumber() - first + ctjs::implicit_arguments;
        return at < entry.getNumArguments() ? entry.getArgument(at) : mlir::BlockArgument{};
    };
    // The object literal `object` names: the literal itself, or a parameter
    // every call passes that one literal for, as `on(h) { reg.click = h; }`
    // receives a registry it captured once lifted. Null otherwise.
    const std::function<mlir::Value(mlir::Value, unsigned)> literalOf =
        [&](mlir::Value object, unsigned depth) -> mlir::Value {
        if (object.getDefiningOp<ctjs::CreateObjectOp>()) { return object; }
        // A read of a constant cell, such as a `const` a function captures.
        if (auto read = object.getDefiningOp<ctjs::CellGetOp>()) {
            auto cell = read.getCell().getDefiningOp<ctjs::CreateCellOp>();
            if (!cell || depth > 8 || !isConstantCell(cell)) { return {}; }
            return literalOf(constantValueOf(cell), depth + 1);
        }
        auto parameter = llvm::dyn_cast<mlir::BlockArgument>(object);
        llvm::SmallVector<mlir::Value> passed;
        if (!parameter || depth > 8 || !passedFor(parameter, passed)) { return {}; }
        mlir::Value literal;
        for (mlir::Value value : passed) {
            const mlir::Value each = literalOf(value, depth + 1);
            if (!each || (literal && each != literal)) { return {}; }
            literal = each;
        }
        return literal;
    };
    // Each literal and the parameters that name it.
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value>> aliases;
    module.walk([&](ctjs::CreateObjectOp made) { aliases[made.getResult()].push_back(made); });
    module.walk([&](ctjs::CellGetOp read) {
        if (const mlir::Value literal = literalOf(read, 0)) { aliases[literal].push_back(read); }
    });
    for (ctjs::FuncOp function : module.getOps<ctjs::FuncOp>()) {
        if (function.getBody().empty()) { continue; }
        for (mlir::BlockArgument parameter :
             function.getBody().front().getArguments().drop_front(ctjs::implicit_arguments)) {
            if (const mlir::Value literal = literalOf(parameter, 0);
                literal && literal != mlir::Value(parameter)) {
                aliases[literal].push_back(parameter);
            }
        }
    }
    // The constant key of a property access on an object literal, or empty;
    // `object` is set to the literal.
    const auto fieldOf = [&](mlir::Operation * access, mlir::Value & object) -> llvm::StringRef {
        mlir::Value key;
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(access)) {
            object = get.getObject();
            key = get.getKey();
        } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(access)) {
            object = set.getObject();
            key = set.getKey();
        } else {
            return {};
        }
        object = literalOf(object, 0);
        if (!object) { return {}; }
        return ctjs::constantKey(key);
    };
    // Whether `use` passes the literal `object` on to a parameter that names
    // the same literal, or is the one write of a constant cell holding it,
    // whose reads name it too.
    const auto passesOn = [&](mlir::OpOperand & use, mlir::Value object) {
        if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner());
            write && use.getOperandNumber() == 1) {
            auto cell = write.getCell().getDefiningOp<ctjs::CreateCellOp>();
            return cell && isConstantCell(cell) && constantValueOf(cell) == object;
        }
        const mlir::BlockArgument parameter = parameterOf(use);
        return parameter && literalOf(parameter, 0) == object;
    };
    // Whether `object`, the receiver of `call`, is unseen by the callee: the
    // callee is a constant-key read of `object` whose every write is a
    // closure (directly or through scf.if merges) that does not read `this`.
    // Such a call cannot write `object`'s fields out of sight, and when the
    // field is a set, rewriteClosureSetMember drops the receiver.
    const auto receiverIsUnread = [&](ctjs::CallOp call, mlir::Value object) {
        mlir::Value on;
        mlir::Operation * read = call.getCallee().getDefiningOp();
        const llvm::StringRef key = read ? fieldOf(read, on) : llvm::StringRef{};
        if (key.empty() || on != object) { return false; }
        llvm::SmallVector<mlir::Value> sources;
        for (mlir::Value name : aliases.lookup(object)) {
            for (mlir::Operation * user : name.getUsers()) {
                if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                    set && set.getObject() == name && fieldOf(user, on) == key) {
                    sources.push_back(set.getValue());
                }
            }
        }
        if (sources.empty()) { return false; }
        for (size_t i = 0; i < sources.size(); ++i) {
            if (auto made = sources[i].getDefiningOp<ctjs::CreateClosureOp>()) {
                ctjs::FuncOp target = targetOf(made);
                if (!target || target.getBody().empty() ||
                    !target.getBody().front().getArgument(ctjs::arg_receiver).use_empty()) {
                    return false;
                }
            } else if (auto merge = llvm::dyn_cast<mlir::OpResult>(sources[i]);
                       merge && llvm::isa<mlir::scf::IfOp>(merge.getOwner())) {
                for (mlir::Value source : sourcesOf(merge)) {
                    if (!source) { return false; }
                    sources.push_back(source);
                }
            } else if (auto parameter = llvm::dyn_cast<mlir::BlockArgument>(sources[i])) {
                llvm::SmallVector<mlir::Value> passed;
                if (sources.size() > 64 || !passedFor(parameter, passed)) { return false; }
                sources.append(passed.begin(), passed.end());
            } else {
                return false;
            }
        }
        return true;
    };
    for (ctjs::CreateClosureOp start : closures) {
        if (seen.contains(start.getOperation()) || lifted.contains(start.getOperation()) ||
            isDeclarationClosure(start)) {
            continue;
        }
        // Grow the group from both ends: forward through the merges and
        // fields a value feeds, backward through the values a merge or a
        // field is fed by.
        closureSet group;
        llvm::SetVector<mlir::Value> values;
        llvm::SetVector<std::pair<mlir::Value, llvm::StringRef>> fields;
        bool closed = true;
        // A value of the group is a closure, a merge, a read of a field, or a
        // parameter whose every call passes a value of the group.
        const auto add = [&](mlir::Value v) {
            mlir::Operation * def = v ? v.getDefiningOp() : nullptr;
            mlir::Value object;
            if (auto parameter = llvm::dyn_cast_or_null<mlir::BlockArgument>(v)) {
                // Type inference types the parameter as the set only when more
                // than one value reaches it; one closure alone types it as that
                // closure, which no set field or call takes.
                llvm::SmallVector<mlir::Value> passed;
                // A parameter that is only called is a borrowed callback's
                // (BorrowedCallbacks.cpp); one that is stored needs the set.
                const bool stored = llvm::any_of(v.getUses(), [](mlir::OpOperand & use) {
                    return llvm::isa<ctjs::SetPropertyOp>(use.getOwner()) &&
                           use.getOperandNumber() == 2;
                });
                if (!stored || !passedFor(parameter, passed) ||
                    (llvm::all_equal(passed) &&
                     passed.front().getDefiningOp<ctjs::CreateClosureOp>())) {
                    closed = false;
                    return;
                }
                values.insert(v);
                return;
            }
            if (!llvm::isa_and_nonnull<ctjs::CreateClosureOp, mlir::scf::IfOp>(def) &&
                !(llvm::isa_and_nonnull<ctjs::GetPropertyOp>(def) &&
                  !fieldOf(def, object).empty())) {
                closed = false;
                return;
            }
            values.insert(v);
        };
        add(start.getResult());
        for (size_t i = 0, f = 0; closed && (i < values.size() || f < fields.size());) {
            if (f < fields.size()) {
                const auto [object, key] = fields[f++];
                for (mlir::Value name : aliases.lookup(object)) {
                    for (mlir::Operation * user : name.getUsers()) {
                        mlir::Value on;
                        if (fieldOf(user, on) != key || on != object) { continue; }
                        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                            if (set.getObject() == name) { add(set.getValue()); }
                        } else if (llvm::cast<ctjs::GetPropertyOp>(user).getObject() == name) {
                            add(user->getResult(0));
                        }
                    }
                }
                continue;
            }
            const mlir::Value v = values[i++];
            for (mlir::OpOperand & use : v.getUses()) {
                mlir::Value object;
                if (auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                    call && use.getOperandNumber() == 0) {
                    group.calls.insert(call);
                } else if (auto test = llvm::dyn_cast<ctjs::UnaryOp>(use.getOwner());
                           test && test.getKind() == ctjs::UnaryKind::TypeOf) {
                    // `typeof f` reads only the kind of value, which the set
                    // decides; it does not let the value out.
                } else if (const mlir::Value merged = mergedBy(use)) {
                    add(merged);
                } else if (const mlir::BlockArgument parameter = parameterOf(use)) {
                    // An argument: the parameter it is passed to.
                    add(parameter);
                } else if (llvm::isa<ctjs::SetPropertyOp>(use.getOwner()) &&
                           use.getOperandNumber() == 2 &&
                           !fieldOf(use.getOwner(), object).empty()) {
                    fields.insert({object, fieldOf(use.getOwner(), object)});
                } else {
                    closed = false;
                    break;
                }
            }
            if (auto merge = llvm::dyn_cast<mlir::OpResult>(v);
                closed && merge && llvm::isa<mlir::scf::IfOp>(merge.getOwner())) {
                for (mlir::Value source : sourcesOf(merge)) { add(source); }
            }
            // A parameter is fed by every call's argument.
            llvm::SmallVector<mlir::Value> passed;
            if (auto parameter = llvm::dyn_cast<mlir::BlockArgument>(v);
                closed && parameter && passedFor(parameter, passed)) {
                for (mlir::Value source : passed) { add(source); }
            }
        }
        // Each field's object: used only through constant keys, or as the
        // receiver of a call that cannot see it.
        // A read no write dominates may find the field absent: type inference
        // types it opt<closure_set>, and a call through it is refused until a
        // typeof test narrows it.
        // Passing the object to a parameter that names it is no escape: that
        // parameter's uses are checked here too.
        for (const auto & entry : fields) {
            const mlir::Value object = entry.first;
            for (mlir::Value name : aliases.lookup(object)) {
                for (mlir::OpOperand & use : name.getUses()) {
                    if (!closed) { break; }
                    mlir::Operation * user = use.getOwner();
                    if (passesOn(use, object)) { continue; }
                    if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
                        closed &= use.getOperandNumber() == 1 && receiverIsUnread(call, object);
                        continue;
                    }
                    mlir::Value on;
                    closed &= use.getOperandNumber() == 0 && !fieldOf(user, on).empty();
                }
            }
        }
        // The one function the set lives in, when it lives in one.
        const auto frameOf = [](mlir::Value v) -> mlir::Operation * {
            return v.getParentRegion()->getParentOfType<ctjs::FuncOp>();
        };
        group.frame = frameOf(start.getResult());
        for (mlir::Value v : values) {
            if (frameOf(v) != group.frame) { group.frame = nullptr; }
        }
        for (const auto & entry : fields) {
            if (frameOf(entry.first) != group.frame) { group.frame = nullptr; }
        }
        for (mlir::Value v : values) {
            if (auto made = v.getDefiningOp<ctjs::CreateClosureOp>()) {
                seen.insert(made.getOperation());
                group.members.push_back(made);
            }
        }
        if (!closed || group.members.size() < 2 || group.calls.empty()) { continue; }
        // A member that reads its receiver is a method: `o.f = function ()
        // { return this.x; }` keeps the method rule and its reason, which
        // names the second write.
        bool eligible = true;
        for (ctjs::CreateClosureOp c : group.members) {
            ctjs::FuncOp target = targetOf(c);
            // A closure another rule takes keeps that rule (a callee that only
            // calls its parameter borrows it, BorrowedCallbacks.cpp).
            eligible &= !borrowedCallbacks.contains(c.getOperation()) &&
                        !arrayCallbacks.contains(c.getOperation()) &&
                        !stringCallbacks.contains(c.getOperation()) &&
                        !hostCallbacks.contains(c.getOperation()) &&
                        !promiseCallbacks.contains(c.getOperation()) &&
                        !returnedClosures.contains(c.getOperation());
            eligible &= target && !lifted.contains(c.getOperation()) && !isDeclarationClosure(c) &&
                        uniqueClosureByTarget.lookup(target.getOperation()) == c &&
                        !target.getBody().empty() &&
                        target.getBody().front().getArgument(ctjs::arg_receiver).use_empty();
        }
        if (!eligible) { continue; }
        auto shared = std::make_shared<closureSet>(std::move(group));
        for (ctjs::CreateClosureOp c : shared->members) { closureSets[c.getOperation()] = shared; }
    }
}

/// Why `c`, one closure of a set, cannot be lifted as a member.
std::optional<std::string> closureLifter::whyNotClosureSetMember(ctjs::CreateClosureOp c) {
    if (auto why = whyTargetIsNotLiftable(c)) { return why; }
    ctjs::FuncOp target = targetOf(c);
    mlir::Block & entry = target.getBody().front();
    // A call through the set passes no receiver or new.target the member
    // could read, and its argument count is fixed per call.
    if (!entry.getArgument(ctjs::arg_receiver).use_empty()) {
        return "a closure called through a set of closures reads `this`";
    }
    if (!entry.getArgument(ctjs::arg_new_target).use_empty()) {
        return "a closure called through a set of closures reads new.target";
    }
    // An arrow's lexical `this` (ArrowThis.cpp) is an object capture. A member
    // binds it as the pointer lift() passes (lexicalThisObjectArgs) when the
    // whole set lives in the frame that made the closure, the method whose
    // `this` it is: no value of the set leaves that frame, so no call through
    // the set outlives the receiver. Anything else is not carried.
    if (target->hasAttr("ctnative.lexical_this") &&
        (closureSets.lookup(c.getOperation())->frame != c->getParentOfType<ctjs::FuncOp>() ||
         lexicalThisObjectArgs(std::array<ctjs::CreateClosureOp, 1>{c}).empty())) {
        return "a closure called through a set of closures captures its lexical `this`, which a "
               "member does not carry";
    }
    if (ctjs::readsRawArguments(target)) {
        return "a closure called through a set of closures reads its `arguments`";
    }
    const unsigned declared = entry.getNumArguments() - ctjs::implicit_arguments;
    for (ctjs::CallOp call : closureSets.lookup(c.getOperation())->calls) {
        const auto passed = static_cast<unsigned>(call.getArgs().size());
        if (passed != declared) {
            return "a closure called through a set of closures declares " +
                   std::to_string(declared) + " parameters and a call through the set passes " +
                   std::to_string(passed) + " - one C++ signature needs the same count";
        }
    }
    // The member holds copies of its captures, made where the closure is: a
    // binding written afterwards would need the variable's address.
    for (unsigned i = 0; i < c.getUpvalues().size(); ++i) {
        if (slotIsCarried(c, i)) {
            return "a closure called through a set of closures captures binding " +
                   std::to_string(i) +
                   ", which is written after it is captured - a member takes its captures "
                   "by value";
        }
    }
    if (auto why = whyCapturesDoNotLift(c)) { return why; }
    return whyLiftTailFails(c, target, std::array<mlir::Operation *, 1>{c.getOperation()});
}

/// Why `c` is not lifted as a member of its set: its own reason, or another
/// member's, since the set has one representation for all of them.
std::optional<std::string> closureLifter::whyNotInClosureSet(ctjs::CreateClosureOp c) {
    if (auto why = whyNotClosureSetMember(c)) { return why; }
    for (ctjs::CreateClosureOp other : closureSets.lookup(c.getOperation())->members) {
        if (other == c) { continue; }
        if (auto why = whyNotClosureSetMember(other)) {
            return "another closure called through the same set is not lifted: " + *why;
        }
    }
    return std::nullopt;
}

/// Keeps `c` as a value bound to its lifted captures, names `target` on it and
/// on every call through the set, and names the whole set on `c`. lift() has
/// made the captures `target`'s leading parameters.
void closureLifter::rewriteClosureSetMember(ctjs::CreateClosureOp c, ctjs::FuncOp target,
                                            liftReport & out) {
    const auto captured = liftedCaptures(c);
    c.getUpvaluesMutable().assign(captured);
    c.removeEnclosingIndicesAttr();
    mlir::OpBuilder at(c);
    const mlir::Value undefined = undefinedAt(at, c.getLoc());
    c.getEnclosingClosureMutable().assign(undefined);
    c.getEnclosingThisMutable().assign(undefined);
    const auto symbol = mlir::FlatSymbolRefAttr::get(target.getSymNameAttr());
    c->setAttr(kNativeBorrowedCallback, symbol);
    c->removeAttr(kNativeClosureReason);
    target->setAttr(kNativeBorrowedCallback, mlir::UnitAttr::get(context));
    const closureSet & set = *closureSets.lookup(c.getOperation());
    llvm::SmallVector<llvm::StringRef> names;
    for (ctjs::CreateClosureOp member : set.members) {
        names.push_back(targetOf(member).getSymName());
    }
    llvm::sort(names);
    llvm::SmallVector<mlir::Attribute> members;
    for (llvm::StringRef name : names) {
        members.push_back(mlir::FlatSymbolRefAttr::get(context, name));
    }
    c->setAttr(kNativeClosureSet, mlir::ArrayAttr::get(context, members));
    for (ctjs::CallOp call : set.calls) {
        llvm::SmallVector<mlir::Attribute> callbacks;
        if (auto listed = call->getAttrOfType<mlir::ArrayAttr>(kNativeBorrowedCall)) {
            llvm::append_range(callbacks, listed.getValue());
        }
        callbacks.push_back(symbol);
        call->setAttr(kNativeBorrowedCall, mlir::ArrayAttr::get(context, callbacks));
        call->setAttr(kNativeClosureSet, mlir::UnitAttr::get(context));
        // `m.fn(x)` passes `m` as `this`, which no member reads
        // (whyNotClosureSetMember), so the call passes undefined instead and
        // `m` keeps a closed shape.
        if (!ctjs::isUndefinedConstant(call.getReceiver())) {
            mlir::OpBuilder before(call);
            call.getReceiverMutable().assign(undefinedAt(before, call.getLoc()));
        }
    }
    target->setAttr(kNativeClosureSet, mlir::UnitAttr::get(context));
    // No ctjs.call_direct names the member, and MLIR's DeadCodeAnalysis treats
    // a private function nothing calls as dead, so it stays public, as a
    // borrowed callback does; its parameters are typed from the closure and
    // the calls (Analysis/Inference/BorrowedCallbacks.cpp).
    mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Public);
    ++out.closures;
    out.captures += static_cast<unsigned>(captured.size());
}

} // namespace ctcompile::ctnative::lowering_detail
