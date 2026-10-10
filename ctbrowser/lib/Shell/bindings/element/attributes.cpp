//===- attributes.cpp - Attributes, Attr and NamedNodeMap -------*- C++ -*-===//
//
// dom_bindings - attributes: what a name may be, Attr and the NamedNodeMap
// over them, and the attribute methods of Element and ProcessingInstruction.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace detail {

/// Reads argument \p i as a nullable DOMString namespace. `null`, `undefined`
/// and the empty string all mean the null namespace, returned as "" (WPT
/// attributes.html: "null and the empty string should result in a null
/// namespace").
[[nodiscard]] std::string namespace_argument(context & cx, std::span<value> args, std::size_t i) {
    const value given = arg(args, i);
    return given.is_nullish() ? std::string{} : cx.to_string(given);
}

} // namespace detail

// --- Attributes as nodes: Attr, and the NamedNodeMap over them --------------

/// Returns the atom this element would store \p qualified under. DOM 4.9: an
/// HTML-namespace element in an HTML document lowercases the name. Any other
/// element (SVG's `viewBox`, or anything in an XML/XHTML document) keeps its
/// case. `getAttributeNS` never folds. The rule itself is in
/// ctbrowser::attribute_key.
atom dom_bindings::attribute_key(const read_txn &, node_id id, std::string_view qualified) const {
    return ctbrowser::attribute_key(*doc_, id, qualified);
}

// Returns the Attr object for `held` on `owner`. It is live in both
// directions: `attr.value` reads the element's attribute at the moment it is
// asked and `attr.value = "x"` writes through to it (attributes.html,
// "Attribute values should not be parsed").
//
// It is keyed on (namespace, local name), never on a position: DOM 4.9 says
// that pair is what an attribute is, and it survives every mutation short of
// removing this attribute, whereas a `removeAttribute` shifts every later
// index down by one.
//
// `value`, `nodeValue` and `textContent` are one string behind three
// spellings, because on an Attr that is what they are.
//
// An empty `owner` means detached. A removed Attr still exists
// (`removeNamedItem` and `removeAttributeNode` both return it, which is how a
// page moves an attribute from one element to another), and DOM 4.9.2 leaves
// its value, name and namespace as they were at removal while setting its
// ownerElement to null. Reading through to the element it used to name would
// answer "" for every one of them, because the element no longer has the
// attribute:
//
//     var gone = e.attributes.removeNamedItem('a');
//     gone.value  // "1" in a browser; "" if the Attr read through to its old element
//
// So a detached Attr carries its own value, which also gives
// `gone.value = "x"` the right meaning: a write to a node that is not in any
// element rather than to an element that has moved on.
value dom_bindings::attribute_object(context & cx, node_id owner, const attribute & held) {
    const std::string qualified{atoms_->text(held.name)};
    const std::string ns{atoms_->text(held.ns)};
    const std::string local{attribute_local_name(*atoms_, held)};
    const std::string prefix{attribute_prefix(*atoms_, held)};

    // Reuse the existing object for an attached attribute: its accessors read
    // the element, so nothing about it is stale.
    const std::string key = ns + '\0' + local;
    if (owner) {
        for (const auto & [known, obj] : attr_objects_[owner.key()]) {
            if (known == key) { return value::object(obj); }
        }
    }
    auto * attr = cx.allocate<script::object_object>();
    attr->set("name", cx.string(qualified));
    attr->set("nodeName", cx.string(qualified));
    attr->set("localName", cx.string(local));
    attr->set("prefix", prefix.empty() ? value::null() : cx.string(prefix));
    attr->set("namespaceURI", ns.empty() ? value::null() : cx.string(ns));
    attr->set("nodeType", value::number(2));
    attr->set("baseURI", cx.string(secondary_ ? std::string{"about:blank"} : location_href_));
    // Always true since DOM4 removed the other answer.
    attr->set("specified", value::boolean(true));
    if (const value proto = interface_prototype("Attr"); proto.is_object()) {
        attr->prototype = proto;
    }
    bind_attr_object(cx, *attr, owner, held);
    if (owner) { attr_objects_[owner.key()].emplace_back(key, attr); }
    return value::object(attr);
}

/// Drops the cached Attr object for (\p ns, \p local) on \p owner, so a later
/// attribute with that name gets a fresh Attr.
void dom_bindings::forget_attr_object(node_id owner, std::string_view ns, std::string_view local) {
    const auto it = attr_objects_.find(owner.key());
    if (it == attr_objects_.end()) { return; }
    const std::string key = std::string{ns} + '\0' + std::string{local};
    std::erase_if(it->second, [&key](const auto & entry) { return entry.first == key; });
}

// Binds the location-dependent half of an Attr: the three spellings of its
// value and its ownerElement. Re-run when that changes: `setAttributeNode`
// attaches the very object the page holds, `removeAttributeNode` detaches it.
void dom_bindings::bind_attr_object(context & cx, script::object_object & attr, node_id owner,
                                    const attribute & held) {
    const std::string ns{atoms_->text(held.ns)};
    const std::string local{attribute_local_name(*atoms_, held)};
    const atom name = held.name;
    const atom uri = held.ns;
    // The document that bound it: an Attr moved to another document's element
    // through setNamedItem is adopted, and its ownerDocument says so
    // (attributes-namednodemap-cross-document.window.js).
    attr.set("ownerDocument", document_);
    // A detached Attr's value is one string behind the three spellings, so a
    // page that writes `attr.value` and reads `attr.nodeValue` sees the write.
    // An attached one reads the element and remembers what it read, which is
    // the value it keeps once `removeAttribute` has taken the attribute away
    // (DOM 4.9.2: a removed Attr keeps its value and loses its element).
    const auto shared = std::make_shared<std::string>(held.value);
    for (const char * spelling : {"value", "nodeValue", "textContent"}) {
        const std::string property{spelling};
        (void)attr.erase_accessor(property);
        (void)attr.erase(property);
        if (!owner) {
            attr.define_accessor(
                property,
                value::object(cx.allocate<script::native_object>(
                    property,
                    [shared](context & c, std::span<value>) { return c.string(*shared); })),
                value::object(cx.allocate<script::native_object>(
                    property, [shared](context & c, std::span<value> a) {
                        *shared = arg_string(c, a, 0);
                        return value::undefined();
                    })));
            continue;
        }
        attr.define_accessor(
            property,
            value::object(cx.allocate<script::native_object>(
                property,
                [this, owner, ns, local, shared](context & c, std::span<value>) {
                    const auto txn = doc_->read();
                    const attribute * found = txn.find_attribute_ns(owner, ns, local);
                    if (found != nullptr) { *shared = found->value; }
                    return c.string(*shared);
                })),
            value::object(cx.allocate<script::native_object>(
                property,
                [this, owner, ns, local, name, uri, shared](context & c, std::span<value> a) {
                    const std::string text = arg_string(c, a, 0);
                    if (doc_->read().find_attribute_ns(owner, ns, local) == nullptr) {
                        *shared = text;
                        return value::undefined();
                    }
                    (void)doc_->set_attribute_ns(owner, uri, name, text);
                    mutated();
                    return value::undefined();
                })));
    }
    (void)attr.erase_accessor("ownerElement");
    (void)attr.erase("ownerElement");
    if (!owner) {
        attr.set("ownerElement", value::null());
        return;
    }
    // The element while it has the attribute, and null once it does not, so
    // every removal path (removeAttribute, a reflected setter writing null, a
    // token list update) detaches the Attr without knowing it exists.
    // `value_of_wrapper` is the fast path and `wrap` the fallback (wrap
    // refreshes the whole element); `el.attributes[i].ownerElement === el` is
    // an identity comparison either way.
    attr.define_accessor("ownerElement",
                         value::object(cx.allocate<script::native_object>(
                             "ownerElement",
                             [this, owner, ns, local](context & c, std::span<value>) {
                                 if (doc_->read().find_attribute_ns(owner, ns, local) == nullptr) {
                                     return value::null();
                                 }
                                 const value already = value_of_wrapper(owner);
                                 return already.is_object_like() ? already : wrap(c, owner);
                             })),
                         value::undefined());
}

// What an Attr carries, read off the object: the page may hold one this
// instance did not make (another document's), and there is no C++ type behind
// it. Returns an empty name when `given` is not an Attr.
attribute dom_bindings::attribute_of_object(context & cx, value given) {
    if (!given.is_object() || context::to_number(cx.lookup_property(given, "nodeType")) != 2) {
        return attribute{};
    }
    const value ns = cx.lookup_property(given, "namespaceURI");
    const value text = cx.lookup_property(given, "value");
    return attribute{atoms_->intern(cx.to_string(cx.lookup_property(given, "name"))),
                     atoms_->intern(ns.is_nullish() ? std::string{} : cx.to_string(ns)),
                     text.is_undefined() ? std::string{} : cx.to_string(text)};
}

// `attr.cloneNode()` and `importNode(attr)`: a detached copy, DOM 4.4's
// cloning steps for an Attr being its four parts and nothing else.
value dom_bindings::clone_attr_object(context & cx, value given) {
    return attribute_object(cx, node_id{}, attribute_of_object(cx, given));
}

// `element.attributes`, refilled in place rather than rebuilt. The map keeps
// its identity (a page may hold on to one, and `el.attributes ===
// el.attributes` is true in a browser), so this rewrites its indexed
// properties and named properties.
//
// The map is an ordinary array-like object (indexed own properties, with
// `length` on the prototype), not a proxy, so `for (const a of el.attributes)`
// (p5's XML module) and `[].concat(...el.attributes)` (Bootstrap) iterate it
// through `context::iterable_values` (vm/call/invoke.cpp) without proxy traps.
void dom_bindings::refresh_attribute_map(context & cx, script::object_object & map, node_id id) {
    // Copy the attributes out before anything else runs: `attribute_object`
    // calls `wrap`, which opens a read_txn of its own, and nesting a read
    // inside another read is avoided everywhere in these bindings.
    std::vector<attribute> held;
    {
        const auto txn = doc_->read();
        const std::span<const attribute> current = txn.attributes(id);
        held.assign(current.begin(), current.end());
    }
    for (std::size_t i = 0; i < held.size(); ++i) {
        map.set(std::to_string(i), attribute_object(cx, id, held[i]));
    }
    // Erase the indices that went away. A removed attribute leaves its old
    // index behind, and an index past `length` that still answers makes
    // `assert_array_equals` report a length it was never given.
    for (std::size_t i = held.size(); map.find(std::to_string(i)) != nullptr; ++i) {
        (void)map.erase(std::to_string(i));
    }
    // `length` is the prototype's (see install_named_node_map), not an own
    // property: Object.getOwnPropertyNames(el.attributes) is the indices and
    // the names, which attributes.html checks verbatim.
    (void)map.erase("length");
    // The named-property rule, DOM 4.9.1 "supported property names": for an
    // HTML element in an HTML document, a qualified name with an ASCII
    // uppercase letter in it is not exposed.
    bool lowercase_only = false;
    {
        const auto txn = doc_->read();
        lowercase_only = txn.element_ns(id) == node_ns::html && !doc_->xml();
    }
    const auto exposed = [lowercase_only](std::string_view qualified) {
        if (!lowercase_only) { return true; }
        return std::ranges::none_of(qualified, [](char c) { return c >= 'A' && c <= 'Z'; });
    };
    // Named properties: `element.attributes.x` is the attribute called `x`
    // (NamedNodeMap's named property getter, tested by
    // attributes-namednodemap.html).
    //
    // Never over a method or over `length`: `setAttributeNS("foo",
    // "setNamedItem", v)` must leave `attributes.setNamedItem` a function, and
    // `setAttributeNS("foo", "length", v)` must leave `attributes.length` the
    // count. Otherwise writing an attribute could break the map it was
    // written into.
    {
        // "Is this key an array index" is object_object's own question (the
        // one that decides property order), so it is asked with its function
        // rather than with a second spelling that could disagree.
        const auto is_index = [](std::string_view key) {
            std::uint32_t at = 0;
            return script::object_object::array_index_key(key, at);
        };
        std::vector<std::string> stale;
        for (const auto & [key, current] : map.props) {
            if (key == "length" || is_index(key)) { continue; }
            // The owner slot stays: it is the map's link to its element (see
            // install_named_node_map). Everything else on this object is a
            // named property an earlier refresh put there, and it goes, so an
            // attribute that has been removed stops answering.
            if (key == named_node_map_owner_key) { continue; }
            stale.push_back(key);
        }
        for (const std::string & key : stale) { (void)map.erase(key); }
        for (std::size_t i = 0; i < held.size(); ++i) {
            const std::string qualified{atoms_->text(held[i].name)};
            if (qualified == "length" || is_index(qualified) || !exposed(qualified)) { continue; }
            // The same Attr object the index holds, not a second one:
            // `el.attributes[0] === el.attributes.x` is true in a browser, an
            // Attr being one node reached two ways. It is read back out of the
            // map rather than kept in a C++ local because the map is what
            // roots it for the GC.
            //
            // Already taken means leave it alone. That covers both the methods
            // and the case DOM's named getter is actually about: two attributes
            // may share a qualified name in different namespaces, and the first
            // is the one the name answers with.
            if (map.find(qualified) != nullptr) { continue; }
            // Also skip anything the prototype chain answers (WebIDL's named
            // property visibility, for an interface without
            // [LegacyOverrideBuiltIns]): an attribute called `toString` must
            // leave `attributes.toString` the function it inherits. The own
            // named properties were erased above, so this finds only the chain.
            if (!cx.lookup_property(value::object(&map), qualified).is_undefined()) { continue; }
            const value * indexed = map.find(std::to_string(i));
            if (indexed == nullptr) { continue; }
            // Not enumerable (WebIDL's named properties never are) and
            // configurable, as `Object.keys(el.attributes)` being the indices
            // alone requires.
            map.define(qualified, *indexed, script::attr_configurable);
        }
    }
    if (!map.prototype.is_object()) {
        // Set late, because the interfaces are built lazily: the first two
        // element wrappers exist before `EventTarget` does. See
        // ensure_dom_interfaces.
        if (const value proto = interface_prototype("NamedNodeMap"); proto.is_object()) {
            map.prototype = proto;
        }
    }
}

// The NamedNodeMap interface, on its prototype and once per realm: `length`,
// `item`, the two getters, the two setters and the two removers. Each finds
// the element through the map's hidden owner slot (the element wrapper, so
// `owner_of` names the bindings and `handle_of` the node) and runs against
// that instance. Nothing is an own property of a map but its indices and
// names, which is what WebIDL says of a legacy platform object.
void dom_bindings::install_named_node_map(context & cx) {
    auto * proto = prototype_object(interface_prototype("NamedNodeMap"));
    if (proto == nullptr || proto->find("item") != nullptr) { return; }
    // The (bindings, element) a map is over, or nothing.
    struct owner {
        dom_bindings * self = nullptr;
        node_id id;
        script::object_object * map = nullptr; // the target behind the proxy
    };
    const auto owner_of_map = [this](context & c) {
        owner found;
        value self = c.current_this();
        if (self.is_kind(script::heap_kind::proxy)) {
            self = static_cast<script::proxy_object *>(self.as_heap())->target;
        }
        if (!self.is_object()) { return found; }
        found.map = static_cast<script::object_object *>(self.as_heap());
        const value wrapper = c.lookup_property(self, std::string{named_node_map_owner_key});
        found.self = owner_of(wrapper);
        if (found.self == nullptr) { return found; }
        found.id = found.self->handle_of(wrapper);
        return found;
    };
    // A write through the map refreshes the map's own properties at once:
    // `map.setNamedItem(a); map.a` reads the map the page already holds.
    const auto refreshed = [](context & c, const owner & at) {
        if (at.map != nullptr) { at.self->refresh_attribute_map(c, *at.map, at.id); }
    };
    const auto native = [&cx](const char * name, unsigned length, script::native_fn fn) {
        auto * made = cx.allocate<script::native_object>(name, std::move(fn));
        made->define("length", value::number(length), script::attr_configurable);
        return value::object(made);
    };
    const auto method = [&](const char * name, unsigned length, script::native_fn fn) {
        proto->define(name, native(name, length, std::move(fn)), script::attr_builtin);
    };
    proto->define_accessor("length",
                           native("length", 0,
                                  [owner_of_map](context & c, std::span<value>) {
                                      const owner at = owner_of_map(c);
                                      if (!at.id) { return value::number(0); }
                                      return value::number(static_cast<double>(
                                          at.self->doc_->read().attributes(at.id).size()));
                                  }),
                           value::undefined(), script::attr_configurable);
    // One attribute, by name or by (namespace, local name), copied out of the
    // read before anything that could open another read runs.
    const auto found_by_name = [](const owner & at, std::string_view qualified) {
        const auto txn = at.self->doc_->read();
        const attribute * held =
            txn.find_attribute(at.id, at.self->attribute_key(txn, at.id, qualified));
        return held == nullptr ? std::optional<attribute>{} : std::optional<attribute>{*held};
    };
    const auto found_by_pair = [](const owner & at, std::string_view ns, std::string_view local) {
        const auto txn = at.self->doc_->read();
        const attribute * held = txn.find_attribute_ns(at.id, ns, local);
        return held == nullptr ? std::optional<attribute>{} : std::optional<attribute>{*held};
    };
    method("item", 1, [owner_of_map](context & c, std::span<value> a) {
        const owner at = owner_of_map(c);
        if (!at.id) { return value::null(); }
        std::vector<attribute> held;
        {
            const auto txn = at.self->doc_->read();
            const std::span<const attribute> current = txn.attributes(at.id);
            held.assign(current.begin(), current.end());
        }
        const double index = arg_number(a, 0);
        if (!(index >= 0) || index >= static_cast<double>(held.size())) { return value::null(); }
        return at.self->attribute_object(c, at.id, held[static_cast<std::size_t>(index)]);
    });
    method("getNamedItem", 1, [owner_of_map, found_by_name](context & c, std::span<value> a) {
        const owner at = owner_of_map(c);
        if (!at.id) { return value::null(); }
        const std::optional<attribute> held = found_by_name(at, arg_string(c, a, 0));
        return held ? at.self->attribute_object(c, at.id, *held) : value::null();
    });
    method("getNamedItemNS", 2, [owner_of_map, found_by_pair](context & c, std::span<value> a) {
        const owner at = owner_of_map(c);
        if (!at.id) { return value::null(); }
        const std::optional<attribute> held =
            found_by_pair(at, namespace_argument(c, a, 0), arg_string(c, a, 1));
        return held ? at.self->attribute_object(c, at.id, *held) : value::null();
    });
    // `setNamedItem` and `setNamedItemNS` are the same operation: DOM 4.9.2
    // defines both as "set an attribute", keyed on the (namespace, local name)
    // pair whichever spelling was used. What an Attr carries is read off the
    // object: an Attr that is some other element's is an InUseAttributeError,
    // one that is already this element's is returned unchanged, and the one
    // it replaces is detached and returned.
    const auto set_named = [owner_of_map, found_by_pair, refreshed](context & c,
                                                                    std::span<value> a) {
        const owner at = owner_of_map(c);
        const value given = arg(a, 0);
        if (!at.id) { return value::null(); }
        if (!given.is_object()) {
            c.throw_error("TypeError", "setNamedItem: the argument is not an Attr");
            return value::null();
        }
        dom_bindings & self = *at.self;
        // Any other element's, another document's included: the owner is
        // read off the Attr, not looked up in this document's wrappers.
        const value owner_now = c.lookup_property(given, "ownerElement");
        if (owner_now.is_object_like() && self.handle_of(owner_now) != at.id) {
            self.throw_dom_exception(c, "InUseAttributeError",
                                     "setNamedItem: the attribute belongs to another element");
            return value::null();
        }
        const value ns_property = c.lookup_property(given, "namespaceURI");
        const std::string ns = ns_property.is_nullish() ? std::string{} : c.to_string(ns_property);
        const std::string qualified = c.to_string(c.lookup_property(given, "name"));
        const value text = c.lookup_property(given, "value");
        const qualified_name split = split_qualified(qualified);
        const std::string_view local = ns.empty() ? std::string_view{qualified} : split.local;
        const std::optional<attribute> replaced = found_by_pair(at, ns, local);
        value old = value::null();
        if (replaced) {
            old = self.attribute_object(c, at.id, *replaced);
            if (old.bits() == given.bits()) { return given; }
            self.forget_attr_object(at.id, ns, local);
            self.bind_attr_object(c, *static_cast<script::object_object *>(old.as_heap()),
                                  node_id{}, *replaced);
        }
        const attribute written{self.atoms_->intern(qualified), self.atoms_->intern(ns),
                                text.is_undefined() ? std::string{} : c.to_string(text)};
        (void)self.doc_->set_attribute_ns(at.id, written.ns, written.name, written.value);
        self.mutated();
        auto * attached = static_cast<script::object_object *>(given.as_heap());
        self.bind_attr_object(c, *attached, at.id, written);
        self.attr_objects_[at.id.key()].emplace_back(ns + '\0' + std::string{local}, attached);
        refreshed(c, at);
        return old;
    };
    method("setNamedItem", 1, set_named);
    method("setNamedItemNS", 1, set_named);
    // ...and removing one throws when there is nothing to remove: "if attr is
    // null, throw a NotFoundError". The object the page may hold comes back,
    // detached with the value it had.
    const auto detach = [](context & c, dom_bindings & self, node_id id, const attribute & held) {
        const value gone = self.attribute_object(c, id, held);
        self.forget_attr_object(id, self.atoms_->text(held.ns),
                                attribute_local_name(*self.atoms_, held));
        self.bind_attr_object(c, *static_cast<script::object_object *>(gone.as_heap()), node_id{},
                              held);
        return gone;
    };
    method("removeNamedItem", 1,
           [this, owner_of_map, found_by_name, detach, refreshed](context & c, std::span<value> a) {
               const owner at = owner_of_map(c);
               const std::string qualified = arg_string(c, a, 0);
               const std::optional<attribute> held =
                   at.id ? found_by_name(at, qualified) : std::optional<attribute>{};
               if (!held) {
                   this->throw_dom_exception(c, "NotFoundError",
                                             "removeNamedItem: no attribute called '" + qualified +
                                                 "'");
                   return value::null();
               }
               const value gone = detach(c, *at.self, at.id, *held);
               (void)at.self->doc_->remove_attribute(at.id, held->name);
               at.self->mutated();
               refreshed(c, at);
               return gone;
           });
    method("removeNamedItemNS", 2,
           [this, owner_of_map, found_by_pair, detach, refreshed](context & c, std::span<value> a) {
               const owner at = owner_of_map(c);
               const std::string ns = namespace_argument(c, a, 0);
               const std::string local = arg_string(c, a, 1);
               const std::optional<attribute> held =
                   at.id ? found_by_pair(at, ns, local) : std::optional<attribute>{};
               if (!held) {
                   this->throw_dom_exception(c, "NotFoundError",
                                             "removeNamedItemNS: no attribute called '" + local +
                                                 "'");
                   return value::null();
               }
               const value gone = detach(c, *at.self, at.id, *held);
               (void)at.self->doc_->remove_attribute_ns(at.id, ns, local);
               at.self->mutated();
               refreshed(c, at);
               return gone;
           });
}

// "Validate and extract", DOM 4.9, shared by `setAttributeNS` and the two
// `setNamedItemNS` spellings. Returns false after it has already thrown, as
// `pre_insert_valid` (element/pre_insert.cpp) does, so the caller only has to
// return.
//
// The order of the two halves matters: the shape of the name is checked
// before the namespace, so `setAttributeNS(XMLNS, "", v)` is an
// InvalidCharacterError and not the NamespaceError its namespace would
// otherwise earn. Both orderings throw; only one throws what WPT asserts.
//
// A prefix is checked for being non-empty and writable and nothing else: it
// is the local name that has to be a name, and the prefix is only a label in
// front of it. The same rule, in the same order, as createAttributeNS in
// document/as_node.cpp.
bool dom_bindings::validate_and_extract(context & cx, std::string_view where,
                                        const std::string & ns, const std::string & qualified) {
    const qualified_name split = split_qualified(qualified);
    if ((split.has_colon && !is_valid_namespace_prefix(split.prefix)) ||
        !is_valid_attribute_name(split.local)) {
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

// All on Element.prototype: the attribute API is Element's and nothing else's,
// so a text node has none of it. ProcessingInstruction gets its own seven below.
void dom_bindings::install_attribute_methods(context & cx) {
    const auto method = [&](const char * name, unsigned length, script::native_fn fn) {
        define_operation(cx, {"Element"}, name, length, std::move(fn));
    };

    method("setAttribute", 2, [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        const std::string name = arg_string(c, args, 0);
        if (!is_valid_attribute_name(name)) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "setAttribute: '" + name + "' is not a valid attribute name");
            return value::undefined();
        }
        if (!id) { return value::undefined(); }
        const std::string text = arg_string(c, args, 1);
        // The first attribute with this qualified name, whatever its
        // namespace, and only its value changes: the DOM layer's set_attribute
        // implements "Setting the same attribute with another prefix should
        // not change the prefix" (a WPT subtest by that name).
        (void)set_element_attribute(*doc_, id, name, text);
        mutated();
        return value::undefined();
    });
    // The namespaced half of the attribute API. Every one of these matches on
    // the pair (namespace, local name), in which the prefix takes no part, and
    // none of them folds case: an element may hold `x` in no namespace and `x`
    // in two others at once, and each of the three lookups has a different
    // right answer (Element-removeAttribute.html).
    method("setAttributeNS", 3, [this](context & c, std::span<value> args) {
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
        // Interned as written. The qualified name is the attribute's name, and
        // folding it would lose the case `setAttributeNS("", "ALIGN", ...)`
        // deliberately keeps (see attribute_key).
        (void)doc_->set_attribute_ns(id, atoms_->intern(ns), atoms_->intern(qualified),
                                     arg_string(c, args, 2));
        mutated();
        return value::undefined();
    });
    method("getAttributeNS", 2, [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::null(); }
        const std::string ns = namespace_argument(c, args, 0);
        const auto txn = doc_->read();
        const attribute * held = txn.find_attribute_ns(id, ns, arg_string(c, args, 1));
        return held == nullptr ? value::null() : c.string(held->value);
    });
    method("hasAttributeNS", 2, [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const std::string ns = namespace_argument(c, args, 0);
        return value::boolean(doc_->read().has_attribute_ns(id, ns, arg_string(c, args, 1)));
    });
    method("removeAttributeNS", 2, [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const std::string ns = namespace_argument(c, args, 0);
        // A local name, not a qualified one: `removeAttributeNS(XML, "a:bb")`
        // removes nothing (Element-removeAttributeNS.html).
        (void)doc_->remove_attribute_ns(id, ns, arg_string(c, args, 1));
        mutated();
        return value::undefined();
    });
    // `hasAttributes()`: whether the element has any attributes, answered
    // without materialising the map.
    method("hasAttributes", 0, [this](context & c, std::span<value>) {
        (void)c;
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        return value::boolean(!doc_->read().attributes(id).empty());
    });
    // The Attr-returning versions of the same lookups. `getAttributeNodeNS` is
    // how Attr-prefix.html reads the prefix and the namespace, the two things
    // only an Attr can report.
    method("getAttributeNode", 1, [this](context & c, std::span<value> args) {
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
    method("getAttributeNodeNS", 2, [this](context & c, std::span<value> args) {
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
    // `removeNamedItem` under other names (DOM 4.9 defines each pair in terms
    // of the same "set an attribute" and "remove an attribute"), so they are
    // forwarded to the map rather than written twice, which keeps the
    // returned old Attr identical.
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
    method("setAttributeNode", 1, through_map("setNamedItem"));
    method("setAttributeNodeNS", 1, through_map("setNamedItemNS"));
    method("removeAttributeNode", 1, [this](context & c, std::span<value> args) {
        // Not through the map: `removeAttributeNode` takes the Attr itself and
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
        // The argument is the return value, rebound to nowhere with the value
        // it had and forgotten by this element, so a later attribute of the
        // same name is a new Attr.
        forget_attr_object(id, ns, local);
        bind_attr_object(c, *static_cast<script::object_object *>(given.as_heap()), node_id{},
                         *held);
        return given;
    });
    // `toggleAttribute(name, force)` returns whether the attribute is present
    // afterwards.
    method("toggleAttribute", 1, [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        const std::string name = arg_string(c, args, 0);
        if (!is_valid_attribute_name(name)) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "toggleAttribute: '" + name + "' is not a valid attribute name");
            return value::boolean(false);
        }
        if (!id) { return value::boolean(false); }
        // `force` is optional: absent means flip, otherwise its truthiness
        // decides. Unlike the ProcessingInstruction version below, an explicit
        // `undefined` counts as given (and so as false) here.
        const auto result = toggle_element_attribute(
            *doc_, id, name,
            args.size() > 1 ? std::optional{context::truthy(args[1])} : std::nullopt);
        if (!result->update || *result->update) { mutated(); }
        return value::boolean(result->present);
    });
    // `getAttributeNames()`: the qualified names, in attribute order.
    method("getAttributeNames", 0, [this](context & c, std::span<value>) {
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
    method("getAttribute", 1, [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::null(); }
        const auto text = get_element_attribute(*doc_, id, arg_string(c, args, 0));
        return text ? c.string(*text) : value::null();
    });
    // removeAttribute/hasAttribute look the name up with attribute_key, so an
    // HTML element folds case the same way getAttribute does.
    method("removeAttribute", 1, [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const auto txn = doc_->read();
        (void)doc_->remove_attribute(id, attribute_key(txn, id, arg_string(c, args, 0)));
        mutated();
        return value::undefined();
    });
    method("hasAttribute", 1, [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const auto txn = doc_->read();
        return value::boolean(
            txn.has_attribute(id, attribute_key(txn, id, arg_string(c, args, 0))));
    });

    // --- ProcessingInstruction's seven, DOM §4.13 ----------------------------
    //
    // A PI's `data` is `name="value"` pairs to the page that wrote it (an
    // `<?xml-stylesheet href=...?>` has always been read that way), and the
    // DOM now says so: an attribute map parsed from the data, and the same
    // seven operations Element has over it. The map is the DOM library's
    // (document::update_pi_attributes / update_pi_data keep it and `data` in
    // step, and say why it is stored rather than derived); what is here is the IDL.
    //
    // These are not Element's natives with a wider receiver check: a PI's
    // names are case-sensitive whatever the document (`getAttribute("X")`
    // misses `x`, `BLAbla` and `blabla` are two attributes), where
    // attribute_key folds an HTML element's, and none of the namespaced half
    // exists on a PI.
    const auto pi_method = [&](const char * name, unsigned length, script::native_fn fn) {
        define_operation(cx, {"ProcessingInstruction"}, name, length, std::move(fn));
    };
    // The receiver, when it is a PI: Element.prototype.getAttribute.call(pi)
    // is not this method, and this method on an element does nothing.
    const auto pi_receiver = [this](context & c) {
        const node_id id = receiver(c);
        if (!id) { return node_id{}; }
        const auto txn = doc_->read();
        return txn.kind(id).value_or(node_kind::element) == node_kind::processing_instruction
                   ? id
                   : node_id{};
    };
    pi_method("hasAttributes", 0, [this, pi_receiver](context & c, std::span<value>) {
        const node_id id = pi_receiver(c);
        return value::boolean(id && !doc_->read().attributes(id).empty());
    });
    pi_method("getAttributeNames", 0, [this, pi_receiver](context & c, std::span<value>) {
        value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        const node_id id = pi_receiver(c);
        if (!id) { return out; }
        const auto txn = doc_->read();
        for (const attribute & held : txn.attributes(id)) {
            items->items.push_back(c.string(std::string{atoms_->text(held.name)}));
        }
        return out;
    });
    pi_method("getAttribute", 1, [this, pi_receiver](context & c, std::span<value> args) {
        const node_id id = pi_receiver(c);
        if (!id) { return value::null(); }
        const auto txn = doc_->read();
        const attribute * held = txn.find_attribute(id, atoms_->intern(arg_string(c, args, 0)));
        return held == nullptr ? value::null() : c.string(held->value);
    });
    pi_method("hasAttribute", 1, [this, pi_receiver](context & c, std::span<value> args) {
        const node_id id = pi_receiver(c);
        return value::boolean(
            id && doc_->read().has_attribute(id, atoms_->intern(arg_string(c, args, 0))));
    });
    // The two that write check the name first, as Element's do: it is the same
    // "valid attribute local name" and the same InvalidCharacterError, and the
    // check comes before the receiver so a bad name throws on any `this`.
    pi_method("setAttribute", 2, [this, pi_receiver](context & c, std::span<value> args) {
        const std::string name = arg_string(c, args, 0);
        if (!is_valid_attribute_name(name)) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "setAttribute: '" + name + "' is not a valid attribute name");
            return value::undefined();
        }
        const node_id id = pi_receiver(c);
        if (!id) { return value::undefined(); }
        (void)doc_->set_attribute(id, atoms_->intern(name), arg_string(c, args, 1));
        mutated();
        return value::undefined();
    });
    pi_method("removeAttribute", 1, [this, pi_receiver](context & c, std::span<value> args) {
        const node_id id = pi_receiver(c);
        if (!id) { return value::undefined(); }
        (void)doc_->remove_attribute(id, atoms_->intern(arg_string(c, args, 0)));
        mutated();
        return value::undefined();
    });
    pi_method("toggleAttribute", 1, [this, pi_receiver](context & c, std::span<value> args) {
        const std::string name = arg_string(c, args, 0);
        if (!is_valid_attribute_name(name)) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "toggleAttribute: '" + name + "' is not a valid attribute name");
            return value::boolean(false);
        }
        const node_id id = pi_receiver(c);
        if (!id) { return value::boolean(false); }
        const atom key = atoms_->intern(name);
        const bool present = doc_->read().has_attribute(id, key);
        // `force` is an optional boolean: an explicit `undefined` is "not
        // given" (WebIDL), not false.
        const bool given = args.size() > 1 && !args[1].is_undefined();
        const bool want = given ? context::truthy(args[1]) : !present;
        if (want == present) { return value::boolean(present); }
        if (want) {
            (void)doc_->set_attribute(id, key, "");
        } else {
            (void)doc_->remove_attribute(id, key);
        }
        mutated();
        return value::boolean(want);
    });
}

} // namespace ctbrowser::shell
