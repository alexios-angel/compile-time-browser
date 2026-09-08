// CharacterData, WHICH IS FOUR NODE TYPES SHARING ONE STRING - and the string
// is measured in UTF-16 CODE UNITS while this engine stores UTF-8.
//
// That is the whole reason this file exists rather than being more of
// element_attrs or node_methods. Every offset in `CharacterData` and in
// `Text.splitText` is a `unsigned long` counting UTF-16 code units:
// `substringData(39, 2)` on a string with CJK in it and `replaceData(5, 8, ..)`
// on one with an astral character in it are the two shapes of the same
// question, and a byte offset answers both of them wrongly in a way that only
// shows up on non-ASCII text. `dom/nodes/CharacterData-*.html` runs the whole
// battery against a Text AND a Comment, which is why the cases below do the
// same rather than trusting one to stand for the other.
//
// The other half is the ARITHMETIC AROUND THE EDGE. An offset is ToUint32'd
// before it is compared, so `-1` is 4294967295 and throws IndexSizeError while
// `-0x100000000 + 2` is 2 and does not; a count is clamped to the end rather
// than throwing. Those are five separate conventions in one signature and the
// corpus tests each of them by name.
//
// The bindings are the block headed "CharacterData" at the bottom of
// lib/Shell/bindings/element.cpp.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><body><div id=host>text</div></body></html>)";

// One expression against a FRESH page, exactly as unit/element_attrs.cpp does
// it: most of these mutate the document, and a case that changed it for the
// next one would report a failure in the wrong place.
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

// The two node types the corpus runs its whole CharacterData battery against.
// Both, every time, because a method installed on `Text.prototype` by accident
// rather than on `CharacterData.prototype` passes half of it.
void both(const std::string & body, const std::string & expected) {
    for (const char * make :
         {"document.createTextNode('test')", "document.createComment('test')"}) {
        is("(function () { var node = " + std::string{make} + "; " + body + " })()", expected);
    }
}

// --- a removed Attr keeps what it had ---------------------------------------

void test_a_removed_attribute_keeps_its_value() {
    // AN Attr IS LIVE ONLY WHILE IT HAS AN ELEMENT. Every Attr this engine
    // hands out reads its value through to the element that names it, which is
    // right until the attribute is REMOVED - and `removeNamedItem` hands the
    // Attr back precisely so a page can put it somewhere else. Reading through
    // then answers "", because the element genuinely no longer has it.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttribute('a', '1');
        var gone = e.attributes.removeNamedItem('a');
        return gone.name + ',' + gone.value + ',' + gone.nodeValue + ',' + gone.textContent +
               ',' + (gone.ownerElement === null) + ',' + e.hasAttribute('a');
    })())JS",
       "a,1,1,1,true,false");
    // The namespaced spelling is a second implementation of the same removal
    // and had the same fault.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttributeNS('urn:x', 'p:a', 'v');
        var gone = e.attributes.removeNamedItemNS('urn:x', 'a');
        return gone.value + ',' + gone.namespaceURI + ',' + gone.prefix + ',' + gone.localName;
    })())JS",
       "v,urn:x,p,a");
    // ...and so is `removeAttributeNode`, which is the one that hands back the
    // OBJECT THE PAGE PASSED IN rather than one made at the point of removal.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttribute('a', '1');
        var node = e.getAttributeNode('a');
        var gone = e.removeAttributeNode(node);
        return (gone === node) + ',' + gone.value + ',' + node.value + ',' +
               (node.ownerElement === null);
    })())JS",
       "true,1,1,true");
    // AND AN ATTACHED Attr IS STILL LIVE, which is the half a fix could easily
    // trade away: `attributes.html`'s "Attribute values should not be parsed"
    // writes through one and reads the element back.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttribute('a', '1');
        var held = e.attributes[0];
        e.setAttribute('a', '2');
        var read = held.value;
        held.value = '3';
        return read + ',' + e.getAttribute('a');
    })())JS",
       "2,3");
    // A DETACHED Attr IS WRITABLE and the write goes nowhere near the element.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttribute('a', '1');
        var gone = e.attributes.removeNamedItem('a');
        gone.value = 'x';
        return gone.value + ',' + e.hasAttribute('a');
    })())JS",
       "x,false");
}

// --- a NamedNodeMap is reached by name as well as by index ------------------

void test_the_attribute_map_answers_to_a_name() {
    // `element.attributes.x` IS THE ATTRIBUTE CALLED x. A NamedNodeMap is a
    // legacy platform object with a named property getter and the indexed half
    // was the only half here, so an attribute could be reached by position and
    // not by name.
    //
    // THE SAME Attr, not a second one: an Attr is one node under two ways of
    // reaching it, so `el.attributes[0] === el.attributes.x` - which is also
    // one wrapper per attribute per read rather than two.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttribute('x', 'first');
        var map = e.attributes;
        return map.length + ',' + map.x.value + ',' + (map.x.ownerElement === e) + ',' +
               (map[0] === map.x);
    })())JS",
       "1,first,true,true");
    // A namespace makes no difference to the name it answers to: the QUALIFIED
    // name is the key either way.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttributeNS('foo', 'x', 'first');
        return e.attributes.length + ',' + e.attributes.x.value;
    })())JS",
       "1,first");
    // NEVER OVER A METHOD OR OVER `length`. An attribute a page can name
    // `setNamedItem` or `length` must not break the map it is written into -
    // which is three subtests of `attributes-namednodemap.html` and the reason
    // the named half is a guarded write rather than a loop.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttributeNS('foo', 'setNamedItem', 'first');
        e.setAttributeNS('foo', 'item', 'second');
        e.setAttribute('length', 'third');
        return e.attributes.length + ',' + typeof e.attributes.setNamedItem + ',' +
               typeof e.attributes.item + ',' + typeof e.attributes.toString;
    })())JS",
       "3,function,function,function");
    // A REMOVED ATTRIBUTE STOPS ANSWERING, which is the half an add-only loop
    // would get wrong: the map is refilled in place and keeps its identity, so
    // a name left behind would outlive the attribute it named. Read through
    // `e.attributes` each time, because THAT is the accessor which refills it.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttribute('x', 'first');
        var map = e.attributes;
        var before = map.x.value;
        e.removeAttribute('x');
        return before + ',' + e.attributes.x + ',' + e.attributes.length + ',' +
               (e.attributes === map);
    })())JS",
       "first,undefined,0,true");
}

// --- the five methods that are one operation --------------------------------

void test_the_data_is_measured_in_code_units() {
    // `CharacterData-data.html`, first case: the length of "test" is 4 and it
    // is not a byte count.
    both("return node.data + ',' + node.length;", "test,4");
    // NON-ASCII, WHICH IS WHERE A BYTE COUNT AND A CODE-UNIT COUNT PART. Seven
    // CJK characters are 21 bytes and 7 code units; "test, append more " is 18
    // of each. `CharacterData-appendData.html` asserts exactly 25.
    both(R"(node.appendData(', append more 資料，測試資料');
            return node.length;)",
         "25");
    // ...and one astral character is TWO code units for its four bytes, which
    // is the only place the two counts diverge for a single character.
    both(R"(node.data = '🌠 test 🌠 TEST'; return node.length;)", "15");
    // `data = null` IS THE EMPTY STRING and `data = undefined` is the word:
    // `data` is [LegacyNullToEmptyString], so this is a rule about null alone
    // and ToString would have written four letters for it.
    both("node.data = null; return node.data + ',' + node.length;", ",0");
    both("node.data = undefined; return node.data + ',' + node.length;", "undefined,9");
    both("node.data = 0; return node.data + ',' + node.length;", "0,1");
}

void test_an_offset_throws_where_a_count_clamps() {
    // AN OFFSET PAST THE END IS AN IndexSizeError - a DOMException with the
    // legacy code 1, which is what `assert_throws_dom("IndexSizeError", ...)`
    // and its older spelling `"INDEX_SIZE_ERR"` both check for.
    both("try { node.substringData(5, 0); } catch (e) { return e.name + ',' + e.code + ',' + "
         "(e.constructor === DOMException); } return 'did not throw';",
         "IndexSizeError,1,true");
    // `-1` IS 4294967295, because the offset is a WebIDL `unsigned long` and
    // ToUint32 wraps rather than clamping. So a negative offset throws...
    both("try { node.substringData(-1, 0); } catch (e) { return e.name; } return 'did not throw';",
         "IndexSizeError");
    // ...unless it wraps back INTO range, which is the same rule read the other
    // way and is a subtest of its own in three of these files.
    both("return node.substringData(-0x100000000 + 2, 1);", "s");
    both("node.insertData(-0x100000000 + 2, 'X'); return node.data;", "teXst");
    // A very large offset wraps to a small one rather than throwing.
    both("return node.substringData(0x100000000 + 1, 1);", "e");
    // AND A NON-NUMBER IS ZERO: ToUint32(NaN) is 0, so `substringData("test", 3)`
    // reads from the start.
    both("return node.substringData('test', 3);", "tes");
    // A COUNT CLAMPS instead. Negative is "to the end", and so is 20.
    both("return node.substringData(0, -1) + ',' + node.substringData(2, 20);", "test,st");
    both("node.replaceData(2, -1, 'yo'); return node.data;", "teyo");
    // TOO FEW ARGUMENTS IS A LANGUAGE ERROR, not a DOMException: the argument
    // is missing before any DOM algorithm can look at it.
    both("try { node.substringData(0); } catch (e) { return e.name; } return 'did not throw';",
         "TypeError");
    both("try { node.appendData(); } catch (e) { return e.name; } return 'did not throw';",
         "TypeError");
    // A FAILED CALL CHANGES NOTHING, which is the half of "with invalid offset"
    // that a throw in the wrong place would still pass.
    both("try { node.replaceData(5, 1, 'x'); } catch (e) {} return node.data;", "test");
    // THE ARGUMENTS ARE CONVERTED FIRST, LEFT TO RIGHT, AND THEN THE NODE IS
    // READ. Both halves are observable through a `toString`: the order the
    // three conversions run in, and the fact that an edit one of them makes is
    // in the text the operation then edits. An offset argument that is an
    // OBJECT is what makes the first half visible at all, and it is why these
    // go through the re-entering ToNumber rather than the static one - the
    // static form cannot call back into the VM and answers NaN for every
    // object, which would have made this offset 0.
    both(R"(var seen = [];
            node.replaceData({ toString: function () { seen.push('a'); return 4; } },
                             { toString: function () { seen.push('b'); return 0; } },
                             { toString: function () {
                                   seen.push('c');
                                   node.appendData('X');
                                   return '!';
                               } });
            return seen.join('') + ',' + node.data;)",
         "abc,test!X");
}

void test_the_five_methods_edit_one_string() {
    both("node.appendData('bar'); return node.data;", "testbar");
    // ToString, so null is the WORD here - appendData's argument is an ordinary
    // DOMString and not the [LegacyNullToEmptyString] one `data` is.
    both("node.appendData(null); return node.data;", "testnull");
    both("node.appendData(undefined); return node.data;", "testundefined");
    both("node.insertData(0, 'X'); return node.data;", "Xtest");
    both("node.insertData(4, 'X'); return node.data;", "testX");
    both("node.deleteData(1, 2); return node.data;", "tt");
    both("node.deleteData(0, 4); return node.data;", "");
    both("node.replaceData(1, 1, 'waddup'); node.replaceData(1, 1, 'yup'); return node.data;",
         "tyupaddupst");
    both("node.replaceData(4, 20, 'yo'); return node.data;", "testyo");
    // EVERY ONE OF THEM IN CODE UNITS. This is the case that a byte offset
    // passes every other line of this file and fails: 33 code units into a
    // string with CJK in it is 33 characters and 47 bytes.
    both(R"(node.data = 'This is the character data test, append ' +
                        '資料，更多資料';
            node.replaceData(33, 6, 'other');
            node.replaceData(44, 2, '文字');
            return node.data;)",
         "This is the character data test, other 資料，更多文字");
    both(R"(node.data = 'This is the character data test, other ' +
                        '資料，更多文字';
            return node.substringData(12, 4) + ',' + node.substringData(39, 2);)",
         "char,資料");
    // ...and an astral character counts TWO. Both boundaries here fall between
    // whole characters; one that fell BETWEEN the halves of a surrogate pair
    // cannot be represented in UTF-8 at all - see utf16_to_byte.
    both(R"(node.data = '🌠 test 🌠 TEST';
            return node.substringData(5, 8);)",
         "st 🌠 TE");
    both(R"(node.data = '🌠 test 🌠 TEST';
            node.replaceData(5, 8, '--');
            return node.data;)",
         "🌠 te--ST");
}

// --- Text, which is CharacterData plus two ----------------------------------

void test_split_text_keeps_the_head_and_hands_back_the_tail() {
    is(R"JS((function () {
        var text = document.createTextNode('camembert');
        try { text.splitText(10); } catch (e) { return e.name; }
        return 'did not throw';
    })())JS",
       "IndexSizeError");
    // A DETACHED Text splits and the NEW node stays detached: "Split root"
    // asserts `new_text.parentNode` is null, and that is the case a
    // `parent.appendChild` in the wrong place would break.
    is(R"JS((function () {
        var text = document.createTextNode('comté');
        var made = text.splitText(3);
        return '[' + text.data + '],[' + made.data + '],' + (made.parentNode === null);
    })())JS",
       "[com],[té],true");
    // AN OFFSET IN CODE UNITS here too: "comté" is 5 code units and 6 bytes, so
    // splitting at 5 leaves the whole string behind and an empty tail.
    is(R"JS((function () {
        var text = document.createTextNode('comté');
        var made = text.splitText(5);
        return '[' + text.data + '],[' + made.data + ']';
    })())JS",
       "[comté],[]");
    // ...and an ATTACHED one puts the tail straight after itself.
    is(R"JS((function () {
        var parent = document.createElement('div');
        var text = document.createTextNode('bleu');
        parent.appendChild(text);
        var made = text.splitText(2);
        return text.data + ',' + made.data + ',' + (text.nextSibling === made) + ',' +
               (made.parentNode === parent) + ',' + parent.childNodes.length;
    })())JS",
       "bl,eu,true,true,2");
}

void test_whole_text_stops_at_the_first_element() {
    is(R"JS((function () {
        var parent = document.createElement('div');
        var t1 = document.createTextNode('a');
        var t2 = document.createTextNode('b');
        var t3 = document.createTextNode('c');
        var detached = t1.wholeText;
        parent.appendChild(t1);
        parent.appendChild(t2);
        parent.appendChild(t3);
        var run = t1.wholeText + ',' + t2.wholeText + ',' + t3.wholeText;
        var a = document.createElement('a');
        a.textContent = 'x';
        parent.insertBefore(a, t3);
        return detached + ',' + run + ',' + t1.wholeText + ',' + t3.wholeText;
    })())JS",
       "a,abc,abc,abc,ab,c");
}

// --- the three node interfaces a page may construct -------------------------

void test_text_comment_and_fragment_are_constructible() {
    // THE PROTOTYPE CHAIN IS WHAT THE FILE OPENS WITH, and it is a real chain
    // rather than three names: Text -> CharacterData -> Node, each link the
    // same object the global names.
    for (const char * ctor : {"Text", "Comment"}) {
        is("(function () { var o = new " + std::string{ctor} + "(); return " +
               "(Object.getPrototypeOf(o) === " + ctor + ".prototype) + ',' + " +
               "(Object.getPrototypeOf(Object.getPrototypeOf(o)) === CharacterData.prototype) + " +
               "',' + (Object.getPrototypeOf(Object.getPrototypeOf(Object.getPrototypeOf(o))) " +
               "=== Node.prototype); })()",
           "true,true,true");
        is("(function () { var o = new " + std::string{ctor} +
               "(); return (o instanceof Node) + ',' + (o instanceof CharacterData) + ',' + " +
               "(o instanceof " + ctor + "); })()",
           "true,true,true");
        // NO ARGUMENT IS THE EMPTY STRING and `undefined` is too - the IDL
        // defaults the parameter, and a defaulted argument is not a passed one.
        // The next line is the reason that distinction is written down: the
        // very same coercion in `appendData` writes the WORD "undefined".
        is("(function () { var o = new " + std::string{ctor} +
               "(); return '[' + o.data + '],[' + o.nodeValue + '],' + " +
               "(o.ownerDocument === document) + ',' + (o.parentNode === null); })()",
           "[],[],true,true");
        is("'[' + new " + std::string{ctor} + "(undefined).data + ']'", "[]");
        is("new " + std::string{ctor} + "(null).data", "null");
        is("new " + std::string{ctor} + "(42).data", "42");
        is("new " + std::string{ctor} + "('<!--').data", "<!--");
        // ONE ARGUMENT, CONVERTED ONCE. The second is never looked at, which
        // `Comment-Text-constructor.js` checks with a toString that would fail
        // the test if it ran.
        is("(function () { var seen = []; var o = new " + std::string{ctor} +
               "({ toString: function () { seen.push('first'); return 'text'; } }, " +
               "{ toString: function () { seen.push('second'); return 'no'; } }); " +
               "return o.data + ',' + seen.join('+'); })()",
           "text,first");
    }
    // A CONSTRUCTED NODE IS A REAL ONE: it can be edited by the CharacterData
    // methods and put into the tree.
    is(R"JS((function () {
        var t = new Text('hello');
        t.appendData(' world');
        document.body.appendChild(t);
        return t.data + ',' + (t.parentNode === document.body);
    })())JS",
       "hello world,true");
    // `new DocumentFragment()` takes no argument and is not a CharacterData.
    is(R"JS((function () {
        var f = new DocumentFragment();
        var t = document.createTextNode('');
        f.appendChild(t);
        return f.nodeType + ',' + f.nodeName + ',' + (f.ownerDocument === document) + ',' +
               (f.firstChild === t);
    })())JS",
       "11,#document-fragment,true,true");
    // AND THE OTHER EIGHTY-EIGHT STILL THROW, which is the half that makes the
    // three above a decision rather than an accident: a browser refuses
    // `new HTMLDivElement()` too.
    is("(function () { try { new HTMLDivElement(); } catch (e) { return e.name; } " +
           std::string{"return 'did not throw'; })()"},
       "TypeError");
}

// --- equal is not the same as same ------------------------------------------

void test_two_nodes_are_equal_by_structure_and_same_by_identity() {
    // `isSameNode` IS IDENTITY, so two nodes built the same way are not it -
    // which is the only thing separating it from `isEqualNode` and is why they
    // are two methods.
    is(R"JS((function () {
        var a = document.createTextNode('data');
        var b = document.createTextNode('data');
        return a.isSameNode(a) + ',' + a.isSameNode(b) + ',' + a.isSameNode(null) + ',' +
               a.isEqualNode(b);
    })())JS",
       "true,false,false,true");
    // AN ELEMENT IS COMPARED ON ITS QUALIFIED NAME AND ITS NAMESPACE, and the
    // PREFIX is part of the name: `prefix:localName` and `prefix2:localName`
    // are different elements.
    is(R"JS((function () {
        var one = document.createElementNS('namespace', 'prefix:localName');
        var same = document.createElementNS('namespace', 'prefix:localName');
        var other_ns = document.createElementNS('namespace2', 'prefix:localName');
        var other_prefix = document.createElementNS('namespace', 'prefix2:localName');
        var other_local = document.createElementNS('namespace', 'prefix:localName2');
        var extra = document.createElementNS('namespace', 'prefix:localName');
        extra.setAttribute('foo', 'bar');
        return [one.isEqualNode(one), one.isEqualNode(same), one.isEqualNode(other_ns),
                one.isEqualNode(other_prefix), one.isEqualNode(other_local),
                one.isEqualNode(extra)].join(',');
    })())JS",
       "true,true,false,false,false,false");
    // ...AND AN ATTRIBUTE IS NOT. The two rules are opposite on purpose: an
    // attribute is compared on (namespace, local name, value) and its prefix
    // takes no part, so `prefix:localName` and `prefix2:localName` on two
    // elements leave them EQUAL. That single `true` in the middle is the whole
    // difference between this and comparing serialised markup.
    is(R"JS((function () {
        function el(ns, name, value) {
            var made = document.createElement('element');
            made.setAttributeNS(ns, name, value);
            return made;
        }
        var one = el('namespace', 'prefix:localName', 'value');
        return [one.isEqualNode(el('namespace', 'prefix:localName', 'value')),
                one.isEqualNode(el('namespace2', 'prefix:localName', 'value')),
                one.isEqualNode(el('namespace', 'prefix2:localName', 'value')),
                one.isEqualNode(el('namespace', 'prefix:localName2', 'value')),
                one.isEqualNode(el('namespace', 'prefix:localName', 'value2'))].join(',');
    })())JS",
       "true,false,true,false,false");
    // THE CHILDREN, PAIRWISE AND IN ORDER - "node equality testing should test
    // descendant equality too", which is the recursion and the reason a
    // comparison of the two nodes alone would pass most of this file.
    is(R"JS((function () {
        var a = document.createElement('foo');
        var b = document.createElement('foo');
        var empty = a.isEqualNode(b);
        a.appendChild(document.createComment('data'));
        var one_sided = a.isEqualNode(b);
        b.appendChild(document.createComment('data'));
        var matched = a.isEqualNode(b);
        b.firstChild.data = 'other';
        return empty + ',' + one_sided + ',' + matched + ',' + a.isEqualNode(b);
    })())JS",
       "true,false,true,false");
    // A fragment has nothing of its own to compare and IS its children.
    is(R"JS((function () {
        var a = new DocumentFragment();
        var b = new DocumentFragment();
        var empty = a.isEqualNode(b);
        a.appendChild(document.createTextNode('x'));
        return empty + ',' + a.isEqualNode(b) + ',' + a.isSameNode(b);
    })())JS",
       "true,false,false");
}

} // namespace

int main() {
    test_a_removed_attribute_keeps_its_value();
    test_the_attribute_map_answers_to_a_name();
    test_the_data_is_measured_in_code_units();
    test_an_offset_throws_where_a_count_clamps();
    test_the_five_methods_edit_one_string();
    test_split_text_keeps_the_head_and_hands_back_the_tail();
    test_whole_text_stops_at_the_first_element();
    test_text_comment_and_fragment_are_constructible();
    test_two_nodes_are_equal_by_structure_and_same_by_identity();
    REPORT("character_data");
}
