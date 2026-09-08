// early errors - expressions: assignment targets, `__proto__` duplicates,
// numeric literals, `delete`, `super` and `new.target`, and the walk itself.
//
// One of six files carved out of a 1,476-line compile/early_errors.cpp on
// 2026-09-08 - the checker was one class with every member inline, as
// compile.cpp was before it became compile/. The class is declared in
// checker.hpp beside this, in ctbrowser::script::detail::early; the bodies are
// where they were, in the file that owns the concern. early_errors.hpp, the
// public face of the pass, did not change.

#include "checker.hpp"

namespace ctbrowser::script::detail::early {

namespace {

// The kinds walk_statement handles by name. Anything else in statement position
// is an expression - and anything in EXPRESSION position that turns out to be
// one of these is a routing mistake, so walk_expression sends it back rather
// than walking a block as if it were an operand.
[[nodiscard]] bool is_statement(nk kind) {
    switch (kind) {
    case nk::block:
    case nk::var_decl:
    case nk::empty:
    case nk::expr_stmt:
    case nk::if_stmt:
    case nk::for_stmt:
    case nk::forof_stmt:
    case nk::while_stmt:
    case nk::do_stmt:
    case nk::return_stmt:
    case nk::break_stmt:
    case nk::continue_stmt:
    case nk::throw_stmt:
    case nk::labeled:
    case nk::try_stmt:
    case nk::switch_stmt:
    case nk::func_decl: return true;
    default: return false;
    }
}

// WHICH OF A NODE'S FOUR FIXED SLOTS ARE REALLY CHILDREN. The same table
// compiler_impl::child_slots keeps, plus `update` - whose `b` is 1 for a prefix
// operator and 0 for a postfix one, and is a NODE INDEX to anything that does
// not know that.
[[nodiscard]] std::array<std::int32_t, 4> slots(const vp::node & n) {
    switch (n.kind) {
    case nk::param:
    case nk::prop:
    case nk::pattern_prop:
    case nk::class_member: return {n.a, n.b, -1, -1};
    case nk::func_decl:
    case nk::func_expr:
    case nk::arrow: return {n.a, -1, -1, -1};
    case nk::update: return {n.a, -1, -1, -1};
    case nk::new_expr: return {n.a, -1, -1, -1};
    case nk::forof_stmt: return {n.a, n.b, n.c, -1};
    case nk::case_clause: return {n.a, -1, -1, -1};
    case nk::import_decl:
    case nk::import_meta: return {-1, -1, -1, -1};
    case nk::import_spec:
    case nk::export_decl:
    case nk::export_spec:
    case nk::dynamic_import: return {n.a, -1, -1, -1};
    default: return {n.a, n.b, n.c, n.d};
    }
}

} // namespace

// --- expressions ---------------------------------------------------------------

// A target a value may be assigned TO. 13.15.1 refuses everything else, and
// "everything else" is most of the grammar: a literal, a call, a sequence,
// an operator expression, an arrow, an optional chain.
[[nodiscard]] bool checker::simple_target(std::int32_t idx) const {
    switch (at(idx).kind) {
    case nk::ident:
    case nk::member:
    case nk::index: return true;
    default: return false;
    }
}

[[nodiscard]] bool checker::destructuring(nk kind) {
    return kind == nk::array || kind == nk::object || kind == nk::array_pattern ||
           kind == nk::object_pattern;
}

// A CALL TARGET, AND THE ONE PLACE THIS PASS IS NOT THE BEST ANSWER.
//
// "Runtime Errors for Function Call Assignment Targets" is a normative
// OPTIONAL clause: an implementation that is a web browser may answer
// ~web-compat~ for `f() = 1` in non-strict code and evaluate it to a
// runtime ReferenceError instead of refusing the source. Five annexB tests
// assert exactly that, and a browser is what this engine is.
//
// It is refused here anyway, because the alternative is not available:
// `compiler_impl::prepare_reference` already fails outright on a call
// target, so the whole script is refused either way and the only thing
// this pass changes is WHICH kind of failure it is - a SyntaxError rather
// than "the compiler does not implement this", which is the truer of the
// two. Emitting the ReferenceError instead is a compiler change (evaluate
// the call, throw, and do not touch the right-hand side) and is the right
// follow-up; until then those five tests fail as they already did.
void checker::check_assignment(std::int32_t idx) {
    const vp::node & n = at(idx);
    const nk target = at(n.a).kind;
    // `=` also accepts an ArrayLiteral or ObjectLiteral, which is then
    // reinterpreted as a destructuring pattern (13.15.5). No other
    // operator does: `[a] += b` is an error.
    if (n.text == "=" && destructuring(target)) { return; }
    if (simple_target(n.a)) { return; }
    report("the left side of `" + std::string{n.text} +
               "` is not something a value can be "
               "assigned to",
           n.a >= 0 ? n.a : idx);
}

void checker::check_update(std::int32_t idx) {
    const vp::node & n = at(idx);
    if (simple_target(n.a)) { return; }
    report("the operand of `" + std::string{n.text} +
               "` is not something a value can be assigned to",
           n.a >= 0 ? n.a : idx);
}

// 13.2.5.1 with B.3.1: an object literal may not carry two plain-data
// `__proto__` properties, because each of them would set the prototype.
// A shorthand, a method, an accessor and a computed key are all excluded -
// none of them is the prototype-setting form.
// IS THIS PROPERTY'S NAME `__proto__`? Written plainly, or QUOTED: the rule
// is about the PropName, and `'__proto__': null` names the same thing that
// `__proto__: null` does. Only a COMPUTED key is exempt, because its name is
// not known until it is evaluated.
//
// The parser routes a quoted key down the same slot a computed one uses -
// `d` bit 0 set, `a` holding the literal - so the two are told apart by
// what is in `a`. A key with an escape in it is left alone rather than
// cooked here: missing one is a miss, and cooking it wrongly would be a
// refusal of source nobody wrote.
[[nodiscard]] bool checker::names_proto(const vp::node & prop) const {
    if ((prop.d & 1) == 0) { return prop.text == "__proto__"; }
    const vp::node & key = at(prop.a);
    if (key.kind != nk::str || key.text.size() < 2) { return false; }
    if (bracketed(prop.a)) { return false; }
    const std::string_view inner = key.text.substr(1, key.text.size() - 2);
    return inner == "__proto__";
}

// IS THIS KEY IN BRACKETS? The parser gives a QUOTED key and a COMPUTED one
// the same shape - `d` bit 0 set and `a` holding the expression - and for a
// computed key that happens to be a string literal the two are identical in
// the tree. `{ '__proto__': null }` sets the prototype and
// `{ ['__proto__']: null }` defines an ordinary property, so the difference
// decides whether the duplicate rule applies at all, and the only place it
// survives is the source.
[[nodiscard]] bool checker::bracketed(std::int32_t key) const {
    const std::size_t where = offset_of(key);
    if (where == early_error::nowhere) { return false; }
    for (std::size_t i = where; i-- > 0;) {
        const char c = source_[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') { continue; }
        return c == '[';
    }
    return false;
}

void checker::check_proto_duplicates(std::int32_t idx) {
    std::size_t seen = 0;
    std::int32_t second = -1;
    for (const std::int32_t p : kids(at(idx))) {
        const vp::node & prop = at(p);
        if (prop.kind != nk::prop) { continue; }
        const bool plain_data = prop.c != 1 && prop.c != 2 && prop.c != 3;
        if (!plain_data || !names_proto(prop)) { continue; }
        ++seen;
        if (seen == 2) { second = p; }
    }
    if (seen > 1) { report("an object literal may not set `__proto__` twice", second); }
}

// A RESERVED WORD USED AS AN IDENTIFIER IS *NOT* CHECKED, and this is the
// one rule that was written, measured, and then TAKEN OUT AGAIN. It is
// recorded here so it is not attempted a second time from the same
// reasoning.
//
// 13.1.1 reserves `const`, `return`, `class` and twenty more in every
// context, and ctjs's parser reads a keyword in expression position as a
// plain name - which it HAS to, because `of`, `get`, `set`, `static`,
// `async`, `let`, `await` and `yield` are contextual and `const of = 1` is
// valid JavaScript. Refusing the unconditionally-reserved ones scored +52
// negative parse tests and refused NOTHING in the 41,163 valid test262
// files.
//
// It refused p5.js. The bundle contains
//
//     if (strandsContext._builtinGlobalsAccessorsInstalled) return
//     const getRuntimeP5Instance = () => ...
//
// and this parser has no automatic semicolon insertion after a bare
// `return`, so it reads the `const` as that return's OPERAND - an
// identifier named `const`. The rule is therefore not a rule about the
// language here, it is a rule about a parser gap, and any statement
// following an argument-less `return` at the end of a line can be caught by
// it. That is a page that does not load, and the whole point of this file
// is not to be that.

// A NUMERIC LITERAL'S OWN GRAMMAR, 12.9.3, read back off the lexeme.
//
// The lexer is deliberately total: it takes `0` followed by `x`, `o` or `b`
// and then EVERY identifier character, and a decimal run of digits, dots
// and underscores, without asking whether the result is a number. That is
// the right shape for a lexer - `0o17` used to lex as `0` followed by the
// identifier `o17` - and it leaves `0b2`, `0x`, `1__0` and `1.5n` as tokens
// that parse and are not literals.
//
// WHAT IS NOT CHECKED: a legacy octal (`01`) and a non-octal decimal (`08`).
// Both are legal sloppy JavaScript and only an error in strict mode, which
// this engine does not have.
void checker::check_number(std::int32_t idx) {
    const std::string_view text = at(idx).text;
    if (text.empty()) { return; }
    const bool bigint = text.back() == 'n';
    const std::string_view body = bigint ? text.substr(0, text.size() - 1) : text;
    if (body.empty()) {
        report("`" + std::string{text} + "` is not a number", idx);
        return;
    }
    const auto decimal = [](char c) { return c >= '0' && c <= '9'; };
    if (body.size() >= 2 && body[0] == '0' &&
        (body[1] == 'x' || body[1] == 'X' || body[1] == 'o' || body[1] == 'O' || body[1] == 'b' ||
         body[1] == 'B')) {
        const char radix = body[1];
        const auto belongs = [radix, decimal](char c) {
            if (radix == 'b' || radix == 'B') { return c == '0' || c == '1'; }
            if (radix == 'o' || radix == 'O') { return c >= '0' && c <= '7'; }
            return decimal(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        };
        const std::string_view digits = body.substr(2);
        if (digits.empty()) {
            report("`" + std::string{text} + "` has no digits after its prefix", idx);
            return;
        }
        for (std::size_t i = 0; i < digits.size(); ++i) {
            // 12.9.3: a separator goes BETWEEN two digits and nowhere else.
            if (digits[i] == '_') {
                if (i == 0 || i + 1 == digits.size() || !belongs(digits[i - 1]) ||
                    !belongs(digits[i + 1])) {
                    report("`" + std::string{text} + "` has a `_` that is not between two digits",
                           idx);
                    return;
                }
                continue;
            }
            if (!belongs(digits[i])) {
                report("`" + std::string{text} + "` has a digit its radix does not have", idx);
                return;
            }
        }
        return;
    }
    // A BigInt IS AN INTEGER (12.9.3): no fraction and no exponent, and no
    // leading zero - `01n` and `08n` are not legacy anything, they are
    // errors even in sloppy code.
    if (bigint) {
        if (body.find('.') != std::string_view::npos || body.find('e') != std::string_view::npos ||
            body.find('E') != std::string_view::npos) {
            report("`" + std::string{text} + "` is not an integer, so it cannot be a BigInt", idx);
            return;
        }
        if (body.size() > 1 && body[0] == '0') {
            report("`" + std::string{text} + "` has a leading zero, which a BigInt may not", idx);
            return;
        }
    }
    // TWO DECIMAL POINTS ARE NOT CHECKED, and the reason is `0..toString(2)`
    // - which is legal JavaScript, is how a page calls a method on a
    // numeric literal, and arrives here as the single lexeme `0..` because
    // the lexer takes every dot it can. Four test262 files are that shape.
    // Refusing it would be refusing valid source to catch a case the parser
    // already fails on.
    // A LEGACY OCTAL OR NON-OCTAL DECIMAL TAKES NO SEPARATORS. `01` and `08`
    // are legal sloppy and `0_1` is not: the separator is only in the
    // modern productions.
    // `body[1] == '_'` COUNTS AS ONE. After a leading `0` the only things
    // the grammar admits are a radix prefix, a `.`, an exponent and a
    // legacy octal digit run - so `0_1` is a legacy literal with a
    // separator in it rather than a modern literal that happens to start
    // with a zero.
    const bool legacy = body.size() > 1 && body[0] == '0' && (decimal(body[1]) || body[1] == '_') &&
                        body.find('.') == std::string_view::npos &&
                        body.find('e') == std::string_view::npos &&
                        body.find('E') == std::string_view::npos;
    if (legacy && body.find('_') != std::string_view::npos) {
        report("`" + std::string{text} + "` is a legacy octal literal and may not use `_`", idx);
        return;
    }
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (body[i] != '_') { continue; }
        if (i == 0 || i + 1 == body.size() || !decimal(body[i - 1]) || !decimal(body[i + 1])) {
            report("`" + std::string{text} + "` has a `_` that is not between two digits", idx);
            return;
        }
    }
}

// 13.5.1.1: `delete` of a private member is an error wherever it appears -
// no strict mode needed, unlike `delete` of a plain identifier.
void checker::check_delete(std::int32_t operand) {
    const vp::node & n = at(operand);
    if ((n.kind == nk::member || n.kind == nk::opt_member) && n.text.starts_with('#')) {
        report("`delete` of the private member " + quoted(n.text) + " is not allowed", operand);
    }
}

// The frame `new.target` and `super` are answered against: the nearest one
// that is not an arrow, since an arrow has neither of its own.
[[nodiscard]] const frame & checker::enclosing_non_arrow_frame() const {
    for (std::size_t i = frames_.size(); i-- > 0;) {
        if (frames_[i].what != frame_kind::arrow) { return frames_[i]; }
    }
    return frames_.front();
}

[[nodiscard]] frame_kind checker::enclosing_non_arrow() const {
    return enclosing_non_arrow_frame().what;
}

void checker::walk_expression(std::int32_t idx) {
    if (idx < 0 || depth_ >= max_depth) { return; }
    const deeper nesting{depth_};
    const vp::node & n = at(idx);
    switch (n.kind) {
    case nk::assign:
        check_assignment(idx);
        if (n.text == "=" && destructuring(at(n.a).kind)) {
            walk_pattern(n.a);
        } else {
            walk_expression(n.a);
        }
        walk_expression(n.b);
        return;

    case nk::update:
        check_update(idx);
        walk_expression(n.a);
        return;

    case nk::unary:
        if (n.text == "delete") { check_delete(n.a); }
        walk_expression(n.a);
        return;

    case nk::arrow:
        // AN ARROW INHERITS `super`, so the constructor's permission has to
        // travel into it: `constructor() { const f = () => super(); }` is
        // legal and is how a derived class defers the call.
        check_function(idx, frame_kind::arrow, enclosing_non_arrow_frame().super_call_ok);
        return;
    case nk::func_expr:
    case nk::func_decl: check_function(idx, frame_kind::function); return;
    case nk::class_decl: check_class(idx); return;

    case nk::call:
        // `super(...)` IS NOT A CALL OF THE VALUE `super`. It is its own
        // production, and 15.7.1 admits it in exactly one place: the
        // constructor of a class with a heritage. Handled here rather than
        // in the `super_lit` arm below, which would otherwise report the
        // callee as a `super` outside a method.
        if (at(n.a).kind == nk::super_lit) {
            if (!enclosing_non_arrow_frame().super_call_ok) {
                report("`super()` outside the constructor of a derived class", idx);
            }
            for (const std::int32_t argument : kids(n)) { walk_expression(argument); }
            return;
        }
        break;

    case nk::object:
        check_proto_duplicates(idx);
        for (const std::int32_t p : kids(n)) { walk_property(p); }
        return;

    case nk::num: check_number(idx); return;

    case nk::new_target:
        // NOT CHECKED, AND IT IS THE SAME DEVIATION AS TOP-LEVEL `return`
        // ABOVE - the second half of one decision rather than a second one.
        //
        // 16.1.1 says a Script may not contain `new.target`: the grammar
        // admits it only inside a function, exactly as it admits `return`
        // only inside one. This engine's embedding contract is that the top
        // level IS a function body - `run_result::returned` is what
        // `return` at the top level hands back, and `unittests/js` compiles
        // `return (expr);` for every expression it checks. Refusing
        // `new.target` there while accepting `return` there refuses the
        // engine's own calling convention halfway: `return String(new.target)`
        // is a line in `unittests/js/vm_objects.cpp`, it is the shape a page's
        // transpiled guard takes, and the VM answers it correctly with
        // `undefined` - there is no constructor at the top level, which is
        // what `undefined` means.
        //
        // The honest fix is the one the `return` note names: a `script_kind`
        // that says "this source is a function body", passed by every
        // embedder. Until there is one, these two agree.
        //
        // `super` below is NOT this case and stays refused. It needs a home
        // object, which no top level has under any reading of the contract,
        // and nothing in this engine's API hands one back.
        return;

    case nk::super_lit: {
        // 16.1.1 again, and 15.7.1: `super` needs a home object, which only
        // a method, an accessor, a constructor or a field initialiser has.
        const frame_kind where = enclosing_non_arrow();
        if (where == frame_kind::script || where == frame_kind::function) {
            report("`super` outside a method", idx);
        }
        return;
    }

    // A template's `${...}` holes are raw text inside one token; the
    // compiler parses them separately, so there is nothing here to walk.
    case nk::tmpl: return;

    default: break;
    }
    if (is_statement(n.kind)) {
        std::vector<binding> ignored;
        walk_statement(idx, list_kind::block, ignored);
        return;
    }
    for (const std::int32_t slot : slots(n)) { walk_expression(slot); }
    for (const std::int32_t k : kids(n)) { walk_expression(k); }
}

// One entry of an object literal. A method or an accessor is a function
// with a home object; a plain value is an expression.
void checker::walk_property(std::int32_t idx) {
    const vp::node & n = at(idx);
    if (n.kind != nk::prop) {
        walk_expression(idx);
        return;
    }
    if ((n.d & 1) != 0) { walk_expression(n.a); } // a computed key
    if (n.c == 1 || n.c == 3) {
        check_function(n.b, frame_kind::method);
        return;
    }
    walk_expression(n.b);
}

} // namespace ctbrowser::script::detail::early
