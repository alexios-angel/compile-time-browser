// ctbrowser.script builtins - String, base64, structuredClone, RegExp and Symbol.
//
// One of five files carved out of a 4,118-line builtins.cpp on 2026-08-09.
// Everything shared - the argument helpers, namespace detail, and these
// functions' declarations - is in internal.hpp.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

namespace {

// --- WHAT EVERY String.prototype METHOD BEGINS WITH ------------------------
//
// 22.1.3: every method in the clause opens with the same two steps -
// RequireObjectCoercible(this value), a TypeError for null and undefined, and
// then ToString(this value). `detail::this_string` does only the SECOND, so
// `String.prototype.trim.call(null)` answered "null",
// `charAt.call(undefined)` answered "u", and `indexOf.call(null, "u")` answered
// 0. test262 counts 45 files across 34 methods that assert the TypeError
// instead - `this-value-not-obj-coercible.js`, `this-is-null-throws.js` and
// `return-abrupt-from-this.js` are the three spellings (counted over the pinned
// corpus, 2026-09-07).
//
// The refusal is installed at the INSTALL SITE rather than repeated in each
// body, which is what makes it impossible for the next method added here to
// forget it: a body is handed the already-coerced string and never sees the
// receiver at all.
using text_body = std::function<value(context &, const std::string &, std::span<value>)>;

[[nodiscard]] native_fn on_text(std::string owner, text_body body) {
    return [owner = std::move(owner), body = std::move(body)](context & c,
                                                              std::span<value> a) -> value {
        if (c.current_this().is_nullish()) {
            c.throw_error("TypeError", owner + " called on null or undefined");
            return value::undefined();
        }
        return body(c, detail::this_string(c), a);
    };
}

// thisStringValue, 22.1.3.35 - `toString` and `valueOf` are NOT generic, which
// is the one place a String method wants its receiver rather than its
// receiver's text. `String.prototype.toString.call(1)` is a TypeError, not
// "1", and it has to be: `''.concat({toString: String.prototype.toString})`
// would otherwise recurse.
//
// There are no String WRAPPER objects in this engine - `new String(x)` is a
// conversion, see the `__conversion` flag on the constructor - so the only
// object carrying a [[StringData]] slot is String.prototype itself, whose slot
// is the empty String by 22.1.3. That is exactly the reasoning
// `detail::this_number_value` uses to make `Number.prototype.toString()`
// answer "0", and the two now agree.
[[nodiscard]] value this_string_value(context & cx, const char * method) {
    const value self = cx.current_this();
    if (self.is_string()) { return self; }
    if (self.is_object() && self.as_heap() == cx.prototype(context::proto_kind::string)) {
        return cx.string(std::string{});
    }
    cx.throw_error("TypeError", std::string{method} + " requires that 'this' be a String");
    return value::undefined();
}

// IsRegExp, 7.2.8 - what `includes`, `startsWith` and `endsWith` REFUSE.
//
// The three are specified to throw rather than stringify: `'a/b'.includes(/b/)`
// searching for the six characters of the pattern's source is a mistake often
// enough that the specification made it loud. This engine stringified, found
// nothing, and answered false.
//
// @@match WINS over the internal slot when it is present at all, which is the
// whole reason the operation is written this way: an ordinary object can claim
// to be a pattern and a real RegExp can disclaim it. `__regex` is this engine's
// [[RegExpMatcher]] - see install_regexp, which defines it on every instance.
[[nodiscard]] bool is_regexp(context & cx, value v) {
    if (!v.is_object()) { return false; }
    const value matcher = cx.lookup_property(v, "@@match");
    if (!matcher.is_undefined()) { return context::truthy(matcher); }
    return context::truthy(cx.lookup_property(v, "__regex"));
}

// ToUint32 (7.1.7) over an argument that MAY BE AN OBJECT, for the one place a
// String method needs it: `split`'s limit. `context::to_uint32` is the static
// coercion and cannot run a user `valueOf`, so
// `"a,b".split(",", {valueOf: () => 1})` read NaN, became 0, and answered [].
[[nodiscard]] double uint32_arg(context & cx, value v) {
    const double n = cx.to_number_value(v);
    if (!std::isfinite(n)) { return 0.0; }
    return static_cast<double>(static_cast<std::uint32_t>(
        static_cast<std::int64_t>(std::fmod(std::trunc(n), 4294967296.0))));
}

// --- TrimString's whitespace set, over UTF-8 -------------------------------
//
// `trim`, `trimStart` and `trimEnd` were `find_first_not_of(" \t\n\r\f\v")`,
// the ASCII six. The specification's set (22.1.3.32.1 takes the union of 12.2
// WhiteSpace and 12.3 LineTerminator) has fourteen more, and every one of them
// is NON-ASCII - so unlike case folding, which needs tables this engine
// deliberately does not carry, this one is answerable EXACTLY from the bytes:
// UTF-8 is self-synchronising, the code points are fixed, and no locale enters
// into it. 30 test262 files in trim/, trimStart/ and trimEnd/ assert them.
struct code_point {
    std::uint32_t value;
    std::size_t width;
};

// NOT A CODE POINT, and it cannot be mistaken for one: every use of this
// decoder asks "is this whitespace", and the answer for a byte that is not part
// of a well-formed sequence must be NO. A stray 0xA0 is not U+00A0 and an
// overlong 0xC1 0xA0 is not U+0020 - trimming either would eat a byte the page
// put there, which is the one thing a trim must never do.
inline constexpr std::uint32_t not_a_code_point = 0xFFFFFFFFu;

// One code point decoded FORWARD from `at`. A malformed lead, a truncated
// sequence and an overlong encoding are each ONE BYTE WIDE and not a code
// point, so a scan can never stall on a stray byte and can never fold one into
// the character it was pretending to be - a JS string here is bytes and nothing
// guarantees they are well-formed UTF-8.
[[nodiscard]] code_point utf8_at(std::string_view s, std::size_t at) {
    const auto lead = static_cast<std::uint32_t>(static_cast<unsigned char>(s[at]));
    const std::size_t left = s.size() - at;
    const auto trail = [&](std::size_t i) {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + i]) & 0x3Fu);
    };
    const auto continues = [&](std::size_t n) {
        if (left < n) { return false; }
        for (std::size_t i = 1; i < n; ++i) {
            if ((static_cast<unsigned char>(s[at + i]) & 0xC0u) != 0x80u) { return false; }
        }
        return true;
    };
    const auto checked = [](std::uint32_t cp, std::uint32_t least, std::size_t width) {
        return cp < least ? code_point{not_a_code_point, 1} : code_point{cp, width};
    };
    if (lead < 0x80u) { return {lead, 1}; }
    if ((lead & 0xE0u) == 0xC0u && continues(2)) {
        return checked(((lead & 0x1Fu) << 6) | trail(1), 0x80u, 2);
    }
    if ((lead & 0xF0u) == 0xE0u && continues(3)) {
        return checked(((lead & 0x0Fu) << 12) | (trail(1) << 6) | trail(2), 0x800u, 3);
    }
    if ((lead & 0xF8u) == 0xF0u && continues(4)) {
        return checked(((lead & 0x07u) << 18) | (trail(1) << 12) | (trail(2) << 6) | trail(3),
                       0x10000u, 4);
    }
    return {not_a_code_point, 1};
}

// U+180E IS NOT HERE. It was a space separator in Unicode 6.2 and stopped being
// one in 6.3, and test262's trim/u180e.js asserts it is left alone - which is
// the sort of thing a hand-written set gets wrong by copying an old table.
[[nodiscard]] bool is_js_space(std::uint32_t cp) {
    switch (cp) {
    case 0x09:   // TAB
    case 0x0A:   // LF, a LineTerminator
    case 0x0B:   // VT
    case 0x0C:   // FF
    case 0x0D:   // CR, a LineTerminator
    case 0x20:   // SP
    case 0xA0:   // NBSP
    case 0x1680: // OGHAM SPACE MARK
    case 0x2028: // LINE SEPARATOR
    case 0x2029: // PARAGRAPH SEPARATOR
    case 0x202F: // NARROW NO-BREAK SPACE
    case 0x205F: // MEDIUM MATHEMATICAL SPACE
    case 0x3000: // IDEOGRAPHIC SPACE
    case 0xFEFF: // ZWNBSP
        return true;
    default: return cp >= 0x2000 && cp <= 0x200A; // EN QUAD .. HAIR SPACE
    }
}

// TrimString, 22.1.3.32.1: the byte range to KEEP. Written as bounds rather
// than as a copy so all three methods are one algorithm - they were three
// copies of the same set, which is how `trim` and `trimEnd` came to disagree
// about an empty result.
void trim_bounds(std::string_view s, bool from_start, bool from_end, std::size_t & from,
                 std::size_t & to) {
    from = 0;
    to = s.size();
    if (from_start) {
        while (from < to) {
            const code_point cp = utf8_at(s, from);
            if (!is_js_space(cp.value)) { break; }
            from += cp.width;
        }
    }
    if (from_end) {
        while (to > from) {
            // Back up over continuation bytes to the lead of the last code
            // point, then require that it ENDS where the range does - a
            // truncated sequence is not whitespace and must not be eaten.
            std::size_t at = to - 1;
            while (at > from && (static_cast<unsigned char>(s[at]) & 0xC0u) == 0x80u) { --at; }
            const code_point cp = utf8_at(s, at);
            if (at + cp.width != to || !is_js_space(cp.value)) { break; }
            to = at;
        }
    }
}

// RegExpCreate, 22.2.3.2, for the three methods whose argument is a PATTERN and
// not a string: `match`, `search` and `matchAll` are each specified to build a
// RegExp out of whatever they were handed and then invoke the pattern's own
// protocol on it.
//
// All three answered "no match" for a non-object instead - `"1234".match(3)`
// was null, `"abc".search("b")` was -1 - which is the commonest spelling of
// all and is SILENT: a page's `if (s.match(x))` reads null as "absent".
//
// The factory is the RESERVED global the compiler emits for a regex literal,
// not `RegExp`, so a page that shadows RegExp cannot change what its own
// `String.prototype.match` means.
[[nodiscard]] value as_regexp(context & cx, value pattern, const char * flags) {
    if (pattern.is_object() && cx.lookup_property(pattern, "exec").is_callable()) {
        return pattern;
    }
    const value factory = cx.global(regexp_factory_name);
    if (!factory.is_callable()) { return value::undefined(); }
    // RegExpInitialize step 1: an undefined pattern is the EMPTY source, which
    // matches at 0 - it is not the four letters of "undefined".
    const value args[2] = {pattern.is_undefined() ? cx.string(std::string{})
                                                  : cx.string(cx.to_string(pattern)),
                           cx.string(std::string{flags})};
    return cx.call(factory, args);
}

// 22.1.3.14 step 2b and 22.1.3.20 step 2b: `matchAll` and `replaceAll` REFUSE a
// RegExp without `g`, because both mean "every match" and a non-global pattern
// cannot deliver one. Answering the first match twice, or once, is the wrong
// answer either way, so the specification made it a TypeError.
[[nodiscard]] bool refuse_non_global(context & cx, value pattern, const char * method) {
    if (!is_regexp(cx, pattern)) { return false; }
    const value flags = cx.lookup_property(pattern, "flags");
    if (flags.is_nullish()) {
        cx.throw_error("TypeError", std::string{method} + " requires flags on its pattern");
        return true;
    }
    if (cx.to_string(flags).find('g') != std::string::npos) { return false; }
    cx.throw_error("TypeError", std::string{method} + " must be called with a global RegExp");
    return true;
}

} // namespace

// String.prototype
void install_string(context & cx) {
    using detail::method;
    using detail::new_table;
    object_object * string_proto = new_table(cx);
    // `on_text` above is what supplies steps 1 and 2 of 22.1.3, so every body
    // here is handed the already-coerced string.
    const auto text = [&cx, string_proto](const char * name, double arity, text_body body) {
        method(cx, string_proto, name, arity,
               on_text(std::string{"String.prototype."} + name, std::move(body)));
    };
    text("charAt", 1, [](context & c, const std::string & s, std::span<value> a) -> value {
        // A NEGATIVE POSITION IS OUT OF RANGE, not clamped to zero - that is
        // `at`'s job, not `charAt`'s. Clamping made `"abc".charAt(-1)` answer
        // "a" where the specification says "".
        const double i = integer_arg(c, a, 0);
        if (!(i >= 0 && i < static_cast<double>(s.size()))) { return c.string(std::string{}); }
        return c.string(std::string{s[static_cast<std::size_t>(i)]});
    });
    // `at` is charAt that counts from the END for a negative index, which is
    // the whole reason to reach for it - `s.at(-1)` is the last character.
    // Arrays had it and strings did not, and the two are meant to match.
    text("at", 1, [](context & c, const std::string & s, std::span<value> a) -> value {
        // integer_arg, not num_at: a NaN index is 0 here, and casting it to
        // size_t instead is what hung the engine on `s.at(undefined)`.
        double i = integer_arg(c, a, 0);
        if (i < 0) { i += static_cast<double>(s.size()); }
        // Written so a NaN could not survive it even if one arrived: the guard
        // is now a range CHECK rather than two comparisons that are both false
        // for NaN and fall through to an out-of-bounds read.
        if (!(i >= 0 && i < static_cast<double>(s.size()))) { return value::undefined(); }
        return c.string(std::string{s[static_cast<std::size_t>(i)]});
    });
    // `toString` and `valueOf`, 22.1.3.28 and 22.1.3.35. Both exist so that
    // generic code written against "any value" works on a string: `String(x)`,
    // `'' + x` and a template literal all reach for toString, and a library
    // that calls it directly - p5.js does, on its own colour objects and on
    // plain strings through the same path - got "undefined is not a function".
    //
    // NOT GENERIC, unlike everything else in this file: see this_string_value.
    method(cx, string_proto, "toString", 0, [](context & c, std::span<value>) {
        return this_string_value(c, "String.prototype.toString");
    });
    method(cx, string_proto, "valueOf", 0, [](context & c, std::span<value>) {
        return this_string_value(c, "String.prototype.valueOf");
    });
    text("codePointAt", 1, [](context & c, const std::string & str, std::span<value> a) -> value {
        // Out of range - including NEGATIVE, which used to clamp to zero and
        // answer with the first character - is undefined.
        const double raw = integer_arg(c, a, 0);
        if (!(raw >= 0 && raw < static_cast<double>(str.size()))) { return value::undefined(); }
        const auto i = static_cast<std::size_t>(raw);
        // BYTES, not code points - strings are bytes in this engine
        // (docs/script.md), so this agrees with charCodeAt rather than
        // pretending to a UTF-16 view that nothing else here has.
        return value::number(static_cast<double>(static_cast<unsigned char>(str[i])));
    });
    // `normalize` is the IDENTITY here, and says so: strings are bytes, so
    // there is no decomposition to compose. Returning the string unchanged is
    // what a page that calls it defensively expects; refusing would break
    // pages that only ever pass ASCII, which is all of them here.
    //
    // THE FORM IS STILL CHECKED. 22.1.3.15 step 4 makes anything but the four
    // names a RangeError, and that half of the method IS answerable without
    // Unicode tables - it is what tells a page that asked for "NFKC1" it made a
    // typo, rather than handing the string back and letting the typo live.
    text("normalize", 0, [](context & c, const std::string & s, std::span<value> a) -> value {
        const std::string form = has_index(a, 0) ? c.to_string(a[0]) : std::string{"NFC"};
        if (form != "NFC" && form != "NFD" && form != "NFKC" && form != "NFKD") {
            c.throw_error("RangeError",
                          "The normalization form should be one of NFC, NFD, NFKC, NFKD");
            return c.string(std::string{});
        }
        return c.string(s);
    });
    text("localeCompare", 1,
         [](context & c, const std::string & self, std::span<value> a) -> value {
             // Byte order, which is the locale this engine has. The argument is
             // ToString'd through `arg_at` and not `str_at`: a MISSING one is
             // `undefined`, and ToString(undefined) is "undefined" - so
             // `"undefined".localeCompare()` is 0, not 1 against the empty string.
             const std::string other = c.to_string(arg_at(a, 0));
             return value::number(self < other ? -1 : (self == other ? 0 : 1));
         });
    text("charCodeAt", 1, [](context & c, const std::string & s, std::span<value> a) -> value {
        // Out of range - including NEGATIVE - is NaN, not the first character.
        const double i = integer_arg(c, a, 0);
        if (!(i >= 0 && i < static_cast<double>(s.size()))) { return value::number(std::nan("")); }
        return value::number(
            static_cast<double>(static_cast<unsigned char>(s[static_cast<std::size_t>(i)])));
    });
    // ALL FIVE OF THESE TAKE A POSITION, and all five used to ignore it - so
    // `"abc".indexOf("a", 1)` answered 0 where the specification says -1, and
    // the idiom for walking every occurrence,
    // `while ((i = s.indexOf(x, i + 1)) !== -1)`, either spun on 0 forever or
    // reported the first hit again and again. Silent in every case.
    //
    // The needle is ToString'd through `arg_at` rather than `str_at`, because a
    // MISSING argument is `undefined` and ToString(undefined) is "undefined" -
    // `"abc".indexOf()` is -1, not 0 for an empty needle.
    //
    // AND THE ORDER OF THE TWO COERCIONS IS THE SPECIFICATION'S: ToString on
    // the needle first (22.1.3.9 step 3), ToIntegerOrInfinity on the position
    // second (step 4). It is observable, because either may be an object with a
    // `valueOf` - and the position went through the STATIC `context::to_number`,
    // which cannot run one at all, so `"abc".indexOf("c", {valueOf: () => 1})`
    // read NaN, became 0, and could not be told from `indexOf("c")`.
    text("indexOf", 1, [](context & c, const std::string & s, std::span<value> a) -> value {
        const std::string needle = c.to_string(arg_at(a, 0));
        const auto from = static_cast<std::size_t>(
            std::clamp(integer_arg(c, a, 1), 0.0, static_cast<double>(s.size())));
        const std::size_t found = s.find(needle, from);
        return value::number(found == std::string::npos ? -1 : static_cast<double>(found));
    });
    text("lastIndexOf", 1, [](context & c, const std::string & s, std::span<value> a) -> value {
        const std::string needle = c.to_string(arg_at(a, 0));
        // The position is the LAST index the match may START at, and it
        // defaults to the end. NaN means the end too - and an absent argument
        // IS NaN, because ToNumber(undefined) is NaN, so the two cases are one
        // line rather than a special case (22.1.3.10 steps 4 and 5).
        const double raw = c.to_number_value(arg_at(a, 1));
        const double at = std::isnan(raw) ? std::numeric_limits<double>::infinity() : raw;
        const auto last = at >= static_cast<double>(s.size())
                              ? std::string::npos
                              : static_cast<std::size_t>(std::max(0.0, at));
        const std::size_t found = s.rfind(needle, last);
        return value::number(found == std::string::npos ? -1 : static_cast<double>(found));
    });
    text("includes", 1, [](context & c, const std::string & s, std::span<value> a) -> value {
        if (is_regexp(c, arg_at(a, 0))) {
            c.throw_error("TypeError",
                          "First argument to String.prototype.includes must not be a regular "
                          "expression");
            return value::boolean(false);
        }
        const std::string needle = c.to_string(arg_at(a, 0));
        const auto from = static_cast<std::size_t>(
            std::clamp(integer_arg(c, a, 1), 0.0, static_cast<double>(s.size())));
        return value::boolean(s.find(needle, from) != std::string::npos);
    });
    text("startsWith", 1, [](context & c, const std::string & s, std::span<value> a) -> value {
        if (is_regexp(c, arg_at(a, 0))) {
            c.throw_error("TypeError",
                          "First argument to String.prototype.startsWith must not be a regular "
                          "expression");
            return value::boolean(false);
        }
        const std::string needle = c.to_string(arg_at(a, 0));
        const auto from = static_cast<std::size_t>(
            std::clamp(integer_arg(c, a, 1), 0.0, static_cast<double>(s.size())));
        return value::boolean(std::string_view{s}.substr(from).starts_with(needle));
    });
    text("endsWith", 1, [](context & c, const std::string & s, std::span<value> a) -> value {
        if (is_regexp(c, arg_at(a, 0))) {
            c.throw_error("TypeError",
                          "First argument to String.prototype.endsWith must not be a regular "
                          "expression");
            return value::boolean(false);
        }
        const std::string needle = c.to_string(arg_at(a, 0));
        // endsWith takes an END position, not a start: `"abc".endsWith("b", 2)`
        // asks whether the first two characters end in "b".
        const double end = has_index(a, 1) ? integer_arg(c, a, 1) : static_cast<double>(s.size());
        const auto stop =
            static_cast<std::size_t>(std::clamp(end, 0.0, static_cast<double>(s.size())));
        return value::boolean(std::string_view{s}.substr(0, stop).ends_with(needle));
    });
    text("slice", 2, [](context & c, const std::string & s, std::span<value> a) -> value {
        const std::size_t from = clamp_index(integer_arg(c, a, 0), s.size());
        // has_index: `slice(1, undefined)` ends at the LENGTH, not at 0.
        const std::size_t to =
            has_index(a, 1) ? clamp_index(integer_arg(c, a, 1), s.size()) : s.size();
        return c.string(to > from ? s.substr(from, to - from) : std::string{});
    });
    text("substring", 2, [](context & c, const std::string & s, std::span<value> a) -> value {
        // substring CLAMPS a negative to zero, unlike slice which counts from
        // the end - that difference between the two is the whole reason both
        // exist - and it SWAPS its arguments if they are backwards. has_index
        // so an explicit `undefined` end still means "to the end" rather than
        // zero.
        std::size_t from = static_cast<std::size_t>(std::max(0.0, integer_arg(c, a, 0)));
        std::size_t to = has_index(a, 1)
                             ? static_cast<std::size_t>(std::max(0.0, integer_arg(c, a, 1)))
                             : s.size();
        from = std::min(from, s.size());
        to = std::min(to, s.size());
        if (from > to) { std::swap(from, to); }
        return c.string(s.substr(from, to - from));
    });
    text("substr", 2, [](context & c, const std::string & s, std::span<value> a) -> value {
        // LEGACY, and present because real code still uses it - Phaser 4 calls
        // it fourteen times and died on the first. It is Annex B rather than
        // the main specification, which is why it was missed: it takes a START
        // and a LENGTH where slice and substring both take two positions.
        //
        // A NEGATIVE START COUNTS FROM THE END, which neither of the others
        // does. `"abcdef".substr(-2)` is "ef", and getting that wrong reads
        // from the front and looks almost right.
        //
        // integer_arg rather than num_at, for the reason `at` above needed it:
        // a NaN start reached `static_cast<std::size_t>` and the engine hung.
        const double raw = integer_arg(c, a, 0);
        const auto size = static_cast<double>(s.size());
        const double start = raw < 0 ? std::max(size + raw, 0.0) : std::min(raw, size);
        const auto from = static_cast<std::size_t>(start);
        // A missing length means "to the end"; a negative one means nothing.
        // has_index, so an explicit `undefined` length is also "to the end".
        double want = has_index(a, 1) ? integer_arg(c, a, 1) : size - start;
        if (std::isnan(want) || want < 0) { want = 0; }
        const auto count = static_cast<std::size_t>(std::min(want, size - start));
        return c.string(s.substr(from, count));
    });
    text("split", 2, [](context & c, const std::string & s, std::span<value> a) -> value {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        // THE LIMIT, which this used to ignore completely - so
        // `"a,b,c".split(",", 2)` handed back all three and `split(x, 0)` handed
        // back everything instead of nothing. `undefined` means unlimited, and
        // it is ToUint32 rather than an integer, so a negative wraps to a very
        // large number (which is why 2**32-1 and "no limit" behave alike).
        const std::size_t limit = has_index(a, 1) ? static_cast<std::size_t>(uint32_arg(c, a[1]))
                                                  : std::numeric_limits<std::size_t>::max();
        // Every exit goes through here, because the limit truncates whichever
        // branch produced the parts.
        const auto finish = [&]() -> value {
            if (result->items.size() > limit) { result->items.resize(limit); }
            return out;
        };
        // A REGEXP SEPARATOR. `str.split(/\r?\n/)` is how essentially every
        // library splits lines, and coercing the pattern to a string made it a
        // separator that never matched - so the whole input came back as one
        // element and nothing said anything. p5.js splits text into lines that
        // way, then takes Math.max over the widths, which for an unsplit line
        // is fine and for an EMPTY list is -Infinity.
        //
        // Driven through the pattern's own `exec`, like matchAll, so there is
        // one regex path and split cannot disagree with match about a boundary.
        // Searching the remainder each round rather than moving lastIndex means
        // a non-global pattern works too, which is the form this is written in.
        const value separator = arg_at(a, 0);
        const value exec =
            separator.is_object() ? c.lookup_property(separator, "exec") : value::undefined();
        // ToString(separator) is step 5 and the `lim = 0` return is step 6, IN
        // THAT ORDER: a separator with a `toString` is coerced even when the
        // limit already says the answer is []. Only the string path does it -
        // the regexp path stands in for RegExp.prototype[@@split], whose own
        // step 12 returns before touching the pattern.
        const std::string sep = exec.is_callable() ? std::string{} : c.to_string(separator);
        if (limit == 0) { return out; }
        if (exec.is_callable()) {
            std::string rest = s;
            while (!rest.empty()) {
                // RESET EVERY ROUND, because the subject shrinks. A global
                // pattern's exec resumes from `lastIndex`, and this hands it a
                // fresh remainder each time - so a stale index points past the
                // start of the new string and finds nothing. `'a b,c'
                // .split(/[ ,]/g)` split once and stopped.
                c.store_property(separator, "lastIndex", value::number(0));
                const value args[1] = {c.string(rest)};
                const value m = c.call(exec, args, separator);
                if (!m.is_array()) { break; }
                auto * parts = static_cast<array_object *>(m.as_heap());
                if (parts->items.empty()) { break; }
                const auto index =
                    static_cast<std::size_t>(std::max(0.0, context::to_number(parts->index)));
                const std::string whole = c.to_string(parts->items[0]);
                if (index >= rest.size()) { break; }
                result->items.push_back(c.string(rest.substr(0, index)));
                // A CAPTURE IN THE SEPARATOR IS KEPT, which is the one reason to
                // put a group in a split pattern at all.
                for (std::size_t g = 1; g < parts->items.size(); ++g) {
                    result->items.push_back(parts->items[g]);
                }
                // An empty match would otherwise never advance.
                rest = rest.substr(index + std::max<std::size_t>(whole.size(), 1));
            }
            result->items.push_back(c.string(rest));
            return finish();
        }
        if (separator.is_undefined()) {
            result->items.push_back(c.string(s));
            return finish();
        }
        if (sep.empty()) {
            for (const char ch : s) { result->items.push_back(c.string(std::string{ch})); }
            return finish();
        }
        std::size_t at = 0;
        while (true) {
            const std::size_t found = s.find(sep, at);
            if (found == std::string::npos) {
                result->items.push_back(c.string(s.substr(at)));
                break;
            }
            result->items.push_back(c.string(s.substr(at, found - at)));
            at = found + sep.size();
        }
        return finish();
    });
    // ONE REPLACEMENT, shared by `replace` and `replaceAll`.
    //
    // The pattern may be a STRING or a REGEXP, and the replacement may be a
    // string with `$` references or a FUNCTION - four combinations, and the
    // only difference between the two methods is how many matches they take.
    //
    // Regexes did not work here at all: the pattern was coerced with to_string
    // and looked for with std::string::find, so `s.replace(/ /g, '|')` searched
    // for the literal text of the regex object and, finding none, returned the
    // string unchanged. Silent, and it is the commonest use of the method -
    // acorn builds its keyword tables with exactly that call, so every keyword
    // matcher inside p5's own parser was a pattern that could never match.
    const auto substitute = [](const std::string & replacement, const std::string & matched,
                               const std::vector<std::string> & groups, std::size_t at,
                               const std::string & subject) {
        std::string out;
        for (std::size_t i = 0; i < replacement.size(); ++i) {
            if (replacement[i] != '$' || i + 1 >= replacement.size()) {
                out += replacement[i];
                continue;
            }
            const char next = replacement[i + 1];
            if (next == '$') {
                out += '$';
            } else if (next == '&') {
                out += matched;
            } else if (next == '`') {
                out += subject.substr(0, at);
            } else if (next == '\'') {
                out += subject.substr(std::min(subject.size(), at + matched.size()));
            } else if (next >= '1' && next <= '9') {
                // Two digits when there is a group to match them - `$12` is
                // group 12 where one exists and group 1 followed by "2" where
                // it does not.
                std::size_t which = static_cast<std::size_t>(next - '0');
                std::size_t used = 1;
                if (i + 2 < replacement.size() && replacement[i + 2] >= '0' &&
                    replacement[i + 2] <= '9') {
                    const std::size_t wider =
                        which * 10 + static_cast<std::size_t>(replacement[i + 2] - '0');
                    if (wider <= groups.size()) {
                        which = wider;
                        used = 2;
                    }
                }
                if (which <= groups.size()) { out += groups[which - 1]; }
                i += used;
                continue;
            } else {
                out += replacement[i];
                continue;
            }
            ++i;
        }
        return out;
    };

    const auto replace_with = [substitute](context & c, const std::string & self,
                                           std::span<value> a, bool all) {
        const value pattern = a.empty() ? value::undefined() : a[0];
        const value replacement = a.size() > 1 ? a[1] : value::undefined();

        // A REGEXP PATTERN, driven through its own `exec` so there is one regex
        // path and replace cannot disagree with match about a boundary.
        if (pattern.is_object() && c.lookup_property(pattern, "exec").is_callable()) {
            const value exec = c.lookup_property(pattern, "exec");
            // `g` on the pattern means every match even for `replace`; a plain
            // `replace` with a non-global pattern takes the first only.
            const bool every = all || context::truthy(c.lookup_property(pattern, "global"));
            std::string out;
            std::string rest = self;
            std::size_t consumed = 0;
            // An empty pattern matches at the END position too - `'ab'
            // .replace(/x*/g, '-')` is "-a-b-" and not "-a-b" - so one pass
            // runs with nothing left, and only a second one stops.
            bool tail_seen = false;
            for (std::size_t guard = 0; guard <= self.size() + 2; ++guard) {
                if (rest.empty()) {
                    if (tail_seen) { break; }
                    tail_seen = true;
                }
                c.store_property(pattern, "lastIndex", value::number(0));
                const value subject = c.string(rest);
                const value found = c.call(exec, std::span<const value>{&subject, 1}, pattern);
                if (!found.is_array()) { break; }
                auto * parts = static_cast<array_object *>(found.as_heap());
                if (parts->items.empty()) { break; }
                const std::string matched = c.to_string(parts->items[0]);
                const auto at =
                    static_cast<std::size_t>(std::max(0.0, context::to_number(parts->index)));
                if (at > rest.size()) { break; }
                std::vector<std::string> groups;
                for (std::size_t g = 1; g < parts->items.size(); ++g) {
                    groups.push_back(parts->items[g].is_undefined() ? std::string{}
                                                                    : c.to_string(parts->items[g]));
                }
                out += rest.substr(0, at);
                if (replacement.is_callable()) {
                    // (match, p1..pn, offset, whole) - the offset is into the
                    // ORIGINAL string, not into what is left of it.
                    std::vector<value> args;
                    args.push_back(c.string(matched));
                    for (const std::string & g : groups) { args.push_back(c.string(g)); }
                    args.push_back(value::number(static_cast<double>(consumed + at)));
                    args.push_back(c.string(self));
                    out += c.to_string(c.call(replacement, args));
                } else {
                    out +=
                        substitute(c.to_string(replacement), matched, groups, consumed + at, self);
                }
                // An empty match must still advance, or this never terminates.
                const std::size_t step = at + std::max<std::size_t>(matched.size(), 1);
                if (matched.empty() && at < rest.size()) { out += rest[at]; }
                consumed += step;
                rest = step >= rest.size() ? std::string{} : rest.substr(step);
                if (!every) { break; }
            }
            out += rest;
            return c.string(out);
        }

        // A STRING pattern: the first occurrence, or all of them.
        const std::string from = c.to_string(pattern);
        if (from.empty()) { return c.string(self); }
        std::string out;
        std::size_t at = 0;
        while (true) {
            const std::size_t found = self.find(from, at);
            if (found == std::string::npos) {
                out += self.substr(at);
                break;
            }
            out += self.substr(at, found - at);
            if (replacement.is_callable()) {
                const value args[3] = {c.string(from), value::number(static_cast<double>(found)),
                                       c.string(self)};
                out += c.to_string(c.call(replacement, args));
            } else {
                out += substitute(c.to_string(replacement), from, {}, found, self);
            }
            at = found + from.size();
            if (!all) {
                out += self.substr(at);
                break;
            }
        }
        return c.string(out);
    };

    text("replace", 2,
         [replace_with](context & c, const std::string & s, std::span<value> a) -> value {
             return replace_with(c, s, a, false);
         });
    // `match` - the single commonest thing done with a regular expression, and
    // it simply was not here. A page calling it got "undefined is not a
    // function" from inside whatever library it was using.
    //
    // The two forms return DIFFERENT SHAPES, which is the part that is easy to
    // get wrong: with `g` it is a flat list of the matched strings and nothing
    // else, and without it a single exec result carrying index, input and the
    // capture groups. Code branches on that difference.
    text("match", 1, [](context & c, const std::string & self, std::span<value> a) -> value {
        // 22.1.3.13 step 3: a non-RegExp argument is RegExpCreate'd, not
        // rejected. `"1234567890".match(3).index` is 2.
        const value pattern = as_regexp(c, arg_at(a, 0), "");
        if (!pattern.is_object()) { return value::null(); }
        const value exec = c.lookup_property(pattern, "exec");
        if (!exec.is_callable()) { return value::null(); }
        const value subject = c.string(self);
        const bool all = context::truthy(c.lookup_property(pattern, "global"));
        c.store_property(pattern, "lastIndex", value::number(0));
        if (!all) {
            const value found = c.call(exec, std::span<const value>{&subject, 1}, pattern);
            return found.is_nullish() ? value::null() : found;
        }
        value list = c.make_array();
        auto * items = static_cast<array_object *>(list.as_heap());
        double previous = -1;
        for (std::size_t guard = 0; guard <= self.size() + 1; ++guard) {
            const value found = c.call(exec, std::span<const value>{&subject, 1}, pattern);
            if (!found.is_array()) { break; }
            auto * parts = static_cast<array_object *>(found.as_heap());
            if (parts->items.empty()) { break; }
            items->items.push_back(parts->items[0]);
            const double now = context::to_number(c.lookup_property(pattern, "lastIndex"));
            if (!(now > previous)) { break; }
            previous = now;
        }
        // A global match that found nothing is NULL, not an empty array - the
        // usual guard is `if (m)`, and an empty array is truthy.
        return items->items.empty() ? value::null() : list;
    });
    // `search` - WHERE a pattern matches, or -1. It is the smallest of the
    // regular-expression string methods and it was the one missing, which is
    // worth stating plainly: Babylon's shader processor calls it on every
    // shader it compiles, so the processing rejected, the processed source
    // stayed empty, and every Babylon material compiled a program out of its
    // `#define` lines alone. Nothing threw where anyone would look - the
    // rejection was inside a promise the engine does not surface - and the
    // canvas simply showed the clear colour. One missing method, and a whole
    // renderer draws nothing.
    //
    // ON `exec`, LIKE `match` AND `matchAll`, so the three cannot disagree
    // about what matched. `search` ignores `lastIndex` and the `g` flag by
    // specification, so it is reset first and the search always starts at 0.
    text("search", 1, [](context & c, const std::string & self, std::span<value> a) -> value {
        // 22.1.3.21 step 3, the same RegExpCreate `match` does:
        // `"abc".search("b")` is 1 and used to be -1.
        const value pattern = as_regexp(c, arg_at(a, 0), "");
        if (!pattern.is_object()) { return value::number(-1); }
        const value exec = c.lookup_property(pattern, "exec");
        if (!exec.is_callable()) { return value::number(-1); }
        const value subject = c.string(self);
        const value saved = c.lookup_property(pattern, "lastIndex");
        c.store_property(pattern, "lastIndex", value::number(0));
        const value found = c.call(exec, std::span<const value>{&subject, 1}, pattern);
        c.store_property(pattern, "lastIndex", saved);
        if (found.is_nullish() || !found.is_array()) { return value::number(-1); }
        return value::number(context::to_number(c.lookup_property(found, "index")));
    });
    // `matchAll` is exec RUN TO EXHAUSTION, which is exactly what it means -
    // so it is built on exec rather than on a second copy of the regex
    // plumbing, and it cannot disagree with `match` about what matched.
    //
    // It hands back an ARRAY where the spec says an iterator. Both work with
    // `for (const m of ...)` and with a spread, which is all anyone does with
    // one; a real iterator would only differ for a caller that stops early on a
    // pattern expensive enough to notice.
    text("matchAll", 1, [](context & c, const std::string & self, std::span<value> a) -> value {
        value list = c.make_array();
        auto * items = static_cast<array_object *>(list.as_heap());
        if (refuse_non_global(c, arg_at(a, 0), "String.prototype.matchAll")) { return list; }
        // 22.1.3.14 step 3 creates the RegExp with "g", which is what makes
        // `"aaa".matchAll("a")` three matches rather than the first forever.
        const value pattern = as_regexp(c, arg_at(a, 0), "g");
        if (!pattern.is_object()) { return list; }
        const value exec = c.lookup_property(pattern, "exec");
        if (!exec.is_callable()) { return list; }
        const value subject = c.string(self);
        c.store_property(pattern, "lastIndex", value::number(0));
        double previous = -1;
        // Bounded by the subject: each round must advance lastIndex, and a
        // pattern that matches empty - or one without `g`, whose exec always
        // restarts at 0 - would otherwise spin forever.
        for (std::size_t guard = 0; guard <= self.size() + 1; ++guard) {
            const value args[1] = {subject};
            const value found = c.call(exec, args, pattern);
            if (found.is_nullish()) { break; }
            items->items.push_back(found);
            const double now = context::to_number(c.lookup_property(pattern, "lastIndex"));
            if (!(now > previous)) { break; }
            previous = now;
        }
        return list;
    });
    text("replaceAll", 2,
         [replace_with](context & c, const std::string & s, std::span<value> a) -> value {
             if (refuse_non_global(c, arg_at(a, 0), "String.prototype.replaceAll")) {
                 return c.string(s);
             }
             return replace_with(c, s, a, true);
         });
    // ASCII-ONLY, on purpose and for the whole family: core/algorithms.hpp
    // folds A-Z and nothing else so that a rendered page cannot depend on the
    // host's locale or Unicode tables. `"Straße".toUpperCase()` is
    // "STRAßE" here and "STRASSE" in V8, which unittests/js/string_basics
    // asserts rather than leaves to be discovered.
    text("toUpperCase", 0, [](context & c, const std::string & s, std::span<value>) -> value {
        std::string out = s;
        ascii_upper_in_place(out);
        return c.string(out);
    });
    text("toLowerCase", 0, [](context & c, const std::string & s, std::span<value>) -> value {
        std::string out = s;
        ascii_lower_in_place(out);
        return c.string(out);
    });
    text("trim", 0, [](context & c, const std::string & s, std::span<value>) -> value {
        std::size_t from = 0;
        std::size_t to = 0;
        trim_bounds(s, true, true, from, to);
        return c.string(s.substr(from, to - from));
    });
    // 22.1.3.16. THE COUNT IS ToIntegerOrInfinity AND THEN A RANGE CHECK, not
    // a clamp: `"x".repeat(-1)` and `"".repeat(Infinity)` are each a RangeError
    // the clamp turned into "" and into a million-character string. And the
    // clamp could not see the case that mattered - an object count coerces to
    // NaN through the static to_number, `std::clamp` passes NaN straight
    // through, and the cast to size_t is undefined behaviour. See integer_arg.
    text("repeat", 1, [](context & c, const std::string & s, std::span<value> a) -> value {
        const double n = integer_arg(c, a, 0);
        if (n < 0 || std::isinf(n)) {
            c.throw_error("RangeError", "Invalid count value");
            return c.string("");
        }
        if (s.empty() || n == 0) { return c.string(""); }
        if (n * static_cast<double>(s.size()) > max_string_length) {
            c.throw_error("RangeError", "Invalid string length");
            return c.string("");
        }
        const auto count = static_cast<std::size_t>(n);
        std::string out;
        out.reserve(s.size() * count);
        for (std::size_t i = 0; i < count; ++i) { out += s; }
        return c.string(out);
    });
    // StringPad (22.1.3.17.1), both directions in one place because they are
    // one algorithm and were two copies of the same defect.
    //
    // THE ORDER IS THE SPECIFICATION'S and it is observable: ToString(this),
    // then ToIntegerOrInfinity(maxLength), then the length test, and ONLY THEN
    // ToString(fillString) - built-ins/String/prototype/padStart/
    // observable-operations.js asserts exactly that sequence of valueOf and
    // toString calls. Coercing the filler first, as this did, gets the answer
    // right and the log wrong.
    const auto pad = [](context & c, const std::string & self, std::span<value> a, bool at_start) {
        const double want = integer_arg(c, a, 0);
        // ToLength: negative and NaN are 0, so there is nothing to do. This is
        // the line `'abc'.padStart(NaN, 'def')` needed - it used to cast NaN to
        // a size_t and loop appending until the process died.
        if (!(want > static_cast<double>(self.size()))) { return c.string(self); }
        if (want > max_string_length) {
            c.throw_error("RangeError", "Invalid string length");
            return c.string("");
        }
        const std::string filler = has_index(a, 1) ? c.to_string(a[1]) : " ";
        if (filler.empty()) { return c.string(self); }
        const auto fill_length = static_cast<std::size_t>(want) - self.size();
        std::string filled;
        filled.reserve(fill_length);
        while (filled.size() < fill_length) { filled += filler; }
        filled.resize(fill_length);
        return c.string(at_start ? filled + self : self + filled);
    };
    text("padStart", 1, [pad](context & c, const std::string & s, std::span<value> a) -> value {
        return pad(c, s, a, true);
    });
    text("padEnd", 1, [pad](context & c, const std::string & s, std::span<value> a) -> value {
        return pad(c, s, a, false);
    });
    text("trimStart", 0, [](context & c, const std::string & s, std::span<value>) -> value {
        std::size_t from = 0;
        std::size_t to = 0;
        trim_bounds(s, true, false, from, to);
        return c.string(s.substr(from, to - from));
    });
    text("trimEnd", 0, [](context & c, const std::string & s, std::span<value>) -> value {
        std::size_t from = 0;
        std::size_t to = 0;
        trim_bounds(s, false, true, from, to);
        return c.string(s.substr(from, to - from));
    });
    // THE SAME ASCII FOLD as toUpperCase, deliberately. This was `std::toupper`,
    // which reads the GLOBAL C LOCALE: what it does to a byte above 0x7F is
    // implementation- and locale-defined, so `toLocaleUpperCase` and
    // `toUpperCase` could disagree about the same string depending on `LC_ALL` -
    // exactly the host dependence core/algorithms.hpp exists to prevent, and it
    // would have shown up as a golden that renders differently on two machines.
    // A real locale fold needs tables this engine does not carry; saying so is
    // better than a fold that varies.
    text("toLocaleUpperCase", 0, [](context & c, const std::string & s, std::span<value>) -> value {
        std::string out = s;
        ascii_upper_in_place(out);
        return c.string(out);
    });
    text("toLocaleLowerCase", 0, [](context & c, const std::string & s, std::span<value>) -> value {
        std::string out = s;
        ascii_lower_in_place(out);
        return c.string(out);
    });
    text("concat", 1, [](context & c, const std::string & s, std::span<value> a) -> value {
        std::string out = s;
        for (std::size_t i = 0; i < a.size(); ++i) { out += c.to_string(a[i]); }
        return c.string(out);
    });
    cx.set_prototype(context::proto_kind::string, string_proto);
}

// Number.prototype
// `Boolean`, and the two methods a boolean has.
//
// Small, and it closes a hole rather than adding a feature: `true.toString()`
// found nothing, so generic code that converts "any value" by calling toString
// on it failed on exactly one of the primitive types.
// `structuredClone` - a DEEP copy of plain data.
//
// A page uses it to take a snapshot it can then mutate without disturbing the
// original; p5.js clones a colour's coordinate array before scaling it, so
// without this every conversion mutated the colour it was reading.
//
// Data only, which is what the algorithm covers: objects, arrays, typed arrays
// and primitives are copied, and anything with behaviour - a function, a DOM
// node - is not clonable. Cycles are preserved through a seen-list, because a
// structure that points back at itself is exactly what a naive recursive copy
// cannot survive.
// `btoa` and `atob` - base64, which is how bytes travel inside a string.
//
// A data: URL is base64, `canvas.toDataURL()` produces one, and a page that
// hand-rolls a download encodes with btoa. Byte-oriented, which is what these
// two actually are: btoa's argument is a "binary string" of bytes 0-255 and not
// text, and treating it as text is how a page's image comes out corrupted.
void install_base64(context & cx) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    cx.define_native("btoa", [](context & c, std::span<value> a) {
        const std::string in = a.empty() ? std::string{} : c.to_string(a[0]);
        std::string out;
        out.reserve((in.size() + 2) / 3 * 4);
        for (std::size_t i = 0; i < in.size(); i += 3) {
            const unsigned b0 = static_cast<unsigned char>(in[i]);
            const unsigned b1 = i + 1 < in.size() ? static_cast<unsigned char>(in[i + 1]) : 0;
            const unsigned b2 = i + 2 < in.size() ? static_cast<unsigned char>(in[i + 2]) : 0;
            const unsigned triple = (b0 << 16) | (b1 << 8) | b2;
            out += alphabet[(triple >> 18) & 0x3F];
            out += alphabet[(triple >> 12) & 0x3F];
            // The padding is what says how many of the last three bytes were
            // real, so it is not optional.
            out += i + 1 < in.size() ? alphabet[(triple >> 6) & 0x3F] : '=';
            out += i + 2 < in.size() ? alphabet[triple & 0x3F] : '=';
        }
        return c.string(out);
    });
    // THE SAME DECODER A data: URL GOES THROUGH (core/algorithms.hpp). These
    // were one loop retyped twice for a while, which is the shape of bug this
    // tree has already paid for once in its two URL parsers.
    cx.define_native("atob", [](context & c, std::span<value> a) {
        return c.string(base64_decode(a.empty() ? std::string{} : c.to_string(a[0])));
    });
}

void install_structured_clone(context & cx) {
    cx.define_native("structuredClone", [](context & c, std::span<value> a) {
        std::vector<std::pair<heap_object *, value>> seen;
        const auto copy = [&](auto && self, value v) -> value {
            if (!v.is_heap()) { return v; }
            for (const auto & [from, to] : seen) {
                if (from == v.as_heap()) { return to; }
            }
            if (v.is_array()) {
                auto * source = static_cast<array_object *>(v.as_heap());
                value made = c.make_array();
                auto * out = static_cast<array_object *>(made.as_heap());
                out->elements = source->elements;
                seen.emplace_back(v.as_heap(), made);
                out->items.reserve(source->items.size());
                for (const value & item : source->items) { out->items.push_back(self(self, item)); }
                return made;
            }
            if (v.is_object()) {
                auto * source = static_cast<object_object *>(v.as_heap());
                value made = c.make_object();
                auto * out = static_cast<object_object *>(made.as_heap());
                seen.emplace_back(v.as_heap(), made);
                for (const auto & [key, item] : source->props) { out->set(key, self(self, item)); }
                return made;
            }
            // A string is immutable, so sharing it IS a copy. Everything else -
            // a function, a symbol - is not clonable, and a browser throws
            // DataCloneError rather than quietly handing back the original.
            if (v.is_string()) { return v; }
            c.throw_error("DataCloneError", std::string{"structuredClone cannot copy a "} +
                                                std::string{context::type_of(v)});
            return value::undefined();
        };
        return copy(copy, a.empty() ? value::undefined() : a[0]);
    });
}
// --- regular expressions ---------------------------------------------------
//
// A RegExp is an ordinary object carrying its source and flags; the compiled
// program lives in a cache beside it, keyed by `source\0flags`, so the same
// literal in a loop compiles once. The cache is shared by every native this
// function installs and dies with them.
//
// `lastIndex` is a plain property, which is what `g` and `y` need and what
// pages read and write directly.
namespace {

using regex_cache = std::map<std::string, std::shared_ptr<rx::rx_prog>>;

[[nodiscard]] std::shared_ptr<rx::rx_prog> compiled(const std::shared_ptr<regex_cache> & cache,
                                                    const std::string & source,
                                                    const std::string & flags) {
    const std::string key = source + '\0' + flags;
    const auto it = cache->find(key);
    if (it != cache->end()) { return it->second; }
    auto program = std::make_shared<rx::rx_prog>(rx::rx_compile(source, flags));
    cache->emplace(key, program);
    return program;
}

// The regex a native was called on, or that was passed to a string method.
[[nodiscard]] bool regex_parts(context & cx, value v, std::string & source, std::string & flags) {
    if (!v.is_object()) { return false; }
    auto * o = static_cast<object_object *>(v.as_heap());
    value * src = o->find("source");
    if (src == nullptr) { return false; }
    source = cx.to_string(*src);
    value * fl = o->find("flags");
    flags = fl == nullptr ? std::string{} : cx.to_string(*fl);
    return true;
}

// The array `exec` and a non-global `match` both return: the whole match, then
// each group, with `index`, `input` and `groups` alongside. ctjs's wrapper
// dropped the positions; they were always in the match.
[[nodiscard]] value exec_result(context & cx, const rx::rx_prog & program, const std::string & s,
                                const rx::rx_match & m) {
    value out = cx.make_array();
    auto * arr = static_cast<array_object *>(out.as_heap());
    arr->items.push_back(cx.string(s.substr(m.begin, m.end - m.begin)));
    for (const auto & [from, to] : m.caps) {
        arr->items.push_back(from < 0 ? value::undefined()
                                      : cx.string(s.substr(static_cast<std::size_t>(from),
                                                           static_cast<std::size_t>(to - from))));
    }
    if (!program.names.empty()) {
        value groups = cx.make_object();
        auto * g = static_cast<object_object *>(groups.as_heap());
        for (const auto & [name, slot] : program.names) {
            const auto at = static_cast<std::size_t>(slot);
            const auto & cap = m.caps[at];
            g->set(name,
                   cap.first < 0
                       ? value::undefined()
                       : cx.string(s.substr(static_cast<std::size_t>(cap.first),
                                            static_cast<std::size_t>(cap.second - cap.first))));
        }
        arr->groups = groups;
    }
    arr->is_match = true;
    arr->index = value::number(static_cast<double>(m.begin));
    arr->input = cx.string(s);
    return out;
}

} // namespace

void install_regexp(context & cx) {
    using detail::method;
    using detail::new_table;
    auto cache = std::make_shared<regex_cache>();

    object_object * regexp_proto = new_table(cx);
    cx.set_prototype(context::proto_kind::regexp, regexp_proto);

    method(cx, regexp_proto, "test", 1, [cache](context & c, std::span<value> a) {
        std::string source;
        std::string flags;
        if (!regex_parts(c, c.current_this(), source, flags)) { return value::boolean(false); }
        const std::shared_ptr<rx::rx_prog> program = compiled(cache, source, flags);
        if (!program->ok) { return value::boolean(false); }
        const std::string subject = a.empty() ? std::string{} : c.to_string(a[0]);
        rx::rx_match m;
        return value::boolean(rx::rx_search(*program, subject, 0, m));
    });

    method(cx, regexp_proto, "exec", 1, [cache](context & c, std::span<value> a) {
        std::string source;
        std::string flags;
        if (!regex_parts(c, c.current_this(), source, flags)) { return value::null(); }
        const std::shared_ptr<rx::rx_prog> program = compiled(cache, source, flags);
        if (!program->ok) { return value::null(); }
        const std::string subject = a.empty() ? std::string{} : c.to_string(a[0]);
        auto * self = static_cast<object_object *>(c.current_this().as_heap());
        // `g` and `y` resume from lastIndex and write it back; without either,
        // exec always starts at 0.
        const bool stateful = program->global || program->sticky;
        std::size_t from = 0;
        if (stateful) {
            if (value * li = self->find("lastIndex")) {
                from = static_cast<std::size_t>(std::max(0.0, context::to_number(*li)));
            }
        }
        rx::rx_match m;
        if (from > subject.size() || !rx::rx_search(*program, subject, from, m) ||
            (program->sticky && m.begin != from)) {
            if (stateful) { self->set("lastIndex", value::number(0)); }
            return value::null();
        }
        if (stateful) { self->set("lastIndex", value::number(static_cast<double>(m.end))); }
        return exec_result(c, *program, subject, m);
    });

    method(cx, regexp_proto, "toString", 0, [](context & c, std::span<value>) {
        std::string source;
        std::string flags;
        if (!regex_parts(c, c.current_this(), source, flags)) { return c.string("/(?:)/"); }
        return c.string("/" + source + "/" + flags);
    });

    // The constructor, and what a regex LITERAL compiles to. Installed under
    // both names: `RegExp` for pages, and a reserved one the compiler emits, so
    // a page that shadows RegExp cannot change what its own literals mean.
    const auto make = [cache](context & c, std::span<value> a) {
        const std::string source = a.empty() ? std::string{} : c.to_string(a[0]);
        const std::string flags = a.size() > 1 ? c.to_string(a[1]) : std::string{};
        value out = c.make_object();
        auto * o = static_cast<object_object *>(out.as_heap());
        const std::shared_ptr<rx::rx_prog> program = compiled(cache, source, flags);
        // NONE OF THESE IS ENUMERABLE. The specification puts source, flags
        // and the four mode flags on RegExp.prototype as ACCESSORS and gives
        // the instance only `lastIndex`, { writable: true, enumerable: false,
        // configurable: false } (22.2.6). They are own data properties here,
        // which is a stated shortcut - but enumerable own data properties made
        // `Object.keys(/x/)` report six and `Object.defineProperties(obj, /x/)`
        // throw on the first one it was handed as a descriptor.
        o->define("source", c.string(source), attr_builtin);
        o->define("flags", c.string(flags), attr_builtin);
        o->define("global", value::boolean(program->global), attr_builtin);
        o->define("ignoreCase", value::boolean(program->icase), attr_builtin);
        o->define("multiline", value::boolean(program->multi), attr_builtin);
        o->define("sticky", value::boolean(program->sticky), attr_builtin);
        o->define("lastIndex", value::number(0), attr_writable);
        o->define("__regex", value::boolean(true), attr_builtin);
        if (object_object * table = c.prototype(context::proto_kind::regexp)) {
            o->prototype = value::object(table);
        }
        return out;
    };
    cx.define_native("RegExp", make);
    cx.define_native(std::string{regexp_factory_name}, make);
}

// `Symbol`. p5.js needs it to EXIST before anything else - the bundled zod
// calls `Symbol(...)` at load, and that one undefined global stopped the whole
// bundle with nothing but "attempted to call a non-function" to say so.
//
// A symbol's identity is a STRING KEY no source literal collides with, so a
// symbol-keyed property works through the existing string-keyed machinery
// without touching the object model. The well-known ones get fixed keys, which
// is what lets `Symbol.iterator` mean the same thing to two different pieces of
// code that never met.
void install_symbol(context & cx) {
    using detail::method;
    using detail::new_table;
    auto counter = std::make_shared<std::uint64_t>(0);

    object_object * symbol_proto = new_table(cx);
    method(cx, symbol_proto, "toString", 0, [](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!self.is_kind(heap_kind::symbol)) { return c.string("Symbol()"); }
        return c.string("Symbol(" + static_cast<symbol_object *>(self.as_heap())->description +
                        ")");
    });
    cx.set_prototype(context::proto_kind::symbol, symbol_proto);

    // Callable AND a namespace: `Symbol('x')` and `Symbol.iterator` are both
    // ordinary uses, which is why a native carries a property table now.
    auto * symbol =
        cx.allocate<native_object>("Symbol", [counter](context & c, std::span<value> a) {
            const std::string description = a.empty() ? std::string{} : c.to_string(a[0]);
            const std::string key =
                std::string{symbol_key_prefix} + std::to_string((*counter)++) + ":" + description;
            return value::object(c.allocate<symbol_object>(description, key));
        });
    // { false, false, false } - a well-known symbol is not writable and not
    // configurable (20.4.2), and enumerable would put `iterator` in
    // `Object.keys(Symbol)`.
    const auto well_known = [&](const char * name, const char * key) {
        symbol->define(name, value::object(cx.allocate<symbol_object>(name, key)), attr_none);
    };
    well_known("iterator", "@@iterator");
    well_known("asyncIterator", "@@asyncIterator");
    well_known("hasInstance", "@@hasInstance");
    well_known("toPrimitive", "@@toPrimitive");
    well_known("toStringTag", "@@toStringTag");
    // `Symbol.match` IS CONSULTED, which is why it is here and its four
    // siblings are not. IsRegExp (7.2.8) reads it to decide whether
    // `includes`, `startsWith` and `endsWith` must refuse their argument, so
    // defining it gives a page the documented way to say "this object is a
    // pattern" or "this RegExp is not one" - and test262's
    // `return-abrupt-from-searchstring-regexp-test.js` asserts exactly that.
    //
    // @@replace, @@search, @@split and @@matchAll are DELIBERATELY ABSENT.
    // Nothing here dispatches through them - the string methods drive a
    // pattern's `exec` directly - and a well-known symbol that no operation
    // reads is a promise the engine does not keep: a page would install a
    // custom @@replace, see it ignored, and have nothing to say why.
    well_known("match", "@@match");
    // A REGISTRY, and it has to hold the SYMBOLS rather than mint a fresh one
    // per call. Two `Symbol.for('x')` produced two objects with the same key,
    // and `===` compares identity - so the one guarantee the registry exists to
    // give, that a key looked up twice is the same symbol, was the one it did
    // not keep. The shared_ptr is captured by both `for` and `keyFor`, which is
    // what lets the second answer questions about the first.
    //
    // AND THE SYMBOLS THEMSELVES HAVE TO LIVE IN A HEAP OBJECT, not in the
    // capture. They were `value`s inside the shared_ptr, and a `value` in a C++
    // capture is invisible to the precise collector: the KEYS survived a
    // collection - they are std::strings, which nothing collects - and every
    // symbol they named was freed while the registry went on listing it.
    // `Symbol.for('KEY')` then handed back a pointer into a freed cell, and
    // once another symbol was allocated over it, `Symbol.for('KEY').description`
    // came back as that other symbol's description. Measured, not reasoned:
    // 'recycled'.
    //
    // So the keys stay in C++, where they are safe by construction, and the
    // symbols move into an ARRAY, which mark_object traces like any other -
    // which means the registry roots itself AS IT GROWS, where a snapshot into
    // native_object::retained would only root what was registered by the time
    // the native was built. Both natives retain the one array handle, so
    // neither can be left reading the other's freed entries.
    auto keys = std::make_shared<std::vector<std::string>>();
    const value registry = cx.make_array();
    auto * symbol_for =
        cx.allocate<native_object>("for", [keys, registry](context & c, std::span<value> a) {
            const std::string d = a.empty() ? std::string{} : c.to_string(a[0]);
            auto * held = static_cast<array_object *>(registry.as_heap());
            for (std::size_t i = 0; i < keys->size() && i < held->items.size(); ++i) {
                if ((*keys)[i] == d) { return held->items[i]; }
            }
            const value made = value::object(c.allocate<symbol_object>(d, "@@for:" + d));
            keys->push_back(d);
            held->items.push_back(made);
            return made;
        });
    symbol_for->retained.push_back(registry);
    symbol->define("for", value::object(symbol_for), attr_builtin);
    // The inverse: the key a registered symbol was made under, or undefined for
    // one that never went through the registry.
    auto * symbol_key_for =
        cx.allocate<native_object>("keyFor", [keys, registry](context & c, std::span<value> a) {
            const value want = arg_at(a, 0);
            auto * held = static_cast<array_object *>(registry.as_heap());
            for (std::size_t i = 0; i < keys->size() && i < held->items.size(); ++i) {
                if (held->items[i] == want) { return c.string((*keys)[i]); }
            }
            return value::undefined();
        });
    symbol_key_for->retained.push_back(registry);
    symbol->define("keyFor", value::object(symbol_key_for), attr_builtin);
    // `Symbol.prototype` is reachable from the constructor, like every other
    // built-in's - a page that walks it found undefined.
    detail::constant(symbol, "prototype", value::object(symbol_proto));
    cx.define_global("Symbol", value::object(symbol));

    // --- BigInt --------------------------------------------------------------
    // A CONVERSION, like Number and String and unlike Array: `new BigInt(1)` is
    // a TypeError in the specification because there is no wrapper object to
    // make. This engine does not box at all, so calling it is the only form.
    object_object * bigint_proto = new_table(cx);
    method(cx, bigint_proto, "toString", 0, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!self.is_kind(heap_kind::bigint)) { return c.string("0"); }
        const int radix = a.empty() || a[0].is_undefined()
                              ? 10
                              : std::clamp(static_cast<int>(context::to_number(a[0])), 2, 36);
        return c.string(
            bigint_to_string(static_cast<bigint_object *>(self.as_heap())->digits, radix));
    });
    method(cx, bigint_proto, "valueOf", 0,
           [](context & c, std::span<value>) { return c.current_this(); });
    cx.set_prototype(context::proto_kind::bigint, bigint_proto);

    auto * bigint_ctor = cx.allocate<native_object>("BigInt", [](context & c, std::span<value> a) {
        const value v = arg_at(a, 0);
        if (v.is_kind(heap_kind::bigint)) { return v; }
        if (v.is_boolean()) {
            return value::object(c.allocate<bigint_object>(bigint{v.as_boolean() ? 1 : 0}));
        }
        if (v.is_string()) {
            // A STRING THAT IS NOT AN INTEGER IS A SyntaxError, not NaN -
            // there is no BigInt NaN to return, so the conversion has to
            // refuse rather than degrade.
            const std::optional<bigint> parsed =
                bigint_from_string(static_cast<string_object *>(v.as_heap())->text);
            if (!parsed) {
                c.throw_error("SyntaxError", "Cannot convert this string to a BigInt");
                return value::undefined();
            }
            return value::object(c.allocate<bigint_object>(*parsed));
        }
        // A NON-INTEGRAL Number is a RangeError - `BigInt(1.5)` refuses
        // rather than truncating, because losing the fraction silently is
        // the failure this type exists to make impossible.
        const std::optional<bigint> parsed = bigint_from_double(context::to_number(v));
        if (!parsed) {
            c.throw_error("RangeError", "Cannot convert a non-integer to a BigInt");
            return value::undefined();
        }
        return value::object(c.allocate<bigint_object>(*parsed));
    });
    detail::constant(bigint_ctor, "prototype", value::object(bigint_proto));
    link_constructor(cx, bigint_proto, "BigInt", 1, value::object(bigint_ctor));
    cx.define_global("BigInt", value::object(bigint_ctor));
}

} // namespace ctbrowser::script::builtins_detail
