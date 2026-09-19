// compiler_impl - expressions.
//
// `compile_expr_inner` is the hub - a 154-line dispatch that most
// of the rest of the compiler eventually reaches.
//
// One of the files carved out of a 3,845-line compile.cpp on 2026-08-09.
// The class is declared whole in compiler_impl.hpp beside this.

#include "compiler_impl.hpp"
#include <ctbrowser/script/regex.hpp>

namespace ctbrowser::script::detail {

void compiler_impl::compile_expr(std::int32_t idx, std::uint16_t dst) {
    // AND THE OTHER ONE. Every expression node the compiler visits passes
    // through here, including the operands of the one that contains it - which
    // is what makes the position per-INSTRUCTION rather than per-statement.
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
        // THE `n` SUFFIX MAKES IT A BigInt. The lexer keeps it on the token
        // because whether the digits are a valid one is this question, not
        // a lexing question: `1.5n` and `1e3n` are integers-only failures
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
        // an object expression - there is no value `super` evaluates to -
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
        // Bare `super` is not a value in JavaScript either - it is only ever
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
        // `super()` is the ReferenceError - see frame::derived_flag.
        if (const std::string * flag = derived_flag()) { emit_super_check(*flag, false); }
        proto().emit(instruction{op::load_this, dst});
        break;
    // `new.target` - a meta-property, so it takes no operands and reads the
    // frame. Every transpiler emits it (Babel's `_classCallCheck` guard is
    // built on it) and Babylon.js uses it in its decorator metadata; it
    // reached this compiler as a PARSE error until ctjs learned it.
    case vp::nk::new_target: proto().emit(instruction{op::load_new_target, dst}); break;
    // Regular expressions are DEFERRED, not overlooked: they need a regex
    // engine, and the standard library says the same about String.match and
    // the RegExp forms of replace/split. Rejecting one by name beats
    // mis-compiling it into something that silently does nothing.
    case vp::nk::regex: compile_regex_literal(n, dst); break;
    // `yield x`, and `yield` with nothing - which yields undefined.
    //
    // Refused by name here until 2026-08-02, and the reason it stopped
    // being refused is Babylon.js: TypeScript compiles every `async`
    // function to a generator driven by an `__awaiter` helper, so 622
    // `function*` bodies in that bundle are not the author writing
    // generators at all - they are what `await` became.
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
        // AN ASYNC GENERATOR AWAITS WHAT IT YIELDS (27.6.3.8 AsyncGeneratorYield
        // step 5): `yield promise` hands out the promise's value, and a
        // rejected one throws at the yield.
        if (fn().is_async) { proto().emit(instruction{op::await_value, sent, sent}); }
        proto().emit(instruction{op::yield_value, dst, sent});
        break;
    }
    // ES MODULES ARE REFUSED BY NAME, not mis-compiled. The syntax parses
    // now (ctjs 2026-08-02); the semantics - a scope per module, live
    // bindings, a dependency graph, a loader - are staged in
    // docs/plans/modules.md and measured by test/corpus/modules/module_ratchet.cpp.
    //
    // Refusing beats accepting: a page whose `import` silently did nothing
    // would run with half its bindings undefined and fail somewhere else
    // entirely, which is the failure mode this tree keeps paying for.
    case vp::nk::dynamic_import: {
        // A RUNTIME SPECIFIER, which is the whole difference from a static
        // import: there is nothing here for the loader to have resolved in
        // advance, and nothing to record in out_.imports. The graph a
        // dynamic import reaches is not knowable at compile time - that is
        // what it is FOR.
        const std::uint32_t mark = reg_mark();
        const std::uint16_t spec = alloc_reg();
        compile_expr(n.a, spec);
        // `import.source(x)` (c == 1): the source phase, answered by a
        // hidden native - see import_source_name. `import.defer(x)` (c == 2)
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
    // THE TEMPORAL DEAD ZONE, decided statically (local::initialized_at): a
    // read of this frame's own `let`/`const`/`class` textually before the
    // declarator that initialises it - `let x = x + 1`, `use(y); const y = 1`
    // - runs before that declarator every time the scope runs, so it is the
    // ReferenceError of 9.1.1.1.6 whenever it runs, `typeof` included.
    // ONLY IN THE PROGRAM'S OWN TREE: a template substitution is re-parsed
    // from its own text (current_ast_ is that sub-tree), and its offsets are
    // relative to the piece, not the program - `${map.keys()}` after `const
    // map` was a false ReferenceError (ctcompile's string_snapshots fixture,
    // 2026-09-16).
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

// `yield* expr` (14.4.14): every value the inner iterator produces is
// yielded by this generator, and the expression's own value is what the
// inner iterator finally returned. GetIterator, the `next()` calls and the
// result checks are the three natives named at yield_delegate_open_name; the
// loop is here. A sync generator hands the inner RESULT OBJECT out untouched
// (step 7.a.vii) - generator_resume knows from the record the settle native
// leaves on the coroutine - and an async one awaits the result, then awaits
// and yields its value like any other async yield. `.throw()`/`.return()`
// while suspended here are generator_resume's, forwarded to the inner
// iterator without resuming the frame.
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
        // A parenthesised function is still anonymous - the parser keeps no
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

void compiler_impl::compile_delete(const vp::node & n, std::uint16_t dst) {
    const std::uint32_t mark = reg_mark();
    const vp::node & target = at(n.a);
    if (target.kind == vp::nk::member || target.kind == vp::nk::index) {
        // Through delete_ref_name, which answers the boolean 13.5.1.2 gives
        // and throws where it says - the opcodes answer nothing. `delete
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
        // `delete 1`, `delete void a.b`, `delete f()`: not a reference - the
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
    // `#x in o` (13.10.1): the private name is the KEY the brand is checked
    // under, spelled as the class body resolved it - never a name to read.
    if (n.text == "in" && at(n.a).kind == vp::nk::ident && at(n.a).text.starts_with('#')) {
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
    // `==` and `!=` are LOOSE. Compiling `!=` as `!==` made `1 != "1"` true,
    // which is the opposite of what the operator means.
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
    // so `0 ?? 5` is 0 and `"" ?? "x"` is "" - which is why anyone reaches
    // for it over `||` in the first place.
    const op test = n.text == "&&"    ? op::jump_if_false
                    : n.text == "?\?" ? op::jump_if_not_nullish
                                      : op::jump_if_true;
    const std::size_t skip = proto().emit(instruction{test, dst});
    compile_expr(n.b, dst);
    patch_here(skip);
}

void compiler_impl::compile_unary(const vp::node & n, std::uint16_t dst) {
    // `delete o.x` must NOT evaluate `o.x` - it takes the object and the
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
        // Promises here are SETTLED on creation - there is no event loop
        // suspending a frame - so awaiting one is reading its value, and
        // awaiting a plain value is the value. That is the same subset the previous engine
        // shipped, and it is what `await fetch(...)` needs.
        proto().emit(instruction{op::await_value, dst, operand});
    } else if (n.text == "+") {
        // NOT `move`. Unary plus is ToNumber, and the only reason a copy
        // survived here is that the difference hides: `+x` used in a string
        // concatenation reads identically whether it converted or not, and
        // that is most of where it appears. It stops hiding the moment the
        // result indexes an array.
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
        // Inside a `with`, the binding is decided ONCE, here (13.15.2 step 1
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
    // `super.x = v` and `super[k] = v`: a Super Reference is PUT with `this`
    // as the receiver (13.3.7.1 MakeSuperPropertyReference, 6.2.5.6 step
    // 4), so a data property lands on `this` and a frozen prototype is the
    // TypeError. The store goes to `this` - which also finds an inherited
    // setter, since `this` sits below the home object. (A read starts above
    // the home object; a compound assignment reads from `this`, a deviation
    // that only a setter or shadowing property on the home object itself can
    // observe.)
    const bool on_super = target.a >= 0 && at(target.a).kind == vp::nk::super_lit;
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
    }
}

void compiler_impl::emit_store(const reference & ref, std::uint16_t src) {
    if (ref.with) {
        reference plain = ref;
        plain.with = false;
        const std::size_t bound = proto().emit(instruction{op::jump_if_not_nullish, ref.with_reg});
        emit_store(plain, src);
        const std::size_t done = proto().emit(instruction{op::jump});
        patch_here(bound);
        proto().emit(instruction{op::set_prop, ref.with_reg, ref.with_name, src});
        patch_here(done);
        return;
    }
    switch (ref.what) {
    case reference::kind::local: proto().emit(instruction{op::move, ref.reg, src}); break;
    case reference::kind::boxed_local: proto().emit(instruction{op::cell_set, ref.reg, src}); break;
    case reference::kind::upvalue: proto().emit(instruction{op::set_upvalue, ref.reg, src}); break;
    case reference::kind::global: {
        // A COPY: the write interns names, which can grow `names` under a
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
    // The bitwise compounds. Every one of these opcodes already existed for
    // the plain operator; only the assignment form was missing, so `x <<= 1`
    // was refused while `x = x << 1` compiled. p5.js's noise module is
    // stopped by exactly that.
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

    // DESTRUCTURING ASSIGNMENT: `[a, b] = pair` and `({x} = o)`.
    //
    // The left side is not a pattern node - the parser met it in expression
    // position and read an array or object LITERAL, which is the only thing
    // it could have been at the time. Re-reading it as a pattern here is
    // what the grammar itself does, and it is why array literals had to
    // learn about holes: `[, ref] = pair` skips the first element.
    if (n.text == "=" && (target.kind == vp::nk::array || target.kind == vp::nk::object)) {
        compile_expr(n.b, dst);
        compile_pattern(n.a, dst);
        release_to(mark);
        return;
    }

    const reference ref = prepare_reference(target);

    if (n.text == "=") {
        // STRICT CODE MAY NOT CREATE A GLOBAL BY ASSIGNMENT (13.15.2 PutValue
        // step 4.a / 9.1.1.4.5 SetMutableBinding step 3): a read of the same
        // name throws the ReferenceError when it is unresolvable, and costs
        // nothing new - set_global stays the one write. BEFORE the right side
        // rather than after it, so `closure; set_global` stays adjacent for
        // ctcompile's closure lift (the order is observable only when the
        // right side has side effects AND the name is unresolvable). The
        // compound and logical forms below read first anyway. NOT IN A
        // MODULE, a deliberate leniency: a module's top-level `OUT = ...` is
        // how ctcompile's module fixtures talk to their host
        // (test/Runtime/Modules); the [[Set]] TypeErrors still apply there.
        // A name the script DECLARES with var/let/const is resolvable from
        // the start (hoisted, 16.1.7 GlobalDeclarationInstantiation) even
        // when the assignment runs before the declaration's line, so it gets
        // no probe - the top level has no hoisting pass to put it in the
        // table early.
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

void compiler_impl::compile_update(const vp::node & n, std::uint16_t dst) {
    const vp::node & target = at(n.a);
    const std::uint32_t mark = reg_mark();
    // Through the same reference machinery as compound assignment, so
    // `obj.n++` and `a[i]++` work and evaluate their target exactly once.
    const reference ref = prepare_reference(target);
    const std::uint16_t cur = alloc_reg();
    const std::uint16_t step = alloc_reg();
    emit_load(ref, cur);
    // BOTH THROUGH op::add - `x--` is `x + (-1)`, the same double - because
    // op::add is the internal add of ++/-- and the counters, never source
    // `+`, and the VM lets it take a BigInt beside its integral Number step
    // (`1n++` is `2n`; `1n - 1` is the mixing TypeError op::sub keeps).
    emit_const(step, value::number(n.text == "++" ? 1 : -1));
    // postfix yields the OLD value AS A NUMERIC (13.4.2.1 step 2: ToNumeric
    // of it, so `false++` reads 0 and an object its valueOf) - `+ 0` through
    // the same op is that conversion; prefix yields the new one.
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
