// browser - default actions: activating a control, <details>, following a
// link, <a download>, the fragment scroll and form submission.
//
// One of ten files carved out of a 2,871-line Shell/browser.cpp on 2026-09-08.
// All are member functions of one class declared in
// include/ctbrowser/shell/browser.hpp; internal.hpp beside this carries the
// includes browser.cpp had, so every file sees exactly what it saw. Nothing
// about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

void browser::activate(node_id target) {
    // A DISABLED control does nothing and dispatches nothing - it does not
    // toggle, submit, focus or fire an event. Without this the attribute
    // was purely decorative, and it was not even that.
    if (is_disabled(control_ancestor(target))) { return; }
    if (follow_link(target)) { return; }
    if (toggle_details(target)) { return; }
    const node_id control = control_ancestor(target);
    if (!control) { return; }
    const auto txn = doc_->read();
    const control_kind kind = kind_of(txn, control);
    if (kind == control_kind::checkbox || kind == control_kind::radio) {
        forms_.toggle(txn, atoms_, control, kind);
        bindings_->dispatch("change", control);
        mark(dirty::paint);
        return;
    }
    if (kind == control_kind::select) {
        // Toggle: clicking an open select closes it again.
        select_open_ = select_open_ == control ? node_id{} : control;
        mark(dirty::paint);
        return;
    }
    if (kind != control_kind::button) { return; }
    const std::string_view type = txn.attribute_value(control, atoms_.intern("type"));
    const node_id form = form_store::owning_form(txn, atoms_, control);
    if (type == "reset") {
        forms_.reset_form(txn, form);
        mark(dirty::paint);
        return;
    }
    // A <button> with no type is a submit button, which is the default
    // people forget and then wonder why their form reloads.
    if (type.empty() || type == "submit") { submit(form); }
}

bool browser::toggle_details(node_id target) {
    node_id summary;
    node_id details;
    {
        const auto txn = doc_->read();
        const atom summary_tag = atoms_.intern_lower("summary");
        for (node_id at = target; at; at = txn.parent(at)) {
            if (txn.tag(at).value_or(atom{}) == summary_tag) {
                summary = at;
                details = txn.parent(at);
                break;
            }
        }
    }
    if (!summary || !details) { return false; }
    {
        const atom open = atoms_.intern("open");
        const bool was_open = doc_->read().has_attribute(details, open);
        if (was_open) {
            (void)doc_->remove_attribute(details, open);
        } else {
            (void)doc_->set_attribute(details, open, "");
        }
    }
    bindings_->dispatch("toggle", details);
    mark(dirty::everything);
    return true;
}

bool browser::follow_link(node_id target) {
    std::string href;
    std::string download;
    bool has_download = false;
    {
        const auto txn = doc_->read();
        const atom anchor = atoms_.intern_lower("a");
        const atom attribute = atoms_.intern("href");
        const atom download_attribute = atoms_.intern("download");
        for (node_id at = target; at; at = txn.parent(at)) {
            if (txn.tag(at).value_or(atom{}) != anchor) { continue; }
            href = txn.attribute_value(at, attribute);
            has_download = txn.has_attribute(at, download_attribute);
            download = txn.attribute_value(at, download_attribute);
            break;
        }
    }
    if (has_download && save_download(href, download)) { return true; }
    if (href.empty()) { return false; }
    location_href_ = href;
    if (href.front() == '#') {
        // A FRAGMENT is not a navigation: it scrolls this document, and the
        // page can read where it went through location.hash.
        location_hash_ = href;
        scroll_to_fragment(href.substr(1));
        bindings_->observe_location(location_href_, location_hash_);
        return true;
    }
    location_hash_.clear();
    bindings_->observe_location(location_href_, location_hash_);
    if (navigate_hook_) { navigate_hook_(href); }
    return true;
}

// AN `<a download>` WRITES A FILE. THIS IS A DELIBERATE DEVIATION, and it is the
// one place this engine invents a behaviour rather than copying one.
//
// A browser would show a save dialog. There is nobody to show one to here, and
// the alternative - doing nothing - makes every export silently fail, which is
// exactly the failure this codebase keeps finding and refusing to ship. p5's
// save(), saveCanvas(), saveJSON(), saveStrings() and saveTable() all end in
// downloadFile: a Blob, an object URL, an <a href download>, click(), revoke.
// So the choice is between a file appearing and the whole export API being a
// no-op with no message.
//
// The bytes come from the asset registry, which is where createObjectURL put
// them - so this needs no knowledge of blobs and works for any href the registry
// resolves. It is called BEFORE the navigation, because a download is not one.
//
// The directory is set by the embedder (set_download_directory). The default is
// the process's working directory, the same place a command-line tool writes.
// Every download is recorded in downloads() whether or not the write succeeds,
// so a test can assert on it without reading the disk and a page's export is
// visible even in a sandbox.
bool browser::save_download(const std::string & href, const std::string & suggested) {
    if (href.empty()) { return false; }
    std::vector<std::byte> bytes = assets_.load(href);
    // A name the page asked for, else the last path segment, else something.
    std::string name = suggested;
    if (name.empty()) {
        const std::size_t slash = href.find_last_of("/\\");
        name = slash == std::string::npos ? href : href.substr(slash + 1);
    }
    // A NAME AND NOT A PATH. `download="../../etc/passwd"` is a page choosing
    // where to write on the host, which it does not get to do.
    for (char & c : name) {
        if (c == '/' || c == '\\' || c == ':') { c = '_'; }
    }
    if (name.empty() || name == "." || name == "..") { name = "download"; }

    const std::filesystem::path where =
        (download_directory_.empty() ? std::filesystem::path{"."} : download_directory_) / name;
    bool written = false;
    if (!bytes.empty()) {
        std::error_code ignored;
        std::filesystem::create_directories(where.parent_path(), ignored);
        std::ofstream out{where, std::ios::binary};
        if (out) {
            out.write(reinterpret_cast<const char *>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            written = out.good();
        }
    }
    downloads_.push_back(download_record{name, where.string(), bytes.size(), written});
    if (download_hook_) { download_hook_(downloads_.back()); }
    return true;
}

void browser::scroll_to_fragment(std::string_view id) {
    if (id.empty()) { return; }
    node_id target;
    {
        const auto txn = doc_->read();
        target = node_by_id(txn, id);
    }
    if (!target) { return; }
    // Fragment bounds are relative to the containing block, so finding the
    // element is not enough - the walk has to accumulate to get an absolute
    // y, which is what a scroll offset is measured in.
    bool found = false;
    float top = 0;
    const auto walk = [&](auto && self, const ctbrowser::layout::fragment & f, float dx,
                          float dy) -> void {
        if (found) { return; }
        const rect box = f.absolute_bounds(dx, dy);
        if (f.source == target) {
            found = true;
            top = box.y;
            return;
        }
        for (const auto & child : f.children) { self(self, child, box.x, box.y); }
    };
    walk(walk, fragments_, 0, 0);
    if (found) { scroll_to(top); }
}

void browser::submit(node_id form) {
    if (!form) { return; }
    if (bindings_->dispatch("submit", form)) { return; } // cancelled
    const auto txn = doc_->read();
    last_submission_ = forms_.form_data(txn, atoms_, form);
}

} // namespace ctbrowser::shell
