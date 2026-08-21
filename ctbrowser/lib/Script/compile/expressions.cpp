// compiler_impl - expressions.
//
// `compile_expr_inner` is the hub - a 154-line dispatch that most
// of the rest of the compiler eventually reaches.
//
// One of the files carved out of a 3,845-line compile.cpp on 2026-08-09.
// The class is declared whole in compiler_impl.hpp beside this.

#include "compiler_impl.hpp"

namespace ctbrowser::script::detail {

void compiler_impl::compile_expr(std::int32_t idx, std::uint16_t dst) {
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
        // an object expression - there is no value `super` evaluates to.
        if (n.a >= 0 && at(n.a).kind == vp::nk::super_lit) {
            emit_super_base(dst);
        } else {
            compile_expr(n.a, dst);
        }
        const std::uint16_t name = name_operand(std::string{n.text});
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
    case vp::nk::this_lit: proto().emit(instruction{op::load_this, dst}); break;
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
        const std::uint16_t sent = alloc_reg();
        if (n.a >= 0) {
            compile_expr(n.a, sent);
        } else {
            proto().emit(instruction{op::load_undef, sent});
        }
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
        proto().emit(instruction{op::dyn_import, dst, spec});
        release_to(mark);
        break;
    }
    case vp::nk::import_meta:
        fail("`import.meta` is not implemented yet - ES modules are staged in "
             "docs/plans/modules.md");
        proto().emit(instruction{op::load_undef, dst});
        break;
    case vp::nk::tagged:
        fail("tagged template literals are not in this VM subset");
        proto().emit(instruction{op::load_undef, dst});
        break;
    default:
        // "AST kind 13" is a number, not a diagnostic. Naming the construct
        // is the difference between a report you can act on and one you
        // have to go and decode: kind 13 is `f(...args)`, and it stops
        // thirteen of p5.js's seventy-one modules on its own.
        fail(std::string{"unsupported syntax in this VM subset: "} + kind_name(n.kind));
        proto().emit(instruction{op::load_undef, dst});
        break;
    }
}

void compiler_impl::emit_write(std::string_view name_text, std::uint16_t src) {
    if (const local * l = find_local_entry(fn(), name_text)) {
        if (l->boxed) {
            proto().emit(instruction{op::cell_set, l->reg, src});
        } else {
            proto().emit(instruction{op::move, l->reg, src});
        }
        return;
    }
    const int up = resolve_upvalue(frames_.size() - 1, name_text);
    if (up >= 0) {
        proto().emit(instruction{op::set_upvalue, static_cast<std::uint16_t>(up), src});
        return;
    }
    proto().emit(instruction::with_bx(op::set_global, src, intern_name(std::string{name_text})));
}

void compiler_impl::compile_ident(const vp::node & n, std::uint16_t dst) {
    if (const local * l = find_local_entry(fn(), n.text)) {
        if (l->boxed) {
            proto().emit(instruction{op::cell_get, dst, l->reg});
        } else {
            proto().emit(instruction{op::move, dst, l->reg});
        }
        return;
    }
    const int up = resolve_upvalue(frames_.size() - 1, n.text);
    if (up >= 0) {
        proto().emit(instruction{op::get_upvalue, dst, static_cast<std::uint16_t>(up)});
        return;
    }
    // `arguments` is not synthesised here: compile_function_body made it a
    // real local at entry, so it resolved above as a local or an upvalue.
    // Reaching this point means the mention is at TOP LEVEL, where a script
    // has no arguments and reading the name is an ordinary global lookup -
    // which is what a browser does too.
    const std::uint16_t name = name_operand(std::string{n.text});
    proto().emit(instruction::with_bx(op::get_global, dst, name));
}

void compiler_impl::compile_delete(const vp::node & n, std::uint16_t dst) {
    const std::uint32_t mark = reg_mark();
    const vp::node & target = at(n.a);
    if (target.kind == vp::nk::member) {
        const std::uint16_t object = alloc_reg();
        compile_expr(target.a, object);
        proto().emit(instruction{op::delete_prop, object, name_operand(std::string{target.text})});
        emit_const(dst, value::boolean(true));
    } else if (target.kind == vp::nk::index) {
        const std::uint16_t object = alloc_reg();
        compile_expr(target.a, object);
        const std::uint16_t key = alloc_reg();
        compile_expr(target.b, key);
        proto().emit(instruction{op::delete_index, object, key});
        emit_const(dst, value::boolean(true));
    } else {
        emit_const(dst, value::boolean(false));
    }
    release_to(mark);
}

void compiler_impl::compile_binary(const vp::node & n, std::uint16_t dst) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t lhs = alloc_reg();
    const std::uint16_t rhs = alloc_reg();
    compile_expr(n.a, lhs);
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
    compile_expr(n.a, operand);
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
    if (target.kind == vp::nk::member) {
        out.what = reference::kind::member;
        out.reg = alloc_reg();
        compile_expr(target.a, out.reg);
        out.name = name_operand(std::string{target.text});
        return out;
    }
    if (target.kind == vp::nk::index) {
        out.what = reference::kind::index;
        out.reg = alloc_reg();
        out.key = alloc_reg();
        compile_expr(target.a, out.reg);
        compile_expr(target.b, out.key);
        return out;
    }
    fail("unsupported assignment target");
    return out;
}

void compiler_impl::emit_load(const reference & ref, std::uint16_t dst) {
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
    switch (ref.what) {
    case reference::kind::local: proto().emit(instruction{op::move, ref.reg, src}); break;
    case reference::kind::boxed_local: proto().emit(instruction{op::cell_set, ref.reg, src}); break;
    case reference::kind::upvalue: proto().emit(instruction{op::set_upvalue, ref.reg, src}); break;
    case reference::kind::global:
        proto().emit(instruction::with_bx(op::set_global, src, ref.name));
        break;
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
        compile_literal_as_pattern(n.a, dst);
        release_to(mark);
        return;
    }

    const reference ref = prepare_reference(target);

    if (n.text == "=") {
        compile_expr(n.b, dst);
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
        compile_expr(n.b, dst);
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
    const std::uint16_t one = alloc_reg();
    emit_load(ref, cur);
    emit_const(one, value::number(1));
    // postfix yields the OLD value, prefix the new one
    if (n.b == 0) { proto().emit(instruction{op::move, dst, cur}); }
    proto().emit(instruction{n.text == "++" ? op::add : op::sub, cur, cur, one});
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

void compiler_impl::compile_template(const vp::node & n, std::uint16_t dst) {
    std::string_view raw = n.text;
    if (raw.size() >= 2 && raw.front() == '`' && raw.back() == '`') {
        raw = raw.substr(1, raw.size() - 2);
    }
    const std::uint32_t mark = reg_mark();
    const std::uint16_t piece = alloc_reg();
    bool started = false;

    const auto append = [&](std::uint16_t src) {
        if (!started) {
            proto().emit(instruction{op::move, dst, src});
            started = true;
        } else {
            proto().emit(instruction{op::concat, dst, dst, src});
        }
    };
    const auto append_literal = [&](std::string text) {
        if (text.empty() && started) { return; }
        emit_string(piece, std::move(text));
        append(piece);
    };

    std::string literal;
    for (std::size_t i = 0; i < raw.size();) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            // One level of escape handling, so `\n` and `\`` behave.
            const char c = raw[i + 1];
            literal += c == 'n' ? '\n' : (c == 't' ? '\t' : (c == 'r' ? '\r' : c));
            i += 2;
            continue;
        }
        if (raw[i] == '$' && i + 1 < raw.size() && raw[i + 1] == '{') {
            append_literal(std::move(literal));
            literal.clear();
            // Find the matching brace, counting nesting so an object
            // literal or a nested template inside the hole survives.
            std::size_t depth = 1;
            std::size_t at_char = i + 2;
            const std::size_t start = at_char;
            while (at_char < raw.size() && depth > 0) {
                if (raw[at_char] == '{') { ++depth; }
                if (raw[at_char] == '}') { --depth; }
                if (depth > 0) { ++at_char; }
            }
            // PARENTHESISED, so the hole is parsed as an EXPRESSION.
            // `${ {v: 1}.v }` parses as a program otherwise, and a leading
            // brace at statement position is a BLOCK - so the object
            // literal became a labelled statement and the whole hole
            // evaluated to undefined, silently.
            compile_owned_expr("(" + std::string{raw.substr(start, at_char - start)} + ")", piece);
            // Concatenation is what stringifies the value, which is exactly
            // the coercion a template literal performs.
            append(piece);
            i = at_char + 1;
            continue;
        }
        literal += raw[i++];
    }
    append_literal(std::move(literal));
    if (!started) { emit_string(dst, std::string{}); }
    release_to(mark);
}

void compiler_impl::emit_super_base(std::uint16_t dst) {
    proto().emit(instruction{op::load_home, dst});
    proto().emit(instruction{op::get_proto, dst, dst});
}

bool compiler_impl::any_spread(std::span<const std::int32_t> args) const {
    for (const std::int32_t arg : args) {
        if (at(arg).kind == vp::nk::spread) { return true; }
    }
    return false;
}

void compiler_impl::emit_argument_array(std::span<const std::int32_t> args, std::uint16_t dst) {
    proto().emit(instruction{op::new_array, dst});
    const std::uint32_t mark = reg_mark();
    for (const std::int32_t arg : args) {
        const std::uint16_t v = alloc_reg();
        if (at(arg).kind == vp::nk::spread) {
            compile_expr(at(arg).a, v);
            emit_append_all(dst, v);
        } else {
            compile_expr(arg, v);
            proto().emit(instruction{op::append, dst, v});
        }
        release_to(mark);
    }
}

void compiler_impl::compile_spread_call(const vp::node & n, std::uint16_t dst) {
    const std::span<const std::int32_t> args = kids(n);
    const vp::node & callee = at(n.a);
    const std::uint32_t mark = reg_mark();
    const std::uint16_t target = alloc_reg();
    const std::uint16_t self = alloc_reg();

    const bool super_method =
        callee.kind == vp::nk::member && callee.a >= 0 && at(callee.a).kind == vp::nk::super_lit;
    if (callee.kind == vp::nk::super_lit || super_method) {
        emit_super_base(target);
        const std::string name = super_method ? std::string{callee.text} : "constructor";
        proto().emit(instruction{op::get_prop, target, target, name_operand(name)});
        proto().emit(instruction{op::load_this, self});
    } else if (callee.kind == vp::nk::member) {
        compile_expr(callee.a, self);
        proto().emit(
            instruction{op::get_prop, target, self, name_operand(std::string{callee.text})});
    } else if (callee.kind == vp::nk::index) {
        compile_expr(callee.a, self);
        const std::uint16_t key = alloc_reg();
        compile_expr(callee.b, key);
        proto().emit(instruction{op::get_index, target, self, key});
    } else {
        compile_expr(n.a, target);
        proto().emit(instruction{op::load_undef, self});
    }

    const std::uint16_t argv = alloc_reg();
    emit_argument_array(args, argv);
    // `super(...)` - NOT `super.m(...)` - carries new.target into the base
    // constructor. Babylon reads it there to hang decorator metadata off
    // the class actually being constructed, and got undefined.
    if (callee.kind == vp::nk::super_lit) { proto().emit(instruction{op::pass_new_target}); }
    proto().emit(instruction{op::apply, target, argv, self});
    proto().emit(instruction{op::move, dst, target});
    release_to(mark);
}

void compiler_impl::compile_call(const vp::node & n, std::uint16_t dst) {
    const std::span<const std::int32_t> args = kids(n);
    if (any_spread(args)) {
        compile_spread_call(n, dst);
        return;
    }
    const vp::node & callee = at(n.a);
    const std::uint32_t mark = reg_mark();
    const std::uint16_t base = alloc_reg();

    const bool super_method =
        callee.kind == vp::nk::member && callee.a >= 0 && at(callee.a).kind == vp::nk::super_lit;
    if (callee.kind == vp::nk::super_lit || super_method) {
        // `super(...)` is the parent CONSTRUCTOR run against this same
        // object - it does not make a new one - so the receiver is `this`
        // in both forms.
        emit_super_base(base);
        const std::string name = super_method ? std::string{callee.text} : "constructor";
        proto().emit(instruction{op::get_prop, base, base, name_operand(name)});
        for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
        const std::uint16_t self = alloc_reg();
        proto().emit(instruction{op::load_this, self});
        // `super(...)` - NOT `super.m(...)` - carries new.target into the
        // base constructor. THIS is the path a plain super() takes; the
        // spread form a few hundred lines up is the other one, and marking
        // only that one left the common case still reading undefined.
        if (callee.kind == vp::nk::super_lit) { proto().emit(instruction{op::pass_new_target}); }
        proto().emit(
            instruction{op::call_receiver, base, static_cast<std::uint16_t>(args.size()), self});
        proto().emit(instruction{op::move, dst, base});
        release_to(mark);
        return;
    }

    // `x?.m(...)` IS A METHOD CALL. The parser gives it as call(opt_member(x,
    // 'm')), so the callee is an opt_member and this fell through to the plain
    // path below - which calls the function with NO receiver. `this` was
    // undefined inside the method, and for a primitive receiver that means the
    // wrong answer rather than an error: `s?.trim()` was undefined,
    // `(5)?.toFixed(1)` was NaN, `[1,2]?.join('-')` was "".
    //
    // It cost every colour string in p5.js. `parse$4` opens with `String(str)
    // ?.trim()`, so every `fill('#ff0000')`, `color('red')` and
    // `background('#fff')` threw "Invalid color string" - and an object
    // receiver hid it, because a method that ignores `this` works either way.
    const bool optional_member =
        callee.kind == vp::nk::opt_member || callee.kind == vp::nk::opt_index;
    if (optional_member) {
        compile_expr(callee.a, base); // the receiver, which may be nullish
        // The short-circuit, released before the argument window is reserved
        // so the arguments stay contiguous from base+1.
        {
            const std::uint32_t guard = reg_mark();
            const std::uint16_t nullish = alloc_reg();
            proto().emit(instruction{op::load_null, nullish});
            const std::uint16_t test = alloc_reg();
            proto().emit(instruction{op::loose_equal, test, base, nullish});
            optional_exits_.push_back(proto().emit(instruction{op::jump_if_true, test}));
            release_to(guard);
        }
        if (callee.kind == vp::nk::opt_member) {
            for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
            proto().emit(instruction{op::call_method, base, static_cast<std::uint16_t>(args.size()),
                                     name_operand(std::string{callee.text})});
        } else {
            std::vector<std::uint16_t> arg_regs;
            arg_regs.reserve(args.size());
            for (std::size_t i = 0; i < args.size(); ++i) { arg_regs.push_back(alloc_reg()); }
            const std::uint16_t key = alloc_reg();
            compile_expr(callee.b, key);
            for (std::size_t i = 0; i < args.size(); ++i) { compile_expr(args[i], arg_regs[i]); }
            proto().emit(
                instruction{op::call_computed, base, static_cast<std::uint16_t>(args.size()), key});
        }
        proto().emit(instruction{op::move, dst, base});
        release_to(mark);
        return;
    }

    if (callee.kind == vp::nk::member) {
        compile_expr(callee.a, base); // the receiver
        for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
        const std::uint16_t name = name_operand(std::string{callee.text});
        proto().emit(
            instruction{op::call_method, base, static_cast<std::uint16_t>(args.size()), name});
    } else if (callee.kind == vp::nk::index) {
        // `obj[name](...)` is a METHOD call: the receiver is obj. Compiling
        // it as a plain call leaves `this` undefined inside the method.
        compile_expr(callee.a, base); // the receiver
        // THE ARGUMENT WINDOW IS RESERVED BEFORE THE KEY IS EVALUATED.
        //
        // call_computed reads its arguments from base+1 upwards, so base+1
        // belongs to argument 0 and to nothing else. Evaluating the key into
        // a register first took base+1, pushed the arguments up one, and the
        // VM then read the KEY as argument 0 and dropped the last argument -
        // `t['k'](1, 2, 3)` arrived as `('k', 1, 2)`.
        //
        // Reserving first and filling after keeps both rules: the arguments
        // are contiguous from base+1, and the key is still evaluated BEFORE
        // them, which is the order JavaScript specifies.
        std::vector<std::uint16_t> arg_regs;
        arg_regs.reserve(args.size());
        for (std::size_t i = 0; i < args.size(); ++i) { arg_regs.push_back(alloc_reg()); }
        const std::uint16_t key = alloc_reg();
        compile_expr(callee.b, key);
        for (std::size_t i = 0; i < args.size(); ++i) { compile_expr(args[i], arg_regs[i]); }
        proto().emit(
            instruction{op::call_computed, base, static_cast<std::uint16_t>(args.size()), key});
    } else {
        compile_expr(n.a, base);
        for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
        proto().emit(instruction{op::call, base, static_cast<std::uint16_t>(args.size())});
    }
    proto().emit(instruction{op::move, dst, base});
    release_to(mark);
}

void compiler_impl::compile_new(const vp::node & n, std::uint16_t dst) {
    const std::span<const std::int32_t> args = kids(n);
    const std::uint32_t mark = reg_mark();
    const std::uint16_t base = alloc_reg();
    if (any_spread(args)) {
        compile_expr(n.a, base);
        const std::uint16_t argv = alloc_reg();
        emit_argument_array(args, argv);
        proto().emit(instruction{op::construct_apply, base, argv});
        proto().emit(instruction{op::move, dst, base});
        release_to(mark);
        return;
    }
    compile_expr(n.a, base);
    for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
    proto().emit(instruction{op::construct, base, static_cast<std::uint16_t>(args.size())});
    proto().emit(instruction{op::move, dst, base});
    release_to(mark);
}

bool compiler_impl::chain_has_optional(std::int32_t idx) const {
    for (std::int32_t at = idx; at >= 0;) {
        const vp::node & n = this->at(at);
        switch (n.kind) {
        case vp::nk::opt_member:
        case vp::nk::opt_index:
        case vp::nk::opt_call: return true;
        case vp::nk::member:
        case vp::nk::index:
        case vp::nk::call: at = n.a; continue;
        default: return false;
        }
    }
    return false;
}

void compiler_impl::compile_chain(std::int32_t idx, std::uint16_t dst) {
    std::vector<std::size_t> outer;
    outer.swap(optional_exits_);
    const bool root = !in_chain_;
    in_chain_ = true;
    compile_expr_inner(idx, dst);
    if (root) {
        in_chain_ = false;
        if (!optional_exits_.empty()) {
            const std::size_t done = proto().emit(instruction{op::jump});
            for (const std::size_t site : optional_exits_) { patch_here(site); }
            proto().emit(instruction{op::load_undef, dst});
            patch_here(done);
        }
    }
    optional_exits_.swap(outer);
    if (!root) {
        // a nested chain hands its exits back to the enclosing one
        for (const std::size_t site : outer) { optional_exits_.push_back(site); }
    }
}

void compiler_impl::compile_optional(const vp::node & n, std::uint16_t dst) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t object = alloc_reg();
    compile_expr(n.a, object);

    // null and undefined both short-circuit; nothing else does.
    const std::uint16_t nullish = alloc_reg();
    proto().emit(instruction{op::load_null, nullish});
    const std::uint16_t test = alloc_reg();
    proto().emit(instruction{op::loose_equal, test, object, nullish});
    const std::size_t skip = proto().emit(instruction{op::jump_if_true, test});

    if (n.kind == vp::nk::opt_member) {
        proto().emit(instruction{op::get_prop, dst, object, name_operand(std::string{n.text})});
    } else if (n.kind == vp::nk::opt_index) {
        const std::uint16_t key = alloc_reg();
        compile_expr(n.b, key);
        proto().emit(instruction{op::get_index, dst, object, key});
    } else { // opt_call
        const std::span<const std::int32_t> args = kids(n);
        const std::uint16_t base = alloc_reg();
        proto().emit(instruction{op::move, base, object});
        for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
        proto().emit(instruction{op::call, base, static_cast<std::uint16_t>(args.size())});
        proto().emit(instruction{op::move, dst, base});
    }
    // The exit belongs to the CHAIN, not to this link. compile_chain
    // patches every one of them to a single point past the whole thing.
    optional_exits_.push_back(skip);
    release_to(mark);
}

void compiler_impl::compile_sequence(const vp::node & n, std::uint16_t dst) {
    // A SEQUENCE IS BINARY, not a list. The parser builds `a, b, c` as
    // seq(seq(a, b), c) - left-nested, two children per node - and this
    // read `kids(n)` instead, which for such a node is EMPTY. So every
    // comma expression evaluated nothing at all and produced undefined,
    // silently, from the day the operator was added.
    //
    // Nothing caught it because the operator was added to the parser with
    // no test that a comma expression has EFFECTS - only that it parsed.
    // `_createClass(e, r, t) { return r && _defineProperties(...), ..., e; }`
    // is how every Babel-transpiled class installs its methods, so this one
    // omission silently emptied every such class in p5.js.
    compile_expr(n.a, dst);
    compile_expr(n.b, dst);
}

void compiler_impl::compile_regex_literal(const vp::node & n, std::uint16_t dst) {
    const std::string_view literal = n.text;
    const std::size_t close = literal.rfind('/');
    if (literal.size() < 2 || literal.front() != '/' || close == 0) {
        fail("malformed regular expression literal (" + std::string{literal} + ")");
        proto().emit(instruction{op::load_undef, dst});
        return;
    }
    const std::uint32_t mark = reg_mark();
    const std::uint16_t callee = alloc_reg();
    proto().emit(instruction::with_bx(op::get_global, callee,
                                      intern_name(std::string{regexp_factory_name})));
    const std::uint16_t source = alloc_reg();
    emit_string(source, std::string{literal.substr(1, close - 1)});
    const std::uint16_t flags = alloc_reg();
    emit_string(flags, std::string{literal.substr(close + 1)});
    proto().emit(instruction{op::call, callee, 2});
    proto().emit(instruction{op::move, dst, callee});
    release_to(mark);
}

void compiler_impl::compile_array(const vp::node & n, std::uint16_t dst) {
    proto().emit(instruction{op::new_array, dst});
    const std::uint32_t mark = reg_mark();
    for (const std::int32_t element : kids(n)) {
        const std::uint16_t v = alloc_reg();
        // A HOLE. `[, x]` and `[a, , b]` are legal, and an element list is
        // the one place kids() yields -1 - which is why at() refuses a
        // negative index rather than reading past the pool.
        if (element < 0) {
            proto().emit(instruction{op::load_undef, v});
            proto().emit(instruction{op::append, dst, v});
            release_to(mark);
            continue;
        }
        if (at(element).kind == vp::nk::spread) {
            // `[...a]` appends a's ELEMENTS, not a itself. Compiled as an
            // index loop rather than an opcode, because that is all it is.
            compile_expr(at(element).a, v);
            emit_append_all(dst, v);
        } else {
            compile_expr(element, v);
            proto().emit(instruction{op::append, dst, v});
        }
        release_to(mark);
    }
}

void compiler_impl::emit_append_all(std::uint16_t target, std::uint16_t source) {
    const std::uint32_t mark = reg_mark();
    // SPREAD TAKES ANYTHING ITERABLE, not just an array. This walks a
    // `length`, so `[...new Set(v)]` and `f(...map.keys())` produced nothing
    // at all - see context::iterable_values for what that cost.
    proto().emit(instruction{op::iterable, source, source});
    const std::uint16_t length = alloc_reg();
    proto().emit(instruction{op::get_prop, length, source, name_operand("length")});
    const std::uint16_t index = alloc_reg();
    emit_const(index, value::number(0));
    const std::uint16_t one = alloc_reg();
    emit_const(one, value::number(1));
    const std::uint16_t test = alloc_reg();
    const std::uint16_t item = alloc_reg();
    const std::size_t top = proto().code.size();
    proto().emit(instruction{op::less, test, index, length});
    const std::size_t exit = proto().emit(instruction{op::jump_if_false, test});
    proto().emit(instruction{op::get_index, item, source, index});
    proto().emit(instruction{op::append, target, item});
    proto().emit(instruction{op::add, index, index, one});
    patch_jump(proto().emit(instruction{op::jump}), top);
    patch_here(exit);
    release_to(mark);
}

void compiler_impl::compile_object(const vp::node & n, std::uint16_t dst) {
    proto().emit(instruction{op::new_object, dst});
    const std::uint32_t mark = reg_mark();
    for (const std::int32_t p : kids(n)) {
        const vp::node & prop = at(p);
        if (prop.kind == vp::nk::spread) {
            // `{...o}` copies o's own properties in, and a later key still
            // wins - which is why this is a copy at this point in the
            // sequence rather than a merge at the end.
            const std::uint16_t source = alloc_reg();
            compile_expr(prop.a, source);
            proto().emit(instruction{op::copy_props, dst, source});
            release_to(mark);
            continue;
        }
        if (prop.kind != vp::nk::prop) {
            fail("unsupported object literal member");
            return;
        }
        if (prop.c == 3) {
            // An accessor, not a data property. `d` bit2 says which half.
            const std::uint16_t fnreg = alloc_reg();
            compile_expr(prop.b, fnreg);
            const std::uint16_t name = name_operand(decode_string_literal(prop.text));
            proto().emit(instruction{(prop.d & 4) != 0 ? op::define_setter : op::define_getter, dst,
                                     name, fnreg});
            release_to(mark);
            continue;
        }
        const std::uint16_t v = alloc_reg();
        if (prop.b >= 0) {
            compile_expr(prop.b, v);
        } else {
            compile_ident(prop, v); // shorthand { x }
        }
        // A computed key - `{[k]: v}`, and also `{"a": v}` and `{1: v}`,
        // which the parser routes the same way so quotes and escapes get
        // cooked by evaluating the literal.
        if ((prop.d & 1) != 0) {
            const std::uint16_t key = alloc_reg();
            compile_expr(prop.a, key);
            proto().emit(instruction{op::set_index, dst, key, v});
        } else {
            const std::uint16_t name = name_operand(decode_string_literal(prop.text));
            proto().emit(instruction{op::set_prop, dst, name, v});
        }
        release_to(mark);
    }
}

} // namespace ctbrowser::script::detail
