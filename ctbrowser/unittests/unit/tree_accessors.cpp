// THE HTML TREE ACCESSORS AND NAMED ACCESS ON THE DOCUMENT.
//
// `document.title`, the eight collections HTML hangs off the Document, and
// `document.someImgName`. Thirty-odd files in `html/dom` are about these and
// nearly all of them fail the same way when the implementation is a property
// pushed on the tick rather than an accessor: they WRITE and then READ BACK in
// the same statement, and a property refreshed a frame later answers with the
// value from before the write.
//
// So every case here does its write and its read inside ONE expression. That is
// not a stylistic choice - it is the thing under test.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

// A page with one of everything the accessors name, and the id/name pairs
// `nameditem-01.html` uses to prove that the id route needs a name.
constexpr const char * page_html = R"(<!DOCTYPE html>
<html><head><title>  a   title  </title></head><body>
<img id=ia name=ib>
<img id=same name=same>
<img id=idonly>
<img name=nameonly>
<form name=theform></form>
<embed name=theembed>
<a href="/one" name=anchor1>one</a>
<a name=anchor2>two</a>
<a>three</a>
<area href="/two">
<script id=s1></script>
</body></html>)";

[[nodiscard]] std::string answer(const std::string & expression) {
    browser page{browser_options{400, 300}};
    std::string html{page_html};
    const std::string tail = "<script>try { console.log(String(" + expression +
                             ")); } catch (e) { console.log('threw:' + e.name); }</script>";
    html.insert(html.find("</body>"), tail);
    page.load_html(html);
    const std::vector<std::string> & logged = page.bindings().console_output();
    if (logged.empty()) { return "<nothing logged: " + page.script_error() + ">"; }
    return logged.back();
}

void is(const std::string & expression, const std::string & expected) {
    const std::string got = answer(expression);
    CHECK_EQ(got, expected);
    if (got != expected) { std::printf("    %s\n", expression.c_str()); }
}

// --- document.title --------------------------------------------------------

void test_the_title_is_stripped_and_collapsed() {
    // The GETTER normalises; "  a   title  " in the markup reads back with the
    // leading and trailing runs gone and the interior run down to one space.
    is("document.title", "a title");
    // And the SETTER does not - the text node keeps what was written, which is
    // why reading it back through the getter collapses again rather than
    // returning what was stored.
    is("(function () { document.title = 'two  spaces'; return document.title; })()", "two spaces");
    is("(function () { document.title = 'one\\ttab'; return document.title; })()", "one tab");
    is("(function () { document.title = 'two\\n\\nnewlines'; return document.title; })()",
       "two newlines");
    // The write is visible to the DOM, not only to the accessor.
    is("(function () { document.title = 'x'; "
       "return document.getElementsByTagName('title')[0].textContent; })()",
       "x");
}

void test_the_title_element_is_found_wherever_it_is() {
    // "The first title element in the document", not "the title in the head" -
    // document.title-01.html removes the head and appends one to the BODY.
    is("(function () {"
       " var head = document.getElementsByTagName('head')[0];"
       " head.parentNode.removeChild(head);"
       " return document.title; })()",
       "");
    // With no title and no head there is nowhere to put one, so the setter
    // does nothing at all and the title stays empty.
    is("(function () {"
       " var head = document.getElementsByTagName('head')[0];"
       " head.parentNode.removeChild(head);"
       " document.title = 'FAIL';"
       " return document.title; })()",
       "");
    is("(function () {"
       " var head = document.getElementsByTagName('head')[0];"
       " head.parentNode.removeChild(head);"
       " var t = document.createElement('title');"
       " t.appendChild(document.createTextNode('PASS'));"
       " document.body.appendChild(t);"
       " return document.title; })()",
       "PASS");
    // AND THE SETTER CREATES ONE when there is a head and no title.
    is("(function () {"
       " var t = document.getElementsByTagName('title')[0];"
       " t.parentNode.removeChild(t);"
       " document.title = 'made';"
       " return document.title + '/' + document.getElementsByTagName('title').length; })()",
       "made/1");
}

// --- the collections -------------------------------------------------------

void test_the_collections_count_what_they_name() {
    is("document.images.length", "4");
    is("document.forms.length", "1");
    is("document.embeds.length", "1");
    // `plugins` is `embeds` under a second name, which is what the
    // specification says and what document.embeds-document.plugins-01 checks.
    is("document.plugins.length", "1");
    // A LINK IS AN <a> OR AN <area> THAT HAS AN href. Three `<a>`s and one
    // `<area>` on the page, and only two of them carry one.
    is("document.links.length", "2");
    // AN ANCHOR IS AN <a> WITH A name, href or not.
    is("document.anchors.length", "2");
    // `applets` kept its property and lost its element.
    is("document.applets.length", "0");
    is("document.images[0].id", "ia");
    is("document.forms[0].getAttribute('name')", "theform");
}

void test_the_collections_are_live() {
    // The whole reason they are accessors over a proxy rather than arrays built
    // once: a page appends and reads the length again in the next statement.
    is("(function () {"
       " var before = document.images.length;"
       " document.body.appendChild(document.createElement('img'));"
       " return before + '->' + document.images.length; })()",
       "4->5");
    // And the same COLLECTION OBJECT sees it, not just a freshly read one.
    is("(function () {"
       " var all = document.forms;"
       " document.body.appendChild(document.createElement('form'));"
       " return all.length; })()",
       "2");
    // A link that gains an href joins the collection.
    is("(function () {"
       " var before = document.links.length;"
       " document.getElementsByTagName('a')[1].setAttribute('href', '/three');"
       " return before + '->' + document.links.length; })()",
       "2->3");
}

// --- named access on the Document ------------------------------------------

void test_an_element_answers_to_its_name_and_its_id() {
    is("document.ib.id", "ia");
    is("document['ib'].id", "ia");
    is("document.ia.getAttribute('name')", "ib");
    // Same value in both attributes is one element, not two.
    is("document.same.id", "same");
    // A form and an embed answer to their NAME.
    is("document.theform.tagName", "FORM");
    is("document.theembed.tagName", "EMBED");
}

void test_the_id_route_needs_a_name_and_the_name_route_does_not() {
    // An `<img>` with an id and no name is NOT a named property: the id route
    // is conditional on a non-empty name, which is the asymmetry that makes
    // nameditem-01's third and fourth cases differ.
    is("document.idonly === undefined", "true");
    is("document.nameonly.tagName", "IMG");
    // Removing the NAME takes both spellings away, because the id route needed
    // it. Removing the ID leaves the name alone.
    is("(function () {"
       " document.ia.removeAttribute('name');"
       " return (document.ia === undefined) + ',' + (document.ib === undefined); })()",
       "true,true");
    is("(function () {"
       " var img = document.ia;"
       " img.removeAttribute('id');"
       " return (document.ia === undefined) + ',' + (document.ib === img); })()",
       "true,true");
}

void test_a_name_never_shadows_a_real_property() {
    // The named getter is a FALLBACK. `document.forms` is the collection
    // accessor even with a `<form name=forms>` on the page, and
    // `document.body` is the body even with an `<img name=body>`.
    is("(function () {"
       " var f = document.createElement('form');"
       " f.setAttribute('name', 'forms');"
       " document.body.appendChild(f);"
       " return typeof document.forms.length; })()",
       "number");
    is("(function () {"
       " var i = document.createElement('img');"
       " i.setAttribute('name', 'title');"
       " document.body.appendChild(i);"
       " return document.title; })()",
       "a title");
    // ...and it does not break the prototype chain either, which is the defect
    // the window proxy had: feature detection is the commonest thing a page
    // does with a host object.
    is("typeof document.hasOwnProperty", "function");
    is("document.hasOwnProperty('nosuchname')", "false");
}

void test_several_of_one_name_is_a_collection() {
    is("(function () {"
       " var i = document.createElement('img');"
       " i.setAttribute('name', 'ib');"
       " document.body.appendChild(i);"
       " return document.ib.length; })()",
       "2");
}

// --- documentElement and body ----------------------------------------------

void test_the_body_is_the_document_elements_child() {
    is("document.body.tagName", "BODY");
    is("document.documentElement.tagName", "HTML");
    // NOT the first <body> anywhere: HTML says the first CHILD of the document
    // element that is a body or a frameset, and Document.body.html builds one
    // inside a <div> to prove the difference.
    is("(function () {"
       " var d = document.createElement('div');"
       " var b = document.createElement('body');"
       " d.appendChild(b);"
       " document.body.appendChild(d);"
       " return document.body.tagName + ',' + (document.body === b); })()",
       "BODY,false");
    // Assigning anything that is not a body or a frameset is a
    // HierarchyRequestError, and null is not one of them.
    is("(function () { document.body = document.createElement('div'); })()",
       "threw:HierarchyRequestError");
    is("(function () { document.body = null; })()", "threw:HierarchyRequestError");
    is("(function () {"
       " var b = document.createElement('body');"
       " b.setAttribute('id', 'fresh');"
       " document.body = b;"
       " return document.body.id + ',' + (document.body === b); })()",
       "fresh,true");
}

void test_the_document_is_still_itself_through_the_proxy() {
    // The page's `document` is a Proxy now. Everything that hands one back has
    // to hand back THAT value, or a page's identity comparison fails against
    // the object it was just given.
    is("document.getRootNode() === document", "true");
    is("document.body.ownerDocument === document", "true");
    is("window.document === document", "true");
    is("document.nodeType", "9");
    is("'ib' in document", "true");
    is("'nosuchname' in document", "false");
}

} // namespace

int main() {
    test_the_title_is_stripped_and_collapsed();
    test_the_title_element_is_found_wherever_it_is();
    test_the_collections_count_what_they_name();
    test_the_collections_are_live();
    test_an_element_answers_to_its_name_and_its_id();
    test_the_id_route_needs_a_name_and_the_name_route_does_not();
    test_a_name_never_shadows_a_real_property();
    test_several_of_one_name_is_a_collection();
    test_the_body_is_the_document_elements_child();
    test_the_document_is_still_itself_through_the_proxy();
    REPORT("tree_accessors");
}
