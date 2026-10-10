//===- ctbrowser/test/support/native_engine.hpp - Native test engine ------===//
//
// The engine a test built with CTBROWSER_TEST_NATIVE uses: the functions
// ctcompile compiled from the test's scripts, called directly, over a
// page::static_page. No interpreter is linked.
//
// The compiled scripts are listed in `native_scripts`, which the build defines
// in a generated translation unit (ctcompile/test/CTNative/Workloads/): one
// entry per script, with its top level and its string entries, each wrapped so
// it runs with the page bound as the document. This header names no ctcompile
// type, so ctbrowser's test support stays free of the compiler.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <ctbrowser/page/static_page.hpp>

namespace ctbrowser_test {

/// One string entry of a compiled script: `css_dump_table` and its C++
/// function, called with the page bound as the document.
struct native_entry {
    std::string_view name;
    std::string (*call)(ctbrowser::page::static_page &, std::span<const std::string_view>);
};

/// One compiled script: its source path as the build saw it (absolute), its
/// top level and its string entries.
struct native_script {
    std::string_view path;
    int (*top_level)(ctbrowser::page::static_page &);
    std::span<const native_entry> entries;
};

/// Every script compiled into this executable; defined by the build.
extern const std::span<const native_script> native_scripts;

/// The native engine: load() runs a compiled script's top level, call()
/// calls one of its string entries. A script that was not compiled into the
/// executable cannot be loaded.
class native_engine final : public engine {
public:
    explicit native_engine(ctbrowser::page::static_page & page) : page_{page} {}

    [[nodiscard]] std::string load(const std::string & path) override {
        std::error_code error;
        const std::filesystem::path wanted = std::filesystem::weakly_canonical(path, error);
        for (const native_script & script : native_scripts) {
            if (error || std::filesystem::weakly_canonical(script.path, error) != wanted) {
                continue;
            }
            try {
                if (script.top_level(page_) != 0) { return path + ": its top level failed"; }
            } catch (const std::exception & thrown) { return path + " threw: " + thrown.what(); }
            loaded_.push_back(&script);
            return {};
        }
        return "no native translation unit for " + path + " in this executable";
    }

    [[nodiscard]] std::string call(std::string_view function,
                                   std::initializer_list<std::string_view> args = {}) override {
        for (const native_script * script : loaded_) {
            for (const native_entry & entry : script->entries) {
                if (entry.name != function) { continue; }
                try {
                    return entry.call(page_, std::span{args.begin(), args.size()});
                } catch (const std::exception & thrown) {
                    return std::string{"threw: "} + thrown.what();
                }
            }
        }
        return "<no answer>";
    }
    [[nodiscard]] const char * name() const override { return "native"; }

private:
    ctbrowser::page::static_page & page_;
    std::vector<const native_script *> loaded_;
};

/// Returns the native engine when CTBROWSER_TEST_ENGINE is "native". This
/// executable has no interpreter: unset or "vm" means the test should have
/// been run as `<name>`, and anything else is a typo; both FAIL.
[[nodiscard]] inline std::unique_ptr<engine> make_engine(ctbrowser::page::static_page & page) {
    const char * chosen = std::getenv("CTBROWSER_TEST_ENGINE");
    const std::string_view kind = chosen == nullptr ? "" : chosen;
    if (kind == "native") { return std::make_unique<native_engine>(page); }
    std::printf("FAIL CTBROWSER_TEST_ENGINE=%s: this executable has no interpreter; run "
                "`<name>` for the VM\n",
                chosen == nullptr ? "(unset)" : chosen);
    std::exit(1);
}

} // namespace ctbrowser_test
