// compiler_impl - frames, registers and scopes.
//
// The register allocator, the scope stack, locals and upvalues,
// and the module import/export bindings. The three template members stay in
// the header - two are called from more than one file.
//
// One of the files carved out of a 3,845-line compile.cpp on 2026-08-09.
// The class is declared whole in compiler_impl.hpp beside this.

#include "compiler_impl.hpp"

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
    // THE DEBUG TABLE IS WRITTEN HERE AND NOWHERE ELSE, because this is the one
    // place a name comes into existence. `finish_frame` cannot do it - the plan
    // suggested it could - since `frame::locals` is a STACK that `pop_scope`
    // has already emptied of everything but the outermost scope by then.
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
    // THE BOXEDNESS AT THE END, not at the declaration. `mark_captured`,
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
    // A SUB-AST ANSWERS NOTHING. A template literal's interpolation and
    // `compile_owned_expr`'s synthesised text are parsed from OTHER buffers, so
    // a lexeme in one is not an offset into the program at all. Keeping the
    // enclosing statement's position is right as well as safe: that is where
    // the template was written.
    if (idx < 0 || current_ast_ != &ast_ || source_view_.empty()) {
        return function_proto::no_offset;
    }
    const vp::node & n = at(idx);
    if (n.begin != 0) { return n.begin; } // a function; see node::begin
    const char * const base = source_view_.data();
    const char * const text = n.text.data();
    // BOUNDS-CHECKED WITH std::less, not with `<`. Comparing pointers into
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
    // THE SPECIFIER IS RECORDED FOR THE LOADER, which walks the graph
    // without re-parsing anything.
    //
    // DECODED, because the parser hands over the token as written - quotes
    // included. Recording `'./m.js'` rather than `./m.js` made every lookup
    // miss and every import read undefined, with the loader reporting a
    // module it had never been asked for.
    const std::string specifier = decode_string_literal(n.text);
    if (std::ranges::find(out_.imports, specifier) == out_.imports.end()) {
        out_.imports.push_back(specifier);
    }
    const std::uint16_t from = name_operand(specifier);
    for (const std::int32_t spec_index : kids(n)) {
        const vp::node & spec = at(spec_index);
        // `c`: 0 named, 1 default, 2 namespace. A namespace import wants
        // the whole module object, which is stage 4 work - it is refused by
        // name rather than bound to nothing.
        if (spec.c == 2) {
            // `import * as ns`: one binding, holding the exporter's
            // namespace object. NOT a cell - the namespace is itself the
            // live thing, because each of its properties reads a cell.
            const int hoisted_ns = find_local(spec.text);
            if (hoisted_ns < 0) {
                fail("`import * as " + std::string{spec.text} +
                     "` was not hoisted - see predeclare_locals");
                return;
            }
            // THROUGH THE CELL IF IT IS ONE. A namespace binding captured
            // by a function was hoisted BOXED, so writing the register
            // directly would throw the cell away and leave the closure
            // reading undefined - the same shape of bug that made an
            // imported function called from a closure read undefined.
            bool boxed = false;
            for (const local & each : fn().locals) {
                if (each.name == spec.text) { boxed = each.boxed; }
            }
            if (boxed) {
                const std::uint16_t temp = alloc_reg();
                proto().emit(instruction{op::load_namespace, temp, from});
                proto().emit(
                    instruction{op::cell_set, static_cast<std::uint16_t>(hoisted_ns), temp});
            } else {
                proto().emit(
                    instruction{op::load_namespace, static_cast<std::uint16_t>(hoisted_ns), from});
            }
            continue;
        }
        // The name the EXPORTER knows it by: the original when renamed,
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
        // ALWAYS BOXED, whether or not this module captures it: every read
        // has to go through the exporter's cell to see the writes that make
        // the binding live. And no `new_cell` - load_import has already put
        // the exporter's box here, and boxing a box leaves the importer
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
        // `export * as ns from './m.js'`: ONE name, holding the namespace.
        // Not built - a namespace is a value rather than a cell, so it does
        // not fit the alias the loader wires - and refused by name rather
        // than silently exporting nothing.
        fail("`export * as ns from` is not implemented yet - ES modules are staged in "
             "docs/plans/modules.md");
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
        out.emplace_back(std::string{default_binding}, "default");
        return;
    }
    if (n.a >= 0) {
        const vp::node & declared = at(n.a);
        if (declared.kind == vp::nk::var_decl) {
            for (const std::int32_t d : kids(declared)) {
                if (!at(d).text.empty()) {
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
    if (level == 0) { return -1; }
    frame & f = frames_[level];
    // Was a linear scan of upvalue_names, and it ran once per level per
    // identifier mention - 8.65% of a Babylon compile between this and its
    // recursive self, most of it inside memcmp.
    if (const auto it = f.upvalue_index.find(name); it != f.upvalue_index.end()) {
        return static_cast<int>(it->second);
    }
    frame & parent = frames_[level - 1];
    // Frame 0 is the script, and its `var`s are globals rather than locals
    // (see the note in the var_decl arm), so the only locals it ever holds
    // are the ones a construct makes for itself: a for..of item, a catch
    // parameter. Those must be capturable or `for (const x of xs)
    // fns.push(() => x)` closes over nothing at the top level.
    if (local * l = find_local_entry(parent, name)) {
        l->boxed = true; // captured, so it must be a cell
        return add_upvalue(level, name, upvalue_desc{true, l->reg});
    }
    const int inherited = resolve_upvalue(level - 1, name);
    if (inherited < 0) { return -1; }
    return add_upvalue(level, name, upvalue_desc{false, static_cast<std::uint16_t>(inherited)});
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
    for (std::size_t i = 0; i < params.size(); ++i) {
        const vp::node & p = at(params[i]);
        if (p.d == 1 || p.a < 0) { continue; }
        const auto slot = static_cast<std::uint16_t>(i);
        const std::size_t skip = proto().emit(instruction{op::jump_if_defined, slot});
        const std::uint32_t mark = reg_mark();
        compile_expr(p.a, slot);
        release_to(mark);
        patch_here(skip);
    }
    // A parameter may be a SHAPE - `function f({x, y})`. Its register holds
    // the argument; the names inside come out of it. Last, so a default has
    // already been applied to the value being destructured.
    for (std::size_t i = 0; i < params.size(); ++i) {
        const vp::node & p = at(params[i]);
        if (p.b >= 0) { compile_pattern_binding(p.b, static_cast<std::uint16_t>(i), true); }
    }
}

std::string compiler_impl::kind_name(vp::nk kind) {
    switch (kind) {
    case vp::nk::spread: return "spread in a call, `f(...args)`";
    case vp::nk::seq: return "the comma operator";
    case vp::nk::regex: return "a regular expression literal";
    case vp::nk::yield_expr: return "`yield`";
    case vp::nk::tagged: return "a tagged template literal";
    case vp::nk::arrow: return "an arrow function";
    case vp::nk::class_decl: return "a class declaration";
    case vp::nk::func_expr: return "a function expression";
    default: return "AST kind " + std::to_string(static_cast<int>(kind));
    }
}

} // namespace ctbrowser::script::detail
