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
	// Enough paragraphs to overflow a short viewport. NOT a styled height: the
	// `style` ATTRIBUTE is not implemented (only <style> elements are), and a
	// test about keyboard defaults should not quietly depend on that.
	const char * tall = "<body><p>one</p><p>two</p><p>three</p><p>four</p><p>five</p>"
	                    "<p>six</p><p>seven</p><p>eight</p>";

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

} // namespace

int main() {
	test_script_mutates_what_is_drawn();
	test_attributes_and_classes();
	test_removeclass_undoes_it();
	test_create_and_append();
	test_remove_child();
	test_a_stale_handle_is_inert();
	test_layout_is_visible_to_script();

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
	test_the_invaders_page_responds_to_input();

	REPORT("bindings_basics");
}
