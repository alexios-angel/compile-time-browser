#include "Heap.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"

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
        const auto & item = state.heap[input.node];
        if (item.tag == node::kind::cell &&
            ((item.requiresWrite && !item.assigned) || !visit(item.contents))) {
            return false;
        }
        for (value capture : item.captures) {
            if (!visit(capture)) { return false; }
        }
        for (const auto & [key, data] : item.entries) {
            if (!visit(key) || !visit(data)) { return false; }
        }
        color = 2;
        return true;
    };
    if (!state.boundary) {
        if (!visit(state.result)) { return {}; }
    } else {
        for (const auto & [original, root] : state.bindings) {
            if (root.tag == value::kind::mapConstructor) { continue; }
            if (root.tag == value::kind::constant &&
                llvm::isa<mlir::IntegerType>(original.getType()) &&
                llvm::isa<mlir::IntegerAttr, ctjs::BooleanAttr>(root.constant)) {
                continue;
            }
            if (!visit(root.tag == value::kind::method ? value::reference(root.node) : root)) {
                return {};
            }
        }
    }
    return live;
}

namespace {

class graphEmitter {
public:
    graphEmitter(mlir::OpBuilder & at, const snapshot & state, llvm::ArrayRef<unsigned> live,
                 mlir::Value enclosingClosure)
        : at(at), context(at.getContext()), state(state), enclosingClosure(enclosingClosure) {
        // Every allocation precedes every edge. Traversal order must not
        // duplicate a shared child or change an object-valued Map key.
        for (unsigned id : live) {
            const auto & item = state.heap[id];
            if (item.tag == node::kind::cell || item.tag == node::kind::closure) { continue; }
            mlir::Operation * made;
            if (item.tag == node::kind::object) {
                made = create("ctjs.create_object", item.location, {}, {});
            } else {
                auto constructor = mapConstructor(item.location);
                made = create("ctjs.construct", item.location, {constructor, constructor}, {});
            }
            objects[id] = made->getResult(0);
        }
        for (unsigned id : live) { (void)allocateCapture(id); }
        for (unsigned id : live) {
            const auto & item = state.heap[id];
            if (item.tag == node::kind::cell || item.tag == node::kind::closure) { continue; }
            mlir::Value setter;
            if (item.tag == node::kind::map && !item.entries.empty()) {
                setter = method(id, "set", item.location);
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
    }

    mlir::Value materialize(value input, mlir::Location location, mlir::Type type = {}) {
        if (input.tag == value::kind::reference) { return objects.lookup(input.node); }
        if (input.tag == value::kind::mapConstructor) { return mapConstructor(location); }
        if (input.tag == value::kind::method) { return method(input.node, input.method, location); }
        if (auto integer = llvm::dyn_cast_or_null<mlir::IntegerType>(type)) {
            mlir::IntegerAttr attribute;
            if (auto original = llvm::dyn_cast<mlir::IntegerAttr>(input.constant)) {
                attribute = mlir::IntegerAttr::get(
                    integer, original.getValue().zextOrTrunc(integer.getWidth()));
            } else {
                attribute = mlir::IntegerAttr::get(
                    integer, llvm::cast<ctjs::BooleanAttr>(input.constant).getValue());
            }
            return mlir::arith::ConstantOp::create(at, location, attribute);
        }
        return constant(input.constant, location);
    }

private:
    mlir::OpBuilder & at;
    mlir::MLIRContext * context;
    llvm::DenseMap<unsigned, mlir::Value> objects;
    const snapshot & state;
    mlir::Value enclosingClosure;

    mlir::Value allocateCapture(unsigned id) {
        if (auto existing = objects.lookup(id)) { return existing; }
        const auto & item = state.heap[id];
        const auto captured = [&](value input) {
            return input.tag == value::kind::reference ? allocateCapture(input.node)
                                                       : constant(input.constant, item.location);
        };
        mlir::Operation * made;
        if (item.tag == node::kind::cell) {
            made = create("ctjs.create_cell", item.location, {captured(item.contents)}, {});
        } else {
            auto receiver = constant(ctjs::UndefinedAttr::get(context), item.location);
            llvm::SmallVector<mlir::Value> operands{enclosingClosure, receiver};
            for (value capture : item.captures) { operands.push_back(captured(capture)); }
            made = create("ctjs.create_closure", item.location, operands,
                          {at.getNamedAttr("function", at.getI32IntegerAttr(
                                                           static_cast<int32_t>(item.function)))});
        }
        objects[id] = made->getResult(0);
        return made->getResult(0);
    }

    mlir::Operation * create(llvm::StringRef name, mlir::Location location,
                             mlir::ValueRange operands,
                             llvm::ArrayRef<mlir::NamedAttribute> attributes, bool result = true) {
        mlir::OperationState op(location, name);
        op.addOperands(operands);
        op.addAttributes(attributes);
        if (result) { op.addTypes(ctjs::ValueType::get(context)); }
        return at.create(op);
    }
    mlir::Value constant(mlir::Attribute value, mlir::Location location) {
        return create("ctjs.constant", location, {}, {at.getNamedAttr("value", value)})
            ->getResult(0);
    }
    mlir::Value mapConstructor(mlir::Location location) {
        return create("ctjs.load_global", location, {},
                      {at.getNamedAttr("name", at.getStringAttr("Map"))})
            ->getResult(0);
    }
    mlir::Value method(unsigned id, llvm::StringRef name, mlir::Location location) {
        auto key = constant(ctjs::StringAttr::get(context, name), location);
        return create("ctjs.get_property", location, {objects[id], key}, {})->getResult(0);
    }
};

void residualizePrefix(ctjs::FuncOp function, const snapshot & state, llvm::ArrayRef<unsigned> live,
                       mlir::Region & replacement) {
    mlir::IRMapping mapping;
    function.getBody().cloneInto(&replacement, mapping);
    auto * boundary = mapping.lookup(state.boundary);
    mlir::OpBuilder at(boundary);
    graphEmitter graph(at, state, live, replacement.front().getArgument(2));
    for (const auto & [original, value] : state.bindings) {
        auto materialized = graph.materialize(value, original.getLoc(), original.getType());
        mapping.lookup(original).replaceAllUsesWith(materialized);
        mapping.map(original, materialized);
    }
    llvm::SmallVector<mlir::Operation *> removed;
    for (mlir::Operation & op : function.getBody().front()) {
        if (&op == state.boundary) { break; }
        if (retainedPrefixScaffolding(&op)) { continue; }
        auto * originalClone = mapping.lookup(&op);
        if (auto root = llvm::dyn_cast<ctjs::RootOp>(op)) {
            // Reestablish surviving roots after reconstruction. A root of an
            // eliminated temporary has no corresponding runtime allocation.
            auto * definition = root.getValue().getDefiningOp();
            if (!definition || retainedPrefixScaffolding(definition) ||
                llvm::any_of(state.bindings, [&](const auto & binding) {
                    return binding.first == root.getValue();
                })) {
                at.clone(op, mapping);
            }
        } else if (llvm::isa<ctjs::FrameExitOp>(op)) {
            at.clone(op, mapping);
        }
        removed.push_back(originalClone);
    }
    for (mlir::Operation * op : llvm::reverse(removed)) { op->erase(); }
}

} // namespace

void residualize(ctjs::FuncOp function, const snapshot & state, llvm::ArrayRef<unsigned> live) {
    auto * context = function.getContext();
    mlir::Region replacement;
    mlir::OpBuilder at(context);
    if (state.boundary) {
        residualizePrefix(function, state, live, replacement);
    } else {
        auto * block = new mlir::Block;
        replacement.push_back(block);
        for (mlir::BlockArgument arg : function.getBody().front().getArguments()) {
            block->addArgument(arg.getType(), arg.getLoc());
        }
        at.setInsertionPointToEnd(block);
        graphEmitter graph(at, state, live, replacement.front().getArgument(2));
        ctjs::ReturnOp::create(at, function.getLoc(),
                               graph.materialize(state.result, function.getLoc()));
    }
    llvm::SmallVector<mlir::NamedAttribute> statistics{
        at.getNamedAttr("steps", at.getI64IntegerAttr(state.steps)),
        at.getNamedAttr("evaluated_nodes",
                        at.getI64IntegerAttr(static_cast<int64_t>(state.heap.size()))),
        at.getNamedAttr("residual_nodes", at.getI64IntegerAttr(static_cast<int64_t>(live.size())))};
    if (state.boundary) {
        statistics.push_back(at.getNamedAttr("mode", at.getStringAttr("prefix")));
        statistics.push_back(at.getNamedAttr(
            "boundary", at.getStringAttr(state.boundary->getName().getStringRef())));
        statistics.push_back(at.getNamedAttr(
            "live_values", at.getI64IntegerAttr(static_cast<int64_t>(state.bindings.size()))));
    }
    function.getBody().takeBody(replacement);
    function->setAttr("ctnative.partial_evaluated", at.getDictionaryAttr(statistics));
}

} // namespace ctcompile::ctnative::partial_eval
