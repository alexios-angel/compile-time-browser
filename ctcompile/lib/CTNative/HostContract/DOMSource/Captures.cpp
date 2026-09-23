#include "Proof.hpp"

namespace ctcompile::ctnative::dom_source_detail {

bool DOMSource::refuse(llvm::StringRef message) {
    if (reason.empty()) { reason = message.str(); }
    return false;
}

bool DOMSource::step() {
    if (!remaining) { return refuse("DOM helper expansion work budget exhausted"); }
    --remaining;
    return true;
}

bool DOMSource::chargeCaptureQuery() {
    // The shared immutable-capture query scans the module and target uses.
    for (unsigned scan = 0; scan < 6; ++scan) {
        if (remaining < operationCount) {
            return refuse("DOM helper expansion work budget exhausted");
        }
        remaining -= operationCount;
    }
    return true;
}

bool DOMSource::undefined(mlir::Value value) {
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    return constant && llvm::isa<ctjs::UndefinedAttr>(constant.getValue());
}

bool DOMSource::precedesInStructuredBody(mlir::Operation * definition, mlir::Operation * use) {
    // The selected arm, loop or protected call follows the definition.
    // Invoke continuations still require their own exception/state proof.
    while (use->getBlock() != definition->getBlock()) {
        if (!step()) { return false; }
        auto invocation = llvm::dyn_cast_or_null<ctjs::InvokeOp>(use->getParentOp());
        const bool protectedCall = invocation && use->getParentRegion() == &invocation.getBody() &&
                                   llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(use);
        if (!protectedCall &&
            !llvm::isa_and_nonnull<mlir::scf::IfOp, mlir::scf::WhileOp>(use->getParentOp())) {
            return false;
        }
        use = use->getParentOp();
    }
    return use->getBlock() == definition->getBlock() && definition->isBeforeInBlock(use);
}

bool DOMSource::confinedFilterCallback(ctjs::CreateClosureOp closure, ctjs::FuncOp target) {
    if (!target || !closure.getUpvalues().empty() || target.getUpvalueCount() != 0 ||
        creations.lookup(static_cast<unsigned>(closure.getFunction())) != 1 ||
        target.getBody().front().getNumArguments() != ctjs::implicit_arguments + 1 ||
        llvm::any_of(target.getBody().front().getArguments().take_front(ctjs::implicit_arguments),
                     [](mlir::BlockArgument argument) { return !argument.use_empty(); })) {
        return false;
    }
    bool invoked = false;
    for (mlir::OpOperand & use : closure.getResult().getUses()) {
        if (!step()) { return false; }
        if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        auto read =
            call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>() : ctjs::GetPropertyOp{};
        if (!call || use.getOperandNumber() != 2 || call.getArgs().size() != 1 || !read ||
            read.getObject() != call.getReceiver() ||
            ctjs::constantKey(read.getKey()) != "filter" ||
            call->getBlock() != closure->getBlock() || !closure->isBeforeInBlock(call)) {
            return false;
        }
        invoked = true;
    }
    return invoked;
}

bool DOMSource::confinedReplacementCallback(ctjs::CreateClosureOp closure, ctjs::FuncOp target) {
    if (!target || !closure.getUpvalues().empty() ||
        creations.lookup(static_cast<unsigned>(closure.getFunction())) != 1 ||
        !host_detail::isLowercaseReplacement(target, [&] { return step(); })) {
        return false;
    }
    bool invoked = false;
    for (mlir::OpOperand & use : closure.getResult().getUses()) {
        if (!step()) { return false; }
        if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        auto read =
            call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>() : ctjs::GetPropertyOp{};
        if (!call || use.getOperandNumber() != 3 || call.getArgs().size() != 2 || !read ||
            read.getObject() != call.getReceiver() ||
            ctjs::constantKey(read.getKey()) != "replace" ||
            !precedesInStructuredBody(closure, call)) {
            return false;
        }
        invoked = true;
    }
    return invoked;
}

bool DOMSource::bindConstantArguments(ctjs::CreateClosureOp closure, ctjs::FuncOp target) {
    if (target.getUpvalueCount() != 0 ||
        (closure && (!closure.getUpvalues().empty() ||
                     creations.lookup(static_cast<unsigned>(closure.getFunction())) != 1))) {
        return true;
    }
    auto & body = target.getBody().front();
    llvm::SmallVector<mlir::ValueRange> invocations;
    if (closure) {
        for (mlir::OpOperand & use : closure.getResult().getUses()) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
            auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
            mlir::ValueRange arguments;
            if (call && use.getOperandNumber() == 0 && undefined(call.getReceiver())) {
                arguments = call.getArgs();
            } else if (direct && use.getOperandNumber() == 2 &&
                       direct.getCallee() == target.getSymName() &&
                       undefined(direct.getReceiver()) && undefined(direct.getNewTarget())) {
                arguments = direct.getArgs();
            } else {
                return true;
            }
            if (!precedesInStructuredBody(closure, use.getOwner()) ||
                arguments.size() + ctjs::implicit_arguments != body.getNumArguments()) {
                return reason.empty();
            }
            invocations.push_back(arguments);
        }
    } else {
        // A private direct target has no remaining numeric closure. Census
        // every symbol use before specializing, not only the first caller.
        auto module = target->getParentOfType<mlir::ModuleOp>();
        if (!target.isPrivate() || creations.lookup(*functionIndex(target)) != 0) { return true; }
        if (remaining / 2 < operationCount) {
            return refuse("DOM helper expansion work budget exhausted");
        }
        remaining -= 2 * operationCount;
        for (const auto & uses : {mlir::SymbolTable::getSymbolUses(module.getOperation()),
                                  mlir::SymbolTable::getSymbolUses(&module.getBodyRegion())}) {
            if (!uses) { return true; }
            for (const auto & use : *uses) {
                if (!step()) { return false; }
                if (mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                        use.getUser(), use.getSymbolRef()) != target) {
                    continue;
                }
                auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getUser());
                if (!call || call.getTarget() != target || !undefined(call.getCalleeValue()) ||
                    !undefined(call.getNewTarget()) ||
                    call.getArgs().size() + ctjs::implicit_arguments != body.getNumArguments()) {
                    return true;
                }
                invocations.push_back(call.getArgs());
            }
        }
    }
    llvm::SmallVector<llvm::SmallVector<ctjs::StringAttr>> inputs(body.getNumArguments());
    llvm::SmallVector<bool> complete(body.getNumArguments(), true);
    for (mlir::ValueRange arguments : invocations) {
        for (auto [index, argument] : llvm::enumerate(arguments)) {
            if (!step()) { return false; }
            auto constant = argument.getDefiningOp<ctjs::ConstantOp>();
            auto string = constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue())
                                   : ctjs::StringAttr{};
            auto found = stringInputs.find(argument);
            const unsigned slot = static_cast<unsigned>(index) + ctjs::implicit_arguments;
            if (!string && found == stringInputs.end()) { complete[slot] = false; }
            if (!complete[slot]) { continue; }
            if (string) {
                inputs[slot].push_back(string);
            } else {
                for (ctjs::StringAttr value : found->second) {
                    if (!step()) { return false; }
                    inputs[slot].push_back(value);
                }
            }
        }
    }
    // Publish only after every use has an exact invocation proof. Unknown
    // inputs poison the whole formal, including earlier constant calls.
    mlir::OpBuilder at(&body, body.begin());
    for (auto [argument, values, known] : llvm::zip(body.getArguments(), inputs, complete)) {
        if (!step()) { return false; }
        if (!known || values.empty()) { continue; }
        bool common = true;
        for (ctjs::StringAttr value : values) {
            if (!step()) { return false; }
            common &= value == values.front();
        }
        if (common) {
            auto value = ctjs::ConstantOp::create(at, target.getLoc(), values.front());
            argument.replaceAllUsesWith(value.getResult());
            ++operationCount;
        } else {
            // ponytail: retain charged inputs, including duplicates; a set
            // becomes worthwhile if repeated calls exhaust the work budget.
            stringInputs[argument] = std::move(values);
        }
    }
    return true;
}

bool DOMSource::foldConstantReplacements(ctjs::FuncOp function) {
    auto & block = function.getBody().front();
    llvm::SmallVector<ctjs::CallOp> calls(block.getOps<ctjs::CallOp>());
    for (ctjs::CallOp call : calls) {
        if (!step()) { return false; }
        auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        auto text = call.getReceiver().getDefiningOp<ctjs::ConstantOp>();
        auto string = text ? llvm::dyn_cast<ctjs::StringAttr>(text.getValue()) : ctjs::StringAttr{};
        auto found = stringInputs.find(call.getReceiver());
        if (!read || read.getObject() != call.getReceiver() ||
            (!string && found == stringInputs.end()) ||
            ctjs::constantKey(read.getKey()) != "replace" || call.getArgs().size() != 2) {
            continue;
        }
        auto regexp = call.getArgs()[0].getDefiningOp<ctjs::CallOp>();
        auto callback = call.getArgs()[1].getDefiningOp<ctjs::CreateClosureOp>();
        if (!regexp || !callback || !callback.getUpvalues().empty() ||
            regexp.getArgs().size() != 2 || !undefined(regexp.getReceiver())) {
            continue;
        }
        auto factory = regexp.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
        const auto stringArgument = [](mlir::Value value) {
            auto constant = value.getDefiningOp<ctjs::ConstantOp>();
            return constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue())
                            : ctjs::StringAttr{};
        };
        auto expression = stringArgument(regexp.getArgs()[0]);
        auto flags = stringArgument(regexp.getArgs()[1]);
        if (!factory || factory.getName() != "__ctbrowser_regexp" || !expression || !flags) {
            continue;
        }
        auto pattern = expression.getValue();
        const auto alphanumeric = [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        };
        // ponytail: a literal ASCII range, no case folding or Unicode flags.
        // Broader patterns need a separately bounded RegExp evaluator.
        if (pattern.size() != 5 || pattern[0] != '[' || pattern[2] != '-' || pattern[4] != ']' ||
            !alphanumeric(pattern[1]) || !alphanumeric(pattern[3]) || pattern[1] > pattern[3] ||
            (!flags.getValue().empty() && flags.getValue() != "g")) {
            continue;
        }
        bool matches = false;
        const auto values = string ? llvm::ArrayRef<ctjs::StringAttr>(string)
                                   : llvm::ArrayRef<ctjs::StringAttr>(found->second);
        for (ctjs::StringAttr value : values) {
            if (!step()) { return false; }
            for (unsigned char c : value.getValue().bytes()) {
                if (!step()) { return false; }
                matches |= c >= static_cast<unsigned char>(pattern[1]) &&
                           c <= static_cast<unsigned char>(pattern[3]);
            }
        }
        auto target = functions.lookup(static_cast<unsigned>(callback.getFunction()));
        if (!target || target == function || target.getUpvalueCount() != 0 ||
            creations.lookup(static_cast<unsigned>(callback.getFunction())) != 1) {
            continue;
        }
        llvm::SmallVector<ctjs::RootOp> roots;
        const auto exclusive = [&](mlir::Value value, mlir::Operation * consumer,
                                   unsigned operand) {
            for (mlir::OpOperand & use : value.getUses()) {
                if (!step()) { return false; }
                if (auto root = llvm::dyn_cast<ctjs::RootOp>(use.getOwner())) {
                    roots.push_back(root);
                } else if (use.getOwner() != consumer || use.getOperandNumber() != operand) {
                    return false;
                }
            }
            return true;
        };
        if (!exclusive(read.getResult(), call, 0) || !exclusive(regexp.getResult(), call, 2) ||
            !exclusive(callback.getResult(), call, 3) ||
            !exclusive(factory.getResult(), regexp, 0)) {
            continue;
        }
        if (!checkBody(target, false)) { return false; }
        if (!target.getBody().front().getOps<ctjs::CreateClosureOp>().empty()) {
            return refuse("DOM replacement callback contains an unproved callable");
        }
        if (matches && (pattern != "[A-Z]" || flags.getValue() != "g" ||
                        !host_detail::isLowercaseReplacement(target, [&] { return step(); }))) {
            if (!reason.empty()) { return false; }
            continue;
        }
        // The isolated provider fixes the complete initial String/RegExp
        // prototype chains (including @@replace, exec and flag accessors)
        // and the reserved literal factory binding.
        // The residual source must still pass complete DOM reproof, which
        // forbids mutation and script reentry. Fresh literal state cannot
        // escape. Matching inputs require the complete callback proof above;
        // the original all-call census supplies every possible input here.
        mlir::Value result = call.getReceiver();
        if (matches) {
            mlir::OpBuilder at(call);
            bool first = true;
            for (ctjs::StringAttr value : values) {
                if (!step()) { return false; }
                std::string output;
                for (char c : value.getValue()) {
                    if (!step()) { return false; }
                    if (c >= 'A' && c <= 'Z') {
                        output += '-';
                        output += ctbrowser::ascii_lower(c);
                    } else {
                        output += c;
                    }
                }
                auto normalized = ctjs::ConstantOp::create(
                    at, call.getLoc(), ctjs::StringAttr::get(call.getContext(), output));
                ++operationCount;
                if (first) {
                    result = normalized.getResult();
                    first = false;
                    continue;
                }
                for (unsigned operation = 0; operation < 6; ++operation) {
                    if (!step()) { return false; }
                }
                auto input = ctjs::ConstantOp::create(at, call.getLoc(), value);
                auto equal = ctjs::CompareOp::create(at, call.getLoc(), call.getType(),
                                                     ctjs::CompareKind::StrictEq,
                                                     call.getReceiver(), input.getResult());
                auto condition =
                    ctjs::TruthyOp::create(at, call.getLoc(), at.getI1Type(), equal.getResult());
                auto branch = mlir::scf::IfOp::create(at, call.getLoc(), call->getResultTypes(),
                                                      condition.getResult());
                for (auto [region, selected] :
                     llvm::zip(branch->getRegions(),
                               llvm::ArrayRef<mlir::Value>{normalized.getResult(), result})) {
                    auto & arm = region.emplaceBlock();
                    mlir::OpBuilder nested(&arm, arm.begin());
                    mlir::scf::YieldOp::create(nested, call.getLoc(), selected);
                }
                operationCount += 6;
                result = branch.getResult(0);
            }
        }
        call.getResult().replaceAllUsesWith(result);
        call.erase();
        for (ctjs::RootOp root : roots) { root.erase(); }
        read.erase();
        regexp.erase();
        factory.erase();
        callback.erase();
        functions.erase(*functionIndex(target));
        target.erase();
    }
    return true;
}

bool DOMSource::captureTarget(ctjs::CreateClosureOp closure, ctjs::FuncOp target) {
    if (!chargeCaptureQuery()) { return false; }
    if (!target || !expanded.contains(target) ||
        closure.getUpvalues().size() != target.getUpvalueCount()) {
        return refuse("DOM helper capture target has not been completely expanded");
    }
    const auto indices = closure.getEnclosingIndicesAttr();
    const bool forwarded =
        indices && llvm::any_of(indices.asArrayRef(), [](int32_t index) { return index >= 0; });
    if (!forwarded) {
        if (immutableClosureTarget(closure, closure->getParentOfType<mlir::ModuleOp>()) != target) {
            return refuse("DOM helper requires an immutable local leaf capture");
        }
        return true;
    }
    // The shared leaf query intentionally excludes forwarding. Here every
    // child has already been expanded, and the original creator census plus
    // the enclosing slot prove which invocation supplies each remaining load.
    auto parent = closure->getParentOfType<ctjs::FuncOp>();
    if (creations.lookup(static_cast<unsigned>(closure.getFunction())) != 1 ||
        indices.size() != closure.getUpvalues().size()) {
        return refuse("DOM helper forwarded capture identity is ambiguous");
    }
    for (auto [index, value] : llvm::zip(indices.asArrayRef(), closure.getUpvalues())) {
        if (!step()) { return false; }
        if (index < -1 ||
            (index >= 0 &&
             (static_cast<unsigned>(index) >= parent.getUpvalueCount() || !undefined(value)))) {
            return refuse("DOM helper forwarded capture lacks an exact enclosing slot");
        }
    }
    const auto checked = target.walk([&](mlir::Operation * operation) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (llvm::isa<ctjs::StoreUpvalueOp, ctjs::CreateClosureOp>(operation)) {
            refuse("DOM helper forwarded capture is mutable or unexpanded");
            return mlir::WalkResult::interrupt();
        }
        return mlir::WalkResult::advance();
    });
    return !checked.wasInterrupted();
}

bool DOMSource::captureStorage(mlir::OpOperand & use) {
    if (auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(use.getOwner())) {
        auto found = cells.find(cell.getResult());
        return use.getOperandNumber() == 0 && found != cells.end() &&
               found->second.value() == use.get();
    }
    if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner())) {
        auto found = cells.find(write.getCell());
        return use.getOperandNumber() == 1 && found != cells.end() && found->second.write == write;
    }
    return false;
}

bool DOMSource::resolveCell(ctjs::CreateCellOp cell) {
    unsigned writes = 0;
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        if (!step()) { return false; }
        auto * operation = use.getOwner();
        if (llvm::isa<ctjs::RootOp, ctjs::CellGetOp>(operation)) { continue; }
        if (llvm::isa<ctjs::CellSetOp>(operation) && use.getOperandNumber() == 0) {
            if (++writes <= 1) { continue; }
        } else if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation);
                   closure && use.getOperandNumber() >= 2) {
            const auto indices = closure.getEnclosingIndicesAttr();
            if ((!indices || indices[use.getOperandNumber() - 2] == -1) &&
                captureTarget(closure,
                              functions.lookup(static_cast<unsigned>(closure.getFunction())))) {
                continue;
            }
        }
        return refuse("DOM helper capture cell is mutable or escapes");
    }
    auto write = captureCellWrite(cell);
    if (write && (write->getBlock() != cell->getBlock() || !cell->isBeforeInBlock(write))) {
        return refuse("DOM helper capture cell has nonlocal or unordered uses");
    }
    llvm::SmallVector<ctjs::CellGetOp> reads;
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        if (!step()) { return false; }
        auto * operation = use.getOwner();
        auto * position = operation;
        if (llvm::isa<ctjs::CellGetOp, ctjs::RootOp>(operation)) {
            // Immutable snapshots may be read in a selected branch. The
            // initialization must precede the entire enclosing branch;
            // writes and callable scheduling still stay in the source block.
            while (position->getBlock() != cell->getBlock() &&
                   llvm::isa_and_nonnull<mlir::scf::IfOp, mlir::scf::WhileOp>(
                       position->getParentOp())) {
                if (!step()) { return false; }
                position = position->getParentOp();
            }
        }
        if (position->getBlock() != cell->getBlock() || !cell->isBeforeInBlock(position)) {
            return refuse("DOM helper capture cell has nonlocal or unordered uses");
        }
        if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(operation)) {
            if (write && !write->isBeforeInBlock(position)) {
                return refuse("DOM helper capture assignment does not precede its read");
            }
            reads.push_back(read);
        }
    }
    const auto value = write ? write.getValue() : cell.getInitial();
    cells[cell.getResult()] = {cell, write};
    for (ctjs::CellGetOp read : reads) {
        read.getResult().replaceAllUsesWith(value);
        read.erase();
    }
    return true;
}

} // namespace ctcompile::ctnative::dom_source_detail
