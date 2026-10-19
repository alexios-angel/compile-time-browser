//===- ctbrowser/lib/Script/compile/frames.cpp - Frames and scopes --------===//
//
// compiler_impl frames, registers and scopes: the register allocator, the
// scope stack, locals and upvalues, and the module import/export bindings.
// The three template members stay in compiler_impl.hpp, where the class is
// declared, because two of them are called from more than one file. Section
// numbers are ECMA-262's.
//
//===----------------------------------------------------------------------===//

#include "compiler_impl.hpp"

#include <boost/scope/scope_exit.hpp>

namespace ctbrowser::script::detail {

compiler_impl::frame & compiler_impl::fn() {
    return frames_.back();
}

function_proto & compiler_impl::proto() {
    return out_.functions[fn().proto];
}

std::uint32_t compiler_impl::new_proto([[maybe_unused]] std::uint32_t at_offset) {
    const auto index = static_cast<std::uint32_t>(out_.functions.size());
    out_.functions.emplace_back();
#if CTBROWSER_SCRIPT_DEBUG_NAMES
    out_.functions.back().debug_offsets = true;
    out_.functions.back().emit_offset = at_offset == function_proto::no_offset ? 0u : at_offset;
#endif
    return index;
}

std::uint16_t compiler_impl::alloc_reg() {
    const std::uint32_t r = fn().next_reg++;
    if (fn().next_reg > fn().high_water) { fn().high_water = fn().next_reg; }
    return static_cast<std::uint16_t>(r);
}

void compiler_impl::release_to(std::uint32_t mark) {
    fn().next_reg = mark;
}

std::uint32_t compiler_impl::reg_mark() const {
    return frames_.back().next_reg;
}

void compiler_impl::push_scope() {
    fn().scope_marks.push_back(fn().locals.size());
}

void compiler_impl::pop_scope() {
    const std::size_t mark = fn().scope_marks.back();
    fn().scope_marks.pop_back();
    shrink_locals(fn(), mark);
}

void compiler_impl::add_local(frame & f, local l) {
#if CTBROWSER_SCRIPT_DEBUG_NAMES
    // The debug-name table is written here because this is the only place a
    // local is created. `finish_frame` cannot do it: `frame::locals` is a
    // stack, and by then `pop_scope` has removed every local except those in
    // the outermost scope.
    function_proto & fp = out_.functions[f.proto];
    const auto pc = static_cast<std::uint32_t>(fp.code.size());
    l.debug_slot = static_cast<std::uint32_t>(fp.locals.size());
    fp.locals.push_back(local_desc{l.name, l.reg, pc, pc, l.boxed});
#endif
    f.local_index[l.name].push_back(static_cast<std::uint32_t>(f.locals.size()));
    f.locals.push_back(std::move(l));
}

void compiler_impl::close_local([[maybe_unused]] const frame & f,
                                [[maybe_unused]] const local & l) {
#if CTBROWSER_SCRIPT_DEBUG_NAMES
    if (l.debug_slot == local::no_slot) { return; }
    function_proto & fp = out_.functions[f.proto];
    local_desc & d = fp.locals[l.debug_slot];
    d.last_pc = static_cast<std::uint32_t>(fp.code.size());
    // Record boxedness at the end, not at the declaration. `mark_captured`,
    // `bind_export` and `bind_imports` all set `boxed` on a local that was
    // declared already, so the value copied at `add_local` is a guess and this
    // one is the answer.
    d.boxed = l.boxed;
#endif
}

void compiler_impl::shrink_locals(frame & f, std::size_t mark) {
    for (std::size_t i = f.locals.size(); i-- > mark;) {
        close_local(f, f.locals[i]);
        const auto it = f.local_index.find(std::string_view{f.locals[i].name});
        if (it == f.local_index.end()) { continue; }
        it->second.pop_back();
        if (it->second.empty()) { f.local_index.erase(it); }
    }
    f.locals.resize(mark);
}

std::uint32_t compiler_impl::offset_of([[maybe_unused]] std::int32_t idx) const {
#if CTBROWSER_SCRIPT_DEBUG_NAMES
    // A sub-AST has no offset. A template literal's interpolation and
    // `compile_owned_expr`'s synthesised text are parsed from other buffers, so
    // a lexeme in one is not an offset into the program at all. Keeping the
    // enclosing statement's position is right as well as safe: that is where
    // the template was written.
    if (idx < 0 || current_ast_ != &ast_ || source_view_.empty()) {
        return function_proto::no_offset;
    }
    const vp::node & n = at(idx);
    if (n.begin != 0) { return n.begin; } // A function; see node::begin.
    const char * const base = source_view_.data();
    const char * const text = n.text.data();
    // Bounds-checked with std::less, not with `<`. Comparing pointers into
    // different objects with a relational operator is unspecified, and this is
    // exactly that comparison: the whole question is whether `text` points into
    // this buffer or into some other one.
    if (text == nullptr || std::less<const void *>{}(text, base) ||
        std::less<const void *>{}(base + source_view_.size(), text)) {
        return function_proto::no_offset;
    }
    return static_cast<std::uint32_t>(text - base);
#else
    return function_proto::no_offset;
#endif
}

std::uint16_t compiler_impl::declare_local(std::string name) {
    const std::uint16_t r = alloc_reg();
    const bool boxed = is_captured(name);
    add_local(fn(), local{std::move(name), r, boxed});
    return r;
}

void compiler_impl::bind_export(const std::string & name, std::uint16_t reg) {
    for (local & each : fn().locals) {
        if (each.reg == reg) { each.boxed = true; }
    }
    proto().emit(instruction::with_bx(op::bind_export, reg, name_operand(name)));
    if (std::ranges::find(out_.exports, name) == out_.exports.end()) {
        out_.exports.push_back(name);
    }
}

void compiler_impl::bind_imports(std::int32_t idx) {
    const vp::node & n = at(idx);
    if (n.kind != vp::nk::import_decl) { return; }
    // The specifier is recorded for the loader, which walks the graph without
    // re-parsing anything. It is decoded, because the parser hands over the
    // token as written, quotes included; the loader looks modules up by the
    // unquoted specifier.
    const std::string specifier = decode_string_literal(n.text);
    if (std::ranges::find(out_.imports, specifier) == out_.imports.end()) {
        out_.imports.push_back(specifier);
    }
    // `import defer * as ns` defers the module's evaluation - unless the
    // same module is imported eagerly somewhere in this one, in which case
    // the eager import runs it anyway (16.2.2: a deferred request applies
    // only to itself).
    const bool deferred = kids(n).size() == 1 && at(kids(n).front()).c == 3;
    const auto listed = std::ranges::find(out_.deferred_imports, specifier);
    if (deferred && listed == out_.deferred_imports.end()) {
        out_.deferred_imports.push_back(specifier);
    } else if (!deferred && listed != out_.deferred_imports.end()) {
        out_.deferred_imports.erase(listed);
    }
    const std::uint16_t from = name_operand(specifier);
    for (const std::int32_t spec_index : kids(n)) {
        const vp::node & spec = at(spec_index);
        // `spec.c`: 0 named, 1 default, 2 namespace, 3 deferred namespace. A
        // deferred namespace is an ordinary binding holding an object, made by
        // a hidden native (import_defer_name), that evaluates the module on
        // first read; it compiles to a plain native call, so no new opcode is
        // needed.
        if (spec.c == 2 || spec.c == 3) {
            // `import * as ns`: one binding, holding the exporter's
            // namespace object. Not a cell: the namespace is itself the
            // live thing, because each of its properties reads a cell.
            const int hoisted_ns = find_local(spec.text);
            if (hoisted_ns < 0) {
                fail("`import * as " + std::string{spec.text} +
                     "` was not hoisted - see predeclare_locals");
                return;
            }
            // A namespace binding captured by a closure was hoisted boxed, so
            // write through the cell; writing the register directly would
            // discard the cell and the closure would read undefined.
            bool boxed = false;
            for (const local & each : fn().locals) {
                if (each.name == spec.text) { boxed = each.boxed; }
            }
            const std::uint16_t temp = alloc_reg();
            if (spec.c == 3) {
                const std::uint16_t key = alloc_reg();
                emit_string(key, specifier);
                emit_iterator_native(import_defer_name, temp, key);
            } else {
                proto().emit(instruction{op::load_namespace, temp, from});
            }
            if (boxed) {
                proto().emit(
                    instruction{op::cell_set, static_cast<std::uint16_t>(hoisted_ns), temp});
            } else {
                proto().emit(instruction{op::move, static_cast<std::uint16_t>(hoisted_ns), temp});
            }
            continue;
        }
        // The name the exporter knows it by: the original when renamed,
        // otherwise the same as the local one. `default` for a default
        // import, which is the name the specification gives it.
        const std::string exported = spec.c == 1   ? std::string{"default"}
                                     : spec.a >= 0 ? std::string{at(spec.a).text}
                                                   : std::string{spec.text};
        const std::uint16_t what = name_operand(exported);
        const int hoisted = find_local(spec.text);
        if (hoisted < 0) {
            fail("`import` of `" + std::string{spec.text} +
                 "` was not hoisted - see predeclare_locals");
            return;
        }
        const auto r = static_cast<std::uint16_t>(hoisted);
        proto().emit(instruction{op::load_import, r, what, from});
        // Always boxed, whether or not this module captures it: every read
        // has to go through the exporter's cell to see the writes that make
        // the binding live. And no `new_cell` - load_import has already put
        // the exporter's box here, and boxing a box would leave the importer
        // reading a cell containing a cell.
        for (local & each : fn().locals) {
            if (each.name == spec.text) { each.boxed = true; }
        }
    }
}

void compiler_impl::collect_reexports(std::int32_t idx) {
    const vp::node & n = at(idx);
    if (n.kind != vp::nk::export_decl || n.text.empty()) { return; }
    const std::string specifier = decode_string_literal(n.text);
    if (std::ranges::find(out_.imports, specifier) == out_.imports.end()) {
        out_.imports.push_back(specifier);
    }
    if (n.c == 2) {
        if (kids(n).empty()) {
            // `export * from './m.js'`: every name it exports.
            out_.reexports.push_back(program::reexport{"", "", specifier});
            return;
        }
        // `export * as ns from './m.js'` (16.2.2.3: an ExportEntry with
        // [[ImportName]] all and no local binding): one export, holding the
        // namespace. It is compiled as `import * as <hidden> from './m.js';
        // export { <hidden> as ns }`. The hidden local is not an identifier,
        // so nothing in the module can name it, and the loader wires nothing:
        // the cell is this module's own, adopted at entry and written with
        // the namespace before the first statement runs.
        const vp::node & spec = at(kids(n).front());
        const std::string hidden = "*namespace*:" + std::string{spec.text};
        const std::uint16_t r = declare_local(hidden);
        proto().emit(instruction{op::load_undef, r});
        bind_export(std::string{spec.text}, r);
        const std::uint32_t mark = reg_mark();
        const std::uint16_t ns = alloc_reg();
        proto().emit(instruction{op::load_namespace, ns, name_operand(specifier)});
        proto().emit(instruction{op::cell_set, r, ns});
        release_to(mark);
        return;
    }
    for (const std::int32_t spec_index : kids(n)) {
        const vp::node & spec = at(spec_index);
        out_.reexports.push_back(
            program::reexport{spec.a >= 0 ? std::string{at(spec.a).text} : std::string{spec.text},
                              std::string{spec.text}, specifier});
    }
}

void compiler_impl::export_bindings(std::int32_t outer,
                                    std::vector<std::pair<std::string, std::string>> & out) {
    const vp::node & n = at(outer);
    if (n.kind != vp::nk::export_decl || !n.text.empty() || n.c == 2) { return; }
    if (n.c == 1) {
        // A named default exports its own binding (16.2.3 ExportEntries: the
        // [[LocalName]] of `export default function f() {}` is "f"), so
        // `f = 2` inside the module is what an importer's `default` reads.
        // Only an anonymous one has the hidden `*default*`.
        const std::int32_t named = default_export_name(outer);
        out.emplace_back(named >= 0 ? std::string{at(named).text} : std::string{default_binding},
                         "default");
        return;
    }
    if (n.a >= 0) {
        const vp::node & declared = at(n.a);
        if (declared.kind == vp::nk::var_decl) {
            for (const std::int32_t d : kids(declared)) {
                if (at(d).b >= 0) {
                    // `export const [a, {b}] = ...`: every name the pattern
                    // binds is an export (16.2.3.2 BoundNames).
                    std::vector<std::string> names;
                    pattern_names(at(d).b, names);
                    for (std::string & name : names) { out.emplace_back(name, name); }
                } else if (!at(d).text.empty()) {
                    out.emplace_back(std::string{at(d).text}, std::string{at(d).text});
                }
            }
        } else if (!declared.text.empty()) {
            out.emplace_back(std::string{declared.text}, std::string{declared.text});
        }
        return;
    }
    for (const std::int32_t spec_index : kids(n)) {
        const vp::node & spec = at(spec_index);
        out.emplace_back(std::string{spec.text},
                         spec.a >= 0 ? std::string{at(spec.a).text} : std::string{spec.text});
    }
}

void compiler_impl::declare_local_at(std::string name, std::uint16_t reg) {
    const bool boxed = is_captured(name);
    if (boxed) { proto().emit(instruction{op::new_cell, reg}); }
    add_local(fn(), local{std::move(name), reg, boxed});
}

int compiler_impl::find_local(std::string_view name) const {
    const frame & f = frames_.back();
    for (std::size_t i = f.locals.size(); i-- > 0;) {
        if (f.locals[i].name == name) { return f.locals[i].reg; }
    }
    return -1;
}

compiler_impl::local * compiler_impl::find_local_in_current_scope(std::string_view name) {
    frame & f = frames_.back();
    const std::size_t from = f.scope_marks.empty() ? 0 : f.scope_marks.back();
    const auto it = f.local_index.find(name);
    if (it == f.local_index.end()) { return nullptr; }
    const std::uint32_t at = it->second.back();
    return at >= from ? &f.locals[at] : nullptr;
}

compiler_impl::local * compiler_impl::find_local_entry(frame & f, std::string_view name) {
    const auto it = f.local_index.find(name);
    return it == f.local_index.end() ? nullptr : &f.locals[it->second.back()];
}

int compiler_impl::resolve_upvalue(std::size_t level, std::string_view name) {
    if (level == 0) {
        // A direct eval's program: its top level is closed over the caller's
        // bindings, which it holds as upvalues from the start (compile_program).
        if (eval_outer_names_.empty()) { return -1; }
        const auto it = frames_[0].upvalue_index.find(name);
        return it == frames_[0].upvalue_index.end() ? -1 : static_cast<int>(it->second);
    }
    frame & f = frames_[level];
    // A hash lookup, not a scan of upvalue_names: this runs once per frame
    // level for every identifier mention, and the scan was 8.65% of compiling
    // Babylon.js.
    if (const auto it = f.upvalue_index.find(name); it != f.upvalue_index.end()) {
        return static_cast<int>(it->second);
    }
    frame & parent = frames_[level - 1];
    // Frame 0 is the script, and its `var`s are globals rather than locals
    // (see compile_stmt's var_decl case in statements/dispatch.cpp), so the
    // only locals it ever holds are the ones a construct makes for itself: a
    // for..of item, a catch parameter. Those must be capturable or
    // `for (const x of xs) fns.push(() => x)` closes over nothing at the top
    // level.
    if (local * l = find_local_entry(parent, name)) {
        l->boxed = true; // Captured, so it must be a cell.
        return add_upvalue(level, name, upvalue_desc{true, l->reg});
    }
    const int inherited = resolve_upvalue(level - 1, name);
    if (inherited < 0) { return -1; }
    return add_upvalue(level, name, upvalue_desc{false, static_cast<std::uint16_t>(inherited)});
}

std::pair<std::size_t, std::size_t> compiler_impl::binding_position(std::string_view name) {
    // Innermost frame first, which is the order resolve_upvalue settles on:
    // an enclosing frame's locals do not change while a nested function is
    // being compiled, so the local found here is the one it captures.
    for (std::size_t level = frames_.size(); level-- > 0;) {
        frame & f = frames_[level];
        if (const local * l = find_local_entry(f, name)) {
            return {level, static_cast<std::size_t>(l - f.locals.data())};
        }
    }
    return {no_binding, 0};
}

immutability compiler_impl::binding_immutability(std::string_view name) {
    const auto [level, position] = binding_position(name);
    for (const class_binding & c : class_bindings_) {
        if (c.level == level && c.position == position && c.name == name) {
            return immutability::strict;
        }
    }
    if (level != no_binding) { return frames_[level].locals[position].readonly; }
    // A direct eval's program: the caller's binding, as the caller declared it.
    if (!eval_outer_names_.empty()) {
        if (const auto it = frames_[0].upvalue_index.find(name);
            it != frames_[0].upvalue_index.end()) {
            return it->second < eval_outer_readonly_.size() ? eval_outer_readonly_[it->second]
                                                            : immutability::none;
        }
    }
    return std::ranges::binary_search(script_consts_, name) ? immutability::strict
                                                            : immutability::none;
}

bool compiler_impl::refuse_immutable_write(immutability readonly, std::string_view global_name) {
    if (readonly == immutability::none) { return false; }
    if (!global_name.empty() && binding_position(global_name).first == no_binding &&
        std::ranges::find(eval_outer_names_, global_name) == eval_outer_names_.end()) {
        // SetMutableBinding checks initialization before immutability, even
        // for a closure compiled with its global const declaration.
        const std::uint32_t mark = reg_mark();
        const std::uint16_t ready = alloc_reg();
        const std::string name{global_name};
        proto().emit(instruction::with_bx(op::get_global, ready, name_operand(name)));
        release_to(mark);
    }
    if (readonly == immutability::strict || fn().is_strict) {
        emit_throw("TypeError", "Assignment to constant variable.");
    }
    return true;
}

immutability compiler_impl::declared_immutability(const vp::node & decl) {
    return decl.text == "const" || is_using_decl(decl) ? immutability::strict : immutability::none;
}

int compiler_impl::add_upvalue(std::size_t level, std::string_view name, upvalue_desc desc) {
    frame & f = frames_[level];
    function_proto & p = out_.functions[f.proto];
    p.upvalues.push_back(desc);
    f.upvalue_names.emplace_back(name);
    // Kept in step here rather than anywhere else: this is the only place
    // upvalue_names grows, and it never shrinks within a frame.
    f.upvalue_index.emplace(std::string{name}, static_cast<std::uint32_t>(p.upvalues.size() - 1));
    return static_cast<int>(p.upvalues.size() - 1);
}

void compiler_impl::fail(std::string message) {
    if (out_.ok) {
        out_.ok = false;
        out_.error = std::move(message);
    }
}

void compiler_impl::compile_parameter_prologue(std::span<const std::int32_t> params) {
    for (std::size_t i = 0; i < params.size(); ++i) {
        const vp::node & p = at(params[i]);
        if (p.d == 1) {
            proto().emit(instruction{op::gather_rest, static_cast<std::uint16_t>(i),
                                     static_cast<std::uint16_t>(i)});
        }
    }
    if (base_fields_pending_) {
        // A base class constructor: its instance fields, before the
        // parameter defaults and the body (see base_fields_pending_), and
        // after the rest gather, since the call's temporaries take the
        // registers past the declared parameters, where the extra arguments
        // still sit until gather_rest has read them. Otherwise
        // `class V { values = []; constructor(...args) {} }` gets an empty
        // `args`.
        base_fields_pending_ = false;
        emit_init_fields_at_entry();
    }
    // Box the captured parameters here, between the rest gather (a raw write)
    // and the defaults (which read earlier parameters by name, i.e. through
    // the cell). See compile_function_body.
    //
    // Parameter i is local i: they are declared first, in order, into a fresh
    // frame. Only the parameters are boxed here; a pattern name is declared
    // and boxed below as it binds, and boxing it twice would wrap a cell in a
    // cell.
    std::vector<bool> boxed(params.size(), false);
    for (std::size_t i = 0; i < params.size(); ++i) {
        boxed[i] = fn().locals[i].boxed;
        if (boxed[i]) { proto().emit(instruction{op::new_cell, static_cast<std::uint16_t>(i)}); }
    }
    for (std::size_t i = 0; i < params.size(); ++i) {
        const vp::node & p = at(params[i]);
        if (p.d == 1 || p.a < 0) { continue; }
        const auto slot = static_cast<std::uint16_t>(i);
        const std::uint32_t mark = reg_mark();
        // This parameter and every one after it are in their TDZ while the
        // default runs; see tdz_names_.
        tdz_names_.clear();
        for (std::size_t j = i; j < params.size(); ++j) {
            if (at(params[j]).b >= 0) {
                pattern_names(at(params[j]).b, tdz_names_);
            } else {
                tdz_names_.emplace_back(at(params[j]).text);
            }
        }
        tdz_frame_ = frames_.size() - 1;
        const boost::scope::scope_exit leaving{[&]() noexcept {
            tdz_names_.clear();
            tdz_frame_ = static_cast<std::size_t>(-1);
        }};
        if (boxed[i]) {
            // `jump_if_defined` on the cell would always jump: test the value.
            const std::uint16_t current = alloc_reg();
            proto().emit(instruction{op::cell_get, current, slot});
            const std::size_t skip = proto().emit(instruction{op::jump_if_defined, current});
            compile_named_expr(p.a, current, p.b >= 0 ? "" : p.text);
            proto().emit(instruction{op::cell_set, slot, current});
            patch_here(skip);
        } else {
            const std::size_t skip = proto().emit(instruction{op::jump_if_defined, slot});
            compile_named_expr(p.a, slot, p.b >= 0 ? "" : p.text);
            patch_here(skip);
        }
        release_to(mark);
    }
    // A parameter may be a pattern, as in `function f({x, y})`. Its register holds
    // the argument; the names inside come out of it. Last, so a default has
    // already been applied to the value being destructured.
    for (std::size_t i = 0; i < params.size(); ++i) {
        const vp::node & p = at(params[i]);
        if (p.b >= 0) { compile_pattern_binding(p.b, static_cast<std::uint16_t>(i), true); }
    }
}

void compiler_impl::emit_throw(std::string_view kind, std::string message) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t ctor = alloc_reg();
    proto().emit(instruction::with_bx(op::get_global, ctor, intern_name(std::string{kind})));
    const std::uint16_t reason = alloc_reg();
    emit_string(reason, std::move(message));
    proto().emit(instruction{op::construct, ctor, 1});
    proto().emit(instruction{op::throw_value, ctor});
    release_to(mark);
}

} // namespace ctbrowser::script::detail
