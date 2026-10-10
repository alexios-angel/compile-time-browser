//===- ctcompile/test/Runtime/Modules/Differential.cpp - module opcodes ---===//
//
// Does a compiled module top level compute what the interpreter computes?
//
// The premise of ctcompile/test/Runtime/Differential/Differential.cpp (run a
// body interpreted and compiled, compare), applied to the four ES-module
// opcodes - and a separate binary rather than four more arms in that file,
// for a structural reason:
//
//   That harness compiles one classic script and runs it with cx.run.
//   op::load_import, op::bind_export and op::load_namespace are emitted by
//   compile_program's `if (module_scope_)` arm and by nowhere else, so a
//   classic script cannot contain one. `current_module_` is null there and
//   `modules_` is empty. No fixture shape reaches three of these four opcodes
//   from that harness at all.
//
// So this builds a three-module graph the way ctbrowser/lib/Shell/browser/
// scripts.cpp does - register the records, fill `resolved`, instantiate every
// module, then evaluate in dependency order - and installs a compiled entry
// on one of them.
//
// The body under test is functions[0] of main.js (embedded as
// module-main.js.inc): the module's top level, not a named function.
//
// What is held still: dep.js and user.js stay interpreted in every arm. dep
// is where the live binding is written, and user is the only thing that can
// see what op::bind_export decided: reading `mine` from inside main proves its
// local works, while reading it from the other side of the record proves the
// cell in that record is the box the local writes through.
//
// What the comparison cannot see: the interpreter and the compiled code both
// call the same context members (module_import_cell, module_export_cell,
// module_namespace_for, dynamic_import), so a bug inside one of them breaks
// both identically and they still agree. Each arm therefore also checks a
// hard-coded expected answer.
//
//===----------------------------------------------------------------------===//
#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/aot/aot_entry.h>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/vm.hpp>

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

#include "check.hpp"

using ctbrowser::script::context;
using ctbrowser::script::function_proto;
using ctbrowser::script::program;
using ctbrowser::script::script_kind;
using ctbrowser::script::value;

// Compiled by the build from main.js, one per name in its ENTRIES list.
// `_script_` is what the compiler calls functions[0]; the rest are ordinary.
#define CT_ENTRY(name_)                                                                            \
    extern "C" std::int32_t ctc_##name_(                                                           \
        ctbrowser::aot::ct_aot_ctx *, const ctbrowser::aot::ct_aot_site *, const std::uint64_t *,  \
        std::uint32_t, std::uint64_t, std::uint32_t, std::uint64_t *);
CT_ENTRY(_script_)
CT_ENTRY(raise2)
CT_ENTRY(loadTwo)
CT_ENTRY(fn)
#undef CT_ENTRY

namespace {

// The same files the pipeline compiles, not transcriptions of them. A
// compiled body bakes in the function index of every closure it builds, so a
// fixture that drifted in order would make a compiled body close over a
// different function while the interpreter and the compiled code agree on a
// stale answer.
constexpr std::string_view main_source =
#include "module-main.js.inc"
    ;
constexpr std::string_view dep_source =
#include "module-dep.js.inc"
    ;
constexpr std::string_view user_source =
#include "module-user.js.inc"
    ;

struct installed {
    const char * name;
    unsigned ordinal;
    ctbrowser::aot::ct_aot_entry_fn entry;
};

/// Every entry the build produced, so the "install everything" pass below is
/// not a hand-kept subset of what was compiled. `<script>` is the name the
/// compiler gives functions[0] and "" is what an anonymous function expression
/// gets.
const installed all_entries[] = {
    {"<script>", 0u, &ctc__script_},
    {"raise2", 0u, &ctc_raise2},
    {"loadTwo", 0u, &ctc_loadTwo},
    {"", 0u, &ctc_fn},
};

/// Only the top level, which is where three of the four opcodes are. Every
/// other function in main stays interpreted, so an arm that changes is an arm
/// this body changed.
const installed top_level_only[] = {{"<script>", 0u, &ctc__script_}};

/// One whole module graph, built and evaluated from scratch.
///
/// A fresh context per run, not merely a fresh record: the module top level is
/// where three of the four opcodes live, so comparing interpreted and compiled
/// runs means running that top level twice, and a module is a singleton within
/// one context - the `evaluated` flag is the whole of "a module is a
/// singleton".
struct graph {
    context cx;
    program dep;
    program main_module;
    program user;
    std::string loader_log;
    bool ok = true;
    std::string why;

    /// `patch` names which of main's protos get a compiled entry. Nothing else
    /// is ever patched: dep and user are the fixed points the comparison is
    /// taken against.
    ///
    /// `as_module_graph` false runs main through context::run instead of
    /// run_module, which leaves `current_module_` null - the case in which
    /// op::bind_export must not write.
    graph(std::span<const installed> patch, bool as_module_graph) {
        ctbrowser::script::install_builtins(cx);
        install_loader();

        dep = ctbrowser::script::compiler::compile(std::string{dep_source}, script_kind::module_);
        main_module =
            ctbrowser::script::compiler::compile(std::string{main_source}, script_kind::module_);
        user = ctbrowser::script::compiler::compile(std::string{user_source}, script_kind::module_);
        if (!dep.ok || !main_module.ok || !user.ok) {
            ok = false;
            why = "a fixture did not compile: " + dep.error + main_module.error + user.error;
            return;
        }

        // The protos are stamped with their module, which is the loader's job
        // and not the compiler's - browser::instantiate_module does exactly
        // this, under the comment "a specifier is the loader's name for a
        // file". Without it every referrer is the empty string and the arm that
        // checks the referrer would be checking nothing.
        for (function_proto & fn : main_module.functions) { fn.module = "main"; }
        for (function_proto & fn : dep.functions) { fn.module = "dep"; }
        for (function_proto & fn : user.functions) { fn.module = "user"; }

        for (const installed & want : patch) {
            unsigned seen = 0;
            function_proto * found = nullptr;
            for (function_proto & candidate : main_module.functions) {
                if (candidate.name != want.name) { continue; }
                if (seen++ == want.ordinal) { found = &candidate; }
            }
            if (found == nullptr) {
                ok = false;
                why = std::string{"no function_proto named `"} + want.name + "` in module-main.js";
                return;
            }
            found->aot_entry = want.entry;
        }

        auto & registry = cx.modules();
        // The registry is not keyed by the specifier as written, and that is
        // the point of `resolved`. `./dep.js` resolves to `dep` here, so a
        // lowering that skipped the resolution step raises "module `./dep.js`
        // was not loaded" instead of answering - which an identity map, the
        // thing a one-directory fixture produces by accident, would hide.
        registry["dep"].specifier = "dep";
        registry["main"].specifier = "main";
        registry["user"].specifier = "user";
        registry["main"].resolved["./dep.js"] = "dep";
        registry["user"].resolved["./main.js"] = "main";

        if (!as_module_graph) {
            // dep still has to be a real evaluated module, or every import in
            // main raises "has no export named" and the arm tests nothing.
            cx.instantiate_module(dep, registry["dep"]);
            if (const auto ran = cx.run_module(dep, registry["dep"]); !ran.ok) {
                ok = false;
                why = "dep did not run: " + ran.error;
                return;
            }
            // And under the specifier as written, because with no current
            // module there is no `resolved` table to go through - which is
            // exactly what context's keyed_by does. The copy shares the cells;
            // module_record holds `value`s, and a value is a heap pointer.
            registry["./dep.js"] = registry["dep"];
            if (const auto ran = cx.run(main_module); !ran.ok) {
                ok = false;
                why = "main did not run as a plain program: " + ran.error;
            }
            return;
        }

        // Instantiate every module before evaluating any; that is the whole of
        // the two-phase split. The cell an importer takes and the cell the
        // exporter writes must be the same object, and the only way to
        // guarantee that is for one of them not to make it.
        cx.instantiate_module(dep, registry["dep"]);
        cx.instantiate_module(main_module, registry["main"]);
        cx.instantiate_module(user, registry["user"]);

        // Depth-first post-order, which is what the specification defines and
        // what browser::evaluate_module walks: dep, then main, then user.
        if (!evaluate(dep, "dep")) { return; }
        if (!evaluate(main_module, "main")) { return; }
        (void)evaluate(user, "user");
    }

    bool evaluate(const program & prog, const char * key) {
        const auto ran = cx.run_module(prog, cx.modules()[key]);
        if (!ran.ok) {
            ok = false;
            why = std::string{"module "} + key + " did not run: " + ran.error;
        }
        return ran.ok;
    }

    void install_loader() {
        cx.set_module_loader(
            [this](context & inner, const std::string & specifier, const std::string & referrer) {
                // Record what it was asked where JavaScript can read it. The
                // referrer is the parameter no compiler can fill - the helper
                // reads it off the frame - so a lowering that dropped it would
                // still return two promises and answer "object" twice. This is
                // the only thing in the fixture that can see it.
                loader_log += (loader_log.empty() ? "" : ",") + referrer + ">" + specifier;
                inner.define_global("LOADER_SAW", inner.string(loader_log));
                if (specifier == "./dep.js") {
                    return inner.make_promise(inner.string("dep-namespace"), false);
                }
                // A missing module is an already-rejected settled promise, not
                // a failure, which is what browser/scripts.cpp's own loader
                // answers. A lowering that treated it as a control-flow event
                // would return out of the body and the second half of the
                // answer would vanish.
                return inner.make_promise(
                    inner.make_error("Error", "module `" + specifier + "` not found"), true);
            });
        // The fixture assigns LOADER_SAW itself before importing; this only has
        // to exist so a read before the first import is not a missing global.
        cx.define_global("LOADER_SAW", cx.string(""));
    }

    [[nodiscard]] std::string drive(unsigned which) {
        const value arguments[] = {value::number(static_cast<double>(which))};
        cx.call(cx.global("DRIVE"), std::span<const value>{arguments}, value::undefined());
        return cx.to_string(cx.global("OUT"));
    }

    /// What user.js sees of main's export, which is the only view of
    /// op::bind_export's decision.
    [[nodiscard]] std::string read_mine() {
        return cx.to_string(
            cx.call(cx.global("READ_MINE"), std::span<const value>{}, value::undefined()));
    }
};

/// One DRIVE(which) case: its number, a display name, and its expected answer.
struct arm {
    unsigned which;
    const char * name;
    /// Why this arm would differ, so a failure says what broke rather than
    /// only that something did.
    const char * separates;
    /// The hard-coded expected answer, for the paths the comparison cannot
    /// see - which here is every path, because both sides call one shared
    /// context member.
    const char * expected;
};

void compare(const arm & each, std::span<const installed> patch, const char * label) {
    graph interpreted_run{std::span<const installed>{}, /*as_module_graph=*/true};
    graph compiled_run{patch, /*as_module_graph=*/true};
    if (!interpreted_run.ok || !compiled_run.ok) {
        std::printf("%-16s FAILED - %s\n", each.name,
                    interpreted_run.ok ? compiled_run.why.c_str() : interpreted_run.why.c_str());
        ++ctbrowser_test_failures;
        return;
    }
    const std::string interpreted = interpreted_run.drive(each.which);
    const std::string generated = compiled_run.drive(each.which);

    if (interpreted == "<the arm did not run>" || interpreted == "<the module did not run>") {
        std::printf("%-16s FAILED - DRIVE(%u) set nothing, so the arm threw or does not exist\n",
                    each.name, each.which);
        ++ctbrowser_test_failures;
        return;
    }
    if (interpreted != each.expected) {
        std::printf("%-16s FAILED - the INTERPRETER answered %s where %s is correct, so something "
                    "shared by both tiers is wrong\n",
                    each.name, interpreted.c_str(), each.expected);
        ++ctbrowser_test_failures;
        return;
    }
    if (interpreted == generated) {
        std::printf("%-16s ok   %-4s  %s\n", each.name, label, interpreted.c_str());
    } else {
        std::printf("%-16s FAILED (%s)\n    interpreted %s\n    compiled    %s\n    separates:  "
                    "%s\n",
                    each.name, label, interpreted.c_str(), generated.c_str(), each.separates);
        ++ctbrowser_test_failures;
    }
}

/// The two checks that are not one of DRIVE's arms, run the same way.
void compare_pair(const char * name, const std::string & interpreted, const std::string & generated,
                  const char * expected, const char * separates) {
    if (interpreted != expected) {
        std::printf("%-16s FAILED - the INTERPRETER answered %s where %s is correct, so something "
                    "shared by both tiers is wrong\n",
                    name, interpreted.c_str(), expected);
        ++ctbrowser_test_failures;
        return;
    }
    if (interpreted == generated) {
        std::printf("%-16s ok         %s\n", name, interpreted.c_str());
        return;
    }
    std::printf("%-16s FAILED\n    interpreted %s\n    compiled    %s\n    separates:  %s\n", name,
                interpreted.c_str(), generated.c_str(), separates);
    ++ctbrowser_test_failures;
}

} // namespace

int main() {
    const arm arms[] = {
        {0u, "named import",
         "a CELL against a copied value - bump() writes dep's own local, and a copy answers 1 - "
         "and the b/c swap, which would raise rather than answer",
         "2/D/7"},
        {1u, "namespace",
         "the accessor over the cell against a snapshot, and the per-record identity cache - a "
         "namespace built per opcode answers false for ns === ns2",
         "2/D/7/true"},
        {2u, "one record twice",
         "whether the named import and the namespace came from the SAME record - a skipped "
         "resolved-specifier lookup would raise, and a second record would answer two numbers",
         "3/3/true"},
        {3u, "dynamic import",
         "the REFERRER, which no operand carries and the helper reads off the frame, and a "
         "missing module answering an already-rejected promise rather than a failure",
         "main>./dep.js,main>./nowhere.js/object/object/false"},
        {4u, "own export",
         "the adopted cell being writable at all from inside the module - and the bx() decode, "
         "which would publish a different name",
         "12"},
    };

    for (const arm & each : arms) { compare(each, top_level_only, "top"); }

    // ---- And every entry installed -----------------------------------------
    //
    // As in Runtime/Differential/Differential.cpp's all-entries pass: a
    // compiled caller reaching a compiled callee goes through paths a single
    // patch never exercises. It separates nothing on its own; it is the check
    // that the arms above did not agree only because everything around them
    // was interpreted.
    for (const arm & each : arms) { compare(each, all_entries, "all"); }

    // ---- What op::bind_export actually decides ------------------------------
    //
    // No arm above can see it. Reading `mine` from inside main.js proves its
    // local works whatever cell it holds; what bind_export decides is whether
    // the cell in main's record is that same box. user.js takes the record's
    // cell through op::load_import and reads it after main has written - so a
    // lowering that published the register's own freshly-made cell instead of
    // adopting the record's answers `undefined` here and passes every arm
    // above.
    {
        graph interpreted_run{std::span<const installed>{}, true};
        graph compiled_run{all_entries, true};
        if (!interpreted_run.ok || !compiled_run.ok) {
            std::printf("adopted cell     FAILED - %s\n", interpreted_run.ok
                                                              ? compiled_run.why.c_str()
                                                              : interpreted_run.why.c_str());
            ++ctbrowser_test_failures;
        } else {
            compare_pair("adopted cell", interpreted_run.read_mine(), compiled_run.read_mine(),
                         "12",
                         "publishing the register's own cell against adopting the record's - the "
                         "importer then holds a box nobody ever writes to");
        }
    }

    // ---- And the case with no module at all --------------------------------
    //
    // op::bind_export's write is conditional: outside a module the interpreter
    // does not touch the destination register, and that register holds the
    // cell predeclare_locals just made for the local being exported. A lowering
    // that wrote undefined there would throw that cell away, every later write
    // would land on a non-cell and be dropped, and `mine` would read undefined.
    //
    // ct_aot_status has no "leave the destination alone" result, so the
    // lowering pre-seeds the out-slot with the register's current value; this
    // case checks that it does.
    {
        graph interpreted_run{std::span<const installed>{}, /*as_module_graph=*/false};
        graph compiled_run{all_entries, /*as_module_graph=*/false};
        if (!interpreted_run.ok || !compiled_run.ok) {
            std::printf("no module        FAILED - %s\n", interpreted_run.ok
                                                              ? compiled_run.why.c_str()
                                                              : interpreted_run.why.c_str());
            ++ctbrowser_test_failures;
        } else {
            compare_pair("no module", interpreted_run.drive(4u), compiled_run.drive(4u), "12",
                         "the conditional write - a compiled body that stored undefined destroys "
                         "the local's cell, and every later write to `mine` is dropped");
        }
    }

    if (ctbrowser_test_failures == 0) {
        std::printf("\nevery module arm agrees with the interpreter\n");
    }
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
