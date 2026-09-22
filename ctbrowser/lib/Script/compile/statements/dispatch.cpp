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
        // An eval's completion value: this statement's, until a later one.
        if (tracking_completion()) {
            proto().emit(instruction{op::move, static_cast<std::uint16_t>(completion_reg_), r});
        }
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
        // ONLY AT THE SCRIPT'S OWN SCOPE, unless it is a `var`. A `let` or
        // `const` inside a block at the top level is BLOCK-scoped, and
        // sending it to set_global too gave a loop body ONE binding: every
        // closure `for (const id of ids) { const el = f(id); fns.push(() =>
        // el); }` made read the last iteration's `el` through get_global.
        // The local path below boxes it and makes a fresh cell per
        // iteration, exactly as it already did inside a function. `var` is
        // function-scoped, so at any depth it is still the script's.
        if (frames_.size() == 1 && !module_scope_ &&
            (n.text == "var" || fn().scope_marks.size() <= 1)) {
            // NOT `declaring_` around the loop: an initialiser is an
            // expression, and `'use strict'; var a = b = 1;` must still refuse
            // `b`. The pattern path below sets it for its own writes only.
            for (const std::int32_t d : kids(n)) {
                const vp::node & decl = at(d);
                // `var x;` INITIALISES NOTHING (14.3.2.1: a VariableDeclaration
                // without an Initializer evaluates to empty): the binding
                // exists from instantiation - program::hoisted_vars, bound by
                // context::run and run_nested before the first instruction -
                // so `x = 5; var x;` should keep 5. It still writes undefined
                // here, KNOWINGLY: tools/check/bootstrap-host-prefix.py's exact
                // wrapper proof reads the `var originalGet;` write as the
                // declaration that closes the global (2026-09-16, gate at
                // a0459d71: "call lacks one closed source invocation context");
                // when the prover takes program::hoisted_vars as the
                // declaration, `continue` here is the whole fix (two test262
                // files: for-in/for-of head-var-bound-names-in-stmt).
                const std::uint32_t mark = reg_mark();
                const std::uint16_t r = alloc_reg();
                if (decl.a >= 0) {
                    compile_named_expr(decl.a, r, decl.b >= 0 ? "" : decl.text);
                } else {
                    proto().emit(instruction{op::load_undef, r});
                }
                if (is_using_decl(n)) { emit_using_add(r, n.text == "await using"); }
                if (decl.b >= 0) { // a shape, not a name
                    compile_pattern_binding(decl.b, r, true);
                } else if (decl.text == "undefined" || decl.text == "NaN" ||
                           decl.text == "Infinity") {
                    // `var undefined = 5;` PutValue on a non-writable global:
                    // dropped (see emit_plain_write).
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
                declare_pattern_names(decl.b, n.text != "var");
                const std::uint32_t mark = reg_mark();
                const std::uint16_t r = alloc_reg();
                if (decl.a >= 0) {
                    compile_expr(decl.a, r);
                } else {
                    proto().emit(instruction{op::load_undef, r});
                }
                // A DECLARATION'S write, even where declare_pattern_names made
                // no local - a `for (const [x] = ...` at a script's top level
                // binds globals, which the strict assignment check must not
                // take for assignments (see declaring_).
                {
                    const bool outer_declaring = declaring_;
                    declaring_ = true;
                    compile_pattern_binding(decl.b, r, false);
                    declaring_ = outer_declaring;
                }
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
                    compile_named_expr(decl.a, tmp, decl.text);
                    if (is_using_decl(n)) { emit_using_add(tmp, n.text == "await using"); }
                    emit_write(decl.text, tmp);
                    release_to(mark);
                }
                continue;
            }
            const std::uint16_t r = declare_local(std::string{decl.text});
            // A lexical binding is in its dead zone until its declarator has
            // run: `let y = y + 1` reads it before that, and compile_ident
            // decides the ReferenceError from this offset (as
            // predeclare_locals records it for a function body's own).
            if (!function_scoped) { fn().locals.back().initialized_at = decl.end; }
            if (fn().locals.back().boxed && decl.a >= 0 && contains_closure(decl.a)) {
                // THE CELL FIRST when the initialiser makes a closure: `{ let
                // y = () => y; }` has the arrow capture `y` while it is being
                // initialised, and op::closure takes what is in the register
                // at that moment - boxing afterwards made a second cell the
                // arrow never saw, so `y()` read undefined (at a function's
                // top level the name is hoisted and boxed at entry, which is
                // this same order). The value goes in through the cell.
                proto().emit(instruction{op::load_undef, r});
                proto().emit(instruction{op::new_cell, r});
                const std::uint32_t mark = reg_mark();
                const std::uint16_t tmp = alloc_reg();
                compile_named_expr(decl.a, tmp, decl.text);
                if (is_using_decl(n)) { emit_using_add(tmp, n.text == "await using"); }
                proto().emit(instruction{op::cell_set, r, tmp});
                release_to(mark);
                continue;
            }
            if (decl.a >= 0) {
                compile_named_expr(decl.a, r, decl.text);
            } else {
                proto().emit(instruction{op::load_undef, r});
            }
            if (is_using_decl(n)) { emit_using_add(r, n.text == "await using"); }
            // A captured local is boxed AFTER its initializer runs, so the
            // cell starts out holding the right value.
            if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, r}); }
            // a declared local keeps its register beyond this statement
            if (fn().next_reg <= r) { fn().next_reg = static_cast<std::uint16_t>(r + 1); }
        }
        return; // locals must NOT be released by the mark below
    case vp::nk::block:
        push_scope();
        compile_statement_list(kids(n), false);
        pop_scope();
        break;
    // UpdateEmpty(_, undefined) constructs (14.6.2, 14.7.1.1, 14.12.4,
    // 14.15.3, 14.11.2): the completion is what their body produces, or
    // undefined - never what came before.
    case vp::nk::if_stmt:
        clear_completion();
        compile_if(n);
        break;
    case vp::nk::while_stmt:
        clear_completion();
        compile_while(n);
        break;
    case vp::nk::do_stmt:
        clear_completion();
        compile_do_while(n);
        break;
    case vp::nk::forof_stmt:
        clear_completion();
        compile_for_of(n);
        break;
    case vp::nk::class_decl: {
        const std::uint16_t r = alloc_reg();
        // A DECLARATION, so its name is a binding of this scope - which is
        // the whole difference from the expression form.
        compile_class(n, r, true);
        {
            const bool outer_declaring = declaring_;
            declaring_ = true;
            emit_write(std::string{n.text}, r);
            declaring_ = outer_declaring;
        }
        break;
    }
    case vp::nk::switch_stmt:
        clear_completion();
        compile_switch(n);
        break;
    case vp::nk::for_stmt:
        clear_completion();
        compile_for(n);
        break;
    case vp::nk::break_stmt: compile_break(n); break;
    case vp::nk::continue_stmt: compile_continue(n); break;
    case vp::nk::labeled: compile_labeled(n); break;
    case vp::nk::try_stmt:
        clear_completion();
        compile_try(n);
        break;
    case vp::nk::throw_stmt: compile_throw(n); break;
    case vp::nk::with_stmt:
        clear_completion();
        compile_with(n);
        break;
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
        // exactly what TypeScript's __awaiter helper does with it. An ASYNC
        // generator's `return v` awaits v first (15.7.2 AsyncGeneratorBody's
        // Return evaluation), so the record never carries a promise.
        if (fn().is_async && fn().is_generator && n.a >= 0) {
            proto().emit(instruction{op::await_value, r, r});
        }
        // AN OPEN `finally` GETS IT FIRST. Returning straight out of a try
        // block skipped the finally entirely - see compile_try_with_finally.
        if (!route_return_through_finally(r)) {
            if (!fn().derived_flag.empty()) {
                emit_derived_return(r);
                break;
            }
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

    compile_clause(n.b);
    if (n.c >= 0) {
        const std::size_t to_end = proto().emit(instruction{op::jump});
        patch_here(to_else);
        compile_clause(n.c);
        patch_here(to_end);
    } else {
        patch_here(to_else);
    }
}

// ANNEX B.3.4: `if (x) function f() {}` is `if (x) { function f() {} }` in
// sloppy code - the declaration binds in a block of its own and only B.3.3's
// var write is seen outside. Without the scope the binding WAS the enclosing
// one, so a parameter named `f` was overwritten by a declaration that must
// not touch it. Everywhere else an `if` clause declares nothing, so the extra
// scope costs a push and a pop.
void compiler_impl::compile_clause(std::int32_t stmt) {
    if (stmt < 0) { return; }
    if (at(stmt).kind != vp::nk::func_decl) {
        compile_stmt(stmt);
        return;
    }
    push_scope();
    compile_stmt(stmt);
    pop_scope();
}

void compiler_impl::compile_throw(const vp::node & n) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t r = alloc_reg();
    compile_expr(n.a, r);
    proto().emit(instruction{op::throw_value, r});
    release_to(mark);
}

} // namespace ctbrowser::script::detail
