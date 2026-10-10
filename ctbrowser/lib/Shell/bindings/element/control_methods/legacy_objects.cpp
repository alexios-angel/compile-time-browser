//===- legacy_objects.cpp - Forms and selects as legacy platform objects --===//
//
// HTMLFormElement and HTMLSelectElement as legacy platform objects
// (WebIDL 3.9): `form[0]`, `form.someName`, `select[2]` and
// `select[2] = option`.
//
// An interface with an indexed or a named getter makes its instances exotic:
// their own properties are whatever the getter answers at the moment of the
// read, so `form.x` after `input.name = "x"` in the same statement is the
// input. The wrapper wrap() hands out for these two is therefore a proxy over
// the ordinary one, with 3.9's internal methods as its traps. The ordinary
// object behind it is still the wrapper every binding keeps its slots on
// (`wrappers_`): handle_of and receiver() see through the proxy to it, and an
// accessor read or written through the proxy runs on it, so the reflected
// attributes, `elements`, `options` and the event handler attributes are the
// same code for a form as for a div. A method is called on the proxy, which
// is `this` at the call site, and finds its node through receiver().
//
// HTMLFormElement is [LegacyOverrideBuiltIns]: a named control shadows the
// interface (`<input name=submit>` makes `form.submit` the input), so "O has
// an own property named P" in the named property visibility algorithm can only
// mean a property the page gave the form. The target's own properties are the
// bindings' (tagName, nodeType, the slots), standing in for what lives on the
// prototype in a browser; the page's are the expandos, the keys it created
// through the proxy.
//
//===----------------------------------------------------------------------===//

#include "helpers.hpp"

#include <memory>
#include <unordered_map>

namespace ctbrowser::shell::detail {

namespace {

/// WebIDL's "array index": ToString(ToUint32(P)) is P, and it is not
/// 2^32 - 1 (4294967295). Rejects leading zeros and more than 10 digits.
[[nodiscard]] std::optional<std::uint32_t> array_index(std::string_view key) {
    if (key.empty() || key.size() > 10 || (key.size() > 1 && key.front() == '0')) {
        return std::nullopt;
    }
    std::uint64_t n = 0;
    for (const char ch : key) {
        if (ch < '0' || ch > '9') { return std::nullopt; }
        n = n * 10 + static_cast<std::uint64_t>(ch - '0');
    }
    if (n >= 0xFFFFFFFFull) { return std::nullopt; }
    return static_cast<std::uint32_t>(n);
}

/// What one legacy platform object keeps between reads: the keys the page
/// created on it, a form's past names map (HTML 4.10.3: each name the named
/// getter answered with one element, oldest first), and the document as it was
/// when last read.
struct legacy_state {
    bool form = false;
    std::vector<std::string> expandos;
    std::vector<std::pair<std::string, node_id>> past_names;
    /// The document as of one version, cached the way a live collection is
    /// (make_live_collection): the items the indices answer (the select's list
    /// of options, the form's listed elements) and a form's candidates for a
    /// named property in tree order, with the listed ones and the img ones that
    /// answer to each id and name. [LegacyOverrideBuiltIns] asks about every
    /// string key before the prototype, `form.submit` included, so this is one
    /// walk per version rather than one per read. Also keyed by the bindings and
    /// the node, which adopting the element changes.
    const dom_bindings * seen_by = nullptr;
    node_id seen_at;
    std::uint64_t seen_version = 0;
    std::vector<node_id> items;
    std::vector<node_id> candidates;
    std::unordered_map<std::string, std::vector<node_id>> listed;
    std::unordered_map<std::string, std::vector<node_id>> images;

    [[nodiscard]] bool expando(std::string_view key) const {
        return std::ranges::find(expandos, key) != expandos.end();
    }
};

} // namespace

// An img is form-associated but not listed, and has no `form` attribute to
// reassociate it, so its form owner is the parser's association, else its
// nearest form ancestor.
node_id control_helpers::image_form_owner(const read_txn & txn, node_id img) const {
    if (const node_id parsed = txn.parser_form(img)) { return parsed; }
    for (node_id up = txn.parent(img); up; up = txn.parent(up)) {
        if (is(txn, up, "form")) { return up; }
    }
    return node_id{};
}

value control_helpers::legacy_object(context & cx, script::object_object & target,
                                     node_id id) const {
    dom_bindings * made_by = bindings;
    bool is_form = false;
    {
        const auto txn = made_by->doc_->read();
        is_form = is(txn, id, "form");
        if (!is_form && !is(txn, id, "select")) { return value::undefined(); }
        // A customized built-in (or a candidate for one, which the parser
        // leaves with only its `is` attribute) keeps an ordinary wrapper:
        // `new` made that object, and the upgrade and the reactions run on it.
        if (made_by->is_values_.contains(id.key()) ||
            txn.has_attribute(id, made_by->atoms_->intern("is"))) {
            return value::undefined();
        }
    }
    const control_helpers helpers{made_by};
    const auto state = std::make_shared<legacy_state>();
    state->form = is_form;

    // Which document and node the target is now. Asked on every trap, because
    // an adopted form moves to another document's bindings with its wrapper.
    const auto node_of = [made_by](value on) -> std::pair<dom_bindings *, node_id> {
        dom_bindings * owner = made_by->owner_of(on);
        return {owner, owner == nullptr ? node_id{} : owner->handle_of(on)};
    };
    // The document as `state` last read it, read again once its version moved.
    const auto view = [helpers, state](dom_bindings * owner, node_id at) -> const legacy_state & {
        legacy_state & seen = *state;
        const std::uint64_t version = owner->doc_->version();
        if (seen.seen_by == owner && seen.seen_at == at && seen.seen_version == version) {
            return seen;
        }
        seen.seen_by = owner;
        seen.seen_at = at;
        seen.seen_version = version;
        seen.items.clear();
        seen.candidates.clear();
        seen.listed.clear();
        seen.images.clear();
        const auto txn = owner->doc_->read();
        if (!txn.contains(at)) { return seen; }
        if (!seen.form) {
            seen.items = helpers.options_of(txn, owner, at);
            return seen;
        }
        const atom answers_to[] = {owner->atoms_->intern("id"), owner->atoms_->intern("name")};
        helpers.walk_tree(txn, helpers.tree_top(txn, owner, at), [&](node_id one) {
            const bool listed = helpers.is_listed(txn, owner, one);
            if (listed ? helpers.form_owner(txn, owner, one) != at
                       : !helpers.is(txn, one, "img") || helpers.image_form_owner(txn, one) != at) {
                return;
            }
            if (listed) { seen.items.push_back(one); }
            seen.candidates.push_back(one);
            for (const atom attribute : answers_to) {
                const std::string_view name = txn.attribute_value(one, attribute);
                if (name.empty()) { continue; }
                std::vector<node_id> & under =
                    (listed ? seen.listed : seen.images)[std::string{name}];
                if (under.empty() || under.back() != one) { under.push_back(one); }
            }
        });
        return seen;
    };
    // HTML 4.10.3 "determine the value of a named property" steps 1-2: the
    // listed elements the form owns that answer to `name`, else the img
    // elements it owns that do. Tree order.
    const auto candidates = [view](dom_bindings * owner, node_id form, const std::string & name) {
        const legacy_state & seen = view(owner, form);
        for (const auto * by_name : {&seen.listed, &seen.images}) {
            if (const auto found = by_name->find(name); found != by_name->end()) {
                return found->second;
            }
        }
        return std::vector<node_id>{};
    };
    // A past name whose element has since changed form owner is forgotten.
    const auto prune = [helpers, state](dom_bindings * owner, node_id form) {
        const auto txn = owner->doc_->read();
        std::erase_if(state->past_names, [&](const std::pair<std::string, node_id> & entry) {
            const node_id element = entry.second;
            if (!txn.contains(element)) { return true; }
            return (helpers.is(txn, element, "img")
                        ? helpers.image_form_owner(txn, element)
                        : helpers.form_owner(txn, owner, element)) != form;
        });
    };
    // HTML 4.10.3 "determine the value of a named property", or undefined for
    // a name that is not a supported property name. Several elements are one
    // RadioNodeList per name, kept on the target: the descriptor and a read of
    // the same name are the same object (form-nameditem.html compares them).
    const auto named = [state, prune, candidates](context & c, dom_bindings * owner, node_id form,
                                                  value on, const std::string & name) -> value {
        const std::vector<node_id> found = candidates(owner, form, name);
        if (found.empty()) {
            prune(owner, form);
            for (const auto & [past, element] : state->past_names) {
                if (past == name) { return owner->wrap(c, element); }
            }
            return value::undefined();
        }
        if (found.size() > 1) {
            auto * holder = static_cast<script::object_object *>(on.as_heap());
            const std::string key = std::string{script::private_key_prefix} + "named:" + name;
            if (const value * held = holder->find(key); held != nullptr) { return *held; }
            const value list = owner->make_live_collection(
                c, [candidates, owner, form, name] { return candidates(owner, form, name); },
                "RadioNodeList");
            holder->define(key, list, script::attr_none);
            return list;
        }
        std::erase_if(state->past_names, [&](const auto & entry) { return entry.first == name; });
        state->past_names.emplace_back(name, found.front());
        return owner->wrap(c, found.front());
    };
    // The supported property names, in HTML 4.10.3's order: by element in tree
    // order, and for one element its id, its name, then its past names.
    const auto names = [state, prune, view](dom_bindings * owner, node_id form) {
        prune(owner, form);
        std::vector<std::string> out;
        const std::vector<node_id> owned = view(owner, form).candidates;
        const auto txn = owner->doc_->read();
        const atom id_attribute = owner->atoms_->intern("id");
        const atom name_attribute = owner->atoms_->intern("name");
        const auto add = [&out](std::string_view name) {
            if (!name.empty() && std::ranges::find(out, name) == out.end()) {
                out.emplace_back(name);
            }
        };
        for (const node_id at : owned) {
            add(txn.attribute_value(at, id_attribute));
            add(txn.attribute_value(at, name_attribute));
            for (const auto & [past, element] : state->past_names) {
                if (element == at) { add(past); }
            }
        }
        return out;
    };
    // A named property is visible (WebIDL 3.9 "named property visibility")
    // unless an own property of that name shadows it: the page's, or a slot
    // of the bindings' that cannot be reconfigured, whose value the proxy's
    // invariants pin (ECMA-262 10.5.5, 10.5.8).
    const auto visible = [state](context & c, value on, const std::string & key) {
        if (!state->form || key.starts_with(symbol_key_mark) || array_index(key) ||
            state->expando(key)) {
            return false;
        }
        context::property_descriptor own;
        return !c.own_property(on, key, own) || own.configurable;
    };
    // Whether a name is a supported property name at all, visible or not: a control or an img
    // answering to it, or a past name.
    const auto supported = [state, prune, view](dom_bindings * owner, node_id form,
                                                const std::string & name) {
        if (!state->form || name.empty() || name.starts_with(symbol_key_mark)) { return false; }
        const legacy_state & seen = view(owner, form);
        if (seen.listed.contains(name) || seen.images.contains(name)) { return true; }
        prune(owner, form);
        return std::ranges::any_of(state->past_names,
                                   [&name](const auto & entry) { return entry.first == name; });
    };
    // The item at a supported index, or none.
    const auto item = [view](dom_bindings * owner, node_id at, std::uint32_t index) {
        const std::vector<node_id> & items = view(owner, at).items;
        return index < items.size() ? items[index] : node_id{};
    };

    auto * handler = cx.allocate<script::object_object>();
    const auto trap = [&cx, handler](const char * name, script::native_fn fn) {
        handler->set(name, native(cx, name, std::move(fn)));
    };
    // [[Get]]: OrdinaryGet over [[GetOwnProperty]] below; an index or a
    // visible name answers, anything else is the target's, up its chain.
    trap("get", [node_of, item, named, visible](context & c, std::span<value> a) -> value {
        if (a.size() < 2 || !a[0].is_object()) { return value::undefined(); }
        const std::string key = c.to_string(a[1]);
        if (!key.starts_with(symbol_key_mark)) {
            const auto [owner, at] = node_of(a[0]);
            if (owner != nullptr && at) {
                if (const auto index = array_index(key)) {
                    if (const node_id one = item(owner, at, *index)) { return owner->wrap(c, one); }
                } else if (visible(c, a[0], key)) {
                    if (const value found = named(c, owner, at, a[0], key); !found.is_undefined()) {
                        return found;
                    }
                }
            }
        }
        return c.lookup_property(a[0], key);
    });
    trap("has", [node_of, item, supported, visible](context & c, std::span<value> a) -> value {
        if (a.size() < 2 || !a[0].is_object()) { return value::boolean(false); }
        const std::string key = c.to_string(a[1]);
        const auto [owner, at] = node_of(a[0]);
        if (owner != nullptr && at) {
            if (const auto index = array_index(key); index && item(owner, at, *index)) {
                return value::boolean(true);
            }
            if (visible(c, a[0], key) && supported(owner, at, key)) { return value::boolean(true); }
        }
        return value::boolean(c.has_property(a[0], key));
    });
    // [[GetOwnProperty]], LegacyPlatformObjectGetOwnProperty: a supported
    // index is an enumerable, configurable data property, writable only for
    // the select's indexed setter; a visible name is configurable and neither
    // enumerable ([LegacyUnenumerableNamedProperties]) nor writable.
    trap("getOwnPropertyDescriptor",
         [node_of, item, named, visible, is_form](context & c, std::span<value> a) -> value {
             if (a.size() < 2 || !a[0].is_object()) { return value::undefined(); }
             const std::string key = c.to_string(a[1]);
             const auto [owner, at] = node_of(a[0]);
             if (owner != nullptr && at) {
                 if (const auto index = array_index(key)) {
                     if (const node_id one = item(owner, at, *index)) {
                         return c.from_property_descriptor(context::property_descriptor::data(
                             owner->wrap(c, one),
                             static_cast<std::uint8_t>(script::attr_enumerable |
                                                       script::attr_configurable |
                                                       (is_form ? 0 : script::attr_writable))));
                     }
                 } else if (visible(c, a[0], key)) {
                     if (const value found = named(c, owner, at, a[0], key);
                         !found.is_undefined()) {
                         return c.from_property_descriptor(
                             context::property_descriptor::data(found, script::attr_configurable));
                     }
                 }
             }
             context::property_descriptor own;
             if (c.own_property(a[0], key, own)) { return c.from_property_descriptor(own); }
             return value::undefined();
         });
    // [[Set]] (WebIDL 3.9.4): the select's indexed setter is the options
    // collection's (HTML 4.10.7); a form has none, so an index is a rejected
    // write. [[Set]] ignores named properties: a setter along the chain still
    // runs for a name a control answers to (`form.action = url` beside an
    // `<input name=action>` sets the attribute), and any other write of that
    // name ends in [[DefineOwnProperty]], which refuses it below. Anything
    // else is an ordinary set on the target, and a property it creates is the
    // page's.
    trap("set", [node_of, supported, state, is_form](context & c, std::span<value> a) -> value {
        if (a.size() < 3 || !a[0].is_object()) { return value::boolean(false); }
        const std::string key = c.to_string(a[1]);
        const auto [owner, at] = node_of(a[0]);
        if (owner != nullptr && at) {
            if (array_index(key)) {
                if (is_form) { return value::boolean(false); }
                c.store_property(c.lookup_property(a[0], "options"), key, a[2]);
                return value::boolean(!c.throw_pending());
            }
            if (supported(owner, at, key)) {
                for (value up = a[0]; up.is_object_like(); up = c.get_prototype(up)) {
                    context::property_descriptor found;
                    if (!c.own_property(up, key, found)) { continue; }
                    if (!found.is_accessor() || !found.setter.is_callable()) { break; }
                    (void)c.call(found.setter, std::span<const value>{&a[2], 1}, a[0]);
                    return value::boolean(!c.throw_pending());
                }
                return value::boolean(false);
            }
        }
        context::property_descriptor own;
        const bool had = c.own_property(a[0], key, own);
        if (had && ((own.is_data() && !own.writable) ||
                    (own.is_accessor() && !own.setter.is_callable()))) {
            return value::boolean(false);
        }
        c.store_property(a[0], key, a[2]);
        if (!had && c.own_property(a[0], key, own)) { state->expandos.push_back(key); }
        return value::boolean(true);
    });
    // [[DefineOwnProperty]]: an index defines through the indexed setter when
    // there is one and a data descriptor asks, and is refused otherwise; a
    // supported name is refused outright ([LegacyOverrideBuiltIns], no named
    // setter).
    trap("defineProperty",
         [node_of, supported, state, is_form](context & c, std::span<value> a) -> value {
             if (a.size() < 3 || !a[0].is_object()) { return value::boolean(false); }
             const std::string key = c.to_string(a[1]);
             const context::property_descriptor wanted = c.to_property_descriptor(a[2]);
             if (c.throw_pending()) { return value::boolean(false); }
             const auto [owner, at] = node_of(a[0]);
             if (owner != nullptr && at) {
                 if (array_index(key)) {
                     if (is_form || wanted.is_accessor()) { return value::boolean(false); }
                     c.store_property(c.lookup_property(a[0], "options"), key, wanted.held);
                     return value::boolean(!c.throw_pending());
                 }
                 if (supported(owner, at, key)) { return value::boolean(false); }
             }
             context::property_descriptor own;
             const bool had = c.own_property(a[0], key, own);
             const bool defined = c.define_own_property(a[0], key, wanted);
             if (defined && !had) { state->expandos.push_back(key); }
             return value::boolean(defined);
         });
    // [[Delete]]: a supported index or a visible name cannot go (there is no
    // deleter); an unsupported index is already absent.
    trap("deleteProperty",
         [node_of, item, supported, state, visible](context & c, std::span<value> a) -> value {
             if (a.size() < 2 || !a[0].is_object()) { return value::boolean(true); }
             const std::string key = c.to_string(a[1]);
             const auto [owner, at] = node_of(a[0]);
             if (owner != nullptr && at) {
                 if (const auto index = array_index(key)) {
                     return value::boolean(!item(owner, at, *index));
                 }
                 if (visible(c, a[0], key) && supported(owner, at, key)) {
                     return value::boolean(false);
                 }
             }
             context::property_descriptor own;
             if (!c.own_property(a[0], key, own)) { return value::boolean(true); }
             if (!own.configurable) { return value::boolean(false); }
             (void)c.delete_own_property(a[0], key);
             std::erase(state->expandos, key);
             return value::boolean(true);
         });
    // [[OwnPropertyKeys]]: the indices, the visible names, then the target's
    // own: strings in creation order, then symbols.
    trap("ownKeys", [node_of, view, names, visible](context & c, std::span<value> a) -> value {
        const value out = c.make_array();
        if (a.empty() || !a[0].is_object()) { return out; }
        std::vector<std::string> keys;
        const auto [owner, at] = node_of(a[0]);
        if (owner != nullptr && at) {
            const std::size_t count = view(owner, at).items.size();
            for (std::size_t i = 0; i < count; ++i) { keys.push_back(std::to_string(i)); }
            for (std::string & name : names(owner, at)) {
                if (visible(c, a[0], name)) { keys.push_back(std::move(name)); }
            }
        }
        std::vector<std::string> symbols;
        static_cast<script::object_object *>(a[0].as_heap())
            ->each_own_key([&keys, &symbols](const std::string & key) {
                if (script::is_private_key(key) || std::ranges::find(keys, key) != keys.end()) {
                    return;
                }
                (key.starts_with(symbol_key_mark) ? symbols : keys).push_back(key);
            });
        auto & listed = static_cast<script::array_object *>(out.as_heap())->items;
        for (const std::string & key : keys) { listed.push_back(c.string(key)); }
        for (const std::string & key : symbols) { listed.push_back(c.key_value(key)); }
        return out;
    });
    // [[PreventExtensions]] answers false for every legacy platform object.
    trap("preventExtensions",
         [](context &, std::span<value>) -> value { return value::boolean(false); });
    return value::object(
        cx.allocate<script::proxy_object>(value::object(&target), value::object(handler)));
}

} // namespace ctbrowser::shell::detail
