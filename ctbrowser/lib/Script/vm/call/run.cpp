// ctbrowser.script context - running a program: the top-level entry, and the
// re-entrant one a dynamic import needs.
//
// One of five files carved out of a 1,171-line vm/call.cpp on 2026-09-08 -
// which was itself one of four carved out of a 3,232-line vm.cpp on
// 2026-08-09. All members of `context`, declared in
// include/ctbrowser/script/vm.hpp - so they split across translation units
// with nothing to declare.

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/number_format.hpp>
#include <ctbrowser/script/vm.hpp>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// The VM's implementation.
//
// `run_loop` alone is 15 KB of object code - the whole instruction dispatch -
// and while it lived in the interface every translation unit that imported the
// module emitted its own copy and optimised it again. The class declaration
// stays in :vm; the bodies live here and are compiled once.

namespace ctbrowser::script {

// RUNNING A PROGRAM WHILE ONE IS ALREADY RUNNING, which `run` cannot do and
// must not pretend to: `execute` CLEARS `frames_` and reassigns `registers_`,
// because it is the entry point for a whole turn. A dynamic import is the first
// thing that ever needed a program evaluated from inside the interpreter, and
// calling `run` there wiped the importing module's own frame - so the module
// stopped dead at the `import(...)` and every statement after it silently never
// ran. Nothing threw; the loop simply found no frames left and finished.
//
// This is `call`'s shape rather than `run`'s: push a frame on top of what is
// already there and run down to the depth it started at.
run_result context::run_reentrant(const program & prog) {
    run_result result;
    if (!prog.ok) {
        result.ok = false;
        result.error = prog.error;
        return result;
    }
    const function_proto & entry = prog.functions[0];
    const value receiver = prog.kind == script_kind::classic ? global_this_ : value::undefined();
    const std::size_t new_base = registers_.size();
    registers_.resize(new_base + entry.frame_size + 8u, value::undefined());
    const std::size_t depth = frames_.size();
    // THE PROGRAM POINTER IS SAVED AND RESTORED. `call` reads it to decide
    // whether a closure can be entered at all, and leaving it pointing at the
    // imported module would make every later call in the IMPORTING one look up
    // its function protos in the wrong program.
    const program * const outer_program = program_;
    program_ = &prog;
    // AND THE SAME QUESTION FOR A MODULE EVALUATING INSIDE ITS IMPORTER. This
    // is the third place that entered a program's top level by pushing a frame
    // of its own; leaving it out would mean an imported module's compiled body
    // was interpreted purely because of who imported it.
    if (value produced = value::undefined();
        enter_compiled(*this, entry, value::undefined(), registers_.data() + new_base, new_base, 0u,
                       receiver, /*constructing*/ false, produced)) {
        program_ = outer_program;
        if (registers_.size() >= new_base) { registers_.resize(new_base); }
        return result;
    }
    frames_.push_back(call_frame{&entry, 0, new_base, 0, 0, nullptr, receiver, handlers_.size()});
    (void)run_loop(depth);
    program_ = outer_program;
    if (registers_.size() >= new_base) { registers_.resize(new_base); }
    // NO drain_microtasks HERE. The checkpoint belongs to the end of a turn,
    // and this is the middle of one - draining now would run the importer's own
    // pending handlers before its next statement.
    result.ok = !failed_;
    result.error = error_;
    return result;
}

run_result context::run(const program & prog) {
    run_result result;
    if (!prog.ok) {
        result.ok = false;
        result.error = prog.error;
        return result;
    }
    failed_ = false;
    error_.clear();
    result.returned = execute(prog, prog.functions[0]);
    // THE END OF THE TURN. A script's promise handlers run after its last
    // statement, not between two of them - so the checkpoint is here, once the
    // top level has finished.
    drain_microtasks();
    // AND A FAILED SCRIPT'S QUEUE DIES WITH IT. `drain_microtasks` stops on
    // `failed_`, so a script that threw left its already-queued jobs in the
    // queue - and once a page's classic scripts became separate programs, the
    // NEXT script's checkpoint ran them, after that script's synchronous code.
    // A handler belonging to a script that is over should not surface in the
    // middle of the following one.
    //
    // CHROME WOULD RUN THEM, and that difference is real: an uncaught throw
    // does not cancel a microtask checkpoint there. It is not matched here
    // because `raise` and an uncaught throw set the same `failed_` - the
    // call-stack ceiling and the allocation ceiling look exactly like a page
    // exception from this function - and draining after a resource ceiling
    // would run more JavaScript precisely where the ceiling exists to stop it.
    // Telling the two apart is the fix; dropping the queue is the safe half of
    // it, and this comment is the record of which half was taken.
    if (failed_) { microtasks_.clear(); }
    result.ok = !failed_;
    result.error = error_;
    return result;
}

value context::execute(const program & prog, const function_proto & entry) {
    registers_.assign(entry.frame_size + 8u, value::undefined());
    frames_.clear();
    // AND THE HANDLER STACK, WHICH FRAMES_.CLEAR() DOES NOT IMPLY. A VM-level
    // `raise` - the call-stack ceiling, the allocation ceiling - returns out of
    // run_loop WITHOUT unwinding, because the loop's condition is
    // `frames_.size() > stop_depth && !failed_` and it simply stops. Any `try`
    // that was live at that moment stays on `handlers_` recording frame 0.
    //
    // A SECOND TOP-LEVEL PROGRAM ON THIS CONTEXT THEN HAS A FRAME 0 TOO, so
    // `unwind_to_handler` accepts the dead program's handler: it only rejects
    // one whose frame has returned. It writes the thrown value into the wrong
    // frame's register and sets `ip` to an ADDRESS OUT OF THE OTHER PROGRAM'S
    // BYTECODE, and the interpreter carries on from there. Measured: a page
    // whose first script exhausts the stack inside a `try` and whose second
    // alerts 1..8 then throws produced 1,2,3,4,5,6,7,8,3,4,5,6,7,8 - the throw
    // swallowed, six statements run twice - and padding the first script moved
    // where the second one resumed.
    //
    // It became reachable when a page's classic scripts stopped being one
    // program: before that, a raise in the first script ended the only top
    // level and nothing else ran on the dirtied context.
    handlers_.clear();
    thrown_ = value::undefined();
    program_ = &prog;
    // Script this belongs to the realm, including in a strict classic script.
    // Modules have no top-level receiver. Never reload the writable globalThis
    // binding here: an earlier script may have replaced or cleared it.
    const value receiver = prog.kind == script_kind::classic ? global_this_ : value::undefined();
    // A COMPILED TOP LEVEL, IF THIS PROGRAM HAS ONE, and for ctcompile that is
    // the ordinary case rather than an exotic one: a page's `<script>` IS a top
    // level, so a backend that compiles anything compiles this. Asked after the
    // reset above, so a compiled body enters a context in the state it expects,
    // and before the frame push, because ct_aot_enter pushes its own.
    if (value produced = value::undefined();
        enter_compiled(*this, entry, value::undefined(), registers_.data(), 0u, 0u, receiver,
                       /*constructing*/ false, produced)) {
        return produced;
    }
    frames_.push_back(call_frame{&entry, 0, 0, 0, 0, nullptr, receiver, 0});
    // Per-frame string interning: a literal in a loop should allocate once,
    // not once per iteration.
    string_cache_.clear();
    bigint_cache_.clear();
    // A TOP-LEVEL PROGRAM IS A C++ ENTRY LIKE ANY OTHER. Marking it is what
    // makes the transition INSIDE it attributable: without this, `executing_`
    // is still `cxx` for the whole run, so an interpreted script calling a
    // compiled function would be counted as C++ reaching it directly. The
    // interpreter is what is running; say so.
    note_transition_into_vm(*this);
    const executing_as running{*this, executing_kind::vm};
    return run_loop(0);
}

} // namespace ctbrowser::script
