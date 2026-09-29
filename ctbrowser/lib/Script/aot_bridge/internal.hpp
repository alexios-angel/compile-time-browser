#pragma once
// THE FIRST EXECUTABLE LINES OF THE AOT ABI.
//
// aot_helpers.def has specified sixty-eight helpers since Phase 2 and not one
// of them had a body. Phase 2's gate, from the master plan, is "VM code calls a
// hand-authored AOT closure through the real runtime ABI" - and until this file
// nothing had ever executed a line of that contract. A table nobody runs is
// prose, however good; the cheapest way to find out whether 1,881 lines are
// right is to make a few of them run before sixty-eight bodies and two code
// generators are written against them.
//
// EIGHT ROWS. Four of them are what a non-throwing call needs - ct_aot_enter,
// ct_aot_leave, ct_aot_check and ct_aot_return_value - and the fifth,
// ct_aot_call, is what Phase 3 needs: without it a compiled body cannot call
// anything, so three of the six transitions the plan requires could only be
// demonstrated by a test reaching around the ABI it is supposed to be testing.
// The sixth, ct_aot_slots, is what Phase 4 needs: a body had a register span
// reserved for it and no way to address it, so the only place it could keep a
// live value was a C++ local - which the collector does not trace. The seventh,
// ct_aot_binary_op_static, is Phase 5's first extracted SEMANTIC helper - the
// first row here that computes a JavaScript answer rather than managing a
// frame, and it computes it by calling the very function the interpreter calls.
// ct_aot_binary_op is the eighth and its re-entering twin: the seven operations
// that can run a page's own valueOf and toString, and therefore the first rows
// whose non-ok statuses are actually reachable.
//
// Each body is a transcription of its row's DELEGATES TO column, and where a
// row could not be satisfied that is recorded rather than worked around - see
// ct_aot_catch_land in aot_helpers.def, which this rung found unimplementable
// as written and did not implement.
//
// WHAT IS NOT HERE, deliberately: the throwing tier. ct_aot_handler_push,
// ct_aot_throw and ct_aot_catch_land are the rows a compiled `try` needs, and
// one of them cannot be written against the runtime as it stands. Executing
// three rows and reporting the fourth as broken is worth more than four bodies
// one of which quietly does something other than its row says.
//
// SPLIT ON 2026-09-08, and the bodies moved INTO the rows on 2026-09-29. This
// file was aot_bridge.cpp, 1,586 lines, with every body a member of
// `struct aot_bridge` and every extern "C" row a one-line forward to it. Each
// row now holds its own body in frames.cpp, operators.cpp, properties.cpp or
// calls.cpp: `context` (vm.hpp) befriends every row by expanding
// aot_helpers.def, so a row may touch the private state its body needs.
// Private to lib/Script/aot_bridge/: NOT installed and in no file set.

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

// The handle a compiled body carries between helpers. It is what
// CT_AOT_FRAME_BYTES sizes, and it is deliberately NOT a call_frame: the row
// says the storage is caller-allocated so that the layout stays changeable, and
// a compiled body must not need to know how big a call_frame is.
struct aot_frame_storage {
    context * ctx = nullptr;
    // WHICH FRAME THIS IS, as an INDEX rather than a pointer. `frames_` is a
    // vector and a nested call reallocates it, so a pointer would dangle across
    // the very calls this handle exists to survive. It is also what ct_aot_check
    // compares against: "frames_.size() < fr depth => CT_AOT_UNWOUND".
    std::size_t frame_index = 0;
    std::size_t register_base = 0;
    std::size_t handler_base = 0;
    // HOW MANY SLOTS THE BODY ASKED FOR. Recorded so the span can be checked
    // rather than assumed: without it `ct_aot_slots` hands back a base and
    // nothing anywhere knows how far it is legal to walk.
    std::uint32_t slot_count = 0;

    // WHERE THE ARRIVING ARGUMENTS ARE, AS AN INDEX INTO registers_, and how
    // many arrived.
    //
    // NOT A POINTER. `argv` is one, and ct_aot_enter's resize of registers_ may
    // reallocate it - which aot_entry.h warns about. An index survives that,
    // because registers_ only grows during a call and the caller's window is
    // not truncated until this frame returns. The VALUES stay rooted either
    // way: collect() marks registers_ in full.
    //
    // THIS FRAME'S OWN register_base IS NOT IT. That is above the caller's
    // window, which is exactly why a compiled body cannot reach the arguments
    // past its declared parameters without being told where they are.
    std::size_t argv_base = 0;
    std::uint32_t argv_count = 0;
};

// THE NUMBER IN THE HEADER, CHECKED AGAINST THE TYPE IT SIZES. A compiled body
// allocates CT_AOT_FRAME_BYTES and hands the room over; if this handle ever
// outgrows it, generated code would be writing past its own stack slot, and
// nothing at the call site could see that.
static_assert(sizeof(aot_frame_storage) <= CT_AOT_FRAME_BYTES,
              "CT_AOT_FRAME_BYTES must hold the frame handle it sizes");
static_assert(alignof(aot_frame_storage) <= alignof(std::max_align_t),
              "and a body's plain byte array must be able to align it");

// AN INTERNED PROPERTY NAME, and the record OWNS its text.
//
// The row is emphatic that this is NOT `prehashed_name`, which is
// {string_view, hash} - a NON-OWNING view, so a pool built out of one dangles
// the moment whoever supplied the characters goes away. A compiled body's names
// come from an image that may be a memory-mapped file or a string literal in
// generated C++; neither is something to hold a view into forever.
//
// GLOBAL AND IMMORTAL, which the row also says, and it is safe here for a
// reason that does NOT transfer to ct_aot_ic: a name record contains no heap
// pointer and is never GC-traced, so it is context-independent by construction.
// An inline cache holds receivers and is not.
//
// THE HASH COMES FROM hash_name AND FROM NOWHERE ELSE. containers.hpp says
// outright that using anything else is "a lookup that silently never matches",
// which is why the handle is produced at image init and never baked into the
// image: a hash computed by the compiler is a hash computed by a different
// build of a different library.
struct aot_name_record {
    std::string text;
    std::size_t hash;
};

// THE READS EVERY ROW SHARES, in-class so that each row's call to them
// inlines. A struct because it is a friend of `context` (vm.hpp): three of
// them read its private frame stack.
struct aot_bridge {
    static context & ctx_of(aot::ct_aot_ctx * c) { return *reinterpret_cast<context *>(c); }

    // Whether the compiled body behind `f` is strict mode code - the frame
    // carries the real proto, so the answer is the interpreter's.
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

    // THE call_frame THIS HANDLE NAMES.
    //
    // BY INDEX AND GUARDED, for the reason aot_frame_storage keeps an index
    // rather than a pointer: frames_ is a vector, a nested call reallocates it,
    // and after an unwind it can be SHORTER than this frame's index - the same
    // state ct_aot_check reports as CT_AOT_UNWOUND. A helper reached in that
    // window must answer, not dereference.
    static context::call_frame * frame_record(aot::ct_aot_frame * f) {
        aot_frame_storage & held = frame_of(f);
        context & cx = *held.ctx;
        return held.frame_index < cx.frames_.size() ? &cx.frames_[held.frame_index] : nullptr;
    }

    // ct_aot_check. The row's precedence, in its order and for its reasons:
    // unwound FIRST because the call_frame is destroyed and every later test
    // would dereference it; failed BEFORE caught because the run loop's own head
    // tests failed_ in the same disjunction and leaves regardless of a handler
    // that just fired; then caught; then ok.
    static std::int32_t check(aot::ct_aot_frame * f) {
        const aot_frame_storage & held = frame_of(f);
        const context & cx = *held.ctx;
        if (cx.frames_.size() <= held.frame_index) {
            return static_cast<std::int32_t>(aot::ct_aot_status::unwound);
        }
        if (cx.failed_) { return static_cast<std::int32_t>(aot::ct_aot_status::failed); }
        // AND CAUGHT, which this could not detect until Phase 6. The row's test
        // is CT_AOT_PAD_BIT in call_frame::ip, and nothing could set it while
        // ct_aot_catch_land was unimplementable; both work now, and the bit is
        // put there by unwind_to_handler assigning `ip` from a handler whose
        // address is the body's pad id.
        if ((cx.frames_[held.frame_index].ip & CT_AOT_PAD_BIT) != 0) {
            return static_cast<std::int32_t>(aot::ct_aot_status::caught);
        }
        return static_cast<std::int32_t>(aot::ct_aot_status::ok);
    }
};

} // namespace ctbrowser::script

// THE ROWS ARE DEFINED WHERE aot.hpp DECLARES THEM, against script's types.
namespace ctbrowser::aot {
using namespace script;
} // namespace ctbrowser::aot
