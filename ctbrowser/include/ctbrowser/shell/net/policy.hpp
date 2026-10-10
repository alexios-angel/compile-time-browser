//===- policy.hpp - Fetch policy --------------------------------*- C++ -*-===//
//
// Where a page may reach over the network. A page's script decides what URL
// fetch() asks for, so without a policy any page could make this process talk
// to the machine it runs on and the network behind it: 127.0.0.1, a router's
// admin page, a cloud metadata service at 169.254.169.254. The default is what
// a page from the internet should get: https only, and no private addresses.
//
// Enforced inside http_get (lib/Shell/net/net.cpp) on the URL, on every
// redirect hop, and on the address a socket is actually opened to. The last
// check is what stops a hostname that resolves to a private address (DNS
// rebinding included).
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ctbrowser::shell {

/// What a page's network requests may reach; checked by check_request.
struct fetch_policy {
    /// The URL schemes allowed.
    std::vector<std::string> schemes{"https"};
    /// Empty: any public host. Otherwise an exact host, or `*.example.com` for
    /// its subdomains (not example.com itself).
    std::vector<std::string> hosts;
    /// Whether loopback, RFC 1918, link-local, CGNAT, ULA and the other ranges
    /// is_private_address names are reachable. A local test server, or a
    /// trusted page talking to its own backend, opts in.
    bool allow_private_addresses = false;
    /// The largest response body accepted: 8 MiB.
    std::size_t max_body_bytes = 8U << 20U;
};

/// Why this request is refused, or nothing when the policy allows it. \p host
/// is a URL's host as the URL parser serialises it (IPv6 with or without
/// brackets).
[[nodiscard]] std::optional<std::string> check_request(std::string_view scheme,
                                                       std::string_view host,
                                                       const fetch_policy & policy);

/// Whether \p ip_literal is an IPv4 or IPv6 literal in a range no public page
/// may reach. A hostname is not a literal and is false here; the socket-level
/// check catches what it resolves to.
[[nodiscard]] bool is_private_address(std::string_view ip_literal);

} // namespace ctbrowser::shell
