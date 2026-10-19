//===- expressions.cpp - Expression compilation ---------------------------===//
//
// compiler_impl expression compilation. compile_expr_inner dispatches on the
// AST node kind, and most of the rest of the compiler is reached from it;
// this file also holds identifiers, operators, assignment targets
// (prepare_reference, emit_load, emit_store) and `yield*`. The class is
// declared in compiler_impl.hpp. Section numbers are ECMA-262's.
//
//===----------------------------------------------------------------------===//

#include "compiler_impl.hpp"
#include <ctbrowser/script/regex.hpp>

namespace ctbrowser::script::detail {

void compiler_impl::compile_expr(std::int32_t idx, std::uint16_t dst) {
    // Records this node's source position for the instructions emitted below
    // (compile_stmt is the other place that does). Every expression node,
    // including nested operands, passes through here, so positions are
    // tracked per instruction rather than per statement.
    const at_source here{*this, idx};
    if (idx >= 0 && out_.ok && !in_chain_ && chain_has_optional(idx)) {
        compile_chain(idx, dst);
        return;
    }
    compile_expr_inner(idx, dst);
}

void compiler_impl::compile_expr_inner(std::int32_t idx, std::uint16_t dst) {
    if (idx < 0 || !out_.ok) {
        proto().emit(instruction{op::load_undef, dst});
        return;
    }
    const vp::node & n = at(idx);
    switch (n.kind) {
    case vp::nk::num: {
        // The `n` suffix makes it a BigInt. The lexer keeps it on the token
        // because whether the digits are a valid one is a compile question,
        // not a lexing question: `1.5n` and `1e3n` are integers-only failures
        // and are refused here rather than there.
        if (n.text.ends_with('n')) {
            const std::string_view digits = n.text.substr(0, n.text.size() - 1);
            proto().emit(
                instruction::with_bx(op::load_bigint, dst, intern_string(std::string{digits})));
            break;
        }
        emit_const(dst, value::number(number_literal(n.text)));
        break;
    }
    case vp::nk::str:
        proto().emit(instruction::with_bx(op::load_string, dst,
                                          intern_string(decode_string_literal(n.text))));
        break;
    case vp::nk::tmpl: compile_template(n, dst); break;
    case vp::nk::new_expr: compile_new(n, dst); break;
    case vp::nk::opt_member:
    case vp::nk::opt_index:
    case vp::nk::opt_call: compile_optional(n, dst); break;
    case vp::nk::seq: compile_sequence(n, dst); break;
    case vp::nk::class_decl: compile_class(n, dst); break;
    case vp::nk::true_lit: proto().emit(instruction{op::load_true, dst}); break;
    case vp::nk::false_lit: proto().emit(instruction{op::load_false, dst}); break;
    case vp::nk::null_lit: proto().emit(instruction{op::load_null, dst}); break;
    case vp::nk::ident: compile_ident(n, dst); break;
    case vp::nk::binary: compile_binary(n, dst); break;
    case vp::nk::logical: compile_logical(n, dst); break;
    case vp::nk::unary: compile_unary(n, dst); break;
    case vp::nk::assign: compile_assign(n, dst); break;
    case vp::nk::update: compile_update(n, dst); break;
    case vp::nk::ternary: compile_ternary(n, dst); break;
    case vp::nk::member: {
        // `super.x` reads through the parent prototype rather than through
        // an object expression (there is no value `super` evaluates to),
        // and with `this` as the receiver, so a getter up there sees it.
        if (n.a >= 0 && at(n.a).kind == vp::nk::super_lit) {
            const std::uint32_t mark = reg_mark();
            const std::uint16_t key = alloc_reg();
            emit_string(key, std::string{n.text});
            emit_super_get(key, dst);
            release_to(mark);
            break;
        }
        compile_expr(n.a, dst);
        const std::uint16_t name = member_operand(n.text);
        proto().emit(instruction{op::get_prop, dst, dst, name});
        break;
    }
    case vp::nk::super_lit:
        // Bare `super` is not a value in JavaScript either; it is only ever
        // `super(...)` or `super.x`, both handled where they appear.
        fail("`super` is only valid as `super(...)` or `super.member`");
        break;
    case vp::nk::index: {
        const std::uint32_t mark = reg_mark();
        // `super[k]`, like `super.x` above (13.3.7.1: the key is evaluated
        // before the base is asked for, and the read carries `this`).
        if (n.a >= 0 && at(n.a).kind == vp::nk::super_lit) {
            const std::uint16_t key = alloc_reg();
            compile_expr(n.b, key);
            emit_super_get(key, dst);
            release_to(mark > dst ? mark : static_cast<std::uint16_t>(dst + 1));
            break;
        }
        compile_expr(n.a, dst);
        const std::uint16_t key = alloc_reg();
        compile_expr(n.b, key);
        proto().emit(instruction{op::get_index, dst, dst, key});
        release_to(mark > dst ? mark : static_cast<std::uint16_t>(dst + 1));
        break;
    }
    case vp::nk::call: compile_call(n, dst); break;
    case vp::nk::array: compile_array(n, dst); break;
    case vp::nk::object: compile_object(n, dst); break;
    case vp::nk::func_expr:
    case vp::nk::arrow: {
        const std::uint32_t index = compile_function_body(idx, std::string{n.text});
        proto().emit(instruction::with_bx(op::closure, dst, index));
        break;
    }
    case vp::nk::this_lit:
        // In a derived constructor (or an arrow in one), `this` before
        // `super()` is the ReferenceError; see frame::derived_flag.
        if (const std::string * flag = derived_flag()) { emit_super_check(*flag, false); }
        proto().emit(instruction{op::load_this, dst});
        break;
    // `new.target` is a meta-property, so it takes no operands and reads the
    // frame (or, in an arrow, the enclosing function's copy).
    case vp::nk::new_target: emit_new_target(dst); break;
    // A regex literal compiles to a RegExp object; see compile_regex_literal.
    case vp::nk::regex: compile_regex_literal(n, dst); break;
    // `yield x`, and a bare `yield`, which yields undefined.
    case vp::nk::yield_expr: {
        if (!fn().is_generator) {
            // `yield` outside a generator is a plain identifier in sloppy
            // mode and a SyntaxError in a generator-less function body.
            // Saying so beats compiling a suspend into a frame that can
            // never be resumed.
            fail("`yield` outside a generator function");
            proto().emit(instruction{op::load_undef, dst});
            break;
        }
        if (n.d == 1) {
            compile_yield_delegate(n, dst);
            break;
        }
        const std::uint16_t sent = alloc_reg();
        if (n.a >= 0) {
            compile_expr(n.a, sent);
        } else {
            proto().emit(instruction{op::load_undef, sent});
        }
        // An async generator awaits what it yields (27.6.3.8 AsyncGeneratorYield
        // step 5): `yield promise` hands out the promise's value, and a
        // rejected one throws at the yield.
        if (fn().is_async) { proto().emit(instruction{op::await_value, sent, sent}); }
        proto().emit(instruction{op::yield_value, dst, sent});
        break;
    }
    // Dynamic `import()` compiles to op::dyn_import, and the loader resolves
    // the specifier at run time. `import.meta` (below) is still refused by
    // name, because a silently undefined binding would fail far from its
    // cause; module work is tracked in ctbrowser/docs/plans/modules.md and
    // measured by ctbrowser/test/corpus/modules/module_ratchet.cpp.
    case vp::nk::dynamic_import: {
        // A runtime specifier, which is the whole difference from a static
        // import: there is nothing here for the loader to have resolved in
        // advance, and nothing to record in out_.imports.
        const std::uint32_t mark = reg_mark();
        const std::uint16_t spec = alloc_reg();
        compile_expr(n.a, spec);
        // `import.source(x)` (c == 1): the source phase, answered by a
        // hidden native; see import_source_name. `import.defer(x)` (c == 2)
        // loads like `import(x)` does: the deferral is not observable until
        // a module graph with side effects asks for it, which is a loader
        // concern (docs/plans/modules.md), not a compiler one.
        if (n.c == 1) {
            emit_iterator_native(import_source_name, dst, spec);
            release_to(mark);
            break;
        }
        // The options argument (13.3.10.1 step 4) is evaluated for its
        // effects and its throw; import attributes themselves are not read.
        if (n.b >= 0) {
            const std::uint16_t options = alloc_reg();
            compile_expr(n.b, options);
        }
        proto().emit(instruction{op::dyn_import, dst, spec});
        release_to(mark);
        break;
    }
    case vp::nk::import_meta:
        fail("`import.meta` is not implemented yet - ES modules are staged in "
             "docs/plans/modules.md");
        proto().emit(instruction{op::load_undef, dst});
        break;
    case vp::nk::tagged: compile_tagged(n, idx, dst); break;
    default:
        // Every kind the compiler once refused by name has its own case
        // above; what reaches here is a kind the parser grew that this
        // switch has not.
        fail("unsupported syntax in this VM subset: AST kind " +
             std::to_string(static_cast<int>(n.kind)));
        proto().emit(instruction{op::load_undef, dst});
        break;
    }
}

void compiler_impl::emit_write(std::string_view name_text, std::uint16_t src) {
    // Inside a `with`: the object that binds the name takes the write.
    const std::uint32_t mark = reg_mark();
    const std::uint16_t obj = alloc_reg();
    if (emit_with_object(name_text, obj)) {
        const std::size_t bound = proto().emit(instruction{op::jump_if_not_nullish, obj});
        emit_plain_write(name_text, src);
        const std::size_t done = proto().emit(instruction{op::jump});
        patch_here(bound);
        emit_with_still_bound(obj, name_text);
        proto().emit(instruction{op::set_prop, obj, name_operand(std::string{name_text}), src});
        patch_here(done);
        release_to(mark);
        return;
    }
    release_to(mark);
    emit_plain_write(name_text, src);
}

void compiler_impl::compile_ident(const vp::node & n, std::uint16_t dst, bool typeof_lookup) {
    if (tdz_frame_ == frames_.size() - 1 &&
        std::find(tdz_names_.begin(), tdz_names_.end(), n.text) != tdz_names_.end()) {
        emit_throw("ReferenceError",
                   "Cannot access '" + std::string{n.text} + "' before initialization");
        proto().emit(instruction{op::load_undef, dst});
        return;
    }
    // The temporal dead zone, decided statically (local::initialized_at): a
    // read of this frame's own `let`/`const`/`class` textually before the
    // declarator that initialises it (`let x = x + 1`, `use(y); const y = 1`)
    // runs before that declarator every time the scope runs, so it is the
    // ReferenceError of 9.1.1.1.6 whenever it runs, `typeof` included.
    // Only in the program's own AST: a template substitution is re-parsed
    // from its own text (current_ast_ is that sub-tree), so its offsets are
    // relative to the substitution and cannot be compared with
    // initialized_at.
    if (n.end > n.begin && current_ast_ == &ast_) {
        if (const local * l = find_local_entry(fn(), n.text);
            l != nullptr && l->initialized_at != 0 && n.begin < l->initialized_at) {
            emit_throw("ReferenceError",
                       "Cannot access '" + std::string{n.text} + "' before initialization");
            proto().emit(instruction{op::load_undef, dst});
            return;
        }
    }
    // Inside a `with`: the object that binds the name answers the read.
    const std::uint32_t mark = reg_mark();
    const std::uint16_t obj = alloc_reg();
    if (emit_with_object(n.text, obj)) {
        const std::size_t bound = proto().emit(instruction{op::jump_if_not_nullish, obj});
        emit_plain_read(n.text, dst, typeof_lookup);
        const std::size_t done = proto().emit(instruction{op::jump});
        patch_here(bound);
        proto().emit(instruction{op::get_prop, dst, obj, name_operand(std::string{n.text})});
        patch_here(done);
        release_to(mark);
        return;
    }
    release_to(mark);
    emit_plain_read(n.text, dst, typeof_lookup);
}

/// `yield* expr` (14.4.14): every value the inner iterator produces is
/// yielded by this generator, and the expression's own value is what the
/// inner iterator finally returned. GetIterator, the `next()` calls and the
/// result checks are three natives (yield_delegate_open_name,
/// yield_delegate_call_name, yield_delegate_settle_name); the loop is here.
/// A sync generator hands the inner result object out untouched (step
/// 7.a.vii; generator_resume knows from the record the settle native leaves
/// on the coroutine), and an async one awaits the result, then awaits and
/// yields its value like any other async yield. `.throw()`/`.return()` while
/// suspended here are handled by generator_resume, forwarded to the inner
/// iterator without resuming the frame.
void compiler_impl::compile_yield_delegate(const vp::node & n, std::uint16_t dst) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t source = alloc_reg();
    compile_expr(n.a, source);
    const std::uint16_t record = alloc_reg();
    emit_iterator_native(yield_delegate_open_name, record, source, fn().is_async ? 1 : 0);
    const std::uint16_t sent = alloc_reg();
    proto().emit(instruction{op::load_undef, sent});
    const std::uint16_t step = alloc_reg();
    const std::uint16_t done = alloc_reg();
    const std::size_t top = proto().code.size();
    {
        const std::uint32_t inner = reg_mark();
        const std::uint16_t callee = alloc_reg();
        proto().emit(instruction::with_bx(op::get_global, callee,
                                          intern_name(std::string{yield_delegate_call_name})));
        const std::uint16_t arg0 = alloc_reg();
        proto().emit(instruction{op::move, arg0, record});
        const std::uint16_t arg1 = alloc_reg();
        proto().emit(instruction{op::move, arg1, sent});
        proto().emit(instruction{op::call, callee, 2});
        proto().emit(instruction{op::move, step, callee});
        release_to(inner);
    }
    if (fn().is_async) { proto().emit(instruction{op::await_value, step, step}); }
    {
        const std::uint32_t inner = reg_mark();
        const std::uint16_t callee = alloc_reg();
        proto().emit(instruction::with_bx(op::get_global, callee,
                                          intern_name(std::string{yield_delegate_settle_name})));
        const std::uint16_t arg0 = alloc_reg();
        proto().emit(instruction{op::move, arg0, record});
        const std::uint16_t arg1 = alloc_reg();
        proto().emit(instruction{op::move, arg1, step});
        proto().emit(instruction{op::call, callee, 2});
        proto().emit(instruction{op::move, step, callee});
        release_to(inner);
    }
    proto().emit(instruction{op::get_prop, done, record, name_operand("done")});
    const std::size_t exit = proto().emit(instruction{op::jump_if_true, done});
    if (fn().is_async) {
        proto().emit(instruction{op::get_prop, step, step, name_operand("value")});
        proto().emit(instruction{op::await_value, step, step});
    }
    proto().emit(instruction{op::yield_value, sent, step});
    patch_jump(proto().emit(instruction{op::jump}), top);
    patch_here(exit);
    proto().emit(instruction{op::get_prop, dst, record, name_operand("value")});
    release_to(mark);
}

void compiler_impl::compile_named_expr(std::int32_t idx, std::uint16_t dst, std::string_view name) {
    if (idx >= 0 && !name.empty()) {
        const vp::node & n = at(idx);
        // A parenthesised function is still anonymous: the parser keeps no
        // paren node, so `(function () {})` arrives as the function itself.
        if ((n.kind == vp::nk::func_expr || n.kind == vp::nk::arrow) && n.text.empty()) {
            const std::uint32_t index = compile_function_body(idx, "");
            out_.functions[index].inferred_name = std::string{name};
            proto().emit(instruction::with_bx(op::closure, dst, index));
            return;
        }
        if (n.kind == vp::nk::class_decl && n.text.empty()) {
            compile_class(n, dst, false, name);
            return;
        }
    }
    compile_expr(idx, dst);
}

void compiler_impl::emit_new_target(std::uint16_t dst) {
    if (out_.functions[fn().proto].is_arrow &&
        binding_position(new_target_local).first != no_binding) {
        emit_plain_read(new_target_local, dst);
        return;
    }
    proto().emit(instruction{op::load_new_target, dst});
}

bool compiler_impl::is_anonymous_function_definition(std::int32_t idx) const {
    if (idx < 0) { return false; }
    const vp::node & n = at(idx);
    return (n.kind == vp::nk::func_expr || n.kind == vp::nk::arrow ||
            n.kind == vp::nk::class_decl) &&
           n.text.empty();
}

std::optional<std::string> compiler_impl::literal_key_name(std::int32_t key) const {
    const vp::node & k = at(key);
    if (k.kind == vp::nk::str) { return decode_string_literal(k.text); }
    // A BigInt literal (`1n`) is left to run time: its ToString is not the
    // double's.
    if (k.kind == vp::nk::num && !k.text.empty() && k.text.back() != 'n') {
        return number_to_string(number_literal(k.text));
    }
    return std::nullopt;
}

void compiler_impl::emit_set_function_name(std::uint16_t fn, std::uint16_t key,
                                           std::string_view prefix) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t callee = alloc_reg();
    proto().emit(instruction::with_bx(op::get_global, callee,
                                      intern_name(std::string{set_function_name_name})));
    const std::uint16_t target = alloc_reg();
    proto().emit(instruction{op::move, target, fn});
    const std::uint16_t name = alloc_reg();
    proto().emit(instruction{op::move, name, key});
    std::uint16_t argc = 2;
    if (!prefix.empty()) {
        emit_string(alloc_reg(), std::string{prefix});
        argc = 3;
    }
    proto().emit(instruction{op::call, callee, argc});
    release_to(mark);
}

void compiler_impl::compile_delete(const vp::node & n, std::uint16_t dst) {
    const std::uint32_t mark = reg_mark();
    const vp::node & target = at(n.a);
    if (target.kind == vp::nk::member || target.kind == vp::nk::index) {
        // Through delete_ref_name, which answers the boolean 13.5.1.2 gives
        // and throws where it says; the opcodes answer nothing. `delete
        // super.x` evaluates the key and is the ReferenceError.
        const bool on_super = at(target.a).kind == vp::nk::super_lit;
        const std::uint16_t callee = alloc_reg();
        proto().emit(instruction::with_bx(op::get_global, callee,
                                          intern_name(std::string{delete_ref_name})));
        const std::uint16_t object = alloc_reg();
        if (on_super) {
            proto().emit(instruction{op::load_undef, object});
        } else {
            compile_expr(target.a, object);
        }
        const std::uint16_t key = alloc_reg();
        if (target.kind == vp::nk::member) {
            emit_string(key, std::string{target.text});
        } else {
            compile_expr(target.b, key);
        }
        const std::uint16_t strict = alloc_reg();
        proto().emit(instruction{fn().is_strict ? op::load_true : op::load_false, strict});
        const std::uint16_t super_flag = alloc_reg();
        proto().emit(instruction{on_super ? op::load_true : op::load_false, super_flag});
        proto().emit(instruction{op::call, callee, 4});
        proto().emit(instruction{op::move, dst, callee});
    } else if (target.kind == vp::nk::ident) {
        // `delete x` inside a `with` whose object binds x deletes the
        // property (13.5.1.2 step 3.b through the object environment's
        // DeleteBinding); any other name is undeletable here and answers
        // false.
        const std::uint16_t obj = alloc_reg();
        emit_const(dst, value::boolean(false));
        if (emit_with_object(target.text, obj)) {
            const std::size_t unbound = proto().emit(instruction{op::jump_if_false, obj});
            proto().emit(instruction{op::delete_prop, obj, name_operand(std::string{target.text})});
            emit_const(dst, value::boolean(true));
            patch_here(unbound);
        }
    } else {
        // `delete 1`, `delete void a.b`, `delete f()`: not a reference; the
        // operand is evaluated and the answer is true (13.5.1.2 step 2).
        const std::uint16_t scratch = alloc_reg();
        compile_expr(n.a, scratch);
        emit_const(dst, value::boolean(true));
    }
    release_to(mark);
}

void compiler_impl::compile_binary(const vp::node & n, std::uint16_t dst) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t lhs = alloc_reg();
    const std::uint16_t rhs = alloc_reg();
    // `#x in o` (13.10.1): the private name is the key the brand is checked
    // under, spelled as the class body resolved it, never a name to read.
    const bool private_in =
        n.text == "in" && at(n.a).kind == vp::nk::ident && at(n.a).text.starts_with('#');
    if (private_in) {
        emit_string(lhs, member_key(at(n.a).text));
    } else {
        compile_expr(n.a, lhs);
    }
    compile_expr(n.b, rhs);
    const std::string_view o = n.text;
    op code = op::add_generic;
    if (o == "+") {
        code = op::add_generic;
    } else if (o == "-") {
        code = op::sub;
    } else if (o == "*") {
        code = op::mul;
    } else if (o == "/") {
        code = op::div;
    } else if (o == "%") {
        code = op::mod;
    } else if (o == "**") {
        code = op::pow;
    } else if (o == "===") {
        code = op::equal;
    } else if (o == "!==") {
        code = op::not_equal;
    }
    // `==` and `!=` are loose equality (with type coercion).
    else if (o == "==") {
        code = op::loose_equal;
    } else if (o == "!=") {
        code = op::loose_not_equal;
    } else if (o == "&") {
        code = op::bit_and;
    } else if (o == "|") {
        code = op::bit_or;
    } else if (o == "^") {
        code = op::bit_xor;
    } else if (o == "<<") {
        code = op::shl;
    } else if (o == ">>") {
        code = op::shr;
    } else if (o == ">>>") {
        code = op::ushr;
    } else if (o == "instanceof") {
        code = op::instance_of;
    } else if (o == "in") {
        if (!private_in) {
            // HasProperty also serves `with` and generic array methods, which
            // accept primitives. Only `in` validates before converting its key.
            emit_iterator_native(in_object_name, rhs, rhs);
            emit_iterator_native(to_property_key_name, lhs, lhs);
        }
        code = op::has_property;
    } else if (o == "<") {
        code = op::less;
    } else if (o == "<=") {
        code = op::less_equal;
    } else if (o == ">") {
        code = op::greater;
    } else if (o == ">=") {
        code = op::greater_equal;
    } else {
        fail("unsupported binary operator '" + std::string{o} + "'");
    }
    proto().emit(instruction{code, dst, lhs, rhs});
    release_to(mark);
}

void compiler_impl::compile_logical(const vp::node & n, std::uint16_t dst) {
    compile_expr(n.a, dst);
    // `??` is not `||`. It asks whether the left side is null or undefined,
    // so `0 ?? 5` is 0 and `"" ?? "x"` is "".
    const op test = n.text == "&&"    ? op::jump_if_false
                    : n.text == "?\?" ? op::jump_if_not_nullish
                                      : op::jump_if_true;
    const std::size_t skip = proto().emit(instruction{test, dst});
    compile_expr(n.b, dst);
    patch_here(skip);
}

void compiler_impl::compile_unary(const vp::node & n, std::uint16_t dst) {
    // `delete o.x` must not evaluate `o.x`: it takes the object and the
    // key, which is why it cannot go through the operand-first path below.
    if (n.text == "delete") {
        compile_delete(n, dst);
        return;
    }
    const std::uint32_t mark = reg_mark();
    const std::uint16_t operand = alloc_reg();
    // Only an IdentifierReference suppresses an unresolved-name error. Keep
    // normal binding resolution, TDZ checks and with-object property reads.
    if (n.text == "typeof" && at(n.a).kind == vp::nk::ident) {
        const at_source here{*this, n.a};
        compile_ident(at(n.a), operand, true);
    } else {
        compile_expr(n.a, operand);
    }
    if (n.text == "-") {
        proto().emit(instruction{op::negate, dst, operand});
    } else if (n.text == "!") {
        proto().emit(instruction{op::logical_not, dst, operand});
    } else if (n.text == "typeof") {
        proto().emit(instruction{op::type_of, dst, operand});
    } else if (n.text == "await") {
        // `await` suspends the frame; the VM resumes it from a microtask once
        // the awaited value settles (see context::execute_await in
        // vm/run_loop/await.cpp, and vm/call/coroutines.cpp).
        proto().emit(instruction{op::await_value, dst, operand});
    } else if (n.text == "+") {
        // Unary plus is ToNumber, not a copy: `+"3"` must index an array as 3.
        proto().emit(instruction{op::to_number, dst, operand});
    } else if (n.text == "~") {
        proto().emit(instruction{op::bit_not, dst, operand});
    }
    // `void x` evaluates x for its effects and yields undefined.
    else if (n.text == "void") {
        proto().emit(instruction{op::load_undef, dst});
    } else {
        fail("unsupported unary operator '" + std::string{n.text} + "'");
    }
    release_to(mark);
}

compiler_impl::reference compiler_impl::prepare_reference(const vp::node & target) {
    reference out;
    if (target.kind == vp::nk::ident) {
        // Inside a `with`, the binding is decided once, here (13.15.2 step 1
        // evaluates the reference before the right side): the object that
        // binds the name, or undefined, sits in a register of its own for as
        // long as the caller keeps the reference.
        {
            const std::uint32_t before = reg_mark();
            const std::uint16_t obj = alloc_reg();
            if (emit_with_object(target.text, obj)) {
                out.with = true;
                out.with_reg = obj;
                out.with_name = name_operand(std::string{target.text});
            } else {
                release_to(before);
            }
        }
        out.readonly = binding_immutability(target.text);
        out.ready_flag = binding_ready_flag(target.text);
        if (const local * l = find_local_entry(fn(), target.text)) {
            out.what = l->boxed ? reference::kind::boxed_local : reference::kind::local;
            out.reg = l->reg;
            return out;
        }
        if (const int up = resolve_upvalue(frames_.size() - 1, target.text); up >= 0) {
            out.what = reference::kind::upvalue;
            out.reg = static_cast<std::uint16_t>(up);
            return out;
        }
        out.what = reference::kind::global;
        out.name = name_operand(std::string{target.text});
        return out;
    }
    // `super.x = v` and `super[k] = v`: a Super Reference (13.3.7.1). The
    // this binding is checked first (the ReferenceError before a derived
    // constructor's super()), then the key, then GetSuperBase; it is read
    // and put on that base with `this` as the receiver (6.2.5.5/6.2.5.6), so
    // a setter on the parent runs on `this` rather than `this`'s own setter
    // of the same name, and `set x(v) { super.x = v; }` does not recurse.
    const bool on_super = target.a >= 0 && at(target.a).kind == vp::nk::super_lit;
    if (on_super && (target.kind == vp::nk::member || target.kind == vp::nk::index)) {
        if (const std::string * flag = derived_flag()) { emit_super_check(*flag, false); }
        out.what = reference::kind::super_property;
        out.key = alloc_reg();
        if (target.kind == vp::nk::member) {
            emit_string(out.key, std::string{target.text});
        } else {
            compile_expr(target.b, out.key);
        }
        out.reg = alloc_reg();
        emit_super_base(out.reg);
        return out;
    }
    if (target.kind == vp::nk::member) {
        out.what = reference::kind::member;
        out.reg = alloc_reg();
        if (on_super) {
            proto().emit(instruction{op::load_this, out.reg});
        } else {
            compile_expr(target.a, out.reg);
        }
        out.name = member_operand(target.text);
        return out;
    }
    if (target.kind == vp::nk::index) {
        out.what = reference::kind::index;
        out.reg = alloc_reg();
        out.key = alloc_reg();
        if (on_super) {
            proto().emit(instruction{op::load_this, out.reg});
        } else {
            compile_expr(target.a, out.reg);
        }
        compile_expr(target.b, out.key);
        return out;
    }
    fail("unsupported assignment target");
    return out;
}

void compiler_impl::emit_load(const reference & ref, std::uint16_t dst) {
    if (ref.with) {
        reference plain = ref;
        plain.with = false;
        const std::size_t bound = proto().emit(instruction{op::jump_if_not_nullish, ref.with_reg});
        emit_load(plain, dst);
        const std::size_t done = proto().emit(instruction{op::jump});
        patch_here(bound);
        proto().emit(instruction{op::get_prop, dst, ref.with_reg, ref.with_name});
        patch_here(done);
        return;
    }
    emit_ready_check(ref.ready_flag);
    switch (ref.what) {
    case reference::kind::local: proto().emit(instruction{op::move, dst, ref.reg}); break;
    case reference::kind::boxed_local: proto().emit(instruction{op::cell_get, dst, ref.reg}); break;
    case reference::kind::upvalue: proto().emit(instruction{op::get_upvalue, dst, ref.reg}); break;
    case reference::kind::global:
        proto().emit(instruction::with_bx(op::get_global, dst, ref.name));
        break;
    case reference::kind::member:
        proto().emit(instruction{op::get_prop, dst, ref.reg, static_cast<std::uint16_t>(ref.name)});
        break;
    case reference::kind::index:
        proto().emit(instruction{op::get_index, dst, ref.reg, ref.key});
        break;
    case reference::kind::super_property: emit_super_access(ref, super_get_name, dst); break;
    }
}

/// Calls `name(base, key, [value,] this[, strict])` for a Super Reference; see
/// reference::kind::super_property. `value` is the store's source, or the
/// load's destination.
void compiler_impl::emit_super_access(const reference & ref, std::string_view name,
                                      std::uint16_t value) {
    const bool store = name == super_set_name;
    const std::uint32_t mark = reg_mark();
    const std::uint16_t callee = alloc_reg();
    proto().emit(instruction::with_bx(op::get_global, callee, intern_name(std::string{name})));
    const std::uint16_t base = alloc_reg();
    proto().emit(instruction{op::move, base, ref.reg});
    const std::uint16_t key = alloc_reg();
    proto().emit(instruction{op::move, key, ref.key});
    if (store) {
        const std::uint16_t v = alloc_reg();
        proto().emit(instruction{op::move, v, value});
    }
    const std::uint16_t self = alloc_reg();
    proto().emit(instruction{op::load_this, self});
    if (store) {
        const std::uint16_t strict = alloc_reg();
        proto().emit(instruction{fn().is_strict ? op::load_true : op::load_false, strict});
    }
    proto().emit(instruction{op::call, callee, static_cast<std::uint16_t>(store ? 5 : 3)});
    if (!store) { proto().emit(instruction{op::move, value, callee}); }
    release_to(mark);
}

void compiler_impl::emit_store(const reference & ref, std::uint16_t src) {
    if (ref.with) {
        reference plain = ref;
        plain.with = false;
        const std::size_t bound = proto().emit(instruction{op::jump_if_not_nullish, ref.with_reg});
        emit_store(plain, src);
        const std::size_t done = proto().emit(instruction{op::jump});
        patch_here(bound);
        // A copy: the check interns strings, which can grow `names`.
        const std::string name = proto().names[ref.with_name];
        emit_with_still_bound(ref.with_reg, name);
        proto().emit(instruction{op::set_prop, ref.with_reg, ref.with_name, src});
        patch_here(done);
        return;
    }
    emit_ready_check(ref.ready_flag);
    if (refuse_immutable_write(ref.readonly, ref.what == reference::kind::global
                                                 ? std::string_view{proto().names[ref.name]}
                                                 : std::string_view{})) {
        return;
    }
    switch (ref.what) {
    case reference::kind::local: proto().emit(instruction{op::move, ref.reg, src}); break;
    case reference::kind::boxed_local: proto().emit(instruction{op::cell_set, ref.reg, src}); break;
    case reference::kind::upvalue: proto().emit(instruction{op::set_upvalue, ref.reg, src}); break;
    case reference::kind::global: {
        // A copy: the write interns names, which can grow `names` under a
        // view into it.
        const std::string name = proto().names[ref.name];
        emit_global_write(name, src);
        break;
    }
    case reference::kind::member:
        proto().emit(instruction{op::set_prop, ref.reg, static_cast<std::uint16_t>(ref.name), src});
        break;
    case reference::kind::index:
        proto().emit(instruction{op::set_index, ref.reg, ref.key, src});
        break;
    case reference::kind::super_property: emit_super_access(ref, super_set_name, src); break;
    }
}

op compiler_impl::compound_op(std::string_view text, bool & ok) {
    ok = true;
    if (text == "+=") { return op::add_generic; }
    if (text == "-=") { return op::sub; }
    if (text == "*=") { return op::mul; }
    if (text == "/=") { return op::div; }
    if (text == "%=") { return op::mod; }
    if (text == "**=") { return op::pow; }
    // The bitwise compound assignments reuse the plain operators' opcodes.
    if (text == "&=") { return op::bit_and; }
    if (text == "|=") { return op::bit_or; }
    if (text == "^=") { return op::bit_xor; }
    if (text == "<<=") { return op::shl; }
    if (text == ">>=") { return op::shr; }
    if (text == ">>>=") { return op::ushr; }
    ok = false;
    return op::add;
}

void compiler_impl::compile_assign(const vp::node & n, std::uint16_t dst) {
    const vp::node & target = at(n.a);
    const std::uint32_t mark = reg_mark();

    // Destructuring assignment: `[a, b] = pair` and `({x} = o)`.
    //
    // The left side is not a pattern node: the parser met it in expression
    // position and read an array or object literal, which is the only thing
    // it could have been at the time. Re-reading it as a pattern here is
    // what the grammar itself does, and it is why array literals keep their
    // holes: `[, ref] = pair` skips the first element.
    if (n.text == "=" && (target.kind == vp::nk::array || target.kind == vp::nk::object)) {
        compile_expr(n.b, dst);
        compile_pattern(n.a, dst);
        release_to(mark);
        return;
    }
    if (target.kind == vp::nk::call) {
        emit_call_target_error(n.a, dst);
        release_to(mark);
        return;
    }

    const reference ref = prepare_reference(target);

    if (n.text == "=") {
        // In strict code, assigning to an unresolvable name is a
        // ReferenceError (13.15.2 PutValue step 4.a, 9.1.1.4.5
        // SetMutableBinding step 3). A get_global probe throws it, so
        // set_global stays the one write. The probe runs before the right
        // side, which keeps `closure; set_global` adjacent for ctcompile's
        // closure lift (the order is observable only when the right side has
        // side effects and the name is unresolvable); the compound and
        // logical forms below read first anyway. Skipped in modules, a
        // deliberate leniency: ctcompile's module fixtures
        // (ctcompile/test/Runtime/Modules) assign undeclared globals to talk
        // to their host. Also skipped for names the script declares with
        // var/let/const, which are hoisted (16.1.7) and so always resolve.
        const auto declared_by_script = [&](std::string_view name) {
            const std::vector<std::string> & names = frames_.front().declared;
            return std::find(names.begin(), names.end(), name) != names.end();
        };
        if (ref.what == reference::kind::global && fn().is_strict && !module_scope_ &&
            !declared_by_script(target.text)) {
            const std::uint32_t probe_mark = reg_mark();
            const std::uint16_t probe = alloc_reg();
            proto().emit(instruction::with_bx(op::get_global, probe, ref.name));
            release_to(probe_mark);
        }
        if (target.kind == vp::nk::ident) {
            compile_named_expr(n.b, dst, target.text);
        } else {
            compile_expr(n.b, dst);
        }
        emit_store(ref, dst);
        release_to(mark);
        return;
    }

    // Logical assignment short-circuits: `a ||= b` must not evaluate b when
    // a is already truthy, which is the whole reason it exists.
    if (n.text == "||=" || n.text == "&&=" || n.text == "?\?=") {
        emit_load(ref, dst);
        const op test = n.text == "&&="    ? op::jump_if_false
                        : n.text == "?\?=" ? op::jump_if_not_nullish
                                           : op::jump_if_true;
        const std::size_t skip = proto().emit(instruction{test, dst});
        if (target.kind == vp::nk::ident) {
            compile_named_expr(n.b, dst, target.text);
        } else {
            compile_expr(n.b, dst);
        }
        emit_store(ref, dst);
        patch_here(skip);
        release_to(mark);
        return;
    }

    bool known = false;
    const op operation = compound_op(n.text, known);
    if (!known) {
        fail("unsupported assignment operator '" + std::string{n.text} + "'");
        release_to(mark);
        return;
    }
    const std::uint16_t rhs = alloc_reg();
    emit_load(ref, dst);
    compile_expr(n.b, rhs);
    proto().emit(instruction{operation, dst, dst, rhs});
    emit_store(ref, dst);
    release_to(mark);
}

/// `f() = v`, `f() += v`, `f()++` in non-strict code: the early-error pass
/// lets a call target through for web compatibility ("Runtime Errors for
/// Function Call Assignment Targets"), and evaluating one is the call and then
/// the ReferenceError, before the right side and before any read of the
/// result.
void compiler_impl::emit_call_target_error(std::int32_t call, std::uint16_t dst) {
    compile_expr(call, dst);
    emit_throw("ReferenceError", "Invalid left-hand side in assignment");
}

void compiler_impl::compile_update(const vp::node & n, std::uint16_t dst) {
    const vp::node & target = at(n.a);
    const std::uint32_t mark = reg_mark();
    if (target.kind == vp::nk::call) {
        emit_call_target_error(n.a, dst);
        release_to(mark);
        return;
    }
    // Through the same reference machinery as compound assignment, so
    // `obj.n++` and `a[i]++` work and evaluate their target exactly once.
    const reference ref = prepare_reference(target);
    const std::uint16_t cur = alloc_reg();
    const std::uint16_t step = alloc_reg();
    emit_load(ref, cur);
    // Both through op::add (`x--` is `x + (-1)`, the same double) because
    // op::add is the internal add of ++/-- and the counters, never source
    // `+`, and the VM lets it take a BigInt beside its integral Number step
    // (`1n++` is `2n`; `1n - 1` is the mixing TypeError op::sub keeps).
    emit_const(step, value::number(n.text == "++" ? 1 : -1));
    // Postfix yields the old value as a numeric (13.4.2.1 step 2: ToNumeric
    // of it, so `false++` reads 0 and an object its valueOf); `+ 0` through
    // the same op is that conversion. Prefix yields the new one.
    if (n.b == 0) {
        const std::uint16_t zero = alloc_reg();
        emit_const(zero, value::number(0));
        proto().emit(instruction{op::add, dst, cur, zero});
    }
    proto().emit(instruction{op::add, cur, cur, step});
    if (n.b != 0) { proto().emit(instruction{op::move, dst, cur}); }
    emit_store(ref, cur);
    release_to(mark);
}

void compiler_impl::compile_ternary(const vp::node & n, std::uint16_t dst) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t cond = alloc_reg();
    compile_expr(n.a, cond);
    const std::size_t to_alt = proto().emit(instruction{op::jump_if_false, cond});
    release_to(mark);
    compile_expr(n.b, dst);
    const std::size_t to_end = proto().emit(instruction{op::jump});
    patch_here(to_alt);
    compile_expr(n.c, dst);
    patch_here(to_end);
}

} // namespace ctbrowser::script::detail
