// ctbrowser.script builtins - RegExp.
//
// One of four files carved out of a 1,333-line builtins/text.cpp on 2026-09-08
// - which was itself one of five carved out of builtins.cpp on 2026-08-09.
// Everything shared - the argument helpers, namespace detail, and these
// functions' declarations - is in ../internal.hpp; what RegExp and String
// share between them is in internal.hpp beside this.
//
// REWRITTEN TO 22.2 ON 2026-09-12. Before, a RegExp was an ordinary object
// with `source`, `flags` and the four mode booleans as OWN data properties,
// `exec`, `test` and `toString` on a prototype, and nothing else: no
// @@replace/@@split/@@match/@@matchAll/@@search, so every String method
// carried its own regex loop, and no accessors, so `Object.keys(/x/)` was
// six names and a subclass could not override anything. test262's
// built-ins/String rows named the cost - `"x".split(/^/)`, `$<name>` in a
// replacement, `matchAll` on a pattern whose prototype lost its @@matchAll -
// and every one of them is one algorithm that the specification writes ONCE,
// on RegExp.prototype, and has String.prototype invoke.
//
// So: an instance carries two internal slots ([[OriginalSource]] and
// [[OriginalFlags]], as private-keyed properties) and `lastIndex`; everything
// else is an accessor or a method on the prototype, and the string methods in
// string.cpp end in `Invoke(rx, @@method)` exactly as 22.1.3 does.

#include "internal.hpp"

#include <map>

#include "regexp/execution.hpp"

namespace ctbrowser::script::builtins_detail {

using namespace regexp_detail;

bool is_regexp(context & cx, value v, bool & out) {
    out = false;
    if (!v.is_object_like()) { return true; }
    const detail::unwind_watch watch{cx};
    const value matcher = cx.lookup_property(v, "@@match");
    if (watch.threw()) { return false; }
    out = matcher.is_undefined() ? has_regexp_matcher(v) : context::truthy(matcher);
    return true;
}

bool get_substitution(context & cx, const std::string & matched, const std::string & subject,
                      std::size_t position, std::span<const value> captures, value named_captures,
                      const std::string & tpl, std::string & out) {
    out.clear();
    const std::size_t tail = std::min(subject.size(), position + matched.size());
    for (std::size_t i = 0; i < tpl.size(); ++i) {
        const char c = tpl[i];
        if (c != '$' || i + 1 >= tpl.size()) {
            out += c;
            continue;
        }
        const char next = tpl[i + 1];
        if (next == '$') {
            out += '$';
            ++i;
        } else if (next == '&') {
            out += matched;
            ++i;
        } else if (next == '`') {
            out += subject.substr(0, position);
            ++i;
        } else if (next == '\'') {
            out += subject.substr(tail);
            ++i;
        } else if (next >= '0' && next <= '9') {
            // `$nn` when two digits name a group that exists, else `$n`, else
            // the text itself (22.1.3.19.1 steps 9-11 of the digit arm).
            std::size_t digits = 1;
            std::size_t index = static_cast<std::size_t>(next - '0');
            if (i + 2 < tpl.size() && tpl[i + 2] >= '0' && tpl[i + 2] <= '9') {
                const std::size_t wide = index * 10 + static_cast<std::size_t>(tpl[i + 2] - '0');
                if (wide >= 1 && wide <= captures.size()) {
                    digits = 2;
                    index = wide;
                }
            }
            if (index >= 1 && index <= captures.size()) {
                const value cap = captures[index - 1];
                if (!cap.is_undefined()) {
                    if (!stringable_arg(cx, cap)) { return false; }
                    out += cx.to_string(cap);
                }
            } else {
                out += tpl.substr(i, 1 + digits);
            }
            i += digits;
        } else if (next == '<') {
            // `$<name>` reads the groups object; with no groups object the
            // three characters are literal, and so are they when no `>` closes.
            const std::size_t close = tpl.find('>', i + 2);
            if (named_captures.is_undefined() || close == std::string::npos) {
                out += "$<";
                ++i;
                continue;
            }
            const std::string name = tpl.substr(i + 2, close - (i + 2));
            const detail::unwind_watch watch{cx};
            const value cap = cx.lookup_property(named_captures, name);
            if (watch.threw()) { return false; }
            if (!cap.is_undefined()) {
                if (!stringable_arg(cx, cap)) { return false; }
                out += cx.to_string(cap);
                if (watch.threw()) { return false; }
            }
            i = close;
        } else {
            out += c;
        }
    }
    return true;
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

    // RegExpInitialize, 22.2.3.3, onto an object that already has the shape
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
        // 22.2.3.4 step 4-6: a pattern that is not a Pattern, or flags that
        // are not flags, is a SyntaxError - judged by the same early-error
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
    // configurable: false } (22.2.3.2 step 2), defined BEFORE the pattern is
    // parsed so that a SyntaxError leaves the shape RegExpAlloc made.
    const auto alloc = [](context & c) {
        value obj = c.make_object();
        auto * o = static_cast<object_object *>(obj.as_heap());
        if (object_object * table = c.prototype(context::proto_kind::regexp)) {
            o->prototype = value::object(table);
        }
        o->define("lastIndex", value::number(0), attr_writable);
        return obj;
    };

    // RegExpCreate, 22.2.3.1 - RegExpAlloc then RegExpInitialize, with NONE of
    // the constructor's IsRegExp reading. It is what a regex LITERAL compiles
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
        // constructor is RegExp - the one case that does not allocate.
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
            // Step 6: something that CLAIMS to be one is read through [[Get]].
            p = c.lookup_property(pattern, "source");
            if (watch.threw()) { return value::undefined(); }
            if (flags.is_undefined()) {
                f = c.lookup_property(pattern, "flags");
                if (watch.threw()) { return value::undefined(); }
            }
        }
        const context::rooted keep_p{c, p};
        const context::rooted keep_f{c, f};
        // RegExpAlloc: the instance `new` made - whose prototype came from
        // new.target, so a subclass instance is already on its own prototype
        // - or a fresh one on RegExp.prototype for a plain call.
        const value obj = constructing ? self : alloc(c);
        const context::rooted keep_obj{c, obj};
        if (constructing) {
            // context::make_instance leaves a NATIVE constructor's instance
            // with no prototype (a subclass instance arrives with its own):
            // RegExp.prototype unless something already chose otherwise.
            auto * o = static_cast<object_object *>(obj.as_heap());
            if (!o->prototype.is_object()) {
                if (object_object * table = c.prototype(context::proto_kind::regexp)) {
                    o->prototype = value::object(table);
                }
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
        regexp_ctor->define_accessor("@@species", value::object(species), value::undefined(),
                                     attr_configurable);
    }

    // 22.2.5.1 RegExp.escape(S), ES2025: a string that matches S literally.
    // The first character is hex-escaped when it is alphanumeric (so the
    // result never continues a preceding `\1` or `\u`), a syntax character
    // and `/` are backslashed, the five control letters use their letter, and
    // the other punctuators, white space, line terminators and lone surrogates
    // become \xHH or \uHHHH. Everything else - including every non-ASCII code
    // point - passes through as itself; the subject is UTF-8 here so a code
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

    // --- the accessors, 22.2.6.3-6.19 ---------------------------------------
    //
    // Each of the flag getters reads its slot; a receiver without the slots is
    // a TypeError UNLESS it is RegExp.prototype itself, which answers undefined
    // (the prototype is not a RegExp, and the specification says so twice per
    // accessor). `source` answers "(?:)" for the prototype and `flags` is
    // GENERIC: it reads the eight booleans through [[Get]], so a subclass that
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

    // --- exec, test, toString, compile --------------------------------------
    method(cx, regexp_proto, "exec", 1, [cache](context & c, std::span<value> a) -> value {
        if (this_regexp(c, "RegExp.prototype.exec") == nullptr) { return value::undefined(); }
        if (!stringable_arg(c, arg_at(a, 0))) { return value::undefined(); }
        const value self = c.current_this();
        const detail::unwind_watch watch{c};
        const std::string subject = c.to_string(arg_at(a, 0));
        if (watch.threw()) { return value::undefined(); }
        return builtin_exec(c, cache, self, subject);
    });
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
    // 22.2.6.17: GENERIC - "/" + ToString(source) + "/" + ToString(flags)
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
        if (this_regexp(c, "RegExp.prototype.compile") == nullptr) { return value::undefined(); }
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

    // --- the Symbol methods -------------------------------------------------
    //
    // Named "[Symbol.match]" and so on (their `name` properties), installed
    // under the `@@` keys the symbol machinery uses (value.hpp).
    const auto symbol_method = [&](const char * key, const char * name, double arity,
                                   native_fn fn) {
        auto * made = detail::method_native(cx, name, std::move(fn));
        detail::install_arity(cx, made, arity);
        regexp_proto->define(key, value::object(made), attr_builtin);
    };

    // 22.2.6.8 [@@match](string)
    symbol_method("@@match", "[Symbol.match]", 1, [](context & c, std::span<value> a) -> value {
        const value rx = c.current_this();
        if (!object_this(c, rx, "RegExp.prototype[Symbol.match]")) { return value::undefined(); }
        if (!stringable_arg(c, arg_at(a, 0))) { return value::undefined(); }
        const detail::unwind_watch watch{c};
        const std::string s = c.to_string(arg_at(a, 0));
        if (watch.threw()) { return value::undefined(); }
        const value subject = c.string(s);
        const context::rooted keep{c, subject};
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
                    advance_string_index(s, static_cast<std::size_t>(this_index), full_unicode);
                if (!set_last_index(c, rx, static_cast<double>(next))) {
                    return value::undefined();
                }
            }
        }
    });

    // 22.2.6.9 [@@matchAll](string): a %RegExpStringIterator% over a SPECIES
    // COPY of the receiver, so the caller's lastIndex is untouched.
    object_object * iterator_proto = new_table(cx);
    if (object_object * iterator_base = cx.prototype(context::proto_kind::generator);
        iterator_base != nullptr && iterator_base->prototype.is_object()) {
        iterator_proto->prototype = iterator_base->prototype; // %IteratorPrototype%
    }
    method(cx, iterator_proto, "next", 0, regexp_string_iterator_next);
    iterator_proto->define("@@toStringTag", cx.string("RegExp String Iterator"), attr_configurable);
    symbol_method("@@matchAll", "[Symbol.matchAll]", 1,
                  [iterator_proto](context & c, std::span<value> a) -> value {
                      const value rx = c.current_this();
                      if (!object_this(c, rx, "RegExp.prototype[Symbol.matchAll]")) {
                          return value::undefined();
                      }
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
                      value made = c.make_object();
                      auto * it = static_cast<object_object *>(made.as_heap());
                      it->prototype = value::object(iterator_proto);
                      it->define("@#IteratingRegExp", matcher, attr_none);
                      it->define("@#IteratedString", subject, attr_none);
                      it->define("@#Global", value::boolean(flags.find('g') != std::string::npos),
                                 attr_none);
                      it->define("@#FullUnicode",
                                 value::boolean(flags.find('u') != std::string::npos ||
                                                flags.find('v') != std::string::npos),
                                 attr_none);
                      it->define("@#Done", value::boolean(false), attr_none);
                      return made;
                  });
    method(cx, iterator_proto, "@@iterator", 0,
           [](context & c, std::span<value>) { return c.current_this(); });

    // 22.2.6.11 [@@replace](string, replaceValue)
    symbol_method("@@replace", "[Symbol.replace]", 2, [](context & c, std::span<value> a) -> value {
        const value rx = c.current_this();
        if (!object_this(c, rx, "RegExp.prototype[Symbol.replace]")) { return value::undefined(); }
        if (!stringable_arg(c, arg_at(a, 0))) { return value::undefined(); }
        const detail::unwind_watch watch{c};
        const std::string s = c.to_string(arg_at(a, 0));
        if (watch.threw()) { return value::undefined(); }
        const value subject = c.string(s);
        const context::rooted keep{c, subject};
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
        // Every match is collected FIRST (steps 11-12), then the replacements
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
                    advance_string_index(s, static_cast<std::size_t>(this_index), full_unicode);
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
            position = std::clamp(position, 0.0, static_cast<double>(s.size()));
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
                accumulated += s.substr(next_source_position, at - next_source_position);
                accumulated += replacement;
                next_source_position = at + matched.size();
            }
        }
        if (next_source_position < s.size()) { accumulated += s.substr(next_source_position); }
        return c.string(accumulated);
    });

    // 22.2.6.12 [@@search](string): lastIndex is put back however it was.
    symbol_method("@@search", "[Symbol.search]", 1, [](context & c, std::span<value> a) -> value {
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

    // 22.2.6.14 [@@split](string, limit): a STICKY species copy walks the
    // subject one position at a time, which is what makes an empty match and
    // a lookahead at a boundary come out the way they do.
    symbol_method("@@split", "[Symbol.split]", 2, [](context & c, std::span<value> a) -> value {
        const value rx = c.current_this();
        if (!object_this(c, rx, "RegExp.prototype[Symbol.split]")) { return value::undefined(); }
        if (!stringable_arg(c, arg_at(a, 0))) { return value::undefined(); }
        const detail::unwind_watch watch{c};
        const std::string s = c.to_string(arg_at(a, 0));
        if (watch.threw()) { return value::undefined(); }
        const value subject = c.string(s);
        const context::rooted keep{c, subject};
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
        // ToUint32(limit), 22.2.6.14 step 13: undefined is 2^32 - 1.
        double lim = 4294967295.0;
        if (!limit.is_undefined()) {
            const double n = c.to_number_value(limit);
            lim = std::isfinite(n)
                      ? static_cast<double>(static_cast<std::uint32_t>(
                            static_cast<std::int64_t>(std::fmod(std::trunc(n), 4294967296.0))))
                      : 0.0;
        }
        if (watch.threw()) { return value::undefined(); }
        if (lim == 0) { return out; }
        const std::size_t size = s.size();
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
                q = advance_string_index(s, q, full_unicode);
                continue;
            }
            double end = 0;
            if (!get_last_index(c, splitter, end)) { return value::undefined(); }
            const std::size_t e = std::min(static_cast<std::size_t>(end), size);
            if (e == p) {
                q = advance_string_index(s, q, full_unicode);
                continue;
            }
            parts->items.push_back(c.string(s.substr(p, q - p)));
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
        parts->items.push_back(c.string(s.substr(p)));
        return out;
    });
}

} // namespace ctbrowser::script::builtins_detail
