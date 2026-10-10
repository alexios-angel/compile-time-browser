// The CSSOM declaration block - CSSOM §6.6 over style/css/properties.hpp -
// with the shorthand expansion and reconstruction css/cssom compares as
// strings. Each case names the WPT file it stands in for; the block is driven
// directly, with no page, because both `el.style` and `rule.style` are built
// on exactly these functions.

#include <ctbrowser/style/css/properties.hpp>

#include "check.hpp"

#include <string>
#include <string_view>
#include <tuple>

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
    // The omitted basis is `0%`, as the cascade expands it (flex-valid).
    CHECK_EQ(round_trip("flex: 1"), std::string{"flex: 1 1 0%;"});
    CHECK_EQ(round_trip("flex: 0"), std::string{"flex: 0 1 0%;"});

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
    // A shorthand this table cannot split still answers from its longhands
    // when `all` set them, and its whole entry is gone.
    block.clear();
    parse_declaration_block(block, "font: 12px serif; all: revert");
    CHECK_EQ(declaration_value(block, "font"), std::string{"revert"});
    CHECK_EQ(serialize_declaration_block(block), std::string{"all: revert;"});
    block.clear();
    parse_declaration_block(block, "font: 12px serif");
    // Seven it names and the six other font-variant-* longhands it resets.
    CHECK_EQ(block.size(), std::size_t{13});
    CHECK_EQ(declaration_value(block, "font"), std::string{"12px serif"});
    CHECK_EQ(declaration_value(block, "font-weight"), std::string{"normal"});
    // font-shorthand-serialization: the slash is a component of its own.
    CHECK_EQ(round_trip("font: 10px/1 Ahem"), std::string{"font: 10px / 1 Ahem;"});
    CHECK_EQ(round_trip("font: italic bold 700 12px / 1.5 'Times New Roman', serif"),
             std::string{"font: italic 700 12px / 1.5 Times New Roman, serif;"});
    CHECK_EQ(round_trip("font: menu"), std::string{"font: menu;"});
    // shorthand-serialization: an important whole shorthand keeps a later
    // longhand out, and `outline-color: auto` is a colour (CSS UI 4 §5.3,
    // which also retired CSS 2.1's `invert`).
    CHECK_EQ(round_trip("background-color: blue; background: red !important; background-color: "
                        "green;"),
             std::string{"background: red !important;"});
    CHECK_EQ(round_trip("outline-color: auto"), std::string{"outline-color: auto;"});
    CHECK_EQ(round_trip("outline-color: invert"), std::string{});
    CHECK_EQ(round_trip("font: Arial"), std::string{});

    // A value this table cannot split stays whole, and a whole shorthand
    // still reads back - shorthand-serialization's `background: var(--a)`.
    block.clear();
    CHECK(set_declaration(block, "margin", "var(--a)", false));
    CHECK_EQ(block.size(), std::size_t{1});
    CHECK_EQ(declaration_value(block, "margin"), std::string{"var(--a)"});
    CHECK_EQ(declaration_value(block, "margin-top"), std::string{});
    CHECK(longhands_of("color").empty());
    CHECK_EQ(longhands_of("grid").size(), std::size_t{6});
    CHECK_EQ(longhands_of("border").size(), std::size_t{17});

    // setproperty-null-undefined, cssstyledeclaration-csstext: a colour is
    // refused by syntax, and a valid one keeps the author's spelling.
    block.clear();
    CHECK(set_declaration(block, "color", "white", false));
    CHECK(!set_declaration(block, "color", "undefined", false));
    CHECK(!set_declaration(block, "color", "unknown color", false));
    CHECK(!set_declaration(block, "color", "12px", false));
    CHECK_EQ(declaration_value(block, "color"), std::string{"white"});
    CHECK(set_declaration(block, "color", "RebeccaPurple", false));
    CHECK_EQ(declaration_value(block, "color"), std::string{"rebeccapurple"});
    CHECK(set_declaration(block, "color", "#ABC", false));
    CHECK_EQ(declaration_value(block, "color"), std::string{"rgb(170, 187, 204)"});
    CHECK(!set_declaration(block, "color", "#ABCDE", false));
    CHECK(set_declaration(block, "background-color", "rgba(0, 0, 0, .5)", false));
    CHECK_EQ(declaration_value(block, "background-color"), std::string{"rgba(0, 0, 0, 0.5)"});
    CHECK(set_declaration(block, "border-top-color", "Highlight", false));
    CHECK(set_declaration(block, "outline-color", "var(--c)", false));
    CHECK(set_declaration(block, "border", "1px solid red", false));
    CHECK_EQ(declaration_value(block, "border"), std::string{"1px solid red"});
    CHECK(set_declaration(block, "outline", "2px dotted", false));
    CHECK_EQ(declaration_value(block, "outline"), std::string{"dotted 2px"});

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

// css/css-backgrounds/parsing/{border-radius,webkit-border-radius}-*.
void test_border_radius() {
    using ctbrowser::style::css::supports_declaration;
    for (const std::string_view name : {"border-radius", "-webkit-border-radius"}) {
        for (const auto & [value, expected] :
             {std::pair{"0", "0px"},
              {"1px 2% 1px 2%", "1px 2%"},
              {"1px 1px 1px 2% / 1px 2% 1px 2%", "1px 1px 1px 2% / 1px 2%"},
              {"1px 1px 1px 1px / 1px 1px 2% 1px", "1px / 1px 1px 2%"},
              {"1px 2% 2% 2% / 1px 2% 3px 2%", "1px 2% 2% / 1px 2% 3px"},
              {"1px 2% / 1px 2%", "1px 2%"},
              {"calc(20px / 2) / 5%", "calc(10px) / 5%"},
              {"calc(-1px) / 10px", "calc(-1px) / 10px"}}) {
            declaration_block block;
            CHECK(supports_declaration(name, value));
            CHECK(set_declaration(block, name, value, false));
            CHECK_EQ(block.size(), std::size_t{4});
            CHECK_EQ(declaration_value(block, name), std::string{expected});
        }
        declaration_block block;
        CHECK(set_declaration(block, name, "1px 2px 3px / 4% 5%", true));
        CHECK_EQ(declaration_value(block, "border-top-left-radius"), std::string{"1px 4%"});
        CHECK_EQ(declaration_value(block, "border-top-right-radius"), std::string{"2px 5%"});
        CHECK_EQ(declaration_value(block, "border-bottom-right-radius"), std::string{"3px 4%"});
        CHECK_EQ(declaration_value(block, "border-bottom-left-radius"), std::string{"2px 5%"});
        for (const std::string_view invalid :
             {"auto", "-1px", "1px / -5%", "1px 2px 3px 4px 5px", "1px / 2px 3px 4px 5px 6px",
              "/ 1px", "1px /", "1px // 2px", "1px / / 2px", "1px / 2px / 3px", "1px inherit",
              "1px, 2px", "1", "1deg", "calc(1s)", "calc(1)"}) {
            CHECK(!supports_declaration(name, invalid));
            CHECK(!set_declaration(block, name, invalid, false));
        }
        CHECK_EQ(declaration_priority(block, name), std::string{"important"});
        bool removed = false;
        CHECK_EQ(remove_declaration(block, name, removed), std::string{"1px 2px 3px / 4% 5%"});
        CHECK(removed && block.empty());
    }
    for (const std::string_view name :
         {"border-top-left-radius", "border-top-right-radius", "border-bottom-right-radius",
          "border-bottom-left-radius", "border-start-start-radius", "border-start-end-radius",
          "border-end-start-radius", "border-end-end-radius", "-webkit-border-top-left-radius",
          "-webkit-border-top-right-radius", "-webkit-border-bottom-right-radius",
          "-webkit-border-bottom-left-radius"}) {
        declaration_block block;
        CHECK(set_declaration(block, name, "1PX 1px", false));
        CHECK_EQ(declaration_value(block, name), std::string{"1px"});
        CHECK(set_declaration(block, name, "calc(10% + 1px) 2em", false));
        for (const std::string_view invalid :
             {"auto", "-1px", "1px -2%", "1px 2px 3px", "1px / 2px", "1px initial", "calc(1deg)"}) {
            CHECK(!supports_declaration(name, invalid));
            CHECK(!set_declaration(block, name, invalid, false));
        }
    }
    declaration_block block;
    CHECK(set_declaration(block, "-webkit-border-radius", "1px 2px", false));
    CHECK_EQ(declaration_value(block, "border-radius"), std::string{"1px 2px"});
    CHECK(set_declaration(block, "border-radius", "3px 4px", false));
    CHECK_EQ(declaration_value(block, "-webkit-border-radius"), std::string{"3px 4px"});
    CHECK(set_declaration(block, "-webkit-border-top-left-radius", "5px", false));
    CHECK_EQ(declaration_value(block, "border-top-left-radius"), std::string{"5px"});
    CHECK_EQ(round_trip("border-radius: 1px; border-start-start-radius: 2px; "
                        "border-top-left-radius: 3px"),
             std::string{"border-top-right-radius: 1px; border-bottom-right-radius: 1px; "
                         "border-bottom-left-radius: 1px; border-start-start-radius: 2px; "
                         "border-top-left-radius: 3px;"});
    CHECK_EQ(round_trip("border-radius: initial"), std::string{"border-radius: initial;"});
    CHECK_EQ(round_trip("border-radius: var(--radius)"),
             std::string{"border-radius: var(--radius);"});
}

// css/css-backgrounds/parsing/border-image-*: grouped slice/width/outset and resets.
void test_border_image() {
    using ctbrowser::style::css::supports_declaration;
    for (const auto & [value, expected] :
         {std::pair{"stretch", "none"},
          {"none 100% / 1 / 0 stretch", "none"},
          {"none space space", "space"},
          {"fill 1 2% 3 4% / auto", "1 2% 3 4% fill / auto"},
          {"1//1px", "1 / / 1px"},
          {"round repeat url('image.png') fill 1 2 / 3% / 4px",
           "url(\"image.png\") 1 2 fill / 3% / 4px round repeat"},
          {"1 1 1 1 / calc(20px / 2) / 2 2", "1 / calc(10px) / 2"},
          {"calc(-1) / calc(-2px) / calc(-3)", "calc(-1) / calc(-2px) / calc(-3)"}}) {
        declaration_block block;
        CHECK(supports_declaration("border-image", value));
        CHECK(set_declaration(block, "border-image", value, true));
        CHECK_EQ(block.size(), std::size_t{5});
        CHECK_EQ(declaration_value(block, "border-image"), std::string{expected});
        CHECK_EQ(declaration_priority(block, "border-image-outset"), std::string{"important"});
        bool removed = false;
        CHECK_EQ(remove_declaration(block, "border-image", removed), std::string{expected});
        CHECK(removed && block.empty());
    }
    declaration_block block;
    CHECK(set_declaration(block, "border-image", "fill 1 2% 3 4% / 5% / 6 repeat", false));
    CHECK_EQ(declaration_value(block, "border-image-source"), std::string{"none"});
    CHECK_EQ(declaration_value(block, "border-image-slice"), std::string{"1 2% 3 4% fill"});
    CHECK_EQ(declaration_value(block, "border-image-width"), std::string{"5%"});
    CHECK_EQ(declaration_value(block, "border-image-outset"), std::string{"6"});
    CHECK_EQ(declaration_value(block, "border-image-repeat"), std::string{"repeat"});
    for (const std::string_view invalid : {"auto",
                                           "none, url(image.png)",
                                           "stretch repeat round",
                                           "fill",
                                           "1 2 3 4 5",
                                           "1% fill 2%",
                                           "1 / -2px",
                                           "-1 / 2px",
                                           "1 / 1 2 3 4 5",
                                           "1 / / auto",
                                           "1 / / 1%",
                                           "1 / none / 1px",
                                           "1 / 1px / auto",
                                           "1 /",
                                           "1 / /",
                                           "1 / / / 2",
                                           "1 none / 2",
                                           "repeat none round",
                                           "1 inherit",
                                           "none none",
                                           "1px",
                                           "calc(1deg)"}) {
        CHECK(!supports_declaration("border-image", invalid));
        CHECK(!set_declaration(block, "border-image", invalid, false));
    }
    CHECK_EQ(declaration_value(block, "border-image-width"), std::string{"5%"});
    CHECK(set_declaration(block, "border", "solid", false));
    CHECK_EQ(declaration_value(block, "border-image"), std::string{"none"});
    CHECK_EQ(round_trip("border-image: initial"), std::string{"border-image: initial;"});
    CHECK_EQ(round_trip("border-image: var(--image)"), std::string{"border-image: var(--image);"});
    for (const auto & [name, value, expected] :
         {std::tuple{"border-image-slice", "fill 1 2 1 2", "1 2 fill"},
          {"border-image-width", "1px 2% 3 AUTO", "1px 2% 3 auto"},
          {"border-image-outset", "0 0 0 0", "0"},
          {"border-image-source", "linear-gradient(red, blue)", "linear-gradient(red, blue)"}}) {
        block.clear();
        CHECK(supports_declaration(name, value));
        CHECK(set_declaration(block, name, value, false));
        CHECK_EQ(declaration_value(block, name), std::string{expected});
    }
    for (const auto & [name, value] : {std::pair{"border-image-slice", "1% fill 2%"},
                                       {"border-image-slice", "-1"},
                                       {"border-image-slice", "1px"},
                                       {"border-image-slice", "fill"},
                                       {"border-image-slice", "1 2 3 4 5"},
                                       {"border-image-width", "-1px"},
                                       {"border-image-width", "-1%"},
                                       {"border-image-width", "-1"},
                                       {"border-image-width", "1 2 3 4 5"},
                                       {"border-image-width", "calc(1deg)"},
                                       {"border-image-outset", "1%"},
                                       {"border-image-outset", "calc(1%)"},
                                       {"border-image-outset", "-1"},
                                       {"border-image-outset", "auto"},
                                       {"border-image-source", "auto"},
                                       {"border-image-source", "none, url(image.png)"},
                                       {"border-image-source", "url(a)url(b)"}}) {
        CHECK(!supports_declaration(name, value));
        CHECK(!set_declaration(block, name, value, false));
    }
}

void test_repeating_gradients() {
    using ctbrowser::style::css::computed_image;
    using ctbrowser::style::css::supports_declaration;
    for (const auto & [value, computed] :
         {std::pair{"repeating-linear-gradient(-45deg, red, 30%, currentcolor 70%, lime)",
                    "repeating-linear-gradient(-45deg, rgb(255, 0, 0), 30%, rgb(0, 0, 255) 70%, "
                    "rgb(0, 255, 0))"},
          {"repeating-radial-gradient(red, 30%, currentcolor 70%, lime)",
           "repeating-radial-gradient(rgb(255, 0, 0), 30%, rgb(0, 0, 255) 70%, rgb(0, 255, 0))"},
          {"repeating-conic-gradient(red, 30deg, currentcolor 70deg, lime)",
           "repeating-conic-gradient(rgb(255, 0, 0), 30deg, rgb(0, 0, 255) 70deg, rgb(0, 255, "
           "0))"}}) {
        for (const std::string_view property :
             {"border-image-source", "background-image", "mask-image", "list-style-image"}) {
            declaration_block block;
            CHECK(supports_declaration(property, value));
            CHECK(set_declaration(block, property, value, false));
            CHECK_EQ(declaration_value(block, property), std::string{value});
        }
        CHECK_EQ(computed_image(value, {"rgb(0, 0, 255)"}), std::string{computed});
    }
    for (const std::string_view function :
         {"repeating-linear-gradient", "repeating-radial-gradient", "repeating-conic-gradient"}) {
        for (const std::string_view stops :
             {"30%, red, blue", "red, 30%, 40%, blue", "red, blue, 30%"}) {
            const std::string value = std::string{function} + "(" + std::string{stops} + ")";
            CHECK(!supports_declaration("border-image-source", value));
        }
    }
    CHECK(
        !supports_declaration("background-image", "unknown(repeating-linear-gradient(red, blue))"));
    CHECK(
        !supports_declaration("background-image", "repeating-linear-gradient(red, unknown(blue))"));
}

// css/css-multicol/parsing/columns-*: width/count in either order and optional height.
void test_columns() {
    using ctbrowser::style::css::supports_declaration;
    for (const auto & [value, expected] : {std::pair{"auto 10em", "10em"},
                                           {"2 auto", "2"},
                                           {"auto auto", "auto"},
                                           {"0 1", "0px 1"},
                                           {"1 0", "0px 1"},
                                           {"2 10px/100px", "10px 2 / 100px"},
                                           {"auto / 100px", "auto / 100px"},
                                           {"10px 2 / auto", "10px 2"},
                                           {"auto / auto", "auto"},
                                           {"calc(20px / 2) / 100px", "calc(10px) / 100px"}}) {
        declaration_block block;
        CHECK(supports_declaration("columns", value));
        CHECK(set_declaration(block, "columns", value, false));
        CHECK_EQ(block.size(), std::size_t{4});
        CHECK_EQ(declaration_value(block, "column-wrap"), std::string{"auto"});
        CHECK_EQ(declaration_value(block, "columns"), std::string{expected});
    }
    for (const std::string_view invalid :
         {"initial initial", "inherit inherit", "auto auto auto", "10px 20px", "0 0", "/ 100px",
          "/ auto", "/", "auto /", "2 100px /", "auto // 100px", "auto / 100px / 200px",
          "auto / initial", "initial / auto", "auto / -1px", "auto / 10%", "wrap", "2 nowrap",
          "10px / 100px wrap"}) {
        declaration_block block;
        CHECK(!supports_declaration("columns", invalid));
        CHECK(!set_declaration(block, "columns", invalid, false));
        CHECK(block.empty());
    }
    declaration_block block;
    CHECK(set_declaration(block, "columns", "2 10px / 100px", true));
    CHECK_EQ(declaration_value(block, "column-width"), std::string{"10px"});
    CHECK_EQ(declaration_value(block, "column-count"), std::string{"2"});
    CHECK_EQ(declaration_value(block, "column-height"), std::string{"100px"});
    CHECK_EQ(declaration_priority(block, "column-height"), std::string{"important"});
    CHECK_EQ(declaration_priority(block, "column-wrap"), std::string{"important"});
    CHECK(set_declaration(block, "columns", "auto", false));
    CHECK_EQ(declaration_value(block, "column-height"), std::string{"auto"});
    CHECK(set_declaration(block, "columns", "inherit", false));
    CHECK_EQ(declaration_value(block, "column-height"), std::string{"inherit"});
    CHECK_EQ(declaration_value(block, "column-wrap"), std::string{"inherit"});
    CHECK_EQ(round_trip("columns: 2 10px / 100px"), std::string{"columns: 10px 2 / 100px;"});

    // column-wrap-reset-interpolation: the shorthand resets this longhand but cannot set it.
    for (const std::string_view value : {"auto", "wrap", "nowrap", "initial", "inherit"}) {
        CHECK(supports_declaration("column-wrap", value));
    }
    for (const std::string_view invalid : {"none", "wrap nowrap", "10px", "auto / wrap"}) {
        CHECK(!supports_declaration("column-wrap", invalid));
    }
    block.clear();
    CHECK(set_declaration(block, "columns", "100px / 100px", false));
    CHECK(set_declaration(block, "column-wrap", "NoWrAp", false));
    CHECK_EQ(declaration_value(block, "column-wrap"), std::string{"nowrap"});
    CHECK_EQ(declaration_value(block, "columns"), std::string{});
    CHECK_EQ(serialize_declaration_block(block),
             std::string{"column-width: 100px; column-count: auto; column-height: 100px; "
                         "column-wrap: nowrap;"});
    CHECK(set_declaration(block, "columns", "100px / 100px", false));
    CHECK_EQ(declaration_value(block, "column-wrap"), std::string{"auto"});
    CHECK_EQ(declaration_value(block, "columns"), std::string{"100px / 100px"});
    CHECK_EQ(round_trip("column-wrap: wrap; columns: 2"), std::string{"columns: 2;"});
    bool removed = false;
    CHECK_EQ(remove_declaration(block, "columns", removed), std::string{"100px / 100px"});
    CHECK(removed && block.empty());
}

// Background layer components reset omitted longhands and retain their own order.
void test_background() {
    using ctbrowser::style::css::supports_declaration;
    declaration_block block;
    CHECK(set_declaration(block, "background", "none", true));
    CHECK_EQ(block.size(), std::size_t{8});
    CHECK_EQ(declaration_value(block, "background-position"), std::string{"0% 0%"});
    CHECK_EQ(declaration_value(block, "background-size"), std::string{"auto"});
    CHECK_EQ(declaration_value(block, "background-origin"), std::string{"padding-box"});
    CHECK_EQ(declaration_value(block, "background-clip"), std::string{"border-box"});
    CHECK_EQ(declaration_priority(block, "background-attachment"), std::string{"important"});
    CHECK_EQ(declaration_value(block, "background"), std::string{"none"});
    CHECK(set_declaration(block, "background", "text border-area", false));
    CHECK_EQ(declaration_value(block, "background-origin"), std::string{"border-box"});
    CHECK_EQ(declaration_value(block, "background-clip"), std::string{"border-area text"});
    CHECK_EQ(declaration_value(block, "background"), std::string{"border-area text"});
    for (const auto & [value, expected] :
         {std::pair{"no-repeat url(a)", "url(\"a\") no-repeat"},
          {"url(a) 0% 0%/10rem", "url(\"a\") 0% 0% / 10rem auto"},
          {"padding-box border-box", "none"},
          {"border-area border-box", "border-area"},
          {"border-area padding-box", "padding-box border-area"},
          {"url(a) top left no-repeat, center/100% 100% white",
           "url(\"a\") left top no-repeat, center center / 100% 100% white"}}) {
        block.clear();
        CHECK(supports_declaration("background", value));
        CHECK(set_declaration(block, "background", value, false));
        CHECK_EQ(declaration_value(block, "background"), std::string{expected});
    }
    CHECK_EQ(declaration_value(block, "background-image"), std::string{"url(\"a\"), none"});
    CHECK_EQ(declaration_value(block, "background-repeat"), std::string{"no-repeat, repeat"});
    for (const std::string_view invalid :
         {"red, green", "black 0 url(a) / cover", "none none", "left /", "/ cover",
          "left / cover / auto", "red blue", "left red top", "initial none",
          "border-area border-area", "text text", "border-box padding-box content-box",
          "left / -1px", "none,", ",none", "none,,none"}) {
        block.clear();
        CHECK_EQ(supports_declaration("background", invalid) ? std::string{invalid} : std::string{},
                 std::string{});
        CHECK(!set_declaration(block, "background", invalid, false));
        CHECK(block.empty());
    }
    CHECK(!supports_declaration("background-position", "0 url(a)"));
    CHECK(!supports_declaration("background-position", "calc(1px) red"));
    CHECK(supports_declaration("background-position", "calc(1px + 2%) top"));
    block.clear();
    CHECK(set_declaration(block, "background", "var(--bg)", false));
    CHECK_EQ(declaration_value(block, "background"), std::string{"var(--bg)"});
    CHECK(set_declaration(block, "background", "inherit", false));
    CHECK_EQ(declaration_value(block, "background-size"), std::string{"inherit"});
    CHECK_EQ(round_trip("background: red !important; background: blue;"),
             std::string{"background: red !important;"});
}

} // namespace

int main() {
    test_shorthand_values();
    test_logical_groups();
    test_reads_and_all();
    test_border_radius();
    test_border_image();
    test_repeating_gradients();
    test_columns();
    test_background();
    REPORT("cssom_declarations");
}
