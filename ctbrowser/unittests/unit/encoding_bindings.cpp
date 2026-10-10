//===- encoding_bindings.cpp - TextEncoder/Decoder ------------ -*- C++ -*-===//
//
// TextEncoder and TextDecoder (Encoding Standard section 8), tested on a live
// page. One case per rule: the UTF-8 decoder's error handling, the two UTF-16
// byte orders, legacy single-byte indexes, the label table, the fatal and
// ignoreBOM flags, streaming, encodeInto, and hostile BufferSource objects.
//
//===----------------------------------------------------------------------===//
#include <ctbrowser.hpp>

#include "check.hpp"
#include "dom_probe.hpp"

#include <string>

namespace {

constexpr const char * page_html = "<!DOCTYPE html><html><body></body></html>";

/// Runs `body` as a function body on a fresh page and checks its string result.
void is(const std::string & body, const std::string & expected) {
    ctbrowser_test::is_in(page_html, "(function () { " + body + " })()", expected);
}

// --- TextEncoder ------------------------------------------------------------

void test_the_encoder() {
    is("return new TextEncoder().encoding;", "utf-8");
    is("return Array.from(new TextEncoder().encode('h\\u00e9\\u{1f320}')).join(',');",
       "104,195,169,240,159,140,160");
    // A lone surrogate becomes U+FFFD: `encode` takes a USVString.
    is("return Array.from(new TextEncoder().encode('\\ud800')).join(',');", "239,191,189");
    is("return new TextEncoder().encode().length;", "0");
}

void test_encode_into_writes_whole_code_points_only() {
    // Three bytes of room and a four-byte character: nothing is written, so a
    // caller can hand the same string to the next buffer.
    is("var r = new TextEncoder().encodeInto('\\u{1f320}', new Uint8Array(3));"
       " return r.read + '|' + r.written;",
       "0|0");
    // `read` counts UTF-16 code units, which is two for U+1F320.
    is("var r = new TextEncoder().encodeInto('\\u{1f320}', new Uint8Array(8));"
       " return r.read + '|' + r.written;",
       "2|4");
    is("var out = new Uint8Array(4);"
       " new TextEncoder().encodeInto('ab', out); return Array.from(out).join(',');",
       "97,98,0,0");
}

// --- TextDecoder ------------------------------------------------------------

void test_the_label_table() {
    is("return new TextDecoder().encoding;", "utf-8");
    is("return new TextDecoder('  UTF8  ').encoding;", "utf-8");
    // `latin1` and `us-ascii` are windows-1252, which is a whole class of
    // mojibake to get wrong.
    is("return [new TextDecoder('latin1').encoding, new TextDecoder('us-ascii').encoding,"
       " new TextDecoder('iso-8859-1').encoding].join('|');",
       "windows-1252|windows-1252|windows-1252");
    is("return new TextDecoder('unicodeFEFF').encoding;", "utf-16le");
    // The replacement encoding's labels must be rejected; that is its purpose.
    is("try { new TextDecoder('iso-2022-kr'); return 'no throw'; } catch (e) { return e.name; }",
       "RangeError");
    is("try { new TextDecoder('not-an-encoding'); return 'no throw'; } catch (e) { return e.name; "
       "}",
       "RangeError");
}

void test_the_utf8_decoder() {
    is("return new TextDecoder().decode(new Uint8Array([104, 195, 169]));", "h\xc3\xa9");
    // A truncated sequence is one U+FFFD, not one per byte, and the byte that
    // ended it is decoded again from scratch, so E0 41 is U+FFFD then "A".
    //
    // Most cases compare the decoded string itself. `length` and `charCodeAt`
    // count UTF-16 code units, as in any engine (see "The UTF-16 gap" in
    // docs/script/02-a--b-was-false.md).
    is("return new TextDecoder().decode(new Uint8Array([0xE0, 0x41]));", "\xef\xbf\xbd"
                                                                         "A");
    is("return new TextDecoder().decode(new Uint8Array([0xC2]));", "\xef\xbf\xbd");
    // `fatal` turns the same input into a TypeError.
    is("try { new TextDecoder('utf-8', {fatal: true}).decode(new Uint8Array([0xC2]));"
       " return 'no throw'; } catch (e) { return e.name; }",
       "TypeError");
    is("return new TextDecoder('utf-8', {fatal: true}).fatal;", "true");
}

void test_the_bom_and_streaming() {
    // The BOM is stripped when it matches the decoder's own encoding...
    is("return new TextDecoder().decode(new Uint8Array([0xEF, 0xBB, 0xBF, 65]));", "A");
    // ...and kept when ignoreBOM says so.
    is("return new TextDecoder('utf-8', {ignoreBOM: true})"
       ".decode(new Uint8Array([0xEF, 0xBB, 0xBF, 65]));",
       "\xef\xbb\xbf"
       "A");
    // A code point split across two calls: `stream` holds the half.
    is("var d = new TextDecoder();"
       " var first = d.decode(new Uint8Array([0xC3]), {stream: true});"
       " var second = d.decode(new Uint8Array([0xA9]));"
       " return first.length + '|' + second;",
       "0|\xc3\xa9");
}

void test_view_hardening() {
    // A getter that drops __bytes (the buffer's backing store) mid-read must
    // not leave the decoder holding a dangling store.
    is("var b = new ArrayBuffer(4); var v = new Uint8Array(b);"
       " Object.defineProperty(v, 'byteLength', {get: function () { delete b.__bytes;"
       " return 4; }}); return typeof new TextDecoder().decode(v);",
       "string");
    is("var v = new Uint8Array([104, 105]);"
       " Object.defineProperty(v, 'byteLength', {get: function () { return Infinity; }});"
       " return new TextDecoder().decode(v);",
       "hi");
    // A plain-object source whose byteLength getter drops the only reference to
    // the store and then allocates enough to collect it. The getters run before
    // the store is looked up, so decode() finds no store and throws a TypeError
    // instead of copying out of freed memory (which ASan would catch).
    is("var buf = {__bytes: new Uint8Array([104, 105])};"
       " var s = {buffer: buf, byteOffset: 0, get byteLength() { buf.__bytes = null;"
       " for (var i = 0; i < 2e5; i++) ({}); return 2; }};"
       " return new TextDecoder().decode(s);",
       "threw:TypeError");
    // ISO-8859-3 leaves 0xA5 unmapped (a NUL in the index row): U+FFFD, no over-read.
    is("return new TextDecoder('iso-8859-3').decode(new Uint8Array([0xA5, 0xFF, "
       "0x41])).charCodeAt(0)"
       " + '|' + new TextDecoder('iso-8859-3').decode(new Uint8Array([0xA5, 0xFF])).length;",
       "65533|2");
}

void test_utf16_and_a_single_byte_index() {
    is("return new TextDecoder('utf-16le').decode(new Uint8Array([0x41, 0x00, 0x42, 0x00]));",
       "AB");
    is("return new TextDecoder('utf-16be').decode(new Uint8Array([0x00, 0x41, 0x00, 0x42]));",
       "AB");
    // A surrogate pair, decoded to the one code point U+1F320.
    is("return new TextDecoder('utf-16le')"
       ".decode(new Uint8Array([0x3C, 0xD8, 0x20, 0xDF]));",
       "\xf0\x9f\x8c\xa0");
    // windows-1252's 0x80 is the euro sign, which is the difference between it
    // and ISO-8859-1 and the reason the indexes are carried at all.
    is("return new TextDecoder('windows-1252').decode(new Uint8Array([0x80]));", "\xe2\x82\xac");
    is("return new TextDecoder('koi8-r').decode(new Uint8Array([0xC1]));", "\xd0\xb0");
    // An ArrayBuffer is a BufferSource too, not only a view of one.
    is("return new TextDecoder().decode(new Uint8Array([65, 66]).buffer);", "AB");
}

} // namespace

/// decode() reads byteLength from the object it is given, so a plain object
/// can claim a huge length. The reservation must be bounded by the bytes that
/// actually exist: this decodes the two real bytes instead of trying to
/// allocate gigabytes and aborting.
void test_a_crafted_bytelength_does_not_over_reserve() {
    is("var u = new Uint8Array([104, 105]);"
       " return new TextDecoder().decode({ buffer: u.buffer, byteOffset: 0, byteLength: 1e12 });",
       "hi");
}

int main() {
    test_the_encoder();
    test_encode_into_writes_whole_code_points_only();
    test_the_label_table();
    test_the_utf8_decoder();
    test_the_bom_and_streaming();
    test_utf16_and_a_single_byte_index();
    test_view_hardening();
    test_a_crafted_bytelength_does_not_over_reserve();
    REPORT("encoding_bindings");
}
