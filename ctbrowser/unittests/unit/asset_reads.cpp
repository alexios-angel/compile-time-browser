#include <ctbrowser.hpp>

#include "check.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

struct scratch_directory {
    std::filesystem::path path;

    ~scratch_directory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

bool write_file(const std::filesystem::path & path, std::string_view text) {
    std::ofstream out{path, std::ios::binary};
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    return out.good();
}

void test_asset_reads() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path =
        std::filesystem::temp_directory_path() / ("ctbrowser-asset-reads-" + std::to_string(stamp));
    const bool created = std::filesystem::create_directory(path);
    CHECK(created);
    if (!created) { return; }
    const scratch_directory scratch{path};

    const auto binary = path / (path.filename().string() + ".bin");
    constexpr std::string_view contents{"\0A\r\n\xff", 5};
    CHECK(write_file(binary, contents));
    CHECK(write_file(path / "empty.bin", ""));
    const auto expected = bytes_of(contents);
    ctbrowser::shell::asset_registry assets;
    assets.set_base_path(path);
    CHECK(assets.load(binary.string()) == expected);
    CHECK(assets.load(binary.filename().string() + "?version=1#fragment") == expected);
    CHECK(assets.load((path / "empty.bin").string()).empty());
    CHECK(assets.load((path / "missing.bin").string()).empty());
    CHECK(assets.load(path.string()).empty());

    // Reopening a changed file observes its current bytes, including empty.
    CHECK(write_file(binary, "C"));
    CHECK(assets.load(binary.string()) == bytes_of("C"));
    CHECK(write_file(binary, ""));
    CHECK(assets.load(binary.string()).empty());

    CHECK(write_file(binary, "D"));
    assets.set_sealed(true);
    CHECK(assets.load(binary.string()).empty());
    assets.add(binary.string(), expected);
    CHECK(assets.load(binary.string()) == expected);
}

} // namespace

int main() {
    test_asset_reads();
    REPORT("asset_reads");
}
