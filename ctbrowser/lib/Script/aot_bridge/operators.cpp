//===- operators.cpp - AOT helpers for values and operators ---------------===//
//
// The AOT runtime helpers for values and operators: binary operators,
// comparisons, conversions, unary operators, instanceof and the failure poll.
// Each extern "C" function implements one row (one CT_AOT_HELPER entry) of
// aot_helpers.def; internal.hpp explains how a row reaches the VM's state.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::aot {

extern "C" {

/// ct_aot_binary_op_static: the seven binary operators that cannot run page
/// JavaScript. It calls the same context::binary_op_static the interpreter
/// uses. Sharing that function is deliberate: a separate implementation of
/// `add` would drift from the interpreter, and the difference would surface
/// only as compiled code disagreeing with the interpreter on some page, not as
/// a build error.
std::int32_t ct_aot_binary_op_static(ct_aot_frame * f, std::uint32_t op_kind, std::uint64_t lhs,
                                     std::uint64_t rhs, std::uint64_t * out) {
    const aot_frame_storage & held = aot_bridge::frame_of(f);
    context & cx = *held.ctx;
    // An op kind read from an image is untrusted input. The interpreter only
    // ever passes the seven, but a compiled body's operand came from a file.
    // binary_op_static returns undefined for anything else, and checking the
    // range here as well means the cast below cannot name an enumerator that
    // does not exist.
    if (op_kind >= opcode_count) {
        *out = value::undefined().bits();
        return static_cast<std::int32_t>(aot::ct_aot_status::ok);
    }
    const value produced =
        cx.binary_op_static(static_cast<op>(op_kind), value::from_bits(lhs), value::from_bits(rhs));
    const std::int32_t status = aot_bridge::check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

/// ct_aot_binary_op: the seven binary operators that can run page JavaScript
/// (through a user valueOf or toString). It calls the same context::binary_op
/// the interpreter uses.
///
/// The status test matters here. Any of these can run user code that throws or
/// exhausts the stack, so unlike the static family this one really reaches its
/// non-ok arms.
std::int32_t ct_aot_binary_op(ct_aot_frame * f, std::uint32_t op_kind, std::uint64_t lhs,
                              std::uint64_t rhs, std::uint64_t * out) {
    const aot_frame_storage & held = aot_bridge::frame_of(f);
    context & cx = *held.ctx;
    if (op_kind >= opcode_count) {
        *out = value::undefined().bits();
        return static_cast<std::int32_t>(aot::ct_aot_status::ok);
    }
    const value produced =
        cx.binary_op(static_cast<op>(op_kind), value::from_bits(lhs), value::from_bits(rhs));
    const std::int32_t status = aot_bridge::check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

// The helpers below are thin wrappers: the runtime already had each function,
// so the helper only adapts it to the AOT calling convention.

/// ct_aot_failed: the uncatchable-failure poll. It is one load of one bool, so
/// it can sit on loop back edges rather than after every allocation.
std::uint32_t ct_aot_failed(ct_aot_frame * f) {
    return aot_bridge::frame_of(f).ctx->failed() ? 1u : 0u;
}

std::int32_t ct_aot_loose_equals(ct_aot_frame * f, std::uint64_t a, std::uint64_t b,
                                 std::uint32_t * out) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    const bool equal = cx.loose_equals(value::from_bits(a), value::from_bits(b));
    *out = equal ? 1u : 0u;
    return aot_bridge::check(f);
}

/// ct_aot_compare. The four relational opcodes are constant comparisons
/// against the ordering this writes, so the numbers matter and are taken from
/// the row's ct_aot_ordering rather than from std::partial_ordering's
/// representation.
std::int32_t ct_aot_compare(ct_aot_frame * f, std::uint64_t lhs, std::uint64_t rhs,
                            std::int32_t * out_ordering) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    const std::partial_ordering ord =
        cx.compare_relational(value::from_bits(lhs), value::from_bits(rhs));
    // Test unordered first. A NaN on either side is unordered with everything,
    // itself included, so testing `ord == less` first would be right but
    // testing `!(ord > 0)` for `<=` would not: that is exactly the mistake that
    // makes `NaN <= NaN` true.
    *out_ordering = static_cast<std::int32_t>(
        ord == std::partial_ordering::unordered ? aot::ct_aot_ordering::unordered
        : ord == std::partial_ordering::less    ? aot::ct_aot_ordering::less
        : ord == std::partial_ordering::greater ? aot::ct_aot_ordering::greater
                                                : aot::ct_aot_ordering::equivalent);
    return aot_bridge::check(f);
}

std::int32_t ct_aot_to_number(ct_aot_frame * f, std::uint64_t v, double * out) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    *out = cx.to_number_value(value::from_bits(v));
    return aot_bridge::check(f);
}

std::uint32_t ct_aot_instance_of(ct_aot_frame * f, std::uint64_t target, std::uint64_t ctor) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    return cx.instance_of(value::from_bits(target), value::from_bits(ctor)) ? 1u : 0u;
}

/// ct_aot_negate: `-x`, through the same context::negate_value the
/// interpreter calls.
///
/// This is not ct_aot_to_number plus an fneg, the lowering the shape invites.
/// The BigInt arm allocates a fresh bigint_object, so the answer may be an
/// unrooted heap value the caller must park; that is why this row's
/// out-parameter is a uint64_t value while ct_aot_to_number's is a double.
///
/// There is no operand range check, unlike the two binary rows: no op_kind
/// comes out of an untrusted image here, only a value, and every bit pattern
/// is a legal one.
std::int32_t ct_aot_negate(ct_aot_frame * f, std::uint64_t v, std::uint64_t * out) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    const value produced = cx.negate_value(value::from_bits(v));
    const std::int32_t status = aot_bridge::check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

/// ct_aot_bit_not: `~x`. The same shape, and the same reason for the value
/// out-parameter: `~1n` allocates.
///
/// Its may_throw flag is a ceiling, not a catchable throw. As the row says,
/// `~1n` computes and `~obj` is -1, so neither arm throws anything JavaScript
/// can catch: an AOT backend needs a fault edge here, not an exception edge,
/// and only on the BigInt arm.
std::int32_t ct_aot_bit_not(ct_aot_frame * f, std::uint64_t v, std::uint64_t * out) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    const value produced = cx.bit_not_value(value::from_bits(v));
    const std::int32_t status = aot_bridge::check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

// Pure, and therefore handle-free. These rows take no frame because they
// cannot throw, re-enter or collect, which lets a backend fold them away
// entirely once it has proven the operand's type.
std::uint32_t ct_aot_truthy(std::uint64_t v) {
    return context::truthy(value::from_bits(v)) ? 1u : 0u;
}

std::uint32_t ct_aot_strict_equals(std::uint64_t a, std::uint64_t b) {
    return value::from_bits(a).strict_equals(value::from_bits(b)) ? 1u : 0u;
}

std::int32_t ct_aot_to_int32(std::uint64_t v) {
    return context::to_int32(value::from_bits(v));
}

std::uint32_t ct_aot_to_uint32(std::uint64_t v) {
    return context::to_uint32(value::from_bits(v));
}

double ct_aot_to_number_primitive(std::uint64_t v) {
    return context::to_number(value::from_bits(v));
}

double ct_aot_exponentiate(double base, double exponent) {
    return context::exponentiate(base, exponent);
}

/// Returns typeof's answer as a length, with *out_text pointing at static
/// storage: context::type_of returns a string_view over string literals, so
/// `typeof x === 'function'` becomes a length compare and a memcmp with no
/// allocation.
std::uint32_t ct_aot_type_of_name(std::uint64_t v, const char ** out_text) {
    const std::string_view text = context::type_of(value::from_bits(v));
    *out_text = text.data();
    return static_cast<std::uint32_t>(text.size());
}

} // extern "C"

} // namespace ctbrowser::aot
