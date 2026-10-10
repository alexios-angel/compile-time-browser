//===- functions.cpp - Function declarations and bodies ---------*- C++ -*-===//
//
// compiler_impl members that compile function declarations and function
// bodies: the parameter prologue, `arguments`, the named-function-expression
// binding, the async fence, derived-constructor `super()` checks and implicit
// returns. Members are declared in ../compiler_impl.hpp.
//
//===----------------------------------------------------------------------===//

#include "../compiler_impl.hpp"

#include <algorithm>

namespace ctbrowser::script::detail {

const std::string * compiler_impl::derived_flag() {
    for (std::size_t level = frames_.size(); level-- > 0;) {
        const frame & f = frames_[level];
        if (!f.derived_flag.empty()) { return &f.derived_flag; }
        if (!out_.functions[f.proto].is_arrow) { return nullptr; }
    }
    return nullptr;
}

void compiler_impl::emit_super_check(const std::string & flag, bool again) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t ran = alloc_reg();
    emit_plain_read(flag, ran);
    const std::size_t fine =
        proto().emit(instruction{again ? op::jump_if_false : op::jump_if_true, ran});
    emit_throw("ReferenceError", again ? "Super constructor may only be called once"
                                       : "Must call super constructor in derived class before "
                                         "accessing 'this' or returning from derived constructor");
    patch_here(fine);
    release_to(mark);
}

void compiler_impl::emit_super_done(const std::string & flag) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t yes = alloc_reg();
    proto().emit(instruction{op::load_true, yes});
    emit_plain_write(flag, yes);
    release_to(mark);
}

void compiler_impl::emit_derived_return(std::uint16_t value) {
    const std::uint32_t mark = reg_mark();
    const std::size_t defined = proto().emit(instruction{op::jump_if_defined, value});
    emit_super_check(fn().derived_flag, false);
    {
        // Return `this` explicitly, not ret_undef: super() may have rebound it to
        // the object the parent returned (bind_this_name), which [[Construct]]
        // would not see in the instance it made.
        const std::uint16_t self = alloc_reg();
        proto().emit(instruction{op::load_this, self});
        proto().emit(instruction{op::ret, self});
    }
    patch_here(defined);
    const std::uint16_t kind = alloc_reg();
    proto().emit(instruction{op::type_of, kind, value});
    const std::uint16_t want = alloc_reg();
    const std::uint16_t same = alloc_reg();
    // typeof null is "object", but [[Construct]] only accepts actual objects.
    proto().emit(instruction{op::load_null, want});
    proto().emit(instruction{op::equal, same, value, want});
    const std::size_t is_null = proto().emit(instruction{op::jump_if_true, same});
    emit_string(want, "object");
    proto().emit(instruction{op::equal, same, kind, want});
    const std::size_t is_object = proto().emit(instruction{op::jump_if_true, same});
    emit_string(want, "function");
    proto().emit(instruction{op::equal, same, kind, want});
    const std::size_t is_function = proto().emit(instruction{op::jump_if_true, same});
    patch_here(is_null);
    emit_throw("TypeError", "Derived constructors may only return object or undefined");
    patch_here(is_object);
    patch_here(is_function);
    proto().emit(instruction{op::ret, value});
    release_to(mark);
}

void compiler_impl::emit_implicit_return() {
    if (!fn().derived_flag.empty()) {
        emit_super_check(fn().derived_flag, false);
        // See emit_derived_return: `this` may be the parent's returned object.
        const std::uint16_t self = alloc_reg();
        proto().emit(instruction{op::load_this, self});
        proto().emit(instruction{op::ret, self});
        return;
    }
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
    // A function declared in a block is a binding of that block (14.2.1, and
    // the `let`-like half of B.3.2). predeclare_locals hoists only a body's
    // own statements, so the block's local is declared here, before the body
    // compiles, so that a recursive call inside resolves to it. Its register
    // survives the statement because compile_stmt does not release a
    // declaration's.
    //
    // A classic script's own top level is the one place the declaration is
    // the global (16.1.7 step 17). A block inside a classic script still gets
    // its block binding: B.3.3.2's var write is a copy of it and is skipped
    // whenever a `let` of the same name shadows.
    const bool script_scope = frames_.size() == 1 && !top_level_is_scope_;
    const bool script_top_level = script_scope && fn().scope_marks.size() <= 1;
    bool block_local = false;
    if (!script_top_level && find_local_in_current_scope(n.text) == nullptr) {
        const std::uint16_t r = declare_local(std::string{n.text});
        proto().emit(instruction{op::load_undef, r});
        if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, r}); }
        block_local = true;
    }
    const std::uint32_t index = compile_function_body(idx, std::string{n.text});
    const std::uint32_t mark = reg_mark();
    const std::uint16_t r = alloc_reg();
    // Pass `index` as a full uint32: with_bx splits it across b and c.
    // Narrowing it to uint16 would wrap silently past 65,535 functions and
    // create the wrong closure.
    proto().emit(instruction::with_bx(op::closure, r, index));
    // Only a classic script's top-level declaration is a global: a page
    // defines functions the host calls by name. Anywhere else (nested
    // functions, IIFE bundles, and a module's top level) it is a local, so
    // helpers in different closures do not collide and `export function f`
    // can find its binding.
    //
    // write_script_var: the script's var is a global, unless this is a direct
    // eval's program and its caller binds the name; then the eval's
    // var-scoped function is that binding (19.2.1.3 step 8, B.3.2.3), as its
    // `var` is (compile_stmt).
    const auto write_script_var = [&] {
        if (const int up = resolve_upvalue(0, n.text); up >= 0) {
            proto().emit(instruction{op::set_upvalue, static_cast<std::uint16_t>(up), r});
        } else if (!eval_vars_name_.empty()) {
            // Or the binding lives on the caller's function's variable
            // environment object (with_scope::eval_vars), where
            // compile_program made it.
            const std::uint32_t object_mark = reg_mark();
            const std::uint16_t object = alloc_reg();
            emit_plain_read(eval_vars_name_, object);
            proto().emit(instruction{op::set_prop, object, name_operand(std::string{n.text}), r});
            release_to(object_mark);
        } else {
            proto().emit(
                instruction::with_bx(op::set_global, r, name_operand(std::string{n.text})));
        }
    };
    if (script_top_level) {
        write_script_var();
    } else {
        emit_write(n.text, r);
        // B.3.3.1 step 1.a.ii.3 / B.3.3.2 step 1.a.ii.3: when the declaration
        // is evaluated, the block binding's value is copied to the var
        // binding of the same name - a global in a classic script, otherwise
        // the one predeclare_locals made, the first entry the frame has for
        // the name (the block's own is the last).
        if (block_local && !fn().is_strict &&
            std::find(fn().annex_b_decls.begin(), fn().annex_b_decls.end(), idx) !=
                fn().annex_b_decls.end()) {
            if (script_scope) {
                write_script_var();
            } else if (const auto it = fn().local_index.find(n.text);
                       it != fn().local_index.end() && it->second.size() >= 2) {
                const local & outer = fn().locals[it->second.front()];
                proto().emit(instruction{outer.boxed ? op::cell_set : op::move, outer.reg, r});
            }
        }
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

    // Strictness is inherited, and a class body or the function's own
    // directive adds it (11.2.2). Decided from the enclosing frame, before the
    // new frame is pushed.
    const bool strict =
        fn().is_strict || class_body_depth_ > 0 ||
        (n.a >= 0 && at(n.a).kind == vp::nk::block && has_use_strict_directive(n.a));
    frames_.emplace_back();
    frames_.back().proto = index;
    frames_.back().is_strict = strict;
    frames_.back().has_home = method_pending_;
    method_pending_ = false;
    out_.functions[index].is_strict = strict;
    // A body is not the declaration that holds it: `const f = function () {
    // typo = 1; }` writes `typo` as an assignment, whatever `f` is.
    const not_declaring body_is_not_a_write{*this};
    push_scope();
    // A function body is not part of the optional chain that encloses it.
    // `a?.b(() => c?.d)` compiles the arrow while the outer chain is open, so
    // without this the arrow's own short-circuit would be recorded on the
    // outer chain's exit list and patched into the enclosing proto's code
    // array, at an index that means something else there.
    const bool saved_in_chain = in_chain_;
    std::vector<std::size_t> saved_exits;
    saved_exits.swap(optional_exits_);
    in_chain_ = false;
    // Nor is it part of an enclosing `try`, for the same reason: a `return`
    // inside this body would otherwise be routed into the enclosing
    // function's open finally, storing a completion in its registers and
    // pushing a jump onto its arrival list, to be patched into the wrong
    // proto's code array.
    std::vector<finally_context> saved_finallies;
    saved_finallies.swap(finallies_);
    // Loops and handlers stop at a function boundary too: a `break` or a
    // `pop_handler` cannot cross one. A labelled break inside a nested
    // function, `L: do { (function(){ break L; })(); } while(0)`, would
    // otherwise find the enclosing function's loop and record a jump patched
    // with the wrong proto's offsets. With loops_ swapped out it becomes a
    // compile refusal, standing in for the 13.9.1 early error (there is no
    // early-error pass; see docs/test262.md).
    std::vector<loop_context> saved_loops;
    saved_loops.swap(loops_);
    const std::size_t saved_handler_depth = handler_depth_;
    handler_depth_ = 0;
    // Which of this body's names some nested function mentions must be known
    // before any local is declared: that decides whether a local gets a
    // plain register or a cell.
    fn().captures = range_of(idx);
    const std::span<const std::int32_t> params = kids(n);
    for (const std::int32_t p : params) { (void)declare_local(std::string{at(p).text}); }
    // `arguments` is materialised once, as a real local, after the
    // parameters are declared and before the prologue runs.
    //
    // Before the prologue, because the prologue rewrites the parameter
    // registers: a destructured parameter unpacks into its own slot and a
    // default overwrites an undefined one, so `function f(a, {b} = {})` would
    // see the pattern's leftovers in `arguments[1]`. After the declarations,
    // because the calling convention puts argument i in register i, and
    // taking a register ahead of them would shift every argument. (Building
    // it where the name is mentioned is too late: by then the surrounding
    // expression has reused the registers holding the extra arguments.)
    //
    // A local also gives it the right identity, one object per call, and lets
    // an arrow capture the enclosing function's. Arrows never make their own:
    // `mentions_arguments` descends into them so the enclosing function makes
    // one they can capture.
    std::uint16_t arguments_slot = 0;
    bool arguments_boxed = false;
    // A parameter default may mention it too (10.2.11 steps 15-22:
    // `arguments` is needed when the parameters or the body name it), as in
    // `function f(x = arguments[2])`.
    const bool wants_arguments =
        !out_.functions[index].is_arrow &&
        (mentions_arguments(n.a) ||
         std::ranges::any_of(params, [&](std::int32_t p) { return mentions_arguments(p); })) &&
        find_local_in_current_scope("arguments") == nullptr;
    if (wants_arguments) {
        arguments_slot = declare_local("arguments");
        proto().emit(instruction{op::make_arguments, arguments_slot});
        arguments_boxed = fn().locals.back().boxed;
    }
    // The object a sloppy direct eval's `var`s are created on
    // (with_scope::eval_vars). Made before the defaults, since an eval may sit
    // in one, and after the parameters and `arguments`, whose registers it
    // must not shift. It has a null prototype so `toString` is nobody's
    // binding here (`undefined` is the explicit null of
    // object_object::prototype). Its `locals_mark` is 0: the eval may not
    // shadow a binding of this function's own, so none is looked up on it.
    // It is named by how many such objects enclose it, which no two on one
    // scope chain share (compile_program orders the ones it is handed so).
    if (!strict && own_direct_eval(idx)) {
        const auto depth =
            std::ranges::count_if(with_scopes_, [](const with_scope & w) { return w.eval_vars; });
        fn().eval_vars = std::string{eval_vars_prefix} + std::to_string(depth);
        const std::uint16_t held = declare_local(fn().eval_vars);
        fn().locals.back().boxed = true;
        proto().emit(instruction{op::new_object, held});
        const std::uint32_t mark = reg_mark();
        const std::uint16_t none = alloc_reg();
        proto().emit(instruction{op::load_undef, none});
        proto().emit(instruction{op::set_proto, held, none});
        release_to(mark);
        proto().emit(instruction{op::new_cell, held});
        with_scopes_.push_back(with_scope{fn().eval_vars, frames_.size() - 1, 0, true});
    }
    // An async (non-generator) function's fence also covers its parameter
    // defaults, so it is pushed here, before the prologue. See the fence
    // below. (Captured parameters are boxed inside compile_parameter_prologue,
    // before any default reads them.)
    const bool is_async_fn = n.c > 0 && (n.c & 1) != 0;
    const bool is_generator_fn = n.c > 0 && (n.c & 2) != 0;
    const bool fenced = is_async_fn;
    constexpr std::size_t no_fence = static_cast<std::size_t>(-1);
    std::uint16_t fence_reg = 0;
    std::size_t fence_guard = no_fence;
    if (fenced && !is_generator_fn) {
        fence_reg = alloc_reg();
        fence_guard = proto().emit(instruction{op::push_handler, fence_reg});
        ++handler_depth_;
    }
    {
        // A direct eval in a default may not `var` a parameter's name, nor
        // `arguments` when the function would make one (10.2.11 steps 15-22:
        // an arrow never does, a function naming a parameter `arguments`
        // does not) - see param_eval_name.
        std::vector<std::string> saved_scope;
        saved_scope.swap(param_scope_names_);
        bool names_arguments = false;
        for (const std::int32_t p : params) {
            std::vector<std::string> names;
            if (at(p).b >= 0) {
                pattern_names(at(p).b, names);
            } else {
                names.emplace_back(at(p).text);
            }
            for (std::string & name : names) {
                names_arguments = names_arguments || name == "arguments";
                param_scope_names_.push_back(std::move(name));
            }
        }
        if (!out_.functions[index].is_arrow && !names_arguments) {
            param_scope_names_.emplace_back("arguments");
        }
        compile_parameter_prologue(params);
        param_scope_names_.swap(saved_scope);
    }
    if (wants_arguments && arguments_boxed) {
        proto().emit(instruction{op::new_cell, arguments_slot});
    }
    // A named function expression binds its own name, in its own body and
    // nowhere else: `var f = function me(n) { return me(n - 1); }` and
    // `(function pump() { raf(pump); })()` both rely on it.
    //
    // Declared after the parameters, because the calling convention puts
    // argument i in register i. A parameter of the same name legitimately
    // shadows this binding, so one is made only when no parameter claimed
    // the name.
    if (n.kind == vp::nk::func_expr && idx != default_function_ &&
        !out_.functions[index].name.empty() &&
        find_local_in_current_scope(out_.functions[index].name) == nullptr) {
        const std::uint16_t self = declare_local(out_.functions[index].name);
        // CreateImmutableBinding(name, false) (15.2.5 step 4): `me = 1` in
        // the body is ignored in sloppy code and a TypeError in strict code.
        // A name the node does not carry belongs to a class member: a
        // constructor's is the class's own name, immutable in its body
        // (class_bindings_). A method binds no name at all (15.4.4), so its
        // local is a known deviation (`foo` inside method `foo` reads the
        // method). It is left mutable and in place because removing it
        // changes the bytecode of every class method, which ctcompile's
        // proofs are pinned to.
        if (!n.text.empty()) {
            fn().locals.back().readonly = immutability::sloppy;
        } else if (!class_bindings_.empty() &&
                   class_bindings_.back().name == out_.functions[index].name) {
            fn().locals.back().readonly = immutability::strict;
        }
        proto().emit(instruction{op::load_callee, self});
        if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, self}); }
    }
    // A derived constructor's `super()`-has-run flag (see
    // frame::derived_flag). Always boxed, whatever the capture index says,
    // because an arrow in the body reads and writes it as an upvalue.
    if (derived_ctor_pending_) {
        derived_ctor_pending_ = false;
        fn().derived_flag = "@super:" + std::to_string(index);
        const std::uint16_t flag = declare_local(fn().derived_flag);
        fn().locals.back().boxed = true;
        proto().emit(instruction{op::load_false, flag});
        proto().emit(instruction{op::new_cell, flag});
    }
    // The new.target this function's arrows see (see new_target_local).
    if (!out_.functions[index].is_arrow && is_captured(new_target_local) &&
        arrow_wants_new_target(n.a, false)) {
        const std::uint16_t held = declare_local(std::string{new_target_local});
        fn().locals.back().boxed = true;
        proto().emit(instruction{op::load_new_target, held});
        proto().emit(instruction{op::new_cell, held});
    }
    // Declarations are hoisted to the top of the body before any nested
    // function is compiled. Otherwise a nested function declaration (which
    // hoists, so it compiles first) would resolve the enclosing local it
    // means to capture as a global instead.
    predeclare_locals(n.a);

    // `c` bit 0 = async, bit 1 = generator. `c` defaults to -1, so an
    // unmarked function looks async unless the sign is checked first.
    fn().is_async = n.c > 0 && (n.c & 1) != 0;
    fn().is_generator = n.c > 0 && (n.c & 2) != 0;
    proto().is_generator = fn().is_generator;
    proto().is_async = fn().is_async;

    // A generator's parameters are evaluated by the call, not by the first
    // `.next()`: FunctionDeclarationInstantiation is step 8 of [[Call]]
    // (10.2.1), before the generator object exists, so a default that throws
    // throws at `g()` and `g(null)` with a pattern parameter is a TypeError
    // there (27.5.3.1 / 27.6.3.1 both say `? FunctionDeclarationInstantiation`).
    // The body proper starts at this suspension: make_generator runs the
    // frame up to it, and the first resume lands past it with its argument
    // in the scratch register, discarded as 27.5.3.3 step 5 discards it.
    // Done only when the prologue can be observed (a default or a pattern),
    // so a plain `function* g(a, b)` costs nothing extra to call.
    if (fn().is_generator) {
        bool observable = false;
        for (const std::int32_t p : params) { observable |= at(p).a >= 0 || at(p).b >= 0; }
        if (observable) {
            const std::uint16_t scratch = alloc_reg();
            proto().emit(instruction{op::load_undef, scratch});
            proto().emit(instruction{op::yield_value, scratch, scratch});
            proto().eager_prologue = true;
        }
    }

    // The async fence: one handler around the whole body, so a throw the body
    // does not catch rejects the returned promise instead of throwing at the
    // caller (27.7.5.2, AsyncBlockStart step 3.f). It returns
    // `Promise.reject(e)` through the builtin's hidden global rather than a
    // new opcode, so the ahead-of-time compiler (ctcompile) sees an ordinary
    // call. Async generators are fenced the same way (settle_async_generator
    // reads the rejection back out of the record). `handler_depth_` counts
    // the fence so a loop exit inside the body pops only what it opened.
    //
    // For a non-generator async function the fence was already opened before
    // the parameter prologue, because a throwing default must reject
    // (27.7.5.1 EvaluateAsyncFunctionBody steps 2-3); an async generator's
    // default throws at the call (27.6.3.1 uses `?`). This block opens the
    // fence only when that did not happen.
    if (fenced && fence_guard == no_fence) {
        fence_reg = alloc_reg();
        fence_guard = proto().emit(instruction{op::push_handler, fence_reg});
        ++handler_depth_;
    }

    const std::int32_t body = n.a;
    if (body >= 0 && at(body).kind == vp::nk::block) {
        for (const std::int32_t s : kids(at(body))) {
            if (at(s).kind == vp::nk::func_decl) { compile_stmt(s); }
        }
        compile_statement_list(kids(at(body)), true);
        emit_implicit_return();
    } else if (body >= 0) {
        // A concise arrow body: `x => expr` returns expr.
        const std::uint16_t r = alloc_reg();
        compile_expr(body, r);
        // Not for a generator, even an async one: a generator's `return`
        // becomes the `value` of a `{value, done: true}` record, and any
        // promise is the driver's job (as in TypeScript's __awaiter helper).
        if (fn().is_async && !fn().is_generator) { proto().emit(instruction{op::wrap_promise, r}); }
        proto().emit(instruction{op::ret, r});
    } else {
        emit_implicit_return();
    }
    if (fenced) {
        --handler_depth_;
        patch_here(fence_guard);
        const std::uint16_t callee = alloc_reg();
        proto().emit(instruction::with_bx(op::get_global, callee,
                                          intern_name(std::string{promise_reject_name})));
        const std::uint16_t reason = alloc_reg();
        proto().emit(instruction{op::move, reason, fence_reg});
        proto().emit(instruction{op::call, callee, 1});
        proto().emit(instruction{op::ret, callee});
    }
    finish_frame(index, params.size());
    // `f.length` counts the parameters before the first default or rest
    // (15.1.5 ExpectedArgumentCount).
    std::uint16_t expected = 0;
    for (const std::int32_t p : params) {
        if (at(p).a >= 0 || at(p).d == 1) { break; }
        ++expected;
    }
    out_.functions[index].length = expected;
    if (!fn().eval_vars.empty()) { with_scopes_.pop_back(); }
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
