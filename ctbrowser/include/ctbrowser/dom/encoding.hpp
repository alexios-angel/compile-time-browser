#pragma once
#include <string>
#include <string_view>

// THE NAME OF A DOCUMENT'S ENCODING - and only the name. This engine decodes
// every byte stream as UTF-8 (see dom/tokenizer.hpp); what a page can observe
// of the encoding it DECLARED is `document.characterSet` (and its two aliases),
// which the Encoding Standard says must answer the encoding's NAME for any of
// its LABELS: `<meta charset="cskoi8r">` is "KOI8-R", `866` is "IBM866".
// Reporting the declared name while decoding as UTF-8 is what a UTF-8 page with
// a stale `<meta charset>` gets in every browser too - the label decides the
// name, the bytes decide what appears - and it is the whole of what
// Document-characterSet-normalization-{1,2}.html measure.

namespace ctbrowser {

// "Get an encoding", https://encoding.spec.whatwg.org/#concept-encoding-get:
// the label stripped of leading and trailing ASCII whitespace and matched
// ASCII case-insensitively against the table of labels; empty when it names
// no encoding ("failure").
[[nodiscard]] std::string_view encoding_from_label(std::string_view label);

// "Prescan a byte stream to determine its encoding", HTML 13.2.3.3, plus the
// BOM sniff that precedes it (13.2.3.2 steps 1-2): the first 1024 bytes are
// scanned for `<meta charset=...>` or `<meta http-equiv=content-type
// content="...charset=...">`; a UTF-16 answer becomes UTF-8 (a document
// whose meta could be read was not UTF-16) and x-user-defined windows-1252.
// Empty when nothing declares one - the caller's default, UTF-8 here, then
// stands.
[[nodiscard]] std::string prescan_encoding(std::string_view bytes);

// "Extract a character encoding from a meta element", HTML 2.7.5: the
// `charset=` inside a content attribute's value, or empty.
[[nodiscard]] std::string_view encoding_from_meta_content(std::string_view content);

} // namespace ctbrowser
