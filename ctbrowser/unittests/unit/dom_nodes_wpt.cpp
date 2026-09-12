// THE dom/nodes CORNERS THAT A DIFF, A HANDLE OR A BYTE COUNT GOT WRONG.
//
// Each case here stood behind a failing web-platform-tests file in dom/nodes,
// and each is the smallest expression that fails if the fix goes: the document
// element's parentNode, a same-value attribute write still queueing a
// mutation record, an Attr keeping its identity, a surrogate pair being split
// on a UTF-16 offset, a collection's indices being its own properties, and
// `createElement("f:oo")` keeping the colon in its local name.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><body>
<div id=outer><p id=p1 class="a">one</p></div>
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
    if (got != expected) { std::printf("  for: %s\n", expression.c_str()); }
}

// The document element sits in the Document node's child list with no
// parent pointer (document::document_node), and the page must not see that.
void test_the_document_element_has_a_parent_and_siblings() {
    is("document.documentElement.parentNode === document", "true");
    is("document.documentElement.previousSibling === document.doctype", "true");
    is("document.doctype.nextSibling === document.documentElement", "true");
    is("document.documentElement.parentElement === null", "true");
    // ...and a node of ANOTHER document is DISCONNECTED, consistently.
    is(R"JS((function () {
        var other = document.implementation.createHTMLDocument('');
        var a = document.getElementById('p1'), b = other.body;
        var ab = a.compareDocumentPosition(b), ba = b.compareDocumentPosition(a);
        return (ab & 33) === 33 && (ba & 33) === 33 && ((ab ^ ba) & 6) === 6;
    })())JS",
       "true");
}

// A diff of before against after cannot see `setAttribute("class", "a")` on
// an element whose class is already "a"; the write log can.
void test_a_write_that_changes_nothing_still_queues_a_record() {
    is(R"JS((function () {
        var p = document.getElementById('p1');
        var seen = [];
        var m = new MutationObserver(function () {});
        m.observe(p, { attributes: true, attributeOldValue: true, characterData: true,
                       characterDataOldValue: true, subtree: true });
        p.setAttribute('class', 'a');
        p.classList.remove('missing');
        p.firstChild.appendData('');
        var records = m.takeRecords();
        return records.map(function (r) { return r.type + ':' + r.oldValue; }).join(',');
    })())JS",
       "attributes:a,attributes:a,characterData:one");
    // The record names the LOCAL name and the namespace.
    is(R"JS((function () {
        var p = document.getElementById('p1');
        var m = new MutationObserver(function () {});
        m.observe(p, { attributes: true });
        p.setAttributeNS('http://example.org/', 'x:lang', 'v');
        var r = m.takeRecords()[0];
        return r.attributeName + ' ' + r.attributeNamespace;
    })())JS",
       "lang http://example.org/");
}

void test_an_attr_keeps_its_identity_and_loses_its_owner_when_removed() {
    is(R"JS((function () {
        var p = document.getElementById('p1');
        var a = p.getAttributeNode('class');
        var same = a === p.attributes[1] && a === p.attributes.getNamedItem('class');
        p.removeAttribute('class');
        return same + ',' + (a.ownerElement === null) + ',' + a.value;
    })())JS",
       "true,true,a");
    is(R"JS((function () {
        var el = document.createElement('div'), other = document.createElement('div');
        var attr = document.createAttribute('foo');
        el.setAttributeNode(attr);
        try { other.setAttributeNode(attr); return 'no throw'; } catch (e) { return e.name; }
    })())JS",
       "InUseAttributeError");
    is("document.createAttribute('x').cloneNode() instanceof Attr", "true");
}

// Offsets are UTF-16 code units, and one may fall between the halves of a
// surrogate pair: the lone half survives (as WTF-8) and rejoins on the other
// side of a replaceData.
void test_a_surrogate_pair_can_be_split_and_rejoined() {
    is(R"JS((function () {
        var t = document.createTextNode('🌠 test');
        var head = t.substringData(1, 1) === '\uDF20';
        t.replaceData(1, 1, '\uDF1F');
        return head + ',' + (t.data === '🌟 test') + ',' + t.length;
    })())JS",
       "true,true,7");
}

// WebIDL's legacy platform object: indices and exposed names are own
// properties, `item` is inherited and an expando may shadow it.
void test_a_collection_owns_its_indices_and_names() {
    is(R"JS((function () {
        var list = document.getElementsByTagName('p');
        var keys = Object.getOwnPropertyNames(list).sort().join(' ');
        var own = list.hasOwnProperty('0') && list.hasOwnProperty('p1') && !('x' in list);
        var proto = list.item === HTMLCollection.prototype.item;
        list.item = 'shadowed';
        list[0] = 'ignored';
        return keys + '|' + own + '|' + proto + '|' + list.item + '|' + (list[0].id);
    })())JS",
       "0 p1|true|true|shadowed|p1");
    is(R"JS((function () {
        var d = document.getElementById('outer');
        var kids = d.childNodes;
        d.appendChild(document.createElement('span'));
        return (kids === d.childNodes) + ',' + kids.length + ',' +
               (kids instanceof NodeList) + ',' + (document.querySelectorAll('p') instanceof NodeList);
    })())JS",
       "true,2,true,true");
    // NodeList-live-mutations: the own keys after a mutation nothing read
    // through, and NodeList-Iterable: a for-of sees what its body appends.
    is(R"JS((function () {
        var d = document.createElement('div');
        var kids = d.childNodes;
        var before = Object.getOwnPropertyNames(kids).length;
        d.appendChild(document.createElement('b')).id = 'b1';
        d.appendChild(document.createElement('b')).id = 'b2';
        var after = Object.getOwnPropertyNames(kids).join();
        var seen = [];
        for (var el of kids) {
            seen.push(el.id);
            if (seen.length < 3) { d.appendChild(document.createElement('b')).id = 'after' + el.id; }
        }
        var keys = [...kids.keys()], entries = [...kids.entries()];
        return [before, after, seen.join(' '), keys.join(' '), entries[1][1].id,
                !(kids.values() instanceof Array), kids.values().next().value === kids[0]].join('|');
    })())JS",
       "0|0,1|b1 b2 afterb1 afterb2|0 1 2 3|b2|true|true");
}

void test_a_colon_is_a_prefix_only_when_it_was_made_as_one() {
    is("document.createElement('f:oo').localName", "f:oo");
    is("document.createElement('f:oo').prefix === null", "true");
    is("document.createElementNS('http://www.w3.org/1999/xhtml', 'f:oo').localName", "oo");
    is("document.createElementNS('http://www.w3.org/1999/xhtml', 'f:oo').prefix", "f");
    is("document.createElementNS('http://www.w3.org/1999/xhtml', 'x:span') instanceof "
       "HTMLSpanElement",
       "true");
}

void test_outer_html_both_ways() {
    is("document.getElementById('p1').outerHTML", "<p id=\"p1\" class=\"a\">one</p>");
    is(R"JS((function () {
        var p = document.getElementById('p1');
        p.outerHTML = '<b id="b1">x</b><i></i>';
        return document.getElementById('p1') + ',' + document.getElementById('outer').innerHTML;
    })())JS",
       "null,<b id=\"b1\">x</b><i></i>");
}

} // namespace

// DOM 5's Range, the shape MutationObserver-childList.html uses it in:
// boundaries, toString, deleteContents across a text edge, extractContents
// into a fragment, insertNode splitting a text node, surroundContents.
void test_a_range_cuts_moves_and_wraps() {
    is("(function () { var d = document.getElementById('outer');"
       " d.innerHTML = '<b>ab</b>cd<i>ef</i>'; var r = document.createRange();"
       " r.setStart(d.firstChild.firstChild, 1); r.setEnd(d.childNodes[1], 1);"
       " var s = r.toString(); r.deleteContents();"
       " return s + '|' + d.innerHTML + '|' + r.collapsed + '|' + (r.startContainer === d)"
       " + r.startOffset; })()",
       "bc|<b>a</b>d<i>ef</i>|true|true1");
    is("(function () { var d = document.getElementById('outer');"
       " d.innerHTML = '<b>ab</b><u>cd</u><i>ef</i>'; var r = new Range();"
       " r.setStartBefore(d.childNodes[1]); r.setEndAfter(d.childNodes[1]);"
       " var f = r.extractContents(); return f.childNodes.length + f.firstChild.tagName + '|'"
       " + d.innerHTML + '|' + r.commonAncestorContainer.id; })()",
       "1U|<b>ab</b><i>ef</i>|outer");
    is("(function () { var d = document.getElementById('outer'); d.textContent = 'abcd';"
       " var r = document.createRange(); r.setStart(d.firstChild, 2); r.collapse(true);"
       " r.insertNode(document.createElement('br')); var out = d.innerHTML + '|' + r.endOffset;"
       " r.selectNodeContents(d); r.surroundContents(document.createElement('s'));"
       " return out + '|' + d.innerHTML; })()",
       "ab<br>cd|2|<s>ab<br>cd</s>");
}

// adoption.window.js and Node-isEqualNode-xhtml.xhtml: a fragment inserted
// from another document is emptied there and stays there; adoptNode takes the
// fragment itself; and isEqualNode reads across documents.
void test_another_documents_fragment_and_nodes() {
    is(R"JS((function () {
        var doc = document.implementation.createHTMLDocument('');
        var df = doc.createDocumentFragment();
        var child = df.appendChild(doc.createTextNode('hi'));
        document.body.appendChild(df);
        var a = [df.childNodes.length, child.ownerDocument === document, df.ownerDocument === doc];
        var df2 = doc.createDocumentFragment();
        var kid2 = df2.appendChild(doc.createElement('i'));
        document.adoptNode(df2);
        a.push(df2.childNodes.length, df2.ownerDocument === document, kid2.ownerDocument === document);
        var p = doc.createElement('p'); p.setAttribute('class', 'a');
        var p1 = document.getElementById('p1');
        a.push(p1.isEqualNode(p), p.isEqualNode(p1));
        p.textContent = 'one';
        a.push(p1.isEqualNode(p), p1.isEqualNode(null));
        return a.join();
    })())JS",
       "0,true,true,1,true,true,false,false,true,false");
}

int main() {
    test_another_documents_fragment_and_nodes();
    test_a_range_cuts_moves_and_wraps();
    test_the_document_element_has_a_parent_and_siblings();
    test_a_write_that_changes_nothing_still_queues_a_record();
    test_an_attr_keeps_its_identity_and_loses_its_owner_when_removed();
    test_a_surrogate_pair_can_be_split_and_rejoined();
    test_a_collection_owns_its_indices_and_names();
    test_a_colon_is_a_prefix_only_when_it_was_made_as_one();
    test_outer_html_both_ways();
    REPORT("dom_nodes_wpt");
}
