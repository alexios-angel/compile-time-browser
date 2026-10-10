//===- ctbrowser/test/support/engine.hpp - Workload test engine seam ------===//
//
// The engine seam for every workload test (Bootstrap, p5, Phaser, Babylon).
//
// A workload test keeps all of its JavaScript in .js and .html files beside it,
// as named functions, and reaches that JavaScript only through `call()`. Its C++
// never builds or evaluates JavaScript text: no run_script, no ask(), no inline
// <script> in a C++ string, no alert hook for answers.
//
// That lets the same test run on two engines. ctcompile compiles ahead of
// time, so it can only compile JavaScript that exists as source at build time
// (these files), and a native engine will call the compiled functions
// directly. tools/check/check-workload.py will run each test natively as
// `<name>.native`: the same executable with CTBROWSER_TEST_ENGINE=native.
// Until ctcompile provides that engine, selecting it fails loudly instead of
// quietly running the VM.
//
// A test built with CTBROWSER_TEST_NATIVE defined links no VM: its page is a
// page::static_page and its engine is native_engine.hpp's, which calls what
// ctcompile compiled from the test's scripts. That build is registered as
// `<name>.native` beside `<name>` and checks the same baselines. Without the
// definition the page is a shell::browser and the engine is the interpreter.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

#include "check.hpp"

#ifdef CTBROWSER_TEST_NATIVE
#include <ctbrowser/page/static_page.hpp>
#else
#include <ctbrowser.hpp>
#endif

namespace ctbrowser_test {

/// How a workload test reaches its page's JavaScript; see the file comment.
class engine {
public:
    virtual ~engine() = default;
    /// Runs one of the test's script files in the page as a classic script: its
    /// top-level code runs and its functions become globals. For a page that
    /// must stay as it is (a fixture other tools load too); a page the test
    /// owns lists its scripts itself. Returns empty on success, else the error.
    [[nodiscard]] virtual std::string load(const std::string & path) = 0;
    /// Calls a global function of the page's scripts with string arguments. The
    /// answer is String(result), or "threw: <message>" when the call throws.
    [[nodiscard]] virtual std::string call(std::string_view function,
                                           std::initializer_list<std::string_view> args = {}) = 0;
    /// The engine's name, "vm" or "native".
    [[nodiscard]] virtual const char * name() const = 0;
};

/// Quotes `text` as a JavaScript string literal. The VM engine uses it to build
/// the call expression around the test's own function, the one piece of
/// JavaScript it writes.
[[nodiscard]] inline std::string js_string(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char escaped[8];
                std::snprintf(escaped, sizeof escaped, "\\u%04x", static_cast<unsigned>(c));
                out += escaped;
            } else {
                out += c;
            }
        }
    }
    return out + "\"";
}

#ifndef CTBROWSER_TEST_NATIVE

/// The interpreter engine. The call runs inside the page and its answer comes
/// back on the console, the one channel both engines will share. The console
/// is append-only, so the answer is the marked line added by this call, not
/// `back()`, which would pick up anything the library logged in between.
class vm_engine final : public engine {
public:
    explicit vm_engine(ctbrowser::browser & page) : page_{page} {}

    [[nodiscard]] std::string load(const std::string & path) override {
        const std::string source = read_file(path);
        if (source.empty()) { return "cannot read " + path; }
        if (page_.run_script(source)) { return {}; }
        const std::string & error = page_.script_error();
        return error.empty() ? path + " did not run" : error;
    }

    [[nodiscard]] std::string call(std::string_view function,
                                   std::initializer_list<std::string_view> args = {}) override {
        std::string expression{function};
        expression += '(';
        bool first = true;
        for (const std::string_view arg : args) {
            if (!first) { expression += ", "; }
            expression += js_string(arg);
            first = false;
        }
        expression += ')';
        const std::size_t before = page_.bindings().console_output().size();
        (void)page_.run_script("try { console.log('" + std::string{marker} + "' + String(" +
                               expression + ")); } catch (e) { console.log('" +
                               std::string{marker} +
                               "threw: ' + (e && e.message ? e.message : e)); }");
        const auto & said = page_.bindings().console_output();
        for (std::size_t i = said.size(); i-- > before;) {
            if (said[i].starts_with(marker)) { return said[i].substr(marker.size()); }
        }
        return "<no answer>";
    }
    [[nodiscard]] const char * name() const override { return "vm"; }

private:
    static constexpr std::string_view marker = "=engine=";
    ctbrowser::browser & page_;
};

/// Returns the interpreter when CTBROWSER_TEST_ENGINE is unset or "vm". This
/// executable has no native engine: "native" means the test should have been
/// run as `<name>.native`, and anything else is a typo; both end the test with
/// a FAIL rather than measure the wrong engine.
[[nodiscard]] inline std::unique_ptr<engine> make_engine(ctbrowser::browser & page) {
    const char * chosen = std::getenv("CTBROWSER_TEST_ENGINE");
    const std::string_view kind = chosen == nullptr ? "vm" : chosen;
    if (kind.empty() || kind == "vm") { return std::make_unique<vm_engine>(page); }
    std::printf("FAIL CTBROWSER_TEST_ENGINE=%s: this executable links the interpreter only; "
                "the native build of this test is `<name>.native`\n",
                chosen);
    std::exit(1);
}

#endif

} // namespace ctbrowser_test

#ifdef CTBROWSER_TEST_NATIVE
#include "native_engine.hpp"
#endif
