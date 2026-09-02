#pragma once
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/shell/net/url.hpp>

// Everything a page loads by name - sprites, sounds, JSON, whatever `fetch`
// asks for - comes through here.
//
// The registry is consulted BEFORE the filesystem, and that order is the whole
// design: an application that ships its assets inside the binary keeps working
// when it is run from another directory, and a test that seeds the registry is
// hermetic no matter what happens to be on disk. Falling back to the filesystem
// is what makes `ctbrowse page.html` able to show a page's images without the
// caller registering anything.

namespace ctbrowser::shell {

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
    // probe order is the previous engine's: the working directory, then here, then two levels
    // up - which is what makes `assets/sprites.bmp` resolve both from a source
    // checkout and from a build directory beside it.
    void set_base_path(std::filesystem::path base) { base_ = std::move(base); }
    [[nodiscard]] const std::filesystem::path & base_path() const { return base_; }

    // WHAT A LEADING `/` MEANS. Empty by default, and then it means what it has
    // always meant here: a path from the root of this filesystem.
    //
    // A page written for a SERVER uses server-absolute paths for its shared
    // resources - `<script src="/resources/testharness.js">` is the first line
    // of every web-platform-test - and there is no server here. Without this
    // the registry reads `/resources/testharness.js` off the root of the disk,
    // misses, and the page loads with no harness at all: every test in the
    // suite reports the same harness error and none of them is about the
    // engine. Set it and a leading `/` is resolved from that directory, which
    // is exactly the document root a server would have been serving.
    //
    // NOT a general URL feature and deliberately not in the URL parser: it is a
    // property of where the bytes come from, the same question base_path
    // answers for a relative name. tools/wpt/run-wpt.py sets it through
    // CTBROWSER_DOC_ROOT; nothing else in the tree sets it at all.
    void set_document_root(std::filesystem::path root) { document_root_ = std::move(root); }
    [[nodiscard]] const std::filesystem::path & document_root() const { return document_root_; }

    // SEALED: the registry and data: URLs only, never the filesystem.
    //
    // What a PACKAGED application is. Its resources travel inside it, so a name
    // that misses is a packaging bug - and the probe order above would answer
    // it from the WORKING DIRECTORY, which is wherever the user happened to be
    // standing. That is not a fallback, it is serving a stranger's bytes under
    // the name this document asked for, and it turns a broken package into
    // something that works on the machine that built it.
    //
    // A miss on a sealed registry is a miss. That is the point: it fails where
    // it is wrong rather than somewhere else later.
    void set_sealed(bool sealed) { sealed_ = sealed; }
    [[nodiscard]] bool sealed() const noexcept { return sealed_; }

    // The registry, then a data: URL, then the filesystem. Empty when none of
    // them has it.
    // WHAT THIS PAGE ASKED FOR, in the order it asked, each name once and
    // spelled exactly as the document spelled it.
    //
    // A PACKAGER CANNOT WORK THIS OUT FOR ITSELF, and the whole reason this
    // exists is that it must not try. The lookup key is the LITERAL reference
    // string - p5-basic.html says `../../vendor/p5/p5.js`, which is neither a
    // path relative to the application directory nor anything a second
    // implementation could re-derive without reproducing the engine's own
    // resolution rules. `browser::script_sources()` exists for the same reason
    // and says so: a copy of that rule in a packaging tool is a copy free to
    // drift from the one that matters.
    //
    // Recorded on LOAD, not on find(), so it captures the request whether the
    // bytes came from the registry, a data: URL, the filesystem or nowhere -
    // and a name recorded here that resolved to nothing is exactly what a
    // packager most needs to hear about.
    [[nodiscard]] const std::vector<std::string> & requested() const noexcept { return requested_; }

    [[nodiscard]] std::vector<std::byte> load(std::string_view name) const {
        if (!name.empty()) {
            bool seen = false;
            for (const std::string & already : requested_) {
                if (already == name) {
                    seen = true;
                    break;
                }
            }
            if (!seen) { requested_.emplace_back(name); }
        }
        if (const std::span<const std::byte> baked = find(name); !baked.empty()) {
            return {baked.begin(), baked.end()};
        }
        if (name.empty()) { return {}; }
        // A data: URL CARRIES ITS OWN BYTES, so it resolves here and reaches no
        // socket and no disk. Doing it in the registry rather than at each
        // caller is what makes an <img src="data:...">, a fetch, a CSS url()
        // and a <script src> all work from one change - and the registry is
        // consulted first, so a page may still override one by name.
        if (is_data_url(name)) {
            data_url inline_bytes;
            if (parse_data_url(name, inline_bytes)) { return std::move(inline_bytes.bytes); }
            return {};
        }
        if (sealed_) { return {}; }
        const std::filesystem::path relative{name};
        if (relative.is_absolute()) {
            // The document root FIRST when there is one, and the filesystem
            // still after it: a checkout is not a chroot, and an application
            // that hands the engine a genuine absolute path must keep working
            // whether or not something else set a root.
            if (!document_root_.empty()) {
                std::vector<std::byte> served =
                    read_file(document_root_ / relative.relative_path());
                if (!served.empty()) { return served; }
            }
            return read_file(relative);
        }
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
    // `load` is const because every caller has a const registry and asking for
    // a resource is not a change to the page. Recording the question is
    // bookkeeping about this registry rather than about its contents, which is
    // what mutable is for.
    mutable std::vector<std::string> requested_;
    bool sealed_ = false;

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
    std::filesystem::path document_root_;
};

} // namespace ctbrowser::shell
