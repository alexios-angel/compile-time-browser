// fetch, over a real socket.
//
// HERMETIC: this stands up an HTTP server on the loopback interface and talks to
// it. No test in this suite reaches the internet - a suite whose result depends
// on somebody else's uptime is not a test - but the code path exercised is the
// real one, sockets and all.

// Asio BEFORE the module import, deliberately. On Windows its headers drag in
// <windows.h>, and a non-modular header included AFTER an import that already
// consumed it is seen twice - which clang reports as several hundred
// "conflicting types" errors against identical declarations.
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

import ctbrowser;

#include "check.hpp"

namespace {

using boost::asio::ip::tcp;

// A one-shot server: it answers `replies.size()` requests and stops. The
// canned replies are byte-exact so the client's parsing is what is under test.
class test_server {
public:
	explicit test_server(std::vector<std::string> replies)
	    : acceptor_{io_, tcp::endpoint{boost::asio::ip::make_address("127.0.0.1"), 0}},
	      replies_{std::move(replies)} {
		port_ = acceptor_.local_endpoint().port();
		thread_ = std::thread{[this] { serve(); }};
	}
	~test_server() {
		if (thread_.joinable()) { thread_.join(); }
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
			boost::system::error_code failed;
			tcp::socket socket = acceptor_.accept(failed);
			if (failed) { return; }
			char buffer[4096];
			const std::size_t got = socket.read_some(boost::asio::buffer(buffer), failed);
			last_request_.assign(buffer, got);
			boost::asio::write(socket, boost::asio::buffer(reply), failed);
			// The client reads to EOF, so the shutdown IS the end of the body.
			socket.shutdown(tcp::socket::shutdown_both, failed);
		}
	}

	boost::asio::io_context io_;
	tcp::acceptor acceptor_;
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
	boost::asio::io_context io;
	tcp::acceptor acceptor{io, tcp::endpoint{boost::asio::ip::make_address("127.0.0.1"), 0}};
	const unsigned short port = acceptor.local_endpoint().port();
	std::atomic<bool> stop{false};
	std::thread server{[&] {
		boost::system::error_code failed;
		tcp::socket socket = acceptor.accept(failed);
		while (!stop.load()) { std::this_thread::sleep_for(std::chrono::milliseconds{10}); }
	}};

	const auto response = ctbrowser::shell::http_get(
	    "http://127.0.0.1:" + std::to_string(port) + "/hang", {.timeout_ms = 300});
	CHECK(!response.completed());
	stop.store(true);
	server.join();
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
	test_failures();
	test_timeout();
	test_tls_is_honest();

	REPORT("net_basics");
}
