// The DOM bindings: script driving the page.
//
// v1's bindings were safe because the document owned every node forever and
// nothing was concurrent. v2's hold HANDLES, so the interesting tests are the
// ones v1 could not have failed:
//
//   * a wrapper for a removed element resolves to nothing, and its methods do
//     nothing, rather than writing through a dangling pointer
//   * a mutation from script invalidates the pipeline, so the NEXT frame shows
//     it - script and rendering are not two views that can disagree
//
// Plus the ordinary web-platform surface, checked through the browser rather
// than against the bindings in isolation: a binding that mutates the DOM but
// does not change what is drawn is not working, whatever a unit test says.

import ctbrowser.core;
import ctbrowser.dom;
import ctbrowser.style;
import ctbrowser.layout;
import ctbrowser.paint;
import ctbrowser.raster;
import ctbrowser.script;
import ctbrowser.shell;

#include "check.hpp"
#include <algorithm>
#include <span>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;

namespace {

void check(bool ok, std::string_view what) {
	if (!ok) {
		std::printf("FAIL %s\n", std::string{what}.c_str());
		++ctbrowser_test_failures;
	}
}

// Everything the page ends up drawing as text. The honest way to ask "did the
// page change", since that is what a user sees.
[[nodiscard]] node_id find_id(browser & page, std::string_view want) {
	const auto txn = page.doc().read();
	const atom key = page.atoms().intern("id");
	node_id found{};
	const auto walk = [&](auto && self, node_id at) -> void {
		if (!found && txn.attribute_value(at, key) == want) { found = at; }
		for (const node_id c : txn.children(at)) { self(self, c); }
	};
	walk(walk, txn.root());
	return found;
}

[[nodiscard]] std::string rendered_text(browser & page) {
	std::string out;
	const auto walk = [&](auto && self, const layout::fragment & f) -> void {
		out += f.text;
		for (const auto & c : f.children) { self(self, c); }
	};
	walk(walk, page.fragments());
	return out;
}

[[nodiscard]] std::size_t count_fill(browser & page, color want) {
	std::size_t n = 0;
	for (const auto & layer : page.layers().layers) {
		if (!layer.contents) { continue; }
		for (const auto & c : layer.contents->commands()) {
			if (c.fill == want) { ++n; }
		}
	}
	return n;
}

[[nodiscard]] const std::vector<std::string> & log_of(browser & page) {
	return page.bindings().console_output();
}

// --- the document API -----------------------------------------------------

void test_script_mutates_what_is_drawn() {
	browser page{browser_options{400, 200}};
	page.load_html("<html><body><div id=a>original</div>"
	               "<script>document.getElementById('a').setText('replaced');</script></body></html>");
	check(page.frame().has_value(), "the page renders");
	check(page.script_error().empty(), "the script ran without error");
	// The whole point: the mutation reached the pixels, not just the DOM.
	check(rendered_text(page).find("replaced") != std::string::npos, "setText changed the page");
	check(rendered_text(page).find("original") == std::string::npos, "and removed the old text");
}

void test_attributes_and_classes() {
	browser page{browser_options{400, 300}};
	page.load_html(R"(<html><head><style>
	.hot { background-color: #ff0000 }
	</style></head><body><div id=a>x</div><script>
	var el = document.getElementById('a');
	el.setAttribute('data-role', 'banner');
	el.addClass('hot');
	console.log('role=' + el.getAttribute('data-role'));
	console.log('hot=' + el.hasClass('hot'));
	console.log('cold=' + el.hasClass('cold'));
	</script></body></html>)");
	check(page.frame().has_value(), "the page renders");
	check(page.script_error().empty(), "the script ran without error");

	const auto & log = log_of(page);
	check(log.size() == 3, "three console lines");
	if (log.size() == 3) {
		check(log[0] == "role=banner", "setAttribute then getAttribute round-trips");
		check(log[1] == "hot=true", "hasClass sees the class it added");
		check(log[2] == "cold=false", "and does not see one it did not");
	}
	// And the class actually restyled the element, which is the part that
	// matters: adding a class that changes nothing on screen is not a binding
	// that works.
	check(count_fill(page, color::rgba(255, 0, 0)) == 1, "addClass restyled the element");
}

void test_removeclass_undoes_it() {
	browser page{browser_options{400, 300}};
	page.load_html(R"(<html><head><style>.hot { background-color: #ff0000 }</style></head>
	<body><div id=a class=hot>x</div><script>
	document.getElementById('a').removeClass('hot');
	</script></body></html>)");
	check(page.frame().has_value(), "the page renders");
	check(count_fill(page, color::rgba(255, 0, 0)) == 0, "removeClass unstyled the element");
}

void test_create_and_append() {
	browser page{browser_options{400, 300}};
	page.load_html(R"(<html><body><div id=host></div><script>
	var el = document.createElement('p');
	el.setText('made by script');
	document.getElementById('host').appendChild(el);
	</script></body></html>)");
	check(page.frame().has_value(), "the page renders");
	check(page.script_error().empty(), "the script ran without error");
	check(rendered_text(page).find("made by script") != std::string::npos,
	      "a created element appears once appended");
}

void test_remove_child() {
	browser page{browser_options{400, 300}};
	page.load_html(R"(<html><body><div id=host><p id=gone>remove me</p></div><script>
	var host = document.getElementById('host');
	host.removeChild(document.getElementById('gone'));
	</script></body></html>)");
	check(page.frame().has_value(), "the page renders");
	check(rendered_text(page).find("remove me") == std::string::npos,
	      "a removed element stops being drawn");
}

void test_a_stale_handle_is_inert() {
	browser page{browser_options{400, 300}};
	page.load_html(R"(<html><body><div id=host><p id=doomed>text</p></div><script>
	var doomed = document.getElementById('doomed');
	document.getElementById('host').removeChild(doomed);
	doomed.setText('written to a dead node');
	doomed.addClass('whatever');
	console.log('survived');
	</script></body></html>)");
	check(page.frame().has_value(), "the page renders");
	// v1 held a raw node* here. The handle turns a use-after-free into a
	// lookup that finds nothing, so the writes go nowhere and the page is
	// unharmed - which is the entire argument for handles.
	check(page.script_error().empty(), "writing through a stale handle does not fail the script");
	check(log_of(page).size() == 1 && log_of(page)[0] == "survived",
	      "and execution continues past it");
	check(rendered_text(page).find("written to a dead node") == std::string::npos,
	      "nothing was written to the removed node");
}

void test_layout_is_visible_to_script() {
	browser page{browser_options{400, 300}};
	page.load_html(R"(<html><head><style>#a { width: 123px; height: 45px }</style></head>
	<body><div id=a>x</div><script>
	var el = document.getElementById('a');
	console.log('w=' + el.offsetWidth + ' h=' + el.offsetHeight);
	</script></body></html>)");
	check(page.frame().has_value(), "the page renders");

	// The script ran BEFORE the first layout, so it must report 0 rather than
	// a guess. Reporting a plausible-looking wrong number is worse: a page that
	// sizes itself from it would be silently wrong.
	check(log_of(page).size() == 1, "one console line");
	if (!log_of(page).empty()) {
		check(log_of(page)[0] == "w=0 h=0", "before the first layout, geometry reads as zero");
	}

	// After a layout, a freshly-obtained wrapper sees real numbers.
	auto & bindings = page.bindings();
	(void)bindings;
	page.load_html(R"(<html><head><style>#a { width: 123px; height: 45px }</style></head>
	<body><div id=a>x</div><script>
	function report() { var el = document.getElementById('a');
	  console.log('w=' + el.offsetWidth + ' h=' + el.offsetHeight); }
	setTimeout(report, 0);
	</script></body></html>)");
	check(page.frame().has_value(), "the second page renders");
	check(page.tick(1) == 1, "the timer ran after layout");
	check(page.frame().has_value(), "and the frame after it renders");
	check(!log_of(page).empty() && log_of(page).back() == "w=123 h=45",
	      "after layout, offsetWidth/Height report the real box");
}

// --- events ---------------------------------------------------------------

// --- alert, location, and <a href> -------------------------------------
//
// The last three things v1's script surface had and v2's did not. MDN's
// breakout calls alert() and document.location.reload() the moment the game
// ends, so a page could win and then die on an undefined identifier.

void test_alert_is_recorded() {
	browser page{browser_options{200, 100}};
	page.load_html("<body><script>alert('hello'); alert('again');</script></body>");
	check(page.script_error().empty(), "the script ran");
	check(page.alerts().size() == 2, "both alerts were recorded");
	if (page.alerts().size() == 2) {
		check(page.alerts()[0] == "hello" && page.alerts()[1] == "again", "in order, with text");
	}
}

void test_alert_reaches_the_hook() {
	browser page{browser_options{200, 100}};
	std::vector<std::string> seen;
	page.set_alert_hook([&seen](const std::string & message) { seen.push_back(message); });
	page.load_html("<body><script>alert('modal');</script></body>");
	check(seen.size() == 1 && !seen.empty() && seen[0] == "modal", "the hook saw it");
}

void test_location_reload_reruns_the_page() {
	browser page{browser_options{200, 100}};
	// The script appends a paragraph, so a reload is visible as a page that
	// has ONE again rather than two - a reload re-parses the source, it does
	// not re-run the script over the mutated document.
	page.load_html(R"(<body><div id=host></div><script>
	var p = document.createElement('p');
	p.setAttribute('id', 'added');
	document.getElementById('host').appendChild(p);
	</script></body>)");
	check(page.script_error().empty(), "the script ran");

	// A page that reloads itself from a timer: the request is recorded and
	// drained BETWEEN callbacks, because a reload inside one would destroy the
	// context the callback is running in.
	page.load_html(R"(<body><p>page</p><script>
	setTimeout(function () { document.location.reload(); }, 5);
	var runs = 0;
	</script></body>)");
	check(page.script_error().empty(), "the reloading page ran");
	(void)page.frame();
	check(page.tick(10) == 1, "the timer fired");
	// After the reload the page is fresh: the same timer is armed again.
	check(page.script_error().empty(), "the reloaded page ran too");
	check(page.tick(10) == 1, "and its timer fired, so the script really re-ran");
}

void test_window_and_document_share_one_location() {
	browser page{browser_options{200, 100}};
	page.load_html(R"(<body><script>
	console.log(String(document.location === window.location));
	console.log(String(location === window.location));
	</script></body>)");
	check(log_of(page).size() == 2, "both comparisons logged");
	if (log_of(page).size() == 2) {
		check(log_of(page)[0] == "true" && log_of(page)[1] == "true",
		      "document.location, window.location and location are ONE object");
	}
}

void test_a_link_is_handed_to_the_embedder() {
	browser page{browser_options{400, 200}};
	std::vector<std::string> visited;
	page.set_navigate_hook([&visited](const std::string & url) { visited.push_back(url); });
	page.load_html("<body><a href='https://example.com/x'>a link</a></body>");
	check(page.frame().has_value(), "the page renders");

	// Clicked on the link's TEXT, which is a different node from the <a>.
	(void)page.handle(input_event::mouse_down_at(8, 8));
	(void)page.handle(input_event::mouse_up_at(8, 8));
	check(visited.size() == 1, "the link was followed");
	if (!visited.empty()) { check(visited[0] == "https://example.com/x", "with its href"); }
	check(page.location_href() == "https://example.com/x", "and location.href records it");
}

void test_a_fragment_scrolls_instead_of_navigating() {
	browser page{browser_options{300, 200}};
	std::vector<std::string> visited;
	page.set_navigate_hook([&visited](const std::string & url) { visited.push_back(url); });
	page.load_html(R"(<body><a href='#far'>jump</a>
	<div style='height:1200px'>tall</div>
	<p id=far>the target</p></body>)");
	check(page.frame().has_value(), "the page renders");
	check(page.scroll_y() == 0, "starts at the top");

	(void)page.handle(input_event::mouse_down_at(8, 8));
	(void)page.handle(input_event::mouse_up_at(8, 8));
	check(visited.empty(), "a fragment is NOT handed to the embedder");
	check(page.scroll_y() > 1000, "it scrolled to the target instead");
	check(page.location_hash() == "#far", "and location.hash says where");
}

void test_a_page_can_read_where_a_link_went() {
	browser page{browser_options{300, 200}};
	page.set_navigate_hook([](const std::string &) {});
	page.load_html(R"(<body><a href='/first'>go</a><script>
	document.addEventListener('click', function () { console.log(location.href); });
	</script></body>)");
	check(page.frame().has_value(), "the page renders");

	// A listener runs BEFORE the default action, so the first click logs the
	// href from before it - empty - and the second logs the first link's.
	// That second value is the point: `href` was written once when the object
	// was built, so a page could never see a link it had already followed.
	(void)page.handle(input_event::mouse_down_at(12, 12));
	(void)page.handle(input_event::mouse_up_at(12, 12));
	check(page.location_href() == "/first", "the browser recorded the href");
	(void)page.handle(input_event::mouse_down_at(12, 12));
	(void)page.handle(input_event::mouse_up_at(12, 12));
	check(page.script_error().empty(), "reading location.href from script works");
	check(log_of(page).size() == 2, "the listener fired twice");
	if (log_of(page).size() == 2) {
		check(log_of(page)[1] == "/first", "and location.href is LIVE, not a page-load snapshot");
	}
}

void test_click_dispatch() {
	browser page{browser_options{400, 300}};
	page.load_html(R"(<html><head><style>#a { width: 200px; height: 100px }</style></head>
	<body><div id=a>click me</div><script>
	document.getElementById('a').addEventListener('click', function (e) {
	  console.log('clicked ' + e.type);
	});
	</script></body></html>)");
	check(page.frame().has_value(), "the page renders");
	check(log_of(page).empty(), "nothing fired yet");

	// A click is a press and a release on the same element - which is what
	// makes dragging off a button cancel it.
	(void)page.handle(input_event::mouse_down_at(20, 20));
	(void)page.handle(input_event::mouse_up_at(20, 20));
	check(log_of(page).size() == 1, "the listener fired once");
	if (!log_of(page).empty()) { check(log_of(page)[0] == "clicked click", "with the event type"); }

	// Released somewhere else: no click.
	(void)page.handle(input_event::mouse_down_at(20, 20));
	(void)page.handle(input_event::mouse_up_at(380, 290));
	check(log_of(page).size() == 1, "releasing off the element does not click it");
}

void test_events_bubble_and_can_be_prevented() {
	browser page{browser_options{400, 300}};
	page.load_html(R"(<html><head><style>#outer { width: 300px; height: 200px }</style></head>
	<body><div id=outer><div id=inner>x</div></div><script>
	document.getElementById('inner').addEventListener('click', function (e) {
	  console.log('inner'); e.preventDefault();
	});
	document.getElementById('outer').addEventListener('click', function () { console.log('outer'); });
	document.addEventListener('click', function () { console.log('document'); });
	</script></body></html>)");
	check(page.frame().has_value(), "the page renders");

	(void)page.handle(input_event::mouse_down_at(10, 10));
	(void)page.handle(input_event::mouse_up_at(10, 10));
	const auto & log = log_of(page);
	check(log.size() == 3, "the event reached all three listeners");
	if (log.size() == 3) {
		// Order matters: a listener on the target must see the event before one
		// on its parent, or preventDefault from the inner one is pointless.
		check(log[0] == "inner" && log[1] == "outer" && log[2] == "document",
		      "and bubbled outwards in order");
	}
}

// --- timers and frames ----------------------------------------------------

void test_timers() {
	browser page{browser_options{200, 200}};
	page.load_html(R"(<html><body><script>
	setTimeout(function () { console.log('late'); }, 100);
	setTimeout(function () { console.log('soon'); }, 5);
	</script></body></html>)");
	check(page.frame().has_value(), "the page renders");
	check(page.bindings().pending_timers() == 2, "two timers are armed");

	check(page.tick(1) == 0, "nothing is due after 1ms");
	check(page.tick(10) == 1, "the 5ms timer fires by 11ms");
	check(log_of(page).size() == 1 && log_of(page)[0] == "soon", "and it is the right one");
	check(page.tick(200) == 1, "the 100ms timer fires later");
	check(log_of(page).back() == "late", "in the right order");
	check(page.tick(1000) == 0, "a one-shot timer does not fire twice");
}

void test_interval_repeats_and_can_be_cleared() {
	browser page{browser_options{200, 200}};
	page.load_html(R"(<html><body><script>
	var n = 0;
	var id = setInterval(function () { n = n + 1; console.log('tick ' + n);
	  if (n == 3) { clearInterval(id); } }, 10);
	</script></body></html>)");
	check(page.frame().has_value(), "the page renders");
	for (int i = 0; i < 6; ++i) { (void)page.tick(11); }
	// Three ticks then cleared. An interval that keeps firing after
	// clearInterval is the classic leak, and it only shows up over time.
	check(log_of(page).size() == 3, "the interval fired three times and then stopped");
	check(page.bindings().pending_timers() == 0, "and is no longer armed");
}

void test_request_animation_frame() {
	browser page{browser_options{200, 200}};
	page.load_html(R"(<html><body><script>
	var frames = 0;
	function loop() { frames = frames + 1; console.log('frame ' + frames);
	  if (frames < 3) { requestAnimationFrame(loop); } }
	requestAnimationFrame(loop);
	</script></body></html>)");
	check(page.frame().has_value(), "the page renders");
	check(page.bindings().pending_animation_frames() == 1, "one frame callback is queued");
	for (int i = 0; i < 5; ++i) { (void)page.tick(16); }
	// A rAF callback that re-registers itself is the commonest animation
	// idiom, and running the queue in place would loop forever inside one tick.
	check(log_of(page).size() == 3, "a self-re-registering rAF runs once per tick");
	check(page.bindings().pending_animation_frames() == 0, "and stops when it stops asking");
}

// --- robustness -----------------------------------------------------------

void test_a_broken_script_still_renders() {
	browser page{browser_options{300, 200}};
	page.load_html("<html><body><p>content</p><script>this is not javascript(((</script></body></html>");
	check(page.frame().has_value(), "the page still renders");
	check(!page.script_error().empty(), "and the error is recorded");
	// A page whose script fails must still show its markup. Anything else
	// turns one bad script into a blank window.
	check(rendered_text(page).find("content") != std::string::npos, "the markup is unaffected");
}

void test_window_and_performance() {
	browser page{browser_options{321, 234}};
	page.load_html(R"(<html><body><script>
	console.log('size ' + window.innerWidth + 'x' + window.innerHeight);
	console.log('t0 ' + performance.now());
	</script></body></html>)");
	check(page.frame().has_value(), "the page renders");
	const auto & log = log_of(page);
	check(log.size() == 2, "two console lines");
	if (log.size() == 2) {
		check(log[0] == "size 321x234", "window reports the viewport");
		check(log[1] == "t0 0", "and the page clock starts at zero");
	}
}


// --- input reaches script -------------------------------------------------
//
// This is the gap that made every game unplayable: the browser handled keys
// itself - scrolling, caret movement - and never told the page. A page could
// register a keydown listener and receive nothing, forever, with no error.

void test_keyboard_reaches_script() {
	browser page{browser_options{400, 200}};
	page.load_html(R"(<body><script>
	  document.addEventListener('keydown', function (e) {
	    console.log('down ' + e.code + ' key=' + e.key + ' shift=' + e.shiftKey);
	  });
	  document.addEventListener('keyup', function (e) { console.log('up ' + e.code); });
	</script></body>)");
	check(page.script_error().empty(), "the script ran");

	(void)page.handle(input_event::key_press("ArrowRight"));
	(void)page.handle(input_event::key_release("ArrowRight"));
	(void)page.handle(input_event::key_press("KeyA"));
	(void)page.handle(input_event::key_press("KeyA", true));
	(void)page.handle(input_event::key_press("Space"));

	const auto log = page.bindings().console_output();
	check(log.size() == 5, "five key events reached the page");
	if (log.size() == 5) {
		check(log[0] == "down ArrowRight key=ArrowRight shift=false", "an arrow key");
		// A RELEASE, which did not exist at all before: without it a game that
		// tracks held keys never stops moving.
		check(log[1] == "up ArrowRight", "the release");
		// `code` is the physical key, `key` is what it means - and shift is
		// what makes them differ.
		check(log[2] == "down KeyA key=a shift=false", "a letter");
		check(log[3] == "down KeyA key=A shift=true", "the same letter shifted");
		check(log[4] == "down Space key=  shift=false", "space's key is a space");
	}
}

void test_preventDefault_stops_the_browser_acting() {
	const char * tall = "<body><div style='height:2000px'>tall</div>";

	browser page{browser_options{200, 100}};
	page.load_html(std::string{tall} + R"(<script>
	  document.addEventListener('keydown', function (e) { e.preventDefault(); });
	</script></body>)");
	check(page.frame().has_value(), "the page renders");
	check(page.max_scroll() > 0, "the page is taller than the viewport");
	(void)page.handle(input_event::key_press("Space"));
	check(page.scroll_y() == 0, "a cancelled keydown does not scroll the page");

	// ...and without the listener it does, which is what makes the test above
	// about preventDefault rather than about Space doing nothing.
	browser plain{browser_options{200, 100}};
	plain.load_html(std::string{tall} + "</body>");
	check(plain.frame().has_value(), "the plain page renders");
	(void)plain.handle(input_event::key_press("Space"));
	check(plain.scroll_y() > 0, "an uncancelled Space still scrolls");
}

void test_mouse_reaches_script() {
	browser page{browser_options{400, 200}};
	page.load_html(R"(<body><script>
	  document.addEventListener('mousemove', function (e) {
	    console.log('move ' + e.clientX + ',' + e.clientY);
	  });
	  document.addEventListener('mousedown', function (e) { console.log('down ' + e.button); });
	  document.addEventListener('mouseup', function () { console.log('up'); });
	</script></body>)");
	check(page.script_error().empty(), "the script ran");

	(void)page.handle(input_event::mouse_move_to(40, 12));
	(void)page.handle(input_event::mouse_down_at(40, 12));
	(void)page.handle(input_event::mouse_up_at(40, 12));

	const auto log = page.bindings().console_output();
	check(log.size() == 3, "three mouse events reached the page");
	if (log.size() == 3) {
		// MDN's breakout moves its paddle from clientX alone, so the
		// coordinates are the whole content of the event.
		check(log[0] == "move 40,12", "the pointer position");
		check(log[1] == "down 0", "the left button is 0 in the DOM, not 1");
		check(log[2] == "up", "and the release");
	}
}


// The end-to-end version of the three tests above, against a page nobody wrote
// for this engine: MDN's breakout reads e.code and e.clientX, and if input does
// not reach it the paddle simply never moves. Comparing frames with and without
// input is the assertion, because "the paddle moved" is JS state this test
// cannot see - but it can see the pixels.
// The reason any of this exists: MDN's breakout ENDS by calling alert("GAME
// OVER") and then document.location.reload(). Both were undefined identifiers,
// so the one page in the suite that proves web compatibility died on its own
// game-over - after the point every other test stops looking.
void test_the_breakout_page_survives_its_own_game_over() {
	browser page{browser_options{480, 320}};
	std::ifstream in{"examples-v2/pages/pong.html", std::ios::binary};
	std::ostringstream buffer;
	buffer << in.rdbuf();
	page.load_html(buffer.str());
	check(page.script_error().empty(), "the page loaded");

	// Left alone the paddle never moves, so the ball is missed and the game
	// ends. Bounded so a page that never ends fails the check below instead of
	// hanging.
	for (int frame = 0; frame < 2000 && page.alerts().empty(); ++frame) {
		(void)page.tick(1000.0 / 60.0);
		(void)page.frame();
	}
	check(!page.alerts().empty(), "the game ended and alerted");
	if (!page.alerts().empty()) { check(page.alerts()[0] == "GAME OVER", "with GAME OVER"); }
	check(page.script_error().empty(), "and reloading itself did not break the script");

	// The reload really re-ran the page: it is playing again, so it can end
	// AGAIN rather than sitting on a dead context.
	const std::size_t after_first = page.alerts().size();
	for (int frame = 0; frame < 2000 && page.alerts().size() == after_first; ++frame) {
		(void)page.tick(1000.0 / 60.0);
		(void)page.frame();
	}
	check(page.alerts().size() > after_first, "and the reloaded game runs and ends too");
}

void test_a_real_page_responds_to_input() {
	const auto render = [](std::string_view held) {
		browser page{browser_options{480, 320}};
		std::ifstream in{"examples-v2/pages/pong.html", std::ios::binary};
		std::ostringstream buffer;
		buffer << in.rdbuf();
		page.load_html(buffer.str());
		if (!held.empty()) { (void)page.handle(input_event::key_press(std::string{held})); }
		for (int frame = 0; frame < 20; ++frame) {
			(void)page.tick(1000.0 / 60.0);
			(void)page.frame();
		}
		const auto image = page.read_pixels();
		std::vector<std::uint32_t> pixels;
		if (image) {
			for (int y = 0; y < image->height(); ++y) {
				const auto row = image->row(y);
				pixels.insert(pixels.end(), row.begin(), row.end());
			}
		}
		return pixels;
	};

	const std::vector<std::uint32_t> idle = render("");
	const std::vector<std::uint32_t> pressed = render("ArrowRight");
	check(!idle.empty(), "the page rendered");
	check(idle.size() == pressed.size(), "both runs are the same size");
	check(idle != pressed, "holding a key changes what MDN's breakout draws");

	// The control: a key the page does not read must change NOTHING. Without
	// it, "the frames differ" could just mean the run is not reproducible, and
	// the test above would pass whether or not input worked.
	check(render("KeyQ") == idle, "a key the page ignores changes nothing");
}

// The same question asked of the ported example page, because it is the one
// whose key names I had to change: e.key was "Left", which nothing produces.
void test_the_invaders_page_responds_to_input() {
	const auto ship_row = [](std::string_view held) {
		browser page{browser_options{320, 240}};
		std::ifstream in{"examples-v2/pages/invaders.html", std::ios::binary};
		std::ostringstream buffer;
		buffer << in.rdbuf();
		page.load_html(buffer.str());
		if (!held.empty()) { (void)page.handle(input_event::key_press(std::string{held})); }
		for (int frame = 0; frame < 30; ++frame) {
			(void)page.tick(1000.0 / 60.0);
			(void)page.frame();
		}
		// The ship's row of the canvas: moving left or right changes it, and
		// the drifting aliens above do not touch it.
		std::vector<std::uint32_t> row;
		if (const auto pixels = page.canvases().pixels_of(find_id(page, "game"))) {
			for (int x = 0; x < pixels->width; ++x) { row.push_back(pixels->at(x, 224)); }
		}
		return row;
	};

	const std::vector<std::uint32_t> still = ship_row("");
	check(!still.empty(), "the game drew a ship");
	check(ship_row("ArrowLeft") != still, "holding left moves the ship");
	check(ship_row("KeyQ") == still, "a key the page ignores does not");
}

// Space fires. It did not, and the reason was not input at all: the page tests
// `e.code === "Space"`, and `===` compared string ALLOCATIONS, so it was false
// for every event. Counting the bullet's own colour is what distinguishes
// "the key arrived" from "the page acted on it".
void test_the_invaders_page_shoots() {
	browser page{browser_options{320, 240}};
	// run_app installs playSound; a bare browser does not, and the page must
	// not depend on that to fire.
	page.define_native("playSound", [](script::context &, std::span<script::value>) {
		return script::value::boolean(true);
	});
	std::ifstream in{"examples-v2/pages/invaders.html", std::ios::binary};
	std::ostringstream buffer;
	buffer << in.rdbuf();
	page.load_html(buffer.str());

	const auto bullet_pixels = [&] {
		std::size_t found = 0;
		if (const auto pixels = page.canvases().pixels_of(find_id(page, "game"))) {
			for (int y = 0; y < pixels->height; ++y) {
				for (int x = 0; x < pixels->width; ++x) {
					if (pixels->at(x, y) == 0xFFFFFF00U) { ++found; } // the bullet's yellow
				}
			}
		}
		return found;
	};
	const auto run = [&](int frames) {
		for (int i = 0; i < frames; ++i) {
			(void)page.tick(1000.0 / 60.0);
			(void)page.frame();
		}
	};

	run(5);
	check(bullet_pixels() == 0, "nothing is firing yet");
	(void)page.handle(input_event::key_press("Space"));
	run(5);
	check(bullet_pixels() > 0, "Space fires a bullet");
}

// A letterboxed page is authored at its LOGICAL size and the window only
// decides how big that gets drawn. SDL announces the window's pixel size on the
// first frame, and taking that as a page resize left the canvas - 320x240 by
// its own attributes - occupying a ninth of the viewport.
void test_a_letterboxed_page_keeps_its_size() {
	browser page{browser_options{320, 240}};
	std::ifstream in{"examples-v2/pages/invaders.html", std::ios::binary};
	std::ostringstream buffer;
	buffer << in.rdbuf();
	page.load_html(buffer.str());
	check(page.frame().has_value(), "the page renders");

	const auto canvas_box = [&] {
		const node_id want = find_id(page, "game");
		const auto walk = [&](auto && self, const layout::fragment & f, float dx,
		                      float dy) -> rect {
			const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
			if (f.source == want) { return box; }
			for (const auto & child : f.children) {
				if (const rect hit = self(self, child, box.x, box.y); !hit.empty()) { return hit; }
			}
			return rect{};
		};
		return walk(walk, page.fragments(), 0, 0);
	};
	// The canvas fills the logical viewport exactly, which is the whole point
	// of authoring at 320x240 and letting SDL scale it.
	check(canvas_box().width == 320.0f, "the canvas is as wide as the page");
	check(canvas_box().height == 240.0f, "and as tall");
}

} // namespace

int main() {
	test_script_mutates_what_is_drawn();
	test_attributes_and_classes();
	test_removeclass_undoes_it();
	test_create_and_append();
	test_remove_child();
	test_a_stale_handle_is_inert();
	test_layout_is_visible_to_script();

	test_alert_is_recorded();
	test_alert_reaches_the_hook();
	test_location_reload_reruns_the_page();
	test_window_and_document_share_one_location();
	test_a_link_is_handed_to_the_embedder();
	test_a_fragment_scrolls_instead_of_navigating();
	test_a_page_can_read_where_a_link_went();
	test_click_dispatch();
	test_events_bubble_and_can_be_prevented();

	test_timers();
	test_interval_repeats_and_can_be_cleared();
	test_request_animation_frame();

	test_a_broken_script_still_renders();
	test_window_and_performance();
	test_keyboard_reaches_script();
	test_preventDefault_stops_the_browser_acting();
	test_mouse_reaches_script();
	test_a_real_page_responds_to_input();
	test_the_breakout_page_survives_its_own_game_over();
	test_the_invaders_page_responds_to_input();
	test_the_invaders_page_shoots();
	test_a_letterboxed_page_keeps_its_size();

	REPORT("bindings_basics");
}
