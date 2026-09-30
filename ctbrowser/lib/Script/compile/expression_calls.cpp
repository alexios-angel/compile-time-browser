// Split from expressions.cpp: expression calls.
#include "compiler_impl.hpp"
#include <ctbrowser/script/regex.hpp>

namespace ctbrowser::script::detail {

void compiler_impl::split_template(std::string_view raw, std::vector<std::string> & chunks,
                                   std::vector<std::string> & holes) {
    if (raw.size() >= 2 && raw.front() == '`' && raw.back() == '`') {
        raw = raw.substr(1, raw.size() - 2);
    }
    std::string chunk;
    for (std::size_t i = 0; i < raw.size();) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            chunk += raw[i];
            chunk += raw[i + 1];
            i += 2;
            continue;
        }
        if (raw[i] == '$' && i + 1 < raw.size() && raw[i + 1] == '{') {
            chunks.push_back(std::move(chunk));
            chunk.clear();
            // THE HOLE ENDS AT THE `}` TOKEN that balances it, found by LEXING
            // the rest: a brace inside a string, comment, regex or nested
            // template is not one, so `${"}${alert(1)}"}` is one hole holding
            // a string, not a hole `"` and a second hole that runs code. The
            // tail past the hole lexes as garbage and is never looked at. With
            // no balancing `}` the hole is the rest, which does not parse and
            // throws a SyntaxError where it is evaluated (compile_foreign_expr).
            // ponytail: re-lexes the remaining template per hole, quadratic in
            // a template of many holes; lex once per hole end if that shows.
            const std::size_t start = i + 2;
            const std::string_view rest = raw.substr(start);
            std::size_t end = rest.size();
            std::size_t resume = rest.size();
            std::size_t depth = 1;
            for (const vp::token & t : vp::lex(rest)) {
                if (t.kind != vp::tk::punct) { continue; }
                if (t.s == "{") { ++depth; }
                if (t.s == "}" && --depth == 0) {
                    end = t.begin;
                    resume = t.end;
                    break;
                }
            }
            holes.emplace_back(rest.substr(0, end));
            i = start + resume;
            continue;
        }
        chunk += raw[i++];
    }
    chunks.push_back(std::move(chunk));
}

bool compiler_impl::template_chunk_cooks(std::string_view chunk) {
    const auto hex = [](char c) { return hex_value(c) >= 0; };
    for (std::size_t i = 0; i + 1 < chunk.size(); ++i) {
        if (chunk[i] != '\\') { continue; }
        const char e = chunk[++i];
        if (e == 'x') {
            if (i + 2 >= chunk.size() || !hex(chunk[i + 1]) || !hex(chunk[i + 2])) { return false; }
            i += 2;
        } else if (e == 'u') {
            if (i + 1 < chunk.size() && chunk[i + 1] == '{') {
                std::size_t j = i + 2;
                unsigned long cp = 0;
                std::size_t count = 0;
                for (; j < chunk.size() && hex(chunk[j]); ++j) {
                    cp = cp * 16 + static_cast<unsigned long>(chunk[j] <= '9'
                                                                  ? chunk[j] - '0'
                                                                  : (chunk[j] | 0x20) - 'a' + 10);
                    if (cp > 0x10FFFF) { cp = 0x110000; }
                    ++count;
                }
                if (count == 0 || j >= chunk.size() || chunk[j] != '}' || cp > 0x10FFFF) {
                    return false;
                }
                i = j;
            } else {
                if (i + 4 >= chunk.size() || !hex(chunk[i + 1]) || !hex(chunk[i + 2]) ||
                    !hex(chunk[i + 3]) || !hex(chunk[i + 4])) {
                    return false;
                }
                i += 4;
            }
        } else if (e >= '0' && e <= '9') {
            // `\0` not followed by a digit is NUL; any other digit escape is
            // the legacy octal / non-octal-decimal form a template refuses.
            if (!(e == '0' &&
                  (i + 1 >= chunk.size() || chunk[i + 1] < '0' || chunk[i + 1] > '9'))) {
                return false;
            }
        }
    }
    return true;
}

void compiler_impl::compile_tagged(const vp::node & n, std::int32_t idx, std::uint16_t dst) {
    const vp::node & tmpl = at(n.b);
    std::vector<std::string> chunks;
    std::vector<std::string> holes;
    split_template(tmpl.text, chunks, holes);

    const std::uint32_t mark = reg_mark();
    // The call's window: callee, then the strings array and one register per
    // substitution, then the receiver (a member tag is called on its object).
    const std::uint16_t base = alloc_reg();
    std::vector<std::uint16_t> arg_regs;
    arg_regs.reserve(holes.size() + 1);
    for (std::size_t i = 0; i <= holes.size(); ++i) { arg_regs.push_back(alloc_reg()); }
    const vp::node & tag = at(n.a);
    const bool receiver = tag.kind == vp::nk::member || tag.kind == vp::nk::index;
    const std::uint16_t self = receiver ? alloc_reg() : base;
    if (receiver) {
        compile_expr(tag.a, self);
        if (tag.kind == vp::nk::member) {
            proto().emit(instruction{op::get_prop, base, self, member_operand(tag.text)});
        } else {
            const std::uint32_t inner = reg_mark();
            const std::uint16_t key = alloc_reg();
            compile_expr(tag.b, key);
            proto().emit(instruction{op::get_index, base, self, key});
            release_to(inner);
        }
    } else {
        compile_expr(n.a, base);
    }

    // GetTemplateObject: `strings = __ctbrowser_template_object(key, cooked, raw)`.
    // The key names the SITE - the template's offset in this source, salted
    // with the source itself - so one site hands the same frozen array to its
    // tag every time and two sites never share one. ponytail: two identical
    // scripts compiled separately share a site; a per-program salt fixes it if
    // anything observes that.
    {
        const std::uint32_t inner = reg_mark();
        const std::uint16_t callee = alloc_reg();
        proto().emit(instruction::with_bx(op::get_global, callee,
                                          intern_name(std::string{template_object_name})));
        const std::uint16_t key = alloc_reg();
        emit_string(key, std::to_string(std::hash<std::string_view>{}(source_view_)) + ":" +
                             std::to_string(offset_of(n.b)) + ":" + std::to_string(idx));
        const std::uint16_t cooked = alloc_reg();
        const std::uint16_t raw = alloc_reg();
        proto().emit(instruction{op::new_array, cooked});
        proto().emit(instruction{op::new_array, raw});
        const std::uint16_t piece = alloc_reg();
        for (const std::string & chunk : chunks) {
            if (template_chunk_cooks(chunk)) {
                emit_string(piece, decode_string_body(chunk));
            } else {
                proto().emit(instruction{op::load_undef, piece});
            }
            proto().emit(instruction{op::append, cooked, piece});
            // TRV: a raw chunk keeps its escapes, with CRLF and CR read as LF.
            std::string spelled;
            for (std::size_t i = 0; i < chunk.size(); ++i) {
                if (chunk[i] == '\r') {
                    spelled += '\n';
                    if (i + 1 < chunk.size() && chunk[i + 1] == '\n') { ++i; }
                } else {
                    spelled += chunk[i];
                }
            }
            emit_string(piece, std::move(spelled));
            proto().emit(instruction{op::append, raw, piece});
        }
        proto().emit(instruction{op::call, callee, 3});
        proto().emit(instruction{op::move, arg_regs[0], callee});
        release_to(inner);
    }
    for (std::size_t i = 0; i < holes.size(); ++i) {
        compile_owned_expr("(" + holes[i] + ")", arg_regs[i + 1]);
    }
    proto().emit(instruction{receiver ? op::call_receiver : op::call, base,
                             static_cast<std::uint16_t>(holes.size() + 1),
                             receiver ? self : std::uint16_t{0}});
    proto().emit(instruction{op::move, dst, base});
    release_to(mark);
}

void compiler_impl::compile_template(const vp::node & n, std::uint16_t dst) {
    // The same cut a tagged template gets (split_template); each chunk is
    // COOKED - every escape 12.9.6 names, through decode_string_literal, not
    // the three this used to know - and each hole is compiled as a
    // parenthesised expression (`${ {v: 1}.v }` would otherwise parse as a
    // block), then concatenated, which is the coercion a template performs.
    std::vector<std::string> chunks;
    std::vector<std::string> holes;
    split_template(n.text, chunks, holes);
    const std::uint32_t mark = reg_mark();
    const std::uint16_t piece = alloc_reg();
    bool started = false;
    const auto append = [&](std::uint16_t src) {
        if (!started) {
            proto().emit(instruction{op::move, dst, src});
            started = true;
        } else {
            proto().emit(instruction{op::concat, dst, dst, src});
        }
    };
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        // A CRLF or lone CR in the template text reads as LF (TV, 12.9.6).
        std::string text;
        for (std::size_t k = 0; k < chunks[i].size(); ++k) {
            if (chunks[i][k] == '\r') {
                text += '\n';
                if (k + 1 < chunks[i].size() && chunks[i][k + 1] == '\n') { ++k; }
            } else {
                text += chunks[i][k];
            }
        }
        if (!text.empty() || !started) {
            emit_string(piece, decode_string_body(text));
            append(piece);
        }
        if (i < holes.size()) {
            compile_owned_expr("(" + holes[i] + ")", piece);
            append(piece);
        }
    }
    if (!started) { emit_string(dst, std::string{}); }
    release_to(mark);
}

void compiler_impl::emit_super_base(std::uint16_t dst) {
    proto().emit(instruction{op::load_home, dst});
    proto().emit(instruction{op::get_proto, dst, dst});
}

void compiler_impl::emit_init_fields_at_entry() {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t callee = alloc_reg();
    proto().emit(
        instruction::with_bx(op::get_global, callee, intern_name(std::string{init_fields_name})));
    const std::uint16_t self = alloc_reg();
    proto().emit(instruction{op::load_this, self});
    const std::uint16_t klass = alloc_reg();
    proto().emit(instruction{op::load_callee, klass});
    proto().emit(instruction{op::call, callee, 2});
    release_to(mark);
}

void compiler_impl::emit_bind_this_after_super(std::uint16_t result) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t callee = alloc_reg();
    proto().emit(
        instruction::with_bx(op::get_global, callee, intern_name(std::string{bind_this_name})));
    const std::uint16_t v = alloc_reg();
    proto().emit(instruction{op::move, v, result});
    proto().emit(instruction{op::call, callee, 1});
    release_to(mark);
}

void compiler_impl::emit_init_fields_after_super() {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t callee = alloc_reg();
    proto().emit(
        instruction::with_bx(op::get_global, callee, intern_name(std::string{init_fields_name})));
    const std::uint16_t self = alloc_reg();
    proto().emit(instruction{op::load_this, self});
    // The class, by way of its HOME: the constructor's home object is
    // C.prototype, whose `constructor` is C - through an arrow too, which
    // inherits the home. The native reads `constructor` off it rather than
    // this emitting `get_prop "constructor"`, because a `.constructor` read
    // whose receiver is not provably a non-function is, to ctcompile's
    // resolve-globals pass, a value that may be `Function` escaping into a
    // call it cannot name - which refused every global of every program with
    // a derived class in it.
    const std::uint16_t home = alloc_reg();
    proto().emit(instruction{op::load_home, home});
    proto().emit(instruction{op::call, callee, 2});
    release_to(mark);
}

// `dst = __ctbrowser_super_get(HomeObject.[[Prototype]], key, this)`.
void compiler_impl::emit_super_get(std::uint16_t key, std::uint16_t dst) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t callee = alloc_reg();
    proto().emit(
        instruction::with_bx(op::get_global, callee, intern_name(std::string{super_get_name})));
    const std::uint16_t base = alloc_reg();
    emit_super_base(base);
    const std::uint16_t k = alloc_reg();
    proto().emit(instruction{op::move, k, key});
    const std::uint16_t self = alloc_reg();
    proto().emit(instruction{op::load_this, self});
    proto().emit(instruction{op::call, callee, 3});
    proto().emit(instruction{op::move, dst, callee});
    release_to(mark);
}

bool compiler_impl::any_spread(std::span<const std::int32_t> args) const {
    for (const std::int32_t arg : args) {
        if (at(arg).kind == vp::nk::spread) { return true; }
    }
    return false;
}

void compiler_impl::emit_argument_array(std::span<const std::int32_t> args, std::uint16_t dst) {
    proto().emit(instruction{op::new_array, dst});
    const std::uint32_t mark = reg_mark();
    for (const std::int32_t arg : args) {
        const std::uint16_t v = alloc_reg();
        if (at(arg).kind == vp::nk::spread) {
            compile_expr(at(arg).a, v);
            emit_append_all(dst, v);
        } else {
            compile_expr(arg, v);
            proto().emit(instruction{op::append, dst, v});
        }
        release_to(mark);
    }
}

namespace {
bool call_has_receiver(const vp::node & callee) {
    return callee.kind == vp::nk::member || callee.kind == vp::nk::index ||
           callee.kind == vp::nk::opt_member || callee.kind == vp::nk::opt_index ||
           callee.kind == vp::nk::super_lit;
}
} // namespace

bool compiler_impl::call_needs_receiver(const vp::node & callee) {
    return call_has_receiver(callee) ||
           (callee.kind == vp::nk::ident && !applicable_with_scopes(callee.text).empty());
}

void compiler_impl::emit_optional_guard(std::uint16_t value) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t nullish = alloc_reg();
    proto().emit(instruction{op::load_null, nullish});
    const std::uint16_t test = alloc_reg();
    proto().emit(instruction{op::loose_equal, test, value, nullish});
    optional_exits_.push_back(proto().emit(instruction{op::jump_if_true, test}));
    release_to(mark);
}

// Evaluate the reference and GetValue before evaluating any argument. Keeping
// the callable and receiver in separate registers also preserves both when an
// argument replaces the method or reassigns the variable naming its receiver.
void compiler_impl::compile_call_target(const vp::node & n, std::uint16_t target,
                                        std::uint16_t self) {
    const vp::node & callee = at(n.a);
    const bool super_method =
        callee.kind == vp::nk::member && callee.a >= 0 && at(callee.a).kind == vp::nk::super_lit;
    if (callee.kind == vp::nk::super_lit || super_method) {
        emit_super_base(target);
        const std::string name = super_method ? std::string{callee.text} : "constructor";
        proto().emit(instruction{op::get_prop, target, target, name_operand(name)});
        proto().emit(instruction{op::load_this, self});
    } else if (callee.kind == vp::nk::index && callee.a >= 0 &&
               at(callee.a).kind == vp::nk::super_lit) {
        // `super[k]()` (13.3.7.1, 13.3.6.1): the key, then the method off the
        // home object's prototype, called with `this`. It was a refusal of
        // the whole script - compile_expr has no value for `super` alone.
        const std::uint32_t mark = reg_mark();
        const std::uint16_t key = alloc_reg();
        compile_expr(callee.b, key);
        emit_super_get(key, target);
        release_to(mark);
        proto().emit(instruction{op::load_this, self});
    } else if (callee.kind == vp::nk::ident && emit_with_object(callee.text, self)) {
        // `f()` inside a `with`: the object that binds f is the receiver,
        // and undefined when none does - which is what a plain call passes.
        const std::size_t bound = proto().emit(instruction{op::jump_if_not_nullish, self});
        emit_plain_read(callee.text, target);
        const std::size_t done = proto().emit(instruction{op::jump});
        patch_here(bound);
        proto().emit(
            instruction{op::get_prop, target, self, name_operand(std::string{callee.text})});
        // An eval's variable environment is a declarative record, whose
        // WithBaseObject is undefined (9.1.1.1.10): a callee found on one is
        // called with no receiver, where a `with`'s object is its own.
        const std::vector<const with_scope *> scopes = applicable_with_scopes(callee.text);
        if (std::ranges::all_of(scopes, [](const with_scope * w) { return w->eval_vars; })) {
            proto().emit(instruction{op::load_undef, self});
        } else {
            for (const with_scope * w : scopes) {
                if (!w->eval_vars) { continue; }
                const std::uint32_t mark = reg_mark();
                const std::uint16_t held = alloc_reg();
                emit_plain_read(w->name, held);
                const std::uint16_t same = alloc_reg();
                proto().emit(instruction{op::equal, same, self, held});
                const std::size_t other = proto().emit(instruction{op::jump_if_false, same});
                proto().emit(instruction{op::load_undef, self});
                patch_here(other);
                release_to(mark);
            }
        }
        patch_here(done);
    } else if (call_has_receiver(callee)) {
        compile_expr(callee.a, self);
        if (callee.kind == vp::nk::opt_member || callee.kind == vp::nk::opt_index) {
            emit_optional_guard(self);
        }
        if (callee.kind == vp::nk::member || callee.kind == vp::nk::opt_member) {
            proto().emit(instruction{op::get_prop, target, self, member_operand(callee.text)});
        } else {
            const std::uint32_t mark = reg_mark();
            const std::uint16_t key = alloc_reg();
            compile_expr(callee.b, key);
            proto().emit(instruction{op::get_index, target, self, key});
            release_to(mark);
        }
    } else {
        compile_expr(n.a, target);
    }
    if (n.kind == vp::nk::opt_call) { emit_optional_guard(target); }
}

void compiler_impl::compile_spread_call(const vp::node & n, std::uint16_t dst) {
    const std::span<const std::int32_t> args = kids(n);
    const vp::node & callee = at(n.a);
    const std::uint32_t mark = reg_mark();
    const std::uint16_t target = alloc_reg();
    const std::uint16_t self = alloc_reg();
    compile_call_target(n, target, self);
    if (!call_needs_receiver(callee)) { proto().emit(instruction{op::load_undef, self}); }
    const std::uint16_t argv = alloc_reg();
    emit_argument_array(args, argv);
    // `super(...)` - NOT `super.m(...)` - carries new.target into the base
    // constructor. Babylon reads it there to hang decorator metadata off
    // the class actually being constructed, and got undefined.
    const std::string * flag = callee.kind == vp::nk::super_lit ? derived_flag() : nullptr;
    if (flag != nullptr) { emit_super_check(*flag, true); } // see compile_call
    if (callee.kind == vp::nk::super_lit) { proto().emit(instruction{op::pass_new_target}); }
    proto().emit(instruction{op::apply, target, argv, self});
    if (flag != nullptr) { emit_super_done(*flag); }
    if (callee.kind == vp::nk::super_lit) {
        emit_bind_this_after_super(target);
        emit_init_fields_after_super();
    }
    proto().emit(instruction{op::move, dst, target});
    release_to(mark);
}

bool compiler_impl::compile_param_eval(const vp::node & n, std::uint16_t dst) {
    const vp::node & callee = at(n.a);
    if (param_scope_names_.empty() || callee.kind != vp::nk::ident || callee.text != "eval" ||
        n.kind != vp::nk::call) {
        return false;
    }
    // `eval` that is a local or an upvalue is somebody else's function.
    if (find_local_entry(fn(), "eval") != nullptr ||
        resolve_upvalue(frames_.size() - 1, "eval") >= 0) {
        return false;
    }
    const std::span<const std::int32_t> args = kids(n);
    if (any_spread(args)) { return false; }
    const std::uint32_t mark = reg_mark();
    const std::uint16_t base = alloc_reg();
    proto().emit(
        instruction::with_bx(op::get_global, base, intern_name(std::string{param_eval_name})));
    const std::uint16_t source = alloc_reg();
    if (args.empty()) {
        proto().emit(instruction{op::load_undef, source});
    } else {
        compile_expr(args[0], source);
    }
    for (std::size_t i = 1; i < args.size(); ++i) {
        // The other arguments are evaluated for their effects, as eval's are.
        const std::uint16_t extra = alloc_reg();
        compile_expr(args[i], extra);
        release_to(static_cast<std::uint32_t>(extra));
    }
    for (const std::string & name : param_scope_names_) {
        const std::uint16_t r = alloc_reg();
        emit_string(r, name);
    }
    // And the object this function's evals make their `var`s on
    // (with_scope::eval_vars): its name, then its cell.
    std::size_t argc = 1 + param_scope_names_.size();
    if (const local * held =
            fn().eval_vars.empty() ? nullptr : find_local_entry(fn(), fn().eval_vars)) {
        const std::uint16_t name = alloc_reg();
        emit_string(name, fn().eval_vars);
        const std::uint16_t cell = alloc_reg();
        proto().emit(instruction{op::move, cell, held->reg});
        argc += 2;
    }
    proto().emit(instruction{op::call, base, static_cast<std::uint16_t>(argc)});
    proto().emit(instruction{op::move, dst, base});
    release_to(mark);
    return true;
}

// A DIRECT EVAL (19.2.1.1 PerformEval, direct): the eval'd source sees every
// binding in scope at the call. They are all cells (is_captured boxes every
// local of a function containing one, and of the functions around it), so
// the call hands them over by name to direct_eval_name, which compiles the
// source with those names as its entry function's upvalues:
//
//   (eval, source, callee, this, descriptor, new.target, cells...)
//
// `eval` is the binding's current value - the call is only direct if it is
// %eval% (step 6 of 13.3.6.1), which the native checks - `callee` the running
// closure, whose upvalues carry the enclosing functions' bindings, and the
// descriptor says where the eval is on its first line ("strict" or "sloppy",
// then the eval_site words) and names each binding on one of its own: "L
// name" a cell argument, "V name" a value argument (a local nothing boxed),
// "U index name" the callee's upvalue - each followed by " strict" or
// " sloppy" when the binding is immutable (see immutability), and an L or V
// by " lexical" when it is a `let`, `const` or `class` - and "P #x key" a
// private name in scope. The shape changes only in functions that contain
// `eval(`, which no native pipeline compiles anyway.
eval_site compiler_impl::eval_site_here() {
    std::size_t level = frames_.size() - 1;
    while (level > 0 && out_.functions[frames_[level].proto].is_arrow) { --level; }
    // The program's own top level: a script's is in no function, and an
    // eval's is wherever that eval was.
    eval_site site = level == 0 ? eval_site_ : eval_site{};
    site.strict = fn().is_strict;
    if (level > 0) {
        const frame & f = frames_[level];
        site.in_function = true;
        site.in_method = f.has_home;
        site.in_class_field_initializer = f.field_initializer;
        // ponytail: `super()` in a direct eval stays refused (in_derived_
        // constructor false): the eval's frame has neither the constructor's
        // new.target nor its derived flag to hand the call. Pass both when a
        // test262 file needs it (language/eval-code/direct/super-call-*).
    }
    return site;
}

bool compiler_impl::compile_direct_eval(const vp::node & n, std::uint16_t dst) {
    // AT A PROGRAM'S TOP LEVEL TOO: a block's `let` there is a local of the
    // script frame, which `{ let x; eval('x') }` reads (lex-env-heritage).
    // Not inside a `with` - a direct eval's own variable environment is no
    // `with` (with_scope::eval_vars).
    if (!is_direct_eval_call(n) ||
        std::ranges::any_of(with_scopes_, [](const with_scope & w) { return !w.eval_vars; })) {
        return false;
    }
    const std::size_t level = frames_.size() - 1;
    // `eval` that is a local or an upvalue is somebody else's function.
    if (find_local_entry(fn(), "eval") != nullptr || resolve_upvalue(level, "eval") >= 0) {
        return false;
    }
    const std::span<const std::int32_t> args = kids(n);
    if (any_spread(args)) { return false; }
    const auto usable = [this](std::string_view name) {
        return eval_reachable(name) ||
               (name.starts_with("@tdz:") && binding_ready_flag(name.substr(5)) == name);
    };
    // The first line: the strictness and where the eval is (eval_site).
    const eval_site site = eval_site_here();
    std::string descriptor = site.strict ? "strict" : "sloppy";
    if (site.in_function) { descriptor += " function"; }
    if (site.in_method) { descriptor += " method"; }
    if (site.in_class_field_initializer) { descriptor += " field"; }
    // "P #x key": a private name in scope, innermost class first, and the key
    // it compiles to here (member_key).
    std::vector<std::string_view> privates;
    for (std::size_t i = private_scopes_.size(); i-- > 0;) {
        for (const std::string_view name : private_scopes_[i].names) {
            if (std::ranges::find(privates, name) != privates.end()) { continue; }
            privates.push_back(name);
            descriptor += "\nP " + std::string{name} + " " + member_key(name);
        }
    }
    std::vector<std::string> seen;
    std::vector<const local *> handed; // this frame's, in descriptor order
    const auto taken = [&](std::string_view name) {
        return std::ranges::find(seen, name) != seen.end();
    };
    const auto readonly = [&](std::string_view name) -> std::string_view {
        switch (binding_immutability(name)) {
        case immutability::strict: return " strict";
        case immutability::sloppy: return " sloppy";
        case immutability::none: break;
        }
        return "";
    };
    // Sorted by name, so the descriptor does not depend on the hash map's order.
    std::vector<std::string> own;
    for (const auto & [name, positions] : fn().local_index) {
        if (!positions.empty() && usable(name)) { own.emplace_back(name); }
    }
    std::ranges::sort(own);
    for (const std::string & name : own) {
        const local & l = fn().locals[fn().local_index.find(name)->second.back()];
        descriptor += std::string{"\n"} + (l.boxed ? "L " : "V ") + name;
        descriptor += readonly(name);
        if (l.lexical) { descriptor += " lexical"; }
        seen.push_back(name);
        handed.push_back(&l);
    }
    // The object this function's evals make their `var`s on, under its own
    // name: the eval's program knows it by the prefix (eval_vars_name_).
    if (!fn().eval_vars.empty()) {
        if (const local * held = find_local_entry(fn(), fn().eval_vars)) {
            descriptor += "\nL " + fn().eval_vars;
            seen.push_back(fn().eval_vars);
            handed.push_back(held);
        }
    }
    for (std::size_t k = level; k-- > 0;) {
        std::vector<std::string> outer;
        for (const auto & [name, positions] : frames_[k].local_index) {
            if (!positions.empty() && usable(name) && !taken(name)) { outer.emplace_back(name); }
        }
        // ...and the one an enclosing function keeps ITS evals' `var`s on,
        // which the eval's code resolves names through too.
        if (!frames_[k].eval_vars.empty()) { outer.push_back(frames_[k].eval_vars); }
        std::ranges::sort(outer);
        for (const std::string & name : outer) {
            if (taken(name)) { continue; }
            const int up = resolve_upvalue(level, name);
            if (up < 0) { continue; }
            descriptor += "\nU " + std::to_string(up) + " " + name;
            descriptor += readonly(name);
            seen.push_back(name);
        }
    }
    // Inside a direct eval's program, the bindings ITS caller handed over.
    std::vector<std::string> handed_down = eval_outer_names_;
    std::ranges::sort(handed_down);
    for (const std::string & name : handed_down) {
        if (taken(name)) { continue; }
        const int up = resolve_upvalue(level, name);
        if (up < 0) { continue; }
        descriptor += "\nU " + std::to_string(up) + " " + name;
        descriptor += readonly(name);
        seen.push_back(name);
    }
    const std::uint32_t mark = reg_mark();
    const std::uint16_t base = alloc_reg();
    proto().emit(
        instruction::with_bx(op::get_global, base, intern_name(std::string{direct_eval_name})));
    const std::uint16_t eval_fn = alloc_reg();
    emit_plain_read("eval", eval_fn, false);
    const std::uint16_t source = alloc_reg();
    if (args.empty()) {
        proto().emit(instruction{op::load_undef, source});
    } else {
        compile_expr(args[0], source);
    }
    for (std::size_t i = 1; i < args.size(); ++i) {
        // The other arguments are evaluated for their effects, as eval's are.
        const std::uint16_t extra = alloc_reg();
        compile_expr(args[i], extra);
        release_to(static_cast<std::uint32_t>(extra));
    }
    const std::uint16_t callee = alloc_reg();
    proto().emit(instruction{op::load_callee, callee});
    const std::uint16_t self = alloc_reg();
    // `this` before a derived constructor's super() is the ReferenceError of
    // reading it; the eval'd code gets undefined instead of the throw.
    if (derived_flag() != nullptr) {
        proto().emit(instruction{op::load_undef, self});
    } else {
        proto().emit(instruction{op::load_this, self});
    }
    const std::uint16_t names = alloc_reg();
    emit_string(names, descriptor);
    // The eval's `new.target` is its caller's (GetNewTarget reads the this
    // environment it shares).
    const std::uint16_t target = alloc_reg();
    emit_new_target(target);
    for (const local * l : handed) {
        const std::uint16_t r = alloc_reg();
        proto().emit(instruction{op::move, r, l->reg});
    }
    proto().emit(instruction{op::call, base, static_cast<std::uint16_t>(6 + handed.size())});
    proto().emit(instruction{op::move, dst, base});
    release_to(mark);
    return true;
}

void compiler_impl::compile_call(const vp::node & n, std::uint16_t dst) {
    const std::span<const std::int32_t> args = kids(n);
    if (compile_param_eval(n, dst)) { return; }
    if (compile_direct_eval(n, dst)) { return; }
    if (any_spread(args)) {
        compile_spread_call(n, dst);
        return;
    }
    const vp::node & callee = at(n.a);
    const std::uint32_t mark = reg_mark();
    const std::uint16_t base = alloc_reg();
    // The bytecode ABI reads arguments at base+1. Reserve that window before
    // allocating the saved receiver or evaluating a computed member key.
    std::vector<std::uint16_t> arg_regs;
    arg_regs.reserve(args.size());
    for (std::size_t i = 0; i < args.size(); ++i) { arg_regs.push_back(alloc_reg()); }
    const bool receiver = call_needs_receiver(callee);
    const std::uint16_t self = receiver ? alloc_reg() : base;
    compile_call_target(n, base, self);
    for (std::size_t i = 0; i < args.size(); ++i) { compile_expr(args[i], arg_regs[i]); }
    // A second `super()` is the ReferenceError, judged after the arguments
    // (10.2.1.3 BindThisValue, reached after the parent constructor ran).
    const std::string * flag = callee.kind == vp::nk::super_lit ? derived_flag() : nullptr;
    if (flag != nullptr) { emit_super_check(*flag, true); }
    if (callee.kind == vp::nk::super_lit) { proto().emit(instruction{op::pass_new_target}); }
    proto().emit(instruction{receiver ? op::call_receiver : op::call, base,
                             static_cast<std::uint16_t>(args.size()),
                             receiver ? self : std::uint16_t{0}});
    if (flag != nullptr) { emit_super_done(*flag); }
    if (callee.kind == vp::nk::super_lit) {
        emit_bind_this_after_super(base);
        emit_init_fields_after_super();
    }
    proto().emit(instruction{op::move, dst, base});
    release_to(mark);
}

void compiler_impl::compile_new(const vp::node & n, std::uint16_t dst) {
    const std::span<const std::int32_t> args = kids(n);
    const std::uint32_t mark = reg_mark();
    const std::uint16_t base = alloc_reg();
    if (any_spread(args)) {
        compile_expr(n.a, base);
        const std::uint16_t argv = alloc_reg();
        emit_argument_array(args, argv);
        proto().emit(instruction{op::construct_apply, base, argv});
        proto().emit(instruction{op::move, dst, base});
        release_to(mark);
        return;
    }
    // Construct reads base+1 arguments, just like Call. Neither the callee
    // nor an argument may allocate a temporary inside that window.
    std::vector<std::uint16_t> arg_regs;
    arg_regs.reserve(args.size());
    for (std::size_t i = 0; i < args.size(); ++i) { arg_regs.push_back(alloc_reg()); }
    compile_expr(n.a, base);
    for (std::size_t i = 0; i < args.size(); ++i) { compile_expr(args[i], arg_regs[i]); }
    proto().emit(instruction{op::construct, base, static_cast<std::uint16_t>(args.size())});
    proto().emit(instruction{op::move, dst, base});
    release_to(mark);
}

bool compiler_impl::chain_has_optional(std::int32_t idx) const {
    for (std::int32_t at = idx; at >= 0;) {
        const vp::node & n = this->at(at);
        switch (n.kind) {
        case vp::nk::opt_member:
        case vp::nk::opt_index:
        case vp::nk::opt_call: return true;
        case vp::nk::member:
        case vp::nk::index:
        case vp::nk::call: at = n.a; continue;
        default: return false;
        }
    }
    return false;
}

void compiler_impl::compile_chain(std::int32_t idx, std::uint16_t dst) {
    std::vector<std::size_t> outer;
    outer.swap(optional_exits_);
    const bool root = !in_chain_;
    in_chain_ = true;
    compile_expr_inner(idx, dst);
    if (root) {
        in_chain_ = false;
        if (!optional_exits_.empty()) {
            const std::size_t done = proto().emit(instruction{op::jump});
            for (const std::size_t site : optional_exits_) { patch_here(site); }
            proto().emit(instruction{op::load_undef, dst});
            patch_here(done);
        }
    }
    optional_exits_.swap(outer);
    if (!root) {
        // a nested chain hands its exits back to the enclosing one
        for (const std::size_t site : outer) { optional_exits_.push_back(site); }
    }
}

void compiler_impl::compile_optional(const vp::node & n, std::uint16_t dst) {
    if (n.kind == vp::nk::opt_call) {
        compile_call(n, dst);
        return;
    }
    const std::uint32_t mark = reg_mark();
    const std::uint16_t object = alloc_reg();
    compile_expr(n.a, object);

    // null and undefined both short-circuit; nothing else does.
    const std::uint16_t nullish = alloc_reg();
    proto().emit(instruction{op::load_null, nullish});
    const std::uint16_t test = alloc_reg();
    proto().emit(instruction{op::loose_equal, test, object, nullish});
    const std::size_t skip = proto().emit(instruction{op::jump_if_true, test});

    if (n.kind == vp::nk::opt_member) {
        proto().emit(instruction{op::get_prop, dst, object, member_operand(n.text)});
    } else if (n.kind == vp::nk::opt_index) {
        const std::uint16_t key = alloc_reg();
        compile_expr(n.b, key);
        proto().emit(instruction{op::get_index, dst, object, key});
    }
    // The exit belongs to the CHAIN, not to this link. compile_chain
    // patches every one of them to a single point past the whole thing.
    optional_exits_.push_back(skip);
    release_to(mark);
}

void compiler_impl::compile_sequence(const vp::node & n, std::uint16_t dst) {
    // A SEQUENCE IS BINARY, not a list. The parser builds `a, b, c` as
    // seq(seq(a, b), c) - left-nested, two children per node - and this
    // read `kids(n)` instead, which for such a node is EMPTY. So every
    // comma expression evaluated nothing at all and produced undefined,
    // silently, from the day the operator was added.
    //
    // Nothing caught it because the operator was added to the parser with
    // no test that a comma expression has EFFECTS - only that it parsed.
    // `_createClass(e, r, t) { return r && _defineProperties(...), ..., e; }`
    // is how every Babel-transpiled class installs its methods, so this one
    // omission silently emptied every such class in p5.js.
    compile_expr(n.a, dst);
    compile_expr(n.b, dst);
}

void compiler_impl::compile_regex_literal(const vp::node & n, std::uint16_t dst) {
    const std::string_view literal = n.text;
    const std::size_t close = literal.rfind('/');
    if (literal.size() < 2 || literal.front() != '/' || close == 0) {
        fail("malformed regular expression literal (" + std::string{literal} + ")");
        proto().emit(instruction{op::load_undef, dst});
        return;
    }
    // INVALID FLAGS ARE AN EARLY ERROR (13.2.7.2): `/a/gg`, `/a/x`. The
    // PATTERN is deliberately not checked here: rx_compile cannot tell a
    // syntax error from a feature it lacks (lookbehind, `\u{...}`), and the
    // runtime path hands it the escape-DECODED text - so a compile-time
    // check refused p5.js whole over `/\u2028/`, which runs fine. A bad
    // pattern stays a throw at the line, as it was.
    if (const rx::rx_prog probe = rx::rx_compile("", literal.substr(close + 1)); !probe.ok) {
        fail("parse error: " + probe.error);
        proto().emit(instruction{op::load_undef, dst});
        return;
    }
    const std::uint32_t mark = reg_mark();
    const std::uint16_t callee = alloc_reg();
    proto().emit(instruction::with_bx(op::get_global, callee,
                                      intern_name(std::string{regexp_factory_name})));
    const std::uint16_t source = alloc_reg();
    emit_string(source, std::string{literal.substr(1, close - 1)});
    const std::uint16_t flags = alloc_reg();
    emit_string(flags, std::string{literal.substr(close + 1)});
    proto().emit(instruction{op::call, callee, 2});
    proto().emit(instruction{op::move, dst, callee});
    release_to(mark);
}

void compiler_impl::compile_array(const vp::node & n, std::uint16_t dst) {
    proto().emit(instruction{op::new_array, dst});
    const std::uint32_t mark = reg_mark();
    // An ELISION IS A HOLE (13.2.4.1): `[1, , 3]` has a length of 3 and no
    // element 1, which forEach and `1 in` can tell from undefined. The slot
    // is appended as undefined and marked afterwards, in one native call
    // naming the positions - only while those positions are static, which
    // a spread before the hole makes them not.
    std::vector<std::uint32_t> holes;
    bool positions_known = true;
    std::uint32_t position = 0;
    for (const std::int32_t element : kids(n)) {
        const std::uint16_t v = alloc_reg();
        // A HOLE. `[, x]` and `[a, , b]` are legal, and an element list is
        // the one place kids() yields -1 - which is why at() refuses a
        // negative index rather than reading past the pool.
        if (element < 0) {
            proto().emit(instruction{op::load_undef, v});
            proto().emit(instruction{op::append, dst, v});
            if (positions_known) { holes.push_back(position); }
            ++position;
            release_to(mark);
            continue;
        }
        if (at(element).kind == vp::nk::spread) {
            // `[...a]` appends a's ELEMENTS, not a itself. Compiled as an
            // index loop rather than an opcode, because that is all it is.
            compile_expr(at(element).a, v);
            emit_append_all(dst, v);
            positions_known = false;
        } else {
            compile_expr(element, v);
            proto().emit(instruction{op::append, dst, v});
            ++position;
        }
        release_to(mark);
    }
    if (!holes.empty()) {
        const std::uint16_t callee = alloc_reg();
        proto().emit(instruction::with_bx(op::get_global, callee,
                                          intern_name(std::string{array_holes_name})));
        const std::uint16_t arr = alloc_reg();
        proto().emit(instruction{op::move, arr, dst});
        std::uint16_t argc = 1;
        for (const std::uint32_t at_index : holes) {
            if (argc == 200) { break; }
            emit_const(alloc_reg(), value::number(static_cast<double>(at_index)));
            ++argc;
        }
        proto().emit(instruction{op::call, callee, argc});
        release_to(mark);
    }
}

void compiler_impl::emit_append_all(std::uint16_t target, std::uint16_t source) {
    const std::uint32_t mark = reg_mark();
    // SPREAD TAKES ANYTHING ITERABLE, not just an array. This walks a
    // `length`, so `[...new Set(v)]` and `f(...map.keys())` produced nothing
    // at all - see context::iterable_values for what that cost.
    proto().emit(instruction{op::iterable, source, source});
    const std::uint16_t length = alloc_reg();
    proto().emit(instruction{op::get_prop, length, source, name_operand("length")});
    const std::uint16_t index = alloc_reg();
    emit_const(index, value::number(0));
    const std::uint16_t one = alloc_reg();
    emit_const(one, value::number(1));
    const std::uint16_t test = alloc_reg();
    const std::uint16_t item = alloc_reg();
    const std::size_t top = proto().code.size();
    proto().emit(instruction{op::less, test, index, length});
    const std::size_t exit = proto().emit(instruction{op::jump_if_false, test});
    proto().emit(instruction{op::get_index, item, source, index});
    proto().emit(instruction{op::append, target, item});
    proto().emit(instruction{op::add, index, index, one});
    patch_jump(proto().emit(instruction{op::jump}), top);
    patch_here(exit);
    release_to(mark);
}

void compiler_impl::compile_object(const vp::node & n, std::uint16_t dst) {
    proto().emit(instruction{op::new_object, dst});
    const std::uint32_t mark = reg_mark();
    for (const std::int32_t p : kids(n)) {
        const vp::node & prop = at(p);
        if (prop.kind == vp::nk::spread) {
            // `{...o}` copies o's own properties in, and a later key still
            // wins - which is why this is a copy at this point in the
            // sequence rather than a merge at the end.
            const std::uint16_t source = alloc_reg();
            compile_expr(prop.a, source);
            proto().emit(instruction{op::copy_props, dst, source});
            release_to(mark);
            continue;
        }
        if (prop.kind != vp::nk::prop) {
            fail("unsupported object literal member");
            return;
        }
        if (prop.c == 3) {
            // An accessor, not a data property. `d` bit2 says which half.
            if ((prop.d & 1) != 0 && prop.a >= 0) {
                emit_computed_accessor(dst, prop.a, prop.b, (prop.d & 4) != 0);
                release_to(mark);
                continue;
            }
            const std::uint16_t fnreg = alloc_reg();
            const bool homed = mentions_super(prop.b, true) || contains_direct_eval(prop.b);
            method_pending_ = homed; // see frame::has_home
            // An accessor is named for its key, "get id" (15.4.5 step 6).
            compile_named_expr(prop.b, fnreg,
                               ((prop.d & 4) != 0 ? "set " : "get ") +
                                   decode_string_literal(prop.text));
            method_pending_ = false;
            const std::uint16_t name = name_operand(decode_string_literal(prop.text));
            proto().emit(instruction{(prop.d & 4) != 0 ? op::define_setter : op::define_getter, dst,
                                     name, fnreg});
            // The literal is the accessor's home object (15.4.5 step 3), so
            // `super.x` inside it resolves through the literal's prototype -
            // wired only when the body says `super`: the own `__home` is a
            // reference from the closure back to the literal, and for the
            // 99% of methods that never use it that is a property to store
            // and a cycle the native backend's escape analysis has to refuse.
            // (A direct eval in it may say `super` too.)
            if (homed) { emit_define_own(fnreg, "__home", dst, false); }
            release_to(mark);
            continue;
        }
        const std::uint16_t v = alloc_reg();
        const bool homed =
            prop.c == 1 && (mentions_super(prop.b, true) || contains_direct_eval(prop.b));
        if (prop.c == 2 && prop.b >= 0) {
            // `{ a = 1 }` outside a pattern: the early-error pass refuses it
            // (13.2.5.1), so this is unreachable from a checked program.
            fail("`" + std::string{prop.text} + " = ...` in an object literal is only a pattern");
            release_to(mark);
            return;
        }
        // A COMPUTED KEY NAMES THE FUNCTION IT HOLDS too (13.2.5.5, 15.4.4
        // DefineMethod): known now for a literal key, and otherwise only once
        // the key is evaluated - which then comes first, made a key once
        // (13.2.5.5 steps 1-2, before the value), and names the value at run
        // time.
        const std::optional<std::string> literal_key =
            (prop.d & 1) != 0 && prop.a >= 0 ? literal_key_name(prop.a) : std::nullopt;
        // Only a colon after an identifier or quoted __proto__ sets the
        // prototype. Computed names, shorthand properties and methods define data.
        const bool sets_prototype =
            prop.c < 0 && (((prop.d & 1) == 0 && prop.text == "__proto__") ||
                           ((prop.d & 2) != 0 && literal_key == "__proto__"));
        const bool named_at_run_time = (prop.d & 1) != 0 && prop.a >= 0 && !literal_key &&
                                       (prop.c == 1 || is_anonymous_function_definition(prop.b));
        std::uint16_t key = 0;
        if ((prop.d & 1) != 0) {
            key = alloc_reg();
            compile_expr(prop.a, key);
            if (!literal_key) { emit_iterator_native(to_property_key_name, key, key); }
        }
        method_pending_ = homed;
        if (prop.b < 0) {
            compile_ident(prop, v); // shorthand { x }
        } else if ((prop.d & 1) == 0 && !sets_prototype) {
            // `{ f: function () {} }` names f (13.2.5.5); `__proto__: ...`
            // is a prototype assignment and names nothing.
            compile_named_expr(prop.b, v, decode_string_literal(prop.text));
        } else if (literal_key && !sets_prototype) {
            compile_named_expr(prop.b, v, *literal_key);
        } else {
            compile_expr(prop.b, v);
        }
        method_pending_ = false;
        // A METHOD'S HOME OBJECT IS THE LITERAL (15.4.4 step 2): `super.m()`
        // in `{ m() { super.m(); } }` starts at Object.prototype or whatever
        // `__proto__:` set.
        if (homed) { emit_define_own(v, "__home", dst, false); }
        if (named_at_run_time) { emit_set_function_name(v, key); }
        if (sets_prototype) {
            // 13.2.5.5 sets the internal link, without consulting an accessor.
            // Only objects and null apply; null is stored as the explicit-null
            // sentinel used by set_proto, while primitive values are ignored.
            const std::uint16_t type = alloc_reg(), kind = alloc_reg(), test = alloc_reg();
            proto().emit(instruction{op::type_of, type, v});
            emit_string(kind, "object");
            proto().emit(instruction{op::equal, test, type, kind});
            const std::size_t object = proto().emit(instruction{op::jump_if_true, test});
            emit_string(kind, "function");
            proto().emit(instruction{op::equal, test, type, kind});
            const std::size_t primitive = proto().emit(instruction{op::jump_if_false, test});
            patch_here(object);
            const std::size_t nonnull = proto().emit(instruction{op::jump_if_not_nullish, v});
            proto().emit(instruction{op::load_undef, v});
            patch_here(nonnull);
            proto().emit(instruction{op::set_proto, dst, v});
            patch_here(primitive);
        } else {
            // DEFINED, not set (13.2.5.5 CreateDataPropertyOrThrow), by an
            // opcode rather than a global helper a page could replace.
            if ((prop.d & 1) == 0) {
                key = alloc_reg();
                emit_string(key, decode_string_literal(prop.text));
            }
            proto().emit(instruction{op::define_data, dst, key, v});
        }
        release_to(mark);
    }
}

} // namespace ctbrowser::script::detail
