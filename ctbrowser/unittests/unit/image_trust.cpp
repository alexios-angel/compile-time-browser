// THE CONSTANT POOL IS ATTACKER-CONTROLLED BYTES, and one bit of it used to be
// a pointer.
//
// A program image is executable input. `load_image` is the only thing between a
// `.ctapp` on disk and the VM, and its constant-pool check was a single
// `is_heap()` bit test - correct as far as it went, and one quiet-bit short of
// enough.
//
//     0xFFF4000000000003   bit 51 CLEAR, bit 50 set, sign set
//                          -> is_number() is TRUE, is_heap() is FALSE, loaded
//     - 1                  the hardware quiets bit 51
//     0xFFFC000000000003   -> is_heap() is TRUE, as_heap() = 0x3
//
// So the first arithmetic operation on the loaded constant turns eight bytes of
// the file into a 48-bit pointer the engine then dereferences. Reproduced end to
// end before this test existed: eight patched bytes in a `.ctapp` bundle made
// `ctrun` SIGSEGV inside `context::type_of` at `si_addr = 0xb` - the payload
// `0x3` plus the offset of `kind` past the vptr - so the bytes in the file chose
// the address.
//
// The engine had met this mechanism once already, from the other direction: a
// Float64Array lets a script write any bit pattern and read it back as a double,
// and `view_get` canonicalises every NaN it returns for exactly this reason
// (value.hpp's canonical_nan_bits, and unittests/js/number_basics.cpp). The
// loader is the same boundary and had no such rule.
//
// WHAT THIS FILE PINS: that the loader REFUSES, rather than canonicalising. See
// the justification block above `legitimate_programs_carry_no_nan_constant` -
// it is the measurement the choice rests on, and it is here rather than in a
// comment because "the compiler cannot emit that" is exactly the kind of claim
// that stops being true without anyone noticing.
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/program_image.hpp>

#include "check.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using ctbrowser::script::canonical_nan_bits;
using ctbrowser::script::compiler;
using ctbrowser::script::image_option;
using ctbrowser::script::load_image;
using ctbrowser::script::program;
using ctbrowser::script::value;

namespace {

// A literal chosen for its BITS, not its value: eight bytes that occur exactly
// once in the image, so the patch below can find the constant without a second
// copy of the format's field order. The count is asserted, never assumed.
constexpr std::string_view one_odd_constant = "var x = 1.2345678901234567e-97; x + 1;\n";
constexpr std::uint64_t odd_constant_bits = 0x2BD0'E07E'C6BC'012Full;

// Every position at which these eight bytes appear, little-endian.
[[nodiscard]] std::vector<std::size_t> find_u64(const std::vector<std::byte> & haystack,
                                                std::uint64_t needle) {
    std::vector<std::size_t> at;
    if (haystack.size() < 8) { return at; }
    for (std::size_t i = 0; i + 8 <= haystack.size(); ++i) {
        std::uint64_t got = 0;
        for (std::size_t b = 0; b < 8; ++b) {
            got |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(haystack[i + b]))
                   << (8 * b);
        }
        if (got == needle) { at.push_back(i); }
    }
    return at;
}

void put_u64(std::vector<std::byte> & into, std::size_t at, std::uint64_t bits) {
    for (std::size_t b = 0; b < 8; ++b) {
        into[at + b] = static_cast<std::byte>(static_cast<std::uint8_t>(bits >> (8 * b)));
    }
}

// --- 1. the mechanism, on this machine ------------------------------------
//
// Not a claim about IEEE-754 read out of a manual: the payload is quieted here,
// by this build's own arithmetic, and the result is checked against the boxing
// scheme's own predicates. Pure arithmetic on a local - nothing is dereferenced,
// so this is safe to run with or without the loader fix.
void arithmetic_really_forges_a_pointer() {
    constexpr std::uint64_t signalling = 0xFFF4'0000'0000'0003ull;
    const value as_loaded = value::from_bits(signalling);
    CHECK(as_loaded.is_number());
    CHECK(!as_loaded.is_heap());

    const double quieted = std::bit_cast<double>(signalling) - 1.0;
    const value after = value::from_bits(std::bit_cast<std::uint64_t>(quieted));
    // THE WHOLE DEFECT IN TWO LINES. A number went into an arithmetic
    // instruction and a heap pointer came out, with a payload the file chose.
    CHECK(after.is_heap());
    CHECK_EQ(after.bits() & 0x0000'FFFF'FFFF'FFFFull, 0x3ull);

    // The Float64Array twin, sign clear: this one quiets into tag_true, which is
    // what number_basics.cpp caught at `view_get`.
    const double twin = std::bit_cast<double>(0x7FF4'0000'0000'0003ull) - 1.0;
    CHECK(value::from_bits(std::bit_cast<std::uint64_t>(twin)).is_boolean());

    // And the canonical NaN is exactly the pattern that does NOT do this,
    // which is why it is the one shape the loader lets through.
    const double safe = std::bit_cast<double>(canonical_nan_bits) - 1.0;
    const value safe_after = value::from_bits(std::bit_cast<std::uint64_t>(safe));
    CHECK(safe_after.is_number());
    CHECK(!safe_after.is_heap());
    CHECK(!safe_after.is_boolean());
}

// --- 2. the justification for REFUSING rather than canonicalising ----------
//
// Refusing a bit pattern the compiler itself emits would break real programs, so
// the choice rests on a fact about the compiler: `add_constant` is reached from
// exactly one place (compile/helpers.cpp's `emit_const`), every caller of which
// passes `value::number(...)` or `value::boolean(...)`, and the only non-integer
// number among them is `number_literal(text)` - which returns a parsed double,
// zero, or +/-Infinity from `out_of_range_value`, and has no path to NaN. A
// JavaScript source cannot spell a NaN LITERAL either: `NaN` is a global
// identifier, `0/0` is a runtime division, and this compiler folds nothing.
//
// So a NaN in a constant pool is not something a legitimate image contains, and
// refusing it costs nothing while keeping the header's promise that a corrupt
// file is refused rather than run. Canonicalising would run a tampered image
// with a silently different number in it, which is the "best effort" this
// format's own header disavows.
//
// This asserts that, over the ways a NaN could plausibly try to reach the pool.
void legitimate_programs_carry_no_nan_constant() {
    constexpr std::string_view nan_shaped =
        "var a = NaN;\n"
        "var b = Number.NaN;\n"
        "var c = 0 / 0;\n"
        "var d = Infinity - Infinity;\n"
        "var e = parseInt('zzz');\n"
        "var f = 1e400;\n"      // out of range: Infinity, not NaN
        "var g = 1e-400;\n"     // out of range the other way: zero
        "var h = -0;\n"         // negative zero IS a legitimate constant
        "var i = 0xFFFFFFFF;\n" // the hex path through from_chars
        "var j = 0b1010;\n"
        "var k = 1_000_000;\n"
        "var l = 9007199254740993;\n"
        "var m = 5e-324;\n"
        "var n = 1.7976931348623157e308;\n"
        "var o = true && false;\n"
        "a + b + c + d + e + f + g + h + i + j + k + l + m + n + o;\n";
    const program built = compiler::compile(nan_shaped);
    CHECK(built.ok);

    std::size_t constants_seen = 0;
    std::size_t nan_constants = 0;
    for (const auto & fn : built.functions) {
        for (const value & v : fn.constants) {
            ++constants_seen;
            const std::uint64_t bits = v.bits();
            const bool is_nan = (bits & 0x7FF0'0000'0000'0000ull) == 0x7FF0'0000'0000'0000ull &&
                                (bits & 0x000F'FFFF'FFFF'FFFFull) != 0;
            if (is_nan) { ++nan_constants; }
        }
    }
    // A pool that is empty would make the assertion below vacuous.
    CHECK(constants_seen > 10);
    CHECK_EQ(nan_constants, std::size_t{0});
}

// --- 3. the loader ---------------------------------------------------------

// One patched image, and the refusal it must produce.
void must_refuse(std::string_view what, std::uint64_t patched, std::string_view mentioning) {
    const program built = compiler::compile(one_odd_constant);
    CHECK(built.ok);
    std::vector<std::byte> bytes = ctbrowser::script::write_image(built, image_option::drop_source);
    CHECK(!bytes.empty());
    if (bytes.empty()) { return; }

    const std::vector<std::size_t> at = find_u64(bytes, odd_constant_bits);
    // ASSERT THE MUTATION LANDS SOMEWHERE UNIQUE. A patch that silently edited
    // nothing would look exactly like a loader that refused.
    CHECK_EQ(at.size(), std::size_t{1});
    if (at.size() != 1) { return; }
    put_u64(bytes, at[0], patched);
    CHECK_EQ(find_u64(bytes, patched).size(), std::size_t{1});

    const auto back = load_image(bytes);
    if (back.ok) {
        std::printf("FAIL the loader ACCEPTED %.*s (0x%016llX)\n", static_cast<int>(what.size()),
                    what.data(), static_cast<unsigned long long>(patched));
        ++ctbrowser_test_failures;
        return;
    }
    if (back.error.find(std::string{mentioning}) == std::string::npos) {
        std::printf("FAIL %.*s was refused as \"%s\", which does not mention \"%.*s\"\n",
                    static_cast<int>(what.size()), what.data(), back.error.c_str(),
                    static_cast<int>(mentioning.size()), mentioning.data());
        ++ctbrowser_test_failures;
    }
}

void the_clean_image_still_loads() {
    const program built = compiler::compile(one_odd_constant);
    CHECK(built.ok);
    const std::vector<std::byte> bytes =
        ctbrowser::script::write_image(built, image_option::drop_source);
    CHECK(!bytes.empty());
    // The literal really is in there once, which is what makes every patch
    // below a patch of the constant pool rather than of some other field.
    CHECK_EQ(find_u64(bytes, odd_constant_bits).size(), std::size_t{1});
    const auto back = load_image(bytes);
    CHECK(back.ok);
    if (!back.ok) { std::printf("  the loader refused a clean image: %s\n", back.error.c_str()); }
}

} // namespace

int main() {
    arithmetic_really_forges_a_pointer();
    legitimate_programs_carry_no_nan_constant();
    the_clean_image_still_loads();

    // THE DEFECT ITSELF. Signalling, sign set: quiets straight into a heap
    // pointer whose payload the file chose.
    must_refuse("a signalling NaN that quiets into a heap pointer", 0xFFF4'0000'0000'0003ull,
                "constant 0");
    // The same payload without the sign bit: quiets into tag_true, so
    // `typeof (x - 1)` is "boolean" and an inferred f64 register holds one.
    must_refuse("a signalling NaN that quiets into a tag", 0x7FF4'0000'0000'0003ull, "constant 0");
    // A quiet NaN with a payload, both signs. Neither quiets into anything, but
    // both are bit patterns this compiler cannot emit, and letting them through
    // would mean the loader had a rule about WHICH payloads are dangerous - a
    // rule that has already been wrong once.
    must_refuse("a quiet NaN carrying a payload", 0x7FFA'0000'0000'0001ull, "constant 0");
    must_refuse("a negative quiet NaN carrying a payload", 0xFFFA'0000'0000'0001ull, "constant 0");
    // The hardware's own default NaN, which is NOT the engine's canonical one:
    // x86-64 produces 0xFFF8... for 0.0/0.0. Refused too, because no constant
    // this compiler emits is any NaN at all.
    must_refuse("the hardware default NaN", 0xFFF8'0000'0000'0000ull, "constant 0");
    // A NaN one bit away from canonical, to pin that the test is comparing the
    // whole pattern rather than an exponent.
    must_refuse("a NaN one mantissa bit from canonical", canonical_nan_bits | 1ull, "constant 0");

    // A boxed pointer outright - the case the original `is_heap()` gate caught.
    // Kept as a control: if the patch machinery ever stopped landing, this is
    // the case that would still pass for the wrong reason, so it asserts the
    // ORIGINAL wording rather than the new one.
    must_refuse("a boxed heap pointer", 0xFFFC'0000'0000'0003ull, "heap pointer");
    // AND THE SHAPE THAT IS NEITHER. Matches the qnan mask, so it is not a
    // number; is not one of the four tags; has the sign clear, so it is not
    // heap either. Nothing in the engine describes what it is, and the old gate
    // let it through: `typeof` fell off the end of its own switch.
    must_refuse("a tag no value has", 0x7FFC'0000'0000'0009ull, "constant 0");
    must_refuse("a tag no value has, sign clear, large payload", 0x7FFC'0000'DEAD'BEEFull,
                "constant 0");

    // AND THE FOUR THAT ARE LEGITIMATE still load: undefined, null, false, true
    // are ordinary values and a pool is allowed to hold them.
    for (const std::uint64_t tag : {0x7FFC'0000'0000'0000ull, 0x7FFC'0000'0000'0001ull,
                                    0x7FFC'0000'0000'0002ull, 0x7FFC'0000'0000'0003ull}) {
        const program built = compiler::compile(one_odd_constant);
        std::vector<std::byte> bytes =
            ctbrowser::script::write_image(built, image_option::drop_source);
        const std::vector<std::size_t> at = find_u64(bytes, odd_constant_bits);
        CHECK_EQ(at.size(), std::size_t{1});
        if (at.size() != 1) { continue; }
        put_u64(bytes, at[0], tag);
        const auto back = load_image(bytes);
        CHECK(back.ok);
        if (!back.ok) {
            std::printf("  a legitimate tag 0x%016llX was refused: %s\n",
                        static_cast<unsigned long long>(tag), back.error.c_str());
        }
    }
    // As does the canonical NaN, which is the one NaN the engine says a number
    // is allowed to be - so if the compiler ever does learn to fold `0/0`, the
    // shape it should emit is already accepted.
    {
        const program built = compiler::compile(one_odd_constant);
        std::vector<std::byte> bytes =
            ctbrowser::script::write_image(built, image_option::drop_source);
        const std::vector<std::size_t> at = find_u64(bytes, odd_constant_bits);
        CHECK_EQ(at.size(), std::size_t{1});
        if (at.size() == 1) {
            put_u64(bytes, at[0], canonical_nan_bits);
            const auto back = load_image(bytes);
            CHECK(back.ok);
        }
    }

    REPORT("image_trust");
}
