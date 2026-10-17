//===- ctcompile/test/Runtime/NativePage.cpp - Native page runtime tests --===//
//
// Tests the native runtime's page views (CTNative/Runtime/Page.hpp) and the
// element members css-dump reads (Browser.hpp: tagName, id, className,
// children), the tree walks and attribute calls Bootstrap's SelectorEngine and
// Manipulator make (parentElement, the element siblings, getAttribute,
// setAttribute, removeAttribute) against a ctbrowser::page::static_page: the answers generated
// code gets for getBoundingClientRect, getComputedStyle, clientWidth and
// devicePixelRatio, and the refusals for what it may not ask.
//
// Kept apart from NativeRuntime.cpp so that test links no page or layout
// library: only programs that read geometry or computed style need them.
//
//===----------------------------------------------------------------------===//

#define CTNATIVE_DOM 1
#include "ctcompile/CTNative/Runtime/Browser.hpp"
#include "ctcompile/CTNative/Runtime/Page.hpp"
#include "ctcompile/CTNative/Runtime/ctnative.hpp"

#include <ctbrowser/dom/element.hpp>
#include <ctbrowser/page/client_rects.hpp>
#include <ctbrowser/page/static_page.hpp>

#include <cstdio>
#include <stdexcept>
#include <string>

#include "check.hpp"

namespace {

constexpr std::string_view fixture =
    "<!doctype html><html><head><style>"
    "body { margin: 0 } #box { width: 50px; height: 20px; margin-left: 10px;"
    " background-color: red } p { color: rgb(0, 128, 0) }"
    "</style></head><body>"
    "<div id=\"box\" class=\"card  wide\"></div><p>text</p><svg><rect/></svg>"
    "</body></html>";

/// A page laid out at 200x100 with no scrollbar, and the scope that binds it.
struct bound_page {
    ctbrowser::page::static_page page{ctbrowser::page::static_page_options{200, 100, 0}};
    bound_page() {
        page.load_html(fixture);
        page.frame();
    }
};

[[nodiscard]] std::string text(const ctnative::js_string & s) {
    return s.value();
}

void test_element_members_match_the_vm() {
    bound_page bound;
    const ctnative::page_scope scope{bound.page};
    const auto root = ctnative::document.documentElement();
    CHECK(root.has_value());
    if (!root) { return; }
    // An HTML element in an HTML document reports its name uppercased.
    CHECK_EQ(text(root->tagName()), std::string{"HTML"});
    const std::vector<ctnative::js_element_t> top = root->children();
    CHECK_EQ(top.size(), std::size_t{2});
    if (top.size() == 2) {
        CHECK_EQ(text(top[0].tagName()), std::string{"HEAD"});
        CHECK_EQ(text(top[1].tagName()), std::string{"BODY"});
        // Element children only: the body's text-holding <p> counts, its text
        // node does not.
        const std::vector<ctnative::js_element_t> body = top[1].children();
        CHECK_EQ(body.size(), std::size_t{3});
        if (body.size() == 3) {
            CHECK_EQ(text(body[0].id()), std::string{"box"});
            // className is the attribute as written, spaces and all.
            CHECK_EQ(text(body[0].className()), std::string{"card  wide"});
            CHECK_EQ(text(body[1].id()), std::string{});
            CHECK_EQ(text(body[1].className()), std::string{});
            // An SVG element keeps its case.
            CHECK_EQ(text(body[2].tagName()), std::string{"svg"});
            CHECK_EQ(body[2].children().size(), std::size_t{1});
        }
    }
    // The unbound view a dead loop slot holds names no element: it equals no
    // bound view, and reading it throws rather than dereferencing.
    const ctnative::js_element_t unbound{};
    CHECK(!(unbound == *root));
    bool threw = false;
    try {
        (void)unbound.value();
    } catch (const std::exception &) { threw = true; }
    CHECK(threw);
}

[[nodiscard]] std::string tag_of(const std::optional<ctnative::js_element_t> & element) {
    return element ? text(element->tagName()) : std::string{"null"};
}

void test_tree_walks_match_the_vm() {
    bound_page bound;
    const ctnative::page_scope scope{bound.page};
    const auto root = *ctnative::document.documentElement();
    const auto box = *ctnative::document.querySelector(ctnative::js_string{"#box"});
    const auto svg = *ctnative::document.querySelector(ctnative::js_string{"svg"});
    const auto head = *ctnative::document.querySelector(ctnative::js_string{"head"});
    // The document element's parent is the Document, which is no element.
    CHECK_EQ(tag_of(root.parentElement()), std::string{"null"});
    CHECK_EQ(tag_of(box.parentElement()), std::string{"BODY"});
    CHECK_EQ(tag_of(box.parentElement()->parentElement()), std::string{"HTML"});
    // Siblings skip text nodes: the <p> holds text, the body holds only elements.
    CHECK_EQ(tag_of(box.previousElementSibling()), std::string{"null"});
    CHECK_EQ(tag_of(box.nextElementSibling()), std::string{"P"});
    CHECK_EQ(tag_of(box.nextElementSibling()->nextElementSibling()), std::string{"svg"});
    CHECK_EQ(tag_of(svg.nextElementSibling()), std::string{"null"});
    CHECK_EQ(tag_of(svg.previousElementSibling()), std::string{"P"});
    CHECK_EQ(tag_of(head.previousElementSibling()), std::string{"null"});
    CHECK_EQ(tag_of(head.nextElementSibling()), std::string{"BODY"});
    CHECK_EQ(tag_of(root.nextElementSibling()), std::string{"null"});
    // A member of null throws the VM's TypeError text.
    CHECK_EQ(text(ctnative::present_element(box.nextElementSibling()).tagName()), std::string{"P"});
    bool threw = false;
    try {
        (void)ctnative::present_element(box.previousElementSibling());
    } catch (const std::invalid_argument & error) {
        threw = std::string{error.what()} == "TypeError: Cannot read properties of null";
    }
    CHECK(threw);
}

void test_attributes_match_the_vm() {
    bound_page bound;
    const ctnative::page_scope scope{bound.page};
    const auto box = *ctnative::document.querySelector(ctnative::js_string{"#box"});
    using kind = ctnative::nullable_string::kind;
    const auto read = [&](std::string_view name) {
        return box.getAttribute(ctnative::js_string{std::string{name}});
    };
    CHECK_EQ(read("id").value, std::string{"box"});
    CHECK(read("id").tag == kind::string);
    // An absent attribute is null, not "".
    CHECK(read("data-bs-toggle").tag == kind::null_value);
    box.setAttribute(ctnative::js_string{"data-bs-toggle"}, ctnative::js_string{""});
    CHECK(read("data-bs-toggle").tag == kind::string);
    CHECK_EQ(read("data-bs-toggle").value, std::string{});
    // An HTML element in an HTML document lowercases the name both ways.
    box.setAttribute(ctnative::js_string{"DATA-BS-Target"}, ctnative::js_string{"#x"});
    CHECK_EQ(read("data-bs-target").value, std::string{"#x"});
    CHECK_EQ(read("DATA-bs-TARGET").value, std::string{"#x"});
    box.removeAttribute(ctnative::js_string{"Data-Bs-Target"});
    CHECK(read("data-bs-target").tag == kind::null_value);
    // Removing what is not there is not an error.
    box.removeAttribute(ctnative::js_string{"data-bs-target"});
    // An invalid name throws before writing, as the VM's InvalidCharacterError.
    bool threw = false;
    try {
        box.setAttribute(ctnative::js_string{"a b"}, ctnative::js_string{"x"});
    } catch (const std::exception &) { threw = true; }
    CHECK(threw);
    CHECK(read("a b").tag == kind::null_value);
}

void test_document_element_is_required() {
    // With a root, the unchecked read is the root element.
    {
        bound_page bound;
        const ctnative::page_scope scope{bound.page};
        CHECK_EQ(text(ctnative::require_document_element("tagName").tagName()),
                 std::string{"HTML"});
    }
    // A document with no root element throws the TypeError the VM throws at
    // the first read of the null root, with its message, not
    // std::bad_optional_access.
    ctbrowser::atom_table atoms;
    ctbrowser::document empty{atoms};
    ctbrowser::style::engine styles{atoms};
    const ctnative::document_scope scope{empty, styles};
    std::string message;
    try {
        (void)ctnative::require_document_element("children");
    } catch (const ctnative::type_error & error) { message = error.what(); }
    CHECK_EQ(message, std::string{"Cannot read properties of null (reading 'children')"});
}

void test_geometry_and_viewport() {
    bound_page bound;
    const ctnative::page_scope scope{bound.page};
    const auto box = ctnative::document.querySelector(ctnative::js_string{"#box"});
    CHECK(box.has_value());
    if (!box) { return; }
    const ctnative::js_dom_rect r = ctnative::getBoundingClientRect(*box);
    CHECK(r.left.value() == 10 && r.top.value() == 0);
    CHECK(r.width.value() == 50 && r.height.value() == 20);
    // The same rectangle ctbrowser::page answers for the VM's binding.
    const ctbrowser::rect page_rect = bound.page.bounding_client_rect(box->value().id);
    CHECK(r.width.value() == static_cast<double>(page_rect.width));
    // The field helpers generated code calls read the same fields.
    CHECK(ctnative::rect_left(r).value() == 10 && ctnative::rect_top(r).value() == 0);
    CHECK(ctnative::rect_width(r).value() == 50 && ctnative::rect_height(r).value() == 20);

    const auto root = *ctnative::document.documentElement();
    CHECK(ctnative::clientWidth(root).value() == 200);
    CHECK(ctnative::clientHeight(root).value() == 100);
    // Only the viewport element's client size is native so far.
    bool refused = false;
    try {
        (void)ctnative::clientWidth(*box);
    } catch (const std::logic_error &) { refused = true; }
    CHECK(refused);
    CHECK(ctnative::window.devicePixelRatio.value() == 1);
}

void test_computed_style_is_live() {
    bound_page bound;
    const ctnative::page_scope scope{bound.page};
    const auto box = *ctnative::document.querySelector(ctnative::js_string{"#box"});
    const auto p = *ctnative::document.querySelector(ctnative::js_string{"p"});
    const ctnative::js_computed_style style = ctnative::getComputedStyle(box);
    CHECK_EQ(text(style.getPropertyValue(ctnative::js_string{"width"})), std::string{"50px"});
    // The IDL spelling reads the same property.
    CHECK_EQ(text(style.getPropertyValue(ctnative::js_string{"backgroundColor"})),
             text(style.getPropertyValue(ctnative::js_string{"background-color"})));
    CHECK_EQ(text(ctnative::getComputedStyle(p).getPropertyValue(ctnative::js_string{"color"})),
             std::string{"rgb(0, 128, 0)"});
    CHECK_EQ(text(style.getPropertyValue(ctnative::js_string{"no-such-property"})), std::string{});
    // Live, as the VM's object is: a change laid out after the call is what a
    // later read sees.
    (void)ctbrowser::set_element_attribute(bound.page.doc(), box.value().id, "style",
                                           "width: 70px");
    bound.page.frame();
    CHECK_EQ(text(style.getPropertyValue(ctnative::js_string{"width"})), std::string{"70px"});
}

void test_what_may_not_be_asked() {
    bound_page bound;
    bound_page other;
    const auto foreign = [&] {
        const ctnative::page_scope scope{other.page};
        return *ctnative::document.documentElement();
    }();
    {
        const ctnative::page_scope scope{bound.page};
        // An element of another page's document.
        bool refused = false;
        try {
            (void)ctnative::getBoundingClientRect(foreign);
        } catch (const std::logic_error &) { refused = true; }
        CHECK(refused);
    }
    // No page bound at all.
    bool refused = false;
    try {
        (void)ctnative::page_scope::active();
    } catch (const std::logic_error &) { refused = true; }
    CHECK(refused);
}

} // namespace

int main() {
    test_element_members_match_the_vm();
    test_tree_walks_match_the_vm();
    test_attributes_match_the_vm();
    test_document_element_is_required();
    test_geometry_and_viewport();
    test_computed_style_is_live();
    test_what_may_not_be_asked();
    REPORT("native_page");
}
