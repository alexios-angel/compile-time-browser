#include "internal.hpp"

namespace ctbrowser::html {

using namespace treebuilder_detail;

// 13.2.6.4.9 "in table"
void tree_builder::mode_in_table(const token & t) {
    switch (t.kind) {
    case token_kind::character:
        if (current_is("table") || current_is("tbody") || current_is("template") ||
            current_is("tfoot") || current_is("thead") || current_is("tr")) {
            pending_table_text_.clear();
            pending_table_text_nonspace_ = false;
            original_mode_ = mode_;
            mode_ = mode::in_table_text;
            return process(t, mode_);
        }
        break;
    case token_kind::comment:
    case token_kind::processing_instruction: return insert_comment(t);
    case token_kind::doctype: return;
    case token_kind::start_tag: {
        const std::string & tag = t.name;
        if (tag == "caption") {
            clear_stack_back_to_table_context();
            push_marker();
            (void)insert_element(t);
            mode_ = mode::in_caption;
            return;
        }
        if (tag == "colgroup") {
            clear_stack_back_to_table_context();
            (void)insert_element(t);
            mode_ = mode::in_column_group;
            return;
        }
        if (tag == "col") {
            clear_stack_back_to_table_context();
            (void)insert_element("colgroup", {});
            mode_ = mode::in_column_group;
            return process(t, mode_);
        }
        if (is_table_section(tag)) {
            clear_stack_back_to_table_context();
            (void)insert_element(t);
            mode_ = mode::in_table_body;
            return;
        }
        if (one_of(tag, {"td", "th", "tr"})) {
            clear_stack_back_to_table_context();
            (void)insert_element("tbody", {});
            mode_ = mode::in_table_body;
            return process(t, mode_);
        }
        if (tag == "table") {
            if (!has_in_scope("table", scope::table)) { return; }
            pop_until("table");
            reset_insertion_mode();
            return process(t, mode_);
        }
        if (one_of(tag, {"style", "script", "template"})) { return process(t, mode::in_head); }
        if (tag == "input") {
            const auto type = std::ranges::find_if(
                t.attributes, [](const token_attribute & a) { return a.name == "type"; });
            if (type != t.attributes.end() && ascii_iequals(type->value, "hidden")) {
                (void)insert_element(t);
                pop();
                return;
            }
            break;
        }
        if (tag == "form") {
            if (template_on_stack() || form_) { return; }
            form_ = insert_element(t);
            pop();
            return;
        }
        break;
    }
    case token_kind::end_tag: {
        const std::string & tag = t.name;
        if (tag == "table") {
            if (!has_in_scope("table", scope::table)) { return; }
            pop_until("table");
            reset_insertion_mode();
            return;
        }
        if (one_of(tag, {"body", "caption", "col", "colgroup", "html", "tbody", "td", "tfoot", "th",
                         "thead", "tr"})) {
            return;
        }
        if (tag == "template") { return process(t, mode::in_head); }
        break;
    }
    case token_kind::end_of_file: return process(t, mode::in_body);
    case token_kind::incomplete: return;
    }
    // Anything else: parse error, process in body with foster parenting.
    foster_parenting_ = true;
    process(t, mode::in_body);
    foster_parenting_ = false;
}

// 13.2.6.4.10 "in table text"
void tree_builder::mode_in_table_text(const token & t) {
    if (t.kind == token_kind::character) {
        for (const char c : t.data) {
            if (c == '\0') { continue; }
            pending_table_text_ += c;
            if (!is_whitespace(c)) { pending_table_text_nonspace_ = true; }
        }
        return;
    }
    // Anything else: flush the pending characters, then reprocess.
    const std::string text = std::move(pending_table_text_);
    const bool nonspace = pending_table_text_nonspace_;
    pending_table_text_.clear();
    pending_table_text_nonspace_ = false;
    mode_ = original_mode_;
    if (!text.empty()) {
        if (nonspace) {
            foster_parenting_ = true;
            reconstruct_formatting();
            insert_text(text);
            frameset_ok_ = false;
            foster_parenting_ = false;
        } else {
            insert_text(text);
        }
    }
    process(t, mode_);
}

// 13.2.6.4.11 "in caption"
void tree_builder::mode_in_caption(const token & t) {
    const auto close_caption = [this] {
        if (!has_in_scope("caption", scope::table)) { return false; }
        generate_implied_end_tags();
        pop_until("caption");
        clear_formatting_to_marker();
        mode_ = mode::in_table;
        return true;
    };
    if (t.kind == token_kind::start_tag && one_of(t.name, {"caption", "col", "colgroup", "tbody",
                                                           "td", "tfoot", "th", "thead", "tr"})) {
        if (close_caption()) { process(t, mode_); }
        return;
    }
    if (t.kind == token_kind::end_tag) {
        if (t.name == "caption") {
            (void)close_caption();
            return;
        }
        if (t.name == "table") {
            if (close_caption()) { process(t, mode_); }
            return;
        }
        if (one_of(t.name, {"body", "col", "colgroup", "html", "tbody", "td", "tfoot", "th",
                            "thead", "tr"})) {
            return;
        }
    }
    process(t, mode::in_body);
}

// 13.2.6.4.12 "in column group"
void tree_builder::mode_in_column_group(const token & t) {
    switch (t.kind) {
    case token_kind::character:
        if (all_whitespace(t.data)) { return insert_text(t.data); }
        if (is_whitespace(t.data[0])) { return characters(t.data, mode::in_column_group); }
        break;
    case token_kind::comment:
    case token_kind::processing_instruction: return insert_comment(t);
    case token_kind::doctype: return;
    case token_kind::start_tag:
        if (t.name == "html") { return process(t, mode::in_body); }
        if (t.name == "col") {
            (void)insert_element(t);
            pop();
            return;
        }
        if (t.name == "template") { return process(t, mode::in_head); }
        break;
    case token_kind::end_tag:
        if (t.name == "colgroup") {
            if (!current_is("colgroup")) { return; }
            pop();
            mode_ = mode::in_table;
            return;
        }
        if (t.name == "col") { return; }
        if (t.name == "template") { return process(t, mode::in_head); }
        break;
    case token_kind::end_of_file: return process(t, mode::in_body);
    case token_kind::incomplete: return;
    }
    if (!current_is("colgroup")) { return; }
    pop();
    mode_ = mode::in_table;
    process(t, mode_);
}

// 13.2.6.4.13 "in table body"
void tree_builder::mode_in_table_body(const token & t) {
    if (t.kind == token_kind::start_tag) {
        const std::string & tag = t.name;
        if (tag == "tr") {
            clear_stack_back_to_table_body_context();
            (void)insert_element(t);
            mode_ = mode::in_row;
            return;
        }
        if (tag == "th" || tag == "td") {
            clear_stack_back_to_table_body_context();
            (void)insert_element("tr", {});
            mode_ = mode::in_row;
            return process(t, mode_);
        }
        if (one_of(tag, {"caption", "col", "colgroup", "tbody", "tfoot", "thead"})) {
            if (!has_in_scope("tbody", scope::table) && !has_in_scope("thead", scope::table) &&
                !has_in_scope("tfoot", scope::table)) {
                return;
            }
            clear_stack_back_to_table_body_context();
            pop();
            mode_ = mode::in_table;
            return process(t, mode_);
        }
    }
    if (t.kind == token_kind::end_tag) {
        const std::string & tag = t.name;
        if (is_table_section(tag)) {
            if (!has_in_scope(tag, scope::table)) { return; }
            clear_stack_back_to_table_body_context();
            pop();
            mode_ = mode::in_table;
            return;
        }
        if (tag == "table") {
            if (!has_in_scope("tbody", scope::table) && !has_in_scope("thead", scope::table) &&
                !has_in_scope("tfoot", scope::table)) {
                return;
            }
            clear_stack_back_to_table_body_context();
            pop();
            mode_ = mode::in_table;
            return process(t, mode_);
        }
        if (one_of(tag, {"body", "caption", "col", "colgroup", "html", "td", "th", "tr"})) {
            return;
        }
    }
    process(t, mode::in_table);
}

// 13.2.6.4.14 "in row"
void tree_builder::mode_in_row(const token & t) {
    if (t.kind == token_kind::start_tag) {
        const std::string & tag = t.name;
        if (tag == "th" || tag == "td") {
            clear_stack_back_to_table_row_context();
            (void)insert_element(t);
            mode_ = mode::in_cell;
            push_marker();
            return;
        }
        if (one_of(tag, {"caption", "col", "colgroup", "tbody", "tfoot", "thead", "tr"})) {
            if (!has_in_scope("tr", scope::table)) { return; }
            clear_stack_back_to_table_row_context();
            pop();
            mode_ = mode::in_table_body;
            return process(t, mode_);
        }
    }
    if (t.kind == token_kind::end_tag) {
        const std::string & tag = t.name;
        if (tag == "tr") {
            if (!has_in_scope("tr", scope::table)) { return; }
            clear_stack_back_to_table_row_context();
            pop();
            mode_ = mode::in_table_body;
            return;
        }
        if (tag == "table") {
            if (!has_in_scope("tr", scope::table)) { return; }
            clear_stack_back_to_table_row_context();
            pop();
            mode_ = mode::in_table_body;
            return process(t, mode_);
        }
        if (is_table_section(tag)) {
            if (!has_in_scope(tag, scope::table) || !has_in_scope("tr", scope::table)) { return; }
            clear_stack_back_to_table_row_context();
            pop();
            mode_ = mode::in_table_body;
            return process(t, mode_);
        }
        if (one_of(tag, {"body", "caption", "col", "colgroup", "html", "td", "th"})) { return; }
    }
    process(t, mode::in_table);
}

// 13.2.6.4.15 "in cell"
void tree_builder::mode_in_cell(const token & t) {
    if (t.kind == token_kind::end_tag) {
        const std::string & tag = t.name;
        if (tag == "td" || tag == "th") {
            if (!has_in_scope(tag, scope::table)) { return; }
            generate_implied_end_tags();
            pop_until(tag);
            clear_formatting_to_marker();
            mode_ = mode::in_row;
            return;
        }
        if (one_of(tag, {"body", "caption", "col", "colgroup", "html"})) { return; }
        if (one_of(tag, {"table", "tbody", "tfoot", "thead", "tr"})) {
            if (!has_in_scope(tag, scope::table)) { return; }
            close_the_cell();
            return process(t, mode_);
        }
    }
    if (t.kind == token_kind::start_tag && one_of(t.name, {"caption", "col", "colgroup", "tbody",
                                                           "td", "tfoot", "th", "thead", "tr"})) {
        if (!has_in_scope("td", scope::table) && !has_in_scope("th", scope::table)) { return; }
        close_the_cell();
        return process(t, mode_);
    }
    process(t, mode::in_body);
}

// 13.2.6.4.18 "in template"
void tree_builder::mode_in_template(const token & t) {
    const auto switch_to = [&](mode next) {
        if (!template_modes_.empty()) { template_modes_.pop_back(); }
        template_modes_.push_back(next);
        mode_ = next;
        process(t, mode_);
    };
    switch (t.kind) {
    case token_kind::character:
    case token_kind::comment:
    case token_kind::processing_instruction:
    case token_kind::doctype: return process(t, mode::in_body);
    case token_kind::start_tag: {
        const std::string & tag = t.name;
        if (one_of(tag, {"base", "basefont", "bgsound", "link", "meta", "noframes", "script",
                         "style", "template", "title"})) {
            return process(t, mode::in_head);
        }
        if (one_of(tag, {"caption", "colgroup", "tbody", "tfoot", "thead"})) {
            return switch_to(mode::in_table);
        }
        if (tag == "col") { return switch_to(mode::in_column_group); }
        if (tag == "tr") { return switch_to(mode::in_table_body); }
        if (tag == "td" || tag == "th") { return switch_to(mode::in_row); }
        return switch_to(mode::in_body);
    }
    case token_kind::end_tag:
        if (t.name == "template") { return process(t, mode::in_head); }
        return;
    case token_kind::end_of_file:
        if (!template_on_stack()) { return; } // stop parsing
        pop_until("template");
        clear_formatting_to_marker();
        if (!template_modes_.empty()) { template_modes_.pop_back(); }
        reset_insertion_mode();
        return process(t, mode_);
    case token_kind::incomplete: return;
    }
}

} // namespace ctbrowser::html
