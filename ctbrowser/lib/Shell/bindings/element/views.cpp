// dom_bindings - the views onto an element that are OBJECTS rather than
// values: `attributes`, `style`, `classList`, `blocking`, `dataset` and the tree accessors.

#include "internal.hpp"

#include <ctbrowser/dom/token_list.hpp>

namespace ctbrowser::shell {

using namespace detail;

namespace {

// `backgroundColor` -> `background-color`; the conversion lives with the property
// table, see declarations.cpp.
using style::css::css_name_of;

} // namespace

long long dom_bindings::size_attribute(const read_txn & txn, node_id id, std::string_view name,
                                       long long fallback) const {
    long long parsed = 0;
    const bool ok = parse_html_integer(txn.attribute_value(id, atoms_->intern(name)), parsed);
    return ok && parsed >= 0 && parsed <= 2147483647LL ? parsed : fallback;
}

void dom_bindings::install_element_views(context & cx, script::object_object & obj, node_id id) {
    // `<style>.sheet` and `<link>.sheet` - the LinkStyle mixin. Here rather than
    // in bindings/stylesheets.cpp for the same reason `style` is here: it is a
    // view onto ONE element and it has to be installed as its wrapper is made.
    install_sheet_property(cx, obj, id);

    // --- the box metrics: offsetLeft/Top/Width/Height, clientWidth/Height,
    // clientLeft/Top, scrollWidth/Height.
    //
    // ACCESSORS THAT FLUSH LAYOUT, not numbers copied in at refresh. Reading
    // `offsetWidth` is how a page - Bootstrap's `reflow(el)`, every WPT file
    // that sizes a box with a style and measures it in the same script - asks
    // for the layout AS OF NOW; a copy taken before the first frame said 0 and
    // a copy taken at the last refresh said whatever the previous statement
    // left. `flush_layout` runs only what is stale (set_layout_hook).
    //
    // THE ROOT'S CLIENT RECTANGLE IS THE VIEWPORT, and its two axes come from
    // different places on purpose: the width is the root box's own (the layout
    // viewport, 15px narrower than the window when a scrollbar appears - which
    // is what Bootstrap's `.container` centred itself in), the height the
    // window's, because `documentElement.clientHeight` means "how tall is the
    // window" to p5's windowHeight. Before the first layout the root has no
    // box and the window stands in FOR THE ROOT ONLY: an ordinary element with
    // no box has a client width of zero, and handing it the viewport told
    // Babylon its canvas was window-sized before layout had sized it, which
    // failed WebGL setup outright. The body is an ordinary element here, as in
    // Chrome. clientLeft/Top are 0: borders are not in the box arithmetic.
    {
        enum class metric : std::uint8_t {
            offset_left,
            offset_top,
            offset_width,
            offset_height,
            client_width,
            client_height,
            client_left,
            client_top,
            scroll_width,
            scroll_height
        };
        constexpr std::pair<const char *, metric> metrics[] = {
            {"offsetLeft", metric::offset_left},   {"offsetTop", metric::offset_top},
            {"offsetWidth", metric::offset_width}, {"offsetHeight", metric::offset_height},
            {"clientWidth", metric::client_width}, {"clientHeight", metric::client_height},
            {"clientLeft", metric::client_left},   {"clientTop", metric::client_top},
            {"scrollWidth", metric::scroll_width}, {"scrollHeight", metric::scroll_height},
        };
        for (const auto & [name, which] : metrics) {
            auto * getter = cx.allocate<script::native_object>(
                name, [this, id, which](context &, std::span<value>) {
                    flush_layout();
                    const rect box = box_of(id);
                    const bool is_root = [&] {
                        const auto txn = doc_->read();
                        return atoms_->text(txn.tag(id).value_or(atom{})) == "html";
                    }();
                    double v = 0;
                    switch (which) {
                    case metric::offset_left: v = box.x; break;
                    case metric::offset_top: v = box.y; break;
                    case metric::offset_width:
                    case metric::scroll_width: v = box.width; break;
                    case metric::offset_height:
                    case metric::scroll_height: v = box.height; break;
                    case metric::client_width:
                        v = is_root && box.width <= 0 ? viewport_width_ : box.width;
                        break;
                    case metric::client_height: v = is_root ? viewport_height_ : box.height; break;
                    case metric::client_left:
                    case metric::client_top: v = 0; break;
                    }
                    return value::number(v);
                });
            obj.define_accessor(name, value::object(getter), value::undefined());
        }
    }

    // --- element.attributes
    //
    // THE MAP IS BUILT ONCE and refilled by the accessor, so it keeps its
    // identity across reads while its contents are read out of the document
    // every time. See refresh_attribute_map for why it is an array-like object
    // and not a proxy.
    auto * map = cx.allocate<script::object_object>();
    // WHOSE MAP IT IS, for the methods on NamedNodeMap.prototype - see
    // install_named_node_map. A symbol key, which getOwnPropertyNames does not
    // report: attributes.html reads the map's own names and expects the
    // indices and the exposed qualified names, nothing else.
    map->define(named_node_map_owner_key, value::object(&obj), script::attr_none);
    {
        // THE MAP IS ROOTED THROUGH THE GETTER. A C++ lambda's captures are
        // invisible to a precise collector, so the raw pointer this closes over
        // would not keep the object alive - `retained` is the channel that
        // does, and the getter itself is reachable from the wrapper's accessor
        // table. See native_object::retained.
        // HANDED OUT BEHIND A TRAP-LESS PROXY. `length`, `item` and the rest
        // are on NamedNodeMap.prototype, and the engine's array-like
        // iteration (`for (a of el.attributes)`, p5's XML module) reads
        // `length` as an OWN property of a plain object but through the
        // prototype chain of a proxy - so the proxy is what makes both the
        // WebIDL property model and the iteration true at once.
        const value map_value = value::object(map);
        const value handed =
            value::object(cx.allocate<script::proxy_object>(map_value, cx.make_object()));
        auto * getter = cx.allocate<script::native_object>(
            "attributes", [this, map, id, handed](context & c, std::span<value>) {
                refresh_attribute_map(c, *map, id);
                return handed;
            });
        getter->retained.push_back(map_value);
        getter->retained.push_back(handed);
        obj.define_accessor("attributes", value::object(getter), value::undefined());
    }

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
    // `style` IS READONLY, AND A WRITE TO IT FORWARDS.
    //
    // CSSOM declares it `[PutForwards=cssText] readonly attribute
    // CSSStyleDeclaration style`, and it was a plain writable data property -
    // so `el.style = "color: red"` REPLACED the declaration object with the
    // string, and every `el.style.color = v` after it wrote a property onto a
    // primitive and vanished. `css/support/numeric-testcommon.js` opens every
    // one of its cases with `testEl.style = ""`, so nineteen files in
    // `css/css-values` failed every subtest they had for this one line.
    //
    // The same shape bindings/stylesheets.cpp gives `rule.style`, and for the
    // same two reasons: the object is [SameObject], and the assignment has a
    // defined meaning that is not "replace me".
    {
        auto * reader = cx.allocate<script::native_object>(
            "style", [style_view](context &, std::span<value>) { return style_view; });
        // The declaration proxy is reachable only from that lambda, and a
        // capture is not a GC edge - see the note on the dataset map.
        reader->retained.push_back(style_view);
        auto * writer = cx.allocate<script::native_object>(
            "style", [style_view](context & c, std::span<value> a) {
                c.store_property(style_view, "cssText", a.empty() ? c.string("") : a[0]);
                return value::undefined();
            });
        writer->retained.push_back(style_view);
        obj.define_accessor("style", value::object(reader), value::object(writer));
    }

    // --- the reflected attributes
    //
    // NOT HERE ANY MORE, and that is the point. `id`, `className`, `href`,
    // `download`, `target`, `rel`, `alt`, `title`, `name`, `placeholder`,
    // `type` and `htmlFor` used to be twelve accessors installed on EVERY
    // wrapper, which was wrong in both directions: `div.href` existed and
    // `input.maxLength` did not, and none of the twelve knew its own type - so
    // `details.open` was a string and `td.colSpan` was nothing at all.
    //
    // They live on the INTERFACE PROTOTYPES now, out of one table, which is
    // what the specification means by reflection being defined per interface.
    // See install_dom_interfaces at the bottom of this file. What is still
    // installed per element below it is the handful that CANNOT be a table row:
    // a control's `value` and `checked`, which track what the user typed rather
    // than an attribute, an <img>'s `src`, which has to start a load, and a
    // <canvas>'s `width` and `height`, which resize a drawing buffer.

    // `innerHTML` and `textContent` are ACCESSORS over the tree, not properties
    // on the wrapper. As properties, assigning markup stored a string, built no
    // nodes, rendered nothing and reported nothing - and reading one back gave
    // whatever the page last wrote rather than what the DOM actually holds.
    define_getter(
        cx, obj, "innerHTML",
        [this, id](context & c, std::span<value>) { return c.string(inner_html(id)); },
        [this, id](context & c, std::span<value> a) {
            set_inner_html(id, arg_string(c, a, 0));
            return value::undefined();
        });
    define_getter(
        cx, obj, "outerHTML",
        [this, id](context & c, std::span<value>) { return c.string(outer_html(id)); },
        [this, id](context & c, std::span<value> a) {
            set_outer_html(c, id, arg_string(c, a, 0));
            return value::undefined();
        });
    // `data` AND `nodeValue` - the text a Text or Comment node holds, which is
    // the one thing those two nodes are FOR. `childNodes` has handed them out
    // all along and there was no way to read what was in one: `.data` was
    // undefined, `.nodeValue` was undefined, and the only spelling that worked
    // was textContent, which is the same answer by accident and a different
    // question. Both are accessors, both write through, and `nodeValue` on an
    // element is null - which is what the DOM says and is not the same as
    // absent. `data` is a CharacterData member and an ELEMENT does not get
    // one: as an own accessor it shadowed the reflected `object.data`.
    const bool character_data = [&] {
        const auto kind = doc_->read().kind(id).value_or(node_kind::element);
        return kind == node_kind::text || kind == node_kind::comment;
    }();
    for (const char * spelling : {"data", "nodeValue"}) {
        if (!character_data && spelling[0] == 'd') { continue; }
        define_getter(
            cx, obj, spelling,
            [this, id](context & c, std::span<value>) {
                const auto txn = doc_->read();
                const node_kind kind = txn.kind(id).value_or(node_kind::element);
                if (kind != node_kind::text && kind != node_kind::comment) { return value::null(); }
                return c.string(std::string{txn.text(id)});
            },
            [this, id](context & c, std::span<value> a) {
                const auto kind = doc_->read().kind(id).value_or(node_kind::element);
                if (kind == node_kind::text || kind == node_kind::comment) {
                    // NULL IS THE EMPTY STRING, not "null". `data` is
                    // [LegacyNullToEmptyString] and `nodeValue` is a nullable
                    // DOMString whose null means "no value"; both land on "",
                    // and ToString would have written the four letters instead.
                    // `CharacterData-data.html` asserts `.data = null` leaves a
                    // node of length 0 - and the very next case asserts
                    // `.data = undefined` writes "undefined", so this is a test
                    // for null ALONE and not for nullish.
                    const value given = arg(a, 0);
                    (void)doc_->set_text(id, given.is_null() ? std::string{} : arg_string(c, a, 0));
                    mutated();
                }
                return value::undefined();
            });
    }
    // NULL ON A DOCTYPE, both ways - DOM 4.4's table, and the four subtests
    // of `Node-textContent.html` that a doctype gets.
    const bool doctype =
        doc_->read().kind(id).value_or(node_kind::element) == node_kind::document_type;
    define_getter(
        cx, obj, "textContent",
        [this, id, doctype](context & c, std::span<value>) {
            return doctype ? value::null() : c.string(text_content(id));
        },
        [this, id, doctype](context & c, std::span<value> a) {
            if (doctype) { return value::undefined(); }
            // Text, never markup: that is the whole point of the property, and
            // the reason a page reaches for it instead of innerHTML. A nullish
            // value is the empty string (DOM 4.4: `[LegacyNullToEmptyString]`
            // on textContent's null), not the four letters.
            const value given = arg(a, 0);
            set_text(id, given.is_nullish() ? std::string{} : c.to_string(given));
            return value::undefined();
        });

    // `value` and `checked` ARE ACCESSORS, on a control, so a page that creates
    // a control and reads it back in the same statement -
    // `createInput('hello').value()`, which is p5's own DOM library - reads the
    // value that exists now. A data property would shadow the accessor.
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(id).value_or(atom{}));
        const std::string_view type = txn.attribute_value(id, atoms_->intern("type"));
        // NOT A <button>: its `value` is a plain reflection of the attribute
        // (HTMLButtonElement, a row in the table), not a control's state, and
        // an own accessor here would shadow the row on the prototype. EVERY
        // <input> though, whatever its type today: `input.type` changes and
        // the store follows the type state (HTML 4.10.5.1's value modes), so
        // the accessor cannot be decided by the type the wrapper was made at.
        if (tag == "input" || control_kind_of(tag, type) != control_kind::none) {
            obj.define_accessor(
                "value",
                value::object(cx.allocate<script::native_object>(
                    "value",
                    [this, id](context & c, std::span<value>) {
                        const auto read = doc_->read();
                        return c.string(forms_->state_of(read, *atoms_, id).value);
                    })),
                value::object(cx.allocate<script::native_object>(
                    "value", [this, id](context & c, std::span<value> a) {
                        // `[LegacyNullToEmptyString]`: null is "", not the word.
                        const value given = arg(a, 0);
                        std::string text = given.is_null() ? std::string{} : c.to_string(given);
                        std::string mode;
                        {
                            const auto read = doc_->read();
                            mode = input_types::value_mode_of(
                                forms_->state_of(read, *atoms_, id).type);
                        }
                        if (mode == "filename") {
                            // HTML 4.10.5.3: only the empty string may be
                            // assigned, and it empties the selected files.
                            if (!text.empty()) {
                                throw_dom_exception(c, "InvalidStateError",
                                                    "the value of a file input can only "
                                                    "be set to the empty string");
                            }
                            return value::undefined();
                        }
                        if (mode == "default" || mode == "default/on") {
                            // The default modes WRITE THE CONTENT ATTRIBUTE.
                            (void)doc_->set_attribute(id, atoms_->intern("value"), text);
                            mutated();
                            return value::undefined();
                        }
                        const auto read = doc_->read();
                        // An assignment DIRTIES the control, so the `value`
                        // attribute stops being the answer - otherwise setting
                        // it to "" would be undone by the next read - and the
                        // type's sanitization runs over what was assigned.
                        forms_->assign_value(read, *atoms_, id, std::move(text));
                        // The browser has to learn a control changed, or the
                        // paint is stale until something else marks it.
                        wrote_to_control_ = true;
                        mutated();
                        return value::undefined();
                    })));
            obj.define_accessor("checked",
                                value::object(cx.allocate<script::native_object>(
                                    "checked",
                                    [this, id](context &, std::span<value>) {
                                        const auto read = doc_->read();
                                        return value::boolean(
                                            forms_->state_of(read, *atoms_, id).checked);
                                    })),
                                value::object(cx.allocate<script::native_object>(
                                    "checked", [this, id](context &, std::span<value> a) {
                                        const auto read = doc_->read();
                                        forms_->state_of(read, *atoms_, id).checked =
                                            !a.empty() && context::truthy(a[0]);
                                        wrote_to_control_ = true;
                                        mutated();
                                        return value::undefined();
                                    })));
        }
    }

    // `parentNode` and `children` are ACCESSORS because the tree moves. A
    // wrapper built when an element was detached and refreshed later would hand
    // back the parent it had at wrapping time, which for an element p5 creates
    // and then appends is null forever.
    // `dom_parent`, not `parent()`: the document element's parent is the
    // Document, which `wrap` hands back as the page's own `document`.
    define_getter(cx, obj, "parentNode", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        return wrap(c, dom_parent(txn, id));
    });
    // `parentElement` IS NOT `parentNode`. It is null when the parent is not an
    // element, which is exactly the case a tree-walking page tests to know it
    // has reached the top: `<html>`'s parent is the DOCUMENT, and answering
    // with it made the walk run one level past the root.
    define_getter(cx, obj, "parentElement", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        const node_id parent = txn.parent(id);
        if (!parent || txn.kind(parent).value_or(node_kind::element) != node_kind::element) {
            return value::null();
        }
        return wrap(c, parent);
    });
    // `element.shadowRoot` - THE ROOT, OR NULL, AND THE MODE DECIDES WHICH.
    // A closed root is not hidden from the engine, only from the page: it is
    // still in `shadow_roots_`, `getRootNode()` on a node inside it still
    // answers with it, and only this one accessor refuses to hand it over.
    // That is the whole of what `mode: "closed"` means.
    define_getter(cx, obj, "shadowRoot", [this, id](context & c, std::span<value>) {
        const node_id root = shadow_root_of(id);
        const shadow_tree * tree = shadow_tree_of(root);
        if (tree == nullptr || !tree->open) { return value::null(); }
        return wrap(c, root);
    });
    // `isConnected` - "shadow-including root is a document", DOM 4.4, and the
    // reason it is here rather than a data property is that it is exactly the
    // question `getRootNode({composed: true})` answers. A node inside a shadow
    // tree whose host is in the document IS connected, which is what
    // `Node-isConnected-shadow-dom.html` is a file about.
    define_getter(cx, obj, "isConnected", [this, id](context & c, std::span<value>) {
        (void)c;
        const auto txn = doc_->read();
        const node_id top = root_of_tree(txn, id, true);
        return value::boolean(is_document_root(txn, top));
    });
    // `baseURI` is the node document's base URL, DOM 4.4 - the document's
    // address here, there being no <base>; the same string `document.baseURI`
    // answers, connected or not. Node-baseURI.html compares the two.
    define_getter(cx, obj, "baseURI", [this](context & c, std::span<value>) {
        return c.string(secondary_ ? std::string{"about:blank"} : location_href_);
    });
    // --- ParentNode and NonDocumentTypeChildNode -----------------------------
    //
    // THE ELEMENT-ONLY HALF OF THE TREE, which this wrapper had none of. Every
    // one of these is a one-assertion test file in `dom/nodes`, and there are
    // eight of them: Element-firstElementChild, -lastElementChild,
    // -nextElementSibling, -previousElementSibling, -childElementCount and
    // three -childElementCount-dynamic-* variants, each reporting `undefined`
    // where a node or a count belongs.
    //
    // ACCESSORS, like `parentNode` above and for the same reason: the three
    // dynamic tests add and remove children and read the count again, so a
    // value captured at wrapping time is wrong by construction.
    const auto element_children = [](const read_txn & txn, node_id parent) {
        std::vector<node_id> out;
        for (const node_id child : txn.children(parent)) {
            if (txn.kind(child).value_or(node_kind::text) == node_kind::element) {
                out.push_back(child);
            }
        }
        return out;
    };
    define_getter(cx, obj, "firstElementChild",
                  [this, id, element_children](context & c, std::span<value>) {
                      const auto txn = doc_->read();
                      const std::vector<node_id> kids = element_children(txn, id);
                      return kids.empty() ? value::null() : wrap(c, kids.front());
                  });
    define_getter(cx, obj, "lastElementChild",
                  [this, id, element_children](context & c, std::span<value>) {
                      const auto txn = doc_->read();
                      const std::vector<node_id> kids = element_children(txn, id);
                      return kids.empty() ? value::null() : wrap(c, kids.back());
                  });
    define_getter(cx, obj, "childElementCount",
                  [this, id, element_children](context & c, std::span<value>) {
                      (void)c;
                      const auto txn = doc_->read();
                      return value::number(static_cast<double>(element_children(txn, id).size()));
                  });
    // A SIBLING WALK NEEDS THE PARENT, because the tree is stored as a child
    // list rather than as sibling links: the element's position among its
    // parent's children is the only place the answer lives. A node with no
    // parent has no siblings, which is null rather than an empty walk.
    const auto sibling = [this, element_children](context & c, node_id self, bool forward,
                                                  bool elements_only) {
        const auto txn = doc_->read();
        const node_id parent = dom_parent(txn, self);
        if (!parent) { return value::null(); }
        const std::vector<node_id> kids =
            elements_only
                ? element_children(txn, parent)
                : std::vector<node_id>{txn.children(parent).begin(), txn.children(parent).end()};
        for (std::size_t i = 0; i < kids.size(); ++i) {
            if (kids[i] != self) { continue; }
            if (forward) { return i + 1 < kids.size() ? wrap(c, kids[i + 1]) : value::null(); }
            return i > 0 ? wrap(c, kids[i - 1]) : value::null();
        }
        // NOT AMONG ITS PARENT'S ELEMENT CHILDREN: a text node asked for its
        // previousElementSibling. Walk the full child list to find where it
        // sits and then scan outward for an element.
        if (!elements_only) { return value::null(); }
        const std::span<const node_id> all = txn.children(parent);
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (all[i] != self) { continue; }
            for (std::size_t step = 1; step <= all.size(); ++step) {
                const std::size_t at = forward ? i + step : i - step;
                if (forward ? at >= all.size() : step > i) { break; }
                if (txn.kind(all[at]).value_or(node_kind::text) == node_kind::element) {
                    return wrap(c, all[at]);
                }
            }
            return value::null();
        }
        return value::null();
    };
    define_getter(cx, obj, "firstChild", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        const std::span<const node_id> kids = txn.children(id);
        return kids.empty() ? value::null() : wrap(c, kids.front());
    });
    define_getter(cx, obj, "lastChild", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        const std::span<const node_id> kids = txn.children(id);
        return kids.empty() ? value::null() : wrap(c, kids.back());
    });
    define_getter(cx, obj, "nextSibling", [id, sibling](context & c, std::span<value>) {
        return sibling(c, id, true, false);
    });
    define_getter(cx, obj, "previousSibling", [id, sibling](context & c, std::span<value>) {
        return sibling(c, id, false, false);
    });
    define_getter(cx, obj, "nextElementSibling", [id, sibling](context & c, std::span<value>) {
        return sibling(c, id, true, true);
    });
    define_getter(cx, obj, "previousElementSibling", [id, sibling](context & c, std::span<value>) {
        return sibling(c, id, false, true);
    });
    // `childNodes` is EVERY child, text nodes included; `children` is the
    // elements only. Both exist because they answer different questions, and a
    // page that wants the text nodes has no other way to reach them.
    // A LIVE NodeList, and THE SAME ONE on every read - `el.childNodes ===
    // el.childNodes` is Node-childNodes.html's first assertion. It is kept on
    // the wrapper under a symbol key, which is what roots it and what keeps it
    // out of `for...in` and getOwnPropertyNames.
    define_getter(cx, obj, "childNodes", [this, id, self = &obj](context & c, std::span<value>) {
        constexpr std::string_view key = "@@sym:ctbrowser:childNodes";
        if (const value * held = self->find(key); held != nullptr) { return *held; }
        const value list = make_live_collection(
            c,
            [this, id] {
                const auto txn = doc_->read();
                const std::span<const node_id> kids = txn.children(id);
                return std::vector<node_id>{kids.begin(), kids.end()};
            },
            "NodeList");
        self->define(key, list, script::attr_none);
        return list;
    });
    // AN HTMLCollection, LIVE - not an Array. `children` is the one of these
    // navigations the DOM gives an interface to, and `ParentNode-children.html`
    // checks liveness by appending and then asks what the thing IS.
    define_getter(cx, obj, "children", [this, id](context & c, std::span<value>) {
        return make_live_collection(c, [this, id] {
            const auto txn = doc_->read();
            std::vector<node_id> found;
            for (const node_id child : txn.children(id)) {
                if (txn.tag(child).has_value()) { found.push_back(child); }
            }
            return found;
        });
    });

    // `width` and `height` are numbers, and on a <canvas> they are the size of
    // its PIXEL BUFFER rather than of its laid-out box - `canvas.width / 2` is
    // the first line of most canvas pages. Assigning one resizes the surface,
    // which is what the spec means by a canvas being reset by the assignment.
    //
    // An `unsigned long` reflection, HTML 2.6.9: the rules for parsing
    // non-negative integers on the way out, and on the way in ToUint32 with
    // anything past 2^31-1 writing the default - which is why `canvas.width =
    // 2147483648` reads back as 300.
    const auto reflect_size = [&](std::string property, long long fallback) {
        const auto read = [this, id](std::string_view name, long long missing) {
            return size_attribute(doc_->read(), id, name, missing);
        };
        obj.define_accessor(
            property,
            value::object(cx.allocate<script::native_object>(
                property,
                [property, fallback, read](context &, std::span<value>) {
                    return value::number(static_cast<double>(read(property, fallback)));
                })),
            value::object(cx.allocate<script::native_object>(
                property, [this, id, property, fallback, read](context &, std::span<value> a) {
                    long long want = to_uint32(arg_number(a, 0));
                    if (want > 2147483647LL) { want = fallback; }
                    (void)doc_->set_attribute(id, atoms_->intern(property), std::to_string(want));
                    // The SURFACE follows, or the canvas keeps drawing into a
                    // buffer of the size it was created at and everything past
                    // that edge is silently discarded.
                    if (canvases_ != nullptr) {
                        const int w = static_cast<int>(read("width", 300));
                        const int h = static_cast<int>(read("height", 150));
                        canvases_->resize(id, w, h);
                        // And the WebGL context over the same canvas, which held
                        // a pointer INTO the buffer that resize just replaced.
                        resize_webgl_context(id, w, h);
                    }
                    mutated();
                    return value::undefined();
                })));
    };
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(id).value_or(atom{}));
        if (tag == "canvas") {
            // The HTML defaults, which a page that omits the attributes relies on.
            reflect_size("width", 300);
            reflect_size("height", 150);
        } else if (tag == "img") {
            install_image_views(cx, obj, id);
        } else if (tag == "input" && txn.attribute_value(id, atoms_->intern("type")) == "file") {
            // AN EMPTY FileList, and it has to EXIST. There is no user here to
            // choose a file, so this is always empty - but `event.target.files`
            // is what every change handler iterates, and undefined there is a
            // TypeError on the first line of the handler rather than a quiet
            // nothing-was-chosen.
            const value files = cx.make_array();
            static_cast<script::array_object *>(files.as_heap())->items.clear();
            // Readonly, as every [SameObject] attribute here is: a page that
            // assigns to `input.files` must not be able to put a string where
            // the next `for (const f of input.files)` looks.
            obj.define("files", files, script::attr_enumerable | script::attr_configurable);
        } else if ((txn.has_attribute(id, atoms_->intern("width")) ||
                    txn.has_attribute(id, atoms_->intern("height"))) &&
                   !interface_reflects_size(tag)) {
            // AND NOT WHERE THE TABLE HAS A ROW. `<td width=50>`, `<marquee
            // width=50>` and `<iframe width=50>` reflect a DOMString - "50",
            // not 50 - and `<input width=50>` an unsigned long with HTML's
            // integer rules rather than the digit loop above. This branch is
            // what is left: an element whose interface says nothing about
            // width, `<div width=50>` and `<svg width=50>` among them, where a
            // number is better than nothing at all.
            reflect_size("width", 0);
            reflect_size("height", 0);
        }
    }

    // --- element.classList, and every other DOMTokenList over an attribute
    //
    // ONE BUILDER, because a DOMTokenList is defined over "an associated
    // attribute" and nothing about it is specific to `class`: `blocking` below
    // is the same object over another attribute with a set of supported
    // tokens. Every operation reads the attribute, edits the token list and
    // writes it back, so nothing is cached and a token added by the parser, by
    // setAttribute or by the style engine is seen by all of them.
    const auto make_token_list = [this, &cx, id](std::string_view attribute,
                                                 std::string_view supported) {
        auto * list = cx.allocate<script::object_object>();
        if (const value proto = interface_prototype("DOMTokenList"); proto.is_object()) {
            list->prototype = proto;
            // `iterable<DOMString>`: keys/values/entries/forEach and
            // `@@iterator`, live over `length` and the indexed getter
            // (DOMTokenList-Iterable.html, -iteration.html). Once.
            install_iterable_declaration(cx, *static_cast<script::object_object *>(proto.as_heap()),
                                         false);
        }
        const std::string attribute_name{attribute};
        const auto attribute_now = [this, id, attribute_name] {
            const auto txn = doc_->read();
            return std::string{txn.attribute_value(id, atoms_->intern(attribute_name))};
        };
        // AN ORDERED SET, DOM 7.1: `class="a a b"` is the two tokens `a` and
        // `b`, so `length` is 2 and `item(1)` is `b` - and it is what every
        // operation edits and then serialises back, which is why `add("a")` on
        // that attribute writes "a b".
        const auto tokens_now = [attribute_now] { return parse_ordered_tokens(attribute_now()); };
        // THE UPDATE STEPS: nothing is written when there is no attribute and
        // nothing to put in one, otherwise the set, space-joined.
        const auto update = [this, id, attribute_name](const std::vector<std::string> & tokens) {
            const auto result = update_tokens(*doc_, id, atoms_->intern(attribute_name), tokens);
            if (!result || *result) { mutated(); }
        };
        // EVERY ARGUMENT IS CHECKED BEFORE ANYTHING CHANGES: "" is a
        // SyntaxError, a token with whitespace in it an InvalidCharacterError,
        // and `add("a", "")` must leave the attribute alone.
        //
        // `add` and `remove` check each token in turn; `replace` checks BOTH
        // for emptiness before either for whitespace (DOM 7.1, steps 1-2),
        // so `replace(" ", "")` is a SyntaxError - hence `all_empty_first`.
        const auto report_token_error = [this](context & c, const std::string & token,
                                               token_error error) {
            if (error == token_error::empty) {
                throw_dom_exception(c, "SyntaxError",
                                    "DOMTokenList: the empty string is not a token");
            } else {
                throw_dom_exception(c, "InvalidCharacterError",
                                    "DOMTokenList: '" + token + "' contains whitespace");
            }
        };
        const auto valid_tokens = [report_token_error](context & c, std::span<value> args,
                                                       std::vector<std::string> & out,
                                                       bool all_empty_first = false) {
            for (const value & v : args) { out.push_back(c.to_string(v)); }
            for (const std::string & token : out) {
                const auto error = validate_token(token);
                if (error && (*error == token_error::empty || !all_empty_first)) {
                    report_token_error(c, token, *error);
                    return false;
                }
            }
            for (const std::string & token : out) {
                if (const auto error = validate_token(token)) {
                    report_token_error(c, token, *error);
                    return false;
                }
            }
            return true;
        };
        const auto has = [](const std::vector<std::string> & tokens, const std::string & token) {
            return std::find(tokens.begin(), tokens.end(), token) != tokens.end();
        };
        set_method(cx, *list, "add",
                   [tokens_now, update, valid_tokens, has](context & c, std::span<value> args) {
                       std::vector<std::string> given;
                       if (!valid_tokens(c, args, given)) { return value::undefined(); }
                       std::vector<std::string> tokens = tokens_now();
                       for (const std::string & token : given) {
                           if (!has(tokens, token)) { tokens.push_back(token); }
                       }
                       update(tokens);
                       return value::undefined();
                   });
        set_method(cx, *list, "remove",
                   [tokens_now, update, valid_tokens](context & c, std::span<value> args) {
                       std::vector<std::string> given;
                       if (!valid_tokens(c, args, given)) { return value::undefined(); }
                       std::vector<std::string> tokens = tokens_now();
                       for (const std::string & token : given) { std::erase(tokens, token); }
                       update(tokens);
                       return value::undefined();
                   });
        set_method(cx, *list, "contains", [tokens_now, has](context & c, std::span<value> args) {
            return value::boolean(has(tokens_now(), arg_string(c, args, 0)));
        });
        // `toggle(token, force)`, DOM 7.1 - and a no-op runs NO update steps:
        // `toggle("c", false)` on `class="a a"` leaves the duplicate in place.
        set_method(
            cx, *list, "toggle",
            [this, id, attribute_name, report_token_error](context & c, std::span<value> args) {
                const std::string token = args.empty() ? "undefined" : c.to_string(args.front());
                const bool forced = args.size() > 1 && !args[1].is_undefined();
                const auto result =
                    toggle_token(*doc_, id, atoms_->intern(attribute_name), token,
                                 forced ? std::optional{context::truthy(args[1])} : std::nullopt);
                if (!result) {
                    report_token_error(c, token, result.error());
                    return value::undefined();
                }
                if (!result->update || *result->update) { mutated(); }
                return value::boolean(result->present);
            });
        // `replace(token, newToken)`: "replace within an ordered set" - the
        // FIRST of either becomes the new token and every other instance of
        // either goes, so `class="a b c"` replacing c with a is "a b".
        set_method(cx, *list, "replace",
                   [tokens_now, update, valid_tokens, has](context & c, std::span<value> args) {
                       if (args.size() < 2) {
                           c.throw_error("TypeError", "replace: 2 arguments required");
                           return value::undefined();
                       }
                       std::vector<std::string> given;
                       if (!valid_tokens(c, args.subspan(0, 2), given, true)) {
                           return value::undefined();
                       }
                       std::vector<std::string> tokens = tokens_now();
                       if (!has(tokens, given[0])) { return value::boolean(false); }
                       std::vector<std::string> replaced;
                       bool done = false;
                       for (const std::string & token : tokens) {
                           if (token != given[0] && token != given[1]) {
                               replaced.push_back(token);
                           } else if (!done) {
                               replaced.push_back(given[1]);
                               done = true;
                           }
                       }
                       update(replaced);
                       return value::boolean(true);
                   });
        set_method(cx, *list, "item", [tokens_now](context & c, std::span<value> args) {
            const std::vector<std::string> tokens = tokens_now();
            const auto i = static_cast<std::ptrdiff_t>(
                context::to_number(args.empty() ? value::undefined() : args[0]));
            if (i < 0 || static_cast<std::size_t>(i) >= tokens.size()) { return value::null(); }
            return c.string(tokens[static_cast<std::size_t>(i)]);
        });
        // `supports(token)`, DOM 7.1: a TypeError when the attribute defines no
        // supported tokens at all - which is `class` - and otherwise an ASCII
        // case-insensitive membership test.
        const std::string supported_tokens{supported};
        set_method(cx, *list, "supports", [supported_tokens](context & c, std::span<value> args) {
            if (supported_tokens.empty()) {
                c.throw_error("TypeError", "DOMTokenList has no supported tokens");
                return value::undefined();
            }
            return value::boolean(
                lists_token(supported_tokens, ascii_lower_copy(arg_string(c, args, 0))));
        });
        // `value` IS the attribute, verbatim in both directions - it is what a
        // `PutForwards=value` assignment writes - and it is the stringifier.
        set_method(cx, *list, "toString", [attribute_now](context & c, std::span<value>) {
            return c.string(attribute_now());
        });
        const auto write_attribute = [this, id, attribute_name](std::string_view text) {
            (void)doc_->set_attribute(id, atoms_->intern(attribute_name), std::string{text});
            mutated();
        };
        list->define_accessor(
            "value",
            value::object(cx.allocate<script::native_object>(
                "value", [attribute_now](context & c,
                                         std::span<value>) { return c.string(attribute_now()); })),
            value::object(cx.allocate<script::native_object>(
                "value", [write_attribute](context & c, std::span<value> args) {
                    write_attribute(arg_string(c, args, 0));
                    return value::undefined();
                })));
        // An ACCESSOR, not a number: the count changes whenever the attribute
        // does, and a data property would report whatever it was when the
        // element was first wrapped.
        list->define_accessor("length",
                              value::object(cx.allocate<script::native_object>(
                                  "length",
                                  [tokens_now](context &, std::span<value>) {
                                      return value::number(
                                          static_cast<double>(tokens_now().size()));
                                  })),
                              value::undefined());
        // `classList[i]` - the indexed getter, which only a proxy can keep live.
        // The same shape as make_live_collection's, and only `get`: an index is
        // read-only and everything else falls through to the list itself.
        auto * handler = cx.allocate<script::object_object>();
        handler->set("get", value::object(cx.allocate<script::native_object>(
                                "get", [tokens_now](context & c, std::span<value> args) {
                                    if (args.size() < 2) { return value::undefined(); }
                                    const std::string key = c.to_string(args[1]);
                                    if (!key.empty() && key.size() < 10 &&
                                        key.find_first_not_of("0123456789") == std::string::npos &&
                                        (key == "0" || key[0] != '0')) {
                                        const std::vector<std::string> tokens = tokens_now();
                                        const std::size_t at = std::stoul(key);
                                        return at < tokens.size() ? c.string(tokens[at])
                                                                  : value::undefined();
                                    }
                                    return c.lookup_property(args[0], key);
                                })));
        return value::object(
            cx.allocate<script::proxy_object>(value::object(list), value::object(handler)));
    };
    // `[SameObject, PutForwards=value] readonly attribute DOMTokenList
    // classList`: ONE list, and a write to the property forwards to its
    // `value` - `el.classList = "a b"` sets the class attribute, which
    // `Element-classlist.html` assigns in its first case (a readonly data
    // property threw there from strict code) and then calls `add`, `contains`
    // and `item` on the list it still expects to find.
    {
        const value list = make_token_list("class", {});
        auto * reader = cx.allocate<script::native_object>(
            "classList", [list](context &, std::span<value>) { return list; });
        // A capture is not a GC edge - see the note on `attributes` above.
        reader->retained.push_back(list);
        auto * writer = cx.allocate<script::native_object>(
            "classList", [list](context & c, std::span<value> a) {
                c.store_property(list, "value", a.empty() ? c.string("") : a[0]);
                return value::undefined();
            });
        writer->retained.push_back(list);
        obj.define_accessor("classList", value::object(reader), value::object(writer),
                            script::attr_enumerable | script::attr_configurable);
    }

    // --- element.blocking, HTML 2.5.7 "blocking attributes"
    //
    // `[SameObject, PutForwards=value] readonly attribute DOMTokenList
    // blocking` on HTMLLinkElement, HTMLScriptElement and HTMLStyleElement -
    // the same list as above over `blocking`, whose one supported token is
    // `render`. An ACCESSOR rather than the readonly data property `classList`
    // is, because a write to it has a meaning: `el.blocking = 'render'`
    // forwards to `value` and sets the attribute, which is how
    // `html/dom/render-blocking` marks a script-inserted element.
    //
    // THE ATTRIBUTE ONLY. This engine loads a stylesheet and a classic script
    // synchronously from the asset registry while the page is being built, so
    // every sheet and script has applied before the first frame is laid out,
    // and there is nothing left for `render` to hold back - the ordering the
    // attribute asks for is the only one the engine has.
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(id).value_or(atom{}));
        if (txn.element_ns(id) == node_ns::html &&
            (tag == "link" || tag == "script" || tag == "style")) {
            const value list = make_token_list("blocking", "render");
            auto * reader = cx.allocate<script::native_object>(
                "blocking", [list](context &, std::span<value>) { return list; });
            // A capture is not a GC edge - see the note on `attributes` above.
            reader->retained.push_back(list);
            auto * writer = cx.allocate<script::native_object>(
                "blocking", [list](context & c, std::span<value> a) {
                    c.store_property(list, "value", a.empty() ? c.string("") : a[0]);
                    return value::undefined();
                });
            writer->retained.push_back(list);
            obj.define_accessor("blocking", value::object(reader), value::object(writer));
        }
    }

    // --- element.dataset
    //
    // A live DOMStringMap over this element's `data-*` attributes, and it did
    // not exist at all: `el.dataset.foo` was a TypeError on the first line of
    // every page that uses the ordinary way of hanging state off an element.
    //
    // A PROXY, because the set of properties IS the set of attributes and a
    // page may write a key this element has never carried. What that costs is
    // said here rather than left to be discovered: this VM implements the
    // `get`, `set` and `has` traps and no others, so
    //
    //   * `delete el.dataset.foo` is a silent no-op - `op::delete_prop` skips
    //     anything that is not exactly a plain object, and
    //     `dataset-delete.html` is what measures it.
    //
    // That is a deviation in `lib/Script` and that is where it is fixable. The
    // alternative shape - a plain object refilled on every read, as
    // `attributes` above is - trades it for a `set` that never reaches the
    // document at all, which is the worse half of the trade: a write that
    // silently does nothing is a wrong answer.
    //
    // THE OTHER TWO ARE FIXED HERE, both without a new trap. Enumeration walks
    // the proxy's TARGET, so the target is refilled with the element's data-*
    // names each time `dataset` is read - which is why it is an accessor rather
    // than a property. And `instanceof` follows a proxy to its target and walks
    // THAT object's prototype, so hanging DOMStringMap.prototype off the target
    // answers `el.dataset instanceof DOMStringMap` without the VM knowing what
    // a proxy's prototype would be.
    //
    // NOT ON EVERY ELEMENT. `dataset` belongs to HTMLElement, SVGElement and
    // MathMLElement, and `document.createElementNS("test", "test").dataset` is
    // `undefined` - which `dataset.html` asserts by name, and which is the only
    // reason this is conditional rather than unconditional.
    {
        const auto txn = doc_->read();
        const std::string ns = namespace_of(id);
        const bool wanted =
            txn.kind(id).value_or(node_kind::text) == node_kind::element &&
            (ns == xhtml_namespace || ns == svg_namespace || ns == mathml_namespace);
        if (wanted) { install_dataset(cx, obj, id); }
    }
}

} // namespace ctbrowser::shell
