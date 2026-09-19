#include <ctbrowser/shell/bindings.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser::shell {
namespace {
constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();
}
// --- the scan ----------------------------------------------------------------------

void dom_bindings::walk_custom_elements(const read_txn & txn, node_id start, bool connected,
                                        bool upgrade, flat_map<std::uint64_t, bool> & roots_seen) {
    struct frame {
        node_id at;
        bool moved; // an ancestor changed parent while connected
    };
    const std::vector<custom_element_definition> & defs = primary().custom_definitions_;
    const auto observed = [](const custom_element_definition & def, std::string_view name) {
        for (const std::string & want : def.observed_attributes) {
            if (want == name) { return true; }
        }
        return false;
    };
    // The observed attributes as they are now, diffed against `was`.
    // THE LOCAL NAME is what observedAttributes names and what the callback
    // receives: `svg:title` in a namespace is the attribute `title`.
    const auto local_name = [this](const attribute & held) {
        const std::string_view qualified = atoms_->text(held.name);
        const std::size_t colon = qualified.find(':');
        return std::string{held.ns && colon != std::string_view::npos ? qualified.substr(colon + 1)
                                                                      : qualified};
    };
    const auto same = [](const attribute & a, const attribute & b) {
        return a.name == b.name && a.ns == b.ns;
    };
    const auto diff_attributes = [&](const custom_element_definition & def,
                                     custom_element_state & state, node_id at) {
        if (def.observed_attributes.empty()) { return; }
        std::vector<attribute> now;
        for (const attribute & held : txn.attributes(at)) {
            const std::string local = local_name(held);
            if (!observed(def, local)) { continue; }
            now.push_back(held);
            const std::string * before = nullptr;
            for (const attribute & old : state.attributes) {
                if (same(old, held)) { before = &old.value; }
            }
            if (before != nullptr && *before == held.value) { continue; }
            custom_element_reaction reaction;
            reaction.target = at;
            reaction.definition = state.definition;
            reaction.what = custom_element_reaction::kind::attribute_changed;
            reaction.name = local;
            reaction.ns = std::string{atoms_->text(held.ns)};
            reaction.has_old = before != nullptr;
            if (before != nullptr) { reaction.old_value = *before; }
            reaction.has_new = true;
            reaction.new_value = held.value;
            custom_reactions_.push_back(std::move(reaction));
        }
        for (const attribute & old : state.attributes) {
            bool still = false;
            for (const attribute & held : now) { still = still || same(held, old); }
            if (still) { continue; }
            custom_element_reaction reaction;
            reaction.target = at;
            reaction.definition = state.definition;
            reaction.what = custom_element_reaction::kind::attribute_changed;
            reaction.name = local_name(old);
            reaction.ns = std::string{atoms_->text(old.ns)};
            reaction.has_old = true;
            reaction.old_value = old.value;
            custom_reactions_.push_back(std::move(reaction));
        }
        state.attributes = std::move(now);
    };
    const auto enqueue = [this](node_id at, std::size_t definition,
                                custom_element_reaction::kind what) {
        custom_element_reaction reaction;
        reaction.target = at;
        reaction.definition = definition;
        reaction.what = what;
        custom_reactions_.push_back(std::move(reaction));
    };
    using kind = custom_element_reaction::kind;
    // HTML 4.13.7.2: a form-associated element's form owner and disabledness,
    // diffed - formAssociatedCallback(form) when the owner changes (the
    // "reset the form owner" steps), formDisabledCallback(disabled) when
    // the `disabled` attribute or a <fieldset> ancestor changes it.
    const auto diff_form = [&](const custom_element_definition & def, custom_element_state & state,
                               node_id at) {
        if (!def.form_associated) { return; }
        const node_id form = form_owner_of(txn, at);
        if (form != state.form) {
            custom_element_reaction reaction;
            reaction.target = at;
            reaction.definition = state.definition;
            reaction.what = kind::form_associated;
            reaction.form = form;
            custom_reactions_.push_back(std::move(reaction));
            state.form = form;
        }
        const bool disabled = form_control_disabled(txn, at);
        if (disabled != state.disabled) {
            custom_element_reaction reaction;
            reaction.target = at;
            reaction.definition = state.definition;
            reaction.what = kind::form_disabled;
            reaction.flag = disabled;
            custom_reactions_.push_back(std::move(reaction));
            state.disabled = disabled;
        }
    };

    std::vector<frame> pending{frame{start, false}};
    while (!pending.empty()) {
        const frame here = pending.back();
        pending.pop_back();
        bool moved = here.moved;
        node_id shadow;
        if (txn.kind(here.at).value_or(node_kind::text) == node_kind::element &&
            txn.element_ns(here.at) == node_ns::html) {
            shadow = shadow_root_of(here.at);
            const std::uint64_t key = here.at.key();
            const auto found = custom_elements_.find(key);
            if (found == custom_elements_.end() && upgrade) {
                const std::size_t index = custom_definition_for(txn, here.at);
                if (index != npos) {
                    // "Upgrade an element", HTML 4.13.5: the constructor,
                    // then attributeChangedCallback for each observed
                    // attribute present, then connectedCallback if connected.
                    const custom_element_definition & def = defs[index];
                    custom_element_state state;
                    state.definition = index;
                    state.state = custom_element_state::status::precustomized;
                    state.connected = connected;
                    state.seen = scan_generation_;
                    state.parent = txn.parent(here.at);
                    enqueue(here.at, index, kind::upgrade);
                    diff_attributes(def, state, here.at);
                    if (connected) { enqueue(here.at, index, kind::connected); }
                    diff_form(def, state, here.at);
                    watch_loose(key, state);
                    custom_elements_.emplace(key, std::move(state));
                }
            } else if (found != custom_elements_.end()) {
                custom_element_state & state = found->second;
                state.seen = scan_generation_;
                if (state.state != custom_element_state::status::failed) {
                    const custom_element_definition & def = defs[state.definition];
                    const node_id parent = txn.parent(here.at);
                    moved = moved || (connected && state.connected && parent != state.parent);
                    if (state.connected && !connected) {
                        enqueue(here.at, state.definition, kind::disconnected);
                    } else if (!state.connected && connected) {
                        enqueue(here.at, state.definition, kind::connected);
                    } else if (moved && connected) {
                        // moveBefore: connectedMoveCallback when the class has
                        // one, the disconnect/connect pair otherwise.
                        if (def.connected_move.is_callable()) {
                            enqueue(here.at, state.definition, kind::connected_move);
                        } else {
                            enqueue(here.at, state.definition, kind::disconnected);
                            enqueue(here.at, state.definition, kind::connected);
                        }
                    }
                    diff_attributes(def, state, here.at);
                    diff_form(def, state, here.at);
                    state.connected = connected;
                    state.parent = parent;
                    watch_loose(key, state);
                }
            }
        }
        // SHADOW-INCLUDING TREE ORDER: a host's shadow tree comes before its
        // own children. Pushed last so it is popped first.
        const std::span<const node_id> children = txn.children(here.at);
        for (std::size_t i = children.size(); i-- > 0;) {
            pending.push_back(frame{children[i], moved});
        }
        if (shadow) {
            roots_seen.emplace(shadow.key(), true);
            pending.push_back(frame{shadow, moved});
        }
    }
}

void dom_bindings::upgrade_created_subtree(node_id root) {
    if (primary().custom_definitions_.empty() || cx_ == nullptr || doc_ == nullptr || !root ||
        !has_browsing_context()) {
        return;
    }
    const std::size_t from = custom_reactions_.size();
    {
        const auto txn = doc_->read();
        const node_id top = root_of_tree(txn, root, true);
        const bool connected =
            top == txn.root() || txn.kind(top).value_or(node_kind::element) == node_kind::document;
        flat_map<std::uint64_t, bool> roots_seen;
        walk_custom_elements(txn, root, connected, true, roots_seen);
    }
    flush_custom_element_reactions(from);
}

void dom_bindings::scan_custom_elements() {
    const auto txn = doc_->read();
    ++scan_generation_;
    flat_map<std::uint64_t, bool> roots_seen;
    const bool upgrade = has_browsing_context();
    // THE SHADOW-INCLUDING WALK crosses every shadow tree of a connected host.
    // A shadow tree whose host is detached is a detached subtree: nothing in
    // it is upgraded until it connects, and what is tracked in it is walked
    // below like any other loose element - so the shadow roots are not
    // enumerated here (a page can hold 100,000 of them).
    walk_custom_elements(txn, txn.root(), true, upgrade, roots_seen);
    // WHAT NO ROOT REACHED IS DETACHED - AND IS STILL A CUSTOM ELEMENT.
    //
    // A disconnected element's reactions do not stop: `el.setAttribute(...)`
    // on one a page made and has not inserted still runs
    // attributeChangedCallback, and that is most of what
    // custom-elements/reactions/* measures - every one of those files creates
    // its element, mutates it, and reads the log before anything is in the
    // document. Walking it with connected=false does both halves at once, the
    // disconnect and the attribute diff, through the same code a connected one
    // goes through.
    //
    // THE KEYS ARE COLLECTED FIRST: an upgrade inside a detached subtree
    // inserts into this map, and a flat_map rehashes under an iterator.
    //
    // AND ONLY THE ONES THAT CAN SAY SOMETHING: a detached element that was
    // already detached, observes no attribute and is not form-associated has
    // no reaction left to make until it connects - and a page that keeps
    // 100,000 of them (ElementInternals-target-element-is-held-strongly)
    // pays a walk of each on every mutation otherwise.
    std::vector<node_id> loose;
    for (const auto & [key, watched] : loose_watch_) {
        const auto held = custom_elements_.find(key);
        if (held != custom_elements_.end() && held->second.seen != scan_generation_) {
            loose.push_back(unpack(key));
        }
    }
    for (const node_id at : loose) {
        if (txn.contains(at)) { walk_custom_elements(txn, at, false, false, roots_seen); }
    }
    // A node whose WRAPPER LEFT WITH IT - `node_from` adopted it into another
    // document and rebound the page's object to the copy - is that
    // document's custom element now: its state moves over, and the adopting
    // steps (HTML 4.13.6) enqueue adoptedCallback there, after the
    // disconnectedCallback the walk above queued here.
    for (const auto & [key, obj] : adopted_away_) {
        const auto it = custom_elements_.find(key);
        if (it == custom_elements_.end()) { continue; }
        dom_bindings * now = owner_of(value::object(obj));
        if (now != nullptr && now != this) {
            const node_id fresh = now->handle_of(value::object(obj));
            custom_element_state state = it->second;
            state.connected = false;
            state.parent = node_id{};
            state.seen = 0;
            if (fresh && state.state == custom_element_state::status::custom) {
                custom_element_reaction reaction;
                reaction.target = fresh;
                reaction.definition = state.definition;
                reaction.what = custom_element_reaction::kind::adopted;
                reaction.old_document = document_;
                reaction.new_document = now->document_;
                bool noted = false;
                for (const auto & [other, from] : adoptees_) { noted = noted || other == now; }
                if (!noted) { adoptees_.emplace_back(now, now->custom_reactions_.size()); }
                now->custom_reactions_.push_back(std::move(reaction));
                now->watch_loose(fresh.key(), state);
                now->custom_elements_.insert_or_assign(fresh.key(), std::move(state));
            }
        }
        loose_watch_.erase(key);
        custom_elements_.erase(it);
    }
    // A node that is GONE, rather than merely detached, is forgotten - swept
    // when the map has doubled since the last sweep, not on every mutation:
    // a stale entry costs a hash slot and nothing else (a slot reused by a
    // new node has a new generation, so a new key).
    if (custom_elements_.size() >= sweep_at_) {
        for (auto it = custom_elements_.begin(); it != custom_elements_.end();) {
            if (it->second.seen != scan_generation_ && !txn.contains(unpack(it->first))) {
                loose_watch_.erase(it->first);
                it = custom_elements_.erase(it);
                continue;
            }
            ++it;
        }
        sweep_at_ = std::max<std::size_t>(64, custom_elements_.size() * 2);
    }
}

void dom_bindings::watch_loose(std::uint64_t key, const custom_element_state & state) {
    // What a DETACHED element can still say: disconnectedCallback if it was
    // connected, attributeChangedCallback if it observes anything, and the
    // form callbacks if it is form-associated. One that can say nothing is
    // not walked until it connects.
    const custom_element_definition & def = primary().custom_definitions_[state.definition];
    if (state.connected || !def.observed_attributes.empty() || def.form_associated) {
        loose_watch_.emplace(key, true);
    } else {
        loose_watch_.erase(key);
    }
}

void dom_bindings::run_upgrade(context & cx, std::size_t index, node_id target, value wrapper) {
    dom_bindings & reg = primary();
    const auto found = custom_elements_.find(target.key());
    if (found == custom_elements_.end()) { return; }
    found->second.state = custom_element_state::status::precustomized;
    bool threw = false;
    value thrown = value::undefined();
    // 8.1: a definition that disabled shadow roots refuses an element that
    // already has one.
    if (reg.custom_definitions_[index].disable_shadow && shadow_root_of(target)) {
        threw = true;
        thrown = make_dom_exception(cx, "NotSupportedError",
                                    "The element already has a shadow root and the definition "
                                    "disables shadow");
    } else {
        reg.custom_definitions_[index].construction_stack.push_back({target, false});
        // COPIED OUT: the constructor may define another element and grow
        // the vector under a reference.
        const value constructor = reg.custom_definitions_[index].constructor;
        const value made = cx.call_fenced(constructor, {}, wrapper, threw, thrown);
        const bool constructed =
            reg.custom_definitions_[index].construction_stack.back().constructed;
        reg.custom_definitions_[index].construction_stack.pop_back();
        if (!threw && cx.failed()) {
            threw = true;
            thrown = cx.make_error("Error", cx.take_error());
        }
        // 8.4: the constructor must have answered THIS element - a class body
        // that returns some other object did not construct it, and one that
        // never reached `super()` left `this` uninitialised.
        if (!threw && ((made.is_object_like() && made.bits() != wrapper.bits()) || !constructed)) {
            threw = true;
            thrown = cx.make_error("TypeError", constructed
                                                    ? "The custom element constructor returned a "
                                                      "different object"
                                                    : "The custom element constructor did not "
                                                      "call super()");
        }
    }
    const auto again = custom_elements_.find(target.key());
    if (again == custom_elements_.end()) { return; }
    if (threw) {
        // The element is `failed`, its queued reactions are dropped, and the
        // exception is reported (HTML 4.13.5 step 8's catch).
        again->second.state = custom_element_state::status::failed;
        for (std::size_t i = custom_reactions_.size(); i-- > 0;) {
            if (custom_reactions_[i].target != target) { continue; }
            custom_reactions_.erase(custom_reactions_.begin() + static_cast<std::ptrdiff_t>(i));
            for (std::size_t & floor : reaction_floors_) {
                if (floor > i) { --floor; }
            }
        }
        report_custom_element_exception(cx, thrown, "custom element upgrade");
        return;
    }
    again->second.state = custom_element_state::status::custom;
}

void dom_bindings::flush_custom_element_reactions(std::size_t from) {
    context & cx = *cx_;
    // HTML 4.13.6's "invoke custom element reactions" over an ELEMENT QUEUE:
    // the elements enqueued by this [CEReactions] native - the reactions at
    // `from` and after, in the order they were first enqueued - and for each
    // of them EVERY reaction its own queue holds, wherever it sits in the
    // vector. A native re-entered from a callback (a `define()` or a
    // setAttribute inside a constructor) pushes its own element queue: it
    // runs the pending reactions of what IT touched, pending ones included,
    // and leaves the rest of the outer queue for the outer pop.
    //
    // THE FLOORS ARE A STACK: a nested pop that takes a pending reaction from
    // below an outer floor shifts the outer's region down, and the floor
    // moves with it, so nothing an outer native enqueued is skipped.
    reaction_floors_.push_back(from);
    const std::size_t level = reaction_floors_.size() - 1;
    while (custom_reactions_.size() > reaction_floors_[level]) {
        const node_id element = custom_reactions_[reaction_floors_[level]].target;
        auto next = std::ranges::find_if(custom_reactions_,
                                         [element](const auto & r) { return r.target == element; });
        const std::size_t at = static_cast<std::size_t>(next - custom_reactions_.begin());
        const custom_element_reaction reaction = std::move(*next);
        custom_reactions_.erase(next);
        for (std::size_t & floor : reaction_floors_) {
            if (floor > at) { --floor; }
        }
        if (reaction.definition >= primary().custom_definitions_.size()) { continue; }
        // A target that is gone (not merely detached, nor adopted away with
        // its wrapper) has nothing to run a callback on.
        if (!doc_->read().contains(reaction.target) &&
            adopted_away_.find(reaction.target.key()) == adopted_away_.end()) {
            continue;
        }
        const value wrapper = wrap(cx, reaction.target);
        if (!wrapper.is_object()) { continue; }
        using kind = custom_element_reaction::kind;
        if (reaction.what == kind::upgrade) {
            run_upgrade(cx, reaction.definition, reaction.target, wrapper);
            continue;
        }
        // COPIED OUT: a callback may define another element and grow the
        // vector under a reference.
        const custom_element_definition def = primary().custom_definitions_[reaction.definition];
        value callback = value::undefined();
        std::vector<value> args;
        switch (reaction.what) {
        case kind::upgrade: break;
        case kind::connected: callback = def.connected; break;
        case kind::disconnected: callback = def.disconnected; break;
        case kind::connected_move: callback = def.connected_move; break;
        case kind::adopted:
            callback = def.adopted;
            args.push_back(reaction.old_document);
            args.push_back(reaction.new_document);
            break;
        case kind::attribute_changed:
            callback = def.attribute_changed;
            args.push_back(cx.string(reaction.name));
            args.push_back(reaction.has_old ? cx.string(reaction.old_value) : value::null());
            args.push_back(reaction.has_new ? cx.string(reaction.new_value) : value::null());
            args.push_back(reaction.ns.empty() ? value::null() : cx.string(reaction.ns));
            break;
        case kind::form_associated:
            callback = def.form_associated_callback;
            args.push_back(reaction.form ? wrap(cx, reaction.form) : value::null());
            break;
        case kind::form_disabled:
            callback = def.form_disabled;
            args.push_back(value::boolean(reaction.flag));
            break;
        }
        if (!callback.is_callable()) { continue; }
        // FENCED, so one callback's throw is reported and the next still
        // runs: `call` from a native parks the throw and refuses every later
        // call from the same native, which is the opposite of what
        // with-exceptions.html measures.
        const context::rooted_values keep_args{cx, args};
        bool threw = false;
        value thrown = value::undefined();
        (void)cx.call_fenced(callback, args, wrapper, threw, thrown);
        if (threw) {
            report_custom_element_exception(cx, thrown, "custom element callback");
        } else {
            note_callback_fault("custom element");
        }
    }
    reaction_floors_.pop_back();
}

void dom_bindings::react_custom_elements() {
    if (primary().custom_definitions_.empty() || cx_ == nullptr || doc_ == nullptr) { return; }
    const std::size_t from = custom_reactions_.size();
    scan_custom_elements();
    flush_custom_element_reactions(from);
    // The adopting steps' reactions run in the ADOPTING document's turn, which
    // may have no mutation of its own coming (document.adoptNode alone).
    while (!adoptees_.empty()) {
        const auto [other, other_from] = adoptees_.back();
        adoptees_.pop_back();
        other->flush_custom_element_reactions(other_from);
    }
}

} // namespace ctbrowser::shell
