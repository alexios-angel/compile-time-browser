#include <ctbrowser/dom/document.hpp>
#include <ctbrowser/dom/token_list.hpp>

#include "check.hpp"
#include <array>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;

namespace {

void test_ordered_tokens() {
    CHECK_EQ(parse_ordered_tokens(" first\tsecond\nfirst\fthird\rsecond "),
             (std::vector<std::string>{"first", "second", "third"}));
    CHECK(parse_ordered_tokens("\t\n\f\r ").empty());
    // Vertical tab and nonbreaking space are not HTML token separators.
    CHECK_EQ(parse_ordered_tokens("a\vb a\302\240b a\vb"),
             (std::vector<std::string>{"a\vb", "a\302\240b"}));
    CHECK(!validate_token("a\vb"));
    CHECK(!validate_token("a\302\240b"));
}

void test_button_toggle() {
    atom_table atoms;
    document doc{atoms};
    const node_id button = doc.create_element(atoms.intern("button"));
    const atom classes = atoms.intern("class");
    const atom pressed = atoms.intern("aria-pressed");
    CHECK(doc.append_child(doc.root(), button).has_value());
    CHECK(doc.set_attribute(button, classes, "btn").has_value());
    doc.log_writes(true);
    for (const bool active : {true, false}) {
        const auto result = toggle_token(doc, button, classes, "active");
        CHECK(result && result->present == active && result->update && *result->update);
        if (!result) { return; }
        CHECK(doc.set_attribute(button, pressed, result->present ? "true" : "false").has_value());
        CHECK_EQ(doc.read().attribute_value(button, classes), active ? "btn active" : "btn");
        CHECK_EQ(doc.read().attribute_value(button, pressed), active ? "true" : "false");
        const auto writes = doc.take_writes();
        CHECK_EQ(writes.size(), 2u);
        if (writes.size() == 2) {
            CHECK(writes[0].node == button && writes[0].name == classes && !writes[0].text);
            CHECK(writes[1].node == button && writes[1].name == pressed && !writes[1].text);
        }
    }
}

void test_update_steps() {
    atom_table atoms;
    document doc{atoms};
    const node_id link = doc.create_element(atoms.intern("a"));
    const atom rel = atoms.intern("rel");
    doc.log_writes(true);
    const auto initial_version = doc.version();
    const auto missing = update_tokens(doc, link, rel, {});
    CHECK(missing && !*missing);
    CHECK(!doc.read().has_attribute(link, rel));
    CHECK_EQ(doc.version(), initial_version);
    CHECK(doc.take_writes().empty());

    CHECK(doc.set_attribute(link, rel, "").has_value());
    (void)doc.take_writes();
    const auto empty = update_tokens(doc, link, rel, {});
    CHECK(empty && *empty);
    CHECK(doc.read().has_attribute(link, rel));
    CHECK(doc.read().attribute_value(link, rel).empty());
    CHECK_EQ(doc.take_writes().size(), 1u);

    const auto added = toggle_token(doc, link, rel, "noopener");
    CHECK(added && added->present && added->update && *added->update);
    CHECK_EQ(doc.read().attribute_value(link, rel), "noopener");
    CHECK(!doc.read().has_attribute(link, atoms.intern("class")));
    (void)doc.take_writes();
    const std::array<std::string, 1> tokens{"noopener"};
    const auto before_same = doc.version();
    const auto same = update_tokens(doc, link, rel, tokens);
    CHECK(same && *same);
    CHECK_EQ(doc.read().attribute_value(link, rel), "noopener");
    CHECK_EQ(doc.version(), before_same + 1);
    const auto writes = doc.take_writes();
    CHECK_EQ(writes.size(), 1u);
    if (!writes.empty()) {
        CHECK(writes[0].node == link && writes[0].name == rel && !writes[0].text);
    }
}

void test_noop_and_validation() {
    atom_table atoms;
    document doc{atoms};
    const node_id node = doc.create_element(atoms.intern("div"));
    const atom classes = atoms.intern("class");
    const std::string_view raw = "\tactive  active\nother\r";
    CHECK(doc.set_attribute(node, classes, raw).has_value());
    doc.log_writes(true);
    const auto version = doc.version();
    const auto keep = toggle_token(doc, node, classes, "active", true);
    CHECK(keep && keep->present && keep->update && !*keep->update);
    const auto absent = toggle_token(doc, node, classes, "missing", false);
    CHECK(absent && !absent->present && absent->update && !*absent->update);
    CHECK_EQ(doc.read().attribute_value(node, classes), raw);
    CHECK_EQ(doc.version(), version);
    CHECK(doc.take_writes().empty());

    for (const std::string_view token : {"", "a b", "a\tb", "a\nb", "a\fb", "a\rb"}) {
        const token_error error = token.empty() ? token_error::empty : token_error::whitespace;
        CHECK(validate_token(token) == error);
        const auto invalid = toggle_token(doc, node, classes, token);
        CHECK(!invalid && invalid.error() == error);
    }
    CHECK_EQ(doc.read().attribute_value(node, classes), raw);
    CHECK_EQ(doc.version(), version);
    CHECK(doc.take_writes().empty());
    const auto removed = toggle_token(doc, node, classes, "active");
    CHECK(removed && !removed->present && removed->update && *removed->update);
    CHECK_EQ(doc.read().attribute_value(node, classes), "other");
    CHECK_EQ(doc.take_writes().size(), 1u);
}

void test_failed_write() {
    atom_table atoms;
    document doc{atoms};
    const node_id live = doc.create_element(atoms.intern("button"));
    const node_id stale{live.slot, live.generation + 1u};
    const atom classes = atoms.intern("class");
    doc.log_writes(true);
    for (const node_id invalid : {node_id{}, stale}) {
        CHECK(!doc.read().contains(invalid));
        const auto result = toggle_token(doc, invalid, classes, "active");
        CHECK(result && result->present && !result->update &&
              result->update.error() == dom_error::no_such_node);
    }
    const node_id text = doc.create_text("unchanged");
    const auto result = toggle_token(doc, text, classes, "active");
    CHECK(result && result->present && !result->update &&
          result->update.error() == dom_error::not_an_element);
    CHECK_EQ(doc.read().text(text), "unchanged");
    CHECK(!doc.read().has_attribute(live, classes));
    CHECK(doc.take_writes().empty());
}

} // namespace

int main() {
    test_ordered_tokens();
    test_button_toggle();
    test_update_steps();
    test_noop_and_validation();
    test_failed_write();
    REPORT("dom_token_list");
}
