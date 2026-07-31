// HTTP over libcurl - the transport behind shell/net.hpp.
//
// WHY libcurl AND NOT THE ASIO IT REPLACES. Asio is a socket; everything above
// it - the request line, header folding, chunked decoding, redirects - was
// hand-written here, and that is the half a browser keeps needing more of.
//
// AND WHY NOT POCO, which was written first and does cross-compile (that work
// is in git history). Two reasons that are about capability rather than taste:
//
//   * TLS ON WINDOWS FOR FREE. libcurl uses Schannel, the operating system's
//     own TLS stack, so https:// on the Windows build needs NO OpenSSL
//     cross-build. The Windows preset ships with CTBROWSER_WITH_TLS=0 today -
//     no https at all - and POCO's NetSSL would have meant cross-building
//     OpenSSL to change that.
//   * IT ALREADY DOES THE REST. HTTP/2, brotli and zstd content encodings,
//     HSTS, alt-svc, IDN. Content-Encoding is on the plan and comes free here,
//     which also retires the zlib work it was going to need.
//
// What POCO had over it is a mature WebSocket; libcurl's is still experimental.
// If WebSocket becomes a real requirement that is the reason to revisit, and
// the interface in net.hpp is what makes revisiting cheap - both are peers
// behind one `fetch()`, selected in src/CMakeLists.txt.
//
// NOTHING libcurl REACHES net.hpp. The public header declares plain structs and
// two functions; every CURL type lives in this file, the same rule url.cpp
// keeps for Boost.URL.
//
// A C API IN A C++ ENGINE, so the two things C gets wrong are handled once
// here: the handle is owned by a unique_ptr with a deleter, and the header list
// by a small RAII holder. There is no path out of this function that leaks.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/net.hpp>
#include <ctbrowser/shell/url.hpp>

#include <curl/curl.h>

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ctbrowser::shell {

namespace {

// ONCE PER PROCESS, and before any handle exists.
//
// This libcurl reports the `threadsafe` feature, so curl_global_init is safe to
// race - but "safe to race" is not "documented to be called from anywhere", and
// older or differently-built libcurls are not. call_once costs nothing and
// removes the question. Deliberately never cleaned up: curl_global_cleanup at
// static-destruction time races with anything still holding a handle, and the
// process is ending anyway.
void ensure_curl() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct easy_deleter {
    void operator()(CURL * handle) const noexcept {
        if (handle != nullptr) { curl_easy_cleanup(handle); }
    }
};
using easy_handle = std::unique_ptr<CURL, easy_deleter>;

// curl_slist is a hand-rolled linked list the caller must free. This owns it.
class header_list {
public:
    header_list() = default;
    header_list(const header_list &) = delete;
    header_list & operator=(const header_list &) = delete;
    ~header_list() {
        if (list_ != nullptr) { curl_slist_free_all(list_); }
    }
    void add(const std::string & line) { list_ = curl_slist_append(list_, line.c_str()); }
    [[nodiscard]] curl_slist * get() const noexcept { return list_; }

private:
    curl_slist * list_ = nullptr;
};

// What the callbacks write into. Passed as the opaque pointer, which is how a C
// callback reaches C++ state.
struct sink {
    std::vector<std::byte> body;
    std::vector<http_header> headers;
    std::size_t max_bytes = 0;
};

std::size_t on_body(char * data, std::size_t size, std::size_t count, void * opaque) {
    auto & into = *static_cast<sink *>(opaque);
    const std::size_t offered = size * count;
    // CAPPED, AND STILL CLAIMING TO HAVE TAKEN IT ALL. Returning less than
    // offered is how a libcurl callback signals failure, which would turn a
    // body that is merely too long into a transport error - and the contract
    // here is that max_bytes TRUNCATES, as the Asio transport did.
    const std::size_t room =
        into.max_bytes > into.body.size() ? into.max_bytes - into.body.size() : 0;
    const std::size_t taking = offered < room ? offered : room;
    const auto * bytes = reinterpret_cast<const std::byte *>(data);
    into.body.insert(into.body.end(), bytes, bytes + taking);
    return offered;
}

std::size_t on_header(char * data, std::size_t size, std::size_t count, void * opaque) {
    auto & into = *static_cast<sink *>(opaque);
    std::string_view line{data, size * count};
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) { line.remove_suffix(1); }
    // A STATUS LINE STARTS A NEW RESPONSE, and with redirects followed inside
    // libcurl there may be several. Only the LAST one's headers are the
    // answer, so each status line throws away what came before it - otherwise a
    // redirect's Location and Content-Type would be reported as the target's.
    if (line.starts_with("HTTP/")) {
        into.headers.clear();
        return size * count;
    }
    if (line.empty()) { return size * count; }
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) { return size * count; }
    std::string_view value = line.substr(colon + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    into.headers.push_back(http_header{std::string{line.substr(0, colon)}, std::string{value}});
    return size * count;
}

} // namespace

std::string_view spelling(http_method method) noexcept {
    switch (method) {
    case http_method::head: return "HEAD";
    case http_method::post: return "POST";
    case http_method::put: return "PUT";
    case http_method::patch: return "PATCH";
    case http_method::delete_: return "DELETE";
    case http_method::get: break;
    }
    return "GET";
}

std::string_view http_response::header(std::string_view name) const noexcept {
    for (const http_header & each : headers) {
        if (ascii_iequals(each.name, name)) { return each.value; }
    }
    return {};
}

http_response fetch(const http_request & request, http_options options) {
    http_response out;
    out.url = request.url;

    // PARSED AND REFUSED HERE rather than by libcurl, and deliberately. libcurl
    // speaks ftp, file, smtp and a dozen others; this client does http and
    // https, and a `file://` reaching a network transport is a bug in the
    // caller rather than a request. shell/url.hpp is also what drops credentials
    // and the fragment, which must not travel.
    const fetch_url target = parse_absolute(request.url);
    if (!target.valid) {
        out.error = "not an http(s) url: " + request.url;
        return out;
    }
    if (target.scheme == "https" && !tls_available()) {
        out.error = "https:// needs TLS, which this build does not have";
        return out;
    }

    ensure_curl();
    const easy_handle handle{curl_easy_init()};
    if (!handle) {
        out.error = "could not create a curl handle";
        return out;
    }

    sink into;
    into.max_bytes = options.max_bytes;

    // Rebuilt from the parsed URL rather than passed through: that is what
    // normalises the scheme and host, removes dot segments and strips the
    // credentials and fragment.
    const std::string url = target.scheme + "://" + target.authority + target.target;
    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, options.user_agent.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT_MS, static_cast<long>(options.timeout_ms));
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_MAXREDIRS, static_cast<long>(options.max_redirects));
    // ONLY http AND https, even after a redirect - a 302 to `file:///etc/passwd`
    // is a real attack and libcurl will follow anything it supports otherwise.
    curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(handle.get(), CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &on_body);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &into);
    curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION, &on_header);
    curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA, &into);

    switch (request.method) {
    case http_method::get: curl_easy_setopt(handle.get(), CURLOPT_HTTPGET, 1L); break;
    case http_method::head: curl_easy_setopt(handle.get(), CURLOPT_NOBODY, 1L); break;
    default:
        curl_easy_setopt(handle.get(), CURLOPT_CUSTOMREQUEST,
                         std::string{spelling(request.method)}.c_str());
        break;
    }
    if (!request.body.empty() && request.method != http_method::get &&
        request.method != http_method::head) {
        curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS,
                         reinterpret_cast<const char *>(request.body.data()));
        curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(request.body.size()));
    }

    header_list sending;
    // Identity, because nothing above this decompresses yet. libcurl WOULD do
    // it - CURLOPT_ACCEPT_ENCODING with an empty string asks for everything it
    // was built with - and turning that on is a one-line change once something
    // wants it. Left off so this commit changes the transport and nothing else.
    sending.add("Accept-Encoding: identity");
    sending.add("Accept: */*");
    // The CALLER'S headers last, so a page can override the defaults - which is
    // what fetch(url, {headers}) means in a page.
    for (const http_header & each : request.headers) { sending.add(each.name + ": " + each.value); }
    curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, sending.get());

    // NEVER THROWS, which the C API makes easy: every failure is a return code.
    const CURLcode result = curl_easy_perform(handle.get());

    long status = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    out.status = static_cast<int>(status);
    const char * effective = nullptr;
    curl_easy_getinfo(handle.get(), CURLINFO_EFFECTIVE_URL, &effective);
    if (effective != nullptr) { out.url = effective; }
    out.headers = std::move(into.headers);
    out.body = std::move(into.body);
    out.content_type = std::string{out.header("content-type")};

    if (result != CURLE_OK) {
        out.error = curl_easy_strerror(result);
        return out;
    }
    return out;
}

http_response http_get(std::string_view url, http_options options) {
    return fetch(http_request{http_method::get, std::string{url}, {}, {}}, options);
}

} // namespace ctbrowser::shell
