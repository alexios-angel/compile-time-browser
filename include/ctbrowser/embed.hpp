#ifndef CTBROWSER__EMBED__HPP
#define CTBROWSER__EMBED__HPP

#ifndef CTBROWSER_IN_A_MODULE
#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>
#endif

// std::embed, ctbrowser dialect. The public compile-time file API for
// pages and examples - P1967-shaped, but with the project's choices
// baked in (call-protocol modeled on phd::embed, CC0, ThePhD):
//
//   constexpr auto table = ctbrowser::embed("examples/assets/table.bin");
//   constexpr auto maybe = ctbrowser::try_embed("might-not-exist.bin");
//
//  - consteval only: the returned span points into compiler-
//    materialized static storage; there is no runtime cost or file IO.
//  - lookup is EMBED-DIRS ONLY (the builds pass --embed-dir=<repo
//    root>), never quoted/call-site-relative: paths mean the same
//    thing from every header, TU and constexpr frame. This also
//    side-steps the call-stack anchor walk entirely (see the fork's
//    23dd34f8f crash fix for why that walk is delicate).
//  - embed() FAILS THE COMPILE on a missing or un-#depend-ed file,
//    with the reason spelled out in the "call to undefined function"
//    diagnostic; try_embed() returns an empty span instead - the
//    opportunistic flavour assets.hpp builds on.
//  - the TU must declare the dependency gate once:
//      #pragma clang diagnostic push
//      #pragma clang diagnostic ignored "-Wc++2d-extensions"
//      #depend "examples/assets/**"
//      #pragma clang diagnostic pop
//
// Compiled out entirely (CTBROWSER_HAS_STD_EMBED undefined) when the
// builtin is absent - only the project's std::embed clang has it.

#if defined(__has_builtin)
#	if __has_builtin(__builtin_std_embed)
#		define CTBROWSER_HAS_STD_EMBED 1
#	endif
#endif

#ifdef CTBROWSER_HAS_STD_EMBED

namespace ctbrowser {

namespace detail {

// deliberately undefined and not constexpr: calling one fails constant
// evaluation and the function NAME carries the reason into the error
void embed_failed_file_not_found(std::string_view path);
void embed_failed_file_found_but_not_depend_ed(std::string_view path);

// __builtin_std_embed protocol: status/length write through the lvalue
// arguments; statuses 0 = not found, 1 = found, 2 = found but not
// #depend-ed, 3 = found and empty
template <typename T>
consteval std::span<const T> embed_impl(std::string_view path, size_t offset,
                                        bool opportunistic) {
	static_assert(std::is_same_v<T, unsigned char> || std::is_same_v<T, char> ||
	                  std::is_same_v<T, std::byte>,
	              "ctbrowser::embed: T must be unsigned char, char or std::byte");
	constexpr unsigned int embed_dirs_only = 0u;
	int status = -1;
	size_t len = 0;
	const T * res = nullptr;
	res = __builtin_std_embed(embed_dirs_only, status, len, res, path.size(), path.data(),
	                          offset);
	if (status == 1 && res != nullptr) { return {res, len}; }
	if (status == 3) { return {}; } // present, zero bytes
	if (!opportunistic) {
		if (status == 2) { embed_failed_file_found_but_not_depend_ed(path); }
		embed_failed_file_not_found(path);
	}
	return {};
}

} // namespace detail

// the file's bytes, or a compile error that names the file and reason
template <typename T = std::byte>
consteval std::span<const T> embed(std::string_view path, size_t offset = 0) {
	return detail::embed_impl<T>(path, offset, false);
}

// the file's bytes, or an empty span (missing file, missing #depend) -
// callers keep a runtime fallback
template <typename T = std::byte>
consteval std::span<const T> try_embed(std::string_view path, size_t offset = 0) noexcept {
	return detail::embed_impl<T>(path, offset, true);
}

} // namespace ctbrowser

#endif // CTBROWSER_HAS_STD_EMBED

#endif
