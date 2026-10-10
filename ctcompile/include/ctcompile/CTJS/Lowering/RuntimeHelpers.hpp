//===- RuntimeHelpers.hpp - The AOT helper ABI table ------------*- C++ -*-===//
//
// The runtime-helper ABI table, as constexpr data the compiler can read.
//
// ctbrowser/aot/aot_helpers.def (with helpers/*.def) declares every runtime
// helper with its obligations. The dialect already names them: CTJS_RuntimeOp
// takes a helper_id enumerator, so an operation cannot claim a helper the
// runtime does not declare, and a wrong name is a C++ compile error. This
// header gives the lowering the rest: the symbol to call, the arity, and the
// role of each parameter and of the return value.
//
// The arity check catches what the name check cannot: an operation whose
// operand list has drifted from the helper's parameters. `get_property(object,
// key)` against a helper that grew a third parameter reads fine in both files
// and would produce a call with a garbage argument.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <cstddef>
#include <string_view>

#include <ctbrowser/aot/aot.hpp>

namespace ctcompile::ctjs {

/// Returns true if `text` starts with `prefix`.
///
/// std::string_view::starts_with is C++20, and this header cannot rely on it:
/// CTJSDialect is built by add_mlir_library at MLIR's own language standard,
/// while a C++23 test reads the same header.
[[nodiscard]] constexpr bool begins(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

/// What one ABI parameter is for.
///
/// The lowering has to supply every parameter of a helper, and they come from
/// different places: some are the operation's operands, one is the frame,
/// several are immediates held in an attribute, and a few have no source in
/// the IR at all. Naming the difference lets one pattern serve the whole table
/// instead of fifty hand-written ones.
///
/// Roles are derived from the .def's C parameter list, never written down a
/// second time. Both the types and the names are needed: every `uint32_t` in
/// the table is ambiguous by type alone (an argc, a slot, a length, a pad, a
/// register count or an opcode) and unambiguous by name.
enum class param_role {
    unknown,   ///< Never valid; the golden test asserts no parameter has it.
    frame,     ///< struct ct_aot_frame *: the enclosing ctjs.frame_enter.
    context,   ///< struct ct_aot_ctx *: the script context itself.
    operand,   ///< uint64_t: a JavaScript value, from the operation's operands.
    out_value, ///< uint64_t *: the result, returned through a pointer.
    out_other, ///< uint32_t*/int32_t*/double*/const char**: a non-value result.
    opcode,    ///< uint32_t op_kind / opcode: a bytecode opcode (see below).
    count,     ///< uint32_t argc/len/slot/pad/index/...: an immediate integer.
    number,    ///< double: an immediate float.
    text,      ///< const char *: a string's bytes.
    values,    ///< const uint64_t *: a run of values (argv, slots, upvalues).
    name,      ///< const struct ct_aot_name *: an interned property name.
    site,      ///< const struct ct_aot_site *: the baked diagnostic identity.
    cache,     ///< struct ct_aot_ic *: inline-cache storage, per site.
    storage,   ///< void *: caller-allocated frame space.
};

/// What one helper returns, and therefore how a caller learns it failed.
///
/// The helper table states the rule: "an unsigned return is data, never a
/// status" (helpers/values-and-properties.def). ct_aot_failed returns uint32_t
/// because its result is data, and ct_aot_bit_not keeps a status return,
/// although its only failure is a raise, because it is a safepoint; both
/// follow from the rule.
enum class return_role {
    unknown, ///< Never valid; a static_assert below checks no row has it.
    nothing, ///< void.
    status,  ///< int32_t: a ct_aot_status the caller must test.
    value,   ///< uint64_t: a JavaScript value.
    data,    ///< uint32_t: a length, a flag, a slot. Data, never a status.
    number,  ///< double.
    frame,   ///< struct ct_aot_frame *: ct_aot_enter only; NULL is the failure.
    values,  ///< uint64_t *: ct_aot_slots' span.
    name,    ///< const struct ct_aot_name *: ct_aot_intern_name.
};

// There are two kinds of failure, and a backend must not conflate them.
//
// raise() sets a flag no try/catch can see and unwinds the whole native chain;
// a thrown exception (thrown_ plus unwind_to_handler) completes inside the
// callee and is catchable (aot_helpers.def). The return type tells them apart,
// which is why it is derived here rather than guessed at each call site:
//
//   returns int32_t  ->  catchable. Test the status after the call.
//   may_throw, but returns data or a value  ->  raise only. The result is
//       always well-formed (allocate() still returns a real object past the
//       allocation ceiling), and the caller polls ct_aot_failed at loop
//       back-edges, not after every allocation: running past the ceiling is
//       wasteful, never unsafe.
//
// Getting this backwards fails silently either way. A status test after
// ct_aot_new_object tests a value, so any match is a coincidence; a missing
// back-edge poll lets a program past the allocation ceiling run forever
// instead of ending.

// `op_kind` is a ctbrowser::script::op, not a CTJS enum ordinal.
//
// The signature does not show this: aot_bridge/operators.cpp does
// `static_cast<op>(op_kind)` and dispatches the interpreter's own switch on
// it. Lowering `ctjs.binary pow` by passing the CTJS BinaryKind ordinal (5)
// would call the runtime with `op(5)` and compile `**` into a different
// operator. The opcode role exists so a backend must spell the opcode by name
// (see OpcodeMapping.hpp), which makes a renumbering of `enum class op` a
// build error rather than a miscompile.

/// One row of the helper table, classified.
struct runtime_helper {
    std::string_view symbol;
    /// Every parameter, including the frame handle, counted from the .def's
    /// C parameter list rather than kept by hand. Always equals role_count:
    /// both come from classify_all().
    std::size_t arity;
    bool may_throw;
    bool may_reenter;
    bool is_safepoint;
    /// What it returns, under the return-type rule; see return_role.
    return_role ret;
    /// One role per parameter, in ABI order. Twelve is wider than the widest
    /// row.
    param_role roles[12];
    std::size_t role_count;
};

/// One parameter's role, from its type and its name.
[[nodiscard]] constexpr param_role classify(std::string_view one) {
    while (!one.empty() && one.front() == ' ') { one.remove_prefix(1); }
    while (!one.empty() && one.back() == ' ') { one.remove_suffix(1); }
    if (one.empty()) { return param_role::unknown; }
    std::size_t split = one.size();
    while (split > 0 && one[split - 1] != ' ' && one[split - 1] != '*') { --split; }
    const std::string_view name = one.substr(split);

    if (begins(one, "struct ct_aot_frame *")) { return param_role::frame; }
    if (begins(one, "struct ct_aot_ctx *")) { return param_role::context; }
    if (begins(one, "const struct ct_aot_name *")) { return param_role::name; }
    if (begins(one, "const struct ct_aot_site *")) { return param_role::site; }
    if (begins(one, "struct ct_aot_ic *")) { return param_role::cache; }
    if (begins(one, "void *")) { return param_role::storage; }
    if (begins(one, "const uint64_t *")) { return param_role::values; }
    if (begins(one, "const char **")) { return param_role::out_other; }
    if (begins(one, "const char *")) { return param_role::text; }
    if (begins(one, "uint64_t *")) { return param_role::out_value; }
    if (begins(one, "uint32_t *") || begins(one, "int32_t *") || begins(one, "double *")) {
        return param_role::out_other;
    }
    if (begins(one, "uint64_t")) { return param_role::operand; }
    if (begins(one, "double")) { return param_role::number; }
    if (begins(one, "uint32_t")) {
        // The ambiguous type, resolved by name: every uint32_t in the table is
        // an opcode or an immediate integer, and only its name says which.
        if (name == "op_kind" || name == "opcode") { return param_role::opcode; }
        return param_role::count;
    }
    return param_role::unknown;
}

/// Classifies every top-level parameter in `params` (a parenthesised C
/// parameter list) into `into`, and returns how many there were: the row's
/// arity. Only top-level commas split, because a parameter's own type could
/// contain one (`std::function<void(int, int)>`); none does today, and a naive
/// count would then be wrong by one without anything saying so.
[[nodiscard]] constexpr std::size_t classify_all(std::string_view params, param_role * into,
                                                 std::size_t capacity) {
    std::size_t depth = 0;
    std::size_t start = 0;
    std::size_t count = 0;
    const auto take = [&](std::size_t end) {
        if (count < capacity) { into[count] = classify(params.substr(start, end - start)); }
        ++count;
    };
    for (std::size_t i = 0; i < params.size(); ++i) {
        const char c = params[i];
        if (c == '(' || c == '<' || c == '[') {
            if (++depth == 1) { start = i + 1; }
        } else if (c == ')' || c == '>' || c == ']') {
            if (depth == 1 && i > start) { take(i); }
            if (depth > 0) { --depth; }
        } else if (c == ',' && depth == 1) {
            take(i);
            start = i + 1;
        }
    }
    return count;
}

/// One row's return role, from the return type and the parameters together.
///
/// The parameters are needed because ct_aot_to_int32 returns a signed int32_t
/// that is data, not a status (ToInt32's result is signed by definition), and
/// it is the only int32_t-returning helper without a frame parameter (see its
/// row in helpers/values-and-properties.def). Every other int32_t row takes a
/// frame, so "int32_t with no frame is data" is an exact rule, not a
/// heuristic, and a second frameless int32_t row would be classified
/// correctly on arrival instead of special-cased by name.
///
/// Getting it wrong would be severe: a backend that read ct_aot_to_int32's
/// result as a status would test the value of `x | 0` against ct_aot_status,
/// where 1 is `failed` and unwinds the whole native chain, 2 is `caught` and
/// branches to a landing pad that does not exist, and 3 is `ok` by coincidence.
/// `2.7 | 0` is 2.
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
    if (begins(ret, "struct ct_aot_frame *")) { return return_role::frame; }
    if (begins(ret, "uint64_t *")) { return return_role::values; }
    if (begins(ret, "const struct ct_aot_name *")) { return return_role::name; }
    return return_role::unknown;
}

/// param_role's enumerators, spelled, in declaration order.
inline constexpr const char * param_role_names[] = {
    "unknown", "frame", "context", "operand", "out_value", "out_other", "opcode", "count",
    "number",  "text",  "values",  "name",    "site",      "cache",     "storage"};
static_assert(std::size(param_role_names) == static_cast<std::size_t>(param_role::storage) + 1);
static_assert(std::string_view{param_role_names[static_cast<std::size_t>(param_role::out_other)]} ==
              "out_other");

/// The spelling of `role`, for diagnostics and the golden test.
[[nodiscard]] constexpr const char * role_name(param_role role) {
    return param_role_names[static_cast<std::size_t>(role)];
}

// Expands each .def row into a classified runtime_helper.
#define CT_AOT_HELPER(name_, ret_, params_, may_throw_, may_reenter_, is_safepoint_)               \
    [] {                                                                                           \
        runtime_helper row{#name_,                                                                 \
                           0,                                                                      \
                           (may_throw_) != 0,                                                      \
                           (may_reenter_) != 0,                                                    \
                           (is_safepoint_) != 0,                                                   \
                           classify_return(#ret_, #params_),                                       \
                           {},                                                                     \
                           0};                                                                     \
        row.arity = row.role_count = classify_all(#params_, row.roles, std::size(row.roles));      \
        return row;                                                                                \
    }(),
/// Every helper, indexed by ctbrowser::aot::helper_id.
inline constexpr runtime_helper runtime_helpers[] = {
#include <ctbrowser/aot/aot_helpers.def>
};
#undef CT_AOT_HELPER

static_assert(std::size(runtime_helpers) == ctbrowser::aot::helper_count,
              "the lowering's helper table and helper_id disagree");

/// The table row for `which`.
[[nodiscard]] constexpr const runtime_helper & helper_for(ctbrowser::aot::helper_id which) {
    return runtime_helpers[static_cast<std::size_t>(which)];
}

/// Returns true if a caller must test a status after this call.
[[nodiscard]] constexpr bool returns_status(const runtime_helper & row) {
    return row.ret == return_role::status;
}

/// Returns true if this helper's only failure is a raise, so the caller takes
/// its result unconditionally and polls ct_aot_failed at the next back-edge.
///
/// ct_aot_enter is deliberately excluded: it fails by returning a NULL frame
/// pointer, which is neither a status nor a poll, and a backend that treated
/// it like the value-returning rows would dereference the null.
[[nodiscard]] constexpr bool is_raise_tier_only(const runtime_helper & row) {
    return row.may_throw && row.ret != return_role::status && row.ret != return_role::frame;
}

// Every row must classify. An unknown return type means the ABI grew a shape
// this file has not been taught, and the compiler would otherwise decide what
// to do about it by accident.
constexpr bool every_return_classifies() {
    for (const runtime_helper & row : runtime_helpers) {
        if (row.ret == return_role::unknown) { return false; }
    }
    return true;
}
static_assert(every_return_classifies(),
              "a helper's return type is not one this file knows - teach classify_return, and "
              "decide which failure tier the new shape belongs to before a backend guesses");

// ct_aot_failed is the raise signal and returns uint32_t data, not a status.
// If it returned int32_t, every poll would test it as a status, and since
// CT_AOT_OK is zero a raised program would read as healthy.
static_assert(helper_for(ctbrowser::aot::helper_id::ct_aot_failed).ret == return_role::data,
              "ct_aot_failed returns DATA - nonzero means the run is over; as a status, zero "
              "would mean the opposite");
static_assert(!returns_status(helper_for(ctbrowser::aot::helper_id::ct_aot_failed)));

// The status classifier, ct_aot_check, returns a status but is not
// may_throw: it is how a caller of any other row obtains one, so it cannot
// need one of its own.
static_assert(returns_status(helper_for(ctbrowser::aot::helper_id::ct_aot_check)));
static_assert(!helper_for(ctbrowser::aot::helper_id::ct_aot_check).may_throw);

// One row for each kind of failure: ct_aot_binary_op is catchable (a valueOf
// can throw), and ct_aot_new_object can only raise at the allocation ceiling.
static_assert(returns_status(helper_for(ctbrowser::aot::helper_id::ct_aot_binary_op)));
static_assert(!is_raise_tier_only(helper_for(ctbrowser::aot::helper_id::ct_aot_binary_op)));
static_assert(is_raise_tier_only(helper_for(ctbrowser::aot::helper_id::ct_aot_new_object)),
              "allocate() raises past the ceiling and STILL returns a real object, so there is "
              "no status here to test - only a poll to schedule");
static_assert(helper_for(ctbrowser::aot::helper_id::ct_aot_new_object).may_throw,
              "and it IS may_throw, which is exactly why may_throw alone cannot tell a backend "
              "what edge to emit");

// A frame or a context is always the first parameter, never a later one.
//
// CTJSABIShape's `leads_with_frame` reads only roles[0], which is correct only
// if no later position can hold a frame. This check fires if a row ever
// arrives that breaks that.
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
              "for leads_with_frame before a lowering decides for you");

// The one deliberate exception, pinned by name: ct_aot_to_int32 returns
// int32_t and is not a status, because ToInt32's result is signed by
// definition. Read as a status it would test the value of `x | 0` against
// ct_aot_status, and `2.7 | 0` is 2, which is `caught`.
static_assert(helper_for(ctbrowser::aot::helper_id::ct_aot_to_int32).ret == return_role::data,
              "ToInt32 returns DATA that happens to be signed - a backend testing it as a status "
              "branches on the arithmetic");
static_assert(!returns_status(helper_for(ctbrowser::aot::helper_id::ct_aot_to_int32)));
// Its sibling differs only in signedness and must stay data too.
static_assert(helper_for(ctbrowser::aot::helper_id::ct_aot_to_uint32).ret == return_role::data);

// ct_aot_enter's failure has a third shape and is excluded from both.
static_assert(helper_for(ctbrowser::aot::helper_id::ct_aot_enter).ret == return_role::frame);
static_assert(!returns_status(helper_for(ctbrowser::aot::helper_id::ct_aot_enter)));
static_assert(!is_raise_tier_only(helper_for(ctbrowser::aot::helper_id::ct_aot_enter)),
              "it signals the depth raise with a NULL frame pointer - neither a status to test "
              "nor a poll to defer");

} // namespace ctcompile::ctjs
