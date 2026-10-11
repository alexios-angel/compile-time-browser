//===- Methods.cpp - Method and object-argument lifts --------- -*- C++ -*-===//
//
// The method lift and the object-argument lift: proving which `o.m(args)`
// calls can become direct calls `m(o, args)`, and which object arguments can
// become a pointer to a closed-shape object, and naming the reason when they
// cannot.
//
// The method lift turns `o.m(args)` into a direct call when:
//  1. The receiver is an object literal (or a lifted constructor's instance)
//     whose every use is a constant-key get/set, a method call on it, or a
//     proved object argument (closedAfterLift).
//  2. Every literal the receiver may name binds the key exactly once, all to
//     the same function (resolveMethod).
//  3. The target uses `this` only through constant keys or as the receiver
//     of another resolved method call (whyThisLeaks).
//  4. The target is otherwise native and its captures lift
//     (whyCapturesDoNotLift).
//
//===----------------------------------------------------------------------===//
#include "ClosureLifter.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSwitch.h"

namespace ctcompile::ctnative::lowering_detail {

// The receiver lift.

/// Condition 1, as it will read after the rewrite. hasClosedShape refuses
/// any use that is not a constant-key get or set, and a method call is two
/// other uses (the object as the call's receiver, and the get_property that
/// loads the method), so asking it before the rewrite would refuse every
/// object with a method. This asks the same question of the IR the rewrite
/// will produce, where the load is gone and the receiver is a call_direct
/// operand that hasClosedShape admits.
bool closureLifter::closedAfterLift(mlir::Value object) {
    if (!object.getDefiningOp<ctjs::CreateObjectOp>() && !makesAnInstance(object)) { return false; }
    return usesCloseTheShape(object);
}

/// True when `object` is the result of a ctjs.construct that the constructor
/// lift will turn into an object literal.
///
/// The constructor lift replaces the construct with an empty
/// ctjs.create_object and a receiver call, but every census here runs before
/// that rewrite and would otherwise answer "not a literal", refusing an
/// instance merely passed to a lifted function. Like closedAfterLift, this
/// asks the question of the IR the rewrite will produce.
///
/// It asks the full constructor proof, not just the diagnostic census. That
/// proof's shape check reads objectSlotsOf without recursing here;
/// argumentCensus's shrinking fixpoint removes any slots whose constructor
/// dependencies fail.
bool closureLifter::makesAnInstance(mlir::Value object) {
    auto built = object.getDefiningOp<ctjs::ConstructOp>();
    if (!built) { return false; }
    auto closure = built.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
    if (!closure || !constructorClosures.contains(closure.getOperation())) { return false; }
    return !whyNotLiftableConstructor(closure);
}

/// True when `use` stores a constructed record into a local `new Map()` via
/// `set`, and every use of that Map and of the records read back from it is
/// confined to the same entry block. Read-back aliases are added to `reads`.
///
/// This proves the record's shape and lifetime only on the assumption that
/// `Map` is the standard one. All Map operations survive the lift;
/// prepareNativeMaps must independently prove that identity before native
/// admission, after local constructors have become direct calls. Attributes
/// already on the input prove nothing.
bool closureLifter::retainedByLocalMap(mlir::OpOperand & use,
                                       llvm::DenseMap<mlir::Value, mlir::Value> * reads) {
    auto selected = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
    auto owner = use.get().getDefiningOp<ctjs::ConstructOp>();
    if (!selected || use.getOperandNumber() != 3 || !owner) { return false; }
    auto map = selected.getReceiver().getDefiningOp<ctjs::ConstructOp>();
    auto builtin = map ? map.getCallee().getDefiningOp<ctjs::LoadGlobalOp>() : ctjs::LoadGlobalOp{};
    auto closure = owner.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
    auto function = owner->getParentOfType<ctjs::FuncOp>();
    if (!map || !builtin || builtin.getName() != "Map" || !map.getArgs().empty() ||
        map.getNewTarget() != map.getCallee() || !closure ||
        !constructorClosures.contains(closure) || !function ||
        owner->getBlock() != &function.getBody().front() || map->getBlock() != owner->getBlock() ||
        selected->getBlock() != owner->getBlock()) {
        return false;
    }
    llvm::DenseSet<mlir::Operation *> calls;
    // ponytail: direct entry-block operations only; following the Map through
    // branches or captures needs a broader owner proof. The work budget bounds
    // each call, since the census asks repeatedly.
    unsigned work = 8192;
    for (mlir::OpOperand & mapUse : map.getResult().getUses()) {
        if (work-- == 0) { return false; }
        auto * operation = mapUse.getOwner();
        if (operation->getBlock() != map->getBlock()) { return false; }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation)) {
            auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (mapUse.getOperandNumber() != 1 || !method ||
                method.getObject() != map.getResult() ||
                ctjs::constantKey(method.getKey()) == "size") {
                return false;
            }
            continue;
        }
        auto method = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
        if (!method || mapUse.getOperandNumber() != 0) { return false; }
        auto key = ctjs::constantKey(method.getKey());
        if (key == "size") { continue; }
        const auto arity = llvm::StringSwitch<std::optional<unsigned>>(key)
                               .Case("set", 2)
                               .Case("clear", 0)
                               .Case("get", 1)
                               .Case("has", 1)
                               .Case("delete", 1)
                               .Default(std::nullopt);
        if (!arity) { return false; }
        for (mlir::OpOperand & methodUse : method.getResult().getUses()) {
            if (work-- == 0) { return false; }
            auto call = llvm::dyn_cast<ctjs::CallOp>(methodUse.getOwner());
            if (!call || methodUse.getOperandNumber() != 0 ||
                call.getReceiver() != map.getResult() || call->getBlock() != map->getBlock() ||
                call.getArgs().size() != *arity ||
                (key == "set" && !call.getResult().use_empty())) {
                return false;
            }
            calls.insert(call);
        }
    }
    if (!calls.contains(selected)) { return false; }
    auto method = selected.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
    if (ctjs::constantKey(method.getKey()) != "set") { return false; }
    llvm::StringSet<> selectors{"__proto__", "constructor", "prototype"};
    if (auto prototype = immutablePrototype(closure)) {
        for (ctjs::SetPropertyOp field : prototype->fields) {
            if (field.getValue().getDefiningOp<ctjs::CreateClosureOp>()) {
                selectors.insert(ctjs::constantKey(field.getKey()));
            }
        }
    }
    llvm::StringMap<mlir::Value> entries;
    llvm::DenseMap<mlir::Value, mlir::Value> aliases;
    for (mlir::Operation & operation : *map->getBlock()) {
        if (work-- == 0) { return false; }
        auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
        if (!call || !calls.contains(call)) { continue; }
        auto action = ctjs::methodKey(call);
        if (action == "clear") {
            entries.clear();
            continue;
        }
        auto key = call.getArgs().front().getDefiningOp<ctjs::ConstantOp>();
        auto text = key ? llvm::dyn_cast<ctjs::StringAttr>(key.getValue()) : ctjs::StringAttr{};
        if (!text) { return false; }
        if (action == "set") {
            auto record = call.getArgs()[1].getDefiningOp<ctjs::ConstructOp>();
            if (!record || record->getBlock() != map->getBlock() ||
                !record->isBeforeInBlock(call) || record.getCallee() != owner.getCallee() ||
                record.getNewTarget() != owner.getCallee()) {
                return false;
            }
            for (auto * user : record.getResult().getUsers()) {
                if (work-- == 0) { return false; }
                auto field = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                if (field && field.getObject() == record.getResult() &&
                    field.getValue().getDefiningOp<ctjs::CreateClosureOp>()) {
                    selectors.insert(ctjs::constantKey(field.getKey()));
                }
            }
            entries[text.getValue()] = record.getResult();
        } else if (action == "delete") {
            entries.erase(text.getValue());
        } else if (action == "get") {
            auto stored = entries.lookup(text.getValue());
            if (!stored) { return false; }
            aliases[call.getResult()] = stored;
        }
    }
    for (const auto & [alias, origin] : aliases) {
        (void)origin;
        const auto methodCall = [&](ctjs::GetPropertyOp get) {
            if (!get || get.getObject() != alias || !get.getResult().hasOneUse()) {
                return ctjs::CallOp{};
            }
            auto call = llvm::dyn_cast<ctjs::CallOp>(*get.getResult().getUsers().begin());
            return call && call.getCallee() == get.getResult() && call.getReceiver() == alias &&
                           call->getBlock() == map->getBlock()
                       ? call
                       : ctjs::CallOp{};
        };
        for (mlir::OpOperand & aliasUse : alias.getUses()) {
            auto * user = aliasUse.getOwner();
            // A field read in a nested block borrows the same entry-frame
            // owner: SSA dominance guarantees the owner is live, and final
            // admission proves the field's carrier. Writes, method calls and
            // Map operations must stay in the entry block.
            if (work-- == 0 || user->getParentOfType<ctjs::FuncOp>() != function ||
                (user->getBlock() != map->getBlock() && !llvm::isa<ctjs::GetPropertyOp>(user))) {
                return false;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(aliasUse.getOwner());
                call && aliasUse.getOperandNumber() == 1 &&
                methodCall(call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()) == call) {
                continue;
            }
            if (aliasUse.getOperandNumber() != 0) { return false; }
            mlir::Value key;
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(aliasUse.getOwner())) {
                key = get.getKey();
            } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(aliasUse.getOwner())) {
                key = set.getKey();
            }
            if (!key || !ctjs::ordinaryKey(ctjs::constantKey(key))) { return false; }
            // The exact origins feed methodCensus, which still proves each
            // method's target and receiver uses. Here a method key may only be
            // called on this alias, never stored or read off it.
            if (selectors.contains(ctjs::constantKey(key)) &&
                !methodCall(llvm::dyn_cast<ctjs::GetPropertyOp>(aliasUse.getOwner()))) {
                return false;
            }
        }
    }
    if (reads) { reads->insert(aliases.begin(), aliases.end()); }
    return true;
}

/// The use-list half of condition 1, independent of what made the value.
/// closedAfterLift asks it of a literal; the constructor lift asks it of a
/// ctjs.construct result, whose use list is the same after the rewrite turns
/// it into a literal. One shared walk keeps the two from drifting.
bool closureLifter::usesCloseTheShape(mlir::Value object) {
    for (mlir::OpOperand & use : object.getUses()) {
        if (isConstantKeyAccess(use)) { continue; }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner())) {
            if (retainedByLocalMap(use)) { continue; }
            // The object as the receiver of a call whose callee is a
            // constant-key read of that same object: a method call.
            if (use.getOperandNumber() == 1) {
                // A borrowed parameter has no method-resolution proof.
                if (llvm::isa<mlir::BlockArgument>(object)) { return false; }
                if (!methodCallOn(call, object)) { return false; }
                continue;
            }
        }
        // The object as an argument whose parameter will receive a
        // `ctn_x *`. argumentCensus decided that before any rewrite, so this
        // is a map lookup, not a second proof. A ctjs.call_direct's
        // arguments start at operand 3, a ctjs.call's at 2; both are handled,
        // or a literal passed to an already-direct call would read as open.
        if (carriesObjectArgument(use)) { continue; }
        return false;
    }
    return true;
}

/// True when `call`'s callee is a constant-key property read of `object`.
bool closureLifter::methodCallOn(ctjs::CallOp call, mlir::Value object) {
    auto load = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
    return load && load.getObject() == object && !ctjs::constantKey(load.getKey()).empty();
}

/// True when `use` passes an object to a parameter that argumentCensus
/// proved can take a pointer. A ctjs.call's arguments start at operand 2, a
/// ctjs.call_direct's at 3.
bool closureLifter::carriesObjectArgument(mlir::OpOperand & use) {
    mlir::Operation * call = use.getOwner();
    const unsigned first = llvm::isa<ctjs::CallDirectOp>(call) ? 3 : 2;
    if (!llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(call) || use.getOperandNumber() < first) {
        return false;
    }
    auto * made = objectArgumentCallee(call);
    return made && slotCarriesAnObject(made, use.getOperandNumber() - first);
}

/// Condition 2: the one function a method call reaches, or nullopt. Every
/// literal the receiver may name must bind the key exactly once, and all of
/// them must name the same ctjs.func, since one target is one C++ signature.
std::optional<closureLifter::methodCall> closureLifter::resolveMethod(ctjs::CallOp call) {
    auto load = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
    if (!load || !load.getResult().hasOneUse()) { return std::nullopt; }
    const mlir::Value receiver = load.getObject();
    if (receiver != call.getReceiver()) { return std::nullopt; }
    const llvm::StringRef key = ctjs::constantKey(load.getKey());
    if (key.empty()) { return std::nullopt; }
    const auto objects = behind.find(receiver);
    if (objects == behind.end() || objects->second.empty()) { return std::nullopt; }
    ctjs::FuncOp target;
    for (mlir::Value object : objects->second) {
        const auto fields = methodsOf.find(object);
        if (fields == methodsOf.end()) { return std::nullopt; }
        const auto field = fields->second.find(key);
        if (field == fields->second.end()) { return std::nullopt; }
        ctjs::FuncOp named = targetOf(field->second.closure);
        if (!named || (target && named != target)) { return std::nullopt; }
        target = named;
    }
    return methodCall{call, load, receiver, target};
}

/// Why a closure stored into an object is not a method field, as a
/// refusal sentence.
std::string closureLifter::whyNotAMethodField(ctjs::SetPropertyOp set) {
    const mlir::Value object = set.getObject();
    const llvm::StringRef key = ctjs::constantKey(set.getKey());
    if (!object.getDefiningOp<ctjs::CreateObjectOp>()) {
        return "it is stored into something that is not an object literal made here - "
               "Phase 59 slice 2";
    }
    if (key.empty()) {
        return "it is stored into an object under a key that is not a constant, so which "
               "field holds it is not known here";
    }
    if (!closedAfterLift(object)) {
        return ("it is a method field of an object whose shape is not closed, so `" + key +
                "` cannot become a free function taking that object")
            .str();
    }
    const auto fields = methodsOf.find(object);
    if (fields == methodsOf.end() || fields->second.find(key) == fields->second.end()) {
        return ("the field `" + key +
                "` is written more than once, so which function a call through it reaches "
                "depends on which store ran")
            .str();
    }
    return "it is stored into an object or an array - Phase 59 slice 2";
}

/// True when every use of `v` is a constant-key get or set. Used only to
/// choose a diagnostic: forwarding is proved by argumentCensus's fixpoint, and
/// this check neither authorizes it nor resolves borrowed methods.
bool closureLifter::onlyConstantKeyAccess(mlir::Value v) {
    return llvm::all_of(v.getUses(),
                        [](mlir::OpOperand & use) { return isConstantKeyAccess(use); });
}

/// Every call site of `c`'s target, or an empty list when any use cannot be
/// matched to a call. Includes direct calls found by symbol, not only uses of
/// the closure value: bindLocalFunctions can remove the closure value from
/// calls in another function.
llvm::SmallVector<closureLifter::closureCall> closureLifter::objectArgumentCalls(
    ctjs::CreateClosureOp c) {
    ctjs::FuncOp target = targetOf(c);
    if (!target || !completeObjectArgumentSymbols) { return {}; }
    llvm::SmallVector<closureCall> calls;
    for (mlir::OpOperand & use : c.getResult().getUses()) {
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(use.getOwner());
            store && objectArgumentDeclarations.lookup(store.getName()) == c) {
            continue;
        }
        const auto site = callSiteOf(use, target);
        if (!site) { return {}; }
        calls.push_back(site);
    }
    for (mlir::Operation * user : objectArgumentSymbolUsers.lookup(target.getSymName())) {
        auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user);
        if (!direct || direct.getTarget() != target || closureCalledBy(direct) != c) { return {}; }
        if (direct.getCalleeValue() != c.getResult()) {
            calls.push_back({direct, direct.getArgs(), direct.getReceiver()});
        }
    }
    return calls;
}

/// Whether JavaScript parameter `j` of `c`'s target may carry an object:
/// every call passes an argument there and the parameter is read. This
/// callable and arity check is fixed; object uses and origins are checked by
/// argumentCensus's shrinking fixpoint.
bool closureLifter::slotIsACandidate(ctjs::CreateClosureOp c, unsigned j) {
    ctjs::FuncOp target = targetOf(c);
    if (!target || target.getBody().empty()) { return false; }
    mlir::Block & entry = target.getBody().front();
    if (3 + j >= entry.getNumArguments()) { return false; }
    const auto calls = objectArgumentCalls(c);
    for (const closureCall & site : calls) {
        // A call that omits the argument disqualifies the slot: the lift pads
        // a missing argument with `undefined`, which is not an object, and
        // the fixpoint would index past the end of the argument list.
        if (j >= site.args.size()) { return false; }
    }
    const mlir::Value parameter = entry.getArgument(3 + j);
    return !calls.empty() && !parameter.use_empty();
}

/// Whether JavaScript parameter `j` of this callable's target will receive a
/// pointer. Read by closedAfterLift, so it answers from objectSlotsOf (the
/// fixpoint's result) and never recomputes.
bool closureLifter::slotCarriesAnObject(mlir::Operation * callable, unsigned j) const {
    const auto at = objectSlotsOf.find(callable);
    return at != objectSlotsOf.end() && llvm::is_contained(at->second, j);
}

/// The closure, or the private directly-called function, whose object slots
/// apply to `call`; null when there is none.
mlir::Operation * closureLifter::objectArgumentCallee(mlir::Operation * call) {
    if (auto closure = closureCalledBy(call)) { return closure; }
    auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(call);
    auto function = direct ? direct.getTarget() : ctjs::FuncOp{};
    return function && directObjectArgumentFunctions.contains(function) ? function.getOperation()
                                                                        : nullptr;
}

/// Decides which JavaScript parameters of each closure (and of each private
/// function only called directly) receive a pointer to a closed-shape object
/// (objectSlotsOf), tags the functions and calls with ctnative.object_args,
/// and records ctnative.object_reason on object literals passed elsewhere.
void closureLifter::argumentCensus() {
    for (ctjs::CreateClosureOp c : closures) {
        auto target = targetOf(c);
        if (!target || target.getUpvalueCount() != 0 || !isDeclarationClosure(c) ||
            uniqueClosureByTarget.lookup(target) != c) {
            continue;
        }
        auto store = llvm::cast<ctjs::StoreGlobalOp>(*c.getResult().getUsers().begin());
        if (closedDeclaration(store, target, module)) {
            objectArgumentDeclarations[store.getName()] = c;
        }
    }
    // Index every symbol use once up front: rescanning the module per
    // parameter and per fixpoint round took minutes on large bundles (e.g.
    // Phaser) instead of seconds.
    if (const auto symbols = mlir::SymbolTable::getSymbolUses(&module.getBodyRegion())) {
        completeObjectArgumentSymbols = true;
        for (const auto & use : *symbols) {
            auto name = llvm::dyn_cast<mlir::FlatSymbolRefAttr>(use.getSymbolRef());
            if (!name) {
                completeObjectArgumentSymbols = false;
                break;
            }
            objectArgumentSymbolUsers[name.getValue()].push_back(use.getUser());
        }
    }
    for (ctjs::CreateClosureOp c : closures) {
        // Method fields need no clause here: their closure is stored, not
        // called, which condition 1 already refuses. (methodClosures would be
        // empty anyway, since this census runs before methodCensus.)
        ctjs::FuncOp target = targetOf(c);
        if (!target || target.getBody().empty()) { continue; }
        const unsigned parameters =
            target.getBody().front().getNumArguments() - ctjs::implicit_arguments;
        llvm::SmallVector<unsigned, 2> slots;
        for (unsigned j = 0; j < parameters; ++j) {
            if (slotIsACandidate(c, j)) { slots.push_back(j); }
        }
        if (!slots.empty()) { objectSlotsOf[c.getOperation()] = std::move(slots); }
    }
    // Class preparation can remove an unused helper closure but keep its
    // private function. Such a function qualifies only if every current
    // symbol use is a plain direct call; attributes from an earlier proof, or
    // one favorable call, do not establish a carrier.
    llvm::DenseSet<mlir::Operation *> closureTargets;
    for (ctjs::CreateClosureOp closure : closures) { closureTargets.insert(targetOf(closure)); }
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<closureCall>> directCalls;
    const auto moduleUses = mlir::SymbolTable::getSymbolUses(module.getOperation());
    llvm::DenseSet<mlir::Operation *> moduleReferences;
    if (moduleUses) {
        for (const auto & use : *moduleUses) {
            if (auto function = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                    use.getUser(), use.getSymbolRef())) {
                moduleReferences.insert(function);
            }
        }
    }
    if (completeObjectArgumentSymbols && moduleUses) {
        for (ctjs::FuncOp function : module.getOps<ctjs::FuncOp>()) {
            if (!function.isPrivate() || function.getUpvalueCount() != 0 ||
                closureTargets.contains(function) || moduleReferences.contains(function) ||
                function.getBody().empty()) {
                continue;
            }
            auto & body = function.getBody().front();
            if (body.getNumArguments() < ctjs::implicit_arguments ||
                llvm::any_of(body.getArguments().take_front(ctjs::implicit_arguments),
                             [](mlir::BlockArgument argument) { return !argument.use_empty(); })) {
                continue;
            }
            llvm::SmallVector<closureCall> sites;
            bool closed = true;
            for (auto * user : objectArgumentSymbolUsers.lookup(function.getSymName())) {
                auto call = llvm::dyn_cast<ctjs::CallDirectOp>(user);
                if (!call || call.getTarget() != function ||
                    !ctjs::isUndefinedConstant(call.getCalleeValue()) ||
                    !ctjs::isUndefinedConstant(call.getReceiver()) ||
                    !ctjs::isUndefinedConstant(call.getNewTarget()) ||
                    call.getArgs().size() + ctjs::implicit_arguments != body.getNumArguments()) {
                    closed = false;
                    break;
                }
                sites.push_back({call, call.getArgs(), call.getReceiver()});
            }
            if (!closed || sites.empty()) { continue; }
            directObjectArgumentFunctions.insert(function);
            directCalls[function] = std::move(sites);
            auto & slots = objectSlotsOf[function];
            for (auto argument : body.getArguments().drop_front(ctjs::implicit_arguments)) {
                if (!argument.use_empty()) {
                    slots.push_back(argument.getArgNumber() - ctjs::implicit_arguments);
                }
            }
        }
    }
    const auto closedArgument = [&](mlir::Value value) {
        if (closedAfterLift(value)) { return true; }
        auto parameter = llvm::dyn_cast<mlir::BlockArgument>(value);
        if (!parameter || !parameter.getOwner()->isEntryBlock() ||
            (parameter.getArgNumber() < ctjs::implicit_arguments &&
             parameter.getArgNumber() != ctjs::arg_receiver)) {
            return false;
        }
        auto owner = llvm::dyn_cast<ctjs::FuncOp>(parameter.getOwner()->getParentOp());
        if (parameter.getArgNumber() >= ctjs::implicit_arguments &&
            directObjectArgumentFunctions.contains(owner)) {
            return slotCarriesAnObject(owner,
                                       parameter.getArgNumber() - ctjs::implicit_arguments) &&
                   usesCloseTheShape(value);
        }
        bool found = false;
        for (ctjs::CreateClosureOp made : closures) {
            if (targetOf(made) != owner) { continue; }
            // For a receiver, the constructor setup proof covers every origin
            // before method resolution; final admission still proves the
            // whole receiver and its methods.
            if (parameter.getArgNumber() == ctjs::arg_receiver
                    ? !constructorClosures.contains(made) || whyConstructorSetupDoesNotLift(made)
                    : !slotCarriesAnObject(made,
                                           parameter.getArgNumber() - ctjs::implicit_arguments)) {
                return false;
            }
            found = true;
        }
        return found && usesCloseTheShape(value);
    };
    // A shrinking fixpoint. A slot whose literal turns out to be open is
    // dropped, which can open another literal that relied on it, so repeat
    // until nothing changes. It terminates because objectSlotsOf never grows
    // here.
    llvm::SmallVector<mlir::Operation *> candidates;
    for (ctjs::CreateClosureOp closure : closures) { candidates.push_back(closure); }
    for (const auto & [function, sites] : directCalls) {
        (void)sites;
        candidates.push_back(function);
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (mlir::Operation * callable : candidates) {
            const auto at = objectSlotsOf.find(callable);
            if (at == objectSlotsOf.end()) { continue; }
            auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(callable);
            auto function = closure ? targetOf(closure) : llvm::cast<ctjs::FuncOp>(callable);
            const auto calls =
                closure ? objectArgumentCalls(closure) : directCalls.lookup(callable);
            llvm::SmallVector<unsigned, 2> kept;
            for (unsigned j : at->second) {
                const auto parameter = function.getBody().front().getArgument(3 + j);
                bool ok = !calls.empty() && usesCloseTheShape(parameter);
                for (const closureCall & site : calls) {
                    if (j >= site.args.size() || !closedArgument(site.args[j])) { ok = false; }
                }
                if (ok) { kept.push_back(j); }
            }
            if (kept.size() == at->second.size()) { continue; }
            changed = true;
            if (kept.empty()) {
                objectSlotsOf.erase(callable);
            } else {
                objectSlotsOf[callable] = std::move(kept);
            }
        }
    }
    // Copies `callable`'s object slots onto `function` and every site
    // `sitesOf` lists.
    const auto publish = [&](mlir::Operation * callable, mlir::Operation * function,
                             const auto & sitesOf) {
        llvm::SmallVector<int32_t> indices;
        for (unsigned slot : objectSlotsOf.lookup(callable)) {
            indices.push_back(static_cast<int32_t>(ctjs::implicit_arguments + slot));
        }
        if (indices.empty()) { return; }
        auto attribute = mlir::Builder(context).getDenseI32ArrayAttr(indices);
        function->setAttr(kNativeObjectArgs, attribute);
        for (const auto & site : sitesOf()) { site.op->setAttr(kNativeObjectArgs, attribute); }
    };
    for (const auto & [function, sites] : directCalls) {
        publish(function, function, [&]() -> const auto & { return sites; });
    }
    // Declarations are already direct and need no closure rewrite; give their
    // proved object parameters the same attribute as lifted local helpers.
    for (const auto & declaration : objectArgumentDeclarations) {
        auto made = declaration.second;
        publish(made, targetOf(made), [&] { return objectArgumentCalls(made); });
    }
    // Record the reason on each object literal argument this rule did not
    // take. It is decided here rather than in admission::whyOpen because
    // every failing condition is a property of the callee (which parameter,
    // read how, called from where), which the use-list walk that meets the
    // escape cannot see. Same idiom as ctnative.closure_reason and
    // ctnative.cell_reason.
    llvm::SmallVector<mlir::Operation *> sites;
    for (ctjs::CallOp call : allCalls) { sites.push_back(call.getOperation()); }
    for (ctjs::CallDirectOp direct : allDirectCalls) { sites.push_back(direct.getOperation()); }
    for (mlir::Operation * site : sites) {
        ctjs::CreateClosureOp made = closureCalledBy(site);
        for (auto [j, argument] : llvm::enumerate(argsOfCallSite(site))) {
            mlir::Operation * literal = argument.getDefiningOp();
            if (!llvm::isa_and_nonnull<ctjs::CreateObjectOp>(literal)) { continue; }
            if (made && slotCarriesAnObject(made, static_cast<unsigned>(j))) { continue; }
            literal->setAttr(kNativeObjectReason,
                             mlir::StringAttr::get(
                                 context, whyNotAnObjectArgument(site, static_cast<unsigned>(j))));
        }
    }
}

/// Why the object passed as argument `j` of `call` cannot become a pointer
/// parameter, one sentence per condition. Asked only where the object really
/// is an argument, so "it is passed to a call" is never the whole answer.
std::string closureLifter::whyNotAnObjectArgument(mlir::Operation * call, unsigned j) {
    ctjs::CreateClosureOp made = closureCalledBy(call);
    if (!made) {
        return "it is passed to a call whose callee is not one function this rewrite can "
               "name, so there is no parameter to give the object's address to";
    }
    // No method-field clause: methodClosures is still empty here (this runs
    // before methodCensus), and a closure stored into a literal is already
    // refused for its non-call use.
    ctjs::FuncOp target = targetOf(made);
    if (!target || target.getBody().empty() ||
        3 + j >= target.getBody().front().getNumArguments()) {
        return "it is passed in an argument position the callee has no parameter for - the "
               "surplus has frame semantics";
    }
    const mlir::Value parameter = target.getBody().front().getArgument(3 + j);
    if (parameter.use_empty()) {
        return "it is passed to a parameter nothing reads, and an object parameter that is "
               "never read is `-Wunused-parameter` in the generated C++";
    }
    if (!onlyConstantKeyAccess(parameter)) {
        return "it is passed to a parameter that reaches it through something other than a "
               "constant key - that needs an owner, and this slice introduces none";
    }
    for (mlir::OpOperand & use : made.getResult().getUses()) {
        const closureCall other = callSiteOf(use, target);
        // A use that is not a call is already refused (and reported first) by
        // whyNotLiftable, so it is skipped here.
        if (!other) { continue; }
        const mlir::ValueRange args = other.args;
        if (j >= args.size() || !args[j].getDefiningOp<ctjs::CreateObjectOp>()) {
            return "it is passed to a parameter that is an object literal at this call and "
                   "something else at another, so the parameter has no single C++ type";
        }
    }
    return "it is passed to a parameter whose other object literal is not itself a closed "
           "shape, so the two would not agree on one class";
}

/// Finds method fields (closures stored under a constant key that is written
/// exactly once) on closed literals and constructed instances, resolves the
/// receivers each method's `this` may name, and records the method calls
/// that resolve (callsOfTarget) and the closures used as methods
/// (methodClosures).
void closureLifter::methodCensus() {
    llvm::DenseSet<mlir::Operation *> instanceStores;
    for (ctjs::ConstructOp built : allConstructs) {
        if (!closedAfterLift(built.getResult())) { continue; }
        for (mlir::Operation * user : built.getResult().getUsers()) {
            auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
            if (!set || set.getObject() != built.getResult() ||
                !set.getValue().getDefiningOp<ctjs::CreateClosureOp>() ||
                set->getBlock() != built->getBlock()) {
                continue;
            }
            // Every method store must precede every read or call of the
            // instance. This also excludes argument aliases, whose writes
            // need a separate proof.
            const bool initialized =
                llvm::all_of(built.getResult().getUses(), [&](mlir::OpOperand & use) {
                    auto * op = use.getOwner();
                    if (use.getOperandNumber() == 0 && llvm::isa<ctjs::SetPropertyOp>(op)) {
                        return true;
                    }
                    return op->getBlock() == set->getBlock() && set->isBeforeInBlock(op) &&
                           ((use.getOperandNumber() == 0 && llvm::isa<ctjs::GetPropertyOp>(op)) ||
                            (use.getOperandNumber() == 1 && llvm::isa<ctjs::CallOp>(op)));
                });
            if (initialized) { instanceStores.insert(set); }
        }
    }
    // True when no write can replace `key` on `receiver`: every set_property
    // that may reach it has a constant key, and writes `key` only through a
    // store `owned` accepts. Stores to other fresh literals cannot reach the
    // receiver and are skipped; any other object, including every parameter,
    // may alias it and counts.
    const auto stableKey = [&](mlir::Value receiver, llvm::StringRef key, const auto & owned) {
        bool stable = true;
        module.walk([&](ctjs::SetPropertyOp set) {
            if (set.getObject().getDefiningOp<ctjs::CreateObjectOp>() &&
                set.getObject() != receiver) {
                return;
            }
            const auto written = ctjs::constantKey(set.getKey());
            if (written.empty() || (written == key && !owned(set))) { stable = false; }
        });
        return stable;
    };
    const auto collect = [&](mlir::Value object, bool constructed) {
        if (!closedAfterLift(object)) { return; }
        llvm::StringMap<unsigned> writes;
        llvm::StringMap<methodField> fields;
        for (mlir::Operation * user : object.getUsers()) {
            auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
            if (!set || set.getObject() != object) { continue; }
            ++writes[ctjs::constantKey(set.getKey())];
            if (auto made = set.getValue().getDefiningOp<ctjs::CreateClosureOp>()) {
                if (constructed && !instanceStores.contains(set)) { continue; }
                fields[ctjs::constantKey(set.getKey())] = methodField{set, made};
            }
        }
        llvm::StringMap<methodField> & into = methodsOf[object];
        for (const auto & entry : fields) {
            // A key written twice is not a method, whatever the second write
            // holds: `o.f = g` after `var o = {f: h}` makes the callee depend
            // on which store ran, which condition 2 forbids. The call through
            // it stays a ctjs.call and is refused by name later.
            if (writes[entry.first()] != 1) { continue; }
            if (!constructed || stableKey(object, entry.first(), [&](ctjs::SetPropertyOp set) {
                    return instanceStores.contains(set);
                })) {
                into[entry.first()] = entry.second;
            }
        }
        behind[object].push_back(object);
    };
    for (ctjs::CreateObjectOp object : objects) { collect(object.getResult(), false); }
    for (ctjs::ConstructOp built : allConstructs) { collect(built.getResult(), true); }
    // The constructor runs before any store to its result, so seed its
    // receiver from the proved prototype alone, never from those stores.
    // whyConstructorSetupDoesNotLift skips only receiver resolution; full
    // constructor admission rechecks that after this census settles.
    for (ctjs::ConstructOp built : allConstructs) {
        auto closure = built.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
        if (!closure || whyConstructorSetupDoesNotLift(closure)) { continue; }
        auto prototype = immutablePrototype(closure);
        if (!prototype) { continue; }
        mlir::Value origin = prototype->attachment.getValue();
        for (ctjs::SetPropertyOp field : prototype->fields) {
            auto method = field.getValue().getDefiningOp<ctjs::CreateClosureOp>();
            if (!method) { continue; }
            const auto key = ctjs::constantKey(field.getKey());
            // As for methods stored after construction, every write that may
            // reach the receiver counts.
            if (!stableKey(origin, key, [&](ctjs::SetPropertyOp set) { return set == field; })) {
                continue;
            }
            methodsOf[origin][key] = methodField{field, method};
            methodsOf[built.getResult()][key] = methodField{field, method};
        }
        behind[built.getResult()] = {built.getResult()};
        auto receiver = targetOf(closure).getBody().front().getArgument(ctjs::arg_receiver);
        if (!llvm::is_contained(behind[receiver], origin)) { behind[receiver].push_back(origin); }
    }
    // A record read back from a local Map keeps naming the record it got,
    // even after the entry is overwritten, deleted or cleared: the enclosing
    // entry frame owns it, independently of the Map.
    for (ctjs::ConstructOp built : allConstructs) {
        if (behind.lookup(built.getResult()).empty()) { continue; }
        llvm::DenseMap<mlir::Value, mlir::Value> reads;
        for (mlir::OpOperand & use : built.getResult().getUses()) {
            (void)retainedByLocalMap(use, &reads);
        }
        for (const auto & [read, owner] : reads) { behind[read] = {owner}; }
    }
    // Fixpoint over the receiver chain. `this.other()` inside a method has
    // `%arg0` as its receiver, and `%arg0` names whatever the call sites pass,
    // which is known only once those call sites resolve. One round per link
    // in the chain; it terminates because `behind` only grows and is bounded
    // by the literals in the module.
    for (bool changed = true; changed;) {
        changed = false;
        for (ctjs::CallOp call : allCalls) {
            auto resolved = resolveMethod(call);
            if (!resolved || resolved->target.getBody().empty() ||
                resolved->target.getBody().front().getNumArguments() < 3) {
                continue;
            }
            llvm::SmallVector<mlir::Value, 2> & named =
                behind[resolved->target.getBody().front().getArgument(0)];
            for (mlir::Value object : behind.lookup(resolved->receiver)) {
                if (!llvm::is_contained(named, object)) {
                    named.push_back(object);
                    changed = true;
                }
            }
        }
    }
    // Record the resolved calls once `behind` has stopped changing.
    for (ctjs::CallOp call : allCalls) {
        auto resolved = resolveMethod(call);
        if (!resolved || resolved->target.getBody().empty() ||
            resolved->target.getBody().front().getNumArguments() < 3) {
            continue;
        }
        callsOfTarget[resolved->target.getOperation()].push_back(*resolved);
    }
    for (const auto & entry : methodsOf) {
        for (const auto & field : entry.second) {
            ctjs::CreateClosureOp bound = field.second.closure;
            methodClosures.insert(bound.getOperation());
        }
    }
    // Run the census last: two of its labels ("is this call one the rewrite
    // resolves", "does that parameter escape") are settled only once `behind`
    // and `methodsOf` have stopped changing. Asking earlier misses the
    // chained receivers.
    if (censusOn) {
        for (ctjs::CreateObjectOp object : objects) {
            if (!closedAfterLift(object.getResult())) { censusOpenLiteral(object.getResult()); }
        }
    }
}

/// Condition 3: what the target does with `this`. Every use must be one the
/// receiver parameter can carry (a constant-key read or write, or the
/// receiver of another resolved method call), and each way out of the
/// function gets its own sentence, because "it leaks `this`" is not
/// actionable and "it is returned" is.
std::optional<std::string> closureLifter::whyThisLeaks(ctjs::FuncOp target) {
    mlir::Block & entry = target.getBody().front();
    const auto constructor = uniqueClosureByTarget.lookup(target);
    const bool constructed = constructor && constructorClosures.contains(constructor);
    for (mlir::OpOperand & use : entry.getArgument(0).getUses()) {
        mlir::Operation * user = use.getOwner();
        // Reuse the settled argument census: a constructor's receiver may be
        // passed on only through a proved object-argument slot.
        if ((constructed && carriesObjectArgument(use)) || isConstantKeyAccess(use) ||
            passesLexicalThis(use)) { // ArrowThis.cpp
            continue;
        }
        if (llvm::isa<ctjs::GetPropertyOp>(user)) {
            return "it reads `this` through a dynamic key";
        }
        if (llvm::isa<ctjs::SetPropertyOp>(user)) {
            if (use.getOperandNumber() == 2) {
                return "it stores `this` into another object - that needs an owner, and this "
                       "slice introduces none";
            }
            return "it writes `this` through a dynamic key";
        }
        if (llvm::isa<ctjs::ReturnOp>(user)) {
            return "it returns `this` - the receiver is the caller's frame, so returning it "
                   "would outlive the object";
        }
        if (llvm::isa<ctjs::StoreGlobalOp>(user)) { return "it stores `this` into a global"; }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
            // `this.other()`: the receiver of a call this rewrite also makes
            // direct. Any other position is `this` passed as an argument,
            // which has no call site to move it to.
            if (use.getOperandNumber() == 1 && resolveMethod(call)) { continue; }
            return "it passes `this` to a call this rewrite cannot make direct";
        }
        // `f(this)` is usually a ctjs.call_direct by now, because
        // --ctjs-resolve-globals already named the callee. Give it its own
        // sentence; the default one ("it reaches `ctjs.call_direct`") names
        // the operation, not the mistake.
        if (llvm::isa<ctjs::CallDirectOp>(user)) {
            return "it passes `this` as an argument to another function - a receiver moves "
                   "to the CALL SITE, and an argument position has none to move to (that "
                   "needs a specialised callee, Phase 63, not a lift)";
        }
        return ("it reaches `" + user->getName().getStringRef() +
                "`, which slice 1 does not carry a receiver through")
            .str();
    }
    return std::nullopt;
}

/// The method form of whyNotLiftable: conditions 1 to 4, one sentence each.
/// The capture and own-closure clauses are the closure lift's, unchanged,
/// because a method is a closure in the IR.
std::optional<std::string> closureLifter::whyNotLiftableMethod(ctjs::CreateClosureOp c) {
    // Check the target first, including the arrow guard: an arrow's `this`
    // is lexical, yet every use of it would pass condition 3 as a constant-key
    // access, and admitting one would rebind `this` to the object and compute
    // the wrong answer instead of refusing.
    if (const std::optional<std::string> why = whyTargetIsNotLiftable(c)) { return why; }
    ctjs::FuncOp target = targetOf(c);
    mlir::Block & entry = target.getBody().front();
    // Condition 2, the other half: the closure value is used for method
    // stores and nothing else. methodClosures says at least one store is a
    // method store; this checks every use is one.
    for (mlir::OpOperand & use : c.getResult().getUses()) {
        // A class method's `__home` edge is class setup (Classes.cpp).
        if (use.getOwner()->hasAttr(kNativeClassSetup)) { continue; }
        auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
        if (!set || use.getOperandNumber() != 2) {
            return "it is a method field that is also used as a value elsewhere - Phase 59 "
                   "slice 2";
        }
        const auto fields = methodsOf.find(set.getObject());
        if (fields == methodsOf.end() ||
            fields->second.lookup(ctjs::constantKey(set.getKey())).closure != c) {
            return whyNotAMethodField(set);
        }
    }
    // Condition 3.
    if (const std::optional<std::string> leak = whyThisLeaks(target)) { return leak; }
    // Every call of it must be one resolveMethod resolved. A method field
    // nothing calls has nowhere to move the receiver to, and an unresolved
    // call would be left dispatching through a closure that is about to lower
    // to nothing.
    const auto calls = callsOfTarget.find(target.getOperation());
    if (calls == callsOfTarget.end()) {
        // A method read as a value: `var g = o.m;` loads the field without
        // calling it, so there is no call site for the receiver to move to,
        // and the closure would have to become a bound function, which the
        // method lift does not build.
        for (mlir::OpOperand & use : c.getResult().getUses()) {
            auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
            if (!set) { continue; }
            const llvm::StringRef key = ctjs::constantKey(set.getKey());
            for (mlir::Operation * user : set.getObject().getUsers()) {
                auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                if (get && ctjs::constantKey(get.getKey()) == key) {
                    return ("its field `" + key +
                            "` is read as a value rather than called - a method used as a "
                            "function value has to carry its receiver, which is a bound "
                            "function and an owner this slice does not build")
                        .str();
                }
            }
        }
        return "nothing calls it";
    }
    const unsigned parameters = entry.getNumArguments() - ctjs::implicit_arguments;
    for (methodCall at : calls->second) {
        if (auto why =
                whyCallSiteDoesNotLift(at.call, at.call.getArgs().size(), parameters, "a call")) {
            return why;
        }
        // Condition 1 at the call site, not only at the literal: the receiver
        // is either a literal proved closed, or the `%arg0` of a method whose
        // own receivers are.
        if (behind.lookup(at.receiver).empty()) {
            return "it is called on a receiver whose shape is not a proved closed literal";
        }
    }
    // Condition 4: the target is otherwise native (the call-graph fixpoint's
    // question) and its captures lift the same way an ordinary closure's do.
    // Checked last so that a leaked `this` is reported as a leaked `this` and
    // not as whatever the enclosing frame boxed for it: `box.held = this`
    // inside a method captures `box`, and a hoisted `var` is always a written
    // cell, so checking captures first would report "capture 0 is a binding
    // that is reassigned" for a program whose real problem is the receiver.
    if (const std::optional<std::string> why = whyCapturesDoNotLift(c)) { return why; }
    // The captured values must also reach the sites this lift rewrites,
    // which for a method are the calls through the object, not the uses of
    // the closure value.
    return whyLiftTailFails(
        c, target, llvm::map_range(calls->second, [](const methodCall & at) -> mlir::Operation * {
            return at.call;
        }));
}

} // namespace ctcompile::ctnative::lowering_detail
