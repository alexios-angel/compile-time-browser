// fetch, over a real socket.
//
// HERMETIC: this stands up an HTTP server on the loopback interface and talks to
// it. No test in this suite reaches the internet - a suite whose result depends
// on somebody else's uptime is not a test - but the code path exercised is the
// real one, sockets and all.

// PLAIN SOCKETS, because the engine no longer depends on Asio and a test
// harness has no business reintroducing the dependency it just removed. What a
// canned-reply server needs - listen, accept, read once, write, close - is a
// dozen calls, and having them here means `ctest` proves the transport against
// a real socket without Boost.Asio existing anywhere in the tree.
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

// A one-shot server: it answers `replies.size()` requests and stops. The
// canned replies are byte-exact so the client's parsing is what is under test.
class test_server {
public:
    explicit test_server(std::vector<std::string> replies) : replies_{std::move(replies)} {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        // PORT ZERO, and the port read back afterwards: a fixed port makes the
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

    [[nodiscard]] std::string url(std::string_view path) const {
        return "http://127.0.0.1:" + std::to_string(port_) + std::string{path};
    }
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
            // The client may read to EOF, so the close IS the end of the body.
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

[[nodiscard]] std::string body_text(const ctbrowser::shell::http_response & response) {
    return std::string{reinterpret_cast<const char *>(response.body.data()), response.body.size()};
}

[[nodiscard]] std::string canned(std::string_view body, std::string_view type = "text/plain") {
    return "HTTP/1.1 200 OK\r\nContent-Type: " + std::string{type} +
           "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + std::string{body};
}

void test_get() {
    test_server server{{canned("{\"n\":7}", "application/json")}};
    const auto response = ctbrowser::shell::http_get(server.url("/thing.json"));
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
    // An HTTP/1.1 server may chunk ANY response. A client that ignores the
    // encoding reads the chunk sizes as content.
    test_server server{{"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                        "5\r\nhello\r\n1\r\n \r\n5\r\nworld\r\n0\r\n\r\n"}};
    const auto response = ctbrowser::shell::http_get(server.url("/stream"));
    CHECK(response.ok());
    CHECK(body_text(response) == "hello world");
}

void test_status_and_body() {
    // A 404 COMPLETED - it is not a network failure, and fetch only rejects on
    // the latter. Getting this wrong turns every missing page into an exception.
    test_server server{{"HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nno such!!"}};
    const auto response = ctbrowser::shell::http_get(server.url("/missing"));
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
    const auto response = ctbrowser::shell::http_get(hop.url("/start"));
    CHECK(response.ok());
    CHECK(body_text(response) == "arrived");
    CHECK(response.url == target.url("/final"));
}

void test_relative_redirect() {
    test_server server{{"HTTP/1.1 301 Moved Permanently\r\nLocation: /elsewhere\r\n"
                        "Content-Length: 0\r\n\r\n",
                        canned("relative worked")}};
    const auto response = ctbrowser::shell::http_get(server.url("/start"));
    CHECK(response.ok());
    CHECK(body_text(response) == "relative worked");
}

void test_the_request_carries_a_method_and_headers() {
    // The surface widened from `http_get(url)` to a request, so it is exercised
    // rather than merely compiled. The server echoes what it was sent, which is
    // the only way to prove the request line and headers left this process.
    test_server server{{canned("ok")}};
    ctbrowser::shell::http_request request;
    request.method = ctbrowser::shell::http_method::post;
    request.url = server.url("/submit");
    request.headers.push_back({"X-Test", "sent"});
    const auto response = ctbrowser::shell::fetch(request);
    CHECK(response.ok());
    const std::string sent = server.last_request();
    CHECK(sent.starts_with("POST /submit HTTP/1.1"));
    CHECK(sent.find("X-Test: sent") != std::string::npos);
}

void test_every_response_header_is_kept() {
    // content_type used to be the ONLY header a caller could see. A page's
    // `Response.headers` needs all of them, and a name may repeat - Set-Cookie
    // is the one that matters, and a map would have lost one of these two.
    test_server server{{"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
                        "Set-Cookie: a=1\r\nSet-Cookie: b=2\r\n"
                        "Content-Length: 2\r\n\r\nhi"}};
    const auto response = ctbrowser::shell::http_get(server.url("/h"));
    CHECK(response.ok());
    CHECK(response.content_type == "text/plain");
    // Case-insensitive, because HTTP field names are.
    CHECK(response.header("CONTENT-TYPE") == "text/plain");
    CHECK(response.header("no-such-header").empty());
    int cookies = 0;
    for (const auto & each : response.headers) {
        if (each.name == "Set-Cookie") { ++cookies; }
    }
    CHECK(cookies == 2);
}

void test_failures() {
    // Port 1 is not listening. This must come back as an error, not a hang and
    // not an exception.
    const auto refused = ctbrowser::shell::http_get("http://127.0.0.1:1/", {.timeout_ms = 500});
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
        // ACCEPT AND THEN SAY NOTHING, holding the connection open. The socket
        // has to outlive the accept or the client sees a close rather than a
        // hang, and would fail for the wrong reason.
        const socket_t peer = ::accept(listener, nullptr, nullptr);
        while (!stop.load()) { std::this_thread::sleep_for(std::chrono::milliseconds{10}); }
        if (peer != invalid_socket) { close_socket(peer); }
    }};

    const auto response = ctbrowser::shell::http_get(
        "http://127.0.0.1:" + std::to_string(port) + "/hang", {.timeout_ms = 300});
    CHECK(!response.completed());
    stop.store(true);
    server.join();
    close_socket(listener);
}

void test_tls_is_honest() {
    // Either this build can do https:// or it says so. What it must never do is
    // fail with a confusing transport error.
    const auto response = ctbrowser::shell::http_get("https://127.0.0.1:1/", {.timeout_ms = 300});
    CHECK(!response.completed());
    if (!ctbrowser::shell::tls_available()) {
        CHECK(response.error.find("OpenSSL") != std::string::npos);
    }
    std::printf("     https:// %s in this build\n",
                ctbrowser::shell::tls_available() ? "enabled" : "disabled");
}

} // namespace

int main() {
    test_get();
    test_chunked();
    test_status_and_body();
    test_redirect();
    test_relative_redirect();
    test_the_request_carries_a_method_and_headers();
    test_every_response_header_is_kept();
    test_failures();
    test_timeout();
    test_tls_is_honest();

    REPORT("net_basics");
}
