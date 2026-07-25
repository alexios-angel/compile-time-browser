#ifndef CTBROWSER__SCRIPT__HPP
#define CTBROWSER__SCRIPT__HPP

#include <cstdint>

#include <cstddef>

#include "dom.hpp"
#include "image.hpp"
#include "font8x8.hpp"
#include "utf.hpp"
#include <ctjs.hpp>
#ifndef CTBROWSER_IN_A_MODULE
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#endif

// The DOM API scripts see, delivered through ctjs host bindings.
// Element handles are JS objects whose methods are native closures
// capturing the node pointer; a canvas context is a JS object whose
// fillStyle property the script assigns and whose fillRect/putPixel
// natives read it back - the real canvas idiom, no special cases in
// the interpreter.
//
//   let el = getElementById("status");
//   el.setText("ready");  el.toggleClass("ok");  el.style("color", "red");
//
//   let ctx = getContext("game");             // or canvas.getContext("2d")
//   ctx.fillStyle = "#ff8800";
//   ctx.fillRect(10, 10, 50, 30);
//   ctx.beginPath(); ctx.arc(60, 60, 10, 0, Math.PI*2); ctx.fill();
//
// Two event styles coexist, both first-class:
//  - ctbrowser conventions: the shell calls the script's globals
//    onClick(id) / onKey(key, down) / onFrame(dt) when they exist;
//  - the web platform: document.getElementById/addEventListener with
//    DOM event objects (e.code "ArrowRight", e.clientX),
//    requestAnimationFrame, alert, document.location.reload - enough
//    that an unmodified MDN-tutorial page (examples/pong.html) runs.
//
// And the network, resolved at BUILD time:
//   const response = await fetch("https://.../config.json");
//   const config = await response.json();   // also text(), bytes()
// fetch() serves from the embedded-asset registry - every fetch("...")
// literal was already fetched at compile time by assets.hpp on
// --fetch-allow builds - so the promises are settled by construction
// (which is the exact subset ctjs implements). Un-baked URLs reject
// with a TypeError, the same shape a real network failure has.

namespace ctbrowser {

namespace detail {
// CTBROWSER_DEBUG (any value): diagnostics print full JS error stacks and the
// engine reports a failed page script. Cheap: getenv is read once.
[[nodiscard]] inline bool debug_on() {
	static const bool on = std::getenv("CTBROWSER_DEBUG") != nullptr;
	return on;
}
// the error's captured call-stack trace ("Msg\n  at f\n  at g"), else its message
inline std::string error_trace(const ctjs::value & v) {
	if (v.is_object()) {
		if (const ctjs::value * s = v.as_object()->find("stack")) { return s->to_string(); }
	}
	return ctjs::error_to_string(v);
}

// --- JS argument readers ---------------------------------------------
// Every host native takes `const std::vector<value> &` and has to cope
// with arguments that are missing or of the wrong type. That policy used
// to be open-coded at ~150 sites (and independently, with a NaN guard, in
// babylon); this is the one place it lives now.
//
// arg_num's NaN guard is the important one: a script passing a
// non-numeric value makes to_number() return NaN, and casting NaN to an
// integer is undefined behavior. Defaulting instead keeps the natives
// total over every input a script can hand them.
[[nodiscard]] inline std::string arg_str(const std::vector<ctjs::value> & a, std::size_t i) {
	return i < a.size() ? a[i].to_string() : std::string{};
}
[[nodiscard]] inline double arg_num(const std::vector<ctjs::value> & a, std::size_t i, double dflt = 0.0) {
	if (i >= a.size()) { return dflt; }
	const double d = a[i].to_number();
	return std::isnan(d) ? dflt : d;
}
[[nodiscard]] inline std::int32_t arg_i32(const std::vector<ctjs::value> & a, std::size_t i,
                            std::int32_t dflt = 0) {
	return static_cast<std::int32_t>(arg_num(a, i, static_cast<double>(dflt)));
}
[[nodiscard]] inline bool arg_bool(const std::vector<ctjs::value> & a, std::size_t i) {
	return i < a.size() && a[i].truthy();
}
[[nodiscard]] inline ctjs::rc<ctjs::object_t> arg_obj(const std::vector<ctjs::value> & a, std::size_t i) {
	return (i < a.size() && a[i].is_object()) ? a[i].as_object() : ctjs::rc<ctjs::object_t>{};
}
[[nodiscard]] inline bool arg_fn(const std::vector<ctjs::value> & a, std::size_t i) {
	return i < a.size() && a[i].is_function();
}

// a named property read off a JS object, with a default
[[nodiscard]] inline double num_prop(const ctjs::rc<ctjs::object_t> & o, const char * k, double dflt) {
	if (!o) { return dflt; }
	const ctjs::value * v = o->find(k);
	return (v != nullptr && v->is_number()) ? v->as_number() : dflt;
}
[[nodiscard]] inline ctjs::rc<ctjs::object_t> child_obj(const ctjs::rc<ctjs::object_t> & o, const char * k) {
	if (!o) { return {}; }
	const ctjs::value * v = o->find(k);
	return (v != nullptr && v->is_object()) ? v->as_object() : ctjs::rc<ctjs::object_t>{};
}
// `this` inside a native, when it is an object
[[nodiscard]] inline ctjs::rc<ctjs::object_t> self_of(ctjs::context & cx) {
	return cx.current_this.is_object() ? cx.current_this.as_object() : ctjs::rc<ctjs::object_t>{};
}

// --- installing a native method --------------------------------------
// ctjs::value::function takes the method's name for stack traces, and the
// object wants it as a key, so every binding site used to spell the same
// string twice. Spelling it once means the trace can never drift from the
// property a script actually called.
template <typename F> void set_method(ctjs::object_t & o, const char * name, F && fn) {
	o.set(name, ctjs::value::function(std::forward<F>(fn), name));
}
template <typename F>
void set_method(const ctjs::rc<ctjs::object_t> & o, const char * name, F && fn) {
	o->set(name, ctjs::value::function(std::forward<F>(fn), name));
}
} // namespace detail

// script-registered callbacks (addEventListener / requestAnimationFrame)
// plus the interpreter context needed to call them back; the context is
// captured at registration time and lives in the engine's run_result
struct dom_events {
	ctjs::context * cx = nullptr;
	document * doc = nullptr; // set by make_document; createElement et al
	std::map<std::string, std::vector<ctjs::value>, std::less<>> listeners;
	std::vector<ctjs::value> raf;
	double raf_id = 0;
	// setTimeout/setInterval: fired by the engine's tick() against the
	// same now_ms clock performance.now exposes
	struct timer_entry {
		double id;
		double due_ms;
		double interval_ms;
		bool repeat;
		ctjs::value fn;
	};
	std::vector<timer_entry> timers;
	double timer_seq = 0;
	std::vector<std::string> alerts;
	bool reload = false;
	double now_ms = 0;
	// audio backend the SDL shell installs (empty in headless/engine-only
	// builds): play_audio(url, loop) -> handle, stop_audio(handle), volume.
	// BABYLON's Sound routes here (babylon.hpp), the shell wires it to the mixer.
	std::function<std::int32_t(const std::string &, bool)> play_audio;
	std::function<void(std::int32_t)> stop_audio;
	std::function<void(float)> set_audio_volume;
	// every element handle carries "__node", an index here - how one
	// handle's native (appendChild) resolves ANOTHER handle's node
	std::vector<node *> handle_nodes;

	double track_node(node * n) {
		handle_nodes.push_back(n);
		return static_cast<double>(handle_nodes.size() - 1);
	}
	node * node_of(const ctjs::value & handle) {
		if (!handle.is_object()) { return nullptr; }
		const ctjs::value * id = handle.as_object()->find("__node");
		if (id == nullptr || !id->is_number()) { return nullptr; }
		const std::size_t i = static_cast<std::size_t>(id->as_number());
		return i < handle_nodes.size() ? handle_nodes[i] : nullptr;
	}
	// element handles whose layout-derived properties refresh per frame
	std::vector<std::pair<ctjs::rc<ctjs::object_t>, node *>> tracked;
	// <select> onchange handlers, keyed by the select node (the engine fires them
	// when the user picks an option); stale node pointers are cleared on reload
	std::map<node *, ctjs::value> change_handlers;
	// per-element "click" listeners (addEventListener('click', ...)); the engine
	// fires the clicked element's and its ancestors' on a click. Node-keyed, so
	// cleared on reload with the rest.
	std::map<node *, std::vector<ctjs::value>> click_listeners;
	// per-element .onclick PROPERTY handlers (el.onclick = fn) - a single handler
	// per node (assignment replaces), distinct from addEventListener. Node-keyed
	// so a click resolves it even though element handles are transient.
	std::map<node *, ctjs::value> onclick_handlers;
	// per-element "change" listeners (addEventListener('change', ...)) - fired
	// with the accessor-style change_handlers by engine::fire_change
	std::map<node *, std::vector<ctjs::value>> change_listeners;
	// per-element "input" listeners (fires on every edit of an editable)
	std::map<node *, std::vector<ctjs::value>> input_listeners;
	// per-form "submit" listeners + .onsubmit property handlers
	std::map<node *, std::vector<ctjs::value>> submit_listeners;
	std::map<node *, ctjs::value> onsubmit_handlers;
	// the engine's submit/reset entry points, reachable from form handles
	// (the ev.play_audio hook precedent - script.hpp cannot see the engine)
	ctc::cfunction<void(node *)> request_submit;
	ctc::cfunction<void(node *)> request_reset;
	// what the last anchor activation recorded (document.location.href/hash)
	std::string location_href;
	std::string location_hash;
	// the window object + the viewport the engine last laid out
	ctjs::rc<ctjs::object_t> window_obj;
	std::int32_t viewport_w = 0;
	std::int32_t viewport_h = 0;

	void reset() {
		cx = nullptr;
		listeners.clear();
		raf.clear();
		timers.clear();
		alerts.clear();
		reload = false;
		tracked.clear();
		change_handlers.clear();
		click_listeners.clear();
		onclick_handlers.clear();
		change_listeners.clear();
		input_listeners.clear();
		submit_listeners.clear();
		onsubmit_handlers.clear();
		location_href.clear();
		location_hash.clear();
	}

	// fire everything due at now_ms. Handlers may add or clear timers
	// mid-flight, so each round re-scans from the start and moves the
	// fired entry out first; the round cap keeps a 0ms-retimer chain
	// from wedging the frame
	void run_timers() {
		for (std::int32_t rounds = 0; rounds < 64; ++rounds) {
			bool fired = false;
			for (std::size_t i = 0; i < timers.size(); ++i) {
				if (timers[i].due_ms > now_ms) { continue; }
				timer_entry t = timers[i];
				if (t.repeat) {
					timers[i].due_ms = t.due_ms + (t.interval_ms > 1.0 ? t.interval_ms : 1.0);
				} else {
					timers.erase(timers.begin() + static_cast<ptrdiff_t>(i));
				}
				invoke(t.fn, {});
				fired = true;
				break;
			}
			if (!fired) { return; }
		}
	}

	void clear_timer(double id) {
		for (std::size_t i = 0; i < timers.size(); ++i) {
			if (timers[i].id == id) {
				timers.erase(timers.begin() + static_cast<ptrdiff_t>(i));
				return;
			}
		}
	}

	void invoke(const ctjs::value & fn, std::vector<ctjs::value> args) {
		if (cx == nullptr || !fn.is_function()) { return; }
		try {
			(void)ctjs::call_value(*cx, fn, std::move(args));
		} catch (const ctjs::js_throw & ex) {
			std::fprintf(stderr, "ctbrowser: uncaught (in listener): %s\n",
			             detail::debug_on() ? detail::error_trace(ex.thrown).c_str()
			                                : ctjs::error_to_string(ex.thrown).c_str());
		}
	}

	void dispatch(std::string_view type, const ctjs::value & event) {
		const auto it = listeners.find(type);
		if (it == listeners.end()) { return; }
		// copy: a handler may add/remove listeners while we iterate
		const std::vector<ctjs::value> fns = it->second;
		for (const ctjs::value & fn : fns) { invoke(fn, {event}); }
	}

	// push the live layout rects back into the exposed handles
	void refresh_tracked() {
		for (auto & [o, n] : tracked) {
			const bool canvas = n->is_canvas();
			o->set("width",
			       ctjs::value{static_cast<double>(canvas ? n->canvas_w : n->w)});
			o->set("height",
			       ctjs::value{static_cast<double>(canvas ? n->canvas_h : n->h)});
			o->set("offsetLeft", ctjs::value{static_cast<double>(n->x)});
			o->set("offsetTop", ctjs::value{static_cast<double>(n->y)});
		}
		if (window_obj) {
			window_obj->set("innerWidth", ctjs::value{viewport_w});
			window_obj->set("innerHeight", ctjs::value{viewport_h});
		}
	}
};

namespace detail {

[[nodiscard]] inline uint32_t css_to_argb(std::string_view spec, uint32_t fallback) {
	const ctcss::color c = ctcss::parse_color(spec);
	if (!c.ok) { return fallback; }
	return (static_cast<uint32_t>(c.a) << 24) | (static_cast<uint32_t>(c.r) << 16) |
	       (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
}

// SDL key names (what the shell feeds engine::key) -> DOM KeyboardEvent.code
inline std::string dom_key_code(std::string_view sdl) {
	if (sdl == "Left") { return "ArrowLeft"; }
	if (sdl == "Right") { return "ArrowRight"; }
	if (sdl == "Up") { return "ArrowUp"; }
	if (sdl == "Down") { return "ArrowDown"; }
	if (sdl == "Space") { return "Space"; }
	if (sdl == "Return") { return "Enter"; }
	if (sdl == "Escape") { return "Escape"; }
	if (sdl == "Backspace") { return "Backspace"; }
	if (sdl == "Tab") { return "Tab"; }
	if (sdl.size() == 1 && sdl[0] >= 'A' && sdl[0] <= 'Z') {
		return std::string{"Key"} + std::string{sdl};
	}
	if (sdl.size() == 1 && sdl[0] >= 'a' && sdl[0] <= 'z') {
		return std::string{"Key"} + static_cast<char>(sdl[0] - 'a' + 'A');
	}
	if (sdl.size() == 1 && sdl[0] >= '0' && sdl[0] <= '9') {
		return std::string{"Digit"} + std::string{sdl};
	}
	return std::string{sdl};
}

// DOM KeyboardEvent.key for the codes tutorial pages compare against
inline std::string dom_key_key(std::string_view code) {
	if (code == "Space") { return " "; }
	if (code.size() == 4 && code.starts_with("Key")) {
		return std::string{static_cast<char>(code[3] - 'A' + 'a')};
	}
	if (code.size() == 6 && code.starts_with("Digit")) {
		return std::string{code.substr(5)};
	}
	return std::string{code};
}

// the methods every DOM Event carries. preventDefault/stopPropagation
// are REAL: they flag the event object itself (read via cx.current_this
// - the receiver of the method call - so no capture cycles), and the
// engine consults the flags after the listeners ran.
inline void add_event_methods(ctjs::object_t & ev) {
	set_method(ev, "preventDefault", [](ctjs::context & cx, const std::vector<ctjs::value> &) {
		if (cx.current_this.is_object()) {
			const ctjs::value * c = cx.current_this.as_object()->find("cancelable");
			if (c == nullptr || c->truthy()) {
				cx.current_this.as_object()->set("defaultPrevented", ctjs::value{true});
			}
		}
		return ctjs::value{};
	});
	set_method(ev, "stopPropagation", [](ctjs::context & cx, const std::vector<ctjs::value> &) {
		if (cx.current_this.is_object()) { cx.current_this.as_object()->set("__stopped", ctjs::value{true}); }
		return ctjs::value{};
	});
	set_method(ev, "stopImmediatePropagation", [](ctjs::context & cx, const std::vector<ctjs::value> &) {
		if (cx.current_this.is_object()) {
			cx.current_this.as_object()->set("__stopped", ctjs::value{true});
			cx.current_this.as_object()->set("__stopped_now", ctjs::value{true});
		}
		return ctjs::value{};
	});
	ev.set("defaultPrevented", ctjs::value{false});
	ev.set("bubbles", ctjs::value{true});
	ev.set("cancelable", ctjs::value{true});
	ev.set("isTrusted", ctjs::value{true});
}

// read a boolean flag a listener may have set on the shared event object
[[nodiscard]] inline bool event_flag(const ctjs::value & evt, std::string_view name) {
	if (!evt.is_object()) { return false; }
	const ctjs::value * f = evt.as_object()->find(name);
	return f != nullptr && f->truthy();
}

inline ctjs::value key_event(std::string_view sdl_name, std::string_view type = "keydown") {
	ctjs::object_t ev;
	const std::string code = dom_key_code(sdl_name);
	ev.set("key", ctjs::value{dom_key_key(code)});
	ev.set("code", ctjs::value{code});
	ev.set("keyCode", ctjs::value{0.0});
	ev.set("repeat", ctjs::value{false});
	ev.set("type", ctjs::value{std::string{type}});
	add_event_methods(ev);
	return ctjs::value::object(std::move(ev));
}

inline ctjs::value mouse_event(double x, double y, std::string_view type = "mousemove") {
	ctjs::object_t ev;
	ev.set("type", ctjs::value{std::string{type}});
	ev.set("clientX", ctjs::value{x});
	ev.set("clientY", ctjs::value{y});
	ev.set("pageX", ctjs::value{x});
	ev.set("pageY", ctjs::value{y});
	ev.set("button", ctjs::value{0.0});
	ev.set("buttons", ctjs::value{0.0});
	add_event_methods(ev);
	return ctjs::value::object(std::move(ev));
}

// a wheel event: mouse fields + the DOM deltas (deltaY > 0 = down)
inline ctjs::value wheel_event(double x, double y, double delta_y) {
	ctjs::value ev = mouse_event(x, y, "wheel");
	ev.as_object()->set("deltaX", ctjs::value{0.0});
	ev.as_object()->set("deltaY", ctjs::value{delta_y});
	ev.as_object()->set("deltaMode", ctjs::value{0.0}); // pixels
	return ev;
}

// a bare event (change, ...): just a type + the standard methods
inline ctjs::value simple_event(std::string_view type) {
	ctjs::object_t ev;
	ev.set("type", ctjs::value{std::string{type}});
	add_event_methods(ev);
	return ctjs::value::object(std::move(ev));
}

inline ctjs::value canvas_context(node * n, image_store * images);
// the installers below build one element handle facet at a time, and the
// query facet hands back handles of its own - so it needs this first
inline ctjs::value element_handle(node * n, image_store * images, dom_events * ev);

// (defined below, after element_handle; used by getElementsByTagName/ClassName)
inline void collect_by_tag(node * n, std::string_view tag, image_store * images, dom_events * ev,
                           std::vector<ctjs::value> & out);
inline void collect_by_class(node * n, std::string_view cls, image_store * images, dom_events * ev,
                             std::vector<ctjs::value> & out);

// the live web-facing properties (refreshed after every layout)
inline void install_core_props(ctjs::object_t & o, node * n, dom_events * ev) {
	o.set("id", ctjs::value{n->id});
	o.set("tag", ctjs::value{n->tag});
	// the live web-facing properties (refreshed after every layout)
	o.set("width", ctjs::value{static_cast<double>(n->is_canvas() ? n->canvas_w : n->w)});
	o.set("height", ctjs::value{static_cast<double>(n->is_canvas() ? n->canvas_h : n->h)});
	o.set("offsetLeft", ctjs::value{static_cast<double>(n->x)});
	o.set("offsetTop", ctjs::value{static_cast<double>(n->y)});
	o.set("__node", ctjs::value{ev->track_node(n)});
}

// getContext, and the node-tree mutators scripts may call
inline void install_tree_api(ctjs::object_t & o, node * n, image_store * images, dom_events * ev) {
	set_method(o, "getContext", [n, images, ev](ctjs::context & cx, const std::vector<ctjs::value> &) {
		ev->cx = &cx;
		return n->is_canvas() ? canvas_context(n, images)
		: ctjs::value{};
	});
	set_method(o, "appendChild", [n, ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
		ev->cx = &cx;
		if (!a.empty() && ev->doc != nullptr) {
			if (node * child = ev->node_of(a[0])) {
				ev->doc->append_child(n, child);
			}
		}
		return a.empty() ? ctjs::value{} : a[0]; // returns the child
	});
	set_method(o, "removeChild", [n, ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
		ev->cx = &cx;
		if (!a.empty() && ev->doc != nullptr) {
			if (node * child = ev->node_of(a[0])) {
				ev->doc->remove_child(n, child);
			}
		}
		return a.empty() ? ctjs::value{} : a[0];
	});
	set_method(o, "setAttribute", [n](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (a.size() >= 2) {
			const std::string key = a[0].to_string();
			const std::string val = a[1].to_string();
			bool found = false;
			for (auto & [k, v] : n->attributes) {
				if (k == key) {
					v = val;
					found = true;
				}
			}
			if (!found) { n->attributes.emplace_back(key, val); }
			if (key == "id") { n->id = val; }
			if (key == "class") { n->classes = val; }
			if (n->is_canvas() && (key == "width" || key == "height")) {
				const std::int32_t d = parse_int_attr(val, 0);
				if (key == "width") { n->canvas_w = d; }
				else { n->canvas_h = d; }
				n->pixels.assign(static_cast<std::size_t>(n->canvas_w) *
				static_cast<std::size_t>(n->canvas_h),
				0x00000000u);
			}
		}
		return ctjs::value{};
	});
	set_method(o, "addEventListener", [n, ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
		ev->cx = &cx;
		if (a.size() >= 2 && a[1].is_function()) {
			const std::string type = a[0].to_string();
			// element "click"/"change" are targeted (fired on the hit
			// element + its ancestors / the changed control); other
			// types stay in the shared, globally-dispatched registry
			// (keydown/resize/mouseover/...)
			if (type == "click") {
				ev->click_listeners[n].push_back(a[1]);
			} else if (type == "change") {
				ev->change_listeners[n].push_back(a[1]);
			} else if (type == "input") {
				ev->input_listeners[n].push_back(a[1]);
			} else if (type == "submit") {
				ev->submit_listeners[n].push_back(a[1]);
			} else {
				ev->listeners[type].push_back(a[1]);
			}
		}
		return ctjs::value{};
	});
}

// text, the class helpers, and the DOM classList object
inline void install_class_api(ctjs::object_t & o, node * n) {
	set_method(o, "text", [n](ctjs::context &, const std::vector<ctjs::value> &) {
		return ctjs::value{n->text};
	});
	set_method(o, "setText", [n](ctjs::context &, const std::vector<ctjs::value> & a) {
		n->text = arg_str(a, 0);
		return ctjs::value{};
	});
	set_method(o, "addClass", [n](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (!a.empty()) { n->add_class(a[0].to_string()); }
		return ctjs::value{};
	});
	set_method(o, "removeClass", [n](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (!a.empty()) { n->remove_class(a[0].to_string()); }
		return ctjs::value{};
	});
	set_method(o, "toggleClass", [n](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (!a.empty()) { n->toggle_class(a[0].to_string()); }
		return ctjs::value{};
	});
	set_method(o, "hasClass", [n](ctjs::context &, const std::vector<ctjs::value> & a) {
		return ctjs::value{!a.empty() && n->has_class(a[0].to_string())};
	});
	// the DOM classList API (add/remove/contains/toggle over node classes)
	{
		ctjs::object_t cl;
		set_method(cl, "add", [n](ctjs::context &, const std::vector<ctjs::value> & a) {
			for (const auto & v : a) { n->add_class(v.to_string()); }
			return ctjs::value{};
		});
		set_method(cl, "remove", [n](ctjs::context &, const std::vector<ctjs::value> & a) {
			for (const auto & v : a) { n->remove_class(v.to_string()); }
			return ctjs::value{};
		});
		set_method(cl, "contains", [n](ctjs::context &, const std::vector<ctjs::value> & a) {
			return ctjs::value{!a.empty() && n->has_class(a[0].to_string())};
		});
		set_method(cl, "toggle", [n](ctjs::context &, const std::vector<ctjs::value> & a) {
			if (!a.empty()) { n->toggle_class(a[0].to_string()); }
			return ctjs::value{!a.empty() && n->has_class(a[0].to_string())};
		});
		o.set("classList", ctjs::value::object(std::move(cl)));
	}
}

// querySelector(All) and the getElementsBy* collections
inline void install_query_api(ctjs::object_t & o, node * n, image_store * images, dom_events * ev) {
	// querySelector(All): a CSS-subset search rooted at this element
	set_method(o, "querySelector", [n, images, ev](ctjs::context &, const std::vector<ctjs::value> & a) -> ctjs::value {
		if (a.empty()) { return ctjs::value{}; }
		node * hit = n->query_selector(a[0].to_string());
		return hit ? element_handle(hit, images, ev) : ctjs::value{};
	});
	set_method(o, "getElementsByTagName", [n, images, ev](ctjs::context &, const std::vector<ctjs::value> & a) -> ctjs::value {
		std::vector<ctjs::value> out;
		if (!a.empty()) {
			const std::string tag = a[0].to_string();
			for (const auto & c : n->children) { collect_by_tag(c.get(), tag, images, ev, out); }
		}
		return ctjs::value::array(std::move(out));
	});
	set_method(o, "getElementsByClassName", [n, images, ev](ctjs::context &, const std::vector<ctjs::value> & a) -> ctjs::value {
		std::vector<ctjs::value> out;
		if (!a.empty()) {
			const std::string cls = a[0].to_string();
			for (const auto & c : n->children) { collect_by_class(c.get(), cls, images, ev, out); }
		}
		return ctjs::value::array(std::move(out));
	});
	// append(...nodes|strings): strings join the element's text (enough for
	// the classic loading-dots idiom); nodes append as children
	set_method(o, "append", [n, ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
		ev->cx = &cx;
		for (const auto & v : a) {
			if (v.is_object() && ev->node_of(v) != nullptr && ev->doc != nullptr) {
				ev->doc->append_child(n, ev->node_of(v));
			} else {
				n->text += v.to_string();
			}
		}
		return ctjs::value{};
	});
}

// inline style, attributes by name, and the live layout rect
inline void install_style_api(ctjs::object_t & o, node * n) {
	set_method(o, "style", [n](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (a.size() >= 2) {
			n->inline_style.set(a[0].to_string(), a[1].to_string());
		}
		return ctjs::value{};
	});
	set_method(o, "attr", [n](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (a.empty()) { return ctjs::value{}; }
		return ctjs::value{std::string{n->attribute(a[0].to_string())}};
	});
	// the element's live layout rect (viewport coordinates) - lets a
	// script convert mouse positions into canvas-local ones
	set_method(o, "rect", [n](ctjs::context &, const std::vector<ctjs::value> &) {
		ctjs::object_t r;
		r.set("x", ctjs::value{static_cast<double>(n->x)});
		r.set("y", ctjs::value{static_cast<double>(n->y)});
		r.set("w", ctjs::value{static_cast<double>(n->w)});
		r.set("h", ctjs::value{static_cast<double>(n->h)});
		return ctjs::value::object(std::move(r));
	});
}

// the .onclick PROPERTY (el.onclick = fn), stored on the node
inline void install_event_props(ctjs::object_t & o, dom_events * ev) {
	// .onclick PROPERTY (el.onclick = fn), like the DOM: stored on the node
	// registry (handles are transient), fired by engine::click_at on this element
	// and — via bubbling — its descendants' clicks. The game's "PLAY AGAIN" panel
	// uses this idiom rather than addEventListener.
	ctjs::attach_accessor(o, "onclick", 's', ctjs::value::function(
	    [ev](ctjs::context & cx, const std::vector<ctjs::value> & a) -> ctjs::value {
		    node * en = ev->node_of(cx.current_this);
		    if (en != nullptr && !a.empty()) {
			    if (a[0].is_function()) { ev->onclick_handlers[en] = a[0]; }
			    else { ev->onclick_handlers.erase(en); }
		    }
		    return ctjs::value{};
	    }, "set onclick"));
	ctjs::attach_accessor(o, "onclick", 'g', ctjs::value::function(
	    [ev](ctjs::context & cx, const std::vector<ctjs::value> &) -> ctjs::value {
		    node * en = ev->node_of(cx.current_this);
		    const auto it = en != nullptr ? ev->onclick_handlers.find(en) : ev->onclick_handlers.end();
		    return it != ev->onclick_handlers.end() ? it->second : ctjs::value{};
	    }, "get onclick"));
}

// the standard attribute API (all elements)
inline void install_attribute_api(ctjs::object_t & o, node * n, dom_events * ev) {
	set_method(o, "getAttribute", [n](ctjs::context &, const std::vector<ctjs::value> & a) -> ctjs::value {
		if (a.empty()) { return ctjs::value::null(); }
		const std::string name = a[0].to_string();
		if (!n->has_attribute(name)) { return ctjs::value::null(); }
		return ctjs::value{std::string{n->attribute(name)}};
	});
	set_method(o, "hasAttribute", [n](ctjs::context &, const std::vector<ctjs::value> & a) {
		return ctjs::value{!a.empty() && n->has_attribute(a[0].to_string())};
	});
	// .onchange works on any element (checkbox/radio/select fire it)
	ctjs::attach_accessor(o, "onchange", 's', ctjs::value::function(
	    [ev](ctjs::context & cx, const std::vector<ctjs::value> & a) -> ctjs::value {
		    node * en = ev->node_of(cx.current_this);
		    if (en != nullptr && !a.empty() && a[0].is_function()) { ev->change_handlers[en] = a[0]; }
		    return ctjs::value{};
	    }, "set onchange"));
}

// editable controls: the live value + caret-preserving setter
inline void install_value_api(ctjs::object_t & o, node * n, dom_events * ev) {
	if (n->is_editable()) {
		ctjs::attach_accessor(o, "value", 'g', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> &) -> ctjs::value {
			    node * en = ev->node_of(cx.current_this);
			    return ctjs::value{en != nullptr ? en->value : std::string{}};
		    }, "get value"));
		ctjs::attach_accessor(o, "value", 's', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> & a) -> ctjs::value {
			    node * en = ev->node_of(cx.current_this);
			    if (en != nullptr && !a.empty()) {
				    // programmatic set: no input/change events (browser-correct)
				    en->value = a[0].to_string();
				    en->caret = static_cast<std::int32_t>(en->value.size());
			    }
			    return ctjs::value{};
		    }, "set value"));
		ctjs::attach_accessor(o, "oninput", 's', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> & a) -> ctjs::value {
			    node * en = ev->node_of(cx.current_this);
			    if (en != nullptr && !a.empty() && a[0].is_function()) { ev->input_listeners[en].push_back(a[0]); }
			    return ctjs::value{};
		    }, "set oninput"));
	}
}

// forms: submit()/reset() + .onsubmit
inline void install_form_api(ctjs::object_t & o, node * n, dom_events * ev) {
	if (n->tag == "form") {
		set_method(o, "submit", [ev](ctjs::context & cx, const std::vector<ctjs::value> &) {
			node * en = ev->node_of(cx.current_this);
			if (en != nullptr && ev->request_submit) { ev->request_submit(en); }
			return ctjs::value{};
		});
		set_method(o, "reset", [ev](ctjs::context & cx, const std::vector<ctjs::value> &) {
			node * en = ev->node_of(cx.current_this);
			if (en != nullptr && ev->request_reset) { ev->request_reset(en); }
			return ctjs::value{};
		});
		ctjs::attach_accessor(o, "onsubmit", 's', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> & a) -> ctjs::value {
			    node * en = ev->node_of(cx.current_this);
			    if (en != nullptr && !a.empty() && a[0].is_function()) { ev->onsubmit_handlers[en] = a[0]; }
			    return ctjs::value{};
		    }, "set onsubmit"));
	}
}

// checkbox / radio
inline void install_toggle_api(ctjs::object_t & o, node * n, dom_events * ev) {
	if (n->is_checkbox() || n->is_radio()) {
		ctjs::attach_accessor(o, "checked", 'g', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> &) -> ctjs::value {
			    node * en = ev->node_of(cx.current_this);
			    return ctjs::value{en != nullptr && en->checked};
		    }, "get checked"));
		ctjs::attach_accessor(o, "checked", 's', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> & a) -> ctjs::value {
			    node * en = ev->node_of(cx.current_this);
			    if (en != nullptr && !a.empty()) {
				    // programmatic set fires no change event (browser-correct);
				    // checking a radio keeps the group invariant
				    if (a[0].truthy() && en->is_radio()) {
					    detail::check_radio(ev->doc != nullptr ? ev->doc->root.get() : nullptr, *en);
				    } else {
					    en->checked = a[0].truthy();
				    }
			    }
			    return ctjs::value{};
		    }, "set checked"));
	}
	if (n->is_input()) {
		ctjs::attach_accessor(o, "type", 'g', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> &) -> ctjs::value {
			    node * en = ev->node_of(cx.current_this);
			    return ctjs::value{en != nullptr ? std::string{en->input_type()} : std::string{}};
		    }, "get type"));
	}
}

// disabled, <details> disclosure, and anchors
inline void install_state_api(ctjs::object_t & o, node * n, dom_events * ev) {
	if (n->is_form_control()) {
		ctjs::attach_accessor(o, "disabled", 'g', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> &) -> ctjs::value {
			    node * en = ev->node_of(cx.current_this);
			    return ctjs::value{en != nullptr && en->is_disabled()};
		    }, "get disabled"));
		ctjs::attach_accessor(o, "disabled", 's', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> & a) -> ctjs::value {
			    node * en = ev->node_of(cx.current_this);
			    if (en != nullptr && !a.empty()) {
				    // attribute-backed so :disabled styling sees it
				    const bool want = a[0].truthy();
				    if (want && !en->has_attribute("disabled")) {
					    en->attributes.emplace_back("disabled", "");
				    } else if (!want) {
					    std::erase_if(en->attributes, [](const auto & kv) { return kv.first == "disabled"; });
				    }
			    }
			    return ctjs::value{};
		    }, "set disabled"));
	}
	// --- <details> disclosure ---
	if (n->is_details()) {
		ctjs::attach_accessor(o, "open", 'g', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> &) -> ctjs::value {
			    node * en = ev->node_of(cx.current_this);
			    return ctjs::value{en != nullptr && en->open};
		    }, "get open"));
		ctjs::attach_accessor(o, "open", 's', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> & a) -> ctjs::value {
			    node * en = ev->node_of(cx.current_this);
			    if (en != nullptr && !a.empty()) { en->open = a[0].truthy(); }
			    return ctjs::value{};
		    }, "set open"));
	}
	// --- anchors ---
	if (n->tag == "a") {
		ctjs::attach_accessor(o, "href", 'g', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> &) -> ctjs::value {
			    node * en = ev->node_of(cx.current_this);
			    return ctjs::value{en != nullptr ? std::string{en->attribute("href")} : std::string{}};
		    }, "get href"));
		ctjs::attach_accessor(o, "href", 's', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> & a) -> ctjs::value {
			    node * en = ev->node_of(cx.current_this);
			    if (en != nullptr && !a.empty()) {
				    const std::string v = a[0].to_string();
				    for (auto & [k, val] : en->attributes) {
					    if (k == "href") { val = v; return ctjs::value{}; }
				    }
				    en->attributes.emplace_back("href", v);
			    }
			    return ctjs::value{};
		    }, "set href"));
	}
}

// <select> / <option> form-control properties
inline void install_select_api(ctjs::object_t & o, node * n, dom_events * ev) {
	if (n->is_select()) {
		// .value: the selected <option>'s value attribute (live), or pick by value
		ctjs::attach_accessor(o, "value", 'g', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> &) -> ctjs::value {
			    node * sn = ev->node_of(cx.current_this);
			    node * opt = sn != nullptr ? sn->nth_option(sn->selected_option()) : nullptr;
			    return ctjs::value{opt != nullptr ? std::string{opt->attribute("value")} : std::string{}};
		    }, "get value"));
		ctjs::attach_accessor(o, "value", 's', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> & a) -> ctjs::value {
			    node * sn = ev->node_of(cx.current_this);
			    if (sn != nullptr && !a.empty()) {
				    const std::string want = a[0].to_string();
				    for (std::int32_t i = 0; i < sn->option_count(); ++i) {
					    node * opt = sn->nth_option(i);
					    if (opt != nullptr && std::string{opt->attribute("value")} == want) { sn->select_index = i; break; }
				    }
			    }
			    return ctjs::value{};
		    }, "set value"));
		ctjs::attach_accessor(o, "selectedIndex", 'g', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> &) -> ctjs::value {
			    node * sn = ev->node_of(cx.current_this);
			    return ctjs::value{static_cast<double>(sn != nullptr ? sn->selected_option() : 0)};
		    }, "get selectedIndex"));
		ctjs::attach_accessor(o, "selectedIndex", 's', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> & a) -> ctjs::value {
			    node * sn = ev->node_of(cx.current_this);
			    if (sn != nullptr && !a.empty()) { sn->select_index = arg_i32(a, 0); }
			    return ctjs::value{};
		    }, "set selectedIndex"));
	} else if (n->tag == "option") {
		ctjs::attach_accessor(o, "value", 'g', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> &) -> ctjs::value {
			    node * on = ev->node_of(cx.current_this);
			    return ctjs::value{on != nullptr ? std::string{on->attribute("value")} : std::string{}};
		    }, "get value"));
		// .selected = true makes this option the parent <select>'s current choice
		ctjs::attach_accessor(o, "selected", 's', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> & a) -> ctjs::value {
			    node * on = ev->node_of(cx.current_this);
			    if (on != nullptr && on->parent != nullptr && on->parent->is_select() && arg_bool(a, 0)) {
				    node * sn = on->parent;
				    std::int32_t k = 0;
				    for (const auto & c : sn->children) {
					    if (c->tag == "option") {
						    if (c.get() == on) { sn->select_index = k; break; }
						    ++k;
					    }
				    }
			    }
			    return ctjs::value{};
		    }, "set selected"));
		ctjs::attach_accessor(o, "selected", 'g', ctjs::value::function(
		    [ev](ctjs::context & cx, const std::vector<ctjs::value> &) -> ctjs::value {
			    node * on = ev->node_of(cx.current_this);
			    if (on == nullptr || on->parent == nullptr || !on->parent->is_select()) { return ctjs::value{false}; }
			    node * sn = on->parent;
			    std::int32_t k = 0;
			    for (const auto & c : sn->children) {
				    if (c->tag == "option") {
					    if (c.get() == on) { return ctjs::value{k == sn->selected_option()}; }
					    ++k;
				    }
			    }
			    return ctjs::value{false};
		    }, "get selected"));
	}
}

inline ctjs::value element_handle(node * n, image_store * images, dom_events * ev) {
	ctjs::object_t o;
	install_core_props(o, n, ev);
	install_tree_api(o, n, images, ev);
	install_class_api(o, n);
	install_query_api(o, n, images, ev);
	install_style_api(o, n);
	install_event_props(o, ev);
	install_attribute_api(o, n, ev);
	install_value_api(o, n, ev);
	install_form_api(o, n, ev);
	install_toggle_api(o, n, ev);
	install_state_api(o, n, ev);
	install_select_api(o, n, ev);
	ctjs::value v = ctjs::value::object(std::move(o));
	ev->tracked.emplace_back(v.as_object(), n);
	return v;
}

// The raw pixel operations every canvas method draws through: read the
// live fillStyle/strokeStyle back off the JS object (the real canvas
// idiom - a script assigns the property and the natives observe it),
// write one pixel, fill a rect, blit an image. These were five separate
// closures each method had to capture individually; as one value the
// method installers below take a single parameter, and the clipping rules
// live in one place instead of being restated per call site.
struct canvas_ops {
	node * n = nullptr;
	image_store * images = nullptr;
	ctjs::rc<ctjs::object_t> ctx;

	[[nodiscard]] std::uint32_t fill_style() const { return style_prop("fillStyle"); }
	[[nodiscard]] std::uint32_t stroke_style() const { return style_prop("strokeStyle"); }
	[[nodiscard]] std::uint32_t style_prop(const char * key) const {
		if (const ctjs::value * v = ctx->find(key)) { return css_to_argb(v->to_string(), 0xFF000000u); }
		return 0xFF000000u;
	}
	// one pixel, silently clipped to the canvas
	void put(std::int32_t x, std::int32_t y, std::uint32_t argb) const {
		if (x >= 0 && y >= 0 && x < n->canvas_w && y < n->canvas_h) {
			n->pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(n->canvas_w) +
			          static_cast<std::size_t>(x)] = argb;
		}
	}
	void fill(std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h,
	          std::uint32_t argb) const {
		if (n->pixels.empty()) { return; }
		const std::int32_t x0 = x < 0 ? 0 : x;
		const std::int32_t y0 = y < 0 ? 0 : y;
		const std::int32_t x1 = x + w > n->canvas_w ? n->canvas_w : x + w;
		const std::int32_t y1 = y + h > n->canvas_h ? n->canvas_h : y + h;
		for (std::int32_t py = y0; py < y1; ++py) {
			for (std::int32_t px = x0; px < x1; ++px) {
				n->pixels[static_cast<std::size_t>(py) * static_cast<std::size_t>(n->canvas_w) +
				          static_cast<std::size_t>(px)] = argb;
			}
		}
	}
	// nearest-neighbour blit with an alpha test (a == 0 skips the pixel)
	void blit(std::int32_t handle, std::int32_t sx, std::int32_t sy, std::int32_t sw, std::int32_t sh,
	          std::int32_t dx, std::int32_t dy, std::int32_t dw, std::int32_t dh) const {
		const image * im = images->get(handle);
		if (im == nullptr || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) { return; }
		for (std::int32_t py = 0; py < dh; ++py) {
			const std::int32_t src_y = sy + py * sh / dh;
			if (src_y < 0 || src_y >= im->h) { continue; }
			for (std::int32_t px = 0; px < dw; ++px) {
				const std::int32_t src_x = sx + px * sw / dw;
				if (src_x < 0 || src_x >= im->w) { continue; }
				const uint32_t argb = im->pixels[static_cast<std::size_t>(src_y) *
				                                     static_cast<std::size_t>(im->w) +
				                                 static_cast<std::size_t>(src_x)];
				if ((argb >> 24) == 0) { continue; }
				put(dx + px, dy + py, argb);
			}
		}
	}
};

inline ctjs::value canvas_context(node * n, image_store * images) {
	auto ctx = ctjs::rc<ctjs::object_t>::make();
	ctx->set("fillStyle", ctjs::value{"#000000"});
	ctx->set("strokeStyle", ctjs::value{"#000000"});
	ctx->set("font", ctjs::value{"10px sans-serif"});
	ctx->set("width", ctjs::value{n->canvas_w});
	ctx->set("height", ctjs::value{n->canvas_h});
	const canvas_ops ops{n, images, ctx};
	set_method(ctx, "fillRect", [ops](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (a.size() >= 4) {
			ops.fill(arg_i32(a, 0),
			arg_i32(a, 1),
			arg_i32(a, 2),
			arg_i32(a, 3), ops.fill_style());
		}
		return ctjs::value{};
	});
	set_method(ctx, "clear", [n, ops](ctjs::context &, const std::vector<ctjs::value> &) {
		for (uint32_t & p : n->pixels) { p = ops.fill_style(); }
		return ctjs::value{};
	});
	set_method(ctx, "putPixel", [ops](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (a.size() >= 2) {
			ops.put(arg_i32(a, 0),
			arg_i32(a, 1), ops.fill_style());
		}
		return ctjs::value{};
	});
	// clearRect clears to TRANSPARENT (the page shows through), per spec
	set_method(ctx, "clearRect", [ops](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (a.size() >= 4) {
			ops.fill(arg_i32(a, 0),
			arg_i32(a, 1),
			arg_i32(a, 2),
			arg_i32(a, 3), 0x00000000u);
		}
		return ctjs::value{};
	});
	set_method(ctx, "strokeRect", [ops](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (a.size() >= 4) {
			const std::int32_t x = arg_i32(a, 0);
			const std::int32_t y = arg_i32(a, 1);
			const std::int32_t w = arg_i32(a, 2);
			const std::int32_t h = arg_i32(a, 3);
			const uint32_t c = ops.stroke_style();
			ops.fill(x, y, w, 1, c);
			ops.fill(x, y + h - 1, w, 1, c);
			ops.fill(x, y, 1, h, c);
			ops.fill(x + w - 1, y, 1, h, c);
		}
		return ctjs::value{};
	});
	// --- the 2D path API with a REAL transform stack. Per spec, path
	// verbs transform their points by the CURRENT matrix as they are
	// appended (so rasterization is matrix-free): subpaths are device-
	// space polylines, ops.fill() scanline-fills them (even-odd), stroke()
	// draws lineWidth-thick segments. arc() honors its angles by
	// sampling; rect() appends a closed 4-point subpath.
	struct ctx2d {
		double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0; // CTM [a c e; b d f]
		std::vector<std::array<double, 6>> stack;
		struct sub {
			std::vector<std::pair<double, double>> pts;
			bool closed = false;
		};
		std::vector<sub> subs;

		std::pair<double, double> tx(double x, double y) const {
			return {a * x + c * y + e, b * x + d * y + f};
		}
		void mul(double a2, double b2, double c2, double d2, double e2, double f2) {
			const double na = a * a2 + c * b2;
			const double nb = b * a2 + d * b2;
			const double nc = a * c2 + c * d2;
			const double nd = b * c2 + d * d2;
			const double ne = a * e2 + c * f2 + e;
			const double nf = b * e2 + d * f2 + f;
			a = na; b = nb; c = nc; d = nd; e = ne; f = nf;
		}
		sub & cur() {
			if (subs.empty()) { subs.emplace_back(); }
			return subs.back();
		}
	};
	auto st = std::make_shared<ctx2d>();
	// even-odd scanline fill of every subpath (closing them implicitly)
	const auto fill_subs = [st, ops](uint32_t col) {
		double miny = 1e300, maxy = -1e300;
		for (const auto & s : st->subs) {
			for (const auto & [px, py] : s.pts) {
				miny = std::min(miny, py);
				maxy = std::max(maxy, py);
			}
		}
		if (miny > maxy) { return; }
		for (std::int32_t y = static_cast<std::int32_t>(std::floor(miny)); y <= static_cast<std::int32_t>(std::ceil(maxy));
		     ++y) {
			const double sy = y + 0.5;
			std::vector<double> xs;
			for (const auto & s : st->subs) {
				const std::size_t n = s.pts.size();
				if (n < 2) { continue; }
				for (std::size_t i = 0; i < n; ++i) {
					const auto [x1, y1] = s.pts[i];
					const auto [x2, y2] = s.pts[(i + 1) % n];
					if ((y1 <= sy) == (y2 <= sy)) { continue; }
					xs.push_back(x1 + (sy - y1) / (y2 - y1) * (x2 - x1));
				}
			}
			std::sort(xs.begin(), xs.end());
			for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {
				for (std::int32_t x = static_cast<std::int32_t>(std::ceil(xs[i] - 0.5));
				     x < static_cast<std::int32_t>(std::ceil(xs[i + 1] - 0.5)) + 1; ++x) {
					ops.put(x, y, col);
				}
			}
		}
	};
	// a lineWidth-thick segment as a filled quad
	const auto thick_seg = [ops](double x1, double y1, double x2, double y2, double w,
	                             uint32_t col) {
		const double dx = x2 - x1;
		const double dy = y2 - y1;
		const double len = std::hypot(dx, dy);
		if (len == 0) { return; }
		const double nx = -dy / len * (w / 2);
		const double ny = dx / len * (w / 2);
		const std::int32_t minx = static_cast<std::int32_t>(std::floor(std::min({x1 - std::fabs(nx), x2 - std::fabs(nx)})));
		const std::int32_t maxx = static_cast<std::int32_t>(std::ceil(std::max({x1 + std::fabs(nx), x2 + std::fabs(nx)})));
		const std::int32_t miny = static_cast<std::int32_t>(std::floor(std::min({y1 - std::fabs(ny) - w, y2 - std::fabs(ny) - w})));
		const std::int32_t maxy = static_cast<std::int32_t>(std::ceil(std::max({y1 + std::fabs(ny) + w, y2 + std::fabs(ny) + w})));
		for (std::int32_t y = miny; y <= maxy; ++y) {
			for (std::int32_t x = minx; x <= maxx; ++x) {
				// distance from pixel center to the segment
				const double px = x + 0.5 - x1;
				const double py = y + 0.5 - y1;
				const double t = std::clamp((px * dx + py * dy) / (len * len), 0.0, 1.0);
				const double ddx = px - t * dx;
				const double ddy = py - t * dy;
				if (ddx * ddx + ddy * ddy <= (w / 2) * (w / 2)) { ops.put(x, y, col); }
			}
		}
	};
	ctx->set("lineWidth", ctjs::value{1.0});
	ctx->set("globalAlpha", ctjs::value{1.0});
	const auto num_prop = [ctx](const char * name, double fallback) {
		const ctjs::value * v = ctx->find(name);
		return v != nullptr && v->is_number() ? v->as_number() : fallback;
	};
	set_method(ctx, "save", [st](ctjs::context &, const std::vector<ctjs::value> &) {
		st->stack.push_back({st->a, st->b, st->c, st->d, st->e, st->f});
		return ctjs::value{};
	});
	set_method(ctx, "restore", [st](ctjs::context &, const std::vector<ctjs::value> &) {
		if (!st->stack.empty()) {
			const auto m = st->stack.back();
			st->stack.pop_back();
			st->a = m[0]; st->b = m[1]; st->c = m[2];
			st->d = m[3]; st->e = m[4]; st->f = m[5];
		}
		return ctjs::value{};
	});
	set_method(ctx, "translate", [st](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (a.size() >= 2) {
			st->mul(1, 0, 0, 1, a[0].to_number(), a[1].to_number());
		}
		return ctjs::value{};
	});
	set_method(ctx, "rotate", [st](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (!a.empty()) {
			const double t = a[0].to_number();
			st->mul(std::cos(t), std::sin(t), -std::sin(t),
			std::cos(t), 0, 0);
		}
		return ctjs::value{};
	});
	set_method(ctx, "scale", [st](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (a.size() >= 2) {
			st->mul(a[0].to_number(), 0, 0, a[1].to_number(), 0, 0);
		}
		return ctjs::value{};
	});
	set_method(ctx, "resetTransform", [st](ctjs::context &, const std::vector<ctjs::value> &) {
		st->a = st->d = 1;
		st->b = st->c = st->e = st->f = 0;
		return ctjs::value{};
	});
	set_method(ctx, "beginPath", [st](ctjs::context &, const std::vector<ctjs::value> &) {
		st->subs.clear();
		return ctjs::value{};
	});
	set_method(ctx, "closePath", [st](ctjs::context &, const std::vector<ctjs::value> &) {
		if (!st->subs.empty()) { st->subs.back().closed = true; }
		return ctjs::value{};
	});
	set_method(ctx, "moveTo", [st](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (a.size() >= 2) {
			st->subs.emplace_back();
			st->subs.back().pts.push_back(
			st->tx(a[0].to_number(), a[1].to_number()));
		}
		return ctjs::value{};
	});
	set_method(ctx, "lineTo", [st](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (a.size() >= 2) {
			st->cur().pts.push_back(
			st->tx(a[0].to_number(), a[1].to_number()));
		}
		return ctjs::value{};
	});
	set_method(ctx, "rect", [st](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (a.size() >= 4) {
			const double x = a[0].to_number();
			const double y = a[1].to_number();
			const double w = a[2].to_number();
			const double h = a[3].to_number();
			ctx2d::sub s;
			s.pts = {st->tx(x, y), st->tx(x + w, y),
			st->tx(x + w, y + h), st->tx(x, y + h)};
			s.closed = true;
			st->subs.push_back(std::move(s));
		}
		return ctjs::value{};
	});
	set_method(ctx, "arc", [st](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (a.size() >= 5) {
			const double cx = a[0].to_number();
			const double cy = a[1].to_number();
			const double r = a[2].to_number();
			double t0 = a[3].to_number();
			double t1 = a[4].to_number();
			const bool ccw = a.size() >= 6 && a[5].truthy();
			if (!ccw && t1 < t0) { t1 += 6.283185307179586; }
			if (ccw && t0 < t1) { t0 += 6.283185307179586; }
			const std::int32_t steps =
			std::max(8, static_cast<std::int32_t>(std::fabs(t1 - t0) * r / 2));
			auto & s = st->cur();
			for (std::int32_t i = 0; i <= steps; ++i) {
				const double t = t0 + (t1 - t0) * i / steps;
				s.pts.push_back(st->tx(cx + r * std::cos(t),
				cy + r * std::sin(t)));
			}
		}
		return ctjs::value{};
	});
	set_method(ctx, "fill", [st, fill_subs, ops](ctjs::context &,
	const std::vector<ctjs::value> &) {
		fill_subs(ops.fill_style());
		return ctjs::value{};
	});
	set_method(ctx, "stroke", [st, thick_seg, ops, num_prop](ctjs::context &,
	const std::vector<ctjs::value> &) {
		const uint32_t col = ops.stroke_style();
		const double w = num_prop("lineWidth", 1.0);
		for (const auto & s : st->subs) {
			const std::size_t n = s.pts.size();
			for (std::size_t i = 0; i + 1 < n; ++i) {
				thick_seg(s.pts[i].first, s.pts[i].second,
				s.pts[i + 1].first, s.pts[i + 1].second, w, col);
			}
			if (s.closed && n > 2) {
				thick_seg(s.pts[n - 1].first, s.pts[n - 1].second,
				s.pts[0].first, s.pts[0].second, w, col);
			}
		}
		return ctjs::value{};
	});
	set_method(ctx, "measureText", [ctx](ctjs::context &, const std::vector<ctjs::value> & a) {
		const std::string text = arg_str(a, 0);
		std::int32_t px = 10;
		if (const ctjs::value * fv = ctx->find("font")) {
			const std::string spec = fv->to_string();
			std::int32_t v = 0;
			for (std::size_t i = 0; i < spec.size() && spec[i] >= '0' && spec[i] <= '9';
			++i) {
				v = v * 10 + (spec[i] - '0');
			}
			if (v > 0) { px = v; }
		}
		const std::int32_t scale = px >= 8 ? px / 8 : 1;
		ctjs::object_t m;
		m.set("width", ctjs::value{static_cast<double>(text.size()) * 8.0 *
		static_cast<double>(scale)});
		return ctjs::value::object(std::move(m));
	});
	// a filled circle - not in the 2D spec, but games want one (documented
	// extension, like drawImageRegion below)
	set_method(ctx, "fillCircle", [ops](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (a.size() >= 3) {
			const std::int32_t cx = arg_i32(a, 0);
			const std::int32_t cy = arg_i32(a, 1);
			const std::int32_t r = arg_i32(a, 2);
			const uint32_t c = ops.fill_style();
			for (std::int32_t y = -r; y <= r; ++y) {
				for (std::int32_t x = -r; x <= r; ++x) {
					if (x * x + y * y <= r * r) { ops.put(cx + x, cy + y, c); }
				}
			}
		}
		return ctjs::value{};
	});
	// DOM fillText: y is the BASELINE, size comes from ctx.font
	// ("16px Arial" -> 16, default 10px), glyphs are the embedded 8x8
	// font scaled by the integer factor px/8 (min 1)
	set_method(ctx, "fillText", [ctx, ops](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (a.size() >= 3) {
			const std::string text = a[0].to_string();
			std::int32_t px = 0;
			if (const ctjs::value * f = ctx->find("font")) {
				const std::string spec = f->to_string();
				for (std::size_t i = 0;
				i < spec.size() && spec[i] >= '0' && spec[i] <= '9';
				++i) {
					px = px * 10 + (spec[i] - '0');
				}
			}
			const std::int32_t scale = px >= 8 ? px / 8 : 1;
			const std::int32_t x = arg_i32(a, 1);
			const std::int32_t y = arg_i32(a, 2) - 8 * scale;
			const uint32_t c = ops.fill_style();
			std::int32_t pen = x;
			for (std::size_t ci = 0; ci < text.size();) { // decode UTF-8
				const char32_t ch = ctbrowser::utf8_next(text, ci);
				for (std::int32_t row = 0; row < 8; ++row) {
					for (std::int32_t col = 0; col < 8; ++col) {
						if (!glyph_pixel(ch, row, col)) { continue; }
						for (std::int32_t sy = 0; sy < scale; ++sy) {
							for (std::int32_t sx = 0; sx < scale; ++sx) {
								ops.put(pen + col * scale + sx,
								y + row * scale + sy, c);
							}
						}
					}
				}
				pen += 8 * scale;
			}
		}
		return ctjs::value{};
	});
	set_method(ctx, "drawImage", [images, ops](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (a.size() >= 3) {
			const std::int32_t handle = arg_i32(a, 0);
			const image * im = images->get(handle);
			if (im != nullptr) {
				const std::int32_t dw = a.size() >= 5 ? arg_i32(a, 3)
				: im->w;
				const std::int32_t dh = a.size() >= 5 ? arg_i32(a, 4)
				: im->h;
				ops.blit(handle, 0, 0, im->w, im->h,
				arg_i32(a, 1),
				arg_i32(a, 2), dw, dh);
			}
		}
		return ctjs::value{};
	});
	// sprite sheets: source region -> destination region
	set_method(ctx, "drawImageRegion", [ops](ctjs::context &, const std::vector<ctjs::value> & a) {
		if (a.size() >= 9) {
			ops.blit(arg_i32(a, 0),
			arg_i32(a, 1),
			arg_i32(a, 2),
			arg_i32(a, 3),
			arg_i32(a, 4),
			arg_i32(a, 5),
			arg_i32(a, 6),
			arg_i32(a, 7),
			arg_i32(a, 8));
		}
		return ctjs::value{};
	});
	return ctjs::value{ctx};
}

// the document global: getElementById/addEventListener plus the
// location.reload() escape hatch (the engine re-instantiates the DOM
// and re-runs the script - navigation, compile-time style)
// collect elements matching a tag ('*' = any) in n's subtree, self included
inline void collect_by_tag(node * n, std::string_view tag, image_store * images, dom_events * ev,
                           std::vector<ctjs::value> & out) {
	if (tag == "*" || n->tag == tag) { out.push_back(element_handle(n, images, ev)); }
	for (const auto & c : n->children) { collect_by_tag(c.get(), tag, images, ev, out); }
}
inline void collect_by_class(node * n, std::string_view cls, image_store * images, dom_events * ev,
                             std::vector<ctjs::value> & out) {
	if (n->has_class(cls)) { out.push_back(element_handle(n, images, ev)); }
	for (const auto & c : n->children) { collect_by_class(c.get(), cls, images, ev, out); }
}

inline ctjs::value make_document(document & doc, image_store & images, dom_events & ev) {
	ev.doc = &doc;
	ctjs::object_t d;
	set_method(d, "createElement", [&doc, &images, &ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
		ev.cx = &cx;
		if (a.empty()) { return ctjs::value{}; }
		node * n = doc.create_element(a[0].to_string());
		return element_handle(n, &images, &ev);
	});
	d.set("body", ctjs::value{}); // filled below once the tree exists
	if (node * b = doc.body()) { d.set("body", element_handle(b, &images, &ev)); }
	set_method(d, "getElementById", [&doc, &images, &ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
		ev.cx = &cx;
		if (a.empty()) { return ctjs::value{}; }
		node * n = doc.by_id(a[0].to_string());
		return n != nullptr ? detail::element_handle(n, &images, &ev)
		: ctjs::value{};
	});
	// querySelector: CSS-subset lookups over the whole tree
	set_method(d, "querySelector", [&doc, &images, &ev](ctjs::context & cx, const std::vector<ctjs::value> & a) -> ctjs::value {
		ev.cx = &cx;
		if (a.empty() || !doc.root) { return ctjs::value{}; }
		node * n = doc.root->query_selector(a[0].to_string());
		return n != nullptr ? detail::element_handle(n, &images, &ev) : ctjs::value{};
	});
	// documentElement handle (the <html> root)
	if (doc.root) { d.set("documentElement", element_handle(doc.root.get(), &images, &ev)); }
	set_method(d, "getElementsByTagName", [&doc, &images, &ev](ctjs::context & cx, const std::vector<ctjs::value> & a) -> ctjs::value {
		ev.cx = &cx;
		std::vector<ctjs::value> out;
		if (doc.root && !a.empty()) { collect_by_tag(doc.root.get(), a[0].to_string(), &images, &ev, out); }
		return ctjs::value::array(std::move(out));
	});
	set_method(d, "getElementsByClassName", [&doc, &images, &ev](ctjs::context & cx, const std::vector<ctjs::value> & a) -> ctjs::value {
		ev.cx = &cx;
		std::vector<ctjs::value> out;
		if (doc.root && !a.empty()) { collect_by_class(doc.root.get(), a[0].to_string(), &images, &ev, out); }
		return ctjs::value::array(std::move(out));
	});
	set_method(d, "addEventListener", [&ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
		ev.cx = &cx;
		if (a.size() >= 2 && a[1].is_function()) {
			ev.listeners[a[0].to_string()].push_back(a[1]);
		}
		return ctjs::value{};
	});
	ctjs::object_t loc;
	set_method(loc, "reload", [&ev](ctjs::context &, const std::vector<ctjs::value> &) {
		ev.reload = true;
		return ctjs::value{};
	});
	// what the last anchor activation recorded (fragments land in hash)
	ctjs::attach_accessor(loc, "href", 'g', ctjs::value::function(
	    [&ev](ctjs::context &, const std::vector<ctjs::value> &) {
		    return ctjs::value{ev.location_href};
	    }, "get href"));
	ctjs::attach_accessor(loc, "hash", 'g', ctjs::value::function(
	    [&ev](ctjs::context &, const std::vector<ctjs::value> &) {
		    return ctjs::value{ev.location_hash};
	    }, "get hash"));
	d.set("location", ctjs::value::object(std::move(loc)));
	return ctjs::value::object(std::move(d));
}

} // namespace detail

// the host bindings a page's script runs with; `title` receives
// document.setTitle updates (the shell mirrors it into the window),
// `images` backs loadImage/drawImage, `ev` collects the script's
// addEventListener/requestAnimationFrame registrations
inline std::vector<ctjs::binding> dom_bindings(document & doc, std::string & title,
                                               image_store & images, dom_events & ev) {
	std::vector<ctjs::binding> out;
	out.push_back({"getElementById",
	               ctjs::native([&doc, &images, &ev](const std::vector<ctjs::value> & a)
	                                -> ctjs::value {
		               if (a.empty()) { return {}; }
		               node * n = doc.by_id(a[0].to_string());
		               return n ? detail::element_handle(n, &images, &ev) : ctjs::value{};
	               },
	               "getElementById")});
	out.push_back({"getContext",
	               ctjs::native([&doc, &images](const std::vector<ctjs::value> & a) -> ctjs::value {
		               if (a.empty()) { return {}; }
		               node * n = doc.by_id(a[0].to_string());
		               return n != nullptr && n->is_canvas()
		                          ? detail::canvas_context(n, &images)
		                          : ctjs::value{};
	               },
	               "getContext")});
	out.push_back({"document", detail::make_document(doc, images, ev)});
	{
		// the window object: the environment surface libraries probe.
		// addEventListener shares the document's listener registry;
		// performance.now is the engine's clock (now_ms)
		auto w = ctjs::rc<ctjs::object_t>::make();
		w->set("innerWidth", ctjs::value{0});
		w->set("innerHeight", ctjs::value{0});
		w->set("devicePixelRatio", ctjs::value{1.0});
		detail::set_method(w, "addEventListener", [&ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
			ev.cx = &cx;
			if (a.size() >= 2 && a[1].is_function()) {
				ev.listeners[a[0].to_string()].push_back(a[1]);
			}
			return ctjs::value{};
		});
		ctjs::object_t perf;
		detail::set_method(perf, "now", [&ev](ctjs::context &, const std::vector<ctjs::value> &) {
			return ctjs::value{ev.now_ms};
		});
		w->set("performance", ctjs::value::object(std::move(perf)));
		// localStorage: an in-memory Storage (a JS object backs it, so it
		// survives location.reload); getItem returns null when absent
		{
			static ctjs::rc<ctjs::object_t> store = ctjs::rc<ctjs::object_t>::make();
			ctjs::object_t ls;
			detail::set_method(ls, "getItem", [](ctjs::context &, const std::vector<ctjs::value> & a) -> ctjs::value {
				if (a.empty()) { return ctjs::value{}; }
				const ctjs::value * p = store->find(a[0].to_string());
				return p != nullptr ? *p : ctjs::value{};
			});
			detail::set_method(ls, "setItem", [](ctjs::context &, const std::vector<ctjs::value> & a) {
				if (a.size() >= 2) { store->set(a[0].to_string(), ctjs::value{a[1].to_string()}); }
				return ctjs::value{};
			});
			detail::set_method(ls, "removeItem", [](ctjs::context &, const std::vector<ctjs::value> & a) {
				if (!a.empty()) { store->set(a[0].to_string(), ctjs::value{}); }
				return ctjs::value{};
			});
			detail::set_method(ls, "clear", [](ctjs::context &, const std::vector<ctjs::value> &) {
				store = ctjs::rc<ctjs::object_t>::make();
				return ctjs::value{};
			});
			ctjs::value lsv = ctjs::value::object(std::move(ls));
			w->set("localStorage", lsv);
			out.push_back({"localStorage", lsv});
		}
		// navigator (desktop UA -> libraries' mobile checks read false)
		{
			ctjs::object_t nav;
			nav.set("userAgent", ctjs::value{std::string{"Mozilla/5.0 (ctbrowser; software) Gecko"}});
			nav.set("platform", ctjs::value{std::string{"ctbrowser"}});
			nav.set("maxTouchPoints", ctjs::value{0});
			ctjs::value navv = ctjs::value::object(std::move(nav));
			w->set("navigator", navv);
			out.push_back({"navigator", navv});
		}
		// scrollTo / scroll: no-op (there is no scrollable viewport)
		detail::set_method(w, "scrollTo", [](ctjs::context &, const std::vector<ctjs::value> &) { return ctjs::value{}; });
		w->set("scroll", *w->find("scrollTo"));
		// onresize: a settable slot; dispatched from engine resize handling
		w->set("onresize", ctjs::value{});
		ev.window_obj = w;
		out.push_back({"window", ctjs::value{w}});
	}
	out.push_back(
	    {"requestAnimationFrame",
	     ctjs::value::function(
	         [&ev](ctjs::context & cx, const std::vector<ctjs::value> & a) {
		         ev.cx = &cx;
		         if (!a.empty() && a[0].is_function()) { ev.raf.push_back(a[0]); }
		         return ctjs::value{++ev.raf_id};
	         },
	         "requestAnimationFrame")});
	{
		// setTimeout/setInterval share the engine clock; fired by tick()
		const auto arm = [&ev](bool repeat, const char * name) {
			return ctjs::value::function(
			    [&ev, repeat](ctjs::context & cx, const std::vector<ctjs::value> & a)
			        -> ctjs::value {
				    ev.cx = &cx;
				    if (a.empty() || !a[0].is_function()) { return ctjs::value{0.0}; }
				    const double ms = a.size() > 1 ? a[1].to_number() : 0.0;
				    ev.timers.push_back({++ev.timer_seq, ev.now_ms + (ms > 0 ? ms : 0),
				                         ms > 0 ? ms : 0, repeat, a[0]});
				    return ctjs::value{ev.timer_seq};
			    },
			    name);
		};
		out.push_back({"setTimeout", arm(false, "setTimeout")});
		out.push_back({"setInterval", arm(true, "setInterval")});
		const auto disarm = [&ev](const char * name) {
			return ctjs::value::function(
			    [&ev](ctjs::context &, const std::vector<ctjs::value> & a) -> ctjs::value {
				    if (!a.empty() && a[0].is_number()) { ev.clear_timer(a[0].as_number()); }
				    return ctjs::value{};
			    },
			    name);
		};
		out.push_back({"clearTimeout", disarm("clearTimeout")});
		out.push_back({"clearInterval", disarm("clearInterval")});
	}
	out.push_back({"alert",
	               ctjs::value::function(
	                   [&ev](ctjs::context &, const std::vector<ctjs::value> & a) {
		                   const std::string msg = detail::arg_str(a, 0);
		                   ev.alerts.push_back(msg);
		                   std::fprintf(stderr, "ctbrowser: alert: %s\n", msg.c_str());
		                   return ctjs::value{};
	                   },
	                   "alert")});
	out.push_back(
	    {"fetch",
	     ctjs::native(
	         [&images](const std::vector<ctjs::value> & a) -> ctjs::value {
		         // the web fetch(), backed by COMPILE-TIME fetching: every
		         // fetch("https://...") literal in the page's script was
		         // fetched during the build (assets.hpp, --fetch-allow
		         // gated) into the embedded-asset registry, so the promise
		         // this returns is already settled - which is exactly the
		         // subset ctjs promises implement. A URL that was not (or
		         // could not be) baked in rejects like a network failure.
		         const std::string url = detail::arg_str(a, 0);
		         const embedded_asset * hit = find_asset(images.embedded, url);
		         if (hit == nullptr) {
			         return ctjs::make_promise(
			             ctjs::make_error("TypeError", "Failed to fetch: " + url), true);
		         }
		         std::string body{reinterpret_cast<const char *>(hit->data), hit->size};
		         ctjs::object_t r;
		         r.set("ok", ctjs::value{true});
		         r.set("status", ctjs::value{200});
		         r.set("url", ctjs::value{url});
		         detail::set_method(r, "text", [body](ctjs::context &, const std::vector<ctjs::value> &) {
			return ctjs::make_promise(ctjs::value{body}, false);
		});
		         detail::set_method(r, "json", [body](ctjs::context &, const std::vector<ctjs::value> &)
		-> ctjs::value {
			try {
				std::size_t i = 0;
				ctjs::value parsed = ctjs::detail::json_value(body, i, 0);
				ctjs::detail::json_ws(body, i);
				if (i != body.size()) { ctjs::detail::json_fail(i); }
				return ctjs::make_promise(std::move(parsed), false);
			} catch (const ctjs::js_throw & ex) {
				return ctjs::make_promise(ex.thrown, true);
			}
		});
		         detail::set_method(r, "bytes", [body](ctjs::context &, const std::vector<ctjs::value> &) {
			ctjs::array_t bytes;
			bytes.reserve(body.size());
			for (const char c : body) {
				bytes.push_back(ctjs::value{static_cast<double>(
				static_cast<unsigned char>(c))});
			}
			return ctjs::make_promise(
			ctjs::value::array(std::move(bytes)), false);
		});
		         return ctjs::make_promise(ctjs::value::object(std::move(r)), false);
	         },
	         "fetch")});
	out.push_back({"loadImage",
	               ctjs::native([&images](const std::vector<ctjs::value> & a) -> ctjs::value {
		               if (a.empty()) { return ctjs::value{-1.0}; }
		               return ctjs::value{
		                   static_cast<double>(images.load(a[0].to_string()))};
	               },
	               "loadImage")});
	out.push_back({"setTitle",
	               ctjs::native([&title](const std::vector<ctjs::value> & a) -> ctjs::value {
		               if (!a.empty()) { title = a[0].to_string(); }
		               return {};
	               },
	               "setTitle")});
	return out;
}

// call a script-defined handler if it exists (missing = quietly skipped)
template <typename... Args>
inline void deliver(ctjs::run_result & script, std::string_view fn, Args &&... args) {
	if (!script[fn].is_function()) { return; }
	(void)script.call(fn, std::forward<Args>(args)...);
}

} // namespace ctbrowser

#endif
