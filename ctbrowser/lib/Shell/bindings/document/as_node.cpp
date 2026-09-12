// dom_bindings - the document as a Node: the twenty-two members of the Node and
// ParentNode surface on an object that has no Document node behind it.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// ============================================================================
// THE DOCUMENT AS A NODE
// ============================================================================
//
// THE STRUCTURAL FACT EVERYTHING BELOW IS BUILT AROUND: `txn.root()` IS THE
// DOCUMENT ELEMENT, not a Document node. `tree_builder` makes `<html>` and
// calls `set_root` with it, every walk in the style, layout and paint engines
// starts there, and `document` is a plain script object carrying no handle at
// all - `handle_of(document)` is the same empty handle it answers for a number.
// That is why none of this could be shared with the element bindings: those
// all start from `receiver(cx)`, and the document has nothing for `receiver`
// to find.
//
// THE CHILD LIST IS REAL, THOUGH. `document::document_node()` is a Document
// node whose children are what `document.childNodes` is - the doctype, the
// document element, and any comment or processing instruction beside them, in
// document order. The document element sits in that list WITH NO PARENT
// POINTER, so that the engine's parent walks still end on it (see
// document_node for the reasoning); the others carry the Document node as
// their parent. So every "is this a child of the document" below is asked of
// the LIST through `is_document_child`, and every insertion of an element goes
// through `set_document_element` rather than `append_child`.
//
// WHAT THE MODEL CANNOT DO, said here rather than guessed at each call site:
//
//   * THE PAGE'S `documentElement` CANNOT BE DETACHED. `document::remove_child`
//     refuses the root - `dom_error::is_root` - because a tree whose root is
//     gone has nothing left to lay out. So `removeChild(documentElement)` and
//     `replaceChildren()` are NotSupportedError on the page's own document;
//     `replaceChild(el, documentElement)` works because the new element
//     takes the slot. A document a page MADE is laid out by nothing, and
//     does all three through `document::remove_document_element`.
//   * A node from ANOTHER document is adopted on the way in, by `node_from`,
//     and checked afterwards - see may_become_a_child.
//
// `Node-lookupNamespaceURI.html`'s twelve document subtests and
// `Document-createAttribute.html`'s HTML half are answered from here, plus
// every page that reads one of these members without a guard.

namespace {

// The DOCUMENT_POSITION_* bits, DOM 4.4. Named because `20` at the one place
// they are combined says nothing and `contained_by | following` says all of it.
constexpr unsigned position_disconnected = 0x01;
constexpr unsigned position_preceding = 0x02;
constexpr unsigned position_following = 0x04;
constexpr unsigned position_contains = 0x08;
constexpr unsigned position_contained_by = 0x10;
constexpr unsigned position_implementation_specific = 0x20;

} // namespace

bool dom_bindings::is_the_document(value v) const {
    // `is_object_like` and NOT `is_object`: what a page holds as `document` is
    // a Proxy - see `make_document_proxy` - and `is_object()` is false for one.
    // The comparison is still pure identity; only the guard changed.
    return v.is_object_like() && document_.is_object_like() && v.bits() == document_.bits();
}

// DOM 4.4, "locate a namespace", run at an ELEMENT and walked up its ancestors.
std::string dom_bindings::locate_namespace(node_id element, const std::string * prefix) {
    // `xml` AND `xmlns` ARE BOUND AT EVERY ELEMENT, with no declaration saying
    // so: the two prefixes are reserved and their namespaces are fixed.
    // `Node-lookupNamespaceURI.html` asserts exactly this on an element that
    // carries neither declaration, so it cannot be derived from the tree.
    // AT EVERY ELEMENT - a DocumentFragment or a parentless text node has
    // none, and "locate a namespace" answers null for it before any prefix
    // is looked at.
    if (!element || atoms_ == nullptr || doc_ == nullptr) { return {}; }
    if (prefix != nullptr && *prefix == "xml") { return std::string{xml_namespace}; }
    if (prefix != nullptr && *prefix == "xmlns") { return std::string{xmlns_namespace}; }
    // THE DECLARATION IS READ OFF THE QUALIFIED NAME, not off an attribute's
    // namespace. `struct attribute` is `(atom name, std::string value)` and has
    // nowhere to put a namespace - see docs/wpt.md's handoff table - so
    // `xmlns` and `xmlns:<prefix>` as WRITTEN are the whole of the evidence.
    // That is exactly what the HTML parser stores and what `setAttribute` and
    // `setAttributeNS` both store, so all three routes read the same.
    const atom declaration = prefix == nullptr ? atoms_->intern_lower("xmlns")
                                               : atoms_->intern_lower("xmlns:" + *prefix);
    // READ THE WHOLE CHAIN OUT FIRST. `namespace_of` opens a read_txn of its
    // own, and a read nested inside another read is a shape nothing else in
    // these bindings has.
    struct step {
        node_id id;
        std::string own_prefix;
        bool declared = false;
        std::string declared_value;
    };
    std::vector<step> chain;
    {
        const auto txn = doc_->read();
        for (node_id at = element; at; at = txn.parent(at)) {
            if (txn.kind(at).value_or(node_kind::element) != node_kind::element) { break; }
            step one;
            one.id = at;
            one.own_prefix =
                std::string{split_qualified(atoms_->text(txn.tag(at).value_or(atom{}))).prefix};
            one.declared = txn.has_attribute(at, declaration);
            if (one.declared) {
                one.declared_value = std::string{txn.attribute_value(at, declaration)};
            }
            chain.push_back(std::move(one));
        }
    }
    for (const step & at : chain) {
        // 1. "If element's namespace is non-null and element's prefix is
        //    prefix, return element's namespace." This comes FIRST, which is
        //    what makes `document.lookupNamespaceURI(null)` the XHTML namespace
        //    on a page whose <html> also carries an `xmlns` attribute.
        const std::string ns = namespace_of(at.id);
        const bool prefix_matches =
            prefix == nullptr ? at.own_prefix.empty() : at.own_prefix == *prefix;
        if (!ns.empty() && prefix_matches) { return ns; }
        // 2/3. A declaration ON THIS ELEMENT terminates the walk even when its
        //      value is empty - "return its value, and null otherwise" returns
        //      either way, so an `xmlns=""` really does undeclare the default.
        if (at.declared) { return at.declared_value; }
    }
    return {};
}

// DOM 4.4, "locate a namespace prefix". The mirror of the above, and it reads
// the declarations off the qualified name for the same reason.
std::string dom_bindings::locate_namespace_prefix(node_id element, const std::string & ns) {
    if (!element || ns.empty() || atoms_ == nullptr || doc_ == nullptr) { return {}; }
    struct step {
        node_id id;
        std::string own_prefix;
        std::vector<std::pair<std::string, std::string>> declarations;
    };
    std::vector<step> chain;
    {
        const auto txn = doc_->read();
        for (node_id at = element; at; at = txn.parent(at)) {
            if (txn.kind(at).value_or(node_kind::element) != node_kind::element) { break; }
            step one;
            one.id = at;
            one.own_prefix =
                std::string{split_qualified(atoms_->text(txn.tag(at).value_or(atom{}))).prefix};
            for (const attribute & held : txn.attributes(at)) {
                const std::string_view name = atoms_->text(held.name);
                if (!name.starts_with("xmlns:")) { continue; }
                one.declarations.emplace_back(std::string{name.substr(6)}, held.value);
            }
            chain.push_back(std::move(one));
        }
    }
    for (const step & at : chain) {
        if (!at.own_prefix.empty() && namespace_of(at.id) == ns) { return at.own_prefix; }
        for (const auto & [declared, uri] : at.declarations) {
            if (uri == ns) { return declared; }
        }
    }
    return {};
}

// `normalize()`: drop empty Text children and merge adjacent ones, over a whole
// subtree. Decided entirely from a snapshot and applied afterwards - removing a
// child while holding the read_txn whose `children()` span is being walked is
// the use-after-free `set_text` above already carries a comment about.
void dom_bindings::normalize_subtree(node_id root) {
    if (!root || doc_ == nullptr) { return; }
    struct child_info {
        node_id id;
        node_kind kind = node_kind::element;
        std::string text;
    };
    std::vector<child_info> kids;
    {
        const auto txn = doc_->read();
        for (const node_id child : txn.children(root)) {
            child_info one;
            one.id = child;
            one.kind = txn.kind(child).value_or(node_kind::element);
            if (one.kind == node_kind::text) { one.text = std::string{txn.text(child)}; }
            kids.push_back(std::move(one));
        }
    }
    std::vector<node_id> doomed;
    std::vector<std::pair<node_id, std::string>> rewritten;
    std::vector<node_id> descend;
    node_id run;
    std::string joined;
    bool merged = false;
    const auto flush = [&] {
        // Only when the run actually absorbed something: rewriting a lone text
        // node with its own text is a mutation nobody asked for, and `mutated()`
        // makes every one of those a restyle.
        if (run && merged) { rewritten.emplace_back(run, joined); }
        run = node_id{};
        joined.clear();
        merged = false;
    };
    for (const child_info & child : kids) {
        if (child.kind == node_kind::text) {
            // "Remove any exclusive Text node whose length is zero" - which
            // happens before the merging, so an empty node between two others
            // does not stop them being joined.
            if (child.text.empty()) {
                doomed.push_back(child.id);
                continue;
            }
            if (run) {
                joined += child.text;
                merged = true;
                doomed.push_back(child.id);
            } else {
                run = child.id;
                joined = child.text;
            }
            continue;
        }
        flush();
        if (child.kind == node_kind::element) { descend.push_back(child.id); }
    }
    flush();
    // ONE `mutated()` PER STEP, because an observer counts them: DOM 4.4
    // "normalize" replaces the run's data and then removes each absorbed node
    // in turn, and MutationObserver-childList.html expects a record apiece.
    for (const auto & [id, text] : rewritten) {
        (void)doc_->set_text(id, text);
        mutated();
    }
    for (const node_id id : doomed) {
        (void)doc_->remove_child(id);
        mutated();
    }
    for (const node_id id : descend) { normalize_subtree(id); }
}

void dom_bindings::install_document_as_node(context & cx, script::object_object & doc) {
    const auto method = [&](std::string name, script::native_fn fn) {
        auto * native = cx.allocate<script::native_object>(name, std::move(fn));
        // `length` IS READ: dom/nodes' pre-insertion-validation-hierarchy.js
        // passes an explicit null reference child only when the method says it
        // takes two arguments, and the two that do have to say so.
        if (name == "insertBefore" || name == "replaceChild") {
            native->define("length", value::number(2), script::attr_configurable);
        }
        doc.set(name, value::object(native));
    };
    // READ-ONLY, not a data property. `document.textContent = "x"` and
    // `document.firstChild = x` are both defined to do nothing, and a data
    // property gets that backwards in the worst direction - the assignment
    // sticks and the document reports a lie for the rest of the page's life.
    // The same trap `document.head` was in before it became an accessor.
    const auto read_only = [&](std::string name, script::native_fn getter) {
        const value fn = value::object(cx.allocate<script::native_object>(name, std::move(getter)));
        doc.define_accessor(name, fn, value::undefined());
    };
    // THE DOCUMENT'S ONE ELEMENT CHILD, by the same route the `documentElement`
    // accessor takes, so the two cannot name different nodes: the ROOT, when
    // it is an element. Not `find_by_tag("html")` - `createDocument(null,
    // "foo")` has a root called `foo`, and `new Document()` has none at all.
    const auto element_child = [this] {
        const auto txn = doc_->read();
        const node_id root = txn.root();
        return txn.kind(root).value_or(node_kind::document) == node_kind::element ? root
                                                                                  : node_id{};
    };

    // --- the constants ----------------------------------------------------
    //
    // A Document inherits these from `Node.prototype` in a browser. There is no
    // Node interface object here to inherit from - it is a rung of its own in
    // docs/wpt.md's handoff - so they are own properties, which is what
    // `document.DOCUMENT_POSITION_CONTAINED_BY` needs to answer at all.
    for (const auto & [name, bits] : std::initializer_list<std::pair<const char *, double>>{
             {"ELEMENT_NODE", 1},
             {"ATTRIBUTE_NODE", 2},
             {"TEXT_NODE", 3},
             {"CDATA_SECTION_NODE", 4},
             {"ENTITY_REFERENCE_NODE", 5},
             {"ENTITY_NODE", 6},
             {"PROCESSING_INSTRUCTION_NODE", 7},
             {"COMMENT_NODE", 8},
             {"DOCUMENT_NODE", 9},
             {"DOCUMENT_TYPE_NODE", 10},
             {"DOCUMENT_FRAGMENT_NODE", 11},
             {"NOTATION_NODE", 12},
             {"DOCUMENT_POSITION_DISCONNECTED", position_disconnected},
             {"DOCUMENT_POSITION_PRECEDING", position_preceding},
             {"DOCUMENT_POSITION_FOLLOWING", position_following},
             {"DOCUMENT_POSITION_CONTAINS", position_contains},
             {"DOCUMENT_POSITION_CONTAINED_BY", position_contained_by},
             {"DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC", position_implementation_specific}}) {
        doc.set(name, value::number(bits));
    }

    // --- where the document sits ------------------------------------------

    // A Document is ALWAYS connected: "connected" means the root is a document,
    // and a document is its own root. Never changes, so a data property is the
    // whole of it.
    doc.set("isConnected", value::boolean(true));
    // A Document has no parent and no siblings, and null is not undefined: a
    // page walking up with `while (n.parentNode) n = n.parentNode` terminates on
    // one and loops forever on the other.
    for (const char * name : {"parentNode", "parentElement", "previousSibling", "nextSibling"}) {
        doc.set(name, value::null());
    }
    // NULL FOR A DOCUMENT, per the table in DOM 4.4 - not "". The distinction is
    // the whole of `Node-textContent.html`'s document section, and `""` would
    // tell a page the document is empty.
    read_only("textContent", [](context &, std::span<value>) { return value::null(); });
    // `getRootNode()` - a Document's root is itself. The `composed` option is
    // ACCEPTED AND IGNORED, which is the right answer rather than a shortcut:
    // composed asks for the shadow-including root and there are no shadow trees,
    // so the two answers are the same one.
    method("getRootNode", [this](context &, std::span<value>) { return document_; });

    // THE CHILD LIST - see the block comment at the top of this file.
    const auto children_now = [this] {
        const auto txn = doc_->read();
        const std::span<const node_id> kids = txn.children(txn.document_node());
        return std::vector<node_id>{kids.begin(), kids.end()};
    };
    // A live NodeList, the same one every read - see the element's in
    // element/views.cpp.
    read_only("childNodes", [this, children_now, self = &doc](context & c, std::span<value>) {
        constexpr std::string_view key = "@@sym:ctbrowser:childNodes";
        if (const value * held = self->find(key); held != nullptr) { return *held; }
        const value list = make_live_collection(c, children_now, "NodeList");
        self->define(key, list, script::attr_none);
        return list;
    });
    read_only("firstChild", [this, children_now](context & c, std::span<value>) {
        const std::vector<node_id> kids = children_now();
        return kids.empty() ? value::null() : wrap(c, kids.front());
    });
    read_only("lastChild", [this, children_now](context & c, std::span<value>) {
        const std::vector<node_id> kids = children_now();
        return kids.empty() ? value::null() : wrap(c, kids.back());
    });
    method("hasChildNodes", [children_now](context &, std::span<value>) {
        return value::boolean(!children_now().empty());
    });
    // IS THIS NODE IN THE DOCUMENT? Up to the top of its tree, then the
    // question is whether that top is a child of the Document - the document
    // element, or a doctype/comment/PI whose parent IS the Document node.
    const auto in_document = [this](node_id other) {
        const auto txn = doc_->read();
        node_id top = other;
        while (const node_id up = txn.parent(top)) {
            if (up == txn.document_node()) { return true; }
            top = up;
        }
        return is_document_child(txn, top);
    };

    // --- the two questions about the tree that a page actually asks --------

    // `contains(other)` is the INCLUSIVE-descendant test, and the Document
    // contains everything in the page including itself. A NULL argument is
    // FALSE and not a throw: the argument is a nullable Node, which is why
    // `Node-contains.html` opens with `assert_false(reference.contains(null))`
    // for all twenty-three of its nodes.
    method("contains", [this, in_document](context &, std::span<value> args) {
        const value given = arg(args, 0);
        if (is_the_document(given)) { return value::boolean(true); }
        const node_id other = handle_of(given);
        return value::boolean(other && in_document(other));
    });

    // `compareDocumentPosition(other)`, as the real bitmask.
    //
    // The Document is first in document order and contains the whole tree, so
    // of the six results only three can ever come back here: 0 for itself,
    // CONTAINED_BY|FOLLOWING for anything in the page, and the disconnected
    // triple for a node that has been created or removed. CONTAINS and a bare
    // PRECEDING are unreachable BY CONSTRUCTION rather than unimplemented -
    // nothing is an ancestor of the document and nothing precedes it.
    method("compareDocumentPosition", [this, in_document](context & c, std::span<value> args) {
        const value given = arg(args, 0);
        if (is_the_document(given)) { return value::number(0); }
        const node_id other = handle_of(given);
        if (!other) {
            if (const unsigned foreign = foreign_document_position(given); foreign != 0) {
                return value::number(foreign);
            }
            // A non-nullable Node in the IDL, so anything else fails argument
            // conversion before the method runs - a TypeError, not a 0 that
            // says "these are the same node".
            c.throw_error("TypeError", "compareDocumentPosition: the argument is not a Node");
            return value::undefined();
        }
        if (in_document(other)) {
            return value::number(static_cast<double>(position_contained_by | position_following));
        }
        // DISCONNECTED, where the specification asks only that the direction be
        // CONSISTENT - it is explicitly implementation-defined, which is what
        // the IMPLEMENTATION_SPECIFIC bit is announcing. The document is first
        // in every order this engine could pick, so a disconnected node always
        // FOLLOWS it, and the answer is the same every time it is asked.
        return value::number(static_cast<double>(
            position_disconnected | position_implementation_specific | position_following));
    });

    // --- namespaces -------------------------------------------------------
    //
    // On a Document all three are defined as "run the element algorithm on
    // documentElement", which is why a document with no documentElement answers
    // null to every one of them.
    method("lookupNamespaceURI", [this, element_child](context & c, std::span<value> args) {
        const value given = arg(args, 0);
        // "If prefix is the empty string, then set it to null." The null prefix
        // is what asks for the DEFAULT namespace, so "" and null are one case.
        const std::string prefix = given.is_nullish() ? std::string{} : c.to_string(given);
        const std::string found =
            locate_namespace(element_child(), prefix.empty() ? nullptr : &prefix);
        return found.empty() ? value::null() : c.string(found);
    });
    method("isDefaultNamespace", [this, element_child](context & c, std::span<value> args) {
        const value given = arg(args, 0);
        const std::string want = given.is_nullish() ? std::string{} : c.to_string(given);
        // The default namespace is what the null prefix locates, and the
        // comparison is against the empty string for null - so a document whose
        // <html> is in the XHTML namespace answers false to `null` and `""`,
        // which is four of this file's twelve document subtests.
        return value::boolean(locate_namespace(element_child(), nullptr) == want);
    });
    method("lookupPrefix", [this, element_child](context & c, std::span<value> args) {
        const value given = arg(args, 0);
        if (given.is_nullish()) { return value::null(); }
        const std::string ns = c.to_string(given);
        const std::string found = locate_namespace_prefix(element_child(), ns);
        return found.empty() ? value::null() : c.string(found);
    });

    // --- the nodes a document can make ------------------------------------

    // `document.createAttribute(localName)`.
    //
    // THE NAME CHECK IS WORTH MORE THAN THE OBJECT. See
    // `is_valid_attribute_name` for what the rule actually is and for the
    // thirteen names the corpus requires it to ACCEPT - it is the third of this
    // engine's three name rules and the loosest of them.
    method("createAttribute", [this](context & c, std::span<value> args) {
        // A DOMString, so `createAttribute(null)` asks for an attribute called
        // "null" and `createAttribute(undefined)` for one called "undefined".
        // The corpus checks both, beside "title" and "TITLE".
        const std::string given = args.empty() ? std::string{"undefined"} : c.to_string(args[0]);
        if (!is_valid_attribute_name(given)) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "createAttribute: '" + given + "' is not a valid attribute name");
            return value::undefined();
        }
        // "If this is an HTML document, then set localName to localName in
        // ASCII lowercase" - and an XML one keeps `createAttribute("TITLE")`
        // as written.
        const std::string local = doc_->xml() ? given : ascii_lower_copy(given);
        return attribute_object(c, node_id{}, attribute{atoms_->intern(local), std::string{}});
    });
    // `document.createAttributeNS(namespace, qualifiedName)`. The same shape as
    // `createElementNS` above and deliberately the same order: the NAME is
    // validated before the namespace is looked at, so a bad name in the XMLNS
    // namespace is an InvalidCharacterError rather than the NamespaceError its
    // namespace would otherwise earn. NOT lowercased - only createAttribute is.
    method("createAttributeNS", [this](context & c, std::span<value> args) {
        const value given = arg(args, 0);
        const std::string ns = given.is_nullish() ? std::string{} : c.to_string(given);
        const std::string qualified =
            args.size() > 1 ? c.to_string(args[1]) : std::string{"undefined"};
        const qualified_name split = split_qualified(qualified);
        const bool prefix_writable =
            !split.prefix.empty() &&
            split.prefix.find_first_of(attribute_name_breaks) == std::string_view::npos;
        if ((split.has_colon && !prefix_writable) || !is_valid_attribute_name(split.local)) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "createAttributeNS: '" + qualified + "' is not a qualified name");
            return value::undefined();
        }
        const auto fail = [this, &c](const std::string & why) {
            throw_dom_exception(c, "NamespaceError", "createAttributeNS: " + why);
            return value::undefined();
        };
        if (split.has_colon && ns.empty()) { return fail("a prefix needs a namespace"); }
        if (split.prefix == "xml" && ns != xml_namespace) {
            return fail("the xml prefix belongs to the XML namespace");
        }
        if ((qualified == "xmlns" || split.prefix == "xmlns") && ns != xmlns_namespace) {
            return fail("xmlns belongs to the XMLNS namespace");
        }
        if (ns == xmlns_namespace && qualified != "xmlns" && split.prefix != "xmlns") {
            return fail("the XMLNS namespace is only for xmlns");
        }
        return attribute_object(
            c, node_id{}, attribute{atoms_->intern(qualified), atoms_->intern(ns), std::string{}});
    });

    // --- everything that would change the document's own child list --------

    // DOM 4.2.3 "ensure pre-insertion validity", steps 4 to 6, FOR A DOCUMENT
    // PARENT - the half of that algorithm the element bindings' pre_insert_valid
    // never reaches, because it has no Document node to be the parent. Answers
    // false HAVING ALREADY THROWN, the same shape pre_insert_valid uses.
    //
    // `before` is the reference child, `ignore` a child that is about to be
    // replaced and so does not count: "other than child" in every clause of the
    // replace variant, DOM 4.2.4.
    const auto may_become_a_child = [this](context & c, value given, node_id before,
                                           node_id ignore) {
        node_id id = handle_of(given);
        if (!id) {
            if (is_a_document(given)) {
                throw_dom_exception(c, "HierarchyRequestError", "a Document cannot be inserted");
                return false;
            }
            // ANOTHER DOCUMENT'S NODE is adopted first and checked second -
            // see node_from. ponytail: the specification checks first, so a
            // refused insertion here leaves the node adopted and detached
            // rather than where it was.
            if (owner_of(given) != nullptr) { id = node_from(c, given); }
        }
        if (!id) {
            // `node_from` turns anything that is not a wrapper into a Text
            // node, and a Document may never have a Text child. Refused BEFORE
            // the node is created rather than after, so a rejected
            // `document.append('text')` leaves nothing behind in the slab.
            throw_dom_exception(c, "HierarchyRequestError", "a Document cannot have a Text child");
            return false;
        }
        const auto txn = doc_->read();
        const node_kind kind = txn.kind(id).value_or(node_kind::element);
        // What the document holds now, `ignore` excepted, and where `before`
        // sits among it: an element AHEAD of the reference, a doctype AT OR
        // AFTER it.
        bool has_element = false;
        bool has_doctype = false;
        bool element_precedes_before = false;
        bool doctype_at_or_after_before = false;
        bool passed_before = false;
        for (const node_id child : txn.children(txn.document_node())) {
            if (child == ignore) { continue; }
            if (child == before) { passed_before = true; }
            const node_kind held = txn.kind(child).value_or(node_kind::comment);
            if (held == node_kind::element) {
                has_element = true;
                if (before && !passed_before) { element_precedes_before = true; }
            }
            if (held == node_kind::document_type) {
                has_doctype = true;
                if (before && passed_before) { doctype_at_or_after_before = true; }
            }
        }
        const auto refuse = [&](const char * why) {
            throw_dom_exception(c, "HierarchyRequestError", why);
            return false;
        };
        switch (kind) {
        case node_kind::text:
        case node_kind::cdata_section: return refuse("a Document cannot have a Text child");
        case node_kind::document: return refuse("a Document cannot be inserted");
        case node_kind::document_fragment: {
            std::size_t elements = 0;
            for (const node_id child : txn.children(id)) {
                const node_kind held = txn.kind(child).value_or(node_kind::comment);
                if (is_text_kind(held)) { return refuse("a Document cannot have a Text child"); }
                if (held == node_kind::element) { ++elements; }
            }
            if (elements > 1) { return refuse("a Document may have at most one element child"); }
            if (elements == 1 && (has_element || doctype_at_or_after_before)) {
                return refuse("a Document may have at most one element child, after its doctype");
            }
            return true;
        }
        case node_kind::element:
            if (has_element || doctype_at_or_after_before) {
                return refuse("a Document may have at most one element child, after its doctype");
            }
            return true;
        case node_kind::document_type:
            if (has_doctype || (before ? element_precedes_before : has_element)) {
                return refuse("a Document may have one doctype, ahead of its element");
            }
            return true;
        case node_kind::comment:
        case node_kind::processing_instruction: return true;
        }
        return true;
    };

    // PUT IT IN, having passed the check above. An element becomes the
    // document element - `set_document_element` gives it the slot without a
    // parent pointer, see the top of this file; a fragment's children move one
    // by one; everything else is an ordinary child of the Document node.
    const auto place = [this](node_id fresh, node_id before) {
        std::vector<node_id> moving;
        {
            const auto txn = doc_->read();
            if (txn.kind(fresh).value_or(node_kind::element) == node_kind::document_fragment) {
                const std::span<const node_id> kids = txn.children(fresh);
                moving.assign(kids.begin(), kids.end());
            } else {
                moving.push_back(fresh);
            }
        }
        for (const node_id one : moving) {
            if (doc_->read().kind(one).value_or(node_kind::comment) == node_kind::element) {
                doc_->set_document_element(one, before);
            } else if (before) {
                (void)doc_->insert_before(doc_->document_node(), one, before);
            } else {
                (void)doc_->append_child(doc_->document_node(), one);
            }
        }
        mutated();
    };
    // "If child is non-null and its parent is not parent, throw a
    // NotFoundError" - DOM 4.2.3 step 3, and it comes BEFORE the check on what
    // is being inserted. Fills in the reference child; false having thrown.
    const auto reference_child = [this](context & c, value ref, node_id & out) {
        out = node_id{};
        if (ref.is_nullish()) { return true; }
        out = handle_of(ref);
        // ANOTHER DOCUMENT'S NODE is a Node that is not a child of this one -
        // a NotFoundError, not a TypeError - and the specification checks it
        // (step 3) before it looks at what is being inserted.
        if (!out && owner_of(ref) == nullptr && !is_a_document(ref)) {
            c.throw_error("TypeError", "the reference node is not a Node");
            return false;
        }
        if (!out || !is_document_child(doc_->read(), out)) {
            throw_dom_exception(c, "NotFoundError",
                                "the reference node is not a child of the document");
            return false;
        }
        return true;
    };

    method("appendChild", [this, may_become_a_child, place](context & c, std::span<value> args) {
        if (!may_become_a_child(c, arg(args, 0), node_id{}, node_id{})) {
            return value::undefined();
        }
        place(handle_of(arg(args, 0)), node_id{});
        return arg(args, 0);
    });
    method("insertBefore",
           [this, may_become_a_child, place, reference_child](context & c, std::span<value> args) {
               if (args.size() < 2) {
                   c.throw_error("TypeError", "insertBefore needs a node and a reference child");
                   return value::undefined();
               }
               node_id before;
               if (!reference_child(c, arg(args, 1), before)) { return value::undefined(); }
               if (!may_become_a_child(c, arg(args, 0), before, node_id{})) {
                   return value::undefined();
               }
               // "If child is node, set child to node's next sibling" - inserting a
               // child before itself leaves it where it is.
               const node_id fresh = handle_of(arg(args, 0));
               if (before != fresh) { place(fresh, before); }
               return arg(args, 0);
           });
    method("removeChild", [this, element_child](context & c, std::span<value> args) {
        const node_id child = handle_of(arg(args, 0));
        if (!child) {
            c.throw_error("TypeError", "removeChild: the argument is not a Node");
            return value::undefined();
        }
        if (!is_document_child(doc_->read(), child)) {
            throw_dom_exception(c, "NotFoundError",
                                "removeChild: the node is not a child of the document");
            return value::undefined();
        }
        if (child == element_child()) {
            // A DOCUMENT NOTHING LAYS OUT - one a page made - may lose its
            // element, as DOM says; the page's own cannot, see the top of
            // this file.
            if (!secondary_) {
                throw_dom_exception(c, "NotSupportedError",
                                    "this engine cannot detach the document element: it is the "
                                    "root of the tree and an emptied document has nothing to lay "
                                    "out");
                return value::undefined();
            }
            doc_->remove_document_element();
            mutated();
            return arg(args, 0);
        }
        (void)doc_->remove_child(child);
        mutated();
        return arg(args, 0);
    });
    method("replaceChild", [this, may_become_a_child, place, element_child](context & c,
                                                                            std::span<value> args) {
        if (args.size() < 2) {
            c.throw_error("TypeError", "replaceChild needs a node and the child it replaces");
            return value::undefined();
        }
        const node_id stale = handle_of(arg(args, 1));
        if (!stale || !is_document_child(doc_->read(), stale)) {
            throw_dom_exception(
                c, "NotFoundError",
                "replaceChild: the node being replaced is not a child of the document");
            return value::undefined();
        }
        if (!may_become_a_child(c, arg(args, 0), stale, stale)) { return value::undefined(); }
        const node_id fresh = handle_of(arg(args, 0));
        if (fresh == stale) { return arg(args, 1); }
        if (stale == element_child()) {
            // Only another element may take the page's root's slot - anything
            // else would leave the document with no element to lay out. A
            // document nothing lays out takes whatever DOM allows.
            const bool element =
                doc_->read().kind(fresh).value_or(node_kind::comment) == node_kind::element;
            if (!element && !secondary_) {
                throw_dom_exception(c, "NotSupportedError",
                                    "replacing the document element with a non-element would "
                                    "detach the root of the tree, which this engine cannot do");
                return value::undefined();
            }
            if (element) {
                doc_->set_document_element(fresh, node_id{});
                mutated();
                return arg(args, 1);
            }
            node_id after;
            {
                const auto txn = doc_->read();
                const std::span<const node_id> kids = txn.children(txn.document_node());
                const auto at = std::ranges::find(kids, stale);
                if (at != kids.end() && at + 1 != kids.end()) { after = *(at + 1); }
            }
            doc_->remove_document_element();
            place(fresh, after);
            return arg(args, 1);
        }
        // The next sibling of `stale` is where the new node lands once the
        // old one is gone.
        node_id after;
        {
            const auto txn = doc_->read();
            const std::span<const node_id> kids = txn.children(txn.document_node());
            const auto at = std::ranges::find(kids, stale);
            if (at != kids.end() && at + 1 != kids.end()) { after = *(at + 1); }
        }
        (void)doc_->remove_child(stale);
        place(fresh, after);
        return arg(args, 1);
    });

    // THE ParentNode MIXIN. `append`, `prepend` and `replaceChildren` take any
    // number of arguments and turn a string into a Text node, which is what
    // makes them what modern code writes - and on a Document the Text half is
    // exactly what the constraint refuses.
    //
    // EVERY ARGUMENT IS CHECKED BEFORE ANYTHING IS INSERTED, which is what
    // `append-on-Document.html` measures rather than assumes: after a refused
    // `parent.append(x, y)` it asserts the childNodes are still empty.
    // ponytail: the arguments are checked one at a time rather than as the
    // fragment the specification would build first; the answers are the same.
    const auto insert_all = [this, may_become_a_child, place](context & c, std::span<value> args,
                                                              node_id before) {
        std::size_t elements = 0;
        for (const value & one : args) {
            if (!may_become_a_child(c, one, before, node_id{})) { return false; }
            if (doc_->read().kind(handle_of(one)).value_or(node_kind::comment) ==
                node_kind::element) {
                ++elements;
            }
        }
        if (elements > 1) {
            throw_dom_exception(c, "HierarchyRequestError",
                                "a Document may have at most one element child");
            return false;
        }
        for (const value & one : args) { place(handle_of(one), before); }
        return true;
    };
    method("append", [insert_all](context & c, std::span<value> args) {
        (void)insert_all(c, args, node_id{});
        return value::undefined();
    });
    method("prepend", [this, insert_all](context & c, std::span<value> args) {
        node_id first;
        {
            const auto txn = doc_->read();
            const std::span<const node_id> kids = txn.children(txn.document_node());
            if (!kids.empty()) { first = kids.front(); }
        }
        (void)insert_all(c, args, first);
        return value::undefined();
    });
    method("replaceChildren",
           [this, insert_all, element_child](context & c, std::span<value> args) {
               // `replaceChildren` has to REMOVE what is there first, which on a
               // document with an element means detaching it - which the
               // page's own document cannot do, see removeChild.
               if (element_child() && !secondary_) {
                   throw_dom_exception(c, "NotSupportedError",
                                       "replaceChildren would detach the document element, which "
                                       "this engine's document cannot do - see removeChild");
                   return value::undefined();
               }
               std::vector<node_id> existing;
               {
                   const auto txn = doc_->read();
                   const std::span<const node_id> kids = txn.children(txn.document_node());
                   existing.assign(kids.begin(), kids.end());
               }
               doc_->remove_document_element();
               for (const node_id one : existing) { (void)doc_->remove_child(one); }
               (void)insert_all(c, args, node_id{});
               return value::undefined();
           });

    // --- the rest of Node --------------------------------------------------

    // `normalize()` on a Document is over its whole subtree, which here is
    // documentElement and everything under it.
    method("normalize", [this](context &, std::span<value>) {
        normalize_subtree(doc_->document_node());
        mutated();
        return value::undefined();
    });
    // `cloneNode(deep)`: a SECOND DOCUMENT of the same kind - the flags, the
    // content type and the prototype copied - holding, when deep, a clone of
    // every child of the Document node: doctype, element and all. Made by the
    // same route `createDocument` takes and then re-flagged, because that route
    // is the one that links a document into the realm. The primary's
    // `implementation` is not on a made document (see install.cpp), so the
    // clone is not a full HTMLDocument; what a page reads off one - doctype,
    // documentElement, childNodes - is there.
    method("cloneNode", [this](context & c, std::span<value> args) {
        const bool deep = !args.empty() && context::truthy(args[0]);
        const value made = make_xml_document(c, {}, {}, false);
        if (secondary_documents_.empty()) { return made; }
        dom_bindings & fresh = *secondary_documents_.back();
        fresh.doc_->set_xml(doc_->xml());
        fresh.doc_->set_quirks(doc_->quirks());
        fresh.content_type_ = content_type_;
        if (auto * mine = document_object();
            mine != nullptr && fresh.document_object() != nullptr) {
            fresh.document_object()->prototype = mine->prototype;
            // Written once at install, from flags that have just changed.
            for (const char * name : {"contentType", "compatMode"}) {
                if (const value * held = mine->find(name)) {
                    fresh.document_object()->set(name, *held);
                }
            }
        }
        if (!deep) { return made; }
        std::vector<node_id> kids;
        {
            const auto txn = doc_->read();
            const std::span<const node_id> held = txn.children(txn.document_node());
            kids.assign(held.begin(), held.end());
        }
        const auto from = doc_->read();
        for (const node_id child : kids) {
            const node_id copied = fresh.clone_node(from, child, true, this);
            if (from.kind(child).value_or(node_kind::comment) == node_kind::element) {
                fresh.doc_->set_document_element(copied);
            } else {
                (void)fresh.doc_->append_child(fresh.doc_->document_node(), copied);
            }
        }
        return made;
    });
    // `isEqualNode` compares type and then children pairwise in general; the
    // document has nothing of its own to compare, so two documents are equal
    // when their children are - and `isSameNode` is identity.
    method("isSameNode", [this](context &, std::span<value> args) {
        return value::boolean(is_the_document(arg(args, 0)));
    });
    method("isEqualNode", [this](context &, std::span<value> args) {
        const value other = arg(args, 0);
        if (is_the_document(other)) { return value::boolean(true); }
        dom_bindings * theirs = nullptr;
        dom_bindings * top = primary_ == nullptr ? this : primary_;
        if (top != this && top->is_the_document(other)) { theirs = top; }
        for (const auto & made : top->secondary_documents_) {
            if (made.get() != this && made->is_the_document(other)) { theirs = made.get(); }
        }
        if (theirs == nullptr) { return value::boolean(false); }
        // Children pairwise, across the two slabs: a node of theirs is cloned
        // into ours only to be compared, and left detached for collect().
        const auto mine = doc_->read();
        const auto txn = theirs->doc_->read();
        const std::span<const node_id> ours = mine.children(mine.document_node());
        const std::span<const node_id> other_kids = txn.children(txn.document_node());
        if (ours.size() != other_kids.size()) { return value::boolean(false); }
        for (std::size_t i = 0; i < ours.size(); ++i) {
            const node_id copied = clone_node(txn, other_kids[i], true, theirs);
            if (!nodes_are_equal(mine, ours[i], copied)) { return value::boolean(false); }
        }
        return value::boolean(true);
    });
}

} // namespace ctbrowser::shell
