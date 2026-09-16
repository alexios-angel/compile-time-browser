// THE PARSER-DRIVEN DOCUMENT: document.open / write / writeln / close, and the
// parser stopping at every `</script>` (HTML 8.4, 13.2.6.4.8).
//
// Each case is one page and one logged answer, the way unit/document_api_wpt
// does it, and each is named for the html/webappapis/dynamic-markup-insertion
// file whose assertion it carries. What they have in common: the script runs
// while the tree is half built, and what it writes is parsed WHERE IT IS.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

namespace {

using ctbrowser::browser;
using ctbrowser::browser_options;

// The page as written, run, and the last thing it logged. No script is
// appended: every case here puts its own <script> where the test needs it.
[[nodiscard]] std::string logged(const std::string & html) {
    browser page{browser_options{400, 300}};
    page.load_html(html);
    const std::vector<std::string> & out = page.bindings().console_output();
    if (out.empty()) { return "<nothing logged: " + page.script_error() + ">"; }
    return out.back();
}

void is(const std::string & html, const std::string & expected) {
    const std::string got = logged(html);
    CHECK_EQ(got, expected);
}

// --- the parser stops at </script> --------------------------------------------

void test_a_script_sees_the_tree_built_so_far() {
    // A script in the head runs before the body exists - `document.body` is
    // null there in every browser, and was the whole body here.
    is("<html><head><script>console.log(document.body)</script></head><body></body></html>",
       "null");
    // And one in the body sees what precedes it, not what follows.
    is("<body><p id=a></p><script>console.log(document.getElementById('a') !== null"
       " && document.getElementById('b') === null)</script><p id=b></p></body>",
       "true");
    // document-write/001.html and its readyState: "loading" while a
    // parser-inserted script runs.
    is("<body><script>console.log(document.readyState)</script></body>", "loading");
}

// --- document.write lands at the insertion point --------------------------------

void test_write_inserts_at_the_insertion_point() {
    // document-write/001.html: written text is in the body, where the script is.
    is("<body><script>document.write('PASS'); console.log(document.body.textContent)"
       "</script></body>",
       "PASS");
    // What is written goes BEFORE the markup that follows the script.
    is("<body><script>document.write('<p id=w></p>')</script><p id=after></p>"
       "<script>console.log(document.getElementById('w').nextElementSibling.id)</script></body>",
       "after");
    // document-write/002.html: an unclosed <i> stays open across what follows.
    is("<body><script>document.write('<i>Filler Text')</script>more"
       "<script>var i = document.body.firstChild;"
       " console.log(i.localName + ':' + i.textContent)</script></body>",
       "i:Filler Textmore");
    // document-write/010.html: two half-writes are one tag, read once whole.
    is("<body><script>document.write('<i id='); document.write(\"'test'>Filler Text\");"
       " var i = document.body.firstChild;"
       " console.log(i.localName + ':' + i.getAttribute('id') + ':' + i.textContent)"
       "</script></body>",
       "i:test:Filler Text");
    // Two writes of text are ONE Text node, not two (13.2.6.1 "insert a
    // character" appends to the Text node before the insertion point).
    is("<body><script>document.write('a'); document.write('b');"
       " console.log(document.body.childNodes.length + ':' + document.body.firstChild.data)"
       "</script></body>",
       "1:ab");
    // writeln adds the newline.
    is("<body><script>document.writeln('x'); console.log(JSON.stringify(document.body."
       "firstChild.data))</script></body>",
       "\"x\\n\"");
}

// --- a written script runs before the writer's next statement ------------------

void test_a_written_script_runs_synchronously_and_nests() {
    // document-write/script_002.html: the written script runs inside write().
    is("<body><script>var order = [];"
       " document.write('<script>order.push(1);<' + '/script>'); order.push(2);</script>"
       "<script>order.push(3); console.log(order.join(','))</script></body>",
       "1,2,3");
    // document-write/script_004.html: a written script writing a script, in
    // order - each inner one runs before its writer continues.
    is("<body><script>var order = []; order.push(1);"
       " document.write('<script>order.push(2); document.write(\\'<script>order.push(3);"
       "</script\\' + \\'>\\'); order.push(4);<' + '/script>'); order.push(5);</script>"
       "<script>console.log(order.join(','))</script></body>",
       "1,2,3,4,5");
    // document.currentScript is the written script while it runs and the
    // writer again afterwards.
    is("<body><script id=outer>"
       " document.write('<script id=inner>window.seen = document.currentScript.id;<' + "
       "'/script>');"
       " console.log(window.seen + ',' + document.currentScript.id)</script></body>",
       "inner,outer");
}

// --- open() replaces the document, close() ends it ------------------------------

void test_open_write_close_replace_the_document() {
    // opening-the-input-stream/002.html-style: after open() the document is
    // empty, writes build a new one, and close() finishes it.
    browser page{browser_options{400, 300}};
    page.load_html("<html><body><p id=old>old</p></body></html>");
    CHECK(page.run_script("document.open(); document.write('<p id=fresh>new</p>');"
                          " document.close();"
                          " console.log(document.getElementById('old') + ':' +"
                          " document.getElementById('fresh').textContent + ':' +"
                          " document.documentElement.outerHTML)"));
    CHECK_EQ(page.bindings().console_output().back(),
             "null:new:<html><head></head><body><p id=\"fresh\">new</p></body></html>");
    // open() from inside a parser-inserted script is a no-op (8.4.2 step 4).
    is("<body><p id=keep></p><script>document.open();"
       " console.log(document.getElementById('keep') !== null)</script></body>",
       "true");
    // A write with no parser opens one (8.4.4 step 4): the page is replaced.
    browser late{browser_options{400, 300}};
    late.load_html("<html><body><p id=old>old</p></body></html>");
    CHECK(late.run_script("document.write('<b>late</b>');"
                          " console.log(document.getElementById('old') + ':' +"
                          " document.body.innerHTML + ':' + document.readyState)"));
    CHECK_EQ(late.bindings().console_output().back(), "null:<b>late</b>:loading");
    // ...and close() brings readyState to complete and fires load again, on
    // a later turn.
    CHECK(late.run_script("window.addEventListener('load', function () {"
                          " console.log('load:' + document.readyState); });"
                          " document.close(); console.log(document.readyState)"));
    CHECK_EQ(late.bindings().console_output().back(), "interactive");
    (void)late.tick(16);
    CHECK_EQ(late.bindings().console_output().back(), "load:complete");
}

// --- a frame's document, the html5lib way ---------------------------------------

void test_a_frame_document_can_be_written() {
    // html5lib_write.html: `frame.contentDocument.open(); write(input);
    // close()`, then the frame's tree is read back on its load event.
    is("<body><script>"
       " var f = document.createElement('iframe'); document.body.appendChild(f);"
       " var d = f.contentDocument; d.open(); d.write('<!DOCTYPE html><p>a<p>b'); d.close();"
       " console.log(d.compatMode + ':' + d.body.children.length + ':' +"
       " d.documentElement.outerHTML)"
       "</script></body>",
       "CSS1Compat:2:<html><head></head><body><p>a</p><p>b</p></body></html>");
    // insert-into-nonempty-document.html: after open() a doctype write leaves
    // the doctype the Document's ONLY child - the <html> element is "before
    // html"'s to make, when something needs it.
    is("<body><script>"
       " var f = document.createElement('iframe'); document.body.appendChild(f);"
       " var d = f.contentDocument; d.open(); d.write('<!DOCTYPE html>');"
       " console.log(d.childNodes.length + ':' + d.doctype.name + ':' + d.documentElement)"
       "</script></body>",
       "1:html:null");
}

} // namespace

int main() {
    test_a_script_sees_the_tree_built_so_far();
    test_write_inserts_at_the_insertion_point();
    test_a_written_script_runs_synchronously_and_nests();
    test_open_write_close_replace_the_document();
    test_a_frame_document_can_be_written();
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
