//===- ctcompile/lib/CTJS/Lowering/EmitC/Functions.cpp - Lower ctjs.func --===//
//
// lowering::lower: replaces one ctjs.func with an extern "C" emitc.func shaped
// like ct_aot_entry_fn. It copies the parameters out of argv, enters the
// frame, reserves a frame slot for every value it will produce, maps the
// implicit arguments, and converts each block's operations.
//
//===----------------------------------------------------------------------===//
#include "Lowering.h"

namespace ctcompile::ctjs::emitc_detail {

/// Lowers `function`, or marks it refused. Returns whether it was replaced.
bool lowering::lower(FuncOp function, mlir::OpBuilder & build, mlir::MLIRContext * context) {
    const mlir::Location where = function.getLoc();

    // Erase unreachable blocks first. A body that ends in an explicit `return`
    // still gets the importer's implicit `return undefined` epilogue after it,
    // and nothing branches there. Removing it is an ordinary simplification,
    // not a decision about semantics.
    mlir::IRRewriter prune(context);
    (void)mlir::eraseUnreachableBlocks(prune, function.getBody());

    // Multi-block bodies are accepted. --emitc-eliminate-block-arguments runs
    // after this pass and turns block arguments into variables, because the
    // C++ emitter miscompiles block-argument copies. That pipeline order is
    // required; end-to-end.mlir runs it.

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
    // Both implicit arguments (callee and new.target) are supported.
    // ct_aot_callee is how a compiled function reaches its upvalues, which
    // live on the closure instance (`site` is the function_proto shared by
    // every closure of the function). ct_aot_new_target reads the frame's
    // new.target, which ct_aot_enter takes from pending_new_target_; both
    // context::construct_new and VM_CASE(construct)'s compiled arm set it.
    // Babel's _classCallCheck tests new.target, so transpiled classes depend
    // on it.

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

    // The address this function's string literals memoise under, declared
    // before the function that takes it. One byte per compiled function,
    // never read; only its address matters, and it has to differ from the
    // function_proto the interpreter keys the same cache by. See
    // compiled_entry::memo_site.
    const std::string marker = "ctc_memo_" + c_identifier(function.getName().str());
    ec::VerbatimOp::create(build, where,
                           build.getStringAttr("static const char " + marker + " = 0;"));

    auto entry = ec::FuncOp::create(build, where, c_identifier(function.getName()),
                                    build.getFunctionType(inputs, {status}));
    // extern "C" so the symbol matches ct_aot_entry_fn's expectations.
    entry.setSpecifiersAttr(build.getStrArrayAttr({"extern \"C\""}));

    mlir::Block * abi = entry.addEntryBlock();
    build.setInsertionPointToStart(abi);
    const mlir::Value in_ctx = abi->getArgument(0);
    const mlir::Value in_site = abi->getArgument(1);
    const mlir::Value in_argv = abi->getArgument(2);
    const mlir::Value in_receiver = abi->getArgument(4);
    const mlir::Value in_constructing = abi->getArgument(5);
    const mlir::Value in_out = abi->getArgument(6);

    // ---- the parameters, copied before the frame exists ---------------
    //
    // argv is an interior pointer into context::registers_ and ct_aot_enter
    // resizes that vector, so every read has to happen first. The caller has
    // already filled the callee's window, with missing parameters set to
    // undefined, so argv[i] is defined for every declared parameter however
    // many arguments actually arrived.
    const unsigned declared = parameterCount(body);
    llvm::SmallVector<mlir::Value> parameters;
    parameters.reserve(declared);
    //
    // The parameter stays `const uint64_t *` and the read casts the const
    // away. The signature is fixed: this function must be assignable to
    // ct_aot_entry_fn, which declares `const uint64_t *argv`. But
    // emitc.subscript requires the result type to equal the pointee type, so
    // reading through it yields a `const uint64_t`, and under the required
    // --declare-variables-at-top EmitC declares every value at the top and
    // assigns later: `const uint64_t v8; v8 = ...;` does not compile.
    //
    // So the pointer is cast once, rather than each element. The result is
    // only ever read (argv is the caller's window, and writing to it would
    // corrupt the caller's registers), so the cast is a spelling concession.
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
    // The frame storage is CT_AOT_FRAME_BYTES of caller-allocated space,
    // sized from the runtime header's macro rather than a literal. Its element
    // type is kFrameStorageElement (`alignas(std::max_align_t) unsigned
    // char`): a plain `unsigned char[N]` is aligned to 1, and ct_aot_enter
    // placement-constructs an aot_frame_storage (a pointer and three indices)
    // in it. aot_bridge/internal.hpp asserts the size but not the alignment,
    // and an under-aligned placement is undefined behaviour.
    //
    // The array is passed directly: it decays to `unsigned char *` and
    // converts to the `void *` parameter, so no subscript, address-of or
    // size_t is needed (!emitc.size_t emits a bare `size_t` that does not
    // compile).
    const auto block_type = ec::ArrayType::get({static_cast<std::int64_t>(CT_AOT_FRAME_BYTES)},
                                               opaque(context, kFrameStorageElement));
    auto storage =
        ec::VariableOp::create(build, where, block_type, ec::OpaqueAttr::get(context, ""));
    // The register window has to hold the roots too.
    //
    // proto.frame_size sizes the interpreter's register file, and this
    // backend needs one slot for every JavaScript value it produces -
    // because a value in a C++ local is invisible to a precise collector.
    // The two are counted together and the larger wins: asking for more
    // than frame_size is safe (ct_aot_enter uses the number verbatim and
    // fills with undefined), asking for less makes ct_aot_slots hand back a
    // null span far from the cause.
    //
    // Slots are never reused. Liveness analysis could pack them, but keeping
    // a dead value alive until the frame exits is a bounded leak, while
    // getting liveness wrong is a use-after-free.
    unsigned parked = 0;
    for (unsigned i = 0; i < declared; ++i) {
        scope_slots[body.getArgument(implicit_arguments + i)] = parked++;
    }
    llvm::DenseMap<mlir::Operation *, unsigned> windows;
    // A memo slot per string literal. Not a frame slot: the cache
    // ct_aot_new_string keys by (site, slot) is a map, so these only have
    // to be unique within the function and stable across calls.
    llvm::DenseMap<mlir::Operation *, unsigned> memo_slots;
    unsigned next_memo = 0;
    function.getBody().walk([&](mlir::Operation * inner) {
        for (const mlir::Value result : inner->getResults()) {
            if (mlir::isa<ValueType>(result.getType())) { scope_slots[result] = parked++; }
        }
        const auto reserve = [&](std::size_t count) {
            windows[inner] = parked;
            parked += static_cast<unsigned>(count);
        };
        // A contiguous run per call site, reserved here so the register
        // window can include it before ct_aot_enter is emitted. A closure's
        // upvalue array is the same kind of run, and must also live in the
        // frame rather than in C++ locals, because ct_aot_make_closure
        // allocates and is therefore a safepoint.
        if (const auto run = window_operands(*inner)) { reserve(run->size()); }
        // One slot per protected region, where the unwinder puts the thrown
        // value: context::unwind_to_handler writes
        // registers_[base + slot] and ct_aot_catch_land reads it back. It
        // has to be inside the run ct_aot_enter reserves, so it is assigned
        // here with everything else rather than invented at the call.
        if (mlir::isa<PushHandlerOp>(inner)) { reserve(1); }
        if (auto constant = mlir::dyn_cast<ConstantOp>(inner)) {
            // A BigInt literal memoises the same way and shares the
            // numbering. Both helpers key on (site, slot) and this backend
            // hands them the same marker, so one counter across both keeps
            // a string and a bigint from ever claiming the same slot.
            if (mlir::isa<StringAttr, BigIntAttr>(constant.getValue())) {
                memo_slots[inner] = next_memo++;
            }
        }
    });
    const unsigned window = std::max(static_cast<unsigned>(entered.getRegCount()), parked);
    const mlir::Value registers = u32_literal(build, where, window);
    const mlir::Value frame = opaque_call(build, where, frame_ptr, "ct_aot_enter",
                                          {in_ctx, in_site, registers, in_receiver, storage});

    // ct_aot_enter reports failure with a null pointer, not a status. There is no frame, so
    // there is nothing to leave and no handler to reach.
    const mlir::Value null = literal(build, where, frame_ptr, "nullptr");
    auto ok = ec::CmpOp::create(build, where, mlir::IntegerType::get(context, 1),
                                ec::CmpPredicate::ne, frame, null);

    mlir::Block * raised = entry.addBlock();
    mlir::Block * running = entry.addBlock();
    mlir::cf::CondBranchOp::create(build, where, ok, running, mlir::ValueRange{}, raised,
                                   mlir::ValueRange{});

    build.setInsertionPointToStart(raised);
    ec::ReturnOp::create(build, where, status_literal(build, where, status, "failed"));

    // ---- the blocks ---------------------------------------------------
    //
    // Every ctjs block gets one here, arguments and all. They are not
    // eliminated in this pass: --emitc-eliminate-block-arguments does that
    // afterwards, so the elimination can be tested on its own against a
    // program that runs.
    //
    // The closure exists only once the frame does. ct_aot_callee reads
    // call_frame::closure, so it cannot be hoisted above ct_aot_enter the way
    // the parameter reads were. It must be emitted before the mapping is
    // built: mapping a block argument to a still-null Value is accepted
    // silently and crashes later inside Operation::create.
    build.setInsertionPointToEnd(running);
    if (!body.getArgument(arg_callee).use_empty()) {
        mapping_callee = opaque_call(build, where, value, "ct_aot_callee", {frame});
    }
    // new.target, the same way and for the same reason. It reads
    // call_frame::new_target, so it cannot be hoisted above ct_aot_enter
    // either, and it must be emitted before the mapping is built.
    if (!body.getArgument(arg_new_target).use_empty()) {
        mapping_new_target = opaque_call(build, where, value, "ct_aot_new_target", {frame});
    }

    // Argument 0 is the effective receiver (ct_aot_this), not the entry's raw
    // one. The importer maps op::load_this to this argument, and
    // VM_CASE(load_this) is `effective_this(*vm_frame)`, which returns the
    // enclosing method's object when the frame's closure is an arrow and the
    // frame's own receiver otherwise. The raw `receiver` would be wrong for
    // every compiled arrow.
    //
    // The return protocol still uses the raw one: ct_aot_return_value
    // substitutes `receiver` when a constructor returns a primitive, and that
    // is the frame's own receiver by definition (an arrow cannot be
    // constructed).
    mlir::IRMapping mapping;
    mapping.map(body.getArgument(arg_receiver),
                opaque_call(build, where, value, "ct_aot_this", {frame}));
    if (mapping_callee) { mapping.map(body.getArgument(arg_callee), mapping_callee); }
    if (mapping_new_target) { mapping.map(body.getArgument(arg_new_target), mapping_new_target); }
    for (unsigned i = 0; i < declared; ++i) {
        mapping.map(body.getArgument(implicit_arguments + i), parameters[i]);
    }
    mapping.map(entered.getResult(), frame);
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
        frame,
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

    // The parameters are rooted first, before anything can collect. They
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
                    // Non-null by construction: body_is_supported refused
                    // this function unless every handler operand is one of
                    // this block's arguments, and every block's arguments
                    // are mapped before any block is converted.
                    scope.caught_operands.push_back(mapping.lookup(each));
                }
            }
        }
        for (mlir::Operation & op : block) {
            convert(op, build, mapping, scope);
            // Root every value this operation produced immediately, here
            // rather than inside convert, so that no conversion can forget.
            for (const mlir::Value produced : op.getResults()) {
                if (!mapping.contains(produced)) { continue; }
                park_if_tracked(scope, build, op.getLoc(), produced, mapping.lookup(produced));
            }
        }
    }

    function.erase();
    return true;
}

/// The EmitC type for a ctjs type. Only !ctjs.value needs translating; an i1 from
// ctjs.truthy is already a machine bit and EmitC prints it as `bool`.
mlir::Type lowering::as_emitc(mlir::Type type, mlir::Type value) {
    return mlir::isa<ValueType>(type) ? value : type;
}

} // namespace ctcompile::ctjs::emitc_detail
