#pragma once
#include <ctbrowser/aot/aot_entry.h>
#include <ctbrowser/script/bytecode.hpp>
#include <ctbrowser/script/value.hpp>

#include <cstdint>

// MIXED-MODE DISPATCH: the one place that decides interpreted or native.
//
// A function may have a compiled body (`function_proto::aot_entry`, set by the
// AOT backend) or only bytecode. Something has to choose, and the whole point
// of Phase 3 is that it is ONE something. Before this header there were three
// places that entered a function and only one of them looked at `aot_entry`:
// the interpreter's `op::call`. So a compiled body was reached from interpreted
// JavaScript and from nowhere else -
//
//   * `context::call` pushed a frame and ran the loop, so EVERY C++ entry into
//     JavaScript - a DOM event, a timer, a promise job, a rAF callback,
//     `Function.prototype.apply`, a getter, a class field initialiser - could
//     not reach a compiled body. Nor could one compiled body call another,
//     because the helper for that goes through `context::call` too.
//   * `op::construct` pushed a frame of its own, so `new C()` could not reach
//     one either, and would not have passed `constructing` if it had.
//
// None of that FAILS. It runs the interpreter and returns the right answer, so
// it presents as a performance cliff under one specific callback rather than as
// a bug - which is exactly what the master plan warns about and why it makes
// centralising dispatch a phase of its own rather than a detail of the backend.
namespace ctbrowser::script {

class context;

// WHAT IS CURRENTLY RUNNING, which is the half of a transition that cannot be
// read off the call site. `enter_compiled` is called from the interpreter and
// from C++ alike; only the context knows whether the code doing the calling was
// itself a compiled body.
enum class executing_kind : std::uint8_t {
    cxx = 0, // C++: the embedder, a native built-in, or nothing yet
    vm = 1,  // the interpreter
    aot = 2, // a compiled body
};

// The six transitions the plan requires, in the order it lists them. Every one
// is counted because an untested transition is where mixed-mode bugs live: the
// counters are what let a test assert that all six were actually exercised
// rather than that a suite which never crossed one of them passed.
enum class transition : std::uint8_t {
    cxx_to_vm = 0,
    cxx_to_aot = 1,
    vm_to_aot = 2,
    aot_to_vm = 3,
    aot_to_aot = 4,
    aot_to_cxx = 5,
    count = 6,
};

// How many of each have happened. Not behind NDEBUG, and the reason is
// measurement rather than taste: every one of these counters sits at a boundary
// that already costs a vector resize, a receiver save or an indirect call
// through the ABI. None is in the interpreter's inner loop - `op::call` to an
// interpreted function does not pass through here at all - so the honest cost
// is one increment per crossing, and a counter that only exists in a build the
// suite does not run is a counter that proves nothing.
[[nodiscard]] std::uint64_t transitions(transition which) noexcept;
void reset_transitions() noexcept;
[[nodiscard]] const char * transition_name(transition which) noexcept;

// ENTER A COMPILED BODY, or report that there is not one.
//
// Returns false when `target` has no compiled body, and the caller carries on
// interpreting - which is what makes this safe to put in front of every entry
// rather than only where a compiled body is expected.
//
// `argv` must already be the callee's window with missing parameters filled in
// with undefined, because that is what the ABI's entry row promises and a
// compiled body reads it directly. It must also be read before anything that
// can resize the register file; `ct_aot_enter` may, which is why the row says
// so and why both call sites pass a window they have just finished filling.
[[nodiscard]] bool enter_compiled_body(context & ctx, const function_proto & target, value closure,
                                       const value * argv, std::size_t argv_base,
                                       std::uint32_t argc, value receiver, bool constructing,
                                       value & out);

// THE TEST IS INLINE AND THE WORK IS NOT, which is the whole reason this is
// two functions.
//
// `op::call` runs this on EVERY interpreted call, and almost every answer is
// no - the compiler does not set `aot_entry` yet and no image carries it. Left
// entirely out of line it would be a real function call per JavaScript call,
// which is a cost the interpreter pays forever to buy a tidier diagram. Inline,
// it is one predictable not-taken branch on a field in the same eight-byte word
// as `param_count`, `frame_size` and `is_generator` - all three of which the
// call handler has already loaded.
//
// It is still ONE decision: this is the only place that reads `aot_entry`, and
// every entry into a function asks it.
//
// `closure` IS THE CALLEE ITSELF, and it is a parameter because the ABI has
// nowhere to put it: ct_aot_entry_fn delivers `site`, the function_proto, and
// upvalues live on the closure INSTANCE - two closures over one function share
// a proto and capture different things. A top-level entry has none and passes
// undefined, which is not the same as a closure with no upvalues.
//
// `argv_base` IS argv's INDEX INTO registers_, passed rather than derived.
// Subtracting two pointers would be undefined behaviour the moment argv pointed
// anywhere else, and this signature only promises the window is FILLED, not
// where it lives - so every caller states the number it already has.
[[nodiscard]] inline bool enter_compiled(context & ctx, const function_proto & target,
                                         value closure, const value * argv, std::size_t argv_base,
                                         std::uint32_t argc, value receiver, bool constructing,
                                         value & out) {
    if (target.aot_entry == nullptr) { return false; }
    return enter_compiled_body(ctx, target, closure, argv, argv_base, argc, receiver, constructing,
                               out);
}

// Counts a crossing into interpreted or native C++ code. `enter_compiled`
// counts its own; these two are for the paths that do not go through it.
void note_transition_into_vm(const context & ctx) noexcept;
void note_transition_into_cxx(const context & ctx) noexcept;

// Sets `executing_` for the duration of a body and puts it back afterwards, so
// the next transition is attributed to whatever is really running. By value
// rather than a stack: the only thing anybody asks is what the INNERMOST body
// is, and that is one byte saved on the C++ stack of the entry that changed it.
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
