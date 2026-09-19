#include "execution.hpp"

namespace ctbrowser::script::builtins_detail {

namespace regexp_detail {

// The compiled program lives in a cache keyed by `source\0flags`, so the same
// literal in a loop compiles once. Shared by every native this file installs
// and dies with them.

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
        const value indices = cx.make_array();
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

} // namespace regexp_detail

} // namespace ctbrowser::script::builtins_detail
