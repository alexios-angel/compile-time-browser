//===- Preparation.h - DOM entry preparation entry points -------*- C++ -*-===//
//
// Declares the steps that ready a DOM entry for native code: closure lifting
// for classes, class-initialization normalization, and prepareDOMEntry, which
// combines them and reproves the result.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTNative/Analysis/HostContract.h"

namespace ctcompile::ctnative {

/// Normalizes a fingerprinted DOM entry (URI, helper, element and iteration
/// rewrites) on a private clone, then re-runs the DOM entry proof on it. The
/// module and the refreshed contract are replaced together only when that
/// proof passes; on refusal both are unchanged. Passing does not by itself
/// prove ownership or permit a native representation.
llvm::Error prepareDOMEntry(mlir::ModuleOp module, HostContract & contract, unsigned maxSteps);

/// Runs closure lifting on a private candidate module holding DOM classes,
/// refusing when the module is too large for `maxSteps`. Lifting proves
/// nothing: the full typed DOM proof must still pass afterwards.
llvm::Error liftDOMClasses(mlir::ModuleOp module, unsigned maxSteps);

/// Rewrites class-definition code to plain initialization, only on
/// prepareDOMEntry's private candidate. The caller must prove all remaining
/// DOM effects again before publishing the result.
llvm::Error normalizeDOMClasses(mlir::ModuleOp module, HostContract & contract, unsigned maxSteps);

} // namespace ctcompile::ctnative
