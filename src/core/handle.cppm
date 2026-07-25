module;
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

export module ctbrowser.core:handle;

// A generation-tagged reference into a slab.
//
// The engine passes these instead of pointers. Under concurrent structural
// mutation a raw pointer is unsound: the slot it names can be freed and reused
// while another thread still holds the pointer, and nothing about the pointer
// says so. A handle carries the generation the slot had when the handle was
// made, so a lookup against a reused slot FAILS rather than silently returning
// somebody else's object.
//
// The generation is also the ABA guard for the slab's free list, which is why
// none of this needs a double-width compare-and-swap.
//
// `Tag` makes handles of different subsystems distinct types - a node_id cannot
// be passed where a layer_id is expected, and that is a compile error rather
// than a very confusing bug.

export namespace ctbrowser {

template <typename Tag> struct handle {
	std::uint32_t slot = 0;
	std::uint32_t generation = 0; // 0 is reserved: a zeroed handle is null

	[[nodiscard]] constexpr explicit operator bool() const noexcept { return generation != 0; }
	[[nodiscard]] friend constexpr auto operator<=>(handle, handle) noexcept = default;
	[[nodiscard]] friend constexpr bool operator==(handle, handle) noexcept = default;

	// The total order over handles is load-bearing, not incidental: multi-node
	// writes acquire their locks in this order, which is what makes concurrent
	// reparenting deadlock-free.
	[[nodiscard]] constexpr std::uint64_t key() const noexcept {
		return (static_cast<std::uint64_t>(slot) << 32) | generation;
	}
};

} // namespace ctbrowser

// hashable, so handles can key the unordered_flat_maps in style and script
export template <typename Tag> struct std::hash<ctbrowser::handle<Tag>> {
	[[nodiscard]] std::size_t operator()(ctbrowser::handle<Tag> h) const noexcept {
		return std::hash<std::uint64_t>{}(h.key());
	}
};
