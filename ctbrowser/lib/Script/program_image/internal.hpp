#pragma once
// Private to lib/Script/program_image/. NOT installed and in no file set:
// include/ctbrowser/script/program_image.hpp is the public header and did not
// change. This exists so the format can be two files - write.cpp and read.cpp,
// carved out of program_image.cpp on 2026-09-08 - without the writer and the
// reader drifting: the magic, the format version and the one refusal both sides
// make are here and nowhere else. The includes are program_image.cpp's, so both
// files see exactly what it saw.

#include <boost/hash2/xxhash.hpp>
#include <cstring>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/program_image.hpp>
#include <unordered_map>

// The format, in one header so the writer and the reader cannot drift.
//
// EVERYTHING IS LITTLE-ENDIAN AND EXPLICIT. Nothing is memcpy'd out of a struct,
// and that is not fastidiousness: `instruction` is {op at 0, PADDING at 1, a at
// 2, b at 4, c at 6} with alignof 2, and the padding byte measurably differs
// between -O0 and -O2 builds of the code that BUILT the instruction. A raw copy
// would produce different images for the same program depending on how the
// writer was compiled, while every correctness test passed. So op, a, b and c
// are written as four fields. ctcompile/include/ctcompile/JavaScript/
// EngineContract.hpp asserts the representation is not canonical, so this
// reasoning is enforced rather than remembered.

namespace ctbrowser::script::detail {

constexpr std::uint32_t magic = 0x43544243; // 'CTBC'

// 3 records `program::kind`. 2 dropped `function_proto::nested`, a per-function
// table of always zero.
// THE LAYOUT CHANGED, which is what this number is for and what the source hash
// tag below deliberately is not: an image written by build 1 has four bytes per
// function that build 2 would read as the next field, so the right refusal
// names the format. The version check runs before the fingerprint for exactly
// this reason.
constexpr std::uint32_t format_version = 4;

// WHAT A CONSTANT IS ALLOWED TO BE - one rule, read by the writer and by the
// reader, because a pool entry is EIGHT BYTES OF A FILE REINTERPRETED AS A
// VALUE and that is the narrowest place in this format.
//
// The rule used to be `is_heap()` alone, which is a pure bit test and correct as
// far as it goes - and one quiet-bit short of enough. A NaN-boxed value is a NaN
// with bits 51 and 50 set (value.hpp's qnan_mask); a NaN with bit 50 set and bit
// 51 CLEAR is therefore an ordinary number to every predicate here, and the
// first arithmetic operation on it QUIETS bit 51:
//
//     0xFFF4000000000003   loaded: is_number() true, is_heap() false
//     - 1                  the hardware quiets bit 51
//     0xFFFC000000000003   is_heap() TRUE, as_heap() = 0x3, from the file
//
// Reproduced end to end: eight patched bytes in a `.ctapp` made `ctrun` fault in
// `context::type_of` at an address the file chose. The engine had already met
// this mechanism arriving through a Float64Array and canonicalises there
// (`view_get`, and unittests/js/number_basics.cpp); an image is the same
// boundary and had no such rule.
//
// REFUSED RATHER THAN CANONICALISED, which is a decision and rests on a fact
// about the compiler rather than on taste. `add_constant` is reached from one
// place - `emit_const`, compile/helpers.cpp:17 - and every caller of it passes
// `value::number(...)` or `value::boolean(...)`; the only non-integral number
// among them is `number_literal(text)`, which returns a parsed double, zero, or
// +/-Infinity from `out_of_range_value`, and has NO PATH TO NaN. A source cannot
// spell a NaN literal either: `NaN` is a global identifier, `0/0` is a runtime
// division, and nothing here folds constants. So no legitimate image contains
// one, refusing costs nothing, and it keeps this file's own promise that a
// corrupt image is refused rather than run - where canonicalising would run a
// tampered image with a silently different number in it.
//
// THE CANONICAL NaN IS STILL ACCEPTED, so that the day something does learn to
// fold `0/0` the shape it should emit already loads. Note that is 0x7FF8...
// exactly: x86-64's own default NaN for 0.0/0.0 is 0xFFF8..., and it is refused
// too, because a rule about WHICH payloads are dangerous is a rule that has
// already been wrong once.
//
// Returns nullptr when the bits are a value a pool may hold, and otherwise the
// tail of the sentence "constant N ...".
[[nodiscard]] constexpr const char * why_not_a_constant(std::uint64_t bits) noexcept {
    const value v = value::from_bits(bits);
    // is_heap(), NOT is_object(): is_object asks the pointed-to object what KIND
    // it is, which dereferences a pointer that came out of a file. Every test
    // here is a pure bit test, which is the only kind that is safe to run on
    // bytes nobody has validated yet.
    //
    // The pool holds immediates only - a string literal lives in `strings` and
    // is materialised by the VM - so a boxed pointer here is a file handing the
    // engine an address to dereference.
    if (v.is_heap()) { return "carries a heap pointer; the pool holds immediates only"; }
    if (v.is_number()) {
        // IEEE-754: a maximal exponent with any non-zero mantissa is a NaN, and
        // there are 2^52 of them. Written as bits rather than std::isnan because
        // this runs on unvalidated input and because the comparison below is
        // against a bit pattern, not against a value - every NaN compares
        // unequal to every NaN, this one included.
        constexpr std::uint64_t exponent = 0x7FF0'0000'0000'0000ull;
        constexpr std::uint64_t mantissa = 0x000F'FFFF'FFFF'FFFFull;
        if ((bits & exponent) == exponent && (bits & mantissa) != 0 && bits != canonical_nan_bits) {
            return "is a NaN carrying a payload - one arithmetic operation quiets bit 51 and "
                   "turns it into a boxed tag or a heap pointer";
        }
        return nullptr;
    }
    // NOT A NUMBER AND NOT A POINTER, so it matches the boxed pattern and must
    // be one of the four tags. Anything else is a value with no meaning at all:
    // `is_kind` says no to every kind, `typeof` falls off the end of its own
    // switch, and nothing in the engine describes what it holds. The old gate
    // let every one of them through.
    if (v.is_undefined() || v.is_null() || v.is_boolean()) { return nullptr; }
    return "is a tag no value has - it is neither a number, nor undefined, null, false or "
           "true, nor a heap pointer";
}

} // namespace ctbrowser::script::detail
