#include "Analysis.h"

#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/ScopeExit.h"

#include <utility>

namespace ctcompile::ctnative::host_detail {
namespace {
mlir::Value explicitArgument(mlir::Operation * operation, unsigned index) {
    if (index < 3) { return {}; }
    auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
    auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
    if (!direct && !call) { return {}; }
    auto args = direct ? direct.getArgs() : call.getArgs();
    return index - 3 < args.size() ? args[index - 3] : mlir::Value{};
}

} // namespace

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
        // The importer leaves factory() indirect inside its wrapper. Record
        // only the zero-argument callback supplied by the entry's sole direct
        // wrapper call. No source operation or operand is rewritten here.
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

bool analyzer::step() {
    if (remaining == 0) {
        exhausted = true;
        return false;
    }
    --remaining;
    return true;
}

ctjs::FuncOp analyzer::target(mlir::Operation * operation) const {
    if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) { return call.getTarget(); }
    return indirectFactories.lookup(operation);
}

ctjs::FuncOp analyzer::callable(mlir::Value value, unsigned depth) {
    if (!value || depth > 64 || !step() || ambiguousFunctions) { return {}; }
    if (auto made = value.getDefiningOp<ctjs::CreateClosureOp>()) {
        // A numeric index names a function only inside the enclosing closure's
        // source program. The importer supplies this activation's callee;
        // arbitrary values cannot establish that program identity.
        auto scope = made->getParentOfType<ctjs::FuncOp>();
        if (!scope || scope.getBody().empty() || scope.getBody().front().getNumArguments() < 3 ||
            made.getEnclosingClosure() != scope.getBody().front().getArgument(2)) {
            return {};
        }
        return made.getFunction() >= 0 ? functions.lookup(static_cast<unsigned>(made.getFunction()))
                                       : ctjs::FuncOp{};
    }
    if (auto load = value.getDefiningOp<ctjs::LoadGlobalOp>()) {
        auto & stores = globals[load.getName()];
        return stores.size() == 1 ? callable(stores.front().getValue(), depth + 1) : ctjs::FuncOp{};
    }
    if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(value)) {
        auto function = llvm::dyn_cast<ctjs::FuncOp>(argument.getOwner()->getParentOp());
        if (!function || function.getBody().empty() ||
            argument.getOwner() != &function.getBody().front() || argument.getArgNumber() < 3) {
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

bool analyzer::transportedCallable(ctjs::FuncOp function) {
    // A callback parameter is transport only when every use reaches the same
    // source callee through one exact, straight-line wrapper invocation. This
    // does not turn arbitrary private visibility into a closed call graph.
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

std::optional<HostCallableEdge> analyzer::propertyCall(mlir::Operation * operation) {
    if (!step()) { return {}; }
    mlir::Value callee, receiver;
    mlir::ValueRange arguments;
    auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
    if (direct) {
        if (!llvm::isa_and_nonnull<ctjs::UndefinedAttr>(primitive(direct.getNewTarget()))) {
            return {};
        }
        callee = direct.getCalleeValue();
        receiver = direct.getReceiver();
        arguments = direct.getArgs();
    } else if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation)) {
        callee = call.getCallee();
        receiver = call.getReceiver();
        arguments = call.getArgs();
    } else {
        return {};
    }
    auto read = callee.getDefiningOp<ctjs::GetPropertyOp>();
    if (!read || !before(read, operation)) { return {}; }
    auto write = currentWrite(read);
    auto closure =
        write ? write.getValue().getDefiningOp<ctjs::CreateClosureOp>() : ctjs::CreateClosureOp{};
    auto function = closure ? callable(closure.getResult()) : ctjs::FuncOp{};
    if (!function || !llvm::hasSingleElement(function.getBody()) ||
        (direct && target(direct) != function)) {
        return {};
    }
    auto enclosingThis = closure.getEnclosingThis().getDefiningOp<ctjs::ConstantOp>();
    if (!enclosingThis || !llvm::isa<ctjs::UndefinedAttr>(enclosingThis.getValue())) { return {}; }
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
    // The capture proof checks every argument use and the complete Map body.
    // Otherwise retain the effect-free literal getter boundary.
    if (!capture) {
        for (mlir::BlockArgument argument : function.getBody().front().getArguments()) {
            for (mlir::Operation * user : argument.getUsers()) {
                if (!step() || !llvm::isa<ctjs::RootOp>(user)) { return {}; }
            }
        }
        bool returned = false;
        for (mlir::Operation & body : function.getBody().front()) {
            if (!step()) { return {}; }
            if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(body)) {
                auto constant = result.getValue().getDefiningOp<ctjs::ConstantOp>();
                if (!constant ||
                    !llvm::isa<ctjs::NumberAttr, ctjs::BooleanAttr, ctjs::StringAttr,
                               ctjs::NullAttr, ctjs::UndefinedAttr>(constant.getValue())) {
                    return {};
                }
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
        for (unsigned index = 0; index < parameters->alternatives.size(); ++index) {
            if (!step()) { return {}; }
            const auto parameter = function.getBody().front().getArgument(3 + offset + index);
            const auto actual = arguments[offset + index];
            ctjs::CreateObjectOp made;
            mlir::BlockArgument element;
            if (llvm::is_contained(parameters->objectKeys, parameter) &&
                !entryCategories(actual, capturedResults, operation).known) {
                element = elementInput(actual);
                made = actual.getDefiningOp<ctjs::CreateObjectOp>();
                if (auto load = actual.getDefiningOp<ctjs::LoadGlobalOp>()) {
                    const auto global = objectGlobalRead(load);
                    if (!global) { return {}; }
                    made = global->object;
                }
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

std::optional<HostCapturedMap> analyzer::capturedMap(ctjs::CreateClosureOp closure,
                                                     ctjs::FuncOp function, mlir::Operation * call,
                                                     ctjs::GetPropertyOp read) {
    if (!step() || closure.getUpvalues().size() != 1 ||
        !llvm::is_contained(contract.initialIntrinsics, "Map")) {
        return {};
    }
    if (auto indices = closure.getEnclosingIndicesAttr();
        indices && (indices.size() != 1 || indices[0] >= 0)) {
        return {};
    }
    auto factory = closure->getParentOfType<ctjs::FuncOp>();
    if (!factory || !llvm::hasSingleElement(factory.getBody()) || !singleInvocation(factory)) {
        return {};
    }
    HostCapturedMap result;
    auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(call);
    auto capture = closure.getUpvalues().front();
    result.cell = capture.getDefiningOp<ctjs::CreateCellOp>();
    const bool prepared = !result.cell;
    auto & body = function.getBody().front();
    if (prepared) {
        if (function.getUpvalueCount() != 0 || body.getNumArguments() < 4 || !direct ||
            direct.getArgs().size() != body.getNumArguments() - ctjs::implicit_arguments) {
            return {};
        }
        result.argument = direct.getArgs().front().getDefiningOp<ctjs::LoadUpvalueOp>();
        if (!result.argument || result.argument.getIndex() != 0 ||
            result.argument.getClosure() != read.getResult() || !before(read, result.argument) ||
            !before(result.argument, call)) {
            return {};
        }
        for (mlir::OpOperand & use : result.argument.getResult().getUses()) {
            if (!step() || (use.getOwner() != call && !llvm::isa<ctjs::RootOp>(use.getOwner())) ||
                (use.getOwner() == call && use.getOperandNumber() != 3)) {
                return {};
            }
        }
    } else if (function.getUpvalueCount() != 1 || body.getNumArguments() < 3) {
        return {};
    }

    // A normalized capture may leave the original dead cell bookkeeping in
    // its factory. Recover that cell from the allocation uses and check it as
    // strictly as the source binding; markers never authorize extra writes.
    mlir::Value resource = capture;
    llvm::DenseSet<mlir::Operation *> captures;
    const auto collectCapture = [&](mlir::OpOperand & use) {
        auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(use.getOwner());
        if (!made || use.getOperandNumber() != 2 || made.getUpvalues().size() != 1 ||
            made->getBlock() != closure->getBlock()) {
            return false;
        }
        captures.insert(made);
        return true;
    };
    if (result.cell) {
        for (mlir::OpOperand & use : result.cell.getResult().getUses()) {
            if (!step()) { return {}; }
            if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner())) {
                if (use.getOperandNumber() != 0 || result.initialization) { return {}; }
                result.initialization = write;
            }
        }
        resource =
            result.initialization ? result.initialization.getValue() : result.cell.getInitial();
    }
    result.allocation = resource.getDefiningOp<ctjs::ConstructOp>();
    if (!result.allocation || !result.allocation.getArgs().empty() ||
        result.allocation.getNewTarget() != result.allocation.getCallee() ||
        result.allocation->getBlock() != closure->getBlock() ||
        !result.allocation->isBeforeInBlock(closure)) {
        return {};
    }
    result.intrinsic = result.allocation.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
    if (!result.intrinsic || result.intrinsic.getName() != "Map" || !globals["Map"].empty() ||
        !before(result.intrinsic, result.allocation)) {
        return {};
    }
    if (prepared) {
        for (mlir::OpOperand & use : resource.getUses()) {
            if (!step()) { return {}; }
            ctjs::CreateCellOp cell;
            if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner())) {
                cell = write.getCell().getDefiningOp<ctjs::CreateCellOp>();
                if (!cell || result.initialization) { return {}; }
                result.initialization = write;
            } else {
                cell = llvm::dyn_cast<ctjs::CreateCellOp>(use.getOwner());
            }
            if (cell) {
                if (result.cell && result.cell != cell) { return {}; }
                result.cell = cell;
            }
        }
    }
    if (result.cell) {
        if (result.cell->getBlock() != closure->getBlock() ||
            !result.cell->isBeforeInBlock(closure)) {
            return {};
        }
        if (result.initialization) {
            auto initial = result.cell.getInitial().getDefiningOp<ctjs::ConstantOp>();
            if (!initial || !llvm::isa<ctjs::UndefinedAttr>(initial.getValue()) ||
                result.initialization.getValue() != resource ||
                result.initialization->getBlock() != closure->getBlock() ||
                !result.cell->isBeforeInBlock(result.initialization) ||
                !result.allocation->isBeforeInBlock(result.initialization) ||
                !result.initialization->isBeforeInBlock(closure)) {
                return {};
            }
        } else if (result.cell.getInitial() != resource) {
            return {};
        }
        for (mlir::OpOperand & use : result.cell.getResult().getUses()) {
            if (!step()) { return {}; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                (use.getOwner() == result.initialization.getOperation() &&
                 use.getOperandNumber() == 0) ||
                (!prepared && collectCapture(use))) {
                continue;
            }
            return {};
        }
    }
    for (mlir::OpOperand & use : resource.getUses()) {
        if (!step()) { return {}; }
        if (llvm::isa<ctjs::RootOp>(use.getOwner()) ||
            (use.getOwner() == result.cell.getOperation() && use.getOperandNumber() == 0) ||
            (use.getOwner() == result.initialization.getOperation() &&
             use.getOperandNumber() == 1) ||
            (prepared && collectCapture(use))) {
            continue;
        }
        return {};
    }

    // A selected getter cannot close the Map contents by itself. Every
    // closure that can reach the same immutable slot must have a checked body
    // and exactly one fixed-field publication in this factory's same table.
    // Walk source order so separate current calls derive identical families.
    ctjs::CreateObjectOp table;
    llvm::DenseSet<mlir::Operation *> familyFunctions;
    llvm::DenseSet<llvm::StringRef> fields;
    llvm::SmallVector<ctjs::SetPropertyOp> publications;
    for (mlir::Operation & operation : factory.getBody().front()) {
        if (!step()) { return {}; }
        if (!captures.contains(&operation)) { continue; }
        auto made = llvm::cast<ctjs::CreateClosureOp>(operation);
        auto member = callable(made.getResult());
        const auto indices = made.getEnclosingIndicesAttr();
        auto enclosingThis = made.getEnclosingThis().getDefiningOp<ctjs::ConstantOp>();
        if (!member || member == entry || member == factory ||
            !familyFunctions.insert(member).second || !llvm::hasSingleElement(member.getBody()) ||
            member.getUpvalueCount() != (prepared ? 0u : 1u) ||
            member.getBody().front().getNumArguments() < (prepared ? 4u : 3u) ||
            (indices && (indices.size() != 1 || indices[0] >= 0)) || !enclosingThis ||
            !llvm::isa<ctjs::UndefinedAttr>(enclosingThis.getValue()) ||
            !result.allocation->isBeforeInBlock(made) ||
            (result.cell && !result.cell->isBeforeInBlock(made)) ||
            (result.initialization && !result.initialization->isBeforeInBlock(made))) {
            return {};
        }
        ctjs::SetPropertyOp publication;
        for (mlir::OpOperand & use : made.getResult().getUses()) {
            if (!step()) { return {}; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
            auto owner = write ? write.getObject().getDefiningOp<ctjs::CreateObjectOp>()
                               : ctjs::CreateObjectOp{};
            if (!write || publication || use.getOperandNumber() != 2 || !owner ||
                owner->getBlock() != made->getBlock() || write->getBlock() != made->getBlock() ||
                (table && table != owner) ||
                !ctjs::ordinaryKey(ctjs::constantKey(write.getKey())) ||
                !fields.insert(ctjs::constantKey(write.getKey())).second ||
                !owner->isBeforeInBlock(write) || !made->isBeforeInBlock(write)) {
                return {};
            }
            table = owner;
            publication = write;
        }
        if (!publication) { return {}; }
        result.closures.push_back(made);
        publications.push_back(publication);
    }
    if (!captures.contains(closure) || result.closures.size() != captures.size()) { return {}; }
    llvm::DenseMap<mlir::Operation *, unsigned> creations;
    module.walk([&](ctjs::CreateClosureOp made) {
        if (!step()) { return; }
        auto member = callable(made.getResult());
        if (member && familyFunctions.contains(member)) { ++creations[member]; }
    });
    for (mlir::Operation * member : familyFunctions) {
        if (!step() || creations.lookup(member) != 1) { return {}; }
    }
    // Discover all call identities before inspecting arguments or bodies.
    // No propertyCall recursion may supply this family's own type authority.
    llvm::SmallVector<llvm::SmallVector<mlir::Operation *>> familyCalls(publications.size());
    for (ctjs::SetPropertyOp publication : publications) {
        auto member = callable(publication.getValue());
        if (!capturedMapCalls(member, publication, prepared,
                              familyCalls[result.parameters.size()])) {
            return {};
        }
        result.parameters.push_back({member, {}});
    }
    if (!scalarCallbacks(result.parameters, result)) { return {}; }
    // Once any sibling can store a local or caller-owned object, unknown Map
    // reads cannot inherit the primitive-only contents guarantee. Inspect the
    // complete family before proving even one invocation result. This only
    // removes authority; the parameter/body proofs still check every use.
    bool primitiveContents = true;
    for (unsigned index = 0; index < result.parameters.size(); ++index) {
        auto member = result.parameters[index].function;
        const auto census = member.getBody().walk([&](mlir::Operation * operation) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (llvm::isa<ctjs::CreateObjectOp, ctjs::ConstructOp>(operation)) {
                primitiveContents = false;
            }
            auto store = llvm::dyn_cast<ctjs::CallOp>(operation);
            auto read = store ? store.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                              : ctjs::GetPropertyOp{};
            if (!read || ctjs::constantKey(read.getKey()) != "set" || store.getArgs().size() != 2) {
                return mlir::WalkResult::advance();
            }
            auto payload = llvm::dyn_cast<mlir::BlockArgument>(store.getArgs()[1]);
            if (!payload || payload.getOwner() != &member.getBody().front() ||
                payload.getArgNumber() < (prepared ? 4u : 3u)) {
                return mlir::WalkResult::advance();
            }
            for (mlir::Operation * invocation : familyCalls[index]) {
                if (!step()) { return mlir::WalkResult::interrupt(); }
                const auto actual = explicitArgument(invocation, payload.getArgNumber());
                if (actual.getDefiningOp<ctjs::CreateObjectOp>() || elementInput(actual)) {
                    primitiveContents = false;
                }
                if (auto load = actual.getDefiningOp<ctjs::LoadGlobalOp>();
                    load && objectGlobalRead(load)) {
                    primitiveContents = false;
                }
            }
            return mlir::WalkResult::advance();
        });
        if (census.wasInterrupted() || exhausted) { return {}; }
    }
    // Positive child-kind authority is independent of invocation results. Only
    // a complete census of outer writes can establish it; membership and the
    // mere presence of a constructor cannot. The body proof below still checks
    // every operation/use, including aliases and all structural continuations.
    llvm::DenseSet<mlir::Operation *> familyInvocations;
    for (const auto & invocations : familyCalls) {
        for (mlir::Operation * invocation : invocations) {
            if (!step()) { return {}; }
            familyInvocations.insert(invocation);
        }
    }
    result.childMapContents = true;
    result.outerStringKeys = true;
    result.childStringKeys = true;
    HostChildMapEntry childEntry;
    PrimitiveAlternatives childScalar;
    const llvm::DenseMap<mlir::Value, PrimitiveAlternatives> noResults;
    bool childEntryProved = !primitiveContents, childPublished = false;
    bool childScalarProved = !primitiveContents, childLeafProved = !primitiveContents;
    for (auto [index, parameters] : llvm::enumerate(result.parameters)) {
        auto member = parameters.function;
        auto & memberBody = member.getBody().front();
        const auto outer = memberBody.getArgument(prepared ? 3 : 2);
        // An invariant may not use the result of the invocation it authorizes.
        // All actual categories must close with no family results available.
        HostMethodParameters independent{member, {}};
        if ((childEntryProved || childScalarProved || childLeafProved || result.outerStringKeys ||
             result.childStringKeys) &&
            !capturedMapParameters(member, prepared, familyCalls[index], familyInvocations,
                                   noResults, independent)) {
            childEntryProved = false;
            childScalarProved = false;
            childLeafProved = false;
            result.outerStringKeys = false;
            result.childStringKeys = false;
        }
        const auto stringKey = [&](mlir::Value value) {
            if (!step()) { return false; }
            if (auto constant = value.getDefiningOp<ctjs::ConstantOp>()) {
                return llvm::isa<ctjs::StringAttr>(constant.getValue());
            }
            auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
            if (!argument || argument.getOwner() != &memberBody ||
                argument.getArgNumber() < (prepared ? 4u : 3u)) {
                return false;
            }
            const auto position = argument.getArgNumber() - (prepared ? 4u : 3u);
            return position < independent.alternatives.size() &&
                   independent.alternatives[position].tag() ==
                       mlir::TypeID::get<ctjs::StringAttr>();
        };
        const auto mapOrigin = [&](auto && self, mlir::Value value,
                                   unsigned depth = 0) -> mlir::Value {
            if (!value || depth > 32 || !step()) { return {}; }
            if (prepared && value == outer) { return outer; }
            if (auto load = value.getDefiningOp<ctjs::LoadUpvalueOp>();
                !prepared && load && load.getIndex() == 0 && load.getClosure() == outer) {
                return outer;
            }
            if (auto made = value.getDefiningOp<ctjs::ConstructOp>()) {
                auto intrinsic = made.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
                if (intrinsic && intrinsic.getName() == "Map" && globals["Map"].empty() &&
                    made.getNewTarget() == made.getCallee() && made.getArgs().empty() &&
                    dominance.dominates(made.getCallee(), made)) {
                    return value;
                }
                return {};
            }
            auto invoke = value.getDefiningOp<ctjs::CallOp>();
            auto read = invoke ? invoke.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                               : ctjs::GetPropertyOp{};
            if (!read || read.getObject() != invoke.getReceiver()) { return {}; }
            const auto receiver = self(self, invoke.getReceiver(), depth + 1);
            const auto action = ctjs::constantKey(read.getKey());
            if (action == "set" && invoke.getArgs().size() == 2) { return receiver; }
            // This role may be used only as a child receiver, never as evidence
            // that an outer payload is a fresh constructor. The completed write
            // census and body-use proof together exclude a root stored in itself.
            if (action == "get" && invoke.getArgs().size() == 1 && receiver == outer) {
                return value;
            }
            return {};
        };
        llvm::SmallVector<std::pair<ctjs::CallOp, mlir::Value>> publications, childWrites;
        const auto census = member.getBody().walk([&](ctjs::CallOp invoke) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            auto read = invoke.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (!read) { return mlir::WalkResult::advance(); }
            const auto action = ctjs::constantKey(read.getKey());
            if (action != "set" && action != "delete" && action != "clear") {
                return mlir::WalkResult::advance();
            }
            const auto receiver = mapOrigin(mapOrigin, invoke.getReceiver());
            if (action != "set") {
                if (receiver != outer) { childEntryProved = false; }
                return mlir::WalkResult::advance();
            }
            if (read.getObject() != invoke.getReceiver() || invoke.getArgs().size() != 2 ||
                !receiver) {
                result.childMapContents = false;
                result.outerStringKeys = false;
                result.childStringKeys = false;
            } else if (receiver == outer) {
                result.outerStringKeys &= stringKey(invoke.getArgs()[0]);
                const auto payload = mapOrigin(mapOrigin, invoke.getArgs()[1]);
                if (!payload || !payload.getDefiningOp<ctjs::ConstructOp>()) {
                    result.childMapContents = false;
                }
                publications.emplace_back(invoke, payload);
            } else {
                result.childStringKeys &= stringKey(invoke.getArgs()[0]);
                childWrites.emplace_back(invoke, receiver);
                auto key = invoke.getArgs()[0].getDefiningOp<ctjs::ConstantOp>();
                PrimitiveAlternatives alternatives;
                bool leaf =
                    static_cast<bool>(invoke.getArgs()[1].getDefiningOp<ctjs::CreateObjectOp>());
                if (auto value = invoke.getArgs()[1].getDefiningOp<ctjs::ConstantOp>()) {
                    alternatives = PrimitiveAlternatives::forTag(value.getValue().getTypeID());
                } else if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(invoke.getArgs()[1]);
                           argument && argument.getOwner() == &memberBody &&
                           argument.getArgNumber() >= (prepared ? 4u : 3u)) {
                    const auto position = argument.getArgNumber() - (prepared ? 4u : 3u);
                    if (position < independent.alternatives.size()) {
                        alternatives = independent.alternatives[position].categories();
                    }
                    leaf |= llvm::is_contained(independent.objectKeys, argument);
                }
                childLeafProved &= leaf || alternatives.known;
                // Category closure does not require initialization or a fixed key.
                // Deletion and empty publication cannot introduce another category.
                if (!alternatives.tag()) {
                    childScalarProved = false;
                } else if (!childScalar.known) {
                    childScalar = alternatives.categories();
                } else if (!(childScalar == alternatives.categories())) {
                    childScalarProved = false;
                }
                // ponytail: one literal String key and one scalar category; generalize only
                // with a per-key mutation proof when a real family needs more keys.
                if (!key || !llvm::isa<ctjs::StringAttr>(key.getValue()) || !alternatives.tag()) {
                    childEntryProved = false;
                } else if (!childEntry.key) {
                    childEntry = {key.getResult(), alternatives.categories()};
                } else if (childEntry.key.getDefiningOp<ctjs::ConstantOp>().getValue() !=
                               key.getValue() ||
                           !(childEntry.alternatives == alternatives.categories())) {
                    childEntryProved = false;
                }
            }
            return mlir::WalkResult::advance();
        });
        if (census.wasInterrupted() || exhausted) { return {}; }
        for (auto [publication, child] : publications) {
            if (!step()) { return {}; }
            childPublished = true;
            bool seeded = false;
            for (auto [write, receiver] : childWrites) {
                if (!step()) { return {}; }
                auto made = child ? child.getDefiningOp<ctjs::ConstructOp>() : ctjs::ConstructOp{};
                if (made && receiver == child && made->getBlock() == write->getBlock() &&
                    write->getBlock() == publication->getBlock() && made->isBeforeInBlock(write) &&
                    write->isBeforeInBlock(publication)) {
                    seeded = true;
                }
            }
            if (!seeded) { childEntryProved = false; }
        }
    }
    if (result.childMapContents && childEntryProved && childPublished && childEntry.key) {
        result.childEntries.push_back(childEntry);
    }
    if (result.childMapContents && childScalarProved && childPublished && childScalar.known) {
        result.childScalarContents = childScalar;
    }
    result.childLeafContents = result.childMapContents && childLeafProved && childPublished;
    result.childStringKeys &= result.childMapContents;
    // Establish invocation results before joining the complete method census.
    // Two calls to one method may have an acyclic result dependency even when
    // a method-level worklist would wait for its own unpublished result. Each
    // edge must independently pass the entire body/effect proof, starting with
    // unknown Map contents and generalized input categories. Only a completed
    // invocation supplies result evidence; cycles cannot authorize themselves.
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> completedResults;
    llvm::DenseSet<mlir::Operation *> completed;
    while (completed.size() != familyInvocations.size()) {
        bool progress = false;
        for (unsigned index = 0; index < result.parameters.size(); ++index) {
            if (!step()) { return {}; }
            auto member = result.parameters[index].function;
            for (mlir::Operation * invocation : familyCalls[index]) {
                if (!step()) { return {}; }
                if (completed.contains(invocation)) { continue; }
                HostMethodParameters parameters{member, {}};
                if (!capturedMapParameters(member, prepared, {invocation}, familyInvocations,
                                           completedResults, parameters)) {
                    continue;
                }
                // Provisional reads/calls never escape into the family plan.
                HostCapturedMap scratch;
                scratch.childMapContents = result.childMapContents;
                scratch.childEntries = result.childEntries;
                scratch.childScalarContents = result.childScalarContents;
                scratch.childLeafContents = result.childLeafContents;
                scratch.outerStringKeys = result.outerStringKeys;
                scratch.childStringKeys = result.childStringKeys;
                scratch.scalarCallbacks = result.scalarCallbacks;
                PrimitiveAlternatives alternatives;
                if (!capturedMapBody(member, prepared, primitiveContents, parameters, scratch,
                                     alternatives)) {
                    return {};
                }
                if (alternatives.known && (alternatives.truthy | alternatives.falsy)) {
                    completedResults.try_emplace(invocation->getResult(0), alternatives);
                }
                completed.insert(invocation);
                progress = true;
            }
        }
        if (!progress || exhausted) { return {}; }
    }
    // Invocation evidence does not authorize a published method or its sibling
    // family. Recheck every body with ALL actual categories, including later
    // calls and uncalled zero-argument siblings. Only this final census records
    // the owning plan. No result is evaluated or substituted, and every getter
    // and mutation remains in runtime order.
    for (unsigned index = 0; index < result.parameters.size(); ++index) {
        if (!step()) { return {}; }
        auto & parameters = result.parameters[index];
        PrimitiveAlternatives alternatives;
        if (!capturedMapParameters(parameters.function, prepared, familyCalls[index],
                                   familyInvocations, completedResults, parameters, &result) ||
            !capturedMapBody(parameters.function, prepared, primitiveContents, parameters, result,
                             alternatives)) {
            return {};
        }
    }
    // A strict identity guard can give one returned SSA value the caller's
    // initialized own fields on that arm. The payload schema and return carrier
    // alone supply neither identity nor presence; every family effect and caller
    // leaf write above must have closed before this read is published.
    const auto guardedLeafRead = [&](ctjs::GetPropertyOp read) {
        const auto receiver = read.getObject();
        if (!familyInvocations.contains(receiver.getDefiningOp()) ||
            !dominance.dominates(receiver, read) || !dominance.dominates(read.getKey(), read) ||
            !ctjs::ordinaryKey(ctjs::constantKey(read.getKey()))) {
            return false;
        }
        for (auto * child = read.getOperation(); child->getParentOp() != entry;
             child = child->getParentOp()) {
            if (!step()) { return false; }
            auto branch = llvm::dyn_cast<mlir::scf::IfOp>(child->getParentOp());
            if (!branch) { return false; }
            bool positive = child->getParentRegion() == &branch.getThenRegion();
            mlir::Value condition = branch.getCondition();
            for (unsigned depth = 0; depth < 16; ++depth) {
                if (!step() || !dominance.dominates(condition, branch)) { return false; }
                if (auto truthy = condition.getDefiningOp<ctjs::TruthyOp>()) {
                    condition = truthy.getValue();
                } else if (auto unary = condition.getDefiningOp<ctjs::UnaryOp>();
                           unary && unary.getKind() == ctjs::UnaryKind::Not) {
                    positive = !positive;
                    condition = unary.getOperand();
                } else {
                    break;
                }
            }
            auto compare = condition.getDefiningOp<ctjs::CompareOp>();
            if (!positive || !compare || compare.getKind() != ctjs::CompareKind::StrictEq) {
                continue;
            }
            mlir::Value other;
            if (compare.getLhs() == receiver) { other = compare.getRhs(); }
            if (compare.getRhs() == receiver) { other = compare.getLhs(); }
            if (!other || !dominance.dominates(receiver, compare) ||
                !dominance.dominates(other, compare)) {
                continue;
            }
            auto made = other.getDefiningOp<ctjs::CreateObjectOp>();
            if (auto load = other.getDefiningOp<ctjs::LoadGlobalOp>()) {
                const auto origin = objectGlobalRead(load);
                if (!origin) { return false; }
                made = origin->object;
            }
            if (!made || made->getParentOp() != entry) { continue; }
            for (ctjs::SetPropertyOp write : result.leafWrites) {
                if (!step()) { return false; }
                if (write->getParentOp() == entry && object(write.getObject()) == made &&
                    ctjs::constantKey(write.getKey()) == ctjs::constantKey(read.getKey()) &&
                    dominance.properlyDominates(write.getOperation(), read)) {
                    return true;
                }
            }
        }
        return false;
    };
    if (!result.leafWrites.empty()) {
        const auto checked = entry.getBody().walk([&](ctjs::GetPropertyOp read) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (read->getParentOp() != entry && guardedLeafRead(read)) {
                result.leafReads.push_back(read);
            }
            return mlir::WalkResult::advance();
        });
        if (checked.wasInterrupted()) { return {}; }
    }
    llvm::SmallVector<ctjs::GetPropertyOp> unguarded;
    const auto fieldCensus = entry.getBody().walk([&](ctjs::GetPropertyOp read) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (familyInvocations.contains(read.getObject().getDefiningOp()) &&
            !llvm::is_contained(result.leafReads, read)) {
            unguarded.push_back(read);
        }
        return mlir::WalkResult::advance();
    });
    if (fieldCensus.wasInterrupted()) { return {}; }
    bool scalarStore = false;
    if (unguarded.empty()) {
        const auto stores = entry.getBody().walk([&](ctjs::StoreGlobalOp store) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            // An already scalar generalized result needs no entry refinement.
            scalarStore |= familyInvocations.contains(store.getValue().getDefiningOp()) &&
                           !completedResults.lookup(store.getValue()).tag();
            return mlir::WalkResult::advance();
        });
        if (stores.wasInterrupted()) { return {}; }
    }
    if (!unguarded.empty() || scalarStore) {
        // Complete reusable effects above remain independent of these optional
        // entry-order facts. Never carry mutable state through the category DAG:
        // its iteration order groups calls by method, not by execution order.
        CapturedMapInvocation invocation;
        invocation.root = result.allocation.getResult();
        auto & initial = invocation.states[{invocation.root, nullptr}];
        initial.completeKeys = true;
        initial.currentSize = 0;
        std::vector<HostReturnedLeaf> leaves;
        std::vector<HostReturnedScalar> scalars;
        unsigned visited = 0;
        bool complete = true;
        for (mlir::Operation & operation : entry.getBody().front()) {
            if (!step()) { return {}; }
            if (!familyInvocations.contains(&operation)) { continue; }
            ctjs::FuncOp member;
            for (unsigned index = 0; index < familyCalls.size(); ++index) {
                if (!step()) { return {}; }
                if (llvm::is_contained(familyCalls[index], &operation)) {
                    member = result.parameters[index].function;
                    break;
                }
            }
            HostMethodParameters parameters{member, {}};
            if (!member || !capturedMapParameters(member, prepared, {&operation}, familyInvocations,
                                                  completedResults, parameters)) {
                complete = false;
                break;
            }
            invocation.call = &operation;
            invocation.arguments.clear();
            for (auto parameter :
                 member.getBody().front().getArguments().drop_front(prepared ? 4u : 3u)) {
                if (!step()) { return {}; }
                auto actual = explicitArgument(&operation, parameter.getArgNumber());
                if (auto load = actual.getDefiningOp<ctjs::LoadGlobalOp>()) {
                    if (auto object = objectGlobalRead(load)) {
                        actual = object->object.getResult();
                    }
                }
                invocation.arguments[parameter] = actual;
            }
            HostCapturedMap scratch;
            scratch.childMapContents = result.childMapContents;
            scratch.childEntries = result.childEntries;
            scratch.childScalarContents = result.childScalarContents;
            scratch.childLeafContents = result.childLeafContents;
            scratch.outerStringKeys = result.outerStringKeys;
            scratch.childStringKeys = result.childStringKeys;
            scratch.scalarCallbacks = result.scalarCallbacks;
            PrimitiveAlternatives alternatives;
            if (!capturedMapBody(member, prepared, primitiveContents, parameters, scratch,
                                 alternatives, &invocation)) {
                complete = false;
                break;
            }
            ++visited;
            auto leaf = invocation.returnedLeaf
                            ? invocation.returnedLeaf.getDefiningOp<ctjs::CreateObjectOp>()
                            : ctjs::CreateObjectOp{};
            if (leaf && leaf->getParentOp() == entry &&
                dominance.properlyDominates(leaf.getOperation(), &operation)) {
                leaves.push_back({&operation, leaf});
            }
            if (alternatives.known && (alternatives.truthy | alternatives.falsy)) {
                if (!step()) { return {}; }
                scalars.push_back({&operation, alternatives});
            }
        }
        if (complete && visited == familyInvocations.size()) {
            result.returnedLeaves = std::move(leaves);
            result.returnedScalars = std::move(scalars);
            for (ctjs::GetPropertyOp read : unguarded) {
                if (!step()) { return {}; }
                if (!ctjs::ordinaryKey(ctjs::constantKey(read.getKey())) ||
                    !dominance.dominates(read.getObject(), read) ||
                    !dominance.dominates(read.getKey(), read)) {
                    continue;
                }
                for (const auto & leaf : result.returnedLeaves) {
                    if (!step()) { return {}; }
                    if (leaf.call != read.getObject().getDefiningOp()) { continue; }
                    for (ctjs::SetPropertyOp write : result.leafWrites) {
                        if (!step()) { return {}; }
                        if (write->getParentOp() == entry &&
                            object(write.getObject()) == leaf.object &&
                            ctjs::constantKey(write.getKey()) == ctjs::constantKey(read.getKey()) &&
                            dominance.properlyDominates(write.getOperation(), read)) {
                            result.leafReads.push_back(read);
                            break;
                        }
                    }
                }
            }
        }
    }
    if (exhausted || !capturedMapOuterKeys(prepared, familyCalls, result)) { return {}; }
    // Only the completed whole-family proof supplies entry expression facts.
    // The invocation worklist above uses its own map, so provisional or cyclic
    // dependencies cannot borrow these published facts to prove themselves.
    for (const auto & [value, alternatives] : completedResults) {
        if (!step()) { return {}; }
        capturedResults[value] = alternatives.categories();
    }
    return result;
}

bool analyzer::capturedMapCalls(ctjs::FuncOp function, ctjs::SetPropertyOp publication,
                                bool prepared, llvm::SmallVectorImpl<mlir::Operation *> & calls) {
    if (!function || !step()) { return false; }
    auto & body = function.getBody().front();
    const auto census = module.walk([&](mlir::Operation * operation) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
        auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
        if (!direct && !call) { return mlir::WalkResult::advance(); }
        auto callee = direct ? direct.getCalleeValue() : call.getCallee();
        auto read = callee.getDefiningOp<ctjs::GetPropertyOp>();
        if (!read || currentWrite(read) != publication) {
            return direct && target(direct) == function ? mlir::WalkResult::interrupt()
                                                        : mlir::WalkResult::advance();
        }
        const auto args = direct ? direct.getArgs() : call.getArgs();
        const auto receiver = direct ? direct.getReceiver() : call.getReceiver();
        const auto owner = object(publication.getObject());
        if (!owner || !before(read, operation) || object(receiver) != owner ||
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

PrimitiveAlternatives analyzer::entryCategories(
    mlir::Value value, const llvm::DenseMap<mlir::Value, PrimitiveAlternatives> & results,
    mlir::Operation * consumer, unsigned depth, std::vector<mlir::Value> * dependencies) {
    if (!value || depth > 64 || !step()) { return {}; }
    auto * definition = value.getDefiningOp();
    // A source-order anchor can order global effects across a selected arm or
    // an invocation. It cannot make an SSA value visible outside its region.
    if (!definition || (consumer && (definition->getParentOfType<ctjs::FuncOp>() !=
                                         consumer->getParentOfType<ctjs::FuncOp>() ||
                                     !dominance.properlyDominates(definition, consumer)))) {
        return {};
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
    if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(definition)) {
        switch (binary.getKind()) {
        case ctjs::BinaryKind::Add:
        case ctjs::BinaryKind::Sub:
        case ctjs::BinaryKind::Mul:
        case ctjs::BinaryKind::Div:
        case ctjs::BinaryKind::Mod:
        case ctjs::BinaryKind::Pow: break;
        default: return {};
        }
        const auto number = mlir::TypeID::get<ctjs::NumberAttr>();
        if (entryCategories(binary.getLhs(), results, binary, depth + 1, dependencies).tag() !=
                number ||
            entryCategories(binary.getRhs(), results, binary, depth + 1, dependencies).tag() !=
                number) {
            return {};
        }
        // This is a category, never an evaluated Number. Zero, signed zero,
        // NaN and infinities remain possible; primitive()/truth() must not
        // consume this evidence to select a source arm or substitute a value.
        return PrimitiveAlternatives::forTag(number);
    }
    if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(definition)) {
        if (!step() || !dominance.dominates(branch.getCondition(), branch)) { return {}; }
        auto result = llvm::cast<mlir::OpResult>(value);
        std::optional<PrimitiveAlternatives> joined;
        // A category is not a branch predicate. Both live yield operands must
        // prove their own scope and category, even for a constant condition.
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
    // Literal-backed scalar expressions have no published-call dependency.
    // Their original operands, sole stores and source scope/order still pass
    // the same bounded category walk, and publication still requires the
    // complete environment proof. An empty list supplies no value or type.
    // The worklist only supplies categories. Require the actual completed
    // calls in the final environment census too, with entry order and scope.
    for (mlir::Value dependency : result.dependencies) {
        if (!step()) { return std::nullopt; }
        auto * call = dependency.getDefiningOp();
        if (!call || call->getParentOp() != entry || !dominance.properlyDominates(call, store)) {
            return std::nullopt;
        }
        bool found = false;
        for (const HostCallableEdge & edge : checkedCalls) {
            if (!step()) { return std::nullopt; }
            found |= edge.call == call && edge.capturedMap.has_value();
        }
        if (!found) { return std::nullopt; }
    }
    return result;
}

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

ctjs::CreateObjectOp analyzer::object(mlir::Value value, unsigned depth) {
    if (!value || depth > 64 || !step() || !evaluating.insert(value).second) { return {}; }
    const llvm::scope_exit done([&] { evaluating.erase(value); });
    if (auto made = value.getDefiningOp<ctjs::CreateObjectOp>()) {
        // An allocation site denotes one object only along a unique invocation
        // path. A sole factory call inside a repeatedly called wrapper still
        // allocates distinct objects; never merge those property histories.
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
        auto operand = primitive(unary.getOperand(), depth + 1);
        llvm::StringRef name;
        if (llvm::isa_and_nonnull<ctjs::UndefinedAttr>(operand)) {
            name = "undefined";
        } else if (llvm::isa_and_nonnull<ctjs::StringAttr>(operand)) {
            name = "string";
        } else if (llvm::isa_and_nonnull<ctjs::BooleanAttr>(operand)) {
            name = "boolean";
        } else if (llvm::isa_and_nonnull<ctjs::NumberAttr>(operand)) {
            name = "number";
        } else if (llvm::isa_and_nonnull<ctjs::NullAttr>(operand) ||
                   object(unary.getOperand(), depth + 1)) {
            name = "object";
        } else if (callable(unary.getOperand(), depth + 1)) {
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
        // UMD branch predicates compare typeof strings. No coercion, NaN,
        // signed-zero or object equality inference is hidden in this helper.
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

std::optional<bool> analyzer::truth(mlir::Value value, unsigned depth) {
    if (!value || depth > 64 || !step()) { return {}; }
    if (auto truthy = value.getDefiningOp<ctjs::TruthyOp>()) {
        return truth(truthy->getOperand(0), depth + 1);
    }
    if (llvm::isa_and_nonnull<mlir::arith::TruncIOp, mlir::arith::ExtUIOp>(value.getDefiningOp())) {
        return truth(value.getDefiningOp()->getOperand(0), depth + 1);
    }
    auto constant = primitive(value, depth + 1);
    if (auto bit = llvm::dyn_cast_if_present<ctjs::BooleanAttr>(constant)) {
        return bit.getValue();
    }
    if (auto integer = llvm::dyn_cast_if_present<mlir::IntegerAttr>(constant)) {
        if (integer.getValue().isZero() || integer.getValue().isOne()) {
            return integer.getValue().isOne();
        }
    }
    if (llvm::isa_and_nonnull<ctjs::NullAttr, ctjs::UndefinedAttr>(constant)) { return false; }
    if (auto text = llvm::dyn_cast_if_present<ctjs::StringAttr>(constant)) {
        return !text.getValue().empty();
    }
    if (object(value, depth + 1) || callable(value, depth + 1)) { return true; }
    return {};
}

} // namespace ctcompile::ctnative::host_detail
