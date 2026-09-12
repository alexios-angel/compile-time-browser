// compiler_impl - statements: a function declaration, and the function body
// that holds every other statement.
//
// One of four files carved out of a 1,017-line compile/statements.cpp on
// 2026-09-08 - which was itself one of the files carved out of a 3,845-line
// compile.cpp on 2026-08-09. All are members of compiler_impl, declared whole
// in compiler_impl.hpp beside the directory; nothing about that header changed.

#include "../compiler_impl.hpp"

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
    proto().emit(instruction{op::ret_undef}); // [[Construct]] hands back `this`
    patch_here(defined);
    const std::uint16_t kind = alloc_reg();
    proto().emit(instruction{op::type_of, kind, value});
    const std::uint16_t want = alloc_reg();
    const std::uint16_t same = alloc_reg();
    emit_string(want, "object");
    proto().emit(instruction{op::equal, same, kind, want});
    const std::size_t is_object = proto().emit(instruction{op::jump_if_true, same});
    emit_string(want, "function");
    proto().emit(instruction{op::equal, same, kind, want});
    const std::size_t is_function = proto().emit(instruction{op::jump_if_true, same});
    emit_throw("TypeError", "Derived constructors may only return object or undefined");
    patch_here(is_object);
    patch_here(is_function);
    proto().emit(instruction{op::ret, value});
    release_to(mark);
}

void compiler_impl::emit_implicit_return() {
    if (!fn().derived_flag.empty()) { emit_super_check(fn().derived_flag, false); }
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

    // STRICTNESS IS INHERITED, and a class body or a directive of its own
    // adds it (11.2.2). Decided before the frame is pushed, off the enclosing
    // one.
    const bool strict =
        fn().is_strict || class_body_depth_ > 0 ||
        (n.a >= 0 && at(n.a).kind == vp::nk::block && has_use_strict_directive(n.a));
    frames_.emplace_back();
    frames_.back().proto = index;
    frames_.back().is_strict = strict;
    out_.functions[index].is_strict = strict;
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
    // A captured PARAMETER needs boxing too, and it arrives already holding
    // its value - so box in place, after the arguments land and BEFORE the
    // defaults are evaluated: a default expression reads an earlier
    // parameter through its cell (`function f(cls, p = cls.name)` with `cls`
    // captured by an arrow in the body - zod's _instanceof), and boxing after
    // the prologue meant that read went through cell_get on a raw value and
    // answered undefined. The prologue writes a boxed parameter's default
    // through cell_set for the same reason.
    // See the fence below: an async (non-generator) function's fence covers
    // its parameter defaults too, so it is pushed here, before them.
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
    compile_parameter_prologue(params);
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
    // A DERIVED CONSTRUCTOR'S `super()` FLAG - see frame::derived_flag. Boxed
    // whatever the capture index says, because an arrow in the body reads
    // and writes it as an upvalue.
    if (derived_ctor_pending_) {
        derived_ctor_pending_ = false;
        fn().derived_flag = "@super:" + std::to_string(index);
        const std::uint16_t flag = declare_local(fn().derived_flag);
        fn().locals.back().boxed = true;
        proto().emit(instruction{op::load_false, flag});
        proto().emit(instruction{op::new_cell, flag});
    }
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
    proto().is_async = fn().is_async;

    // A GENERATOR'S PARAMETERS ARE EVALUATED BY THE CALL, not by the first
    // `.next()`: FunctionDeclarationInstantiation is step 8 of [[Call]]
    // (10.2.1), before the generator object exists, so a default that throws
    // throws at `g()` and `g(null)` with a pattern parameter is a TypeError
    // there (27.5.3.1 / 27.6.3.1 both say `? FunctionDeclarationInstantiation`).
    // The body proper starts at this suspension: make_generator runs the
    // frame up to it, and the first resume lands past it with its argument
    // in the scratch register - discarded, as 27.5.3.3 step 5 discards it.
    // Only when the prologue can be observed - a default or a pattern - so a
    // plain `function* g(a, b)` still costs nothing extra to call.
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

    // THE ASYNC FENCE. An async function never throws at its caller: a throw
    // its body does not catch REJECTS the promise it returned (27.7.5.2,
    // AsyncBlockStart step 3.f). Before this, `async function f() { throw e }`
    // threw e synchronously out of `f()`, and after a real suspension the
    // throw reached nothing at all and was an engine fault. One handler round
    // the whole body, landing on a return of `Promise.reject(e)` - through the
    // builtin's hidden global rather than a new opcode, so the compiled tier
    // sees a call it already knows how to make. An async GENERATOR is fenced
    // the same way; settle_async_generator reads the rejection back out of
    // the record. `handler_depth_` counts it so a loop exit inside the body
    // pops only what it opened.
    // THE FENCE OPENS BEFORE THE PARAMETER PROLOGUE for an async function
    // that is not a generator: a default that throws REJECTS the promise
    // (27.7.5.1 EvaluateAsyncFunctionBody step 2-3), where an async
    // generator's throws at the call (27.6.3.1 uses `?`). So the push may
    // already have happened above; this block only opens it for the rest.
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
