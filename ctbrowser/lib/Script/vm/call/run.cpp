//===- ctbrowser/lib/Script/vm/call/run.cpp - Running a program -----------===//
//
// Running a program: the top-level entry, the re-entrant one a dynamic import
// needs, and global declaration instantiation. All functions here are members
// of `context`, declared in include/ctbrowser/script/vm.hpp.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <string>

#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

/// Runs \p prog from inside the interpreter (used by dynamic import). Unlike
/// run(), whose execute() clears `frames_` and resets `registers_` for a new
/// turn, this pushes a frame on top of the current stack and runs back down to
/// the starting depth, so the importer continues after `import(...)`.
/// Microtasks are not drained.
run_result context::run_reentrant(const program & prog) {
    run_result result;
    if (!prog.ok) {
        result.ok = false;
        result.error = prog.error;
        return result;
    }
    const function_proto & entry = prog.functions[0];
    const value receiver =
        prog.kind == script_kind::classic ? current_realm().global_this : value::undefined();
    const std::size_t new_base = registers_.size();
    registers_.resize(new_base + entry.frame_size + 8u, value::undefined());
    const std::size_t depth = frames_.size();
    // The program pointer is saved and restored. `call` reads it to decide
    // whether a closure can be entered at all, and leaving it pointing at the
    // imported module would make every later call in the importing one look up
    // its function protos in the wrong program.
    const program * const outer_program = program_;
    program_ = &prog;
    // Run the compiled (ahead-of-time) body when there is one, as execute()
    // and invoke() do, so an imported module runs compiled no matter who
    // imported it.
    if (value produced = value::undefined();
        enter_compiled(*this, entry, value::undefined(), registers_.data() + new_base, new_base, 0u,
                       receiver, /*constructing*/ false, produced)) {
        program_ = outer_program;
        if (registers_.size() >= new_base) { registers_.resize(new_base); }
        return result;
    }
    call_frame entered{&entry, 0, new_base, 0, 0, nullptr, receiver, handlers_.size()};
    entered.realm = &current_realm();
    frames_.push_back(entered);
    (void)run_loop(depth);
    program_ = outer_program;
    if (registers_.size() >= new_base) { registers_.resize(new_base); }
    // No drain_microtasks here. The checkpoint belongs to the end of a turn,
    // and this is the middle of one: draining now would run the importer's own
    // pending handlers before its next statement.
    result.ok = !failed_;
    result.error = error_;
    return result;
}

/// GlobalDeclarationInstantiation (16.1.7) for a classic script, or
/// EvalDeclarationInstantiation for a global eval. Returns false after
/// throwing when a declaration is refused.
bool context::global_declaration_instantiation(const program & prog) {
    if (prog.eval_code) { return !prog.eval_global || eval_declaration_instantiation(prog); }
    if (prog.kind != script_kind::classic ||
        (prog.lexical_names.empty() && prog.function_names.empty() && prog.hoisted_vars.empty())) {
        return true;
    }
    realm_record & realm = current_realm();
    const auto refuse = [&](const std::string & name) {
        throw_error("SyntaxError", "Identifier '" + name + "' has already been declared");
        return false;
    };
    // HasRestrictedGlobalProperty (9.1.1.4.14): a non-configurable own
    // property of the global object (`undefined`, `NaN`, or one a page defined
    // that way) may not be shadowed by a `let`. Asked of the proxy's target, as
    // declare_global_var asks it.
    value holder = realm.global_this;
    if (holder.is_kind(heap_kind::proxy)) {
        holder = static_cast<proxy_object *>(holder.as_heap())->target;
    }
    for (const std::string & name : prog.lexical_names) {
        if (realm.var_names.contains(name) || realm.lexical.contains(name)) { return refuse(name); }
        property_descriptor own;
        if (holder.is_object() && own_property(holder, name, own) && own.has_configurable &&
            !own.configurable) {
            return refuse(name);
        }
    }
    // An Annex B name is not a VarName (B.3.2.2): a collision skips it
    // instead (annex_b_skipped).
    const auto annex_b = [&](const std::string & name) {
        return std::ranges::binary_search(prog.annex_b_names, name);
    };
    for (const std::string & name : prog.hoisted_vars) {
        if (realm.lexical.contains(name) && !annex_b(name)) { return refuse(name); }
    }
    for (const std::string & name : prog.function_names) {
        if (realm.lexical.contains(name)) { return refuse(name); }
    }
    // CanDeclareGlobalFunction and CanDeclareGlobalVar (9.1.1.4.15-16, 16.1.7
    // steps 8-12): a function may replace a configurable property or a
    // writable, enumerable data one; any other name needs the global object
    // extensible unless it already owns one. A TypeError otherwise, before a
    // single binding exists. Read from the two stores behind the traps (the
    // target, then the table), because the traps' own invariant check refuses
    // a table name once the target is not extensible.
    const bool extensible = !holder.is_heap() || is_extensible(holder);
    const auto global_own = [&](const std::string & name, property_descriptor & out) {
        if (holder.is_object() && own_property(holder, name, out)) { return true; }
        const auto it = realm.globals.find(name);
        if (it == realm.globals.end() || realm.lexical.contains(name)) { return false; }
        out = property_descriptor::data(it->second, realm.builtin.contains(name)
                                                        ? attr_writable | attr_configurable
                                                        : attr_default);
        return true;
    };
    const auto cannot = [&](const std::string & name) {
        throw_error("TypeError", "Cannot declare global '" + name + "'");
        return false;
    };
    for (const std::string & name : prog.function_names) {
        property_descriptor existing;
        if (!global_own(name, existing)) {
            if (!extensible) { return cannot(name); }
        } else if (!existing.configurable &&
                   !(existing.is_data() && existing.writable && existing.enumerable)) {
            return cannot(name);
        }
    }
    for (const std::string & name : prog.hoisted_vars) {
        if (property_descriptor existing;
            !extensible && !annex_b(name) && !global_own(name, existing)) {
            return cannot(name);
        }
    }
    for (const std::string & name : prog.lexical_names) {
        // A configurable object binding may be shadowed by a lexical binding.
        // Keep its value on the object before reusing the table slot.
        if (const auto prior = realm.globals.find(name); prior != realm.globals.end()) {
            property_descriptor own;
            if (holder.is_object() && !own_property(holder, name, own)) {
                static_cast<object_object *>(holder.as_heap())
                    ->define(name, prior->second,
                             realm.builtin.contains(name) ? attr_writable | attr_configurable
                                                          : attr_default);
            }
            realm.globals.erase(prior);
        }
        realm.lexical[name] = false;
    }
    for (const std::string & name : prog.hoisted_vars) {
        if (!annex_b_skipped(prog, name)) { realm.var_names[name] = true; }
    }
    for (const std::string & name : prog.function_names) { realm.var_names[name] = true; }
    return true;
}

/// EvalDeclarationInstantiation (19.2.1.3) for a global eval.
/// Returns false after throwing when a declaration is refused.
bool context::eval_declaration_instantiation(const program & prog) {
    realm_record & realm = current_realm();
    // Step 3.a: a var or function name the global environment has as a
    // lexical binding (a `let x` of a script, and then `eval('var x')`). Not
    // an Annex B block function's, which is skipped instead (B.3.2.3,
    // annex_b_skipped).
    const auto annex_b = [&](const std::string & name) {
        return std::ranges::binary_search(prog.annex_b_names, name);
    };
    for (const auto * names : {&prog.hoisted_vars, &prog.function_names}) {
        for (const std::string & name : *names) {
            if (realm.lexical.contains(name) && (names == &prog.function_names || !annex_b(name))) {
                throw_error("SyntaxError", "Identifier '" + name + "' has already been declared");
                return false;
            }
        }
    }
    // Steps 8-10, as GlobalDeclarationInstantiation asks them (above).
    value holder = realm.global_this;
    if (holder.is_kind(heap_kind::proxy)) {
        holder = static_cast<proxy_object *>(holder.as_heap())->target;
    }
    const bool extensible = !holder.is_heap() || is_extensible(holder);
    const auto global_own = [&](const std::string & name, property_descriptor & out) {
        if (holder.is_object() && own_property(holder, name, out)) { return true; }
        const auto it = realm.globals.find(name);
        if (it == realm.globals.end() || realm.lexical.contains(name)) { return false; }
        out = property_descriptor::data(it->second, realm.builtin.contains(name)
                                                        ? attr_writable | attr_configurable
                                                        : attr_default);
        return true;
    };
    for (const std::string & name : prog.function_names) {
        property_descriptor existing;
        const bool definable =
            global_own(name, existing)
                ? existing.configurable ||
                      (existing.is_data() && existing.writable && existing.enumerable)
                : extensible;
        if (!definable) {
            throw_error("TypeError", "Cannot declare global function '" + name + "'");
            return false;
        }
    }
    for (const std::string & name : prog.hoisted_vars) {
        if (property_descriptor existing;
            !extensible && !annex_b(name) && !global_own(name, existing)) {
            throw_error("TypeError", "Cannot declare global variable '" + name + "'");
            return false;
        }
    }
    // Not recorded as var names: an eval's bindings are configurable, and a
    // later script's `let` of the same name shadows them (16.1.7 step 3.c's
    // note: only a restricted, non-configurable global property collides).
    return true;
}

/// Returns true when Annex B block function \p name must not become a global
/// var. B.3.2.2 step 12.b.i / B.3.2.3 step 11.b.ii: the name becomes a global
/// var only when the global environment has no lexical binding of it and
/// CanDeclareGlobalVar(F) allows it (an own property of the global object, the
/// target's or the table's, or an extensible global). Otherwise it is
/// fnDefinable false, and skipped without an error.
///
/// Known bug: the declaration's evaluation still copies the function into
/// the global of that name (compile_function_decl emits the set_global
/// whatever this decides), so `let f = 1` and then `{ function f() {} }` in
/// another script or an eval leaves f the function rather than 1. Skipping
/// that write needs the copy's instruction told apart from a `f = x`, which
/// is the same op::set_global.
bool context::annex_b_skipped(const program & prog, const std::string & name) {
    if (!std::ranges::binary_search(prog.annex_b_names, name)) { return false; }
    realm_record & realm = current_realm();
    if (realm.lexical.contains(name)) { return true; }
    value holder = realm.global_this;
    if (holder.is_kind(heap_kind::proxy)) {
        holder = static_cast<proxy_object *>(holder.as_heap())->target;
    }
    if (!holder.is_heap() || is_extensible(holder)) { return false; }
    property_descriptor own;
    if (holder.is_object() && own_property(holder, name, own)) { return false; }
    return !realm.globals.contains(name);
}

run_result context::run_script_fenced(const program & prog, value & thrown) {
    thrown = value::undefined();
    if (!prog.ok) { return {value::undefined(), false, prog.error}; }
    const value start =
        value::object(allocate<native_object>("<script>", [&prog](context & cx, std::span<value>) {
            if (!cx.global_declaration_instantiation(prog)) { return value::undefined(); }
            for (const std::string & name : prog.hoisted_vars) {
                if (!cx.annex_b_skipped(prog, name)) {
                    cx.declare_global_var(name, prog.eval_code);
                }
            }
            for (const std::string & name : prog.function_names) {
                cx.declare_global_function(name, prog.eval_code);
            }
            auto * entry = cx.allocate<closure_object>(&prog.functions[0]);
            entry->owner = &prog;
            entry->realm = &cx.current_realm();
            return cx.call(value::object(entry), {}, cx.global_this());
        }));
    bool threw = false;
    const value returned = call_fenced(start, {}, global_this(), threw, thrown);
    const rooted keep_thrown{*this, thrown};
    run_result result{.returned = returned, .ok = !threw && !failed_, .error = {}};
    if (!result.ok) { result.error = failed_ ? error_ : "uncaught " + describe_thrown(thrown); }
    return result;
}

/// Runs \p prog as a whole turn: declaration instantiation, the top level,
/// then the microtask checkpoint.
run_result context::run(const program & prog) {
    run_result result;
    if (!prog.ok) {
        result.ok = false;
        result.error = prog.error;
        return result;
    }
    failed_ = false;
    error_.clear();
    thrown_ = value::undefined();
    // See program::hoisted_vars: the script's `var`s exist before it starts,
    // once GlobalDeclarationInstantiation has found nothing to refuse. Its
    // refusal is thrown with no frame and no handler, as execute() would
    // leave them, so a dead program's `try` cannot catch it.
    frames_.clear();
    handlers_.clear();
    if (global_declaration_instantiation(prog)) {
        for (const std::string & name : prog.hoisted_vars) {
            if (!annex_b_skipped(prog, name)) { declare_global_var(name, prog.eval_code); }
        }
        for (const std::string & name : prog.function_names) {
            declare_global_function(name, prog.eval_code);
        }
        result.returned = execute(prog, prog.functions[0]);
    }
    // The end of the turn. A script's promise handlers run after its last
    // statement, not between two of them, so the checkpoint is here, once the
    // top level has finished.
    drain_microtasks();
    // Drop the microtask queue of a script that failed. `drain_microtasks`
    // stops on `failed_`, so the jobs would otherwise run during the next
    // script, after its synchronous code. Chrome would still run them after an
    // uncaught throw; we cannot, because `failed_` is also set by `raise` for
    // the call-stack and allocation limits, and draining then would run more
    // JavaScript exactly where the limit is meant to stop it.
    if (failed_) { microtasks_.clear(); }
    result.ok = !failed_;
    result.error = error_;
    return result;
}

/// Resets the stacks and runs \p entry, the top level of \p prog, compiled
/// when it has a compiled body and interpreted otherwise.
value context::execute(const program & prog, const function_proto & entry) {
    registers_.assign(entry.frame_size + 8u, value::undefined());
    frames_.clear();
    // Clear the handler stack as well. A VM-level `raise` (call-stack or
    // allocation limit) leaves run_loop without unwinding, so a `try` that was
    // live stays on `handlers_` pointing at frame 0. The next program on this
    // context also has a frame 0, and unwind_to_handler would accept the stale
    // handler and jump to an address in the other program's bytecode.
    handlers_.clear();
    thrown_ = value::undefined();
    program_ = &prog;
    // A classic script's `this` is the realm's global object, even in strict
    // mode.
    // Modules have no top-level receiver. Never reload the writable globalThis
    // binding here: an earlier script may have replaced or cleared it.
    const value receiver =
        prog.kind == script_kind::classic ? current_realm().global_this : value::undefined();
    // Run the compiled top level if this program has one, which for ctcompile
    // is the ordinary case: a page's `<script>` is a top level. Checked after
    // the reset above, so a compiled body enters a context in the state it
    // expects, and before the frame push, because ct_aot_enter pushes its own.
    if (value produced = value::undefined();
        enter_compiled(*this, entry, value::undefined(), registers_.data(), 0u, 0u, receiver,
                       /*constructing*/ false, produced)) {
        return produced;
    }
    call_frame entered{&entry, 0, 0, 0, 0, nullptr, receiver, 0};
    entered.realm = &current_realm();
    frames_.push_back(entered);
    // Literal caches are per program: a string or BigInt literal in a loop
    // allocates once, not once per iteration.
    string_cache_.clear();
    bigint_cache_.clear();
    // A top-level program is a C++ entry like any other. Marking it is what
    // makes the transitions inside it attributable: without this, `executing_`
    // is still `cxx` for the whole run, so an interpreted script calling a
    // compiled function would be counted as C++ reaching it directly. The
    // interpreter is what is running; say so.
    note_transition_into_vm(*this);
    const executing_as running{*this, executing_kind::vm};
    return run_loop(0);
}

} // namespace ctbrowser::script
