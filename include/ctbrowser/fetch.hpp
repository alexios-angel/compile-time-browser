#ifndef CTBROWSER__FETCH__HPP
#define CTBROWSER__FETCH__HPP

#ifndef CTBROWSER_IN_A_MODULE
#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>
#endif

// std::fetch, ctbrowser dialect. embed.hpp's network sibling: an
// HTTP(S) GET performed DURING CONSTANT EVALUATION, response bytes
// handed back as a span into compiler-materialized static storage.
//
//   constexpr auto page   = ctbrowser::fetch("https://example.com/");
//   constexpr auto pinned = ctbrowser::fetch(url, "aabb...ff"); // SHA-256 hex
//   constexpr auto maybe  = ctbrowser::try_fetch(url);
//
//  - consteval only: no runtime cost and NO runtime networking.
//  - the compiler must authorize every URL: builds pass one or more
//    --fetch-allow=<url-glob> (`*` within a path segment, `**` across
//    '/'). NOTHING is allowed by default.
//  - there is NO caching: every build re-fetches. Pin bytes with the
//    optional lowercase-hex SHA-256 argument for reproducible builds -
//    anything network-sourced that feeds a golden test should be pinned.
//  - fetch() FAILS THE COMPILE on an un-allowed URL, a non-200
//    response or a pin mismatch, with the reason spelled out in the
//    "call to undefined function" diagnostic; try_fetch() returns an
//    empty span instead - the opportunistic flavour, matching
//    try_embed(). CAVEAT: a transport-level failure (DNS, no route, no
//    curl in the compiler) is a hard compiler diagnostic, not a
//    status - unlike try_embed, try_fetch cannot swallow being
//    offline, so keep it off default build paths.
//
// Compiled out entirely (CTBROWSER_HAS_STD_FETCH undefined) when the
// builtin is absent - only the project's std::embed clang has it.

#if defined(__has_builtin)
#	if __has_builtin(__builtin_std_fetch)
#		define CTBROWSER_HAS_STD_FETCH 1
#	endif
#endif

#ifdef CTBROWSER_HAS_STD_FETCH

namespace ctbrowser {

namespace detail {

// deliberately undefined and not constexpr: calling one fails constant
// evaluation and the function NAME carries the reason into the error
void fetch_failed_url_not_on_the_fetch_allow_list(std::string_view url);
void fetch_failed_http_response_not_ok(std::string_view url);
void fetch_failed_sha256_pin_mismatch(std::string_view url);

// __builtin_std_fetch protocol: status/length write through the lvalue
// arguments; statuses 1 = ok, 2 = not authorized, 3 = request failed
// (non-200), 4 = ok but empty body, 5 = content-hash mismatch
template <typename T>
consteval std::span<const T> fetch_impl(std::string_view url, std::string_view sha256_hex,
                                        bool opportunistic) {
	static_assert(std::is_same_v<T, unsigned char> || std::is_same_v<T, char> ||
	                  std::is_same_v<T, std::byte>,
	              "ctbrowser::fetch: T must be unsigned char, char or std::byte");
	constexpr unsigned int absolute_locus = 0u; // a URL has no quoted-search anchor
	int status = -1;
	size_t len = 0;
	const T * res = nullptr;
	if (sha256_hex.empty()) {
		res = __builtin_std_fetch(absolute_locus, status, len, res, url.size(), url.data());
	} else {
		res = __builtin_std_fetch(absolute_locus, status, len, res, url.size(), url.data(),
		                          sha256_hex.size(), sha256_hex.data());
	}
	if (status == 1 && res != nullptr) { return {res, len}; }
	if (status == 4) { return {}; } // 200, zero bytes
	if (!opportunistic) {
		if (status == 2) { fetch_failed_url_not_on_the_fetch_allow_list(url); }
		if (status == 5) { fetch_failed_sha256_pin_mismatch(url); }
		fetch_failed_http_response_not_ok(url);
	}
	return {};
}

} // namespace detail

// the URL's bytes, or a compile error that names the URL and reason
template <typename T = std::byte>
consteval std::span<const T> fetch(std::string_view url, std::string_view sha256_hex = {}) {
	return detail::fetch_impl<T>(url, sha256_hex, false);
}

// the URL's bytes, or an empty span (not allow-listed, non-200, pin
// mismatch) - callers keep a fallback; see the transport caveat above
template <typename T = std::byte>
consteval std::span<const T> try_fetch(std::string_view url,
                                       std::string_view sha256_hex = {}) noexcept {
	return detail::fetch_impl<T>(url, sha256_hex, true);
}

} // namespace ctbrowser

#endif // CTBROWSER_HAS_STD_FETCH

#endif
