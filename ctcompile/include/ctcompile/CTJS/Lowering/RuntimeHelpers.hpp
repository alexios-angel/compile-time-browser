#pragma once
// THE ABI TABLE, AS DATA THE COMPILER CAN READ.
//
// aot_helpers.def declares 69 helpers with their obligations. The dialect
// already names them - CTJS_RuntimeOp takes a helper_id enumerator, so an
// operation cannot claim a helper the runtime does not declare, and a wrong
// name is a C++ compile error. This is the other half: at lowering time the
// compiler needs the SYMBOL to call and the ARITY to check against.
//
// THE ARITY CHECK CLOSES THE HOLE FROM THE OTHER SIDE. The name check catches a
// helper that does not exist; it cannot catch an operation whose operand list
// has drifted from the helper's parameters - `get_property(object, key)` against
// a helper that grew a third parameter reads fine in both files and produces a
// call with a garbage argument. Counting both and comparing is one line and
// catches exactly that.
#include <cstddef>
#include <string_view>

#include <ctbrowser/aot/aot.hpp>

namespace ctcompile::ctjs {

// WHAT ONE ABI PARAMETER IS FOR.
//
// The lowering has to supply every parameter of a helper, and they do not all
// come from the same place: some are the operation's operands, one is the
// frame, several are immediates that live in an ATTRIBUTE, and a few have no
// source in the IR at all. Naming the difference is what lets one pattern serve
// the whole table instead of fifty hand-written ones.
//
// DERIVED FROM THE .def, NEVER WRITTEN DOWN TWICE. The parameter list is
// already there as C source, with its TYPES and its NAMES, and both are needed:
// every `uint32_t` in the table is ambiguous by type alone - it is an argc, a
// slot, a length, a pad, a register count or an opcode - and unambiguous by
// name. A hand-kept column would be a second copy of the ABI, which this
// project treats as a defect in itself.
enum class param_role {
    unknown,   // nothing should ever be this - the golden test asserts it
    frame,     // struct ct_aot_frame * - the enclosing ctjs.frame_enter
    context,   // struct ct_aot_ctx * - the script context itself
    operand,   // uint64_t - a JavaScript value, from the operation's operands
    out_value, // uint64_t * - the result, returned through a pointer
    out_other, // uint32_t*/int32_t*/double*/const char** - a non-value result
    opcode,    // uint32_t op_kind / opcode - A BYTECODE OPCODE, see below
    count,     // uint32_t argc/len/slot/pad/index/... - an immediate integer
    number,    // double - an immediate float
    text,      // const char * - a string's bytes
    values,    // const uint64_t * - a run of values (argv, slots, upvalues)
    name,      // const struct ct_aot_name * - an interned property name
    site,      // const struct ct_aot_site * - the baked diagnostic identity
    cache,     // struct ct_aot_ic * - inline-cache storage, per site
    storage,   // void * - caller-allocated frame space
};

// WHAT ONE HELPER RETURNS - AND THEREFORE HOW A CALLER LEARNS IT FAILED.
//
// THE .def STATES THIS AS A RULE AND THIS IS THAT RULE, READ: "an unsigned
// return is data, never a status" (aot_helpers.def:761). It is not a
// convention - ct_aot_failed argues for its own uint32_t return explicitly,
// "it is data, not a status" (:232), and ct_aot_bit_not argues the other way,
// keeping a status return "despite being raise-tier-only" because the row is a
// safepoint. Both arguments are only decidable because the rule exists.
enum class return_role {
    unknown, // nothing should ever be this - a static_assert below says so
    nothing, // void
    status,  // int32_t - a ct_aot_status the caller MUST test
    value,   // uint64_t - a JavaScript value
    data,    // uint32_t - a length, a flag, a slot. DATA, NEVER A STATUS.
    number,  // double
    frame,   // struct ct_aot_frame * - ct_aot_enter alone; NULL is the failure
    values,  // uint64_t * - ct_aot_slots' span
    name,    // const struct ct_aot_name * - ct_aot_intern_name
};

// TWO TIERS OF FAILURE, AND A BACKEND THAT CONFLATES THEM IS WRONG TWICE OVER.
//
// aot_helpers.def opens by separating them: raise() "sets a flag no try/catch
// can see" and unwinds the whole native chain, while thrown_ plus
// unwind_to_handler "completes INSIDE the callee". The RETURN TYPE is what
// tells them apart, which is why this is derived here rather than guessed at
// each call site:
//
//   returns int32_t   ->  the catchable edge. Test the status after the call.
//   may_throw, but returns data or a value  ->  RAISE TIER ONLY. The result is
//       always well-formed - allocate() "raises past the ceiling and then STILL
//       returns a real object" (:1132) - and the caller polls ct_aot_failed at
//       BACK-EDGES, not after every allocation, because "a function running
//       past the ceiling is WASTEFUL, NEVER UNSAFE" (:230).
//
// GETTING THIS BACKWARDS FAILS SILENTLY IN BOTH DIRECTIONS. Emit a status test
// after ct_aot_new_object and there is no status to test - the uint64_t is a
// value, and any bit pattern it compares equal to is a coincidence. Omit the
// back-edge poll and a program past the allocation ceiling runs on forever
// instead of ending.

// `op_kind` IS A ctbrowser::script::op, NOT A CTJS ENUM ORDINAL.
//
// This is the sharpest thing about the table and it is invisible from the
// signature: aot_bridge.cpp does `static_cast<op>(op_kind)` and dispatches the
// interpreter's own switch on it. So lowering `ctjs.binary pow` by passing the
// CTJS BinaryKind ordinal - 5 - would call the runtime with `op(5)`, whatever
// that happens to be, and compile `**` into a different operator entirely. It
// reads correctly in both files and computes the wrong answer.
//
// The role exists so a backend must spell the opcode BY NAME, which makes a
// renumbering of `enum class op` a build error rather than a miscompile.

struct runtime_helper {
    std::string_view symbol;
    // EVERY PARAMETER, INCLUDING THE FRAME HANDLE. The .def writes the
    // parameter list as C source and this counts it, rather than a hand-kept
    // number beside each row that could disagree with the row above it.
    std::size_t arity;
    bool may_throw;
    bool may_reenter;
    bool is_safepoint;
    // WHETHER ITS PARAMETERS ARE "FRAME, THEN VALUES" AND NOTHING ELSE.
    //
    // ARITY IS NOT ENOUGH AND THIS IS WHY. `ctjs.call` is variadic, so its
    // operand count varies per call site - and a site with five arguments
    // matches ct_aot_call's eight parameters exactly. The types do not save it
    // either: the FIRST call site creates the declaration, so a wrong shape
    // becomes self-consistent and every later site agrees with it. Running the
    // pass over p5.js emitted 17,848 calls, some of which passed a value where
    // ct_aot_call wants an argv pointer and an argc.
    //
    // That is the failure this project keeps meeting - code that verifies,
    // prints plausibly and is wrong - reached by a check that looked sufficient.
    //
    // So the shape is READ, not counted: every parameter after an optional
    // leading frame must be exactly `uint64_t`, which is what a JavaScript
    // value is in this ABI. A `uint64_t *` is an out-parameter, a `uint32_t` is
    // a kind or a count, and a `const struct ...*` is a site or a name - none of
    // which any CTJS operand can supply, and each of which needs its own
    // deliberate materialisation.
    bool values_only;
    // WHAT IT RETURNS, under the .def's own return-type rule. See return_role.
    return_role ret;
    // One per parameter, in ABI order. Twelve is wider than the widest row.
    param_role roles[12];
    std::size_t role_count;
};

// One parameter's role, from its type and its name.
[[nodiscard]] constexpr param_role classify(std::string_view one) {
    while (!one.empty() && one.front() == ' ') { one.remove_prefix(1); }
    while (!one.empty() && one.back() == ' ') { one.remove_suffix(1); }
    if (one.empty()) { return param_role::unknown; }
    std::size_t split = one.size();
    while (split > 0 && one[split - 1] != ' ' && one[split - 1] != '*') { --split; }
    const std::string_view name = one.substr(split);

    if (one.starts_with("struct ct_aot_frame *")) { return param_role::frame; }
    if (one.starts_with("struct ct_aot_ctx *")) { return param_role::context; }
    if (one.starts_with("const struct ct_aot_name *")) { return param_role::name; }
    if (one.starts_with("const struct ct_aot_site *")) { return param_role::site; }
    if (one.starts_with("struct ct_aot_ic *")) { return param_role::cache; }
    if (one.starts_with("void *")) { return param_role::storage; }
    if (one.starts_with("const uint64_t *")) { return param_role::values; }
    if (one.starts_with("const char **")) { return param_role::out_other; }
    if (one.starts_with("const char *")) { return param_role::text; }
    if (one.starts_with("uint64_t *")) { return param_role::out_value; }
    if (one.starts_with("uint32_t *") || one.starts_with("int32_t *") ||
        one.starts_with("double *")) {
        return param_role::out_other;
    }
    if (one.starts_with("uint64_t")) { return param_role::operand; }
    if (one.starts_with("double")) { return param_role::number; }
    if (one.starts_with("uint32_t")) {
        // THE AMBIGUOUS ONE, RESOLVED BY NAME. Every uint32_t in the table is
        // an opcode or an immediate integer, and only its name says which.
        if (name == "op_kind" || name == "opcode") { return param_role::opcode; }
        return param_role::count;
    }
    return param_role::unknown;
}

// Every parameter's role, in order. Returns how many there were.
[[nodiscard]] constexpr std::size_t classify_all(std::string_view params, param_role * into,
                                                 std::size_t capacity) {
    std::size_t depth = 0;
    std::size_t start = 0;
    std::size_t count = 0;
    const auto take = [&](std::string_view one) {
        if (count < capacity) { into[count] = classify(one); }
        ++count;
    };
    for (std::size_t i = 0; i < params.size(); ++i) {
        const char c = params[i];
        if (c == '(') {
            ++depth;
            if (depth == 1) { start = i + 1; }
            continue;
        }
        if (c == ')') {
            if (depth == 1 && i > start) { take(params.substr(start, i - start)); }
            if (depth > 0) { --depth; }
            continue;
        }
        if (c == ',' && depth == 1) {
            take(params.substr(start, i - start));
            start = i + 1;
        }
    }
    return count;
}

// One row's return role - FROM THE RETURN TYPE AND THE PARAMETERS BOTH.
//
// THE PARAMETERS ARE NOT OPTIONAL HERE, and the .def says why: ct_aot_to_int32
// is "THE ONE DELIBERATE EXCEPTION to the return-type rule ... a signed int32
// return that is DATA, not a status, because ToInt32's result is signed by
// definition. It takes no frame handle, which is the mechanical tell - no
// status-returning helper in the table lacks one" (aot_helpers.def:824-828).
//
// THE TELL IS EXACT, NOT A HEURISTIC: 24 rows return int32_t and take a frame,
// exactly one returns int32_t and does not, and it is that row. So the rule is
// READ rather than the row special-cased by name - a second frameless int32_t
// row would classify correctly on arrival instead of being missed.
//
// AND GETTING IT WRONG IS THE WORST KIND OF WRONG. A backend that read
// ct_aot_to_int32's result as a status would be testing the value of `x | 0`
// against ct_aot_status: 1 is `failed` and unwinds the whole native chain, 2 is
// `caught` and branches to a landing pad that does not exist, and 3 is `ok` by
// coincidence. `2.7 | 0` is 2.
[[nodiscard]] constexpr return_role classify_return(std::string_view ret, std::string_view params) {
    while (!ret.empty() && ret.front() == ' ') { ret.remove_prefix(1); }
    while (!ret.empty() && ret.back() == ' ') { ret.remove_suffix(1); }
    if (ret == "void") { return return_role::nothing; }
    if (ret == "int32_t") {
        return params.find("ct_aot_frame *") == std::string_view::npos ? return_role::data
                                                                       : return_role::status;
    }
    if (ret == "uint64_t") { return return_role::value; }
    if (ret == "uint32_t") { return return_role::data; }
    if (ret == "double") { return return_role::number; }
    if (ret.starts_with("struct ct_aot_frame *")) { return return_role::frame; }
    if (ret.starts_with("uint64_t *")) { return return_role::values; }
    if (ret.starts_with("const struct ct_aot_name *")) { return return_role::name; }
    return return_role::unknown;
}

[[nodiscard]] constexpr const char * role_name(param_role role) {
    switch (role) {
    case param_role::unknown: return "unknown";
    case param_role::frame: return "frame";
    case param_role::context: return "context";
    case param_role::operand: return "operand";
    case param_role::out_value: return "out_value";
    case param_role::out_other: return "out_other";
    case param_role::opcode: return "opcode";
    case param_role::count: return "count";
    case param_role::number: return "number";
    case param_role::text: return "text";
    case param_role::values: return "values";
    case param_role::name: return "name";
    case param_role::site: return "site";
    case param_role::cache: return "cache";
    case param_role::storage: return "storage";
    }
    return "unknown";
}

// Whether a helper's parameters are "optionally a frame, then values only".
//
// DEFINED FROM THE ROLES NOW, AND THAT IS A CORRECTION. This was a second
// parser asking whether each parameter's text starts with "uint64_t " - and
// `uint64_t *out` DOES start with "uint64_t ", because the space comes before
// the `*`. So it admitted six rows that carry an out-parameter -
// ct_aot_negate, ct_aot_bit_not, ct_aot_catch_land, ct_aot_iterable_values,
// ct_aot_await_settled and ct_aot_dynamic_import - while the comment beside it
// asserted in as many words that a `uint64_t *` was excluded.
//
// NOTHING HAD HIT IT, WHICH IS NOT A DEFENCE. No CTJS operation names any of
// those six yet, and the arity check happened to cover them. It is the same
// shape as the bug this predicate exists to stop - a check that looks
// sufficient, reads correctly and is not - sitting inside the check itself,
// waiting for the next operation to be added.
//
// classify() already reads the same text and already distinguishes `uint64_t`
// from `uint64_t *`. Asking the roles cannot drift from the roles, and it
// deletes the second parser rather than fixing it.
[[nodiscard]] constexpr bool roles_are_values_only(const param_role * roles, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        // A frame or a script context is allowed, but only in first position.
        //
        // THIS CLAUSE IS NOT EXERCISED BY THE TABLE AND THAT IS SAID HERE
        // RATHER THAN LEFT TO BE DISCOVERED. Removing `i == 0` leaves the whole
        // suite green, because no row in the ABI puts a frame anywhere but
        // first - so the guard cannot be shown to be load-bearing by breaking
        // it. What CAN be checked is the property it assumes, and
        // frame_is_always_first() below does exactly that: if a future row ever
        // takes a frame in second position, that assertion fires and this
        // clause is what stops the row being read as values-only in the
        // meantime.
        if (i == 0 && (roles[i] == param_role::frame || roles[i] == param_role::context)) {
            continue;
        }
        if (roles[i] != param_role::operand) { return false; }
    }
    return true;
}

// How many top-level parameters a stringified parameter list declares.
//
// TOP-LEVEL, because a parameter's own type can contain a comma - none does
// today, and `std::function<void(int, int)>` in a future row would make a naive
// count wrong by one without anything saying so.
[[nodiscard]] constexpr std::size_t count_parameters(std::string_view params) {
    std::size_t depth = 0;
    std::size_t count = 0;
    bool seen_anything = false;
    for (const char c : params) {
        if (c == '(' || c == '<' || c == '[') { ++depth; }
        if (c == ')' || c == '>' || c == ']') {
            if (depth > 0) { --depth; }
        }
        // Depth 1 is inside the outermost parentheses, which is where a
        // parameter separator lives.
        if (c == ',' && depth == 1) { ++count; }
        if (depth == 1 && c != '(' && c != ' ') { seen_anything = true; }
    }
    return seen_anything ? count + 1 : 0;
}

#define CT_AOT_HELPER(name_, ret_, params_, may_throw_, may_reenter_, is_safepoint_)               \
    [] {                                                                                           \
        runtime_helper row{#name_,                                                                 \
                           count_parameters(#params_),                                             \
                           (may_throw_) != 0,                                                      \
                           (may_reenter_) != 0,                                                    \
                           (is_safepoint_) != 0,                                                   \
                           false,                                                                  \
                           classify_return(#ret_, #params_),                                       \
                           {},                                                                     \
                           0};                                                                     \
        row.role_count = classify_all(#params_, row.roles, 12);                                    \
        row.values_only = roles_are_values_only(row.roles, row.role_count);                        \
        return row;                                                                                \
    }(),
inline constexpr runtime_helper runtime_helpers[] = {
#include <ctbrowser/aot/aot_helpers.def>
};
#undef CT_AOT_HELPER

static_assert(std::size(runtime_helpers) == ctbrowser::aot::helper_count,
              "the lowering's helper table and helper_id disagree");

[[nodiscard]] constexpr const runtime_helper & helper_for(ctbrowser::aot::helper_id which) {
    return runtime_helpers[static_cast<std::size_t>(which)];
}

// WHETHER A CALLER MUST TEST A STATUS AFTER THIS CALL.
[[nodiscard]] constexpr bool returns_status(const runtime_helper & row) {
    return row.ret == return_role::status;
}

// WHETHER THIS HELPER'S ONLY FAILURE IS THE RAISE TIER, so the caller takes its
// result unconditionally and polls ct_aot_failed at the next back-edge.
//
// ct_aot_enter IS NOT ONE OF THESE and the carve-out is deliberate: it fails by
// returning a NULL frame pointer, which is neither a status nor a poll, and a
// backend that lumped it in with the value-returning rows would dereference the
// null. It is the single row whose failure has its own shape.
[[nodiscard]] constexpr bool is_raise_tier_only(const runtime_helper & row) {
    return row.may_throw && row.ret != return_role::status && row.ret != return_role::frame;
}

// EVERY ROW CLASSIFIES. An unknown return type means the ABI grew a shape this
// file has not been taught, and the compiler would otherwise decide what to do
// about it by accident.
constexpr bool every_return_classifies() {
    for (const runtime_helper & row : runtime_helpers) {
        if (row.ret == return_role::unknown) { return false; }
    }
    return true;
}
static_assert(every_return_classifies(),
              "a helper's return type is not one this file knows - teach classify_return, and "
              "decide which failure tier the new shape belongs to before a backend guesses");

// THE ROW THE .def ARGUES ABOUT, PINNED. ct_aot_failed IS THE SIGNAL for the
// raise tier and returns uint32_t "not int32_t, under the return-type rule - it
// is data, not a status" (aot_helpers.def:232). If it ever returned int32_t,
// every poll site would start testing it as a status - and CT_AOT_OK is zero,
// so a raised program would read as healthy.
static_assert(helper_for(ctbrowser::aot::helper_id::ct_aot_failed).ret == return_role::data,
              "ct_aot_failed returns DATA - nonzero means the run is over; as a status, zero "
              "would mean the opposite");
static_assert(!returns_status(helper_for(ctbrowser::aot::helper_id::ct_aot_failed)));

// AND THE CLASSIFIER ITSELF returns a status while NOT being may_throw: it is
// how a caller of any other row obtains one, so it cannot need one of its own.
static_assert(returns_status(helper_for(ctbrowser::aot::helper_id::ct_aot_check)));
static_assert(!helper_for(ctbrowser::aot::helper_id::ct_aot_check).may_throw);

// THE TWO TIERS, ONE ROW EACH. ct_aot_binary_op is the catchable edge - a
// valueOf can throw - and ct_aot_new_object is the ceiling only.
static_assert(returns_status(helper_for(ctbrowser::aot::helper_id::ct_aot_binary_op)));
static_assert(!is_raise_tier_only(helper_for(ctbrowser::aot::helper_id::ct_aot_binary_op)));
static_assert(is_raise_tier_only(helper_for(ctbrowser::aot::helper_id::ct_aot_new_object)),
              "allocate() raises past the ceiling and STILL returns a real object, so there is "
              "no status here to test - only a poll to schedule");
static_assert(helper_for(ctbrowser::aot::helper_id::ct_aot_new_object).may_throw,
              "and it IS may_throw, which is exactly why may_throw alone cannot tell a backend "
              "what edge to emit");

// A FRAME OR A CONTEXT IS ALWAYS THE FIRST PARAMETER, NEVER A LATER ONE.
//
// This is the assumption roles_are_values_only's position clause rests on, and
// it is asserted here because it cannot be demonstrated there: with the table
// as it stands, deleting that clause changes no answer. Checking the property
// over all 69 rows is the honest version of the same claim - and it is the one
// that fires if a row ever arrives that breaks it.
constexpr bool frame_is_always_first() {
    for (const runtime_helper & row : runtime_helpers) {
        for (std::size_t i = 1; i < row.role_count; ++i) {
            if (row.roles[i] == param_role::frame || row.roles[i] == param_role::context) {
                return false;
            }
        }
    }
    return true;
}
static_assert(frame_is_always_first(),
              "a helper takes a frame handle somewhere other than first - decide what that means "
              "for the values-only rule before a lowering decides for you");

// THE ONE DELIBERATE EXCEPTION, PINNED BY NAME. ct_aot_to_int32 returns
// int32_t and is NOT a status: ToInt32's result is signed by definition. Read
// as a status it would test the value of `x | 0` against ct_aot_status - and
// `2.7 | 0` is 2, which is `caught`.
static_assert(helper_for(ctbrowser::aot::helper_id::ct_aot_to_int32).ret == return_role::data,
              "ToInt32 returns DATA that happens to be signed - a backend testing it as a status "
              "branches on the arithmetic");
static_assert(!returns_status(helper_for(ctbrowser::aot::helper_id::ct_aot_to_int32)));
// AND ITS SIBLING, which differs only in signedness and must stay data too.
static_assert(helper_for(ctbrowser::aot::helper_id::ct_aot_to_uint32).ret == return_role::data);

// THE SIX ROWS THE OLD values_only ADMITTED. Each carries a `uint64_t *out`,
// which is an out-parameter and not something any CTJS operand can supply; the
// text-prefix check accepted them because "uint64_t *out" starts with
// "uint64_t ". Naming them here is what makes the correction stay corrected.
static_assert(!helper_for(ctbrowser::aot::helper_id::ct_aot_negate).values_only,
              "`uint64_t *out` is an out-parameter - passing a value there writes through a "
              "JavaScript value reinterpreted as a pointer");
static_assert(!helper_for(ctbrowser::aot::helper_id::ct_aot_bit_not).values_only);
static_assert(!helper_for(ctbrowser::aot::helper_id::ct_aot_catch_land).values_only);
static_assert(!helper_for(ctbrowser::aot::helper_id::ct_aot_iterable_values).values_only);
static_assert(!helper_for(ctbrowser::aot::helper_id::ct_aot_await_settled).values_only);
static_assert(!helper_for(ctbrowser::aot::helper_id::ct_aot_dynamic_import).values_only);

// AND THE ROWS IT SHOULD STILL ADMIT, so the correction did not simply refuse
// everything - which is the failure mode of a tightened check nobody measured.
static_assert(helper_for(ctbrowser::aot::helper_id::ct_aot_cell_get).values_only,
              "(uint64_t cell) is values-only");
static_assert(helper_for(ctbrowser::aot::helper_id::ct_aot_leave).values_only,
              "(struct ct_aot_frame *fr) is a bare frame, which is allowed in first position");
static_assert(helper_for(ctbrowser::aot::helper_id::ct_aot_new_object).values_only);
static_assert(helper_for(ctbrowser::aot::helper_id::ct_aot_append).values_only);
// A FRAME ANYWHERE BUT FIRST IS NOT VALUES-ONLY, and ct_aot_binary_op is the
// row that proves the position rule is doing work: frame, opcode, ...
static_assert(!helper_for(ctbrowser::aot::helper_id::ct_aot_binary_op).values_only);

// ct_aot_enter's failure has a THIRD shape and is excluded from both.
static_assert(helper_for(ctbrowser::aot::helper_id::ct_aot_enter).ret == return_role::frame);
static_assert(!returns_status(helper_for(ctbrowser::aot::helper_id::ct_aot_enter)));
static_assert(!is_raise_tier_only(helper_for(ctbrowser::aot::helper_id::ct_aot_enter)),
              "it signals the depth raise with a NULL frame pointer - neither a status to test "
              "nor a poll to defer");

} // namespace ctcompile::ctjs
