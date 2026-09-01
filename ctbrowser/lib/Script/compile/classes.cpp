// compiler_impl - classes.
//
// `class` declarations and expressions, their fields and their
// constructors. It lived in the expressions section, at 200 lines inside a
// file of 1,353.
//
// One of the files carved out of a 3,845-line compile.cpp on 2026-08-09.
// The class is declared whole in compiler_impl.hpp beside this.

#include "compiler_impl.hpp"

namespace ctbrowser::script::detail {

std::uint32_t compiler_impl::compile_field_initialiser(const std::vector<std::int32_t> & fields) {
    const std::uint32_t index = new_proto(offset_of(fields.empty() ? -1 : fields.front()));
    out_.functions[index].name = "<fields>";

    frames_.emplace_back();
    frames_.back().proto = index;
    push_scope();
    // A FUNCTION BODY IS NOT PART OF THE CHAIN THAT ENCLOSES IT.
    //
    // `a?.b(() => c?.d)` compiles the arrow while the outer chain is open,
    // so without this the arrow's own short-circuit would be recorded on
    // the outer chain's exit list - and patched into the ENCLOSING proto's
    // code array, at an index that means something else entirely there.
    const bool saved_in_chain = in_chain_;
    std::vector<std::size_t> saved_exits;
    saved_exits.swap(optional_exits_);
    in_chain_ = false;
    for (const std::int32_t member : fields) {
        const vp::node & m = at(member);
        const std::uint32_t mark = reg_mark();
        const std::uint16_t self = alloc_reg();
        proto().emit(instruction{op::load_this, self});
        const std::uint16_t v = alloc_reg();
        // `class A { x; }` declares x and gives it undefined - a field
        // without an initialiser is still a field.
        if (m.b >= 0) {
            compile_expr(m.b, v);
        } else {
            proto().emit(instruction{op::load_undef, v});
        }
        if ((m.d & 2) != 0 && m.a >= 0) { // a computed key: `[expr] = init`
            const std::uint16_t key = alloc_reg();
            compile_expr(m.a, key);
            proto().emit(instruction{op::set_index, self, key, v});
        } else {
            proto().emit(instruction{op::set_prop, self, name_operand(std::string{m.text}), v});
        }
        release_to(mark);
    }
    proto().emit(instruction{op::ret_undef});
    finish_frame(index, 0);
    pop_scope();
    frames_.pop_back();
    in_chain_ = saved_in_chain;
    optional_exits_.swap(saved_exits);
    return index;
}

void compiler_impl::declare_class_name(std::string name, bool force) {
    if ((force || frames_.size() > 1) && !was_predeclared(name) &&
        find_local_in_current_scope(name) == nullptr) {
        const std::uint16_t reg = declare_local(name);
        proto().emit(instruction{op::load_undef, reg});
        if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, reg}); }
    }
}

void compiler_impl::compile_class(const vp::node & n, std::uint16_t dst, bool as_declaration) {
    // The name is DECLARED first, before any method body is compiled, so a
    // method that mentions it resolves to this binding rather than to an
    // outer one - capture is decided when the nested function is compiled,
    // not when it runs. The value is written further down, as soon as the
    // class exists.
    // A scope of its own for an expression's name, so the methods can capture
    // it and the code after the expression cannot see it.
    const bool own_scope = !as_declaration && !n.text.empty();
    if (own_scope) { push_scope(); }
    if (!n.text.empty()) { declare_class_name(std::string{n.text}, own_scope); }
    const std::span<const std::int32_t> members = kids(n);
    const std::uint32_t mark = reg_mark();
    const std::uint16_t prototype_reg = alloc_reg();
    proto().emit(instruction{op::new_object, prototype_reg});

    if (n.a >= 0) {
        // `extends`: the parent's prototype becomes this one's, so a lookup
        // that misses here walks up to it.
        const std::uint16_t parent = alloc_reg();
        compile_expr(n.a, parent);
        const std::uint16_t parent_proto = alloc_reg();
        proto().emit(instruction{op::get_prop, parent_proto, parent, name_operand("prototype")});
        proto().emit(instruction{op::set_proto, prototype_reg, parent_proto});
    }

    std::int32_t constructor_body = -1;
    for (const std::int32_t member : members) {
        const vp::node & m = at(member);
        if (m.c == 1 && m.text == "constructor") { constructor_body = m.b; }
    }

    if (constructor_body >= 0) {
        // Named after the CLASS. A constructor is a function expression, so
        // it had no name of its own and every stack trace through one said
        // `<anonymous>` - which in a 4.5 MB bundle is no answer at all.
        const std::uint32_t index = compile_function_body(constructor_body, std::string{n.text});
        proto().emit(instruction::with_bx(op::closure, dst, index));
    } else if (n.a >= 0) {
        // A DERIVED class with no constructor gets `constructor(...args) {
        // super(...args); }`. Synthesising an EMPTY one instead meant the
        // parent constructor never ran, so `class MySet extends Set {}`
        // produced an object with none of Set's state and every method on
        // it failed - with nothing to say the constructor had been skipped.
        compile_foreign_expr("(function (...args) { super(...args); })", dst);
    } else {
        // A base class with no constructor still needs a callable, or `new`
        // has nothing to invoke.
        compile_foreign_expr("(function () {})", dst);
    }
    // A SYNTHESISED CONSTRUCTOR IS STILL NAMED AFTER ITS CLASS. Both
    // branches above build one from source text, so it arrives anonymous -
    // and `Object.getPrototypeOf(x).constructor.name` is a standard way to
    // identify a value, where an undefined name compares equal to the other
    // undefined it is being tested against and reports a false match.
    if (!proto().code.empty() && proto().code.back().code == op::closure) {
        out_.functions[proto().code.back().bx()].name = std::string{n.text};
    }
    proto().emit(instruction{op::set_prop, dst, name_operand("prototype"), prototype_reg});
    // `C.prototype.constructor === C`, which is both what pages expect and
    // how `super(...)` finds the parent constructor to call.
    proto().emit(instruction{op::set_prop, prototype_reg, name_operand("constructor"), dst});
    // The constructor's home is this class's prototype, so `super(...)`
    // inside it starts one level up.
    proto().emit(instruction{op::set_prop, dst, name_operand("__home"), prototype_reg});

    // INSTANCE FIELDS ARE PER INSTANCE, and they used to be evaluated once
    // at class-definition time and stored on the PROTOTYPE. So every
    // instance of `class A { items = [] }` shared one array: push to one and
    // it appeared in all of them. Nothing about that was an error, and it is
    // the nastiest shape of bug in this whole batch.
    //
    // They are compiled into a hidden initialiser instead, which `new` runs
    // against the fresh object before the constructor body. Static fields
    // are NOT this - evaluating those once and putting them on the
    // constructor is exactly right, and that is what still happens below.
    std::vector<std::int32_t> instance_fields;
    for (const std::int32_t member : members) {
        const vp::node & m = at(member);
        if (m.c == 0 && (m.d & 1) == 0) { instance_fields.push_back(member); }
    }
    if (!instance_fields.empty()) {
        const std::uint32_t fields = compile_field_initialiser(instance_fields);
        const std::uint16_t init = alloc_reg();
        proto().emit(instruction::with_bx(op::closure, init, fields));
        proto().emit(instruction{op::set_prop, dst, name_operand("__fields"), init});
    }

    // A NAMED CLASS EXPRESSION BINDS ITS OWN NAME, and its methods see it.
    //
    // `let p5$2 = class p5 { static register(a) { p5._seen.has(a); } }` is
    // how p5.js declares itself, and `p5` inside those methods is the class
    // - not any outer binding. Without this the methods read an undefined
    // global and failed at the first property they touched, three frames
    // deep and nowhere near the class.
    //
    // The binding is made before the methods are COMPILED so they capture
    // it, and written as soon as the class value exists.
    if (!n.text.empty()) { emit_write(n.text, dst); }

    const std::uint16_t slot = alloc_reg();
    for (const std::int32_t member : members) {
        const vp::node & m = at(member);
        if (m.text == "constructor" && m.c == 1) { continue; }
        if (m.c == 0 && (m.d & 1) == 0) { continue; } // an instance field; handled above
        if (m.c == 2) {
            // An accessor. It goes on the prototype like a method - or on
            // the constructor when static - and `d` bit2 says which half.
            // (Named below, with the methods.)
            // Installing it as a DATA property, which is what happened
            // before, made `obj.v` be the function rather than call it, and
            // a `set` of the same name overwrote the getter outright.
            compile_expr(m.b, slot);
            const std::uint16_t name = name_operand(std::string{m.text});
            const std::uint16_t target = (m.d & 1) != 0 ? dst : prototype_reg;
            proto().emit(instruction{(m.d & 4) != 0 ? op::define_setter : op::define_getter, target,
                                     name, slot});
            continue;
        }
        if (m.b < 0) { continue; }
        // A METHOD KNOWS ITS OWN NAME, and JavaScript says so: `({m(){}}).m.name`
        // is "m". The name is on the MEMBER node; the function expression under
        // it is anonymous, so compiling it through compile_expr gave every
        // method a nameless proto - and every stack trace, every TypeError and
        // every `fn.name` through one said "<anonymous>". The constructor above
        // already goes through compile_function_body for exactly this reason.
        //
        // ONLY FOR c == 1. A STATIC FIELD reaches this line too and its `b` is
        // an arbitrary expression rather than a function.
        if (m.c == 1) {
            const std::uint32_t index = compile_function_body(m.b, std::string{m.text});
            proto().emit(instruction::with_bx(op::closure, slot, index));
        } else {
            compile_expr(m.b, slot);
        }
        const std::uint16_t name = name_operand(std::string{m.text});
        // A static member goes on the constructor; everything else on the
        // prototype, where instances find it.
        const std::uint16_t target = (m.d & 1) != 0 ? dst : prototype_reg;
        proto().emit(instruction{op::set_prop, target, name, slot});
        // Each method remembers where it was WRITTEN. `super.m()` resolves
        // against that, not against `this` - in a three-deep hierarchy the
        // two differ and resolving against `this` calls the same method
        // forever.
        if (m.c == 1) {
            proto().emit(instruction{op::set_prop, slot, name_operand("__home"), target});
        }
    }
    release_to(mark);
    // Closed AFTER every method is compiled, so they capture the name, and
    // before anything else in the enclosing scope is - so nothing else sees
    // it. The register stays allocated, which is what a scope pop means here.
    if (own_scope) { pop_scope(); }
}

} // namespace ctbrowser::script::detail
