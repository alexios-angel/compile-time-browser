#pragma once
// ctdrive's emulation of the web-platform-tests' Python handlers - see
// wpt_handlers.cpp.

#include <ctbrowser.hpp>

#include <string>
#include <string_view>

namespace ctdrive {

// The path wptserve would have routed `url` by, given the directory it serves.
[[nodiscard]] std::string server_path(std::string_view url, std::string_view document_root);

// The handler table, for browser_options::on_request.
[[nodiscard]] ctbrowser::shell::request_handler wpt_handlers(std::string document_root);

} // namespace ctdrive
