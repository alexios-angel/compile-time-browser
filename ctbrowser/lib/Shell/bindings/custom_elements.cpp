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
// WHAT `super()` MEANS HERE. The VM does not rebind `this` to what a NATIVE
// base constructor returns (context::rebind_receiver), so the specification's
// "the HTMLElement constructor returns the element being upgraded" cannot be
// spelled that way. Instead the RECEIVER becomes the element: `new X()`
// arrives with a fresh object whose prototype is X.prototype, and the
// constructor turns that object into the wrapper of a new node. An UPGRADE
// calls the class body with the existing wrapper as `this`, so the identity
// every reference to the element already holds is kept, and `super()` then
// finds a receiver that is on the definition's construction stack: it takes
// the prototype and marks the entry constructed, exactly as HTML 4.13.4 step
// 7 does, and a second `super()` meets the marker and throws.
//
// WHAT A NATIVE CANNOT SEE: new.target. The VM carries it only into a JS
// frame, so `Reflect.construct(HTMLElement, [], NewTarget)` - the receiver
// made from HTMLElement.prototype, NewTarget invisible - is refused where a
// browser would build NewTarget's element. `new C()` and `super()` are what
// pages write, and both carry the class on the receiver's prototype.
//
// ponytail: the scan is O(tree) per DOM write once anything is defined, and
// a candidate parsed into a DETACHED subtree is upgraded when it connects or
// when the native that made it says so (upgrade_created_subtree). Both hold
// until a page shows the cost.
// ponytail: a customized built-in's "is value" is its `is` ATTRIBUTE. A
// browser keeps the two apart (the value is immutable, the attribute is not);
// the serialiser and `:defined` read the attribute here, and setting `is`
// afterwards changes the definition lookup where it should not.

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

// HTML 4.13.1, "valid custom element name" AS IT IS TODAY - not
// PotentialCustomElementName, which whatwg/html#7991 replaced.
//
// The old production was a fixed list of XML name characters; a name is now
// any VALID ELEMENT LOCAL NAME that has no ASCII upper alpha in it, has a
// hyphen, and is not one of the eight SVG/MathML names. That is much wider:
// `a!-element`, `a\x01-element` and `_-element` are all names a page may
// define, and PotentialCustomElementName refused every one of them.
//
// A valid element local name is the tag names the HTML and the XML parsers
// can both produce, which is the specification's one regular expression:
//   ^(?:[A-Za-z][^NUL TAB LF FF CR SPACE / >]*
//     | [:_ or U+0080 and up][A-Za-z0-9-.:_ or U+0080 and up]*)$
// The first branch is what the HTML tokenizer will read back as a tag name -
// it bans only the characters that would END one - and the second is the XML
// Name production's shape.
[[nodiscard]] bool html_tag_name_char(char32_t c) {
    return c != 0 && c != 0x9 && c != 0xA && c != 0xC && c != 0xD && c != 0x20 && c != '/' &&
           c != '>';
}

[[nodiscard]] bool xml_name_char(char32_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '.' || c == ':' || c == '_' || (c >= 0x80 && c <= 0x10FFFF);
}

[[nodiscard]] bool is_valid_element_local_name(const std::vector<char32_t> & points) {
    if (points.empty()) { return false; }
    const char32_t first = points.front();
    if ((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z')) {
        bool ok = true;
        for (std::size_t i = 1; i < points.size(); ++i) {
            ok = ok && html_tag_name_char(points[i]);
        }
        if (ok) { return true; }
    }
    if (first != ':' && first != '_' && (first < 0x80 || first > 0x10FFFF)) { return false; }
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (!xml_name_char(points[i])) { return false; }
    }
    return true;
}

[[nodiscard]] bool is_valid_custom_element_name(std::string_view name) {
    if (name.empty()) { return false; }
    std::vector<char32_t> points;
    for (std::size_t at = 0; at < name.size();) { points.push_back(decode_utf8(name, at)); }
    // THE FIRST CHARACTER IS AN ASCII LOWER ALPHA. It is a requirement of its
    // own, beside the local-name production: `_-element`, `:-element` and
    // `\u{10000}-element` are all valid element local names and none of them
    // is a valid CUSTOM element name. MEASURED - five subtests of
    // valid-custom-element-names.html say so by code point.
    if (points.front() < 'a' || points.front() > 'z') { return false; }
    bool hyphen = false;
    for (const char32_t c : points) {
        if (c >= 'A' && c <= 'Z') { return false; }
        hyphen = hyphen || c == '-';
    }
    if (!hyphen || !is_valid_element_local_name(points)) { return false; }
    for (const std::string_view reserved :
         {"annotation-xml", "color-profile", "font-face", "font-face-src", "font-face-uri",
          "font-face-format", "font-face-name", "missing-glyph"}) {
        if (name == reserved) { return false; }
    }
    return true;
}

// The text a reported exception carries. `name` and `message` are read as
// data rather than through `toString`, which a page's own thrown object may
// have made throw - see events/dispatch.cpp's twin.
[[nodiscard]] std::string describe(context & cx, value thrown) {
    if (!thrown.is_object()) { return cx.to_string(thrown); }
    const value name = cx.lookup_property(thrown, "name");
    const value message = cx.lookup_property(thrown, "message");
    if (name.is_undefined() && message.is_undefined()) { return "an exception"; }
    std::string text = name.is_undefined() ? std::string{"Error"} : cx.to_string(name);
    const std::string body = message.is_undefined() ? std::string{} : cx.to_string(message);
    if (!body.empty()) { text += ": " + body; }
    return text;
}

// WebIDL `sequence<DOMString>` from an iterable: every conversion step can
// throw - Symbol.iterator not callable, `next` throwing - and each of those
// leaves the VM's throw pending for the native to rethrow. False when it did.
[[nodiscard]] bool string_sequence(context & cx, value from, std::vector<std::string> & out) {
    if (!from.is_object_like()) {
        cx.throw_error("TypeError", "The provided value cannot be converted to a sequence.");
        return false;
    }
    const value items = cx.iterable_values(from);
    if (cx.throw_pending()) { return false; }
    if (!items.is_array()) {
        cx.throw_error("TypeError", "The provided value cannot be converted to a sequence.");
        return false;
    }
    for (const value & entry : static_cast<script::array_object *>(items.as_heap())->items) {
        out.push_back(cx.to_string(entry));
        if (cx.throw_pending()) { return false; }
    }
    return true;
}

} // namespace

// --- lookups ------------------------------------------------------------------

std::size_t dom_bindings::custom_definition_for(const read_txn & txn, node_id id) const {
    // HTML 4.13.3 "look up a custom element definition": null for a document
    // with no browsing context, and otherwise THIS document's registry.
    if (!has_browsing_context()) { return npos; }
    const std::string_view tag = atoms_->text(txn.tag(id).value_or(atom{}));
    const std::vector<custom_element_definition> & defs = primary().custom_definitions_;
    for (std::size_t i = 0; i < defs.size(); ++i) {
        const custom_element_definition & def = defs[i];
        if (def.registry != registry_ || def.local_name != tag) { continue; }
        if (def.name == def.local_name) { return i; }
        // A customized built-in is named by its `is` attribute.
        if (txn.attribute_value(id, atoms_->intern("is")) == def.name) { return i; }
    }
    return npos;
}

std::size_t dom_bindings::custom_definition_of(context & cx, value receiver) {
    const std::vector<custom_element_definition> & defs = primary().custom_definitions_;
    dom_bindings * owner = owner_of(receiver);
    if (const node_id held = owner == nullptr ? node_id{} : owner->handle_of(receiver)) {
        // AN UPGRADE IN PROGRESS: the receiver is the element on top of some
        // definition's construction stack.
        for (std::size_t i = 0; i < defs.size(); ++i) {
            const auto & stack = defs[i].construction_stack;
            if (!stack.empty() && stack.back().element == held &&
                defs[i].registry->document == owner) {
                return i;
            }
        }
        return npos;
    }
    // `new C()`: the receiver was made from C.prototype, and C is the class
    // the definition holds - EXACTLY that prototype, not one on its chain,
    // because a subclass nobody defined is not a defined element (HTML
    // 4.13.4 step 3 looks the definition up by the constructor itself).
    const value proto = cx.get_prototype(receiver);
    for (std::size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].prototype.bits() == proto.bits()) { return i; }
    }
    return npos;
}

void dom_bindings::sync_custom_element_roots() {
    dom_bindings & top = primary();
    if (top.custom_elements_interface_ == nullptr) { return; }
    std::vector<value> & roots = top.custom_elements_interface_->retained;
    roots.clear();
    for (const custom_element_definition & def : top.custom_definitions_) {
        for (const value & held :
             {def.constructor, def.prototype, def.connected, def.disconnected, def.adopted,
              def.attribute_changed, def.connected_move, def.form_associated_callback,
              def.form_reset, def.form_disabled, def.form_state_restore}) {
            roots.push_back(held);
        }
    }
    for (const auto & reg : top.registries_) {
        roots.push_back(reg->object);
        for (const auto & [name, promise] : reg->when_defined) { roots.push_back(promise); }
    }
    roots.push_back(top.construct_fence_);
    roots.push_back(top.custom_elements_registry_prototype_);
}

dom_bindings::custom_element_registry & dom_bindings::registry_of(value receiver) {
    dom_bindings & top = primary();
    const auto found = top.registry_objects_.find(receiver.bits());
    return found == top.registry_objects_.end() ? *top.registry_ : *found->second;
}

dom_bindings::custom_element_registry & dom_bindings::make_registry(dom_bindings * document,
                                                                    value object) {
    dom_bindings & top = primary();
    auto & made = *top.registries_.emplace_back(std::make_unique<custom_element_registry>());
    made.document = document;
    made.object = object;
    top.registry_objects_.emplace(object.bits(), &made);
    sync_custom_element_roots();
    return made;
}

value dom_bindings::custom_elements_registry(context & cx) {
    if (registry_ != nullptr) { return registry_->object; }
    auto * registry = cx.allocate<script::object_object>();
    registry->prototype = primary().custom_elements_registry_prototype_;
    // Only the page and a frame ask for one: a document a page made has no
    // window to hang it on. Having a registry is having a browsing context.
    registry_ = &make_registry(this, value::object(registry));
    return registry_->object;
}

// --- the HTML element constructor ---------------------------------------------

value dom_bindings::construct_html_element(context & c, value self,
                                           std::string_view interface_name) {
    if (!self.is_object_like()) {
        c.throw_error("TypeError", "Failed to construct '" + std::string{interface_name} +
                                       "': please use the 'new' operator.");
        return value::undefined();
    }
    const bool is_element = owner_of(self) != nullptr;
    // HTML 4.13.4 step 2: NewTarget equal to the active function object - the
    // receiver made from THIS interface's own prototype - is a TypeError
    // whether or not somebody defined the interface itself as an element.
    if (!is_element) {
        const value own = c.lookup_property(c.global(std::string{interface_name}), "prototype");
        if (own.bits() == c.get_prototype(self).bits()) {
            c.throw_error("TypeError", "Illegal constructor");
            return value::undefined();
        }
    }
    const std::size_t index = custom_definition_of(c, self);
    if (index == npos) {
        c.throw_error("TypeError", "Illegal constructor");
        return value::undefined();
    }
    custom_element_definition & def = primary().custom_definitions_[index];
    // Steps 5-6: an autonomous definition constructs through HTMLElement and
    // nothing else; a customized built-in through the interface its `extends`
    // tag has - `class extends HTMLButtonElement` with `{extends: "p"}` is a
    // TypeError at `super()`.
    const std::string_view wanted =
        def.name == def.local_name ? "HTMLElement" : interface_name_for_tag(def.local_name);
    if (wanted != interface_name) {
        c.throw_error("TypeError", "Illegal constructor: the custom element definition does not "
                                   "extend " +
                                       std::string{interface_name});
        return value::undefined();
    }
    // Step 7: AN UPGRADE. The element is on the construction stack; `super()`
    // gives it the class's prototype and marks it constructed. A second
    // `super()` on the same element is the "already constructed marker".
    if (is_element) {
        custom_element_definition::construction & top = def.construction_stack.back();
        if (top.constructed) {
            throw_dom_exception(c, "InvalidStateError",
                                "The HTML element constructor was called twice for one upgrade");
            return value::undefined();
        }
        top.constructed = true;
        static_cast<script::object_object *>(self.as_heap())->prototype = def.prototype;
        return self;
    }
    // Steps 8-13: a NEW element, custom from the start, IN THE DOCUMENT WHOSE
    // REGISTRY HOLDS THE DEFINITION - a frame's, when the class was defined
    // through `frame.contentWindow.customElements`.
    dom_bindings & owner = def.registry->document == nullptr ? primary() : *def.registry->document;
    const node_id made = owner.doc_->create_element(atoms_->intern(def.local_name));
    if (def.local_name != def.name) {
        (void)owner.doc_->set_attribute(made, atoms_->intern("is"), def.name);
    }
    // THE RECEIVER BECOMES THE WRAPPER - what wrap() does to a fresh object,
    // done to the instance `new` made, whose prototype is the author's class.
    auto * obj = static_cast<script::object_object *>(self.as_heap());
    obj->set(std::string{handle_property}, value::number(static_cast<double>(made.key())));
    owner.install_element_views(c, *obj, made);
    owner.refresh_element(c, *obj, made);
    owner.wrappers_.emplace(made.key(), obj);
    custom_element_state state;
    state.definition = index;
    owner.custom_elements_.emplace(made.key(), std::move(state));
    // A customized built-in <script> is a script nothing has started, exactly
    // as createElement("script") notes one: its text runs when it connects,
    // and before its connectedCallback (the reactions follow the
    // post-connection steps).
    if (def.local_name == "script") { owner.note_unstarted_script(made); }
    return self;
}

// --- creation -------------------------------------------------------------------

value dom_bindings::construct_fenced(context & cx, value constructor, bool & threw,
                                     value & thrown) {
    threw = false;
    thrown = value::undefined();
    value & fence = primary().construct_fence_;
    if (!fence.is_callable()) {
        // `new C()` from JavaScript, where a throw has a `catch` to land in:
        // from C++ a throw crossing construct() unwinds to whatever page code
        // is above the native, which is the page's `try`, not this one's.
        script::program compiled = script::compiler::compile(
            "return (function (C) { try { return [true, new C()]; } catch (e) { return [false, "
            "e]; } });\n");
        if (compiled.ok) { fence = cx.run_nested(cx.own_program(std::move(compiled))); }
        sync_custom_element_roots();
    }
    if (!fence.is_callable()) {
        const value made = cx.construct(constructor, {});
        threw = cx.throw_pending();
        return made;
    }
    const value passed[1] = {constructor};
    const value answer = cx.call(fence, passed);
    if (!answer.is_array()) {
        threw = true;
        return value::undefined();
    }
    const auto & items = static_cast<script::array_object *>(answer.as_heap())->items;
    if (items.size() < 2 || !context::truthy(items[0])) {
        threw = true;
        thrown = items.size() >= 2 ? items[1] : value::undefined();
        return value::undefined();
    }
    return items[1];
}

void dom_bindings::report_custom_element_exception(context & cx, value thrown,
                                                   std::string_view where) {
    const context::rooted keep{cx, thrown};
    const std::string fault = std::string{where} + ": uncaught " + describe(cx, thrown);
    const bool handled = primary().dispatch_error_value(fault, thrown);
    if (!handled && callback_error_.empty()) { callback_error_ = fault; }
}

value dom_bindings::create_html_element(context & cx, const std::string & name, value options) {
    const atom tag = atoms_->intern_lower(name);
    const std::string_view lowered = atoms_->text(tag);
    // DOM 4.9.2 "create an element": the `is` option names a customized
    // built-in; a definition is looked up only in a document that has a
    // browsing context (HTML 4.13.3 "look up a custom element definition").
    std::string is;
    if (options.is_object_like()) {
        const value given = cx.lookup_property(options, "is");
        if (cx.throw_pending()) { return value::undefined(); }
        if (!given.is_nullish()) { is = cx.to_string(given); }
    }
    dom_bindings & reg = primary();
    std::size_t index = npos;
    if (has_browsing_context()) {
        for (std::size_t i = 0; i < reg.custom_definitions_.size(); ++i) {
            const custom_element_definition & def = reg.custom_definitions_[i];
            if (def.registry != registry_ || def.local_name != lowered) { continue; }
            if (def.name == def.local_name || def.name == is) {
                index = i;
                break;
            }
        }
    }
    if (index == npos) {
        const node_id made = doc_->create_element(tag);
        // A candidate for a definition that may come later: its `is` value.
        if (!is.empty() && is_valid_custom_element_name(is) && !lowered.contains('-')) {
            (void)doc_->set_attribute(made, atoms_->intern("is"), is);
        }
        return wrap(cx, made);
    }
    // SYNCHRONOUSLY, through the author's class (the synchronous custom
    // elements flag is set): `super()` makes the receiver the wrapper of the
    // node, so the instance IS the element - and everything the constructor
    // did to it is checked, steps 6.1.3 to 6.1.9.
    const value constructor = reg.custom_definitions_[index].constructor;
    bool threw = false;
    value thrown = value::undefined();
    const value made = construct_fenced(cx, constructor, threw, thrown);
    const context::rooted keep_made{cx, made};
    std::string failure;
    std::string failure_name = "NotSupportedError";
    if (!threw) {
        dom_bindings * home = owner_of(made);
        const node_id result = home == nullptr ? node_id{} : home->handle_of(made);
        const auto txn = (home == nullptr ? *this : *home).doc_->read();
        if (!result || txn.kind(result).value_or(node_kind::text) != node_kind::element) {
            failure_name = "TypeError";
            failure = "the custom element constructor did not return an element";
        } else if (home != this) {
            failure = "the custom element was adopted into another document during "
                      "construction";
        } else if (txn.local_name(result) != lowered) {
            failure = "the custom element constructor returned an element with the wrong "
                      "local name";
        } else if (txn.parent(result)) {
            failure = "the custom element was inserted into a parent during construction";
        } else if (!txn.children(result).empty()) {
            failure = "the custom element gained children during construction";
        } else {
            for (const attribute & held : txn.attributes(result)) {
                if (atoms_->text(held.name) != "is") {
                    failure = "the custom element gained attributes during construction";
                    break;
                }
            }
        }
        if (!failure.empty()) {
            thrown = failure_name == "TypeError" ? cx.make_error("TypeError", failure)
                                                 : make_dom_exception(cx, failure_name, failure);
        }
    }
    if (threw || !failure.empty()) {
        // "Report the exception, and set result to a new HTMLUnknownElement" -
        // a constructor that threw, or never reached HTMLElement, does not
        // make createElement throw. Custom element state "failed".
        report_custom_element_exception(cx, thrown, "custom element constructor");
        const node_id made_instead = doc_->create_element(tag);
        custom_element_state state;
        state.definition = index;
        state.state = custom_element_state::status::failed;
        custom_elements_.emplace(made_instead.key(), std::move(state));
        return wrap(cx, made_instead);
    }
    // createElement notes a <script> it made as unstarted itself, and the
    // constructor already did: one entry, not two runs.
    if (lowered == "script") { std::erase(unstarted_scripts_, handle_of(made)); }
    return made;
}

// --- the scan ----------------------------------------------------------------------

void dom_bindings::walk_custom_elements(const read_txn & txn, node_id start, bool connected,
                                        bool upgrade, std::vector<node_id> & roots_seen) {
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
                    state.visited = true;
                    state.parent = txn.parent(here.at);
                    enqueue(here.at, index, kind::upgrade);
                    diff_attributes(def, state, here.at);
                    if (connected) { enqueue(here.at, index, kind::connected); }
                    diff_form(def, state, here.at);
                    custom_elements_.emplace(key, std::move(state));
                }
            } else if (found != custom_elements_.end()) {
                custom_element_state & state = found->second;
                state.visited = true;
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
            roots_seen.push_back(shadow);
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
        std::vector<node_id> roots_seen;
        walk_custom_elements(txn, root, connected, true, roots_seen);
    }
    flush_custom_element_reactions(from);
}

void dom_bindings::scan_custom_elements() {
    const auto txn = doc_->read();
    for (auto & [key, state] : custom_elements_) { state.visited = false; }
    std::vector<node_id> roots_seen;
    const bool upgrade = has_browsing_context();
    walk_custom_elements(txn, txn.root(), true, upgrade, roots_seen);
    // Every shadow tree the walk did not cross - one whose host is detached -
    // connected when its host's shadow-including root is the document.
    for (const node_id root : doc_->shadow_roots()) {
        bool seen = false;
        for (const node_id crossed : roots_seen) { seen = seen || crossed == root; }
        if (seen) { continue; }
        const node_id top = root_of_tree(txn, shadow_tree_of(root)->host, true);
        const bool connected =
            top == txn.root() || txn.kind(top).value_or(node_kind::element) == node_kind::document;
        walk_custom_elements(txn, root, connected, upgrade, roots_seen);
    }
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
    std::vector<node_id> loose;
    for (const auto & [key, state] : custom_elements_) {
        if (!state.visited) { loose.push_back(unpack(key)); }
    }
    for (const node_id at : loose) {
        if (txn.contains(at)) { walk_custom_elements(txn, at, false, false, roots_seen); }
    }
    // A node whose WRAPPER LEFT WITH IT - `node_from` adopted it into another
    // document and rebound the page's object to the copy - is that
    // document's custom element now: its state moves over, and the adopting
    // steps (HTML 4.13.6) enqueue adoptedCallback there, after the
    // disconnectedCallback the walk above queued here.
    for (auto it = custom_elements_.begin(); it != custom_elements_.end();) {
        const auto away = adopted_away_.find(it->first);
        if (away == adopted_away_.end()) {
            ++it;
            continue;
        }
        dom_bindings * now = owner_of(value::object(away->second));
        if (now != nullptr && now != this) {
            const node_id fresh = now->handle_of(value::object(away->second));
            custom_element_state state = it->second;
            state.connected = false;
            state.parent = node_id{};
            state.visited = false;
            if (fresh && state.state == custom_element_state::status::custom) {
                custom_element_reaction reaction;
                reaction.target = fresh;
                reaction.definition = state.definition;
                reaction.what = custom_element_reaction::kind::adopted;
                reaction.old_document = document_;
                reaction.new_document = now->document_;
                now->custom_reactions_.push_back(std::move(reaction));
                now->custom_elements_.insert_or_assign(fresh.key(), std::move(state));
            }
        }
        it = custom_elements_.erase(it);
    }
    // A node that is GONE, rather than merely detached, is forgotten.
    for (auto it = custom_elements_.begin(); it != custom_elements_.end();) {
        if (!it->second.visited && !txn.contains(unpack(it->first))) {
            it = custom_elements_.erase(it);
            continue;
        }
        ++it;
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
}

// --- the interfaces ---------------------------------------------------------------

void dom_bindings::install_custom_elements(context & cx) {
    // --- HTMLElement, CONSTRUCTIBLE. Defined here, before install_dom_interfaces
    // --- builds the table, which adopts a global that already exists rather
    // --- than replacing it with the throwing stub every other interface gets.
    auto * html_element_proto = cx.allocate<script::object_object>();
    auto * html_element_ctor = cx.allocate<script::native_object>(
        "HTMLElement", [this](context & c, std::span<value>) -> value {
            return construct_html_element(c, c.current_this(), "HTMLElement");
        });
    html_element_ctor->set("prototype", value::object(html_element_proto));
    html_element_proto->define("constructor", value::object(html_element_ctor),
                               script::attr_builtin);
    cx.define_global("HTMLElement", value::object(html_element_ctor));
    install_element_internals(cx, *html_element_proto);

    // --- CustomElementRegistry.prototype -----------------------------------
    auto * registry_proto = cx.allocate<script::object_object>();
    custom_elements_registry_prototype_ = value::object(registry_proto);
    // (registry, name) -> the definition's index in the primary's vector.
    const auto defined = [this](const custom_element_registry & reg, std::string_view name) {
        for (std::size_t i = 0; i < custom_definitions_.size(); ++i) {
            if (custom_definitions_[i].registry == &reg && custom_definitions_[i].name == name) {
                return i;
            }
        }
        return npos;
    };

    set_method(
        cx, *registry_proto, "define",
        [this, defined](context & c, std::span<value> args) -> value {
            custom_element_registry & reg = registry_of(c.current_this());
            // HTML 4.13.4 "element definition", in the specification's order:
            // the constructor, the name, the name twice over, the `extends`,
            // the running flag, then the prototype and everything read off it.
            const value ctor = arg(args, 1);
            if (!script::is_constructor(ctor)) {
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
            if (defined(reg, name) != npos) {
                throw_dom_exception(c, "NotSupportedError",
                                    "the name '" + name +
                                        "' has already been used with this registry");
                return value::undefined();
            }
            for (const custom_element_definition & def : custom_definitions_) {
                if (def.registry == &reg && def.constructor.bits() == ctor.bits()) {
                    throw_dom_exception(
                        c, "NotSupportedError",
                        "this constructor has already been used with this registry");
                    return value::undefined();
                }
            }
            std::string local_name = name;
            const value options = arg(args, 2);
            if (options.is_object_like()) {
                const value extends = c.lookup_property(options, "extends");
                if (c.throw_pending()) { return value::undefined(); }
                if (!extends.is_undefined() && !extends.is_null()) {
                    local_name = c.to_string(extends);
                    if (is_valid_custom_element_name(local_name)) {
                        throw_dom_exception(c, "NotSupportedError",
                                            "'" + local_name +
                                                "' is a custom element name, not a "
                                                "built-in element to extend");
                        return value::undefined();
                    }
                    if (interface_name_for_tag(local_name) == "HTMLUnknownElement") {
                        throw_dom_exception(c, "NotSupportedError",
                                            "'" + local_name +
                                                "' is not a built-in element to extend");
                        return value::undefined();
                    }
                }
            }
            if (reg.running) {
                throw_dom_exception(c, "NotSupportedError",
                                    "customElements.define is already running");
                return value::undefined();
            }
            reg.running = true;
            // Steps 8-9: everything read off the constructor and its
            // prototype, with the flag cleared however it ends.
            custom_element_definition def;
            def.name = name;
            def.local_name = local_name;
            def.registry = &reg;
            def.constructor = ctor;
            const bool read = [&] {
                def.prototype = c.lookup_property(ctor, "prototype");
                if (c.throw_pending()) { return false; }
                if (!def.prototype.is_object_like()) {
                    c.throw_error("TypeError",
                                  "Failed to execute 'define' on 'CustomElementRegistry': "
                                  "The 'prototype' property is not an object");
                    return false;
                }
                const auto callback = [&](const char * which, value & into) {
                    into = c.lookup_property(def.prototype, which);
                    if (c.throw_pending()) { return false; }
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
                    !callback("connectedMoveCallback", def.connected_move) ||
                    !callback("adoptedCallback", def.adopted) ||
                    !callback("attributeChangedCallback", def.attribute_changed)) {
                    return false;
                }
                if (def.attribute_changed.is_callable()) {
                    const value observed = c.lookup_property(ctor, "observedAttributes");
                    if (c.throw_pending()) { return false; }
                    if (!observed.is_undefined() &&
                        !string_sequence(c, observed, def.observed_attributes)) {
                        return false;
                    }
                }
                const value disabled = c.lookup_property(ctor, "disabledFeatures");
                if (c.throw_pending()) { return false; }
                if (!disabled.is_undefined()) {
                    std::vector<std::string> features;
                    if (!string_sequence(c, disabled, features)) { return false; }
                    for (const std::string & feature : features) {
                        def.disable_internals = def.disable_internals || feature == "internals";
                        def.disable_shadow = def.disable_shadow || feature == "shadow";
                    }
                }
                const value form_associated = c.lookup_property(ctor, "formAssociated");
                if (c.throw_pending()) { return false; }
                def.form_associated = context::truthy(form_associated);
                if (def.form_associated) {
                    if (!callback("formAssociatedCallback", def.form_associated_callback) ||
                        !callback("formResetCallback", def.form_reset) ||
                        !callback("formDisabledCallback", def.form_disabled) ||
                        !callback("formStateRestoreCallback", def.form_state_restore)) {
                        return false;
                    }
                }
                return true;
            }();
            reg.running = false;
            if (!read) { return value::undefined(); }
            custom_definitions_.push_back(std::move(def));
            sync_custom_element_roots();
            // The candidates in the registry's document are upgraded, then
            // whenDefined settles.
            if (reg.document != nullptr) { reg.document->react_custom_elements(); }
            if (const auto waiting = reg.when_defined.find(name);
                waiting != reg.when_defined.end()) {
                const value promise = waiting->second;
                reg.when_defined.erase(waiting);
                c.settle_promise(promise, ctor, false);
            }
            sync_custom_element_roots();
            return value::undefined();
        },
        script::attr_builtin);

    set_method(
        cx, *registry_proto, "get",
        [this, defined](context & c, std::span<value> args) {
            const std::size_t index =
                defined(registry_of(c.current_this()), arg_string(c, args, 0));
            return index == npos ? value::undefined() : custom_definitions_[index].constructor;
        },
        script::attr_builtin);

    set_method(
        cx, *registry_proto, "getName",
        [this](context & c, std::span<value> args) {
            const value ctor = arg(args, 0);
            if (!script::is_constructor(ctor)) {
                c.throw_error("TypeError", "Failed to execute 'getName' on "
                                           "'CustomElementRegistry': parameter 1 is not of "
                                           "type 'Function'.");
                return value::undefined();
            }
            const custom_element_registry & reg = registry_of(c.current_this());
            for (const custom_element_definition & def : custom_definitions_) {
                if (def.registry == &reg && def.constructor.bits() == ctor.bits()) {
                    return cx_->string(def.name);
                }
            }
            return value::null();
        },
        script::attr_builtin);

    set_method(
        cx, *registry_proto, "whenDefined",
        [this, defined](context & c, std::span<value> args) {
            custom_element_registry & reg = registry_of(c.current_this());
            const std::string name = arg_string(c, args, 0);
            if (!is_valid_custom_element_name(name)) {
                return c.make_promise(
                    make_dom_exception(c, "SyntaxError",
                                       "'" + name + "' is not a valid custom element name"),
                    true);
            }
            if (const std::size_t index = defined(reg, name); index != npos) {
                return c.make_promise(custom_definitions_[index].constructor, false);
            }
            // ONE promise per name, however often it is asked for.
            if (const auto held = reg.when_defined.find(name); held != reg.when_defined.end()) {
                return held->second;
            }
            const value promise = c.make_pending_promise();
            reg.when_defined.emplace(name, promise);
            sync_custom_element_roots();
            return promise;
        },
        script::attr_builtin);

    set_method(
        cx, *registry_proto, "upgrade",
        [this](context & c, std::span<value> args) {
            // "Try to upgrade" every candidate under the node, against the
            // registry of the node's own document.
            const value given = arg(args, 0);
            dom_bindings * owner = owner_of(given);
            node_id root = owner == nullptr ? node_id{} : owner->handle_of(given);
            if (!root && given.is_object_like()) {
                for (dom_bindings * each : {static_cast<dom_bindings *>(this), owner}) {
                    if (each != nullptr && each->document_.is_object_like() &&
                        given.bits() == each->document_.bits()) {
                        owner = each;
                        root = each->doc_->root();
                    }
                }
            }
            if (!root || owner == nullptr) {
                c.throw_error("TypeError",
                              "Failed to execute 'upgrade' on 'CustomElementRegistry': "
                              "parameter 1 is not of type 'Node'.");
                return value::undefined();
            }
            if (custom_definitions_.empty() || !owner->has_browsing_context()) {
                return value::undefined();
            }
            const std::size_t from = owner->custom_reactions_.size();
            {
                const auto txn = owner->doc_->read();
                const node_id top = owner->root_of_tree(txn, root, true);
                for (auto & [key, state] : owner->custom_elements_) { state.visited = false; }
                std::vector<node_id> roots_seen;
                owner->walk_custom_elements(txn, root,
                                            top == txn.root() ||
                                                txn.kind(top).value_or(node_kind::element) ==
                                                    node_kind::document,
                                            true, roots_seen);
            }
            owner->flush_custom_element_reactions(from);
            return value::undefined();
        },
        script::attr_builtin);

    // `new CustomElementRegistry()` - a SCOPED registry (HTML 4.13.3): its
    // definitions belong to no document, so no parser, createElement or
    // clone looks them up; `new C()` through a class it holds constructs in
    // the page's document.
    // ponytail: `initialize()`, `customElementRegistry` on elements, shadow
    // roots and creation options are not here - an element cannot carry a
    // scoped registry yet, so a scoped definition never upgrades anything.
    auto * registry_ctor = cx.allocate<script::native_object>(
        "CustomElementRegistry", [this](context & c, std::span<value>) {
            const value self = c.current_this();
            if (!self.is_object()) {
                c.throw_error("TypeError", "Failed to construct 'CustomElementRegistry': please "
                                           "use the 'new' operator.");
                return value::undefined();
            }
            (void)make_registry(nullptr, self);
            return self;
        });
    registry_ctor->set("prototype", value::object(registry_proto));
    registry_proto->define("constructor", value::object(registry_ctor), script::attr_builtin);
    cx.define_global("CustomElementRegistry", value::object(registry_ctor));
    custom_elements_interface_ = registry_ctor;

    // `window.customElements` - a bare global, which the window proxy answers
    // for `window.customElements` and `self.customElements` too.
    cx.define_global("customElements", custom_elements_registry(cx));
}

} // namespace ctbrowser::shell
