// Boxed EmitC runtimeops lowering.
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

    // A CALL, WHICH IS THE FIRST OPERATION THAT NEEDS THE FRAME FOR
    // SOMETHING OTHER THAN ROOTING.
    //
    // ct_aot_call takes `const uint64_t *argv` - a CONTIGUOUS run - and the
    // arguments are rooted in slots of their own, which are not adjacent.
    // So a run was reserved for this site and the arguments are copied into
    // it here, immediately before the call. IN THE FRAME, not in a C++
    // array: ct_aot_call is a safepoint that runs arbitrary user JavaScript
    // before it reads them, and an array of locals is invisible to a
    // precise collector.
    //
    // THE SPAN IS FETCHED ONCE PER STORE AND AGAIN FOR THE POINTER, because
    // context::call resizes context::registers_ and the row says the
    // pointer is valid "NOT ONE INSTRUCTION LONGER" than the next
    // safepoint. Nothing between these stores is a safepoint, so one fetch
    // would do - and hoisting it is a decision this backend has no analysis
    // to justify.
    //
    // AND ctjs.call_direct IS THE SAME CALL IN THIS TIER. The resolver's
    // symbol is for the native backend; here the callee VALUE it kept is
    // what ct_aot_call dispatches on, exactly as for ctjs.call, and its
    // new.target operand is the undefined constant body_is_supported
    // insisted on - which is what the runtime gives a plain call anyway.
    mlir::Value dispatched_callee;
    mlir::Value dispatched_receiver;
    mlir::ValueRange dispatched_arguments;
    mlir::Value dispatched_result;
    if (auto call = mlir::dyn_cast<CallOp>(op)) {
        dispatched_callee = call.getCallee();
        dispatched_receiver = call.getReceiver();
        dispatched_arguments = call.getArgs();
        dispatched_result = call.getResult();
    } else if (auto direct = mlir::dyn_cast<CallDirectOp>(op)) {
        dispatched_callee = direct.getCalleeValue();
        dispatched_receiver = direct.getReceiver();
        dispatched_arguments = direct.getArgs();
        dispatched_result = direct.getResult();
    }
    if (dispatched_result) {
        const unsigned base = park_window(scope, build, where, mapping, op, dispatched_arguments);
        const mlir::Value argv = window_pointer(scope, build, where, base);

        // `key` AND `site` ARE THE ROW'S DIAGNOSTIC ARGUMENTS and the
        // implementation ignores both. `key` names the callee in the
        // message op::call_computed would produce; `site` carries a
        // backwards scan of bytecode that an AOT frame does not have.
        // Passing undefined and nullptr is honest about having neither
        // rather than inventing one.
        mapping.map(dispatched_result,
                    status_call(scope, build, where, callee("ct_aot_call"),
                                {scope.frame, mapping.lookup(dispatched_callee),
                                 mapping.lookup(dispatched_receiver), argv,
                                 literal(build, where, opaque(build.getContext(), "uint32_t"),
                                         std::to_string(dispatched_arguments.size())),
                                 undefined(build, where, value),
                                 literal(build, where,
                                         pointer_to(build.getContext(),
                                                    "const ctbrowser::aot::ct_aot_site"),
                                         "nullptr")},
                                value));
        return true;
    }

    // READING AND WRITING A CAPTURED BINDING, each a pair of calls.
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

    // BUILDING A CLOSURE.
    //
    // RAISE TIER ONLY, so there is no status and no exception edge: the
    // row's three failures - the allocation ceiling, no program to take a
    // function from, and an index or count that does not match - all raise,
    // and a caller polls ct_aot_failed at a back edge. It IS a safepoint, so
    // the result is parked like every other value.
    //
    // THE UPVALUES GO IN PARALLEL WITH THE DESCRIPTORS, which is what the
    // importer built and what the helper reads. Packed would capture the
    // wrong bindings and say nothing.
    if (auto made = mlir::dyn_cast<CreateClosureOp>(op)) {
        const auto upvalues = made.getUpvalues();
        const unsigned base = park_window(scope, build, where, mapping, op, upvalues);
        const auto u32 = opaque(build.getContext(), "uint32_t");
        mapping.map(
            made.getResult(),
            ec::CallOpaqueOp::create(
                build, where, mlir::TypeRange{value}, callee("ct_aot_make_closure"),
                mlir::ValueRange{scope.frame, mapping.lookup(made.getEnclosingClosure()),
                                 literal(build, where, u32, std::to_string(made.getFunction())),
                                 window_pointer(scope, build, where, base),
                                 literal(build, where, u32, std::to_string(upvalues.size())),
                                 mapping.lookup(made.getEnclosingThis())})
                .getResult(0));
        return true;
    }

    // A PROPERTY WRITE. Its inline cache is nullptr for the reason
    // ctjs.get_property's is: ct_aot_ic is forward-declared and nothing can
    // allocate one.
    //
    // ITS STATUS EDGE IS NOT COVERED BY A CASE, and that is worth saying
    // rather than leaving to be assumed. Nothing in the differential
    // fixture makes a property write FAIL - that needs a setter that
    // throws, or a proxy trap - so the branch this emits is built and never
    // taken. The write itself is covered; the edge is not.
    if (auto write = mlir::dyn_cast<SetPropertyOp>(op)) {
        status_call_void(
            scope, build, where, callee("ct_aot_set_index"),
            {scope.frame, mapping.lookup(write.getObject()), mapping.lookup(write.getKey()),
             mapping.lookup(write.getValue()),
             literal(build, where, pointer_to(build.getContext(), "ctbrowser::aot::ct_aot_ic"),
                     "nullptr")});
        return true;
    }

    // AN ARRAY LITERAL.
    //
    // IT ALLOCATES AND IS RAISE TIER ONLY: allocate() raises past the
    // ceiling and STILL returns a well-formed object, so there is no status
    // to test - only a poll to schedule at a back edge. It is a safepoint,
    // so its result is parked like every other value.
    if (auto made = mlir::dyn_cast<CreateArrayOp>(op)) {
        // `reserve_hint` IS A HINT and the row says so - an array that
        // ignores it is merely slower. The elements the operation carries
        // are appended after, which is also how the bytecode builds one:
        // new_array then one append per element.
        const auto elements = made.getElements();
        const mlir::Value array =
            ec::CallOpaqueOp::create(
                build, where, mlir::TypeRange{value}, callee("ct_aot_new_array"),
                mlir::ValueRange{scope.frame,
                                 literal(build, where, opaque(build.getContext(), "uint32_t"),
                                         std::to_string(elements.size()))})
                .getResult(0);
        for (const mlir::Value element : elements) {
            ec::CallOpaqueOp::create(build, where, mlir::TypeRange{}, callee("ct_aot_append"),
                                     mlir::ValueRange{scope.frame, array, mapping.lookup(element)});
        }
        mapping.map(made.getResult(), array);
        return true;
    }
    if (auto opened = mlir::dyn_cast<PushHandlerOp>(op)) {
        // THE PAD ID IS THIS OPERATION'S OWN SLOT NUMBER, which makes it
        // unique within the function without a second counter. It is what
        // ct_aot_catch_land hands back to say WHICH handler fired - and
        // with one region per function, nothing yet reads it.
        //
        // THE $handler EDGE IS DROPPED HERE, deliberately. It exists in the
        // CTJS CFG so the pad is reachable and its arguments dominate; the
        // edge the emitted code takes is ctjs.check's, at the throw site,
        // because that is where the live register file is.
        const unsigned slot = scope.argument_windows.lookup(&op);
        const auto u32 = opaque(build.getContext(), "uint32_t");
        ec::CallOpaqueOp::create(
            build, where, mlir::TypeRange{}, callee("ct_aot_handler_push"),
            mlir::ValueRange{scope.frame, literal(build, where, u32, std::to_string(slot)),
                             literal(build, where, u32, std::to_string(slot))});
        llvm::SmallVector<mlir::Value> onward;
        for (const mlir::Value each : opened.getBodyOperands()) {
            onward.push_back(mapping.lookup(each));
        }
        mlir::cf::BranchOp::create(build, where, mapping.lookup(opened.getBody()), onward);
        return true;
    }
    if (mlir::isa<PopHandlerOp>(op)) {
        ec::CallOpaqueOp::create(build, where, mlir::TypeRange{}, callee("ct_aot_handler_pop"),
                                 mlir::ValueRange{scope.frame});
        return true;
    }
    if (auto guarded = mlir::dyn_cast<CheckOp>(op)) {
        // A PLAIN BRANCH. The caught edge was already emitted, once per
        // fallible call in this block, by caught_or_failure - which is the
        // only place the status exists to test.
        llvm::SmallVector<mlir::Value> onward;
        for (const mlir::Value each : guarded.getContOperands()) {
            onward.push_back(mapping.lookup(each));
        }
        mlir::cf::BranchOp::create(build, where, mapping.lookup(guarded.getCont()), onward);
        return true;
    }
    if (auto landed = mlir::dyn_cast<CatchLandOp>(op)) {
        // READS BACK WHAT THE UNWINDER WROTE and clears the pad marker, so
        // it runs exactly once and only on a caught edge - which the
        // importer enforces by refusing a pad reachable any other way.
        const auto u32 = opaque(build.getContext(), "uint32_t");
        auto slot = ec::VariableOp::create(build, where, ec::LValueType::get(value),
                                           ec::OpaqueAttr::get(build.getContext(), ""));
        auto address =
            ec::AddressOfOp::create(build, where, ec::PointerType::get(value), slot.getResult());
        auto pad = ec::CallOpaqueOp::create(build, where, mlir::TypeRange{u32},
                                            callee("ct_aot_catch_land"),
                                            mlir::ValueRange{scope.frame, address.getResult()});
        mapping.map(landed.getPad(), pad.getResult(0));
        mapping.map(landed.getThrown(),
                    ec::LoadOp::create(build, where, value, slot.getResult()).getResult());
        return true;
    }
    // THE ARRIVING ARGUMENT WINDOW, WHICH IS NOT THIS FRAME'S SLOTS.
    //
    // ct_aot_enter puts this frame's registers ABOVE the caller's window,
    // and the entry prologue reads argv only for the DECLARED parameters -
    // so an extra argument is reachable from neither. ct_aot_args and
    // ct_aot_argc answer for the frame, and both operations take what they
    // need from there rather than from operands the importer could not
    // build.
    if (mlir::isa<MakeArgumentsOp, GatherRestOp>(op)) {
        const auto u32 = opaque(build.getContext(), "uint32_t");
        const mlir::Value window =
            ec::CallOpaqueOp::create(build, where,
                                     mlir::TypeRange{pointer_to(build.getContext(), "uint64_t")},
                                     callee("ct_aot_args"), mlir::ValueRange{scope.frame})
                .getResult(0);
        const mlir::Value count =
            ec::CallOpaqueOp::create(build, where, mlir::TypeRange{u32}, callee("ct_aot_argc"),
                                     mlir::ValueRange{scope.frame})
                .getResult(0);
        // RAISE TIER, both: they answer the array rather than a status, so
        // there is no edge - a caller polls at a back edge. Both allocate,
        // so the result is parked like every other value.
        if (auto rest = mlir::dyn_cast<GatherRestOp>(op)) {
            mapping.map(
                rest.getResult(),
                ec::CallOpaqueOp::create(
                    build, where, mlir::TypeRange{value}, callee("ct_aot_gather_rest"),
                    mlir::ValueRange{scope.frame, window, count,
                                     literal(build, where, u32, std::to_string(rest.getFrom()))})
                    .getResult(0));
            return true;
        }
        auto made = mlir::cast<MakeArgumentsOp>(op);
        mapping.map(made.getResult(),
                    ec::CallOpaqueOp::create(build, where, mlir::TypeRange{value},
                                             callee("ct_aot_make_arguments"),
                                             mlir::ValueRange{scope.frame, window, count})
                        .getResult(0));
        return true;
    }
    if (auto gone = mlir::dyn_cast<DeleteNamedOp>(op)) {
        // TWO CALLS, for ctjs.define_accessor's reason: the helper wants an
        // INTERNED name record rather than characters. The length travels
        // beside the text because a property name is BYTES and one
        // containing a zero byte is legal JavaScript that strlen stops at.
        const mlir::Value interned = intern(build, where, gone.getName());
        ec::CallOpaqueOp::create(
            build, where, mlir::TypeRange{}, callee("ct_aot_delete_prop"),
            mlir::ValueRange{scope.frame, mapping.lookup(gone.getObject()), interned});
        return true;
    }
    // ---- THE FOUR MODULE OPERATIONS ------------------------------
    //
    // Three of them take their names as BYTES AND A LENGTH rather than a
    // NUL-terminated string, for ct_aot_global_get's reason: a specifier or
    // an export name is bytes, and one containing a zero byte is a string
    // strlen stops at. `default` is an export name the compiler synthesises
    // and the rest come out of the source, so the length is always known
    // here and never worth a strlen.
    if (auto imported = mlir::dyn_cast<ModuleImportCellOp>(op)) {
        // RAISE TIER: it answers the CELL rather than a status, so there is
        // no exception edge - a missing module and a missing export are
        // both uncatchable engine faults and a caller polls at a back edge.
        // NOT a safepoint either: two flat_map lookups allocate nothing.
        const auto [specifier, specifier_length] =
            bytes_and_length(build, where, imported.getSpecifier());
        const auto [exported, exported_length] =
            bytes_and_length(build, where, imported.getExportName());
        mapping.map(imported.getResult(),
                    ec::CallOpaqueOp::create(
                        build, where, mlir::TypeRange{value}, callee("ct_aot_module_import_cell"),
                        mlir::ValueRange{scope.frame, specifier, specifier_length, exported,
                                         exported_length})
                        .getResult(0));
        return true;
    }
    if (auto published = mlir::dyn_cast<ModuleExportCellOp>(op)) {
        // THE ONLY SEEDED status_call IN THE FILE. $current is the
        // destination register's value on entry, and the helper hands it
        // straight back when no module is being evaluated - which is how
        // op::bind_export's CONDITIONAL write is expressed without the
        // CT_AOT_NO_WRITE status its ABI row asked for and the enum never
        // had. See status_call.
        const auto [name, length] = bytes_and_length(build, where, published.getName());
        mapping.map(published.getResult(),
                    status_call(scope, build, where, callee("ct_aot_module_export_cell"),
                                {scope.frame, name, length}, value,
                                mapping.lookup(published.getCurrent())));
        return true;
    }
    if (auto space = mlir::dyn_cast<ModuleNamespaceOp>(op)) {
        // RAISE TIER like the import row, and a SAFEPOINT unlike it: the
        // namespace object and one native getter per export are allocated
        // here. The result is parked like every other value.
        const auto [specifier, length] = bytes_and_length(build, where, space.getSpecifier());
        mapping.map(space.getResult(),
                    ec::CallOpaqueOp::create(build, where, mlir::TypeRange{value},
                                             callee("ct_aot_module_namespace"),
                                             mlir::ValueRange{scope.frame, specifier, length})
                        .getResult(0));
        return true;
    }
    if (auto accessor = mlir::dyn_cast<DefineAccessorOp>(op)) {
        // TWO CALLS, WHICH IS WHY THIS IS NOT A CTJS_RuntimeOp. The helper
        // wants an INTERNED name record rather than characters, so the
        // name is interned first - the pool is immortal and hash-indexed,
        // so the repeat cost is a lookup and a memo can come later.
        //
        // THE LENGTH IS EMITTED BESIDE THE TEXT for ct_aot_global_get's
        // reason: a property name is BYTES, and one containing a zero byte
        // is legal JavaScript that strlen would stop at.
        const mlir::Value interned = intern(build, where, accessor.getName());
        ec::CallOpaqueOp::create(build, where, mlir::TypeRange{}, callee("ct_aot_define_accessor"),
                                 mlir::ValueRange{scope.frame, mapping.lookup(accessor.getTarget()),
                                                  interned, mapping.lookup(accessor.getGetter()),
                                                  mapping.lookup(accessor.getSetter())});
        return true;
    }
    // THE SPREAD CALLS, WHICH NEED NO WINDOW. Their arguments arrived as
    // one already-rooted array rather than as a run of values, so there is
    // nothing to park and no argc to pass - which is the entire difference
    // from the ctjs.call and ctjs.construct branches below.
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
        // ITS OUT-PARAMETER IS A uint32_t BOOLEAN, not a value - the same
        // shape ct_aot_loose_equals has, and boxed the same way.
        const auto u32 = opaque(build.getContext(), "uint32_t");
        const mlir::Value answered = status_call(
            scope, build, where, callee("ct_aot_has_property"),
            {scope.frame, mapping.lookup(asks.getObject()), mapping.lookup(asks.getKey())}, u32);
        mapping.map(asks.getResult(),
                    box(build, where, value, "ctc_box_bool", nonzero(build, where, answered)));
        return true;
    }
    if (auto is_a = mlir::dyn_cast<InstanceOfOp>(op)) {
        // RAISE TIER: it returns its BOOLEAN rather than a status, so there
        // is no out-parameter and no exception edge - a caller polls
        // ct_aot_failed at a back edge. It is still a safepoint.
        const auto u32 = opaque(build.getContext(), "uint32_t");
        const mlir::Value answered =
            ec::CallOpaqueOp::create(build, where, mlir::TypeRange{u32},
                                     callee("ct_aot_instance_of"),
                                     mlir::ValueRange{scope.frame, mapping.lookup(is_a.getObject()),
                                                      mapping.lookup(is_a.getConstructor())})
                .getResult(0);
        mapping.map(is_a.getResult(), nonzero(build, where, answered));
        return true;
    }
    if (auto boxed = mlir::dyn_cast<FromBoolOp>(op)) {
        mapping.map(boxed.getResult(),
                    box(build, where, value, "ctc_box_bool", mapping.lookup(boxed.getBit())));
        return true;
    }

    // A THROW, WHICH NEVER COMES BACK ok.
    //
    // The row is explicit: ct_aot_throw returns CAUGHT, UNWOUND or FAILED
    // and never CT_AOT_OK, because the throw COMPLETES INSIDE THE CALLEE -
    // by the time it returns there is no exception in flight, only a frame
    // stack shorter than it was. So there is no status to TEST and no
    // continuation: the operation is a terminator, and this branches
    // straight to the shared failure path, which already gets the
    // conditional ct_aot_leave right.
    //
    // THE CONDITIONAL ct_aot_leave ON THAT PATH IS NOT PINNED BY A CASE,
    // and it is worth saying which way. Making it unconditional changes no
    // answer, because leave TRUNCATES to this frame's own recorded index
    // rather than popping - each resize guarded so it cannot destroy
    // somebody else's frame - and its row calls a second call "a harmless
    // no-op after a failure". So it is an unchecked invariant, not a live
    // defect, and a case pinning it would be pinning the runtime's
    // defensiveness rather than this backend's correctness.
    //
    // CT_AOT_CAUGHT CANNOT ARRIVE HERE, and that is a fact about the input
    // rather than an assumption: `caught` is reported only when a handler
    // in THIS frame won, and ctjs.push_handler has no lowering - so any
    // function containing a `try` is refused whole and never reaches this
    // line. When try/catch arrives, the failure path needs a third arm
    // BEFORE this does.
    if (auto thrown = mlir::dyn_cast<ThrowOp>(op)) {
        auto answered = ec::CallOpaqueOp::create(
            build, where, mlir::TypeRange{status}, callee("ct_aot_throw"),
            mlir::ValueRange{scope.frame, mapping.lookup(thrown.getValue())});
        mlir::cf::BranchOp::create(build, where, failure_path(scope, build, where),
                                   mlir::ValueRange{answered.getResult(0)});
        return true;
    }

    // `new callee(...)`. The same contiguous argument window a call needs,
    // for the same reason: ct_aot_construct is a safepoint that runs
    // arbitrary user JavaScript - the field initialisers and then the body -
    // before it is done with them.
    //
    // ITS `site` IS THE ENTRY'S OWN, unlike ct_aot_call's which the
    // implementation ignores: construct uses it to name THIS function in
    // the TypeError a `new` on a non-constructor throws.
    //
    // AND new.target IS NOT PASSED, because the ABI has nowhere to put it -
    // ct_aot_construct sets it from the callee itself, which is what
    // op::construct does with `fresh.new_target = callee`.
    if (auto built = mlir::dyn_cast<ConstructOp>(op)) {
        const auto arguments = built.getArgs();
        const unsigned base = park_window(scope, build, where, mapping, op, arguments);
        mapping.map(built.getResult(),
                    status_call(scope, build, where, callee("ct_aot_construct"),
                                {scope.frame, mapping.lookup(built.getCallee()),
                                 window_pointer(scope, build, where, base),
                                 literal(build, where, opaque(build.getContext(), "uint32_t"),
                                         std::to_string(arguments.size())),
                                 scope.entry_site},
                                value));
        return true;
    }

    if (auto returned = mlir::dyn_cast<ReturnOp>(op)) {
        // Normalize first: a constructor returning a primitive carries its
        // receiver instead. The escape hook must root that actual result
        // while removing the frame and excluding its dead register window.
        const mlir::Value produced = returned.getValue() ? mapping.lookup(returned.getValue())
                                                         : undefined(build, where, value);
        auto result = ec::CallOpaqueOp::create(build, where, mlir::TypeRange{value},
                                               callee("ct_aot_return_value"),
                                               mlir::ValueRange{produced, receiver, constructing});
        ec::CallOpaqueOp::create(build, where, mlir::TypeRange{}, callee("ct_aot_leave_return"),
                                 mlir::ValueRange{scope.frame, result.getResult(0)});
        // `out[0] = ...` AND NOT `*out = ...`. LLVM 23's C++ emitter refuses
        // an assignment through an `emitc.dereference` result under
        // --declare-variables-at-top ("result variable for the operation
        // has not been declared") while accepting the identical store
        // through `emitc.subscript`, and the two spell the same C++ for a
        // pointer to one slot. Measured on the 22 -> 23 bump, 2026-09-02.
        const mlir::Value slot =
            ec::ConstantOp::create(build, where, build.getI32Type(), build.getI32IntegerAttr(0));
        auto destination = ec::SubscriptOp::create(build, where, ec::LValueType::get(value), out,
                                                   mlir::ValueRange{slot});
        ec::AssignOp::create(build, where, destination, result.getResult(0));
        ec::ReturnOp::create(build, where,
                             literal(build, where, status,
                                     "static_cast<int32_t>(ctbrowser::aot::ct_aot_status::ok)"));
        return true;
    }

    // A BRANCH IS CLONED, NOT REBUILT. cf.br and cf.cond_br carry their
    // successors as blocks, and IRMapping remaps a cloned operation's
    // successors as well as its operands - so the block map built above is
    // all this needs.
    if (mlir::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp>(op)) {
        build.clone(op, mapping);
        return true;
    }

    // EVERY OTHER CTJS_RuntimeOp IS ONE CALL TO ITS HELPER: the frame when
    // the row leads with one, then the operands in order. The row decides the
    // tier - a status return is tested; anything else is raise tier or
    // edge-free, called plainly, its result parked like every other value.
    // A row with a parameter this does not supply falls through to
    // convert()'s loud failure rather than being called without it.
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
