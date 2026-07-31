#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// HTTP over Boost.Asio, which is header-only. That USED to be the project's
// blanket rule; since 2026-07-31 it is not - shell/url.hpp links Boost.URL,
// because Boost.URL cannot be header-only and RFC 3986 was being hand-rolled
// twice. Boost.Context still must not appear: it is per-ABI assembly and is what
// actually breaks the llvm-mingw cross-build. See NOTICE.
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

// One header. A LIST, not a map, because HTTP allows a name to repeat -
// `Set-Cookie` is the one that matters and the one a map would silently lose.
struct http_header {
    std::string name;
    std::string value;
};

// WHY THERE IS A REQUEST TYPE AT ALL, when only GET is issued today.
//
// This surface existed as a single `http_get(url)` and every widening of it -
// a method, a header, a body - was a change to every caller. `fetch()` in a
// page already means all three, and cookies and keep-alive are ahead of it. So
// the shape a browser needs is here now, and the parts not yet honoured say so
// rather than being absent.
enum class http_method : std::uint8_t {
    get,
    head,
    post,
    put,
    patch,
    delete_
};

[[nodiscard]] std::string_view spelling(http_method method) noexcept;

struct http_request {
    http_method method = http_method::get;
    std::string url;
    std::vector<http_header> headers;
    std::vector<std::byte> body; // ignored for get and head
};

struct http_response {
    int status = 0;
    std::string url;   // the FINAL url, after redirects
    std::string error; // non-empty: the request never completed
    std::vector<std::byte> body;
    std::string content_type;
    // EVERY header, in the order the server sent them. `Response.headers` in a
    // page needs this, and content_type above is the one field that was ever
    // exposed - kept because callers read it, and now a shorthand for
    // header("content-type") rather than the only thing available.
    std::vector<http_header> headers;

    // Case-insensitive, because HTTP field names are - and ASCII-only, because
    // that is what the specification says and a locale-aware fold would make
    // the answer depend on the host.
    [[nodiscard]] std::string_view header(std::string_view name) const noexcept;

    // `Response.ok` in the fetch API is the 2xx range, and it is NOT the same
    // question as "did the request complete" - a 404 completed.
    [[nodiscard]] bool ok() const noexcept { return status >= 200 && status < 300; }
    [[nodiscard]] bool completed() const noexcept { return error.empty(); }
};

// Whether this build can do https:// at all.
[[nodiscard]] constexpr bool tls_available() noexcept {
#if CTBROWSER_WITH_TLS
    return true;
#else
    return false;
#endif
}

// Make a request, following redirects. NEVER THROWS: a failure is an `error` on
// the response, because that is what the caller has to turn into a rejected
// promise anyway, and because an exception crossing into the script engine has
// nowhere to go.
[[nodiscard]] http_response fetch(const http_request & request, http_options options = {});

// The common case, kept because it reads better at the call site and because
// every caller today is one.
[[nodiscard]] http_response http_get(std::string_view url, http_options options = {});

} // namespace ctbrowser::shell
