// compiler_impl - statements: `using` and `await using` (ECMAScript explicit
// resource management, 14.3.2 / 9.13).
//
// A using declaration binds a name like `const` does and REGISTERS the value
// as a resource of the enclosing scope; when the scope ends - by falling off
// its end, a `return`, a `break`, a `continue` or a throw - every resource is
// disposed in reverse order, and an error thrown while disposing is folded
// into a SuppressedError with whatever completion was already in flight.
//
// The lowering is the try/finally one (try_finally.cpp): the statements from
// the first using declaration to the end of the list are the protected body,
// and the disposal is the finally. The bookkeeping lives in a hidden
// DisposableStack-shaped object built by `__ctbrowser_using_stack` and fed by
// `__ctbrowser_using_add`; the sync disposal is one native call, the async
// one (`await using`) a loop that awaits each `[Symbol.asyncDispose]()` in
// turn. See using_stack_name and its siblings in vm.hpp, and their bodies in
// builtins/objects/function.cpp beside the destructuring natives.

#include "../compiler_impl.hpp"

namespace ctbrowser::script::detail {

bool compiler_impl::is_using_decl(const vp::node & n) {
    return n.kind == vp::nk::var_decl && (n.text == "using" || n.text == "await using");
}

// `reg = __ctbrowser_using_add(stack, reg, async)`: the value keeps its
// identity (the native answers it back), and a null or undefined registers
// nothing (9.13.1 AddDisposableResource step 1.a).
void compiler_impl::emit_using_add(std::uint16_t reg, bool async) {
    if (using_stacks_.empty()) {
        fail("a `using` declaration outside a statement list");
        return;
    }
    const std::uint32_t mark = reg_mark();
    const std::uint16_t callee = alloc_reg();
    proto().emit(
        instruction::with_bx(op::get_global, callee, intern_name(std::string{using_add_name})));
    const std::uint16_t stack = alloc_reg();
    proto().emit(instruction{op::move, stack, using_stacks_.back()});
    const std::uint16_t v = alloc_reg();
    proto().emit(instruction{op::move, v, reg});
    const std::uint16_t flag = alloc_reg();
    proto().emit(instruction{async ? op::load_true : op::load_false, flag});
    proto().emit(instruction{op::call, callee, 3});
    proto().emit(instruction{op::move, reg, callee});
    release_to(mark);
}

void compiler_impl::compile_statement_list(std::span<const std::int32_t> stmts,
                                           bool skip_func_decls) {
    for (std::size_t i = 0; i < stmts.size(); ++i) {
        const vp::node & s = at(stmts[i]);
        if (is_using_decl(s)) {
            // FROM HERE TO THE END OF THE LIST is the resources' scope.
            // Every later using declaration in this list adds to the same
            // stack, so one region covers them all.
            const std::span<const std::int32_t> rest = stmts.subspan(i);
            compile_using_region(s.text == "await using", [&] {
                for (const std::int32_t r : rest) {
                    if (skip_func_decls && at(r).kind == vp::nk::func_decl) { continue; }
                    compile_stmt(r);
                }
            });
            return;
        }
        if (skip_func_decls && s.kind == vp::nk::func_decl) { continue; }
        compile_stmt(stmts[i]);
    }
}

// The shape of compile_try_with_finally with the disposal as the finally.
// `async` says the stack may hold `await using` resources, whose disposal
// has to be awaited one by one; a sync stack is disposed by one call.
void compiler_impl::compile_using_region(bool async, const std::function<void()> & body) {
    const std::uint16_t kind_reg = alloc_reg();
    const std::uint16_t value_reg = alloc_reg();
    const std::uint16_t caught_reg = alloc_reg();
    const std::uint16_t stack_reg = alloc_reg();
    {
        const std::uint32_t mark = reg_mark();
        const std::uint16_t callee = alloc_reg();
        proto().emit(instruction::with_bx(op::get_global, callee,
                                          intern_name(std::string{using_stack_name})));
        proto().emit(instruction{op::call, callee, 0});
        proto().emit(instruction{op::move, stack_reg, callee});
        release_to(mark);
    }
    const auto set_kind = [&](std::uint16_t k) {
        emit_const(kind_reg, value::number(static_cast<double>(k)));
    };
    using_stacks_.push_back(stack_reg);
    finallies_.push_back(finally_context{kind_reg, value_reg, loops_.size(), {}, {}});

    const std::size_t guard = proto().emit(instruction{op::push_handler, caught_reg});
    ++handler_depth_;
    body();
    proto().emit(instruction{op::pop_handler});
    --handler_depth_;
    set_kind(0);
    finallies_.back().arrivals.push_back(proto().emit(instruction{op::jump}));
    // The throw path: the completion is the thrown value.
    patch_here(guard);
    proto().emit(instruction{op::move, value_reg, caught_reg});
    set_kind(1);

    const finally_context open = std::move(finallies_.back());
    finallies_.pop_back();
    using_stacks_.pop_back();
    for (const std::size_t arrival : open.arrivals) { patch_here(arrival); }

    // THE DISPOSAL. The natives are handed the completion so they can fold a
    // disposal error into it (9.13.4 DisposeResources); when what comes out
    // is a throw, the native throws it - so a throw completion never reaches
    // the dispatch below, and the return and loop-exit completions do.
    const std::uint32_t mark = reg_mark();
    if (!async) {
        const std::uint16_t callee = alloc_reg();
        proto().emit(instruction::with_bx(op::get_global, callee,
                                          intern_name(std::string{using_dispose_name})));
        const std::uint16_t s = alloc_reg();
        proto().emit(instruction{op::move, s, stack_reg});
        const std::uint16_t k = alloc_reg();
        proto().emit(instruction{op::move, k, kind_reg});
        const std::uint16_t v = alloc_reg();
        proto().emit(instruction{op::move, v, value_reg});
        proto().emit(instruction{op::call, callee, 3});
    } else {
        // loop: r = step(stack, kind, value); if (r === stack) break;
        //       try { await r } catch (e) { failed(stack, e) }
        const std::uint16_t r = alloc_reg();
        const std::uint16_t test = alloc_reg();
        const std::uint16_t caught = alloc_reg();
        const std::size_t top = proto().code.size();
        {
            const std::uint32_t inner = reg_mark();
            const std::uint16_t callee = alloc_reg();
            proto().emit(instruction::with_bx(op::get_global, callee,
                                              intern_name(std::string{using_step_name})));
            const std::uint16_t s = alloc_reg();
            proto().emit(instruction{op::move, s, stack_reg});
            const std::uint16_t k = alloc_reg();
            proto().emit(instruction{op::move, k, kind_reg});
            const std::uint16_t v = alloc_reg();
            proto().emit(instruction{op::move, v, value_reg});
            proto().emit(instruction{op::call, callee, 3});
            proto().emit(instruction{op::move, r, callee});
            release_to(inner);
        }
        proto().emit(instruction{op::equal, test, r, stack_reg});
        const std::size_t done = proto().emit(instruction{op::jump_if_true, test});
        const std::size_t await_guard = proto().emit(instruction{op::push_handler, caught});
        ++handler_depth_;
        proto().emit(instruction{op::await_value, r, r});
        proto().emit(instruction{op::pop_handler});
        --handler_depth_;
        patch_jump(proto().emit(instruction{op::jump}), top);
        patch_here(await_guard);
        {
            const std::uint32_t inner = reg_mark();
            const std::uint16_t callee = alloc_reg();
            proto().emit(instruction::with_bx(op::get_global, callee,
                                              intern_name(std::string{using_failed_name})));
            const std::uint16_t s = alloc_reg();
            proto().emit(instruction{op::move, s, stack_reg});
            const std::uint16_t e = alloc_reg();
            proto().emit(instruction{op::move, e, caught});
            proto().emit(instruction{op::call, callee, 2});
            release_to(inner);
        }
        patch_jump(proto().emit(instruction{op::jump}), top);
        patch_here(done);
    }
    release_to(mark);
    emit_finally_dispatch(open);
}

} // namespace ctbrowser::script::detail
