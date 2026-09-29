// The computed ROLE and the accessible NAME of an element - what a screen
// reader would be told, and what testdriver's get_computed_role and
// get_computed_label ask a browser for (ctdrive's `accessibility` command).
//
// The role is the first concrete ARIA role the `role` attribute names, else the
// one ElementInternals gave the element as its default, else HTML-AAM's
// implicit role for the tag. The name is Accessible Name and Description
// Computation 1.2, step 2 as written: hidden (2A), aria-labelledby (2B), an
// embedded control's value (2C), aria-label (2D), the host language's own label
// (2E: <label>, alt, value, legend, caption...), the subtree's text (2F-2H)
// and the tooltip (2I).
//
// ponytail: CSS generated content (::before/::after) contributes nothing, and
// "presentation" conflict resolution (a focusable element keeps its role) is
// not done - the two corners of the algorithm no test here reaches.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/page/accessibility.hpp>

#include "../bindings/element/internal.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace ctbrowser::shell {

namespace accessibility {

namespace {

// ARIA 1.2's concrete roles, and the 1.3 name `image` the synonym folds to.
constexpr std::string_view concrete_roles[] = {"alert",         "alertdialog",
                                               "application",   "article",
                                               "banner",        "blockquote",
                                               "button",        "caption",
                                               "cell",          "checkbox",
                                               "code",          "columnheader",
                                               "combobox",      "comment",
                                               "complementary", "contentinfo",
                                               "definition",    "deletion",
                                               "dialog",        "directory",
                                               "document",      "emphasis",
                                               "feed",          "figure",
                                               "form",          "generic",
                                               "grid",          "gridcell",
                                               "group",         "heading",
                                               "image",         "img",
                                               "insertion",     "link",
                                               "list",          "listbox",
                                               "listitem",      "log",
                                               "main",          "mark",
                                               "marquee",       "math",
                                               "menu",          "menubar",
                                               "menuitem",      "menuitemcheckbox",
                                               "menuitemradio", "meter",
                                               "navigation",    "none",
                                               "note",          "option",
                                               "paragraph",     "presentation",
                                               "progressbar",   "radio",
                                               "radiogroup",    "region",
                                               "row",           "rowgroup",
                                               "rowheader",     "scrollbar",
                                               "search",        "searchbox",
                                               "separator",     "slider",
                                               "spinbutton",    "status",
                                               "strong",        "subscript",
                                               "suggestion",    "superscript",
                                               "switch",        "tab",
                                               "table",         "tablist",
                                               "tabpanel",      "term",
                                               "textbox",       "time",
                                               "timer",         "toolbar",
                                               "tooltip",       "tree",
                                               "treegrid",      "treeitem"};

} // namespace

std::string_view explicit_role(std::string_view tokens) {
    for (const std::string_view token : split_top_level(tokens, html_whitespace)) {
        const auto found = std::ranges::find_if(
            concrete_roles, [&](std::string_view role) { return ascii_iequals(token, role); });
        if (found == std::end(concrete_roles)) { continue; }
        if (*found == "img") { return "image"; }
        if (*found == "presentation") { return "none"; }
        if (*found == "directory") { return "list"; }
        return *found;
    }
    return {};
}

std::string_view implicit_role(const read_txn & txn, atom_table & atoms, node_id element) {
    const std::string_view tag = atoms.text(txn.tag(element).value_or(atom{}));
    const auto attr = [&](std::string_view name) {
        return txn.attribute_value(element, atoms.intern_lower(name));
    };
    const auto has = [&](std::string_view name) {
        return txn.has_attribute(element, atoms.intern_lower(name));
    };
    // The landmark a <header>/<footer> is unless it is scoped to a section.
    const auto scoped = [&] {
        for (node_id up = txn.parent(element); up; up = txn.parent(up)) {
            const std::string_view name = atoms.text(txn.tag(up).value_or(atom{}));
            if (name == "article" || name == "aside" || name == "main" || name == "nav" ||
                name == "section") {
                return true;
            }
        }
        return false;
    };
    if (txn.element_ns(element) != node_ns::html) { return tag == "math" ? "math" : ""; }
    struct entry {
        std::string_view tag;
        std::string_view role;
    };
    static constexpr entry fixed[] = {
        {"article", "article"},
        {"aside", "complementary"},
        {"blockquote", "blockquote"},
        {"button", "button"},
        {"caption", "caption"},
        {"code", "code"},
        {"datalist", "listbox"},
        {"dd", "definition"},
        {"del", "deletion"},
        {"details", "group"},
        {"dfn", "term"},
        {"dialog", "dialog"},
        {"dt", "term"},
        {"em", "emphasis"},
        {"fieldset", "group"},
        {"figure", "figure"},
        {"form", "form"},
        {"h1", "heading"},
        {"h2", "heading"},
        {"h3", "heading"},
        {"h4", "heading"},
        {"h5", "heading"},
        {"h6", "heading"},
        {"hgroup", "group"},
        {"hr", "separator"},
        {"ins", "insertion"},
        {"li", "listitem"},
        {"main", "main"},
        {"mark", "mark"},
        {"math", "math"},
        {"menu", "list"},
        {"meter", "meter"},
        {"nav", "navigation"},
        {"ol", "list"},
        {"optgroup", "group"},
        {"option", "option"},
        {"output", "status"},
        {"p", "paragraph"},
        {"progress", "progressbar"},
        {"search", "search"},
        {"strong", "strong"},
        {"sub", "subscript"},
        {"sup", "superscript"},
        {"table", "table"},
        {"tbody", "rowgroup"},
        {"td", "cell"},
        {"textarea", "textbox"},
        {"tfoot", "rowgroup"},
        {"thead", "rowgroup"},
        {"time", "time"},
        {"tr", "row"},
        {"ul", "list"},
    };
    for (const entry & e : fixed) {
        if (e.tag == tag) { return e.role; }
    }
    if (tag == "a" || tag == "area") { return has("href") ? "link" : "generic"; }
    if (tag == "header") { return scoped() ? "generic" : "banner"; }
    if (tag == "footer") { return scoped() ? "generic" : "contentinfo"; }
    if (tag == "img") { return has("alt") && attr("alt").empty() ? "none" : "image"; }
    if (tag == "section") {
        return has("aria-label") || has("aria-labelledby") || has("title") ? "region" : "generic";
    }
    if (tag == "select") {
        const std::string_view size = attr("size");
        return has("multiple") || (!size.empty() && size != "0" && size != "1") ? "listbox"
                                                                                : "combobox";
    }
    if (tag == "th") { return ascii_iequals(attr("scope"), "row") ? "rowheader" : "columnheader"; }
    if (tag == "input") {
        const std::string type = ascii_lower_copy(attr("type"));
        const bool list = has("list");
        if (type == "button" || type == "image" || type == "reset" || type == "submit") {
            return "button";
        }
        if (type == "checkbox") { return "checkbox"; }
        if (type == "radio") { return "radio"; }
        if (type == "range") { return "slider"; }
        if (type == "number") { return "spinbutton"; }
        if (type == "search") { return list ? "combobox" : "searchbox"; }
        if (type.empty() || type == "text" || type == "email" || type == "tel" || type == "url") {
            return list ? "combobox" : "textbox";
        }
        return type == "hidden" ? "" : "generic";
    }
    // Everything else - <div>, <span>, a custom element - is in the tree as a
    // generic container; the document's own two are its root.
    if (tag == "html" || tag == "head" || tag == "script" || tag == "style" || tag == "template") {
        return "";
    }
    return "generic";
}

bool names_from_content(std::string_view role) {
    static constexpr std::string_view roles[] = {
        "button",  "cell",    "checkbox",  "columnheader",     "comment",       "gridcell",
        "heading", "link",    "menuitem",  "menuitemcheckbox", "menuitemradio", "option",
        "radio",   "row",     "rowheader", "sectionhead",      "suggestion",    "switch",
        "tab",     "tooltip", "treeitem"};
    return std::ranges::find(roles, role) != std::end(roles);
}

bool is_embedded_control(std::string_view role) {
    static constexpr std::string_view roles[] = {"textbox",     "searchbox", "combobox",
                                                 "listbox",     "slider",    "spinbutton",
                                                 "progressbar", "scrollbar", "meter"};
    return std::ranges::find(roles, role) != std::end(roles);
}

} // namespace accessibility

// testdriver's get_computed_role / get_computed_label, through ctdrive: the
// element is whatever `getter(slot)` returns in the page (testdriver-vendor.js
// keeps what it was asked about under a slot number), and the answer is the
// role or the name. Nothing when the getter does not hand back one of this
// document's elements.
std::optional<std::string> dom_bindings::computed_accessibility(std::string_view getter,
                                                                double slot, bool label) {
    if (cx_ == nullptr || doc_ == nullptr) { return std::nullopt; }
    context & cx = *cx_;
    const auto in_realm = enter_realm(cx);
    // A property of the WINDOW, which is where `window.x = f` put it - the
    // realm's own global table is the other storage.
    value fn = value::undefined();
    if (script::object_object * window = window_object(); window != nullptr) {
        fn = cx.lookup_property(value::object(window), std::string{getter});
    }
    if (!fn.is_callable() && cx.has_global(getter)) { fn = cx.global(getter); }
    if (!fn.is_callable()) { return std::nullopt; }
    const context::rooted keep_fn{cx, fn};
    value argument = value::number(slot);
    const value element = cx.call(fn, std::span<const value>{&argument, 1}, value::undefined());
    const context::rooted keep_element{cx, element};
    if (cx.failed()) {
        (void)cx.take_error();
        return std::nullopt;
    }
    const node_id root = handle_of(element);
    if (!root) { return std::nullopt; }
    flush_layout();
    // NOT IN A DOCUMENT, NOT IN THE TREE: a detached element has no role and
    // no name (aria-element-reflection-labelledby.html asks before inserting).
    if (!is_connected(root)) { return std::string{}; }

    // The page's own IDL, read the way a script would - the reflection rules
    // (scope, detachment) are the getters' and are not restated here.
    const auto property = [&](node_id id, std::string_view name) {
        return cx.lookup_property(wrap(cx, id), std::string{name});
    };
    const auto internals_of = [&](node_id id) -> value {
        const auto held = wrappers_.find(id.key());
        if (held == wrappers_.end() || held->second == nullptr) { return value::undefined(); }
        const value * found = held->second->find(std::string{detail::internals_key});
        return found == nullptr ? value::undefined() : *found;
    };
    const auto string_of = [&](value v) { return v.is_string() ? cx.to_string(v) : std::string{}; };
    // A FrozenArray or a NodeList of elements, as node ids.
    const auto elements_of = [&](value list) {
        std::vector<node_id> out;
        if (!list.is_object_like()) { return out; }
        const context::rooted keep_list{cx, list};
        const double length = context::to_number(cx.lookup_property(list, "length"));
        for (double i = 0; i < length; ++i) {
            if (const node_id id = handle_of(cx.lookup_index(list, value::number(i)))) {
                out.push_back(id);
            }
        }
        return out;
    };

    const auto txn = doc_->read();
    const auto tag_of = [&](node_id id) { return atoms_->text(txn.tag(id).value_or(atom{})); };
    const auto attr = [&](node_id id, std::string_view name) {
        return txn.attribute_value(id, atoms_->intern_lower(name));
    };
    const auto has = [&](node_id id, std::string_view name) {
        return txn.has_attribute(id, atoms_->intern_lower(name));
    };
    const auto is_element = [&](node_id id) {
        return txn.kind(id).value_or(node_kind::text) == node_kind::element;
    };
    const auto style_value = [&](node_id id, std::string_view name) -> std::string_view {
        if (styles_ == nullptr) { return {}; }
        const auto found = styles_->find(style::engine::key_of(id));
        if (found == styles_->end() || !found->second) { return {}; }
        return found->second->get(atoms_->intern(name));
    };
    // The flat tree's parent: a slotted node's slot, a shadow root's host.
    const auto flat_parent = [&](node_id id) {
        if (const node_id slot = assigned_slot_of(id)) { return slot; }
        if (const node_id up = txn.parent(id)) { return up; }
        const shadow_tree * tree = shadow_tree_of(id);
        return tree != nullptr ? tree->host : node_id{};
    };
    const auto flat_children = [&](node_id id) {
        if (const node_id shadow = shadow_root_of(id)) {
            const auto kids = txn.children(shadow);
            return std::vector<node_id>{kids.begin(), kids.end()};
        }
        if (tag_of(id) == "slot") {
            std::vector<node_id> assigned = assigned_nodes_of(id);
            if (!assigned.empty()) { return assigned; }
        }
        const auto kids = txn.children(id);
        return std::vector<node_id>{kids.begin(), kids.end()};
    };
    // 2A's "hidden": not rendered - display:none on it or anything above it,
    // visibility hidden or collapse - or aria-hidden="true" on the way up.
    // An element the cascade gave no style is one that is not rendered - the
    // map holds what is being rendered, which is what getComputedStyle's
    // "rendered" test reads too.
    const auto unstyled = [&](node_id id) {
        return styles_ != nullptr && styles_->find(style::engine::key_of(id)) == styles_->end();
    };
    const auto hidden = [&](node_id id) {
        if (is_element(id) && unstyled(id)) { return true; }
        const std::string_view visibility = style_value(id, "visibility");
        if (visibility == "hidden" || visibility == "collapse") { return true; }
        for (node_id at = id; at; at = flat_parent(at)) {
            if (!is_element(at)) { continue; }
            if (style_value(at, "display") == "none") { return true; }
            if (ascii_iequals(trim(attr(at, "aria-hidden"), html_whitespace), "true")) {
                return true;
            }
        }
        return false;
    };
    const auto role_of = [&](node_id id) -> std::string {
        if (const std::string_view role = accessibility::explicit_role(attr(id, "role"));
            !role.empty()) {
            return std::string{role};
        }
        if (const value internals = internals_of(id); internals.is_object()) {
            const std::string given = string_of(cx.lookup_property(internals, "role"));
            if (const std::string_view role = accessibility::explicit_role(given); !role.empty()) {
                return std::string{role};
            }
        }
        return std::string{accessibility::implicit_role(txn, *atoms_, id)};
    };
    if (!label) { return hidden(root) ? std::string{} : role_of(root); }

    struct traversal {
        bool recursion = false;    // inside another node's computation
        bool labelledby = false;   // inside an aria-labelledby traversal
        bool allow_hidden = false; // the node labelledby named was itself hidden
    };
    const auto labels_of = [&](node_id id) { return elements_of(property(id, "labels")); };
    const auto labelledby_of = [&](node_id id) {
        std::vector<node_id> named = elements_of(property(id, "ariaLabelledByElements"));
        if (named.empty() && !has(id, "aria-labelledby")) {
            if (const value internals = internals_of(id); internals.is_object()) {
                named = elements_of(cx.lookup_property(internals, "ariaLabelledByElements"));
            }
        }
        return named;
    };
    const auto aria_label_of = [&](node_id id) -> std::string {
        if (has(id, "aria-label")) { return std::string{attr(id, "aria-label")}; }
        if (const value internals = internals_of(id); internals.is_object()) {
            return string_of(cx.lookup_property(internals, "ariaLabel"));
        }
        return {};
    };
    const auto join = [](const std::vector<std::string> & parts) {
        std::string out;
        for (const std::string & part : parts) {
            if (trim(part, html_whitespace).empty()) { continue; }
            if (!out.empty()) { out += ' '; }
            out += trim(part, html_whitespace);
        }
        return out;
    };

    std::vector<node_id> visited;
    const auto compute = [&](auto && self, node_id current, traversal state) -> std::string {
        // 2G: a text node is its text.
        if (txn.kind(current).value_or(node_kind::element) == node_kind::text) {
            return std::string{txn.text(current)};
        }
        if (!is_element(current)) { return {}; }
        // EACH ELEMENT ONCE: a label holding the very control it names, or two
        // elements labelledby each other, reach a node already being computed,
        // and it contributes nothing the second time.
        if (std::ranges::find(visited, current) != visited.end()) { return {}; }
        visited.push_back(current);
        // 2A.
        if (!state.allow_hidden && hidden(current)) { return {}; }
        // 2B: aria-labelledby, not followed from inside another one.
        if (!state.labelledby) {
            if (const std::vector<node_id> named = labelledby_of(current); !named.empty()) {
                std::vector<std::string> parts;
                for (const node_id each : named) {
                    parts.push_back(self(
                        self, each, traversal{true, true, state.allow_hidden || hidden(each)}));
                }
                return join(parts);
            }
        }
        const std::string role = role_of(current);
        // 2C: a control inside the label of ANOTHER one contributes its value.
        if (state.recursion && current != root && accessibility::is_embedded_control(role)) {
            if (role == "textbox" || role == "searchbox") {
                return string_of(property(current, "value"));
            }
            if (role == "combobox" || role == "listbox") {
                std::vector<std::string> chosen;
                if (tag_of(current) == "select") {
                    for (const node_id option : elements_of(property(current, "selectedOptions"))) {
                        chosen.push_back(string_of(property(option, "text")));
                    }
                } else if (tag_of(current) == "input") {
                    chosen.push_back(string_of(property(current, "value")));
                }
                return join(chosen);
            }
            if (has(current, "aria-valuetext")) {
                return std::string{attr(current, "aria-valuetext")};
            }
            if (has(current, "aria-valuenow")) {
                return std::string{attr(current, "aria-valuenow")};
            }
            return string_of(property(current, "value"));
        }
        // 2D: aria-label - the attribute, else ElementInternals' default.
        if (const std::string given = aria_label_of(current);
            !trim(given, html_whitespace).empty()) {
            return given;
        }
        // The subtree's text (2F), block boxes set apart by spaces.
        const auto content = [&](node_id of) {
            std::string out;
            for (const node_id child : flat_children(of)) {
                const std::string part =
                    self(self, child, traversal{true, state.labelledby, state.allow_hidden});
                const std::string_view display =
                    is_element(child) ? style_value(child, "display") : std::string_view{"inline"};
                const bool block =
                    !display.empty() && !display.starts_with("inline") && display != "contents";
                if (block) { out += ' '; }
                out += part;
                if (block) { out += ' '; }
            }
            return out;
        };
        // 2E: what the host language names it with, unless presentational.
        const std::string_view tag = tag_of(current);
        if (role != "none") {
            const std::string_view type = attr(current, "type");
            const bool input = tag == "input";
            if (input && ascii_iequals_any(type, {"button", "submit", "reset"})) {
                if (has(current, "value")) { return std::string{attr(current, "value")}; }
                if (ascii_iequals(type, "submit")) { return "Submit"; }
                if (ascii_iequals(type, "reset")) { return "Reset"; }
            } else if (input && ascii_iequals(type, "image")) {
                for (const std::string_view name : {"alt", "value", "title"}) {
                    if (!trim(attr(current, name), html_whitespace).empty()) {
                        return std::string{attr(current, name)};
                    }
                }
                return "Submit";
            } else if (input || tag == "textarea" || tag == "select" || tag == "button" ||
                       tag == "meter" || tag == "output" || tag == "progress") {
                std::vector<std::string> parts;
                for (const node_id each : labels_of(current)) { parts.push_back(content(each)); }
                if (std::string named = join(parts); !named.empty()) { return named; }
                if (tag != "button" && has(current, "title")) {
                    return std::string{attr(current, "title")};
                }
                if (tag != "button" && has(current, "placeholder")) {
                    return std::string{attr(current, "placeholder")};
                }
            } else if (tag == "img" || tag == "area") {
                if (has(current, "alt")) { return std::string{attr(current, "alt")}; }
            } else if (tag == "fieldset" || tag == "figure" || tag == "table") {
                const std::string_view first = tag == "fieldset" ? "legend"
                                               : tag == "figure" ? "figcaption"
                                                                 : "caption";
                for (const node_id child : txn.children(current)) {
                    if (tag_of(child) == first) {
                        return self(self, child, traversal{true, false, state.allow_hidden});
                    }
                }
            }
        }
        // 2F: the subtree, for a role named from content, anything reached by
        // recursion (2H), and anything inside a labelledby traversal.
        if (accessibility::names_from_content(role) || state.recursion || state.labelledby) {
            if (std::string text = content(current); !trim(text, html_whitespace).empty()) {
                return text;
            }
        }
        // 2I: the tooltip.
        if (has(current, "title")) { return std::string{attr(current, "title")}; }
        return {};
    };
    // Whitespace collapsed to single spaces and trimmed: a flat string.
    const std::string raw = compute(compute, root, traversal{});
    std::string flat;
    bool space = false;
    for (const char c : trim(raw, html_whitespace)) {
        if (html_whitespace.find(c) != std::string_view::npos) {
            space = true;
            continue;
        }
        if (space) { flat += ' '; }
        space = false;
        flat += c;
    }
    return flat;
}

} // namespace ctbrowser::shell
