// Unicode: the engine works in UTF-32 code points, with conformant UTF-8/16/32
// transcoding at the boundaries. Proven at compile time (transcoding folds) and
// at runtime (a page with non-ASCII text lays out by code point, not by byte).
#include <ctbrowser.hpp>
#include <ctbrowser/utf.hpp>
#include <cstdio>
#include <string>

namespace u = ctbrowser;

// --- compile-time transcoding conformance ---
// ASCII, 2-byte (é), 3-byte (Ω), 4-byte astral (😀)
static_assert(u::utf8_to_utf32("A").size() == 1 && u::utf8_to_utf32("A")[0] == U'A');
static_assert(u::utf8_to_utf32("é")[0] == 0x00E9 && u::utf8_to_utf32("é").size() == 1);
static_assert(u::utf8_to_utf32("Ω")[0] == 0x03A9);
static_assert(u::utf8_to_utf32("\U0001F600")[0] == 0x1F600 && u::utf8_to_utf32("\U0001F600").size() == 1);
// code-point count vs byte count: "Aé Ω😀" is 5 code points but 10 bytes
static_assert(u::utf8_length("Aé Ω\U0001F600") == 5);
// round-trips
static_assert(u::utf32_to_utf8(U"\U0001F600").size() == 4);
static_assert(u::utf32_to_utf8(u::utf8_to_utf32("café")) == "café");
static_assert(u::utf16_to_utf32(u::utf32_to_utf16(U"\U0001F600"))[0] == 0x1F600); // surrogate pair
// ill-formed UTF-8 -> U+FFFD (lone byte, overlong '/', encoded surrogate)
static_assert(u::utf8_to_utf32("\xFF")[0] == u::REPLACEMENT_CHAR);
static_assert(u::utf8_to_utf32("\xC0\xAF")[0] == u::REPLACEMENT_CHAR);
static_assert(u::utf8_to_utf32("\xED\xA0\x80")[0] == u::REPLACEMENT_CHAR);
// out-of-range scalar / surrogate -> U+FFFD on encode
static_assert(u::utf32_to_utf8(U"\xD800").size() == 3); // U+FFFD is 3 UTF-8 bytes

using upage = ctbrowser::page<R"(<!DOCTYPE html>
<title>u</title>
<style>#t{font-size:16px}</style>
<div id="t">café</div>)">;

static int fails = 0;
#define CHECK(c)                                                              \
	do {                                                                      \
		if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } \
	} while (0)

int main() {
	ctbrowser::engine<upage> e;
	CHECK(e.script.ok());
	const std::vector<ctbrowser::paint_cmd> paints = e.frame(400);
	bool found = false;
	for (const ctbrowser::paint_cmd & p : paints) {
		if (p.what == ctbrowser::paint_cmd::kind::text && p.text == U"café") {
			found = true;
			CHECK(p.text.size() == 4);   // 4 code points (é is one), not 5 UTF-8 bytes
			CHECK(p.w == 4 * 16);        // width counts code points at 16px, not bytes
		}
	}
	CHECK(found); // the page's UTF-8 source is decoded to the right code points

	if (fails == 0) { std::printf("unicode suite: all checks passed\n"); }
	return fails ? 1 : 0;
}
