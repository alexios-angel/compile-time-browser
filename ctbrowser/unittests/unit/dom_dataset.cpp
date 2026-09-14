#include <ctbrowser/dom/dataset.hpp>
#include <ctbrowser/dom/document.hpp>

#include "check.hpp"
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace ctbrowser;

namespace {

void test_names() {
    constexpr auto names = std::to_array<std::pair<std::string_view, std::string_view>>({
        {"data-", ""},
        {"data-foo-bar", "fooBar"},
        {"data--foo", "Foo"},
        {"data-foo--bar", "foo-Bar"},
        {"data-foo-", "foo-"},
        {"data-foo-1", "foo-1"},
        {"data-foo.bar_baz:qux", "foo.bar_baz:qux"},
        {"data-\303\204-\303\251", "\303\204-\303\251"},
    });
    for (const auto & [attribute, key] : names) {
        std::string name = "previous";
        CHECK(dataset_name_of(attribute, name));
        CHECK_EQ(name, key);
        CHECK(dataset_attribute_of(key, name) == dataset_fault::none);
        CHECK_EQ(name, attribute);
    }
    for (const std::string_view attribute : {"data", "DATA-foo", "id", "data-Foo", "data-fooBar"}) {
        std::string name;
        CHECK(!dataset_name_of(attribute, name));
    }
}

void test_invalid_keys() {
    std::string name;
    for (const std::string_view key : {"-foo", "foo-bar", "foo--bar", "bad name-foo"}) {
        CHECK(dataset_attribute_of(key, name) == dataset_fault::syntax);
    }
    constexpr std::array invalid = {
        std::string_view{"a\0b", 3}, std::string_view{"a\tb"}, std::string_view{"a\nb"},
        std::string_view{"a\fb"},    std::string_view{"a\rb"}, std::string_view{"a b"},
        std::string_view{"a/b"},     std::string_view{"a=b"},  std::string_view{"a>b"},
    };
    for (const std::string_view key : invalid) {
        CHECK(dataset_attribute_of(key, name) == dataset_fault::character);
    }
    for (const std::string_view key : {"0", ":", "a\vb", "a\302\240b", "'", "\"", "<"}) {
        CHECK(dataset_attribute_of(key, name) == dataset_fault::none);
        CHECK_EQ(name, "data-" + std::string{key});
    }
}

void test_document_values() {
    atom_table atoms;
    document doc{atoms};
    const node_id element = doc.create_element(atoms.intern("svg"), node_ns::svg);
    const atom null_ns = atoms.intern("");
    const atom custom = atoms.intern("data-my-custom-attr");
    CHECK(doc.set_attribute_ns(element, atoms.intern("urn:first"), custom, "one").has_value());
    CHECK(doc.set_attribute_ns(element, atoms.intern("urn:second"), custom, "two").has_value());
    CHECK(!dataset_value(doc, element, "myCustomAttr"));
    CHECK(dataset_entries(doc, element).empty());

    CHECK(doc.set_attribute_ns(element, null_ns, custom, "own").has_value());
    CHECK(doc.set_attribute(element, atoms.intern("data--foo"), "double dash").has_value());
    CHECK(doc.set_attribute(element, atoms.intern("data-"), "").has_value());
    CHECK(doc.set_attribute(element, atoms.intern("data-Upper"), "excluded").has_value());
    CHECK(doc.set_attribute(element, atoms.intern("id"), "excluded").has_value());
    const std::vector<std::pair<std::string, std::string>> expected = {
        {"myCustomAttr", "own"}, {"Foo", "double dash"}, {"", ""}};
    const auto saved_entries = dataset_entries(doc, element);
    const auto saved_value = dataset_value(doc, element, "myCustomAttr");
    CHECK_EQ(saved_entries, expected);
    CHECK_EQ(saved_value, "own");
    CHECK_EQ(dataset_value(doc, element, "Foo"), "double dash");
    CHECK(!dataset_value(doc, element, "-foo"));
    CHECK(!dataset_value(doc, element, "Upper"));
    CHECK(!dataset_value(doc, element, "missing"));
    CHECK_EQ(dataset_value(doc, element, ""), "");

    // Native writes use the same name conversion and null-namespace mutation.
    std::string name;
    CHECK(dataset_attribute_of("myCustomAttr", name) == dataset_fault::none);
    CHECK(doc.set_attribute_ns(element, null_ns, atoms.intern(name), "changed").has_value());
    CHECK_EQ(dataset_value(doc, element, "myCustomAttr"), "changed");
    CHECK_EQ(dataset_entries(doc, element),
             (std::vector<std::pair<std::string, std::string>>{
                 {"myCustomAttr", "changed"}, {"Foo", "double dash"}, {"", ""}}));
    CHECK_EQ(saved_value, "own");
    CHECK_EQ(saved_entries, expected);
    CHECK(doc.remove_attribute_ns(element, "", name).has_value());
    CHECK(!dataset_value(doc, element, "myCustomAttr"));
    CHECK_EQ(dataset_entries(doc, element),
             (std::vector<std::pair<std::string, std::string>>{{"Foo", "double dash"}, {"", ""}}));
    CHECK(doc.set_attribute_ns(element, null_ns, atoms.intern(name), "readded").has_value());
    CHECK_EQ(dataset_entries(doc, element),
             (std::vector<std::pair<std::string, std::string>>{
                 {"Foo", "double dash"}, {"", ""}, {"myCustomAttr", "readded"}}));
    const auto txn = doc.read();
    const auto * first = txn.find_attribute_ns(element, "urn:first", "data-my-custom-attr");
    const auto * second = txn.find_attribute_ns(element, "urn:second", "data-my-custom-attr");
    CHECK(first != nullptr && first->value == "one");
    CHECK(second != nullptr && second->value == "two");
}

void test_missing_nodes() {
    atom_table atoms;
    document doc{atoms};
    const node_id live = doc.create_element(atoms.intern("div"));
    CHECK(doc.set_attribute(live, atoms.intern("data-foo"), "live").has_value());
    const node_id stale{live.slot, live.generation + 1u};
    for (const node_id node : {node_id{}, stale, doc.create_text("text")}) {
        CHECK(!dataset_value(doc, node, "foo"));
        CHECK(dataset_entries(doc, node).empty());
    }
}

} // namespace

int main() {
    test_names();
    test_invalid_keys();
    test_document_values();
    test_missing_nodes();
    REPORT("dom_dataset");
}
