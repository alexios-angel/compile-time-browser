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

namespace ctbrowser::script::builtins_detail {

namespace {

// The compiled program lives in a cache keyed by `source\0flags`, so the same
// literal in a loop compiles once. Shared by every native this file installs
// and dies with them.
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

// [[OriginalSource]] and [[OriginalFlags]]. Private keys, which no source text
// can spell and no enumeration reports (value.hpp, private_key_prefix).
constexpr std::string_view slot_source = "@#RegExpSource";
constexpr std::string_view slot_flags = "@#RegExpFlags";

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
// a line terminator is spelled as its escape.
[[nodiscard]] std::string escape_pattern(const std::string & source) {
    if (source.empty()) { return "(?:)"; }
    std::string out;
    bool in_class = false;
    for (std::size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        if (c == '\\' && i + 1 < source.size()) {
            out += c;
            out += source[++i];
            continue;
        }
        if (c == '[') { in_class = true; }
        if (c == ']') { in_class = false; }
        if (c == '/' && !in_class) {
            out += "\\/";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out += c;
        }
    }
    return out;
}

// The array `exec` returns: the whole match, then each group, with `index`,
// `input`, `groups` and - under `d` - `indices` alongside (22.2.7.2 steps
// 19-36). ctjs's wrapper dropped the positions; they were always in the match.
[[nodiscard]] value exec_result(context & cx, const rx::rx_prog & program, const std::string & s,
                                const rx::rx_match & m) {
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
    arr->index = value::number(static_cast<double>(m.begin));
    arr->input = cx.string(s);
    if (program.indices) {
        // MakeMatchIndicesIndexPairArray, 22.2.7.8: `[start, end]` per group,
        // undefined for one that did not participate, and its own `groups`.
        value indices = cx.make_array();
        arr->named_table().set("indices", indices); // rooted through the result
        auto * list = static_cast<array_object *>(indices.as_heap());
        const auto pair = [&](std::ptrdiff_t from, std::ptrdiff_t to) {
            if (from < 0) { return value::undefined(); }
            value p = cx.make_array();
            auto * xy = static_cast<array_object *>(p.as_heap());
            xy->items.push_back(value::number(static_cast<double>(from)));
            xy->items.push_back(value::number(static_cast<double>(to)));
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

// RegExpBuiltinExec, 22.2.7.2. `lastIndex` is read through [[Get]] and
// ToLength - a page may have written anything into it - and written back
// through Set with Throw=true, so a frozen pattern refuses with a TypeError.
[[nodiscard]] value builtin_exec(context & cx, const std::shared_ptr<regex_cache> & cache, value rx,
                                 const std::string & subject) {
    std::string source;
    std::string flags;
    if (!regexp_slots(rx, source, flags)) { return value::null(); }
    const std::shared_ptr<rx::rx_prog> program = compiled(cache, source, flags);
    double last = 0;
    if (!get_last_index(cx, rx, last)) { return value::undefined(); }
    const bool stateful = program->global || program->sticky;
    if (!stateful) { last = 0; }
    rx::rx_match m;
    const bool hit = program->ok && last <= static_cast<double>(subject.size()) &&
                     rx::rx_search(*program, subject, static_cast<std::size_t>(last), m) &&
                     (!program->sticky || m.begin == static_cast<std::size_t>(last));
    if (!hit) {
        if (stateful && !set_last_index(cx, rx, 0)) { return value::undefined(); }
        return value::null();
    }
    if (stateful && !set_last_index(cx, rx, static_cast<double>(m.end))) {
        return value::undefined();
    }
    return exec_result(cx, *program, subject, m);
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
    const value species = cx.lookup_property(ctor, "@@species");
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
    value * matcher = it->find("@#IteratingRegExp");
    value * subject = it->find("@#IteratedString");
    if (matcher == nullptr || subject == nullptr) {
        cx.throw_error("TypeError",
                       "next called on an object that is not a RegExp String Iterator");
        return value::undefined();
    }
    if (value * done = it->find("@#Done"); done != nullptr && done->as_boolean()) {
        return finish(value::undefined(), true);
    }
    const bool global = context::truthy(*it->find("@#Global"));
    const bool full_unicode = context::truthy(*it->find("@#FullUnicode"));
    const value rx = *matcher;
    const std::string s = cx.to_string(*subject);
    const value match = regexp_exec(cx, rx, *subject);
    if (match.is_undefined()) { return value::undefined(); }
    if (match.is_null()) {
        it->define("@#Done", value::boolean(true), attr_none);
        return finish(value::undefined(), true);
    }
    if (!global) {
        it->define("@#Done", value::boolean(true), attr_none);
        return finish(match, false);
    }
    std::string text;
    if (!matched_text(cx, match, text)) { return value::undefined(); }
    if (text.empty()) {
        double this_index = 0;
        if (!get_last_index(cx, rx, this_index)) { return value::undefined(); }
        const std::size_t next =
            advance_string_index(s, static_cast<std::size_t>(this_index), full_unicode);
        if (!set_last_index(cx, rx, static_cast<double>(next))) { return value::undefined(); }
    }
    return finish(match, false);
}

} // namespace

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

bool is_regexp(context & cx, value v, bool & out) {
    out = false;
    if (!v.is_object_like()) { return true; }
    const detail::unwind_watch watch{cx};
    const value matcher = cx.lookup_property(v, "@@match");
    if (watch.threw()) { return false; }
    out = matcher.is_undefined() ? has_regexp_matcher(v) : context::truthy(matcher);
    return true;
}

std::size_t advance_string_index(const std::string & s, std::size_t index, bool full_unicode) {
    if (!full_unicode || index + 1 >= s.size()) { return index + 1; }
    std::size_t next = index + 1;
    while (next < s.size() && (static_cast<unsigned char>(s[next]) & 0xC0u) == 0x80u) { ++next; }
    return next;
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
    // The built-in matcher IS %RegExp.prototype.exec%; calling it through the
    // prototype keeps one implementation.
    const value builtin =
        cx.lookup_property(value::object(cx.prototype(context::proto_kind::regexp)), "exec");
    if (!builtin.is_callable()) { return value::null(); }
    const value result = cx.call(builtin, std::span<const value>{&subject, 1}, rx);
    return watch.threw() ? value::undefined() : result;
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
    auto cache = std::make_shared<regex_cache>();

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
        value list = c.make_array();
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
        value replace_value = arg_at(a, 1);
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
        value results = c.make_array();
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
