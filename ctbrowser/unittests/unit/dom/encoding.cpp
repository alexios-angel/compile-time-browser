//===- ctbrowser/unittests/unit/dom/encoding.cpp - charset tests ----------===//
//
// Tests for a document's declared encoding: the Encoding Standard's label
// table, HTML's prescan over the first 1024 bytes, decoding the bytes by
// encoding name, and what `document.characterSet` answers for a page that
// declared one. WPT's dom/nodes/Document-characterSet-normalization-{1,2}.html
// walk every label through an iframe; this file tests the algorithm directly.
//
//===----------------------------------------------------------------------===//
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/shell/shell.hpp>

#include "check.hpp"
#include <string>
#include <string_view>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

/// The canonical encoding name for `text`, or "" when it is not a label.
[[nodiscard]] std::string label(std::string_view text) {
    return std::string{encoding_from_label(text)};
}

void test_a_label_names_an_encoding() {
    // Whitespace-trimmed and ASCII case-insensitive; a name is its own label.
    CHECK_EQ(label("cskoi8r"), std::string{"KOI8-R"});
    CHECK_EQ(label(" \t\n\f\r866 \t\n\f\r"), std::string{"IBM866"});
    CHECK_EQ(label("UtF-8"), std::string{"UTF-8"});
    CHECK_EQ(label("latin1"), std::string{"windows-1252"});
    CHECK_EQ(label("iso-8859-8-i"), std::string{"ISO-8859-8-I"});
    CHECK_EQ(label("unicode"), std::string{"UTF-16LE"});
    CHECK_EQ(label("hz-gb-2312"), std::string{"replacement"});
    CHECK_EQ(label("x-user-defined"), std::string{"x-user-defined"});
    CHECK_EQ(label("utf-7"), std::string{});
    CHECK_EQ(label(""), std::string{});
}

void test_the_prescan_reads_the_meta() {
    CHECK_EQ(prescan_encoding("<!doctype html><meta charset=\"cskoi8r\">"), std::string{"KOI8-R"});
    CHECK_EQ(prescan_encoding("<meta charset=Latin1>"), std::string{"windows-1252"});
    CHECK_EQ(prescan_encoding("<meta http-equiv=\"Content-Type\" content=\"text/html; "
                              "charset=iso-8859-2\">"),
             std::string{"ISO-8859-2"});
    // A content= without the pragma is not a declaration; a charset= is.
    CHECK_EQ(prescan_encoding("<meta content=\"text/html; charset=iso-8859-2\">"), std::string{});
    // UTF-16 by meta is UTF-8 (the meta could be read), x-user-defined is
    // windows-1252, an unknown label is nothing, and a later meta then counts.
    CHECK_EQ(prescan_encoding("<meta charset=utf-16>"), std::string{"UTF-8"});
    CHECK_EQ(prescan_encoding("<meta charset=x-user-defined>"), std::string{"windows-1252"});
    CHECK_EQ(prescan_encoding("<meta charset=nonsense><meta charset=greek>"),
             std::string{"ISO-8859-7"});
    // A commented-out meta, and an attribute inside another tag, are skipped.
    CHECK_EQ(prescan_encoding("<!-- <meta charset=greek> --><p charset=greek><meta charset=l2>"),
             std::string{"ISO-8859-2"});
    // The BOM wins over everything.
    CHECK_EQ(prescan_encoding("\xEF\xBB\xBF<meta charset=greek>"), std::string{"UTF-8"});
    CHECK_EQ(prescan_encoding("\xFF\xFEx"), std::string{"UTF-16LE"});
    // Only the first 1024 bytes are looked at.
    CHECK_EQ(prescan_encoding(std::string(1024, ' ') + "<meta charset=greek>"), std::string{});
    CHECK_EQ(prescan_encoding("<p>nothing declared</p>"), std::string{});
}

/// "Get an XML encoding": the fallback when the prescan finds no meta. It reads
/// the whole stream, is case-sensitive, and stops at the declaration's `>`.
void test_the_prescan_reads_an_xml_declaration() {
    CHECK_EQ(prescan_encoding("<?xml version=\"1.0\" encoding=\"windows-1251\"?><p>"),
             std::string{"windows-1251"});
    CHECK_EQ(prescan_encoding("<?xml encoding = 'cp1251' ?>"), std::string{"windows-1251"});
    // A `>` past the 1024th byte still ends the declaration.
    CHECK_EQ(prescan_encoding("<?xml version=\"1.0\" encoding=\"windows-1251\"" +
                              std::string(1000, ' ') + "?>"),
             std::string{"windows-1251"});
    // A meta the prescan reads wins; `<?XML`, `<?xml>encoding=`, an unquoted
    // value, `encodingencoding=` and a label with a space are all failure.
    CHECK_EQ(prescan_encoding("<?xml version=\"1.0\" encoding=\"windows-1251\"?>"
                              "<meta charset=greek>"),
             std::string{"ISO-8859-7"});
    CHECK_EQ(prescan_encoding("<?XML encoding=\"windows-1251\"?>"), std::string{});
    CHECK_EQ(prescan_encoding("<?xml>encoding=\"windows-1251\"?>"), std::string{});
    CHECK_EQ(prescan_encoding("<?xml encoding=windows-1251?>"), std::string{});
    CHECK_EQ(prescan_encoding("<?xml encodingencoding=\"windows-1251\"?>"), std::string{});
    CHECK_EQ(prescan_encoding("<?xml encoding=\"windows 1251\"?>"), std::string{});
    // UTF-16 in the declaration means UTF-8; a UTF-16 `<?x` is the byte order.
    CHECK_EQ(prescan_encoding("<?xml encoding=\"utf-16\"?>"), std::string{"UTF-8"});
    CHECK_EQ(prescan_encoding(std::string_view{"<\0?\0x\0m\0l\0", 10}), std::string{"UTF-16LE"});
    CHECK_EQ(prescan_encoding(std::string_view{"\0<\0?\0x\0m\0l", 10}), std::string{"UTF-16BE"});
}

void test_the_bytes_are_decoded_by_name() {
    // windows-1251 0xE6 is U+0436; ISO-8859-7 0xE6 is U+03B6; an unmapped
    // byte is U+FFFD; UTF-8 comes through minus its BOM; UTF-16 pairs up.
    CHECK_EQ(decode_document_bytes("a\xE6", "windows-1251"), std::string{"a\xD0\xB6"});
    CHECK_EQ(decode_document_bytes("a\xE6", "ISO-8859-7"), std::string{"a\xCE\xB6"});
    CHECK_EQ(decode_document_bytes("\xAA", "windows-1253"), std::string{"\xEF\xBF\xBD"});
    CHECK_EQ(decode_document_bytes("\xEF\xBB\xBF\xD0\xB6", "UTF-8"), std::string{"\xD0\xB6"});
    // UTF-8 is decoded, not passed through (Encoding 4.2): each malformed
    // sequence is one U+FFFD and the byte that broke it is read again, so an
    // encoded surrogate's three bytes give three U+FFFD and a truncated
    // sequence gives one.
    CHECK_EQ(decode_document_bytes("a\xFF"
                                   "b",
                                   "UTF-8"),
             std::string{"a\xEF\xBF\xBD"
                         "b"});
    CHECK_EQ(decode_document_bytes("\xE2\x82"
                                   "A",
                                   "UTF-8"),
             std::string{"\xEF\xBF\xBD"
                         "A"});
    CHECK_EQ(decode_document_bytes("\xED\xA0\x80", "UTF-8"),
             std::string{"\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"});
    CHECK_EQ(decode_document_bytes("\xC0\xAF", "UTF-8"), std::string{"\xEF\xBF\xBD\xEF\xBF\xBD"});
    CHECK_EQ(decode_document_bytes("\xF0\x9F\x98", "UTF-8"), std::string{"\xEF\xBF\xBD"});
    CHECK_EQ(decode_document_bytes("\xF0\x9F\x98\x80", "UTF-8"), std::string{"\xF0\x9F\x98\x80"});
    CHECK_EQ(
        decode_document_bytes(std::string_view{"\xFF\xFE\x36\x04\x3D\xD8\x00\xDC", 8}, "UTF-16LE"),
        std::string{"\xD0\xB6\xF0\x9F\x90\x80"});
    CHECK_EQ(decode_document_bytes(std::string_view{"\x04\x36\xD8", 3}, "UTF-16BE"),
             std::string{"\xD0\xB6\xEF\xBF\xBD"});
    CHECK_EQ(decode_document_bytes("\x80", "x-user-defined"), std::string{"\xEF\x9E\x80"});
    CHECK_EQ(decode_document_bytes("anything", "replacement"), std::string{"\xEF\xBF\xBD"});
    // An encoding without a decoder here (Big5) passes its bytes through.
    CHECK_EQ(decode_document_bytes("\xE6", "Big5"), std::string{"\xE6"});
    // End to end: a windows-1251 page shows its Cyrillic.
    browser page{browser_options{300, 200}};
    page.load_html("<?xml version=\"1.0\" encoding=\"windows-1251\"?><p id=p>\xE6</p><script>"
                   "console.log(document.characterSet + ',' + "
                   "(document.getElementById('p').textContent === '\\u0436'))"
                   "</script>");
    CHECK_EQ(page.bindings().console_output().back(), std::string{"windows-1251,true"});
}

/// characterSet, charset and inputEncoding all report the declared encoding;
/// a created or DOMParser-parsed document is always UTF-8.
void test_the_document_reports_the_name() {
    browser page{browser_options{300, 200}};
    page.load_html("<!DOCTYPE html><meta charset=\"cskoi8r\"><body><script>"
                   "var made = document.implementation.createHTMLDocument('');"
                   "console.log([document.characterSet, document.charset, document.inputEncoding,"
                   " made.characterSet, new DOMParser().parseFromString("
                   "'<meta charset=greek>', 'text/html').characterSet].join());"
                   "</script></body>");
    CHECK_EQ(page.bindings().console_output().back(),
             std::string{"KOI8-R,KOI8-R,KOI8-R,UTF-8,UTF-8"});
    browser plain{browser_options{300, 200}};
    plain.load_html("<!DOCTYPE html><body><script>console.log(document.characterSet)</script>");
    CHECK_EQ(plain.bindings().console_output().back(), std::string{"UTF-8"});
}

} // namespace

int main() {
    test_a_label_names_an_encoding();
    test_the_prescan_reads_the_meta();
    test_the_prescan_reads_an_xml_declaration();
    test_the_bytes_are_decoded_by_name();
    test_the_document_reports_the_name();
    REPORT("dom_encoding");
}
