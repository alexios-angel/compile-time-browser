#include "../../PartialEvaluation/Heap.h"
#include "Proof.hpp"

#include <cmath>

namespace ctcompile::ctnative::class_detail {

bool classInitialization::clearOwnFieldLoop(ctjs::CallOp snapshot,
                                            llvm::ArrayRef<llvm::StringRef> fields,
                                            const HostContract & contract) {
    const auto helper = [](ctjs::CallOp call) {
        auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
        return load ? load.getName() : llvm::StringRef{};
    };
    ctjs::CallOp open;
    for (mlir::OpOperand & use : snapshot.getResult().getUses()) {
        if (!step()) { return false; }
        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        if (!call || helper(call) != "__ctbrowser_for_of_open") { continue; }
        if (open || use.getOperandNumber() != 2 || call.getArgs().size() != 1 ||
            !undefined(call.getReceiver()) || call->getBlock() != snapshot->getBlock() ||
            !snapshot->isBeforeInBlock(call)) {
            return refuse("class own-key loop requires one ordered snapshot iterator");
        }
        open = call;
    }
    if (!open) { return true; }
    for (llvm::StringRef name :
         {"Array", "__ctbrowser_for_of_open", "__ctbrowser_iter_next", "__ctbrowser_iter_close"}) {
        if (!llvm::is_contained(contract.initialIntrinsics, name)) {
            return refuse("class own-key loop requires original Array iterator identities");
        }
    }
    auto * first = open.getCallee().getDefiningOp();
    if (first->getBlock() != snapshot->getBlock() || !snapshot->isBeforeInBlock(first)) {
        return refuse("class own-key loop requires an ordered iterator load");
    }
    llvm::SmallVector<mlir::Operation *> segment;
    ctjs::CallOp close;
    for (auto cursor = first->getIterator(); cursor != first->getBlock()->end(); ++cursor) {
        if (!step()) { return false; }
        segment.push_back(&*cursor);
        auto call = llvm::dyn_cast<ctjs::CallOp>(*cursor);
        if (call && helper(call) == "__ctbrowser_iter_close") {
            close = call;
            break;
        }
    }
    if (!close || close.getArgs().size() != 2 || close.getArgs()[0] != open.getResult() ||
        !undefined(close.getReceiver())) {
        return refuse("class own-key loop requires a local normal iterator close");
    }

    // ponytail: evaluate only the finite snapshot's scalar iterator machinery
    // and null stores. Other loop bodies need a separate effect/ownership proof.
    // Census both arms first: even an empty snapshot cannot hide unknown effects.
    llvm::DenseSet<mlir::Operation *> operations;
    for (auto * root : segment) {
        const auto walked = root->walk([&](mlir::Operation * op) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            operations.insert(op);
            bool accepted = llvm::isa<ctjs::ConstantOp, mlir::arith::ConstantOp, mlir::ub::PoisonOp,
                                      ctjs::IterableOp, ctjs::GetPropertyOp, ctjs::BinaryOp,
                                      ctjs::BinaryStaticOp, ctjs::CompareOp, ctjs::TruthyOp,
                                      mlir::arith::IndexCastUIOp, mlir::arith::TruncIOp,
                                      mlir::scf::IfOp, mlir::scf::IndexSwitchOp, mlir::scf::WhileOp,
                                      mlir::scf::YieldOp, mlir::scf::ConditionOp>(op);
            if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
                accepted = host_detail::iteratorIntrinsicArity(load.getName()) != 0;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
                const unsigned arity = host_detail::iteratorIntrinsicArity(helper(call));
                accepted = arity && call.getArgs().size() == arity && undefined(call.getReceiver());
            }
            if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
                auto value = write.getValue().getDefiningOp<ctjs::ConstantOp>();
                accepted = write.getObject() == snapshot.getArgs().front() && value &&
                           llvm::isa<ctjs::NullAttr>(value.getValue());
            }
            if (!accepted) {
                refuse("class own-key loop contains an unproved effect");
                return mlir::WalkResult::interrupt();
            }
            return mlir::WalkResult::advance();
        });
        if (walked.wasInterrupted()) { return false; }
    }
    for (auto * op : operations) {
        for (auto & operand : op->getOpOperands()) {
            if (!step()) { return false; }
            auto value = operand.get();
            auto * producer = value.getDefiningOp();
            auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
            if (operations.contains(producer) ||
                (argument && operations.contains(argument.getOwner()->getParentOp())) ||
                llvm::isa_and_nonnull<ctjs::ConstantOp, mlir::arith::ConstantOp,
                                      mlir::ub::PoisonOp>(producer) ||
                value == snapshot.getResult() ||
                (llvm::isa<ctjs::SetPropertyOp>(op) && operand.getOperandNumber() == 0 &&
                 value == snapshot.getArgs().front())) {
                continue;
            }
            return refuse("class own-key loop depends on an unproved source value");
        }
    }

    using partial_eval::value;
    using Values = llvm::SmallVector<value>;
    llvm::DenseMap<mlir::Value, value> values;
    values[snapshot.getResult()] = value::reference(0);
    const auto get = [&](mlir::Value input) {
        if (auto found = values.find(input); found != values.end()) { return found->second; }
        if (auto constant = input.getDefiningOp<ctjs::ConstantOp>()) {
            return value::primitive(constant.getValue());
        }
        if (auto constant = input.getDefiningOp<mlir::arith::ConstantOp>()) {
            return value::primitive(constant.getValue());
        }
        return value{};
    };
    auto * context = module.getContext();
    const auto empty = value::primitive(ctjs::UndefinedAttr::get(context));
    const auto integer = [&](value input) -> std::optional<int64_t> {
        auto attr = llvm::dyn_cast_if_present<mlir::IntegerAttr>(input.constant);
        return attr ? std::optional<int64_t>(attr.getInt()) : std::nullopt;
    };
    llvm::SmallVector<llvm::StringRef> cleared;
    const auto evaluate = [&](auto && visit, mlir::Block::iterator begin, mlir::Block::iterator end,
                              unsigned depth) -> std::optional<Values> {
        if (depth >= 64) {
            refuse("class own-key loop nesting limit exceeded");
            return {};
        }
        for (auto cursor = begin; cursor != end; ++cursor) {
            if (!step()) { return {}; }
            auto & op = *cursor;
            const auto inputs = [&](mlir::ValueRange operands) -> std::optional<Values> {
                Values result;
                for (auto input : operands) {
                    if (!step()) { return {}; }
                    result.push_back(get(input));
                }
                return result;
            };
            if (llvm::isa<mlir::scf::YieldOp, mlir::scf::ConditionOp>(op)) {
                return inputs(op.getOperands());
            }
            if (llvm::isa<ctjs::ConstantOp, mlir::arith::ConstantOp, mlir::ub::PoisonOp,
                          ctjs::RootOp, ctjs::LoadGlobalOp>(op)) {
                continue;
            }
            if (llvm::isa<mlir::scf::IfOp, mlir::scf::IndexSwitchOp>(op)) {
                const auto key = integer(get(op.getOperand(0)));
                if (!key) { return {}; }
                mlir::Region * selected = nullptr;
                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
                    selected = *key ? &branch.getThenRegion() : &branch.getElseRegion();
                } else {
                    auto switcher = llvm::cast<mlir::scf::IndexSwitchOp>(op);
                    selected = &switcher.getDefaultRegion();
                    for (auto [i, choice] : llvm::enumerate(switcher.getCases())) {
                        if (!step()) { return {}; }
                        if (choice == *key) { selected = &switcher.getCaseRegions()[i]; }
                    }
                }
                if (!selected->hasOneBlock() || selected->front().getNumArguments()) { return {}; }
                auto result =
                    visit(visit, selected->front().begin(), selected->front().end(), depth + 1);
                if (!result || result->size() != op.getNumResults()) { return {}; }
                for (auto [output, input] : llvm::zip(op.getResults(), *result)) {
                    if (!step()) { return {}; }
                    values[output] = input;
                }
                continue;
            }
            if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(op)) {
                if (!loop.getBefore().hasOneBlock() || !loop.getAfter().hasOneBlock()) {
                    return {};
                }
                auto carried = inputs(loop.getInits());
                while (carried) {
                    if (!step() || carried->size() != loop.getBeforeArguments().size()) {
                        return {};
                    }
                    for (auto [arg, input] : llvm::zip(loop.getBeforeArguments(), *carried)) {
                        if (!step()) { return {}; }
                        values[arg] = input;
                    }
                    auto result = visit(visit, loop.getBefore().front().begin(),
                                        loop.getBefore().front().end(), depth + 1);
                    if (!result || result->size() != loop.getNumResults() + 1) { return {}; }
                    auto condition = integer(result->front());
                    if (!condition) { return {}; }
                    result->erase(result->begin());
                    if (!*condition) {
                        for (auto [arg, input] : llvm::zip(loop.getResults(), *result)) {
                            if (!step()) { return {}; }
                            values[arg] = input;
                        }
                        break;
                    }
                    if (result->size() != loop.getAfterArguments().size()) { return {}; }
                    for (auto [arg, input] : llvm::zip(loop.getAfterArguments(), *result)) {
                        if (!step()) { return {}; }
                        values[arg] = input;
                    }
                    carried = visit(visit, loop.getAfter().front().begin(),
                                    loop.getAfter().front().end(), depth + 1);
                }
                if (!carried) { return {}; }
                continue;
            }
            value result;
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
                if (call == open) {
                    result = empty;
                } else if (call == close && get(call.getArgs()[0]).constant == empty.constant &&
                           partial_eval::truthy(get(call.getArgs()[1])) == false) {
                    result = empty;
                } else {
                    return {};
                }
            } else if (auto iterable = llvm::dyn_cast<ctjs::IterableOp>(op)) {
                result = get(iterable->getOperand(0));
                if (result.tag != value::kind::reference) { return {}; }
            } else if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
                if (get(read.getObject()).tag != value::kind::reference) { return {}; }
                auto key = get(read.getKey()).constant;
                auto text = llvm::dyn_cast_if_present<ctjs::StringAttr>(key);
                auto number = llvm::dyn_cast_if_present<ctjs::NumberAttr>(key);
                if (text && text.getValue() == "length") {
                    result = partial_eval::numeric(context, static_cast<double>(fields.size()));
                } else if (number) {
                    const double index = number.getDouble();
                    if (!std::isfinite(index) || index < 0 || std::floor(index) != index ||
                        index >= static_cast<double>(fields.size())) {
                        return {};
                    }
                    result = value::primitive(
                        ctjs::StringAttr::get(context, fields[static_cast<size_t>(index)]));
                } else {
                    return {};
                }
            } else if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
                auto key =
                    llvm::dyn_cast_if_present<ctjs::StringAttr>(get(write.getKey()).constant);
                bool own = false;
                for (auto field : fields) {
                    if (!step()) { return {}; }
                    own |= key && field == key.getValue();
                }
                if (!own) { return {}; }
                cleared.push_back(key.getValue());
                continue;
            } else if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(op)) {
                result = partial_eval::binary(binary.getKind(), get(binary.getLhs()),
                                              get(binary.getRhs()), context);
            } else if (auto binary = llvm::dyn_cast<ctjs::BinaryStaticOp>(op)) {
                result = partial_eval::binary(binary.getKind(), get(binary.getLhs()),
                                              get(binary.getRhs()), context, true);
            } else if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(op)) {
                result = partial_eval::compare(compare.getKind(), get(compare.getLhs()),
                                               get(compare.getRhs()), context);
            } else if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(op)) {
                auto bit = partial_eval::truthy(get(truth.getValue()));
                if (!bit) { return {}; }
                result = value::primitive(
                    mlir::IntegerAttr::get(mlir::IntegerType::get(context, 1), *bit));
            } else if (llvm::isa<mlir::arith::TruncIOp, mlir::arith::IndexCastUIOp>(op)) {
                auto input =
                    llvm::dyn_cast_if_present<mlir::IntegerAttr>(get(op.getOperand(0)).constant);
                if (!input) { return {}; }
                auto type = op.getResult(0).getType();
                const unsigned width =
                    type.isIndex() ? 64 : llvm::cast<mlir::IntegerType>(type).getWidth();
                result = value::primitive(
                    mlir::IntegerAttr::get(type, input.getValue().zextOrTrunc(width)));
            } else {
                return {};
            }
            if (result.tag == value::kind::unknown || op.getNumResults() != 1) { return {}; }
            values[op.getResult(0)] = result;
        }
        return Values{};
    };
    if (!evaluate(evaluate, first->getIterator(), std::next(close->getIterator()), 0)) {
        return refuse("class own-key loop requires bounded literal null clearing");
    }
    // Only constants may escape this eliminated protocol segment. No receiver,
    // iterator record or snapshot value is replaced with a runtime container.
    llvm::SmallVector<std::pair<mlir::Value, mlir::Attribute>> replacements;
    for (auto * op : segment) {
        for (auto output : op->getResults()) {
            for (auto * user : output.getUsers()) {
                if (!step()) { return false; }
                if (operations.contains(user)) { continue; }
                auto input = get(output);
                if (input.tag != value::kind::constant) {
                    return refuse("class own-key loop result escapes its scalar proof");
                }
                replacements.emplace_back(output, input.constant);
                break;
            }
        }
    }
    mlir::OpBuilder at(first);
    for (auto [output, literal] : replacements) {
        mlir::Value constant;
        if (auto attr = llvm::dyn_cast<mlir::TypedAttr>(literal)) {
            constant = mlir::arith::ConstantOp::create(at, first->getLoc(), attr);
        } else {
            constant = ctjs::ConstantOp::create(at, first->getLoc(), literal);
        }
        for (auto & cell : cells) {
            if (!step()) { return false; }
            if (cell.second == output) { cell.second = constant; }
        }
        output.replaceAllUsesWith(constant);
    }
    for (auto key : cleared) {
        if (!step()) { return false; }
        auto name =
            ctjs::ConstantOp::create(at, open.getLoc(), ctjs::StringAttr::get(context, key));
        auto null = ctjs::ConstantOp::create(at, open.getLoc(), ctjs::NullAttr::get(context));
        ctjs::SetPropertyOp::create(at, open.getLoc(), snapshot.getArgs().front(), name, null);
    }
    for (auto * op : llvm::reverse(segment)) { op->erase(); }
    return true;
}

} // namespace ctcompile::ctnative::class_detail
