// ct262 - the host shell test262 is run through.
//
// test262 does not test an engine; it tests a HOST that has one. The suite's
// contract (its INTERPRETING.md) is a list of things the runner must provide -
// `print`, a `$262` object, a fresh realm per test, the two harness files
// evaluated as global code before the test, a strict-mode transformation, an
// ES-module mode - and every one of those is a decision about ctbrowser rather
// than about JavaScript. This binary is where those decisions live, so
// `tools/check/test262.py` can stay a process runner and a table.
//
//   ct262 [--prelude harness/assert.js]... [--strict] [--module] test.js
//
// ONE PROCESS, ONE TEST, ONE REALM. `script::context` is one agent and one
// heap, and the suite requires a test to see a realm nothing else has touched -
// so isolation here is the process, which also makes a hang or a crash cost one
// test instead of the run. The runner caps the time and the address space.
//
// WHAT IT PRINTS is the whole interface, and it is deliberately not prose. A
// failing run writes ONE tab-separated line to stderr:
//
//   ct262<TAB>error<TAB><phase><TAB><constructor><TAB><name><TAB><message>
//
// `phase` is the suite's own vocabulary (parse / resolution / runtime) with two
// of this engine's own added, because they are not the same thing and folding
// them together is how a conformance number flatters itself:
//
//   refusal   the COMPILER refused a construct it has not implemented -
//             "tagged template literals are not in this VM subset". The source
//             is valid JavaScript. A `negative: {phase: parse}` test would
//             otherwise be scored a PASS for the engine's own gap, which is
//             precisely backwards, so it gets its own phase and never matches
//             SyntaxError.
//   vm        the run ended with no thrown value at all - the allocation
//             ceiling, the call-stack ceiling. Nothing was thrown, so no
//             `negative` test can match it.
//
// `constructor` is `thrown.constructor.name` and `name` is `thrown.name`, both
// reported because they DISAGREE here: `context::throw_error` builds every
// error on the one Error prototype, so an engine-raised TypeError has
// `name === "TypeError"` and `constructor === Error`. The runner is told both
// and documents which it accepted; inventing one number out of two would hide a
// real gap in the engine.
//
// Exit status: 0 the test ran to completion, 1 it did not (the line above says
// how), 3 the HOST failed - a file that will not open, a harness file that will
// not parse. 3 is never a test result.
#include <ctbrowser.hpp>

#include "options.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ctbrowser::script::context;
using ctbrowser::script::program;
using ctbrowser::script::run_result;
using ctbrowser::script::value;

[[nodiscard]] std::string read_file(const std::filesystem::path & path, bool & ok) {
    // A DIRECTORY OPENS. `import('')` resolves to the test's own directory,
    // and libstdc++ opens it fine and then throws ios_failure from the read -
    // which nothing here catches, so the process aborted. Not a file, not ok.
    if (!std::filesystem::is_regular_file(path)) {
        ok = false;
        return {};
    }
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        ok = false;
        return {};
    }
    ok = true;
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

// ONE LINE, WHATEVER THE MESSAGE CONTAINS. A parse error carries the offending
// source in it and a stack trace carries newlines; a report that can be two
// lines is a report the runner has to guess the end of.
[[nodiscard]] std::string one_line(std::string text) {
    for (char & c : text) {
        if (c == '\n' || c == '\r' || c == '\t') { c = ' '; }
    }
    return text;
}

void report(std::string_view phase, std::string_view ctor, std::string_view name,
            const std::string & message) {
    std::fprintf(stderr, "ct262\terror\t%.*s\t%.*s\t%.*s\t%s\n", static_cast<int>(phase.size()),
                 phase.data(), static_cast<int>(ctor.size()), ctor.data(),
                 static_cast<int>(name.size()), name.data(), one_line(message).c_str());
}

void host_error(const std::string & message) {
    std::fprintf(stderr, "ct262\thost\t%s\n", one_line(message).c_str());
}

// A COMPILE FAILURE IS TWO DIFFERENT ANSWERS. `compiler::compile` sets the same
// `ok = false` for source the parser REJECTED and for source the compiler has
// not implemented, and only the first is a SyntaxError. The parser's own errors
// are the ones it prefixes "parse error:" (compile.cpp writes exactly that);
// everything else came from `compiler_impl::fail` and is this engine's gap.
[[nodiscard]] bool is_parse_error(const std::string & message) {
    return message.starts_with("parse error:");
}

void report_compile_failure(const program & prog) {
    if (is_parse_error(prog.error)) {
        // The suite's rule, not a guess: source text that does not parse must
        // produce a SyntaxError, and this engine has no other way to say so.
        report("parse", "SyntaxError", "SyntaxError", prog.error);
    } else {
        report("refusal", "", "", prog.error);
    }
}

// WHAT WAS THROWN, BY NAME. `constructor.name` first, because that is what
// test262's own `assert.throws` compares and what `negative: {type}` means.
void report_run_failure(context & cx, std::string_view phase, const run_result & result) {
    const value thrown = cx.last_thrown();
    if (thrown.is_undefined()) {
        // No thrown value: a VM-level refusal, not an exception. `raise()` sets
        // the failure flag and leaves `thrown_` alone.
        report("vm", "", "", result.error);
        return;
    }
    std::string ctor;
    std::string name;
    if (thrown.is_object_like()) {
        const value constructor = cx.lookup_property(thrown, "constructor");
        if (constructor.is_callable()) {
            const value ctor_name = cx.lookup_property(constructor, "name");
            if (!ctor_name.is_undefined()) { ctor = cx.to_string(ctor_name); }
        }
        const value own_name = cx.lookup_property(thrown, "name");
        if (!own_name.is_undefined()) { name = cx.to_string(own_name); }
    } else {
        // `$DONOTEVALUATE()` throws a STRING, and a test that reaches it has
        // failed whatever else is true - so the type has to survive the report.
        name = std::string{"<"} + std::string{context::type_of(thrown)} + ">";
    }
    report(phase, ctor, name, result.error);
}

// --- the host objects ----------------------------------------------------

// `print`, which is how an async test says it finished: the suite's
// doneprintHandle.js calls it with "Test262:AsyncTestComplete". stdout, and
// flushed, because the runner reads it after the process is gone.
void install_print(context & cx) {
    cx.define_native("print", [](context & c, std::span<value> args) {
        std::string line;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i > 0) { line += ' '; }
            line += c.to_string(args[i]);
        }
        line += '\n';
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fflush(stdout);
        return value::undefined();
    });
}

// `$262`, with the four properties this host can honestly provide and stubs
// that THROW for the rest.
//
// A stub that returns undefined instead of throwing is the one thing that must
// not happen here: a `$262.createRealm()` returning undefined would let a test
// measure one realm as two and pass some of its assertions. A throw makes the
// test fail, which is the truth - and the runner SKIPS those tests by feature
// rather than counting the failures, so the throw is a backstop rather than
// the plan.
void install_262(context & cx) {
    auto * host = static_cast<ctbrowser::script::object_object *>(cx.make_object().as_heap());
    const auto method = [&cx, host](const char * name, ctbrowser::script::native_fn fn) {
        host->set(name, value::object(
                            cx.allocate<ctbrowser::script::native_object>(name, std::move(fn))));
    };

    // `global`: AN ORDINARY OBJECT, AND IT IS NOT THE GLOBAL OBJECT. This
    // engine's globals are a map on the context, not properties of an object -
    // `globalThis` exists only as a DOM binding the Shell installs (see
    // lib/Shell/bindings/window/window.cpp), and the script engine alone has no global
    // object at all. So this is the property the suite requires, holding the
    // only thing there is to put in it. Tests that reach the global through it
    // FAIL rather than silently measuring a different object, and the gap is
    // written down in docs/test262.md rather than papered over here.
    host->set("global", cx.make_object());

    // `evalScript`: parse and run the string as a CLASSIC SCRIPT in this realm,
    // exactly as `new Function` already does - the context owns the program, so
    // it outlives the closures the script leaves behind.
    method("evalScript", [](context & c, std::span<value> args) {
        const std::string source = args.empty() ? std::string{} : c.to_string(args[0]);
        program compiled = ctbrowser::script::compiler::compile_script(source);
        if (!compiled.ok) {
            c.throw_error("SyntaxError", compiled.error);
            return value::undefined();
        }
        return c.run_nested(c.own_program(std::move(compiled)));
    });

    // `gc`: the real collector, not a no-op. The VM's mark-sweep is precise over
    // its roots, so this is a genuine collection and a WeakRef test measures
    // something.
    method("gc", [](context & c, std::span<value>) {
        return value::number(static_cast<double>(c.collect()));
    });

    // `createRealm`: REFUSED, and it cannot be otherwise today. A realm here is
    // a `script::context`, which owns its heap and sweeps it in its destructor;
    // a value from one context is a raw pointer into another context's heap, so
    // handing one across is a use-after-free waiting for the first GC. The
    // `cross-realm` feature is on the runner's skip list for this reason.
    method("createRealm", [](context & c, std::span<value>) {
        c.throw_error("TypeError",
                      "$262.createRealm: this host has one realm - a script::context owns its "
                      "heap, so a value cannot cross between two");
        return value::undefined();
    });
    // `detachArrayBuffer`: DetachArrayBuffer(buffer) with an undefined key,
    // which is exactly what ArrayBuffer.prototype.transfer does to its
    // receiver (25.1.6.13 step 20) - the copy it makes is dropped. A
    // non-buffer or an immutable one throws there, as the host hook would.
    method("detachArrayBuffer", [](context & c, std::span<value> a) {
        const value transfer =
            c.lookup_property(c.lookup_property(c.global("ArrayBuffer"), "prototype"), "transfer");
        (void)c.call(transfer, {}, a.empty() ? value::undefined() : a[0]);
        return value::undefined();
    });

    // `agent`: an OBJECT whose every method throws, rather than an absent
    // property. `typeof $262.agent` is what a test looks at before it decides
    // it can run, and an absent agent and a present-but-useless one are
    // different answers to that.
    auto * agent = static_cast<ctbrowser::script::object_object *>(cx.make_object().as_heap());
    for (const char * name : {"start", "broadcast", "getReport", "sleep", "monotonicNow"}) {
        agent->set(name, value::object(cx.allocate<ctbrowser::script::native_object>(
                             name, [](context & c, std::span<value>) {
                                 c.throw_error("TypeError",
                                               "$262.agent: this host runs one agent on one "
                                               "thread and has no SharedArrayBuffer");
                                 return value::undefined();
                             })));
    }
    host->set("agent", value::object(agent));

    // `AbstractModuleSource`: the %AbstractModuleSource% intrinsic, which is
    // not a global - the engine keeps it on Function.prototype under a private
    // key (builtins/objects/function.cpp) for exactly this.
    if (auto * fn_proto = cx.prototype(context::proto_kind::function)) {
        if (const value * intrinsic =
                fn_proto->find(CTBROWSER_PRIVATE_KEY("AbstractModuleSource"))) {
            host->set("AbstractModuleSource", *intrinsic);
        }
    }

    cx.define_global("$262", value::object(host));
}

// --- modules -------------------------------------------------------------

// The two-pass module loader, over the FILESYSTEM rather than an asset
// registry. It is the shape `lib/Shell/browser/scripts.cpp` uses and for the same
// reason: every binding in the graph has to exist before any of the graph runs,
// or a cycle hands the importer a name whose exporter has not started.
//
// test262's specifiers all begin "./" and name a file beside the importer
// (INTERPRETING.md says so), so resolution is `std::filesystem` and nothing
// more - no URL, no import map.
class module_loader {
public:
    explicit module_loader(context & cx) : cx_{cx} {}

    void instantiate(const std::filesystem::path & file) {
        const std::string key = std::filesystem::weakly_canonical(file).generic_string();
        auto & registry = cx_.modules();
        if (registry.find(key) != registry.end()) { return; }

        bool ok = false;
        const std::string source = read_file(file, ok);
        if (!ok) {
            note("module `" + key + "` could not be read");
            return;
        }
        // Registered BEFORE its dependencies, so a cycle stops here rather than
        // descending for ever.
        ctbrowser::script::module_record & record = registry[key];
        record.specifier = key;

        // A JSON MODULE - `import v from './x.json' with { type: 'json' }`
        // (16.2.1.8 ParseJSONModule, CreateDefaultExportSyntheticModule): the
        // text parsed as JSON NOW, so text that is not JSON is the graph's
        // SyntaxError while it loads, and the value its one export, `default`.
        // By the file's extension, which is how the suite names them; the
        // attribute itself does not reach the program.
        if (file.extension() == ".json") {
            instantiate_json(record, key, source);
            return;
        }

        auto compiled = std::make_unique<program>(
            ctbrowser::script::compiler::compile(source, ctbrowser::script::script_kind::module_));
        if (!compiled->ok) {
            if (!failed_) {
                failed_ = true;
                compile_error_ = *compiled;
                failed_key_ = key;
            }
            owned_.push_back(std::move(compiled));
            return;
        }
        for (ctbrowser::script::function_proto & fn : compiled->functions) { fn.module = key; }
        cx_.instantiate_module(*compiled, record);

        std::vector<std::filesystem::path> needed;
        for (const std::string & written : compiled->imports) {
            const std::filesystem::path target = file.parent_path() / written;
            record.resolved[written] = std::filesystem::weakly_canonical(target).generic_string();
            needed.push_back(target);
        }
        owned_.push_back(std::move(compiled));
        ++depth_;
        for (const std::filesystem::path & target : needed) { instantiate(target); }
        --depth_;
        if (depth_ == 0) { wire_reexports(); }
    }

    void instantiate_json(ctbrowser::script::module_record & record, const std::string & key,
                          const std::string & source) {
        const value json = cx_.global("JSON");
        const value parse = cx_.lookup_property(json, "parse");
        const value text = cx_.string(source);
        bool threw = false;
        value thrown = value::undefined();
        const value parsed =
            cx_.call_fenced(parse, std::span<const value>{&text, 1}, json, threw, thrown);
        if (threw || cx_.failed()) {
            (void)cx_.take_error();
            note("`" + key + "` is not a JSON text");
            return;
        }
        // An empty module program, for evaluate() to run; the export is the
        // record's own cell.
        auto compiled = std::make_unique<program>(
            ctbrowser::script::compiler::compile("", ctbrowser::script::script_kind::module_));
        for (ctbrowser::script::function_proto & fn : compiled->functions) { fn.module = key; }
        cx_.instantiate_module(*compiled, record);
        record.exports["default"] =
            value::object(cx_.allocate<ctbrowser::script::cell_object>(parsed));
        owned_.push_back(std::move(compiled));
    }

    // `export { a } from './m.js'` AND `export * from './m.js'` - 16.2.1.6.3
    // ResolveExport, over the WHOLE graph once it is loaded. A re-export is an
    // alias of the other module's cell, not a copy, so a write there is a
    // read here. Wired to a fixed point rather than deepest-first, because a
    // cycle of index files has no deepest member: an edge whose source is
    // not bound yet simply waits for the next pass, and a genuinely circular
    // one never resolves - which is the specification's null. An explicit
    // edge outranks a star's, a name the module binds itself outranks both,
    // `export *` never carries `default`, and one name reached through two
    // stars as two different cells is AMBIGUOUS: not exported at all. An
    // explicit edge left unresolved at the end is the resolution SyntaxError.
    void wire_reexports() {
        auto & registry = cx_.modules();
        std::set<std::pair<std::string, std::string>> from_star;
        std::set<std::pair<std::string, std::string>> ambiguous;
        for (bool changed = true; changed;) {
            changed = false;
            for (auto & [key, record] : registry) {
                if (record.compiled == nullptr) { continue; }
                for (const bool explicit_pass : {true, false}) {
                    for (const auto & edge : record.compiled->reexports) {
                        if (edge.source.empty() == explicit_pass) { continue; }
                        const auto mapped = record.resolved.find(edge.from);
                        const std::string from =
                            mapped == record.resolved.end() ? edge.from : mapped->second;
                        const auto source = registry.find(from);
                        if (source == registry.end()) { continue; }
                        std::vector<std::pair<std::string, value>> wire;
                        if (explicit_pass) {
                            const auto cell = source->second.exports.find(edge.source);
                            if (cell != source->second.exports.end()) {
                                wire.emplace_back(edge.exported, cell->second);
                            }
                        } else {
                            for (const auto & [name, cell] : source->second.exports) {
                                if (name != "default") { wire.emplace_back(name, cell); }
                            }
                        }
                        for (const auto & [name, cell] : wire) {
                            const auto held = record.exports.find(name);
                            const bool via_star = from_star.count({key, name}) != 0;
                            if (held != record.exports.end() && !(via_star && explicit_pass)) {
                                if (!explicit_pass && via_star &&
                                    !held->second.strict_equals(cell)) {
                                    record.exports.erase(held);
                                    ambiguous.insert({key, name});
                                    changed = true;
                                }
                                continue;
                            }
                            if (!explicit_pass && ambiguous.count({key, name}) != 0) { continue; }
                            record.exports[name] = cell;
                            if (explicit_pass) {
                                from_star.erase({key, name});
                                ambiguous.erase({key, name});
                            } else {
                                from_star.insert({key, name});
                            }
                            changed = true;
                        }
                    }
                }
            }
        }
        for (const auto & [key, record] : registry) {
            if (record.compiled == nullptr) { continue; }
            for (const auto & edge : record.compiled->reexports) {
                if (edge.source.empty() ||
                    record.exports.find(edge.exported) != record.exports.end()) {
                    continue;
                }
                note("`" + edge.from + "` has no export named `" + edge.source + "`");
            }
        }
        check_imports();
    }

    // AND EVERY NAMED IMPORT, the same way (16.2.1.6.4 InitializeEnvironment
    // step 7: ResolveExport of each ImportEntry, null or ambiguous a
    // SyntaxError) - which the VM only found at the import's first read, as a
    // "has no export named" raise at run time. The program records specifiers,
    // not import entries; they are the module entry's `load_import`s (names[b]
    // the export, names[c] the specifier), which is what the VM reads too.
    void check_imports() {
        auto & registry = cx_.modules();
        for (const auto & [key, record] : registry) {
            if (record.compiled == nullptr || record.compiled->functions.empty()) { continue; }
            const ctbrowser::script::function_proto & entry = record.compiled->functions[0];
            for (const ctbrowser::script::instruction & in : entry.code) {
                if (in.code != ctbrowser::script::op::load_import) { continue; }
                if (in.b >= entry.names.size() || in.c >= entry.names.size()) { continue; }
                const std::string & name = entry.names[in.b];
                const std::string & written = entry.names[in.c];
                const auto mapped = record.resolved.find(written);
                const auto source =
                    registry.find(mapped == record.resolved.end() ? written : mapped->second);
                if (source == registry.end() || source->second.compiled == nullptr) { continue; }
                if (source->second.exports.find(name) == source->second.exports.end()) {
                    note("`" + written + "` has no export named `" + name + "`");
                }
            }
        }
    }

    // POST-ORDER, each module once: a dependency has finished before the module
    // importing it reaches its first statement.
    run_result evaluate(const std::filesystem::path & file) {
        const std::string key = std::filesystem::weakly_canonical(file).generic_string();
        auto & registry = cx_.modules();
        const auto found = registry.find(key);
        if (found == registry.end() || found->second.evaluated ||
            found->second.compiled == nullptr) {
            return run_result{};
        }
        found->second.evaluated = true;
        const program * const compiled = found->second.compiled;
        std::vector<std::string> needed;
        for (const std::string & written : compiled->imports) {
            // `import defer` (16.2.2): linked, left for its namespace's
            // first read - context::deferred_module_namespace calls back
            // into this very function through set_module_evaluator.
            if (std::find(compiled->deferred_imports.begin(), compiled->deferred_imports.end(),
                          written) != compiled->deferred_imports.end()) {
                continue;
            }
            const auto mapped = found->second.resolved.find(written);
            needed.push_back(mapped == found->second.resolved.end() ? written : mapped->second);
        }
        for (const std::string & target : needed) {
            const run_result nested = evaluate(std::filesystem::path{target});
            if (!nested.ok) { return nested; }
        }
        // Re-found: the recursion above can insert into the registry, and a
        // flat_map invalidates references when it does.
        const auto self = registry.find(key);
        if (self == registry.end()) { return run_result{}; }
        return cx_.run_module(*compiled, self->second);
    }

    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] const program & compile_error() const noexcept { return compile_error_; }
    // Which module the compile error is in - the test file or a dependency.
    [[nodiscard]] const std::string & failed_key() const noexcept { return failed_key_; }
    [[nodiscard]] const std::string & resolution_error() const noexcept { return resolution_; }
    // A dynamic import that failed is one rejected promise, not a failed run:
    // clear the record so the next import starts clean.
    void reset_failure() {
        failed_ = false;
        compile_error_ = program{};
        failed_key_.clear();
        resolution_.clear();
    }

private:
    void note(std::string message) {
        if (resolution_.empty()) { resolution_ = std::move(message); }
    }

    context & cx_;
    std::vector<std::unique_ptr<program>> owned_;
    std::size_t depth_ = 0; // instantiate() recursing: the graph is wired when it is back at 0
    bool failed_ = false;
    program compile_error_;
    std::string failed_key_;
    std::string resolution_;
};

} // namespace

int main(int argc, char ** argv) {
    namespace po = boost::program_options;
    // STRINGS, NOT PATHS. Boost would read a std::filesystem::path through
    // lexical_cast and so through std::quoted: a path with a space in it is
    // refused, and one wrapped in quotes loses them.
    std::vector<std::string> preludes;
    std::string test;
    bool strict = false;
    bool as_module = false;
    po::options_description options{
        "usage: ct262 [--prelude <harness.js>]... [--strict] [--module] <test.js>\n"
        "  runs ONE test262 file in a fresh realm. Exit 0 ran to completion, 1 did not,\n"
        "  3 the host failed. tools/check/test262.py is what drives it"};
    auto add = options.add_options();
    add("prelude", po::value(&preludes),
        "a harness file, run as global code before the test; repeatable, run in the order given");
    add("strict", po::bool_switch(&strict), "prepend \"use strict\"; to the test");
    add("module", po::bool_switch(&as_module), "run the test as an ES module; --strict is ignored");
    add("test", po::value(&test)->required(), "the test262 file to run");
    po::positional_options_description positional;
    positional.add("test", 1);
    if (!ctbrowser_cli::parse_options(argc, argv, options, positional)) { return 3; }

    context cx;
    ctbrowser::script::install_builtins(cx);
    install_print(cx);
    install_262(cx);

    // THE HARNESS FILES ARE THEIR OWN PROGRAMS, not text glued to the front of
    // the test. Two reasons, and the second is the one that matters: the suite
    // says they are evaluated as global code before the test, which is what
    // this is; and a concatenated test whose PARSE fails cannot be told apart
    // from a harness file whose parse fails, so every `negative: {phase: parse}`
    // test would be scored on the whole blob. Compiling them separately makes a
    // harness failure exit 3 - a host error, never a test result.
    for (const std::string & prelude : preludes) {
        bool ok = false;
        const std::string source = read_file(prelude, ok);
        if (!ok) {
            host_error("cannot read harness file " + prelude);
            return 3;
        }
        // OWNED BY THE CONTEXT, not by this loop. A harness file's whole
        // purpose is to leave functions behind for the test to call, and a
        // closure holds a `function_proto *` into the program it came from - so
        // a `program` that dies at the end of this iteration leaves the global
        // `assert` pointing into freed memory. It does not fail immediately:
        // `assert.sameValue(1, 1)` returns before it touches anything that has
        // been reused, and the first test to FAIL an assertion segfaults inside
        // the message it was building. Which is exactly how this was found -
        // every passing test passed and every failing one crashed.
        const program & compiled =
            cx.own_program(ctbrowser::script::compiler::compile_script(source));
        if (!compiled.ok) {
            host_error(prelude + ": " + compiled.error);
            return 3;
        }
        const run_result ran = cx.run(compiled);
        if (!ran.ok) {
            host_error(prelude + ": " + ran.error);
            return 3;
        }
    }

    module_loader loader{cx};
    // WHAT `import(specifier)` CALLS, in both modes: a script may import a
    // module and a module may import another lazily. The graph is walked
    // the same two-pass way; a compile failure or a missing file REJECTS the
    // promise (16.2.1.9 HostLoadImportedModule's failure path) rather than
    // failing the run, and an evaluation that threw rejects with what it
    // threw. Settled at once, like the shell's, because the filesystem is
    // synchronous. A referrer that is not a module - the test script - is
    // resolved beside the test file.
    cx.set_module_evaluator([&loader](context &, ctbrowser::script::module_record & record) {
        return loader.evaluate(std::filesystem::path{record.specifier}).ok;
    });
    cx.set_module_loader([&loader, &test](context & c, const std::string & specifier,
                                          const std::string & referrer) {
        const std::filesystem::path base = referrer.empty()
                                               ? std::filesystem::path{test}.parent_path()
                                               : std::filesystem::path{referrer}.parent_path();
        const std::filesystem::path target = base / specifier;
        loader.instantiate(target);
        if (loader.failed()) {
            const program & bad = loader.compile_error();
            loader.reset_failure();
            return c.make_promise(c.make_error("SyntaxError", bad.error), true);
        }
        if (!loader.resolution_error().empty()) {
            const std::string why = loader.resolution_error();
            loader.reset_failure();
            return c.make_promise(c.make_error("SyntaxError", why), true);
        }
        const run_result ran = loader.evaluate(target);
        if (!ran.ok) {
            const value thrown = c.last_thrown();
            return c.make_promise(thrown.is_undefined() ? c.make_error("Error", ran.error) : thrown,
                                  true);
        }
        auto & registry = c.modules();
        const auto found =
            registry.find(std::filesystem::weakly_canonical(target).generic_string());
        if (found == registry.end()) {
            return c.make_promise(c.make_error("Error", "module `" + specifier + "` did not load"),
                                  true);
        }
        return c.make_promise(c.module_namespace(found->second), false);
    });

    if (as_module) {
        // `--strict` is not applied: a module is strict code by definition, and
        // the suite never sets both.
        loader.instantiate(test);
        if (loader.failed()) {
            // A DEPENDENCY THAT DOES NOT PARSE fails the graph while it loads,
            // which is the suite's "resolution" phase (instn-resolve-err-
            // syntax-*.js); only the test file's own syntax error is "parse".
            const program & bad = loader.compile_error();
            if (is_parse_error(bad.error) &&
                loader.failed_key() != std::filesystem::weakly_canonical(test).generic_string()) {
                report("resolution", "SyntaxError", "SyntaxError", bad.error);
            } else {
                report_compile_failure(bad);
            }
            return 1;
        }
        if (!loader.resolution_error().empty()) {
            // The suite expects a SyntaxError from a resolution failure, which
            // is what a missing module or a missing export is here.
            report("resolution", "SyntaxError", "SyntaxError", loader.resolution_error());
            return 1;
        }
        const run_result ran = loader.evaluate(test);
        if (!ran.ok) {
            report_run_failure(cx, "runtime", ran);
            return 1;
        }
        return 0;
    }

    bool ok = false;
    std::string source = read_file(test, ok);
    if (!ok) {
        host_error("cannot read " + test);
        return 3;
    }
    // The suite's own transformation, character for character: the directive,
    // a semicolon and a newline, before anything else the metadata asks for.
    if (strict) { source = "\"use strict\";\n" + source; }

    // A TEST IS A Script (16.1), so compile_script: a top-level `return` or
    // `new.target` is the SyntaxError the suite expects, where compile() takes
    // it as the embedder's way of returning a value.
    const program & compiled = cx.own_program(ctbrowser::script::compiler::compile_script(source));
    if (!compiled.ok) {
        report_compile_failure(compiled);
        return 1;
    }
    const run_result ran = cx.run(compiled);
    if (!ran.ok) {
        report_run_failure(cx, "runtime", ran);
        return 1;
    }
    return 0;
}
