// compiler_impl - destructuring.
//
// Array and object patterns, in bindings and in assignments.
// `declaring` is what distinguishes `const {a} = o` from `({a} = o)`.
//
// One of the files carved out of a 3,845-line compile.cpp on 2026-08-09.
// The class is declared whole in compiler_impl.hpp beside this.

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

void compiler_impl::declare_pattern_names(std::int32_t pat) {
    if (frames_.size() <= 1) { return; } // a declaration there is a global
    std::vector<std::string> names;
    pattern_names(pat, names);
    for (std::string & name : names) {
        // ONLY the current scope decides this. was_predeclared is
        // function-scoped - it is how `var` and function declarations
        // hoist - and asking it here made a `const {x}` inside a block
        // reuse a slot hoisted at the top of the function, writing
        // through to it instead of shadowing it. A hoisted name IS a
        // local of the function's top scope, so when we are in that
        // scope this finds it anyway.
        if (find_local_in_current_scope(name) != nullptr) { continue; }
        const std::uint16_t reg = declare_local(name);
        proto().emit(instruction{op::load_undef, reg});
        if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, reg}); }
    }
}

void compiler_impl::compile_pattern_binding(std::int32_t pat, std::uint16_t src, bool declaring) {
    if (declaring) { declare_pattern_names(pat); }
    compile_pattern(pat, src);
}

void compiler_impl::compile_pattern(std::int32_t pat, std::uint16_t src) {
    if (pat < 0 || !out_.ok) { return; }
    const vp::node & n = at(pat);
    switch (n.kind) {
    case vp::nk::ident: emit_write(n.text, src); return;

    case vp::nk::member:
    case vp::nk::index: {
        // `[o.a, o.b] = pair` - a target that is not a name at all.
        const reference ref = prepare_reference(n);
        emit_store(ref, src);
        return;
    }

    case vp::nk::assign_pattern: {
        // The default applies when the value is UNDEFINED, so it is written
        // into the source register before the target ever sees it.
        const std::size_t skip = proto().emit(instruction{op::jump_if_defined, src});
        const std::uint32_t mark = reg_mark();
        compile_named_expr(n.b, src, at(n.a).kind == vp::nk::ident ? at(n.a).text : "");
        release_to(mark);
        patch_here(skip);
        compile_pattern(n.a, src);
        return;
    }

    case vp::nk::array_pattern:
        compile_array_pattern(
            kids(n), src, vp::nk::rest_element,
            [this](std::int32_t element, std::uint16_t item) { compile_pattern(element, item); });
        return;

    case vp::nk::object_pattern: {
        // RequireObjectCoercible (8.6.2 / 13.15.5.2): `{} = null` is a
        // TypeError even though nothing is read. A pattern with a named
        // property throws from its own get_prop; an empty or rest-first one
        // has to ask.
        const std::span<const std::int32_t> entries = kids(n);
        if (entries.empty() || at(entries.front()).kind == vp::nk::rest_element) {
            const std::uint32_t mark = reg_mark();
            const std::uint16_t scratch = alloc_reg();
            emit_iterator_native(require_object_name, scratch, src);
            release_to(mark);
        }
        // The keys already taken, so an object rest knows what to leave out.
        std::vector<std::string> taken;
        for (const std::int32_t entry : entries) {
            const vp::node & e = at(entry);
            const std::uint32_t mark = reg_mark();
            const std::uint16_t item = alloc_reg();
            if (e.kind == vp::nk::rest_element) {
                emit_rest_object(item, src, taken);
                compile_pattern(e.a, item);
            } else if ((e.d & 2) != 0 && e.a >= 0) { // a computed key
                const std::uint16_t key = alloc_reg();
                compile_expr(e.a, key);
                proto().emit(instruction{op::get_index, item, src, key});
                compile_pattern(e.b, item);
            } else {
                proto().emit(
                    instruction{op::get_prop, item, src, name_operand(std::string{e.text})});
                taken.emplace_back(e.text);
                compile_pattern(e.b, item);
            }
            release_to(mark);
        }
        return;
    }

    default: fail("unsupported destructuring target: " + kind_name(n.kind)); return;
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

// 8.6.2 IteratorBindingInitialization and 13.15.5.5, which are the same
// walk: GetIterator once, IteratorStep per element (a hole steps and drops),
// every remaining step into a rest array, and IteratorClose at the end when
// the iterator is not done. A `next()` that threw or answered a non-object
// marks the record done, so no close follows it.
//
// NOT CLOSED ON A THROW out of an element's default or a nested pattern
// (7.4.10's throw completion path), deliberately: that needs a handler round
// the pattern, and a protected region is what ctcompile's importer refuses a
// function for - `const [a, b] = pair` is in every modern bundle, so the
// handler cost every such function its native body. The normal-path close
// is the observable half.
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
            if (element >= 0) { bind(element, item); } // a hole steps and binds nothing
        }
        release_to(inner);
    }
    emit_iterator_native(iterator_close_name, record, record, 0);
    release_to(mark);
}

void compiler_impl::compile_literal_as_pattern(std::int32_t literal, std::uint16_t src) {
    const vp::node & n = at(literal);
    if (n.kind == vp::nk::array) {
        compile_array_pattern(kids(n), src, vp::nk::spread,
                              [this](std::int32_t element, std::uint16_t item) {
                                  compile_literal_target(element, item);
                              });
        return;
    }
    const std::span<const std::int32_t> entries = kids(n);
    if (entries.empty() || at(entries.front()).kind == vp::nk::spread) {
        const std::uint32_t mark = reg_mark();
        const std::uint16_t scratch = alloc_reg();
        emit_iterator_native(require_object_name, scratch, src);
        release_to(mark);
    }
    std::vector<std::string> taken;
    for (const std::int32_t entry : entries) {
        const vp::node & e = at(entry);
        const std::uint32_t mark = reg_mark();
        const std::uint16_t item = alloc_reg();
        if (e.kind == vp::nk::spread) {
            emit_rest_object(item, src, taken);
            compile_literal_target(e.a, item);
        } else {
            // `{a}` is shorthand (c == 2) and binds its own name; `{a: b}`
            // binds `b`.
            const std::int32_t value_node = e.c == 2 ? -1 : e.b;
            proto().emit(instruction{op::get_prop, item, src, name_operand(std::string{e.text})});
            taken.emplace_back(e.text);
            if (value_node < 0) {
                emit_write(e.text, item);
            } else {
                compile_literal_target(value_node, item);
            }
        }
        release_to(mark);
    }
}

void compiler_impl::compile_literal_target(std::int32_t target, std::uint16_t src) {
    if (target < 0) { return; }
    const vp::node & t = at(target);
    if (t.kind == vp::nk::array || t.kind == vp::nk::object) {
        compile_literal_as_pattern(target, src);
        return;
    }
    if (t.kind == vp::nk::assign) { // `[a = 1] = xs`
        const std::size_t skip = proto().emit(instruction{op::jump_if_defined, src});
        const std::uint32_t mark = reg_mark();
        compile_named_expr(t.b, src, at(t.a).kind == vp::nk::ident ? at(t.a).text : "");
        release_to(mark);
        patch_here(skip);
        compile_literal_target(t.a, src);
        return;
    }
    if (t.kind == vp::nk::ident) {
        emit_write(t.text, src);
        return;
    }
    const reference ref = prepare_reference(t);
    emit_store(ref, src);
}

void compiler_impl::emit_rest_object(std::uint16_t dst, std::uint16_t source,
                                     const std::vector<std::string> & taken) {
    proto().emit(instruction{op::new_object, dst});
    proto().emit(instruction{op::copy_props, dst, source});
    for (const std::string & key : taken) {
        proto().emit(instruction{op::delete_prop, dst, name_operand(key)});
    }
}

} // namespace ctbrowser::script::detail
