// The AOT ABI - values and operators: the binary operators, the comparisons,
// the conversions, the unary rows, instanceof, and the failure poll.
//
// One of four files carved out of a 1,586-line aot_bridge.cpp on 2026-09-08.
// `struct aot_bridge` is declared in internal.hpp beside this; each of its
// bodies is defined in the file that also holds the extern "C" row calling
// it, so a wrapper and the member behind it are still one translation unit
// and the call still inlines. The four members every file calls - ctx_of,
// frame_of, frame_record and check - stay in-class in the header for the
// same reason.

#include "internal.hpp"

namespace ctbrowser::script {

// ct_aot_binary_op_static. Phase 5's first extracted semantic helper: the
// seven non-re-entering binary operations, delegating to the SAME
// context::binary_op_static the interpreter now calls.
//
// That identity is the point of the phase. A helper that reimplemented
// `add` would agree with the interpreter on the day it was written and
// drift afterwards, and the difference would surface as a Phase 12A oracle
// mismatch on some page - not as a build error.
std::int32_t aot_bridge::binary_op_static(aot::ct_aot_frame * f, std::uint32_t op_kind,
                                          std::uint64_t lhs, std::uint64_t rhs,
                                          std::uint64_t * out) {
    aot_frame_storage & held = frame_of(f);
    context & cx = *held.ctx;
    // AN OP KIND OUT OF AN IMAGE IS UNTRUSTED INPUT. The interpreter only
    // ever passes the seven; a compiled body's operand came from a file.
    // binary_op_static returns undefined for anything else, and checking
    // the range here as well means the cast below cannot name an
    // enumerator that does not exist.
    if (op_kind >= opcode_count) {
        *out = value::undefined().bits();
        return static_cast<std::int32_t>(aot::ct_aot_status::ok);
    }
    const value produced =
        cx.binary_op_static(static_cast<op>(op_kind), value::from_bits(lhs), value::from_bits(rhs));
    const std::int32_t status = check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

// ct_aot_binary_op. Phase 5's second extracted semantic helper: the seven
// that CAN run page JavaScript, through the same context::binary_op the
// interpreter now calls.
//
// The status test is not decoration here. Any of these can run a user
// valueOf or toString, which can throw, and can call something that
// exhausts the stack - so unlike the static family this one genuinely
// reaches its non-ok arms.
std::int32_t aot_bridge::binary_op(aot::ct_aot_frame * f, std::uint32_t op_kind, std::uint64_t lhs,
                                   std::uint64_t rhs, std::uint64_t * out) {
    aot_frame_storage & held = frame_of(f);
    context & cx = *held.ctx;
    if (op_kind >= opcode_count) {
        *out = value::undefined().bits();
        return static_cast<std::int32_t>(aot::ct_aot_status::ok);
    }
    const value produced =
        cx.binary_op(static_cast<op>(op_kind), value::from_bits(lhs), value::from_bits(rhs));
    const std::int32_t status = check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

// ---- THE ROWS THAT NEEDED NO EXTRACTION AT ALL ----------------------
//
// Phase 5 is mostly about lifting semantics out of the interpreter's
// handlers. These are the rows where the lift was already done - the
// runtime already had the function, and what was missing was the ABI shim
// in front of it.
//
// BATCHED, and the plan's one-helper-per-commit rule is about EXTRACTIONS
// rather than about this: an extraction can change the VM's behaviour and
// the discipline exists to keep that diagnosable. A shim adds a symbol and
// touches no handler, so it cannot.

// ct_aot_failed. The uncatchable-failure poll, one load of one bool, so
// that it can sit on back edges rather than after every allocation.
std::uint32_t aot_bridge::failed(aot::ct_aot_frame * f) {
    return frame_of(f).ctx->failed() ? 1u : 0u;
}

std::int32_t aot_bridge::loose_equals(aot::ct_aot_frame * f, std::uint64_t a, std::uint64_t b,
                                      std::uint32_t * out) {
    context & cx = *frame_of(f).ctx;
    const bool equal = cx.loose_equals(value::from_bits(a), value::from_bits(b));
    *out = equal ? 1u : 0u;
    return check(f);
}

// ct_aot_compare. The four relational opcodes are constant comparisons
// against the ordering this writes, so the NUMBERS matter and are taken
// from the row rather than from std::partial_ordering's representation.
std::int32_t aot_bridge::compare(aot::ct_aot_frame * f, std::uint64_t lhs, std::uint64_t rhs,
                                 std::int32_t * out_ordering) {
    context & cx = *frame_of(f).ctx;
    const std::partial_ordering ord =
        cx.compare_relational(value::from_bits(lhs), value::from_bits(rhs));
    // UNORDERED FIRST. A NaN on either side is unordered with everything
    // INCLUDING ITSELF, so testing `ord == less` first would be right and
    // testing `!(ord > 0)` for `<=` would not - which is exactly the
    // mistake that makes `NaN <= NaN` true.
    *out_ordering = static_cast<std::int32_t>(
        ord == std::partial_ordering::unordered ? aot::ct_aot_ordering::unordered
        : ord == std::partial_ordering::less    ? aot::ct_aot_ordering::less
        : ord == std::partial_ordering::greater ? aot::ct_aot_ordering::greater
                                                : aot::ct_aot_ordering::equivalent);
    return check(f);
}

std::int32_t aot_bridge::to_number(aot::ct_aot_frame * f, std::uint64_t v, double * out) {
    context & cx = *frame_of(f).ctx;
    *out = cx.to_number_value(value::from_bits(v));
    return check(f);
}

std::uint32_t aot_bridge::instance_of(aot::ct_aot_frame * f, std::uint64_t target,
                                      std::uint64_t ctor) {
    context & cx = *frame_of(f).ctx;
    return cx.instance_of(value::from_bits(target), value::from_bits(ctor)) ? 1u : 0u;
}

// ct_aot_negate. `-x` through the same context::negate_value the
// interpreter now calls.
//
// NOT ct_aot_to_number PLUS AN fneg, which is the lowering the shape
// invites. The BigInt arm ALLOCATES a fresh bigint_object, so the answer
// may be an unrooted heap value and the caller must park it - which is why
// this row's out-parameter is a uint64_t VALUE while ct_aot_to_number's is
// a double.
//
// NO OPERAND RANGE CHECK, unlike the two binary rows: there is no op_kind
// out of an untrusted image here, only a value, and every bit pattern is a
// legal one.
std::int32_t aot_bridge::negate(aot::ct_aot_frame * f, std::uint64_t v, std::uint64_t * out) {
    context & cx = *frame_of(f).ctx;
    const value produced = cx.negate_value(value::from_bits(v));
    const std::int32_t status = check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

// ct_aot_bit_not. The same shape, and the same reason for the value
// out-parameter: `~1n` allocates.
//
// ITS may_throw IS THE CEILING, NOT A CATCHABLE THROW. The row says so:
// `~1n` computes and `~obj` is -1, so neither arm has a JS-catchable
// throw - an AOT backend needs a FAULT edge here, not an exception edge,
// and only on the BigInt arm.
std::int32_t aot_bridge::bit_not(aot::ct_aot_frame * f, std::uint64_t v, std::uint64_t * out) {
    context & cx = *frame_of(f).ctx;
    const value produced = cx.bit_not_value(value::from_bits(v));
    const std::int32_t status = check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

} // namespace ctbrowser::script

namespace ctbrowser::aot {

extern "C" {

std::int32_t ct_aot_binary_op(ct_aot_frame * fr, std::uint32_t op_kind, std::uint64_t lhs,
                              std::uint64_t rhs, std::uint64_t * out) {
    return script::aot_bridge::binary_op(fr, op_kind, lhs, rhs, out);
}

std::int32_t ct_aot_binary_op_static(ct_aot_frame * fr, std::uint32_t op_kind, std::uint64_t lhs,
                                     std::uint64_t rhs, std::uint64_t * out) {
    return script::aot_bridge::binary_op_static(fr, op_kind, lhs, rhs, out);
}

std::uint32_t ct_aot_failed(ct_aot_frame * fr) {
    return script::aot_bridge::failed(fr);
}

// PURE, AND THEREFORE HANDLE-FREE. These four take no frame because they
// cannot throw, cannot re-enter and cannot collect - which is what lets a
// backend fold them away entirely when it has proven the operand's type.
std::uint32_t ct_aot_truthy(std::uint64_t v) {
    return script::context::truthy(script::value::from_bits(v)) ? 1u : 0u;
}

std::uint32_t ct_aot_strict_equals(std::uint64_t a, std::uint64_t b) {
    return script::value::from_bits(a).strict_equals(script::value::from_bits(b)) ? 1u : 0u;
}

std::int32_t ct_aot_to_int32(std::uint64_t v) {
    return script::context::to_int32(script::value::from_bits(v));
}

std::uint32_t ct_aot_to_uint32(std::uint64_t v) {
    return script::context::to_uint32(script::value::from_bits(v));
}

double ct_aot_to_number_primitive(std::uint64_t v) {
    return script::context::to_number(script::value::from_bits(v));
}

double ct_aot_exponentiate(double base, double exponent) {
    return script::context::exponentiate(base, exponent);
}

// THE RETURN SLOT CARRIES A LENGTH, and *out_text points at static storage:
// context::type_of returns a string_view over string literals, which is what
// lets `typeof x === 'function'` be a length compare and a memcmp with no
// allocation at all.
std::uint32_t ct_aot_type_of_name(std::uint64_t v, const char ** out_text) {
    const std::string_view text = script::context::type_of(script::value::from_bits(v));
    *out_text = text.data();
    return static_cast<std::uint32_t>(text.size());
}

std::int32_t ct_aot_loose_equals(ct_aot_frame * fr, std::uint64_t a, std::uint64_t b,
                                 std::uint32_t * out) {
    return script::aot_bridge::loose_equals(fr, a, b, out);
}

std::int32_t ct_aot_compare(ct_aot_frame * fr, std::uint64_t lhs, std::uint64_t rhs,
                            std::int32_t * out_ordering) {
    return script::aot_bridge::compare(fr, lhs, rhs, out_ordering);
}

std::int32_t ct_aot_to_number(ct_aot_frame * fr, std::uint64_t v, double * out) {
    return script::aot_bridge::to_number(fr, v, out);
}

std::int32_t ct_aot_negate(ct_aot_frame * fr, std::uint64_t v, std::uint64_t * out) {
    return script::aot_bridge::negate(fr, v, out);
}

std::int32_t ct_aot_bit_not(ct_aot_frame * fr, std::uint64_t v, std::uint64_t * out) {
    return script::aot_bridge::bit_not(fr, v, out);
}

std::uint32_t ct_aot_instance_of(ct_aot_frame * fr, std::uint64_t target, std::uint64_t ctor) {
    return script::aot_bridge::instance_of(fr, target, ctor);
}

} // extern "C"

} // namespace ctbrowser::aot
