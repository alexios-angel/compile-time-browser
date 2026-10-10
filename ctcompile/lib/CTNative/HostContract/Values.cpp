//===- Values.cpp - Value resolvers of the host-contract analyzer ---------===//
//
// The value-tracking half of the host-contract analyzer: resolves which
// function a value calls, which object it denotes, which primitive or truth
// value it has, and which property write a read sees. Every query charges the
// shared step budget (`step`/`spend`) and recursion is capped at depth 64.
// Once the budget runs out, every answer is "unknown".
//
//===----------------------------------------------------------------------===//

#include "Values/Arguments.hpp"

#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/ScopeExit.h"

#include <algorithm>
#include <utility>

namespace ctcompile::ctnative::host_detail {

/// Indexes the module once: global stores by name, functions by index,
/// returns per function, direct-call callers, and indirect factory calls.
analyzer::analyzer(mlir::ModuleOp input, const HostContract & request, unsigned steps)
    : module(input), contract(request), remaining(steps), dominance(input) {
    entry = module.lookupSymbol<ctjs::FuncOp>(contract.entry);
    module.walk([&](ctjs::StoreGlobalOp store) {
        if (step()) { globals[store.getName()].push_back(store); }
    });
    module.walk([&](ctjs::FuncOp function) {
        if (!step()) { return; }
        if (auto index = functionIndex(function)) {
            ambiguousFunctions |= !functions.try_emplace(*index, function).second;
        }
        function.getBody().walk([&](ctjs::ReturnOp returned) {
            if (step()) { returns[function].push_back(returned); }
        });
    });
    module.walk([&](ctjs::CallDirectOp call) {
        if (!step()) { return; }
        if (auto function = target(call)) { callers[function].push_back(call); }
    });
    if (llvm::is_contained(contract.initialIntrinsics, "Map")) {
        // UMD-style modules call `factory()` through a parameter of a wrapper
        // function, which the importer emits as an indirect call. When the
        // entry function calls that wrapper exactly once, resolve the
        // parameter to its one callback and record the call as an edge to it.
        // The IR is not rewritten.
        module.walk([&](ctjs::CallOp call) {
            if (!step() || !call.getArgs().empty()) { return; }
            auto argument = llvm::dyn_cast<mlir::BlockArgument>(call.getCallee());
            if (!argument || argument.getArgNumber() < ctjs::implicit_arguments) { return; }
            auto wrapper = llvm::dyn_cast<ctjs::FuncOp>(argument.getOwner()->getParentOp());
            if (!wrapper || wrapper == entry || wrapper.getUpvalueCount() != 0 ||
                !llvm::hasSingleElement(wrapper.getBody()) ||
                argument.getOwner() != &wrapper.getBody().front() || callers[wrapper].size() != 1) {
                return;
            }
            auto * invocation = callers[wrapper].front();
            if (!llvm::isa<ctjs::CallDirectOp>(invocation) ||
                invocation->getParentOfType<ctjs::FuncOp>() != entry) {
                return;
            }
            auto function = callable(argument);
            if (!function || function == entry || function == wrapper ||
                function.getUpvalueCount() != 0 || !llvm::hasSingleElement(function.getBody()) ||
                function.getBody().front().getNumArguments() != 3) {
                return;
            }
            indirectFactories[call] = function;
            callers[function].push_back(call);
        });
    }
}

/// Charges one step; false once the budget is exhausted.
bool analyzer::step() {
    return spend(1);
}

/// Charges `count` steps. Running out sets `exhausted` and empties the
/// budget, so every later query also fails.
bool analyzer::spend(uint64_t count) {
    if (count > remaining) {
        exhausted = true;
        remaining = 0;
        return false;
    }
    remaining -= static_cast<unsigned>(count);
    return true;
}

/// True when every operand of `operation` dominates it.
bool analyzer::operandsDominate(mlir::Operation * operation) {
    for (mlir::Value operand : operation->getOperands()) {
        if (!step() || !dominance.dominates(operand, operation)) { return false; }
    }
    return true;
}

/// True when `use` is inside `function` and dominated by `value`.
bool analyzer::localUse(mlir::Value value, mlir::OpOperand & use, mlir::Operation * function) {
    return step() && use.getOwner()->getParentOfType<ctjs::FuncOp>().getOperation() == function &&
           dominance.dominates(value, use.getOwner());
}

/// True when `value` is used only by ctjs.root.
bool analyzer::rootedUsers(mlir::Value value) {
    for (mlir::Operation * user : value.getUsers()) {
        if (!step() || !llvm::isa<ctjs::RootOp>(user)) { return false; }
    }
    return true;
}

/// The function a direct call or a resolved indirect factory call invokes.
ctjs::FuncOp analyzer::target(mlir::Operation * operation) const {
    if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) { return call.getTarget(); }
    return indirectFactories.lookup(operation);
}

/// The one function `value` can be a closure of, traced through globals with
/// one store and through parameters whose every caller passes the same
/// function. Null when unknown. `depth` (as in the other resolvers below)
/// caps recursion at 64 on deep or cyclic IR; the step budget does the real
/// limiting.
ctjs::FuncOp analyzer::callable(mlir::Value value, unsigned depth) {
    if (!value || depth > 64 || !step() || ambiguousFunctions) { return {}; }
    if (auto made = value.getDefiningOp<ctjs::CreateClosureOp>()) {
        // Only a closure creation names its function; no other value can.
        return closureTarget(made, functions);
    }
    if (auto load = value.getDefiningOp<ctjs::LoadGlobalOp>()) {
        auto & stores = globals[load.getName()];
        return stores.size() == 1 ? callable(stores.front().getValue(), depth + 1) : ctjs::FuncOp{};
    }
    if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(value)) {
        auto function = llvm::dyn_cast<ctjs::FuncOp>(argument.getOwner()->getParentOp());
        if (!function || function.getBody().empty() ||
            argument.getOwner() != &function.getBody().front() || argument.getArgNumber() < 3) {
            // Entry arguments 0-2 are implicit (receiver, new.target, callee).
            return {};
        }
        ctjs::FuncOp found;
        for (mlir::Operation * call : callers[function]) {
            auto candidate = callable(explicitArgument(call, argument.getArgNumber()), depth + 1);
            if (!candidate || (found && found != candidate)) { return {}; }
            found = candidate;
        }
        return found;
    }
    return {};
}

/// True when `operation` is a direct call, or a resolved zero-argument
/// factory call with an undefined receiver, whose callee value is the target's
/// own closure, whose argument count matches, and whose target is closed or
/// a transported callback (see transportedCallable).
bool analyzer::exactCall(mlir::Operation * operation) {
    auto function = target(operation);
    if (!function || function.getBody().empty()) { return false; }
    auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
    auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
    if (!direct && (!call || !indirectFactories.contains(call) || !call.getArgs().empty() ||
                    !llvm::isa_and_nonnull<ctjs::UndefinedAttr>(primitive(call.getReceiver())))) {
        return false;
    }
    auto callee = direct ? direct.getCalleeValue() : call.getCallee();
    const auto arguments = direct ? direct->getNumOperands() : call->getNumOperands() + 1;
    return function.getBody().front().getNumArguments() == arguments &&
           callable(callee) == function &&
           (closedCallableProblem(function, module).empty() || transportedCallable(function));
}

/// True when `function` is a callback that is only passed along: its single
/// closure is forwarded through parameters of wrappers that are each called
/// exactly once, unconditionally, until it reaches the one factory call. Any
/// other use refuses. This does not make an arbitrary private function part of
/// a closed call graph.
bool analyzer::transportedCallable(ctjs::FuncOp function) {
    if (!llvm::is_contained(contract.initialIntrinsics, "Map") || function == entry ||
        function.getUpvalueCount() != 0 || !llvm::hasSingleElement(function.getBody()) ||
        callers[function].size() != 1) {
        return false;
    }
    llvm::SmallVector<mlir::Value> pending;
    unsigned creations = 0;
    module.walk([&](ctjs::CreateClosureOp made) {
        if (!step()) { return; }
        if (callable(made.getResult()) == function) {
            pending.push_back(made.getResult());
            ++creations;
        }
    });
    if (creations != 1) { return false; }
    llvm::DenseSet<mlir::Value> visited;
    while (!pending.empty()) {
        const auto value = pending.pop_back_val();
        if (!step() || !visited.insert(value).second) { return false; }
        for (mlir::OpOperand & use : value.getUses()) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                call && use.getOperandNumber() == 0 && call == callers[function].front() &&
                indirectFactories.lookup(call) == function) {
                continue;
            }
            // In ctjs.call_direct, operand 2 is the callee and explicit
            // arguments start at operand 3.
            auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
            if (!call) { return false; }
            if (use.getOperandNumber() == 2 && call == callers[function].front()) { continue; }
            auto wrapper = target(call);
            if (use.getOperandNumber() < 3 || !wrapper || wrapper == function || wrapper == entry ||
                wrapper.getUpvalueCount() != 0 || !llvm::hasSingleElement(wrapper.getBody()) ||
                callers[wrapper].size() != 1 || callers[wrapper].front() != call ||
                call->getNumOperands() != wrapper.getBody().front().getNumArguments() ||
                callable(call.getCalleeValue()) != wrapper || !anchor(call)) {
                return false;
            }
            pending.push_back(wrapper.getBody().front().getArgument(use.getOperandNumber()));
        }
    }
    return !exhausted;
}

/// The property write `read` sees: the latest write to the same object and
/// key that runs before it. Null when there is none, or when some other write
/// to that field cannot be ordered against the read or the latest write.
ctjs::SetPropertyOp analyzer::currentWrite(ctjs::GetPropertyOp read, unsigned depth) {
    if (depth > 64 || !step()) { return {}; }
    const auto owner = object(read.getObject(), depth + 1);
    const auto key = ctjs::constantKey(read.getKey());
    if (!owner || !ctjs::ordinaryKey(key)) { return {}; }
    ctjs::SetPropertyOp latest;
    bool uncertain = false;
    module.walk([&](ctjs::SetPropertyOp write) {
        if (!step() || ctjs::constantKey(write.getKey()) != key ||
            object(write.getObject(), depth + 1) != owner) {
            return;
        }
        if (before(write, read)) {
            if (!latest || before(latest, write)) {
                latest = write;
            } else if (!before(write, latest)) {
                uncertain = true;
            }
        } else if (!before(read, write) && active(write)) {
            uncertain = true;
        }
    });
    return latest && !uncertain ? latest : ctjs::SetPropertyOp{};
}

/// Resolves `obj.method(...)` where `method` is the closure last stored into
/// that field of a known object. The closure must either capture a Map that
/// capturedMap() proves, or be a capture-free getter that ignores its
/// arguments and returns a primitive literal. nullopt otherwise.
std::optional<HostCallableEdge> analyzer::propertyCall(mlir::Operation * operation) {
    if (!step()) { return {}; }
    auto js = jsCallOf(operation);
    if (!js) { return {}; }
    auto direct = js->direct;
    if (direct && !llvm::isa_and_nonnull<ctjs::UndefinedAttr>(primitive(direct.getNewTarget()))) {
        return {};
    }
    const auto receiver = js->receiver;
    const auto arguments = js->args;
    auto read = js->callee.getDefiningOp<ctjs::GetPropertyOp>();
    if (!read || !before(read, operation)) { return {}; }
    auto write = currentWrite(read);
    auto closure =
        write ? write.getValue().getDefiningOp<ctjs::CreateClosureOp>() : ctjs::CreateClosureOp{};
    auto function = closure ? callable(closure.getResult()) : ctjs::FuncOp{};
    if (!function || !llvm::hasSingleElement(function.getBody()) ||
        (direct && target(direct) != function)) {
        return {};
    }
    if (!ctjs::isUndefinedConstant(closure.getEnclosingThis())) { return {}; }
    const auto owner = object(read.getObject());
    if (!owner || object(receiver) != owner) { return {}; }
    std::optional<HostCapturedMap> capture;
    if (!closure.getUpvalues().empty()) {
        capture = capturedMap(closure, function, operation, read);
        if (!capture) { return {}; }
    } else if (function.getUpvalueCount() != 0 ||
               function.getBody().front().getNumArguments() != 3 || !arguments.empty()) {
        return {};
    }
    // capturedMap() already checked every argument use and the whole method
    // body. Any other closure is accepted only as a getter that ignores its
    // arguments and returns a primitive literal with no side effects.
    if (!capture) {
        for (mlir::BlockArgument argument : function.getBody().front().getArguments()) {
            if (!rootedUsers(argument)) { return {}; }
        }
        bool returned = false;
        for (mlir::Operation & body : function.getBody().front()) {
            if (!step()) { return {}; }
            if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(body)) {
                auto constant = result.getValue().getDefiningOp<ctjs::ConstantOp>();
                if (!constant || !ctjs::isPrimitiveAttr(constant.getValue())) { return {}; }
                returned = true;
            } else if (!llvm::isa<ctjs::ConstantOp, ctjs::FrameEnterOp, ctjs::FrameExitOp,
                                  ctjs::RootOp>(body)) {
                return {};
            }
        }
        if (!returned) { return {}; }
    }
    for (mlir::OpOperand & use : closure.getResult().getUses()) {
        if (!step()) { return {}; }
        if (llvm::isa<ctjs::RootOp>(use.getOwner()) ||
            (use.getOwner() == write.getOperation() && use.getOperandNumber() == 2)) {
            continue;
        }
        return {};
    }
    for (mlir::OpOperand & use : read.getResult().getUses()) {
        if (!step()) { return {}; }
        if (llvm::isa<ctjs::RootOp>(use.getOwner()) ||
            (llvm::isa<ctjs::CallOp>(use.getOwner()) && use.getOperandNumber() == 0) ||
            (llvm::isa<ctjs::CallDirectOp>(use.getOwner()) && use.getOperandNumber() == 2) ||
            (capture && use.getOwner() == capture->argument.getOperation() &&
             use.getOperandNumber() == 0)) {
            continue;
        }
        return {};
    }
    HostCallableEdge edge{operation, read, write, closure, function, std::nullopt, {}};
    if (capture) {
        const auto parameters = llvm::find_if(
            capture->parameters, [&](const auto & member) { return member.function == function; });
        const unsigned offset = capture->argument ? 1u : 0u;
        if (parameters == capture->parameters.end() ||
            arguments.size() != parameters->alternatives.size() + offset) {
            return {};
        }
        // Formals start after the three implicit entry arguments and, when
        // present, the prepared Map parameter.
        for (unsigned index = 0; index < parameters->alternatives.size(); ++index) {
            if (!step()) { return {}; }
            const auto parameter = function.getBody().front().getArgument(3 + offset + index);
            const auto actual = arguments[offset + index];
            ctjs::CreateObjectOp made;
            mlir::BlockArgument element;
            if (llvm::is_contained(parameters->objectKeys, parameter) &&
                !entryCategories(actual, capturedResults, operation).known) {
                element = elementInput(actual);
                made = namedObject(actual);
                if (element) {
                    if (!llvm::is_contained(capture->outerKeyInputs, element) ||
                        !llvm::is_contained(capture->outerKeyParameters, parameter)) {
                        return {};
                    }
                } else if (!made) {
                    return {};
                }
            }
            edge.arguments.push_back(
                {parameter, actual, parameters->alternatives[index], made, element});
        }
    }
    edge.capturedMap = std::move(capture);
    return edge;
}

/// Collects every call of `function` through the field `publication` stores
/// it in. False when some call reaches `function` another way or does not
/// match its arity, receiver or new.target. With `prepared`, the calls must be
/// direct and pass the closure's captured Map (upvalue 0) as the first
/// explicit argument (operand 3).
bool analyzer::capturedMapCalls(ctjs::FuncOp function, ctjs::SetPropertyOp publication,
                                bool prepared, llvm::SmallVectorImpl<mlir::Operation *> & calls) {
    if (!function || !step()) { return false; }
    auto & body = function.getBody().front();
    const auto census = module.walk([&](mlir::Operation * operation) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        auto js = jsCallOf(operation);
        if (!js) { return mlir::WalkResult::advance(); }
        auto direct = js->direct;
        auto read = js->callee.getDefiningOp<ctjs::GetPropertyOp>();
        if (!read || currentWrite(read) != publication) {
            return direct && target(direct) == function ? mlir::WalkResult::interrupt()
                                                        : mlir::WalkResult::advance();
        }
        const auto args = js->args;
        const auto owner = object(publication.getObject());
        if (!owner || !before(read, operation) || object(js->receiver) != owner ||
            args.size() != body.getNumArguments() - ctjs::implicit_arguments ||
            (prepared && !direct) ||
            (direct && (target(direct) != function || !llvm::isa_and_nonnull<ctjs::UndefinedAttr>(
                                                          primitive(direct.getNewTarget()))))) {
            return mlir::WalkResult::interrupt();
        }
        if (prepared) {
            auto environment = args.front().getDefiningOp<ctjs::LoadUpvalueOp>();
            if (!environment || environment.getIndex() != 0 ||
                environment.getClosure() != read.getResult() || !before(read, environment) ||
                !before(environment, operation)) {
                return mlir::WalkResult::interrupt();
            }
            for (mlir::OpOperand & use : environment.getResult().getUses()) {
                if (!step() ||
                    (use.getOwner() != operation && !llvm::isa<ctjs::RootOp>(use.getOwner())) ||
                    (use.getOwner() == operation && use.getOperandNumber() != 3)) {
                    return mlir::WalkResult::interrupt();
                }
            }
        }
        calls.push_back(operation);
        return mlir::WalkResult::advance();
    });
    return !census.wasInterrupted() && !exhausted;
}

/// The set of primitive types (categories) `value` may have, as seen by
/// `consumer`, derived from literals, globals with one store, own-field reads
/// of entry objects, arithmetic, `===` and scf.if joins. Values found in
/// `results` are taken from there and, with `dependencies`, appended to it.
/// Unknown when the value cannot be classified.
PrimitiveAlternatives analyzer::entryCategories(
    mlir::Value value, const llvm::DenseMap<mlir::Value, PrimitiveAlternatives> & results,
    mlir::Operation * consumer, unsigned depth, std::vector<mlir::Value> * dependencies) {
    if (!value || depth > 64 || !step()) { return {}; }
    auto * definition = value.getDefiningOp();
    // before() can order effects across an scf.if arm or a call, but an SSA
    // value is only usable where it dominates, in its own function.
    if (!definition || (consumer && (definition->getParentOfType<ctjs::FuncOp>() !=
                                         consumer->getParentOfType<ctjs::FuncOp>() ||
                                     !dominance.properlyDominates(definition, consumer)))) {
        return {};
    }
    if (auto known = classScalarReads.find(value); known != classScalarReads.end()) {
        return known->second.categories();
    }
    if (auto known = results.find(value); known != results.end()) {
        if (dependencies) {
            if (!step()) { return {}; }
            dependencies->push_back(value);
        }
        return known->second.categories();
    }
    if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(definition)) {
        if (mutableScalarReads.contains(load)) {
            return PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::NumberAttr>());
        }
        const auto & stores = globals[load.getName()];
        if (stores.empty() && (llvm::is_contained(contract.undefinedBindings, load.getName()) ||
                               (load.getTypeofLookup() &&
                                llvm::is_contained(contract.absentBindings, load.getName())))) {
            return PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::UndefinedAttr>());
        }
        if (stores.size() != 1 || !before(stores.front(), load)) { return {}; }
        auto store = stores.front();
        return entryCategories(store.getValue(), results, store, depth + 1, dependencies);
    }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(definition)) {
        // A read of a field of an object made in the entry function has the
        // category of the value last written before it, even if a later write
        // changes the field. This proves only the type, never the value. The
        // caller must still check every use of the object.
        auto made = read.getObject().getDefiningOp<ctjs::CreateObjectOp>();
        auto write = made ? currentWrite(read, depth + 1) : ctjs::SetPropertyOp{};
        if (!made || made->getParentOp() != entry || read->getParentOp() != entry || !write ||
            write->getParentOp() != entry || write.getObject() != made.getResult() ||
            !dominance.properlyDominates(made.getOperation(), write) ||
            !dominance.properlyDominates(write.getOperation(), read)) {
            return {};
        }
        if (dependencies) { dependencies->push_back(value); }
        return entryCategories(write.getValue(), results, write, depth + 1, dependencies);
    }
    if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(definition)) {
        if (!ctjs::isArithmetic(binary.getKind())) { return {}; }
        const auto number = mlir::TypeID::get<ctjs::NumberAttr>();
        const auto numeric = [&](mlir::Value operand) {
            auto tag = entryCategories(operand, results, binary, depth + 1, dependencies).tag();
            return tag == number || tag == mlir::TypeID::get<ctjs::BooleanAttr>();
        };
        if (!numeric(binary.getLhs()) || !numeric(binary.getRhs())) { return {}; }
        // This is a type, never an evaluated Number: zero, -0, NaN and
        // infinities remain possible, so primitive() and truth() must not use
        // it to pick a branch or substitute a value.
        return PrimitiveAlternatives::forTag(number);
    }
    if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(definition);
        compare && compare.getKind() == ctjs::CompareKind::StrictEq) {
        if (!entryCategories(compare.getLhs(), results, compare, depth + 1, dependencies).known ||
            !entryCategories(compare.getRhs(), results, compare, depth + 1, dependencies).known) {
            return {};
        }
        return PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>());
    }
    if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(definition)) {
        if (!step() || !dominance.dominates(branch.getCondition(), branch)) { return {}; }
        auto result = llvm::cast<mlir::OpResult>(value);
        std::optional<PrimitiveAlternatives> joined;
        // Join both arms, even when the condition is constant: a type proof
        // does not select branches, so each yielded value must be classified.
        for (mlir::Region & arm : branch->getRegions()) {
            if (!step() || !llvm::hasSingleElement(arm)) { return {}; }
            auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(arm.front().getTerminator());
            if (!yield || result.getResultNumber() >= yield.getNumOperands()) { return {}; }
            auto categories = entryCategories(yield.getOperand(result.getResultNumber()), results,
                                              yield, depth + 1, dependencies);
            joined = joined ? joined->joined(categories) : categories;
        }
        return joined.value_or(PrimitiveAlternatives{});
    }
    if (!llvm::isa<ctjs::ConstantOp, mlir::arith::ConstantOp, ctjs::UnaryOp, ctjs::CompareOp>(
            definition)) {
        return {};
    }
    for (mlir::Value operand : definition->getOperands()) {
        if (!entryCategories(operand, results, definition, depth + 1, dependencies).known) {
            return {};
        }
    }
    auto constant = primitive(value, depth + 1);
    return constant ? PrimitiveAlternatives::forTag(constant.getTypeID()) : PrimitiveAlternatives{};
}

/// Classifies an entry-function read of a global with one earlier store as a
/// Number, Boolean or String. Every value the classification relied on must be
/// a checked captured-Map call or a field read of one of its leaf objects.
std::optional<HostScalarGlobalRead> analyzer::scalarGlobalRead(ctjs::LoadGlobalOp read) {
    if (!step() || read->getParentOp() != entry || !llvm::hasSingleElement(entry.getBody())) {
        return std::nullopt;
    }
    const auto & stores = globals[read.getName()];
    if (stores.size() != 1) { return std::nullopt; }
    auto store = stores.front();
    if (store->getParentOp() != entry || !before(store, read)) { return std::nullopt; }
    HostScalarGlobalRead result{store, read, store.getValue(), {}, {}};
    result.alternatives =
        entryCategories(result.value, capturedResults, store, 0, &result.dependencies);
    if (result.alternatives.tag() != mlir::TypeID::get<ctjs::NumberAttr>() &&
        result.alternatives.tag() != mlir::TypeID::get<ctjs::BooleanAttr>() &&
        result.alternatives.tag() != mlir::TypeID::get<ctjs::StringAttr>()) {
        return std::nullopt;
    }
    // A scalar built only from literals has no dependencies and needs no
    // further check. Otherwise every value it depends on must be either a
    // checked captured-Map call (in `checkedCalls`) or a read of a leaf
    // object field that such a call already proved.
    for (mlir::Value dependency : result.dependencies) {
        if (!step()) { return std::nullopt; }
        auto * origin = dependency.getDefiningOp();
        if (!origin || origin->getParentOp() != entry ||
            !dominance.properlyDominates(origin, store)) {
            return std::nullopt;
        }
        auto field = llvm::dyn_cast<ctjs::GetPropertyOp>(origin);
        auto leaf = field ? field.getObject().getDefiningOp<ctjs::CreateObjectOp>()
                          : ctjs::CreateObjectOp{};
        bool found = false;
        for (const HostCallableEdge & edge : checkedCalls) {
            if (!step()) { return std::nullopt; }
            if (!edge.capturedMap) { continue; }
            found |= edge.call == origin ||
                     (leaf && llvm::is_contained(edge.capturedMap->leafObjects, leaf) &&
                      llvm::is_contained(edge.capturedMap->leafReads, field));
        }
        if (!found) { return std::nullopt; }
    }
    return result;
}

/// True when `function` runs at most once: each function on the chain from it
/// to the entry has exactly one exact, unconditional caller, and nothing
/// calls the entry.
bool analyzer::singleInvocation(ctjs::FuncOp function) {
    llvm::DenseSet<mlir::Operation *> visited;
    while (function && function != entry) {
        if (!step() || !visited.insert(function).second) { return false; }
        auto & sites = callers[function];
        if (sites.size() != 1) { return false; }
        auto call = sites.front();
        if (!exactCall(call) || !anchor(call)) { return false; }
        function = call->getParentOfType<ctjs::FuncOp>();
    }
    return function == entry && callers[entry].empty();
}

/// The one allocation `value` denotes, traced through globals, field reads,
/// parameters, call results and scf.if results. Null when unknown.
ctjs::CreateObjectOp analyzer::object(mlir::Value value, unsigned depth) {
    if (!value || depth > 64 || !step() || !evaluating.insert(value).second) { return {}; }
    const llvm::scope_exit done([&] { evaluating.erase(value); });
    if (auto made = value.getDefiningOp<ctjs::CreateObjectOp>()) {
        // An allocation site denotes one object only if its function runs
        // once. A factory called once from a wrapper that is called many
        // times still makes several objects, whose fields must not be merged.
        return singleInvocation(made->getParentOfType<ctjs::FuncOp>()) ? made
                                                                       : ctjs::CreateObjectOp{};
    }
    if (auto load = value.getDefiningOp<ctjs::LoadGlobalOp>()) {
        auto & stores = globals[load.getName()];
        return stores.size() == 1 ? object(stores.front().getValue(), depth + 1)
                                  : ctjs::CreateObjectOp{};
    }
    if (auto read = value.getDefiningOp<ctjs::GetPropertyOp>()) {
        auto write = currentWrite(read, depth + 1);
        return write ? object(write.getValue(), depth + 1) : ctjs::CreateObjectOp{};
    }
    if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(value)) {
        auto function = llvm::dyn_cast<ctjs::FuncOp>(argument.getOwner()->getParentOp());
        if (!function || function.getBody().empty() || argument.getArgNumber() < 3 ||
            argument.getOwner() != &function.getBody().front()) {
            return {};
        }
        ctjs::CreateObjectOp found;
        for (mlir::Operation * call : callers[function]) {
            if (!exactCall(call)) { return {}; }
            auto candidate = object(explicitArgument(call, argument.getArgNumber()), depth + 1);
            if (!candidate || (found && found != candidate)) { return {}; }
            found = candidate;
        }
        return found;
    }
    if (auto * call = value.getDefiningOp();
        llvm::isa_and_nonnull<ctjs::CallDirectOp, ctjs::CallOp>(call)) {
        if (!exactCall(call)) { return {}; }
        ctjs::CreateObjectOp found;
        for (ctjs::ReturnOp returned : returns[target(call)]) {
            if (!active(returned)) { continue; }
            auto candidate = object(returned.getValue(), depth + 1);
            if (!candidate || (found && found != candidate)) { return {}; }
            found = candidate;
        }
        return found;
    }
    if (auto result = llvm::dyn_cast<mlir::OpResult>(value)) {
        auto branch = llvm::dyn_cast<mlir::scf::IfOp>(result.getOwner());
        if (!branch) { return {}; }
        const auto selected = truth(branch.getCondition(), depth + 1);
        ctjs::CreateObjectOp found;
        for (unsigned arm = 0; arm < 2; ++arm) {
            if (selected && arm != (*selected ? 0u : 1u)) { continue; }
            auto & region = branch->getRegion(arm);
            if (region.empty()) { return {}; }
            auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
            if (!yield || result.getResultNumber() >= yield.getNumOperands()) { return {}; }
            auto candidate = object(yield.getOperand(result.getResultNumber()), depth + 1);
            if (!candidate || (found && found != candidate)) { return {}; }
            found = candidate;
        }
        return found;
    }
    return {};
}

/// The primitive literal `value` always holds, or null when unknown.
mlir::Attribute analyzer::primitive(mlir::Value value, unsigned depth) {
    if (!value || depth > 64 || !step()) { return {}; }
    auto * context = module.getContext();
    if (auto constant = value.getDefiningOp<ctjs::ConstantOp>()) { return constant.getValue(); }
    if (auto constant = value.getDefiningOp<mlir::arith::ConstantOp>()) {
        return constant.getValue();
    }
    if (auto load = value.getDefiningOp<ctjs::LoadGlobalOp>()) {
        auto & stores = globals[load.getName()];
        if (stores.empty() && (llvm::is_contained(contract.undefinedBindings, load.getName()) ||
                               (load.getTypeofLookup() &&
                                llvm::is_contained(contract.absentBindings, load.getName())))) {
            return ctjs::UndefinedAttr::get(context);
        }
        return stores.size() == 1 ? primitive(stores.front().getValue(), depth + 1)
                                  : mlir::Attribute{};
    }
    if (auto unary = value.getDefiningOp<ctjs::UnaryOp>()) {
        if (unary.getKind() == ctjs::UnaryKind::Not) {
            auto bit = truth(unary.getOperand(), depth + 1);
            return bit ? ctjs::BooleanAttr::get(context, !*bit) : mlir::Attribute{};
        }
        if (unary.getKind() != ctjs::UnaryKind::TypeOf) { return {}; }
        auto name = typeofLiteral(primitive(unary.getOperand(), depth + 1));
        if (name.empty() && object(unary.getOperand(), depth + 1)) {
            name = "object";
        } else if (name.empty() && callable(unary.getOperand(), depth + 1)) {
            name = "function";
        }
        return name.empty() ? mlir::Attribute{} : ctjs::StringAttr::get(context, name);
    }
    if (auto compare = value.getDefiningOp<ctjs::CompareOp>()) {
        if (compare.getKind() != ctjs::CompareKind::Eq &&
            compare.getKind() != ctjs::CompareKind::StrictEq) {
            return {};
        }
        auto left = primitive(compare.getLhs(), depth + 1);
        auto right = primitive(compare.getRhs(), depth + 1);
        // UMD module wrappers branch on comparisons such as `typeof exports
        // == "object"`. Only string-to-string comparisons are folded: no
        // coercion, NaN, signed-zero or object equality is inferred.
        if (llvm::isa_and_nonnull<ctjs::StringAttr>(left) &&
            llvm::isa_and_nonnull<ctjs::StringAttr>(right)) {
            return ctjs::BooleanAttr::get(context, left == right);
        }
    }
    if (auto result = llvm::dyn_cast<mlir::OpResult>(value)) {
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(result.getOwner())) {
            auto selected = truth(branch.getCondition(), depth + 1);
            if (!selected) { return {}; }
            auto & region = branch->getRegion(*selected ? 0u : 1u);
            if (region.empty()) { return {}; }
            auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
            if (yield && result.getResultNumber() < yield.getNumOperands()) {
                return primitive(yield.getOperand(result.getResultNumber()), depth + 1);
            }
        }
    }
    return {};
}

/// The JS truthiness of `value`, or nullopt when unknown. Objects and
/// closures are truthy.
std::optional<bool> analyzer::truth(mlir::Value value, unsigned depth) {
    if (!value || depth > 64 || !step()) { return {}; }
    if (auto truthy = value.getDefiningOp<ctjs::TruthyOp>()) {
        return truth(truthy->getOperand(0), depth + 1);
    }
    if (llvm::isa_and_nonnull<mlir::arith::TruncIOp, mlir::arith::ExtUIOp>(value.getDefiningOp())) {
        return truth(value.getDefiningOp()->getOperand(0), depth + 1);
    }
    if (auto bit = literalTruth(primitive(value, depth + 1))) { return bit; }
    if (object(value, depth + 1) || callable(value, depth + 1)) { return true; }
    return {};
}

} // namespace ctcompile::ctnative::host_detail
