#include "Analysis.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/ImmutableCaptures.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/IRMapping.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

namespace ctcompile::ctnative {
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
    llvm::DenseSet<mlir::Operation *> active, expanded;
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

    bool bindConstantArguments(ctjs::CreateClosureOp closure, ctjs::FuncOp target) {
        if (!closure.getUpvalues().empty() || target.getUpvalueCount() != 0 ||
            creations.lookup(static_cast<unsigned>(closure.getFunction())) != 1) {
            return true;
        }
        auto & body = target.getBody().front();
        llvm::SmallVector<llvm::SmallVector<ctjs::StringAttr>> inputs(body.getNumArguments());
        llvm::SmallVector<bool> complete(body.getNumArguments(), true);
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
            if (use.getOwner()->getBlock() != closure->getBlock() ||
                !closure->isBeforeInBlock(use.getOwner()) ||
                arguments.size() + ctjs::implicit_arguments != body.getNumArguments()) {
                return true;
            }
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

    bool foldNoMatchReplacements(ctjs::FuncOp function) {
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
            if (matches) { continue; }
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
            // The isolated provider fixes the complete initial String/RegExp
            // prototype chains (including @@replace, exec and flag accessors)
            // and the reserved literal factory binding.
            // The residual source must still pass complete DOM reproof, which
            // forbids mutation and script reentry. Fresh literal state cannot
            // escape, and this no-match execution never invokes the callback.
            call.getResult().replaceAllUsesWith(call.getReceiver());
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
        for (mlir::Operation & operation : target.getBody().front()) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::StoreUpvalueOp, ctjs::CreateClosureOp>(operation)) {
                return refuse("DOM helper forwarded capture is mutable or unexpanded");
            }
        }
        return true;
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
        llvm::SmallVector<ctjs::CellGetOp> reads;
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            if (!step()) { return false; }
            auto * operation = use.getOwner();
            if (operation->getBlock() != cell->getBlock() || !cell->isBeforeInBlock(operation)) {
                return refuse("DOM helper capture cell has nonlocal or unordered uses");
            }
            if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(operation)) {
                if (write && !write->isBeforeInBlock(read)) {
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

    bool resolveMethods(ctjs::CreateObjectOp object, llvm::DenseSet<mlir::Operation *> & methods) {
        llvm::DenseMap<mlir::Attribute, ctjs::SetPropertyOp> slots;
        llvm::SmallVector<ctjs::GetPropertyOp> reads;
        const auto key = [](mlir::Value value) {
            auto constant = value.getDefiningOp<ctjs::ConstantOp>();
            return constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue())
                            : ctjs::StringAttr{};
        };
        // The isolated DOM provider starts with standard Object.prototype.
        // Every remaining source effect must still pass the complete DOM
        // proof: no prototype mutation, unknown calls or script reentry.
        // Only __proto__ has an inherited setter in that initial object.
        for (mlir::OpOperand & use : object.getResult().getUses()) {
            if (!step()) { return false; }
            auto * operation = use.getOwner();
            if (operation->getBlock() != object->getBlock() ||
                !object->isBeforeInBlock(operation)) {
                return refuse("DOM helper object has nonlocal or unordered uses");
            }
            if (llvm::isa<ctjs::RootOp>(operation)) { continue; }
            if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
                auto name = key(store.getKey());
                auto closure = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
                if (use.getOperandNumber() != 0 || !name || name.getValue() == "__proto__" ||
                    !closure || closure->getBlock() != object->getBlock() ||
                    !closure->isBeforeInBlock(store) || !slots.try_emplace(name, store).second) {
                    return refuse("DOM helper object requires unique own callable slots");
                }
            } else if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
                       read && use.getOperandNumber() == 0) {
                reads.push_back(read);
            } else {
                auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
                auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
                mlir::Value callee;
                if (call && use.getOperandNumber() == 1) { callee = call.getCallee(); }
                if (direct && use.getOperandNumber() == 0) { callee = direct.getCalleeValue(); }
                auto methodRead =
                    callee ? callee.getDefiningOp<ctjs::GetPropertyOp>() : ctjs::GetPropertyOp{};
                if (!methodRead || methodRead.getObject() != object.getResult() ||
                    methodRead->getBlock() != object->getBlock() ||
                    !methodRead->isBeforeInBlock(operation)) {
                    return refuse("DOM helper object escapes or observes its identity");
                }
                methods.insert(operation);
            }
        }
        if (slots.empty()) { return refuse("DOM helper object has no callable slots"); }
        for (ctjs::GetPropertyOp read : reads) {
            if (!step()) { return false; }
            auto name = key(read.getKey());
            auto store = name ? slots.lookup(name) : ctjs::SetPropertyOp{};
            if (!store || !store->isBeforeInBlock(read)) {
                return refuse("DOM helper object read lacks a preceding own callable slot");
            }
            read.getResult().replaceAllUsesWith(store.getValue());
            read.erase();
        }
        for (auto [name, store] : slots) {
            (void)name;
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

    bool checkBody(ctjs::FuncOp function, bool entry) {
        auto & block = function.getBody().front();
        llvm::DenseSet<mlir::Value> values;
        for (mlir::BlockArgument argument : block.getArguments()) {
            if (!step()) { return false; }
            values.insert(argument);
            if (entry || argument.getArgNumber() >= ctjs::implicit_arguments) { continue; }
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
        mlir::Value frame;
        bool entered = false, returned = false;
        for (mlir::Operation & operation : block) {
            if (!step()) { return false; }
            if ((operation.getNumRegions() && !(entry && llvm::isa<mlir::scf::IfOp>(operation))) ||
                operation.getNumSuccessors() || returned) {
                return refuse("DOM helper requires a complete straight-line body");
            }
            for (mlir::Value operand : operation.getOperands()) {
                if (!step()) { return false; }
                if (!values.contains(operand) && operand != frame) {
                    return refuse("DOM helper operand has no preceding local definition");
                }
                if (operand == frame && !llvm::isa<ctjs::RootOp, ctjs::FrameExitOp>(operation)) {
                    return refuse("DOM helper observes its shadow frame");
                }
            }
            if (auto enter = llvm::dyn_cast<ctjs::FrameEnterOp>(operation)) {
                if (entered) { return refuse("DOM helper has repeated shadow frames"); }
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
                if (frame) { return refuse("DOM helper returns with a live shadow frame"); }
                returned = true;
            } else {
                values.insert(operation.getResults().begin(), operation.getResults().end());
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
        if (!returned) { return refuse("DOM helper has no complete return"); }
        return true;
    }

    bool expand(ctjs::FuncOp function, unsigned depth, bool entry = false) {
        if (!step()) { return false; }
        if (expanded.contains(function)) { return true; }
        // ponytail: bounded local call trees; recursive source needs a separate
        // call/lifetime proof, not recursive compiler expansion.
        if (depth == 64 || !active.insert(function).second) {
            return refuse("DOM helper call tree is recursive or too deep");
        }
        if (!checkBody(function, entry)) { return false; }
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
        if (!foldNoMatchReplacements(function)) { return false; }
        auto & block = function.getBody().front();
        llvm::SmallVector<ctjs::CreateClosureOp> closures;
        llvm::SmallVector<ctjs::CreateObjectOp> objects;
        llvm::SmallVector<ctjs::CreateCellOp> localCells;
        for (mlir::Operation & operation : block) {
            if (!step()) { return false; }
            if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation)) {
                closures.push_back(closure);
            }
            if (auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(operation)) {
                localCells.push_back(cell);
            }
            if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(operation)) {
                objects.push_back(object);
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
               resolvedObjects.size() != objects.size()) {
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
            for (ctjs::CreateObjectOp object : objects) {
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
                        if (operation->getBlock() != &block ||
                            !closure->isBeforeInBlock(operation) ||
                            arguments.size() + ctjs::implicit_arguments !=
                                target.getBody().front().getNumArguments()) {
                            return refuse("DOM helper call has unsupported arity or source order");
                        }
                        for (const Capture & capture : captures) {
                            if (!step()) { return false; }
                            if (capture.write && !capture.write->isBeforeInBlock(operation)) {
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
                if (calls.empty()) { return refuse("DOM helper has no source invocation"); }
                if (!bindConstantArguments(closure, target)) { return false; }
                if (!expand(target, depth + 1)) { return false; }
                for (const Call & call : calls) {
                    mlir::IRMapping mapping;
                    auto & body = target.getBody().front();
                    for (auto [formal, actual] :
                         llvm::zip(body.getArguments().drop_front(ctjs::implicit_arguments),
                                   call.arguments)) {
                        mapping.map(formal, actual);
                    }
                    mlir::OpBuilder at(call.operation);
                    for (mlir::Operation & operation : body) {
                        if (!step()) { return false; }
                        if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(
                                operation)) {
                            continue;
                        }
                        if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation)) {
                            // Read the checked cell at this invocation, never bind a
                            // shared helper body to its first caller's SSA values.
                            auto & capture = captures[static_cast<unsigned>(load.getIndex())];
                            mlir::Value value;
                            if (capture.enclosingIndex >= 0) {
                                value = ctjs::LoadUpvalueOp::create(
                                    at, load.getLoc(), load.getType(),
                                    block.getArgument(ctjs::arg_callee), capture.enclosingIndex);
                                ++operationCount;
                            } else {
                                value = capture.value();
                            }
                            mapping.map(load.getResult(), value);
                        } else if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                            call.operation->getResult(0).replaceAllUsesWith(
                                mapping.lookup(result.getValue()));
                        } else {
                            auto * cloned = at.clone(operation, mapping);
                            if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(cloned)) {
                                callDepth[cloned] = 1 + callDepth.lookup(call.operation) +
                                                    callDepth.lookup(&operation);
                                if (depth + callDepth[cloned] >= 64) {
                                    return refuse("DOM helper call tree is recursive or too deep");
                                }
                            }
                            ++operationCount;
                        }
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
        for (ctjs::CreateObjectOp object : objects) {
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
                source.refuse("DOM helper source contains an unvisited function");
                break;
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
        if (function != target && function != wrapper) { function.erase(); }
    }
    return llvm::Error::success();
}
} // namespace ctcompile::ctnative
