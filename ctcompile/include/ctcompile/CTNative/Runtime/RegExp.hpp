//===- RegExp.hpp - RegExp literals and String.prototype.replace -*- C++ -*-===//
//
// The RegExp a generated program builds for `text.replace(/pattern/g, fn)`,
// and String.prototype.replace with a function replacement. Only programs that
// lower such a call include this header, so the others never parse <regex>.
//
// std::regex with the ECMAScript grammar runs the pattern. The compiler admits
// only patterns whose meaning that engine shares with JavaScript over WTF-8
// text: ASCII literals, non-negated ASCII classes, groups, alternation and `+`,
// with at most the `g` flag. Each of those matches whole ASCII characters, and
// an ASCII byte is never part of a multibyte sequence, so a byte match is the
// code-unit match JavaScript makes. It refuses `.`, `\s`, `\w`, `\d`, `\b`,
// negated classes (each can match one byte of a multibyte character or differs
// with the locale) and anything that can match the empty string.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "String.hpp"

#include <concepts>
#include <regex>
#include <string>
#include <string_view>

namespace ctnative {

/// A RegExp literal with the subset of flags the compiler admits: `g` or none.
class reg_exp {
    std::regex pattern;
    bool global_flag;

public:
    reg_exp(std::string_view source, std::string_view flags)
        : pattern(std::string{source}, std::regex::ECMAScript),
          global_flag(flags.find('g') != std::string_view::npos) {}

    const std::regex & engine() const { return pattern; }
    bool global() const { return global_flag; }
};

/// The RegExp constructor's call, as the literal `/source/flags` evaluates
/// (ECMA-262 22.2.3.1 RegExpCreate).
struct reg_exp_constructor {
    reg_exp operator()(std::string_view source, std::string_view flags) const {
        return reg_exp{source, flags};
    }
};
inline constexpr reg_exp_constructor RegExp{};

namespace detail {
/// One captured substring as a String. The compiler admits a callback that
/// reads a group only when the group takes part in every match, so an
/// unmatched group (undefined in JavaScript) is never read.
inline js_string group(const std::smatch & match, std::size_t index) {
    return js_string{match[index].str()};
}

/// Calls a replacement function with as many of (matched, p1, p2) as it
/// declares (ECMA-262 22.2.6.11 step 14.k). The compiler refuses a callback
/// that declares the position or the whole string, which count UTF-16 units.
template <class Callback> decltype(auto) call_replacer(Callback & callback, const std::smatch & m) {
    if constexpr (std::invocable<Callback &, js_string, js_string, js_string>) {
        return callback(group(m, 0), group(m, 1), group(m, 2));
    } else if constexpr (std::invocable<Callback &, js_string, js_string>) {
        return callback(group(m, 0), group(m, 1));
    } else if constexpr (std::invocable<Callback &, js_string>) {
        return callback(group(m, 0));
    } else {
        return callback();
    }
}
/// GetSubstitution (ECMA-262 22.1.3.19.1) of `replacement` for `match` in
/// `text`, appended to `out`: `$$` is `$`, `$&` the match, `` $` `` and `$'`
/// the text before and after it, `$n` and `$nn` a group (empty when it did not
/// take part), preferring two digits that name a group. Any other `$` is
/// itself. The compiler admits no named groups, so `$<` is literal too.
inline void append_substitution(std::string & out, std::string_view replacement,
                                const std::smatch & match, const std::string & text) {
    const std::size_t groups = match.size() - 1;
    const auto digit = [&](std::size_t at) {
        return at < replacement.size() && replacement[at] >= '0' && replacement[at] <= '9'
                   ? static_cast<std::size_t>(replacement[at] - '0')
                   : std::string::npos;
    };
    for (std::size_t i = 0; i < replacement.size(); ++i) {
        const char next = i + 1 < replacement.size() ? replacement[i + 1] : '\0';
        if (replacement[i] != '$' || next == '\0') {
            out += replacement[i];
        } else if (next == '$') {
            out += '$';
            ++i;
        } else if (next == '&') {
            out += match[0].str();
            ++i;
        } else if (next == '`') {
            out.append(text.cbegin(), match[0].first);
            ++i;
        } else if (next == '\'') {
            out.append(match[0].second, text.cend());
            ++i;
        } else if (const std::size_t one = digit(i + 1); one != std::string::npos) {
            const std::size_t two = digit(i + 2);
            if (two != std::string::npos && one * 10 + two >= 1 && one * 10 + two <= groups) {
                out += match[one * 10 + two].str();
                i += 2;
            } else if (one >= 1 && one <= groups) {
                out += match[one].str();
                ++i;
            } else {
                out += '$';
            }
        } else {
            out += '$';
        }
    }
}
} // namespace detail

/// String.prototype.replace (ECMA-262 22.1.3.19) with a RegExp and a String
/// replacement, through RegExp.prototype[@@replace] (22.2.6.11): each match
/// (every one with `g`, the first without) is replaced by GetSubstitution of
/// the replacement, and the text between matches is kept.
inline js_string replace_method::call(const js_string & self, const reg_exp & pattern,
                                      const js_string & replacement) const {
    const std::string & text = self.value();
    std::string out;
    auto kept = text.cbegin();
    for (std::sregex_iterator it{text.cbegin(), text.cend(), pattern.engine()}, end; it != end;
         ++it) {
        const std::smatch & match = *it;
        out.append(kept, match[0].first);
        detail::append_substitution(out, replacement.value(), match, text);
        kept = match[0].second;
        if (!pattern.global()) { break; }
    }
    out.append(kept, text.cend());
    return js_string{std::move(out)};
}

/// String.prototype.replace (ECMA-262 22.1.3.19) with a RegExp and a function
/// replacement, through RegExp.prototype[@@replace] (22.2.6.11): each match
/// (every one with `g`, the first without) is replaced by ToString of the
/// function's result, and the text between matches is kept. The callback is a
/// template parameter and is never stored.
template <class Callback>
js_string replace_method::call(const js_string & self, const reg_exp & pattern,
                               Callback callback) const {
    const std::string & text = self.value();
    std::string out;
    auto kept = text.cbegin();
    for (std::sregex_iterator it{text.cbegin(), text.cend(), pattern.engine()}, end; it != end;
         ++it) {
        const std::smatch & match = *it;
        out.append(kept, match[0].first);
        out += (js_string{} + detail::call_replacer(callback, match)).value();
        kept = match[0].second;
        if (!pattern.global()) { break; }
    }
    out.append(kept, text.cend());
    return js_string{std::move(out)};
}

} // namespace ctnative
