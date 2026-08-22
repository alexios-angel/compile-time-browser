// The manifest, and the forty lines of it that can be wrong.
//
// Most of `to_json` is numbers being printed. The part with any risk is the
// string escaping, because the strings are not the compiler's: a resource name
// is whatever the document said, and a document may say `p5"; drop</script>`.
// A manifest that emitted that raw would be a file nothing can parse, produced
// by a packager that reported success - so the negative cases here are the
// point of the file, exactly as they are in ProgramImage.cpp and AppBundle.cpp.
#include <ctcompile/Support/Manifest.hpp>

#include <cstdio>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %.*s\n", static_cast<int>(what.size()), what.data());
        ++failures;
    }
}

void escapes(std::string_view input, std::string_view expect) {
    const std::string got = ctcompile::json_string(input);
    if (got != expect) {
        std::printf("FAIL <<%.*s>> became <<%s>>, not <<%.*s>>\n", static_cast<int>(input.size()),
                    input.data(), got.c_str(), static_cast<int>(expect.size()), expect.data());
        ++failures;
    }
}

} // namespace

int main() {
    // The ordinary case, and the quotes are part of the answer - a caller that
    // added its own would double them.
    escapes("p5.js", "\"p5.js\"");
    escapes("", "\"\"");

    // THE TWO THAT BREAK THE FILE. A quote ends the string early and a
    // backslash eats whatever follows it.
    escapes("p5\";x", "\"p5\\\";x\"");
    escapes("a\\b", "\"a\\\\b\"");

    // The five with short escapes.
    escapes("\b\f\n\r\t", "\"\\b\\f\\n\\r\\t\"");

    // AND EVERY OTHER CONTROL CHARACTER, which is the one people forget. RFC
    // 8259 forbids a raw byte below 0x20 in a string; half the parsers in the
    // world reject it and the other half accept it differently.
    escapes(std::string_view{"\x01", 1}, "\"\\u0001\"");
    escapes(std::string_view{"\x1f", 1}, "\"\\u001f\"");
    // A NUL IS A CHARACTER HERE, not a terminator: a std::string_view carrying
    // one must come out as an escape and must not truncate the name.
    escapes(std::string_view{"a\0b", 3}, "\"a\\u0000b\"");

    // 0x7F is DEL and is NOT a control character by JSON's rule, so it passes
    // through. Pinned because "escape everything unprintable" is the obvious
    // wrong generalisation of the case above.
    escapes(std::string_view{"\x7f", 1}, "\"\x7f\"");

    // UTF-8 PASSES THROUGH UNTOUCHED. Escaping it would mean decoding it, and
    // decoding means assuming it is valid - which a file name is not obliged to
    // be. These are the bytes of "é" and of a byte sequence that is not valid
    // UTF-8 at all; both must survive and neither may be mangled.
    escapes("\xc3\xa9", "\"\xc3\xa9\"");
    escapes(std::string_view{"\xff\xfe", 2}, "\"\xff\xfe\"");

    // ---- and the document as a whole -------------------------------------
    ctcompile::manifest one;
    one.compiler_version = "0.1.0";
    one.engine = "ctbrowser 2.0.0, 93 bytecode operations";
    one.entry = "index.html";
    one.bundle_format = 1;
    one.image_format = 3;
    one.engine_fingerprint = 0x1b0fb1310f6b5265ull;
    one.scripts.push_back({0, 24, 171, 1, 0xb13700f8d5f43be2ull});
    one.resources.push_back({"a\"quoted\".js", 7});

    const std::string json = ctcompile::to_json(one);
    check(json.find("\"bundle_format\": 1") != std::string::npos, "a number is a number");
    check(json.find("\"0x1b0fb1310f6b5265\"") != std::string::npos,
          "an identity is hex, and a string - it is not a quantity");
    check(json.find("\"0xb13700f8d5f43be2\"") != std::string::npos, "and so is a program id");
    check(json.find("a\\\"quoted\\\".js") != std::string::npos,
          "A HOSTILE RESOURCE NAME IS ESCAPED where it lands in the document");
    check(json.find("a\"quoted\".js") == std::string::npos,
          "and the raw form is nowhere in it");
    check(!json.empty() && json.back() == '\n', "the document ends with a newline");

    // EMPTY LISTS STILL PRODUCE A DOCUMENT. An application with no resources is
    // ordinary, and `[` followed by `]` on the next line is the shape a
    // hand-written emitter gets wrong.
    const ctcompile::manifest nothing;
    const std::string empty_json = ctcompile::to_json(nothing);
    check(empty_json.find("\"scripts\": [],") != std::string::npos, "no scripts is an empty list");
    check(empty_json.find("\"resources\": []") != std::string::npos,
          "and so is no resources");

    // BALANCED, which is the cheapest whole-document check there is and catches
    // a stray comma's neighbour: a brace or bracket left open by the list code.
    for (const auto & [open, close, what] :
         {std::tuple{'{', '}', "braces"}, std::tuple{'[', ']', "brackets"}}) {
        for (const std::string & document : {json, empty_json}) {
            int depth = 0;
            bool in_string = false;
            bool escaped = false;
            for (const char c : document) {
                if (in_string) {
                    if (escaped) {
                        escaped = false;
                    } else if (c == '\\') {
                        escaped = true;
                    } else if (c == '"') {
                        in_string = false;
                    }
                    continue;
                }
                if (c == '"') { in_string = true; }
                if (c == open) { ++depth; }
                if (c == close) { --depth; }
                if (depth < 0) { break; }
            }
            check(depth == 0 && !in_string, std::string{"the "} + what + " balance");
        }
    }

    if (failures == 0) { std::printf("ok manifest (%zu bytes of document)\n", json.size()); }
    return failures == 0 ? 0 : 1;
}
