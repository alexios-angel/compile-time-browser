#pragma once

#include <ctbrowser/style/css/substitute.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/boolean.hpp>
#include <ctbrowser/style/css/calc.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <ctbrowser/style/css/token.hpp>

namespace ctbrowser::style::css::substitute_detail {

// §3: twenty substitutions deep is the spec's limit, and a byte budget as well -
// `--a: var(--b) var(--b)` doubles on every level, so a depth limit alone still
// allows an exponential blow-up.
inline constexpr int max_depth = 20;

inline constexpr std::size_t max_bytes = 64 * 1024;

// One substitution function, located inside a token stream. `var()` and
// `attr()` share the shape: a name, an optional type (attr only), and a
// fallback after the first top-level comma.
struct call {
    bool is_attr = false;
    bool is_if = false;
    bool is_ident = false;
    bool is_random_item = false;
    bool is_inherit = false; // `inherit(--x, fallback)`: var()'s shape, the parent's value
    bool is_custom = false;  // `--name(args)`: a custom function, CSS Functions and Mixins 1
    // A var() whose name position holds a FUNCTION - `var(ident("--" "x"))` -
    // which is read only once that function has been substituted.
    bool name_is_call = false;
    std::size_t open = 0;     // the `var(`, `attr(`, `if(` or `ident(` token
    std::size_t close = 0;    // the matching `)`, or the eof token if none
    std::size_t name_at = 0;  // the `--x` ident
    std::size_t comma_at = 0; // the first top-level comma, or `close` if none
};

[[nodiscard]] bool is_function_named(const token_stream & s, const css_token & t,
                                     std::string_view want);

[[nodiscard]] bool find_call(const token_stream & s, call & out);

[[nodiscard]] std::string text_between(const token_stream & s, std::size_t first, std::size_t last);

[[nodiscard]] bool introduced_structure(std::string_view value);

// --- attr(), CSS Values 5 §attr ------------------------------------------------
//
//   attr( <attr-name> <attr-type>? , <declaration-value>? )
//   <attr-type> = type( <syntax> ) | raw-string | <attr-unit>
//
// THE ATTRIBUTE'S TEXT IS NOT CSS UNTIL THE TYPE SAYS SO. With no type, or
// `raw-string`, the substitution is a <string> holding the text verbatim - which
// is why `content: attr(title)` has always been safe. With a unit, the text
// must be a bare number and gains the unit. With `type()`, the text is parsed
// against the syntax and substituted only if it matches; what does not match
// takes the fallback, and without one the declaration is invalid at
// computed-value time. A malformed argument list - `attr(!)`, a `<syntax>`
// naming a type that does not exist - is a syntax error whatever the attribute
// holds.

// One alternative of a `<syntax>`: a type name, or a keyword.
struct syntax_alternative {
    std::string name; // `length`, `number`, ... or the keyword itself
    bool keyword = false;
    char multiplier = 0; // 0, '+' (space list) or '#' (comma list)
};

// The type names `type()` may ask for. `<url>` is NOT here on purpose: an
// attribute may not produce a URL (§attr's security note), so asking for one
// is a syntax error rather than a type this cannot match. `<image>` is - a
// gradient is an image with no URL in it - and a `url()` in one is refused
// for the same reason.
inline constexpr std::string_view syntax_types[] = {
    "length", "number", "percentage", "length-percentage",  "color",        "integer",
    "angle",  "time",   "resolution", "transform-function", "custom-ident", "transform-list",
    "string", "image",  "frequency"};

[[nodiscard]] std::optional<std::vector<syntax_alternative>> parse_syntax(std::string_view text);

// The significant tokens of a stream, as index ranges of the items a
// multiplier separates: the whole value, its space-separated parts, or its
// comma-separated parts, each [first, last] over significant tokens with
// brackets kept whole.
struct item {
    std::size_t first = 0;
    std::size_t last = 0; // inclusive
};

[[nodiscard]] std::vector<item> items_of(const token_stream & s, char multiplier);

[[nodiscard]] std::string slice_of(const token_stream & s, const item & it);

[[nodiscard]] std::optional<std::string> match_item(const token_stream & s, const item & it,
                                                    std::string_view type);

[[nodiscard]] std::optional<std::string> match_syntax(
    std::string_view value, const std::vector<syntax_alternative> & syntax);

[[nodiscard]] std::string quoted(std::string_view text);

[[nodiscard]] std::vector<std::pair<token_type, std::string>> significant(std::string_view text);

class substituter {
public:
    substituter(const custom_lookup & lookup, atom_table & atoms,
                const attribute_lookup & attributes, const condition_environment * conditions)
        : lookup_(&lookup), atoms_(&atoms), attributes_(&attributes), conditions_(conditions) {}

    // TWO TOKEN STREAMS JOINED STAY TWO TOKENS. `if(style(--x): a)if(style(--x):
    // b)` substitutes to `a` beside `b`, which read back as one token `ab`;
    // CSS Syntax 3 §9 puts an empty comment between two tokens that would
    // otherwise merge, and so does every engine's serialisation of a custom
    // property (if-conditionals).
    static void join(std::string & left, std::string_view right) {
        const auto name_char = [](char c) { return is_name(c) || c == '.' || c == '%'; };
        if (!left.empty() && !right.empty() && name_char(left.back()) && name_char(right.front())) {
            left += "/**/";
        }
        left += right;
    }

    // THE PROPERTY THIS VALUE BELONGS TO, when it is a custom one, so that a
    // style query reading it back is a cycle: `--x: if(style(--x): a; else: b)`.
    void resolving(atom property) {
        self_ = property;
        resolving_.push_back(property.id);
    }
    [[nodiscard]] bool reached_self() const noexcept { return root_cycle_; }

    // ONE MATH FUNCTION, against the caller's bases, with the value's
    // random() functions numbered in order across every expression this
    // substitution evaluates: `style(random(0, 1) = random(0, 1))` draws
    // twice, and two `if()`s in one value draw a first and a second
    // (random-in-if).
    [[nodiscard]] math_answer math_of(std::string_view text) {
        length_context ctx = conditions_ != nullptr ? conditions_->lengths : length_context{};
        if (ctx.property.empty() && conditions_ != nullptr) {
            ctx.property = conditions_->property;
        }
        ctx.random_index += randoms_seen_;
        const math_answer answer = evaluate_math(text, ctx);
        randoms_seen_ += answer.randoms;
        return answer;
    }

    [[nodiscard]] bool run(std::string_view value, std::string & out, int depth) {
        if (depth > max_depth || value.size() > max_bytes) { return false; }
        const token_stream s = tokenize(value);
        call found;
        if (!find_call(s, found)) {
            // No call, or a `var()` with no name - which is a call and invalid.
            // Telling the two apart is one scan for the function token.
            for (const css_token & t : s.tokens) {
                if (is_function_named(s, t, "var")) { return false; }
            }
            out.assign(value);
            return true;
        }
        // AN attr() WITHOUT AN ELEMENT IS LEFT AS WRITTEN, and so is an if()
        // without an environment: the text survives and whoever reads it
        // decides. Everything after it is still substituted.
        if ((found.is_attr && !*attributes_) || (found.is_if && conditions_ == nullptr) ||
            (found.is_custom && (conditions_ == nullptr || !conditions_->functions))) {
            std::string result = text_between(s, 0, found.close + 1);
            std::string expanded_tail;
            if (!run(text_between(s, found.close + 1, s.tokens.size()), expanded_tail, depth + 1)) {
                return false;
            }
            out = std::move(result) + std::move(expanded_tail);
            return true;
        }
        // Everything before the call, unchanged.
        std::string result = text_between(s, 0, found.open);
        std::string expansion;
        if (found.is_attr) {
            if (!attribute(s, found, depth, expansion)) { return false; }
        } else if (found.is_if) {
            if (!conditional(s, found, depth, expansion)) { return false; }
        } else if (found.is_ident) {
            if (!identifier(s, found, depth, expansion)) { return false; }
        } else if (found.is_random_item) {
            if (!random_item(s, found, depth, expansion)) { return false; }
        } else if (found.is_inherit) {
            if (!inherited_value(s, found, depth, expansion)) { return false; }
        } else if (found.is_custom) {
            if (!custom_call(s, found, depth, expansion)) { return false; }
        } else if (!variable(s, found, depth, expansion)) {
            return false;
        }
        join(result, expansion);
        // And everything after the call, which may contain more calls - so the
        // remainder is substituted rather than copied.
        const std::string tail = text_between(s, found.close + 1, s.tokens.size());
        std::string expanded_tail;
        if (!run(tail, expanded_tail, depth + 1)) { return false; }
        join(result, expanded_tail);
        if (result.size() > max_bytes) { return false; }
        out = std::move(result);
        return true;
    }

private:
    // THE FALLBACK is everything after the FIRST comma, commas included:
    // `var(--a, 1px, 2px)` has the fallback `1px, 2px`, because a custom
    // property's value may itself be a comma list.
    [[nodiscard]] bool fallback(const token_stream & s, const call & found, int depth,
                                std::string & expansion) {
        if (found.comma_at >= found.close) { return false; } // no fallback: invalid
        const std::string text = text_between(s, found.comma_at + 1, found.close);
        return run(trim(text, html_whitespace), expansion, depth + 1);
    }

    [[nodiscard]] bool variable(const token_stream & s, const call & found, int depth,
                                std::string & expansion) {
        std::string_view name_text = s.text_of(s.tokens[found.name_at]);
        std::string built;
        if (found.name_is_call) {
            // `var(ident("--" attr(data-name)))`: the name is what the call
            // makes, and it has to be exactly one identifier.
            if (!run(text_between(s, found.name_at, found.comma_at), built, depth + 1)) {
                return false;
            }
            const std::vector<std::pair<token_type, std::string>> tokens = significant(built);
            if (tokens.size() != 1 || tokens.front().first != token_type::ident) { return false; }
            built = tokens.front().second;
            name_text = built;
        }
        // A custom property's name must start with `--`; anything else in that
        // position is not a custom property and the call is invalid.
        if (!name_text.starts_with("--")) { return false; }
        const atom name = atoms_->intern(name_text);

        // CYCLE DETECTION. `--a: var(--b); --b: var(--a)` must make BOTH invalid rather
        // than recursing to the depth limit, and a set of the properties currently
        // being resolved is what says so.
        const bool cyclic = reached_again(name);
        bool ok = false;
        if (conditions_ != nullptr && conditions_->on_read) { conditions_->on_read(name_text); }
        if (!cyclic) {
            if (const std::optional<std::string_view> held = (*lookup_)(name)) {
                resolving_.push_back(name.id);
                // A custom property's own value may itself contain var() -
                // and its attr()s are ITS OWN, computed as they would be for
                // the property itself: `--x: attr(data-foo)` read through
                // var(--x) while data-foo is being substituted is the string
                // it always is, not a ring (attr-cycle 26, 27). A ring through
                // var() is still caught, by this stack.
                std::vector<std::string> attrs_outside;
                attrs_outside.swap(attrs_resolving_);
                ok = run(*held, expansion, depth + 1);
                attrs_resolving_.swap(attrs_outside);
                resolving_.pop_back();
            }
        }
        if (ok) { return true; }
        return fallback(s, found, depth, expansion);
    }

    // inherit( <custom-property-name> , <declaration-value>? ), CSS Values 5
    // §inherit-notation: the PARENT's computed value of the property, which
    // is what `style(--x: inherit)` already reads, and the fallback when the
    // parent has none (inherit-function-basic).
    //
    // ponytail: custom properties only, and one level - the parent's own value
    // is substituted in the parent's scope with no grandparent to ask, so
    // `--v: e2 inherit(--v)` on the parent does not accumulate through it.
    [[nodiscard]] bool inherited_value(const token_stream & s, const call & found, int depth,
                                       std::string & expansion) {
        const std::string_view name_text = s.text_of(s.tokens[found.name_at]);
        if (!name_text.starts_with("--")) { return false; }
        if (conditions_ != nullptr && conditions_->inherited) {
            if (std::optional<std::string> held = conditions_->inherited(name_text)) {
                expansion = std::move(*held);
                return true;
            }
        }
        return fallback(s, found, depth, expansion);
    }

    // attr(): the argument list is substituted FIRST - `attr(var(--x))` - and
    // read after, and what the attribute holds is then judged by the type.
    [[nodiscard]] bool attribute(const token_stream & outer, const call & found, int depth,
                                 std::string & expansion);

    // --- ident(), CSS Values 5 §ident ---------------------------------------
    //
    //   ident( <ident-arg>+ )
    //   <ident-arg> = <string> | <integer> | <ident>
    //
    // The arguments joined into one identifier, at computed-value time: they may
    // hold other substitution functions, and an `<integer>` may be a math
    // function - `ident("vtl-" sibling-index())` names the third child `vtl-3`.
    // Anything else among them makes the whole invalid, and so does a result
    // that is not an identifier at all.
    [[nodiscard]] bool identifier(const token_stream & outer, const call & found, int depth,
                                  std::string & expansion);

    // --- --name(), CSS Functions and Mixins 1 §2 ------------------------------
    //
    //   --name( <declaration-value>#? )
    //
    // The arguments are substituted in the CALLER's scope and bound to the
    // parameters: a missing one takes its default, a typed one is computed
    // like the type it names, and too many or a missing one with no default
    // makes the call invalid. The body's locals and `result` are then
    // substituted in the FUNCTION's scope - parameters and locals first, the
    // calling element's custom properties after - and a typed `result` is
    // computed like its type on the way out. What comes out replaces the
    // call, and an untyped result is a token stream the caller reads.
    //
    // A `random()` met while computing a typed parameter or result is keyed on
    // the function and the slot (`--f/result`, `--f/--x`) and on THIS
    // INVOCATION as its element: `property-index-scoped` there is the same
    // draw wherever the call sits and `element-scoped` differs per call
    // (random-in-custom-function). An untyped one escapes as text and is drawn
    // where it lands.
    [[nodiscard]] bool custom_call(const token_stream & outer, const call & found, int depth,
                                   std::string & expansion);

    // --- random-item(), CSS Values 5 §random-item -----------------------------
    //
    //   random-item( <random-key> , [ <declaration-value>? ]# )
    //   <random-key> = [ auto | <dashed-ident> | fixed <number> ] || element-scoped
    //
    // One of the items, chosen by the same base `random()` uses - `fixed`
    // picks by index outright, `auto` is per element and property, a name is
    // shared - and substituted only once chosen, so an item nobody picked may
    // hold a var() nothing resolves. `{a, b}` braces keep a comma inside one
    // item and come off with it.
    [[nodiscard]] bool random_item(const token_stream & outer, const call & found, int depth,
                                   std::string & expansion);

    // --- if(), CSS Values 5 §if-notation ------------------------------------
    //
    //   if( [ <if-branch> ; ]* <if-branch> ;? )
    //   <if-branch> = <if-condition> : <declaration-value>?
    //   <if-condition> = <boolean-expr[ <if-test> ]> | else
    //   <if-test> = style( <style-query> ) | media( <media-condition> ) |
    //               supports( <supports-condition> | <ident> : <declaration-value> )
    //
    // The first branch whose condition holds supplies the value, substituted
    // like any other; `else` always holds. No branch holding is invalid at
    // computed-value time, exactly as a `var()` with nothing to substitute.
    //
    // A CONDITION IS SUBSTITUTED BEFORE IT IS READ - `style(--x: var(--y))`
    // compares against what `--y` holds and `if(var(--else): ...)` is an
    // `else` branch - and a condition that fails to substitute, or does not
    // parse as a condition, is simply false: `if-conditionals` writes both
    // `if(style(--x) and invalid: a; else: b)` and `if(style(--missing:
    // var(--missing)): a; else: b)` and asks for `b`. A `!` anywhere in the
    // argument list is different: no branch can contain one, so the whole
    // function is malformed.
    [[nodiscard]] bool conditional(const token_stream & outer, const call & found, int depth,
                                   std::string & expansion) {
        const std::size_t first = found.open + 1;
        const std::size_t last = found.close; // the `)` or eof, exclusive
        if (first >= last) { return false; }  // `if()`
        // The branches, split at the top-level `;`.
        struct branch {
            std::size_t from = 0;
            std::size_t colon = 0; // the first top-level `:`, or `to` if none
            std::size_t to = 0;
        };
        std::vector<branch> branches;
        {
            branch current{first, last, last};
            int block = 0;
            for (std::size_t i = first; i < last; ++i) {
                const css_token & t = outer.tokens[i];
                if (t.type == token_type::function || t.type == token_type::open_paren ||
                    t.type == token_type::open_square || t.type == token_type::open_curly) {
                    ++block;
                } else if (t.type == token_type::close_paren ||
                           t.type == token_type::close_square ||
                           t.type == token_type::close_curly) {
                    --block;
                } else if (block == 0 && t.type == token_type::delim && outer.text_of(t) == "!") {
                    return false;
                } else if (block == 0 && t.type == token_type::semicolon) {
                    current.to = i;
                    branches.push_back(current);
                    current = branch{i + 1, last, last};
                } else if (block == 0 && t.type == token_type::colon && current.colon == last) {
                    current.colon = i;
                }
            }
            // The last branch, unless the `;` before it was the optional
            // trailing one.
            bool blank = true;
            for (std::size_t i = current.from; i < last; ++i) {
                if (outer.tokens[i].type != token_type::whitespace) { blank = false; }
            }
            if (!blank) {
                current.to = last;
                branches.push_back(current);
            }
        }
        if (branches.empty()) { return false; }
        for (const branch & b : branches) {
            if (b.colon == last || b.colon > b.to) { return false; } // no `:`
            const std::string condition =
                std::string{trim(text_between(outer, b.from, b.colon), html_whitespace)};
            bool holds = false;
            std::string substituted;
            if (run(condition, substituted, depth + 1)) {
                // `else`, and a `var(--else)` that substitutes to it.
                holds = ascii_iequals(trim(substituted, html_whitespace), "else") ||
                        evaluate_condition(substituted, depth) == truth::yes;
            } else if (!cyclic_ && !root_cycle_) {
                // A var() the condition could not resolve makes only the
                // FEATURE holding it false, not the whole condition: `style(not
                // (--x: var(--y)))` with a cyclic `--y` is true (if-cycle).
                // The features substitute their own values as they are read.
                holds = evaluate_condition(condition, depth) == truth::yes;
            }
            // A query that read the property being resolved is a cycle, and a
            // cycle is invalid whichever branch it would have chosen.
            if (cyclic_ || root_cycle_) { return false; }
            if (!holds) { continue; }
            std::string_view value = text_between_view(outer, b.colon + 1, b.to);
            while (!value.empty() &&
                   html_whitespace.find(value.front()) != std::string_view::npos) {
                value.remove_prefix(1);
            }
            return run(value, expansion, depth + 1);
        }
        return false;
    }

    // `text_between` as a view over the pool, valid while `s` is: the tokens
    // of one stream are contiguous slices of it in order, escapes aside, and a
    // branch value has to keep its trailing whitespace exactly as written.
    [[nodiscard]] static std::string_view text_between_view(const token_stream & s,
                                                            std::size_t first, std::size_t last) {
        if (first >= last) { return {}; }
        const css_token & a = s.tokens[first];
        const css_token & b = s.tokens[last - 1];
        if (b.text + b.length < a.text) { return {}; }
        return std::string_view{s.pool}.substr(a.text, b.text + b.length - a.text);
    }

    [[nodiscard]] truth evaluate_condition(std::string_view condition, int depth) {
        const token_stream s = tokenize(condition);
        const auto test = [&](std::string_view name, std::size_t from, std::size_t to) -> truth {
            if (ascii_iequals(name, "style")) { return style_query(s, from, to, depth); }
            if (ascii_iequals(name, "media")) {
                if (!conditions_->media) { return truth::unknown; }
                const std::optional<bool> held = conditions_->media(text_between(s, from, to));
                return held ? (*held ? truth::yes : truth::no) : truth::unknown;
            }
            if (ascii_iequals(name, "supports")) {
                const std::string text = text_between(s, from, to);
                // `supports( <ident> : <declaration-value> )` is the one form
                // `<supports-condition>` does not already cover.
                if (supports_condition(text)) { return truth::yes; }
                if (supports_condition("(" + text + ")")) { return truth::yes; }
                return truth::no;
            }
            return truth::unknown;
        };
        // A parenthesised group that is not a condition is a
        // `<general-enclosed>`: undecidable, not an error.
        const auto enclosed = [](std::size_t, std::size_t) { return truth::unknown; };
        const std::size_t end = s.tokens.size() - 1; // the eof token
        return boolean_expression(s, 0, end, test, enclosed).value_or(truth::no);
    }

    // --- style(), CSS Conditional 5 §style-container --------------------------
    //
    //   <style-query> = <style-condition> | <style-feature>
    //   <style-feature> = <mf-plain> | <mf-boolean> | <mf-range>
    //
    // A plain feature compares a property's computed value with the query's
    // as token streams; a boolean one asks whether it has a value at all; a
    // range compares two numeric values of one type. The whole query is tried
    // as one feature first - `style(--x: 3)` - and then as a condition whose
    // parenthesised groups are features: `style((--x: 3) and (not (--y: red)))`.
    [[nodiscard]] truth style_query(const token_stream & s, std::size_t from, std::size_t to,
                                    int depth) {
        if (const std::optional<truth> one = style_feature(s, from, to, depth)) { return *one; }
        // `style(style(--x))`, `style(var(--x))`: a function is not a feature.
        const auto test = [](std::string_view, std::size_t, std::size_t) { return truth::unknown; };
        const auto enclosed = [&](std::size_t a, std::size_t b) {
            return style_feature(s, a, b, depth).value_or(truth::unknown);
        };
        return boolean_expression(s, from, to, test, enclosed).value_or(truth::unknown);
    }

    // IS `name` ALREADY BEING RESOLVED - and if so, whose cycle is it? A cycle
    // back to the property whose value this whole substitution is (`self_`) is
    // remembered for good: an `if()` whose condition reached it is invalid
    // whichever branch would have been taken, and so is a var() of it
    // (if-cycle). A cycle back to the property a style query is currently
    // computing (`owners_.back()`) makes THAT property valueless, which the
    // query then reads as false. A cycle among any other properties merely
    // leaves them without a value.
    [[nodiscard]] bool reached_again(atom name) {
        if (std::find(resolving_.begin(), resolving_.end(), name.id) == resolving_.end()) {
            return false;
        }
        if (self_ && name == self_) { root_cycle_ = true; }
        if (!owners_.empty() && name == owners_.back()) { cyclic_ = true; }
        return true;
    }

    // The computed value of a custom property on the element, substituted, or
    // nothing when it has none - unset, the guaranteed-invalid value, or a
    // value whose own substitution fails. A cycle through it is a cycle.
    [[nodiscard]] std::optional<std::string> custom_value(std::string_view name, int depth) {
        const atom key = atoms_->intern(name);
        if (reached_again(key)) { return std::nullopt; }
        const std::optional<std::string_view> held = (*lookup_)(key);
        if (!held) { return std::nullopt; }
        // The property's own value is resolved AS ITS OWN: a cycle back to it
        // found while computing it makes IT invalid, not the query that asked.
        const bool outer_cyclic = cyclic_;
        cyclic_ = false;
        owners_.push_back(key);
        resolving_.push_back(key.id);
        std::string out;
        const bool ok = run(*held, out, depth + 1);
        resolving_.pop_back();
        owners_.pop_back();
        cyclic_ = outer_cyclic;
        if (!ok) { return std::nullopt; }
        return out;
    }

    // ONE NUMERIC VALUE FOR A RANGE QUERY. A lone `--x` is the property's
    // computed value; anything else is the text as written. Either then has to
    // be one number, dimension or percentage - `calc(3px + 3px)` folds, `initial`
    // does not - and a percentage is its own family, so `1px >= 1%` is
    // undecidable rather than a comparison of magnitudes.
    struct magnitude {
        numeric_type type = numeric_type::number;
        bool percent = false;
        double value = 0.0;
    };
    [[nodiscard]] std::optional<magnitude> range_operand(std::string_view text, int depth) {
        std::string own_text;
        if (!run(text, own_text, depth + 1)) { return std::nullopt; }
        std::string_view value = trim(own_text, html_whitespace);
        std::string held;
        if (value.starts_with("--")) {
            const std::vector<std::pair<token_type, std::string>> tokens = significant(value);
            if (tokens.size() != 1 || tokens.front().first != token_type::ident) {
                return std::nullopt;
            }
            const std::optional<std::string> computed = custom_value(value, depth);
            if (!computed) { return std::nullopt; }
            held = *computed;
            value = trim(held, html_whitespace);
        }
        const math_answer answer = math_of(value);
        if (answer.outcome != math_outcome::resolved) { return std::nullopt; }
        magnitude out;
        if (answer.value.has_percent) {
            if (answer.value.px != 0.0) { return std::nullopt; } // `calc(10% + 1px)`
            out.percent = true;
            out.value = answer.value.percent;
            return out;
        }
        out.type = answer.value.type;
        out.value = answer.value.px;
        return out;
    }

    [[nodiscard]] static truth compare(magnitude a, std::string_view op, magnitude b) {
        // A UNITLESS ZERO IS A LENGTH where a length is wanted (CSS Values 4
        // §6.1 <zero>), and nowhere else: `0 = 0px` holds, `0 = 0%` does not.
        const auto zero_length = [](const magnitude & m) {
            return m.type == numeric_type::number && !m.percent && m.value == 0.0;
        };
        if (zero_length(a) && b.type == numeric_type::length && !b.percent) { a.type = b.type; }
        if (zero_length(b) && a.type == numeric_type::length && !a.percent) { b.type = a.type; }
        if (a.type != b.type || a.percent != b.percent) { return truth::unknown; }
        bool holds = false;
        if (op == "<") {
            holds = a.value < b.value;
        } else if (op == "<=") {
            holds = a.value <= b.value;
        } else if (op == ">") {
            holds = a.value > b.value;
        } else if (op == ">=") {
            holds = a.value >= b.value;
        } else {
            holds = a.value == b.value;
        }
        return holds ? truth::yes : truth::no;
    }

    // One `<style-feature>` over the tokens [from, to), or nothing when they
    // are not one - a condition, or nonsense.
    [[nodiscard]] std::optional<truth> style_feature(const token_stream & s, std::size_t from,
                                                     std::size_t to, int depth) {
        // The top-level structure: the comparison operators and the colon, with
        // a `!` anywhere making the feature invalid.
        struct op_at {
            std::size_t at = 0;
            std::size_t width = 1; // `<=` is two delim tokens
            std::string text;
        };
        std::vector<op_at> ops;
        std::size_t colon = to;
        std::size_t first_significant = to;
        std::size_t count = 0;
        int block = 0;
        for (std::size_t i = from; i < to; ++i) {
            const css_token & t = s.tokens[i];
            if (t.type == token_type::whitespace) { continue; }
            if (first_significant == to) { first_significant = i; }
            ++count;
            if (t.type == token_type::function || t.type == token_type::open_paren ||
                t.type == token_type::open_square || t.type == token_type::open_curly) {
                ++block;
                continue;
            }
            if (t.type == token_type::close_paren || t.type == token_type::close_square ||
                t.type == token_type::close_curly) {
                --block;
                continue;
            }
            if (block != 0) { continue; }
            if (t.type == token_type::delim && s.text_of(t) == "!") { return truth::no; }
            if (t.type == token_type::colon && colon == to) { colon = i; }
            if (t.type == token_type::delim) {
                const std::string_view d = s.text_of(t);
                if (d == "<" || d == ">" || d == "=") {
                    op_at op{i, 1, std::string{d}};
                    if (d != "=" && is_delim_text(s, i + 1, '=')) {
                        op.width = 2;
                        op.text += '=';
                        ++i;
                    }
                    ops.push_back(std::move(op));
                }
            }
        }
        if (count == 0) { return std::nullopt; }
        // <mf-range>: `a < b`, `a < b < c`.
        if (!ops.empty()) {
            if (colon != to || ops.size() > 2) { return truth::no; }
            std::vector<std::string_view> sides;
            std::size_t at = from;
            for (const op_at & op : ops) {
                sides.push_back(text_between_view(s, at, op.at));
                at = op.at + op.width;
            }
            sides.push_back(text_between_view(s, at, to));
            if (ops.size() == 2) {
                // Both operators must point the same way: `3 < x <= 5`.
                const bool less_a = ops[0].text.front() == '<';
                const bool less_b = ops[1].text.front() == '<';
                if (ops[0].text == "=" || ops[1].text == "=" || less_a != less_b) {
                    return truth::no;
                }
            }
            truth result = truth::yes;
            for (std::size_t i = 0; i < ops.size(); ++i) {
                const std::optional<magnitude> a = range_operand(sides[i], depth);
                const std::optional<magnitude> b = range_operand(sides[i + 1], depth);
                if (!a || !b) { return truth::unknown; }
                result = both(result, compare(*a, ops[i].text, *b));
            }
            return result;
        }
        // <mf-plain> and <mf-boolean>: the property name first.
        const css_token & head = s.tokens[first_significant];
        if (head.type != token_type::ident) { return std::nullopt; }
        const std::string_view property = s.text_of(head);
        const bool custom = property.starts_with("--");
        if (colon == to) {
            // `style(--x)`: does it have a value? Anything else always does.
            if (count != 1) { return std::nullopt; }
            if (!custom) { return truth::yes; }
            return custom_value(property, depth) ? truth::yes : truth::no;
        }
        // The name must be the only thing before the colon.
        for (std::size_t i = first_significant + 1; i < colon; ++i) {
            if (s.tokens[i].type != token_type::whitespace) { return std::nullopt; }
        }
        std::string query_text;
        if (!run(text_between_view(s, colon + 1, to), query_text, depth + 1)) {
            return truth::no; // a value nothing can substitute matches nothing
        }
        const std::string_view query = trim(query_text, html_whitespace);
        if (custom) {
            const std::optional<std::string> own = custom_value(property, depth);
            const property_registration * registered =
                conditions_->registered ? conditions_->registered(property) : nullptr;
            const auto same = [](const std::optional<std::string> & a,
                                 const std::optional<std::string> & b) {
                if (!a && !b) { return truth::yes; }
                if (!a || !b) { return truth::no; }
                return significant(*a) == significant(*b) ? truth::yes : truth::no;
            };
            const auto parent = [&]() -> std::optional<std::string> {
                return conditions_->inherited ? conditions_->inherited(property) : std::nullopt;
            };
            // THE CSS-WIDE KEYWORDS NAME A VALUE TO COMPARE WITH rather than
            // being one: `initial` is the guaranteed-invalid value an
            // unregistered property starts from and the initial value a
            // registered one has, `inherit` is the parent's, and `unset` is
            // whichever of the two the property inherits. `revert` and
            // `revert-layer` name nothing here.
            if (ascii_iequals(query, "initial")) {
                if (registered == nullptr) { return own ? truth::no : truth::yes; }
                return same(own, compute_registered(registered->initial, registered->syntax,
                                                    conditions_->lengths));
            }
            if (ascii_iequals(query, "inherit")) { return same(own, parent()); }
            if (ascii_iequals(query, "unset")) {
                if (registered != nullptr && !registered->inherits) {
                    return same(own, compute_registered(registered->initial, registered->syntax,
                                                        conditions_->lengths));
                }
                return same(own, parent());
            }
            if (ascii_iequals(query, "revert") || ascii_iequals(query, "revert-layer")) {
                return truth::no;
            }
            if (!own) { return truth::no; }
            // A registered property compares computed values of its type, so
            // `style(--length: 1em)` holds against a `30px` under a 30px font.
            if (registered != nullptr) {
                std::optional<std::string> theirs =
                    compute_registered(query, registered->syntax, conditions_->lengths);
                if (conditions_->canonical_color && theirs &&
                    registered->syntax.find("<color>") != std::string::npos) {
                    return same(conditions_->canonical_color(*own),
                                conditions_->canonical_color(*theirs));
                }
                return same(own, theirs);
            }
            return significant(*own) == significant(query) ? truth::yes : truth::no;
        }
        if (!conditions_->computed) { return truth::unknown; }
        const std::optional<std::string> own = conditions_->computed(property);
        if (!own) { return truth::no; }
        // The query's value in the same form the computed one takes, as far as
        // a dimension goes: `style(width: 1in)` against a computed `96px`.
        std::string folded{query};
        if (may_have_math(folded)) { folded = fold_math(folded, conditions_->lengths).text; }
        if (const auto canonical = canonical_dimension_text(folded, conditions_->lengths)) {
            folded = *canonical;
        }
        return significant(*own) == significant(folded) ? truth::yes : truth::no;
    }

    const custom_lookup * lookup_;
    atom_table * atoms_;
    const attribute_lookup * attributes_;
    const condition_environment * conditions_;
    // The properties currently being expanded, innermost last. A vector rather than a
    // set because it is never more than a handful deep and a linear scan of four
    // integers beats hashing one.
    std::vector<std::uint32_t> resolving_;
    // The attributes whose values are being substituted, outermost first, and
    // whether a cycle through them was found (attr-cycle).
    std::vector<std::string> attrs_resolving_;
    bool attr_cycle_ = false;
    // The property whose value this is, the properties whose values style
    // queries are computing (innermost last), and the two cycle flags
    // `reached_again` explains.
    atom self_{};
    std::vector<atom> owners_;
    bool root_cycle_ = false;
    bool cyclic_ = false;
    std::uint32_t randoms_seen_ = 0; // the random() functions math_of has numbered
    std::uint64_t calls_ = 0;        // the custom function calls made, for their keys
};

} // namespace ctbrowser::style::css::substitute_detail
