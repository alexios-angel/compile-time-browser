#pragma once
//===- internal.hpp - Shared internals of the AOT bridge --------*- C++ -*-===//
//
// The AOT bridge is the C ABI, declared by aot_helpers.def and aot.hpp,
// through which compiled JavaScript bodies call back into the script VM. Each
// extern "C" helper is defined in frames.cpp, operators.cpp, properties.cpp or
// calls.cpp and transcribes the "DELEGATES TO" column of its row in
// aot_helpers.def. `context` (vm.hpp) befriends every helper by expanding
// aot_helpers.def, so a helper may touch the VM's private state.
//
// This header holds what those files share: the caller-allocated frame handle,
// the interned-name record and the inline reads in `aot_bridge`.
// Private to lib/Script/aot_bridge/: not installed and in no file set.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/core/containers.hpp>
#include <ctbrowser/script/vm.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ctbrowser::script {

/// The handle a compiled body carries between helpers, sized by
/// CT_AOT_FRAME_BYTES. It is deliberately not a call_frame: the row makes the
/// storage caller-allocated so the layout can change without a compiled body
/// needing to know how big a call_frame is.
struct aot_frame_storage {
    context * ctx = nullptr;
    /// Which frame this is, as an index into `frames_` rather than a pointer:
    /// frames_ is a vector and a nested call may reallocate it, so a pointer
    /// would dangle across the very calls this handle exists to survive.
    /// ct_aot_check also compares it with frames_.size() to detect
    /// CT_AOT_UNWOUND.
    std::size_t frame_index = 0;
    std::size_t register_base = 0;
    std::size_t handler_base = 0;
    /// How many slots the body asked for. Recorded so the span can be checked
    /// rather than assumed: without it `ct_aot_slots` hands back a base and
    /// nothing knows how far it is legal to walk.
    std::uint32_t slot_count = 0;

    /// Where the arriving arguments are, as an index into registers_, and how
    /// many arrived.
    ///
    /// Not a pointer: `argv` is one, and ct_aot_enter's resize of registers_
    /// may reallocate it, as aot_entry.h warns. An index survives that because
    /// registers_ only grows during a call and the caller's window is not
    /// truncated until this frame returns. The values stay rooted either way:
    /// collect() marks all of registers_.
    ///
    /// This frame's own register_base is a different thing: it lies above the
    /// caller's window, which is why a compiled body cannot reach arguments
    /// past its declared parameters without being told where they are.
    std::size_t argv_base = 0;
    std::uint32_t argv_count = 0;
};

// The size in the header, checked against the type it sizes. A compiled body
// allocates CT_AOT_FRAME_BYTES and hands the room over; if this handle ever
// outgrew it, generated code would write past its own stack slot, and nothing
// at the call site could see that.
static_assert(sizeof(aot_frame_storage) <= CT_AOT_FRAME_BYTES,
              "CT_AOT_FRAME_BYTES must hold the frame handle it sizes");
static_assert(alignof(aot_frame_storage) <= alignof(std::max_align_t),
              "and a body's plain byte array must be able to align it");

/// An interned property name that owns its text.
///
/// This is deliberately not `prehashed_name`, which is a non-owning
/// {string_view, hash}: a compiled body's names may come from a memory-mapped
/// image or a string literal in generated C++, neither of which is safe to
/// view forever.
///
/// Records are global and immortal. That is safe here, though not for
/// ct_aot_ic, because a name record holds no heap pointer and is never traced
/// by the GC; an inline cache holds receivers.
///
/// `hash` comes from hash_name and nowhere else: containers.hpp warns that any
/// other hash is "a lookup that silently never matches". That is why the
/// handle is produced at image init and never baked into the image by the
/// compiler, which may be a different build of a different library.
struct aot_name_record {
    std::string text;
    std::size_t hash;
};

/// The reads every row shares, defined in-class so each call inlines. A struct
/// because it is a friend of `context` (vm.hpp): some of them read its private
/// frame stack.
struct aot_bridge {
    static context & ctx_of(aot::ct_aot_ctx * c) { return *reinterpret_cast<context *>(c); }

    /// Whether the compiled body behind `f` is strict mode code. The frame
    /// carries the real proto, so the answer is the interpreter's.
    static bool strict_frame(aot::ct_aot_frame * f) {
        const aot_frame_storage & held = frame_of(f);
        const context & cx = *held.ctx;
        return held.frame_index < cx.frames_.size() &&
               cx.frames_[held.frame_index].proto != nullptr &&
               cx.frames_[held.frame_index].proto->is_strict;
    }
    static aot_frame_storage & frame_of(aot::ct_aot_frame * f) {
        return *reinterpret_cast<aot_frame_storage *>(f);
    }

    /// Returns the call_frame this handle names, or null once an unwind has
    /// shrunk frames_ below the handle's index (the state ct_aot_check reports
    /// as CT_AOT_UNWOUND). Looked up by index for the reason aot_frame_storage
    /// keeps one: a nested call may reallocate frames_. A helper reached in
    /// that window must answer, not dereference.
    static context::call_frame * frame_record(aot::ct_aot_frame * f) {
        aot_frame_storage & held = frame_of(f);
        context & cx = *held.ctx;
        return held.frame_index < cx.frames_.size() ? &cx.frames_[held.frame_index] : nullptr;
    }

    /// Implements ct_aot_check. The row's precedence, in order: unwound first,
    /// because the call_frame is gone and every later test would dereference
    /// it; failed before caught, because the run loop's own head tests failed_
    /// in the same disjunction and leaves regardless of a handler that just
    /// fired; then caught; then ok.
    static std::int32_t check(aot::ct_aot_frame * f) {
        const aot_frame_storage & held = frame_of(f);
        const context & cx = *held.ctx;
        if (cx.frames_.size() <= held.frame_index) {
            return static_cast<std::int32_t>(aot::ct_aot_status::unwound);
        }
        if (cx.failed_) { return static_cast<std::int32_t>(aot::ct_aot_status::failed); }
        // Caught: unwind_to_handler set `ip` to a handler whose address is the
        // body's landing-pad id, which carries CT_AOT_PAD_BIT.
        if ((cx.frames_[held.frame_index].ip & CT_AOT_PAD_BIT) != 0) {
            return static_cast<std::int32_t>(aot::ct_aot_status::caught);
        }
        return static_cast<std::int32_t>(aot::ct_aot_status::ok);
    }
};

} // namespace ctbrowser::script

// The rows are defined where aot.hpp declares them, against script's types.
namespace ctbrowser::aot {
using namespace script;
} // namespace ctbrowser::aot
