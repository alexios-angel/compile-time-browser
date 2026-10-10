//===- rendered_text.cpp - innerText and editing attributes -----*- C++ -*-===//
//
// HTMLElement's text and editing attributes: the innerText/outerText getter
// (HTML's inner text collection steps, approximated without layout) and
// setters, plus translate, draggable, spellcheck, contentEditable and
// isContentEditable.
//
//===----------------------------------------------------------------------===//
#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {
/// Splits text into HTML's "rendered text fragment", which both the
/// `innerText` and `outerText` setters build. The string is cut at every
/// U+000A, U+000D or CRLF pair: each run of other code points goes to
/// `on_text` (a Text node) and each break to `on_break` (a `br` element), so
/// `el.innerText = "a\nb"` leaves three children where `textContent` leaves one.
///
/// Nothing is parsed or escaped, which is why the callers build nodes rather
/// than markup for set_inner_html: `abc<def` stays seven characters of text
/// and a U+0000 survives, where the HTML tokenizer would make an element of
/// the first and U+FFFD of the second (WPT innertext-setter-tests.js checks
/// both).
template <typename OnText, typename OnBreak>
void each_rendered_text_part(std::string_view text, OnText && on_text, OnBreak && on_break) {
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t start = at;
        while (at < text.size() && text[at] != '\n' && text[at] != '\r') { ++at; }
        if (at > start) { on_text(text.substr(start, at - start)); }
        while (at < text.size() && (text[at] == '\n' || text[at] == '\r')) {
            // One break for CRLF but two for CR CR: the pair is a single line
            // ending, the only case where the two characters are not
            // independent.
            if (text[at] == '\r' && at + 1 < text.size() && text[at + 1] == '\n') { ++at; }
            ++at;
            on_break();
        }
    }
}

// --- innerText getter: the "inner text collection steps", HTML 3.2.7
//
// The specification reads four computed properties (display, white-space,
// visibility, text-transform) from the box tree. This engine lays out once
// per frame, so the box tree would not reflect the script's own changes yet
// (as in `container.innerHTML = x; e.innerText`). Instead this reads the
// cascade when it can be flushed (a connected element of the page's own
// document), otherwise the element's inline `style` over the UA per-tag
// defaults.
// ponytail: text-transform is ASCII (the engine's rule, see
// core/algorithms.hpp), and ::first-line/::first-letter are not consulted.

/// The inherited properties that decide how a text node's data contributes.
struct text_style {
    bool preserve = false;        // white-space: pre / pre-wrap / break-spaces
    bool preserve_breaks = false; // white-space: pre-line
    bool hidden = false;          // visibility: hidden / collapse
    int transform = 0;            // +1 uppercase, -1 lowercase
};

/// One property of a `style="..."` attribute, lowercased, or "" when it is not
/// declared. Same split as declarations.cpp uses to seed `el.style`, with the
/// same limit: a `;` inside a string ends a declaration, which none of the
/// keyword properties read here can contain.
[[nodiscard]] std::string inline_declaration(std::string_view style, std::string_view property) {
    std::size_t at = 0;
    while (at < style.size()) {
        std::size_t end = style.find(';', at);
        if (end == std::string_view::npos) { end = style.size(); }
        const std::string_view declared = style.substr(at, end - at);
        const std::size_t colon = declared.find(':');
        if (colon != std::string_view::npos &&
            ascii_iequals(trim(declared.substr(0, colon), html_whitespace), property)) {
            return ascii_lower_copy(trim(declared.substr(colon + 1), html_whitespace));
        }
        at = end + 1;
    }
    return {};
}

/// The UA sheet's `display` for an HTML tag. Every foreign element is inline.
[[nodiscard]] std::string_view default_display(std::string_view tag) {
    if (lists_token("script style template noscript head title meta link base area param source "
                    "track datalist rp col colgroup",
                    tag)) {
        return "none";
    }
    if (lists_token("html body div p h1 h2 h3 h4 h5 h6 ul ol li dl dt dd pre listing xmp plaintext "
                    "blockquote address article aside footer header hr main nav section figure "
                    "figcaption fieldset legend form details summary dialog center dir menu "
                    "optgroup option hgroup search frameset frame",
                    tag)) {
        return "block";
    }
    if (tag == "table") { return "table"; }
    if (tag == "tr") { return "table-row"; }
    if (tag == "td" || tag == "th") { return "table-cell"; }
    if (tag == "caption") { return "table-caption"; }
    if (lists_token("tbody thead tfoot", tag)) { return "table-row-group"; }
    if (lists_token("input button select textarea img video audio canvas iframe object embed "
                    "meter progress",
                    tag)) {
        return "inline-block";
    }
    return "inline";
}

/// The elements whose contents are never text: replaced elements, and the
/// widgets whose children are not what they render.
constexpr std::string_view replaced_tags =
    "img input textarea iframe audio video canvas object embed meter progress";

/// The SVG elements that never render their contents: the `<defs>` and
/// gradient family, and the metadata the DOM keeps but no box shows.
constexpr std::string_view unrendered_svg_tags =
    "defs stop symbol clipPath mask marker pattern linearGradient radialGradient metadata title "
    "desc script style";

/// Implements the innerText getter: walks an element's subtree, recording
/// text, line breaks, tabs and atomic-inline boundaries as items, then joins
/// them with CSS white-space processing in finish().
class inner_text_collector {
public:
    inner_text_collector(const read_txn & txn, atom_table & atoms, const style::style_map * styles)
        : txn_(txn), atoms_(atoms), styles_(styles), style_(atoms.intern("style")),
          type_(atoms.intern("type")), hidden_(atoms.intern("hidden")),
          open_(atoms.intern("open")) {}

    [[nodiscard]] std::string_view tag_of(node_id node) const {
        return atoms_.text(txn_.tag(node).value_or(atom{}));
    }
    [[nodiscard]] bool is_html(node_id node) const {
        return txn_.element_ns(node) == node_ns::html;
    }
    [[nodiscard]] std::string_view own_style(node_id node) const {
        return txn_.attribute_value(node, style_);
    }
    /// A property's value from the cascade for this element (lowercased and
    /// trimmed), or, when there is no cascade or it has no value, the
    /// element's inline `style` declaration.
    [[nodiscard]] std::string declared(node_id node, std::string_view property) const {
        if (styles_ != nullptr) {
            const auto found = styles_->find(style::engine::key_of(node));
            if (found != styles_->end() && found->second) {
                const std::string_view text = found->second->get(atoms_.intern(property));
                if (!text.empty()) { return ascii_lower_copy(trim(text, html_whitespace)); }
            }
        }
        return inline_declaration(own_style(node), property);
    }

    /// The element's computed `display` keyword: cascade or inline style over
    /// the UA defaults. A float or an absolute/fixed position makes any
    /// display block-level.
    [[nodiscard]] std::string display_of(node_id node) const {
        const std::string_view tag = tag_of(node);
        const bool html = is_html(node);
        // `noscript` is `display: none !important` while scripting is on, and
        // a <template>'s children are its contents, which live elsewhere.
        if (html && (tag == "noscript" || tag == "template")) { return "none"; }
        if (txn_.element_ns(node) == node_ns::svg && lists_token(unrendered_svg_tags, tag)) {
            return "none";
        }
        // `[hidden]` and `<input type=hidden>` are `display: none` in the UA
        // sheet. This engine's sheet has no such rule, so they are checked
        // here, and an inline `display` still wins as the cascade would have it.
        if (html &&
            (txn_.has_attribute(node, hidden_) ||
             (tag == "input" && ascii_iequals(txn_.attribute_value(node, type_), "hidden"))) &&
            inline_declaration(own_style(node), "display").empty()) {
            return "none";
        }
        std::string display = declared(node, "display");
        // This engine's UA sheet says `tr, td { display: block }` because that
        // is how it lays a table out; the collection wants the tag's CSS table
        // display, so the sheet's answer yields unless the element's own style
        // said block.
        if (html && display == "block" && default_display(tag).starts_with("table") &&
            inline_declaration(own_style(node), "display").empty()) {
            display.clear();
        }
        if (display.empty()) {
            display = html ? std::string{default_display(tag)} : std::string{"inline"};
        }
        if (display == "none") { return display; }
        const std::string floated = declared(node, "float");
        const std::string position = declared(node, "position");
        if (floated == "left" || floated == "right" || position == "absolute" ||
            position == "fixed") {
            return "block";
        }
        return display;
    }

    /// True when this element or any ancestor is `display: none`: the
    /// specification's "not being rendered", which sends the getter to
    /// textContent instead.
    [[nodiscard]] bool unrendered(node_id node) const {
        for (node_id at = node; at && txn_.kind(at).value_or(node_kind::text) == node_kind::element;
             at = txn_.parent(at)) {
            if (display_of(at) == "none") { return true; }
        }
        return false;
    }

    /// The three inherited properties (white-space, visibility,
    /// text-transform) as this element passes them to its children: its own
    /// declared values over what it inherited.
    [[nodiscard]] text_style style_under(node_id node, text_style inherited) const {
        const std::string white_space = declared(node, "white-space");
        if (!white_space.empty()) {
            inherited.preserve =
                white_space == "pre" || white_space == "pre-wrap" || white_space == "break-spaces";
            inherited.preserve_breaks = white_space == "pre-line";
        } else if (is_html(node) &&
                   lists_token("pre listing xmp plaintext textarea", tag_of(node))) {
            inherited.preserve = true;
            inherited.preserve_breaks = false;
        }
        const std::string visibility = declared(node, "visibility");
        if (visibility == "hidden" || visibility == "collapse") {
            inherited.hidden = true;
        } else if (visibility == "visible") {
            inherited.hidden = false;
        }
        const std::string transform = declared(node, "text-transform");
        if (transform == "uppercase") {
            inherited.transform = 1;
        } else if (transform == "lowercase") {
            inherited.transform = -1;
        } else if (transform == "none") {
            inherited.transform = 0;
        }
        return inherited;
    }

    /// What the target's text inherits: its ancestors' styles applied
    /// root-first, then its own, since the target's `white-space: pre` governs
    /// its text too.
    [[nodiscard]] text_style inherited_style(node_id target) const {
        std::vector<node_id> chain;
        for (node_id at = target;
             at && txn_.kind(at).value_or(node_kind::text) == node_kind::element;
             at = txn_.parent(at)) {
            chain.push_back(at);
        }
        text_style style;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            style = style_under(*it, style);
        }
        return style;
    }

    /// Collects the children of `node` under `style`. The walk starts here for
    /// the target, whose own display contributes nothing ("No tab on
    /// table-cell itself", "No newline on table-row itself").
    void collect_children(node_id node, const text_style & style,
                          std::string_view display = "block") {
        const bool html = is_html(node);
        const std::string_view tag = tag_of(node);
        if (html && lists_token(replaced_tags, tag)) { return; }
        const bool closed_details = html && tag == "details" && !txn_.has_attribute(node, open_);
        // A <select> renders its options and an <optgroup> its options: text
        // beside them, and a group inside a group, has no box.
        const bool grouped = html && tag == "optgroup" && is_html(txn_.parent(node)) &&
                             tag_of(txn_.parent(node)) == "select";
        const std::string_view widget_children =
            html && tag == "select" ? "option optgroup" : (grouped ? "option" : "");
        // "Blockification": the in-flow children of a flex or grid container
        // are block-level whatever their display says.
        const bool blockify = lists_token("flex inline-flex grid inline-grid", display);
        // White space in a tabular container has no box (CSS 2.1 17.2.1): a
        // white-space-only text child whose neighbours (where it has any) are
        // table-internal boxes or captions is `display: none`, whatever
        // `white-space` says.
        const bool tabular = lists_token(
            "table inline-table table-row-group table-header-group table-footer-group table-row",
            display);
        const auto boxless_space = [&](std::span<const node_id> siblings, std::size_t i) {
            const std::string_view data = txn_.text(siblings[i]);
            if (data.find_first_not_of(html_whitespace) != std::string_view::npos) { return false; }
            const auto tabular_box = [&](std::size_t at) {
                const node_kind held = txn_.kind(siblings[at]).value_or(node_kind::comment);
                if (held != node_kind::element) { return held != node_kind::text; }
                return display_of(siblings[at]).starts_with("table-");
            };
            for (std::size_t at = i; at-- > 0;) {
                if (txn_.kind(siblings[at]).value_or(node_kind::comment) == node_kind::comment) {
                    continue;
                }
                if (!tabular_box(at)) { return false; }
                break;
            }
            for (std::size_t at = i + 1; at < siblings.size(); ++at) {
                if (txn_.kind(siblings[at]).value_or(node_kind::comment) == node_kind::comment) {
                    continue;
                }
                return tabular_box(at);
            }
            return true;
        };
        const std::span<const node_id> children = txn_.children(node);
        for (std::size_t i = 0; i < children.size(); ++i) {
            const node_id child = children[i];
            if (tabular && txn_.kind(child).value_or(node_kind::comment) == node_kind::text &&
                boxless_space(children, i)) {
                continue;
            }
            if (closed_details && !(is_html(child) && tag_of(child) == "summary")) { continue; }
            if (!widget_children.empty() &&
                !(is_html(child) && lists_token(widget_children, tag_of(child)))) {
                continue;
            }
            collect(child, style, blockify);
        }
    }

    /// Joins the collected items with the CSS white-space processing the
    /// specification defers to, done on the flat list: a collapsible run is
    /// one space, dropped at the start of a line and before a break, and a run
    /// of required line breaks is the longest of them, never at either end.
    [[nodiscard]] std::string finish() const {
        std::string out;
        bool pending_space = false;
        bool line_start = true;
        int pending_breaks = 0;
        const auto flush_breaks = [&] {
            if (pending_breaks > 0 && !out.empty()) {
                out.append(static_cast<std::size_t>(pending_breaks), '\n');
            }
            pending_breaks = 0;
        };
        for (const text_item & item : items_) {
            switch (item.kind) {
            case text_item::collapsible:
                for (const char c : item.text) {
                    if (html_whitespace.find(c) != std::string_view::npos) {
                        if (!line_start) { pending_space = true; }
                        continue;
                    }
                    flush_breaks();
                    if (pending_space) { out += ' '; }
                    pending_space = false;
                    line_start = false;
                    out += c;
                }
                break;
            case text_item::preserved:
                if (item.text.empty()) { break; }
                flush_breaks();
                if (pending_space) { out += ' '; }
                pending_space = false;
                out += item.text;
                line_start = item.text.back() == '\n';
                break;
            case text_item::separator:
                pending_space = false;
                flush_breaks();
                out += item.text;
                line_start = true;
                break;
            case text_item::open:
                // An atomic inline is a box of its own: a space before it is
                // real, and its own leading and trailing spaces are not.
                flush_breaks();
                if (pending_space) { out += ' '; }
                pending_space = false;
                line_start = true;
                break;
            case text_item::close:
                pending_space = false;
                line_start = false;
                break;
            case text_item::required:
                pending_space = false;
                line_start = true;
                pending_breaks = std::max(pending_breaks, item.breaks);
                break;
            }
        }
        return out;
    }

private:
    /// One entry in the flat list finish() joins.
    struct text_item {
        enum {
            collapsible,
            preserved,
            separator,
            open,
            close,
            required
        } kind = collapsible;
        std::string text;
        int breaks = 0;
    };

    void collect(node_id node, const text_style & inherited, bool blockify = false) {
        const node_kind kind = txn_.kind(node).value_or(node_kind::comment);
        if (kind == node_kind::text) {
            if (inherited.hidden) { return; }
            std::string text{txn_.text(node)};
            if (inherited.transform > 0) { ascii_upper_in_place(text); }
            if (inherited.transform < 0) { text = ascii_lower_copy(text); }
            if (inherited.preserve) {
                items_.push_back({text_item::preserved, std::move(text), 0});
            } else if (inherited.preserve_breaks) {
                // pre-line: a newline is a forced break, everything else collapses.
                std::size_t at = 0;
                while (at <= text.size()) {
                    const std::size_t nl = text.find('\n', at);
                    const std::size_t stop = nl == std::string::npos ? text.size() : nl;
                    items_.push_back({text_item::collapsible, text.substr(at, stop - at), 0});
                    if (nl == std::string::npos) { break; }
                    items_.push_back({text_item::separator, "\n", 0});
                    at = nl + 1;
                }
            } else {
                items_.push_back({text_item::collapsible, std::move(text), 0});
            }
            return;
        }
        if (kind != node_kind::element) { return; }
        std::string display = display_of(node);
        if (display == "none") { return; }
        if (blockify && display != "contents" && !display.starts_with("table-")) {
            display = "block";
        }
        const bool html = is_html(node);
        const std::string_view tag = tag_of(node);
        if (html && tag == "br") {
            items_.push_back({text_item::separator, "\n", 0});
            return;
        }
        const text_style style = style_under(node, inherited);
        // "If node's computed value of visibility is not visible, then return
        // items": the children's text, and none of the breaks or tabs this
        // element's own box would have added.
        if (style.hidden) {
            collect_children(node, style, display);
            return;
        }
        // "If node is a p element, append 2", by tag whatever its display (WPT
        // checks the blank lines around a `display: inline-block` <p>).
        // Otherwise a block-level box is a line of its own.
        const int breaks =
            html && tag == "p"                                                                ? 2
            : lists_token("block table flex grid list-item flow-root table-caption", display) ? 1
                                                                                              : 0;
        const bool atomic = breaks == 0 && (display.starts_with("inline-") ||
                                            (html && lists_token(replaced_tags, tag)));
        if (breaks > 0) { items_.push_back({text_item::required, {}, breaks}); }
        if (atomic) { items_.push_back({text_item::open, {}, 0}); }
        collect_children(node, style, display);
        if (display == "table-cell" && !last_of_kind(node, "table-cell")) {
            items_.push_back({text_item::separator, "\t", 0});
        }
        if (display == "table-row" && !last_row(node)) {
            items_.push_back({text_item::separator, "\n", 0});
        }
        if (atomic) { items_.push_back({text_item::close, {}, 0}); }
        if (breaks > 0) { items_.push_back({text_item::required, {}, breaks}); }
    }

    /// True when no later sibling element has this display. The specification
    /// asks about boxes ("the last table-cell box of its enclosing table-row
    /// box"), and a sibling in the same row is that box.
    [[nodiscard]] bool last_of_kind(node_id node, std::string_view display) const {
        bool after = false;
        for (const node_id sibling : txn_.children(txn_.parent(node))) {
            if (sibling == node) {
                after = true;
                continue;
            }
            if (after && txn_.kind(sibling).value_or(node_kind::text) == node_kind::element &&
                display_of(sibling) == display) {
                return false;
            }
        }
        return true;
    }
    /// The same for a table row, whose table is one level further up: the
    /// rows of a later row group are still rows of this table.
    [[nodiscard]] bool last_row(node_id node) const {
        if (!last_of_kind(node, "table-row")) { return false; }
        const node_id group = txn_.parent(node);
        if (!group || display_of(group) != "table-row-group") { return true; }
        bool after = false;
        for (const node_id sibling : txn_.children(txn_.parent(group))) {
            if (sibling == group) {
                after = true;
                continue;
            }
            if (!after || txn_.kind(sibling).value_or(node_kind::text) != node_kind::element) {
                continue;
            }
            for (const node_id row : txn_.children(sibling)) {
                if (txn_.kind(row).value_or(node_kind::text) == node_kind::element &&
                    display_of(row) == "table-row") {
                    return false;
                }
            }
        }
        return true;
    }

    const read_txn & txn_;
    atom_table & atoms_;
    const style::style_map * styles_;
    atom style_;
    atom type_;
    atom hidden_;
    atom open_;
    std::vector<text_item> items_;
};

} // namespace

namespace detail {
/// True when `id` or an ancestor is `display: none` (see
/// inner_text_collector::unrendered).
bool element_unrendered(const read_txn & txn, atom_table & atoms, const style::style_map * styles,
                        node_id id) {
    return inner_text_collector{txn, atoms, styles}.unrendered(id);
}
} // namespace detail

void dom_bindings::install_rendered_text(context & cx) {
    // --- innerText and outerText setters -------------------------------------
    //
    // `el.innerText = "a\nb"` is not `textContent = "a\nb"`: the newline becomes
    // a <br> element and the text on either side of it a Text node. That rule,
    // "the rendered text fragment", is the whole of both setters and reads no
    // layout, which is why the two halves of this property can be separated.
    //
    // The getter reads no layout either; see inner_text_collector above.
    if (const value html_interface = interface_prototype("HTMLElement");
        html_interface.is_object()) {
        auto * proto = static_cast<script::object_object *>(html_interface.as_heap());
        // Installed on HTMLElement, not Element: `svg.innerText = "abc"` must
        // leave the <svg> empty, and WPT innertext-setter-tests.js checks a
        // MathML element as well.
        //
        // [LegacyNullToEmptyString], so `null` clears the element and
        // `undefined` writes the string "undefined", the same asymmetry
        // `CharacterData.data` has.
        const auto assigned = [](context & c, std::span<value> args) {
            return arg(args, 0).is_null() ? std::string{} : arg_string(c, args, 0);
        };
        // The fragment, as a list of nodes in document order. Built before
        // anything is removed: these calls only make nodes, and a fragment that
        // failed to build should not have emptied the element on its way out.
        const auto rendered_nodes = [this](std::string_view text) {
            std::vector<node_id> made;
            each_rendered_text_part(
                text,
                [&](std::string_view run) {
                    if (const node_id node = doc_->create_text(run)) { made.push_back(node); }
                },
                [&] {
                    if (const node_id node = doc_->create_element(atoms_->intern_lower("br"))) {
                        made.push_back(node);
                    }
                });
            return made;
        };
        // "Merge with the next text node": a Text node followed by a Text node
        // becomes one, and nothing else is normalised. outerText leaves
        // `A|B|Replaced|D|E` as `A|BReplacedD|E` on purpose (the WPT subtest
        // "does not completely normalize").
        const auto merge_forward = [this](node_id node) {
            if (!node) { return; }
            std::string joined;
            node_id next;
            {
                const auto txn = doc_->read();
                if (txn.kind(node) != node_kind::text) { return; }
                const node_id parent = txn.parent(node);
                if (!parent) { return; }
                const std::span<const node_id> kids = txn.children(parent);
                for (std::size_t i = 0; i + 1 < kids.size(); ++i) {
                    if (kids[i] == node) {
                        next = kids[i + 1];
                        break;
                    }
                }
                if (!next || txn.kind(next) != node_kind::text) { return; }
                joined = std::string{txn.text(node)} + std::string{txn.text(next)};
            }
            (void)doc_->set_text(node, joined);
            (void)doc_->remove_child(next);
        };
        const auto set_inner_text = [this, assigned, rendered_nodes](context & c,
                                                                     std::span<value> args) {
            const node_id id = receiver(c);
            if (!id) { return value::undefined(); }
            const std::vector<node_id> made = rendered_nodes(assigned(c, args));
            // "Replace all with fragment within this."
            std::vector<node_id> existing;
            {
                const auto txn = doc_->read();
                for (const node_id child : txn.children(id)) { existing.push_back(child); }
            }
            for (const node_id child : existing) { (void)doc_->remove_child(child); }
            for (const node_id child : made) { (void)doc_->append_child(id, child); }
            mutated();
            return value::undefined();
        };
        const auto set_outer_text = [this, assigned, rendered_nodes,
                                     merge_forward](context & c, std::span<value> args) {
            const node_id id = receiver(c);
            if (!id) { return value::undefined(); }
            node_id parent;
            node_id next;
            node_id previous;
            {
                const auto txn = doc_->read();
                parent = txn.parent(id);
                if (parent) {
                    const std::span<const node_id> kids = txn.children(parent);
                    for (std::size_t i = 0; i < kids.size(); ++i) {
                        if (kids[i] != id) { continue; }
                        if (i + 1 < kids.size()) { next = kids[i + 1]; }
                        if (i > 0) { previous = kids[i - 1]; }
                        break;
                    }
                }
            }
            // "If this's parent is null, then throw a
            // NoModificationAllowedError": the one way either setter can fail,
            // and the reason outerText needs a body of its own.
            if (!parent) {
                throw_dom_exception(c, "NoModificationAllowedError",
                                    "outerText: the element has no parent to replace it in");
                return value::undefined();
            }
            std::vector<node_id> made = rendered_nodes(assigned(c, args));
            // "If fragment has no children, append a new Text node whose data
            // is the empty string": `el.outerText = ""` replaces the element
            // with an empty text node rather than removing it, which is what
            // lets the merge below join the text on either side of it.
            if (made.empty()) {
                if (const node_id empty = doc_->create_text("")) { made.push_back(empty); }
            }
            for (const node_id node : made) { (void)doc_->insert_before(parent, node, id); }
            (void)doc_->remove_child(id);
            // The two merges the specification names, in its order: the node
            // now in front of `next` first, then `previous`.
            if (!made.empty() && next) { merge_forward(made.back()); }
            merge_forward(previous);
            mutated();
            return value::undefined();
        };
        // The getter, shared by both names: `outerText` reads exactly what
        // `innerText` reads. "If this is not being rendered, return this's
        // descendant text content"; an element that is not connected, or is
        // under a `display: none`, is not rendered. See inner_text_collector.
        const auto rendered_text = [this](context & c, std::span<value>) {
            const node_id id = receiver(c);
            if (!id) { return value::undefined(); }
            bool not_rendered = false;
            std::string text;
            // The cascade as the script left the tree, so a class from the
            // page's own stylesheet counts; `styles_` stays null for a document
            // nothing lays out, and the collector reads inline style then.
            flush_layout();
            {
                const auto txn = doc_->read();
                inner_text_collector collector{txn, *atoms_, styles_};
                if (!is_document_root(txn, root_of_tree(txn, id, true)) ||
                    collector.unrendered(id)) {
                    not_rendered = true;
                } else {
                    collector.collect_children(id, collector.inherited_style(id),
                                               collector.display_of(id));
                    text = collector.finish();
                }
            }
            // Outside the read above: text_content opens one of its own.
            return c.string(not_rendered ? text_content(id) : text);
        };
        proto->define_accessor(
            "innerText",
            value::object(cx.allocate<script::native_object>("innerText", rendered_text)),
            value::object(cx.allocate<script::native_object>("innerText", set_inner_text)));
        proto->define_accessor(
            "outerText",
            value::object(cx.allocate<script::native_object>("outerText", rendered_text)),
            value::object(cx.allocate<script::native_object>("outerText", set_outer_text)));

        // --- translate, HTML 3.2.6.3 ---------------------------------------
        //
        // A boolean over an inherited enumerated attribute: `yes` and "" are
        // translate-enabled, `no` is no-translate, and anything else (absent
        // included) is the "inherit" state, the parent element's mode. The
        // walk stops at the first node that is not an element, so a child of a
        // DocumentFragment or ShadowRoot is translate-enabled whatever the
        // host says (WPT translate-inherit-no-parent-element.html). Only an
        // HTML element's attribute counts; a foreign element is in the inherit
        // state and passes the question up.
        proto->define_accessor(
            "translate",
            value::object(cx.allocate<script::native_object>(
                "translate",
                [this](context & c, std::span<value>) {
                    const auto txn = doc_->read();
                    const atom name = atoms_->intern("translate");
                    for (node_id at = receiver(c);
                         at && txn.kind(at).value_or(node_kind::text) == node_kind::element;
                         at = txn.parent(at)) {
                        if (txn.element_ns(at) != node_ns::html || !txn.has_attribute(at, name)) {
                            continue;
                        }
                        const std::string mode = ascii_lower_copy(txn.attribute_value(at, name));
                        if (mode.empty() || mode == "yes") { return value::boolean(true); }
                        if (mode == "no") { return value::boolean(false); }
                    }
                    return value::boolean(true);
                })),
            value::object(cx.allocate<script::native_object>(
                "translate", [this](context & c, std::span<value> a) {
                    if (const node_id id = receiver(c)) {
                        (void)doc_->set_attribute(id, atoms_->intern("translate"),
                                                  !a.empty() && context::truthy(a[0]) ? "yes"
                                                                                      : "no");
                        mutated();
                    }
                    return value::undefined();
                })));

        // --- draggable (6.11.8) and spellcheck (6.8.4) ---------------------
        //
        // Two more booleans over enumerated attributes whose keywords are
        // `true` and `false` (ASCII case-insensitive; spellcheck's "" is
        // true too), written back as those words. They differ only in the
        // auto state: draggable is true for an <img> and for an <a> with an
        // href, false otherwise; spellcheck inherits by default (the parent
        // element's answer, true at the top).
        const auto keyword_state = [this](const read_txn & txn, node_id at,
                                          std::string_view name) -> int {
            if (txn.element_ns(at) != node_ns::html) { return -1; }
            const atom attr = atoms_->intern(name);
            if (!txn.has_attribute(at, attr)) { return -1; }
            const std::string word = ascii_lower_copy(txn.attribute_value(at, attr));
            if (word == "true" || (word.empty() && name == "spellcheck")) { return 1; }
            if (word == "false") { return 0; }
            return -1;
        };
        const auto boolean_writer = [this, &cx](const char * name) {
            return value::object(cx.allocate<script::native_object>(
                name, [this, name](context & c, std::span<value> a) {
                    if (const node_id id = receiver(c)) {
                        (void)doc_->set_attribute(id, atoms_->intern(name),
                                                  !a.empty() && context::truthy(a[0]) ? "true"
                                                                                      : "false");
                        mutated();
                    }
                    return value::undefined();
                }));
        };
        // --- contentEditable and isContentEditable (6.8.1) -----------------
        //
        // The enumerated attribute's state as a keyword ("true" for "" too,
        // "plaintext-only", "false", or "inherit" for absent and invalid),
        // and the setter the other way: "inherit" removes the attribute,
        // the three keywords are written lowercased, anything else is a
        // SyntaxError. isContentEditable walks up to the nearest element
        // whose attribute decides (designMode is not modelled).
        const auto editable_state = [this](const read_txn & txn, node_id at) -> std::string_view {
            const atom attr = atoms_->intern("contenteditable");
            if (txn.element_ns(at) != node_ns::html || !txn.has_attribute(at, attr)) {
                return "inherit";
            }
            const std::string word = ascii_lower_copy(txn.attribute_value(at, attr));
            if (word.empty() || word == "true") { return "true"; }
            if (word == "plaintext-only") { return "plaintext-only"; }
            if (word == "false") { return "false"; }
            return "inherit";
        };
        proto->define_accessor(
            "contentEditable",
            value::object(cx.allocate<script::native_object>(
                "contentEditable",
                [this, editable_state](context & c, std::span<value>) {
                    const node_id id = receiver(c);
                    if (!id) { return c.string("inherit"); }
                    const auto txn = doc_->read();
                    return c.string(std::string{editable_state(txn, id)});
                })),
            value::object(cx.allocate<script::native_object>(
                "contentEditable", [this](context & c, std::span<value> a) {
                    const node_id id = receiver(c);
                    if (!id) { return value::undefined(); }
                    const std::string word = ascii_lower_copy(arg_string(c, a, 0));
                    const atom attr = atoms_->intern("contenteditable");
                    if (word == "inherit") {
                        (void)doc_->remove_attribute(id, attr);
                    } else if (word == "true" || word == "false" || word == "plaintext-only") {
                        (void)doc_->set_attribute(id, attr, word);
                    } else {
                        throw_dom_exception(c, "SyntaxError",
                                            "contentEditable: not true, false, plaintext-only "
                                            "or inherit");
                        return value::undefined();
                    }
                    mutated();
                    return value::undefined();
                })));
        proto->define_accessor("isContentEditable",
                               value::object(cx.allocate<script::native_object>(
                                   "isContentEditable",
                                   [this, editable_state](context & c, std::span<value>) {
                                       const auto txn = doc_->read();
                                       for (node_id at = receiver(c);
                                            at && txn.kind(at).value_or(node_kind::text) ==
                                                      node_kind::element;
                                            at = txn.parent(at)) {
                                           const std::string_view state = editable_state(txn, at);
                                           if (state != "inherit") {
                                               return value::boolean(state != "false");
                                           }
                                       }
                                       return value::boolean(false);
                                   })),
                               value::undefined());
        proto->define_accessor(
            "draggable",
            value::object(cx.allocate<script::native_object>(
                "draggable",
                [this, keyword_state](context & c, std::span<value>) {
                    const node_id id = receiver(c);
                    if (!id) { return value::boolean(false); }
                    const auto txn = doc_->read();
                    if (const int state = keyword_state(txn, id, "draggable"); state >= 0) {
                        return value::boolean(state == 1);
                    }
                    const std::string_view local = txn.local_name(id);
                    return value::boolean(
                        txn.element_ns(id) == node_ns::html &&
                        (local == "img" ||
                         (local == "a" && txn.has_attribute(id, atoms_->intern("href")))));
                })),
            boolean_writer("draggable"));
        proto->define_accessor(
            "spellcheck",
            value::object(cx.allocate<script::native_object>(
                "spellcheck",
                [this, keyword_state](context & c, std::span<value>) {
                    const auto txn = doc_->read();
                    for (node_id at = receiver(c);
                         at && txn.kind(at).value_or(node_kind::text) == node_kind::element;
                         at = txn.parent(at)) {
                        if (const int state = keyword_state(txn, at, "spellcheck"); state >= 0) {
                            return value::boolean(state == 1);
                        }
                    }
                    return value::boolean(true);
                })),
            boolean_writer("spellcheck"));
    }
}

} // namespace ctbrowser::shell
