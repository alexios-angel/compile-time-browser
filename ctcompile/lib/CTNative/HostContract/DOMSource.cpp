#include "Analysis.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/ImmutableCaptures.h"
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
    llvm::DenseSet<mlir::Operation *> active, expanded;
    struct Capture {
        ctjs::CreateCellOp cell;
        ctjs::CellSetOp write;
        mlir::Value value() { return write ? write.getValue() : cell.getInitial(); }
    };
    llvm::DenseMap<mlir::Value, Capture> cells;

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

    bool resolveCell(ctjs::CreateCellOp cell) {
        auto module = cell->getParentOfType<mlir::ModuleOp>();
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::CreateClosureOp>(use.getOwner()) && !chargeCaptureQuery()) {
                return false;
            }
        }
        if (!immutableCaptureCell(cell, module)) {
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

    bool expand(ctjs::FuncOp function, unsigned depth, bool entry = false) {
        if (!step()) { return false; }
        if (expanded.contains(function)) { return true; }
        // ponytail: bounded local call trees; recursive source needs a separate
        // call/lifetime proof, not recursive compiler expansion.
        if (depth == 64 || !active.insert(function).second) {
            return refuse("DOM helper call tree is recursive or too deep");
        }
        auto & block = function.getBody().front();
        llvm::DenseSet<mlir::Value> values;
        llvm::SmallVector<ctjs::CreateClosureOp> closures;
        llvm::SmallVector<ctjs::CreateObjectOp> objects;
        llvm::SmallVector<ctjs::CreateCellOp> localCells;
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
            if (operation.getNumRegions() || operation.getNumSuccessors() || returned) {
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
                closures.push_back(closure);
            }
            if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation)) {
                if (entry || load.getClosure() != block.getArgument(ctjs::arg_callee) ||
                    load.getIndex() < 0 || load.getIndex() >= function.getUpvalueCount()) {
                    return refuse("DOM helper upvalue lacks an exact capture slot");
                }
            }
            if (auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(operation)) {
                localCells.push_back(cell);
            }
            if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(operation)) {
                objects.push_back(object);
            }
        }
        if (!returned) { return refuse("DOM helper has no complete return"); }
        for (ctjs::CreateCellOp cell : localCells) {
            if (!resolveCell(cell)) { return false; }
        }
        llvm::DenseSet<mlir::Operation *> methods;
        for (ctjs::CreateObjectOp object : objects) {
            if (!resolveMethods(object, methods)) { return false; }
        }
        for (ctjs::CreateClosureOp closure : closures) {
            auto target = functions.lookup(static_cast<unsigned>(closure.getFunction()));
            if (!target) { return refuse("DOM helper closure target is missing"); }
            if (closure.getUpvalues().size() != target.getUpvalueCount()) {
                return refuse("DOM helper capture count disagrees with its source target");
            }
            llvm::SmallVector<Capture> captures;
            if (!closure.getUpvalues().empty()) {
                if (!chargeCaptureQuery()) { return false; }
                if (immutableClosureTarget(closure, closure->getParentOfType<mlir::ModuleOp>()) !=
                    target) {
                    return refuse("DOM helper requires an immutable local leaf capture");
                }
                for (mlir::Value capture : closure.getUpvalues()) {
                    if (!step()) { return false; }
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
                               (undefined(direct.getReceiver()) || methods.contains(operation)) &&
                               undefined(direct.getNewTarget())) {
                        arguments = direct.getArgs();
                    } else {
                        return refuse(
                            "DOM helper callable escapes or its call shape is unsupported");
                    }
                    if (operation->getBlock() != &block || !closure->isBeforeInBlock(operation) ||
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
                    calls.push_back({operation, arguments});
                }
            }
            if (calls.empty()) { return refuse("DOM helper has no source invocation"); }
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
                    if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(operation)) {
                        continue;
                    }
                    if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation)) {
                        // Read the checked cell at this invocation, never bind a
                        // shared helper body to its first caller's SSA values.
                        mapping.map(load.getResult(),
                                    captures[static_cast<unsigned>(load.getIndex())].value());
                    } else if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                        call.operation->getResult(0).replaceAllUsesWith(
                            mapping.lookup(result.getValue()));
                    } else {
                        at.clone(operation, mapping);
                        ++operationCount;
                    }
                }
                call.operation->erase();
            }
            for (ctjs::RootOp root : roots) { root.erase(); }
            closure.erase();
        }
        for (ctjs::CreateCellOp cell : localCells) {
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
    candidate.walk([&](mlir::Operation *) {
        if (!source.step()) { return mlir::WalkResult::interrupt(); }
        ++source.operationCount;
        return mlir::WalkResult::advance();
    });
    auto target = candidate.lookupSymbol<ctjs::FuncOp>(entry);
    ctjs::FuncOp wrapper;
    for (mlir::Operation & operation : candidate.getBody()->getOperations()) {
        auto function = llvm::dyn_cast<ctjs::FuncOp>(operation);
        if (!source.step()) { break; }
        if (!function || !llvm::hasSingleElement(function.getBody()) ||
            ((function == target || functionIndex(function) == 0) &&
             function.getUpvalueCount() != 0) ||
            function->hasAttr("ctjs.skipped") ||
            function.getBody().front().getNumArguments() < ctjs::implicit_arguments ||
            !llvm::all_of(function.getBody().front().getArgumentTypes(),
                          [](mlir::Type type) { return llvm::isa<ctjs::ValueType>(type); })) {
            source.refuse("DOM helper requires complete source functions and an uncaptured entry");
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
    if (source.reason.empty() && wrapper &&
        !host_detail::isInertEntryDeclaration(wrapper, target, [&] { return source.step(); })) {
        source.refuse("DOM entry wrapper contains observable source operations");
    }
    if (source.reason.empty() && source.expand(target, 0, true)) {
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
    for (auto [index, function] : source.functions) {
        (void)index;
        if (function != target && function != wrapper) { function.erase(); }
    }
    return llvm::Error::success();
}
} // namespace ctcompile::ctnative
