//===- aot_entry.h - AOT entry signature ------------------------*- C++ -*-===//
//
// What the runtime calls when it reaches a compiled function. aot_helpers.def
// (with the tables in helpers/*.def) lists the helpers compiled code may call;
// it does not specify this other direction, so this header defines it.
//
// The entry follows the convention lib/Script/aot_contract.cpp enforces for
// every helper: a status in the return register, data through an out-pointer,
// nothing returned by value.
//
// The declarations are C, using <stdint.h> alone: the LLVM-dialect backend
// emits declarations against these symbols, and the engine must still build
// with no LLVM at all.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

/// Bytes a compiled body reserves for the frame handle ct_aot_enter fills in.
///
/// The handle needs a context pointer and three indices (the frame's position
/// in `frames_`, its register base and its handler base), 32 bytes on 64-bit
/// targets. The storage is caller-allocated so the handle's layout can change
/// without an ABI break; 64 leaves room to double, and
/// lib/Script/aot_bridge/internal.hpp static_asserts that the real type fits.
/// It is a macro rather than a constant because generated C sizes an array with
/// it before seeing any C++.
#define CT_AOT_FRAME_BYTES 64

/// The bit of call_frame::ip that marks a compiled frame whose catch handler
/// has just fired.
///
/// `ip` is a std::size_t bytecode index and the image writer refuses programs
/// far below 2^63 instructions, so the top bit can never collide with a real
/// ip. unwind_to_handler assigns `ip` from `handler::address`, which for a
/// compiled frame is the body's landing-pad id with this bit set, so the
/// unwinder needs no change to resume a compiled body at its landing pad.
#define CT_AOT_PAD_BIT (((uint64_t)1) << 63)

namespace ctbrowser::aot {

extern "C" {

// The five opaque handles the ABI passes by pointer, declared here so a
// generated translation unit that only needs the entry signature does not
// have to include the full helper table.
//
// Two have a fixed identity: `ct_aot_ctx` is a `script::context`, and at an
// entry `ct_aot_site` is the `script::function_proto` being called.
// `ct_aot_frame`'s layout is private to the runtime (see CT_AOT_FRAME_BYTES),
// and `ct_aot_ic` and `ct_aot_name` have no definitions yet.
struct ct_aot_ctx;
struct ct_aot_frame;
struct ct_aot_site;
struct ct_aot_ic;
struct ct_aot_name;

/// The signature of a compiled function body.
///
///   ctx           the context to act on. It comes first because a body needs
///                 one before it has a frame: its first act is ct_aot_enter,
///                 and the frame handle is that call's result.
///   site          this function's identity. ct_aot_enter needs it to give
///                 the frame the image's real function_proto; without it
///                 context::current_stack (script/vm/execution_inline.hpp)
///                 skips the frame and compiled frames vanish from stack traces.
///   argv, argc    the callee's register window, already filled by the caller.
///                 Valid only until ct_aot_enter: that helper resizes
///                 `registers_` and may reallocate it, so a body copies what it
///                 needs first. argc is the number of arguments that arrived,
///                 not param_count; a rest parameter is why call_frame keeps
///                 them apart.
///   receiver      `this`, and `constructing`, both by value because
///                 ct_aot_return_value takes them and takes no frame handle,
///                 so a body must hold them in its own locals and the entry
///                 has to deliver them.
///   out           where the returned value goes, when the status is ok.
///
/// Returns a ct_aot_status. On anything but `ok` the runtime writes no result
/// and re-derives its own state, exactly as the interpreter does after a throw.
typedef int32_t (*ct_aot_entry_fn)(struct ct_aot_ctx * ctx, const struct ct_aot_site * site,
                                   const uint64_t * argv, uint32_t argc, uint64_t receiver,
                                   uint32_t constructing, uint64_t * out);

} // extern "C"

} // namespace ctbrowser::aot
