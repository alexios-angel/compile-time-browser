// browser - default actions: activating a control, <details>, following a
// link, <a download>, the fragment scroll and form submission.

#include "internal.hpp"

namespace ctbrowser::shell {

namespace {

// HTML 3.2.5.2.7 "interactive content" - what a <label> leaves alone: a click
// aimed at one, or at anything inside one, is that element's own.
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

// THE ACTIVATION BEHAVIOUR of one element, run by the bindings' dispatch after
// a `click`'s listeners and only if none of them cancelled it - `target` IS the
// activation target, the first node on the event's path that has any, so this
// dispatches on its tag rather than walking up from wherever the click landed.
// A checkbox or radio was already toggled before the listeners ran and has
// already fired `input` and `change`; all it needs here is the repaint.
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
            // its click's activation target - and must not then click the
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
    // A <label>'s activation behaviour is a click at the control it labels -
    // HTML 4.10.4 - and that click has an activation behaviour of its own,
    // which is how the label's text toggles the checkbox. A disabled control
    // refuses it, in dom_bindings::click, as it refuses `element.click()`.
    //
    // ONCE: a control with no activation behaviour of its own - a <textarea> -
    // leaves its click's activation target to the label it sits in, so the
    // click forwarded to it came straight back here and was forwarded again,
    // for ever; `<label>Notes <textarea>` hung the page on the first click.
    // That second click is aimed at the label's interactive content, for
    // which the activation behaviour is to do nothing - which is what the
    // DOM's "click in progress" flag makes of it. ponytail: a thread-local
    // stack for the flag, since only this one synthesized click can re-enter;
    // a per-element flag in dom_bindings::click is the general form.
    if (tag == "label") {
        static thread_local std::vector<node_id> forwarding;
        if (labelled && std::ranges::find(forwarding, target) == forwarding.end()) {
            forwarding.push_back(target);
            (void)bindings_->click(labelled);
            forwarding.pop_back();
        }
        return;
    }
    // A DISABLED control does nothing and dispatches nothing - it does not
    // toggle, submit, focus or fire an event. Without this the attribute
    // was purely decorative, and it was not even that.
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
        // THE PICKER TYPES' input activation behaviour (HTML 4.10.5.1): "show
        // the picker, if applicable" - with transient activation, and on a
        // mutable control, it CONSUMES the activation first (there is no
        // picker here to show; showPicker() is the same steps).
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
        // The kind is read NOW, after the click's listeners: one that made a
        // text field `type=submit` made a submit button, and this click is its.
        // `<input type=image>` is one, with a picture on it (4.10.5.1.20),
        // though control_kind_of files it with the text controls because that
        // is how it is laid out; so is a <button> with no type, which is the
        // default people forget and then wonder why their form reloads. Its
        // FORM OWNER, `form=` and all; with none, a button does nothing.
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
    // The toggle event is the attribute change steps', a task later.
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
    // THE ADDRESS IS RESOLVED against the document's when it has one - a page
    // at file:///a/b.html following `#x` is at file:///a/b.html#x, not at
    // `#x`. The embedder's hook still gets the author's text.
    const std::string old_href = location_href_;
    location_href_ = location_href_.empty() ? href : resolve(location_href_, href);
    if (href.front() == '#') {
        // A FRAGMENT is not a navigation: it scrolls this document, and the
        // page can read where it went through location.hash - and hears
        // `hashchange` at the window (HTML 7.4.6.2, "scroll to the fragment").
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
    // The hook gets the RESOLVED address - what an external opener must judge
    // by - the author's text, and whether a user gesture is behind it. Following
    // the link CONSUMES that gesture, so one click cannot open two things.
    const bool activated = bindings_->has_transient_activation();
    if (activated) { bindings_->consume_activation(); }
    if (navigate_hook_) { navigate_hook_(location_href_, href, activated); }
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
    return true;
}

void browser::scroll_to_fragment(std::string_view id) {
    if (id.empty()) { return; }
    const node_id target = bindings_->find_by_id(std::string{id});
    if (!target) { return; }
    // Fragment bounds are relative to the containing block, so finding the
    // element is not enough - the walk has to accumulate to get an absolute
    // y, which is what a scroll offset is measured in.
    if (const std::optional<rect> box = absolute_rect_of(fragments_, target)) { scroll_to(box->y); }
}

// "Submit" a form FROM A SUBMITTER (HTML 4.10.21.3), a submit button's
// activation behaviour: the bindings' algorithm - validation, the `submit`
// SubmitEvent carrying the button, the entry list with its `formdata`, the
// navigation - and, when it went ahead, the store's data kept for the embedder
// to read back.
//
// With NO submitter it is IMPLICIT SUBMISSION (4.10.21.2), Enter in a field: a
// click at the form's default button - the first submit button in tree order
// that it owns - which a disabled one refuses; and when it has none, and no
// more than one field that blocks implicit submission, the form submitted from
// itself.
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

// HTML 4.10.21.5 "reset the form", the bindings' as form.reset() is: a
// cancelable `reset` event at the form FIRST - so `onreset` runs whether or
// not the reset is then cancelled - and each resettable control it owns reset
// only when it was not, a <select>'s selectedness and an <output>'s text
// included.
void browser::reset(node_id form) {
    if (!form || !bindings_->reset_form_) { return; }
    bindings_->reset_form_(form);
    mark(dirty::paint);
}

} // namespace ctbrowser::shell
