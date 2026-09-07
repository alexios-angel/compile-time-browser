// dom_bindings - MutationObserver, MutationRecord, and the snapshot diff behind
// them.
//
// WHY THIS IS A DIFF AND NOT A REPORT.
//
// The specification's algorithm is push: every mutating step of the DOM calls
// "queue a mutation record" with exactly what it changed, in the middle of
// changing it. This engine has no such step. What it has is
// `dom_bindings::mutated()` - one funnel that 23 natives call after they have
// finished changing the document, with no argument, because the funnel exists
// to tell the browser its pipeline is stale and that question has no operands.
//
// Rewriting 23 call sites to report their own mutations would be a much larger
// change than this one and would put the platform's bookkeeping inside every
// binding. So instead: `observe()` takes a SNAPSHOT of every observed node -
// its child list, its attributes, its text - and `record_mutations()` diffs
// that snapshot against the document as it is now, queues a record per
// difference, and re-snapshots. It runs from `mutated()`, so it runs once per
// mutating CALL, which is what makes `el.id = "a"; el.id = "b"` two records
// with the two different old values rather than one.
//
// THREE SHAPES A DIFF CANNOT RECOVER, named here rather than discovered later:
//
//   * A WRITE THAT CHANGES NOTHING. `setAttribute("class", theSameValue)`,
//     `appendData("")` and `deleteData(0, 0)` all queue a record in a browser
//     and are invisible to a diff. `MutationObserver-attributes.html` has four
//     such subtests and `MutationObserver-characterData.html` five.
//   * THE SILENT REMOVAL INSIDE `replaceChild`. Replacing a child with a node
//     that is ALREADY a child queues two records - the pre-removal from the old
//     position, then the replacement - and the net effect on the tree is one
//     deletion. A diff sees the deletion. Same for `replaceChild(x, x)`, whose
//     net effect on the tree is nothing at all and which queues two records.
//   * WHICH OF TWO SWAPPED SIBLINGS MOVED. `insertBefore(b, a)` over [a, b, c]
//     and "a was moved after b" produce the identical tree. The diff answers
//     with the longest common subsequence and a fixed tie-break, so exactly one
//     of `MutationObserver-childList.html`'s two swap subtests agrees; the
//     tree does not carry the information to settle the other.
//
// Everything else - what was added, what was removed, in what order, next to
// which siblings, with which old value - the diff reconstructs exactly, and the
// spec's own batching falls out of it: several mutations in one script turn
// leave several records on one queue and ONE delivery microtask.

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

// THE TWO PRIVATE SLOTS ON A MutationObserver INSTANCE, under names no author
// would write, non-enumerable and non-configurable - the pattern
// bindings/exceptions.cpp uses for a DOMException's name and message, and for
// the same reason: this engine has no internal-slot mechanism, and an own data
// property is the closest thing that is invisible to `Object.keys`, to
// `JSON.stringify` and to a `for...in`.
//
// The callback lives on the INSTANCE rather than in a C++ member because that
// makes one object the unit of liveness: whatever roots the observer roots its
// callback and its pending records, and nothing can be collected out from under
// the other two.
constexpr std::string_view observer_index_key = "__ctbrowser_mutation_index";
constexpr std::string_view observer_callback_key = "__ctbrowser_mutation_callback";
constexpr std::string_view observer_records_key = "__ctbrowser_mutation_records";

// Whether a dictionary member is PRESENT, which is not whether it is true:
// `{ attributeOldValue: false }` turns attribute observation ON, because the
// specification's step reads "if options["attributeOldValue"] EXISTS". Read as
// "is not undefined" rather than as a real [[HasProperty]], which is the same
// answer for every dictionary any page or any test writes.
[[nodiscard]] bool member_present(context & cx, value init, const std::string & name) {
    if (!init.is_object()) { return false; }
    return !cx.lookup_property(init, name).is_undefined();
}

[[nodiscard]] bool member_flag(context & cx, value init, const std::string & name) {
    if (!init.is_object()) { return false; }
    return context::truthy(cx.lookup_property(init, name));
}

// THE LONGEST COMMON SUBSEQUENCE of two child lists, as a keep-flag per entry
// of each. What is NOT kept and appears in both lists is a node that MOVED,
// which is the only way a diff can see a reordering at all - a set difference
// of [a,b,c] and [b,a,c] is empty, and a page that reorders children with
// appendChild would get no records whatsoever.
//
// Quadratic, so it is capped: past the cap nothing is reported as moved, which
// is the honest degradation - a missing pair of records rather than a frame
// spent on an O(n*m) table for a list of ten thousand children.
constexpr std::size_t lcs_cap = 4096;

void common_subsequence(const std::vector<node_id> & before, const std::vector<node_id> & after,
                        std::vector<char> & kept_before, std::vector<char> & kept_after) {
    kept_before.assign(before.size(), 0);
    kept_after.assign(after.size(), 0);
    if (before.empty() || after.empty()) { return; }
    if (before.size() * after.size() > lcs_cap) {
        // Past the cap: every node present in both lists counts as kept, so
        // nothing is reported as moved.
        for (std::size_t i = 0; i < before.size(); ++i) {
            kept_before[i] =
                static_cast<char>(std::ranges::find(after, before[i]) != after.end() ? 1 : 0);
        }
        for (std::size_t j = 0; j < after.size(); ++j) {
            kept_after[j] =
                static_cast<char>(std::ranges::find(before, after[j]) != before.end() ? 1 : 0);
        }
        return;
    }
    const std::size_t n = before.size();
    const std::size_t m = after.size();
    // Suffix table: table[i][j] is the LCS length of before[i..] and after[j..].
    std::vector<std::uint32_t> table((n + 1) * (m + 1), 0);
    const auto at = [m](std::size_t i, std::size_t j) { return i * (m + 1) + j; };
    for (std::size_t i = n; i-- > 0;) {
        for (std::size_t j = m; j-- > 0;) {
            table[at(i, j)] = before[i] == after[j]
                                  ? table[at(i + 1, j + 1)] + 1
                                  : std::max(table[at(i + 1, j)], table[at(i, j + 1)]);
        }
    }
    // Backtrack, preferring to advance the BEFORE cursor on a tie. The
    // preference is arbitrary and is what decides which of two swapped siblings
    // is called the moved one - see the header note.
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < n && j < m) {
        if (before[i] == after[j]) {
            kept_before[i] = 1;
            kept_after[j] = 1;
            ++i;
            ++j;
        } else if (table[at(i + 1, j)] >= table[at(i, j + 1)]) {
            ++i;
        } else {
            ++j;
        }
    }
}

} // namespace

// --- the plumbing -----------------------------------------------------------

std::size_t dom_bindings::mutation_observer_index(value v) const {
    if (!v.is_object()) { return std::numeric_limits<std::size_t>::max(); }
    const value * held =
        static_cast<script::object_object *>(v.as_heap())->find(std::string{observer_index_key});
    if (held == nullptr) { return std::numeric_limits<std::size_t>::max(); }
    const double index = context::to_number(*held);
    if (!(index >= 0) || index >= static_cast<double>(mutation_observers_.size())) {
        return std::numeric_limits<std::size_t>::max();
    }
    return static_cast<std::size_t>(index);
}

script::object_object * dom_bindings::mutation_observer_at(std::size_t index) {
    if (index >= mutation_observers_.size() || !mutation_observers_[index].is_object()) {
        return nullptr;
    }
    return static_cast<script::object_object *>(mutation_observers_[index].as_heap());
}

script::array_object * dom_bindings::mutation_records_of(std::size_t index) {
    script::object_object * observer = mutation_observer_at(index);
    if (observer == nullptr) { return nullptr; }
    value * held = observer->find(std::string{observer_records_key});
    if (held == nullptr || !held->is_array()) { return nullptr; }
    return static_cast<script::array_object *>(held->as_heap());
}

void dom_bindings::sync_mutation_roots() {
    if (mutation_interface_ == nullptr) { return; }
    // Rebuilt rather than appended to: the list is the whole root set, it is
    // never more than a handful of entries, and a list that only grows is a
    // leak that looks like a cache.
    mutation_interface_->retained.clear();
    mutation_interface_->retained.push_back(mutation_observer_prototype_);
    mutation_interface_->retained.push_back(mutation_record_prototype_);
    mutation_interface_->retained.push_back(mutation_trampoline_);
    // EVERY observer, not only the registered ones. An observer object carries
    // the C++ side's index and this vector holds its only other reference; an
    // entry the collector was allowed to free would be a dangling `value` the
    // next `disconnect()` or delivery walks. Pages hold single digits of these.
    for (const value & observer : mutation_observers_) {
        mutation_interface_->retained.push_back(observer);
    }
}

void dom_bindings::queue_mutation_record(std::size_t observer, value record) {
    script::array_object * queue = mutation_records_of(observer);
    if (queue == nullptr) { return; }
    queue->items.push_back(record);
}

void dom_bindings::queue_mutation_delivery() {
    if (mutation_delivery_queued_ || cx_ == nullptr || !mutation_trampoline_.is_callable()) {
        return;
    }
    mutation_delivery_queued_ = true;
    // A NATIVE TRAMPOLINE, not the callback itself. `queue_microtask` fixes the
    // arguments at QUEUE time and the record list has to stay open until the
    // microtask RUNS - that openness is exactly what makes several mutations in
    // one turn arrive as one callback with several records.
    cx_->queue_microtask(mutation_trampoline_);
}

void dom_bindings::deliver_mutation_records() {
    // Cleared FIRST. A callback may mutate the document, and the records that
    // produces belong to the NEXT microtask rather than to this one - which is
    // what `MutationObserver-nested-crash.html` observes from the inside.
    mutation_delivery_queued_ = false;
    if (cx_ == nullptr) { return; }
    // By index with the bound re-read: a callback may construct observers.
    for (std::size_t i = 0; i < mutation_observers_.size(); ++i) {
        script::object_object * observer = mutation_observer_at(i);
        script::array_object * queue = mutation_records_of(i);
        if (observer == nullptr || queue == nullptr || queue->items.empty()) { continue; }
        const value * callback = observer->find(std::string{observer_callback_key});
        const value records = value::object(queue);
        // The queue is EMPTIED BEFORE the callback runs, per "notify mutation
        // observers": a callback that mutates must not append to the list it
        // was handed.
        observer->define(observer_records_key, cx_->make_array(), script::attr_none);
        if (callback == nullptr || !callback->is_callable()) { continue; }
        const value self = mutation_observers_[i];
        const value args[2] = {records, self};
        // `this` IS THE OBSERVER, and so is the second argument.
        // MutationObserver-callback-arguments.html asserts both.
        (void)cx_->call(*callback, args, self);
        note_callback_fault("MutationObserver");
    }
    sync_mutation_roots();
}

// --- the snapshot -----------------------------------------------------------

void dom_bindings::collect_observed(const read_txn & txn, node_id root, bool subtree,
                                    std::vector<node_id> & into) const {
    if (!subtree) {
        into.push_back(root);
        return;
    }
    // Pre-order, which is document order, which is the order records come out
    // in. An explicit stack rather than recursion: a page can build a list ten
    // thousand deep, and a recursive walk of one is a stack overflow rather
    // than a slow frame. Children are pushed in REVERSE so the first one is on
    // top and comes out first.
    std::vector<node_id> pending{root};
    while (!pending.empty()) {
        const node_id at = pending.back();
        pending.pop_back();
        into.push_back(at);
        const std::span<const node_id> children = txn.children(at);
        for (std::size_t i = children.size(); i-- > 0;) { pending.push_back(children[i]); }
    }
}

void dom_bindings::take_mutation_snapshot() {
    mutation_snapshot_.clear();
    if (doc_ == nullptr || mutation_registrations_.empty()) { return; }
    const auto txn = doc_->read();
    std::vector<node_id> observed;
    for (const mutation_registration & reg : mutation_registrations_) {
        if (!reg.target) { continue; }
        observed.clear();
        collect_observed(txn, reg.target, reg.options.subtree, observed);
        for (const node_id at : observed) {
            const std::uint64_t key = pack(at);
            if (mutation_snapshot_.find(key) != mutation_snapshot_.end()) { continue; }
            mutation_node_state state;
            for (const node_id child : txn.children(at)) { state.children.push_back(child); }
            for (const attribute & held : txn.attributes(at)) {
                state.attributes.emplace_back(held.name, held.value);
            }
            state.text = std::string{txn.text(at)};
            mutation_snapshot_.emplace(key, std::move(state));
        }
    }
}

// --- the record -------------------------------------------------------------

value dom_bindings::make_mutation_record(context & cx, std::string_view type, node_id target) {
    auto * record = static_cast<script::object_object *>(cx.make_object().as_heap());
    // The prototype FIRST, so `records[0] instanceof MutationRecord` is true of
    // an object that is already complete.
    record->prototype = mutation_record_prototype_;
    record->set("type", cx.string(std::string{type}));
    record->set("target", wrap(cx, target));
    // REAL, EMPTY NODE LISTS. `assert_array_equals(record.addedNodes, [])` is in
    // every one of these tests and an absent property fails it before the test
    // gets to what it was about. Ordinary arrays rather than a live NodeList:
    // a record's lists are a snapshot by definition, and testharness's
    // assert_array_equals wants length and indices, which is all an array is.
    record->set("addedNodes", cx.make_array());
    record->set("removedNodes", cx.make_array());
    // ...and the absent fields are NULL, not undefined, which is what most of
    // these tests actually check.
    record->set("previousSibling", value::null());
    record->set("nextSibling", value::null());
    record->set("attributeName", value::null());
    // ALWAYS NULL, and it is a DOM-layer gap rather than a binding one:
    // `struct attribute` is (atom name, std::string value) with nowhere to put
    // a namespace, so `setAttributeNS` has none to report. See the attribute
    // namespaces row of docs/wpt.md.
    record->set("attributeNamespace", value::null());
    record->set("oldValue", value::null());
    return value::object(record);
}

// --- the diff ---------------------------------------------------------------

void dom_bindings::record_mutations() {
    // THE FAST PATH, and the reason a page that never constructs a
    // MutationObserver pays nothing for this file: `mutated()` runs on every
    // DOM write a script makes.
    if (mutation_registrations_.empty() || cx_ == nullptr || doc_ == nullptr) { return; }
    context & cx = *cx_;
    const auto txn = doc_->read();

    // ONE RECORD PER (observer, node, kind, attribute) FOR THIS MUTATION. An
    // observer that registered on both a node and an ancestor with `subtree`
    // is interested twice and gets one record, which is what the
    // specification's "interested observers" map means.
    struct emitted_key {
        std::size_t observer;
        std::uint64_t node;
        int kind;
        std::string name;
    };
    std::vector<emitted_key> emitted;
    const auto first_time = [&emitted](std::size_t observer, std::uint64_t node, int kind,
                                       const std::string & name) {
        for (const emitted_key & seen : emitted) {
            if (seen.observer == observer && seen.node == node && seen.kind == kind &&
                seen.name == name) {
                return false;
            }
        }
        emitted.push_back(emitted_key{observer, node, kind, name});
        return true;
    };

    bool anything = false;
    std::vector<node_id> observed;
    std::vector<char> kept_before;
    std::vector<char> kept_after;
    std::vector<node_id> now_children;

    for (const mutation_registration & reg : mutation_registrations_) {
        if (!reg.target) { continue; }
        observed.clear();
        collect_observed(txn, reg.target, reg.options.subtree, observed);
        for (const node_id at : observed) {
            const auto found = mutation_snapshot_.find(pack(at));
            // A node that was not in the snapshot is one that has only just
            // come under observation - a descendant appended a moment ago. It
            // has no "before", so it has no differences; the addition itself is
            // reported against its PARENT, which does have one.
            if (found == mutation_snapshot_.end()) { continue; }
            const mutation_node_state & was = found->second;
            const node_kind kind = txn.kind(at).value_or(node_kind::element);

            // --- childList ------------------------------------------------
            if (reg.options.child_list) {
                now_children.clear();
                for (const node_id child : txn.children(at)) { now_children.push_back(child); }
                if (now_children != was.children) {
                    common_subsequence(was.children, now_children, kept_before, kept_after);
                    const auto present_in = [](const std::vector<node_id> & list, node_id want) {
                        return std::ranges::find(list, want) != list.end();
                    };
                    // A node in the OLD list and not in the new one is gone; a
                    // node in both that the common subsequence did not keep
                    // MOVED, and a move is a removal followed by an insertion -
                    // two records, which is what the DOM queues for
                    // `insertBefore(existingChild, ...)`.
                    std::vector<std::size_t> gone;
                    std::vector<std::size_t> moved;
                    for (std::size_t i = 0; i < was.children.size(); ++i) {
                        if (!present_in(now_children, was.children[i])) {
                            gone.push_back(i);
                        } else if (kept_before[i] == 0) {
                            moved.push_back(i);
                        }
                    }
                    std::vector<std::size_t> added;
                    for (std::size_t j = 0; j < now_children.size(); ++j) {
                        if (!present_in(was.children, now_children[j]) || kept_after[j] == 0) {
                            added.push_back(j);
                        }
                    }
                    // The removal half of each move, with the siblings the node
                    // had WHERE IT WAS.
                    for (const std::size_t i : moved) {
                        // Keyed on the MOVED CHILD rather than on the parent:
                        // two children of one parent may move in one call, and
                        // a key that named only the parent would collapse both
                        // removals into one record.
                        if (!first_time(reg.observer, pack(was.children[i]), 0, std::string{})) {
                            continue;
                        }
                        const value record = make_mutation_record(cx, "childList", at);
                        auto * held = static_cast<script::object_object *>(record.as_heap());
                        const value list = cx.make_array();
                        static_cast<script::array_object *>(list.as_heap())
                            ->items.push_back(wrap(cx, was.children[i]));
                        held->set("removedNodes", list);
                        held->set("previousSibling",
                                  i > 0 ? wrap(cx, was.children[i - 1]) : value::null());
                        held->set("nextSibling", i + 1 < was.children.size()
                                                     ? wrap(cx, was.children[i + 1])
                                                     : value::null());
                        queue_mutation_record(reg.observer, record);
                        anything = true;
                    }
                    // ...and ONE record for everything else this call did to
                    // the child list. One rather than two because that is what
                    // "replace all" queues - `textContent = "x"` over existing
                    // children is a single record naming both lists, which
                    // MutationObserver-textContent.html asserts three ways.
                    if (!gone.empty() || !added.empty()) {
                        const bool fresh = first_time(reg.observer, pack(at), 1, std::string{});
                        if (fresh) {
                            const value record = make_mutation_record(cx, "childList", at);
                            auto * held = static_cast<script::object_object *>(record.as_heap());
                            const value removed_list = cx.make_array();
                            auto * removed_items =
                                static_cast<script::array_object *>(removed_list.as_heap());
                            for (const std::size_t i : gone) {
                                removed_items->items.push_back(wrap(cx, was.children[i]));
                            }
                            held->set("removedNodes", removed_list);
                            const value added_list = cx.make_array();
                            auto * added_items =
                                static_cast<script::array_object *>(added_list.as_heap());
                            for (const std::size_t j : added) {
                                added_items->items.push_back(wrap(cx, now_children[j]));
                            }
                            held->set("addedNodes", added_list);
                            // The siblings the record names are the ones the
                            // inserted run sits between, read from the list it
                            // sits in - the NEW list when anything was added,
                            // and otherwise the old one, where the surviving
                            // neighbours of the removed run are.
                            node_id previous;
                            node_id next;
                            if (!added.empty()) {
                                const auto is_added = [&added](std::size_t j) {
                                    return std::ranges::find(added, j) != added.end();
                                };
                                for (std::size_t j = added.front(); j-- > 0;) {
                                    if (!is_added(j)) {
                                        previous = now_children[j];
                                        break;
                                    }
                                }
                                for (std::size_t j = added.back() + 1; j < now_children.size();
                                     ++j) {
                                    if (!is_added(j)) {
                                        next = now_children[j];
                                        break;
                                    }
                                }
                            } else {
                                const auto survives = [&](std::size_t i) {
                                    return present_in(now_children, was.children[i]);
                                };
                                for (std::size_t i = gone.front(); i-- > 0;) {
                                    if (survives(i)) {
                                        previous = was.children[i];
                                        break;
                                    }
                                }
                                for (std::size_t i = gone.back() + 1; i < was.children.size();
                                     ++i) {
                                    if (survives(i)) {
                                        next = was.children[i];
                                        break;
                                    }
                                }
                            }
                            held->set("previousSibling", wrap(cx, previous));
                            held->set("nextSibling", wrap(cx, next));
                            queue_mutation_record(reg.observer, record);
                            anything = true;
                        }
                    }
                }
            }

            // --- attributes -----------------------------------------------
            if (reg.options.attributes && kind == node_kind::element) {
                const auto wanted = [&reg](std::string_view name) {
                    if (!reg.options.has_attribute_filter) { return true; }
                    return std::ranges::find(reg.options.attribute_filter, name) !=
                           reg.options.attribute_filter.end();
                };
                const auto report = [&](atom name, const std::string * old_value) {
                    const std::string spelling{atoms_->text(name)};
                    if (!wanted(spelling)) { return; }
                    if (!first_time(reg.observer, pack(at), 2, spelling)) { return; }
                    const value record = make_mutation_record(cx, "attributes", at);
                    auto * held = static_cast<script::object_object *>(record.as_heap());
                    held->set("attributeName", cx.string(spelling));
                    if (reg.options.attribute_old_value && old_value != nullptr) {
                        held->set("oldValue", cx.string(*old_value));
                    }
                    queue_mutation_record(reg.observer, record);
                    anything = true;
                };
                for (const attribute & held : txn.attributes(at)) {
                    const auto before = std::ranges::find_if(
                        was.attributes, [&](const auto & pair) { return pair.first == held.name; });
                    if (before == was.attributes.end()) {
                        // A NEW attribute: its old value is null even under
                        // attributeOldValue, there being no old value.
                        report(held.name, nullptr);
                    } else if (before->second != held.value) {
                        report(held.name, &before->second);
                    }
                }
                for (const auto & [name, old_value] : was.attributes) {
                    const bool still = std::ranges::any_of(
                        txn.attributes(at),
                        [name = name](const attribute & held) { return held.name == name; });
                    if (!still) { report(name, &old_value); }
                }
            }

            // --- characterData ---------------------------------------------
            if (reg.options.character_data &&
                (kind == node_kind::text || kind == node_kind::comment)) {
                const std::string_view now_text = txn.text(at);
                if (now_text != was.text && first_time(reg.observer, pack(at), 3, std::string{})) {
                    const value record = make_mutation_record(cx, "characterData", at);
                    auto * held = static_cast<script::object_object *>(record.as_heap());
                    if (reg.options.character_data_old_value) {
                        held->set("oldValue", cx.string(was.text));
                    }
                    queue_mutation_record(reg.observer, record);
                    anything = true;
                }
            }
        }
    }

    take_mutation_snapshot();
    if (anything) {
        queue_mutation_delivery();
        sync_mutation_roots();
    }
}

// --- the interface ----------------------------------------------------------

void dom_bindings::install_mutation_observer(context & cx) {
    // --- MutationRecord: an interface object with a prototype and no
    // --- constructor. `records[0] instanceof MutationRecord` is what
    // --- MutationObserver-callback-arguments.html asks, and a marker object
    // --- makes that silently false.
    auto * record_proto = static_cast<script::object_object *>(cx.make_object().as_heap());
    mutation_record_prototype_ = value::object(record_proto);
    auto * record_ctor =
        cx.allocate<script::native_object>("MutationRecord", [](context & c, std::span<value>) {
            c.throw_error("TypeError", "Illegal constructor");
            return value::undefined();
        });
    record_ctor->define("prototype", mutation_record_prototype_, script::attr_none);
    record_proto->define("constructor", value::object(record_ctor), script::attr_builtin);
    cx.define_global("MutationRecord", value::object(record_ctor));

    // --- MutationObserver.prototype ----------------------------------------
    auto * observer_proto = static_cast<script::object_object *>(cx.make_object().as_heap());
    mutation_observer_prototype_ = value::object(observer_proto);
    const auto method = [&cx, observer_proto](const char * name, script::native_fn fn) {
        observer_proto->define(
            name, value::object(cx.allocate<script::native_object>(name, std::move(fn))),
            script::attr_builtin);
    };

    method("observe", [this](context & c, std::span<value> args) {
        const std::size_t index = mutation_observer_index(c.current_this());
        if (index == std::numeric_limits<std::size_t>::max()) {
            c.throw_error("TypeError", "Illegal invocation");
            return value::undefined();
        }
        if (args.empty()) {
            c.throw_error("TypeError", "Failed to execute 'observe' on 'MutationObserver': 1 "
                                       "argument required, but only 0 present.");
            return value::undefined();
        }
        // WHICH NODE. An element wrapper carries its handle; the document
        // object carries none, this tree builder having no Document node above
        // `<html>`, so `observe(document, ...)` registers on the root element
        // and remembers that it stands for the document.
        node_id target = handle_of(args[0]);
        bool whole_document = false;
        // `is_object_like`, because `document` is a Proxy and `is_object()` is
        // false for one - see make_document_proxy in bindings/document.cpp.
        if (!target && args[0].is_object_like() && document_.is_object_like() &&
            args[0].bits() == document_.bits()) {
            target = doc_->root();
            whole_document = true;
        }
        if (!target) {
            c.throw_error("TypeError", "Failed to execute 'observe' on 'MutationObserver': "
                                       "parameter 1 is not of type 'Node'.");
            return value::undefined();
        }

        const value init = arg(args, 1);
        mutation_options options;
        options.child_list = member_flag(c, init, "childList");
        options.attributes = member_flag(c, init, "attributes");
        options.character_data = member_flag(c, init, "characterData");
        options.subtree = member_flag(c, init, "subtree");
        options.attribute_old_value = member_flag(c, init, "attributeOldValue");
        options.character_data_old_value = member_flag(c, init, "characterDataOldValue");
        const bool has_attributes = member_present(c, init, "attributes");
        const bool has_character_data = member_present(c, init, "characterData");
        const bool has_old_value = member_present(c, init, "attributeOldValue");
        const bool has_data_old_value = member_present(c, init, "characterDataOldValue");
        options.has_attribute_filter = member_present(c, init, "attributeFilter");
        if (options.has_attribute_filter) {
            const value filter = c.lookup_property(init, "attributeFilter");
            if (filter.is_array()) {
                for (const value & entry :
                     static_cast<script::array_object *>(filter.as_heap())->items) {
                    options.attribute_filter.push_back(c.to_string(entry));
                }
            } else if (filter.is_object()) {
                const double length = context::to_number(c.lookup_property(filter, "length"));
                for (double i = 0; i < length; ++i) {
                    options.attribute_filter.push_back(
                        c.to_string(c.lookup_property(filter, std::to_string(i))));
                }
            }
        }
        // THE DICTIONARY'S OWN DEFAULTING, DOM 4.3.1 steps 3-5, and it is
        // "exists" rather than "is true": `{ attributeOldValue: false }` turns
        // attribute observation ON, which MutationObserver-sanity.html tests
        // both ways round.
        if ((has_old_value || options.has_attribute_filter) && !has_attributes) {
            options.attributes = true;
        }
        if (has_data_old_value && !has_character_data) { options.character_data = true; }
        if (!options.child_list && !options.attributes && !options.character_data) {
            c.throw_error("TypeError", "Failed to execute 'observe' on 'MutationObserver': The "
                                       "options object must set at least one of 'attributes', "
                                       "'characterData', or 'childList' to true.");
            return value::undefined();
        }
        if (options.attribute_old_value && !options.attributes) {
            c.throw_error("TypeError",
                          "Failed to execute 'observe' on 'MutationObserver': The options object "
                          "may only set 'attributeOldValue' to true when 'attributes' is true or "
                          "not present.");
            return value::undefined();
        }
        if (options.has_attribute_filter && !options.attributes) {
            c.throw_error("TypeError",
                          "Failed to execute 'observe' on 'MutationObserver': The options object "
                          "may only set 'attributeFilter' when 'attributes' is true or not "
                          "present.");
            return value::undefined();
        }
        if (options.character_data_old_value && !options.character_data) {
            c.throw_error("TypeError",
                          "Failed to execute 'observe' on 'MutationObserver': The options object "
                          "may only set 'characterDataOldValue' to true when 'characterData' is "
                          "true or not present.");
            return value::undefined();
        }

        // OBSERVING THE SAME TARGET TWICE REPLACES THE REGISTRATION rather
        // than adding a second one - MutationObserver-disconnect.html
        // re-observes the same node three times and counts on getting one
        // record per change rather than three.
        const auto existing =
            std::ranges::find_if(mutation_registrations_, [&](const mutation_registration & reg) {
                return reg.observer == index && reg.target == target;
            });
        if (existing != mutation_registrations_.end()) {
            existing->options = std::move(options);
            existing->whole_document = whole_document;
        } else {
            mutation_registrations_.push_back(
                mutation_registration{index, target, whole_document, std::move(options)});
        }
        // THE SNAPSHOT IS TAKEN HERE, which is what makes the diff mean
        // "since you started observing" rather than "since the page loaded".
        take_mutation_snapshot();
        sync_mutation_roots();
        return value::undefined();
    });

    method("disconnect", [this](context & c, std::span<value>) {
        const std::size_t index = mutation_observer_index(c.current_this());
        if (index == std::numeric_limits<std::size_t>::max()) { return value::undefined(); }
        std::erase_if(mutation_registrations_,
                      [index](const mutation_registration & reg) { return reg.observer == index; });
        // ...AND THE QUEUE WITH THEM. `disconnect()` empties the record list,
        // which is the whole point of the second half of
        // MutationObserver-disconnect.html: mutations made before it are
        // discarded rather than delivered late.
        if (script::object_object * observer = mutation_observer_at(index)) {
            observer->define(observer_records_key, c.make_array(), script::attr_none);
        }
        take_mutation_snapshot();
        sync_mutation_roots();
        return value::undefined();
    });

    method("takeRecords", [this](context & c, std::span<value>) {
        const std::size_t index = mutation_observer_index(c.current_this());
        if (index == std::numeric_limits<std::size_t>::max()) { return c.make_array(); }
        script::array_object * queue = mutation_records_of(index);
        if (queue == nullptr) { return c.make_array(); }
        const value taken = value::object(queue);
        if (script::object_object * observer = mutation_observer_at(index)) {
            observer->define(observer_records_key, c.make_array(), script::attr_none);
        }
        sync_mutation_roots();
        return taken;
    });

    // --- the constructor ---------------------------------------------------
    auto * ctor = cx.allocate<script::native_object>(
        "MutationObserver", [this](context & c, std::span<value> args) -> value {
            const value self = c.current_this();
            // AN INTERFACE OBJECT IS NOT CALLABLE. `new` and `super()` arrive
            // with an object receiver; a plain call arrives with none - the
            // same test bindings/events.cpp makes for every Event interface.
            if (!self.is_object()) {
                c.throw_error("TypeError", "Failed to construct 'MutationObserver': please use the "
                                           "'new' operator.");
                return value::undefined();
            }
            if (args.empty() || !args[0].is_callable()) {
                c.throw_error("TypeError",
                              "Failed to construct 'MutationObserver': parameter 1 is not of type "
                              "'MutationCallback'.");
                return value::undefined();
            }
            auto * observer = static_cast<script::object_object *>(self.as_heap());
            // Only when it is missing: `class M extends MutationObserver`
            // arrives with M.prototype already in place and overwriting it
            // would flatten the subclass.
            if (!observer->prototype.is_object()) {
                observer->prototype = mutation_observer_prototype_;
            }
            const std::size_t index = mutation_observers_.size();
            mutation_observers_.push_back(self);
            observer->define(observer_index_key, value::number(static_cast<double>(index)),
                             script::attr_none);
            observer->define(observer_callback_key, args[0], script::attr_none);
            observer->define(observer_records_key, c.make_array(), script::attr_none);
            sync_mutation_roots();
            return self;
        });
    ctor->define("prototype", mutation_observer_prototype_, script::attr_none);
    observer_proto->define("constructor", value::object(ctor), script::attr_builtin);
    cx.define_global("MutationObserver", value::object(ctor));
    mutation_interface_ = ctor;

    // The delivery trampoline. It is not reachable from script and never
    // installed anywhere a page can see it; what keeps it alive is the
    // interface object's retained list, and what runs it is the microtask
    // queue.
    mutation_trampoline_ = value::object(cx.allocate<script::native_object>(
        "deliverMutationRecords", [this](context &, std::span<value>) {
            deliver_mutation_records();
            return value::undefined();
        }));
    sync_mutation_roots();
}

} // namespace ctbrowser::shell
