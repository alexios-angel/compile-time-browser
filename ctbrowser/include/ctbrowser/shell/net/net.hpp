#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// HTTP over libcurl. It used to be a hand-written client on Boost.Asio, and
// that is gone rather than kept as a fallback: Asio is a socket, and the
// request line, header folding, chunked decoding and redirects above it all had
// to be maintained here. curl also brings TLS - Schannel on Windows - which is
// how the cross build has https:// with no OpenSSL. See docs/build.md.
//
// SYNCHRONOUS, on purpose. Promises in this VM are settled when they are made,
// so `await fetch(url)` has to have the bytes by the time fetch returns; there
// is no suspended frame to resume. A request therefore BLOCKS the frame it is
// made in, which is why the timeout is short by default and why the asset
// registry is consulted first - a page that bakes its resources in never waits.
// A real event loop with pending promises would fix this properly and is a
// bigger change than this stage.
//
// HTTPS needs OpenSSL, which is NOT part of Boost and is optional: without it
// the build still does http:// and rejects https:// by name rather than
// silently failing to connect.

// NOTHING third-party is included above. Asio's headers are ~1 MB of C++ and a
// module's global module fragment is SERIALIZED INTO ITS BMI, so including
// them here made ctbrowser.shell-net.pcm 27 MB and every translation unit that
// imported the browser paid to deserialize all of it. They live in net.cpp now,
// where they are compiled once and reach nobody.

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
