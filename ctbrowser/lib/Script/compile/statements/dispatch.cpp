//===- dispatch.cpp - Statement dispatch ----------------------------------===//
//
// compiler_impl statements: compile_stmt, the dispatch at the centre of the
// statement compiler, plus compile_if, compile_clause and compile_throw. The
// other statement families live beside this file; all members are declared
// in ../compiler_impl.hpp. Section numbers are ECMA-262's.
//
//===----------------------------------------------------------------------===//

#include "../compiler_impl.hpp"

namespace ctbrowser::script::detail {

void compiler_impl::compile_stmt(std::int32_t idx) {
    if (idx < 0 || !out_.ok) { return; }
    // One of the two places the emit cursor's source position moves (the
    // other is compile_expr). See compiler_impl::at_source.
    const at_source here{*this, idx};
    const vp::node & n = at(idx);
    const std::uint32_t mark = reg_mark();
    switch (n.kind) {
    // `import` and `export` are only valid in a module; a classic script that
    // uses them fails loudly. In a module their bindings are made at module
    // entry (bind_imports, bind_export, collect_reexports), so little is
    // emitted here.
    case vp::nk::import_decl: {
        if (!module_scope_) {
            fail("`import` is only allowed in a module - a classic <script> cannot use it");
            break;
        }
        // Nothing else happens here. The binding was made at module entry
        // (see bind_imports) because a function declared above the
        // `import` still closes over what it imports.
        break;
    }
    case vp::nk::export_decl: {
        if (!module_scope_) {
            fail("`export` is only allowed in a module - a classic <script> cannot use it");
            break;
        }
        // A re-export binds nothing here. It was recorded at module entry
        // as an edge for the loader to resolve (see collect_reexports), and
        // there is no local name for it to compile into.
        if (!n.text.empty()) { break; }
        if (n.c == 1) {
            // `export default <expr>`: the binding was hoisted and bound at
            // entry under `*default*`, so this only writes it. Making a
            // fresh cell here instead would leave the importer holding the
            // one from entry, which an import cycle would expose.
            const int slot = find_local(default_binding);
            if (slot < 0) {
                fail("`export default` was not hoisted - see predeclare_locals");
                break;
            }
            const std::uint32_t mark = reg_mark();
            const std::uint16_t r = alloc_reg();
            // `export default function f() {}` declares f (16.2.3.7): inside
            // it, `f` is that module binding, which `f = 2` writes, not the
            // own name a function expression binds.
            if (const std::int32_t named = default_export_name(idx);
                named >= 0 && at(named).kind == vp::nk::func_expr) {
                default_function_ = named;
            }
            // An anonymous one is named "default" (16.2.3.7 NamedEvaluation;
            // the same for `export default class {}` and a parenthesised
            // function). The name is the function's metadata, so this
            // compiles to exactly what compile_expr would.
            compile_named_expr(n.a, r, "default");
            proto().emit(instruction{op::cell_set, static_cast<std::uint16_t>(slot), r});
            // A named default also initialises the binding its name makes
            // (predeclare_locals).
            if (const std::int32_t named = default_export_name(idx); named >= 0) {
                emit_write(at(named).text, r);
            }
            release_to(mark);
            break;
        }
        if (n.a >= 0) {
            // `export const x = 1`, `export function f() {}`: the
            // declaration compiles as itself. Its names were bound to their
            // export cells at module entry, so there is nothing left to
            // publish here; see bind_export.
            compile_stmt(n.a);
            break;
        }
        // `export { a, b as c }` emits nothing. The binding pass at entry
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
        // A classic script's top-level declarations become globals, not
        // frame-0 registers, so functions declared beside them and other
        // <script> tags can see them. A module's top-level declarations are
        // its own, so they take the local path below exactly as a function
        // body's would.
        // Only at the script's own scope, unless it is a `var`. A `let` or
        // `const` inside a block at the top level is block-scoped: as a
        // global, a loop body would have one binding, and every closure in
        // `for (const id of ids) { const el = f(id); fns.push(() => el); }`
        // would read the last iteration's `el`. The local path below boxes
        // it and makes a fresh cell per iteration. `var` is function-scoped,
        // so at any depth it is still the script's.
        // An eval's lexical declarations are its own, not globals; see
        // compile_program's predeclare_locals.
        if (frames_.size() == 1 && !top_level_is_scope_ &&
            (n.text == "var" || (fn().scope_marks.size() <= 1 && !completion_value_))) {
            // Not `declaring_` around the loop: an initialiser is an
            // expression, and `'use strict'; var a = b = 1;` must still refuse
            // `b`. The pattern path below sets it for its own writes only.
            for (const std::int32_t d : kids(n)) {
                const vp::node & decl = at(d);
                // Per 14.3.2.1 a `var x;` with no initialiser should write
                // nothing: the binding already exists from instantiation
                // (program::hoisted_vars, bound by context::run and run_nested
                // before the first instruction), so `x = 5; var x;` should
                // keep 5. This still writes undefined because
                // tools/check/bootstrap-host-prefix.py treats this write as
                // the declaration of the global. Once that checker reads
                // program::hoisted_vars instead, skip the write here
                // (`continue`); that fixes test262's for-in/for-of
                // head-var-bound-names-in-stmt.
                const std::uint32_t mark = reg_mark();
                const std::uint16_t r = alloc_reg();
                if (decl.a >= 0) {
                    compile_named_expr(decl.a, r, decl.b >= 0 ? "" : decl.text);
                } else {
                    proto().emit(instruction{op::load_undef, r});
                }
                if (is_using_decl(n)) { emit_using_add(r, n.text == "await using"); }
                if (decl.b >= 0) { // A pattern, not a name.
                    compile_pattern_binding(decl.b, r, true);
                } else if (decl.text == "undefined" || decl.text == "NaN" ||
                           decl.text == "Infinity") {
                    // `var undefined = 5;` PutValue on a non-writable global:
                    // dropped (see emit_plain_write).
                } else if (const int up = resolve_upvalue(0, decl.text);
                           up >= 0 && n.text == "var") {
                    // A direct eval's `var` of a name its caller already binds
                    // is that binding (19.2.1.3: the variable environment is
                    // the caller's): the initialiser writes the caller's cell,
                    // and `var x;` alone leaves it as it is.
                    if (decl.a >= 0) {
                        proto().emit(
                            instruction{op::set_upvalue, static_cast<std::uint16_t>(up), r});
                    }
                } else if (!with_scopes_.empty() || find_local_entry(fn(), decl.text) != nullptr) {
                    // Inside a `with` the initialiser is a PutValue on the name
                    // as the with resolves it (14.3.2.1 step 1: ResolveBinding
                    // from the running environment), the object's property
                    // when it has one, and `var x;` alone writes nothing. So
                    // too inside a `catch (x)`, whose parameter a `var x` may share
                    // (B.3.4): the initialiser writes the parameter.
                    if (decl.a >= 0) {
                        const bool outer_declaring = declaring_;
                        declaring_ = true;
                        emit_write(decl.text, r);
                        declaring_ = outer_declaring;
                    }
                } else {
                    const std::uint16_t name = name_operand(std::string{decl.text});
                    proto().emit(instruction::with_bx(
                        n.text == "var" ? op::set_global : op::init_global, r, name));
                }
                release_to(mark);
            }
            return;
        }
        for (const std::int32_t d : kids(n)) {
            const vp::node & decl = at(d);
            if (decl.b >= 0) { // A pattern, not a name.
                // Declare the names first, above the mark. release_to(mark)
                // below frees the temporary holding the initializer, and would
                // free the pattern's own locals with it if they were
                // allocated inside, leaving the next temporary to overwrite
                // one. See declare_pattern_names.
                declare_pattern_names(decl.b, n.text != "var", declared_immutability(n),
                                      n.text != "var");
                const std::uint32_t mark = reg_mark();
                const std::uint16_t r = alloc_reg();
                if (decl.a >= 0) {
                    compile_expr(decl.a, r);
                } else {
                    proto().emit(instruction{op::load_undef, r});
                }
                // A declaration's write, even where declare_pattern_names made
                // no local: a `for (const [x] = ...` at a script's top level
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
            // `var` is function-scoped: it always writes the hoisted binding,
            // wherever the statement sits, because there is only one per
            // function. `let` and `const` are block-scoped and must shadow,
            // so they reuse the hoisted slot only when it belongs to this
            // scope; was_predeclared alone would make a `const` inside a
            // block assign through to a binding at the top of the function.
            const bool function_scoped = n.text == "var";
            const local * here = find_local_in_current_scope(decl.text);
            if ((was_predeclared(decl.text) && (function_scoped || here != nullptr)) ||
                (!function_scoped && here != nullptr && here->initialized_at == decl.end)) {
                // Hoisted above (a body's by predeclare_locals, a block's by
                // hoist_captured_lexicals, which marks the local with this
                // declarator's end), so this statement is only the
                // initializer. emit_write knows whether it goes through a
                // cell; declaring_ marks it as the declaration's own write,
                // which a `const` accepts.
                if (decl.a >= 0) {
                    const std::uint32_t mark = reg_mark();
                    const std::uint16_t tmp = alloc_reg();
                    compile_named_expr(decl.a, tmp, decl.text);
                    if (is_using_decl(n)) { emit_using_add(tmp, n.text == "await using"); }
                    const bool outer_declaring = declaring_;
                    declaring_ = true;
                    emit_write(decl.text, tmp);
                    declaring_ = outer_declaring;
                    release_to(mark);
                }
                initialize_lexical(decl.text);
                continue;
            }
            const std::uint16_t r = declare_local(std::string{decl.text});
            // By position from here: the initialiser may declare locals of its
            // own (a class's computed field names, field_key_name), after
            // which locals.back() is one of those.
            const std::size_t declared = fn().locals.size() - 1;
            // A lexical binding is in its dead zone until its declarator has
            // run: `let y = y + 1` reads it before that, and compile_ident
            // decides the ReferenceError from this offset (as
            // predeclare_locals records it for a function body's own).
            if (!function_scoped) { fn().locals.back().initialized_at = decl.end; }
            fn().locals.back().readonly = declared_immutability(n);
            fn().locals.back().lexical = !function_scoped;
            if (fn().locals.back().boxed && decl.a >= 0 && contains_closure(decl.a)) {
                // Make the cell first when the initialiser makes a closure:
                // `{ let y = () => y; }` has the arrow capture `y` while it is
                // being initialised, and op::closure takes what is in the
                // register at that moment, so boxing afterwards would make a
                // second cell the arrow never sees. (At a function's top level
                // the name is hoisted and boxed at entry, the same order.) The
                // value goes in through the cell.
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
            // A captured local is boxed after its initializer runs, so the
            // cell starts out holding the right value.
            if (fn().locals[declared].boxed) { proto().emit(instruction{op::new_cell, r}); }
            // A declared local keeps its register beyond this statement.
            if (fn().next_reg <= r) { fn().next_reg = static_cast<std::uint16_t>(r + 1); }
        }
        return; // Locals must not be released by the mark below.
    case vp::nk::block:
        push_scope();
        hoist_captured_lexicals(n);
        compile_statement_list(kids(n), false);
        pop_scope();
        break;
    // UpdateEmpty(_, undefined) constructs (14.6.2, 14.7.1.1, 14.12.4,
    // 14.15.3, 14.11.2): the completion is what their body produces, or
    // undefined, never what came before.
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
        // A declaration, so its name is a binding of this scope, which is
        // the whole difference from the expression form.
        compile_class(n, r, true);
        {
            const bool outer_declaring = declaring_;
            declaring_ = true;
            emit_write(std::string{n.text}, r);
            declaring_ = outer_declaring;
            initialize_lexical(n.text);
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
        // `async function f() { return 5 }` hands back a promise of 5, not
        // 5, so `f().then(...)` works and not only `await f()`.
        // Not for a generator, even an async one. A generator's `return`
        // becomes the `value` of a `{value, done: true}` record; the
        // promise, if there is to be one, is the driver's job, which is
        // exactly what TypeScript's __awaiter helper does with it. An async
        // generator's `return v` awaits v first (15.7.2 AsyncGeneratorBody's
        // Return evaluation), so the record never carries a promise.
        if (fn().is_async && fn().is_generator && n.a >= 0) {
            proto().emit(instruction{op::await_value, r, r});
        }
        // An enclosing `finally` runs first; returning straight out of the
        // try block would skip it. See compile_try_with_finally.
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
        // Anything not handled above is compiled as an expression.
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

/// Annex B.3.4: `if (x) function f() {}` is `if (x) { function f() {} }` in
/// sloppy code. The declaration binds in a block of its own and only B.3.3's
/// var write is seen outside, so a parameter named `f` is not overwritten.
/// Any other `if` clause is compiled without an extra scope.
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
    // Attribute the throw to where its operand was written. A `throw` node
    // carries no lexeme of its own (offset_of), so the instruction would take
    // the enclosing position (a function's or script's first byte), and that
    // is the line an uncaught throw is reported at
    // (context::last_throw_site). Positions
    // only: this moves an entry of code_offsets, not an instruction.
    const at_source here{*this, n.a};
    emit_rethrow(r, false);
    release_to(mark);
}

} // namespace ctbrowser::script::detail
