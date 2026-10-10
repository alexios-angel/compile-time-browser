//===- tree_accessors.cpp - DOM tree accessor tests -------------*- C++ -*-===//
//
// The HTML tree accessors and named access on the Document: `document.title`,
// the eight collections HTML hangs off the Document, and
// `document.someImgName`. Thirty-odd WPT files in `html/dom` cover these, and
// nearly all fail the same way if the implementation is a property pushed once
// per frame rather than an accessor: they write and then read back in the same
// statement, and a property refreshed a frame later answers with the value
// from before the write.
//
// So every case here does its write and its read inside one expression. That
// is not a stylistic choice; it is the thing under test.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

#include "check.hpp"
#include "dom_probe.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

/// A page with one of everything the accessors name, and the id/name pairs
/// `nameditem-01.html` uses to prove that the id route needs a name.
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

/// Checks that `expression`, evaluated against a fresh `page_html`, yields
/// `expected`. A fresh page per case, since many of these mutate the document.
void is(const std::string & expression, const std::string & expected) {
    ctbrowser_test::is_in(page_html, expression, expected);
}

// --- document.dir and the body colours --------------------------------------

void test_the_document_reflects_dir_and_the_body_colours() {
    // reflection-sections.html's #document rows: `dir` is the html element's,
    // limited to the known values; `fgColor` is the body's `text`, and null
    // writes "" ([LegacyNullToEmptyString]).
    is("(function () { var out = [document.dir]; document.dir = 'RTL'; out.push(document.dir,"
       " document.documentElement.getAttribute('dir')); document.dir = 'x';"
       " out.push(document.dir); return out.join(); })()",
       ",rtl,RTL,");
    is("(function () { var out = [document.fgColor]; document.fgColor = 'red';"
       " out.push(document.body.getAttribute('text')); document.bgColor = null;"
       " out.push(document.body.getAttribute('bgcolor')); return JSON.stringify(out); })()",
       "[\"\",\"red\",\"\"]");
}

// --- document.title --------------------------------------------------------

void test_the_title_is_stripped_and_collapsed() {
    // The getter normalises; "  a   title  " in the markup reads back with the
    // leading and trailing runs gone and the interior run down to one space.
    is("document.title", "a title");
    // The setter does not: the text node keeps what was written, which is
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
    // "The first title element in the document", not "the title in the head":
    // document.title-01.html removes the head and appends one to the body.
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
    // The setter creates a title when there is a head and no title.
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
    // A link is an <a> or an <area> that has an href. Three `<a>`s and one
    // `<area>` on the page, and only two of them carry one.
    is("document.links.length", "2");
    // An anchor is an <a> with a name, href or not.
    is("document.anchors.length", "2");
    // `applets` kept its property and lost its element.
    is("document.applets.length", "0");
    is("document.images[0].id", "ia");
    is("document.forms[0].getAttribute('name')", "theform");
}

void test_the_collections_are_live() {
    // This is why they are accessors over a proxy rather than arrays built
    // once: a page appends and reads the length again in the next statement.
    is("(function () {"
       " var before = document.images.length;"
       " document.body.appendChild(document.createElement('img'));"
       " return before + '->' + document.images.length; })()",
       "4->5");
    // The same collection object sees it, not just a freshly read one.
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

void test_get_elements_by_tag_name_knows_about_namespaces() {
    // An HTML element matches case-insensitively and a foreign one exactly,
    // which are the two branches DOM 4.5 gives for an HTML document. Foreign
    // content keeps its case, so `<linearGradient>` interns as written, and
    // lowercasing the search would make it findable by neither spelling.
    is("(function () {"
       " document.body.innerHTML ="
       "   '<svg><linearGradient id=g></linearGradient></svg>';"
       " return document.getElementsByTagName('linearGradient').length + ',' +"
       "        document.getElementsByTagName('lineargradient').length; })()",
       "1,0");
    is("document.getElementsByTagName('IMG').length + ',' +"
       " document.getElementsByTagName('img').length",
       "4,4");
    // `instanceof HTMLCollection` is a subtest of its own in three files.
    is("document.getElementsByTagName('img') instanceof HTMLCollection", "true");
    is("document.images instanceof HTMLCollection", "true");
    is("document.body.children instanceof HTMLCollection", "true");
    // An index is read-only: the assignment does not stick and the next read is
    // still what the walk finds.
    is("(function () {"
       " var c = document.getElementsByTagName('img');"
       " c[0] = 42;"
       " return c[0].tagName; })()",
       "IMG");
}

/// A live collection is a proxy, and it must still iterate: `for...of` and
/// spread see every element. A failure here is a silently empty list, not an
/// error, and an indexed `for (i = 0; i < n; i++)` loop would not notice it.
void test_a_live_collection_iterates() {
    is("(function () {"
       " var n = 0;"
       " for (var e of document.getElementsByTagName('img')) { n += 1; }"
       " return n; })()",
       "4");
    is("[...document.getElementsByTagName('img')].length", "4");
    is("Array.from(document.body.children).length > 0", "true");
    is("(function () {"
       " var tags = [];"
       " for (var e of document.body.children) { tags.push(e.tagName); }"
       " return tags.indexOf('IMG') >= 0; })()",
       "true");
}

// --- a bare identifier, which is the same rule one level up ----------------

/// HTML 7.3.3 makes an element with an `id` a named property of the global
/// object, and a bare identifier resolves against that object, so `ia.tagName`
/// with `ia` written nowhere but in the markup is the specified behaviour and
/// not a page being sloppy. web-platform-tests leans on it constantly;
/// `getComputedStyle(target1)` in six `css/cssom` files is exactly this.
void test_a_bare_identifier_finds_an_element() {
    is("ia.tagName", "IMG");
    is("ib.id", "ia");
    is("theform.tagName", "FORM");
    // Several of one name is a collection here too.
    is("anchor1.getAttribute('name')", "anchor1");
    // A declared name still wins: the hook is consulted only on a miss.
    is("(function () { var ia = 7; return ia; })()", "7");
    // A name that is neither a global nor an element is an unresolvable
    // reference: `typeof` says undefined, a read throws ReferenceError.
    is("typeof nosuchnameanywhere", "undefined");
    is("(function () { try { nosuchnameanywhere; } catch (e) { return e.constructor.name; } })()",
       "ReferenceError");
    // The window is the global object, so a bare `toString` is
    // Object.prototype's, as in every browser.
    is("typeof toString", "function");
}

// --- named access on the Document ------------------------------------------

void test_an_element_answers_to_its_name_and_its_id() {
    is("document.ib.id", "ia");
    is("document['ib'].id", "ia");
    is("document.ia.getAttribute('name')", "ib");
    // Same value in both attributes is one element, not two.
    is("document.same.id", "same");
    // A form and an embed answer to their name.
    is("document.theform.tagName", "FORM");
    is("document.theembed.tagName", "EMBED");
}

void test_the_id_route_needs_a_name_and_the_name_route_does_not() {
    // An `<img>` with an id and no name is not a named property: the id route
    // is conditional on a non-empty name, which is the asymmetry that makes
    // nameditem-01's third and fourth cases differ.
    is("document.idonly === undefined", "true");
    is("document.nameonly.tagName", "IMG");
    // Removing the name takes both spellings away, because the id route needed
    // it. Removing the id leaves the name alone.
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
    // The named getter is a fallback. `document.forms` is the collection
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
    // The named getter does not hide inherited members such as hasOwnProperty;
    // feature detection is the commonest thing a page does with a host object.
    is("typeof document.hasOwnProperty", "function");
    is("document.hasOwnProperty('nosuchname')", "false");
    // Nor an inherited property: `constructor` and a null handler property stay
    // what Document.prototype says (nameditem-no-shadowing), and an <iframe>
    // alone answers its WindowProxy rather than the element (nameditem-02).
    is("(function () {"
       " var i = document.createElement('img'); i.setAttribute('name', 'constructor');"
       " var j = document.createElement('img'); j.setAttribute('name', 'onreadystatechange');"
       " document.body.appendChild(i); document.body.appendChild(j);"
       " return (document.constructor === Document) + ',' + document.onreadystatechange; })()",
       "true,null");
    is("(function () {"
       " var f = document.createElement('iframe'); f.setAttribute('name', 'fr');"
       " document.body.appendChild(f); return document.fr === f.contentWindow; })()",
       "true");
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
    // Not the first <body> anywhere: HTML says the first child of the document
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
    // The page's `document` is a Proxy. Everything that hands one back has
    // to hand back that value, or a page's identity comparison fails against
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
    test_the_document_reflects_dir_and_the_body_colours();
    test_the_title_is_stripped_and_collapsed();
    test_the_title_element_is_found_wherever_it_is();
    test_the_collections_count_what_they_name();
    test_the_collections_are_live();
    test_get_elements_by_tag_name_knows_about_namespaces();
    test_a_live_collection_iterates();
    test_a_bare_identifier_finds_an_element();
    test_an_element_answers_to_its_name_and_its_id();
    test_the_id_route_needs_a_name_and_the_name_route_does_not();
    test_a_name_never_shadows_a_real_property();
    test_several_of_one_name_is_a_collection();
    test_the_body_is_the_document_elements_child();
    test_the_document_is_still_itself_through_the_proxy();
    REPORT("tree_accessors");
}
