#pragma once
// THE ENGINE SEAM, for every workload test (Bootstrap, p5, Phaser, Babylon).
//
// A workload test keeps ALL of its JavaScript in .js and .html files beside it,
// as named functions, and reaches that JavaScript only through `call()`. Its C++
// never builds or evaluates JavaScript text: no run_script, no ask(), no inline
// <script> in a C++ string, no alert hook for answers.
//
// That is what lets the same test run on two engines. ctcompile compiles ahead
// of time, so it can only compile JavaScript that exists as source at build
// time - which these files are - and a native engine will call the compiled
// functions directly. tools/check/check-workload.py's native-run stage runs the
// same CTest with CTBROWSER_TEST_ENGINE=native. Until ctcompile can provide that
// engine, selecting it fails loudly instead of quietly running the VM.
//
// WHAT THE SEAM DOES NOT COVER YET: loading the page, ticking frames, reading
// pixels and dispatching input still go through ctbrowser::shell::browser, which
// links the VM. A native run needs a native host for those too; its shape is
// for the compiler work to decide, so it is not guessed at here.

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

#include <ctbrowser.hpp>

namespace ctbrowser_test {

class engine {
public:
    virtual ~engine() = default;
    // Call a global function of the page's scripts with string arguments. The
    // answer is String(result), or "threw: <message>" when the call throws.
    [[nodiscard]] virtual std::string call(std::string_view function,
                                           std::initializer_list<std::string_view> args = {}) = 0;
    [[nodiscard]] virtual const char * name() const = 0;
};

// A JavaScript string literal for `text`, the one piece of JavaScript the VM
// engine writes: the call expression around the test's own function.
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

// The interpreter: the call runs inside the page and its answer comes back on
// the console, the one channel both engines will share. The console is append-
// only, so the answer is the marked line added by this call - not `back()`,
// which would pick up anything the library logged in between.
class vm_engine final : public engine {
public:
    explicit vm_engine(ctbrowser::browser & page) : page_{page} {}

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

// The engine CTBROWSER_TEST_ENGINE names: unset or "vm" is the interpreter.
// "native" has no implementation yet, and anything else is a typo; both end the
// test with a FAIL rather than measure the wrong engine.
[[nodiscard]] inline std::unique_ptr<engine> make_engine(ctbrowser::browser & page) {
    const char * chosen = std::getenv("CTBROWSER_TEST_ENGINE");
    const std::string_view kind = chosen == nullptr ? "vm" : chosen;
    if (kind.empty() || kind == "vm") { return std::make_unique<vm_engine>(page); }
    std::printf("FAIL CTBROWSER_TEST_ENGINE=%s: no such engine in this test yet (the native "
                "engine comes from ctcompile; see tools/check/check-workload.py)\n",
                chosen);
    std::exit(1);
}

} // namespace ctbrowser_test
