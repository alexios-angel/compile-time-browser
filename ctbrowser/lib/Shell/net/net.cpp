// HTTP over libcurl - the transport behind shell/net/net.hpp. Why libcurl
// rather than Asio or POCO is in docs/build.md.
//
// NOTHING libcurl REACHES net.hpp. The public header declares plain structs and
// two functions; every CURL type lives in this file, the same rule url.cpp
// keeps for Boost.URL.
//
// A C API IN A C++ ENGINE, so the two things C gets wrong are handled once
// here: the handle is owned by a unique_ptr with a deleter, and the header list
// by a small RAII holder. There is no path out of this function that leaks.

#include <ctbrowser/shell/net/net.hpp>
#include <ctbrowser/shell/net/url.hpp>

#include <curl/curl.h>

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

// What the body callback writes into. Passed as the opaque pointer, which is
// how a C callback reaches C++ state.
struct sink {
    std::vector<std::byte> body;
    std::size_t max_bytes = 0;
};

std::size_t on_body(char * data, std::size_t size, std::size_t count, void * opaque) {
    auto & into = *static_cast<sink *>(opaque);
    const std::size_t offered = size * count;
    // CAPPED, AND STILL CLAIMING TO HAVE TAKEN IT ALL. Returning less than
    // offered is how a libcurl callback signals failure, which would turn a
    // body that is merely too long into a transport error - and the contract
    // here is that max_bytes TRUNCATES.
    const std::size_t room =
        into.max_bytes > into.body.size() ? into.max_bytes - into.body.size() : 0;
    const std::size_t taking = offered < room ? offered : room;
    const auto * bytes = reinterpret_cast<const std::byte *>(data);
    into.body.insert(into.body.end(), bytes, bytes + taking);
    return offered;
}

} // namespace

// ASKED OF LIBCURL, not of the build system. curl_version_info reports the
// features the linked library actually has, so this is right whether the
// backend is Schannel on Windows, OpenSSL on Linux, or absent entirely.
bool tls_available() noexcept {
    ensure_curl();
    const curl_version_info_data * about = curl_version_info(CURLVERSION_NOW);
    return about != nullptr && (about->features & CURL_VERSION_SSL) != 0;
}

http_response http_get(std::string_view url_text, http_options options) {
    http_response out;
    out.url = std::string{url_text};

    // PARSED AND REFUSED HERE rather than by libcurl, and deliberately. libcurl
    // speaks ftp, file, smtp and a dozen others; this client does http and
    // https, and a `file://` reaching a network transport is a bug in the
    // caller rather than a request. shell/net/url.hpp is also what drops credentials
    // and the fragment, which must not travel.
    const fetch_url target = parse_absolute(url_text);
    if (!target.valid) {
        out.error = "not an http(s) url: " + out.url;
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
    curl_easy_setopt(handle.get(), CURLOPT_HTTPGET, 1L);

    header_list sending;
    // Identity, because nothing above this decompresses yet. libcurl WOULD do
    // it - CURLOPT_ACCEPT_ENCODING with an empty string asks for everything it
    // was built with - and turning that on is a one-line change once something
    // wants it. Left off so this commit changes the transport and nothing else.
    sending.add("Accept-Encoding: identity");
    sending.add("Accept: */*");
    curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, sending.get());

    // NEVER THROWS, which the C API makes easy: every failure is a return code.
    const CURLcode result = curl_easy_perform(handle.get());

    long status = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    out.status = static_cast<int>(status);
    const char * effective = nullptr;
    curl_easy_getinfo(handle.get(), CURLINFO_EFFECTIVE_URL, &effective);
    if (effective != nullptr) { out.url = effective; }
    // The FINAL response's, after redirects - libcurl reports the last one.
    const char * content_type = nullptr;
    curl_easy_getinfo(handle.get(), CURLINFO_CONTENT_TYPE, &content_type);
    if (content_type != nullptr) { out.content_type = content_type; }
    out.body = std::move(into.body);

    if (result != CURLE_OK) {
        out.error = curl_easy_strerror(result);
        return out;
    }
    return out;
}

} // namespace ctbrowser::shell
