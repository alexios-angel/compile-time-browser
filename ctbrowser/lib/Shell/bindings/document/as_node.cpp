// dom_bindings - the document as a Node: the twenty-two members of the Node and
// ParentNode surface on an object that has no Document node behind it.
//
// One of eight files carved out of a 3,071-line bindings/document.cpp on
// 2026-09-08 - which was itself one of six carved out of bindings.cpp on
// 2026-08-09. All are member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp; the name productions every one of
// them needs are in internal.hpp beside this. Nothing about the public header
// changed.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// ============================================================================
// THE DOCUMENT AS A NODE
// ============================================================================
//
// THE STRUCTURAL FACT EVERYTHING BELOW IS BUILT AROUND: there is no Document
// node in this tree. `tree_builder` makes `<html>` and calls `set_root` with
// it, so `txn.root()` IS the document element, `node_kind::document` is a kind
// nothing in the engine ever produces, and `document` is a plain script object
// carrying no handle at all - `handle_of(document)` is the same empty handle it
// answers for a number. That is why none of this could be shared with the
// element bindings: those all start from `receiver(cx)`, and the document has
// nothing for `receiver` to find.
//
// THE MODEL, decided once and applied to every member here:
//
//     the Document is a node whose child list is exactly [documentElement],
//     whose parent is null, which is connected, which contains everything
//     `<html>` contains and `<html>` itself, and which precedes every node in
//     the tree in document order.
//
// `documentElement` is `find_by_tag("html")` rather than `txn.root()`, because
// the `documentElement` property installed above is, and
// `document.firstChild === document.documentElement` has to hold.
//
// WHAT THE MODEL CANNOT DO, said here rather than guessed at each call site:
//
//   * THERE IS NO DOCTYPE NODE. `node_kind` has no `document_type`, so
//     `document.doctype` is null (see the block above where it is set) and
//     `document.firstChild` on a page that begins `<!DOCTYPE html>` reports
//     `<html>` where a browser reports the DocumentType. That is a WRONG
//     answer, not a missing one, and it is the one place in this block where
//     the honest alternative - refusing to answer firstChild at all - would be
//     worse for every page that has no doctype.
//   * NOTHING CAN BE INSERTED. A Comment is the one child the DOM permits a
//     Document that already has an element child, and there is no node above
//     `<html>` for a sibling of it to hang from. Every insertion therefore
//     throws: HierarchyRequestError where the specification requires one (an
//     element, when there is already `<html>`; a Text child, ever), and
//     NotSupportedError where the DOM would have allowed it and this engine
//     cannot. No specification puts a NotSupportedError at that step, which is
//     the point - the name says "this implementation" instead of making a false
//     claim about the hierarchy.
//   * `documentElement` CANNOT BE DETACHED. `document::remove_child` refuses
//     the root - `dom_error::is_root` - because a tree whose root is gone has
//     nothing left to be. So `removeChild(documentElement)`, `replaceChild` and
//     `replaceChildren()` are NotSupportedError for the same reason.
//   * THERE IS NO SECOND DOCUMENT, so `cloneNode` has nothing to answer with.
//     What that would cost is written out beside `createDocument` above.
//
// The corpus reading behind this, because it is not what the file names
// suggest: `Node-contains.html`, `Node-compareDocumentPosition.html`,
// `Node-properties.html` and `Node-textContent.html` all die in `setup()` on
// `document.implementation.createHTMLDocument` / `createDocument`, and
// `append-on-Document.html`, `prepend-on-Document.html` and
// `DocumentType-remove.html` run entirely against a document `createDocument`
// made. None of the seven can pass until there are two Documents. What IS
// reachable from here is `Document-createAttribute.html`'s HTML half and
// `Node-lookupNamespaceURI.html`'s twelve document subtests - plus every page
// that reads one of these twenty-two members without a guard and gets a
// TypeError on the first line.

namespace {

// The DOCUMENT_POSITION_* bits, DOM 4.4. Named because `20` at the one place
// they are combined says nothing and `contained_by | following` says all of it.
constexpr unsigned position_disconnected = 0x01;
constexpr unsigned position_preceding = 0x02;
constexpr unsigned position_following = 0x04;
constexpr unsigned position_contains = 0x08;
constexpr unsigned position_contained_by = 0x10;
constexpr unsigned position_implementation_specific = 0x20;

// An Attr, as `createAttribute` and `createAttributeNS` hand one back.
//
// NOT A NODE, and for the reason `createProcessingInstruction` above is not
// one: `node_kind` has no `attribute`, so there is nowhere in the tree to put
// it and `attr instanceof Attr` is false. What it carries is exactly what
// `dom/nodes/attributes.js`'s `attr_is` reads off one - nine properties, and
// the corpus checks every one of them on every case.
//
// `value`, `nodeValue` and `textContent` are ONE STRING behind three
// spellings, because on an Attr that is what they are: a page that writes
// `attr.value` and reads `attr.nodeValue` must not see the old text. Three
// data properties would have been three independent strings.
[[nodiscard]] value make_attr_object(context & cx, const std::string & qualified,
                                     const std::string & local, const std::string & prefix,
                                     const std::string & ns) {
    auto * attr = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto held = std::make_shared<std::string>();
    for (const char * spelling : {"value", "nodeValue", "textContent"}) {
        const value getter = value::object(cx.allocate<script::native_object>(
            spelling, [held](context & c, std::span<value>) { return c.string(*held); }));
        const value setter = value::object(
            cx.allocate<script::native_object>(spelling, [held](context & c, std::span<value> a) {
                *held = arg_string(c, a, 0);
                return value::undefined();
            }));
        attr->define_accessor(spelling, getter, setter);
    }
    attr->set("name", cx.string(qualified));
    attr->set("nodeName", cx.string(qualified));
    attr->set("localName", cx.string(local));
    attr->set("prefix", prefix.empty() ? value::null() : cx.string(prefix));
    attr->set("namespaceURI", ns.empty() ? value::null() : cx.string(ns));
    attr->set("nodeType", value::number(2));
    // TRUE for every Attr since DOM4 deleted the other answer, and `attr_is`
    // asserts it on every case it runs.
    attr->set("specified", value::boolean(true));
    // NULL, and it stays null: `setAttributeNode` is the only thing that would
    // ever set it and there is none.
    attr->set("ownerElement", value::null());
    return value::object(attr);
}

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
    if (prefix != nullptr && *prefix == "xml") { return std::string{xml_namespace}; }
    if (prefix != nullptr && *prefix == "xmlns") { return std::string{xmlns_namespace}; }
    if (!element || atoms_ == nullptr || doc_ == nullptr) { return {}; }
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
    for (const auto & [id, text] : rewritten) { (void)doc_->set_text(id, text); }
    for (const node_id id : doomed) { (void)doc_->remove_child(id); }
    for (const node_id id : descend) { normalize_subtree(id); }
}

void dom_bindings::install_document_as_node(context & cx, script::object_object & doc) {
    const auto method = [&](std::string name, script::native_fn fn) {
        const value native = value::object(cx.allocate<script::native_object>(name, std::move(fn)));
        doc.set(name, native);
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
    // property above takes, so the two cannot name different nodes.
    const auto element_child = [this] { return find_by_tag("html"); };

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

    read_only("childNodes", [this, element_child](context & c, std::span<value>) {
        value list = c.make_array();
        auto * items = static_cast<script::array_object *>(list.as_heap());
        if (const node_id html = element_child()) { items->items.push_back(wrap(c, html)); }
        return list;
    });
    // THE DOCTYPE IS MISSING FROM BOTH OF THESE, and that is the one wrong
    // answer in this block rather than a missing one: a page beginning
    // `<!DOCTYPE html>` has a DocumentType as its first child in every browser,
    // `node_kind` has no `document_type` for one to be, and `document.doctype`
    // is null for the same reason. See the note beside it above.
    read_only("firstChild", [this, element_child](context & c, std::span<value>) {
        return wrap(c, element_child());
    });
    read_only("lastChild", [this, element_child](context & c, std::span<value>) {
        return wrap(c, element_child());
    });
    method("hasChildNodes", [element_child](context &, std::span<value>) {
        return value::boolean(static_cast<bool>(element_child()));
    });

    // --- the two questions about the tree that a page actually asks --------

    // `contains(other)` is the INCLUSIVE-descendant test, and the Document
    // contains everything in the page including itself. A NULL argument is
    // FALSE and not a throw: the argument is a nullable Node, which is why
    // `Node-contains.html` opens with `assert_false(reference.contains(null))`
    // for all twenty-three of its nodes.
    method("contains", [this](context &, std::span<value> args) {
        const value given = arg(args, 0);
        if (is_the_document(given)) { return value::boolean(true); }
        const node_id other = handle_of(given);
        if (!other) { return value::boolean(false); }
        const auto txn = doc_->read();
        // `is_ancestor_of` is self-first, so this is true for <html> as well as
        // for everything under it - which is exactly what the Document contains.
        return value::boolean(txn.is_ancestor_of(txn.root(), other));
    });

    // `compareDocumentPosition(other)`, as the real bitmask.
    //
    // The Document is first in document order and contains the whole tree, so
    // of the six results only three can ever come back here: 0 for itself,
    // CONTAINED_BY|FOLLOWING for anything in the page, and the disconnected
    // triple for a node that has been created or removed. CONTAINS and a bare
    // PRECEDING are unreachable BY CONSTRUCTION rather than unimplemented -
    // nothing is an ancestor of the document and nothing precedes it.
    method("compareDocumentPosition", [this](context & c, std::span<value> args) {
        const value given = arg(args, 0);
        if (is_the_document(given)) { return value::number(0); }
        const node_id other = handle_of(given);
        if (!other) {
            // A non-nullable Node in the IDL, so anything else fails argument
            // conversion before the method runs - a TypeError, not a 0 that
            // says "these are the same node".
            c.throw_error("TypeError", "compareDocumentPosition: the argument is not a Node");
            return value::undefined();
        }
        const auto txn = doc_->read();
        if (txn.is_ancestor_of(txn.root(), other)) {
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
        // ASCII lowercase." Every document in this engine is one - see
        // `contentType` above - so this is unconditional, and it is why
        // `createAttribute("TITLE").name` is "title".
        const std::string local = ascii_lower_copy(given);
        return make_attr_object(c, local, local, {}, {});
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
        return make_attr_object(c, qualified, std::string{split.local}, std::string{split.prefix},
                                ns);
    });

    // --- everything that would change the document's own child list --------

    // WHAT MAY BECOME A CHILD OF THIS DOCUMENT. Answers false HAVING ALREADY
    // THROWN, the same shape `pre_insert_valid` uses and for the same reason -
    // a caller is one `if` rather than an error channel.
    //
    // EVERY PATH THROUGH IT THROWS TODAY, and that is a statement about this
    // engine rather than a stub. DOM 4.2.3 step 5 gives a Document its own
    // constraint - at most one element child, never a Text child, at most one
    // doctype - and this document always already has `<html>`, so the two cases
    // a page actually writes are refused by the SPECIFICATION. The third, a
    // Comment, the specification allows and this engine cannot hold. Keeping
    // the boolean rather than collapsing it to a throw is what makes the true
    // path appear the day there is a Document node.
    const auto may_become_a_child = [this, element_child](context & c, value given) {
        const node_id id = handle_of(given);
        if (!id) {
            // `node_from` turns anything that is not a wrapper into a Text
            // node, and a Document may never have a Text child. Refused BEFORE
            // the node is created rather than after, so a rejected
            // `document.append('text')` leaves nothing behind in the slab.
            throw_dom_exception(c, "HierarchyRequestError", "a Document cannot have a Text child");
            return false;
        }
        node_kind kind = node_kind::element;
        bool fragment_has_a_real_child = false;
        {
            const auto txn = doc_->read();
            kind = txn.kind(id).value_or(node_kind::element);
            if (kind == node_kind::document_fragment) {
                for (const node_id child : txn.children(id)) {
                    const node_kind held = txn.kind(child).value_or(node_kind::element);
                    if (held == node_kind::element || held == node_kind::text) {
                        fragment_has_a_real_child = true;
                    }
                }
            }
        }
        switch (kind) {
        case node_kind::text:
            throw_dom_exception(c, "HierarchyRequestError", "a Document cannot have a Text child");
            return false;
        case node_kind::document:
            throw_dom_exception(c, "HierarchyRequestError", "a Document cannot be inserted");
            return false;
        case node_kind::element:
            if (element_child()) {
                throw_dom_exception(c, "HierarchyRequestError",
                                    "a Document may have at most one element child and this one "
                                    "already has <html>");
                return false;
            }
            break;
        case node_kind::document_fragment:
            if (fragment_has_a_real_child && element_child()) {
                throw_dom_exception(c, "HierarchyRequestError",
                                    "the fragment has an element or Text child, which a Document "
                                    "that already has <html> cannot take");
                return false;
            }
            break;
        case node_kind::comment: break;
        }
        // A Comment, or an element for a document that somehow has none. Both
        // are legal DOM and neither is possible: there is no node above <html>
        // for a child of the Document to hang from. NotSupportedError rather
        // than HierarchyRequestError because no specification puts one here, so
        // the name cannot be mistaken for a claim about the hierarchy.
        throw_dom_exception(c, "NotSupportedError",
                            "this engine's document has no node above <html>, so nothing can be "
                            "made a child of it");
        return false;
    };

    method("appendChild", [may_become_a_child](context & c, std::span<value> args) {
        if (!may_become_a_child(c, arg(args, 0))) { return value::undefined(); }
        return arg(args, 0);
    });
    method("insertBefore",
           [this, may_become_a_child, element_child](context & c, std::span<value> args) {
               // "If child is non-null and its parent is not parent, throw a
               // NotFoundError" - DOM 4.2.3 step 3, and it comes BEFORE the check on
               // what is being inserted. The Document's only child is documentElement,
               // so anything else as the reference is a NotFoundError.
               const value ref = arg(args, 1);
               if (!ref.is_nullish()) {
                   const node_id before = handle_of(ref);
                   if (!before || before != element_child()) {
                       throw_dom_exception(
                           c, "NotFoundError",
                           "insertBefore: the reference node is not a child of the document");
                       return value::undefined();
                   }
               }
               if (!may_become_a_child(c, arg(args, 0))) { return value::undefined(); }
               return arg(args, 0);
           });
    method("removeChild", [this, element_child](context & c, std::span<value> args) {
        const node_id child = handle_of(arg(args, 0));
        if (!child) {
            c.throw_error("TypeError", "removeChild: the argument is not a Node");
            return value::undefined();
        }
        if (child != element_child()) {
            throw_dom_exception(c, "NotFoundError",
                                "removeChild: the node is not a child of the document");
            return value::undefined();
        }
        throw_dom_exception(c, "NotSupportedError",
                            "this engine cannot detach <html>: it is the root of the tree and "
                            "there is no Document node above it for an emptied document to be");
        return value::undefined();
    });
    method("replaceChild", [this, element_child](context & c, std::span<value> args) {
        const node_id stale = handle_of(arg(args, 1));
        if (!stale || stale != element_child()) {
            throw_dom_exception(
                c, "NotFoundError",
                "replaceChild: the node being replaced is not a child of the document");
            return value::undefined();
        }
        throw_dom_exception(c, "NotSupportedError",
                            "replacing <html> would detach the root of the tree, which this "
                            "engine's document cannot do - see removeChild");
        return value::undefined();
    });

    // THE ParentNode MIXIN. `append`, `prepend` and `replaceChildren` take any
    // number of arguments and turn a string into a Text node, which is what
    // makes them what modern code writes - and on a Document the Text half is
    // exactly what the constraint refuses.
    //
    // EVERY ARGUMENT IS CHECKED BEFORE ANYTHING IS INSERTED, which is what
    // `append-on-Document.html` measures rather than assumes: after a refused
    // `parent.append(x, y)` it asserts the childNodes are still empty.
    const auto check_every_argument = [may_become_a_child](context & c, std::span<value> args) {
        for (const value & one : args) {
            if (!may_become_a_child(c, one)) { return false; }
        }
        return true;
    };
    // With no arguments both are a documented no-op, and that is the ONE
    // insertion case on this document that succeeds.
    method("append", [check_every_argument](context & c, std::span<value> args) {
        (void)check_every_argument(c, args);
        return value::undefined();
    });
    method("prepend", [check_every_argument](context & c, std::span<value> args) {
        (void)check_every_argument(c, args);
        return value::undefined();
    });
    method("replaceChildren",
           [this, check_every_argument, element_child](context & c, std::span<value> args) {
               if (!check_every_argument(c, args)) { return value::undefined(); }
               // Nothing was refused, so there was nothing to insert - and
               // `replaceChildren()` still has to REMOVE what is there, which on this
               // document means detaching <html>.
               if (element_child()) {
                   throw_dom_exception(c, "NotSupportedError",
                                       "replaceChildren would detach <html>, which this engine's "
                                       "document cannot do - see removeChild");
               }
               return value::undefined();
           });

    // --- the rest of Node --------------------------------------------------

    // `normalize()` on a Document is over its whole subtree, which here is
    // documentElement and everything under it.
    method("normalize", [this, element_child](context &, std::span<value>) {
        normalize_subtree(element_child());
        mutated();
        return value::undefined();
    });
    method("cloneNode", [this](context & c, std::span<value>) {
        throw_dom_exception(c, "NotSupportedError",
                            "cloneNode on the document needs a second Document, which this engine "
                            "does not have - see document.implementation");
        return value::undefined();
    });
    // ONE DOCUMENT, so the only node this one is equal to, or the same as, is
    // itself. `isEqualNode` compares type and then children pairwise in
    // general; with a second Document impossible the general case has exactly
    // one true answer and is not an approximation of anything.
    for (const char * spelling : {"isEqualNode", "isSameNode"}) {
        method(spelling, [this](context &, std::span<value> args) {
            return value::boolean(is_the_document(arg(args, 0)));
        });
    }
}

} // namespace ctbrowser::shell
