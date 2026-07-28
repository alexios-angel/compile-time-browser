module;

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module ctbrowser.shell:assets;

import ctbrowser.core;

// Everything a page loads by name - sprites, sounds, JSON, whatever `fetch`
// asks for - comes through here.
//
// The registry is consulted BEFORE the filesystem, and that order is the whole
// design: an application that ships its assets inside the binary keeps working
// when it is run from another directory, and a test that seeds the registry is
// hermetic no matter what happens to be on disk. Falling back to the filesystem
// is what makes `ctbrowse page.html` able to show a page's images without the
// caller registering anything.

export namespace ctbrowser::shell {

class asset_registry {
public:
	// Seeded from app_options::assets, and by whatever the page registers
	// later. A second add() under the same name REPLACES the first, so a caller
	// can override a built-in.
	void add(std::string name, std::vector<std::byte> bytes) {
		for (auto & [existing, data] : entries_) {
			if (existing == name) {
				data = std::move(bytes);
				return;
			}
		}
		entries_.emplace_back(std::move(name), std::move(bytes));
	}

	// Registry only - no filesystem. `fetch` uses this to decide whether a URL
	// was baked in before it considers opening a socket.
	[[nodiscard]] std::span<const std::byte> find(std::string_view name) const {
		for (const auto & [existing, data] : entries_) {
			if (existing == name) { return data; }
		}
		return {};
	}
	[[nodiscard]] bool contains(std::string_view name) const { return !find(name).empty(); }

	// Where a relative path is resolved from when the registry misses. The
	// probe order is v1's: the working directory, then here, then two levels
	// up - which is what makes `assets/sprites.bmp` resolve both from a source
	// checkout and from a build directory beside it.
	void set_base_path(std::filesystem::path base) { base_ = std::move(base); }
	[[nodiscard]] const std::filesystem::path & base_path() const { return base_; }

	// The registry, then the filesystem. Empty when neither has it.
	[[nodiscard]] std::vector<std::byte> load(std::string_view name) const {
		if (const std::span<const std::byte> baked = find(name); !baked.empty()) {
			return {baked.begin(), baked.end()};
		}
		if (name.empty()) { return {}; }
		const std::filesystem::path relative{name};
		if (relative.is_absolute()) { return read_file(relative); }
		for (const std::filesystem::path & root :
		     {std::filesystem::path{"."}, base_, base_ / ".." / ".."}) {
			if (root.empty()) { continue; }
			std::vector<std::byte> bytes = read_file(root / relative);
			if (!bytes.empty()) { return bytes; }
		}
		return {};
	}

	[[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
	[[nodiscard]] static std::vector<std::byte> read_file(const std::filesystem::path & path) {
		std::error_code failed;
		if (!std::filesystem::is_regular_file(path, failed)) { return {}; }
		std::ifstream in{path, std::ios::binary};
		if (!in) { return {}; }
		std::vector<std::byte> bytes;
		bytes.resize(static_cast<std::size_t>(std::filesystem::file_size(path, failed)));
		if (failed) { return {}; }
		in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		bytes.resize(static_cast<std::size_t>(in.gcount()));
		return bytes;
	}

	std::vector<std::pair<std::string, std::vector<std::byte>>> entries_;
	std::filesystem::path base_;
};

} // namespace ctbrowser::shell
