#include "Heap.h"

#include "mlir/IR/Builders.h"

#include <functional>

namespace ctcompile::ctnative::partial_eval {

std::optional<std::vector<unsigned>> reachable(const snapshot & state) {
    std::vector<unsigned> live;
    std::vector<unsigned char> colors(state.heap.size(), 0);
    std::function<bool(value)> visit = [&](value input) {
        if (input.tag == value::kind::constant) {
            return llvm::isa<ctjs::NumberAttr, ctjs::BooleanAttr, ctjs::NullAttr,
                             ctjs::UndefinedAttr, ctjs::StringAttr>(input.constant);
        }
        if (input.tag != value::kind::reference || input.node >= state.heap.size()) {
            return false;
        }
        auto & color = colors[input.node];
        if (color == 1) { return false; }
        if (color == 2) { return true; }
        color = 1;
        live.push_back(input.node);
        for (const auto & [key, data] : state.heap[input.node].entries) {
            if (!visit(key) || !visit(data)) { return false; }
        }
        color = 2;
        return true;
    };
    if (!visit(state.result)) { return {}; }
    return live;
}

void residualize(ctjs::FuncOp function, const snapshot & state, llvm::ArrayRef<unsigned> live) {
    auto * context = function.getContext();
    const auto valueType = ctjs::ValueType::get(context);
    mlir::Region replacement;
    auto * block = new mlir::Block;
    replacement.push_back(block);
    for (mlir::BlockArgument arg : function.getBody().front().getArguments()) {
        block->addArgument(arg.getType(), arg.getLoc());
    }
    mlir::OpBuilder at(context);
    at.setInsertionPointToEnd(block);
    const auto create = [&](llvm::StringRef name, mlir::Location location,
                            mlir::ValueRange operands,
                            llvm::ArrayRef<mlir::NamedAttribute> attributes, bool result = true) {
        mlir::OperationState op(location, name);
        op.addOperands(operands);
        op.addAttributes(attributes);
        if (result) { op.addTypes(valueType); }
        return at.create(op);
    };
    const auto constant = [&](mlir::Attribute value, mlir::Location location) {
        return create("ctjs.constant", location, {}, {at.getNamedAttr("value", value)})
            ->getResult(0);
    };
    llvm::DenseMap<unsigned, mlir::Value> objects;
    // Every allocation precedes every edge. This preserves sharing without
    // depending on the traversal order used to discover the graph.
    for (unsigned id : live) {
        const auto & item = state.heap[id];
        mlir::Operation * made;
        if (item.tag == node::kind::object) {
            made = create("ctjs.create_object", item.location, {}, {});
        } else {
            auto constructor = create("ctjs.load_global", item.location, {},
                                      {at.getNamedAttr("name", at.getStringAttr("Map"))})
                                   ->getResult(0);
            made = create("ctjs.construct", item.location, {constructor, constructor}, {});
        }
        objects[id] = made->getResult(0);
    }
    const auto materialize = [&](value input, mlir::Location location) {
        return input.tag == value::kind::reference ? objects.lookup(input.node)
                                                   : constant(input.constant, location);
    };
    for (unsigned id : live) {
        const auto & item = state.heap[id];
        mlir::Value setter;
        if (item.tag == node::kind::map && !item.entries.empty()) {
            auto key = constant(ctjs::StringAttr::get(context, "set"), item.location);
            setter =
                create("ctjs.get_property", item.location, {objects[id], key}, {})->getResult(0);
        }
        for (const auto & [key, data] : item.entries) {
            auto k = materialize(key, item.location), v = materialize(data, item.location);
            if (item.tag == node::kind::object) {
                create("ctjs.set_property", item.location, {objects[id], k, v}, {}, false);
            } else {
                create("ctjs.call", item.location, {setter, objects[id], k, v}, {});
            }
        }
    }
    create("ctjs.return", function.getLoc(), {materialize(state.result, function.getLoc())}, {},
           false);
    function.getBody().takeBody(replacement);
    function->setAttr(
        "ctnative.partial_evaluated",
        at.getDictionaryAttr(
            {at.getNamedAttr("steps", at.getI64IntegerAttr(state.steps)),
             at.getNamedAttr("evaluated_nodes",
                             at.getI64IntegerAttr(static_cast<int64_t>(state.heap.size()))),
             at.getNamedAttr("residual_nodes",
                             at.getI64IntegerAttr(static_cast<int64_t>(live.size())))}));
}

} // namespace ctcompile::ctnative::partial_eval
