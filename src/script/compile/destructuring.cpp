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
        compile_expr(n.b, src);
        release_to(mark);
        patch_here(skip);
        compile_pattern(n.a, src);
        return;
    }

    case vp::nk::array_pattern: {
        const std::span<const std::int32_t> elements = kids(n);
        for (std::size_t i = 0; i < elements.size(); ++i) {
            if (elements[i] < 0) { continue; } // a hole binds nothing
            const std::uint32_t mark = reg_mark();
            const std::uint16_t item = alloc_reg();
            if (at(elements[i]).kind == vp::nk::rest_element) {
                // everything from here on, as a new array
                emit_slice_from(item, src, i);
                compile_pattern(at(elements[i]).a, item);
            } else {
                const std::uint16_t index = alloc_reg();
                emit_const(index, value::number(static_cast<double>(i)));
                proto().emit(instruction{op::get_index, item, src, index});
                compile_pattern(elements[i], item);
            }
            release_to(mark);
        }
        return;
    }

    case vp::nk::object_pattern: {
        // The keys already taken, so an object rest knows what to leave out.
        std::vector<std::string> taken;
        for (const std::int32_t entry : kids(n)) {
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

void compiler_impl::compile_literal_as_pattern(std::int32_t literal, std::uint16_t src) {
    const vp::node & n = at(literal);
    if (n.kind == vp::nk::array) {
        const std::span<const std::int32_t> elements = kids(n);
        for (std::size_t i = 0; i < elements.size(); ++i) {
            if (elements[i] < 0) { continue; } // `[, x] = pair` skips one
            const std::uint32_t mark = reg_mark();
            const std::uint16_t item = alloc_reg();
            if (at(elements[i]).kind == vp::nk::spread) {
                emit_slice_from(item, src, i);
                compile_literal_target(at(elements[i]).a, item);
            } else {
                const std::uint16_t index = alloc_reg();
                emit_const(index, value::number(static_cast<double>(i)));
                proto().emit(instruction{op::get_index, item, src, index});
                compile_literal_target(elements[i], item);
            }
            release_to(mark);
        }
        return;
    }
    std::vector<std::string> taken;
    for (const std::int32_t entry : kids(n)) {
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
        compile_expr(t.b, src);
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

void compiler_impl::emit_slice_from(std::uint16_t dst, std::uint16_t source, std::size_t from) {
    proto().emit(instruction{op::new_array, dst});
    const std::uint32_t mark = reg_mark();
    const std::uint16_t length = alloc_reg();
    proto().emit(instruction{op::get_prop, length, source, name_operand("length")});
    const std::uint16_t index = alloc_reg();
    emit_const(index, value::number(static_cast<double>(from)));
    const std::uint16_t one = alloc_reg();
    emit_const(one, value::number(1));
    const std::uint16_t test = alloc_reg();
    const std::uint16_t item = alloc_reg();
    const std::size_t top = proto().code.size();
    proto().emit(instruction{op::less, test, index, length});
    const std::size_t exit = proto().emit(instruction{op::jump_if_false, test});
    proto().emit(instruction{op::get_index, item, source, index});
    proto().emit(instruction{op::append, dst, item});
    proto().emit(instruction{op::add, index, index, one});
    patch_jump(proto().emit(instruction{op::jump}), top);
    patch_here(exit);
    release_to(mark);
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
