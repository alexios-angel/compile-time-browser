// THE NODE AND ELEMENT METHOD SURFACE: walking and editing the tree, the live
// collections, the constructible DOM (`createComment`, fragments, the insertion
// methods, `document.implementation`, `createElementNS`), `querySelector` on an
// element, and the four ways of putting markup or text into one - `innerHTML`,
// `innerText`, `insertAdjacentHTML` and its two sibling spellings.
//
// One of eight files carved out of unittests/unit/bindings_basics.cpp on
// 2026-09-07, when it had reached 3,365 lines. Every case is verbatim and in
// the order it had; the helpers more than one of the eight needs are in
// page_probe.hpp beside this, and `find_id` is test/support/dom_probe.hpp's.

#include <ctbrowser/app/app.hpp>
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "page_probe.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;
using ctbrowser_test::check;
using ctbrowser_test::log_of;

namespace {

// parentNode / remove / insertBefore - WALKING the tree, not just editing it.
//
// appendChild and removeChild already worked; nothing could find a parent. That
// makes `this.elt.parentNode.removeChild(this.elt)` - the ordinary way to take
// an element out of a page - throw, which is how p5.js's discarded default
// canvas stayed in the document underneath the real one.
void test_tree_navigation() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><div id=box><span id=a></span><span id=b></span></div><script>
        const box = document.getElementById('box');
        const a = document.getElementById('a');
        console.log('parent=' + a.parentNode.id + ',' + a.parentElement.id);
        console.log('children=' + box.children.length);
        // The idiom that needs both halves.
        a.parentNode.removeChild(a);
        console.log('removed=' + box.children.length + ',' + (document.getElementById('a') === null));
        const c = document.createElement('span');
        c.id = 'c';
        box.insertBefore(c, document.getElementById('b'));
        console.log('inserted=' + box.children[0].id + ',' + box.children[1].id);
        document.getElementById('b').remove();
        console.log('self=' + box.children.length);
    </script></body></html>)");
    check(page.script_error().empty(), "the navigation script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "parent=box,box", "parentNode and parentElement: " + log[0]);
    check(log[1] == "children=2", "children lists the element children: " + log[1]);
    check(log[2] == "removed=1,true", "removeChild through parentNode: " + log[2]);
    check(log[3] == "inserted=c,b", "insertBefore puts it before the reference: " + log[3]);
    check(log[4] == "self=1", "remove() takes an element out itself: " + log[4]);
}

// getElementsByClassName and getElementsByName, and the property that makes
// them hard: they are LIVE. A page takes the collection, mutates the document
// and reads the collection AGAIN, expecting the new answer - which a snapshot
// array cannot give, and which is what five of web-platform-tests' own
// getElementsByClassName tests do. Asserted here rather than only there because
// WPT is opt-in, needs a 40 MB corpus, and takes four minutes.
void test_element_collections_are_live() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html class="a"><body class="a">
      <p id=p1 class="x  y"></p><input id=i1 name=q><script>
        const all = document.getElementsByClassName('a');
        console.log('n=' + all.length + ',' + all[0].tagName + ',' + all[1].tagName);
        // THE ORDERED SET PARSER: split on all five ASCII whitespace
        // characters, a repeat asks for the class once, an all-whitespace
        // argument matches nothing, and the match is case-SENSITIVE.
        console.log('tokens=' + document.getElementsByClassName('\ty\n x\r').length +
                    ',' + document.getElementsByClassName('x x').length +
                    ',' + document.getElementsByClassName('   ').length +
                    ',' + document.getElementsByClassName('X').length);
        const made = document.createElement('span');
        made.className = 'a';
        document.body.appendChild(made);
        console.log('grew=' + all.length);
        document.body.removeAttribute('class');
        console.log('shrank=' + all.length);
        // What assert_array_equals checks before it compares one element.
        console.log('own=' + all.hasOwnProperty(0) + ',' + all.hasOwnProperty(9) +
                    ',' + ('length' in all) + ',' + (typeof all));
        // Scoped to a subtree, and the element is never one of its own results.
        console.log('scoped=' + document.body.getElementsByClassName('a').length);
        // getElementsByName is keyed on the name ATTRIBUTE, never on id.
        console.log('named=' + document.getElementsByName('q').length +
                    ',' + document.getElementsByName('i1').length +
                    ',' + document.getElementsByName('q')[0].id);
        console.log('nodes=' + made.nodeName + ',' + made.nodeType + ',' + made.localName);
        console.log('attr=' + document.getElementById('p1').hasAttribute('class') +
                    ',' + document.body.hasAttribute('class'));
      </script></body></html>)");
    check(page.script_error().empty(), "the collection script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log.size() == 9, "every line was logged");
    check(log[0] == "n=2,HTML,BODY", "document order, elements only: " + log[0]);
    check(log[1] == "tokens=1,1,0,0", "the ordered set parser: " + log[1]);
    check(log[2] == "grew=3", "an appended element joins the collection: " + log[2]);
    check(log[3] == "shrank=2", "a removed class leaves it: " + log[3]);
    check(log[4] == "own=true,false,true,object", "the collection is array-shaped: " + log[4]);
    check(log[5] == "scoped=1", "an element's search is its descendants: " + log[5]);
    check(log[6] == "named=1,0,i1", "getElementsByName reads `name`: " + log[6]);
    check(log[7] == "nodes=SPAN,1,span", "nodeName, nodeType and localName: " + log[7]);
    check(log[8] == "attr=true,false", "hasAttribute after removeAttribute: " + log[8]);
}

// The rest of the constructible DOM: a comment, a fragment, the five insertion
// methods that take any number of arguments, cloneNode and contains. A page
// could make an element and a text node and nothing else before this.
void test_the_constructible_dom() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><ul id=list><li id=b>B</li></ul><script>
        function ids(el) {
          var out = [];
          for (var i = 0; i < el.children.length; i++) { out.push(el.children[i].id); }
          return out.join('');
        }
        var list = document.getElementById('list');
        var b = document.getElementById('b');

        // A FRAGMENT IS FLATTENED BY INSERTION: its children move and it does
        // not, which is the whole reason to build one.
        var frag = document.createDocumentFragment();
        var one = document.createElement('li'); one.id = 'c';
        var two = document.createElement('li'); two.id = 'd';
        frag.append(one, two);
        console.log('frag=' + frag.nodeType + ',' + frag.nodeName + ',' +
                    frag.childNodes.length);
        list.append(frag);
        console.log('flat=' + ids(list) + ',' + frag.childNodes.length +
                    ',' + (list.contains(one)) + ',' + (list.contains(list)));

        // prepend keeps the ARGUMENT order, and a string becomes a Text node.
        var a = document.createElement('li'); a.id = 'a';
        list.prepend(a, 'loose');
        console.log('prepend=' + ids(list) + ',' + list.childNodes[1].nodeType +
                    ',' + list.childNodes[1].data);

        // before / after / replaceWith, relative to a child.
        var z = document.createElement('li'); z.id = 'z';
        b.before(z);
        var y = document.createElement('li'); y.id = 'y';
        b.after(y);
        console.log('around=' + ids(list));
        var w = document.createElement('li'); w.id = 'w';
        y.replaceWith(w);
        console.log('replaced=' + ids(list));

        // A comment is a node with data, and it is not an element.
        var note = document.createComment('hi');
        list.append(note);
        console.log('comment=' + note.nodeType + ',' + note.nodeName + ',' + note.data +
                    ',' + (b.data === null) + ',' + list.children.length +
                    ',' + list.childNodes.length);

        // cloneNode: shallow keeps the attributes only, deep keeps the subtree,
        // and neither is attached to anything.
        var shallow = list.cloneNode(false);
        var deep = list.cloneNode(true);
        console.log('clone=' + shallow.id + ',' + shallow.childNodes.length +
                    ',' + deep.childNodes.length + ',' + (deep.parentNode === null));
      </script></body></html>)");
    check(page.script_error().empty(), "the constructible-DOM script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log.size() == 7, "every line was logged");
    check(log[0] == "frag=11,#document-fragment,2", "a fragment holds nodes: " + log[0]);
    check(log[1] == "flat=bcd,0,true,true", "inserting a fragment moves its children: " + log[1]);
    check(log[2] == "prepend=abcd,3,loose", "prepend keeps argument order: " + log[2]);
    check(log[3] == "around=azbycd", "before and after place a sibling: " + log[3]);
    check(log[4] == "replaced=azbwcd", "replaceWith swaps one for another: " + log[4]);
    check(log[5] == "comment=8,#comment,hi,true,6,8",
          "a comment is a node and not an element: " + log[5]);
    check(log[6] == "clone=list,0,8,true", "cloneNode is shallow or deep, and detached: " + log[6]);
}

// `document.implementation`, whose absence cost 136 assertions in one WPT file
// and reported none of them by name.
void test_document_implementation() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><script>
        var impl = document.implementation;
        // TRUE FOR EVERYTHING, which is what the DOM standard defines rather
        // than what this engine could honestly claim.
        console.log('feature=' + impl.hasFeature() + ',' + impl.hasFeature('Core', '2.0') +
                    ',' + impl.hasFeature('nonsense') +
                    ',' + (typeof impl.hasFeature.apply));
        var dt = impl.createDocumentType('html', '', '');
        console.log('doctype=' + dt.name + ',' + (dt.publicId === '') + ',' + dt.nodeType);
        // BOTH OF THESE USED TO BE ABSENT, and this line asserted the absence
        // by name. A second Document exists now - see unit/second_document -
        // so the same line asserts that the two are functions and that the
        // document each returns is not this one.
        var made = impl.createHTMLDocument('m');
        console.log('second=' + (typeof impl.createHTMLDocument) +
                    ',' + (typeof impl.createDocument) +
                    ',' + (made !== document) + ',' + made.title);
      </script></body></html>)");
    check(page.script_error().empty(), "the implementation script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log.size() == 3, "every line was logged");
    check(log[0] == "feature=true,true,true,function", "hasFeature is always true: " + log[0]);
    check(log[1] == "doctype=html,true,10",
          "createDocumentType carries its three strings: " + log[1]);
    check(log[2] == "second=function,function,true,m",
          "createHTMLDocument makes a SECOND document: " + log[2]);
}

// `createElementNS`, and the round trip that has to survive it: the exact
// namespace, the qualified name, the prefix and the local part, none of them
// case-folded. A createElementNS that lost the namespace would be worse than
// none - a page would build an SVG element that styled and painted as HTML.
void test_create_element_ns() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><svg id=s><rect/></svg><script>
        var HTML = 'http://www.w3.org/1999/xhtml';
        var SVG  = 'http://www.w3.org/2000/svg';
        var MINE = 'http://example.com/ns';

        var h = document.createElementNS(HTML, 'div');
        var v = document.createElementNS(SVG, 'linearGradient');
        var m = document.createElementNS(MINE, 'pre:fix');
        console.log('html=' + h.namespaceURI + ',' + h.tagName + ',' + h.localName +
                    ',' + (h.prefix === null));
        // NOT case-folded, and an HTML-namespace tagName still uppercases.
        console.log('svg=' + v.namespaceURI + ',' + v.tagName + ',' + v.localName);
        console.log('mine=' + m.namespaceURI + ',' + m.tagName + ',' + m.prefix +
                    ',' + m.localName);
        // The parser's own elements answer the same three questions.
        var s = document.getElementById('s');
        console.log('parsed=' + s.namespaceURI + ',' + document.body.namespaceURI);
        // A clone keeps the namespace: it is not on the node, so this is the
        // one that would silently regress.
        console.log('clone=' + m.cloneNode(false).namespaceURI);
        // The null namespace is null, not the empty string.
        console.log('nullns=' + (document.createElementNS(null, 'x').namespaceURI === null));
        // The prefix rules, which are what make a prefix mean anything.
        function threw(fn) { try { fn(); return 'no'; } catch (e) { return e.name; } }
        console.log('errors=' + threw(function () { document.createElementNS(null, 'p:q'); }) +
                    ',' + threw(function () { document.createElementNS(MINE, 'xml:q'); }) +
                    ',' + threw(function () { document.createElementNS(MINE, 'xmlns'); }) +
                    ',' + threw(function () { document.createElementNS(MINE, ':q'); }) +
                    ',' + threw(function () { document.createElementNS(MINE, 'q:'); }));
        // getElementsByTagNameNS matches on the namespace AND the local part.
        document.body.appendChild(v);
        // Three SVG elements by then: <svg>, its <rect>, and the appended one.
        console.log('bytag=' + document.getElementsByTagNameNS(SVG, '*').length +
                    ',' + document.getElementsByTagNameNS(HTML, 'div').length +
                    ',' + document.getElementsByTagNameNS('*', 'linearGradient').length);
      </script></body></html>)");
    check(page.script_error().empty(), "the createElementNS script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log.size() == 8, "every line was logged");
    check(log[0] == "html=http://www.w3.org/1999/xhtml,DIV,div,true",
          "an HTML-namespace element uppercases tagName only: " + log[0]);
    check(log[1] == "svg=http://www.w3.org/2000/svg,linearGradient,linearGradient",
          "createElementNS does not case-fold: " + log[1]);
    check(log[2] == "mine=http://example.com/ns,pre:fix,pre,fix",
          "the qualified name splits at the first colon: " + log[2]);
    check(log[3] == "parsed=http://www.w3.org/2000/svg,http://www.w3.org/1999/xhtml",
          "the parser's elements know their namespace too: " + log[3]);
    check(log[4] == "clone=http://example.com/ns", "a clone keeps the namespace: " + log[4]);
    check(log[5] == "nullns=true", "the null namespace reports null: " + log[5]);
    check(log[6] == "errors=NamespaceError,NamespaceError,NamespaceError,"
                    "InvalidCharacterError,InvalidCharacterError",
          "the prefix rules are enforced: " + log[6]);
    check(log[7] == "bytag=3,0,1", "getElementsByTagNameNS matches both halves: " + log[7]);
}

// `querySelector` ON AN ELEMENT, searching its own subtree rather than the
// document. The document had both and an element had neither, so "find
// something inside this" - what a library does with a container it owns -
// threw. p5.js's describe() builds an offscreen tree and queries it.
void test_element_query_selector() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body>
        <div id=box><span class=hit>a</span><span class=hit>b</span></div>
        <span class=hit>outside</span>
        <script>
          const box = document.getElementById('box');
          console.log('one=' + box.querySelector('.hit').getText());
          console.log('all=' + box.querySelectorAll('.hit').length);
          // The document's own search still sees everything, including the one
          // outside the box - that is what makes the scoping meaningful.
          console.log('doc=' + document.querySelectorAll('.hit').length);
          console.log('miss=' + (box.querySelector('.nothing') === null));
        </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "one=a", "an element finds the first match inside itself: " + log[0]);
    check(log[1] == "all=2", "...and only the ones inside it: " + log[1]);
    check(log[2] == "doc=3", "while the document still sees them all: " + log[2]);
    check(log[3] == "miss=true", "no match is null: " + log[3]);
}

// `innerHTML` PARSES, and `textContent` does not.
//
// innerHTML was a plain property on the wrapper: assigning markup stored a
// string, built no nodes, rendered nothing and reported nothing - and reading
// it back gave whatever the page last wrote rather than what the DOM holds. It
// goes through the same WHATWG tokenizer and tree builder the page did, because
// the alternative is a second and worse parser for the commonest way a page
// builds content.
void test_inner_html() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><head><style>.k { background-color: #008000 }</style></head>
        <body><div id=d></div><script>
        const d = document.getElementById('d');
        d.innerHTML = '<b id="ib" class="k">hi</b><span>there</span>';
        console.log('nodes=' + d.children.length + ',' + (document.getElementById('ib') !== null));
        console.log('read=' + d.innerHTML);
        console.log('text=' + d.textContent);
        // Replacing wipes what was there rather than appending.
        d.innerHTML = '<i>only</i>';
        console.log('replaced=' + d.children.length + ',' + d.textContent);
        // textContent is TEXT, never markup - that is why a page reaches for it.
        d.textContent = '<not markup>';
        console.log('asText=' + d.children.length + ',' + d.textContent);
        </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "nodes=2,true", "the markup became real nodes, findable by id: " + log[0]);
    // Serialised from the tree rather than echoed back, so a node appended
    // afterwards would show up too.
    check(log[1] == R"(read=<b id="ib" class="k">hi</b><span>there</span>)",
          "reading serialises the children: " + log[1]);
    check(log[2] == "text=hithere", "textContent is every text node under it: " + log[2]);
    check(log[3] == "replaced=1,only", "assigning again replaces: " + log[3]);
    check(log[4] == "asText=0,<not markup>", "textContent stores text, not markup: " + log[4]);
    // And the parsed nodes are in the CASCADE, which is what says they are
    // really in the document rather than in a side table.
    check(page.frame().has_value(), "the page renders");

    // A <script>'s textContent is its SOURCE, unmangled. p5's error system
    // reads it back and parses it, so anything lost here becomes a syntax
    // error in a file the page never wrote.
    browser scripts{browser_options{200, 200}};
    scripts.load_html(R"(<html><body>
<script id=t>
var a = 1;
if (a < 2 && a > 0) { a++; }
</script>
<script>
console.log('src=' + document.getElementById('t').textContent.split('\n').join('|'));
</script></body></html>)");
    check(scripts.script_error().empty(), "the script ran: " + scripts.script_error());
    check(!log_of(scripts).empty(), "the script reported");
    if (!log_of(scripts).empty()) {
        check(log_of(scripts).back() == "src=|var a = 1;|if (a < 2 && a > 0) { a++; }|",
              "a script's text survives the round trip: " + log_of(scripts).back());
    }
}

// A CONTROL'S `value` IS LIVE, and a canvas is an image source.
//
// Both were the same shape of bug: a property written on whatever tick a sync
// next ran, read by a page in the statement that created it.
void test_control_value_is_live() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body><input id=static value="s"><script>
        console.log('static=' + document.getElementById('static').value);
        // The order p5's createInput uses: create, set the ATTRIBUTE, append.
        // The value has to be visible immediately, not after the next refresh.
        const made = document.createElement('input');
        made.setAttribute('type', 'text');
        made.setAttribute('value', 'from-attribute');
        document.body.appendChild(made);
        console.log('created=' + made.value);
        // An assignment wins over the attribute from then on - including an
        // assignment of the empty string, which is how a page clears a field.
        made.value = 'assigned';
        console.log('assigned=' + made.value);
        made.value = '';
        console.log('cleared=[' + made.value + ']');
        const box = document.createElement('input');
        box.setAttribute('type', 'checkbox');
        document.body.appendChild(box);
        box.checked = true;
        console.log('checked=' + box.checked);
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "static=s", "a control in the markup reads its attribute: " + log[0]);
    check(log[1] == "created=from-attribute",
          "a control created by script reads it immediately: " + log[1]);
    check(log[2] == "assigned=assigned", "an assignment is read back: " + log[2]);
    // The attribute must NOT come back and undo it.
    check(log[3] == "cleared=[]", "assigning the empty string clears the field: " + log[3]);
    check(log[4] == "checked=true", "checked round-trips: " + log[4]);
}

// `innerText` AND `outerText` ASSIGN TEXT AND LINE BREAKS, which is the half of
// those two properties that does not need layout.
//
// The assigned string is cut at every CR, LF or CRLF pair: each run of other
// characters becomes a Text node and each break becomes a <br> ELEMENT. That is
// what separates it from `textContent`, which would have stored the newline as
// a character and rendered nothing, and from `innerHTML`, which would have read
// `<` as a tag and turned a U+0000 into U+FFFD.
//
// The GETTERS are deliberately absent - reading either is `undefined` - because
// innerText reads the rendered box tree and this engine lays out on a frame
// rather than on demand. See the note in lib/Shell/bindings/element/interfaces.cpp.
void test_inner_text_and_outer_text_assign() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body><div id=d>old</div>
        <ul><li id=host>A <span id=target>B</span> C</li></ul>
        <script>
        const d = document.getElementById('d');
        d.innerText = 'abc';
        console.log('plain=' + d.childNodes.length + ',' + d.firstChild.nodeType + ',' +
                    d.firstChild.data);
        d.innerText = 'abc\ndef';
        console.log('newline=' + d.innerHTML);
        d.innerText = 'a\r\nb\r\rc';
        console.log('crlf=' + d.innerHTML);
        // Text, and only text: no escape, no parse, no character lost.
        d.innerText = 'abc<def&';
        console.log('literal=' + d.childNodes.length + ',' + d.firstChild.data);
        // [LegacyNullToEmptyString] on one side and ToString on the other.
        d.innerText = null;
        const cleared = d.childNodes.length;
        d.innerText = undefined;
        console.log('nullish=' + cleared + ',' + d.firstChild.data);
        // outerText replaces the ELEMENT, and merges the text it lands between.
        const target = document.getElementById('target');
        const host = document.getElementById('host');
        target.outerText = 'Replaced';
        console.log('outer=' + host.innerHTML + ',' + host.childNodes.length);
        // A detached element has nothing to replace it in, and says so.
        try {
            document.createElement('span').outerText = 'x';
            console.log('detached=no throw');
        } catch (e) { console.log('detached=' + e.name); }
        // On HTMLElement, so an <svg> has neither - assigning one there stores a
        // property and leaves the element empty, which is what a browser does.
        const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        svg.innerText = 'abc';
        console.log('svg=' + svg.childNodes.length);
        </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "plain=1,3,abc", "one new text node replaces the children: " + log[0]);
    check(log[1] == "newline=abc<br>def", "a newline is a <br> element: " + log[1]);
    check(log[2] == "crlf=a<br>b<br><br>c", "CRLF is one break and CR CR is two: " + log[2]);
    check(log[3] == "literal=1,abc<def&", "the text is stored, not parsed: " + log[3]);
    check(log[4] == "nullish=0,undefined", "null empties and undefined writes: " + log[4]);
    check(log[5] == "outer=A Replaced C,1",
          "outerText replaces the element and merges the text around it: " + log[5]);
    check(log[6] == "detached=NoModificationAllowedError",
          "outerText on a parentless element throws: " + log[6]);
    check(log[7] == "svg=0", "innerText is HTMLElement's, not Element's: " + log[7]);
}

// `insertAdjacentHTML` - a fragment parse at one of four places relative to the
// element. Same parser and same copy as innerHTML; only where the nodes land
// differs.
void test_insert_adjacent_html() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body><div id=box><span id=mid>mid</span></div><script>
        const mid = document.getElementById('mid');
        mid.insertAdjacentHTML('beforebegin', '<i id=before>b</i>');
        mid.insertAdjacentHTML('afterend', '<i id=after>a</i>');
        mid.insertAdjacentHTML('afterbegin', '<b id=in-first>1</b>');
        mid.insertAdjacentHTML('beforeend', '<b id=in-last>2</b>');
        const box = document.getElementById('box');
        let order = '';
        for (const kid of box.children) { order += kid.id + ' '; }
        let inner = '';
        for (const kid of mid.children) { inner += kid.id + ' '; }
        console.log('outer=' + order.trim());
        console.log('inner=' + inner.trim());
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "outer=before mid after", "beforebegin and afterend place siblings: " + log[0]);
    check(log[1] == "inner=in-first in-last", "afterbegin and beforeend place children: " + log[1]);
}

// THE OTHER TWO SPELLINGS OF THE SAME ALGORITHM. `insertAdjacentElement` and
// `insertAdjacentText` take a node rather than markup and are otherwise
// `insertAdjacentHTML` exactly, which is why all three go through one
// "insert adjacent" now - and why `afterend` is the case worth a test: it used
// to APPEND to the parent, so a node placed after an element with a later
// sibling landed at the end of the list instead of beside it.
void test_insert_adjacent_element_and_text() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body><div id=box><span id=a>a</span><span id=z>z</span></div>
        <i id=moved>m</i><script>
        const a = document.getElementById('a');
        a.insertAdjacentElement('afterend', document.getElementById('moved'));
        const box = document.getElementById('box');
        let order = '';
        for (const kid of box.children) { order += kid.id + ' '; }
        console.log('order=' + order.trim());
        const back = a.insertAdjacentElement('beforebegin', document.getElementById('moved'));
        console.log('returned=' + back.id);
        a.insertAdjacentText('afterbegin', 'T');
        console.log('text=' + a.firstChild.nodeValue + ',' + a.textContent);
        let threw = '';
        try { a.insertAdjacentText('nowhere', 'x'); } catch (e) { threw = e.name; }
        let rooted = '';
        try {
            document.documentElement.insertAdjacentText('beforebegin', 'x');
        } catch (e) { rooted = e.name; }
        console.log('threw=' + threw + ',' + rooted);
        // An element with no parent has nowhere to put a sibling, and that is
        // a null rather than a throw.
        console.log('detached=' +
                    (document.createElement('div')
                         .insertAdjacentElement('afterend', document.createElement('b')) === null));
    </script></body></html>)");
    check(page.script_error().empty(), "the insert-adjacent script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log.size() > 4, "every insert-adjacent case logged");
    if (log.size() <= 4) { return; }
    check(log[0] == "order=a moved z",
          "afterend places the node BESIDE, not at the end: " + log[0]);
    check(log[1] == "returned=moved", "insertAdjacentElement answers with the node: " + log[1]);
    check(log[2] == "text=T,Ta", "insertAdjacentText makes a Text node: " + log[2]);
    check(log[3] == "threw=SyntaxError,HierarchyRequestError",
          "an unknown position and the document element's sibling both throw: " + log[3]);
    check(log[4] == "detached=true", "an element with no parent answers null: " + log[4]);
}

} // namespace

int main() {
    test_control_value_is_live();
    test_inner_text_and_outer_text_assign();
    test_insert_adjacent_html();
    test_insert_adjacent_element_and_text();
    test_inner_html();
    test_element_query_selector();
    test_tree_navigation();
    test_element_collections_are_live();
    test_the_constructible_dom();
    test_document_implementation();
    test_create_element_ns();
    REPORT("node_methods");
}
