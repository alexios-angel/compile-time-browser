#include "Proof.hpp"

#include "llvm/ADT/ScopeExit.h"

namespace ctcompile::ctnative::class_detail {
namespace {

// `record` and every retained alias recorded for it, one step per recorded alias.
bool recordAliases(classInitialization & proof, mlir::Value record,
                   llvm::SmallVectorImpl<mlir::Value> & aliases) {
    aliases.push_back(record);
    for (auto [alias, owner] : proof.retainedRecordOrigins) {
        if (!proof.step()) { return false; }
        if (owner == record) { aliases.push_back(alias); }
    }
    return true;
}

// `use` is the stored value of a checked Map set.
bool mapSetValue(classInitialization & proof, mlir::OpOperand & use) {
    auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
    return call && use.getOperandNumber() == 3 && proof.mapOperations.contains(call) &&
           ctjs::constantKey(call.getCallee().getDefiningOp<ctjs::GetPropertyOp>().getKey()) ==
               "set";
}

} // namespace

bool classInitialization::forwardDOMDataFields(const HostContract & contract) {
    auto entry = module.lookupSymbol<ctjs::FuncOp>(contract.entry);
    // ponytail: fixed fields on method-free local records. Mutated fields,
    // inheritance and method receivers need a wider read-time identity proof.
    for (auto function : module.getOps<ctjs::FuncOp>()) {
        if (!step()) { return false; }
        if (!constructors.contains(function) || !llvm::hasSingleElement(function.getBody())) {
            continue;
        }
        llvm::SmallVector<ctjs::ConstructOp> instances;
        bool local = true;
        if (!walkSteps(module, [&](ctjs::ConstructOp made) {
                auto closure = sourceValue(made.getCallee()).getDefiningOp<ctjs::CreateClosureOp>();
                if (target(closure) != function) { return mlir::WalkResult::advance(); }
                local &= made->getParentOp() == entry &&
                         !baseClasses.contains(closure.getResult()) &&
                         !heritage.contains(closure.getResult());
                for (auto [instance, definition] : methodProbes) {
                    (void)definition;
                    if (!step()) { return mlir::WalkResult::interrupt(); }
                    local &= instance != made;
                }
                instances.push_back(made);
                return mlir::WalkResult::advance();
            })) {
            return false;
        }
        if (!local || instances.empty()) { continue; }
        auto & body = function.getBody().front();
        const auto self = body.getArgument(ctjs::arg_receiver);
        for (auto write : llvm::make_early_inc_range(body.getOps<ctjs::SetPropertyOp>())) {
            if (!step()) { return false; }
            auto parameter = llvm::dyn_cast<mlir::BlockArgument>(write.getValue());
            if (write.getObject() != self || !ctjs::ordinaryKey(write.getKey()) || !parameter ||
                parameter.getOwner() != &body ||
                parameter.getArgNumber() < ctjs::implicit_arguments) {
                continue;
            }
            const auto index = parameter.getArgNumber() - ctjs::implicit_arguments;
            const auto key = ctjs::constantKey(write.getKey());
            bool fixed = true;
            for (mlir::OpOperand & use : parameter.getUses()) {
                if (!step()) { return false; }
                fixed &= llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                         (use.getOwner() == write && use.getOperandNumber() == 2);
            }
            for (mlir::OpOperand & use : self.getUses()) {
                if (!step()) { return false; }
                if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
                fixed &= store && use.getOperandNumber() == 0 &&
                         ctjs::ordinaryKey(store.getKey()) &&
                         (store == write || ctjs::constantKey(store.getKey()) != key);
            }
            llvm::MapVector<mlir::Operation *, mlir::Value> reads;
            for (ctjs::ConstructOp made : instances) {
                if (!step()) { return false; }
                auto actual = index < made.getArgs().size() ? sourceValue(made.getArgs()[index])
                                                            : mlir::Value{};
                if (!declaredElementInput(contract, entry,
                                          llvm::dyn_cast_if_present<mlir::BlockArgument>(actual))) {
                    fixed = false;
                    break;
                }
                llvm::SmallVector<mlir::Value> aliases;
                if (!recordAliases(*this, made.getResult(), aliases)) { return false; }
                for (mlir::Value alias : aliases) {
                    for (mlir::OpOperand * use : sourceUses(alias)) {
                        if (!step()) { return false; }
                        auto * op = use->getOwner();
                        if (op->getParentOfType<ctjs::FuncOp>() != entry ||
                            (op->getBlock() == made->getBlock()
                                 ? !made->isBeforeInBlock(op)
                                 : !llvm::isa<ctjs::GetPropertyOp>(op))) {
                            fixed = false;
                            continue;
                        }
                        if (llvm::isa<ctjs::RootOp>(op) || mapSetValue(*this, *use)) { continue; }
                        const auto selected = ctjs::propertyKey(op);
                        if (use->getOperandNumber() != 0 || !selected ||
                            !ctjs::ordinaryKey(selected)) {
                            fixed = false;
                        } else if (ctjs::constantKey(selected) == key) {
                            if (llvm::isa<ctjs::GetPropertyOp>(op)) {
                                reads[op] = actual;
                            } else {
                                fixed = false;
                            }
                        }
                    }
                }
            }
            if (!fixed) { continue; }
            // Only the field disappears, never its record or retained aliases.
            // The exposed element uses still owe the complete Map-key census.
            for (auto [op, actual] : reads) {
                if (!step()) { return false; }
                auto read = llvm::cast<ctjs::GetPropertyOp>(op);
                for (auto & [cell, initial] : cells) {
                    (void)cell;
                    if (!step()) { return false; }
                    if (initial == read.getResult()) { initial = actual; }
                }
                read.getResult().replaceAllUsesWith(actual);
                read.erase();
            }
            write.erase();
            for (ctjs::ConstructOp made : instances) {
                if (!step()) { return false; }
                mlir::OpBuilder at(made);
                auto absent = undefinedAt(at, made.getLoc());
                made.getArgsMutable()[index].set(absent);
            }
        }
    }
    // The provider requires direct input uses. Retire only already-proved
    // immutable entry cells with no capture or other transport observer.
    for (auto [cell, initial] : llvm::make_early_inc_range(cells)) {
        if (!step()) { return false; }
        auto actual = sourceValue(initial);
        if (cell.getDefiningOp()->getParentOp() != entry ||
            !declaredElementInput(contract, entry,
                                  llvm::dyn_cast_if_present<mlir::BlockArgument>(actual))) {
            continue;
        }
        bool local = true;
        for (mlir::Operation * user : cell.getUsers()) {
            if (!step()) { return false; }
            local &= cellOperations.contains(user);
        }
        if (!local) { continue; }
        for (ctjs::CellGetOp read : cellReads) {
            if (!step()) { return false; }
            if (read.getCell() == cell) { read.getResult().replaceAllUsesWith(actual); }
        }
        retireCell(cell);
    }
    return reason.empty();
}

bool classInitialization::proveDOMDataScalars(host_detail::analyzer & analysis) {
    auto entry = analysis.entry;
    llvm::DenseMap<mlir::Value, mlir::Value> origins;
    llvm::DenseMap<mlir::Value, llvm::StringMap<PrimitiveAlternatives>> initialFields;
    // ponytail: only literal/formal constructor stores and explicit entry overwrites.
    // Constructor expressions and method effects need their own read-time proof.
    for (ctjs::ConstructOp made : entry.getBody().front().getOps<ctjs::ConstructOp>()) {
        if (!step()) { return false; }
        auto closure = sourceValue(made.getCallee()).getDefiningOp<ctjs::CreateClosureOp>();
        auto constructor = target(closure);
        if (!constructor || !constructors.contains(constructor) ||
            heritage.contains(closure.getResult())) {
            continue;
        }
        auto & body = constructor.getBody().front();
        const auto primitive = [&](mlir::Value value) -> mlir::Attribute {
            if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(value)) {
                if (argument.getOwner() != &body ||
                    argument.getArgNumber() < ctjs::implicit_arguments) {
                    return {};
                }
                const unsigned index = argument.getArgNumber() - ctjs::implicit_arguments;
                if (index >= made.getArgs().size()) { return {}; }
                value = made.getArgs()[index];
            }
            auto literal = sourceValue(value).getDefiningOp<ctjs::ConstantOp>();
            return literal && ctjs::isPrimitiveAttr(literal.getValue()) ? literal.getValue()
                                                                        : mlir::Attribute{};
        };
        llvm::StringMap<PrimitiveAlternatives> initialized;
        bool safe = true, returned = false;
        for (mlir::Operation & op : body) {
            if (!step()) { return false; }
            if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
                auto value = primitive(write.getValue());
                safe &= write.getObject() == body.getArgument(ctjs::arg_receiver) &&
                        ctjs::ordinaryKey(write.getKey()) && value;
                if (value) {
                    initialized[ctjs::constantKey(write.getKey())] =
                        PrimitiveAlternatives::forTag(value.getTypeID());
                }
            } else if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(op)) {
                returned = true;
                // A returned formal could replace this allocation at another site.
                auto literal = result.getValue().getDefiningOp<ctjs::ConstantOp>();
                safe &= literal && ctjs::isPrimitiveAttr(literal.getValue());
            } else {
                safe &= llvm::isa<ctjs::ConstantOp, ctjs::RootOp, ctjs::FrameEnterOp,
                                  ctjs::FrameExitOp>(op);
            }
        }
        if (!safe || !returned) { continue; }
        llvm::SmallVector<mlir::Value> aliases;
        if (!recordAliases(*this, made.getResult(), aliases)) { return false; }
        for (mlir::Value alias : aliases) {
            for (mlir::OpOperand * use : sourceUses(alias)) {
                if (!step()) { return false; }
                auto * op = use->getOwner();
                if (op->getParentOfType<ctjs::FuncOp>() != entry ||
                    (op->getParentOp() != entry && !llvm::isa<ctjs::GetPropertyOp>(op))) {
                    safe = false;
                }
                if (llvm::isa<ctjs::RootOp>(op) || mapSetValue(*this, *use)) { continue; }
                const auto key = ctjs::propertyKey(op);
                safe &= use->getOperandNumber() == 0 && key && ctjs::ordinaryKey(key);
            }
        }
        if (!safe) { continue; }
        initialFields[made.getResult()] = std::move(initialized);
        for (mlir::Value alias : aliases) {
            if (!step()) { return false; }
            origins[alias] = made.getResult();
        }
    }
    llvm::DenseMap<mlir::Value, llvm::StringMap<PrimitiveAlternatives>> fields;
    const llvm::DenseMap<mlir::Value, PrimitiveAlternatives> noResults;
    // The complete Map census proves these operations' categories, not their
    // values. In particular, has/delete evidence cannot select a source arm.
    for (mlir::Operation * op : mapOperations) {
        if (!step()) { return false; }
        if (op->getParentOfType<ctjs::FuncOp>() != entry) { continue; }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
            auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            const auto key = ctjs::constantKey(read.getKey());
            if (key == "has" || key == "delete") {
                analysis.classScalarReads[call.getResult()] =
                    PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>());
            } else if (key == "clear") {
                analysis.classScalarReads[call.getResult()] =
                    PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::UndefinedAttr>());
            }
        } else if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
                   read && ctjs::constantKey(read.getKey()) == "size") {
            analysis.classScalarReads[read.getResult()] =
                PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::NumberAttr>());
        }
    }
    // Alias validation above forbids nested writes. Postorder visits both
    // read-only arms before a scalar join, retaining each read-time field tag.
    return walkSteps(entry,
                     [&](mlir::Operation * op) {
                         if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(op)) {
                             if (auto initial = initialFields.find(made.getResult());
                                 initial != initialFields.end()) {
                                 fields[made.getResult()] = std::move(initial->second);
                             }
                         } else if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
                             auto owner = origins.lookup(sourceValue(write.getObject()));
                             if (!owner) { return mlir::WalkResult::advance(); }
                             analysis.remaining = remaining;
                             std::vector<mlir::Value> dependencies;
                             auto category = analysis.entryCategories(write.getValue(), noResults,
                                                                      write, 0, &dependencies);
                             remaining = analysis.remaining;
                             // Other records need their own complete use census. Only literals,
                             // their scalar expressions and already checked class reads qualify.
                             if (!dependencies.empty()) { category = {}; }
                             fields[owner][ctjs::constantKey(write.getKey())] = category;
                         } else if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
                             auto owner = origins.lookup(sourceValue(read.getObject()));
                             if (!owner) { return mlir::WalkResult::advance(); }
                             auto category = fields[owner].lookup(ctjs::constantKey(read.getKey()));
                             if (category.tag()) {
                                 analysis.classScalarReads[read.getResult()] = category;
                             }
                         }
                         return mlir::WalkResult::advance();
                     }) &&
           reason.empty() && !analysis.exhausted;
}

bool classInitialization::proveDOMDataFamily(const HostContract & contract) {
    auto entry = module.lookupSymbol<ctjs::FuncOp>(contract.entry);
    llvm::SmallVector<std::pair<mlir::BlockArgument, mlir::OpOperand *>> uses;
    for (mlir::BlockArgument input :
         entry.getBody().front().getArguments().drop_front(ctjs::implicit_arguments)) {
        for (mlir::OpOperand * use : sourceUses(input)) {
            if (!step()) { return false; }
            if (!llvm::isa<ctjs::RootOp>(use->getOwner())) { uses.emplace_back(input, use); }
        }
    }
    if (!reason.empty() || uses.empty()) { return reason.empty(); }
    // These facts authorize source effects only, never native storage. The
    // provider and owned-global analyses still reprove the rewritten module.
    host_detail::analyzer analysis(module, contract, remaining);
    remaining = analysis.remaining;
    if (!proveDOMDataScalars(analysis)) {
        return refuse("class initialization work budget exhausted");
    }
    analysis.remaining = remaining;
    const llvm::scope_exit recordWork([&] { remaining = analysis.remaining; });
    const auto reject = [&] {
        return refuse(analysis.exhausted
                          ? "class initialization work budget exhausted"
                          : "class DOM input has an observer outside its proved Map keys");
    };
    if (contract.roots.size() != 1 || contract.roots.front().properties.size() != 1) {
        return reject();
    }
    auto slot = analysis.slot(contract.roots.front(), contract.roots.front().properties[0]);
    if (!slot.reason.empty() || slot.writes.size() != 1) { return reject(); }
    llvm::DenseMap<mlir::Operation *, HostCallableEdge> edges;
    ctjs::ConstructOp allocation;
    for (auto [input, use] : uses) {
        if (!analysis.step()) { return reject(); }
        auto found = edges.find(use->getOwner());
        if (found == edges.end()) {
            auto edge = analysis.propertyCall(use->getOwner());
            if (!edge || !edge->capturedMap) { return reject(); }
            found = edges.try_emplace(use->getOwner(), std::move(*edge)).first;
        }
        auto & edge = found->second;
        auto & capture = *edge.capturedMap;
        const unsigned offset =
            (llvm::isa<ctjs::CallDirectOp>(edge.call) ? 3u : 2u) + (capture.argument ? 1u : 0u);
        if (use->getOperandNumber() < offset ||
            use->getOperandNumber() - offset >= edge.arguments.size()) {
            return reject();
        }
        const auto & argument = edge.arguments[use->getOperandNumber() - offset];
        if (argument.element != input || argument.actual != use->get() ||
            !llvm::is_contained(capture.outerKeyParameters, argument.parameter) ||
            !llvm::is_contained(capture.outerKeyInputs, input) ||
            (allocation && allocation != capture.allocation)) {
            return reject();
        }
        allocation = capture.allocation;
        auto table = edge.read.getObject().getDefiningOp<ctjs::GetPropertyOp>();
        if (!table || !llvm::any_of(slot.edges, [&](HostSlotEdge published) {
                return analysis.step() && published.read == table;
            })) {
            return reject();
        }
    }
    for (ctjs::GetPropertyOp table : slot.reads) {
        for (mlir::OpOperand & use : table.getResult().getUses()) {
            if (!analysis.step()) { return reject(); }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            if (!llvm::any_of(edges, [&](const auto & item) {
                    const auto & edge = item.second;
                    return analysis.step() &&
                           ((use.getOwner() == edge.read && use.getOperandNumber() == 0) ||
                            (use.getOwner() == edge.call &&
                             use.getOperandNumber() ==
                                 (llvm::isa<ctjs::CallDirectOp>(edge.call) ? 0u : 1u)));
                })) {
                return reject();
            }
        }
    }
    for (auto & [call, edge] : edges) {
        auto & capture = *edge.capturedMap;
        for (mlir::Operation * op :
             {call, edge.read.getOperation(), capture.intrinsic.getOperation(),
              capture.allocation.getOperation(), capture.cell.getOperation(),
              capture.initialization.getOperation(), capture.argument.getOperation()}) {
            if (!analysis.step()) { return reject(); }
            if (op) { domDataOperations.insert(op); }
        }
        for (ctjs::CreateClosureOp closure : capture.closures) {
            if (!analysis.step()) { return reject(); }
            domDataOperations.insert(closure);
            // capturedMap publishes only after every complete reusable body,
            // invocation and outer-key observer has passed its own census.
            auto member = analysis.callable(closure.getResult());
            if (!member) { return reject(); }
            member.walk([&](mlir::Operation * op) {
                if (analysis.step()) { domDataOperations.insert(op); }
            });
        }
    }
    auto factory = allocation->getParentOfType<ctjs::FuncOp>();
    auto * factoryCall = slot.writes.front().getValue().getDefiningOp();
    auto wrapper = slot.writes.front()->getParentOfType<ctjs::FuncOp>();
    if (!factoryCall || wrapper == entry || wrapper == factory ||
        analysis.target(factoryCall) != factory || !analysis.exactCall(factoryCall) ||
        analysis.callers[wrapper].size() != 1) {
        return reject();
    }
    auto wrapperCall = llvm::dyn_cast<ctjs::CallDirectOp>(analysis.callers[wrapper].front());
    if (!wrapperCall || wrapperCall->getParentOfType<ctjs::FuncOp>() != entry ||
        !undefined(wrapperCall.getReceiver()) || !undefined(wrapperCall.getNewTarget()) ||
        !analysis.exactCall(wrapperCall)) {
        return reject();
    }
    for (unsigned index : {ctjs::arg_receiver, ctjs::arg_new_target}) {
        for (mlir::Operation * user : wrapper.getBody().front().getArgument(index).getUsers()) {
            if (!analysis.step() || !llvm::isa<ctjs::RootOp>(user)) { return reject(); }
        }
    }
    // Only the wrapper's signature and exact calls gain authority. Its body
    // and the factory body retain the complete class source-effect census.
    domDataOperations.insert(wrapper);
    domDataOperations.insert(wrapperCall);
    domDataOperations.insert(factoryCall);
    module.walk([&](ctjs::LoadGlobalOp load) {
        const auto & stores = analysis.globals[load.getName()];
        if (analysis.step() && stores.size() == 1 && analysis.before(stores.front(), load) &&
            analysis.object(load.getResult()) == slot.owner) {
            domDataOperations.insert(load);
        }
    });
    if (analysis.exhausted) { return refuse("class initialization work budget exhausted"); }
    return true;
}

} // namespace ctcompile::ctnative::class_detail
