//===- wpt_handlers.hpp - wptserve emulation --------------------*- C++ -*-===//
//
// ctdrive's emulation of the web-platform-tests' Python handlers; see
// wpt_handlers.cpp.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctbrowser.hpp>

#include <string>
#include <string_view>

namespace ctdrive {

/// The path wptserve would have routed `url` by, given the directory it serves.
[[nodiscard]] std::string server_path(std::string_view url, std::string_view document_root);

/// The handler table, for app_options::on_request: answers the emulated `.py`
/// paths and `*.sub.*` files, and returns nullopt for everything else.
[[nodiscard]] ctbrowser::shell::request_handler wpt_handlers(std::string document_root);

} // namespace ctdrive
