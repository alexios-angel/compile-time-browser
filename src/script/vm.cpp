#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <ctbrowser/script/vm.hpp>

// The VM's implementation.
//
// `run_loop` alone is 15 KB of object code - the whole instruction dispatch -
// and while it lived in the interface every translation unit that imported the
// module emitted its own copy and optimised it again. The class declaration
// stays in :vm; the bodies live here and are compiled once.

namespace ctbrowser::script {

bool context::truthy(value v) {
    if (v.is_boolean()) { return v.as_boolean(); }
    if (v.is_nullish()) { return false; }
    if (v.is_number()) {
        const double d = v.as_number();
        return d != 0 && !std::isnan(d);
    }
    if (v.is_string()) { return !static_cast<string_object *>(v.as_heap())->text.empty(); }
    return true; // every other object is truthy
}

double context::to_number(value v) {
    if (v.is_number()) { return v.as_number(); }
    if (v.is_boolean()) { return v.as_boolean() ? 1 : 0; }
    if (v.is_null()) { return 0; }
    if (v.is_undefined()) { return std::nan(""); }
    if (v.is_string()) {
        const std::string & s = static_cast<string_object *>(v.as_heap())->text;
        try {
            std::size_t consumed = 0;
            const double d = std::stod(s, &consumed);
            while (consumed < s.size() && (s[consumed] == ' ' || s[consumed] == '\t')) {
                ++consumed;
            }
            return consumed == s.size() ? d : std::nan("");
        } catch (...) {
            return s.find_first_not_of(" \t\n\r") == std::string::npos ? 0.0 : std::nan("");
        }
    }
    return std::nan("");
}

std::string context::to_string(value v) {
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

std::string_view context::type_of(value v) {
    if (v.is_undefined()) { return "undefined"; }
    if (v.is_null()) { return "object"; } // the famous wart, preserved
    if (v.is_boolean()) { return "boolean"; }
    if (v.is_number()) { return "number"; }
    if (v.is_string()) { return "string"; }
    if (v.is_callable()) { return "function"; }
    return "object";
}

bool context::loose_equals(value a, value b) {
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

void context::mark_object(heap_object * o) {
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
    case heap_kind::cell: mark(static_cast<cell_object *>(o)->slot); break;
    case heap_kind::function: {
        auto * closure = static_cast<closure_object *>(o);
        // A closure OWNS its upvalue cells. Missing this frees a captured
        // variable while the closure that captured it is still reachable.
        for (const value & up : closure->upvalues) { mark(up); }
        // ...and its own properties, which is where a class keeps its statics
        // and its prototype.
        for (const auto & [name, v] : closure->props) { mark(v); }
        // ...and an arrow's captured `this`, which nothing else can reach.
        mark(closure->captured_this);
        break;
    }
    default: break; // strings and natives own no values
    }
}

void context::mark(value v) {
    if (v.is_heap()) { mark_object(v.as_heap()); }
}

value context::lookup_index(value target, value key) {
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

value context::lookup_property(value target, const std::string & name) const {
    // Own properties first: a page that writes `arr.length = 0` or shadows a
    // method on one object must not be overridden by the prototype.
    if (target.is_object()) {
        // Own properties, then the object's OWN prototype chain (what a class
        // instance uses to find its methods), then the shared table. A depth
        // cap because a page can make the chain cyclic and a lookup must not
        // hang because of it.
        auto * obj = static_cast<object_object *>(target.as_heap());
        for (int depth = 0; obj != nullptr && depth < 64; ++depth) {
            if (value * found = obj->find(name)) { return *found; }
            obj = obj->prototype.is_object()
                      ? static_cast<object_object *>(obj->prototype.as_heap())
                      : nullptr;
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
        return value::undefined();
    }
    if (target.is_kind(heap_kind::function)) {
        if (value * found = static_cast<closure_object *>(target.as_heap())->find(name)) {
            return *found;
        }
    }
    return value::undefined();
}

std::size_t context::collect() {
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
    // The per-function string cache. These are live `value`s held by the
    // context itself and referenced from nowhere else - a sweep without them
    // frees a string literal that a running loop is about to read again.
    for (auto & [proto, cache] : string_cache_) {
        for (auto & [index, v] : cache) { mark(v); }
    }
    // And whatever the embedder holds: every DOM listener, timer callback and
    // element wrapper lives in the bindings, not in any VM structure.
    if (external_roots_) {
        external_roots_([this](value v) { mark(v); });
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

void context::sweep_all() {
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
value context::call(value callable, std::span<const value> args, value this_value) {
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
    frames_.push_back(call_frame{&target, 0, new_base, 0, static_cast<std::uint8_t>(args.size()),
                                 fnobj, this_value, handlers_.size()});
    const value out = run_loop(depth);
    current_this_ = saved;
    // Only shrink back if nothing below is still using the space - a nested
    // call that grew the stack further has already returned by now.
    if (registers_.size() >= new_base) { registers_.resize(new_base); }
    return out;
}

run_result context::run(const program & prog) {
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

value context::execute(const program & prog, const function_proto & entry) {
    registers_.assign(entry.frame_size + 8u, value::undefined());
    frames_.clear();
    frames_.push_back(call_frame{&entry, 0, 0, 0, 0, nullptr, value::undefined(), 0});
    program_ = &prog;
    // Per-frame string interning: a literal in a loop should allocate once,
    // not once per iteration.
    string_cache_.clear();
    return run_loop(0);
}

// The interpreter loop, entered at a frame depth and running until it unwinds
// back to it. `stop_depth` is 0 for the top-level program and the caller's
// depth for a call from C++ - which is what makes call() re-entrant instead of
// a second interpreter.
value context::run_loop(std::size_t stop_depth) {
    const program & prog = *program_;
    auto & string_cache = string_cache_;

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
            auto & cache = string_cache[&fn];
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
        case op::loose_not_equal:
            reg(in.a) = value::boolean(!loose_equals(reg(in.b), reg(in.c)));
            break;

        // `x instanceof C` is true when C.prototype appears anywhere in x's
        // prototype chain - the same chain lookup_property walks.
        case op::instance_of: {
            reg(in.a) = value::boolean(false);
            value target = reg(in.b);
            const value ctor = reg(in.c);
            value wanted = value::undefined();
            if (ctor.is_kind(heap_kind::function)) {
                if (value * p = static_cast<closure_object *>(ctor.as_heap())->find("prototype")) {
                    wanted = *p;
                }
            } else if (ctor.is_object()) {
                if (value * p = static_cast<object_object *>(ctor.as_heap())->find("prototype")) {
                    wanted = *p;
                }
            }
            for (int depth = 0; depth < 64 && target.is_object(); ++depth) {
                target = static_cast<object_object *>(target.as_heap())->prototype;
                if (target.is_object() && wanted.is_object() &&
                    target.as_heap() == wanted.as_heap()) {
                    reg(in.a) = value::boolean(true);
                    break;
                }
            }
            break;
        }
        case op::has_property: {
            const std::string key = to_string(reg(in.b));
            const value target = reg(in.c);
            bool present = false;
            if (target.is_object()) {
                present = static_cast<object_object *>(target.as_heap())->find(key) != nullptr;
            } else if (target.is_array()) {
                // `0 in [7, 8]` asks about an INDEX, so the key has to be a
                // whole number and the whole key - "1x" is not index 1.
                std::size_t index = 0;
                const char * first = key.data();
                const char * last = first + key.size();
                const auto [stopped, failed] = std::from_chars(first, last, index);
                present = failed == std::errc{} && stopped == last &&
                          index < static_cast<array_object *>(target.as_heap())->items.size();
            }
            reg(in.a) = value::boolean(present);
            break;
        }

        // ToInt32 / ToUint32 first: `-1 >>> 0` is 4294967295, not -1, and
        // `2.7 | 0` is 2. Doing this on the raw double gets both wrong.
        case op::bit_and:
            reg(in.a) = value::number(to_int32(reg(in.b)) & to_int32(reg(in.c)));
            break;
        case op::bit_or:
            reg(in.a) = value::number(to_int32(reg(in.b)) | to_int32(reg(in.c)));
            break;
        case op::bit_xor:
            reg(in.a) = value::number(to_int32(reg(in.b)) ^ to_int32(reg(in.c)));
            break;
        case op::shl:
            reg(in.a) = value::number(static_cast<std::int32_t>(
                static_cast<std::uint32_t>(to_int32(reg(in.b))) << (to_uint32(reg(in.c)) & 31U)));
            break;
        case op::shr:
            reg(in.a) = value::number(to_int32(reg(in.b)) >> (to_uint32(reg(in.c)) & 31U));
            break;
        case op::ushr:
            reg(in.a) = value::number(
                static_cast<double>(to_uint32(reg(in.b)) >> (to_uint32(reg(in.c)) & 31U)));
            break;
        case op::bit_not: reg(in.a) = value::number(~to_int32(reg(in.b))); break;

        case op::copy_props: {
            if (!reg(in.a).is_object()) { break; }
            auto * target = static_cast<object_object *>(reg(in.a).as_heap());
            if (reg(in.b).is_object()) {
                // A copy of the source's entries first: `set` can reallocate
                // the target's storage, and target and source may be the same
                // object.
                const std::vector<std::pair<std::string, value>> entries =
                    static_cast<object_object *>(reg(in.b).as_heap())->props;
                for (const auto & [name, item] : entries) { target->set(name, item); }
            } else if (reg(in.b).is_array()) {
                const std::vector<value> items =
                    static_cast<array_object *>(reg(in.b).as_heap())->items;
                for (std::size_t i = 0; i < items.size(); ++i) {
                    target->set(std::to_string(i), items[i]);
                }
            }
            break;
        }
        case op::delete_prop:
            if (reg(in.a).is_object()) {
                (void)static_cast<object_object *>(reg(in.a).as_heap())->erase(fn.names[in.b]);
            }
            break;
        case op::delete_index:
            if (reg(in.a).is_object()) {
                (void)static_cast<object_object *>(reg(in.a).as_heap())
                    ->erase(to_string(reg(in.b)));
            }
            break;

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

        case op::jump:
            frame.ip = static_cast<std::size_t>(static_cast<std::int64_t>(frame.ip) + in.sbx());
            break;
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
        case op::jump_if_not_nullish:
            if (!reg(in.a).is_nullish()) {
                frame.ip = static_cast<std::size_t>(static_cast<std::int64_t>(frame.ip) + in.sbx());
            }
            break;
        case op::jump_if_defined:
            if (!reg(in.a).is_undefined()) {
                frame.ip = static_cast<std::size_t>(static_cast<std::int64_t>(frame.ip) + in.sbx());
            }
            break;

        case op::gather_rest: {
            // The arguments past the declared parameters. They are still in
            // this frame's registers - the caller wrote them there and the
            // callee's base IS the argument base - so this reads them in place.
            value out = make_array();
            auto * rest = static_cast<array_object *>(out.as_heap());
            for (std::size_t i = in.b; i < frame.argc; ++i) {
                rest->items.push_back(registers_[base + i]);
            }
            reg(in.a) = out;
            break;
        }

        case op::new_object: reg(in.a) = make_object(); break;
        case op::new_array: reg(in.a) = make_array(); break;
        case op::append:
            if (reg(in.a).is_array()) {
                static_cast<array_object *>(reg(in.a).as_heap())->items.push_back(reg(in.b));
            }
            break;
        case op::get_prop: reg(in.a) = lookup_property(reg(in.b), fn.names[in.c]); break;
        case op::set_prop:
            if (reg(in.a).is_object()) {
                static_cast<object_object *>(reg(in.a).as_heap())->set(fn.names[in.b], reg(in.c));
            } else if (reg(in.a).is_kind(heap_kind::function)) {
                static_cast<closure_object *>(reg(in.a).as_heap())->set(fn.names[in.b], reg(in.c));
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
            // An arrow's `this` is decided HERE, where it is written, not where
            // it is called. Reading the effective receiver rather than the raw
            // one is what makes an arrow inside an arrow inside a method still
            // see the method's object.
            if (target.is_arrow) { made->captured_this = effective_this(frame); }
            reg(in.a) = value::object(made);
            break;
        }

        case op::call:
        case op::call_method:
        case op::call_computed:
        case op::call_receiver: {
            value callee = reg(in.a);
            value receiver = value::undefined();
            if (in.code == op::call_receiver) {
                // The callee was resolved elsewhere (up the prototype chain, for
                // `super`) and the receiver is passed explicitly.
                receiver = reg(in.c);
            } else if (in.code == op::call_method) {
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
            frames_.push_back(
                call_frame{&target, 0, new_base, in.a, in.b, fnobj, receiver, handlers_.size()});
            break;
        }

        case op::ret:
        case op::ret_undef: {
            value returned = in.code == op::ret ? reg(in.a) : value::undefined();
            if (frame.constructing && !returned.is_object()) { returned = frame.receiver; }
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

        case op::load_this: reg(in.a) = effective_this(frame); break;

        case op::own_keys: {
            // The own property names of an object, as an array. `for (k in o)`
            // compiles to a for-of over this, which keeps one iteration
            // mechanism instead of two.
            value out = make_array();
            auto * keys = static_cast<array_object *>(out.as_heap());
            if (reg(in.b).is_object()) {
                for (const auto & [name, item] :
                     static_cast<object_object *>(reg(in.b).as_heap())->props) {
                    keys->items.push_back(string(name));
                }
            } else if (reg(in.b).is_array()) {
                const std::size_t n =
                    static_cast<array_object *>(reg(in.b).as_heap())->items.size();
                for (std::size_t i = 0; i < n; ++i) {
                    keys->items.push_back(string(std::to_string(i)));
                }
            }
            reg(in.a) = out;
            break;
        }

        case op::wrap_promise:
            // Already a promise (`return somePromise` inside an async function)
            // stays as it is rather than nesting.
            if (!(reg(in.a).is_object() &&
                  static_cast<object_object *>(reg(in.a).as_heap())->find("__value") != nullptr)) {
                reg(in.a) = make_promise(reg(in.a), false);
            }
            break;
        case op::await_value: {
            // A settled promise carries its value in `__value`; anything else
            // awaits to itself. A REJECTED promise throws, which is what makes
            // `try { await f() } catch` work.
            const value awaited = reg(in.b);
            reg(in.a) = awaited;
            if (awaited.is_object()) {
                auto * obj = static_cast<object_object *>(awaited.as_heap());
                if (value * state = obj->find("__rejected"); state != nullptr && truthy(*state)) {
                    thrown_ = obj->find("__value") != nullptr ? *obj->find("__value")
                                                              : value::undefined();
                    if (!unwind_to_handler()) { raise("uncaught rejection"); }
                    break;
                }
                if (value * settled = obj->find("__value")) { reg(in.a) = *settled; }
            }
            break;
        }

        case op::set_proto:
            if (reg(in.a).is_object()) {
                static_cast<object_object *>(reg(in.a).as_heap())->prototype = reg(in.b);
            }
            break;

        case op::get_proto:
            reg(in.a) = reg(in.b).is_object()
                            ? static_cast<object_object *>(reg(in.b).as_heap())->prototype
                            : value::undefined();
            break;

        // `super` has to start its lookup at the prototype ABOVE the class the
        // running method was written in - not above `this`, which in a
        // three-deep hierarchy is a different object and would call the method
        // again forever. So each method carries its home object.
        case op::load_home: {
            closure_object * running = frames_.empty() ? nullptr : frames_.back().closure;
            reg(in.a) = value::undefined();
            if (running != nullptr) {
                if (value * home = running->find("__home")) { reg(in.a) = *home; }
            }
            break;
        }

        case op::construct: {
            const value callee = reg(in.a);
            // The instance's prototype comes from the constructor's own
            // `prototype` property, which is what makes a method defined on the
            // class reachable from every instance.
            auto * instance = allocate<object_object>();
            if (callee.is_object()) {
                if (value * proto =
                        static_cast<object_object *>(callee.as_heap())->find("prototype")) {
                    instance->prototype = *proto;
                }
            } else if (callee.is_kind(heap_kind::function)) {
                if (value * proto =
                        static_cast<closure_object *>(callee.as_heap())->find("prototype")) {
                    instance->prototype = *proto;
                }
            }
            const value self = value::object(instance);
            const std::size_t arg_base = base + in.a + 1;

            if (callee.is_kind(heap_kind::native)) {
                auto * nat = static_cast<native_object *>(callee.as_heap());
                std::vector<value> args{registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                                        registers_.begin() +
                                            static_cast<std::ptrdiff_t>(arg_base + in.b)};
                const value saved = current_this_;
                current_this_ = self;
                const value produced = nat->fn(*this, args);
                current_this_ = saved;
                reg(in.a) = produced.is_object() ? produced : self;
                break;
            }
            if (!callee.is_kind(heap_kind::function)) {
                raise("attempted to construct a non-function");
                break;
            }
            auto * fnobj = static_cast<closure_object *>(callee.as_heap());
            const function_proto & target = *fnobj->proto;
            const std::size_t new_base = arg_base;
            const std::size_t needed = new_base + target.frame_size + 8u;
            if (registers_.size() < needed) { registers_.resize(needed, value::undefined()); }
            for (std::size_t i = in.b; i < target.param_count; ++i) {
                registers_[new_base + i] = value::undefined();
            }
            if (frames_.size() > 512) {
                raise("call stack exhausted");
                break;
            }
            call_frame fresh{&target, 0, new_base, in.a, in.b, fnobj, self, handlers_.size()};
            fresh.constructing = true;
            frames_.push_back(fresh);
            break;
        }

        case op::push_handler:
            handlers_.push_back(
                handler{frames_.size() - 1,
                        static_cast<std::size_t>(frame.ip) + static_cast<std::size_t>(in.sbx()),
                        registers_.size(), in.a});
            break;
        case op::pop_handler:
            if (!handlers_.empty()) { handlers_.pop_back(); }
            break;
        case op::throw_value: {
            thrown_ = reg(in.a);
            if (!unwind_to_handler()) { raise("uncaught exception: " + to_string(thrown_)); }
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
