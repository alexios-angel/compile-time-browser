#pragma once

#include <ctbrowser/aot/aot_entry.h>
#include <ctbrowser/script/bytecode.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// HOW A GENERATED TRANSLATION UNIT REACHES THE RIGHT `function_proto`.
//
// The runtime enters a compiled body only when `function_proto::aot_entry` is
// set, and nothing sets it. Every test that has run compiled code so far did it
// by hand: Differential.cpp holds a table of {name, ordinal, &symbol} written
// out in its own source, walks `program::functions` looking for each name, and
// assigns the field. That is right for a driver whose job is to install
// forty-six named bodies one case at a time, and it is not a mechanism - a
// generated application has no driver, and a table somebody types is a table
// that disagrees with the code the moment either moves.
//
// THE MAPPING IS CARRIED IN THE SYMBOL NAME, and it was already there.
// `BytecodeImport.cpp` names every imported function `<sanitised>$<index>`,
// where `index` is its position in `program::functions` and the sanitiser maps
// every character that is not `[A-Za-z0-9_]` to `_`; `CTJSToEmitC.cpp` then
// maps `$` to `_`, because `$` is not in C++'s basic character set. So the
// emitted symbol `stepShip_4` states, in the one place both sides can read,
// that it is the compiled body of function 4 and that function 4 was called
// `stepShip`. Nothing else has to be transported: no side file, no serialized
// metadata, no ordinal anybody counted twice.
//
// THIS FILE IS A SECOND COPY OF THAT NAMING RULE, and that cost is paid
// deliberately. The alternative is a build-time tool that compiles the source a
// third time to emit a metadata table - a third copy plus a build edge. What
// makes the duplication safe is that a drift is LOUD RATHER THAN SILENT:
// `install` refuses when a symbol in the table matches no function, and
// `install_strict` refuses when a function matches no symbol, so the two
// together are a BIJECTION. A change to either sanitiser turns every generated
// application into a startup refusal naming the symbol, not into an application
// that quietly interprets everything - and a silent fall back to the
// interpreter is the failure this project treats as worst, because the answer
// is still right and nothing reports it.
//
// WHAT THE BIJECTION CANNOT SEE, stated rather than discovered: a table
// generated from a DIFFERENT program whose functions happen to have the same
// names at the same indices. Nothing here would notice, and the compiled bodies
// would be wrong. The defence is the build graph rather than a check - one
// fixture, read by the generator and by whatever compiles the program, which is
// the discipline `embed-js.cmake` exists to enforce and which this project has
// already been bitten by ignoring.
//
// IT IS HEADER-ONLY AND LIVES IN ctcompile, WHICH IS NOT WHERE IT BELONGS.
// Phase 18 says the launcher is a fixed precompiled library in ctbrowser
// (`ctbrowser/lib/Application/`) so that a shipped application never links a
// compiler-side header. This is the installer rather than the launcher, and it
// moves there with it; see `ctcompile/docs/plans/launcher.md`. Header-only
// until then, because a new library is a new CMake target in a file three other
// branches are editing at the same time.
namespace ctcompile::aot {

// ONE COMPILED BODY, AS THE GENERATED TABLE SPELLS IT.
//
// The symbol TEXT rather than an index, because the index is already in the
// symbol and a generator writing both could write them inconsistently. One
// fact, one place.
struct entry_binding {
    const char * symbol = nullptr;
    ctbrowser::aot::ct_aot_entry_fn entry = nullptr;
};

// EVERYTHING ONE GENERATED TRANSLATION UNIT CONTRIBUTES. `source_name` is
// carried for diagnostics only - a refusal that cannot say which application it
// is about is a refusal somebody has to bisect.
struct entry_table {
    const char * source_name = nullptr;
    const entry_binding * bindings = nullptr;
    std::size_t binding_count = 0;
};

// WHAT INSTALLING DID, in numbers a test can assert on rather than in a bool.
//
// `interpreted` is the number this exists for. An application that installed
// nothing and one that installed everything both run and both print the right
// answer; from outside, the only difference is this count and the dispatch
// counters. That is why strict mode below is a rule about a number and not a
// flag somebody sets.
struct install_report {
    std::size_t functions = 0;   // program::functions.size()
    std::size_t installed = 0;   // protos that now have a compiled body
    std::size_t interpreted = 0; // and the ones that do not
    std::string error;           // non-empty means NOTHING was installed

    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

// THE IMPORTER'S NAMING RULE, reproduced. See the header comment for why this
// is a copy and what stops it drifting quietly.
//
// `BytecodeImport.cpp`, at the `ctjs.func` it creates per function:
//     std::string name = proto.name.empty() ? std::string{"fn"} : proto.name;
//     for (char & c : name) { if (!isalnum(c) && c != '_') { c = '_'; } }
//     name += "$" + std::to_string(index);
// `CTJSToEmitC.cpp::c_identifier` then maps `$` to `_`, and the two together
// cannot collide: the sanitised part contains no `$` and the index is decimal,
// so `a$1` becomes `a_1` and `a_1$2` becomes `a_1_2`.
//
// ASCII BY HAND rather than `std::isalnum`, for the reason `core/algorithms.hpp`
// gives about case folding: `isalnum` is locale-sensitive, and a symbol that
// depends on `LC_ALL` is a symbol the linker resolves on one machine and not on
// another.
[[nodiscard]] inline std::string entry_symbol(std::string_view name, std::size_t index) {
    std::string symbol{name.empty() ? std::string_view{"fn"} : name};
    for (char & c : symbol) {
        const auto raw = static_cast<unsigned char>(c);
        const bool alnum =
            (raw >= '0' && raw <= '9') || (raw >= 'A' && raw <= 'Z') || (raw >= 'a' && raw <= 'z');
        if (!alnum && c != '_') { c = '_'; }
    }
    symbol += "_" + std::to_string(index);
    return symbol;
}

// STAMP A GENERATED TABLE ONTO A PROGRAM.
//
// ALL OR NOTHING. A table with one bad row installs none of it, because a
// partially installed program is a program running an unknown mixture: the
// answer is still right, the counters still move, and nothing distinguishes it
// from the build that was intended. So everything is resolved before anything
// is written.
[[nodiscard]] inline install_report install(ctbrowser::script::program & into,
                                            const entry_table & table) {
    install_report report;
    report.functions = into.functions.size();
    report.interpreted = report.functions;

    // EVERY PROTO'S SYMBOL, COMPUTED ONCE. Resolving each binding by rebuilding
    // the names would be quadratic on a real program - Phaser is 7,725
    // functions - and the resolution has to happen before any assignment
    // anyway.
    std::vector<std::string> symbols;
    symbols.reserve(into.functions.size());
    for (std::size_t index = 0; index < into.functions.size(); ++index) {
        symbols.push_back(entry_symbol(into.functions[index].name, index));
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
        std::size_t found = into.functions.size();
        for (std::size_t index = 0; index < symbols.size(); ++index) {
            if (symbols[index] == wanted) {
                found = index;
                break;
            }
        }
        // THE REFUSAL THIS FILE IS FOR. A symbol matching nothing means the two
        // naming rules have drifted, or the table is not this program's - and
        // in both cases the application would otherwise start, interpret
        // everything, and report a clean and truthful zero.
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

// STRICT AOT-ONLY, AS A RULE ABOUT A NUMBER.
//
// Phase 19 describes strict mode as a policy over dynamic source processing -
// no `eval`, no `new Function`, no `innerHTML`, no runtime CSS text. This is
// the half of it that the JavaScript backend can be held to, and it is stated
// in the one way a test can check without reading the compiler's mind: an
// application whose program still holds a function with no compiled body is not
// an AOT-only application, and it REFUSES TO START rather than falling back.
//
// A refusal and not a warning, for the shape of this project's failure mode: a
// build where the backend quietly stopped lowering one function produces an
// application that starts, renders, answers correctly and is slower - which is
// invisible, and stays invisible until somebody profiles it.
//
// It is also the other half of the bijection. `install` refuses a symbol that
// matches no function; this refuses a function that matches no symbol.
[[nodiscard]] inline install_report install_strict(ctbrowser::script::program & into,
                                                   const entry_table & table) {
    install_report report = install(into, table);
    if (!report.ok()) { return report; }
    if (report.interpreted != 0) {
        // NAMED, NOT COUNTED. "3 functions are not compiled" sends whoever
        // reads it back to the backend with no idea which three, and the answer
        // is one loop away.
        std::string names;
        for (std::size_t index = 0; index < into.functions.size(); ++index) {
            if (into.functions[index].aot_entry != nullptr) { continue; }
            if (!names.empty()) { names += ", "; }
            names += entry_symbol(into.functions[index].name, index);
        }
        report.error = "strict AOT-only: " + std::to_string(report.interpreted) + " of " +
                       std::to_string(report.functions) + " functions have no compiled body (" +
                       names + ")";
        // AND THE ENTRIES COME BACK OFF. A refusal that left half a program
        // stamped is a refusal the caller can ignore into exactly the mixed
        // build this exists to forbid.
        for (ctbrowser::script::function_proto & proto : into.functions) {
            proto.aot_entry = nullptr;
        }
        report.installed = 0;
        report.interpreted = report.functions;
    }
    return report;
}

} // namespace ctcompile::aot
