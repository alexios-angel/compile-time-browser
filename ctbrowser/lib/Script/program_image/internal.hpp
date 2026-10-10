//===- program_image/internal.hpp - Shared image format rules ---*- C++ -*-===//
//
// Private to lib/Script/program_image/ (not installed; the public header is
// include/ctbrowser/script/program_image.hpp). Holds what write.cpp and
// read.cpp must agree on - the magic, the format version and the constant
// validation rule - so the writer and reader cannot drift. The includes
// cover what both files need.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <boost/hash2/xxhash.hpp>
#include <cstring>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/program_image.hpp>
#include <unordered_map>

// Everything is little-endian and written field by field; nothing is
// memcpy'd out of a struct. `instruction` is {op at 0, padding at 1, a at 2,
// b at 4, c at 6} with alignof 2, and the padding byte differs between -O0
// and -O2 builds of the code that built the instruction, so a raw copy would
// produce different images for the same program. op, a, b and c are written
// as four fields. ctcompile/include/ctcompile/JavaScript/EngineContract.hpp
// asserts the representation is not canonical, so this stays enforced.

namespace ctbrowser::script::detail {

/// The image's first u32: "CTBC" read as a big-endian number (stored
/// little-endian, so the file starts with the bytes "CBTC").
constexpr std::uint32_t magic = 0x43544243;

/// Bumped whenever the byte layout changes. The version is checked before the
/// source fingerprint, so an old image is refused with a message that names
/// the format rather than misread as later fields. Notable versions:
///   2: dropped function_proto::nested (a per-function table, always zero).
///   3: program::kind.
///   6: function_proto::length.
///   7: program::lexical_names and function_names, after hoisted_vars.
///   8: program::annex_b_names after those, and function_proto::rethrows
///      after each function's code_offsets.
constexpr std::uint32_t format_version = 8;

/// What a constant-pool entry may be. One rule, used by both writer and
/// reader, because a pool entry is eight bytes of a file reinterpreted as a
/// value.
///
/// A heap pointer is refused, and so is any NaN other than the canonical one.
/// A NaN-boxed value is a NaN with bits 51 and 50 set (value.hpp's
/// qnan_mask); a NaN with bit 50 set and bit 51 clear is an ordinary number
/// to every predicate here, but the first arithmetic operation on it quiets
/// bit 51 and turns it into a boxed value:
///
///     0xFFF4000000000003   loaded: is_number() true, is_heap() false
///     - 1                  the hardware quiets bit 51
///     0xFFFC000000000003   is_heap() true, as_heap() = 0x3, from the file
///
/// so a patched image could make the engine dereference an address of its
/// choosing. (A Float64Array read is the same boundary and canonicalises
/// instead; see `view_get`.)
///
/// Such constants are refused rather than canonicalised because the compiler
/// never emits them: `add_constant` is reached only from `emit_const`, whose
/// callers pass `value::number(...)` or `value::boolean(...)`, and the only
/// non-integral source is `number_literal`, which returns a parsed double,
/// zero or +/-Infinity, never NaN. Source cannot spell a NaN literal either
/// (`NaN` is a global identifier and nothing folds `0/0`). So no legitimate
/// image contains one, and canonicalising would silently run a tampered
/// image with a different number in it.
///
/// The canonical NaN (exactly 0x7FF8000000000000) is still accepted, so that
/// constant folding of `0/0` would already load. x86-64's default NaN,
/// 0xFFF8000000000000, is refused like any other payload.
///
/// Returns nullptr when the bits are a value a pool may hold, and otherwise
/// the tail of the sentence "constant N ...".
[[nodiscard]] constexpr const char * why_not_a_constant(std::uint64_t bits) noexcept {
    const value v = value::from_bits(bits);
    // is_heap(), not is_object(): is_object asks the pointed-to object what
    // kind it is, which dereferences a pointer that came out of a file. Every
    // test here is a pure bit test, the only kind safe to run on bytes nobody
    // has validated yet.
    //
    // The pool holds immediates only (a string literal lives in `strings` and
    // is materialised by the VM), so a boxed pointer here is a file handing
    // the engine an address to dereference.
    if (v.is_heap()) { return "carries a heap pointer; the pool holds immediates only"; }
    if (v.is_number()) {
        // IEEE-754: a maximal exponent with any non-zero mantissa is a NaN
        // (there are 2^52 - 1 bit patterns per sign). Written as bit tests
        // rather than std::isnan because the comparison below is against a
        // bit pattern, not a value: every NaN compares unequal to every NaN.
        constexpr std::uint64_t exponent = 0x7FF0'0000'0000'0000ull;
        constexpr std::uint64_t mantissa = 0x000F'FFFF'FFFF'FFFFull;
        if ((bits & exponent) == exponent && (bits & mantissa) != 0 && bits != canonical_nan_bits) {
            return "is a NaN carrying a payload - one arithmetic operation quiets bit 51 and "
                   "turns it into a boxed tag or a heap pointer";
        }
        return nullptr;
    }
    // Neither a number nor a pointer, so it must be one of the four tags
    // (undefined, null, false, true). Any other bit pattern has no meaning:
    // `is_kind` rejects every kind and `typeof` has no case for it.
    if (v.is_undefined() || v.is_null() || v.is_boolean()) { return nullptr; }
    return "is a tag no value has - it is neither a number, nor undefined, null, false or "
           "true, nor a heap pointer";
}

} // namespace ctbrowser::script::detail
