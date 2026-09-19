#include "Analysis.h"
#include "ctbrowser/core/algorithms.hpp"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/ImmutableCaptures.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"

#include <optional>

namespace ctcompile::ctnative {

bool host_detail::isLowercaseReplacement(ctjs::FuncOp function, llvm::function_ref<bool()> step) {
    if (!function.getBody().hasOneBlock() || function.getUpvalueCount() != 0) { return false; }
    auto & body = function.getBody().front();
    if (body.empty() || body.getNumArguments() != ctjs::implicit_arguments + 1) { return false; }
    for (auto argument : body.getArguments().take_front(ctjs::implicit_arguments)) {
        if (!step() || !argument.use_empty()) { return false; }
    }
    auto returned = llvm::dyn_cast<ctjs::ReturnOp>(body.back());
    auto concat = returned ? returned.getValue().getDefiningOp<ctjs::BinaryOp>() : ctjs::BinaryOp{};
    if (!concat || (concat.getKind() != ctjs::BinaryKind::Concat &&
                    concat.getKind() != ctjs::BinaryKind::Add)) {
        return false;
    }
    auto prefix = concat.getLhs().getDefiningOp<ctjs::ConstantOp>();
    auto text = prefix ? llvm::dyn_cast<ctjs::StringAttr>(prefix.getValue()) : ctjs::StringAttr{};
    auto call = concat.getRhs().getDefiningOp<ctjs::CallOp>();
    auto read =
        call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>() : ctjs::GetPropertyOp{};
    if (!text || text.getValue() != "-" || !call || !read || !call.getArgs().empty() ||
        call.getReceiver() != body.getArgument(ctjs::implicit_arguments) ||
        read.getObject() != call.getReceiver() ||
        ctjs::constantKey(read.getKey()) != "toLowerCase") {
        return false;
    }
    // Prove the entire original callback, including discarded operations.
    // Its input is one matched ASCII uppercase unit; no coercion or script
    // reentry is needed for the initial lowercase method or concatenation.
    for (mlir::Operation & operation : body) {
        if (!step()) { return false; }
        if (&operation == read || &operation == call || &operation == concat ||
            llvm::isa<ctjs::ConstantOp, ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp,
                      ctjs::ReturnOp>(operation)) {
            continue;
        }
        return false;
    }
    return true;
}

namespace {

// This only normalizes a private candidate. The complete DOM analysis must
// still prove every expanded operation before the candidate can be published.
struct DOMSource {
    explicit DOMSource(unsigned maxSteps) : remaining(maxSteps) {}

    unsigned remaining;
    unsigned operationCount = 0;
    std::string reason;
    llvm::DenseMap<unsigned, ctjs::FuncOp> functions;
    llvm::DenseMap<unsigned, unsigned> creations;
    llvm::DenseSet<mlir::Operation *> active, expanded, retainedCallbacks;
    struct Capture {
        ctjs::CreateCellOp cell;
        ctjs::CellSetOp write;
        int32_t enclosingIndex = -1;
        mlir::Value value() { return write ? write.getValue() : cell.getInitial(); }
    };
    llvm::DenseMap<mlir::Value, Capture> cells;
    llvm::DenseMap<mlir::Operation *, unsigned> callDepth;
    llvm::DenseMap<mlir::Value, llvm::SmallVector<ctjs::StringAttr>> stringInputs;

    bool refuse(llvm::StringRef message) {
        if (reason.empty()) { reason = message.str(); }
        return false;
    }
    bool step() {
        if (!remaining) { return refuse("DOM helper expansion work budget exhausted"); }
        --remaining;
        return true;
    }
    bool chargeCaptureQuery() {
        // The shared immutable-capture query scans the module and target uses.
        for (unsigned scan = 0; scan < 6; ++scan) {
            if (remaining < operationCount) {
                return refuse("DOM helper expansion work budget exhausted");
            }
            remaining -= operationCount;
        }
        return true;
    }
    static bool undefined(mlir::Value value) {
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        return constant && llvm::isa<ctjs::UndefinedAttr>(constant.getValue());
    }

    bool precedesInStructuredBody(mlir::Operation * definition, mlir::Operation * use) {
        // Crossing only these regions proves the definition runs before the
        // entire selected arm or loop. Invoke continuations require their own
        // exception proof and cannot inherit this source-order shortcut.
        while (use->getBlock() != definition->getBlock() &&
               llvm::isa_and_nonnull<mlir::scf::IfOp, mlir::scf::WhileOp>(use->getParentOp())) {
            if (!step()) { return false; }
            use = use->getParentOp();
        }
        return use->getBlock() == definition->getBlock() && definition->isBeforeInBlock(use);
    }

    bool confinedFilterCallback(ctjs::CreateClosureOp closure, ctjs::FuncOp target) {
        if (!target || !closure.getUpvalues().empty() || target.getUpvalueCount() != 0 ||
            creations.lookup(static_cast<unsigned>(closure.getFunction())) != 1 ||
            target.getBody().front().getNumArguments() != ctjs::implicit_arguments + 1 ||
            llvm::any_of(
                target.getBody().front().getArguments().take_front(ctjs::implicit_arguments),
                [](mlir::BlockArgument argument) { return !argument.use_empty(); })) {
            return false;
        }
        bool invoked = false;
        for (mlir::OpOperand & use : closure.getResult().getUses()) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
            auto read = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                             : ctjs::GetPropertyOp{};
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

    bool confinedReplacementCallback(ctjs::CreateClosureOp closure, ctjs::FuncOp target) {
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
            auto read = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                             : ctjs::GetPropertyOp{};
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

    bool bindConstantArguments(ctjs::CreateClosureOp closure, ctjs::FuncOp target) {
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
            if (!target.isPrivate() || creations.lookup(*functionIndex(target)) != 0) {
                return true;
            }
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
                        call.getArgs().size() + ctjs::implicit_arguments !=
                            body.getNumArguments()) {
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

    bool foldConstantReplacements(ctjs::FuncOp function) {
        auto & block = function.getBody().front();
        llvm::SmallVector<ctjs::CallOp> calls(block.getOps<ctjs::CallOp>());
        for (ctjs::CallOp call : calls) {
            if (!step()) { return false; }
            auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            auto text = call.getReceiver().getDefiningOp<ctjs::ConstantOp>();
            auto string =
                text ? llvm::dyn_cast<ctjs::StringAttr>(text.getValue()) : ctjs::StringAttr{};
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
            if (pattern.size() != 5 || pattern[0] != '[' || pattern[2] != '-' ||
                pattern[4] != ']' || !alphanumeric(pattern[1]) || !alphanumeric(pattern[3]) ||
                pattern[1] > pattern[3] || (!flags.getValue().empty() && flags.getValue() != "g")) {
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
                    auto condition = ctjs::TruthyOp::create(at, call.getLoc(), at.getI1Type(),
                                                            equal.getResult());
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

    bool captureTarget(ctjs::CreateClosureOp closure, ctjs::FuncOp target) {
        if (!chargeCaptureQuery()) { return false; }
        if (!target || !expanded.contains(target) ||
            closure.getUpvalues().size() != target.getUpvalueCount()) {
            return refuse("DOM helper capture target has not been completely expanded");
        }
        const auto indices = closure.getEnclosingIndicesAttr();
        const bool forwarded =
            indices && llvm::any_of(indices.asArrayRef(), [](int32_t index) { return index >= 0; });
        if (!forwarded) {
            if (immutableClosureTarget(closure, closure->getParentOfType<mlir::ModuleOp>()) !=
                target) {
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

    bool captureStorage(mlir::OpOperand & use) {
        if (auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(use.getOwner())) {
            auto found = cells.find(cell.getResult());
            return use.getOperandNumber() == 0 && found != cells.end() &&
                   found->second.value() == use.get();
        }
        if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner())) {
            auto found = cells.find(write.getCell());
            return use.getOperandNumber() == 1 && found != cells.end() &&
                   found->second.write == write;
        }
        return false;
    }

    bool resolveCell(ctjs::CreateCellOp cell) {
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

    bool forwardFields(ctjs::FuncOp function) {
        auto & block = function.getBody().front();
        llvm::SmallVector<ctjs::CreateObjectOp> objects(block.getOps<ctjs::CreateObjectOp>());
        for (ctjs::CreateObjectOp object : objects) {
            llvm::DenseSet<mlir::Operation *> uses;
            bool confined = true, read = false;
            for (mlir::OpOperand & use : object.getResult().getUses()) {
                if (!step()) { return false; }
                auto * operation = use.getOwner();
                auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
                auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(operation);
                const bool ordered =
                    get ? precedesInStructuredBody(object, get)
                        : operation->getBlock() == &block && object->isBeforeInBlock(operation);
                if (!ordered ||
                    (!llvm::isa<ctjs::RootOp>(operation) &&
                     (use.getOperandNumber() != 0 ||
                      !(get ? ctjs::ordinaryKey(get.getKey())
                            : set && ctjs::ordinaryKey(set.getKey()) &&
                                  !set.getValue().getDefiningOp<ctjs::CreateClosureOp>())))) {
                    confined = false;
                    break;
                }
                read |= static_cast<bool>(get);
                uses.insert(operation);
            }
            if (!confined || !read) { continue; }
            // The isolated DOM provider fixes the initial prototype. Only
            // initialized own fields are read; the complete use census rules
            // out aliases, identity observations, accessors and prototype edits.
            // Keep every value producer for the subsequent complete DOM proof.
            // Writes stay in the source block; nested reads see the fields at
            // their enclosing branch/loop, which cannot mutate this object.
            // ponytail: one charged function scan per local object; index stores
            // if large straight-line entries exhaust the existing work budget.
            llvm::StringMap<mlir::Value> fields;
            const auto walked = function.walk([&](mlir::Operation * operation) {
                if (!step()) { return mlir::WalkResult::interrupt(); }
                if (!uses.contains(operation)) { return mlir::WalkResult::advance(); }
                if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
                    fields[ctjs::constantKey(set.getKey())] = set.getValue();
                } else if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                    auto value = fields.lookup(ctjs::constantKey(get.getKey()));
                    if (!value) {
                        refuse("DOM local field read lacks a preceding write");
                        return mlir::WalkResult::interrupt();
                    }
                    get.getResult().replaceAllUsesWith(value);
                }
                operation->erase();
                return mlir::WalkResult::advance();
            });
            if (walked.wasInterrupted()) { return false; }
            object.erase();
        }
        return true;
    }

    bool dataObject(ctjs::CreateObjectOp object) {
        // Preserve fresh data allocations for the complete DOM proof. Unlike
        // callable holders, their constructors and uses must not be erased here.
        for (mlir::OpOperand & operand : object.getResult().getUses()) {
            if (!step()) { return false; }
            auto * use = operand.getOwner();
            if (llvm::isa<ctjs::RootOp, ctjs::ReturnOp, mlir::scf::YieldOp, ctjs::CopyPropsOp>(
                    use)) {
                continue;
            }
            if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(use);
                store && operand.getOperandNumber() == 0 &&
                !store.getValue().getDefiningOp<ctjs::CreateClosureOp>() &&
                precedesInStructuredBody(object, store)) {
                // Keep assignment order intact. Key/value semantics still need
                // complete DOM proof; callable slots retain their separate proof.
                continue;
            }
            if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(use);
                unary && unary.getKind() == ctjs::UnaryKind::TypeOf) {
                continue;
            }
            return false;
        }
        return true;
    }

    bool resolveMethods(ctjs::CreateObjectOp object, llvm::DenseSet<mlir::Operation *> & methods) {
        auto proof = analyzeLocalCallableObject(object, [&] { return step(); });
        if (!proof) { return refuse(llvm::toString(proof.takeError())); }
        methods.insert(proof->calls.begin(), proof->calls.end());
        for (auto [read, closure] : proof->reads) {
            read.getResult().replaceAllUsesWith(closure.getResult());
            read.erase();
        }
        for (ctjs::SetPropertyOp store : proof->stores) {
            if (!step()) { return false; }
            store.erase();
        }
        return true;
    }

    bool flattenEntryFactory(ctjs::FuncOp wrapper, ctjs::FuncOp target) {
        ctjs::StoreGlobalOp publication;
        for (auto store : wrapper.getBody().front().getOps<ctjs::StoreGlobalOp>()) {
            if (!step()) { return false; }
            if (store.getName() != target.getSymName().rsplit('$').first) { continue; }
            if (publication) { return refuse("DOM entry factory has repeated publication"); }
            publication = store;
        }
        auto * call = publication ? publication.getValue().getDefiningOp() : nullptr;
        auto selection = llvm::dyn_cast_or_null<ctjs::GetPropertyOp>(call);
        if (selection) { call = selection.getObject().getDefiningOp(); }
        auto indirect = llvm::dyn_cast_or_null<ctjs::CallOp>(call);
        auto direct = llvm::dyn_cast_or_null<ctjs::CallDirectOp>(call);
        if (!indirect && !direct) { return true; }
        auto callee = indirect ? indirect.getCallee() : direct.getCalleeValue();
        auto receiver = indirect ? indirect.getReceiver() : direct.getReceiver();
        auto arguments = indirect ? indirect.getArgs() : direct.getArgs();
        auto closure = callee.getDefiningOp<ctjs::CreateClosureOp>();
        auto factory = closure && closure.getFunction() >= 0
                           ? functions.lookup(static_cast<unsigned>(closure.getFunction()))
                           : ctjs::FuncOp{};
        if (!factory || factory == target || factory == wrapper || !closure.getUpvalues().empty() ||
            factory.getUpvalueCount() != 0 ||
            creations.lookup(static_cast<unsigned>(closure.getFunction())) != 1 ||
            !undefined(receiver) ||
            (direct &&
             (direct.getCallee() != factory.getSymName() || !undefined(direct.getNewTarget()))) ||
            arguments.size() + ctjs::implicit_arguments !=
                factory.getBody().front().getNumArguments()) {
            return refuse("DOM entry factory requires one exact uncaptured local call");
        }
        if (!checkBody(wrapper, false) || !checkBody(factory, false)) { return false; }
        if (closure->getBlock() != &wrapper.getBody().front() ||
            call->getBlock() != closure->getBlock() || !closure->isBeforeInBlock(call)) {
            return refuse("DOM entry factory lacks local source order");
        }
        llvm::SmallVector<ctjs::RootOp> roots;
        for (mlir::OpOperand & use : closure.getResult().getUses()) {
            if (!step()) { return false; }
            if (auto root = llvm::dyn_cast<ctjs::RootOp>(use.getOwner())) {
                roots.push_back(root);
            } else if (use.getOwner() != call || use.getOperandNumber() != (indirect ? 0U : 2U)) {
                return refuse("DOM entry factory identity escapes or is called repeatedly");
            }
        }
        for (mlir::Operation * use : call->getResult(0).getUsers()) {
            if (!step()) { return false; }
            if (use != (selection ? selection.getOperation() : publication.getOperation()) &&
                !llvm::isa<ctjs::RootOp>(use)) {
                return refuse("DOM entry factory result escapes its unique publication");
            }
        }
        if (selection) {
            for (mlir::Operation * use : selection.getResult().getUsers()) {
                if (!step()) { return false; }
                if (use != publication && !llvm::isa<ctjs::RootOp>(use)) {
                    return refuse("DOM entry factory selection escapes its unique publication");
                }
            }
        }
        auto & body = factory.getBody().front();
        auto result = llvm::cast<ctjs::ReturnOp>(body.back());
        auto entry = result.getValue().getDefiningOp<ctjs::CreateClosureOp>();
        auto table = result.getValue().getDefiningOp<ctjs::CreateObjectOp>();
        if (selection ? !table
                      : !entry || entry.getFunction() < 0 ||
                            static_cast<unsigned>(entry.getFunction()) != *functionIndex(target)) {
            return refuse("DOM entry factory must return its exact source entry");
        }
        // The single invocation moves its local cells with the returned closure.
        // Only unobserved creator operands change; full capture/source proof runs
        // on the flattened candidate before any native code can be published.
        mlir::IRMapping mapping;
        mapping.map(body.getArgument(ctjs::arg_receiver), receiver);
        mapping.map(body.getArgument(ctjs::arg_new_target), receiver);
        mapping.map(body.getArgument(ctjs::arg_callee),
                    wrapper.getBody().front().getArgument(ctjs::arg_callee));
        for (auto [formal, actual] :
             llvm::zip(body.getArguments().drop_front(ctjs::implicit_arguments), arguments)) {
            if (!step()) { return false; }
            mapping.map(formal, actual);
        }
        mlir::OpBuilder at(call);
        for (mlir::Operation & operation : body) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp, ctjs::ReturnOp>(
                    operation)) {
                continue;
            }
            at.clone(operation, mapping);
            ++operationCount;
        }
        call->getResult(0).replaceAllUsesWith(mapping.lookup(result.getValue()));
        call->erase();
        if (selection) {
            auto object = mapping.lookup(table.getResult()).getDefiningOp<ctjs::CreateObjectOp>();
            llvm::DenseSet<mlir::Operation *> methods;
            if (!resolveMethods(object, methods)) { return false; }
            // Selection removes only checked own slots. Every source callable,
            // including unselected slots, still needs its invocation proof.
            while (!object.getResult().use_empty()) {
                if (!step()) { return false; }
                auto root = llvm::dyn_cast<ctjs::RootOp>(*object.getResult().getUsers().begin());
                if (!root) { return refuse("DOM entry factory table has an unexpanded use"); }
                root.erase();
            }
            object.erase();
        }
        for (ctjs::RootOp root : roots) { root.erase(); }
        closure.erase();
        functions.erase(*functionIndex(factory));
        factory.erase();
        return true;
    }

    bool initializeEntry(ctjs::FuncOp wrapper, ctjs::FuncOp target) {
        if (!flattenEntryFactory(wrapper, target)) { return false; }
        auto & block = wrapper.getBody().front();
        const auto index = functionIndex(target);
        if (!index || *index == 0 || creations.lookup(*index) != 1 ||
            block.getNumArguments() != ctjs::implicit_arguments) {
            return refuse("DOM entry initialization lacks a unique source declaration");
        }
        for (mlir::BlockArgument argument : block.getArguments()) {
            for (mlir::OpOperand & use : argument.getUses()) {
                if (!step()) { return false; }
                if (!llvm::isa<ctjs::RootOp, ctjs::CreateClosureOp>(use.getOwner())) {
                    return refuse("DOM entry initialization observes an implicit argument");
                }
            }
        }
        ctjs::StoreGlobalOp publication;
        ctjs::CreateClosureOp closure;
        ctjs::ReturnOp result;
        for (mlir::Operation & operation : block) {
            if (!step()) { return false; }
            if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation)) {
                auto made = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
                if (publication || !made || made->getBlock() != &block ||
                    !made->isBeforeInBlock(store) || made.getFunction() < 0 ||
                    static_cast<unsigned>(made.getFunction()) != *index ||
                    store.getName() == "undefined" ||
                    store.getName() != target.getSymName().rsplit('$').first) {
                    return refuse("DOM entry initialization has an unknown or repeated export");
                }
                publication = store;
                closure = made;
            } else if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                if (result || !undefined(returned.getValue())) {
                    return refuse("DOM entry initialization has an observable return");
                }
                result = returned;
            } else if (!llvm::isa<ctjs::ConstantOp, ctjs::CreateCellOp, ctjs::CellGetOp,
                                  ctjs::CellSetOp, ctjs::CreateClosureOp, ctjs::CreateObjectOp,
                                  ctjs::GetPropertyOp, ctjs::SetPropertyOp, ctjs::FrameEnterOp,
                                  ctjs::FrameExitOp, ctjs::RootOp>(operation)) {
                return refuse("DOM entry initialization contains an observable source operation");
            }
        }
        if (!publication || !result) {
            return refuse("DOM entry initialization lacks a complete source export");
        }
        // Host calls follow the whole initialization, including assignments after
        // publication. Replay is safe only if expansion eliminates every private
        // cell/holder/callable and the complete DOM proof accepts the result.
        for (mlir::BlockArgument argument :
             target.getBody().front().getArguments().drop_front(ctjs::implicit_arguments)) {
            if (!step()) { return false; }
            block.addArgument(argument.getType(), argument.getLoc());
        }
        wrapper.setFunctionTypeAttr(target.getFunctionTypeAttr());
        wrapper->removeAttr("arg_attrs");
        mlir::OpBuilder at(result);
        auto call = ctjs::CallOp::create(at, result.getLoc(), result.getValue().getType(),
                                         closure.getResult(), result.getValue(),
                                         block.getArguments().drop_front(ctjs::implicit_arguments));
        ++operationCount;
        result.getValueMutable().assign(call.getResult());
        publication.erase();
        return true;
    }

    bool normalizeCompletion(ctjs::FuncOp function) {
        bool dispatch = false;
        const auto walked = function.walk([&](mlir::Operation * operation) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            dispatch |= llvm::isa<mlir::scf::IndexSwitchOp, mlir::ub::PoisonOp>(operation);
            return mlir::WalkResult::advance();
        });
        if (walked.wasInterrupted()) { return false; }
        if (!dispatch) { return true; }

        // Inactive poison slots need completion proof even without a switch.
        // Use the same exact continuation proof before helper expansion.

        // Keep the original body until every completion path has been copied.
        // A yield supplies the exact values for its continuation; inactive
        // poison slots may be forwarded but must never be observed.
        struct Continuation {
            mlir::Block * block;
            mlir::Block::iterator next;
            mlir::ValueRange results;
            const Continuation * outer;
        };
        struct Terminal {
            enum Kind {
                returned,
                condition,
                yielded
            } kind = returned;
            llvm::SmallVector<mlir::Type> types;
            mlir::scf::WhileOp loop;
            llvm::SmallVector<unsigned> carried;
            llvm::SmallVector<bool> inactiveAfter;
        };
        using Results = std::optional<llvm::SmallVector<mlir::Value>>;
        mlir::Region normalized;
        auto & destination = normalized.emplaceBlock();
        mlir::IRMapping mapping;
        mlir::DominanceInfo dominance(function);
        llvm::DenseSet<mlir::Operation *> visited;
        for (mlir::BlockArgument argument : function.getBody().front().getArguments()) {
            if (!step()) { return false; }
            mapping.map(argument, destination.addArgument(argument.getType(), argument.getLoc()));
        }
        const auto emit = [&](auto && self, mlir::Block & body, mlir::Block::iterator begin,
                              mlir::IRMapping & values, mlir::OpBuilder & at,
                              const Continuation * continuation, Terminal & terminal,
                              unsigned depth) -> Results {
            if (depth >= 64) {
                refuse("DOM helper completion nesting is too deep");
                return {};
            }
            for (auto cursor = begin; cursor != body.end(); ++cursor) {
                if (!step()) { return {}; }
                auto & operation = *cursor;
                visited.insert(&operation);
                for (mlir::Value operand : operation.getOperands()) {
                    if (!step()) { return {}; }
                    if (!values.contains(operand) || !dominance.dominates(operand, &operation)) {
                        refuse("DOM helper completion operand has no preceding local definition");
                        return {};
                    }
                }
                if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(operation)) {
                    if (!continuation && terminal.kind == Terminal::yielded) {
                        if (yield.getOperandTypes() != terminal.loop.getInits().getTypes()) {
                            refuse("DOM helper loop lost its carried result correspondence");
                            return {};
                        }
                        llvm::SmallVector<mlir::Value> result;
                        for (unsigned index : terminal.carried) {
                            if (!step()) { return {}; }
                            auto value = values.lookup(yield.getOperand(index));
                            if (value.getDefiningOp<mlir::ub::PoisonOp>()) {
                                refuse("DOM helper completion observes an inactive value");
                                return {};
                            }
                            result.push_back(value);
                        }
                        return result;
                    }
                    if (!continuation ||
                        yield.getOperandTypes() != continuation->results.getTypes()) {
                        refuse("DOM helper completion lost its result correspondence");
                        return {};
                    }
                    for (auto [result, operand] :
                         llvm::zip(continuation->results, yield.getOperands())) {
                        if (!step()) { return {}; }
                        values.map(result, values.lookupOrDefault(operand));
                    }
                    return self(self, *continuation->block, continuation->next, values, at,
                                continuation->outer, terminal, depth);
                }
                if (auto poison = llvm::dyn_cast<mlir::ub::PoisonOp>(operation)) {
                    values.map(poison.getResult(), poison.getResult());
                    continue;
                }
                if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(operation)) {
                    if (continuation || terminal.kind != Terminal::condition ||
                        condition.getArgs().getTypes() != terminal.loop.getResultTypes()) {
                        refuse("DOM helper loop lost its condition result correspondence");
                        return {};
                    }
                    auto predicate = values.lookup(condition.getCondition());
                    auto constant = predicate.getDefiningOp<mlir::arith::ConstantOp>();
                    auto integer = constant ? llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue())
                                            : mlir::IntegerAttr{};
                    if (predicate.getDefiningOp<mlir::ub::PoisonOp>()) {
                        refuse("DOM helper completion observes an inactive value");
                        return {};
                    }
                    llvm::SmallVector<mlir::Value> result{predicate};
                    for (auto [index, argument] : llvm::enumerate(condition.getArgs())) {
                        if (!step()) { return {}; }
                        auto value = values.lookup(argument);
                        if (value.getDefiningOp<mlir::ub::PoisonOp>()) {
                            const bool inactive =
                                integer &&
                                (integer.getValue().isZero()
                                     ? terminal.loop.getResult(static_cast<unsigned>(index))
                                           .use_empty()
                                     : terminal.inactiveAfter[index]);
                            if (!inactive || index >= terminal.loop.getBeforeArguments().size()) {
                                refuse("DOM helper completion observes an inactive value");
                                return {};
                            }
                            auto state = terminal.loop.getBeforeArguments()[index];
                            if (!values.contains(state) || state.getType() != value.getType()) {
                                refuse("DOM helper completion observes an inactive value");
                                return {};
                            }
                            // The predicate selects a destination with no use of
                            // this slot. Its existing state keeps the SCF tuple
                            // defined and preserves the carried representation.
                            value = values.lookup(state);
                        }
                        result.push_back(value);
                    }
                    return result;
                }
                if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(operation)) {
                    if (!loop.getBefore().hasOneBlock() || !loop.getAfter().hasOneBlock() ||
                        loop.getBefore().front().getArgumentTypes() != loop.getInits().getTypes() ||
                        loop.getAfter().front().getArgumentTypes() != loop.getResultTypes()) {
                        refuse("DOM helper loop lacks complete source regions");
                        return {};
                    }
                    Terminal before;
                    before.kind = Terminal::condition;
                    before.loop = loop;
                    before.types.push_back(at.getI1Type());
                    before.types.append(loop.getResultTypes().begin(), loop.getResultTypes().end());
                    Terminal after;
                    after.kind = Terminal::yielded;
                    after.loop = loop;
                    llvm::SmallVector<mlir::Value> initial;
                    for (auto [index, argument] : llvm::enumerate(loop.getBeforeArguments())) {
                        if (!step()) { return {}; }
                        if (argument.use_empty()) { continue; }
                        auto value = values.lookup(loop.getInits()[index]);
                        if (value.getDefiningOp<mlir::ub::PoisonOp>()) {
                            refuse("DOM helper completion observes an inactive value");
                            return {};
                        }
                        after.carried.push_back(static_cast<unsigned>(index));
                        after.types.push_back(argument.getType());
                        initial.push_back(value);
                    }
                    for (mlir::BlockArgument argument : loop.getAfterArguments()) {
                        if (!step()) { return {}; }
                        bool inactive = true;
                        for (mlir::OpOperand & use : argument.getUses()) {
                            if (!step()) { return {}; }
                            auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(use.getOwner());
                            inactive &= yield && yield->getBlock() == &loop.getAfter().front() &&
                                        !llvm::is_contained(after.carried, use.getOperandNumber());
                        }
                        before.inactiveAfter.push_back(inactive);
                    }
                    auto copied = mlir::scf::WhileOp::create(at, loop.getLoc(),
                                                             loop.getResultTypes(), initial);
                    ++operationCount;
                    for (auto [source, target] :
                         llvm::zip(loop->getRegions(), copied->getRegions())) {
                        for (const auto & item : values.getValueMap()) {
                            (void)item;
                            if (!step()) { return {}; }
                        }
                        for (const auto & item : values.getOperationMap()) {
                            (void)item;
                            if (!step()) { return {}; }
                        }
                        mlir::IRMapping path(values);
                        auto & output = target.emplaceBlock();
                        const bool isBefore = &source == &loop.getBefore();
                        if (isBefore) {
                            for (unsigned index : after.carried) {
                                if (!step()) { return {}; }
                                auto argument = source.front().getArgument(index);
                                path.map(argument,
                                         output.addArgument(argument.getType(), argument.getLoc()));
                            }
                        } else {
                            for (mlir::BlockArgument argument : source.front().getArguments()) {
                                if (!step()) { return {}; }
                                path.map(argument,
                                         output.addArgument(argument.getType(), argument.getLoc()));
                            }
                        }
                        mlir::OpBuilder nested(&output, output.begin());
                        auto result = self(self, source.front(), source.front().begin(), path,
                                           nested, nullptr, isBefore ? before : after, depth + 1);
                        if (!result) { return {}; }
                        if (isBefore) {
                            mlir::scf::ConditionOp::create(nested, loop.getLoc(), result->front(),
                                                           mlir::ValueRange(*result).drop_front());
                        } else {
                            mlir::scf::YieldOp::create(nested, loop.getLoc(), *result);
                        }
                        ++operationCount;
                    }
                    values.map(loop.getResults(), copied.getResults());
                    continue;
                }
                for (mlir::Value operand : operation.getOperands()) {
                    if (!step()) { return {}; }
                    if (values.lookupOrDefault(operand).getDefiningOp<mlir::ub::PoisonOp>()) {
                        refuse("DOM helper completion observes an inactive value");
                        return {};
                    }
                }
                if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                    if (continuation || terminal.kind != Terminal::returned) {
                        refuse("DOM helper completion returns inside a source region");
                        return {};
                    }
                    return llvm::SmallVector<mlir::Value>{
                        values.lookupOrDefault(result.getValue())};
                }
                Continuation tail{&body, std::next(cursor), operation.getResults(), continuation};
                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                    auto condition = values.lookupOrDefault(branch.getCondition());
                    auto constant = condition.getDefiningOp<mlir::arith::ConstantOp>();
                    auto integer = constant ? llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue())
                                            : mlir::IntegerAttr{};
                    if (integer && integer.getType().isInteger(1)) {
                        auto & selected = integer.getValue().isZero() ? branch.getElseRegion()
                                                                      : branch.getThenRegion();
                        if (selected.empty() && branch.getNumResults() == 0) {
                            return self(self, body, tail.next, values, at, continuation, terminal,
                                        depth + 1);
                        }
                        if (!selected.hasOneBlock() || selected.front().getNumArguments()) {
                            refuse("DOM helper completion has an incomplete selected arm");
                            return {};
                        }
                        // Like a switch, select only a proved completion arm.
                        // The original-body census still requires every operation
                        // to be visited on some source path before publication.
                        return self(self, selected.front(), selected.front().begin(), values, at,
                                    &tail, terminal, depth + 1);
                    }
                    // ponytail: duplicate bounded continuations, preserving their
                    // effects in each arm. Large trees stop at the work budget.
                    auto copied =
                        mlir::scf::IfOp::create(at, branch.getLoc(), terminal.types,
                                                values.lookupOrDefault(branch.getCondition()));
                    ++operationCount;
                    for (auto [source, target] :
                         llvm::zip(branch->getRegions(), copied->getRegions())) {
                        for (const auto & item : values.getValueMap()) {
                            (void)item;
                            if (!step()) { return {}; }
                        }
                        for (const auto & item : values.getOperationMap()) {
                            (void)item;
                            if (!step()) { return {}; }
                        }
                        mlir::IRMapping path(values);
                        target.emplaceBlock();
                        mlir::OpBuilder nested(&target.front(), target.front().begin());
                        Results returned;
                        if (source.empty() && branch.getNumResults() == 0) {
                            returned = self(self, body, tail.next, path, nested, continuation,
                                            terminal, depth + 1);
                        } else if (source.hasOneBlock() && source.front().getNumArguments() == 0) {
                            returned = self(self, source.front(), source.front().begin(), path,
                                            nested, &tail, terminal, depth + 1);
                        }
                        if (!returned) {
                            refuse("DOM helper completion has an incomplete branch");
                            return {};
                        }
                        mlir::scf::YieldOp::create(nested, branch.getLoc(), *returned);
                        ++operationCount;
                    }
                    return llvm::SmallVector<mlir::Value>(copied.getResults());
                }
                if (auto switcher = llvm::dyn_cast<mlir::scf::IndexSwitchOp>(operation)) {
                    auto selector = values.lookupOrDefault(switcher.getArg());
                    if (auto cast = selector.getDefiningOp<mlir::arith::IndexCastUIOp>()) {
                        selector = cast.getIn();
                    }
                    auto constant = selector.getDefiningOp<mlir::arith::ConstantOp>();
                    auto integer = constant ? llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue())
                                            : mlir::IntegerAttr{};
                    if (!integer || integer.getValue().isNegative() ||
                        integer.getValue().getActiveBits() > 63) {
                        refuse("DOM helper completion selector is not an exact constant");
                        return {};
                    }
                    auto * selected = &switcher.getDefaultRegion();
                    for (auto [index, key] : llvm::enumerate(switcher.getCases())) {
                        if (!step()) { return {}; }
                        if (key == integer.getInt()) {
                            selected = &switcher.getCaseRegions()[index];
                        }
                    }
                    if (!selected->hasOneBlock() || selected->front().getNumArguments()) {
                        refuse("DOM helper completion has an incomplete switch arm");
                        return {};
                    }
                    return self(self, selected->front(), selected->front().begin(), values, at,
                                &tail, terminal, depth + 1);
                }
                if (operation.getNumRegions() || operation.getNumSuccessors()) {
                    refuse("DOM helper completion requires acyclic structured source");
                    return {};
                }
                if (auto truncate = llvm::dyn_cast<mlir::arith::TruncIOp>(operation)) {
                    auto operand = values.lookup(truncate.getIn());
                    auto constant = operand.getDefiningOp<mlir::arith::ConstantOp>();
                    auto integer = constant ? llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue())
                                            : mlir::IntegerAttr{};
                    if (integer && truncate.getType().isInteger(1)) {
                        auto value = mlir::arith::ConstantIntOp::create(
                            at, truncate.getLoc(), integer.getValue()[0] ? 1 : 0, 1);
                        values.map(truncate.getResult(), value.getResult());
                        ++operationCount;
                        continue;
                    }
                }
                auto * copied = at.clone(operation, values);
                if (llvm::isa<mlir::arith::IndexCastUIOp, mlir::arith::CmpIOp>(operation)) {
                    // Two-way completion switches lower to index casts and
                    // integer comparisons. Reuse MLIR's exact arithmetic folds.
                    llvm::SmallVector<mlir::Value> folded;
                    if (mlir::succeeded(at.tryFold(copied, folded)) && !folded.empty()) {
                        values.map(operation.getResults(), folded);
                        copied->erase();
                    }
                }
                ++operationCount;
            }
            refuse("DOM helper completion has no return or yield");
            return {};
        };
        mlir::OpBuilder at(function.getContext());
        at.setInsertionPointToStart(&destination);
        auto & body = function.getBody().front();
        Terminal terminal;
        terminal.types.append(function.getResultTypes().begin(), function.getResultTypes().end());
        auto result = emit(emit, body, body.begin(), mapping, at, nullptr, terminal, 0);
        if (!result) { return false; }
        const auto complete = function.walk([&](mlir::Operation * operation) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (operation != function && !visited.contains(operation)) {
                refuse("DOM helper completion contains unvisited source operations");
                return mlir::WalkResult::interrupt();
            }
            return mlir::WalkResult::advance();
        });
        if (complete.wasInterrupted()) { return false; }
        ctjs::ReturnOp::create(at, function.getLoc(), result->front());
        // Only inert completion arithmetic is removed. Source effects remain
        // for the unchanged complete DOM/frame proof below.
        llvm::SmallVector<mlir::Operation *> arithmetic;
        const auto cleanup = normalized.walk([&](mlir::Operation * operation) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (llvm::isa<mlir::arith::ConstantOp, mlir::arith::IndexCastUIOp>(operation)) {
                arithmetic.push_back(operation);
            }
            return mlir::WalkResult::advance();
        });
        if (cleanup.wasInterrupted()) { return false; }
        for (mlir::Operation * operation : llvm::reverse(arithmetic)) {
            if (!step()) { return false; }
            if (operation->use_empty()) { operation->erase(); }
        }
        for (mlir::BlockArgument argument : body.getArguments()) {
            if (!step()) { return false; }
            auto found = stringInputs.find(argument);
            if (found == stringInputs.end()) { continue; }
            auto inputs = std::move(found->second);
            stringInputs.erase(found);
            stringInputs[mapping.lookup(argument)] = std::move(inputs);
        }
        function.getBody().takeBody(normalized);
        return true;
    }

    bool checkBody(ctjs::FuncOp function, bool entry, bool directReceiver = false,
                   bool beforeReplacement = false) {
        auto & block = function.getBody().front();
        llvm::DenseSet<mlir::Value> values;
        for (mlir::BlockArgument argument : block.getArguments()) {
            if (!step()) { return false; }
            values.insert(argument);
            if (entry || argument.getArgNumber() >= ctjs::implicit_arguments ||
                (directReceiver && argument.getArgNumber() == ctjs::arg_receiver)) {
                continue;
            }
            for (mlir::OpOperand & use : argument.getUses()) {
                if (!step()) { return false; }
                auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(use.getOwner());
                if (!llvm::isa<ctjs::RootOp, ctjs::CreateClosureOp>(use.getOwner()) &&
                    !(load && argument.getArgNumber() == ctjs::arg_callee &&
                      use.getOperandNumber() == 0)) {
                    return refuse("DOM helper observes an implicit argument");
                }
            }
        }
        mlir::DominanceInfo dominance(function);
        const auto visit = [&](auto && self, mlir::Block & body, unsigned depth,
                               mlir::Value & frame) -> bool {
            if (depth == 64 || (depth && body.getNumArguments() &&
                                !llvm::isa<mlir::scf::WhileOp>(body.getParentOp()))) {
                return refuse(entry ? "DOM entry branch depth or arguments are unsupported"
                                    : "DOM helper branch depth or arguments are unsupported");
            }
            for (mlir::BlockArgument argument : body.getArguments()) {
                if (!step()) { return false; }
                values.insert(argument);
            }
            bool entered = false, returned = false;
            for (mlir::Operation & operation : body) {
                if (!step()) { return false; }
                // A normalized URI invoke is opaque here: normalizeDOMURI proved
                // its regions, and the complete DOM entry proof reproves the
                // inlined result before anything is published.
                if ((operation.getNumRegions() &&
                     !llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp, ctjs::InvokeOp>(operation)) ||
                    operation.getNumSuccessors() || returned) {
                    return refuse("DOM helper requires complete structured branches");
                }
                for (mlir::Value operand : operation.getOperands()) {
                    if (!step()) { return false; }
                    if ((!values.contains(operand) && operand != frame) ||
                        !dominance.dominates(operand, &operation)) {
                        return refuse("DOM helper operand has no preceding local definition");
                    }
                    if (operand == frame &&
                        !llvm::isa<ctjs::RootOp, ctjs::FrameExitOp>(operation)) {
                        return refuse("DOM helper observes its shadow frame");
                    }
                }
                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                    if (!branch.getCondition().getType().isInteger(1) ||
                        !branch.getThenRegion().hasOneBlock() ||
                        (!branch.getElseRegion().empty() &&
                         !branch.getElseRegion().hasOneBlock()) ||
                        (branch.getNumResults() && branch.getElseRegion().empty())) {
                        return refuse("DOM helper branch lacks complete Boolean arms");
                    }
                    mlir::Value thenFrame = frame, elseFrame = frame;
                    for (mlir::Region & region : branch->getRegions()) {
                        if (region.empty()) { continue; }
                        auto & armFrame =
                            &region == &branch.getThenRegion() ? thenFrame : elseFrame;
                        if (!self(self, region.front(), depth + 1, armFrame)) { return false; }
                        auto yield =
                            llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
                        if (!yield || yield.getOperandTypes() != branch.getResultTypes()) {
                            return refuse("DOM helper branch has incomplete result correspondence");
                        }
                    }
                    if (thenFrame != elseFrame) {
                        return refuse("DOM helper branch has inconsistent shadow frame exits");
                    }
                    frame = thenFrame;
                    values.insert(branch.getResults().begin(), branch.getResults().end());
                    continue;
                }
                if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(operation)) {
                    if (!loop.getBefore().hasOneBlock() || !loop.getAfter().hasOneBlock() ||
                        loop.getBefore().front().getArgumentTypes() != loop.getInits().getTypes() ||
                        loop.getAfter().front().getArgumentTypes() != loop.getResultTypes()) {
                        return refuse("DOM helper loop lacks complete source regions");
                    }
                    mlir::Value beforeFrame = frame, afterFrame = frame;
                    if (!self(self, loop.getBefore().front(), depth + 1, beforeFrame) ||
                        !self(self, loop.getAfter().front(), depth + 1, afterFrame)) {
                        return false;
                    }
                    auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(
                        loop.getBefore().front().getTerminator());
                    auto yield =
                        llvm::dyn_cast<mlir::scf::YieldOp>(loop.getAfter().front().getTerminator());
                    if (!condition || !condition.getCondition().getType().isInteger(1) ||
                        condition.getArgs().getTypes() != loop.getResultTypes() || !yield ||
                        yield.getOperandTypes() != loop.getInits().getTypes()) {
                        return refuse("DOM helper loop has incomplete result correspondence");
                    }
                    if (beforeFrame != frame || afterFrame != frame) {
                        return refuse("DOM helper loop changes its shadow frame");
                    }
                    values.insert(loop.getResults().begin(), loop.getResults().end());
                    continue;
                }
                if (auto enter = llvm::dyn_cast<ctjs::FrameEnterOp>(operation)) {
                    if (depth || entered) {
                        return refuse("DOM helper has repeated shadow frames");
                    }
                    entered = true;
                    frame = enter.getContext();
                } else if (auto root = llvm::dyn_cast<ctjs::RootOp>(operation)) {
                    if (!frame || root.getContext() != frame) {
                        return refuse("DOM helper root is outside its shadow frame");
                    }
                } else if (auto exit = llvm::dyn_cast<ctjs::FrameExitOp>(operation)) {
                    if (!frame || exit.getContext() != frame) {
                        return refuse("DOM helper exits an unknown shadow frame");
                    }
                    frame = {};
                } else if (llvm::isa<ctjs::ReturnOp>(operation)) {
                    if (depth || frame) {
                        return refuse(
                            "DOM helper returns inside a branch or with a live shadow frame");
                    }
                    returned = true;
                } else if (llvm::isa<mlir::scf::YieldOp>(operation)) {
                    if (!depth) { return refuse("DOM helper yield is outside a branch"); }
                    returned = true;
                } else if (llvm::isa<mlir::scf::ConditionOp>(operation)) {
                    auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(body.getParentOp());
                    if (!depth || !loop || body.getParent() != &loop.getBefore()) {
                        return refuse("DOM helper condition is outside a loop before region");
                    }
                    returned = true;
                } else {
                    values.insert(operation.getResults().begin(), operation.getResults().end());
                }
                // A proved capture-free callback stays in its selected source
                // arm. Other callable/capture definitions still require the
                // entry block; repeated loop-local identities are not proved.
                auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(operation);
                if (depth) {
                    auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation);
                    auto target =
                        closure && closure.getFunction() >= 0
                            ? functions.lookup(static_cast<unsigned>(closure.getFunction()))
                            : ctjs::FuncOp{};
                    if ((closure && (!target || closure->getParentOfType<mlir::scf::WhileOp>() ||
                                     (!confinedFilterCallback(closure, target) &&
                                      !confinedReplacementCallback(closure, target)))) ||
                        llvm::isa<ctjs::CreateCellOp>(operation) ||
                        (object && !dataObject(object))) {
                        return refuse("DOM helper branch contains an unproved local identity");
                    }
                }
                if (directReceiver && !beforeReplacement) {
                    auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation);
                    auto target =
                        closure && closure.getFunction() >= 0
                            ? functions.lookup(static_cast<unsigned>(closure.getFunction()))
                            : ctjs::FuncOp{};
                    if (llvm::isa<ctjs::LoadUpvalueOp>(operation) ||
                        (closure && (!target || (!confinedFilterCallback(closure, target) &&
                                                 !confinedReplacementCallback(closure, target))))) {
                        return refuse("DOM direct helper contains an unproved closure or capture");
                    }
                }
                if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation)) {
                    if (closure.getFunctionAttr().getInt() < 0 ||
                        closure.getEnclosingClosure() != block.getArgument(ctjs::arg_callee) ||
                        (closure.getEnclosingThis() != block.getArgument(ctjs::arg_receiver) &&
                         !undefined(closure.getEnclosingThis()))) {
                        return refuse("DOM helper lacks an exact local closure identity");
                    }
                }
                if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation)) {
                    if (entry || load.getClosure() != block.getArgument(ctjs::arg_callee) ||
                        load.getIndex() < 0 || load.getIndex() >= function.getUpvalueCount()) {
                        return refuse("DOM helper upvalue lacks an exact capture slot");
                    }
                }
            }
            if (!returned) { return refuse("DOM helper has no complete return or yield"); }
            return true;
        };
        mlir::Value frame;
        return visit(visit, block, 0, frame);
    }

    bool proveUnusedBody(ctjs::FuncOp function) {
        if (function.getUpvalueCount() != 0 || !checkBody(function, false)) { return false; }
        // No invocation supplies parameter facts. These operations are inert for
        // every value, including Objects and Symbols; conversions and calls are
        // deliberately excluded. Check the original body before retiring it.
        // Check both arms, including discarded values and constant-dead arms.
        // checkBody already proved their dominance, completion and shadow frames.
        // ponytail: uncaptured conditional leaves; loops and helper calls need
        // their own complete independent body proof.
        const auto checked = function.walk([&](mlir::Operation * operation) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (operation == function) { return mlir::WalkResult::advance(); }
            if (llvm::isa<ctjs::ConstantOp, ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp,
                          ctjs::ReturnOp, ctjs::TruthyOp, mlir::scf::IfOp, mlir::scf::YieldOp>(
                    operation)) {
                return mlir::WalkResult::advance();
            }
            if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation);
                unary && (unary.getKind() == ctjs::UnaryKind::TypeOf ||
                          unary.getKind() == ctjs::UnaryKind::Not ||
                          unary.getKind() == ctjs::UnaryKind::Void)) {
                return mlir::WalkResult::advance();
            }
            if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation);
                compare && compare.getKind() == ctjs::CompareKind::StrictEq) {
                return mlir::WalkResult::advance();
            }
            refuse(("unused DOM helper body contains an unproved operation: " +
                    function.getSymName() + " / " + operation->getName().getStringRef())
                       .str());
            return mlir::WalkResult::interrupt();
        });
        if (checked.wasInterrupted()) { return false; }
        expanded.insert(function);
        return true;
    }

    bool inlineCall(ctjs::FuncOp function, ctjs::FuncOp target, mlir::Operation * call,
                    mlir::ValueRange arguments, mlir::Value receiver, mlir::Value callee,
                    llvm::MutableArrayRef<Capture> captures, unsigned depth) {
        auto & block = function.getBody().front();
        auto & body = target.getBody().front();
        mlir::IRMapping mapping;
        mapping.map(body.getArgument(ctjs::arg_callee), callee);
        mapping.map(body.getArgument(ctjs::arg_receiver), receiver);
        for (auto [formal, actual] :
             llvm::zip(body.getArguments().drop_front(ctjs::implicit_arguments), arguments)) {
            if (!step()) { return false; }
            mapping.map(formal, actual);
        }
        const auto cloneBody = [&](auto && self, mlir::Block & source,
                                   mlir::OpBuilder & at) -> bool {
            for (mlir::Operation & operation : source) {
                if (!step()) { return false; }
                if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(operation)) {
                    continue;
                }
                if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation)) {
                    // Substitute at each invocation, including branch-local
                    // loads, never bind a shared body to its first caller.
                    auto & capture = captures[static_cast<unsigned>(load.getIndex())];
                    mlir::Value value;
                    if (capture.enclosingIndex >= 0) {
                        value = ctjs::LoadUpvalueOp::create(at, load.getLoc(), load.getType(),
                                                            block.getArgument(ctjs::arg_callee),
                                                            capture.enclosingIndex);
                        ++operationCount;
                    } else {
                        value = capture.value();
                    }
                    mapping.map(load.getResult(), value);
                } else if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                    call->getResult(0).replaceAllUsesWith(mapping.lookup(result.getValue()));
                } else if (llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp>(operation)) {
                    mlir::OperationState state(operation.getLoc(), operation.getName());
                    for (mlir::Value operand : operation.getOperands()) {
                        if (!step()) { return false; }
                        state.addOperands(mapping.lookup(operand));
                    }
                    state.addTypes(operation.getResultTypes());
                    state.addAttributes(operation.getAttrs());
                    for (unsigned i = 0; i < operation.getNumRegions(); ++i) { state.addRegion(); }
                    auto * cloned = at.create(state);
                    ++operationCount;
                    for (auto [from, to] :
                         llvm::zip(operation.getRegions(), cloned->getRegions())) {
                        if (from.empty()) { continue; }
                        auto & destination = to.emplaceBlock();
                        for (mlir::BlockArgument argument : from.front().getArguments()) {
                            if (!step()) { return false; }
                            mapping.map(argument, destination.addArgument(argument.getType(),
                                                                          argument.getLoc()));
                        }
                        mlir::OpBuilder nested(&destination, destination.begin());
                        if (!self(self, from.front(), nested)) { return false; }
                    }
                    mapping.map(operation.getResults(), cloned->getResults());
                } else {
                    auto * cloned = at.clone(operation, mapping);
                    if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation);
                        closure &&
                        (confinedFilterCallback(closure, functions.lookup(static_cast<unsigned>(
                                                             closure.getFunction()))) ||
                         confinedReplacementCallback(
                             closure,
                             functions.lookup(static_cast<unsigned>(closure.getFunction()))))) {
                        // The original enclosure and unused implicit arguments
                        // were proved before cloning. Preserve this callback in
                        // its source arm with the caller's inert enclosure.
                        auto callback = llvm::cast<ctjs::CreateClosureOp>(cloned);
                        callback.getEnclosingClosureMutable().assign(
                            block.getArgument(ctjs::arg_callee));
                        if (closure.getEnclosingThis() == body.getArgument(ctjs::arg_receiver)) {
                            callback.getEnclosingThisMutable().assign(
                                block.getArgument(ctjs::arg_receiver));
                        }
                    }
                    // Regions (a normalized invoke) clone with the
                    // same mapping; charge every nested operation.
                    const auto counted = cloned->walk([&](mlir::Operation * inner) {
                        if (inner == cloned) { return mlir::WalkResult::advance(); }
                        if (!step()) { return mlir::WalkResult::interrupt(); }
                        ++operationCount;
                        return mlir::WalkResult::advance();
                    });
                    if (counted.wasInterrupted()) { return false; }
                    if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(cloned)) {
                        callDepth[cloned] =
                            1 + callDepth.lookup(call) + callDepth.lookup(&operation);
                        if (depth + callDepth[cloned] >= 64) {
                            return refuse("DOM helper call tree is recursive or too deep");
                        }
                    }
                    ++operationCount;
                }
            }
            return true;
        };
        mlir::OpBuilder at(call);
        if (!cloneBody(cloneBody, body, at)) { return false; }
        return true;
    }

    bool expand(ctjs::FuncOp function, unsigned depth, bool entry = false,
                bool directReceiver = false) {
        if (!step()) { return false; }
        if (expanded.contains(function)) { return true; }
        // ponytail: bounded local call trees; recursive source needs a separate
        // call/lifetime proof, not recursive compiler expansion.
        if (depth == 64 || !active.insert(function).second) {
            return refuse("DOM helper call tree is recursive or too deep");
        }
        if (!normalizeCompletion(function) || !checkBody(function, entry, directReceiver, true)) {
            return false;
        }
        // Parameters with nested source functions may have an otherwise local
        // cell. Resolve those reads before specializing their intrinsic calls;
        // captured cells still wait for the unchanged child/capture proof.
        for (ctjs::CreateCellOp cell : function.getBody().front().getOps<ctjs::CreateCellOp>()) {
            bool captured = false;
            for (mlir::Operation * use : cell.getResult().getUsers()) {
                if (!step()) { return false; }
                captured |= llvm::isa<ctjs::CreateClosureOp>(use);
            }
            if (!captured && !resolveCell(cell)) { return false; }
        }
        if (!foldConstantReplacements(function)) { return false; }
        // Enclosing identities were checked on the original body above. Only
        // the complete replacement proof may remove callbacks. Only confined,
        // capture-free callbacks may survive direct helper expansion.
        if (directReceiver && !checkBody(function, entry, true)) { return false; }
        llvm::SmallVector<ctjs::CallDirectOp> directCalls;
        const auto collected = function.walk([&](mlir::Operation * operation) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
                call && undefined(call.getCalleeValue())) {
                directCalls.push_back(call);
            }
            return mlir::WalkResult::advance();
        });
        if (collected.wasInterrupted()) { return false; }
        for (ctjs::CallDirectOp call : directCalls) {
            if (!step()) { return false; }
            auto target = call.getTarget();
            const auto index = target ? functionIndex(target) : std::nullopt;
            if (!index || functions.lookup(*index) != target || !target.isPrivate() ||
                creations.lookup(*index) != 0 || target.getUpvalueCount() != 0 ||
                !undefined(call.getNewTarget()) ||
                call.getArgs().size() + ctjs::implicit_arguments !=
                    target.getBody().front().getNumArguments()) {
                return refuse("DOM direct helper requires an exact uncaptured local call");
            }
            if (depth + callDepth.lookup(call) >= 63) {
                return refuse("DOM helper call tree is recursive or too deep");
            }
            if (!bindConstantArguments({}, target)) { return false; }
        }
        // Bind every sibling's complete call census before expanding a shared
        // callee. Otherwise an unvisited sibling's formal hides its actual
        // String inputs from that callee's existing all-use proof.
        for (ctjs::CallDirectOp call : directCalls) {
            auto target = call.getTarget();
            // The symbol is already the direct-call contract. This normalization
            // proves no new source dispatch: it binds each actual receiver and
            // retains every operation for the complete DOM entry reproof.
            if (!expand(target, depth + 1, false, true) ||
                !inlineCall(function, target, call, call.getArgs(), call.getReceiver(),
                            call.getCalleeValue(), {}, depth)) {
                return false;
            }
            callDepth.erase(call);
            call.erase();
        }
        if (!forwardFields(function)) { return false; }
        auto & block = function.getBody().front();
        llvm::SmallVector<ctjs::CreateClosureOp> closures;
        llvm::SmallVector<ctjs::CreateObjectOp> methodObjects;
        llvm::SmallVector<ctjs::CreateCellOp> localCells;
        for (mlir::Operation & operation : block) {
            if (!step()) { return false; }
            if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation)) {
                closures.push_back(closure);
            }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                const auto collected =
                    branch.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * nested) {
                        if (!step()) { return mlir::WalkResult::interrupt(); }
                        if (llvm::isa<ctjs::InvokeOp>(nested)) { return mlir::WalkResult::skip(); }
                        if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(nested)) {
                            closures.push_back(closure);
                        }
                        return mlir::WalkResult::advance();
                    });
                if (collected.wasInterrupted()) { return false; }
            }
            if (auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(operation)) {
                localCells.push_back(cell);
            }
            if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(operation)) {
                if (!dataObject(object)) {
                    if (!reason.empty()) { return false; }
                    methodObjects.push_back(object);
                }
            }
        }
        // Capturing children must be leaves before the shared capture query.
        // Uncaptured helpers wait until their holders retire and expose every
        // invocation; only then can argument facts specialize their bodies.
        for (ctjs::CreateClosureOp closure : closures) {
            auto target = functions.lookup(static_cast<unsigned>(closure.getFunction()));
            if (!target) { return refuse("DOM helper closure target is missing"); }
            if (!closure.getUpvalues().empty() && !expand(target, depth + 1)) { return false; }
        }
        for (ctjs::CreateCellOp cell : localCells) {
            if (!cells.contains(cell.getResult()) && !resolveCell(cell)) { return false; }
        }
        llvm::DenseSet<mlir::Operation *> methods, resolvedObjects;
        // ponytail: bounded rescans of local capture dependencies; index the
        // worklist if large helper graphs exhaust the existing work budget.
        while (!closures.empty() || !localCells.empty() ||
               resolvedObjects.size() != methodObjects.size()) {
            if (!step()) { return false; }
            bool progress = false;
            for (ctjs::CreateCellOp & cell : localCells) {
                if (!cell) { continue; }
                bool held = false;
                for (mlir::Operation * use : cell.getResult().getUsers()) {
                    if (!step()) { return false; }
                    held |= llvm::isa<ctjs::CreateClosureOp>(use);
                }
                if (held) { continue; }
                auto capture = cells.lookup(cell.getResult());
                if (capture.write) { capture.write.erase(); }
                while (!cell.getResult().use_empty()) {
                    if (!step()) { return false; }
                    auto root = llvm::dyn_cast<ctjs::RootOp>(*cell.getResult().getUsers().begin());
                    if (!root) { return refuse("DOM helper capture cell has an unexpanded use"); }
                    root.erase();
                }
                cells.erase(cell.getResult());
                cell.erase();
                cell = {};
                progress = true;
            }
            for (ctjs::CreateObjectOp object : methodObjects) {
                if (!step()) { return false; }
                if (resolvedObjects.contains(object)) { continue; }
                bool held = false;
                for (mlir::OpOperand & use : object.getResult().getUses()) {
                    if (!step()) { return false; }
                    held |= captureStorage(use);
                }
                if (held) { continue; }
                if (!resolveMethods(object, methods)) { return false; }
                resolvedObjects.insert(object);
                progress = true;
            }
            for (ctjs::CreateClosureOp & closure : closures) {
                if (!closure) { continue; }
                bool held = false;
                for (mlir::OpOperand & use : closure.getResult().getUses()) {
                    if (!step()) { return false; }
                    auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
                    auto object = store ? store.getObject().getDefiningOp<ctjs::CreateObjectOp>()
                                        : ctjs::CreateObjectOp{};
                    held |= captureStorage(use) ||
                            (object && use.getOperandNumber() == 2 && object->getBlock() == &block);
                }
                if (held) { continue; }
                auto target = functions.lookup(static_cast<unsigned>(closure.getFunction()));
                if (!target) { return refuse("DOM helper closure target is missing"); }
                if (confinedFilterCallback(closure, target) ||
                    confinedReplacementCallback(closure, target)) {
                    if (!expand(target, depth + 1)) { return false; }
                    // Preserve both source identities. Complete DOM reproof must
                    // establish the intrinsic method, callback body and every use.
                    retainedCallbacks.insert(target);
                    closure = {};
                    progress = true;
                    continue;
                }
                if (!reason.empty()) { return false; }
                if (closure.getUpvalues().size() != target.getUpvalueCount()) {
                    return refuse("DOM helper capture count disagrees with its source target");
                }
                llvm::SmallVector<Capture> captures;
                if (!closure.getUpvalues().empty()) {
                    if (!captureTarget(closure, target)) { return false; }
                    const auto indices = closure.getEnclosingIndicesAttr();
                    for (auto [slot, capture] : llvm::enumerate(closure.getUpvalues())) {
                        if (!step()) { return false; }
                        if (indices && indices[slot] >= 0) {
                            captures.push_back({{}, {}, indices[slot]});
                            continue;
                        }
                        const auto found = cells.find(capture);
                        if (found == cells.end()) {
                            return refuse("DOM helper capture lacks a proved local cell");
                        }
                        captures.push_back(found->second);
                    }
                }
                struct Call {
                    mlir::Operation * operation;
                    mlir::ValueRange arguments;
                };
                llvm::SmallVector<Call> calls;
                llvm::SmallVector<ctjs::RootOp> roots;
                for (mlir::OpOperand & use : closure.getResult().getUses()) {
                    if (!step()) { return false; }
                    if (auto root = llvm::dyn_cast<ctjs::RootOp>(use.getOwner())) {
                        roots.push_back(root);
                    } else {
                        auto * operation = use.getOwner();
                        mlir::ValueRange arguments;
                        if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
                            call && use.getOperandNumber() == 0 &&
                            (undefined(call.getReceiver()) || methods.contains(operation))) {
                            arguments = call.getArgs();
                        } else if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
                                   direct && use.getOperandNumber() == 2 &&
                                   direct.getCallee() == target.getSymName() &&
                                   (undefined(direct.getReceiver()) ||
                                    methods.contains(operation)) &&
                                   undefined(direct.getNewTarget())) {
                            arguments = direct.getArgs();
                        } else {
                            return refuse(
                                "DOM helper callable escapes or its call shape is unsupported");
                        }
                        if (!precedesInStructuredBody(closure, operation) ||
                            arguments.size() + ctjs::implicit_arguments !=
                                target.getBody().front().getNumArguments()) {
                            return refuse("DOM helper call has unsupported arity or source order");
                        }
                        for (const Capture & capture : captures) {
                            if (!step()) { return false; }
                            if (capture.write &&
                                !precedesInStructuredBody(capture.write, operation)) {
                                return refuse(
                                    "DOM helper capture assignment does not precede its call");
                            }
                        }
                        if (depth + callDepth.lookup(operation) >= 63) {
                            return refuse("DOM helper call tree is recursive or too deep");
                        }
                        calls.push_back({operation, arguments});
                    }
                }
                if (calls.empty()) {
                    if (!closure.getUpvalues().empty() ||
                        creations.lookup(static_cast<unsigned>(closure.getFunction())) != 1 ||
                        !proveUnusedBody(target)) {
                        return refuse("DOM helper has no independently proved unused body");
                    }
                } else {
                    if (!bindConstantArguments(closure, target)) { return false; }
                    if (!expand(target, depth + 1)) { return false; }
                }
                for (const Call & call : calls) {
                    // Live closure metadata retains its enclosing identities;
                    // receiver-observing direct functions bind the actual instead.
                    if (!inlineCall(function, target, call.operation, call.arguments,
                                    block.getArgument(ctjs::arg_receiver),
                                    block.getArgument(ctjs::arg_callee), captures, depth)) {
                        return false;
                    }
                    methods.erase(call.operation);
                    callDepth.erase(call.operation);
                    call.operation->erase();
                }
                for (ctjs::RootOp root : roots) { root.erase(); }
                closure.erase();
                closure = {};
                progress = true;
            }
            llvm::erase_if(closures, [](auto closure) { return !closure; });
            llvm::erase_if(localCells, [](auto cell) { return !cell; });
            if (!progress) { return refuse("DOM helper capture graph is cyclic or escapes"); }
        }
        for (ctjs::CreateObjectOp object : methodObjects) {
            while (!object.getResult().use_empty()) {
                if (!step()) { return false; }
                auto root = llvm::dyn_cast<ctjs::RootOp>(*object.getResult().getUsers().begin());
                if (!root) { return refuse("DOM helper object has an unexpanded use"); }
                root.erase();
            }
            object.erase();
        }
        active.erase(function);
        expanded.insert(function);
        return true;
    }
};
} // namespace

llvm::Error expandDOMHelpers(mlir::ModuleOp candidate, llvm::StringRef entry, unsigned maxSteps) {
    DOMSource source(maxSteps);
    candidate.walk([&](mlir::Operation * operation) {
        if (!source.step()) { return mlir::WalkResult::interrupt(); }
        ++source.operationCount;
        if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation);
            closure && closure.getFunction() >= 0) {
            ++source.creations[static_cast<unsigned>(closure.getFunction())];
        }
        return mlir::WalkResult::advance();
    });
    auto target = candidate.lookupSymbol<ctjs::FuncOp>(entry);
    ctjs::FuncOp wrapper;
    for (mlir::Operation & operation : candidate.getBody()->getOperations()) {
        auto function = llvm::dyn_cast<ctjs::FuncOp>(operation);
        if (!source.step()) { break; }
        if (!function || !llvm::hasSingleElement(function.getBody()) ||
            (functionIndex(function) == 0 && function.getUpvalueCount() != 0) ||
            function->hasAttr("ctjs.skipped") ||
            function.getBody().front().getNumArguments() < ctjs::implicit_arguments ||
            !llvm::all_of(function.getBody().front().getArgumentTypes(),
                          [](mlir::Type type) { return llvm::isa<ctjs::ValueType>(type); })) {
            source.refuse(
                "DOM helper requires complete source functions and an uncaptured wrapper");
            break;
        }
        const auto index = functionIndex(function);
        if (!index || !source.functions.try_emplace(*index, function).second) {
            source.refuse("DOM helper source function identity is ambiguous");
            break;
        }
        if (*index == 0 && function != target) { wrapper = function; }
    }
    if (source.reason.empty() && !target) { source.refuse("DOM entry function is missing"); }
    bool initialized = false;
    if (source.reason.empty() && wrapper &&
        !host_detail::isInertEntryDeclaration(wrapper, target, [&] { return source.step(); })) {
        initialized = source.initializeEntry(wrapper, target);
    }
    if (source.reason.empty() && source.expand(initialized ? wrapper : target, 0, true)) {
        for (auto [index, function] : source.functions) {
            (void)index;
            if (!source.step()) { break; }
            if (function != wrapper && !source.expanded.contains(function)) {
                // Class lifting can retire an unread holder slot while retaining
                // its original body. Numeric closures and symbolic calls must
                // both be absent before its independent leaf proof can suffice.
                if (source.remaining / 2 < source.operationCount) {
                    source.refuse("DOM helper expansion work budget exhausted");
                    break;
                }
                source.remaining -= 2 * source.operationCount;
                if (source.creations.lookup(index) != 0 ||
                    !mlir::SymbolTable::symbolKnownUseEmpty(function, candidate.getOperation()) ||
                    !mlir::SymbolTable::symbolKnownUseEmpty(function, &candidate.getBodyRegion()) ||
                    !source.proveUnusedBody(function)) {
                    source.refuse("DOM helper source contains an unvisited function");
                    break;
                }
            }
        }
    }
    if (!source.reason.empty()) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), source.reason);
    }
    if (initialized) {
        target.getBody().takeBody(wrapper.getBody());
        target.setUpvalueCount(0);
        wrapper.erase();
        source.functions.erase(0);
        wrapper = {};
    }
    for (auto [index, function] : source.functions) {
        (void)index;
        if (function != target && function != wrapper &&
            !source.retainedCallbacks.contains(function)) {
            function.erase();
        }
    }
    return llvm::Error::success();
}
} // namespace ctcompile::ctnative
