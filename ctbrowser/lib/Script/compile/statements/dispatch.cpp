// compiler_impl - statements: `compile_stmt`, the dispatch at the centre of the
// statement compiler, and the two forms small enough to live beside it.
//
// One of four files carved out of a 1,017-line compile/statements.cpp on
// 2026-09-08 - which was itself one of the files carved out of a 3,845-line
// compile.cpp on 2026-08-09. All are members of compiler_impl, declared whole
// in compiler_impl.hpp beside the directory; nothing about that header changed.

#include "../compiler_impl.hpp"

namespace ctbrowser::script::detail {

void compiler_impl::compile_stmt(std::int32_t idx) {
    if (idx < 0 || !out_.ok) { return; }
    // ONE OF THE TWO PLACES THE EMIT CURSOR MOVES. See compiler_impl::at_source.
    const at_source here{*this, idx};
    const vp::node & n = at(idx);
    const std::uint32_t mark = reg_mark();
    switch (n.kind) {
    // REFUSED BY NAME, for the same reason the expression forms are: a
    // page whose `import` silently did nothing would run with half its
    // bindings undefined and fail somewhere else entirely. The syntax
    // parses (ctjs 2026-08-02); the semantics are staged in
    // docs/plans/modules.md.
    case vp::nk::import_decl: {
        if (!module_scope_) {
            fail("`import` is only allowed in a module - a classic <script> cannot use it");
            break;
        }
        // AND NOTHING ELSE HAPPENS HERE. The binding was made at module
        // entry - see bind_imports - because a function declared above the
        // `import` still closes over what it imports.
        break;
    }
    case vp::nk::export_decl: {
        if (!module_scope_) {
            fail("`export` is only allowed in a module - a classic <script> cannot use it");
            break;
        }
        // A RE-EXPORT BINDS NOTHING HERE. It was recorded at module entry
        // as an edge for the loader to resolve - see collect_reexports -
        // and there is no local name for it to compile into.
        if (!n.text.empty()) { break; }
        if (n.c == 1) {
            // `export default <expr>`: the binding was hoisted and bound at
            // entry under `*default*`, so this only WRITES it. Making a
            // fresh cell here instead would leave the importer holding the
            // one from entry - which is exactly the bug a cycle exposes.
            const int slot = find_local(default_binding);
            if (slot < 0) {
                fail("`export default` was not hoisted - see predeclare_locals");
                break;
            }
            const std::uint32_t mark = reg_mark();
            const std::uint16_t r = alloc_reg();
            compile_expr(n.a, r);
            proto().emit(instruction{op::cell_set, static_cast<std::uint16_t>(slot), r});
            release_to(mark);
            break;
        }
        if (n.a >= 0) {
            // `export const x = 1`, `export function f() {}`. The
            // declaration compiles as itself, then the names it binds are
            // published.
            //
            // BY NAME, NOT BY DIFFING fn().locals. A module's top-level
            // declarations are PRE-DECLARED at entry - that is how a
            // function declared above its own `let` still closes over it -
            // so the locals list does not grow here and a diff publishes
            // nothing at all. It read `undefined` on the other side and
            // said nothing, which is the failure this whole ladder exists
            // to make loud.
            if (at(n.a).kind == vp::nk::var_decl) {
                for (const std::int32_t d : kids(at(n.a))) {
                    if (at(d).text.empty()) {
                        fail("`export` of a destructuring declaration is not implemented yet - "
                             "ES modules are staged in docs/plans/modules.md");
                        break;
                    }
                }
            }
            // AND THAT IS ALL: the declaration compiles as itself. Its
            // names were bound to their export cells at module entry, so
            // there is nothing left to publish here - see bind_export.
            compile_stmt(n.a);
            break;
        }
        // `export { a, b as c }` emits NOTHING. The binding pass at entry
        // already tied each name to its cell, and it is the pass that
        // reports a name this module does not declare.
        break;
    }
    case vp::nk::expr_stmt: {
        const std::uint16_t r = alloc_reg();
        compile_expr(n.a, r);
        break;
    }
    case vp::nk::var_decl:
        // TOP-LEVEL declarations become globals, not frame-0 registers.
        // Script scope is what functions declared alongside them close
        // over, and `let n = 0; function inc() { n = n + 1; }` is the most
        // common shape in JavaScript there is. Making them registers would
        // turn every one of those into the enclosing-local refusal below,
        // which would be correct and useless.
        // A MODULE'S top-level declarations are its OWN, so they take the
        // local path below exactly as a function body's would. A classic
        // script's become globals, which is what lets two <script> tags see
        // each other.
        if (frames_.size() == 1 && !module_scope_) {
            for (const std::int32_t d : kids(n)) {
                const vp::node & decl = at(d);
                const std::uint32_t mark = reg_mark();
                const std::uint16_t r = alloc_reg();
                if (decl.a >= 0) {
                    compile_expr(decl.a, r);
                } else {
                    proto().emit(instruction{op::load_undef, r});
                }
                if (decl.b >= 0) { // a shape, not a name
                    compile_pattern_binding(decl.b, r, true);
                } else {
                    const std::uint16_t name = name_operand(std::string{decl.text});
                    proto().emit(instruction::with_bx(op::set_global, r, name));
                }
                release_to(mark);
            }
            return;
        }
        for (const std::int32_t d : kids(n)) {
            const vp::node & decl = at(d);
            if (decl.b >= 0) { // a shape, not a name
                // THE NAMES FIRST, ABOVE THE MARK. release_to(mark) below
                // frees the temporary holding the initializer - and would
                // free the pattern's own locals with it if they were
                // allocated inside, leaving the next temporary to overwrite
                // one. See declare_pattern_names.
                declare_pattern_names(decl.b);
                const std::uint32_t mark = reg_mark();
                const std::uint16_t r = alloc_reg();
                if (decl.a >= 0) {
                    compile_expr(decl.a, r);
                } else {
                    proto().emit(instruction{op::load_undef, r});
                }
                compile_pattern_binding(decl.b, r, false);
                release_to(mark);
                continue;
            }
            // WHICH KEYWORD IT IS DECIDES THIS, and until now nothing
            // asked. `var` is FUNCTION-scoped: it always writes the hoisted
            // binding, wherever the statement sits, because there is only
            // one of it per function. `let` and `const` are BLOCK-scoped
            // and must shadow, which is why the hoisted slot is reused for
            // them only when it is in THIS scope - asking was_predeclared
            // alone made a `const` inside a block assign through to a
            // binding at the top of the function.
            //
            // Getting this wrong the other way is what `if (c) { var x = 1; }`
            // did: the block declared its own `x`, the scope popped it, and
            // every later read found undefined.
            const bool function_scoped = n.text == "var";
            if (was_predeclared(decl.text) &&
                (function_scoped || find_local_in_current_scope(decl.text) != nullptr)) {
                // hoisted above: this statement is only the initializer,
                // and emit_write knows whether it goes through a cell
                if (decl.a >= 0) {
                    const std::uint32_t mark = reg_mark();
                    const std::uint16_t tmp = alloc_reg();
                    compile_expr(decl.a, tmp);
                    emit_write(decl.text, tmp);
                    release_to(mark);
                }
                continue;
            }
            const std::uint16_t r = declare_local(std::string{decl.text});
            if (decl.a >= 0) {
                compile_expr(decl.a, r);
            } else {
                proto().emit(instruction{op::load_undef, r});
            }
            // A captured local is boxed AFTER its initializer runs, so the
            // cell starts out holding the right value.
            if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, r}); }
            // a declared local keeps its register beyond this statement
            if (fn().next_reg <= r) { fn().next_reg = static_cast<std::uint16_t>(r + 1); }
        }
        return; // locals must NOT be released by the mark below
    case vp::nk::block:
        push_scope();
        for (const std::int32_t s : kids(n)) { compile_stmt(s); }
        pop_scope();
        break;
    case vp::nk::if_stmt: compile_if(n); break;
    case vp::nk::while_stmt: compile_while(n); break;
    case vp::nk::do_stmt: compile_do_while(n); break;
    case vp::nk::forof_stmt: compile_for_of(n); break;
    case vp::nk::class_decl: {
        const std::uint16_t r = alloc_reg();
        // A DECLARATION, so its name is a binding of this scope - which is
        // the whole difference from the expression form.
        compile_class(n, r, true);
        emit_write(std::string{n.text}, r);
        break;
    }
    case vp::nk::switch_stmt: compile_switch(n); break;
    case vp::nk::for_stmt: compile_for(n); break;
    case vp::nk::break_stmt: compile_break(n); break;
    case vp::nk::continue_stmt: compile_continue(n); break;
    case vp::nk::labeled: compile_labeled(n); break;
    case vp::nk::try_stmt: compile_try(n); break;
    case vp::nk::throw_stmt: compile_throw(n); break;
    case vp::nk::return_stmt: {
        const std::uint16_t r = alloc_reg();
        if (n.a >= 0) {
            compile_expr(n.a, r);
        } else {
            proto().emit(instruction{op::load_undef, r});
        }
        // `async function f() { return 5 }` hands back a PROMISE of 5, not
        // 5 - so `f().then(...)` works and not only `await f()`.
        // NOT for a generator, even an async one. A generator's `return`
        // becomes the `value` of a `{value, done: true}` record; the
        // promise, if there is to be one, is the driver's job - which is
        // exactly what TypeScript's __awaiter helper does with it.
        // AN OPEN `finally` GETS IT FIRST. Returning straight out of a try
        // block skipped the finally entirely - see compile_try_with_finally.
        if (!route_return_through_finally(r)) {
            if (fn().is_async && !fn().is_generator) {
                proto().emit(instruction{op::wrap_promise, r});
            }
            proto().emit(instruction{op::ret, r});
        }
        break;
    }
    case vp::nk::func_decl: compile_function_decl(idx); return;
    case vp::nk::empty: break;
    default: {
        // anything not yet handled is still an expression in most cases
        const std::uint16_t r = alloc_reg();
        compile_expr(idx, r);
        break;
    }
    }
    release_to(mark);
}

void compiler_impl::compile_if(const vp::node & n) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t cond = alloc_reg();
    compile_expr(n.a, cond);
    const std::size_t to_else = proto().emit(instruction{op::jump_if_false, cond});
    release_to(mark);

    compile_stmt(n.b);
    if (n.c >= 0) {
        const std::size_t to_end = proto().emit(instruction{op::jump});
        patch_here(to_else);
        compile_stmt(n.c);
        patch_here(to_end);
    } else {
        patch_here(to_else);
    }
}

void compiler_impl::compile_throw(const vp::node & n) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t r = alloc_reg();
    compile_expr(n.a, r);
    proto().emit(instruction{op::throw_value, r});
    release_to(mark);
}

} // namespace ctbrowser::script::detail
