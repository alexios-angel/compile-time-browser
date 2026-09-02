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

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
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
// not happen here: `$262.detachArrayBuffer(b)` returning undefined leaves the
// buffer attached, the test's assertions then measure an attached buffer, and
// some of them pass. A throw makes the test fail, which is the truth - and the
// runner SKIPS those tests by name rather than counting the failures, so the
// throw is a backstop rather than the plan.
void install_262(context & cx) {
    auto * host = static_cast<ctbrowser::script::object_object *>(cx.make_object().as_heap());
    const auto method = [&cx, host](const char * name, ctbrowser::script::native_fn fn) {
        host->set(name, value::object(
                            cx.allocate<ctbrowser::script::native_object>(name, std::move(fn))));
    };

    // `global`: AN ORDINARY OBJECT, AND IT IS NOT THE GLOBAL OBJECT. This
    // engine's globals are a map on the context, not properties of an object -
    // `globalThis` exists only as a DOM binding the Shell installs (see
    // lib/Shell/bindings/window.cpp), and the script engine alone has no global
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
        program compiled = ctbrowser::script::compiler::compile(source);
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
    method("detachArrayBuffer", [](context & c, std::span<value>) {
        c.throw_error("TypeError", "$262.detachArrayBuffer: this engine has no detach operation");
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

    cx.define_global("$262", value::object(host));
}

// --- modules -------------------------------------------------------------

// The two-pass module loader, over the FILESYSTEM rather than an asset
// registry. It is the shape `lib/Shell/browser.cpp` uses and for the same
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

        auto compiled = std::make_unique<program>(
            ctbrowser::script::compiler::compile(source, ctbrowser::script::script_kind::module_));
        if (!compiled->ok) {
            if (!failed_) {
                failed_ = true;
                compile_error_ = *compiled;
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
        for (const std::filesystem::path & target : needed) { instantiate(target); }
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
    [[nodiscard]] const std::string & resolution_error() const noexcept { return resolution_; }

private:
    void note(std::string message) {
        if (resolution_.empty()) { resolution_ = std::move(message); }
    }

    context & cx_;
    std::vector<std::unique_ptr<program>> owned_;
    bool failed_ = false;
    program compile_error_;
    std::string resolution_;
};

struct options {
    std::vector<std::filesystem::path> preludes;
    std::filesystem::path test;
    bool strict = false;
    bool module = false;
};

[[nodiscard]] bool parse_arguments(int argc, char ** argv, options & out) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--prelude" && i + 1 < argc) {
            out.preludes.emplace_back(argv[++i]);
        } else if (arg == "--strict") {
            out.strict = true;
        } else if (arg == "--module") {
            out.module = true;
        } else if (!arg.starts_with("--")) {
            out.test = std::filesystem::path{arg};
        } else {
            return false;
        }
    }
    return !out.test.empty();
}

} // namespace

int main(int argc, char ** argv) {
    options opts;
    if (!parse_arguments(argc, argv, opts)) {
        std::fprintf(stderr, "usage: ct262 [--prelude <harness.js>]... [--strict] [--module]"
                             " <test.js>\n"
                             "  runs ONE test262 file in a fresh realm. Exit 0 ran to completion,"
                             " 1 did not,\n"
                             "  3 the host failed. tools/check/test262.py is what drives it.\n");
        return 3;
    }

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
    for (const std::filesystem::path & prelude : opts.preludes) {
        bool ok = false;
        const std::string source = read_file(prelude, ok);
        if (!ok) {
            host_error("cannot read harness file " + prelude.string());
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
        const program & compiled = cx.own_program(ctbrowser::script::compiler::compile(source));
        if (!compiled.ok) {
            host_error(prelude.string() + ": " + compiled.error);
            return 3;
        }
        const run_result ran = cx.run(compiled);
        if (!ran.ok) {
            host_error(prelude.string() + ": " + ran.error);
            return 3;
        }
    }

    if (opts.module) {
        // `--strict` is not applied: a module is strict code by definition, and
        // the suite never sets both.
        module_loader loader{cx};
        loader.instantiate(opts.test);
        if (loader.failed()) {
            report_compile_failure(loader.compile_error());
            return 1;
        }
        if (!loader.resolution_error().empty()) {
            // The suite expects a SyntaxError from a resolution failure, which
            // is what a missing module or a missing export is here.
            report("resolution", "SyntaxError", "SyntaxError", loader.resolution_error());
            return 1;
        }
        const run_result ran = loader.evaluate(opts.test);
        if (!ran.ok) {
            report_run_failure(cx, "runtime", ran);
            return 1;
        }
        return 0;
    }

    bool ok = false;
    std::string source = read_file(opts.test, ok);
    if (!ok) {
        host_error("cannot read " + opts.test.string());
        return 3;
    }
    // The suite's own transformation, character for character: the directive,
    // a semicolon and a newline, before anything else the metadata asks for.
    if (opts.strict) { source = "\"use strict\";\n" + source; }

    const program & compiled = cx.own_program(ctbrowser::script::compiler::compile(source));
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
