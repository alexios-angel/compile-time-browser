//===- ctbrowser/unittests/unit/xml/document.cpp - XML document tests -----===//
//
// A page loaded as XML, from the bindings' side.
//
// unittests/unit/xml/parse.cpp proves the parser builds the right tree. This
// proves the document a script sees is an XML one: `contentType`,
// `compatMode`, a `<script>` written inside a marked section actually runs,
// and the case a tag was written in survives all the way to `tagName`.
//
// Every case loads through `load_document(..., source_kind::xml)`, which is
// the same route `ctdrive page.xhtml` takes: the extension picks the front end
// and nothing sniffs the bytes. See dom/xml.hpp.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

constexpr const char * xhtml_page = R"(<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml">
<head><title>an xml page</title></head>
<body>
<p id="parentEl" style="font-weight:bold;">Test.</p>
<i id="mixed">x</i>
<script><![CDATA[
window.__answers = [];
function say(text) { window.__answers.push(String(text)); }
]]></script>
</body>
</html>)";

/// Loads `xhtml_page` as XML with a trailing script that passes `expression`
/// to `say` and logs it; returns the last logged line, `threw:<name>`, or a
/// "<nothing logged: ...>" note carrying the script and XML errors.
[[nodiscard]] std::string answer(const std::string & expression) {
    browser page{browser_options{400, 300}};
    std::string source{xhtml_page};
    const std::string tail = "<script><![CDATA[\ntry { say(" + expression +
                             "); } catch (e) { say('threw:' + e.name); }\nconsole.log("
                             "window.__answers[window.__answers.length - 1]);\n]]></script>\n";
    source.insert(source.find("</body>"), tail);
    page.load_document(source, browser::source_kind::xml);
    const std::vector<std::string> & logged = page.bindings().console_output();
    if (logged.empty()) {
        return "<nothing logged: " + page.script_error() + "|" + page.xml_error() + ">";
    }
    return logged.back();
}

/// Checks answer(expression) against `expected`, printing the expression on a
/// mismatch.
void is(const std::string & expression, const std::string & expected) {
    const std::string got = answer(expression);
    CHECK_EQ(got, expected);
    if (got != expected) { std::printf("    %s\n", expression.c_str()); }
}

/// A <script> body inside a CDATA marked section must reach the script
/// compiler as plain text. If the markers leaked through, the helper `say`
/// would not be defined and every other case here would report nothing (the
/// twelve WPT Element-*-xhtml.xhtml files depend on this).
void test_a_script_in_a_marked_section_runs() {
    is("typeof say", "function");
    is("1 < 2", "true");
}

/// The WPT cases `document-compatmode-06.xhtml` and `Document-contentType`.
void test_the_document_says_what_it_is() {
    is("document.contentType", "application/xhtml+xml");
    is("document.compatMode", "CSS1Compat");
}

/// `Node-nodeName-xhtml.xhtml`: an XHTML `<i>` is `i`, not `I`. `tagName`
/// uppercases only HTML elements in an HTML document; in an XML document the
/// name is reported as written.
void test_case_survives_to_the_binding() {
    is("document.getElementById('mixed').tagName", "i");
    is("document.getElementById('mixed').nodeName", "i");
}

/// The element accessors the twelve WPT `Element-*-xhtml.xhtml` files test,
/// which only run once the marked-section script compiles.
void test_the_element_accessors_answer() {
    is("document.getElementById('parentEl').firstElementChild === null", "true");
    is("document.getElementById('parentEl').lastElementChild === null", "true");
    is("document.getElementById('parentEl').childElementCount", "0");
    is("document.documentElement.tagName", "html");
}

/// A document that is not well-formed does not load, and the engine says why
/// rather than rendering half of it silently.
void test_a_malformed_document_reports_its_error() {
    browser page{browser_options{400, 300}};
    page.load_document("<a><b></a></b>", browser::source_kind::xml);
    CHECK(!page.xml_error().empty());
    browser good{browser_options{400, 300}};
    good.load_document("<a>x</a>", browser::source_kind::xml);
    CHECK(good.xml_error().empty());
}

/// `createDocument` makes an XML document too, and DOM 4.5.1 decides its
/// content type from the namespace rather than from the tree.
void test_create_document_is_xml() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><script>
        var a = document.implementation.createDocument(null, 'foo', null);
        var b = document.implementation.createDocument(
            'http://www.w3.org/1999/xhtml', 'html', null);
        var c = document.implementation.createDocument('http://www.w3.org/2000/svg', 'svg', null);
        console.log(a.contentType + ',' + b.contentType + ',' + c.contentType +
                    ',' + a.compatMode + ',' + a.documentElement.tagName);
      </script></body></html>)");
    CHECK(page.script_error().empty());
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (!logged.empty()) {
        CHECK_EQ(logged.back(),
                 std::string{"application/xml,application/xhtml+xml,image/svg+xml,CSS1Compat,foo"});
    }
}

} // namespace

int main() {
    test_a_script_in_a_marked_section_runs();
    test_the_document_says_what_it_is();
    test_case_survives_to_the_binding();
    test_the_element_accessors_answer();
    test_a_malformed_document_reports_its_error();
    test_create_document_is_xml();
    REPORT("xml_document");
}
