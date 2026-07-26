module;

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// Asio, from whichever distribution the build found. Boost's is what this tree
// already depends on (Boost::headers is on three other v2 targets and is
// HEADER-ONLY, which is the standing constraint); STANDALONE Asio is the same
// library under a different namespace, so it is a configure-time switch rather
// than a rewrite.
#if CTBROWSER_ASIO_STANDALONE
#include <asio/connect.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>
#if CTBROWSER_WITH_TLS
#include <asio/ssl.hpp>
#endif
#else
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#if CTBROWSER_WITH_TLS
#include <boost/asio/ssl.hpp>
#endif
#endif

namespace ctbrowser::shell::detail {
#if CTBROWSER_ASIO_STANDALONE
namespace aio = ::asio;
using error_code = ::asio::error_code;
#else
namespace aio = ::boost::asio;
// Boost.Asio reports through Boost.System's error_code; standalone Asio uses
// std::error_code. The two are the only difference the rest of this file sees.
using error_code = ::boost::system::error_code;
#endif
} // namespace ctbrowser::shell::detail

module ctbrowser.shell;

// The HTTP client's implementation - and the only place Asio's headers are
// read. See the note in net.cppm about why they are not in the interface.

namespace ctbrowser::shell {

namespace detail {

struct parsed_url {
	std::string scheme;
	std::string host;
	std::string port;
	std::string target = "/";
	bool valid = false;
};

[[nodiscard]] inline parsed_url parse_url(std::string_view url) {
	parsed_url out;
	const std::size_t scheme_end = url.find("://");
	if (scheme_end == std::string_view::npos) { return out; }
	out.scheme = url.substr(0, scheme_end);
	std::transform(out.scheme.begin(), out.scheme.end(), out.scheme.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	url.remove_prefix(scheme_end + 3);

	const std::size_t path_at = url.find_first_of("/?#");
	std::string_view authority = url.substr(0, path_at);
	if (path_at != std::string_view::npos) {
		// A fragment is CLIENT-side and is never sent.
		std::string_view rest = url.substr(path_at);
		const std::size_t fragment = rest.find('#');
		out.target = std::string{rest.substr(0, fragment)};
		if (out.target.empty() || out.target.front() != '/') { out.target.insert(0, "/"); }
	}
	// Credentials in the authority are parsed off and dropped: this client does
	// not do HTTP auth, and sending them as part of the host would be worse.
	if (const std::size_t at = authority.rfind('@'); at != std::string_view::npos) {
		authority.remove_prefix(at + 1);
	}
	if (const std::size_t colon = authority.rfind(':');
	    colon != std::string_view::npos && authority.find(']', colon) == std::string_view::npos) {
		out.port = std::string{authority.substr(colon + 1)};
		authority = authority.substr(0, colon);
	}
	out.host = std::string{authority};
	if (out.host.size() > 2 && out.host.front() == '[' && out.host.back() == ']') {
		out.host = out.host.substr(1, out.host.size() - 2); // an IPv6 literal
	}
	if (out.port.empty()) { out.port = out.scheme == "https" ? "443" : "80"; }
	out.valid = !out.host.empty() && (out.scheme == "http" || out.scheme == "https");
	return out;
}

[[nodiscard]] inline bool iequals(std::string_view a, std::string_view b) {
	return a.size() == b.size() &&
	       std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		       return std::tolower(static_cast<unsigned char>(x)) ==
		              std::tolower(static_cast<unsigned char>(y));
	       });
}

// One asynchronous operation, given a deadline. Asio's synchronous calls cannot
// be interrupted, so a hung server would hang the frame forever - this starts
// the async form and lets the io_context run only until the timeout.
template <typename Start>
[[nodiscard]] error_code with_deadline(aio::io_context & io, Start start,
                                       std::chrono::milliseconds timeout) {
	error_code result = aio::error::would_block;
	start([&result](const error_code & failed, std::size_t) { result = failed; });
	io.restart();
	io.run_for(timeout);
	if (result == aio::error::would_block) {
		io.stop();
		return aio::error::timed_out;
	}
	return result;
}

struct message {
	int status = 0;
	std::string location;
	std::string content_type;
	std::vector<std::byte> body;
	std::string error;
};

// Status line, headers, then the body - with `Transfer-Encoding: chunked`
// decoded, because an HTTP/1.1 server may use it for any response and a client
// that ignores it reads the chunk sizes as content.
[[nodiscard]] inline message parse_response(std::string_view raw, std::size_t max_bytes) {
	message out;
	const std::size_t headers_end = raw.find("\r\n\r\n");
	if (headers_end == std::string_view::npos) {
		out.error = "malformed response (no header terminator)";
		return out;
	}
	std::string_view head = raw.substr(0, headers_end);
	std::string_view body = raw.substr(headers_end + 4);

	const std::size_t first_line_end = head.find("\r\n");
	std::string_view status_line = head.substr(0, first_line_end);
	if (const std::size_t space = status_line.find(' '); space != std::string_view::npos) {
		const std::string_view code = status_line.substr(space + 1, 3);
		std::from_chars(code.data(), code.data() + code.size(), out.status);
	}
	if (out.status == 0) {
		out.error = "malformed response (no status)";
		return out;
	}

	bool chunked = false;
	std::size_t content_length = std::string_view::npos;
	std::string_view rest = first_line_end == std::string_view::npos ? std::string_view{}
	                                                                : head.substr(first_line_end + 2);
	while (!rest.empty()) {
		const std::size_t line_end = rest.find("\r\n");
		const std::string_view line = rest.substr(0, line_end);
		const std::size_t colon = line.find(':');
		if (colon != std::string_view::npos) {
			const std::string_view name = line.substr(0, colon);
			std::string_view v = line.substr(colon + 1);
			while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) { v.remove_prefix(1); }
			if (iequals(name, "location")) { out.location = std::string{v}; }
			else if (iequals(name, "content-type")) { out.content_type = std::string{v}; }
			else if (iequals(name, "transfer-encoding")) { chunked = v.find("chunked") != std::string_view::npos; }
			else if (iequals(name, "content-length")) {
				std::size_t parsed = 0;
				if (std::from_chars(v.data(), v.data() + v.size(), parsed).ec == std::errc{}) {
					content_length = parsed;
				}
			}
		}
		if (line_end == std::string_view::npos) { break; }
		rest = rest.substr(line_end + 2);
	}

	const auto append = [&](std::string_view piece) {
		const std::size_t room = max_bytes > out.body.size() ? max_bytes - out.body.size() : 0;
		piece = piece.substr(0, std::min(piece.size(), room));
		const auto * bytes = reinterpret_cast<const std::byte *>(piece.data());
		out.body.insert(out.body.end(), bytes, bytes + piece.size());
	};

	if (chunked) {
		while (!body.empty()) {
			const std::size_t line_end = body.find("\r\n");
			if (line_end == std::string_view::npos) { break; }
			std::size_t size = 0;
			const std::string_view size_text = body.substr(0, line_end);
			if (std::from_chars(size_text.data(), size_text.data() + size_text.size(), size, 16).ec !=
			    std::errc{}) {
				break;
			}
			if (size == 0) { break; } // the terminating chunk
			body = body.substr(line_end + 2);
			append(body.substr(0, std::min(size, body.size())));
			if (body.size() <= size + 2) { break; }
			body = body.substr(size + 2);
		}
	} else if (content_length != std::string_view::npos) {
		append(body.substr(0, std::min(content_length, body.size())));
	} else {
		append(body); // the server closed the connection to mark the end
	}
	return out;
}

// One request, no redirect following. `raw` comes back so the caller can decide
// what to do with a 3xx.
[[nodiscard]] inline message fetch_once(const parsed_url & url, const http_options & options) {
	message out;
	const auto timeout = std::chrono::milliseconds{options.timeout_ms};
	std::string request;
	request += "GET " + url.target + " HTTP/1.1\r\n";
	request += "Host: " + url.host + "\r\n";
	request += "User-Agent: " + options.user_agent + "\r\n";
	request += "Accept: */*\r\n";
	// Identity, and close when done: this client reads to EOF and does not keep
	// connections alive, so asking for a compressed body it cannot decode would
	// be asking to fail.
	request += "Accept-Encoding: identity\r\n";
	request += "Connection: close\r\n\r\n";

	try {
		aio::io_context io;
		aio::ip::tcp::resolver resolver{io};
		error_code failed;
		const auto endpoints = resolver.resolve(url.host, url.port, failed);
		if (failed) {
			out.error = "could not resolve " + url.host + " (" + failed.message() + ")";
			return out;
		}

		std::string raw;
		const auto drain = [&](auto & stream) {
			// Read to EOF: `Connection: close` means the server ends the body by
			// closing, and end_of_file is therefore the SUCCESS case here.
			for (;;) {
				char chunk[8192];
				std::size_t got = 0;
				const error_code read_failed = with_deadline(
				    io,
				    [&](auto handler) {
					    stream.async_read_some(aio::buffer(chunk),
					                           [&got, handler](const error_code & ec,
					                                           std::size_t n) {
						                           got = n;
						                           handler(ec, n);
					                           });
				    },
				    timeout);
				raw.append(chunk, got);
				if (read_failed) {
					const bool clean = read_failed == aio::error::eof ||
					                   read_failed == aio::error::connection_reset;
#if CTBROWSER_WITH_TLS
					// OpenSSL reports a server that closes without close_notify
					// as a stream truncation. Most of the web does that.
					const bool truncated = read_failed == aio::ssl::error::stream_truncated;
#else
					const bool truncated = false;
#endif
					if (!clean && !truncated) { out.error = read_failed.message(); }
					return;
				}
				if (raw.size() > options.max_bytes + 65536U) { return; } // headers + the cap
			}
		};

		if (url.scheme == "https") {
#if CTBROWSER_WITH_TLS
			aio::ssl::context tls{aio::ssl::context::tls_client};
			tls.set_default_verify_paths();
			tls.set_verify_mode(aio::ssl::verify_peer);
			aio::ssl::stream<aio::ip::tcp::socket> stream{io, tls};
			stream.set_verify_callback(aio::ssl::host_name_verification(url.host));
			// SNI. Without it a shared-IP host serves the wrong certificate and
			// verification fails for a reason that has nothing to do with the
			// certificate being wrong.
			if (SSL_set_tlsext_host_name(stream.native_handle(), url.host.c_str()) == 0) {
				out.error = "could not set TLS server name";
				return out;
			}
			if (const error_code connect_failed = with_deadline(
			        io,
			        [&](auto handler) {
				        aio::async_connect(
				            stream.next_layer(), endpoints,
				            [handler](const error_code & ec, const auto &) {
					            handler(ec, 0);
				            });
			        },
			        timeout)) {
				out.error = "could not connect to " + url.host + " (" + connect_failed.message() + ")";
				return out;
			}
			if (const error_code handshake_failed = with_deadline(
			        io,
			        [&](auto handler) {
				        stream.async_handshake(aio::ssl::stream_base::client,
				                               [handler](const error_code & ec) {
					                               handler(ec, 0);
				                               });
			        },
			        timeout)) {
				out.error = "TLS handshake with " + url.host + " failed (" +
				            handshake_failed.message() + ")";
				return out;
			}
			if (const error_code write_failed = with_deadline(
			        io,
			        [&](auto handler) {
				        aio::async_write(stream, aio::buffer(request), handler);
			        },
			        timeout)) {
				out.error = write_failed.message();
				return out;
			}
			drain(stream);
#else
			out.error = "https:// needs OpenSSL, which this build does not have "
			            "(configure with OpenSSL to enable TLS)";
			return out;
#endif
		} else {
			aio::ip::tcp::socket socket{io};
			if (const error_code connect_failed = with_deadline(
			        io,
			        [&](auto handler) {
				        aio::async_connect(
				            socket, endpoints,
				            [handler](const error_code & ec, const auto &) {
					            handler(ec, 0);
				            });
			        },
			        timeout)) {
				out.error = "could not connect to " + url.host + " (" + connect_failed.message() + ")";
				return out;
			}
			if (const error_code write_failed = with_deadline(
			        io,
			        [&](auto handler) {
				        aio::async_write(socket, aio::buffer(request), handler);
			        },
			        timeout)) {
				out.error = write_failed.message();
				return out;
			}
			drain(socket);
		}

		if (!out.error.empty()) { return out; }
		message parsed = parse_response(raw, options.max_bytes);
		parsed.error = out.error;
		return parsed;
	} catch (const std::exception & problem) {
		out.error = problem.what();
		return out;
	}
}

} // namespace detail

// A GET, following redirects. Never throws: a failure is an `error` on the
// response, because that is what the caller has to turn into a rejected promise
// anyway.
http_response http_get(std::string_view url, http_options options) {
	http_response out;
	out.url = std::string{url};

	std::string current{url};
	for (int hop = 0; hop <= options.max_redirects; ++hop) {
		const detail::parsed_url target = detail::parse_url(current);
		if (!target.valid) {
			out.error = "not an http(s) url: " + current;
			return out;
		}
		if (target.scheme == "https" && !tls_available()) {
			out.error = "https:// needs OpenSSL, which this build does not have";
			return out;
		}
		detail::message reply = detail::fetch_once(target, options);
		out.url = current;
		out.status = reply.status;
		out.content_type = std::move(reply.content_type);
		out.body = std::move(reply.body);
		if (!reply.error.empty()) {
			out.error = std::move(reply.error);
			return out;
		}
		const bool redirect = out.status == 301 || out.status == 302 || out.status == 303 ||
		                      out.status == 307 || out.status == 308;
		if (!redirect || reply.location.empty()) { return out; }
		// A relative Location is resolved against where we just were, which is
		// the common case for a site redirecting to its own canonical path.
		if (reply.location.find("://") != std::string::npos) {
			current = reply.location;
		} else if (reply.location.front() == '/') {
			current = target.scheme + "://" + target.host + ":" + target.port + reply.location;
		} else {
			const std::size_t last_slash = target.target.rfind('/');
			current = target.scheme + "://" + target.host + ":" + target.port +
			          target.target.substr(0, last_slash + 1) + reply.location;
		}
	}
	out.error = "too many redirects";
	return out;
}

} // namespace ctbrowser::shell
