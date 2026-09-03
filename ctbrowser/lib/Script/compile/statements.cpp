// compiler_impl - statements.
//
// Every statement form, and the function body that holds them.
// `compile_stmt` is the 227-line dispatch at the centre of it.
//
// One of the files carved out of a 3,845-line compile.cpp on 2026-08-09.
// The class is declared whole in compiler_impl.hpp beside this.

#include "compiler_impl.hpp"

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

void compiler_impl::patch_breaks(loop_context & loop) {
    for (const std::size_t site : loop.breaks) { patch_here(site); }
}

void compiler_impl::patch_continues(loop_context & loop, std::size_t target) {
    for (const std::size_t site : loop.continues) { patch_jump(site, target); }
}

compiler_impl::loop_context * compiler_impl::loop_for(std::string_view label) {
    if (loops_.empty()) { return nullptr; }
    if (label.empty()) { return &loops_.back(); }
    for (std::size_t i = loops_.size(); i-- > 0;) {
        if (loops_[i].label == label) { return &loops_[i]; }
    }
    return nullptr;
}

void compiler_impl::compile_break(const vp::node & n) {
    loop_context * loop = loop_for(n.text);
    if (loop == nullptr) {
        fail(n.text.empty() ? "break outside a loop" : "break to unknown label");
        return;
    }
    // AN OPEN `finally` GETS IT FIRST, because breaking out of a try block is
    // one of the ways out that has to run it - and this used to jump straight
    // past. The finally's dispatch re-emits the break, so the handler popping
    // below happens there, at the right depth.
    if (route_exit_through_finally(static_cast<std::size_t>(loop - loops_.data()), false)) {
        return;
    }
    // Leaving a try block by jumping out of it has to drop its handler, or
    // the catch stays reachable after the loop is gone.
    for (std::size_t i = handler_depth_; i > loop->handler_depth; --i) {
        proto().emit(instruction{op::pop_handler});
    }
    loop->breaks.push_back(proto().emit(instruction{op::jump}));
}

void compiler_impl::compile_continue(const vp::node & n) {
    loop_context * loop = loop_for(n.text);
    if (loop == nullptr) {
        fail(n.text.empty() ? "continue outside a loop" : "continue to unknown label");
        return;
    }
    if (route_exit_through_finally(static_cast<std::size_t>(loop - loops_.data()), true)) {
        return;
    }
    for (std::size_t i = handler_depth_; i > loop->handler_depth; --i) {
        proto().emit(instruction{op::pop_handler});
    }
    loop->continues.push_back(proto().emit(instruction{op::jump}));
}

void compiler_impl::compile_labeled(const vp::node & n) {
    // A label on a LOOP or a switch is picked up by that statement, which
    // owns the break and continue targets. A label on anything else - most
    // often a bare block - has no loop to hand it to, and `break lbl` out
    // of one is legal JavaScript that used to be refused outright.
    //
    // The mechanism is already there: a loop_context is a label plus a list
    // of jumps to patch. This pushes one with nothing to continue TO, so
    // `continue lbl` still fails (correctly - there is no iteration), and
    // `break lbl` lands after the block.
    const vp::nk labelled = n.a >= 0 ? at(n.a).kind : vp::nk::empty;
    const bool owns_its_label = labelled == vp::nk::while_stmt || labelled == vp::nk::do_stmt ||
                                labelled == vp::nk::for_stmt || labelled == vp::nk::forof_stmt ||
                                labelled == vp::nk::switch_stmt || labelled == vp::nk::labeled;
    if (owns_its_label) {
        pending_label_ = std::string{n.text};
        compile_stmt(n.a);
        pending_label_.clear();
        return;
    }
    loops_.push_back(loop_context{std::string{n.text}, {}, {}, handler_depth_});
    compile_stmt(n.a);
    patch_breaks(loops_.back());
    if (!loops_.back().continues.empty()) {
        fail("`continue " + std::string{n.text} + "` names a block, not a loop");
    }
    loops_.pop_back();
}

void compiler_impl::compile_while(const vp::node & n) {
    const std::size_t top = proto().code.size();
    loops_.push_back(loop_context{take_label(), {}, {}, handler_depth_});
    const std::uint32_t mark = reg_mark();
    const std::uint16_t cond = alloc_reg();
    compile_expr(n.a, cond);
    const std::size_t exit = proto().emit(instruction{op::jump_if_false, cond});
    release_to(mark);
    compile_stmt(n.b);
    patch_continues(loops_.back(), top); // continue re-tests the condition
    patch_jump(proto().emit(instruction{op::jump}), top);
    patch_here(exit);
    patch_breaks(loops_.back());
    loops_.pop_back();
}

void compiler_impl::compile_do_while(const vp::node & n) {
    const std::size_t top = proto().code.size();
    loops_.push_back(loop_context{take_label(), {}, {}, handler_depth_});
    compile_stmt(n.a);
    const std::size_t test = proto().code.size();
    patch_continues(loops_.back(), test);
    const std::uint32_t mark = reg_mark();
    const std::uint16_t cond = alloc_reg();
    compile_expr(n.b, cond);
    const std::size_t exit = proto().emit(instruction{op::jump_if_false, cond});
    release_to(mark);
    patch_jump(proto().emit(instruction{op::jump}), top);
    patch_here(exit);
    patch_breaks(loops_.back());
    loops_.pop_back();
}

void compiler_impl::compile_for(const vp::node & n) {
    push_scope();
    const std::string label = take_label();
    const std::size_t init_mark = fn().scope_marks.back();
    if (n.a >= 0) { compile_stmt(n.a); }
    // THE PER-ITERATION BINDINGS. `for (let i = 0; ...)` gives every
    // iteration its OWN `i`, so closures made in the body capture 0, 1, 2 -
    // where `for (var i = ...)` shares one binding and they all capture 3.
    // This engine had only the `var` behaviour, silently, and modern
    // minified output leans on the difference constantly.
    //
    // Only a BOXED local can tell: an unboxed one lives in a register that
    // nothing outside the frame can reach, so copying it would be work with
    // no observer. `var` is hoisted to the function scope and so never
    // appears among the locals this scope opened - which is what keeps the
    // two loops apart without the compiler having to track declaration
    // kinds. unittests/js/obfuscated.cpp pins BOTH shapes, so getting that
    // backwards fails immediately.
    std::vector<std::uint16_t> per_iteration;
    for (std::size_t k = init_mark; k < fn().locals.size(); ++k) {
        if (fn().locals[k].boxed) { per_iteration.push_back(fn().locals[k].reg); }
    }
    const std::size_t top = proto().code.size();
    loops_.push_back(loop_context{label, {}, {}, handler_depth_});
    std::size_t exit = 0;
    bool has_cond = false;
    if (n.b >= 0) {
        const std::uint32_t mark = reg_mark();
        const std::uint16_t cond = alloc_reg();
        compile_expr(n.b, cond);
        exit = proto().emit(instruction{op::jump_if_false, cond});
        has_cond = true;
        release_to(mark);
    }
    compile_stmt(n.d);
    // `continue` in a for-loop runs the UPDATE and then re-tests - it does
    // not skip back to the condition. Getting this wrong turns every
    // `for (...; i++) { ... continue; }` into an infinite loop.
    patch_continues(loops_.back(), proto().code.size());
    // BETWEEN THE BODY AND THE UPDATE, which is where the specification puts
    // it (ForBodyEvaluation step 3.e): the fresh binding takes the value the
    // body left, and the increment then applies to the NEW one. Doing it
    // after the update instead would shift every captured value by one.
    //
    // `continue` lands on the instruction above, so it flows through here
    // too - which is correct, and is why this sits after patch_continues
    // rather than before it.
    if (!per_iteration.empty()) {
        const std::uint32_t mark = reg_mark();
        const std::uint16_t scratch = alloc_reg();
        for (const std::uint16_t r : per_iteration) {
            proto().emit(instruction{op::cell_get, scratch, r});
            proto().emit(instruction{op::move, r, scratch});
            proto().emit(instruction{op::new_cell, r});
        }
        release_to(mark);
    }
    if (n.c >= 0) {
        const std::uint32_t mark = reg_mark();
        const std::uint16_t tmp = alloc_reg();
        compile_expr(n.c, tmp);
        release_to(mark);
    }
    patch_jump(proto().emit(instruction{op::jump}), top);
    if (has_cond) { patch_here(exit); }
    patch_breaks(loops_.back());
    loops_.pop_back();
    pop_scope();
}

void compiler_impl::compile_for_of(const vp::node & n) {
    push_scope();
    const std::string label = take_label();
    const std::uint32_t mark = reg_mark();

    const std::uint16_t source = alloc_reg();
    compile_expr(n.b, source);
    if (n.text == "in") {
        proto().emit(instruction{op::own_keys, source, source});
    } else {
        // `for (x of ...)` TAKES ANYTHING ITERABLE. This is an index loop over
        // `length`, so a Map or a Set - which has neither - ran zero times and
        // reported nothing.
        proto().emit(instruction{op::iterable, source, source});
    }

    const std::uint16_t length = alloc_reg();
    const std::uint16_t length_name = name_operand("length");
    proto().emit(instruction{op::get_prop, length, source, length_name});
    const std::uint16_t index = alloc_reg();
    emit_const(index, value::number(0));
    const std::uint16_t one = alloc_reg();
    emit_const(one, value::number(1));

    // The loop variable is a real local, so a closure made inside the body
    // captures THIS iteration's value - which is the whole reason `let` in
    // a loop behaves differently from `var`.
    //
    // Two shapes are not a plain local: `for (const [k, v] of pairs)` binds
    // a SHAPE, and `for (prop in obj)` with no declaration keyword assigns
    // to a binding that already exists (d bit1). Both still need a register
    // to read the element into.
    const vp::node & target = at(n.a);
    const bool declares = (n.d & 2) == 0;
    const bool is_shape = target.b >= 0;
    const std::uint16_t item =
        (declares && !is_shape) ? declare_local(std::string{target.text}) : alloc_reg();

    const std::size_t top = proto().code.size();
    loops_.push_back(loop_context{label, {}, {}, handler_depth_});
    const std::uint16_t test = alloc_reg();
    proto().emit(instruction{op::less, test, index, length});
    const std::size_t exit = proto().emit(instruction{op::jump_if_false, test});

    proto().emit(instruction{op::get_index, item, source, index});
    if (is_shape) {
        compile_pattern_binding(target.b, item, declares);
    } else if (!declares) {
        emit_write(target.text, item);
    } else if (const local * l = find_local_entry(fn(), target.text); l != nullptr && l->boxed) {
        // A captured loop variable lives in a cell, and a fresh cell per
        // iteration is what makes the capture see this element rather than
        // the last.
        proto().emit(instruction{op::new_cell, item});
    }
    compile_stmt(n.c);

    patch_continues(loops_.back(), proto().code.size());
    proto().emit(instruction{op::add, index, index, one});
    patch_jump(proto().emit(instruction{op::jump}), top);
    patch_here(exit);
    patch_breaks(loops_.back());
    loops_.pop_back();
    release_to(mark);
    pop_scope();
}

void compiler_impl::compile_switch(const vp::node & n) {
    push_scope();
    const std::uint32_t mark = reg_mark();
    const std::uint16_t subject = alloc_reg();
    compile_expr(n.a, subject);

    const std::span<const std::int32_t> clauses = kids(n);
    std::vector<std::size_t> entries(clauses.size(), 0);
    std::size_t default_clause = clauses.size();

    const std::uint16_t candidate = alloc_reg();
    const std::uint16_t matched = alloc_reg();
    for (std::size_t i = 0; i < clauses.size(); ++i) {
        const vp::node & clause = at(clauses[i]);
        // `default` is `d == 1`. Testing `d != 0` treated EVERY case as the
        // default - node.d starts at -1, which is ctjs's documented gotcha -
        // so no comparison was emitted at all and each clause ran
        // unconditionally.
        if (clause.d == 1 || clause.a < 0) {
            default_clause = i;
            continue;
        }
        compile_expr(clause.a, candidate);
        // STRICT equality, per spec - `switch (1)` does not match `case "1"`.
        proto().emit(instruction{op::equal, matched, subject, candidate});
        entries[i] = proto().emit(instruction{op::jump_if_true, matched});
    }
    const std::size_t to_default = proto().emit(instruction{op::jump});
    release_to(mark);

    // `break` inside a switch leaves the switch, so it needs a loop context
    // even though nothing here loops.
    loops_.push_back(loop_context{take_label(), {}, {}, handler_depth_});
    for (std::size_t i = 0; i < clauses.size(); ++i) {
        if (i == default_clause) {
            patch_here(to_default);
        } else {
            patch_here(entries[i]);
        }
        for (const std::int32_t statement : kids(at(clauses[i]))) { compile_stmt(statement); }
    }
    if (default_clause == clauses.size()) { patch_here(to_default); }
    patch_breaks(loops_.back());
    loops_.pop_back();
    pop_scope();
}

// A `try` WITH A `finally` IS A DIFFERENT SHAPE, and it gets its own function
// rather than more branches in this one. What is below handles `try`/`catch`
// with no finally, which is the common case and was always correct.
void compiler_impl::compile_try(const vp::node & n) {
    if (n.c >= 0) {
        compile_try_with_finally(n);
        return;
    }
    const std::int32_t catch_clause = n.b;
    const std::int32_t finally_block = n.c;

    std::uint16_t caught_reg = 0;
    std::string caught_name;
    if (catch_clause >= 0) {
        // The parameter name is the clause's TEXT and the body is its `a`.
        // Reading them as `a` and `b` compiled an empty catch block, so a
        // throw jumped correctly and then fell straight through it.
        caught_name = std::string{at(catch_clause).text};
    }

    push_scope();
    caught_reg = alloc_reg();
    const std::size_t guard = proto().emit(instruction{op::push_handler, caught_reg});
    ++handler_depth_;
    compile_stmt(n.a);
    proto().emit(instruction{op::pop_handler});
    --handler_depth_;
    if (finally_block >= 0) { compile_stmt(finally_block); }
    const std::size_t done = proto().emit(instruction{op::jump});

    // The catch block. push_handler's operand is a RELATIVE address, so it
    // is patched the same way a forward jump is.
    patch_here(guard);
    if (catch_clause >= 0) {
        push_scope();
        if (!caught_name.empty()) { declare_local_at(caught_name, caught_reg); }
        compile_stmt(at(catch_clause).a);
        pop_scope();
    }
    if (finally_block >= 0) { compile_stmt(finally_block); }
    patch_here(done);
    pop_scope();
}

// A COMPLETION RECORD AND ONE COPY OF THE BLOCK.
//
// `finally` used to be compiled by emitting the block TWICE - once after the
// try body and once after the catch - and by letting every other way out of the
// block leave without running it at all. Measured against what JavaScript
// specifies, six of nine cases were wrong, and the worst of them lost an
// exception entirely: with a finally and no catch, `push_handler` sent a throw
// to the catch path, which had no catch to run, ran the finally and FELL
// THROUGH. `try { throw x } finally { }` swallowed x, with no error anywhere.
//
// Every exit now writes what it was doing into two registers and jumps to one
// copy of the block, whose tail does that thing. That is the standard shape and
// it is the only one that makes `return`, `break`, `continue` and a rethrow all
// work without emitting the block once per exit.
void compiler_impl::compile_try_with_finally(const vp::node & n) {
    const std::int32_t catch_clause = n.b;
    const std::int32_t finally_block = n.c;

    push_scope();
    const std::uint16_t kind_reg = alloc_reg();
    const std::uint16_t value_reg = alloc_reg();
    const std::uint16_t caught_reg = alloc_reg();
    const std::uint16_t scratch = alloc_reg();

    const auto set_kind = [&](std::uint16_t k) {
        emit_const(kind_reg, value::number(static_cast<double>(k)));
    };

    finallies_.push_back(finally_context{kind_reg, value_reg, loops_.size(), {}, {}});

    // THE TRY BODY, protected. The handler lands with the thrown value in
    // `caught_reg`, which is also the catch parameter's register.
    const std::size_t guard = proto().emit(instruction{op::push_handler, caught_reg});
    ++handler_depth_;
    compile_stmt(n.a);
    proto().emit(instruction{op::pop_handler});
    --handler_depth_;
    set_kind(0);
    finallies_.back().arrivals.push_back(proto().emit(instruction{op::jump}));

    // THE EXCEPTION PATH.
    patch_here(guard);
    if (catch_clause >= 0) {
        push_scope();
        const std::string caught_name{at(catch_clause).text};
        if (!caught_name.empty()) { declare_local_at(caught_name, caught_reg); }
        // THE CATCH BODY IS PROTECTED TOO, and that is a fix of its own: a
        // throw inside a catch used to skip its own finally, because
        // `unwind_to_handler` had already consumed this try's handler on the
        // way in. `try { try { throw 1 } catch (e) { throw 2 } finally { f() } }`
        // did not run f().
        const std::size_t catch_guard = proto().emit(instruction{op::push_handler, value_reg});
        ++handler_depth_;
        compile_stmt(at(catch_clause).a);
        proto().emit(instruction{op::pop_handler});
        --handler_depth_;
        set_kind(0);
        finallies_.back().arrivals.push_back(proto().emit(instruction{op::jump}));
        patch_here(catch_guard);
        // The catch threw: `value_reg` already holds it.
        set_kind(1);
        pop_scope();
    } else {
        proto().emit(instruction{op::move, value_reg, caught_reg});
        set_kind(1);
    }

    // THE FINALLY, ONCE.
    finally_context open = std::move(finallies_.back());
    finallies_.pop_back();
    for (const std::size_t arrival : open.arrivals) { patch_here(arrival); }
    compile_stmt(finally_block);
    emit_finally_dispatch(open);
    (void)scratch;
    pop_scope();
}

// The tail of a finally: do what the completion says, or fall through.
//
// Each branch emits the SAME lowering the statement would have emitted outside
// the try - which is what makes nesting work: a `return` crossing three
// finallys is stored in the innermost, and this dispatch re-emits a return that
// finds the next one still open and stores it there in turn. No finally needs
// to know how deep it is.
void compiler_impl::emit_finally_dispatch(const finally_context & open) {
    const std::uint16_t test = alloc_reg();
    const std::uint16_t want = alloc_reg();

    const auto branch_if_kind = [&](std::uint16_t k) {
        emit_const(want, value::number(static_cast<double>(k)));
        proto().emit(instruction{op::equal, test, open.kind_reg, want});
        return proto().emit(instruction{op::jump_if_false, test});
    };

    // 1 - rethrow.
    {
        const std::size_t skip = branch_if_kind(1);
        proto().emit(instruction{op::throw_value, open.value_reg});
        patch_here(skip);
    }
    // 2 - return. `wrap_promise` is re-applied here for the same reason the
    // statement applies it: an async function hands back a promise, and this IS
    // that function's return.
    {
        const std::size_t skip = branch_if_kind(2);
        if (!route_return_through_finally(open.value_reg)) {
            if (fn().is_async && !fn().is_generator) {
                proto().emit(instruction{op::wrap_promise, open.value_reg});
            }
            proto().emit(instruction{op::ret, open.value_reg});
        }
        patch_here(skip);
    }
    // 3 + i - a loop exit that was recorded on the way in.
    for (std::size_t i = 0; i < open.exits.size(); ++i) {
        const std::size_t skip = branch_if_kind(static_cast<std::uint16_t>(3 + i));
        const finally_context::exit & one = open.exits[i];
        if (!route_exit_through_finally(one.loop_index, one.is_continue)) {
            loop_context & loop = loops_[one.loop_index];
            for (std::size_t d = handler_depth_; d > loop.handler_depth; --d) {
                proto().emit(instruction{op::pop_handler});
            }
            const std::size_t jump = proto().emit(instruction{op::jump});
            (one.is_continue ? loop.continues : loop.breaks).push_back(jump);
        }
        patch_here(skip);
    }
    // 0 - fall through, which is the whole of the normal path.
}

bool compiler_impl::route_return_through_finally(std::uint16_t value_reg) {
    if (finallies_.empty()) { return false; }
    finally_context & open = finallies_.back();
    proto().emit(instruction{op::move, open.value_reg, value_reg});
    emit_const(open.kind_reg, value::number(2.0));
    open.arrivals.push_back(proto().emit(instruction{op::jump}));
    return true;
}

bool compiler_impl::route_exit_through_finally(std::size_t loop_index, bool is_continue) {
    if (finallies_.empty()) { return false; }
    finally_context & open = finallies_.back();
    // ONLY IF THIS BREAK ACTUALLY LEAVES THE TRY. A loop opened inside it is
    // exited without the finally ever being crossed, and routing such a break
    // here made the dispatch re-emit a jump to a loop that had already been
    // popped - which asan found as a leaked patch list in the compiler.
    if (loop_index >= open.loops_open) { return false; }
    std::size_t which = 0;
    bool found = false;
    for (std::size_t i = 0; i < open.exits.size(); ++i) {
        if (open.exits[i].loop_index == loop_index && open.exits[i].is_continue == is_continue) {
            which = i;
            found = true;
            break;
        }
    }
    if (!found) {
        which = open.exits.size();
        open.exits.push_back(finally_context::exit{loop_index, is_continue});
    }
    emit_const(open.kind_reg, value::number(static_cast<double>(3 + which)));
    open.arrivals.push_back(proto().emit(instruction{op::jump}));
    return true;
}

void compiler_impl::compile_throw(const vp::node & n) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t r = alloc_reg();
    compile_expr(n.a, r);
    proto().emit(instruction{op::throw_value, r});
    release_to(mark);
}

std::string compiler_impl::take_label() {
    std::string out = std::move(pending_label_);
    pending_label_.clear();
    return out;
}

void compiler_impl::emit_implicit_return() {
    if (!fn().is_async || fn().is_generator) {
        proto().emit(instruction{op::ret_undef});
        return;
    }
    const std::uint16_t r = alloc_reg();
    proto().emit(instruction{op::load_undef, r});
    proto().emit(instruction{op::wrap_promise, r});
    proto().emit(instruction{op::ret, r});
}

void compiler_impl::compile_function_decl(std::int32_t idx) {
    const vp::node & n = at(idx);
    const std::uint32_t index = compile_function_body(idx, std::string{n.text});
    const std::uint32_t mark = reg_mark();
    const std::uint16_t r = alloc_reg();
    // `index` GOES IN WHOLE. `with_bx` takes a uint32 and splits it across b
    // and c; three of the four op::closure sites narrowed it to uint16 first,
    // which is not a bound, it is a WRAP. Measured before the casts went: a
    // program with 70,001 functions called function 69,999 and ran function
    // 4,463 - 69,999 minus 65,536 - with no error anywhere. Babylon is 31,905,
    // so the corpus in this repository sat at 49% of a ceiling that the
    // instruction encoding never had.
    proto().emit(instruction::with_bx(op::closure, r, index));
    // AT THE TOP LEVEL a function declaration is a global, by design: a page
    // defines functions the host calls by name, and script scope is what
    // sibling declarations close over.
    //
    // ANYWHERE ELSE it is a local, and this used to emit set_global at every
    // depth. Two helpers named `handler` in two different closures collided
    // in one table, and a nested function meant to capture an enclosing
    // local read a global instead. In a bundle where every module is an
    // IIFE - which is every bundle - that is the whole point of the IIFE
    // silently undone.
    // A MODULE'S TOP LEVEL IS NOT THE GLOBAL SCOPE, so its functions are
    // locals like any other binding. Leaving them global would leak every
    // module's helpers into one namespace - and, more visibly, make
    // `export function f() {}` unpublishable, because find_local would not
    // know the name.
    if (frames_.size() == 1 && !module_scope_) {
        proto().emit(instruction::with_bx(op::set_global, r, name_operand(std::string{n.text})));
    } else {
        emit_write(n.text, r);
    }
    release_to(mark);
}

std::uint32_t compiler_impl::compile_function_body(std::int32_t idx, std::string name) {
    const vp::node & n = at(idx);
    const std::uint32_t index = new_proto(offset_of(idx));
    out_.functions[index].name = std::move(name);
    // The VM cannot tell an arrow from a function once it is bytecode, and
    // it has to: an arrow sees the `this` where it was written.
    out_.functions[index].is_arrow = n.kind == vp::nk::arrow;
    out_.functions[index].source_begin = n.begin;
    out_.functions[index].source_end = n.end;

    frames_.emplace_back();
    frames_.back().proto = index;
    push_scope();
    // A FUNCTION BODY IS NOT PART OF THE CHAIN THAT ENCLOSES IT.
    //
    // `a?.b(() => c?.d)` compiles the arrow while the outer chain is open,
    // so without this the arrow's own short-circuit would be recorded on
    // the outer chain's exit list - and patched into the ENCLOSING proto's
    // code array, at an index that means something else entirely there.
    const bool saved_in_chain = in_chain_;
    std::vector<std::size_t> saved_exits;
    saved_exits.swap(optional_exits_);
    in_chain_ = false;
    // AND NEITHER IS IT PART OF AN ENCLOSING `try`. Same reasoning as the
    // chain above, and the same failure: a `return` inside this body would
    // otherwise be routed into the enclosing function's open finally, storing a
    // completion in ITS registers and pushing a jump onto ITS arrival list -
    // which is then patched into the ENCLOSING proto's code array at an index
    // that means something else in this one. Caught by the asan preset as a
    // leak in the compiler, which is what an arrival list that never gets
    // patched looks like from outside.
    std::vector<finally_context> saved_finallies;
    saved_finallies.swap(finallies_);
    // A loop and a handler stop at a function boundary for the same reason: a
    // `break` or a `pop_handler` cannot cross one.
    //
    // `loops_` USED TO BE LEFT ALONE HERE, on the argument that `loop_for` is
    // only reached from `break`/`continue` and that the parser refuses those
    // outside a loop. The comment said it was worth someone checking rather
    // than inheriting; it was checked, and it is WRONG. A LABELLED break
    // inside a nested function - `L: do { (function(){ break L; })(); }
    // while(0)` - reaches loop_for, finds the ENCLOSING function's loop, and
    // pushes a jump site onto its `breaks` list. That site is an index into
    // this proto's code array and is later patched using the OUTER proto's
    // offsets, so the inner function jumps to an instruction chosen at random:
    // test262's language/statements/break/S12.8_A6.js landed on a `load_string`
    // with an out-of-range slot and allocated until std::bad_alloc killed the
    // process (SIGABRT, measured 2026-09-03).
    //
    // Swapping the vector is the same fix the chain and the finally above
    // already use, and it makes `break L` across a function boundary the
    // compile refusal it should always have been - which is the early error
    // 13.9.1 requires, arriving as a refusal rather than a SyntaxError because
    // this engine has no early-error pass (docs/test262.md).
    std::vector<loop_context> saved_loops;
    saved_loops.swap(loops_);
    const std::size_t saved_handler_depth = handler_depth_;
    handler_depth_ = 0;
    // Which of this body's names some nested function mentions has to be
    // known BEFORE any local is declared - that is what decides whether a
    // local gets a register or a cell.
    fn().captures = range_of(idx);
    const std::span<const std::int32_t> params = kids(n);
    for (const std::int32_t p : params) { (void)declare_local(std::string{at(p).text}); }
    // WHICH LOCALS THE BOXING LOOP BELOW OWNS: the parameters, and only
    // them. The prologue declares more - every name inside a destructuring
    // pattern - and boxes those itself as it binds them. Boxing them a
    // second time here wrapped a cell in a cell, so reading the variable
    // gave the inner CELL rather than the value: a captured `{ space }`
    // parameter came out as an object with no properties, which is exactly
    // what colorjs then failed to use as a colour space.
    const std::size_t declared_parameters = fn().locals.size();
    // `arguments` IS MATERIALISED ONCE, BEFORE THE PROLOGUE, AND IS A REAL
    // LOCAL.
    //
    // Before, because the prologue REWRITES the parameter registers: a
    // destructured parameter unpacks into its own slot, a default overwrites
    // an undefined one. Building `arguments` after that read the unpacked
    // values rather than the arguments, so `function f(a, {b} = {})` had
    // `arguments[1]` holding whatever the pattern left there - which is how
    // colorjs's `isString(arguments[1])` was handed an options object it
    // then tried to use as a colour space.
    //
    // After the parameters are DECLARED, though: the calling convention puts
    // argument i in register i, so taking a register ahead of them would
    // shift every one.
    //
    // Building it where the name is MENTIONED is wrong for the same reason
    // one step further on - by then the surrounding expression has reused
    // the registers holding arguments past the last declared parameter.
    //
    // A local also gives it the right identity - one object per call, not a
    // fresh array per mention - and lets an arrow capture the enclosing
    // function's, which is the arrow rule, for free. Arrows do not make
    // their own: `mentions_arguments` descends into them so the enclosing
    // function makes one they can capture.
    std::uint16_t arguments_slot = 0;
    bool arguments_boxed = false;
    const bool wants_arguments = !out_.functions[index].is_arrow && mentions_arguments(n.a) &&
                                 find_local_in_current_scope("arguments") == nullptr;
    if (wants_arguments) {
        arguments_slot = declare_local("arguments");
        proto().emit(instruction{op::make_arguments, arguments_slot});
        arguments_boxed = fn().locals.back().boxed;
    }
    compile_parameter_prologue(params);
    // A captured PARAMETER needs boxing too, and it arrives already
    // holding its value - so box in place, after the arguments land.
    for (std::size_t i = 0; i < declared_parameters && i < fn().locals.size(); ++i) {
        const local & l = fn().locals[i];
        // `arguments` was built above and boxes itself; a pattern name is
        // declared and boxed by the prologue. Neither is a parameter, and
        // neither belongs here.
        if (l.boxed && !(wants_arguments && l.reg == arguments_slot)) {
            proto().emit(instruction{op::new_cell, l.reg});
        }
    }
    if (wants_arguments && arguments_boxed) {
        proto().emit(instruction{op::new_cell, arguments_slot});
    }
    // A NAMED FUNCTION EXPRESSION BINDS ITS OWN NAME, in its own body and
    // nowhere else. `var f = function me(n) { return me(n - 1); }` is how
    // an unnamed function recurses, and `(function pump() { raf(pump); })()`
    // is how one drives an animation loop - both called an undefined name
    // without this, silently in the second case because a callback that is
    // undefined simply never runs.
    //
    // AFTER the parameters, because the calling convention puts argument i
    // in register i: taking a register ahead of them would shift every
    // argument by one. A parameter of the same name legitimately shadows
    // this binding, so one is only made when no parameter claimed the name.
    if (n.kind == vp::nk::func_expr && !out_.functions[index].name.empty() &&
        find_local_in_current_scope(out_.functions[index].name) == nullptr) {
        const std::uint16_t self = declare_local(out_.functions[index].name);
        proto().emit(instruction{op::load_callee, self});
        if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, self}); }
    }
    collect_declared_names(n.a);
    // Declarations are hoisted to the top of the body BEFORE any nested
    // function is compiled. Without this, a nested function DECLARATION
    // (which hoists, so it compiles first) resolves the enclosing local
    // it means to capture as a global instead, and reads undefined.
    predeclare_locals(n.a);

    // c bit0 = async, bit1 = generator - but `c` DEFAULTS TO -1, so an
    // unmarked function looks async unless the sign is checked first.
    fn().is_async = n.c > 0 && (n.c & 1) != 0;
    fn().is_generator = n.c > 0 && (n.c & 2) != 0;
    proto().is_generator = fn().is_generator;

    const std::int32_t body = n.a;
    if (body >= 0 && at(body).kind == vp::nk::block) {
        for (const std::int32_t s : kids(at(body))) {
            if (at(s).kind == vp::nk::func_decl) { compile_stmt(s); }
        }
        for (const std::int32_t s : kids(at(body))) {
            if (at(s).kind != vp::nk::func_decl) { compile_stmt(s); }
        }
        emit_implicit_return();
    } else if (body >= 0) {
        // concise arrow body: `x => expr` returns expr
        const std::uint16_t r = alloc_reg();
        compile_expr(body, r);
        // NOT for a generator, even an async one. A generator's `return`
        // becomes the `value` of a `{value, done: true}` record; the
        // promise, if there is to be one, is the driver's job - which is
        // exactly what TypeScript's __awaiter helper does with it.
        if (fn().is_async && !fn().is_generator) { proto().emit(instruction{op::wrap_promise, r}); }
        proto().emit(instruction{op::ret, r});
    } else {
        emit_implicit_return();
    }
    finish_frame(index, params.size());
    pop_scope();
    frames_.pop_back();
    in_chain_ = saved_in_chain;
    finallies_.swap(saved_finallies);
    loops_.swap(saved_loops);
    handler_depth_ = saved_handler_depth;
    optional_exits_.swap(saved_exits);
    return index;
}

} // namespace ctbrowser::script::detail
