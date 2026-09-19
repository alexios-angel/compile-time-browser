#include "internal.hpp"

namespace ctbrowser::html {

using namespace treebuilder_detail;

// 13.2.6.4.7 "in body"
void tree_builder::mode_in_body(const token & t) {
    switch (t.kind) {
    case token_kind::character: {
        // NUL characters are dropped; anything else is inserted, and a
        // non-whitespace character sets the frameset-ok flag to "not ok".
        std::string data;
        data.reserve(t.data.size());
        for (const char c : t.data) {
            if (c != '\0') { data += c; }
        }
        if (data.empty()) { return; }
        reconstruct_formatting();
        insert_text(data);
        if (!all_whitespace(data)) { frameset_ok_ = false; }
        return;
    }
    case token_kind::comment:
    case token_kind::processing_instruction: return insert_comment(t);
    case token_kind::doctype: return;
    case token_kind::start_tag: return in_body_start(t);
    case token_kind::end_tag: return in_body_end(t);
    case token_kind::end_of_file:
        if (!template_modes_.empty()) { return process(t, mode::in_template); }
        return; // stop parsing
    case token_kind::incomplete: return;
    }
}

void tree_builder::in_body_start(const token & t) {
    const std::string & tag = t.name;
    if (tag == "html") {
        if (template_on_stack()) { return; }
        merge_attributes(root_, t.attributes);
        return;
    }
    if (one_of(tag, {"base", "basefont", "bgsound", "link", "meta", "noframes", "script", "style",
                     "template", "title"})) {
        return process(t, mode::in_head);
    }
    if (tag == "body") {
        if (open_.size() < 2 || open_[1].tag != "body" || template_on_stack()) { return; }
        frameset_ok_ = false;
        merge_attributes(open_[1].id, t.attributes);
        return;
    }
    if (tag == "frameset") {
        if (open_.size() < 2 || open_[1].tag != "body" || !frameset_ok_) { return; }
        // The body element is removed from its parent and the stack is
        // popped down to the html element.
        (void)doc_->remove_child(open_[1].id);
        while (open_.size() > 1) { pop(); }
        (void)insert_element(t);
        mode_ = mode::in_frameset;
        return;
    }
    if (one_of(tag,
               {"address", "article", "aside",   "blockquote", "center",     "details", "dialog",
                "dir",     "div",     "dl",      "fieldset",   "figcaption", "figure",  "footer",
                "header",  "hgroup",  "main",    "menu",       "nav",        "ol",      "p",
                "search",  "section", "summary", "ul"})) {
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        (void)insert_element(t);
        return;
    }
    if (is_heading(tag)) {
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        if (!open_.empty() && open_.back().ns == node_ns::html && is_heading(open_.back().tag)) {
            pop();
        }
        (void)insert_element(t);
        return;
    }
    if (one_of(tag, {"pre", "listing"})) {
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        (void)insert_element(t);
        // A newline immediately after the start tag is ignored - the next
        // token's, whether it was written or came from `&#x0a;`.
        skip_newline_ = true;
        frameset_ok_ = false;
        return;
    }
    if (tag == "form") {
        if (form_ && !template_on_stack()) { return; }
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        const node_id element = insert_element(t);
        if (!template_on_stack()) { form_ = element; }
        return;
    }
    if (tag == "li") {
        frameset_ok_ = false;
        for (std::size_t i = open_.size(); i-- > 0;) {
            const entry & node = open_[i];
            if (node.ns == node_ns::html && node.tag == "li") {
                generate_implied_end_tags("li");
                pop_until("li");
                break;
            }
            if (is_special(node) && !one_of(node.tag, {"address", "div", "p"})) { break; }
        }
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        (void)insert_element(t);
        return;
    }
    if (tag == "dd" || tag == "dt") {
        frameset_ok_ = false;
        for (std::size_t i = open_.size(); i-- > 0;) {
            const entry & node = open_[i];
            if (node.ns == node_ns::html && (node.tag == "dd" || node.tag == "dt")) {
                const std::string found = node.tag;
                generate_implied_end_tags(found);
                pop_until(found);
                break;
            }
            if (is_special(node) && !one_of(node.tag, {"address", "div", "p"})) { break; }
        }
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        (void)insert_element(t);
        return;
    }
    if (tag == "plaintext") {
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        (void)insert_element(t);
        lexer_.set_content_model(content_model::plaintext, tag);
        return;
    }
    if (tag == "button") {
        if (has_in_scope("button")) {
            generate_implied_end_tags();
            pop_until("button");
        }
        reconstruct_formatting();
        (void)insert_element(t);
        frameset_ok_ = false;
        return;
    }
    if (tag == "a") {
        for (std::size_t i = active_.size(); i-- > 0;) {
            if (active_[i].marker) { break; }
            if (active_[i].tag == "a") {
                const node_id anchor = active_[i].id;
                if (!adoption_agency("a")) { in_body_any_other_end_tag("a"); }
                remove_formatting(anchor);
                remove_from_stack(anchor);
                break;
            }
        }
        reconstruct_formatting();
        const node_id element = insert_element(t);
        push_formatting(element, t);
        return;
    }
    if (one_of(tag, {"b", "big", "code", "em", "font", "i", "s", "small", "strike", "strong", "tt",
                     "u"})) {
        reconstruct_formatting();
        const node_id element = insert_element(t);
        push_formatting(element, t);
        return;
    }
    if (tag == "nobr") {
        reconstruct_formatting();
        if (has_in_scope("nobr")) {
            if (!adoption_agency("nobr")) { in_body_any_other_end_tag("nobr"); }
            reconstruct_formatting();
        }
        const node_id element = insert_element(t);
        push_formatting(element, t);
        return;
    }
    if (one_of(tag, {"applet", "marquee", "object"})) {
        reconstruct_formatting();
        (void)insert_element(t);
        push_marker();
        frameset_ok_ = false;
        return;
    }
    if (tag == "table") {
        if (!doc_->quirks() && has_in_scope("p", scope::button)) { close_p_element(); }
        (void)insert_element(t);
        frameset_ok_ = false;
        mode_ = mode::in_table;
        return;
    }
    if (one_of(tag, {"area", "br", "embed", "img", "keygen", "wbr"})) {
        reconstruct_formatting();
        (void)insert_element(t);
        pop();
        frameset_ok_ = false;
        return;
    }
    if (tag == "input") {
        if (context_ == "select") { return; } // fragment case
        // A <select> is parsed in body now (no "in select" mode since the
        // customizable-select change): an input inside one closes it.
        if (has_in_scope("select")) { pop_until("select"); }
        reconstruct_formatting();
        (void)insert_element(t);
        pop();
        const auto type = std::ranges::find_if(
            t.attributes, [](const token_attribute & a) { return a.name == "type"; });
        if (type == t.attributes.end() || !ascii_iequals(type->value, "hidden")) {
            frameset_ok_ = false;
        }
        return;
    }
    if (one_of(tag, {"param", "source", "track"})) {
        (void)insert_element(t);
        pop();
        return;
    }
    if (tag == "hr") {
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        if (has_in_scope("select")) { generate_implied_end_tags(); }
        (void)insert_element(t);
        pop();
        frameset_ok_ = false;
        return;
    }
    if (tag == "image") {
        token img = t;
        img.name = "img";
        return in_body_start(img);
    }
    if (tag == "textarea") {
        (void)insert_element(t);
        skip_newline_ = true;
        lexer_.set_content_model(content_model::rcdata, tag);
        original_mode_ = mode_;
        frameset_ok_ = false;
        mode_ = mode::text;
        return;
    }
    if (tag == "xmp") {
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        reconstruct_formatting();
        frameset_ok_ = false;
        return insert_text_element(t, content_model::rawtext);
    }
    if (tag == "iframe") {
        frameset_ok_ = false;
        return insert_text_element(t, content_model::rawtext);
    }
    if (tag == "noembed" || (tag == "noscript" && scripting_)) {
        return insert_text_element(t, content_model::rawtext);
    }
    if (tag == "select") {
        if (context_ == "select") { return; } // fragment case
        // A second <select> inside one CLOSES it and is itself ignored.
        if (has_in_scope("select")) {
            pop_until("select");
            return;
        }
        reconstruct_formatting();
        (void)insert_element(t);
        frameset_ok_ = false;
        return;
    }
    if (tag == "option") {
        if (has_in_scope("select")) {
            generate_implied_end_tags("optgroup");
        } else if (current_is("option")) {
            pop();
        }
        reconstruct_formatting();
        (void)insert_element(t);
        return;
    }
    if (tag == "optgroup") {
        if (has_in_scope("select")) {
            generate_implied_end_tags();
        } else if (current_is("option")) {
            pop();
        }
        reconstruct_formatting();
        (void)insert_element(t);
        return;
    }
    if (tag == "rb" || tag == "rtc") {
        if (has_in_scope("ruby")) { generate_implied_end_tags(); }
        (void)insert_element(t);
        return;
    }
    if (tag == "rp" || tag == "rt") {
        if (has_in_scope("ruby")) { generate_implied_end_tags("rtc"); }
        (void)insert_element(t);
        return;
    }
    if (tag == "math") {
        // MathML: foreign content from here to the matching end tag or a
        // breakout, in `node_ns::other` with its URI on the document.
        reconstruct_formatting();
        (void)insert_element(t, node_ns::other);
        if (t.self_closing) { pop(); }
        return;
    }
    if (tag == "svg") {
        reconstruct_formatting();
        (void)insert_element(t, node_ns::svg);
        open_foreign(t);
        if (t.self_closing) {
            close_foreign(t.source_end);
            pop();
        }
        return;
    }
    if (one_of(tag, {"caption", "col", "colgroup", "frame", "head", "tbody", "td", "tfoot", "th",
                     "thead", "tr"})) {
        return;
    }
    reconstruct_formatting();
    (void)insert_element(t);
}

void tree_builder::in_body_end(const token & t) {
    const std::string & tag = t.name;
    if (tag == "template") { return process(t, mode::in_head); }
    if (tag == "body") {
        if (!has_in_scope("body")) { return; }
        mode_ = mode::after_body;
        return;
    }
    if (tag == "html") {
        if (!has_in_scope("body")) { return; }
        mode_ = mode::after_body;
        return process(t, mode_);
    }
    if (one_of(tag,
               {"address", "article", "aside",  "blockquote", "button",   "center",     "details",
                "dialog",  "dir",     "div",    "dl",         "fieldset", "figcaption", "figure",
                "footer",  "header",  "hgroup", "listing",    "main",     "menu",       "nav",
                "ol",      "pre",     "search", "section",    "select",   "summary",    "ul"})) {
        if (!has_in_scope(tag)) { return; }
        generate_implied_end_tags();
        pop_until(tag);
        return;
    }
    if (tag == "form") {
        if (!template_on_stack()) {
            const node_id node = form_;
            form_ = node_id{};
            if (!node || !has_in_scope(node)) { return; }
            generate_implied_end_tags();
            remove_from_stack(node);
            return;
        }
        if (!has_in_scope("form")) { return; }
        generate_implied_end_tags();
        pop_until("form");
        return;
    }
    if (tag == "p") {
        if (!has_in_scope("p", scope::button)) { (void)insert_element("p", {}); }
        close_p_element();
        return;
    }
    if (tag == "li") {
        if (!has_in_scope("li", scope::list_item)) { return; }
        generate_implied_end_tags("li");
        pop_until("li");
        return;
    }
    if (tag == "dd" || tag == "dt") {
        if (!has_in_scope(tag)) { return; }
        generate_implied_end_tags(tag);
        pop_until(tag);
        return;
    }
    if (is_heading(tag)) {
        bool any = false;
        for (const std::string_view h : {"h1", "h2", "h3", "h4", "h5", "h6"}) {
            if (has_in_scope(h)) { any = true; }
        }
        if (!any) { return; }
        generate_implied_end_tags();
        pop_until_any({"h1", "h2", "h3", "h4", "h5", "h6"});
        return;
    }
    if (is_formatting_element(tag)) {
        if (adoption_agency(tag)) { return; }
        return in_body_any_other_end_tag(tag);
    }
    if (one_of(tag, {"applet", "marquee", "object"})) {
        if (!has_in_scope(tag)) { return; }
        generate_implied_end_tags();
        pop_until(tag);
        clear_formatting_to_marker();
        return;
    }
    if (tag == "br") {
        token br = t;
        br.kind = token_kind::start_tag;
        br.attributes.clear();
        return in_body_start(br);
    }
    in_body_any_other_end_tag(tag);
}

void tree_builder::in_body_any_other_end_tag(const std::string & tag) {
    for (std::size_t i = open_.size(); i-- > 0;) {
        const entry & node = open_[i];
        if (node.ns == node_ns::html && node.tag == tag) {
            generate_implied_end_tags(tag);
            while (open_.size() > i) { pop(); }
            return;
        }
        if (is_special(node)) { return; }
    }
}

// 13.2.6.4.8 "text"
void tree_builder::mode_text(const token & t) {
    switch (t.kind) {
    case token_kind::character: return insert_text(t.data);
    case token_kind::end_of_file:
        // An unclosed <script> at EOF is popped and never run: its
        // already-started flag is set and that is all.
        pop();
        mode_ = original_mode_;
        return process(t, mode_);
    case token_kind::end_tag: {
        const node_id closed = current();
        const bool script = current_is("script");
        pop();
        mode_ = original_mode_;
        // `</script>` RUNS THE SCRIPT, with the insertion point just past
        // this tag so its writes land there. The whole reason a page's
        // scripts see a half-built document.
        if (script) { run_script(closed); }
        return;
    }
    default: return;
    }
}

} // namespace ctbrowser::html
