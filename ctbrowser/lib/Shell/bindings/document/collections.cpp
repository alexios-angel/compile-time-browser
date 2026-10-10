//===- collections.cpp - Element walks and live collections -----*- C++ -*-===//
//
// dom_bindings' element collections: the walks by tag, class and name, the
// HTML element lookups (body, title), and the live HTMLCollection/NodeList
// proxy they are handed back as.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

#include <ctbrowser/dom/token_list.hpp>

namespace ctbrowser::shell {

using namespace detail;

// HTML defines these lookups by local name and namespace, which `find_by_tag`
// below cannot check. An SVG `<title>` interns to the same atom as an HTML
// `<title>`, so without the namespace test `<svg><title>Chart</title></svg>`
// made `document.title` answer "Chart".
namespace {

/// The part of a qualified name after the first `:`, or all of it.
[[nodiscard]] std::string_view local_name_of(std::string_view qualified) {
    const std::size_t colon = qualified.find(':');
    return colon == std::string_view::npos ? qualified : qualified.substr(colon + 1);
}

} // namespace

/// The first HTML-namespace element in tree order whose local name is
/// `local`, or an empty id.
node_id dom_bindings::first_html_element(std::string_view local) {
    const auto txn = doc_->read();
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (found) { return; }
        if (const auto tagged = txn.tag(at); tagged.has_value() &&
                                             txn.element_ns(at) == node_ns::html &&
                                             local_name_of(atoms_->text(*tagged)) == local) {
            found = at;
            return;
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

/// Every HTML-namespace element whose local name is `local`, in tree order.
std::vector<node_id> dom_bindings::all_html_elements(std::string_view local) {
    const auto txn = doc_->read();
    std::vector<node_id> found;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (const auto tagged = txn.tag(at); tagged.has_value() &&
                                             txn.element_ns(at) == node_ns::html &&
                                             local_name_of(atoms_->text(*tagged)) == local) {
            found.push_back(at);
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

/// "The body element", HTML 4.2.3: the first child of the document element
/// that is a `body` or a `frameset`. Not the first `<body>` in the document: a
/// `<body>` inside a `<div>` is not the document's body, and WPT
/// Document.body.html asserts that by building one.
node_id dom_bindings::body_element() {
    const auto txn = doc_->read();
    const node_id root = txn.root();
    // "The body element of a document is the first child of the html element
    // that is either a body element or a frameset element": a root that is
    // not an html element in the HTML namespace has no body, whatever its
    // children are called (WPT Document.body.html).
    if (txn.element_ns(root) != node_ns::html || txn.local_name(root) != "html") {
        return node_id{};
    }
    for (const node_id child : txn.children(root)) {
        if (txn.element_ns(child) != node_ns::html) { continue; }
        const auto tagged = txn.tag(child);
        if (!tagged.has_value()) { continue; }
        const std::string_view local = local_name_of(atoms_->text(*tagged));
        if (local == "body" || local == "frameset") { return child; }
    }
    return node_id{};
}

/// "The title element", HTML 4.2.2. A document whose root is `<svg>` takes its
/// title from that root's own first SVG `<title>` child, not from any HTML
/// title anywhere; every other document takes the first HTML title element in
/// tree order, wherever it is. WPT document.title-01.html tests that
/// "wherever": it removes the head, appends a `<title>` to the body, and
/// expects the title to be that one.
node_id dom_bindings::title_element() {
    const auto txn = doc_->read();
    const node_id root = txn.root();
    const auto is_svg_root = [&] {
        if (txn.element_ns(root) != node_ns::svg) { return false; }
        const auto tagged = txn.tag(root);
        return tagged.has_value() && local_name_of(atoms_->text(*tagged)) == "svg";
    };
    if (is_svg_root()) {
        for (const node_id child : txn.children(root)) {
            if (txn.element_ns(child) != node_ns::svg) { continue; }
            const auto tagged = txn.tag(child);
            if (tagged.has_value() && local_name_of(atoms_->text(*tagged)) == "title") {
                return child;
            }
        }
        return node_id{};
    }
    // Not first_html_element("title"): it opens its own read transaction, and
    // nothing else in these bindings nests one read inside another.
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (found) { return; }
        if (const auto tagged = txn.tag(at); tagged.has_value() &&
                                             txn.element_ns(at) == node_ns::html &&
                                             local_name_of(atoms_->text(*tagged)) == "title") {
            found = at;
            return;
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, root);
    return found;
}

/// The first element in tree order whose tag atom is `tag` lowercased, in any
/// namespace.
node_id dom_bindings::find_by_tag(std::string_view tag) {
    const auto txn = doc_->read();
    const atom want = atoms_->intern_lower(tag);
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (!found && txn.tag(at).value_or(atom{}) == want) { found = at; }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

namespace {

/// The index a property key names, if it is a whole non-negative integer and
/// nothing else. "1x" is not index 1, and neither is " 1", "+1" or "1.0"; a
/// collection has to say no to those or `hasOwnProperty` starts agreeing to
/// keys nobody indexed. The index must also be below 2^32 - 1, WebIDL's "array
/// index": "4294967295" is a name, answered with the element whose id it is.
[[nodiscard]] std::optional<std::size_t> whole_index(std::string_view key) {
    if (key.empty() || (key.size() > 1 && key.front() == '0')) { return std::nullopt; }
    std::size_t at = 0;
    const char * first = key.data();
    const char * last = first + key.size();
    const auto [stopped, failed] = std::from_chars(first, last, at);
    if (failed != std::errc{} || stopped != last || at >= 4294967295u) { return std::nullopt; }
    return at;
}

} // namespace

/// The namespace URI of element `id`, or "" for the null namespace.
std::string dom_bindings::namespace_of(node_id id) const {
    if (const auto it = namespaces_.find(id.key()); it != namespaces_.end()) { return it->second; }
    const auto txn = doc_->read();
    switch (txn.element_ns(id)) {
    case node_ns::svg: return std::string{svg_namespace};
    case node_ns::html: return std::string{xhtml_namespace};
    // An `other` element the parsers made (MathML from the HTML parser, a
    // page's own vocabulary from the XML one) has its URI on the document;
    // one createElementNS made is in `namespaces_` above. A stale handle
    // resolves to neither, and gets the null namespace.
    case node_ns::other: return std::string{atoms_->text(txn.element_namespace(id))};
    }
    return {};
}

/// The elements carrying every class in `tokens`, in tree order: under
/// element `root` (excluding it), or in the whole document when `root` is
/// empty.
std::vector<node_id> dom_bindings::all_by_class(node_id root,
                                                const std::vector<std::string> & tokens) {
    if (tokens.empty()) { return {}; }
    const auto txn = doc_->read();
    const atom class_attribute = atoms_->intern("class");
    std::vector<node_id> found;
    // Case-sensitive in standards mode, ASCII-case-insensitive in quirks mode
    // (DOM 4.5 "list of elements with class names"); WPT
    // getElementsByClassName-14.htm is a doctype-less page with `class="a A"`.
    const bool quirks = doc_->quirks();
    const auto has_every = [&](node_id at) {
        const std::vector<std::string> held =
            parse_ordered_tokens(txn.attribute_value(at, class_attribute));
        return std::ranges::all_of(tokens, [&](const std::string & want) {
            return std::ranges::any_of(held, [&](const std::string & one) {
                return quirks ? ascii_iequals(one, want) : one == want;
            });
        });
    };
    const auto walk = [&](auto && self, node_id at, bool include) -> void {
        // Elements only, and never the element the search started from: a
        // search rooted at an element looks at its descendants.
        if (include && txn.tag(at).has_value() && has_every(at)) { found.push_back(at); }
        for (const node_id child : txn.children(at)) { self(self, child, true); }
    };
    // A document-wide search starts at txn.root(), the document element (the
    // `<html>`), not the Document node above it. So it must include its start,
    // or `document.getElementsByClassName` could never return the element most
    // often given a class. An element-rooted search excludes its start.
    walk(walk, root ? root : txn.root(), !root);
    return found;
}

/// Every HTML element whose `name` attribute is `name`, in tree order
/// (getElementsByName).
std::vector<node_id> dom_bindings::all_by_name(std::string_view name) {
    const auto txn = doc_->read();
    const atom key = atoms_->intern("name");
    std::vector<node_id> found;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (txn.tag(at).has_value() && txn.element_ns(at) == node_ns::html &&
            txn.has_attribute(at, key) && txn.attribute_value(at, key) == name) {
            found.push_back(at);
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

// The live collection and its property model: WebIDL's "legacy platform
// object" with an indexed getter and, for an HTMLCollection, a named one.
//
// What a page can observe (WPT Document-Element-getElementsByTagName.js
// observes all of it): `length`, `item` and `namedItem` are on the prototype
// (`list.item = "pass"` shadows the inherited one with an expando); every
// index and every exposed id/name is an own property of the collection, so
// `Object.getOwnPropertyNames(list)` is ["0", "1", "x"] and nothing else, an
// index enumerable and a name not; an index cannot be assigned; and all of it
// is live, recomputed as the document changes.
//
// How: a proxy over a target that carries the own properties. The traps
// refresh the target first, but only when the document's version moved since
// the last refresh, which keeps `for (i < list.length) list[i]` from walking
// the document per iteration. A refresh re-runs `members`, adds or removes
// the index accessors at the tail (an index reads `members[i]`, so the
// accessor itself never goes stale) and re-derives the named keys. The
// prototype's three members find the collection's state through one hidden
// symbol-keyed own property (getOwnPropertyNames does not report symbol
// keys), so nothing about the walk lives on the prototype.
//
// An index write is refused by the `set` trap returning false. The VM treats
// that as a rejected store (vm/objects/store.cpp): ignored in sloppy code, a
// TypeError in strict code.
namespace {

constexpr std::string_view collection_state_key = CTBROWSER_SYMBOL_KEY("sym:ctbrowser:collection");

/// The collection's hidden state native, or undefined when `self` is not a
/// collection. The state is one native answering three questions, closing over
/// the C++ side of a collection: state("length"), state("item", i) and
/// state("namedItem", name).
///
/// This is the brand check, WebIDL 3.7.6 step 1: the state is an own property
/// of the collection (of the proxy's target, read without a trap), so an
/// object that merely inherits from a collection is not one, and
/// `Object.create(list).length` is a TypeError (WPT
/// HTMLCollection-as-prototype.html). The proxy's get trap passes the
/// receiver on, which lets this see the object the read started from.
[[nodiscard]] value collection_state_of(value self) {
    value holder = self;
    if (self.is_kind(script::heap_kind::proxy)) {
        holder = static_cast<script::proxy_object *>(self.as_heap())->target;
    }
    if (!holder.is_object()) { return value::undefined(); }
    const value * held =
        static_cast<script::object_object *>(holder.as_heap())->find(collection_state_key);
    return held == nullptr ? value::undefined() : *held;
}

/// Asks `self`'s collection state `what`, throwing "Illegal invocation" when
/// `self` is not a collection.
[[nodiscard]] value ask_collection(context & cx, value self, const char * what, value argument) {
    const value state = collection_state_of(self);
    if (!state.is_callable()) {
        cx.throw_error("TypeError", "Illegal invocation");
        return value::undefined();
    }
    const value args[2] = {cx.string(what), argument};
    return cx.call(state, args, value::undefined());
}

/// Installs `length`, `item` and (when `named`) `namedItem` on a collection
/// interface's prototype, once. They are enumerable, as WebIDL 3.7.6-7 makes
/// every regular operation and attribute on an interface prototype:
/// `for (p in document.forms)` lists the indices and then item, length and
/// namedItem (WPT document.forms.html); `@@iterator` alone is not enumerable.
void install_collection_prototype(context & cx, script::object_object & proto, bool named) {
    if (proto.find("item") != nullptr) { return; }
    const auto native = [&cx](const char * name, unsigned length, script::native_fn fn) {
        auto * made = cx.allocate<script::native_object>(name, std::move(fn));
        made->define("length", value::number(length), script::attr_configurable);
        return value::object(made);
    };
    proto.define_accessor("length",
                          native("length", 0,
                                 [](context & c, std::span<value>) {
                                     const value n = ask_collection(c, c.current_this(), "length",
                                                                    value::undefined());
                                     return n.is_number() ? n : value::number(0);
                                 }),
                          value::undefined(), script::attr_enumerable | script::attr_configurable);
    proto.define("item",
                 native("item", 1,
                        [](context & c, std::span<value> a) {
                            const value found =
                                ask_collection(c, c.current_this(), "item", arg(a, 0));
                            return found.is_undefined() ? value::null() : found;
                        }),
                 script::attr_default);
    dom_bindings::install_iterable_declaration(cx, proto, named);
    if (named) {
        proto.define("namedItem",
                     native("namedItem", 1,
                            [](context & c, std::span<value> a) {
                                const value found =
                                    ask_collection(c, c.current_this(), "namedItem", arg(a, 0));
                                return found.is_undefined() ? value::null() : found;
                            }),
                     script::attr_default);
    }
}

} // namespace

/// The iterable declaration (WebIDL 3.7.10): `[Symbol.iterator]`, and for a
/// value iterable that is not a named collection (NodeList, DOMTokenList)
/// forEach/keys/values/entries too. Static because DOMTokenList.prototype is
/// built in element/child_views.cpp over the same shape: an object with
/// `length` and an indexed getter.
///
/// The members are Array.prototype's own function objects: for an interface
/// with an indexed property getter, @@iterator is %Array.prototype.values% and
/// the four value-iterator members are Array.prototype's (WebIDL 3.7.10.1 and
/// 3.7.11); WPT Node-childNodes.html and DOMTokenList-iteration.html compare
/// them by identity. They are generic over array-likes and live, reading
/// `length` and the index through the collection on every step
/// (builtins/collections/array_iteration.cpp), so `list.values()` sees what a
/// loop body appended.
void dom_bindings::install_iterable_declaration(context & cx, script::object_object & proto,
                                                bool named_only) {
    if (proto.find(CTBROWSER_SYMBOL_KEY("iterator")) != nullptr) { return; }
    const value array = cx.global("Array");
    if (!array.is_object_like()) { return; }
    const value array_proto = cx.lookup_property(array, "prototype");
    const value values = cx.lookup_property(array_proto, "values");
    if (!values.is_callable()) { return; }
    proto.define(CTBROWSER_SYMBOL_KEY("iterator"), values, script::attr_builtin);
    if (named_only) { return; }
    for (const char * name : {"keys", "values", "entries", "forEach"}) {
        const value member = cx.lookup_property(array_proto, name);
        if (member.is_callable()) { proto.define(name, member, script::attr_default); }
    }
}

/// A live collection of interface `interface_name` whose members are what
/// `members` returns, re-run whenever the document's version changes. See the
/// property model above.
value dom_bindings::make_live_collection(context & cx,
                                         std::function<std::vector<node_id>()> members,
                                         std::string_view interface_name) {
    auto * target = cx.allocate<script::object_object>();
    auto * handler = cx.allocate<script::object_object>();
    // The interface prototypes looked up below may not exist yet: a page can
    // ask for a collection (`document.images`) before anything was wrapped.
    // Without them `instanceof HTMLCollection` would be false.
    ensure_dom_interfaces(cx);
    // The derived collections build on a base: HTMLOptionsCollection and
    // HTMLFormControlsCollection chain to HTMLCollection, and RadioNodeList to
    // NodeList. So `length`, `item` and `namedItem` go on the base prototype
    // once and the derived one carries only its own members.
    const bool named = interface_name != "NodeList" && interface_name != "RadioNodeList";
    const bool form_controls = interface_name == "HTMLFormControlsCollection";
    if (const value base = interface_prototype(named ? "HTMLCollection" : "NodeList");
        base.is_object()) {
        install_collection_prototype(cx, *static_cast<script::object_object *>(base.as_heap()),
                                     named);
    }
    if (const value proto = interface_prototype(interface_name); proto.is_object()) {
        target->prototype = proto;
    }

    struct state {
        std::function<std::vector<node_id>()> live;
        std::uint64_t version = 0; // the document version `members` is of
        std::vector<node_id> members;
        std::vector<std::string> names; // the named own keys installed
        std::size_t indices = 0;        // the index own keys installed: 0..indices-1
    };
    const auto held = std::make_shared<state>();
    held->live = std::move(members);
    // A member is the wrapper it already has, without the refresh `wrap`
    // does: that re-measures the element's box on every read, and `list[j]`
    // in a loop is the hottest thing a page does with a collection. A wrapper
    // held in a variable is no fresher, and every wrapper is refreshed before
    // a frame and a dispatch regardless.
    const auto member = [this](context & c, node_id id) {
        const value already = value_of_wrapper(id);
        return already.is_object() ? already : wrap(c, id);
    };

    // The first member whose id (or, for an HTML element, name) is `want`.
    const auto named_member = [this](const std::vector<node_id> & found, std::string_view want) {
        const auto txn = doc_->read();
        const atom id = atoms_->intern("id");
        const atom name = atoms_->intern("name");
        for (const node_id at : found) {
            if (txn.attribute_value(at, id) == want) { return at; }
            if (txn.element_ns(at) == node_ns::html && txn.attribute_value(at, name) == want) {
                return at;
            }
        }
        return node_id{};
    };
    // For HTMLFormControlsCollection (HTML 4.10.20.1), every such member: one
    // is the element, several are a RadioNodeList over them, none is null.
    const auto all_named = [this](const std::vector<node_id> & found, std::string_view want) {
        std::vector<node_id> matching;
        const auto txn = doc_->read();
        const atom id = atoms_->intern("id");
        const atom name = atoms_->intern("name");
        for (const node_id at : found) {
            if (txn.attribute_value(at, id) == want || txn.attribute_value(at, name) == want) {
                matching.push_back(at);
            }
        }
        return matching;
    };
    const auto named_value = [this, named_member, form_controls, all_named,
                              held](context & c, const std::vector<node_id> & found,
                                    std::string_view want) -> value {
        if (!form_controls) { return wrap(c, named_member(found, want)); }
        const std::vector<node_id> matching = all_named(found, want);
        if (matching.empty()) { return value::null(); }
        if (matching.size() == 1) { return wrap(c, matching.front()); }
        // Live over the form's controls, re-walked: the ones answering to the
        // name at each read.
        return make_live_collection(
            c, [all_named, held, key = std::string{want}] { return all_named(held->live(), key); },
            "RadioNodeList");
    };

    // Brings the target's own properties up to date when the document moved.
    // `refresh` and the accessors take the calling context as a parameter
    // rather than capturing `cx`: they outlive this call.
    const auto refresh = [this, target, held, named, named_value, member](context & c) {
        const std::uint64_t now = doc_->version();
        if (now == held->version && held->version != 0) { return; }
        held->version = now;
        held->members = held->live();
        // The indices: only the tail changes, and an accessor reads the
        // member it names at call time.
        for (std::size_t i = held->indices; i < held->members.size(); ++i) {
            target->define_accessor(std::to_string(i),
                                    native(c, std::to_string(i),
                                           [held, i, member](context & inner, std::span<value>) {
                                               return i < held->members.size()
                                                          ? member(inner, held->members[i])
                                                          : value::undefined();
                                           }),
                                    value::undefined(),
                                    script::attr_enumerable | script::attr_configurable);
        }
        for (std::size_t i = held->members.size(); i < held->indices; ++i) {
            (void)target->erase_accessor(std::to_string(i));
        }
        held->indices = held->members.size();
        if (!named) { return; }
        // The names: every exposed id and name, in member order, first wins.
        // Never over an index, `length` or a key the page's expando took.
        std::vector<std::string> keys;
        {
            const auto txn = doc_->read();
            const atom id = atoms_->intern("id");
            const atom name = atoms_->intern("name");
            for (const node_id at : held->members) {
                for (const atom which : {id, name}) {
                    if (which == name && txn.element_ns(at) != node_ns::html) { continue; }
                    const std::string_view text = txn.attribute_value(at, which);
                    if (text.empty() || text == "length" || whole_index(text).has_value()) {
                        continue;
                    }
                    if (std::ranges::find(keys, text) == keys.end()) { keys.emplace_back(text); }
                }
            }
        }
        if (keys == held->names) { return; }
        for (const std::string & stale : held->names) { (void)target->erase_accessor(stale); }
        for (const std::string & key : keys) {
            if (target->find(key) != nullptr) { continue; } // an expando
            target->define_accessor(
                key,
                native(c, key,
                       [held, named_value, key](context & inner, std::span<value>) {
                           return named_value(inner, held->members, key);
                       }),
                value::undefined(), script::attr_configurable);
        }
        held->names = std::move(keys);
    };

    // The hidden state property the prototype's members reach the walk through.
    // Configurable, and only that: a non-configurable key is one the ownKeys
    // trap below would be obliged to report, and this one is nobody's business.
    target->define(collection_state_key,
                   native(cx, "collection",
                          [held, named_value, refresh, member](context & c, std::span<value> a) {
                              refresh(c);
                              const std::string what = arg_string(c, a, 0);
                              if (what == "length") {
                                  return value::number(static_cast<double>(held->members.size()));
                              }
                              // `item(unsigned long index)`: WebIDL's
                              // conversion wraps modulo 2^32, so item(2^32)
                              // is item(0) and item(-1) is past any end.
                              if (what == "item") {
                                  const std::uint32_t at = context::to_uint32(arg(a, 1));
                                  if (at >= held->members.size()) { return value::null(); }
                                  return member(c, held->members[at]);
                              }
                              if (what == "namedItem") {
                                  const std::string want = arg_string(c, a, 1);
                                  if (want.empty()) { return value::null(); }
                                  const value found = named_value(c, held->members, want);
                                  return found.is_undefined() ? value::null() : found;
                              }
                              return value::undefined();
                          }),
                   script::attr_configurable);

    // Refresh once now: `Object.getOwnPropertyNames(list)` reads the target through no
    // trap at all, and a collection nothing has touched must already own its
    // indices.
    refresh(cx);

    // Whether `key` is a visible supported name (WebIDL 3.9 "named property
    // visibility"): one refresh() gave its accessor. An expando of the same
    // name shadows it, and then the name is the expando's.
    const auto visible_name = [held, target, named](const std::string & key) {
        return named && std::ranges::find(held->names, key) != held->names.end() &&
               target->find_accessor(key) != nullptr;
    };
    handler->set("get",
                 native(cx, "get", [held, refresh, member](context & c, std::span<value> args) {
                     if (args.size() < 2) { return value::undefined(); }
                     refresh(c);
                     const std::string key = c.to_string(args[1]);
                     if (const std::optional<std::size_t> at = whole_index(key)) {
                         // Supported indices are exactly 0..length-1, as for
                         // item().
                         return *at < held->members.size() ? member(c, held->members[*at])
                                                           : value::undefined();
                     }
                     return c.get_with_receiver(args[0], key, args.size() > 2 ? args[2] : args[0]);
                 }));
    // An index is read-only: `collection[0] = x` must not stick, because an
    // HTMLCollection's indexed properties have no setter. Anything else is an
    // expando on the target, which is how `list.item = "pass"` shadows the
    // prototype's.
    handler->set(
        "set", native(cx, "set", [held, refresh, visible_name](context & c, std::span<value> args) {
            if (args.size() < 3 || !args[0].is_object()) { return value::boolean(false); }
            refresh(c);
            const std::string key = c.to_string(args[1]);
            // A write through an object that inherits from the collection
            // (`Object.create(list).x = 1`) is WebIDL 3.9.2 [[Set]] with a
            // receiver that is not the collection: the named properties are
            // ignored (step 2's ignoreNamedProps), a supported index is still
            // a read-only data property, and the rest is OrdinarySet, which
            // ends in a data property on the receiver (WPT
            // HTMLCollection-as-prototype.html).
            const value receiver = args.size() > 3 ? args[3] : args[0];
            const bool through_proxy =
                receiver.is_kind(script::heap_kind::proxy) &&
                static_cast<script::proxy_object *>(receiver.as_heap())->target.bits() ==
                    args[0].bits();
            if (!through_proxy && receiver.bits() != args[0].bits()) {
                if (const auto at = whole_index(key); at && *at < held->members.size()) {
                    return value::boolean(false);
                }
                return value::boolean(script::set_for_receiver(
                    c, visible_name(key) ? c.get_prototype(args[0]) : args[0], key, args[2],
                    receiver));
            }
            // `length` is read-only unless an interface up the chain has a
            // setter for it: HTMLOptionsCollection's `length` is writable
            // (HTML 4.10.10, `select.options.length = 2` truncates), and it
            // lives on that prototype, which this trap would otherwise never
            // consult.
            if (key == "length") {
                for (value at = c.get_prototype(args[0]); at.is_object();
                     at = c.get_prototype(at)) {
                    script::context::property_descriptor found;
                    if (c.own_property(at, key, found) && found.setter.is_callable()) {
                        const value v = args[2];
                        (void)c.call(found.setter, {&v, 1}, args.size() > 3 ? args[3] : args[0]);
                        return value::boolean(true);
                    }
                }
            }
            // A supported name has no setter either: the write is
            // refused rather than landing as an expando that would
            // shadow it (WebIDL 3.9.2 [[Set]] via [[DefineOwnProperty]]).
            if (key == "length" || whole_index(key).has_value() || visible_name(key)) {
                return value::boolean(false);
            }
            c.store_property(args[0], key, args[2]);
            return value::boolean(true);
        }));
    // WebIDL 3.9.3 [[DefineOwnProperty]] and 3.9.4 [[Delete]] of a legacy
    // platform object with no indexed or named setter or deleter: an array
    // index can never be defined, a supported index or visible name never
    // deleted (`Object.defineProperty(list, 0, ...)` throws and `delete
    // list[0]` is false), and any other key is an ordinary own property.
    handler->set(
        "defineProperty",
        native(cx, "defineProperty", [refresh, visible_name](context & c, std::span<value> args) {
            if (args.size() < 3) { return value::boolean(false); }
            refresh(c);
            const std::string key = c.to_string(args[1]);
            if (whole_index(key).has_value() || visible_name(key)) { return value::boolean(false); }
            return value::boolean(
                c.define_own_property(args[0], key, c.to_property_descriptor(args[2])));
        }));
    handler->set("deleteProperty",
                 native(cx, "deleteProperty",
                        [held, refresh, visible_name](context & c, std::span<value> args) {
                            if (args.size() < 2) { return value::boolean(true); }
                            refresh(c);
                            const std::string key = c.to_string(args[1]);
                            if (const std::optional<std::size_t> at = whole_index(key)) {
                                return value::boolean(*at >= held->members.size());
                            }
                            if (visible_name(key)) { return value::boolean(false); }
                            const bool deleted = c.delete_own_property(args[0], key);
                            // An expando that shadowed a supported name is
                            // gone, so the name shows again: force a refresh
                            // to put its accessor back.
                            if (deleted &&
                                std::ranges::find(held->names, key) != held->names.end()) {
                                held->names.clear();
                                held->version = 0;
                                refresh(c);
                            }
                            return value::boolean(deleted);
                        }));
    // `in` and hasOwnProperty both come here (builtins/objects/object.cpp): an
    // own key of the refreshed target, or something the prototype answers.
    handler->set("has", native(cx, "has", [refresh](context & c, std::span<value> args) {
                     if (args.size() < 2) { return value::boolean(false); }
                     refresh(c);
                     return value::boolean(c.has_property(args[0], args[1]));
                 }));
    // WebIDL 3.9.1 [[GetOwnProperty]]: a supported index, or a visible name, is
    // a data property holding the member (read-only and configurable, and
    // enumerable for an index only), whatever accessor the target keeps it in.
    handler->set("getOwnPropertyDescriptor",
                 native(cx, "getOwnPropertyDescriptor",
                        [held, refresh, member, visible_name, named_value](context & c,
                                                                           std::span<value> args) {
                            if (args.size() < 2) { return value::undefined(); }
                            refresh(c);
                            const std::string key = c.to_string(args[1]);
                            using descriptor = script::context::property_descriptor;
                            if (const std::optional<std::size_t> at = whole_index(key);
                                at && *at < held->members.size()) {
                                return c.from_property_descriptor(descriptor::data(
                                    member(c, held->members[*at]),
                                    script::attr_enumerable | script::attr_configurable));
                            }
                            if (visible_name(key)) {
                                return c.from_property_descriptor(descriptor::data(
                                    named_value(c, held->members, key), script::attr_configurable));
                            }
                            descriptor found;
                            if (!c.own_property(args[0], key, found)) { return value::undefined(); }
                            return c.from_property_descriptor(found);
                        }));
    // `Object.getOwnPropertyNames(list)` after a mutation nothing else has
    // read through: the keys are the refreshed target's, in its order (the
    // indices first, as WebIDL's [[OwnPropertyKeys]] has them, then the names
    // and any expando; WPT NodeList-live-mutations.window.js). The only symbol
    // key is the hidden state slot, which is not reported; see
    // document/named_access.cpp for why a symbol is not rebuilt here.
    handler->set("ownKeys", native(cx, "ownKeys", [refresh](context & c, std::span<value> args) {
                     const value out = c.make_array();
                     if (args.empty() || !args[0].is_object()) { return out; }
                     refresh(c);
                     auto & items = static_cast<script::array_object *>(out.as_heap())->items;
                     std::size_t count = 0;
                     // Cap on keys reported, so a corrupted or runaway key
                     // table cannot build an unbounded array.
                     constexpr std::size_t max_keys = 1000000;
                     static_cast<script::object_object *>(args[0].as_heap())
                         ->each_own_key([&](const std::string & key) {
                             if (!key.starts_with(symbol_key_mark) && count++ < max_keys) {
                                 items.push_back(c.string(key));
                             }
                         });
                     return out;
                 }));
    return value::object(
        cx.allocate<script::proxy_object>(value::object(target), value::object(handler)));
}

} // namespace ctbrowser::shell
