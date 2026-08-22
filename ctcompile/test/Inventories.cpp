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

// AND THE AOT ABI, expanded HERE the way ctcompile will expand it - which is
// the mechanism Principle 11 is about: the runtime declares the helpers from
// aot_helpers.def and the compiler builds its descriptor table from the SAME
// file, so the two cannot disagree without failing to build.
#include <ctbrowser/aot/aot.hpp>

#include <cstdio>
#include <string_view>

namespace {

struct row {
    std::string_view name;
    // How each operand SLOT is encoded. Not what it means: `a` is the
    // destination for get_prop and the target for set_prop, so there is no
    // uniform role to name these after - see the .def's header.
    std::string_view a_kind, b_kind, c_kind;
    bool writes_a, allocates, may_throw, may_reenter, is_safepoint, may_suspend, resumable;
    std::string_view impl;
};

// The operand kind tokens are bare identifiers in the .def, so they are
// stringised here rather than named - which also checks that every one of them
// is a token this file recognises.
#define CT_OPCODE(name_, a_kind_, b_kind_, c_kind_, writes_a_, allocates_, may_throw_,             \
                  may_reenter_, is_safepoint_, may_suspend_, resumable_, impl_)                    \
    row{#name_,                                                                                    \
        #a_kind_,                                                                                  \
        #b_kind_,                                                                                  \
        #c_kind_,                                                                                  \
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
    // kidx, sidx, nidx and fidx are FOUR DIFFERENT TABLES - constants, strings,
    // names and the program's function list. They were two until the rows were
    // checked against the handlers; anything generating a bounds check from
    // these columns has to know which table it is checking.
    return k == "reg" || k == "kidx" || k == "sidx" || k == "nidx" || k == "fidx" || k == "jump" ||
           k == "count" || k == "bx_hi" || k == "unused";
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

// THE HELPER DESCRIPTORS, as the compiler needs them: a name to emit, and the
// three obligations a call site has to honour.
struct helper {
    std::string_view name;
    bool may_throw, may_reenter, is_safepoint;
};
#define CT_AOT_HELPER(name_, ret_, params_, may_throw_, may_reenter_, is_safepoint_)               \
    helper{#name_, (may_throw_) != 0, (may_reenter_) != 0, (is_safepoint_) != 0},
constexpr helper helpers[] = {
#include <ctbrowser/aot/aot_helpers.def>
};

// WHICH OPCODE EACH HELPER SERVES, and this is where drift becomes a BUILD
// ERROR rather than a test failure: the opcode is spelled as a real
// `ctbrowser::script::op::` enumerator, so renaming or deleting an opcode
// without updating the ABI table stops the compiler's build at this line.
// A test that ran later would be a weaker guarantee for the same information.
struct coverage {
    std::string_view helper;
    ctbrowser::script::op opcode;
};
#define CT_AOT_COVERS(helper_, opcode_) coverage{#helper_, ctbrowser::script::op::opcode_},
constexpr coverage covers[] = {
#include <ctbrowser/aot/aot_helpers.def>
};

// The ten opcodes no helper serves, each because it needs no runtime call. Kept
// here as well as in the .def's closing comment so that the TEST fails when the
// two disagree - a list in a comment is not checkable, and this one is the
// difference between "deliberately uncovered" and "quietly forgotten".
constexpr ctbrowser::script::op uncovered[] = {
    ctbrowser::script::op::load_const,
    ctbrowser::script::op::load_undef,
    ctbrowser::script::op::load_null,
    ctbrowser::script::op::load_true,
    ctbrowser::script::op::load_false,
    ctbrowser::script::op::move,
    ctbrowser::script::op::jump,
    ctbrowser::script::op::jump_if_not_nullish,
    ctbrowser::script::op::jump_if_defined,
    ctbrowser::script::op::halt,
};

int main() {
    // The same count the header asserts, restated where a person reading the
    // suite can see it fail.
    check(std::size(table) == ctbrowser::script::opcode_count,
          "the table and the engine disagree about how many opcodes exist", "bytecode_opcodes.def");

    for (const row & r : table) {
        check(known_kind(r.a_kind) && known_kind(r.b_kind) && known_kind(r.c_kind),
              "an operand kind that is not one of "
              "reg/kidx/sidx/nidx/fidx/jump/count/bx_hi/unused",
              r.name);

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
        if (r.c_kind == "bx_hi") {
            check(r.b_kind == "kidx" || r.b_kind == "sidx" || r.b_kind == "nidx" ||
                      r.b_kind == "fidx" || r.b_kind == "jump",
                  "c is bx_hi but b is not an index or a jump", r.name);
        }
        check(r.b_kind != "bx_hi", "b cannot be a bx high half - a is not read through bx()",
              r.name);
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

    // --- THE AOT ABI ------------------------------------------------------
    check(std::size(helpers) == ctbrowser::aot::helper_count,
          "the descriptor table and the runtime's helper_id enum disagree", "aot_helpers.def");

    for (const helper & h : helpers) {
        check(h.name.starts_with("ct_aot_"), "a helper must be ct_aot_ prefixed", h.name);
        // A HELPER THAT CAN RUN USER JAVASCRIPT IS A SAFEPOINT, always. The
        // collector is precise and a value living only in a native frame is
        // reachable from none of its roots, so a re-entering helper that did
        // not declare itself a safepoint would tell every call site it is safe
        // to keep values where the collector cannot see them.
        check(!h.may_reenter || h.is_safepoint, "may_reenter implies is_safepoint", h.name);
    }

    // THE HELPERS SERVING AN OPCODE MAY NOT UNDERSTATE WHAT IT DOES. Phase 5
    // asks for this in as many words: "add a unit test asserting that every
    // helper's flags match its opcode's flags in bytecode_opcodes.def".
    //
    // TWO THINGS "MATCH" DOES NOT MEAN, and the table already knew both.
    //
    // It is not per-helper. `type_of` is served by TWO rows on purpose -
    // ct_aot_type_of_name classifies without allocating, ct_aot_new_string
    // materialises the string only when the result escapes - and that row says
    // so: "(0,0,0,0) here OR ct_aot_new_string's (1,1,0,1) is exactly type_of's
    // inventory row". So the test is against the UNION, which is what a code
    // generator emitting the pair actually faces.
    //
    // And it is not equality. A helper serving four call opcodes carries the
    // union of what they can do, so equality would be wrong wherever one of
    // them throws and another does not. Overstating is merely conservative - a
    // call site roots a value it did not have to. UNDERSTATING tells a code
    // generator it is safe to keep a value in a register across a collection,
    // or to skip the status test after a call that can throw, and that is a
    // miscompilation no verifier catches.
    for (const row & r : table) {
        const auto opcode = static_cast<ctbrowser::script::op>(&r - table);
        bool any = false;
        bool may_throw = false;
        bool may_reenter = false;
        bool is_safepoint = false;
        for (const coverage & c : covers) {
            if (c.opcode != opcode) { continue; }
            for (const helper & h : helpers) {
                if (h.name != c.helper) { continue; }
                any = true;
                may_throw = may_throw || h.may_throw;
                may_reenter = may_reenter || h.may_reenter;
                is_safepoint = is_safepoint || h.is_safepoint;
            }
        }
        if (!any) { continue; } // the ten that need no helper are checked below

        // ONE OPCODE IS ALLOWED TO UNDERSTATE, and it is named here so that a
        // SECOND one cannot appear quietly.
        //
        // `await_value` is is_safepoint because the inventory derives that
        // mechanically as allocates||may_reenter - its own row says "the
        // mandated derivation, not an observed collection point". The helper
        // covers the READ half only: no allocate<> and no call(), with the
        // suspend half left to Phase 14. That split is legitimate only while
        // the DECISION stays allocation-free, which its row states as an ABI
        // contract rather than an observation - if promise creation is ever
        // hoisted above the three-way test, this becomes a real safepoint and
        // every caller's spill disappears. Then this exception has to go, and
        // this line is where somebody finds that out.
        const bool await_split = r.name == "await_value";
        check(may_throw || !r.may_throw, "the opcode may throw and no helper serving it says so",
              r.name);
        check(may_reenter || !r.may_reenter,
              "the opcode may re-enter user JavaScript and no helper serving it says so", r.name);
        check(is_safepoint || !r.is_safepoint || await_split,
              "the opcode is a safepoint and no helper serving it says so - a call site would "
              "keep live values where the collector cannot see them",
              r.name);
        // AND THE EXCEPTION IS ITSELF CHECKED: if the helper ever declares the
        // safepoint, the exemption is dead, and saying so is better than
        // leaving a permanent hole shaped like one opcode.
        check(!await_split || !is_safepoint,
              "await_value's helper now declares the safepoint, so the exemption above is stale "
              "and should be deleted",
              r.name);
    }

    // AND EVERY COVERAGE ROW NAMES A REAL HELPER, which the loop above would
    // otherwise pass over in silence: an opcode served only by a misspelled
    // helper looks exactly like one served by nothing.
    for (const coverage & c : covers) {
        bool named = false;
        for (const helper & h : helpers) {
            if (h.name == c.helper) { named = true; }
        }
        check(named, "a coverage row names a helper that is not in the table", c.helper);
    }

    // EVERY OPCODE IS ACCOUNTED FOR: served by a helper, or on the list of ten
    // that need no runtime call. Neither silently.
    for (const row & r : table) {
        const auto op = static_cast<ctbrowser::script::op>(&r - table);
        bool served = false;
        for (const coverage & c : covers) {
            if (c.opcode == op) { served = true; }
        }
        bool exempt = false;
        for (const ctbrowser::script::op u : uncovered) {
            if (u == op) { exempt = true; }
        }
        check(served != exempt,
              served ? "covered AND listed as needing no helper"
                     : "no helper serves it and it is not on the exempt list",
              r.name);

        // AND A SAFEPOINT OPCODE NEEDS A SAFEPOINT HELPER. If an opcode can
        // collect but every helper serving it claims it cannot, the call sites
        // will not keep their live values rooted across it.
        // await_value and yield_value are safepoints only on the path that
        // SUSPENDS - allocating a coroutine and a promise - and that path is
        // deliberately unimplemented: Phase 14 has not decided how an AOT frame
        // suspends when it has no register window to save, so the ABI offers
        // ct_aot_suspend_unsupported and a diagnosable fault instead of a
        // guess. Named here rather than weakening the rule, so that whoever
        // implements suspension is told by a failing test that this exemption
        // has to go.
        const bool suspension_pending =
            op == ctbrowser::script::op::await_value || op == ctbrowser::script::op::yield_value;
        if (served && r.is_safepoint && !suspension_pending) {
            bool any = false;
            for (const coverage & c : covers) {
                if (c.opcode != op) { continue; }
                for (const helper & h : helpers) {
                    if (h.name == c.helper && h.is_safepoint) { any = true; }
                }
            }
            check(any, "a safepoint opcode is served only by non-safepoint helpers", r.name);
        }
    }

    if (failures == 0) {
        std::printf("ok inventories (%zu opcodes, %zu call paths, %zu gc roots, "
                    "%zu abi helpers over %zu opcodes)\n",
                    std::size(table), std::size(call_paths), std::size(gc_roots),
                    std::size(helpers), std::size(covers));
    }
    return failures == 0 ? 0 : 1;
}
