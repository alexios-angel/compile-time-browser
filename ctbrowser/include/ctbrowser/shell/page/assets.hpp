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
    // probe order: the working directory, then here, then two levels up -
    // which is what makes `assets/sprites.bmp` resolve both from a source
    // checkout and from a build directory beside it.
    void set_base_path(std::filesystem::path base) { base_ = std::move(base); }

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
    // property of where the bytes come from, the same question the base path
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

    // CONFINED (the default): the filesystem answers only from inside the
    // base path - the working directory when none was set - or the document
    // root. A page decides what names it asks for, so without this any page
    // reads any file the process can: `fetch('/etc/passwd')`,
    // `fetch('../../.ssh/id_rsa')`, `file:///...`. Confined, a relative name is
    // resolved from the working directory and then the base path, an absolute
    // one is either already inside a
    // root or is taken as relative to the document root (else the base path),
    // and whatever it resolves to - `..` and symlinks included - must still be
    // inside a root. A `file:` URL is honoured only from a document that is
    // itself a file: one.
    //
    // UNCONFINED is the old probe order - the working directory, the base
    // path, two levels above it, and any absolute path at all. It is for
    // trusted local content only (the in-tree examples and fixtures that reach
    // `../../vendor/`); never turn it off for a page from the network.
    void set_confine(bool confine) { confine_ = confine; }
    [[nodiscard]] bool confine() const noexcept { return confine_; }
    // ANOTHER DIRECTORY a confined registry may read an absolute path from -
    // for what the EMBEDDER names rather than the page: the font directory
    // (browser::use_real_fonts reads CTBROWSER_FONT_PATH through here).
    void add_root(std::filesystem::path root) { extra_roots_.push_back(std::move(root)); }

    // What fetch()/XHR may reach over the network. Kept here rather than on
    // the bindings because the registry outlives a page's reloads and every
    // frame's bindings already reach it. See shell/net/policy.hpp.
    void set_fetch_policy(fetch_policy policy) { policy_ = std::move(policy); }
    [[nodiscard]] const fetch_policy & network_policy() const noexcept { return policy_; }

    // NAME under ROOT, canonical, if it is still inside ROOT once `..` and
    // symlinks are resolved - else nothing. An absolute NAME is taken relative
    // to ROOT.
    [[nodiscard]] static std::optional<std::filesystem::path> contained(
        const std::filesystem::path & root, const std::filesystem::path & name) {
        return inside(root, root / name.relative_path());
    }

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
        // A REGISTERED name matched without its query or fragment too: a
        // form submission navigates a frame to `inner.html?a=1`, and the
        // registry holds `inner.html` (the filesystem probe below drops both
        // already; a packager records the literal reference, so the exact
        // lookup above stays first).
        if (const std::size_t cut = std::min(name.find('?'), name.find('#'));
            cut != std::string_view::npos && !is_data_url(name)) {
            if (const std::span<const std::byte> baked = find(name.substr(0, cut));
                !baked.empty()) {
                return {baked.begin(), baked.end()};
            }
        }
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
        if (confine_) {
            return load_confined(
                std::filesystem::path{name.substr(0, std::min(name.find('?'), name.find('#')))});
        }
        // A FILE HAS NO QUERY AND NO FRAGMENT. `support/x.js?pipe=trickle(d1)`
        // names x.js in every browser once the URL is a file: one - the query
        // is the server's business and there is no server here - so the
        // filesystem probe drops both. The registry lookup above keeps the
        // literal reference, which is what a packager recorded.
        name = name.substr(0, std::min(name.find('?'), name.find('#')));
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

    // A `file:` URL, read as the path it names: the host (empty, or
    // `localhost`) dropped, the query and fragment cut, the path
    // percent-decoded. Confined, only for a document at a file: URL itself -
    // an http page has no business reading the disk - and only inside a root.
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

    // CANDIDATE, canonical, when it lies under ROOT (compared component by
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
        if (base->filename().empty()) { *base = base->parent_path(); } // a trailing separator
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
            // The working directory first, as the unconfined probe order has it
            // - but only where that lands inside a root (the font directory,
            // usually), never as a root of its own.
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
