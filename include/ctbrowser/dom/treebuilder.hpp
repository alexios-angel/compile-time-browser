#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/dom/document.hpp>
#include <ctbrowser/dom/node.hpp>
#include <ctbrowser/dom/tokenizer.hpp>

// Tree construction: tokens into a DOM.
//
// This is the half that makes HTML parsing famously strange, and every strange
// part of it is a rule some page depends on. `<p>a<p>b` is two paragraphs
// because a <p> start tag closes an open <p>. `<table><td>` grows a <tbody> and
// a <tr> that the author never wrote. `<b><i>x</b>y</i>` produces overlapping
// formatting that no tree can express, so the spec has an algorithm - the
// adoption agency - for reconstructing it the way every browser does.
//
// A parser that "handles a practical subset" gets all three of those wrong in
// ways that look reasonable until you compare against a browser. Implementing
// the spec is the only way to agree with one.
//
// WHAT IS HERE: insertion modes initial, before html, before head, in head,
// after head, in body, in table (with foster parenting), in text, plus the
// stack of open elements, implied end tags, the list of active formatting
// elements and the adoption agency algorithm.
//
// WHAT IS NOT: MathML, templates, forms' special ownership rules, and the
// after-body/after-frameset tail modes. Those are named here rather than
// silently missing.
//
// SVG is a case of its own and NOT an insertion mode. An <svg> element is built
// normally, but its subtree is CAPTURED AS SOURCE rather than parsed - see
// dom/tokenizer.hpp for the reasoning, which comes down to this tokenizer
// lowercasing `viewBox` into a `viewbox` no SVG parser reads. The element is in
// the tree; its shapes are not, and `foreign_sources()` hands the exact bytes
// to whatever rasterises them.
//
// That also settles three things that used to leak, because there is no longer
// anything inside an <svg> for them to find: an SVG <title> is not the window
// title, an SVG <style> is not a page stylesheet, and an SVG <text> is not
// document text.

namespace ctbrowser::html {

// Elements that never have children and close themselves.
[[nodiscard]] inline bool is_void_element(std::string_view tag) {
    constexpr std::string_view names[] = {"area",  "base",   "br",    "col",  "embed",
                                          "hr",    "img",    "input", "link", "meta",
                                          "param", "source", "track", "wbr"};
    return std::ranges::find(names, tag) != std::ranges::end(names);
}

// The elements a <p> or an <li> is implicitly closed by, and the ones that
// close themselves when the same tag opens again.
[[nodiscard]] inline bool closes_open_paragraph(std::string_view tag) {
    constexpr std::string_view names[] = {
        "address",  "article",    "aside",  "blockquote", "details", "div",    "dl",
        "fieldset", "figcaption", "figure", "footer",     "form",    "h1",     "h2",
        "h3",       "h4",         "h5",     "h6",         "header",  "hgroup", "hr",
        "main",     "menu",       "nav",    "ol",         "p",       "pre",    "section",
        "summary",  "table",      "ul",     "li",         "dd",      "dt"};
    return std::ranges::find(names, tag) != std::ranges::end(names);
}

// Formatting elements get the adoption agency treatment.
[[nodiscard]] inline bool is_formatting_element(std::string_view tag) {
    constexpr std::string_view names[] = {"a",    "b", "big",   "code",   "em",     "font", "i",
                                          "nobr", "s", "small", "strike", "strong", "tt",   "u"};
    return std::ranges::find(names, tag) != std::ranges::end(names);
}

// "Special" in the spec's sense: these break out of formatting scope.
[[nodiscard]] inline bool is_special_element(std::string_view tag) {
    constexpr std::string_view names[] = {
        "address",  "applet",     "area",     "article",    "aside",    "base",     "basefont",
        "bgsound",  "blockquote", "body",     "br",         "button",   "caption",  "center",
        "col",      "colgroup",   "dd",       "details",    "dir",      "div",      "dl",
        "dt",       "embed",      "fieldset", "figcaption", "figure",   "footer",   "form",
        "frame",    "frameset",   "h1",       "h2",         "h3",       "h4",       "h5",
        "h6",       "head",       "header",   "hgroup",     "hr",       "html",     "iframe",
        "img",      "input",      "li",       "link",       "listing",  "main",     "marquee",
        "menu",     "meta",       "nav",      "noembed",    "noframes", "noscript", "object",
        "ol",       "p",          "param",    "plaintext",  "pre",      "script",   "section",
        "select",   "source",     "style",    "summary",    "table",    "tbody",    "td",
        "textarea", "tfoot",      "th",       "thead",      "title",    "tr",       "track",
        "ul",       "wbr",        "xmp"};
    return std::ranges::find(names, tag) != std::ranges::end(names);
}

// The content model a start tag switches the tokenizer into.
[[nodiscard]] inline content_model content_model_for(std::string_view tag) {
    if (tag == "title" || tag == "textarea") { return content_model::rcdata; }
    if (tag == "style" || tag == "xmp" || tag == "iframe" || tag == "noembed" ||
        tag == "noframes") {
        return content_model::rawtext;
    }
    if (tag == "script") { return content_model::script; }
    if (tag == "plaintext") { return content_model::plaintext; }
    return content_model::data;
}

// --- foreign content ------------------------------------------------------

// SVG elements whose CHILDREN are HTML again. `<foreignObject>` is the whole
// reason SVG has these - it exists to embed a fragment of HTML inside a
// graphic - and `<desc>` and `<title>` take flowing HTML for accessibility.
//
// Case-sensitive, because by this point names are not folded: `foreignObject`
// with a lowercase o is a different element and not an integration point.
[[nodiscard]] inline bool is_html_integration_point(std::string_view tag) {
    return tag == "foreignObject" || tag == "desc" || tag == "title";
}

// HTML start tags that BREAK OUT of foreign content: seeing one closes the SVG
// rather than nesting inside it. The spec's list, and it exists because pages
// forget `</svg>` - without it, a paragraph after an unclosed graphic would
// become part of the graphic and vanish.
[[nodiscard]] inline bool breaks_out_of_foreign_content(std::string_view tag) {
    constexpr std::string_view names[] = {
        "b",      "big",  "blockquote", "body",  "br",   "center", "code",    "dd",   "div",
        "dl",     "dt",   "em",         "embed", "h1",   "h2",     "h3",      "h4",   "h5",
        "h6",     "head", "hr",         "i",     "img",  "li",     "listing", "menu", "meta",
        "nobr",   "ol",   "p",          "pre",   "ruby", "s",      "small",   "span", "strong",
        "strike", "sub",  "sup",        "table", "tt",   "u",      "ul",      "var"};
    return std::ranges::find(names, tag) != std::ranges::end(names);
}

class tree_builder {
public:
    tree_builder(document & doc, atom_table & atoms) : doc_(&doc), atoms_(&atoms) {}

    [[nodiscard]] node_id parse(std::string_view input);

    // The verbatim source of each <svg> in the document, by the element it
    // belongs to. Populated during parse and read straight after; see the
    // tokenizer header for why the ORIGINAL BYTES rather than the parsed tree.
    [[nodiscard]] const std::vector<std::pair<node_id, std::string>> & foreign_sources()
        const noexcept {
        return foreign_sources_;
    }

private:
    struct entry {
        node_id id;
        std::string tag;
        node_ns ns = node_ns::html;
    };
    // A formatting element remembered so it can be reconstructed. `marker`
    // entries are scope boundaries (a cell, a caption) that reconstruction
    // stops at.
    struct formatting {
        node_id id;
        std::string tag;
        std::vector<token_attribute> attributes;
        bool marker = false;
    };

    void handle(const token & t, tokenizer & lexer);

    // --- insertion --------------------------------------------------------

    [[nodiscard]] node_id current() const { return open_.empty() ? root_ : open_.back().id; }
    [[nodiscard]] std::string_view current_tag() const;

    void insert_text(const std::string & text);

    [[nodiscard]] static bool is_text_content_element(std::string_view tag);

    // Foster parenting: text and most elements inside a <table> but outside a
    // cell go BEFORE the table, not into it. Pages rely on this - it is what
    // stops stray whitespace in a table from breaking the layout.
    struct insertion_point {
        node_id parent;
        node_id before; // empty = append
    };

    [[nodiscard]] insertion_point where_to_insert() const;

    void insert_at(const insertion_point & where, node_id child);
    [[nodiscard]] bool foster_parenting() const;

    // Table structure belongs INSIDE the table; everything else that turns up
    // there is fostered out. Fostering the sections too is what produced a flat
    // row of table/tbody/tr/td siblings instead of a table.
    [[nodiscard]] static bool is_table_structure(std::string_view tag);

    [[nodiscard]] node_id insert_element(const std::string & tag,
                                         const std::vector<token_attribute> & attributes,
                                         node_ns ns = node_ns::html) {
        // intern vs intern_lower, and BOTH CALLS MATTER. Doing only the tag is
        // the natural half-implementation: `linearGradient` survives while
        // every attribute on it is still folded, so `viewBox` and
        // `gradientUnits` are gone and the graphic is subtly wrong rather than
        // visibly broken. The tokenizer already preserved the case; this is
        // where it would be thrown away again.
        const bool foreign = ns == node_ns::svg;
        const node_id element =
            builder_->create_element(foreign ? atoms_->intern(tag) : atoms_->intern_lower(tag), ns);
        for (const token_attribute & a : attributes) {
            builder_->set_attribute(
                element, foreign ? atoms_->intern(a.name) : atoms_->intern_lower(a.name), a.value);
        }
        if (is_table_structure(tag)) {
            builder_->append(current(), element);
        } else {
            insert_at(where_to_insert(), element);
        }
        return element;
    }

    // --- start tags -------------------------------------------------------

    void start(const token & t, tokenizer & lexer);

    [[nodiscard]] static bool is_heading(std::string_view tag);
    [[nodiscard]] static bool is_head_only(std::string_view tag);

    void ensure_head();

    void ensure_body();

    void implicit(const std::string & tag);

    // --- end tags ---------------------------------------------------------

    void end(const token & t, tokenizer & lexer);

    // Pop to and including the nearest matching element. A close tag with no
    // match is IGNORED rather than unwinding the stack - which is what stops
    // one stray </div> from closing the whole document.
    void close_element(const std::string & tag);

    void pop();

    void close_list_item(const std::string & tag);
    void close_definition_item();

    void close_cell();
    void close_row();

    // --- scopes -----------------------------------------------------------

    [[nodiscard]] bool has_in_button_scope(std::string_view tag) const;

    // --- formatting elements ----------------------------------------------

    // Re-open formatting elements that are still active but no longer on the
    // stack. This is what makes `<b>one<p>two</p></b>` bold the second
    // paragraph too: the <b> was closed by the <p>, and gets recreated inside.
    void reconstruct_formatting();

    [[nodiscard]] bool on_stack(node_id id) const;

    // The adoption agency algorithm, in the shape that matters for a tree that
    // is BUILT ONCE rather than mutated: `<b><i>x</b>y</i>` cannot be a tree, so
    // the spec closes the <b>, leaves the <i> open, and reconstructs it around
    // the following text. Returns true when it handled the tag.
    //
    // The full algorithm reparents already-inserted nodes; this one does not,
    // because document::builder appends and never moves. The difference shows
    // on `<b>1<p>2</b>3</p>`, where the spec moves the <b> into the <p>. What
    // this gets right is the common case - overlapping inline formatting - and
    // the difference is recorded rather than hidden.
    [[nodiscard]] bool adoption_agency(const std::string & tag);

    document * doc_;
    atom_table * atoms_;
    document::builder * builder_ = nullptr;
    node_id root_;
    node_id head_;
    node_id body_;
    bool in_body_ = false;
    std::vector<entry> open_;
    std::vector<formatting> active_;

    // Foreign content, captured as source. `input_` is the whole document, so
    // slicing it is free; `foreign_begin_` is where the open <svg> started and
    // is only meaningful while `foreign_node_` is set.
    std::string_view input_;
    std::vector<std::pair<node_id, std::string>> foreign_sources_;
    node_id foreign_node_;
    std::size_t foreign_begin_ = 0;
    // Nesting, because an SVG may contain another one and the OUTERMOST close
    // tag is the one that ends the capture. Without this, `<svg>...<svg/>...`
    // would file a truncated graphic.
    int foreign_depth_ = 0;

    // begin/end of one captured subtree. Split out because the end also has to
    // run at EOF: an unclosed <svg> still has to reach the rasteriser, which
    // renders what it can, rather than being dropped for being malformed.
    void open_foreign(const token & t);
    void close_foreign(std::size_t source_end);

    // Whether a new element lands in the SVG vocabulary. True inside an <svg>
    // EXCEPT immediately inside an HTML integration point, which is what makes
    // `<foreignObject><div>` a real HTML div.
    [[nodiscard]] bool in_foreign_content() const;

    // Tell the tokenizer whether to keep case. Called wherever the open stack
    // changes, because that is what decides the answer.
    void sync_foreign(tokenizer & lexer) const;
};

} // namespace ctbrowser::html
