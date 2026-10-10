//===- ctbrowser/unittests/unit/net_basics.cpp - Networking tests ---------===//
//
// shell::http_get over a real socket. The test is hermetic: it stands up an
// HTTP server on the loopback interface and talks to it, so nothing reaches the
// internet, but the code path exercised is the real one, sockets and all.
//
//===----------------------------------------------------------------------===//

// Plain sockets rather than a networking library, so the test adds no
// dependency the engine does not have. A canned-reply server needs only
// listen, accept, read, write and close.
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <ctbrowser.hpp>

#include "check.hpp"

namespace {

// One socket API, two spellings. Winsock needs an explicit startup and spells
// close and the length type differently; nothing else here differs.
#if defined(_WIN32)
using socket_t = SOCKET;
inline constexpr socket_t invalid_socket = INVALID_SOCKET;
inline void close_socket(socket_t s) {
    ::closesocket(s);
}
using socklen_arg = int;
// send/recv take an int length on Winsock and a size_t on POSIX. Naming the
// type is what keeps -Wconversion quiet honestly, rather than casting at each
// call and hoping the cast is the right one on the other platform.
using iolen_t = int;
struct winsock_once {
    winsock_once() {
        WSADATA data;
        ::WSAStartup(MAKEWORD(2, 2), &data);
    }
};
#else
using socket_t = int;
inline constexpr socket_t invalid_socket = -1;
inline void close_socket(socket_t s) {
    ::close(s);
}
using socklen_arg = socklen_t;
using iolen_t = std::size_t;
struct winsock_once {};
#endif

/// A one-shot server: it answers `replies.size()` requests and stops. The
/// canned replies are byte-exact so the client's parsing is what is under test.
class test_server {
public:
    explicit test_server(std::vector<std::string> replies) : replies_{std::move(replies)} {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        // Port zero, and the port read back afterwards: a fixed port makes the
        // suite fail when anything else on the machine happens to hold it, and
        // fail in a way that looks like the client is broken.
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        ::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address));
        ::listen(listener_, 4);
        sockaddr_in bound{};
        auto length = static_cast<socklen_arg>(sizeof(bound));
        ::getsockname(listener_, reinterpret_cast<sockaddr *>(&bound), &length);
        port_ = ::ntohs(bound.sin_port);
        thread_ = std::thread{[this] { serve(); }};
    }
    ~test_server() {
        if (thread_.joinable()) { thread_.join(); }
        if (listener_ != invalid_socket) { close_socket(listener_); }
    }
    test_server(const test_server &) = delete;
    test_server & operator=(const test_server &) = delete;

    /// The URL of \p path on this server.
    [[nodiscard]] std::string url(std::string_view path) const {
        return "http://127.0.0.1:" + std::to_string(port_) + std::string{path};
    }
    /// The raw bytes of the last request received (one read of up to 4 KiB).
    [[nodiscard]] std::string last_request() const { return last_request_; }

private:
    void serve() {
        for (const std::string & reply : replies_) {
            const socket_t peer = ::accept(listener_, nullptr, nullptr);
            if (peer == invalid_socket) { return; }
            char buffer[4096];
            const auto got = ::recv(peer, buffer, sizeof(buffer), 0);
            if (got > 0) { last_request_.assign(buffer, static_cast<std::size_t>(got)); }
            ::send(peer, reply.data(), static_cast<iolen_t>(reply.size()), 0);
            // The client may read to EOF, so the close is the end of the body.
            close_socket(peer);
        }
    }

    [[maybe_unused]] winsock_once winsock_;
    socket_t listener_ = invalid_socket;
    std::vector<std::string> replies_;
    unsigned short port_ = 0;
    std::string last_request_;
    std::thread thread_;
};

/// This suite talks to 127.0.0.1 over http, which the default fetch policy
/// refuses, so every request to the test server opts in explicitly.
[[nodiscard]] ctbrowser::shell::http_options local(ctbrowser::shell::http_options options = {}) {
    options.policy.schemes = {"http", "https"};
    options.policy.allow_private_addresses = true;
    return options;
}

[[nodiscard]] std::string body_text(const ctbrowser::shell::http_response & response) {
    return std::string{reinterpret_cast<const char *>(response.body.data()), response.body.size()};
}

/// A complete 200 response carrying \p body with the given Content-Type.
[[nodiscard]] std::string canned(std::string_view body, std::string_view type = "text/plain") {
    return "HTTP/1.1 200 OK\r\nContent-Type: " + std::string{type} +
           "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + std::string{body};
}

void test_get() {
    test_server server{{canned("{\"n\":7}", "application/json")}};
    const auto response = ctbrowser::shell::http_get(server.url("/thing.json"), local());
    CHECK(response.completed());
    CHECK(response.ok());
    CHECK(response.status == 200);
    CHECK(body_text(response) == "{\"n\":7}");
    CHECK(response.content_type == "application/json");
    // The request line and Host header are what a server routes on.
    CHECK(server.last_request().find("GET /thing.json HTTP/1.1") == 0);
    CHECK(server.last_request().find("Host: 127.0.0.1") != std::string::npos);
}

void test_chunked() {
    // An HTTP/1.1 server may chunk any response. A client that ignores the
    // encoding reads the chunk sizes as content.
    test_server server{{"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                        "5\r\nhello\r\n1\r\n \r\n5\r\nworld\r\n0\r\n\r\n"}};
    const auto response = ctbrowser::shell::http_get(server.url("/stream"), local());
    CHECK(response.ok());
    CHECK(body_text(response) == "hello world");
}

void test_body_limit() {
    test_server server{{canned("AB"), canned("AB"), canned(""), canned("A"),
                        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                        "2\r\nAB\r\n2\r\nCD\r\n0\r\n\r\n"}};
    const auto exact = ctbrowser::shell::http_get(server.url("/exact"), local({.max_bytes = 2}));
    CHECK(exact.completed());
    CHECK(exact.ok());
    CHECK(body_text(exact) == "AB");

    const auto exceeded =
        ctbrowser::shell::http_get(server.url("/exceeded"), local({.max_bytes = 1}));
    CHECK(!exceeded.completed());
    CHECK(exceeded.error == "response body exceeds max_bytes");
    CHECK(exceeded.body.empty());

    const auto empty = ctbrowser::shell::http_get(server.url("/empty"), local({.max_bytes = 0}));
    CHECK(empty.completed());
    CHECK(empty.ok());
    CHECK(empty.body.empty());

    const auto zero = ctbrowser::shell::http_get(server.url("/zero"), local({.max_bytes = 0}));
    CHECK(!zero.completed());
    CHECK(zero.error == exceeded.error);
    CHECK(zero.body.empty());

    // A later chunk exceeding the cap also discards the already accepted bytes.
    const auto chunked =
        ctbrowser::shell::http_get(server.url("/chunked"), local({.max_bytes = 3}));
    CHECK(!chunked.completed());
    CHECK(chunked.error == exceeded.error);
    CHECK(chunked.body.empty());
}

void test_status_and_body() {
    // A 404 completed: it is not a network failure, and fetch only rejects on
    // the latter. Getting this wrong turns every missing page into an exception.
    test_server server{{"HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nno such!!"}};
    const auto response = ctbrowser::shell::http_get(server.url("/missing"), local());
    CHECK(response.completed());
    CHECK(!response.ok());
    CHECK(response.status == 404);
    CHECK(body_text(response) == "no such!!");
}

void test_redirect() {
    // A redirect to an absolute URL, then the real answer. The Location has to
    // name a port that exists, so the target server is stood up first.
    test_server target{{canned("arrived")}};
    test_server hop{{"HTTP/1.1 302 Found\r\nLocation: " + target.url("/final") +
                     "\r\nContent-Length: 0\r\n\r\n"}};
    const auto response = ctbrowser::shell::http_get(hop.url("/start"), local());
    CHECK(response.ok());
    CHECK(body_text(response) == "arrived");
    CHECK(response.url == target.url("/final"));
}

void test_relative_redirect() {
    test_server server{{"HTTP/1.1 301 Moved Permanently\r\nLocation: /elsewhere\r\n"
                        "Content-Length: 0\r\n\r\n",
                        canned("relative worked")}};
    const auto response = ctbrowser::shell::http_get(server.url("/start"), local());
    CHECK(response.ok());
    CHECK(body_text(response) == "relative worked");
}

void test_failures() {
    // Port 1 is not listening. This must come back as an error, not a hang and
    // not an exception.
    const auto refused =
        ctbrowser::shell::http_get("http://127.0.0.1:1/", local({.timeout_ms = 500}));
    CHECK(!refused.completed());
    CHECK(!refused.error.empty());

    const auto nonsense = ctbrowser::shell::http_get("not a url");
    CHECK(!nonsense.completed());

    // ftp:// is not something this client does, and saying so beats trying.
    const auto wrong_scheme = ctbrowser::shell::http_get("ftp://example.com/x");
    CHECK(!wrong_scheme.completed());
}

void test_timeout() {
    // A server that accepts and then says nothing. Without a deadline the frame
    // would block forever; with one the request fails and the page carries on.
    [[maybe_unused]] const winsock_once winsock;
    const socket_t listener = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    ::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    ::listen(listener, 4);
    sockaddr_in bound{};
    auto length = static_cast<socklen_arg>(sizeof(bound));
    ::getsockname(listener, reinterpret_cast<sockaddr *>(&bound), &length);
    const unsigned short port = ::ntohs(bound.sin_port);
    std::atomic<bool> stop{false};
    std::thread server{[&] {
        // Accept and then say nothing, holding the connection open. The socket
        // has to outlive the accept or the client sees a close rather than a
        // hang, and would fail for the wrong reason.
        const socket_t peer = ::accept(listener, nullptr, nullptr);
        while (!stop.load()) { std::this_thread::sleep_for(std::chrono::milliseconds{10}); }
        if (peer != invalid_socket) { close_socket(peer); }
    }};

    const auto response = ctbrowser::shell::http_get(
        "http://127.0.0.1:" + std::to_string(port) + "/hang", local({.timeout_ms = 300}));
    CHECK(!response.completed());
    stop.store(true);
    server.join();
    close_socket(listener);
}

void test_tls_is_honest() {
    // Either this build can do https:// or it says so. What it must never do is
    // fail with a confusing transport error.
    const auto response =
        ctbrowser::shell::http_get("https://127.0.0.1:1/", local({.timeout_ms = 300}));
    CHECK(!response.completed());
    if (!ctbrowser::shell::tls_available()) {
        CHECK(response.error.find("OpenSSL") != std::string::npos);
    }
    std::printf("     https:// %s in this build\n",
                ctbrowser::shell::tls_available() ? "enabled" : "disabled");
}

/// The default fetch policy allows https only and public addresses only,
/// checked on the URL, on each redirect hop, and on the address the socket
/// actually connects to.
void test_policy() {
    using ctbrowser::shell::check_request;
    using ctbrowser::shell::fetch_policy;
    using ctbrowser::shell::is_private_address;
    for (const char * ip :
         {"127.0.0.1", "10.0.0.1", "172.16.0.1", "172.31.255.255", "192.168.1.1", "169.254.1.1",
          "169.254.169.254", "0.0.0.0", "100.64.0.1", "[::1]", "::1", "::", "::ffff:127.0.0.1",
          "::ffff:7f00:1", "fc00::1", "fd12:3456::1", "fe80::1", "fe80::1%eth0"}) {
        CHECK_EQ(is_private_address(ip), true);
    }
    for (const char * ip : {"8.8.8.8", "172.32.0.1", "100.128.0.1", "2001:4860:4860::8888",
                            "::ffff:8.8.8.8", "example.com", "1.2.3", "1::2::3"}) {
        CHECK_EQ(is_private_address(ip), false);
    }
    const fetch_policy defaults;
    CHECK(!check_request("https", "8.8.8.8", defaults).has_value());
    CHECK(check_request("http", "8.8.8.8", defaults).has_value());
    CHECK(check_request("https", "127.0.0.1", defaults).has_value());
    CHECK(check_request("https", "::1", defaults).has_value());
    CHECK(check_request("https", "localhost", defaults).has_value());
    CHECK(check_request("https", "a.localhost", defaults).has_value());
    fetch_policy hosts;
    hosts.hosts = {"*.example.com", "exact.org"};
    CHECK(!check_request("https", "cdn.example.com", hosts).has_value());
    CHECK(check_request("https", "example.com", hosts).has_value());
    CHECK(check_request("https", "evilexample.com", hosts).has_value());
    CHECK(!check_request("https", "exact.org", hosts).has_value());
    CHECK(check_request("https", "sub.exact.org", hosts).has_value());

    // http_get enforces it: no socket is opened for a refused URL.
    const auto refused = ctbrowser::shell::http_get("http://127.0.0.1:1/");
    CHECK(!refused.completed());
    CHECK(refused.error.find("fetch policy") != std::string::npos);
    // An allowed scheme on a private literal is still refused. (A hostname
    // that resolves to one is refused when the socket opens, which is not testable
    // hermetically without a resolver that lies.)
    ctbrowser::shell::http_options http_only;
    http_only.policy.schemes = {"http"};
    const auto private_peer = ctbrowser::shell::http_get("http://127.0.0.1:1/", http_only);
    CHECK(private_peer.error.find("private address") != std::string::npos);
    // A redirect is re-checked on its own URL: only the first hop's host is
    // allowed here, so the Location is refused rather than followed. substr(17)
    // drops the 17-character "http://127.0.0.1:" prefix, keeping port and path.
    test_server target2{{canned("should not arrive")}};
    test_server hop2{{"HTTP/1.1 302 Found\r\nLocation: http://localhost:" +
                      target2.url("/x").substr(17) + "\r\nContent-Length: 0\r\n\r\n"}};
    ctbrowser::shell::http_options redirect_checked = local();
    redirect_checked.policy.hosts = {"127.0.0.1"};
    const auto second = ctbrowser::shell::http_get(hop2.url("/start"), redirect_checked);
    CHECK(!second.completed());
    CHECK(second.error.find("does not allow host localhost") != std::string::npos);
    // target2 was never reached: serve its one reply so its thread can end.
    (void)ctbrowser::shell::http_get(target2.url("/x"), local());
}

} // namespace

int main() {
    test_get();
    test_chunked();
    test_body_limit();
    test_status_and_body();
    test_redirect();
    test_relative_redirect();
    test_failures();
    test_timeout();
    test_tls_is_honest();
    test_policy();

    REPORT("net_basics");
}
