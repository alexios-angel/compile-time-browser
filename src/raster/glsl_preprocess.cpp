#include <ctbrowser/raster/glsl.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_map>

// The GLSL ES preprocessor.
//
// IN ITS OWN FILE because it is a separate language from the one below it, and
// because it is where the surprises were. The plan named `#define`, `#ifdef`,
// `#else` and `#endif`; p5's own shaders also use `#ifndef`, `#if 0`,
// `#extension`, FUNCTION-LIKE macros (`#define INT(x) float(x)`) and macros that
// rename TYPES (`#define int float`). Every one of those was found by reading
// tests/glsl/, not by remembering the specification.
//
// LINE COUNT IS PRESERVED. Every directive and every skipped block leaves its
// newlines behind, so line N of the output is line N of the input and a
// diagnostic points at what the author actually wrote. Emitting nothing for a
// skipped `#if` block would be simpler and would misreport every error after it.

namespace ctbrowser::raster::glsl {
namespace {

struct macro {
    std::vector<std::string> parameters;
    std::string body;
    bool function_like = false;
};

[[nodiscard]] bool name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
[[nodiscard]] bool name_part(char c) { return name_start(c) || (c >= '0' && c <= '9'); }

[[nodiscard]] std::string_view trim(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) { return {}; }
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

// Comments become spaces BEFORE anything else looks at the text, so a directive
// with a trailing `// note` and a macro body with a comment in it both behave.
// Newlines inside a block comment are kept, because line numbers are.
[[nodiscard]] std::string strip_comments(std::string_view source) {
    std::string out;
    out.reserve(source.size());
    for (std::size_t i = 0; i < source.size();) {
        if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            while (i < source.size() && source[i] != '\n') { ++i; }
            continue;
        }
        if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            i += 2;
            while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == '/')) {
                if (source[i] == '\n') { out.push_back('\n'); }
                ++i;
            }
            i = std::min(i + 2, source.size());
            out.push_back(' ');
            continue;
        }
        out.push_back(source[i]);
        ++i;
    }
    return out;
}

// The state of one `#if` nest level. `taken` is what stops an `#elif` from
// running after an earlier branch already did.
struct conditional {
    bool active = true;   // is this branch's text being emitted
    bool taken = false;   // has any branch of this #if been taken
    bool parent_active = true;
};

class preprocessor {
public:
    preprocessor(const options & how, std::vector<diagnostic> & into) : errors_(&into) {
        for (const auto & [name, body] : how.defines) {
            macros_[name] = macro{{}, body, false};
        }
        // p5's preamble branches on this to decide what IN means, and the
        // difference is `attribute` versus `varying` - so a fragment shader
        // preprocessed as a vertex one declares the wrong storage for every
        // input it has.
        if (how.which == stage::fragment) { macros_["FRAGMENT_SHADER"] = macro{{}, "1", false}; }
        else { macros_["VERTEX_SHADER"] = macro{{}, "1", false}; }
        macros_["GL_ES"] = macro{{}, "1", false};
    }

    [[nodiscard]] std::string run(std::string_view source) {
        const std::string clean = strip_comments(source);
        std::string out;
        out.reserve(clean.size());

        std::size_t at = 0;
        line_ = 1;
        while (at <= clean.size()) {
            const std::size_t end = std::min(clean.find('\n', at), clean.size());
            const std::string_view raw{clean.data() + at, end - at};
            const std::string_view text = trim(raw);

            if (!text.empty() && text.front() == '#') {
                directive(text.substr(1));
                // The directive leaves a blank line, so nothing after it moves.
            } else if (emitting()) {
                out += expand(raw);
            }
            out.push_back('\n');
            ++line_;
            if (end == clean.size()) { break; }
            at = end + 1;
        }
        if (!nest_.empty()) { fail("unterminated #if - " + std::to_string(nest_.size()) + " open"); }
        return out;
    }

private:
    [[nodiscard]] bool emitting() const {
        return std::ranges::all_of(nest_, [](const conditional & c) { return c.active; });
    }

    void fail(std::string message) { errors_->push_back(diagnostic{line_, std::move(message)}); }

    void directive(std::string_view body) {
        const std::string_view text = trim(body);
        const std::size_t space = text.find_first_of(" \t(");
        const std::string_view word = text.substr(0, space);
        const std::string_view rest =
            space == std::string_view::npos ? std::string_view{} : trim(text.substr(space));

        // The conditionals are handled even when NOT emitting, because a nested
        // `#if` inside a skipped block still has to find its own `#endif`.
        if (word == "if" || word == "ifdef" || word == "ifndef") {
            const bool outer = emitting();
            bool yes = false;
            if (outer) {
                if (word == "ifdef") { yes = macros_.contains(std::string{first_word(rest)}); }
                else if (word == "ifndef") { yes = !macros_.contains(std::string{first_word(rest)}); }
                else { yes = evaluate(rest) != 0; }
            }
            nest_.push_back(conditional{outer && yes, yes, outer});
            return;
        }
        if (word == "elif" || word == "else") {
            if (nest_.empty()) {
                fail("#" + std::string{word} + " without #if");
                return;
            }
            conditional & now = nest_.back();
            const bool yes = word == "else" || (now.parent_active && evaluate(rest) != 0);
            // ONLY IF NO EARLIER BRANCH RAN. Without this, `#if 0 ... #else ...
            // #endif` is fine but a three-way chain runs two of its arms.
            now.active = now.parent_active && !now.taken && yes;
            now.taken = now.taken || now.active;
            return;
        }
        if (word == "endif") {
            if (nest_.empty()) { fail("#endif without #if"); }
            else { nest_.pop_back(); }
            return;
        }
        if (!emitting()) { return; }

        if (word == "define") {
            define(rest);
        } else if (word == "undef") {
            macros_.erase(std::string{first_word(rest)});
        } else if (word == "error") {
            fail("#error " + std::string{rest});
        } else if (word == "version" || word == "extension" || word == "pragma" ||
                   word == "line") {
            // RECORDED AND IGNORED, deliberately. `#version 300 es` cannot be
            // honoured - this is a WebGL 1 implementation and says so - and
            // `#extension GL_OES_standard_derivatives : enable` asks for
            // something a scanline rasteriser cannot give. Failing here would
            // reject p5's font shader, which asks and then guards its use with
            // `#ifdef`, so ignoring is both correct and what a driver does with
            // an extension it lacks.
        } else {
            fail("unknown directive #" + std::string{word});
        }
    }

    [[nodiscard]] static std::string_view first_word(std::string_view text) {
        std::size_t i = 0;
        while (i < text.size() && name_part(text[i])) { ++i; }
        return text.substr(0, i);
    }

    void define(std::string_view rest) {
        const std::string_view name = first_word(rest);
        if (name.empty()) {
            fail("#define needs a name");
            return;
        }
        std::string_view tail = rest.substr(name.size());
        macro made;
        // FUNCTION-LIKE ONLY IF THE PAREN TOUCHES THE NAME. `#define A (x)` is
        // an object-like macro whose body is `(x)`; `#define A(x)` takes an
        // argument. p5's `#define INT(x) float(x)` is the second and its
        // `#define HOOK_DEFINES` is neither.
        if (!tail.empty() && tail.front() == '(') {
            made.function_like = true;
            const std::size_t close = tail.find(')');
            if (close == std::string_view::npos) {
                fail("#define " + std::string{name} + " has no closing paren");
                return;
            }
            std::string_view params = tail.substr(1, close - 1);
            while (!params.empty()) {
                const std::size_t comma = params.find(',');
                made.parameters.emplace_back(trim(params.substr(0, comma)));
                if (comma == std::string_view::npos) { break; }
                params = params.substr(comma + 1);
            }
            tail = tail.substr(close + 1);
        }
        made.body = std::string{trim(tail)};
        macros_[std::string{name}] = std::move(made);
    }

    // Expand macros in one line of ordinary text.
    //
    // Bounded rather than recursive-to-exhaustion: a macro that expands to
    // itself is a page's mistake and must not be a hang. A real preprocessor
    // uses a hide-set; this uses a depth cap, which differs only for programs
    // that were already ill-formed.
    [[nodiscard]] std::string expand(std::string_view text, int depth = 0) {
        if (depth > 32) { return std::string{text}; }
        std::string out;
        out.reserve(text.size());
        bool any = false;
        for (std::size_t i = 0; i < text.size();) {
            if (!name_start(text[i])) {
                out.push_back(text[i]);
                ++i;
                continue;
            }
            const std::size_t start = i;
            while (i < text.size() && name_part(text[i])) { ++i; }
            const std::string word{text.substr(start, i - start)};
            const auto found = macros_.find(word);
            if (found == macros_.end()) {
                out += word;
                continue;
            }
            if (!found->second.function_like) {
                out += found->second.body;
                any = true;
                continue;
            }
            // A function-like macro NOT followed by `(` is not an invocation -
            // it is just the name, which is how a shader can pass one around.
            std::size_t open = i;
            while (open < text.size() && (text[open] == ' ' || text[open] == '\t')) { ++open; }
            if (open >= text.size() || text[open] != '(') {
                out += word;
                continue;
            }
            std::vector<std::string> args;
            const std::size_t after = read_arguments(text, open, args);
            if (after == std::string::npos) {
                fail("unterminated arguments to macro " + word);
                out += word;
                continue;
            }
            out += substitute(found->second, args);
            any = true;
            i = after;
        }
        // Only re-expand when something changed, so a line with no macros costs
        // one pass rather than two.
        return any ? expand(out, depth + 1) : out;
    }

    // Read `(a, b(c), d)` starting at the open paren. Returns the index past the
    // close, or npos. Nested parens do not end an argument, which is what makes
    // `INT(float(x))` work.
    [[nodiscard]] static std::size_t read_arguments(std::string_view text, std::size_t open,
                                                    std::vector<std::string> & into) {
        int depth = 0;
        std::string current;
        for (std::size_t i = open; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '(') {
                ++depth;
                if (depth == 1) { continue; }
            } else if (c == ')') {
                --depth;
                if (depth == 0) {
                    into.push_back(std::string{trim(current)});
                    return i + 1;
                }
            } else if (c == ',' && depth == 1) {
                into.push_back(std::string{trim(current)});
                current.clear();
                continue;
            }
            current.push_back(c);
        }
        return std::string::npos;
    }

    [[nodiscard]] static std::string substitute(const macro & m,
                                                const std::vector<std::string> & args) {
        std::string out;
        const std::string_view body{m.body};
        for (std::size_t i = 0; i < body.size();) {
            if (!name_start(body[i])) {
                out.push_back(body[i]);
                ++i;
                continue;
            }
            const std::size_t start = i;
            while (i < body.size() && name_part(body[i])) { ++i; }
            const std::string_view word = body.substr(start, i - start);
            const auto at = std::ranges::find(m.parameters, word);
            if (at == m.parameters.end()) {
                out += word;
                continue;
            }
            const auto which = static_cast<std::size_t>(at - m.parameters.begin());
            // A parameter with no argument expands to nothing, which is what an
            // empty argument means anyway.
            if (which < args.size()) { out += args[which]; }
        }
        return out;
    }

    // --- #if expressions ---------------------------------------------------
    //
    // A small integer-expression evaluator: literals, `defined X`, the macros in
    // scope, and the operators GLSL ES allows. Everything is a long long, which
    // is what the preprocessor's arithmetic is defined on.

    [[nodiscard]] long long evaluate(std::string_view text) {
        // `defined X` and `defined(X)` are replaced BEFORE macro expansion,
        // because expanding first would replace X with its body and then ask
        // whether the body is defined.
        std::string resolved;
        for (std::size_t i = 0; i < text.size();) {
            if (name_start(text[i])) {
                const std::size_t start = i;
                while (i < text.size() && name_part(text[i])) { ++i; }
                const std::string_view word = text.substr(start, i - start);
                if (word == "defined") {
                    while (i < text.size() && (text[i] == ' ' || text[i] == '(')) { ++i; }
                    const std::size_t name_at = i;
                    while (i < text.size() && name_part(text[i])) { ++i; }
                    const std::string name{text.substr(name_at, i - name_at)};
                    while (i < text.size() && (text[i] == ' ' || text[i] == ')')) { ++i; }
                    resolved += macros_.contains(name) ? '1' : '0';
                    continue;
                }
                resolved += word;
                continue;
            }
            resolved.push_back(text[i]);
            ++i;
        }
        const std::string expanded = expand(resolved);
        std::size_t at = 0;
        const long long value = ternary(expanded, at);
        return value;
    }

    // Precedence climbing, lowest first. Written out rather than table-driven
    // because there are nine levels and they read better as functions.
    [[nodiscard]] long long ternary(const std::string & text, std::size_t & at) {
        const long long condition = binary(text, at, 0);
        skip(text, at);
        if (at < text.size() && text[at] == '?') {
            ++at;
            const long long yes = ternary(text, at);
            skip(text, at);
            if (at < text.size() && text[at] == ':') { ++at; }
            const long long no = ternary(text, at);
            return condition != 0 ? yes : no;
        }
        return condition;
    }

    struct level {
        std::string_view ops;
    };

    [[nodiscard]] long long binary(const std::string & text, std::size_t & at, int precedence) {
        static constexpr std::string_view levels[] = {"||", "&&", "|", "^", "&",
                                                      "==", "<>", "+-", "*/%"};
        static constexpr int count = 9;
        if (precedence >= count) { return unary_expr(text, at); }
        long long left = binary(text, at, precedence + 1);
        while (true) {
            skip(text, at);
            const std::string_view here = levels[precedence];
            std::string op;
            if (precedence <= 1) { // || &&
                if (at + 1 < text.size() && text.compare(at, 2, here) == 0) { op = here; }
            } else if (precedence == 5) { // == !=
                if (at + 1 < text.size() &&
                    (text.compare(at, 2, "==") == 0 || text.compare(at, 2, "!=") == 0)) {
                    op = text.substr(at, 2);
                }
            } else if (precedence == 6) { // < > <= >= and the shifts
                if (at + 1 < text.size() &&
                    (text.compare(at, 2, "<=") == 0 || text.compare(at, 2, ">=") == 0 ||
                     text.compare(at, 2, "<<") == 0 || text.compare(at, 2, ">>") == 0)) {
                    op = text.substr(at, 2);
                } else if (at < text.size() && (text[at] == '<' || text[at] == '>')) {
                    op = text.substr(at, 1);
                }
            } else if (at < text.size() && here.find(text[at]) != std::string_view::npos) {
                // A single-character operator, but not the first half of a
                // two-character one that belongs to a different level.
                if (!(text[at] == '|' && at + 1 < text.size() && text[at + 1] == '|') &&
                    !(text[at] == '&' && at + 1 < text.size() && text[at + 1] == '&')) {
                    op = text.substr(at, 1);
                }
            }
            if (op.empty()) { return left; }
            at += op.size();
            const long long right = binary(text, at, precedence + 1);
            left = apply(op, left, right);
        }
    }

    [[nodiscard]] static long long apply(std::string_view op, long long l, long long r) {
        if (op == "||") { return (l != 0 || r != 0) ? 1 : 0; }
        if (op == "&&") { return (l != 0 && r != 0) ? 1 : 0; }
        if (op == "|") { return l | r; }
        if (op == "^") { return l ^ r; }
        if (op == "&") { return l & r; }
        if (op == "==") { return l == r ? 1 : 0; }
        if (op == "!=") { return l != r ? 1 : 0; }
        if (op == "<=") { return l <= r ? 1 : 0; }
        if (op == ">=") { return l >= r ? 1 : 0; }
        if (op == "<<") { return l << (r & 63); }
        if (op == ">>") { return l >> (r & 63); }
        if (op == "<") { return l < r ? 1 : 0; }
        if (op == ">") { return l > r ? 1 : 0; }
        if (op == "+") { return l + r; }
        if (op == "-") { return l - r; }
        if (op == "*") { return l * r; }
        // DIVISION BY ZERO IS ZERO, not a trap. `#if X/0` is a page's mistake
        // and must not take the process with it.
        if (op == "/") { return r == 0 ? 0 : l / r; }
        if (op == "%") { return r == 0 ? 0 : l % r; }
        return 0;
    }

    [[nodiscard]] long long unary_expr(const std::string & text, std::size_t & at) {
        skip(text, at);
        if (at >= text.size()) { return 0; }
        if (text[at] == '!') {
            ++at;
            return unary_expr(text, at) == 0 ? 1 : 0;
        }
        if (text[at] == '-') {
            ++at;
            return -unary_expr(text, at);
        }
        if (text[at] == '+') {
            ++at;
            return unary_expr(text, at);
        }
        if (text[at] == '~') {
            ++at;
            return ~unary_expr(text, at);
        }
        if (text[at] == '(') {
            ++at;
            const long long inner = ternary(text, at);
            skip(text, at);
            if (at < text.size() && text[at] == ')') { ++at; }
            return inner;
        }
        if (std::isdigit(static_cast<unsigned char>(text[at])) != 0) {
            char * end = nullptr;
            const long long value = std::strtoll(text.c_str() + at, &end, 0);
            at = static_cast<std::size_t>(end - text.c_str());
            return value;
        }
        // AN UNDEFINED NAME IS ZERO. That is the C rule and the GLSL one, and it
        // is what makes `#if 0` and `#if UNSET` behave the same way.
        while (at < text.size() && name_part(text[at])) { ++at; }
        return 0;
    }

    static void skip(const std::string & text, std::size_t & at) {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) { ++at; }
    }

    std::unordered_map<std::string, macro> macros_;
    std::vector<conditional> nest_;
    std::vector<diagnostic> * errors_;
    std::uint32_t line_ = 1;
};

} // namespace

std::string preprocess(std::string_view source, const options & how,
                       std::vector<diagnostic> & into) {
    preprocessor pass{how, into};
    return pass.run(source);
}

} // namespace ctbrowser::raster::glsl
