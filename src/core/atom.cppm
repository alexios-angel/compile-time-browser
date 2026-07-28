module;
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>

export module ctbrowser.core:atom;

import :containers;

// Interned strings.
//
// the previous engine compared tag, class and attribute names as std::string, by value, on every
// selector match and every attribute lookup - so the hottest inner loop in the
// style system was strcmp. An atom is a 32-bit id; equality is an integer
// compare, and the id can key a hash map directly.
//
// Interning is write-rare and read-hot, so the table takes a shared_mutex and
// readers almost never block each other. Storage is a deque because atoms hand
// out string_views into it: those must stay valid as the table grows, which a
// vector cannot promise.

export namespace ctbrowser {

struct atom {
	std::uint32_t id = 0; // 0 is the empty atom

	[[nodiscard]] constexpr explicit operator bool() const noexcept { return id != 0; }
	[[nodiscard]] friend constexpr bool operator==(atom, atom) noexcept = default;
	[[nodiscard]] friend constexpr auto operator<=>(atom, atom) noexcept = default;
};

class atom_table {
public:
	atom_table() {
		storage_.emplace_back(); // id 0 == ""
		index_.emplace(std::string_view{}, 0u);
	}

	atom_table(const atom_table &) = delete;
	atom_table & operator=(const atom_table &) = delete;

	[[nodiscard]] atom intern(std::string_view text) {
		{
			const std::shared_lock read{mutex_};
			if (const auto it = index_.find(text); it != index_.end()) { return atom{it->second}; }
		}
		const std::unique_lock write{mutex_};
		// re-check: another thread may have interned it between the two locks
		if (const auto it = index_.find(text); it != index_.end()) { return atom{it->second}; }
		const auto id = static_cast<std::uint32_t>(storage_.size());
		// The view must key off the STORED copy, not the caller's argument,
		// which may dangle the moment intern() returns.
		const std::string & stored = storage_.emplace_back(text);
		index_.emplace(std::string_view{stored}, id);
		return atom{id};
	}

	// ASCII-lowercasing intern, for HTML tag and attribute names
	[[nodiscard]] atom intern_lower(std::string_view text) {
		std::string folded;
		folded.reserve(text.size());
		for (const char c : text) {
			folded.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
		}
		return intern(folded);
	}

	[[nodiscard]] std::string_view text(atom a) const {
		const std::shared_lock read{mutex_};
		return a.id < storage_.size() ? std::string_view{storage_[a.id]} : std::string_view{};
	}

	[[nodiscard]] std::size_t size() const {
		const std::shared_lock read{mutex_};
		return storage_.size();
	}

private:
	mutable std::shared_mutex mutex_;
	std::deque<std::string> storage_; // stable addresses; string_views point here
	flat_map<std::string_view, std::uint32_t> index_;
};

} // namespace ctbrowser

export template <> struct std::hash<ctbrowser::atom> {
	[[nodiscard]] std::size_t operator()(ctbrowser::atom a) const noexcept {
		return std::hash<std::uint32_t>{}(a.id);
	}
};
