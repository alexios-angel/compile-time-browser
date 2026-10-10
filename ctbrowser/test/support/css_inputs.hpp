//===- ctbrowser/test/support/css_inputs.hpp - CSS test input seam --------===//
//
// How a CSS test gets the token stream, stylesheet or selector verdict for one
// of its inputs, so the same test runs on two builds:
//
//   ordinary      the tokenizer and the parser, at run time
//   native        CTBROWSER_TEST_NATIVE: what ctcss-translate compiled from
//                 the test's input manifest at build time, materialized with
//                 css::materialize; the tokenizer and parser are not linked
//
// The test keeps its inputs inline and asks for them through css_tokens,
// css_sheet, css_declarations, css_supports_selector and css_sheet_file. Every
// input it asks for must be listed in its manifest, a JSON file of the five
// kinds (tokens, stylesheets, declaration_lists, selectors, files), each a list
// of entries: a string, or a list of strings and {"repeat": s, "times": n}
// parts joined in order, for inputs too long to write out.
// check_css_inputs() at the end of the test fails on an input asked for but
// not listed, and on one listed but never asked for, in both builds, so the
// manifest and the test cannot drift apart.
//
// The native build's table is `compiled_css_inputs`, defined by the C++ the
// build generates from the manifest: one compiled_css_input per entry.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/style/css/compiled.hpp>
#include <ctbrowser/style/css/value.hpp>

#ifndef CTBROWSER_TEST_NATIVE
#include <ctbrowser/core/json.hpp>
#include <ctbrowser/style/css/parser.hpp>
#include <ctbrowser/style/css/token.hpp>
#endif

#include "check.hpp"

namespace ctbrowser_test {

/// What a test asks of one CSS input, and the manifest key that lists it.
enum class css_input_kind : std::uint8_t {
    tokens,           // css_tokens: the token stream.
    stylesheet,       // css_sheet: parse_stylesheet.
    declaration_list, // css_declarations: parse_declaration_list.
    selector,         // css_supports_selector: whether it parses as a selector.
    file,             // css_sheet_file: parse_stylesheet of a file, by path.
};

/// One input as the native build compiled it. `text` is the input, or for a
/// file the path the test names. Exactly one answer is set, by kind.
struct compiled_css_input {
    css_input_kind kind = css_input_kind::tokens;
    std::string_view text;
    const ctbrowser::style::css::compiled_tokens * tokens = nullptr;
    const ctbrowser::style::css::compiled_sheet * sheet = nullptr;
    bool supported = false;
};

#ifdef CTBROWSER_TEST_NATIVE
/// Every input this executable carries compiled; defined by the build.
extern const std::span<const compiled_css_input> compiled_css_inputs;
#endif

namespace detail {

using css_input = std::pair<css_input_kind, std::string>;

[[nodiscard]] inline std::set<css_input> & css_inputs_asked() {
    static std::set<css_input> asked;
    return asked;
}

[[nodiscard]] inline const char * name_of(css_input_kind kind) {
    switch (kind) {
    case css_input_kind::tokens: return "tokens";
    case css_input_kind::stylesheet: return "stylesheets";
    case css_input_kind::declaration_list: return "declaration_lists";
    case css_input_kind::selector: return "selectors";
    case css_input_kind::file: return "files";
    }
    return "?";
}

/// The first 60 bytes of `text`, with anything unprintable as `\xNN`, for a
/// failure message.
[[nodiscard]] inline std::string shown(std::string_view text) {
    std::string out;
    for (const char c : text.substr(0, 60)) {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 0x20 && u < 0x7f) {
            out += c;
        } else {
            char escaped[8];
            std::snprintf(escaped, sizeof escaped, "\\x%02x", static_cast<unsigned>(u));
            out += escaped;
        }
    }
    if (text.size() > 60) { out += "... (" + std::to_string(text.size()) + " bytes)"; }
    return out;
}

inline void ask(css_input_kind kind, std::string_view text) {
    css_inputs_asked().emplace(kind, std::string{text});
}

#ifdef CTBROWSER_TEST_NATIVE
/// The compiled answer for one input. An input the build did not compile
/// ends the test: there is nothing to answer with, and the parser is not
/// there to fall back to.
[[nodiscard]] inline const compiled_css_input & compiled(css_input_kind kind,
                                                         std::string_view text) {
    // ponytail: a linear scan; a test has a hundred-odd inputs.
    for (const compiled_css_input & input : compiled_css_inputs) {
        if (input.kind == kind && input.text == text) { return input; }
    }
    std::printf("FAIL %s input not compiled into this executable: [%s] - list it in the "
                "test's .inputs.json\n",
                name_of(kind), shown(text).c_str());
    std::exit(1);
}
#else
/// One manifest entry's text: a string, or the parts of a list joined.
[[nodiscard]] inline std::optional<std::string> entry_text(const ctbrowser::json_value & entry) {
    if (const auto * text = std::get_if<std::string>(&entry.data)) { return *text; }
    const auto * parts = std::get_if<ctbrowser::json_value::array>(&entry.data);
    if (parts == nullptr) { return std::nullopt; }
    std::string out;
    for (const ctbrowser::json_value & part : *parts) {
        if (const auto * text = std::get_if<std::string>(&part.data)) {
            out += *text;
            continue;
        }
        const auto * fields = std::get_if<ctbrowser::json_value::object>(&part.data);
        if (fields == nullptr) { return std::nullopt; }
        const std::string * repeated = nullptr;
        double times = -1;
        for (const ctbrowser::json_value::member & field : *fields) {
            if (field.key == "repeat") { repeated = std::get_if<std::string>(&field.value.data); }
            if (field.key == "times") {
                if (const auto * n = std::get_if<double>(&field.value.data)) { times = *n; }
            }
        }
        if (repeated == nullptr || !(times >= 0)) { return std::nullopt; }
        for (int i = 0; i < static_cast<int>(times); ++i) { out += *repeated; }
    }
    return out;
}

/// Every input `manifest` lists, or nothing with `why` set.
[[nodiscard]] inline std::optional<std::set<css_input>> manifest_inputs(std::string_view manifest,
                                                                        std::string & why) {
    const auto parsed = ctbrowser::parse_json(manifest);
    const auto * kinds =
        parsed ? std::get_if<ctbrowser::json_value::object>(&parsed->data) : nullptr;
    if (kinds == nullptr) {
        why = "not a JSON object";
        return std::nullopt;
    }
    std::set<css_input> listed;
    for (const ctbrowser::json_value::member & kind : *kinds) {
        if (kind.key == "about") { continue; }
        std::optional<css_input_kind> which;
        for (const css_input_kind k :
             {css_input_kind::tokens, css_input_kind::stylesheet, css_input_kind::declaration_list,
              css_input_kind::selector, css_input_kind::file}) {
            if (kind.key == name_of(k)) { which = k; }
        }
        const auto * entries = std::get_if<ctbrowser::json_value::array>(&kind.value.data);
        if (!which || entries == nullptr) {
            why = "unknown or malformed key \"" + kind.key + "\"";
            return std::nullopt;
        }
        for (const ctbrowser::json_value & entry : *entries) {
            const std::optional<std::string> text = entry_text(entry);
            if (!text) {
                why = "a malformed entry under \"" + kind.key + "\"";
                return std::nullopt;
            }
            listed.emplace(*which, *text);
        }
    }
    return listed;
}
#endif

} // namespace detail

/// The token stream of `text`: tokenize()'s.
[[nodiscard]] inline ctbrowser::style::css::token_stream css_tokens(std::string_view text) {
    detail::ask(css_input_kind::tokens, text);
#ifdef CTBROWSER_TEST_NATIVE
    return ctbrowser::style::css::materialize(
        *detail::compiled(css_input_kind::tokens, text).tokens);
#else
    return ctbrowser::style::css::tokenize(text);
#endif
}

/// The stylesheet `text` parses to: parse_stylesheet's.
[[nodiscard]] inline ctbrowser::style::css::stylesheet css_sheet(std::string_view text,
                                                                 ctbrowser::atom_table & atoms) {
    detail::ask(css_input_kind::stylesheet, text);
#ifdef CTBROWSER_TEST_NATIVE
    return ctbrowser::style::css::materialize(
        *detail::compiled(css_input_kind::stylesheet, text).sheet, atoms);
#else
    return ctbrowser::style::css::parse_stylesheet(text, atoms);
#endif
}

/// The declaration list `text` parses to: parse_declaration_list's.
[[nodiscard]] inline ctbrowser::style::css::stylesheet css_declarations(
    std::string_view text, ctbrowser::atom_table & atoms) {
    detail::ask(css_input_kind::declaration_list, text);
#ifdef CTBROWSER_TEST_NATIVE
    return ctbrowser::style::css::materialize(
        *detail::compiled(css_input_kind::declaration_list, text).sheet, atoms);
#else
    return ctbrowser::style::css::parse_declaration_list(text, atoms);
#endif
}

/// Whether `text` parses as a selector: supports_selector's answer.
[[nodiscard]] inline bool css_supports_selector(std::string_view text) {
    detail::ask(css_input_kind::selector, text);
#ifdef CTBROWSER_TEST_NATIVE
    return detail::compiled(css_input_kind::selector, text).supported;
#else
    return ctbrowser::style::css::supports_selector(text);
#endif
}

/// The stylesheet in the file at `path` (relative to the test's working
/// directory), or nothing when the ordinary build cannot read it.
[[nodiscard]] inline std::optional<ctbrowser::style::css::stylesheet> css_sheet_file(
    std::string_view path, ctbrowser::atom_table & atoms) {
    detail::ask(css_input_kind::file, path);
#ifdef CTBROWSER_TEST_NATIVE
    return ctbrowser::style::css::materialize(*detail::compiled(css_input_kind::file, path).sheet,
                                              atoms);
#else
    const std::string text = read_file(std::string{path});
    if (text.empty()) { return std::nullopt; }
    return ctbrowser::style::css::parse_stylesheet(text, atoms);
#endif
}

/// Fails the test on any input it asked for that `manifest_path` does not
/// list, and on any listed input it never asked for. The native build checks
/// against what it was compiled with, which the build generated from the same
/// manifest.
inline void check_css_inputs([[maybe_unused]] const std::string & manifest_path) {
    std::set<detail::css_input> available;
#ifdef CTBROWSER_TEST_NATIVE
    for (const compiled_css_input & input : compiled_css_inputs) {
        available.emplace(input.kind, std::string{input.text});
    }
#else
    std::string why;
    const auto listed = detail::manifest_inputs(read_file(manifest_path), why);
    if (!listed) {
        std::printf("FAIL %s: %s\n", manifest_path.c_str(), why.c_str());
        ++ctbrowser_test_failures;
        return;
    }
    available = *listed;
#endif
    const auto & asked = detail::css_inputs_asked();
    for (const auto & [kind, text] : asked) {
        if (!available.contains({kind, text})) {
            std::printf("FAIL %s does not list the %s input [%s]\n", manifest_path.c_str(),
                        detail::name_of(kind), detail::shown(text).c_str());
            ++ctbrowser_test_failures;
        }
    }
    for (const auto & [kind, text] : available) {
        if (!asked.contains({kind, text})) {
            std::printf("FAIL %s lists the %s input [%s], which the test never asks for\n",
                        manifest_path.c_str(), detail::name_of(kind), detail::shown(text).c_str());
            ++ctbrowser_test_failures;
        }
    }
}

} // namespace ctbrowser_test
