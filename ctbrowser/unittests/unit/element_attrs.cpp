// THE ATTRIBUTE API ON Element, WHICH IS THREE QUESTIONS AND NOT ONE.
//
// `getAttribute(qualifiedName)` matches the QUALIFIED name irrespective of
// namespace and answers the FIRST such attribute; `getAttributeNS(ns, local)`
// matches the PAIR, in which the prefix takes no part; and `element.attributes`
// hands back the whole list as Attr nodes, each of which knows which of the
// three it is. An element may hold `x` in no namespace and `x` in two others at
// once, and every one of those lookups has a different right answer.
//
// WHAT MAKES THIS WORTH A FILE OF ITS OWN rather than more of bindings_basics:
// every case below turns on a DISTINCTION - qualified against namespaced, HTML
// against foreign, a name that serialises against a name that parses - and each
// of them used to have one answer where two belong. `bindings_basics` asks
// whether a binding works; these ask whether two bindings that look alike are
// being kept apart.
//
// The DOM layer under them is `struct attribute`'s interned namespace and the
// six lookups in dom/document.hpp; the binding layer is the block headed
// "ATTRIBUTES AS NODES" in lib/Shell/bindings/element.cpp.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

// `xml:lang` ON AN HTML ELEMENT AND `xlink:href` INSIDE <svg> are the pair
// `Attr-prefix.html` draws, and they are here rather than built from script
// because what they pin down is what the PARSER stored: a colon in an HTML
// start tag is part of the name and nothing else, and the same colon in
// foreign content binds a real namespace. Only a parsed document can tell the
// two apart.
constexpr const char * page_html = R"(<!DOCTYPE html>
<html><body>
<div id=test xml:lang="with prefix" class="without prefix"></div>
<svg id=s><g xlink:href="with prefix" class="without prefix"></g></svg>
</body></html>)";

// One expression against that page, and whatever it logged. A fresh page per
// case, exactly as unit/document_node.cpp does it: most of these MUTATE the
// document, and a case that changed it for the next one would report a failure
// in the wrong place.
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

// --- the qualified lookup and the namespaced one are different questions ----

void test_the_first_attribute_wins_the_qualified_lookup() {
    // Element-setAttribute.html, first subtest: a `setAttribute` CHANGES the
    // first attribute with that qualified name rather than adding a second one
    // in the null namespace, and which one it found is observable through the
    // namespaced read.
    is(R"JS((function () {
        var e = document.createElement('p');
        e.setAttributeNS('foo', 'x', 'first');
        e.setAttributeNS('foo2', 'x', 'second');
        e.setAttribute('x', 'changed');
        return e.attributes.length + ',' + e.getAttribute('x') + ',' +
               e.getAttributeNS('foo', 'x') + ',' + e.getAttributeNS('foo2', 'x');
    })())JS",
       "2,changed,changed,second");
    // Element-removeAttribute.html: removing by qualified name takes the FIRST
    // one away and the OTHER becomes the answer to the same question.
    is(R"JS((function () {
        var e = document.createElement('p');
        e.setAttribute('x', 'first');
        e.setAttributeNS('foo', 'x', 'second');
        e.removeAttribute('x');
        return e.getAttribute('x') + ',' + e.getAttributeNS(null, 'x') + ',' +
               e.getAttributeNS('foo', 'x') + ',' + e.attributes.length;
    })())JS",
       "second,null,second,1");
    // ...and `hasAttribute` asks about presence irrespective of namespace,
    // where `getAttributeNS(null, ...)` is asking about the null one.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttributeNS('ab', 'attr', 't1');
        e.setAttributeNS('kl', 'attr', 't2');
        return e.hasAttribute('attr') + ',' + e.hasAttributeNS('ab', 'attr') + ',' +
               e.hasAttributeNS('kl', 'attr') + ',' + e.hasAttributeNS(null, 'attr') + ',' +
               e.getAttribute('attr');
    })())JS",
       "true,true,true,false,t1");
    // `removeAttributeNS` takes a LOCAL name. Handing it a qualified one
    // removes nothing at all, which is the whole of Element-removeAttributeNS.
    is(R"JS((function () {
        var XML = 'http://www.w3.org/XML/1998/namespace';
        var e = document.createElement('foo');
        e.setAttributeNS(XML, 'a:bb', 'pass');
        e.removeAttributeNS(XML, 'a:bb');
        var still = e.attributes.length;
        e.removeAttributeNS(XML, 'bb');
        return still + ',' + e.attributes.length;
    })())JS",
       "1,0");
}

// --- and the fold is the ELEMENT's, not the API's --------------------------

void test_only_an_html_element_folds_its_attribute_names() {
    // attributes.html, "setAttribute should lowercase its name argument": the
    // fold happens on the way IN, so the namespaced read - which does not fold -
    // finds the lowercase spelling and not the one that was written.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttribute('CHEEseCaKe', 'tasty');
        return e.getAttributeNS('', 'CHEEseCaKe') + ',' + e.getAttributeNS('', 'cheesecake') +
               ',' + e.getAttribute('CHEEseCaKe');
    })())JS",
       "null,tasty,tasty");
    // ...and setAttributeNS does NOT fold, so the same element can hold a name
    // no `getAttribute` on an HTML element can ever ask for.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttributeNS('', 'ALIGN', 'left');
        return e.hasAttribute('ALIGN') + ',' + e.hasAttribute('align') + ',' +
               e.hasAttributeNS(null, 'ALIGN') + ',' + e.getAttributeNS('', 'ALIGN');
    })())JS",
       "false,false,true,left");
    // THE FOLD IS THE HTML NAMESPACE'S. An SVG element keeps its capitals, and
    // this is the case that used to be silently unreachable: `viewBox` interned
    // as `viewbox`, which is a name nothing in the rasteriser or the style
    // engine ever looks for.
    is(R"JS((function () {
        var s = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        s.setAttribute('viewBox', '0 0 4 3');
        return s.getAttribute('viewBox') + ',' + s.getAttribute('viewbox');
    })())JS",
       "0 0 4 3,null");
}

// --- an attribute name is a serialisation rule, not the Name production -----

void test_what_an_attribute_may_be_called() {
    // productions.js's whole list. Twelve of the thirteen are refused by the
    // XML `Name` production and every one of them must SUCCEED: the rule is
    // that a name has to survive being written into a start tag and read back.
    is(R"JS((function () {
        var e = document.createElement('foo');
        var names = ['x', 'X', ':', 'a:0', 'invalid^Name', '\\', "'", '"', '0', '0:a', ':a',
                     'x:y:x', '~'];
        var ok = 0;
        for (var i = 0; i < names.length; i++) {
            e.setAttribute(names[i], 'test');
            if (e.getAttribute(names[i]) === 'test') { ok++; }
        }
        return ok + ',' + names.length;
    })())JS",
       "13,13");
    // The empty string is the ONLY one productions.js calls invalid...
    is("document.createElement('foo').setAttribute('', 'x')", "threw:InvalidCharacterError");
    // ...and the break set is the other half of the rule: these four are the
    // characters the tokenizer's attribute name state stops on.
    is("document.createElement('foo').setAttribute('a b', 'x')", "threw:InvalidCharacterError");
    is("document.createElement('foo').setAttribute('a/b', 'x')", "threw:InvalidCharacterError");
    is("document.createElement('foo').setAttribute('a=b', 'x')", "threw:InvalidCharacterError");
    is("document.createElement('foo').setAttribute('a>b', 'x')", "threw:InvalidCharacterError");
    // `xmlns` is an ordinary attribute name to `setAttribute` - the namespace
    // rules below belong to `setAttributeNS` and to nothing else.
    is(R"JS((function () {
        var e = document.createElement('foo');
        e.setAttribute('xmlns', 'a');
        e.setAttribute('xmlns:a', 'b');
        return e.getAttribute('xmlns') + ',' + e.getAttribute('xmlns:a');
    })())JS",
       "a,b");
}

// --- "validate and extract", and its two different throws -------------------

void test_setattributens_validates_then_extracts() {
    // THE ORDER IS PART OF THE ANSWER: the shape of the name is decided before
    // the namespace is looked at, so a bad name in the XMLNS namespace is an
    // InvalidCharacterError and not the NamespaceError it would otherwise earn.
    is("document.createElement('foo')"
       ".setAttributeNS('http://www.w3.org/2000/xmlns/', '', 'fail')",
       "threw:InvalidCharacterError");
    // A QName with an empty half either side of the colon.
    is("document.createElement('foo').setAttributeNS('a', 'b:', 'fail')",
       "threw:InvalidCharacterError");
    // A prefix needs a namespace...
    is("document.createElement('foo').setAttributeNS('', 'aa:bb', 'fail')", "threw:NamespaceError");
    is("document.createElement('foo').setAttributeNS(null, 'aa:bb', 'fail')",
       "threw:NamespaceError");
    // ...and three prefixes are bound to a namespace each, in both directions.
    is("document.createElement('foo').setAttributeNS('a', 'xml:bb', 'fail')",
       "threw:NamespaceError");
    is("document.createElement('foo').setAttributeNS('a', 'xmlns:bb', 'fail')",
       "threw:NamespaceError");
    is("document.createElement('foo').setAttributeNS('a', 'xmlns', 'fail')",
       "threw:NamespaceError");
    is("document.createElement('foo')"
       ".setAttributeNS('http://www.w3.org/2000/xmlns/', 'b:foo', 'fail')",
       "threw:NamespaceError");
    // And the four spellings that are LEGAL, which are what stops the rules
    // above from being written as "refuse anything with a colon".
    is(R"JS((function () {
        var XML = 'http://www.w3.org/XML/1998/namespace';
        var XMLNS = 'http://www.w3.org/2000/xmlns/';
        var e = document.createElement('foo');
        e.setAttributeNS(XML, 'a:bb', 'one');
        e.setAttributeNS(XMLNS, 'xmlns:a', 'two');
        e.setAttributeNS(XMLNS, 'xmlns', 'three');
        e.setAttributeNS('ns', 'a:xmlns', 'four');
        return e.attributes.length + ',' + e.getAttributeNS(XML, 'bb') + ',' +
               e.getAttributeNS(XMLNS, 'a') + ',' + e.getAttributeNS(XMLNS, 'xmlns') + ',' +
               e.getAttributeNS('ns', 'xmlns');
    })())JS",
       "4,one,two,three,four");
    // "Setting the same attribute with another prefix should not change the
    // prefix" - the write finds the attribute by (namespace, local name) and
    // changes its VALUE, never its name.
    is(R"JS((function () {
        var e = document.createElement('foo');
        e.setAttributeNS('a', 'foo:bar', 'X');
        e.setAttributeNS('a', 'quux:bar', 'Y');
        return e.attributes.length + ',' + e.attributes[0].name + ',' + e.attributes[0].value;
    })())JS",
       "1,foo:bar,Y");
}

// --- element.attributes is a NamedNodeMap of Attr nodes ---------------------

void test_the_map_holds_attr_nodes() {
    // `attr_is` in dom/nodes/attributes.js reads nine properties off every
    // attribute it checks, and this is all nine of them at once.
    is(R"JS((function () {
        var e = document.createElement('baz');
        e.setAttributeNS('foo', 'foo:bar', '1');
        var a = e.attributes[0];
        return [a.value, a.nodeValue, a.textContent, a.localName, a.namespaceURI, a.prefix,
                a.name, a.nodeName, a.nodeType, a.specified, a.ownerElement === e].join(',');
    })())JS",
       "1,1,1,bar,foo,foo,foo:bar,foo:bar,2,true,true");
    // An attribute with NO namespace has no prefix and no local name to split
    // off: the whole qualified name is the local name, colons and all.
    is(R"JS((function () {
        var e = document.createElement('foo');
        e.setAttribute('x:y', 'v');
        var a = e.attributes[0];
        return a.localName + ',' + (a.prefix === null) + ',' + (a.namespaceURI === null);
    })())JS",
       "x:y,true,true");
    // WRITING THROUGH ONE reaches the element, which is what makes an Attr a
    // view rather than a copy. attributes.html's "Attribute values should not
    // be parsed" is exactly this, entity and all.
    is(R"JS((function () {
        var e = document.createElement('foo');
        e.setAttribute('x', 'y');
        var a = e.attributes[0];
        a.value = 'Y&lt;';
        return a.value + ',' + a.nodeValue + ',' + a.textContent + ',' + e.getAttribute('x');
    })())JS",
       "Y&lt;,Y&lt;,Y&lt;,Y&lt;");
    // ...and it is LIVE on the way out too: the map is read out of the document
    // when it is asked for, not when the wrapper was made.
    is(R"JS((function () {
        var e = document.createElement('div');
        var before = e.attributes.length;
        e.setAttribute('a', '1');
        return before + ',' + e.attributes.length + ',' + e.attributes[0].value;
    })())JS",
       "0,1,1");
}

void test_the_map_is_iterable_and_named() {
    // FOR-OF AND SPREAD, which is what p5's XML module and Bootstrap
    // respectively walk one with. They go through `iterable_values`, which
    // yields nothing at all for a proxy - hence an array-like object.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttribute('a', '1');
        e.setAttribute('b', '2');
        var out = '';
        for (const one of e.attributes) { out += one.name + '=' + one.value + ';'; }
        return out + [...e.attributes].length;
    })())JS",
       "a=1;b=2;2");
    // `item`, and the named lookups in both spellings.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttributeNS('urn:x', 'p:a', '1');
        var m = e.attributes;
        return m.item(0).name + ',' + (m.item(9) === null) + ',' + m.getNamedItem('p:a').value +
               ',' + m.getNamedItemNS('urn:x', 'a').value + ',' + (m.getNamedItem('no') === null);
    })())JS",
       "p:a,true,1,1,true");
    // Removing by name ANSWERS with the attribute it removed, and THROWS when
    // there is nothing to remove - the half that is easy to leave out.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttribute('a', '1');
        var gone = e.attributes.removeNamedItem('a');
        return gone.name + ',' + gone.value + ',' + e.hasAttribute('a');
    })())JS",
       "a,1,false");
    is("document.createElement('div').attributes.removeNamedItem('nope')", "threw:NotFoundError");
    // `hasAttributes` is the question the map's length answers, asked without
    // building the map.
    is(R"JS((function () {
        var e = document.createElement('div');
        var before = e.hasAttributes();
        e.setAttribute('a', '');
        return before + ',' + e.hasAttributes() + ',' +
               document.getElementById('test').hasAttributes();
    })())JS",
       "false,true,true");
}

// --- what the PARSER stored, which is the other half of Attr-prefix ---------

void test_a_parsed_attribute_keeps_the_namespace_it_was_given() {
    // A colon in an HTML start tag is part of the NAME and binds nothing:
    // `xml:lang` on a <div> is one unprefixed attribute called "xml:lang".
    is(R"JS((function () {
        var a = document.getElementById('test').getAttributeNodeNS(null, 'xml:lang');
        return a.value + ',' + a.localName + ',' + (a.namespaceURI === null) + ',' +
               (a.prefix === null) + ',' + a.name;
    })())JS",
       "with prefix,xml:lang,true,true,xml:lang");
    // The same colon in FOREIGN content binds a real namespace, and the local
    // name and the prefix are then derived from the pair.
    is(R"JS((function () {
        var g = document.getElementsByTagName('g')[0];
        var a = g.getAttributeNodeNS('http://www.w3.org/1999/xlink', 'href');
        return a.value + ',' + a.localName + ',' + a.namespaceURI + ',' + a.prefix + ',' + a.name;
    })())JS",
       "with prefix,href,http://www.w3.org/1999/xlink,xlink,xlink:href");
    // ...while an ordinary attribute on the same element has no namespace at
    // all, which is what keeps the case above from being "everything in <svg>".
    is(R"JS((function () {
        var a = document.getElementsByTagName('g')[0].getAttributeNodeNS(null, 'class');
        return a.value + ',' + a.localName + ',' + (a.namespaceURI === null) + ',' +
               (a.prefix === null);
    })())JS",
       "without prefix,class,true,true");
    // `getAttributeNames` is the qualified names, in order.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttributeNS('urn:x', 'p:a', '1');
        e.setAttribute('b', '2');
        return e.getAttributeNames().join(',');
    })())JS",
       "p:a,b");
}

void test_an_element_searches_its_own_subtree_by_namespace() {
    // `getElementsByTagNameNS` on an ELEMENT, which only the document had.
    // "*" is any on either half, and the element is not one of its own results.
    is(R"JS((function () {
        var svg = document.getElementById('s');
        return svg.getElementsByTagNameNS('http://www.w3.org/2000/svg', 'g').length + ',' +
               svg.getElementsByTagNameNS('*', 'g').length + ',' +
               svg.getElementsByTagNameNS('http://www.w3.org/1999/xhtml', 'g').length + ',' +
               svg.getElementsByTagNameNS('*', 'svg').length;
    })())JS",
       "1,1,0,0");
}

// --- ARIA, which is the reflection table's one nullable type ----------------

void test_aria_reflects_as_a_nullable_string() {
    // `role` and the forty-odd `aria-*` names reflect on Element, so a <div>
    // and an <svg> both have them. The content attribute is spelled out in the
    // table rather than derived: no rule turns `ariaBrailleRoleDescription`
    // into `aria-brailleroledescription`.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttribute('role', 'button');
        e.setAttribute('aria-brailleroledescription', 'x');
        e.setAttribute('aria-multiselectable', 'true');
        return e.role + ',' + e.ariaBrailleRoleDescription + ',' + e.ariaMultiSelectable;
    })())JS",
       "button,x,true");
    is(R"JS((function () {
        var e = document.createElement('div');
        e.ariaLabel = 'y';
        e.ariaValueNow = '51';
        return e.getAttribute('aria-label') + ',' + e.getAttribute('aria-valuenow');
    })())JS",
       "y,51");
    // NULLABLE, which is the whole reason this is a type of its own: an absent
    // one is `null` and not "", and writing null or undefined REMOVES it rather
    // than writing those four or nine characters. `testNullable` in
    // aria-attribute-reflection.html runs exactly this on every row.
    is(R"JS((function () {
        var e = document.createElement('div');
        var absent = e.ariaChecked;
        e.setAttribute('aria-checked', 'mixed');
        var set = e.ariaChecked;
        e.ariaChecked = null;
        var cleared = e.ariaChecked + ',' + e.hasAttribute('aria-checked');
        e.ariaChecked = 'true';
        e.ariaChecked = undefined;
        return (absent === null) + ',' + set + ',' + cleared + ',' + e.ariaChecked + ',' +
               e.hasAttribute('aria-checked');
    })())JS",
       "true,mixed,null,false,null,false");
    // A plain DOMString row is NOT nullable, and the two must not converge:
    // `el.id` is "" when absent and assigning null writes "null".
    is(R"JS((function () {
        var e = document.createElement('div');
        e.id = null;
        return '[' + e.id + '],[' + document.createElement('div').id + ']';
    })())JS",
       "[null],[]");
}

} // namespace

int main() {
    test_the_first_attribute_wins_the_qualified_lookup();
    test_only_an_html_element_folds_its_attribute_names();
    test_what_an_attribute_may_be_called();
    test_setattributens_validates_then_extracts();
    test_the_map_holds_attr_nodes();
    test_the_map_is_iterable_and_named();
    test_a_parsed_attribute_keeps_the_namespace_it_was_given();
    test_an_element_searches_its_own_subtree_by_namespace();
    test_aria_reflects_as_a_nullable_string();
    REPORT("element_attrs");
}
