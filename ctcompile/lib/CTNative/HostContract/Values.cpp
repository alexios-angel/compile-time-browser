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
            if (!argument || argument.getArgNumber() != 3) { return; }
            auto wrapper = llvm::dyn_cast<ctjs::FuncOp>(argument.getOwner()->getParentOp());
            if (!wrapper || wrapper == entry || wrapper.getUpvalueCount() != 0 ||
                !llvm::hasSingleElement(wrapper.getBody()) ||
                argument.getOwner() != &wrapper.getBody().front() ||
                argument.getOwner()->getNumArguments() != 4 || callers[wrapper].size() != 1) {
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
    if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
        return mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, call.getCalleeAttr());
    }
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
    const auto key = keyOf(read.getKey());
    if (!owner || !ordinaryKey(key)) { return {}; }
    ctjs::SetPropertyOp latest;
    bool uncertain = false;
    module.walk([&](ctjs::SetPropertyOp write) {
        if (!step() || keyOf(write.getKey()) != key ||
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
    HostCallableEdge edge{operation, read, write, closure, function, capture, {}};
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
            edge.arguments.push_back({function.getBody().front().getArgument(3 + offset + index),
                                      arguments[offset + index], parameters->alternatives[index]});
        }
    }
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
            direct.getArgs().size() != body.getNumArguments() - 3) {
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
                (table && table != owner) || !ordinaryKey(keyOf(write.getKey())) ||
                !fields.insert(keyOf(write.getKey())).second || !owner->isBeforeInBlock(write) ||
                !made->isBeforeInBlock(write)) {
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
    // Once any sibling can allocate a leaf object, unknown Map reads cannot
    // inherit the primitive-only contents guarantee. Inspect the complete
    // current family before proving even one invocation result. The body
    // proof below still independently checks every allocation and its uses.
    bool primitiveContents = true;
    for (auto & parameters : result.parameters) {
        const auto census = parameters.function.getBody().walk([&](mlir::Operation * operation) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (llvm::isa<ctjs::CreateObjectOp>(operation)) { primitiveContents = false; }
            return mlir::WalkResult::advance();
        });
        if (census.wasInterrupted()) { return {}; }
    }
    // Establish invocation results before joining the complete method census.
    // Two calls to one method may have an acyclic result dependency even when
    // a method-level worklist would wait for its own unpublished result. Each
    // edge must independently pass the entire body/effect proof, starting with
    // unknown Map contents and generalized input categories. Only a completed
    // invocation supplies result evidence; cycles cannot authorize themselves.
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> completedResults;
    llvm::DenseSet<mlir::Operation *> completed;
    std::size_t invocationCount = 0;
    for (const auto & invocations : familyCalls) {
        if (!step()) { return {}; }
        invocationCount += invocations.size();
    }
    while (completed.size() != invocationCount) {
        bool progress = false;
        for (unsigned index = 0; index < result.parameters.size(); ++index) {
            if (!step()) { return {}; }
            auto member = result.parameters[index].function;
            for (mlir::Operation * invocation : familyCalls[index]) {
                if (!step()) { return {}; }
                if (completed.contains(invocation)) { continue; }
                HostMethodParameters parameters{member, {}};
                if (!capturedMapParameters(member, prepared, {invocation}, completedResults,
                                           parameters)) {
                    continue;
                }
                // Provisional reads/calls never escape into the family plan.
                HostCapturedMap scratch;
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
                                   completedResults, parameters) ||
            !capturedMapBody(parameters.function, prepared, primitiveContents, parameters, result,
                             alternatives)) {
            return {};
        }
    }
    if (exhausted) { return {}; }
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
            args.size() != body.getNumArguments() - 3 || (prepared && !direct) ||
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

bool analyzer::capturedMapParameters(
    ctjs::FuncOp function, bool prepared, llvm::ArrayRef<mlir::Operation *> calls,
    const llvm::DenseMap<mlir::Value, PrimitiveAlternatives> & results,
    HostMethodParameters & result) {
    const unsigned count = function.getBody().front().getNumArguments() - (prepared ? 4u : 3u);
    std::optional<std::vector<PrimitiveAlternatives>> found;
    for (mlir::Operation * operation : calls) {
        if (!step()) { return false; }
        auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
        const auto args = direct ? direct.getArgs() : llvm::cast<ctjs::CallOp>(operation).getArgs();
        std::vector<PrimitiveAlternatives> tags;
        for (mlir::Value actual : args.drop_front(prepared ? 1u : 0u)) {
            if (!step()) { return false; }
            auto value = primitive(actual);
            if (llvm::isa_and_nonnull<ctjs::NumberAttr, ctjs::BooleanAttr, ctjs::StringAttr,
                                      ctjs::NullAttr, ctjs::UndefinedAttr>(value)) {
                tags.push_back(PrimitiveAlternatives::forTag(value.getTypeID()));
            } else if (auto known = results.find(actual); known != results.end()) {
                if (!before(actual.getDefiningOp(), operation)) { return false; }
                tags.push_back(known->second.categories());
            } else {
                return false;
            }
        }
        if (found) {
            if (found->size() != tags.size()) { return false; }
            for (unsigned index = 0; index < tags.size(); ++index) {
                if (!step()) { return false; }
                tags[index] = tags[index].joined((*found)[index]);
            }
        }
        found = std::move(tags);
    }
    // An uncalled zero-argument sibling can still have its effects checked;
    // the owning plan separately requires current calls for the full family.
    if (exhausted || (!found && count != 0)) { return false; }
    if (found) {
        for (const auto & alternatives : *found) {
            if (!step()) { return false; }
            const auto mask = alternatives.truthy | alternatives.falsy;
            // Extend the established exact primitive boundary only to the
            // existing nullable String family. Unknown/empty sets and other
            // heterogeneous parameters still lack a supported proof here.
            constexpr unsigned nullableString = PrimitiveAlternatives::String |
                                                PrimitiveAlternatives::Null |
                                                PrimitiveAlternatives::Undefined;
            if (!alternatives.known || !mask ||
                (!alternatives.tag() &&
                 (!(mask & PrimitiveAlternatives::String) || (mask & ~nullableString)))) {
                return false;
            }
        }
    }
    result.alternatives = found ? std::move(*found) : std::vector<PrimitiveAlternatives>{};
    return true;
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
        if (stores.empty() && llvm::is_contained(contract.undefinedBindings, load.getName())) {
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
        if (auto load = unary.getOperand().getDefiningOp<ctjs::LoadGlobalOp>();
            load && globals[load.getName()].empty() &&
            llvm::is_contained(contract.absentBindings, load.getName())) {
            return ctjs::StringAttr::get(context, "undefined");
        }
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
