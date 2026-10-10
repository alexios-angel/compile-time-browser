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

void test_contains_add_remove() {
    atom_table atoms;
    document doc{atoms};
    const node_id node = doc.create_element(atoms.intern("a"));
    const atom rel = atoms.intern("rel");
    doc.log_writes(true);
    for (const auto change : {add_tokens, remove_tokens}) {
        const auto result = change(doc, node, rel, {});
        CHECK(result && *result && !**result);
        CHECK(!doc.read().has_attribute(node, rel));
    }
    CHECK(doc.take_writes().empty());
    const std::string_view raw = "\ta  a\nb\r";
    CHECK(doc.set_attribute(node, rel, raw).has_value());
    (void)doc.take_writes();
    const auto before = doc.version();
    CHECK(contains_token(doc.read().attribute_value(node, rel), "a"));
    CHECK(contains_token(doc.read().attribute_value(node, rel), "b"));
    const std::string snapshot{doc.read().attribute_value(node, rel)};
    CHECK(contains_token(snapshot, "a"));
    for (const std::string_view token : {"", "a b", "a\tb", "a\nb", "A", "missing"}) {
        CHECK(!contains_token(doc.read().attribute_value(node, rel), token));
    }
    CHECK_EQ(doc.read().attribute_value(node, rel), raw);
    CHECK_EQ(doc.version(), before);
    CHECK(doc.take_writes().empty());

    for (const auto change : {add_tokens, remove_tokens}) {
        CHECK(doc.set_attribute(node, rel, raw).has_value());
        (void)doc.take_writes();
        for (int repeat = 0; repeat < 2; ++repeat) {
            const auto version = doc.version();
            const auto result = change(doc, node, rel, {});
            CHECK(result && *result && **result);
            CHECK_EQ(doc.read().attribute_value(node, rel), "a b");
            CHECK_EQ(doc.version(), version + 1);
            CHECK_EQ(doc.take_writes().size(), 1u);
        }
        for (const auto & given : {std::array<std::string, 3>{"good", "a b", ""},
                                   std::array<std::string, 3>{"good", "", "a b"}}) {
            const auto version = doc.version();
            const auto result = change(doc, node, rel, given);
            CHECK(!result && result.error().index == 1 &&
                  result.error().error ==
                      (given[1].empty() ? token_error::empty : token_error::whitespace));
            CHECK_EQ(doc.read().attribute_value(node, rel), "a b");
            CHECK_EQ(doc.version(), version);
            CHECK(doc.take_writes().empty());
            const auto invalid_node = change(doc, {}, rel, given);
            CHECK(!invalid_node && invalid_node.error().index == 1);
        }
    }
    const std::array<std::string, 3> added{"b", "c", "c"};
    const auto add = add_tokens(doc, node, rel, added);
    CHECK(add && *add && **add);
    CHECK_EQ(doc.read().attribute_value(node, rel), "a b c");
    CHECK_EQ(doc.take_writes().size(), 1u);
    const std::array<std::string, 3> removed{"b", "missing", "b"};
    const auto remove = remove_tokens(doc, node, rel, removed);
    CHECK(remove && *remove && **remove);
    CHECK_EQ(doc.read().attribute_value(node, rel), "a c");
    CHECK(contains_token(snapshot, "b") &&
          !contains_token(doc.read().attribute_value(node, rel), "b"));
    CHECK_EQ(doc.take_writes().size(), 1u);
    CHECK(!doc.read().has_attribute(node, atoms.intern("class")));

    const node_id stale{node.slot, node.generation + 1};
    const auto failed = add_tokens(doc, stale, rel, added);
    CHECK(failed && !*failed && failed->error() == dom_error::no_such_node);
    const node_id text = doc.create_text("unchanged");
    const auto wrong_kind = add_tokens(doc, text, rel, added);
    CHECK(wrong_kind && !*wrong_kind && wrong_kind->error() == dom_error::not_an_element);
    CHECK_EQ(doc.read().text(text), "unchanged");
    CHECK(doc.take_writes().empty());
}

} // namespace

int main() {
    test_ordered_tokens();
    test_button_toggle();
    test_update_steps();
    test_noop_and_validation();
    test_failed_write();
    test_contains_add_remove();
    REPORT("dom_token_list");
}
