#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace ctbrowser {

// ECMAScript URI decoding: malformed escapes and escaped invalid UTF-8 fail;
// unescaped bytes pass through unchanged. The result owns its bytes, including NULs.
// decode_uri preserves escapes for ;/?:@&=+$,# with their original hex case.
[[nodiscard]] std::optional<std::string> decode_uri(std::string_view text);
[[nodiscard]] std::optional<std::string> decode_uri_component(std::string_view text);

} // namespace ctbrowser
