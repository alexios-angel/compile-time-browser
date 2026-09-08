// compiler_impl - statements: `try`/`catch`, and the completion-record lowering
// of `finally` that every non-local exit is routed through.
//
// One of four files carved out of a 1,017-line compile/statements.cpp on
// 2026-09-08 - which was itself one of the files carved out of a 3,845-line
// compile.cpp on 2026-08-09. All are members of compiler_impl, declared whole
// in compiler_impl.hpp beside the directory; nothing about that header changed.

#include "../compiler_impl.hpp"

namespace ctbrowser::script::detail {

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

} // namespace ctbrowser::script::detail
