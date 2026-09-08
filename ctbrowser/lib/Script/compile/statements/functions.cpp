// compiler_impl - statements: a function declaration, and the function body
// that holds every other statement.
//
// One of four files carved out of a 1,017-line compile/statements.cpp on
// 2026-09-08 - which was itself one of the files carved out of a 3,845-line
// compile.cpp on 2026-08-09. All are members of compiler_impl, declared whole
// in compiler_impl.hpp beside the directory; nothing about that header changed.

#include "../compiler_impl.hpp"

namespace ctbrowser::script::detail {

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
