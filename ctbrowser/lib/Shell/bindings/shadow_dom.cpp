// dom_bindings - the parts of the shadow DOM that are not one element's own:
// slots and their assignments, `setHTMLUnsafe`/`getHTML`, and the declarative
// shadow roots the first of those attaches.
//
// `element.attachShadow` and the ShadowRoot's own members are in
// element/shadow.cpp, because they are installed per element and per root.
// What is here goes on the INTERFACE PROTOTYPES, once: `assignedNodes` is the
// same function for every <slot> there will ever be.
//
// WHAT AN ASSIGNMENT IS. DOM 4.2.2: a slottable - an element or a text node
// that is a CHILD OF A SHADOW HOST - has a name (an element's `slot`
// attribute, and always the empty string for text), and it is assigned to the
// FIRST slot in the host's shadow tree whose `name` matches. Nothing is
// stored: the assignment is a function of the two trees, so it is computed
// when it is asked for and cannot go stale the way a cached list would.
//
// `slotchange` (DOM 4.2.2.4) is found by DIFFING that answer: mutated()
// recomputes every slot's list and signals the ones that moved
// (signal_slot_changes), and the mutation observer microtask fires the
// events after the observers' callbacks (fire_signalled_slots).
//
// ponytail: nothing here reaches the FLAT TREE that layout would render: a
// slot's assigned nodes are an answer to a question, not a rearrangement of
// the boxes. See the report.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/dom/xml.hpp>
#include <ctbrowser/shell/bindings.hpp>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ctbrowser::shell {
namespace {

// A MANUAL ASSIGNMENT IS BETWEEN OBJECTS, not handles: HTML's slot.assign()
// records "manually assigned nodes" on the slot and a "manual slot
// assignment" on each node, and both survive the nodes moving to another
// document and back (imperative-slot-api.html) - where this engine's handles
// do not, since adoption copies a node into the other document's slab. So the
// two lists live on the WRAPPERS, which adoption rebinds but keeps, and
// which the collector traces through these properties.
constexpr std::string_view manual_nodes_key =
    CTBROWSER_SYMBOL_KEY("sym:ctbrowser:manuallyAssignedNodes");
constexpr std::string_view manual_slot_key =
    CTBROWSER_SYMBOL_KEY("sym:ctbrowser:manualSlotAssignment");

// The slot name an element asks for, and the one a slot offers. Text is always
// the default slot's.
[[nodiscard]] std::string slot_name_of(const read_txn & txn, atom_table & atoms, node_id id,
                                       std::string_view attribute) {
    if (!txn.tag(id)) { return {}; }
    return std::string{txn.attribute_value(id, atoms.intern(attribute))};
}

[[nodiscard]] bool is_slot(const read_txn & txn, node_id id) {
    return txn.tag(id) && txn.element_ns(id) == node_ns::html && txn.local_name(id) == "slot";
}

// The first <slot> of that name in a tree, in tree order - the only one an
// assignment can go to.
[[nodiscard]] node_id first_slot_named(const read_txn & txn, atom_table & atoms, node_id root,
                                       std::string_view name) {
    node_id found;
    const auto walk = [&](auto && self, node_id at) -> void {
        for (const node_id child : txn.children(at)) {
            if (found) { return; }
            if (is_slot(txn, child) && slot_name_of(txn, atoms, child, "name") == name) {
                found = child;
                return;
            }
            self(self, child);
        }
    };
    walk(walk, root);
    return found;
}

// THE SANITIZER, AS FAR AS THE HTML PARTIAL-UPDATE METHODS REACH IT (Sanitizer
// API "sanitize", run over the parsed fragment before it is inserted): the
// "remove unsafe" pass the SAFE spellings always make - script-running
// elements, event handler attributes and `javascript:` navigations - and a
// configuration's removeElements, replaceWithChildrenElements and
// removeAttributes lists, which both spellings honour.
// ponytail: the built-in safe DEFAULT configuration - an allow-list of every
// element and attribute a sanitised fragment may keep - is not modelled, so
// a safe call keeps an unknown element the default would drop; model it when
// setHTML() itself is built on this.
struct sanitize_names {
    // (namespace URI, local name); "" is the null namespace.
    std::vector<std::pair<std::string, std::string>> remove_elements;
    std::vector<std::pair<std::string, std::string>> unwrap_elements;
    std::vector<std::pair<std::string, std::string>> remove_attributes;
};

[[nodiscard]] std::string element_namespace_uri(const read_txn & txn, const atom_table & atoms,
                                                node_id id) {
    switch (txn.element_ns(id)) {
    case node_ns::html: return std::string{xhtml_namespace};
    case node_ns::svg: return std::string{svg_namespace};
    case node_ns::other: break;
    }
    return std::string{atoms.text(txn.element_namespace(id))};
}

// "Remove unsafe": the elements that run script or embed a browsing context,
// in the HTML and SVG namespaces, whatever a configuration says.
[[nodiscard]] bool unsafe_element(std::string_view ns, std::string_view local) {
    if (ns == xhtml_namespace) {
        return local == "script" || local == "frame" || local == "iframe" || local == "object" ||
               local == "embed";
    }
    return ns == svg_namespace && (local == "script" || local == "use");
}

void sanitize(document & doc, const atom_table & atoms, node_id at, const sanitize_names & names,
              bool safe) {
    const auto listed = [](const std::vector<std::pair<std::string, std::string>> & list,
                           std::string_view ns, std::string_view local) {
        return std::ranges::any_of(
            list, [&](const auto & one) { return one.first == ns && one.second == local; });
    };
    std::vector<node_id> kids;
    {
        const auto txn = doc.read();
        const std::span<const node_id> held = txn.children(at);
        kids.assign(held.begin(), held.end());
    }
    for (const node_id child : kids) {
        std::string ns;
        std::string local;
        {
            const auto txn = doc.read();
            if (!txn.tag(child)) { continue; }
            ns = element_namespace_uri(txn, atoms, child);
            local = std::string{txn.local_name(child)};
        }
        if ((safe && unsafe_element(ns, local)) || listed(names.remove_elements, ns, local)) {
            (void)doc.remove_child(child);
            continue;
        }
        // The contents of a <template> and of a declarative shadow root are
        // the fragment's too.
        if (const node_id contents = doc.template_content(child)) {
            sanitize(doc, atoms, contents, names, safe);
        }
        if (const node_id shadow = doc.shadow_root_of(child)) {
            sanitize(doc, atoms, shadow, names, safe);
        }
        sanitize(doc, atoms, child, names, safe);
        std::vector<atom> dropped;
        {
            const auto txn = doc.read();
            for (const attribute & a : txn.attributes(child)) {
                const std::string_view name = attribute_local_name(atoms, a);
                const std::string_view uri = atoms.text(a.ns);
                const std::string value = ascii_lower_copy(trim(a.value, html_whitespace));
                const bool handler = !a.ns && name.size() > 2 && name.starts_with("on");
                const bool navigation =
                    (name == "href" || name == "action" || name == "formaction") &&
                    value.starts_with("javascript:");
                if ((safe && (handler || navigation)) ||
                    listed(names.remove_attributes, uri, name)) {
                    dropped.push_back(a.name);
                }
            }
        }
        for (const atom name : dropped) { (void)doc.remove_attribute(child, name); }
        if (listed(names.unwrap_elements, ns, local)) {
            std::vector<node_id> inner;
            {
                const auto txn = doc.read();
                const std::span<const node_id> held = txn.children(child);
                inner.assign(held.begin(), held.end());
            }
            for (const node_id one : inner) { (void)doc.insert_before(at, one, child); }
            (void)doc.remove_child(child);
        }
    }
}

// A configuration's name lists, from the `sanitizer` member of the options:
// a string names an element in the HTML namespace or an attribute in none, a
// dictionary its `name` and `namespace`.
[[nodiscard]] sanitize_names sanitizer_names(context & cx, value config) {
    sanitize_names out;
    if (!config.is_object()) { return out; }
    const auto read = [&](const std::string & member, std::string_view default_ns,
                          std::vector<std::pair<std::string, std::string>> & into) {
        const value list = dict_member(cx, config, member);
        if (!list.is_array()) { return; }
        for (const value & item : static_cast<script::array_object *>(list.as_heap())->items) {
            if (item.is_string()) {
                into.emplace_back(std::string{default_ns}, cx.to_string(item));
                continue;
            }
            if (!item.is_object()) { continue; }
            const value given = dict_member(cx, item, "namespace");
            into.emplace_back(given.is_undefined() ? std::string{default_ns}
                              : given.is_null()    ? std::string{}
                                                   : cx.to_string(given),
                              dict_string(cx, item, "name"));
        }
    };
    read("removeElements", xhtml_namespace, out.remove_elements);
    read("replaceWithChildrenElements", xhtml_namespace, out.unwrap_elements);
    read("removeAttributes", {}, out.remove_attributes);
    return out;
}

} // namespace

// The nodes assigned to one slot, in tree order. Empty for a slot that is not
// in a shadow tree, for a slot a NAME has already been claimed for by an
// earlier one, and for every slot of a manual-assignment tree.
std::vector<node_id> dom_bindings::assigned_nodes_of(node_id slot) const {
    const auto txn = doc_->read();
    if (!is_slot(txn, slot)) { return {}; }
    const node_id root = root_of_tree(txn, slot, false);
    const document::shadow_tree * tree = shadow_tree_of(root);
    if (tree == nullptr) { return {}; }
    // MANUAL: the page said which nodes, and nothing else qualifies. A node
    // that has since left the host is dropped here rather than when it moved.
    if (tree->manual_slots) {
        const auto slot_wrapper = wrappers_.find(slot.key());
        if (slot_wrapper == wrappers_.end()) { return {}; }
        const value * held = slot_wrapper->second->find(manual_nodes_key);
        if (held == nullptr || !held->is_array()) { return {}; }
        std::vector<node_id> out;
        for (const value & one : static_cast<script::array_object *>(held->as_heap())->items) {
            if (!one.is_object()) { continue; }
            // A node of THIS document now: its wrapper's handle is one of ours.
            auto * obj = static_cast<script::object_object *>(one.as_heap());
            const value * handle = obj->find(handle_property);
            if (handle == nullptr) { continue; }
            const auto packed = static_cast<std::uint64_t>(context::to_number(*handle));
            if (const auto mine = wrappers_.find(packed);
                mine == wrappers_.end() || mine->second != obj) {
                continue;
            }
            const node_id id = unpack(packed);
            const node_kind kind = txn.kind(id).value_or(node_kind::comment);
            if (kind != node_kind::element && kind != node_kind::text) { continue; }
            if (txn.parent(id) == tree->host) { out.push_back(id); }
        }
        return out;
    }
    const std::string name = slot_name_of(txn, *atoms_, slot, "name");
    // FIRST SLOT OF THAT NAME WINS - a second <slot name=x> is assigned
    // nothing at all, which is what `slots-fallback.html` reads.
    if (first_slot_named(txn, *atoms_, root, name) != slot) { return {}; }
    std::vector<node_id> out;
    for (const node_id child : txn.children(tree->host)) {
        const node_kind kind = txn.kind(child).value_or(node_kind::comment);
        if (kind != node_kind::element && kind != node_kind::text) { continue; }
        if (slot_name_of(txn, *atoms_, child, "slot") == name) { out.push_back(child); }
    }
    return out;
}

// "Signal a slot change" for every slot whose assigned nodes moved since the
// last mutation: the slots of every shadow tree, each list recomputed and
// compared with the one kept. A slot new to the map with nothing assigned
// has not changed; one that has left its tree keeps the signal it earned.
void dom_bindings::signal_slot_changes(const std::vector<document::write_note> & writes) {
    if (doc_ == nullptr || cx_ == nullptr || !doc_->has_shadow_roots()) { return; }
    // ONLY THE TREES THE WRITES COULD HAVE MOVED AN ASSIGNMENT IN: a host's
    // child list (its shadow root's slots), a shadow tree's own content (a
    // slot inserted or removed, at any depth), a `slot` or `name` attribute
    // (the child's host's root, or the slot's tree) - and, for slot.assign(),
    // the root it named. A page with ten thousand hosts that appends one
    // element must not recompute ten thousand trees.
    std::vector<node_id> roots;
    // DOM 4.2.3 "insert" step 7.4 and "remove" step 11: a slot IN A SHADOW
    // TREE WITH NOTHING ASSIGNED whose children change has changed what it
    // shows - its fallback content - and that is a slot change too, though no
    // assignment moved (moveBefore/slotchange-events.html). Not a slot being
    // built outside one: a clone's children arrive before it is anywhere, and
    // the notes are read after the fact - so a slot that was itself inserted
    // by these writes, or inside something that was, got its children first.
    std::vector<node_id> fallback;
    // DOM 4.2.3 "remove" step 13: A SLOT THAT LEFT ITS SHADOW TREE - itself or
    // inside what was removed - is assigned nothing now, and one that had
    // nodes has changed (imperative-slot-api-slotchange.html). The trees
    // below are the ones it is still in; this is the one it left.
    std::vector<node_id> departed;
    const auto note_root = [&](node_id root) {
        if (root && shadow_tree_of(root) != nullptr &&
            std::ranges::find(roots, root) == roots.end()) {
            roots.push_back(root);
        }
    };
    {
        const auto txn = doc_->read();
        const atom slot_attr = atoms_->intern("slot");
        const atom name_attr = atoms_->intern("name");
        for (const document::write_note & note : writes) {
            using edit = document::write_note::edit;
            if (note.kind == edit::data || !note.node || !txn.contains(note.node)) { continue; }
            if (note.kind == edit::attribute && note.name != slot_attr && note.name != name_attr) {
                continue;
            }
            // The node's own tree, when it is a shadow tree...
            note_root(root_of_tree(txn, note.node, false));
            // ...and the shadow root of the host whose child it is, or is.
            note_root(shadow_root_of(note.node));
            if (const node_id parent = txn.parent(note.node)) { note_root(shadow_root_of(parent)); }
            if (note.kind == edit::attribute) { continue; }
            if (is_slot(txn, note.node) &&
                shadow_tree_of(root_of_tree(txn, note.node, false)) != nullptr &&
                std::ranges::find(fallback, note.node) == fallback.end()) {
                fallback.push_back(note.node);
            }
            if (note.child && txn.contains(note.child)) {
                note_root(root_of_tree(txn, note.child, false));
                note_root(shadow_root_of(note.child));
                if (note.kind == edit::removed) {
                    const auto gather = [&](auto && self, node_id at) -> void {
                        if (is_slot(txn, at)) { departed.push_back(at); }
                        for (const node_id child : txn.children(at)) { self(self, child); }
                    };
                    gather(gather, note.child);
                }
            }
        }
        for (const node_id root : slot_roots_dirty_) { note_root(root); }
        std::erase_if(fallback, [&](node_id slot) {
            for (node_id at = slot; at; at = txn.parent(at)) {
                for (const document::write_note & note : writes) {
                    if (note.kind == document::write_note::edit::inserted && note.child == at) {
                        return true;
                    }
                }
            }
            return false;
        });
    }
    slot_roots_dirty_.clear();
    bool any = false;
    for (const node_id slot : fallback) {
        if (!assigned_nodes_of(slot).empty() ||
            std::ranges::find(signal_slots_, slot) != signal_slots_.end()) {
            continue;
        }
        signal_slots_.push_back(slot);
        any = true;
    }
    for (const node_id slot : departed) {
        const auto before = slot_assignments_.find(slot.key());
        if (before == slot_assignments_.end() || before->second.empty() ||
            before->second == assigned_nodes_of(slot)) {
            continue;
        }
        slot_assignments_.insert_or_assign(slot.key(), assigned_nodes_of(slot));
        if (std::ranges::find(signal_slots_, slot) == signal_slots_.end()) {
            signal_slots_.push_back(slot);
            any = true;
        }
    }
    if (roots.empty()) {
        if (any) { queue_mutation_delivery(); }
        return;
    }
    flat_map<std::uint64_t, std::vector<node_id>> now;
    {
        const auto txn = doc_->read();
        const auto walk = [&](auto && self, node_id at) -> void {
            for (const node_id child : txn.children(at)) {
                if (is_slot(txn, child)) { now[child.key()] = assigned_nodes_of(child); }
                self(self, child);
            }
        };
        for (const node_id root : roots) { walk(walk, root); }
    }
    for (const auto & [key, assigned] : now) {
        const auto before = slot_assignments_.find(key);
        const bool moved =
            before == slot_assignments_.end() ? !assigned.empty() : before->second != assigned;
        slot_assignments_.insert_or_assign(key, assigned);
        if (!moved) { continue; }
        const node_id slot = unpack(key);
        if (std::ranges::find(signal_slots_, slot) == signal_slots_.end()) {
            signal_slots_.push_back(slot);
            any = true;
        }
    }
    if (any) { queue_mutation_delivery(); }
}

// The `slotchange` events of the signalled slots: bubbles, not cancelable,
// not composed, at each slot in the order they were signalled.
void dom_bindings::fire_signalled_slots() {
    if (cx_ == nullptr || signal_slots_.empty()) { return; }
    std::vector<node_id> due;
    due.swap(signal_slots_);
    for (const node_id slot : due) {
        if (!doc_->read().contains(slot)) { continue; }
        const value event = make_event(*cx_, "slotchange", slot);
        auto * object = static_cast<script::object_object *>(event.as_heap());
        object->set("bubbles", value::boolean(true));
        object->set("cancelable", value::boolean(false));
        (void)dispatch_event("slotchange", slot, event);
    }
}

// The <slot> a slottable is assigned to, mode-agnostic. Its host is the node's
// parent; the answer is the first slot in that host's shadow tree whose
// assigned-nodes list contains the node - the same question `assignedSlot`
// asks, without the open/closed gate the getter adds on top.
node_id dom_bindings::assigned_slot_of(node_id slottable) const {
    const auto txn = doc_->read();
    const node_id host = txn.parent(slottable);
    if (!host) { return node_id{}; }
    const node_id root = shadow_root_of(host);
    if (!root) { return node_id{}; }
    if (shadow_tree_of(root) == nullptr) { return node_id{}; }
    node_id answer;
    const auto walk = [&](auto && self, node_id at) -> void {
        for (const node_id child : txn.children(at)) {
            if (answer) { return; }
            if (is_slot(txn, child)) {
                for (const node_id one : assigned_nodes_of(child)) {
                    if (one == slottable) {
                        answer = child;
                        return;
                    }
                }
            }
            self(self, child);
        }
    };
    walk(walk, root);
    return answer;
}

// THE SHADOW TREES A COPY DOES NOT CARRY. `copy_subtree` copies nodes and
// attributes, and a shadow root is neither - it is a second tree the document
// remembers beside the host. The two trees have the same SHAPE, so this walks
// them in step and re-attaches each root on the copy.
void dom_bindings::copy_shadow_trees(const document & src, const read_txn & from, node_id source,
                                     node_id made) {
    if (const node_id root = src.shadow_root_of(source)) {
        if (const document::shadow_tree * how = src.shadow_tree_of(root)) {
            if (const auto mine = doc_->attach_shadow(made, *how)) {
                const std::span<const node_id> kids = from.children(root);
                const std::vector<node_id> sources{kids.begin(), kids.end()};
                for (const node_id child : sources) {
                    copy_shadow_trees(src, from, child, copy_subtree(from, child, *mine));
                }
            }
        }
    }
    const std::span<const node_id> kids = from.children(source);
    const std::vector<node_id> sources{kids.begin(), kids.end()};
    const std::span<const node_id> copied = doc_->read().children(made);
    const std::vector<node_id> destinations{copied.begin(), copied.end()};
    for (std::size_t i = 0; i < sources.size() && i < destinations.size(); ++i) {
        copy_shadow_trees(src, from, sources[i], destinations[i]);
    }
}

// `setHTMLUnsafe(markup, {runScripts})`: `innerHTML` PLUS the declarative
// shadow roots, HTML 8.6.1 "unsafely set HTML".
//
// The fragment is parsed into a SCRATCH document whose "allow declarative
// shadow roots" is on, so the tree builder attaches them there (see
// tree_builder::declarative_shadow_root) and the copy carries the shadow trees
// over. The context element is the element itself - or a shadow root's host -
// and the children go where it keeps them: a <template>'s are its CONTENTS.
//
// `runScripts` (whatwg/html#12495): the parsed <script>s are not marked
// already started, so each runs when it is connected - after the whole
// fragment is in, in shadow-including tree order, a script an earlier one
// removed not at all (the inserted-script batch in document/entry.cpp).
void dom_bindings::set_html_unsafe(node_id target, std::string_view markup, bool run_scripts) {
    if (!target || atoms_ == nullptr) { return; }
    node_id context = target;
    if (const shadow_tree * tree = shadow_tree_of(target)) { context = tree->host; }
    node_id into = target;
    bool is_template = false;
    {
        const auto txn = doc_->read();
        is_template =
            txn.element_ns(target) == node_ns::html && txn.local_name(target) == "template";
    }
    if (is_template) {
        into = doc_->template_content(target);
        if (!into) {
            into = doc_->create_fragment();
            doc_->set_template_content(target, into);
        }
    }
    {
        const auto txn = doc_->read();
        const std::span<const node_id> kids = txn.children(into);
        const std::vector<node_id> existing{kids.begin(), kids.end()};
        for (const node_id child : existing) { (void)doc_->remove_child(child); }
    }
    document scratch{*atoms_};
    // The fragment case with the ELEMENT as its context (HTML 12.4 step 5:
    // "with context element"), not a body's - `table.setHTMLUnsafe("<tr>")`
    // keeps its row - and the document's own scripting flag (none for a
    // template's contents, which are its inert document's).
    node_id body;
    {
        const auto txn = doc_->read();
        body =
            parse_html_fragment(scratch, markup, txn.local_name(context), txn.element_ns(context),
                                doc_->scripting() && !is_template, true);
    }
    const auto from = scratch.read();
    const std::span<const node_id> kids = from.children(body);
    const std::vector<node_id> sources{kids.begin(), kids.end()};
    for (const node_id child : sources) {
        copy_shadow_trees(scratch, from, child, copy_subtree(from, child, into));
    }
    {
        // The registries, as set_inner_html gives them - the declarative
        // shadow roots' included.
        const auto txn = doc_->read();
        custom_element_registry * inherited = registry_of_node(txn, target);
        if (is_template) { inherited = nullptr; }
        if (scratch.has_null_custom_element_registries() || scratch.has_shadow_roots() ||
            inherited != document_registry()) {
            for (const node_id child : txn.children(into)) {
                assign_parsed_registries(txn, child, inherited);
            }
        }
    }
    if (run_scripts) {
        std::vector<node_id> scripts;
        {
            const auto txn = doc_->read();
            const auto walk = [&](auto && self, node_id at) -> void {
                const node_ns ns = txn.element_ns(at);
                if (txn.tag(at) && (ns == node_ns::html || ns == node_ns::svg) &&
                    txn.local_name(at) == "script") {
                    scripts.push_back(at);
                }
                if (const node_id shadow = shadow_root_of(at)) { self(self, shadow); }
                for (const node_id child : txn.children(at)) { self(self, child); }
            };
            for (const node_id child : txn.children(into)) { walk(walk, child); }
        }
        for (const node_id script : scripts) { note_unstarted_script(script); }
    }
    mutated();
}

void dom_bindings::install_shadow_dom(context & cx) {
    // `Document.parseHTMLUnsafe(html)` and `Document.parseHTML(html,
    // options)`, HTML 8.6.1: a new document, parsed with scripting off and
    // "allow declarative shadow roots" on - the static twins of setHTMLUnsafe
    // and the safe spelling, on the Document interface object - whose URL is
    // about:blank. parseHTML sanitises the result as the safe partial
    // updates do (`sanitize`). (The interface object is a NATIVE, not a plain
    // object - `is_object()` is heap_kind::object exactly.)
    if (const value ctor = cx.global("Document"); ctor.is_kind(script::heap_kind::native)) {
        for (const bool safe : {false, true}) {
            set_method(
                cx, *static_cast<script::native_object *>(ctor.as_heap()),
                safe ? "parseHTML" : "parseHTMLUnsafe",
                [this, safe](context & c, std::span<value> a) {
                    const std::string markup = arg_string(c, a, 0);
                    dom_bindings & top = primary_ == nullptr ? *this : *primary_;
                    document & fresh =
                        *top.owned_documents_.emplace_back(std::make_unique<document>(*atoms_));
                    fresh.set_scripting(false);
                    fresh.set_allow_declarative_shadow_roots(true);
                    html::tree_builder builder{fresh, *atoms_};
                    builder.set_scripting(false);
                    (void)builder.parse(markup);
                    if (safe) {
                        sanitize(fresh, *atoms_, fresh.document_node(),
                                 sanitizer_names(c, dict_member(c, arg(a, 1), "sanitizer")), true);
                    }
                    dom_bindings & made = adopt_second_document(c, fresh);
                    made.install_document(c);
                    made.observe_location("about:blank", "");
                    return made.document_;
                });
        }
    }
    // --- HTMLSlotElement ------------------------------------------------------
    if (const value slot_proto = interface_prototype("HTMLSlotElement"); slot_proto.is_object()) {
        auto * on = static_cast<script::object_object *>(slot_proto.as_heap());
        // `assignedNodes({flatten})`: without the flag the assignment itself,
        // with it what would actually be RENDERED there - the slot's own
        // children when nothing is assigned, and a nested slot expanded.
        for (const bool elements_only : {false, true}) {
            set_method(
                cx, *on, elements_only ? "assignedElements" : "assignedNodes",
                [this, elements_only](context & c, std::span<value> args) {
                    const value made = c.make_array();
                    // THE SLOT'S OWN DOCUMENT answers - a frame's, or one a
                    // page made - since the method is on the shared prototype.
                    dom_bindings * owner = owner_of(c.current_this());
                    if (owner == nullptr) { return made; }
                    dom_bindings & d = *owner;
                    const node_id slot = d.handle_of(c.current_this());
                    if (!slot) { return made; }
                    auto & items = static_cast<script::array_object *>(made.as_heap())->items;
                    const bool flatten = dict_flag(c, arg(args, 0), "flatten");
                    // "Find flattened slottables", DOM 4.2.2.3: NOTHING for a
                    // slot outside a shadow tree, whose children are not
                    // fallback content of anything; the slot's own slottable
                    // children when nothing is assigned to it.
                    const auto in_shadow_tree = [&d](const read_txn & txn, node_id at) {
                        return d.shadow_tree_of(d.root_of_tree(txn, at, false)) != nullptr;
                    };
                    const auto gather = [&](auto && self, node_id at) -> void {
                        if (flatten && !in_shadow_tree(d.doc_->read(), at)) { return; }
                        std::vector<node_id> assigned = d.assigned_nodes_of(at);
                        if (flatten && assigned.empty()) {
                            const auto txn = d.doc_->read();
                            const std::span<const node_id> kids = txn.children(at);
                            assigned.assign(kids.begin(), kids.end());
                        }
                        for (const node_id one : assigned) {
                            const auto txn = d.doc_->read();
                            // A SLOT AMONG THEM IS ITS OWN ASSIGNMENT, flattened
                            // in its place - that is what makes the flat tree
                            // flat. Only one IN a shadow tree: a light-DOM
                            // <slot> assigned here is an ordinary slottable.
                            if (flatten && is_slot(txn, one) && in_shadow_tree(txn, one)) {
                                self(self, one);
                                continue;
                            }
                            const node_kind kind = txn.kind(one).value_or(node_kind::comment);
                            const bool wanted = elements_only ? kind == node_kind::element
                                                              : kind == node_kind::element ||
                                                                    kind == node_kind::text;
                            if (wanted) { items.push_back(d.wrap(c, one)); }
                        }
                    };
                    gather(gather, slot);
                    return made;
                },
                script::attr_builtin);
        }
        // `slot.assign(...nodes)`: VARIADIC, not a sequence, and it REPLACES
        // whatever was assigned before. It means nothing to a tree that
        // assigns by name - assigned_nodes_of only reads the list for a manual
        // one - and the nodes are remembered as given, because whether one
        // still qualifies is a question about where it is NOW. An ORDERED
        // SET: a node given twice keeps its first place.
        set_method(
            cx, *on, "assign",
            [this](context & c, std::span<value> args) {
                // THE SLOT MAY BE ANOTHER DOCUMENT'S - one a page moved away -
                // and its own bindings answer for it. Every wrapper below is
                // the ORDINARY object: a form's or a select's is behind the
                // proxy the page holds (control_methods/legacy_objects.cpp),
                // and the marks and the list are kept on that object.
                const value self = unwrap_legacy_proxy(c.current_this());
                dom_bindings * home = owner_of(self);
                const node_id slot = home == nullptr ? node_id{} : home->handle_of(self);
                if (!slot) { return value::undefined(); }
                std::vector<value> nodes;
                for (const value & given : args) {
                    const value one = unwrap_legacy_proxy(given);
                    // ONLY A SLOTTABLE, of any document: the argument type is
                    // `(Element or Text)...`, so anything else is a WebIDL
                    // conversion failure before one node is assigned.
                    dom_bindings * of = owner_of(one);
                    const node_id id = of == nullptr ? node_id{} : of->handle_of(one);
                    const node_kind kind =
                        id ? of->doc_->read().kind(id).value_or(node_kind::comment)
                           : node_kind::comment;
                    if (kind != node_kind::element && kind != node_kind::text) {
                        c.throw_error("TypeError",
                                      "Failed to execute 'assign' on 'HTMLSlotElement': the "
                                      "arguments must be Element or Text nodes.");
                        return value::undefined();
                    }
                    if (std::ranges::none_of(
                            nodes, [&](const value & v) { return v.bits() == one.bits(); })) {
                        nodes.push_back(one);
                    }
                }
                // Every document whose slots this changes recomputes them - and
                // reports a slot that lost a node as a slotchange too.
                std::vector<dom_bindings *> touched{home};
                const auto dirty = [&](value slot_value) {
                    dom_bindings * at = owner_of(slot_value);
                    const node_id id = at == nullptr ? node_id{} : at->handle_of(slot_value);
                    if (!id) { return; }
                    const auto txn = at->doc_->read();
                    at->slot_roots_dirty_.push_back(root_of_tree(txn, id, false));
                    if (std::ranges::find(touched, at) == touched.end()) { touched.push_back(at); }
                };
                auto * slot_object = static_cast<script::object_object *>(self.as_heap());
                const auto assignment_of = [](value node) -> value * {
                    return static_cast<script::object_object *>(node.as_heap())
                        ->find(manual_slot_key);
                };
                // Step 1: what this slot had loses its manual slot assignment.
                if (const value * had = slot_object->find(manual_nodes_key);
                    had != nullptr && had->is_array()) {
                    for (const value & node :
                         static_cast<script::array_object *>(had->as_heap())->items) {
                        if (value * mark = assignment_of(node);
                            mark != nullptr && mark->bits() == self.bits()) {
                            *mark = value::null();
                        }
                    }
                }
                // Step 3: each node leaves the slot it was manually assigned
                // to - in this tree, another, or another document - for this.
                for (const value & node : nodes) {
                    value * mark = assignment_of(node);
                    if (mark != nullptr && mark->is_object() && mark->bits() != self.bits()) {
                        auto * other = static_cast<script::object_object *>(mark->as_heap());
                        if (value * list = other->find(manual_nodes_key);
                            list != nullptr && list->is_array()) {
                            auto & items =
                                static_cast<script::array_object *>(list->as_heap())->items;
                            std::erase_if(items,
                                          [&](const value & v) { return v.bits() == node.bits(); });
                        }
                        dirty(*mark);
                    }
                    if (mark != nullptr) {
                        *mark = self;
                    } else {
                        static_cast<script::object_object *>(node.as_heap())
                            ->define(std::string{manual_slot_key}, self, script::attr_configurable);
                    }
                }
                // Step 4: this slot's manually assigned nodes, as given.
                const value list = c.make_array();
                static_cast<script::array_object *>(list.as_heap())->items = nodes;
                if (value * had = slot_object->find(manual_nodes_key)) {
                    *had = list;
                } else {
                    slot_object->define(std::string{manual_nodes_key}, list,
                                        script::attr_configurable);
                }
                dirty(self);
                for (dom_bindings * at : touched) { at->mutated(); }
                return value::undefined();
            },
            script::attr_builtin);
    }

    // --- Slottable.assignedSlot, on an Element and on a Text -------------------
    //
    // NULL FOR A CLOSED TREE. The assignment is the same either way - a closed
    // slot still renders what it was given - but a page outside the tree may
    // not learn which slot that was.
    for (const char * interface : {"Element", "Text"}) {
        const value proto = interface_prototype(interface);
        if (!proto.is_object()) { continue; }
        define_getter(cx, *static_cast<script::object_object *>(proto.as_heap()), "assignedSlot",
                      [this](context & c, std::span<value>) {
                          dom_bindings * owner = owner_of(c.current_this());
                          const node_id id =
                              owner == nullptr ? node_id{} : owner->handle_of(c.current_this());
                          if (!id) { return value::null(); }
                          // NULL FOR A CLOSED TREE, though the assignment is the
                          // same: assigned_slot_of answers mode-agnostically
                          // (the event path needs that), so the open gate is
                          // here.
                          const node_id host = owner->doc_->read().parent(id);
                          const node_id root = host ? owner->doc_->shadow_root_of(host) : node_id{};
                          const document::shadow_tree * tree = owner->shadow_tree_of(root);
                          if (tree == nullptr || !tree->open) { return value::null(); }
                          const node_id answer = owner->assigned_slot_of(id);
                          return answer ? owner->wrap(c, answer) : value::null();
                      });
    }

    // --- Element.shadowRoot, on the interface -----------------------------------
    //
    // The attribute is Element's, so `'shadowRoot' in Element.prototype` and
    // not on Node's or Text's (Element-interface-shadowRoot-attribute.html).
    // Each element wrapper carries the same answer as its own property
    // (element/views.cpp); this is the one the interface declares, with
    // WebIDL's brand check.
    if (const value proto = interface_prototype("Element"); proto.is_object()) {
        define_getter(cx, *static_cast<script::object_object *>(proto.as_heap()), "shadowRoot",
                      [this](context & c, std::span<value>) {
                          dom_bindings * owner = owner_of(c.current_this());
                          const node_id id =
                              owner == nullptr ? node_id{} : owner->handle_of(c.current_this());
                          if (!id || !owner->doc_->read().tag(id)) {
                              c.throw_error("TypeError", "Illegal invocation");
                              return value::undefined();
                          }
                          const node_id root = owner->shadow_root_of(id);
                          const document::shadow_tree * tree = owner->shadow_tree_of(root);
                          if (tree == nullptr || !tree->open) { return value::null(); }
                          return owner->wrap(c, root);
                      });
    }

    // --- HTMLTemplateElement's declarative shadow root attributes -------------
    //
    // HTML 4.12.3: `shadowRootMode` reflects `shadowrootmode` as an enumerated
    // attribute LIMITED TO KNOWN VALUES (open, closed; missing and invalid are
    // ""), `shadowRootSlotAssignment` likewise with named/manual and "named"
    // for missing and invalid, and the other three are boolean attributes.
    // What the tree builder READS when it meets the template - these only
    // write the markup, as a reflected attribute does.
    if (const value template_proto = interface_prototype("HTMLTemplateElement");
        template_proto.is_object()) {
        auto * on = static_cast<script::object_object *>(template_proto.as_heap());
        const auto element_of = [this](context & c) -> std::pair<dom_bindings *, node_id> {
            dom_bindings * owner = owner_of(c.current_this());
            return {owner, owner == nullptr ? node_id{} : owner->handle_of(c.current_this())};
        };
        const auto enumerated = [&](const char * idl, const char * content,
                                    std::initializer_list<std::string_view> keywords,
                                    std::string_view fallback) {
            const std::vector<std::string_view> known{keywords};
            const std::string attribute_name{content};
            on->define_accessor(
                idl,
                native(cx, idl,
                       [this, element_of, known, fallback, attribute_name](context & c,
                                                                           std::span<value>) {
                           const auto [owner, id] = element_of(c);
                           if (!id) { return c.string(std::string{fallback}); }
                           const std::string_view held = owner->doc_->read().attribute_value(
                               id, atoms_->intern(attribute_name));
                           for (const std::string_view keyword : known) {
                               if (ascii_iequals(held, keyword)) {
                                   return c.string(std::string{keyword});
                               }
                           }
                           return c.string(std::string{fallback});
                       }),
                native(cx, idl,
                       [this, element_of, attribute_name](context & c, std::span<value> a) {
                           const auto [owner, id] = element_of(c);
                           if (!id) { return value::undefined(); }
                           const std::string text = arg_string(c, a, 0);
                           (void)owner->doc_->set_attribute(id, atoms_->intern(attribute_name),
                                                            text);
                           owner->mutated();
                           return value::undefined();
                       }),
                script::attr_enumerable | script::attr_configurable);
        };
        enumerated("shadowRootMode", "shadowrootmode", {"open", "closed"}, "");
        enumerated("shadowRootSlotAssignment", "shadowrootslotassignment", {"named", "manual"},
                   "named");
        // `shadowRootCustomElementRegistry` reflects as a plain DOMString -
        // its presence is what the parser reads, not its value.
        on->define_accessor(
            "shadowRootCustomElementRegistry",
            native(cx, "shadowRootCustomElementRegistry",
                   [this, element_of](context & c, std::span<value>) {
                       const auto [owner, id] = element_of(c);
                       if (!id) { return c.string(std::string{}); }
                       return c.string(std::string{owner->doc_->read().attribute_value(
                           id, atoms_->intern("shadowrootcustomelementregistry"))});
                   }),
            native(cx, "shadowRootCustomElementRegistry",
                   [this, element_of](context & c, std::span<value> a) {
                       const auto [owner, id] = element_of(c);
                       if (!id) { return value::undefined(); }
                       (void)owner->doc_->set_attribute(
                           id, atoms_->intern("shadowrootcustomelementregistry"),
                           arg_string(c, a, 0));
                       owner->mutated();
                       return value::undefined();
                   }),
            script::attr_enumerable | script::attr_configurable);
        for (const auto & [idl, content] :
             std::initializer_list<std::pair<const char *, const char *>>{
                 {"shadowRootDelegatesFocus", "shadowrootdelegatesfocus"},
                 {"shadowRootClonable", "shadowrootclonable"},
                 {"shadowRootSerializable", "shadowrootserializable"}}) {
            const std::string attribute_name{content};
            on->define_accessor(
                idl,
                native(cx, idl,
                       [this, element_of, attribute_name](context & c, std::span<value>) {
                           const auto [owner, id] = element_of(c);
                           return value::boolean(id && owner->doc_->read().has_attribute(
                                                           id, atoms_->intern(attribute_name)));
                       }),
                native(cx, idl,
                       [this, element_of, attribute_name](context & c, std::span<value> a) {
                           const auto [owner, id] = element_of(c);
                           if (!id) { return value::undefined(); }
                           const atom name = atoms_->intern(attribute_name);
                           if (context::truthy(arg(a, 0))) {
                               (void)owner->doc_->set_attribute(id, name, "");
                           } else {
                               (void)owner->doc_->remove_attribute(id, name);
                           }
                           owner->mutated();
                           return value::undefined();
                       }),
                script::attr_enumerable | script::attr_configurable);
        }
    }

    // --- ShadowRoot.activeElement, DocumentOrShadowRoot (HTML 6.6.3) -----------
    //
    // The focused element RETARGETED AGAINST THIS ROOT - so a focus inside a
    // tree nested in this one answers with that tree's host - and null unless
    // the result is in this root's own tree: focus outside it, or on an
    // element that has left the document, is not this tree's to report.
    if (const value root_proto = interface_prototype("ShadowRoot"); root_proto.is_object()) {
        define_getter(cx, *static_cast<script::object_object *>(root_proto.as_heap()),
                      "activeElement", [this](context & c, std::span<value>) {
                          const node_id root = handle_of(c.current_this());
                          if (!root || shadow_tree_of(root) == nullptr || !focused_ ||
                              !is_connected(focused_)) {
                              return value::null();
                          }
                          const auto txn = doc_->read();
                          // DOM 4.4 "retarget": out of every tree whose root is not a
                          // shadow-including inclusive ancestor of this root.
                          const auto holds = [&](node_id ancestor) {
                              for (node_id b = root; b;) {
                                  if (b == ancestor) { return true; }
                                  const node_id up = txn.parent(b);
                                  const shadow_tree * above = up ? nullptr : shadow_tree_of(b);
                                  b = up ? up : above == nullptr ? node_id{} : above->host;
                              }
                              return false;
                          };
                          node_id candidate = focused_;
                          for (int guard = 0; candidate && guard < 64; ++guard) {
                              const node_id own = root_of_tree(txn, candidate, false);
                              const shadow_tree * tree = shadow_tree_of(own);
                              if (tree == nullptr || holds(own)) { break; }
                              candidate = tree->host;
                          }
                          return root_of_tree(txn, candidate, false) == root ? wrap(c, candidate)
                                                                             : value::null();
                      });
        // --- elementFromPoint / elementsFromPoint, DocumentOrShadowRoot ------
        //
        // The document's hit test, retargeted against THIS root rather than
        // the document (geometry.cpp, elements_from_point): what is in this
        // tree answers as itself, a host inside it for anything deeper. The
        // coordinates are `double`s, and a non-finite one is a TypeError.
        auto & shadow_proto = *static_cast<script::object_object *>(root_proto.as_heap());
        for (const bool all : {false, true}) {
            const char * name = all ? "elementsFromPoint" : "elementFromPoint";
            set_method(
                cx, shadow_proto, name, [this, name, all](context & c, std::span<value> args) {
                    dom_bindings * owner = owner_of(c.current_this());
                    const node_id root =
                        owner == nullptr ? node_id{} : owner->handle_of(c.current_this());
                    if (owner == nullptr || owner->shadow_tree_of(root) == nullptr) {
                        c.throw_error("TypeError", "Illegal invocation");
                        return value::undefined();
                    }
                    const double x = args.empty() ? std::nan("") : context::to_number(args[0]);
                    const double y = args.size() < 2 ? std::nan("") : context::to_number(args[1]);
                    if (!std::isfinite(x) || !std::isfinite(y)) {
                        c.throw_error("TypeError",
                                      std::string{name} + ": the coordinates must be finite");
                        return value::undefined();
                    }
                    const std::vector<node_id> found = owner->elements_from_point(x, y, all, root);
                    if (!all) {
                        return found.empty() ? value::null() : owner->wrap(c, found.front());
                    }
                    const value out = c.make_array();
                    auto * items = static_cast<script::array_object *>(out.as_heap());
                    for (const node_id id : found) { items->items.push_back(owner->wrap(c, id)); }
                    return out;
                });
        }
    }

    // --- getHTML / setHTMLUnsafe, on an Element and on a ShadowRoot ------------
    //
    // `setHTMLUnsafe` is `innerHTML` PLUS the declarative shadow roots: the
    // markup is parsed the same way and then every `<template shadowrootmode>`
    // in it becomes a real shadow root. That is the only difference, and it is
    // why the safe spelling leaves the templates where they are.
    for (const char * interface : {"Element", "ShadowRoot"}) {
        const value proto = interface_prototype(interface);
        if (!proto.is_object()) { continue; }
        auto * on = static_cast<script::object_object *>(proto.as_heap());
        set_method(
            cx, *on, "setHTMLUnsafe",
            [this](context & c, std::span<value> args) {
                // THE RECEIVER'S DOCUMENT parses and owns the result - a frame's
                // element, or one a page made in another document, is not a
                // node of these bindings.
                dom_bindings * owner = owner_of(c.current_this());
                if (owner == nullptr) { return value::undefined(); }
                if (const node_id id = owner->handle_of(c.current_this())) {
                    owner->set_html_unsafe(id, arg_string(c, args, 0),
                                           dict_flag(c, arg(args, 1), "runScripts"));
                }
                return value::undefined();
            },
            script::attr_builtin);
        // `getHTML({serializableShadowRoots, shadowRoots})` - HTML fragment
        // serialisation with a "serializable shadow roots" set: a shadow root
        // is written out as its `<template shadowrootmode>` when it is in the
        // `shadowRoots` list, or when `serializableShadowRoots` is true and the
        // root's own `serializable` is set. With neither it is exactly
        // `innerHTML`.
        set_method(
            cx, *on, "getHTML",
            [this](context & c, std::span<value> args) {
                dom_bindings * owner = owner_of(c.current_this());
                const node_id id =
                    owner == nullptr ? node_id{} : owner->handle_of(c.current_this());
                if (!id) { return c.string(std::string{}); }
                bool serializable = false;
                std::vector<node_id> roots;
                const value opts = args.empty() ? value::undefined() : args[0];
                if (opts.is_object()) {
                    serializable =
                        context::truthy(c.lookup_property(opts, "serializableShadowRoots"));
                    const value list = c.lookup_property(opts, "shadowRoots");
                    if (list.is_array()) {
                        for (const value & item :
                             static_cast<script::array_object *>(list.as_heap())->items) {
                            if (const node_id r = owner->handle_of(item)) { roots.push_back(r); }
                        }
                    }
                }
                return c.string(owner->serialize_html(id, false, serializable, roots));
            },
            script::attr_builtin);
    }

    // --- {append,prepend,before,after,replaceWith}HTML{,Unsafe} ---------------
    //
    // THE HTML PARTIAL UPDATES (WICG declarative-partial-updates): markup
    // parsed as a fragment in the context of the element it lands in, then
    // inserted where the method's name says - the ParentNode positions on an
    // Element or ShadowRoot (a <template>'s go into its contents), the
    // ChildNode ones on anything with a parent. The context is that parent,
    // or the host when the parent is a shadow root or a template's contents;
    // a Document, a plain DocumentFragment and a <template> ELEMENT give no
    // context and are a HierarchyRequestError. Declarative shadow roots are
    // attached, the safe spellings are sanitised (see `sanitize`), and only
    // an Unsafe one with `{runScripts: true}` runs the scripts it inserts.
    const auto partial_update = [this](context & c, std::span<value> args, std::string_view where,
                                       bool safe) -> value {
        dom_bindings * owner = owner_of(c.current_this());
        const node_id self = owner == nullptr ? node_id{} : owner->handle_of(c.current_this());
        if (!self) {
            c.throw_error("TypeError", "Illegal invocation");
            return value::undefined();
        }
        const std::string markup = arg_string(c, args, 0);
        const value options = arg(args, 1);
        document & doc = *owner->doc_;
        const bool into_self = where == "append" || where == "prepend";
        node_id parent;
        node_id context;
        node_id before;
        bool refused = false;
        {
            const auto txn = doc.read();
            const auto is_template = [&](node_id n) {
                return txn.tag(n) && txn.element_ns(n) == node_ns::html &&
                       txn.local_name(n) == "template";
            };
            if (into_self) {
                parent = context = self;
                if (const shadow_tree * tree = owner->shadow_tree_of(self)) {
                    context = tree->host;
                } else if (is_template(self)) {
                    parent = node_id{}; // its contents, made below
                }
            } else {
                parent = dom_parent(txn, self);
                if (!parent) { return value::undefined(); }
                const node_kind kind = txn.kind(parent).value_or(node_kind::document);
                if (kind == node_kind::document) {
                    refused = true;
                } else if (kind == node_kind::document_fragment) {
                    if (const shadow_tree * tree = owner->shadow_tree_of(parent)) {
                        context = tree->host;
                    } else {
                        context = doc.template_of(parent);
                        refused = !context;
                    }
                } else if (is_template(parent)) {
                    refused = true;
                } else {
                    context = parent;
                }
                // After the node - and for replaceWith too, the node itself
                // going before the markup comes in.
                if (where == "before") {
                    before = self;
                } else {
                    const std::span<const node_id> row = txn.children(parent);
                    const auto here = std::ranges::find(row, self);
                    before = here != row.end() && here + 1 != row.end() ? *(here + 1) : node_id{};
                }
            }
        }
        if (refused) {
            throw_dom_exception(c, "HierarchyRequestError",
                                std::string{where} + "HTML: the node's parent gives the markup "
                                                     "no context element");
            return value::undefined();
        }
        if (!parent) {
            parent = doc.template_content(self);
            if (!parent) {
                parent = doc.create_fragment();
                doc.set_template_content(self, parent);
            }
        }
        if (into_self && where == "prepend") {
            const auto txn = doc.read();
            const std::span<const node_id> row = txn.children(parent);
            before = row.empty() ? node_id{} : row.front();
        }
        const bool run_scripts = !safe && dict_flag(c, options, "runScripts");
        const sanitize_names names = sanitizer_names(c, dict_member(c, options, "sanitizer"));
        document scratch{*atoms_};
        node_id body;
        {
            const auto txn = doc.read();
            body = parse_html_fragment(scratch, markup, txn.local_name(context),
                                       txn.element_ns(context), doc.scripting(), true);
        }
        sanitize(scratch, *atoms_, body, names, safe);
        const node_id holder = doc.create_fragment();
        {
            const auto from = scratch.read();
            const std::span<const node_id> kids = from.children(body);
            const std::vector<node_id> sources{kids.begin(), kids.end()};
            for (const node_id child : sources) {
                owner->copy_shadow_trees(scratch, from, child,
                                         owner->copy_subtree(from, child, holder));
            }
        }
        {
            // The parent's registry, as set_inner_html gives the context's.
            const auto txn = doc.read();
            custom_element_registry * inherited = owner->registry_of_node(txn, parent);
            if (scratch.has_null_custom_element_registries() || scratch.has_shadow_roots() ||
                inherited != owner->document_registry()) {
                for (const node_id child : txn.children(holder)) {
                    owner->assign_parsed_registries(txn, child, inherited);
                }
            }
        }
        if (run_scripts) {
            std::vector<node_id> scripts;
            {
                const auto txn = doc.read();
                const auto walk = [&](auto && walk_self, node_id at) -> void {
                    const node_ns ns = txn.element_ns(at);
                    if (txn.tag(at) && (ns == node_ns::html || ns == node_ns::svg) &&
                        txn.local_name(at) == "script") {
                        scripts.push_back(at);
                    }
                    if (const node_id shadow = doc.shadow_root_of(at)) {
                        walk_self(walk_self, shadow);
                    }
                    for (const node_id child : txn.children(at)) { walk_self(walk_self, child); }
                };
                walk(walk, holder);
            }
            for (const node_id script : scripts) { owner->note_unstarted_script(script); }
        }
        if (where == "replaceWith") { (void)doc.remove_child(self); }
        (void)owner->insert_node(parent, holder, before);
        return value::undefined();
    };
    const auto install_positions = [&](const char * interface,
                                       std::initializer_list<std::string_view> positions) {
        const value proto = interface_prototype(interface);
        if (!proto.is_object()) { return; }
        auto * on = static_cast<script::object_object *>(proto.as_heap());
        for (const std::string_view where : positions) {
            for (const bool safe : {true, false}) {
                const std::string name = std::string{where} + (safe ? "HTML" : "HTMLUnsafe");
                set_method(
                    cx, *on, name.c_str(),
                    [partial_update, where, safe](context & c, std::span<value> args) {
                        return partial_update(c, args, where, safe);
                    },
                    script::attr_builtin);
            }
        }
    };
    install_positions("Element", {"append", "prepend", "before", "after", "replaceWith"});
    install_positions("ShadowRoot", {"append", "prepend"});
    install_positions("CharacterData", {"before", "after", "replaceWith"});
    install_positions("DocumentType", {"before", "after", "replaceWith"});
    // `customElementRegistry` on Element, ShadowRoot and Document - the
    // prototypes exist from here (custom_elements.cpp).
    install_registry_accessors(cx);
}

} // namespace ctbrowser::shell
