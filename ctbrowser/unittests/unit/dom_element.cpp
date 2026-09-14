#include <ctbrowser/dom/element.hpp>

#include "check.hpp"
#include <array>
#include <string_view>

using namespace ctbrowser;

namespace {

void test_borrowed_identity() {
    atom_table atoms;
    atom_table other_atoms;
    document doc{atoms};
    document other{other_atoms};
    const node_id node = doc.create_element(atoms.intern("button"));
    const node_id other_node = other.create_element(other_atoms.intern("button"));
    CHECK(node == other_node);
    const element_ref element{&doc, node};
    const element_ref same{&doc, node};
    const element_ref different_document{&other, other_node};
    const element_ref different_node{&doc, doc.create_element(atoms.intern("button"))};
    CHECK(element == same);
    CHECK(element != different_document);
    CHECK(element != different_node);
    CHECK(validate_element(element).has_value());
    CHECK(validate_element(different_document).has_value());
    CHECK(doc.append_child(doc.root(), node).has_value());
    CHECK(doc.remove_child(node).has_value());
    CHECK(element == same);
    CHECK(validate_element(element).has_value());

    const node_id stale{node.slot, node.generation + 1u};
    for (const element_ref invalid :
         {element_ref{}, element_ref{&doc, {}}, element_ref{&doc, stale}}) {
        const auto result = validate_element(invalid);
        CHECK(!result && result.error() == dom_error::no_such_node);
    }
    for (const node_id non_element :
         {doc.create_text("text"), doc.create_processing_instruction(atoms.intern("pi"), "")}) {
        const auto result = validate_element(element_ref{&doc, non_element});
        CHECK(!result && result.error() == dom_error::not_an_element);
    }
}

void test_attribute_names_and_writes() {
    atom_table atoms;
    document doc{atoms};
    const node_id element = doc.create_element(atoms.intern("button"));
    const atom pressed = atoms.intern("aria-pressed");
    doc.log_writes(true);
    CHECK(set_element_attribute(doc, element, "ARIA-Pressed", "false").has_value());
    CHECK_EQ(doc.read().attribute_value(element, pressed), "false");
    CHECK(!doc.read().has_attribute(element, atoms.intern("ARIA-Pressed")));
    (void)doc.take_writes();
    const auto before = doc.version();
    CHECK(set_element_attribute(doc, element, "ARIA-Pressed", "false").has_value());
    CHECK_EQ(doc.version(), before + 1);
    const auto writes = doc.take_writes();
    CHECK_EQ(writes.size(), 1u);
    if (!writes.empty()) {
        CHECK(writes.front().node == element && writes.front().name == pressed);
    }

    for (const std::string_view name :
         {":", "a:0", "invalid^Name", "\\", "'", "\"", "0", "~", "a\vb", "a\302\240b"}) {
        CHECK(is_valid_attribute_name(name));
        CHECK(set_element_attribute(doc, element, name, "<&>").has_value());
        CHECK_EQ(doc.read().attribute_value(element, attribute_key(doc, element, name)), "<&>");
    }
    (void)doc.take_writes();
    const auto version = doc.version();
    const auto interned = atoms.size();
    constexpr std::array invalid_names = {std::string_view{},       std::string_view{"a\0b", 3},
                                          std::string_view{"a\tb"}, std::string_view{"a\nb"},
                                          std::string_view{"a\fb"}, std::string_view{"a\rb"},
                                          std::string_view{"a b"},  std::string_view{"a/b"},
                                          std::string_view{"a=b"},  std::string_view{"a>b"}};
    for (const std::string_view name : invalid_names) {
        CHECK(!is_valid_attribute_name(name));
        const auto result = set_element_attribute(doc, element, name, "changed");
        CHECK(!result && result.error() == dom_error::invalid_attribute_name);
    }
    const auto priority = set_element_attribute(doc, {}, "", "changed");
    CHECK(!priority && priority.error() == dom_error::invalid_attribute_name);
    CHECK_EQ(doc.version(), version);
    CHECK_EQ(atoms.size(), interned);
    CHECK(doc.take_writes().empty());
    const auto missing = set_element_attribute(doc, {}, "valid", "changed");
    CHECK(!missing && missing.error() == dom_error::no_such_node);
    const auto text = set_element_attribute(doc, doc.create_text("text"), "valid", "changed");
    CHECK(!text && text.error() == dom_error::not_an_element);
}

void test_case_and_namespace() {
    atom_table atoms;
    document doc{atoms};
    const node_id svg = doc.create_element(atoms.intern("svg"), node_ns::svg);
    const atom view_box = atoms.intern("viewBox");
    const atom folded = atoms.intern("viewbox");
    CHECK(set_element_attribute(doc, svg, "viewBox", "0 0 4 3").has_value());
    CHECK_EQ(doc.read().attribute_value(svg, view_box), "0 0 4 3");
    CHECK(!doc.read().has_attribute(svg, folded));
    CHECK(get_element_attribute(doc, svg, "viewBox") == "0 0 4 3");
    CHECK(!get_element_attribute(doc, svg, "viewbox"));

    document xml{atoms};
    xml.set_xml(true);
    const node_id html_in_xml = xml.create_element(atoms.intern("div"));
    CHECK(set_element_attribute(xml, html_in_xml, "viewBox", "case preserved").has_value());
    CHECK_EQ(xml.read().attribute_value(html_in_xml, view_box), "case preserved");
    CHECK(!xml.read().has_attribute(html_in_xml, folded));
    CHECK(get_element_attribute(xml, html_in_xml, "viewBox") == "case preserved");
    CHECK(!get_element_attribute(xml, html_in_xml, "viewbox"));

    const node_id element = doc.create_element(atoms.intern("div"));
    const atom name = atoms.intern("p:attr");
    CHECK(doc.set_attribute_ns(element, atoms.intern("urn:first"), name, "one").has_value());
    CHECK(doc.set_attribute_ns(element, atoms.intern("urn:second"), name, "two").has_value());
    CHECK(set_element_attribute(doc, element, "P:ATTR", "changed").has_value());
    const auto txn = doc.read();
    CHECK_EQ(txn.attributes(element).size(), 2u);
    const auto * first = txn.find_attribute_ns(element, "urn:first", "attr");
    const auto * second = txn.find_attribute_ns(element, "urn:second", "attr");
    CHECK(first != nullptr && first->name == name && first->value == "changed");
    CHECK(second != nullptr && second->name == name && second->value == "two");
    CHECK(get_element_attribute(doc, element, "P:ATTR") == "changed");
    CHECK(!get_element_attribute(doc, element, "attr"));
    CHECK(doc.remove_attribute(element, name).has_value());
    CHECK(get_element_attribute(doc, element, "P:ATTR") == "two");
}

void test_attribute_reads() {
    atom_table atoms;
    document doc{atoms};
    const node_id element = doc.create_element(atoms.intern("button"));
    CHECK(set_element_attribute(doc, element, "disabled", "").has_value());
    constexpr std::string_view bytes{"a\0b", 3};
    CHECK(set_element_attribute(doc, element, "data-value", bytes).has_value());
    const auto saved = get_element_attribute(doc, element, "DATA-VALUE");
    CHECK(saved == bytes);
    CHECK(set_element_attribute(doc, element, "data-value", "changed").has_value());
    CHECK(saved == bytes);
    CHECK(get_element_attribute(doc, element, "data-value") == "changed");
    CHECK(doc.remove_attribute(element, atoms.intern("data-value")).has_value());
    CHECK(saved == bytes);

    // Reads accept even names which the public setter rejects.
    constexpr std::array invalid_names = {std::string_view{}, std::string_view{"bad name"}, bytes};
    for (const std::string_view name : invalid_names) {
        CHECK(!is_valid_attribute_name(name));
        CHECK(doc.set_attribute(element, atoms.intern(name), "unvalidated").has_value());
    }
    doc.log_writes(true);
    const auto before = doc.version();
    CHECK(get_element_attribute(doc, element, "DISABLED") == "");
    CHECK(!get_element_attribute(doc, element, "missing"));
    CHECK(!get_element_attribute(doc, element, "data-value"));
    for (const std::string_view name : invalid_names) {
        CHECK(get_element_attribute(doc, element, name) == "unvalidated");
    }
    CHECK_EQ(doc.version(), before);
    CHECK(doc.take_writes().empty());
}

void test_attribute_toggle() {
    atom_table atoms;
    document doc{atoms};
    const node_id element = doc.create_element(atoms.intern("button"));
    const atom disabled = atoms.intern("disabled");
    doc.log_writes(true);
    auto before = doc.version();
    auto result = toggle_element_attribute(doc, element, "DISABLED").value();
    CHECK(result.present && result.update.value());
    CHECK(doc.read().has_attribute(element, disabled));
    CHECK(doc.read().attribute_value(element, disabled).empty());
    CHECK_EQ(doc.version(), before + 1);
    const auto writes = doc.take_writes();
    CHECK_EQ(writes.size(), 1u);
    if (!writes.empty()) { CHECK(writes.front().name == disabled); }

    CHECK(doc.set_attribute(element, disabled, "kept").has_value());
    (void)doc.take_writes();
    before = doc.version();
    result = toggle_element_attribute(doc, element, "DISABLED", true).value();
    CHECK(result.present && !result.update.value());
    CHECK_EQ(doc.read().attribute_value(element, disabled), "kept");
    CHECK_EQ(doc.version(), before);
    CHECK(doc.take_writes().empty());

    result = toggle_element_attribute(doc, element, "disabled").value();
    CHECK(!result.present && result.update.value());
    CHECK(!doc.read().has_attribute(element, disabled));
    CHECK_EQ(doc.version(), before + 1);
    // Removals are observed from the tree; write notes only record value writes.
    CHECK(doc.take_writes().empty());
    before = doc.version();
    result = toggle_element_attribute(doc, element, "disabled", false).value();
    CHECK(!result.present && !result.update.value());
    CHECK_EQ(doc.version(), before);
    CHECK(doc.take_writes().empty());

    const auto interned = atoms.size();
    for (const std::string_view name :
         {std::string_view{}, std::string_view{"bad name"}, std::string_view{"a\0b", 3}}) {
        // Validation wins even over an invalid handle, before interning a name.
        const auto invalid = toggle_element_attribute(doc, {}, name, false);
        CHECK(!invalid && invalid.error() == dom_error::invalid_attribute_name);
    }
    CHECK_EQ(atoms.size(), interned);
    CHECK_EQ(doc.version(), before);
    CHECK(doc.take_writes().empty());
    for (const node_id invalid : {node_id{}, node_id{element.slot, element.generation + 1u}}) {
        result = toggle_element_attribute(doc, invalid, "disabled").value();
        CHECK(result.present && !result.update && result.update.error() == dom_error::no_such_node);
    }
    result = toggle_element_attribute(doc, doc.create_text("text"), "disabled").value();
    CHECK(result.present && !result.update && result.update.error() == dom_error::not_an_element);
}

void test_toggle_case_and_namespace() {
    atom_table atoms;
    document doc{atoms};
    document xml{atoms};
    xml.set_xml(true);
    const node_id svg = doc.create_element(atoms.intern("svg"), node_ns::svg);
    const node_id html_in_xml = xml.create_element(atoms.intern("div"));
    const atom mixed = atoms.intern("viewBox");
    const atom folded = atoms.intern("viewbox");
    for (const element_ref element : {element_ref{&doc, svg}, element_ref{&xml, html_in_xml}}) {
        const auto result = toggle_element_attribute(*element.owner, element.id, "viewBox").value();
        CHECK(result.present && result.update.value());
        CHECK(element.owner->read().has_attribute(element.id, mixed));
        CHECK(!element.owner->read().has_attribute(element.id, folded));
    }

    const node_id element = doc.create_element(atoms.intern("div"));
    const atom name = atoms.intern("p:attr");
    CHECK(doc.set_attribute_ns(element, atoms.intern("urn:first"), name, "one").has_value());
    CHECK(doc.set_attribute_ns(element, atoms.intern("urn:second"), name, "two").has_value());
    const auto result = toggle_element_attribute(doc, element, "P:ATTR", false).value();
    CHECK(!result.present && result.update.value());
    // The return value is the requested state; only the first qualified match
    // is removed, so another namespace can still hold the same qualified name.
    const auto txn = doc.read();
    CHECK_EQ(txn.attributes(element).size(), 1u);
    CHECK(txn.find_attribute_ns(element, "urn:first", "attr") == nullptr);
    const auto * second = txn.find_attribute_ns(element, "urn:second", "attr");
    CHECK(second != nullptr && second->value == "two");
}

} // namespace

int main() {
    test_borrowed_identity();
    test_attribute_names_and_writes();
    test_case_and_namespace();
    test_attribute_reads();
    test_attribute_toggle();
    test_toggle_case_and_namespace();
    REPORT("dom_element");
}
