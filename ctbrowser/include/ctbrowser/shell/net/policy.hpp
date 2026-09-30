#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// WHERE A PAGE MAY REACH OVER THE NETWORK. A page's script decides what URL
// fetch() asks for, so without a policy any page can make this process talk to
// the machine it runs on and the network behind it - 127.0.0.1, a router's
// admin page, a cloud metadata service at 169.254.169.254. The default is what a
// page from the internet should get: https only, and no private addresses.
//
// Enforced inside http_get (lib/Shell/net/net.cpp) on the URL, on every
// redirect hop, and on the address a socket is actually opened to - the last is
// what a hostname that RESOLVES to a private address (DNS rebinding included)
// cannot get past.

namespace ctbrowser::shell {

struct fetch_policy {
    std::vector<std::string> schemes{"https"};
    // Empty: any public host. Otherwise an exact host, or `*.example.com` for
    // its subdomains (not example.com itself).
    std::vector<std::string> hosts;
    // Loopback, RFC 1918, link-local, CGNAT, ULA, ... - see is_private_address.
    // A local test server or a trusted page talking to its own backend opts in.
    bool allow_private_addresses = false;
    std::size_t max_body_bytes = 8U << 20U;
};

// Why this request is refused, or nothing when the policy allows it. `host` is
// a URL's host as the URL parser serialises it (IPv6 with or without brackets).
[[nodiscard]] std::optional<std::string> check_request(std::string_view scheme,
                                                       std::string_view host,
                                                       const fetch_policy & policy);

// An IPv4 or IPv6 LITERAL in a range no public page may reach. A hostname is
// not a literal and is false here; the socket-level check catches what it
// resolves to.
[[nodiscard]] bool is_private_address(std::string_view ip_literal);

} // namespace ctbrowser::shell
