// browser - focus: the editable field, labels, HTML's focusable areas and
// focusing steps, sequential navigation, and the drag auto-scroll that runs
// off tick().

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

    // `for` wins when it is there, even if it names nothing - an explicit
    // reference that does not resolve labels NOTHING, rather than quietly
    // falling back to whatever the label happens to contain.
    const std::string_view target = txn.attribute_value(label, atoms_.intern("for"));
    if (!target.empty()) {
        const node_id named = bindings_->find_by_id(std::string{target});
        return kind_of(txn, named) != control_kind::none ? named : node_id{};
    }

    // Otherwise the first labelable DESCENDANT, in document order. The label
    // itself cannot be the answer - a <label> is not a control - so the walk
    // starts at its children.
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
    // Nothing up the tree IS a control, so the click may still be on a label
    // FOR one. Second, not first: a press on the control inside a label finds
    // it on the walk above and never reaches here, so a click can never resolve
    // twice and toggle a checkbox back off again.
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

// Did the mutation being announced move the focused element, or an ancestor
// of it, out of the tree and back? See dom_bindings::moved_by_mutation.
bool browser::focus_was_moved() const {
    const std::span<const node_id> moved = bindings_->moved_by_mutation();
    if (moved.empty()) { return false; }
    const auto txn = doc_->read();
    for (node_id at = focused_; at; at = txn.parent(at)) {
        if (std::ranges::find(moved, at) != moved.end()) { return true; }
    }
    return false;
}

// ============================================================================
// FOCUS, HTML 6.6
// ============================================================================
//
// A FOCUSABLE AREA (6.6.2) is an element with a tabindex value, or one the
// user agent makes focusable - a form control, a link, its details' summary,
// an editing host, media showing controls - that is not a shadow host
// delegating its focus, not actually disabled, not inert and being rendered.
// Nothing else holds focus. `focus()` on anything else runs "get the
// focusable area", and when that finds none it does nothing at all - which is
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
    // A navigable container: its focus is its document's (frames are not
    // entered by Tab here, so the frame itself is the stop).
    if (local == "iframe" || local == "frame") { return true; }
    if (local == "summary") {
        // The summary for its parent details: that details' FIRST summary.
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

// AN EDITING HOST: contenteditable true or plaintext-only on an element whose
// parent is not editable itself - the host, not what it holds.
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

// An element that takes TEXT from the keyboard - a text field, a textarea,
// an editing host - shows focus however it got it (:focus-visible).
bool browser::supports_keyboard_input(const read_txn & txn, node_id id) {
    const control_kind kind = kind_of(txn, id);
    return kind == control_kind::text || kind == control_kind::textarea || is_editing_host(txn, id);
}

// A SCROLLABLE REGION is a focusable area of its own (6.6.2): a scroll
// container whose content overflows it, so the keyboard can page it -
// `scroller.focus()` and then the arrow keys (scrollend-event-for-user-scroll).
bool browser::is_scroller(node_id id) const {
    const ctbrowser::layout::fragment * f = fragments_.find(id);
    if (f == nullptr || f->box == nullptr || !f->box->scroll_container) { return false; }
    const rect area = ctbrowser::layout::scrolling_area_of(*f);
    const rect padding = ctbrowser::layout::padding_box_of(*f);
    return area.width > padding.width || area.height > padding.height;
}

// The shadow host whose tree `id` is in; null in the document's own tree.
node_id browser::host_of(const read_txn & txn, node_id id) const {
    const document::shadow_tree * tree = doc_->shadow_tree_of(txn.root_of_tree(id));
    return tree != nullptr ? tree->host : node_id{};
}

// Being rendered: the element has a box.
//
// ponytail: shadow trees are not laid out - style and layout do not walk the
// flat tree yet - so an element of one counts as rendered when its host is.
// Once shadow trees get boxes of their own this is the box test alone.
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
    // "Actually disabled" is a form control's state - a form-associated
    // custom element's too: a <div tabindex> in a disabled fieldset is as
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

// "Get the focusable area" for a focus target that is not one itself: the
// document element's is the viewport (`viewport` says so), a shadow host
// that delegates focus keeps a focus already inside it or hands it to its
// focus delegate, and nothing else has one.
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

// A shadow host's FOCUS DELEGATE: its shadow root's autofocus delegate when
// there is one, else the first descendant of the root, in tree order, that
// is a focusable area or has one to give.
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

// THE FOCUSING STEPS (6.6.4), for script's focus(), a click and the keyboard
// alike: a target that is not a focusable area gives way to the one "get the
// focusable area" finds, and without one nothing happens. Null is the
// viewport, which is what blur() and a click on nothing come to.
bool browser::focus(node_id id, focus_origin origin) {
    const bool click = origin == focus_origin::pointer;
    if (id) {
        flush_for_read(); // whether it is rendered is a question about now
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

// THE FOCUS UPDATE STEPS: `change` owed by the field losing focus, `blur` and
// `focusout` there, then `focus` and `focusin` at what gains it.
//
// And whether the focus is shown - :focus-visible (Selectors 4 leaves the
// heuristic to the user agent; this is every engine's): always after the
// keyboard moved it and on a field that takes typing; never after a pointer
// moved it anywhere else; and for a script's focus() as the user last moved
// focus - a click that focused nothing moved nothing, and before the user
// has moved it at all a script's focus shows.
bool browser::focus_update(node_id id, focus_origin origin) {
    if (id == focused_) { return false; }
    if (focused_) {
        // `change` fires on BLUR, not on every keystroke - that is the
        // difference between it and `input`, and pages rely on it. And only
        // for a text field the USER edited to a different value since it took
        // focus (HTML 4.10.5.5, "the user commits the change"): a button, a
        // checkbox (whose change fires on activation) or an untouched field
        // has nothing to commit. Bubbling, and neither cancelable nor composed.
        if (edited_since_focus_) {
            const control_state * state = control_state_of(focused_);
            if (state != nullptr && state->value != value_at_focus_) {
                (void)bindings_->dispatch_input("change", focused_, std::nullopt, std::nullopt);
            }
        }
        (void)set_state(focused_, state_focus, false);
        // HTML's focus update steps: `blur` then `focusout` at what loses
        // focus, each naming what gains it. `blur` never fired at all before.
        (void)bindings_->dispatch_focus("blur", focused_, id);
        (void)bindings_->dispatch_focus("focusout", focused_, id);
        // And the outgoing field DROPS ITS SELECTION. A highlight left
        // behind in a field nobody is typing in reads as still selected,
        // and Ctrl+A followed by a click somewhere else did exactly that.
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
    // Told to the bindings BEFORE the event fires, so a `focus` listener asking
    // document.activeElement gets the element it was just handed rather than
    // the one that had focus a moment ago.
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

// The parent in the FLAT tree: a shadow root's child's is the host, a host's
// child's is the slot it is assigned to - none when it is assigned nowhere,
// since then it is not in the flat tree at all.
node_id browser::flat_parent(const read_txn & txn, node_id at) {
    const node_id up = txn.parent(at);
    if (!up) { return host_of(txn, at); }
    if (doc_->shadow_tree_of(up) != nullptr) { return doc_->shadow_tree_of(up)->host; }
    if (doc_->shadow_root_of(up)) { return bindings_->assigned_slot_of(at); }
    return up;
}

// WHAT A CLICK FOCUSES: the nearest inclusive flat-tree ancestor that is a
// focusable area or a host delegating its focus, or the control of a <label>
// on the way; with none of those the viewport, which is how a click on the
// page's background blurs a field.
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

// Whether anything under `at`, in its own tree, is a focusable area.
bool browser::holds_focusable(const read_txn & txn, node_id at) {
    for (const node_id child : txn.children(at)) {
        if (is_focusable_area(txn, child) || holds_focusable(txn, child)) { return true; }
    }
    return false;
}

// SEQUENTIAL FOCUS NAVIGATION (6.6.5) follows the document's FLATTENED
// TABINDEX-ORDERED FOCUS NAVIGATION SCOPE. Every focus navigation scope owner
// - the document, a shadow host, a slot - orders the focusable areas and the
// owners in its own scope by tabindex: positive values first, ascending,
// then everything at zero or with no value, each run in tree order; a
// negative tabindex takes its element out, and with an owner the whole scope
// it owns. Flattening puts each owner's list after it, or in its place when
// the owner cannot take focus itself - a host that delegates focus, a slot.
//
// `also` is placed as if its tabindex were zero when it is not in the order
// itself - a focused tabindex -1 element is where a search starts.
std::vector<node_id> browser::sequential_order(const read_txn & txn, node_id also) {
    struct item {
        node_id id;
        long long rank; // a positive tabindex, or the largest there is for zero
        bool owner;
        bool focusable;
    };
    const auto is_slot = [&](node_id id) {
        return txn.element_ns(id) == node_ns::html && txn.local_name(id) == "slot";
    };
    // The elements a scope owner owns, found down from `roots`: never into a
    // host's own children, which its slots own, and never into a slot's
    // fallback when something is assigned to it, since then that is not
    // rendered.
    const auto scope = [&](std::span<const node_id> roots) {
        std::vector<item> out;
        const auto walk = [&](auto && self, node_id at) -> void {
            if (txn.kind(at).value_or(node_kind::text) != node_kind::element) { return; }
            const bool host = static_cast<bool>(doc_->shadow_root_of(at));
            const bool slot = is_slot(at);
            const std::optional<long long> own = tabindex_value(txn, at);
            const long long tabindex = own.value_or(0);
            // A scroller that is focusable only for being one is a Tab stop
            // only when nothing inside it takes focus: the keyboard reaches
            // its content instead (every engine; focus-navigation-scroller-
            // interactive-child).
            const bool focusable =
                is_focusable_area(txn, at) &&
                (own || focusable_by_default(txn, at) || !holds_focusable(txn, at));
            const auto rank = [](long long t) {
                return t > 0 ? t : std::numeric_limits<long long>::max();
            };
            if ((host || slot || focusable) && tabindex >= 0) {
                out.push_back(item{at, rank(tabindex), host || slot, focusable});
            } else if (at == also) {
                // The start of a search, not a stop of its own: where a
                // tabindex of zero would put it.
                out.push_back(item{at, rank(0), false, true});
            }
            if (host || (slot && !bindings_->assigned_nodes_of(at).empty())) { return; }
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        for (const node_id root : roots) { walk(walk, root); }
        std::ranges::stable_sort(out, {}, &item::rank);
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

// RADIO GROUPS keep one stop in the order (HTML 4.10.5.1.18, and every
// browser): the checked radio, or with none checked whichever is reached
// first - and from a radio of the group only its checked one, so Tab never
// walks from one radio to the next of the same group.
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
        const std::vector<node_id> order = sequential_order(txn, from);
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
    // Handled even when nothing else is reachable - the key is still Tab's.
    if (target) { (void)focus_update(target, focus_origin::keyboard); }
    return true;
}

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

    // ...and only where the view can still MOVE that way. Without this the
    // wakeup below is scheduled forever and an idle loop with the pointer
    // parked below a fully-scrolled field spins at the step interval.
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

void browser::autoscroll_step(const autoscroll_state & at) {
    const rect box = viewport_box_of(at.field);
    if (box.empty()) { return; }
    const auto txn = doc_->read();
    const control_kind kind = kind_of(txn, at.field);
    control_state & state = forms_.state_of(txn, atoms_, at.field);
    {
        const field_layout geometry = layout_of_field(box, at.field, state, kind);
        // THE VIEW MOVES FIRST. One line, one character - the smallest step
        // there is, so the rate alone decides how fast it goes.
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
    // ...and THEN the caret follows it, derived from where the pointer actually
    // is against the view we just moved. Not reveal_caret, which is the inverse
    // and would drag the view back to the caret it is trying to lead.
    const std::size_t caret = offset_at_point(at.field, state, kind, pointer_.x, pointer_.y);
    if (caret != state.caret) { state.caret = caret; }
    restart_caret_blink();
    mark(dirty::paint);
}

} // namespace ctbrowser::shell
