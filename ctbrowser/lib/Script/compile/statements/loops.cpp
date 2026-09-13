// compiler_impl - statements: the loops, `switch`, labels, `break` and
// `continue`, and the loop_context bookkeeping they share.
//
// One of four files carved out of a 1,017-line compile/statements.cpp on
// 2026-09-08 - which was itself one of the files carved out of a 3,845-line
// compile.cpp on 2026-08-09. All are members of compiler_impl, declared whole
// in compiler_impl.hpp beside the directory; nothing about that header changed.

#include "../compiler_impl.hpp"
#include <algorithm>

namespace ctbrowser::script::detail {

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
        if (std::ranges::find(loops_[i].labels, label) != loops_[i].labels.end()) {
            return &loops_[i];
        }
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
    pending_labels_.emplace_back(n.text);
    if (owns_its_label) {
        compile_stmt(n.a);
        pending_labels_.clear();
        return;
    }
    loops_.push_back(loop_context{take_labels(), {}, {}, handler_depth_});
    compile_stmt(n.a);
    patch_breaks(loops_.back());
    if (!loops_.back().continues.empty()) {
        fail("`continue " + std::string{n.text} + "` names a block, not a loop");
    }
    loops_.pop_back();
}

void compiler_impl::compile_while(const vp::node & n) {
    const std::size_t top = proto().code.size();
    loops_.push_back(loop_context{take_labels(), {}, {}, handler_depth_});
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
    loops_.push_back(loop_context{take_labels(), {}, {}, handler_depth_});
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
    const std::vector<std::string> label = take_labels();
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

// `for await (x of y)`, 14.7.5.6 with iteratorKind async. Unlike the sync
// loop below, which MATERIALISES its source through op::iterable, this one
// runs the real protocol: GetIterator(async) through the builtin's hidden
// native, then `next()` -> await -> `done`/`value` per iteration, so an async
// generator is pulled lazily and a promise in a sync iterator's `value` is
// awaited. Only the statement form; `for await` at a module's top level is
// refused with the same message as an `await` there would be.
// AsyncIteratorClose (7.4.13) runs on `break` and on a throw out of the body:
// the iterator's `return()`, if it has one, is called and awaited. A `return`
// statement inside the body and a throw from `next()` itself do not close
// (the first is a known gap; the second is the specification). The sync
// for-of below closes on `break` only.
void compiler_impl::compile_for_await(const vp::node & n) {
    if (!fn().is_async) {
        fail("`for await` outside an async function");
        return;
    }
    push_scope();
    const std::vector<std::string> label = take_labels();
    const std::uint32_t mark = reg_mark();

    const std::uint16_t iterator = alloc_reg();
    proto().emit(instruction::with_bx(op::get_global, iterator,
                                      intern_name(std::string{async_iterator_name})));
    const std::uint16_t source = alloc_reg();
    compile_expr(n.b, source);
    proto().emit(instruction{op::call, iterator, 1});

    const vp::node & target = at(n.a);
    const bool declares = (n.d & 2) == 0;
    const bool is_shape = target.b >= 0;
    // `for (var x in o)` AT A SCRIPT'S TOP LEVEL binds the GLOBAL x, as a
    // top-level `var x` does - a local scoped to the loop left `x` unbound
    // after it, so the next `for (x in o)` in strict code was an assignment
    // to an unresolvable name. Written as a declaration (declaring_).
    const bool var_global = declares && (n.d & 57) == 0 && frames_.size() == 1 && !module_scope_;
    const std::uint16_t item = (declares && !is_shape && !var_global)
                                   ? declare_local(std::string{target.text})
                                   : alloc_reg();

    const std::size_t top = proto().code.size();
    loops_.push_back(loop_context{label, {}, {}, handler_depth_});
    const std::uint16_t step = alloc_reg();
    proto().emit(instruction{op::get_prop, step, iterator, name_operand("next")});
    proto().emit(instruction{op::call_receiver, step, 0, iterator});
    proto().emit(instruction{op::await_value, step, step});
    const std::uint16_t done = alloc_reg();
    proto().emit(instruction{op::get_prop, done, step, name_operand("done")});
    const std::size_t exit = proto().emit(instruction{op::jump_if_true, done});
    proto().emit(instruction{op::get_prop, item, step, name_operand("value")});
    // The body is protected so a throw closes the iterator; `continue` and the
    // fall-through pop the handler, `break` pops it on its way out (it counts
    // in handler_depth_).
    const std::uint16_t caught = alloc_reg();
    const std::size_t guard = proto().emit(instruction{op::push_handler, caught});
    ++handler_depth_;
    const bool using_head = (n.d & 48) != 0; // see compile_for_of
    const auto bind_and_body = [&] {
        if (using_head) { emit_using_add(item, (n.d & 32) != 0); }
        if (var_global) {
            const bool outer_declaring = declaring_;
            declaring_ = true;
            if (is_shape) {
                compile_pattern_binding(target.b, item, false);
            } else {
                emit_write(target.text, item);
            }
            declaring_ = outer_declaring;
        } else if (is_shape) {
            compile_pattern_binding(target.b, item, declares);
        } else if (!declares) {
            emit_write(target.text, item);
        } else if (const local * l = find_local_entry(fn(), target.text);
                   l != nullptr && l->boxed) {
            proto().emit(instruction{op::new_cell, item});
        }
        compile_stmt(n.c);
    };
    if (using_head) {
        compile_using_region((n.d & 32) != 0, bind_and_body);
    } else {
        bind_and_body();
    }
    --handler_depth_;
    patch_continues(loops_.back(), proto().code.size());
    proto().emit(instruction{op::pop_handler});
    patch_jump(proto().emit(instruction{op::jump}), top);

    // `return()` on the iterator, awaited, when it has one.
    const auto close = [&] {
        const std::uint16_t back = alloc_reg();
        proto().emit(instruction{op::get_prop, back, iterator, name_operand("return")});
        const std::size_t has = proto().emit(instruction{op::jump_if_defined, back});
        const std::size_t none = proto().emit(instruction{op::jump});
        patch_here(has);
        proto().emit(instruction{op::call_receiver, back, 0, iterator});
        proto().emit(instruction{op::await_value, back, back});
        patch_here(none);
    };
    // break: close, then leave.
    patch_breaks(loops_.back());
    close();
    const std::size_t leave = proto().emit(instruction{op::jump});
    // throw: close, then rethrow.
    patch_here(guard);
    close();
    proto().emit(instruction{op::throw_value, caught});
    patch_here(exit);
    patch_here(leave);
    loops_.pop_back();
    release_to(mark);
    pop_scope();
}

void compiler_impl::compile_for_of(const vp::node & n) {
    if ((n.d & 4) != 0) {
        compile_for_await(n);
        return;
    }
    push_scope();
    const std::vector<std::string> label = take_labels();
    const std::uint32_t mark = reg_mark();

    const std::uint16_t source = alloc_reg();
    compile_expr(n.b, source);
    const bool of = n.text != "in";
    // `for (x of ...)` RUNS THE ITERATOR PROTOCOL for a generator or a page's
    // own iterable - one `next()` per iteration, `return()` on `break` - and
    // stays the index loop for everything op::iterable materialises exactly:
    // see for_of_open_name, which answers undefined for those. The choice is
    // one register tested at the top of every iteration, so the two paths
    // share one body and one set of locals, and `for (x in ...)` never has a
    // record at all. Not closed on a `return` or a throw out of the body:
    // that needs a handler round the body, and a protected region is what
    // ctcompile's importer refuses a function for.
    const std::uint16_t record = of ? alloc_reg() : 0;
    std::size_t lazy = 0;
    if (of) {
        emit_iterator_native(for_of_open_name, record, source);
        lazy = proto().emit(instruction{op::jump_if_defined, record});
        proto().emit(instruction{op::iterable, source, source});
    } else {
        proto().emit(instruction{op::own_keys, source, source});
    }
    const std::uint16_t length = alloc_reg();
    const std::uint16_t length_name = name_operand("length");
    proto().emit(instruction{op::get_prop, length, source, length_name});
    if (of) { patch_here(lazy); }
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
    // `for (var x in o)` AT A SCRIPT'S TOP LEVEL binds the GLOBAL x, as a
    // top-level `var x` does - a local scoped to the loop left `x` unbound
    // after it, so the next `for (x in o)` in strict code was an assignment
    // to an unresolvable name. Written as a declaration (declaring_).
    const bool var_global = declares && (n.d & 57) == 0 && frames_.size() == 1 && !module_scope_;
    const std::uint16_t item = (declares && !is_shape && !var_global)
                                   ? declare_local(std::string{target.text})
                                   : alloc_reg();

    const std::size_t top = proto().code.size();
    loops_.push_back(loop_context{label, {}, {}, handler_depth_});
    const std::size_t step = of ? proto().emit(instruction{op::jump_if_defined, record}) : 0;
    const std::uint16_t test = alloc_reg();
    proto().emit(instruction{op::less, test, index, length});
    const std::size_t exit = proto().emit(instruction{op::jump_if_false, test});
    proto().emit(instruction{op::get_index, item, source, index});
    std::size_t finished = 0;
    if (of) {
        const std::size_t body = proto().emit(instruction{op::jump});
        patch_here(step);
        emit_iterator_native(iterator_next_name, item, record);
        proto().emit(instruction{op::get_prop, test, record, name_operand("done")});
        finished = proto().emit(instruction{op::jump_if_true, test});
        patch_here(body);
    }
    // `for (using x of xs)` (d bit4, bit5 for `await using`): each iteration
    // is a region of its own, disposed before the next element is read.
    const bool using_head = (n.d & 48) != 0;
    const auto bind_and_body = [&] {
        if (using_head) { emit_using_add(item, (n.d & 32) != 0); }
        if (var_global) {
            const bool outer_declaring = declaring_;
            declaring_ = true;
            if (is_shape) {
                compile_pattern_binding(target.b, item, false);
            } else {
                emit_write(target.text, item);
            }
            declaring_ = outer_declaring;
        } else if (is_shape) {
            compile_pattern_binding(target.b, item, declares);
        } else if (!declares) {
            emit_write(target.text, item);
        } else if (const local * l = find_local_entry(fn(), target.text);
                   l != nullptr && l->boxed) {
            // A captured loop variable lives in a cell, and a fresh cell per
            // iteration is what makes the capture see this element rather than
            // the last.
            proto().emit(instruction{op::new_cell, item});
        }
        compile_stmt(n.c);
    };
    if (using_head) {
        compile_using_region((n.d & 32) != 0, bind_and_body);
    } else {
        bind_and_body();
    }

    patch_continues(loops_.back(), proto().code.size());
    proto().emit(instruction{op::add, index, index, one});
    patch_jump(proto().emit(instruction{op::jump}), top);
    patch_here(exit);
    // `break` lands here too: IteratorClose (7.4.10) on a record that is not
    // done, nothing at all on the index loop's undefined or a finished one.
    patch_breaks(loops_.back());
    if (of) {
        patch_here(finished);
        emit_iterator_native(iterator_close_name, record, record, 0);
    }
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
    loops_.push_back(loop_context{take_labels(), {}, {}, handler_depth_});
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

std::vector<std::string> compiler_impl::take_labels() {
    std::vector<std::string> out = std::move(pending_labels_);
    pending_labels_.clear();
    return out;
}

} // namespace ctbrowser::script::detail
