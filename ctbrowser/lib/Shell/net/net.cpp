//===- ctbrowser/lib/Shell/net/net.cpp - HTTP over libcurl ----------------===//
//
// HTTP over libcurl, the transport behind shell/net/net.hpp. Why libcurl
// rather than Asio or POCO is in docs/build.md.
//
// Nothing from libcurl reaches net.hpp: the public header declares plain
// structs and two functions, and every CURL type stays in this file.
//
// libcurl is a C API, so its resources are owned by RAII here: the easy
// handle by a unique_ptr with a deleter, and the header list by a small
// holder, so no path out of http_get leaks.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/shell/net/net.hpp>
#include <ctbrowser/shell/net/url.hpp>

#include <curl/curl.h>

#if defined(_WIN32)
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ctbrowser::shell {

namespace {

/// Initialises libcurl once per process, before any handle exists.
///
/// A libcurl built with the `threadsafe` feature makes curl_global_init safe
/// to race, but older or differently built ones are not, and call_once costs
/// nothing. Deliberately never cleaned up: curl_global_cleanup at
/// static-destruction time races with anything still holding a handle, and
/// the process is ending anyway.
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

/// Owns a curl_slist, libcurl's linked list of header lines, which the caller
/// must free.
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

/// What the body callback writes into, passed as the opaque pointer, which is
/// how a C callback reaches C++ state.
struct sink {
    std::vector<std::byte> body;
    std::size_t max_bytes = 0;
    bool limit_exceeded = false;
};

/// CURLOPT_WRITEFUNCTION callback: appends a chunk of the body to the sink.
/// Returning less than `size * count` makes libcurl abort the transfer.
std::size_t on_body(char * data, std::size_t size, std::size_t count, void * opaque) {
    auto & into = *static_cast<sink *>(opaque);
    // Reject before multiplying or copying: a prefix is not a completed body.
    const std::size_t room = into.max_bytes - into.body.size();
    if (size != 0 && count > room / size) {
        into.limit_exceeded = true;
        return 0;
    }
    const std::size_t offered = size * count;
    const auto * bytes = reinterpret_cast<const std::byte *>(data);
    into.body.insert(into.body.end(), bytes, bytes + offered);
    return offered;
}

/// State for open_checked_socket: whether private peers are allowed, and
/// whether a connection was refused.
struct peer_check {
    bool allow_private = false;
    bool refused = false;
};

/// CURLOPT_OPENSOCKETFUNCTION callback: opens the socket only if its address
/// is one the policy allows. The address is formatted and handed to
/// is_private_address, so there is one definition of "private" rather than a
/// second one over sockaddr bytes.
curl_socket_t open_checked_socket(void * opaque, curlsocktype, curl_sockaddr * address) {
    auto & check = *static_cast<peer_check *>(opaque);
    if (!check.allow_private) {
        char text[INET6_ADDRSTRLEN] = {};
        const void * raw = nullptr;
        if (address->family == AF_INET) {
            raw = &reinterpret_cast<const sockaddr_in *>(&address->addr)->sin_addr;
        } else if (address->family == AF_INET6) {
            raw = &reinterpret_cast<const sockaddr_in6 *>(&address->addr)->sin6_addr;
        }
        // Not IPv4 or IPv6 (a unix socket), or unprintable: refused, not guessed.
        if (raw == nullptr || inet_ntop(address->family, raw, text, sizeof(text)) == nullptr ||
            is_private_address(text)) {
            check.refused = true;
            return CURL_SOCKET_BAD;
        }
    }
    return ::socket(address->family, address->socktype, address->protocol);
}

} // namespace

/// Whether the linked libcurl supports TLS. Asked of libcurl, not of the build
/// system: curl_version_info reports the features the linked library actually
/// has, so this is right whether the backend is Schannel on Windows, OpenSSL
/// on Linux, or absent entirely.
bool tls_available() noexcept {
    ensure_curl();
    const curl_version_info_data * about = curl_version_info(CURLVERSION_NOW);
    return about != nullptr && (about->features & CURL_VERSION_SSL) != 0;
}

http_response http_get(std::string_view url_text, http_options options) {
    http_response out;
    out.url = std::string{url_text};

    ensure_curl();
    const easy_handle handle{curl_easy_init()};
    if (!handle) {
        out.error = "could not create a curl handle";
        return out;
    }

    sink into;
    into.max_bytes = std::min(options.max_bytes, options.policy.max_body_bytes);
    peer_check peers{options.policy.allow_private_addresses, false};

    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, options.user_agent.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT_MS, static_cast<long>(options.timeout_ms));
    // Redirects are followed here, not by libcurl, so every hop is parsed and
    // checked against the policy the same way the first URL is; a public
    // server answering `302 Location: http://169.254.169.254/` is the attack.
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 0L);
    // Only http and https, even after a redirect: a 302 to `file:///etc/passwd`
    // is a real attack, and libcurl would otherwise follow anything it supports.
    curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(handle.get(), CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &on_body);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &into);
    curl_easy_setopt(handle.get(), CURLOPT_HTTPGET, 1L);
    // The address actually connected to is checked as the socket is opened: a
    // public hostname that resolves to 127.0.0.1 (or re-resolves to it, which
    // is DNS rebinding) is refused here, after resolution and before connect.
    curl_easy_setopt(handle.get(), CURLOPT_OPENSOCKETFUNCTION, &open_checked_socket);
    curl_easy_setopt(handle.get(), CURLOPT_OPENSOCKETDATA, &peers);

    header_list sending;
    // Ask for identity encoding because nothing above this layer decompresses.
    // Setting CURLOPT_ACCEPT_ENCODING to "" would let libcurl negotiate and
    // decode every encoding it was built with.
    sending.add("Accept-Encoding: identity");
    sending.add("Accept: */*");
    curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, sending.get());

    std::string next{url_text};
    for (int hop = 0;; ++hop) {
        // Parsed and refused here rather than by libcurl, deliberately.
        // libcurl speaks ftp, file, smtp and a dozen others; this client does
        // http and https, and a `file://` reaching a network transport is a bug
        // in the caller rather than a request. shell/net/url.hpp also drops
        // the credentials and the fragment, which must not be sent.
        const fetch_url target = parse_absolute(next);
        if (!target.valid) {
            out.error = "not an http(s) url: " + next;
            return out;
        }
        if (std::optional<std::string> refused =
                check_request(target.scheme, target.host, options.policy)) {
            out.error = std::move(*refused);
            return out;
        }
        if (target.scheme == "https" && !tls_available()) {
            out.error = "https:// needs TLS, which this build does not have";
            return out;
        }
        // Rebuilt from the parsed URL rather than passed through: that is what
        // normalises the scheme and host, removes dot segments and strips the
        // credentials and fragment.
        const std::string url = target.scheme + "://" + target.authority + target.target;
        curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
        into.body.clear();
        into.limit_exceeded = false;
        peers.refused = false;

        // Never throws: every failure is a return code.
        const CURLcode result = curl_easy_perform(handle.get());

        long status = 0;
        curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
        out.status = static_cast<int>(status);
        out.url = url;
        const char * content_type = nullptr;
        curl_easy_getinfo(handle.get(), CURLINFO_CONTENT_TYPE, &content_type);
        out.content_type = content_type != nullptr ? content_type : "";
        if (peers.refused) {
            out.body.clear();
            out.error = "the fetch policy does not allow a private address for " + url;
            return out;
        }
        if (into.limit_exceeded) {
            out.body.clear();
            out.error = "response body exceeds max_bytes";
            return out;
        }
        if (result != CURLE_OK) {
            out.body = std::move(into.body);
            out.error = curl_easy_strerror(result);
            return out;
        }
        const char * location = nullptr;
        curl_easy_getinfo(handle.get(), CURLINFO_REDIRECT_URL, &location);
        const bool redirect =
            status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
        if (!redirect || location == nullptr) {
            out.body = std::move(into.body);
            return out;
        }
        if (hop >= options.max_redirects) {
            out.error = "too many redirects";
            return out;
        }
        next = location;
    }
}

} // namespace ctbrowser::shell
