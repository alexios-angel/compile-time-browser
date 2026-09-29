#include "../../PartialEvaluation/Heap.h"
#include "Proof.hpp"

namespace ctcompile::ctnative::class_detail {
namespace {
// Every own and prototype-chain property name a ctbrowser VM element has,
// enumerated from the VM; element-names.test fails on any difference.
constexpr std::string_view elementNames[] = {
#include "ElementNames.inc"
};
} // namespace

void classInitialization::forgetRecords(mlir::Region & region) {
    region.walk([&](mlir::Operation * op) {
        helperCalls.erase(op);
        mapOperations.erase(op);
        if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op)) {
            mapCaptures.erase(read.getResult());
            holderCaptures.erase(read.getResult());
            callableCaptures.erase(read);
            llvm::erase(captureReads, read);
        }
    });
}

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
        auto bit = ctjs::constantAttr<ctjs::BooleanAttr>(cell.getInitial());
        if (!bit || bit.getValue()) { return refuse("super guard must begin false"); }
        guard = cell;
    }
    // An implicit derived constructor, `super(...args)`, spreads a copy of its
    // rest array made by the importer's exact zero-based index loop. With no
    // arguments object on either side it is `super(p0, .., pn)` over formals
    // the copy gains up to the base's count: missing actuals stay undefined
    // and extra ones were already evaluated and are ignored.
    ctjs::CallSpreadOp spread;
    llvm::DenseSet<mlir::Operation *> forwarding;
    const auto usesOnly = [&](mlir::Operation * op, llvm::ArrayRef<mlir::Operation *> allowed) {
        for (mlir::Operation * user : op->getUsers()) {
            if (!step() || (!llvm::isa<ctjs::RootOp>(user) && !llvm::is_contained(allowed, user))) {
                return false;
            }
        }
        return true;
    };
    const auto numberIs = [](mlir::Value value, double expected) {
        auto number = ctjs::constantAttr<ctjs::NumberAttr>(value);
        return number && number.getDouble() == expected;
    };
    const auto forwardsActuals = [&] {
        for (mlir::Operation & op : original) {
            if (!step() || llvm::isa<ctjs::MakeArgumentsOp>(op)) { return false; }
            if (auto call = llvm::dyn_cast<ctjs::CallSpreadOp>(op)) {
                if (spread) { return false; }
                spread = call;
            }
        }
        const auto observed = base.walk([&](mlir::Operation * op) {
            return !step() || llvm::isa<ctjs::MakeArgumentsOp, ctjs::GatherRestOp>(op)
                       ? mlir::WalkResult::interrupt()
                       : mlir::WalkResult::advance();
        });
        auto array =
            spread ? spread.getArgs().getDefiningOp<ctjs::CreateArrayOp>() : ctjs::CreateArrayOp{};
        if (observed.wasInterrupted() || !array || !array.getElements().empty() ||
            array->getBlock() != &original) {
            return false;
        }
        ctjs::AppendOp append;
        for (mlir::Operation * user : array->getUsers()) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(user) || user == spread) { continue; }
            auto put = llvm::dyn_cast<ctjs::AppendOp>(user);
            if (!put || append || put.getArray() != array.getResult()) { return false; }
            append = put;
        }
        auto loop = append ? llvm::dyn_cast<mlir::scf::WhileOp>(append->getParentOp())
                           : mlir::scf::WhileOp{};
        if (!loop || loop->getBlock() != &original || !loop.getBefore().hasOneBlock() ||
            !loop.getAfter().hasOneBlock() || loop.getInits().size() != 1 ||
            !numberIs(loop.getInits()[0], 0) || !usesOnly(loop, {})) {
            return false;
        }
        auto & before = loop.getBefore().front();
        auto & after = loop.getAfter().front();
        if (before.getNumArguments() != 1 || after.getNumArguments() != 1 ||
            before.getOperations().size() != 3 || after.getOperations().size() != 4) {
            return false;
        }
        auto compare = llvm::dyn_cast<ctjs::CompareOp>(before.front());
        auto truth = llvm::dyn_cast<ctjs::TruthyOp>(before.front().getNextNode());
        auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(before.back());
        auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(after.front());
        auto advance = llvm::dyn_cast<ctjs::BinaryStaticOp>(append->getNextNode());
        auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(after.back());
        auto length =
            compare ? compare.getRhs().getDefiningOp<ctjs::GetPropertyOp>() : ctjs::GetPropertyOp{};
        auto items =
            length ? length.getObject().getDefiningOp<ctjs::IterableOp>() : ctjs::IterableOp{};
        auto rest =
            items ? items.getSource().getDefiningOp<ctjs::GatherRestOp>() : ctjs::GatherRestOp{};
        if (!compare || compare.getKind() != ctjs::CompareKind::Lt ||
            compare.getLhs() != before.getArgument(0) || !truth ||
            truth.getValue() != compare.getResult() || !condition ||
            condition.getCondition() != truth.getResult() || condition.getArgs().size() != 1 ||
            condition.getArgs()[0] != before.getArgument(0) || !read ||
            read->getNextNode() != append || read.getKey() != after.getArgument(0) ||
            append.getElement() != read.getResult() || !advance ||
            advance.getKind() != ctjs::BinaryKind::Add ||
            advance.getLhs() != after.getArgument(0) || !numberIs(advance.getRhs(), 1) || !yield ||
            yield.getNumOperands() != 1 || yield.getOperand(0) != advance.getResult() ||
            ctjs::constantKey(length.getKey()) != "length" || !rest || rest.getFrom() != 0 ||
            read.getObject() != items.getResult() || rest->getBlock() != &original ||
            items->getBlock() != &original || length->getBlock() != &original ||
            !rest->isBeforeInBlock(items) || !items->isBeforeInBlock(length) ||
            !length->isBeforeInBlock(loop) || !array->isBeforeInBlock(loop) ||
            !loop->isBeforeInBlock(spread) || !usesOnly(rest, {items}) ||
            !usesOnly(items, {length, read}) || !usesOnly(length, {compare})) {
            return false;
        }
        for (mlir::Operation * op :
             {rest.getOperation(), items.getOperation(), length.getOperation(),
              array.getOperation(), loop.getOperation()}) {
            forwarding.insert(op);
        }
        return true;
    };
    if (!guard && !forwardsActuals()) {
        return refuse("derived class requires receiver-preserving super normalization");
    }
    if (guard) {
        for (auto & use : guard.getResult().getUses()) {
            if (!step()) { return false; }
            if (use.getOperandNumber() != 0 ||
                !llvm::isa<ctjs::CellGetOp, ctjs::CellSetOp, ctjs::RootOp>(use.getOwner())) {
                return refuse("super guard escapes its private reads and write");
            }
        }
    }
    if (!spendWalk(function, cloneCost)) { return false; }
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
        if (!spendWalk(&op, cloneCost)) { return false; }
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
    mlir::Value baseResult;
    llvm::DenseSet<mlir::Block *> visited;
    const auto visit = [&](auto && self, mlir::Block & block,
                           llvm::SmallVectorImpl<mlir::Value> & yielded, unsigned depth) -> bool {
        if (!step() || depth == 64 || !visited.insert(&block).second) {
            return refuse("super initialization has cyclic control flow");
        }
        for (mlir::Operation & op : block) {
            if (!step()) { return false; }
            // The rest copy becomes the spliced formals; its roots go with it.
            if (forwarding.contains(&op) ||
                (llvm::isa<ctjs::RootOp>(op) &&
                 forwarding.contains(op.getOperand(0).getDefiningOp()))) {
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
                const auto bit = integer(branch.getCondition());
                auto truth = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
                auto read =
                    truth ? truth.getValue().getDefiningOp<ctjs::CellGetOp>() : ctjs::CellGetOp{};
                const bool guardBranch = read && guard && read.getCell() == guard.getResult();
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
                if (!guard || read.getCell() != guard.getResult()) {
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
                auto bit = ctjs::constantAttr<ctjs::BooleanAttr>(write.getValue());
                if (phase != 1 || !guard || write.getCell() != guard.getResult() || !bit ||
                    !bit.getValue()) {
                    return refuse("super guard requires one true write after the base call");
                }
                initialized = true;
                phase = 2;
                continue;
            }
            if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(op)) {
                auto bit = ctjs::constantAttr<ctjs::BooleanAttr>(
                    mapping.lookupOrDefault(truth.getValue()));
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
                auto * next = pass->getNextNode();
                if (phase != 0 || !next || (!llvm::isa<ctjs::CallOp>(next) && next != spread)) {
                    return refuse("super new.target must immediately precede its base call");
                }
                continue;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op); call || &op == spread) {
                auto read = (call ? call.getCallee() : spread.getCallee())
                                .getDefiningOp<ctjs::GetPropertyOp>();
                auto proto =
                    read ? read.getObject().getDefiningOp<ctjs::GetProtoOp>() : ctjs::GetProtoOp{};
                if (proto && proto->getOperand(0).getDefiningOp<ctjs::LoadHomeOp>() &&
                    ctjs::constantKey(read.getKey()) == "constructor") {
                    if (phase != 0 ||
                        (call ? call.getReceiver() : spread.getReceiver()) != receiver ||
                        !llvm::isa_and_nonnull<ctjs::PassNewTargetOp>(op.getPrevNode()) ||
                        (call && call.getArgs().size() + ctjs::implicit_arguments >
                                     base.getBody().front().getNumArguments())) {
                        return refuse("super requires one exact ordered base call");
                    }
                    for (auto & use : op.getResult(0).getUses()) {
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
                        const auto index = static_cast<unsigned>(ctjs::implicit_arguments + i);
                        while (!call && output->getNumArguments() <= index) {
                            output->addArgument(formal.getType(), formal.getLoc());
                        }
                        arguments.map(formal, !call ? output->getArgument(index)
                                              : i < call.getArgs().size()
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
                    mapping.map(op.getResult(0), absent);
                    baseResult = op.getResult(0);
                    phase = call ? 1 : 2; // No guard records the forwarded call.
                    continue;
                }
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
                auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
                if (load && undefined(call.getReceiver()) && call.getResult().use_empty()) {
                    if (load.getName() == "__ctbrowser_bind_this" && phase == 2 &&
                        call.getArgs().size() == 1 && call.getArgs()[0] == baseResult) {
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
            if (guard && llvm::isa<ctjs::RootOp>(op) && op.getOperand(0) == guard.getResult()) {
                continue;
            }
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
    if (!spend(captureReads.size()) ||
        !spendWalk(function, [](mlir::Operation *) { return uint64_t(1); }) || !reason.empty()) {
        return false;
    }
    forgetRecords(function.getBody());
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
        setCaptures(constructor, function, captures);
        mapClosures.insert(constructor);
        if (!llvm::is_contained(capturedClosures, constructor)) {
            capturedClosures.push_back(constructor);
        }
    }
    function.getBody().takeBody(copy->getBody());
    // A forwarded super call adds the base's formals.
    function.setFunctionTypeAttr(mlir::TypeAttr::get(
        mlir::FunctionType::get(module.getContext(), function.getBody().front().getArgumentTypes(),
                                function.getFunctionType().getResults())));
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
            if (!spend(cloneCost(&op))) { return false; }
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

std::optional<partial_eval::value> classInitialization::evaluateHelper(
    ctjs::FuncOp fn, llvm::ArrayRef<partial_eval::value> actuals, unsigned depth) {
    // An exact captured helper runs the same way for the same actuals: walk
    // only its executed path through pure primitive operations and nested
    // exact helper calls. Any other operation leaves it unknown. Poison is
    // the lift's dead carried slot: it may travel, never be used.
    // An actual may be a declared DOM element parameter, a reference to its
    // index. It is truthy, typeof "object" and strictly unequal to every
    // primitive; its `nodeType` is 1 and a constant key outside the VM's
    // element name set reads undefined: the census refuses prototype writes
    // and the typed DOM proof refuses unmodeled element writes, so no source
    // adds one. Keys spelled `__` stay unknown: the binding adds internal
    // slots (`__selected`, `__cached_...`) to an element lazily.
    // ponytail: structured single-block bodies; loops, objects, other
    // property reads and host bindings need the full partial evaluator's heap.
    if (depth == 16 || !fn || !fn.getBody().hasOneBlock()) { return std::nullopt; }
    auto * context = module.getContext();
    using partial_eval::value;
    const auto known = [](const value & data) {
        return data.tag == value::kind::constant || data.tag == value::kind::reference;
    };
    llvm::DenseMap<mlir::Value, value> values;
    llvm::DenseMap<mlir::Value, ctjs::FuncOp> callees;
    auto & entry = fn.getBody().front();
    for (auto [i, formal] :
         llvm::enumerate(entry.getArguments().drop_front(ctjs::implicit_arguments))) {
        values[formal] =
            i < actuals.size() ? actuals[i] : value::primitive(ctjs::UndefinedAttr::get(context));
    }
    std::optional<value> returned;
    const auto walk = [&](auto && self, mlir::Block & block,
                          llvm::SmallVectorImpl<value> & yielded) -> bool {
        // A structured arm or switch case: its yields become the op's results.
        const auto enter = [&](mlir::Operation & op, mlir::Region & region) {
            llvm::SmallVector<value> results;
            if (!region.hasOneBlock() || !self(self, region.front(), results) || returned ||
                results.size() != op.getNumResults()) {
                return false;
            }
            for (auto [result, data] : llvm::zip(op.getResults(), results)) {
                values[result] = data;
            }
            return true;
        };
        for (mlir::Operation & op : block) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp, mlir::ub::PoisonOp>(
                    op)) {
                continue;
            }
            if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(op)) {
                auto data = values.lookup(result.getValue());
                if (!known(data)) { return false; }
                returned = data;
                return true;
            }
            if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(op)) {
                for (mlir::Value operand : yield.getOperands()) {
                    yielded.push_back(values.lookup(operand));
                }
                return true;
            }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
                auto take = partial_eval::truthy(values.lookup(branch.getCondition()));
                if (!take || !enter(op, *take ? branch.getThenRegion() : branch.getElseRegion())) {
                    return false;
                }
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::scf::IndexSwitchOp>(op)) {
                auto index = values.lookup(branch.getArg());
                auto number = index.tag == value::kind::constant
                                  ? llvm::dyn_cast<mlir::IntegerAttr>(index.constant)
                                  : mlir::IntegerAttr{};
                if (!number) { return false; }
                auto * region = &branch.getDefaultRegion();
                for (auto [i, key] : llvm::enumerate(branch.getCases())) {
                    if (key == number.getInt()) { region = &branch.getCaseRegions()[i]; }
                }
                if (!enter(op, *region)) { return false; }
                continue;
            }
            if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op)) {
                // The caller reached this body through its proved capture.
                auto helper = callableCaptures.lookup(load);
                if (!helper) { return false; }
                callees[load.getResult()] = helper;
                continue;
            }
            value data;
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
                auto helper = callees.lookup(call.getCallee());
                llvm::SmallVector<value> arguments;
                for (mlir::Value argument : call.getArgs()) {
                    arguments.push_back(values.lookup(argument));
                    if (!known(arguments.back())) { return false; }
                }
                auto answer = helper && undefined(call.getReceiver())
                                  ? evaluateHelper(helper, arguments, depth + 1)
                                  : std::nullopt;
                if (!answer) { return false; }
                data = *answer;
            } else if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
                const auto key = ctjs::constantKey(read.getKey());
                if (values.lookup(read.getObject()).tag == value::kind::reference && !key.empty() &&
                    !key.starts_with("__")) {
                    if (key == "nodeType") {
                        data = partial_eval::numeric(context, 1);
                    } else if (!std::ranges::binary_search(elementNames, std::string_view{key})) {
                        data = value::primitive(ctjs::UndefinedAttr::get(context));
                    }
                }
            } else if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(op)) {
                if (ctjs::isPrimitiveAttr(constant.getValue())) {
                    data = value::primitive(constant.getValue());
                }
            } else if (auto constant = llvm::dyn_cast<mlir::arith::ConstantOp>(op)) {
                if (llvm::isa<mlir::IntegerAttr>(constant.getValue())) {
                    data = value::primitive(constant.getValue());
                }
            } else if (auto cast = llvm::dyn_cast<mlir::arith::IndexCastUIOp>(op)) {
                auto input = values.lookup(cast.getIn());
                auto number = input.tag == value::kind::constant
                                  ? llvm::dyn_cast<mlir::IntegerAttr>(input.constant)
                                  : mlir::IntegerAttr{};
                if (number) {
                    data = value::primitive(mlir::IntegerAttr::get(
                        cast.getType(), static_cast<int64_t>(number.getValue().getZExtValue())));
                }
            } else if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op)) {
                auto operand = values.lookup(unary.getOperand());
                data = operand.tag == value::kind::reference &&
                               unary.getKind() == ctjs::UnaryKind::TypeOf
                           ? value::primitive(ctjs::StringAttr::get(context, "object"))
                           : partial_eval::unary(unary.getKind(), operand, context);
            } else if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(op)) {
                auto lhs = values.lookup(compare.getLhs());
                auto rhs = values.lookup(compare.getRhs());
                // Two element parameters may be one element.
                if (lhs.tag != value::kind::reference || rhs.tag != value::kind::reference ||
                    lhs.node == rhs.node) {
                    data = partial_eval::compare(compare.getKind(), lhs, rhs, context);
                }
            } else if (auto binary = llvm::dyn_cast<ctjs::BinaryStaticOp>(op)) {
                data = partial_eval::binary(binary.getKind(), values.lookup(binary.getLhs()),
                                            values.lookup(binary.getRhs()), context, true);
            } else if (auto test = llvm::dyn_cast<ctjs::TruthyOp>(op)) {
                if (auto truth = partial_eval::truthy(values.lookup(test.getValue()))) {
                    data = partial_eval::boolean(context, *truth);
                }
            } else if (auto boxed = llvm::dyn_cast<ctjs::FromBoolOp>(op)) {
                data = values.lookup(boxed.getOperand());
            }
            if (!known(data) || op.getNumResults() != 1) { return false; }
            values[op.getResult(0)] = data;
        }
        return false;
    };
    llvm::SmallVector<value> unused;
    if (!walk(walk, entry, unused) || !returned) { return std::nullopt; }
    return returned;
}

bool classInitialization::foldDecidedArms(ctjs::CreateClosureOp constructor,
                                          llvm::ArrayRef<ctjs::ConstructOp> instances) {
    // Nothing extends this class, so these exact `new`s are every construction.
    // A top-level arm on a formal - or on an exact captured helper applied to
    // formals and literals - that every actual (a literal or a declared DOM
    // element) makes equally truthy runs the same way each time: keep that
    // arm alone. Arms whose dropped side holds constructions, closures, cells,
    // global writes or setup keep today's proofs.
    if (instances.empty() || baseClasses.contains(constructor.getResult())) { return true; }
    auto function = target(constructor);
    if (!function || function.getBody().empty()) { return true; }
    auto & entry = function.getBody().front();
    auto * context = module.getContext();
    const auto formal = [&](mlir::Value value) {
        auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
        return argument && argument.getOwner() == &entry &&
               argument.getArgNumber() >= ctjs::implicit_arguments;
    };
    // What an exact `new` gives an operand: a literal, a declared DOM element
    // parameter (a reference to its index) or unknown.
    using partial_eval::value;
    const auto actual = [&](ctjs::ConstructOp made, mlir::Value operand) -> value {
        if (auto constant = operand.getDefiningOp<ctjs::ConstantOp>()) {
            return value::primitive(constant.getValue());
        }
        const auto index =
            llvm::cast<mlir::BlockArgument>(operand).getArgNumber() - ctjs::implicit_arguments;
        if (index >= made.getArgs().size()) {
            return value::primitive(ctjs::UndefinedAttr::get(context));
        }
        if (auto constant = made.getArgs()[index].getDefiningOp<ctjs::ConstantOp>()) {
            return value::primitive(constant.getValue());
        }
        auto element = elementActuals.find(made.getArgs()[index]);
        return element == elementActuals.end() ? value{} : value::reference(element->second);
    };
    for (mlir::Operation & op : llvm::make_early_inc_range(entry)) {
        if (!step()) { return false; }
        auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op);
        auto truth =
            branch ? branch.getCondition().getDefiningOp<ctjs::TruthyOp>() : ctjs::TruthyOp{};
        const auto operand = truth ? sourceValue(truth.getValue()) : mlir::Value{};
        auto call = operand ? operand.getDefiningOp<ctjs::CallOp>() : ctjs::CallOp{};
        auto load =
            call ? call.getCallee().getDefiningOp<ctjs::LoadUpvalueOp>() : ctjs::LoadUpvalueOp{};
        auto helper = load ? callableCaptures.lookup(load) : ctjs::FuncOp{};
        llvm::SmallVector<mlir::Value> arguments;
        if (helper && undefined(call.getReceiver())) {
            for (mlir::Value argument : call.getArgs()) {
                arguments.push_back(sourceValue(argument));
                if (!arguments.back().getDefiningOp<ctjs::ConstantOp>() &&
                    !formal(arguments.back())) {
                    helper = {};
                }
            }
        } else {
            helper = {};
        }
        if (!operand || (!formal(operand) && !helper)) { continue; }
        std::optional<bool> decided;
        bool undecided = false;
        // The call arguments whose element every construction's call returns.
        llvm::SmallVector<bool> forwarded(arguments.size(), true);
        for (ctjs::ConstructOp made : instances) {
            if (!step()) { return false; }
            value literal = helper ? value{} : actual(made, operand);
            if (helper) {
                llvm::SmallVector<value> literals;
                for (mlir::Value argument : arguments) {
                    literals.push_back(actual(made, argument));
                }
                if (llvm::all_of(literals,
                                 [](const value & a) { return a.tag != value::kind::unknown; })) {
                    literal = evaluateHelper(helper, literals, 0).value_or(value{});
                }
                for (auto [i, argument] : llvm::enumerate(literals)) {
                    forwarded[i] = forwarded[i] && literal.tag == value::kind::reference &&
                                   partial_eval::sameValue(argument, literal);
                }
            }
            const auto truthy = partial_eval::truthy(literal);
            if (!truthy || (decided && *decided != *truthy)) {
                undecided = true;
                break;
            }
            decided = truthy;
        }
        if (undecided || !decided) { continue; }
        auto & kept = *decided ? branch.getThenRegion() : branch.getElseRegion();
        auto & dropped = *decided ? branch.getElseRegion() : branch.getThenRegion();
        const auto risky = dropped.walk([&](mlir::Operation * nested) {
            if (!step() ||
                llvm::isa<ctjs::ConstructOp, ctjs::CreateClosureOp, ctjs::CreateCellOp,
                          ctjs::CellSetOp, ctjs::StoreGlobalOp, ctjs::DefineAccessorOp>(nested) ||
                setup.contains(nested) || retainedSetup.contains(nested) ||
                errorOperations.contains(nested) || cellOperations.contains(nested) ||
                domDataOperations.contains(nested) || methodCalls.contains(nested) ||
                instanceOfResults.contains(nested) || deferredPublications.contains(nested)) {
                return mlir::WalkResult::interrupt();
            }
            return mlir::WalkResult::advance();
        });
        if (!reason.empty()) { return false; }
        if (risky.wasInterrupted()) { continue; }
        // The dropped arm loses its records, as a replaced super body does.
        // Its captured helper and holder slot calls were source calls: their
        // targets may have lost their last one.
        dropped.walk([&](ctjs::CallOp made) {
            if (auto helper = callableCaptures.lookup(made.getCallee().getDefiningOp())) {
                decidedHelpers.insert(helper);
                return;
            }
            if (auto read = made.getCallee().getDefiningOp<ctjs::GetPropertyOp>()) {
                decideSlot(holderCaptures.lookup(read.getObject()), read);
            }
        });
        forgetRecords(dropped);
        if (kept.empty()) {
            if (branch->getNumResults()) { continue; }
            branch.erase();
        } else {
            auto & block = kept.front();
            auto yield = llvm::cast<mlir::scf::YieldOp>(block.getTerminator());
            branch.getResults().replaceAllUsesWith(yield.getOperands());
            entry.getOperations().splice(branch->getIterator(), block.getOperations(),
                                         block.begin(), yield->getIterator());
            branch.erase();
        }
        // Every construction ran the helper on a pure path, so a call whose
        // result died with the arm does nothing: erase it. One that returns
        // the same argument's element in every construction is that argument.
        if (!helper) { continue; }
        const auto forward = llvm::find(forwarded, true);
        if (forward != forwarded.end()) {
            call.getResult().replaceUsesWithIf(
                call.getArgs()[static_cast<unsigned>(forward - forwarded.begin())],
                [](mlir::OpOperand & use) { return !llvm::isa<ctjs::RootOp>(use.getOwner()); });
        } else if (domEntryHelpers.contains(helper)) {
            continue;
        }
        if (truth->use_empty()) { truth.erase(); }
        if (rootedOnly(call)) {
            helperCalls.erase(call);
            eraseRooted(call);
            decidedHelpers.insert(helper);
        }
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
    bool testedConstructor = false;
    if (!normalizePublicationHelper(closure, contract)) { return false; }
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
        if (llvm::isa<ctjs::InstanceOfOp>(op) && use.getOperandNumber() == 1) {
            testedConstructor = true; // Checked after every class setup has been proved.
            continue;
        }
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
            if (closureMetadataKey(key) || !staticMethods.try_emplace(key, write).second) {
                return refuse("static method shadows closure metadata or repeats a slot");
            }
        } else {
            return refuse("class methods, static fields or repeated setup remain unsupported");
        }
    }
    auto prototype = attachment ? attachment.getValue().getDefiningOp<ctjs::CreateObjectOp>()
                                : ctjs::CreateObjectOp{};
    if (!prototype || !home || !backedge ||
        (instances.empty() && !baseClasses.contains(closure.getResult()) && !testedConstructor) ||
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
    llvm::StringSet<> shadowedGetters;
    llvm::SmallVector<ctjs::SetPropertyOp> baseDefinitions, selectedBaseDefinitions,
        shadowedBaseDefinitions;
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
            } else {
                shadowedBaseDefinitions.push_back(definition);
            }
        }
        // One shared method/getter body has one selected target. Reuse the
        // complete base getter environment, including transitive dependencies.
        // An own getter overrides its inherited key; an inherited getter whose
        // receiver reads reach an override gets a copy for this receiver.
        for (const auto & item : inheritedGetters[sourceValue(inherited.getArgs()[1])]) {
            if (!step()) { return false; }
            if (!staticDefinitions.try_emplace(item.first(), item.second).second) {
                shadowedGetters.insert(item.first());
            }
        }
        // Each inherited DOM body must reach an actual leaf's private proof.
        if (domEntry && !methodKeys.empty() && instances.empty() &&
            !baseClasses.contains(closure.getResult())) {
            return refuse("inherited DOM methods require a constructed leaf");
        }
        if (!shadowedGetters.empty() &&
            !cloneReceiverGetters(closure.getResult(), staticDefinitions, shadowedGetters)) {
            return false;
        }
        if ((!shadowedGetters.empty() || (domEntry && !instances.empty())) &&
            !cloneReceiverMethods(closure, prototype, call, shadowedGetters,
                                  selectedBaseDefinitions, definitions,
                                  domEntry && !instances.empty())) {
            return false;
        }
        if (domEntry && !instances.empty() &&
            !cloneReceiverMethods(closure, prototype, call, shadowedGetters,
                                  shadowedBaseDefinitions, definitions, true, true)) {
            return false;
        }
    }
    llvm::DenseSet<mlir::Operation *> methodHomes;
    if (auto inherited = heritage.lookup(closure.getResult())) {
        auto baseClosure =
            sourceValue(inherited.getArgs()[1]).getDefiningOp<ctjs::CreateClosureOp>();
        if (!constructors.contains(target(baseClosure)) ||
            (!normalizedSuper.contains(function) &&
             !normalizeSuper(closure, baseClosure, contract))) {
            return false;
        }
    }
    // Publication and own-key snapshots both need the complete leaf body,
    // including all effects and field writes from its proved super chain.
    if (!foldDecidedArms(closure, instances) ||
        !sinkConstructorPublication(closure, instances, contract)) {
        return false;
    }
    if (!ownFieldSnapshots(closure, instances, definitions, methodKeys, staticDefinitions,
                           staticMethods, contract)) {
        return false;
    }
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
            // A static body only runs from an exact local call, so its receiver
            // is this class: an own getter read, or an own static method called
            // on it, selects the body an exact `C.key` would.
            // ponytail: inherited receivers and new this need separate
            // constructor-identity proofs.
            const auto receiver = block.getArgument(ctjs::arg_receiver);
            llvm::SmallPtrSet<mlir::Operation *, 4> receiverCalls;
            for (mlir::OpOperand & use : receiver.getUses()) {
                if (!step()) { return false; }
                auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
                if (use.getOperandNumber() != 0 || !read) { continue; }
                const auto key = ctjs::constantKey(read.getKey());
                if (staticDefinitions.contains(key)) {
                    staticReads.push_back(read);
                    continue;
                }
                auto callee = staticMethods.lookup(key);
                auto fn = callee ? target(callee.getValue().getDefiningOp<ctjs::CreateClosureOp>())
                                 : ctjs::FuncOp{};
                if (!fn) {
                    return refuse("static method receiver requires an exact own getter read");
                }
                for (mlir::OpOperand & call : read.getResult().getUses()) {
                    if (!step()) { return false; }
                    if (llvm::isa<ctjs::RootOp>(call.getOwner())) { continue; }
                    auto invocation = llvm::dyn_cast<ctjs::CallOp>(call.getOwner());
                    if (!invocation || call.getOperandNumber() != 0 ||
                        invocation.getReceiver() != receiver ||
                        invocation.getArgs().size() + ctjs::implicit_arguments >
                            fn.getBody().front().getNumArguments()) {
                        return refuse("static method escapes its exact local call");
                    }
                    helperCalls.insert(invocation);
                    receiverCalls.insert(invocation);
                }
                staticMethodReads.emplace_back(read, fn);
            }
            for (mlir::OpOperand & use : receiver.getUses()) {
                if (!step()) { return false; }
                if (llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                    (use.getOperandNumber() == 0 &&
                     llvm::isa<ctjs::GetPropertyOp>(use.getOwner())) ||
                    (use.getOperandNumber() == 1 && receiverCalls.contains(use.getOwner()))) {
                    continue;
                }
                return refuse("static method receiver requires an exact own getter read");
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
        // Receiver copies live in this class's environment alone: a class
        // derived from it would inherit the shared definitions instead.
        auto copies = receiverGetters.find(closure.getResult());
        const bool copied = copies != receiverGetters.end() && !copies->second.empty();
        if (copied && baseClasses.contains(closure.getResult())) {
            return refuse("derived class requires receiver-preserving super normalization");
        }
        // Recheck inherited receiver uses against the final method table:
        // a base field write must not shadow a method added by the leaf.
        for (ctjs::SetPropertyOp definition : baseDefinitions) {
            auto fn = target(definition.getValue().getDefiningOp<ctjs::CreateClosureOp>());
            llvm::SmallVector<ctjs::GetPropertyOp> receiverReads;
            if (!step() || !fieldsOnly(fn.getBody().front().getArgument(ctjs::arg_receiver),
                                       methodKeys, receiverReads, true)) {
                return false;
            }
            // A shared inherited body resolves each read once, for every class.
            // This class's instances reach only its nearest non-overridden
            // bodies - copies replaced the others - and ancestors only through
            // super calls, which were expanded and are rechecked on this receiver.
            bool conflicting = false;
            for (ctjs::GetPropertyOp read : receiverReads) {
                if (!step()) { return false; }
                const auto key = ctjs::constantKey(read.getKey());
                conflicting |=
                    shadowedGetters.contains(key) || (copied && copies->second.contains(key));
            }
            if (!conflicting) {
                staticReads.append(receiverReads);
            } else if (llvm::is_contained(selectedBaseDefinitions, definition)) {
                return refuse("derived class requires receiver-preserving super normalization");
            }
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
    // A base no exact `new` constructs runs only as its leaves' normalized
    // super copies, and each leaf proves its own receiver across that copy.
    // The original still records every use for the census; rewrite erases it.
    const bool leafConstructed = instances.empty() && baseClasses.contains(closure.getResult());
    if (leafConstructed) { leafBuilt.insert(function); }
    if (!entry.getArgument(ctjs::arg_new_target).use_empty() ||
        (closure.getUpvalues().empty() && !entry.getArgument(ctjs::arg_callee).use_empty()) ||
        !fieldsOnly(entry.getArgument(ctjs::arg_receiver), methodKeys, staticReads, true,
                    leafConstructed)) {
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
            if (!spend(fn.getBody().front().getNumArguments() - invocation.getArgs().size() -
                       ctjs::implicit_arguments)) {
                return false;
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
    llvm::erase_if(allDefinitions, [&](ctjs::SetPropertyOp definition) {
        return proofOnlyMethods.contains(definition);
    });
    allDefinitions.append(baseDefinitions);
    inheritedGetters[closure.getResult()] = std::move(staticDefinitions);
    return true;
}

} // namespace ctcompile::ctnative::class_detail
