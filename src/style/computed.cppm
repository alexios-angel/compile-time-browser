module;
#include <boost/container/small_vector.hpp>
#include <boost/intrusive_ptr.hpp>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

export module ctbrowser.style:computed;

import ctbrowser.core;

// The resolved style of one element, and the table that shares them.
//
// Most elements in a real document resolve to the SAME computed style: every
// <li> in a list, every cell in a table, every paragraph of body text. Storing
// a separate copy per element wastes memory in proportion to document size and
// - more importantly - destroys the cheapest possible invalidation check,
// because two elements with identical style should be pointer-equal.
//
// So computed styles are interned and refcounted. intrusive_ptr rather than
// shared_ptr: the count lives in the object (one word, one allocation, no
// separate control block), and these are copied constantly as layout passes
// them around.

export namespace ctbrowser::style {

using ctbrowser::atom;

// One resolved declaration. The value stays textual at this stage - turning
// "12px" into a length is layout's job, and doing it here would mean parsing
// values that the box tree never asks for.
struct declaration {
	atom property;
	std::string value;
};

// small_vector because the overwhelming majority of elements carry a handful
// of declarations; a heap allocation each would dominate.
using declaration_list = boost::container::small_vector<declaration, 8>;

[[nodiscard]] inline bool same_declarations(const declaration_list & a,
                                            const declaration_list & b) noexcept {
	if (a.size() != b.size()) { return false; }
	for (std::size_t i = 0; i < a.size(); ++i) {
		if (a[i].property != b[i].property || a[i].value != b[i].value) { return false; }
	}
	return true;
}

[[nodiscard]] inline std::size_t hash_declarations(const declaration_list & d) noexcept {
	std::size_t h = 1469598103934665603ull;
	for (const declaration & one : d) {
		h = (h ^ one.property.id) * 1099511628211ull;
		for (const char c : one.value) { h = (h ^ static_cast<std::size_t>(c)) * 1099511628211ull; }
	}
	return h;
}

class computed_style {
public:
	// The refcount is an atomic, so this type is neither copyable nor
	// movable - interning therefore builds the declaration list first and
	// constructs the style in place around it.
	explicit computed_style(declaration_list d) noexcept : declarations(std::move(d)) {}

	declaration_list declarations;

	[[nodiscard]] std::string_view get(atom property) const noexcept {
		for (const declaration & d : declarations) {
			if (d.property == property) { return d.value; }
		}
		return {};
	}
	[[nodiscard]] bool has(atom property) const noexcept {
		return !get(property).empty();
	}
	[[nodiscard]] std::size_t size() const noexcept { return declarations.size(); }

	[[nodiscard]] bool operator==(const computed_style & o) const noexcept {
		return same_declarations(declarations, o.declarations);
	}
	[[nodiscard]] std::size_t hash() const noexcept { return hash_declarations(declarations); }

private:
	friend void intrusive_ptr_add_ref(const computed_style * s) noexcept {
		s->refs_.fetch_add(1, std::memory_order_relaxed);
	}
	friend void intrusive_ptr_release(const computed_style * s) noexcept {
		// acquire on the last release so the destructor sees every prior write
		if (s->refs_.fetch_sub(1, std::memory_order_release) == 1) {
			std::atomic_thread_fence(std::memory_order_acquire);
			delete s;
		}
	}
	mutable std::atomic<std::uint32_t> refs_{0};
};

using computed_style_ptr = boost::intrusive_ptr<const computed_style>;

// Interning table. Style resolution runs in PARALLEL across elements, so this
// is the one piece of the style engine that several threads write to - hence
// the mutex. It is taken once per resolved element rather than per property,
// and the common case (a style that already exists) is a hash lookup.
class style_table {
public:
	[[nodiscard]] computed_style_ptr intern(declaration_list && candidate) {
		const std::size_t h = hash_declarations(candidate);
		const std::lock_guard lock{mutex_};
		auto & bucket = by_hash_[h];
		for (const computed_style_ptr & existing : bucket) {
			if (same_declarations(existing->declarations, candidate)) { return existing; }
		}
		computed_style_ptr fresh{new computed_style{std::move(candidate)}};
		bucket.push_back(fresh);
		return fresh;
	}

	[[nodiscard]] std::size_t distinct_styles() const {
		const std::lock_guard lock{mutex_};
		std::size_t n = 0;
		for (const auto & [h, bucket] : by_hash_) { n += bucket.size(); }
		return n;
	}

private:
	mutable std::mutex mutex_;
	// hash -> the styles that share it; collisions are compared for real
	flat_map<std::size_t, std::vector<computed_style_ptr>> by_hash_;
};

} // namespace ctbrowser::style
