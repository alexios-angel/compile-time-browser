#include "../computed_style/internal.hpp"
#include "internal.hpp"

#include "view_geometry.hpp"
#include <ctbrowser/dom/token_list.hpp>
#include <ctbrowser/layout/overflow.hpp>
#include <ctbrowser/shell/page/canvas.hpp>

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_element_views(context & cx, script::object_object & obj, node_id id) {
    // `<style>.sheet` and `<link>.sheet` - the LinkStyle mixin. Here rather than
    // in bindings/stylesheets.cpp for the same reason `style` is here: it is a
    // view onto ONE element and it has to be installed as its wrapper is made.
    install_sheet_property(cx, obj, id);

    // --- the box metrics: offsetParent/Left/Top/Width/Height, clientWidth/
    // Height, clientLeft/Top, scrollWidth/Height - CSSOM View §7 and §8.
    //
    // ACCESSORS THAT FLUSH LAYOUT, not numbers copied in at refresh. Reading
    // `offsetWidth` is how a page - Bootstrap's `reflow(el)`, every WPT file
    // that sizes a box with a style and measures it in the same script - asks
    // for the layout AS OF NOW; a copy taken before the first frame said 0 and
    // a copy taken at the last refresh said whatever the previous statement
    // left. `flush_layout` runs only what is stale (set_layout_hook).
    //
    // THE ROOT'S CLIENT RECTANGLE IS THE VIEWPORT (§7: the root element in a
    // no-quirks document, the body in a quirks one), and its two axes come
    // from different places on purpose: the width is the layout viewport (15px
    // narrower than the window when a scrollbar appears - which is what
    // Bootstrap's `.container` centred itself in), the height the window's,
    // because `documentElement.clientHeight` means "how tall is the window"
    // to p5's windowHeight. An ordinary element with no box has a client width
    // of zero: handing it the viewport told Babylon its canvas was
    // window-sized before layout had sized it, which failed WebGL setup
    // outright. An inline box answers zero for all four client metrics, as
    // §7 says. scrollWidth/scrollHeight are the SCROLLING AREA - the padding
    // box grown to everything that overflows it (layout/overflow.hpp) - and
    // for the root the viewport's; they answered the border box before, which
    // is 600 subtests of scrollWidthHeight-negative-margin-002 alone.
    // The `long` metrics are rounded, because that is what a `long` is.
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
                    const located at = locate(id);
                    const bool viewport_element = is_viewport_element(
                        id, which == metric::scroll_width || which == metric::scroll_height);
                    double v = 0;
                    switch (which) {
                    case metric::offset_left:
                    case metric::offset_top: {
                        if (at.f == nullptr || id == body_element()) { break; }
                        // Against the offsetParent's padding edge, or the
                        // initial containing block when there is none (§8).
                        // "ignoring any transforms that apply to the element
                        // and its ancestors": the translation comes off both.
                        point origin{};
                        if (const node_id parent = offset_parent_of(id)) {
                            const located p = locate(parent);
                            if (p.f != nullptr) {
                                const rect pad = layout::padding_box_of(*p.f);
                                origin = point{p.abs.x + pad.x - p.translation.x,
                                               p.abs.y + pad.y - p.translation.y};
                            }
                        }
                        v = which == metric::offset_left ? at.abs.x - at.translation.x - origin.x
                                                         : at.abs.y - at.translation.y - origin.y;
                        break;
                    }
                    case metric::offset_width: v = at.abs.width; break;
                    case metric::offset_height: v = at.abs.height; break;
                    case metric::client_width:
                    case metric::client_height:
                    case metric::client_left:
                    case metric::client_top: {
                        if (viewport_element &&
                            (which == metric::client_width || which == metric::client_height)) {
                            v = which == metric::client_width ? viewport_width_ : viewport_height_;
                            break;
                        }
                        if (at.f == nullptr || is_inline_box(*at.f)) { break; }
                        // A TABLE'S client box is its whole border box and its
                        // client edges are 0: the border sits on the table
                        // wrapper's grid, not around a padding box
                        // (table-client-props, table-with-border-client-*).
                        const bool table =
                            at.f->box != nullptr && at.f->box->kind == layout::box_kind::table;
                        const rect pad = table ? rect{0, 0, at.abs.width, at.abs.height}
                                               : layout::padding_box_of(*at.f);
                        switch (which) {
                        case metric::client_width: v = pad.width; break;
                        case metric::client_height: v = pad.height; break;
                        case metric::client_left: v = pad.x; break;
                        default: v = pad.y; break;
                        }
                        break;
                    }
                    case metric::scroll_width:
                    case metric::scroll_height: {
                        rect area{};
                        if (viewport_element) {
                            area = fragments_ == nullptr
                                       ? rect{0, 0, static_cast<float>(viewport_width_),
                                              static_cast<float>(viewport_height_)}
                                       : layout::viewport_scrolling_area(
                                             *fragments_, static_cast<float>(viewport_width_),
                                             static_cast<float>(viewport_height_));
                        } else if (at.f != nullptr) {
                            area = layout::scrolling_area_of(*at.f);
                        }
                        v = which == metric::scroll_width ? area.width : area.height;
                        break;
                    }
                    }
                    return value::number(std::round(v));
                });
            obj.define_accessor(name, value::object(getter), value::undefined());
        }
        // scrollTop/scrollLeft, §6: doubles, read and written through the
        // scroll state (scroll_position / set_scroll_position).
        for (const auto & [name, axis] :
             {std::pair{"scrollLeft", 'x'}, std::pair{"scrollTop", 'y'}}) {
            define_getter(
                cx, obj, name,
                [this, id, axis](context &, std::span<value>) {
                    return value::number(scroll_position(id, axis));
                },
                [this, id, axis](context &, std::span<value> args) {
                    set_scroll_position(id, axis, args.empty() ? 0.0 : context::to_number(args[0]));
                    return value::undefined();
                });
        }
        // `currentCSSZoom`, §7: the effective zoom, which nothing here changes.
        obj.define_accessor("currentCSSZoom",
                            native(cx, "get currentCSSZoom",
                                   [](context &, std::span<value>) { return value::number(1); }),
                            value::undefined());
        // `scrollParent`, §8: the nearest scroll container up the containing
        // block chain, the scrollingElement at the initial containing block;
        // null for the root, the body, a box-less or unanchored fixed element.
        auto * scroll_parent_getter = cx.allocate<script::native_object>(
            "scrollParent", [this, id](context & c, std::span<value>) {
                flush_layout();
                const located at = locate(id);
                node_id body, root;
                {
                    const auto txn = doc_->read();
                    root = txn.root();
                }
                body = body_element();
                if (at.f == nullptr || at.f->box == nullptr || id == root || id == body) {
                    return value::null();
                }
                const bool fixed = at.f->box->position == layout::position_kind::fixed;
                const bool absolute = at.f->box->position == layout::position_kind::absolute;
                for (node_id up = doc_->read().parent(id); up; up = doc_->read().parent(up)) {
                    const located ancestor = locate(up);
                    const layout::box_node * b = ancestor.f == nullptr ? nullptr : ancestor.f->box;
                    if (b == nullptr) { continue; }
                    // A fixed box's containing block is the viewport unless a
                    // transform anchors it; an absolute one skips to the
                    // nearest positioned ancestor.
                    if (fixed && !b->transformed) { continue; }
                    if (absolute && !b->is_positioned() && !b->transformed) { continue; }
                    if (b->scroll_container && up != root) { return wrap(c, up); }
                    if (up == root) { break; }
                }
                if (fixed) { return value::null(); }
                const node_id scrolling = scrolling_element();
                return scrolling ? wrap(c, scrolling) : value::null();
            });
        obj.define_accessor("scrollParent", value::object(scroll_parent_getter),
                            value::undefined());
        // HTMLImageElement's `x` and `y` (§9): the border edge against the
        // initial containing block, ignoring the scroll.
        if (const auto txn = doc_->read(); txn.local_name(id) == "img") {
            for (const auto & [name, vertical] : {std::pair{"x", false}, std::pair{"y", true}}) {
                auto * getter = cx.allocate<script::native_object>(
                    name, [this, id, vertical](context &, std::span<value>) {
                        flush_layout();
                        const located at = locate(id);
                        return value::number(std::round(at.f == nullptr ? 0.0
                                                        : vertical ? at.abs.y - at.translation.y
                                                                   : at.abs.x - at.translation.x));
                    });
                obj.define_accessor(name, value::object(getter), value::undefined());
            }
        }
        // `offsetParent`, §8: null for the root, the body, a box-less element
        // and a fixed one; otherwise the nearest positioned ancestor, the body,
        // or a table part around a static element.
        auto * parent_getter = cx.allocate<script::native_object>(
            "offsetParent", [this, id](context & c, std::span<value>) {
                flush_layout();
                const node_id parent = offset_parent_of(id);
                return parent ? wrap(c, parent) : value::null();
            });
        obj.define_accessor("offsetParent", value::object(parent_getter), value::undefined());
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
                        if (forms_->assign_value(read, *atoms_, id, std::move(text))) {
                            // A changed value resets the selection direction
                            // with the caret (the slot control_methods keeps).
                            const value self = c.current_this();
                            if (self.is_object()) {
                                (void)static_cast<script::object_object *>(self.as_heap())
                                    ->erase("__selectionDirection");
                            }
                        }
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
    install_element_child_views(cx, obj, id);
}

} // namespace ctbrowser::shell
