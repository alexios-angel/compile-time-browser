//===- computed_style/object.cpp - getComputedStyle object ------*- C++ -*-===//
//
// dom_bindings: the live, read-only CSSStyleDeclaration getComputedStyle hands
// back, and the `getComputedStyle` global itself.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <ctbrowser/style/css/selector.hpp>

#include <memory>

namespace ctbrowser::shell {

using namespace detail;

namespace {

/// One element's computed entries, cached while nothing that could change them
/// has moved. The object computed_style_object returns is live, so every
/// property read must be able to re-derive, and re-deriving costs the probe's
/// tree walks plus every property's value; a page reading one property per
/// element would pay that per read. The stamps below record the state the
/// entries were computed at; while all four match, a read gives the same
/// answer.
struct computed_cache {
    std::vector<std::pair<std::string, std::string>> entries;
    /// document::version() at computation: DOM changes, including style
    /// attributes.
    std::uint64_t stamp = 0;
    /// dom_bindings::style_stamp(): a stylesheet edited through the CSSOM
    /// (insertRule, replaceSync), which changes which rules match without
    /// touching the DOM.
    std::uint64_t styles = 0;
    /// dom_bindings::animation_stamp(): animations change what an element
    /// computes to without touching the document (a seek, a pause, or the
    /// clock moving under a running one). Zero for a page that has none.
    std::uint64_t animations = 0;
    std::uint64_t restyles = 0; ///< dom_bindings::restyle_stamp().
};

/// The private key under which the read-only declaration keeps its answer to
/// one of CSSStyleDeclaration's members (see the end of computed_style_object).
/// The prototype's members read the same keys
/// (stylesheets/prototypes/declarations.cpp).
[[nodiscard]] std::string computed_member_key(std::string_view member) {
    return std::string{script::private_key_prefix} + "computed-member:" + std::string{member};
}

/// Whether `getComputedStyle`'s second argument requests a pseudo-element
/// (CSSOM §5.1). An argument that is null, absent, empty, or does not begin
/// with a colon is ignored: `getComputedStyle(div, "before")` answers about
/// the element itself ("the argument is ignored (due to no colon)" in
/// getComputedStyle-pseudo). Anything that begins with a colon is a
/// pseudo-element request and gets either the pseudo-element's style or, when
/// it does not parse or names one the engine has no style for, an empty
/// CSSStyleDeclaration (`length === 0`, every property ""), as CSSOM requires
/// for a pseudo-element that does not exist. resolvable_pseudo decides which.
[[nodiscard]] bool names_a_pseudo_element(context & c, value given) {
    if (given.is_nullish()) { return false; }
    const std::string text = c.to_string(given);
    return !text.empty() && text.front() == ':';
}

/// The pseudo-element a second argument names, when it parses as a
/// <pseudo-element-selector> the selector parser knows: one or two colons (one
/// only for the four legacy names), then exactly one identifier, escapes
/// decoded, case folded, and nothing after it. Returns "" otherwise, which the
/// caller answers with the empty declaration; `::before ` and `::before,` are
/// not selectors (getComputedStyle-pseudo).
[[nodiscard]] std::string resolvable_pseudo(std::string_view text) {
    if (text.size() < 2 || text.front() != ':') { return {}; }
    const bool doubled = text[1] == ':';
    text.remove_prefix(doubled ? 2 : 1);
    using style::css::token_type;
    const style::css::token_stream ts = style::css::tokenize(text);
    // A functional pseudo-element (`::highlight(name)`, `::picker(select)`,
    // `::view-transition-group(name)`) takes exactly one identifier, with
    // whitespace around it and the `)` optional, since EOF closes a block
    // (getComputedStyle-pseudo-with-argument). It names the pseudo-element
    // the cascade knows as `name(argument)`, the argument case-folded the
    // way the selector compiler interns it.
    if (doubled && ts.tokens.front().type == token_type::function) {
        std::string_view function = ts.text_of(ts.tokens.front());
        if (function.ends_with('(')) { function.remove_suffix(1); }
        const std::string name = ascii_lower_copy(function);
        std::string argument;
        bool closed = false;
        for (std::size_t i = 1; i < ts.tokens.size(); ++i) {
            const style::css::css_token & t = ts.tokens[i];
            if (t.type == token_type::whitespace) { continue; }
            if (t.type == token_type::eof) { break; }
            if (closed) { return {}; } // `::highlight(name)a`
            if (t.type == token_type::close_paren) {
                closed = true;
            } else if (t.type == token_type::ident && argument.empty()) {
                argument = ascii_lower_copy(ts.text_of(t));
            } else {
                return {};
            }
        }
        if (argument.empty()) { return {}; }
        const bool takes_ident = name == "highlight" || name == "view-transition-group" ||
                                 name == "view-transition-image-pair" ||
                                 name == "view-transition-old" || name == "view-transition-new";
        if (!takes_ident && !(name == "picker" && argument == "select")) { return {}; }
        return name + "(" + argument + ")";
    }
    if (ts.tokens.size() != 2 || ts.tokens.front().type != token_type::ident) { return {}; }
    const std::string name = ascii_lower_copy(ts.text_of(ts.tokens.front()));
    const bool legacy =
        name == "before" || name == "after" || name == "first-line" || name == "first-letter";
    if (!doubled && !legacy) { return {}; }
    // A vendor's pseudo-element has no cascade here, and answers empty
    // (getComputedStyle-pseudo "::-webkit-file-upload-button").
    if (name.starts_with('-')) { return {}; }
    return style::css::known_pseudo_element(name) ? name : std::string{};
}

} // namespace

value dom_bindings::computed_style_object(context & cx, node_id id) {
    return computed_style_object(cx, id, atom{});
}

value dom_bindings::computed_style_object(context & cx, node_id id, atom pseudo) {
    // One object per element and pseudo-element, held on the wrapper under a
    // private key so it lives and dies with it. `getComputedStyle(el) ===
    // getComputedStyle(el)` in every browser, and building one costs an
    // accessor per property in both spellings, which the interpolation harness
    // would pay per subtest per sample. The one thing fixed at creation is
    // `length` (see below): an object made for an unrendered element is remade
    // once the element has a style.
    script::object_object * wrapper = nullptr;
    std::string slot;
    if (id) {
        if (const auto it = wrappers_.find(id.key()); it != wrappers_.end()) {
            wrapper = it->second;
        }
    }
    if (wrapper != nullptr) {
        slot = std::string{script::private_key_prefix} +
               "computed:" + std::string{pseudo ? atoms_->text(pseudo) : std::string_view{}};
        if (const value * cached = wrapper->find(slot); cached != nullptr && cached->is_object()) {
            auto * made = static_cast<script::object_object *>(cached->as_heap());
            const value * length = made->find(computed_member_key("length"));
            const bool rendered =
                styles_ != nullptr && styles_->find(style::engine::key_of(id)) != styles_->end();
            if (length != nullptr && (context::to_number(*length) != 0 || !rendered)) {
                return *cached;
            }
        }
    }
    auto * held = cx.allocate<script::object_object>();
    if (wrapper != nullptr) { wrapper->set(slot, value::object(held)); }

    // The current entries: the element's, or its pseudo-element's, resolved on
    // demand through the engine against the element's own resolved style, so a
    // `::before` reads the sheet as it stands and inherits what the element has.
    const auto entries_now = [this, id, pseudo]() {
        if (!pseudo) { return computed_style_entries(id); }
        std::vector<std::pair<std::string, std::string>> none;
        if (selector_engine_ == nullptr || styles_ == nullptr) { return none; }
        style::computed_style_ptr own;
        if (const auto found = styles_->find(style::engine::key_of(id));
            found != styles_->end() && found->second) {
            own = found->second;
        }
        const auto txn = doc_->read();
        const style::computed_style_ptr made =
            selector_engine_->resolve_pseudo(txn, id, pseudo, own, styles_);
        if (!made) { return none; }
        std::vector<std::pair<std::string, std::string>> entries = computed_style_entries(id, made);
        // `content: normal` on ::before and ::after computes to `none` (CSS
        // Generated Content 3 §1.1): they have no content of their own.
        const std::string_view name = atoms_->text(pseudo);
        if (ascii_iequals_any(name, {"before", "after", "::before", "::after"})) {
            for (auto & [property, text] : entries) {
                if (property == "content" && ascii_iequals(text, "normal")) { text = "none"; }
            }
        }
        return entries;
    };
    const auto cached = std::make_shared<computed_cache>();
    cached->entries = entries_now();
    cached->stamp = doc_->version();
    cached->styles = style_stamp();
    cached->animations = animation_stamp();
    cached->restyles = restyle_stamp();

    // The live read, and the flush it needs. Every property re-derives, so a
    // page holding the object across a write sees the write (CSSOM's "live";
    // getComputedStyle-display-none-001/-002). Re-deriving is only correct
    // once the pipeline has caught up: `el.style.color = 'green'` marks the
    // cascade stale and nothing resolves it until the next frame, so a read
    // that skipped the flush would answer from before the page's own write.
    //
    // The flush is reached through the global, the only channel a binding has.
    // browser::run_scripts wraps `getComputedStyle` with a native that runs the
    // stages `dirty_` says are stale; the bindings deliberately do not know
    // layout exists (see the top of shell/bindings.hpp), so calling that
    // wrapper with no arguments is how a property read asks for the flush.
    // With no argument the inner native makes one empty object and returns, so
    // the call costs only the flush. A page that has replaced the global gets
    // no flush rather than a call into its own function: the kind check makes
    // that safe.
    //
    // This stands in for a shared flush hook on dom_bindings, which
    // getBoundingClientRect, offsetWidth and clientHeight also want; replace it
    // once that exists.
    const auto refresh = [this, cached, entries_now](context & c) {
        const std::uint64_t now = doc_->version();
        if (now == cached->stamp && style_stamp() == cached->styles &&
            animation_stamp() == cached->animations && restyle_stamp() == cached->restyles) {
            return;
        }
        const value flush = c.global("getComputedStyle");
        if (flush.is_kind(script::heap_kind::native)) {
            (void)c.call(flush, std::span<const value>{});
        }
        cached->entries = entries_now();
        cached->stamp = doc_->version();
        cached->styles = style_stamp();
        cached->animations = animation_stamp();
        cached->restyles = restyle_stamp();
    };
    const auto answer = [cached](std::string_view name) -> std::string {
        for (const auto & [key, text] : cached->entries) {
            if (std::string_view{key} == name) { return text; }
        }
        return {};
    };

    // A computed style is read-only: every mutating member throws
    // NoModificationAllowedError (CSSOM §6.7.2), so a page cannot believe a
    // write landed. One throwing setter object is shared by every property
    // accessor, so the rule costs a single allocation. computed-style-001 and
    // computed-style-set-property check `style.color =`, `style.cssText =` and
    // `style.setProperty()`.
    const value refuse = value::object(
        cx.allocate<script::native_object>("set", [this](context & c, std::span<value>) {
            throw_dom_exception(c, "NoModificationAllowedError",
                                "a computed style declaration is read-only");
            return value::undefined();
        }));
    const auto reader = [&cx, refresh, answer](const std::string & name) {
        return value::object(cx.allocate<script::native_object>(
            name, [refresh, answer, name](context & c, std::span<value>) {
                refresh(c);
                return c.string(answer(name));
            }));
    };

    // Every name the object answers to: the whole property table, longhands
    // and shorthands alike, because CSSOM gives a computed style an attribute
    // for every supported property; then whatever this element declared that
    // the table does not know.
    //
    // Both spellings share one getter: `background-color` and
    // `backgroundColor` are one property read two ways, and a second native per
    // name would double the allocations per getComputedStyle call.
    const auto publish = [&](const std::string & css_name) {
        const value get = reader(css_name);
        held->define_accessor(css_name, get, refuse);
        const std::string idl = style::css::idl_name_of(css_name);
        if (idl != css_name) { held->define_accessor(idl, get, refuse); }
    };
    for (const style::css::property_syntax & p : style::css::known_properties()) {
        publish(std::string{p.name});
    }
    for (const auto & [name, text] : cached->entries) {
        if (style::css::find_property(name) == nullptr) { publish(name); }
    }
    // The legacy `-webkit-` aliases of properties the table has, CSSOM
    // §6.7.2's "webkit-cased attribute": `webkitTransition` is an attribute of
    // a computed style, and writing it throws like any other
    // (computed-style-set-property). They answer as the unprefixed one.
    for (const std::string_view aliased :
         {"transition", "transform", "animation", "filter", "box-sizing", "user-select"}) {
        if (style::css::find_property(aliased) == nullptr) { continue; }
        const value get = reader(std::string{aliased});
        const std::string css = "-webkit-" + std::string{aliased};
        held->define_accessor(css, get, refuse);
        held->define_accessor(style::css::idl_name_of(css), get, refuse);
    }

    // The indexed properties (CSSOM §6.7), as data properties below the
    // `length` the prototype answers from this object's private count; the
    // interface's @@iterator walks the two (stylesheets/prototypes/helpers.cpp).
    //
    // They are the supported longhands, in the lexicographic order
    // computed_style_entries put them in, without the shorthands that follow
    // them there. Empty for an element that is not being rendered (`length ===
    // 0` in getComputedStyle-detached-subtree).
    //
    // Unlike every value on the object, the list is fixed when the object is
    // made: a declaration taken for a detached element keeps `length === 0`
    // after the element is appended. Making it live would need an accessor per
    // index.
    std::vector<std::string> indexed;
    for (const auto & [name, text] : cached->entries) {
        const style::css::property_syntax * known = style::css::find_property(name);
        if (known != nullptr && !known->shorthand) { indexed.push_back(name); }
    }
    // Vendor-prefixed names go last, each run still in order: CSSOM's
    // "lexicographical order" sorts `-webkit-` after the standard names
    // (getComputedStyle-property-order), where a plain sort puts `-` first.
    std::ranges::stable_partition(indexed,
                                  [](const std::string & name) { return !name.starts_with('-'); });
    // Then the custom properties, every one the element has a value for
    // (cssstyledeclaration-custom-properties, -registered-...).
    for (const auto & [name, text] : cached->entries) {
        if (name.starts_with("--")) { indexed.push_back(name); }
    }
    for (std::size_t i = 0; i < indexed.size(); ++i) {
        held->set(std::to_string(i), cx.string(indexed[i]));
    }

    // CSSStyleDeclaration's own members (`length`, `cssText`, `item()`,
    // `getPropertyValue()` and the rest) are WebIDL attributes and operations,
    // so they live on its prototype and a computed style inherits them
    // (cssom/idlharness: "must inherit property"). What is here is only this
    // declaration's answer to each, under private keys the prototype's members
    // hand a computed receiver's call to
    // (stylesheets/prototypes/declarations.cpp spells the same keys).
    const auto member = [&](std::string_view name, value answer_value) {
        held->define(computed_member_key(name), answer_value, script::attr_none);
    };
    const auto native = [&](std::string_view name,
                            std::function<value(context &, std::span<value>)> fn) {
        return value::object(cx.allocate<script::native_object>(std::string{name}, std::move(fn)));
    };
    member("length", value::number(static_cast<double>(indexed.size())));
    // getPropertyValue takes the CSS name (the spelling the parity dump uses)
    // and accepts the IDL one too, so a page holding `backgroundColor` need not
    // hyphenate it. Live, like the accessors: css-style-declaration-
    // modifications edits a stylesheet rule and reads the value back this way.
    member("getPropertyValue",
           native("getPropertyValue", [refresh, answer](context & c, std::span<value> args) {
               if (args.empty()) { return c.string(std::string{}); }
               const std::string asked = style::css::css_name_of(c.to_string(args[0]));
               refresh(c);
               return c.string(answer(asked));
           }));
    // Always empty. Importance is a cascade input, settled by the time a value
    // is computed; this engine does not keep which declaration won past
    // resolve().
    member("getPropertyPriority", native("getPropertyPriority", [](context & c, std::span<value>) {
               return c.string("");
           }));
    member("item", native("item", [indexed](context & c, std::span<value> args) {
               if (args.empty()) { return c.string(std::string{}); }
               const double i = context::to_number(args[0]);
               if (!(i >= 0) || static_cast<std::size_t>(i) >= indexed.size()) {
                   return c.string("");
               }
               return c.string(indexed[static_cast<std::size_t>(i)]);
           }));
    const auto refuse_method = [this](context & c, std::span<value>) {
        throw_dom_exception(c, "NoModificationAllowedError",
                            "a computed style declaration is read-only");
        return value::undefined();
    };
    member("setProperty", native("setProperty", refuse_method));
    member("removeProperty", native("removeProperty", refuse_method));
    // A computed style's cssText getter returns the empty string, as the
    // specification says; engines disagree on how to rebuild shorthands, which
    // is also why the parity harness compares longhands only. The setter
    // throws (computed-style-001: `cs.cssText = "color: blue"`).
    member("cssText", native("cssText", [](context & c, std::span<value>) {
               return c.string(std::string{});
           }));
    member("cssText=", refuse);
    // The prototype that carries them is CSSStyleProperties.prototype, so
    // `getComputedStyle(el) instanceof CSSStyleDeclaration` and `[...style]`
    // runs the interface's own iterator.
    if (script::object_object * internals = cssom_internals(cx)) {
        if (const value * proto = internals->find("CSSStyleProperties.prototype")) {
            held->prototype = *proto;
        }
    }
    return value::object(held);
}

page::page_view dom_bindings::view_of_page() const {
    page::page_view view;
    view.doc = doc_;
    view.atoms = atoms_;
    view.styles = styles_;
    view.selector_engine = selector_engine_;
    view.boxes = boxes_;
    view.fragments = fragments_;
    view.viewport_width = viewport_width_;
    view.viewport_height = viewport_height_;
    view.viewport_scroll = viewport_scroll();
    view.scroll_offset_of = [this](node_id id) { return scroll_offset_of(id); };
    view.animated_values = [this](node_id id, float font_size,
                                  const std::function<std::string_view(std::string_view)> & under) {
        return animated_values(id, font_size, under);
    };
    return view;
}

std::vector<std::pair<std::string, std::string>> dom_bindings::computed_style_entries(node_id id) {
    return computed_style_entries(id, {});
}

std::vector<std::pair<std::string, std::string>> dom_bindings::computed_style_entries(
    node_id id, const style::computed_style_ptr & pseudo) {
    return page::computed_style_entries(view_of_page(), id, pseudo);
}

void dom_bindings::install_computed_style(context & cx) {
    // A bare global also serves `window.getComputedStyle`: `window` is a proxy
    // whose handler falls back to the globals (install_window). p5.js reads the
    // bare form and pages write both.
    //
    // browser::run_scripts wraps this global to flush a pending restyle before
    // it runs, since the bindings deliberately do not know layout exists;
    // without the flush every answer would predate the script's own write. A
    // live read reaches the same flush through this global (see `refresh` in
    // computed_style_object).
    cx.define_native("getComputedStyle", [this](context & c, std::span<value> args) {
        const node_id id = args.empty() ? node_id{} : handle_of(args[0]);
        if (!id) {
            // A node of another document (a frame's, which `handle_of` refuses
            // because its handle names a different slab) is answered by that
            // document's bindings, whose styles and boxes the browser's
            // layout_frames observed at the frame's size.
            if (!args.empty()) {
                if (dom_bindings * owner = owner_of(args[0]); owner != nullptr && owner != this) {
                    // Only a document the browser has laid out has styles; one
                    // it has not (a `display: none` frame's, or one from
                    // createHTMLDocument) is unrendered and answers the empty
                    // declaration (getComputedStyle-detached-subtree).
                    if (owner->styles_ == nullptr) { return computed_style_object(c, node_id{}); }
                    return owner->computed_style_object(c, owner->handle_of(args[0]));
                }
            }
            if (!args.empty() && args[0].is_object() &&
                static_cast<script::object_object *>(args[0].as_heap())
                        ->find(std::string{handle_property}) != nullptr) {
                return computed_style_object(c, node_id{});
            }
            return c.make_object();
        }
        // A second argument naming a pseudo-element: `::before`, `::after` and
        // the others resolvable_pseudo accepts resolve through the engine; any
        // other gets an empty declaration, spelled as an empty node handle
        // because `computed_style_entries` already answers nothing for it
        // (every property reads "", `length` is 0, writes still throw
        // NoModificationAllowedError).
        if (args.size() > 1 && names_a_pseudo_element(c, args[1])) {
            const std::string pseudo = resolvable_pseudo(c.to_string(args[1]));
            if (pseudo.empty()) { return computed_style_object(c, node_id{}); }
            return computed_style_object(c, id, atoms_->intern(pseudo));
        }
        return computed_style_object(c, id);
    });
}

} // namespace ctbrowser::shell
