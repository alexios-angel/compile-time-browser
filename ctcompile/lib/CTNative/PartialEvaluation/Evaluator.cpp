//===- Evaluator.cpp - Abstract interpreter for ctjs regions --------------===//
//
// The partial evaluator's interpreter (class evaluator in Heap.h). It runs a
// function on known arguments over an abstract heap of objects, Maps, cells
// and closures. Anything it cannot compute exactly (an unknown value reaching
// a branch, a host read, an unproved call) stops evaluation with a reason, so
// a successful run is an exact result.
//
//===----------------------------------------------------------------------===//

#include "Heap.h"

#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/ImmutableCaptures.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative::partial_eval {

value evaluator::fail(llvm::StringRef reason) {
    if (problem.empty()) { problem = reason.str(); }
    return {};
}

value evaluator::allocate(node::kind kind, mlir::Location location) {
    if (state.heap.size() >= maxNodes) { return fail("heap-node budget exhausted"); }
    const unsigned id = static_cast<unsigned>(state.heap.size());
    state.heap.emplace_back(kind, location);
    return value::reference(id);
}

bool evaluator::implicitArgumentsUnobserved(mlir::Block & entry, size_t count) {
    for (unsigned i = 0; i < ctjs::implicit_arguments && i < count; ++i) {
        for (mlir::OpOperand & use : entry.getArgument(i).getUses()) {
            if (!factoryParameterUse(use, i)) {
                fail("callee observes receiver, constructor state or closure identity");
                return false;
            }
        }
    }
    return true;
}

value evaluator::call(ctjs::FuncOp function, llvm::ArrayRef<value> args, unsigned depth) {
    if (depth >= maxDepth) { return fail("call-depth budget exhausted"); }
    if (!function || function.getBody().empty() || !function.isPrivate() ||
        function.getUpvalueCount() != 0) {
        return fail("callee is not a closed capture-free function");
    }
    if (args.size() != function.getBody().front().getNumArguments()) {
        return fail("callee argument count is not exact");
    }
    if (!implicitArgumentsUnobserved(function.getBody().front(), args.size())) { return {}; }
    if (depth != 0 && constructsClosures(function)) {
        return fail("closure construction must remain in its original factory");
    }
    environment env;
    auto result = region(function.getBody(), args, env, depth + 1);
    if (result.tag != completion::kind::returned || result.values.size() != 1) {
        return fail("callee has no evaluated normal return");
    }
    return result.values.front();
}

std::optional<snapshot> evaluator::run(ctjs::FuncOp function, llvm::ArrayRef<value> args) {
    state.result = call(function, args, 0);
    if (state.result.tag == value::kind::unknown && problem.empty()) {
        fail("return depends on an unknown value");
    }
    if (!problem.empty()) { return {}; }
    return std::move(state);
}

/// Interprets one region from its entry block, following cf branches between
/// blocks, until a ctjs.return, scf.yield or scf.condition ends it. Any value
/// that cannot be computed exactly halts evaluation with a reason in `problem`.
evaluator::completion evaluator::region(mlir::Region & body, llvm::ArrayRef<value> incoming,
                                        environment & env, unsigned depth) {
    if (body.empty()) { return halt("empty region"); }
    mlir::Block * block = &body.front();
    llvm::SmallVector<value> args(incoming);
    while (block) {
        if (args.size() != block->getNumArguments()) {
            return halt("region argument count mismatch");
        }
        for (auto [argument, input] : llvm::zip(block->getArguments(), args)) {
            env[argument] = input;
        }
        mlir::Block * next = nullptr;
        for (mlir::Operation & op : *block) {
            if (++state.steps > maxSteps) { return halt("step budget exhausted"); }
            const auto values = [&](mlir::ValueRange operands) {
                llvm::SmallVector<value> result;
                for (mlir::Value operand : operands) { result.push_back(env.lookup(operand)); }
                return result;
            };
            if (auto ret = llvm::dyn_cast<ctjs::ReturnOp>(op)) {
                return {completion::kind::returned, {env.lookup(ret.getValue())}, false};
            }
            if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(op)) {
                return {completion::kind::yielded, values(yield.getOperands()), false};
            }
            if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(op)) {
                auto take = truthy(env.lookup(condition.getCondition()));
                if (!take) { return halt("unknown loop condition"); }
                return {completion::kind::condition, values(condition.getArgs()), *take};
            }
            if (auto branch = llvm::dyn_cast<mlir::cf::BranchOp>(op)) {
                next = branch.getDest();
                args = values(branch.getDestOperands());
                break;
            }
            if (auto branch = llvm::dyn_cast<mlir::cf::CondBranchOp>(op)) {
                const auto take = truthy(env.lookup(branch.getCondition()));
                if (!take) { return halt("unknown branch condition"); }
                next = *take ? branch.getTrueDest() : branch.getFalseDest();
                args = values(*take ? branch.getTrueDestOperands() : branch.getFalseDestOperands());
                break;
            }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
                const auto take = truthy(env.lookup(branch.getCondition()));
                if (!take) { return halt("unknown branch condition"); }
                auto & chosen = *take ? branch.getThenRegion() : branch.getElseRegion();
                completion result{completion::kind::yielded, {}, false};
                if (!chosen.empty()) { result = region(chosen, {}, env, depth); }
                if (result.tag != completion::kind::yielded ||
                    result.values.size() != op.getNumResults()) {
                    return halt("branch does not yield its declared results");
                }
                for (auto [output, v] : llvm::zip(op.getResults(), result.values)) {
                    env[output] = v;
                }
                continue;
            }
            if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(op)) {
                auto carried = values(loop.getInits());
                while (problem.empty()) {
                    auto before = region(loop.getBefore(), carried, env, depth);
                    if (before.tag != completion::kind::condition) {
                        fail("loop lacks condition");
                        break;
                    }
                    if (!before.condition) {
                        if (before.values.size() != op.getNumResults()) {
                            fail("loop result mismatch");
                            break;
                        }
                        for (auto [output, v] : llvm::zip(op.getResults(), before.values)) {
                            env[output] = v;
                        }
                        break;
                    }
                    auto after = region(loop.getAfter(), before.values, env, depth);
                    if (after.tag != completion::kind::yielded) {
                        fail("loop lacks yield");
                        break;
                    }
                    carried = std::move(after.values);
                }
                if (!problem.empty()) { return {}; }
                continue;
            }
            if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(op)) { continue; }
            value result = operation(&op, env, depth);
            if (!problem.empty()) { return {}; }
            if (op.getNumResults() == 1) { env[op.getResult(0)] = result; }
        }
        block = next;
    }
    return halt("region has no supported terminator");
}

value evaluator::operation(mlir::Operation * op, environment & env, unsigned depth) {
    auto * context = module.getContext();
    const auto get = [&](mlir::Value input) { return env.lookup(input); };
    // Lifting to scf introduces dead loop-carried slots initialized with
    // ub.poison. An unknown value is safe to carry, but it cannot control a
    // branch, take part in a computation or escape.
    if (op->getName().getStringRef() == "ub.poison") { return {}; }
    if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(op)) {
        if (!ctjs::isPrimitiveAttr(constant.getValue())) {
            return fail("unsupported constant kind");
        }
        return value::primitive(constant.getValue());
    }
    if (auto constant = llvm::dyn_cast<mlir::arith::ConstantOp>(op)) {
        if (llvm::isa<mlir::IntegerAttr>(constant.getValue())) {
            return value::primitive(constant.getValue());
        }
    }
    if (llvm::isa<mlir::arith::TruncIOp>(op)) {
        auto input = get(op->getOperand(0));
        auto integer = input.tag == value::kind::constant
                           ? llvm::dyn_cast<mlir::IntegerAttr>(input.constant)
                           : mlir::IntegerAttr{};
        auto type = llvm::dyn_cast<mlir::IntegerType>(op->getResult(0).getType());
        if (integer && type) {
            return value::primitive(
                mlir::IntegerAttr::get(type, integer.getValue().trunc(type.getWidth())));
        }
        return fail("unknown control-flow integer conversion");
    }
    if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
        if (load->hasAttr(kNativeMapConstructor)) {
            return {value::kind::mapConstructor, {}, 0, {}};
        }
        // A global loaded only to be the callee of a direct call is left
        // unknown; directCall proves the callee's identity separately.
        if (calleeSlotOnly(load)) { return {}; }
        return fail("host or mutable global read");
    }
    if (llvm::isa<ctjs::CreateCellOp, ctjs::CellGetOp, ctjs::CellSetOp, ctjs::CreateClosureOp>(
            op)) {
        return closureOperation(op, env);
    }
    if (llvm::isa<ctjs::CreateObjectOp>(op)) { return allocate(node::kind::object, op->getLoc()); }
    if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(op)) {
        if (!made->hasAttr(kNativeMapSite) || !made.getArgs().empty() ||
            get(made.getCallee()).tag != value::kind::mapConstructor ||
            made.getNewTarget() != made.getCallee()) {
            return fail("unproved constructor");
        }
        return allocate(node::kind::map, op->getLoc());
    }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
        value object = get(read.getObject()), key = get(read.getKey());
        if (object.tag != value::kind::reference || key.tag != value::kind::constant) {
            return fail("property access outside the evaluated heap");
        }
        auto text = llvm::dyn_cast<ctjs::StringAttr>(key.constant);
        if (!text) { return fail("property key is not a known string"); }
        const auto & target = state.heap[object.node];
        if (target.tag == node::kind::map) {
            if (nativeMapAction(op) == "size") {
                return numeric(context, static_cast<double>(target.entries.size()));
            }
            if (!op->hasAttr(kNativeMapMethod)) { return fail("unproved Map method"); }
            return {value::kind::method, {}, object.node, text.getValue().str()};
        }
        for (const auto & [field, data] : target.entries) {
            if (sameValue(field, key)) { return data; }
        }
        return fail("property read may consult a prototype");
    }
    if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
        value object = get(write.getObject()), key = get(write.getKey()),
              data = get(write.getValue());
        if (!isNode(object, node::kind::object) || key.tag != value::kind::constant ||
            !llvm::isa<ctjs::StringAttr>(key.constant) || !data.known()) {
            return fail("property write outside a known plain object");
        }
        auto & entries = state.heap[object.node].entries;
        for (auto & entry : entries) {
            if (sameValue(entry.first, key)) {
                entry.second = data;
                return {};
            }
        }
        entries.emplace_back(key, data);
        return {};
    }
    if (auto invoked = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
        return directCall(invoked, env, depth);
    }
    if (auto invoked = llvm::dyn_cast<ctjs::CallOp>(op)) {
        value method = get(invoked.getCallee()), receiver = get(invoked.getReceiver());
        const auto action = nativeMapAction(op);
        if (method.tag != value::kind::method || receiver.tag != value::kind::reference ||
            method.node != receiver.node || action.empty() || action != method.method) {
            return fail("unknown call or mismatched receiver");
        }
        auto & entries = state.heap[receiver.node].entries;
        if (action == "clear") {
            entries.clear();
            return value::primitive(ctjs::UndefinedAttr::get(context));
        }
        if (!nativeMapKeyAction(action)) {
            return fail("Map snapshot or callback is not evaluated");
        }
        value key = get(invoked.getArgs().front());
        if (!key.known()) { return fail("unknown Map key"); }
        auto found = llvm::find_if(
            entries, [&](const auto & entry) { return sameValue(entry.first, key, true); });
        if (action == "has") { return boolean(context, found != entries.end()); }
        if (action == "get") {
            return found != entries.end() ? found->second
                                          : value::primitive(ctjs::UndefinedAttr::get(context));
        }
        if (action == "delete") {
            const bool present = found != entries.end();
            if (present) { entries.erase(found); }
            return boolean(context, present);
        }
        value data = get(invoked.getArgs()[1]);
        if (!data.known()) { return fail("unknown Map payload"); }
        if (found != entries.end()) {
            found->second = data;
        } else {
            entries.emplace_back(key, data);
        }
        return receiver;
    }
    value result;
    if (auto binaryOp = llvm::dyn_cast<ctjs::BinaryOp>(op)) {
        result =
            binary(binaryOp.getKind(), get(binaryOp.getLhs()), get(binaryOp.getRhs()), context);
    } else if (auto binaryOp = llvm::dyn_cast<ctjs::BinaryStaticOp>(op)) {
        result = binary(binaryOp.getKind(), get(binaryOp.getLhs()), get(binaryOp.getRhs()), context,
                        true);
    } else if (auto unaryOp = llvm::dyn_cast<ctjs::UnaryOp>(op)) {
        auto input = get(unaryOp.getOperand());
        if (unaryOp.getKind() == ctjs::UnaryKind::TypeOf && isNode(input, node::kind::closure)) {
            result = value::primitive(ctjs::StringAttr::get(context, "function"));
        } else {
            result = unary(unaryOp.getKind(), input, context);
        }
    } else if (auto cmp = llvm::dyn_cast<ctjs::CompareOp>(op)) {
        result = compare(cmp.getKind(), get(cmp.getLhs()), get(cmp.getRhs()), context);
    } else if (auto test = llvm::dyn_cast<ctjs::TruthyOp>(op)) {
        if (auto answer = truthy(get(test.getValue()))) { result = boolean(context, *answer); }
    }
    if (result.tag == value::kind::unknown) {
        return fail(
            ("unsupported or unknown operation `" + op->getName().getStringRef() + "`").str());
    }
    return result;
}

} // namespace ctcompile::ctnative::partial_eval
