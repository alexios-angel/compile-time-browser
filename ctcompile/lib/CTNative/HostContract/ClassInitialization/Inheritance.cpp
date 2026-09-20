#include "Proof.hpp"

namespace ctcompile::ctnative::class_detail {

bool classInitialization::proveHeritage(const HostContract & contract) {
    llvm::DenseMap<mlir::Value, ctjs::CallOp> completed;
    for (ctjs::CallOp call : calls) {
        if (!step()) { return false; }
        if (call.getArgs().size() != 1) { continue; }
        auto [at, fresh] = completed.try_emplace(sourceValue(call.getArgs().front()), call);
        if (!fresh) { at->second = {}; }
    }
    const auto walked = module.walk([&](ctjs::CallOp call) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
        if (!load || load.getName() != "__ctbrowser_class_heritage") {
            return mlir::WalkResult::advance();
        }
        if (!llvm::is_contained(contract.initialIntrinsics, load.getName()) ||
            call.getArgs().size() != 3 || !undefined(call.getReceiver()) ||
            !call.getResult().use_empty()) {
            refuse("class heritage needs its declared direct helper and unused result");
            return mlir::WalkResult::interrupt();
        }
        const auto derived = sourceValue(call.getArgs()[0]);
        const auto base = sourceValue(call.getArgs()[1]);
        const auto derivedClosure = derived.getDefiningOp<ctjs::CreateClosureOp>();
        const auto baseClosure = base.getDefiningOp<ctjs::CreateClosureOp>();
        const auto derivedDone = completed.lookup(derived);
        const auto baseDone = completed.lookup(base);
        auto prototype = call.getArgs()[2].getDefiningOp<ctjs::CreateObjectOp>();
        if (derived == base || !target(derivedClosure) || !target(baseClosure) || !derivedDone ||
            !baseDone || !prototype || derivedClosure->getBlock() != call->getBlock() ||
            baseClosure->getBlock() != call->getBlock() ||
            derivedDone->getBlock() != call->getBlock() ||
            baseDone->getBlock() != call->getBlock() || prototype->getBlock() != call->getBlock() ||
            !derivedClosure->isBeforeInBlock(call) || !prototype->isBeforeInBlock(call) ||
            !baseDone->isBeforeInBlock(call) || !call->isBeforeInBlock(derivedDone) ||
            !heritage.try_emplace(derived, call).second) {
            refuse("class heritage needs an earlier completed local base and fresh derived "
                   "prototype");
            return mlir::WalkResult::interrupt();
        }
        // Strict source order rules out cycles. Every base's exact home,
        // prototype, captures and complete use census still pass examine.
        baseClasses.insert(base);
        return mlir::WalkResult::advance();
    });
    return !walked.wasInterrupted();
}

bool classInitialization::heritageUse(mlir::OpOperand & use, mlir::Value constructor) {
    auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
    if (!call || call.getArgs().size() != 3 ||
        heritage.lookup(sourceValue(call.getArgs()[0])) != call) {
        return false;
    }
    return (use.getOperandNumber() == 2 || use.getOperandNumber() == 3) &&
           sourceValue(use.get()) == constructor;
}

bool classInitialization::normalizeSuper(ctjs::CreateClosureOp constructor,
                                         ctjs::CreateClosureOp baseClosure,
                                         const HostContract & contract) {
    auto function = target(constructor);
    auto base = target(baseClosure);
    auto & original = function.getBody().front();
    if (!llvm::hasSingleElement(base.getBody()) ||
        !original.getArgument(ctjs::arg_new_target).use_empty()) {
        return refuse("derived class requires receiver-preserving super normalization");
    }
    for (auto & use : original.getArgument(ctjs::arg_callee).getUses()) {
        if (!step()) { return false; }
        auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(use.getOwner());
        if (use.getOperandNumber() != 0 || (!callableCaptures.contains(use.getOwner()) &&
                                            (!read || (!holderCaptures.contains(read.getResult()) &&
                                                       !mapCaptures.contains(read.getResult()))))) {
            return refuse("derived class requires receiver-preserving super normalization");
        }
    }
    auto returned = llvm::dyn_cast<ctjs::ReturnOp>(base.getBody().front().getTerminator());
    if (!returned || !undefined(returned.getValue())) {
        return refuse("super base requires an undefined constructor return");
    }
    for (llvm::StringRef name : {"__ctbrowser_bind_this", "__ctbrowser_init_fields"}) {
        if (!llvm::is_contained(contract.initialIntrinsics, name)) {
            return refuse("super initialization requires declared helper identities");
        }
    }
    // Other class/helper/global records borrow original operations. Keep
    // this first constructor slice free of declarations/publications instead
    // of invalidating those records while replacing its private body.
    for (auto fn : {function, base}) {
        const auto census = fn.walk([&](mlir::Operation * op) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (llvm::isa<ctjs::CreateClosureOp, ctjs::StoreGlobalOp, ctjs::RootOp>(op)) {
                refuse(
                    "super constructor declarations, roots and global writes remain unsupported");
                return mlir::WalkResult::interrupt();
            }
            return mlir::WalkResult::advance();
        });
        if (census.wasInterrupted()) { return false; }
    }
    ctjs::CreateCellOp guard;
    for (auto cell : original.getOps<ctjs::CreateCellOp>()) {
        if (!step() || guard) { return refuse("super requires one private Boolean guard"); }
        auto value = cell.getInitial().getDefiningOp<ctjs::ConstantOp>();
        auto bit =
            value ? llvm::dyn_cast<ctjs::BooleanAttr>(value.getValue()) : ctjs::BooleanAttr{};
        if (!bit || bit.getValue()) { return refuse("super guard must begin false"); }
        guard = cell;
    }
    if (!guard) { return refuse("derived class requires receiver-preserving super normalization"); }
    for (auto & use : guard.getResult().getUses()) {
        if (!step()) { return false; }
        if (use.getOperandNumber() != 0 ||
            !llvm::isa<ctjs::CellGetOp, ctjs::CellSetOp, ctjs::RootOp>(use.getOwner())) {
            return refuse("super guard escapes its private reads and write");
        }
    }
    auto counted = function.walk([&](mlir::Operation * op) {
        const uint64_t cost = uint64_t(1) + op->getNumOperands() + op->getNumResults();
        if (cost > remaining) {
            refuse("class initialization work budget exhausted");
            return mlir::WalkResult::interrupt();
        }
        remaining -= static_cast<unsigned>(cost);
        return mlir::WalkResult::advance();
    });
    if (counted.wasInterrupted()) { return false; }
    mlir::OwningOpRef<ctjs::FuncOp> copy(llvm::cast<ctjs::FuncOp>(function->cloneWithoutRegions()));
    auto * output = new mlir::Block;
    copy->getBody().push_back(output);
    mlir::IRMapping mapping;
    for (auto argument : original.getArguments()) {
        mapping.map(argument, output->addArgument(argument.getType(), argument.getLoc()));
    }
    mlir::OpBuilder at(function.getContext());
    at.setInsertionPointToEnd(output);
    auto absent = ctjs::ConstantOp::create(at, function.getLoc(),
                                           ctjs::UndefinedAttr::get(module.getContext()));
    llvm::SmallVector<std::pair<ctjs::LoadUpvalueOp, ctjs::FuncOp>> copiedCaptures;
    llvm::SmallVector<std::pair<ctjs::LoadUpvalueOp, mlir::Value>> copiedHolders;
    llvm::SmallVector<std::pair<ctjs::LoadUpvalueOp, mlir::Value>> copiedMaps;
    llvm::SmallVector<mlir::Value> captures(constructor.getUpvalues());
    llvm::SmallVector<mlir::Operation *> copiedHelperCalls;
    const auto clone = [&](mlir::Operation & op, mlir::IRMapping & values) {
        const auto counted = op.walk([&](mlir::Operation * source) {
            const uint64_t cost = uint64_t(1) + source->getNumOperands() + source->getNumResults();
            if (cost > remaining) {
                refuse("class initialization work budget exhausted");
                return mlir::WalkResult::interrupt();
            }
            remaining -= static_cast<unsigned>(cost);
            return mlir::WalkResult::advance();
        });
        if (counted.wasInterrupted()) { return false; }
        at.clone(op, values);
        // A base's slot number belongs to its original closure, never the
        // leaf's environment. Carry the proved helper/holder identity with each copy.
        auto recorded = op.walk([&](mlir::Operation * source) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            auto * copied = values.lookupOrNull(source);
            if (auto helper = callableCaptures.lookup(source)) {
                copiedCaptures.emplace_back(llvm::cast<ctjs::LoadUpvalueOp>(copied), helper);
            }
            if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(source)) {
                if (auto object = holderCaptures.lookup(read.getResult())) {
                    copiedHolders.emplace_back(llvm::cast<ctjs::LoadUpvalueOp>(copied), object);
                }
                if (auto map = mapCaptures.lookup(read.getResult())) {
                    // Slot numbers belong to the declaring closure. Reuse a
                    // leaf slot by Map identity, or carry the base's exact cell.
                    size_t index = 0;
                    while (index < captures.size() &&
                           sourceValue(cells.lookup(captures[index])) != map) {
                        if (!step()) { return mlir::WalkResult::interrupt(); }
                        ++index;
                    }
                    if (index == captures.size()) {
                        if (baseClosure->getBlock() != constructor->getBlock() ||
                            !baseClosure->isBeforeInBlock(constructor)) {
                            refuse("inherited Map capture requires initialization before the leaf "
                                   "closure");
                            return mlir::WalkResult::interrupt();
                        }
                        mlir::Value cell;
                        for (auto candidate : baseClosure.getUpvalues()) {
                            if (!step()) { return mlir::WalkResult::interrupt(); }
                            if (mapCells.contains(candidate) &&
                                sourceValue(cells.lookup(candidate)) == map) {
                                cell = candidate;
                                break;
                            }
                        }
                        if (!cell) {
                            refuse("inherited Map read lacks its original capture cell");
                            return mlir::WalkResult::interrupt();
                        }
                        captures.push_back(cell);
                    }
                    auto load = llvm::cast<ctjs::LoadUpvalueOp>(copied);
                    load->setOperand(0, output->getArgument(ctjs::arg_callee));
                    load.setIndex(static_cast<uint32_t>(index));
                    copiedMaps.emplace_back(load, map);
                }
            }
            if (helperCalls.contains(source)) { copiedHelperCalls.push_back(copied); }
            return mlir::WalkResult::advance();
        });
        return !recorded.wasInterrupted();
    };
    const auto receiver = original.getArgument(ctjs::arg_receiver);
    const auto integer = [&](mlir::Value value) -> std::optional<int64_t> {
        auto constant = mapping.lookupOrDefault(value).getDefiningOp<mlir::arith::ConstantOp>();
        auto number =
            constant ? llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue()) : mlir::IntegerAttr{};
        return number ? std::optional<int64_t>(number.getInt()) : std::nullopt;
    };
    unsigned phase = 0; // base call, guard write, bind-this, fields, completion.
    bool initialized = false, finished = false;
    ctjs::CallOp baseCall;
    llvm::DenseSet<mlir::Block *> visited;
    const auto visit = [&](auto && self, mlir::Block & block,
                           llvm::SmallVectorImpl<mlir::Value> & yielded, unsigned depth) -> bool {
        if (!step() || depth == 64 || !visited.insert(&block).second) {
            return refuse("super initialization has cyclic control flow");
        }
        for (mlir::Operation & op : block) {
            if (!step()) { return false; }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
                const auto bit = integer(branch.getCondition());
                auto truth = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
                auto read =
                    truth ? truth.getValue().getDefiningOp<ctjs::CellGetOp>() : ctjs::CellGetOp{};
                const bool guardBranch = read && read.getCell() == guard.getResult();
                if (!bit || !guardBranch) {
                    if (phase != 4) {
                        return refuse("super initialization has an unproved branch");
                    }
                    // Preserve runtime conditions after the receiver is initialized.
                    // Each arm must return normally to this same super phase.
                    auto * copied = at.cloneWithoutRegions(op, mapping);
                    for (auto [source, destination] :
                         llvm::zip(branch->getRegions(), copied->getRegions())) {
                        if (source.empty()) { continue; }
                        if (!source.hasOneBlock() || source.front().getNumArguments() ||
                            !llvm::isa<mlir::scf::YieldOp>(source.front().getTerminator())) {
                            return refuse("super branch requires a structured yield");
                        }
                        auto * body = new mlir::Block;
                        destination.push_back(body);
                        mlir::OpBuilder::InsertionGuard restore(at);
                        at.setInsertionPointToEnd(body);
                        llvm::SmallVector<mlir::Value> values;
                        if (!self(self, source.front(), values, depth + 1) || phase != 4 ||
                            finished || values.size() != branch.getNumResults()) {
                            return refuse("super branch changes constructor completion");
                        }
                        mlir::scf::YieldOp::create(at, branch.getLoc(), values);
                    }
                    // Equal completion flags/receivers need no runtime selection.
                    // Keep the branch itself and every original source effect.
                    if (branch.getNumResults()) {
                        auto conditional = llvm::cast<mlir::scf::IfOp>(copied);
                        auto yes = conditional.thenYield().getOperands();
                        auto no = conditional.elseYield().getOperands();
                        for (auto [result, left, right] : llvm::zip(branch.getResults(), yes, no)) {
                            if (!step()) { return false; }
                            if (left == right) {
                                mapping.map(result, left);
                                continue;
                            }
                            auto a = left.getDefiningOp<mlir::arith::ConstantOp>();
                            auto b = right.getDefiningOp<mlir::arith::ConstantOp>();
                            if (a && b && left.getType() == right.getType() &&
                                a.getValue() == b.getValue()) {
                                mapping.map(result, at.clone(*a)->getResult(0));
                            }
                        }
                    }
                    continue;
                }
                auto & region = *bit ? branch.getThenRegion() : branch.getElseRegion();
                if (!region.hasOneBlock()) { return refuse("super branch is not linear"); }
                llvm::SmallVector<mlir::Value> values;
                if (!self(self, region.front(), values, depth + 1) ||
                    values.size() != branch.getNumResults()) {
                    return false;
                }
                mapping.map(branch.getResults(), values);
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::scf::IndexSwitchOp>(op)) {
                const auto key = integer(branch.getArg());
                if (!key) { return refuse("super completion has an unproved switch"); }
                auto * region = &branch.getDefaultRegion();
                for (auto [i, value] : llvm::enumerate(branch.getCases())) {
                    if (!step()) { return false; }
                    if (value == *key) { region = &branch.getCaseRegions()[i]; }
                }
                if (!region->hasOneBlock()) { return refuse("super completion is not linear"); }
                llvm::SmallVector<mlir::Value> values;
                if (!self(self, region->front(), values, depth + 1) ||
                    values.size() != branch.getNumResults()) {
                    return false;
                }
                mapping.map(branch.getResults(), values);
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::cf::SwitchOp>(op)) {
                const auto key = integer(branch.getFlag());
                if (!key) { return refuse("super exit has an unproved switch"); }
                auto * destination = branch.getDefaultDestination();
                mlir::ValueRange operands = branch.getDefaultOperands();
                if (auto cases = branch.getCaseValues()) {
                    for (auto [i, value] : llvm::enumerate(cases->getValues<llvm::APInt>())) {
                        if (!step()) { return false; }
                        if (value.getSExtValue() == *key) {
                            destination = branch.getCaseDestinations()[i];
                            operands = branch.getCaseOperands(static_cast<unsigned>(i));
                        }
                    }
                }
                for (auto [argument, value] : llvm::zip(destination->getArguments(), operands)) {
                    mapping.map(argument, mapping.lookupOrDefault(value));
                }
                return self(self, *destination, yielded, depth + 1);
            }
            if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(op)) {
                for (auto value : yield.getOperands()) {
                    yielded.push_back(mapping.lookupOrDefault(value));
                }
                return true;
            }
            if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(op)) {
                if (phase != 4 ||
                    mapping.lookupOrDefault(returned.getValue()) != mapping.lookup(receiver)) {
                    return refuse("derived completion requires its initialized receiver");
                }
                ctjs::ReturnOp::create(at, returned.getLoc(), absent);
                finished = true;
                return true;
            }
            if (&op == guard) { continue; }
            if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(op)) {
                if (read.getCell() != guard.getResult()) {
                    return refuse("super reads an unrelated cell");
                }
                mapping.map(
                    read.getResult(),
                    ctjs::ConstantOp::create(
                        at, read.getLoc(), ctjs::BooleanAttr::get(module.getContext(), initialized))
                        .getResult());
                continue;
            }
            if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(op)) {
                auto value = write.getValue().getDefiningOp<ctjs::ConstantOp>();
                auto bit = value ? llvm::dyn_cast<ctjs::BooleanAttr>(value.getValue())
                                 : ctjs::BooleanAttr{};
                if (phase != 1 || write.getCell() != guard.getResult() || !bit || !bit.getValue()) {
                    return refuse("super guard requires one true write after the base call");
                }
                initialized = true;
                phase = 2;
                continue;
            }
            if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(op)) {
                auto value =
                    mapping.lookupOrDefault(truth.getValue()).getDefiningOp<ctjs::ConstantOp>();
                auto bit = value ? llvm::dyn_cast<ctjs::BooleanAttr>(value.getValue())
                                 : ctjs::BooleanAttr{};
                if (!bit) {
                    if (phase != 4) { return refuse("super condition is not a proved Boolean"); }
                    if (!clone(op, mapping)) { return false; }
                    continue;
                }
                mapping.map(truth.getResult(), mlir::arith::ConstantIntOp::create(
                                                   at, truth.getLoc(), bit.getValue(), 1)
                                                   .getResult());
                continue;
            }
            if (auto cast = llvm::dyn_cast<mlir::arith::IndexCastUIOp>(op)) {
                const auto value = integer(cast.getIn());
                if (!value || *value < 0) { return refuse("super completion index is not proved"); }
                mapping.map(
                    cast.getResult(),
                    mlir::arith::ConstantIndexOp::create(at, cast.getLoc(), *value).getResult());
                continue;
            }
            if (auto pass = llvm::dyn_cast<ctjs::PassNewTargetOp>(op)) {
                if (phase != 0 || !llvm::isa_and_nonnull<ctjs::CallOp>(pass->getNextNode())) {
                    return refuse("super new.target must immediately precede its base call");
                }
                continue;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
                auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                auto proto =
                    read ? read.getObject().getDefiningOp<ctjs::GetProtoOp>() : ctjs::GetProtoOp{};
                if (proto && proto->getOperand(0).getDefiningOp<ctjs::LoadHomeOp>() &&
                    ctjs::constantKey(read.getKey()) == "constructor") {
                    if (phase != 0 || call.getReceiver() != receiver ||
                        !llvm::isa_and_nonnull<ctjs::PassNewTargetOp>(call->getPrevNode()) ||
                        call.getArgs().size() + ctjs::implicit_arguments >
                            base.getBody().front().getNumArguments()) {
                        return refuse("super requires one exact ordered base call");
                    }
                    for (auto & use : call.getResult().getUses()) {
                        if (!step()) { return false; }
                        auto bind = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                        auto load = bind ? bind.getCallee().getDefiningOp<ctjs::LoadGlobalOp>()
                                         : ctjs::LoadGlobalOp{};
                        if (!llvm::isa<ctjs::RootOp>(use.getOwner()) &&
                            (!load || load.getName() != "__ctbrowser_bind_this" ||
                             use.getOperandNumber() != 2)) {
                            return refuse("super result escapes its receiver binding");
                        }
                    }
                    mlir::IRMapping arguments;
                    auto & body = base.getBody().front();
                    arguments.map(body.getArgument(ctjs::arg_receiver), mapping.lookup(receiver));
                    arguments.map(body.getArgument(ctjs::arg_new_target),
                                  mapping.lookup(original.getArgument(ctjs::arg_new_target)));
                    arguments.map(body.getArgument(ctjs::arg_callee), absent);
                    for (auto [i, formal] : llvm::enumerate(
                             body.getArguments().drop_front(ctjs::implicit_arguments))) {
                        if (!step()) { return false; }
                        arguments.map(formal, i < call.getArgs().size()
                                                  ? mapping.lookupOrDefault(call.getArgs()[i])
                                                  : absent.getResult());
                    }
                    for (mlir::Operation & operation : body) {
                        if (!step()) { return false; }
                        if (!llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::ReturnOp>(
                                operation)) {
                            if (!clone(operation, arguments)) { return false; }
                        }
                    }
                    mapping.map(call.getResult(), absent);
                    baseCall = call;
                    phase = 1;
                    continue;
                }
                auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
                if (load && undefined(call.getReceiver()) && call.getResult().use_empty()) {
                    if (load.getName() == "__ctbrowser_bind_this" && phase == 2 &&
                        call.getArgs().size() == 1 && call.getArgs()[0] == baseCall.getResult()) {
                        phase = 3;
                        continue;
                    }
                    if (load.getName() == "__ctbrowser_init_fields" && phase == 3 &&
                        call.getArgs().size() == 2 && call.getArgs()[0] == receiver &&
                        call.getArgs()[1].getDefiningOp<ctjs::LoadHomeOp>()) {
                        phase = 4;
                        continue;
                    }
                }
                // After super, preserve ordinary calls for the complete receiver,
                // callable-holder and source-effect census. It runs on the live
                // normalized body, so no holder record borrows erased operations.
                if (phase == 4 || (phase == 0 && helperCalls.contains(call))) {
                    if (phase == 0 && llvm::is_contained(call.getOperands(), receiver)) {
                        return refuse("derived receiver is used before super initialization");
                    }
                    if (!clone(op, mapping)) { return false; }
                    continue;
                }
                return refuse("super initialization contains an unproved call");
            }
            if (llvm::isa<ctjs::RootOp>(op) && op.getOperand(0) == guard.getResult()) { continue; }
            if (phase != 4 && llvm::is_contained(op.getOperands(), receiver)) {
                return refuse("derived receiver is used before super initialization");
            }
            if (op.getNumRegions() || op.hasTrait<mlir::OpTrait::IsTerminator>()) {
                return refuse("super initialization contains unsupported control flow");
            }
            if (!clone(op, mapping)) { return false; }
        }
        return false;
    };
    llvm::SmallVector<mlir::Value> unused;
    if (!visit(visit, original, unused, 0) || !finished) {
        return refuse("derived class requires receiver-preserving super normalization");
    }
    // Only unobserved super metadata and pure transport can disappear. Any
    // unexpected use keeps the operation for the complete class census.
    for (mlir::Operation & op : llvm::make_early_inc_range(llvm::reverse(*output))) {
        if (!step()) { return false; }
        auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
        const auto proto = read ? read.getObject().getDefiningOp<ctjs::GetProtoOp>()
                                : llvm::dyn_cast<ctjs::GetProtoOp>(op);
        auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op);
        const bool metadata = (proto && proto->getOperand(0).getDefiningOp<ctjs::LoadHomeOp>() &&
                               (!read || ctjs::constantKey(read.getKey()) == "constructor")) ||
                              (load && (load.getName() == "__ctbrowser_bind_this" ||
                                        load.getName() == "__ctbrowser_init_fields"));
        if (op.use_empty() &&
            (metadata || llvm::isa<ctjs::LoadHomeOp, ctjs::ConstantOp, mlir::arith::ConstantOp,
                                   mlir::ub::PoisonOp>(op))) {
            op.erase();
        }
    }
    // Base records still describe live source bodies, including bases shared
    // by several leaves. Only the replaced derived body loses its records.
    llvm::erase_if(captureReads, [&](ctjs::LoadUpvalueOp read) {
        if (!step()) { return false; }
        if (read->getParentOfType<ctjs::FuncOp>() != function) { return false; }
        callableCaptures.erase(read);
        holderCaptures.erase(read.getResult());
        return true;
    });
    auto removed = function.walk([&](mlir::Operation * op) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        helperCalls.erase(op);
        mapOperations.erase(op);
        if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op)) {
            mapCaptures.erase(read.getResult());
        }
        return mlir::WalkResult::advance();
    });
    if (!reason.empty() || removed.wasInterrupted()) { return false; }
    for (auto [read, helper] : copiedCaptures) {
        if (!step()) { return false; }
        captureReads.push_back(read);
        callableCaptures[read] = helper;
    }
    for (auto [read, object] : copiedHolders) {
        if (!step()) { return false; }
        captureReads.push_back(read);
        holderCaptures[read.getResult()] = object;
    }
    helperCalls.insert(copiedHelperCalls.begin(), copiedHelperCalls.end());
    for (auto [read, map] : copiedMaps) {
        if (!step()) { return false; }
        mapCaptures[read.getResult()] = map;
        mapOperations.insert(read);
    }
    if (!copiedMaps.empty()) {
        constructor.getUpvaluesMutable().assign(captures);
        constructor.removeEnclosingIndicesAttr();
        function.setUpvalueCount(static_cast<uint32_t>(captures.size()));
        mapClosures.insert(constructor);
        if (!llvm::is_contained(capturedClosures, constructor)) {
            capturedClosures.push_back(constructor);
        }
    }
    function.getBody().takeBody(copy->getBody());
    return true;
}

bool classInitialization::normalizeSuperMethods(
    ctjs::FuncOp function, llvm::ArrayRef<ctjs::SetPropertyOp> baseDefinitions) {
    llvm::SmallVector<ctjs::CallOp> invocations;
    auto walked = function.walk([&](ctjs::CallOp call) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        invocations.push_back(call);
        return mlir::WalkResult::advance();
    });
    if (walked.wasInterrupted()) { return false; }
    for (ctjs::CallOp call : invocations) {
        auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        auto proto = read ? read.getObject().getDefiningOp<ctjs::GetProtoOp>() : ctjs::GetProtoOp{};
        auto home =
            proto ? proto.getObject().getDefiningOp<ctjs::LoadHomeOp>() : ctjs::LoadHomeOp{};
        if (!home) { continue; }
        if (!step() || !home.getResult().hasOneUse() || !proto.getResult().hasOneUse() ||
            !read.getResult().hasOneUse() || !ctjs::ordinaryKey(read.getKey()) ||
            call.getReceiver() != function.getBody().front().getArgument(ctjs::arg_receiver)) {
            return refuse("super method requires an unobserved lexical lookup and exact receiver");
        }
        ctjs::FuncOp selected;
        // Definitions are nearest-first, including shadowed ancestors. A
        // lexical lookup starts at the declaring home's immediate base,
        // independently of the final receiver's ordinary method table.
        for (ctjs::SetPropertyOp definition : baseDefinitions) {
            if (!step()) { return false; }
            if (ctjs::constantKey(definition.getKey()) == ctjs::constantKey(read.getKey())) {
                selected = target(definition.getValue().getDefiningOp<ctjs::CreateClosureOp>());
                break;
            }
        }
        if (!selected || !methods.contains(selected) || selected.getUpvalueCount() != 0 ||
            !llvm::hasSingleElement(selected.getBody())) {
            return refuse("super method requires a proved capture-free linear base target");
        }
        auto & body = selected.getBody().front();
        auto returned = llvm::dyn_cast<ctjs::ReturnOp>(body.getTerminator());
        if (!returned || !body.getArgument(ctjs::arg_callee).use_empty() ||
            !body.getArgument(ctjs::arg_new_target).use_empty() ||
            call.getArgs().size() + ctjs::implicit_arguments > body.getNumArguments()) {
            return refuse("super method observes its identity, completion or excess arguments");
        }
        // ponytail: expand linear targets at their calls; target CFG/captures
        // need a receiver-preserving direct-call proof in closure lifting.
        for (mlir::Operation & op : body) {
            const uint64_t cost = uint64_t(1) + op.getNumOperands() + op.getNumResults();
            if (cost > remaining) { return refuse("class initialization work budget exhausted"); }
            remaining -= static_cast<unsigned>(cost);
            if (op.getNumRegions() ||
                llvm::isa<ctjs::CreateClosureOp, ctjs::StoreGlobalOp, ctjs::LoadHomeOp,
                          ctjs::RootOp>(op) ||
                (op.hasTrait<mlir::OpTrait::IsTerminator>() && &op != returned.getOperation())) {
                return refuse("super method target has unsupported control flow, roots or "
                              "declarations");
            }
        }
        mlir::OpBuilder at(call);
        auto absent = ctjs::ConstantOp::create(at, call.getLoc(),
                                               ctjs::UndefinedAttr::get(module.getContext()));
        mlir::IRMapping mapping;
        mapping.map(body.getArgument(ctjs::arg_receiver), call.getReceiver());
        mapping.map(body.getArgument(ctjs::arg_new_target), absent);
        mapping.map(body.getArgument(ctjs::arg_callee), absent);
        for (auto [i, formal] :
             llvm::enumerate(body.getArguments().drop_front(ctjs::implicit_arguments))) {
            if (!step()) { return false; }
            mapping.map(formal, i < call.getArgs().size() ? call.getArgs()[i] : absent.getResult());
        }
        // Argument effects already precede the original call. Preserve every
        // target operation there and keep the original body for the complete
        // source census; fieldsOnly will recheck the expanded leaf receiver.
        if (snapshotMethods.contains(selected)) { snapshotMethods.insert(function); }
        for (mlir::Operation & op : body) {
            if (!llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::ReturnOp>(op)) {
                at.clone(op, mapping);
            }
        }
        call.getResult().replaceAllUsesWith(mapping.lookupOrDefault(returned.getValue()));
        call.erase();
        read.erase();
        proto.erase();
        home.erase();
    }
    return true;
}

bool classInitialization::examine(ctjs::CallOp call, const HostContract & contract, bool domEntry) {
    if (!step() || call.getArgs().size() != 1 || !undefined(call.getReceiver()) ||
        !call.getResult().use_empty()) {
        return refuse("class helper needs one local constructor and an unused result");
    }
    auto closure = call.getArgs().front().getDefiningOp<ctjs::CreateClosureOp>();
    auto function = target(closure);
    if (!function || !undefined(closure.getEnclosingThis()) ||
        closure->getBlock() != call->getBlock() || !closure->isBeforeInBlock(call)) {
        return refuse("class constructor lacks a local ordinary closure identity");
    }
    ctjs::SetPropertyOp attachment, home, backedge;
    llvm::SmallVector<ctjs::ConstructOp> instances;
    llvm::StringMap<ctjs::DefineAccessorOp> staticDefinitions;
    llvm::StringMap<ctjs::SetPropertyOp> staticMethods;
    llvm::SmallVector<ctjs::CallOp> staticInvocations;
    llvm::SmallVector<ctjs::GetPropertyOp> staticReads;
    llvm::SmallVector<ctjs::SetPropertyOp> getterHomes;
    // Prove original slots once, before super expansion mixes base and leaf
    // operations. The copied reads retain these exact helper identities.
    if (!closure.getUpvalues().empty() &&
        !methodCaptures(closure, closure, staticReads, domEntry)) {
        return false;
    }
    for (mlir::OpOperand * sourceUse : sourceUses(closure.getResult())) {
        auto & use = *sourceUse;
        if (!step()) { return false; }
        auto * op = use.getOwner();
        if (op == call && use.getOperandNumber() == 2) { continue; }
        if (llvm::isa<ctjs::RootOp>(op)) { continue; }
        if (heritageUse(use, closure.getResult())) { continue; }
        if (auto invocation = llvm::dyn_cast<ctjs::CallOp>(op)) {
            auto read = invocation.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (use.getOperandNumber() == 1 && read &&
                sourceValue(read.getObject()) == closure.getResult() &&
                invocation->getBlock() == call->getBlock() && call->isBeforeInBlock(invocation)) {
                staticInvocations.push_back(invocation); // Exact slot checked after collection.
                continue;
            }
        }
        if (auto definition = llvm::dyn_cast<ctjs::DefineAccessorOp>(op)) {
            if (use.getOperandNumber() != 0 || definition->getBlock() != call->getBlock() ||
                !definition->isBeforeInBlock(call) ||
                !staticDefinitions.try_emplace(definition.getName(), definition).second) {
                return refuse("static accessor setup is repeated or outside initialization");
            }
            continue;
        }
        if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
            if (use.getOperandNumber() != 0 || read->getBlock() != call->getBlock() ||
                !call->isBeforeInBlock(read)) {
                return refuse("static getter read lacks completed local initialization");
            }
            staticReads.push_back(read);
            continue;
        }
        if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(op)) {
            if (use.getOperandNumber() >= 2 ||
                sourceValue(made.getCallee()) != closure.getResult() ||
                sourceValue(made.getNewTarget()) != closure.getResult() ||
                made->getBlock() != call->getBlock() || !call->isBeforeInBlock(made)) {
                return refuse("class construction escapes or lacks exact local new.target");
            }
            if (use.getOperandNumber() == 0) { instances.push_back(made); }
            continue;
        }
        auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
        if (!write || write->getBlock() != call->getBlock() || !write->isBeforeInBlock(call)) {
            auto invocation = llvm::dyn_cast<ctjs::CallOp>(op);
            auto load = invocation ? invocation.getCallee().getDefiningOp<ctjs::LoadGlobalOp>()
                                   : ctjs::LoadGlobalOp{};
            if (load && load.getName() == "__ctbrowser_class_heritage") {
                return refuse("class inheritance requires proved heritage, receiver and "
                              "super initialization");
            }
            return refuse("class constructor has an observable use outside its setup");
        }
        const auto key = ctjs::constantKey(write.getKey());
        if (use.getOperandNumber() == 0 && key == "prototype" && !attachment) {
            attachment = write;
        } else if (use.getOperandNumber() == 0 && key == "__home" && !home) {
            home = write;
        } else if (use.getOperandNumber() == 2 && key == "constructor" && !backedge) {
            backedge = write;
        } else if (use.getOperandNumber() == 2 && key == "__home") {
            getterHomes.push_back(write); // Rechecked against exact getters below.
        } else if (use.getOperandNumber() == 0 && ctjs::ordinaryKey(write.getKey())) {
            if (key == "name" || key == "length" || key == "__home" || key == "caller" ||
                key == "arguments" || !staticMethods.try_emplace(key, write).second) {
                return refuse("static method shadows closure metadata or repeats a slot");
            }
        } else {
            return refuse("class methods, static fields or repeated setup remain unsupported");
        }
    }
    auto prototype = attachment ? attachment.getValue().getDefiningOp<ctjs::CreateObjectOp>()
                                : ctjs::CreateObjectOp{};
    if (!prototype || !home || !backedge ||
        (instances.empty() && !baseClasses.contains(closure.getResult())) ||
        prototype->getBlock() != call->getBlock() || home.getValue() != prototype.getResult() ||
        backedge.getObject() != prototype.getResult()) {
        return refuse("class setup needs its exact fresh prototype, constructor and home");
    }
    if (auto inherited = heritage.lookup(closure.getResult());
        inherited && inherited.getArgs()[2] != prototype.getResult()) {
        return refuse("class heritage prototype differs from its attached prototype");
    }
    const auto firstConstructorRead = constructorReads.size();
    llvm::StringSet<> methodKeys;
    llvm::SmallVector<ctjs::SetPropertyOp> definitions;
    for (mlir::OpOperand & use : prototype.getResult().getUses()) {
        if (!step()) { return false; }
        auto * op = use.getOwner();
        if ((op == attachment || op == home) && use.getOperandNumber() == 2) { continue; }
        if (op == backedge && use.getOperandNumber() == 0) { continue; }
        if (llvm::isa<ctjs::RootOp>(op)) { continue; }
        if (op == heritage.lookup(closure.getResult()) && use.getOperandNumber() == 4) { continue; }
        auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
        if (!write || write->getBlock() != call->getBlock() || !write->isBeforeInBlock(call)) {
            return refuse("class prototype is observed or mutated outside initialization");
        }
        if (use.getOperandNumber() == 2 && ctjs::constantKey(write.getKey()) == "__home") {
            continue; // Rechecked against the exact method closure below.
        }
        if (use.getOperandNumber() != 0 || !ctjs::ordinaryKey(write.getKey()) ||
            !methodKeys.insert(ctjs::constantKey(write.getKey())).second) {
            return refuse("class prototype needs unique ordinary method definitions");
        }
        definitions.push_back(write);
    }
    const bool hasOwnStaticGetters = !staticDefinitions.empty();
    llvm::SmallVector<ctjs::SetPropertyOp> baseDefinitions, selectedBaseDefinitions;
    if (auto inherited = heritage.lookup(closure.getResult())) {
        auto base = inheritedMethods.find(sourceValue(inherited.getArgs()[1]));
        if (base == inheritedMethods.end()) {
            return refuse("class heritage base setup has not been proved");
        }
        baseDefinitions = base->second;
        for (ctjs::SetPropertyOp definition : baseDefinitions) {
            if (!step()) { return false; }
            // Own definitions precede each ancestor's definitions. Only
            // the nearest binding enters the leaf table; every shadowed
            // body stays in baseDefinitions for the receiver/source census.
            if (methodKeys.insert(ctjs::constantKey(definition.getKey())).second) {
                selectedBaseDefinitions.push_back(definition);
            }
        }
        // One shared method/getter body has one selected target. Reuse the
        // complete base getter environment, including transitive dependencies.
        // Overrides need separate bodies for the actual most-derived receiver.
        if (!hasOwnStaticGetters) {
            for (const auto & item : inheritedGetters[sourceValue(inherited.getArgs()[1])]) {
                if (!step()) { return false; }
                staticDefinitions.try_emplace(item.first(), item.second);
            }
        }
    }
    llvm::DenseSet<mlir::Operation *> methodHomes;
    if (!ownFieldSnapshots(closure, instances, definitions, methodKeys, contract)) { return false; }
    llvm::SmallVector<ctjs::SetPropertyOp> allMethods(definitions);
    for (const auto & [key, definition] : staticMethods) {
        if (!step()) { return false; }
        if (staticDefinitions.contains(key)) {
            return refuse("static method conflicts with an accessor");
        }
        allMethods.push_back(definition);
    }
    for (ctjs::SetPropertyOp definition : allMethods) {
        const bool isStatic = definition.getObject() == closure.getResult();
        auto method = definition.getValue().getDefiningOp<ctjs::CreateClosureOp>();
        auto fn = target(method);
        if (!step() || !fn || method->getBlock() != call->getBlock() ||
            !method->isBeforeInBlock(definition) || !undefined(method.getEnclosingThis())) {
            return refuse("class method needs a local ordinary closure");
        }
        if (!methodCaptures(method, closure, staticReads, domEntry && !isStatic)) { return false; }
        if (isStatic && mapClosures.contains(method)) {
            return refuse("static class Map captures require separate call transport");
        }
        ctjs::SetPropertyOp methodHome;
        for (mlir::OpOperand & use : method.getResult().getUses()) {
            if (!step()) { return false; }
            auto * op = use.getOwner();
            if (op == definition && use.getOperandNumber() == 2) { continue; }
            if (llvm::isa<ctjs::RootOp>(op)) { continue; }
            auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
            if (methodHome || !write || use.getOperandNumber() != 0 ||
                ctjs::constantKey(write.getKey()) != "__home" ||
                write.getValue() != (isStatic ? closure.getResult() : prototype.getResult()) ||
                write->getBlock() != call->getBlock() || !method->isBeforeInBlock(write) ||
                !write->isBeforeInBlock(call)) {
                return refuse("class method identity or lexical home escapes initialization");
            }
            methodHome = write;
        }
        auto & block = fn.getBody().front();
        if (domEntry && !isStatic && !proveCells(fn)) { return false; }
        if (!methodHome || !block.getArgument(ctjs::arg_new_target).use_empty() ||
            (!isStatic &&
             (!normalizeSuperMethods(fn, baseDefinitions) ||
              !fieldsOnly(block.getArgument(ctjs::arg_receiver), methodKeys, staticReads, true)))) {
            return refuse("class method observes its identity, home or an unproved receiver");
        }
        if (isStatic) {
            // ponytail: own getter reads only; inherited receivers, static
            // dispatch and new this need separate constructor-identity proofs.
            for (mlir::OpOperand & use : block.getArgument(ctjs::arg_receiver).getUses()) {
                if (!step()) { return false; }
                if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
                if (use.getOperandNumber() != 0 || !read ||
                    !staticDefinitions.contains(ctjs::constantKey(read.getKey()))) {
                    return refuse("static method receiver requires an exact own getter read");
                }
                staticReads.push_back(read);
            }
            // Static bodies retain the strict complete helper census, even
            // for DOM entries. Instance method probes grant them no authority.
            helpers.insert(fn);
            staticMethodClosures.push_back(method);
            setup.insert(definition);
        } else {
            methods.insert(fn);
        }
        methodHomes.insert(methodHome);
        setup.insert(methodHome);
    }
    for (mlir::OpOperand & use : prototype.getResult().getUses()) {
        if (!step()) { return false; }
        if (use.getOperandNumber() == 2 && use.getOwner() != attachment && use.getOwner() != home &&
            !methodHomes.contains(use.getOwner())) {
            return refuse("class prototype reaches an unrelated lexical home");
        }
    }
    for (ctjs::ConstructOp made : instances) {
        if (!fieldsOnly(made.getResult(), methodKeys, staticReads, true)) { return false; }
        for (ctjs::SetPropertyOp definition : definitions) {
            if (!step()) { return false; }
            methodProbes.emplace_back(made, definition);
        }
    }
    if (auto inherited = heritage.lookup(closure.getResult())) {
        if (hasOwnStaticGetters) {
            return refuse("derived class requires receiver-preserving super normalization");
        }
        // Recheck inherited receiver uses against the final method table:
        // a base field write must not shadow a method added by the leaf.
        // ponytail: inherited DOM still needs per-leaf body proofs.
        if (domEntry && !methodKeys.empty()) {
            return refuse("inherited DOM methods require per-leaf body proof");
        }
        for (ctjs::SetPropertyOp definition : baseDefinitions) {
            auto fn = target(definition.getValue().getDefiningOp<ctjs::CreateClosureOp>());
            llvm::SmallVector<ctjs::GetPropertyOp> receiverReads;
            if (!step() || !fieldsOnly(fn.getBody().front().getArgument(ctjs::arg_receiver),
                                       methodKeys, receiverReads, true)) {
                return false;
            }
            staticReads.append(receiverReads);
        }
        auto baseClosure =
            sourceValue(inherited.getArgs()[1]).getDefiningOp<ctjs::CreateClosureOp>();
        auto base = target(baseClosure);
        if (!constructors.contains(base) || (!snapshotFields.contains(closure.getResult()) &&
                                             !normalizeSuper(closure, baseClosure, contract))) {
            return false;
        }
        setup.insert(inherited);
        retainedSetup.insert(inherited.getCallee().getDefiningOp());
    }
    if (!definitions.empty() || constructorReads.size() != firstConstructorRead) {
        for (ctjs::ReturnOp returned : function.getBody().front().getOps<ctjs::ReturnOp>()) {
            if (!step() || !returned.getValue().getDefiningOp<ctjs::ConstantOp>()) {
                return refuse(
                    "class receiver getter or method needs a primitive constructor return");
            }
        }
    }
    auto & entry = function.getBody().front();
    if (!entry.getArgument(ctjs::arg_new_target).use_empty() ||
        (closure.getUpvalues().empty() && !entry.getArgument(ctjs::arg_callee).use_empty()) ||
        !fieldsOnly(entry.getArgument(ctjs::arg_receiver), methodKeys, staticReads, true)) {
        return refuse("class constructor observes new.target, lexical home or receiver identity");
    }
    llvm::SmallVector<ctjs::GetPropertyOp> getterOnlyReads;
    for (ctjs::GetPropertyOp read : staticReads) {
        if (!step()) { return false; }
        auto definition = staticMethods.lookup(ctjs::constantKey(read.getKey()));
        if (!definition) {
            getterOnlyReads.push_back(read);
            continue;
        }
        if (sourceValue(read.getObject()) != closure.getResult()) {
            return refuse("static method read requires its exact local constructor");
        }
        auto fn = target(definition.getValue().getDefiningOp<ctjs::CreateClosureOp>());
        for (mlir::OpOperand & use : read.getResult().getUses()) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            auto invocation = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
            if (!invocation || use.getOperandNumber() != 0 ||
                sourceValue(invocation.getReceiver()) != closure.getResult() ||
                invocation->getBlock() != call->getBlock() || !call->isBeforeInBlock(invocation) ||
                invocation.getArgs().size() + ctjs::implicit_arguments >
                    fn.getBody().front().getNumArguments()) {
                return refuse("static method escapes its exact local call");
            }
            for (auto i = invocation.getArgs().size() + ctjs::implicit_arguments;
                 i < fn.getBody().front().getNumArguments(); ++i) {
                if (!step()) { return false; }
            }
            helperCalls.insert(invocation);
        }
        staticMethodReads.emplace_back(read, fn);
    }
    for (ctjs::CallOp invocation : staticInvocations) {
        if (!step() || !helperCalls.contains(invocation)) {
            return refuse("constructor receiver call lacks an exact own static method");
        }
    }
    if (!staticGetters(staticDefinitions, getterOnlyReads, call)) { return false; }
    for (ctjs::SetPropertyOp write : getterHomes) {
        if (!step()) { return false; }
        if (!setup.contains(write)) {
            return refuse("class constructor reaches an unrelated getter home");
        }
    }
    constructors.insert(function);
    // Keep method definitions on the prototype until constructor lowering.
    // They must already be available when the constructor body runs.
    if (instances.empty()) {
        setup.insert(attachment);
        for (ctjs::SetPropertyOp definition : definitions) { setup.insert(definition); }
    } else if (methodKeys.empty()) {
        setup.insert(attachment);
    } else {
        retainedSetup.insert(attachment);
        for (ctjs::SetPropertyOp definition : selectedBaseDefinitions) {
            if (!step()) { return false; }
            inheritedSlots.emplace_back(heritage.lookup(closure.getResult()), definition);
        }
    }
    setup.insert(home);
    setup.insert(backedge);
    auto & allDefinitions = inheritedMethods[closure.getResult()];
    allDefinitions = definitions;
    allDefinitions.append(baseDefinitions);
    inheritedGetters[closure.getResult()] = std::move(staticDefinitions);
    return true;
}

} // namespace ctcompile::ctnative::class_detail
