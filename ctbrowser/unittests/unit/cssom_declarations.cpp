// The CSSOM declaration block - CSSOM §6.6 over style/css/properties.hpp -
// with the shorthand expansion and reconstruction css/cssom compares as
// strings. Each case names the WPT file it stands in for; the block is driven
// directly, with no page, because both `el.style` and `rule.style` are built
// on exactly these functions.

#include <ctbrowser/style/css/properties.hpp>

#include "check.hpp"

#include <string>
#include <string_view>

using ctbrowser::style::css::declaration_block;
using ctbrowser::style::css::declaration_priority;
using ctbrowser::style::css::declaration_value;
using ctbrowser::style::css::longhands_of;
using ctbrowser::style::css::parse_declaration_block;
using ctbrowser::style::css::remove_declaration;
using ctbrowser::style::css::serialize_declaration_block;
using ctbrowser::style::css::set_declaration;

namespace {

[[nodiscard]] std::string round_trip(std::string_view text) {
    declaration_block block;
    parse_declaration_block(block, text);
    return serialize_declaration_block(block);
}

// shorthand-values.html: every row is `cssText = a; cssText == b`.
void test_shorthand_values() {
    CHECK_EQ(round_trip("border: 1px; border-top: 1px;"), std::string{"border: 1px;"});
    CHECK_EQ(round_trip("border: 1px solid red;"), std::string{"border: 1px solid red;"});
    CHECK_EQ(round_trip("border: red;"), std::string{"border: red;"});
    CHECK_EQ(round_trip("border-top: 1px; border-right: 1px; border-bottom: 1px; border-left: "
                        "1px; border-image: none;"),
             std::string{"border: 1px;"});
    CHECK_EQ(
        round_trip("border-top: 1px; border-right: 1px; border-bottom: 1px; border-left: 1px;"),
        std::string{"border-width: 1px; border-style: none; border-color: currentcolor;"});
    CHECK_EQ(
        round_trip("border-top: 1px; border-right: 2px; border-bottom: 3px; border-left: 4px;"),
        std::string{"border-width: 1px 2px 3px 4px; border-style: none; border-color: "
                    "currentcolor;"});
    CHECK_EQ(round_trip("border: 1px; border-top: 2px;"),
             std::string{"border-width: 2px 1px 1px; border-style: none; border-color: "
                         "currentcolor; border-image: none;"});
    CHECK_EQ(round_trip("border: 1px; border-top: 1px !important;"),
             std::string{"border-right: 1px; border-bottom: 1px; border-left: 1px; border-image: "
                         "none; border-top: 1px !important;"});
    CHECK_EQ(round_trip("border: 1px; border-top-color: red;"),
             std::string{"border-width: 1px; border-style: none; border-color: red currentcolor "
                         "currentcolor; border-image: none;"});
    CHECK_EQ(round_trip("border: solid; border-style: dotted"), std::string{"border: dotted;"});
    CHECK_EQ(round_trip("overflow-x: scroll; overflow-y: hidden;"),
             std::string{"overflow: scroll hidden;"});
    CHECK_EQ(round_trip("overflow-x: scroll; overflow-y: scroll;"),
             std::string{"overflow: scroll;"});
    CHECK_EQ(round_trip("outline-width: 2px; outline-style: dotted; outline-color: blue;"),
             std::string{"outline: blue dotted 2px;"});
    CHECK_EQ(
        round_trip("margin-top: 1px; margin-right: 2px; margin-bottom: 3px; margin-left: 4px;"),
        std::string{"margin: 1px 2px 3px 4px;"});
    CHECK_EQ(round_trip("list-style-type: circle; list-style-position: inside; list-style-image: "
                        "none;"),
             std::string{"list-style: inside circle;"});
    CHECK_EQ(round_trip("list-style-type: lower-alpha;"),
             std::string{"list-style-type: lower-alpha;"});
    CHECK_EQ(round_trip("padding: 10px !important; padding-left: 20px;"),
             std::string{"padding: 10px !important;"});
}

// cssstyledeclaration-csstext.html: the logical property groups.
void test_logical_groups() {
    CHECK_EQ(round_trip("margin: 10px; margin-inline: 10px; margin-block: 10px; margin-inline-end: "
                        "10px; margin-bottom: 10px;"),
             std::string{"margin-top: 10px; margin-right: 10px; margin-left: 10px; "
                         "margin-inline-start: 10px; margin-block: 10px; margin-inline-end: 10px; "
                         "margin-bottom: 10px;"});
    CHECK_EQ(round_trip("margin-top: 10px; margin-left: 10px; margin-right: 10px; margin-bottom: "
                        "10px; margin-inline-start: 10px; margin-inline-end: 10px; "
                        "margin-block-start: 10px; margin-block-end: 10px;"),
             std::string{"margin: 10px; margin-inline: 10px; margin-block: 10px;"});
    // cssstyledeclaration-setter-logical: a longhand set again lands after a
    // declaration of the other mapping logic that followed it.
    declaration_block block;
    parse_declaration_block(block, "padding-top: 1px; padding-block-start: 2px");
    CHECK(set_declaration(block, "padding-top", "3px", false));
    CHECK_EQ(serialize_declaration_block(block),
             std::string{"padding-block-start: 2px; padding-top: 3px;"});
}

// shorthand-serialization, flex-serialization, cssstyledeclaration-all-shorthand.
void test_reads_and_all() {
    declaration_block block;
    CHECK(set_declaration(block, "margin", "20px 20px 20px 20px", false));
    CHECK_EQ(block.size(), std::size_t{4});
    CHECK_EQ(declaration_value(block, "margin"), std::string{"20px"});
    CHECK_EQ(serialize_declaration_block(block), std::string{"margin: 20px;"});
    CHECK(set_declaration(block, "margin-top", "initial", true));
    CHECK_EQ(declaration_value(block, "margin"), std::string{});
    CHECK_EQ(declaration_priority(block, "margin-top"), std::string{"important"});
    bool removed = false;
    CHECK_EQ(remove_declaration(block, "margin", removed), std::string{});
    CHECK(removed && block.empty());

    CHECK_EQ(round_trip("flex: initial; flex-basis: initial; flex-shrink: initial;"),
             std::string{"flex: initial;"});
    CHECK_EQ(round_trip("flex: initial; flex-shrink: 0;"),
             std::string{"flex-grow: initial; flex-basis: initial; flex-shrink: 0;"});
    CHECK_EQ(round_trip("flex: 1"), std::string{"flex: 1 1 0%;"});

    CHECK_EQ(round_trip("width: 100px; all: inherit; height: inherit"),
             std::string{"all: inherit;"});
    CHECK_EQ(round_trip("direction: ltr; all: inherit; unicode-bidi: plaintext"),
             std::string{"direction: ltr; all: inherit; unicode-bidi: plaintext;"});
    CHECK_EQ(round_trip("width: 100px; --a: a; all: inherit; --b: b; height: inherit"),
             std::string{"--a: a; all: inherit; --b: b;"});
    block.clear();
    parse_declaration_block(block, "all: revert; width: 50px");
    CHECK_EQ(declaration_value(block, "all"), std::string{});
    CHECK_EQ(declaration_value(block, "width"), std::string{"50px"});
    CHECK(set_declaration(block, "all", "unset", false));
    CHECK_EQ(declaration_value(block, "width"), std::string{"unset"});
    CHECK_EQ(declaration_value(block, "all"), std::string{"unset"});
    CHECK(!set_declaration(block, "all", "10px", false));

    // A value this table cannot split stays whole, and a whole shorthand
    // still reads back - shorthand-serialization's `background: var(--a)`.
    block.clear();
    CHECK(set_declaration(block, "margin", "var(--a)", false));
    CHECK_EQ(block.size(), std::size_t{1});
    CHECK_EQ(declaration_value(block, "margin"), std::string{"var(--a)"});
    CHECK_EQ(declaration_value(block, "margin-top"), std::string{});
    CHECK(longhands_of("background").empty());
    CHECK_EQ(longhands_of("border").size(), std::size_t{17});

    // variable-names.html: a custom property's name is decoded by the
    // tokenizer and written back escaped, so cssText survives a re-parse.
    block.clear();
    parse_declaration_block(block, "--a\\;b: value");
    CHECK_EQ(block.size(), std::size_t{1});
    CHECK_EQ(block.front().name, std::string{"--a;b"});
    CHECK_EQ(serialize_declaration_block(block), std::string{"--a\\;b: value;"});
    block.clear();
    parse_declaration_block(block, "--\\61 b: value; --\\30 : x");
    CHECK_EQ(serialize_declaration_block(block), std::string{"--ab: value; --0: x;"});
}

} // namespace

int main() {
    test_shorthand_values();
    test_logical_groups();
    test_reads_and_all();
    REPORT("cssom_declarations");
}
