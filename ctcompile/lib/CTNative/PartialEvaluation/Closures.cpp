#include "Heap.h"

#include "ctcompile/CTNative/Analysis/ImmutableCaptures.h"

namespace ctcompile::ctnative::partial_eval {

bool factoryParameterUse(mlir::OpOperand & use, unsigned parameter) {
    if (llvm::isa<ctjs::RootOp>(use.getOwner())) { return true; }
    if (!llvm::isa<ctjs::CreateClosureOp>(use.getOwner())) { return false; }
    return (parameter == 0 && use.getOperandNumber() == 1) ||
           (parameter == 2 && use.getOperandNumber() == 0);
}

value evaluator::closureOperation(mlir::Operation * op, environment & env) {
    const auto get = [&](mlir::Value input) { return env.lookup(input); };
    const auto known = [](value input) {
        return input.tag == value::kind::constant || input.tag == value::kind::reference;
    };
    if (auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(op)) {
        if (!immutableCaptureCell(cell, module)) {
            return fail("capture cell is mutable or escapes");
        }
        value initial = get(cell.getInitial());
        if (!known(initial)) { return fail("capture cell has an unknown initial value"); }
        value result = allocate(node::kind::cell, op->getLoc());
        if (result.tag != value::kind::reference) { return result; }
        auto & item = state.heap[result.node];
        item.contents = initial;
        item.requiresWrite = static_cast<bool>(captureCellWrite(cell));
        return result;
    }
    if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(op)) {
        value cell = get(read.getCell());
        if (cell.tag != value::kind::reference || state.heap[cell.node].tag != node::kind::cell) {
            return fail("cell read is outside the evaluated heap");
        }
        return state.heap[cell.node].contents;
    }
    if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(op)) {
        value cell = get(write.getCell()), data = get(write.getValue());
        if (cell.tag != value::kind::reference || state.heap[cell.node].tag != node::kind::cell ||
            !known(data)) {
            return fail("cell initialization is not known");
        }
        auto & item = state.heap[cell.node];
        if (item.assigned) { return fail("capture cell is assigned more than once"); }
        item.contents = data;
        item.assigned = true;
        return {};
    }
    auto made = llvm::cast<ctjs::CreateClosureOp>(op);
    if (!immutableClosureTarget(made, module)) {
        return fail("closure target or immutable capture contract is unproved");
    }
    llvm::SmallVector<value> captures;
    for (mlir::Value operand : made.getUpvalues()) {
        value capture = get(operand);
        if (capture.tag != value::kind::reference ||
            state.heap[capture.node].tag != node::kind::cell) {
            return fail("closure capture is not a known local cell");
        }
        captures.push_back(capture);
    }
    value result = allocate(node::kind::closure, op->getLoc());
    if (result.tag != value::kind::reference) { return result; }
    auto & item = state.heap[result.node];
    item.function = static_cast<unsigned>(made.getFunction());
    item.captures = std::move(captures);
    return result;
}

} // namespace ctcompile::ctnative::partial_eval
