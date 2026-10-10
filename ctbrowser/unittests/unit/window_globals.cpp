//===- window_globals.cpp - Window global tests -----------------*- C++ -*-===//
//
// A script's `var` is a property of the window that `delete` cannot remove.
//
// ECMA-262 16.1.7 GlobalDeclarationInstantiation binds a script's `var`s, and
// a block function's Annex B var, with CreateGlobalVarBinding(name, false):
// enumerable, not configurable. An eval's are configurable (19.2.1.3), and a
// global made by plain assignment is an ordinary configurable property that a
// `delete` really removes (realm_record::declared, the window view's traps).
// Stands in for test262's language/global-code/decl-var.js and the Annex B
// global-init files, seen through the Window instead of the bare VM global.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

int main() {
    browser page{browser_options{400, 300}};
    page.load_html(R"html(<!doctype html><html><body><script>
        var declared = 1;
        assigned = 2;
        (0, eval)('var fromEval = 3;');
        { function inBlock() {} }
        const d = n => {
            const p = Object.getOwnPropertyDescriptor(window, n);
            return n + ':' + p.enumerable + '/' + p.configurable;
        };
        const seen = [d('declared'), d('assigned'), d('fromEval'), d('inBlock'),
                      'delete:' + [delete window.declared, delete window.assigned,
                                   delete window.fromEval].join('/'),
                      'after:' + [typeof declared, typeof assigned, typeof fromEval].join('/')];
        console.log(seen.join(' '));
        </script></body></html>)html");
    CHECK_EQ(page.script_error(), std::string{});
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (!logged.empty()) {
        CHECK_EQ(logged.back(),
                 std::string{"declared:true/false assigned:true/true "
                             "fromEval:true/true inBlock:true/false "
                             "delete:false/true/true after:number/undefined/undefined"});
    }
    REPORT("window_globals");
}
