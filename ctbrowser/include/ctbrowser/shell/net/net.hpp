#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// HTTP over libcurl, which also brings TLS - Schannel on Windows - so the
// cross build has https:// with no OpenSSL. See docs/build.md.
//
// SYNCHRONOUS: a request BLOCKS the frame it is made in, which is why the
// timeout is short by default and why the asset registry is consulted first -
// a page that bakes its resources in never waits.
//
// NOTHING third-party is included above: curl.h lives in net.cpp and reaches
// nobody.

namespace ctbrowser::shell {

struct http_options {
    int timeout_ms = 5000;
    int max_redirects = 5;
    std::size_t max_bytes = 8U * 1024U * 1024U;
    std::string user_agent = "ctbrowser/2.0";
};

struct http_response {
    int status = 0;
    std::string url;   // the FINAL url, after redirects
    std::string error; // non-empty: the request never completed
    std::vector<std::byte> body;
    std::string content_type;

    // `Response.ok` in the fetch API is the 2xx range, and it is NOT the same
    // question as "did the request complete" - a 404 completed.
    [[nodiscard]] bool ok() const noexcept { return status >= 200 && status < 300; }
    [[nodiscard]] bool completed() const noexcept { return error.empty(); }
};

// Whether this build can do https:// at all.
//
// A RUNTIME QUESTION NOW, and it had to become one. This was constexpr on
// CTBROWSER_WITH_TLS, which was set by `find_package(OpenSSL)` - correct while
// the transport was Asio and the engine linked OpenSSL itself. With libcurl the
// engine links no TLS library at all: curl brings its own, and on Windows that
// is SCHANNEL, the operating system's stack, which no OpenSSL probe can see.
//
// So the transport answers instead, and the libcurl one answers by ASKING
// LIBCURL - curl_version_info reports what it was actually built with, which
// cannot drift from the truth the way a build-system guess can.
[[nodiscard]] bool tls_available() noexcept;

// GET, following redirects. NEVER THROWS: a failure is an `error` on the
// response, because that is what the caller has to turn into a rejected
// promise anyway, and because an exception crossing into the script engine has
// nowhere to go.
[[nodiscard]] http_response http_get(std::string_view url, http_options options = {});

} // namespace ctbrowser::shell
