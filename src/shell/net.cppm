module;

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

export module ctbrowser.shell:net;

// HTTP over Boost.Asio - HEADER-ONLY Boost, which is the project's standing
// constraint (see NOTICE: compiled Boost, Boost.Context above all, breaks the
// llvm-mingw Windows cross-build).
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

export namespace ctbrowser::shell {

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
[[nodiscard]] constexpr bool tls_available() noexcept {
#if CTBROWSER_WITH_TLS
    return true;
#else
    return false;
#endif
}

// A GET, following redirects. Never throws: a failure is an `error` on the
// response, because that is what the caller has to turn into a rejected
// promise anyway.
[[nodiscard]] http_response http_get(std::string_view url, http_options options = {});

} // namespace ctbrowser::shell
