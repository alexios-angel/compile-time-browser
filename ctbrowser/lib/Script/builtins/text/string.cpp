// ctbrowser.script builtins - String.prototype, with what every one of its
// methods begins with, TrimString's whitespace set, and RegExpCreate.
//
// One of four files carved out of a 1,333-line builtins/text.cpp on 2026-09-08
// - which was itself one of five carved out of builtins.cpp on 2026-08-09.
// Everything shared - the argument helpers, namespace detail, and these
// functions' declarations - is in ../internal.hpp; what String reaches into
// RegExp for is in internal.hpp beside this.
//
// EVERY INDEX AND LENGTH HERE IS IN UTF-16 CODE UNITS, as 22.1.3 writes them,
// over text stored as WTF-8 (script/utf16.hpp). A method is handed the
// receiver's string_object, which measures itself once: an ASCII string's
// unit index IS its byte index, and any other string answers through the
// marks it keeps (value/base.hpp).

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
//
// A PRIMITIVE RECEIVER IS HANDED OVER AS ITSELF - the frame holds it, so it
// outlives the call - which is no copy per call, and a string that has
// measured its units stays measured across a loop of `s.charCodeAt(i)`.
// Anything else is ToString'd into a string_object on the stack, which the
// collector never sees.
using text_body = std::function<value(context &, const string_object &, std::span<value>)>;

[[nodiscard]] value with_this_text(context & c, const text_body & body, std::span<value> a) {
    const value self = c.current_this();
    if (self.is_string()) {
        return body(c, *static_cast<const string_object *>(self.as_heap()), a);
    }
    const context::stack_mark before = c.mark_stack();
    const string_object text{detail::this_string(c)};
    if (detail::threw_since(c, before)) { return value::undefined(); }
    return body(c, text, a);
}

[[nodiscard]] native_fn on_text(std::string owner, text_body body) {
    return [owner = std::move(owner), body = std::move(body)](context & c,
                                                              std::span<value> a) -> value {
        if (c.current_this().is_nullish()) {
            c.throw_error("TypeError", owner + " called on null or undefined");
            return value::undefined();
        }
        return with_this_text(c, body, a);
    };
}

// thisStringValue, 22.1.3.35 - `toString` and `valueOf` are NOT generic, which
// is the one place a String method wants its receiver rather than its
// receiver's text. `String.prototype.toString.call(1)` is a TypeError, not
// "1", and it has to be: `''.concat({toString: String.prototype.toString})`
// would otherwise recurse.
//
// A String WRAPPER (`Object("ab")`, see detail::wrap_primitive) carries its
// [[StringData]] in the primitive slot; String.prototype itself has the empty
// String for one by 22.1.3. That is exactly the reasoning
// `detail::this_number_value` uses to make `Number.prototype.toString()`
// answer "0", and the two agree.
[[nodiscard]] value this_string_value(context & cx, const char * method) {
    const value self = cx.current_this();
    if (self.is_string()) { return self; }
    if (value * slot = primitive_slot(self); slot != nullptr && slot->is_string()) { return *slot; }
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
// nothing, and answered false. The operation itself is regexp.cpp's; this is
// the yes/no form for a caller that has no separate throw path.
[[nodiscard]] bool is_regexp_value(context & cx, value v) {
    bool out = false;
    return builtins_detail::is_regexp(cx, v, out) && out;
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

// 22.1.3.14 step 2.b and 22.1.3.20 step 2.b: `matchAll` and `replaceAll`
// REFUSE a RegExp without `g`, because both mean "every match" and a
// non-global pattern cannot deliver one. The flags are read through [[Get]]
// (a getter may throw), RequireObjectCoercible'd and ToString'd, in that
// order. TRUE means a throw is in flight.
[[nodiscard]] bool refuse_non_global(context & cx, value pattern, const char * method) {
    bool regexp = false;
    if (!builtins_detail::is_regexp(cx, pattern, regexp)) { return true; }
    if (!regexp) { return false; }
    const context::stack_mark before = cx.mark_stack();
    const value flags = cx.lookup_property(pattern, "flags");
    if (detail::threw_since(cx, before)) { return true; }
    if (flags.is_nullish()) {
        cx.throw_error("TypeError", std::string{method} + " requires flags on its pattern");
        return true;
    }
    const std::string text = string_arg(cx, flags);
    if (detail::threw_since(cx, before)) { return true; }
    if (text.find('g') != std::string::npos) { return false; }
    cx.throw_error("TypeError", std::string{method} + " must be called with a global RegExp");
    return true;
}

// STEP 2 OF match, matchAll, replace, replaceAll, search AND split: the
// argument is asked for its own @@match / @@replace / @@search / @@split /
// @@matchAll (GetMethod, 7.3.10 - a getter that throws propagates, a value
// that is neither callable nor nullish is a TypeError), and when it has one
// the answer is that method's, called on the argument with the ORIGINAL
// receiver first. A real RegExp carries all five on RegExp.prototype
// (regexp.cpp), so this is how a pattern reaches them.
//
// TRUE means `out` is the answer (or a throw is in flight) and the caller
// returns it at once.
[[nodiscard]] bool symbol_dispatch(context & cx, value target, const char * symbol,
                                   std::span<value> a, value & out) {
    out = value::undefined();
    // AN OBJECT ONLY: step 2 is "if regexp is an Object", so a primitive's
    // prototype is never asked - a getter on Number.prototype[@@match] does
    // not run for `"a1b".match(1)` (the cstm-*-on-*-primitive files), now
    // that a primitive's [[Get]] does reach its prototype's accessors.
    if (!target.is_object_like()) { return false; }
    const value method = cx.lookup_property(target, symbol);
    if (cx.throw_pending()) { return true; }
    if (method.is_nullish()) { return false; }
    if (!method.is_callable()) {
        cx.throw_error("TypeError", std::string{symbol} + " is not a function");
        return true;
    }
    std::vector<value> args;
    args.push_back(cx.current_this());
    for (std::size_t i = 1; i < a.size(); ++i) { args.push_back(a[i]); }
    out = cx.call(method, args, target);
    return true;
}

// The tail of match, matchAll and search: RegExpCreate over the argument and
// Invoke(rx, @@method, [S]) (22.1.3.13 steps 3-5 and its siblings).
[[nodiscard]] value create_and_invoke(context & cx, value pattern, const char * flags,
                                      const char * symbol, const std::string & self) {
    const value rx =
        regexp_create(cx, pattern, flags == nullptr ? value::undefined() : cx.string(flags));
    if (rx.is_undefined()) { return value::undefined(); }
    const context::rooted keep{cx, rx};
    const value method = cx.lookup_property(rx, symbol);
    if (cx.throw_pending()) { return value::undefined(); }
    if (!method.is_callable()) {
        cx.throw_error("TypeError", std::string{symbol} + " is not a function");
        return value::undefined();
    }
    const value subject = cx.string(self);
    return cx.call(method, std::span<const value>{&subject, 1}, rx);
}

// StringIndexOf, 6.1.4.1, in UNITS: the first index at or after `from` where
// `needle` occurs, or npos. An empty search string is found at any position
// up to and including the length, which is what makes
// `"abc".replaceAll("", "_")` "_a_b_c_".
//
// A BYTE SEARCH STANDS IN FOR THE UNIT SEARCH whenever the needle holds no
// lone surrogate: UTF-8 is self-synchronising, so a match of well-formed text
// begins and ends on code point boundaries, and those are unit boundaries -
// the one byte-to-unit conversion is at the hit. A needle WITH a lone
// surrogate is the case no byte of the subject spells - `"\uD83D"` is half of
// the four bytes of U+1F600 - and is searched as UTF-16.
[[nodiscard]] std::size_t index_of(const string_object & s, std::string_view needle,
                                   std::size_t from) {
    if (from > s.length()) { return std::string::npos; }
    if (needle.empty()) { return from; }
    if (s.bytewise()) { return s.text.find(needle, from); }
    // ponytail: a needle holding a lone surrogate converts the whole subject
    // on every call, so split/replace by one is O(n * matches); keep a UTF-16
    // copy on string_object if a page ever does that to a large text.
    if (utf16::has_lone_surrogate(needle)) {
        return wtf8_to_utf16(s.text).find(wtf8_to_utf16(needle), from);
    }
    const utf16::position at = s.locate(from);
    std::size_t byte = at.byte;
    // A well-formed needle cannot begin at a pair's low half: the next code
    // point is the first place it can.
    if (at.low_half) { (void)decode_utf8(s.text, byte); }
    const std::size_t found = s.text.find(needle, byte);
    return found == std::string::npos ? found : s.unit_index(found);
}

// The last index at or before `from` (at most the length) where `needle`
// occurs - lastIndexOf's loop, 22.1.3.11 steps 9-11.
[[nodiscard]] std::size_t last_index_of(const string_object & s, std::string_view needle,
                                        std::size_t from) {
    if (s.bytewise()) { return s.text.rfind(needle, from); }
    if (needle.empty()) { return from; }
    if (utf16::has_lone_surrogate(needle)) {
        return wtf8_to_utf16(s.text).rfind(wtf8_to_utf16(needle), from);
    }
    // At a low half the latest a well-formed needle may start is its pair's
    // first unit - which is exactly the byte locate answers.
    const std::size_t found = s.text.rfind(needle, s.locate(from).byte);
    return found == std::string::npos ? found : s.unit_index(found);
}

// Does `needle` occur at unit `at` exactly? startsWith and endsWith.
[[nodiscard]] bool occurs_at(const string_object & s, std::string_view needle, std::size_t at) {
    if (needle.empty()) { return at <= s.length(); }
    if (s.bytewise()) {
        return at <= s.text.size() && s.text.compare(at, needle.size(), needle) == 0;
    }
    if (utf16::has_lone_surrogate(needle)) {
        const std::u16string hay = wtf8_to_utf16(s.text);
        const std::u16string pin = wtf8_to_utf16(needle);
        return at <= hay.size() && hay.compare(at, pin.size(), pin) == 0;
    }
    const utf16::position p = s.locate(at);
    return !p.low_half && s.text.compare(p.byte, needle.size(), needle) == 0;
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
    // THE SIX THAT ASK THEIR ARGUMENT FIRST - match, matchAll, replace,
    // replaceAll, search, split (22.1.3.13-14, .19-20, .22, .24): step 1 is
    // RequireObjectCoercible(this) only; the argument's own @@method (and, for
    // matchAll/replaceAll, the global-flag refusal) comes next, and ToString of
    // the receiver is AFTER that - so a receiver whose toString throws never
    // runs it when the argument answers, and a poisoned receiver beside a
    // non-global RegExp sees the TypeError, not its own throw.
    const auto text_after_dispatch = [&cx, string_proto](const char * name, double arity,
                                                         const char * symbol, bool needs_global,
                                                         text_body body) {
        const std::string owner = std::string{"String.prototype."} + name;
        method(cx, string_proto, name, arity,
               [owner, symbol, needs_global, body = std::move(body)](context & c,
                                                                     std::span<value> a) -> value {
                   const context::stack_mark before = c.mark_stack();
                   if (c.current_this().is_nullish()) {
                       c.throw_error("TypeError", owner + " called on null or undefined");
                       return value::undefined();
                   }
                   if (needs_global && refuse_non_global(c, arg_at(a, 0), owner.c_str())) {
                       return value::undefined();
                   }
                   if (value dispatched; symbol_dispatch(c, arg_at(a, 0), symbol, a, dispatched)) {
                       return dispatched;
                   }
                   if (detail::threw_since(c, before)) { return value::undefined(); }
                   return with_this_text(c, body, a);
               });
    };
    text("charAt", 1, [](context & c, const string_object & s, std::span<value> a) -> value {
        // A NEGATIVE POSITION IS OUT OF RANGE, not clamped to zero - that is
        // `at`'s job, not `charAt`'s. Clamping made `"abc".charAt(-1)` answer
        // "a" where the specification says "".
        const double i = integer_arg(c, a, 0);
        if (!(i >= 0 && i < static_cast<double>(s.length()))) { return c.string(std::string{}); }
        return c.string(s.unit_text(static_cast<std::size_t>(i)));
    });
    // `at` is charAt that counts from the END for a negative index, which is
    // the whole reason to reach for it - `s.at(-1)` is the last character.
    // Arrays had it and strings did not, and the two are meant to match.
    text("at", 1, [](context & c, const string_object & s, std::span<value> a) -> value {
        // integer_arg, not num_at: a NaN index is 0 here, and casting it to
        // size_t instead is what hung the engine on `s.at(undefined)`.
        double i = integer_arg(c, a, 0);
        const auto length = static_cast<double>(s.length());
        if (i < 0) { i += length; }
        // Written so a NaN could not survive it even if one arrived: the guard
        // is now a range CHECK rather than two comparisons that are both false
        // for NaN and fall through to an out-of-bounds read.
        if (!(i >= 0 && i < length)) { return value::undefined(); }
        return c.string(s.unit_text(static_cast<std::size_t>(i)));
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
    // 22.1.3.36 String.prototype[@@iterator]: a String Iterator over the
    // receiver's CODE POINTS (22.1.5.1) - a surrogate pair is one element and
    // a lone surrogate one of its own - which is what `for (c of s)` and
    // `[...s]` do too (context::iterable_values), so the three agree. `length`
    // is 0 and the name is "[Symbol.iterator]" (20.2.4.1).
    {
        auto * iterator_fn = cx.allocate<native_object>(
            "[Symbol.iterator]",
            on_text("String.prototype[Symbol.iterator]",
                    [](context & c, const string_object & s, std::span<value>) -> value {
                        const value list = c.iterable_values(c.string(s.text));
                        return detail::list_iterator(c, list, "String Iterator");
                    }));
        detail::install_arity(cx, iterator_fn, 0);
        iterator_fn->is_constructor = false;
        string_proto->define(CTBROWSER_SYMBOL_KEY("iterator"), value::object(iterator_fn),
                             attr_builtin);
    }
    method(cx, string_proto, "valueOf", 0, [](context & c, std::span<value>) {
        return this_string_value(c, "String.prototype.valueOf");
    });
    text("codePointAt", 1, [](context & c, const string_object & s, std::span<value> a) -> value {
        // Out of range - including NEGATIVE, which used to clamp to zero and
        // answer with the first character - is undefined.
        const double raw = integer_arg(c, a, 0);
        const std::size_t length = s.length();
        if (!(raw >= 0 && raw < static_cast<double>(length))) { return value::undefined(); }
        // CodePointAt, 11.1.4: a lead surrogate followed by a trail is the
        // pair's code point; anything else is the unit itself, a lone half
        // included.
        const auto i = static_cast<std::size_t>(raw);
        const char16_t first = s.unit(i);
        if (first >= 0xD800 && first <= 0xDBFF && i + 1 < length) {
            const char16_t second = s.unit(i + 1);
            if (second >= 0xDC00 && second <= 0xDFFF) {
                return value::number(
                    static_cast<double>(0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00)));
            }
        }
        return value::number(static_cast<double>(first));
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
    // 22.1.3.10 isWellFormed and 22.1.3.33 toWellFormed. A JS string here is
    // UTF-8 bytes, and a LONE SURROGATE - `"\uD83D"` on its own - is stored
    // as the three-byte WTF-8 form (ED A0..BF xx) the literal decoder emits.
    // That form is exactly what these two look for: an unpaired one is
    // ill-formed, and a lead immediately followed by a trail is the pair the
    // concatenation `lead + trail` made, which toWellFormed joins into the one
    // four-byte code point it should always have been.
    const auto wtf8_surrogate = [](const std::string & s, std::size_t at) -> std::uint32_t {
        if (at + 3 > s.size() || static_cast<unsigned char>(s[at]) != 0xEDu) { return 0; }
        const auto b1 = static_cast<unsigned char>(s[at + 1]);
        const auto b2 = static_cast<unsigned char>(s[at + 2]);
        if ((b1 & 0xE0u) != 0xA0u || (b2 & 0xC0u) != 0x80u) { return 0; }
        return 0xD000u | (static_cast<std::uint32_t>(b1 & 0x3Fu) << 6) | (b2 & 0x3Fu);
    };
    text("isWellFormed", 0,
         [wtf8_surrogate](context &, const string_object & str, std::span<value>) -> value {
             const std::string & s = str.text;
             for (std::size_t i = 0; i < s.size(); ++i) {
                 const std::uint32_t cu = wtf8_surrogate(s, i);
                 if (cu == 0) { continue; }
                 const std::uint32_t next = cu <= 0xDBFFu ? wtf8_surrogate(s, i + 3) : 0;
                 if (cu > 0xDBFFu || next < 0xDC00u) { return value::boolean(false); }
                 i += 5;
             }
             return value::boolean(true);
         });
    text("toWellFormed", 0,
         [wtf8_surrogate](context & c, const string_object & str, std::span<value>) -> value {
             const std::string & s = str.text;
             std::string out;
             for (std::size_t i = 0; i < s.size(); ++i) {
                 const std::uint32_t cu = wtf8_surrogate(s, i);
                 if (cu == 0) {
                     out += s[i];
                     continue;
                 }
                 const std::uint32_t next = cu <= 0xDBFFu ? wtf8_surrogate(s, i + 3) : 0;
                 if (cu > 0xDBFFu || next < 0xDC00u) {
                     out += "\xEF\xBF\xBD"; // U+FFFD
                     i += 2;
                     continue;
                 }
                 const std::uint32_t cp = 0x10000u + ((cu - 0xD800u) << 10) + (next - 0xDC00u);
                 append_utf8(out, static_cast<char32_t>(cp));
                 i += 5;
             }
             return c.string(out);
         });
    text("normalize", 0, [](context & c, const string_object & s, std::span<value> a) -> value {
        const context::stack_mark before = c.mark_stack();
        const std::string form = has_index(a, 0) ? string_arg(c, a[0]) : std::string{"NFC"};
        if (detail::threw_since(c, before)) { return value::undefined(); }
        if (form != "NFC" && form != "NFD" && form != "NFKC" && form != "NFKD") {
            c.throw_error("RangeError",
                          "The normalization form should be one of NFC, NFD, NFKC, NFKD");
            return c.string(std::string{});
        }
        return c.string(s.text);
    });
    text("localeCompare", 1,
         [](context & c, const string_object & receiver, std::span<value> a) -> value {
             const std::string & self = receiver.text;
             // Byte order, which is the locale this engine has. The argument is
             // ToString'd through `arg_at` and not `str_at`: a MISSING one is
             // `undefined`, and ToString(undefined) is "undefined" - so
             // `"undefined".localeCompare()` is 0, not 1 against the empty string.
             const std::string other = string_arg(c, arg_at(a, 0));
             return value::number(self < other ? -1 : (self == other ? 0 : 1));
         });
    text("charCodeAt", 1, [](context & c, const string_object & s, std::span<value> a) -> value {
        // Out of range - including NEGATIVE - is NaN, not the first character.
        const context::stack_mark before = c.mark_stack();
        const double i = integer_arg(c, a, 0);
        if (detail::threw_since(c, before)) { return value::undefined(); }
        if (!(i >= 0 && i < static_cast<double>(s.length()))) {
            return value::number(std::nan(""));
        }
        // A CODE UNIT, as `length` and charAt and every other index here count
        // them - they came together, because one of them alone made
        // `s.charAt(i)` and `s.charCodeAt(i)` describe different characters.
        return value::number(static_cast<double>(s.unit(static_cast<std::size_t>(i))));
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
    text("indexOf", 1, [](context & c, const string_object & s, std::span<value> a) -> value {
        const context::stack_mark before = c.mark_stack();
        const std::string needle = string_arg(c, arg_at(a, 0));
        if (detail::threw_since(c, before)) { return value::undefined(); }
        const auto from = static_cast<std::size_t>(
            std::clamp(integer_arg(c, a, 1), 0.0, static_cast<double>(s.length())));
        const std::size_t found = index_of(s, needle, from);
        return value::number(found == std::string::npos ? -1 : static_cast<double>(found));
    });
    text("lastIndexOf", 1, [](context & c, const string_object & s, std::span<value> a) -> value {
        const context::stack_mark before = c.mark_stack();
        const std::string needle = string_arg(c, arg_at(a, 0));
        if (detail::threw_since(c, before)) { return value::undefined(); }
        // The position is the LAST index the match may START at, and it
        // defaults to the end. NaN means the end too - and an absent argument
        // IS NaN, because ToNumber(undefined) is NaN, so the two cases are one
        // line rather than a special case (22.1.3.10 steps 4 and 5).
        const double raw = c.to_number_value(arg_at(a, 1));
        const double at = std::isnan(raw) ? std::numeric_limits<double>::infinity() : raw;
        const auto last = static_cast<std::size_t>(
            std::clamp(std::trunc(at), 0.0, static_cast<double>(s.length())));
        const std::size_t found = last_index_of(s, needle, last);
        return value::number(found == std::string::npos ? -1 : static_cast<double>(found));
    });
    text("includes", 1, [](context & c, const string_object & s, std::span<value> a) -> value {
        const context::stack_mark before = c.mark_stack();
        if (is_regexp_value(c, arg_at(a, 0))) {
            c.throw_error("TypeError",
                          "First argument to String.prototype.includes must not be a regular "
                          "expression");
            return value::boolean(false);
        }
        if (detail::threw_since(c, before)) { return value::undefined(); }
        const std::string needle = string_arg(c, arg_at(a, 0));
        if (detail::threw_since(c, before)) { return value::undefined(); }
        const auto from = static_cast<std::size_t>(
            std::clamp(integer_arg(c, a, 1), 0.0, static_cast<double>(s.length())));
        return value::boolean(index_of(s, needle, from) != std::string::npos);
    });
    text("startsWith", 1, [](context & c, const string_object & s, std::span<value> a) -> value {
        const context::stack_mark before = c.mark_stack();
        if (is_regexp_value(c, arg_at(a, 0))) {
            c.throw_error("TypeError",
                          "First argument to String.prototype.startsWith must not be a regular "
                          "expression");
            return value::boolean(false);
        }
        if (detail::threw_since(c, before)) { return value::undefined(); }
        const std::string needle = string_arg(c, arg_at(a, 0));
        if (detail::threw_since(c, before)) { return value::undefined(); }
        const auto from = static_cast<std::size_t>(
            std::clamp(integer_arg(c, a, 1), 0.0, static_cast<double>(s.length())));
        return value::boolean(occurs_at(s, needle, from));
    });
    text("endsWith", 1, [](context & c, const string_object & s, std::span<value> a) -> value {
        const context::stack_mark before = c.mark_stack();
        if (is_regexp_value(c, arg_at(a, 0))) {
            c.throw_error("TypeError",
                          "First argument to String.prototype.endsWith must not be a regular "
                          "expression");
            return value::boolean(false);
        }
        if (detail::threw_since(c, before)) { return value::undefined(); }
        const std::string needle = string_arg(c, arg_at(a, 0));
        if (detail::threw_since(c, before)) { return value::undefined(); }
        // endsWith takes an END position, not a start: `"abc".endsWith("b", 2)`
        // asks whether the first two characters end in "b".
        const auto length = static_cast<double>(s.length());
        const double end = has_index(a, 1) ? integer_arg(c, a, 1) : length;
        const auto stop = static_cast<std::size_t>(std::clamp(end, 0.0, length));
        const std::size_t units = utf16::length(needle);
        return value::boolean(units <= stop && occurs_at(s, needle, stop - units));
    });
    text("slice", 2, [](context & c, const string_object & s, std::span<value> a) -> value {
        // Each ToIntegerOrInfinity is `?`: a throw from the first ends the
        // method before the second argument is coerced (its valueOf unrun).
        const detail::unwind_watch watch{c};
        const std::size_t length = s.length();
        const std::size_t from = clamp_index(integer_arg(c, a, 0), length);
        if (watch.threw()) { return value::undefined(); }
        // has_index: `slice(1, undefined)` ends at the LENGTH, not at 0.
        const std::size_t to = has_index(a, 1) ? clamp_index(integer_arg(c, a, 1), length) : length;
        return c.string(s.slice(from, to));
    });
    text("substring", 2, [](context & c, const string_object & s, std::span<value> a) -> value {
        // substring CLAMPS a negative to zero, unlike slice which counts from
        // the end - that difference between the two is the whole reason both
        // exist - and it SWAPS its arguments if they are backwards. has_index
        // so an explicit `undefined` end still means "to the end" rather than
        // zero.
        const detail::unwind_watch watch{c};
        const std::size_t length = s.length();
        std::size_t from = static_cast<std::size_t>(std::max(0.0, integer_arg(c, a, 0)));
        if (watch.threw()) { return value::undefined(); }
        std::size_t to = has_index(a, 1)
                             ? static_cast<std::size_t>(std::max(0.0, integer_arg(c, a, 1)))
                             : length;
        from = std::min(from, length);
        to = std::min(to, length);
        if (from > to) { std::swap(from, to); }
        return c.string(s.slice(from, to));
    });
    text("substr", 2, [](context & c, const string_object & s, std::span<value> a) -> value {
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
        const detail::unwind_watch watch{c};
        const double raw = integer_arg(c, a, 0);
        if (watch.threw()) { return value::undefined(); } // the length is not coerced
        const auto size = static_cast<double>(s.length());
        const double start = raw < 0 ? std::max(size + raw, 0.0) : std::min(raw, size);
        const auto from = static_cast<std::size_t>(start);
        // A missing length means "to the end"; a negative one means nothing.
        // has_index, so an explicit `undefined` length is also "to the end".
        double want = has_index(a, 1) ? integer_arg(c, a, 1) : size - start;
        if (std::isnan(want) || want < 0) { want = 0; }
        const auto count = static_cast<std::size_t>(std::min(want, size - start));
        return c.string(s.slice(from, from + count));
    });
    // 22.1.3.23. A separator with @@split - every RegExp - answers through it
    // (regexp.cpp's [@@split] is the specification's sticky walk); the string
    // form is here.
    text_after_dispatch("split", 2, CTBROWSER_SYMBOL_KEY("split"), false,
                        [](context & c, const string_object & s, std::span<value> a) -> value {
                            const context::stack_mark before = c.mark_stack();
                            value out = c.make_array();
                            const context::rooted keep{c, out};
                            auto * result = static_cast<array_object *>(out.as_heap());
                            // THE LIMIT, which this used to ignore completely - so
                            // `"a,b,c".split(",", 2)` handed back all three and `split(x, 0)`
                            // handed back everything instead of nothing. `undefined` means
                            // unlimited, and it is ToUint32 rather than an integer, so a negative
                            // wraps to a very large number (which is why 2**32-1 and "no limit"
                            // behave alike).
                            const std::size_t limit =
                                has_index(a, 1) ? static_cast<std::size_t>(uint32_arg(c, a[1]))
                                                : std::numeric_limits<std::size_t>::max();
                            if (detail::threw_since(c, before)) { return value::undefined(); }
                            // ToString(separator) is step 5 and the `lim = 0` return is step 6, IN
                            // THAT ORDER: a separator with a `toString` is coerced even when the
                            // limit already says the answer is [].
                            const value separator = arg_at(a, 0);
                            const std::string sep = string_arg(c, separator);
                            if (detail::threw_since(c, before)) { return value::undefined(); }
                            if (limit == 0) { return out; }
                            if (separator.is_undefined()) {
                                result->items.push_back(c.string(s.text));
                                return out;
                            }
                            // BY CODE UNIT for the empty separator: a pair is
                            // two elements, each its lone half.
                            const std::size_t length = s.length();
                            if (sep.empty()) {
                                for (std::size_t i = 0; i < length; ++i) {
                                    if (result->items.size() >= limit) { break; }
                                    result->items.push_back(c.string(s.unit_text(i)));
                                }
                                return out;
                            }
                            std::size_t at = 0;
                            const std::size_t step = utf16::length(sep);
                            while (result->items.size() < limit) {
                                const std::size_t found = index_of(s, sep, at);
                                if (found == std::string::npos) {
                                    result->items.push_back(c.string(s.slice(at, length)));
                                    break;
                                }
                                result->items.push_back(c.string(s.slice(at, found)));
                                at = found + step;
                            }
                            return out;
                        });
    // 22.1.3.19 replace and 22.1.3.20 replaceAll, THE STRING FORMS. A pattern
    // with @@replace - every RegExp - answered above through symbol_dispatch,
    // and regexp.cpp's [@@replace] is where the matching loop lives; what is
    // left is a literal search string, replaced once or at every position.
    //
    // Regexes did not work here at all once: the pattern was coerced with
    // to_string and looked for with std::string::find, so `s.replace(/ /g,
    // '|')` searched for the literal text of the regex object and, finding
    // none, returned the string unchanged. Silent, and it is the commonest use
    // of the method - acorn builds its keyword tables with exactly that call.
    const auto replace_string = [](context & c, const string_object & self, std::span<value> a,
                                   bool all) -> value {
        const context::stack_mark before = c.mark_stack();
        // Steps 3-8: ToString(searchValue), then IsCallable(replaceValue), then
        // ToString(replaceValue) - in that order, each observable.
        const std::string search = string_arg(c, arg_at(a, 0));
        if (detail::threw_since(c, before)) { return value::undefined(); }
        const value replace_value = arg_at(a, 1);
        const bool functional = replace_value.is_callable();
        std::string tpl;
        if (!functional) {
            tpl = string_arg(c, replace_value);
            if (detail::threw_since(c, before)) { return value::undefined(); }
        }
        // Every position is in units: what the replacer is handed, and where
        // GetSubstitution cuts `$`` and `$'`.
        const std::size_t search_length = utf16::length(search);
        std::vector<std::size_t> positions;
        const std::size_t advance = std::max<std::size_t>(1, search_length);
        for (std::size_t at = index_of(self, search, 0); at != std::string::npos;
             at = index_of(self, search, at + advance)) {
            positions.push_back(at);
            if (!all) { break; }
        }
        std::string out;
        std::size_t end_of_last = 0;
        const value subject = c.string(self.text);
        const context::rooted keep{c, subject};
        for (const std::size_t at : positions) {
            std::string replacement;
            if (functional) {
                const value args[3] = {c.string(search), value::number(static_cast<double>(at)),
                                       subject};
                const value produced = c.call(replace_value, args);
                if (detail::threw_since(c, before)) { return value::undefined(); }
                replacement = string_arg(c, produced);
                if (detail::threw_since(c, before)) { return value::undefined(); }
            } else if (!get_substitution(c, search, self.text, at, {}, value::undefined(), tpl,
                                         replacement)) {
                return value::undefined();
            }
            const std::string prefix = self.slice(end_of_last, at);
            if (!check_string_growth(c, out.size(), prefix.size())) { return value::undefined(); }
            out += prefix;
            if (!check_string_growth(c, out.size(), replacement.size())) {
                return value::undefined();
            }
            out += replacement;
            end_of_last = at + search_length;
        }
        const std::string rest = self.slice(end_of_last, self.length());
        if (!check_string_growth(c, out.size(), rest.size())) { return value::undefined(); }
        out += rest;
        return c.string(out);
    };

    text_after_dispatch(
        "replace", 2, CTBROWSER_SYMBOL_KEY("replace"), false,
        [replace_string](context & c, const string_object & s, std::span<value> a) -> value {
            return replace_string(c, s, a, false);
        });
    // `match` - the single commonest thing done with a regular expression.
    // 22.1.3.13: the argument's own @@match, else RegExpCreate and the new
    // pattern's @@match, which is where the `g` / no-`g` shapes are decided.
    text_after_dispatch("match", 1, CTBROWSER_SYMBOL_KEY("match"), false,
                        [](context & c, const string_object & self, std::span<value> a) -> value {
                            return create_and_invoke(c, arg_at(a, 0), nullptr,
                                                     CTBROWSER_SYMBOL_KEY("match"), self.text);
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
    text_after_dispatch("search", 1, CTBROWSER_SYMBOL_KEY("search"), false,
                        [](context & c, const string_object & self, std::span<value> a) -> value {
                            return create_and_invoke(c, arg_at(a, 0), nullptr,
                                                     CTBROWSER_SYMBOL_KEY("search"), self.text);
                        });
    // 22.1.3.14 matchAll: the non-global refusal first, then the argument's
    // @@matchAll, then RegExpCreate with "g" - which is what makes
    // `"aaa".matchAll("a")` three matches rather than the first forever.
    text_after_dispatch("matchAll", 1, CTBROWSER_SYMBOL_KEY("matchAll"), true,
                        [](context & c, const string_object & self, std::span<value> a) -> value {
                            return create_and_invoke(c, arg_at(a, 0), "g",
                                                     CTBROWSER_SYMBOL_KEY("matchAll"), self.text);
                        });
    text_after_dispatch(
        "replaceAll", 2, CTBROWSER_SYMBOL_KEY("replace"), true,
        [replace_string](context & c, const string_object & s, std::span<value> a) -> value {
            return replace_string(c, s, a, true);
        });
    // UNICODE'S DEFAULT CASE CONVERSION, from a table generated off the pinned
    // Unicode release (case_mapping.cpp says why that is not the host
    // dependence the ASCII-only fold was avoiding): `"Straße".toUpperCase()`
    // is "STRASSE" and a final Σ lowercases to ς, as in every browser.
    text("toUpperCase", 0, [](context & c, const string_object & s, std::span<value>) -> value {
        return c.string(unicode_case(s.text, true));
    });
    text("toLowerCase", 0, [](context & c, const string_object & s, std::span<value>) -> value {
        return c.string(unicode_case(s.text, false));
    });
    text("trim", 0, [](context & c, const string_object & s, std::span<value>) -> value {
        return c.string(std::string{trim_js_space(s.text)});
    });
    // 22.1.3.16. THE COUNT IS ToIntegerOrInfinity AND THEN A RANGE CHECK, not
    // a clamp: `"x".repeat(-1)` and `"".repeat(Infinity)` are each a RangeError
    // the clamp turned into "" and into a million-character string. And the
    // clamp could not see the case that mattered - an object count coerces to
    // NaN through the static to_number, `std::clamp` passes NaN straight
    // through, and the cast to size_t is undefined behaviour. See integer_arg.
    text("repeat", 1, [](context & c, const string_object & str, std::span<value> a) -> value {
        const std::string & s = str.text;
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
    const auto pad = [](context & c, const string_object & self, std::span<value> a,
                        bool at_start) {
        const double want = integer_arg(c, a, 0);
        const std::size_t length = self.length();
        // ToLength: negative and NaN are 0, so there is nothing to do. This is
        // the line `'abc'.padStart(NaN, 'def')` needed - it used to cast NaN to
        // a size_t and loop appending until the process died.
        if (!(want > static_cast<double>(length))) { return c.string(self.text); }
        if (want > max_string_length) {
            c.throw_error("RangeError", "Invalid string length");
            return c.string("");
        }
        const string_object filler{has_index(a, 1) ? string_arg(c, a[1]) : std::string{" "}};
        const std::size_t unit = filler.length();
        if (unit == 0) { return c.string(self.text); }
        // The filler repeated and CUT IN UNITS: `"abc".padEnd(6, "\u{1F4A9}")`
        // ends in a lone lead surrogate, as the specification's does.
        const auto fill_length = static_cast<std::size_t>(want) - length;
        std::string filled;
        filled.reserve(fill_length);
        for (std::size_t n = fill_length / unit; n > 0; --n) { filled += filler.text; }
        filled += filler.slice(0, fill_length % unit);
        return c.string(at_start ? filled + self.text : self.text + filled);
    };
    text("padStart", 1, [pad](context & c, const string_object & s, std::span<value> a) -> value {
        return pad(c, s, a, true);
    });
    text("padEnd", 1, [pad](context & c, const string_object & s, std::span<value> a) -> value {
        return pad(c, s, a, false);
    });
    text("trimStart", 0, [](context & c, const string_object & s, std::span<value>) -> value {
        return c.string(std::string{trim_js_space(s.text, true, false)});
    });
    text("trimEnd", 0, [](context & c, const string_object & s, std::span<value>) -> value {
        return c.string(std::string{trim_js_space(s.text, false, true)});
    });
    // Annex B.2.2.15-16: trimLeft and trimRight are not copies, they ARE
    // trimStart and trimEnd - the same function objects, so their `name` reads
    // "trimStart" and `String.prototype.trimLeft === String.prototype.trimStart`.
    string_proto->define("trimLeft", *string_proto->find("trimStart"), attr_builtin);
    string_proto->define("trimRight", *string_proto->find("trimEnd"), attr_builtin);
    // Annex B.2.2.2-14, the HTML methods, all one abstract operation:
    // CreateHTML (B.2.2.2.1). ToString(this) is on_text's, and it comes FIRST;
    // then ToString of the attribute's value, with every `"` in it written as
    // &quot; - the one escape the operation performs. `'<'.anchor('<')` is
    // `<a name="<"><</a>`, exactly.
    struct html_method {
        const char * name;
        const char * tag;
        const char * attribute;
    };
    static constexpr html_method html_methods[] = {
        {"anchor", "a", "name"},      {"big", "big", ""},
        {"blink", "blink", ""},       {"bold", "b", ""},
        {"fixed", "tt", ""},          {"fontcolor", "font", "color"},
        {"fontsize", "font", "size"}, {"italics", "i", ""},
        {"link", "a", "href"},        {"small", "small", ""},
        {"strike", "strike", ""},     {"sub", "sub", ""},
        {"sup", "sup", ""},
    };
    for (const html_method m : html_methods) {
        const std::string_view attribute = m.attribute;
        text(m.name, attribute.empty() ? 0 : 1,
             [m, attribute](context & c, const string_object & s, std::span<value> a) -> value {
                 std::string out = std::string{"<"} + m.tag;
                 if (!attribute.empty()) {
                     const context::stack_mark before = c.mark_stack();
                     const std::string v = string_arg(c, arg_at(a, 0));
                     if (detail::threw_since(c, before)) { return value::undefined(); }
                     out += ' ';
                     out += attribute;
                     out += "=\"";
                     for (const char ch : v) {
                         if (ch == '"') {
                             out += "&quot;";
                         } else {
                             out += ch;
                         }
                     }
                     out += '"';
                 }
                 out += '>';
                 out += s.text;
                 out += "</";
                 out += m.tag;
                 out += '>';
                 return c.string(out);
             });
    }
    // THE SAME CONVERSION as toUpperCase, deliberately. This was `std::toupper`,
    // which reads the GLOBAL C LOCALE: what it does to a byte above 0x7F is
    // implementation- and locale-defined, so `toLocaleUpperCase` and
    // `toUpperCase` could disagree about the same string depending on `LC_ALL` -
    // exactly the host dependence core/algorithms.hpp exists to prevent. With
    // no ECMA-402 there is no locale to tailor for (Turkish dotless i), and
    // 22.1.3.26 lets the locale forms answer the default.
    text("toLocaleUpperCase", 0,
         [](context & c, const string_object & s, std::span<value>) -> value {
             return c.string(unicode_case(s.text, true));
         });
    text("toLocaleLowerCase", 0,
         [](context & c, const string_object & s, std::span<value>) -> value {
             return c.string(unicode_case(s.text, false));
         });
    text("concat", 1, [](context & c, const string_object & s, std::span<value> a) -> value {
        const context::stack_mark before = c.mark_stack();
        std::string out = s.text;
        for (std::size_t i = 0; i < a.size(); ++i) {
            out += string_arg(c, a[i]);
            if (detail::threw_since(c, before)) { return value::undefined(); }
        }
        return c.string(out);
    });
    cx.set_prototype(context::proto_kind::string, string_proto);
}

} // namespace ctbrowser::script::builtins_detail
