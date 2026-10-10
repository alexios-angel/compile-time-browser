//===- ctcompile/test/Core/Inventories.cpp - Opcode and ABI tables --------===//
//
// Checks the bytecode opcode table (bytecode_opcodes.def) and the AOT helper
// ABI table (aot_helpers.def) against themselves and against the engine.
//
// bytecode.hpp already static_asserts that the opcode table has one row per
// opcode. This test covers what a static_assert cannot: that the columns are
// internally consistent, and that the table can be read as data, which is how
// an AOT backend consumes it.
//
//===----------------------------------------------------------------------===//
#include <ctbrowser/script/bytecode.hpp>

// EngineContract.hpp holds static_asserts on the engine's program layout that
// the program image serializer depends on. It has no runtime surface, so
// including it in this translation unit is what makes those asserts compile.
#include <ctcompile/CTJS/Lowering/RuntimeHelpers.hpp>
#include <ctcompile/JavaScript/EngineContract.hpp>

// The AOT ABI, expanded here the way ctcompile expands it. The runtime declares
// its helpers from aot_helpers.def and the compiler builds its descriptor table
// from the same file, so the two cannot disagree and still build.
#include <ctbrowser/aot/aot.hpp>

#include <cstdio>
#include <string_view>
#include <unordered_set>

#include "check.hpp"

namespace {

/// One row of bytecode_opcodes.def, read as data.
struct row {
    std::string_view name;
    /// How each operand slot is encoded, not what it means: `a` is the
    /// destination for get_prop and the target for set_prop, so there is no
    /// uniform role to name these after (see the .def's header).
    std::string_view a_kind, b_kind, c_kind;
    bool writes_a, allocates, may_throw, may_reenter, is_safepoint, may_suspend, resumable;
    std::string_view impl;
};

// The operand kind tokens are bare identifiers in the .def, so they are
// stringised here; known_kind() then checks that each is a recognised token.
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

void check(bool ok, const char * what, std::string_view who) {
    ::check(ok, std::format("{}: {}", who, what));
}

/// Whether `k` is one of the operand kinds the opcode table may use.
bool known_kind(std::string_view k) {
    // kidx, sidx, nidx and fidx index four different tables: constants,
    // strings, names and the program's function list. Anything generating a
    // bounds check from these columns has to know which table it is checking.
    static const std::unordered_set<std::string_view> kinds{
        "reg", "kidx", "sidx", "nidx", "fidx", "jump", "count", "bx_hi", "unused"};
    return kinds.contains(k);
}

} // namespace

/// A helper descriptor as the compiler needs it: a name to emit, and the three
/// obligations a call site has to honour.
struct helper {
    std::string_view name;
    bool may_throw, may_reenter, is_safepoint;
};
#define CT_AOT_HELPER(name_, ret_, params_, may_throw_, may_reenter_, is_safepoint_)               \
    helper{#name_, (may_throw_) != 0, (may_reenter_) != 0, (is_safepoint_) != 0},
constexpr helper helpers[] = {
#include <ctbrowser/aot/aot_helpers.def>
};

/// Which opcode each helper serves. The opcode is spelled as a real
/// `ctbrowser::script::op::` enumerator, so renaming or deleting an opcode
/// without updating the ABI table is a build error here rather than a test
/// failure.
struct coverage {
    std::string_view helper;
    ctbrowser::script::op opcode;
};
#define CT_AOT_COVERS(helper_, opcode_) coverage{#helper_, ctbrowser::script::op::opcode_},
constexpr coverage covers[] = {
#include <ctbrowser/aot/aot_helpers.def>

};

/// The ten opcodes no helper serves, each because it needs no runtime call.
/// The .def's closing comment lists them too, but only this copy is checked;
/// it separates "deliberately uncovered" from "quietly forgotten".
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

        // A safepoint means exactly "a collection can happen here", and a
        // collection happens where something allocates or where user
        // JavaScript runs. A row that claims otherwise would make an AOT
        // backend omit a root exactly where one is needed.
        check(r.is_safepoint == (r.allocates || r.may_reenter),
              "is_safepoint must equal allocates || may_reenter", r.name);

        // Suspending and not being resumable would mean a frame that stops and
        // can never be continued.
        check(!r.may_suspend || r.resumable, "may_suspend without resumable", r.name);

        // bx_hi is the high half of the preceding field, so it can only follow
        // an operand that is read through bx(): an index or a jump.
        if (r.c_kind == "bx_hi") {
            static const std::unordered_set<std::string_view> wideKinds{"kidx", "sidx", "nidx",
                                                                        "fidx", "jump"};
            check(wideKinds.contains(r.b_kind), "c is bx_hi but b is not an index or a jump",
                  r.name);
        }
        check(r.b_kind != "bx_hi", "b cannot be a bx high half - a is not read through bx()",
              r.name);
        check(!r.impl.empty(), "impl must say where the semantics live", r.name);
    }

    // --- Every parameter of every helper is classified ---------------------
    //
    // The lowering supplies each helper parameter from an operand, the frame,
    // an attribute immediate, or nothing in the IR, and it derives which from
    // the .def's parameter text rather than from a hand-kept second copy of the
    // ABI (see RuntimeHelpers.hpp). That derivation is only sound if every
    // parameter classifies, so this checks the whole table.
    for (const helper & h : helpers) {
        const ctcompile::ctjs::runtime_helper & row =
            ctcompile::ctjs::runtime_helpers[&h - helpers];
        for (std::size_t i = 0; i < row.role_count; ++i) {
            check(row.roles[i] != ctcompile::ctjs::param_role::unknown,
                  "every ABI parameter must classify - an unclassified one means the roles are a "
                  "second copy of the ABI rather than a reading of it",
                  h.name);
        }
        check(row.role_count == row.arity, "a helper's role count must equal its arity", h.name);
    }

    // The role the C signature cannot show: ct_aot_binary_op's `uint32_t
    // op_kind` is a ctbrowser::script::op (aot_bridge/operators.cpp casts it
    // and runs the interpreter's own switch on it), so a lowering that passed a
    // CTJS enum ordinal would compile one operator as another. Checking that
    // the parameter classifies as `opcode` makes that a test failure.
    {
        const ctcompile::ctjs::runtime_helper & binary =
            ctcompile::ctjs::helper_for(ctbrowser::aot::helper_id::ct_aot_binary_op);
        check(binary.role_count == 5, "ct_aot_binary_op takes five parameters", "ct_aot_binary_op");
        check(binary.roles[0] == ctcompile::ctjs::param_role::frame, "the first is the frame",
              "ct_aot_binary_op");
        check(binary.roles[1] == ctcompile::ctjs::param_role::opcode,
              "AND THE SECOND IS AN OPCODE, not a count - a backend must spell it by name",
              "ct_aot_binary_op");
        check(binary.roles[2] == ctcompile::ctjs::param_role::operand, "then two values",
              "ct_aot_binary_op");
        check(binary.roles[4] == ctcompile::ctjs::param_role::out_value,
              "and the result comes back through a pointer", "ct_aot_binary_op");
    }

    // --- The AOT ABI -------------------------------------------------------
    check(std::size(helpers) == ctbrowser::aot::helper_count,
          "the descriptor table and the runtime's helper_id enum disagree", "aot_helpers.def");

    for (const helper & h : helpers) {
        check(h.name.starts_with("ct_aot_"), "a helper must be ct_aot_ prefixed", h.name);
        // A helper that can run user JavaScript is always a safepoint. The
        // collector is precise and a value living only in a native frame is
        // reachable from none of its roots, so a re-entering helper that did
        // not declare itself a safepoint would tell every call site it is safe
        // to keep values where the collector cannot see them.
        check(!h.may_reenter || h.is_safepoint, "may_reenter implies is_safepoint", h.name);
    }

    // The helpers serving an opcode may not understate what it does. Two
    // subtleties:
    //
    // The check is against the union of all helpers serving the opcode, not
    // per helper. `type_of` is served by two rows on purpose:
    // ct_aot_type_of_name classifies without allocating, and ct_aot_new_string
    // materialises the string only when the result escapes. A code generator
    // emitting the pair faces the union.
    //
    // It is one-sided, not equality. A helper serving four call opcodes carries
    // the union of what they can do, so equality would be wrong wherever one
    // of them throws and another does not. Overstating is merely conservative
    // (a call site roots a value it did not have to). Understating lets a code
    // generator keep a value in a register across a collection, or skip the
    // status test after a call that can throw: a miscompilation no verifier
    // catches.
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
        if (!any) { continue; } // The ten that need no helper are checked below.

        // One opcode is allowed to understate, and it is named here so that a
        // second one cannot appear quietly.
        //
        // `await_value`'s row is a safepoint only because is_safepoint is
        // derived mechanically as allocates||may_reenter. Its helper,
        // ct_aot_await_settled, implements only the non-suspending read path,
        // which neither allocates nor calls; suspension in AOT code is not
        // implemented. That split holds only while the settled-or-not decision
        // stays allocation-free, which the helper's row states as an ABI
        // contract. If promise creation ever moves before that test, this
        // becomes a real safepoint and the exemption must go.
        const bool await_split = r.name == "await_value";
        check(may_throw || !r.may_throw, "the opcode may throw and no helper serving it says so",
              r.name);
        check(may_reenter || !r.may_reenter,
              "the opcode may re-enter user JavaScript and no helper serving it says so", r.name);
        check(is_safepoint || !r.is_safepoint || await_split,
              "the opcode is a safepoint and no helper serving it says so - a call site would "
              "keep live values where the collector cannot see them",
              r.name);
        // The exemption is itself checked: if the helper ever declares the
        // safepoint, the exemption is stale and this fails.
        check(!await_split || !is_safepoint,
              "await_value's helper now declares the safepoint, so the exemption above is stale "
              "and should be deleted",
              r.name);
    }

    // Every coverage row must name a real helper, which the loop above would
    // otherwise pass over in silence: an opcode served only by a misspelled
    // helper looks exactly like one served by nothing.
    for (const coverage & c : covers) {
        bool named = false;
        for (const helper & h : helpers) {
            if (h.name == c.helper) { named = true; }
        }
        check(named, "a coverage row names a helper that is not in the table", c.helper);
    }

    // Every opcode is accounted for exactly once: served by a helper, or on the
    // list of ten that need no runtime call.
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

        // A safepoint opcode needs a safepoint helper. If an opcode can collect
        // but every helper serving it claims it cannot, the call sites will not
        // keep their live values rooted across it.
        //
        // await_value and yield_value are safepoints only on the path that
        // suspends (allocating a coroutine and a promise). AOT frames cannot
        // suspend yet: they have no register window to save, so the ABI offers
        // ct_aot_suspend_unsupported, a diagnosable fault, instead of a guess.
        // Whoever implements suspension must remove this exemption.
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

    if (ctbrowser_test_failures == 0) {
        std::printf("ok inventories (%zu opcodes, %zu abi helpers over %zu opcodes)\n",
                    std::size(table), std::size(helpers), std::size(covers));
    }
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
