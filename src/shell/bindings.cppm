module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module ctbrowser.shell:bindings;

import ctbrowser.core;
import ctbrowser.dom;
import ctbrowser.script;
import ctbrowser.layout;
import :input;

// The web platform, bound to the v2 VM.
//
// v1 had ~179 hand-written set_method calls against a tree-walking interpreter
// that held raw `node *`. Two things changed and both matter:
//
//   * SCRIPT HOLDS HANDLES, NOT POINTERS. An element wrapper carries a
//     node_id, and every native resolves it against the live document. A stale
//     reference is a failed lookup that returns undefined, not a use-after-free.
//     v1 was only safe because the document owned every node forever and
//     nothing was concurrent.
//   * MUTATION IS A CALLBACK. A native that changes the document calls
//     on_mutation, and the browser decides what that invalidates. Bindings do
//     not know about layout, and layout does not know script exists.
//
// The surface is deliberately the part of v1's that real pages use, and the
// gaps are named rather than stubbed: a `getContext` that returns an object
// with no drawing on it is worse than one that is absent, because a page
// checking for canvas support gets the wrong answer.

export namespace ctbrowser::shell {

using ctbrowser::script::context;
using ctbrowser::script::value;

// Argument coercion. v1 open-coded these at 150 call sites; the plan's answer
// was an IDL generator, and this is the part of it that is actually load
// bearing - the codegen would emit exactly these.
[[nodiscard]] inline std::string arg_string(context & cx, std::span<value> args, std::size_t i) {
	return i < args.size() ? cx.to_string(args[i]) : std::string{};
}
[[nodiscard]] inline double arg_number(std::span<value> args, std::size_t i) {
	return i < args.size() ? context::to_number(args[i]) : 0.0;
}
[[nodiscard]] inline value arg(std::span<value> args, std::size_t i) {
	return i < args.size() ? args[i] : value::undefined();
}

// Where an element wrapper keeps its handle. A property rather than a side
// table, so a wrapper is self-describing and two wrappers for the same element
// resolve to the same node.
inline constexpr std::string_view handle_property = "__node";

class dom_bindings {
public:
	// `on_mutation` is how the browser learns it has to re-run the pipeline.
	// Taking it as a callback rather than a browser reference keeps this
	// testable on its own and keeps the dependency pointing one way.
	dom_bindings(document & doc, atom_table & atoms, std::function<void()> on_mutation)
	    : doc_(&doc), atoms_(&atoms), on_mutation_(std::move(on_mutation)) {}

	// Layout results, so offsetWidth and friends can answer. Set by the
	// browser after each layout; null until the first one, and the natives
	// return 0 then rather than pretending.
	void observe_layout(const layout::fragment * fragments) { fragments_ = fragments; }
	void observe_viewport(int width, int height) {
		viewport_width_ = width;
		viewport_height_ = height;
	}
	// Milliseconds since the page loaded, for performance.now and the timers.
	void advance_clock(double ms) { now_ms_ += ms; }
	[[nodiscard]] double now_ms() const noexcept { return now_ms_; }

	void install(context & cx) {
		cx_ = &cx;
		install_console(cx);
		install_document(cx);
		install_window(cx);
		install_timers(cx);
	}

	// --- dispatch, from the browser --------------------------------------

	// Fire `type` at `target` and at every ancestor, like a bubbling DOM event.
	// Returns whether a listener called preventDefault.
	bool dispatch(std::string_view type, node_id target) {
		if (cx_ == nullptr) { return false; }
		const auto txn = doc_->read();
		value event = make_event(*cx_, type, target);
		for (node_id at = target; at; at = txn.parent(at)) {
			fire_at(at, type, event);
		}
		fire_global(type, event);
		return prevented(event);
	}

	// Run the timers that are due, then the animation callbacks. Returns how
	// many ran, so an event loop can tell whether it needs another frame.
	std::size_t run_due_callbacks() {
		if (cx_ == nullptr) { return 0; }
		std::size_t ran = 0;
		// Copied before running: a callback may add or cancel timers, and
		// iterating the live list while it does is how a timer that
		// re-registers itself becomes an infinite loop inside one tick.
		std::vector<timer> due;
		for (timer & t : timers_) {
			if (!t.cancelled && t.due_ms <= now_ms_) { due.push_back(t); }
		}
		for (const timer & t : due) {
			const auto still = std::ranges::find_if(
			    timers_, [&](const timer & x) { return x.id == t.id && !x.cancelled; });
			if (still == timers_.end()) { continue; }
			if (still->repeating) {
				still->due_ms = now_ms_ + still->interval_ms;
			} else {
				still->cancelled = true;
			}
			(void)cx_->call(t.callback, {});
			++ran;
		}
		std::erase_if(timers_, [](const timer & t) { return t.cancelled; });

		std::vector<value> frame_callbacks;
		frame_callbacks.swap(animation_callbacks_);
		for (const value & cb : frame_callbacks) {
			const value ms = value::number(now_ms_);
			(void)cx_->call(cb, std::span<const value>{&ms, 1});
			++ran;
		}
		return ran;
	}

	[[nodiscard]] std::size_t pending_timers() const noexcept { return timers_.size(); }
	[[nodiscard]] std::size_t pending_animation_frames() const noexcept {
		return animation_callbacks_.size();
	}
	[[nodiscard]] const std::vector<std::string> & console_output() const noexcept {
		return console_;
	}

private:
	struct timer {
		std::uint32_t id = 0;
		value callback;
		double due_ms = 0;
		double interval_ms = 0;
		bool repeating = false;
		bool cancelled = false;
	};
	struct listener {
		node_id target; // empty = document/window
		std::string type;
		value callback;
	};

	// --- element wrappers -------------------------------------------------

	[[nodiscard]] value wrap(context & cx, node_id id) {
		if (!id) { return value::null(); }
		auto * obj = static_cast<script::object_object *>(cx.make_object().as_heap());
		value wrapper = value::object(obj);
		obj->set(std::string{handle_property}, value::number(static_cast<double>(pack(id))));
		install_element_methods(cx, *obj);
		refresh_element(cx, *obj, id);
		return wrapper;
	}

	[[nodiscard]] static std::uint64_t pack(node_id id) {
		return (static_cast<std::uint64_t>(id.generation) << 32) | id.slot;
	}
	[[nodiscard]] static node_id unpack(std::uint64_t bits) {
		node_id id;
		id.slot = static_cast<std::uint32_t>(bits & 0xFFFFFFFFu);
		id.generation = static_cast<std::uint32_t>(bits >> 32);
		return id;
	}

	// The element a native was called on. Returns an empty handle when the
	// receiver is not a wrapper - which a native must treat as "do nothing"
	// rather than as "the document root".
	[[nodiscard]] node_id receiver(context & cx) {
		const value self = cx.current_this();
		if (!self.is_object()) { return node_id{}; }
		auto * obj = static_cast<script::object_object *>(self.as_heap());
		const value * slot = obj->find(std::string{handle_property});
		if (slot == nullptr) { return node_id{}; }
		return unpack(static_cast<std::uint64_t>(context::to_number(*slot)));
	}

	// Live-ish properties. Refreshed when a wrapper is made and after layout,
	// which is what `element.offsetWidth` actually needs to be useful.
	void refresh_element(context & cx, script::object_object & obj, node_id id) {
		const auto txn = doc_->read();
		obj.set("tagName", cx.string(std::string{atoms_->text(txn.tag(id).value_or(atom{}))}));
		obj.set("id", cx.string(std::string{txn.attribute_value(id, atoms_->intern("id"))}));
		obj.set("className", cx.string(std::string{txn.attribute_value(id, atoms_->intern("class"))}));
		const rect box = box_of(id);
		obj.set("offsetLeft", value::number(static_cast<double>(box.x)));
		obj.set("offsetTop", value::number(static_cast<double>(box.y)));
		obj.set("offsetWidth", value::number(static_cast<double>(box.width)));
		obj.set("offsetHeight", value::number(static_cast<double>(box.height)));
	}

	[[nodiscard]] rect box_of(node_id id) const {
		if (fragments_ == nullptr) { return rect{}; }
		const auto find = [&](auto && self, const layout::fragment & f, float dx,
		                      float dy) -> rect {
			const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
			if (f.source == id) { return box; }
			for (const auto & child : f.children) {
				if (const rect hit = self(self, child, box.x, box.y); !hit.empty()) { return hit; }
			}
			return rect{};
		};
		return find(find, *fragments_, 0, 0);
	}

	void install_element_methods(context & cx, script::object_object & obj) {
		const auto method = [&](std::string name, script::native_fn fn) {
			obj.set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
		};

		method("setAttribute", [this](context & c, std::span<value> args) {
			const node_id id = receiver(c);
			if (!id) { return value::undefined(); }
			(void)doc_->set_attribute(id, atoms_->intern_lower(arg_string(c, args, 0)),
			                          arg_string(c, args, 1));
			mutated();
			return value::undefined();
		});
		method("getAttribute", [this](context & c, std::span<value> args) {
			const node_id id = receiver(c);
			if (!id) { return value::null(); }
			const auto txn = doc_->read();
			const std::string_view v =
			    txn.attribute_value(id, atoms_->intern_lower(arg_string(c, args, 0)));
			return v.empty() ? value::null() : c.string(std::string{v});
		});
		method("setText", [this](context & c, std::span<value> args) {
			set_text(id_or_nothing(c), arg_string(c, args, 0));
			return value::undefined();
		});
		method("getText", [this](context & c, std::span<value>) {
			const node_id id = receiver(c);
			return id ? c.string(text_of(id)) : c.string(std::string{});
		});
		method("addClass", [this](context & c, std::span<value> args) {
			edit_classes(receiver(c), arg_string(c, args, 0), true);
			return value::undefined();
		});
		method("removeClass", [this](context & c, std::span<value> args) {
			edit_classes(receiver(c), arg_string(c, args, 0), false);
			return value::undefined();
		});
		method("hasClass", [this](context & c, std::span<value> args) {
			const node_id id = receiver(c);
			if (!id) { return value::boolean(false); }
			const auto txn = doc_->read();
			const std::string want = arg_string(c, args, 0);
			for (const std::string_view cls :
			     split(txn.attribute_value(id, atoms_->intern("class")))) {
				if (cls == want) { return value::boolean(true); }
			}
			return value::boolean(false);
		});
		method("appendChild", [this](context & c, std::span<value> args) {
			const node_id parent = receiver(c);
			const node_id child = handle_of(arg(args, 0));
			if (parent && child) {
				(void)doc_->append_child(parent, child);
				mutated();
			}
			return arg(args, 0);
		});
		method("removeChild", [this](context &, std::span<value> args) {
			const node_id child = handle_of(arg(args, 0));
			if (child) {
				(void)doc_->remove_child(child);
				mutated();
			}
			return arg(args, 0);
		});
		method("addEventListener", [this](context & c, std::span<value> args) {
			const node_id id = receiver(c);
			if (id) { listeners_.push_back(listener{id, arg_string(c, args, 0), arg(args, 1)}); }
			return value::undefined();
		});
	}

	[[nodiscard]] node_id id_or_nothing(context & c) { return receiver(c); }

	[[nodiscard]] node_id handle_of(value v) {
		if (!v.is_object()) { return node_id{}; }
		auto * obj = static_cast<script::object_object *>(v.as_heap());
		const value * slot = obj->find(std::string{handle_property});
		return slot == nullptr ? node_id{}
		                       : unpack(static_cast<std::uint64_t>(context::to_number(*slot)));
	}

	[[nodiscard]] std::string text_of(node_id id) const {
		const auto txn = doc_->read();
		std::string out;
		const auto walk = [&](auto && self, node_id at) -> void {
			out += txn.text(at);
			for (const node_id child : txn.children(at)) { self(self, child); }
		};
		walk(walk, id);
		return out;
	}

	void set_text(node_id id, std::string text) {
		if (!id) { return; }
		// Copied before removing: children() is a view onto the live child
		// list, and removing while iterating it is a use-after-free waiting for
		// the second child.
		std::vector<node_id> existing;
		{
			const auto txn = doc_->read();
			for (const node_id child : txn.children(id)) { existing.push_back(child); }
		}
		for (const node_id child : existing) { (void)doc_->remove_child(child); }
		if (const node_id created = doc_->create_text(text)) {
			(void)doc_->append_child(id, created);
		}
		mutated();
	}

	void edit_classes(node_id id, const std::string & name, bool add) {
		if (!id || name.empty()) { return; }
		const auto txn = doc_->read();
		std::vector<std::string> classes;
		for (const std::string_view cls : split(txn.attribute_value(id, atoms_->intern("class")))) {
			if (cls != name) { classes.emplace_back(cls); }
		}
		if (add) { classes.push_back(name); }
		std::string joined;
		for (const std::string & cls : classes) {
			if (!joined.empty()) { joined += ' '; }
			joined += cls;
		}
		(void)doc_->set_attribute(id, atoms_->intern("class"), joined);
		mutated();
	}

	[[nodiscard]] static std::vector<std::string_view> split(std::string_view text) {
		std::vector<std::string_view> out;
		std::size_t at = 0;
		while (at < text.size()) {
			while (at < text.size() && text[at] == ' ') { ++at; }
			const std::size_t start = at;
			while (at < text.size() && text[at] != ' ') { ++at; }
			if (at > start) { out.push_back(text.substr(start, at - start)); }
		}
		return out;
	}

	void mutated() {
		if (on_mutation_) { on_mutation_(); }
	}

	// --- globals ----------------------------------------------------------

	void install_console(context & cx) {
		auto * console = static_cast<script::object_object *>(cx.make_object().as_heap());
		const auto log = [this](context & c, std::span<value> args) {
			std::string line;
			for (std::size_t i = 0; i < args.size(); ++i) {
				if (i > 0) { line += ' '; }
				line += c.to_string(args[i]);
			}
			console_.push_back(std::move(line));
			return value::undefined();
		};
		console->set("log", value::object(cx.allocate<script::native_object>("log", log)));
		console->set("warn", value::object(cx.allocate<script::native_object>("warn", log)));
		console->set("error", value::object(cx.allocate<script::native_object>("error", log)));
		cx.define_global("console", value::object(console));
	}

	void install_document(context & cx) {
		auto * doc = static_cast<script::object_object *>(cx.make_object().as_heap());
		const auto method = [&](std::string name, script::native_fn fn) {
			doc->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
		};

		method("getElementById", [this](context & c, std::span<value> args) {
			return wrap(c, find_by_id(arg_string(c, args, 0)));
		});
		method("createElement", [this](context & c, std::span<value> args) {
			// NOT a mutation: a created element is detached and changes nothing
			// on screen until it is appended.
			return wrap(c, doc_->create_element(atoms_->intern_lower(arg_string(c, args, 0))));
		});
		method("createTextNode", [this](context & c, std::span<value> args) {
			return wrap(c, doc_->create_text(arg_string(c, args, 0)));
		});
		method("addEventListener", [this](context & c, std::span<value> args) {
			listeners_.push_back(listener{node_id{}, arg_string(c, args, 0), arg(args, 1)});
			return value::undefined();
		});

		doc->set("body", wrap(cx, find_by_tag("body")));
		doc->set("documentElement", wrap(cx, find_by_tag("html")));
		cx.define_global("document", value::object(doc));
	}

	void install_window(context & cx) {
		auto * window = static_cast<script::object_object *>(cx.make_object().as_heap());
		window->set("innerWidth", value::number(viewport_width_));
		window->set("innerHeight", value::number(viewport_height_));
		window->set("devicePixelRatio", value::number(1));
		window->set("addEventListener",
		            value::object(cx.allocate<script::native_object>(
		                "addEventListener", [this](context & c, std::span<value> args) {
			                listeners_.push_back(listener{node_id{}, arg_string(c, args, 0),
			                                              arg(args, 1)});
			                return value::undefined();
		                })));
		auto * performance = static_cast<script::object_object *>(cx.make_object().as_heap());
		performance->set("now", value::object(cx.allocate<script::native_object>(
		                            "now", [this](context &, std::span<value>) {
			                            return value::number(now_ms_);
		                            })));
		window->set("performance", value::object(performance));
		cx.define_global("window", value::object(window));
		cx.define_global("performance", value::object(performance));
	}

	void install_timers(context & cx) {
		cx.define_native("setTimeout", [this](context &, std::span<value> args) {
			return value::number(add_timer(arg(args, 0), arg_number(args, 1), false));
		});
		cx.define_native("setInterval", [this](context &, std::span<value> args) {
			return value::number(add_timer(arg(args, 0), arg_number(args, 1), true));
		});
		const auto cancel = [this](context &, std::span<value> args) {
			const auto id = static_cast<std::uint32_t>(arg_number(args, 0));
			for (timer & t : timers_) {
				if (t.id == id) { t.cancelled = true; }
			}
			return value::undefined();
		};
		cx.define_native("clearTimeout", cancel);
		cx.define_native("clearInterval", cancel);
		cx.define_native("requestAnimationFrame", [this](context &, std::span<value> args) {
			animation_callbacks_.push_back(arg(args, 0));
			return value::number(++next_timer_id_);
		});
	}

	[[nodiscard]] std::uint32_t add_timer(value callback, double delay_ms, bool repeating) {
		const std::uint32_t id = ++next_timer_id_;
		timers_.push_back(timer{id, callback, now_ms_ + std::max(0.0, delay_ms),
		                        std::max(0.0, delay_ms), repeating, false});
		return id;
	}

	// --- events -----------------------------------------------------------

	[[nodiscard]] value make_event(context & cx, std::string_view type, node_id target) {
		auto * event = static_cast<script::object_object *>(cx.make_object().as_heap());
		event->set("type", cx.string(std::string{type}));
		event->set("target", wrap(cx, target));
		event->set("defaultPrevented", value::boolean(false));
		// One SHARED event object per dispatch, so preventDefault called by any
		// listener is visible to the browser and to every later listener - which
		// is what makes it mean anything at all.
		event->set("preventDefault",
		           value::object(cx.allocate<script::native_object>(
		               "preventDefault", [](context & c, std::span<value>) {
			               const value self = c.current_this();
			               if (self.is_object()) {
				               static_cast<script::object_object *>(self.as_heap())
				                   ->set("defaultPrevented", value::boolean(true));
			               }
			               return value::undefined();
		               })));
		return value::object(event);
	}

	[[nodiscard]] static bool prevented(value event) {
		if (!event.is_object()) { return false; }
		const value * slot =
		    static_cast<script::object_object *>(event.as_heap())->find("defaultPrevented");
		return slot != nullptr && context::truthy(*slot);
	}

	void fire_at(node_id target, std::string_view type, value event) {
		for (const listener & l : listeners_) {
			if (l.target == target && l.type == type) {
				(void)cx_->call(l.callback, std::span<const value>{&event, 1});
			}
		}
	}
	void fire_global(std::string_view type, value event) {
		for (const listener & l : listeners_) {
			if (!l.target && l.type == type) {
				(void)cx_->call(l.callback, std::span<const value>{&event, 1});
			}
		}
	}

	// --- lookups ----------------------------------------------------------

	[[nodiscard]] node_id find_by_id(const std::string & want) {
		const auto txn = doc_->read();
		const atom key = atoms_->intern("id");
		node_id found{};
		const auto walk = [&](auto && self, node_id at) -> void {
			if (!found && txn.attribute_value(at, key) == want) { found = at; }
			for (const node_id child : txn.children(at)) { self(self, child); }
		};
		walk(walk, txn.root());
		return found;
	}

	[[nodiscard]] node_id find_by_tag(std::string_view tag) {
		const auto txn = doc_->read();
		const atom want = atoms_->intern_lower(tag);
		node_id found{};
		const auto walk = [&](auto && self, node_id at) -> void {
			if (!found && txn.tag(at).value_or(atom{}) == want) { found = at; }
			for (const node_id child : txn.children(at)) { self(self, child); }
		};
		walk(walk, txn.root());
		return found;
	}

	document * doc_;
	atom_table * atoms_;
	std::function<void()> on_mutation_;
	context * cx_ = nullptr;
	const layout::fragment * fragments_ = nullptr;
	int viewport_width_ = 0;
	int viewport_height_ = 0;
	double now_ms_ = 0;

	std::vector<listener> listeners_;
	std::vector<timer> timers_;
	std::vector<value> animation_callbacks_;
	std::vector<std::string> console_;
	std::uint32_t next_timer_id_ = 0;
};

} // namespace ctbrowser::shell
