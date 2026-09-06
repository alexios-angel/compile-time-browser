#include "Importer.h"

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
    const auto set = [&](std::uint16_t slot, mlir::Value v) { state.assign(slot, v, at); };
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
            set(in.a,
                state.constant(where, ctjs::NumberAttr::get(
                                          context, std::bit_cast<std::uint64_t>(k.as_number()))));
        } else if (k.is_boolean()) {
            set(in.a, state.constant(where, ctjs::BooleanAttr::get(context, k.as_boolean())));
        } else if (k.is_undefined()) {
            set(in.a, state.undefined(where));
        } else if (k.is_null()) {
            set(in.a, state.constant(where, ctjs::NullAttr::get(context)));
        } else {
            // A CONSTANT POOL ENTRY THIS CUT CANNOT NAME. Abandoning is
            // the whole point: guessing would produce a function that
            // runs and computes something else.
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
    // NOT arg_callee AND THEN A LOOKUP. The callee is a block argument
    // because the ABI hands it to the entry; __home is a property ON
    // that closure, and reading it is the helper's job rather than two
    // operations here - the row's flags are all zero precisely because
    // closure_object::find touches no accessors.
    case op::load_home: set(in.a, ctjs::LoadHomeOp::create(into, where, value_type)); break;
    case op::pass_new_target: ctjs::PassNewTargetOp::create(into, where); break;
    case op::get_proto:
        set(in.a, ctjs::GetProtoOp::create(into, where, value_type, reg(in.b)));
        break;
    case op::set_proto:
        // a IS THE TARGET, b THE NEW LINK, and nothing is written back.
        ctjs::SetProtoOp::create(into, where, reg(in.a), reg(in.b));
        break;
    case op::closure: {
        // THE OPCODE THAT MADE EVERY DECLARING FUNCTION UNIMPORTABLE.
        // ctjs.create_closure's first operand was a !ctjs.program that
        // nothing produced, so this case did not exist and any function
        // declaring another was skipped whole - which is every top
        // level in a real file.
        if (in.bx() >= state.prog.functions.size()) {
            state.give_up(at, in.code, "closure function index out of range");
            break;
        }
        const function_proto & target = state.prog.functions[in.bx()];
        // IN PARALLEL WITH THE DESCRIPTORS, NOT PACKED. Only the
        // entries the compiler marked from_parent_local are read by the
        // helper; the rest it fills from the enclosing closure, and
        // undefined here is a placeholder that is never looked at. A
        // packed list would silently capture the wrong bindings.
        //
        // AND WHICH UPVALUE FILLS SUCH A SLOT GOES ON THE ATTRIBUTE
        // BESIDE THE LIST, `enclosing_indices`: the descriptor's
        // `up.index` where the placeholder stands, -1 where the operand
        // is the cell. The VM copies `enclosing->upvalues[up.index]` -
        // the CELL - into the slot (context::make_closure,
        // call.cpp:920), and every read of the slot goes through
        // op::get_upvalue, which yields `cell->slot`, the VALUE
        // (run_loop.cpp). ct_aot_make_closure reads neither the
        // placeholder nor the attribute, because it walks the same
        // descriptors and fills the slot itself. What reads the
        // attribute is the native tier's closure lift: once THIS
        // function is lifted, its upvalue `up.index` IS its capture
        // parameter, holding the initial of a cell an outer frame
        // proved constant, and the nested closure captures that same
        // constant - Phase 59 slice 1b.
        //
        // AN ATTRIBUTE RATHER THAN AN OPERAND, and the difference is
        // what the boxed tier is charged. Writing the index as a live
        // `ctjs.load_upvalue` of this frame's own closure said the same
        // thing, but CTJSToEmitC parks EVERY capture operand into
        // ct_aot_make_closure's argument window - so each one became a
        // runtime upvalue read, parked, and then ignored. Measured on
        // bootstrap: 219 of the 1,021 capture operands, 12,371 more
        // bytes of emitted C++, and no gain anywhere, the native tier
        // included.
        //
        // AND BOTH FIGURES HAVE A COMMAND. tools/check/capture-census.py
        // is the census of the operands - point it at the module this
        // function writes - and the bytes are the boxed pipeline
        // (compile-js-to-cpp.cmake) over bootstrap at 0bf7501,
        // 10,988,521, against 69ea710 and this encoding, both
        // 10,976,150. A number in a comment nobody can re-run is a
        // number that quietly stops being true.
        //
        // OUT OF RANGE IS -1 AND THE PLACEHOLDER STANDS ALONE, exactly
        // as the VM has it (`up.index < enclosing->upvalues.size()`,
        // else undefined): the enclosing frame holds no such cell, so
        // there is no index to name and nothing for the lift to carry.
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
        // `this` ONLY WHEN THE TARGET IS AN ARROW, and that is a
        // correction with a measured cost. The VM reads
        // $enclosing_this at exactly one line - call.cpp:924,
        // `if (target.is_arrow) { made->captured_this = ... }` - so
        // for every ordinary function this operand was DEAD, and a
        // dead operand is still a USE. The receiver arrives as %arg0,
        // and admission::function refuses any function that reads it:
        // so `function outer() { function inner() {} }` was refused
        // for "uses `this`" when nothing in it mentions `this` at all.
        // Measured over bootstrap, p5 and phaser, that artefact is
        // 1,866 of the 8,600 `uses \`this\`` refusals.
        //
        // AND THE UNDEFINED IS A MARKER, not only a smaller graph:
        // it is now the ONLY place the IR says whether a target is an
        // arrow, which is what lets the native tier lift an ordinary
        // closure and refuse an arrow that reads its lexical `this`
        // (LowerToEmitC.cpp, the Phase 59 lift).
        set(in.a, ctjs::CreateClosureOp::create(
                      into, where, value_type, entry->getArgument(arg_callee),
                      target.is_arrow ? entry->getArgument(arg_receiver) : state.undefined(where),
                      into.getI32IntegerAttr(static_cast<std::int32_t>(in.bx())), captured,
                      // ABSENT WHEN NO SLOT IS FILLED FROM THE ENCLOSING
                      // CLOSURE, which is most closures: an all -1 list says
                      // nothing the missing attribute does not, and printing
                      // one on every create_closure in a bundle is noise a
                      // reader has to check.
                      any_from_enclosing ? into.getDenseI32ArrayAttr(from_enclosing)
                                         : mlir::DenseI32ArrayAttr{}));
        break;
    }
    case op::get_global:
        if (in.bx() >= proto.names.size()) {
            state.give_up(at, in.code, "name index out of range");
            break;
        }
        set(in.a, ctjs::LoadGlobalOp::create(into, where, value_type,
                                             into.getStringAttr(proto.names[in.bx()])));
        break;
    case op::set_global:
        if (in.bx() >= proto.names.size()) {
            state.give_up(at, in.code, "name index out of range");
            break;
        }
        ctjs::StoreGlobalOp::create(into, where, into.getStringAttr(proto.names[in.bx()]),
                                    reg(in.a));
        break;
    case op::get_prop: {
        if (in.c >= proto.names.size()) {
            state.give_up(at, in.code, "name index out of range");
            break;
        }
        const mlir::Value key =
            state.constant(where, ctjs::StringAttr::get(context, proto.names[in.c]));
        set(in.a, ctjs::GetPropertyOp::create(into, where, value_type, reg(in.b), key));
        break;
    }
    case op::set_prop: {
        if (in.b >= proto.names.size()) {
            state.give_up(at, in.code, "name index out of range");
            break;
        }
        const mlir::Value key =
            state.constant(where, ctjs::StringAttr::get(context, proto.names[in.b]));
        ctjs::SetPropertyOp::create(into, where, reg(in.a), key, reg(in.c));
        break;
    }
    case op::get_index:
        set(in.a, ctjs::GetPropertyOp::create(into, where, value_type, reg(in.b), reg(in.c)));
        break;
    case op::has_property:
        // b IS THE KEY AND c IS THE TARGET, which is the reverse of
        // both the operation's operand order and the reading order of
        // `key in obj`. Swapping them answers about the wrong object.
        set(in.a, ctjs::HasPropertyOp::create(into, where, value_type, reg(in.c), reg(in.b)));
        break;
    case op::instance_of:
        // BOXED HERE, because ctjs.instanceof answers an i1 on purpose.
        set(in.a, ctjs::FromBoolOp::create(into, where, value_type,
                                           ctjs::InstanceOfOp::create(into, where, into.getI1Type(),
                                                                      reg(in.b), reg(in.c))));
        break;
    case op::delete_index:
        // a IS THE TARGET HERE, not the destination - delete_index
        // produces nothing and writes no register.
        ctjs::DeletePropertyOp::create(into, where, reg(in.a), reg(in.b));
        break;
    case op::push_handler: {
        mlir::Block * pad = state.block_at(jump_target(at, in));
        mlir::Block * body = state.block_at(static_cast<std::int64_t>(at) + 1);
        if (pad == nullptr || body == nullptr) {
            state.give_up(at, in.code, "handler target is not a block leader");
            break;
        }
        // AND THE PAD MUST BE REACHABLE ONLY BY THROWING.
        //
        // ct_aot_catch_land reads back what the unwinder wrote and then
        // CLEARS the pad marker; its row says it is "called exactly
        // once, only after CT_AOT_CAUGHT". Reached on a normal path it
        // clears a bit nothing set and binds a thrown value that was
        // never thrown.
        //
        // `try { f(); } catch (e) {}` IS THAT SHAPE. With an empty catch
        // the compiler emits no jump over it, so the try body falls
        // straight into the pad. A non-empty catch is preceded by that
        // jump, which is why this tests the instruction before the pad
        // rather than the catch clause itself.
        //
        // TWO WAYS IN, AND BOTH HAVE TO BE CLOSED. The pad can be fallen
        // into from the instruction above it, and it can be JUMPED to -
        // which is not exotic: jumping over an EMPTY catch clause lands
        // exactly on the pad, because the clause it is skipping has no
        // instructions. `try { f(); } catch (e) {}` is that shape, and a
        // test that only looked at the instruction above it let that
        // through.
        const std::int64_t landing = jump_target(at, in);
        bool reachable_without_throwing =
            landing > 0 && falls_through(proto.code[static_cast<std::size_t>(landing) - 1].code);
        for (std::size_t other = 0; other < proto.code.size() && !reachable_without_throwing;
             ++other) {
            if (other == at || !is_jump(proto.code[other].code)) { continue; }
            if (jump_target(other, proto.code[other]) == landing) {
                reachable_without_throwing = true;
            }
        }
        if (reachable_without_throwing) {
            state.give_up(at, in.code,
                          "the catch clause is reachable without throwing, which would "
                          "run ct_aot_catch_land on a normal path");
            break;
        }
        // A TERMINATOR WITH TWO SUCCESSORS, and the handler edge here
        // exists only to keep the pad reachable - the edge the emitted
        // code takes is ctjs.check's, at the throw site, because that
        // is the only place the live register vector exists.
        ctjs::PushHandlerOp::create(into, where, state.outgoing(), state.outgoing(), body, pad);
        state.handlers.push_back(function_importer::open_handler{pad, in.a});
        // AND THE WALK MOVES ON BY ITSELF. push_handler ends a block, so
        // `body` is a leader and the next iteration's enter_block enters
        // it and rebinds the registers - doing it here too made
        // enter_block see `previous == body` and emit a branch from the
        // block to ITSELF, which is a cf.br that does not terminate its
        // parent and the only symptom is "the imported function did not
        // verify".
        break;
    }
    case op::pop_handler:
        if (state.handlers.empty()) {
            state.give_up(at, in.code, "pop_handler with no open handler in this function");
            break;
        }
        ctjs::PopHandlerOp::create(into, where);
        state.handlers.pop_back();
        break;
    case op::make_arguments:
        set(in.a, ctjs::MakeArgumentsOp::create(into, where, value_type));
        break;
    case op::gather_rest:
        // b IS A COUNT - how many parameters were declared before the
        // rest one - not a register. The emitter spells it
        // `{gather_rest, i, i}` with both fields equal, which makes a
        // mutation from b to a invisible; that is recorded in
        // docs/plans/arguments-and-rest.md rather than papered over.
        set(in.a,
            ctjs::GatherRestOp::create(into, where, value_type, static_cast<std::uint32_t>(in.b)));
        break;
    case op::load_bigint:
        if (in.bx() >= proto.strings.size()) {
            state.give_up(at, in.code, "bigint literal index out of range");
            break;
        }
        // THE SOURCE TEXT, NOT A PARSED INTEGER. bigint_from_literal
        // owns `0x1fn`, `0b..n` and the 1.5n-to-0n substitution, and
        // parsing here would be a second implementation of all three.
        set(in.a, state.constant(where, ctjs::BigIntAttr::get(context, proto.strings[in.bx()])));
        break;
    // ---- ES modules ---------------------------------------------
    //
    // THREE OF THESE FOUR ONLY EXIST IN functions[0] OF A MODULE
    // PROGRAM. compile_program emits load_import, bind_export and
    // load_namespace from its `if (module_scope_)` arm and from
    // nowhere else, so a classic script cannot contain one and a
    // fixture that means to reach them has to be compiled with
    // script_kind::module. dyn_import is the exception: `import(x)` is
    // an expression and appears in ordinary functions.
    case op::load_import:
        // b IS THE EXPORT NAME AND c IS THE SPECIFIER, which is the
        // reverse of the reading order and makes this the only opcode
        // that reads c as a standalone index. bytecode.hpp says the
        // same thing from the other side: "a = the cell exported as
        // names[b] by the module at specifier names[c]". Filling the
        // operation in reading order compiles and raises `module
        // `count` was not loaded` at run time.
        if (in.b >= proto.names.size() || in.c >= proto.names.size()) {
            state.give_up(at, in.code, "name index out of range");
            break;
        }
        // WHAT LANDS HERE IS A CELL, and it must NOT be boxed again -
        // the compiler marked this local boxed and deliberately emitted
        // no new_cell, so every later read is already a ctjs.cell_get.
        set(in.a, ctjs::ModuleImportCellOp::create(into, where, value_type,
                                                   into.getStringAttr(proto.names[in.c]),
                                                   into.getStringAttr(proto.names[in.b])));
        break;
    case op::bind_export:
        // bx(), NOT b. b is the HIGH half of the pair, so it is 0 for
        // every module with fewer than 65,536 names - which is all of
        // them - and a decoder that reads b alone publishes name 0
        // under this name, silently.
        if (in.bx() >= proto.names.size()) {
            state.give_up(at, in.code, "name index out of range");
            break;
        }
        // reg(in.a) IS A SOURCE AS WELL AS THE DESTINATION. The write
        // is conditional on a module being evaluated, and outside one
        // the register keeps the local being exported - so the
        // operation carries that value and answers it back rather than
        // answering undefined and destroying the local.
        set(in.a,
            ctjs::ModuleExportCellOp::create(into, where, value_type,
                                             into.getStringAttr(proto.names[in.bx()]), reg(in.a)));
        break;
    case op::load_namespace:
        if (in.b >= proto.names.size()) {
            state.give_up(at, in.code, "name index out of range");
            break;
        }
        // A PLAIN 16-BIT b HERE, where bind_export uses the bx pair:
        // three module opcodes, two encodings for one name table.
        set(in.a, ctjs::ModuleNamespaceOp::create(into, where, value_type,
                                                  into.getStringAttr(proto.names[in.b])));
        break;
    case op::dyn_import:
        // b IS A REGISTER, not a name index - the specifier is computed,
        // which is the whole difference between this and load_namespace.
        set(in.a, ctjs::DynamicImportOp::create(into, where, value_type, reg(in.b)));
        break;
    case op::delete_prop:
        // a IS THE TARGET, b NAMES THE PROPERTY, and nothing is
        // written back - the delete produces no value.
        ctjs::DeleteNamedOp::create(into, where, reg(in.a), into.getStringAttr(proto.names[in.b]));
        break;
    case op::own_keys:
        set(in.a, ctjs::OwnKeysOp::create(into, where, value_type, reg(in.b)));
        break;
    // AN ASYNC FUNCTION'S `return`, AND `a` IS BOTH SOURCE AND
    // DESTINATION. The compiler emits `{op::wrap_promise, r}` over the
    // register the return value is already in - b and c are unused, and
    // the opcode row says so - so reading a and writing a is the whole
    // encoding rather than a coincidence to be careful about.
    case op::wrap_promise:
        set(in.a, ctjs::WrapPromiseOp::create(into, where, value_type, reg(in.a)));
        break;
    case op::define_getter:
    case op::define_setter: {
        // a IS THE TARGET, b NAMES THE PROPERTY and c IS THE FUNCTION -
        // which half it is comes from the OPCODE and from nowhere else,
        // so it is resolved here and the operation carries both.
        const bool getter = in.code == op::define_getter;
        const mlir::Value nothing = state.undefined(where);
        ctjs::DefineAccessorOp::create(into, where, reg(in.a),
                                       into.getStringAttr(proto.names[in.b]),
                                       getter ? reg(in.c) : nothing, getter ? nothing : reg(in.c));
        break;
    }
    case op::copy_props:
        // a IS THE TARGET and is NOT written back - the object is
        // mutated in place, so this produces no value.
        ctjs::CopyPropsOp::create(into, where, reg(in.a), reg(in.b));
        break;
    case op::apply:
        // a IS BOTH THE CALLEE AND THE DESTINATION, b the argument
        // array, c the receiver - and the destination being an operand
        // is why this reads all three before writing.
        set(in.a,
            ctjs::CallSpreadOp::create(into, where, value_type, reg(in.a), reg(in.b), reg(in.c)));
        break;
    case op::construct_apply:
        set(in.a, ctjs::ConstructSpreadOp::create(into, where, value_type, reg(in.a), reg(in.b)));
        break;
    case op::iterable:
        // b IN, a OUT, and the row warns that both emitters spell it
        // `{iterable, source, source}` with a and b ALIASED - which
        // reading b and writing a handles without needing to know.
        set(in.a, ctjs::IterableOp::create(into, where, value_type, reg(in.b)));
        break;
    case op::set_index:
        ctjs::SetPropertyOp::create(into, where, reg(in.a), reg(in.b), reg(in.c));
        break;
    case op::new_object: set(in.a, ctjs::CreateObjectOp::create(into, where, value_type)); break;
    case op::new_cell:
        // IN PLACE: the bytecode boxes the register's current value and
        // leaves the box where the value was.
        set(in.a, ctjs::CreateCellOp::create(into, where, value_type, reg(in.a)));
        break;
    case op::cell_get:
        set(in.a, ctjs::CellGetOp::create(into, where, value_type, reg(in.b)));
        break;
    case op::cell_set:
        state.names.assign(reg(in.b), in.a, at);
        ctjs::CellSetOp::create(into, where, reg(in.a), reg(in.b));
        break;
    case op::get_upvalue:
        // THE FRAME'S OWN CLOSURE, which arrives as the third implicit
        // argument. The interpreter reads vm_frame->closure; a compiled
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
        // THE ARGUMENTS ARE THE REGISTERS ABOVE THE CALLEE, which is
        // the interpreter's own layout: `arg_base = base + in.a + 1`,
        // and `in.b` of them. The callee's frame starts where its
        // arguments already are, which is why the bytecode puts them
        // there.
        mlir::Value callee = reg(in.a);
        mlir::Value receiver = state.undefined(where);
        if (in.code == op::call_receiver) {
            receiver = reg(in.c);
        } else if (in.code == op::call_method) {
            if (in.c >= proto.names.size()) {
                state.give_up(at, in.code, "name index out of range");
                break;
            }
            // THROUGH THE SAME LOOKUP AS get_prop, which the
            // interpreter is explicit about: `s.split(...)` and
            // `var f = s.split; f(...)` must find the same function.
            receiver = reg(in.a);
            const mlir::Value key =
                state.constant(where, ctjs::StringAttr::get(context, proto.names[in.c]));
            callee = ctjs::GetPropertyOp::create(into, where, value_type, receiver, key);
        } else if (in.code == op::call_computed) {
            receiver = reg(in.a);
            callee = ctjs::GetPropertyOp::create(into, where, value_type, receiver, reg(in.c));
        }
        llvm::SmallVector<mlir::Value> args;
        bool reachable = true;
        for (unsigned i = 0; i < in.b; ++i) {
            const auto slot = static_cast<std::uint16_t>(in.a + 1 + i);
            if (slot >= state.registers.size()) {
                state.give_up(at, in.code, "an argument register is outside the frame");
                reachable = false;
                break;
            }
            args.push_back(reg(slot));
        }
        if (!reachable) { break; }
        set(in.a, ctjs::CallOp::create(into, where, value_type, callee, receiver, args));
        break;
    }
    case op::construct: {
        llvm::SmallVector<mlir::Value> args;
        bool reachable = true;
        for (unsigned i = 0; i < in.b; ++i) {
            const auto slot = static_cast<std::uint16_t>(in.a + 1 + i);
            if (slot >= state.registers.size()) {
                state.give_up(at, in.code, "an argument register is outside the frame");
                reachable = false;
                break;
            }
            args.push_back(reg(slot));
        }
        if (!reachable) { break; }
        // NEW.TARGET IS THE CALLEE for a plain `new C()`. A super() call
        // hands a different one along, and that is op::pass_new_target's
        // business rather than this opcode's.
        set(in.a, ctjs::ConstructOp::create(into, where, value_type, reg(in.a), reg(in.a), args));
        break;
    }
    case op::jump_if_not_nullish:
    case op::jump_if_defined: {
        mlir::Block * taken = state.block_at(jump_target(at, in));
        mlir::Block * fallthrough = state.block_at(static_cast<std::int64_t>(at) + 1);
        if (taken == nullptr || fallthrough == nullptr) {
            state.give_up(at, in.code, "branch target is not a block leader");
            break;
        }
        // NOT TRUTHINESS. `jump_if_defined` branches when the value is
        // not `undefined` - which 0, "" and false all are not - and
        // `jump_if_not_nullish` when it is neither null nor undefined.
        // Lowering either through ctjs.truthy would send `x ?? y` down
        // the wrong arm for every falsy x, which is exactly the bug
        // optional chaining exists to avoid.
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
        // The comparison is TRUE when the value IS the sentinel, and
        // both opcodes jump when it is NOT - so the arms are swapped.
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
        mlir::Block * taken = state.block_at(jump_target(at, in));
        mlir::Block * fallthrough = state.block_at(static_cast<std::int64_t>(at) + 1);
        if (taken == nullptr || fallthrough == nullptr) {
            state.give_up(at, in.code, "branch target is not a block leader");
            break;
        }
        // THE ONLY BRIDGE FROM A VALUE TO A BRANCH. cf.cond_br takes an
        // i1; ctjs.convert to_boolean would produce a JavaScript
        // boolean, which is a different thing.
        const mlir::Value bit = ctjs::TruthyOp::create(into, where, into.getI1Type(), reg(in.a));
        const auto operands = state.outgoing();
        if (in.code == op::jump_if_true) {
            mlir::cf::CondBranchOp::create(into, where, bit, taken, operands, fallthrough,
                                           operands);
        } else {
            mlir::cf::CondBranchOp::create(into, where, bit, fallthrough, operands, taken,
                                           operands);
        }
        break;
    }
    case op::ret:
    case op::ret_undef:
    case op::halt: {
        const mlir::Value returned = in.code == op::ret ? reg(in.a) : state.undefined(where);
        // THE FRAME IS RELEASED ON THE RETURNING PATH ONLY. On an
        // unwound one it is already gone, and leaving again would pop
        // somebody else's.
        ctjs::FrameExitOp::create(into, where, state.frame);
        ctjs::ReturnOp::create(into, where, returned);
        break;
    }
    case op::throw_value: ctjs::ThrowOp::create(into, where, reg(in.a)); break;
    default: state.give_up(at, in.code, "no CTJS operation for this opcode yet"); break;
    }
}

} // namespace ctcompile::js::bytecode_detail
