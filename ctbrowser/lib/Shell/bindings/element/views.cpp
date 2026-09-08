// dom_bindings - the views onto an element that are OBJECTS rather than
// values: `attributes`, `style`, `classList`, `dataset` and the tree accessors.
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

namespace {

// `backgroundColor` -> `background-color`; the conversion lives with the property
// table, see declarations.cpp.
using style::css::css_name_of;

// The tokens of a `class` attribute. Whitespace-separated, and a class list
// operation is defined in terms of them rather than of the string.
std::vector<std::string> class_tokens(std::string_view text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t start = text.find_first_not_of(" \t\n\r\f", i);
        if (start == std::string_view::npos) { break; }
        const std::size_t end = text.find_first_of(" \t\n\r\f", start);
        out.emplace_back(text.substr(start, end == std::string_view::npos ? end : end - start));
        i = end == std::string_view::npos ? text.size() : end;
    }
    return out;
}

} // namespace

void dom_bindings::install_element_views(context & cx, script::object_object & obj, node_id id) {
    // `<style>.sheet` and `<link>.sheet` - the LinkStyle mixin. Here rather than
    // in bindings/stylesheets.cpp for the same reason `style` is here: it is a
    // view onto ONE element and it has to be installed as its wrapper is made.
    install_sheet_property(cx, obj, id);

    // --- element.attributes
    //
    // THE MAP IS BUILT ONCE and refilled by the accessor, so it keeps its
    // identity across reads while its contents are read out of the document
    // every time. See refresh_attribute_map for why it is an array-like object
    // and not a proxy.
    auto * map = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto map_method = [&](std::string name, script::native_fn fn) {
        map->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // ONE ATTRIBUTE, BY WHICHEVER OF THE TWO QUESTIONS WAS ASKED, copied out of
    // the read before anything that could open another one runs.
    const auto found_by_name = [this, id](std::string_view qualified) {
        const auto txn = doc_->read();
        const attribute * held = txn.find_attribute(id, attribute_key(txn, id, qualified));
        return held == nullptr ? std::optional<attribute>{} : std::optional<attribute>{*held};
    };
    const auto found_by_pair = [this, id](std::string_view ns, std::string_view local) {
        const auto txn = doc_->read();
        const attribute * held = txn.find_attribute_ns(id, ns, local);
        return held == nullptr ? std::optional<attribute>{} : std::optional<attribute>{*held};
    };
    map_method("item", [this, id](context & c, std::span<value> a) {
        std::vector<attribute> held;
        {
            const auto txn = doc_->read();
            const std::span<const attribute> current = txn.attributes(id);
            held.assign(current.begin(), current.end());
        }
        const double at = arg_number(a, 0);
        if (!(at >= 0) || at >= static_cast<double>(held.size())) { return value::null(); }
        return attribute_object(c, id, held[static_cast<std::size_t>(at)]);
    });
    map_method("getNamedItem", [this, id, found_by_name](context & c, std::span<value> a) {
        const std::optional<attribute> held = found_by_name(arg_string(c, a, 0));
        return held ? attribute_object(c, id, *held) : value::null();
    });
    map_method("getNamedItemNS", [this, id, found_by_pair](context & c, std::span<value> a) {
        const std::optional<attribute> held =
            found_by_pair(namespace_argument(c, a, 0), arg_string(c, a, 1));
        return held ? attribute_object(c, id, *held) : value::null();
    });
    // `setNamedItem` and `setNamedItemNS` ARE THE SAME OPERATION - DOM 4.9.2
    // defines both as "set an attribute", which is keyed on the (namespace,
    // local name) pair whichever spelling was used. What an Attr carries is
    // read off the OBJECT rather than off a C++ type, so an Attr from
    // `document.createAttribute` - which bindings/document.cpp makes and which
    // is not one of these - works here too.
    const auto set_named = [this, id, found_by_pair](context & c, std::span<value> a) {
        const value given = arg(a, 0);
        if (!given.is_object()) {
            c.throw_error("TypeError", "setNamedItem: the argument is not an Attr");
            return value::null();
        }
        const value ns_property = c.lookup_property(given, "namespaceURI");
        const std::string ns = ns_property.is_nullish() ? std::string{} : c.to_string(ns_property);
        const std::string qualified = c.to_string(c.lookup_property(given, "name"));
        const value text = c.lookup_property(given, "value");
        const split_name split = split_attribute_name(qualified);
        const std::string_view local = ns.empty() ? std::string_view{qualified} : split.local;
        // THE ONE IT REPLACES IS THE RETURN VALUE, and it has to be read before
        // the write: "return oldAttr" is how a page takes an attribute off one
        // element and puts it on another.
        const std::optional<attribute> replaced = found_by_pair(ns, local);
        (void)doc_->set_attribute_ns(id, atoms_->intern(ns), atoms_->intern(qualified),
                                     text.is_undefined() ? std::string{} : c.to_string(text));
        mutated();
        return replaced ? attribute_object(c, id, *replaced) : value::null();
    };
    map_method("setNamedItem", set_named);
    map_method("setNamedItemNS", set_named);
    // ...and removing one THROWS when there is nothing to remove, which is the
    // half that is easy to miss: "if attr is null, throw a NotFoundError".
    map_method("removeNamedItem", [this, id, found_by_name](context & c, std::span<value> a) {
        const std::string qualified = arg_string(c, a, 0);
        const std::optional<attribute> held = found_by_name(qualified);
        if (!held) {
            throw_dom_exception(c, "NotFoundError",
                                "removeNamedItem: no attribute called '" + qualified + "'");
            return value::null();
        }
        // DETACHED, and made AFTER the removal so it cannot be read live: the
        // Attr this hands back keeps the value it had, and no element.
        (void)doc_->remove_attribute(id, held->name);
        mutated();
        return attribute_object(c, node_id{}, *held);
    });
    map_method("removeNamedItemNS", [this, id, found_by_pair](context & c, std::span<value> a) {
        const std::string ns = namespace_argument(c, a, 0);
        const std::string local = arg_string(c, a, 1);
        const std::optional<attribute> held = found_by_pair(ns, local);
        if (!held) {
            throw_dom_exception(c, "NotFoundError",
                                "removeNamedItemNS: no attribute called '" + local + "'");
            return value::null();
        }
        (void)doc_->remove_attribute_ns(id, ns, local);
        mutated();
        return attribute_object(c, node_id{}, *held);
    });
    {
        // THE MAP IS ROOTED THROUGH THE GETTER. A C++ lambda's captures are
        // invisible to a precise collector, so the raw pointer this closes over
        // would not keep the object alive - `retained` is the channel that
        // does, and the getter itself is reachable from the wrapper's accessor
        // table. See native_object::retained.
        const value map_value = value::object(map);
        auto * getter = cx.allocate<script::native_object>(
            "attributes", [this, map, id](context & c, std::span<value>) {
                refresh_attribute_map(c, *map, id);
                return value::object(map);
            });
        getter->retained.push_back(map_value);
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
    auto * held = static_cast<script::object_object *>(cx.make_object().as_heap());
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
    auto * handler = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto trap = [&](std::string name, script::native_fn fn) {
        handler->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // `length`, `cssText` and the indexed properties are COMPUTED here rather
    // than stored. Storing them would put `length: 5` in the element's style
    // attribute - the store IS the declaration list, and anything in it that is
    // not a declaration has to be filtered back out by every reader.
    trap("get", [reseed](context & c, std::span<value> args) {
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
        const value * found = store->find(css);
        if (found == nullptr) {
            // A SUPPORTED PROPERTY THAT IS NOT SET IS "", NOT undefined.
            // CSSOM 6.7.2 gives every property in the IDL a getter that
            // returns the empty string when the declaration block has none,
            // and `serialize-values.html` reads exactly that for the ones it
            // could not set. `undefined` is reserved for a name that is not a
            // property at all - `el.style.toString`, `el.style.constructor` -
            // because answering "" there would break every ordinary lookup.
            if (style::css::find_property(css) != nullptr) { return c.string(""); }
            return value::undefined();
        }
        return c.string(std::string{declared_value(c.to_string(*found))});
    });
    trap("set", [this, reseed, wrote](context & c, std::span<value> args) {
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
            // grammar. A refusal is silent - CSSOM says an unparseable value
            // leaves the declaration alone, and a throw here would break every
            // page that sets a property this engine has not implemented.
            (void)store_declaration(*store, c, css_name_of(name), c.to_string(args[2]), false,
                                    false);
        }
        wrote(c, *store);
        mutated();
        return value::boolean(true);
    });
    const value style_view =
        value::object(cx.allocate<script::proxy_object>(target, value::object(handler)));

    // `setProperty` / `getPropertyValue` / `removeProperty` take the CSS
    // spelling rather than the IDL one, so they are the only way to reach a
    // custom property (`--x`) - which no identifier can name.
    const auto declaration_method = [&](std::string name, script::native_fn fn) {
        held->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // A NON-CUSTOM PROPERTY NAME IS LOWERCASED, a custom one is not: `--X` and
    // `--x` are two different properties and `COLOR` and `color` are one.
    const auto asked_name = [](context & c, std::span<value> args) {
        const std::string given = arg_string(c, args, 0);
        return given.starts_with("--") ? given : ascii_lower_copy(given);
    };
    declaration_method(
        "setProperty", [this, held, asked_name, reseed, wrote](context & c, std::span<value> args) {
            // "If priority is not the empty string and is not an ASCII
            // case-insensitive match for 'important', return." - CSSOM 6.7.2.
            // The VALUE may not carry one; the third argument is the only way
            // a page can ask for it.
            const std::string priority = args.size() > 2 ? c.to_string(args[2]) : std::string{};
            if (!priority.empty() && !ascii_iequals(priority, "important")) {
                return value::undefined();
            }
            reseed(c, *held);
            (void)store_declaration(*held, c, asked_name(c, args),
                                    args.size() > 1 ? c.to_string(args[1]) : std::string{}, false,
                                    !priority.empty());
            wrote(c, *held);
            mutated();
            return value::undefined();
        });
    // ...and it ANSWERS with the value it removed, which is what CSSOM says and
    // what a page toggling a property reads to put it back.
    declaration_method("removeProperty", [this, held, asked_name, reseed,
                                          wrote](context & c, std::span<value> args) {
        reseed(c, *held);
        const std::string name = asked_name(c, args);
        const value * found = held->find(name);
        const std::string was =
            found == nullptr ? std::string{} : std::string{declared_value(c.to_string(*found))};
        held->erase(name);
        wrote(c, *held);
        mutated();
        return c.string(was);
    });
    declaration_method("getPropertyValue",
                       [held, asked_name, reseed](context & c, std::span<value> args) {
                           reseed(c, *held);
                           const value * found = held->find(asked_name(c, args));
                           if (found == nullptr) { return c.string(""); }
                           return c.string(std::string{declared_value(c.to_string(*found))});
                       });
    declaration_method("getPropertyPriority",
                       [held, asked_name, reseed](context & c, std::span<value> args) {
                           reseed(c, *held);
                           const value * found = held->find(asked_name(c, args));
                           if (found == nullptr) { return c.string(""); }
                           return c.string(std::string{declared_priority(c.to_string(*found))});
                       });
    declaration_method("item", [held, reseed](context & c, std::span<value> args) {
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
    const auto tree_property = [&](std::string property, script::native_fn read,
                                   script::native_fn write) {
        obj.define_accessor(
            property, value::object(cx.allocate<script::native_object>(property, std::move(read))),
            value::object(cx.allocate<script::native_object>(property, std::move(write))));
    };
    tree_property(
        "innerHTML", [this, id](context & c, std::span<value>) { return c.string(inner_html(id)); },
        [this, id](context & c, std::span<value> a) {
            set_inner_html(id, arg_string(c, a, 0));
            return value::undefined();
        });
    // `data` AND `nodeValue` - the text a Text or Comment node holds, which is
    // the one thing those two nodes are FOR. `childNodes` has handed them out
    // all along and there was no way to read what was in one: `.data` was
    // undefined, `.nodeValue` was undefined, and the only spelling that worked
    // was textContent, which is the same answer by accident and a different
    // question. Both are accessors, both write through, and on an element they
    // are null - which is what the DOM says and is not the same as absent.
    for (const char * spelling : {"data", "nodeValue"}) {
        tree_property(
            spelling,
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
    tree_property(
        "textContent",
        [this, id](context & c, std::span<value>) { return c.string(text_content(id)); },
        [this, id](context & c, std::span<value> a) {
            // Text, never markup: that is the whole point of the property, and
            // the reason a page reaches for it instead of innerHTML.
            set_text(id, arg_string(c, a, 0));
            return value::undefined();
        });

    // `value` and `checked` ARE ACCESSORS, on a control.
    //
    // They were data properties written by refresh_control on whatever tick it
    // next ran. A page that creates a control and reads it back in the same
    // statement - `createInput('hello').value()`, which is p5's own DOM library
    // - therefore read the property as it was before the value existed. The
    // header's note that "the VM has no property accessors" was true when it
    // was written and is not any more.
    //
    // refresh_control still runs: it writes BACK a property assignment into the
    // control, which is how `input.value = ''` clears a field. These make the
    // READ live, which is the half that could not be done before.
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(id).value_or(atom{}));
        const std::string_view type = txn.attribute_value(id, atoms_->intern("type"));
        if (control_kind_of(tag, type) != control_kind::none) {
            obj.define_accessor("value",
                                value::object(cx.allocate<script::native_object>(
                                    "value",
                                    [this, id](context & c, std::span<value>) {
                                        const auto read = doc_->read();
                                        return c.string(forms_->state_of(read, *atoms_, id).value);
                                    })),
                                value::object(cx.allocate<script::native_object>(
                                    "value", [this, id](context & c, std::span<value> a) {
                                        const auto read = doc_->read();
                                        control_state & control =
                                            forms_->state_of(read, *atoms_, id);
                                        control.value = arg_string(c, a, 0);
                                        control.caret = control.value.size();
                                        control.selection = control.caret;
                                        // An assignment DIRTIES the control, so the `value`
                                        // attribute stops being the answer - otherwise setting
                                        // it to "" would be undone by the next read.
                                        control.value_edited = true;
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
    const auto navigate = [&](std::string property, script::native_fn fn) {
        obj.define_accessor(
            property, value::object(cx.allocate<script::native_object>(property, std::move(fn))),
            value::undefined());
    };
    navigate("parentNode", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        return wrap(c, txn.parent(id));
    });
    // `parentElement` IS NOT `parentNode`. It is null when the parent is not an
    // element, which is exactly the case a tree-walking page tests to know it
    // has reached the top: `<html>`'s parent is the DOCUMENT, and answering
    // with it made the walk run one level past the root.
    navigate("parentElement", [this, id](context & c, std::span<value>) {
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
    navigate("shadowRoot", [this, id](context & c, std::span<value>) {
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
    navigate("isConnected", [this, id](context & c, std::span<value>) {
        (void)c;
        const auto txn = doc_->read();
        const node_id top = root_of_tree(txn, id, true);
        return value::boolean(is_document_root(txn, top));
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
    navigate("firstElementChild", [this, id, element_children](context & c, std::span<value>) {
        const auto txn = doc_->read();
        const std::vector<node_id> kids = element_children(txn, id);
        return kids.empty() ? value::null() : wrap(c, kids.front());
    });
    navigate("lastElementChild", [this, id, element_children](context & c, std::span<value>) {
        const auto txn = doc_->read();
        const std::vector<node_id> kids = element_children(txn, id);
        return kids.empty() ? value::null() : wrap(c, kids.back());
    });
    navigate("childElementCount", [this, id, element_children](context & c, std::span<value>) {
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
        const node_id parent = txn.parent(self);
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
    navigate("firstChild", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        const std::span<const node_id> kids = txn.children(id);
        return kids.empty() ? value::null() : wrap(c, kids.front());
    });
    navigate("lastChild", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        const std::span<const node_id> kids = txn.children(id);
        return kids.empty() ? value::null() : wrap(c, kids.back());
    });
    navigate("nextSibling",
             [id, sibling](context & c, std::span<value>) { return sibling(c, id, true, false); });
    navigate("previousSibling",
             [id, sibling](context & c, std::span<value>) { return sibling(c, id, false, false); });
    navigate("nextElementSibling",
             [id, sibling](context & c, std::span<value>) { return sibling(c, id, true, true); });
    navigate("previousElementSibling",
             [id, sibling](context & c, std::span<value>) { return sibling(c, id, false, true); });
    // `childNodes` is EVERY child, text nodes included; `children` is the
    // elements only. Both exist because they answer different questions, and a
    // page that wants the text nodes has no other way to reach them.
    navigate("childNodes", [this, id](context & c, std::span<value>) {
        value list = c.make_array();
        auto * items = static_cast<script::array_object *>(list.as_heap());
        const auto txn = doc_->read();
        for (const node_id child : txn.children(id)) { items->items.push_back(wrap(c, child)); }
        return list;
    });
    // AN HTMLCollection, LIVE - not an Array. `children` is the one of these
    // navigations the DOM gives an interface to, and `ParentNode-children.html`
    // checks liveness by appending and then asks what the thing IS.
    navigate("children", [this, id](context & c, std::span<value>) {
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
    const auto reflect_size = [&](std::string property, double fallback) {
        obj.define_accessor(
            property,
            value::object(cx.allocate<script::native_object>(
                property,
                [this, id, property, fallback](context &, std::span<value>) {
                    const auto txn = doc_->read();
                    const std::string_view text = txn.attribute_value(id, atoms_->intern(property));
                    double parsed = 0;
                    bool any = false;
                    for (const char c : text) {
                        if (c < '0' || c > '9') { break; }
                        parsed = parsed * 10 + (c - '0');
                        any = true;
                    }
                    return value::number(any ? parsed : fallback);
                })),
            value::object(cx.allocate<script::native_object>(
                property, [this, id, property](context &, std::span<value> a) {
                    const double want = arg_number(a, 0);
                    (void)doc_->set_attribute(id, atoms_->intern(property),
                                              std::to_string(static_cast<long long>(want)));
                    // The SURFACE follows, or the canvas keeps drawing into a
                    // buffer of the size it was created at and everything past
                    // that edge is silently discarded.
                    if (canvases_ != nullptr) {
                        const auto txn = doc_->read();
                        const auto number = [&](std::string_view name, int missing) {
                            const std::string_view text =
                                txn.attribute_value(id, atoms_->intern(name));
                            int out = 0;
                            bool any = false;
                            for (const char c : text) {
                                if (c < '0' || c > '9') { break; }
                                out = out * 10 + (c - '0');
                                any = true;
                            }
                            return any ? out : missing;
                        };
                        const int w = number("width", 300);
                        const int h = number("height", 150);
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

    // --- element.classList
    //
    // Every operation reads the attribute, edits the token list and writes it
    // back, so nothing is cached and a class added by the parser, by
    // setAttribute or by the style engine is seen by all of them.
    auto * list = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto tokens_now = [this, id] {
        const auto txn = doc_->read();
        return class_tokens(txn.attribute_value(id, atoms_->intern("class")));
    };
    const auto write_tokens = [this, id](const std::vector<std::string> & tokens) {
        std::string text;
        for (const std::string & token : tokens) {
            if (!text.empty()) { text += ' '; }
            text += token;
        }
        (void)doc_->set_attribute(id, atoms_->intern("class"), text);
        mutated();
    };
    const auto list_method = [&](std::string name, script::native_fn fn) {
        list->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    list_method("add", [tokens_now, write_tokens](context & c, std::span<value> args) {
        std::vector<std::string> tokens = tokens_now();
        for (const value & v : args) {
            const std::string token = c.to_string(v);
            if (token.empty()) { continue; }
            if (std::find(tokens.begin(), tokens.end(), token) == tokens.end()) {
                tokens.push_back(token);
            }
        }
        write_tokens(tokens);
        return value::undefined();
    });
    list_method("remove", [tokens_now, write_tokens](context & c, std::span<value> args) {
        std::vector<std::string> tokens = tokens_now();
        for (const value & v : args) {
            const std::string token = c.to_string(v);
            std::erase(tokens, token);
        }
        write_tokens(tokens);
        return value::undefined();
    });
    list_method("contains", [tokens_now](context & c, std::span<value> args) {
        const std::vector<std::string> tokens = tokens_now();
        return value::boolean(std::find(tokens.begin(), tokens.end(), arg_string(c, args, 0)) !=
                              tokens.end());
    });
    list_method("toggle", [tokens_now, write_tokens](context & c, std::span<value> args) {
        std::vector<std::string> tokens = tokens_now();
        const std::string token = arg_string(c, args, 0);
        // The two-argument form FORCES a state rather than flipping it -
        // `classList.toggle("on", isOn)` is the idiom, and treating the second
        // argument as absent turns it into a flip that is right half the time.
        const bool present = std::find(tokens.begin(), tokens.end(), token) != tokens.end();
        const bool want = args.size() > 1 ? context::truthy(args[1]) : !present;
        if (want && !present) { tokens.push_back(token); }
        if (!want && present) { std::erase(tokens, token); }
        write_tokens(tokens);
        return value::boolean(want);
    });
    list_method("item", [tokens_now](context & c, std::span<value> args) {
        const std::vector<std::string> tokens = tokens_now();
        const auto i = static_cast<std::ptrdiff_t>(
            context::to_number(args.empty() ? value::undefined() : args[0]));
        if (i < 0 || static_cast<std::size_t>(i) >= tokens.size()) { return value::null(); }
        return c.string(tokens[static_cast<std::size_t>(i)]);
    });
    // An ACCESSOR, not a number: the count changes whenever the attribute does,
    // and a data property would report whatever it was when the element was
    // first wrapped.
    list->define_accessor("length",
                          value::object(cx.allocate<script::native_object>(
                              "length",
                              [tokens_now](context &, std::span<value>) {
                                  return value::number(static_cast<double>(tokens_now().size()));
                              })),
                          value::undefined());
    // READONLY, and this one had a price. `classList` is `[SameObject] readonly
    // attribute DOMTokenList` and it was a writable data property, so
    // `Element-classlist.html` - 1,420 subtests - assigned a STRING to it in
    // its first case and every case after it called `add`, `contains` and
    // `item` on that string. A write to a readonly property is silently
    // discarded in sloppy mode, which is what the corpus expects to happen.
    obj.define("classList", value::object(list),
               script::attr_enumerable | script::attr_configurable);

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
        const bool wanted = txn.kind(id).value_or(node_kind::text) == node_kind::element &&
                            (ns == html_namespace || ns == svg_namespace || ns == mathml_namespace);
        if (wanted) { install_dataset(cx, obj, id); }
    }
}

} // namespace ctbrowser::shell
