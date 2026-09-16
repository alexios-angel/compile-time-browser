// dom_bindings' CSSOM - a rule: the identifier serialiser, cssText, and parsing
// a sheet's source rule by rule through the same entry point insertRule uses.

#include "internal.hpp"

#include <ctbrowser/shell/net/url.hpp>

#include <charconv>

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
           name == "-webkit-keyframes" || name == "scope" || name == "starting-style" ||
           name == "layer";
}

[[nodiscard]] bool at_rule_holds_declarations(std::string_view name) {
    return name == "font-face" || name == "page" || name == "counter-style" || name == "property" ||
           name == "font-palette-values" || name == "view-transition";
}

} // namespace

namespace detail {

// A NESTED DECLARATIONS RULE, CSS Nesting 1 §4: a run of declarations after
// a nested rule, in a style rule or in a group nested in one. Type 0 and no
// at-keyword, so it is told apart by this marker in `at_name`, which is
// private to these files.
const std::string_view nested_declarations_name = "nested-declarations";

// Is `rule` inside a style rule - so that its selectors are nested ones and a
// run of bare declarations in it belongs to that style rule?
[[nodiscard]] bool nested_in_style(
    const std::vector<std::unique_ptr<dom_bindings::css_rule_record>> & store, std::size_t rule) {
    for (std::size_t up = rule < store.size() ? store[rule]->parent : no_index; up < store.size();
         up = store[up]->parent) {
        if (store[up]->type == style_rule) { return true; }
    }
    return false;
}

// Bare declarations in a group that is NOT nested in a style rule are dropped
// (CSS Syntax 3 §5.4.4), which is only known once the group has a parent.
void prune_bare_declarations(std::vector<std::unique_ptr<dom_bindings::css_rule_record>> & store,
                             std::size_t rule) {
    if (rule >= store.size() || store[rule]->type == style_rule) { return; }
    std::vector<std::size_t> & children = store[rule]->children;
    std::erase_if(children, [&](std::size_t child) {
        return child < store.size() && store[child]->at_name == nested_declarations_name;
    });
    for (const std::size_t child : children) { prune_bare_declarations(store, child); }
}

} // namespace detail

// --- the record store -------------------------------------------------------

std::string dom_bindings::rule_css_text(const css_rule_record & rule) const {
    // A nested declarations rule is its block, and nothing around it.
    if (rule.at_name == nested_declarations_name) {
        return style::css::serialize_declaration_block(rule.declarations);
    }
    // A PRELUDE AND A DECLARATION BLOCK. A keyframe's prelude is its keyText and
    // a style rule's is its selector; neither carries an at-keyword.
    if (rule.type == style_rule || rule.type == keyframe_rule) {
        const std::string block = style::css::serialize_declaration_block(rule.declarations);
        if (rule.children.empty()) {
            if (block.empty()) { return rule.selector + " { }"; }
            return rule.selector + " { " + block + " }";
        }
        // WITH CHILD RULES, CSSOM's multi-line form: the declarations on one
        // indented line, then each child on its own, each indented by two
        // spaces ON ITS FIRST LINE ONLY - css-nesting/cssom.html asserts the
        // "broken" indentation of a nested `@supports` exactly.
        std::string out = rule.selector + " {\n";
        if (!block.empty()) { out += "  " + block + "\n"; }
        for (const std::size_t child : rule.children) {
            if (child >= css_rule_store_.size()) { continue; }
            out += "  " + rule_css_text(*css_rule_store_[child]) + "\n";
        }
        return out + "}";
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
        if (!rule.selector.empty()) {
            out += style::css::serialize_identifier(rule.selector) + " ";
        }
        return out + serialize_url(rule.prelude) + ";";
    }
    // `@font-feature-values <family>#`, whose block is feature blocks each a
    // list of `name: <integer>+`. One line, blocks in the order they arrived.
    if (rule.type == font_feature_values_rule) {
        std::string out = "@font-feature-values " + rule.prelude + " {";
        std::string type;
        for (const css_rule_record::feature_value & f : rule.features) {
            if (f.type != type) {
                if (!type.empty()) { out += " }"; }
                type = f.type;
                out += " @" + type + " {";
            }
            out += " " + style::css::serialize_identifier(f.name) + ":";
            for (const double n : f.numbers) {
                out += " " + std::to_string(static_cast<long long>(n));
            }
            out += ";";
        }
        if (!type.empty()) { out += " }"; }
        return out + " }";
    }
    // AN AT-RULE WHOSE BLOCK IS DECLARATIONS - `@font-face`, `@page`,
    // `@counter-style`. The prelude is `@page`'s page selector and empty for
    // most of them, and CSSOM 6.4.5 puts a SPACE on each side of the block
    // whether or not there is anything in it: `@page { }`, not `@page {}`.
    if (at_rule_holds_declarations(rule.at_name)) {
        std::string out = "@" + rule.at_name;
        if (!rule.prelude.empty()) { out += " " + rule.prelude; }
        const std::string block = style::css::serialize_declaration_block(rule.declarations);
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
    } else if (rule.type == keyframes_rule && !rule.prelude.empty()) {
        // A `<keyframes-name>` is a `<custom-ident>` or a `<string>`, and a
        // CSS-wide keyword or `none` is neither identifier - so a name a
        // script set to one serialises as the string `@keyframes "none"`,
        // which is the only spelling that reads back as that name.
        static constexpr std::string_view not_idents[] = {
            "initial", "inherit", "unset", "revert", "revert-layer", "default", "none"};
        bool reserved = false;
        for (const std::string_view word : not_idents) {
            if (ascii_iequals(rule.prelude, word)) { reserved = true; }
        }
        out += " " + (reserved ? quoted_string(rule.prelude) : rule.prelude);
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
    for (const std::size_t old : css_sheets_[sheet]->rules) { detach_rule(css_rule_store_, old); }
    css_sheets_[sheet]->rules.clear();
    for (const std::string_view span : split_top_level_rules(css)) {
        std::string error;
        const std::size_t at = parse_one_rule(sheet, span, error);
        // A SPAN THAT IS NOT A RULE IS DROPPED AND THE SHEET CONTINUES, which is
        // §5.4's recovery and is the difference between a stylesheet with a
        // mistake in it and a broken one.
        if (at == no_index) { continue; }
        // "replace() and replaceSync() ... remove any @import rules": a
        // constructed sheet fetches nothing, and the rule is not there to
        // report either (CSSStyleSheet-constructable-disallow-import).
        if (css_sheets_[sheet]->constructed && css_rule_store_[at]->type == import_rule) {
            css_rule_store_[at]->sheet = no_index;
            continue;
        }
        prune_bare_declarations(css_rule_store_, at);
        css_sheets_[sheet]->rules.push_back(at);
    }
}

// A NESTED STYLE RULE'S SELECTOR, once it has a parent: `& .b` for what was
// written `.b`, relative selectors anchored on `&`, and `&` itself kept -
// which is how CSS Nesting 1 §2.1 has it and how `cssRules[0].cssText` reads
// it back. The parent list is not needed to spell it, so a placeholder stands
// in for it.
namespace detail {

void nest_rule_selector(std::vector<std::unique_ptr<dom_bindings::css_rule_record>> & store,
                        atom_table & atoms,
                        std::span<const style::css::namespace_declaration> namespaces,
                        std::size_t rule) {
    if (rule >= store.size()) { return; }
    dom_bindings::css_rule_record & made = *store[rule];
    if (made.type == style_rule && nested_in_style(store, rule)) {
        bool bad = false;
        const std::vector<style::css::namespace_declaration> known(namespaces.begin(),
                                                                   namespaces.end());
        const std::vector<style::compiled_selector> placeholder(1);
        const style::css::nesting_context nesting{placeholder, false};
        const style::css::stylesheet parsed =
            style::css::parse_selector_text(made.prelude, atoms, bad, &known, &nesting);
        if (!bad && !parsed.selectors.empty()) {
            made.selector = representable(parsed.selectors)
                                ? serialize_selector_list(parsed.selectors, atoms, known)
                                : collapse_whitespace(made.prelude, html_whitespace);
        }
    }
    for (const std::size_t child : made.children) {
        nest_rule_selector(store, atoms, namespaces, child);
    }
}

} // namespace detail

// The `@namespace` rules a sheet holds, in the form the selector parser takes
// - so `ns|div` with an undeclared `ns` is the SyntaxError CSSOM 6.3.3 asks
// for rather than a prefix taken on trust (at-namespace.html).
std::vector<style::css::namespace_declaration> dom_bindings::sheet_namespaces(
    std::size_t sheet) const {
    std::vector<style::css::namespace_declaration> out;
    if (sheet >= css_sheets_.size()) { return out; }
    for (const std::size_t rule : css_sheets_[sheet]->rules) {
        if (rule >= css_rule_store_.size() || css_rule_store_[rule]->type != namespace_rule) {
            continue;
        }
        out.push_back({css_rule_store_[rule]->selector, css_rule_store_[rule]->prelude});
    }
    return out;
}

std::size_t dom_bindings::load_imported_sheet(std::size_t rule, std::string_view href) {
    if (rule >= css_rule_store_.size()) { return no_index; }
    const std::size_t parent = css_rule_store_[rule]->sheet;
    if (parent >= css_sheets_.size()) { return no_index; }
    css_sheets_.push_back(std::make_unique<css_sheet_record>());
    const std::size_t at = css_sheets_.size() - 1;
    css_sheet_record & made = *css_sheets_[at];
    made.owner_rule = rule;
    made.href = resolve_sheet_href(css_sheets_[parent]->href, href);
    made.origin_clean = !parse_absolute(made.href).valid ||
                        location_parts(made.href).origin == location_parts(location_href_).origin;
    // A CYCLE - `a.css` importing `b.css` importing `a.css` - is an empty sheet
    // at the point it closes, which is what every engine does; so is a chain
    // deeper than anyone writes by hand.
    std::size_t depth = 0;
    for (std::size_t up = parent; up < css_sheets_.size() && depth < 16; ++depth) {
        if (css_sheets_[up]->href == made.href) { return at; }
        const std::size_t via = css_sheets_[up]->owner_rule;
        if (via >= css_rule_store_.size()) { break; }
        up = css_rule_store_[via]->sheet;
    }
    if (depth >= 16 || assets_ == nullptr) { return at; }
    const std::vector<std::byte> bytes = assets_->load(made.href);
    made.source.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    parse_sheet_rules(at, css_sheets_[at]->source);
    return at;
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

    // The declarations of a block - see parse_declarations_into, which a
    // `cssText` write shares.
    const auto collect_into = [](css_rule_record & into, std::string_view body) {
        parse_declarations_into(into, body);
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
            frame.selector += collapse_whitespace(part, html_whitespace);
        }
        collect_into(frame, one.substr(brace + 1, shut - brace - 1));
        return css_rule_store_.size() - 1;
    };

    // A BLOCK'S CONTENTS: DECLARATIONS AND RULES, MIXED (CSS Syntax 3 §5.4.4).
    // A run of declarations up to the first nested rule is the rule's own when
    // `own_first`; every later run - and every run in a group - is a nested
    // declarations rule (CSS Nesting 1 §4), a child in its source position.
    // A `{` before the next `;` opens a nested rule, unless the run is a custom
    // property, whose value may hold a block; an at-keyword opens a nested
    // at-rule. The store is addressed by index throughout because every
    // child parse may move it.
    const auto parse_block_contents = [this, sheet](std::size_t owner, std::string_view body,
                                                    bool own_first) {
        std::string run;
        bool first = own_first;
        const auto flush = [&] {
            if (trim(run, html_whitespace).empty()) {
                run.clear();
                return;
            }
            if (first) {
                parse_declarations_into(*css_rule_store_[owner], run);
            } else {
                css_rule_store_.push_back(std::make_unique<css_rule_record>());
                const std::size_t child = css_rule_store_.size() - 1;
                css_rule_store_[child]->type = 0;
                css_rule_store_[child]->at_name = std::string{nested_declarations_name};
                css_rule_store_[child]->sheet = sheet;
                css_rule_store_[child]->parent = owner;
                parse_declarations_into(*css_rule_store_[child], run);
                css_rule_store_[owner]->children.push_back(child);
            }
            first = false;
            run.clear();
        };
        const auto adopt = [&](std::string_view span) {
            flush();
            first = false;
            std::string ignored;
            const std::size_t child = parse_one_rule(sheet, span, ignored);
            if (child == no_index) { return; }
            css_rule_store_[child]->parent = owner;
            css_rule_store_[owner]->children.push_back(child);
            const std::vector<style::css::namespace_declaration> namespaces =
                sheet_namespaces(sheet);
            nest_rule_selector(css_rule_store_, *atoms_, namespaces, child);
        };
        std::size_t at = 0;
        while (at < body.size()) {
            if (html_whitespace.find(body[at]) != std::string_view::npos || body[at] == ';') {
                ++at;
                continue;
            }
            if (body.compare(at, 2, "/*") == 0) {
                const std::size_t close = body.find("*/", at + 2);
                at = close == std::string_view::npos ? body.size() : close + 2;
                continue;
            }
            if (body[at] == '@') {
                std::size_t end = scan_to(body, at, "{;");
                if (end < body.size() && body[end] == '{') { end = scan_to(body, end + 1, "}"); }
                end = std::min(end + 1, body.size());
                adopt(trim(body.substr(at, end - at), html_whitespace));
                at = end;
                continue;
            }
            const std::size_t stop = scan_to(body, at, ";{");
            if (stop >= body.size() || body[stop] == ';') {
                run += body.substr(at, stop - at);
                run += ';';
                at = stop + 1;
                continue;
            }
            const std::string_view head = trim(body.substr(at, stop - at), html_whitespace);
            const std::size_t colon = scan_to(head, 0, ":");
            if (head.starts_with("--") && colon < head.size()) {
                // A custom property whose value holds a block: to the `;`.
                std::size_t end = scan_to(body, stop + 1, "}");
                end = scan_to(body, std::min(end + 1, body.size()), ";");
                run += body.substr(at, end - at);
                run += ';';
                at = std::min(end + 1, body.size());
                continue;
            }
            std::size_t end = scan_to(body, stop + 1, "}");
            end = std::min(end + 1, body.size());
            adopt(trim(body.substr(at, end - at), html_whitespace));
            at = end;
        }
        flush();
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
        // `@charset` IS NOT A RULE - CSS Syntax 3 §4 consumes it before the
        // rule list is parsed - so a sheet beginning with one has no rule
        // zero, and `insertRule("@charset ...")` is a SyntaxError.
        if (made.at_name == "charset") {
            css_rule_store_.pop_back();
            error = "SyntaxError";
            return no_index;
        }
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
        } else if (ascii_iequals(name, "font-feature-values")) {
            made.type = font_feature_values_rule;
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
                extra += collapse_whitespace(part, html_whitespace);
                after = probe;
            }
            if (!href.empty()) {
                made.selector = href;
                made.media_queries = parse_media_query_list(written.substr(after));
                made.prelude = extra;
                made.verbatim.clear();
                // THE IMPORTED SHEET, fetched now. Not for a constructed sheet:
                // CSSOM's replace() and replaceSync() parse an `@import` and
                // then drop it, and insertRule refuses one outright.
                if (sheet < css_sheets_.size() && !css_sheets_[sheet]->constructed) {
                    made.imported_sheet = load_imported_sheet(at, href);
                }
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
                // The prefix DECODED, as the selector parser decodes the one
                // in `x\*|test` - so the two meet (selectorSerialize.html,
                // "escaped character (*) in element prefix"). cssText
                // re-escapes it through style::css::serialize_identifier.
                std::string prefix;
                if (!second.empty()) {
                    const style::css::token_stream tokens = style::css::tokenize(first);
                    prefix = !tokens.tokens.empty() &&
                                     tokens.tokens.front().type == style::css::token_type::ident
                                 ? std::string{tokens.value_of(tokens.tokens.front())}
                                 : std::string{first};
                }
                made.selector = std::move(prefix);
                made.prelude = uri;
                made.verbatim.clear();
            }
        } else if (made.type == page_rule) {
            bool ok = false;
            made.prelude = serialize_page_selector(made.prelude, ok);
        } else if (made.type == font_feature_values_rule && open != std::string_view::npos &&
                   close != std::string_view::npos) {
            // CSS Fonts 4 §8.9: `<family-name>#`, then feature blocks, each
            // `@<feature-type> { <custom-ident>: <integer>+; ... }`. A block
            // whose name is not one of the seven, and an entry whose value is
            // not integers, are dropped as the parser drops them.
            made.prelude = style::css::serialize_font_family(
                collapse_whitespace(made.prelude, html_whitespace));
            static constexpr std::string_view feature_types[] = {
                "stylistic", "historical-forms", "styleset",  "character-variant",
                "swash",     "ornaments",        "annotation"};
            for (const std::string_view span : split_top_level_rules(body)) {
                const std::string_view one = trim(span, html_whitespace);
                const std::size_t brace = brace_at(one);
                const std::size_t shut =
                    brace == std::string_view::npos ? brace : block_end(one, brace);
                if (one.empty() || one.front() != '@' || brace == std::string_view::npos ||
                    shut == std::string_view::npos) {
                    continue;
                }
                const std::string type =
                    ascii_lower_copy(trim(one.substr(1, brace - 1), html_whitespace));
                if (std::find(std::begin(feature_types), std::end(feature_types), type) ==
                    std::end(feature_types)) {
                    continue;
                }
                const style::css::stylesheet parsed = style::css::parse_declaration_list(
                    one.substr(brace + 1, shut - brace - 1), *atoms_);
                for (const style::css::raw_declaration & d : parsed.declarations) {
                    css_rule_record::feature_value entry;
                    entry.type = type;
                    entry.name = std::string{atoms_->text(d.property)};
                    bool ok = true;
                    for (const std::string_view part :
                         split_top_level(parsed.text_of(d), " \t\n\r\f")) {
                        // A NUMBER, and only a number: `random(1, 3)` is a
                        // valid `order` and no feature value, and reading its
                        // digits out of the text was a std::stod that threw
                        // (random-in-descriptors).
                        const style::css::value_check n = check_declaration("order", part, false);
                        double number = 0.0;
                        const auto [end, ec] = std::from_chars(
                            n.serialized.data(), n.serialized.data() + n.serialized.size(), number);
                        if (!n.valid || ec != std::errc{} ||
                            end != n.serialized.data() + n.serialized.size()) {
                            ok = false;
                            break;
                        }
                        entry.numbers.push_back(number);
                    }
                    if (!ok || entry.numbers.empty()) { continue; }
                    std::erase_if(made.features, [&](const css_rule_record::feature_value & f) {
                        return f.type == entry.type && f.name == entry.name;
                    });
                    made.features.push_back(std::move(entry));
                }
            }
            made.verbatim.clear();
        }
        if (made.type == keyframes_rule) {
            // RECURSIVELY, THROUGH THIS SAME FUNCTION, rather than through the
            // sheet parser: the sheet parser drops a qualified rule with an
            // empty block, and `@media all { * {} }` is nothing but one. The
            // record store holds `unique_ptr`s, so the reference above stays
            // valid however far the recursion pushes the vector about.
            for (const std::string_view span : split_top_level_rules(body)) {
                const std::size_t child = make_keyframe(span);
                if (child == no_index) { continue; }
                css_rule_store_[child]->parent = at;
                made.children.push_back(child);
            }
            made.verbatim.clear();
        } else if (made.at_name == "layer" && open == std::string_view::npos) {
            // `@layer a, b;` - the statement form, its names in `prelude`.
            made.at_name = "layer-statement";
            made.prelude = collapse_whitespace(made.prelude, html_whitespace);
        } else if (at_rule_holds_rules(made.at_name)) {
            if (made.at_name == "layer" || made.at_name == "scope" || made.at_name == "container") {
                made.prelude = collapse_whitespace(made.prelude, html_whitespace);
            }
            parse_block_contents(at, body, false);
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
    const std::vector<style::css::namespace_declaration> namespaces = sheet_namespaces(sheet);
    const style::css::stylesheet selectors =
        style::css::parse_selector_text(prelude, *atoms_, bad, &namespaces);
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
                        ? serialize_selector_list(selectors.selectors, *atoms_, namespaces)
                        : collapse_whitespace(prelude, html_whitespace);
    // The author's selector is kept: a rule that becomes NESTED - once it has
    // a parent - is re-spelled from it with `&` (nest_rule_selector).
    made.prelude = collapse_whitespace(prelude, html_whitespace);
    parse_block_contents(at, trimmed.substr(open + 1, close - open - 1), true);
    return at;
}

} // namespace ctbrowser::shell
