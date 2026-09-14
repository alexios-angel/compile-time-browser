#include <ctbrowser/dom/dataset.hpp>
#include <ctbrowser/dom/element.hpp>

namespace ctbrowser {

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
bool dataset_name_of(std::string_view attribute_name, std::string & out) {
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

// ...and `fooBar` -> `data-foo-bar`, which is the other direction and NOT the
// inverse. That is the whole reason both exist: `data--foo` reads back as
// `Foo`, so `-foo` names nothing on the way in, and letting it name
// `data--foo` on the way out would make one attribute answer to two keys.
dataset_fault dataset_attribute_of(std::string_view idl, std::string & out) {
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
    return is_valid_attribute_name(out) ? dataset_fault::none : dataset_fault::character;
}

std::optional<std::string> dataset_value(document & doc, node_id element, std::string_view key) {
    const auto txn = doc.read();
    std::string name;
    for (const attribute & held : txn.attributes(element)) {
        if (held.ns) { continue; }
        if (dataset_name_of(doc.atoms().text(held.name), name) && name == key) {
            return held.value;
        }
    }
    return std::nullopt;
}

std::vector<std::pair<std::string, std::string>> dataset_entries(document & doc, node_id element) {
    const auto txn = doc.read();
    std::vector<std::pair<std::string, std::string>> entries;
    std::string name;
    for (const attribute & held : txn.attributes(element)) {
        if (held.ns) { continue; }
        if (dataset_name_of(doc.atoms().text(held.name), name)) {
            entries.emplace_back(name, held.value);
        }
    }
    return entries;
}

} // namespace ctbrowser
