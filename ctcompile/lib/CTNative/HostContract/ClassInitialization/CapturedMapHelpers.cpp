#include "Proof.hpp"

#include <bit>

namespace ctcompile::ctnative::class_detail {

bool classInitialization::normalizeNestedMaps(ctjs::FuncOp scope) {
    // ponytail: literal outer keys and statically selected Map-only branches.
    // Dynamic topology needs path-sensitive origins and an owner proof.
    auto & entry = scope.getBody().front();
    const auto entryPosition = [&](mlir::Operation * op) {
        while (op->getBlock() != &entry && llvm::isa<mlir::scf::IfOp>(op->getParentOp())) {
            if (!step()) { break; }
            op = op->getParentOp();
        }
        return op;
    };
    const auto localMap = [&](mlir::Value value) {
        auto made = sourceValue(value).getDefiningOp<ctjs::ConstructOp>();
        auto load =
            made ? made.getCallee().getDefiningOp<ctjs::LoadGlobalOp>() : ctjs::LoadGlobalOp{};
        return made && load && load.getName() == "Map" && made.getArgs().empty() &&
                       made.getNewTarget() == made.getCallee() &&
                       entryPosition(made)->getBlock() == &entry
                   ? made
                   : ctjs::ConstructOp{};
    };
    llvm::SmallVector<ctjs::ConstructOp> candidates;
    for (auto made : entry.getOps<ctjs::ConstructOp>()) {
        if (!step()) { return false; }
        if (maps.contains(made.getResult())) { candidates.push_back(made); }
    }
    for (auto map : candidates) {
        bool nested = false;
        for (mlir::OpOperand * use : sourceUses(map.getResult())) {
            if (!step()) { return false; }
            auto call = llvm::dyn_cast<ctjs::CallOp>(use->getOwner());
            if (!call || use->getOperandNumber() != 1 || call.getArgs().size() != 2) { continue; }
            auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            nested |=
                read && ctjs::constantKey(read.getKey()) == "set" && localMap(call.getArgs()[1]);
        }
        if (!nested) { continue; }
        llvm::SmallVector<mlir::Value> bindings;
        for (auto [cell, initial] : cells) {
            if (!step()) { return false; }
            if (sourceValue(initial) != map.getResult()) { continue; }
            bindings.push_back(cell);
            for (mlir::Operation * user : cell.getUsers()) {
                if (!step()) { return false; }
                if (!cellOperations.contains(user)) {
                    return refuse("class nested Map cannot capture its outer owner");
                }
            }
        }
        // Captures box caller locals too. Discharge these already-proved
        // reads before a selected branch can retire their cached operations.
        for (ctjs::CellGetOp read : cellReads) {
            if (!step()) { return false; }
            if (!llvm::is_contained(bindings, read.getCell())) { continue; }
            for (mlir::Operation * user : read->getUsers()) {
                (void)user;
                if (!step()) { return false; }
            }
        }
        llvm::erase_if(cellReads, [&](ctjs::CellGetOp read) {
            if (!llvm::is_contained(bindings, read.getCell())) { return false; }
            cellOperations.erase(read);
            read.getResult().replaceAllUsesWith(map.getResult());
            eraseRooted(read);
            return true;
        });
        llvm::DenseSet<mlir::Operation *> operations;
        llvm::DenseSet<mlir::Operation *> conditional;
        llvm::SmallVector<ctjs::GetPropertyOp> selections;
        for (mlir::OpOperand * use : sourceUses(map.getResult())) {
            if (!step()) { return false; }
            auto * op = use->getOwner();
            if (llvm::isa<ctjs::RootOp>(op)) { continue; }
            auto * position = entryPosition(op);
            if (position->getBlock() != &entry || !map->isBeforeInBlock(position)) {
                return refuse("class nested Map requires ordered entry operations");
            }
            for (auto * parent = op->getParentOp(); parent != scope;
                 parent = parent->getParentOp()) {
                if (!step()) { return false; }
                conditional.insert(parent);
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op);
                call && use->getOperandNumber() == 1) {
                auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (read && read.getObject() == call.getReceiver()) { continue; }
            }
            auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
            if (!read || use->getOperandNumber() != 0) {
                return refuse("class nested Map outer owner escapes");
            }
            selections.push_back(read);
            const auto key = ctjs::constantKey(read.getKey());
            if (key == "size") {
                operations.insert(read);
                continue;
            }
            const unsigned arity = key == "set" ? 2u : key == "clear" ? 0u : 1u;
            if (key != "set" && key != "get" && key != "has" && key != "delete" && key != "clear") {
                return refuse("class nested Map has an unproved member");
            }
            for (mlir::OpOperand & selected : read.getResult().getUses()) {
                if (!step()) { return false; }
                if (llvm::isa<ctjs::RootOp>(selected.getOwner())) { continue; }
                auto call = llvm::dyn_cast<ctjs::CallOp>(selected.getOwner());
                if (!call || selected.getOperandNumber() != 0 ||
                    call.getReceiver() != read.getObject() || call.getArgs().size() != arity ||
                    entryPosition(call)->getBlock() != &entry) {
                    return refuse("class nested Map method escapes its exact receiver call");
                }
                operations.insert(call);
            }
        }
        llvm::StringMap<mlir::Value> entries;
        llvm::MapVector<mlir::Operation *, mlir::Attribute> constants;
        llvm::MapVector<mlir::Value, mlir::Value> children;
        llvm::MapVector<mlir::scf::IfOp, bool> branches;
        llvm::DenseMap<mlir::Value, mlir::Value> branchValues;
        llvm::DenseMap<mlir::Value, llvm::StringMap<mlir::Value>> childEntries;
        llvm::DenseMap<mlir::Value, bool> lookupTruth;
        llvm::DenseMap<mlir::Value, mlir::Value> lookupOwners;
        llvm::DenseSet<mlir::Operation *> lookupTests;
        llvm::SmallVector<ctjs::CallOp> missingReads;
        llvm::DenseSet<mlir::Value> observedChildren;
        llvm::SmallVector<ctjs::ConstructOp> created;
        llvm::DenseSet<mlir::Operation *> visited;
        const auto resolved = [&](mlir::Value value) {
            value = sourceValue(value);
            while (true) {
                auto next = branchValues.lookup(value);
                if (!next) { next = children.lookup(value); }
                if (!next || !step()) { return value; }
                value = sourceValue(next);
            }
        };
        const auto constant = [&](mlir::Value value) -> mlir::Attribute {
            value = resolved(value);
            auto * op = value.getDefiningOp();
            if (auto known = constants.lookup(op)) { return known; }
            if (auto literal = llvm::dyn_cast_or_null<ctjs::ConstantOp>(op)) {
                return literal.getValue();
            }
            return {};
        };
        const auto truth = [](mlir::Attribute value) -> std::optional<bool> {
            if (llvm::isa_and_nonnull<ctjs::NullAttr, ctjs::UndefinedAttr>(value)) { return false; }
            if (auto flag = llvm::dyn_cast_or_null<ctjs::BooleanAttr>(value)) {
                return flag.getValue();
            }
            if (auto number = llvm::dyn_cast_or_null<ctjs::NumberAttr>(value)) {
                const double n = number.getDouble();
                return n != 0 && n == n;
            }
            return std::nullopt;
        };
        const auto valueTruth = [&](mlir::Value value) -> std::optional<bool> {
            auto found = lookupTruth.find(resolved(value));
            return found == lookupTruth.end() ? truth(constant(value)) : found->second;
        };
        // This is only a candidate origin for the both-arm effect census.
        // The ordered visit must still resolve an actually present child.
        const auto mapCandidate = [&](auto && self, mlir::Value value, unsigned depth) -> bool {
            if (!step() || depth == 64) { return false; }
            value = sourceValue(value);
            if (localMap(value)) { return true; }
            if (auto call = value.getDefiningOp<ctjs::CallOp>()) {
                auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                return read && sourceValue(call.getReceiver()) == map.getResult() &&
                       read.getObject() == call.getReceiver() &&
                       ctjs::constantKey(read.getKey()) == "get" && call.getArgs().size() == 1;
            }
            auto branch = value.getDefiningOp<mlir::scf::IfOp>();
            if (!branch || branch.getNumResults() != 1) { return false; }
            for (auto & region : branch->getRegions()) {
                if (!region.hasOneBlock()) { return false; }
                auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
                if (!yield || yield.getNumOperands() != 1 ||
                    !self(self, yield.getOperand(0), depth + 1)) {
                    return false;
                }
            }
            return true;
        };
        llvm::SmallVector<mlir::Value> observations;
        const auto childBranches = scope.walk([&](ctjs::GetPropertyOp read) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (!mapCandidate(mapCandidate, read.getObject(), 0)) {
                return mlir::WalkResult::advance();
            }
            if (entryPosition(read)->getBlock() != &entry) {
                refuse("class nested Map child requires ordered entry operations");
                return mlir::WalkResult::interrupt();
            }
            if (ctjs::constantKey(read.getKey()) == "get") {
                observations.push_back(read.getResult());
            }
            for (auto * parent = read->getParentOp(); parent != scope;
                 parent = parent->getParentOp()) {
                if (!step()) { return mlir::WalkResult::interrupt(); }
                conditional.insert(parent);
            }
            return mlir::WalkResult::advance();
        });
        if (childBranches.wasInterrupted() || !reason.empty()) { return false; }
        // A getter's final || null follows its inner && rather than enclosing
        // the Map reads. Include that result flow in the same both-arm census.
        llvm::DenseSet<mlir::Value> observed;
        while (!observations.empty()) {
            auto value = observations.pop_back_val();
            if (!observed.insert(value).second) { continue; }
            for (mlir::OpOperand * use : sourceUses(value)) {
                if (!step()) { return false; }
                auto * op = use->getOwner();
                if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
                    auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                    if (use->getOperandNumber() == 0 && read &&
                        call.getReceiver() == read.getObject() &&
                        mapCandidate(mapCandidate, read.getObject(), 0)) {
                        observations.push_back(call.getResult());
                    }
                } else if (llvm::isa<ctjs::TruthyOp, ctjs::FromBoolOp, ctjs::UnaryOp,
                                     ctjs::CompareOp>(op)) {
                    observations.push_back(op->getResult(0));
                } else {
                    auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op);
                    if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(op)) {
                        branch = llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp());
                    }
                    if (!branch) { continue; }
                    if (entryPosition(branch)->getBlock() != &entry) {
                        return refuse("class nested Map result requires ordered entry branches");
                    }
                    for (auto * parent = branch.getOperation(); parent != scope;
                         parent = parent->getParentOp()) {
                        if (!step()) { return false; }
                        conditional.insert(parent);
                    }
                    llvm::append_range(observations, branch.getResults());
                }
            }
        }
        // Check both arms before discarding either. These operations cannot
        // retain cached class/cell/closure facts or invoke user code.
        for (mlir::Operation * op : conditional) {
            auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op);
            if (!branch || branch.getNumResults() > 1) {
                return refuse("class nested Map branch carries unproved values");
            }
            const auto checked = branch.walk([&](mlir::Operation * nested) {
                if (!step()) { return mlir::WalkResult::interrupt(); }
                if (llvm::isa<ctjs::ConstantOp, ctjs::RootOp, ctjs::TruthyOp, ctjs::FromBoolOp,
                              mlir::scf::YieldOp, mlir::scf::IfOp>(nested)) {
                    return mlir::WalkResult::advance();
                }
                if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(nested);
                    unary && unary.getKind() == ctjs::UnaryKind::Not) {
                    return mlir::WalkResult::advance();
                }
                if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(nested);
                    compare && compare.getKind() == ctjs::CompareKind::StrictEq) {
                    return mlir::WalkResult::advance();
                }
                if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(nested);
                    load && load.getName() == "Map") {
                    return mlir::WalkResult::advance();
                }
                if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(nested);
                    made && localMap(made.getResult())) {
                    return mlir::WalkResult::advance();
                }
                auto call = llvm::dyn_cast<ctjs::CallOp>(nested);
                auto read = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                                 : llvm::dyn_cast<ctjs::GetPropertyOp>(nested);
                if (read && mapCandidate(mapCandidate, read.getObject(), 0)) {
                    const auto key = ctjs::constantKey(read.getKey());
                    const unsigned arity = key == "set" ? 2u : key == "clear" ? 0u : 1u;
                    if ((key == "set" || key == "get" || key == "has" || key == "delete" ||
                         key == "clear" || (!call && key == "size")) &&
                        (!call || (call.getReceiver() == read.getObject() &&
                                   call.getArgs().size() == arity))) {
                        return mlir::WalkResult::advance();
                    }
                }
                refuse("class nested Map branch contains an unproved effect or observer");
                return mlir::WalkResult::interrupt();
            });
            if (checked.wasInterrupted()) { return false; }
        }
        // A set returns the outer identity. Only unused yield transport can
        // disappear with that owner; a cell, observer or escape must still refuse.
        const auto unusedResult = [&](mlir::Value value) {
            llvm::SmallVector<mlir::Value> pending{value};
            llvm::DenseSet<mlir::Value> seen;
            while (!pending.empty()) {
                auto current = pending.pop_back_val();
                if (!seen.insert(current).second) { continue; }
                for (mlir::OpOperand & use : current.getUses()) {
                    if (!step()) { return false; }
                    if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                    auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(use.getOwner());
                    auto branch = yield ? llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp())
                                        : mlir::scf::IfOp{};
                    if (!branch || !conditional.contains(branch) || branch.getNumResults() != 1 ||
                        use.getOperandNumber() != 0) {
                        return false;
                    }
                    pending.push_back(branch.getResult(0));
                }
            }
            return true;
        };
        for (mlir::Operation * op : operations) {
            if (!step()) { return false; }
            auto call = llvm::dyn_cast<ctjs::CallOp>(op);
            if (!call) { continue; }
            const auto action =
                ctjs::constantKey(call.getCallee().getDefiningOp<ctjs::GetPropertyOp>().getKey());
            if ((action == "clear" || action == "set") && !unusedResult(call.getResult())) {
                return refuse("class nested Map mutation result escapes");
            }
        }
        const auto visit = [&](auto && self, mlir::Block & block, unsigned depth) -> bool {
            if (depth == 64) { return refuse("class nested Map branch nesting exhausted"); }
            for (mlir::Operation & op : block) {
                if (!step()) { return false; }
                visited.insert(&op);
                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op);
                    branch && conditional.contains(branch)) {
                    auto known = valueTruth(branch.getCondition());
                    if (!known) {
                        return refuse("class nested Map branch condition is not proved");
                    }
                    branches[branch] = *known;
                    auto & selected = *known ? branch.getThenRegion() : branch.getElseRegion();
                    if (selected.empty()) { continue; }
                    if (!selected.hasOneBlock() || selected.front().getNumArguments() ||
                        !llvm::isa<mlir::scf::YieldOp>(selected.front().getTerminator()) ||
                        !self(self, selected.front(), depth + 1)) {
                        return refuse("class nested Map branch lacks an exact selected arm");
                    }
                    auto yield = llvm::cast<mlir::scf::YieldOp>(selected.front().getTerminator());
                    if (yield.getNumOperands() != branch.getNumResults()) {
                        return refuse("class nested Map branch lacks an exact selected result");
                    }
                    for (auto [result, value] :
                         llvm::zip(branch.getResults(), yield.getOperands())) {
                        if (!step()) { return false; }
                        branchValues[result] = resolved(value);
                    }
                    continue;
                }
                if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(op);
                    made && localMap(made.getResult())) {
                    if (&block != &entry) { created.push_back(made); }
                    if (made != map) { childEntries.try_emplace(made.getResult()); }
                }
                if (auto test = llvm::dyn_cast<ctjs::TruthyOp>(op)) {
                    if (auto flag = valueTruth(test.getValue())) {
                        constants[&op] = ctjs::BooleanAttr::get(module.getContext(), *flag);
                        if (lookupTruth.contains(resolved(test.getValue()))) {
                            lookupTests.insert(&op);
                            observedChildren.insert(lookupOwners.lookup(resolved(test.getValue())));
                        }
                    }
                } else if (auto boxed = llvm::dyn_cast<ctjs::FromBoolOp>(op)) {
                    if (auto value = constant(boxed.getBit())) { constants[&op] = value; }
                } else if (auto test = llvm::dyn_cast<ctjs::UnaryOp>(op);
                           test && test.getKind() == ctjs::UnaryKind::Not) {
                    if (auto flag = truth(constant(test.getOperand()))) {
                        constants[&op] = ctjs::BooleanAttr::get(module.getContext(), !*flag);
                    }
                } else if (auto test = llvm::dyn_cast<ctjs::CompareOp>(op);
                           test && test.getKind() == ctjs::CompareKind::StrictEq) {
                    auto left = constant(test.getLhs()), right = constant(test.getRhs());
                    if (left && right && truth(left).has_value() && truth(right).has_value()) {
                        bool equal = left == right;
                        if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(left)) {
                            auto other = llvm::dyn_cast<ctjs::NumberAttr>(right);
                            equal = other && number.getDouble() == other.getDouble();
                        }
                        constants[&op] = ctjs::BooleanAttr::get(module.getContext(), equal);
                    }
                }
                if (!operations.contains(&op)) {
                    auto call = llvm::dyn_cast<ctjs::CallOp>(op);
                    auto read = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                                     : llvm::dyn_cast<ctjs::GetPropertyOp>(op);
                    auto child = read ? localMap(resolved(read.getObject())) : ctjs::ConstructOp{};
                    if (!child || child == map) { continue; }
                    auto found = childEntries.find(child.getResult());
                    if (found == childEntries.end()) {
                        return refuse("class nested Map child lacks an ordered allocation");
                    }
                    auto & contents = found->second;
                    const auto action = ctjs::constantKey(read.getKey());
                    if (!call) {
                        if (action == "size") {
                            constants[&op] = ctjs::NumberAttr::get(
                                module.getContext(),
                                std::bit_cast<uint64_t>(static_cast<double>(contents.size())));
                            observedChildren.insert(child.getResult());
                        }
                        continue;
                    }
                    const unsigned arity = action == "set" ? 2u : action == "clear" ? 0u : 1u;
                    if (call.getReceiver() != read.getObject() || call.getArgs().size() != arity ||
                        (action != "set" && action != "get" && action != "has" &&
                         action != "delete" && action != "clear")) {
                        return refuse("class nested Map child has an unproved invocation");
                    }
                    if (action == "clear") {
                        contents.clear();
                        continue;
                    }
                    auto key =
                        llvm::dyn_cast_or_null<ctjs::StringAttr>(constant(call.getArgs()[0]));
                    if (!key) {
                        return refuse("class nested Map child requires literal String keys");
                    }
                    if (action == "set") {
                        contents[key.getValue()] = resolved(call.getArgs()[1]);
                    } else if (action == "get") {
                        auto record = contents.lookup(key.getValue());
                        if (!record || record.getDefiningOp<ctjs::ConstructOp>()) {
                            lookupTruth[call.getResult()] = bool(record);
                            lookupOwners[call.getResult()] = child.getResult();
                            if (!record) { missingReads.push_back(call); }
                        }
                    } else {
                        constants[&op] = ctjs::BooleanAttr::get(module.getContext(),
                                                                contents.contains(key.getValue()));
                        observedChildren.insert(child.getResult());
                        if (action == "delete") { contents.erase(key.getValue()); }
                    }
                    continue;
                }
                if (llvm::isa<ctjs::GetPropertyOp>(op)) {
                    constants[&op] = ctjs::NumberAttr::get(
                        module.getContext(),
                        std::bit_cast<uint64_t>(static_cast<double>(entries.size())));
                    continue;
                }
                auto call = llvm::cast<ctjs::CallOp>(op);
                const auto action = ctjs::constantKey(
                    call.getCallee().getDefiningOp<ctjs::GetPropertyOp>().getKey());
                if (action == "clear") {
                    entries.clear();
                    continue;
                }
                auto key = resolved(call.getArgs()[0]).getDefiningOp<ctjs::ConstantOp>();
                auto text =
                    key ? llvm::dyn_cast<ctjs::StringAttr>(key.getValue()) : ctjs::StringAttr{};
                if (!text) { return refuse("class nested Map requires literal String keys"); }
                if (action == "set") {
                    auto value = resolved(call.getArgs()[1]);
                    auto child = localMap(value);
                    if (!child || child == map ||
                        (!maps.contains(child.getResult()) &&
                         !llvm::is_contained(created, child))) {
                        return refuse("class nested Map requires a selected child owner");
                    }
                    entries[text.getValue()] = child.getResult();
                } else if (action == "get") {
                    auto child = entries.lookup(text.getValue());
                    if (!child) { return refuse("class nested Map get requires a present child"); }
                    children[call.getResult()] = child;
                } else {
                    const bool present = entries.count(text.getValue()) != 0;
                    constants[&op] = ctjs::BooleanAttr::get(module.getContext(), present);
                    if (action == "delete") { entries.erase(text.getValue()); }
                }
            }
            return true;
        };
        if (!visit(visit, entry, 0)) { return false; }
        // A second outer owner or a captured observer can mutate a child through
        // an alias this visit did not resolve. Check every alias before folding.
        for (mlir::Value child : observedChildren) {
            llvm::SmallVector<mlir::Value> aliases{child};
            for (auto [read, value] : children) {
                if (!step()) { return false; }
                if (resolved(value) == child) { aliases.push_back(read); }
            }
            for (auto [result, value] : branchValues) {
                if (!step()) { return false; }
                if (resolved(value) == child) { aliases.push_back(result); }
            }
            for (auto [cell, initial] : cells) {
                if (!step()) { return false; }
                if (resolved(initial) != child) { continue; }
                for (mlir::Operation * user : cell.getUsers()) {
                    if (!step()) { return false; }
                    if (!cellOperations.contains(user)) {
                        return refuse("class nested Map child occupancy has a captured observer");
                    }
                }
            }
            for (mlir::Value alias : aliases) {
                for (mlir::OpOperand * use : sourceUses(alias)) {
                    if (!step()) { return false; }
                    auto * op = use->getOwner();
                    if (llvm::isa<ctjs::RootOp>(op)) { continue; }
                    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
                        read && use->getOperandNumber() == 0) {
                        continue;
                    }
                    if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(op);
                        yield && conditional.contains(yield->getParentOp())) {
                        continue;
                    }
                    auto call = llvm::dyn_cast<ctjs::CallOp>(op);
                    auto read = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                                     : ctjs::GetPropertyOp{};
                    if (read && read.getObject() == call.getReceiver() &&
                        (use->getOperandNumber() == 1 ||
                         (use->getOperandNumber() == 3 && operations.contains(call) &&
                          ctjs::constantKey(read.getKey()) == "set"))) {
                        continue;
                    }
                    return refuse("class nested Map child occupancy has an outside alias");
                }
            }
        }
        // Keep child operations for ownership proof. Only outer observations
        // and proved lookup truth tests need materialized replacements.
        for (auto at = constants.begin(); at != constants.end();) {
            if (!operations.contains(at->first) && !lookupTests.contains(at->first)) {
                at = constants.erase(at);
            } else {
                ++at;
            }
        }
        // Charge rewriting before mutation; the enclosing pass discards its
        // private module if any later child/record/effect proof fails.
        for (mlir::Operation * op : operations) {
            if (!step()) { return false; }
            if (constants.contains(op) && (!step() || !step())) { return false; }
            for (mlir::OpOperand & use : op->getResult(0).getUses()) {
                (void)use;
                if (!step()) { return false; }
            }
        }
        for (mlir::Operation * op : lookupTests) {
            if (!step() || !step()) { return false; }
            for (mlir::Operation * user : op->getUsers()) {
                (void)user;
                if (!step()) { return false; }
            }
        }
        for (ctjs::CallOp read : missingReads) {
            if (!step()) { return false; }
            for (mlir::Operation * user : read->getUsers()) {
                (void)user;
                if (!step()) { return false; }
            }
        }
        for (auto [cell, initial] : cells) {
            (void)cell;
            (void)initial;
            if (!step()) { return false; }
        }
        for (auto [branch, selected] : branches) {
            (void)selected;
            for (mlir::Value result : branch.getResults()) {
                for (mlir::Operation * user : result.getUsers()) {
                    (void)user;
                    if (!step()) { return false; }
                }
                for (auto [cell, initial] : cells) {
                    (void)cell;
                    (void)initial;
                    if (!step()) { return false; }
                }
            }
        }
        for (mlir::Value cell : bindings) {
            for (mlir::Operation * user : cell.getUsers()) {
                (void)user;
                if (!step()) { return false; }
            }
        }
        if (!reason.empty()) { return false; }
        llvm::erase_if(selections, [&](ctjs::GetPropertyOp read) {
            return !visited.contains(read.getOperation());
        });
        // DenseSet erasure can relocate other buckets, invalidating iteration.
        operations.remove_if([&](mlir::Operation * op) { return !visited.contains(op); });
        // Repair result and cell aliases before erasing their SSA producers.
        // Reverse order keeps nested yields live until their contents move.
        for (auto [branch, selected] : llvm::reverse(branches)) {
            auto & region = selected ? branch.getThenRegion() : branch.getElseRegion();
            if (!region.empty()) {
                auto & contents = region.front().getOperations();
                auto yield = llvm::cast<mlir::scf::YieldOp>(region.front().getTerminator());
                for (auto [result, value] : llvm::zip(branch.getResults(), yield.getOperands())) {
                    result.replaceAllUsesWith(value);
                    for (auto & [cell, initial] : cells) {
                        (void)cell;
                        if (initial == result) { initial = value; }
                    }
                }
                branch->getBlock()->getOperations().splice(branch->getIterator(), contents,
                                                           contents.begin(), yield->getIterator());
            }
            branch.erase();
        }
        for (ctjs::ConstructOp made : created) {
            maps.insert(made.getResult());
            mapOperations.insert(made);
            mapOperations.insert(made.getCallee().getDefiningOp());
        }
        for (auto [read, child] : children) { read.replaceAllUsesWith(child); }
        for (auto [op, value] : constants) {
            mlir::OpBuilder at(op);
            mlir::Value literal;
            if (lookupTests.contains(op)) {
                literal = mlir::arith::ConstantIntOp::create(
                    at, op->getLoc(), llvm::cast<ctjs::BooleanAttr>(value).getValue(), 1);
            } else {
                literal = ctjs::ConstantOp::create(at, op->getLoc(), value);
            }
            children[op->getResult(0)] = literal;
            op->getResult(0).replaceAllUsesWith(literal);
        }
        // Cell facts cache SSA values; RAUW alone does not update this table.
        for (auto & [cell, initial] : cells) {
            (void)cell;
            if (auto replacement = children.lookup(initial)) { initial = replacement; }
        }
        for (mlir::Operation * op : lookupTests) { eraseRooted(op); }
        // Missing lookup results may disappear only after every observer was
        // discharged. Present reads retain the existing record/lifetime proof.
        for (ctjs::CallOp read : missingReads) {
            if (llvm::all_of(read->getUsers(), [](mlir::Operation * user) {
                    return llvm::isa<ctjs::RootOp>(user);
                })) {
                mapOperations.erase(read);
                eraseRooted(read);
            }
        }
        for (mlir::Operation * op : operations) {
            if (llvm::isa<ctjs::GetPropertyOp>(op)) { continue; }
            mapOperations.erase(op);
            eraseRooted(op);
        }
        for (ctjs::GetPropertyOp read : selections) {
            mapOperations.erase(read);
            eraseRooted(read);
        }
        // Fixed outer aliases are transport only; no captured observer remains.
        for (mlir::Value cell : bindings) {
            for (mlir::Operation * user : llvm::make_early_inc_range(cell.getUsers())) {
                cellOperations.erase(user);
                user->erase();
            }
            cellOperations.erase(cell.getDefiningOp());
            cells.erase(cell);
            cell.getDefiningOp()->erase();
        }
        maps.erase(map.getResult());
        mapOperations.erase(map);
        auto load = map.getCallee().getDefiningOp();
        eraseRooted(map);
        if (llvm::all_of(load->getUsers(),
                         [](mlir::Operation * user) { return llvm::isa<ctjs::RootOp>(user); })) {
            mapOperations.erase(load);
            eraseRooted(load);
        }
    }
    return reason.empty();
}

bool classInitialization::normalizeCapturedMapHelpers(ctjs::FuncOp scope) {
    // ponytail: Map-only helpers called in their owning entry block.
    // Nested helper calls and other regions need a wider capture/lifetime proof.
    auto & entry = scope.getBody().front();
    llvm::SmallVector<ctjs::CreateClosureOp> closures;
    for (auto closure : entry.getOps<ctjs::CreateClosureOp>()) {
        if (!step()) { return false; }
        closures.push_back(closure);
    }
    const auto expand = [&](ctjs::CreateClosureOp helper) {
        auto function = target(helper);
        if (!function || helper.getUpvalues().size() != 1 || function.getUpvalueCount() != 1 ||
            !function.getBody().hasOneBlock() || !undefined(helper.getEnclosingThis()) ||
            (helper.getEnclosingIndicesAttr() &&
             llvm::any_of(helper.getEnclosingIndicesAttr().asArrayRef(),
                          [](int32_t index) { return index >= 0; }))) {
            return true;
        }
        auto mapCell = helper.getUpvalues().front();
        auto map = sourceValue(cells.lookup(mapCell)).getDefiningOp<ctjs::ConstructOp>();
        if (!map || !maps.contains(map.getResult()) || map->getBlock() != &entry) { return true; }
        auto & body = function.getBody().front();
        if (!body.getArgument(ctjs::arg_receiver).use_empty() ||
            !body.getArgument(ctjs::arg_new_target).use_empty()) {
            return true;
        }
        auto returned = llvm::dyn_cast<ctjs::ReturnOp>(body.getTerminator());
        if (!returned) { return true; }
        llvm::DenseSet<mlir::Value> captures;
        const auto checked = function.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * op) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (op == function) { return mlir::WalkResult::advance(); }
            if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op)) {
                if (read.getIndex() != 0 ||
                    read.getClosure() != body.getArgument(ctjs::arg_callee)) {
                    return mlir::WalkResult::interrupt();
                }
                captures.insert(read.getResult());
                return mlir::WalkResult::advance();
            }
            if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
                return load.getName() == "Map" ? mlir::WalkResult::advance()
                                               : mlir::WalkResult::interrupt();
            }
            if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(op)) {
                auto load = made.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
                if (!load || load.getName() != "Map" || !made.getArgs().empty() ||
                    made.getNewTarget() != made.getCallee()) {
                    return mlir::WalkResult::interrupt();
                }
                captures.insert(made.getResult());
                return mlir::WalkResult::advance();
            }
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
                if (!captures.contains(read.getObject())) { return mlir::WalkResult::interrupt(); }
                const auto key = ctjs::constantKey(read.getKey());
                if (key != "size" && key != "set" && key != "get" && key != "has" &&
                    key != "delete" && key != "clear") {
                    return mlir::WalkResult::interrupt();
                }
                return mlir::WalkResult::advance();
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
                auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (!read || !captures.contains(call.getReceiver()) ||
                    read.getObject() != call.getReceiver()) {
                    return mlir::WalkResult::interrupt();
                }
                const auto key = ctjs::constantKey(read.getKey());
                const unsigned arity = key == "set" ? 2u : key == "clear" ? 0u : 1u;
                if (key == "size" || call.getArgs().size() != arity) {
                    return mlir::WalkResult::interrupt();
                }
                // A get is only a candidate child origin. Nested routing and
                // the unchanged record-owner proof must establish its identity.
                if (key == "get") { captures.insert(call.getResult()); }
                return mlir::WalkResult::advance();
            }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
                if (branch.getNumResults() > 1) { return mlir::WalkResult::interrupt(); }
                unsigned depth = 0;
                for (auto * parent = op; parent != function; parent = parent->getParentOp()) {
                    if (!step()) { return mlir::WalkResult::interrupt(); }
                    if (++depth == 64) { return mlir::WalkResult::interrupt(); }
                }
                for (auto & region : branch->getRegions()) {
                    if (!region.empty() &&
                        (!region.hasOneBlock() || region.front().getNumArguments())) {
                        return mlir::WalkResult::interrupt();
                    }
                }
                for (mlir::Value result : branch.getResults()) { captures.insert(result); }
                return mlir::WalkResult::advance();
            }
            if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op);
                unary && unary.getKind() == ctjs::UnaryKind::Not) {
                return mlir::WalkResult::advance();
            }
            if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(op);
                compare && compare.getKind() == ctjs::CompareKind::StrictEq) {
                return mlir::WalkResult::advance();
            }
            if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::ReturnOp>(op)) {
                return op->getBlock() == &body ? mlir::WalkResult::advance()
                                               : mlir::WalkResult::interrupt();
            }
            return llvm::isa<ctjs::ConstantOp, ctjs::RootOp, ctjs::TruthyOp, ctjs::FromBoolOp,
                             mlir::scf::YieldOp>(op)
                       ? mlir::WalkResult::advance()
                       : mlir::WalkResult::interrupt();
        });
        if (checked.wasInterrupted()) { return reason.empty(); }
        for (mlir::OpOperand & use : body.getArgument(ctjs::arg_callee).getUses()) {
            if (!step()) { return false; }
            if (!llvm::isa<ctjs::LoadUpvalueOp, ctjs::RootOp>(use.getOwner())) { return true; }
        }
        mlir::Value helperCell;
        for (auto [cell, initial] : cells) {
            if (!step()) { return false; }
            if (sourceValue(initial) != helper.getResult()) { continue; }
            if (helperCell) { return true; }
            helperCell = cell;
        }
        if (helperCell) {
            for (mlir::Operation * user : helperCell.getUsers()) {
                if (!step()) { return false; }
                // sourceUses omits capture transport: a second observer must
                // not survive retirement of this binding.
                if (!cellOperations.contains(user)) { return true; }
            }
        }
        llvm::SmallVector<ctjs::GetPropertyOp> holderReads;
        ctjs::SetPropertyOp publication;
        auto uses = sourceUses(helper.getResult());
        llvm::SmallVector<mlir::Operation *> invocations;
        for (size_t i = 0; i < uses.size(); ++i) {
            mlir::OpOperand * use = uses[i];
            if (!step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use->getOwner())) { continue; }
            if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(use->getOwner())) {
                auto object = store.getObject().getDefiningOp<ctjs::CreateObjectOp>();
                if (publication || use->getOperandNumber() != 2 || !object ||
                    object->getBlock() != &entry) {
                    return true;
                }
                // Include the original constructor's captured reads in the
                // holder census. Its registration slot stays intact because
                // this expansion only accepts calls in the owning entry block.
                auto holderUses = sourceUses(object.getResult());
                llvm::DenseMap<mlir::Value, mlir::Operation *> captureSites;
                for (auto [cell, initial] : cells) {
                    if (!step()) { return false; }
                    if (sourceValue(initial) != object.getResult()) { continue; }
                    for (mlir::OpOperand & selected : cell.getUses()) {
                        if (!step()) { return false; }
                        if (cellOperations.contains(selected.getOwner())) { continue; }
                        auto constructor =
                            llvm::dyn_cast<ctjs::CreateClosureOp>(selected.getOwner());
                        auto fn = target(constructor);
                        if (!fn || constructor->getBlock() != &entry ||
                            constructor.getUpvalues().size() != 1 ||
                            constructor.getUpvalues().front() != cell ||
                            fn.getUpvalueCount() != 1 || !fn.getBody().hasOneBlock()) {
                            return true;
                        }
                        bool classConstructor = false;
                        for (ctjs::CallOp setup : calls) {
                            if (!step()) { return false; }
                            classConstructor |= setup.getArgs().size() == 1 &&
                                                setup.getArgs().front() == constructor.getResult();
                        }
                        if (!classConstructor) { return true; }
                        auto & constructorBody = fn.getBody().front();
                        for (mlir::OpOperand & use :
                             constructorBody.getArgument(ctjs::arg_callee).getUses()) {
                            if (!step()) { return false; }
                            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                            auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(use.getOwner());
                            if (!load || load.getIndex() != 0 ||
                                load->getBlock() != &constructorBody) {
                                return true;
                            }
                            captureSites[load.getResult()] = constructor;
                            for (mlir::OpOperand & capturedUse : load.getResult().getUses()) {
                                if (!step()) { return false; }
                                holderUses.push_back(&capturedUse);
                            }
                        }
                    }
                }
                auto holder = analyzeLocalCallableObject(
                    object, [&] { return step(); }, [&](mlir::Value) { return holderUses; });
                if (!holder) {
                    llvm::consumeError(holder.takeError());
                    return reason.empty();
                }
                for (ctjs::SetPropertyOp slot : holder->stores) {
                    if (!step()) { return false; }
                    if (slot.getValue() == helper.getResult() && slot != store) { return true; }
                }
                for (auto [read, closure] : holder->reads) {
                    if (!step()) { return false; }
                    auto * position = captureSites.lookup(read.getObject());
                    if (!position) { position = read; }
                    for (ctjs::SetPropertyOp slot : holder->stores) {
                        if (!step()) { return false; }
                        if (position->getBlock() != &entry || !slot->isBeforeInBlock(position)) {
                            return true;
                        }
                    }
                    if (closure != helper) { continue; }
                    holderReads.push_back(read);
                    for (mlir::OpOperand & selected : read.getResult().getUses()) {
                        if (!step()) { return false; }
                        uses.push_back(&selected);
                    }
                }
                publication = store;
                continue;
            }
            auto * call = use->getOwner();
            mlir::ValueRange arguments;
            if (auto dynamic = llvm::dyn_cast<ctjs::CallOp>(call)) {
                auto read = dynamic.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                const bool holderCall = read && llvm::is_contained(holderReads, read);
                if (use->getOperandNumber() != 0 ||
                    (holderCall ? dynamic.getReceiver() != read.getObject()
                                : !undefined(dynamic.getReceiver()))) {
                    return true;
                }
                arguments = dynamic.getArgs();
            } else if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(call)) {
                if (direct.getCalleeValue().getDefiningOp<ctjs::GetPropertyOp>() ||
                    use->getOperandNumber() != 2 || direct.getTarget() != function ||
                    !undefined(direct.getReceiver()) || !undefined(direct.getNewTarget())) {
                    return true;
                }
                arguments = direct.getArgs();
            } else {
                return true;
            }
            if (call->getBlock() != &entry || !helper->isBeforeInBlock(call) ||
                !map->isBeforeInBlock(call) ||
                arguments.size() != body.getNumArguments() - ctjs::implicit_arguments) {
                return true;
            }
            // Capturing a hoisted cell does not read it. Every invocation
            // must follow initialization of this exact captured Map cell.
            bool initialized =
                mapCell.getDefiningOp<ctjs::CreateCellOp>().getInitial() == map.getResult();
            for (mlir::Operation * user : mapCell.getUsers()) {
                if (!step()) { return false; }
                auto write = llvm::dyn_cast<ctjs::CellSetOp>(user);
                initialized |= write && write->getBlock() == &entry &&
                               write->isBeforeInBlock(call) &&
                               sourceValue(write.getValue()) == map.getResult();
            }
            if (!initialized) { return true; }
            invocations.push_back(call);
        }
        if (invocations.empty()) { return true; }
        for (const auto & uses : {mlir::SymbolTable::getSymbolUses(module.getOperation()),
                                  mlir::SymbolTable::getSymbolUses(&module.getBodyRegion())}) {
            if (!uses) { return true; }
            for (const auto & use : *uses) {
                if (!step()) { return false; }
                if (mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                        use.getUser(), use.getSymbolRef()) == function &&
                    !llvm::is_contained(invocations, use.getUser())) {
                    return true;
                }
            }
        }
        // Precharge expansion and cleanup before the first mutation. The
        // enclosing pass owns a disposable module if a later proof refuses.
        for (mlir::Operation * call : invocations) {
            if (!step()) { return false; }
            for (mlir::OpOperand & use : call->getResult(0).getUses()) {
                (void)use;
                if (!step()) { return false; }
            }
            for (auto [cell, initial] : cells) {
                (void)cell;
                (void)initial;
                if (!step()) { return false; }
            }
            const auto counted = function.walk([&](mlir::Operation * op) {
                const uint64_t cost = uint64_t(1) + op->getNumOperands() + op->getNumResults();
                if (cost > remaining) {
                    refuse("class initialization work budget exhausted");
                    return mlir::WalkResult::interrupt();
                }
                remaining -= static_cast<unsigned>(cost);
                return mlir::WalkResult::advance();
            });
            if (counted.wasInterrupted()) { return false; }
        }
        for (ctjs::CellGetOp read : cellReads) {
            if (!step()) { return false; }
            if (read.getCell() != helperCell) { continue; }
            for (mlir::Operation * user : read->getUsers()) {
                (void)user;
                if (!step()) { return false; }
            }
        }
        for (mlir::Value value : {mlir::Value(helper.getResult()), helperCell}) {
            if (!value) { continue; }
            for (mlir::Operation * user : value.getUsers()) {
                (void)user;
                if (!step()) { return false; }
            }
        }
        if (publication) {
            for (mlir::Operation * user : publication.getObject().getUsers()) {
                (void)user;
                if (!step()) { return false; }
            }
        }
        if (!reason.empty()) { return false; }
        for (mlir::Operation * call : invocations) {
            mlir::IRMapping mapping;
            auto dynamic = llvm::dyn_cast<ctjs::CallOp>(call);
            auto arguments =
                dynamic ? dynamic.getArgs() : llvm::cast<ctjs::CallDirectOp>(call).getArgs();
            mapping.map(body.getArguments().drop_front(ctjs::implicit_arguments), arguments);
            mlir::OpBuilder at(call);
            for (mlir::Operation & op : body) {
                if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op)) {
                    mapping.map(read.getResult(), map.getResult());
                } else if (!llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp,
                                      ctjs::ReturnOp>(op)) {
                    auto * cloned = at.clone(op, mapping);
                    // Region cloning retains branch-local capture loads.
                    // Substitute each one before retiring the original callee.
                    cloned->walk([&](mlir::Operation * nested) {
                        if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(nested)) {
                            read.getResult().replaceAllUsesWith(map.getResult());
                            read.erase();
                        } else if (llvm::isa<ctjs::RootOp>(nested)) {
                            nested->erase();
                        }
                    });
                    if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(cloned)) {
                        maps.insert(made.getResult());
                        mapOperations.insert(made);
                        mapOperations.insert(made.getCallee().getDefiningOp());
                    }
                }
            }
            auto result = mapping.lookup(returned.getValue());
            for (auto & [cell, initial] : cells) {
                (void)cell;
                if (initial == call->getResult(0)) { initial = result; }
            }
            call->getResult(0).replaceAllUsesWith(result);
            call->erase();
        }
        for (ctjs::GetPropertyOp read : holderReads) { eraseRooted(read); }
        if (publication) {
            auto object = publication.getObject();
            publication.erase();
            if (llvm::all_of(object.getUsers(), [](mlir::Operation * user) {
                    return llvm::isa<ctjs::RootOp>(user);
                })) {
                eraseRooted(object.getDefiningOp());
            }
        }
        if (helperCell) {
            llvm::erase_if(cellReads, [&](ctjs::CellGetOp read) {
                if (read.getCell() != helperCell) { return false; }
                cellOperations.erase(read);
                eraseRooted(read);
                return true;
            });
            for (mlir::Operation * user : llvm::make_early_inc_range(helperCell.getUsers())) {
                cellOperations.erase(user);
                user->erase();
            }
            cellOperations.erase(helperCell.getDefiningOp());
            cells.erase(helperCell);
            helperCell.getDefiningOp()->erase();
        }
        earlyCaptures.erase(helper);
        eraseRooted(helper);
        functions.erase(*functionIndex(function));
        function.erase();
        return true;
    };
    for (ctjs::CreateClosureOp helper : closures) {
        if (!step() || !expand(helper)) { return false; }
    }
    return reason.empty();
}

} // namespace ctcompile::ctnative::class_detail
