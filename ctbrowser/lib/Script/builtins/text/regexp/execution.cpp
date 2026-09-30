#include "execution.hpp"

namespace ctbrowser::script::builtins_detail {

namespace regexp_detail {

namespace {

bool full_unicode_flags(std::string_view flags) {
    return flags.find('u') != std::string_view::npos || flags.find('v') != std::string_view::npos;
}

// WITHOUT `u` OR `v` THE PATTERN IS CODE UNITS (22.2.2): a literal
// "\u{1D306}" in it is two characters, each quantifiable and each a class
// member on its own, so it is compiled with its pairs split
// (utf16::split_pairs) - and matched against a subject split the same way
// (builtin_exec). A group's NAME is not a character of the pattern and keeps
// its pair: `(?<name>` and `\k<name>` are copied as written.
std::string split_pattern_pairs(const std::string & source) {
    if (!utf16::has_pair(source)) { return source; }
    std::string out;
    std::size_t done = 0;
    for (std::size_t i = 0; i < source.size(); ++i) {
        const bool group_name = source.compare(i, 3, "(?<") == 0 && i + 3 < source.size() &&
                                source[i + 3] != '=' && source[i + 3] != '!';
        const bool reference = source.compare(i, 3, "\\k<") == 0;
        if (source[i] == '\\' && !reference) {
            ++i; // an escaped character is not the start of either
            continue;
        }
        if (!group_name && !reference) { continue; }
        const std::size_t close = source.find('>', i);
        if (close == std::string::npos) { break; }
        out += utf16::split_pairs(std::string_view{source}.substr(done, i - done));
        out.append(source, i, close + 1 - i);
        done = close + 1;
        i = close;
    }
    out += utf16::split_pairs(std::string_view{source}.substr(done));
    return out;
}

} // namespace

// The compiled program lives in a cache keyed by `source\0flags`, so the same
// literal in a loop compiles once. Shared by every native this file installs
// and dies with them.

[[nodiscard]] std::shared_ptr<rx::rx_prog> compiled(const std::shared_ptr<regex_cache> & cache,
                                                    const std::string & source,
                                                    const std::string & flags) {
    const std::string key = source + '\0' + flags;
    const auto it = cache->find(key);
    if (it != cache->end()) { return it->second; }
    auto program = std::make_shared<rx::rx_prog>(
        rx::rx_compile(full_unicode_flags(flags) ? source : split_pattern_pairs(source), flags));
    cache->emplace(key, program);
    return program;
}

// [[OriginalSource]] and [[OriginalFlags]]. Private keys, which no source text
// can spell and no enumeration reports (value.hpp, private_key_prefix).

[[nodiscard]] object_object * regexp_table(value v) {
    if (!v.is_object()) { return nullptr; }
    auto * o = static_cast<object_object *>(v.as_heap());
    return o->find(slot_source) == nullptr ? nullptr : o;
}

// thisRegExpObject: the receiver of exec and the boolean accessors must carry
// the slots; anything else is a TypeError with the method's name in it.
[[nodiscard]] object_object * this_regexp(context & cx, const char * method) {
    object_object * o = regexp_table(cx.current_this());
    if (o == nullptr) {
        cx.throw_error("TypeError", std::string{method} + " requires that 'this' be a RegExp");
    }
    return o;
}

// EscapeRegExpPattern, 22.2.6.13.1: what `source` reads back so that
// `new RegExp(re.source)` and `eval("/" + re.source + "/")` both mean the
// same pattern again. The empty pattern is "(?:)", a bare `/` is escaped and
// a line terminator - LF, CR, U+2028 and U+2029, none of which a literal may
// hold - is spelled as its escape. One ALREADY escaped (`RegExp("\\\n")`, a
// backslash then an LF) keeps its backslash and takes only the letter, so the
// source reads `\n` and not `\\n`, which would be a backslash and an n.
[[nodiscard]] std::string escape_pattern(const std::string & source) {
    if (source.empty()) { return "(?:)"; }
    // The escape for a line terminator at `i`, and its width - or nothing.
    const auto terminator = [&](std::size_t i, std::size_t & width) -> const char * {
        width = 1;
        if (source[i] == '\n') { return "n"; }
        if (source[i] == '\r') { return "r"; }
        width = 3;
        if (source.compare(i, 3, "\xE2\x80\xA8") == 0) { return "u2028"; }
        if (source.compare(i, 3, "\xE2\x80\xA9") == 0) { return "u2029"; }
        return nullptr;
    };
    std::string out;
    bool in_class = false;
    for (std::size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        std::size_t width = 0;
        if (c == '\\' && i + 1 < source.size()) {
            out += c;
            if (const char * letter = terminator(i + 1, width)) {
                out += letter;
                i += width;
            } else {
                out += source[++i];
            }
            continue;
        }
        if (c == '[') { in_class = true; }
        if (c == ']') { in_class = false; }
        if (c == '/' && !in_class) {
            out += "\\/";
        } else if (const char * letter = terminator(i, width)) {
            out += '\\';
            out += letter;
            i += width - 1;
        } else {
            out += c;
        }
    }
    return out;
}

// The array `exec` returns: the whole match, then each group, with `index`,
// `input`, `groups` and - under `d` - `indices` alongside (22.2.7.2 steps
// 19-36). ctjs's wrapper dropped the positions; they were always in the match.
//
// THE MATCHER'S POSITIONS ARE BYTES into the WTF-8 text, and they cut the
// strings; every position the result REPORTS - `index` and each `indices`
// pair - is the UTF-16 index a script counts in (string_object::unit_index).
[[nodiscard]] value exec_result(context & cx, const rx::rx_prog & program, value subject,
                                const rx::rx_match & m, const string_object * matched) {
    const auto & str =
        matched != nullptr ? *matched : *static_cast<const string_object *>(subject.as_heap());
    const std::string & s = str.text;
    value out = cx.make_array();
    const context::rooted keep{cx, out};
    auto * arr = static_cast<array_object *>(out.as_heap());
    arr->items.push_back(cx.string(s.substr(m.begin, m.end - m.begin)));
    for (const auto & [from, to] : m.caps) {
        arr->items.push_back(from < 0 ? value::undefined()
                                      : cx.string(s.substr(static_cast<std::size_t>(from),
                                                           static_cast<std::size_t>(to - from))));
    }
    // `groups` is undefined when the pattern has no named group, and a
    // null-prototype object when it has (step 25.a): a group named
    // "toString" must not find Object.prototype's. A name used in more than
    // one alternative reports whichever slot participated.
    value groups = value::undefined();
    if (!program.names.empty()) {
        groups = cx.make_object();
        arr->groups = groups; // reachable from the rooted result while its strings allocate
        auto * g = static_cast<object_object *>(groups.as_heap());
        g->prototype = value::undefined(); // an EXPLICIT null (object_object::prototype)
        for (const auto & [name, slot] : program.names) {
            const auto & cap = m.caps[static_cast<std::size_t>(slot)];
            if (value * already = g->find(name); already != nullptr && cap.first < 0) { continue; }
            g->set(name,
                   cap.first < 0
                       ? value::undefined()
                       : cx.string(s.substr(static_cast<std::size_t>(cap.first),
                                            static_cast<std::size_t>(cap.second - cap.first))));
        }
    }
    arr->groups = groups;
    arr->is_match = true;
    arr->index = value::number(static_cast<double>(str.unit_index(m.begin)));
    // The subject itself: a string's identity is not observable, and it is
    // the same text.
    arr->input = subject;
    // AND AS OWN DATA PROPERTIES, which is what CreateDataProperty makes them
    // (steps 22-23 and 25): `hasOwnProperty("groups")`, a page's write, a
    // delete, OwnPropertyKeys - index, input, groups, then `indices` - and no
    // setter on Array.prototype runs. The slots above stay for the legacy
    // statics, which read the match as it was made.
    object_object & own = arr->named_table();
    own.set("index", arr->index);
    own.set("input", arr->input);
    own.set("groups", groups);
    if (program.indices) {
        // MakeMatchIndicesIndexPairArray, 22.2.7.8: `[start, end]` per group,
        // undefined for one that did not participate, and its own `groups`.
        const value indices = cx.make_array();
        arr->named_table().set("indices", indices); // rooted through the result
        auto * list = static_cast<array_object *>(indices.as_heap());
        const auto pair = [&](std::ptrdiff_t from, std::ptrdiff_t to) {
            if (from < 0) { return value::undefined(); }
            value p = cx.make_array();
            auto * xy = static_cast<array_object *>(p.as_heap());
            xy->items.push_back(
                value::number(static_cast<double>(str.unit_index(static_cast<std::size_t>(from)))));
            xy->items.push_back(
                value::number(static_cast<double>(str.unit_index(static_cast<std::size_t>(to)))));
            return p;
        };
        list->items.push_back(
            pair(static_cast<std::ptrdiff_t>(m.begin), static_cast<std::ptrdiff_t>(m.end)));
        for (const auto & [from, to] : m.caps) { list->items.push_back(pair(from, to)); }
        value index_groups = value::undefined();
        if (!program.names.empty()) {
            index_groups = cx.make_object();
            list->named_table().set("groups", index_groups);
            auto * g = static_cast<object_object *>(index_groups.as_heap());
            g->prototype = value::undefined(); // an EXPLICIT null (object_object::prototype)
            for (const auto & [name, slot] : program.names) {
                const auto & cap = m.caps[static_cast<std::size_t>(slot)];
                if (value * already = g->find(name); already != nullptr && cap.first < 0) {
                    continue;
                }
                g->set(name, pair(cap.first, cap.second));
            }
        }
        if (index_groups.is_undefined()) { list->named_table().set("groups", index_groups); }
    }
    return out;
}

// The six helpers below are RegExpExec and its neighbours as this file spells
// them; string.cpp reaches RegExp only through is_regexp, regexp_create and
// get_substitution (internal.hpp), so these are file-local.
bool has_regexp_matcher(value v) {
    return regexp_table(v) != nullptr;
}

bool regexp_slots(value v, std::string & source, std::string & flags) {
    object_object * o = regexp_table(v);
    if (o == nullptr) { return false; }
    const value * src = o->find(slot_source);
    const value * fl = o->find(slot_flags);
    source = static_cast<string_object *>(src->as_heap())->text;
    flags = fl == nullptr ? std::string{} : static_cast<string_object *>(fl->as_heap())->text;
    return true;
}

// AdvanceStringIndex, 22.2.7.3, in units: past one code unit, or - under `u`
// and `v` - past the whole surrogate pair that starts at `index`.
std::size_t advance_string_index(const string_object & s, std::size_t index, bool full_unicode) {
    if (!full_unicode || index + 1 >= s.length()) { return index + 1; }
    const char16_t first = s.unit(index);
    if (first < 0xD800 || first > 0xDBFF) { return index + 1; }
    const char16_t second = s.unit(index + 1);
    return second >= 0xDC00 && second <= 0xDFFF ? index + 2 : index + 1;
}

bool get_last_index(context & cx, value rx, double & out) {
    const detail::unwind_watch watch{cx};
    const value raw = cx.lookup_property(rx, "lastIndex");
    if (watch.threw() || !numeric_arg(cx, raw)) { return false; }
    out = to_length(cx.to_number_value(raw));
    return !watch.threw();
}

bool set_last_index(context & cx, value rx, double v) {
    const detail::unwind_watch watch{cx};
    cx.clear_store_rejected();
    cx.store_property(rx, "lastIndex", value::number(v));
    if (watch.threw()) { return false; }
    cx.strict_store_check("lastIndex");
    return !watch.threw();
}

value regexp_exec(context & cx, value rx, value subject) {
    const detail::unwind_watch watch{cx};
    const value exec = cx.lookup_property(rx, "exec");
    if (watch.threw()) { return value::undefined(); }
    if (exec.is_callable()) {
        const value result = cx.call(exec, std::span<const value>{&subject, 1}, rx);
        if (watch.threw()) { return value::undefined(); }
        if (!result.is_object_like() && !result.is_null()) {
            cx.throw_error("TypeError", "exec result must be an object or null");
            return value::undefined();
        }
        return result;
    }
    if (!has_regexp_matcher(rx)) {
        cx.throw_error("TypeError", "RegExp exec method called on an incompatible receiver");
        return value::undefined();
    }
    // The built-in matcher IS %RegExp.prototype.exec% - the INTRINSIC, kept
    // under a private key, not whatever `exec` a page has since put on the
    // prototype (step 4 runs exactly when that one is not callable).
    object_object * proto = cx.prototype(context::proto_kind::regexp);
    const value * intrinsic =
        proto == nullptr ? nullptr : proto->find(CTBROWSER_PRIVATE_KEY("IntrinsicRegExpExec:"));
    const value builtin = intrinsic == nullptr ? value::undefined() : *intrinsic;
    if (!builtin.is_callable()) { return value::null(); }
    const value result = cx.call(builtin, std::span<const value>{&subject, 1}, rx);
    return watch.threw() ? value::undefined() : result;
}

// RegExpBuiltinExec, 22.2.7.2. `lastIndex` is read through [[Get]] and
// ToLength - a page may have written anything into it - and written back
// through Set with Throw=true, so a frozen pattern refuses with a TypeError.
//
// lastIndex is a UNIT index and the matcher walks BYTES: the one conversion
// in is locate, and the ones out are unit_index. A lastIndex at a pair's low
// half is where the two views part: under `u` the input is code points and
// the match starts at the pair holding it (step 13.b); without `u` the input
// is code units, and the matcher walks the subject with its pairs split
// (utf16::split_pairs, as the pattern was compiled), where every unit is a
// code point of its own - so there is no pair to stand inside. The split
// text has the subject's units at the subject's unit indices, so every
// position reported from it is the subject's.
[[nodiscard]] value builtin_exec(context & cx, const std::shared_ptr<regex_cache> & cache, value rx,
                                 value subject) {
    std::string source;
    std::string flags;
    if (!regexp_slots(rx, source, flags)) { return value::null(); }
    const std::shared_ptr<rx::rx_prog> program = compiled(cache, source, flags);
    double last = 0;
    if (!get_last_index(cx, rx, last)) { return value::undefined(); }
    const bool stateful = program->global || program->sticky;
    if (!stateful) { last = 0; }
    const auto & subject_text = *static_cast<const string_object *>(subject.as_heap());
    const string_object & str =
        full_unicode_flags(flags) ? subject_text : subject_text.code_units();
    rx::rx_match m;
    bool hit = program->ok && last <= static_cast<double>(str.length());
    if (hit) {
        const std::size_t start = str.locate(static_cast<std::size_t>(last)).byte;
        hit = rx::rx_search(*program, str.text, start, m, program->sticky);
    }
    // A search that ran out of its depth or step budget has no answer: not
    // null, which would claim the pattern does not match.
    if (m.exhausted) {
        cx.throw_error("RangeError", "Maximum regular expression backtracking exceeded");
        return value::undefined();
    }
    if (!hit) {
        if (stateful && !set_last_index(cx, rx, 0)) { return value::undefined(); }
        return value::null();
    }
    if (stateful && !set_last_index(cx, rx, static_cast<double>(str.unit_index(m.end)))) {
        return value::undefined();
    }
    const value result = exec_result(cx, *program, subject, m, &str);
    // The legacy statics (RegExp.$1, RegExp.lastMatch, ...): updated by a
    // match of a RegExp with its legacy features, emptied by a subclass
    // instance's, so a stale answer is never reported for either.
    const context::rooted keep{cx, result};
    const bool legacy = static_cast<object_object *>(rx.as_heap())->find(slot_legacy) != nullptr;
    set_legacy_statics(cx, legacy ? result : value::undefined());
    return result;
}

void set_legacy_statics(context & cx, value result) {
    object_object * home = cx.prototype(context::proto_kind::regexp);
    if (home == nullptr) { return; }
    if (!result.is_array()) {
        home->define(slot_legacy_record, value::undefined(), attr_none);
        return;
    }
    // Every field is a value the result already holds - the strings are
    // shared, not copied - and the two contexts are cut from the subject
    // only when read, so a global replace over a long string does not copy
    // it twice more per match.
    const auto * match = static_cast<const array_object *>(result.as_heap());
    const value empty = cx.string(std::string{});
    const context::rooted keep_empty{cx, empty};
    const value record = cx.make_array();
    auto * fields = static_cast<array_object *>(record.as_heap());
    fields->items.assign(legacy_fields, empty);
    const std::size_t captures = match->items.size() - 1;
    // A group that did not participate reads as the empty String.
    const auto capture = [&](std::size_t i) {
        const value v = match->items[i + 1];
        return v.is_string() ? v : empty;
    };
    fields->items[legacy_input] = match->input;
    fields->items[legacy_last_match] = match->items[0];
    if (captures > 0) { fields->items[legacy_last_paren] = capture(captures - 1); }
    fields->items[legacy_subject] = match->input;
    fields->items[legacy_begin] = match->index;
    fields->items[legacy_end] =
        value::number(match->index.as_number() +
                      static_cast<double>(
                          static_cast<const string_object *>(match->items[0].as_heap())->length()));
    for (std::size_t i = 0; i < 9 && i < captures; ++i) {
        fields->items[legacy_paren1 + i] = capture(i);
    }
    home->define(slot_legacy_record, record, attr_none);
}

// SpeciesConstructor, 7.3.22: `O.constructor[@@species]`, or the default when
// the chain says nothing. Undefined with a throw in flight.
[[nodiscard]] value species_constructor(context & cx, value o, value fallback) {
    const detail::unwind_watch watch{cx};
    const value ctor = cx.lookup_property(o, "constructor");
    if (watch.threw()) { return value::undefined(); }
    if (ctor.is_undefined()) { return fallback; }
    if (!ctor.is_object_like()) {
        cx.throw_error("TypeError", "The constructor property is not an object");
        return value::undefined();
    }
    const value species = cx.lookup_property(ctor, CTBROWSER_SYMBOL_KEY("species"));
    if (watch.threw()) { return value::undefined(); }
    if (species.is_nullish()) { return fallback; }
    if (!is_constructor(species)) {
        cx.throw_error("TypeError", "The species is not a constructor");
        return value::undefined();
    }
    return species;
}

// The flags a @@method reads: ToString(Get(rx, "flags")). FALSE with a throw.
[[nodiscard]] bool read_flags(context & cx, value rx, std::string & out) {
    const detail::unwind_watch watch{cx};
    const value flags = cx.lookup_property(rx, "flags");
    if (watch.threw()) { return false; }
    if (!stringable_arg(cx, flags)) { return false; }
    out = cx.to_string(flags);
    return !watch.threw();
}

// A @@method's receiver must be an object (22.2.6.8 step 2 and its siblings).
[[nodiscard]] bool object_this(context & cx, value self, const char * method) {
    if (self.is_object_like()) { return true; }
    cx.throw_error("TypeError", std::string{method} + " called on a non-object");
    return false;
}

// ToString(Get(result, "0")): the matched text out of an exec result, which a
// page's own `exec` may have built by hand.
[[nodiscard]] bool matched_text(context & cx, value result, std::string & out) {
    const detail::unwind_watch watch{cx};
    const value first = cx.lookup_index(result, value::number(0));
    if (watch.threw() || !stringable_arg(cx, first)) { return false; }
    out = cx.to_string(first);
    return !watch.threw();
}

// %RegExpStringIteratorPrototype%.next (22.2.9.2.1). The iterator's state -
// the matcher, the subject and the two flags - is in private-keyed slots on
// the iterator itself, so the collector sees one object holding everything.
value regexp_string_iterator_next(context & cx, std::span<value>) {
    const value self = cx.current_this();
    auto * out = static_cast<object_object *>(cx.make_object().as_heap());
    const context::rooted keep{cx, value::object(out)};
    const auto finish = [&](value v, bool done) {
        out->set("value", v);
        out->set("done", value::boolean(done));
        return value::object(out);
    };
    if (!self.is_object()) {
        cx.throw_error("TypeError", "RegExp String Iterator next called on a non-object");
        return value::undefined();
    }
    auto * it = static_cast<object_object *>(self.as_heap());
    value * matcher = it->find(CTBROWSER_PRIVATE_KEY("IteratingRegExp"));
    value * subject = it->find(CTBROWSER_PRIVATE_KEY("IteratedString"));
    if (matcher == nullptr || subject == nullptr) {
        cx.throw_error("TypeError",
                       "next called on an object that is not a RegExp String Iterator");
        return value::undefined();
    }
    if (value * done = it->find(CTBROWSER_PRIVATE_KEY("Done"));
        done != nullptr && done->as_boolean()) {
        return finish(value::undefined(), true);
    }
    // THE SLOTS ARE PROPERTIES, and a computed key can spell `@#` - so this
    // reads them as a page may have left them: a missing flag is the
    // TypeError a missing slot is, and the subject is made a string again
    // before anything below treats it as one.
    const value * global_slot = it->find(CTBROWSER_PRIVATE_KEY("Global"));
    const value * unicode_slot = it->find(CTBROWSER_PRIVATE_KEY("FullUnicode"));
    if (global_slot == nullptr || unicode_slot == nullptr) {
        cx.throw_error("TypeError",
                       "next called on an object that is not a RegExp String Iterator");
        return value::undefined();
    }
    const bool global = context::truthy(*global_slot);
    const bool full_unicode = context::truthy(*unicode_slot);
    const value rx = *matcher;
    const value s = subject->is_string() ? *subject : cx.string(cx.to_string(*subject));
    if (cx.throw_pending()) { return value::undefined(); }
    const context::rooted keep_subject{cx, s};
    const value match = regexp_exec(cx, rx, s);
    if (match.is_undefined()) { return value::undefined(); }
    if (match.is_null()) {
        it->define(CTBROWSER_PRIVATE_KEY("Done"), value::boolean(true), attr_none);
        return finish(value::undefined(), true);
    }
    if (!global) {
        it->define(CTBROWSER_PRIVATE_KEY("Done"), value::boolean(true), attr_none);
        return finish(match, false);
    }
    std::string text;
    if (!matched_text(cx, match, text)) { return value::undefined(); }
    if (text.empty()) {
        double this_index = 0;
        if (!get_last_index(cx, rx, this_index)) { return value::undefined(); }
        const std::size_t next =
            advance_string_index(*static_cast<const string_object *>(s.as_heap()),
                                 static_cast<std::size_t>(this_index), full_unicode);
        if (!set_last_index(cx, rx, static_cast<double>(next))) { return value::undefined(); }
    }
    return finish(match, false);
}

} // namespace regexp_detail

} // namespace ctbrowser::script::builtins_detail
