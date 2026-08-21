// The program image: does it round-trip, and does the loader refuse what it
// should?
//
// Same discipline as the DOM and style comparators. The positive case is a few
// lines; the negative cases are the file. A comparator too lenient does not
// fail to catch a bad image - it certifies one - and a loader too permissive
// does not fail to catch a corrupt file, it EXECUTES it.
#include <ctcompile/JavaScript/ProgramComparator.hpp>

#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/program_image.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using ctbrowser::script::compiler;
using ctbrowser::script::image_option;
using ctbrowser::script::image_source_hash;
using ctbrowser::script::load_image;
using ctbrowser::script::program;
using ctbrowser::script::script_kind;
using ctbrowser::script::write_image;

namespace {

int failures = 0;

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %.*s\n", static_cast<int>(what.size()), what.data());
        ++failures;
    }
}

// Deliberately saturated: a field nobody serialized comes back at its default
// and fails here. Generators, arrows in methods, accessors, a BigInt literal,
// closures three deep, a for-of, negative zero, and awkward strings.
constexpr std::string_view rich_source =
    "const big = 123456789012345678901234567890n;\n"
    "const neg = -0;\n"
    "const nan = NaN;\n"
    "const odd = \"a\\tb\";\n"
    "function* counter(n) { for (let i = 0; i < n; i++) { yield i * 2; } }\n"
    "class Thing {\n"
    "  constructor(x) { this.x = x; }\n"
    "  get doubled() { return this.x * 2; }\n"
    "  set doubled(v) { this.x = v / 2; }\n"
    "  method() { return [1, 2, 3].map(v => v + this.x); }\n"
    "  static make(x) { return new Thing(x); }\n"
    "}\n"
    "function outer(a) {\n"
    "  return function middle(b) {\n"
    "    return function inner(c) { return a + b + c; };\n"
    "  };\n"
    "}\n"
    "async function later(p) { const v = await p; return v + 1; }\n"
    "let total = 0;\n"
    "for (const v of counter(4)) { total += v; }\n"
    "const t = Thing.make(3);\n"
    "total += t.doubled + outer(1)(2)(3) + t.method().length;\n"
    "try { throw new Error(\"x\"); } catch (e) { total += e.message.length; }\n"
    "return total;\n";

[[nodiscard]] program compiled(std::string_view source) {
    return compiler::compile(source);
}

// Round-trip and require equality. Returns the bytes so a caller can corrupt them.
std::vector<std::byte> round_trip(const program & from, std::string_view what,
                                  image_option option = image_option::keep_source) {
    const std::vector<std::byte> bytes = write_image(from, option);
    if (bytes.empty()) {
        std::printf("FAIL %.*s: the writer refused: %.*s\n", static_cast<int>(what.size()),
                    what.data(), static_cast<int>(ctbrowser::script::write_error().size()),
                    ctbrowser::script::write_error().data());
        ++failures;
        return bytes;
    }
    const auto back = load_image(bytes);
    if (!back.ok) {
        std::printf("FAIL %.*s: the loader refused: %s\n", static_cast<int>(what.size()),
                    what.data(), back.error.c_str());
        ++failures;
        return bytes;
    }
    if (const auto diff = ctcompile::js::compare(from, back.value)) {
        std::printf("FAIL %.*s: %s: %s\n", static_cast<int>(what.size()), what.data(),
                    diff->where.c_str(), diff->what.c_str());
        ++failures;
    }
    return bytes;
}

// A corrupt image must be REFUSED. Not "must not crash" - refused, with a
// reason, because an image that loads and is subtly wrong is worse than one
// that fails.
void must_refuse(std::string_view what, std::vector<std::byte> bytes,
                 void (*corrupt)(std::vector<std::byte> &)) {
    corrupt(bytes);
    const auto back = load_image(bytes);
    if (back.ok) {
        std::printf("FAIL the loader accepted: %.*s\n", static_cast<int>(what.size()), what.data());
        ++failures;
    }
}

// AND REFUSED FOR THE STATED REASON. Two decisions in program_image.cpp rest on
// which check runs first - the source hash algorithm is tagged into the
// FINGERPRINT rather than the version because the layout did not change, and
// dropping `nested` bumped the VERSION because it did. Neither is worth
// anything if the loader reports the wrong one, and "it was refused" cannot
// tell them apart.
void must_refuse_saying(std::string_view what, std::vector<std::byte> bytes,
                        void (*corrupt)(std::vector<std::byte> &), std::string_view expected) {
    corrupt(bytes);
    const auto back = load_image(bytes);
    if (back.ok) {
        std::printf("FAIL the loader accepted: %.*s\n", static_cast<int>(what.size()), what.data());
        ++failures;
        return;
    }
    if (back.error.find(expected) == std::string::npos) {
        std::printf("FAIL %.*s was refused as \"%s\", which does not mention \"%.*s\"\n",
                    static_cast<int>(what.size()), what.data(), back.error.c_str(),
                    static_cast<int>(expected.size()), expected.data());
        ++failures;
    }
}

// --- THE HASHES THE SOURCE HASH MUST NOT BE ---------------------------
//
// `image_source_hash` is what makes the cache safe - an image built from other
// source is refused rather than run - and it is XXH64 over 64-bit words rather
// than a byte at a time because it also sits on the page-load path. Widening a
// hash gives it failure modes a byte-at-a-time one cannot have, and these are
// those failure modes, written out so the case that catches each can be
// WATCHED failing. A negative case nobody has seen go red is not evidence.
//
// The first of them is not hypothetical. The four-lane FNV below is what this
// file's hash was for most of a day, and it collides on one in five single-byte
// edits to real JavaScript.

constexpr std::uint64_t fnv_prime = 1099511628211ull;
constexpr std::uint64_t fnv_basis = 14695981039346656037ull;

[[nodiscard]] std::uint64_t word_le(const unsigned char * at) {
    std::uint64_t w = 0;
    for (int i = 0; i < 8; ++i) { w |= static_cast<std::uint64_t>(at[i]) << (8 * i); }
    return w;
}

[[nodiscard]] std::uint64_t avalanche(std::uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;
    return x;
}

// WHAT THE HASH WAS UNTIL TODAY: one multiply per byte. The fingerprint now
// carries a tag naming which algorithm an image was written with, and that tag
// is only worth its line if the two answers actually differ.
[[nodiscard]] std::uint64_t legacy_byte_fnv(std::string_view s) {
    std::uint64_t h = 1469598103934665603ull; // the basis as it was written
    for (const char c : s) {
        h ^= static_cast<std::uint8_t>(c);
        h *= fnv_prime;
    }
    return h;
}

// FNV-1a WIDENED TO 64-BIT WORDS - the obvious way to make the hash above four
// times faster, and the way that was taken first. It is fast, it round-trips,
// every test in this file passed with it, and it is BROKEN: a multiply carries a
// difference only upward, so an edit to a word's top bits leaves the lane
// differing in its top three bits and nowhere else - the same three bits
// wherever in the lane the edit landed. Distinct edits reach identical
// accumulators. Kept here because a defect with a witness is worth more than a
// paragraph saying it was avoided.
[[nodiscard]] std::uint64_t word_fnv_four_lane(std::string_view s) {
    std::uint64_t h0 = fnv_basis;
    std::uint64_t h1 = fnv_basis ^ 0x9E3779B97F4A7C15ull;
    std::uint64_t h2 = fnv_basis ^ 0xBF58476D1CE4E5B9ull;
    std::uint64_t h3 = fnv_basis ^ 0x94D049BB133111EBull;
    const auto * at = reinterpret_cast<const unsigned char *>(s.data());
    std::size_t left = s.size();
    while (left >= 32) {
        h0 = (h0 ^ word_le(at)) * fnv_prime;
        h1 = (h1 ^ word_le(at + 8)) * fnv_prime;
        h2 = (h2 ^ word_le(at + 16)) * fnv_prime;
        h3 = (h3 ^ word_le(at + 24)) * fnv_prime;
        at += 32;
        left -= 32;
    }
    for (std::size_t i = 0; i < left; ++i) { h0 = (h0 ^ at[i]) * fnv_prime; }
    return avalanche(avalanche(h0) ^ std::rotl(avalanche(h1), 16) ^ std::rotl(avalanche(h2), 32) ^
                     std::rotl(avalanche(h3), 48));
}

// THE SAME AGAIN WITH THE LANES INTERCHANGEABLE: one basis for all four and a
// symmetric fold, so the hash cannot tell which lane a block went to. Moving
// eight bytes from one lane to another is invisible to it.
[[nodiscard]] std::uint64_t interchangeable_lanes(std::string_view s) {
    std::uint64_t h[4] = {fnv_basis, fnv_basis, fnv_basis, fnv_basis};
    const auto * at = reinterpret_cast<const unsigned char *>(s.data());
    std::size_t left = s.size();
    while (left >= 32) {
        for (int i = 0; i < 4; ++i) { h[i] = (h[i] ^ word_le(at + 8 * i)) * fnv_prime; }
        at += 32;
        left -= 32;
    }
    unsigned char pad[32] = {};
    for (std::size_t i = 0; i < left; ++i) { pad[i] = at[i]; }
    for (std::size_t w = 0; w * 8 < left; ++w) { h[0] = (h[0] ^ word_le(pad + 8 * w)) * fnv_prime; }
    return avalanche(h[0] ^ h[1] ^ h[2] ^ h[3]);
}

// AND ONE WITH A LANE READING ANOTHER'S WORD - the copy-paste four nearly
// identical lines invite, in which bytes 16..23 of every block are never read.
// It round-trips, it is exactly as fast, and it hashes a quarter of the source
// to nothing.
[[nodiscard]] std::uint64_t dropped_lane(std::string_view s) {
    std::uint64_t h0 = fnv_basis;
    std::uint64_t h1 = fnv_basis ^ 0x9E3779B97F4A7C15ull;
    std::uint64_t h2 = fnv_basis ^ 0xBF58476D1CE4E5B9ull;
    std::uint64_t h3 = fnv_basis ^ 0x94D049BB133111EBull;
    const auto * at = reinterpret_cast<const unsigned char *>(s.data());
    std::size_t left = s.size();
    while (left >= 32) {
        h0 = (h0 ^ word_le(at)) * fnv_prime;
        h1 = (h1 ^ word_le(at + 8)) * fnv_prime;
        h2 = (h2 ^ word_le(at + 8)) * fnv_prime; // the defect
        h3 = (h3 ^ word_le(at + 24)) * fnv_prime;
        at += 32;
        left -= 32;
    }
    for (std::size_t i = 0; i < left; ++i) { h0 = (h0 ^ at[i]) * fnv_prime; }
    return avalanche(avalanche(h0) ^ std::rotl(avalanche(h1), 16) ^ std::rotl(avalanche(h2), 32) ^
                     std::rotl(avalanche(h3), 48));
}

// A case is evidence only if the blinded hash it is aimed at MISSES it. This
// asserts both halves and names which half went wrong.
void hash_case(std::string_view what, std::string_view a, std::string_view b,
               std::uint64_t (*blinded)(std::string_view), std::string_view blinded_name) {
    if (image_source_hash(a) == image_source_hash(b)) {
        std::printf("FAIL %.*s: the source hash COLLIDES\n", static_cast<int>(what.size()),
                    what.data());
        ++failures;
    }
    if (blinded(a) != blinded(b)) {
        std::printf("FAIL %.*s: %.*s does not collide either, so this case proves nothing\n",
                    static_cast<int>(what.size()), what.data(),
                    static_cast<int>(blinded_name.size()), blinded_name.data());
        ++failures;
    }
}

} // namespace

int main() {
    const program rich = compiled(rich_source);
    check(rich.ok, "the saturated fixture compiles");
    if (!rich.ok) {
        std::printf("  (%s)\n", rich.error.c_str());
        return 1;
    }

    const std::vector<std::byte> bytes = round_trip(rich, "the saturated fixture");
    if (bytes.empty()) { return 1; }

    // DETERMINISM. Two writes of the same program must be byte-identical, or an
    // image cannot be hashed, cached or compared. This is the assertion that
    // catches a writer copying `instruction`'s padding byte, whose contents
    // depend on the optimisation level of the code that built it.
    check(write_image(rich) == write_image(rich), "two writes of one program are identical");
    check(write_image(compiled(rich_source)) == bytes,
          "two compiles of one source produce identical images");

    // Dropping the source is a different image AND a different behaviour, so
    // the round trip has to hold for it separately.
    {
        const auto lean = write_image(rich, image_option::drop_source);
        check(!lean.empty() && lean.size() < bytes.size(), "dropping the source shrinks the image");
        const auto back = load_image(lean);
        check(back.ok, "an image without source loads");
        if (back.ok) {
            check(back.value.source.empty(), "and its source really is gone");
            check(back.value.functions.size() == rich.functions.size(),
                  "while every function survives");
        }
    }

    // A program the compiler rejected is not an image.
    {
        const program broken = compiled("function ( {");
        check(!broken.ok, "the broken fixture does not compile");
        check(write_image(broken).empty(), "the writer refuses a program that did not compile");
    }

    // --- WHAT THE LOADER MUST REFUSE ------------------------------------
    // Each of these is a file that would otherwise reach the VM, whose pool
    // reads are unchecked and one of whose writes is unbounded.
    must_refuse("a truncated image", bytes,
                [](std::vector<std::byte> & b) { b.resize(b.size() / 2); });
    must_refuse("an image with no magic", bytes,
                [](std::vector<std::byte> & b) { b[0] = std::byte{0}; });
    // THE TWO REFUSALS THAT MUST NOT BE CONFUSED WITH EACH OTHER. The version
    // sits at offset 4 and the fingerprint at 8, and the loader reads them in
    // that order on purpose: a changed LAYOUT has to be reported as a format
    // version, and a changed MEANING as a different engine. Bumping the wrong
    // one sends a reader looking for a format difference that is not there.
    must_refuse_saying(
        "an image in an older format version", bytes,
        [](std::vector<std::byte> & b) { b[4] = std::byte{1}; }, "image format version 1");
    must_refuse_saying(
        "an image from a different engine build", bytes,
        [](std::vector<std::byte> & b) { b[8] ^= std::byte{0xFF}; }, "different engine build");
    must_refuse("an empty image", bytes, [](std::vector<std::byte> & b) { b.clear(); });
    must_refuse("an image with a nonsense option", bytes,
                [](std::vector<std::byte> & b) { b[16] = std::byte{9}; });

    // --- THE TWO THE VALIDATOR USED TO MISS ------------------------------
    // Both were found by reading the loader against the VM rather than by any
    // test here, which is why each gets a case that fails without its fix.

    // AN UPVALUE THAT CAPTURES A REGISTER ITS ENCLOSING FRAME DOES NOT HAVE.
    // run_loop.cpp:904 pushes `reg(up.index)` with no bound - `registers_[base
    // + index]` - and then a cell test dereferences whatever came back. The
    // program is mutated rather than the bytes, so the case names the defect
    // instead of an offset that moves whenever the format does.
    {
        program bad = compiled(rich_source);
        bool patched = false;
        for (auto & fn : bad.functions) {
            for (auto & up : fn.upvalues) {
                if (up.from_parent_local) {
                    up.index = 60000; // no frame in this fixture is that large
                    patched = true;
                    break;
                }
            }
            if (patched) { break; }
        }
        check(patched, "the fixture has a closure that captures a local");
        const auto image = write_image(bad);
        check(!image.empty(), "and an image can be written for the mutated program");
        const auto back = load_image(image);
        check(!back.ok, "an upvalue capturing a register outside the enclosing frame is refused");
    }

    // A POOL COUNT THAT IS SMALLER THAN THE FILE AND FAR LARGER THAN THE FILE
    // CAN JUSTIFY. The guard used to compare an ENTRY COUNT against BYTES
    // REMAINING and then reserve count * sizeof(std::string); on a 25.9 MB
    // babylon image that reserves up to 828 MB before reading anything.
    //
    // WHAT THIS CASE CAN AND CANNOT SHOW, stated because a test that looks
    // like it covers a bug it does not is worse than no test: both the old
    // guard and the new one end at `!ok` here, because a count that outruns
    // the data fails either way once entries are read. What the new guard
    // buys is BOUNDED ALLOCATION on the way to that refusal, and the
    // difference is only visible at image sizes a unit test should not build.
    // So this pins the verdict, and the allocation bound is defended by the
    // arithmetic in read_pool rather than by this assertion.
    {
        constexpr std::size_t name_count_at = 4 + 4 + 8 + 4 + 8;
        std::vector<std::byte> huge = bytes;
        const std::uint32_t nearly_the_file = static_cast<std::uint32_t>(bytes.size() - 64);
        for (std::size_t i = 0; i < 4; ++i) {
            huge[name_count_at + i] = static_cast<std::byte>((nearly_the_file >> (8 * i)) & 0xFF);
        }
        const auto back = load_image(huge);
        check(!back.ok, "a pool count the image cannot possibly hold is refused");
    }

    // AND EVERY SINGLE-BYTE CORRUPTION, AT EVERY OFFSET. This used to walk a
    // stride of 7 to stay quick, and that made it blind in the one place it
    // most needed to see: a 32-bit count's low byte is worth plus or minus 255,
    // while its HIGH bytes are worth millions, and a stride reaches one and not
    // the others. Two reserve-the-world bugs lived behind exactly that gap. The
    // fixture image is a few kilobytes, so every offset costs nothing.
    //
    // The loader may accept - a flipped byte inside the retained source is
    // legitimately harmless - but it must never crash, never allocate wildly,
    // and never hand back a program whose tables disagree with its operands.
    {
        std::size_t accepted = 0, tried = 0;
        for (std::size_t at = 0; at < bytes.size(); ++at) {
            std::vector<std::byte> corrupt = bytes;
            corrupt[at] ^= std::byte{0x5A};
            ++tried;
            if (load_image(corrupt).ok) { ++accepted; }
        }
        std::printf("  single-byte corruption: %zu of %zu offsets still loaded\n", accepted, tried);
    }

    // --- MORE FUNCTIONS THAN A 16-BIT OPERAND HOLDS ----------------------
    // `op::closure` names its target with a THIRTY-TWO BIT operand, and three
    // of the four sites that emit it narrowed the index to uint16 first. That
    // is not a bound, it is a wrap: a program with 70,001 functions called
    // function 69,999 and ran function 4,463 - 69,999 minus 65,536 - with no
    // error from the compiler, the VM or this format. Babylon is 31,905
    // functions, so the corpus in this repository was at 49% of a ceiling the
    // instruction encoding never had.
    //
    // 70,001 functions compile in 17 ms, so this is a cheap thing to pin.
    {
        std::string many = "var a = [";
        // ONE name for all of them: the compiler refuses a program with more
        // than 65,536 distinct property names - a real guard, for the operand
        // that selects one - and naming each function would hit that first and
        // prove nothing about this.
        constexpr int count = 70000;
        for (int i = 0; i < count; ++i) {
            if (i != 0) { many += ","; }
            many += "()=>" + std::to_string(i);
        }
        many += "];\nvar answer = a[" + std::to_string(count - 1) + "]();\n";

        const program wide = compiled(many);
        check(wide.ok, "a program with 70,001 functions compiles");
        check(wide.functions.size() > 65536, "and really does have more than 65,536 of them");

        // THE INSTRUCTION, NOT THE OUTPUT. Running it would prove the same
        // thing, but this says exactly which field was wrong: every op::closure
        // must name a function index that exists, and the last one must reach
        // past the old ceiling.
        std::uint32_t highest = 0;
        std::size_t closures = 0;
        for (const auto & fn : wide.functions) {
            for (const auto & one : fn.code) {
                if (one.code == ctbrowser::script::op::closure) {
                    ++closures;
                    highest = std::max(highest, one.bx());
                    if (one.bx() >= wide.functions.size()) {
                        std::printf("FAIL an op::closure names function %u of %zu\n", one.bx(),
                                    wide.functions.size());
                        ++failures;
                        break;
                    }
                }
            }
        }
        check(closures >= static_cast<std::size_t>(count), "every function is closed over");
        check(highest > 65535, "AND AN INDEX PAST 65,535 SURVIVES THE ENCODING");

        // And the image carries it, which the writer used to refuse outright.
        const auto image = write_image(wide);
        check(!image.empty(), "an image is written for it");
        if (!image.empty()) {
            const auto back = load_image(image);
            check(back.ok, "and loads");
            if (back.ok) {
                check(back.value.functions.size() == wide.functions.size(),
                      "with every function present");
            }
        }
    }

    // --- THE SAME TEXT, COMPILED TWO WAYS ---------------------------------
    // A source hash is a hash of the TEXT, and the same text compiles to two
    // different programs: a classic script declares into the global object, a
    // module into a scope of its own. Before the image recorded which,
    // `var out = 41 + 1;` compiled as a module recorded source hash
    // d52677f505aae6c3 - identical to the classic one - loaded against it with
    // ok=1 and no error, ran without raising, and left `globalThis.out`
    // undefined where the page expected 42. Silent wrong code at full speed,
    // which is the one failure this format exists to prevent.
    {
        constexpr std::string_view same_text = "var out = 41 + 1;\n";
        const program classic = compiler::compile(same_text, script_kind::classic);
        const program module_ = compiler::compile(same_text, script_kind::module_);
        check(classic.ok && module_.ok, "the same text compiles both ways");
        check(ctbrowser::script::image_source_hash(same_text) ==
                  ctbrowser::script::image_source_hash(same_text),
              "and the source hash cannot tell them apart, because it hashes text");

        const auto classic_bytes = write_image(classic);
        const auto module_bytes = write_image(module_);
        check(!classic_bytes.empty() && !module_bytes.empty(), "both write an image");

        const auto want = ctbrowser::script::image_source_hash(same_text);
        const auto right = load_image(classic_bytes, want, script_kind::classic);
        check(right.ok, "the classic image loads when a classic script is asked for");
        check(right.kind == script_kind::classic, "and reports the kind it was compiled as");

        const auto wrong = load_image(module_bytes, want, script_kind::classic);
        check(!wrong.ok, "THE MODULE IMAGE IS REFUSED when a classic script is asked for");
        check(wrong.error.find("module") != std::string::npos,
              "and the refusal says which way round it is, not 'different source'");

        const auto other_way = load_image(classic_bytes, want, script_kind::module_);
        check(!other_way.ok, "and a classic image is refused when a module is asked for");

        const auto as_module = load_image(module_bytes, want, script_kind::module_);
        check(as_module.ok, "while the module image loads when a module is asked for");
        check(as_module.value.kind == script_kind::module_,
              "and the program it hands back knows what it is");
    }

    // --- THE IDENTITY OF THE INSTRUCTION SET ------------------------------
    // An image stores opcodes as bare bytes, so if byte 45 means `sub` when it
    // is written and `add` when it is read, the program loads clean and
    // computes the wrong answer. `image_fingerprint()` guards that, and until
    // today it guarded it with `opcode_count` alone - which a renumbering does
    // not change. Measured before the fix: reordering two opcodes in both the
    // enum and the table left the fingerprint at 19b2766c29d904c0 and the image
    // loaded with ok=1. After it, the same reorder moves the fingerprint and
    // the loader refuses.
    //
    // The value itself is NOT pinned here. Phases 13 and 14 add opcodes on
    // purpose, so a golden number would be a line to edit rather than a fact to
    // check. What is pinned is the property that makes it work: the identity
    // depends on the ORDER of the names and not merely on their contents.
    {
        const auto fold = [](const std::vector<std::string_view> & names) {
            std::uint64_t h = fnv_basis;
            for (const std::string_view one : names) {
                for (const char c : one) {
                    h ^= static_cast<std::uint8_t>(c);
                    h *= fnv_prime;
                }
                h ^= static_cast<std::uint8_t>(';');
                h *= fnv_prime;
            }
            return h;
        };
        std::vector<std::string_view> forward;
        for (std::size_t at = 0; at < ctbrowser::script::opcode_names_joined.size();) {
            const std::size_t end = ctbrowser::script::opcode_names_joined.find(';', at);
            forward.push_back(ctbrowser::script::opcode_names_joined.substr(at, end - at));
            at = end + 1;
        }
        check(forward.size() == ctbrowser::script::opcode_count,
              "the joined opcode names split back into one name per opcode");
        check(fold(forward) == ctbrowser::script::opcode_set_identity,
              "and folding them reproduces the identity the fingerprint mixes");

        std::vector<std::string_view> swapped = forward;
        if (swapped.size() > 1) { std::swap(swapped[0], swapped[1]); }
        check(fold(swapped) != ctbrowser::script::opcode_set_identity,
              "EXCHANGING TWO OPCODES CHANGES THE IDENTITY - which is the whole "
              "point, and is what opcode_count could not see");

        std::vector<std::string_view> reversed = forward;
        std::ranges::reverse(reversed);
        check(fold(reversed) != ctbrowser::script::opcode_set_identity,
              "and so does reversing the whole instruction set");
    }

    // --- THE SOURCE HASH -------------------------------------------------
    // The field that decides whether a cached image is this page's. It is now
    // XXH64 over 64-bit words rather than FNV-1a a byte at a time, because it
    // was 4.16 ms of a p5 page load on its own. Speed is why it changed; these
    // are what it still has to be.

    // THE PRECONDITION FOR THE FINGERPRINT TAG. `image_fingerprint()` mixes in
    // `source_hash_algorithm` so an image written by the old build is refused
    // as a different ENGINE rather than as different SOURCE. That tag is worth
    // its line only if the two algorithms really disagree, and this is the one
    // thing about it that a single build can check.
    check(image_source_hash(rich_source) != legacy_byte_fnv(rich_source),
          "the source hash is no longer the byte-at-a-time one it replaced");

    // BLOCKS THAT SWAP LANES. Hashing 32 bytes at a time means bytes 0..7 and
    // 8..15 go through different accumulators, and accumulators that start
    // alike and are folded together symmetrically cannot tell one arrangement
    // from the other - so an edit that moves one line past another produces the
    // same image hash.
    hash_case("two eight-byte blocks exchanged between lanes", "AAAAAAAABBBBBBBBCCCCCCCCDDDDDDDD",
              "BBBBBBBBAAAAAAAACCCCCCCCDDDDDDDD", interchangeable_lanes,
              "a hash whose lanes are interchangeable");

    // A LAST PARTIAL WORD PADDED WITH ZEROS cannot see a trailing NUL. XXH64
    // mixes the length in before it reads the tail, which is what closes this.
    hash_case("a trailing NUL byte", std::string_view{"let x = 1;\0", 10},
              std::string_view{"let x = 1;\0", 11}, interchangeable_lanes,
              "a hash whose last word is zero-padded");

    // A LANE THAT READS ANOTHER LANE'S WORD, so a quarter of every source is
    // not hashed at all. The image would round-trip, load and run; it would
    // simply stop noticing a class of edit.
    hash_case("a change in the third eight bytes of a block", "0123456789abcdefghijklmnopqrstuv",
              "0123456789abcdefghXjklmnopqrstuv", dropped_lane,
              "a hash with one lane reading another's word");

    // EVERY BYTE OF THE INPUT MUST MATTER, at every offset, across several
    // blocks and into the tail - and THIS IS THE CASE THAT CAUGHT THE REAL ONE.
    // The four-lane FNV that stood here for most of a day passes every other
    // assertion in this file and fails this one on nine of two hundred edits;
    // on 64 KB of real p5.js it collides on 50,678 of 262,145. A round trip
    // cannot see it, because the hash is not what is round-tripped.
    {
        std::string base;
        for (int i = 0; i < 200; ++i) { base += static_cast<char>('a' + (i % 26)); }
        std::set<std::uint64_t> real_seen{image_source_hash(base)};
        std::set<std::uint64_t> word_fnv_seen{word_fnv_four_lane(base)};
        std::set<std::uint64_t> dropped_seen{dropped_lane(base)};
        for (std::size_t at = 0; at < base.size(); ++at) {
            std::string flipped = base;
            flipped[at] = static_cast<char>(flipped[at] ^ 0x20);
            real_seen.insert(image_source_hash(flipped));
            word_fnv_seen.insert(word_fnv_four_lane(flipped));
            dropped_seen.insert(dropped_lane(flipped));
        }
        const std::size_t all = base.size() + 1;
        check(real_seen.size() == all,
              "every one-byte change to a 200-byte source changes the hash");
        check(word_fnv_seen.size() < all,
              "and four-lane FNV does NOT, which is why this case exists");
        check(dropped_seen.size() < all,
              "nor does a hash with a lane missing, so the case sees that too");
        std::printf("  one-byte edits distinguished: xxh64 %zu, four-lane fnv %zu, "
                    "dropped lane %zu, of %zu\n",
                    real_seen.size(), word_fnv_seen.size(), dropped_seen.size(), all);
    }

    // EVERY LENGTH IS ITS OWN, across the 32-byte boundary and both sides of
    // it, and through each of XXH64's three tail steps - eight bytes, four, one.
    {
        std::set<std::uint64_t> seen;
        for (std::size_t n = 0; n <= 96; ++n) {
            seen.insert(image_source_hash(std::string(n, 'a')));
        }
        check(seen.size() == 97, "97 lengths of the same byte hash 97 different ways");
    }

    // WHERE THE BYTES SIT IN MEMORY IS NOT PART OF THE ANSWER. True today for
    // free; here for the day somebody reaches for an aligned load.
    {
        const std::string content = "function f(a, b) { return a + b; } // and a tail";
        bool same = true;
        for (std::size_t off = 0; off < 8; ++off) {
            std::string buffer(off, '.');
            buffer += content;
            same = same && image_source_hash(std::string_view{buffer}.substr(off)) ==
                               image_source_hash(content);
        }
        check(same, "the same bytes at eight different alignments hash alike");
    }

    // KNOWN ANSWERS, AND NOT OURS. These came out of `xxhsum -H64`, Yann
    // Collet's own tool, and the first two are the values XXH64's specification
    // publishes; nothing in this repository computed them. So they check three
    // things at once that a self-generated table checks none of: that the hash
    // is really XXH64 at seed zero, that a host of the other byte order would
    // fail here rather than quietly disagree, and that the algorithm cannot
    // change without somebody also changing `source_hash_algorithm` in the
    // fingerprint. Regenerate with `printf ... | xxhsum -H64`.
    {
        struct vector {
            std::string_view what;
            std::string_view input;
            std::uint64_t expect;
        };
        const std::string long_input(65536, static_cast<char>(0xA5));
        const vector vectors[] = {
            {"empty", "", 0xef46db3751d8e999ull},
            {"one byte", "a", 0xd24ec4f1a98c6e5bull},
            {"31 bytes: no block, all three tail steps", "0123456789abcdefghijklmnopqrstu",
             0x80adfc1d42020f39ull},
            {"exactly one block, no tail", "0123456789abcdefghijklmnopqrstuv",
             0xbf7c9dbe16b5c6e2ull},
            {"one block and a one-byte tail", "0123456789abcdefghijklmnopqrstuv!",
             0xfd219fb7f6d3e0adull},
            {"a line of JavaScript", "function f(a, b) { return a + b; }\n", 0xc58480a990accb51ull},
            {"64 KB of one byte", long_input, 0xf76434b97a550fecull},
        };
        for (const vector & v : vectors) {
            if (image_source_hash(v.input) != v.expect) {
                std::printf("FAIL the source hash of \"%.*s\" is %016llx, not the pinned %016llx"
                            " - if that was deliberate, bump source_hash_algorithm\n",
                            static_cast<int>(v.what.size()), v.what.data(),
                            static_cast<unsigned long long>(image_source_hash(v.input)),
                            static_cast<unsigned long long>(v.expect));
                ++failures;
            }
        }
    }

    if (failures == 0) {
        std::printf("ok program_image (%zu functions, %zu byte image)\n", rich.functions.size(),
                    bytes.size());
    }
    return failures == 0 ? 0 : 1;
}
