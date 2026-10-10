//===- ctbrowser/lib/Script/vm/call/modules.cpp - ES modules ----*- C++ -*-===//
//
// ES modules: instantiation, the namespace objects, the four module opcode
// bodies, dynamic import(), and running a module. All functions here are
// members of `context`, declared in include/ctbrowser/script/vm.hpp.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <span>
#include <string>
#include <vector>

#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

/// Creates the export bindings of \p prog in \p into without running anything.
/// This is the instantiate half of the specification's two-phase module
/// loading, and cycles are the reason it exists: A imports B imports A, so B
/// evaluates first and asks A for a binding A has not reached the declaration
/// of. With every cell in the graph created up front, B gets a real box that is
/// merely empty, and the write A makes later is a write B reads, so the cycle
/// resolves rather than reporting a missing export.
///
/// It also has to happen before evaluation for a reason unrelated to cycles:
/// the cell an importer takes and the cell the exporter writes must be the same
/// object, and the only way to guarantee that is for one of them not to make
/// it.
void context::instantiate_module(const program & prog, module_record & into) {
    into.compiled = &prog;
    for (const std::string & name : prog.exports) {
        value & slot = into.exports[name];
        if (!slot.is_kind(heap_kind::cell)) {
            slot = value::object(allocate<cell_object>(value::undefined()));
        }
    }
}

/// Returns the module namespace object (`import * as ns`) for \p of. Its
/// properties are accessors rather than values.
///
/// A namespace is live exactly as a named import is (`ns.count` after the
/// exporter reassigns `count` must read the new value), so copying the cells'
/// contents into an ordinary object would be wrong. Each property is a getter
/// over the export's cell instead.
///
/// There is one object per module, cached in the record: `import * as a` and
/// `import * as b` of the same module must be the same object, and code
/// compares namespaces by identity.
///
/// It implements as much of 10.4.6's exotic object as an ordinary object can
/// be: a null [[Prototype]], not extensible, `@@toStringTag` "Module"
/// (non-writable, non-enumerable, non-configurable), the exports in code-unit
/// order, each enumerable and not configurable, so a write, a `delete` and a
/// new property are refused as the specification refuses them. They are still
/// getters rather than the spec's data properties: a namespace read is a
/// get_prop and an accessor re-entry, which is what the AOT bridge's lowering
/// is written against (aot/helpers/calls-and-modules.def,
/// ct_aot_module_namespace).
value context::module_namespace(module_record & of) {
    if (of.namespace_object.is_kind(heap_kind::object)) { return of.namespace_object; }
    object_object * const ns = allocate<object_object>();
    of.namespace_object = value::object(ns);
    ns->prototype = value::undefined(); // An explicit null (object_object::prototype).
    std::vector<std::string> names;
    names.reserve(of.exports.size());
    for (const auto & [name, cell] : of.exports) { names.push_back(name); }
    std::ranges::sort(names);
    for (const std::string & name : names) {
        const value box = of.exports.find(name)->second;
        ns->define_accessor(name,
                            value::object(allocate<native_object>(
                                "get " + name,
                                [box](context &, std::span<value>) {
                                    return box.is_kind(heap_kind::cell)
                                               ? static_cast<cell_object *>(box.as_heap())->slot
                                               : value::undefined();
                                })),
                            value::undefined(), attr_enumerable);
    }
    ns->define(CTBROWSER_SYMBOL_KEY("toStringTag"), string("Module"), attr_none);
    ns->define(module_namespace_key, value::boolean(true), attr_none);
    ns->extensible = false;
    return of.namespace_object;
}

/// Returns the deferred namespace (`import defer * as ns`, 16.2.2) for \p of.
/// This one is exotic: a proxy over the specification's namespace object.
/// Every operation on a string key but `then` ([[Get]], [[GetOwnProperty]],
/// [[DefineOwnProperty]], [[HasProperty]], [[Delete]] and [[OwnPropertyKeys]])
/// evaluates the module first (EnsureDeferredNamespaceEvaluation); a symbol
/// key, `then`, [[Set]] and the prototype and extensibility questions do not
/// (test262 language/import/import-defer/evaluation-triggers). The target is
/// what the traps must answer consistently with: no prototype, not
/// extensible, each export a writable, enumerable, non-configurable property,
/// and @@toStringTag "Deferred Module". Values come from the cells.
value context::deferred_module_namespace(module_record & of) {
    if (!of.deferred_namespace_object.is_undefined()) { return of.deferred_namespace_object; }
    const std::string key = of.specifier;
    std::vector<std::string> names;
    names.reserve(of.exports.size());
    for (const auto & [name, cell] : of.exports) { names.push_back(name); }
    std::ranges::sort(names);

    object_object * const target = allocate<object_object>();
    const rooted keep_target{*this, value::object(target)};
    target->prototype = value::undefined(); // An explicit null.
    for (const std::string & name : names) {
        target->define(name, value::undefined(), attr_writable | attr_enumerable);
    }
    target->define(CTBROWSER_SYMBOL_KEY("toStringTag"), string("Deferred Module"), attr_none);
    target->extensible = false;
    object_object * const handler = allocate<object_object>();
    const rooted keep_handler{*this, value::object(handler)};
    const auto trap = [&](const char * name, native_fn fn) {
        handler->set(name, value::object(allocate<native_object>(name, std::move(fn))));
    };

    // IsSymbolLikeNamespaceKey: a symbol, or "then".
    const auto symbol_like = [](context & c, value k) {
        return k.is_kind(heap_kind::symbol) || c.to_string(k) == "then";
    };
    // EnsureDeferredNamespaceEvaluation. False when the module threw, which
    // is then this operation's throw.
    const auto evaluate = [key](context & c) {
        const auto found = c.modules_.find(key);
        if (found == c.modules_.end() || found->second.evaluated) { return true; }
        if (!c.module_evaluator_) {
            c.raise("a deferred module has no evaluator installed");
            return false;
        }
        if (!c.module_evaluator_(c, found->second)) {
            const value thrown = c.last_thrown();
            c.throw_value(thrown.is_undefined()
                              ? c.make_error("Error", "module `" + key + "` failed to evaluate")
                              : thrown);
            return false;
        }
        return true;
    };
    // The export's cell, or null for a name the module does not export.
    const auto cell_of = [key](context & c, const std::string & name) -> const value * {
        const auto found = c.modules_.find(key);
        if (found == c.modules_.end()) { return nullptr; }
        const auto cell = found->second.exports.find(name);
        return cell == found->second.exports.end() ? nullptr : &cell->second;
    };
    const auto slot_of = [](const value & cell) {
        return cell.is_kind(heap_kind::cell) ? static_cast<cell_object *>(cell.as_heap())->slot
                                             : value::undefined();
    };

    trap("get", [symbol_like, evaluate, cell_of, slot_of](context & c, std::span<value> a) {
        if (symbol_like(c, a[1])) { return c.lookup_property(a[0], c.to_string(a[1])); }
        if (!evaluate(c)) { return value::undefined(); }
        const value * cell = cell_of(c, c.to_string(a[1]));
        return cell == nullptr ? value::undefined() : slot_of(*cell);
    });
    trap("getOwnPropertyDescriptor",
         [symbol_like, evaluate, cell_of, slot_of](context & c, std::span<value> a) {
             const std::string name = c.to_string(a[1]);
             property_descriptor found;
             if (symbol_like(c, a[1])) {
                 return c.own_property(a[0], name, found) ? c.from_property_descriptor(found)
                                                          : value::undefined();
             }
             if (!evaluate(c)) { return value::undefined(); }
             const value * cell = cell_of(c, name);
             if (cell == nullptr) { return value::undefined(); }
             return c.from_property_descriptor(
                 property_descriptor::data(slot_of(*cell), attr_writable | attr_enumerable));
         });
    // 10.4.6.6 for a string key: only a description the binding already
    // satisfies is "defined".
    trap("defineProperty",
         [symbol_like, evaluate, cell_of, slot_of](context & c, std::span<value> a) {
             const std::string name = c.to_string(a[1]);
             const property_descriptor wanted = c.to_property_descriptor(a[2]);
             if (c.throw_pending()) { return value::boolean(false); }
             if (symbol_like(c, a[1])) {
                 return value::boolean(c.define_own_property(a[0], name, wanted));
             }
             if (!evaluate(c)) { return value::boolean(false); }
             const value * cell = cell_of(c, name);
             if (cell == nullptr) { return value::boolean(false); }
             if ((wanted.has_configurable && wanted.configurable) ||
                 (wanted.has_enumerable && !wanted.enumerable) || wanted.is_accessor() ||
                 (wanted.has_writable && !wanted.writable)) {
                 return value::boolean(false);
             }
             return value::boolean(!wanted.has_value || wanted.held.same_value(slot_of(*cell)));
         });
    trap("has", [symbol_like, evaluate, cell_of](context & c, std::span<value> a) {
        if (symbol_like(c, a[1])) { return value::boolean(c.has_property(a[0], a[1])); }
        if (!evaluate(c)) { return value::boolean(false); }
        return value::boolean(cell_of(c, c.to_string(a[1])) != nullptr);
    });
    trap("deleteProperty", [symbol_like, evaluate, cell_of](context & c, std::span<value> a) {
        const std::string name = c.to_string(a[1]);
        if (symbol_like(c, a[1])) { return value::boolean(c.delete_own_property(a[0], name)); }
        if (!evaluate(c)) { return value::boolean(false); }
        return value::boolean(cell_of(c, name) == nullptr);
    });
    trap("ownKeys", [evaluate, names](context & c, std::span<value>) {
        if (!evaluate(c)) { return value::undefined(); }
        const value keys = c.make_array();
        auto * list = static_cast<array_object *>(keys.as_heap());
        for (const std::string & name : names) { list->items.push_back(c.string(name)); }
        list->items.push_back(c.key_value(CTBROWSER_SYMBOL_KEY("toStringTag")));
        return keys;
    });
    trap("set", [](context &, std::span<value>) { return value::boolean(false); });

    of.deferred_namespace_object =
        value::object(allocate<proxy_object>(value::object(target), value::object(handler)));
    return of.deferred_namespace_object;
}

namespace {

/// Maps a specifier as written to the key it is loaded under. `./dep.js` in one
/// module and in another are two different files. The loader left the
/// translation in the record (module_record::resolved), and the bytecode can
/// only carry what was written, so every lookup by specifier goes through here
/// first.
///
/// Returns a reference into \p written when there is no translation, so the
/// caller must keep the string alive across the lookup. The callers below hold
/// it in a parameter.
[[nodiscard]] const std::string & keyed_by(const module_record * current,
                                           const std::string & written) {
    if (current == nullptr) { return written; }
    const auto mapped = current->resolved.find(written);
    return mapped == current->resolved.end() ? written : mapped->second;
}

} // namespace

// The four module opcode bodies. They are members rather than four blocks
// inside run_loop (see the declarations in vm.hpp) because a compiled module
// and an interpreted one must not be able to disagree, and one copy is the
// only way to guarantee it.

/// Returns the export cell \p export_name of module \p specifier.
value context::module_import_cell(const std::string & specifier, const std::string & export_name) {
    // The exporter has been evaluated already (the loader walks the graph
    // depth-first), so its cell is there to be taken.
    const std::string & from = keyed_by(current_module_, specifier);
    const auto found = modules_.find(from);
    if (found == modules_.end()) {
        raise("module `" + from + "` was not loaded");
        return value::undefined();
    }
    const auto cell = found->second.exports.find(export_name);
    if (cell == found->second.exports.end()) {
        raise("`" + from + "` has no export named `" + export_name + "`");
        return value::undefined();
    }
    return cell->second;
}

/// Returns the running module's export cell \p name, creating it if needed.
value context::module_export_cell(const std::string & name, value current) {
    // Outside a module this is not an error and must not write. The register
    // holds the local being exported, so returning undefined would destroy it;
    // the interpreter skips the store, and returning `current` gives
    // AOT-compiled code the same effect.
    if (current_module_ == nullptr) { return current; }
    // Adopt the record's cell; do not publish this register's. The cell is
    // created before any module in the graph runs (see instantiate_module), so
    // by the time this executes it already exists and something in a cycle may
    // already be holding it. Overwriting the record here would hand that
    // importer a box nobody ever writes to again.
    //
    // The create branch is reachable, not defensive: only
    // browser::instantiate_module calls instantiate_module, so a host that
    // calls run_module without instantiating first arrives here with an empty
    // `exports` map.
    value & slot = current_module_->exports[name];
    if (!slot.is_kind(heap_kind::cell)) {
        slot = value::object(allocate<cell_object>(value::undefined()));
    }
    return slot;
}

/// Returns the namespace object of module \p specifier.
value context::module_namespace_for(const std::string & specifier) {
    const std::string & from = keyed_by(current_module_, specifier);
    const auto found = modules_.find(from);
    if (found == modules_.end()) {
        raise("module `" + from + "` was not loaded");
        return value::undefined();
    }
    return module_namespace(found->second);
}

/// Returns the deferred namespace object of module \p specifier.
value context::deferred_module_namespace_for(const std::string & specifier) {
    const std::string & from = keyed_by(current_module_, specifier);
    const auto found = modules_.find(from);
    if (found == modules_.end()) {
        raise("module `" + from + "` was not loaded");
        return value::undefined();
    }
    return deferred_module_namespace(found->second);
}

/// `import(specifier)` from the module or script \p referrer. Returns a promise.
value context::dynamic_import(value specifier, const std::string & referrer) {
    if (!module_loader_) {
        raise("dynamic import() has no loader installed");
        return value::undefined();
    }
    // ToString runs here, before the loader and inside the fenced call, not at
    // the interpreter or AOT call site: for an object it runs the page's own
    // toString, which can throw and re-enter, so converting in two places
    // would duplicate its effects.
    //
    // Per ECMA-262 13.3.10.1 EvaluateImportCall, a throwing ToString or a
    // failed module evaluation rejects the returned promise
    // (IfAbruptRejectPromise) instead of throwing synchronously at the
    // importer, so `import(x).catch(...)` sees it.
    auto * step =
        allocate<native_object>("import()", [referrer](context & c, std::span<value> args) {
            const std::string text = c.to_string(args.empty() ? value::undefined() : args[0]);
            if (c.throw_pending()) { return value::undefined(); }
            return c.module_loader_(c, text, referrer);
        });
    const rooted keep{*this, value::object(step)};
    bool threw = false;
    value thrown = value::undefined();
    const value arg[1] = {specifier};
    const value promise = call_fenced(value::object(step), arg, value::undefined(), threw, thrown);
    if (failed_) { return value::undefined(); }
    return threw ? make_promise(thrown, true) : promise;
}

/// Runs module \p prog like any other program, with two differences: it knows
/// which record it is filling in, so `bind_export` knows which cells to adopt,
/// and its exports outlive the call.
run_result context::run_module(const program & prog, module_record & into) {
    module_record * const outer = current_module_;
    current_module_ = &into;
    into.compiled = &prog;
    const run_result result = frames_.empty() ? run(prog) : run_reentrant(prog);
    into.evaluated = true;
    current_module_ = outer;
    return result;
}

} // namespace ctbrowser::script
