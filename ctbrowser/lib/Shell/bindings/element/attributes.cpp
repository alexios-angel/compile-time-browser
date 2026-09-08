// dom_bindings - attributes: what a name may be, Attr and the NamedNodeMap over
// them, and the attribute half of the element method surface.
//
// One of twelve files carved out of a 5,442-line bindings/element.cpp on
// 2026-09-08 - which was itself one of six carved out of bindings.cpp on
// 2026-08-09. All are member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp; the helpers more than one of them
// needs are declared in internal.hpp beside this, with external linkage in
// ctbrowser::shell::detail. Nothing about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace detail {

// WHAT AN ATTRIBUTE MAY BE CALLED - and it is NOT the XML `Name` production,
// which is what this used to approximate.
//
// `setAttribute` must throw an InvalidCharacterError for a name that is not a
// valid one (DOM 4.9.1). This refused everything `Name` refuses, and
// `dom/nodes/productions.js` is blunt about how wrong that is:
//
//     var invalid_names = [""]
//     var valid_names = ["x", "X", ":", "a:0", "invalid^Name", "\\", "'",
//                        '"', "0", "0:a", ":a", "x:y:x", "~"]
//
// Thirteen names, twelve of which `Name` refuses and every one of which
// `attributes.html` and `Document-createAttribute.html` require to SUCCEED.
// Only the empty string throws. The rule the platform enforces is a
// SERIALISATION one: a name has to survive being written into a start tag and
// read back, and the HTML tokenizer's attribute name state ends a name on
// whitespace, `/`, `=` and `>` and on nothing else. There is no
// first-character rule at all - `"0"` and `":a"` are legal attribute names and
// illegal ELEMENT names, which is exactly the pair productions.js draws.
//
// THE SECOND COPY OF THIS RULE is `is_valid_attribute_name` in
// bindings/document.cpp, which createAttribute and createAttributeNS answer
// to. That file's comment records that the two disagreed and that reconciling
// them was this one's to do; they agree now. Two translation units' worth of a
// four-line rule rather than one shared helper because `core/algorithms.hpp`
// is for what three callers share and this has two, both of them bindings.
//
// BYTE-WISE ON PURPOSE, and exact rather than approximate: every character the
// rule names is ASCII, and no byte of a multi-byte UTF-8 sequence is. So no
// decoder, and no dependence on how the VM happens to store a string.

[[nodiscard]] bool valid_attribute_name(std::string_view name) {
    // U+0000 is the one character the tokenizer cannot carry - it becomes
    // U+FFFD, so a name holding one does not read back as itself.
    return !name.empty() && name.find_first_of(attribute_name_breaks) == std::string_view::npos &&
           name.find('\0') == std::string_view::npos;
}

[[nodiscard]] split_name split_attribute_name(std::string_view name) {
    const std::size_t colon = name.find(':');
    if (colon == std::string_view::npos) { return split_name{{}, name, false}; }
    return split_name{name.substr(0, colon), name.substr(colon + 1), true};
}

// A NULLABLE DOMString argument. `null` and `undefined` are both the null
// namespace, and so is the empty string - `attributes.html`'s "null and the
// empty string should result in a null namespace" is that sentence as a test.
[[nodiscard]] std::string namespace_argument(context & cx, std::span<value> args, std::size_t i) {
    const value given = arg(args, i);
    return given.is_nullish() ? std::string{} : cx.to_string(given);
}

} // namespace detail

// --- ATTRIBUTES AS NODES: Attr, and the NamedNodeMap over them --------------

// A QUALIFIED NAME, AS THIS ELEMENT WOULD HAVE STORED IT. "If the element is in
// the HTML namespace and its node document is an HTML document, set
// qualifiedName to qualifiedName in ASCII lowercase" - DOM 4.9, and it is the
// whole difference between `getAttribute` and `getAttributeNS`, which is
// case-SENSITIVE and has no such rule.
//
// IT USED TO BE UNCONDITIONAL, and that was wrong in a way an SVG page could
// see: `svg.setAttribute("viewBox", ...)` interned `viewbox`, which is a name
// the rasteriser and the style engine never look for - so the whole of
// `<svg>`'s capitalised attribute surface was unreachable from script, in the
// one namespace the tokenizer goes out of its way to preserve the case of.
// `attributes.html`'s "Only lowercase attributes are returned on HTML
// elements" is the other half of the same rule.
//
// AND THE SECOND HALF OF THE CONDITION IS REAL NOW. "and its node document is an
// HTML DOCUMENT" was not checked, because until the XML front end landed there
// was no other kind - an `.xhtml` file and a frame whose `src` is one both have
// `document::xml()` set, and in one of those `getAttribute("viewBox")` must find
// the attribute the parser stored with its capitals intact.
atom dom_bindings::attribute_key(const read_txn & txn, node_id id,
                                 std::string_view qualified) const {
    const bool folds = txn.element_ns(id) == node_ns::html && !doc_->xml();
    return folds ? atoms_->intern_lower(qualified) : atoms_->intern(qualified);
}

// ONE Attr, AND IT IS LIVE IN BOTH DIRECTIONS. `attr.value` reads the element's
// attribute at the moment it is asked and `attr.value = "x"` writes through to
// it - which is `attributes.html`'s "Attribute values should not be parsed",
// two lines of which write an Attr and then read `el.getAttribute`. Three data
// properties would have been three strings captured when the object was made.
//
// IT IS KEYED ON (namespace, local name), never on a position: DOM 4.9 says
// that pair is what an attribute IS, and it survives every mutation short of
// removing this attribute. An index does not - a `removeAttribute` moves every
// attribute after it down one.
//
// `value`, `nodeValue` and `textContent` are ONE string behind three
// spellings, because on an Attr that is what they are. They are exactly what
// `dom/nodes/attributes.js`'s `attr_is` reads, and it reads all nine of these
// properties on every case of three files.
//
// AN EMPTY `owner` MEANS DETACHED, and that is the whole of the second half of
// this function. An Attr that has been REMOVED still exists - `removeNamedItem`
// and `removeAttributeNode` both HAND IT BACK, which is how a page moves an
// attribute from one element to another - and DOM 4.9.2 leaves its value, name
// and namespace exactly as they were at the moment of removal while setting its
// ownerElement to null. Reading through to the element it used to name answers
// "" for every one of them, because the element no longer has the attribute:
//
//     var gone = e.attributes.removeNamedItem('a');
//     gone.value                                  // "1" in a browser, "" here
//
// So a detached Attr carries its OWN value as three ordinary data properties -
// which also gives `gone.value = "x"` the right meaning, a write to a node that
// is not in any element rather than a write to an element that has moved on.
value dom_bindings::attribute_object(context & cx, node_id owner, const attribute & held) {
    const std::string qualified{atoms_->text(held.name)};
    const std::string ns{atoms_->text(held.ns)};
    const std::string local{attribute_local_name(*atoms_, held)};
    const std::string prefix{attribute_prefix(*atoms_, held)};
    const atom name = held.name;
    const atom uri = held.ns;

    auto * attr = static_cast<script::object_object *>(cx.make_object().as_heap());
    for (const char * spelling : {"value", "nodeValue", "textContent"}) {
        const std::string property{spelling};
        if (!owner) {
            attr->set(property, cx.string(held.value));
            continue;
        }
        attr->define_accessor(
            property,
            value::object(cx.allocate<script::native_object>(
                property,
                [this, owner, ns, local](context & c, std::span<value>) {
                    const auto txn = doc_->read();
                    const attribute * found = txn.find_attribute_ns(owner, ns, local);
                    return c.string(found == nullptr ? std::string{} : found->value);
                })),
            value::object(cx.allocate<script::native_object>(
                property, [this, owner, name, uri](context & c, std::span<value> a) {
                    (void)doc_->set_attribute_ns(owner, uri, name, arg_string(c, a, 0));
                    mutated();
                    return value::undefined();
                })));
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
    // THE WRAPPER THAT ALREADY EXISTS, which is how `attributes_are`'s
    // `assert_equals(el.attributes[i].ownerElement, el)` can be an identity
    // comparison at all. `wrap` is the fallback rather than the path: it
    // REFRESHES the whole element, and going through it once per attribute
    // would re-measure the box for an answer already in hand.
    const value already = value_of_wrapper(owner);
    attr->set("ownerElement", already.is_object() ? already : wrap(cx, owner));
    if (const value proto = interface_prototype("Attr"); proto.is_object()) {
        attr->prototype = proto;
    }
    return value::object(attr);
}

// `element.attributes`, REFILLED IN PLACE rather than rebuilt. The map keeps
// its identity - a page may hold on to one, and `el.attributes ===
// el.attributes` is true in a browser - so this rewrites its indexed
// properties and its `length`.
//
// AN ARRAY-LIKE OBJECT RATHER THAN A PROXY, and that is a measured choice
// rather than a shortcut. `for (const a of el.attributes)` is how p5's XML
// module walks one and `[].concat(...el.attributes)` is Bootstrap's spelling;
// both go through `context::iterable_values`, which materialises any object
// carrying a numeric `length` and indexed properties and yields NOTHING AT ALL
// for a proxy (vm/call.cpp, and bytecode_opcodes.def's note on `iterable`). A
// proxy would have been live and uniterable, which is the worse half of each.
void dom_bindings::refresh_attribute_map(context & cx, script::object_object & map, node_id id) {
    // COPIED OUT BEFORE ANYTHING ELSE RUNS. `attribute_object` calls `wrap`,
    // which opens a read_txn of its own, and a read nested inside another read
    // is a shape nothing else in these bindings has.
    std::vector<attribute> held;
    {
        const auto txn = doc_->read();
        const std::span<const attribute> current = txn.attributes(id);
        held.assign(current.begin(), current.end());
    }
    for (std::size_t i = 0; i < held.size(); ++i) {
        map.set(std::to_string(i), attribute_object(cx, id, held[i]));
    }
    // THE INDICES THAT WENT AWAY. A removed attribute leaves its old index
    // behind, and an index past `length` that still answers is how
    // `assert_array_equals` reports a length it was never given.
    for (std::size_t i = held.size(); map.find(std::to_string(i)) != nullptr; ++i) {
        (void)map.erase(std::to_string(i));
    }
    map.set("length", value::number(static_cast<double>(held.size())));
    // THE NAMED PROPERTIES. `element.attributes.x` is the attribute called `x`
    // - a NamedNodeMap is a legacy platform object with a named property getter
    // and `attributes-namednodemap.html` is five subtests of exactly this. The
    // indexed half was here from the start and this half was not, so a page
    // could reach an attribute by position and not by name.
    //
    // NEVER OVER A METHOD OR OVER `length`, which is the rest of that file:
    // `setAttributeNS("foo", "setNamedItem", v)` must leave
    // `attributes.setNamedItem` a function and `setAttributeNS("foo", "length",
    // v)` must leave `attributes.length` the count. A named property that
    // shadowed either would be an attribute a page can write breaking the map
    // it was written into.
    {
        // "Is this key an array index" is object_object's own question - the
        // one that decides property ORDER - so it is asked with its function
        // rather than with a second spelling that could disagree.
        const auto is_index = [](std::string_view key) {
            std::uint32_t at = 0;
            return script::object_object::array_index_key(key, at);
        };
        std::vector<std::string> stale;
        for (const auto & [key, current] : map.props) {
            if (key == "length" || is_index(key)) { continue; }
            // A METHOD STAYS. Everything else on this object is a named
            // property this function put there on an earlier refresh, and it
            // goes: an attribute that has been removed must stop answering.
            if (current.is_callable()) { continue; }
            stale.push_back(key);
        }
        for (const std::string & key : stale) { (void)map.erase(key); }
        for (std::size_t i = 0; i < held.size(); ++i) {
            const std::string qualified{atoms_->text(held[i].name)};
            if (qualified == "length" || is_index(qualified)) { continue; }
            // THE SAME Attr OBJECT THE INDEX HOLDS, not a second one. Sharing
            // is both cheaper - a wrapper per attribute per read rather than
            // two - and RIGHT: `el.attributes[0] === el.attributes.x` is true
            // in a browser, an Attr being one node under two ways of reaching
            // it. It is read back out of the map rather than kept in a C++
            // local because the map is what roots it.
            //
            // ALREADY TAKEN means leave it alone, which covers both the methods
            // and the case DOM's named getter is actually about: two attributes
            // may share a qualified name in different namespaces, and the FIRST
            // is the one the name answers with.
            if (map.find(qualified) != nullptr) { continue; }
            const value * indexed = map.find(std::to_string(i));
            if (indexed == nullptr) { continue; }
            map.set(qualified, *indexed);
        }
    }
    if (!map.prototype.is_object()) {
        // LATE, because the interfaces are built lazily: the first two element
        // wrappers exist before `EventTarget` does. See ensure_dom_interfaces.
        if (const value proto = interface_prototype("NamedNodeMap"); proto.is_object()) {
            map.prototype = proto;
        }
    }
}

// "VALIDATE AND EXTRACT", DOM 4.9, shared by `setAttributeNS` and the two
// `setNamedItemNS` spellings. Answers false HAVING ALREADY THROWN, which is the
// shape `pre_insert_valid` above uses and for the same reason.
//
// THE ORDER OF THE TWO HALVES IS PART OF THE ANSWER. The SHAPE of the name is
// decided before the namespace is looked at, so `setAttributeNS(XMLNS, "", v)`
// is an InvalidCharacterError and not the NamespaceError its namespace would
// otherwise earn. Both orderings throw; only one throws what the suite asserts.
//
// A prefix is checked for being non-empty and writable and NOTHING ELSE - it is
// the LOCAL name that has to be a name, and the prefix is only ever a label in
// front of it. Deliberately the same rule, in the same order, as
// createAttributeNS in bindings/document.cpp.
bool dom_bindings::validate_and_extract(context & cx, std::string_view where,
                                        const std::string & ns, const std::string & qualified) {
    const split_name split = split_attribute_name(qualified);
    const bool prefix_writable =
        !split.prefix.empty() &&
        split.prefix.find_first_of(attribute_name_breaks) == std::string_view::npos;
    if ((split.has_colon && !prefix_writable) || !valid_attribute_name(split.local)) {
        throw_dom_exception(cx, "InvalidCharacterError",
                            std::string{where} + ": '" + qualified +
                                "' is not a qualified attribute name");
        return false;
    }
    const auto fail = [&](const std::string & why) {
        throw_dom_exception(cx, "NamespaceError", std::string{where} + ": " + why);
        return false;
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
    return true;
}

void dom_bindings::install_attribute_methods(context & cx, script::object_object & obj) {
    const auto method = [&](std::string name, script::native_fn fn) {
        obj.set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };

    method("setAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        const std::string name = arg_string(c, args, 0);
        if (!valid_attribute_name(name)) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "setAttribute: '" + name + "' is not a valid attribute name");
            return value::undefined();
        }
        if (!id) { return value::undefined(); }
        const std::string text = arg_string(c, args, 1);
        const auto txn = doc_->read();
        // THE FIRST ATTRIBUTE WITH THIS QUALIFIED NAME, whatever its namespace,
        // and only its VALUE changes - the DOM layer's set_attribute is what
        // means "Setting the same attribute with another prefix should not
        // change the prefix", which is a subtest by name.
        (void)doc_->set_attribute(id, attribute_key(txn, id, name), text);
        mutated();
        return value::undefined();
    });
    // THE NAMESPACED HALF OF THE ATTRIBUTE API. Every one of these matches on
    // the PAIR (namespace, local name), in which the prefix takes no part, and
    // none of them folds case: an element may hold `x` in no namespace and `x`
    // in two others at once, and each of the three lookups has a different
    // right answer. `Element-removeAttribute.html`'s two subtests are that
    // sentence, in both orders.
    method("setAttributeNS", [this](context & c, std::span<value> args) {
        const std::string ns = namespace_argument(c, args, 0);
        // A DOMString rather than a nullable one, so `null` here really is the
        // four characters "null" and an omitted argument is "undefined".
        const std::string qualified =
            args.size() > 1 ? c.to_string(args[1]) : std::string{"undefined"};
        if (!validate_and_extract(c, "setAttributeNS", ns, qualified)) {
            return value::undefined();
        }
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        // INTERNED AS WRITTEN. The qualified name IS the attribute's name and
        // folding it would lose the case `setAttributeNS("", "ALIGN", ...)`
        // deliberately keeps - see the note on attribute_key.
        (void)doc_->set_attribute_ns(id, atoms_->intern(ns), atoms_->intern(qualified),
                                     arg_string(c, args, 2));
        mutated();
        return value::undefined();
    });
    method("getAttributeNS", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::null(); }
        const std::string ns = namespace_argument(c, args, 0);
        const auto txn = doc_->read();
        const attribute * held = txn.find_attribute_ns(id, ns, arg_string(c, args, 1));
        return held == nullptr ? value::null() : c.string(held->value);
    });
    method("hasAttributeNS", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const std::string ns = namespace_argument(c, args, 0);
        return value::boolean(doc_->read().has_attribute_ns(id, ns, arg_string(c, args, 1)));
    });
    method("removeAttributeNS", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const std::string ns = namespace_argument(c, args, 0);
        // A LOCAL NAME, not a qualified one: `removeAttributeNS(XML, "a:bb")`
        // removes NOTHING, which is the whole of Element-removeAttributeNS.html.
        (void)doc_->remove_attribute_ns(id, ns, arg_string(c, args, 1));
        mutated();
        return value::undefined();
    });
    // `hasAttributes()` - "does this element have any at all", which is a
    // different question from `attributes.length !== 0` only in that a page can
    // ask it without materialising the map.
    method("hasAttributes", [this](context & c, std::span<value>) {
        (void)c;
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        return value::boolean(!doc_->read().attributes(id).empty());
    });
    // THE Attr SPELLINGS OF THE SAME FOUR LOOKUPS. `getAttributeNodeNS` is what
    // `Attr-prefix.html` reaches for on every one of its six cases, because the
    // prefix and the namespace are the two things only an Attr can report.
    method("getAttributeNode", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::null(); }
        std::optional<attribute> held;
        {
            const auto txn = doc_->read();
            const attribute * found =
                txn.find_attribute(id, attribute_key(txn, id, arg_string(c, args, 0)));
            if (found != nullptr) { held = *found; }
        }
        return held ? attribute_object(c, id, *held) : value::null();
    });
    method("getAttributeNodeNS", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::null(); }
        const std::string ns = namespace_argument(c, args, 0);
        std::optional<attribute> held;
        {
            const auto txn = doc_->read();
            const attribute * found = txn.find_attribute_ns(id, ns, arg_string(c, args, 1));
            if (found != nullptr) { held = *found; }
        }
        return held ? attribute_object(c, id, *held) : value::null();
    });
    // `setAttributeNode` and `removeAttributeNode` are `setNamedItem` and
    // `removeNamedItem` under other names - DOM 4.9 defines each pair in terms
    // of the same "set an attribute" and "remove an attribute" - so they are
    // FORWARDED rather than written twice. Two implementations of one operation
    // is two chances for the returned old Attr to differ.
    const auto through_map = [this](std::string on_map) {
        return [this, on_map](context & c, std::span<value> args) {
            const node_id id = receiver(c);
            if (!id) { return value::null(); }
            const value map = c.lookup_property(c.current_this(), "attributes");
            const value fn = c.lookup_property(map, on_map);
            if (!fn.is_callable()) { return value::null(); }
            return c.call(fn, args, map);
        };
    };
    method("setAttributeNode", through_map("setNamedItem"));
    method("setAttributeNodeNS", through_map("setNamedItemNS"));
    method("removeAttributeNode", [this](context & c, std::span<value> args) {
        // NOT through the map: `removeAttributeNode` takes the Attr ITSELF and
        // throws a NotFoundError when it is not this element's, where
        // `removeNamedItem` takes a name.
        const node_id id = receiver(c);
        const value given = arg(args, 0);
        if (!id || !given.is_object()) {
            throw_dom_exception(c, "NotFoundError",
                                "removeAttributeNode: the argument is not an attribute of this "
                                "element");
            return value::null();
        }
        const value ns_property = c.lookup_property(given, "namespaceURI");
        const std::string ns = ns_property.is_nullish() ? std::string{} : c.to_string(ns_property);
        const std::string local = c.to_string(c.lookup_property(given, "localName"));
        std::optional<attribute> held;
        {
            const auto txn = doc_->read();
            const attribute * found = txn.find_attribute_ns(id, ns, local);
            if (found != nullptr) { held = *found; }
        }
        if (!held) {
            throw_dom_exception(c, "NotFoundError",
                                "removeAttributeNode: '" + local +
                                    "' is not an attribute of this "
                                    "element");
            return value::null();
        }
        (void)doc_->remove_attribute_ns(id, ns, local);
        mutated();
        // THE ARGUMENT IS THE ANSWER, and it is the ARGUMENT that has to be
        // detached: this is the one removal that hands back an object the page
        // already holds rather than one made here, so the live accessors on it
        // are still reading through to an element that no longer has the
        // attribute. Frozen in place, for the reason attribute_object gives.
        auto * detaching = static_cast<script::object_object *>(given.as_heap());
        for (const char * spelling : {"value", "nodeValue", "textContent"}) {
            (void)detaching->erase_accessor(spelling);
            detaching->set(spelling, c.string(held->value));
        }
        detaching->set("ownerElement", value::null());
        return given;
    });
    // `toggleAttribute(name, force)` - the boolean-attribute spelling, and it
    // ANSWERS whether the attribute is present afterwards, which is what a page
    // toggling one reads. Without it the only way to flip `disabled` was a
    // hasAttribute/removeAttribute/setAttribute dance that reads the tree twice.
    method("toggleAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        const std::string name = arg_string(c, args, 0);
        if (!valid_attribute_name(name)) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "toggleAttribute: '" + name + "' is not a valid attribute name");
            return value::boolean(false);
        }
        if (!id) { return value::boolean(false); }
        const atom key = attribute_key(doc_->read(), id, name);
        const bool present = doc_->read().has_attribute(id, key);
        // `force` is TRISTATE: absent means "flip", a present `false` means
        // "remove whether or not it is there". `args.size()` is the only thing
        // that can tell the first from the second.
        const bool want = args.size() > 1 ? context::truthy(args[1]) : !present;
        if (want == present) { return value::boolean(present); }
        if (want) {
            (void)doc_->set_attribute(id, key, "");
        } else {
            (void)doc_->remove_attribute(id, key);
        }
        mutated();
        return value::boolean(want);
    });
    // `getAttributeNames()` - the QUALIFIED names, in order, which is the one
    // answer `element.attributes` cannot give in a single string comparison.
    method("getAttributeNames", [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        if (!id) { return out; }
        const auto txn = doc_->read();
        for (const attribute & held : txn.attributes(id)) {
            items->items.push_back(c.string(std::string{atoms_->text(held.name)}));
        }
        return out;
    });
    method("getAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::null(); }
        const auto txn = doc_->read();
        // PRESENT-BUT-EMPTY is not absent. `<details open>`, `<input
        // disabled>` and `<option selected>` all have an empty value, and
        // returning null for them made every boolean attribute unreadable
        // from script - the one shape of attribute that is only ever tested
        // for presence.
        const atom name = attribute_key(txn, id, arg_string(c, args, 0));
        const attribute * held = txn.find_attribute(id, name);
        if (held == nullptr) { return value::null(); }
        return c.string(held->value);
    });
    // THE OTHER TWO HALVES OF THE ATTRIBUTE API. `setAttribute` and
    // `getAttribute` were here and these were not, so an attribute could be
    // written and read and never taken away: `el.removeAttribute('class')` was a
    // TypeError, and `el.hasAttribute('disabled')` - the correct way to ask
    // about a boolean attribute - did not exist at all, leaving `getAttribute()
    // !== null` as the only spelling and undefined behaviour for the page that
    // did not know it.
    method("removeAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const auto txn = doc_->read();
        (void)doc_->remove_attribute(id, attribute_key(txn, id, arg_string(c, args, 0)));
        mutated();
        return value::undefined();
    });
    method("hasAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const auto txn = doc_->read();
        return value::boolean(
            txn.has_attribute(id, attribute_key(txn, id, arg_string(c, args, 0))));
    });
}

} // namespace ctbrowser::shell
