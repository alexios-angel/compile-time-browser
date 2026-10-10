//===- Captures.cpp - DOM helper captures and budget ------------*- C++ -*-===//
//
// DOMSource's work-budget accounting (step, drain, refuse), specialisation of
// private helpers on constant string arguments, folding of
// `str.replace(/[A-Z]/g, ...)` on known strings, and the proofs that a
// captured cell is written at most once, before any read.
//
//===----------------------------------------------------------------------===//

#include "Proof.hpp"

namespace ctcompile::ctnative::dom_source_detail {
namespace {
/// Whether `op`'s enclosing operation orders it: an scf.if/while or ctjs.try
/// arm, an invocation's normal or unwind continuation, or - with
/// `protectedCall` - a call in the invocation's protected body.
bool orderedByParent(mlir::Operation * op, bool protectedCall) {
    auto * parent = op->getParentOp();
    if (llvm::isa_and_nonnull<mlir::scf::IfOp, mlir::scf::WhileOp, ctjs::TryOp>(parent)) {
        return true;
    }
    auto invocation = llvm::dyn_cast_or_null<ctjs::InvokeOp>(parent);
    auto * region = op->getParentRegion();
    return invocation &&
           (region == &invocation.getNormalBody() || region == &invocation.getUnwindBody() ||
            (protectedCall && region == &invocation.getBody() &&
             llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(op)));
}
} // namespace

bool DOMSource::refuse(llvm::StringRef message) {
    if (reason.empty()) { reason = message.str(); }
    return false;
}

bool DOMSource::step(uint64_t cost) {
    if (cost > remaining) { return refuse("DOM helper expansion work budget exhausted"); }
    remaining -= static_cast<unsigned>(cost);
    return true;
}

bool DOMSource::drain(uint64_t cost) {
    if (cost > remaining) {
        remaining = 0;
        return refuse("DOM helper expansion work budget exhausted");
    }
    remaining -= static_cast<unsigned>(cost);
    return true;
}

bool DOMSource::eraseRootedUses(mlir::Value value, llvm::StringRef reason) {
    while (!value.use_empty()) {
        if (!step()) { return false; }
        auto root = llvm::dyn_cast<ctjs::RootOp>(*value.getUsers().begin());
        if (!root) { return refuse(reason); }
        root.erase();
    }
    return true;
}

bool DOMSource::chargeCaptureQuery() {
    // Charge six full scans of the module: the cost model for
    // immutableClosureTarget, which walks the module and the target's uses.
    for (unsigned scan = 0; scan < 6; ++scan) {
        if (!step(operationCount)) { return false; }
    }
    return true;
}

bool DOMSource::precedesInStructuredBody(mlir::Operation * definition, mlir::Operation * use) {
    // Climb from `use` to the definition's block through ordered parents
    // (structured arms and invocation outcomes), so whichever arm runs, it runs
    // after the definition. Only ordering is proved here; callers check
    // captures, lifetimes and effects. A capture assignment must therefore
    // precede the whole enclosing operation.
    while (use->getBlock() != definition->getBlock()) {
        if (!step() || !orderedByParent(use, /*protectedCall=*/true)) { return false; }
        use = use->getParentOp();
    }
    return use->getBlock() == definition->getBlock() && definition->isBeforeInBlock(use);
}

bool DOMSource::confinedCallback(ctjs::CreateClosureOp closure, ctjs::FuncOp target) {
    if (!target || !closure.getUpvalues().empty() || !uniqueCreation(closure)) { return false; }
    for (const bool replacement : {false, true}) {
        auto & body = target.getBody();
        if (replacement
                ? !host_detail::isLowercaseReplacement(target, [&] { return step(); })
                : target.getUpvalueCount() != 0 ||
                      body.front().getNumArguments() != ctjs::implicit_arguments + 1 ||
                      llvm::any_of(
                          body.front().getArguments().take_front(ctjs::implicit_arguments),
                          [](mlir::BlockArgument argument) { return !argument.use_empty(); })) {
            continue;
        }
        bool invoked = false, confined = true;
        for (mlir::OpOperand & use : closure.getResult().getUses()) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
            auto read = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                             : ctjs::GetPropertyOp{};
            if (!call || use.getOperandNumber() != (replacement ? 3u : 2u) ||
                call.getArgs().size() != (replacement ? 2u : 1u) || !read ||
                read.getObject() != call.getReceiver() ||
                ctjs::constantKey(read.getKey()) != (replacement ? "replace" : "filter") ||
                (replacement ? !precedesInStructuredBody(closure, call)
                             : call->getBlock() != closure->getBlock() ||
                                   !closure->isBeforeInBlock(call))) {
                confined = false;
                break;
            }
            invoked = true;
        }
        if (confined && invoked) { return true; }
    }
    return false;
}

bool DOMSource::bindConstantArguments(ctjs::CreateClosureOp closure, ctjs::FuncOp target) {
    if (target.getUpvalueCount() != 0 ||
        (closure && (!closure.getUpvalues().empty() || !uniqueCreation(closure)))) {
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
        // A private target called directly by symbol has no closure. Collect
        // every symbol use before specializing, not only the first caller.
        auto module = target->getParentOfType<mlir::ModuleOp>();
        if (!target.isPrivate() || creations.lookup(*functionIndex(target)) != 0) { return true; }
        if (!step(2 * uint64_t(operationCount))) { return false; }
        if (auto stop = host_detail::forEachSymbolUse(
                module, true, [&] { return step(); },
                [&](const mlir::SymbolTable::SymbolUse & use,
                    ctjs::FuncOp callee) -> std::optional<bool> {
                    if (callee != target) { return std::nullopt; }
                    auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getUser());
                    if (!call || call.getTarget() != target || !undefined(call.getCalleeValue()) ||
                        !undefined(call.getNewTarget()) ||
                        call.getArgs().size() + ctjs::implicit_arguments !=
                            body.getNumArguments()) {
                        return true;
                    }
                    invocations.push_back(call.getArgs());
                    return std::nullopt;
                })) {
            return *stop;
        }
    }
    llvm::SmallVector<llvm::SmallVector<ctjs::StringAttr>> inputs(body.getNumArguments());
    llvm::SmallVector<bool> complete(body.getNumArguments(), true);
    for (mlir::ValueRange arguments : invocations) {
        for (auto [index, argument] : llvm::enumerate(arguments)) {
            if (!step()) { return false; }
            auto string = ctjs::constantAttr<ctjs::StringAttr>(argument);
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
    // Rewrite only now that every use is a known invocation. One unknown
    // input disqualifies the whole parameter, even if other calls passed
    // constants.
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
            // Remember the possible strings for foldConstantReplacements.
            // ponytail: duplicates are kept; dedupe with a set if repeated
            // calls start exhausting the work budget.
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
        auto string = ctjs::constantAttr<ctjs::StringAttr>(call.getReceiver());
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
        auto expression = ctjs::constantAttr<ctjs::StringAttr>(regexp.getArgs()[0]);
        auto flags = ctjs::constantAttr<ctjs::StringAttr>(regexp.getArgs()[1]);
        if (!factory || factory.getName() != "__ctbrowser_regexp" || !expression || !flags) {
            continue;
        }
        auto pattern = expression.getValue();
        const auto alphanumeric = [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        };
        // ponytail: accepts only a five-character `[X-Y]` ASCII range with no
        // flags or `g`; broader patterns need a bounded RegExp evaluator.
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
        auto target = closureTarget(callback);
        if (!target || target == function || target.getUpvalueCount() != 0 ||
            !uniqueCreation(callback)) {
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
        // Folding is safe because the isolated DOM provider guarantees the
        // built-in String/RegExp prototypes (@@replace, exec, flag accessors)
        // and the __ctbrowser_regexp literal factory are unmodified, and the
        // fresh RegExp cannot escape. The rewritten body is checked again by
        // the DOM entry proof, which forbids mutation and reentry. When the
        // pattern matches, the callback has been proved to be the lowercase
        // replacement above, and bindConstantArguments collected every string
        // the receiver can be.
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
                // Charge for the six operations each further alternative adds,
                // matching `operationCount += 6` below.
                for (unsigned operation = 0; operation < 6; ++operation) {
                    if (!step()) { return false; }
                }
                auto input = ctjs::ConstantOp::create(at, call.getLoc(), value);
                auto equal = ctjs::CompareOp::create(at, call.getLoc(), call.getType(),
                                                     ctjs::CompareKind::StrictEq,
                                                     call.getReceiver(), input.getResult());
                auto condition =
                    ctjs::TruthyOp::create(at, call.getLoc(), at.getI1Type(), equal.getResult());
                result = emitSelect(at, call.getLoc(), call->getResultTypes(),
                                    condition.getResult(), normalized.getResult(), result);
                operationCount += 6;
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
    // immutableClosureTarget does not handle forwarded captures. Here every
    // child has already been expanded, and the unique creation (counted in
    // `creations`) plus the enclosing slot index prove which invocation
    // supplies each remaining upvalue load.
    auto parent = closure->getParentOfType<ctjs::FuncOp>();
    if (!uniqueCreation(closure) || indices.size() != closure.getUpvalues().size()) {
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
            return halt("DOM helper forwarded capture is mutable or unexpanded");
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
                captureTarget(closure, closureTarget(closure))) {
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
            // A read may sit in either outcome of a protected call, so climb
            // to the operation in the cell's block that encloses it; the
            // initializing write must precede that whole operation. Writes
            // and captures are not lifted this way.
            while (position->getBlock() != cell->getBlock() &&
                   orderedByParent(position, /*protectedCall=*/false)) {
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
