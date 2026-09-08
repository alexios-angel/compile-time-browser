// ctbrowser.script context - ES modules: instantiation, the namespace object,
// the four module opcode bodies, and running a module.
//
// One of five files carved out of a 1,171-line vm/call.cpp on 2026-09-08 -
// which was itself one of four carved out of a 3,232-line vm.cpp on
// 2026-08-09. All members of `context`, declared in
// include/ctbrowser/script/vm.hpp - so they split across translation units
// with nothing to declare.

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/number_format.hpp>
#include <ctbrowser/script/vm.hpp>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// The VM's implementation.
//
// `run_loop` alone is 15 KB of object code - the whole instruction dispatch -
// and while it lived in the interface every translation unit that imported the
// module emitted its own copy and optimised it again. The class declaration
// stays in :vm; the bodies live here and are compiled once.

namespace ctbrowser::script {

// CREATE THE BINDINGS WITHOUT RUNNING ANYTHING. This is the instantiate half of
// the specification's two-phase module loading, and a cycle is the reason it
// exists: A imports B imports A, so B evaluates first and asks A for a binding
// A has not reached the declaration of. With every cell in the graph created up
// front, B gets a real box that is merely EMPTY, and the write A makes later is
// a write B reads - which is a cycle resolving rather than a missing export.
//
// It also has to happen before evaluation for a reason unrelated to cycles: the
// cell an importer takes and the cell the exporter writes must be the SAME
// object, and the only way to guarantee that is for one of them not to make it.
void context::instantiate_module(const program & prog, module_record & into) {
    into.compiled = &prog;
    for (const std::string & name : prog.exports) {
        value & slot = into.exports[name];
        if (!slot.is_kind(heap_kind::cell)) {
            slot = value::object(allocate<cell_object>(value::undefined()));
        }
    }
}

// THE NAMESPACE OBJECT, and its properties are ACCESSORS rather than values.
//
// A namespace is live exactly as a named import is - `ns.count` after the
// exporter reassigns `count` must read the new value - so copying the cells'
// contents into an ordinary object here would be the same shortcut in a
// different shape. Each property is a getter over the cell instead, which is
// what the cell was for.
//
// ONE OBJECT PER MODULE, cached in the record: `import * as a` and `import * as
// b` of the same module are required to be the SAME object, and code compares
// namespaces by identity.
value context::module_namespace(module_record & of) {
    if (of.namespace_object.is_kind(heap_kind::object)) { return of.namespace_object; }
    object_object * const ns = allocate<object_object>();
    of.namespace_object = value::object(ns);
    for (auto & [name, cell] : of.exports) {
        const value box = cell;
        ns->define_accessor(name,
                            value::object(allocate<native_object>(
                                "get " + name,
                                [box](context &, std::span<value>) {
                                    return box.is_kind(heap_kind::cell)
                                               ? static_cast<cell_object *>(box.as_heap())->slot
                                               : value::undefined();
                                })),
                            value::undefined());
    }
    return of.namespace_object;
}

namespace {

// AS WRITTEN IS NOT AS KEYED: `./dep.js` in one module and in another are two
// different files. The loader left the translation in the record - see
// module_record::resolved - and the bytecode can only carry what was written,
// so every lookup by specifier goes through here first.
//
// A REFERENCE INTO `written` when there is no translation, which is why the
// caller must keep the string alive across the lookup. Both callers below hold
// it in a parameter.
[[nodiscard]] const std::string & keyed_by(const module_record * current,
                                           const std::string & written) {
    if (current == nullptr) { return written; }
    const auto mapped = current->resolved.find(written);
    return mapped == current->resolved.end() ? written : mapped->second;
}

} // namespace

// THE FOUR MODULE OPCODE BODIES. See the declarations in vm.hpp for why they
// are members rather than four blocks inside run_loop: a compiled module and an
// interpreted one must not be able to disagree, and one copy is the only way to
// guarantee it.

value context::module_import_cell(const std::string & specifier, const std::string & export_name) {
    // The exporter has been evaluated already - the loader walks the graph
    // depth-first - so its cell is there to be taken.
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

value context::module_export_cell(const std::string & name, value current) {
    // NOT IN A MODULE IS NOT AN ERROR, and it must not write. The register
    // holds the local being exported, so answering undefined here would destroy
    // it - the interpreter simply skips the store, and answering `current` is
    // the same thing in a form both tiers can express.
    if (current_module_ == nullptr) { return current; }
    // ADOPT THE RECORD'S CELL, do not publish this register's. The cell is
    // created before ANY module in the graph runs - see instantiate_module - so
    // by the time this executes it already exists and something in a cycle may
    // already be holding it. Overwriting the record here would hand that
    // importer a box nobody ever writes to again.
    //
    // THE CREATING ARM IS LIVE, not a belt: instantiate_module is called by
    // browser::instantiate_module and by nothing else, so a host that runs a
    // module through run_module without instantiating it first arrives here
    // with an empty `exports` map.
    value & slot = current_module_->exports[name];
    if (!slot.is_kind(heap_kind::cell)) {
        slot = value::object(allocate<cell_object>(value::undefined()));
    }
    return slot;
}

value context::module_namespace_for(const std::string & specifier) {
    const std::string & from = keyed_by(current_module_, specifier);
    const auto found = modules_.find(from);
    if (found == modules_.end()) {
        raise("module `" + from + "` was not loaded");
        return value::undefined();
    }
    return module_namespace(found->second);
}

value context::dynamic_import(value specifier, const std::string & referrer) {
    if (!module_loader_) {
        raise("dynamic import() has no loader installed");
        return value::undefined();
    }
    // to_string BEFORE the loader and INSIDE this member. For an object it runs
    // the page's own toString, which can throw and can re-enter - so a tier that
    // converted at its call site instead would be a second conversion with a
    // second set of effects.
    return module_loader_(*this, to_string(specifier), referrer);
}

// A MODULE IS RUN LIKE ANY OTHER PROGRAM, with two differences: it knows which
// record it is filling in, so `bind_export` knows which cells to adopt, and its
// exports outlive the call.
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
