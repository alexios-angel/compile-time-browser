#include "evaluator/internal.hpp"

namespace ctbrowser::style::css {

using namespace evaluator_detail;

// THE RANDOM BASE, CSS Values 5 §random-caching: a number in [0, 1) that is
// the same every time the same KEY asks for it, so a page reflows to the same
// random layout it first had. The key is what the sharing options say
// (`random_options`, internal.hpp) - hashed (FNV-1a) into the mantissa of a
// double. Deterministic on purpose: the render goldens are byte-compared and
// `Math.random` is seeded for the same reason.
[[nodiscard]] double random_base(std::string_view options, const length_context & ctx) {
    const random_key sharing = random_options(options, ctx.property, ctx.random_index);
    std::string key = sharing.name + '|' + sharing.ua;
    if (sharing.element_scoped) { key += '|' + std::to_string(ctx.element_key); }
    std::uint64_t hash = 14695981039346656037ull;
    for (const char c : key) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }
    // A final mix so a one-character difference reaches every bit.
    hash ^= hash >> 29;
    hash *= 0xbf58476d1ce4e5b9ull;
    hash ^= hash >> 32;
    return static_cast<double>(hash >> 11) / 9007199254740992.0; // 2^53
}

namespace detail {

random_key random_options(std::string_view options, std::string_view property, std::size_t index) {
    random_key out;
    bool property_scoped = false;
    bool property_index_scoped = false;
    for (const std::string_view word : split_top_level(options, " \t\n\r\f")) {
        if (word.starts_with("--")) {
            out.name = std::string{word};
        } else if (ascii_istarts_with(word, "ua-")) {
            out.ua = ascii_lower_copy(word);
        } else if (ascii_iequals(word, "element-scoped")) {
            out.element_scoped = true;
        } else if (ascii_iequals(word, "property-scoped")) {
            property_scoped = true;
        } else if (ascii_iequals(word, "property-index-scoped")) {
            property_index_scoped = true;
        }
    }
    if (property_scoped) {
        out.ua = "ua-" + std::string{property};
    } else if (property_index_scoped) {
        out.ua = "ua-" + std::string{property} + '-' + std::to_string(index + 1);
    } else if (out.name.empty() && out.ua.empty() && !out.element_scoped) {
        out.ua = "ua-" + std::string{property} + '-' + std::to_string(index + 1);
        out.element_scoped = true;
    }
    return out;
}

number_symbols_scope::number_symbols_scope(std::span<const std::string_view> names) noexcept {
    number_symbols = names;
}
number_symbols_scope::~number_symbols_scope() {
    number_symbols = {};
}
bool is_number_symbol(std::string_view key) noexcept {
    return key.starts_with('$');
}

// One expression with NO bases at all - which is what a specified value is
// written against - and its answer as a term rather than as a `calc_result`,
// because a sum of units that could not be added has no single magnitude.
[[nodiscard]] std::pair<math_outcome, term> evaluate_symbolic(std::string_view expression,
                                                              bool size_symbol) {
    const token_stream tokens = tokenize(expression);
    const length_context none; // deliberately unused: nothing is measured here
    evaluator run{tokens, none, basis::symbolic, size_symbol};
    return run.run_symbolic();
}

} // namespace detail

math_answer evaluate_math(std::string_view expression, const length_context & ctx) {
    const token_stream tokens = tokenize(expression);
    evaluator run{tokens, ctx};
    return run.run();
}

std::optional<calc_result> math_type_of(std::string_view expression) {
    const auto [outcome, sum] = detail::evaluate_symbolic(expression);
    if (outcome != math_outcome::resolved || !sum.simple()) { return std::nullopt; }
    calc_result out;
    out.type = sum.type();
    out.is_number = sum.is_number();
    out.has_percent = sum.has_percent;
    return out;
}

} // namespace ctbrowser::style::css
