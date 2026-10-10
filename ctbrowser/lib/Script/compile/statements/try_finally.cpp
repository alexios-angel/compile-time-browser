//===- try_finally.cpp - try/catch and finally lowering ---------*- C++ -*-===//
//
// compiler_impl members that compile `try`/`catch`, and the completion-record
// lowering of `finally` that every non-local exit (return, break, continue,
// throw) is routed through. Members are declared in ../compiler_impl.hpp.
//
//===----------------------------------------------------------------------===//

#include "../compiler_impl.hpp"

namespace ctbrowser::script::detail {

/// Compiles `try`/`catch` with no `finally`. A `try` with a `finally` is
/// delegated to compile_try_with_finally, so the `finally_block >= 0`
/// branches below never run.
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
        // The parameter name is the clause's `text` and the body is its `a`
        // (a destructuring pattern, if any, is its `b`).
        caught_name = std::string{at(catch_clause).text};
    }

    push_scope();
    caught_reg = alloc_reg();
    const std::size_t guard = proto().emit(instruction{op::push_handler, caught_reg});
    ++handler_depth_;
    compile_stmt(n.a);
    proto().emit(instruction{op::pop_handler});
    --handler_depth_;
    if (finally_block >= 0) {
        ++completion_suspended_; // A normal finally keeps the try's value (14.15.3).
        compile_stmt(finally_block);
        --completion_suspended_;
    }
    const std::size_t done = proto().emit(instruction{op::jump});

    // The catch block. push_handler's operand is a relative address, so it
    // is patched the same way a forward jump is.
    patch_here(guard);
    if (catch_clause >= 0) {
        push_scope();
        emit_catch_filter(caught_reg);
        if (!caught_name.empty()) { declare_local_at(caught_name, caught_reg); }
        // `catch ({message})`: the parameter is a pattern in `b` (14.15.3).
        if (at(catch_clause).b >= 0) {
            compile_pattern_binding(at(catch_clause).b, caught_reg, true, true);
        }
        compile_stmt(at(catch_clause).a);
        pop_scope();
    }
    if (finally_block >= 0) {
        ++completion_suspended_; // A normal finally keeps the try's value (14.15.3).
        compile_stmt(finally_block);
        --completion_suspended_;
    }
    patch_here(done);
    pop_scope();
}

/// Compiles `try ... finally` (with or without a catch) using one copy of
/// the finally block. Every exit from the try or catch (normal, throw,
/// return, break, continue) records its kind and value in two registers and
/// jumps to the block; the block's tail (emit_finally_dispatch) then performs
/// the recorded exit. This avoids emitting the block once per exit.
void compiler_impl::compile_try_with_finally(const vp::node & n) {
    const std::int32_t catch_clause = n.b;
    const std::int32_t finally_block = n.c;

    push_scope();
    // The completion record. kind_reg: 0 normal, 1 throw, 2 return, 3 + i
    // the i-th break/continue in finally_context::exits. value_reg holds the
    // thrown or returned value. emit_finally_dispatch decodes these.
    const std::uint16_t kind_reg = alloc_reg();
    const std::uint16_t value_reg = alloc_reg();
    const std::uint16_t caught_reg = alloc_reg();
    const std::uint16_t scratch = alloc_reg();

    const auto set_kind = [&](std::uint16_t k) {
        emit_const(kind_reg, value::number(static_cast<double>(k)));
    };

    finallies_.push_back(
        finally_context{kind_reg, value_reg, loops_.size(), {}, {}, handler_depth_});

    // The try body, protected. The handler lands with the thrown value in
    // `caught_reg`, which is also the catch parameter's register.
    const std::size_t guard = proto().emit(instruction{op::push_handler, caught_reg});
    ++handler_depth_;
    compile_stmt(n.a);
    proto().emit(instruction{op::pop_handler});
    --handler_depth_;
    set_kind(0);
    finallies_.back().arrivals.push_back(proto().emit(instruction{op::jump}));

    // The exception path.
    patch_here(guard);
    if (catch_clause >= 0) {
        push_scope();
        const std::string caught_name{at(catch_clause).text};
        if (!caught_name.empty()) { declare_local_at(caught_name, caught_reg); }
        // The catch body has its own handler so that a throw from the catch
        // still runs this finally: unwind_to_handler consumed the try's
        // handler on the way in.
        const std::size_t catch_guard = proto().emit(instruction{op::push_handler, value_reg});
        ++handler_depth_;
        emit_catch_filter(caught_reg); // Inside the guard, so the finally still runs.
        if (at(catch_clause).b >= 0) {
            compile_pattern_binding(at(catch_clause).b, caught_reg, true, true);
        }
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

    // The finally block, emitted once.
    const finally_context open = std::move(finallies_.back());
    finallies_.pop_back();
    for (const std::size_t arrival : open.arrivals) { patch_here(arrival); }
    ++completion_suspended_; // A normal finally keeps the try's value (14.15.3).
    compile_stmt(finally_block);
    --completion_suspended_;
    emit_finally_dispatch(open);
    (void)scratch;
    pop_scope();
}

/// A catch clause never sees a generator's return completion: `.return(v)`
/// travels through the body as a thrown marker so that every finally runs
/// (see return_marker_key), and the first thing a catch in a generator does
/// is rethrow a marker. Costs one native call, on the exceptional path only.
void compiler_impl::emit_catch_filter(std::uint16_t caught) {
    if (!fn().is_generator) { return; }
    const std::uint32_t mark = reg_mark();
    const std::uint16_t scratch = alloc_reg();
    emit_iterator_native(catch_filter_name, scratch, caught);
    release_to(mark);
}

/// Emits the tail of a finally block: performs the exit recorded in
/// \p open's completion record (see compile_try_with_finally), or falls
/// through for a normal completion.
///
/// Each branch emits the same lowering the statement would have emitted
/// outside the try, which is what makes nesting work: a `return` crossing
/// three finally blocks is stored in the innermost, and this dispatch
/// re-emits a return that finds the next one still open and stores it there
/// in turn. No finally needs to know how deep it is.
void compiler_impl::emit_finally_dispatch(const finally_context & open) {
    const std::uint16_t test = alloc_reg();
    const std::uint16_t want = alloc_reg();

    const auto branch_if_kind = [&](std::uint16_t k) {
        emit_const(want, value::number(static_cast<double>(k)));
        proto().emit(instruction{op::equal, test, open.kind_reg, want});
        return proto().emit(instruction{op::jump_if_false, test});
    };

    // Kind 1: rethrow.
    {
        const std::size_t skip = branch_if_kind(1);
        emit_rethrow(open.value_reg, true);
        patch_here(skip);
    }
    // Kind 2: return. `wrap_promise` is re-applied here for the same reason
    // the statement applies it: an async function hands back a promise, and
    // this is that function's return.
    {
        const std::size_t skip = branch_if_kind(2);
        if (!route_return_through_finally(open.value_reg)) {
            if (!fn().derived_flag.empty()) {
                emit_derived_return(open.value_reg);
            } else {
                if (fn().is_async && !fn().is_generator) {
                    proto().emit(instruction{op::wrap_promise, open.value_reg});
                }
                proto().emit(instruction{op::ret, open.value_reg});
            }
        }
        patch_here(skip);
    }
    // Kind 3 + i: a loop exit that was recorded on the way in.
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
    // Kind 0: fall through, which is the whole of the normal path.
}

void compiler_impl::emit_caught_throw(std::uint16_t value_reg) {
    if (proto().debug_offsets) {
        proto().rethrows.push_back(static_cast<std::uint32_t>(proto().code.size()));
    }
    proto().emit(instruction{op::throw_value, value_reg});
}

void compiler_impl::emit_rethrow(std::uint16_t value_reg, bool caught) {
    // Close the iterators of for-of loops opened at this handler depth. A
    // catch/finally inside the loop gets the exception before the iterator
    // closes; its eventual rethrow comes back here at the surrounding depth.
    const std::uint32_t mark = reg_mark();
    for (auto loop = loops_.rbegin(); loop != loops_.rend(); ++loop) {
        if (loop->handler_depth != handler_depth_) { break; }
        if (!loop->iterator_record) { continue; }
        const std::uint16_t ignored = alloc_reg();
        const std::uint16_t scratch = alloc_reg();
        // The original exception wins even if getting iterator.return throws.
        const std::size_t guard = proto().emit(instruction{op::push_handler, ignored});
        emit_iterator_native(iterator_close_name, scratch, *loop->iterator_record, 1);
        proto().emit(instruction{op::pop_handler});
        const std::size_t closed = proto().emit(instruction{op::jump});
        patch_here(guard);
        // A catch landing must not also be reachable from the normal path.
        const std::size_t suppressed = proto().emit(instruction{op::jump});
        patch_here(closed);
        patch_here(suppressed);
        release_to(mark);
    }
    if (caught) {
        emit_caught_throw(value_reg);
    } else {
        proto().emit(instruction{op::throw_value, value_reg});
    }
}

bool compiler_impl::route_return_through_finally(std::uint16_t value_reg) {
    // The return expression is already evaluated. Close only scopes crossed
    // before the next finally, which may replace this return with a continue.
    const std::size_t first = finallies_.empty() ? 0 : finallies_.back().loops_open;
    std::size_t depth = handler_depth_;
    const auto pop_to = [&](std::size_t target) {
        while (depth > target) {
            proto().emit(instruction{op::pop_handler});
            --depth;
        }
    };
    const std::uint32_t mark = reg_mark();
    for (std::size_t end = loops_.size(); end > first;) {
        const auto & loop = loops_[--end];
        if (!loop.iterator_record) { continue; }
        // Catches inside an exited iterator cannot catch its close error.
        // A catch between iterators must run before any outer iterator closes.
        pop_to(loop.handler_depth);
        std::vector<std::uint16_t> records{*loop.iterator_record};
        while (end > first && loops_[end - 1].handler_depth == loop.handler_depth) {
            if (auto record = loops_[--end].iterator_record) { records.push_back(*record); }
        }
        const std::uint16_t scratch = alloc_reg();
        if (records.size() == 1) {
            emit_iterator_native(iterator_close_name, scratch, records.front(), 0);
            release_to(mark);
            continue;
        }
        // One guard for the close sequence, not the loop body. On failure the
        // runtime's done flag skips already closed records; remaining closes
        // cannot replace the first error, even when their return getter throws.
        const std::uint16_t caught = alloc_reg();
        const std::uint16_t ignored = alloc_reg();
        const std::size_t guard = proto().emit(instruction{op::push_handler, caught});
        for (auto record : records) {
            emit_iterator_native(iterator_close_name, scratch, record, 0);
        }
        proto().emit(instruction{op::pop_handler});
        const std::size_t done = proto().emit(instruction{op::jump});
        patch_here(guard);
        for (auto record : records) {
            const std::size_t suppress = proto().emit(instruction{op::push_handler, ignored});
            emit_iterator_native(iterator_close_name, scratch, record, 1);
            proto().emit(instruction{op::pop_handler});
            patch_here(suppress);
        }
        emit_caught_throw(caught);
        patch_here(done);
        release_to(mark);
    }
    if (finallies_.empty()) {
        // The body is complete: its catches cannot intercept [[Construct]]'s
        // derived-return validation, emitted by the caller before op::ret.
        pop_to(0);
        return false;
    }
    finally_context & open = finallies_.back();
    pop_to(open.handler_depth);
    proto().emit(instruction{op::move, open.value_reg, value_reg});
    emit_const(open.kind_reg, value::number(2.0));
    open.arrivals.push_back(proto().emit(instruction{op::jump}));
    return true;
}

bool compiler_impl::route_exit_through_finally(std::size_t loop_index, bool is_continue) {
    if (finallies_.empty()) { return false; }
    finally_context & open = finallies_.back();
    // Only if this break actually leaves the try. A loop opened inside it is
    // exited without crossing the finally; routing such a break here would
    // make the dispatch re-emit a jump to a loop that has already been popped.
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
    for (std::size_t depth = handler_depth_; depth > open.handler_depth; --depth) {
        proto().emit(instruction{op::pop_handler});
    }
    emit_const(open.kind_reg, value::number(static_cast<double>(3 + which)));
    open.arrivals.push_back(proto().emit(instruction{op::jump}));
    return true;
}

} // namespace ctbrowser::script::detail
