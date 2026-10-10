//===- ctbrowser/lib/Shell/browser/focus.cpp - Focus and navigation -------===//
//
// The browser's focus handling: the editable field, labels, HTML's focusable
// areas and focusing steps, sequential (Tab) navigation, and the drag
// auto-scroll that tick() runs while a selection drag leaves a field.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

#include "../bindings/element/internal.hpp" // detail::parse_html_integer

#include <ctbrowser/layout/overflow.hpp>

#include <limits>

namespace ctbrowser::shell {

bool browser::has_editable_focus() {
    if (!focused_) { return false; }
    const auto txn = doc_->read();
    if (!txn.contains(focused_)) { return false; }
    const control_kind kind = kind_of(txn, focused_);
    return kind == control_kind::text || kind == control_kind::textarea;
}

control_state * browser::editable_focus() {
    if (!focused_) { return nullptr; }
    const auto txn = doc_->read();
    if (!txn.contains(focused_)) { return nullptr; }
    const control_kind kind = kind_of(txn, focused_);
    if (kind != control_kind::text && kind != control_kind::textarea) { return nullptr; }
    return &forms_.state_of(txn, atoms_, focused_);
}

control_kind browser::kind_of(const read_txn & txn, node_id id) {
    return control_kind_of(atoms_.text(txn.tag(id).value_or(atom{})),
                           txn.attribute_value(id, atoms_.intern("type")));
}

node_id browser::labelled_control(const read_txn & txn, node_id from) {
    if (!from) { return node_id{}; }
    const atom label_tag = atoms_.intern_lower("label");
    node_id label;
    for (node_id at = from; at; at = txn.parent(at)) {
        if (txn.tag(at).value_or(atom{}) == label_tag) {
            label = at;
            break;
        }
    }
    if (!label) { return node_id{}; }

    // `for` wins when it is present, even if it names nothing: an explicit
    // reference that does not resolve labels nothing, rather than quietly
    // falling back to whatever the label happens to contain.
    const std::string_view target = txn.attribute_value(label, atoms_.intern("for"));
    if (!target.empty()) {
        const node_id named = bindings_->find_by_id(std::string{target});
        return kind_of(txn, named) != control_kind::none ? named : node_id{};
    }

    // Otherwise the first labelable descendant, in document order. The label
    // itself cannot be the answer (a <label> is not a control), so the walk
    // skips it.
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (found) { return; }
        if (at != label && kind_of(txn, at) != control_kind::none) {
            found = at;
            return;
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, label);
    return found;
}

node_id browser::control_ancestor(node_id from) {
    if (!from) { return node_id{}; }
    const auto txn = doc_->read();
    for (node_id at = from; at; at = txn.parent(at)) {
        if (kind_of(txn, at) != control_kind::none) { return at; }
    }
    // Nothing up the tree is a control, so the click may still be on a label
    // for one. The label check comes second: a press on the control inside a
    // label finds it on the walk above and never reaches here, so a click
    // cannot resolve twice and toggle a checkbox back off.
    return labelled_control(txn, from);
}

bool browser::via_label(node_id from) {
    if (!from) { return false; }
    const auto txn = doc_->read();
    for (node_id at = from; at; at = txn.parent(at)) {
        if (kind_of(txn, at) != control_kind::none) { return false; }
    }
    return static_cast<bool>(labelled_control(txn, from));
}

/// Whether the mutation being announced moved the focused element, or an
/// ancestor of it, out of the tree and back. See
/// dom_bindings::moved_by_mutation.
bool browser::focus_was_moved() const {
    const std::span<const node_id> moved = bindings_->moved_by_mutation();
    if (moved.empty()) { return false; }
    const auto txn = doc_->read();
    for (node_id at = focused_; at; at = txn.parent(at)) {
        if (std::ranges::find(moved, at) != moved.end()) { return true; }
    }
    return false;
}

//===----------------------------------------------------------------------===//
// Focus, HTML 6.6
//===----------------------------------------------------------------------===//
//
// A focusable area (HTML 6.6.2) is an element with a tabindex value, or one
// the user agent makes focusable (a form control, a link, its details'
// summary, an editing host, media showing controls), that is not a shadow
// host delegating its focus, not actually disabled, not inert, and is being
// rendered. Nothing else holds focus. `focus()` on anything else runs "get the
// focusable area", and when that finds none it does nothing at all. That is
// how `body.focus()`, the first thing testdriver's send_keys does, leaves
// focus where it was instead of taking it.

std::optional<long long> browser::tabindex_value(const read_txn & txn, node_id id) {
    const attribute * found = txn.find_attribute(id, atoms_.intern("tabindex"));
    long long parsed = 0;
    if (found == nullptr || !detail::parse_html_integer(found->value, parsed)) {
        return std::nullopt;
    }
    return parsed;
}

bool browser::focusable_by_default(const read_txn & txn, node_id id) {
    if (kind_of(txn, id) != control_kind::none) { return true; }
    const std::string_view local = txn.local_name(id);
    const auto has = [&](std::string_view name) {
        return txn.has_attribute(id, atoms_.intern(name));
    };
    if (txn.element_ns(id) == node_ns::svg) { return local == "a" && has("href"); }
    if (txn.element_ns(id) != node_ns::html) { return false; }
    if (local == "a" || local == "area") { return has("href"); }
    if (local == "audio" || local == "video") { return has("controls"); }
    // A navigable container: its focus is its document's. Tab does not enter
    // frames here, so the frame itself is the stop.
    if (local == "iframe" || local == "frame") { return true; }
    if (local == "summary") {
        // The summary for its parent details: that details' first summary.
        const node_id details = txn.parent(id);
        if (!details || txn.element_ns(details) != node_ns::html ||
            txn.local_name(details) != "details") {
            return false;
        }
        for (const node_id child : txn.children(details)) {
            if (txn.element_ns(child) == node_ns::html && txn.local_name(child) == "summary") {
                return child == id;
            }
        }
        return false;
    }
    return is_editing_host(txn, id);
}

/// An editing host: contenteditable true or plaintext-only on an element
/// whose parent is not itself editable (the host, not what it holds).
bool browser::is_editing_host(const read_txn & txn, node_id id) {
    const atom editable = atoms_.intern("contenteditable");
    const auto state = [&](node_id at) {
        if (!txn.has_attribute(at, editable)) { return -1; } // inherits
        const std::string_view value = txn.attribute_value(at, editable);
        if (value.empty() || ascii_iequals(value, "true") ||
            ascii_iequals(value, "plaintext-only")) {
            return 1;
        }
        return ascii_iequals(value, "false") ? 0 : -1;
    };
    if (state(id) != 1) { return false; }
    for (node_id up = txn.parent(id); up; up = txn.parent(up)) {
        if (const int above = state(up); above != -1) { return above == 0; }
    }
    return true;
}

/// Whether `id` takes text from the keyboard (a text field, a textarea, an
/// editing host). Such an element matches :focus-visible however it got focus.
bool browser::supports_keyboard_input(const read_txn & txn, node_id id) {
    const control_kind kind = kind_of(txn, id);
    return kind == control_kind::text || kind == control_kind::textarea || is_editing_host(txn, id);
}

/// Whether `id` is a scrollable region, a focusable area of its own (HTML
/// 6.6.2): a scroll container whose content overflows it, so the keyboard can
/// scroll it with `scroller.focus()` and then the arrow keys (WPT
/// scrollend-event-for-user-scroll).
bool browser::is_scroller(node_id id) const {
    const ctbrowser::layout::fragment * f = fragments_.find(id);
    if (f == nullptr || f->box == nullptr || !f->box->scroll_container) { return false; }
    const rect area = ctbrowser::layout::scrolling_area_of(*f);
    const rect padding = ctbrowser::layout::padding_box_of(*f);
    return area.width > padding.width || area.height > padding.height;
}

/// The shadow host whose tree `id` is in; null in the document's own tree.
node_id browser::host_of(const read_txn & txn, node_id id) const {
    const document::shadow_tree * tree = doc_->shadow_tree_of(txn.root_of_tree(id));
    return tree != nullptr ? tree->host : node_id{};
}

/// Being rendered: the element has a box.
///
/// ponytail: shadow trees are not laid out (style and layout do not walk the
/// flat tree yet), so an element of one counts as rendered when its host is.
/// Once shadow trees get boxes of their own this is the box test alone.
bool browser::being_rendered(const read_txn & txn, node_id id) const {
    for (int guard = 0; id && guard < 64; ++guard) {
        const node_id host = host_of(txn, id);
        if (!host) { return absolute_rect_of(fragments_, id).has_value(); }
        id = host;
    }
    return false;
}

bool browser::is_focusable_area(const read_txn & txn, node_id id) {
    if (!id || txn.kind(id).value_or(node_kind::text) != node_kind::element) { return false; }
    // A slot is where its assigned nodes render, never a box of its own.
    if (txn.element_ns(id) == node_ns::html && txn.local_name(id) == "slot") { return false; }
    if (!tabindex_value(txn, id) && !focusable_by_default(txn, id) && !is_scroller(id)) {
        return false;
    }
    if (const document::shadow_tree * tree = doc_->shadow_tree_of(doc_->shadow_root_of(id));
        tree != nullptr && tree->delegates_focus) {
        return false;
    }
    // "Actually disabled" is a state of form controls and form-associated
    // custom elements only: a <div tabindex> in a disabled fieldset is as
    // focusable as one anywhere else.
    if ((kind_of(txn, id) != control_kind::none || bindings_->form_associated(id)) &&
        form_store::is_actually_disabled(txn, atoms_, id)) {
        return false;
    }
    const atom inert = atoms_.intern("inert");
    for (node_id at = id; at; at = txn.parent(at) ? txn.parent(at) : host_of(txn, at)) {
        if (txn.has_attribute(at, inert)) { return false; }
    }
    return being_rendered(txn, id);
}

/// "Get the focusable area" for a focus target that is not one itself: the
/// document element's is the viewport (null, with `viewport` set), a shadow
/// host that delegates focus keeps a focus already inside it or hands it to
/// its focus delegate, and nothing else has one.
node_id browser::focusable_area_for(const read_txn & txn, node_id target, bool click,
                                    bool & viewport) {
    viewport = target == txn.root();
    if (viewport) { return node_id{}; }
    const document::shadow_tree * tree = doc_->shadow_tree_of(doc_->shadow_root_of(target));
    if (tree == nullptr || !tree->delegates_focus) { return node_id{}; }
    for (node_id at = focused_; at; at = txn.parent(at) ? txn.parent(at) : host_of(txn, at)) {
        if (at == target) { return focused_; }
    }
    return focus_delegate(txn, target, click);
}

/// A shadow host's focus delegate: its shadow root's autofocus delegate when
/// there is one, else the first descendant of the root, in tree order, that
/// is a focusable area or has one to give.
node_id browser::focus_delegate(const read_txn & txn, node_id target, bool click) {
    const node_id root = doc_->shadow_root_of(target);
    const document::shadow_tree * tree = doc_->shadow_tree_of(root);
    if (tree == nullptr || !tree->delegates_focus) { return node_id{}; }
    const atom autofocus = atoms_.intern("autofocus");
    node_id found;
    const auto search = [&](auto && self, node_id at, bool autofocused) -> void {
        for (const node_id child : txn.children(at)) {
            if (found) { return; }
            if (txn.kind(child).value_or(node_kind::text) != node_kind::element) { continue; }
            if (!autofocused || txn.has_attribute(child, autofocus)) {
                bool viewport = false;
                found = is_focusable_area(txn, child)
                            ? child
                            : focusable_area_for(txn, child, click, viewport);
                if (found) { return; }
            }
            self(self, child, autofocused);
        }
    };
    search(search, root, true);
    if (!found) { search(search, root, false); }
    return found;
}

/// The focusing steps (HTML 6.6.4), for script's focus(), a click and the
/// keyboard alike: a target that is not a focusable area gives way to the one
/// "get the focusable area" finds, and without one nothing happens. Null is
/// the viewport, which is what blur() and a click on nothing come to.
bool browser::focus(node_id id, focus_origin origin) {
    const bool click = origin == focus_origin::pointer;
    if (id) {
        flush_for_read(); // "being rendered" must reflect the current DOM
        const auto txn = doc_->read();
        if (!txn.contains(id)) { return false; }
        if (!is_focusable_area(txn, id)) {
            bool viewport = false;
            id = focusable_area_for(txn, id, click, viewport);
            if (!id && !viewport) { return false; }
        }
    }
    return focus_update(id, origin);
}

/// The focus update steps: `change` owed by the field losing focus, `blur`
/// and `focusout` there, then `focus` and `focusin` at what gains it.
///
/// Also decides whether the focus is shown (:focus-visible; Selectors 4 leaves
/// the heuristic to the user agent and this is every engine's): always after
/// the keyboard moved it and on a field that takes typing; never after a
/// pointer moved it anywhere else; and for a script's focus(), according to
/// what the user did last. A key shows it, a pointer that moved focus does not,
/// and a click that focused nothing or no interaction at all leaves it shown.
bool browser::focus_update(node_id id, focus_origin origin) {
    if (id == focused_) { return false; }
    if (focused_) {
        // `change` fires on blur, not on every keystroke; that is the
        // difference between it and `input`, and pages rely on it. It fires
        // only for a text field the user edited to a different value since
        // it took focus (HTML 4.10.5.5, "the user commits the change"): a
        // button, a checkbox (whose change fires on activation) or an
        // untouched field has nothing to commit. It bubbles and is neither
        // cancelable nor composed.
        if (edited_since_focus_) {
            const control_state * state = control_state_of(focused_);
            if (state != nullptr && state->value != value_at_focus_) {
                (void)bindings_->dispatch_input("change", focused_, std::nullopt, std::nullopt);
            }
        }
        (void)set_state(focused_, state_focus, false);
        // HTML's focus update steps: `blur` then `focusout` at what loses
        // focus, each naming what gains it.
        (void)bindings_->dispatch_focus("blur", focused_, id);
        (void)bindings_->dispatch_focus("focusout", focused_, id);
        // The field losing focus collapses its selection to the caret, so a
        // highlight does not linger in a field nobody is typing in.
        const auto txn = doc_->read();
        if (txn.contains(focused_)) {
            const control_kind kind = kind_of(txn, focused_);
            if (kind == control_kind::text || kind == control_kind::textarea) {
                control_state & state = forms_.state_of(txn, atoms_, focused_);
                state.selection = state.caret;
            }
        }
    }
    const node_id was = focused_;
    focused_ = id;
    edited_since_focus_ = false;
    {
        const control_state * state = control_state_of(focused_);
        value_at_focus_ = state != nullptr ? state->value : std::string{};
    }
    // Tell the bindings before the event fires, so a `focus` listener asking
    // for document.activeElement gets the element it was just handed rather
    // than the one that had focus a moment ago.
    bindings_->observe_focus(focused_);
    restart_caret_blink(); // a field you just clicked into shows its caret at once
    if (focused_) {
        (void)set_state(focused_, state_focus, true);
        if (origin != focus_origin::script) { last_focus_by_ = origin; }
        const bool shown =
            origin == focus_origin::keyboard ||
            (origin == focus_origin::script && last_focus_by_ != focus_origin::pointer) ||
            supports_keyboard_input(doc_->read(), focused_);
        (void)set_state(focused_, state_focus_visible, shown);
        (void)bindings_->dispatch_focus("focus", focused_, was);
        (void)bindings_->dispatch_focus("focusin", focused_, was);
    }
    mark(dirty::paint);
    return true;
}

/// The parent of `at` in the flat tree. For a shadow root's child it is the
/// host; for a host's child it is the slot the child is assigned to, or null
/// when it is assigned nowhere, since then it is not in the flat tree at all.
node_id browser::flat_parent(const read_txn & txn, node_id at) {
    const node_id up = txn.parent(at);
    if (!up) { return host_of(txn, at); }
    if (doc_->shadow_tree_of(up) != nullptr) { return doc_->shadow_tree_of(up)->host; }
    if (doc_->shadow_root_of(up)) { return bindings_->assigned_slot_of(at); }
    return up;
}

/// What a click focuses: the nearest inclusive flat-tree ancestor that is a
/// focusable area or a host delegating its focus, or the control of a <label>
/// on the way. With none of those it is the viewport (null), which is how a
/// click on the page's background blurs a field.
node_id browser::click_focus_target(node_id from) {
    if (!from) { return node_id{}; }
    flush_for_read();
    const auto txn = doc_->read();
    const atom label = atoms_.intern_lower("label");
    for (node_id at = from; at; at = flat_parent(txn, at)) {
        if (is_focusable_area(txn, at)) { return at; }
        if (const document::shadow_tree * tree = doc_->shadow_tree_of(doc_->shadow_root_of(at));
            tree != nullptr && tree->delegates_focus) {
            return at;
        }
        if (txn.element_ns(at) == node_ns::html && txn.tag(at).value_or(atom{}) == label) {
            if (const node_id control = labelled_control(txn, at)) { return control; }
        }
    }
    return node_id{};
}

/// Whether anything under `at`, in its own tree, is a focusable area.
bool browser::holds_focusable(const read_txn & txn, node_id at) {
    for (const node_id child : txn.children(at)) {
        if (is_focusable_area(txn, child) || holds_focusable(txn, child)) { return true; }
    }
    return false;
}

/// Sequential focus navigation (HTML 6.6.5) follows the document's flattened
/// tabindex-ordered focus navigation scope. Every focus navigation scope owner
/// (the document, a shadow host, a slot) orders the focusable areas and the
/// owners in its own scope by tabindex: positive values first, ascending,
/// then everything at zero or with no value, each run in tree order. A
/// negative tabindex takes its element out, and for an owner the whole scope
/// it owns. Flattening puts each owner's list after it, or in its place when
/// the owner cannot take focus itself (a host that delegates focus, a slot).
///
/// `also`, where a search starts, is placed by tree order when it is not in
/// the order itself (a focused tabindex -1 element), and so is every owner
/// around it whose scope is out of the order: before the next element of its
/// scope that is in the order, or, going `backwards`, after the one before it.
/// Every engine does this, and the order the spec gives cannot say where such
/// a start is (WPT focus-with-negative-index: "after focusing negative
/// tabindex-ed elements, focus moves in tree order").
std::vector<node_id> browser::sequential_order(const read_txn & txn, node_id also, bool backwards) {
    struct item {
        node_id id;
        long long rank; // A positive tabindex, or LLONG_MAX for zero/none.
        bool owner;
        bool focusable;
    };
    const auto is_slot = [&](node_id id) {
        return txn.element_ns(id) == node_ns::html && txn.local_name(id) == "slot";
    };
    const auto inside = [&](node_id id, node_id owner) {
        for (node_id up = flat_parent(txn, id); up; up = flat_parent(txn, up)) {
            if (up == owner) { return true; }
        }
        return false;
    };
    // The elements a scope owner owns, found down from `roots`. The walk never
    // enters a host's own children, which its slots own, nor a slot, which
    // owns what is assigned to it or else its fallback content.
    const auto scope = [&](std::span<const node_id> roots) {
        std::vector<item> out;
        std::optional<item> start;
        std::size_t start_at = 0; // How many of `out` precede it in tree order.
        const auto walk = [&](auto && self, node_id at) -> void {
            if (txn.kind(at).value_or(node_kind::text) != node_kind::element) { return; }
            const bool host = static_cast<bool>(doc_->shadow_root_of(at));
            const bool slot = is_slot(at);
            const std::optional<long long> own = tabindex_value(txn, at);
            const long long tabindex = own.value_or(0);
            // A scroller that is focusable only because it scrolls is a Tab
            // stop only when nothing inside it takes focus; otherwise the
            // keyboard reaches its content instead (every engine; WPT
            // focus-navigation-scroller-interactive-child).
            const bool focusable =
                is_focusable_area(txn, at) &&
                (own || focusable_by_default(txn, at) || !holds_focusable(txn, at));
            const auto rank = [](long long t) {
                return t > 0 ? t : std::numeric_limits<long long>::max();
            };
            if ((host || slot || focusable) && tabindex >= 0) {
                out.push_back(item{at, rank(tabindex), host || slot, focusable});
            } else if ((host || slot) && also && inside(also, at)) {
                // A scope out of the order is still walked when the search
                // starts inside it, so Tab goes on to the rest of that scope
                // (WPT focus-tabindex-order-shadow-varying-tabindex-2, -3).
                start = item{at, 0, true, false};
                start_at = out.size();
            } else if (at == also) {
                start = item{at, 0, false, true}; // A start, not a stop of its own.
                start_at = out.size();
            }
            if (host || slot) { return; }
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        for (const node_id root : roots) { walk(walk, root); }
        const node_id after = start_at < out.size() ? out[start_at].id : node_id{};
        const node_id before = start_at > 0 ? out[start_at - 1].id : node_id{};
        std::ranges::stable_sort(out, {}, &item::rank);
        if (start) {
            auto where = backwards ? out.begin() : out.end();
            if (const node_id next = backwards ? before : after) {
                where = std::ranges::find(out, next, &item::id) + (backwards ? 1 : 0);
            }
            out.insert(where, *start);
        }
        return out;
    };
    std::vector<node_id> order;
    const auto flatten = [&](auto && self, const std::vector<item> & items, int depth) -> void {
        for (const item & one : items) {
            if (one.focusable) { order.push_back(one.id); }
            if (!one.owner || depth > 32) { continue; }
            std::vector<node_id> roots;
            if (is_slot(one.id)) {
                roots = bindings_->assigned_nodes_of(one.id);
                if (roots.empty()) {
                    const std::span<const node_id> fallback = txn.children(one.id);
                    roots.assign(fallback.begin(), fallback.end());
                }
            } else {
                const std::span<const node_id> kids = txn.children(doc_->shadow_root_of(one.id));
                roots.assign(kids.begin(), kids.end());
            }
            self(self, scope(roots), depth + 1);
        }
    };
    flatten(flatten, scope(txn.children(txn.root())), 0);
    return order;
}

/// Whether Tab may stop at `candidate` when moving from `from`. A radio group
/// keeps one stop in the order (HTML 4.10.5.1.18, and every browser): the
/// checked radio, or with none checked whichever is reached first. From a
/// radio of the group no other member is reachable, so Tab never walks from
/// one radio to the next of the same group.
bool browser::sequentially_reachable(const read_txn & txn, node_id candidate, node_id from) {
    if (kind_of(txn, candidate) != control_kind::radio) { return true; }
    const atom name_attr = atoms_.intern("name");
    const std::string_view name = txn.attribute_value(candidate, name_attr);
    if (name.empty()) { return true; }
    const node_id form = form_store::owning_form(txn, atoms_, candidate);
    const node_id tree = txn.root_of_tree(candidate);
    const auto grouped = [&](node_id other) {
        return other && kind_of(txn, other) == control_kind::radio &&
               txn.attribute_value(other, name_attr) == name &&
               form_store::owning_form(txn, atoms_, other) == form &&
               txn.root_of_tree(other) == tree;
    };
    const auto checked = [&](node_id id) { return forms_.state_of(txn, atoms_, id).checked; };
    if (checked(candidate)) { return true; }
    if (grouped(from)) { return false; }
    bool any = false;
    const auto walk = [&](auto && self, node_id at) -> void {
        for (const node_id child : txn.children(at)) {
            if (any) { return; }
            if (grouped(child) && checked(child)) {
                any = true;
                return;
            }
            self(self, child);
        }
    };
    walk(walk, tree);
    return !any;
}

bool browser::focus_next(bool backwards) {
    flush_for_read();
    node_id target;
    {
        const auto txn = doc_->read();
        const node_id from = focused_ && txn.contains(focused_) ? focused_ : node_id{};
        const std::vector<node_id> order = sequential_order(txn, from, backwards);
        if (order.empty()) { return false; }
        const std::size_t count = order.size();
        const auto at = std::ranges::find(order, from);
        // With nothing focused, Tab starts at the top and Shift+Tab at the
        // bottom; past either end it wraps.
        const std::size_t here = at != order.end() ? static_cast<std::size_t>(at - order.begin())
                                 : backwards       ? 0
                                                   : count - 1;
        for (std::size_t step = 1; step <= count && !target; ++step) {
            const node_id candidate =
                order[backwards ? (here + count - step) % count : (here + step) % count];
            if (candidate != from && sequentially_reachable(txn, candidate, from)) {
                target = candidate;
            }
        }
    }
    // Handled even when nothing else is reachable: the key is still Tab's.
    if (target) { (void)focus_update(target, focus_origin::keyboard); }
    return true;
}

/// The drag auto-scroll due now: how far the pointer is outside the field
/// being drag-selected on each axis, counted only on axes the view can still
/// scroll. `field` is null when no step is due.
browser::autoscroll_state browser::autoscroll_now() {
    autoscroll_state out;
    if (!field_selecting_ || !have_pointer_) { return out; }
    const rect box = viewport_box_of(field_selecting_);
    if (box.empty()) { return out; }
    const auto txn = doc_->read();
    if (!txn.contains(field_selecting_)) { return out; }
    const control_kind kind = kind_of(txn, field_selecting_);
    control_state & state = forms_.state_of(txn, atoms_, field_selecting_);
    const field_layout geometry = layout_of_field(box, field_selecting_, state, kind);

    // How far outside, per axis. Inside on an axis means no motion on it, so a
    // drag straight down does not also creep sideways.
    const float below = pointer_.y > box.bottom() ? pointer_.y - box.bottom()
                        : pointer_.y < box.y      ? pointer_.y - box.y
                                                  : 0;
    const float beside = pointer_.x > box.right() ? pointer_.x - box.right()
                         : pointer_.x < box.x     ? pointer_.x - box.x
                                                  : 0;

    // Move only where the view can still scroll that way. Otherwise
    // next_wakeup_ms() keeps scheduling a step forever, and an idle loop with the pointer
    // parked below a fully scrolled field spins at the step interval.
    const std::size_t most = geometry.lines.size() > geometry.visible_lines
                                 ? geometry.lines.size() - geometry.visible_lines
                                 : 0;
    if ((below > 0 && geometry.scroll_line < most) || (below < 0 && geometry.scroll_line > 0)) {
        out.below = below;
    }
    const float widest = std::max(0.0f, geometry.content_width + 1 - geometry.inner.width);
    if ((beside > 0 && geometry.scroll_x < widest) || (beside < 0 && geometry.scroll_x > 0)) {
        out.beside = beside;
    }
    if (out.below != 0 || out.beside != 0) { out.field = field_selecting_; }
    return out;
}

double browser::autoscroll_interval_ms(float distance) const {
    // The rate rises with how far outside the field the pointer is, so a small
    // overshoot creeps and a big one races: 100 ms / (1 + distance / 20 px),
    // floored at a frame.
    const double ramp = 1.0 + static_cast<double>(std::fabs(distance)) / 20.0;
    return std::max(16.0, 100.0 / ramp);
}

/// Runs one auto-scroll step: scrolls the field one line or character toward
/// the pointer, then moves the caret to the point under the pointer.
void browser::autoscroll_step(const autoscroll_state & at) {
    const rect box = viewport_box_of(at.field);
    if (box.empty()) { return; }
    const auto txn = doc_->read();
    const control_kind kind = kind_of(txn, at.field);
    control_state & state = forms_.state_of(txn, atoms_, at.field);
    {
        const field_layout geometry = layout_of_field(box, at.field, state, kind);
        // The view moves first, by one line or one character: the smallest
        // step there is, so the rate alone decides how fast it goes.
        if (at.below > 0) {
            state.scroll_line = geometry.scroll_line + 1;
        } else if (at.below < 0 && geometry.scroll_line > 0) {
            state.scroll_line = geometry.scroll_line - 1;
        }
        if (at.beside != 0) {
            // A character's worth, measured rather than assumed: "one column"
            // means nothing in a proportional face.
            const float step = measure()("n", geometry.size, geometry.metrics_face);
            state.scroll_x = geometry.scroll_x + (at.beside > 0 ? step : -step);
        }
    }
    // Then the caret follows, derived from where the pointer is against the
    // view just moved. Not reveal_caret, which is the inverse and would drag
    // the view back to the caret it is trying to lead.
    const std::size_t caret = offset_at_point(at.field, state, kind, pointer_.x, pointer_.y);
    if (caret != state.caret) { state.caret = caret; }
    restart_caret_blink();
    mark(dirty::paint);
}

} // namespace ctbrowser::shell
