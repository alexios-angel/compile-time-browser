#include "../computed_style/internal.hpp"
#include "internal.hpp"

#include "view_geometry.hpp"
#include <ctbrowser/dom/token_list.hpp>
#include <ctbrowser/layout/overflow.hpp>
#include <ctbrowser/shell/page/canvas.hpp>

namespace ctbrowser::shell {

using namespace detail;

std::string_view dom_bindings::cascade_value(node_id id, std::string_view property) const {
    if (styles_ == nullptr) { return {}; }
    const auto found = styles_->find(style::engine::key_of(id));
    if (found == styles_->end() || !found->second) { return {}; }
    return trim(found->second->get(atoms_->intern(property)), html_whitespace);
}

bool dom_bindings::potentially_scrollable(node_id body) const {
    if (locate(body).f == nullptr) { return false; }
    const auto scrolls = [this](node_id id, const char * property) {
        const std::string_view v = cascade_value(id, property);
        return !v.empty() && !ascii_iequals(v, "visible") && !ascii_iequals(v, "clip");
    };
    const node_id parent = doc_->read().parent(body);
    if (!parent) { return false; }
    for (const char * axis : {"overflow-x", "overflow-y"}) {
        if (scrolls(body, axis) && scrolls(parent, axis)) { return true; }
    }
    return false;
}

bool dom_bindings::is_viewport_element(node_id id, bool scrolling) {
    const bool quirks = doc_->quirks();
    const bool root = [&] {
        const auto txn = doc_->read();
        return id == txn.root();
    }();
    if (root) { return !quirks; }
    if (!quirks || id != body_element()) { return false; }
    return !scrolling || !potentially_scrollable(id);
}

node_id dom_bindings::offset_parent_of(node_id id) {
    const located at = locate(id);
    if (at.f == nullptr || at.f->box == nullptr) { return node_id{}; }
    const node_id body = body_element();
    node_id parent;
    {
        const auto txn = doc_->read();
        if (id == txn.root() || id == body) { return node_id{}; }
        parent = txn.parent(id);
    }
    const auto box_of_node = [this](node_id node) -> const layout::box_node * {
        const located found = locate(node);
        return found.f == nullptr ? nullptr : found.f->box;
    };
    // A transform establishes a containing block for fixed and absolute
    // descendants alike (CSS Transforms 1 §2).
    const auto anchors_fixed = [](const layout::box_node * b) {
        return b != nullptr && b->transformed;
    };
    const auto anchors_absolute = [](const layout::box_node * b) {
        return b != nullptr && (b->is_positioned() || b->transformed);
    };
    const bool fixed = at.f->box->position == layout::position_kind::fixed;
    const bool static_ = at.f->box->position == layout::position_kind::static_;
    if (fixed) {
        bool anchored = false;
        for (node_id up = parent; up; up = doc_->read().parent(up)) {
            if (anchors_fixed(box_of_node(up))) {
                anchored = true;
                break;
            }
        }
        if (!anchored) { return node_id{}; }
    }
    for (node_id up = parent; up; up = doc_->read().parent(up)) {
        const layout::box_node * b = box_of_node(up);
        if (fixed ? anchors_fixed(b) : anchors_absolute(b)) { return up; }
        if (fixed) { continue; }
        if (up == body) { return up; }
        if (static_) {
            const auto txn = doc_->read();
            const std::string_view tag = txn.local_name(up);
            if (txn.element_ns(up) == node_ns::html &&
                (tag == "td" || tag == "th" || tag == "table")) {
                return up;
            }
        }
    }
    return node_id{};
}

long long dom_bindings::size_attribute(const read_txn & txn, node_id id, std::string_view name,
                                       long long fallback) const {
    long long parsed = 0;
    const bool ok = parse_html_integer(txn.attribute_value(id, atoms_->intern(name)), parsed);
    return ok && parsed >= 0 && parsed <= 2147483647LL ? parsed : fallback;
}

// One element's `style` object - see the accessor on the prototype below.
value dom_bindings::make_style_view(context & cx, node_id id) {
    // --- element.style
    //
    // A PROXY, because a style object has no fixed set of properties: a page
    // may write any CSS property and the write has to reach the document. The
    // proxy's target holds the declarations and the `set` trap re-serialises it
    // into the element's `style` attribute - which the style engine already
    // parses, so there is no second representation to keep in step.
    //
    // BOTH traps canonicalise the name, because `backgroundColor` and
    // `background-color` are two spellings of ONE property. Storing them as
    // written put both in the attribute and made a read miss a write.
    auto * held = cx.allocate<script::object_object>();
    // AND IT IS RE-SEEDED, not seeded once. The store was filled from the
    // `style` attribute at wrapper construction and never again, so
    // `el.setAttribute("style", "color: red")` - which writes the attribute
    // directly and never touches this proxy - left `el.style.color` reading the
    // empty store. `css-style-attr-decl-block.html` names the defect outright
    // ("Changes to style attribute should reflect on CSS declaration block")
    // and `serialize-values.html` is 697 subtests of it: it does createElement,
    // setAttribute("style", …) and then reads the IDL attribute back.
    //
    // The last text SEEN rather than the document version, because a write
    // through this proxy sets the attribute itself and must not then re-seed
    // from what it just wrote - `seen` is updated to the serialisation instead,
    // so a write costs nothing and only a change from OUTSIDE re-reads.
    const auto seen = std::make_shared<std::string>();
    const auto reseed = [this, id, seen](context & c, script::object_object & store) {
        std::string now;
        {
            const auto txn = doc_->read();
            now = std::string{txn.attribute_value(id, atoms_->intern("style"))};
        }
        if (now == *seen) { return; }
        *seen = now;
        // The METHODS stay: `setProperty` and its four siblings live on this
        // same object, and erasing everything would take them with it.
        std::vector<std::string> declared;
        for (const auto & [key, v] : store.props) {
            if (is_declaration(v)) { declared.push_back(key); }
        }
        for (const std::string & key : declared) { store.erase(key); }
        seed_declarations(store, c, now);
    };
    // The write side of the same bookkeeping: after this proxy has written the
    // attribute, what is in it is what we put there.
    const auto wrote = [this, id, seen](context & c, script::object_object & store) {
        *seen = style_attribute(store, c);
        (void)doc_->set_attribute(id, atoms_->intern("style"), *seen);
    };
    reseed(cx, *held);
    const value target = value::object(held);
    auto * handler = cx.allocate<script::object_object>();
    // `length`, `cssText` and the indexed properties are COMPUTED here rather
    // than stored. Storing them would put `length: 5` in the element's style
    // attribute - the store IS the declaration list, and anything in it that is
    // not a declaration has to be filtered back out by every reader.
    set_method(cx, *handler, "get", [reseed](context & c, std::span<value> args) {
        if (args.size() < 2 || !args[0].is_object()) { return value::undefined(); }
        auto * store = static_cast<script::object_object *>(args[0].as_heap());
        reseed(c, *store);
        const std::string name = c.to_string(args[1]);
        if (name == "length") {
            double count = 0;
            for (const auto & [key, v] : store->props) {
                if (is_declaration(v)) { count += 1; }
            }
            return value::number(count);
        }
        if (name == "cssText") { return c.string(css_text_of(*store, c)); }
        // AN INDEX NAMES A PROPERTY, not a value: CSSOM §6.7.1 makes the
        // indexed properties of a CSSStyleDeclaration its property NAMES, in
        // declaration order, which is what `[...el.style]` and every
        // `for (const p of el.style)` in the corpus iterates.
        if (!name.empty() && name.find_first_not_of("0123456789") == std::string::npos) {
            // COUNTED DOWN rather than converted. `std::stoull` throws on an
            // index a page can write in one keystroke (`el.style[1e30]` arrives
            // here as twenty digits), and an uncaught std::out_of_range out of a
            // native is a terminate() rather than a TypeError.
            std::size_t want = 0;
            for (const char digit : name) {
                if (want > store->props.size()) { return value::undefined(); }
                want = want * 10 + static_cast<std::size_t>(digit - '0');
            }
            for (const auto & [key, v] : store->props) {
                if (!is_declaration(v)) { continue; }
                if (want-- == 0) { return c.string(key); }
            }
            return value::undefined();
        }
        // The raw name first: that is where setProperty and getPropertyValue
        // live, and canonicalising them turns them into `set-property`.
        if (const value * found = store->find(name)) {
            // A METHOD is handed back as it is; a DECLARATION loses its
            // priority, because `el.style.width` is a value and never
            // "100px !important".
            if (found->is_callable()) { return *found; }
            return c.string(std::string{declared_value(c.to_string(*found))});
        }
        const std::string css = css_name_of(name);
        // A SUPPORTED PROPERTY THAT IS NOT SET IS "", NOT undefined.
        // CSSOM 6.7.2 gives every property in the IDL a getter that
        // returns the empty string when the declaration block has none,
        // and `serialize-values.html` reads exactly that for the ones it
        // could not set. `undefined` is reserved for a name that is not a
        // property at all - `el.style.toString`, `el.style.constructor` -
        // because answering "" there would break every ordinary lookup.
        // A shorthand is read from its longhands (declarations.cpp).
        if (store->find(css) == nullptr && style::css::find_property(css) == nullptr) {
            // An ordinary property the page put there (the set trap below).
            if (const value * own = expandos_of(*store, c).find(name)) { return *own; }
            return value::undefined();
        }
        return c.string(read_declaration(*store, c, css));
    });
    set_method(cx, *handler, "set", [this, reseed, wrote](context & c, std::span<value> args) {
        if (args.size() < 3 || !args[0].is_object()) { return value::boolean(false); }
        auto * store = static_cast<script::object_object *>(args[0].as_heap());
        reseed(c, *store);
        const std::string name = c.to_string(args[1]);
        if (name == "cssText") {
            // A WHOLE-BLOCK REPLACEMENT, not a merge: `el.style.cssText = "…"`
            // drops every declaration the element had. Erasing in place would
            // leave the methods behind, which is exactly what has to survive.
            std::vector<std::string> declared;
            for (const auto & [key, v] : store->props) {
                if (is_declaration(v)) { declared.push_back(key); }
            }
            for (const std::string & key : declared) { store->erase(key); }
            seed_declarations(*store, c, c.to_string(args[2]));
        } else {
            // The IDL spelling, canonicalised, and the value through the
            // grammar. A NAME THAT IS NOT A PROPERTY has no IDL setter: the
            // write makes an ordinary property (expandos_of), never a
            // declaration - `style.unknown` and `style.COLOR` are the two
            // cssstyledeclaration-csstext.html reads back through cssText.
            // A refusal of the value is silent - CSSOM says an unparseable
            // value leaves the declaration alone, and a throw here would break
            // every page that sets a property this engine has not implemented
            // - and writes nothing, so no mutation record is queued for it.
            const std::string css = css_name_of(name);
            if (!css.starts_with("--") && store->find(css) == nullptr &&
                style::css::find_property(css) == nullptr) {
                expandos_of(*store, c).set(name, args[2]);
                return value::boolean(true);
            }
            if (!store_declaration(*store, c, css, c.to_string(args[2]), false)) {
                return value::boolean(true);
            }
        }
        wrote(c, *store);
        mutated();
        return value::boolean(true);
    });
    // `name in el.style`: every SUPPORTED property has an IDL attribute in
    // both spellings (CSSOM 6.7.2), set or not, beside the methods, `length`,
    // `cssText` and the indices - and nothing else does: a custom property
    // has no attribute, an unknown name only the expando a page put there.
    // CSS-supports-CSSStyleDeclaration.html holds `CSS.supports(p, "inherit")`
    // against `p in style` for 700 names.
    set_method(cx, *handler, "has", [reseed](context & c, std::span<value> args) {
        if (args.size() < 2 || !args[0].is_object()) { return value::boolean(false); }
        auto * store = static_cast<script::object_object *>(args[0].as_heap());
        reseed(c, *store);
        const std::string name = c.to_string(args[1]);
        if (name == "length" || name == "cssText" || store->find(name) != nullptr) {
            return value::boolean(true);
        }
        if (!name.empty() && name.find_first_not_of("0123456789") == std::string::npos) {
            std::size_t count = 0;
            for (const auto & [key, v] : store->props) {
                if (is_declaration(v)) { ++count; }
            }
            return value::boolean(name.size() < 10 && std::stoul(name) < count);
        }
        const std::string css = css_name_of(name);
        if (!css.starts_with("--") && style::css::find_property(css) != nullptr) {
            return value::boolean(true);
        }
        return value::boolean(
            expandos_of(*store, c).find(name) != nullptr ||
            (store->prototype.is_object() && c.has_property(store->prototype, name)));
    });
    const value style_view =
        value::object(cx.allocate<script::proxy_object>(target, value::object(handler)));

    // `setProperty` / `getPropertyValue` / `removeProperty` take the CSS
    // spelling rather than the IDL one, so they are the only way to reach a
    // custom property (`--x`) - which no identifier can name.
    // A NON-CUSTOM PROPERTY NAME IS LOWERCASED, a custom one is not: `--X` and
    // `--x` are two different properties and `COLOR` and `color` are one.
    const auto asked_name = [](context & c, std::span<value> args) {
        const std::string given = arg_string(c, args, 0);
        return given.starts_with("--") ? given : ascii_lower_copy(given);
    };
    set_method(cx, *held, "setProperty",
               [this, held, asked_name, reseed, wrote](context & c, std::span<value> args) {
                   // "If priority is not the empty string and is not an ASCII
                   // case-insensitive match for 'important', return." - CSSOM 6.7.2.
                   // The VALUE may not carry one; the third argument is the only way
                   // a page can ask for it.
                   // [LegacyNullToEmptyString], and optional: null and undefined are "".
                   const std::string priority = args.size() > 2 && !args[2].is_nullish()
                                                    ? c.to_string(args[2])
                                                    : std::string{};
                   if (!priority.empty() && !ascii_iequals(priority, "important")) {
                       return value::undefined();
                   }
                   reseed(c, *held);
                   // [LegacyNullToEmptyString]: null is "", and an undefined value is
                   // the string "undefined", which no grammar accepts.
                   const std::string text =
                       args.size() > 1 && !args[1].is_null() ? c.to_string(args[1]) : std::string{};
                   if (store_declaration(*held, c, asked_name(c, args), text, !priority.empty())) {
                       wrote(c, *held);
                       mutated();
                   }
                   return value::undefined();
               });
    // ...and it ANSWERS with the value it removed, which is what CSSOM says and
    // what a page toggling a property reads to put it back.
    set_method(cx, *held, "removeProperty",
               [this, held, asked_name, reseed, wrote](context & c, std::span<value> args) {
                   reseed(c, *held);
                   bool removed = false;
                   const std::string was =
                       remove_stored_declaration(*held, c, asked_name(c, args), removed);
                   if (removed) {
                       wrote(c, *held);
                       mutated();
                   }
                   return c.string(was);
               });
    set_method(cx, *held, "getPropertyValue",
               [held, asked_name, reseed](context & c, std::span<value> args) {
                   reseed(c, *held);
                   return c.string(read_declaration(*held, c, asked_name(c, args)));
               });
    set_method(cx, *held, "getPropertyPriority",
               [held, asked_name, reseed](context & c, std::span<value> args) {
                   reseed(c, *held);
                   return c.string(read_priority(*held, c, asked_name(c, args)));
               });
    set_method(cx, *held, "item", [held, reseed](context & c, std::span<value> args) {
        reseed(c, *held);
        double want = args.empty() ? 0 : context::to_number(args[0]);
        if (!(want >= 0)) { return c.string(""); }
        for (const auto & [key, v] : held->props) {
            if (!is_declaration(v)) { continue; }
            if (want < 1) { return c.string(key); }
            want -= 1;
        }
        return c.string("");
    });
    return style_view;
}

// `style` ON THE PROTOTYPE (CSSOM's ElementCSSInlineStyle, mixed into
// HTMLElement, SVGElement and MathMLElement), not an own property of every
// wrapper: `[PutForwards=cssText] readonly attribute CSSStyleDeclaration
// style` is an accessor a page finds on the prototype chain
// (inline-style-001.html asserts exactly that), and building the
// declaration proxy on first READ rather than on every wrap is what a page
// that wraps ten thousand elements and styles none of them wants. The
// object is [SameObject]: kept on the wrapper under a symbol key.
void dom_bindings::install_style_accessor(context & cx) {
    if (secondary_) { return; }
    const auto view_of = [this](context & c, value self) -> value {
        constexpr std::string_view key = "@@sym:ctbrowser:style";
        if (!self.is_object()) { return value::undefined(); }
        dom_bindings & owner = target_owner(self);
        const node_id id = owner.handle_of(self);
        if (!id) { return value::undefined(); }
        auto * wrapper = static_cast<script::object_object *>(self.as_heap());
        // The cached view is THIS document's: a node adopted into another
        // document keeps its wrapper, and a view built over the old document
        // would write the old node (style-attr-update-across-documents.html).
        constexpr std::string_view owner_key = "@@sym:ctbrowser:style-owner";
        const value * held = wrapper->find(key);
        const value * made_by = wrapper->find(owner_key);
        if (held != nullptr && made_by != nullptr && made_by->bits() == owner.document_.bits()) {
            return *held;
        }
        const value made = owner.make_style_view(c, id);
        wrapper->define(std::string{key}, made, script::attr_none);
        wrapper->define(std::string{owner_key}, owner.document_, script::attr_none);
        return made;
    };
    for (const char * interface : {"HTMLElement", "SVGElement", "MathMLElement"}) {
        const value proto = interface_prototype(interface);
        if (!proto.is_object()) { continue; }
        define_getter(
            cx, *static_cast<script::object_object *>(proto.as_heap()), "style",
            [view_of](context & c, std::span<value>) { return view_of(c, c.current_this()); },
            [view_of](context & c, std::span<value> a) {
                // `[PutForwards=cssText]`: `el.style = "color: red"` writes the
                // declaration's text, and never replaces the object.
                const value view = view_of(c, c.current_this());
                if (view.is_object_like()) {
                    c.store_property(view, "cssText", a.empty() ? c.string("") : a[0]);
                }
                return value::undefined();
            });
    }
}

} // namespace ctbrowser::shell
