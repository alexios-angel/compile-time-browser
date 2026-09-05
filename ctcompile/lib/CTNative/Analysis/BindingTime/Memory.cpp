#include "Analysis.h"
#include "ctcompile/CTNative/Analysis/ImmutableCaptures.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctnative {
namespace binding_time_detail {
fact join(fact left, const fact & right, bool staticControl) {
    if (!staticControl || left.time != BindingTime::Static || right.time != BindingTime::Static) {
        left.time = BindingTime::Dynamic;
    }
    if (left.domain != right.domain) { left.domain = fact::kind::unknown; }
    if (left.literal != right.literal) { left.literal = {}; }
    for (auto * node : right.nodes) {
        if (!llvm::is_contained(left.nodes, node)) { left.nodes.push_back(node); }
    }
    return left;
}
void invalidate(flow & state) {
    for (auto & item : state.heaps) { item.second.dynamic = true; }
}
} // namespace binding_time_detail

BindingTimeAnalysis::Impl::fact BindingTimeAnalysis::Impl::memory(mlir::Operation * op,
                                                                  flow & state, bool control,
                                                                  bool & eligible) {
    using kind = fact::kind;
    const auto get = [&](mlir::Value value) { return state.values.lookup(value); };
    const auto clean = [&](const fact & object) {
        return object.time == BindingTime::Static && !object.nodes.empty() &&
               llvm::all_of(object.nodes, [&](mlir::Operation * id) {
                   auto found = state.heaps.find(id);
                   return found != state.heaps.end() && !found->second.dynamic;
               });
    };
    const auto primitive = [] { return fact{kind::primitive, BindingTime::Static, {}, {}}; };
    if (llvm::isa<ctjs::CreateObjectOp>(op) ||
        (llvm::isa<ctjs::ConstructOp>(op) && op->hasAttr(kNativeMapSite))) {
        fact result{llvm::isa<ctjs::CreateObjectOp>(op) ? kind::object : kind::map,
                    BindingTime::Static,
                    {},
                    {op}};
        state.heaps[op] = {};
        // An allocation under unknown control may or may not exist at runtime.
        if (!control) {
            result.time = BindingTime::Dynamic;
            state.heaps[op].dynamic = true;
        }
        eligible = control;
        return result;
    }
    if (auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(op)) {
        const fact initial = get(cell.getInitial());
        eligible =
            control && immutableCaptureCell(cell, module) && initial.time == BindingTime::Static;
        state.heaps[op].dynamic = !eligible;
        state.heaps[op].fields["value"] = initial;
        return {kind::cell, eligible ? BindingTime::Static : BindingTime::Dynamic, {}, {op}};
    }
    if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(op)) {
        const fact cell = get(read.getCell());
        if (cell.domain != kind::cell || !clean(cell) || cell.nodes.size() != 1) { return {}; }
        fact result = state.heaps[cell.nodes.front()].fields.lookup("value");
        eligible = result.time == BindingTime::Static;
        return result;
    }
    if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(op)) {
        const fact cell = get(write.getCell()), data = get(write.getValue());
        eligible =
            control && cell.domain == kind::cell && clean(cell) && data.time == BindingTime::Static;
        for (auto * id : cell.nodes) {
            state.heaps[id].dynamic |= !eligible;
            state.heaps[id].fields["value"] = data;
        }
        return {};
    }
    if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
        eligible = control && immutableClosureTarget(made, module) &&
                   llvm::all_of(made.getUpvalues(), [&](mlir::Value input) {
                       const fact capture = get(input);
                       return capture.domain == kind::cell && clean(capture);
                   });
        state.heaps[op].dynamic = !eligible;
        return {kind::closure, eligible ? BindingTime::Static : BindingTime::Dynamic, {}, {op}};
    }
    if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
        if (load->hasAttr(kNativeMapConstructor)) {
            eligible = true;
            return {kind::constructor, BindingTime::Static, {}, {}};
        }
        if (llvm::all_of(load.getResult().getUses(), [](mlir::OpOperand & use) {
                return llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                       (llvm::isa<ctjs::CallDirectOp>(use.getOwner()) &&
                        use.getOperandNumber() == 2);
            })) {
            eligible = true;
            return {kind::bookkeeping, BindingTime::Static, {}, {}};
        }
        binding_time_detail::invalidate(state);
        return {};
    }
    if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
        const fact object = get(set.getObject()), key = get(set.getKey()),
                   data = get(set.getValue());
        auto text = llvm::dyn_cast_or_null<ctjs::StringAttr>(key.literal);
        eligible = control && clean(object) && object.domain == kind::object && text &&
                   data.time == BindingTime::Static && text.getValue() != "__proto__" &&
                   text.getValue() != "prototype" && text.getValue() != "constructor";
        for (auto * id : object.nodes) {
            state.heaps[id].dynamic |= !eligible;
            if (text) { state.heaps[id].fields[text.getValue()] = data; }
        }
        if (object.nodes.empty()) { binding_time_detail::invalidate(state); }
        return {};
    }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
        fact object = get(read.getObject()), key = get(read.getKey());
        if (!clean(object) || key.time != BindingTime::Static) { return {}; }
        if (object.domain == kind::map && read->hasAttr(kNativeMapMethod)) {
            object.domain = kind::method;
            eligible = true;
            return object;
        }
        if (object.domain == kind::map && nativeMapAction(op) == "size") {
            eligible = true;
            return primitive();
        }
        auto text = llvm::dyn_cast_or_null<ctjs::StringAttr>(key.literal);
        if (object.domain != kind::object || !text) { return {}; }
        fact result;
        bool first = true;
        for (auto * id : object.nodes) {
            auto found = state.heaps[id].fields.find(text.getValue());
            if (found == state.heaps[id].fields.end()) { return {}; }
            result = first ? found->second : binding_time_detail::join(result, found->second, true);
            first = false;
        }
        eligible = result.time == BindingTime::Static;
        return result;
    }
    if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
        const auto action = nativeMapAction(op);
        fact object = get(call.getReceiver());
        if (action.empty() || object.domain != kind::map || object.nodes.empty()) {
            binding_time_detail::invalidate(state);
            return {};
        }
        eligible = clean(object) && get(call.getCallee()).time == BindingTime::Static &&
                   llvm::all_of(call.getArgs(), [&](mlir::Value arg) {
                       return get(arg).time == BindingTime::Static;
                   });
        const bool writes = action == "set" || action == "delete" || action == "clear";
        if (writes) {
            eligible &= control;
            for (auto * id : object.nodes) {
                auto & target = state.heaps[id];
                target.dynamic |= !eligible;
                if (action == "set" && call.getArgs().size() == 2) {
                    const fact data = get(call.getArgs()[1]);
                    if (target.fields.empty()) {
                        target.contents = data;
                    } else {
                        target.contents = binding_time_detail::join(target.contents, data, true);
                    }
                    target.fields["written"] = data;
                }
                if (action == "clear" && eligible) {
                    target.fields.clear();
                    target.contents = primitive();
                }
            }
        }
        if (!eligible) { return {}; }
        if (action == "set") { return object; }
        if (action == "get") {
            fact result;
            bool first = true;
            for (auto * id : object.nodes) {
                auto data = state.heaps[id].contents;
                result = first ? data : binding_time_detail::join(result, data, true);
                first = false;
            }
            // Contents summarize all stored payloads, not membership at this
            // particular key. A missing or deleted entry yields undefined;
            // do not infer an exact literal or a definite object from payloads.
            result.literal = {};
            if (result.domain != kind::primitive) { result.domain = kind::unknown; }
            return result;
        }
        if (action == "has" || action == "delete" || action == "clear") { return primitive(); }
        // Snapshot iteration is still runtime work in the evaluator.
        eligible = false;
        return {};
    }
    return {};
}
} // namespace ctcompile::ctnative
