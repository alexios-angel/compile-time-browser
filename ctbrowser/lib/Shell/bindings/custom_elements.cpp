// dom_bindings - customElements: the CustomElementRegistry, the HTMLElement
// constructor a class extends, and the reactions.
//
// WHY THIS IS A SCAN, like the mutation observer beside it. The specification
// enqueues a reaction from inside every mutating step of the DOM; this engine
// has `mutated()`, which says THAT the document changed and not what. So a
// definition is applied by walking the tree: an element a definition covers
// and nothing has tracked yet is upgraded, and a tracked one is diffed against
// what it was - connected or not, under which parent, with which observed
// attributes. The reactions run before the native that mutated returns, which
// is what [CEReactions] promises a page.
//
// WHAT `super()` MEANS HERE. The VM does not rebind `this` to what a base
// constructor returns, so the specification's "the HTMLElement constructor
// returns the element being upgraded" cannot be spelled that way. Instead the
// RECEIVER becomes the element: `new X()` arrives with a fresh object whose
// prototype is X.prototype, and the constructor turns that object into the
// wrapper of a new node. An UPGRADE calls the class body with the existing
// wrapper as `this`, so the identity every reference to the element already
// holds is kept, and `super()` then finds a receiver that is an element
// already and does nothing.
//
// ponytail: class fields on an upgraded element are not initialised - the
// VM's field-initialiser run is private to `new`. `new X()` runs them.
// ponytail: the scan is O(tree) per DOM write once anything is defined, and
// a candidate parsed into a DETACHED subtree is upgraded when it connects,
// not when it is parsed. Both hold until a page shows the cost.

#include <ctbrowser/shell/bindings.hpp>

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

// HTML 4.13.1 "valid custom element name": PotentialCustomElementName.
[[nodiscard]] bool is_pcen_char(char32_t c) {
    return c == '-' || c == '.' || (c >= '0' && c <= '9') || c == '_' || (c >= 'a' && c <= 'z') ||
           c == 0xB7 || (c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) ||
           (c >= 0xF8 && c <= 0x37D) || (c >= 0x37F && c <= 0x1FFF) ||
           (c >= 0x200C && c <= 0x200D) || (c >= 0x203F && c <= 0x2040) ||
           (c >= 0x2070 && c <= 0x218F) || (c >= 0x2C00 && c <= 0x2FEF) ||
           (c >= 0x3001 && c <= 0xD7FF) || (c >= 0xF900 && c <= 0xFDCF) ||
           (c >= 0xFDF0 && c <= 0xFFFD) || (c >= 0x10000 && c <= 0xEFFFF);
}

[[nodiscard]] bool is_valid_custom_element_name(std::string_view name) {
    if (name.empty() || name[0] < 'a' || name[0] > 'z') { return false; }
    if (name.find('-') == std::string_view::npos) { return false; }
    for (std::size_t i = 0; i < name.size();) {
        const auto lead = static_cast<unsigned char>(name[i]);
        const std::size_t length = lead < 0x80           ? 1
                                   : (lead >> 5) == 0x6  ? 2
                                   : (lead >> 4) == 0xE  ? 3
                                   : (lead >> 3) == 0x1E ? 4
                                                         : 0;
        if (length == 0 || i + length > name.size()) { return false; }
        char32_t point = length == 1   ? lead
                         : length == 2 ? (lead & 0x1Fu)
                         : length == 3 ? (lead & 0x0Fu)
                                       : (lead & 0x07u);
        for (std::size_t k = 1; k < length; ++k) {
            const auto trail = static_cast<unsigned char>(name[i + k]);
            if ((trail & 0xC0) != 0x80) { return false; }
            point = (point << 6) | (trail & 0x3Fu);
        }
        if (!is_pcen_char(point)) { return false; }
        i += length;
    }
    for (const std::string_view reserved :
         {"annotation-xml", "color-profile", "font-face", "font-face-src", "font-face-uri",
          "font-face-format", "font-face-name", "missing-glyph"}) {
        if (name == reserved) { return false; }
    }
    return true;
}

} // namespace

// --- lookups ------------------------------------------------------------------

std::size_t dom_bindings::custom_definition_for(const read_txn & txn, node_id id) const {
    const std::string_view tag = atoms_->text(txn.tag(id).value_or(atom{}));
    for (std::size_t i = 0; i < custom_definitions_.size(); ++i) {
        const custom_element_definition & def = custom_definitions_[i];
        if (def.local_name != tag) { continue; }
        if (def.name == def.local_name) { return i; }
        // A customized built-in is named by its `is` attribute.
        if (txn.attribute_value(id, atoms_->intern("is")) == def.name) { return i; }
    }
    return npos;
}

std::size_t dom_bindings::custom_definition_of(context & cx, value receiver) const {
    for (value proto = cx.get_prototype(receiver); proto.is_object();
         proto = cx.get_prototype(proto)) {
        for (std::size_t i = 0; i < custom_definitions_.size(); ++i) {
            if (custom_definitions_[i].prototype.bits() == proto.bits()) { return i; }
        }
    }
    return npos;
}

void dom_bindings::sync_custom_element_roots() {
    if (custom_elements_interface_ == nullptr) { return; }
    std::vector<value> & roots = custom_elements_interface_->retained;
    roots.clear();
    for (const custom_element_definition & def : custom_definitions_) {
        for (const value & held : {def.constructor, def.prototype, def.connected, def.disconnected,
                                   def.adopted, def.attribute_changed, def.connected_move}) {
            roots.push_back(held);
        }
    }
    for (const auto & [name, promises] : when_defined_) {
        for (const value & promise : promises) { roots.push_back(promise); }
    }
}

// --- creation -------------------------------------------------------------------

value dom_bindings::create_html_element(context & cx, const std::string & name) {
    const atom tag = atoms_->intern_lower(name);
    const std::string_view lowered = atoms_->text(tag);
    for (const custom_element_definition & def : custom_definitions_) {
        if (def.name != lowered || def.local_name != def.name) { continue; }
        // SYNCHRONOUSLY, through the author's class: `super()` makes the
        // receiver the wrapper of the node, so the instance IS the element.
        const value made = cx.construct(def.constructor, {});
        if (!cx.failed() && handle_of(made)) { return made; }
        // "Report the exception, and set result to a new HTMLUnknownElement" -
        // a constructor that threw, or never reached HTMLElement, does not
        // make createElement throw.
        note_callback_fault("custom element constructor");
        break;
    }
    return wrap(cx, doc_->create_element(tag));
}

// --- the scan ----------------------------------------------------------------------

void dom_bindings::walk_custom_elements(const read_txn & txn, node_id start, bool connected) {
    struct frame {
        node_id at;
        bool moved; // an ancestor changed parent while connected
    };
    const auto observed = [](const custom_element_definition & def, std::string_view name) {
        for (const std::string & want : def.observed_attributes) {
            if (want == name) { return true; }
        }
        return false;
    };
    // The observed attributes as they are now, diffed against `was`.
    const auto diff_attributes = [&](const custom_element_definition & def,
                                     custom_element_state & state, node_id at) {
        if (def.observed_attributes.empty()) { return; }
        std::vector<std::pair<atom, std::string>> now;
        for (const attribute & held : txn.attributes(at)) {
            const std::string spelling{atoms_->text(held.name)};
            if (!observed(def, spelling)) { continue; }
            now.emplace_back(held.name, held.value);
            const std::string * before = nullptr;
            for (const auto & [name, old_value] : state.attributes) {
                if (name == held.name) { before = &old_value; }
            }
            if (before != nullptr && *before == held.value) { continue; }
            custom_element_reaction reaction{
                at,       custom_element_reaction::kind::attribute_changed,
                spelling, {},
                {},       before != nullptr,
                true};
            if (before != nullptr) { reaction.old_value = *before; }
            reaction.new_value = held.value;
            custom_reactions_.push_back(std::move(reaction));
        }
        for (const auto & [name, old_value] : state.attributes) {
            bool still = false;
            for (const auto & [now_name, now_value] : now) { still = still || now_name == name; }
            if (still) { continue; }
            custom_reactions_.push_back(
                custom_element_reaction{at,
                                        custom_element_reaction::kind::attribute_changed,
                                        std::string{atoms_->text(name)},
                                        old_value,
                                        {},
                                        true,
                                        false});
        }
        state.attributes = std::move(now);
    };
    const auto enqueue = [this](node_id at, custom_element_reaction::kind what) {
        custom_reactions_.push_back(custom_element_reaction{at, what, {}, {}, {}, false, false});
    };
    using kind = custom_element_reaction::kind;

    std::vector<frame> pending{frame{start, false}};
    while (!pending.empty()) {
        const frame here = pending.back();
        pending.pop_back();
        bool moved = here.moved;
        if (txn.kind(here.at).value_or(node_kind::text) == node_kind::element &&
            txn.element_ns(here.at) == node_ns::html) {
            const std::uint64_t key = pack(here.at);
            const auto found = custom_elements_.find(key);
            if (found == custom_elements_.end()) {
                const std::size_t index = custom_definition_for(txn, here.at);
                if (index != npos) {
                    // "Upgrade an element": the constructor, then
                    // attributeChangedCallback for each observed attribute
                    // present, then connectedCallback if connected.
                    const custom_element_definition & def = custom_definitions_[index];
                    custom_element_state state;
                    state.definition = index;
                    state.connected = connected;
                    state.visited = true;
                    state.parent = txn.parent(here.at);
                    enqueue(here.at, kind::upgrade);
                    diff_attributes(def, state, here.at);
                    if (connected) { enqueue(here.at, kind::connected); }
                    custom_elements_.emplace(key, std::move(state));
                }
            } else {
                custom_element_state & state = found->second;
                const custom_element_definition & def = custom_definitions_[state.definition];
                state.visited = true;
                const node_id parent = txn.parent(here.at);
                moved = moved || (connected && state.connected && parent != state.parent);
                if (state.connected && !connected) {
                    enqueue(here.at, kind::disconnected);
                } else if (!state.connected && connected) {
                    enqueue(here.at, kind::connected);
                } else if (moved && connected) {
                    // moveBefore: connectedMoveCallback when the class has
                    // one, the disconnect/connect pair otherwise.
                    if (def.connected_move.is_callable()) {
                        enqueue(here.at, kind::connected_move);
                    } else {
                        enqueue(here.at, kind::disconnected);
                        enqueue(here.at, kind::connected);
                    }
                }
                diff_attributes(def, state, here.at);
                state.connected = connected;
                state.parent = parent;
            }
        }
        const std::span<const node_id> children = txn.children(here.at);
        for (std::size_t i = children.size(); i-- > 0;) {
            pending.push_back(frame{children[i], moved});
        }
    }
}

void dom_bindings::scan_custom_elements() {
    const auto txn = doc_->read();
    for (auto & [key, state] : custom_elements_) { state.visited = false; }
    walk_custom_elements(txn, txn.root(), true);
    // Every shadow tree, connected when its host's shadow-including root is
    // the document.
    for (const auto & [host_key, root] : shadow_roots_) {
        const node_id top = root_of_tree(txn, unpack(host_key), true);
        const bool connected =
            top == txn.root() || txn.kind(top).value_or(node_kind::element) == node_kind::document;
        walk_custom_elements(txn, root, connected);
    }
    // What no root reached is detached: a connected one left the tree.
    for (auto it = custom_elements_.begin(); it != custom_elements_.end();) {
        if (it->second.visited) {
            ++it;
            continue;
        }
        const node_id at = unpack(it->first);
        if (!txn.contains(at)) {
            it = custom_elements_.erase(it);
            continue;
        }
        if (it->second.connected) {
            custom_reactions_.push_back(custom_element_reaction{
                at, custom_element_reaction::kind::disconnected, {}, {}, {}, false, false});
            it->second.connected = false;
            it->second.parent = node_id{};
        }
        ++it;
    }
}

void dom_bindings::flush_custom_element_reactions() {
    context & cx = *cx_;
    // Drained from the front so a reaction that mutates - and so re-enters
    // this through mutated() - drains what was queued before it, in order.
    while (!custom_reactions_.empty()) {
        const custom_element_reaction reaction = std::move(custom_reactions_.front());
        custom_reactions_.erase(custom_reactions_.begin());
        const auto found = custom_elements_.find(pack(reaction.target));
        if (found == custom_elements_.end()) { continue; }
        // COPIED OUT: a callback may define another element and grow the
        // vector under a reference.
        const custom_element_definition def = custom_definitions_[found->second.definition];
        const value wrapper = wrap(cx, reaction.target);
        if (!wrapper.is_object()) { continue; }
        using kind = custom_element_reaction::kind;
        value callback = value::undefined();
        std::vector<value> args;
        switch (reaction.what) {
        case kind::upgrade:
            static_cast<script::object_object *>(wrapper.as_heap())->prototype = def.prototype;
            callback = def.constructor;
            break;
        case kind::connected: callback = def.connected; break;
        case kind::disconnected: callback = def.disconnected; break;
        case kind::connected_move: callback = def.connected_move; break;
        case kind::attribute_changed:
            callback = def.attribute_changed;
            args.push_back(cx.string(reaction.name));
            args.push_back(reaction.has_old ? cx.string(reaction.old_value) : value::null());
            args.push_back(reaction.has_new ? cx.string(reaction.new_value) : value::null());
            args.push_back(value::null()); // namespace: attributes carry none here
            break;
        }
        if (!callback.is_callable()) { continue; }
        (void)cx.call(callback, args, wrapper);
        note_callback_fault("custom element");
    }
}

void dom_bindings::react_custom_elements() {
    if (custom_definitions_.empty() || cx_ == nullptr || doc_ == nullptr) { return; }
    scan_custom_elements();
    flush_custom_element_reactions();
}

// --- the interfaces ---------------------------------------------------------------

void dom_bindings::install_custom_elements(context & cx) {
    // --- HTMLElement, CONSTRUCTIBLE. Defined here, before install_dom_interfaces
    // --- builds the table, which adopts a global that already exists rather
    // --- than replacing it with the throwing stub every other interface gets.
    auto * html_element_proto = static_cast<script::object_object *>(cx.make_object().as_heap());
    auto * html_element_ctor = cx.allocate<script::native_object>(
        "HTMLElement", [this](context & c, std::span<value>) -> value {
            const value self = c.current_this();
            if (!self.is_object()) {
                c.throw_error("TypeError",
                              "Failed to construct 'HTMLElement': please use the 'new' operator.");
                return value::undefined();
            }
            // AN UPGRADE: the receiver is the element already.
            if (handle_of(self)) { return self; }
            const std::size_t index = custom_definition_of(c, self);
            if (index == npos) {
                c.throw_error("TypeError", "Illegal constructor");
                return value::undefined();
            }
            const custom_element_definition & def = custom_definitions_[index];
            const node_id made = doc_->create_element(atoms_->intern(def.local_name));
            if (def.local_name != def.name) {
                (void)doc_->set_attribute(made, atoms_->intern("is"), def.name);
            }
            // THE RECEIVER BECOMES THE WRAPPER - what wrap() does to a fresh
            // object, done to the instance `new` made, whose prototype is the
            // author's class.
            auto * obj = static_cast<script::object_object *>(self.as_heap());
            obj->set(std::string{handle_property}, value::number(static_cast<double>(pack(made))));
            install_element_views(c, *obj, made);
            refresh_element(c, *obj, made);
            wrappers_.emplace(pack(made), obj);
            custom_element_state state;
            state.definition = index;
            custom_elements_.emplace(pack(made), std::move(state));
            return self;
        });
    html_element_ctor->set("prototype", value::object(html_element_proto));
    html_element_proto->define("constructor", value::object(html_element_ctor),
                               script::attr_builtin);
    cx.define_global("HTMLElement", value::object(html_element_ctor));

    // --- CustomElementRegistry.prototype -----------------------------------
    auto * registry_proto = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto method = [&cx, registry_proto](const char * name, script::native_fn fn) {
        registry_proto->define(
            name, value::object(cx.allocate<script::native_object>(name, std::move(fn))),
            script::attr_builtin);
    };
    const auto defined = [this](std::string_view name) {
        for (std::size_t i = 0; i < custom_definitions_.size(); ++i) {
            if (custom_definitions_[i].name == name) { return i; }
        }
        return npos;
    };

    method("define", [this, defined](context & c, std::span<value> args) -> value {
        // In the specification's order: the constructor, the name, the name
        // twice over, the `extends`, then the prototype and its callbacks.
        const value ctor = arg(args, 1);
        if (!ctor.is_callable()) {
            c.throw_error("TypeError", "Failed to execute 'define' on 'CustomElementRegistry': "
                                       "parameter 2 is not of type 'Function'.");
            return value::undefined();
        }
        const std::string name = arg_string(c, args, 0);
        if (!is_valid_custom_element_name(name)) {
            throw_dom_exception(c, "SyntaxError",
                                "'" + name + "' is not a valid custom element name");
            return value::undefined();
        }
        if (defined(name) != npos) {
            throw_dom_exception(c, "NotSupportedError",
                                "the name '" + name + "' has already been used with this registry");
            return value::undefined();
        }
        for (const custom_element_definition & def : custom_definitions_) {
            if (def.constructor.bits() == ctor.bits()) {
                throw_dom_exception(c, "NotSupportedError",
                                    "this constructor has already been used with this registry");
                return value::undefined();
            }
        }
        std::string local_name = name;
        const value options = arg(args, 2);
        if (options.is_object()) {
            const value extends = c.lookup_property(options, "extends");
            if (c.failed()) { return value::undefined(); }
            if (!extends.is_undefined() && !extends.is_null()) {
                local_name = c.to_string(extends);
                if (is_valid_custom_element_name(local_name)) {
                    throw_dom_exception(c, "NotSupportedError",
                                        "'" + local_name +
                                            "' is a custom element name, not a "
                                            "built-in element to extend");
                    return value::undefined();
                }
            }
        }
        const value prototype = c.lookup_property(ctor, "prototype");
        if (c.failed()) { return value::undefined(); }
        if (!prototype.is_object()) {
            c.throw_error("TypeError", "Failed to execute 'define' on 'CustomElementRegistry': "
                                       "The 'prototype' property is not an object");
            return value::undefined();
        }
        custom_element_definition def;
        def.name = name;
        def.local_name = local_name;
        def.constructor = ctor;
        def.prototype = prototype;
        const auto callback = [&](const char * which, value & into) {
            into = c.lookup_property(prototype, which);
            if (c.failed()) { return false; }
            if (!into.is_undefined() && !into.is_callable()) {
                c.throw_error("TypeError", std::string{"Failed to execute 'define' on "
                                                       "'CustomElementRegistry': '"} +
                                               which + "' is not a function");
                return false;
            }
            return true;
        };
        if (!callback("connectedCallback", def.connected) ||
            !callback("disconnectedCallback", def.disconnected) ||
            !callback("adoptedCallback", def.adopted) ||
            !callback("attributeChangedCallback", def.attribute_changed) ||
            !callback("connectedMoveCallback", def.connected_move)) {
            return value::undefined();
        }
        if (def.attribute_changed.is_callable()) {
            const value observed = c.lookup_property(ctor, "observedAttributes");
            if (c.failed()) { return value::undefined(); }
            if (!observed.is_undefined()) {
                const value items = c.iterable_values(observed);
                if (items.is_array()) {
                    for (const value & entry :
                         static_cast<script::array_object *>(items.as_heap())->items) {
                        def.observed_attributes.push_back(c.to_string(entry));
                    }
                }
            }
        }
        custom_definitions_.push_back(std::move(def));
        // The candidates in the document are upgraded, then whenDefined settles.
        react_custom_elements();
        if (const auto waiting = when_defined_.find(name); waiting != when_defined_.end()) {
            const std::vector<value> promises = std::move(waiting->second);
            when_defined_.erase(waiting);
            for (const value & promise : promises) { c.settle_promise(promise, ctor, false); }
        }
        sync_custom_element_roots();
        return value::undefined();
    });

    method("get", [this, defined](context & c, std::span<value> args) {
        const std::size_t index = defined(arg_string(c, args, 0));
        return index == npos ? value::undefined() : custom_definitions_[index].constructor;
    });

    method("getName", [this](context &, std::span<value> args) {
        const value ctor = arg(args, 0);
        for (const custom_element_definition & def : custom_definitions_) {
            if (def.constructor.bits() == ctor.bits()) { return cx_->string(def.name); }
        }
        return value::null();
    });

    method("whenDefined", [this, defined](context & c, std::span<value> args) {
        const std::string name = arg_string(c, args, 0);
        if (!is_valid_custom_element_name(name)) {
            return c.make_promise(
                make_dom_exception(c, "SyntaxError",
                                   "'" + name + "' is not a valid custom element name"),
                true);
        }
        if (const std::size_t index = defined(name); index != npos) {
            return c.make_promise(custom_definitions_[index].constructor, false);
        }
        const value promise = c.make_pending_promise();
        when_defined_[name].push_back(promise);
        sync_custom_element_roots();
        return promise;
    });

    method("upgrade", [this](context & c, std::span<value> args) {
        if (custom_definitions_.empty()) { return value::undefined(); }
        node_id root = handle_of(arg(args, 0));
        if (!root && arg(args, 0).is_object_like() && document_.is_object_like() &&
            arg(args, 0).bits() == document_.bits()) {
            root = doc_->root();
        }
        if (!root) {
            c.throw_error("TypeError", "Failed to execute 'upgrade' on 'CustomElementRegistry': "
                                       "parameter 1 is not of type 'Node'.");
            return value::undefined();
        }
        {
            const auto txn = doc_->read();
            const node_id top = root_of_tree(txn, root, true);
            for (auto & [key, state] : custom_elements_) { state.visited = false; }
            walk_custom_elements(txn, root,
                                 top == txn.root() || txn.kind(top).value_or(node_kind::element) ==
                                                          node_kind::document);
        }
        flush_custom_element_reactions();
        return value::undefined();
    });

    auto * registry_ctor = cx.allocate<script::native_object>(
        "CustomElementRegistry", [](context & c, std::span<value>) {
            c.throw_error("TypeError", "Illegal constructor");
            return value::undefined();
        });
    registry_ctor->set("prototype", value::object(registry_proto));
    registry_proto->define("constructor", value::object(registry_ctor), script::attr_builtin);
    cx.define_global("CustomElementRegistry", value::object(registry_ctor));
    custom_elements_interface_ = registry_ctor;

    // `window.customElements` - a bare global, which the window proxy answers
    // for `window.customElements` and `self.customElements` too.
    auto * registry = static_cast<script::object_object *>(cx.make_object().as_heap());
    registry->prototype = value::object(registry_proto);
    cx.define_global("customElements", value::object(registry));
}

} // namespace ctbrowser::shell
