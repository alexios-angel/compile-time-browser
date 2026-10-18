//===- WholePage.h - The page a module is compiled for ----------*- C++ -*-===//
//
// The compilation model, stated once: a compiled module is every script its
// page will ever run. The page driver compiles all of the page's <script>
// elements and inline `on*` handler attributes into the one module, checks
// that it took every one, and records that on the module as kWholePage
// (`ctjs-translate --whole-page`, which pipeline.cmake, native-claims.py and
// check-workload.py pass). No other script ever runs in the page's realm, so
// none calls the module's exported API, adds listeners, defines or rebinds
// globals, or passes callbacks into it. The browser host still calls in
// (listeners, timers, toString and valueOf, toJSON), and outside data still
// arrives (postMessage, location, storage).
//
// Passes read the property through wholePage() and never assume it. What the
// model does not give for free still needs proof: an element whose id or
// name shadows a global (HTML named access on Window), built-ins the module
// rebinds itself (clause 5), and code the module creates at run time, which
// markRuntimeCode() finds: clause 5 counts it as a dynamic global write, and
// native admission refuses it, since a native program cannot run new code.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <string>

namespace ctcompile::ctjs {

/// The module attribute the page driver sets: a dictionary whose `scripts` are
/// the scripts it compiled, in document order.
inline constexpr llvm::StringLiteral kWholePage = "ctjs.whole_page";

/// Whether the page driver recorded `module` as every script its page will
/// ever run.
[[nodiscard]] bool wholePage(mlir::ModuleOp module);

/// Set on an operation that may create code at run time, with the reason.
/// Native admission refuses the function holding it with that reason.
inline constexpr llvm::StringLiteral kRuntimeCode = "ctjs.runtime_code";

/// An operation that may create code at run time, and why.
struct RuntimeCodeSite {
    mlir::Operation * op;
    std::string why;
};

/// Finds every operation in `module` that may create code at run time, marks
/// it kRuntimeCode, and returns them in walk order:
///  - a load of `eval` or `Function`, and `import()`;
///  - `setTimeout` or `setInterval` whose first argument is not a function made
///    here, since a String is compiled;
///  - `createElement` with a tag that may be `script`, or a frame or plugin
///    element, whose document runs its own scripts;
///  - `document.write`, `writeln` and `createContextualFragment`, which can
///    add a script;
///  - markup or an attribute that may add an `on*` handler: `innerHTML`,
///    `outerHTML` or `insertAdjacentHTML` with markup that is not a constant
///    free of `on*=`, `parseFromString` with markup not known at build time,
///    and `setAttribute` with a name that may start with "on";
///  - a `javascript:` URL: a constant that is one, or a link, form action or
///    location set to a string not known at build time.
/// A shape not listed (a `.constructor` read that may be `Function`) is clause
/// 5's to follow.
llvm::SmallVector<RuntimeCodeSite> markRuntimeCode(mlir::ModuleOp module);

} // namespace ctcompile::ctjs
