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

// CONFINEMENT: a name resolves inside its root or not at all - `..`, an
// absolute path and a symlink out of the root all fail; `..` that stays
// inside is fine.
void test_contained() {
    using ctbrowser::shell::asset_registry;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root =
        std::filesystem::temp_directory_path() / ("ctbrowser-contained-" + std::to_string(stamp));
    const auto outside = std::filesystem::temp_directory_path() /
                         ("ctbrowser-contained-outside-" + std::to_string(stamp));
    std::filesystem::create_directories(root / "sub");
    std::filesystem::create_directories(outside);
    const scratch_directory keep_root{root};
    const scratch_directory keep_outside{outside};
    CHECK(write_file(root / "inside.txt", "inside"));
    CHECK(write_file(outside / "secret.txt", "secret"));

    CHECK(asset_registry::contained(root, "inside.txt").has_value());
    CHECK(asset_registry::contained(root, "sub/../inside.txt").has_value());
    CHECK(!asset_registry::contained(root, "../x").has_value());
    CHECK(!asset_registry::contained(root, "sub/../../x").has_value());
    // An absolute name is taken relative to the root, never as itself.
    const auto rooted = asset_registry::contained(root, "/etc/passwd");
    CHECK(rooted.has_value() && rooted->filename() == "passwd" &&
          rooted->parent_path().filename() == "etc");
    // A root that is a prefix of the name's spelling is not its parent.
    CHECK(!asset_registry::contained(root / "sub", "../sub2/x").has_value());

    std::error_code no_symlinks;
    std::filesystem::create_directory_symlink(outside, root / "link", no_symlinks);
    if (!no_symlinks) {
        CHECK(!asset_registry::contained(root, "link/secret.txt").has_value());
        asset_registry assets;
        assets.set_base_path(root);
        CHECK(assets.load("link/secret.txt").empty());
    }

    asset_registry assets;
    assets.set_base_path(root / "sub");
    CHECK(assets.load("../inside.txt").empty());
    CHECK(assets.load((outside / "secret.txt").string()).empty());
    assets.set_base_path(root);
    CHECK(assets.load("inside.txt") == bytes_of("inside"));
    CHECK(assets.load("sub/../inside.txt") == bytes_of("inside"));
    CHECK(assets.load("/inside.txt") == bytes_of("inside"));
    CHECK(assets.load((root / "inside.txt").string()) == bytes_of("inside"));
    CHECK(assets.load("../" + outside.filename().string() + "/secret.txt").empty());
    CHECK(assets
              .load_file_url("file://" + (outside / "secret.txt").generic_string(),
                             "file:///page.html")
              .empty());
    // Only a file: document may read a file: URL at all.
    const std::string inside_url = "file://" + (root / "inside.txt").generic_string();
    CHECK(assets.load_file_url(inside_url, "file:///page.html") == bytes_of("inside"));
    CHECK(assets.load_file_url(inside_url, "https://example.com/").empty());
    // Unconfined is the old probe order, for trusted local content only.
    assets.set_confine(false);
    CHECK(assets.load((outside / "secret.txt").string()) == bytes_of("secret"));
}

// THE PAGE'S VIEW: fetch of an absolute path, a file: URL and a `..` name all
// reject, while an ordinary relative asset loads.
void test_fetch_is_confined() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root =
        std::filesystem::temp_directory_path() / ("ctbrowser-fetch-root-" + std::to_string(stamp));
    std::filesystem::create_directories(root);
    const scratch_directory keep{root};
    CHECK(write_file(root / "data.txt", "hello"));
    const std::string outside = "../" + root.filename().string() + "-escaped.txt";
    CHECK(write_file(root.parent_path() / (root.filename().string() + "-escaped.txt"), "nope"));
    const scratch_directory keep_escaped{root.parent_path() /
                                         (root.filename().string() + "-escaped.txt")};

    ctbrowser::shell::browser page{ctbrowser::shell::browser_options{200, 100}};
    page.assets().set_base_path(root);
    page.load_html("<script>var log = [];"
                   "function probe(u) { fetch(u).then(function (r) { return r.text(); })"
                   ".then(function (t) { log.push(u + '=' + t); },"
                   " function () { log.push(u + '=rejected'); }); }"
                   "probe('data.txt'); probe('/etc/passwd'); probe('file:///etc/passwd');"
                   "probe('" +
                   outside + "');</script>");
    CHECK(page.script_error().empty());
    for (int frame = 0; frame < 4; ++frame) { page.tick(16); }
    CHECK(page.run_script("console.log(log.sort().join('|'));"));
    const auto & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (logged.empty()) { return; }
    const std::string & line = logged.back();
    CHECK(line.find("data.txt=hello") != std::string::npos);
    CHECK(line.find("/etc/passwd=rejected") != std::string::npos);
    CHECK(line.find("file:///etc/passwd=rejected") != std::string::npos);
    CHECK(line.find(outside + "=rejected") != std::string::npos);
    CHECK(line.find("nope") == std::string::npos);
}

} // namespace

int main() {
    test_asset_reads();
    test_contained();
    test_fetch_is_confined();
    REPORT("asset_reads");
}
