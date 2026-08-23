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
};

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
    runtime_helper{#name_,                                                                         \
                   count_parameters(#params_),                                                     \
                   (may_throw_) != 0,                                                              \
                   (may_reenter_) != 0,                                                            \
                   (is_safepoint_) != 0,                                                           \
                   parameters_are_values_only(#params_)},
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
