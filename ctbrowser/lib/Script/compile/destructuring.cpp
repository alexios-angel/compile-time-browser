//===- destructuring.cpp - Destructuring patterns ---------------*- C++ -*-===//
//
// compiler_impl members for array and object patterns, in bindings and in
// assignments. `declaring` is what distinguishes `const {a} = o` from
// `({a} = o)`. The class is declared in compiler_impl.hpp.
//
//===----------------------------------------------------------------------===//

#include "compiler_impl.hpp"

namespace ctbrowser::script::detail {

void compiler_impl::pattern_names(std::int32_t pat, std::vector<std::string> & out) const {
    if (pat < 0) { return; }
    const vp::node & n = at(pat);
    switch (n.kind) {
    case vp::nk::ident: out.emplace_back(n.text); return;
    case vp::nk::assign_pattern: pattern_names(n.a, out); return;
    case vp::nk::rest_element: pattern_names(n.a, out); return;
    case vp::nk::pattern_prop: pattern_names(n.b, out); return;
    case vp::nk::array_pattern:
    case vp::nk::object_pattern:
        for (const std::int32_t k : kids(n)) { pattern_names(k, out); }
        return;
    default: return;
    }
}

void compiler_impl::declare_pattern_names(std::int32_t pat, bool block_scoped,
                                          immutability readonly, bool lexical) {
    // A declaration at a script's top level is a global, unless it is
    // block-scoped (a catch parameter, or `let`/`const` inside a block):
    // those get a local so `catch ({ f })` does not leak `f` as a global
    // after the try (test262 annexB/language/global-code/skip-early-err-try).
    if (frames_.size() <= 1 && !block_scoped) { return; }
    std::vector<std::string> names;
    pattern_names(pat, names);
    for (const std::string & name : names) {
        // Only the current scope is checked. was_predeclared is
        // function-scoped (it tracks hoisted `var`s and functions), so using
        // it would make `const {x}` in a block reuse the function's hoisted
        // `x` instead of shadowing it. Hoisted names are locals of the top
        // scope, so this still finds them there.
        if (find_local_in_current_scope(name) != nullptr) { continue; }
        const std::uint16_t reg = declare_local(name);
        fn().locals.back().readonly = readonly;
        fn().locals.back().lexical = lexical;
        proto().emit(instruction{op::load_undef, reg});
        if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, reg}); }
    }
}

void compiler_impl::compile_pattern_binding(std::int32_t pat, std::uint16_t src, bool declaring,
                                            bool block_scoped, immutability readonly) {
    if (declaring) { declare_pattern_names(pat, block_scoped, readonly); }
    // A declaration's names at a script's top level are globals written
    // through emit_write as the declaration's own write; see declaring_.
    const bool outer_declaring = declaring_;
    declaring_ = declaring_ || declaring;
    compile_pattern(pat, src);
    declaring_ = outer_declaring;
}

void compiler_impl::compile_pattern(std::int32_t pat, std::uint16_t src) {
    if (pat < 0 || !out_.ok) { return; }
    const vp::node & n = at(pat);
    switch (n.kind) {
    case vp::nk::ident: emit_write(n.text, src); return;

    case vp::nk::member:
    case vp::nk::index: {
        // `[o.a, o.b] = pair`: a target that is not a name at all.
        const not_declaring expression{*this};
        const reference ref = prepare_reference(n);
        emit_store(ref, src);
        return;
    }

    // `for (f() in o)` in sloppy code is the one place a call reaches here
    // (early_errors' web_compat_call): the call runs, then the ReferenceError.
    case vp::nk::call: {
        const not_declaring expression{*this};
        const std::uint32_t mark = reg_mark();
        emit_call_target_error(pat, alloc_reg());
        release_to(mark);
        return;
    }

    // An array or object literal is a pattern too. The parser met the left
    // side of `[a, b] = pair` in expression position and read a literal,
    // the only thing it could have been at the time; reinterpreting it here
    // is what the grammar itself does (the assignment-pattern cover grammar).
    // The literal kinds spell the same shapes with different node kinds:
    // `spread` for `rest_element`, `assign` for `assign_pattern`, and a
    // `prop` whose computed-key bit is `d & 1` rather than `d & 2` and whose
    // shorthand `{a}` has no target node (c == 2). The early-error pass has
    // already refused every target that is not a valid pattern.
    case vp::nk::assign:
    case vp::nk::assign_pattern: {
        // The default applies when the value is undefined, so it is written
        // into the source register before the target ever sees it.
        const std::size_t skip = proto().emit(instruction{op::jump_if_defined, src});
        const std::uint32_t mark = reg_mark();
        {
            // The default is an expression, not the declaration's write:
            // `const {a = (b = 1)} = o` in strict code still refuses `b`.
            const not_declaring expression{*this};
            compile_named_expr(n.b, src, at(n.a).kind == vp::nk::ident ? at(n.a).text : "");
        }
        release_to(mark);
        patch_here(skip);
        compile_pattern(n.a, src);
        return;
    }

    case vp::nk::array:
    case vp::nk::array_pattern:
        compile_array_pattern(
            kids(n), src, n.kind == vp::nk::array ? vp::nk::spread : vp::nk::rest_element,
            [this](std::int32_t element, std::uint16_t item) { compile_pattern(element, item); });
        return;

    case vp::nk::object:
    case vp::nk::object_pattern: {
        const bool literal = n.kind == vp::nk::object;
        const auto rest_kind = literal ? vp::nk::spread : vp::nk::rest_element;
        const std::int32_t computed_bit = literal ? 1 : 2;
        // RequireObjectCoercible (8.6.2 / 13.15.5.2): `{} = null` is a
        // TypeError even though nothing is read. A pattern with a named
        // property throws from its own get_prop; an empty or rest-first one
        // has to ask.
        const std::span<const std::int32_t> entries = kids(n);
        if (entries.empty() || at(entries.front()).kind == rest_kind) {
            const std::uint32_t mark = reg_mark();
            const std::uint16_t scratch = alloc_reg();
            emit_iterator_native(require_object_name, scratch, src);
            release_to(mark);
        }
        // The keys already taken, so an object rest knows what to leave out:
        // the names, and the computed keys, which stay in registers of their
        // own for the rest to delete (14.3.3.3: excludedNames holds every
        // property key evaluated so far, `{[a]: b, ...rest}` included).
        std::vector<std::string> taken;
        std::vector<std::uint16_t> taken_keys;
        for (const std::int32_t entry : entries) {
            const vp::node & e = at(entry);
            const bool computed = (e.d & computed_bit) != 0 && e.a >= 0 && e.kind != rest_kind;
            std::uint16_t key = 0;
            if (computed) {
                key = alloc_reg(); // Outlives this entry, for the rest.
                taken_keys.push_back(key);
                const not_declaring expression{*this};
                compile_expr(e.a, key);
            }
            const std::uint32_t mark = reg_mark();
            const std::uint16_t item = alloc_reg();
            if (e.kind == rest_kind) {
                emit_rest_object(item, src, taken, taken_keys);
                compile_pattern(e.a, item);
            } else if (computed) {
                proto().emit(instruction{op::get_index, item, src, key});
                compile_pattern(e.b, item);
            } else {
                proto().emit(
                    instruction{op::get_prop, item, src, name_operand(std::string{e.text})});
                taken.emplace_back(e.text);
                if (literal && e.c == 2 && e.b < 0) {
                    emit_write(e.text, item); // Shorthand `{a}` binds its own name.
                } else {
                    compile_pattern(e.b, item);
                }
            }
            release_to(mark);
        }
        return;
    }

    default:
        fail("unsupported destructuring target: AST kind " +
             std::to_string(static_cast<int>(n.kind)));
        return;
    }
}

void compiler_impl::emit_iterator_native(std::string_view name, std::uint16_t dst,
                                         std::uint16_t arg, int flag) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t callee = alloc_reg();
    proto().emit(instruction::with_bx(op::get_global, callee, intern_name(std::string{name})));
    const std::uint16_t first = alloc_reg();
    proto().emit(instruction{op::move, first, arg});
    std::uint16_t argc = 1;
    if (flag >= 0) {
        const std::uint16_t second = alloc_reg();
        proto().emit(instruction{flag != 0 ? op::load_true : op::load_false, second});
        argc = 2;
    }
    proto().emit(instruction{op::call, callee, argc});
    proto().emit(instruction{op::move, dst, callee});
    release_to(mark);
}

/// ECMA-262 8.6.2 IteratorBindingInitialization and 13.15.5.5, which are the
/// same walk: GetIterator once, IteratorStep per element (a hole steps and drops),
/// every remaining step into a rest array, and IteratorClose at the end when
/// the iterator is not done. A `next()` that threw or answered a non-object
/// marks the record done, so no close follows it.
///
/// Deliberately not closed when an element's default or a nested pattern
/// throws (7.4.10, throw completion). Closing would need an exception handler
/// around the pattern, and the ctcompile importer refuses to compile any
/// function containing a handler natively; `const [a, b] = pair` is too
/// common to give that up. Only the normal-path close is done.
void compiler_impl::compile_array_pattern(
    std::span<const std::int32_t> elements, std::uint16_t src, vp::nk rest_kind,
    const std::function<void(std::int32_t, std::uint16_t)> & bind) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t record = alloc_reg();
    emit_iterator_native(iterator_open_name, record, src);
    for (const std::int32_t element : elements) {
        const std::uint32_t inner = reg_mark();
        const std::uint16_t item = alloc_reg();
        if (element >= 0 && at(element).kind == rest_kind) {
            proto().emit(instruction{op::new_array, item});
            const std::uint16_t step = alloc_reg();
            const std::uint16_t done = alloc_reg();
            const std::size_t top = proto().code.size();
            emit_iterator_native(iterator_next_name, step, record);
            proto().emit(instruction{op::get_prop, done, record, name_operand("done")});
            const std::size_t exit = proto().emit(instruction{op::jump_if_true, done});
            proto().emit(instruction{op::append, item, step});
            patch_jump(proto().emit(instruction{op::jump}), top);
            patch_here(exit);
            bind(at(element).a, item);
        } else {
            emit_iterator_native(iterator_next_name, item, record);
            if (element >= 0) { bind(element, item); } // A hole steps and binds nothing.
        }
        release_to(inner);
    }
    emit_iterator_native(iterator_close_name, record, record, 0);
    release_to(mark);
}

void compiler_impl::emit_rest_object(std::uint16_t dst, std::uint16_t source,
                                     const std::vector<std::string> & taken,
                                     const std::vector<std::uint16_t> & taken_keys) {
    proto().emit(instruction{op::new_object, dst});
    proto().emit(instruction{op::copy_props, dst, source});
    for (const std::string & key : taken) {
        proto().emit(instruction{op::delete_prop, dst, name_operand(key)});
    }
    for (const std::uint16_t key : taken_keys) {
        proto().emit(instruction{op::delete_index, dst, key});
    }
}

} // namespace ctbrowser::script::detail
