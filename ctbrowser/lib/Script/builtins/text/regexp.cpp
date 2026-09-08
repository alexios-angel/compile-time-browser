// ctbrowser.script builtins - RegExp.
//
// One of four files carved out of a 1,333-line builtins/text.cpp on 2026-09-08
// - which was itself one of five carved out of builtins.cpp on 2026-08-09.
// Everything shared - the argument helpers, namespace detail, and these
// functions' declarations - is in ../internal.hpp; nothing is shared between
// these four alone, so there is no second header.

#include "../internal.hpp"

namespace ctbrowser::script::builtins_detail {

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

} // namespace ctbrowser::script::builtins_detail
