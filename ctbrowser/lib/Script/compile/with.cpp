// compiler_impl - the `with` statement.
//
// 14.11: `with (o) body` evaluates `o`, makes an Object Environment Record
// of it, and runs the body with that record in front of the scope chain. The
// shape here is described at `with_scope` in compiler_impl.hpp: the object is
// a hidden boxed local, and every identifier the body resolves emits a
// HasProperty on it (with the @@unscopables veto) before its ordinary
// resolution. Nothing new in the VM: `has_property`, `get_prop`, `set_prop`
// and two conditional jumps are the whole of it, which is what keeps this a
// compiler change - and keeps the price on the pages that use `with`, which
// is next to none of them (strict code and modules cannot).

#include "compiler_impl.hpp"

namespace ctbrowser::script::detail {

void compiler_impl::compile_with(const vp::node & n) {
    // The object, in a scope of its own so the hidden local dies with the
    // statement. Evaluated first (step 1), then ToObject: null and undefined
    // are the TypeError, and a primitive is left as it is - `has_property`
    // answers false for one, so its properties are simply not seen.
    push_scope();
    const std::string name = "@with:" + std::to_string(with_scopes_.size() + 1) + ":" +
                             std::to_string(frames_.size() - 1) + ":" +
                             std::to_string(fn().locals.size());
    const std::uint16_t obj = declare_local(name);
    const std::size_t position = fn().locals.size() - 1;
    compile_expr(n.a, obj);
    const std::size_t is_object = proto().emit(instruction{op::jump_if_not_nullish, obj});
    emit_throw("TypeError", "Cannot convert undefined or null to object");
    patch_here(is_object);
    // Boxed whatever the capture index says: a closure made inside the body
    // captures the CELL, and function declarations hoisted to the body's
    // start are made before this value exists.
    fn().locals.back().boxed = true;
    proto().emit(instruction{op::new_cell, obj});

    with_scopes_.push_back(with_scope{name, frames_.size() - 1, position});
    compile_stmt(n.b);
    with_scopes_.pop_back();
    pop_scope();
}

std::pair<std::size_t, std::size_t> compiler_impl::declaring_frame(std::string_view name) {
    for (std::size_t level = frames_.size(); level-- > 0;) {
        frame & f = frames_[level];
        if (const local * l = find_local_entry(f, name)) {
            return {level, static_cast<std::size_t>(l - f.locals.data())};
        }
    }
    return {frames_.size(), 0};
}

std::vector<const compiler_impl::with_scope *> compiler_impl::applicable_with_scopes(
    std::string_view name) {
    std::vector<const with_scope *> applicable;
    if (with_scopes_.empty()) { return applicable; }
    const auto [declared_in, position] = declaring_frame(name);
    // The scopes that may bind the name, innermost first, up to the first
    // one the name is declared INSIDE - a local of a deeper frame, or one of
    // the same frame declared after the with's own hidden local.
    for (std::size_t i = with_scopes_.size(); i-- > 0;) {
        const with_scope & w = with_scopes_[i];
        const bool inside =
            declared_in < frames_.size() &&
            (declared_in > w.frame || (declared_in == w.frame && position >= w.locals_mark));
        if (inside) { break; }
        applicable.push_back(&w);
    }
    return applicable;
}

bool compiler_impl::emit_with_object(std::string_view name, std::uint16_t obj) {
    const std::vector<const with_scope *> applicable = applicable_with_scopes(name);
    if (applicable.empty()) { return false; }

    proto().emit(instruction{op::load_undef, obj});
    std::vector<std::size_t> to_end;
    for (const with_scope * w : applicable) {
        const std::uint32_t mark = reg_mark();
        const std::uint16_t candidate = alloc_reg();
        emit_plain_read(w->name, candidate);
        // HasProperty(o, name) - the whole chain, through a proxy's trap.
        const std::uint16_t key = alloc_reg();
        emit_string(key, std::string{name});
        const std::uint16_t has = alloc_reg();
        proto().emit(instruction{op::has_property, has, key, candidate});
        const std::size_t missing = proto().emit(instruction{op::jump_if_false, has});
        // ...unless o[@@unscopables] is an object whose `name` is truthy.
        // A non-object there has no properties, so `get_prop` on it reads
        // undefined and the veto does not fire - which is 9.1.1.2.1 step 4.
        const std::uint16_t unscopables = alloc_reg();
        proto().emit(
            instruction{op::get_prop, unscopables, candidate, name_operand("@@unscopables")});
        const std::size_t no_veto = proto().emit(instruction{op::jump_if_false, unscopables});
        const std::uint16_t blocked = alloc_reg();
        proto().emit(
            instruction{op::get_prop, blocked, unscopables, name_operand(std::string{name})});
        const std::size_t vetoed = proto().emit(instruction{op::jump_if_true, blocked});
        patch_here(no_veto);
        proto().emit(instruction{op::move, obj, candidate});
        to_end.push_back(proto().emit(instruction{op::jump}));
        patch_here(missing);
        patch_here(vetoed);
        release_to(mark);
    }
    for (const std::size_t j : to_end) { patch_here(j); }
    return true;
}

void compiler_impl::emit_plain_read(std::string_view name, std::uint16_t dst) {
    if (const local * l = find_local_entry(fn(), name)) {
        proto().emit(instruction{l->boxed ? op::cell_get : op::move, dst, l->reg});
        return;
    }
    const int up = resolve_upvalue(frames_.size() - 1, name);
    if (up >= 0) {
        proto().emit(instruction{op::get_upvalue, dst, static_cast<std::uint16_t>(up)});
        return;
    }
    // `arguments` is not synthesised here: compile_function_body made it a
    // real local at entry, so it resolved above as a local or an upvalue.
    // Reaching this point means the mention is at TOP LEVEL, where a script
    // has no arguments and reading the name is an ordinary global lookup -
    // which is what a browser does too.
    proto().emit(instruction::with_bx(op::get_global, dst, name_operand(std::string{name})));
}

void compiler_impl::emit_plain_write(std::string_view name, std::uint16_t src) {
    if (const local * l = find_local_entry(fn(), name)) {
        if (l->boxed) {
            proto().emit(instruction{op::cell_set, l->reg, src});
        } else {
            proto().emit(instruction{op::move, l->reg, src});
        }
        return;
    }
    const int up = resolve_upvalue(frames_.size() - 1, name);
    if (up >= 0) {
        proto().emit(instruction{op::set_upvalue, static_cast<std::uint16_t>(up), src});
        return;
    }
    emit_strict_assign_check(name);
    proto().emit(instruction::with_bx(op::set_global, src, intern_name(std::string{name})));
}

// 6.2.5.6 PutValue step 3.a: in strict code, assigning to a name that resolves
// nowhere is a ReferenceError rather than a new global. Emitted only for an
// assignment - a declaration's first write sets declaring_ - and only in
// strict code, as a call of strict_assign_check_name (op::set_global itself
// may not throw). The write that follows still runs when the check passes.
void compiler_impl::emit_strict_assign_check(std::string_view name) {
    if (!fn().is_strict || declaring_) { return; }
    // NOT AT A MODULE'S TOP LEVEL, deliberately (docs/script.md, strict
    // mode): ctcompile's module fixtures publish to their host through
    // `OUT = ...` and rely on the write.
    if (module_scope_ && frames_.size() == 1) { return; }
    const std::uint32_t mark = reg_mark();
    const std::uint16_t callee = alloc_reg();
    proto().emit(instruction::with_bx(op::get_global, callee,
                                      intern_name(std::string{strict_assign_check_name})));
    const std::uint16_t arg = alloc_reg();
    emit_string(arg, std::string{name});
    proto().emit(instruction{op::call, callee, 1});
    release_to(mark);
}

} // namespace ctbrowser::script::detail
