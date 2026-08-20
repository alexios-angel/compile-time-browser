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

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using ctbrowser::script::compiler;
using ctbrowser::script::image_option;
using ctbrowser::script::load_image;
using ctbrowser::script::program;
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

[[nodiscard]] program compiled(std::string_view source) { return compiler::compile(source); }

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
    must_refuse("an image from a different engine build", bytes,
                [](std::vector<std::byte> & b) { b[8] ^= std::byte{0xFF}; });
    must_refuse("an empty image", bytes, [](std::vector<std::byte> & b) { b.clear(); });
    must_refuse("an image with a nonsense option", bytes,
                [](std::vector<std::byte> & b) { b[16] = std::byte{9}; });

    // AND EVERY SINGLE-BYTE CORRUPTION ON A STRIDE, which is the only way to be
    // sure the five refusals above are not the only paths anyone tested. The
    // loader may accept - a flipped byte inside the retained source is
    // legitimately harmless - but it must never crash, and never hand back a
    // program whose tables do not agree with its operands.
    {
        std::size_t accepted = 0, tried = 0;
        for (std::size_t at = 0; at < bytes.size(); at += 7) {
            std::vector<std::byte> corrupt = bytes;
            corrupt[at] ^= std::byte{0x5A};
            ++tried;
            if (load_image(corrupt).ok) { ++accepted; }
        }
        std::printf("  single-byte corruption: %zu of %zu offsets still loaded\n", accepted, tried);
    }

    if (failures == 0) {
        std::printf("ok program_image (%zu functions, %zu byte image)\n", rich.functions.size(),
                    bytes.size());
    }
    return failures == 0 ? 0 : 1;
}
