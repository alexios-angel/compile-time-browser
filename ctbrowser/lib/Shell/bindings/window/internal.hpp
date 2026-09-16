#pragma once
// Private to lib/Shell/bindings/window/.
#include <ctbrowser/shell/bindings.hpp>

namespace ctbrowser::shell {

// `URL` and `URLSearchParams`, the URL Standard's two interfaces (§6), as thin
// adapters over shell/net/url.hpp - defined in url.cpp, installed as globals.
// Answers the URL constructor so install_window can hang the two blob methods
// (`createObjectURL`, `revokeObjectURL`) on it: those belong to the File API
// and need the bindings' asset registry, which nothing else here does.
[[nodiscard]] script::native_object * install_url(context & cx);

// `TextEncoder` and `TextDecoder`, the Encoding Standard §8 - encoding.cpp,
// which also carries the label table and the single-byte indexes.
void install_encoding(context & cx);

} // namespace ctbrowser::shell
