//===- RuntimeOps.cpp - Lower runtime-helper ops --------------------------===//
//
// lowering::convertRuntime: lowers ctjs runtime operations (calls, closures,
// property writes, try handlers, throws, returns, module cells) to EmitC calls
// into the ctbrowser ct_aot_* helpers, for the VM-backed backend where every
// JavaScript value is a NaN-boxed `uint64_t`.
//
// Each helper's contract, its "ABI entry", is in
// ctbrowser/include/ctbrowser/aot/helpers/*.def. A helper reports failure in
// one of two ways. A helper returning a ct_aot_status is tested after the
// call and gets an exception edge (status_call). A helper that returns its
// result directly fails by raising an uncatchable engine fault, which the
// caller polls with ct_aot_failed at a loop back edge, so it gets no edge here.
//
//===----------------------------------------------------------------------===//
#include "Lowering.h"
#include "ctcompile/CTJS/Lowering/RuntimeHelpers.hpp"

namespace ctcompile::ctjs::emitc_detail {

bool lowering::convertRuntime(mlir::Operation & op, mlir::OpBuilder & build,
                              mlir::IRMapping & mapping, compiled_entry & scope) {
    const mlir::Location where = op.getLoc();
    const mlir::Type value = scope.value;
    const mlir::Value receiver = scope.receiver;
    const mlir::Value constructing = scope.constructing;
    const mlir::Value out = scope.out;
    const mlir::Type status = scope.status;

    // A call. ct_aot_call takes `const uint64_t *argv`, a contiguous run, and
    // the arguments are rooted in slots of their own, which are not adjacent.
    // So a run was reserved for this site and the arguments are copied into
    // it here, immediately before the call. In the frame, not in a C++ array:
    // ct_aot_call is a safepoint that runs arbitrary user JavaScript before
    // it reads them, and an array of locals is invisible to a precise
    // collector.
    //
    // The span is fetched once per store and again for the pointer, because
    // context::call resizes context::registers_ and ct_aot_slots' pointer is
    // valid only until the next safepoint. Nothing between these stores is a
    // safepoint, so one fetch would do, but this backend has no analysis to
    // justify hoisting it.
    //
    // ctjs.call_direct is the same call in this backend. The resolver's
    // symbol is for the native backend; here ct_aot_call dispatches on the
    // callee value it kept, exactly as for ctjs.call, and its new.target
    // operand is the undefined constant body_is_supported requires, which is
    // what the runtime gives a plain call anyway.
    if (const auto call = callView(&op)) {
        const unsigned base = park_window(scope, build, where, mapping, op);
        const mlir::Value argv = window_pointer(scope, build, where, base);

        // `key` and `site` are ct_aot_call's diagnostic arguments, and the
        // implementation ignores both. `key` names the callee in the
        // message op::call_computed would produce; `site` carries a
        // backwards scan of bytecode that an AOT frame does not have.
        // Passing undefined and nullptr says this code has neither.
        mapping.map(op.getResult(0),
                    status_call(scope, build, where, callee("ct_aot_call"),
                                {scope.frame, mapping.lookup(call->callee),
                                 mapping.lookup(call->receiver), argv,
                                 u32_literal(build, where, call->args.size()),
                                 undefined(build, where, value),
                                 literal(build, where,
                                         pointer_to(build.getContext(),
                                                    "const ctbrowser::aot::ct_aot_site"),
                                         "nullptr")},
                                value));
        return true;
    }

    // Reading and writing a captured binding, each a pair of calls.
    if (auto load = mlir::dyn_cast<LoadUpvalueOp>(op)) {
        mapping.map(load.getResult(), cell_of_upvalue(scope, build, where, load.getIndex(),
                                                      /*read=*/true, mlir::Value{}));
        return true;
    }
    if (auto store = mlir::dyn_cast<StoreUpvalueOp>(op)) {
        (void)cell_of_upvalue(scope, build, where, store.getIndex(), /*read=*/false,
                              mapping.lookup(store.getValue()));
        return true;
    }

    // Building a closure. ct_aot_make_closure reports failure by raising, not
    // by a status, so there is no exception edge: its three failures (the
    // allocation ceiling, no program to take the function from, and a
    // mismatched index or count) all raise. It is a safepoint, so the result
    // is parked like every other value.
    //
    // The upvalues go in parallel with the target's descriptors, as the
    // importer built them and as the helper reads them. Packed, they would
    // silently capture the wrong bindings.
    if (auto made = mlir::dyn_cast<CreateClosureOp>(op)) {
        const auto upvalues = made.getUpvalues();
        const unsigned base = park_window(scope, build, where, mapping, op);
        mapping.map(made.getResult(),
                    opaque_call(build, where, value, "ct_aot_make_closure",
                                {scope.frame, mapping.lookup(made.getEnclosingClosure()),
                                 u32_literal(build, where, made.getFunction()),
                                 window_pointer(scope, build, where, base),
                                 u32_literal(build, where, upvalues.size()),
                                 mapping.lookup(made.getEnclosingThis())}));
        return true;
    }

    // A property write. Its inline cache is nullptr for the same reason as
    // ctjs.get_property's: ct_aot_ic is forward-declared and nothing can
    // allocate one.
    //
    // No test covers the status edge: nothing in the differential fixture
    // makes a property write fail (that needs a throwing setter or a proxy
    // trap), so the emitted failure branch is never taken. The write itself
    // is covered.
    if (auto write = mlir::dyn_cast<SetPropertyOp>(op)) {
        // An object literal's definition (op::define_data) is not an
        // assignment.
        if (write->hasAttr(defineOwnMarker)) {
            status_call_void(scope, build, where, callee("ct_aot_define_data"),
                             {scope.frame, mapping.lookup(write.getObject()),
                              mapping.lookup(write.getKey()), mapping.lookup(write.getValue())});
            return true;
        }
        status_call_void(
            scope, build, where, callee("ct_aot_set_index"),
            {scope.frame, mapping.lookup(write.getObject()), mapping.lookup(write.getKey()),
             mapping.lookup(write.getValue()),
             literal(build, where, pointer_to(build.getContext(), "ctbrowser::aot::ct_aot_ic"),
                     "nullptr")});
        return true;
    }

    // An array literal. It allocates and reports failure only by raising:
    // allocate() raises past the ceiling and still returns a well-formed
    // object, so there is no status to test. It is a safepoint, so its result
    // is parked like every other value.
    if (auto made = mlir::dyn_cast<CreateArrayOp>(op)) {
        // `reserve_hint` is only a hint: an array that ignores it is merely
        // slower. The elements the operation carries
        // are appended after, which is also how the bytecode builds one:
        // new_array then one append per element.
        const auto elements = made.getElements();
        const mlir::Value array =
            opaque_call(build, where, value, "ct_aot_new_array",
                        {scope.frame, u32_literal(build, where, elements.size())});
        for (const mlir::Value element : elements) {
            opaque_call(build, where, {}, "ct_aot_append",
                        {scope.frame, array, mapping.lookup(element)});
        }
        mapping.map(made.getResult(), array);
        return true;
    }
    if (auto opened = mlir::dyn_cast<PushHandlerOp>(op)) {
        // The pad id is this operation's own slot number, which makes it
        // unique within the function without a second counter.
        // ct_aot_catch_land hands it back to say which handler fired; with
        // one region per function, nothing reads it yet.
        //
        // The $handler edge is dropped here on purpose. It exists in the ctjs
        // CFG so the pad is reachable and its arguments dominate; the edge the
        // emitted code takes is ctjs.check's, at the throw site, because that
        // is where the live register file is.
        const unsigned slot = scope.argument_windows.lookup(&op);
        opaque_call(
            build, where, {}, "ct_aot_handler_push",
            {scope.frame, u32_literal(build, where, slot), u32_literal(build, where, slot)});
        llvm::SmallVector<mlir::Value> onward;
        for (const mlir::Value each : opened.getBodyOperands()) {
            onward.push_back(mapping.lookup(each));
        }
        mlir::cf::BranchOp::create(build, where, mapping.lookup(opened.getBody()), onward);
        return true;
    }
    if (mlir::isa<PopHandlerOp>(op)) {
        opaque_call(build, where, {}, "ct_aot_handler_pop", {scope.frame});
        return true;
    }
    if (auto guarded = mlir::dyn_cast<CheckOp>(op)) {
        // A plain branch. The caught edge was already emitted, once per
        // fallible call in this block, by caught_or_failure, the only place
        // the status exists to test.
        llvm::SmallVector<mlir::Value> onward;
        for (const mlir::Value each : guarded.getContOperands()) {
            onward.push_back(mapping.lookup(each));
        }
        mlir::cf::BranchOp::create(build, where, mapping.lookup(guarded.getCont()), onward);
        return true;
    }
    if (auto landed = mlir::dyn_cast<CatchLandOp>(op)) {
        // Reads back what the unwinder wrote and clears the pad marker, so it
        // must run exactly once and only on a caught edge; the importer
        // enforces that by refusing a pad reachable any other way.
        const auto u32 = opaque(build.getContext(), "uint32_t");
        auto slot = ec::VariableOp::create(build, where, ec::LValueType::get(value),
                                           ec::OpaqueAttr::get(build.getContext(), ""));
        auto address =
            ec::AddressOfOp::create(build, where, ec::PointerType::get(value), slot.getResult());
        mapping.map(landed.getPad(), opaque_call(build, where, u32, "ct_aot_catch_land",
                                                 {scope.frame, address.getResult()}));
        mapping.map(landed.getThrown(),
                    ec::LoadOp::create(build, where, value, slot.getResult()).getResult());
        return true;
    }
    // The arriving argument window, which is not this frame's slots.
    //
    // ct_aot_enter puts this frame's registers above the caller's window,
    // and the entry prologue reads argv only for the declared parameters,
    // so an extra argument is reachable from neither. ct_aot_args and
    // ct_aot_argc answer for the frame, and both operations take what they
    // need from there rather than from operands the importer could not
    // build.
    if (mlir::isa<MakeArgumentsOp, GatherRestOp>(op)) {
        const auto u32 = opaque(build.getContext(), "uint32_t");
        const mlir::Value window = opaque_call(
            build, where, pointer_to(build.getContext(), "uint64_t"), "ct_aot_args", {scope.frame});
        const mlir::Value count = opaque_call(build, where, u32, "ct_aot_argc", {scope.frame});
        // Both return the array rather than a status, so they fail only by
        // raising and get no edge. Both allocate, so the result is parked
        // like every other value.
        if (auto rest = mlir::dyn_cast<GatherRestOp>(op)) {
            mapping.map(rest.getResult(), opaque_call(build, where, value, "ct_aot_gather_rest",
                                                      {scope.frame, window, count,
                                                       u32_literal(build, where, rest.getFrom())}));
            return true;
        }
        auto made = mlir::cast<MakeArgumentsOp>(op);
        mapping.map(made.getResult(), opaque_call(build, where, value, "ct_aot_make_arguments",
                                                  {scope.frame, window, count}));
        return true;
    }
    if (auto gone = mlir::dyn_cast<DeleteNamedOp>(op)) {
        // Two calls, as for ctjs.define_accessor: the helper wants an interned
        // name record rather than characters. The length travels beside the
        // text because a property name is bytes, and one containing a zero
        // byte is legal JavaScript that strlen stops at.
        const mlir::Value interned = intern(build, where, gone.getName());
        opaque_call(build, where, {}, "ct_aot_delete_prop",
                    {scope.frame, mapping.lookup(gone.getObject()), interned});
        return true;
    }
    // ---- the four module operations ------------------------------
    //
    // Three of them take their names as bytes and a length rather than a
    // NUL-terminated string, for ct_aot_global_get's reason: a specifier or
    // an export name is bytes, and one containing a zero byte is a string
    // strlen stops at. `default` is an export name the compiler synthesises
    // and the rest come out of the source, so the length is always known
    // here and never worth a strlen.
    if (auto imported = mlir::dyn_cast<ModuleImportCellOp>(op)) {
        // Returns the cell rather than a status, so there is no exception
        // edge: a missing module and a missing export both raise uncatchable
        // engine faults. Not a safepoint either: two flat_map lookups
        // allocate nothing.
        const auto [specifier, specifier_length] =
            bytes_and_length(build, where, imported.getSpecifier());
        const auto [exported, exported_length] =
            bytes_and_length(build, where, imported.getExportName());
        mapping.map(
            imported.getResult(),
            opaque_call(build, where, value, "ct_aot_module_import_cell",
                        {scope.frame, specifier, specifier_length, exported, exported_length}));
        return true;
    }
    if (auto published = mlir::dyn_cast<ModuleExportCellOp>(op)) {
        // The only seeded status_call in the file. $current is the
        // destination register's value on entry, and the helper hands it
        // straight back when no module is being evaluated. That expresses
        // op::bind_export's conditional write without a "no write" status,
        // which ct_aot_status does not have. See status_call.
        const auto [name, length] = bytes_and_length(build, where, published.getName());
        mapping.map(published.getResult(),
                    status_call(scope, build, where, callee("ct_aot_module_export_cell"),
                                {scope.frame, name, length}, value,
                                mapping.lookup(published.getCurrent())));
        return true;
    }
    if (auto space = mlir::dyn_cast<ModuleNamespaceOp>(op)) {
        // Fails only by raising, like the import helper, but unlike it is a
        // safepoint: the namespace object and one native getter per export
        // are allocated here. The result is parked like every other value.
        const auto [specifier, length] = bytes_and_length(build, where, space.getSpecifier());
        mapping.map(space.getResult(), opaque_call(build, where, value, "ct_aot_module_namespace",
                                                   {scope.frame, specifier, length}));
        return true;
    }
    if (auto accessor = mlir::dyn_cast<DefineAccessorOp>(op)) {
        // Two calls, which is why this is not a CTJS_RuntimeOp. The helper
        // wants an interned name record rather than characters, so the name
        // is interned first; the pool is immortal and hash-indexed, so the
        // repeat cost is a lookup.
        //
        // The length is emitted beside the text because a property name is
        // bytes, and one containing a zero byte is legal JavaScript that
        // strlen would stop at.
        const mlir::Value interned = intern(build, where, accessor.getName());
        opaque_call(build, where, {}, "ct_aot_define_accessor",
                    {scope.frame, mapping.lookup(accessor.getTarget()), interned,
                     mapping.lookup(accessor.getGetter()), mapping.lookup(accessor.getSetter())});
        return true;
    }
    // The spread calls need no window. Their arguments arrive as one
    // already-rooted array rather than as a run of values, so there is
    // nothing to park and no argc to pass, which is the entire difference
    // from the ctjs.call branch above and the ctjs.construct branch below.
    if (auto spread = mlir::dyn_cast<CallSpreadOp>(op)) {
        mapping.map(spread.getResult(),
                    status_call(scope, build, where, callee("ct_aot_call_spread"),
                                {scope.frame, mapping.lookup(spread.getCallee()),
                                 mapping.lookup(spread.getArgs()),
                                 mapping.lookup(spread.getReceiver()), scope.entry_site},
                                value));
        return true;
    }
    if (auto spread = mlir::dyn_cast<ConstructSpreadOp>(op)) {
        mapping.map(spread.getResult(),
                    status_call(scope, build, where, callee("ct_aot_construct_spread"),
                                {scope.frame, mapping.lookup(spread.getCallee()),
                                 mapping.lookup(spread.getArgs()), scope.entry_site},
                                value));
        return true;
    }
    if (auto asks = mlir::dyn_cast<HasPropertyOp>(op)) {
        // Its out-parameter is a uint32_t boolean, not a value: the same
        // shape as ct_aot_loose_equals, and boxed the same way.
        const auto u32 = opaque(build.getContext(), "uint32_t");
        const mlir::Value answered = status_call(
            scope, build, where, callee("ct_aot_has_property"),
            {scope.frame, mapping.lookup(asks.getObject()), mapping.lookup(asks.getKey())}, u32);
        mapping.map(asks.getResult(),
                    box(build, where, value, shim::box_bool, nonzero(build, where, answered)));
        return true;
    }
    if (auto is_a = mlir::dyn_cast<InstanceOfOp>(op)) {
        // Returns its boolean rather than a status, so there is no
        // out-parameter and no exception edge; it fails only by raising. It
        // is still a safepoint.
        const auto u32 = opaque(build.getContext(), "uint32_t");
        const mlir::Value answered = opaque_call(
            build, where, u32, "ct_aot_instance_of",
            {scope.frame, mapping.lookup(is_a.getObject()), mapping.lookup(is_a.getConstructor())});
        mapping.map(is_a.getResult(), nonzero(build, where, answered));
        return true;
    }
    if (auto boxed = mlir::dyn_cast<FromBoolOp>(op)) {
        mapping.map(boxed.getResult(),
                    box(build, where, value, shim::box_bool, mapping.lookup(boxed.getBit())));
        return true;
    }

    // A throw never comes back ok. ct_aot_throw returns CAUGHT, UNWOUND or
    // FAILED and never CT_AOT_OK, because the throw completes inside the
    // callee: by the time it returns there is no exception in flight, only a
    // shorter frame stack. So there is no continuation: the operation is a
    // terminator, and this branches straight to the shared failure path,
    // which handles the conditional ct_aot_leave.
    //
    // No test pins the conditional ct_aot_leave on that path. Making it
    // unconditional would be harmless: leave truncates to this frame's
    // recorded index, so a second call after a failure is a no-op.
    //
    // This does not go through caught_or_failure, and failure_path does not
    // test for CT_AOT_CAUGHT (see Status.cpp). A throw caught by a handler
    // this same frame pushed therefore returns that status to the caller
    // rather than branching to the pad.
    if (auto thrown = mlir::dyn_cast<ThrowOp>(op)) {
        const mlir::Value answered = opaque_call(build, where, status, "ct_aot_throw",
                                                 {scope.frame, mapping.lookup(thrown.getValue())});
        mlir::cf::BranchOp::create(build, where, failure_path(scope, build, where),
                                   mlir::ValueRange{answered});
        return true;
    }

    // `new callee(...)`. The same contiguous argument window a call needs,
    // for the same reason: ct_aot_construct is a safepoint that runs
    // arbitrary user JavaScript - the field initialisers and then the body -
    // before it is done with them.
    //
    // Its `site` is the entry's own, unlike ct_aot_call's, which the
    // implementation ignores: construct uses it to name this function in the
    // TypeError a `new` on a non-constructor throws.
    //
    // new.target is not passed, because the ABI has nowhere to put it:
    // ct_aot_construct sets it from the callee itself, which is what
    // op::construct does with `fresh.new_target = callee`.
    if (auto built = mlir::dyn_cast<ConstructOp>(op)) {
        const auto arguments = built.getArgs();
        const unsigned base = park_window(scope, build, where, mapping, op);
        mapping.map(built.getResult(),
                    status_call(scope, build, where, callee("ct_aot_construct"),
                                {scope.frame, mapping.lookup(built.getCallee()),
                                 window_pointer(scope, build, where, base),
                                 u32_literal(build, where, arguments.size()), scope.entry_site},
                                value));
        return true;
    }

    if (auto returned = mlir::dyn_cast<ReturnOp>(op)) {
        // Normalize first: a constructor returning a primitive carries its
        // receiver instead. The escape hook must root that actual result
        // while removing the frame and excluding its dead register window.
        const mlir::Value produced = returned.getValue() ? mapping.lookup(returned.getValue())
                                                         : undefined(build, where, value);
        const mlir::Value result = opaque_call(build, where, value, "ct_aot_return_value",
                                               {produced, receiver, constructing});
        opaque_call(build, where, {}, "ct_aot_leave_return", {scope.frame, result});
        // Write `out[0] = ...`, not `*out = ...`: with
        // --declare-variables-at-top, LLVM 23's C++ emitter rejects an
        // assignment through emitc.dereference ("result variable for the
        // operation has not been declared") but accepts the same store
        // through emitc.subscript.
        const mlir::Value slot =
            ec::ConstantOp::create(build, where, build.getI32Type(), build.getI32IntegerAttr(0));
        auto destination = ec::SubscriptOp::create(build, where, ec::LValueType::get(value), out,
                                                   mlir::ValueRange{slot});
        ec::AssignOp::create(build, where, destination, result);
        ec::ReturnOp::create(build, where, status_literal(build, where, status, "ok"));
        return true;
    }

    // A branch is cloned, not rebuilt. cf.br and cf.cond_br carry their
    // successors as blocks, and IRMapping remaps a cloned operation's
    // successors as well as its operands - so the block map built above is
    // all this needs.
    if (mlir::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp>(op)) {
        build.clone(op, mapping);
        return true;
    }

    // Every other CTJS_RuntimeOp is one call to its helper: the frame when
    // the helper's first parameter is one, then the operands in order. A
    // status return is tested; anything else is called plainly and its
    // result parked like every other value. A helper with a parameter this
    // does not supply falls through to convert()'s loud failure rather than
    // being called without it.
    if (auto runtime = mlir::dyn_cast<RuntimeCallOpInterface>(op)) {
        const runtime_helper & helper = helper_for(runtime.getHelperID());
        const bool tested = returns_status(helper);
        llvm::SmallVector<mlir::Value> arguments;
        if (helper.roles[0] == param_role::frame) { arguments.push_back(scope.frame); }
        for (const mlir::Value operand : op.getOperands()) {
            arguments.push_back(mapping.lookup(operand));
        }
        if (arguments.size() + (tested ? op.getNumResults() : 0) != helper.role_count) {
            return false;
        }
        const std::string symbol = callee(helper.symbol);
        if (tested && op.getNumResults() == 0) {
            status_call_void(scope, build, where, symbol, arguments);
        } else if (tested) {
            mapping.map(op.getResult(0),
                        status_call(scope, build, where, symbol, arguments, value));
        } else {
            const llvm::SmallVector<mlir::Type> produces(op.getNumResults(), value);
            auto call = ec::CallOpaqueOp::create(build, where, produces, symbol, arguments);
            if (op.getNumResults() != 0) { mapping.map(op.getResult(0), call.getResult(0)); }
        }
        return true;
    }

    return false;
}

} // namespace ctcompile::ctjs::emitc_detail
