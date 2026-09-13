#include "OwnedGlobalRoots.h"

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctnative {
void OwnedGlobalRoots::analyzeMethodTable(mlir::ModuleOp module, const HostContract & contract,
                                          const HostContractAnalysis & host, unsigned maxSteps) {
    const auto spend = [&] {
        if (workSteps == maxSteps) {
            budgetExhausted = true;
            refusal = "owned global method analysis work budget exhausted";
            return false;
        }
        ++workSteps;
        return true;
    };
    const auto reject = [&](llvm::StringRef why) {
        if (refusal.empty()) { refusal = why.str(); }
    };
    const auto & slot = host.slots().front();
    auto owner = slot.owner;
    auto field = slot.writes.front();
    auto * factoryCall = field.getValue().getDefiningOp();
    auto directFactory = llvm::dyn_cast<ctjs::CallDirectOp>(factoryCall);
    auto entry = module.lookupSymbol<ctjs::FuncOp>(contract.entry);
    auto capture = host.callables().empty() ? std::optional<HostCapturedMap>{}
                                            : host.callables().front().capturedMap;
    // An indirect factory has already passed the complete live host proof:
    // its unique wrapper callback produces this exact captured allocation.
    auto factory = directFactory ? directFactory.getTarget()
                                 : (capture ? capture->allocation->getParentOfType<ctjs::FuncOp>()
                                            : ctjs::FuncOp{});
    auto publicationScope = field->getParentOfType<ctjs::FuncOp>();
    auto wrapper = publicationScope != entry ? publicationScope : ctjs::FuncOp{};
    if (!spend()) { return; }
    if (!factory || factory == entry || factory.getBody().empty() ||
        factory.getUpvalueCount() != 0 ||
        factoryCall->getNumOperands() != (directFactory ? 3u : 2u) ||
        factory.getBody().front().getNumArguments() != 3 ||
        owner->getParentOfType<ctjs::FuncOp>() != entry || (!capture && wrapper) ||
        (capture && !wrapper) || factoryCall->getParentOfType<ctjs::FuncOp>() != publicationScope ||
        host.callables().empty()) {
        reject("owned global method table requires one uncaptured factory and visible calls");
        return;
    }

    auto first = host.callables().front();
    auto table = first.write.getObject().getDefiningOp<ctjs::CreateObjectOp>();
    if (!table || table->getParentOfType<ctjs::FuncOp>() != factory) {
        reject("owned global method table requires a fresh factory-local table");
        return;
    }
    if (capture &&
        (capture->allocation->getParentOfType<ctjs::FuncOp>() != factory || wrapper == factory ||
         wrapper.getUpvalueCount() != 0 || wrapper.getBody().empty() ||
         (wrapper.getBody().front().getNumArguments() != 3 &&
          wrapper.getBody().front().getNumArguments() != 4))) {
        reject("owned global captured table requires one wrapper, factory and Map environment");
        return;
    }

    llvm::SmallVector<OwnedGlobalMethod> methods;
    llvm::DenseMap<mlir::Operation *, unsigned> methodIndices;
    llvm::DenseSet<mlir::Operation *> methodFunctions;
    llvm::DenseSet<mlir::Operation *> methodInitializations;
    llvm::DenseSet<mlir::Operation *> methodReads;
    llvm::DenseSet<mlir::Operation *> methodCalls;
    llvm::DenseSet<mlir::Operation *> argumentObjects;
    for (const HostCallableEdge & edge : host.callables()) {
        if (!spend()) { return; }
        auto method = edge.function;
        auto closure = edge.closure;
        auto write = edge.write;
        if (method == entry || method == factory || method == wrapper ||
            closure->getParentOfType<ctjs::FuncOp>() != factory ||
            write->getParentOfType<ctjs::FuncOp>() != factory ||
            write.getObject() != table.getResult() ||
            edge.capturedMap.has_value() != capture.has_value() ||
            (capture && (edge.capturedMap->intrinsic != capture->intrinsic ||
                         edge.capturedMap->allocation != capture->allocation ||
                         edge.capturedMap->cell != capture->cell ||
                         edge.capturedMap->initialization != capture->initialization ||
                         edge.capturedMap->closures != capture->closures ||
                         edge.capturedMap->parameters != capture->parameters ||
                         edge.capturedMap->upvalues != capture->upvalues ||
                         edge.capturedMap->reads != capture->reads ||
                         edge.capturedMap->calls != capture->calls ||
                         edge.capturedMap->snapshotOperations != capture->snapshotOperations ||
                         edge.capturedMap->scalarCallbacks != capture->scalarCallbacks ||
                         edge.capturedMap->returnedLeaves != capture->returnedLeaves ||
                         edge.capturedMap->childMaps != capture->childMaps ||
                         edge.capturedMap->childMapContents != capture->childMapContents ||
                         !(edge.capturedMap->childScalarContents == capture->childScalarContents) ||
                         edge.capturedMap->childLeafContents != capture->childLeafContents ||
                         edge.capturedMap->outerStringKeys != capture->outerStringKeys ||
                         edge.capturedMap->childStringKeys != capture->childStringKeys ||
                         edge.capturedMap->childEntries != capture->childEntries ||
                         edge.capturedMap->returnedChildMaps != capture->returnedChildMaps ||
                         edge.capturedMap->leafObjects != capture->leafObjects ||
                         edge.capturedMap->leafWrites != capture->leafWrites ||
                         edge.capturedMap->leafReads != capture->leafReads)) ||
            edge.call->getParentOfType<ctjs::FuncOp>() != entry) {
            reject("owned global method table has another environment or invocation context");
            return;
        }
        const auto [position, inserted] =
            methodIndices.try_emplace(closure, static_cast<unsigned>(methods.size()));
        if (inserted) {
            if ((!capture && !methods.empty()) || !methodFunctions.insert(method).second ||
                !methodInitializations.insert(write).second) {
                reject("owned global method table requires distinct fixed captured methods");
                return;
            }
            methods.push_back({write, closure, method});
        } else {
            const auto & previous = methods[position->second];
            if (previous.function != method || previous.initialization != write) {
                reject("owned global method identity disagrees across current calls");
                return;
            }
        }
        methodReads.insert(edge.read);
        methodCalls.insert(edge.call);
        for (const HostMethodArgument & argument : edge.arguments) {
            if (!spend()) { return; }
            if (!argument.object) { continue; }
            auto made = argument.object;
            auto load = argument.actual.getDefiningOp<ctjs::LoadGlobalOp>();
            const auto * global = load ? host.objectRead(load) : nullptr;
            if ((made.getResult() != argument.actual &&
                 (!global || global->object != made || global->read != load)) ||
                made->getParentOp() != entry || !made->isBeforeInBlock(edge.call)) {
                reject("owned global object argument lacks its earlier entry allocation");
                return;
            }
            argumentObjects.insert(made);
        }
    }
    if (capture) {
        if (!spend()) { return; }
        if (capture->closures.size() != methods.size()) {
            reject("owned global Map family lacks a current call for every published method");
            return;
        }
        for (ctjs::CreateClosureOp closure : capture->closures) {
            if (!spend()) { return; }
            if (!methodIndices.contains(closure)) {
                reject("owned global Map family has another captured closure");
                return;
            }
        }
    }

    llvm::DenseSet<mlir::Operation *> callbackFunctions, callbackOperations;
    if (capture) {
        for (auto & callback : capture->scalarCallbacks) {
            if (!spend()) { return; }
            if (callback.function == entry || callback.function == factory ||
                callback.function == wrapper || methodFunctions.contains(callback.function) ||
                !callbackFunctions.insert(callback.function).second ||
                callback.owner->getParentOp() != entry ||
                callback.initialization->getParentOp() != entry ||
                callback.write->getParentOp() != entry ||
                callback.closure->getParentOp() != entry ||
                callback.initialization.getValue() != callback.owner.getResult() ||
                callback.write.getObject() != callback.owner.getResult() ||
                callback.write.getValue() != callback.closure.getResult()) {
                reject("scalar callback disagrees with its complete source ownership proof");
                return;
            }
            for (mlir::Operation * operation : callback.operations) {
                if (!spend()) { return; }
                callbackOperations.insert(operation);
            }
            for (mlir::Operation * operation :
                 {callback.owner.getOperation(), callback.initialization.getOperation(),
                  callback.write.getOperation(), callback.closure.getOperation()}) {
                if (!spend()) { return; }
                callbackOperations.insert(operation);
            }
            for (ctjs::LoadGlobalOp load : callback.loads) {
                if (!spend()) { return; }
                callbackOperations.insert(load);
            }
            for (ctjs::GetPropertyOp read : callback.reads) {
                if (!spend()) { return; }
                callbackOperations.insert(read);
            }
            for (mlir::Operation * call : callback.calls) {
                if (!spend()) { return; }
                callbackOperations.insert(call);
            }
        }
    }

    llvm::DenseSet<mlir::Operation *> childMaps;
    if (capture && (!capture->childMaps.empty() || !capture->returnedChildMaps.empty() ||
                    !capture->childEntries.empty() || capture->childMapContents ||
                    capture->childScalarContents.known || capture->childLeafContents ||
                    capture->outerStringKeys || capture->childStringKeys)) {
        mlir::DominanceInfo dominance(module);
        llvm::DenseSet<mlir::Value> outers, children, constructedChildren, returnedChildren;
        llvm::DenseMap<mlir::Value, ctjs::ConstructOp> childOrigins;
        for (ctjs::LoadUpvalueOp load : capture->upvalues) {
            if (!spend()) { return; }
            outers.insert(load.getResult());
        }
        for (const HostMethodParameters & method : capture->parameters) {
            if (!spend()) { return; }
            auto function = method.function;
            auto & body = function.getBody().front();
            if (body.getNumArguments() - method.alternatives.size() == 4) {
                outers.insert(body.getArgument(3));
            }
        }
        for (mlir::Value child : capture->returnedChildMaps) {
            if (!spend()) { return; }
            auto call = child.getDefiningOp<ctjs::CallOp>();
            auto read = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                             : ctjs::GetPropertyOp{};
            if (!read || ctjs::constantKey(read.getKey()) != "get" ||
                !llvm::is_contained(capture->calls, call) ||
                !returnedChildren.insert(child).second) {
                reject("owned returned child Map lacks its distinct checked get call");
                return;
            }
        }
        for (ctjs::ConstructOp made : capture->childMaps) {
            if (!spend()) { return; }
            auto intrinsic = made.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
            auto function = made->getParentOfType<ctjs::FuncOp>();
            if (!childMaps.insert(made).second || made == capture->allocation ||
                !methodFunctions.contains(function) || !intrinsic || intrinsic.getName() != "Map" ||
                intrinsic->getParentOfType<ctjs::FuncOp>() != function ||
                made.getNewTarget() != intrinsic.getResult() || !made.getArgs().empty() ||
                !dominance.dominates(intrinsic.getResult(), made)) {
                reject("owned child Map requires its exact empty method-local constructor");
                return;
            }
            children.insert(made.getResult());
            constructedChildren.insert(made.getResult());
            childOrigins[made.getResult()] = made;
        }
        for (ctjs::ConstructOp made : capture->childMaps) {
            for (mlir::OpOperand & use : made.getCallee().getUses()) {
                if (!spend()) { return; }
                if (!dominance.dominates(made.getCallee(), use.getOwner())) {
                    reject("owned child Map constructor use is outside its source scope");
                    return;
                }
                if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                if (use.getOperandNumber() > 1 || !childMaps.contains(use.getOwner())) {
                    reject("owned child Map constructor has another use");
                    return;
                }
            }
        }
        const auto scalar = [&](mlir::Value value) -> PrimitiveAlternatives {
            if (auto constant = value.getDefiningOp<ctjs::ConstantOp>()) {
                return PrimitiveAlternatives::literal(constant.getValue()).categories();
            }
            auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
            if (!argument) { return {}; }
            for (const auto & method : capture->parameters) {
                if (!spend()) { return {}; }
                auto function = method.function;
                auto & block = function.getBody().front();
                const auto first = block.getNumArguments() - method.alternatives.size();
                if (argument.getOwner() == &block && argument.getArgNumber() >= first) {
                    return method.alternatives[argument.getArgNumber() - first].categories();
                }
            }
            return {};
        };
        PrimitiveAlternatives scalarContents;
        bool childWrite = false, childPublication = false;
        if (capture->childLeafContents && !capture->childMapContents) {
            reject("owned child leaf contents lack their complete outer Map census");
            return;
        }
        if (capture->childStringKeys && !capture->childMapContents) {
            reject("owned child String keys lack their complete outer Map census");
            return;
        }
        if (capture->childScalarContents.known &&
            (!capture->childMapContents || !capture->childScalarContents.tag() ||
             !(capture->childScalarContents == capture->childScalarContents.categories()))) {
            reject("owned child contents lack a complete homogeneous scalar category");
            return;
        }
        // These sets distinguish owning roles, not runtime identities. A
        // present outer get can name a child from an earlier invocation. Only
        // fresh constructors and their fluent aliases establish the universal
        // payload kind; no returned owner supplies allocation or content facts.
        // NativeMap independently derives schemas and presence.
        for (ctjs::CallOp call : capture->calls) {
            if (!spend()) { return; }
            auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (!read || read.getObject() != call.getReceiver() ||
                !llvm::is_contained(capture->reads, read) ||
                !dominance.dominates(read.getResult(), call) ||
                (!outers.contains(call.getReceiver()) && !children.contains(call.getReceiver()))) {
                reject("owned child Map call lacks a checked owning receiver");
                return;
            }
            const auto action = ctjs::constantKey(read.getKey());
            const unsigned arity =
                action == "set" ? 2u : (action == "clear" || action == "keys" ? 0u : 1u);
            if ((action != "set" && action != "get" && action != "has" && action != "delete" &&
                 action != "clear" && action != "keys") ||
                call.getArgs().size() != arity) {
                reject("owned child Map call has another method or arity");
                return;
            }
            if (action == "set") {
                if ((outers.contains(call.getReceiver()) ? capture->outerStringKeys
                                                         : capture->childStringKeys) &&
                    scalar(call.getArgs()[0]).tag() != mlir::TypeID::get<ctjs::StringAttr>()) {
                    reject("owned Map String keys have an unproved insertion");
                    return;
                }
                if (outers.contains(call.getArgs()[1]) || (children.contains(call.getArgs()[1]) &&
                                                           !outers.contains(call.getReceiver()))) {
                    reject("owned child Map payload would add an unchecked ownership edge");
                    return;
                }
                if (capture->childMapContents && outers.contains(call.getReceiver()) &&
                    !constructedChildren.contains(call.getArgs()[1])) {
                    reject("owned child Map contents lack a complete constructor payload "
                           "census");
                    return;
                }
                if (capture->childScalarContents.known) {
                    if (outers.contains(call.getReceiver())) {
                        childPublication = true;
                    } else {
                        const auto payload = scalar(call.getArgs()[1]);
                        if (!payload.tag()) {
                            reject("owned child scalar contents have an unproved write");
                            return;
                        }
                        scalarContents = childWrite ? scalarContents.joined(payload) : payload;
                        childWrite = true;
                    }
                }
                if (capture->childLeafContents) {
                    if (outers.contains(call.getReceiver())) {
                        childPublication = true;
                    } else {
                        const auto value = call.getArgs()[1];
                        bool leaf = scalar(value).known;
                        if (auto made = value.getDefiningOp<ctjs::CreateObjectOp>()) {
                            leaf |= llvm::is_contained(capture->leafObjects, made);
                        }
                        for (const auto & method : capture->parameters) {
                            if (!spend()) { return; }
                            leaf |= llvm::is_contained(method.objectKeys, value);
                        }
                        if (!leaf) {
                            reject("owned child leaf contents have an unproved write");
                            return;
                        }
                    }
                }
                (outers.contains(call.getReceiver()) ? outers : children).insert(call.getResult());
                if (constructedChildren.contains(call.getReceiver())) {
                    constructedChildren.insert(call.getResult());
                    childOrigins[call.getResult()] = childOrigins.lookup(call.getReceiver());
                }
            } else if (action == "get" && returnedChildren.contains(call.getResult())) {
                if (!outers.contains(call.getReceiver())) {
                    reject("owned child Map cannot contain another Map");
                    return;
                }
                children.insert(call.getResult());
            }
        }
        // A content category permits absence; deletion and clear preserve it.
        // It supplies neither a required entry nor a returned-child identity.
        if (capture->childScalarContents.known &&
            (!childPublication || !childWrite ||
             !(scalarContents == capture->childScalarContents))) {
            reject("owned child scalar contents disagree with the complete write census");
            return;
        }
        if (capture->childLeafContents && !childPublication) {
            reject("owned child leaf contents lack a checked publication");
            return;
        }
        if (!capture->childEntries.empty()) {
            if (!spend()) { return; }
            const auto & entry = capture->childEntries.front();
            auto literal = entry.key.getDefiningOp<ctjs::ConstantOp>();
            if (!capture->childMapContents || capture->childEntries.size() != 1 || !literal ||
                !llvm::isa<ctjs::StringAttr>(literal.getValue()) || !entry.alternatives.tag()) {
                reject("owned child invariant lacks its exact scalar key and category");
                return;
            }
            // Recheck the complete mutation census and initialization order.
            // Returned owners establish no seed or allocation identity here.
            llvm::DenseMap<mlir::Value, ctjs::CallOp> seeds;
            for (ctjs::CallOp call : capture->calls) {
                if (!spend()) { return; }
                const auto action = ctjs::constantKey(
                    call.getCallee().getDefiningOp<ctjs::GetPropertyOp>().getKey());
                if (children.contains(call.getReceiver())) {
                    if (action == "delete" || action == "clear") {
                        reject("owned child mutation can remove its required entry");
                        return;
                    }
                    if (action != "set") { continue; }
                    auto key = call.getArgs()[0].getDefiningOp<ctjs::ConstantOp>();
                    if (!key || key.getValue() != literal.getValue() ||
                        !(scalar(call.getArgs()[1]) == entry.alternatives.categories())) {
                        reject("owned child mutation does not preserve its required entry");
                        return;
                    }
                    if (auto origin = childOrigins.lookup(call.getReceiver());
                        origin && origin->getBlock() == call->getBlock()) {
                        seeds[origin.getResult()] = call;
                    }
                } else if (action == "set") {
                    auto origin = childOrigins.lookup(call.getArgs()[1]);
                    auto seed = origin ? seeds.lookup(origin.getResult()) : ctjs::CallOp{};
                    if (!origin || !seed || origin->getBlock() != call->getBlock() ||
                        seed->getBlock() != call->getBlock() || !origin->isBeforeInBlock(seed) ||
                        !seed->isBeforeInBlock(call)) {
                        reject("owned child publication precedes its definite initialization");
                        return;
                    }
                }
            }
        }
        for (ctjs::GetPropertyOp read : capture->reads) {
            if (!spend()) { return; }
            if (!outers.contains(read.getObject()) && !children.contains(read.getObject())) {
                reject("owned child Map property lacks its checked owning role");
                return;
            }
        }
        for (mlir::Value child : children) {
            for (mlir::OpOperand & use : child.getUses()) {
                if (!spend()) { return; }
                auto * user = use.getOwner();
                auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                auto call = llvm::dyn_cast<ctjs::CallOp>(user);
                const bool property =
                    read && use.getOperandNumber() == 0 && llvm::is_contained(capture->reads, read);
                auto method = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                                   : ctjs::GetPropertyOp{};
                const bool invocation =
                    call && llvm::is_contained(capture->calls, call) &&
                    (use.getOperandNumber() == 1 ||
                     (use.getOperandNumber() == 3 && outers.contains(call.getReceiver()) &&
                      method && ctjs::constantKey(method.getKey()) == "set"));
                if (!dominance.dominates(child, user) ||
                    (!llvm::isa<ctjs::RootOp>(user) && !property && !invocation)) {
                    reject("owned child Map has another alias, key or publication use");
                    return;
                }
            }
        }
    }

    llvm::DenseSet<mlir::Operation *> observations;
    if (capture) {
        llvm::DenseSet<mlir::Operation *> returnedCalls;
        for (const auto & returned : capture->returnedLeaves) {
            if (!spend()) { return; }
            auto object = returned.object;
            if (!returned.call || !methodCalls.contains(returned.call) ||
                !returnedCalls.insert(returned.call).second ||
                returned.call->getNumResults() != 1 || !object ||
                !argumentObjects.contains(object) || object->getParentOp() != entry ||
                returned.call->getParentOp() != entry || !object->isBeforeInBlock(returned.call)) {
                reject("owned returned leaf lacks its exact current call and caller allocation");
                return;
            }
        }
        for (ctjs::GetPropertyOp read : capture->leafReads) {
            if (!spend()) { return; }
            if (read->getParentOfType<ctjs::FuncOp>() != entry) { continue; }
            for (auto * parent = read->getParentOp(); parent != entry;
                 parent = parent->getParentOp()) {
                if (!spend()) { return; }
                if (!llvm::isa<mlir::scf::IfOp>(parent)) {
                    reject("owning entry field read has unsupported control");
                    return;
                }
                observations.insert(parent);
            }
        }
    }
    llvm::SmallVector<mlir::Operation *> operations;
    unsigned functions = 0;
    module.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * operation) {
        if (!spend()) { return mlir::WalkResult::interrupt(); }
        if (operation == module.getOperation()) { return mlir::WalkResult::advance(); }
        if (auto function = llvm::dyn_cast<ctjs::FuncOp>(operation)) {
            ++functions;
            if ((function != entry && function != factory && !methodFunctions.contains(function) &&
                 function != wrapper && !callbackFunctions.contains(function)) ||
                function->getParentOp() != module || !llvm::hasSingleElement(function.getBody()) ||
                function->hasAttr("ctjs.skipped")) {
                reject("owned global method table requires its exact straight-line source "
                       "functions");
            }
        } else {
            // Only the complete live captured-body proof authorizes control
            // inside a method. Allocation, publication and entry calls retain
            // their unconditional source-order requirements below.
            const bool capturedBody =
                capture && (methodFunctions.contains(operation->getParentOfType<ctjs::FuncOp>()) ||
                            callbackFunctions.contains(operation->getParentOfType<ctjs::FuncOp>()));
            // Only pure observation regions around checked owning field reads
            // are admitted at entry. Calls, allocation and publication still
            // require their unconditional source positions.
            bool observation = observations.contains(operation);
            // Pure entry observations may select scalar results, including
            // comparisons of checked method results. Both arms are censused;
            // calls, stores and allocations still need unconditional positions.
            bool scalarObservation =
                operation->getParentOfType<ctjs::FuncOp>() == entry &&
                llvm::isa<ctjs::ConstantOp, ctjs::RootOp, ctjs::TruthyOp, ctjs::CompareOp,
                          ctjs::UnaryOp, mlir::scf::IfOp, mlir::scf::YieldOp>(operation);
            if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
                scalarObservation &= compare.getKind() == ctjs::CompareKind::StrictEq;
            }
            if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation)) {
                scalarObservation &= unary.getKind() == ctjs::UnaryKind::Not;
            }
            for (auto * parent = operation->getParentOp(); scalarObservation && parent != entry;
                 parent = parent->getParentOp()) {
                if (!spend()) { return mlir::WalkResult::interrupt(); }
                scalarObservation = llvm::isa<mlir::scf::IfOp>(parent);
            }
            observation |= scalarObservation;
            if (observations.contains(operation->getParentOp())) {
                observation |=
                    llvm::isa<ctjs::ConstantOp, ctjs::RootOp, ctjs::TruthyOp, mlir::scf::YieldOp>(
                        operation);
                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                    observation |= llvm::is_contained(capture->leafReads, read);
                }
                if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
                    observation |= compare.getKind() == ctjs::CompareKind::StrictEq;
                }
                if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation)) {
                    observation |= unary.getKind() == ctjs::UnaryKind::Not;
                }
            }
            if (!capturedBody && !observation &&
                (!llvm::isa<ctjs::FuncOp>(operation->getParentOp()) ||
                 operation->getNumRegions() != 0)) {
                reject("owned global method table requires unconditional straight-line "
                       "operations");
            }
            operations.push_back(operation);
        }
        return refusal.empty() ? mlir::WalkResult::advance() : mlir::WalkResult::interrupt();
    });
    if (!refusal.empty()) { return; }
    if (functions != methods.size() + callbackFunctions.size() + (wrapper ? 3u : 2u)) {
        reject("owned global method table requires its exact source function chain");
        return;
    }

    ctjs::StoreGlobalOp initialization;
    llvm::SmallVector<ctjs::LoadGlobalOp> loads;
    ctjs::ReturnOp factoryReturn;
    ctjs::CallDirectOp wrapperCall;
    ctjs::ReturnOp wrapperReturn;
    for (mlir::Operation * operation : operations) {
        if (!spend()) { return; }
        if (callbackOperations.contains(operation)) { continue; }
        if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(operation);
            made && (!capture || (made != capture->allocation && !childMaps.contains(made)))) {
            reject("owned global method table has another constructor");
        }
        if (auto made = llvm::dyn_cast<ctjs::CreateObjectOp>(operation)) {
            if (made != owner && made != table && !argumentObjects.contains(made) &&
                (!capture || !llvm::is_contained(capture->leafObjects, made))) {
                reject("owned global method table has another allocation");
            }
        }
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation);
            store && store.getName() == slot.binding) {
            if (initialization || store.getValue() != owner.getResult() ||
                store->getParentOp() != entry) {
                reject("owned global method binding must publish its sole source allocation once");
            }
            initialization = store;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation);
            load && load.getName() == slot.binding) {
            if (load->getParentOp() != entry && load->getParentOp() != wrapper) {
                reject("owned global method binding cannot be read by another activation");
            }
            loads.push_back(load);
        }
        if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
            if (write != field && !methodInitializations.contains(write) &&
                (!capture || !llvm::is_contained(capture->leafWrites, write))) {
                reject("owned global method fields cannot be replaced or extended");
            }
        }
        if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
            if (!llvm::is_contained(slot.reads, read) && !methodReads.contains(read) &&
                (!capture || (!llvm::is_contained(capture->reads, read) &&
                              !llvm::is_contained(capture->leafReads, read) &&
                              !llvm::is_contained(capture->snapshotOperations, operation)))) {
                reject("owned global method field read lacks a complete live callable edge");
            }
        }
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
            wrapper && call && call.getCallee() == wrapper.getSymName()) {
            if (wrapperCall || call->getParentOp() != entry ||
                call->getNumOperands() != wrapper.getBody().front().getNumArguments()) {
                reject("owned global wrapper requires exactly one entry invocation");
            }
            wrapperCall = call;
        }
        if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(operation) && operation != factoryCall &&
            operation != wrapperCall.getOperation() && !methodCalls.contains(operation) &&
            (!capture ||
             (!llvm::is_contained(capture->calls, llvm::dyn_cast<ctjs::CallOp>(operation)) &&
              !llvm::is_contained(capture->snapshotOperations, operation)))) {
            reject("owned global method table has another call or factory invocation");
        }
        if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation);
            returned && returned->getParentOp() == factory) {
            if (factoryReturn || returned.getValue() != table.getResult()) {
                reject("owned global factory must return its initialized table directly");
            }
            factoryReturn = returned;
        }
        if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation);
            wrapper && returned && returned->getParentOp() == wrapper) {
            auto value = returned.getValue().getDefiningOp<ctjs::ConstantOp>();
            if (wrapperReturn || !value || !llvm::isa<ctjs::UndefinedAttr>(value.getValue())) {
                reject("owned global wrapper must return undefined after publication");
            }
            wrapperReturn = returned;
        }
        if (!refusal.empty()) { return; }
    }
    if (!initialization || !factoryReturn || !owner->isBeforeInBlock(initialization) ||
        !factoryCall->isBeforeInBlock(field) ||
        (wrapper &&
         (!wrapperCall || !wrapperReturn || !initialization->isBeforeInBlock(wrapperCall) ||
          !field->isBeforeInBlock(wrapperReturn)))) {
        reject("owned global method table lacks unconditional allocation and initialization order");
        return;
    }

    for (const OwnedGlobalMethod & method : methods) {
        if (!spend()) { return; }
        if (!table->isBeforeInBlock(method.initialization) ||
            !method.closure->isBeforeInBlock(method.initialization) ||
            !method.initialization->isBeforeInBlock(factoryReturn)) {
            reject("owned global method table has an incomplete method initialization order");
            return;
        }
    }

    llvm::DenseSet<mlir::Value> owners{owner.getResult()};
    for (ctjs::LoadGlobalOp load : loads) {
        if (!spend()) { return; }
        auto * position =
            load->getParentOp() == wrapper ? wrapperCall.getOperation() : load.getOperation();
        if (!initialization->isBeforeInBlock(position) ||
            (load->getParentOp() == wrapper && !load->isBeforeInBlock(field))) {
            reject("owned global method load precedes binding initialization");
            return;
        }
        owners.insert(load.getResult());
    }
    if (!owners.contains(field.getObject()) || ctjs::constantKey(field.getKey()) != slot.property) {
        reject("owned global method publication does not name the checked root field");
        return;
    }
    llvm::DenseSet<mlir::Value> tables{table.getResult(), factoryCall->getResult(0)};
    for (ctjs::GetPropertyOp read : slot.reads) {
        if (!spend()) { return; }
        const auto * edge = host.property(read);
        if (read->getParentOp() != entry || !owners.contains(read.getObject()) || !edge ||
            edge->write != field ||
            !(wrapper ? wrapperCall.getOperation() : field.getOperation())->isBeforeInBlock(read)) {
            reject("owned global method read lacks its sole definite publication");
            return;
        }
        tables.insert(read.getResult());
    }
    for (mlir::Value alias : owners) {
        for (mlir::OpOperand & use : alias.getUses()) {
            if (!spend()) { return; }
            auto * user = use.getOwner();
            if (llvm::isa<ctjs::RootOp>(user) || user == initialization.getOperation()) {
                continue;
            }
            if (use.getOperandNumber() == 0 &&
                (user == field.getOperation() ||
                 llvm::is_contained(slot.reads, llvm::dyn_cast<ctjs::GetPropertyOp>(user)))) {
                continue;
            }
            reject("owned global method owner has another alias or publication use");
            return;
        }
    }
    for (mlir::Value alias : tables) {
        for (mlir::OpOperand & use : alias.getUses()) {
            if (!spend()) { return; }
            auto * user = use.getOwner();
            if (llvm::isa<ctjs::RootOp>(user) || user == factoryReturn.getOperation()) { continue; }
            if (user == field.getOperation() && use.getOperandNumber() == 2) { continue; }
            if (methodInitializations.contains(user) && use.getOperandNumber() == 0 &&
                alias == table.getResult()) {
                continue;
            }
            if (methodReads.contains(user) && use.getOperandNumber() == 0) { continue; }
            if (methodCalls.contains(user) &&
                use.getOperandNumber() == (llvm::isa<ctjs::CallOp>(user) ? 1u : 0u)) {
                continue;
            }
            reject("owned global method table has another alias, inspection or publication use");
            return;
        }
    }
    for (const HostCallableEdge & edge : host.callables()) {
        if (!spend()) { return; }
        auto read = edge.read;
        if (!tables.contains(read.getObject())) {
            reject("owned global callable receiver is outside its owning table family");
            return;
        }
    }

    llvm::SmallVector<HostScalarGlobalRead> scalarReads;
    llvm::DenseMap<mlir::Operation *, unsigned> scalarIndex;
    for (const HostScalarGlobalRead & edge : host.scalarReads()) {
        if (!spend()) { return; }
        auto read = edge.read;
        auto store = edge.initialization;
        if (read->getParentOp() != entry || store->getParentOp() != entry ||
            read.getName() != store.getName() || store.getValue() != edge.value ||
            (edge.alternatives.tag() != mlir::TypeID::get<ctjs::NumberAttr>() &&
             edge.alternatives.tag() != mlir::TypeID::get<ctjs::BooleanAttr>() &&
             edge.alternatives.tag() != mlir::TypeID::get<ctjs::StringAttr>())) {
            reject("saved scalar read disagrees with the complete host proof");
            return;
        }
        // Constant-only scalar origins need no method result. Any results
        // they do use must belong to this complete live owning family.
        for (mlir::Value dependency : edge.dependencies) {
            if (!spend()) { return; }
            if (!methodCalls.contains(dependency.getDefiningOp())) {
                reject("saved scalar result is outside the complete owned method family");
                return;
            }
        }
        scalarIndex[read] = static_cast<unsigned>(scalarReads.size());
        scalarReads.push_back(edge);
    }

    llvm::SmallVector<HostObjectGlobalRead> objectReads;
    llvm::DenseMap<mlir::Operation *, unsigned> objectIndex;
    for (const HostObjectGlobalRead & edge : host.objectReads()) {
        if (!spend()) { return; }
        auto made = edge.object;
        auto store = edge.initialization;
        auto read = edge.read;
        for (const std::string & observation : contract.observations) {
            if (!spend()) { return; }
            if (observation == read.getName()) {
                reject("object key global cannot be a scalar observation");
                return;
            }
        }
        if (!argumentObjects.contains(made) || made->getParentOp() != entry ||
            store->getParentOp() != entry || read->getParentOp() != entry ||
            store.getName() != read.getName() || !made->isBeforeInBlock(store) ||
            !store->isBeforeInBlock(read)) {
            reject("object global read disagrees with the complete key ownership proof");
            return;
        }
        if (store.getValue() != made.getResult()) {
            auto source = store.getValue().getDefiningOp<ctjs::LoadGlobalOp>();
            const auto * predecessor = source ? host.objectRead(source) : nullptr;
            // This complete census revalidates every predecessor too. Each
            // step precedes its store in the entry block, so no cycle can
            // survive and the chain must end at the checked allocation.
            if (!predecessor || predecessor->object != made || source->getParentOp() != entry ||
                !source->isBeforeInBlock(store)) {
                reject("object key alias lacks its earlier source global initialization");
                return;
            }
        }
        for (mlir::Operation * operation : operations) {
            if (!spend()) { return; }
            if (auto other = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation);
                other && other.getName() == store.getName() && other != store) {
                reject("object key global has another source store");
                return;
            }
            if (auto other = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation);
                other && other.getName() == read.getName()) {
                const auto * checkedRead = host.objectRead(other);
                if (!checkedRead || checkedRead->object != made ||
                    checkedRead->initialization != store) {
                    reject("object key global has an unchecked source read");
                    return;
                }
            }
        }
        for (mlir::Value value : {made.getResult(), read.getResult()}) {
            for (mlir::OpOperand & use : value.getUses()) {
                if (!spend()) { return; }
                if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                if (auto initialization = llvm::dyn_cast<ctjs::StoreGlobalOp>(use.getOwner());
                    initialization && use.getOperandNumber() == 0) {
                    bool checked = false;
                    for (const HostObjectGlobalRead & candidate : host.objectReads()) {
                        if (!spend()) { return; }
                        if (candidate.initialization == initialization &&
                            candidate.object == made) {
                            checked = true;
                            break;
                        }
                    }
                    if (checked) { continue; }
                }
                // The complete host proof checks scalar fields and definite own
                // initialization; ownership separately binds each use to this
                // exact caller allocation and its stable global aliases.
                if (use.getOwner()->getParentOp() == entry &&
                    value.getDefiningOp()->isBeforeInBlock(use.getOwner())) {
                    if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
                        write && use.getOperandNumber() == 0 && capture &&
                        llvm::is_contained(capture->leafWrites, write)) {
                        continue;
                    }
                    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
                        read && use.getOperandNumber() == 0 && capture &&
                        llvm::is_contained(capture->leafReads, read)) {
                        continue;
                    }
                    if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(use.getOwner());
                        compare && compare.getKind() == ctjs::CompareKind::StrictEq) {
                        continue;
                    }
                }
                const auto * call = host.callable(use.getOwner());
                if (!call || !methodCalls.contains(use.getOwner()) ||
                    use.getOwner()->getParentOp() != entry ||
                    !value.getDefiningOp()->isBeforeInBlock(use.getOwner())) {
                    reject("object key global has a use outside its owning call family");
                    return;
                }
                const unsigned offset = llvm::isa<ctjs::CallDirectOp>(use.getOwner()) ? 3u : 2u;
                const bool argument = llvm::any_of(call->arguments, [&](const auto & actual) {
                    return actual.actual == value && actual.object == made &&
                           use.getOperandNumber() == actual.parameter.getArgNumber() - 3 + offset;
                });
                if (!argument) {
                    reject("object key global use lacks its exact original actual argument");
                    return;
                }
            }
        }
        const auto index = static_cast<unsigned>(objectReads.size());
        objectIndex[store] = index;
        objectIndex[read] = index;
        objectReads.push_back(edge);
    }

    // The query exposes only source ownership. A later consumer must prove a
    // callable carrier and native call component; no annotation closes them.
    OwnedGlobalRoot result{owner, initialization, std::move(loads), field,
                           {},    slot.binding,   slot.property,    std::nullopt};
    result.reads.append(slot.reads.begin(), slot.reads.end());
    result.methodTable.emplace(OwnedGlobalMethodTable{
        factoryCall, factory, table, std::move(methods), {}, wrapper, wrapperCall, capture});
    result.methodTable->calls.append(host.callables().begin(), host.callables().end());
    llvm::DenseMap<mlir::Operation *, unsigned> committed;
    for (ctjs::LoadGlobalOp load : result.loads) {
        if (!spend()) { return; }
        committed[load] = 0;
    }
    for (ctjs::GetPropertyOp read : result.reads) {
        if (!spend()) { return; }
        committed[read] = 0;
    }
    if (!spend()) { return; }
    committed[owner] = 0;
    committed[initialization] = 0;
    committed[field] = 0;
    llvm::SmallVector<OwnedGlobalRoot, 1> roots;
    roots.push_back(std::move(result));
    if (capture) {
        for (auto & callback : capture->scalarCallbacks) {
            if (!spend()) { return; }
            const auto index = static_cast<unsigned>(roots.size());
            OwnedGlobalRoot root{callback.owner,
                                 callback.initialization,
                                 {},
                                 callback.write,
                                 {},
                                 callback.initialization.getName().str(),
                                 ctjs::constantKey(callback.write.getKey()).str(),
                                 std::nullopt,
                                 callback.function};
            root.loads.append(callback.loads.begin(), callback.loads.end());
            root.reads.append(callback.reads.begin(), callback.reads.end());
            for (mlir::Operation * operation :
                 {callback.owner.getOperation(), callback.initialization.getOperation(),
                  callback.write.getOperation()}) {
                if (!spend()) { return; }
                committed[operation] = index;
            }
            for (ctjs::LoadGlobalOp load : callback.loads) {
                if (!spend()) { return; }
                committed[load] = index;
            }
            for (ctjs::GetPropertyOp read : callback.reads) {
                if (!spend()) { return; }
                committed[read] = index;
            }
            roots.push_back(std::move(root));
        }
    }
    checked = std::move(roots);
    edges = std::move(committed);
    checkedScalarReads = std::move(scalarReads);
    scalarEdges = std::move(scalarIndex);
    checkedObjectReads = std::move(objectReads);
    objectEdges = std::move(objectIndex);
}

} // namespace ctcompile::ctnative
