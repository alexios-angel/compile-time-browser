// early errors - regular expression literals.
//
// 13.2.7.1: a RegularExpressionLiteral is an early error when its body is not
// a Pattern, or its flags are not a legal set of flags. The whole of the
// regular expression grammar is that rule, and the whole grammar is NOT what
// runs here: `regex.hpp` compiles a pattern when the literal is evaluated,
// and running that compiler over every literal at compile time would turn
// each of its gaps into a page that does not load (p5.js has one at `/ /`,
// which the rx engine could not take when this was first tried).
//
// So this is a SCAN, not a parse. It reads the literal the way the lexer did
// - escapes, character classes, groups - and refuses only what is provably
// not a Pattern by inspection: a flag that does not exist or repeats, a group
// that never closes, `(?` followed by nothing the grammar names, a quantifier
// with nothing to repeat or with its bounds reversed, a quantified lookbehind,
// a named group that is not a name or is bound twice on one path, `\k<name>`
// naming no group, and - under the `u` flag, which turns Annex B off - the
// identity escapes and lone brackets Annex B otherwise forgives. Anything it
// does not understand it lets through: a miss here is a runtime SyntaxError
// at the literal, a false positive is a script that never runs.
//
// The lexer keeps the delimiters, so the lexeme is `/body/flags`.

#include "checker.hpp"

#include <algorithm>
#include <cstdint>

namespace ctbrowser::script::detail::early {

namespace {

[[nodiscard]] bool hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
[[nodiscard]] bool digit(char c) {
    return c >= '0' && c <= '9';
}
[[nodiscard]] bool ascii_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
// The SyntaxCharacters, which are the identity escapes `u` mode still allows.
[[nodiscard]] bool syntax_char(char c) {
    return std::string_view{"^$\\.*+?()[]{}|/"}.find(c) != std::string_view::npos;
}

// ONE OPEN GROUP, and the names bound along the current path through it.
// 22.2.1.1 lets one name be bound twice only in different alternatives of
// some disjunction: `(?<a>x)|(?<a>y)` is legal, `(?<a>x)(?<a>y)` is not.
// Each alternative's names are kept apart until `|`, and a closing group
// hands the union of its alternatives up as part of the parent's current
// alternative.
struct group {
    std::vector<std::string> current; // names bound in the alternative being read
    std::vector<std::string> earlier; // names bound in the alternatives before it
    bool lookbehind = false;
    bool lookahead = false;
    bool capturing = false;
};

class scan {
public:
    scan(std::string_view body, bool unicode) : s_(body), unicode_(unicode) {}

    [[nodiscard]] std::optional<std::string> run() {
        groups_.push_back(group{});
        while (i_ < s_.size()) {
            if (error_) { return error_; }
            step();
        }
        if (error_) { return error_; }
        if (in_class_) { return "the character class is never closed"; }
        if (groups_.size() > 1) { return "a group is never closed"; }
        // The forward references, resolved now that every group is known.
        // \k is a backreference at all only under `u` or when the pattern
        // binds a name (B.1.2); otherwise it was the letter k.
        if (unicode_ || !names_.empty()) {
            for (const std::string & name : k_refs_) {
                if (name.empty()) { return "`\\k` must be followed by `<name>`"; }
                if (std::find(names_.begin(), names_.end(), name) == names_.end()) {
                    return "`\\k<" + name + ">` names no group in the pattern";
                }
            }
        }
        if (unicode_) {
            for (const std::uint32_t n : back_refs_) {
                if (n > captures_) { return "`\\" + std::to_string(n) + "` names no group"; }
            }
        }
        return std::nullopt;
    }

private:
    void fail(std::string what) {
        if (!error_) { error_ = std::move(what); }
    }
    [[nodiscard]] char peek(std::size_t ahead = 0) const {
        return i_ + ahead < s_.size() ? s_[i_ + ahead] : '\0';
    }

    void step() {
        const char c = s_[i_];
        // A raw line terminator cannot be in the literal (12.9.5); the lexer
        // stops at LF but a CR, LS or PS slips through.
        if (c == '\r' || c == '\n') {
            fail("a regular expression literal may not contain a line terminator");
            return;
        }
        if (static_cast<unsigned char>(c) == 0xE2 && static_cast<unsigned char>(peek(1)) == 0x80 &&
            (static_cast<unsigned char>(peek(2)) == 0xA8 ||
             static_cast<unsigned char>(peek(2)) == 0xA9)) {
            fail("a regular expression literal may not contain a line terminator");
            return;
        }
        if (c == '\\') {
            escape();
            return;
        }
        if (in_class_) {
            if (c == ']') { in_class_ = false; }
            ++i_;
            return;
        }
        switch (c) {
        case '[':
            in_class_ = true;
            ++i_;
            if (peek() == '^') { ++i_; }
            if (peek() == ']') { // `[]` and `[^]` are classes with no ranges
                in_class_ = false;
                ++i_;
            }
            quantifiable_ = true;
            return;
        case ']':
        case '}':
            if (unicode_) {
                fail(std::string{"a lone `"} + c + "` is not allowed with the u flag");
            }
            ++i_;
            quantifiable_ = true;
            return;
        case '{': brace(); return;
        case '*':
        case '+':
        case '?': quantifier(1); return;
        case '(': open(); return;
        case ')': close(); return;
        case '|':
            alternative();
            ++i_;
            quantifiable_ = false;
            return;
        case '^':
        case '$':
            ++i_;
            quantifiable_ = false;
            return;
        default:
            ++i_;
            quantifiable_ = true;
            return;
        }
    }

    // A quantifier `width` characters wide, at i_. The thing before it has to
    // be something a quantifier may follow.
    void quantifier(std::size_t width) {
        if (!quantifiable_) {
            fail("nothing to repeat before the quantifier `" + std::string{s_.substr(i_, width)} +
                 "`");
            return;
        }
        i_ += width;
        if (peek() == '?') { ++i_; } // lazy
        quantifiable_ = false;
    }

    // `{n}`, `{n,}`, `{n,m}` - or, outside `u` mode, a literal brace when it
    // is not one of those (B.1.2 ExtendedPatternCharacter).
    void brace() {
        std::size_t j = i_ + 1;
        std::uint64_t low = 0;
        std::uint64_t high = 0;
        bool has_high = false;
        bool comma = false;
        std::size_t digits = 0;
        while (j < s_.size() && digit(s_[j])) {
            low = std::min<std::uint64_t>(low * 10 + static_cast<std::uint64_t>(s_[j] - '0'),
                                          1ull << 53);
            ++j;
            ++digits;
        }
        bool well_formed = digits > 0;
        if (well_formed && j < s_.size() && s_[j] == ',') {
            comma = true;
            ++j;
            std::size_t more = 0;
            while (j < s_.size() && digit(s_[j])) {
                high = std::min<std::uint64_t>(high * 10 + static_cast<std::uint64_t>(s_[j] - '0'),
                                               1ull << 53);
                ++j;
                ++more;
            }
            has_high = more > 0;
        }
        well_formed = well_formed && j < s_.size() && s_[j] == '}';
        if (!well_formed) {
            if (unicode_) { fail("a lone `{` is not allowed with the u flag"); }
            ++i_;
            quantifiable_ = true;
            return;
        }
        if (comma && has_high && high < low) {
            fail("the quantifier `" + std::string{s_.substr(i_, j + 1 - i_)} +
                 "` has its bounds out of order");
            return;
        }
        quantifier(j + 1 - i_);
    }

    void escape() {
        ++i_;
        if (i_ >= s_.size()) {
            fail("`\\` at the end of the pattern");
            return;
        }
        const char c = s_[i_];
        ++i_;
        quantifiable_ = true;
        if (c == 'b' || c == 'B') {
            if (!in_class_) { quantifiable_ = false; } // an assertion
            return;
        }
        if (c == 'k' && !in_class_) {
            std::string name;
            if (peek() == '<') {
                const std::size_t end = s_.find('>', i_);
                if (end != std::string_view::npos) {
                    name = std::string{s_.substr(i_ + 1, end - i_ - 1)};
                    i_ = end + 1;
                }
            }
            k_refs_.push_back(name.empty() ? std::string{} : name);
            if (name.empty() && (unicode_)) { fail("`\\k` must be followed by `<name>`"); }
            return;
        }
        if (c == 'u') {
            if (peek() == '{') {
                const std::size_t end = s_.find('}', i_);
                const std::string_view digits = end == std::string_view::npos
                                                    ? std::string_view{}
                                                    : s_.substr(i_ + 1, end - i_ - 1);
                const bool ok = !digits.empty() && std::all_of(digits.begin(), digits.end(), hex);
                if (unicode_) {
                    if (!ok) {
                        fail("`\\u{...}` needs hexadecimal digits");
                        return;
                    }
                    std::uint64_t code = 0;
                    for (const char d : digits) { // saturating: leading zeros are legal
                        code = std::min<std::uint64_t>(
                            code * 16 + static_cast<std::uint64_t>(
                                            digit(d) ? d - '0' : (d | 0x20) - 'a' + 10),
                            0x110000);
                    }
                    if (code > 0x10FFFF) {
                        fail("`\\u{" + std::string{digits} + "}` is past the last code point");
                        return;
                    }
                    i_ = end + 1;
                }
                return; // without `u`, `\u{` is the letter u and a brace
            }
            const bool four = hex(peek()) && hex(peek(1)) && hex(peek(2)) && hex(peek(3));
            if (four) {
                i_ += 4;
            } else if (unicode_) {
                fail("`\\u` must be followed by four hexadecimal digits");
            }
            return;
        }
        if (c == 'x') {
            if (hex(peek()) && hex(peek(1))) {
                i_ += 2;
            } else if (unicode_) {
                fail("`\\x` must be followed by two hexadecimal digits");
            }
            return;
        }
        if (c == 'c') {
            if (ascii_letter(peek())) {
                ++i_;
            } else if (unicode_) {
                fail("`\\c` must be followed by a letter");
            }
            return;
        }
        if (c == '0') {
            if (unicode_ && digit(peek())) {
                fail("a legacy octal escape is not allowed with the u flag");
            }
            return;
        }
        if (digit(c)) {
            std::uint32_t n = static_cast<std::uint32_t>(c - '0');
            while (digit(peek()) && n < 100000) {
                n = n * 10 + static_cast<std::uint32_t>(peek() - '0');
                ++i_;
            }
            if (!in_class_) {
                back_refs_.push_back(n);
            } else if (unicode_) {
                fail("a decimal escape is not allowed in a character class with the u flag");
            }
            return;
        }
        if (c == 'p' || c == 'P') {
            // `\p{Name}` / `\p{Name=Value}` under `u`: the braces and something
            // in them are judged, the property name is not - that table is the
            // rx engine's. Without `u` it is the letter p.
            if (!unicode_) { return; }
            const std::size_t end = peek() == '{' ? s_.find('}', i_) : std::string_view::npos;
            if (end == std::string_view::npos || end == i_ + 1) {
                fail("`\\p` must be followed by `{...}` with the u flag");
                return;
            }
            i_ = end + 1;
            return;
        }
        if (std::string_view{"dDsSwWfnrtv"}.find(c) != std::string_view::npos) { return; }
        if (syntax_char(c)) { return; }
        if (c == '-' && in_class_) { return; } // ClassEscape :: `-` under u
        if (unicode_ && (static_cast<unsigned char>(c) < 0x80)) {
            fail(std::string{"`\\"} + c + "` is not an escape the u flag allows");
        }
    }

    void alternative() {
        group & g = groups_.back();
        g.earlier.insert(g.earlier.end(), g.current.begin(), g.current.end());
        g.current.clear();
    }

    void bind_name(const std::string & name) {
        group & g = groups_.back();
        if (std::find(g.current.begin(), g.current.end(), name) != g.current.end()) {
            fail("the group name `" + name + "` is bound twice in one alternative");
            return;
        }
        g.current.push_back(name);
        names_.push_back(name);
    }

    // `(?<name>`: the name is an IdentifierName, read up to `>`. Only the
    // ASCII half of ID_Start/ID_Continue is judged; a non-ASCII byte or a
    // `\u` escape is taken as a letter rather than looked up.
    [[nodiscard]] bool group_name(std::string & out) {
        const std::size_t end = s_.find('>', i_);
        if (end == std::string_view::npos) {
            fail("the group name is never closed with `>`");
            return false;
        }
        const std::string_view name = s_.substr(i_, end - i_);
        if (name.empty()) {
            fail("a group name may not be empty");
            return false;
        }
        for (std::size_t k = 0; k < name.size(); ++k) {
            const char c = name[k];
            if (c == '\\') { // `\uXXXX` or `\u{...}` in a name
                if (k + 1 >= name.size() || name[k + 1] != 'u') {
                    fail("only a `\\u` escape may appear in a group name");
                    return false;
                }
                k += 1;
                if (k + 1 < name.size() && name[k + 1] == '{') {
                    const std::size_t close = name.find('}', k);
                    if (close == std::string_view::npos) {
                        fail("the `\\u{` escape in the group name is never closed");
                        return false;
                    }
                    k = close;
                } else {
                    k += 4;
                }
                continue;
            }
            const bool ok = ascii_letter(c) || c == '$' || c == '_' ||
                            static_cast<unsigned char>(c) >= 0x80 || (k > 0 && digit(c));
            if (!ok) {
                fail("`" + std::string{name} + "` is not a group name");
                return false;
            }
        }
        out = std::string{name};
        i_ = end + 1;
        return true;
    }

    void open() {
        ++i_; // `(`
        quantifiable_ = false;
        group g;
        if (peek() != '?') {
            g.capturing = true;
            ++captures_;
            groups_.push_back(std::move(g));
            return;
        }
        ++i_; // `?`
        const char c = peek();
        if (c == ':' || c == '=' || c == '!') {
            g.lookahead = c != ':';
            ++i_;
            groups_.push_back(std::move(g));
            return;
        }
        if (c == '<' && (peek(1) == '=' || peek(1) == '!')) {
            g.lookbehind = true;
            i_ += 2;
            groups_.push_back(std::move(g));
            return;
        }
        if (c == '<') {
            ++i_;
            std::string name;
            if (!group_name(name)) { return; }
            g.capturing = true;
            ++captures_;
            groups_.push_back(std::move(g));
            bind_name(name);
            return;
        }
        // `(?ims-ims:` - the modifiers (22.2.1: each of i, m, s at most once
        // across both halves, and not both halves empty).
        std::string seen;
        bool dash = false;
        std::size_t before = 0;
        std::size_t after = 0;
        for (;;) {
            const char m = peek();
            if (m == 'i' || m == 'm' || m == 's') {
                if (seen.find(m) != std::string::npos) {
                    fail(std::string{"the modifier `"} + m + "` repeats");
                    return;
                }
                seen.push_back(m);
                (dash ? after : before) += 1;
                ++i_;
                continue;
            }
            if (m == '-' && !dash) {
                dash = true;
                ++i_;
                continue;
            }
            break;
        }
        if (peek() != ':' || (before == 0 && after == 0)) {
            fail("`(?` must be followed by `:`, `=`, `!`, `<=`, `<!`, `<name>` or modifiers");
            return;
        }
        ++i_; // `:`
        groups_.push_back(std::move(g));
    }

    void close() {
        ++i_;
        if (groups_.size() <= 1) {
            fail("`)` with no group to close");
            return;
        }
        group done = std::move(groups_.back());
        groups_.pop_back();
        group & parent = groups_.back();
        // The union of the closed group's alternatives - one name in two of
        // them is one name - joins the parent's current alternative.
        std::vector<std::string> bound = std::move(done.current);
        for (const std::string & name : done.earlier) {
            if (std::find(bound.begin(), bound.end(), name) == bound.end()) {
                bound.push_back(name);
            }
        }
        for (const std::string & name : bound) {
            if (std::find(parent.current.begin(), parent.current.end(), name) !=
                parent.current.end()) {
                fail("the group name `" + name + "` is bound twice in one alternative");
                return;
            }
            parent.current.push_back(name);
        }
        // A lookbehind is never quantifiable; a lookahead is, without `u`
        // (B.1.2 QuantifiableAssertion). quantifier() reports the rest.
        quantifiable_ = !done.lookbehind && !(done.lookahead && unicode_);
    }

    std::string_view s_;
    bool unicode_;
    std::size_t i_ = 0;
    bool in_class_ = false;
    bool quantifiable_ = false;
    std::uint32_t captures_ = 0;
    std::vector<group> groups_;
    std::vector<std::string> names_;
    std::vector<std::string> k_refs_;
    std::vector<std::uint32_t> back_refs_;
    std::optional<std::string> error_;
};

} // namespace

std::optional<std::string> regexp_literal_error(std::string_view lexeme) {
    if (lexeme.size() < 2 || lexeme.front() != '/') { return std::nullopt; }
    // The closing `/`: the first one outside a class that is not escaped.
    // The lexer stopped at a line feed when there was none, so the token can
    // end without one - `/a\/` is exactly that.
    std::size_t close = std::string_view::npos;
    bool in_class = false;
    for (std::size_t i = 1; i < lexeme.size(); ++i) {
        const char c = lexeme[i];
        if (c == '\\') {
            ++i;
            continue;
        }
        if (c == '[') { in_class = true; }
        if (c == ']') { in_class = false; }
        if (c == '/' && !in_class) {
            close = i;
            break;
        }
    }
    if (close == std::string_view::npos) {
        return "the regular expression literal is never closed";
    }
    const std::string_view body = lexeme.substr(1, close - 1);
    const std::string_view flags = lexeme.substr(close + 1);
    std::string seen;
    for (const char f : flags) {
        // The lexer reads any non-ASCII byte as part of the flags, a
        // Unicode space after the literal included - not judged here.
        if (static_cast<unsigned char>(f) >= 0x80) { break; }
        if (std::string_view{"dgimsuvy"}.find(f) == std::string_view::npos) {
            return std::string{"`"} + f + "` is not a regular expression flag";
        }
        if (seen.find(f) != std::string::npos) {
            return std::string{"the flag `"} + f + "` repeats";
        }
        seen.push_back(f);
    }
    if (seen.find('u') != std::string::npos && seen.find('v') != std::string::npos) {
        return "the flags `u` and `v` may not be combined";
    }
    // `v` changes what a character class is; the body is not judged under it.
    if (seen.find('v') != std::string::npos) { return std::nullopt; }
    return scan{body, seen.find('u') != std::string::npos}.run();
}

} // namespace ctbrowser::script::detail::early
