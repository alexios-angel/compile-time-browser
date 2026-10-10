//===- EntryTable.hpp - Install compiled entry points -----------*- C++ -*-===//
//
// Connects each generated translation unit's compiled bodies to the right
// `function_proto` in a loaded program.
//
// The runtime enters a compiled body only when `function_proto::aot_entry` is
// set. A generated application has no driver to set it by hand, so the mapping
// is carried in the symbol name. `BytecodeImport.cpp` names every imported
// function `<sanitised>$<index>`, where `index` is its position in
// `program::functions` and the sanitiser maps every character that is not
// `[A-Za-z0-9_]` to `_`; `EmitC/Support.cpp` (`c_identifier`) then maps `$` to
// `_`, because `$` is not in C++'s basic character set. So the emitted symbol
// `stepShip_4` says it is the compiled body of function 4, named `stepShip`.
// No side file or serialized metadata is needed.
//
// This file restates that naming rule in entry_symbol(). The duplication is
// safe because drift is loud: install() refuses a symbol that matches no
// function, and install_strict() refuses a function that matches no symbol, so
// together they enforce a bijection. A changed sanitiser turns every generated
// application into a startup refusal naming the symbol, never into one that
// silently falls back to the interpreter (still correct, so nothing reports it).
//
// Not detected: a table generated from a different program whose functions
// happen to have the same names at the same indices. The defence is the build
// graph: the generator and the program compiler read one fixture, as
// ctcompile/test/Support/embed-js.cmake enforces.
//
// This header should eventually move to ctbrowser together with the launcher,
// so a shipped application never links a compiler-side header (see
// ctcompile/docs/plans/launcher.md). It stays header-only here until then.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <ctbrowser/aot/aot_entry.h>
#include <ctbrowser/script/bytecode.hpp>

#include "ctcompile/Support/CppLiterals.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ctcompile::aot {

/// One compiled body, as the generated table spells it.
///
/// The table stores the symbol text rather than an index, because the index is
/// already in the symbol and a generator writing both could write them
/// inconsistently.
struct entry_binding {
    const char * symbol = nullptr;
    ctbrowser::aot::ct_aot_entry_fn entry = nullptr;
};

/// Everything one generated translation unit contributes. `source_name` is
/// carried only so a refusal can say which application it is about.
struct entry_table {
    const char * source_name = nullptr;
    const entry_binding * bindings = nullptr;
    std::size_t binding_count = 0;
};

/// What install() did, as counts a test can assert on.
///
/// `interpreted` is the important one: an application that installed nothing
/// and one that installed everything both run and print the right answer, and
/// only this count (and the dispatch counters) tells them apart. That is why
/// install_strict() is a rule about this number rather than a flag.
struct install_report {
    std::size_t functions = 0;   ///< Size of program::functions.
    std::size_t installed = 0;   ///< Protos that now have a compiled body.
    std::size_t interpreted = 0; ///< Protos that still run interpreted.
    std::string error;           ///< Non-empty means nothing was installed.

    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

/// Returns the C++ symbol the backend emits for function `index` named `name`.
///
/// This restates the importer's naming rule (see the file header for why the
/// copy is safe). `BytecodeImport.cpp` names each `ctjs.func`
/// `cpp::identifier(name or "fn")` + `$` + index, and
/// `EmitC/Support.cpp::c_identifier` then maps `$` to `_`. The two cannot
/// collide: the sanitised part contains no `$` and the index is decimal, so
/// `a$1` becomes `a_1` and `a_1$2` becomes `a_1_2`.
[[nodiscard]] inline std::string entry_symbol(std::string_view name, std::size_t index) {
    return cpp::identifier(name.empty() ? std::string_view{"fn"} : name) + "_" +
           std::to_string(index);
}

/// Sets `aot_entry` on each function named by `table`.
///
/// All or nothing: if any row is empty, names no function, or claims a
/// function twice, nothing is installed and `report.error` says why. A
/// partially installed program would still answer correctly, but with an
/// unknown mix of compiled and interpreted code that nothing reports, so every
/// row is resolved before anything is written.
[[nodiscard]] inline install_report install(ctbrowser::script::program & into,
                                            const entry_table & table) {
    install_report report;
    report.functions = into.functions.size();
    report.interpreted = report.functions;

    // Compute every proto's symbol once. Rebuilding the names per binding would
    // be quadratic on a real program (Phaser has 7,725 functions), and every
    // row must be resolved before any assignment anyway.
    std::unordered_map<std::string, std::size_t> symbols;
    symbols.reserve(into.functions.size());
    for (std::size_t index = 0; index < into.functions.size(); ++index) {
        symbols.try_emplace(entry_symbol(into.functions[index].name, index), index);
    }

    std::vector<std::size_t> chosen(table.binding_count, into.functions.size());
    std::vector<bool> claimed(into.functions.size(), false);
    for (std::size_t row = 0; row < table.binding_count; ++row) {
        const entry_binding & binding = table.bindings[row];
        if (binding.symbol == nullptr || binding.entry == nullptr) {
            report.error = "the entry table has an empty row at " + std::to_string(row);
            return report;
        }
        const std::string_view wanted{binding.symbol};
        const auto hit = symbols.find(std::string{wanted});
        const std::size_t found = hit == symbols.end() ? into.functions.size() : hit->second;
        // A symbol matching nothing means the two naming rules have drifted or
        // the table belongs to another program. Without this refusal the
        // application would start, interpret everything, and report zero
        // compiled functions without complaint.
        if (found == into.functions.size()) {
            report.error = "no function in this program is named " + std::string{wanted} +
                           " - the generated bodies and the runtime disagree about how a "
                           "function_proto is named, or this table is not this program's";
            return report;
        }
        if (claimed[found]) {
            report.error = "two rows of the entry table both claim " + std::string{wanted};
            return report;
        }
        claimed[found] = true;
        chosen[row] = found;
    }

    for (std::size_t row = 0; row < table.binding_count; ++row) {
        into.functions[chosen[row]].aot_entry = table.bindings[row].entry;
    }
    report.installed = table.binding_count;
    report.interpreted = report.functions - report.installed;
    return report;
}

/// Like install(), but refuses unless every function in the program receives a
/// compiled body.
///
/// Strict AOT-only mode forbids dynamic source processing (no `eval`, no
/// `new Function`, no `innerHTML`, no runtime CSS text). This checks the part
/// the JavaScript backend controls: a program that still holds an uncompiled
/// function refuses to start instead of falling back to the interpreter, which
/// would be correct but silently slower. Together with install() this makes
/// the symbol-to-function mapping a bijection.
[[nodiscard]] inline install_report install_strict(ctbrowser::script::program & into,
                                                   const entry_table & table) {
    install_report report = install(into, table);
    if (!report.ok()) { return report; }
    if (report.interpreted != 0) {
        // List the uncompiled functions by name, so the error says which ones
        // to look at in the backend.
        std::string names;
        for (std::size_t index = 0; index < into.functions.size(); ++index) {
            if (into.functions[index].aot_entry != nullptr) { continue; }
            if (!names.empty()) { names += ", "; }
            names += entry_symbol(into.functions[index].name, index);
        }
        report.error = "strict AOT-only: " + std::to_string(report.interpreted) + " of " +
                       std::to_string(report.functions) + " functions have no compiled body (" +
                       names + ")";
        // Remove the entries again, so a caller that ignores the refusal still
        // cannot run a partially compiled program.
        for (ctbrowser::script::function_proto & proto : into.functions) {
            proto.aot_entry = nullptr;
        }
        report.installed = 0;
        report.interpreted = report.functions;
    }
    return report;
}

} // namespace ctcompile::aot
