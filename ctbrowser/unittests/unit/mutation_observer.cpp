// MutationObserver: the records a diff can reconstruct, and when they arrive.
//
// This is the regression net for lib/Shell/bindings/mutation.cpp, and it asks
// the two questions no other test in the suite asks:
//
//   * WHAT SHAPE IS A RECORD. `addedNodes` has to be a real empty array and
//     `previousSibling` a real null, because that is what almost every
//     assertion in web-platform-tests' MutationObserver files reads first. A
//     record whose absent fields are `undefined` fails on the shape before the
//     test gets to what it was about.
//   * WHEN DOES THE CALLBACK RUN. Delivery is a MICROTASK, so a mutation and
//     its record are never in the same statement - and several mutations in one
//     script turn are ONE callback holding several records. Half these WPT
//     files are counting records per callback, so a delivery that fired per
//     mutation would look like it worked and be wrong everywhere.
//
// The page's own `alert()` is the observation channel: the browser keeps them
// in order, they survive the turn the callback ran in, and a test that reads
// them is reading exactly what a page could read.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include <string>
#include <string_view>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

// Load a page and return everything it said, in order, joined with `;`. A
// script fault is appended rather than swallowed: a test whose page threw
// halfway through would otherwise report a short list and blame the feature.
[[nodiscard]] std::string said(std::string_view html) {
    browser page{browser_options{200, 100}};
    page.load_html(html);
    std::string out;
    for (const std::string & one : page.alerts()) {
        if (!out.empty()) { out += ';'; }
        out += one;
    }
    if (!page.script_error().empty()) { out += "|error:" + page.script_error(); }
    return out;
}

// THE VALIDATION HALF, and it is the half that is all-or-nothing: five
// MutationObserver files in dom/nodes are a fast FAIL without it and a
// ten-second TIMEOUT with only the validation, because their last subtest is an
// async_test waiting for a record that never comes. Both halves or neither.
void test_observe_validates_its_options() {
    CHECK_EQ(said("<html><body><p id=t></p><script>"
                  "function threw(f){ try { f(); return 'ok'; } catch (e) { return e.name; } }"
                  "var t = document.getElementById('t');"
                  "var m = new MutationObserver(function () {});"
                  // Nothing asked for at all.
                  "alert(threw(function(){ m.observe(t, {}); }));"
                  // An old value or a filter with attributes explicitly OFF.
                  "alert(threw(function(){ m.observe(t, {childList:true, attributeOldValue:true,"
                  " attributes:false}); }));"
                  "alert(threw(function(){ m.observe(t, {childList:true, attributeFilter:['a'],"
                  " attributes:false}); }));"
                  "alert(threw(function(){ m.observe(t, {childList:true,"
                  " characterDataOldValue:true, characterData:false}); }));"
                  // ...and the same three with attributes OMITTED, which turns
                  // attribute observation on rather than throwing. Present, not
                  // true: `attributeOldValue: false` still enables it.
                  "alert(threw(function(){ m.observe(t, {attributeOldValue:true}); m.disconnect();"
                  " }));"
                  "alert(threw(function(){ m.observe(t, {attributeOldValue:false}); m.disconnect();"
                  " }));"
                  "alert(threw(function(){ m.observe(t, {attributeFilter:['a']}); m.disconnect();"
                  " }));"
                  "alert(threw(function(){ m.observe(t, {characterDataOldValue:true});"
                  " m.disconnect(); }));"
                  // And a callback that is not callable is a TypeError from the
                  // constructor, before any of the above can be reached.
                  "alert(threw(function(){ new MutationObserver(42); }));"
                  "</script></body></html>"),
             "TypeError;TypeError;TypeError;TypeError;ok;ok;ok;ok;TypeError");
}

// One childList record, every field of it, and the fact that it arrives after
// the script rather than inside the appendChild.
void test_one_child_list_record() {
    CHECK_EQ(said("<html><body><div id=host></div><script>"
                  "var host = document.getElementById('host');"
                  "var m = new MutationObserver(function (records, observer) {"
                  "  var r = records[0];"
                  "  alert('n=' + records.length);"
                  "  alert('type=' + r.type);"
                  "  alert('target=' + (r.target === host));"
                  "  alert('added=' + r.addedNodes.length + ',' + r.addedNodes[0].tagName);"
                  "  alert('removed=' + r.removedNodes.length);"
                  "  alert('siblings=' + r.previousSibling + ',' + r.nextSibling);"
                  "  alert('absent=' + r.attributeName + ',' + r.attributeNamespace + ',' +"
                  "        r.oldValue);"
                  "  alert('callback=' + (this === m) + ',' + (observer === m));"
                  "  alert('record=' + (r instanceof MutationRecord));"
                  "});"
                  "m.observe(host, {childList: true});"
                  "host.appendChild(document.createElement('span'));"
                  // Printed BEFORE anything above: a delivery that ran from
                  // inside appendChild would put this last.
                  "alert('during-the-turn');"
                  "</script></body></html>"),
             "during-the-turn;n=1;type=childList;target=true;added=1,SPAN;removed=0;"
             "siblings=null,null;absent=null,null,null;callback=true,true;record=true");
}

// The siblings an insertion names, which is the part of the diff that has to
// know WHERE in the child list the new node landed rather than only that it is
// there.
void test_a_record_names_the_siblings_of_what_changed() {
    CHECK_EQ(said("<html><body><div id=host><i id=a></i><i id=b></i></div><script>"
                  "var host = document.getElementById('host');"
                  "var b = document.getElementById('b');"
                  "var m = new MutationObserver(function (records) {"
                  "  var r = records[0];"
                  "  alert('added=' + r.addedNodes[0].tagName);"
                  "  alert('prev=' + r.previousSibling.id + ',next=' + r.nextSibling.id);"
                  "});"
                  "m.observe(host, {childList: true});"
                  "host.insertBefore(document.createElement('u'), b);"
                  "</script></body></html>"),
             "added=U;prev=a,next=b");
}

// TWO MUTATIONS, ONE CALLBACK, TWO RECORDS - which is what makes the delivery a
// microtask rather than a call from the mutation, and what half of these WPT
// files are actually counting. And the old values, which only a per-CALL diff
// can get right: one diff at delivery time would see the first value and the
// last and report a single record.
void test_two_attribute_writes_batch_into_one_callback() {
    CHECK_EQ(said("<html><body><div id=host class=one></div><script>"
                  "var host = document.getElementById('host');"
                  "var calls = 0;"
                  "var m = new MutationObserver(function (records) {"
                  "  calls = calls + 1;"
                  "  var out = '';"
                  "  for (var i = 0; i < records.length; i++) {"
                  "    out = out + records[i].type + ':' + records[i].attributeName + ':' +"
                  "          records[i].oldValue + ' ';"
                  "  }"
                  "  alert('calls=' + calls + ' n=' + records.length);"
                  "  alert(out);"
                  "});"
                  "m.observe(host, {attributes: true, attributeOldValue: true});"
                  "host.setAttribute('class', 'two');"
                  "host.setAttribute('class', 'three');"
                  "</script></body></html>"),
             "calls=1 n=2;attributes:class:one attributes:class:two ");
}

// attributeFilter, and the record for an attribute that did not exist before -
// whose oldValue is null even under attributeOldValue, there being no old value.
void test_attribute_filter_and_a_new_attribute() {
    CHECK_EQ(said("<html><body><div id=host></div><script>"
                  "var host = document.getElementById('host');"
                  "var m = new MutationObserver(function (records) {"
                  "  alert('n=' + records.length);"
                  "  alert(records[0].attributeName + ':' + records[0].oldValue);"
                  "});"
                  "m.observe(host, {attributes: true, attributeOldValue: true,"
                  "                 attributeFilter: ['data-x']});"
                  "host.setAttribute('data-y', '1');"
                  "host.setAttribute('data-x', '2');"
                  "</script></body></html>"),
             "n=1;data-x:null");
}

// characterData on a text node, with the old value, and `subtree` reaching a
// descendant's attributes.
void test_character_data_and_subtree() {
    CHECK_EQ(said("<html><body><p id=p>hello</p><script>"
                  "var text = document.getElementById('p').firstChild;"
                  "var m = new MutationObserver(function (records) {"
                  "  alert('n=' + records.length + ' ' + records[0].type + ':' +"
                  "        records[0].oldValue + ':' + (records[0].target === text));"
                  "});"
                  "m.observe(text, {characterData: true, characterDataOldValue: true});"
                  "text.data = 'world';"
                  "</script></body></html>"),
             "n=1 characterData:hello:true");

    CHECK_EQ(said("<html><body><div id=root><span id=inner></span></div><script>"
                  "var root = document.getElementById('root');"
                  "var inner = document.getElementById('inner');"
                  "var m = new MutationObserver(function (records) {"
                  "  alert('n=' + records.length + ' ' + records[0].type + ':' +"
                  "        (records[0].target === inner));"
                  "});"
                  "m.observe(root, {subtree: true, attributes: true});"
                  "inner.setAttribute('x', '1');"
                  "</script></body></html>"),
             "n=1 attributes:true");
}

// "REPLACE ALL" IS ONE RECORD NAMING BOTH LISTS, not a removal record and an
// addition record. `textContent = "x"` over existing children is the case, and
// MutationObserver-textContent.html asserts it three ways.
void test_replacing_the_children_is_one_record() {
    CHECK_EQ(said("<html><body><div id=host><span>old</span></div><script>"
                  "var host = document.getElementById('host');"
                  "var m = new MutationObserver(function (records) {"
                  "  alert('n=' + records.length + ' removed=' + records[0].removedNodes.length +"
                  "        ' added=' + records[0].addedNodes.length);"
                  "});"
                  "m.observe(host, {childList: true});"
                  "host.textContent = 'new';"
                  "</script></body></html>"),
             "n=1 removed=1 added=1");
}

// takeRecords empties the queue, so the callback the delivery microtask would
// have made never happens - and a second takeRecords answers with nothing.
void test_take_records_empties_the_queue() {
    CHECK_EQ(said("<html><body><div id=host></div><script>"
                  "var host = document.getElementById('host');"
                  "var fired = 0;"
                  "var m = new MutationObserver(function () { fired = fired + 1; });"
                  "m.observe(host, {attributes: true});"
                  "host.setAttribute('a', '1');"
                  "var taken = m.takeRecords();"
                  "alert('taken=' + taken.length + ' ' + taken[0].type + ':' +"
                  "      taken[0].attributeName);"
                  "alert('again=' + m.takeRecords().length);"
                  "</script><script>alert('fired=' + fired);</script></body></html>"),
             "taken=1 attributes:a;again=0;fired=0");
}

// disconnect drops the registrations AND the queue: a mutation made before it
// is discarded rather than delivered late, and mutations after it are not seen
// at all. Re-observing afterwards works, which is what the second half of
// MutationObserver-disconnect.html walks through three times.
void test_disconnect_drops_the_registrations_and_the_queue() {
    CHECK_EQ(said("<html><body><div id=host></div><script>"
                  "var host = document.getElementById('host');"
                  "var seen = 'none';"
                  "var m = new MutationObserver(function (records) {"
                  "  seen = records.length + ':' + records[0].attributeName + ':' +"
                  "         records[0].oldValue;"
                  "});"
                  "m.observe(host, {attributes: true});"
                  "host.setAttribute('id', 'first');"
                  "m.disconnect();"
                  "host.setAttribute('id', 'second');"
                  "m.observe(host, {attributes: true, attributeOldValue: true});"
                  "host.setAttribute('id', 'third');"
                  "</script><script>alert(seen);</script></body></html>"),
             "1:id:second");
}

// Observing a target a second time REPLACES the registration rather than adding
// one beside it - otherwise every mutation after a re-observe would deliver two
// copies of the same record.
void test_observing_the_same_target_twice_replaces_it() {
    CHECK_EQ(said("<html><body><div id=host></div><script>"
                  "var host = document.getElementById('host');"
                  "var m = new MutationObserver(function (records) {"
                  "  alert('n=' + records.length + ' old=' + records[0].oldValue);"
                  "});"
                  "m.observe(host, {attributes: true});"
                  "m.observe(host, {attributes: true, attributeOldValue: true});"
                  "host.setAttribute('id', 'changed');"
                  "</script></body></html>"),
             "n=1 old=host");
}

// A page with no observer pays nothing and behaves as it always did - the point
// being that `record_mutations()` is on the path of EVERY DOM write a script
// makes, so a fault in it would surface everywhere rather than here.
void test_a_page_without_an_observer_is_untouched() {
    CHECK_EQ(said("<html><body><div id=host></div><script>"
                  "var host = document.getElementById('host');"
                  "host.setAttribute('class', 'x');"
                  "host.appendChild(document.createElement('span'));"
                  "alert(host.className + ',' + host.childNodes.length);"
                  "alert(typeof MutationObserver + ',' + typeof MutationRecord);"
                  "</script></body></html>"),
             "x,1;function,function");
}

} // namespace

int main() {
    test_observe_validates_its_options();
    test_one_child_list_record();
    test_a_record_names_the_siblings_of_what_changed();
    test_two_attribute_writes_batch_into_one_callback();
    test_attribute_filter_and_a_new_attribute();
    test_character_data_and_subtree();
    test_replacing_the_children_is_one_record();
    test_take_records_empties_the_queue();
    test_disconnect_drops_the_registrations_and_the_queue();
    test_observing_the_same_target_twice_replaces_it();
    test_a_page_without_an_observer_is_untouched();

    REPORT("mutation_observer");
}
