//===- form_writes.cpp - Form control state writes --------------*- C++ -*-===//
//
// dom_bindings::settle_form_writes: the insertion and attribute change steps
// of the two controls whose state lives beside the tree rather than in it: a radio button's
// checkedness (HTML 4.10.5.1.16) and an option's selectedness (HTML 4.10.10
// and 4.10.7). Run from mutated() over the document's write log, so an
// insertion or an attribute write by any path (a Node method, innerHTML, the
// parser) takes its steps before the script that made it can look.
//
//   * A radio whose checkedness is true after it becomes connected, gains a
//     form owner, has its name, type or form attribute changed, or is given
//     its `checked` attribute while clean (which sets the checkedness)
//     unchecks every other radio in its group, so the last of two checked
//     radios the parser met wins, and `text.type = 'radio'` unchecks the radio
//     it joins.
//   * An option's `selected` attribute, added or removed, sets a clean
//     option's selectedness to match, and its select re-settles ("reset a
//     select's selectedness": removing the selected one selects the first
//     enabled option); and an option inserted into a single select's list of
//     options with its selectedness true (or given it by the attribute)
//     deselects the others.
//
// The writes are settled as one batch, in order. The log holds everything
// since the last mutation, and the tree is already in its final shape: two
// checked radios the parser inserted are both present when the first one's
// steps run. So each step leaves alone a control that has a later step of
// its own in the batch (in write order it was not there yet), and that is
// what makes the last one win. For the same reason an attribute written
// while its element was not yet where it ended up (innerHTML copies an
// element's attributes in before the element) takes no steps of its own: its
// insertion, later in the batch, takes them where it lands.
//
//===----------------------------------------------------------------------===//

#include "helpers.hpp"

#include <unordered_map>

namespace ctbrowser::shell {
using namespace detail;

void dom_bindings::settle_form_writes(const std::vector<document::write_note> & writes) {
    if (cx_ == nullptr || doc_ == nullptr || forms_ == nullptr || writes.empty()) { return; }
    context & c = *cx_;
    const control_helpers helpers{this};
    using edit = document::write_note::edit;
    struct step {
        node_id node;
        bool attribute = false; ///< An attribute change, else an insertion.
        bool removal = false;   ///< The attribute change was a removal.
    };
    std::vector<step> steps;
    {
        const auto txn = doc_->read();
        const auto radio = [&](node_id at) {
            return helpers.is(txn, at, "input") && helpers.input_type(txn, this, at) == "radio";
        };
        // Where each inserted node's last insertion is: a subtree walked for
        // one insertion skips a descendant inserted again later, which its own
        // note covers, so a parse (one note per element) walks each node once.
        std::unordered_map<std::uint64_t, std::size_t> inserted_at;
        for (std::size_t i = 0; i < writes.size(); ++i) {
            if (writes[i].kind == edit::inserted && writes[i].child) {
                inserted_at[writes[i].child.key()] = i;
            }
        }
        for (std::size_t i = 0; i < writes.size(); ++i) {
            const document::write_note & note = writes[i];
            if (note.kind == edit::inserted) {
                if (!note.child || !txn.contains(note.child)) { continue; }
                const auto walk = [&](auto && self, node_id at) -> void {
                    if (at != note.child) {
                        const auto again = inserted_at.find(at.key());
                        if (again != inserted_at.end() && again->second > i) { return; }
                    }
                    if (helpers.is(txn, at, "option") || radio(at)) { steps.push_back({at}); }
                    for (const node_id child : txn.children(at)) { self(self, child); }
                };
                walk(walk, note.child);
            } else if (note.kind == edit::attribute && note.node && txn.contains(note.node)) {
                bool placed_later = false;
                for (node_id up = note.node; up && !placed_later; up = txn.parent(up)) {
                    const auto again = inserted_at.find(up.key());
                    placed_later = again != inserted_at.end() && again->second > i;
                }
                if (placed_later) { continue; }
                const std::string_view name = atoms_->text(note.name);
                if ((name == "selected" && helpers.is(txn, note.node, "option")) ||
                    ((name == "name" || name == "type" || name == "form" || name == "checked") &&
                     helpers.is(txn, note.node, "input"))) {
                    steps.push_back({note.node, true, note.removal});
                }
            }
        }
    }
    if (steps.empty()) { return; }
    std::unordered_map<std::uint64_t, std::size_t> last;
    for (std::size_t k = 0; k < steps.size(); ++k) { last[steps[k].node.key()] = k; }
    const auto later = [&last](node_id other, std::size_t k) {
        const auto held = last.find(other.key());
        return held != last.end() && held->second > k;
    };
    for (std::size_t k = 0; k < steps.size(); ++k) {
        const step & at = steps[k];
        const auto txn = doc_->read();
        if (!txn.contains(at.node)) { continue; }
        if (helpers.is(txn, at.node, "option")) {
            // The attribute's change steps: a clean option's selectedness
            // follows it; a dirty one's belongs to the script or the user.
            const bool was = helpers.option_selected(c, txn, this, at.node);
            bool now = was;
            if (at.attribute && !context::truthy(helpers.slot_of(c, this, at.node, dirty_slot))) {
                now = !at.removal;
                helpers.set_slot(c, this, at.node, selected_slot, value::boolean(now));
            }
            const node_id select = helpers.select_of(txn, at.node);
            if (!select) { continue; }
            // Selectedness true on arriving, or made true here, leaves every
            // other option of a single select false...
            if (now && !(at.attribute && was) && !helpers.is_multiple(txn, this, select)) {
                for (const node_id other : helpers.options_of(txn, this, select)) {
                    if (other != at.node && !later(other, k) &&
                        helpers.option_selected(c, txn, this, other)) {
                        helpers.set_slot(c, this, other, selected_slot, value::boolean(false));
                    }
                }
            }
            // ...and an attribute change re-settles the select either way.
            if (at.attribute) { helpers.ask_for_reset(c, this, select); }
            continue;
        }
        if (!helpers.is(txn, at.node, "input") ||
            helpers.input_type(txn, this, at.node) != "radio") {
            continue;
        }
        // An insertion unchecks the rest of the group only when it connected
        // the radio or gave it a form owner; in a detached tree with no form,
        // two checked radios may coexist.
        if (!at.attribute && !is_connected(at.node) && !helpers.form_owner(txn, this, at.node)) {
            continue;
        }
        if (!forms_->state_of(txn, *atoms_, at.node).checked) { continue; }
        helpers.walk_tree(txn, helpers.tree_top(txn, this, at.node), [&](node_id other) {
            if (other != at.node && !later(other, k) &&
                helpers.in_radio_group(txn, this, at.node, other)) {
                control_state & held = forms_->state_of(txn, *atoms_, other);
                held.checked = false;
                held.checked_edited = true;
            }
        });
    }
}

} // namespace ctbrowser::shell
