//===- net.hpp - Synchronous HTTP client ------------------------*- C++ -*-===//
//
// HTTP over libcurl, which also brings TLS (Schannel on Windows), so the cross
// build has https:// with no OpenSSL. See docs/build.md.
//
// Synchronous: a request blocks the frame it is made in, which is why the
// timeout is short by default and why the asset registry is consulted first;
// a page that bakes its resources in never waits.
//
// No third-party header is included here: curl.h stays in net.cpp.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/shell/net/policy.hpp>

namespace ctbrowser::shell {

/// Options for http_get.
struct http_options {
    int timeout_ms = 5000;
    int max_redirects = 5;
    /// The largest body accepted (8 MiB). Exceeding it fails the request and
    /// discards the partial body. Zero accepts only an empty body.
    std::size_t max_bytes = 8U * 1024U * 1024U;
    std::string user_agent = "ctbrowser/2.0";
    /// Which schemes, hosts and addresses may be reached, checked on the URL, on
    /// every redirect hop and on the connected address. The body cap is the
    /// smaller of max_bytes and policy.max_body_bytes.
    fetch_policy policy{};
};

/// The outcome of http_get.
struct http_response {
    int status = 0;
    std::string url;   // The final URL, after redirects.
    std::string error; // Non-empty when the request never completed.
    std::vector<std::byte> body;
    std::string content_type;

    /// `Response.ok` in the fetch API: the 2xx range. Not the same question as
    /// completed(); a 404 completed.
    [[nodiscard]] bool ok() const noexcept { return status >= 200 && status < 300; }
    [[nodiscard]] bool completed() const noexcept { return error.empty(); }
};

/// Whether this build can do https://. Asks libcurl (curl_version_info) at
/// run time, because curl brings its own TLS backend (Schannel on Windows),
/// which no build-time OpenSSL probe can detect.
[[nodiscard]] bool tls_available() noexcept;

/// GET \p url, following redirects (each hop re-checked against
/// options.policy). Never throws: a failure is an `error` on the response,
/// because the caller has to turn it into a rejected promise anyway, and an
/// exception crossing into the script engine has nowhere to go.
[[nodiscard]] http_response http_get(std::string_view url, http_options options = {});

} // namespace ctbrowser::shell
