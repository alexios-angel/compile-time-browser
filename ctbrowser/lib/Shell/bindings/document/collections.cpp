// dom_bindings - the element collections: by tag, class and name, the HTML
// element walks, and the live HTMLCollection they are handed back as.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// THE LOCAL NAME AND THE NAMESPACE, which is the pair HTML's own definitions
// are written in and the pair `find_by_tag` below cannot ask about.
//
// It matters twice over in this engine. The tokenizer preserves case inside
// foreign content on purpose - see CLAUDE.md - so an SVG `<title>` and an HTML
// `<title>` can intern to the same atom while an SVG `<clipPath>` and an HTML
// one do not, and `<svg><title>Chart</title></svg>` really did make
// `document.title` answer "Chart".
namespace {

// The five, and not `isspace`: the same set `dom_whitespace` further down
// names, spelled again here because that one is defined after its first use
// and one constant cannot be in two anonymous namespaces at once.
constexpr std::string_view ascii_whitespace = "\t\n\f\r ";

[[nodiscard]] std::string_view local_name_of(std::string_view qualified) {
    const std::size_t colon = qualified.find(':');
    return colon == std::string_view::npos ? qualified : qualified.substr(colon + 1);
}

} // namespace

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

// "The body element", HTML 4.2.3: the FIRST CHILD of the document element that
// is a `body` or a `frameset`. Not the first `<body>` in the document - a
// `<body>` the parser has put inside a `<div>` is not the document's body, and
// `Document.body.html` asserts that by building one.
node_id dom_bindings::body_element() {
    const auto txn = doc_->read();
    const node_id root = txn.root();
    for (const node_id child : txn.children(root)) {
        if (txn.element_ns(child) != node_ns::html) { continue; }
        const auto tagged = txn.tag(child);
        if (!tagged.has_value()) { continue; }
        const std::string_view local = local_name_of(atoms_->text(*tagged));
        if (local == "body" || local == "frameset") { return child; }
    }
    return node_id{};
}

// "The title element", HTML 4.2.2 - and the SVG branch is not a curiosity. A
// document whose root is `<svg>` takes its title from that root's own first
// SVG `<title>` CHILD, not from any HTML title anywhere; every other document
// takes the first HTML title element in tree order, wherever it is. That
// "wherever" is what `document.title-01.html` is about: it removes the head,
// appends a `<title>` to the BODY, and expects the title to be that one.
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
    // Outside the transaction would be tidier, but `first_html_element` opens
    // one of its own and a read nested inside another read is a shape nothing
    // else in these bindings has - so the walk is repeated here instead.
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

// Infra's "strip and collapse ASCII whitespace": the five ASCII whitespace
// characters, not `isspace`, and a run of them becomes exactly one space.
// `document.title-03.html` writes "two\t\ttabs" and reads back "two tabs".
std::string dom_bindings::strip_and_collapse(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool pending = false;
    for (const char c : text) {
        if (ascii_whitespace.find(c) != std::string_view::npos) {
            pending = !out.empty();
            continue;
        }
        if (pending) { out.push_back(' '); }
        pending = false;
        out.push_back(c);
    }
    return out;
}

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

// ASCII whitespace, as the DOM defines it: TAB, LF, FF, CR and SPACE. Not
// `isspace`, which is locale-dependent and includes vertical tab.
constexpr std::string_view dom_whitespace = "\t\n\f\r ";

// A property key that is a whole non-negative integer and nothing else. "1x" is
// not index 1, and neither is " 1", "+1" or "1.0" - a collection has to say no
// to those or `hasOwnProperty` starts agreeing to keys nobody indexed.
[[nodiscard]] std::optional<std::size_t> whole_index(std::string_view key) {
    if (key.empty() || (key.size() > 1 && key.front() == '0')) { return std::nullopt; }
    std::size_t at = 0;
    const char * first = key.data();
    const char * last = first + key.size();
    const auto [stopped, failed] = std::from_chars(first, last, at);
    if (failed != std::errc{} || stopped != last) { return std::nullopt; }
    return at;
}

} // namespace

std::string dom_bindings::namespace_of(node_id id) const {
    if (const auto it = namespaces_.find(pack(id)); it != namespaces_.end()) { return it->second; }
    switch (doc_->read().element_ns(id)) {
    case node_ns::svg: return std::string{svg_namespace};
    case node_ns::html: return std::string{html_namespace};
    // An `other` element with no recorded URI cannot happen - the only thing
    // that makes one records it - but a stale handle resolves to `html` and
    // then to this, and the null namespace is the honest answer for a node that
    // is not there any more.
    case node_ns::other: break;
    }
    return {};
}

std::vector<std::string> dom_bindings::ordered_set(std::string_view text) {
    std::vector<std::string> out;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t start = text.find_first_not_of(dom_whitespace, at);
        if (start == std::string_view::npos) { break; }
        std::size_t end = text.find_first_of(dom_whitespace, start);
        if (end == std::string_view::npos) { end = text.size(); }
        std::string token{text.substr(start, end - start)};
        // AN ORDERED *SET*: "a a" asks for one class twice, and a duplicate in
        // the wanted list is a match requirement that is already satisfied.
        if (std::ranges::find(out, token) == out.end()) { out.push_back(std::move(token)); }
        at = end;
    }
    return out;
}

std::vector<node_id> dom_bindings::all_by_class(node_id root,
                                                const std::vector<std::string> & tokens) {
    if (tokens.empty()) { return {}; }
    const auto txn = doc_->read();
    const atom class_attribute = atoms_->intern("class");
    std::vector<node_id> found;
    // CASE-SENSITIVE in standards mode, ASCII-case-insensitive in quirks mode
    // (DOM 4.5 "list of elements with class names") - which is
    // getElementsByClassName-14.htm, a doctype-less page with `class="a A"`.
    const bool quirks = doc_->quirks();
    const auto has_every = [&](node_id at) {
        const std::vector<std::string> held = ordered_set(txn.attribute_value(at, class_attribute));
        return std::ranges::all_of(tokens, [&](const std::string & want) {
            return std::ranges::any_of(held, [&](const std::string & one) {
                return quirks ? ascii_iequals(one, want) : one == want;
            });
        });
    };
    const auto walk = [&](auto && self, node_id at, bool include) -> void {
        // ELEMENTS ONLY, and never the element the search started from: a search
        // rooted at an element looks at its DESCENDANTS.
        if (include && txn.tag(at).has_value() && has_every(at)) { found.push_back(at); }
        for (const node_id child : txn.children(at)) { self(self, child, true); }
    };
    // THE DOCUMENT'S ROOT IS THE <html> ELEMENT, not a Document node - this
    // tree builder makes `<html>` and calls set_root with it, and there is no
    // node above it. So a document-wide search must INCLUDE the root, or
    // `document.getElementsByClassName` silently cannot return the one element
    // that is most often given a class. An element-rooted search excludes it.
    walk(walk, root ? root : txn.root(), !root);
    return found;
}

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

// THE LIVE COLLECTION, and its property model - WebIDL's "legacy platform
// object" with an indexed getter and, for an HTMLCollection, a named one.
//
// WHAT A PAGE CAN OBSERVE, and `Document-Element-getElementsByTagName.js`
// observes all of it: `length`, `item` and `namedItem` are on the PROTOTYPE
// (`list.item = "pass"` shadows the inherited one with an expando); every
// index and every exposed id/name is an OWN property of the collection - so
// `Object.getOwnPropertyNames(list)` is ["0", "1", "x"] and nothing else,
// an index enumerable and a name not; an index cannot be assigned; and all of
// it is LIVE, recomputed as the document changes.
//
// HOW: a proxy over a target that carries the own properties. The traps
// REFRESH the target first - but only when the document's version moved since
// the last refresh, which is what keeps `for (i < list.length) list[i]` from
// walking the document per iteration. A refresh re-runs `members`, adds or
// removes the index accessors at the tail (an index reads `members[i]` so the
// accessor itself never goes stale) and re-derives the named keys. The
// prototype's three members find the collection's state through one hidden
// symbol-keyed own property - a symbol key is what getOwnPropertyNames does
// not report - so nothing about the walk lives on the prototype.
//
// WHAT IS NOT MODELLED: a strict-mode `list[5] = x` should throw; this engine
// has no strict mode (vm/objects/store.cpp), so it is silently refused instead.
namespace {

constexpr std::string_view collection_state_key = "@@sym:ctbrowser:collection";

// One native, three questions, closing over the C++ side of a collection.
// Called as state("length") / state("item", i) / state("namedItem", name).
[[nodiscard]] value collection_state_of(context & cx, value self) {
    return cx.lookup_property(self, std::string{collection_state_key});
}

[[nodiscard]] value ask_collection(context & cx, value self, const char * what, value argument) {
    const value state = collection_state_of(cx, self);
    if (!state.is_callable()) { return value::undefined(); }
    const value args[2] = {cx.string(what), argument};
    return cx.call(state, args, value::undefined());
}

// `length`, `item` and `namedItem` on a collection interface's prototype, once.
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
                          value::undefined(), script::attr_configurable);
    proto.define("item",
                 native("item", 1,
                        [](context & c, std::span<value> a) {
                            const value found =
                                ask_collection(c, c.current_this(), "item", arg(a, 0));
                            return found.is_undefined() ? value::null() : found;
                        }),
                 script::attr_builtin);
    // THE ITERABLE DECLARATION: `[Symbol.iterator]` on both, and NodeList's
    // forEach/keys/values/entries. Each materialises the members through the
    // proxy - `iterable_values` reads `length` and every index - and hands the
    // array's own iterator back, so `list.keys()` is an Array Iterator and
    // not an Array, which Node-childNodes.html asserts.
    const auto through_array = [&native](const char * name, const char * array_method) {
        return native(name, 0, [array_method](context & c, std::span<value>) {
            const value items = c.iterable_values(c.current_this());
            const value fn = c.lookup_property(items, array_method);
            return fn.is_callable() ? c.call(fn, {}, items) : value::undefined();
        });
    };
    proto.define("@@iterator", through_array("values", "values"), script::attr_builtin);
    if (named) {
        proto.define("namedItem",
                     native("namedItem", 1,
                            [](context & c, std::span<value> a) {
                                const value found =
                                    ask_collection(c, c.current_this(), "namedItem", arg(a, 0));
                                return found.is_undefined() ? value::null() : found;
                            }),
                     script::attr_builtin);
        return;
    }
    proto.define("keys", through_array("keys", "keys"), script::attr_builtin);
    proto.define("values", through_array("values", "values"), script::attr_builtin);
    proto.define("entries", through_array("entries", "entries"), script::attr_builtin);
    proto.define("forEach",
                 native("forEach", 1,
                        [](context & c, std::span<value> a) {
                            const value self = c.current_this();
                            const value callback = arg(a, 0);
                            if (!callback.is_callable()) {
                                c.throw_error("TypeError", "NodeList.forEach: not a function");
                                return value::undefined();
                            }
                            const value items = c.iterable_values(self);
                            const auto * held =
                                static_cast<script::array_object *>(items.as_heap());
                            for (std::size_t i = 0; i < held->items.size(); ++i) {
                                const value args[3] = {held->items[i],
                                                       value::number(static_cast<double>(i)), self};
                                (void)c.call(callback, args, arg(a, 1));
                                if (c.failed()) { break; }
                            }
                            return value::undefined();
                        }),
                 script::attr_builtin);
}

} // namespace

value dom_bindings::make_live_collection(context & cx,
                                         std::function<std::vector<node_id>()> members,
                                         std::string_view interface_name) {
    auto * target = static_cast<script::object_object *>(cx.make_object().as_heap());
    auto * handler = static_cast<script::object_object *>(cx.make_object().as_heap());
    // The table is built lazily on the first `wrap()`, and a page can ask for a
    // collection before it has touched a single element - `document.images`
    // reaches here without wrapping anything. Without this the prototype was
    // `undefined` and `instanceof HTMLCollection` was false for the first
    // collection a page made and true for every one after it.
    ensure_dom_interfaces(cx);
    const bool named = interface_name == "HTMLCollection";
    if (const value proto = interface_prototype(interface_name); proto.is_object()) {
        install_collection_prototype(cx, *static_cast<script::object_object *>(proto.as_heap()),
                                     named);
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
    // Allocating in the context the CALL arrives in, not the one captured at
    // creation: `refresh` outlives this frame.
    const auto native_in = [](context & in, std::string name, script::native_fn fn) {
        return value::object(in.allocate<script::native_object>(std::move(name), std::move(fn)));
    };
    const auto native = [&cx, native_in](std::string name, script::native_fn fn) {
        return native_in(cx, std::move(name), std::move(fn));
    };

    // The first member whose id - or, for an HTML element, name - is `want`.
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

    // BRING THE TARGET'S OWN PROPERTIES UP TO DATE, when the document moved.
    const auto refresh = [this, target, held, named, named_member, native_in](context & c) {
        const std::uint64_t now = doc_->version();
        if (now == held->version && held->version != 0) { return; }
        held->version = now;
        held->members = held->live();
        // The indices: only the tail changes, and an accessor reads the
        // member it names at call time.
        for (std::size_t i = held->indices; i < held->members.size(); ++i) {
            target->define_accessor(std::to_string(i),
                                    native_in(c, std::to_string(i),
                                              [this, held, i](context & inner, std::span<value>) {
                                                  return i < held->members.size()
                                                             ? wrap(inner, held->members[i])
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
                native_in(c, key,
                          [this, held, named_member, key](context & inner, std::span<value>) {
                              return wrap(inner, named_member(held->members, key));
                          }),
                value::undefined(), script::attr_configurable);
        }
        held->names = std::move(keys);
    };

    // The hidden state property the prototype's members reach the walk through.
    target->define(collection_state_key,
                   native("collection",
                          [this, held, named_member, refresh](context & c, std::span<value> a) {
                              refresh(c);
                              const std::string what = arg_string(c, a, 0);
                              if (what == "length") {
                                  return value::number(static_cast<double>(held->members.size()));
                              }
                              if (what == "item") {
                                  const double at = arg_number(a, 1);
                                  if (at < 0 || at >= static_cast<double>(held->members.size())) {
                                      return value::null();
                                  }
                                  return wrap(c, held->members[static_cast<std::size_t>(at)]);
                              }
                              if (what == "namedItem") {
                                  const std::string want = arg_string(c, a, 1);
                                  const node_id found =
                                      want.empty() ? node_id{} : named_member(held->members, want);
                                  return found ? wrap(c, found) : value::null();
                              }
                              return value::undefined();
                          }),
                   script::attr_none);

    // ONCE NOW: `Object.getOwnPropertyNames(list)` reads the target through no
    // trap at all, and a collection nothing has touched must already own its
    // indices.
    refresh(cx);

    handler->set("get", native("get", [this, held, refresh](context & c, std::span<value> args) {
                     if (args.size() < 2) { return value::undefined(); }
                     refresh(c);
                     const std::string key = c.to_string(args[1]);
                     if (const std::optional<std::size_t> at = whole_index(key)) {
                         return *at < held->members.size() ? wrap(c, held->members[*at])
                                                           : value::undefined();
                     }
                     return c.lookup_property(args[0], key);
                 }));
    // AN INDEX IS READ-ONLY. `collection[0] = x` must not stick - an
    // HTMLCollection's indexed properties have no setter, so a non-strict
    // assignment is silently ignored. Anything else is an expando on the
    // target, which is how `list.item = "pass"` shadows the prototype's.
    handler->set("set", native("set", [](context & c, std::span<value> args) {
                     if (args.size() < 3 || !args[0].is_object()) { return value::boolean(false); }
                     const std::string key = c.to_string(args[1]);
                     if (key == "length" || whole_index(key).has_value()) {
                         return value::boolean(false);
                     }
                     c.store_property(args[0], key, args[2]);
                     return value::boolean(true);
                 }));
    // `in` AND hasOwnProperty both come here (builtins/objects/object.cpp): an
    // own key of the refreshed target, or something the prototype answers.
    handler->set("has", native("has", [refresh](context & c, std::span<value> args) {
                     if (args.size() < 2) { return value::boolean(false); }
                     refresh(c);
                     return value::boolean(c.has_property(args[0], args[1]));
                 }));
    handler->set("getOwnPropertyDescriptor",
                 native("getOwnPropertyDescriptor", [refresh](context & c, std::span<value> args) {
                     if (args.size() < 2) { return value::undefined(); }
                     refresh(c);
                     script::context::property_descriptor found;
                     if (!c.own_property(args[0], c.to_string(args[1]), found)) {
                         return value::undefined();
                     }
                     return c.from_property_descriptor(found);
                 }));
    return value::object(
        cx.allocate<script::proxy_object>(value::object(target), value::object(handler)));
}

} // namespace ctbrowser::shell
