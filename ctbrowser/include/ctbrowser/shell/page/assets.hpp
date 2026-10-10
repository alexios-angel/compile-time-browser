//===- assets.hpp - Asset registry ------------------------------*- C++ -*-===//
//
// Everything a page loads by name (sprites, sounds, JSON, whatever `fetch`
// asks for) comes through asset_registry.
//
// The registry is consulted before the filesystem: an application that ships
// its assets inside the binary keeps working when it is run from another
// directory, and a test that seeds the registry is hermetic no matter what is
// on disk. Falling back to the filesystem lets `ctbrowse page.html` show a
// page's images without the caller registering anything.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/shell/net/policy.hpp>
#include <ctbrowser/shell/net/url.hpp>

namespace ctbrowser::shell {

/// Name-to-bytes resources for a page, backed by the filesystem; see the file
/// comment for the lookup order.
class asset_registry {
public:
    /// Registers \p bytes under \p name. Seeded from app_options::assets and
    /// by whatever the page registers later. A second add() under the same name
    /// replaces the first, so a caller can override a built-in.
    void add(std::string name, std::vector<std::byte> bytes) {
        for (auto & [existing, data] : entries_) {
            if (existing == name) {
                data = std::move(bytes);
                return;
            }
        }
        entries_.emplace_back(std::move(name), std::move(bytes));
    }

    /// The registered bytes for exactly \p name, or empty. Registry only, no
    /// filesystem: `fetch` uses this to decide whether a URL was baked in
    /// before it considers opening a socket.
    [[nodiscard]] std::span<const std::byte> find(std::string_view name) const {
        for (const auto & [existing, data] : entries_) {
            if (existing == name) { return data; }
        }
        return {};
    }
    [[nodiscard]] bool contains(std::string_view name) const { return !find(name).empty(); }

    /// Where a relative path is resolved from when the registry misses.
    /// Confined (the default): the working directory, then this base, and only
    /// inside a root. Unconfined: the working directory, this base, then two
    /// levels above it, which lets `assets/sprites.bmp` resolve from both a
    /// source checkout and a build directory beside it.
    void set_base_path(std::filesystem::path base) { base_ = std::move(base); }

    /// Where a leading `/` is resolved from: the directory a server would have
    /// served as its document root. Empty by default; then a leading `/` names
    /// a path from the filesystem root when unconfined, and is taken relative
    /// to the base path when confined.
    ///
    /// A page written for a server uses server-absolute paths for its shared
    /// resources (`<script src="/resources/testharness.js">` is the first line
    /// of every web-platform-test), and there is no server here.
    ///
    /// Deliberately not in the URL parser: it is a property of where the bytes
    /// come from, the same question the base path answers for a relative name.
    /// tools/wpt/run-wpt.py sets it through CTBROWSER_DOC_ROOT; nothing else in
    /// the tree sets it.
    void set_document_root(std::filesystem::path root) { document_root_ = std::move(root); }
    [[nodiscard]] const std::filesystem::path & document_root() const { return document_root_; }

    /// Sealed: the registry and data: URLs only, never the filesystem.
    ///
    /// For a packaged application, whose resources travel inside it, so a name
    /// that misses is a packaging bug. The filesystem probe would answer it
    /// from the working directory, wherever the user happens to be, serving
    /// unrelated bytes and making a broken package work on the machine that
    /// built it. Sealed, a miss stays a miss.
    void set_sealed(bool sealed) { sealed_ = sealed; }
    [[nodiscard]] bool sealed() const noexcept { return sealed_; }

    /// Confined (the default): the filesystem answers only from inside a root:
    /// the base path (the working directory when none was set), the document
    /// root, or one added with add_root(). A page decides what names it asks
    /// for, so without this any page reads any file the process can:
    /// `fetch('/etc/passwd')`, `fetch('../../.ssh/id_rsa')`, `file:///...`.
    /// Confined, a relative name is resolved from the working directory and
    /// then the base path; an absolute one is either already inside a root or
    /// is taken as relative to the document root (else the base path); and
    /// whatever it resolves to, `..` and symlinks included, must still be
    /// inside a root. A `file:` URL is honoured only from a document that is
    /// itself a file: one.
    ///
    /// Unconfined, the filesystem is probed in the working directory, the base
    /// path and two levels above it, and any absolute path is read as-is. Use
    /// it only for trusted local content (the in-tree examples and fixtures
    /// that reach `../../vendor/`); never for a page from the network.
    void set_confine(bool confine) { confine_ = confine; }
    [[nodiscard]] bool confine() const noexcept { return confine_; }
    /// Adds another directory a confined registry may read from, for files
    /// the embedder names rather than the page: the font directory
    /// (browser::use_real_fonts reads CTBROWSER_FONT_PATH through here).
    void add_root(std::filesystem::path root) { extra_roots_.push_back(std::move(root)); }

    /// What fetch() and XHR may reach over the network. Kept here rather than
    /// on the bindings because the registry outlives a page's reloads and
    /// every frame's bindings already reach it. See shell/net/policy.hpp.
    void set_fetch_policy(fetch_policy policy) { policy_ = std::move(policy); }
    [[nodiscard]] const fetch_policy & network_policy() const noexcept { return policy_; }

    /// \p name under \p root, canonical, if it is still inside \p root once
    /// `..` and symlinks are resolved; else nothing. An absolute \p name is
    /// taken relative to \p root.
    [[nodiscard]] static std::optional<std::filesystem::path> contained(
        const std::filesystem::path & root, const std::filesystem::path & name) {
        return inside(root, root / name.relative_path());
    }

    /// Every name passed to load(), in first-request order, spelled exactly as
    /// the document wrote it.
    ///
    /// A packager uses this list instead of re-deriving the engine's resolution
    /// rules: the key is the literal reference (`../../vendor/p5/p5.js`), and a
    /// name that resolved to nothing is recorded too, because that is the
    /// packaging bug worth reporting.
    [[nodiscard]] const std::vector<std::string> & requested() const noexcept { return requested_; }

    /// Returns the bytes for \p name: the registry (exact name, then without
    /// its query or fragment), then a data: URL, then the filesystem unless the
    /// registry is sealed. Empty when nothing has it. Records \p name in
    /// requested().
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
        // A registered name also matches without its query or fragment: a
        // form submission navigates a frame to `inner.html?a=1`, and the
        // registry holds `inner.html`. The exact lookup above stays first
        // because a packager records the literal reference.
        if (const std::size_t cut = std::min(name.find('?'), name.find('#'));
            cut != std::string_view::npos && !is_data_url(name)) {
            if (const std::span<const std::byte> baked = find(name.substr(0, cut));
                !baked.empty()) {
                return {baked.begin(), baked.end()};
            }
        }
        // A data: URL carries its own bytes, so it resolves here and reaches
        // no socket and no disk. Doing it in the registry rather than at each
        // caller covers <img src="data:...">, fetch, CSS url() and
        // <script src> at once; the registry is consulted first, so a page may
        // still override one by name.
        if (is_data_url(name)) {
            data_url inline_bytes;
            if (parse_data_url(name, inline_bytes)) { return std::move(inline_bytes.bytes); }
            return {};
        }
        if (sealed_) { return {}; }
        if (confine_) {
            return load_confined(
                std::filesystem::path{name.substr(0, std::min(name.find('?'), name.find('#')))});
        }
        // A file has no query and no fragment: `support/x.js?pipe=trickle(d1)`
        // names x.js in every browser once the URL is a file: one (the query
        // is the server's business), so the filesystem probe drops both.
        name = name.substr(0, std::min(name.find('?'), name.find('#')));
        const std::filesystem::path relative{name};
        if (relative.is_absolute()) {
            // The document root first when there is one, and the filesystem
            // after it: a checkout is not a chroot, and an application that
            // hands the engine a genuine absolute path must keep working
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

    /// Reads a `file:` URL as the path it names: the host (empty, or
    /// `localhost`) dropped, the query and fragment cut, the path
    /// percent-decoded. Confined, only for a document that is itself at a
    /// file: URL (an http page has no business reading the disk), and only
    /// inside a root.
    [[nodiscard]] std::vector<std::byte> load_file_url(std::string_view url,
                                                       std::string_view document_href) const {
        if (!url.starts_with("file:")) { return {}; }
        if (confine_ && !document_href.starts_with("file:")) { return {}; }
        std::string_view path = url.substr(5);
        if (path.starts_with("//")) {
            path.remove_prefix(2);
            path = path.substr(std::min(path.find('/'), path.size()));
        }
        path = path.substr(0, std::min(path.find('?'), path.find('#')));
        return load(percent_decode(path));
    }

    /// The number of registered entries.
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    // `load` is const because every caller has a const registry and asking for
    // a resource is not a change to the page. Recording the question is
    // bookkeeping about this registry rather than about its contents, which is
    // what mutable is for.
    mutable std::vector<std::string> requested_;
    bool sealed_ = false;
    bool confine_ = true;
    fetch_policy policy_;

    // `candidate`, canonical, when it lies under `root` (compared component by
    // component, so /a/bc is not under /a/b).
    [[nodiscard]] static std::optional<std::filesystem::path> inside(
        const std::filesystem::path & root, const std::filesystem::path & candidate) {
        const auto canonical = [](const std::filesystem::path & path) {
            std::error_code failed;
            std::filesystem::path out = std::filesystem::absolute(path, failed);
            if (!failed) { out = std::filesystem::weakly_canonical(out, failed); }
            return failed || out.empty() ? std::optional<std::filesystem::path>{}
                                         : std::optional<std::filesystem::path>{std::move(out)};
        };
        std::optional<std::filesystem::path> base = canonical(root);
        const std::optional<std::filesystem::path> full = canonical(candidate);
        if (!base || !full) { return std::nullopt; }
        if (base->filename().empty()) { *base = base->parent_path(); } // A trailing separator.
        if (std::mismatch(base->begin(), base->end(), full->begin(), full->end()).first !=
            base->end()) {
            return std::nullopt;
        }
        return full;
    }

    [[nodiscard]] std::vector<std::byte> load_confined(const std::filesystem::path & name) const {
        if (name.empty()) { return {}; }
        const std::filesystem::path base = base_.empty() ? std::filesystem::path{"."} : base_;
        std::vector<std::filesystem::path> roots{base};
        if (!document_root_.empty()) { roots.push_back(document_root_); }
        roots.insert(roots.end(), extra_roots_.begin(), extra_roots_.end());
        std::vector<std::filesystem::path> candidates;
        if (name.has_root_path()) {
            // Already a path inside a root (a frame's subresource, a font), or
            // server-absolute: from the document root, else the base path.
            candidates = {name,
                          (document_root_.empty() ? base : document_root_) / name.relative_path()};
        } else {
            // The working directory first, as the unconfined probe order has
            // it, but only where that lands inside a root (usually the font
            // directory), never as a root of its own.
            candidates = {name, base / name};
        }
        for (const std::filesystem::path & candidate : candidates) {
            for (const std::filesystem::path & root : roots) {
                if (const std::optional<std::filesystem::path> path = inside(root, candidate)) {
                    std::vector<std::byte> bytes = read_file(*path);
                    if (!bytes.empty()) { return bytes; }
                    break;
                }
            }
        }
        return {};
    }

    [[nodiscard]] static std::vector<std::byte> read_file(const std::filesystem::path & path) {
        std::error_code failed;
        if (!std::filesystem::is_regular_file(path, failed)) { return {}; }
        std::ifstream in{path, std::ios::binary | std::ios::ate};
        if (!in) { return {}; }
        // Size the opened file, so a renamed path cannot supply an error
        // sentinel or the size of a different file to the allocation.
        const std::streamoff size = in.tellg();
        if (size <= 0 || !std::in_range<std::size_t>(size) ||
            !std::in_range<std::streamsize>(size)) {
            return {};
        }
        std::vector<std::byte> bytes;
        if (static_cast<std::size_t>(size) > bytes.max_size()) { return {}; }
        in.seekg(0);
        if (!in) { return {}; }
        bytes.resize(static_cast<std::size_t>(size));
        if (!in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size))) {
            return {};
        }
        return bytes;
    }

    std::vector<std::pair<std::string, std::vector<std::byte>>> entries_;
    std::filesystem::path base_;
    std::filesystem::path document_root_;
    std::vector<std::filesystem::path> extra_roots_;
};

} // namespace ctbrowser::shell
