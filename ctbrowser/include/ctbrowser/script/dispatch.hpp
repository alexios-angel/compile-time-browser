//===- dispatch.hpp - Mixed-mode dispatch -----------------------*- C++ -*-===//
//
// Mixed-mode dispatch: the one place that decides whether a call runs
// bytecode or a compiled (AOT) body.
//
// A function may have a compiled body (`function_proto::aot_entry`) or only
// bytecode. Every way into a function must ask here: the interpreter's
// `op::call` and `op::construct`, and every C++ entry through `context::call`
// (DOM events, timers, promise jobs, rAF callbacks, getters,
// `Function.prototype.apply`, and one compiled body calling another). A path
// that skips the check still returns the right answer by interpreting, so the
// bug shows up only as a silent slowdown, which is why the decision lives in
// exactly one place.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <ctbrowser/aot/aot_entry.h>
#include <ctbrowser/script/bytecode.hpp>
#include <ctbrowser/script/value.hpp>

#include <cstdint>

namespace ctbrowser::script {

class context;

/// What is currently running: the half of a transition that cannot be read off
/// the call site. `enter_compiled` is called from the interpreter and from C++
/// alike; only the context knows whether the code doing the calling was itself
/// a compiled body.
enum class executing_kind : std::uint8_t {
    cxx = 0, // C++: the embedder, a native built-in, or nothing yet
    vm = 1,  // the interpreter
    aot = 2, // a compiled body
};

/// The six possible crossings between C++, the interpreter (vm) and compiled
/// bodies (aot). Each is counted so a test can assert that every crossing was
/// actually exercised, not just that a suite which never crossed one passed.
enum class transition : std::uint8_t {
    cxx_to_vm = 0,
    cxx_to_aot = 1,
    vm_to_aot = 2,
    aot_to_vm = 3,
    aot_to_aot = 4,
    aot_to_cxx = 5,
    count = 6,
};

/// How many crossings of `which` have happened. Not behind NDEBUG: every
/// counter sits at a boundary that already costs a vector resize, a receiver
/// save or an indirect call through the ABI, and none is in the interpreter's
/// inner loop (`op::call` to an interpreted function does not pass through
/// here at all). So the cost is one increment per crossing, and a counter that
/// only exists in a build the suite does not run would prove nothing.
[[nodiscard]] std::uint64_t transitions(transition which) noexcept;
/// Zeroes every transition counter.
void reset_transitions() noexcept;
/// The enumerator's name, such as "vm_to_aot", for test output.
[[nodiscard]] const char * transition_name(transition which) noexcept;

/// Enters `target`'s compiled body, which must exist; the out-of-line half of
/// enter_compiled. Returns true, because the body ran; `out` is written only
/// when it returned normally, and otherwise the caller sees the exception
/// state it would have seen after an interpreted throw.
///
/// `argv` must already be the callee's window with missing parameters filled in
/// with undefined, because that is what the ABI's entry signature promises and
/// a compiled body reads it directly. It must also be read before anything that
/// can resize the register file; `ct_aot_enter` may, which is why both call
/// sites pass a window they have just finished filling.
[[nodiscard]] bool enter_compiled_body(context & ctx, const function_proto & target, value closure,
                                       const value * argv, std::size_t argv_base,
                                       std::uint32_t argc, value receiver, bool constructing,
                                       value & out);

/// Enters `target`'s compiled body, or returns false when it has none and the
/// caller should carry on interpreting. That makes it safe to put in front of
/// every entry rather than only where a compiled body is expected.
///
/// The test is inline and the work (enter_compiled_body) is not. `op::call`
/// runs this on every interpreted call, and almost every answer is no: only an
/// application built by ctcompile installs compiled bodies, and an ordinary
/// page has none. Out of line it would be a real function call per JavaScript
/// call; inline it is one predictable not-taken branch.
///
/// `closure` is the callee itself. It is a parameter because the ABI has
/// nowhere to put it: ct_aot_entry_fn delivers `site`, the function_proto, and
/// upvalues live on the closure instance (two closures over one function share
/// a proto and capture different things). A top-level entry has none and
/// passes undefined, which is not the same as a closure with no upvalues.
///
/// `argv_base` is argv's index into `registers_`, passed rather than derived.
/// Subtracting two pointers would be undefined behaviour if argv pointed
/// anywhere else, and this signature only promises the window is filled, not
/// where it lives, so every caller passes the index it already has.
[[nodiscard]] inline bool enter_compiled(context & ctx, const function_proto & target,
                                         value closure, const value * argv, std::size_t argv_base,
                                         std::uint32_t argc, value receiver, bool constructing,
                                         value & out) {
    if (target.aot_entry == nullptr) { return false; }
    return enter_compiled_body(ctx, target, closure, argv, argv_base, argc, receiver, constructing,
                               out);
}

/// Counts a crossing into interpreted or native C++ code. `enter_compiled`
/// counts its own; these two are for the paths that do not go through it.
void note_transition_into_vm(const context & ctx) noexcept;
void note_transition_into_cxx(const context & ctx) noexcept;

/// Sets the context's `executing_` for the duration of a body and puts it back
/// afterwards, so the next transition is attributed to whatever is really
/// running. The previous kind is saved by value rather than on a separate
/// stack: the only question asked is what the innermost body is, and the C++
/// stack of the entry that changed it already nests correctly.
class executing_as {
public:
    executing_as(context & ctx, executing_kind now) noexcept;
    ~executing_as() noexcept;
    executing_as(const executing_as &) = delete;
    executing_as & operator=(const executing_as &) = delete;

private:
    context * ctx_ = nullptr;
    executing_kind saved_ = executing_kind::cxx;
};

} // namespace ctbrowser::script
