module;
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module ctbrowser.script:vm;

import ctbrowser.core;

import :bytecode;
import :value;

// The interpreter.
//
// v1 walked the AST on every execution: each time round a loop it re-decided
// what every node meant, re-looked-up every identifier by string, and
// re-dispatched through a virtual call per node. This walks a flat array of
// 4-byte instructions with registers already assigned.
//
// GC is mark-and-sweep over precise roots. Precise because the VM knows
// exactly where its roots are - the register stack, the globals table, and
// the call frames - so there is no conservative stack scanning and no
// pointer-shaped integer can accidentally keep an object alive. Generational
// collection is the next step, and the allocation list here is already the
// shape a nursery would slot into.
//
// One agent per thread, like a real JS agent: a context is NOT thread-safe
// and is not meant to be. Workers get their own context; what they share is
// the DOM, which has its own concurrency control.

export namespace ctbrowser::script {

struct closure_object;

using native_fn = std::function<value(class context &, std::span<value>)>;

struct native_object final : heap_object {
	std::string name;
	native_fn fn;
	native_object(std::string n, native_fn f)
	    : heap_object(heap_kind::native), name(std::move(n)), fn(std::move(f)) {}
};

// A captured variable's box. Sharing the CELL rather than the value is what
// makes a mutation through a closure visible to everyone else holding it.
struct cell_object final : heap_object {
	value slot;
	explicit cell_object(value v) : heap_object(heap_kind::cell), slot(v) {}
};

struct closure_object final : heap_object {
	const function_proto * proto = nullptr;
	std::vector<value> upvalues; // each one is a cell_object
	explicit closure_object(const function_proto * p) : heap_object(heap_kind::function), proto(p) {}
};

struct run_result {
	value returned = value::undefined();
	bool ok = true;
	std::string error;
};

class context {
public:
	context() = default;
	~context() { sweep_all(); }

	context(const context &) = delete;
	context & operator=(const context &) = delete;

	// --- allocation -------------------------------------------------------
	template <typename T, typename... Args> [[nodiscard]] T * allocate(Args &&... args) {
		auto * p = new T(std::forward<Args>(args)...);
		p->next = heap_;
		heap_ = p;
		++live_objects_;
		return p;
	}
	[[nodiscard]] value string(std::string s) {
		return value::object(allocate<string_object>(std::move(s)));
	}
	[[nodiscard]] value make_object() { return value::object(allocate<object_object>()); }
	[[nodiscard]] value make_array() { return value::object(allocate<array_object>()); }

	void define_global(std::string name, value v) { globals_[std::move(name)] = v; }
	void define_native(std::string name, native_fn fn) {
		value v = value::object(allocate<native_object>(name, std::move(fn)));
		globals_[std::move(name)] = v;
	}
	[[nodiscard]] value global(std::string_view name) const {
		const auto it = globals_.find(std::string{name});
		return it == globals_.end() ? value::undefined() : it->second;
	}

	// --- execution ---------------------------------------------------------
	//
	// THE PROGRAM MUST OUTLIVE THE CONTEXT. Closures hold `const function_proto *`
	// into it, and anything that calls back in later - a timer, an event
	// listener, requestAnimationFrame - dereferences them long after run()
	// returned. Passing a temporary works exactly until the first callback.
	run_result run(const program & prog);

	// The receiver of the method call currently running, for natives. JS
	// methods get `this` from the call site; a native has no frame to read it
	// from, so the VM hands it over here. It is how one native object's methods
	// tell which object they were called on - `el.setText(...)` and
	// `other.setText(...)` are the same native.
	[[nodiscard]] value current_this() const noexcept { return current_this_; }

	// Call a JS function FROM C++. This is what an event listener, a timer and
	// a requestAnimationFrame callback all need, and without it script can only
	// ever be entered at the top.
	//
	// Re-entrant: it runs a nested interpreter loop on the existing register
	// stack rather than resetting it, so a listener may itself call back into
	// script.
	value call(value callable, std::span<const value> args,
	           value this_value = value::undefined());

	// --- conversions (ECMA-262 shaped, and shared with the bindings) -------
	[[nodiscard]] static bool truthy(value v);
	[[nodiscard]] static double to_number(value v);
	[[nodiscard]] std::string to_string(value v);
	[[nodiscard]] static std::string_view type_of(value v);
	[[nodiscard]] static bool loose_equals(value a, value b);

	// --- prototypes ---------------------------------------------------------
	//
	// `"abc".split(...)` and `[1,2].push(...)` resolve to nothing without these:
	// a string is not an object_object, so there is nowhere on it to put a
	// method. Rather than special-case every builtin inside the interpreter,
	// each VALUE KIND gets a prototype object, and property lookup falls back to
	// it. Adding a method is then putting a native in a table - which is what
	// makes a standard library mechanical instead of 2000 lines of switch.
	//
	// Not a full prototype CHAIN: there is one level, and no user-visible
	// `__proto__` or `Object.create`. That is a real limitation, and it covers
	// everything a page does with builtins.
	enum class proto_kind : std::uint8_t { object, array, string, number, count_ };

	void set_prototype(proto_kind kind, object_object * table) {
		prototypes_[static_cast<std::size_t>(kind)] = table;
	}
	[[nodiscard]] object_object * prototype(proto_kind kind) const {
		return prototypes_[static_cast<std::size_t>(kind)];
	}

	// One property lookup, shared by get_prop, get_index-with-a-string-key and
	// call_method. Three copies of this is three chances for `a.length` and
	// `a["length"]` to disagree.
	[[nodiscard]] value lookup_property(value target, const std::string & name) const;
	// `target[key]` for an arbitrary key value. Numeric keys index an array or
	// a string; anything else is a named lookup. Shared by get_index and by
	// computed method calls, because `a[0]()` and `a['push']()` must both work
	// and they take different branches.
	[[nodiscard]] value lookup_index(value target, value key);

	// --- gc ----------------------------------------------------------------
	std::size_t collect();
	[[nodiscard]] std::size_t live_objects() const noexcept { return live_objects_; }

private:
	struct call_frame {
		const function_proto * proto = nullptr;
		std::size_t ip = 0;
		std::size_t base = 0; // index into registers_ of this frame's r0
		std::uint8_t result_reg = 0;
		closure_object * closure = nullptr; // whose upvalues this body sees
		// The receiver. A JS body reads it through `this`; before this existed
		// `this` compiled to undefined unconditionally, so no method could see
		// the object it was called on.
		value receiver = value::undefined();
		// How many exception handlers this frame had on entry. Unwinding pops
		// back to it, so a handler in a caller cannot be caught by a callee.
		std::size_t handler_base = 0;
	};

	// A live try block: where to jump, and where the state was when it started.
	struct handler {
		std::size_t frame = 0;    // index into frames_
		std::size_t address = 0;  // the catch block
		std::size_t reg_top = 0;  // registers_ size on entry
		std::uint8_t slot = 0;    // where to put the thrown value
	};

	[[nodiscard]] value execute(const program & prog, const function_proto & entry);
	[[nodiscard]] value run_loop(std::size_t stop_depth);
	void raise(std::string message) {
		if (!failed_) {
			failed_ = true;
			error_ = std::move(message);
		}
	}

	// Find the innermost live handler and jump to it, discarding every call
	// frame between here and the one that owns it. Returning false means
	// nothing caught it, which is an uncaught exception.
	//
	// This is why exceptions are a VM change and not a compiler one: a `throw`
	// several frames deep has to reach a `try` in a caller, and only the VM
	// knows where those frames are.
	[[nodiscard]] bool unwind_to_handler() {
		while (!handlers_.empty()) {
			const handler h = handlers_.back();
			handlers_.pop_back();
			if (h.frame >= frames_.size()) { continue; } // its frame already returned
			frames_.resize(h.frame + 1);
			call_frame & target = frames_.back();
			target.ip = h.address;
			if (registers_.size() < h.reg_top) { registers_.resize(h.reg_top, value::undefined()); }
			registers_[target.base + h.slot] = thrown_;
			thrown_ = value::undefined();
			return true;
		}
		return false;
	}

	void mark(value v);
	void mark_object(heap_object * o);
	void sweep_all();

	// Set while a native runs, so it can see its receiver.
	value current_this_ = value::undefined();
	// The program being executed, so a call from C++ can find the string
	// tables a nested frame needs.
	const program * program_ = nullptr;
	std::vector<flat_map<std::uint16_t, value>> string_cache_;
	// Live try blocks, innermost last. Not per-frame, because a throw has to be
	// able to find a handler several frames up.
	std::vector<handler> handlers_;
	std::array<object_object *, static_cast<std::size_t>(proto_kind::count_)> prototypes_{};
	value thrown_ = value::undefined();

	flat_map<std::string, value> globals_;
	std::vector<value> registers_;
	std::vector<call_frame> frames_;
	heap_object * heap_ = nullptr;
	std::size_t live_objects_ = 0;
	bool failed_ = false;
	std::string error_;
};

// ===================== conversions ======================================

inline bool context::truthy(value v) {
	if (v.is_boolean()) { return v.as_boolean(); }
	if (v.is_nullish()) { return false; }
	if (v.is_number()) {
		const double d = v.as_number();
		return d != 0 && !std::isnan(d);
	}
	if (v.is_string()) { return !static_cast<string_object *>(v.as_heap())->text.empty(); }
	return true; // every other object is truthy
}

inline double context::to_number(value v) {
	if (v.is_number()) { return v.as_number(); }
	if (v.is_boolean()) { return v.as_boolean() ? 1 : 0; }
	if (v.is_null()) { return 0; }
	if (v.is_undefined()) { return std::nan(""); }
	if (v.is_string()) {
		const std::string & s = static_cast<string_object *>(v.as_heap())->text;
		try {
			std::size_t consumed = 0;
			const double d = std::stod(s, &consumed);
			while (consumed < s.size() && (s[consumed] == ' ' || s[consumed] == '\t')) { ++consumed; }
			return consumed == s.size() ? d : std::nan("");
		} catch (...) {
			return s.find_first_not_of(" \t\n\r") == std::string::npos ? 0.0 : std::nan("");
		}
	}
	return std::nan("");
}

inline std::string context::to_string(value v) {
	if (v.is_undefined()) { return "undefined"; }
	if (v.is_null()) { return "null"; }
	if (v.is_boolean()) { return v.as_boolean() ? "true" : "false"; }
	if (v.is_number()) {
		const double d = v.as_number();
		if (std::isnan(d)) { return "NaN"; }
		if (std::isinf(d)) { return d > 0 ? "Infinity" : "-Infinity"; }
		// integral doubles print without a decimal point, as JS does
		if (d == static_cast<double>(static_cast<std::int64_t>(d)) && std::abs(d) < 1e15) {
			return std::to_string(static_cast<std::int64_t>(d));
		}
		std::string out = std::to_string(d);
		while (out.size() > 1 && out.back() == '0') { out.pop_back(); }
		if (!out.empty() && out.back() == '.') { out.pop_back(); }
		return out;
	}
	if (v.is_string()) { return static_cast<string_object *>(v.as_heap())->text; }
	if (v.is_array()) {
		auto * arr = static_cast<array_object *>(v.as_heap());
		std::string out;
		for (std::size_t i = 0; i < arr->items.size(); ++i) {
			if (i != 0) { out += ','; }
			if (!arr->items[i].is_nullish()) { out += to_string(arr->items[i]); }
		}
		return out;
	}
	if (v.is_callable()) { return "function"; }
	return "[object Object]";
}

inline std::string_view context::type_of(value v) {
	if (v.is_undefined()) { return "undefined"; }
	if (v.is_null()) { return "object"; } // the famous wart, preserved
	if (v.is_boolean()) { return "boolean"; }
	if (v.is_number()) { return "number"; }
	if (v.is_string()) { return "string"; }
	if (v.is_callable()) { return "function"; }
	return "object";
}

inline bool context::loose_equals(value a, value b) {
	if (a.is_nullish() && b.is_nullish()) { return true; }
	if (a.is_nullish() || b.is_nullish()) { return false; }
	if (a.is_number() && b.is_number()) { return a.as_number() == b.as_number(); }
	if (a.is_string() && b.is_string()) {
		return static_cast<string_object *>(a.as_heap())->text ==
		       static_cast<string_object *>(b.as_heap())->text;
	}
	if (a.is_heap() && b.is_heap()) { return a == b; }
	return to_number(a) == to_number(b); // the coercing cases
}

// ===================== gc ================================================

inline void context::mark_object(heap_object * o) {
	if (o == nullptr || o->marked) { return; }
	o->marked = true;
	switch (o->kind) {
	case heap_kind::array:
		for (const value & v : static_cast<array_object *>(o)->items) { mark(v); }
		break;
	case heap_kind::object: {
		auto * obj = static_cast<object_object *>(o);
		for (const auto & [name, v] : obj->props) { mark(v); }
		mark(obj->prototype);
		break;
	}
	case heap_kind::cell:
		mark(static_cast<cell_object *>(o)->slot);
		break;
	case heap_kind::function:
		// A closure OWNS its upvalue cells. Missing this frees a captured
		// variable while the closure that captured it is still reachable.
		for (const value & up : static_cast<closure_object *>(o)->upvalues) { mark(up); }
		break;
	default: break; // strings and natives own no values
	}
}

inline void context::mark(value v) {
	if (v.is_heap()) { mark_object(v.as_heap()); }
}

inline value context::lookup_index(value target, value key) {
	if (target.is_array() && key.is_number()) {
		auto * arr = static_cast<array_object *>(target.as_heap());
		const auto i = static_cast<std::ptrdiff_t>(key.as_number());
		if (i >= 0 && static_cast<std::size_t>(i) < arr->items.size()) {
			return arr->items[static_cast<std::size_t>(i)];
		}
		return value::undefined();
	}
	if (target.is_string() && key.is_number()) {
		const std::string & text = static_cast<string_object *>(target.as_heap())->text;
		const auto i = static_cast<std::size_t>(key.as_number());
		return i < text.size() ? string(std::string{text[i]}) : value::undefined();
	}
	return lookup_property(target, to_string(key));
}

inline value context::lookup_property(value target, const std::string & name) const {
	// Own properties first: a page that writes `arr.length = 0` or shadows a
	// method on one object must not be overridden by the prototype.
	if (target.is_object()) {
		if (value * found = static_cast<object_object *>(target.as_heap())->find(name)) {
			return *found;
		}
		if (object_object * table = prototype(proto_kind::object)) {
			if (value * found = table->find(name)) { return *found; }
		}
		return value::undefined();
	}
	if (target.is_array()) {
		auto * arr = static_cast<array_object *>(target.as_heap());
		if (name == "length") { return value::number(static_cast<double>(arr->items.size())); }
		if (object_object * table = prototype(proto_kind::array)) {
			if (value * found = table->find(name)) { return *found; }
		}
		return value::undefined();
	}
	if (target.is_string()) {
		auto * str = static_cast<string_object *>(target.as_heap());
		if (name == "length") { return value::number(static_cast<double>(str->text.size())); }
		if (object_object * table = prototype(proto_kind::string)) {
			if (value * found = table->find(name)) { return *found; }
		}
		return value::undefined();
	}
	if (target.is_number()) {
		if (object_object * table = prototype(proto_kind::number)) {
			if (value * found = table->find(name)) { return *found; }
		}
	}
	return value::undefined();
}

inline std::size_t context::collect() {
	// Precise roots: everything reachable is reachable from exactly these.
	for (const auto & [name, v] : globals_) { mark(v); }
	for (const value & v : registers_) { mark(v); }
	// The receiver of a native call in progress. It is held in a C++ local, not
	// in a register, so nothing else would keep it alive - and collecting the
	// object a method is running on is about as bad as it gets.
	mark(current_this_);
	// And the closure each live frame is executing. A function called from C++
	// via call() is likewise only referenced from a C++ local; without this its
	// upvalues can be freed while its body is still running.
	for (const call_frame & f : frames_) {
		if (f.closure != nullptr) { mark_object(f.closure); }
		mark(f.receiver);
	}
	// A thrown value in flight is reachable from nothing else.
	mark(thrown_);
	// The prototype tables hold every builtin method. Nothing else references
	// them, so without this the standard library is collected on the first gc.
	for (object_object * table : prototypes_) {
		if (table != nullptr) { mark_object(table); }
	}

	std::size_t freed = 0;
	heap_object ** link = &heap_;
	while (*link != nullptr) {
		heap_object * o = *link;
		if (o->marked) {
			o->marked = false; // clear for the next cycle
			link = &o->next;
		} else {
			*link = o->next;
			delete o;
			++freed;
			--live_objects_;
		}
	}
	return freed;
}

inline void context::sweep_all() {
	while (heap_ != nullptr) {
		heap_object * next = heap_->next;
		delete heap_;
		heap_ = next;
	}
	live_objects_ = 0;
}

// ===================== the dispatch loop =================================

// Call a JS function from C++.
//
// The two cases are genuinely different: a native is just a C++ call, while a
// closure needs a frame on the interpreter's own stack. Giving the closure a
// region ABOVE everything currently live is what lets this be re-entrant - the
// caller's registers are untouched, so a listener that triggers another
// listener works rather than corrupting the frame that dispatched it.
inline value context::call(value callable, std::span<const value> args, value this_value) {
	if (callable.is_kind(heap_kind::native)) {
		auto * nat = static_cast<native_object *>(callable.as_heap());
		std::vector<value> copy{args.begin(), args.end()};
		const value saved = current_this_;
		current_this_ = this_value;
		const value out = nat->fn(*this, copy);
		current_this_ = saved;
		return out;
	}
	if (!callable.is_kind(heap_kind::function) || program_ == nullptr) {
		return value::undefined();
	}
	auto * fnobj = static_cast<closure_object *>(callable.as_heap());
	const function_proto & target = *fnobj->proto;

	const std::size_t new_base = registers_.size();
	registers_.resize(new_base + target.frame_size + 8u, value::undefined());
	for (std::size_t i = 0; i < target.param_count; ++i) {
		registers_[new_base + i] = i < args.size() ? args[i] : value::undefined();
	}
	if (frames_.size() > 512) {
		raise("call stack exhausted");
		return value::undefined();
	}
	const std::size_t depth = frames_.size();
	const value saved = current_this_;
	current_this_ = this_value;
	frames_.push_back(call_frame{&target, 0, new_base, 0, fnobj, this_value, handlers_.size()});
	const value out = run_loop(depth);
	current_this_ = saved;
	// Only shrink back if nothing below is still using the space - a nested
	// call that grew the stack further has already returned by now.
	if (registers_.size() >= new_base) { registers_.resize(new_base); }
	return out;
}

inline run_result context::run(const program & prog) {
	run_result result;
	if (!prog.ok) {
		result.ok = false;
		result.error = prog.error;
		return result;
	}
	failed_ = false;
	error_.clear();
	result.returned = execute(prog, prog.functions[0]);
	result.ok = !failed_;
	result.error = error_;
	return result;
}

inline value context::execute(const program & prog, const function_proto & entry) {
	registers_.assign(entry.frame_size + 8u, value::undefined());
	frames_.clear();
	frames_.push_back(call_frame{&entry, 0, 0, 0, nullptr, value::undefined(), 0});
	program_ = &prog;
	// Per-frame string interning: a literal in a loop should allocate once,
	// not once per iteration.
	string_cache_.clear();
	string_cache_.resize(prog.functions.size());
	return run_loop(0);
}

// The interpreter loop, entered at a frame depth and running until it unwinds
// back to it. `stop_depth` is 0 for the top-level program and the caller's
// depth for a call from C++ - which is what makes call() re-entrant instead of
// a second interpreter.
inline value context::run_loop(std::size_t stop_depth) {
	const program & prog = *program_;
	std::vector<flat_map<std::uint16_t, value>> & string_cache = string_cache_;

	while (frames_.size() > stop_depth && !failed_) {
		call_frame & frame = frames_.back();
		const function_proto & fn = *frame.proto;
		if (frame.ip >= fn.code.size()) { break; }
		const instruction in = fn.code[frame.ip++];
		const std::size_t base = frame.base;
		const auto reg = [&](std::uint8_t r) -> value & { return registers_[base + r]; };

		switch (in.code) {
		case op::load_const: reg(in.a) = fn.constants[in.bx()]; break;
		case op::load_string: {
			const auto proto_index =
			    static_cast<std::size_t>(&fn - prog.functions.data());
			auto & cache = string_cache[proto_index];
			const auto it = cache.find(in.bx());
			if (it != cache.end()) {
				reg(in.a) = it->second;
			} else {
				const value v = string(fn.strings[in.bx()]);
				cache.emplace(in.bx(), v);
				reg(in.a) = v;
			}
			break;
		}
		case op::load_undef: reg(in.a) = value::undefined(); break;
		case op::load_null: reg(in.a) = value::null(); break;
		case op::load_true: reg(in.a) = value::boolean(true); break;
		case op::load_false: reg(in.a) = value::boolean(false); break;
		case op::move: reg(in.a) = reg(in.b); break;

		case op::get_global: {
			const auto it = globals_.find(fn.names[in.bx()]);
			reg(in.a) = it == globals_.end() ? value::undefined() : it->second;
			break;
		}
		case op::set_global: globals_[fn.names[in.bx()]] = reg(in.a); break;

		case op::add: reg(in.a) = value::number(to_number(reg(in.b)) + to_number(reg(in.c))); break;
		case op::sub: reg(in.a) = value::number(to_number(reg(in.b)) - to_number(reg(in.c))); break;
		case op::mul: reg(in.a) = value::number(to_number(reg(in.b)) * to_number(reg(in.c))); break;
		case op::div: reg(in.a) = value::number(to_number(reg(in.b)) / to_number(reg(in.c))); break;
		case op::mod:
			reg(in.a) = value::number(std::fmod(to_number(reg(in.b)), to_number(reg(in.c))));
			break;
		case op::pow:
			reg(in.a) = value::number(std::pow(to_number(reg(in.b)), to_number(reg(in.c))));
			break;
		case op::add_generic: {
			// JS `+`: string concatenation if EITHER side is a string, numeric
			// addition otherwise. The one operator whose meaning is decided by
			// its operands, which is why it is not folded into `add`.
			const value l = reg(in.b);
			const value r = reg(in.c);
			if (l.is_string() || r.is_string()) {
				reg(in.a) = string(to_string(l) + to_string(r));
			} else {
				reg(in.a) = value::number(to_number(l) + to_number(r));
			}
			break;
		}
		case op::concat: reg(in.a) = string(to_string(reg(in.b)) + to_string(reg(in.c))); break;
		case op::negate: reg(in.a) = value::number(-to_number(reg(in.b))); break;
		case op::logical_not: reg(in.a) = value::boolean(!truthy(reg(in.b))); break;

		case op::equal: reg(in.a) = value::boolean(reg(in.b).strict_equals(reg(in.c))); break;
		case op::not_equal: reg(in.a) = value::boolean(!reg(in.b).strict_equals(reg(in.c))); break;
		case op::loose_equal: reg(in.a) = value::boolean(loose_equals(reg(in.b), reg(in.c))); break;
		case op::less:
			reg(in.a) = value::boolean(to_number(reg(in.b)) < to_number(reg(in.c)));
			break;
		case op::less_equal:
			reg(in.a) = value::boolean(to_number(reg(in.b)) <= to_number(reg(in.c)));
			break;
		case op::greater:
			reg(in.a) = value::boolean(to_number(reg(in.b)) > to_number(reg(in.c)));
			break;
		case op::greater_equal:
			reg(in.a) = value::boolean(to_number(reg(in.b)) >= to_number(reg(in.c)));
			break;

		case op::jump: frame.ip = static_cast<std::size_t>(static_cast<std::int64_t>(frame.ip) + in.sbx()); break;
		case op::jump_if_false:
			if (!truthy(reg(in.a))) {
				frame.ip = static_cast<std::size_t>(static_cast<std::int64_t>(frame.ip) + in.sbx());
			}
			break;
		case op::jump_if_true:
			if (truthy(reg(in.a))) {
				frame.ip = static_cast<std::size_t>(static_cast<std::int64_t>(frame.ip) + in.sbx());
			}
			break;

		case op::new_object: reg(in.a) = make_object(); break;
		case op::new_array: reg(in.a) = make_array(); break;
		case op::append:
			if (reg(in.a).is_array()) {
				static_cast<array_object *>(reg(in.a).as_heap())->items.push_back(reg(in.b));
			}
			break;
		case op::get_prop:
			reg(in.a) = lookup_property(reg(in.b), fn.names[in.c]);
			break;
		case op::set_prop:
			if (reg(in.a).is_object()) {
				static_cast<object_object *>(reg(in.a).as_heap())->set(fn.names[in.b], reg(in.c));
			}
			break;
		case op::get_index: reg(in.a) = lookup_index(reg(in.b), reg(in.c)); break;
		case op::set_index: {
			const value target = reg(in.a);
			const value key = reg(in.b);
			if (target.is_array() && key.is_number()) {
				auto * arr = static_cast<array_object *>(target.as_heap());
				const auto i = static_cast<std::ptrdiff_t>(key.as_number());
				if (i >= 0) {
					if (static_cast<std::size_t>(i) >= arr->items.size()) {
						arr->items.resize(static_cast<std::size_t>(i) + 1, value::undefined());
					}
					arr->items[static_cast<std::size_t>(i)] = reg(in.c);
				}
			} else if (target.is_object()) {
				static_cast<object_object *>(target.as_heap())->set(to_string(key), reg(in.c));
			}
			break;
		}

		case op::closure: {
			const function_proto & target = prog.functions[in.bx()];
			auto * made = allocate<closure_object>(&target);
			// Walk the descriptors the compiler resolved: each upvalue is
			// either a cell sitting in THIS frame's register, or one this
			// frame's own closure already holds. The second case is what
			// carries a capture down through more than one level of nesting.
			made->upvalues.reserve(target.upvalues.size());
			for (const upvalue_desc & up : target.upvalues) {
				if (up.from_parent_local) {
					made->upvalues.push_back(reg(up.index));
				} else if (frame.closure != nullptr && up.index < frame.closure->upvalues.size()) {
					made->upvalues.push_back(frame.closure->upvalues[up.index]);
				} else {
					made->upvalues.push_back(value::undefined());
				}
			}
			reg(in.a) = value::object(made);
			break;
		}

		case op::call:
		case op::call_method:
		case op::call_computed: {
			value callee = reg(in.a);
			value receiver = value::undefined();
			if (in.code == op::call_method) {
				receiver = reg(in.a);
				// Through the SAME lookup as get_prop, so `s.split(...)` and
				// `var f = s.split; f(...)` find the same function.
				callee = lookup_property(receiver, fn.names[in.c]);
			} else if (in.code == op::call_computed) {
				receiver = reg(in.a);
				callee = lookup_index(receiver, reg(in.c));
			}
			const std::size_t arg_base = base + in.a + 1;
			if (callee.is_kind(heap_kind::native)) {
				auto * nat = static_cast<native_object *>(callee.as_heap());
				// COPIED, not spanned into the register stack. A native may call
				// back into script - an event listener dispatching another
				// event - and that grows registers_, which would leave a span
				// into it dangling. One small vector per native call is the
				// price of natives being allowed to re-enter the VM at all.
				std::vector<value> args{registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
				                        registers_.begin() +
				                            static_cast<std::ptrdiff_t>(arg_base + in.b)};
				const value saved_this = current_this_;
				current_this_ = receiver;
				const value produced = nat->fn(*this, args);
				current_this_ = saved_this;
				reg(in.a) = produced;
				break;
			}
			if (!callee.is_kind(heap_kind::function)) {
				raise("attempted to call a non-function");
				break;
			}
			auto * fnobj = static_cast<closure_object *>(callee.as_heap());
			const function_proto & target = *fnobj->proto;
			// The callee's frame starts where its arguments already are, so no
			// copying is needed to pass them.
			const std::size_t new_base = arg_base;
			const std::size_t needed = new_base + target.frame_size + 8u;
			if (registers_.size() < needed) { registers_.resize(needed, value::undefined()); }
			for (std::size_t i = in.b; i < target.param_count; ++i) {
				registers_[new_base + i] = value::undefined(); // missing args
			}
			if (frames_.size() > 512) {
				raise("call stack exhausted");
				break;
			}
			frames_.push_back(call_frame{&target, 0, new_base, in.a, fnobj, receiver, handlers_.size()});
			break;
		}

		case op::ret:
		case op::ret_undef: {
			const value returned = in.code == op::ret ? reg(in.a) : value::undefined();
			const std::uint8_t slot = frame.result_reg;
			// Handlers this frame installed die with it: a `return` out of a
			// try block must not leave its catch reachable from the caller.
			if (handlers_.size() > frame.handler_base) { handlers_.resize(frame.handler_base); }
			frames_.pop_back();
			if (frames_.size() <= stop_depth) { return returned; }
			registers_[frames_.back().base + slot] = returned;
			break;
		}

		case op::type_of: reg(in.a) = string(std::string{type_of(reg(in.b))}); break;

		case op::load_this: reg(in.a) = frame.receiver; break;

		case op::push_handler:
			handlers_.push_back(handler{frames_.size() - 1,
			                            static_cast<std::size_t>(frame.ip) +
			                                static_cast<std::size_t>(in.sbx()),
			                            registers_.size(), in.a});
			break;
		case op::pop_handler:
			if (!handlers_.empty()) { handlers_.pop_back(); }
			break;
		case op::throw_value: {
			thrown_ = reg(in.a);
			if (!unwind_to_handler()) {
				raise("uncaught exception: " + to_string(thrown_));
			}
			break;
		}

		case op::new_cell: reg(in.a) = value::object(allocate<cell_object>(reg(in.a))); break;
		case op::cell_get:
			reg(in.a) = reg(in.b).is_kind(heap_kind::cell)
			                ? static_cast<cell_object *>(reg(in.b).as_heap())->slot
			                : value::undefined();
			break;
		case op::cell_set:
			if (reg(in.a).is_kind(heap_kind::cell)) {
				static_cast<cell_object *>(reg(in.a).as_heap())->slot = reg(in.b);
			}
			break;
		case op::get_upvalue: {
			reg(in.a) = value::undefined();
			if (frame.closure != nullptr && in.b < frame.closure->upvalues.size()) {
				const value cell = frame.closure->upvalues[in.b];
				if (cell.is_kind(heap_kind::cell)) {
					reg(in.a) = static_cast<cell_object *>(cell.as_heap())->slot;
				}
			}
			break;
		}
		case op::set_upvalue: {
			if (frame.closure != nullptr && in.a < frame.closure->upvalues.size()) {
				const value cell = frame.closure->upvalues[in.a];
				if (cell.is_kind(heap_kind::cell)) {
					static_cast<cell_object *>(cell.as_heap())->slot = reg(in.b);
				}
			}
			break;
		}
		case op::halt: return value::undefined();
		}
	}
	return value::undefined();
}

} // namespace ctbrowser::script
