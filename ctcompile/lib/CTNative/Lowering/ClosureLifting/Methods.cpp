// ClosureLifting/Methods.cpp - native lowering implementation.
#include "ClosureLifter.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "llvm/ADT/StringMap.h"

namespace ctcompile::ctnative::lowering_detail {

// --- the receiver lift --------------------------------------------------

// CONDITION 1, AS IT WILL READ AFTER THE REWRITE. `hasClosedShape` refuses
// any use that is not a constant-key get or set, and a method call is two
// of those uses - the object as the call's RECEIVER, and the get_property
// that loads the method - so asking it before the rewrite would refuse
// every object with a method on it. This is the same question asked of the
// IR this rewrite is about to produce, where the load is gone and the
// receiver is a call_direct operand `hasClosedShape` now admits by name.
bool closureLifter::closedAfterLift(mlir::Value object) {
    if (!object.getDefiningOp<ctjs::CreateObjectOp>() && !makesAnInstance(object)) { return false; }
    return usesCloseTheShape(object);
}

// A `ctjs.construct` RESULT IS A LITERAL THAT HAS NOT HAPPENED YET.
//
// The constructor lift replaces the construct with an empty
// ctjs.create_object and a receiver call, so by the time anything reads a
// shape the instance IS an object literal. Every census here runs BEFORE
// that rewrite, though, so each would see a `ctjs.construct` and answer
// "not a literal" - which refuses the module for an instance that is
// merely passed to a lifted function. This is the same question asked of
// the IR the rewrite is about to produce, exactly as `closedAfterLift`
// itself is for a method call.
//
// Ask the complete constructor proof, not just the diagnostic census. Its
// shape check reads objectSlotsOf without recursing here; argumentCensus's
// shrinking fixpoint removes any slots whose constructor dependencies fail.
bool closureLifter::makesAnInstance(mlir::Value object) {
    auto built = object.getDefiningOp<ctjs::ConstructOp>();
    if (!built) { return false; }
    auto closure = built.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
    if (!closure || !constructorClosures.contains(closure.getOperation())) { return false; }
    return !whyNotLiftableConstructor(closure);
}

// This establishes record shape/lifetime conditional on standard Map identity.
// All Map operations survive the lift; prepareNativeMaps must independently
// establish that identity before native admission, after local constructors
// have become direct calls. Source or prior native annotations prove nothing.
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
    // ponytail: direct entry-block operations only; branch/capture transport
    // needs a broader owner proof. Bound repeated censuses independently.
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
        static const llvm::StringMap<unsigned> arities{{"set", 2}, {"clear", 0}};
        const auto foundArity = key.size() <= 5 ? arities.find(key) : arities.end();
        const unsigned arity = foundArity != arities.end() ? foundArity->second : 1U;
        if (key != "set" && key != "get" && key != "has" && key != "delete" && key != "clear") {
            return false;
        }
        for (mlir::OpOperand & methodUse : method.getResult().getUses()) {
            if (work-- == 0) { return false; }
            auto call = llvm::dyn_cast<ctjs::CallOp>(methodUse.getOwner());
            if (!call || methodUse.getOperandNumber() != 0 ||
                call.getReceiver() != map.getResult() || call->getBlock() != map->getBlock() ||
                call.getArgs().size() != arity || (key == "set" && !call.getResult().use_empty())) {
                return false;
            }
            calls.insert(call);
        }
    }
    if (!calls.contains(selected)) { return false; }
    auto method = selected.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
    if (ctjs::constantKey(method.getKey()) != "set") { return false; }
    llvm::StringSet<> selectors;
    selectors.insert("__proto__");
    selectors.insert("constructor");
    selectors.insert("prototype");
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
        auto action =
            ctjs::constantKey(call.getCallee().getDefiningOp<ctjs::GetPropertyOp>().getKey());
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
            if (work-- == 0 || aliasUse.getOwner()->getBlock() != map->getBlock()) { return false; }
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
            // Exact origins feed the existing immutable method census below.
            // A method selector may only be called on this alias, never stored
            // or detached. That census still proves its target and receiver uses.
            if (selectors.contains(ctjs::constantKey(key)) &&
                !methodCall(llvm::dyn_cast<ctjs::GetPropertyOp>(aliasUse.getOwner()))) {
                return false;
            }
        }
    }
    if (reads) { reads->insert(aliases.begin(), aliases.end()); }
    return true;
}

// THE USE-LIST HALF OF CONDITION 1, ASKED WITHOUT THE QUESTION OF WHAT MADE
// THE VALUE. `closedAfterLift` asks it of a literal. The constructor lift
// asks the identical question of a `ctjs.construct` result, because the
// rewrite turns that result INTO a literal and its use list does not move -
// so a second, drifting copy of this walk is exactly what is not wanted.
bool closureLifter::usesCloseTheShape(mlir::Value object) {
    for (mlir::OpOperand & use : object.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
            if (use.getOperandNumber() != 0 || ctjs::constantKey(get.getKey()).empty()) {
                return false;
            }
            continue;
        }
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            if (use.getOperandNumber() != 0 || ctjs::constantKey(set.getKey()).empty()) {
                return false;
            }
            continue;
        }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
            if (retainedByLocalMap(use)) { continue; }
            // The object as the RECEIVER of a call whose callee is a
            // constant-key read of that same object: a method call.
            if (use.getOperandNumber() == 1) {
                // Borrowed parameters still have no method-resolution proof.
                if (llvm::isa<mlir::BlockArgument>(object)) { return false; }
                auto load = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (!load || load.getObject() != object ||
                    ctjs::constantKey(load.getKey()).empty()) {
                    return false;
                }
                continue;
            }
            // AND THE OBJECT AS AN ARGUMENT, which is the same carrier one
            // operand along: a parameter this rewrite will hand a
            // `ctn_x *`. `argumentCensus` decided that before anything was
            // rewritten, so this is a map lookup and not a second proof.
            if (use.getOperandNumber() >= 2) {
                auto * made = objectArgumentCallee(call);
                if (made && slotCarriesAnObject(made, use.getOperandNumber() - 2)) { continue; }
            }
            return false;
        }
        // THE SAME TWO QUESTIONS AT THE POSITIONS call_direct PUTS THEM.
        // Its operands are the callee's entry block in order, so argument i
        // is operand 3 + i rather than 2 + i. Without this arm a literal
        // handed to a call the closed world had already named read as an
        // OPEN shape, and the object-argument lift lost it.
        if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user)) {
            if (use.getOperandNumber() >= 3) {
                auto * made = objectArgumentCallee(direct);
                if (made && slotCarriesAnObject(made, use.getOperandNumber() - 3)) { continue; }
            }
            return false;
        }
        return false;
    }
    return true;
}

// CONDITION 2: the one function a method call reaches, or null. Every
// literal the receiver may name must bind the key exactly once, and all of
// them must name the SAME ctjs.func - one target is one C++ signature.
ctjs::FuncOp closureLifter::resolveMethod(ctjs::CallOp call, mlir::Value & receiverOut,
                                          ctjs::GetPropertyOp & loadOut) {
    auto load = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
    if (!load || !load.getResult().hasOneUse()) { return {}; }
    const mlir::Value receiver = load.getObject();
    if (receiver != call.getReceiver()) { return {}; }
    const llvm::StringRef key = ctjs::constantKey(load.getKey());
    if (key.empty()) { return {}; }
    const auto objects = behind.find(receiver);
    if (objects == behind.end() || objects->second.empty()) { return {}; }
    ctjs::FuncOp target;
    for (mlir::Value object : objects->second) {
        const auto fields = methodsOf.find(object);
        if (fields == methodsOf.end()) { return {}; }
        const auto field = fields->second.find(key);
        if (field == fields->second.end()) { return {}; }
        ctjs::FuncOp named = targetOf(field->second.closure);
        if (!named || (target && named != target)) { return {}; }
        target = named;
    }
    receiverOut = receiver;
    loadOut = load;
    return target;
}

// WHY A CLOSURE STORED INTO AN OBJECT IS NOT A METHOD FIELD. Three routes,
// and only the third is "this is not a method table at all".
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

// Diagnostic distinction between plain field access and a use that needs
// another proof. Forwarding is proved by argumentCensus's shrinking graph;
// this state-free label does not authorize it or resolve borrowed methods.
bool closureLifter::onlyConstantKeyAccess(mlir::Value v) {
    for (mlir::OpOperand & use : v.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
            if (use.getOperandNumber() == 0 && !ctjs::constantKey(get.getKey()).empty()) {
                continue;
            }
        } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            if (use.getOperandNumber() == 0 && !ctjs::constantKey(set.getKey()).empty()) {
                continue;
            }
        }
        return false;
    }
    return true;
}

// Include every symbolic call, not only uses of the closure value. The local
// binding proof can erase that value from calls in another function.
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

// The callable and arity census does not move. All object use and origin
// dependencies are checked by the shrinking argumentCensus fixpoint.
bool closureLifter::slotIsACandidate(ctjs::CreateClosureOp c, unsigned j) {
    ctjs::FuncOp target = targetOf(c);
    if (!target || target.getBody().empty()) { return false; }
    mlir::Block & entry = target.getBody().front();
    if (3 + j >= entry.getNumArguments()) { return false; }
    const auto calls = objectArgumentCalls(c);
    for (const closureCall & site : calls) {
        // A SHORT CALL IS NOT A CANDIDATE, and this half is load-bearing
        // twice over: the lift pads a missing argument with `undefined`,
        // which is not an object, and the fixpoint below would index past
        // the end asking whether it is.
        if (j >= site.args.size()) { return false; }
    }
    const mlir::Value parameter = entry.getArgument(3 + j);
    return !calls.empty() && !parameter.use_empty();
}

// Is JS parameter `j` of this closure's target one this rewrite will hand a
// pointer? Read by `closedAfterLift`, so it must answer from the map and
// never recompute - the map IS the fixpoint's result.
bool closureLifter::slotCarriesAnObject(mlir::Operation * callable, unsigned j) const {
    const auto at = objectSlotsOf.find(callable);
    return at != objectSlotsOf.end() && llvm::is_contained(at->second, j);
}

mlir::Operation * closureLifter::objectArgumentCallee(mlir::Operation * call) {
    if (auto closure = closureCalledBy(call)) { return closure; }
    auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(call);
    auto function = direct ? direct.getTarget() : ctjs::FuncOp{};
    return function && directObjectArgumentFunctions.contains(function) ? function.getOperation()
                                                                        : nullptr;
}

void closureLifter::argumentCensus() {
    for (ctjs::CreateClosureOp c : closures) {
        auto target = targetOf(c);
        if (!target || target.getUpvalueCount() != 0 || !admissionIsDeclaration(c) ||
            uniqueClosureByTarget.lookup(target) != c) {
            continue;
        }
        auto store = llvm::cast<ctjs::StoreGlobalOp>(*c.getResult().getUsers().begin());
        if (closedDeclaration(store, target, module)) {
            objectArgumentDeclarations[store.getName()] = c;
        }
    }
    // Scanning the module once per parameter and fixpoint round made the
    // unchanged Phaser admission scan take minutes instead of seconds.
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
        // A METHOD FIELD IS NOT THIS RULE'S, AND NEEDS NO CLAUSE HERE:
        // its closure value is STORED rather than called, which condition
        // 1's "every use is a call at operand 0" already refuses. That is
        // load-bearing for the ORDER - this census runs before
        // `methodCensus`, so `methodClosures` is still empty - and a
        // clause reading it here would have been silently vacuous.
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
    // Class preparation can consume an inert helper closure while retaining its
    // private function. Prove all current symbol uses before borrowing parameters;
    // neither old proof annotations nor one favorable call establishes a carrier.
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
                    !isUndefinedConstant(call.getCalleeValue()) ||
                    !isUndefinedConstant(call.getReceiver()) ||
                    !isUndefinedConstant(call.getNewTarget()) ||
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
            // Constructor setup proves every origin before method resolution.
            // Final admission still proves its complete receiver and methods.
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
    // THE FIXPOINT, WHICH ONLY SHRINKS. A slot whose literal turns out to
    // be open is not a slot, and dropping it can open another literal that
    // was relying on it - so this repeats until nothing moves. It
    // terminates because `objectSlotsOf` never grows here.
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
    for (const auto & [function, sites] : directCalls) {
        llvm::SmallVector<int32_t> indices;
        for (unsigned slot : objectSlotsOf.lookup(function)) {
            indices.push_back(static_cast<int32_t>(ctjs::implicit_arguments + slot));
        }
        if (indices.empty()) { continue; }
        auto attribute = mlir::Builder(context).getDenseI32ArrayAttr(indices);
        function->setAttr("ctnative.object_args", attribute);
        for (const auto & site : sites) { site.op->setAttr("ctnative.object_args", attribute); }
    }
    // Declarations are already direct and need no closure rewrite. Give their
    // proved borrows the same caller/callee carrier as lifted local helpers.
    for (const auto & declaration : objectArgumentDeclarations) {
        auto made = declaration.second;
        llvm::SmallVector<int32_t> indices;
        for (unsigned slot : objectSlotsOf.lookup(made.getOperation())) {
            indices.push_back(static_cast<int32_t>(ctjs::implicit_arguments + slot));
        }
        if (indices.empty()) { continue; }
        auto attribute = mlir::Builder(context).getDenseI32ArrayAttr(indices);
        targetOf(made)->setAttr("ctnative.object_args", attribute);
        for (const auto & site : objectArgumentCalls(made)) {
            site.op->setAttr("ctnative.object_args", attribute);
        }
    }
    // AND THE REASON, ONTO THE LITERAL, for every object argument this
    // rule did NOT take. It is written here rather than worked out by
    // `admission::whyOpen` because every condition that can fail is a
    // property of the CALLEE - which parameter, read how, called from
    // where - and the use-list walk that meets the escape has none of it.
    // Same idiom as `ctnative.closure_reason` and `ctnative.cell_reason`.
    llvm::SmallVector<mlir::Operation *> sites;
    for (ctjs::CallOp call : allCalls) { sites.push_back(call.getOperation()); }
    for (ctjs::CallDirectOp direct : allDirectCalls) { sites.push_back(direct.getOperation()); }
    for (mlir::Operation * site : sites) {
        ctjs::CreateClosureOp made = closureCalledBy(site);
        for (auto [j, argument] : llvm::enumerate(argsOfCallSite(site))) {
            mlir::Operation * literal = argument.getDefiningOp();
            if (!llvm::isa_and_nonnull<ctjs::CreateObjectOp>(literal)) { continue; }
            if (made && slotCarriesAnObject(made, static_cast<unsigned>(j))) { continue; }
            literal->setAttr("ctnative.object_reason",
                             mlir::StringAttr::get(
                                 context, whyNotAnObjectArgument(site, static_cast<unsigned>(j))));
        }
    }
}

// WHY AN OBJECT PASSED TO A CALL IS NOT A PARAMETER, in one sentence per
// condition. Asked only where the object really is an argument, so "it is
// passed to a call" is never the answer on its own.
std::string closureLifter::whyNotAnObjectArgument(mlir::Operation * call, unsigned j) {
    ctjs::CreateClosureOp made = closureCalledBy(call);
    if (!made) {
        return "it is passed to a call whose callee is not one function this rewrite can "
               "name, so there is no parameter to give the object's address to";
    }
    // A METHOD-FIELD CLAUSE WAS HERE AND IS GONE, FOR TWO REASONS. It was
    // VACUOUS - this runs at the end of `argumentCensus`, which is before
    // `methodCensus`, so `methodClosures` is still empty - and it was
    // REDUNDANT: a closure stored into a literal has a use that is not a
    // call, which the loop at the bottom names better ("used as a value
    // elsewhere") than "it is a method field" would. Both were measured on
    // the same program.
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
        // A SENTENCE FOR "THE CLOSURE IS USED AS A VALUE ELSEWHERE" WAS
        // HERE AND IS GONE, BECAUSE NO PROGRAM REACHES IT. Every use of a
        // closure that is not a call of it is already refused by
        // `whyNotLiftable`, and that refusal lands on this same function
        // and is reported first: measured on `kept = take; take(o);`,
        // which says "a closure used as a value: it is stored to a global"
        // and never asks this question. The cast still needs an else.
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
            // Every method binding precedes every observation. This also
            // excludes argument aliases, whose writes need a separate proof.
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
            // A KEY WRITTEN TWICE IS NOT A METHOD, whatever the second
            // write holds: `o.f = g` after `var o = {f: h}` makes the
            // callee depend on which store ran, which is exactly what
            // condition 2 forbids. The key is simply not admitted, and the
            // call through it stays a ctjs.call - refused by name below.
            if (writes[entry.first()] != 1) { continue; }
            bool stable = true;
            if (constructed) {
                // Distinct fresh literals cannot be the constructed receiver.
                // Unknown aliases, including every formal, still count.
                module.walk([&](ctjs::SetPropertyOp set) {
                    if (set.getObject().getDefiningOp<ctjs::CreateObjectOp>() &&
                        set.getObject() != object) {
                        return;
                    }
                    const auto key = ctjs::constantKey(set.getKey());
                    if (key.empty() || (key == entry.first() && !instanceStores.contains(set))) {
                        stable = false;
                    }
                });
            }
            if (stable) { into[entry.first()] = entry.second; }
        }
        behind[object].push_back(object);
    };
    for (ctjs::CreateObjectOp object : objects) { collect(object.getResult(), false); }
    for (ctjs::ConstructOp built : allConstructs) { collect(built.getResult(), true); }
    // Constructor calls precede every store to the constructed result. Seed
    // their receiver from the proved prototype alone, never from those stores.
    // The structural check omits only receiver resolution; full constructor
    // admission rechecks that after this census reaches its fixpoint.
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
            bool stable = true;
            // As with post-construction methods, all receiver writes count.
            module.walk([&](ctjs::SetPropertyOp set) {
                if (set.getObject().getDefiningOp<ctjs::CreateObjectOp>() &&
                    set.getObject() != origin) {
                    return;
                }
                auto written = ctjs::constantKey(set.getKey());
                if (written.empty() || (written == key && set != field)) { stable = false; }
            });
            if (!stable) { continue; }
            methodsOf[origin][key] = methodField{field, method};
            methodsOf[built.getResult()][key] = methodField{field, method};
        }
        behind[built.getResult()] = {built.getResult()};
        auto receiver = targetOf(closure).getBody().front().getArgument(ctjs::arg_receiver);
        if (!llvm::is_contained(behind[receiver], origin)) { behind[receiver].push_back(origin); }
    }
    // A saved get keeps the exact record selected before overwrite/delete/clear.
    // The enclosing entry frame owns it, independently of the Map entry.
    for (ctjs::ConstructOp built : allConstructs) {
        if (behind.lookup(built.getResult()).empty()) { continue; }
        llvm::DenseMap<mlir::Value, mlir::Value> reads;
        for (mlir::OpOperand & use : built.getResult().getUses()) {
            (void)retainedByLocalMap(use, &reads);
        }
        for (const auto & [read, owner] : reads) { behind[read] = {owner}; }
    }
    // THE FIXPOINT OVER THE RECEIVER CHAIN. `this.other()` inside a method
    // has `%arg0` for a receiver, and `%arg0` names whatever the call sites
    // pass - which is only known once those call sites resolve. One round
    // per link in the chain, and it terminates because `behind` only grows
    // and is bounded by the literals in the module.
    for (bool changed = true; changed;) {
        changed = false;
        for (ctjs::CallOp call : allCalls) {
            mlir::Value receiver;
            ctjs::GetPropertyOp load;
            ctjs::FuncOp target = resolveMethod(call, receiver, load);
            if (!target || target.getBody().empty() ||
                target.getBody().front().getNumArguments() < 3) {
                continue;
            }
            llvm::SmallVector<mlir::Value, 2> & named =
                behind[target.getBody().front().getArgument(0)];
            for (mlir::Value object : behind.lookup(receiver)) {
                if (!llvm::is_contained(named, object)) {
                    named.push_back(object);
                    changed = true;
                }
            }
        }
    }
    // AND THE CALLS THEMSELVES, once `behind` has stopped moving.
    for (ctjs::CallOp call : allCalls) {
        mlir::Value receiver;
        ctjs::GetPropertyOp load;
        ctjs::FuncOp target = resolveMethod(call, receiver, load);
        if (!target || target.getBody().empty() || target.getBody().front().getNumArguments() < 3) {
            continue;
        }
        callsOfTarget[target.getOperation()].push_back(methodCall{call, load, receiver, target});
    }
    for (const auto & entry : methodsOf) {
        for (const auto & field : entry.second) {
            ctjs::CreateClosureOp bound = field.second.closure;
            methodClosures.insert(bound.getOperation());
        }
    }
    // THE CENSUS LAST, because two of its labels ask questions - "is this
    // call one the rewrite resolves", "does that parameter escape" - whose
    // answers are only settled once `behind` and `methodsOf` have stopped
    // moving. Asking during the first loop undercounted by exactly the
    // chained receivers.
    if (censusOn) {
        for (ctjs::CreateObjectOp object : objects) {
            if (!closedAfterLift(object.getResult())) { censusOpenLiteral(object.getResult()); }
        }
    }
}

// CONDITION 3: what the target does with `this`. Every use has to be
// something the receiver parameter can carry - a constant-key read or
// write, or the receiver of another method call - and every route out of
// the function is named, because "it leaks `this`" is not a work item and
// "it is returned" is.
std::optional<std::string> closureLifter::whyThisLeaks(ctjs::FuncOp target) {
    mlir::Block & entry = target.getBody().front();
    const auto constructor = uniqueClosureByTarget.lookup(target);
    const bool constructed = constructor && constructorClosures.contains(constructor);
    for (mlir::OpOperand & use : entry.getArgument(0).getUses()) {
        mlir::Operation * user = use.getOwner();
        if (constructed) {
            // Reuse the settled argument census: only an exact borrowed slot
            // transports this caller-owned receiver into another function.
            const bool direct = llvm::isa<ctjs::CallDirectOp>(user);
            if ((direct || llvm::isa<ctjs::CallOp>(user)) &&
                use.getOperandNumber() >= (direct ? 3u : 2u)) {
                auto * callee = objectArgumentCallee(user);
                if (callee &&
                    slotCarriesAnObject(callee, use.getOperandNumber() - (direct ? 3u : 2u))) {
                    continue;
                }
            }
        }
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
            if (use.getOperandNumber() == 0 && !ctjs::constantKey(get.getKey()).empty()) {
                continue;
            }
            return "it reads `this` through a dynamic key";
        }
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            if (use.getOperandNumber() == 0 && !ctjs::constantKey(set.getKey()).empty()) {
                continue;
            }
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
            // `this.other()`: the receiver of a call this rewrite also
            // makes direct. Any other position is `this` passed as an
            // argument, which has no call site to move it to.
            mlir::Value receiver;
            ctjs::GetPropertyOp load;
            if (use.getOperandNumber() == 1 && resolveMethod(call, receiver, load)) { continue; }
            return "it passes `this` to a call this rewrite cannot make direct";
        }
        // AND A CALL --ctjs-resolve-globals ALREADY MADE DIRECT, which is
        // what `f(this)` is by the time this rewrite runs: the closed world
        // named that callee long before, so the operand sits on a
        // ctjs.call_direct and not on a ctjs.call. Without this arm the
        // commonest way there is to leak a receiver got the default
        // sentence - "it reaches `ctjs.call_direct`" - which names the
        // operation and not the mistake.
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

// The method form of whyNotLiftable: conditions 1 to 4, one sentence each.
// The capture and own-closure clauses are the closure lift's, unchanged -
// a method IS a closure in the IR, and the two rules compose.
std::optional<std::string> closureLifter::whyNotLiftableMethod(ctjs::CreateClosureOp c) {
    // THE ARROW GUARD FIRST, and the rest of the target's validity with it:
    // an arrow's `this` is lexical, every use of it reads as a legal
    // constant-key access to condition 3, and admitting one would rebind
    // `this` to the object and answer wrongly rather than refuse.
    if (const std::optional<std::string> why = whyTargetIsNotLiftable(c)) { return why; }
    ctjs::FuncOp target = targetOf(c);
    mlir::Block & entry = target.getBody().front();
    // CONDITION 2, the other half: the closure value is used for method
    // stores and nothing else. `methodClosures` says at least one store is
    // one; this says none of them is anything else.
    for (mlir::OpOperand & use : c.getResult().getUses()) {
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
    // CONDITION 3.
    if (const std::optional<std::string> leak = whyThisLeaks(target)) { return leak; }
    // AND EVERY CALL OF IT IS ONE THIS RESOLVES. A method field nothing
    // calls has nowhere to move the receiver to, and a call the resolution
    // above could not name would be left dispatching through a closure
    // that is about to lower to nothing.
    const auto calls = callsOfTarget.find(target.getOperation());
    if (calls == callsOfTarget.end()) {
        // A METHOD READ AS A VALUE. `var g = o.m;` loads the field and does
        // not call it, so there is no call site for the receiver to move
        // to and the closure would have to become a value that carries one
        // - a bound function, which is an owner this slice does not build.
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
        if (at.call.getArgs().size() > parameters) {
            return "a call passes " + std::to_string(at.call.getArgs().size()) +
                   " argument(s) to " + std::to_string(parameters) +
                   " parameter(s) - the surplus has frame semantics";
        }
        auto caller = at.call->getParentOfType<ctjs::FuncOp>();
        if (caller && passesNewTarget.contains(caller.getOperation())) {
            return "a call of it sits in a function that passes new.target";
        }
        // CONDITION 1 AT THE CALL SITE, not only at the literal: the
        // receiver is either a literal this proved closed, or the `%arg0`
        // of a method whose own receivers are.
        if (behind.lookup(at.receiver).empty()) {
            return "it is called on a receiver whose shape is not a proved closed literal";
        }
    }
    // CONDITION 4: the target is otherwise native - which is the existing
    // call-graph fixpoint's question - and its captures lift as Phase 59
    // slice 1 already lifts them. LAST, so that a leaked `this` is reported
    // as a leaked `this` and not as whatever the enclosing frame happened
    // to box for it: `box.held = this` inside a method captures `box`, and
    // a hoisted `var` is always a cell that is written, so asking the
    // capture clause first answered "capture 0 is a binding that is
    // reassigned" for a program whose actual problem is the receiver.
    if (const std::optional<std::string> why = whyCapturesDoNotLift(c)) { return why; }
    // AND THE CAPTURED VALUES REACH THE SITES THIS LIFT IS ABOUT TO
    // REWRITE - which for a method are the calls through the object, not
    // the uses of the closure value.
    for (methodCall at : calls->second) {
        if (const std::optional<std::string> why = whyCapturesDoNotReach(c, at.call)) {
            return why;
        }
    }
    if (const std::optional<std::string> escapes = whyOwnClosureEscapes(target)) { return escapes; }
    return whyUpvalueReadsDoNotLift(c, target);
}

} // namespace ctcompile::ctnative::lowering_detail
