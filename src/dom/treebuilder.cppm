module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module ctbrowser.dom:treebuilder;

import ctbrowser.core;
import :document;
import :node;
import :tokenizer;

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
// WHAT IS NOT: foreign content (SVG and MathML), templates, forms' special
// ownership rules, and the after-body/after-frameset tail modes. Those are
// named here rather than silently missing - a <svg> subtree parses as ordinary
// HTML elements, which is wrong but predictable, and is the next thing to do.

export namespace ctbrowser::html {

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

class tree_builder {
public:
    tree_builder(document & doc, atom_table & atoms) : doc_(&doc), atoms_(&atoms) {}

    [[nodiscard]] node_id parse(std::string_view input) {
        document::builder builder = doc_->build();
        builder_ = &builder;

        root_ = builder.create_element(atoms_->intern_lower("html"));
        builder.set_root(root_);
        open_.push_back(entry{root_, "html"});

        tokenizer lexer{input};
        while (true) {
            const token t = lexer.next();
            if (t.kind == token_kind::end_of_file) { break; }
            handle(t, lexer);
        }
        builder_ = nullptr;
        return root_;
    }

private:
    struct entry {
        node_id id;
        std::string tag;
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

    void handle(const token & t, tokenizer & lexer) {
        switch (t.kind) {
        case token_kind::doctype: return; // nothing downstream renders differently
        case token_kind::comment: return; // dropped: nothing reads comments yet
        case token_kind::character: return insert_text(t.data);
        case token_kind::start_tag: return start(t, lexer);
        case token_kind::end_tag: return end(t.name);
        case token_kind::end_of_file: return;
        }
    }

    // --- insertion --------------------------------------------------------

    [[nodiscard]] node_id current() const { return open_.empty() ? root_ : open_.back().id; }
    [[nodiscard]] std::string_view current_tag() const {
        return open_.empty() ? std::string_view{"html"} : std::string_view{open_.back().tag};
    }

    void insert_text(const std::string & text) {
        if (text.empty()) { return; }
        // Text inside a text-content element belongs to it, wherever it is.
        // Forcing <body> here is what put a <title>'s text into the body and
        // left the title empty.
        if (is_text_content_element(current_tag())) {
            builder_->append(current(), builder_->create_text(text));
            return;
        }
        // Text before <body> that is only whitespace is dropped; text with
        // content forces <body> open, which is how a document that starts with
        // bare text still renders.
        if (!in_body_ && text.find_first_not_of(" \t\n\r\f") == std::string::npos) { return; }
        ensure_body();
        reconstruct_formatting();
        insert_at(where_to_insert(), builder_->create_text(text));
    }

    [[nodiscard]] static bool is_text_content_element(std::string_view tag) {
        return content_model_for(tag) != content_model::data;
    }

    // Foster parenting: text and most elements inside a <table> but outside a
    // cell go BEFORE the table, not into it. Pages rely on this - it is what
    // stops stray whitespace in a table from breaking the layout.
    struct insertion_point {
        node_id parent;
        node_id before; // empty = append
    };

    [[nodiscard]] insertion_point where_to_insert() const {
        if (!foster_parenting()) {
            return insertion_point{current(), node_id{}};
        }
        for (std::size_t i = open_.size(); i-- > 0;) {
            if (open_[i].tag == "table") {
                return insertion_point{i > 0 ? open_[i - 1].id : root_, open_[i].id};
            }
        }
        return insertion_point{current(), node_id{}};
    }

    void insert_at(const insertion_point & where, node_id child) {
        if (where.before) {
            builder_->insert_before(where.parent, child, where.before);
        } else {
            builder_->append(where.parent, child);
        }
    }
    [[nodiscard]] bool foster_parenting() const {
        const std::string_view tag = current_tag();
        return tag == "table" || tag == "tbody" || tag == "thead" || tag == "tfoot" || tag == "tr";
    }

    // Table structure belongs INSIDE the table; everything else that turns up
    // there is fostered out. Fostering the sections too is what produced a flat
    // row of table/tbody/tr/td siblings instead of a table.
    [[nodiscard]] static bool is_table_structure(std::string_view tag) {
        constexpr std::string_view names[] = {"caption", "colgroup", "col",   "tbody", "td",
                                              "tfoot",   "th",       "thead", "tr"};
        return std::ranges::find(names, tag) != std::ranges::end(names);
    }

    [[nodiscard]] node_id insert_element(const std::string & tag,
                                         const std::vector<token_attribute> & attributes) {
        const node_id element = builder_->create_element(atoms_->intern_lower(tag));
        for (const token_attribute & a : attributes) {
            builder_->set_attribute(element, atoms_->intern_lower(a.name), a.value);
        }
        if (is_table_structure(tag)) {
            builder_->append(current(), element);
        } else {
            insert_at(where_to_insert(), element);
        }
        return element;
    }

    // --- start tags -------------------------------------------------------

    void start(const token & t, tokenizer & lexer) {
        const std::string & tag = t.name;

        if (tag == "html") { return; } // already open; attributes merge in the spec
        if (tag == "head") {
            if (head_) { return; }
            ensure_head();
            return;
        }
        if (tag == "body") {
            ensure_body();
            return;
        }

        if (is_head_only(tag) && !in_body_) {
            ensure_head();
        } else {
            ensure_body();
        }

        // A <p> is closed by any block-level start tag. This is the rule that
        // makes `<p>one<p>two` two paragraphs instead of nested ones, and every
        // page written before XHTML depends on it.
        if (closes_open_paragraph(tag) && has_in_button_scope("p")) { close_element("p"); }
        // <li>, <dd> and <dt> close the previous one of their kind.
        if (tag == "li") { close_list_item("li"); }
        // <dt> and <dd> close EACH OTHER, not just themselves - a definition
        // list alternates them and almost never closes either.
        if (tag == "dd" || tag == "dt") { close_definition_item(); }
        // Headings do not nest.
        if (is_heading(tag) && is_heading(current_tag())) { pop(); }
        // A table cell or row implies closing the previous one.
        if (tag == "td" || tag == "th") { close_cell(); }
        if (tag == "tr") { close_row(); }
        // <table> implies the sections a page usually omits, so a <tr> written
        // straight inside a <table> lands in a <tbody> like a browser makes.
        if ((tag == "tr") && current_tag() == "table") { implicit("tbody"); }
        if ((tag == "td" || tag == "th") && current_tag() != "tr") { implicit("tr"); }

        if (is_formatting_element(tag)) { reconstruct_formatting(); }

        const node_id element = insert_element(tag, t.attributes);

        if (is_void_element(tag) || t.self_closing) { return; } // no children, nothing to push
        open_.push_back(entry{element, tag});
        if (is_formatting_element(tag)) {
            active_.push_back(formatting{element, tag, t.attributes, false});
        }
        if (tag == "td" || tag == "th" || tag == "caption") {
            active_.push_back(formatting{node_id{}, {}, {}, true}); // scope marker
        }

        // Text-only elements switch the tokenizer, which is the only way
        // `<script>a < b</script>` tokenizes as text rather than as markup.
        if (const content_model model = content_model_for(tag); model != content_model::data) {
            lexer.set_content_model(model, tag);
        }
    }

    [[nodiscard]] static bool is_heading(std::string_view tag) {
        return tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6';
    }
    [[nodiscard]] static bool is_head_only(std::string_view tag) {
        constexpr std::string_view names[] = {"base",     "basefont", "bgsound", "link",
                                              "meta",     "noscript", "script",  "style",
                                              "template", "title"};
        return std::ranges::find(names, tag) != std::ranges::end(names);
    }

    void ensure_head() {
        if (head_) {
            if (current_tag() != "head") { open_.push_back(entry{head_, "head"}); }
            return;
        }
        head_ = builder_->create_element(atoms_->intern_lower("head"));
        builder_->append(root_, head_);
        open_.push_back(entry{head_, "head"});
    }

    void ensure_body() {
        if (in_body_) { return; }
        // Leaving <head> is what "after head" means; anything that is not
        // head-only content does it.
        while (open_.size() > 1 && open_.back().tag != "html") { open_.pop_back(); }
        body_ = builder_->create_element(atoms_->intern_lower("body"));
        builder_->append(root_, body_);
        open_.push_back(entry{body_, "body"});
        in_body_ = true;
    }

    void implicit(const std::string & tag) {
        const node_id element = insert_element(tag, {});
        open_.push_back(entry{element, tag});
    }

    // --- end tags ---------------------------------------------------------

    void end(const std::string & tag) {
        if (tag == "body" || tag == "html") {
            // Ignored: content after them is still content, which is what a
            // browser does with a stray </body> halfway down a page.
            return;
        }
        if (is_formatting_element(tag)) {
            if (adoption_agency(tag)) { return; }
        }
        if (tag == "p" && !has_in_button_scope("p")) {
            // </p> with no <p> open creates one, per the spec. Pages do this.
            ensure_body();
            const node_id element = insert_element("p", {});
            (void)element;
            return;
        }
        close_element(tag);
    }

    // Pop to and including the nearest matching element. A close tag with no
    // match is IGNORED rather than unwinding the stack - which is what stops
    // one stray </div> from closing the whole document.
    void close_element(const std::string & tag) {
        std::size_t found = open_.size();
        for (std::size_t i = open_.size(); i-- > 0;) {
            if (open_[i].tag == tag) {
                found = i;
                break;
            }
            // Do not unwind past a special element looking for a match - EXCEPT
            // through table structure when closing table structure. The
            // implied <tbody> is a special element, so `</table>` stopped at it
            // and never popped the table at all: everything after the table was
            // then foster-parented BEFORE it, which reversed two consecutive
            // tables and swallowed whatever followed them.
            // (is_table_structure excludes <table> itself - it is about what
            // belongs INSIDE one - so the target is checked for both.)
            const bool closing_table = tag == "table" || is_table_structure(tag);
            if (is_special_element(open_[i].tag) &&
                !(closing_table && is_table_structure(open_[i].tag))) {
                break;
            }
        }
        if (found == open_.size()) { return; }
        while (open_.size() > found) { pop(); }
    }

    void pop() {
        if (open_.size() <= 1) { return; } // never pop <html>
        const node_id gone = open_.back().id;
        open_.pop_back();
        std::erase_if(active_, [&](const formatting & f) { return !f.marker && f.id == gone; });
    }

    void close_list_item(const std::string & tag) {
        for (std::size_t i = open_.size(); i-- > 0;) {
            if (open_[i].tag == tag) {
                while (open_.size() > i) { pop(); }
                return;
            }
            if (is_special_element(open_[i].tag) && open_[i].tag != "div" && open_[i].tag != "p" &&
                open_[i].tag != "address") {
                return;
            }
        }
    }
    void close_definition_item() {
        for (std::size_t i = open_.size(); i-- > 0;) {
            if (open_[i].tag == "dd" || open_[i].tag == "dt") {
                while (open_.size() > i) { pop(); }
                return;
            }
            if (open_[i].tag == "dl") { return; }
            if (is_special_element(open_[i].tag) && open_[i].tag != "div" && open_[i].tag != "p") {
                return;
            }
        }
    }

    void close_cell() {
        for (std::size_t i = open_.size(); i-- > 0;) {
            if (open_[i].tag == "td" || open_[i].tag == "th") {
                while (open_.size() > i) { pop(); }
                return;
            }
            if (open_[i].tag == "tr" || open_[i].tag == "table") { return; }
        }
    }
    void close_row() {
        close_cell();
        for (std::size_t i = open_.size(); i-- > 0;) {
            if (open_[i].tag == "tr") {
                while (open_.size() > i) { pop(); }
                return;
            }
            if (open_[i].tag == "table") { return; }
        }
    }

    // --- scopes -----------------------------------------------------------

    [[nodiscard]] bool has_in_button_scope(std::string_view tag) const {
        for (std::size_t i = open_.size(); i-- > 0;) {
            if (open_[i].tag == tag) { return true; }
            if (open_[i].tag == "button" || open_[i].tag == "html" || open_[i].tag == "table" ||
                open_[i].tag == "td" || open_[i].tag == "th" || open_[i].tag == "caption") {
                return false;
            }
        }
        return false;
    }

    // --- formatting elements ----------------------------------------------

    // Re-open formatting elements that are still active but no longer on the
    // stack. This is what makes `<b>one<p>two</p></b>` bold the second
    // paragraph too: the <b> was closed by the <p>, and gets recreated inside.
    void reconstruct_formatting() {
        if (active_.empty()) { return; }
        std::size_t start = active_.size();
        while (start > 0) {
            const formatting & f = active_[start - 1];
            if (f.marker || on_stack(f.id)) { break; }
            --start;
        }
        for (std::size_t i = start; i < active_.size(); ++i) {
            formatting & f = active_[i];
            if (f.marker) { continue; }
            const node_id fresh = insert_element(f.tag, f.attributes);
            open_.push_back(entry{fresh, f.tag});
            f.id = fresh;
        }
    }

    [[nodiscard]] bool on_stack(node_id id) const {
        return std::ranges::any_of(open_, [&](const entry & e) { return e.id == id; });
    }

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
    [[nodiscard]] bool adoption_agency(const std::string & tag) {
        // Reverse search by index: <ranges> reverse views are not worth pulling
        // in for one lookup over a vector that is a handful of entries deep.
        std::size_t found = active_.size();
        for (std::size_t i = active_.size(); i-- > 0;) {
            if (!active_[i].marker && active_[i].tag == tag) {
                found = i;
                break;
            }
            if (active_[i].marker) { break; } // do not look past a scope boundary
        }
        if (found == active_.size()) { return false; }
        const formatting target = active_[found];
        if (!on_stack(target.id)) {
            // Active but already gone from the stack: just forget it.
            std::erase_if(active_,
                          [&](const formatting & f) { return !f.marker && f.id == target.id; });
            return true;
        }

        // Where the formatting element sits on the stack, and what block - if
        // any - was opened inside it and is still open.
        std::size_t at = open_.size();
        for (std::size_t i = open_.size(); i-- > 0;) {
            if (open_[i].id == target.id) {
                at = i;
                break;
            }
        }
        std::size_t furthest = open_.size();
        for (std::size_t i = at + 1; i < open_.size(); ++i) {
            if (is_special_element(open_[i].tag)) {
                furthest = i;
                break;
            }
        }

        if (furthest == open_.size()) {
            // Nothing block-level inside it. Pop back to and including it;
            // anything else that was open stays in the ACTIVE list but leaves
            // the stack, so the next insertion reconstructs it as a NEW element
            // in the new place. That is how `<b><i>x</b>y</i>` puts the y in its
            // own <i> outside the <b>, the way a browser shows it.
            while (!open_.empty() && open_.back().id != target.id) { open_.pop_back(); }
            if (!open_.empty()) { open_.pop_back(); }
            std::erase_if(active_,
                          [&](const formatting & f) { return !f.marker && f.id == target.id; });
            return true;
        }

        // THE reparenting case: `<b>1<p>2</b>3</p>`. A tree cannot have the <p>
        // both inside and outside the <b>, so the spec clones the formatting
        // element, moves the block's children into the clone, puts the clone in
        // the block, and lifts the block out to the formatting element's parent.
        // Browsers show <b>1</b><p><b>2</b>3</p>, and this is where that comes
        // from.
        const entry block = open_[furthest];
        const node_id common_ancestor = at > 0 ? open_[at - 1].id : root_;

        const node_id clone = builder_->create_element(atoms_->intern_lower(target.tag));
        for (const token_attribute & a : target.attributes) {
            builder_->set_attribute(clone, atoms_->intern_lower(a.name), a.value);
        }
        {
            // Snapshot first: reparenting edits the list being walked.
            std::vector<node_id> moving;
            const auto txn = doc_->read();
            for (const node_id child : txn.children(block.id)) { moving.push_back(child); }
            for (const node_id child : moving) { builder_->reparent(child, clone); }
        }
        builder_->append(block.id, clone);
        builder_->reparent(block.id, common_ancestor);

        // The formatting element is finished; the block is now the insertion
        // point, and the clone is what later text reconstructs into.
        open_.erase(open_.begin() + static_cast<std::ptrdiff_t>(at));
        std::erase_if(active_,
                      [&](const formatting & f) { return !f.marker && f.id == target.id; });
        return true;
    }

    document * doc_;
    atom_table * atoms_;
    document::builder * builder_ = nullptr;
    node_id root_;
    node_id head_;
    node_id body_;
    bool in_body_ = false;
    std::vector<entry> open_;
    std::vector<formatting> active_;
};

} // namespace ctbrowser::html
