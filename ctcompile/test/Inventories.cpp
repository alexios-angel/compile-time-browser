// Phase 0's inventories, checked against themselves and against the engine.
//
// Phase 0 asks for a test asserting that the table's entry count equals the
// VM's opcode count. bytecode.hpp already makes that a static_assert - a build
// error, which is stronger and is what this project prefers - so this test
// covers what a static_assert cannot: the internal consistency of the columns,
// and the fact that the table is READABLE as data rather than only expandable
// as a macro. An AOT backend consumes it as data, and a table nobody has ever
// walked is a table whose shape is only a guess.
#include <ctbrowser/script/bytecode.hpp>

// INCLUDED FOR ITS STATIC_ASSERTS. EngineContract.hpp is the program
// representation inventory - the layout facts Phase 15's serializer will
// depend on, written as build errors rather than as prose. It has no runtime
// surface, so this is the translation unit that makes it fire.
#include <ctcompile/JavaScript/EngineContract.hpp>

#include <cstdio>
#include <string_view>

namespace {

struct row {
    std::string_view name;
    std::string_view a, b, c;
    bool writes_a, allocates, may_throw, may_reenter, is_safepoint, may_suspend, resumable;
    std::string_view impl;
};

// The operand kind tokens are bare identifiers in the .def, so they are
// stringised here rather than named - which also checks that every one of them
// is a token this file recognises.
#define CT_OPCODE(name_, a_, b_, c_, writes_a_, allocates_, may_throw_, may_reenter_,              \
                  is_safepoint_, may_suspend_, resumable_, impl_)                                  \
    row{#name_,                                                                                    \
        #a_,                                                                                       \
        #b_,                                                                                       \
        #c_,                                                                                       \
        (writes_a_) != 0,                                                                          \
        (allocates_) != 0,                                                                         \
        (may_throw_) != 0,                                                                         \
        (may_reenter_) != 0,                                                                       \
        (is_safepoint_) != 0,                                                                      \
        (may_suspend_) != 0,                                                                       \
        (resumable_) != 0,                                                                         \
        impl_},
constexpr row table[] = {
#include <ctbrowser/script/bytecode_opcodes.def>
};
#undef CT_OPCODE

int failures = 0;

void check(bool ok, const char * what, std::string_view who) {
    if (!ok) {
        std::printf("FAIL %.*s: %s\n", static_cast<int>(who.size()), who.data(), what);
        ++failures;
    }
}

bool known_kind(std::string_view k) {
    return k == "reg" || k == "kidx" || k == "sidx" || k == "jump" || k == "count" ||
           k == "bx_hi" || k == "unused";
}

} // namespace

// THE OTHER TWO INVENTORIES, walked for the same reason the opcode table is:
// a table nobody reads is a table whose shape is a guess.
struct call_path {
    std::string_view name, signature;
    bool canonical, resets_stack, clears_failure, drains_microtasks;
    std::string_view note;
};
#define CT_CALL_PATH(name_, sig_, canonical_, resets_, clears_, drains_, note_)                    \
    call_path{#name_, sig_, canonical_, resets_, clears_, drains_, note_},
constexpr call_path call_paths[] = {
#include <ctcompile/JavaScript/CallPaths.def>
};
#undef CT_CALL_PATH

struct gc_root {
    std::string_view name, owner, marked_in, note;
};
#define CT_GC_ROOT(name_, owner_, marked_in_, note_) gc_root{#name_, owner_, marked_in_, note_},
constexpr gc_root gc_roots[] = {
#include <ctcompile/JavaScript/GCRoots.def>
};
#undef CT_GC_ROOT

int main() {
    // The same count the header asserts, restated where a person reading the
    // suite can see it fail.
    check(std::size(table) == ctbrowser::script::opcode_count,
          "the table and the engine disagree about how many opcodes exist", "bytecode_opcodes.def");

    for (const row & r : table) {
        check(known_kind(r.a) && known_kind(r.b) && known_kind(r.c),
              "an operand kind that is not one of reg/kidx/sidx/jump/count/bx_hi/unused", r.name);

        // A SAFEPOINT IS NOT AN OPINION. It is exactly "can a collection happen
        // here", and a collection happens where something allocates or where
        // user JavaScript runs. A row that claims otherwise is a
        // classification mistake, and it is the kind that makes an AOT backend
        // omit a root exactly where one is needed.
        check(r.is_safepoint == (r.allocates || r.may_reenter),
              "is_safepoint must equal allocates || may_reenter", r.name);

        // Suspending and not being resumable would mean a frame that stops and
        // can never be continued.
        check(!r.may_suspend || r.resumable, "may_suspend without resumable", r.name);

        // bx_hi is the HIGH HALF of the preceding field, so it can only follow
        // an operand that is read through bx(): an index or a jump.
        if (r.c == "bx_hi") {
            check(r.b == "kidx" || r.b == "sidx" || r.b == "jump",
                  "c is bx_hi but b is not an index or a jump", r.name);
        }
        check(r.b != "bx_hi", "b cannot be a bx high half - a is not read through bx()", r.name);
        check(!r.impl.empty(), "impl must say where the semantics live", r.name);
    }

    // EXACTLY ONE CANONICAL ENTRY into the VM. Phase 3's whole job is that every
    // other path routes through it or stops existing; two canonical paths would
    // mean two places a mixed-mode dispatch decision has to be made, and the
    // second one is the one that gets forgotten.
    int canonical = 0;
    for (const call_path & p : call_paths) {
        if (p.canonical) { ++canonical; }
        // Only a TOP-LEVEL entry may clear the stack. Doing it from a nested one
        // discards the frames of whoever was running.
        check(!p.resets_stack || !p.canonical,
              "the canonical entry must not reset the stack - it is entered from inside a run",
              p.name);
    }
    check(canonical == 1, "exactly one call path is the canonical one", "CallPaths.def");

    // The one hook the embedder gets, and the reason Phase 4 cannot simply call
    // set_external_roots: it ASSIGNS rather than appends.
    bool has_external = false;
    for (const gc_root & r : gc_roots) {
        check(!r.name.empty() && !r.owner.empty(), "a root must name itself and its owner", r.name);
        if (r.name == "external") { has_external = true; }
    }
    check(has_external,
          "the external-roots hook must be in the table - AOT frames reach the collector "
          "through it",
          "GCRoots.def");

    if (failures == 0) {
        std::printf("ok inventories (%zu opcodes, %zu call paths, %zu gc roots)\n",
                    std::size(table), std::size(call_paths), std::size(gc_roots));
    }
    return failures == 0 ? 0 : 1;
}
