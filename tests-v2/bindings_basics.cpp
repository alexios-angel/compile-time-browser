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

	REPORT("bindings_basics");
}
