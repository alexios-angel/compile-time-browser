//===- unittests/unit/css/compiled.cpp - Compiled stylesheet round trip ---===//
//
// css::compile flattens a parsed stylesheet into constant-friendly data and
// css::materialize rebuilds it without parsing (style/css/compiled.hpp). A
// build that writes the flat data out as C++ is only as faithful as these two,
// so every sheet here must come back equal, field for field, to what the
// parser made: through the same atom table, and through a fresh one.
//
// The sheets cover each part of css::stylesheet: @import, @namespace, @layer,
// nested @media, @container, @scope, @keyframes, @font-face, @property,
// @function, nesting, every selector construct the compiled form holds, and
// Bootstrap. A field added to a stylesheet type that compile() does not carry
// shows up here as the first difference.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/style/css/compiled.hpp>
#include <ctbrowser/style/css/parser.hpp>
#include <ctbrowser/style/css/token.hpp>

#include "check.hpp"

#include <cstdio>
#include <string>
#include <string_view>

namespace css = ctbrowser::style::css;

namespace {

/// The name of the first member of `a` and `b` that differs, or empty.
[[nodiscard]] std::string first_difference(const css::stylesheet & a, const css::stylesheet & b) {
    const auto table = [](std::string_view name, const auto & x, const auto & y) -> std::string {
        if (x.size() != y.size()) {
            return std::string{name} + " (size " + std::to_string(x.size()) + " vs " +
                   std::to_string(y.size()) + ")";
        }
        for (std::size_t i = 0; i < x.size(); ++i) {
            if (!(x[i] == y[i])) { return std::string{name} + "[" + std::to_string(i) + "]"; }
        }
        return {};
    };
    if (a.pool != b.pool) { return "pool"; }
    if (a.prefixes_checked != b.prefixes_checked) { return "prefixes_checked"; }
    if (a.forgiving != b.forgiving) { return "forgiving"; }
    if (a.source_length != b.source_length) { return "source_length"; }
    for (std::string found :
         {table("namespaces", a.namespaces, b.namespaces), table("imports", a.imports, b.imports),
          table("tokens", a.tokens, b.tokens), table("values", a.values, b.values),
          table("selectors", a.selectors, b.selectors),
          table("declarations", a.declarations, b.declarations), table("rules", a.rules, b.rules),
          table("font_faces", a.font_faces, b.font_faces),
          table("properties", a.properties, b.properties),
          table("functions", a.functions, b.functions),
          table("keyframes", a.keyframes, b.keyframes),
          table("conditions", a.conditions, b.conditions), table("layers", a.layers, b.layers),
          table("scopes", a.scopes, b.scopes), table("containers", a.containers, b.containers)}) {
        if (!found.empty()) { return found; }
    }
    if (!(a == b)) { return "a member first_difference does not list"; }
    return {};
}

/// Compiles `sheet` and checks that it comes back equal, through `atoms` and
/// through a fresh table.
void expect_round_trip(std::string_view label, const css::stylesheet & sheet,
                       ctbrowser::atom_table & atoms) {
    const css::compiled_sheet_data data = css::compile(sheet, atoms);
    const css::stylesheet back = css::materialize(data.view(), atoms);
    if (const std::string where = first_difference(sheet, back); !where.empty()) {
        std::printf("FAIL %s: the rebuilt sheet differs at %s\n", std::string{label}.c_str(),
                    where.c_str());
        ++ctbrowser_test_failures;
        return;
    }
    // Into a table that has never seen these names, then back into the first
    // one: the atoms' ids differ in between, and only their text may matter.
    ctbrowser::atom_table fresh;
    const css::stylesheet elsewhere = css::materialize(data.view(), fresh);
    const css::compiled_sheet_data again = css::compile(elsewhere, fresh);
    const css::stylesheet home = css::materialize(again.view(), atoms);
    if (const std::string where = first_difference(sheet, home); !where.empty()) {
        std::printf("FAIL %s: through a fresh atom table the sheet differs at %s\n",
                    std::string{label}.c_str(), where.c_str());
        ++ctbrowser_test_failures;
    }
}

void expect_sheet_round_trip(std::string_view css_text) {
    ctbrowser::atom_table atoms;
    expect_round_trip(css_text.substr(0, 60), css::parse_stylesheet(css_text, atoms), atoms);
}

void test_at_rules_round_trip() {
    expect_sheet_round_trip("@charset \"UTF-8\";\n"
                            "@import url(a.css) layer(base) supports(display: grid) screen;\n"
                            "@import 'b.css' scope(.card);\n"
                            "@namespace svg url(http://www.w3.org/2000/svg);\n"
                            "@namespace url(http://www.w3.org/1999/xhtml);\n"
                            "a { color: red }");
    expect_sheet_round_trip("@layer base, theme; @layer base { .a { color: red } }"
                            " @layer { b { margin: 0 } } @layer theme.dark { c { color: blue } }");
    expect_sheet_round_trip("@media screen and (min-width: 100px) { .a { color: red }"
                            " @media print { .b { color: blue !important } } }"
                            " @media not all and (orientation: portrait), (hover) { i { x: y } }");
    expect_sheet_round_trip("@container card (min-width: 400px) { .t { font-size: 2em } }"
                            " @container (width > 10px) { @container x (height < 5px) { p {"
                            " margin: 0 } } }");
    expect_sheet_round_trip("@scope (.card) to (.content) { img { border: 1px solid } }"
                            " @scope { :scope { color: red } } @scope (.a, .b) { & p { x: y } }");
    expect_sheet_round_trip("@keyframes spin { from { transform: rotate(0) }"
                            " 50%, 75% { opacity: .5 } to { transform: rotate(360deg) } }"
                            " @media print { @keyframes fade { 0% { opacity: 0 } } }");
    expect_sheet_round_trip("@font-face { font-family: X; src: url(x.woff2) format('woff2');"
                            " font-weight: bold } @property --x { syntax: '<length>';"
                            " inherits: false; initial-value: 0px }"
                            " @function --double(--n) returns <number> { result: calc(var(--n) *"
                            " 2) }");
}

void test_selectors_round_trip() {
    expect_sheet_round_trip("div#id.a.b[type=checkbox i] { x: y }"
                            " [data-x|=en][href^='http' s][title~=t][c$=d][e*=f][g] { x: y }"
                            " ul > li + li ~ li a, :root, svg|rect, *|*, |p { x: y }"
                            " @namespace svg url(http://www.w3.org/2000/svg);");
    expect_sheet_round_trip("@namespace svg url(http://www.w3.org/2000/svg);"
                            " svg|rect, *|*, |p, [svg|href] { x: y }");
    expect_sheet_round_trip(
        ":not(.a, :is(.b, :where(#c))) .d { x: y }"
        " :has(> img, + p) { x: y }"
        " li:nth-child(2n+1 of .x):nth-last-of-type(3):nth-of-type(odd) { x: y }"
        " :lang(en, \"*-Latn\") :dir(rtl) { x: y } :heading(1, 2), :heading { x: y"
        " }");
    expect_sheet_round_trip("p::before:hover, .a::after, a:hover:focus-visible:checked,"
                            " ::part(x), ::slotted(span), input::placeholder { x: y }"
                            " :first-child:last-child:only-of-type:empty { x: y }");
    expect_sheet_round_trip(".card { color: red; & .title { color: blue } .x & { y: z }"
                            " @media (hover) { &:hover { z: w } } }");
    expect_sheet_round_trip(".foo\\:bar { content: \"\\26 x\" } #a\\:b { --custom: { a } ;"
                            " color: var(--x, red) } .form-select:-moz-focusring { x: y }");
}

void test_declaration_lists_and_depth_round_trip() {
    {
        ctbrowser::atom_table atoms;
        expect_round_trip("declaration list",
                          css::parse_declaration_list(
                              "view-transition-name: \\33 myident; color: red !important", atoms),
                          atoms);
    }
    std::string deep = "a { color: ";
    for (int i = 0; i < 1000; ++i) { deep += '('; }
    for (int i = 0; i < 1000; ++i) { deep += ')'; }
    deep += "; width: 1px } b { c: d }";
    expect_sheet_round_trip(deep);
    expect_sheet_round_trip("");
}

void test_bootstrap_round_trips() {
    const std::string bootstrap = read_file("vendor/bootstrap/bootstrap.css");
    check(!bootstrap.empty(), "vendor/bootstrap/bootstrap.css reads");
    if (bootstrap.empty()) { return; }
    ctbrowser::atom_table atoms;
    const css::stylesheet sheet = css::parse_stylesheet(bootstrap, atoms);
    expect_round_trip("bootstrap.css", sheet, atoms);
    // The compiled form shares each name: Bootstrap's thousands of selectors
    // name a few hundred classes.
    const css::compiled_sheet_data data = css::compile(sheet, atoms);
    std::printf("  bootstrap: %zu bytes of names for %zu selectors, %zu tokens\n",
                data.strings.size(), data.selectors.size(), data.tokens.size());
    CHECK(data.selector_count == sheet.selectors.size());
}

void test_token_streams_round_trip() {
    for (const std::string_view text : {"div", "12.5px", "#0d6efd", "\"a\\\"b\"", "url(a b.png)",
                                        ".foo\\:bar", "\\0", "@charset \"UTF-8\";", "a\r\nb",
                                        "\xEF\xBB\xBF"
                                        "body{a:b}",
                                        ""}) {
        const css::token_stream stream = css::tokenize(text);
        const css::token_stream back = css::materialize(
            css::compiled_tokens{stream.pool, stream.tokens, stream.source_length});
        check(back == stream, "token stream of [" + std::string{text} + "] round-trips");
    }
}

// The compiled forms are constant-friendly: a generated file declares them
// constexpr and a program materializes them without parsing.
constexpr css::css_token div_tokens[] = {
    {css::token_type::ident, 0, 0, 0, 3, 0},
    {css::token_type::eof, 0, 0, 3, 0, 0},
};
constexpr css::compiled_tokens compiled_div{"div", div_tokens, 3};
static_assert(compiled_div.tokens.size() == 2);
constexpr css::compiled_text red_text{0, 3};
constexpr css::compiled_media_query print_query{css::media_type::print, false, false, red_text};
constexpr css::compiled_sheet empty_sheet{};
static_assert(empty_sheet.selector_count == 0 && print_query.type == css::media_type::print);

void test_a_constant_materializes_as_the_tokenizer_would_make_it() {
    check(css::materialize(compiled_div) == css::tokenize("div"),
          "a constexpr token stream equals tokenize()'s");
}

} // namespace

int main() {
    test_at_rules_round_trip();
    test_selectors_round_trip();
    test_declaration_lists_and_depth_round_trip();
    test_bootstrap_round_trips();
    test_token_streams_round_trip();
    test_a_constant_materializes_as_the_tokenizer_would_make_it();
    REPORT("css_compiled");
}
