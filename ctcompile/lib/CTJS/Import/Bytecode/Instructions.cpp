//===- Instructions.cpp - Per-opcode import -------------------------------===//
//
// importInstruction: translates one ctbrowser bytecode instruction into ctjs
// operations, or gives up on the function when it cannot do so exactly.
// Operand fields are the VM's: `a` is usually the destination register and
// `b`/`c` the sources, with per-opcode exceptions noted at each case.
//
//===----------------------------------------------------------------------===//
#include "OperatorTables.h"

#include "llvm/ADT/Sequence.h"

namespace ctcompile::js::bytecode_detail {

void importInstruction(function_importer & state, mlir::Block * entry, std::size_t at) {
    mlir::OpBuilder & into = state.builder;
    mlir::MLIRContext * context = state.context;
    const function_proto & proto = state.proto;
    const instruction & in = proto.code[at];
    const mlir::Location where = state.location_for(at);
    const auto value_type = ctjs::ValueType::get(context);
    const auto reg = [&](std::uint16_t slot) -> mlir::Value {
        return slot < state.registers.size() ? state.registers[slot] : mlir::Value{};
    };
    const auto set = [&](std::uint16_t slot, mlir::Value v) { state.write(slot, v); };
    // The `in.b` arguments are the registers above the callee, the
    // interpreter's own layout: `arg_base = base + in.a + 1`. The callee's
    // frame starts where its arguments already are, which is why the bytecode
    // puts them there. False after giving up on one outside the frame.
    const auto arguments_above = [&](llvm::SmallVector<mlir::Value> & args) {
        for (unsigned i = 0; i < in.b; ++i) {
            const auto slot = static_cast<std::uint16_t>(in.a + 1 + i);
            if (slot >= state.registers.size()) {
                state.give_up(at, in.code, "an argument register is outside the frame");
                return false;
            }
            args.push_back(reg(slot));
        }
        return true;
    };

    // The operator tables first: each row is one ctjs operation over b and c.
    for (const binary_row & row : binary_rows) {
        if (row.code != in.code) { continue; }
        const auto kind = ctjs::BinaryKindAttr::get(context, row.kind);
        set(in.a,
            row.re_entering
                ? ctjs::BinaryOp::create(into, where, value_type, kind, reg(in.b), reg(in.c))
                      .getResult()
                : ctjs::BinaryStaticOp::create(into, where, value_type, kind, reg(in.b), reg(in.c))
                      .getResult());
        return;
    }
    for (const compare_row & row : compare_rows) {
        if (row.code != in.code) { continue; }
        mlir::Value made = ctjs::CompareOp::create(into, where, value_type,
                                                   ctjs::CompareKindAttr::get(context, row.kind),
                                                   reg(in.b), reg(in.c));
        if (row.negate) {
            made = ctjs::UnaryOp::create(into, where, value_type,
                                         ctjs::UnaryKindAttr::get(context, ctjs::UnaryKind::Not),
                                         made);
        }
        set(in.a, made);
        return;
    }
    for (const unary_row & row : unary_rows) {
        if (row.code != in.code) { continue; }
        set(in.a, ctjs::UnaryOp::create(into, where, value_type,
                                        ctjs::UnaryKindAttr::get(context, row.kind), reg(in.b)));
        return;
    }

    switch (in.code) {
    case op::load_undef: set(in.a, state.undefined(where)); break;
    case op::load_null: set(in.a, state.constant(where, ctjs::NullAttr::get(context))); break;
    case op::load_true:
        set(in.a, state.constant(where, ctjs::BooleanAttr::get(context, true)));
        break;
    case op::load_false:
        set(in.a, state.constant(where, ctjs::BooleanAttr::get(context, false)));
        break;
    case op::load_string:
        if (in.bx() >= proto.strings.size()) {
            state.give_up(at, in.code, "string index out of range");
            break;
        }
        set(in.a, state.constant(where, ctjs::StringAttr::get(context, proto.strings[in.bx()])));
        break;
    case op::load_const: {
        if (in.bx() >= proto.constants.size()) {
            state.give_up(at, in.code, "constant index out of range");
            break;
        }
        const value k = proto.constants[in.bx()];
        if (k.is_number()) {
            set(in.a, state.constant(where, ctjs::numberAttr(context, k.as_number())));
        } else if (k.is_boolean()) {
            set(in.a, state.constant(where, ctjs::BooleanAttr::get(context, k.as_boolean())));
        } else if (k.is_undefined()) {
            set(in.a, state.undefined(where));
        } else if (k.is_null()) {
            set(in.a, state.constant(where, ctjs::NullAttr::get(context)));
        } else {
            // A constant-pool entry kind the importer does not support. Give
            // up rather than guess: a guess would produce a function that
            // runs and computes the wrong thing.
            state.give_up(at, in.code,
                          "constant is not a number, boolean, null or "
                          "undefined");
        }
        break;
    }
    case op::move: set(in.a, reg(in.b)); break;
    case op::load_this: set(in.a, entry->getArgument(arg_receiver)); break;
    case op::load_new_target: set(in.a, entry->getArgument(arg_new_target)); break;
    case op::load_callee: set(in.a, entry->getArgument(arg_callee)); break;
    // One operation, not arg_callee and then a lookup. The callee is a block
    // argument because the ABI hands it to the entry; __home is a property on
    // that closure, and reading it is the helper's job. The helper's ABI entry
    // has no throw, re-entry or safepoint flag because closure_object::find
    // touches no accessors.
    case op::load_home: set(in.a, ctjs::LoadHomeOp::create(into, where, value_type)); break;
    case op::pass_new_target: ctjs::PassNewTargetOp::create(into, where); break;
    case op::get_proto:
        set(in.a, ctjs::GetProtoOp::create(into, where, value_type, reg(in.b)));
        break;
    case op::set_proto:
        // `a` is the target, `b` the new prototype, and nothing is written
        // back.
        ctjs::SetProtoOp::create(into, where, reg(in.a), reg(in.b));
        break;
    case op::closure: {
        // Creates a closure for nested function `bx` of this program.
        if (in.bx() >= state.prog.functions.size()) {
            state.give_up(at, in.code, "closure function index out of range");
            break;
        }
        const function_proto & target = state.prog.functions[in.bx()];
        // Capture operands run in parallel with the target's upvalue
        // descriptors, not packed. The helper reads only the entries the
        // compiler marked from_parent_local; it fills the rest from the
        // enclosing closure, and undefined here is a placeholder that is never
        // read. A packed list would silently capture the wrong bindings.
        //
        // The `enclosing_indices` attribute records which upvalue of the
        // enclosing closure fills each placeholder slot: the descriptor's
        // `up.index`, or -1 where the operand itself is the cell. The VM
        // copies that cell into the slot (context::make_closure in
        // ctbrowser/lib/Script/vm/call/coroutines.cpp), and op::get_upvalue
        // reads the cell's value. ct_aot_make_closure ignores both placeholder
        // and attribute because it walks the same descriptors. Native closure
        // lifting reads the attribute: once this function is lifted, upvalue
        // `up.index` is one of its capture parameters, holding a cell value
        // an outer frame proved constant, and the nested closure captures that
        // same constant.
        //
        // An attribute, not a live ctjs.load_upvalue operand: CTJSToEmitC
        // passes every capture operand to ct_aot_make_closure, so an operand
        // would cost a runtime upvalue read that is then ignored (on the
        // Bootstrap workload, about a fifth of all capture operands; recount
        // with tools/check/capture-census.py).
        //
        // An out-of-range index becomes -1 with the placeholder alone, as in
        // the VM (`up.index < enclosing->upvalues.size()`, else undefined):
        // the enclosing frame holds no such cell, so there is nothing to name.
        llvm::SmallVector<mlir::Value> captured;
        llvm::SmallVector<std::int32_t> from_enclosing;
        captured.reserve(target.upvalues.size());
        from_enclosing.reserve(target.upvalues.size());
        bool reachable = true;
        bool any_from_enclosing = false;
        for (const upvalue_desc & up : target.upvalues) {
            if (!up.from_parent_local) {
                const bool in_range = up.index < proto.upvalues.size();
                captured.push_back(state.undefined(where));
                from_enclosing.push_back(in_range ? static_cast<std::int32_t>(up.index) : -1);
                any_from_enclosing = any_from_enclosing || in_range;
                continue;
            }
            if (up.index >= state.registers.size()) {
                state.give_up(at, in.code, "upvalue names a register past the frame");
                reachable = false;
                break;
            }
            captured.push_back(reg(up.index));
            from_enclosing.push_back(-1);
        }
        if (!reachable) { break; }
        // Pass the receiver only when the target is an arrow function. The VM
        // reads the enclosing `this` only for arrows (make_closure in
        // vm/call/coroutines.cpp: `if (target.is_arrow) { made->captured_this
        // = ... }`). Passing it for ordinary functions would be a use of
        // %arg0, which makes admission refuse the enclosing function for
        // "uses `this`" even when its source never mentions `this`.
        //
        // The undefined operand is also the only place the IR records whether
        // the target is an arrow; native closure lifting
        // (CTNative/Lowering/ClosureLifting/) relies on it to refuse arrows
        // that read their lexical `this`.
        set(in.a, ctjs::CreateClosureOp::create(
                      into, where, value_type, entry->getArgument(arg_callee),
                      target.is_arrow ? entry->getArgument(arg_receiver) : state.undefined(where),
                      into.getI32IntegerAttr(static_cast<std::int32_t>(in.bx())), captured,
                      // Absent when no slot is filled from the enclosing
                      // closure, which is most closures: an all -1 list says
                      // nothing the missing attribute does not, and would be
                      // noise on every create_closure in a bundle.
                      any_from_enclosing ? into.getDenseI32ArrayAttr(from_enclosing)
                                         : mlir::DenseI32ArrayAttr{}));
        break;
    }
    case op::get_global:
    case op::get_global_typeof:
        if (const std::string * name = state.name_at(at, in.code, in.bx())) {
            set(in.a,
                ctjs::LoadGlobalOp::create(into, where, value_type, into.getStringAttr(*name),
                                           into.getBoolAttr(in.code == op::get_global_typeof)));
        }
        break;
    case op::set_global:
        if (const std::string * name = state.name_at(at, in.code, in.bx())) {
            ctjs::StoreGlobalOp::create(into, where, into.getStringAttr(*name), reg(in.a));
        }
        break;
    case op::get_prop:
        if (const std::string * name = state.name_at(at, in.code, in.c)) {
            const mlir::Value key = state.key(where, *name);
            set(in.a, ctjs::GetPropertyOp::create(into, where, value_type, reg(in.b), key));
        }
        break;
    case op::set_prop:
        if (const std::string * name = state.name_at(at, in.code, in.b)) {
            const mlir::Value key = state.key(where, *name);
            ctjs::SetPropertyOp::create(into, where, reg(in.a), key, reg(in.c));
        }
        break;
    case op::get_index:
        set(in.a, ctjs::GetPropertyOp::create(into, where, value_type, reg(in.b), reg(in.c)));
        break;
    case op::has_property:
        // `b` is the key and `c` the target, the reverse of the operation's
        // operand order. Swapping them answers about the wrong object.
        set(in.a, ctjs::HasPropertyOp::create(into, where, value_type, reg(in.c), reg(in.b)));
        break;
    case op::instance_of:
        // Boxed here, because ctjs.instanceof deliberately returns an i1.
        set(in.a, ctjs::FromBoolOp::create(into, where, value_type,
                                           ctjs::InstanceOfOp::create(into, where, into.getI1Type(),
                                                                      reg(in.b), reg(in.c))));
        break;
    case op::delete_index:
        // `a` is the target here, not the destination: delete_index
        // produces nothing and writes no register.
        ctjs::DeletePropertyOp::create(into, where, reg(in.a), reg(in.b));
        break;
    case op::push_handler: {
        const auto targets = state.branch_targets(at, in, "handler target is not a block leader");
        if (!targets) { break; }
        const auto [pad, body] = *targets;
        // The catch pad must be reachable only by throwing. ct_aot_catch_land
        // reads what the unwinder wrote and then clears the pad marker (its
        // ABI entry says "called exactly once, only after CT_AOT_CAUGHT");
        // reached on a normal path it would bind a value that was never
        // thrown. A pad can be reached normally by falling through from the
        // instruction above it or by a jump that skips an empty catch clause
        // (`try { f(); } catch (e) {}`), so give up if any instruction in the
        // function has the pad as a successor.
        const std::int64_t landing = jump_target(at, in);
        const bool reachable_without_throwing =
            llvm::any_of(llvm::seq<std::size_t>(0, proto.code.size()), [&](std::size_t other) {
                return llvm::is_contained(successors(other, proto.code[other]), landing);
            });
        if (reachable_without_throwing) {
            state.give_up(at, in.code,
                          "the catch clause is reachable without throwing, which would "
                          "run ct_aot_catch_land on a normal path");
            break;
        }
        // A terminator with two successors. The handler edge exists only to
        // keep the pad reachable; the edge the emitted code takes is
        // ctjs.check's, at the throw site, because only there does the live
        // register vector exist.
        ctjs::PushHandlerOp::create(into, where, state.outgoing(), state.outgoing(), body, pad);
        state.handlers.push_back(function_importer::open_handler{pad, in.a});
        // Do not enter `body` here. push_handler ends a block, so `body` is a
        // leader and the next iteration's enter_block enters it and rebinds
        // the registers. Entering it here too would make enter_block see
        // `previous == body` and emit a self-branch, which fails
        // verification.
        break;
    }
    case op::pop_handler:
        // An empty handler stack means this pop is unreachable:
        // import_program's handler dataflow refused every reachable pop with
        // no open handler. The usual one is
        // the normal-path pop after a try body that always returns through a
        // finally, whose return popped the handler first.
        ctjs::PopHandlerOp::create(into, where);
        if (!state.handlers.empty()) { state.handlers.pop_back(); }
        break;
    case op::make_arguments:
        set(in.a, ctjs::MakeArgumentsOp::create(into, where, value_type));
        break;
    case op::gather_rest:
        // `b` is a count (how many parameters were declared before the rest
        // one), not a register. The emitter spells it `{gather_rest, i, i}`
        // with both fields equal, which makes a mutation from b to a
        // invisible to tests; see ctcompile/docs/plans/arguments-and-rest.md.
        set(in.a,
            ctjs::GatherRestOp::create(into, where, value_type, static_cast<std::uint32_t>(in.b)));
        break;
    case op::load_bigint:
        if (in.bx() >= proto.strings.size()) {
            state.give_up(at, in.code, "bigint literal index out of range");
            break;
        }
        // Literal spelling after the compiler strips the source `n` suffix.
        // bigint_from_literal owns radices and invalid-spelling substitution;
        // preserve these unparsed bytes instead of duplicating its parser.
        set(in.a, state.constant(where, ctjs::BigIntAttr::get(context, proto.strings[in.bx()])));
        break;
    // ---- ES modules ---------------------------------------------
    //
    // Three of these four exist only in functions[0] of a module program.
    // compile_program emits load_import, bind_export and
    // load_namespace from its `if (module_scope_)` arm and from
    // nowhere else, so a classic script cannot contain one and a
    // fixture that means to reach them has to be compiled with
    // script_kind::module. dyn_import is the exception: `import(x)` is
    // an expression and appears in ordinary functions.
    case op::load_import:
        // `b` is the export name and `c` the specifier, the reverse of the
        // reading order; this is the only opcode that reads c as a
        // standalone index. bytecode.hpp says the
        // same thing from the other side: "a = the cell exported as
        // names[b] by the module at specifier names[c]". Filling the
        // operation in reading order compiles and raises `module
        // `count` was not loaded` at run time.
        //
        // The result is a cell and must not be boxed again: the compiler
        // marked this local boxed and emitted no new_cell, so every later
        // read is already a ctjs.cell_get.
        if (const std::string *exported = state.name_at(at, in.code, in.b),
            *specifier = state.name_at(at, in.code, in.c);
            exported != nullptr && specifier != nullptr) {
            set(in.a, ctjs::ModuleImportCellOp::create(into, where, value_type,
                                                       into.getStringAttr(*specifier),
                                                       into.getStringAttr(*exported)));
        }
        break;
    case op::bind_export:
        // bx(), not b. b is the high half of the pair, so it is 0 for every
        // module with fewer than 65,536 names, and a decoder that reads b
        // alone would silently publish name 0 under this name.
        //
        // reg(in.a) is a source as well as the destination. The write
        // is conditional on a module being evaluated, and outside one
        // the register keeps the local being exported - so the
        // operation carries that value and answers it back rather than
        // answering undefined and destroying the local.
        if (const std::string * name = state.name_at(at, in.code, in.bx())) {
            set(in.a, ctjs::ModuleExportCellOp::create(into, where, value_type,
                                                       into.getStringAttr(*name), reg(in.a)));
        }
        break;
    case op::load_namespace:
        // A plain 16-bit b here, where bind_export uses the bx pair: three
        // module opcodes, two encodings for one name table.
        if (const std::string * name = state.name_at(at, in.code, in.b)) {
            set(in.a, ctjs::ModuleNamespaceOp::create(into, where, value_type,
                                                      into.getStringAttr(*name)));
        }
        break;
    case op::dyn_import:
        // `b` is a register, not a name index: the specifier is computed,
        // which is the whole difference from load_namespace.
        set(in.a, ctjs::DynamicImportOp::create(into, where, value_type, reg(in.b)));
        break;
    case op::delete_prop:
        // `a` is the target, `b` names the property, and nothing is written
        // back: the delete produces no value.
        if (const std::string * name = state.name_at(at, in.code, in.b)) {
            ctjs::DeleteNamedOp::create(into, where, reg(in.a), into.getStringAttr(*name));
        }
        break;
    case op::own_keys:
        set(in.a, ctjs::OwnKeysOp::create(into, where, value_type, reg(in.b)));
        break;
    // An async function's `return`; `a` is both source and destination. The
    // compiler emits `{op::wrap_promise, r}` over the register the return
    // value is already in, and b and c are unused.
    case op::wrap_promise:
        set(in.a, ctjs::WrapPromiseOp::create(into, where, value_type, reg(in.a)));
        break;
    case op::define_getter:
    case op::define_setter: {
        // `a` is the target, `b` names the property and `c` is the function.
        // Only the opcode says whether it is the getter or the setter, so
        // that is resolved here and the operation carries both slots.
        const std::string * name = state.name_at(at, in.code, in.b);
        if (name == nullptr) { break; }
        const bool getter = in.code == op::define_getter;
        const mlir::Value nothing = state.undefined(where);
        ctjs::DefineAccessorOp::create(into, where, reg(in.a), into.getStringAttr(*name),
                                       getter ? reg(in.c) : nothing, getter ? nothing : reg(in.c));
        break;
    }
    case op::copy_props:
        // `a` is the target and is not written back: the object is mutated
        // in place, so this produces no value.
        ctjs::CopyPropsOp::create(into, where, reg(in.a), reg(in.b));
        break;
    case op::apply:
        // `a` is both the callee and the destination, `b` the argument array
        // and `c` the receiver, so all three are read before writing.
        set(in.a,
            ctjs::CallSpreadOp::create(into, where, value_type, reg(in.a), reg(in.b), reg(in.c)));
        break;
    case op::construct_apply:
        set(in.a, ctjs::ConstructSpreadOp::create(into, where, value_type, reg(in.a), reg(in.b)));
        break;
    case op::iterable:
        // `b` in, `a` out. Both bytecode emitters spell it
        // `{iterable, source, source}` with a and b aliased, which reading b
        // and then writing a handles.
        set(in.a, ctjs::IterableOp::create(into, where, value_type, reg(in.b)));
        break;
    case op::set_index:
        ctjs::SetPropertyOp::create(into, where, reg(in.a), reg(in.b), reg(in.c));
        break;
    case op::define_data:
        // An object literal's own property. It is marked so the VM-backed C++
        // backend defines the property (no setter lookup) instead of
        // assigning it; the native analyses also rely on this marker.
        ctjs::SetPropertyOp::create(into, where, reg(in.a), reg(in.b), reg(in.c))
            ->setAttr(ctjs::defineOwnMarker, into.getUnitAttr());
        break;
    case op::new_object: set(in.a, ctjs::CreateObjectOp::create(into, where, value_type)); break;
    case op::new_cell:
        // In place: the bytecode boxes the register's current value and
        // leaves the box where the value was.
        set(in.a, ctjs::CreateCellOp::create(into, where, value_type, reg(in.a)));
        break;
    case op::cell_get:
        set(in.a, ctjs::CellGetOp::create(into, where, value_type, reg(in.b)));
        break;
    case op::cell_set: ctjs::CellSetOp::create(into, where, reg(in.a), reg(in.b)); break;
    case op::get_upvalue:
        // Reads through the frame's own closure, which arrives as the third
        // implicit argument. The interpreter reads vm_frame->closure; a compiled
        // body is handed the same thing.
        set(in.a,
            ctjs::LoadUpvalueOp::create(into, where, value_type, entry->getArgument(arg_callee),
                                        into.getI32IntegerAttr(in.b)));
        break;
    case op::set_upvalue:
        ctjs::StoreUpvalueOp::create(into, where, entry->getArgument(arg_callee),
                                     into.getI32IntegerAttr(in.a), reg(in.b));
        break;
    case op::new_array:
        set(in.a,
            ctjs::CreateArrayOp::create(into, where, value_type, llvm::SmallVector<mlir::Value>{}));
        break;
    case op::append: ctjs::AppendOp::create(into, where, reg(in.a), reg(in.b)); break;
    case op::call:
    case op::call_method:
    case op::call_computed:
    case op::call_receiver: {
        mlir::Value callee = reg(in.a);
        mlir::Value receiver = state.undefined(where);
        if (in.code == op::call_receiver) {
            receiver = reg(in.c);
        } else if (in.code == op::call_method) {
            const std::string * name = state.name_at(at, in.code, in.c);
            if (name == nullptr) { break; }
            // Through the same lookup as get_prop, as the interpreter
            // requires: `s.split(...)` and
            // `var f = s.split; f(...)` must find the same function.
            receiver = reg(in.a);
            const mlir::Value key = state.key(where, *name);
            callee = ctjs::GetPropertyOp::create(into, where, value_type, receiver, key);
        } else if (in.code == op::call_computed) {
            receiver = reg(in.a);
            callee = ctjs::GetPropertyOp::create(into, where, value_type, receiver, reg(in.c));
        }
        llvm::SmallVector<mlir::Value> args;
        if (!arguments_above(args)) { break; }
        set(in.a, ctjs::CallOp::create(into, where, value_type, callee, receiver, args));
        break;
    }
    case op::construct: {
        llvm::SmallVector<mlir::Value> args;
        if (!arguments_above(args)) { break; }
        // new.target is the callee for a plain `new C()`. A super() call
        // hands a different one along, and that is op::pass_new_target's
        // business rather than this opcode's.
        set(in.a, ctjs::ConstructOp::create(into, where, value_type, reg(in.a), reg(in.a), args));
        break;
    }
    case op::jump_if_not_nullish:
    case op::jump_if_defined: {
        const auto targets = state.branch_targets(at, in, "branch target is not a block leader");
        if (!targets) { break; }
        const auto [taken, fallthrough] = *targets;
        // Not truthiness. `jump_if_defined` branches when the value is not
        // `undefined` (0, "" and false all qualify), and `jump_if_not_nullish`
        // when it is neither null nor undefined. Lowering either through
        // ctjs.truthy would send `x ?? y` down the wrong arm for every falsy
        // x.
        const mlir::Value sentinel = in.code == op::jump_if_defined
                                         ? state.undefined(where)
                                         : state.constant(where, ctjs::NullAttr::get(context));
        const auto kind =
            in.code == op::jump_if_defined ? ctjs::CompareKind::StrictEq : ctjs::CompareKind::Eq;
        const mlir::Value matches =
            ctjs::CompareOp::create(into, where, value_type,
                                    ctjs::CompareKindAttr::get(context, kind), reg(in.a), sentinel);
        const mlir::Value bit = ctjs::TruthyOp::create(into, where, into.getI1Type(), matches);
        const auto operands = state.outgoing();
        // The comparison is true when the value is the sentinel, and both
        // opcodes jump when it is not, so the arms are swapped.
        mlir::cf::CondBranchOp::create(into, where, bit, fallthrough, operands, taken, operands);
        break;
    }
    case op::jump: {
        mlir::Block * target = state.block_at(jump_target(at, in));
        if (target == nullptr) {
            state.give_up(at, in.code, "jump target is not a block leader");
            break;
        }
        mlir::cf::BranchOp::create(into, where, target, state.outgoing());
        break;
    }
    case op::jump_if_false:
    case op::jump_if_true: {
        const auto targets = state.branch_targets(at, in, "branch target is not a block leader");
        if (!targets) { break; }
        const auto [taken, fallthrough] = *targets;
        // ctjs.truthy is the only bridge from a value to a branch: cf.cond_br
        // takes an i1, and ctjs.convert to_boolean would produce a
        // JavaScript boolean instead.
        const mlir::Value bit = ctjs::TruthyOp::create(into, where, into.getI1Type(), reg(in.a));
        const auto operands = state.outgoing();
        const bool jumps_on_true = in.code == op::jump_if_true;
        mlir::cf::CondBranchOp::create(into, where, bit, jumps_on_true ? taken : fallthrough,
                                       operands, jumps_on_true ? fallthrough : taken, operands);
        break;
    }
    case op::ret:
    case op::ret_undef:
    case op::halt: {
        const mlir::Value returned = in.code == op::ret ? reg(in.a) : state.undefined(where);
        state.emit_return(where, returned);
        break;
    }
    case op::throw_value: ctjs::ThrowOp::create(into, where, reg(in.a)); break;
    default: state.give_up(at, in.code, "no CTJS operation for this opcode yet"); break;
    }
}

} // namespace ctcompile::js::bytecode_detail
