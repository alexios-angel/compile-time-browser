module;
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

export module ctbrowser.script:vm;

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

struct closure_object final : heap_object {
	const function_proto * proto = nullptr;
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
	run_result run(const program & prog);

	// --- conversions (ECMA-262 shaped, and shared with the bindings) -------
	[[nodiscard]] static bool truthy(value v);
	[[nodiscard]] static double to_number(value v);
	[[nodiscard]] std::string to_string(value v);
	[[nodiscard]] static std::string_view type_of(value v);
	[[nodiscard]] static bool loose_equals(value a, value b);

	// --- gc ----------------------------------------------------------------
	std::size_t collect();
	[[nodiscard]] std::size_t live_objects() const noexcept { return live_objects_; }

private:
	struct call_frame {
		const function_proto * proto = nullptr;
		std::size_t ip = 0;
		std::size_t base = 0; // index into registers_ of this frame's r0
		std::uint8_t result_reg = 0;
	};

	[[nodiscard]] value execute(const program & prog, const function_proto & entry);
	void raise(std::string message) {
		if (!failed_) {
			failed_ = true;
			error_ = std::move(message);
		}
	}

	void mark(value v);
	void mark_object(heap_object * o);
	void sweep_all();

	boost::unordered_flat_map<std::string, value> globals_;
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
	default: break; // strings, closures and natives own no values
	}
}

inline void context::mark(value v) {
	if (v.is_heap()) { mark_object(v.as_heap()); }
}

inline std::size_t context::collect() {
	// Precise roots: everything reachable is reachable from exactly these.
	for (const auto & [name, v] : globals_) { mark(v); }
	for (const value & v : registers_) { mark(v); }

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
	frames_.push_back(call_frame{&entry, 0, 0, 0});

	// Per-frame string interning: a literal in a loop should allocate once,
	// not once per iteration.
	std::vector<boost::unordered_flat_map<std::uint16_t, value>> string_cache;
	string_cache.resize(prog.functions.size());

	while (!frames_.empty() && !failed_) {
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
		case op::get_prop: {
			const value target = reg(in.b);
			const std::string & name = fn.names[in.c];
			reg(in.a) = value::undefined();
			if (target.is_object()) {
				if (value * found = static_cast<object_object *>(target.as_heap())->find(name)) {
					reg(in.a) = *found;
				}
			} else if (target.is_array() && name == "length") {
				reg(in.a) = value::number(
				    static_cast<double>(static_cast<array_object *>(target.as_heap())->items.size()));
			} else if (target.is_string() && name == "length") {
				reg(in.a) = value::number(
				    static_cast<double>(static_cast<string_object *>(target.as_heap())->text.size()));
			}
			break;
		}
		case op::set_prop:
			if (reg(in.a).is_object()) {
				static_cast<object_object *>(reg(in.a).as_heap())->set(fn.names[in.b], reg(in.c));
			}
			break;
		case op::get_index: {
			const value target = reg(in.b);
			const value key = reg(in.c);
			reg(in.a) = value::undefined();
			if (target.is_array() && key.is_number()) {
				auto * arr = static_cast<array_object *>(target.as_heap());
				const auto i = static_cast<std::ptrdiff_t>(key.as_number());
				if (i >= 0 && static_cast<std::size_t>(i) < arr->items.size()) {
					reg(in.a) = arr->items[static_cast<std::size_t>(i)];
				}
			} else if (target.is_object()) {
				if (value * found = static_cast<object_object *>(target.as_heap())->find(to_string(key))) {
					reg(in.a) = *found;
				}
			} else if (target.is_string() && key.is_number()) {
				const std::string & s = static_cast<string_object *>(target.as_heap())->text;
				const auto i = static_cast<std::size_t>(key.as_number());
				if (i < s.size()) { reg(in.a) = string(std::string{s[i]}); }
			}
			break;
		}
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

		case op::closure:
			reg(in.a) = value::object(allocate<closure_object>(&prog.functions[in.bx()]));
			break;

		case op::call:
		case op::call_method: {
			value callee = reg(in.a);
			if (in.code == op::call_method) {
				const value receiver = reg(in.a);
				callee = value::undefined();
				if (receiver.is_object()) {
					if (value * found =
					        static_cast<object_object *>(receiver.as_heap())->find(fn.names[in.c])) {
						callee = *found;
					}
				}
			}
			const std::size_t arg_base = base + in.a + 1;
			if (callee.is_kind(heap_kind::native)) {
				auto * nat = static_cast<native_object *>(callee.as_heap());
				std::span<value> args{registers_.data() + arg_base, in.b};
				reg(in.a) = nat->fn(*this, args);
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
			frames_.push_back(call_frame{&target, 0, new_base, in.a});
			break;
		}

		case op::ret:
		case op::ret_undef: {
			const value returned = in.code == op::ret ? reg(in.a) : value::undefined();
			const std::uint8_t slot = frame.result_reg;
			frames_.pop_back();
			if (frames_.empty()) { return returned; }
			registers_[frames_.back().base + slot] = returned;
			break;
		}

		case op::type_of: reg(in.a) = string(std::string{type_of(reg(in.b))}); break;
		case op::get_upvalue:
		case op::close_over:
			raise("upvalues are not implemented in this VM subset");
			break;
		case op::halt: return value::undefined();
		}
	}
	return value::undefined();
}

} // namespace ctbrowser::script
