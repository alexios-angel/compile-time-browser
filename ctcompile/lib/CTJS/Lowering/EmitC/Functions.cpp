// Boxed EmitC functions lowering.
#include "Lowering.h"

namespace ctcompile::ctjs::emitc_detail {

// Returns whether the function was replaced.
bool lowering::lower(FuncOp function, mlir::OpBuilder & build, mlir::MLIRContext * context) {
    const mlir::Location where = function.getLoc();

    // UNREACHABLE BLOCKS FIRST, because the importer emits one for every
    // function and it is not a reason to refuse anything.
    //
    // A body that ends in an explicit `return` still gets the implicit
    // `return undefined` epilogue after it, and nothing branches there. A
    // block with no predecessors has no edges, so it cannot carry the
    // block-argument copy the emitter miscompiles - refusing on its account
    // would refuse EVERY function, including the one-line one this pass
    // exists to compile. Removing it is an ordinary simplification, not a
    // decision about semantics.
    mlir::IRRewriter prune(context);
    (void)mlir::eraseUnreachableBlocks(prune, function.getBody());

    // BLOCKS ARE NO LONGER A REASON TO REFUSE. They were, until
    // --emitc-eliminate-block-arguments existed: emitting a block argument
    // as it stands hits the copy the C++ emitter loses. That pass runs
    // after this one and turns each into a variable, so what this pass
    // emits may carry block arguments and what reaches mlir-translate must
    // not. The pipeline order is the contract, and end-to-end.mlir runs it.

    std::string why;
    if (!body_is_supported(function, why)) {
        refuse(function, why);
        return false;
    }
    mlir::Block & body = function.getBody().front();
    if (body.getNumArguments() < implicit_arguments) {
        refuse(function, "fewer arguments than the importer's three implicit ones");
        return false;
    }
    // BOTH IMPLICIT ARGUMENTS ARE DELIVERABLE NOW, and neither always was.
    //
    // ct_aot_callee is the only way a compiled function can reach its own
    // upvalues: they live on the closure INSTANCE, while `site` is the
    // function_proto every closure over the same function shares.
    //
    // ct_aot_new_target was refused here until it had a body, and behind
    // that sat a real blocker in TWO halves: ct_aot_enter takes new_target
    // from pending_new_target_, and neither of the two paths into a
    // compiled constructor set it. context::construct_new does now, and so
    // does VM_CASE(construct)'s compiled arm - which was the half nothing
    // noticed until a differential case read new.target and disagreed.
    //
    // The refusal is gone, which matters more than one opcode suggests:
    // Babel's _classCallCheck guard is a new.target test, so a transpiled
    // bundle has one in almost every class.

    FrameEnterOp entered;
    function.getBody().walk([&](FrameEnterOp op) { entered = op; });
    if (!entered) {
        refuse(function, "no ctjs.frame_enter - every compiled body must establish a frame");
        return false;
    }

    // ---- the entry signature ------------------------------------------
    const auto value = opaque(context, "uint64_t");
    const auto u32 = opaque(context, "uint32_t");
    const auto ctx_ptr = pointer_to(context, "ctbrowser::aot::ct_aot_ctx");
    const auto site_ptr = pointer_to(context, "const ctbrowser::aot::ct_aot_site");
    const auto argv_ptr = pointer_to(context, "const uint64_t");
    const auto out_ptr = pointer_to(context, "uint64_t");
    const auto frame_ptr = pointer_to(context, "ctbrowser::aot::ct_aot_frame");
    const auto status = mlir::IntegerType::get(context, 32);

    const llvm::SmallVector<mlir::Type> inputs{ctx_ptr, site_ptr, argv_ptr, u32,
                                               value,   u32,      out_ptr};
    build.setInsertionPoint(function);

    // THE ADDRESS THIS FUNCTION'S STRING LITERALS MEMOISE UNDER, declared
    // BEFORE the function that takes it. One byte per compiled function,
    // never read - only its address matters, and it has to differ from the
    // function_proto the interpreter keys the same cache by. See
    // compiled_entry::memo_site.
    const std::string marker = "ctc_memo_" + c_identifier(function.getName().str());
    ec::VerbatimOp::create(build, where,
                           build.getStringAttr("static const char " + marker + " = 0;"));

    auto entry = ec::FuncOp::create(build, where, c_identifier(function.getName()),
                                    build.getFunctionType(inputs, {status}));
    // extern "C" SO THE SYMBOL MATCHES ct_aot_entry_fn's expectations.
    entry.setSpecifiersAttr(build.getStrArrayAttr({"extern \"C\""}));

    mlir::Block * abi = entry.addEntryBlock();
    build.setInsertionPointToStart(abi);
    const mlir::Value in_ctx = abi->getArgument(0);
    const mlir::Value in_site = abi->getArgument(1);
    const mlir::Value in_argv = abi->getArgument(2);
    const mlir::Value in_receiver = abi->getArgument(4);
    const mlir::Value in_constructing = abi->getArgument(5);
    const mlir::Value in_out = abi->getArgument(6);

    // ---- the parameters, copied BEFORE the frame exists ---------------
    //
    // argv is an interior pointer into context::registers_ and ct_aot_enter
    // resizes that vector, so every read has to happen first. The caller
    // has already filled the callee's window - "argv must already be the
    // callee's window with missing parameters filled in with undefined" -
    // so argv[i] is well-defined for every declared parameter regardless of
    // how many arguments actually arrived.
    const unsigned declared = body.getNumArguments() - implicit_arguments;
    llvm::SmallVector<mlir::Value> parameters;
    parameters.reserve(declared);
    //
    // THE PARAMETER STAYS `const uint64_t *` AND THE READ CASTS IT AWAY,
    // which is the least-bad of three options and worth saying why.
    //
    // The signature is not negotiable: this function has to be assignable
    // to ct_aot_entry_fn, which declares `const uint64_t *argv`, and
    // spelling it without the const would make the assignment ill-typed.
    // But emitc.subscript requires the result's type to equal the
    // pointee's, so reading through it yields a `const uint64_t` - and
    // under --declare-variables-at-top, which is mandatory here, EmitC
    // declares every value at the top and assigns later. `const uint64_t
    // v8; v8 = ...;` does not compile.
    //
    // So the CAST IS ON THE POINTER, once, rather than on each element.
    // It only ever reads through the result - argv is the caller's window
    // and writing to it would corrupt the caller's registers - so casting
    // the const away is a spelling concession, not a licence.
    const auto element = opaque(context, "uint64_t");
    const auto element_slot = ec::LValueType::get(element);
    const mlir::Value readable =
        declared == 0
            ? mlir::Value{}
            : ec::CastOp::create(build, where, ec::PointerType::get(element), in_argv).getResult();
    for (unsigned i = 0; i < declared; ++i) {
        const mlir::Value at =
            literal(build, where, mlir::IntegerType::get(context, 32), std::to_string(i));
        auto slot =
            ec::SubscriptOp::create(build, where, element_slot, readable, mlir::ValueRange{at});
        parameters.push_back(ec::LoadOp::create(build, where, element, slot.getResult()));
    }

    llvm::DenseMap<mlir::Value, unsigned> scope_slots;
    mlir::Value mapping_callee;
    mlir::Value mapping_new_target;

    // ---- the frame ----------------------------------------------------
    //
    // CT_AOT_FRAME_BYTES OF CALLER-ALLOCATED SPACE, sized from the macro
    // the runtime's own header defines rather than from a number written
    // ALIGNED, because `unsigned char[N]` is aligned to 1 and ct_aot_enter
    // constructs an aot_frame_storage in it - a struct with a pointer and
    // three indices. aot_bridge/internal.hpp asserts its size against
    // CT_AOT_FRAME_BYTES but nothing asserted the ALIGNMENT, and an
    // under-aligned placement is undefined behaviour that happens to work
    // on x86-64 and need not elsewhere.
    //
    // here. The array is passed directly: C++ decays it to `unsigned char *`
    // and converts that to the `void *` the row declares, which needs no
    // subscript, no address-of, and no size_t - and !emitc.size_t emits a
    // bare `size_t` that does not compile.
    const auto block_type = ec::ArrayType::get({static_cast<std::int64_t>(CT_AOT_FRAME_BYTES)},
                                               opaque(context, kFrameStorageElement));
    auto storage =
        ec::VariableOp::create(build, where, block_type, ec::OpaqueAttr::get(context, ""));
    // THE REGISTER WINDOW HAS TO HOLD THE ROOTS TOO.
    //
    // proto.frame_size sizes the interpreter's register file, and this
    // backend needs one slot for every JavaScript value it produces -
    // because a value in a C++ local is invisible to a precise collector.
    // The two are counted together and the larger wins: asking for more
    // than frame_size is safe (ct_aot_enter uses the number verbatim and
    // fills with undefined), asking for less makes ct_aot_slots hand back a
    // null span far from the cause.
    //
    // SLOTS ARE NEVER REUSED. A liveness analysis would pack them; keeping
    // a dead value alive until the frame is left is a bounded leak, and
    // getting liveness wrong is a use-after-free. This is the MVP's trade.
    unsigned parked = 0;
    for (unsigned i = 0; i < declared; ++i) {
        scope_slots[body.getArgument(implicit_arguments + i)] = parked++;
    }
    llvm::DenseMap<mlir::Operation *, unsigned> windows;
    // AND A MEMO SLOT PER STRING LITERAL. Not a frame slot: the cache
    // ct_aot_new_string keys by (site, slot) is a map, so these only have
    // to be unique within the function and stable across calls.
    llvm::DenseMap<mlir::Operation *, unsigned> memo_slots;
    unsigned next_memo = 0;
    function.getBody().walk([&](mlir::Operation * inner) {
        for (const mlir::Value result : inner->getResults()) {
            if (mlir::isa<ValueType>(result.getType())) { scope_slots[result] = parked++; }
        }
        // A CONTIGUOUS RUN PER CALL SITE, reserved here so the register
        // window can be sized to include it before ct_aot_enter is emitted.
        if (auto call = mlir::dyn_cast<CallOp>(inner)) {
            windows[inner] = parked;
            parked += static_cast<unsigned>(call.getArgs().size());
        }
        if (auto direct = mlir::dyn_cast<CallDirectOp>(inner)) {
            windows[inner] = parked;
            parked += static_cast<unsigned>(direct.getArgs().size());
        }
        // A CLOSURE'S UPVALUE ARRAY IS AN ARGV BY ANOTHER NAME: a
        // contiguous run the helper reads, and one that must live in the
        // frame rather than in C++ locals, because ct_aot_make_closure
        // allocates and is therefore a safepoint.
        if (auto made = mlir::dyn_cast<CreateClosureOp>(inner)) {
            windows[inner] = parked;
            parked += static_cast<unsigned>(made.getUpvalues().size());
        }
        if (auto built = mlir::dyn_cast<ConstructOp>(inner)) {
            windows[inner] = parked;
            parked += static_cast<unsigned>(built.getArgs().size());
        }
        // ONE SLOT PER PROTECTED REGION, which is where the unwinder puts
        // the thrown value: context::unwind_to_handler writes
        // registers_[base + slot] and ct_aot_catch_land reads it back. It
        // has to be inside the run ct_aot_enter reserves, so it is assigned
        // here with everything else rather than invented at the call.
        if (mlir::isa<PushHandlerOp>(inner)) {
            windows[inner] = parked;
            parked += 1;
        }
        if (auto constant = mlir::dyn_cast<ConstantOp>(inner)) {
            // A BIGINT LITERAL MEMOISES THE SAME WAY AND SHARES THE
            // NUMBERING. Both helpers key on (site, slot) and this backend
            // hands them the same marker, so one counter across both keeps
            // a string and a bigint from ever claiming the same slot.
            if (mlir::isa<StringAttr, BigIntAttr>(constant.getValue())) {
                memo_slots[inner] = next_memo++;
            }
        }
    });
    const unsigned window = std::max(static_cast<unsigned>(entered.getRegCount()), parked);
    const mlir::Value registers = literal(build, where, u32, std::to_string(window));
    auto frame = ec::CallOpaqueOp::create(
        build, where, mlir::TypeRange{frame_ptr}, callee("ct_aot_enter"),
        mlir::ValueRange{in_ctx, in_site, registers, in_receiver, storage});

    // ITS FAILURE IS A NULL POINTER, NOT A STATUS. There is no frame, so
    // there is nothing to leave and no handler to reach.
    const mlir::Value null = literal(build, where, frame_ptr, "nullptr");
    auto ok = ec::CmpOp::create(build, where, mlir::IntegerType::get(context, 1),
                                ec::CmpPredicate::ne, frame.getResult(0), null);

    mlir::Block * raised = entry.addBlock();
    mlir::Block * running = entry.addBlock();
    mlir::cf::CondBranchOp::create(build, where, ok, running, mlir::ValueRange{}, raised,
                                   mlir::ValueRange{});

    build.setInsertionPointToStart(raised);
    ec::ReturnOp::create(build, where,
                         literal(build, where, status,
                                 "static_cast<int32_t>(ctbrowser::aot::ct_aot_status::failed)"));

    // ---- the blocks ---------------------------------------------------
    //
    // Every CTJS block gets one here, arguments and all. They are NOT
    // eliminated in this pass: --emitc-eliminate-block-arguments does that
    // afterwards, and keeping the two separate is what lets the elimination
    // be tested against a program that runs rather than only against the
    // output of this file.
    // THE CLOSURE, WHICH ONLY EXISTS ONCE THE FRAME DOES. ct_aot_callee
    // reads call_frame::closure, so this cannot be hoisted above
    // ct_aot_enter the way the parameters had to be pulled below it - and
    // it must be emitted BEFORE the mapping is built rather than after,
    // which is not a style point: mapping a block argument to a Value that
    // is still null is accepted silently, and the crash arrives later
    // inside Operation::create, with a stack that names neither.
    build.setInsertionPointToEnd(running);
    if (!body.getArgument(arg_callee).use_empty()) {
        mapping_callee =
            ec::CallOpaqueOp::create(build, where, mlir::TypeRange{value}, callee("ct_aot_callee"),
                                     mlir::ValueRange{frame.getResult(0)})
                .getResult(0);
    }
    // AND new.target, THE SAME WAY AND FOR THE SAME REASON. It reads
    // call_frame::new_target, so it cannot be hoisted above ct_aot_enter
    // either, and it must be emitted before the mapping is built.
    if (!body.getArgument(arg_new_target).use_empty()) {
        mapping_new_target = ec::CallOpaqueOp::create(build, where, mlir::TypeRange{value},
                                                      callee("ct_aot_new_target"),
                                                      mlir::ValueRange{frame.getResult(0)})
                                 .getResult(0);
    }

    // ARGUMENT 0 IS THE EFFECTIVE RECEIVER, NOT THE ENTRY'S RAW ONE, and the
    // difference is a live bug for arrows. The importer maps op::load_this
    // to this argument, and VM_CASE(load_this) is
    // `effective_this(*vm_frame)` - which returns the enclosing method's
    // object when the frame's closure is an arrow, and the frame's own
    // receiver otherwise. Delivering the entry's `receiver` agrees for
    // every ordinary function and is wrong for every compiled arrow.
    //
    // THE RETURN PROTOCOL STILL USES THE RAW ONE. ct_aot_return_value
    // substitutes `receiver` when a constructor returns a primitive, and
    // that is the frame's own receiver by definition - an arrow cannot be
    // constructed at all.
    mlir::IRMapping mapping;
    mapping.map(body.getArgument(arg_receiver),
                ec::CallOpaqueOp::create(build, where, mlir::TypeRange{value},
                                         callee("ct_aot_this"),
                                         mlir::ValueRange{frame.getResult(0)})
                    .getResult(0));
    if (mapping_callee) { mapping.map(body.getArgument(arg_callee), mapping_callee); }
    if (mapping_new_target) { mapping.map(body.getArgument(arg_new_target), mapping_new_target); }
    for (unsigned i = 0; i < declared; ++i) {
        mapping.map(body.getArgument(implicit_arguments + i), parameters[i]);
    }
    mapping.map(entered.getResult(), frame.getResult(0));
    mapping.map(&body, running);

    for (mlir::Block & block : llvm::drop_begin(function.getBody())) {
        llvm::SmallVector<mlir::Type> types;
        llvm::SmallVector<mlir::Location> places;
        for (const mlir::BlockArgument argument : block.getArguments()) {
            types.push_back(as_emitc(argument.getType(), value));
            places.push_back(argument.getLoc());
        }
        mlir::Block * fresh = entry.addBlock();
        fresh->addArguments(types, places);
        mapping.map(&block, fresh);
        for (auto [before, after] : llvm::zip(block.getArguments(), fresh->getArguments())) {
            mapping.map(before, after);
        }
    }

    // ---- and their operations -----------------------------------------
    compiled_entry scope{
        entry,
        frame.getResult(0),
        in_receiver,
        in_constructing,
        in_out,
        value,
        status,
        nullptr,
        scope_slots,
        windows,
        literal(build, where, pointer_to(context, "const ctbrowser::aot::ct_aot_site"),
                "reinterpret_cast<const ctbrowser::aot::ct_aot_site *>(&" + marker + ")"),
        in_site,
        memo_slots};

    // THE PARAMETERS ARE ROOTED FIRST, before anything can collect. They
    // were read out of argv before the frame existed - argv dies at
    // ct_aot_enter - so this is the earliest moment they can be.
    for (unsigned i = 0; i < declared; ++i) {
        park_if_tracked(scope, build, where, body.getArgument(implicit_arguments + i),
                        parameters[i]);
    }

    for (mlir::Block & block : function.getBody()) {
        build.setInsertionPointToEnd(mapping.lookup(&block));
        scope.caught_target = nullptr;
        scope.caught_operands.clear();
        if (!block.empty()) {
            if (auto guarded = mlir::dyn_cast<CheckOp>(block.back())) {
                scope.caught_target = mapping.lookup(guarded.getHandler());
                for (const mlir::Value each : guarded.getHandlerOperands()) {
                    // NON-NULL BY CONSTRUCTION: body_is_supported refused
                    // this function unless every handler operand is one of
                    // THIS block's arguments, and every block's arguments
                    // are mapped before any block is converted.
                    scope.caught_operands.push_back(mapping.lookup(each));
                }
            }
        }
        for (mlir::Operation & op : block) {
            convert(op, build, mapping, scope);
            // EVERY VALUE THIS OPERATION PRODUCED, ROOTED IMMEDIATELY -
            // after convert rather than inside it, so that no conversion
            // can forget. Same reason the ABI shape check is a trait on the
            // base class rather than a verifier written per operation.
            for (const mlir::Value produced : op.getResults()) {
                if (!mapping.contains(produced)) { continue; }
                park_if_tracked(scope, build, op.getLoc(), produced, mapping.lookup(produced));
            }
        }
    }

    function.erase();
    return true;
}

// ONE CTJS TYPE, IN C++. Only !ctjs.value needs translating; an i1 from
// ctjs.truthy is already a machine bit and EmitC prints it as `bool`.
mlir::Type lowering::as_emitc(mlir::Type type, mlir::Type value) {
    return mlir::isa<ValueType>(type) ? value : type;
}

} // namespace ctcompile::ctjs::emitc_detail
