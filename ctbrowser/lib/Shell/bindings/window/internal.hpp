//===- internal.hpp - Window bindings' private declarations -----*- C++ -*-===//
//
// Install functions shared between the files of lib/Shell/bindings/window/.
// Private to that directory; nothing outside it includes this header.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <ctbrowser/shell/bindings.hpp>

#include <functional>
#include <string>
#include <string_view>

namespace ctbrowser::shell {

/// Installs `locationbar`, `menubar` and the other BarProp objects onto
/// `window` (window.cpp).
void install_bar_props(context & cx, script::object_object & window, value prototype);

/// Installs `URL` and `URLSearchParams` (URL Standard §6) as globals: thin
/// adapters over shell/net/url.hpp, defined in url.cpp. Returns the URL
/// constructor so install_window can add the two File API methods
/// (`createObjectURL`, `revokeObjectURL`) to it, since only those need the
/// bindings' asset registry.
[[nodiscard]] script::native_object * install_url(context & cx);

/// Installs `TextEncoder` and `TextDecoder` (Encoding Standard §8). Defined in
/// encoding.cpp, which also holds the label table and the single-byte indexes.
void install_encoding(context & cx);

/// Throws a DOMException: (context, exception name, message). The bindings pass
/// their own `throw_dom_exception` here.
using throw_dom_fn = std::function<void(context &, std::string_view, std::string)>;
/// Installs `crypto` and the `Crypto` interface onto `window` (crypto.cpp).
/// DOMExceptions are thrown through `throw_dom`.
void install_crypto(context & cx, script::object_object & window, const throw_dom_fn & throw_dom);

} // namespace ctbrowser::shell
