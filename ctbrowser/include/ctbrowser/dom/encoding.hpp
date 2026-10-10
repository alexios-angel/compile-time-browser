//===- encoding.hpp - Document encoding detection ------------- -*- C++ -*-===//
//
// A document's encoding: sniffed from its bytes (HTML 13.2.3.2-3), named for
// `document.characterSet` by the Encoding Standard's label table (`<meta
// charset="cskoi8r">` is "KOI8-R", `866` is "IBM866"), and then decoded by that
// name, so a windows-1251 page shows Cyrillic rather than mojibake. The decoder
// covers UTF-8, both UTF-16s, the twenty-seven single-byte indexes
// (encoding_tables.inc, shared with TextDecoder) and x-user-defined. The legacy
// multi-byte encodings are not supported: their bytes pass through unchanged,
// but the document still reports the encoding's name.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <string>
#include <string_view>

namespace ctbrowser {

/// "Get an encoding" (https://encoding.spec.whatwg.org/#concept-encoding-get):
/// the encoding's name for `label`, stripped of leading and trailing ASCII
/// whitespace and matched ASCII case-insensitively against the label table;
/// empty when it names no encoding ("failure").
[[nodiscard]] std::string_view encoding_from_label(std::string_view label);

/// "Prescan a byte stream to determine its encoding" (HTML 13.2.3.2), plus the
/// BOM sniff that precedes it (13.2.3.1 steps 1-2). A UTF-16 `<?x` at the
/// start decides. Otherwise the first 1024 bytes are scanned for
/// `<meta charset=...>` or
/// `<meta http-equiv=content-type content="...charset=...">`. A UTF-16 result
/// becomes UTF-8, because a meta tag readable as ASCII means the document is
/// not UTF-16, and x-user-defined becomes windows-1252. When nothing is found,
/// "get an XML encoding" reads `encoding="..."` from an `<?xml ...>`
/// declaration at the very start, which text/html honours for compatibility.
/// Returns empty when nothing declares an encoding; the caller's default then
/// stands.
[[nodiscard]] std::string prescan_encoding(std::string_view bytes);

/// A document's bytes as UTF-8, decoded by the encoding name that
/// `prescan_encoding` or the label table gave (13.2.3.1's "decode"). A BOM is
/// dropped, an unmapped byte becomes U+FFFD, "replacement" yields one U+FFFD
/// for the whole stream, and UTF-8 goes through the Encoding Standard's
/// decoder (one U+FFFD per malformed sequence), so what reaches the tokenizer
/// is well-formed. For a name with no decoder here (the legacy multi-byte
/// encodings), the bytes are returned unchanged.
[[nodiscard]] std::string decode_document_bytes(std::string_view bytes, std::string_view name);

/// "Extract a character encoding from a meta element" (HTML 2.7.5): the
/// `charset=` value inside a content attribute's value, or empty.
[[nodiscard]] std::string_view encoding_from_meta_content(std::string_view content);

} // namespace ctbrowser
