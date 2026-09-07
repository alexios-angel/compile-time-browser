// CharacterData, WHICH IS FOUR NODE TYPES SHARING ONE STRING - and the string
// is measured in UTF-16 CODE UNITS while this engine stores UTF-8.
//
// That is the whole reason this file exists rather than being more of
// element_attrs or bindings_basics. Every offset in `CharacterData` and in
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

} // namespace

int main() {
    test_a_removed_attribute_keeps_its_value();
    REPORT("character_data");
}
