//===- CppEmitter.cpp - Compiled CSS as C++ data --------------------------===//
//
// Prints compiled_sheet and compiled_tokens as C++ constants. See
// ctcompile/CSS/CppEmitter.hpp.
//
//===----------------------------------------------------------------------===//

#include <ctcompile/CSS/CppEmitter.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace ctcompile::css {

namespace css = ctbrowser::style::css;
namespace style = ctbrowser::style;

namespace {

// --- values as C++ ------------------------------------------------------------

[[nodiscard]] std::string_view name_of(css::token_type t) {
    switch (t) {
    case css::token_type::eof: return "eof";
    case css::token_type::whitespace: return "whitespace";
    case css::token_type::ident: return "ident";
    case css::token_type::function: return "function";
    case css::token_type::at_keyword: return "at_keyword";
    case css::token_type::hash: return "hash";
    case css::token_type::string: return "string";
    case css::token_type::bad_string: return "bad_string";
    case css::token_type::url: return "url";
    case css::token_type::bad_url: return "bad_url";
    case css::token_type::number: return "number";
    case css::token_type::percentage: return "percentage";
    case css::token_type::dimension: return "dimension";
    case css::token_type::delim: return "delim";
    case css::token_type::cdo: return "cdo";
    case css::token_type::cdc: return "cdc";
    case css::token_type::colon: return "colon";
    case css::token_type::semicolon: return "semicolon";
    case css::token_type::comma: return "comma";
    case css::token_type::open_square: return "open_square";
    case css::token_type::close_square: return "close_square";
    case css::token_type::open_paren: return "open_paren";
    case css::token_type::close_paren: return "close_paren";
    case css::token_type::open_curly: return "open_curly";
    case css::token_type::close_curly: return "close_curly";
    }
    return "eof";
}

[[nodiscard]] std::string_view name_of(css::cv_kind k) {
    switch (k) {
    case css::cv_kind::token: return "token";
    case css::cv_kind::function: return "function";
    case css::cv_kind::block: return "block";
    }
    return "token";
}

[[nodiscard]] std::string_view name_of(style::combinator c) {
    switch (c) {
    case style::combinator::none: return "none";
    case style::combinator::descendant: return "descendant";
    case style::combinator::child: return "child";
    case style::combinator::next_sibling: return "next_sibling";
    case style::combinator::subsequent_sibling: return "subsequent_sibling";
    }
    return "none";
}

[[nodiscard]] std::string_view name_of(style::attr_op o) {
    switch (o) {
    case style::attr_op::present: return "present";
    case style::attr_op::exact: return "exact";
    case style::attr_op::includes: return "includes";
    case style::attr_op::dash: return "dash";
    case style::attr_op::prefix: return "prefix";
    case style::attr_op::suffix: return "suffix";
    case style::attr_op::substring: return "substring";
    }
    return "present";
}

[[nodiscard]] std::string_view name_of(style::ns_prefix n) {
    switch (n) {
    case style::ns_prefix::unset: return "unset";
    case style::ns_prefix::any: return "any";
    case style::ns_prefix::none: return "none";
    case style::ns_prefix::named: return "named";
    }
    return "unset";
}

[[nodiscard]] std::string_view name_of(style::pseudo_kind k) {
    switch (k) {
    case style::pseudo_kind::nth_child: return "nth_child";
    case style::pseudo_kind::nth_last_child: return "nth_last_child";
    case style::pseudo_kind::nth_of_type: return "nth_of_type";
    case style::pseudo_kind::nth_last_of_type: return "nth_last_of_type";
    case style::pseudo_kind::not_: return "not_";
    case style::pseudo_kind::is_: return "is_";
    case style::pseudo_kind::where_: return "where_";
    case style::pseudo_kind::lang: return "lang";
    case style::pseudo_kind::dir: return "dir";
    case style::pseudo_kind::heading: return "heading";
    }
    return "nth_child";
}

[[nodiscard]] std::string_view name_of(css::media_type t) {
    switch (t) {
    case css::media_type::all: return "all";
    case css::media_type::screen: return "screen";
    case css::media_type::print: return "print";
    case css::media_type::other: return "other";
    }
    return "all";
}

/// The C++ spelling of an enumerator: the scope the tables' aliases give it.
[[nodiscard]] std::string cpp(css::token_type v) {
    return std::format("css::token_type::{}", name_of(v));
}
[[nodiscard]] std::string cpp(css::cv_kind v) {
    return std::format("css::cv_kind::{}", name_of(v));
}
[[nodiscard]] std::string cpp(style::combinator v) {
    return std::format("style::combinator::{}", name_of(v));
}
[[nodiscard]] std::string cpp(style::attr_op v) {
    return std::format("style::attr_op::{}", name_of(v));
}
[[nodiscard]] std::string cpp(style::ns_prefix v) {
    return std::format("style::ns_prefix::{}", name_of(v));
}
[[nodiscard]] std::string cpp(style::pseudo_kind v) {
    return std::format("style::pseudo_kind::{}", name_of(v));
}
[[nodiscard]] std::string cpp(css::media_type v) {
    return std::format("css::media_type::{}", name_of(v));
}
[[nodiscard]] std::string cpp(bool v) {
    return v ? "true" : "false";
}
[[nodiscard]] std::string cpp(std::uint8_t v) {
    return std::to_string(v);
}
[[nodiscard]] std::string cpp(std::uint16_t v) {
    return std::to_string(v);
}
[[nodiscard]] std::string cpp(std::uint32_t v) {
    return std::to_string(v);
}
[[nodiscard]] std::string cpp(std::uint64_t v) {
    return std::to_string(v);
}
[[nodiscard]] std::string cpp(std::int32_t v) {
    // INT32_MIN cannot be written as a negated literal: 2147483648 is not an int.
    if (v == INT32_MIN) { return "(-2147483647 - 1)"; }
    return std::to_string(v);
}
[[nodiscard]] std::string cpp(double v) {
    if (std::isnan(v)) { return "std::numeric_limits<double>::quiet_NaN()"; }
    if (std::isinf(v)) {
        return v < 0 ? "-std::numeric_limits<double>::infinity()"
                     : "std::numeric_limits<double>::infinity()";
    }
    // Shortest round-trip form, so the constant is the same double.
    std::string text = std::format("{}", v);
    if (text.find_first_of(".en") == std::string::npos) { text += ".0"; }
    return text;
}
[[nodiscard]] std::string cpp(char v) {
    if (v == '\0') { return "'\\0'"; }
    if (v == '\'' || v == '\\') { return std::format("'\\{}'", v); }
    if (v >= 0x20 && v < 0x7f) { return std::format("'{}'", v); }
    return std::format("'\\{:03o}'", static_cast<unsigned char>(v));
}
[[nodiscard]] std::string cpp(css::compiled_text v) {
    return std::format("{{.offset = {}, .length = {}}}", v.offset, v.length);
}
[[nodiscard]] std::string cpp(css::compiled_range v) {
    return std::format("{{.first = {}, .count = {}}}", v.first, v.count);
}

[[nodiscard]] bool same(css::compiled_text a, css::compiled_text b) {
    return a.offset == b.offset && a.length == b.length;
}
[[nodiscard]] bool same(css::compiled_range a, css::compiled_range b) {
    return a.first == b.first && a.count == b.count;
}
template <class T> [[nodiscard]] bool same(const T & a, const T & b) {
    return a == b;
}

/// One designated initializer, `{.a = 1, .c = true}`: a field is written only
/// when it differs from the record's default, so most tokens are a type, an
/// offset and a length.
class record {
public:
    explicit record(std::ostream & out) : out_(out) { out_ << "    {"; }
    record(const record &) = delete;
    record & operator=(const record &) = delete;
    ~record() { out_ << "},\n"; }

    template <class T> record & field(std::string_view name, const T & value, const T & initial) {
        if (same(value, initial)) { return *this; }
        out_ << (first_ ? "." : ", .") << name << " = " << cpp(value);
        first_ = false;
        return *this;
    }

private:
    std::ostream & out_;
    bool first_ = true;
};

/// Writes `bytes` as a string literal, split over lines. Anything outside
/// printable ASCII, and the quote and backslash, is a three-digit octal escape,
/// which, unlike `\x`, cannot run into the character after it.
void literal(std::ostream & out, std::string_view bytes) {
    constexpr std::size_t per_line = 96;
    std::size_t on_line = 0;
    out << "\"";
    for (const char c : bytes) {
        if (on_line >= per_line) {
            out << "\"\n    \"";
            on_line = 0;
        }
        const auto u = static_cast<unsigned char>(c);
        if (u >= 0x20 && u < 0x7f && c != '"' && c != '\\') {
            out << c;
            ++on_line;
        } else {
            out << std::format("\\{:03o}", u);
            on_line += 4;
        }
    }
    out << "\"";
}

/// The text of `chunks` joined, as one string.
[[nodiscard]] std::string joined(css::text_chunks chunks) {
    std::string out;
    for (const std::string_view chunk : chunks) { out += chunk; }
    return out;
}

/// One table. An empty table is not written, since C++ has no empty array; the
/// sheet's span is then left empty.
template <class T, class Write>
void table(std::ostream & out, std::string_view type, std::string_view name,
           std::span<const T> rows, Write write) {
    if (rows.empty()) { return; }
    out << "inline constexpr " << type << " " << name << "[] = {\n";
    for (const T & row : rows) { write(row); }
    out << "};\n\n";
}

void write(std::ostream & out, const css::css_token & t) {
    const css::css_token d{};
    record{out}
        .field("type", t.type, d.type)
        .field("flags", t.flags, d.flags)
        .field("unit_length", t.unit_length, d.unit_length)
        .field("text", t.text, d.text)
        .field("length", t.length, d.length)
        .field("number", t.number, d.number);
}

} // namespace

/// `inline constexpr std::string_view <name>[] = {{"...", n}, ...};`: the text
/// as chunks of at most 32 KiB, each one literal with its length, which keeps
/// any NUL. A single literal past 64 KiB is longer than compilers are required
/// to accept (-Woverlength-strings), and joining the chunks into one array at
/// compile time would copy every byte in constant evaluation, which runs into
/// the compilers' evaluation limits on a large sheet; css::materialize joins
/// them at load instead. Empty text writes nothing and its span stays empty.
/// Returns whether a table was written.
bool emit_text(std::ostream & out, std::string_view name, std::string_view bytes) {
    constexpr std::size_t chunk_size = 32 * 1024;
    if (bytes.empty()) { return false; }
    out << "inline constexpr std::string_view " << name << "[] = {\n";
    for (std::size_t at = 0; at < bytes.size(); at += chunk_size) {
        const std::string_view chunk = bytes.substr(at, chunk_size);
        out << "    {";
        literal(out, chunk);
        out << ",\n     " << chunk.size() << "},\n";
    }
    out << "};\n\n";
    return true;
}

void open_file(std::ostream & out, std::string_view source, std::string_view sha256,
               std::string_view ns) {
    out << "// Generated by ctcss-translate from " << source << "\n// (sha256 " << sha256
        << "). Do not edit: the build regenerates it.\n\n"
        << "#pragma once\n\n"
        << "#include <limits>\n#include <string_view>\n\n"
        << "#include <ctbrowser/style/css/compiled.hpp>\n\n"
        << "namespace " << ns << " {\n\n"
        << "namespace css = ctbrowser::style::css;\n"
        << "namespace style = ctbrowser::style;\n\n";
}

void close_file(std::ostream & out, std::string_view ns) {
    out << "} // namespace " << ns << "\n";
}

void emit_tokens(std::ostream & out, const css::compiled_tokens & tokens, std::string_view name) {
    const std::string n{name};
    const bool pool = emit_text(out, n + "_pool", joined(tokens.pool));
    table(out, "css::css_token", n + "_tokens", tokens.tokens,
          [&](const css::css_token & t) { write(out, t); });
    out << "inline constexpr css::compiled_tokens " << n << "{\n";
    if (pool) { out << "    .pool = " << n << "_pool,\n"; }
    if (!tokens.tokens.empty()) { out << "    .tokens = " << n << "_tokens,\n"; }
    out << "    .source_length = " << tokens.source_length << ",\n};\n\n";
}

void emit_sheet(std::ostream & out, const css::compiled_sheet & s, std::string_view name) {
    const std::string n{name};
    const bool pool = emit_text(out, n + "_pool", joined(s.pool));
    const bool strings = emit_text(out, n + "_strings", joined(s.strings));

    table(out, "css::compiled_namespace", n + "_namespaces", s.namespaces,
          [&](const css::compiled_namespace & r) {
              const css::compiled_namespace d{};
              record{out}.field("prefix", r.prefix, d.prefix).field("uri", r.uri, d.uri);
          });
    table(out, "css::compiled_import", n + "_imports", s.imports,
          [&](const css::compiled_import & r) {
              const css::compiled_import d{};
              record{out}
                  .field("href", r.href, d.href)
                  .field("media", r.media, d.media)
                  .field("layer", r.layer, d.layer)
                  .field("layered", r.layered, d.layered)
                  .field("supports", r.supports, d.supports)
                  .field("scope", r.scope, d.scope)
                  .field("scoped", r.scoped, d.scoped)
                  .field("begin", r.begin, d.begin)
                  .field("end", r.end, d.end);
          });
    table(out, "css::css_token", n + "_tokens", s.tokens,
          [&](const css::css_token & t) { write(out, t); });
    table(out, "css::component_value", n + "_values", s.values,
          [&](const css::component_value & r) {
              const css::component_value d{};
              record{out}
                  .field("kind", r.kind, d.kind)
                  .field("open", r.open, d.open)
                  .field("too_deep", r.too_deep, d.too_deep)
                  .field("pad", r.pad, d.pad)
                  .field("token", r.token, d.token)
                  .field("end_token", r.end_token, d.end_token)
                  .field("first_child", r.first_child, d.first_child)
                  .field("child_count", r.child_count, d.child_count);
          });
    table(out, "css::compiled_complex", n + "_selectors", s.selectors,
          [&](const css::compiled_complex & r) {
              const css::compiled_complex d{};
              record{out}
                  .field("parts", r.parts, d.parts)
                  .field("links", r.links, d.links)
                  .field("specificity", r.specificity, d.specificity)
                  .field("explicit_scope", r.explicit_scope, d.explicit_scope);
          });
    table(out, "css::compiled_compound", n + "_compounds", s.compounds,
          [&](const css::compiled_compound & r) {
              const css::compiled_compound d{};
              record{out}
                  .field("tag", r.tag, d.tag)
                  .field("tag_exact", r.tag_exact, d.tag_exact)
                  .field("id", r.id, d.id)
                  .field("classes", r.classes, d.classes)
                  .field("attributes", r.attributes, d.attributes)
                  .field("states", r.states, d.states)
                  .field("structural", r.structural, d.structural)
                  .field("states_after_pe", r.states_after_pe, d.states_after_pe)
                  .field("structural_after_pe", r.structural_after_pe, d.structural_after_pe)
                  .field("pseudos", r.pseudos, d.pseudos)
                  .field("ns", r.ns, d.ns)
                  .field("ns_name", r.ns_name, d.ns_name)
                  .field("ns_uri", r.ns_uri, d.ns_uri)
                  .field("pseudo_element", r.pseudo_element, d.pseudo_element)
                  .field("nesting", r.nesting, d.nesting)
                  .field("never_matches", r.never_matches, d.never_matches)
                  .field("dropped", r.dropped, d.dropped);
          });
    table(out, "style::combinator", n + "_combinators", s.combinators,
          [&](style::combinator c) { out << "    " << cpp(c) << ",\n"; });
    table(out, "css::compiled_attribute", n + "_attributes", s.attributes,
          [&](const css::compiled_attribute & r) {
              const css::compiled_attribute d{};
              record{out}
                  .field("name", r.name, d.name)
                  .field("name_exact", r.name_exact, d.name_exact)
                  .field("value", r.value, d.value)
                  .field("op", r.op, d.op)
                  .field("case_insensitive", r.case_insensitive, d.case_insensitive)
                  .field("case_sensitive_flag", r.case_sensitive_flag, d.case_sensitive_flag)
                  .field("ns", r.ns, d.ns)
                  .field("ns_uri", r.ns_uri, d.ns_uri);
          });
    table(out, "css::compiled_pseudo", n + "_pseudos", s.pseudos,
          [&](const css::compiled_pseudo & r) {
              const css::compiled_pseudo d{};
              record{out}
                  .field("kind", r.kind, d.kind)
                  .field("a", r.a, d.a)
                  .field("b", r.b, d.b)
                  .field("args", r.args, d.args)
                  .field("relative", r.relative, d.relative)
                  .field("nesting", r.nesting, d.nesting)
                  .field("ranges", r.ranges, d.ranges)
                  .field("levels", r.levels, d.levels);
          });
    table(out, "css::compiled_text", n + "_texts", s.texts,
          [&](css::compiled_text t) { out << "    " << cpp(t) << ",\n"; });
    table(out, "std::int32_t", n + "_levels", s.levels,
          [&](std::int32_t v) { out << "    " << cpp(v) << ",\n"; });
    table(out, "css::compiled_declaration", n + "_declarations", s.declarations,
          [&](const css::compiled_declaration & r) {
              const css::compiled_declaration d{};
              record{out}
                  .field("property", r.property, d.property)
                  .field("first_value", r.first_value, d.first_value)
                  .field("value_count", r.value_count, d.value_count)
                  .field("text", r.text, d.text)
                  .field("length", r.length, d.length)
                  .field("order", r.order, d.order)
                  .field("important", r.important, d.important)
                  .field("custom", r.custom, d.custom);
          });
    table(out, "css::raw_rule", n + "_rules", s.rules, [&](const css::raw_rule & r) {
        const css::raw_rule d{};
        record{out}
            .field("first_selector", r.first_selector, d.first_selector)
            .field("selector_count", r.selector_count, d.selector_count)
            .field("first_declaration", r.first_declaration, d.first_declaration)
            .field("declaration_count", r.declaration_count, d.declaration_count)
            .field("condition", r.condition, d.condition)
            .field("layer", r.layer, d.layer)
            .field("scope", r.scope, d.scope)
            .field("container", r.container, d.container);
    });
    table(out, "css::font_face", n + "_font_faces", s.font_faces, [&](const css::font_face & r) {
        const css::font_face d{};
        record{out}
            .field("first_declaration", r.first_declaration, d.first_declaration)
            .field("declaration_count", r.declaration_count, d.declaration_count);
    });
    const auto at_rule = [&](const css::at_rule_block & r) {
        const css::at_rule_block d{};
        record{out}
            .field("prelude_first", r.prelude_first, d.prelude_first)
            .field("prelude_count", r.prelude_count, d.prelude_count)
            .field("first_declaration", r.first_declaration, d.first_declaration)
            .field("declaration_count", r.declaration_count, d.declaration_count);
    };
    table(out, "css::at_rule_block", n + "_properties", s.properties, at_rule);
    table(out, "css::at_rule_block", n + "_functions", s.functions, at_rule);
    table(out, "css::compiled_keyframes", n + "_keyframes", s.keyframes,
          [&](const css::compiled_keyframes & r) {
              const css::compiled_keyframes d{};
              record{out}
                  .field("name", r.name, d.name)
                  .field("condition", r.condition, d.condition)
                  .field("layer", r.layer, d.layer)
                  .field("frames", r.frames, d.frames);
          });
    table(out, "css::compiled_keyframe", n + "_keyframe_blocks", s.keyframe_blocks,
          [&](const css::compiled_keyframe & r) {
              const css::compiled_keyframe d{};
              record{out}
                  .field("offsets", r.offsets, d.offsets)
                  .field("first_declaration", r.first_declaration, d.first_declaration)
                  .field("declaration_count", r.declaration_count, d.declaration_count);
          });
    table(out, "double", n + "_offsets", s.offsets,
          [&](double v) { out << "    " << cpp(v) << ",\n"; });
    table(
        out, "css::compiled_media_condition", n + "_conditions", s.conditions,
        [&](const css::compiled_media_condition & r) {
            const css::compiled_media_condition d{};
            record{out}.field("parent", r.parent, d.parent).field("queries", r.queries, d.queries);
        });
    table(out, "css::compiled_media_query", n + "_media_queries", s.media_queries,
          [&](const css::compiled_media_query & r) {
              const css::compiled_media_query d{};
              record{out}
                  .field("type", r.type, d.type)
                  .field("negated", r.negated, d.negated)
                  .field("malformed", r.malformed, d.malformed)
                  .field("condition", r.condition, d.condition);
          });
    table(out, "css::compiled_text", n + "_layers", s.layers,
          [&](css::compiled_text t) { out << "    " << cpp(t) << ",\n"; });
    table(out, "css::scope_block", n + "_scopes", s.scopes, [&](const css::scope_block & r) {
        const css::scope_block d{};
        record{out}
            .field("parent", r.parent, d.parent)
            .field("first_root", r.first_root, d.first_root)
            .field("root_count", r.root_count, d.root_count)
            .field("first_limit", r.first_limit, d.first_limit)
            .field("limit_count", r.limit_count, d.limit_count)
            .field("implicit_root", r.implicit_root, d.implicit_root);
    });
    table(
        out, "css::compiled_container_condition", n + "_containers", s.containers,
        [&](const css::compiled_container_condition & r) {
            const css::compiled_container_condition d{};
            record{out}.field("parent", r.parent, d.parent).field("queries", r.queries, d.queries);
        });
    table(
        out, "css::compiled_container_query", n + "_container_queries", s.container_queries,
        [&](const css::compiled_container_query & r) {
            const css::compiled_container_query d{};
            record{out}.field("name", r.name, d.name).field("condition", r.condition, d.condition);
        });

    // The sheet, naming each table that was written.
    const css::compiled_sheet d{};
    out << "inline constexpr css::compiled_sheet " << n << "{\n";
    if (pool) { out << "    .pool = " << n << "_pool,\n"; }
    if (strings) { out << "    .strings = " << n << "_strings,\n"; }
    if (s.prefixes_checked != d.prefixes_checked) {
        out << "    .prefixes_checked = " << cpp(s.prefixes_checked) << ",\n";
    }
    if (s.forgiving != d.forgiving) { out << "    .forgiving = " << cpp(s.forgiving) << ",\n"; }
    out << "    .source_length = " << s.source_length << ",\n";
    const auto span_field = [&](std::string_view field, std::size_t size) {
        if (size != 0) { out << "    ." << field << " = " << n << "_" << field << ",\n"; }
    };
    span_field("namespaces", s.namespaces.size());
    span_field("imports", s.imports.size());
    span_field("tokens", s.tokens.size());
    span_field("values", s.values.size());
    span_field("selectors", s.selectors.size());
    out << "    .selector_count = " << s.selector_count << ",\n";
    span_field("compounds", s.compounds.size());
    span_field("combinators", s.combinators.size());
    span_field("attributes", s.attributes.size());
    span_field("pseudos", s.pseudos.size());
    span_field("texts", s.texts.size());
    span_field("levels", s.levels.size());
    span_field("declarations", s.declarations.size());
    span_field("rules", s.rules.size());
    span_field("font_faces", s.font_faces.size());
    span_field("properties", s.properties.size());
    span_field("functions", s.functions.size());
    span_field("keyframes", s.keyframes.size());
    span_field("keyframe_blocks", s.keyframe_blocks.size());
    span_field("offsets", s.offsets.size());
    span_field("conditions", s.conditions.size());
    span_field("media_queries", s.media_queries.size());
    span_field("layers", s.layers.size());
    span_field("scopes", s.scopes.size());
    span_field("containers", s.containers.size());
    span_field("container_queries", s.container_queries.size());
    out << "};\n\n";
}

} // namespace ctcompile::css
