// dom_bindings - `element.dataset`, the DOMStringMap over `data-*` attributes.
//
// One of twelve files carved out of a 5,442-line bindings/element.cpp on
// 2026-09-08 - which was itself one of six carved out of bindings.cpp on
// 2026-08-09. All are member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp; the helpers more than one of them
// needs are declared in internal.hpp beside this, with external linkage in
// ctbrowser::shell::detail. Nothing about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

// `data-foo-bar` -> `fooBar`. HTML's dataset mangling in the direction that
// decides which properties EXIST: the supported property names of a
// DOMStringMap are computed from the attributes, never from the key a page
// asks about, which is why `el.dataset['-foo']` is undefined on an element
// carrying `data--foo` - that attribute's name is `Foo`.
//
// A `-` followed by an ASCII LOWERCASE letter becomes that letter uppercased;
// everything else is carried across untouched, including a `-` at the end and a
// `-` in front of anything that is not a lowercase letter. False for a name
// that is not a dataset attribute at all: one without the prefix, or one
// carrying an ASCII uppercase letter - which no attribute of an HTML element
// can have and one of an SVG element can.
[[nodiscard]] bool dataset_name_of(std::string_view attribute_name, std::string & out) {
    if (!attribute_name.starts_with("data-")) { return false; }
    const std::string_view rest = attribute_name.substr(5);
    out.clear();
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] >= 'A' && rest[i] <= 'Z') { return false; }
        if (rest[i] == '-' && i + 1 < rest.size() && rest[i + 1] >= 'a' && rest[i + 1] <= 'z') {
            out.push_back(static_cast<char>(rest[i + 1] - 'a' + 'A'));
            ++i;
            continue;
        }
        out.push_back(rest[i]);
    }
    return true;
}

// Why a write can be refused. Two DIFFERENT exceptions, and the corpus checks
// both by name: a key naming an attribute that could never map back to it is a
// SyntaxError, and one whose attribute name could not be written into a start
// tag is an InvalidCharacterError.
enum class dataset_fault : std::uint8_t {
    none,
    syntax,
    character
};

// ...and `fooBar` -> `data-foo-bar`, which is the other direction and NOT the
// inverse. That is the whole reason both exist: `data--foo` reads back as
// `Foo`, so `-foo` names nothing on the way in, and letting it name
// `data--foo` on the way out would make one attribute answer to two keys.
[[nodiscard]] dataset_fault dataset_attribute_of(std::string_view idl, std::string & out) {
    out = "data-";
    for (std::size_t i = 0; i < idl.size(); ++i) {
        if (idl[i] == '-' && i + 1 < idl.size() && idl[i + 1] >= 'a' && idl[i + 1] <= 'z') {
            return dataset_fault::syntax;
        }
        if (idl[i] >= 'A' && idl[i] <= 'Z') {
            out.push_back('-');
            out.push_back(static_cast<char>(idl[i] - 'A' + 'a'));
            continue;
        }
        out.push_back(idl[i]);
    }
    return valid_attribute_name(out) ? dataset_fault::none : dataset_fault::character;
}

} // namespace

void dom_bindings::install_dataset(context & cx, script::object_object & obj, node_id id) {
    // THE STORE IS THE PROXY'S TARGET, and it is what `for (k in el.dataset)`
    // and `Object.keys` walk - so it holds the element's data-* names, refilled
    // on every read of `dataset` below. `DOMStringMap.prototype` goes in front
    // of it, which costs nothing: a prototype chain that ends in undefined
    // falls through to the builtin Object.prototype tables anyway, so
    // `dataset-prototype.html`'s "Properties on Object.prototype should shine
    // through" still holds.
    auto * store = static_cast<script::object_object *>(cx.make_object().as_heap());
    auto * handler = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto trap = [&](std::string name, script::native_fn fn) {
        handler->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // ONE ATTRIBUTE, FOUND THE WAY THE SPECIFICATION FINDS IT: by computing
    // every supported property name from the attribute list and comparing, not
    // by mangling the key and looking that up. The two disagree exactly where
    // `dataset-delete.html` and `dataset-get.html` say they must.
    const auto value_of = [this, id](std::string_view key) -> std::optional<std::string> {
        const auto txn = doc_->read();
        std::string name;
        for (const attribute & held : txn.attributes(id)) {
            // NULL NAMESPACE ONLY: `xlink:data-x` is not a dataset attribute
            // however its local name reads.
            if (held.ns) { continue; }
            if (dataset_name_of(atoms_->text(held.name), name) && name == key) {
                return held.value;
            }
        }
        return std::nullopt;
    };
    // A MISS FALLS THROUGH TO THE TARGET, which is how `dataset.toString` finds
    // Object.prototype's - but NOT past a name the refill left on the target.
    // The store holds the element's data-* names so that enumeration can walk
    // them, and a page that kept `var ds = el.dataset` across a
    // `removeAttribute` must still read undefined out of it: the DOCUMENT is
    // the map, and the target is a list of its keys as of the last read.
    const auto refilled_key = [store](const std::string & key) {
        return store->find(key) != nullptr;
    };
    trap("get", [value_of, refilled_key](context & c, std::span<value> args) {
        if (args.size() < 2) { return value::undefined(); }
        const std::string key = c.to_string(args[1]);
        if (const std::optional<std::string> found = value_of(key)) { return c.string(*found); }
        if (refilled_key(key)) { return value::undefined(); }
        return c.lookup_property(args[0], key);
    });
    trap("has", [value_of, refilled_key](context & c, std::span<value> args) {
        if (args.size() < 2) { return value::boolean(false); }
        const std::string key = c.to_string(args[1]);
        if (value_of(key)) { return value::boolean(true); }
        if (refilled_key(key)) { return value::boolean(false); }
        return value::boolean(!c.lookup_property(args[0], key).is_undefined());
    });
    trap("set", [this, id](context & c, std::span<value> args) {
        if (args.size() < 3) { return value::boolean(false); }
        const std::string key = c.to_string(args[1]);
        std::string name;
        switch (dataset_attribute_of(key, name)) {
        case dataset_fault::syntax:
            // "If name contains a U+002D followed by an ASCII lower alpha,
            // throw a SyntaxError" - because that key is not one this map could
            // ever hand back, `data--foo` reading as `Foo` and not as `-foo`.
            throw_dom_exception(c, "SyntaxError",
                                "dataset: '" + key + "' is not a name a data- attribute can have");
            return value::boolean(false);
        case dataset_fault::character:
            throw_dom_exception(c, "InvalidCharacterError",
                                "dataset: '" + name + "' is not a valid attribute name");
            return value::boolean(false);
        case dataset_fault::none: break;
        }
        // ALREADY LOWERCASE by construction, so this interns as written rather
        // than folding: the only characters the mangle can emit above 'z' are
        // the ones it copied, and folding them would be folding the author's.
        //
        // IN NO NAMESPACE, EXPLICITLY. The qualified `set_attribute` changes the
        // FIRST attribute with that name whatever namespace it is in, so an
        // element already carrying `data-my-custom-attr` in two namespaces of
        // its own had one of THOSE rewritten instead of gaining a third
        // attribute - which is `custom-attrs.html`, whole and entire. A
        // data-* attribute is a null-namespace attribute by definition: it is
        // the same rule `value_of` above reads by.
        (void)doc_->set_attribute_ns(id, atoms_->intern(""), atoms_->intern(name),
                                     c.to_string(args[2]));
        mutated();
        return value::boolean(true);
    });
    const value proxy = value::object(
        cx.allocate<script::proxy_object>(value::object(store), value::object(handler)));
    // AN ACCESSOR, so the target can be refilled before the page sees it.
    // `d.setAttribute('data-foo', 'v')` does not go through this object at all,
    // so a store filled once at install would enumerate whatever the element
    // carried when it was first wrapped - and the corpus sets the attributes
    // AFTER reading nothing out of `dataset`. The values are refilled with the
    // keys because `Object.keys` and JSON.stringify read them off the target;
    // a read of one property still goes through the `get` trap, which asks the
    // document, so nothing here can go stale between two statements.
    auto * reader = cx.allocate<script::native_object>(
        "dataset", [this, id, store, proxy](context & c, std::span<value>) {
            if (!store->prototype.is_object()) {
                const value map = interface_prototype("DOMStringMap");
                if (map.is_object()) { store->prototype = map; }
            }
            std::vector<std::string> stale;
            stale.reserve(store->props.size());
            for (const auto & [key, held] : store->props) { stale.push_back(key); }
            for (const std::string & key : stale) { (void)store->erase(key); }
            const auto txn = doc_->read();
            std::string name;
            for (const attribute & held : txn.attributes(id)) {
                if (held.ns) { continue; }
                if (dataset_name_of(atoms_->text(held.name), name)) {
                    store->set(name, c.string(held.value));
                }
            }
            return proxy;
        });
    // THE PROXY IS REACHABLE ONLY FROM THAT LAMBDA, and a lambda's captures are
    // not a GC edge - `retained` is. Without this the map is collected out from
    // under an element nothing else refers to.
    reader->retained.push_back(proxy);
    obj.define_accessor("dataset", value::object(reader), value::undefined());
}

} // namespace ctbrowser::shell
