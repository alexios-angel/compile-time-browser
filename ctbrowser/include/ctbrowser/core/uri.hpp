//===- uri.hpp - ECMAScript URI decoding ---------------------- -*- C++ -*-===//
//
// decodeURI and decodeURIComponent.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace ctbrowser {

/// ECMAScript decodeURI / decodeURIComponent. Returns nullopt for a malformed
/// escape or escaped invalid UTF-8; unescaped bytes pass through unchanged and
/// the result may contain NULs. decode_uri keeps escapes for ;/?:@&=+$,# as
/// written, including their hex case.
[[nodiscard]] std::optional<std::string> decode_uri(std::string_view text);
[[nodiscard]] std::optional<std::string> decode_uri_component(std::string_view text);

} // namespace ctbrowser
