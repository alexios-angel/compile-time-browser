//===- DOMData.cpp - DOM Data Map proofs ------------------------*- C++ -*-===//
//
// Class-initialization proofs for the "DOM Data" pattern: a Map keyed by DOM
// elements behind a factory and a wrapper function, as in Bootstrap's Data
// module. forwardDOMDataFields replaces constructor fields that only hold an
// element input with that input; proveDOMDataScalars records the primitive
// kind of each field and Map-method result read in the entry function;
// proveDOMDataFamily checks that element inputs reach nothing but the
// captured Map's calls and marks those operations as domDataOperations.
//
//===----------------------------------------------------------------------===//

#include "Proof.hpp"

#include "llvm/ADT/ScopeExit.h"

namespace ctcompile::ctnative::class_detail {
namespace {

/// Collects `record` and every retained alias of it into `aliases`, charging
/// one work-budget step per retained-origin entry scanned. Returns false when
/// the budget runs out.
bool recordAliases(classInitialization & proof, mlir::Value record,
                   llvm::SmallVectorImpl<mlir::Value> & aliases) {
    aliases.push_back(record);
    for (auto [alias, owner] : proof.retainedRecordOrigins) {
        if (!proof.step()) { return false; }
        if (owner == record) { aliases.push_back(alias); }
    }
    return true;
}

/// True if `use` is the stored value (operand 3: the second argument) of a
/// `map.set(key, value)` call already recorded in mapOperations.
bool mapSetValue(classInitialization & proof, mlir::OpOperand & use) {
    auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
    return call && use.getOperandNumber() == 3 && proof.mapOperations.contains(call) &&
           ctjs::constantKey(call.getCallee().getDefiningOp<ctjs::GetPropertyOp>().getKey()) ==
               "set";
}

} // namespace

/// Replaces reads of a constructor field that only ever holds a declared DOM
/// element input with that input, erases the field store, and passes undefined
/// for the now-unused constructor argument. Then forwards reads of immutable
/// entry cells holding an element input the same way.
bool classInitialization::forwardDOMDataFields(const HostContract & contract) {
    auto entry = module.lookupSymbol<ctjs::FuncOp>(contract.entry);
    // ponytail: only never-reassigned fields of objects built in the entry
    // function and never used as method receivers. Mutated fields, inheritance
    // and method receivers would need a proof of which value each read sees.
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
            // Erase only the field (its write and reads); the record and its
            // retained aliases stay. Element inputs that remain exposed must still
            // pass proveDOMDataFamily's check that every use is a proved Map key.
            for (auto [op, actual] : reads) {
                if (!step()) { return false; }
                auto read = llvm::cast<ctjs::GetPropertyOp>(op);
                if (!spend(cells.size())) { return false; }
                retargetCells(read.getResult(), actual);
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
    // The native DOM provider needs element inputs used directly, not through
    // a cell. Replace reads of an entry-function cell that holds an input, but
    // only when the cell is already proved immutable and every operation on it
    // is a recorded cell operation (no closure capture or other use).
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

/// Records in analysis.classScalarReads the primitive kind (boolean, number,
/// string, ...) of each field read on a class instance the entry function builds,
/// and of each Map `has`/`delete`/`clear`/`size` result there. Returns false on
/// refusal or budget exhaustion.
bool classInitialization::proveDOMDataScalars(host_detail::analyzer & analysis) {
    auto entry = analysis.entry;
    llvm::DenseMap<mlir::Value, mlir::Value> origins;
    llvm::DenseMap<mlir::Value, llvm::StringMap<PrimitiveAlternatives>> initialFields;
    // ponytail: only constructor stores of literals or formals, and explicit
    // overwrites in the entry function. Computed constructor stores and method
    // effects would need a proof of which value each read sees.
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
    // The Map check proves the kind of these results, not their values: a
    // has/delete result is known to be a boolean but cannot decide a branch.
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
    // The alias check above allows writes only at the top of the entry
    // function, so branches only read fields. The post-order walk visits both
    // arms of a branch before the join and records the kind each read sees.
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
                             // A value depending on another record would need that record's
                             // own check of every use. Only literals, scalar expressions of
                             // them and already checked class reads qualify.
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

/// Proves that every non-root use of an entry-function element input is an
/// argument used as an outer Map key in a call to a helper that captures one
/// DOM Data Map, reached through the single published table slot, and that the
/// Map is built by a factory called once from a wrapper the entry calls once.
/// Marks the operations involved in domDataOperations.
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
    // These facts only allow the source effects; they say nothing about native
    // storage. The DOM provider and owned-global analyses still check the
    // rewritten module from scratch.
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
            // analysis.propertyCall sets capturedMap only after every helper body,
            // every call and every use of an outer key has been checked.
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
    // Only the wrapper itself and the exact calls are marked. The wrapper and
    // factory bodies still go through censusSource's check of every operation.
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
