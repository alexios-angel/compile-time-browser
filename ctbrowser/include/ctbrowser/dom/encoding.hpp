#pragma once
#include <string>
#include <string_view>

// A DOCUMENT'S ENCODING: sniffed from its bytes (HTML 13.2.3.2-3), named for
// `document.characterSet` by the Encoding Standard's label table -
// `<meta charset="cskoi8r">` is "KOI8-R", `866` is "IBM866" - and then
// DECODED by that name, so a windows-1251 page shows Cyrillic rather than
// the mojibake of its bytes read as UTF-8. The decoder covers UTF-8, the two
// UTF-16s, the twenty-seven single-byte indexes (encoding_tables.inc, shared
// with TextDecoder) and x-user-defined; the legacy multi-byte encodings are
// not here and their bytes pass through as UTF-8, named rather than silently
// missing.

namespace ctbrowser {

// "Get an encoding", https://encoding.spec.whatwg.org/#concept-encoding-get:
// the label stripped of leading and trailing ASCII whitespace and matched
// ASCII case-insensitively against the table of labels; empty when it names
// no encoding ("failure").
[[nodiscard]] std::string_view encoding_from_label(std::string_view label);

// "Prescan a byte stream to determine its encoding", HTML 13.2.3.2, plus the
// BOM sniff that precedes it (13.2.3.1 steps 1-2): a UTF-16 `<?x` at the
// start decides; else the first 1024 bytes are scanned for `<meta
// charset=...>` or `<meta http-equiv=content-type content="...charset=...">`
// - a UTF-16 answer becomes UTF-8 (a document whose meta could be read was
// not UTF-16) and x-user-defined windows-1252; and when that finds nothing,
// "get an XML encoding" reads `encoding="..."` out of an `<?xml ...>`
// declaration at the very start, which text/html honours for compatibility.
// Empty when nothing declares one - the caller's default then stands.
[[nodiscard]] std::string prescan_encoding(std::string_view bytes);

// The bytes of a document as UTF-8, decoded by the NAME `prescan_encoding`
// or the label table gave (13.2.3.1's "decode"): a BOM is dropped, an
// unmapped byte is U+FFFD, "replacement" is one U+FFFD for the whole stream.
// A name this engine has no decoder for - the multi-byte legacy encodings -
// or "UTF-8" itself returns the bytes as they are.
[[nodiscard]] std::string decode_document_bytes(std::string_view bytes, std::string_view name);

// "Extract a character encoding from a meta element", HTML 2.7.5: the
// `charset=` inside a content attribute's value, or empty.
[[nodiscard]] std::string_view encoding_from_meta_content(std::string_view content);

} // namespace ctbrowser
