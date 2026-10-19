//===- RegExp.hpp - RegExp literals and String.prototype.replace -*- C++ -*-===//
//
// The RegExp a generated program builds for `text.replace(/pattern/g, fn)` or
// `new RegExp(source)`, String.prototype.replace with a function replacement,
// String.prototype.match, and RegExp.prototype.test. Only programs that
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

#include "ctcompile/CTNative/Runtime/ctnative.hpp"

#include <concepts>
#include <cstddef>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctnative {

/// A RegExp with the subset of flags the compiler admits: `g`, or `i` where
/// the text is ASCII (std::regex folds case per byte), or none.
class reg_exp {
    std::regex pattern;
    bool global_flag;

public:
    reg_exp(std::string_view source, std::string_view flags)
        : pattern(std::string{source}, flags.find('i') != std::string_view::npos
                                           ? std::regex::ECMAScript | std::regex::icase
                                           : std::regex::ECMAScript),
          global_flag(flags.find('g') != std::string_view::npos) {}

    const std::regex & engine() const { return pattern; }
    bool global() const { return global_flag; }
};

/// What String.prototype.match answers for a RegExp without `g`
/// (RegExpBuiltinExec, ECMA-262 22.2.7.2): the matched text and each group, or
/// null when nothing matched.
class match_result {
    std::optional<std::vector<js_string>> parts;

public:
    explicit match_result(std::optional<std::vector<js_string>> found) : parts(std::move(found)) {}

    /// `m[k]` where the compiler proved group `k` takes part in every match:
    /// its text, or, when nothing matched, the TypeError reading `k` of null
    /// throws (ECMA-262 7.1.18 ToObject), with the interpreter's message.
    const js_string & group(std::size_t k) const {
        if (!parts) {
            throw type_error("Cannot read properties of null (reading '" + std::to_string(k) +
                             "')");
        }
        return (*parts)[k];
    }
};

/// RegExp.prototype.test (ECMA-262 22.2.6.16) of a RegExp without `g`:
/// whether the pattern matches anywhere in the text (RegExpBuiltinExec from
/// lastIndex 0, which a non-global RegExp never moves).
struct reg_exp_test_method {
    js_boolean_t call(const reg_exp & self, const js_string & text) const {
        return js_boolean_t{std::regex_search(text.value(), self.engine())};
    }
};

/// The RegExp prototype's methods native code calls.
struct reg_exp_prototype {
    reg_exp_test_method test;
};

/// The RegExp constructor's call, as the literal `/source/flags` evaluates
/// (ECMA-262 22.2.3.1 RegExpCreate), and `new RegExp(source)`, whose flags are
/// empty.
struct reg_exp_constructor {
    reg_exp_prototype prototype;
    reg_exp operator()(std::string_view source, std::string_view flags = {}) const {
        return reg_exp{source, flags};
    }
    /// Literal bounds retain embedded NULs and exclude only the terminators.
    template <std::size_t N>
    reg_exp operator()(const char (&source)[N], std::string_view flags = {}) const {
        return (*this)(std::string_view{source, N - 1}, flags);
    }
    template <std::size_t N, std::size_t M>
    reg_exp operator()(const char (&source)[N], const char (&flags)[M]) const {
        return (*this)(std::string_view{source, N - 1}, std::string_view{flags, M - 1});
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

/// Keeps unmatched text around each replacement, checking the global flag
/// after the appender runs so a callback observes the same sequence.
template <class Appender>
js_string replace_matches(const js_string & self, const reg_exp & pattern, Appender & append) {
    const std::string & text = self.value();
    std::string out;
    auto kept = text.cbegin();
    for (std::sregex_iterator it{text.cbegin(), text.cend(), pattern.engine()}, end; it != end;
         ++it) {
        const std::smatch & match = *it;
        out.append(kept, match[0].first);
        append(out, match, text);
        kept = match[0].second;
        if (!pattern.global()) { break; }
    }
    out.append(kept, text.cend());
    ctbrowser::join_surrogates(out);
    return js_string{std::move(out)};
}
} // namespace detail

/// String.prototype.match (ECMA-262 22.1.3.13) with a RegExp without `g`:
/// RegExpExec from lastIndex 0, the first match and its groups.
inline match_result match_method::call(const js_string & self, const reg_exp & pattern) const {
    std::smatch found;
    if (!std::regex_search(self.value(), found, pattern.engine())) {
        return match_result{std::nullopt};
    }
    std::vector<js_string> parts;
    for (const auto & part : found) { parts.emplace_back(part.str()); }
    return match_result{std::move(parts)};
}

/// String.prototype.replace (ECMA-262 22.1.3.19) with a RegExp and a String
/// replacement, through RegExp.prototype[@@replace] (22.2.6.11): each match
/// (every one with `g`, the first without) is replaced by GetSubstitution of
/// the replacement, and the text between matches is kept.
inline js_string replace_method::call(const js_string & self, const reg_exp & pattern,
                                      const js_string & replacement) const {
    auto append = [&](std::string & out, const std::smatch & match, const std::string & text) {
        detail::append_substitution(out, replacement.value(), match, text);
    };
    return detail::replace_matches(self, pattern, append);
}

/// String.prototype.replace (ECMA-262 22.1.3.19) with a RegExp and a function
/// replacement, through RegExp.prototype[@@replace] (22.2.6.11): each match
/// (every one with `g`, the first without) is replaced by ToString of the
/// function's result, and the text between matches is kept. The callback is a
/// template parameter and is never stored.
template <class Callback>
js_string replace_method::call(const js_string & self, const reg_exp & pattern,
                               Callback callback) const {
    auto append = [&](std::string & out, const std::smatch & match, const std::string &) {
        out += (js_string{} + detail::call_replacer(callback, match)).value();
    };
    return detail::replace_matches(self, pattern, append);
}

} // namespace ctnative
