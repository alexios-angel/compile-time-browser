#pragma once

#include <stdint.h>

// WHAT A COMPILED FUNCTION LOOKS LIKE, which the ABI table never said.
//
// aot_helpers.def specifies sixty-eight HELPERS - what compiled code may call.
// It does not specify the other direction: what the runtime calls when it
// reaches a function that has been compiled. `ct_aot_make_closure`'s row names
// the gap and leaves it open ("AOT stops at the first nested function unless
// closure_object or function_proto carries a compiled entry point"), so this
// header is the first thing in the project that is ABI without being a row.
//
// IT OBEYS THE ROWS' OWN CONVENTION, which ctbrowser/lib/Script/aot_contract.cpp
// enforces over all sixty-eight of them: a status in the return register, data
// through an out-pointer, nothing returned by value. An entry is not a row and
// nothing would have caught it drifting, which is exactly why it should not.
//
// C, NOT C++, and <stdint.h> alone: the LLVM dialect backend emits declarations
// against these symbols, and the engine must still build with no LLVM at all.

// HOW MUCH ROOM A COMPILED BODY GIVES ITS FRAME HANDLE.
//
// aot_helpers.def cites this name and never defined it, and aot.hpp said so
// deliberately: it is a layout decision, and nothing had measured one. Now
// something has. `ct_aot_enter`'s handle needs a context pointer and three
// indices - the frame's position in `frames_`, its register base and its
// handler base - which is 32 bytes on this target, and the row's whole reason
// for making the storage CALLER-allocated is that the layout stays Phase 4's to
// change. 64 is that with room to double, and it is asserted against the real
// type in lib/Script/aot_bridge.cpp rather than trusted.
//
// It is a macro rather than a constant because generated C must size an array
// with it before it has seen a single C++ declaration.
#define CT_AOT_FRAME_BYTES 64

// WHICH BIT OF call_frame::ip MARKS A LANDED CATCH, and the measurement that
// picks it. Phase 6.
//
// aot.hpp records this as a gap on purpose: "inventing either here would freeze
// a choice with no measurement behind it into a header two backends will read".
// The measurement is now available and it is arithmetic rather than a
// benchmark. `ip` is a std::size_t index into a function's bytecode; the image
// writer refuses a program long before 2^32 instructions, and 2^63 of them
// would be 74 exabytes of eight-byte instructions. The top bit cannot collide
// with a real ip, and it does not narrow the field a real ip may use.
//
// It marks an AOT frame whose handler has just fired: unwind_to_handler assigns
// `ip` from `handler::address`, which for a compiled frame is the body's own
// PAD ID with this bit set - so the same four steps that resume the interpreter
// at a catch block hand a compiled body its landing pad, with no change to the
// unwinder at all.
#define CT_AOT_PAD_BIT (((uint64_t)1) << 63)

namespace ctbrowser::aot {

extern "C" {

// The five opaque handles the ABI passes by pointer. Declared HERE rather than
// in aot.hpp so that a consumer needing only the entry signature - a generated
// translation unit, say - does not have to expand a 1,881-line table to get it.
//
// Two of the five have their identity fixed by this rung, and stating it is the
// point: `ct_aot_ctx` IS a `script::context`, and at an entry `ct_aot_site` IS
// the `script::function_proto` being called. The other three stay genuinely
// opaque - `ct_aot_frame`'s layout is Phase 4's, and `ct_aot_ic` and
// `ct_aot_name` have no bodies yet.
struct ct_aot_ctx;
struct ct_aot_frame;
struct ct_aot_site;
struct ct_aot_ic;
struct ct_aot_name;

// A COMPILED BODY.
//
//   ctx           the context to act on. It comes first because a body needs
//                 one BEFORE it has a frame: its first act is ct_aot_enter,
//                 and the frame handle is that call's RESULT.
//   site          this function's identity. ct_aot_enter's row requires the
//                 frame to carry "the image's real function_proto" - without
//                 it current_stack skips the frame (vm.hpp:429-432) and every
//                 compiled frame vanishes from a stack trace - and the row's
//                 own parameters give it nowhere else to come from.
//   argv, argc    the callee's register window, already filled by the caller.
//                 VALID ONLY UNTIL ct_aot_enter: that helper resizes
//                 `registers_` and may reallocate it, so a body copies what it
//                 needs first. argc is what ARRIVED, not param_count - a rest
//                 parameter is the reason call_frame keeps them apart.
//   receiver      `this`, and `constructing`, both by value because
//                 ct_aot_return_value takes them and takes NO frame handle
//                 (aot_helpers.def, ct_aot_return_value): a body must hold them
//                 in its own locals, so the entry has to deliver them.
//   out           where the returned value goes, when the status is ok.
//
// Returns a ct_aot_status. On anything but `ok` the runtime writes no result
// and re-derives its own state, exactly as the interpreter does after a throw.
typedef int32_t (*ct_aot_entry_fn)(struct ct_aot_ctx * ctx, const struct ct_aot_site * site,
                                   const uint64_t * argv, uint32_t argc, uint64_t receiver,
                                   uint32_t constructing, uint64_t * out);

} // extern "C"

} // namespace ctbrowser::aot
