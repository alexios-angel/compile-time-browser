// `URL` and `URLSearchParams` - the URL Standard §6, as WebIDL-shaped bindings
// over shell/net/url.hpp. Nothing here parses: a URL object is its serialised
// record in a private slot and every getter re-parses it, every setter parses,
// applies the §6.1 setter steps and serialises back. The record round-trips
// through its serialisation by construction, URL parsing is on no profile, and
// it is what keeps this file thin and the parser testable without a VM.
//
// A URLSearchParams is likewise a VIEW: attached to a URL it reads that URL's
// query on every call and writes it back (§6.2 "update steps"), so the two
// cannot disagree and `url.search = ...` needs no hook to reach the params;
// standalone it keeps its list as the serialised form, which
// application/x-www-form-urlencoded round-trips exactly.

#include "internal.hpp"

#include <ctbrowser/shell/net/url.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace ctbrowser::shell {

namespace {

// The private slots - the device bindings/exceptions.cpp and the CSSOM use:
// this engine has no internal-slot mechanism, so state lives under names no
// author writes, non-enumerable and non-configurable.
constexpr std::string_view href_key = "__ctbrowser_url_href";
constexpr std::string_view params_key = "__ctbrowser_url_params";
constexpr std::string_view list_key = "__ctbrowser_params_list";
constexpr std::string_view owner_key = "__ctbrowser_params_url";
constexpr std::string_view iter_target_key = "__ctbrowser_params_iter";
constexpr std::string_view iter_kind_key = "__ctbrowser_params_iter_kind";
constexpr std::string_view iter_at_key = "__ctbrowser_params_iter_at";

[[nodiscard]] script::object_object * as_object(value v) {
    return v.is_object() ? static_cast<script::object_object *>(v.as_heap()) : nullptr;
}

// The receiver, branded: `this` must carry the slot or the call is the
// TypeError WebIDL gives a method invoked on the wrong kind of object.
[[nodiscard]] script::object_object * branded_this(context & c, std::string_view slot,
                                                   const char * interface) {
    script::object_object * self = as_object(c.current_this());
    if (self == nullptr || self->find(slot) == nullptr) {
        c.throw_error("TypeError", std::string{interface} + ": illegal invocation");
        return nullptr;
    }
    return self;
}

// WebIDL USVString: ToString, then lone surrogates to U+FFFD.
[[nodiscard]] std::string usv_arg(context & c, std::span<value> a, std::size_t i) {
    return to_usv_string(c.to_string(i < a.size() ? a[i] : value::undefined()));
}

// --- URL: the record behind the slot ----------------------------------------------

[[nodiscard]] url_record url_of(script::object_object & self) {
    const value * held = self.find(href_key);
    if (held == nullptr || !held->is_string()) { return {}; }
    return parse_url(static_cast<script::string_object *>(held->as_heap())->text)
        .value_or(url_record{});
}

void store_url(context & c, script::object_object & self, const url_record & url) {
    self.define(href_key, c.string(url.serialize()), script::attr_none);
}

// --- URLSearchParams: the list behind the slot ----------------------------------------

[[nodiscard]] form_pairs list_of(script::object_object & params) {
    if (const value * owner = params.find(owner_key); owner != nullptr) {
        if (script::object_object * url = as_object(*owner)) {
            return parse_form_urlencoded(url_of(*url).query.value_or(std::string{}));
        }
    }
    const value * held = params.find(list_key);
    if (held == nullptr || !held->is_string()) { return {}; }
    return parse_form_urlencoded(static_cast<script::string_object *>(held->as_heap())->text);
}

// §6.2 "update steps": the serialised list back into the URL's query, or
// null when it is empty - which is what makes deleting the last parameter
// take the `?` with it.
void store_list(context & c, script::object_object & params, const form_pairs & list) {
    const std::string serialised = serialize_form_urlencoded(list);
    if (const value * owner = params.find(owner_key); owner != nullptr) {
        if (script::object_object * holder = as_object(*owner)) {
            url_record url = url_of(*holder);
            url.query = list.empty() ? std::nullopt : std::optional<std::string>{serialised};
            store_url(c, *holder, url);
        }
        return;
    }
    params.define(list_key, c.string(serialised), script::attr_none);
}

// A name, in the UTF-16 code units `sort()` is defined to order by - which is
// not byte order once a supplementary character meets one above U+E000.
[[nodiscard]] std::vector<char16_t> code_units(std::string_view text) {
    std::vector<char16_t> out;
    for (std::size_t at = 0; at < text.size();) {
        const char32_t cp = decode_utf8(text, at);
        if (cp >= 0x10000) {
            out.push_back(static_cast<char16_t>(0xD800 + ((cp - 0x10000) >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + ((cp - 0x10000) & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

// One [name, value] pair as the array `entries()` yields.
[[nodiscard]] value pair_value(context & c, const std::pair<std::string, std::string> & pair) {
    const value out = c.make_array();
    auto * items = static_cast<script::array_object *>(out.as_heap());
    items->items.push_back(c.string(pair.first));
    items->items.push_back(c.string(pair.second));
    return out;
}

// The interface's constructor and prototype, wired the WebIDL way: `prototype`
// on the constructor, `constructor` and `@@toStringTag` on the prototype, and
// the constructor a global.
struct interface_pair {
    script::native_object * ctor;
    script::object_object * proto;
};

[[nodiscard]] interface_pair make_interface(context & cx, const char * name,
                                            script::native_fn construct) {
    auto * proto = cx.allocate<script::object_object>();
    auto * ctor = cx.allocate<script::native_object>(name, std::move(construct));
    ctor->define("prototype", value::object(proto), script::attr_none);
    proto->define("constructor", value::object(ctor), script::attr_builtin);
    proto->define("@@toStringTag", cx.string(name), script::attr_configurable);
    cx.define_global(name, value::object(ctor));
    return {ctor, proto};
}

// --- URLSearchParams -------------------------------------------------------------

// The iterator `entries()`, `keys()`, `values()` and `@@iterator` return: LIVE,
// an index into the list re-read on every `next()`, which is what the standard's
// value pair iterator is and what the suite's delete-during-iteration cases
// ask. (for-of materialises through iterable_values first - docs/script.md -
// so a mutation inside the loop body is seen by a hand-driven `next()` and not
// by the loop; that is the VM's rule, written down there.)
[[nodiscard]] value make_params_iterator(context & c, value params, const char * kind,
                                         value proto) {
    auto * made = c.allocate<script::object_object>();
    made->prototype = proto;
    made->define(iter_target_key, params, script::attr_none);
    made->define(iter_kind_key, c.string(kind), script::attr_none);
    made->define(iter_at_key, value::number(0), script::attr_none);
    return value::object(made);
}

[[nodiscard]] script::object_object * make_params_iterator_prototype(context & cx) {
    auto * proto = cx.allocate<script::object_object>();
    if (const value iterator = cx.global("Iterator"); iterator.is_object_like()) {
        const value base = cx.lookup_property(iterator, "prototype");
        if (base.is_object()) { proto->prototype = base; }
    }
    proto->define("@@toStringTag", cx.string("URLSearchParams Iterator"),
                  script::attr_configurable);
    set_method(
        cx, *proto, "next",
        [](context & c, std::span<value>) {
            script::object_object * self = as_object(c.current_this());
            auto * result = static_cast<script::object_object *>(c.make_object().as_heap());
            result->set("done", value::boolean(true));
            result->set("value", value::undefined());
            if (self == nullptr) { return value::object(result); }
            const value * target = self->find(iter_target_key);
            const value * kind = self->find(iter_kind_key);
            const value * at = self->find(iter_at_key);
            script::object_object * params = target == nullptr ? nullptr : as_object(*target);
            if (params == nullptr || kind == nullptr || at == nullptr) {
                return value::object(result);
            }
            const form_pairs list = list_of(*params);
            const auto index = static_cast<std::size_t>(std::max(0.0, context::to_number(*at)));
            if (index >= list.size()) { return value::object(result); }
            self->define(iter_at_key, value::number(static_cast<double>(index + 1)),
                         script::attr_none);
            const std::string which = c.to_string(*kind);
            result->set("done", value::boolean(false));
            result->set("value", which == "keys"     ? c.string(list[index].first)
                                 : which == "values" ? c.string(list[index].second)
                                                     : pair_value(c, list[index]));
            return value::object(result);
        },
        script::attr_builtin);
    set_method(
        cx, *proto, "@@iterator", [](context & c, std::span<value>) { return c.current_this(); },
        script::attr_builtin);
    return proto;
}

void install_search_params(context & cx, const value iterator_proto) {
    const interface_pair made = make_interface(cx, "URLSearchParams", {});
    auto * ctor = made.ctor;
    // §6.2 "new URLSearchParams(init)": a sequence of pairs, a record, or a
    // string with one leading `?` dropped - the WebIDL union, told apart the
    // way WebIDL tells it: an object with @@iterator is the sequence, any
    // other object the record, and everything else is stringified.
    ctor->fn = [ctor](context & c, std::span<value> a) {
        const value self_value = c.current_this();
        script::object_object * self = as_object(self_value);
        if (self == nullptr || !c.instance_of(self_value, value::object(ctor))) {
            c.throw_error("TypeError", "URLSearchParams constructor: 'new' is required");
            return value::undefined();
        }
        const value init = arg(a, 0);
        form_pairs list;
        if (init.is_object_like()) {
            const context::rooted keep{c, init};
            if (const value method = c.lookup_property(init, "@@iterator"); method.is_callable()) {
                const value items = c.iterable_values(init);
                if (c.throw_pending()) { return value::undefined(); }
                const context::rooted keep_items{c, items};
                for (const value & item :
                     static_cast<script::array_object *>(items.as_heap())->items) {
                    const value pair = c.iterable_values(item);
                    if (c.throw_pending()) { return value::undefined(); }
                    const auto & parts = static_cast<script::array_object *>(pair.as_heap())->items;
                    if (parts.size() != 2) {
                        c.throw_error("TypeError",
                                      "URLSearchParams constructor: each pair must have exactly "
                                      "two elements");
                        return value::undefined();
                    }
                    list.emplace_back(to_usv_string(c.to_string(parts[0])),
                                      to_usv_string(c.to_string(parts[1])));
                    if (c.throw_pending()) { return value::undefined(); }
                }
            } else if (c.throw_pending()) {
                return value::undefined();
            } else {
                // WebIDL record<USVString, USVString>: the own enumerable string
                // keys in order, each converted, a repeated converted key
                // (two lone surrogates both becoming U+FFFD) updating in place.
                const value keys = c.own_keys(init);
                const context::rooted keep_keys{c, keys};
                for (const value & key :
                     static_cast<script::array_object *>(keys.as_heap())->items) {
                    const std::string name = to_usv_string(c.to_string(key));
                    const value held = c.lookup_property(init, c.to_string(key));
                    if (c.throw_pending()) { return value::undefined(); }
                    const std::string text = to_usv_string(c.to_string(held));
                    if (c.throw_pending()) { return value::undefined(); }
                    const auto found = std::find_if(
                        list.begin(), list.end(), [&](const auto & p) { return p.first == name; });
                    if (found != list.end()) {
                        found->second = text;
                    } else {
                        list.emplace_back(name, text);
                    }
                }
            }
        } else {
            std::string text =
                init.is_undefined() ? std::string{} : to_usv_string(c.to_string(init));
            if (c.throw_pending()) { return value::undefined(); }
            if (!text.empty() && text.front() == '?') { text.erase(0, 1); }
            list = parse_form_urlencoded(text);
        }
        self->define(list_key, c.string(serialize_form_urlencoded(list)), script::attr_none);
        return self_value;
    };

    const auto method = [&](const char * name, script::native_fn fn) {
        set_method(cx, *made.proto, name, std::move(fn), script::attr_builtin);
    };
    // Every operation reads the live list, edits it, and writes it back
    // through the update steps.
    method("append", [](context & c, std::span<value> a) {
        script::object_object * self = branded_this(c, list_key, "URLSearchParams");
        if (self == nullptr) { return value::undefined(); }
        form_pairs list = list_of(*self);
        list.emplace_back(usv_arg(c, a, 0), usv_arg(c, a, 1));
        store_list(c, *self, list);
        return value::undefined();
    });
    method("delete", [](context & c, std::span<value> a) {
        script::object_object * self = branded_this(c, list_key, "URLSearchParams");
        if (self == nullptr) { return value::undefined(); }
        form_pairs list = list_of(*self);
        const std::string name = usv_arg(c, a, 0);
        const bool with_value = a.size() > 1 && !a[1].is_undefined();
        const std::string text = with_value ? usv_arg(c, a, 1) : std::string{};
        std::erase_if(list, [&](const auto & p) {
            return p.first == name && (!with_value || p.second == text);
        });
        store_list(c, *self, list);
        return value::undefined();
    });
    method("get", [](context & c, std::span<value> a) {
        script::object_object * self = branded_this(c, list_key, "URLSearchParams");
        if (self == nullptr) { return value::undefined(); }
        const std::string name = usv_arg(c, a, 0);
        for (const auto & [key, text] : list_of(*self)) {
            if (key == name) { return c.string(text); }
        }
        return value::null();
    });
    method("getAll", [](context & c, std::span<value> a) {
        script::object_object * self = branded_this(c, list_key, "URLSearchParams");
        if (self == nullptr) { return value::undefined(); }
        const std::string name = usv_arg(c, a, 0);
        const value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        for (const auto & [key, text] : list_of(*self)) {
            if (key == name) { items->items.push_back(c.string(text)); }
        }
        return out;
    });
    method("has", [](context & c, std::span<value> a) {
        script::object_object * self = branded_this(c, list_key, "URLSearchParams");
        if (self == nullptr) { return value::undefined(); }
        const std::string name = usv_arg(c, a, 0);
        const bool with_value = a.size() > 1 && !a[1].is_undefined();
        const std::string text = with_value ? usv_arg(c, a, 1) : std::string{};
        for (const auto & [key, held] : list_of(*self)) {
            if (key == name && (!with_value || held == text)) { return value::boolean(true); }
        }
        return value::boolean(false);
    });
    method("set", [](context & c, std::span<value> a) {
        script::object_object * self = branded_this(c, list_key, "URLSearchParams");
        if (self == nullptr) { return value::undefined(); }
        form_pairs list = list_of(*self);
        const std::string name = usv_arg(c, a, 0);
        const std::string text = usv_arg(c, a, 1);
        // The first match takes the value and the rest go; none appends.
        bool seen = false;
        std::erase_if(list, [&](auto & p) {
            if (p.first != name) { return false; }
            if (seen) { return true; }
            seen = true;
            p.second = text;
            return false;
        });
        if (!seen) { list.emplace_back(name, text); }
        store_list(c, *self, list);
        return value::undefined();
    });
    method("sort", [](context & c, std::span<value>) {
        script::object_object * self = branded_this(c, list_key, "URLSearchParams");
        if (self == nullptr) { return value::undefined(); }
        form_pairs list = list_of(*self);
        // STABLE, by the names' UTF-16 code units, as §6.2 says.
        std::stable_sort(list.begin(), list.end(), [](const auto & x, const auto & y) {
            return code_units(x.first) < code_units(y.first);
        });
        store_list(c, *self, list);
        return value::undefined();
    });
    method("toString", [](context & c, std::span<value>) {
        script::object_object * self = branded_this(c, list_key, "URLSearchParams");
        if (self == nullptr) { return value::undefined(); }
        return c.string(serialize_form_urlencoded(list_of(*self)));
    });
    method("forEach", [](context & c, std::span<value> a) {
        script::object_object * self = branded_this(c, list_key, "URLSearchParams");
        if (self == nullptr) { return value::undefined(); }
        const value callback = arg(a, 0);
        if (!callback.is_callable()) {
            c.throw_error("TypeError", "URLSearchParams.forEach: the callback is not a function");
            return value::undefined();
        }
        const value self_value = c.current_this();
        const value this_arg = arg(a, 1);
        const context::rooted keep_callback{c, callback};
        const context::rooted keep_this{c, this_arg};
        // Live: the list is re-read each step, so a callback that deletes
        // the next pair skips it, as the standard's iteration does.
        for (std::size_t i = 0;; ++i) {
            const form_pairs list = list_of(*self);
            if (i >= list.size()) { break; }
            const value args[3] = {c.string(list[i].second), c.string(list[i].first), self_value};
            (void)c.call(callback, args, this_arg);
            if (c.throw_pending()) { break; }
        }
        return value::undefined();
    });
    define_getter(cx, *made.proto, "size", [](context & c, std::span<value>) {
        script::object_object * self = branded_this(c, list_key, "URLSearchParams");
        if (self == nullptr) { return value::undefined(); }
        return value::number(static_cast<double>(list_of(*self).size()));
    });
    for (const char * kind : {"entries", "keys", "values"}) {
        method(kind, [kind, iterator_proto](context & c, std::span<value>) {
            if (branded_this(c, list_key, "URLSearchParams") == nullptr) {
                return value::undefined();
            }
            return make_params_iterator(c, c.current_this(), kind, iterator_proto);
        });
    }
    // `URLSearchParams.prototype[Symbol.iterator] === ...entries`, per WebIDL.
    if (const value * entries = made.proto->find("entries"); entries != nullptr) {
        made.proto->define("@@iterator", *entries, script::attr_builtin);
    }
    // THE ITERATOR PROTOTYPE IS A ROOT AND NOTHING ELSE POINTS AT IT. It lives
    // in the `entries`/`keys`/`values` lambdas' C++ captures, which a precise
    // collector cannot see (script::native_object::retained is the note), and
    // nothing in the heap graph refers to it until the first iterator is made.
    // So it was swept, `made->prototype` pointed at a reused cell, and every
    // `for (x of params)` died with "iterator.next is not a function" - the
    // whole of urlsearchparams-foreach and most of -constructor.
    made.ctor->retained.push_back(iterator_proto);
}

// --- URL -----------------------------------------------------------------------------

// The §6.1 getters and setters, one table: each reads the record and, for a
// setter, runs the setter steps and stores the result.
struct url_attribute {
    const char * name;
    std::string (*get)(const url_record &);
    std::optional<url_part> set; // nullopt: readonly (origin, searchParams)
};

constexpr url_attribute url_attributes[] = {
    {"href", [](const url_record & u) { return u.serialize(); }, url_part::href},
    {"origin", [](const url_record & u) { return u.origin(); }, std::nullopt},
    {"protocol", [](const url_record & u) { return u.protocol(); }, url_part::protocol},
    {"username", [](const url_record & u) { return u.username; }, url_part::username},
    {"password", [](const url_record & u) { return u.password; }, url_part::password},
    {"host", [](const url_record & u) { return u.host_and_port(); }, url_part::host},
    {"hostname", [](const url_record & u) { return u.hostname(); }, url_part::hostname},
    {"port", [](const url_record & u) { return u.port_text(); }, url_part::port},
    {"pathname", [](const url_record & u) { return u.pathname(); }, url_part::pathname},
    {"search", [](const url_record & u) { return u.search(); }, url_part::search},
    {"hash", [](const url_record & u) { return u.hash(); }, url_part::hash},
};

} // namespace

script::native_object * install_url(context & cx) {
    const value iterator_proto = value::object(make_params_iterator_prototype(cx));
    install_search_params(cx, iterator_proto);
    const value params_ctor = cx.global("URLSearchParams");

    const interface_pair made = make_interface(cx, "URL", {});
    auto * ctor = made.ctor;
    // Captured below, so retained here for the same reason: a page that deletes
    // the `URLSearchParams` global must not take `new URL()`'s query object
    // with it.
    ctor->retained.push_back(params_ctor);
    // §6.1 "new URL(url, base)": the base parsed first, then the URL against
    // it; either failing is a TypeError. `undefined` is the string "undefined"
    // here - WebIDL's USVString conversion - which is why
    // `URL.canParse(undefined, "aaa:b")` is false and not a throw.
    ctor->fn = [ctor, params_ctor](context & c, std::span<value> a) {
        const value self_value = c.current_this();
        script::object_object * self = as_object(self_value);
        if (self == nullptr || !c.instance_of(self_value, value::object(ctor))) {
            c.throw_error("TypeError", "URL constructor: 'new' is required");
            return value::undefined();
        }
        const std::string given = usv_arg(c, a, 0);
        std::optional<url_record> base;
        if (a.size() > 1 && !a[1].is_undefined()) {
            const std::string base_text = usv_arg(c, a, 1);
            base = parse_url(base_text);
            if (!base) {
                c.throw_error("TypeError", "Invalid base URL: '" + base_text + "'");
                return value::undefined();
            }
        }
        const std::optional<url_record> url = parse_url(given, base ? &*base : nullptr);
        if (!url) {
            c.throw_error("TypeError", "Invalid URL: '" + given + "'");
            return value::undefined();
        }
        store_url(c, *self, *url);
        // The query object, made with the URL and attached to it: it reads
        // this URL's query and writes it back, so there is nothing to keep in
        // step.
        const value params = c.construct(params_ctor, {});
        if (c.throw_pending()) { return value::undefined(); }
        if (script::object_object * held = as_object(params)) {
            held->define(owner_key, self_value, script::attr_none);
            // The brand: every method checks for list_key, so an attached
            // params carries it too - the value is unused.
            held->define(list_key, value::undefined(), script::attr_none);
        }
        self->define(params_key, params, script::attr_none);
        return self_value;
    };

    for (const url_attribute & attribute : url_attributes) {
        script::native_fn setter;
        if (attribute.set) {
            const url_part part = *attribute.set;
            setter = [part](context & c, std::span<value> a) {
                script::object_object * self = branded_this(c, href_key, "URL");
                if (self == nullptr) { return value::undefined(); }
                url_record url = url_of(*self);
                const std::string given = usv_arg(c, a, 0);
                if (!set_url_part(url, part, given)) {
                    c.throw_error("TypeError", "Invalid URL: '" + given + "'");
                    return value::undefined();
                }
                store_url(c, *self, url);
                return value::undefined();
            };
        }
        define_getter(
            cx, *made.proto, attribute.name,
            [get = attribute.get](context & c, std::span<value>) {
                script::object_object * self = branded_this(c, href_key, "URL");
                if (self == nullptr) { return value::undefined(); }
                return c.string(get(url_of(*self)));
            },
            std::move(setter));
    }
    define_getter(cx, *made.proto, "searchParams", [](context & c, std::span<value>) {
        script::object_object * self = branded_this(c, href_key, "URL");
        if (self == nullptr) { return value::undefined(); }
        const value * held = self->find(params_key);
        return held == nullptr ? value::undefined() : *held;
    });
    for (const char * name : {"toString", "toJSON"}) {
        set_method(
            cx, *made.proto, name,
            [](context & c, std::span<value>) {
                script::object_object * self = branded_this(c, href_key, "URL");
                if (self == nullptr) { return value::undefined(); }
                return c.string(url_of(*self).serialize());
            },
            script::attr_builtin);
    }
    // §6.1 `URL.parse` and `URL.canParse`: the constructor's answer without
    // the throw - null, and false.
    const auto parses = [](context & c, std::span<value> a) -> std::optional<url_record> {
        std::optional<url_record> base;
        if (a.size() > 1 && !a[1].is_undefined()) {
            base = parse_url(usv_arg(c, a, 1));
            if (!base) { return std::nullopt; }
        }
        return parse_url(usv_arg(c, a, 0), base ? &*base : nullptr);
    };
    set_method(
        cx, *ctor, "canParse",
        [parses](context & c, std::span<value> a) {
            return value::boolean(parses(c, a).has_value());
        },
        script::attr_builtin);
    set_method(
        cx, *ctor, "parse",
        [parses, ctor](context & c, std::span<value> a) {
            const std::optional<url_record> url = parses(c, a);
            if (!url) { return value::null(); }
            const value args[1] = {c.string(url->serialize())};
            return c.construct(value::object(ctor), args);
        },
        script::attr_builtin);
    return ctor;
}

} // namespace ctbrowser::shell
