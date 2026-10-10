//===- views.cpp - Per-element view accessors -------------------*- C++ -*-===//
//
// install_element_views: the accessors defined on each node wrapper as it is
// made, because they are views onto that one node: box metrics (CSSOM View),
// `attributes`, innerHTML/outerHTML, textContent, a control's value and
// checked, parentNode/parentElement, shadowRoot, isConnected and baseURI.
// Reflected attributes live on the interface prototypes instead.
//
//===----------------------------------------------------------------------===//
#include "control_methods/helpers.hpp"
#include "internal.hpp"

#include "view_geometry.hpp"
#include <ctbrowser/dom/token_list.hpp>
#include <ctbrowser/layout/overflow.hpp>
#include <ctbrowser/shell/page/canvas.hpp>

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_element_views(context & cx, script::object_object & obj, node_id id) {
    // `<style>.sheet` and `<link>.sheet` (the LinkStyle mixin), defined in
    // bindings/stylesheets/objects.cpp. Installed per wrapper because it is a
    // view onto one element.
    install_sheet_property(cx, obj, id);

    // --- the box metrics: offsetParent/Left/Top/Width/Height, clientWidth/
    // Height, clientLeft/Top, scrollWidth/Height (CSSOM View §7 and §8).
    //
    // Getters that flush layout first, so a read reflects styles set earlier
    // in the same script, as Bootstrap's `reflow(el)` relies on (flush_layout
    // runs only stale work; see set_layout_hook).
    //
    // The viewport element (root, or body in quirks mode) reports the layout
    // viewport width, which excludes a scrollbar (Bootstrap centres
    // `.container` in it), and the window height (p5's windowHeight reads
    // `documentElement.clientHeight`). An element with no box reports 0, not
    // the viewport, and an inline box reports 0 for all client metrics (§7).
    // scrollWidth/Height are the scrolling area: the padding box grown to
    // cover overflow (layout/overflow.hpp), and the viewport's for the root.
    // `long` metrics are rounded.
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
                        // A table's client box is its whole border box and its
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
    // The map is built once and refilled by the accessor, so it keeps its
    // identity across reads while its contents are read out of the document
    // every time. See refresh_attribute_map for why it is an array-like object
    // and not a proxy.
    auto * map = cx.allocate<script::object_object>();
    // The owning wrapper, for the methods on NamedNodeMap.prototype (see
    // install_named_node_map). It is under a symbol key, which
    // getOwnPropertyNames does not report: WPT dom/nodes/attributes.html
    // expects the map's own names to be the indices and the exposed qualified
    // names, nothing else.
    map->define(named_node_map_owner_key, value::object(&obj), script::attr_none);
    {
        // The map is kept alive through the getter. A C++ lambda's captures
        // are invisible to a precise collector, so the raw pointer this closes
        // over would not keep the object alive; `retained` does, and the
        // getter itself is reachable from the wrapper's accessor table. See
        // native_object::retained.
        //
        // The map is handed out behind a trap-less proxy. `length`, `item` and
        // the rest are on NamedNodeMap.prototype, and the engine's array-like
        // iteration (`for (a of el.attributes)`) reads `length` as an own
        // property of a plain object but through the prototype chain of a
        // proxy, so the proxy makes both the WebIDL property model and the
        // iteration work.
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
    // The reflected attributes (`id`, `className`, `href`, ...) are not
    // installed here: they live on the interface prototypes, built from one
    // table by install_dom_interfaces (interfaces.cpp), because reflection is
    // defined per interface. Only properties that cannot be a table row stay
    // per element: a control's `value` and `checked` (below), which track user
    // edits rather than an attribute, and <canvas> `width`/`height`, which
    // resize a buffer (install_element_child_views in child_views.cpp).

    // `innerHTML` and `outerHTML` are accessors over the tree: a write parses
    // markup into nodes and a read serialises what the DOM holds now. Both are
    // [LegacyNullToEmptyString]: `el.innerHTML = null` empties the element
    // rather than writing the word (WPT innerhtml-07.html).
    const auto markup_of = [](context & c, std::span<value> a) {
        return arg(a, 0).is_null() ? std::string{} : arg_string(c, a, 0);
    };
    define_getter(
        cx, obj, "innerHTML",
        [this, id](context & c, std::span<value>) { return c.string(inner_html(id)); },
        [this, id, markup_of](context & c, std::span<value> a) {
            set_inner_html(id, markup_of(c, a));
            return value::undefined();
        });
    define_getter(
        cx, obj, "outerHTML",
        [this, id](context & c, std::span<value>) { return c.string(outer_html(id)); },
        [this, id, markup_of](context & c, std::span<value> a) {
            set_outer_html(c, id, markup_of(c, a));
            return value::undefined();
        });
    // `data` and `nodeValue`: the text of a Text or Comment node, read and
    // written through. `nodeValue` is null on an element; `data` is not
    // installed on one, since an own accessor would shadow `object.data`.
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
                    // Null is the empty string, not "null": `data` is
                    // [LegacyNullToEmptyString] and `nodeValue` is a nullable
                    // DOMString whose null means "no value". Only null, not
                    // undefined: WPT CharacterData-data.html expects
                    // `.data = undefined` to write "undefined".
                    const value given = arg(a, 0);
                    (void)doc_->set_text(id, given.is_null() ? std::string{} : arg_string(c, a, 0));
                    mutated();
                }
                return value::undefined();
            });
    }
    // `textContent` is null on a doctype, and writing it does nothing (DOM
    // 4.4's table).
    const bool doctype =
        doc_->read().kind(id).value_or(node_kind::element) == node_kind::document_type;
    define_getter(
        cx, obj, "textContent",
        [this, id, doctype](context & c, std::span<value>) {
            return doctype ? value::null() : c.string(text_content(id));
        },
        [this, id, doctype](context & c, std::span<value> a) {
            if (doctype) { return value::undefined(); }
            // Text, never markup. textContent is a nullable DOMString, so null
            // means ""; this also maps undefined to "", which the spec would
            // write as "undefined".
            const value given = arg(a, 0);
            set_text(id, given.is_nullish() ? std::string{} : c.to_string(given));
            control_helpers{this}.children_replaced(this, id);
            return value::undefined();
        });

    // `value` and `checked` are accessors on a control, so a page that creates
    // a control and reads it back in the same statement (p5's
    // `createInput('hello').value()`) reads the value that exists now.
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(id).value_or(atom{}));
        const std::string_view type = txn.attribute_value(id, atoms_->intern("type"));
        // Not a <button>: its `value` is a plain reflection of the attribute
        // (HTMLButtonElement, a row in the table), not a control's state, and
        // an own accessor here would shadow the row on the prototype. Every
        // <input> though, whatever its type today: `input.type` changes and
        // the store follows the type state (HTML 4.10.5.1's value modes), so
        // the accessor cannot be decided by the type the wrapper was made at.
        if (tag != "button" &&
            (tag == "input" || control_kind_of(tag, type) != control_kind::none)) {
            obj.define_accessor(
                "value",
                value::object(cx.allocate<script::native_object>(
                    "value",
                    [this, id](context & c, std::span<value>) {
                        const auto read = doc_->read();
                        // A <select>'s value is its first selected option's,
                        // or "" when none is (HTML 4.10.7) - not what was last
                        // assigned: `select.value = "no such option"`
                        // deselects every option and reads back "".
                        if (read.element_ns(id) == node_ns::html &&
                            read.local_name(id) == "select") {
                            const control_helpers helpers{this};
                            std::vector<node_id> options;
                            const std::vector<bool> flags =
                                helpers.selected_flags(c, read, this, id, options);
                            for (std::size_t i = 0; i < options.size(); ++i) {
                                if (flags[i]) {
                                    return c.string(helpers.option_value(read, this, options[i]));
                                }
                            }
                            return c.string("");
                        }
                        return c.string(control_helpers{this}.value_of(read, this, id));
                    })),
                value::object(cx.allocate<script::native_object>("value", [this,
                                                                           id](context & c,
                                                                               std::span<value> a) {
                    // `[LegacyNullToEmptyString]`: null is "", not the word.
                    const value given = arg(a, 0);
                    std::string text = given.is_null() ? std::string{} : c.to_string(given);
                    // A <select> sets selectedness (HTML 4.10.7): every
                    // option off, then the first whose value is the new
                    // one on and dirty - a multiple select too, which
                    // the lazy reconcile of the store never reaches.
                    // The model is read first, so a list that changed
                    // before this assignment has had its reset.
                    if (const auto read = doc_->read();
                        read.element_ns(id) == node_ns::html && read.local_name(id) == "select") {
                        const control_helpers helpers{this};
                        // A plain DOMString: null is "null", no option's value.
                        const std::string wanted = given.is_null() ? std::string{"null"} : text;
                        std::vector<node_id> options;
                        (void)helpers.selected_flags(c, read, this, id, options);
                        bool found = false;
                        for (const node_id option : options) {
                            const bool hit =
                                !found && helpers.option_value(read, this, option) == wanted;
                            found = found || hit;
                            helpers.set_slot(c, this, option, selected_slot, value::boolean(hit));
                            if (hit) {
                                helpers.set_slot(c, this, option, dirty_slot, value::boolean(true));
                            }
                        }
                        const std::string chosen = found ? wanted : std::string{};
                        forms_->state_of(read, *atoms_, id).value = chosen;
                        helpers.set_slot(c, this, id, "__syncedValue", c.string(chosen));
                        wrote_to_control_ = true;
                        mutated();
                        return value::undefined();
                    }
                    std::string mode;
                    {
                        const auto read = doc_->read();
                        mode = input_types::value_mode_of(forms_->state_of(read, *atoms_, id).type);
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
                        // The default modes write the content attribute.
                        (void)doc_->set_attribute(id, atoms_->intern("value"), text);
                        mutated();
                        return value::undefined();
                    }
                    const auto read = doc_->read();
                    // An assignment dirties the control, so the `value`
                    // attribute stops being the answer (otherwise setting
                    // it to "" would be undone by the next read), and the
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
            obj.define_accessor(
                "checked",
                value::object(cx.allocate<script::native_object>(
                    "checked",
                    [this, id](context &, std::span<value>) {
                        const auto read = doc_->read();
                        return value::boolean(forms_->state_of(read, *atoms_, id).checked);
                    })),
                value::object(cx.allocate<script::native_object>(
                    "checked", [this, id](context &, std::span<value> a) {
                        const auto read = doc_->read();
                        const bool on = !a.empty() && context::truthy(a[0]);
                        control_state & held = forms_->state_of(read, *atoms_, id);
                        held.checked = on;
                        held.checked_edited = true;
                        if (on) { control_helpers{this}.uncheck_radio_group(read, this, id); }
                        wrote_to_control_ = true;
                        mutated();
                        return value::undefined();
                    })));
        }
    }

    // `parentNode` is an accessor because the tree moves: a value captured at
    // wrapping time would stay null for an element created detached and
    // appended later.
    // `dom_parent`, not `parent()`: the document element's parent is the
    // Document, which `wrap` hands back as the page's own `document`.
    define_getter(cx, obj, "parentNode", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        return wrap(c, dom_parent(txn, id));
    });
    // `parentElement` is not `parentNode`: it is null when the parent is not an
    // element, which is how a tree-walking page knows it has reached the top
    // (`<html>`'s parent is the Document).
    define_getter(cx, obj, "parentElement", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        const node_id parent = txn.parent(id);
        if (!parent || txn.kind(parent).value_or(node_kind::element) != node_kind::element) {
            return value::null();
        }
        return wrap(c, parent);
    });
    // `element.shadowRoot`: the root if it is open, otherwise null. A closed
    // root is hidden only from the page, not the engine: the document still
    // records it and `getRootNode()` on a node inside it still answers with
    // it; only this accessor refuses to hand it over.
    define_getter(cx, obj, "shadowRoot", [this, id](context & c, std::span<value>) {
        const node_id root = shadow_root_of(id);
        const shadow_tree * tree = shadow_tree_of(root);
        if (tree == nullptr || !tree->open) { return value::null(); }
        return wrap(c, root);
    });
    // `isConnected`: "shadow-including root is a document" (DOM 4.4), the
    // same walk `getRootNode({composed: true})` does. A node inside a shadow
    // tree whose host is in the document is connected (WPT
    // Node-isConnected-shadow-dom.html).
    define_getter(cx, obj, "isConnected", [this, id](context & c, std::span<value>) {
        (void)c;
        const auto txn = doc_->read();
        const node_id top = root_of_tree(txn, id, true);
        return value::boolean(is_document_root(txn, top));
    });
    // `baseURI` (DOM 4.4) should be the document base URL. This returns the
    // document's own address and ignores any <base href>, unlike
    // detail::document_base_url; WPT Node-baseURI.html only compares it with
    // `document.baseURI`, which answers the same string.
    define_getter(cx, obj, "baseURI", [this](context & c, std::span<value>) {
        return c.string(secondary_ ? std::string{"about:blank"} : location_href_);
    });
    install_element_child_views(cx, obj, id);
}

} // namespace ctbrowser::shell
