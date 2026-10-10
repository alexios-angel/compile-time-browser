//===- actions.cpp - Default actions of clicks and forms --------*- C++ -*-===//
//
// The browser's default actions: activating a control, toggling <details>,
// following a link, <a download>, the fragment scroll, and form submission
// and reset.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

namespace {

/// True for HTML 3.2.5.2.7 "interactive content", which a <label> leaves alone:
/// a click aimed at one, or at anything inside one, is that element's own.
[[nodiscard]] bool is_interactive_content(const read_txn & txn, atom_table & atoms, node_id id) {
    if (txn.element_ns(id) != node_ns::html) { return false; }
    const std::string_view local = txn.local_name(id);
    const auto has = [&](std::string_view name) {
        return txn.has_attribute(id, atoms.intern(name));
    };
    if (local == "a") { return has("href"); }
    if (local == "audio" || local == "video") { return has("controls"); }
    if (local == "img") { return has("usemap"); }
    if (local == "input") {
        return !ascii_iequals(txn.attribute_value(id, atoms.intern("type")), "hidden");
    }
    return local == "button" || local == "details" || local == "embed" || local == "iframe" ||
           local == "label" || local == "select" || local == "textarea";
}

} // namespace

/// Runs the activation behaviour of one element. The bindings' dispatch calls
/// this after a `click`'s listeners, and only if none of them cancelled it.
/// `target` is the activation target (the first node on the event's path that
/// has an activation behaviour), so this dispatches on its tag rather than
/// walking up from wherever the click landed. A checkbox or radio was already
/// toggled before the listeners ran and has already fired `input` and
/// `change`; all it needs here is the repaint.
void browser::activate(node_id target) {
    std::string_view tag;
    node_id labelled;
    // The node the click was dispatched at, read before anything below
    // dispatches another.
    const node_id clicked = bindings_->activation_event_target;
    {
        const auto txn = doc_->read();
        tag = atoms_.text(txn.tag(target).value_or(atom{}));
        if (tag == "label") {
            // "...for events targeted at interactive content descendants of a
            // label element, and any descendants of those, must be to do
            // nothing" (4.10.4): a <textarea> or an <audio controls> in the
            // label has no activation behaviour of its own, so the label is
            // its click's activation target, and must not then click the
            // control it labels.
            bool interactive = false;
            node_id at = clicked;
            for (; at && at != target; at = txn.parent(at)) {
                interactive = interactive || is_interactive_content(txn, atoms_, at);
            }
            if (interactive && at == target) { return; }
        }
    }
    if (tag == "label") { labelled = bindings_->labeled_control(target); }
    if (tag == "a" || tag == "area") {
        (void)follow_link(target);
        return;
    }
    if (tag == "summary") {
        (void)toggle_details(target);
        return;
    }
    // A <label>'s activation behaviour is a click at the control it labels
    // (HTML 4.10.4), and that click has an activation behaviour of its own,
    // which is how the label's text toggles the checkbox. A disabled control
    // refuses it, in dom_bindings::click, as it refuses `element.click()`.
    //
    // Forward at most once per label. A control with no activation behaviour
    // of its own (a <textarea>) makes the enclosing label its click's
    // activation target, so a forwarded click would come straight back here
    // forever; the re-entrant click targets the label's interactive content,
    // for which HTML says to do nothing (the DOM's "click in progress" flag).
    // ponytail: a thread-local stack, since only this synthesized click
    // re-enters; a per-element flag in dom_bindings::click is the general form.
    if (tag == "label") {
        static thread_local std::vector<node_id> forwarding;
        if (labelled && std::ranges::find(forwarding, target) == forwarding.end()) {
            forwarding.push_back(target);
            (void)bindings_->click(labelled);
            forwarding.pop_back();
        }
        return;
    }
    // A disabled control does nothing and dispatches nothing: it does not
    // toggle, submit, focus or fire an event.
    if (is_disabled(target)) { return; }
    form_store::form_action action;
    {
        const auto txn = doc_->read();
        const control_kind kind = kind_of(txn, target);
        if (kind == control_kind::checkbox || kind == control_kind::radio) {
            mark(dirty::paint);
            return;
        }
        if (kind == control_kind::select) {
            // Toggle: clicking an open select closes it again.
            select_open_ = select_open_ == target ? node_id{} : target;
            mark(dirty::paint);
            return;
        }
        // The picker types' input activation behaviour (HTML 4.10.5.1): "show
        // the picker, if applicable". With transient activation, on a mutable
        // control, it consumes the activation (there is no picker here to
        // show; showPicker() runs the same steps).
        if (txn.local_name(target) == "input" && txn.element_ns(target) == node_ns::html) {
            const std::string type =
                input_types::type_state_of(txn.attribute_value(target, atoms_.intern("type")));
            if (ascii_iequals_any(
                    type, {"color", "date", "datetime-local", "file", "month", "time", "week"})) {
                const bool read_only = type != "color" && type != "file" &&
                                       txn.has_attribute(target, atoms_.intern("readonly"));
                if (!read_only && bindings_->has_transient_activation()) {
                    bindings_->consume_activation();
                }
                return;
            }
        }
        // The kind is read now, after the click's listeners: a listener that
        // made a text field `type=submit` made a submit button, and this click
        // submits. `<input type=image>` is a submit button with a picture on it
        // (4.10.5.1.20), though control_kind_of files it with the text controls
        // because that is how it is laid out; so is a <button> with no type.
        // The action goes to the control's form owner (honouring `form=`);
        // with none, a button does nothing.
        action = form_store::form_action_of(txn, atoms_, target);
    }
    if (!action.form) { return; }
    if (action.submits) { submit(action.form, target); }
    if (action.resets) { reset(action.form); }
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
    // The toggle event is fired by the attribute change steps, a task later.
    bindings_->mutated();
    mark(dirty::everything);
    return true;
}

void browser::set_location(std::string href) {
    const std::size_t hash = href.find('#');
    location_hash_ = hash == std::string::npos ? std::string{} : href.substr(hash);
    location_href_ = std::move(href);
    if (bindings_) { bindings_->observe_location(location_href_, location_hash_); }
}

bool browser::follow_link(node_id target) {
    std::string href;
    std::string download;
    bool has_download = false;
    {
        const auto txn = doc_->read();
        const atom anchor = atoms_.intern_lower("a");
        const atom area = atoms_.intern_lower("area");
        const atom attribute = atoms_.intern("href");
        const atom download_attribute = atoms_.intern("download");
        // An <area> is a hyperlink as an <a> is (HTML 4.6.1): an image map's
        // region follows its own href.
        for (node_id at = target; at; at = txn.parent(at)) {
            const atom tag = txn.tag(at).value_or(atom{});
            if (tag != anchor && tag != area) { continue; }
            href = txn.attribute_value(at, attribute);
            has_download = txn.has_attribute(at, download_attribute);
            download = txn.attribute_value(at, download_attribute);
            break;
        }
    }
    if (has_download && save_download(href, download)) { return true; }
    if (href.empty()) { return false; }
    // The address is resolved against the document's when it has one: a page
    // at file:///a/b.html following `#x` goes to file:///a/b.html#x, not to
    // `#x`. The embedder's hook still gets the author's text.
    const std::string old_href = location_href_;
    location_href_ = location_href_.empty() ? href : resolve(location_href_, href);
    if (href.front() == '#') {
        // A fragment is not a navigation: it scrolls this document, the page
        // can read where it went through location.hash, and `hashchange`
        // fires at the window (HTML 7.4.6.2, "scroll to the fragment").
        location_hash_ = href;
        scroll_to_fragment(href.substr(1));
        bindings_->observe_location(location_href_, location_hash_);
        if (old_href != location_href_) {
            (void)bindings_->dispatch_hash_change(old_href, location_href_);
        }
        return true;
    }
    location_hash_.clear();
    bindings_->observe_location(location_href_, location_hash_);
    // The hook gets the resolved address (what an external opener must judge
    // by), the author's text, and whether a user gesture is behind it.
    // Following the link consumes that gesture, so one click cannot open two
    // things.
    const bool activated = bindings_->has_transient_activation();
    if (activated) { bindings_->consume_activation(); }
    if (navigate_hook_) { navigate_hook_(location_href_, href, activated); }
    return true;
}

/// Saves the target of an `<a download>` click as a file. This deliberately
/// deviates from browsers, the one place this engine invents a behaviour
/// rather than copying one.
///
/// A browser would show a save dialog. There is nobody to show one to here,
/// and doing nothing would make every export silently fail: p5's save(),
/// saveCanvas(), saveJSON(), saveStrings() and saveTable() all end in
/// downloadFile (a Blob, an object URL, an <a href download>, click(),
/// revoke).
///
/// The bytes come from the asset registry, which is where createObjectURL put
/// them, so this needs no knowledge of blobs and works for any href the
/// registry resolves. It is called before the navigation, because a download
/// is not one.
///
/// The directory is set by the embedder (set_download_directory); unset means
/// no write, because the working directory of a shipped game is its own folder
/// and a page could plant `version.dll` beside the executable. A write also
/// needs a user gesture (the click consumes it, as a navigation does), fits
/// under the size limit, gets a sanitised name and never replaces a file.
/// Every download is recorded in downloads() whether or not it was written,
/// so tests can assert on it without reading the disk.
bool browser::save_download(const std::string & href, const std::string & suggested) {
    if (href.empty()) { return false; }
    const bool activated = bindings_->has_transient_activation();
    if (activated) { bindings_->consume_activation(); }
    std::vector<std::byte> bytes = assets_.load(href);
    // A name the page asked for, else the last path segment. It must be a name
    // and not a path: `download="../../etc/passwd"` is a page choosing where
    // to write.
    const std::string name = sanitize_download_name(suggested.empty() ? href : suggested);

    std::filesystem::path where;
    bool written = false;
    if (!download_directory_.empty() && activated && !bytes.empty() &&
        bytes.size() <= max_download_bytes_) {
        std::error_code ignored;
        std::filesystem::create_directories(download_directory_, ignored);
        // Never overwrite: try name, name-1, name-2, ... using symlink_status,
        // so a dangling link counts as occupied rather than written through.
        // ponytail: check-then-open races another writer; std::ios::noreplace
        // closes that once every supported standard library has it.
        const std::filesystem::path base{name};
        where = download_directory_ / base;
        for (int n = 1;
             n <= 1000 && std::filesystem::exists(std::filesystem::symlink_status(where, ignored));
             ++n) {
            where = download_directory_ /
                    (base.stem().string() + "-" + std::to_string(n) + base.extension().string());
        }
        if (!std::filesystem::exists(std::filesystem::symlink_status(where, ignored))) {
            std::ofstream out{where, std::ios::binary};
            if (out) {
                out.write(reinterpret_cast<const char *>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()));
                written = out.good();
            }
        }
    }
    downloads_.push_back(download_record{name, where.string(), bytes.size(), written});
    return true;
}

/// Reduces a page-chosen download name to a safe file name: the last path
/// segment, with "download" returned instead for control or reserved
/// characters, Windows device names, desktop.ini, or an executable extension.
std::string sanitize_download_name(std::string_view name) {
    const std::string fallback = "download";
    if (const std::size_t slash = name.find_last_of("/\\"); slash != std::string_view::npos) {
        name.remove_prefix(slash + 1);
    }
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F || std::string_view{"<>:\"|?*"}.contains(c)) { return fallback; }
    }
    // Windows drops trailing dots and spaces, so `evil.dll.` is evil.dll.
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) { name.remove_suffix(1); }
    if (name.empty()) { return fallback; }
    // A device name is one whatever follows its first dot: CON.txt opens CON.
    std::string_view device = name.substr(0, name.find('.'));
    while (!device.empty() && device.back() == ' ') { device.remove_suffix(1); }
    if (ascii_iequals_any(device, {"con", "prn", "aux", "nul"}) ||
        (device.size() == 4 &&
         (ascii_iequals(device.substr(0, 3), "com") || ascii_iequals(device.substr(0, 3), "lpt")) &&
         device[3] >= '1' && device[3] <= '9')) {
        return fallback;
    }
    if (ascii_iequals(name, "desktop.ini")) { return fallback; }
    if (const std::size_t dot = name.rfind('.'); dot != std::string_view::npos) {
        if (ascii_iequals_any(name.substr(dot + 1),
                              {"exe", "dll", "com", "bat", "cmd", "ps1", "scr", "msi", "lnk", "url",
                               "vbs", "js", "hta", "cpl", "sys", "so", "dylib", "sh"})) {
            return fallback;
        }
    }
    return std::string{name};
}

void browser::scroll_to_fragment(std::string_view id) {
    if (id.empty()) { return; }
    const node_id target = bindings_->find_by_id(std::string{id});
    if (!target) { return; }
    // Fragment bounds are relative to the containing block, and a scroll
    // offset is absolute, so absolute_rect_of accumulates the offsets.
    if (const std::optional<rect> box = absolute_rect_of(fragments_, target)) { scroll_to(box->y); }
}

/// Submits `form` from `submitter` (HTML 4.10.21.3), a submit button's
/// activation behaviour. It runs the bindings' algorithm (validation, the
/// `submit` SubmitEvent carrying the button, the entry list with its
/// `formdata`, the navigation) and, when the submission went ahead, keeps the
/// form data for the embedder to read back.
///
/// With no submitter this is implicit submission (4.10.21.2), Enter in a
/// field: a click at the form's default button (the first submit button in
/// tree order that it owns), which a disabled one refuses. When there is no
/// default button, and at most one field that blocks implicit submission, the
/// form is submitted from itself.
void browser::submit(node_id form, node_id submitter) {
    if (!form || !bindings_->submit_from_) { return; }
    if (!submitter) {
        node_id button;
        std::size_t blocking = 0;
        {
            const auto txn = doc_->read();
            const atom type_attribute = atoms_.intern("type");
            std::vector<node_id> pending{txn.root_of_tree(form)};
            while (!pending.empty() && !button) {
                const node_id at = pending.back();
                pending.pop_back();
                const std::span<const node_id> children = txn.children(at);
                for (std::size_t i = children.size(); i-- > 0;) { pending.push_back(children[i]); }
                const std::string_view tag = atoms_.text(txn.tag(at).value_or(atom{}));
                if ((tag != "input" && tag != "button") ||
                    form_store::owning_form(txn, atoms_, at) != form) {
                    continue;
                }
                if (form_store::is_submit_button(txn, atoms_, at)) {
                    button = at;
                } else if (tag == "input" &&
                           ascii_iequals_any(
                               input_types::type_state_of(txn.attribute_value(at, type_attribute)),
                               {"text", "search", "tel", "url", "email", "password", "date",
                                "month", "week", "time", "datetime-local", "number"})) {
                    ++blocking;
                }
            }
        }
        if (button) {
            (void)bindings_->click(button);
            return;
        }
        if (blocking > 1) { return; }
    }
    if (!bindings_->submit_from_(form, submitter)) { return; } // refused, invalid or cancelled
    last_submission_ = forms_.form_data(doc_->read(), atoms_, form);
    mark(dirty::layout);
}

/// HTML 4.10.21.5 "reset the form", delegated to the same bindings code as
/// form.reset(): fire a cancelable `reset` event at the form first (so
/// `onreset` runs even if the reset is cancelled), then, if not cancelled,
/// reset each control the form owns, including <select> selectedness and
/// <output> text.
void browser::reset(node_id form) {
    if (!form || !bindings_->reset_form_) { return; }
    bindings_->reset_form_(form);
    mark(dirty::paint);
}

} // namespace ctbrowser::shell
