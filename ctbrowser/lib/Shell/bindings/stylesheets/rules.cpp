// dom_bindings' CSSOM - a rule: the identifier serialiser, cssText, and parsing
// a sheet's source rule by rule through the same entry point insertRule uses.
//
// One of six files carved out of a 2,814-line bindings/stylesheets.cpp on
// 2026-09-08. The member functions belong to one class declared in
// include/ctbrowser/shell/bindings.hpp; the helpers more than one of these
// files needs are declared in internal.hpp beside this and defined in
// serialize.cpp and source.cpp. Nothing about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

// WHAT AN AT-RULE'S BLOCK CONTAINS - the one thing the at-keyword decides that
// nothing else can, and the reason a rule's contents can be modelled at all
// rather than kept as the author's bytes. There are exactly three answers:
// a list of RULES, a list of DECLARATIONS, or no block, and getting it wrong
// means reporting `@page`'s margins as child rules or `@media`'s rules as
// declarations. Anything not named here keeps its bytes and says nothing.
[[nodiscard]] bool at_rule_holds_rules(std::string_view name) {
    return name == "media" || name == "supports" || name == "container" || name == "keyframes" ||
           name == "-webkit-keyframes" || name == "scope" || name == "starting-style";
}

[[nodiscard]] bool at_rule_holds_declarations(std::string_view name) {
    return name == "font-face" || name == "page" || name == "counter-style" || name == "property" ||
           name == "font-palette-values" || name == "view-transition";
}

} // namespace

// CSSOM §2.1. Shared with `CSS.escape` in bindings/css.cpp, which is the same
// algorithm asked for by a page rather than by a serialiser.
std::string dom_bindings::serialize_css_identifier(std::string_view text) {
    std::string out;
    const auto hex_escape = [&out](unsigned char c) {
        static constexpr char digits[] = "0123456789abcdef";
        out += '\\';
        if (c >= 16) { out += digits[c >> 4]; }
        out += digits[c & 0xF];
        out += ' ';
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        // NULL is not escaped, it is REPLACED - §2.1 step 3, the same U+FFFD
        // substitution the CSS tokenizer does to its input.
        if (c == 0) {
            out += "\xEF\xBF\xBD";
            continue;
        }
        if (c <= 0x1F || c == 0x7F) {
            hex_escape(c);
            continue;
        }
        // A LEADING DIGIT, or a digit after a leading `-`, would make the
        // identifier a number: both are escaped numerically rather than with a
        // backslash, because `\1` is not a valid identifier start either.
        if (c >= '0' && c <= '9' && (i == 0 || (i == 1 && text[0] == '-'))) {
            hex_escape(c);
            continue;
        }
        if (c == '-' && text.size() == 1) {
            out += "\\-";
            continue;
        }
        if (c >= 0x80 || c == '-' || c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z')) {
            out += static_cast<char>(c);
            continue;
        }
        out += '\\';
        out += static_cast<char>(c);
    }
    return out;
}

// --- the record store -------------------------------------------------------

std::string dom_bindings::rule_css_text(const css_rule_record & rule) const {
    // A PRELUDE AND A DECLARATION BLOCK. A keyframe's prelude is its keyText and
    // a style rule's is its selector; neither carries an at-keyword.
    if (rule.type == style_rule || rule.type == keyframe_rule) {
        const std::string block = serialize_block(rule.declarations);
        if (block.empty()) { return rule.selector + " { }"; }
        return rule.selector + " { " + block + " }";
    }
    // AN AT-RULE THIS ENGINE DOES NOT MODEL answers with the author's bytes: an
    // `@layer a, b;` reconstructed from a prelude nobody parsed would be a claim
    // about a rule nobody read. `verbatim` is cleared exactly when the block WAS
    // read, so this is the test for which of the two a record is.
    if (!rule.verbatim.empty()) { return rule.verbatim; }
    // THE TWO WITH NO BLOCK AT ALL. CSSOM 6.4.7 and 6.4.9, and both serialise
    // their URL as a `url()` whatever the author wrote: `@import "a.css"` comes
    // back `@import url("a.css");` from every engine.
    if (rule.type == import_rule) {
        std::string out = "@import " + serialize_url(rule.selector);
        if (!rule.prelude.empty()) { out += " " + rule.prelude; }
        const std::string media = serialize_media_query_list(rule.media_queries);
        if (!media.empty()) { out += " " + media; }
        return out + ";";
    }
    if (rule.type == namespace_rule) {
        std::string out = "@namespace ";
        if (!rule.selector.empty()) { out += rule.selector + " "; }
        return out + serialize_url(rule.prelude) + ";";
    }
    // AN AT-RULE WHOSE BLOCK IS DECLARATIONS - `@font-face`, `@page`,
    // `@counter-style`. The prelude is `@page`'s page selector and empty for
    // most of them, and CSSOM 6.4.5 puts a SPACE on each side of the block
    // whether or not there is anything in it: `@page { }`, not `@page {}`.
    if (at_rule_holds_declarations(rule.at_name)) {
        std::string out = "@" + rule.at_name;
        if (!rule.prelude.empty()) { out += " " + rule.prelude; }
        const std::string block = serialize_block(rule.declarations);
        return block.empty() ? out + " { }" : out + " { " + block + " }";
    }
    // A GROUPING RULE IS THE ONE MULTI-LINE SERIALISATION IN THE CSSOM, and it
    // is not a style choice - CSSOM 6.4.1 spells it out for CSSMediaRule: the
    // at-keyword, a SPACE, the media query list, a SPACE, `{`, a newline, then
    // each child rule indented by two spaces and followed by a newline, then
    // `}`. `css/cssom/serialize-media-rule.html` compares the result byte for
    // byte, including the DOUBLE space an empty query list leaves in
    // `@media  {` - one after the at-keyword and one before the brace.
    std::string out = "@" + rule.at_name;
    if (rule.type == media_rule) {
        out += " " + serialize_media_query_list(rule.media_queries);
    } else if (!rule.prelude.empty()) {
        out += " " + rule.prelude;
    }
    out += " {\n";
    for (const std::size_t child : rule.children) {
        if (child >= css_rule_store_.size()) { continue; }
        // TWO SPACES ON EVERY LINE of the child rather than on its first, so a
        // nested group indents cumulatively - which is what the recursion means
        // and what a single leading indent would get wrong.
        out += "  ";
        for (const char c : rule_css_text(*css_rule_store_[child])) {
            out += c;
            if (c == '\n') { out += "  "; }
        }
        out += '\n';
    }
    out += '}';
    return out;
}

// THE SOURCE, RULE BY RULE, through the same entry point `insertRule` uses.
//
// It used to be built from `style::css::parse_stylesheet`'s output, which meant
// the CSSOM could only report the three rule shapes the CASCADE needs: a
// qualified rule, `@media` and `@font-face`. Everything else the author wrote -
// `@import`, `@namespace`, `@page`, `@keyframes`, `@supports`,
// `@counter-style` - was not a rule with less in it, it was not there, so
// `cssRules[0]` was the wrong rule and every index after it was off by one.
// `css/cssom/cssom-ruleTypeAndOrder.html` asserts exactly that indexing over
// seven sheets. The front end also drops a qualified rule whose block is empty,
// which is what `@media all { * {} }` is made of and what the setup of every
// `css/cssom/CSSGroupingRule-*.html` fixture asserts before it tests anything.
//
// `split_top_level_rules` walks the bytes and `parse_one_rule` is asked about
// each span, so a rule from a `<style>` and the same rule from `insertRule`
// cannot be spelled - or typed, or counted - two different ways.
void dom_bindings::parse_sheet_rules(std::size_t sheet, std::string_view css) {
    if (sheet >= css_sheets_.size()) { return; }
    css_sheets_[sheet]->rules.clear();
    for (const std::string_view span : split_top_level_rules(css)) {
        std::string error;
        const std::size_t at = parse_one_rule(sheet, span, error);
        // A SPAN THAT IS NOT A RULE IS DROPPED AND THE SHEET CONTINUES, which is
        // §5.4's recovery and is the difference between a stylesheet with a
        // mistake in it and a broken one.
        if (at == no_index) { continue; }
        css_sheets_[sheet]->rules.push_back(at);
    }
}

std::size_t dom_bindings::parse_one_rule(std::size_t sheet, std::string_view text,
                                         std::string & error) {
    const std::string_view trimmed = trim(text, html_whitespace);
    if (trimmed.empty()) {
        error = "SyntaxError";
        return no_index;
    }
    css_rule_store_.push_back(std::make_unique<css_rule_record>());
    const std::size_t at = css_rule_store_.size() - 1;
    css_rule_record & made = *css_rule_store_[at];
    made.sheet = sheet;

    // THE DECLARATIONS OF A BLOCK, through the two entry points that exist for
    // exactly this - `parse_declaration_list`, which a `style` attribute uses,
    // and `check_declaration`, which `el.style` writes through. A value the
    // grammar refuses is one the cascade drops, so publishing it would advertise
    // a declaration that does not apply.
    const auto collect_into = [this](css_rule_record & into, std::string_view body) {
        const style::css::stylesheet parsed = style::css::parse_declaration_list(body, *atoms_);
        for (const style::css::raw_declaration & d : parsed.declarations) {
            const std::string property{atoms_->text(d.property)};
            const style::css::value_check checked =
                check_declaration(property, parsed.text_of(d), false);
            if (!checked.valid) { continue; }
            into.declarations.push_back(css_declaration{property, checked.serialized, d.important});
        }
    };

    // ONE KEYFRAME. `0%, to { opacity: 0 }` is a qualified rule whose prelude is
    // a `<keyframe-selector>#` and NOT a selector, so `parse_selector_text`
    // refuses it and treating a `@keyframes` block as a list of ordinary rules
    // would empty it. CSSOM 6.4.10's keyText is that list, comma-separated;
    // `from` and `to` keep the spelling the author used.
    const auto make_keyframe = [this, sheet, &collect_into](std::string_view span) -> std::size_t {
        const std::string_view one = trim(span, html_whitespace);
        const std::size_t brace = brace_at(one);
        const std::size_t shut = brace == std::string_view::npos ? brace : block_end(one, brace);
        if (brace == std::string_view::npos || shut == std::string_view::npos) { return no_index; }
        css_rule_store_.push_back(std::make_unique<css_rule_record>());
        css_rule_record & frame = *css_rule_store_.back();
        frame.sheet = sheet;
        frame.type = keyframe_rule;
        for (const std::string_view part : split_on_commas(one.substr(0, brace))) {
            if (!frame.selector.empty()) { frame.selector += ", "; }
            frame.selector += collapse_whitespace(part);
        }
        collect_into(frame, one.substr(brace + 1, shut - brace - 1));
        return css_rule_store_.size() - 1;
    };

    if (trimmed.front() == '@') {
        // AN AT-RULE. The name decides the type, which is what `instanceof
        // CSSMediaRule` asks; what happens to its BLOCK depends on which of the
        // three shapes the rule is - a list of rules, a list of declarations, or
        // neither, in which case the author's bytes are the honest answer to
        // `cssText` for a rule nothing here has modelled.
        const std::size_t name_end = trimmed.find_first_of(std::string{html_whitespace} + "{;", 1);
        const std::string_view name =
            trimmed.substr(1, (name_end == std::string_view::npos ? trimmed.size() : name_end) - 1);
        made.at_name = ascii_lower_copy(name);
        made.verbatim = std::string{trimmed};
        {
            // The prelude is what stands between the at-keyword and the block
            // (or the `;` that ends a statement at-rule): `@media` -> `all and
            // (min-width: 10px)`.
            const std::size_t after =
                name_end == std::string_view::npos ? trimmed.size() : name_end;
            std::size_t body = trimmed.find_first_of("{;", after);
            if (body == std::string_view::npos) { body = trimmed.size(); }
            made.prelude = std::string{trim(trimmed.substr(after, body - after), html_whitespace)};
        }
        if (ascii_iequals(name, "media")) {
            made.type = media_rule;
        } else if (ascii_iequals(name, "font-face")) {
            made.type = font_face_rule;
        } else if (ascii_iequals(name, "import")) {
            made.type = import_rule;
        } else if (ascii_iequals(name, "namespace")) {
            made.type = namespace_rule;
        } else if (ascii_iequals(name, "supports")) {
            made.type = supports_rule;
        } else if (ascii_iequals(name, "page")) {
            made.type = page_rule;
        } else if (ascii_iequals(name, "counter-style")) {
            made.type = counter_style_rule;
        } else if (ascii_iequals(name, "keyframes") || ascii_iequals(name, "-webkit-keyframes")) {
            made.type = keyframes_rule;
        } else {
            made.type = 0;
        }

        // The block, if there is one. `@import` and `@namespace` end at a `;`
        // and have none at all.
        const std::size_t open = brace_at(trimmed);
        const std::size_t close = open == std::string_view::npos ? open : block_end(trimmed, open);
        const std::string_view body =
            open == std::string_view::npos || close == std::string_view::npos
                ? std::string_view{}
                : trimmed.substr(open + 1, close - open - 1);

        if (made.type == media_rule) {
            // THE AUTHOR'S BYTES, which is the whole reason a media rule made
            // here serialises exactly and one recovered from a sheet does not.
            made.media_queries = parse_media_query_list(made.prelude);
            made.prelude.clear();
        } else if (made.type == import_rule) {
            // `<url> <layer>? <supports()>? <media-query-list>?`. The URL and
            // the media list get fields of their own because the interface has
            // a reader for each; `layer` and `supports()` stay in `prelude` in
            // the order the author wrote them, which is the only order CSS
            // Cascade 5 allows.
            const std::string written = made.prelude;
            std::size_t after = 0;
            const std::string href = url_value(next_component(written, after));
            std::string extra;
            for (std::size_t probe = after;;) {
                const std::string_view part = next_component(written, probe);
                if (part.empty()) { break; }
                const bool layer =
                    ascii_iequals(part, "layer") || ascii_iequals(part.substr(0, 6), "layer(");
                if (!layer && !ascii_iequals(part.substr(0, 9), "supports(")) { break; }
                if (!extra.empty()) { extra += ' '; }
                extra += collapse_whitespace(part);
                after = probe;
            }
            if (!href.empty()) {
                made.selector = href;
                made.media_queries = parse_media_query_list(written.substr(after));
                made.prelude = extra;
                made.verbatim.clear();
            }
        } else if (made.type == namespace_rule) {
            // `[<prefix>]? <url>`, and the prefix is optional - a default
            // namespace has none, which is not the same as having an empty one.
            const std::string written = made.prelude;
            std::size_t after = 0;
            const std::string_view first = next_component(written, after);
            const std::string_view second = next_component(written, after);
            const std::string uri = url_value(second.empty() ? first : second);
            if (!uri.empty()) {
                made.selector = second.empty() ? std::string{} : std::string{first};
                made.prelude = uri;
                made.verbatim.clear();
            }
        } else if (made.type == page_rule) {
            bool ok = false;
            made.prelude = serialize_page_selector(made.prelude, ok);
        }
        if (at_rule_holds_rules(made.at_name)) {
            // RECURSIVELY, THROUGH THIS SAME FUNCTION, rather than through the
            // sheet parser: the sheet parser drops a qualified rule with an
            // empty block, and `@media all { * {} }` is nothing but one. The
            // record store holds `unique_ptr`s, so the reference above stays
            // valid however far the recursion pushes the vector about.
            for (const std::string_view span : split_top_level_rules(body)) {
                std::string ignored;
                const std::size_t child = made.type == keyframes_rule
                                              ? make_keyframe(span)
                                              : parse_one_rule(sheet, span, ignored);
                if (child == no_index) { continue; }
                css_rule_store_[child]->parent = at;
                made.children.push_back(child);
            }
            // Reconstructible from here on, so the author's bytes are dropped
            // and `cssText` serialises the group and its children.
            made.verbatim.clear();
        } else if (at_rule_holds_declarations(made.at_name) && open != std::string_view::npos &&
                   close != std::string_view::npos) {
            collect_into(made, body);
            made.verbatim.clear();
        }
        return at;
    }

    // A QUALIFIED RULE, SPLIT AT THE BRACE rather than run through the sheet
    // parser, and the reason is the EMPTY BLOCK. `consume_qualified_rule` keeps
    // a rule only when it has both a selector and a declaration, so `div { }`
    // came back as no rule at all and `insertRule` answered SyntaxError for text
    // that is perfectly good CSS. That is not an edge case in this suite: it is
    // what `addRule()` with no arguments produces (`undefined { }`, asserted in
    // `css/cssom/CSSStyleSheet.html`) and it is what every `@media all { * {} }`
    // fixture in `CSSGroupingRule-*.html` is made of.
    //
    // The two halves go through the two entry points that exist for exactly
    // them - `parse_selector_text`, which `querySelector` uses, and
    // `parse_declaration_list`, which a `style` attribute uses - so nothing is
    // parsed here by a rule of its own.
    const std::size_t open = brace_at(trimmed);
    const std::size_t close = open == std::string_view::npos ? open : block_end(trimmed, open);
    if (open == std::string_view::npos || close == std::string_view::npos ||
        !trim(trimmed.substr(close + 1), html_whitespace).empty()) {
        // No block at all, an unterminated one, or a second rule after the
        // first - CSSOM asks for exactly one rule and all three are the same
        // answer.
        css_rule_store_.pop_back();
        error = "SyntaxError";
        return no_index;
    }
    bool bad = false;
    const std::string_view prelude = trim(trimmed.substr(0, open), html_whitespace);
    const style::css::stylesheet selectors = style::css::parse_selector_text(prelude, *atoms_, bad);
    if (bad || selectors.selectors.empty()) {
        css_rule_store_.pop_back();
        error = "SyntaxError";
        return no_index;
    }
    made.type = style_rule;
    // The canonical serialisation when the compiled form IS the selector, and
    // the author's bytes when it is not - see `representable`. Whitespace is
    // collapsed either way, so `span  div  ` is `span div` in both.
    made.selector = representable(selectors.selectors)
                        ? serialize_selector_list(selectors.selectors, *atoms_)
                        : collapse_whitespace(prelude);
    collect_into(made, trimmed.substr(open + 1, close - open - 1));
    return at;
}

} // namespace ctbrowser::shell
