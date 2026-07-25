module;
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <ctjs/vparse.hpp>

export module ctbrowser.script:compile;

import :bytecode;
import :value;

// AST to bytecode.
//
// The front end is ctjs's existing Pratt parser, reused rather than rewritten
// for the same reason cthtml's parser is reused in the DOM: it works, and
// blocking a VM on a fresh JavaScript parser would be the wrong order to
// build in. What is replaced is everything after it - v1 walked this tree on
// every execution, re-deciding what each node meant each time round a loop.
// Compiling once and dispatching on 4-byte instructions is the entire point.
//
// Register allocation is a high-water mark. Locals take the low registers of
// a frame and keep them for their scope; expression temporaries are allocated
// above the locals and released as each statement finishes. No liveness
// analysis, no spilling - the frame is simply as large as the deepest
// expression needed.
//
// CLOSURES OVER LOCALS ARE A COMPILE ERROR HERE, deliberately and loudly. A
// nested function that reads an enclosing function's local needs upvalue
// cells to get the semantics right, and capturing by value instead would
// silently compute the wrong answer for the single most common closure idiom
// there is (a counter that mutates what it captured). Refusing with a clear
// message is worth far more than being subtly wrong; cells are the next
// piece of work. Free variables that are not enclosing locals resolve to
// globals, which is most real code.

export namespace ctbrowser::script {

namespace vp = ctjs::vp;

class compiler {
public:
	[[nodiscard]] static program compile(std::string_view source) {
		const vp::ast tree = vp::parse(source);
		program out;
		if (!tree.ok) {
			out.ok = false;
			out.error = "parse error: " + std::string{tree.error};
			return out;
		}
		compiler c{tree, out};
		c.compile_program();
		return out;
	}

private:
	struct local {
		std::string name;
		std::uint8_t reg;
	};
	struct frame {
		std::uint32_t proto = 0;
		std::vector<local> locals;
		std::vector<std::string> declared;    // pre-scanned; see collect_declared_names
		std::vector<std::size_t> scope_marks; // locals.size() at each scope entry
		std::uint8_t next_reg = 0;
		std::uint8_t high_water = 0;
	};

	compiler(const vp::ast & tree, program & out) : ast_(tree), out_(out) {}

	// --- AST access -------------------------------------------------------
	[[nodiscard]] const vp::node & at(std::int32_t i) const { return ast_.nodes[static_cast<std::size_t>(i)]; }
	[[nodiscard]] std::vector<std::int32_t> kids(const vp::node & n) const {
		std::vector<std::int32_t> out;
		for (std::int32_t i = 0; i < n.list_len; ++i) {
			out.push_back(ast_.pool[static_cast<std::size_t>(n.list + i)]);
		}
		return out;
	}

	// --- frames and registers ----------------------------------------------
	[[nodiscard]] frame & fn() { return frames_.back(); }
	[[nodiscard]] function_proto & proto() { return out_.functions[fn().proto]; }

	[[nodiscard]] std::uint8_t alloc_reg() {
		const std::uint8_t r = fn().next_reg++;
		if (fn().next_reg > fn().high_water) { fn().high_water = fn().next_reg; }
		return r;
	}
	void release_to(std::uint8_t mark) { fn().next_reg = mark; }
	[[nodiscard]] std::uint8_t reg_mark() const { return frames_.back().next_reg; }

	void push_scope() { fn().scope_marks.push_back(fn().locals.size()); }
	void pop_scope() {
		const std::size_t mark = fn().scope_marks.back();
		fn().scope_marks.pop_back();
		fn().locals.resize(mark);
	}
	[[nodiscard]] std::uint8_t declare_local(std::string name) {
		const std::uint8_t r = alloc_reg();
		fn().locals.push_back(local{std::move(name), r});
		return r;
	}
	// -1 when not a local of the CURRENT frame
	[[nodiscard]] int find_local(std::string_view name) const {
		const frame & f = frames_.back();
		for (std::size_t i = f.locals.size(); i-- > 0;) {
			if (f.locals[i].name == name) { return f.locals[i].reg; }
		}
		return -1;
	}
	// the diagnostic that makes the closure limitation loud instead of silent
	[[nodiscard]] bool is_enclosing_local(std::string_view name) const {
		// frame 0 is the script, whose declarations are globals - reachable
		// from anywhere, so never an enclosing-local problem
		for (std::size_t i = frames_.size() - 1; i-- > 1;) {
			for (const local & l : frames_[i].locals) {
				if (l.name == name) { return true; }
			}
			for (const std::string & d : frames_[i].declared) {
				if (d == name) { return true; }
			}
		}
		return false;
	}

	// The lexer hands back the RAW lexeme, quotes and all - `'a'` arrives as
	// three characters. Without this, every string literal in the program is
	// wrong by two characters, which shows up as 'a' + 'b' === "'a''b'" and
	// as o['a'] failing to find the property named a.
	[[nodiscard]] static std::string decode_string_literal(std::string_view lexeme) {
		if (lexeme.size() >= 2 && (lexeme.front() == '\'' || lexeme.front() == '"' ||
		                           lexeme.front() == '`')) {
			lexeme = lexeme.substr(1, lexeme.size() - 2);
		}
		std::string out;
		out.reserve(lexeme.size());
		for (std::size_t i = 0; i < lexeme.size(); ++i) {
			if (lexeme[i] != '\\' || i + 1 >= lexeme.size()) {
				out.push_back(lexeme[i]);
				continue;
			}
			switch (lexeme[++i]) {
			case 'n': out.push_back('\n'); break;
			case 't': out.push_back('\t'); break;
			case 'r': out.push_back('\r'); break;
			case '0': out.push_back('\0'); break;
			case 'b': out.push_back('\b'); break;
			case 'f': out.push_back('\f'); break;
			case 'v': out.push_back('\v'); break;
			default: out.push_back(lexeme[i]); break; // \\ \' \" and anything else
			}
		}
		return out;
	}

	// Names a nested function might close over. Collected BEFORE the body is
	// compiled, because function declarations hoist and are therefore compiled
	// before the `let` that a closure would capture has been reached - without
	// this pre-scan the enclosing-local check simply never fires.
	void collect_declared_names(std::int32_t body) {
		if (body < 0) { return; }
		const vp::node & n = at(body);
		if (n.kind == vp::nk::var_decl) {
			for (const std::int32_t d : kids(n)) { fn().declared.push_back(std::string{at(d).text}); }
			return;
		}
		if (n.kind == vp::nk::block || n.kind == vp::nk::program) {
			for (const std::int32_t s : kids(n)) { collect_declared_names(s); }
			return;
		}
		// loops and conditionals can declare too
		for (const std::int32_t slot : {n.a, n.b, n.c, n.d}) {
			if (slot >= 0 && at(slot).kind != vp::nk::func_decl &&
			    at(slot).kind != vp::nk::func_expr && at(slot).kind != vp::nk::arrow) {
				collect_declared_names(slot);
			}
		}
	}

	void fail(std::string message) {
		if (out_.ok) {
			out_.ok = false;
			out_.error = std::move(message);
		}
	}

	// --- entry --------------------------------------------------------------
	void compile_program() {
		out_.functions.emplace_back();
		out_.functions[0].name = "<script>";
		frames_.push_back(frame{0, {}, {}, {}, 0, 0});
		push_scope();

		const vp::node & root = at(ast_.root);
		collect_declared_names(ast_.root);
		// Function declarations hoist: a script may call one before its text.
		for (const std::int32_t s : kids(root)) {
			if (at(s).kind == vp::nk::func_decl) { compile_function_decl(s); }
		}
		for (const std::int32_t s : kids(root)) {
			if (at(s).kind != vp::nk::func_decl) { compile_stmt(s); }
		}
		proto().emit(instruction{op::ret_undef});
		proto().frame_size = fn().high_water;
		pop_scope();
		frames_.pop_back();
	}

	// --- statements ---------------------------------------------------------
	void compile_stmt(std::int32_t idx) {
		if (idx < 0 || !out_.ok) { return; }
		const vp::node & n = at(idx);
		const std::uint8_t mark = reg_mark();
		switch (n.kind) {
		case vp::nk::expr_stmt: {
			const std::uint8_t r = alloc_reg();
			compile_expr(n.a, r);
			break;
		}
		case vp::nk::var_decl:
			// TOP-LEVEL declarations become globals, not frame-0 registers.
			// Script scope is what functions declared alongside them close
			// over, and `let n = 0; function inc() { n = n + 1; }` is the most
			// common shape in JavaScript there is. Making them registers would
			// turn every one of those into the enclosing-local refusal below,
			// which would be correct and useless.
			if (frames_.size() == 1) {
				for (const std::int32_t d : kids(n)) {
					const vp::node & decl = at(d);
					const std::uint8_t mark = reg_mark();
					const std::uint8_t r = alloc_reg();
					if (decl.a >= 0) {
						compile_expr(decl.a, r);
					} else {
						proto().emit(instruction{op::load_undef, r});
					}
					const std::uint16_t name = proto().add_name(std::string{decl.text});
					proto().emit(instruction::with_bx(op::set_global, r, name));
					release_to(mark);
				}
				return;
			}
			for (const std::int32_t d : kids(n)) {
				const vp::node & decl = at(d);
				const std::uint8_t r = declare_local(std::string{decl.text});
				if (decl.a >= 0) {
					compile_expr(decl.a, r);
				} else {
					proto().emit(instruction{op::load_undef, r});
				}
				// a declared local keeps its register beyond this statement
				if (fn().next_reg <= r) { fn().next_reg = static_cast<std::uint8_t>(r + 1); }
			}
			return; // locals must NOT be released by the mark below
		case vp::nk::block:
			push_scope();
			for (const std::int32_t s : kids(n)) { compile_stmt(s); }
			pop_scope();
			break;
		case vp::nk::if_stmt: compile_if(n); break;
		case vp::nk::while_stmt: compile_while(n); break;
		case vp::nk::for_stmt: compile_for(n); break;
		case vp::nk::return_stmt:
			if (n.a >= 0) {
				const std::uint8_t r = alloc_reg();
				compile_expr(n.a, r);
				proto().emit(instruction{op::ret, r});
			} else {
				proto().emit(instruction{op::ret_undef});
			}
			break;
		case vp::nk::func_decl: compile_function_decl(idx); return;
		case vp::nk::empty: break;
		default: {
			// anything not yet handled is still an expression in most cases
			const std::uint8_t r = alloc_reg();
			compile_expr(idx, r);
			break;
		}
		}
		release_to(mark);
	}

	void compile_if(const vp::node & n) {
		const std::uint8_t mark = reg_mark();
		const std::uint8_t cond = alloc_reg();
		compile_expr(n.a, cond);
		const std::size_t to_else = proto().emit(instruction{op::jump_if_false, cond});
		release_to(mark);

		compile_stmt(n.b);
		if (n.c >= 0) {
			const std::size_t to_end = proto().emit(instruction{op::jump});
			patch_here(to_else);
			compile_stmt(n.c);
			patch_here(to_end);
		} else {
			patch_here(to_else);
		}
	}

	void compile_while(const vp::node & n) {
		const std::size_t top = proto().code.size();
		const std::uint8_t mark = reg_mark();
		const std::uint8_t cond = alloc_reg();
		compile_expr(n.a, cond);
		const std::size_t exit = proto().emit(instruction{op::jump_if_false, cond});
		release_to(mark);
		compile_stmt(n.b);
		patch_jump(proto().emit(instruction{op::jump}), top);
		patch_here(exit);
	}

	void compile_for(const vp::node & n) {
		push_scope();
		if (n.a >= 0) { compile_stmt(n.a); }
		const std::size_t top = proto().code.size();
		std::size_t exit = 0;
		bool has_cond = false;
		if (n.b >= 0) {
			const std::uint8_t mark = reg_mark();
			const std::uint8_t cond = alloc_reg();
			compile_expr(n.b, cond);
			exit = proto().emit(instruction{op::jump_if_false, cond});
			has_cond = true;
			release_to(mark);
		}
		compile_stmt(n.d);
		if (n.c >= 0) {
			const std::uint8_t mark = reg_mark();
			const std::uint8_t tmp = alloc_reg();
			compile_expr(n.c, tmp);
			release_to(mark);
		}
		patch_jump(proto().emit(instruction{op::jump}), top);
		if (has_cond) { patch_here(exit); }
		pop_scope();
	}

	void compile_function_decl(std::int32_t idx) {
		const vp::node & n = at(idx);
		const std::uint32_t index = compile_function_body(idx, std::string{n.text});
		const std::uint8_t r = alloc_reg();
		proto().emit(instruction::with_bx(op::closure, r, static_cast<std::uint16_t>(index)));
		const std::uint16_t name = proto().add_name(std::string{n.text});
		proto().emit(instruction::with_bx(op::set_global, r, name));
		release_to(static_cast<std::uint8_t>(r));
	}

	[[nodiscard]] std::uint32_t compile_function_body(std::int32_t idx, std::string name) {
		const vp::node & n = at(idx);
		const auto index = static_cast<std::uint32_t>(out_.functions.size());
		out_.functions.emplace_back();
		out_.functions[index].name = std::move(name);

		frames_.push_back(frame{index, {}, {}, {}, 0, 0});
		push_scope();
		const std::vector<std::int32_t> params = kids(n);
		for (const std::int32_t p : params) { (void)declare_local(std::string{at(p).text}); }
		collect_declared_names(n.a);
		out_.functions[index].param_count = static_cast<std::uint8_t>(params.size());

		const std::int32_t body = n.a;
		if (body >= 0 && at(body).kind == vp::nk::block) {
			for (const std::int32_t s : kids(at(body))) {
				if (at(s).kind == vp::nk::func_decl) { compile_stmt(s); }
			}
			for (const std::int32_t s : kids(at(body))) {
				if (at(s).kind != vp::nk::func_decl) { compile_stmt(s); }
			}
			proto().emit(instruction{op::ret_undef});
		} else if (body >= 0) {
			// concise arrow body: `x => expr` returns expr
			const std::uint8_t r = alloc_reg();
			compile_expr(body, r);
			proto().emit(instruction{op::ret, r});
		} else {
			proto().emit(instruction{op::ret_undef});
		}
		out_.functions[index].frame_size = fn().high_water;
		pop_scope();
		frames_.pop_back();
		return index;
	}

	// --- expressions ---------------------------------------------------------
	void compile_expr(std::int32_t idx, std::uint8_t dst) {
		if (idx < 0 || !out_.ok) {
			proto().emit(instruction{op::load_undef, dst});
			return;
		}
		const vp::node & n = at(idx);
		switch (n.kind) {
		case vp::nk::num: {
			double d = 0;
			const std::string lex{n.text};
			std::from_chars(lex.data(), lex.data() + lex.size(), d);
			emit_const(dst, value::number(d));
			break;
		}
		case vp::nk::str:
			proto().emit(instruction::with_bx(op::load_string, dst,
			                                  proto().add_string(decode_string_literal(n.text))));
			break;
		case vp::nk::true_lit: proto().emit(instruction{op::load_true, dst}); break;
		case vp::nk::false_lit: proto().emit(instruction{op::load_false, dst}); break;
		case vp::nk::null_lit: proto().emit(instruction{op::load_null, dst}); break;
		case vp::nk::ident: compile_ident(n, dst); break;
		case vp::nk::binary: compile_binary(n, dst); break;
		case vp::nk::logical: compile_logical(n, dst); break;
		case vp::nk::unary: compile_unary(n, dst); break;
		case vp::nk::assign: compile_assign(n, dst); break;
		case vp::nk::update: compile_update(n, dst); break;
		case vp::nk::ternary: compile_ternary(n, dst); break;
		case vp::nk::member: {
			compile_expr(n.a, dst);
			const std::uint16_t name = proto().add_name(std::string{n.text});
			proto().emit(instruction{op::get_prop, dst, dst, static_cast<std::uint8_t>(name)});
			break;
		}
		case vp::nk::index: {
			const std::uint8_t mark = reg_mark();
			compile_expr(n.a, dst);
			const std::uint8_t key = alloc_reg();
			compile_expr(n.b, key);
			proto().emit(instruction{op::get_index, dst, dst, key});
			release_to(mark > dst ? mark : static_cast<std::uint8_t>(dst + 1));
			break;
		}
		case vp::nk::call: compile_call(n, dst); break;
		case vp::nk::array: compile_array(n, dst); break;
		case vp::nk::object: compile_object(n, dst); break;
		case vp::nk::func_expr:
		case vp::nk::arrow: {
			const std::uint32_t index = compile_function_body(idx, std::string{n.text});
			proto().emit(instruction::with_bx(op::closure, dst, static_cast<std::uint16_t>(index)));
			break;
		}
		case vp::nk::this_lit: proto().emit(instruction{op::load_undef, dst}); break;
		default:
			fail("unsupported syntax in this VM subset (AST kind " +
			     std::to_string(static_cast<int>(n.kind)) + ")");
			proto().emit(instruction{op::load_undef, dst});
			break;
		}
	}

	void compile_ident(const vp::node & n, std::uint8_t dst) {
		const int local_reg = find_local(n.text);
		if (local_reg >= 0) {
			proto().emit(instruction{op::move, dst, static_cast<std::uint8_t>(local_reg)});
			return;
		}
		if (is_enclosing_local(n.text)) {
			fail("closure over the enclosing local '" + std::string{n.text} +
			     "' needs upvalue cells, which this VM does not have yet - "
			     "refusing rather than capturing by value and computing the wrong answer");
			proto().emit(instruction{op::load_undef, dst});
			return;
		}
		const std::uint16_t name = proto().add_name(std::string{n.text});
		proto().emit(instruction::with_bx(op::get_global, dst, name));
	}

	void compile_binary(const vp::node & n, std::uint8_t dst) {
		const std::uint8_t mark = reg_mark();
		const std::uint8_t lhs = alloc_reg();
		const std::uint8_t rhs = alloc_reg();
		compile_expr(n.a, lhs);
		compile_expr(n.b, rhs);
		const std::string_view o = n.text;
		op code = op::add_generic;
		if (o == "+") { code = op::add_generic; }
		else if (o == "-") { code = op::sub; }
		else if (o == "*") { code = op::mul; }
		else if (o == "/") { code = op::div; }
		else if (o == "%") { code = op::mod; }
		else if (o == "**") { code = op::pow; }
		else if (o == "===") { code = op::equal; }
		else if (o == "!==") { code = op::not_equal; }
		else if (o == "==") { code = op::loose_equal; }
		else if (o == "!=") { code = op::not_equal; }
		else if (o == "<") { code = op::less; }
		else if (o == "<=") { code = op::less_equal; }
		else if (o == ">") { code = op::greater; }
		else if (o == ">=") { code = op::greater_equal; }
		else { fail("unsupported binary operator '" + std::string{o} + "'"); }
		proto().emit(instruction{code, dst, lhs, rhs});
		release_to(mark);
	}

	// && and || must not evaluate the right side unless they have to, so they
	// are control flow rather than an opcode.
	void compile_logical(const vp::node & n, std::uint8_t dst) {
		compile_expr(n.a, dst);
		const bool is_and = n.text == "&&";
		const std::size_t skip =
		    proto().emit(instruction{is_and ? op::jump_if_false : op::jump_if_true, dst});
		compile_expr(n.b, dst);
		patch_here(skip);
	}

	void compile_unary(const vp::node & n, std::uint8_t dst) {
		const std::uint8_t mark = reg_mark();
		const std::uint8_t operand = alloc_reg();
		compile_expr(n.a, operand);
		if (n.text == "-") { proto().emit(instruction{op::negate, dst, operand}); }
		else if (n.text == "!") { proto().emit(instruction{op::logical_not, dst, operand}); }
		else if (n.text == "typeof") { proto().emit(instruction{op::type_of, dst, operand}); }
		else if (n.text == "+") { proto().emit(instruction{op::move, dst, operand}); }
		else { fail("unsupported unary operator '" + std::string{n.text} + "'"); }
		release_to(mark);
	}

	void compile_assign(const vp::node & n, std::uint8_t dst) {
		const vp::node & target = at(n.a);
		if (n.text != "=") {
			fail("compound assignment '" + std::string{n.text} + "' is not in this VM subset yet");
			return;
		}
		if (target.kind == vp::nk::ident) {
			const int local_reg = find_local(target.text);
			if (local_reg >= 0) {
				compile_expr(n.b, static_cast<std::uint8_t>(local_reg));
				proto().emit(instruction{op::move, dst, static_cast<std::uint8_t>(local_reg)});
				return;
			}
			compile_expr(n.b, dst);
			const std::uint16_t name = proto().add_name(std::string{target.text});
			proto().emit(instruction::with_bx(op::set_global, dst, name));
			return;
		}
		if (target.kind == vp::nk::member) {
			const std::uint8_t mark = reg_mark();
			const std::uint8_t obj = alloc_reg();
			compile_expr(target.a, obj);
			compile_expr(n.b, dst);
			const std::uint16_t name = proto().add_name(std::string{target.text});
			proto().emit(instruction{op::set_prop, obj, static_cast<std::uint8_t>(name), dst});
			release_to(mark);
			return;
		}
		if (target.kind == vp::nk::index) {
			const std::uint8_t mark = reg_mark();
			const std::uint8_t obj = alloc_reg();
			const std::uint8_t key = alloc_reg();
			compile_expr(target.a, obj);
			compile_expr(target.b, key);
			compile_expr(n.b, dst);
			proto().emit(instruction{op::set_index, obj, key, dst});
			release_to(mark);
			return;
		}
		fail("unsupported assignment target");
	}

	void compile_update(const vp::node & n, std::uint8_t dst) {
		const vp::node & target = at(n.a);
		if (target.kind != vp::nk::ident) {
			fail("++/-- is only supported on plain variables in this VM subset");
			return;
		}
		const int local_reg = find_local(target.text);
		const std::uint8_t mark = reg_mark();
		const std::uint8_t cur = alloc_reg();
		const std::uint8_t one = alloc_reg();
		if (local_reg >= 0) {
			proto().emit(instruction{op::move, cur, static_cast<std::uint8_t>(local_reg)});
		} else {
			const std::uint16_t name = proto().add_name(std::string{target.text});
			proto().emit(instruction::with_bx(op::get_global, cur, name));
		}
		emit_const(one, value::number(1));
		// postfix yields the OLD value, prefix the new one
		if (n.b == 0) { proto().emit(instruction{op::move, dst, cur}); }
		const op code = n.text == "++" ? op::add : op::sub;
		proto().emit(instruction{code, cur, cur, one});
		if (n.b != 0) { proto().emit(instruction{op::move, dst, cur}); }
		if (local_reg >= 0) {
			proto().emit(instruction{op::move, static_cast<std::uint8_t>(local_reg), cur});
		} else {
			const std::uint16_t name = proto().add_name(std::string{target.text});
			proto().emit(instruction::with_bx(op::set_global, cur, name));
		}
		release_to(mark);
	}

	void compile_ternary(const vp::node & n, std::uint8_t dst) {
		const std::uint8_t mark = reg_mark();
		const std::uint8_t cond = alloc_reg();
		compile_expr(n.a, cond);
		const std::size_t to_alt = proto().emit(instruction{op::jump_if_false, cond});
		release_to(mark);
		compile_expr(n.b, dst);
		const std::size_t to_end = proto().emit(instruction{op::jump});
		patch_here(to_alt);
		compile_expr(n.c, dst);
		patch_here(to_end);
	}

	// Calls need their arguments in CONSECUTIVE registers starting just above
	// the callee, so the VM can hand the callee a contiguous frame.
	void compile_call(const vp::node & n, std::uint8_t dst) {
		const std::vector<std::int32_t> args = kids(n);
		const vp::node & callee = at(n.a);
		const std::uint8_t mark = reg_mark();
		const std::uint8_t base = alloc_reg();

		if (callee.kind == vp::nk::member) {
			compile_expr(callee.a, base); // the receiver
			for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
			const std::uint16_t name = proto().add_name(std::string{callee.text});
			proto().emit(instruction{op::call_method, base, static_cast<std::uint8_t>(args.size()),
			                         static_cast<std::uint8_t>(name)});
		} else {
			compile_expr(n.a, base);
			for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
			proto().emit(instruction{op::call, base, static_cast<std::uint8_t>(args.size())});
		}
		proto().emit(instruction{op::move, dst, base});
		release_to(mark);
	}

	void compile_array(const vp::node & n, std::uint8_t dst) {
		proto().emit(instruction{op::new_array, dst});
		const std::uint8_t mark = reg_mark();
		for (const std::int32_t element : kids(n)) {
			const std::uint8_t v = alloc_reg();
			compile_expr(element, v);
			proto().emit(instruction{op::append, dst, v});
			release_to(mark);
		}
	}

	void compile_object(const vp::node & n, std::uint8_t dst) {
		proto().emit(instruction{op::new_object, dst});
		const std::uint8_t mark = reg_mark();
		for (const std::int32_t p : kids(n)) {
			const vp::node & prop = at(p);
			if (prop.kind != vp::nk::prop || (prop.d & 1) != 0) {
				fail("computed and spread object keys are not in this VM subset yet");
				return;
			}
			const std::uint8_t v = alloc_reg();
			if (prop.b >= 0) {
				compile_expr(prop.b, v);
			} else {
				compile_ident(prop, v); // shorthand { x }
			}
			const std::uint16_t name = proto().add_name(decode_string_literal(prop.text));
			proto().emit(instruction{op::set_prop, dst, static_cast<std::uint8_t>(name), v});
			release_to(mark);
		}
	}

	// --- helpers -------------------------------------------------------------
	void emit_const(std::uint8_t dst, value v) {
		proto().emit(instruction::with_bx(op::load_const, dst, proto().add_constant(v)));
	}

	void patch_here(std::size_t at_index) {
		patch_jump(at_index, proto().code.size());
	}
	void patch_jump(std::size_t at_index, std::size_t target) {
		const auto offset = static_cast<std::int32_t>(target) - static_cast<std::int32_t>(at_index) - 1;
		instruction & jump = proto().code[at_index];
		const auto narrow = static_cast<std::uint16_t>(static_cast<std::int16_t>(offset));
		jump.b = static_cast<std::uint8_t>(narrow >> 8);
		jump.c = static_cast<std::uint8_t>(narrow & 0xFF);
	}

	const vp::ast & ast_;
	program & out_;
	std::vector<frame> frames_;
};

} // namespace ctbrowser::script
