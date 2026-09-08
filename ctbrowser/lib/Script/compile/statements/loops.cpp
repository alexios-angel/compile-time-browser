// compiler_impl - statements: the loops, `switch`, labels, `break` and
// `continue`, and the loop_context bookkeeping they share.
//
// One of four files carved out of a 1,017-line compile/statements.cpp on
// 2026-09-08 - which was itself one of the files carved out of a 3,845-line
// compile.cpp on 2026-08-09. All are members of compiler_impl, declared whole
// in compiler_impl.hpp beside the directory; nothing about that header changed.

#include "../compiler_impl.hpp"

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

std::string compiler_impl::take_label() {
    std::string out = std::move(pending_label_);
    pending_label_.clear();
    return out;
}

} // namespace ctbrowser::script::detail
