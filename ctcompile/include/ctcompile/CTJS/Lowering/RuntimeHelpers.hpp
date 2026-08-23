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

// Whether a stringified parameter list is "optionally a frame, then uint64_t".
[[nodiscard]] constexpr bool parameters_are_values_only(std::string_view params) {
    std::size_t depth = 0;
    std::size_t index = 0;
    std::size_t start = 0;
    bool ok = true;
    const auto check_one = [&](std::string_view one, std::size_t which) {
        // Trim.
        while (!one.empty() && one.front() == ' ') { one.remove_prefix(1); }
        while (!one.empty() && one.back() == ' ') { one.remove_suffix(1); }
        if (one.empty()) { return; }
        const bool is_frame =
            one.starts_with("struct ct_aot_frame *") || one.starts_with("struct ct_aot_ctx *");
        if (which == 0 && is_frame) { return; }
        if (!one.starts_with("uint64_t ")) { ok = false; }
    };
    for (std::size_t i = 0; i < params.size(); ++i) {
        const char c = params[i];
        if (c == '(') {
            ++depth;
            if (depth == 1) { start = i + 1; }
            continue;
        }
        if (c == ')') {
            if (depth == 1) {
                check_one(params.substr(start, i - start), index);
                ++index;
            }
            if (depth > 0) { --depth; }
            continue;
        }
        if (c == ',' && depth == 1) {
            check_one(params.substr(start, i - start), index);
            ++index;
            start = i + 1;
        }
    }
    return ok;
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
                           parameters_are_values_only(#params_),                                   \
                           {},                                                                     \
                           0};                                                                     \
        row.role_count = classify_all(#params_, row.roles, 12);                                    \
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

} // namespace ctcompile::ctjs
