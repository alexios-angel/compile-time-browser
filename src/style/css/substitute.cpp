#include <ctbrowser/style/css/substitute.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/token.hpp>

namespace ctbrowser::style::css {
namespace {

// §3: twenty substitutions deep is the spec's limit, and a byte budget as well -
// `--a: var(--b) var(--b)` doubles on every level, so a depth limit alone still
// allows an exponential blow-up.
constexpr int max_depth = 20;
constexpr std::size_t max_bytes = 64 * 1024;

// One `var()` call, located inside a token stream.
struct var_call {
    std::size_t open = 0;      // the `var(` token
    std::size_t close = 0;     // the matching `)`
    std::size_t name_at = 0;   // the `--x` ident
    std::size_t comma_at = 0;  // the first top-level comma, or `close` if none
};

[[nodiscard]] bool is_var_function(const token_stream & s, const css_token & t) {
    if (t.type != token_type::function) { return false; }
    std::string_view name = s.text_of(t);
    if (!name.empty() && name.back() == '(') { name.remove_suffix(1); }
    return ascii_iequals(name, "var");
}

// The first `var()` in the stream, at the OUTERMOST level it appears at. Outermost
// because substituting an inner one first would rewrite text the outer one is about
// to replace wholesale.
[[nodiscard]] bool find_var(const token_stream & s, var_call & out) {
    for (std::size_t i = 0; i < s.tokens.size(); ++i) {
        if (!is_var_function(s, s.tokens[i])) { continue; }
        out = var_call{};
        out.open = i;
        int depth = 1;
        std::size_t j = i + 1;
        bool have_name = false;
        for (; j < s.tokens.size() && depth > 0; ++j) {
            const css_token & t = s.tokens[j];
            if (t.type == token_type::function || t.type == token_type::open_paren) { ++depth; }
            if (t.type == token_type::close_paren) {
                --depth;
                if (depth == 0) { break; }
            }
            if (depth != 1) { continue; }
            if (!have_name && t.type == token_type::ident) {
                out.name_at = j;
                have_name = true;
            }
            if (out.comma_at == 0 && t.type == token_type::comma) { out.comma_at = j; }
        }
        out.close = j < s.tokens.size() ? j : s.tokens.size() - 1;
        if (out.comma_at == 0) { out.comma_at = out.close; }
        if (!have_name) { return false; } // `var()` with no name is invalid
        return true;
    }
    return false;
}

[[nodiscard]] std::string text_between(const token_stream & s, std::size_t first,
                                       std::size_t last) {
    std::string out;
    for (std::size_t i = first; i < last; ++i) { out += s.text_of(s.tokens[i]); }
    return out;
}

// A substituted value must not have gained a top-level `!important` or `;`. §3 says a
// var() that expands to either is invalid, and the reason is structural rather than
// pedantic: the declaration's priority and its extent were both decided before
// substitution, so a value that changes them afterwards has escaped the grammar.
[[nodiscard]] bool introduced_structure(std::string_view value) {
    const token_stream s = tokenize(value);
    int depth = 0;
    for (const css_token & t : s.tokens) {
        if (t.type == token_type::function || t.type == token_type::open_paren ||
            t.type == token_type::open_square || t.type == token_type::open_curly) {
            ++depth;
        } else if (t.type == token_type::close_paren || t.type == token_type::close_square ||
                   t.type == token_type::close_curly) {
            --depth;
        } else if (depth == 0) {
            if (t.type == token_type::semicolon) { return true; }
            if (t.type == token_type::delim && s.text_of(t) == "!") { return true; }
        }
    }
    return false;
}

class substituter {
public:
    substituter(const custom_lookup & lookup, atom_table & atoms)
        : lookup_(&lookup), atoms_(&atoms) {}

    [[nodiscard]] bool run(std::string_view value, std::string & out, int depth) {
        if (depth > max_depth || value.size() > max_bytes) { return false; }
        const token_stream s = tokenize(value);
        var_call call;
        if (!find_var(s, call)) {
            out.assign(value);
            return true;
        }
        // Everything before the call, unchanged.
        std::string result = text_between(s, 0, call.open);

        const std::string_view name_text = s.text_of(s.tokens[call.name_at]);
        // A custom property's name must start with `--`; anything else in that
        // position is not a custom property and the call is invalid.
        if (!name_text.starts_with("--")) { return false; }
        const atom name = atoms_->intern(name_text);

        // CYCLE DETECTION. `--a: var(--b); --b: var(--a)` must make BOTH invalid rather
        // than recursing to the depth limit, and a set of the properties currently
        // being resolved is what says so.
        const bool cyclic = std::find(resolving_.begin(), resolving_.end(), name.id) !=
                            resolving_.end();
        std::string expansion;
        bool ok = false;
        if (!cyclic) {
            if (const std::optional<std::string_view> held = (*lookup_)(name)) {
                resolving_.push_back(name.id);
                // A custom property's own value may itself contain var().
                ok = run(*held, expansion, depth + 1);
                resolving_.pop_back();
            }
        }
        if (!ok) {
            // THE FALLBACK is everything after the FIRST comma, commas included:
            // `var(--a, 1px, 2px)` has the fallback `1px, 2px`, because a custom
            // property's value may itself be a comma list.
            if (call.comma_at >= call.close) { return false; } // no fallback: invalid
            const std::string fallback = text_between(s, call.comma_at + 1, call.close);
            std::string trimmed{trim(fallback, html_whitespace)};
            std::string expanded_fallback;
            if (!run(trimmed, expanded_fallback, depth + 1)) { return false; }
            expansion = std::move(expanded_fallback);
        }
        result += expansion;
        // And everything after the call, which may contain more var()s - so the
        // remainder is substituted rather than copied.
        const std::string tail = text_between(s, call.close + 1, s.tokens.size());
        std::string expanded_tail;
        if (!run(tail, expanded_tail, depth + 1)) { return false; }
        result += expanded_tail;
        if (result.size() > max_bytes) { return false; }
        out = std::move(result);
        return true;
    }

private:
    const custom_lookup * lookup_;
    atom_table * atoms_;
    // The properties currently being expanded, innermost last. A vector rather than a
    // set because it is never more than a handful deep and a linear scan of four
    // integers beats hashing one.
    std::vector<std::uint32_t> resolving_;
};

} // namespace

bool may_have_var(std::string_view value) noexcept {
    return value.find("var(") != std::string_view::npos ||
           value.find("VAR(") != std::string_view::npos ||
           value.find("Var(") != std::string_view::npos;
}

std::optional<std::string> substitute_var(std::string_view value, const custom_lookup & lookup,
                                         atom_table & atoms) {
    substituter engine{lookup, atoms};
    std::string out;
    if (!engine.run(value, out, 0)) { return std::nullopt; }
    if (introduced_structure(out)) { return std::nullopt; }
    return out;
}

} // namespace ctbrowser::style::css
