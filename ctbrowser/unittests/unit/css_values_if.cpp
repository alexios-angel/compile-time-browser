// `if()`, CSS Values 5 §if-notation, and the Media Queries 4 condition grammar
// it shares with `@media`. The cases are `css/css-values/if-conditionals`'
// shapes, one per behaviour, run through the substituter directly and through
// a page.

#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "cssom_probe.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace {

using ctbrowser::atom;
using ctbrowser::atom_table;
using ctbrowser::style::css::attribute_lookup;
using ctbrowser::style::css::condition_environment;
using ctbrowser::style::css::custom_lookup;
using ctbrowser::style::css::evaluate_media_condition;
using ctbrowser::style::css::media_environment;
using ctbrowser::style::css::substitute_var;

// An element with a few custom properties, a parent with its own, one
// attribute, and a 30px font.
struct scope {
    atom_table atoms;
    std::map<std::string, std::string> own{{"--x", "3"},
                                           {"--inherited", "inner"},
                                           {"--same", "outer"},
                                           {"--len", "11px"},
                                           {"--n", "5"},
                                           {"--empty", ""},
                                           {"--self", "if(style(--self): a; else: b)"}};
    std::map<std::string, std::string> parent{{"--inherited", "outer"}, {"--same", "outer"}};
    custom_lookup lookup = [this](atom name) -> std::optional<std::string_view> {
        const auto it = own.find(std::string{atoms.text(name)});
        if (it == own.end()) { return std::nullopt; }
        return std::string_view{it->second};
    };
    attribute_lookup attributes = [](std::string_view name) -> std::optional<std::string> {
        if (name == "data-foo") { return "30px"; }
        return std::nullopt;
    };
    condition_environment conditions;
    scope() {
        conditions.lengths.font_size = 30;
        conditions.inherited = [this](std::string_view name) -> std::optional<std::string> {
            const auto it = parent.find(std::string{name});
            if (it == parent.end()) { return std::nullopt; }
            return it->second;
        };
        conditions.computed = [](std::string_view name) -> std::optional<std::string> {
            if (name == "color") { return "rgb(0, 128, 0)"; }
            return std::nullopt;
        };
        conditions.media = [](std::string_view text) {
            media_environment env;
            env.viewport_width = 800;
            env.viewport_height = 600;
            return evaluate_media_condition(text, env);
        };
    }
    [[nodiscard]] std::string sub(std::string_view value, std::string_view property = "--p") {
        conditions.property = std::string{property};
        return substitute_var(value, lookup, atoms, attributes, &conditions).value_or("<invalid>");
    }
};

void test_branches_and_else() {
    scope s;
    CHECK_EQ(s.sub("if(style(--x: 3): true_value)"), std::string{"true_value"});
    CHECK_EQ(s.sub("if( style( --x : 3 ) : true_value )"), std::string{"true_value "});
    CHECK_EQ(s.sub("if(style(--x): a;)"), std::string{"a"});
    CHECK_EQ(s.sub("if(style(--x: 0): a; else: b)"), std::string{"b"});
    CHECK_EQ(s.sub("if(style(--x: 3): ;)"), std::string{""});
    CHECK_EQ(s.sub("if(style(--x: 0): a;)"), std::string{"<invalid>"});
    CHECK_EQ(s.sub("if(style(--x: 1): v1; style(--x: 3): v3; else: v4)"), std::string{"v3"});
    CHECK_EQ(s.sub("if(style(--x: 1): v1; else: v2; style(--x: 3): v3)"), std::string{"v2"});
    CHECK_EQ(s.sub("if(style(--x: 3): a; else: b)if(style(--x): c)"), std::string{"ac"});
    // Malformed: no branch colon, a `!` anywhere, nothing at all.
    CHECK_EQ(s.sub("if(style(--x: 3) a; else: b)"), std::string{"<invalid>"});
    CHECK_EQ(s.sub("if(style(--x: 3): a; else: b!)"), std::string{"<invalid>"});
    CHECK_EQ(s.sub("if(!style(--x: 3): a; else: b)"), std::string{"<invalid>"});
    CHECK_EQ(s.sub("if()"), std::string{"<invalid>"});
    // A condition that does not parse is false, not fatal.
    CHECK_EQ(s.sub("if(style(--x) and invalid: a; else: b)"), std::string{"b"});
    CHECK_EQ(s.sub("if(invalid or style(--x): a; else: b)"), std::string{"b"});
    // The condition is substituted before it is read.
    CHECK_EQ(s.sub("if(style(--x: var(--x)): a; else: b)"), std::string{"a"});
    CHECK_EQ(s.sub("if(style(--x: var(--n)): a; else: b)"), std::string{"b"});
    CHECK_EQ(s.sub("if(style(--missing: var(--missing)): a; else: b)"), std::string{"b"});
    CHECK_EQ(s.sub("if(style(--len: attr(data-foo type(<length>))): a; else: b)"),
             std::string{"b"});
    CHECK_EQ(s.sub("if(style(--len: 11px): var(--x); else: b)"), std::string{"3"});
    // A query about the property being resolved is a cycle, and so is a var()
    // of it in a condition; another property's cycle leaves it without a value.
    CHECK_EQ(s.sub("if(style(--self): a; else: b)", "--self"), std::string{"<invalid>"});
    CHECK_EQ(s.sub("if(style((--self) or (--x)): a; else: b)", "--self"), std::string{"<invalid>"});
    CHECK_EQ(s.sub("if(style(--x: var(--self)): a; else: b)", "--self"), std::string{"<invalid>"});
    CHECK_EQ(s.sub("if(style(--self): a; else: b)", "--other"), std::string{"b"});
    CHECK_EQ(s.sub("if(style(--x: 0): var(--self); else: b)", "--self"), std::string{"b"});
    CHECK_EQ(s.sub("if(style(--x: 3): var(--self); else: b)", "--self"), std::string{"<invalid>"});
}

// ident-function-substitution and attr-argument-grammar: ident() joins its
// arguments at computed-value time and may name a var()'s property; a var()
// in attr()'s head may not grow a comma; a cycle is not rescued by a fallback.
void test_ident_and_argument_lists() {
    scope s;
    s.own["--part"] = "\"name\"";
    s.own["--myname"] = "PASS";
    s.own["--head"] = "data-foo type(<length>), 10";
    CHECK_EQ(s.sub("ident(\"my\" \"name\")"), std::string{"myname"});
    CHECK_EQ(s.sub("ident(\"my\" var(--part))"), std::string{"myname"});
    CHECK_EQ(s.sub("ident(ident(\"my\") ident(\"name\"))"), std::string{"myname"});
    CHECK_EQ(s.sub("ident(\"vtl-\" calc(3 * 2))"), std::string{"vtl-6"});
    CHECK_EQ(s.sub("var(ident(\"--\" \"myname\"))"), std::string{"PASS"});
    CHECK_EQ(s.sub("ident(var(--missing))"), std::string{"<invalid>"});
    CHECK_EQ(s.sub("ident(5px)"), std::string{"<invalid>"});
    CHECK_EQ(s.sub("ident(\"x\" var(--p))", "--p"), std::string{"<invalid>"});
    CHECK_EQ(s.sub("var(--p, 3px)", "--p"), std::string{"<invalid>"});
    CHECK_EQ(s.sub("attr(var(--head))"), std::string{"<invalid>"});
    CHECK_EQ(s.sub("var(--x type(*))"), std::string{"<invalid>"});
}

void test_style_queries() {
    scope s;
    const auto holds = [&](std::string_view query) {
        return s.sub("if(" + std::string{query} + ": y; else: n)") == "y";
    };
    CHECK(holds("style(--x: 3)"));
    CHECK(!holds("style(--x: calc(1 + 2))"));
    CHECK(holds("style(--x)"));
    CHECK(!holds("style(--missing)"));
    CHECK(holds("style(--empty)"));
    CHECK(holds("style(not (--missing))"));
    CHECK(holds("not style(--missing)"));
    CHECK(holds("style(--missing: initial)"));
    CHECK(!holds("style(--x: initial)"));
    CHECK(!holds("style(--inherited: inherit)"));
    CHECK(holds("style(--same: inherit)"));
    CHECK(holds("style(--same: unset)"));
    CHECK(!holds("style(--x: revert)"));
    CHECK(holds("style((--x: 3) and (not (--len: 1px)))"));
    CHECK(holds("style((--x: 0) or (--len: 11px))"));
    CHECK(!holds("style((--x: 3) and (not (--y: red) or (--z: 10px)))")); // unparseable: unknown
    CHECK(holds("style(--x: 3) and style(--len: 11px)"));
    CHECK(holds("style(--x: 0) or (style(--len: 11px) and style(--n: 5))"));
    CHECK(!holds("style(style(--x))"));
    CHECK(!holds("style(--x!)"));
    CHECK(holds("style(color: green)") == false); // rgb(0, 128, 0) is not the text `green`
    CHECK(holds("style(color: rgb(0, 128, 0))"));
    // Ranges.
    CHECK(holds("style(5 > 3)"));
    CHECK(holds("style(0 = 0px)"));
    CHECK(!holds("style(0 = 0%)"));
    CHECK(holds("style(0 < 3px)"));
    CHECK(!holds("style(5 > 3 !invalid)"));
    CHECK(holds("style(10em > 3px)"));
    CHECK(holds("style(3turn > 3deg)"));
    CHECK(!holds("style(3turn <= 3deg)"));
    CHECK(holds("style(3% >= 3%)"));
    CHECK(holds("style(3s > 3ms)"));
    CHECK(holds("style(3dppx > 96dpi)"));
    CHECK(!holds("style(3px > 3)"));
    CHECK(!holds("style(1px >= 1%) or style(1px <= 1%)"));
    CHECK(holds("style(--x <= 3)"));
    CHECK(holds("style(--x >= --x)"));
    CHECK(holds("style(--len > 3px)"));
    CHECK(!holds("style(--len > 3)"));
    CHECK(holds("style(3 < --n <= 5)"));
    CHECK(!holds("style(--x >= --n > --x)"));
    CHECK(!holds("style(--x + 1 >= --n)"));
    CHECK(holds("style(calc(var(--x) + 1) >= var(--x))"));
    CHECK(!holds("style(--x = initial)"));
}

void test_media_and_supports() {
    scope s;
    const auto holds = [&](std::string_view query) {
        return s.sub("if(" + std::string{query} + ": y; else: n)") == "y";
    };
    CHECK(!holds("media(max-width: 1px)"));
    CHECK(!holds("media((max-width: 1px))"));
    CHECK(holds("media(height <= 999999px)"));
    CHECK(holds("media(min-color: 1)"));
    CHECK(holds("media((min-color: 1) and (height <= 999999px))"));
    CHECK(holds("(media(min-width: 1px)) or (style(--missing))"));
    CHECK(!holds("(media(height <= 999999px)) and style(--missing)"));
    CHECK(holds("supports((display: table-cell))"));
    CHECK(holds("supports(display: table-cell)"));
    CHECK(!holds("supports(display)"));
    CHECK(!holds("supports(display: invalid)"));
    CHECK(holds("supports((display: table-cell) and (display: list-item))"));
    CHECK(!holds("supports((display: invalid) and (display: list-item))"));
    CHECK(holds("(media((min-color: 8) and (height <= 600px)) and style(--x: 3px)) or "
                "supports(display: table-cell)"));
}

// Media Queries 4: ranges, `or`, nesting, `not`, and an unknown feature that is
// unknown rather than a syntax error.
void test_media_conditions() {
    media_environment env;
    env.viewport_width = 800;
    env.viewport_height = 600;
    const auto q = [&](std::string_view text) { return evaluate_media_condition(text, env); };
    CHECK_EQ(q("(width >= 600px)"), std::optional<bool>{true});
    CHECK_EQ(q("(600px <= width < 900px)"), std::optional<bool>{true});
    CHECK_EQ(q("(900px < width)"), std::optional<bool>{false});
    CHECK_EQ(q("(min-width: 900px) or (orientation: landscape)"), std::optional<bool>{true});
    CHECK_EQ(q("not (width < 100px)"), std::optional<bool>{true});
    CHECK_EQ(q("((width > 100px) and (height > 100px)) or (hover: none)"),
             std::optional<bool>{true});
    CHECK_EQ(q("(unknown-feature: 3)"), std::optional<bool>{false});
    CHECK_EQ(q("not (unknown-feature: 3)"), std::optional<bool>{false});
    CHECK_EQ(q("(width > 100px) and (unknown-feature)"), std::optional<bool>{false});
    CHECK_EQ(q("(width > 100px) or (unknown-feature)"), std::optional<bool>{true});
    CHECK_EQ(q("(width > 100px) and (height > 100px) or (hover)"), std::optional<bool>{});
    CHECK_EQ(q("width > 100px"), std::optional<bool>{true});
    CHECK_EQ(q("garbage"), std::optional<bool>{false}); // a boolean feature nothing here knows
    CHECK_EQ(q("garbage garbage garbage"), std::optional<bool>{});
    // ...and a whole query list, as `@media` writes one.
    using ctbrowser::style::css::evaluate;
    using ctbrowser::style::css::parse_media_query_list;
    const auto list = [&](std::string_view text) {
        return evaluate(parse_media_query_list(text), env);
    };
    CHECK(list("screen and (min-width: 576px)"));
    CHECK(list("screen, print"));
    CHECK(!list("print"));
    CHECK(list("not print"));
    CHECK(!list("tv"));
    CHECK(list("not tv"));
    CHECK(list("only screen and (max-width: 900px)"));
    CHECK(!list("screen and (max-width: 500px)"));
    CHECK(list("(width >= 600px) and (width <= 900px)"));
    CHECK(!list("screen and (min-width: 900px) or (max-width: 10px)")); // `or` after a type
    CHECK(list(""));
    CHECK(!list("screen and"));
    CHECK(!list("and (width > 1px)"));
    CHECK(list("(prefers-color-scheme: light)"));
}

// The whole way through a page: a custom property, an ordinary one, and a
// parent to inherit from.
void test_a_page_reads_if_back() {
    using ctbrowser::shell::browser;
    using ctbrowser::shell::browser_options;
    using ctbrowser_test::logged;
    browser page{browser_options{400, 200}};
    page.load_html(R"html(<html><head><style>
      .outer { --inherited: outer_value; --x: 11; }
      #t { --a: if(style(--x: 3): yes; else: no);
           --b: if(style(--inherited: inherit): same; else: differs);
           --c: if(style(--missing): a);
           width: if(style(--x: 3): 30px; else: 60px);
           --d: if(media(width >= 100px): wide; else: narrow); }
    </style></head><body><div class=outer><div id=t style="--x: 3"></div></div>
    <script>
        const cs = getComputedStyle(document.getElementById('t'));
        console.log('if=' + [cs.getPropertyValue('--a'), cs.getPropertyValue('--b'),
                             cs.getPropertyValue('--c'), cs.width,
                             cs.getPropertyValue('--d')].join('|'));
    </script></body></html>)html");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "if="), std::string{"if=yes|same||30px|wide"});
}

} // namespace

int main() {
    test_branches_and_else();
    test_ident_and_argument_lists();
    test_style_queries();
    test_media_and_supports();
    test_media_conditions();
    test_a_page_reads_if_back();
    REPORT("css_values_if");
}
