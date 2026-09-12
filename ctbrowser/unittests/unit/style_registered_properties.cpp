// Registered custom properties, CSS Properties and Values API 1: `@property`
// in a sheet and `CSS.registerProperty()` from script, and what each does to
// the cascade - a syntax, an initial value, an inheritance flag, and a
// computed value of the type the syntax names.

#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "cssom_probe.hpp"
#include "style_fixture.hpp"

#include <string>
#include <string_view>

namespace {

void test_at_property_in_the_cascade() {
    {
        // A declared value is parsed against the syntax and computed like the
        // type: `1em` is pixels, a `calc()` is folded, and what does not parse
        // is the initial value.
        fixture f;
        f.load("<div id=outer><p id=a></p><p id=b></p></div>",
               "@property --length { syntax: \"<length>\"; inherits: false; initial-value: 3px }"
               "@property --number { syntax: '<number>'; inherits: true; initial-value: 3 }"
               "@property --any { syntax: '*'; inherits: true }"
               "p { font-size: 30px }"
               "#a { --length: 1em; --number: calc(1 + 2); --any: 1em 2 three }"
               "#b { --length: 3; --number: red }");
        expect_value(f, f.find_id("a"), "--length", "30px", "a length computes to pixels");
        expect_value(f, f.find_id("a"), "--number", "3", "a calc() folds");
        expect_value(f, f.find_id("a"), "--any", "1em 2 three", "`*` keeps the tokens");
        expect_value(f, f.find_id("b"), "--length", "3px", "a number is not a length");
        expect_value(f, f.find_id("b"), "--number", "3", "a colour is not a number");
    }
    {
        // Inheritance is the registration's to decide, and an undeclared
        // property has its initial value rather than nothing.
        fixture f;
        f.load("<div id=outer><p id=a></p></div>",
               "@property --own { syntax: \"<length>\"; inherits: false; initial-value: 3px }"
               "@property --shared { syntax: \"<length>\"; inherits: true; initial-value: 4px }"
               "#outer { --own: 30px; --shared: 40px }");
        expect_value(f, f.find_id("a"), "--own", "3px", "a non-inherited one starts over");
        expect_value(f, f.find_id("a"), "--shared", "40px", "an inherited one comes through");
        expect_value(f, f.find("html"), "--shared", "4px", "the root has the initial value");
    }
    {
        // `initial`, `unset` and `inherit` on a registered property, and a
        // var() of one reads its computed value.
        fixture f;
        f.load("<div id=outer><p id=a></p></div>",
               "@property --own { syntax: \"<length>\"; inherits: false; initial-value: 3px }"
               "#outer { --own: 30px } #a { --own: unset; width: var(--own) }");
        expect_value(f, f.find_id("a"), "--own", "3px",
                     "unset is initial when it does not inherit");
        expect_value(f, f.find_id("a"), "width", "3px", "and var() reads the computed value");
    }
    {
        // A substituted CSS-wide keyword is that keyword (attr-css-wide-keywords).
        fixture f;
        f.load("<div id=outer><p id=a data-i=inherit data-u=unset data-n=initial></p></div>",
               "@property --own { syntax: \"<length>\"; inherits: false; initial-value: 5px }"
               "@property --shared { syntax: \"<length>\"; inherits: true; initial-value: 5px }"
               "#outer { --own: 4px; --shared: 4px }"
               "#a { --own: attr(data-i type(*)); --shared: attr(data-u type(*)) }");
        expect_value(f, f.find_id("a"), "--own", "4px", "inherit from an attribute");
        expect_value(f, f.find_id("a"), "--shared", "4px", "unset inherits when it inherits");
        fixture g;
        g.load("<div id=outer><p id=a data-n=initial data-u=unset></p></div>",
               "@property --own { syntax: \"<length>\"; inherits: false; initial-value: 5px }"
               "@property --shared { syntax: \"<length>\"; inherits: true; initial-value: 5px }"
               "#outer { --own: 4px; --shared: 4px }"
               "#a { --own: attr(data-u type(*)); --shared: attr(data-n type(*)) }");
        expect_value(g, g.find_id("a"), "--own", "5px", "unset is initial when it does not");
        expect_value(g, g.find_id("a"), "--shared", "5px", "initial from an attribute");
    }
    {
        // A font-size reading a registered property written in `em` is a
        // cycle: the font-size is inherited and the property is its initial
        // value (typed_arithmetic_cycle).
        fixture f;
        f.load("<div id=a></div>",
               "@property --length { syntax: \"<length>\"; inherits: false; initial-value: 0px }"
               ":root { font-size: 228px }"
               "div { --length: calc(10px * (2em / 1em)); font-size: var(--length) }");
        expect_value(f, f.find_id("a"), "font-size", "228px", "the cycle inherits");
        expect_value(f, f.find_id("a"), "--length", "0px", "and the property is initial");
    }
    {
        // A rule missing a required descriptor registers nothing; the first
        // registration of a name stands.
        fixture f;
        f.load("<p id=a></p>",
               "@property --x { syntax: \"<length>\"; inherits: true }"
               "@property --y { syntax: \"<length>\"; inherits: true; initial-value: 1px }"
               "@property --y { syntax: \"<number>\"; inherits: true; initial-value: 2 }"
               "#a { --x: 5 }");
        expect_value(f, f.find_id("a"), "--x", "5", "unregistered: the tokens as written");
        expect_value(f, f.find_id("a"), "--y", "1px", "the first registration wins");
    }
}

void test_register_property_from_script() {
    using ctbrowser::shell::browser;
    using ctbrowser::shell::browser_options;
    using ctbrowser_test::logged;
    browser page{browser_options{400, 200}};
    page.load_html(R"html(<html><head><style>
      #t { --len: 1em; font-size: 20px; --if: if(style(--len: 20px): yes; else: no);
           --c: green; --cif: if(style(--c: rgb(0, 128, 0)): same; else: differs); }
    </style></head><body><div id=t></div>
    <script>
        const cs = () => getComputedStyle(document.getElementById('t'));
        const before = cs().getPropertyValue('--len');
        CSS.registerProperty({ name: '--len', syntax: '<length>', inherits: false, initialValue: '3px' });
        CSS.registerProperty({ name: '--c', syntax: '<color>', inherits: false, initialValue: 'blue' });
        const after = cs().getPropertyValue('--len') + '|' + cs().getPropertyValue('--c') + '|' +
                      cs().getPropertyValue('--cif');
        let errors = '';
        try { CSS.registerProperty({ name: '--len', syntax: '<length>', inherits: false, initialValue: '3px' }); }
        catch (e) { errors += e.name; }
        try { CSS.registerProperty({ name: 'len', syntax: '*', inherits: false }); }
        catch (e) { errors += ',' + e.name; }
        try { CSS.registerProperty({ name: '--typed', syntax: '<length>', inherits: false }); }
        catch (e) { errors += ',' + e.name; }
        try { CSS.registerProperty({ name: '--bad', syntax: '<length>', inherits: false, initialValue: 'red' }); }
        catch (e) { errors += ',' + e.name; }
        try { CSS.registerProperty({ name: '--x' }); }
        catch (e) { errors += ',' + e.name; }
        console.log('reg=' + [before, after, cs().getPropertyValue('--if'), errors].join('|'));
    </script></body></html>)html");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "reg="),
             std::string{
                 "reg=1em|20px|rgb(0, 128, 0)|same|yes|InvalidStateError,SyntaxError,SyntaxError,"
                 "SyntaxError,TypeError"});
}

} // namespace

int main() {
    test_at_property_in_the_cascade();
    test_register_property_from_script();
    REPORT("style_registered_properties");
}
