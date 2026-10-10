//===- ctbrowser/lib/Script/builtins/text/regexp.cpp - RegExp builtins ----===//
//
// The RegExp constructor and prototype, following ECMA-262 22.2, plus the
// helpers String shares with it (is_regexp, get_substitution, regexp_create).
// Shared helpers and these functions' declarations are in ../internal.hpp;
// helpers shared only by RegExp and String are in ./internal.hpp, and the
// matching machinery is in regexp/execution.cpp.
//
// An instance holds two internal slots ([[OriginalSource]] and
// [[OriginalFlags]], stored as private-keyed properties) plus `lastIndex`.
// Everything else is an accessor or method on RegExp.prototype, including
// @@match/@@matchAll/@@replace/@@search/@@split. The String methods in
// string.cpp call these through `Invoke(rx, @@method)`, as 22.1.3 specifies.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

#include <map>

#include "regexp/execution.hpp"

namespace ctbrowser::script::builtins_detail {

using namespace regexp_detail;

/// IsRegExp (7.2.8): sets `out` to whether `v` is a RegExp, judged by its
/// @@match property, or by its internal slots when @@match is undefined.
/// Returns false only when reading @@match threw.
bool is_regexp(context & cx, value v, bool & out) {
    out = false;
    if (!v.is_object_like()) { return true; }
    const detail::unwind_watch watch{cx};
    const value matcher = cx.lookup_property(v, CTBROWSER_SYMBOL_KEY("match"));
    if (watch.threw()) { return false; }
    out = matcher.is_undefined() ? has_regexp_matcher(v) : context::truthy(matcher);
    return true;
}

bool get_substitution(context & cx, const std::string & matched, const std::string & subject,
                      std::size_t position, std::span<const value> captures, value named_captures,
                      const std::string & tpl, std::string & out) {
    out.clear();
    // `position` is a unit index, as the specification's is (22.1.3.19.1
    // steps 2-5): `$\`` is the subject's first `position` units and `$'` what
    // follows the match, both cut as UTF-16 would cut them, and converted only
    // when the template names one, which is rare.
    const auto before_match = [&] {
        return utf16::slice(subject, {0, false}, utf16::locate(subject, position));
    };
    const auto after_match = [&] {
        const std::size_t tail =
            std::min(utf16::length(subject), position + utf16::length(matched));
        return utf16::slice(subject, utf16::locate(subject, tail), {subject.size(), false});
    };
    std::size_t length = 0;
    const auto expand = [&](bool build) {
        length = 0;
        const auto append = [&](std::string_view text) {
            if (!check_string_growth(cx, length, text.size())) { return false; }
            length += text.size();
            if (build) { out += text; }
            return true;
        };
        for (std::size_t i = 0; i < tpl.size(); ++i) {
            const char c = tpl[i];
            if (c != '$' || i + 1 >= tpl.size()) {
                if (!append(std::string_view{tpl}.substr(i, 1))) { return false; }
                continue;
            }
            const char next = tpl[i + 1];
            if (next == '$') {
                if (!append("$")) { return false; }
                ++i;
            } else if (next == '&') {
                if (!append(matched)) { return false; }
                ++i;
            } else if (next == '`') {
                if (!append(before_match())) { return false; }
                ++i;
            } else if (next == '\'') {
                if (!append(after_match())) { return false; }
                ++i;
            } else if (next >= '0' && next <= '9') {
                // `$nn` when two digits name a group that exists, else `$n`, else
                // the text itself (22.1.3.19.1 steps 9-11 of the digit arm).
                std::size_t digits = 1;
                std::size_t index = static_cast<std::size_t>(next - '0');
                if (i + 2 < tpl.size() && tpl[i + 2] >= '0' && tpl[i + 2] <= '9') {
                    const std::size_t wide =
                        index * 10 + static_cast<std::size_t>(tpl[i + 2] - '0');
                    if (wide >= 1 && wide <= captures.size()) {
                        digits = 2;
                        index = wide;
                    }
                }
                if (index >= 1 && index <= captures.size()) {
                    const value cap = captures[index - 1];
                    if (!cap.is_undefined()) {
                        const auto & text = static_cast<const string_object *>(cap.as_heap())->text;
                        if (!append(text)) { return false; }
                    }
                } else {
                    if (!append(std::string_view{tpl}.substr(i, 1 + digits))) { return false; }
                }
                i += digits;
            } else if (next == '<') {
                // `$<name>` reads the groups object; with no groups object the
                // three characters are literal, and so are they when no `>` closes.
                const std::size_t close = tpl.find('>', i + 2);
                if (named_captures.is_undefined() || close == std::string::npos) {
                    if (!append("$<")) { return false; }
                    ++i;
                    continue;
                }
                const std::string name = tpl.substr(i + 2, close - (i + 2));
                const detail::unwind_watch watch{cx};
                const value cap = cx.lookup_property(named_captures, name);
                if (watch.threw()) { return false; }
                if (!cap.is_undefined()) {
                    if (!stringable_arg(cx, cap)) { return false; }
                    const std::string text = cx.to_string(cap);
                    if (watch.threw() || !append(text)) { return false; }
                }
                i = close;
            } else {
                if (!append(std::string_view{tpl}.substr(i, 1))) { return false; }
            }
        }
        return true;
    };
    // Numbered captures are already strings, so this pass has no observable
    // work and refuses huge expansions before allocating the result. Named
    // getters and their ToString calls must instead run once, in token order.
    if (named_captures.is_undefined()) {
        if (!expand(false)) { return false; }
        out.reserve(length);
    }
    return expand(true);
}

value regexp_create(context & cx, value pattern, value flags) {
    const value factory = cx.global(regexp_factory_name);
    if (!factory.is_callable()) { return value::undefined(); }
    const value args[2] = {pattern, flags};
    const detail::unwind_watch watch{cx};
    const value made = cx.call(factory, args);
    return watch.threw() ? value::undefined() : made;
}

void install_regexp(context & cx) {
    using detail::method;
    using detail::new_table;
    const auto cache = std::make_shared<regex_cache>();

    object_object * regexp_proto = new_table(cx);
    cx.set_prototype(context::proto_kind::regexp, regexp_proto);

    // RegExpInitialize (22.2.3.3), onto an object that already has the shape
    // RegExpAlloc gave it: ToString the pattern and flags (undefined is the
    // empty string for both), refuse bad flags and a bad pattern with a
    // SyntaxError, set the two slots, and Set lastIndex to 0 with Throw=true.
    const auto initialize = [cache](context & c, value obj, value pattern, value flags) -> value {
        if (!stringable_arg(c, pattern) || !stringable_arg(c, flags)) { return value::undefined(); }
        const detail::unwind_watch watch{c};
        const std::string p = pattern.is_undefined() ? std::string{} : c.to_string(pattern);
        if (watch.threw()) { return value::undefined(); }
        const std::string f = flags.is_undefined() ? std::string{} : c.to_string(flags);
        if (watch.threw()) { return value::undefined(); }
        // 22.2.3.4 steps 4-6: a pattern that is not a Pattern, or flags that
        // are not flags, is a SyntaxError, judged by the same early-error
        // scan a literal gets at parse time (the matcher's own reader is
        // lenient where Annex B is, and only refuses what it cannot read).
        if (const auto wrong = rx::rx_pattern_error(p, f)) {
            c.throw_error("SyntaxError",
                          "Invalid regular expression: /" + p + "/" + f + ": " + *wrong);
            return value::undefined();
        }
        const std::shared_ptr<rx::rx_prog> program = compiled(cache, p, f);
        if (!program->ok) {
            c.throw_error("SyntaxError", program->error);
            return value::undefined();
        }
        auto * o = static_cast<object_object *>(obj.as_heap());
        o->define(slot_source, c.string(p), attr_none);
        o->define(slot_flags, c.string(f), attr_none);
        if (o->find("lastIndex") == nullptr) {
            o->define("lastIndex", value::number(0), attr_writable);
        } else if (!set_last_index(c, obj, 0)) {
            return value::undefined();
        }
        return obj;
    };

    // RegExpAlloc for a caller that is not `new`: a fresh object on
    // RegExp.prototype with lastIndex { writable: true, enumerable: false,
    // configurable: false } (22.2.3.2 step 2), defined before the pattern is
    // parsed so that a SyntaxError leaves the shape RegExpAlloc made.
    const auto alloc = [](context & c) {
        value obj = c.make_object();
        auto * o = static_cast<object_object *>(obj.as_heap());
        if (object_object * table = c.prototype(context::proto_kind::regexp)) {
            o->prototype = value::object(table);
        }
        o->define("lastIndex", value::number(0), attr_writable);
        o->define(slot_legacy, value::boolean(true), attr_none); // new.target is %RegExp%.
        return obj;
    };

    // RegExpCreate (22.2.3.1): RegExpAlloc then RegExpInitialize, with none of
    // the constructor's IsRegExp reading. It is what a regex literal compiles
    // to (the reserved factory name, so a page that shadows RegExp cannot
    // change what its own literals mean) and what match, matchAll and search
    // build over a non-RegExp argument.
    const auto create = [initialize, alloc](context & c, std::span<value> a) -> value {
        const value obj = alloc(c);
        const context::rooted keep{c, obj};
        return initialize(c, obj, arg_at(a, 0), arg_at(a, 1));
    };

    // 22.2.4.1 RegExp(pattern, flags): `new RegExp` and the plain call.
    const auto make = [initialize, alloc](context & c, std::span<value> a) -> value {
        const value pattern = arg_at(a, 0);
        const value flags = arg_at(a, 1);
        const value self = c.current_this();
        // A native has no new.target; a fresh, empty `this` is `new` (see
        // detail::constructing_this).
        const bool constructing = detail::constructing_this(self);
        bool pattern_is_regexp = false;
        if (!is_regexp(c, pattern, pattern_is_regexp)) { return value::undefined(); }
        const detail::unwind_watch watch{c};
        // Step 4: `RegExp(re)` with no flags answers `re` itself when its
        // constructor is RegExp: the one case that does not allocate.
        if (!constructing && pattern_is_regexp && flags.is_undefined()) {
            const value ctor = c.lookup_property(pattern, "constructor");
            if (watch.threw()) { return value::undefined(); }
            if (ctor.is_heap() && ctor.as_heap() == c.global("RegExp").as_heap()) {
                return pattern;
            }
        }
        value p = pattern;
        value f = flags;
        if (std::string source, source_flags; regexp_slots(pattern, source, source_flags)) {
            // Step 5: another RegExp is copied from its slots.
            p = c.string(source);
            if (flags.is_undefined()) { f = c.string(source_flags); }
        } else if (pattern_is_regexp) {
            // Step 6: something that claims to be one is read through [[Get]].
            p = c.lookup_property(pattern, "source");
            if (watch.threw()) { return value::undefined(); }
            if (flags.is_undefined()) {
                f = c.lookup_property(pattern, "flags");
                if (watch.threw()) { return value::undefined(); }
            }
        }
        const context::rooted keep_p{c, p};
        const context::rooted keep_f{c, f};
        // RegExpAlloc: the instance `new` made (whose prototype came from
        // new.target, so a subclass instance is already on its own prototype),
        // or a fresh one on RegExp.prototype for a plain call.
        const value obj = constructing ? self : alloc(c);
        const context::rooted keep_obj{c, obj};
        if (constructing) {
            // context::make_instance gives a native constructor's instance no
            // prototype, so no prototype means `new RegExp` itself: give it
            // RegExp.prototype and the legacy features. An instance that
            // already has a prototype came from a subclass or a foreign
            // new.target and gets no legacy features (RegExpAlloc steps 2-3).
            auto * o = static_cast<object_object *>(obj.as_heap());
            if (!o->prototype.is_object()) {
                if (object_object * table = c.prototype(context::proto_kind::regexp)) {
                    o->prototype = value::object(table);
                }
                o->define(slot_legacy, value::boolean(true), attr_none);
            }
            o->define("lastIndex", value::number(0), attr_writable);
        }
        return initialize(c, obj, p, f);
    };

    auto * regexp_ctor = cx.allocate<native_object>("RegExp", make);
    detail::constant(regexp_ctor, "prototype", value::object(regexp_proto));
    link_constructor(cx, regexp_proto, "RegExp", 2, value::object(regexp_ctor));
    cx.define_global("RegExp", value::object(regexp_ctor));
    cx.define_native(std::string{regexp_factory_name}, create);
    {
        // 22.2.5.2 get RegExp[@@species]: `this`, so a subclass's @@split and
        // @@matchAll build instances of the subclass.
        auto * species =
            detail::method_native(cx, "get [Symbol.species]",
                                  [](context & c, std::span<value>) { return c.current_this(); });
        detail::install_arity(cx, species, 0);
        regexp_ctor->define_accessor(CTBROWSER_SYMBOL_KEY("species"), value::object(species),
                                     value::undefined(), attr_configurable);
    }
    // The legacy static properties (Annex B, the legacy RegExp features):
    // RegExp.input/$_, lastMatch/$&, lastParen/$+, leftContext/$`,
    // rightContext/$' and $1-$9, each an accessor { enumerable: false,
    // configurable: true } over the record RegExpBuiltinExec keeps (see
    // set_legacy_statics). GetLegacyRegExpStaticProperty refuses a receiver
    // that is not %RegExp% itself (a subclass constructor included) and an
    // emptied record; only `input` has a setter, storing ToString of its value.
    regexp_proto->define(slot_intrinsic, value::object(regexp_ctor), attr_none);
    {
        const value empty = cx.string(std::string{});
        const context::rooted keep{cx, empty};
        const value record = cx.make_array();
        auto * fields = static_cast<array_object *>(record.as_heap());
        fields->items.assign(legacy_fields, empty);
        fields->items[legacy_begin] = value::number(0);
        fields->items[legacy_end] = value::number(0);
        regexp_proto->define(slot_legacy_record, record, attr_none);
    }
    // Returns the legacy statics record, or null with the TypeError in flight
    // (wrong receiver, or the record was emptied by a subclass's match).
    const auto legacy_record = [](context & c, const std::string & name) -> array_object * {
        object_object * home = c.prototype(context::proto_kind::regexp);
        const value * intrinsic = home == nullptr ? nullptr : home->find(slot_intrinsic);
        if (intrinsic == nullptr || !c.current_this().same_value(*intrinsic)) {
            c.throw_error("TypeError", "RegExp." + name + " requires that 'this' be %RegExp%");
            return nullptr;
        }
        const value * record = home->find(slot_legacy_record);
        if (record == nullptr || !record->is_array()) {
            c.throw_error("TypeError",
                          "RegExp." + name + " is empty after a match by a RegExp subclass");
            return nullptr;
        }
        return static_cast<array_object *>(record->as_heap());
    };
    // `field` is the record slot; -1 and -2 are leftContext and rightContext,
    // cut from the matched subject on demand.
    const auto legacy_accessor = [&](int field, std::initializer_list<const char *> names) {
        for (const char * name : names) {
            const std::string shown = name;
            const value getter = detail::accessor_fn(
                cx, "get " + shown, [legacy_record, field, shown](context & c, std::span<value>) {
                    array_object * record = legacy_record(c, shown);
                    if (record == nullptr) { return value::undefined(); }
                    if (field >= 0) { return record->items[static_cast<std::size_t>(field)]; }
                    // Unit positions, like the match's `index`.
                    const auto & subject = *static_cast<const string_object *>(
                        record->items[legacy_subject].as_heap());
                    const auto begin =
                        static_cast<std::size_t>(record->items[legacy_begin].as_number());
                    const auto end =
                        static_cast<std::size_t>(record->items[legacy_end].as_number());
                    return c.string(field == -1 ? subject.slice(0, begin)
                                                : subject.slice(end, subject.length()));
                });
            detail::install_arity(cx, static_cast<native_object *>(getter.as_heap()), 0);
            value setter = value::undefined();
            if (field == legacy_input) {
                setter = detail::accessor_fn(
                    cx, "set " + shown, [legacy_record, shown](context & c, std::span<value> a) {
                        if (legacy_record(c, shown) == nullptr) { return value::undefined(); }
                        const detail::unwind_watch watch{c};
                        const std::string text = string_arg(c, arg_at(a, 0));
                        if (watch.threw()) { return value::undefined(); }
                        // Re-found: ToString may have run a match that replaced it.
                        if (array_object * record = legacy_record(c, shown)) {
                            record->items[legacy_input] = c.string(text);
                        }
                        return value::undefined();
                    });
                detail::install_arity(cx, static_cast<native_object *>(setter.as_heap()), 1);
            }
            regexp_ctor->define_accessor(shown, getter, setter, attr_configurable);
        }
    };
    legacy_accessor(legacy_input, {"input", "$_"});
    legacy_accessor(legacy_last_match, {"lastMatch", "$&"});
    legacy_accessor(legacy_last_paren, {"lastParen", "$+"});
    legacy_accessor(-1, {"leftContext", "$`"});
    legacy_accessor(-2, {"rightContext", "$'"});
    for (int i = 0; i < 9; ++i) {
        const std::string paren = "$" + std::to_string(i + 1);
        legacy_accessor(static_cast<int>(legacy_paren1) + i, {paren.c_str()});
    }

    // 22.2.5.1 RegExp.escape(S), ES2025: a string that matches S literally.
    // The first character is hex-escaped when it is alphanumeric (so the
    // result never continues a preceding `\1` or `\u`), a syntax character
    // and `/` are backslashed, the five control letters use their letter, and
    // the other punctuators, white space, line terminators and lone surrogates
    // become \xHH or \uHHHH. Everything else, including every non-ASCII code
    // point, passes through as itself; the subject is UTF-8 here so a code
    // point is its byte run.
    method(cx, regexp_ctor, "escape", 1, [](context & c, std::span<value> a) -> value {
        const value arg = arg_at(a, 0);
        if (!arg.is_string()) {
            c.throw_error("TypeError", "RegExp.escape requires a string");
            return value::undefined();
        }
        const std::string & s = static_cast<string_object *>(arg.as_heap())->text;
        std::string out;
        const auto hex = [](std::uint32_t n, int width) {
            static constexpr char digits[] = "0123456789abcdef";
            std::string h;
            for (int i = width - 1; i >= 0; --i) { h += digits[(n >> (4 * i)) & 0xFu]; }
            return h;
        };
        const auto escape_unit = [&](std::uint32_t cu) {
            return cu <= 0xFFu ? "\\x" + hex(cu, 2) : "\\u" + hex(cu, 4);
        };
        for (std::size_t i = 0; i < s.size();) {
            // One code point, decoded forward; a stray byte is left alone.
            std::size_t width = 1;
            const std::uint32_t cp = rx::rx_utf8_decode(s, i, width);
            const std::string_view raw{s.data() + i, width};
            i += width;
            const bool alnum =
                (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
            if (out.empty() && i == width && alnum) {
                out += escape_unit(cp);
                continue;
            }
            if (std::string_view{"^$\\.*+?()[]{}|/"}.find(static_cast<char>(cp)) !=
                    std::string_view::npos &&
                cp < 0x80u) {
                out += '\\';
                out += static_cast<char>(cp);
                continue;
            }
            switch (cp) {
            case '\t': out += "\\t"; continue;
            case '\n': out += "\\n"; continue;
            case '\v': out += "\\v"; continue;
            case '\f': out += "\\f"; continue;
            case '\r': out += "\\r"; continue;
            default: break;
            }
            const bool other_punctuator =
                cp < 0x80u && std::string_view{",-=<>#&!%:;@~'`\""}.find(static_cast<char>(cp)) !=
                                  std::string_view::npos;
            const bool space = cp == ' ' || cp == 0xA0u || cp == 0x1680u ||
                               (cp >= 0x2000u && cp <= 0x200Au) || cp == 0x2028u || cp == 0x2029u ||
                               cp == 0x202Fu || cp == 0x205Fu || cp == 0x3000u || cp == 0xFEFFu;
            const bool surrogate = cp >= 0xD800u && cp <= 0xDFFFu;
            if (other_punctuator || space || surrogate) {
                out += escape_unit(cp);
                continue;
            }
            out += raw;
        }
        return c.string(out);
    });

    // The accessors (22.2.6.3-22.2.6.19).
    //
    // Each of the flag getters reads its slot; a receiver without the slots is
    // a TypeError unless it is RegExp.prototype itself, which answers undefined
    // (the prototype is not a RegExp, and the specification says so twice per
    // accessor). `source` answers "(?:)" for the prototype and `flags` is
    // generic: it reads the eight booleans through [[Get]], so a subclass that
    // overrides `global` changes what `flags` says.
    const auto flag_getter = [&](const char * name, char letter) {
        auto * getter = detail::method_native(
            cx, std::string{"get "} + name, [letter, name](context & c, std::span<value>) -> value {
                const value self = c.current_this();
                std::string source;
                std::string flags;
                if (regexp_slots(self, source, flags)) {
                    return value::boolean(flags.find(letter) != std::string::npos);
                }
                if (self.is_object() &&
                    self.as_heap() == c.prototype(context::proto_kind::regexp)) {
                    return value::undefined();
                }
                c.throw_error("TypeError", std::string{"RegExp.prototype."} + name +
                                               " getter called on a non-RegExp");
                return value::undefined();
            });
        detail::install_arity(cx, getter, 0);
        regexp_proto->define_accessor(name, value::object(getter), value::undefined(),
                                      attr_configurable);
    };
    flag_getter("hasIndices", 'd');
    flag_getter("global", 'g');
    flag_getter("ignoreCase", 'i');
    flag_getter("multiline", 'm');
    flag_getter("dotAll", 's');
    flag_getter("unicode", 'u');
    flag_getter("unicodeSets", 'v');
    flag_getter("sticky", 'y');
    {
        auto * getter =
            detail::method_native(cx, "get source", [](context & c, std::span<value>) -> value {
                const value self = c.current_this();
                std::string source;
                std::string flags;
                if (regexp_slots(self, source, flags)) { return c.string(escape_pattern(source)); }
                if (self.is_object() &&
                    self.as_heap() == c.prototype(context::proto_kind::regexp)) {
                    return c.string("(?:)");
                }
                c.throw_error("TypeError", "RegExp.prototype.source getter called on a non-RegExp");
                return value::undefined();
            });
        detail::install_arity(cx, getter, 0);
        regexp_proto->define_accessor("source", value::object(getter), value::undefined(),
                                      attr_configurable);
    }
    {
        auto * getter =
            detail::method_native(cx, "get flags", [](context & c, std::span<value>) -> value {
                const value self = c.current_this();
                if (!object_this(c, self, "RegExp.prototype.flags getter")) {
                    return value::undefined();
                }
                // 22.2.6.4 step 4 onward, in the specification's order, each a
                // [[Get]] that may run a page's getter and throw.
                static constexpr std::pair<const char *, char> letters[] = {
                    {"hasIndices", 'd'}, {"global", 'g'},  {"ignoreCase", 'i'},  {"multiline", 'm'},
                    {"dotAll", 's'},     {"unicode", 'u'}, {"unicodeSets", 'v'}, {"sticky", 'y'},
                };
                std::string out;
                const detail::unwind_watch watch{c};
                for (const auto & [name, letter] : letters) {
                    const value on = c.lookup_property(self, name);
                    if (watch.threw()) { return value::undefined(); }
                    if (context::truthy(on)) { out += letter; }
                }
                return c.string(out);
            });
        detail::install_arity(cx, getter, 0);
        regexp_proto->define_accessor("flags", value::object(getter), value::undefined(),
                                      attr_configurable);
    }

    // exec, test, toString and compile.
    method(cx, regexp_proto, "exec", 1, [cache](context & c, std::span<value> a) -> value {
        if (this_regexp(c, "RegExp.prototype.exec") == nullptr) { return value::undefined(); }
        const value arg = arg_at(a, 0);
        if (!stringable_arg(c, arg)) { return value::undefined(); }
        const value self = c.current_this();
        const detail::unwind_watch watch{c};
        // The subject as a string value: the result's `input` is it, and the
        // units it has measured stay on it for the next exec over it.
        const value subject = arg.is_string() ? arg : c.string(c.to_string(arg));
        if (watch.threw()) { return value::undefined(); }
        const context::rooted keep{c, subject};
        return builtin_exec(c, cache, self, subject);
    });
    // %RegExp.prototype.exec% by a name no page can reach or replace: what
    // RegExpExec falls back to (22.2.7.1 step 4) when a page has set `exec`
    // to something that is not callable. The `:` marks it engine data, as
    // Object.prototype's intrinsic toString does.
    regexp_proto->define(CTBROWSER_PRIVATE_KEY("IntrinsicRegExpExec:"), *regexp_proto->find("exec"),
                         attr_none);
    // 22.2.6.16: `test` is RegExpExec, so a subclass's `exec` is what it asks.
    method(cx, regexp_proto, "test", 1, [](context & c, std::span<value> a) -> value {
        const value self = c.current_this();
        if (!object_this(c, self, "RegExp.prototype.test")) { return value::undefined(); }
        if (!stringable_arg(c, arg_at(a, 0))) { return value::undefined(); }
        const detail::unwind_watch watch{c};
        const value subject = c.string(c.to_string(arg_at(a, 0)));
        if (watch.threw()) { return value::undefined(); }
        const context::rooted keep{c, subject};
        const value result = regexp_exec(c, self, subject);
        if (result.is_undefined()) { return value::undefined(); }
        return value::boolean(!result.is_null());
    });
    // 22.2.6.17 toString is generic: "/" + ToString(source) + "/" + ToString(flags)
    // through [[Get]], so `RegExp.prototype.toString.call({source: "a",
    // flags: "g"})` is "/a/g".
    method(cx, regexp_proto, "toString", 0, [](context & c, std::span<value>) -> value {
        const value self = c.current_this();
        if (!object_this(c, self, "RegExp.prototype.toString")) { return value::undefined(); }
        const detail::unwind_watch watch{c};
        const value source = c.lookup_property(self, "source");
        if (watch.threw() || !stringable_arg(c, source)) { return value::undefined(); }
        const std::string p = c.to_string(source);
        if (watch.threw()) { return value::undefined(); }
        const value flags = c.lookup_property(self, "flags");
        if (watch.threw() || !stringable_arg(c, flags)) { return value::undefined(); }
        const std::string f = c.to_string(flags);
        if (watch.threw()) { return value::undefined(); }
        return c.string("/" + p + "/" + f);
    });
    // B.2.4.1 RegExp.prototype.compile(pattern, flags): re-initialise in
    // place. Another RegExp as the pattern is copied from its slots, and
    // then flags must be absent.
    method(cx, regexp_proto, "compile", 2, [initialize](context & c, std::span<value> a) -> value {
        const value self = c.current_this();
        object_object * o = this_regexp(c, "RegExp.prototype.compile");
        if (o == nullptr) { return value::undefined(); }
        // compile is itself a legacy feature: an instance without them (a
        // subclass's) refuses it.
        if (o->find(slot_legacy) == nullptr) {
            c.throw_error("TypeError", "RegExp.prototype.compile: the legacy RegExp features are "
                                       "disabled for this object");
            return value::undefined();
        }
        value p = arg_at(a, 0);
        value f = arg_at(a, 1);
        if (std::string source, flags; regexp_slots(p, source, flags)) {
            if (!f.is_undefined()) {
                c.throw_error("TypeError", "flags must be undefined when the pattern is a RegExp");
                return value::undefined();
            }
            p = c.string(source);
            f = c.string(flags);
        }
        const context::rooted keep_p{c, p};
        const context::rooted keep_f{c, f};
        return initialize(c, self, p, f);
    });

    // The Symbol methods.
    //
    // Named "[Symbol.match]" and so on (their `name` properties), installed
    // under the well-known symbol's key (core/symbol.hpp).
    const auto symbol_method = [&](std::string_view symbol, double arity, native_fn fn) {
        auto * made =
            detail::method_native(cx, "[Symbol." + std::string{symbol} + "]", std::move(fn));
        detail::install_arity(cx, made, arity);
        regexp_proto->define(std::string{symbol_key_mark} + std::string{symbol},
                             value::object(made), attr_builtin);
    };

    // 22.2.6.8 [@@match](string)
    symbol_method("match", 1, [](context & c, std::span<value> a) -> value {
        const value rx = c.current_this();
        if (!object_this(c, rx, "RegExp.prototype[Symbol.match]")) { return value::undefined(); }
        if (!stringable_arg(c, arg_at(a, 0))) { return value::undefined(); }
        const detail::unwind_watch watch{c};
        const std::string s = c.to_string(arg_at(a, 0));
        if (watch.threw()) { return value::undefined(); }
        const value subject = c.string(s);
        const context::rooted keep{c, subject};
        const auto & str = *static_cast<const string_object *>(subject.as_heap());
        std::string flags;
        if (!read_flags(c, rx, flags)) { return value::undefined(); }
        if (flags.find('g') == std::string::npos) { return regexp_exec(c, rx, subject); }
        const bool full_unicode =
            flags.find('u') != std::string::npos || flags.find('v') != std::string::npos;
        if (!set_last_index(c, rx, 0)) { return value::undefined(); }
        const value list = c.make_array();
        const context::rooted keep_list{c, list};
        auto * items = static_cast<array_object *>(list.as_heap());
        while (true) {
            const value result = regexp_exec(c, rx, subject);
            if (result.is_undefined()) { return value::undefined(); }
            if (result.is_null()) { return items->items.empty() ? value::null() : list; }
            std::string text;
            if (!matched_text(c, result, text)) { return value::undefined(); }
            items->items.push_back(c.string(text));
            if (text.empty()) {
                double this_index = 0;
                if (!get_last_index(c, rx, this_index)) { return value::undefined(); }
                const std::size_t next =
                    advance_string_index(str, static_cast<std::size_t>(this_index), full_unicode);
                if (!set_last_index(c, rx, static_cast<double>(next))) {
                    return value::undefined();
                }
            }
        }
    });

    // 22.2.6.9 [@@matchAll](string): a %RegExpStringIterator% over a species
    // copy of the receiver, so the caller's lastIndex is untouched.
    object_object * iterator_proto = new_table(cx);
    if (object_object * iterator_base = cx.prototype(context::proto_kind::generator);
        iterator_base != nullptr && iterator_base->prototype.is_object()) {
        iterator_proto->prototype = iterator_base->prototype; // %IteratorPrototype%
    }
    method(cx, iterator_proto, "next", 0, regexp_string_iterator_next);
    iterator_proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("RegExp String Iterator"),
                           attr_configurable);
    symbol_method("matchAll", 1, [iterator_proto](context & c, std::span<value> a) -> value {
        const value rx = c.current_this();
        if (!object_this(c, rx, "RegExp.prototype[Symbol.matchAll]")) { return value::undefined(); }
        if (!stringable_arg(c, arg_at(a, 0))) { return value::undefined(); }
        const detail::unwind_watch watch{c};
        const value subject = c.string(c.to_string(arg_at(a, 0)));
        if (watch.threw()) { return value::undefined(); }
        const context::rooted keep{c, subject};
        const value ctor = species_constructor(c, rx, c.global("RegExp"));
        if (ctor.is_undefined()) { return value::undefined(); }
        std::string flags;
        if (!read_flags(c, rx, flags)) { return value::undefined(); }
        const value flags_value = c.string(flags);
        const context::rooted keep_flags{c, flags_value};
        const value args[2] = {rx, flags_value};
        const value matcher = c.construct(ctor, args);
        if (watch.threw()) { return value::undefined(); }
        const context::rooted keep_matcher{c, matcher};
        double last = 0;
        if (!get_last_index(c, rx, last) || !set_last_index(c, matcher, last)) {
            return value::undefined();
        }
        // %RegExpStringIteratorPrototype%.[[Prototype]] is
        // %IteratorPrototype% (22.2.9.2), found now, at first
        // use, because Iterator installs after RegExp does.
        if (!iterator_proto->prototype.is_object()) {
            if (const value iterator = c.global("Iterator"); iterator.is_object_like()) {
                const value base = c.lookup_property(iterator, "prototype");
                if (base.is_object()) { iterator_proto->prototype = base; }
            }
        }
        value made = c.make_object();
        auto * it = static_cast<object_object *>(made.as_heap());
        it->prototype = value::object(iterator_proto);
        it->define(CTBROWSER_PRIVATE_KEY("IteratingRegExp"), matcher, attr_none);
        it->define(CTBROWSER_PRIVATE_KEY("IteratedString"), subject, attr_none);
        it->define(CTBROWSER_PRIVATE_KEY("Global"),
                   value::boolean(flags.find('g') != std::string::npos), attr_none);
        it->define(CTBROWSER_PRIVATE_KEY("FullUnicode"),
                   value::boolean(flags.find('u') != std::string::npos ||
                                  flags.find('v') != std::string::npos),
                   attr_none);
        it->define(CTBROWSER_PRIVATE_KEY("Done"), value::boolean(false), attr_none);
        return made;
    });
    method(cx, iterator_proto, CTBROWSER_SYMBOL_KEY("iterator"), 0,
           [](context & c, std::span<value>) { return c.current_this(); });

    // 22.2.6.11 [@@replace](string, replaceValue)
    symbol_method("replace", 2, [](context & c, std::span<value> a) -> value {
        const value rx = c.current_this();
        if (!object_this(c, rx, "RegExp.prototype[Symbol.replace]")) { return value::undefined(); }
        if (!stringable_arg(c, arg_at(a, 0))) { return value::undefined(); }
        const detail::unwind_watch watch{c};
        const std::string s = c.to_string(arg_at(a, 0));
        if (watch.threw()) { return value::undefined(); }
        const value subject = c.string(s);
        const context::rooted keep{c, subject};
        // Every position below (`index`, the replacer's argument, lastIndex) is
        // in units, and the pieces between matches are cut through the
        // subject's own marks.
        const auto & str = *static_cast<const string_object *>(subject.as_heap());
        const std::size_t length = str.length();
        const value replace_value = arg_at(a, 1);
        const bool functional = replace_value.is_callable();
        std::string tpl;
        if (!functional) {
            if (!stringable_arg(c, replace_value)) { return value::undefined(); }
            tpl = c.to_string(replace_value);
            if (watch.threw()) { return value::undefined(); }
        }
        std::string flags;
        if (!read_flags(c, rx, flags)) { return value::undefined(); }
        const bool global = flags.find('g') != std::string::npos;
        const bool full_unicode =
            flags.find('u') != std::string::npos || flags.find('v') != std::string::npos;
        if (global && !set_last_index(c, rx, 0)) { return value::undefined(); }
        // Every match is collected first (steps 11-12), then the replacements
        // are built (step 14): a replacer function runs after the matching
        // is over and cannot change what matched.
        const value results = c.make_array();
        const context::rooted keep_results{c, results};
        auto * list = static_cast<array_object *>(results.as_heap());
        while (true) {
            const value result = regexp_exec(c, rx, subject);
            if (result.is_undefined()) { return value::undefined(); }
            if (result.is_null()) { break; }
            list->items.push_back(result);
            if (!global) { break; }
            std::string text;
            if (!matched_text(c, result, text)) { return value::undefined(); }
            if (text.empty()) {
                double this_index = 0;
                if (!get_last_index(c, rx, this_index)) { return value::undefined(); }
                const std::size_t next =
                    advance_string_index(str, static_cast<std::size_t>(this_index), full_unicode);
                if (!set_last_index(c, rx, static_cast<double>(next))) {
                    return value::undefined();
                }
            }
        }
        std::string accumulated;
        std::size_t next_source_position = 0;
        for (std::size_t r = 0; r < list->items.size(); ++r) {
            const value result = list->items[r];
            const double result_length = detail::array_like_length(c, result);
            if (watch.threw()) { return value::undefined(); }
            const std::size_t n_captures =
                result_length > 1 ? static_cast<std::size_t>(result_length - 1) : 0;
            std::string matched;
            if (!matched_text(c, result, matched)) { return value::undefined(); }
            const value raw_position = c.lookup_property(result, "index");
            if (watch.threw() || !numeric_arg(c, raw_position)) { return value::undefined(); }
            double position = c.to_number_value(raw_position);
            if (watch.threw()) { return value::undefined(); }
            position = std::isnan(position) ? 0 : std::trunc(position);
            position = std::clamp(position, 0.0, static_cast<double>(length));
            const auto at = static_cast<std::size_t>(position);
            std::vector<value> captures;
            for (std::size_t n = 1; n <= n_captures; ++n) {
                value cap = c.lookup_index(result, value::number(static_cast<double>(n)));
                if (watch.threw()) { return value::undefined(); }
                if (!cap.is_undefined()) {
                    if (!stringable_arg(c, cap)) { return value::undefined(); }
                    cap = c.string(c.to_string(cap));
                    if (watch.threw()) { return value::undefined(); }
                }
                captures.push_back(cap);
            }
            const context::rooted_values keep_captures{c, captures};
            value named_captures = c.lookup_property(result, "groups");
            if (watch.threw()) { return value::undefined(); }
            std::string replacement;
            if (functional) {
                std::vector<value> args;
                args.push_back(c.string(matched));
                args.insert(args.end(), captures.begin(), captures.end());
                args.push_back(value::number(position));
                args.push_back(subject);
                if (!named_captures.is_undefined()) { args.push_back(named_captures); }
                const context::rooted_values keep_args{c, args};
                const value produced = c.call(replace_value, args);
                if (watch.threw() || !stringable_arg(c, produced)) { return value::undefined(); }
                replacement = c.to_string(produced);
                if (watch.threw()) { return value::undefined(); }
            } else {
                if (!named_captures.is_undefined()) {
                    named_captures = detail::box_primitive(c, named_captures);
                    if (named_captures.is_nullish()) {
                        c.throw_error("TypeError", "groups is null");
                        return value::undefined();
                    }
                }
                const context::rooted keep_named{c, named_captures};
                if (!get_substitution(c, matched, s, at, captures, named_captures, tpl,
                                      replacement)) {
                    return value::undefined();
                }
            }
            if (at >= next_source_position) {
                const std::string prefix = str.slice(next_source_position, at);
                if (!check_string_growth(c, accumulated.size(), prefix.size())) {
                    return value::undefined();
                }
                accumulated += prefix;
                if (!check_string_growth(c, accumulated.size(), replacement.size())) {
                    return value::undefined();
                }
                accumulated += replacement;
                next_source_position = at + utf16::length(matched);
            }
        }
        if (next_source_position < length) {
            const std::string rest = str.slice(next_source_position, length);
            if (!check_string_growth(c, accumulated.size(), rest.size())) {
                return value::undefined();
            }
            accumulated += rest;
        }
        return c.string(accumulated);
    });

    // 22.2.6.12 [@@search](string): lastIndex is put back however it was.
    symbol_method("search", 1, [](context & c, std::span<value> a) -> value {
        const value rx = c.current_this();
        if (!object_this(c, rx, "RegExp.prototype[Symbol.search]")) { return value::undefined(); }
        if (!stringable_arg(c, arg_at(a, 0))) { return value::undefined(); }
        const detail::unwind_watch watch{c};
        const value subject = c.string(c.to_string(arg_at(a, 0)));
        if (watch.threw()) { return value::undefined(); }
        const context::rooted keep{c, subject};
        const value previous = c.lookup_property(rx, "lastIndex");
        if (watch.threw()) { return value::undefined(); }
        const context::rooted keep_previous{c, previous};
        if (!(previous.is_number() && previous.as_number() == 0 &&
              !std::signbit(previous.as_number()))) {
            if (!set_last_index(c, rx, 0)) { return value::undefined(); }
        }
        const value result = regexp_exec(c, rx, subject);
        if (result.is_undefined()) { return value::undefined(); }
        const context::rooted keep_result{c, result};
        const value current = c.lookup_property(rx, "lastIndex");
        if (watch.threw()) { return value::undefined(); }
        if (!current.strict_equals(previous) ||
            (current.is_number() && previous.is_number() &&
             std::signbit(current.as_number()) != std::signbit(previous.as_number()))) {
            c.clear_store_rejected();
            c.store_property(rx, "lastIndex", previous);
            if (watch.threw()) { return value::undefined(); }
            c.strict_store_check("lastIndex");
            if (watch.threw()) { return value::undefined(); }
        }
        if (result.is_null()) { return value::number(-1); }
        const value index = c.lookup_property(result, "index");
        return watch.threw() ? value::undefined() : index;
    });

    // 22.2.6.14 [@@split](string, limit): a sticky species copy walks the
    // subject one position at a time, which is what makes an empty match and
    // a lookahead at a boundary come out the way they do.
    symbol_method("split", 2, [](context & c, std::span<value> a) -> value {
        const value rx = c.current_this();
        if (!object_this(c, rx, "RegExp.prototype[Symbol.split]")) { return value::undefined(); }
        if (!stringable_arg(c, arg_at(a, 0))) { return value::undefined(); }
        const detail::unwind_watch watch{c};
        const std::string s = c.to_string(arg_at(a, 0));
        if (watch.threw()) { return value::undefined(); }
        const value subject = c.string(s);
        const context::rooted keep{c, subject};
        const auto & str = *static_cast<const string_object *>(subject.as_heap());
        const value ctor = species_constructor(c, rx, c.global("RegExp"));
        if (ctor.is_undefined()) { return value::undefined(); }
        std::string flags;
        if (!read_flags(c, rx, flags)) { return value::undefined(); }
        const bool full_unicode =
            flags.find('u') != std::string::npos || flags.find('v') != std::string::npos;
        if (flags.find('y') == std::string::npos) { flags += 'y'; }
        const value new_flags = c.string(flags);
        const context::rooted keep_flags{c, new_flags};
        const value args[2] = {rx, new_flags};
        const value splitter = c.construct(ctor, args);
        if (watch.threw()) { return value::undefined(); }
        const context::rooted keep_splitter{c, splitter};
        value out = c.make_array();
        const context::rooted keep_out{c, out};
        auto * parts = static_cast<array_object *>(out.as_heap());
        const value limit = arg_at(a, 1);
        if (!numeric_arg(c, limit)) { return value::undefined(); }
        // ToUint32(limit) (22.2.6.14 step 13): undefined is 2^32 - 1.
        double lim = 4294967295.0;
        if (!limit.is_undefined()) {
            lim = static_cast<double>(number_to_uint32(c.to_number_value(limit)));
        }
        if (watch.threw()) { return value::undefined(); }
        if (lim == 0) { return out; }
        // p, q and e are unit positions (22.2.6.14 steps 19-25).
        const std::size_t size = str.length();
        if (size == 0) {
            const value z = regexp_exec(c, splitter, subject);
            if (z.is_undefined()) { return value::undefined(); }
            if (!z.is_null()) { return out; }
            parts->items.push_back(subject);
            return out;
        }
        std::size_t p = 0;
        std::size_t q = p;
        while (q < size) {
            if (!set_last_index(c, splitter, static_cast<double>(q))) { return value::undefined(); }
            const value z = regexp_exec(c, splitter, subject);
            if (z.is_undefined()) { return value::undefined(); }
            if (z.is_null()) {
                q = advance_string_index(str, q, full_unicode);
                continue;
            }
            double end = 0;
            if (!get_last_index(c, splitter, end)) { return value::undefined(); }
            const std::size_t e = std::min(static_cast<std::size_t>(end), size);
            if (e == p) {
                q = advance_string_index(str, q, full_unicode);
                continue;
            }
            parts->items.push_back(c.string(str.slice(p, q)));
            if (static_cast<double>(parts->items.size()) == lim) { return out; }
            p = e;
            const double captures = detail::array_like_length(c, z);
            if (watch.threw()) { return value::undefined(); }
            const std::size_t n_captures =
                captures > 1 ? static_cast<std::size_t>(captures - 1) : 0;
            for (std::size_t i = 1; i <= n_captures; ++i) {
                const value cap = c.lookup_index(z, value::number(static_cast<double>(i)));
                if (watch.threw()) { return value::undefined(); }
                parts->items.push_back(cap);
                if (static_cast<double>(parts->items.size()) == lim) { return out; }
            }
            q = p;
        }
        parts->items.push_back(c.string(str.slice(p, size)));
        return out;
    });
}

} // namespace ctbrowser::script::builtins_detail
